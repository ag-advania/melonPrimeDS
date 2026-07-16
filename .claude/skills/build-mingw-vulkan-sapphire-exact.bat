@echo off
setlocal EnableExtensions

if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help

set "BASH=C:\msys64\usr\bin\bash.exe"
if not exist "%BASH%" (
    echo [melonprime-build-vulkan-sapphire-exact] Missing MSYS2 bash: %BASH%
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
    echo [melonprime-build-vulkan-sapphire-exact] Could not find CMakeLists.txt above %~dp0
    exit /b 1
)
set "SEARCH_DIR=%PARENT_DIR%"
goto find_repo_root

:repo_root_found
set "BUILD_DIR=%REPO_ROOT_WIN%\build\release-mingw-x86_64-sapphire-exact"
set "JOBS=1"

echo [melonprime-build-vulkan-sapphire-exact] Configuring exact-pin GPU2D build tree.
"%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && export PATH=$repo'/build/.mingw-make-shim:/mingw64/bin:/usr/bin:$PATH' && /mingw64/bin/cmake.exe -S . -B build/release-mingw-x86_64-sapphire-exact -DCMAKE_BUILD_TYPE=Release -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON -DMELONPRIME_ENABLE_VULKAN=ON -DMELONPRIME_FORCE_DISABLE_VULKAN=OFF -DMELONPRIME_DIAGNOSTIC_SYMBOLS=ON -DMELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=ON -DUSE_VCPKG=ON -DBUILD_STATIC=ON"
if errorlevel 1 exit /b 1

python "%REPO_ROOT_WIN%\tools\generate_build_identity.py" --repo "%REPO_ROOT_WIN%" --output "%BUILD_DIR%\src\MelonPrimeGitBuildIdentity.h"
if errorlevel 1 exit /b 1

echo [melonprime-build-vulkan-sapphire-exact] Building exact-pin target.
"%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && export PATH=$repo'/build/.mingw-make-shim:/mingw64/bin:/usr/bin:$PATH' && /mingw64/bin/cmake.exe --build build/release-mingw-x86_64-sapphire-exact --parallel %JOBS%"
exit /b %ERRORLEVEL%

:help
echo Usage: .claude\skills\build-mingw-vulkan-sapphire-exact.bat
echo.
echo Configure and build a separate release tree with pinned upstream GPU2D_Soft.cpp
echo for Sapphire exact A/B cold-start comparison (S80-8).
exit /b 0
