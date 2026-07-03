# Non-MelonPrime Upstream Diff

Status: captured on 2026-06-11 after the upstream merge commit `05d54243`.

Compared range:

```powershell
git diff upstream/master..05d54243 -- ':!src/frontend/qt_sdl/MelonPrime*' ':!src/frontend/qt_sdl/InputConfig/MelonPrime*'
```

Baseline refs:

- `upstream/master`: `10a173b5`
- local branch tip at capture time: `05d54243`
- `upstream/master` is an ancestor of the local branch.

## Scope

This note intentionally excludes the primary MelonPrime implementation files whose basename starts with `MelonPrime` under `src/frontend/qt_sdl/` and `src/frontend/qt_sdl/InputConfig/`.

It includes upstream-owned files that now contain MelonPrime integration points, plus fork-only documentation, assets, CI, and build metadata whose filenames do not start with `MelonPrime`.

The captured non-MelonPrime diff was 133 files, about 15k insertions and 2.5k deletions. Most of the meaningful risk is concentrated in core hook sites, `EmuThread`, `Screen`, `Config`, and input/menu plumbing.

## Core Runtime

| Files | Difference from upstream |
| --- | --- |
| `src/CMakeLists.txt` | Defines `MELONPRIME_DS` for the core target so core code can expose hook points to the Qt frontend integration. |
| `src/ARM.cpp` | Adds ARM interpreter hook inclusion for ARM9 instruction interception under `MELONPRIME_DS`. |
| `src/ARMJIT.cpp`, `src/ARMJIT_x64/ARMJIT_Compiler.cpp`, `src/ARMJIT_x64/ARMJIT_Compiler.h` | Adds JIT-side hook/trampoline integration so ARM9 instruction hooks stay active when JIT is enabled. |
| `src/NDS.cpp`, `src/NDS.h` | Adds native BIOS checksum tracking and accessors, plus ARM9 hook declarations. This is used by boot/direct-boot and patch safety checks. |
| `src/DMA.cpp` | Replaces `std::array` assignment of MRAM burst timing tables with `__builtin_memcpy`. This is compatibility/performance oriented and not directly MelonPrime gameplay logic. |
| `src/GPU3D_Compute.cpp` | Reworks compute renderer tile sizing in `SetRenderSettings()` with branch-minimized arithmetic. This belongs with the compute renderer mosaic/performance history. |

Watch these files closely during upstream merges. CPU decode, JIT block generation, BIOS boot, and renderer tiling changes can silently break MelonPrime hooks even when the MelonPrime files themselves merge cleanly.

## Qt Frontend Integration

| Files | Difference from upstream |
| --- | --- |
| `src/frontend/qt_sdl/CMakeLists.txt` | Adds the MelonPrime source inventory, compile definitions, custom HUD option, developer feature option, and Windows raw input source handling. |
| `src/frontend/qt_sdl/Config.cpp`, `src/frontend/qt_sdl/Config.h` | Adds Metroid/MelonPrime defaults, legacy key aliases, HUD and radar settings, menu language setting, feature toggles, input defaults, and `GL_BetterPolygons` default handling. |
| `src/frontend/qt_sdl/EmuInstance.cpp`, `src/frontend/qt_sdl/EmuInstance.h` | Adds ROM metadata capture, MPH ROM detection warning, config migration, RTC sync throttling, MelonPrime hotkey IDs, input masks, mouse hooks, and helper access used by `MelonPrimeCore`. |
| `src/frontend/qt_sdl/EmuInstanceInput.cpp` | Adds Metroid hotkey names, 64-bit input masks, mouse button hotkey handling, KB+M fast path, joystick refresh after frame pacing sleep, and analogue hotkey helpers. |
| `src/frontend/qt_sdl/EmuThread.cpp`, `src/frontend/qt_sdl/EmuThread.h` | Owns `MelonPrimeCore`, exposes `GetMelonPrimeCore()`, adds fast message pending state, includes split MelonPrime frame/message helpers, calls frame hooks and deferred input drain, manages pause/reset lifecycle hooks, restores VSync handling, skips unused DSi volume sync for NDS-only MelonPrime, and carries frame timing/debug plumbing. |
| `src/frontend/qt_sdl/Screen.cpp`, `src/frontend/qt_sdl/Screen.h` | Adds custom HUD rendering/editing integration, bottom-screen cursor clipping, layout epoch hooks, software and GL overlay paths, radar overlay GL resources, mouse and wheel forwarding for edit mode, and OSD fast paths. |
| `src/frontend/qt_sdl/Window.cpp`, `src/frontend/qt_sdl/Window.h` | Adds the Metroid menu, settings entry points, runtime feature toggles, menu localization updates, Escape cursor unlock behavior, and localized dynamic cart/recent action text. |
| `src/frontend/qt_sdl/InputConfig/InputConfigDialog.cpp`, `src/frontend/qt_sdl/InputConfig/InputConfigDialog.h` | Embeds Metroid input/settings pages into the standard input config dialog and routes save/refresh/tab switching. |
| `src/frontend/qt_sdl/InputConfig/MapButton.h` | Allows MelonPrime bindings to keep modifiers and display native mouse/key text where upstream normally filters those cases. |
| `src/frontend/qt_sdl/main.cpp` | Sets Qt runtime attributes and environment variables used by the Windows/MelonPrime low-latency path. |
| `src/frontend/qt_sdl/main_shaders.h` | Adds bottom radar overlay shaders and palette uniform support. |
| `src/frontend/qt_sdl/OSD_shaders.h` | Adjusts OSD shader sampling for MelonPrime's overlay path. |
| `src/frontend/qt_sdl/EmuSettingsDialog.*`, `src/frontend/qt_sdl/VideoSettingsDialog.h` | Effectively formatting/noise only in the current diff. `git diff -w` leaves only a blank-line deletion in `VideoSettingsDialog.h`. |
| `src/frontend/qt_sdl/.claude/settings.local.json` | Local frontend workspace settings, not runtime code. |

