#pragma once
#include <cstdint>
namespace livemap::hardcore {
// Dedicated-server backend, verified build only; never uses proximity/name matching.
bool probe(const uint8_t* image);
bool install(const uint8_t* image);
void manager(uintptr_t server);
void death(uint32_t handle, uint64_t account);
}
