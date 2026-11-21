#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <algorithm>

namespace livemap {

namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
std::mutex g_console_mutex;
}  // namespace

void log_open(const std::wstring& dir, bool enabled) {
    std::lock_guard lock(g_mutex);
    if (!enabled || g_file != nullptr) {
        return;
    }
    std::wstring path = dir + L"\\livemap.log";
    g_file = _wfsopen(path.c_str(), L"a", _SH_DENYWR);
}

void log(const char* fmt, ...) {
    std::lock_guard lock(g_mutex);
    if (g_file == nullptr) {
        return;
    }
    SYSTEMTIME t;
    GetSystemTime(&t);
    std::fprintf(g_file, "%04d-%02d-%02d %02d:%02d:%02d.%03d ", t.wYear, t.wMonth, t.wDay,
                 t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_file, fmt, args);
    va_end(args);
    std::fputc('\n', g_file);
    std::fflush(g_file);
}

void console(const std::vector<std::string>& lines, bool boxed) {
    size_t width = 0;
    for (const auto& line : lines) width = (std::max)(width, line.size());
    const std::string border = "[Livemap] +" + std::string(width + 4, '=') + "+\r\n";
    std::string message = boxed ? border : std::string{};
    for (const auto& line : lines)
        message += boxed ? "[Livemap] |  " + line + std::string(width - line.size(), ' ') + "  |\r\n"
                         : "[Livemap] " + line + "\r\n";
    if (boxed) message += border;
    std::lock_guard lock(g_console_mutex);
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
}

}  // namespace livemap
