#include "hardcore.h"
#include <windows.h>
#include <cstdio>
int wmain(int argc,wchar_t** argv){if(argc!=2)return 2;if(livemap::hardcore::install(nullptr))return 3;livemap::hardcore::manager(0);livemap::hardcore::death(0,0);auto fixture=LoadLibraryW(argv[1]);if(!fixture)return 4;if(!livemap::hardcore::install(nullptr))return 5;livemap::hardcore::manager(0x1234);livemap::hardcore::death(65,99);auto check=reinterpret_cast<bool(*)()>(GetProcAddress(fixture,"BridgeFixtureVerified"));if(!check||!check())return 6;FreeLibrary(fixture);if(livemap::hardcore::install(nullptr))return 7;return 0;}
