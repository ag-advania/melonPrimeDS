# Custom HUD spawn helmet flash

## Symptom

With Custom HUD enabled (helmet hide on), the native helmet/visor briefly flashes at spawn (match start and every respawn).

## Root cause (mphCodex)

The helmet hide patch flips the orr at the helmet site (JP1.0 `0202F934`: `hudToggle |= 0x0E` → `&= ~0x0E`), so the game itself clamps the top-screen BG1-3 layer bits every frame the upper-HUD block runs.

But that block (`0202F624-0202F9FC`) **early-returns during spawn/death states** (e.g. `0202F894`: player state byte `+0x4D6 == 3` → return). In that window, init/restore writers (`02030E58 = 0x1F`, `0202A494 |= 0x0E`, `0205A958` saved-mode restore) set the helmet layers and the final reflection (`02007374`: `DISPCNT = (DISPCNT & ~0x1F00) | (hudToggle << 8)`) pushes them into `04000000` → the visor flashes until the clamp path runs again.

## Rejected approaches

- **Full NoHud writer set (17 NOPs) + `hudToggle = 0x01`** — fixes the flash but forces BG0-only: kills OBJ/BG0-based elements and conflicts with per-element selective hide settings. Also broke the start-button score UI until gated. **Do not use.**
- **Partial writer-NOP spawn guards** — still racy; a writer + reflection inside one RunFrame beats pre-frame instruction patching.

## Final fix (selective, host-side)

`NoHudPatch_ClampHelmetLayers` (MelonPrimePatchNoHud.cpp), called every frame from `RunFrameHook` (before `RunFrame`) via `CustomHud_ClampHelmetLayersPreFrame`:

1. `hudToggle &= ~0x0E` (RAM) — replicates the patched clamp even when the game's HUD block early-outs.
2. main `DISPCNT (0x04000000) &= ~0x0E00` — clears layers the spawn-frame VBlank logic already reflected, before this frame's scanlines render.

Only BG1-3 of the top screen are touched — BG0/OBJ stay game-controlled, so other selective-hide elements and native UI are unaffected.

Gating:
- Tracker-based: runs only when `NoHudPatch_GetAppliedMask()` has `NOHUD_HELMET` (no per-frame config lookup).
- Skipped on start pressed / HP 0 / game over / adventure pause → native score/death UI keeps normal layers.
- `Render()` (MelonPrimeHudRenderMain.inc) syncs `NoHudPatch_Sync` **before** the gameplay-state early return so the helmet bit is tracked from the first loading frame.

Timing argument: melonDS renders scanlines first, then VBlank (game logic). A spawn-frame write of `0x1F` reflects into DISPCNT at VBlank, but the next pre-frame clamp clears it before any scanline of the next frame renders → no visible flash.

## mphCodex references

- `mnt/data/analysis/mphAnalysis/_JP1_0/HUD-Selective-Hide-JP1_0.md` — per-element patch table, `hudToggle` ↔ DISPCNT bit map
- `mnt/data/analysis/mphAnalysis/_JP1_0/HUD_NoHud_Patch_Analysis-JP1_0.md` — writer map, `0202F624-0202F9FC` block, reflection at `02007374`
