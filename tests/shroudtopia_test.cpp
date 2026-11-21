// Exercise the actual DLL through the loader's public ABI, including its vtable.
#include <windows.h>
#include <cstdio>
#include <string>

struct ModContext;
struct ModMetaData {
    std::string name, description, version, author, targetShroudtopiaVersion;
    bool hasClientSupport, hasServerSupport;
};
class Mod {
public:
    bool loaded = false, active = false;
    virtual ~Mod() = default;
    virtual ModMetaData GetMetaData() = 0;
    virtual void Load(ModContext*) = 0;
    virtual void Unload(ModContext*) = 0;
    virtual void Activate(ModContext*) = 0;
    virtual void Deactivate(ModContext*) = 0;
    virtual void Update(ModContext*) = 0;
};

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 1;
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) return 2;
    auto factory = reinterpret_cast<Mod*(*)()>(GetProcAddress(dll, "CreateModInstance"));
    if (!factory) return 3;
    Mod* mod = factory();
    auto meta = mod->GetMetaData();
    if (meta.name != "Livemap" || meta.version != LIVEMAP_VERSION ||
        !meta.hasClientSupport || !meta.hasServerSupport || mod->active || mod->loaded) return 4;
    mod->Load(nullptr);
    mod->Update(nullptr);
    if (!mod->loaded || mod->active) return 5;
    wchar_t gate_name[96]{};
    swprintf_s(gate_name, L"Local\\EnshroudedLiveMap.%lu", GetCurrentProcessId());
    HANDLE gate = OpenMutexW(SYNCHRONIZE, FALSE, gate_name);
    if (gate) { CloseHandle(gate); return 6; } // Load must not activate the runtime.
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    const std::wstring path(executable);
    const std::wstring ini = path.substr(0, path.find_last_of(L"\\/")) + L"\\livemap.ini";
    // Isolated test executable: never install game hooks.
    if (!WritePrivateProfileStringW(L"livemap", L"Enabled", L"0", ini.c_str())) return 12;
    mod->Activate(nullptr);
    if (!mod->active) return 7;
    gate = OpenMutexW(SYNCHRONIZE, FALSE, gate_name);
    if (!gate) return 8;
    CloseHandle(gate);
    mod->Activate(nullptr); // Idempotent activation.
    if (!mod->active) return 9;
    mod->Deactivate(nullptr);
    mod->Unload(nullptr);
    if (mod->loaded || mod->active) return 10;
    delete mod;
    FreeLibrary(dll);
    HMODULE resident = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, argv[1], &resident)) return 11;
    std::puts("Shroudtopia ABI, passive loading, activation gate and resident hooks: OK");
    return 0;
}
