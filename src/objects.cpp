#include "objects.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include "text.h"

namespace livemap::objects {

namespace {

void append_escaped(std::string& out, const std::string& s) {
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c >= 0x20) {
            out += static_cast<char>(c);
        }
    }
}

// Minimal reader for the store's own JSON: the objects of the array after "key":[ as bodies.
std::vector<std::string> objects_in(const std::string& t, const char* key) {
    std::vector<std::string> out;
    const std::string k = std::string("\"") + key + "\":[";
    size_t i = t.find(k);
    if (i == std::string::npos) return out;
    i += k.size();
    while (i < t.size() && t[i] != ']') {
        if (t[i] != '{') { ++i; continue; }
        size_t end = i + 1;
        int depth = 1;
        bool quoted = false, escaped = false;
        for (; end < t.size(); ++end) {
            const char c = t[end];
            if (quoted) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') quoted = false;
            } else if (c == '"') quoted = true;
            else if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) break;
        }
        if (end == t.size()) break;
        out.push_back(t.substr(i + 1, end - i - 1));
        i = end + 1;
    }
    return out;
}

double number(const std::string& body, const char* key, double fallback) {
    const std::string k = std::string("\"") + key + "\":";
    size_t at = body.find(k);
    return at == std::string::npos ? fallback : std::strtod(body.c_str() + at + k.size(), nullptr);
}

bool flag(const std::string& body, const char* key) {
    return body.find(std::string("\"") + key + "\":true") != std::string::npos;
}

std::string text(const std::string& body, const char* key) {
    return livemap::text::json_string_member("{" + body + "}", key);
}

std::string place_key(const std::string& tpl, float x, float z) {
    return tpl + "@" + std::to_string(std::lround(x)) + "," + std::to_string(std::lround(z));
}

}  // namespace

bool is_player_tombstone(const std::string& name, uint8_t family) {
    return family == 12 || name == "Base_LootContainer_Player_LootCorpse_Tombstone";
}

bool Store::update_tombstones(const std::vector<Tombstone>& stones, const std::vector<deaths::Death>& deaths, int64_t now) {
    std::lock_guard lock(mutex_);
    std::set<std::string> observed;
    bool changed = false;
    for (const auto& stone : stones) {
        if (!stone.entity || !std::isfinite(stone.x) || !std::isfinite(stone.y) || !std::isfinite(stone.z) ||
            stone.x < 0 || stone.z < 0 || stone.x > 10240 || stone.z > 10240) continue;
        const std::string key = stone.marker.empty() ? "entity:" + std::to_string(stone.entity) + "@" +
            std::to_string(std::lround(stone.x * 100)) + "," + std::to_string(std::lround(stone.y * 100)) + "," +
            std::to_string(std::lround(stone.z * 100)) : "marker:" + stone.marker;
        observed.insert(key);
        auto [it, added] = tombstones_.try_emplace(key);
        auto& r = it->second;
        if (added) r.first = now;
        const bool collected = r.collected || stone.removing || (stone.inventory_ready && stone.empty);
        if (added || !r.loaded || r.collected != collected || r.stone.x != stone.x || r.stone.y != stone.y || r.stone.z != stone.z)
            changed = true;
        r.stone = stone;
        r.seen = now;
        r.loaded = true;
        r.collected = collected;
        std::string owner = r.owner, source = r.owner_source;
        if (!stone.owner.empty()) { owner = stone.owner; source = "entity"; }
        else if (source != "entity") {
            std::set<std::string> candidates;
            for (const auto& death : deaths) {
                const double dx = death.x - stone.x, dz = death.z - stone.z;
                if (!death.name.empty() && std::abs(death.at - r.first) <= 10 && dx * dx + dz * dz <= 9)
                    candidates.insert(death.name);
            }
            if (candidates.size() == 1) { owner = *candidates.begin(); source = "nearby-death"; }
            else if (candidates.size() > 1) { owner.clear(); source.clear(); }
        }
        if (owner != r.owner || source != r.owner_source) changed = true;
        r.owner = std::move(owner);
        r.owner_source = std::move(source);
    }
    for (auto it = tombstones_.begin(); it != tombstones_.end();) {
        auto& r = it->second;
        if (r.loaded && !observed.contains(it->first)) { r.loaded = false; changed = true; }
        // Collection proofs only need a short grace period; death history is kept separately.
        if (r.collected && !r.loaded && now > r.seen && now - r.seen > 3600) {
            it = tombstones_.erase(it);
            changed = true;
        } else ++it;
    }
    if (changed) ++version_;
    return changed;
}

