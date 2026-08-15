# Vulkan Formal Phase 3 A/B — 2026-08-13

## Verdict

`FORMAL NVIDIA PHASE 3 = COMPLETE` for the fixed NVIDIA/Windows capture
surface described below. The result does not authorize a shipping-default
change or a cross-vendor claim. `TelemetryOnly` remains the default.

The closest candidate was A2. It improved the measured host-pipeline proxy
median P50 by 3.3815% versus A0, but its median P95 improvement was 1.9302%,
below the 2% winner threshold. Therefore the result is `NO MATERIAL
DIFFERENCE` for default-selection purposes, not a latency-superiority claim.
The CSV does not measure click-to-photon latency.

## Fixed capture

| Item | Value |
|---|---|
| source/runtime revision | `ca390a48bbeb5a4c1135417e9070d59e018ac1c1` |
| release executable SHA-256 | `82414AA36E76291CBBEB45460B93C5C9D640CBA446182BA02D5234C3A201346B` |
| ROM SHA-256 | `3F07C5832F7ADA7C6BBE38C6C690E1FFA41188B14E402F2AA1F1142A5E34F12C` |
| build | Release, MinGW/Ninja, developer features OFF |
| capture | Vulkan latency capture ON; validation/layers OFF for formal capture |
| display | one fixed Windows display surface, 60 FPS target |
| formal budget | 600 warm-up + 10,000 measured frames requested per run |
| runs | 21 CSVs, 3 independent runs per mode, randomized order with recorded catch-up |

Configure/build commands:

```text
/mingw64/bin/cmake.exe -S . -B build/release-mingw-x86_64 -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF -DMELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE=ON -U pkgcfg_lib_Faad_m -U pkgcfg_lib_SDL2_m
tools/build/windows/build-mingw-existing.bat --build-dir build\release-mingw-x86_64 --jobs 1 --tail 50
```

The formal runner disables implicit Vulkan layers with
`VK_LOADER_LAYERS_DISABLE=~implicit~`, writes a unique run ID/CSV, and restores
the portable configuration and layer-settings file byte-for-byte.

## Formal result

Values below are medians of per-run statistics; percentiles were not pooled
across frames. `pipeline` is the host-pipeline proxy ending at the Vulkan
present call, not system or click-to-photon latency. Frame-time P99 is included
as the regression guard.

| Mode | Valid | Target active | Max wait timeout | Pipeline P50 ms | Pipeline P95 ms | Frame P99 ms | Result |
|---|---:|---:|---:|---:|---:|---:|---|
| A0 TelemetryOnly | 3/3 | N/A | N/A | 4.081 | 7.046 | 20.614 | baseline |
| A1 PresentWait | 3/3 | N/A | 0.0949% | 4.119 | 7.078 | 20.606 | no material difference |
| A2 JIT | 3/3 | 100% | 0% | 3.943 | 6.910 | 20.513 | candidate; P95 threshold missed |
| A3 JIT + FIFO latest-ready | 3/3 | 100% | 0.0079% | 4.159 | 7.034 | 20.498 | no material difference |
| B1 Reflex On + JIT | 3/3 | N/A | N/A | 4.098 | 7.126 | 20.604 | no material difference |
| B2 Reflex On+Boost + JIT | 3/3 | N/A | N/A | 4.156 | 7.194 | 20.707 | no adoption; latency/frame-time trade-off |
| C0 JIT, VSync off | 3/3 | 0% | 0.1186% | 3.988 | 6.960 | 20.503 | control only |

Machine checks:

- aggregator exit code: `0`;
- `invalid_rows=0` for all 21 runs;
- measured-window swapchain generation changes: `0` for all 21 runs;
- A2 and A3 target-active ratio: `100%` for every run;
- PresentWait maximum timeout rate: `0.0949%` (`<1%`);
- timing queue full/recovery counts: `0/0` for every run;
- formal stdout/stderr: no `VUID-`, `SYNC-HAZARD`, or `DEVICE_LOST` findings.