Highest merge-conflict risk lives in `EmuThread.cpp`, `Screen.cpp`, `Config.cpp`, and `Window.cpp` because upstream also changes these for event loop, renderer, config, and menu work.

V6 Phase 1 marker snapshot (2026-07-04, `highres_fonts_v3` @ `d90cc1ec`):

| File | `MELONPRIME` marker count |
|---|---:|
| `src/frontend/qt_sdl/EmuThread.cpp` | 53 |
| `src/frontend/qt_sdl/Screen.cpp` | 32 |
| `src/frontend/qt_sdl/Window.cpp` | 20 |
| `src/frontend/qt_sdl/Config.cpp` | 17 |
| `src/frontend/qt_sdl/EmuInstance.h` | 16 |
| `src/frontend/qt_sdl/Screen.h` | 16 |
| `src/frontend/qt_sdl/EmuInstanceInput.cpp` | 13 |

These are recorded for future upstream merge triage only; V6 does not reduce
hook sites without a concrete upstream merge target.

## Build, CI, and Tooling

| Files | Difference from upstream |
| --- | --- |
| `.github/workflows/build-bsd.yml`, `.github/workflows/build-macos.yml`, `.github/workflows/build-ubuntu.yml` | Restored from upstream and adapted for MelonPrimeDS artifact naming/packaging. |
| `.github/workflows/build-windows.yml` | Replaced upstream matrix workflow with a Windows x86_64 MSYS2/UCRT64 + MinGW + pinned vcpkg flow, explicit binary/download/build caches, `release-mingw-x86_64` preset use, and MelonPrime config/schema/literal audits. |
| `cmake/overlay-triplets/x64-mingw-static-release.cmake` | Uses `VCPKG_ENV_PASSTHROUGH_UNTRACKED PATH` for the MinGW static-release triplet. |
| `vcpkg` | Adds a vcpkg submodule pointer at `dd3097e3`. |
| `.gitignore` | Adds local IDE, vcpkg, Claude workspace, and old asset ignore entries. |
| `CMakeLists.txt`, `cmake/ConfigureVcpkg.cmake` | Only end-of-file newline differences in the captured diff. |
| `flake.nix` | End-of-file newline difference only. |

## Assets and Resources

| Files | Difference from upstream |
| --- | --- |
| `res/assets/weapons/**`, `res/assets/bombs/**`, `res/assets/radar/Radar.svg` | Adds custom HUD weapon, bomb, pickup, and radar visual assets, including raw PNG variants. |
| `res/fonts/mph.ttf` | Adds the MPH font used by the custom HUD/OSD path. |
| `res/melon.qrc` | Registers the new HUD/radar/font resources with Qt resources. |

## Documentation and Automation

| Files | Difference from upstream |
| --- | --- |
| `CLAUDE.md`, `.claude/**` | Adds the local project rules, skills, build helpers, merge workflow, refactor notes, patch knowledge, and agent/hook indexes. |
| `.codex/config.toml` | Adds local Codex project config. |
| `README.md` | Fork-oriented README content with MelonPrimeDS context. |

These files are not upstream runtime code, but they are part of the fork contract. Keep them when comparing local behavior to upstream melonDS.

## Regenerating

To refresh the inventory after another upstream merge:

```powershell
git fetch upstream
git diff --name-status upstream/master..HEAD -- ':!src/frontend/qt_sdl/MelonPrime*' ':!src/frontend/qt_sdl/InputConfig/MelonPrime*'
git diff --stat upstream/master..HEAD -- ':!src/frontend/qt_sdl/MelonPrime*' ':!src/frontend/qt_sdl/InputConfig/MelonPrime*'
```

For dialog/UI files with noisy formatting, also check:

```powershell
git diff -w upstream/master..HEAD -- src/frontend/qt_sdl/EmuSettingsDialog.cpp src/frontend/qt_sdl/EmuSettingsDialog.h src/frontend/qt_sdl/EmuSettingsDialog.ui src/frontend/qt_sdl/VideoSettingsDialog.h
```
