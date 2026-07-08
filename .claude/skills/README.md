# Skills

- [Windows MinGW Build](build-windows-mingw.md) — build MelonPrimeDS on the Windows dev machine
  - `build-mingw.bat`: configure + build
  - `build-mingw-existing.bat`: build existing tree only, no configure/vcpkg
- [macOS Build](build-macos.md) — configure, build, and launch-test MelonPrimeDS on macOS
  - `build-macos-dev.sh`: canonical dev configure + build (`DEVELOPER_FEATURES=ON`, parallel 4)
  - `build-macos-dev.command`: Finder-friendly wrapper for `build-macos-dev.sh`
  - `build-macos-dev-existing.sh`: build existing `build-mac` tree only
  - `build-macos-dev-existing.command`: Finder-friendly wrapper for incremental rebuild
  - `build-macos.sh`: optional wrapper (`--release`, `--jobs`, `--open`, …)
- [Custom HUD Runtime](custom-hud-runtime.md) — reference for runtime HUD rendering, caches, fonts, no-HUD patch, radar overlay
- [Settings UI and Edit Mode](settings-ui-and-edit-mode.md) — settings dialog and in-game HUD edit mode; how to add a HUD setting surface
- [Qt Menu Actions](qt-menu-actions.md) — add a menu item / checkable MelonPrime toggle to the Qt menubar
- [Add a Menu Language](add-menu-language.md) — add a new `MenuLangId` / locale detection / translation content, and how to verify a translation pack before applying it
- [Merge Latest Commits from melonDS Upstream](merge-upstream-melonds.md) — pull `melonDS-emu/melonDS:master` into this fork
- [Release Notes Generation](release-notes.md) — generate GitHub release notes from a commit range

## Utilities

- `audit-config-defaults.ps1`: verify Metroid config read/default type coverage, including HUD schema defaults
- `audit-hud-key-parity.ps1`: compare HUD key references across schema-aware defaults, dialog, edit descriptors, side panel, and runtime load
- `audit-metroid-literal-budget.ps1`: enforce the non-canonical quoted `"Metroid.*"` literal ratchet budget
- `audit-platform-scatter-budget.ps1`: enforce the V4 macOS/Linux input-dispatch
  scatter ratchet (budget 22; excludes facade + localization) and macOS
  cursor-warp guard
- `apply-perf-baseline.py`: apply perf `.summary.txt` files to the V6/V5 baseline markdown tables
- `compare-perf-repro.py`: compare two perf logs/summaries for V6 S24 p50/p99 reproducibility
- `collect-perf-baseline.sh` / `.ps1`: run a developer build with `MELONPRIME_PERF=1`, tee the log, and write a summary for V6 Phase 0
- `generate-hud-prop-schema.py`: generate the V2 Phase 2 HUD property schema, dialog prop include, on-screen edit prop include, and drift report
- `check-inc-ownership.ps1`: verify `.inc` ownership expectations
