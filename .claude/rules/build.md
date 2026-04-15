# Build

## Build Notes
- Main feature flags in `src/frontend/qt_sdl/CMakeLists.txt`:
  - `MELONPRIME_DS`
  - `MELONPRIME_CUSTOM_HUD`
- `MelonPrimeHudRender.cpp` and `InputConfig/MelonPrimeInputConfig.cpp` are explicitly built as part of the frontend
  - `MelonPrimeHudRender*.inc` files are unity include fragments pulled in by `MelonPrimeHudRender.cpp`; do not add them to `CMakeLists.txt`
- `MelonPrimeHudConfigOnScreen.cpp` is a unity-build include (pulled in by `MelonPrimeHudRender.cpp`); do not add it to `CMakeLists.txt`
  - Its `MelonPrimeHudConfigOnScreen*.inc` fragments are also unity include fragments; do not add those to `CMakeLists.txt` either
- The project has been built on Windows and via MinGW cross-compilation from WSL
- `vcpkg/` is used for dependencies in this repo setup

## Windows Build Command
Windows-only AI build command. Do not rebuild, bootstrap, or reinstall `vcpkg/` unless the user explicitly asks for it.

Use MSYS bash and the existing CMake preset. Put `/mingw64/bin` first in `PATH`; Qt's `moc.exe` depends on MinGW DLLs such as `libgcc_s_seh-1.dll` and `libstdc++-6.dll`, and can fail with `Process failed with return value 3221225781` if they are not discoverable.

```powershell
& 'C:\msys64\usr\bin\bash.exe' -lc "export PATH='/mingw64/bin:/c/Program Files/Python312:/c/Program Files/Python312/Scripts:/c/Users/Admin/Documents/git/melonPrimeDS/build/release-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/tools/Qt6/bin:/c/Users/Admin/Documents/git/melonPrimeDS/build/release-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/bin:`$PATH'; cd /c/Users/Admin/Documents/git/melonPrimeDS && /mingw64/bin/cmake.exe --build --preset=release-mingw-x86_64 --parallel 1 --verbose 2>&1"
```

For short output, use:

```powershell
& 'C:\msys64\usr\bin\bash.exe' -lc "export PATH='/mingw64/bin:/c/Program Files/Python312:/c/Program Files/Python312/Scripts:/c/Users/Admin/Documents/git/melonPrimeDS/build/release-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/tools/Qt6/bin:/c/Users/Admin/Documents/git/melonPrimeDS/build/release-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/bin:`$PATH'; cd /c/Users/Admin/Documents/git/melonPrimeDS && /mingw64/bin/cmake.exe --build --preset=release-mingw-x86_64 --parallel 1 2>&1 | tail -20"
```

When building manually from the `C:\msys64\mingw64.exe` console:

```bash
export PATH="/c/Program Files/Python312:/c/Program Files/Python312/Scripts:$PATH"
cd /c/Users/Admin/Documents/git/melonPrimeDS
cmake --build --preset=release-mingw-x86_64 --parallel 2
```

Notes:
- Keep the backtick before `$PATH` in PowerShell commands so bash receives `$PATH` instead of PowerShell expanding it.
- Avoid unlimited `--parallel`; it can exhaust RAM during compilation and fail with `cc1plus.exe: out of memory allocating 65536 bytes`. Use `--parallel 1` for AI builds, or `--parallel 2` when building manually if memory allows.
