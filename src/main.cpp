#include "hardcore.h"
// Live map for dedicated servers and local games. Shroudtopia activates mods/livemap.dll;
// the alternative proxy build loads as iphlpapi.dll (Wine: iphlpapi=n,b).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string>

#include "a2s.h"

#include "config.h"
#include "log.h"
#ifndef LIVEMAP_SHROUDTOPIA
#include "proxy.h"
#endif
#include "scan.h"
#include "web.h"
#include "world.h"
#include "announce.h"
#include "link.h"
#include "public_ip.h"
#include "updates.h"

#include <mutex>

#include <cstdio>
#include <ctime>
#include <thread>

namespace {

constexpr const char* kVersion = LIVEMAP_VERSION;  // from project() in CMakeLists.txt
#ifndef LIVEMAP_SHROUDTOPIA
bool g_real_loaded = false;
#endif

std::wstring exe_dir() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, len);
    return dir.substr(0, dir.find_last_of(L"\\/"));
}

// The link to share: PublicUrl from livemap.ini, or the public IP asked once an hour.
std::mutex g_link_mutex;
std::string g_link;

std::string current_link() {
    std::lock_guard lock(g_link_mutex);
    return g_link;
}

void set_link(const std::string& value) {
    {
        std::lock_guard lock(g_link_mutex);
        if (g_link == value) return;
        g_link = value;
    }
    livemap::log("map link: %s", value.c_str());
    livemap::console({"Map address: " + value});
}

void start_link(const livemap::Config& config) {
    if (!config.public_url.empty()) {
        set_link(livemap::link::make(config.public_url, "", config.port, config.key));
        return;
    }
    if (!config.detect_public_address) {
        livemap::log("map link: unknown (DetectPublicAddress=0 and no PublicUrl); open port %d over TCP and share "
                     "http://<server address>:%d/%s", config.port, config.port, config.require_key ? "?key=<Key>" : "");
        return;
    }
    livemap::Config c = config;
    std::thread([c] {
        for (;;) {
            std::string error;
            std::string ip = livemap::public_ip::detect(error);
            if (!ip.empty()) {
                set_link(livemap::link::make("", ip, c.port, c.key));
                Sleep(60 * 60 * 1000);
            } else {
                livemap::log("map link: public address unknown (%s), retry in 5 minutes", error.c_str());
                Sleep(5 * 60 * 1000);
            }
        }
    }).detach();
}

bool read_file(const std::wstring& path, std::string& text) {
    text.clear();
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    return true;
}

// Written to a temp file and renamed into place, so a crash never leaves half a file.
bool write_file_atomic(const std::wstring& path, const std::string& text) {
    const std::wstring tmp = path + L".tmp";
    bool ok = false;
    if (FILE* f = _wfopen(tmp.c_str(), L"wb")) {
        ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        std::fclose(f);
    }
    return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Asks the server itself, like Steam's server browser does (A2S_INFO on the game's query port), for
