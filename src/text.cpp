#include "text.h"

#include <cstdint>
#include <charconv>

namespace livemap::text {

namespace {

bool read_hex(std::string_view json, size_t& i, uint32_t& cp) {
    if (json.size() - i < 4) return false;
    const char* end = json.data() + i + 4;
    const auto result = std::from_chars(json.data() + i, end, cp, 16);
    i += 4;
    return result.ec == std::errc{} && result.ptr == end;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else {
        const int tail = cp < 0x800 ? 1 : cp < 0x10000 ? 2 : 3;
        constexpr unsigned lead[] = {0, 0xc0, 0xe0, 0xf0};
        out += static_cast<char>(lead[tail] | (cp >> (6 * tail)));
        for (int shift = 6 * (tail - 1); shift >= 0; shift -= 6) {
            out += static_cast<char>(0x80 | ((cp >> shift) & 0x3f));
        }
    }
}

bool read_json_string(std::string_view json, size_t& i, std::string& out) {
    while (i < json.size()) {
        const char c = json[i++];
        if (c == '"') return true;
        if (c != '\\') {
            if (static_cast<unsigned char>(c) < 0x20) return false;
            out += c;
            continue;
        }
        if (i == json.size()) return false;
        switch (const char escaped = json[i++]) {
        case '"': case '\\': case '/': out += escaped; break;
        case 'b': case 'f': case 'n': case 'r': case 't': out += ' '; break;
        case 'u': {
            uint32_t cp = 0;
            if (!read_hex(json, i, cp)) return false;
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (json.substr(i, 2) != "\\u") return false;
                i += 2;
                uint32_t low = 0;
                if (!read_hex(json, i, low) || low < 0xdc00 || low > 0xdfff) return false;
                cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
            } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                return false;
            }
            append_utf8(out, cp);
            break;
        }
        default: return false;
        }
    }
    return false;
}

}  // namespace

std::string json_string_member(std::string_view json, std::string_view name) {
    constexpr std::string_view whitespace = " \t\r\n";
    if (json.starts_with("\xef\xbb\xbf")) json.remove_prefix(3);
    size_t i = json.find_first_not_of(whitespace);
    if (i == json.npos || json[i++] != '{') return {};
    int depth = 1;
    bool member = true;
    while (i < json.size() && depth > 0) {
        const char c = json[i++];
        if (c == '"') {
            std::string token;
            if (!read_json_string(json, i, token)) return {};
            if (depth != 1 || !member) continue;
            member = false;
            if (token != name) continue;
            i = json.find_first_not_of(whitespace, i);
            if (i == json.npos || json[i++] != ':') return {};
            i = json.find_first_not_of(whitespace, i);
            if (i == json.npos || json[i++] != '"') return {};
            std::string value;
            return read_json_string(json, i, value) ? clean_utf8(value) : std::string{};
        }
        if (c == '{' || c == '[') ++depth;
        if (c == '}' || c == ']') --depth;
        if (c == ',' && depth == 1) member = true;
    }
    return {};
}

std::optional<bool> json_bool_member(std::string_view json, std::string_view name) {
    constexpr std::string_view whitespace = " \t\r\n";
    size_t i = json.find_first_not_of(whitespace);
    if (i == json.npos || json[i++] != '{') return {};
    int depth = 1;
    bool member = true;
    while (i < json.size() && depth > 0) {
        const char c = json[i++];
        if (c == '"') {
            std::string token;
            if (!read_json_string(json, i, token)) return {};
            if (depth != 1 || !member) continue;
            member = false;
            if (token != name) continue;
            i = json.find_first_not_of(whitespace, i);
            if (i == json.npos || json[i++] != ':') return {};
            i = json.find_first_not_of(whitespace, i);
            if (i == json.npos) return {};
            const auto tail = json.substr(i);
            const size_t n = tail.starts_with("true") ? 4 : tail.starts_with("false") ? 5 : 0;
            if (!n) return {};
            const size_t end = json.find_first_not_of(whitespace, i + n);
            if (end == json.npos || (json[end] != ',' && json[end] != '}')) return {};
            return n == 4;
        }
        if (c == '{' || c == '[') ++depth;
        if (c == '}' || c == ']') --depth;
        if (c == ',' && depth == 1) member = true;
    }
    return {};
}

std::string escape_html(std::string_view text) {
    std::string result;
    for (const char c : text) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        default: result += c; break;
        }
    }
    return result;
}

std::string clean_utf8(std::string_view bytes) {
    static constexpr char kReplacement[] = "\xEF\xBF\xBD";
    std::string out;
    out.reserve(bytes.size());
    size_t i = 0;
    while (i < bytes.size()) {
        const auto b = static_cast<uint8_t>(bytes[i]);
        if (b < 0x80) {
            out += (b < 0x20 || b == 0x7F) ? ' ' : static_cast<char>(b);
            ++i;
            continue;
        }
        // Sequence length and the smallest code point it may encode (rejects overlong forms).
        size_t length = 0;
        uint32_t cp = 0, min = 0;
        if (b >= 0xC2 && b <= 0xDF) { length = 2; cp = b & 0x1F; min = 0x80; }
        else if (b >= 0xE0 && b <= 0xEF) { length = 3; cp = b & 0x0F; min = 0x800; }
        else if (b >= 0xF0 && b <= 0xF4) { length = 4; cp = b & 0x07; min = 0x10000; }
        bool ok = length != 0 && i + length <= bytes.size();
        for (size_t k = 1; ok && k < length; ++k) {
            const auto c = static_cast<uint8_t>(bytes[i + k]);
            if ((c & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (c & 0x3F);
        }
        if (ok && (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))) ok = false;
        if (!ok) {
            out += kReplacement;
            ++i;
            continue;
        }
        out.append(bytes.substr(i, length));
        i += length;
    }
    return out;
}

}  // namespace livemap::text
