#pragma once
#include "text.h"
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>

namespace livemap::updates {
inline constexpr wchar_t api_path[] = L"/repos/edtereza/enshrouded-livemap/releases/latest";
inline constexpr char release_page[] = "https://github.com/edtereza/enshrouded-livemap/releases/latest";

inline std::optional<std::array<uint32_t, 3>> version(std::string_view tag) {
    if (tag.starts_with('v') || tag.starts_with('V')) tag.remove_prefix(1);
    std::array<uint32_t, 3> result{};
    for (size_t i = 0; i < result.size(); ++i) {
        const size_t dot = tag.find('.');
        const auto part = tag.substr(0, dot);
        if (part.empty()) return {};
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), result[i]);
        if (error != std::errc{} || end != part.data() + part.size()) return {};
        if (i == 2) { if (dot != tag.npos) return {}; }
        else { if (dot == tag.npos) return {}; tag.remove_prefix(dot + 1); }
    }
    return result;
}

inline std::string release_tag(std::string_view json) {
    if (text::json_bool_member(json, "draft") != std::optional<bool>{false} ||
        text::json_bool_member(json, "prerelease") != std::optional<bool>{false}) return {};
    auto tag = text::json_string_member(json, "tag_name");
    return version(tag) ? tag : std::string{};
}

inline bool newer(std::string_view candidate, std::string_view current) {
    const auto a = version(candidate), b = version(current);
    return a && b && *a > *b;
}

// Initial request runs synchronously before hooks/HTTP startup; reminders run on a worker.
// Informational only. Caller must keep the DLL resident.
void check_on_startup(std::string current);
}