// the keywords that start with the world's Flame level. -1 when there is no answer.
int query_flame_level(int port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return -1;
    DWORD timeout = 2000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
    std::string challenge;
    int level = -1;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::string request = livemap::a2s::info_request(challenge);
        sendto(s, request.data(), static_cast<int>(request.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
        char buf[1400];
        int n = recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;
        const std::string reply(buf, static_cast<size_t>(n));
        challenge = livemap::a2s::challenge_of(reply);
        if (!challenge.empty()) continue;  // the server wants its challenge sent back first
        level = livemap::a2s::flame_level(livemap::a2s::keywords_of(reply));
        break;
    }
    closesocket(s);
    return level;
}

void start_flame_level(int query_port) {
    if (query_port <= 0) {
        livemap::log("FLAME level unknown: no queryPort in enshrouded_server.json");
        return;
    }
    std::thread([query_port] {
        int misses = 0;
        for (;;) {
            int level = query_flame_level(query_port);
            if (level >= 0) {
                livemap::world::set_flame_level(level);
                misses = 0;
            } else if (++misses == 5) {
                livemap::log("FLAME level: no answer on query port %d yet (UDP, 127.0.0.1)", query_port);
            }
            Sleep(level >= 0 ? 60'000 : 15'000);
        }
    }).detach();
}

std::once_flag hardcore_install_once;
void start_hardcore(const livemap::Config& config) {
    if (config.enabled && !config.local_game)
        std::call_once(hardcore_install_once, [] { livemap::hardcore::install(reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr))); });
}
DWORD WINAPI start(LPVOID) {
    std::wstring dir = exe_dir();
    livemap::Config config = livemap::load_config(dir);
    livemap::log_open(dir, config.log);
    // Boot banner, matching Guardian's figlet "slant" presentation.
    const std::string signature = std::string("Enshrouded Live Map v") + kVersion +
        " | github.com/edtereza/enshrouded-livemap";
    livemap::console({
        R"(    __    _            __  ___            )",
        R"(   / /   (_)   _____  /  |/  /___ _____  )",
        R"(  / /   / / | / / _ \/ /|_/ / __ `/ __ \ )",
        R"( / /___/ /| |/ /  __/ /  / / /_/ / /_/ / )",
        R"(/_____/_/ |___/\___/_/  /_/\__,_/ .___/  )",
        R"(                              /_/       )",
        signature,
    }, false);
#ifdef LIVEMAP_SHROUDTOPIA
    livemap::log("livemap %s starting via Shroudtopia: Enabled=%d Port=%d", kVersion, config.enabled, config.port);
#else
    livemap::log("livemap %s starting: Enabled=%d Port=%d%s real iphlpapi %s", kVersion, config.enabled, config.port,
                 config.port_from_game ? " (auto: the game's queryPort, TCP)" : "", g_real_loaded ? "loaded" : "MISSING");
#endif
    livemap::Targets targets;
    std::string error;
    auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    const bool recognized = livemap::find_targets(base, targets, error);
    const std::string loaded = std::string("Enshrouded Live Map v") + kVersion + " loaded (" +
        (config.local_game ? "game build " : "server build ") +
        (recognized ? targets.build : "unrecognized") + ")";
    livemap::log("%s", loaded.c_str());
    livemap::console({loaded,
        config.local_game ? "Mode: local game / player-hosted world" : "Mode: dedicated server",
    }, false);
    if (recognized) start_hardcore(config);
    // Required and informational, including when an unsupported build prevents hooks.
    livemap::updates::check_on_startup(kVersion);
    if (config.require_key && !livemap::link::key_ok(config.key)) {
        livemap::log("WARNING: Key in livemap.ini should be 3 to 64 letters, digits, - or _ (and not state, events, "
                     "finds, icons, index, livemap); other characters can break the link");
    }
    if (!config.enabled) {
        livemap::log("disabled in livemap.ini");
        livemap::console({"Livemap is disabled in livemap.ini (Enabled=0)."});
        return 0;
    }
    if (!recognized || !livemap::world::install(targets, error)) {
        livemap::log("%s; no live map", error.c_str());
        livemap::console({"Livemap could not initialize: " + error, "The game/server will continue without the live map."});
        return 0;
    }
    if (config.finds) {
        livemap::world::install_mining(targets);
        livemap::world::install_chest(targets);
    } else {
        livemap::log("finds off in livemap.ini (Finds=0)");
    }
    if (config.bases) livemap::world::install_bases(targets);
    else livemap::log("Flame Altars off in livemap.ini (Bases=0)");
    if (config.chat_probe) livemap::world::enable_chat_probe(targets);
    std::string text;
    const std::wstring deaths_path = dir + L"\\livemap\\deaths.json";
    if (config.deaths) {
        livemap::world::install_deaths(targets);
        if (read_file(deaths_path, text) && livemap::world::deaths_store().from_json(text)) {
            livemap::log("deaths: %zu players loaded", livemap::world::deaths_store().players());
        }
    } else {
        livemap::log("death counts off in livemap.ini (Deaths=0)");
    }
    const std::wstring objects_path = dir + L"\\livemap\\objects.json";
    if (config.objects || config.tombstones) {
        if (read_file(objects_path, text) && livemap::world::objects_store().from_json(text)) {
            livemap::log("objects: %zu chests, %zu locks/switches loaded", livemap::world::objects_store().chests(),
                         livemap::world::objects_store().switches());
        }
    }
    if (config.objects) livemap::world::enable_objects();
    else {
        livemap::log("chests, switches and creatures off in livemap.ini (Objects=0)");
    }
    if (config.tombstones) livemap::world::enable_tombstones();
    const std::wstring finds_path = dir + L"\\livemap\\finds.json";
    if (read_file(finds_path, text)) {
        if (livemap::world::store().from_json(text)) {
            livemap::log("finds: %zu ore deposits, %zu chests loaded", livemap::world::store().deposits(),
                         livemap::world::store().chests());
        } else {
            livemap::log("finds: could not read livemap\\finds.json, starting empty (file left as is)");
        }
    }
    const std::wstring discovered_path = dir + L"\\livemap\\discovered.json";
    const std::wstring explored_path = dir + L"\\livemap\\explored.json";
    if (config.discovered) {
        if (!targets.legacy_knowledge_map) {
            livemap::log("DISCOVERED live updates off: server build %s uses a new player knowledge layout", targets.build);
        }
        if (read_file(discovered_path, text) && livemap::world::discovered_store().from_json(text)) {
            livemap::log("discovered: %zu players loaded", livemap::world::discovered_store().players());
        }
        if (read_file(explored_path, text) && livemap::world::explored_store().from_json(text)) {
            livemap::log("explored: %zu players loaded", livemap::world::explored_store().players());
        }
    } else {
        livemap::log("discovered places and explored area off in livemap.ini (Discovered=0)");
    }
    uint64_t discovered_saved = livemap::world::discovered_store().version();
    uint64_t deaths_saved = livemap::world::deaths_store().version();
    uint64_t objects_saved = livemap::world::objects_store().version(), last_objects_save = 0;
    uint64_t explored_saved = livemap::world::explored_store().version();
    uint64_t last_discovered = 0, last_explored = 0, last_explored_save = 0;
    uint64_t saved_version = livemap::world::store().version();
    uint64_t last_save = 0;
    const std::wstring players_path = dir + L"\\livemap\\players.json";
    auto& history = livemap::world::player_history();
    if (read_file(players_path, text) && !history.from_json(text)) {
        livemap::log("players: could not read livemap\\players.json");
    }
    uint64_t players_saved = history.version();
    // Recover names recorded before the persistent player list was introduced.
    livemap::discovered::Store old_discovered;
    livemap::explored::Store old_explored;
    livemap::deaths::Store old_deaths;
    if (read_file(discovered_path, text) && old_discovered.from_json(text))
        for (const auto& name : old_discovered.names()) history.update(name, {}, 0);
    if (read_file(explored_path, text) && old_explored.from_json(text))
        for (const auto& name : old_explored.names()) history.update(name, {}, 0);
    if (read_file(deaths_path, text) && old_deaths.from_json(text))
        for (const auto& name : old_deaths.names()) history.update(name, {}, 0);
    if (read_file(dir + L"\\livemap.log", text)) {
        size_t line = 0;
        while (line < text.size()) {
            const size_t next = text.find('\n', line);
            const std::string entry = text.substr(line, next == std::string::npos ? next : next - line);
            if (entry.find(" PLAYER slot ") != std::string::npos || entry.find(" NAME   slot ") != std::string::npos) {
                const size_t name = entry.find(" name \"");
                const size_t end = entry.rfind('"');
                if (name != std::string::npos && end > name + 7)
                    history.update(entry.substr(name + 7, end - name - 7), {}, 0);
            }
            if (next == std::string::npos) break;
            line = next + 1;
        }
    }
    history.set_online({});
    const std::wstring accounts_path = dir + L"\\livemap\\player-accounts.json";
    if (read_file(accounts_path, text)) livemap::world::load_player_accounts(text);
    if (!livemap::web::start(config, dir, error)) {
        livemap::log("%s; no live map", error.c_str());
        livemap::console({"Livemap HTTP startup failed: " + error});
        return 0;
    }
    livemap::console({"Livemap is ready. Game build: " + std::string(targets.build),
        "HTTP listener: " + config.bind + ":" + std::to_string(config.port) + " (TCP)",
        config.log ? "Configuration: livemap.ini | Diagnostics: livemap.log"
                   : "Configuration: livemap.ini | File logging disabled (Log=0)."});
    start_link(config);
    if (!targets.local_game) start_flame_level(config.game_query_port);
    if (config.announce_in_chat) {
        const int port = config.port;
        livemap::install_announce(base, [port] { return livemap::link::chat_line(current_link(), port); });
    }
    uint64_t last_diag = 0;
    uint64_t last_guardian_read = 0;
    for (;;) {
        const uint64_t guardian_now = GetTickCount64();
        if (last_guardian_read == 0 || guardian_now - last_guardian_read >= 2'000) {
            last_guardian_read = guardian_now;
            if (read_file(dir + L"\\guardian\\events.jsonl", text) ||
                read_file(dir + L"\\guardiao-dados\\registro.jsonl", text))
                livemap::world::load_guardian_sessions(text);
        }
        livemap::world::refresh_players();
        if (history.version() != players_saved) {
            const uint64_t version = history.version();
            if (write_file_atomic(players_path, history.to_json())) players_saved = version;
        }
        uint64_t now = GetTickCount64();
        // Finds go to disk at most every 5 s, written to a temp file and renamed into place.
        if (now - last_save >= 5'000 && livemap::world::store().version() != saved_version) {
            last_save = now;
            uint64_t version = livemap::world::store().version();
            if (write_file_atomic(finds_path, livemap::world::store().to_json())) saved_version = version;
            else livemap::log("finds: could not write livemap\\finds.json (error %lu), will retry", GetLastError());
        }
        // Deaths: saved on the next pass after one was counted.
        if (config.deaths && livemap::world::deaths_store().version() != deaths_saved) {
            const uint64_t version = livemap::world::deaths_store().version();
            if (write_file_atomic(deaths_path, livemap::world::deaths_store().to_json())) deaths_saved = version;
        }
        // Chests and switches: saved at most every 10 s when they changed.
        if ((config.objects || config.tombstones) && now - last_objects_save >= 10'000 && livemap::world::objects_store().version() != objects_saved) {
            last_objects_save = now;
            const uint64_t version = livemap::world::objects_store().version();
            if (write_file_atomic(objects_path, livemap::world::objects_store().objects_json())) objects_saved = version;
        }
        // Discovered places: read every 5 s, saved the same way when they changed.
        if (config.discovered && targets.legacy_knowledge_map && now - last_discovered >= 5'000) {
            last_discovered = now;
            livemap::world::refresh_discovered(static_cast<int64_t>(std::time(nullptr)));
            const uint64_t version = livemap::world::discovered_store().version();
            if (version != discovered_saved && write_file_atomic(discovered_path, livemap::world::discovered_store().to_json())) {
                discovered_saved = version;
            }
        }
        // Explored area: every second around each player, saved at most every 30 s.
        if (config.discovered && now - last_explored >= 1'000) {
            last_explored = now;
            livemap::world::refresh_explored();
            const uint64_t version = livemap::world::explored_store().version();
            if (version != explored_saved && now - last_explored_save >= 30'000) {
                last_explored_save = now;
                if (write_file_atomic(explored_path, livemap::world::explored_store().to_json())) explored_saved = version;
            }
        }
        if (config.log_diagnostics && now - last_diag >= 30'000) {
            last_diag = now;
            livemap::world::log_diagnostics();
        }
        Sleep(250);
    }
}

}  // namespace

