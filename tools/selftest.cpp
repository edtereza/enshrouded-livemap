// Offline check: maps an enshrouded_server.exe from disk (without running it) and runs the same
// build check and pattern search the DLL runs inside the server.
//
// Usage: livemap_selftest <path\to\enshrouded_server.exe>
// Exit code 0 = the mod would hook this build, 1 = it would stay inactive, 2 = usage/load error.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "scan.h"

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: livemap_selftest <path\\to\\enshrouded_server.exe>\n");
        return 2;
    }
    HMODULE image = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (image == nullptr) {
        std::fprintf(stderr, "could not map %ls (error %lu)\n", argv[1], GetLastError());
        return 2;
    }
    auto base = reinterpret_cast<const uint8_t*>(image);
    livemap::Targets t;
    std::string error;
    if (!livemap::find_targets(base, t, error)) {
        std::printf("INACTIVE: %s\n", error.c_str());
        return 1;
    }
    const bool october_hotfix = std::strcmp(t.build, "26083076") == 0;
    const bool client = std::strcmp(t.build, "26430625") == 0;
    const bool november = client || std::strcmp(t.build, "26430253") == 0;
    std::printf("server build %s\n", t.build);
    struct Site {
        const char* name;
        uintptr_t at;
        uintptr_t expected;
    };
    const Site sites[] = {
        {"player_table", t.player_table, client ? 0xe627e0ULL : november ? 0x7bed90ULL : october_hotfix ? 0x7b5800ULL : 0x7b34c0ULL},
        {"movement", t.movement, client ? 0x283b2aULL : november ? 0x9581aULL : october_hotfix ? 0x91d2aULL : 0x90e7aULL},
        {"daytime", t.daytime, client ? 0xec1dc2ULL : november ? 0x969c62ULL : october_hotfix ? 0x95f842ULL : 0x95d1f2ULL},
        {"registry", t.registry, client ? 0x9e2780ULL : november ? 0x67e720ULL : october_hotfix ? 0x677240ULL : 0x675e10ULL},
        {"mining", t.mining, november ? 0 : october_hotfix ? 0x2245a8ULL : 0x2236f8ULL},
        {"chest", t.chest, client ? 0x2a831aULL : november ? 0xa578aULL : october_hotfix ? 0xa1afaULL : 0xa0c4aULL},
        {"bases", t.bases, client ? 0x32e700ULL : november ? 0xee3f0ULL : october_hotfix ? 0xea090ULL : 0xe91e0ULL},
        {"chat_post", t.chat_post, client ? 0xe6e1c0ULL : november ? 0x9151f0ULL : october_hotfix ? 0x90b330ULL : 0x908ce0ULL},
        {"player_death", t.player_death, client ? 0x2355f0ULL : november ? 0x52430ULL : october_hotfix ? 0x4ee70ULL : 0x4dfc0ULL},
    };
    bool ok = true;
    for (const auto& s : sites) {
        uintptr_t rva = s.at ? s.at - reinterpret_cast<uintptr_t>(base) : 0;
        ok = ok && rva == s.expected;
        std::printf("%-13s rva 0x%llx%s\n", s.name, static_cast<unsigned long long>(rva),
                    rva == s.expected ? "" : "  (UNEXPECTED)");
    }
    if (november) {
        livemap::ChatSites chat;
        const auto image_base = reinterpret_cast<uintptr_t>(base);
        const bool chat_ok = livemap::find_chat_sites(base, chat) &&
                             chat.join_call == image_base + (client ? 0xe639e2 : 0x7bff92) && chat.join_after == image_base + (client ? 0xe639e7 : 0x7bff97) &&
                             chat.chat_post == t.chat_post && !chat.shared;
        std::printf("chat join      %s\n", chat_ok ? "OK" : "UNEXPECTED");
        ok = ok && chat_ok;
    }
    ok = ok && t.movement_entity_table_offset == (november ? 0x830 : 0x828) &&
         t.legacy_knowledge_map == !november && t.local_game == client;
    std::printf(ok ? "OK: all expected hook sites found\n" : "FAIL: unexpected hook or layout\n");
    return ok ? 0 : 1;
}
