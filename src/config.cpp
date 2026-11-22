#include "config.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <cstring>

#include "link.h"
#include "text.h"

namespace livemap {

namespace {

std::string random_key() {
    unsigned char bytes[12] = {};
    BCryptGenRandom(nullptr, bytes, sizeof bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    static constexpr char kAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
    std::string key;
    for (unsigned char b : bytes) key += kAlphabet[b % (sizeof kAlphabet - 1)];
    return key;
}

std::string narrow(const wchar_t* s) {
    std::string out;
    for (; *s; ++s) out += (*s < 128) ? static_cast<char>(*s) : '?';
    return out;
}

std::string utf8(const wchar_t* s, int length) {
    if (length == 0) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, s, length, nullptr, 0, nullptr, nullptr);
    std::string out(bytes, '\0');
    if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, s, length, out.data(), bytes, nullptr, nullptr);
    return text::clean_utf8(out);
}

}  // namespace

std::string read_title(const std::wstring& dir) {
    const auto trim = [](std::string_view value) {
        const size_t first = value.find_first_not_of(" \t\r\n");
        return first == value.npos ? std::string_view{} :
            value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    };
    const std::wstring path = dir + L"\\livemap.ini";
    std::string bytes;
    if (FILE* file = _wfopen(path.c_str(), L"rb")) {
        char chunk[4096];
        size_t count;
        while ((count = std::fread(chunk, 1, sizeof chunk, file)) > 0 && bytes.size() <= 1024 * 1024)
            bytes.append(chunk, count);
        std::fclose(file);
    }
    if (bytes.size() > 1024 * 1024) return {};
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.erase(0, 3);
    // Win32 profile APIs treat UTF-8 as the legacy system code page. Parse valid
    // UTF-8 directly; retain their native UTF-16/ANSI handling for other files.
    if (!bytes.empty() && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
            static_cast<int>(bytes.size()), nullptr, 0)) {
        wchar_t value[2048]{};
        const DWORD length = GetPrivateProfileStringW(L"livemap", L"Title", L"", value, 2048, path.c_str());
        return std::string(trim(utf8(value, static_cast<int>(length))));
    }
    const auto equal = [](std::string_view a, std::string_view b) {
        return a.size() == b.size() && _strnicmp(a.data(), b.data(), a.size()) == 0;
    };
    bool section = false;
    std::string_view remaining(bytes);
    while (!remaining.empty()) {
        const size_t newline = remaining.find('\n');
        const auto line = trim(remaining.substr(0, newline));
        remaining = newline == remaining.npos ? std::string_view{} : remaining.substr(newline + 1);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = equal(trim(line.substr(1, line.size() - 2)), "livemap");
            continue;
        }
        const size_t equals = line.find('=');
        if (!section || equals == line.npos || !equal(trim(line.substr(0, equals)), "Title")) continue;
        auto value = trim(line.substr(equals + 1));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
            value = value.substr(1, value.size() - 2);
        return text::clean_utf8(trim(value));
    }
    return {};
}

