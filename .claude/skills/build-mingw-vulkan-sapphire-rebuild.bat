@echo off
setlocal EnableExtensions

if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help

set "BASE_SCRIPT=%~dp0build-mingw-vulkan-existing.bat"
if not exist "%BASE_SCRIPT%" (
    echo [melonprime-build-vulkan-sapphire-rebuild] Missing base script: %BASE_SCRIPT%
    exit /b 1
)

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
set "BUILD_DIR=build/release-mingw-x86_64"
set "CACHE=%REPO_ROOT_WIN%\%BUILD_DIR%\CMakeCache.txt"
if not exist "%CACHE%" (
    echo [melonprime-build-vulkan-sapphire-rebuild] Missing %BUILD_DIR% tree.
    echo [melonprime-build-vulkan-sapphire-rebuild] Run .claude\skills\build-mingw-vulkan.bat first.
    exit /b 1
)

echo [melonprime-build-vulkan-sapphire-rebuild] Verifying Sapphire generated sources.
python "%REPO_ROOT_WIN%\tools\generate_sapphire_vulkan_sources.py" --verify
if errorlevel 1 exit /b 1

echo [melonprime-build-vulkan-sapphire-rebuild] Enabling rebuild flags on existing Vulkan tree.
"%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && export PATH=$repo'/build/.mingw-make-shim:/mingw64/bin:/usr/bin:$PATH' && /mingw64/bin/cmake.exe -S . -B %BUILD_DIR% -DMELONPRIME_SAPPHIRE_REBUILD=ON -DMELONPRIME_SAPPHIRE_REBUILD_SOLID_COLOR=OFF -DMELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=OFF"
if errorlevel 1 exit /b 1

python "%REPO_ROOT_WIN%\tools\generate_build_identity.py" --repo "%REPO_ROOT_WIN%" --output "%REPO_ROOT_WIN%\%BUILD_DIR%\src\MelonPrimeGitBuildIdentity.h"
if errorlevel 1 (
    echo [melonprime-build-vulkan-sapphire-rebuild] WARNING: generate_build_identity.py failed; continuing.
)

echo [melonprime-build-vulkan-sapphire-rebuild] Building incremental rebuild target.
call "%BASE_SCRIPT%" %*
exit /b %ERRORLEVEL%

:help
echo Usage: .claude\skills\build-mingw-vulkan-sapphire-rebuild.bat [--jobs N]
echo.
echo Reconfigure the existing release-mingw-x86_64 Vulkan tree with
echo MELONPRIME_SAPPHIRE_REBUILD=ON and build incrementally.
exit /b 0