The complete per-run table is [`summary.csv`](summary.csv), and the raw
CSV/log/metadata files are under [`runs/formal`](runs/formal). The actual order,
including the late A1-R0 catch-up caused by an initial batch omission, is in
[`run-order.txt`](run-order.txt). The catch-up did not overwrite any artifact.

## Manual Phase 1

The Debug Validation manual harness results are summarized in
[`manual-phase1/README.md`](manual-phase1/README.md). Video settings passed
20 cancel, 20 same-value apply, and a changed-VSync Apply cycle; the changed
run observed `requested-vsync=off` and `selected-present-mode=IMMEDIATE`.
Speed modes passed with runtime
`fallback=not normal speed`, `targetScheduling=off`, and `boundedWait=off`
evidence; ROM save/load/undo/reset plus reopen passed; renderer switching
passed 20 iterations each for Software, OpenGL, OpenGL Compute, and DX12.
The window/fullscreen/minimize matrix and targeted Sync follow-up were clean.

DPI is explicitly `NOT RUN`: WMI exposed one physical monitor, so there was no
genuine cross-DPI monitor transition available. This is not represented as a
pass. AMD Anti-Lag runtime, Intel Vulkan, Linux hardware/vendor runtime, and
physical retail-Mac/full-ROM coverage remain separate gates. Hosted macOS
MoltenVK startup/WSI smoke is recorded in the CI evidence, but it does not
replace those gates. Current-SHA cross-platform CI build and smoke evidence is
recorded in
[`ci/README.md`](ci/README.md); it does not replace those hardware/runtime
gates.

## Current-SHA CI follow-up

The audited implementation SHA `4a503debf15abd4120e1bf4e19629f396800bf33` was
verified by the manually dispatched Ubuntu, macOS, and Windows workflows on
2026-08-13. The later workflow-only macOS update also ran the hosted arm64
MoltenVK startup/WSI smoke at run `31711999894`; source runtime code remained
unchanged. The run-level evidence and limitations are recorded in
[`ci/README.md`](ci/README.md).

The current-SHA macOS follow-up at run `31716658392` completed successfully
with arm64 runtime smoke, x86_64/arm64/universal bundle checks, and signatures
passing. Its separate Intel-host MoltenVK diagnostic was intentionally
non-gating: the hosted Apple Paravirtual Metal device reached Vulkan
instance/surface/logical-device creation but exited `SIGABRT (-6)` before
presenter readiness. This is not native Intel Vulkan coverage; the raw log is
retained as artifact `9187724074`.

## Reproduction and evidence

- Runner: [`tools/testing/vulkan-present-formal-ab.ps1`](../../../../../tools/testing/vulkan-present-formal-ab.ps1)
- Manual UI driver: [`tools/testing/vulkan-manual-ui-actions.ps1`](../../../../../tools/testing/vulkan-manual-ui-actions.ps1)
- Aggregator: [`tools/perf/aggregate-vulkan-latency.py`](../../../../../tools/perf/aggregate-vulkan-latency.py)
- Evidence verifier: [`tools/perf/verify-vulkan-formal-ab.py`](../../../../../tools/perf/verify-vulkan-formal-ab.py)
- Environment: [`environment.txt`](environment.txt)
- Platform availability audit: [`platform-availability.txt`](platform-availability.txt)
- Verification output: [`verification.log`](verification.log)
- Aggregator stderr: [`aggregate.stderr.log`](aggregate.stderr.log)
- Aggregator stdout CSV: [`aggregate.stdout.csv`](aggregate.stdout.csv)

## Physical Intel macOS follow-up — 2026-08-14

The separate physical Intel/MoltenVK current-SHA gate is recorded in
[`docs/archive/audits/vulkan/2026-08-14-intel-macbook/README.md`](../2026-08-14-intel-macbook/README.md).
It must not be merged into this NVIDIA/Windows formal A/B verdict. The
follow-up reached bundled MoltenVK, `VK_EXT_metal_surface`, the real Intel
GPU, and a Vulkan ROM frame, while controlled gameplay/lifecycle and long AC
coverage remained open. No historical evidence in this directory was deleted.
