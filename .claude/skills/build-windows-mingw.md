# Windows MinGW Build

Use this when an AI agent needs to build MelonPrimeDS on Windows and should not reconstruct the command by hand.

## One Command

Run from the repository root:

```bat
.\.claude\skills\build-mingw.bat
```

The batch file:
- locates the repo root from its own path
- runs MSYS2 bash from `C:\msys64\usr\bin\bash.exe`
- configures `build/release-mingw-x86_64` with `MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON`
- puts `/mingw64/bin`, `/usr/bin`, Python 3.12, and the built vcpkg Qt/bin paths first in `PATH`
- builds preset `release-mingw-x86_64` with `--parallel 1`
- returns a nonzero exit code on configure or build failure

## Options

```bat
.\.claude\skills\build-mingw.bat --verbose
.\.claude\skills\build-mingw.bat --jobs 2
.\.claude\skills\build-mingw.bat --tail 80
```

Keep `--jobs 1` as the default for AI runs because this project can run out of memory during C++ compilation. Use `--jobs 2` only when the user asks or memory is known to be available.

## Notes

Do not bootstrap, rebuild, or reinstall `vcpkg` manually. Let CMake's normal manifest flow run through the existing repository setup.

If ninja says there is no work after external edits, touch the changed source files and run the batch again.
