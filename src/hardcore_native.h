#pragma once
// Native ABI facts adapted from EMF contributors' MIT engine research.
// See dependencies/emf/LICENSE.txt and docs/HARDCORE.md.
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace livemap::hardcore::native {
struct Name { const char* text; uint64_t size; };
struct List { const Name* data; uint64_t size; };
struct Program {
    Name name; void (*update)(void*);
    List tpl, tpl_opt, read, read_opt, write, write_opt, add, remove;
    List sys_read; uint64_t sys_read_optional;
    List sys_write; uint64_t sys_write_optional, sys_write_parallel;
    List other_tpl, other_read, other_write, other_unserialized, with, without, any, all_write, all_read;
    List events_in, events_out; uint32_t flags, interval;
};
static_assert(sizeof(Program)==0x188);
struct Stack { uint32_t item, count, persistent_id; };
struct Inventory { Stack slots[8]; };
struct Setup { uint32_t links[16]; uint8_t categories[16], flags, generic, available, initialized; };
static_assert(sizeof(Inventory)==96 && sizeof(Setup)==84);
struct Transform { int64_t position[3]; float orientation[4]; int64_t scale[2]; };
static_assert(sizeof(Transform)==56);
struct Teleport { Transform target; uint32_t entity, padding; int64_t delay; uint8_t applied, ground, pad[6]; };
static_assert(sizeof(Teleport)==80 && offsetof(Teleport,entity)==56);
constexpr uint32_t hash(const char* s) { uint32_t h=2166136261u; while(*s)h=(h^static_cast<uint8_t>(*s++))*16777619u; return h; }
constexpr uint32_t storage=0xE179C0;
constexpr uint32_t level_id=0x724bf568, xp_id=0x8dcddaf4, pending_xp_id=0x1d7dd102, required_xp_id=0x04723f62, health_id=0x8eb84995;
inline bool exact_session(uint32_t expected_handle, uint64_t expected_account, uint32_t handle, uint64_t account) {
    return expected_handle && expected_account && expected_handle==handle && expected_account==account;
}
inline bool progress_proof(int32_t level, int32_t xp, int32_t pending, bool inventory_empty, bool skills_empty) {
    return level==1 && xp==0 && pending==0 && inventory_empty && skills_empty;
}
inline bool near_start(const Transform& a,const Transform& b) {
    double sum=0;for(int i=0;i<3;++i){const double d=static_cast<double>(a.position[i])/4294967296.0-static_cast<double>(b.position[i])/4294967296.0;sum+=d*d;}return sum<=625.0;
}
}
