@echo off
setlocal EnableExtensions

rem S81-1: dedicated, clean, symbolizable rebuild tree.
rem Unlike build-mingw-vulkan-sapphire-rebuild.bat (which reconfigures the
rem shared build/release-mingw-x86_64 tree in place), this always configures a
rem dedicated build\rebuild-mingw-x86_64 tree from the "rebuild-mingw-x86_64"
rem CMake preset: MELONPRIME_SAPPHIRE_REBUILD=ON,
rem MELONPRIME_SAPPHIRE_REBUILD_SOLID_COLOR=OFF,
rem MELONPRIME_SAPPHIRE_REBUILD_FEATURES=OFF,
rem MELONPRIME_DIAGNOSTIC_SYMBOLS=ON, ENABLE_LTO_RELEASE=OFF. No stale
rem build/release-mingw-x86_64 or release-mingw-x86_64-sapphire-rebuild tree
rem is ever reused.
rem
rem MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN is left OFF here. Two of the three
rem compile errors it exposes were fixed (see
rem docs/vulkan/rebuild/EXACT_PIN_COMPILE_STATUS.md), but the third --
rem 'melonDS::GPU2D' redeclared as different kind of entity (class vs.
rem namespace) between src/GPU2D.h and the vendored
rem SapphireVendor/upstream/melonDS-android-lib/src/GPU2D.h -- is still
rem unfixed. Do not flip this ON until that is resolved; doing so makes this
rem script fail to configure/build at all.
rem
rem First-time configure of a brand-new build dir needs three things a bare
rem "bash.exe -lc" login shell does not provide by default (every other
rem MelonPrime build script only ever reconfigures an already-bootstrapped
rem tree, so this was never hit before):
rem   1. cmake/ConfigureVcpkg.cmake's triplet auto-detect reads the native
rem      %PROCESSOR_ARCHITECTURE% env var, which the login shell does not
rem      import. Pass -DUSE_RECOMMENDED_TRIPLETS=OFF plus explicit
rem      -DVCPKG_TARGET_TRIPLET/-DVCPKG_HOST_TRIPLET instead.
rem   2. vcpkg's manifest installer needs %LOCALAPPDATA%/%APPDATA% or it
rem      fails immediately with "both ... were unreadable". Export both.
rem   3. "cmake --preset=rebuild-mingw-x86_64" itself only reliably passes its
rem      inherited condition (notEquals $env{MINGW_PREFIX} "") when invoked
rem      directly from an MSYS2 bash session; through this script's actual
rem      PowerShell -> cmd.exe -> bash.exe -lc chain it intermittently fails
rem      with "Could not use disabled preset" even with MINGW_PREFIX visibly
rem      set in that same shell. Rather than chase that nesting-dependent
rem      quirk, this script configures with explicit -S/-B and -D flags (the
rem      same pattern every other MelonPrime build script uses) instead of
rem      "--preset=", so preset condition evaluation is never on the critical
rem      path. The CMakePresets.json entry still exists and works from an
rem      interactive MSYS2 MinGW64 shell or an IDE preset picker.

if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help

set "BASH=C:\msys64\usr\bin\bash.exe"
if not exist "%BASH%" (
    echo [melonprime-build-vulkan-sapphire-rebuild-exact] Missing MSYS2 bash: %BASH%
    exit /b 1
)

set "JOBS=1"
set "TAIL_LINES=60"

:parse_args
if "%~1"=="" goto find_repo_root
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
echo [melonprime-build-vulkan-sapphire-rebuild-exact] Unknown argument: %~1
goto usage

:usage
call :print_usage
exit /b 2

:help
call :print_usage
exit /b 0

:print_usage
echo Usage: .claude\skills\build-mingw-vulkan-sapphire-rebuild-exact.bat [--jobs N] [--tail N]
echo.
echo Configures and builds the dedicated "rebuild-mingw-x86_64" preset
echo (build\rebuild-mingw-x86_64): exact-pinned Sapphire GPU2D, no Desktop
echo rebuild heuristics/features, LTO and symbol stripping both disabled so
echo crash RVAs captured from this binary are addr2line-resolvable.
exit /b 0

