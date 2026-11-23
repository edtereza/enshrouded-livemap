#include <cstdint>
static uintptr_t manager_value{};static uint32_t handle_value{};static uint64_t account_value{};
extern "C" __declspec(dllexport) uint32_t GuardianNativeBackendVersion(){return 2;}
extern "C" __declspec(dllexport) void GuardianNativeManager(uintptr_t value){manager_value=value;}
extern "C" __declspec(dllexport) void GuardianNativeDeath(uint32_t handle,uint64_t account){handle_value=handle;account_value=account;}
extern "C" __declspec(dllexport) bool BridgeFixtureVerified(){return manager_value==0x1234&&handle_value==65&&account_value==99;}
