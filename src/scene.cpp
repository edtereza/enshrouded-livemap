#include "scene.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "log.h"
#include "memory.h"

namespace livemap::scene {

namespace {

// World (build 26083076): +0 type registry, +0x130 archetype table, +0x140 its capacity (u32),
// +0x1f8 the EntityGrid service, which also starts with the registry. Registry: +0x08 type
// descriptors (0x100 bytes each, FNV hash at +0x20), +0x10 their count. Archetype: +0 bitset of
// type indices (0x50 bytes), +0x50 first chunk, +0x84 / +0xa84 u16 offset / element size per type.
// Chunk: +0x08 next, +0x10 u32 entity count, +0x14 u16 offset of the 16-byte entity ids, +0x18
// u16 offset of the TemplateInfo pointers ({GUID, name pointer +0x10, name length +0x18}).
constexpr uintptr_t kGridInWorld = 0x1f8;
constexpr uintptr_t kArchetypes = 0x130, kArchetypeCapacity = 0x140;
constexpr uintptr_t kBits = 0x00, kFirstChunk = 0x50, kOffsets = 0x84, kSizes = 0xa84;
constexpr size_t kBitsBytes = 0x50;
constexpr uint32_t kMaxEntitiesPerChunk = 8192;

constexpr uint32_t fnv(const char* s) {
    uint32_t h = 0x811c9dc5u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 0x01000193u;
    return h;
}
constexpr uint32_t kTransform = fnv("keen::ecs::CurrentTransform");
constexpr uint32_t kRandomLoot = fnv("keen::ecs::RandomLootContainer");
constexpr uint32_t kEmptyState = fnv("keen::ecs::InventoryEmptyState");     // {toggleVFX, isEmpty}
constexpr uint32_t kDestroyOnLoot = fnv("keen::ecs::DestroyOnLoot");
constexpr uint32_t kFamily = fnv("keen::ecs::TemplateFamilyInfo");
constexpr uint32_t kMapMarker = fnv("keen::ecs::MapMarker");
constexpr uint32_t kOwner = fnv("keen::ecs::OwnerRelationship");
constexpr uint32_t kInventorySetup = fnv("keen::ecs::InventorySetup");
constexpr uint32_t kLootLock = fnv("keen::ecs::RandomLootLock");            // {isEmpty}, saved
constexpr uint32_t kLootNode = fnv("keen::ecs::SceneLootNodeId");           // u32
constexpr uint32_t kLock = fnv("keen::ecs::InteractionLock");               // {destroy, itemUsed}
constexpr uint32_t kToggle = fnv("keen::ecs::ServerToggleState");           // {state, ..., lastChange +8}
constexpr uint32_t kBarrier = fnv("keen::ecs::MagicalBarrier");             // tag
constexpr uint32_t kHealth = fnv("keen::ecs::NetworkHealth");               // {health, healthMax}
constexpr uint32_t kEnemy = fnv("keen::ecs::NetworkEnemy");                 // {state u8, ...}
constexpr uint32_t kLevel = fnv("keen::ecs::NetworkLevel");                 // u16
constexpr uint32_t kBoss = fnv("keen::ecs::Boss");                          // tag
static_assert(kTransform == 0x027ac564 && kRandomLoot == 0x061dda01 && kEmptyState == 0x37c4cfe7);

std::atomic<uintptr_t> g_grid{0};
std::mutex g_mutex;  // the game thread walks, hooks on other threads may ask for type indices
uintptr_t g_attached = 0, g_world = 0;
bool g_bad = false;
std::unordered_map<uint32_t, int> g_types;
uint32_t g_type_count = 0;
std::unordered_map<uintptr_t, std::string> g_template_names;  // TemplateInfo -> name (fixed per process)
std::set<std::string> g_toggle_templates_logged;
int g_walks_logged = 0;

bool attach() {
    const uintptr_t grid = g_grid.load(std::memory_order_relaxed);
    if (grid == 0) return false;
    if (grid == g_attached) return g_world != 0;
    g_attached = grid;
    g_world = 0;
    g_bad = false;
    g_types.clear();
    const uintptr_t world = grid - kGridInWorld;
    uintptr_t registry = 0, again = 0, descriptors = 0;
    uint64_t count = 0;
    if (!read_ptr(world, registry) || !read_ptr(world + kGridInWorld, again) || registry == 0 || registry != again ||
        !read_ptr(registry + 8, descriptors) || !read_u64(registry + 0x10, count) || count < 0x100 || count > kBitsBytes * 8) {
        log("SCENE world layout not recognised (grid %llx): chests, switches and creatures are off",
            static_cast<unsigned long long>(grid));
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t hash = 0;
        if (read_u32(descriptors + static_cast<uintptr_t>(i) * 0x100 + 0x20, hash)) g_types.emplace(hash, static_cast<int>(i));
    }
    g_type_count = static_cast<uint32_t>(count);
    if (!g_types.count(kTransform) || !g_types.count(kRandomLoot) || !g_types.count(kHealth)) {
        log("SCENE type registry at %llx has no CurrentTransform/RandomLootContainer/NetworkHealth: off",
            static_cast<unsigned long long>(registry));
        g_types.clear();
        return false;
    }
    g_world = world;
    log("SCENE world %llx, %u component types", static_cast<unsigned long long>(world), g_type_count);
    return true;
}

int index_of(uint32_t hash) {
    auto it = g_types.find(hash);
    return it == g_types.end() ? -1 : it->second;
}

struct Arch {
    uintptr_t at = 0;
    uint8_t bits[kBitsBytes] = {};
    bool has(int ti) const { return ti >= 0 && ti < static_cast<int>(kBitsBytes * 8) && ((bits[ti >> 3] >> (ti & 7)) & 1); }
    bool component(int ti, uint16_t& offset, uint16_t& size) const {
        return has(ti) && read_u16(at + kOffsets + static_cast<uintptr_t>(ti) * 2, offset) &&
               read_u16(at + kSizes + static_cast<uintptr_t>(ti) * 2, size);
    }
};

std::string template_name(uintptr_t info) {
    if (info == 0) return {};
    auto it = g_template_names.find(info);
    if (it != g_template_names.end()) return it->second;
    uintptr_t ptr = 0;
    uint64_t len = 0;
    std::string name;
    if (read_ptr(info + 0x10, ptr) && read_u64(info + 0x18, len) && ptr != 0 && len > 0 && len < 200) {
        name.resize(static_cast<size_t>(len));
        if (!read_bytes(ptr, name.data(), name.size())) name.clear();
        for (char& c : name) if (static_cast<unsigned char>(c) < 0x20 || c == '"' || c == '\\') c = '_';
    }
    g_template_names.emplace(info, name);
    return name;
}

bool contains(const std::string& s, const char* what) {
    auto it = std::search(s.begin(), s.end(), what, what + std::strlen(what),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return it != s.end();
}

double metres(int64_t fixed) { return static_cast<double>(fixed) / 4294967296.0; }

// One chunk's entities: template names, positions and the wanted component arrays.
struct Rows {
    uint32_t n = 0;
    std::vector<std::string> names;
    std::vector<float> x, y, z;
    std::vector<uint32_t> ids;
    bool ok = false;
};

Rows read_chunk(uintptr_t chunk, uint16_t transform_offset, uint16_t transform_size) {
    Rows r;
    uint16_t ids = 0, infos = 0;
    if (!read_u32(chunk + 0x10, r.n) || r.n == 0 || r.n > kMaxEntitiesPerChunk || !read_u16(chunk + 0x14, ids) ||
        !read_u16(chunk + 0x18, infos) || transform_size < 24) {
        return r;
    }
    std::vector<uintptr_t> info(r.n);
    std::vector<uint8_t> transforms(static_cast<size_t>(r.n) * transform_size);
    std::vector<uint8_t> id_entries(static_cast<size_t>(r.n) * 16);
    if (infos == 0 || !read_bytes(chunk + infos, info.data(), info.size() * sizeof(uintptr_t)) ||
        !read_bytes(chunk + transform_offset, transforms.data(), transforms.size()) ||
        !read_bytes(chunk + ids, id_entries.data(), id_entries.size())) {
        return r;
    }
    r.names.resize(r.n);
    r.x.resize(r.n);
    r.y.resize(r.n);
    r.z.resize(r.n);
    r.ids.resize(r.n);
    for (uint32_t j = 0; j < r.n; ++j) {
        r.names[j] = template_name(info[j]);
        int64_t fx = 0, fy = 0, fz = 0;
        std::memcpy(&fx, &transforms[static_cast<size_t>(j) * transform_size], 8);
        std::memcpy(&fy, &transforms[static_cast<size_t>(j) * transform_size + 8], 8);
        std::memcpy(&fz, &transforms[static_cast<size_t>(j) * transform_size + 16], 8);
        r.x[j] = static_cast<float>(metres(fx));
        r.y[j] = static_cast<float>(metres(fy));
        r.z[j] = static_cast<float>(metres(fz));
        std::memcpy(&r.ids[j], &id_entries[static_cast<size_t>(j) * 16], 4);
    }
    r.ok = true;
    return r;
}

// The bytes of component `ti` for all n entities of a chunk, or empty when absent / size differs.
std::vector<uint8_t> column(const Arch& a, uintptr_t chunk, uint32_t n, int ti, uint16_t want_size) {
    uint16_t offset = 0, size = 0;
    std::vector<uint8_t> out;
    if (!a.component(ti, offset, size) || size != want_size) return out;
    out.resize(static_cast<size_t>(n) * size);
    if (!read_bytes(chunk + offset, out.data(), out.size())) out.clear();
    return out;
}

bool in_world(float x, float z) { return x >= 0 && x <= 10240 && z >= 0 && z <= 10240; }

// World doors, barriers, levers and the like; not what players built (crafted doors) and not the
// enemy spawners whose names mention a door (seen on the test server).
bool switch_template(const std::string& t) {
    for (const char* w : {"crafted", "craftable", "buildingset", "spawnpoint", "respawner"}) if (contains(t, w)) return false;
    for (const char* w : {"barrier", "lever", "button", "secret", "switch", "gate", "door", "valve", "seal"}) if (contains(t, w)) return true;
    return false;
}

bool creature_helper(const std::string& t) {
    for (const char* w : {"collider", "colldier", "weakspot", "weapon", "corpse", "signalreceiver", "marker", "additional", "feeding"}) {
        if (contains(t, w)) return true;
    }
    return false;
}

}  // namespace

void set_entity_grid(uintptr_t grid) {
    g_grid.store(grid, std::memory_order_relaxed);
}

int type_index(uint32_t hash) {
    std::lock_guard held(g_mutex);
    return attach() ? index_of(hash) : -1;
}

std::vector<PlayerTransform> player_transforms(const std::vector<uint32_t>& entities) {
    std::vector<PlayerTransform> result;
    if (entities.empty()) return result;
    std::lock_guard held(g_mutex);
    if (!attach()) return result;
    uintptr_t table = 0;
    uint32_t capacity = 0;
    if (!read_ptr(g_world + kArchetypes, table) || !read_u32(g_world + kArchetypeCapacity, capacity) ||
        table == 0 || capacity == 0 || capacity > 65536) return result;
    std::vector<uintptr_t> layouts(capacity);
    if (!read_bytes(table, layouts.data(), layouts.size() * sizeof(uintptr_t))) return result;
    std::sort(layouts.begin(), layouts.end());
    const int transform = index_of(kTransform);
    for (size_t k = 0; k < layouts.size(); ++k) {
        const uintptr_t p = layouts[k];
        if (p == 0 || (p & 7) || (k && layouts[k - 1] == p) ||
            (k + 1 < layouts.size() && layouts[k + 1] == p)) continue;
        Arch layout;
        layout.at = p;
        uint16_t offset = 0, stride = 0;
        if (!read_bytes(p + kBits, layout.bits, kBitsBytes) ||
            !layout.component(transform, offset, stride) || stride != 0x38) continue;
        uintptr_t chunk = 0;
        read_ptr(p + kFirstChunk, chunk);
        for (int guard = 0; chunk && guard < 20000; ++guard) {
            uint32_t count = 0;
            uint16_t ids_offset = 0;
            if (!read_u32(chunk + 0x10, count) || count > kMaxEntitiesPerChunk ||
                !read_u16(chunk + 0x14, ids_offset)) break;
            std::vector<uint8_t> ids(static_cast<size_t>(count) * 16);
            if (!read_bytes(chunk + ids_offset, ids.data(), ids.size())) break;
            for (uint32_t row = 0; row < count; ++row) {
                uint32_t entity = 0;
                std::memcpy(&entity, ids.data() + row * 16, 4);
                if (std::find(entities.begin(), entities.end(), entity) == entities.end()) continue;
                PlayerTransform value;
                value.entity = entity;
                const uintptr_t data = chunk + offset + static_cast<uintptr_t>(row) * stride;
                if (read_bytes(data, value.position, sizeof value.position) &&
                    read_bytes(data + 0x18, value.rotation, sizeof value.rotation)) result.push_back(value);
            }
            if (result.size() == entities.size()) return result;
            uintptr_t next = 0;
            if (!read_ptr(chunk + 8, next) || next == chunk) break;
            chunk = next;
        }
    }
    return result;
}

bool walk(Pass& out, bool tombstones_only) {
    std::lock_guard held(g_mutex);
    if (!attach()) return false;
    uintptr_t table = 0;
    uint32_t capacity = 0;
    if (!read_ptr(g_world + kArchetypes, table) || !read_u32(g_world + kArchetypeCapacity, capacity) || table == 0 ||
        capacity == 0 || capacity > 65536) {
        if (!g_bad) log("SCENE archetype table not readable");
        g_bad = true;
        return false;
    }
    std::vector<uintptr_t> slots(capacity);
    if (!read_bytes(table, slots.data(), slots.size() * sizeof(uintptr_t))) return false;
    // Free slots hold 0 or one shared sentinel; an archetype appears exactly once.
    std::vector<uintptr_t> sorted(slots);
    std::sort(sorted.begin(), sorted.end());
    const int transform = index_of(kTransform), loot = index_of(kRandomLoot), empty = index_of(kEmptyState),
              loot_lock = index_of(kLootLock), node = index_of(kLootNode), lock = index_of(kLock), toggle = index_of(kToggle),
              barrier = index_of(kBarrier), health = index_of(kHealth), enemy = index_of(kEnemy), level = index_of(kLevel),
              boss = index_of(kBoss), destroy = index_of(kDestroyOnLoot), family = index_of(kFamily),
              marker = index_of(kMapMarker), owner = index_of(kOwner), setup = index_of(kInventorySetup);
    for (size_t k = 0; k < sorted.size(); ++k) {
        const uintptr_t p = sorted[k];
        if (p == 0 || (p & 7) != 0 || (k > 0 && sorted[k - 1] == p) || (k + 1 < sorted.size() && sorted[k + 1] == p)) continue;
        Arch a;
        a.at = p;
        if (!read_bytes(p + kBits, a.bits, kBitsBytes) || !a.has(transform)) continue;
        const bool is_chest = !tombstones_only && a.has(loot) && a.has(empty),
                   is_lock = !tombstones_only && a.has(lock), is_toggle = !tombstones_only && a.has(toggle),
                   is_creature = !tombstones_only && a.has(health), is_tombstone = a.has(empty) && a.has(destroy);
        if (!is_chest && !is_lock && !is_toggle && !is_creature && !is_tombstone) continue;
        ++out.archetypes;
        uint16_t t_offset = 0, t_size = 0;
        if (!a.component(transform, t_offset, t_size)) continue;
        uintptr_t chunk = 0;
        read_ptr(p + kFirstChunk, chunk);
        for (int guard = 0; chunk != 0 && guard < 20000; ++guard) {
            Rows rows = read_chunk(chunk, t_offset, t_size);
            if (rows.ok) {
                const uint32_t n = rows.n;
                if (is_tombstone) {
                    const auto es = column(a, chunk, n, empty, 2), dl = column(a, chunk, n, destroy, 104),
                               fm = column(a, chunk, n, family, 1), mk = column(a, chunk, n, marker, 40),
                               ow = column(a, chunk, n, owner, 4), inv = column(a, chunk, n, setup, 84);
                    for (uint32_t j = 0; j < n && !es.empty() && !dl.empty(); ++j) {
                        if (!objects::is_player_tombstone(rows.names[j], fm.empty() ? 0 : fm[j]) ||
                            !in_world(rows.x[j], rows.z[j])) continue;
                        objects::Tombstone stone;
                        stone.entity = rows.ids[j];
                        stone.x = rows.x[j]; stone.y = rows.y[j]; stone.z = rows.z[j];
                        stone.empty = es[j * 2 + 1] != 0;
                        stone.removing = dl[j * 104 + 64] != 0 && dl[j * 104 + 65] != 0;
                        stone.inventory_ready = !a.has(setup) || (!inv.empty() && inv[j * 84 + 83] != 0);
                        if (!ow.empty()) std::memcpy(&stone.owner_entity, &ow[j * 4], 4);
                        if (!mk.empty()) {
                            constexpr char hex[] = "0123456789abcdef";
                            bool nonzero = false;
                            for (size_t b = 0; b < 16; ++b) {
                                const uint8_t value = mk[j * 40 + b];
                                nonzero |= value != 0;
                                stone.marker += hex[value >> 4]; stone.marker += hex[value & 15];
                            }
                            if (!nonzero) stone.marker.clear();
                        }
                        out.tombstones.push_back(std::move(stone));
                    }
                }
                if (is_chest) {
                    const auto es = column(a, chunk, n, empty, 2), ll = column(a, chunk, n, loot_lock, 1), id = column(a, chunk, n, node, 4);
                    for (uint32_t j = 0; j < n && !es.empty(); ++j) {
                        const std::string& t = rows.names[j];
                        if ((t.rfind("LootChest_", 0) != 0 && !contains(t, "sarcophag")) || !in_world(rows.x[j], rows.z[j])) continue;
                        objects::Chest c;
                        if (!id.empty()) std::memcpy(&c.id, &id[j * 4], 4);
                        c.tpl = t;
                        c.x = rows.x[j];
                        c.y = rows.y[j];
                        c.z = rows.z[j];
                        c.empty = es[j * 2 + 1] != 0 || (!ll.empty() && ll[j] != 0);
                        out.chests.push_back(std::move(c));
                    }
                }
                if (is_lock) {
                    const auto lk = column(a, chunk, n, lock, 2);
                    for (uint32_t j = 0; j < n && !lk.empty(); ++j) {
                        if (!contains(rows.names[j], "locked") || !in_world(rows.x[j], rows.z[j])) continue;
                        out.switches.push_back({rows.names[j], rows.x[j], rows.y[j], rows.z[j], 'l', lk[j * 2 + 1] != 0});
                    }
                }
                if (is_toggle) {
                    const auto tg = column(a, chunk, n, toggle, 24);
                    for (uint32_t j = 0; j < n && !tg.empty(); ++j) {
                        const std::string& t = rows.names[j];
                        const bool is_barrier = a.has(barrier);
                        if (g_toggle_templates_logged.size() < 400 && g_toggle_templates_logged.insert(t).second) {
                            log("TOGGLE %s state %u at %.1f %.1f%s", t.c_str(), tg[j * 24], rows.x[j], rows.z[j], is_barrier ? " (barrier)" : "");
                        }
                        if ((!is_barrier && !switch_template(t)) || !in_world(rows.x[j], rows.z[j])) continue;
                        out.switches.push_back({t, rows.x[j], rows.y[j], rows.z[j], is_barrier || contains(t, "barrier") ? 'b' : 's', tg[j * 24] != 0});
                    }
                }
                if (is_creature) {
                    const auto hp = column(a, chunk, n, health, 8), en = column(a, chunk, n, enemy, 8), lv = column(a, chunk, n, level, 2);
                    for (uint32_t j = 0; j < n && !hp.empty() && out.creatures.size() < 600; ++j) {
                        const std::string& t = rows.names[j];
                        const bool animal = t.rfind("Animal_", 0) == 0;
                        if ((en.empty() && !animal) || creature_helper(t) || !in_world(rows.x[j], rows.z[j])) continue;
                        objects::Creature c;
                        std::memcpy(&c.hp, &hp[j * 8], 4);
                        std::memcpy(&c.max, &hp[j * 8 + 4], 4);
                        if (c.hp == 0 || c.max == 0 || c.hp > c.max) continue;
                        c.state = en.empty() ? 0 : en[j * 8];
                        if (c.state == 3) continue;  // dead
                        if (!lv.empty()) std::memcpy(&c.level, &lv[j * 2], 2);
                        c.entity = rows.ids[j];
                        c.tpl = t;
                        c.x = rows.x[j];
                        c.z = rows.z[j];
                        c.boss = a.has(boss);
                        c.animal = animal;
                        out.creatures.push_back(std::move(c));
                    }
                }
            }
            if (!read_ptr(chunk + 8, chunk)) break;
        }
    }
    if (!tombstones_only && g_walks_logged < 3) {
        ++g_walks_logged;
        size_t looted = 0, on = 0;
        for (const auto& c : out.chests) looted += c.empty;
        for (const auto& s : out.switches) on += s.on;
        log("SCENE pass: %d archetypes, %zu chests (%zu looted), %zu locks/switches (%zu open), %zu creatures", out.archetypes,
            out.chests.size(), looted, out.switches.size(), on, out.creatures.size());
    }
    return true;
}

}  // namespace livemap::scene
