@echo off
setlocal EnableExtensions

if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help

set "BASE_SCRIPT=%~dp0build-mingw.bat"
if not exist "%BASE_SCRIPT%" (
    echo [melonprime-build-vulkan-debug] Missing base script: %BASE_SCRIPT%
    exit /b 1
)

call :configure_lto_make
if errorlevel 1 exit /b 1

set "SEARCH_DIR=%~dp0"
:find_repo_root
if exist "%SEARCH_DIR%CMakeLists.txt" (
    for %%I in ("%SEARCH_DIR%.") do set "REPO_ROOT_WIN=%%~fI"
    goto repo_root_found
)
for %%I in ("%SEARCH_DIR%..\") do set "PARENT_DIR=%%~fI\"
if /I "%PARENT_DIR%"=="%SEARCH_DIR%" (
    echo [melonprime-build-vulkan-debug] Could not find CMakeLists.txt above %~dp0
    exit /b 1
)
set "SEARCH_DIR=%PARENT_DIR%"
goto find_repo_root

:repo_root_found
set "BASH=C:\msys64\usr\bin\bash.exe"
if not exist "%BASH%" (
    echo [melonprime-build-vulkan-debug] Missing MSYS2 bash: %BASH%
    exit /b 1
)

set "JOBS=1"
set "TAIL_LINES=40"
shift
:parse_args
if "%~1"=="" goto run_build
if /I "%~1"=="--jobs" (
    set "JOBS=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="--tail" (
    set "TAIL_LINES=%~2"
    shift
    shift
    goto parse_args
)
shift
goto parse_args

:run_build
echo [melonprime-build-vulkan-debug] Configuring debug-mingw-x86_64 with Vulkan ON.
echo [melonprime-build-vulkan-debug] Debug flags: -g3 -O0 -fno-omit-frame-pointer
"%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && export PATH=$repo'/build/.mingw-make-shim:/mingw64/bin:/usr/bin:$PATH' && /mingw64/bin/cmake.exe -S . -B build/debug-mingw-x86_64 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS_DEBUG='-g3 -O0 -fno-omit-frame-pointer' -DCMAKE_C_FLAGS_DEBUG='-g3 -O0 -fno-omit-frame-pointer' -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON -DMELONPRIME_ENABLE_VULKAN=ON -DMELONPRIME_FORCE_DISABLE_VULKAN=OFF -DUSE_VCPKG=ON -DBUILD_STATIC=ON && LOG=build/debug-mingw-x86_64/last-build.log && stdbuf -oL -eL /mingw64/bin/cmake.exe --build build/debug-mingw-x86_64 --parallel %JOBS% 2>&1 | tee $LOG; STATUS=${PIPESTATUS[0]}; echo; echo '[melonprime-build-vulkan-debug] Last '%TAIL_LINES%' log lines (full log: '$LOG'):'; tail -n %TAIL_LINES% $LOG; exit $STATUS"
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
    echo [melonprime-build-vulkan-debug] Build failed with exit code %RESULT%.
) else (
    echo [melonprime-build-vulkan-debug] Build succeeded.
)
exit /b %RESULT%

:configure_lto_make
set "LTO_MAKE=C:\msys64\mingw64\bin\mingw32-make.exe"
if not exist "%LTO_MAKE%" set "LTO_MAKE=C:\msys64\mingw64\bin\make.exe"
if not exist "%LTO_MAKE%" (
    echo [melonprime-build-vulkan-debug] ERROR: Native MinGW make was not found.
    exit /b 1
)
set "MAKE=%LTO_MAKE%"
exit /b 0

:help
echo Usage: .claude\skills\build-mingw-vulkan-debug.bat [--jobs N] [--tail N]
echo.
echo Configure and build debug-mingw-x86_64 with Vulkan enabled and frame-pointer
echo symbols for Windows minidump / gdb stack capture.
exit /b 0
