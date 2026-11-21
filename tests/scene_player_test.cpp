#include "scene.h"
#include <cstdio>
#include <cstring>
#include <vector>

template<class T> void put(std::vector<uint8_t>& bytes, size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}

uint32_t hash(const char* name) {
    uint32_t value = 0x811c9dc5;
    for (; *name; ++name) value = (value ^ static_cast<uint8_t>(*name)) * 0x01000193;
    return value;
}

int main() {
    std::vector<uint8_t> world(0x300), registry(0x20), descriptors(640 * 0x100),
                         layout(0x1500), chunk(0x200);
    uintptr_t table[] = {reinterpret_cast<uintptr_t>(layout.data()), 0};
    put(world, 0, reinterpret_cast<uintptr_t>(registry.data()));
    put(world, 0x1f8, reinterpret_cast<uintptr_t>(registry.data()));
    put(world, 0x130, reinterpret_cast<uintptr_t>(table));
    put(world, 0x140, uint32_t{2});
    put(registry, 8, reinterpret_cast<uintptr_t>(descriptors.data()));
    put(registry, 0x10, uint64_t{640});
    put(descriptors, 0x20, hash("keen::ecs::CurrentTransform"));
    put(descriptors, 0x120, hash("keen::ecs::RandomLootContainer"));
    put(descriptors, 0x220, hash("keen::ecs::NetworkHealth"));
    layout[0] = 1;
    put(layout, 0x50, reinterpret_cast<uintptr_t>(chunk.data()));
    put(layout, 0x84, uint16_t{0x100});
    put(layout, 0xa84, uint16_t{0x38});
    put(chunk, 0x10, uint32_t{1});
    put(chunk, 0x14, uint16_t{0x40});
    put(chunk, 0x40, uint32_t{1});
    put(chunk, 0x100, int64_t{123} << 32);
    put(chunk, 0x108, int64_t{42} << 32);
    put(chunk, 0x110, int64_t{456} << 32);
    livemap::scene::set_entity_grid(reinterpret_cast<uintptr_t>(world.data()) + 0x1f8);
    const auto found = livemap::scene::player_transforms({1});
    if (found.size() != 1 || found[0].entity != 1 || found[0].position[0] != (int64_t{123} << 32) ||
        found[0].position[2] != (int64_t{456} << 32)) return 1;
    put(chunk, 0x100, int64_t{124} << 32);
    const auto moved = livemap::scene::player_transforms({1});
    if (moved.size() != 1 || moved[0].position[0] != (int64_t{124} << 32)) return 1;
    if (!livemap::scene::player_transforms({2}).empty()) return 1;
    std::puts("Player transform read and movement update passed");
}
