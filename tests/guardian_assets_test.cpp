#include "guardian_assets.h"
#include <iostream>
int main() {
    using namespace livemap::guardian_assets;
    for (const char* path : {"3.1.0/viewer.css", "3.1.0/catalogs.js", "3.1.0/images/item-123.webp", "3.1.0/images/ui-ab09.webp", "3.1.0/images/css-0.webp"})
        if (!valid_path(path)) return 1;
    for (const char* path : {"../guardian.json", "3.1.0/../../guardian.json", "3.1.0/images/../viewer.js", "3.1.0/ready", "3.1.0/images/item-%31.webp", "3.1.0/images/item-1.webp:stream", "3.1.0\\viewer.js"})
        if (valid_path(path)) return 2;
    std::string html = "<link href=\"../assets/3.1.0/viewer.css\"><script src=\"../assets/3.1.0/catalogs.js\"></script>";
    rewrite_sheet(html, "abc-123");
    if (html != "<link href=\"/assets/3.1.0/viewer.css?key=abc-123\"><script src=\"/assets/3.1.0/catalogs.js?key=abc-123\"></script>") return 3;
    std::string css = "background:url(images/css-0.webp) center";
    rewrite_css(css, "3.1.0", "abc-123");
    if (css != "background:url(/assets/3.1.0/images/css-0.webp?key=abc-123) center") return 4;
    std::string local = "<link href=\"../assets/3.1.0/viewer.css\">";
    rewrite_sheet(local, "");
    if (local != "<link href=\"/assets/3.1.0/viewer.css\">") return 5;
    std::cout << "Guardian shared asset paths and authentication passed\n";
}