std::string chest_key(const Chest& c) {
    return c.id != 0 ? "#" + std::to_string(c.id) : place_key(c.tpl, c.x, c.z);
}

std::string switch_key(const Switch& s) {
    return place_key(s.tpl, s.x, s.z);
}

bool Store::update(const std::vector<Chest>& chests, const std::vector<Switch>& switches, int64_t now) {
    std::lock_guard lock(mutex_);
    bool changed = false;
    for (const Chest& c : chests) {
        auto [it, added] = chests_.try_emplace(chest_key(c), ChestRec{c, now});
        if (added) { changed = true; continue; }
        if (it->second.c.empty != c.empty) { it->second.at = now; changed = true; }
        it->second.c = c;
    }
    for (const Switch& s : switches) {
        auto [it, added] = switches_.try_emplace(switch_key(s), SwitchRec{s, now});
        if (added) { changed = true; continue; }
        if (it->second.s.on != s.on) { it->second.at = now; changed = true; }
        it->second.s = s;
    }
    if (changed) ++version_;
    return changed;
}

void Store::set_creatures(std::vector<Creature> creatures) {
    std::lock_guard lock(mutex_);
    if (creatures.empty() && creatures_.empty()) return;
    creatures_ = std::move(creatures);
    ++creatures_version_;
}

std::string Store::objects_json(bool include_objects, bool include_tombstones) {
    std::lock_guard lock(mutex_);
    std::string out = "{\"version\":" + std::to_string(version_) + ",\"chests\":[";
    bool first = true;
    char buf[160];
    for (const auto& [key, r] : chests_) {
        if (!include_objects) break;
        std::snprintf(buf, sizeof buf, "%s{\"id\":%u,\"t\":\"", first ? "" : ",", r.c.id);
        out += buf;
        append_escaped(out, r.c.tpl);
        std::snprintf(buf, sizeof buf, "\",\"x\":%.1f,\"y\":%.1f,\"z\":%.1f,\"empty\":%s,\"at\":%lld}", r.c.x, r.c.y, r.c.z,
                      r.c.empty ? "true" : "false", static_cast<long long>(r.at));
        out += buf;
        first = false;
    }
    out += "],\"switches\":[";
    first = true;
    for (const auto& [key, r] : switches_) {
        if (!include_objects) break;
        out += first ? "{\"t\":\"" : ",{\"t\":\"";
        append_escaped(out, r.s.tpl);
        std::snprintf(buf, sizeof buf, "\",\"x\":%.1f,\"y\":%.1f,\"z\":%.1f,\"k\":\"%c\",\"on\":%s,\"at\":%lld}", r.s.x, r.s.y, r.s.z, r.s.kind,
                      r.s.on ? "true" : "false", static_cast<long long>(r.at));
        out += buf;
        first = false;
    }
    out += "],\"tombstones\":[";
    first = true;
    for (const auto& [key, r] : tombstones_) {
        if (!include_tombstones) break;
        if (!first) out += ',';
        first = false;
        out += "{\"key\":" + livemap::text::quote_json(key) + ",\"marker\":" + livemap::text::quote_json(r.stone.marker);
        out += ",\"owner\":" + livemap::text::quote_json(r.owner) + ",\"ownerSource\":" + livemap::text::quote_json(r.owner_source);
        char stone[300];
        std::snprintf(stone, sizeof stone,
            ",\"entity\":%u,\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"first\":%lld,\"seen\":%lld,\"loaded\":%s,\"collected\":%s}",
            r.stone.entity, r.stone.x, r.stone.y, r.stone.z, static_cast<long long>(r.first), static_cast<long long>(r.seen),
            r.loaded ? "true" : "false", r.collected ? "true" : "false");
        out += stone;
    }
    out += "]}";
    return out;
}

