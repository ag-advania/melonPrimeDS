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
- `MelonPrimeEmuThread*.inc` files are unity include fragments pulled into `EmuThread.cpp`; do not add them to `CMakeLists.txt`
- The project has been built on Windows and via MinGW cross-compilation from WSL, and (since 2026-07) natively on macOS (Intel) — see the macOS Build section below
- `vcpkg/` is used for dependencies in the Windows setup; the macOS build uses Homebrew packages with `USE_VCPKG=OFF` (the default)

## CI Audits

The Windows GitHub Actions workflow is the canonical CI gate for MelonPrime
config/schema discipline. It runs before the Windows build:

- `.claude/skills/audit-config-defaults.ps1`
- `.claude/skills/audit-hud-key-parity.ps1 -Strict`
- `.claude/skills/check-inc-ownership.ps1`
- `.claude/skills/audit-metroid-literal-budget.ps1 -Budget 1`
- `.claude/skills/audit-platform-scatter-budget.ps1 -Budget 22`
- `.claude/skills/audit-color-dialog-prefs.ps1`
- `.claude/skills/audit-melonprime-thread-boundary.ps1 -Strict`
- `.claude/skills/generate-hud-prop-schema.py` followed by `git diff --exit-code`

Rules:

- The non-canonical `"Metroid.*"` literal budget is a ratchet. Lowering it is
  allowed; raising it requires a review note explaining why the new literal
  cannot live in `MelonPrimeHudPropSchema.inc`, `MelonPrimeOsdColorSchema.inc`,
  or `MelonPrimeDef.h`.
- Generated HUD schema files must be byte-identical after regeneration. If the
  generator output changes, commit both the source change and generated files in
  the same change.
- `.inc` fragments remain unity-build includes owned by their parent `.cpp` or
  `.inc`; do not add them directly to CMake.
- The macOS/Linux platform-condition scatter budget is also a ratchet. It is
  fixed at 22 call-site markers for **input/runtime dispatch**; future platform
  branching belongs in `MelonPrimePlatformInput.h` unless a review explicitly
  accepts the new call site. `MelonPrimeLocalization/` is out of scope (locale
  detection may use `__APPLE__` without consuming the input budget).

## HUD Golden Harness

The hidden `--melonprime-hud-golden <out>` CLI is developer-only and must leave
no release-binary symbols or strings. Update golden hashes only in a commit whose
purpose is an intentional visual change, and attach before/after screenshots or
hash output in the review notes. Refactors that claim visual identity should run
the harness without changing `src/frontend/qt_sdl/tests/melonprime-hud-golden.txt`.

## macOS Build (Homebrew, Intel/ARM)

Native macOS build added 2026-07. Uses Homebrew dependencies; vcpkg is not involved
(`USE_VCPKG` defaults `OFF`).

1. Dependencies (one-time): `brew install cmake ninja pkgconf sdl2 qt libarchive enet zstd faad2 libslirp`
   (recent Homebrew installs `sdl2-compat`, which satisfies the `sdl2` pkg-config check).
2. Configure + build from the repo root:

```zsh
cmake -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libarchive)" -DUSE_QT6=ON
cmake --build build-mac --parallel 4
```

The bundle is produced at `build-mac/melonPrimeDS.app`.

macOS platform notes:
- Windows-only RawInput sources are removed from the source list on APPLE.
- The RawInput-equivalent aim path is `MelonPrimeRawInputMacFilter.{h,mm}` (`#ifdef __APPLE__`
  TU, ObjC++/ARC). Backend order: **GCMouse** (GameController framework, macOS 11+, raw
  unaccelerated deltas, **no TCC permission needed**, frontmost-only delivery) → **IOHIDManager**
  (started only if no GCMouse appears within ~3s; needs the Input Monitoring permission, and
  unsigned dev builds lose that TCC grant on every rebuild — the reason GCMouse is primary) →
  QCursor center-delta fallback. `gcActive` gates the IOHID callback so the backends can never
  double-count. Backend selection is logged to stderr as `[MelonPrime] mac input: ...`.
- Mouse buttons / keyboard hotkeys use the Qt event path (`EmuInstance::onMousePress` etc.) —
  intentionally not HID-driven, to avoid double press edges (see MelonPrimeRawInputMacFilter.h).
