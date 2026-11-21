#include "config.h"
#include "text.h"
#include <windows.h>
#include <cstdio>
#include <string>

int main() {
    wchar_t temporary[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temporary);
    const std::wstring directory = std::wstring(temporary) + L"livemap-title-" + std::to_wstring(GetCurrentProcessId());
    if (!CreateDirectoryW(directory.c_str(), nullptr)) return 1;
    const std::wstring path = directory + L"\\livemap.ini";
    int failures = 0;
    const auto check = [&](const std::string& bytes, const std::string& expected) {
        FILE* file = _wfopen(path.c_str(), L"wb");
        if (!file) { ++failures; return; }
        std::fwrite(bytes.data(), 1, bytes.size(), file);
        std::fclose(file);
        if (livemap::read_title(directory) != expected) ++failures;
    };
    const std::string unicode = "S\xc3\xa3" "o Paulo \xe5\xae\x88\xe8\xad\xb7";
    check("[livemap]\nTitle=" + unicode + "  \n", unicode);
    check("\xef\xbb\xbf[livemap]\nTitle=" + unicode + "\n", unicode);
    check("; comment\n[ LIVEMAP ]\n title = \"A & B <map>\"\n", "A & B <map>");
    check("[other]\nTitle=Wrong\n[livemap]\nTitle=Correct\n", "Correct");
    check("[other]\nTitle=Wrong\n", "");
    check("[livemap]\nTitle= \t\n", "");
    check("[livemap]\nTitle=First\n", "First");
    check("[livemap]\nTitle=Second\n", "Second"); // Fresh reads observe edits without restart.
    const std::wstring wide = L"[livemap]\r\nTitle=S\u00e3o Paulo\r\n";
    std::string utf16 = "\xff\xfe";
    for (const wchar_t c : wide) { utf16 += static_cast<char>(c & 255); utf16 += static_cast<char>(c >> 8); }
    check(utf16, "S\xc3\xa3" "o Paulo");
    DeleteFileW(path.c_str());
    if (!livemap::read_title(directory).empty()) ++failures;
    RemoveDirectoryW(directory.c_str());
    const std::string value = "A \"quoted\" \\ title\n" + unicode;
    if (livemap::text::json_string_member("{\"title\":" + livemap::text::quote_json(value) + "}", "title")
        != livemap::text::clean_utf8(value)) ++failures;
    if (livemap::text::escape_html("</title><script>bad</script>") != "&lt;/title&gt;&lt;script&gt;bad&lt;/script&gt;") ++failures;
    if (failures) std::printf("Title regression failures: %d\n", failures);
    else std::puts("Title UTF-8/BOM/UTF-16, section isolation, live reads and escaping: OK");
    return failures ? 1 : 0;
}
