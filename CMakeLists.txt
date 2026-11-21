cmake_minimum_required(VERSION 3.28)
project(enshrouded_livemap VERSION 1.12.2 LANGUAGES CXX ASM_MASM RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the Wine prefix of the server image needs no extra runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
configure_file(src/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)

add_executable(EnshroudedWithLivemap WIN32 tools/local_launcher.cpp)
target_link_libraries(EnshroudedWithLivemap PRIVATE user32)
target_compile_definitions(EnshroudedWithLivemap PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

include(FetchContent)
set(SAFETYHOOK_FETCH_ZYDIS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    safetyhook
    GIT_REPOSITORY https://github.com/cursey/safetyhook.git
    GIT_TAG v0.7.0
)
FetchContent_MakeAvailable(safetyhook)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(zlib)

add_library(livemap_core STATIC src/scan.cpp src/finds.cpp src/link.cpp src/text.cpp src/bases.cpp src/a2s.cpp src/discovered.cpp src/explored.cpp
    src/memory.cpp src/names.cpp src/deaths.cpp src/objects.cpp)
target_include_directories(livemap_core PUBLIC src)

# The DLL is named iphlpapi.dll because the server loads it in place of the real one.
set(LIVEMAP_SOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
    src/main.cpp
    src/config.cpp
    src/log.cpp
    src/ecs.cpp
    src/scene.cpp
    src/world.cpp
    src/web.cpp
    src/announce.cpp
    src/public_ip.cpp
    src/updates.cpp
)
add_library(iphlpapi SHARED ${LIVEMAP_SOURCES} src/proxy.cpp src/proxy_stubs.asm src/proxy.def)
add_library(livemap SHARED ${LIVEMAP_SOURCES} src/shroudtopia.cpp)
target_compile_definitions(livemap PRIVATE LIVEMAP_SHROUDTOPIA)
foreach(target iphlpapi livemap)
    target_link_libraries(${target} PRIVATE livemap_core safetyhook::safetyhook zlibstatic ws2_32 bcrypt winhttp)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS LIVEMAP_VERSION="${PROJECT_VERSION}")
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->)
endforeach()

enable_testing()
foreach(test finds link text bases a2s discovered explored names deaths objects updates)
    add_executable(${test}_test tests/${test}_test.cpp)
    target_link_libraries(${test}_test PRIVATE livemap_core)
    add_test(NAME ${test} COMMAND ${test}_test)
endforeach()

add_executable(scene_player_test tests/scene_player_test.cpp src/scene.cpp src/log.cpp)
target_link_libraries(scene_player_test PRIVATE livemap_core)
add_test(NAME scene_player COMMAND scene_player_test)

# Offline compatibility check against an executable on disk.
add_executable(livemap_selftest tools/selftest.cpp)
target_link_libraries(livemap_selftest PRIVATE livemap_core)
target_compile_definitions(livemap_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

add_executable(guardian_assets_test tests/guardian_assets_test.cpp)
target_include_directories(guardian_assets_test PRIVATE src)
add_test(NAME guardian_assets COMMAND guardian_assets_test)

add_executable(shroudtopia_test tests/shroudtopia_test.cpp)
target_compile_definitions(shroudtopia_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    LIVEMAP_VERSION="${PROJECT_VERSION}")
# The runtime creates its INI/log beside the executable; isolate those test files.
set_target_properties(shroudtopia_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shroudtopia-test")
add_dependencies(shroudtopia_test livemap)
add_test(NAME shroudtopia COMMAND shroudtopia_test $<TARGET_FILE:livemap>)

add_executable(title_test tests/title_test.cpp src/config.cpp)
target_link_libraries(title_test PRIVATE livemap_core bcrypt)
target_compile_definitions(title_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_test(NAME title COMMAND title_test)
