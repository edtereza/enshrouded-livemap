#pragma once

#include <string>

namespace livemap {

struct Config {
    bool enabled = true;
    bool local_game = false;
    int port = 15680;            // TCP port of the web map
    bool port_from_game = false; // Port=auto: the game's queryPort number (TCP)
    int game_query_port = 0;     // queryPort from enshrouded_server.json (UDP, Steam queries); 0 = unknown
    std::string public_url;      // PublicUrl=, e.g. https://map.example.com; empty = detect
    std::string title;           // browser title in UTF-8
    bool detect_public_address = true;
    bool announce_in_chat = true;
    std::string bind = "0.0.0.0";
    bool require_key = false;
    std::string key;             // used only when RequireKey=1
    int update_ms = 250;         // how often the page gets new positions
    bool log = true;
    bool log_diagnostics = true; // time and player positions every 30 s, new players' raw names
    bool finds = true;           // ore and chest hooks (Finds=0 turns them off)
    bool bases = true;           // Flame Altars on the map (Bases=0 turns them off)
    bool discovered = true;      // which markers each player discovered (Discovered=0 turns it off)
    bool deaths = true;          // death count per player (Deaths=0 turns it off)
    bool objects = true;         // chests, locks, switches and creatures from the world (Objects=0 turns it off)
    bool tombstones = true;
    bool chat_probe = false;     // research: after a join, post one chat line per message type
};

// Reads livemap.ini from `dir`; writes it with the defaults (and a random key) if it is missing.
Config load_config(const std::wstring& dir);

// Read only Title from livemap.ini, preserving UTF-8 and supporting Windows INI encodings.
std::string read_title(const std::wstring& dir);

}  // namespace livemap
