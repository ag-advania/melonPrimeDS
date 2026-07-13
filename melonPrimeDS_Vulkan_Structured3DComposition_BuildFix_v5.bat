@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "DO_BUILD=1"
set "REPO_ROOT="

if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
if /I "%~1"=="--no-build" set "DO_BUILD=0"

if exist "%~dp0CMakeLists.txt" if exist "%~dp0src\GPU_Vulkan.cpp" set "REPO_ROOT=%~dp0"
if not defined REPO_ROOT if exist "%CD%\CMakeLists.txt" if exist "%CD%\src\GPU_Vulkan.cpp" set "REPO_ROOT=%CD%"
if not defined REPO_ROOT if exist "C:\Users\Admin\Documents\git\melonPrimeDS\CMakeLists.txt" set "REPO_ROOT=C:\Users\Admin\Documents\git\melonPrimeDS"

if not defined REPO_ROOT (
    echo [structured-3d-fix] ERROR: melonPrimeDS repository was not found.
    echo [structured-3d-fix] Place this BAT in the repository root.
    pause
    exit /b 2
)

for %%I in ("%REPO_ROOT%\.") do set "REPO_ROOT=%%~fI"
if "%REPO_ROOT:~-1%"=="\" set "REPO_ROOT=%REPO_ROOT:~0,-1%"

echo ============================================================
echo  MelonPrimeDS Vulkan - Structured 3D Build Fix v5
echo ============================================================
echo Repository: %REPO_ROOT%
echo.
echo Repairs stale Explicit-Ownership identifiers left in the
echo v4 Structured 3D Composition source patch, then rebuilds.
echo.

set "SELF_BAT=%~f0"
set "PATCH_PS1=%TEMP%\melonprime_structured_3d_buildfix_%RANDOM%_%RANDOM%.ps1"

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command ^
  "$raw=[IO.File]::ReadAllText($env:SELF_BAT); $marker='__POWERSHELL_PAYLOAD__'; $index=$raw.LastIndexOf($marker,[StringComparison]::Ordinal); if($index -lt 0){Write-Error 'Embedded payload marker is missing.';exit 3}; $b64=$raw.Substring($index+$marker.Length).Trim(); try{$bytes=[Convert]::FromBase64String($b64)}catch{Write-Error ('Embedded payload is invalid: '+$_.Exception.Message);exit 3}; [IO.File]::WriteAllBytes($env:PATCH_PS1,$bytes)"