std::string Store::creatures_json() {
    std::lock_guard lock(mutex_);
    std::string out = "{\"version\":" + std::to_string(creatures_version_) + ",\"creatures\":[";
    char buf[200];
    for (size_t i = 0; i < creatures_.size(); ++i) {
        const Creature& c = creatures_[i];
        std::snprintf(buf, sizeof buf, "%s{\"e\":%u,\"t\":\"", i ? "," : "", c.entity);
        out += buf;
        append_escaped(out, c.tpl);
        std::snprintf(buf, sizeof buf, "\",\"x\":%.1f,\"z\":%.1f,\"hp\":%u,\"max\":%u,\"s\":%u,\"lv\":%u,\"boss\":%s,\"animal\":%s}",
                      c.x, c.z, c.hp, c.max, c.state, c.level, c.boss ? "true" : "false", c.animal ? "true" : "false");
        out += buf;
    }
    out += "]}";
    return out;
}

bool Store::from_json(const std::string& t) {
    if (t.find("\"chests\":[") == std::string::npos || t.find("\"switches\":[") == std::string::npos) return false;
    std::map<std::string, ChestRec> chests;
    std::map<std::string, SwitchRec> switches;
    std::map<std::string, TombstoneRec> tombstones;
    for (const std::string& b : objects_in(t, "chests")) {
        ChestRec r;
        r.c.id = static_cast<uint32_t>(number(b, "id", 0));
        r.c.tpl = text(b, "t");
        r.c.x = static_cast<float>(number(b, "x", -1));
        r.c.y = static_cast<float>(number(b, "y", 0));
        r.c.z = static_cast<float>(number(b, "z", -1));
        r.c.empty = flag(b, "empty");
        r.at = static_cast<int64_t>(number(b, "at", 0));
        if (r.c.tpl.empty() || r.c.x < 0 || r.c.z < 0) return false;
        chests[chest_key(r.c)] = r;
    }
    for (const std::string& b : objects_in(t, "switches")) {
        SwitchRec r;
        r.s.tpl = text(b, "t");
        r.s.x = static_cast<float>(number(b, "x", -1));
        r.s.y = static_cast<float>(number(b, "y", 0));
        r.s.z = static_cast<float>(number(b, "z", -1));
        const std::string k = text(b, "k");
        r.s.kind = k.empty() ? 's' : k[0];
        r.s.on = flag(b, "on");
        r.at = static_cast<int64_t>(number(b, "at", 0));
        if (r.s.tpl.empty() || r.s.x < 0 || r.s.z < 0) return false;
        switches[switch_key(r.s)] = r;
    }
    for (const auto& b : objects_in(t, "tombstones")) {
        const auto key = text(b, "key");
        TombstoneRec r;
        r.stone.marker = text(b, "marker");
        r.stone.entity = static_cast<uint32_t>(number(b, "entity", 0));
        r.stone.x = static_cast<float>(number(b, "x", -1));
        r.stone.y = static_cast<float>(number(b, "y", 0));
        r.stone.z = static_cast<float>(number(b, "z", -1));
        r.first = static_cast<int64_t>(number(b, "first", 0));
        r.seen = static_cast<int64_t>(number(b, "seen", 0));
        r.collected = flag(b, "collected");
        r.owner = text(b, "owner");
        r.owner_source = text(b, "ownerSource");
        if (key.empty() || !std::isfinite(r.stone.x) || !std::isfinite(r.stone.y) || !std::isfinite(r.stone.z) ||
            r.stone.x < 0 || r.stone.z < 0 || r.stone.x > 10240 || r.stone.z > 10240) return false;
        tombstones[key] = std::move(r); // Never restore "loaded": a fresh world observation is required.
    }
    std::lock_guard lock(mutex_);
    chests_ = std::move(chests);
    switches_ = std::move(switches);
    tombstones_ = std::move(tombstones);
    ++version_;
    return true;
}

uint64_t Store::version() {
    std::lock_guard lock(mutex_);
    return version_;
}

uint64_t Store::creatures_version() {
    std::lock_guard lock(mutex_);
    return creatures_version_;
}

size_t Store::chests() {
    std::lock_guard lock(mutex_);
    return chests_.size();
}

size_t Store::switches() {
    std::lock_guard lock(mutex_);
    return switches_.size();
}

}  // namespace livemap::objects
