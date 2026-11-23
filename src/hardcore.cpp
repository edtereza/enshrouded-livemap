// Optional bridge only: Guardian owns native hardcore hooks and simulation.
#include "hardcore.h"
#include "log.h"
#include <windows.h>
namespace livemap::hardcore {
namespace {
struct Api {uint32_t(*version)();void(*manager)(uintptr_t);void(*death)(uint32_t,uint64_t);};
Api api(){auto module=GetModuleHandleW(L"guardian.dll");if(!module)return {};Api found{reinterpret_cast<uint32_t(*)()>(GetProcAddress(module,"GuardianNativeBackendVersion")),reinterpret_cast<void(*)(uintptr_t)>(GetProcAddress(module,"GuardianNativeManager")),reinterpret_cast<void(*)(uint32_t,uint64_t)>(GetProcAddress(module,"GuardianNativeDeath"))};if(!found.version||found.version()<2)return {};return found;}
}
bool install(const uint8_t*){auto guard=api();log(guard.version?"HARDCORE: integrated Guardian backend detected; optional telemetry bridge only.":"HARDCORE: no integrated Guardian backend detected; Live Map installs no hardcore hooks.");return guard.version!=nullptr;}
void manager(uintptr_t value){auto guard=api();if(guard.manager)guard.manager(value);}
void death(uint32_t handle,uint64_t account){auto guard=api();if(guard.death)guard.death(handle,account);}
}
