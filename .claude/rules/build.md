# Build

## Build Notes
- Main feature flags in `src/frontend/qt_sdl/CMakeLists.txt`:
  - `MELONPRIME_DS`
  - `MELONPRIME_CUSTOM_HUD`
- MelonPrimeDS development builds should configure with `MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON`
  - This enables the `DEVELOPER ONLY` settings section for local development and patch verification
  - Release/distribution builds should explicitly configure this option `OFF`
- `MelonPrimeHudRender.cpp` and `InputConfig/MelonPrimeInputConfig.cpp` are explicitly built as part of the frontend
  - `MelonPrimeHudRender*.inc` files are unity include fragments pulled in by `MelonPrimeHudRender.cpp`; do not add them to `CMakeLists.txt`
- `MelonPrimeHudConfigOnScreenUnity.inc` is a unity-build include (pulled in by `MelonPrimeHudRender.cpp`); do not add it to `CMakeLists.txt`
  - Its `MelonPrimeHudConfigOnScreen*.inc` fragments are also unity include fragments pulled in by `MelonPrimeHudConfigOnScreenUnity.inc`; do not add those to `CMakeLists.txt` either
- `MelonPrimeHudScreenCpp*.inc` files are unity include fragments pulled into `Screen.cpp`; do not add them to `CMakeLists.txt`
- The project has been built on Windows and via MinGW cross-compilation from WSL
- `vcpkg/` is used for dependencies in this repo setup

## Windows Build Command
Windows-only AI build command. Do not rebuild, bootstrap, or reinstall `vcpkg/` unless the user explicitly asks for it.

**AI agents must use the checked-in batch file** — do not hand-craft the long `powershell.exe -Command "& 'C:\msys64\usr\bin\bash.exe' ..."` chain. The batch file owns the MSYS2/MinGW PATH and the CMake invocation, and is the only supported AI build entry point on Windows.

Run it as a single foreground command. Do not start it in the background, do not run a manual CMake/Ninja build beside it, and do not touch broad core files just to force a rebuild.

Run from the repository root:

```bat
.\.claude\skills\build-mingw.bat
```

Default behavior: configures `build/release-mingw-x86_64` with `-DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON`, then builds the `release-mingw-x86_64` preset with `--parallel 1` and prints the last 40 build log lines.

If the build tree is already configured and the normal command gets stuck in a long vcpkg configure/install path, stop stale repo-scoped build processes and use the build-only batch:

```bat
.\.claude\skills\build-mingw-existing.bat --tail 60
```

This skips CMake configure and vcpkg entirely, requiring an existing `build/release-mingw-x86_64/build.ninja`, then runs only `cmake --build --preset=release-mingw-x86_64 --parallel 1`. Do not use it after changing CMake files, presets, dependencies, toolchain settings, or feature flags.

Options:

```bat
.\.claude\skills\build-mingw.bat --verbose
.\.claude\skills\build-mingw.bat --jobs 2
.\.claude\skills\build-mingw.bat --tail 80
.\.claude\skills\build-mingw-existing.bat --tail 80
```

Keep `--jobs 1` for AI runs because this project can run out of memory during C++ compilation (`cc1plus.exe: out of memory allocating 65536 bytes`). Only raise jobs when the user explicitly asks for it or memory is known to be available.

Invoke the bat from Claude Code's Bash tool via PowerShell so cmd's quoting does not silently drop the path:

```bash
powershell.exe -NoProfile -Command "& '.\\.claude\\skills\\build-mingw.bat'"
```

Use `--verbose` for full compiler output when investigating an error; otherwise let the default 40-line tail keep transcript noise low. The skill source lives at [.claude/skills/build-windows-mingw.md](../skills/build-windows-mingw.md).

If a previous AI build timed out and the next run fails with `ninja: error: failed recompaction: Permission denied`, use the recovery steps in [.claude/skills/build-windows-mingw.md](../skills/build-windows-mingw.md#recovering-from-a-timed-out-build): inspect repo-scoped stale MSYS2/CMake/Ninja/compiler processes, stop only those tied to `melonPrimeDS`, then rerun the batch.

If `Running vcpkg install` turns into a long package rebuild with many `cc1plus.exe` processes or `vcpkg\downloads\tools\cmake...\ninja.exe`, suspect a stale/parallel build or invalidated build cache. Do not launch another build; inspect and stop only repo-scoped stale build processes using the skill's recovery commands. If the build tree is already configured, use `build-mingw-existing.bat` for the verification build after cleanup.

### What the batch file does

- Locates the repo root from its own path (works from any cwd).
- Runs MSYS2 bash from `C:\msys64\usr\bin\bash.exe`.
- Puts `/mingw64/bin`, `/usr/bin`, Python 3.12, and the built vcpkg Qt/bin paths first in `PATH`. Qt's `moc.exe` depends on MinGW DLLs (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`) and fails with `Process failed with return value 3221225781` if those are not discoverable.
- Configures `build/release-mingw-x86_64` with `MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON`. The MinGW configure preset can be disabled when launched from a Windows-hosted shell, so the batch file uses `-S . -B build/release-mingw-x86_64` instead.
- Builds preset `release-mingw-x86_64`.
- Returns a non-zero exit code on configure or build failure.

### Fallback (only if the batch file is unavailable or broken)

The raw command the batch file expands to, kept here only as a reference for debugging the batch file itself. Do not run this directly during normal AI work — use the batch file:

```bash
powershell.exe -Command "& 'C:\msys64\usr\bin\bash.exe' -lc \"cd /c/Users/Admin/Documents/git/melonPrimeDS && export PATH='/mingw64/bin:/usr/bin:/c/Program Files/Python312:/c/Program Files/Python312/Scripts:/c/Users/Admin/Documents/git/melonPrimeDS/build/release-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/tools/Qt6/bin:/c/Users/Admin/Documents/git/melonPrimeDS/build/release-mingw-x86_64/vcpkg_installed/x64-mingw-static-release/bin:\$PATH'; /mingw64/bin/cmake.exe -S . -B build/release-mingw-x86_64 -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON && /mingw64/bin/cmake.exe --build --preset=release-mingw-x86_64 --parallel 1 2>&1 | tail -20\""
```

When building manually from the `C:\msys64\mingw64.exe` console:

```bash
export PATH="/c/Program Files/Python312:/c/Program Files/Python312/Scripts:$PATH"
cd /c/Users/Admin/Documents/git/melonPrimeDS
cmake -S . -B build/release-mingw-x86_64 -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON
cmake --build --preset=release-mingw-x86_64 --parallel 2
```

Notes:
- Use `powershell.exe -Command "..."` wrapper and escape inner `$PATH` as `\$PATH` so bash receives it unexpanded.
- Include `/usr/bin` in PATH inside the bash session so utilities like `tail` work.
- Avoid unlimited `--parallel`; it can exhaust RAM during compilation and fail with `cc1plus.exe: out of memory allocating 65536 bytes`. Use `--parallel 1` for AI builds, or `--parallel 2` when building manually if memory allows.
- Ninja detects changes by mtime. If files were edited externally and ninja reports `no work to do`, run `touch` on the changed files before rebuilding:
  ```bash
  powershell.exe -Command "& 'C:\msys64\usr\bin\bash.exe' -lc \"touch /c/Users/Admin/Documents/git/melonPrimeDS/src/frontend/qt_sdl/ChangedFile.cpp\""
  ```