:find_repo_root
set "SEARCH_DIR=%~dp0"
:find_repo_root_loop
if exist "%SEARCH_DIR%CMakeLists.txt" (
    for %%I in ("%SEARCH_DIR%.") do set "REPO_ROOT_WIN=%%~fI"
    goto repo_root_found
)
for %%I in ("%SEARCH_DIR%..\") do set "PARENT_DIR=%%~fI\"
if /I "%PARENT_DIR%"=="%SEARCH_DIR%" (
    echo [melonprime-build-vulkan-sapphire-rebuild-exact] Could not find CMakeLists.txt above %~dp0
    exit /b 1
)
set "SEARCH_DIR=%PARENT_DIR%"
goto find_repo_root_loop

:repo_root_found
echo [melonprime-build-vulkan-sapphire-rebuild-exact] Repo: %REPO_ROOT_WIN%
echo [melonprime-build-vulkan-sapphire-rebuild-exact] Preset: rebuild-mingw-x86_64 (build\rebuild-mingw-x86_64)
echo [melonprime-build-vulkan-sapphire-rebuild-exact] Jobs: %JOBS%

echo [melonprime-build-vulkan-sapphire-rebuild-exact] Verifying Sapphire generated sources.
python "%REPO_ROOT_WIN%\tools\generate_sapphire_vulkan_sources.py" --verify
if errorlevel 1 exit /b 1

"%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && export LOCALAPPDATA='C:\Users\Admin\AppData\Local' && export APPDATA='C:\Users\Admin\AppData\Roaming' && export PATH=$repo'/build/.mingw-make-shim:/mingw64/bin:/usr/bin:/c/Program Files/Python312:/c/Program Files/Python312/Scripts:'$repo'/build/rebuild-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/tools/Qt6/bin:'$repo'/build/rebuild-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/bin':$PATH && /mingw64/bin/cmake.exe -S . -B build/rebuild-mingw-x86_64 -DUSE_VCPKG=ON -DBUILD_STATIC=ON -DUSE_RECOMMENDED_TRIPLETS=OFF -DVCPKG_TARGET_TRIPLET=x64-mingw-static-release -DVCPKG_HOST_TRIPLET=x64-mingw-static-release -DCMAKE_BUILD_TYPE=Release -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON -DMELONPRIME_ENABLE_VULKAN=ON -DMELONPRIME_FORCE_DISABLE_VULKAN=OFF -DMELONPRIME_SAPPHIRE_REBUILD=ON -DMELONPRIME_SAPPHIRE_REBUILD_SOLID_COLOR=OFF -DMELONPRIME_SAPPHIRE_REBUILD_FEATURES=OFF -DMELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=OFF -DMELONPRIME_DIAGNOSTIC_SYMBOLS=ON -DENABLE_LTO_RELEASE=OFF"
if errorlevel 1 (
    echo [melonprime-build-vulkan-sapphire-rebuild-exact] Configure failed.
    exit /b 1
)

echo [melonprime-build-vulkan-sapphire-rebuild-exact] Generating build identity (fatal on failure; S81 P0).
python "%REPO_ROOT_WIN%\tools\generate_build_identity.py" --repo "%REPO_ROOT_WIN%" --output "%REPO_ROOT_WIN%\build\rebuild-mingw-x86_64\src\MelonPrimeGitBuildIdentity.h"
if errorlevel 1 (
    echo [melonprime-build-vulkan-sapphire-rebuild-exact] ERROR: generate_build_identity.py failed. A build without a known commit/dirty state cannot be treated as a reproducible cold-start artifact.
    exit /b 1
)

"%BASH%" -lc "set -o pipefail; cd '%REPO_ROOT_WIN%' && repo=$(pwd) && export LOCALAPPDATA='C:\Users\Admin\AppData\Local' && export APPDATA='C:\Users\Admin\AppData\Roaming' && export PATH=$repo'/build/.mingw-make-shim:/mingw64/bin:/usr/bin:/c/Program Files/Python312:/c/Program Files/Python312/Scripts:'$repo'/build/rebuild-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/tools/Qt6/bin:'$repo'/build/rebuild-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/bin':$PATH && LOG=build/rebuild-mingw-x86_64/last-build.log && stdbuf -oL -eL /mingw64/bin/cmake.exe --build build/rebuild-mingw-x86_64 --parallel %JOBS% 2>&1 | tee $LOG; STATUS=${PIPESTATUS[0]}; echo; echo '[melonprime-build-vulkan-sapphire-rebuild-exact] Last '%TAIL_LINES%' log lines (full log: '$LOG'):'; tail -n %TAIL_LINES% $LOG; exit $STATUS"
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
    echo [melonprime-build-vulkan-sapphire-rebuild-exact] Build failed with exit code %RESULT%.
    exit /b %RESULT%
)

set "CACHE=%REPO_ROOT_WIN%\build\rebuild-mingw-x86_64\CMakeCache.txt"
if not exist "%CACHE%" (
    echo [melonprime-build-vulkan-sapphire-rebuild-exact] Build succeeded but CMakeCache.txt is missing.
    exit /b 1
)
rem Match KEY.*=VALUE rather than an exact KEY:TYPE=VALUE line: cache_variable
rem type (BOOL vs. INTERNAL vs. UNINITIALIZED) can legitimately differ between
rem a plain option() and a cmake_dependent_option() like ENABLE_LTO_RELEASE
rem without the underlying value being wrong.
for %%V in (
    "MELONPRIME_ENABLE_VULKAN=ON"
    "MELONPRIME_SAPPHIRE_REBUILD=ON"
    "MELONPRIME_SAPPHIRE_REBUILD_SOLID_COLOR=OFF"
    "MELONPRIME_SAPPHIRE_REBUILD_FEATURES=OFF"
    "MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=OFF"
    "MELONPRIME_DIAGNOSTIC_SYMBOLS=ON"
    "ENABLE_LTO_RELEASE=OFF"
) do (
    for /f "tokens=1,2 delims==" %%K in (%%V) do (
        findstr /R /C:"^%%K:.*=%%L$" "%CACHE%" >nul
        if errorlevel 1 (
            echo [melonprime-build-vulkan-sapphire-rebuild-exact] ERROR: CMake cache missing expected value %%V
            exit /b 1
        )
    )
)

echo [melonprime-build-vulkan-sapphire-rebuild-exact] Verified symbolizable rebuild configuration.
exit /b 0
