@echo off
rem Builds livemap (Release) with the installed Visual Studio C++ tools.
setlocal
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "vcvars="
if exist "%vswhere%" (
    for /f "usebackq delims=" %%I in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do set "vcvars=%%I"
)
if not defined vcvars (
    >&2 echo MSVC x64/x86 tools not found. Install the Desktop development with C++ workload in Visual Studio Installer.
    exit /b 1
)
call "%vcvars%" >nul || exit /b 1
where cmake.exe >nul 2>nul && where ninja.exe >nul 2>nul
if errorlevel 1 (
    >&2 echo CMake or Ninja not found. Install C++ CMake tools for Windows or add both tools to PATH.
    exit /b 1
)
cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1
ctest --test-dir build --output-on-failure || exit /b 1
