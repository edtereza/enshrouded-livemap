#pragma once
#include <cstdint>
namespace livemap::hardcore {
// Optional telemetry bridge. All hardcore engine hooks belong to Guardian.
bool install(const uint8_t* image);
void manager(uintptr_t server);
void death(uint32_t handle, uint64_t account);
}
