// Start the Steam game and load the same iphlpapi.dll used by the dedicated server.
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <string>

namespace {
int fail(const wchar_t* message) {
    MessageBoxW(nullptr, message, L"Enshrouded Live Map", MB_OK | MB_ICONERROR);
    return 1;
}

uintptr_t remote_kernel(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof entry;
    uintptr_t result = 0;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, L"kernel32.dll") == 0) {
                result = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

HANDLE running_game(const std::filesystem::path& exe, DWORD& pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return nullptr;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    HANDLE process = nullptr;
    if (Process32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szExeFile, L"enshrouded.exe") != 0) continue;
        HANDLE candidate = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                       PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | SYNCHRONIZE,
                                       FALSE, entry.th32ProcessID);
        if (!candidate) continue;
        wchar_t path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(candidate, 0, path, &size) && _wcsicmp(path, exe.c_str()) == 0) {
            process = candidate;
            pid = entry.th32ProcessID;
            break;
        }
        CloseHandle(candidate);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return process;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const auto dir = std::filesystem::path(path).parent_path();
    const auto exe = dir / L"enshrouded.exe";
    const auto dll = dir / L"iphlpapi.dll";
    if (!std::filesystem::is_regular_file(exe) || !std::filesystem::is_regular_file(dll))
        return fail(L"Place EnshroudedWithLivemap.exe and iphlpapi.dll next to enshrouded.exe, with the livemap folder.");
    SetEnvironmentVariableW(L"SteamAppId", L"1203620");
    SetEnvironmentVariableW(L"SteamGameId", L"1203620");
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    process.hProcess = running_game(exe, process.dwProcessId);
    if (!process.hProcess) {
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            dir.c_str(), &startup, &process))
            return fail(L"Could not start Enshrouded. Start Steam and try again.");
        CloseHandle(process.hThread);
    }
    uintptr_t kernel = 0;
    const ULONGLONG deadline = GetTickCount64() + 30000;
    while (!kernel && GetTickCount64() < deadline && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
        kernel = remote_kernel(process.dwProcessId);
        if (!kernel) Sleep(50);
    }
    HMODULE local_kernel = GetModuleHandleW(L"kernel32.dll");
    const auto load = GetProcAddress(local_kernel, "LoadLibraryW");
    // Resolve the owning module as forwarded exports may reside in KernelBase.
    HMODULE owner = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR>(load), &owner);
    uintptr_t remote_owner = 0;
    if (owner == local_kernel) remote_owner = kernel;
    else {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, process.dwProcessId);
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof entry;
        wchar_t owner_path[MAX_PATH]{};
        GetModuleFileNameW(owner, owner_path, MAX_PATH);
        const auto owner_name = std::filesystem::path(owner_path).filename().wstring();
        if (snapshot != INVALID_HANDLE_VALUE) {
            if (Module32FirstW(snapshot, &entry)) do {
                if (_wcsicmp(entry.szModule, owner_name.c_str()) == 0)
                    remote_owner = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
            } while (Module32NextW(snapshot, &entry));
            CloseHandle(snapshot);
        }
    }
    const std::wstring dll_path = dll.wstring();
    const size_t bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    void* memory = remote_owner ? VirtualAllocEx(process.hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
    HANDLE thread = nullptr;
    if (memory && WriteProcessMemory(process.hProcess, memory, dll_path.c_str(), bytes, nullptr)) {
        const uintptr_t target = remote_owner + reinterpret_cast<uintptr_t>(load) - reinterpret_cast<uintptr_t>(owner);
        thread = CreateRemoteThread(process.hProcess, nullptr, 0,
                                    reinterpret_cast<LPTHREAD_START_ROUTINE>(target), memory, 0, nullptr);
    }
    DWORD loaded = 0;
    if (thread) {
        // Keep the path alive until LoadLibrary finishes, even during a slow game start.
        WaitForSingleObject(thread, INFINITE);
        GetExitCodeThread(thread, &loaded);
        CloseHandle(thread);
    }
    if (memory) VirtualFreeEx(process.hProcess, memory, 0, MEM_RELEASE);
    CloseHandle(process.hProcess);
    if (!loaded) return fail(L"Enshrouded started, but Live Map could not load. Check livemap.log and the game installation.");
    return 0;
}
