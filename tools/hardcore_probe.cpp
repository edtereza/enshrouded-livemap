#include "hardcore.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
int wmain(int argc,wchar_t**argv){if(argc!=2)return 2;livemap::log_open(L".",true);auto image=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_AS_IMAGE_RESOURCE|LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);if(!image)return 3;auto base=reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(image)&~uintptr_t{3});bool ok=livemap::hardcore::probe(base);FreeLibrary(image);std::puts(ok?"Native patterns and RVAs verified (no engine code executed).":"Native compatibility probe failed.");return ok?0:1;}
