@echo off
setlocal EnableExtensions

rem MinGW Debug build for the Vulkan Validation Layer workflow.
rem
rem CMAKE_BUILD_TYPE=Debug is what defines MELONDS_VULKAN_ENABLE_VALIDATION, so
rem the ordinary release build script cannot be used for validation no matter
rem how it is configured. This one exists so that distinction is a preset and a
rem script rather than something to remember.
rem
rem Never take latency numbers from this build: the validation layer and the
rem debug configuration both change timing. Measurement uses the release build.

set "BASH=C:\msys64\usr\bin\bash.exe"
if not exist "%BASH%" (
    echo [melonprime-validation] Missing MSYS2 bash: %BASH%
    echo [melonprime-validation] Install MSYS2 at C:\msys64 or update this batch file.
    exit /b 1
)

set "JOBS=1"
set "TAIL_LINES=40"

:parse_args
if "%~1"=="" goto run_build
if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
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

echo [melonprime-validation] Unknown argument: %~1
goto usage

:help
call :print_usage
exit /b 0

:usage
call :print_usage
exit /b 2

:print_usage
echo Usage: tools\build\windows\build-mingw-validation.bat [--jobs N] [--tail N]
echo.
echo Configures and builds debug-mingw-x86_64 with
echo MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON. The Debug configuration is what
echo enables MELONDS_VULKAN_ENABLE_VALIDATION, so the resulting binary looks for
echo VK_LAYER_KHRONOS_validation at startup and logs whether it was enabled.
echo.
echo This build is for correctness only. Latency and frame-pacing numbers must
echo come from tools\build\windows\build-mingw.bat instead.
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
    echo [melonprime-validation] Could not find CMakeLists.txt above %~dp0
    exit /b 1
)
set "SEARCH_DIR=%PARENT_DIR%"
goto find_repo_root

:repo_root_found

if not exist "%REPO_ROOT_WIN%\build\release-mingw-x86_64\vcpkg_installed" (
    echo [melonprime-validation] Missing build\release-mingw-x86_64\vcpkg_installed.
    echo [melonprime-validation] Run tools\build\windows\build-mingw.bat first: the
    echo [melonprime-validation] Debug tree reuses that dependency set instead of
    echo [melonprime-validation] building a second, identical one.
    exit /b 1
)

echo [melonprime-validation] Repo: %REPO_ROOT_WIN%
echo [melonprime-validation] Preset: debug-mingw-x86_64
echo [melonprime-validation] Jobs: %JOBS%

rem MSYSTEM must be set before bash starts so the MSYS2 profile exports
rem MINGW_PREFIX. Without it, ConfigureVcpkg.cmake sees a non-MinGW Windows
rem host, selects the MSVC triplet and tries to build every dependency again.
set "MSYSTEM=MINGW64"
set "MINGW_PREFIX=/mingw64"

"%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && export MSYSTEM=MINGW64 && export MINGW_PREFIX=/mingw64 && export PATH='/mingw64/bin:/usr/bin:/c/Program Files/Python312:/c/Program Files/Python312/Scripts:'$repo'/build/release-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/tools/Qt6/bin:'$repo'/build/release-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/bin':$PATH && /mingw64/bin/cmake.exe --preset debug-mingw-x86_64 -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON -U pkgcfg_lib_Faad_m -U pkgcfg_lib_SDL2_m && LOG=build/debug-mingw-x86_64/last-build.log && stdbuf -oL -eL /mingw64/bin/cmake.exe --build --preset=debug-mingw-x86_64 --parallel %JOBS% 2>&1 | tee $LOG; STATUS=${PIPESTATUS[0]}; echo; echo '[melonprime-validation] Last '%TAIL_LINES%' log lines (full log: '$LOG'):'; tail -n %TAIL_LINES% $LOG; exit $STATUS"

set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
    echo [melonprime-validation] Build failed with exit code %RESULT%.
) else (
    echo [melonprime-validation] Build succeeded.
    echo [melonprime-validation] Run the binary and confirm this log line before
    echo [melonprime-validation] treating any validation result as meaningful:
    echo [melonprime-validation]     [Vulkan] validation layer enabled
)
exit /b %RESULT%