if errorlevel 1 (
    echo [structured-3d-fix] ERROR: Could not extract repair payload.
    pause
    exit /b 3
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass ^
  -File "%PATCH_PS1%" -RepoRoot "%REPO_ROOT%"
set "PATCH_RESULT=%ERRORLEVEL%"
del /q "%PATCH_PS1%" >nul 2>&1

if not "%PATCH_RESULT%"=="0" (
    echo.
    echo [structured-3d-fix] Repair failed. Original files were restored.
    pause
    exit /b %PATCH_RESULT%
)

where git.exe >nul 2>&1
if not errorlevel 1 (
    pushd "%REPO_ROOT%"
    git diff --check
    if errorlevel 1 (
        popd
        echo [structured-3d-fix] ERROR: git diff --check failed.
        pause
        exit /b 4
    )
    echo.
    git diff --stat -- src/GPU_Soft.h src/GPU_Soft.cpp src/GPU2D_Soft.h src/GPU2D_Soft.cpp src/GPU_Vulkan.h src/GPU_Vulkan.cpp
    popd
)

if "%DO_BUILD%"=="0" (
    echo.
    echo [structured-3d-fix] Repair applied. Build was skipped.
    echo [structured-3d-fix] Run .claude\skills\build-mingw-vulkan.bat next.
    pause
    exit /b 0
)

set "BUILD_SCRIPT=%REPO_ROOT%\.claude\skills\build-mingw-vulkan.bat"
if not exist "%BUILD_SCRIPT%" (
    echo [structured-3d-fix] ERROR: Build script is missing:
    echo   %BUILD_SCRIPT%
    echo [structured-3d-fix] Source repair remains applied.
    pause
    exit /b 5
)

echo.
echo [structured-3d-fix] Rebuilding Vulkan-enabled Windows target...
call "%BUILD_SCRIPT%"
set "BUILD_RESULT=%ERRORLEVEL%"

if not "%BUILD_RESULT%"=="0" (
    echo.
    echo [structured-3d-fix] ERROR: Vulkan build failed with exit code %BUILD_RESULT%.
    echo [structured-3d-fix] Source repair remains applied.
    pause
    exit /b %BUILD_RESULT%
)

echo.
echo ============================================================
echo  SUCCESS
echo ============================================================
echo Structured 3D Composition compiled successfully.
echo The stale Output3DOwnership reference has been removed.
echo.
pause
exit /b 0

:help
echo Place this BAT in the melonPrimeDS repository root.
echo.
echo Usage:
echo   %~nx0
echo   %~nx0 --no-build
echo.
echo This repair:
echo   - requires Structured 3D Composition v4 to be applied
echo   - backs up six affected source files
echo   - replaces stale ownership API identifiers
echo   - verifies all three Output3DComposition clear paths
echo   - runs git diff --check and the Windows Vulkan build
exit /b 0

__POWERSHELL_PAYLOAD__
cGFyYW0oCiAgICBbUGFyYW1ldGVyKE1hbmRhdG9yeSA9ICR0cnVlKV0KICAgIFtzdHJpbmddJFJl
cG9Sb290CikKCiRFcnJvckFjdGlvblByZWZlcmVuY2UgPSAnU3RvcCcKU2V0LVN0cmljdE1vZGUg
LVZlcnNpb24gTGF0ZXN0CgokUmVwb1Jvb3QgPSBbU3lzdGVtLklPLlBhdGhdOjpHZXRGdWxsUGF0
aCgkUmVwb1Jvb3QpCiR1dGY4Tm9Cb20gPSBOZXctT2JqZWN0IFN5c3RlbS5UZXh0LlVURjhFbmNv
ZGluZygkZmFsc2UpCgokcmVsYXRpdmVGaWxlcyA9IEAoCiAgICAnc3JjL0dQVV9Tb2Z0LmgnLAog
ICAgJ3NyYy9HUFVfU29mdC5jcHAnLAogICAgJ3NyYy9HUFUyRF9Tb2Z0LmgnLAogICAgJ3NyYy9H
UFUyRF9Tb2Z0LmNwcCcsCiAgICAnc3JjL0dQVV9WdWxrYW4uaCcsCiAgICAnc3JjL0dQVV9WdWxr
YW4uY3BwJwopCgpmb3JlYWNoICgkcmVsYXRpdmUgaW4gJHJlbGF0aXZlRmlsZXMpIHsKICAgICRw
YXRoID0gSm9pbi1QYXRoICRSZXBvUm9vdCAkcmVsYXRpdmUKICAgIGlmICgtbm90IChUZXN0LVBh
dGggLUxpdGVyYWxQYXRoICRwYXRoIC1QYXRoVHlwZSBMZWFmKSkgewogICAgICAgIHRocm93ICJN
aXNzaW5nIHJlcXVpcmVkIGZpbGU6ICRyZWxhdGl2ZSIKICAgIH0KfQoKJHNvZnRIZWFkZXIgPSBb
U3lzdGVtLklPLkZpbGVdOjpSZWFkQWxsVGV4dCgKICAgIChKb2luLVBhdGggJFJlcG9Sb290ICdz
cmMvR1BVX1NvZnQuaCcpKQppZiAoLW5vdCAkc29mdEhlYWRlci5Db250YWlucygnTUVMT05QUklN
RV9WVUxLQU5fU1RSVUNUVVJFRF8zRF9DT01QT1NJVElPTl9WMScpKSB7CiAgICB0aHJvdyAnU3Ry
dWN0dXJlZCAzRCBDb21wb3NpdGlvbiB2NCBpcyBub3QgYXBwbGllZC4gUnVuIHRoZSB2NCBwYXRj
aCBmaXJzdC4nCn0KaWYgKC1ub3QgJHNvZnRIZWFkZXIuQ29udGFpbnMoJ2FsaWduYXMoOCkgdTMy
IE91dHB1dDNEQ29tcG9zaXRpb25bMjU2XTsnKSkgewogICAgdGhyb3cgJ091dHB1dDNEQ29tcG9z
aXRpb24gZGVjbGFyYXRpb24gaXMgbWlzc2luZyBmcm9tIHNyYy9HUFVfU29mdC5oLicKfQoKJHN0
YW1wID0gR2V0LURhdGUgLUZvcm1hdCAneXl5eU1NZGRfSEhtbXNzX2ZmZicKJGJhY2t1cFJvb3Qg
PSBKb2luLVBhdGggJFJlcG9Sb290ICgKICAgICIubWVsb25wcmltZV92dWxrYW5fc3RydWN0dXJl
ZF8zZF9idWlsZGZpeF9iYWNrdXBcYXBwbHlfJHN0YW1wIikKJG1vZGlmaWVkID0gTmV3LU9iamVj
dCBTeXN0ZW0uQ29sbGVjdGlvbnMuR2VuZXJpYy5MaXN0W3N0cmluZ10KJHRvdGFsUmVwbGFjZW1l
bnRzID0gMAoKJHJlcGxhY2VtZW50TWFwID0gW29yZGVyZWRdQHsKICAgICdPdXRwdXQzRE93bmVy
c2hpcCcgPSAnT3V0cHV0M0RDb21wb3NpdGlvbicKICAgICdPbkNvbXBvc2VkM0RPd25lcnNoaXBM
aW5lJyA9ICdPbkNvbXBvc2VkM0RDb21wb3NpdGlvbkxpbmUnCiAgICAnQ29weU5hdGl2ZTNET3du
ZXJzaGlwRm9yUHJlc2VudGVyJyA9ICdDb3B5TmF0aXZlM0RDb21wb3NpdGlvbkZvclByZXNlbnRl
cicKICAgICdOYXRpdmUzRFZpc2libGUnID0gJ05hdGl2ZTNEQ29tcG9zaXRpb24nCn0KCmZ1bmN0
aW9uIE5vcm1hbGl6ZS1MZihbc3RyaW5nXSRUZXh0KSB7CiAgICByZXR1cm4gJFRleHQuUmVwbGFj
ZSgiYHJgbiIsICJgbiIpLlJlcGxhY2UoImByIiwgImBuIikKfQoKZnVuY3Rpb24gV3JpdGUtUHJl
c2VydmVkVGV4dCB7CiAgICBwYXJhbSgKICAgICAgICBbUGFyYW1ldGVyKE1hbmRhdG9yeSA9ICR0
cnVlKV1bc3RyaW5nXSRQYXRoLAogICAgICAgIFtQYXJhbWV0ZXIoTWFuZGF0b3J5ID0gJHRydWUp
XVtzdHJpbmddJFRleHQsCiAgICAgICAgW1BhcmFtZXRlcihNYW5kYXRvcnkgPSAkdHJ1ZSldW2Jv
b2xdJFVzZUNyTGYKICAgICkKICAgICRvdXRwdXQgPSBpZiAoJFVzZUNyTGYpIHsKICAgICAgICAo
Tm9ybWFsaXplLUxmICRUZXh0KS5SZXBsYWNlKCJgbiIsICJgcmBuIikKICAgIH0KICAgIGVsc2Ug
ewogICAgICAgIE5vcm1hbGl6ZS1MZiAkVGV4dAogICAgfQogICAgW1N5c3RlbS5JTy5GaWxlXTo6
V3JpdGVBbGxUZXh0KCRQYXRoLCAkb3V0cHV0LCAkdXRmOE5vQm9tKQp9CgpOZXctSXRlbSAtSXRl
bVR5cGUgRGlyZWN0b3J5IC1QYXRoICRiYWNrdXBSb290IC1Gb3JjZSB8IE91dC1OdWxsCmZvcmVh
Y2ggKCRyZWxhdGl2ZSBpbiAkcmVsYXRpdmVGaWxlcykgewogICAgJHNvdXJjZSA9IEpvaW4tUGF0
aCAkUmVwb1Jvb3QgJHJlbGF0aXZlCiAgICAkZGVzdGluYXRpb24gPSBKb2luLVBhdGggJGJhY2t1
cFJvb3QgJHJlbGF0aXZlCiAgICBOZXctSXRlbSAtSXRlbVR5cGUgRGlyZWN0b3J5IGAKICAgICAg
ICAtUGF0aCAoU3BsaXQtUGF0aCAtUGFyZW50ICRkZXN0aW5hdGlvbikgLUZvcmNlIHwgT3V0LU51
bGwKICAgIENvcHktSXRlbSAtTGl0ZXJhbFBhdGggJHNvdXJjZSAtRGVzdGluYXRpb24gJGRlc3Rp
bmF0aW9uIC1Gb3JjZQp9Cgp0cnkgewogICAgZm9yZWFjaCAoJHJlbGF0aXZlIGluICRyZWxhdGl2
ZUZpbGVzKSB7CiAgICAgICAgJHBhdGggPSBKb2luLVBhdGggJFJlcG9Sb290ICRyZWxhdGl2ZQog
ICAgICAgICRyYXcgPSBbU3lzdGVtLklPLkZpbGVdOjpSZWFkQWxsVGV4dCgkcGF0aCkKICAgICAg
ICAkdXNlQ3JMZiA9ICRyYXcuQ29udGFpbnMoImByYG4iKQogICAgICAgICR1cGRhdGVkID0gJHJh
dwogICAgICAgICRmaWxlUmVwbGFjZW1lbnRzID0gMAoKICAgICAgICBmb3JlYWNoICgkZW50cnkg
aW4gJHJlcGxhY2VtZW50TWFwLkdldEVudW1lcmF0b3IoKSkgewogICAgICAgICAgICAkY291bnQg
PSBbcmVnZXhdOjpNYXRjaGVzKAogICAgICAgICAgICAgICAgJHVwZGF0ZWQsCiAgICAgICAgICAg
ICAgICBbcmVnZXhdOjpFc2NhcGUoW3N0cmluZ10kZW50cnkuS2V5KSkuQ291bnQKICAgICAgICAg
ICAgaWYgKCRjb3VudCAtZ3QgMCkgewogICAgICAgICAgICAgICAgJHVwZGF0ZWQgPSAkdXBkYXRl
ZC5SZXBsYWNlKAogICAgICAgICAgICAgICAgICAgIFtzdHJpbmddJGVudHJ5LktleSwKICAgICAg
ICAgICAgICAgICAgICBbc3RyaW5nXSRlbnRyeS5WYWx1ZSkKICAgICAgICAgICAgICAgICRmaWxl
UmVwbGFjZW1lbnRzICs9ICRjb3VudAogICAgICAgICAgICAgICAgJHRvdGFsUmVwbGFjZW1lbnRz
ICs9ICRjb3VudAogICAgICAgICAgICAgICAgV3JpdGUtSG9zdCAoCiAgICAgICAgICAgICAgICAg
ICAgIltzdHJ1Y3R1cmVkLTNkLWZpeF0gezB9OiB7MX0gLT4gezJ9ICh7M30pIiAtZgogICAgICAg
ICAgICAgICAgICAgICRyZWxhdGl2ZSwgJGVudHJ5LktleSwgJGVudHJ5LlZhbHVlLCAkY291bnQp
CiAgICAgICAgICAgIH0KICAgICAgICB9CgogICAgICAgIGlmICgkZmlsZVJlcGxhY2VtZW50cyAt
Z3QgMCkgewogICAgICAgICAgICBXcml0ZS1QcmVzZXJ2ZWRUZXh0IGAKICAgICAgICAgICAgICAg
IC1QYXRoICRwYXRoIGAKICAgICAgICAgICAgICAgIC1UZXh0ICR1cGRhdGVkIGAKICAgICAgICAg
ICAgICAgIC1Vc2VDckxmICR1c2VDckxmCiAgICAgICAgICAgICRtb2RpZmllZC5BZGQoJHJlbGF0
aXZlKQogICAgICAgIH0KICAgIH0KCiAgICBmb3JlYWNoICgkcmVsYXRpdmUgaW4gJHJlbGF0aXZl
RmlsZXMpIHsKICAgICAgICAkdGV4dCA9IFtTeXN0ZW0uSU8uRmlsZV06OlJlYWRBbGxUZXh0KAog
ICAgICAgICAgICAoSm9pbi1QYXRoICRSZXBvUm9vdCAkcmVsYXRpdmUpKQogICAgICAgIGZvcmVh
Y2ggKCRvbGROYW1lIGluICRyZXBsYWNlbWVudE1hcC5LZXlzKSB7CiAgICAgICAgICAgIGlmICgk
dGV4dC5Db250YWlucyhbc3RyaW5nXSRvbGROYW1lKSkgewogICAgICAgICAgICAgICAgdGhyb3cg
IlN0YWxlIGlkZW50aWZpZXIgcmVtYWlucyBpbiAke3JlbGF0aXZlfTogJG9sZE5hbWUiCiAgICAg
ICAgICAgIH0KICAgICAgICB9CiAgICB9CgogICAgJHNvZnRDcHBQYXRoID0gSm9pbi1QYXRoICRS
ZXBvUm9vdCAnc3JjL0dQVV9Tb2Z0LmNwcCcKICAgICRzb2Z0Q3BwID0gW1N5c3RlbS5JTy5GaWxl
XTo6UmVhZEFsbFRleHQoJHNvZnRDcHBQYXRoKQogICAgJGNvbXBvc2l0aW9uQ2xlYXJDb3VudCA9
IFtyZWdleF06Ok1hdGNoZXMoCiAgICAgICAgJHNvZnRDcHAsCiAgICAgICAgW3JlZ2V4XTo6RXNj
YXBlKAogICAgICAgICAgICAnbWVtc2V0KE91dHB1dDNEQ29tcG9zaXRpb24sIDAsIHNpemVvZihP
dXRwdXQzRENvbXBvc2l0aW9uKSk7JykKICAgICkuQ291bnQKICAgIGlmICgkY29tcG9zaXRpb25D
bGVhckNvdW50IC1sdCAzKSB7CiAgICAgICAgdGhyb3cgKAogICAgICAgICAgICAnR1BVX1NvZnQu
Y3BwIG11c3QgY2xlYXIgT3V0cHV0M0RDb21wb3NpdGlvbiBpbiBSZXNldCwgU3RvcCwgJyArCiAg
ICAgICAgICAgICJhbmQgcGVyLXNjYW5saW5lIHBhdGhzLiBhY3R1YWw9JGNvbXBvc2l0aW9uQ2xl
YXJDb3VudCIpCiAgICB9CiAgICBpZiAoLW5vdCAkc29mdENwcC5Db250YWlucygKICAgICAgICAn
T25Db21wb3NlZDNEQ29tcG9zaXRpb25MaW5lKGxpbmUsIE91dHB1dDNEQ29tcG9zaXRpb24pOycp
KSB7CiAgICAgICAgdGhyb3cgJ1N0cnVjdHVyZWQgY29tcG9zaXRpb24gY2FsbGJhY2sgY2FsbCBp
cyBtaXNzaW5nIGZyb20gR1BVX1NvZnQuY3BwLicKICAgIH0KCiAgICAkbWFuaWZlc3QgPSBbb3Jk
ZXJlZF1AewogICAgICAgIFBhdGNoID0gJ01lbG9uUHJpbWVEUyBWdWxrYW4gU3RydWN0dXJlZCAz
RCBDb21wb3NpdGlvbiBidWlsZCBmaXggdjUnCiAgICAgICAgQXBwbGllZEF0ID0gKEdldC1EYXRl
KS5Ub1N0cmluZygnbycpCiAgICAgICAgUmVwb3NpdG9yeSA9ICRSZXBvUm9vdAogICAgICAgIEJh
Y2t1cCA9ICRiYWNrdXBSb290CiAgICAgICAgUmVwbGFjZW1lbnRDb3VudCA9ICR0b3RhbFJlcGxh
Y2VtZW50cwogICAgICAgIEZpbGVzID0gJG1vZGlmaWVkCiAgICAgICAgVmFsaWRhdGlvbiA9IFtv
cmRlcmVkXUB7CiAgICAgICAgICAgIFN0cnVjdHVyZWRNYXJrZXIgPSAkdHJ1ZQogICAgICAgICAg
ICBPdXRwdXREZWNsYXJhdGlvbiA9ICR0cnVlCiAgICAgICAgICAgIENsZWFyUGF0aENvdW50ID0g
JGNvbXBvc2l0aW9uQ2xlYXJDb3VudAogICAgICAgICAgICBTdGFsZUlkZW50aWZpZXJzUmVtYWlu
aW5nID0gMAogICAgICAgIH0KICAgIH0KICAgIFtTeXN0ZW0uSU8uRmlsZV06OldyaXRlQWxsVGV4
dCgKICAgICAgICAoSm9pbi1QYXRoICRiYWNrdXBSb290ICdtYW5pZmVzdC5qc29uJyksCiAgICAg
ICAgKCRtYW5pZmVzdCB8IENvbnZlcnRUby1Kc29uIC1EZXB0aCA2KSwKICAgICAgICAkdXRmOE5v
Qm9tKQoKICAgIGlmICgkdG90YWxSZXBsYWNlbWVudHMgLWVxIDApIHsKICAgICAgICBXcml0ZS1I
b3N0ICdbc3RydWN0dXJlZC0zZC1maXhdIE5vIHN0YWxlIGlkZW50aWZpZXJzIHdlcmUgZm91bmQu
JwogICAgICAgIFdyaXRlLUhvc3QgJ1tzdHJ1Y3R1cmVkLTNkLWZpeF0gU291cmNlIGFscmVhZHkg
Y29udGFpbnMgdGhlIGJ1aWxkIGZpeC4nCiAgICB9CiAgICBlbHNlIHsKICAgICAgICBXcml0ZS1I
b3N0ICgKICAgICAgICAgICAgIltzdHJ1Y3R1cmVkLTNkLWZpeF0gQXBwbGllZCAkdG90YWxSZXBs
YWNlbWVudHMgcmVwbGFjZW1lbnQocykuIikKICAgIH0KICAgIFdyaXRlLUhvc3QgIltzdHJ1Y3R1
cmVkLTNkLWZpeF0gQmFja3VwOiAkYmFja3VwUm9vdCIKfQpjYXRjaCB7CiAgICBXcml0ZS1Ib3N0
ICJbc3RydWN0dXJlZC0zZC1maXhdIEVSUk9SOiAkKCRfLkV4Y2VwdGlvbi5NZXNzYWdlKSIgYAog
ICAgICAgIC1Gb3JlZ3JvdW5kQ29sb3IgUmVkCiAgICBXcml0ZS1Ib3N0ICdbc3RydWN0dXJlZC0z
ZC1maXhdIFJlc3RvcmluZyBvcmlnaW5hbCBmaWxlcy4uLicKICAgIGZvcmVhY2ggKCRyZWxhdGl2
ZSBpbiAkcmVsYXRpdmVGaWxlcykgewogICAgICAgICRzb3VyY2UgPSBKb2luLVBhdGggJGJhY2t1
cFJvb3QgJHJlbGF0aXZlCiAgICAgICAgJGRlc3RpbmF0aW9uID0gSm9pbi1QYXRoICRSZXBvUm9v
dCAkcmVsYXRpdmUKICAgICAgICBpZiAoVGVzdC1QYXRoIC1MaXRlcmFsUGF0aCAkc291cmNlIC1Q
YXRoVHlwZSBMZWFmKSB7CiAgICAgICAgICAgIENvcHktSXRlbSAtTGl0ZXJhbFBhdGggJHNvdXJj
ZSBgCiAgICAgICAgICAgICAgICAtRGVzdGluYXRpb24gJGRlc3RpbmF0aW9uIC1Gb3JjZQogICAg
ICAgIH0KICAgIH0KICAgIHRocm93Cn0K
