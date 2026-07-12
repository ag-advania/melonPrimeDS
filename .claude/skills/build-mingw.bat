@echo off
setlocal EnableExtensions

set "BASH=C:\msys64\usr\bin\bash.exe"
if not exist "%BASH%" (
    echo [melonprime-build] Missing MSYS2 bash: %BASH%
    echo [melonprime-build] Install MSYS2 at C:\msys64 or update this batch file.
    exit /b 1
)

set "JOBS=1"
set "TAIL_LINES=40"
set "VERBOSE=0"
set "VULKAN_MODE=default-off"
set "VULKAN_ARGS=-DMELONPRIME_ENABLE_VULKAN=OFF -DMELONPRIME_FORCE_DISABLE_VULKAN=OFF"
set "VCPKG_MAX_CONCURRENCY=1"

:parse_args
if "%~1"=="" goto run_build
if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
if /I "%~1"=="--verbose" (
    set "VERBOSE=1"
    shift
    goto parse_args
)
if /I "%~1"=="--vulkan" (
    set "VULKAN_MODE=enabled"
    set "VULKAN_ARGS=-DMELONPRIME_ENABLE_VULKAN=ON -DMELONPRIME_FORCE_DISABLE_VULKAN=OFF"
    shift
    goto parse_args
)
if /I "%~1"=="--force-disable-vulkan" (
    set "VULKAN_MODE=force-disabled"
    set "VULKAN_ARGS=-DMELONPRIME_ENABLE_VULKAN=ON -DMELONPRIME_FORCE_DISABLE_VULKAN=ON"
    shift
    goto parse_args
)
if /I "%~1"=="--jobs" (
    if "%~2"=="" goto usage
    set "JOBS=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="--tail" (
    if "%~2"=="" goto usage
    set "TAIL_LINES=%~2"
    shift
    shift
    goto parse_args
)

echo [melonprime-build] Unknown argument: %~1
goto usage

:help
call :print_usage
exit /b 0

:usage
call :print_usage
exit /b 2

:print_usage
echo Usage: .claude\skills\build-mingw.bat [--verbose] [--vulkan] [--force-disable-vulkan] [--jobs N] [--tail N]
echo.
echo Default: configure with MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON, then build
echo release-mingw-x86_64 with --parallel 1, streaming the build output live and
echo printing the last 40 log lines as a recap when it finishes. Full output is
echo also saved to build\release-mingw-x86_64\last-build.log.
exit /b 0

:run_build
set "SEARCH_DIR=%~dp0"

:find_repo_root
if exist "%SEARCH_DIR%CMakeLists.txt" (
    for %%I in ("%SEARCH_DIR%.") do set "REPO_ROOT_WIN=%%~fI"
    goto repo_root_found
)
for %%I in ("%SEARCH_DIR%..\") do set "PARENT_DIR=%%~fI\"
if /I "%PARENT_DIR%"=="%SEARCH_DIR%" (
    echo [melonprime-build] Could not find CMakeLists.txt above %~dp0
    exit /b 1
)
set "SEARCH_DIR=%PARENT_DIR%"
goto find_repo_root

:repo_root_found

echo [melonprime-build] Repo: %REPO_ROOT_WIN%
echo [melonprime-build] Preset: release-mingw-x86_64
echo [melonprime-build] Jobs: %JOBS%
echo [melonprime-build] Vulkan: %VULKAN_MODE%
echo [melonprime-build] vcpkg jobs: %VCPKG_MAX_CONCURRENCY%

if "%VERBOSE%"=="1" (
    "%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && export PATH='/mingw64/bin:/usr/bin:/c/Program Files/Python312:/c/Program Files/Python312/Scripts:'$repo'/build/release-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/tools/Qt6/bin:'$repo'/build/release-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/bin':$PATH && /mingw64/bin/cmake.exe -S . -B build/release-mingw-x86_64 -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON %VULKAN_ARGS% && /mingw64/bin/cmake.exe --build --preset=release-mingw-x86_64 --parallel %JOBS% --verbose"
) else (
    "%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && export PATH='/mingw64/bin:/usr/bin:/c/Program Files/Python312:/c/Program Files/Python312/Scripts:'$repo'/build/release-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/tools/Qt6/bin:'$repo'/build/release-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/bin':$PATH && /mingw64/bin/cmake.exe -S . -B build/release-mingw-x86_64 -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON %VULKAN_ARGS% && LOG=build/release-mingw-x86_64/last-build.log && stdbuf -oL -eL /mingw64/bin/cmake.exe --build --preset=release-mingw-x86_64 --parallel %JOBS% 2>&1 | tee $LOG; STATUS=${PIPESTATUS[0]}; echo; echo '[melonprime-build] Last '%TAIL_LINES%' log lines (full log: '$LOG'):'; tail -n %TAIL_LINES% $LOG; exit $STATUS"
)

set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
    echo [melonprime-build] Build failed with exit code %RESULT%.
) else (
    echo [melonprime-build] Build succeeded.
)
exit /b %RESULT%
