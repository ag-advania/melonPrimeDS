@echo off
setlocal EnableExtensions

if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help

set "BASE_SCRIPT=%~dp0build-mingw.bat"
if not exist "%BASE_SCRIPT%" (
    echo [melonprime-build-vulkan] Missing base script: %BASE_SCRIPT%
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
    echo [melonprime-build-vulkan] Could not find CMakeLists.txt above %~dp0
    exit /b 1
)
set "SEARCH_DIR=%PARENT_DIR%"
goto find_repo_root

:repo_root_found
if not exist "%REPO_ROOT_WIN%\src\frontend\qt_sdl\MelonPrimeVulkanSettings.h" (
    echo [melonprime-build-vulkan] Missing R26 canonical Vulkan settings header.
    exit /b 1
)
findstr /C:"MELONPRIME_VULKAN_R26_CANONICAL_SETTINGS_V1" "%REPO_ROOT_WIN%\src\frontend\qt_sdl\MelonPrimeVulkanSettings.h" >nul
if errorlevel 1 (
    echo [melonprime-build-vulkan] R26 canonical Vulkan settings marker is missing.
    exit /b 1
)

if not exist "%TEMP%" mkdir "%TEMP%" >nul 2>&1

echo [melonprime-build-vulkan] Forcing MELONPRIME_ENABLE_VULKAN=ON.
echo [melonprime-build-vulkan] GCC LTO make: %MAKE%
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

echo [melonprime-build-vulkan] Verified Vulkan-enabled R26 build configuration.
exit /b 0

:configure_lto_make
set "LTO_MAKE=C:\msys64\mingw64\bin\mingw32-make.exe"
if not exist "%LTO_MAKE%" set "LTO_MAKE=C:\msys64\mingw64\bin\make.exe"
if not exist "%LTO_MAKE%" (
    echo [melonprime-build-vulkan] ERROR: Native MinGW make was not found.
    echo [melonprime-build-vulkan] GCC -flto=auto must not use C:\msys64\usr\bin\make.exe.
    echo [melonprime-build-vulkan] Install it from an MSYS2 MinGW64 shell:
    echo [melonprime-build-vulkan]   pacman -S --needed mingw-w64-x86_64-make
    exit /b 1
)
set "MAKE=%LTO_MAKE%"
set "MAKEFLAGS="
set "MFLAGS="
set "GNUMAKEFLAGS="
set "TMP=C:\msys64\tmp"
set "TEMP=C:\msys64\tmp"
exit /b 0

:help
echo Usage: .claude\skills\build-mingw-vulkan.bat [--verbose] [--jobs N] [--tail N]
echo.
echo Runs build-mingw.bat with Vulkan forced ON. Before invoking GCC LTO, it
echo forces MAKE to the native MinGW mingw32-make.exe instead of the MSYS
echo C:\msys64\usr\bin\make.exe that cannot safely serve native GCC LTO.
echo.
echo Required cache values:
echo   MELONPRIME_ENABLE_VULKAN=ON
echo   MELONPRIME_FORCE_DISABLE_VULKAN=OFF
echo   MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON
exit /b 0
