@echo off
setlocal EnableExtensions

if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help

set "BASE_SCRIPT=%~dp0build-mingw.bat"
if not exist "%BASE_SCRIPT%" (
    echo [melonprime-build-vulkan] Missing base script: %BASE_SCRIPT%
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
    echo [melonprime-build-vulkan] Could not find CMakeLists.txt above %~dp0
    exit /b 1
)
set "SEARCH_DIR=%PARENT_DIR%"
goto find_repo_root

:repo_root_found
if not exist "%REPO_ROOT_WIN%\src\frontend\qt_sdl\VideoSettingsDialog.cpp" (
    echo [melonprime-build-vulkan] Missing VideoSettingsDialog.cpp
    exit /b 1
)
findstr /C:"MELONPRIME_VULKAN_PHASE12_DYNAMIC_LAYOUT_V1" "%REPO_ROOT_WIN%\src\frontend\qt_sdl\VideoSettingsDialog.cpp" >nul
if errorlevel 1 (
    echo [melonprime-build-vulkan] Phase 12 UI patch is not applied.
    echo [melonprime-build-vulkan] Apply Phase 12 Completion before building.
    exit /b 1
)

echo [melonprime-build-vulkan] Forcing MELONPRIME_ENABLE_VULKAN=ON.
echo [melonprime-build-vulkan] Developer features remain ON through build-mingw.bat.
call "%BASE_SCRIPT%" %* --vulkan
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" exit /b %RESULT%

set "CACHE=%REPO_ROOT_WIN%\build\release-mingw-x86_64\CMakeCache.txt"
if not exist "%CACHE%" (
    echo [melonprime-build-vulkan] Build succeeded but CMakeCache.txt is missing.
    exit /b 1
)
findstr /R /X /C:"MELONPRIME_ENABLE_VULKAN:BOOL=ON" "%CACHE%" >nul
if errorlevel 1 (
    echo [melonprime-build-vulkan] ERROR: CMake cache did not retain MELONPRIME_ENABLE_VULKAN=ON.
    exit /b 1
)
findstr /R /X /C:"MELONPRIME_FORCE_DISABLE_VULKAN:BOOL=OFF" "%CACHE%" >nul
if errorlevel 1 (
    echo [melonprime-build-vulkan] ERROR: MELONPRIME_FORCE_DISABLE_VULKAN is not OFF.
    exit /b 1
)
findstr /R /X /C:"MELONPRIME_ENABLE_DEVELOPER_FEATURES:BOOL=ON" "%CACHE%" >nul
if errorlevel 1 (
    echo [melonprime-build-vulkan] ERROR: Developer features are not enabled.
    exit /b 1
)

echo [melonprime-build-vulkan] Verified Vulkan-enabled Phase 12 build configuration.
exit /b 0

:help
echo Usage: .claude\skills\build-mingw-vulkan.bat [--verbose] [--jobs N] [--tail N]
echo.
echo Runs build-mingw.bat with --vulkan forced, then verifies the CMake cache has:
echo   MELONPRIME_ENABLE_VULKAN=ON
echo   MELONPRIME_FORCE_DISABLE_VULKAN=OFF
echo   MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON
echo.
echo This is the full configure-and-build script. Run it once after enabling Vulkan
echo or after using a build tree that was previously configured with Vulkan OFF.
exit /b 0
