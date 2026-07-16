@echo off
setlocal EnableExtensions

if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help

set "BASH=C:\msys64\usr\bin\bash.exe"
if not exist "%BASH%" (
    echo [melonprime-build-vulkan-sapphire-rebuild] Missing MSYS2 bash: %BASH%
    exit /b 1
)

set "SEARCH_DIR=%~dp0"
:find_repo_root
if exist "%SEARCH_DIR%CMakeLists.txt" (
    for %%I in ("%SEARCH_DIR%.") do set "REPO_ROOT_WIN=%%~fI"
    goto repo_root_found
)
for %%I in ("%SEARCH_DIR%..\") do set "PARENT_DIR=%%~fI\"
if /I "%PARENT_DIR%"=="%SEARCH_DIR%" (
    echo [melonprime-build-vulkan-sapphire-rebuild] Could not find CMakeLists.txt above %~dp0
    exit /b 1
)
set "SEARCH_DIR=%PARENT_DIR%"
goto find_repo_root

:repo_root_found
set "BUILD_DIR=%REPO_ROOT_WIN%\build\release-mingw-x86_64-sapphire-rebuild"
set "JOBS=1"

echo [melonprime-build-vulkan-sapphire-rebuild] Verifying Sapphire generated sources.
python "%REPO_ROOT_WIN%\tools\generate_sapphire_vulkan_sources.py" --verify
if errorlevel 1 exit /b 1

echo [melonprime-build-vulkan-sapphire-rebuild] Configuring pure-Sapphire rebuild tree.
"%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && export PATH=$repo'/build/.mingw-make-shim:/mingw64/bin:/usr/bin:$PATH' && /mingw64/bin/cmake.exe -S . -B build/release-mingw-x86_64-sapphire-rebuild -DCMAKE_BUILD_TYPE=Release -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON -DMELONPRIME_ENABLE_VULKAN=ON -DMELONPRIME_FORCE_DISABLE_VULKAN=OFF -DMELONPRIME_SAPPHIRE_REBUILD=ON -DMELONPRIME_SAPPHIRE_REBUILD_SOLID_COLOR=ON -DUSE_VCPKG=ON -DBUILD_STATIC=ON"
if errorlevel 1 exit /b 1

python "%REPO_ROOT_WIN%\tools\generate_build_identity.py" --repo "%REPO_ROOT_WIN%" --output "%BUILD_DIR%\src\MelonPrimeGitBuildIdentity.h"
if errorlevel 1 exit /b 1

echo [melonprime-build-vulkan-sapphire-rebuild] Building rebuild target.
"%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && export PATH=$repo'/build/.mingw-make-shim:/mingw64/bin:/usr/bin:$PATH' && /mingw64/bin/cmake.exe --build build/release-mingw-x86_64-sapphire-rebuild --parallel %JOBS%"
exit /b %ERRORLEVEL%

:help
echo Usage: .claude\skills\build-mingw-vulkan-sapphire-rebuild.bat
echo.
echo Configure and build the pure-Sapphire desktop rebuild tree
echo (MELONPRIME_SAPPHIRE_REBUILD=ON, exact-pin GPU2D, solid-color phase 2).
exit /b 0
