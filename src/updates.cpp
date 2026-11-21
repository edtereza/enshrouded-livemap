#include "updates.h"
#include "log.h"
#include <windows.h>
#include <winhttp.h>
#include <chrono>
#include <thread>

namespace livemap::updates {
namespace {
struct InternetHandle {
    HINTERNET value;
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};

std::string latest(const std::string& current) {
    const std::wstring agent = L"Enshrouded-Livemap/" + std::wstring(current.begin(), current.end());
    InternetHandle session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return {};
    WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000);
    InternetHandle connection{WinHttpConnect(session.value, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    InternetHandle request{connection.value ? WinHttpOpenRequest(connection.value, L"GET", api_path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr};
    if (!request.value || !WinHttpSendRequest(request.value, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1L),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr)) return {};
    DWORD status = 0, size = sizeof status;
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200) return {};
    std::string body;
    char chunk[4096];
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request.value, chunk, sizeof chunk, &read)) return {};
        if (!read) break;
        if (body.size() + read > 1024 * 1024) return {};
        body.append(chunk, read);
    }
    return release_tag(body);
}

}

void check_on_startup(std::string current) {
    log("Checking Livemap version v%s against %s", current.c_str(), release_page);
    console({"Checking Livemap version v" + current + " against GitHub..."});
    auto tag = latest(current);
    const auto announce = [](const std::string& release, const std::string& running) {
        log("A new Livemap version is available: %s (running v%s): %s", release.c_str(), running.c_str(), release_page);
        console({"NEW LIVEMAP VERSION AVAILABLE: " + release, "Running v" + running, release_page});
    };
    if (newer(tag, current)) announce(tag, current);
    else if (tag.empty()) {
        const std::string message = "Update check: no published release could be read. No public release or request unavailable.";
        log("%s %s", message.c_str(), release_page);
        console({message, release_page});
    } else {
        const std::string message = "Livemap is up to date (v" + current + ")";
        log("%s", message.c_str());
        console({message});
    }
    // Only later checks and reminders are detached; the initial request has completed.
    std::thread([current = std::move(current), tag = std::move(tag), announce]() mutable {
        for (;;) {
            if (newer(tag, current)) {
                std::this_thread::sleep_for(std::chrono::hours(1));
                console({"NEW LIVEMAP VERSION AVAILABLE: " + tag, "Running v" + current, release_page});
            } else {
                std::this_thread::sleep_for(std::chrono::hours(24));
                tag = latest(current);
                if (newer(tag, current)) announce(tag, current);
            }
        }
    }).detach();
}
}
