#pragma once
#include <string>
#include <string_view>
#include <optional>

namespace livemap::text {

// Bytes from game memory as text for JSON and the log: valid UTF-8 sequences stay, every
// invalid byte becomes U+FFFD, control characters become spaces.
std::string clean_utf8(std::string_view bytes);

// Decodes the string value of a top-level JSON object member.
std::string json_string_member(std::string_view json, std::string_view name);
std::optional<bool> json_bool_member(std::string_view json, std::string_view name);

std::string escape_html(std::string_view text);
std::string quote_json(std::string_view text);

}  // namespace livemap::text