// Hooks and detached workers require the module resident until process exit.
extern "C" bool livemap_activate() {
    static std::mutex activation_mutex;
    static bool started = false;
    std::lock_guard lock(activation_mutex);
    if (started) return true;
    wchar_t name[96]{};
    swprintf_s(name, L"Local\\EnshroudedLiveMap.%lu", GetCurrentProcessId());
    HANDLE gate = CreateMutexW(nullptr, FALSE, name);
    const DWORD error = GetLastError();
    if (!gate) return false;
    if (error == ERROR_ALREADY_EXISTS) {
        CloseHandle(gate);
        return false;
    }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&livemap_activate), &pinned)) {
        CloseHandle(gate);
        return false;
    }
#ifdef LIVEMAP_SHROUDTOPIA
    // World program registration must be hooked before Activate returns to the game.
    const auto config = livemap::load_config(exe_dir());
    livemap::log_open(exe_dir(), config.log);
    start_hardcore(config);
#endif
    HANDLE thread = CreateThread(nullptr, 0, start, nullptr, 0, nullptr);
    if (!thread) {
        CloseHandle(gate);
        return false;
    }
    CloseHandle(thread);
    // Keep gate open for the process lifetime to prevent duplicate hooks across DLLs.
    started = true;
    return true;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
#ifndef LIVEMAP_SHROUDTOPIA
        g_real_loaded = livemap::load_real_library(self);
        livemap_activate();
#endif
    }
    return TRUE;
}
