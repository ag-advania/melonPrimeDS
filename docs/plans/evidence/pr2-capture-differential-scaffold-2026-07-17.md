# PR-2 evidence — capture differential validation scaffold

**Date:** 2026-07-17  
**HEAD base:** `cf962615` (PR-1)  
**Instruction:** `docs/plans/melonPrimeDS_develop_metal_完全Metal化_実行指示書_17b46586_2026-07-17.md` §8

## Implemented

- Runtime flag `MELONPRIME_METAL_CAPTURE_EXPERIMENT=1`
- Optional artifacts via `MELONPRIME_METAL_CAPTURE_EXPERIMENT_DIR=<dir>`
- SoftRenderer remains display authority on capture frames
- Side-channel Metal candidate (`mp_metal_capture_experiment`) from Soft `Output2D`/`Output3D` + aux VRAM/FIFO
- Per-line checksum / mismatch metadata → stderr + `capture-lines.csv` / `frame.json`
- PPM stand-ins for reference/metal/diff (PNG deferred)
- Homebrew matrix design: `docs/plans/metal-capture-homebrew-test-design.md`
- Static audit: `tools/ci/audits/audit-metal-capture-experiment-scaffold.py`

## Explicitly unchanged (§29)

- `CaptureCnt` Full-GPU exclusion (`GPU.CaptureCnt & (1u << 31)`)
- `MetalRenderer : public SoftRenderer`
- RasterReference / GetLine path
- Presenter CpuBgra / lease policy (experiment never publishes candidate)

## Validation

| Check | Result |
|---|---|
| `audit-metal-output-state-publication.py` | PASS |
| `audit-metal-capture-experiment-scaffold.py` | PASS |
| macOS Intel Metal ON rebuild (`build-mac-metal`) | PASS |
| Live ROM / experiment run | **not run** (needs capture-active scene + flag) |
| Homebrew ROM + checksum golden | **design only** |
| Capture-backed source B compare | **skipped** until PR-3 native textures |

## Gate decision

PR-2 **accepted as scaffold** for continuing to PR-3 (native canonical capture storage). Production display path is unchanged when the flag is off; when on, candidate stays side-channel.