- Cursor clipping (`ClipCursor`) is Windows-only. On macOS the in-game aim path uses
  `aimContainmentLocalRect()` union warps via `MacWarpCursorGlobal` (never `QCursor::setPos` —
  Qt uses `CGEventPost`, which needs Accessibility and a failed recenter spins aim). While
  clipped:
  - **GCMouse (external mouse)**: `MacSetAimCursorCaptured(true)` disassociates and hides the OS
    cursor; containment warps are skipped (avoids cursor flash on the DS screens).
  - **IOHID (built-in trackpad)**: raw aim deltas but **no** cursor disassociation — applying
    `MacSetAimCursorCaptured` on trackpad drops Qt release events and sticks fire/zoom. Use
    containment warps only. `syncMouseHotkeysFromQtButtons()` recovers lost releases.
  See [melonprime-aim-input.md](melonprime-aim-input.md) §10.
- Frame pacing uses the same portable SDL hybrid sleep+spin limiter; the spin loop issues
  `pause`/`yield` on x86/ARM (P-11 timer-resolution setup is Windows-only and not needed on macOS).

Linux platform notes:
- Windows/macOS native input sources are removed from Linux builds.
- The RawInput-equivalent aim path is `MelonPrimeRawInputLinuxFilter.{h,cpp}` (XInput2
  `XI_RawMotion` on X11). Raw mode engages only when `isAvailable() && hasReceivedMotion()` —
  XWayland sessions accept the XI2 selection but never deliver raw events, so trusting
  availability alone froze aim (2026-07-03). Relative axes are used as-is. **Absolute pointing
  devices — VirtualBox's integrated tablet pointer — are converted to deltas per-device**
  (successive-value differencing, axis modes queried via `XIQueryDevice`, baselines re-seeded by
  `resetAll()`). Axes above 0/1 (scroll wheel valuators) are never fed into aim.
- Why device-level deltas: warps never change a device's own axis state, so `XWarpPointer`
  echoes and VBox host-position re-syncs cannot corrupt them. The previous center-delta +
  warp-per-event scheme fought VBox's re-sync — the full host-minus-center offset was re-added
  on every host mouse event, producing a constant bottom-right drift with no aim control.
- The Qt fallback (raw silent / non-XCB) uses **previous-position differencing** in
  `ScreenPanel::mouseMoveEvent` — never center-delta + warp-per-event (drift, see above). All
  warps re-seed the baseline via `resetAimMouseDelta()` so jumps are never counted as motion.
- Linux never warps per-frame in `ProcessAimInputMouse` (`warpCursorAfterAim == false`);
  containment is the panel's >96px threshold warp. See `melonprime-aim-input.md` §9 for the
  full design and the three historical failure modes.
- Mouse buttons and keyboard hotkeys stay on the Qt/SDL path to avoid duplicate press edges.
- Wayland does not expose the needed global raw mouse stream to normal clients. On Wayland, missing
  XInput2, unavailable X11 display, or an XInput2 path that never emits raw motion, the aim path
  falls back to the Qt previous-position-differencing accumulator. Wayland warp behavior is
  compositor-dependent; use an Xorg session for reliable Linux FPS aim testing.
- **The recenter must use `MelonPrime::LinuxWarpCursorGlobal` (XWarpPointer) on X11, never
  `QCursor::setPos` there**: Qt's setPos can fail under VirtualBox guest mouse integration or
  when called off the GUI thread — a failed recenter re-applies the cursor-minus-center delta
  every frame and spins aim. Non-X11 Linux keeps the Qt fallback path.
- `ScreenPanel::unfocus()` and `unclip()` must run on Linux as well as Windows. Escape should always
  restore the arrow cursor and release any platform cursor confinement state.
- Linux builds need the XInput2 development library (`libxi-dev` on Ubuntu).
- **Linux VM testing on Mac**: see [linux-vm-build.md](linux-vm-build.md) — VirtualBox +
  Ubuntu 22.04 scripts under `tools/linux-vm/` (`01`…`05`; `05` is shared-folder remount only).

## Windows Build Command
Windows-only AI build command. Do not rebuild, bootstrap, or reinstall `vcpkg/` unless the user explicitly asks for it.

**AI agents must use the checked-in batch file** — do not hand-craft the long `powershell.exe -Command "& 'C:\msys64\usr\bin\bash.exe' ..."` chain. The batch file owns the MSYS2/MinGW PATH and the CMake invocation, and is the only supported AI build entry point on Windows.

Run it as a single foreground command. Do not start it in the background, do not run a manual CMake/Ninja build beside it, and do not touch broad core files just to force a rebuild.

Run from the repository root:

```bat
.\.claude\skills\build-mingw.bat
```

Default behavior: configures `build/release-mingw-x86_64` with `-DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON`, then builds the `release-mingw-x86_64` preset with `--parallel 1`, streaming the build output to the console live as it runs (via `tee`, not a buffering `tail`) and printing the last 40 log lines as a recap once it finishes. Full output is also saved to `build\release-mingw-x86_64\last-build.log`.

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
