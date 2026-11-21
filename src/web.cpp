#include "web.h"
#include "guardian_assets.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <string>
#include <thread>
#include <io.h>
#include <limits>
#include <mutex>
#include <zlib.h>

#include "link.h"
#include "log.h"
#include "text.h"
#include "world.h"

namespace livemap::web {

namespace {

constexpr int kMaxClients = 32;

Config g_config;
std::wstring g_web_dir;
std::wstring g_guardian_dir;
std::wstring g_config_dir;
std::mutex g_title_mutex;
std::string g_title;
uint64_t g_title_checked = 0;
std::atomic<int> g_clients{0};

std::string page_title() {
    std::lock_guard lock(g_title_mutex);
    const uint64_t now = GetTickCount64();
    if (!g_title_checked || now - g_title_checked >= 5000) {
        g_title = read_title(g_config_dir);
        g_title_checked = now;
    }
    return g_title.empty() ? "Enshrouded Live Map" : g_title;
}

std::string snapshot_json() {
    auto state = world::snapshot_json();
    state.insert(1, "\"title\":" + text::quote_json(page_title()) + ",");
    return state;
}

bool send_all(SOCKET s, const char* data, size_t size) {
    while (size > 0) {
        int n = send(s, data, static_cast<int>(size > 65536 ? 65536 : size), 0);
        if (n <= 0) return false;
        data += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool send_response(SOCKET s, const char* status, const char* type, const std::string& body,
                   const char* cache = "no-store", const char* extra = "") {
    char head[400];
    const bool no_body = std::strncmp(status, "304", 3) == 0 || std::strncmp(status, "204", 3) == 0;
    const std::string length = no_body ? "" : "Content-Length: " + std::to_string(body.size()) + "\r\n";
    int n = std::snprintf(head, sizeof head,
                          "HTTP/1.1 %s\r\nContent-Type: %s\r\n%s"
                          "Cache-Control: %s\r\n%sConnection: close\r\n\r\n",
                          status, type, length.c_str(), cache, extra);
    return send_all(s, head, static_cast<size_t>(n)) && send_all(s, body.data(), body.size());
}

std::string request_header(const std::string& request, const char* name) {
    const std::string prefix = std::string(name) + ":";
    size_t line = request.find("\r\n");
    while (line != std::string::npos) {
        line += 2;
        const size_t end = request.find("\r\n", line);
        if (end == std::string::npos || end == line) break;
        if (_strnicmp(request.c_str() + line, prefix.c_str(), prefix.size()) == 0) {
            const size_t start = request.find_first_not_of(" \t", line + prefix.size());
            if (start == std::string::npos || start >= end) return {};
            const size_t last = request.find_last_not_of(" \t", end - 1);
            return request.substr(start, last - start + 1);
        }
        line = end;
    }
    return {};
}

bool compress_gzip(const std::string& input, std::string& output) {
    if (input.size() > std::numeric_limits<uInt>::max()) return false;
    z_stream stream{};
    if (deflateInit2(&stream, Z_BEST_SPEED, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
    output.resize(deflateBound(&stream, static_cast<uLong>(input.size())));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = deflate(&stream, Z_FINISH);
    output.resize(stream.total_out);
    deflateEnd(&stream);
    return result == Z_STREAM_END;
}

const char* content_type(const std::string& path) {
    auto ends = [&](const char* ext) {
        size_t n = std::strlen(ext);
        return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".js")) return "text/javascript; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".json")) return "application/json";
    if (ends(".png")) return "image/png";
    if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
    if (ends(".webp")) return "image/webp";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".woff2")) return "font/woff2";
    if (ends(".txt")) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

bool read_file(const std::string& rel, std::string& out) {
    std::wstring path = g_web_dir;
    for (char c : rel) path += (c == '/') ? L'\\' : static_cast<wchar_t>(c);
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (f == nullptr) return false;
    char buf[65536];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

// Value of `name` in the query string, or "".
std::string query_param(const std::string& query, const char* name) {
    std::string key = std::string(name) + "=";
    size_t pos = 0;
    while (pos < query.size()) {
        size_t end = query.find('&', pos);
        if (end == std::string::npos) end = query.size();
        if (query.compare(pos, key.size(), key) == 0) return query.substr(pos + key.size(), end - pos - key.size());
        pos = end + 1;
    }
    return {};
}

// True if the request's Accept-Encoding header lists gzip.
bool accepts_gzip(const std::string& req) {
    size_t pos = 0;
    while ((pos = req.find("\n", pos)) != std::string::npos) {
        ++pos;
        if (req.compare(pos, 16, "Accept-Encoding:") == 0 || req.compare(pos, 16, "accept-encoding:") == 0) {
            size_t end = req.find("\r\n", pos);
            return req.substr(pos, end == std::string::npos ? std::string::npos : end - pos).find("gzip") != std::string::npos;
        }
    }
    return false;
}

// Only plain file names and one folder level: letters, digits, '-', '_', '.', '/', no "..".
bool safe_path(const std::string& p) {
    if (p.empty() || p.find("..") != std::string::npos || p[0] == '/') return false;
    for (char c : p) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '/')) return false;
    }
    return true;
}

void stream_events(SOCKET s) {
    const char* head =
        "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\n"
        "Connection: keep-alive\r\nX-Accel-Buffering: no\r\n\r\n";
    if (!send_all(s, head, std::strlen(head))) return;
    for (;;) {
        std::string msg = "data: " + snapshot_json() + "\n\n";
        if (!send_all(s, msg.data(), msg.size())) return;
        Sleep(static_cast<DWORD>(g_config.update_ms));
    }
}

void handle(SOCKET s) {
    DWORD timeout = 5000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
    std::string req;
    char buf[2048];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 16384) {
        int n = recv(s, buf, sizeof buf, 0);
        if (n <= 0) return;
        req.append(buf, n);
    }
    size_t sp1 = req.find(' ');
    size_t sp2 = sp1 == std::string::npos ? std::string::npos : req.find(' ', sp1 + 1);
    if (sp2 == std::string::npos || req.compare(0, sp1, "GET") != 0) {
        send_response(s, "405 Method Not Allowed", "text/plain", "GET only\n");
        return;
    }
    std::string target = req.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string path = target, query;
    if (size_t q = target.find('?'); q != std::string::npos) {
        path = target.substr(0, q);
        query = target.substr(q + 1);
    }
    // The short link: /<key> (typed from the chat) goes to the page with ?key=.
    if (query.empty() && path.size() > 1 && !g_config.key.empty() && link::key_equal(path.substr(1), g_config.key)) {
        send_response(s, "302 Found", "text/plain", "", "no-store", ("Location: /?key=" + g_config.key + "\r\n").c_str());
        return;
    }
    if (g_config.require_key && (g_config.key.empty() || !link::key_equal(query_param(query, "key"), g_config.key))) {
        send_response(s, "403 Forbidden", "text/plain", "Open the map with ?key=<Key from livemap.ini>\n");
        return;
    }
    if (path == "/state") {
        send_response(s, "200 OK", "application/json", snapshot_json());
        return;
    }
    if (path.starts_with("/assets/")) {
        const std::string rel = path.substr(8);
        if (!guardian_assets::valid_path(rel)) {
            send_response(s, "404 Not Found", "text/plain", "Asset unavailable"); return;
        }
        std::wstring file = g_guardian_dir + L"guardian\\assets\\";
        for (char c : rel) file += c == '/' ? L'\\' : static_cast<wchar_t>(c);
        FILE* f = _wfopen(file.c_str(), L"rb");
        if (!f) { send_response(s, "404 Not Found", "text/plain", "Asset unavailable"); return; }
        std::string body;
        char chunk[65536]; size_t count;
        while ((count = std::fread(chunk, 1, sizeof chunk, f)) > 0) body.append(chunk, count);
        const bool ok = !std::ferror(f); std::fclose(f);
        if (!ok) { send_response(s, "500 Internal Server Error", "text/plain", "Could not read asset"); return; }
        if (rel.ends_with("/viewer.css")) guardian_assets::rewrite_css(body, rel.substr(0, rel.find('/')), query_param(query, "key"));
        const char* cache = "private, max-age=31536000, immutable";
        std::string compressed;
        if ((rel.ends_with(".js") || rel.ends_with(".css")) && accepts_gzip(req) && compress_gzip(body, compressed))
            send_response(s, "200 OK", content_type(rel), compressed, cache, "Vary: Accept-Encoding\r\nContent-Encoding: gzip\r\n");
        else send_response(s, "200 OK", content_type(rel), body, cache, "Vary: Accept-Encoding\r\n");
        return;
    }
    if (path == "/character") {
        const std::string id = query_param(query, "id");
        if (id.empty() || id.size() > 20 || id.find_first_not_of("0123456789") != std::string::npos) {
            send_response(s, "404 Not Found", "text/plain", "Character sheet unavailable");
            return;
        }
        const std::wstring folder = std::wstring(id.begin(), id.end()) + L"\\";
        std::string body;
        FILE* f = nullptr;
        for (const wchar_t* root : {L"guardian\\", L"guardiao-dados\\"}) {
            for (const wchar_t* name : {L"server_save.html", L"personagem.html"}) {
                const std::wstring file = g_guardian_dir + root + folder + name;
                f = _wfopen(file.c_str(), L"rb");
                if (f) break;
            }
            if (f) break;
        }
        if (!f) {
            send_response(s, "404 Not Found", "text/plain", "Character sheet unavailable");
            return;
        }
        BY_HANDLE_FILE_INFORMATION info{};
        std::string etag;
        if (GetFileInformationByHandle(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(f))), &info)) {
            char tag[160];
            std::snprintf(tag, sizeof tag, "W/\"%lx-%lx-%lx-%lx-%lx-%lx-%lx\"",
                          info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow,
                          info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime,
                          info.nFileSizeHigh, info.nFileSizeLow);
            etag = tag;
        }
        const std::string headers = "Vary: Accept-Encoding\r\n" + (etag.empty() ? std::string{} : "ETag: " + etag + "\r\n");
        const char* cache = etag.empty() ? "no-store" : "private, no-cache";
        if (query_param(query, "check") == "1") {
            std::fclose(f);
            send_response(s, "204 No Content", "text/plain", "", "no-store", headers.c_str());
            return;
        }
        const std::string validator = request_header(req, "If-None-Match");
        if (!etag.empty() && (validator == etag || validator == "*")) {
            std::fclose(f);
            send_response(s, "304 Not Modified", "text/html; charset=utf-8", "", cache, headers.c_str());
            return;
        }
        char chunk[65536];
        size_t n;
        while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0) body.append(chunk, n);
        const bool ok = !std::ferror(f);
        std::fclose(f);
        if (!ok) {
            send_response(s, "500 Internal Server Error", "text/plain", "Could not read character sheet");
            return;
        }
        guardian_assets::rewrite_sheet(body, query_param(query, "key"));
        std::string compressed;
        if (accepts_gzip(req) && compress_gzip(body, compressed)) {
            send_response(s, "200 OK", "text/html; charset=utf-8", compressed, cache,
                          (headers + "Content-Encoding: gzip\r\n").c_str());
        } else {
            send_response(s, "200 OK", "text/html; charset=utf-8", body, cache, headers.c_str());
        }
        return;
    }
    if (path == "/finds") {
        send_response(s, "200 OK", "application/json", world::store().to_json());
        return;
    }
    if (path == "/explored") {
        send_response(s, "200 OK", "application/json", world::explored_store().to_json());
        return;
    }
    if (path == "/discovered") {
        send_response(s, "200 OK", "application/json", world::discovered_store().to_json());
        return;
    }
    if (path == "/objects") {
        send_response(s, "200 OK", "application/json", world::objects_store().objects_json());
        return;
    }
    if (path == "/creatures") {
        send_response(s, "200 OK", "application/json", world::objects_store().creatures_json());
        return;
    }
    if (path == "/deaths") {
        send_response(s, "200 OK", "application/json", world::deaths_store().to_json());
        return;
    }
    if (path == "/bases") {
        send_response(s, "200 OK", "application/json", world::bases_store().to_json(GetTickCount64()));
        return;
    }
    if (path == "/events") {
        stream_events(s);
        return;
    }
    std::string rel = path == "/" ? "index.html" : path.substr(1);
    if (!safe_path(rel)) {
        send_response(s, "404 Not Found", "text/plain", "not found\n");
        return;
    }
    // A pre-compressed twin (file.gz, written by tools/extract_pois.py) is sent as is to
    // browsers that accept gzip: the data files shrink to a fifth.
    std::string body;
    const char* encoding = "";
    if (rel != "index.html" && accepts_gzip(req) && read_file(rel + ".gz", body)) {
        encoding = "Content-Encoding: gzip\r\n";
    } else if (!read_file(rel, body)) {
        send_response(s, "404 Not Found", "text/plain", "not found\n");
        return;
    }
    if (rel == "index.html") {
        constexpr char opening[] = "<title>";
        const size_t start = body.find(opening);
        if (start != std::string::npos) {
            const size_t value = start + sizeof opening - 1;
            const size_t end = body.find("</title>", value);
            if (end != std::string::npos) body.replace(value, end - value, text::escape_html(page_title()));
        }
    }
    // Map images and icons change only with a new release, data files rarely: let browsers keep
    // them. The page itself is always fetched, so an update shows at the next load.
    const char* type = content_type(rel);
    const char* cache = std::strncmp(type, "image/", 6) == 0 ? "public, max-age=86400"
                        : rel == "index.html" ? "no-store" : "public, max-age=600";
    send_response(s, "200 OK", type, body, cache, encoding);
}

