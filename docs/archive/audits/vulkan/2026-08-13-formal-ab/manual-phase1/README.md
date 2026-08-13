# Manual Phase 1 evidence

Executable: `build/debug-mingw-vulkan-validation2/melonPrimeDS.exe`.
Validation and Sync Validation were enabled for these runs. No runtime source
code was changed for the manual gate.

## Results

| Gate | Result | Evidence |
|---|---|---|
| Video Settings | PASS | `video-20-final-r2/manual-video.harness.log`: dialog observed, `cancel=1..20`, `apply-same-value=1..20`; `video-change-final-r4/manual-video-change.harness.log`: VSync changed and runtime `requested-vsync=off`; validation findings 0 |
| Fast Forward / Slow Motion | PASS | `speed-final-r3/manual-speed.harness.log`: all four hold/toggle actions sent, runtime speed-path evidence 11, normal-speed target path reactivated, validation findings 0 |
| ROM lifecycle | PASS | `rom-final/manual-rom.harness.log`: save slot 1, load, undo, reset, and second-session reopen; both sessions reported actual Vulkan/JIT startup; validation findings 0 |
| renderer Software | PASS | `vk-manual-renderer-software-ca390.out.log`: 20 stress switches, Sync hazards 0, validation clean |
| renderer OpenGL | PASS | `vk-manual-renderer-opengl-ca390.out.log`: 20 stress switches, Sync hazards 0, validation clean |
| renderer OpenGL Compute | PASS | `vk-manual-renderer-compute-ca390.out.log`: 20 stress switches, Sync hazards 0, validation clean |
| renderer DX12 | PASS | `vk-manual-renderer-dx12-ca390.out.log`: 20 stress switches, Sync hazards 0, validation clean |
| window/minimize/fullscreen matrix | PASS | `vk-manual-window-all-ca390.out.log`: resize x40, minimize/restore x20, fullscreen x8, swapchain rebuilds 70, DEVICE_LOST 0 |
| targeted Sync follow-up | PASS | renderer-switch and fullscreen paths above each ran with Sync Validation; all reported zero hazards |
| DPI transition | NOT RUN | `dpi-final/manual-dpi.harness.log`: one physical monitor exposed; no genuine cross-DPI transition available |

The first 20-cycle Video Settings attempt was interrupted when the desktop
window lost focus during user mouse input. It is retained as an aborted
diagnostic attempt; the separate `video-20-final-r2` run was executed without
desktop interaction and is the authoritative PASS for cancel/same-value Apply.
The independent `video-change-final-r4` run is the authoritative changed-setting
Apply PASS; it toggled VSync through the dialog and observed
`requested-vsync=off` with `selected-present-mode=IMMEDIATE`.

All final Debug Validation stderr logs are empty, and the final harnesses
report no `VUID-`, `SYNC-HAZARD`, `DEVICE_LOST`, or software-fallback findings.
