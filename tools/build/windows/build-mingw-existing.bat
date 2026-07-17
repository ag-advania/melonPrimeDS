@echo off
setlocal EnableExtensions

set "BASH=C:\msys64\usr\bin\bash.exe"
if not exist "%BASH%" (
    echo [melonprime-build-existing] Missing MSYS2 bash: %BASH%
    echo [melonprime-build-existing] Install MSYS2 at C:\msys64 or update this batch file.
    exit /b 1
)

set "JOBS=1"
set "TAIL_LINES=40"
set "VERBOSE=0"
set "BUILD_DIR=build\release-mingw-x86_64"

:parse_args
if "%~1"=="" goto run_build
if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
if /I "%~1"=="--verbose" (
    set "VERBOSE=1"
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
if /I "%~1"=="--build-dir" (
    if "%~2"=="" goto usage
    set "BUILD_DIR=%~2"
    shift
    shift
    goto parse_args
)

echo [melonprime-build-existing] Unknown argument: %~1
goto usage

:help
call :print_usage
exit /b 0

:usage
call :print_usage
exit /b 2

:print_usage
echo Usage: tools\build\windows\build-mingw-existing.bat [--verbose] [--jobs N] [--tail N] [--build-dir PATH]
echo.
echo Build only: skips CMake configure and vcpkg manifest install, then builds
echo the selected existing build tree with --parallel 1 by default. PATH is
echo relative to the repository root and defaults to build\release-mingw-x86_64.
echo The command streams build output live and prints the last 40 log lines as a
echo recap when it finishes. Full output is saved to the selected build tree.
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
    echo [melonprime-build-existing] Could not find CMakeLists.txt above %~dp0
    exit /b 1
)
set "SEARCH_DIR=%PARENT_DIR%"
goto find_repo_root

:repo_root_found

if not exist "%REPO_ROOT_WIN%\%BUILD_DIR%\build.ninja" (
    echo [melonprime-build-existing] Missing %BUILD_DIR%\build.ninja
    echo [melonprime-build-existing] Configure that build tree before using --build-dir.
    exit /b 1
)

set "BUILD_DIR_POSIX=%BUILD_DIR:\=/%"

echo [melonprime-build-existing] Repo: %REPO_ROOT_WIN%
echo [melonprime-build-existing] Build dir: %BUILD_DIR%
echo [melonprime-build-existing] Jobs: %JOBS%
echo [melonprime-build-existing] Skipping CMake configure and vcpkg install.

if "%VERBOSE%"=="1" (
    "%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && build_dir='%BUILD_DIR_POSIX%' && export MAKE=/mingw64/bin/mingw32-make.exe && export PATH='/mingw64/bin:/usr/bin:/c/Program Files/Python312:/c/Program Files/Python312/Scripts:'$repo'/'$build_dir'/vcpkg_installed/x64-mingw-static-release/tools/Qt6/bin:'$repo'/'$build_dir'/vcpkg_installed/x64-mingw-static-release/bin':$PATH && /mingw64/bin/cmake.exe --build $build_dir --parallel %JOBS% --verbose"
) else (
    "%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && build_dir='%BUILD_DIR_POSIX%' && export MAKE=/mingw64/bin/mingw32-make.exe && export PATH='/mingw64/bin:/usr/bin:/c/Program Files/Python312:/c/Program Files/Python312/Scripts:'$repo'/'$build_dir'/vcpkg_installed/x64-mingw-static-release/tools/Qt6/bin:'$repo'/'$build_dir'/vcpkg_installed/x64-mingw-static-release/bin':$PATH && LOG=$build_dir/last-build.log && stdbuf -oL -eL /mingw64/bin/cmake.exe --build $build_dir --parallel %JOBS% 2>&1 | tee $LOG; STATUS=${PIPESTATUS[0]}; echo; echo '[melonprime-build-existing] Last '%TAIL_LINES%' log lines (full log: '$LOG'):'; tail -n %TAIL_LINES% $LOG; exit $STATUS"
)

set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
    echo [melonprime-build-existing] Build failed with exit code %RESULT%.
) else (
    echo [melonprime-build-existing] Build succeeded.
)
exit /b %RESULT%