void serve(SOCKET listener) {
    for (;;) {
        SOCKET s = accept(listener, nullptr, nullptr);
        if (s == INVALID_SOCKET) {
            Sleep(100);
            continue;
        }
        if (g_clients.load() >= kMaxClients) {
            closesocket(s);
            continue;
        }
        ++g_clients;
        std::thread([s] {
            handle(s);
            shutdown(s, SD_BOTH);
            closesocket(s);
            --g_clients;
        }).detach();
    }
}

}  // namespace

bool start(const Config& config, const std::wstring& exe_dir, std::string& error) {
    g_config = config;
    g_config_dir = exe_dir;
    g_web_dir = exe_dir + L"\\livemap\\";
    g_guardian_dir = exe_dir + L"\\";
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        error = "WSAStartup failed";
        return false;
    }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        error = "socket failed";
        return false;
    }
    BOOL yes = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(config.port));
    if (inet_pton(AF_INET, config.bind.c_str(), &addr.sin_addr) != 1) {
        error = "invalid Bind address " + config.bind;
        closesocket(listener);
        return false;
    }
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(listener, 16) != 0) {
        error = "could not listen on " + config.bind + ":" + std::to_string(config.port) + " (error " +
                std::to_string(WSAGetLastError()) + ")";
        closesocket(listener);
        return false;
    }
    std::thread(serve, listener).detach();
    log("web map listening on %s:%d (TCP)", config.bind.c_str(), config.port);
    log("browser title: %s", page_title().c_str());
    return true;
}

}  // namespace livemap::web
