#include "updates.h"
#include <cstdio>

int main() {
    using namespace livemap::updates;
    if (std::wstring_view(api_path) != L"/repos/edtereza/enshrouded-livemap/releases/latest" ||
        std::string_view(release_page) != "https://github.com/edtereza/enshrouded-livemap/releases/latest") return 1;
    if (!newer("v1.11.0", "1.9.9") || !newer("V2.0.0", "1.99.99") ||
        newer("v1.11.0", "1.11.0") || newer("v1.10.9", "1.11.0")) return 2;
    for (const auto bad : {"", "v", "1.2", "1.2.3.4", "1.2.3-beta", "-1.2.3", "1.2.4294967296", "garbage"}) {
        if (version(bad) || newer(bad, "1.0.0") || newer("2.0.0", bad)) return 3;
    }
    if (release_tag(R"({"tag_name":"v1.11.0","draft":false,"prerelease":false})") != "v1.11.0") return 4;
    for (const auto bad : {
        R"({"message":"Not Found"})",
        R"({"tag_name":"v2.0.0","draft":true,"prerelease":false})",
        R"({"tag_name":"v2.0.0","draft":false,"prerelease":true})",
        R"({"tag_name":"v2.0.0-beta","draft":false,"prerelease":false})",
        R"({"tag_name":"v2.0.0","draft":"false","prerelease":false})",
        R"({"tag_name":"v2.0.0","author":{"draft":false,"prerelease":false}})",
        R"({"tag_name":"v2.0.0","draft":falsehood,"prerelease":false})",
        R"({"tag_name":"","draft":false,"prerelease":false})",
        "not JSON"}) {
        if (!release_tag(bad).empty()) return 5;
    }
    if (release_tag(R"({"author":{"draft":true},"body":"\"prerelease\":true","tag_name":"v2.0.0","draft":false,"prerelease":false})") != "v2.0.0") return 6;
    std::puts("Update source, numeric versions and stable-release response filtering: OK");
    return 0;
}
