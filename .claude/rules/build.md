# Build

## Build Notes
- Main feature flags in `src/frontend/qt_sdl/CMakeLists.txt`:
  - `MELONPRIME_DS`
  - `MELONPRIME_CUSTOM_HUD`
- `MelonPrimeHudRender.cpp` and `InputConfig/MelonPrimeInputConfig.cpp` are explicitly built as part of the frontend
- `MelonPrimeHudConfigOnScreen.cpp` is a unity-build include (pulled in by `MelonPrimeHudRender.cpp`); do not add it to `CMakeLists.txt`
- The project has been built on Windows and via MinGW cross-compilation from WSL
- `vcpkg/` is used for dependencies in this repo setup

## Windows Build Command
Windows-only AI build command. Do not rebuild, bootstrap, or reinstall `vcpkg/` unless the user explicitly asks for it.

Use MSYS bash and the existing CMake preset:

```powershell
& 'C:\msys64\usr\bin\bash.exe' -lc "export PATH='/c/Program Files/Python312:/c/Program Files/Python312/Scripts:`$PATH'; cd /c/Users/Admin/Documents/git/melonPrimeDS && /mingw64/bin/cmake.exe --build --preset=release-mingw-x86_64 --parallel 1 --verbose 2>&1"
```

For short output, use:

```powershell
& 'C:\msys64\usr\bin\bash.exe' -lc "cd /c/Users/Admin/Documents/git/melonPrimeDS && /mingw64/bin/cmake.exe --build --preset=release-mingw-x86_64 --parallel 1 2>&1 | tail -20"
```

Note: keep the backtick before `$PATH` when running from PowerShell so bash receives `$PATH` instead of PowerShell expanding it.
