# Windows MinGW Build

Use this when an AI agent needs to build MelonPrimeDS on Windows and should not reconstruct the command by hand.

## One Command

Normal configure + build command, run from the repository root:

```bat
.\.claude\skills\build-mingw.bat
```

AI agents must run this as a single foreground command. Do not start it in the background, do not pipe it through an interactive `cmd.exe`, and do not run a hand-written `cmake -S . -B ... && cmake --build ...` command before or beside it.

The batch file:
- locates the repo root from its own path
- runs MSYS2 bash from `C:\msys64\usr\bin\bash.exe`
- configures `build/release-mingw-x86_64` with `MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON`
- puts `/mingw64/bin`, `/usr/bin`, Python 3.12, and the built vcpkg Qt/bin paths first in `PATH`
- builds preset `release-mingw-x86_64` with `--parallel 1`, streaming build output to the console live (via `stdbuf`+`tee`) and saving it to `build\release-mingw-x86_64\last-build.log`, then printing the last `--tail` lines as a recap
- returns a nonzero exit code on configure or build failure

The non-verbose path used to pipe through `tail -n N`, which buffers all input and prints nothing until the build finishes — the console looked idle the whole time. It now pipes through `tee` (plus `stdbuf -oL -eL` to force line buffering through the pipe) so build progress appears in the console as it happens; the `--tail`-sized recap is printed at the end in addition to the live stream.

## Existing Build Tree Only

If the normal command enters a long `vcpkg install` compile or if CMake configure is locked by a stale process, stop the repo-scoped stale build processes first, then use the build-only batch:

```bat
.\.claude\skills\build-mingw-existing.bat --tail 60
```

This variant:
- requires `build/release-mingw-x86_64/build.ninja` to already exist
- skips `cmake -S . -B ...`
- skips vcpkg manifest install/checks
- only runs `cmake --build --preset=release-mingw-x86_64 --parallel 1`
- uses the same MSYS2/MinGW/Python/Qt PATH setup as the normal batch

Use it for incremental verification when the build tree is already configured. Do not use it after changing `CMakeLists.txt`, presets, toolchain settings, dependencies, or feature flags; those require the normal configure + build command.

## Options

```bat
.\.claude\skills\build-mingw.bat --verbose
.\.claude\skills\build-mingw.bat --jobs 2
.\.claude\skills\build-mingw.bat --tail 80
.\.claude\skills\build-mingw-existing.bat --tail 80
```

Keep `--jobs 1` as the default for AI runs because this project can run out of memory during C++ compilation. Use `--jobs 2` only when the user asks or memory is known to be available.

## Notes

Do not bootstrap, rebuild, or reinstall `vcpkg` manually. Let CMake's normal manifest flow run through the existing repository setup.

The configure step may print `Running vcpkg install`; that is normal when CMake checks the manifest. It should usually be quick because this repo already has the needed packages installed. If it starts compiling many vcpkg ports or spawns many `cc1plus.exe` processes from `vcpkg\downloads\tools\cmake...\ninja.exe`, stop and inspect for a stale or parallel build before continuing. After stopping the stale processes, prefer `build-mingw-existing.bat` when you only need to verify source edits and the existing build tree is valid.

If ninja says there is no work after external edits, touch only the changed source files and run the batch again. Do not touch broad core files such as `src/ARM.cpp`, `src/NDS.cpp`, or `src/ARMJIT_x64/ARMJIT_Compiler.cpp` unless those files were actually edited; that can trigger a much larger rebuild.

## Recovering From A Timed-Out Build

If an AI tool times out while the batch is still compiling/linking, the underlying MSYS2/CMake/Ninja process can keep running in the background. A later build may then fail during CMake generation with a lock-like error such as:

```text
ninja: error: failed recompaction: Permission denied
CMake Generate step failed. Build files cannot be regenerated correctly.
```

First inspect only build-related processes tied to this repo:

```powershell
Get-CimInstance Win32_Process |
  Where-Object {
    $_.CommandLine -like '*melonPrimeDS*' -and
    $_.Name -match '^(bash|cmd|ninja|cmake|c\+\+|g\+\+|cc1plus|collect2|ld)\.exe$'
  } |
  Select-Object ProcessId,ParentProcessId,Name,CommandLine
```

If the listed processes are stale leftovers from the timed-out build, stop them:

```powershell
Get-CimInstance Win32_Process |
  Where-Object {
    $_.CommandLine -like '*melonPrimeDS*' -and
    $_.Name -match '^(bash|cmd|ninja|cmake|c\+\+|g\+\+|cc1plus|collect2|ld)\.exe$'
  } |
  ForEach-Object {
    Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
  }
```

Confirm the process list is empty, then rerun the normal batch command:

```bat
.\.claude\skills\build-mingw.bat --tail 60
```

Do not kill unrelated compiler or shell processes. Scope by `CommandLine -like '*melonPrimeDS*'` so only this repo's stale build is touched.

## If Another Build Is Already Running

Do not start a second build. Check first:

```powershell
Get-CimInstance Win32_Process |
  Where-Object {
    $_.CommandLine -like '*melonPrimeDS*' -and
    $_.Name -match '^(bash|cmd|ninja|cmake|c\+\+|g\+\+|cc1plus|collect2|ld)\.exe$'
  } |
  Select-Object ProcessId,ParentProcessId,Name,CommandLine
```

If this shows an active build started by another agent, wait for it to finish or stop the stale process set using the recovery steps above. Running the batch while a manual CMake/Ninja build is already active can make the machine look like it is rebuilding vcpkg and can leave build files locked.
