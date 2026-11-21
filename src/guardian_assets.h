#pragma once
#include <string>
#include <string_view>
namespace livemap::guardian_assets {
inline bool valid_path(std::string_view path) {
    const auto slash = path.find('/');
    if (slash == std::string_view::npos || slash == 0 || path.find("..") != std::string_view::npos) return false;
    for (char c : path.substr(0, slash)) if ((c < '0' || c > '9') && c != '.') return false;
    auto file = path.substr(slash + 1);
    if (file == "viewer.js" || file == "viewer.css" || file == "catalogs.js") return true;
    if (!file.starts_with("images/") || !file.ends_with(".webp")) return false;
    file.remove_prefix(7); file.remove_suffix(5);
    bool hex = file.starts_with("ui-");
    if (hex) file.remove_prefix(3);
    else if (file.starts_with("item-")) file.remove_prefix(5);
    else if (file.starts_with("css-")) file.remove_prefix(4);
    else return false;
    if (file.empty()) return false;
    for (char c : file) if ((c < '0' || c > '9') && !(hex && c >= 'a' && c <= 'f')) return false;
    return true;
}
inline std::string key_suffix(const std::string& key) {
    if (key.empty()) return {};
    std::string result = "?key=";
    constexpr char digits[] = "0123456789ABCDEF";
    for (unsigned char c : key) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') result += static_cast<char>(c);
        else { result += '%'; result += digits[c >> 4]; result += digits[c & 15]; }
    }
    return result;
}
inline void rewrite_sheet(std::string& html, const std::string& key) {
    size_t pos = 0;
    while ((pos = html.find("../assets/", pos)) != std::string::npos) {
        const size_t end = html.find('"', pos);
        if (end == std::string::npos) break;
        const std::string relative = html.substr(pos + 10, end - pos - 10);
        if (!valid_path(relative)) { pos = end + 1; continue; }
        const auto replacement = "/assets/" + relative + key_suffix(key);
        html.replace(pos, end - pos, replacement);
        pos += replacement.size();
    }
}
inline void rewrite_css(std::string& css, const std::string& version, const std::string& key) {
    size_t pos = 0;
    while ((pos = css.find("url(images/", pos)) != std::string::npos) {
        const size_t end = css.find(')', pos);
        if (end == std::string::npos) break;
        const std::string relative = version + "/" + css.substr(pos + 4, end - pos - 4);
        if (!valid_path(relative)) { pos = end + 1; continue; }
        const auto replacement = "url(/assets/" + relative + key_suffix(key) + ")";
        css.replace(pos, end - pos + 1, replacement);
        pos += replacement.size();
    }
}
}
