#include "scene.h"
#include <array>
#include <cstdio>
#include <cstring>

template<class T> void put(std::vector<uint8_t>& bytes, size_t offset, T value) { std::memcpy(bytes.data() + offset, &value, sizeof value); }
uint32_t hash(const char* s) { uint32_t h = 0x811c9dc5; for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 0x01000193; return h; }

int main() {
    std::vector<uint8_t> world(0x300), registry(0x20), descriptors(640 * 0x100), layout(0x1500), chunk(0x600);
    uintptr_t table[] = {reinterpret_cast<uintptr_t>(layout.data()), 0};
    put(world, 0, reinterpret_cast<uintptr_t>(registry.data()));
    put(world, 0x1f8, reinterpret_cast<uintptr_t>(registry.data()));
    put(world, 0x130, reinterpret_cast<uintptr_t>(table)); put(world, 0x140, uint32_t{2});
    put(registry, 8, reinterpret_cast<uintptr_t>(descriptors.data())); put(registry, 0x10, uint64_t{640});
    const char* types[] = {"CurrentTransform", "RandomLootContainer", "NetworkHealth", "InventoryEmptyState", "DestroyOnLoot",
        "MapMarker", "TemplateFamilyInfo", "OwnerRelationship", "InventorySetup"};
    for (size_t i = 0; i < std::size(types); ++i) put(descriptors, i * 0x100 + 0x20, hash((std::string("keen::ecs::") + types[i]).c_str()));
    layout[0] = 0xf9; layout[1] = 1;
    put(layout, 0x50, reinterpret_cast<uintptr_t>(chunk.data()));
    const uint16_t offsets[] = {0x100, 0, 0, 0x1b0, 0x1c0, 0x300, 0x380, 0x390, 0x400};
    const uint16_t sizes[] = {56, 0, 0, 2, 104, 40, 1, 4, 84};
    for (size_t i = 0; i < std::size(types); ++i) { put(layout, 0x84 + i * 2, offsets[i]); put(layout, 0xa84 + i * 2, sizes[i]); }
    put(chunk, 0x10, uint32_t{3}); put(chunk, 0x14, uint16_t{0x40}); put(chunk, 0x18, uint16_t{0x80});
    const std::array<std::string, 3> names = {"Base_LootContainer_Player_LootCorpse_Tombstone", "Prop_Deco_Villageprops_Tombstone_07", "VFX_Player_LootCorpse_Tombstone_OnSpawn"};
    std::array<std::array<uint8_t, 0x30>, 3> infos{};
    for (size_t i = 0; i < names.size(); ++i) {
        const auto pointer = reinterpret_cast<uintptr_t>(names[i].data()); const uint64_t length = names[i].size();
        std::memcpy(infos[i].data() + 0x10, &pointer, 8); std::memcpy(infos[i].data() + 0x18, &length, 8);
        put(chunk, 0x80 + i * 8, reinterpret_cast<uintptr_t>(infos[i].data())); put(chunk, 0x40 + i * 16, static_cast<uint32_t>(10 + i));
        put(chunk, 0x100 + i * 56, int64_t{100} << 32); put(chunk, 0x108 + i * 56, int64_t{40} << 32); put(chunk, 0x110 + i * 56, int64_t{200} << 32);
        chunk[0x400 + i * 84 + 83] = 1;
    }
    chunk[0x380] = 12; chunk[0x381] = 8; chunk[0x300] = 0xab;
    put(chunk, 0x390, uint32_t{1009});
    livemap::scene::set_entity_grid(reinterpret_cast<uintptr_t>(world.data()) + 0x1f8);
    livemap::scene::Pass pass;
    if (!livemap::scene::walk(pass, true) || pass.tombstones.size() != 1) return 1;
    const auto& stone = pass.tombstones[0];
    if (stone.entity != 10 || stone.owner_entity != 1009 || stone.marker != "ab000000000000000000000000000000" ||
        stone.x != 100 || stone.y != 40 || stone.z != 200 || !stone.inventory_ready || stone.empty || stone.removing) return 2;
    chunk[0x1b1] = 1; chunk[0x400 + 83] = 0; chunk[0x1c0 + 64] = 1; chunk[0x1c0 + 65] = 1;
    livemap::scene::Pass changed;
    if (!livemap::scene::walk(changed, true) || changed.tombstones.size() != 1 || !changed.tombstones[0].empty ||
        changed.tombstones[0].inventory_ready || !changed.tombstones[0].removing) return 3;
    std::puts("Real ECS tombstones, decorative exclusion, marker identity and collection flags: OK");
}
