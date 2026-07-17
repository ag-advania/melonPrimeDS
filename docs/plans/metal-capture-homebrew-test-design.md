# Capture homebrew test design (PR-2 scaffold)

Status: design only (no ROM checked in yet). Bound to
`MELONPRIME_METAL_CAPTURE_EXPERIMENT=1` differential scaffold.

## Goal

One deterministic NDS homebrew scene per matrix cell with fixed per-line
checksums in `capture-lines.csv`, so PR-3+ can regress without relying on
commercial ROMs alone.

## Proposed scenes (1 ROM or multi-scene ROM)

| ID | Capture size | Source A | Source B | Mode | Notes |
|---|---|---|---|---|---|
| C128-A2D | 128x128 | Engine A 2D | — | A | solid + checker BG |
| C128-A3D | 128x128 | 3D | — | A | signed X offset ±8 |
| C256x64-BV | 256x64 | — | VRAM | B | src offset 0..3 |
| C256x128-BF | 256x128 | — | FIFO | B | FIFO fill pattern |
| C256x192-AB | 256x192 | 2D | VRAM | A+B | EVA/EVB 8/8 |
| C256-AB-CAP | 256x192 | 2D | capture-backed | A+B | skipped by scaffold until PR-3 |

## Expected harness

```bash
export MELONPRIME_METAL_CAPTURE_EXPERIMENT=1
export MELONPRIME_METAL_CAPTURE_EXPERIMENT_DIR=/tmp/mp-capture-exp
# run homebrew for N frames, then:
diff -u expected/capture-lines.csv "$MELONPRIME_METAL_CAPTURE_EXPERIMENT_DIR/capture-lines.csv"
```

## Artifact contract (already emitted by scaffold)

- `reference-native.rgb5551` / `metal-native.rgb5551`
- `reference.ppm` / `metal.ppm` / `diff.ppm` (PPM stand-in for PNG)
- `frame.json` / `capture-lines.csv` / `commands.txt`

## Out of scope for PR-2

- Shipping the `.nds` binary
- Automating the matrix in CI (PR-15)
- Removing CaptureCnt Full-GPU exclusion