Config load_config(const std::wstring& dir) {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const wchar_t* name = wcsrchr(exe, L'\\');
    const bool local_game = _wcsicmp(name ? name + 1 : exe, L"enshrouded.exe") == 0;
    std::wstring path = dir + L"\\livemap.ini";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::string text =
            "; Live map for Enshrouded dedicated servers.\n"
            "; The link to share is written to livemap.log (\"map link: ...\") and shown in the chat\n"
            "; when a player joins. Settings except Title take effect after a server restart.\n"
            "[livemap]\n"
            "Enabled=1\n"
            "; Browser tab title. Empty or missing = Enshrouded Live Map.\n"
            "Title=\n"
            "; auto = the same number as the game's queryPort in enshrouded_server.json, but TCP.\n"
            "; Open that port for TCP too (router: TCP/UDP; Docker: \"<port>:<port>/tcp\"), or set a number.\n"
            "Port=auto\n"
            "; Address people use from the internet. Empty = ask api.ipify.org for the public IP\n"
            "; (once an hour). Set it for a hostname or a tunnel, e.g. https://map.example.com\n"
            "PublicUrl=\n"
            "; 0 = never ask api.ipify.org (then set PublicUrl, or the link stays unknown).\n"
            "DetectPublicAddress=" + std::string(local_game ? "0" : "1") + "\n"
            "; 1 = the chat shows the map link when a player joins (everyone online sees it).\n"
            "AnnounceInChat=" + std::string(local_game ? "0" : "1") + "\n"
            "; 0.0.0.0 = all network interfaces, 127.0.0.1 = only this machine.\n"
            "Bind=" + std::string(local_game ? "127.0.0.1" : "0.0.0.0") + "\n"
            "; Set RequireKey=1 to restrict access to people with the key.\n"
            "RequireKey=0\n"
            "; Key in the link when required. Use 3 to 64 letters, digits, - or _ (case does not\n"
            "; matter). Players can type <address>/<Key>, e.g. 203.0.113.5:15637/ourmap. Short words are\n"
            "; easier to type but easier to guess. Everyone with the key sees all player positions;\n"
            "; change it to lock old links out.\n"
            "Key=" + random_key() + "\n"
            "; Milliseconds between updates of the page (100 to 5000).\n"
            "UpdateMs=250\n"
            "Log=1\n"
            "; 1 = observe real player tombstones and their loot-removal state.\n"
            "Tombstones=1\n"
            "; 1 = log time and positions every 30 s (to check the mod reads the right values).\n"
            "LogDiagnostics=1\n";
        if (FILE* f = _wfopen(path.c_str(), L"wb")) {
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        }
    }
    Config c;
    c.local_game = local_game;
    c.enabled = GetPrivateProfileIntW(L"livemap", L"Enabled", 1, path.c_str()) != 0;
    wchar_t buf[512];
    c.title = read_title(dir);
    GetPrivateProfileStringW(L"livemap", L"Port", L"auto", buf, 32, path.c_str());
    std::string port = narrow(buf);
    int number = std::atoi(port.c_str());
    std::wstring json_path = dir + L"\\enshrouded_server.json";
    if (FILE* f = _wfopen(json_path.c_str(), L"rb")) {
        std::string json;
        char chunk[4096];
        size_t n;
        while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0 && json.size() < 1 << 20) json.append(chunk, n);
        std::fclose(f);
        c.game_query_port = link::query_port(json);
    }
    if (c.title.find_first_not_of(' ') == std::string::npos) c.title.clear();
    if (port.empty() || port == "auto" || number == 0) {
        c.port_from_game = true;
        c.port = c.game_query_port;
        if (c.port == 0) c.port = 15680;
    } else {
        c.port = number >= 1 && number <= 65535 ? number : 15680;
    }
    GetPrivateProfileStringW(L"livemap", L"PublicUrl", L"", buf, 512, path.c_str());
    c.public_url = narrow(buf);
    c.detect_public_address = GetPrivateProfileIntW(L"livemap", L"DetectPublicAddress", local_game ? 0 : 1, path.c_str()) != 0;
    c.announce_in_chat = GetPrivateProfileIntW(L"livemap", L"AnnounceInChat", local_game ? 0 : 1, path.c_str()) != 0;
    GetPrivateProfileStringW(L"livemap", L"Bind", local_game ? L"127.0.0.1" : L"0.0.0.0", buf, 128, path.c_str());
    c.bind = narrow(buf);
    if (local_game && c.public_url.empty() && !c.detect_public_address)
        c.public_url = "http://localhost:" + std::to_string(c.port);
    c.require_key = GetPrivateProfileIntW(L"livemap", L"RequireKey", 0, path.c_str()) != 0;
    if (c.require_key) {
        GetPrivateProfileStringW(L"livemap", L"Key", L"", buf, 128, path.c_str());
        c.key = narrow(buf);
    }
    c.update_ms = static_cast<int>(GetPrivateProfileIntW(L"livemap", L"UpdateMs", 250, path.c_str()));
    if (c.update_ms < 100) c.update_ms = 100;
    if (c.update_ms > 5000) c.update_ms = 5000;
    c.log = GetPrivateProfileIntW(L"livemap", L"Log", 1, path.c_str()) != 0;
    c.log_diagnostics = GetPrivateProfileIntW(L"livemap", L"LogDiagnostics", 1, path.c_str()) != 0;
    c.finds = GetPrivateProfileIntW(L"livemap", L"Finds", 1, path.c_str()) != 0;
    c.bases = GetPrivateProfileIntW(L"livemap", L"Bases", 1, path.c_str()) != 0;
    c.discovered = GetPrivateProfileIntW(L"livemap", L"Discovered", 1, path.c_str()) != 0;
    c.deaths = GetPrivateProfileIntW(L"livemap", L"Deaths", 1, path.c_str()) != 0;
    c.objects = GetPrivateProfileIntW(L"livemap", L"Objects", 1, path.c_str()) != 0;
    c.tombstones = GetPrivateProfileIntW(L"livemap", L"Tombstones", 1, path.c_str()) != 0;
    c.chat_probe = GetPrivateProfileIntW(L"livemap", L"ChatProbe", 0, path.c_str()) != 0;
    return c;
}

}  // namespace livemap
