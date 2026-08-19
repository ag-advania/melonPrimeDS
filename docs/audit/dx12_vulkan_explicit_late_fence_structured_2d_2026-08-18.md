# DX12/Vulkan explicit renderer late-fence and Structured 2D audit — 2026-08-18

This is the implementation and evidence record for
`.codex/MelonPrimeDS_DX12_Vulkan_もっさり根本原因_修正指示_2026-08-18.md`.
The instruction document is intentionally not modified. No commit or push was
performed for this work.

The record separates source/build evidence from physical-runtime evidence. A
successful build or static audit is not promoted to a hardware, driver, or
click-to-photon result.

## Result summary

| Area | Status | Result |
| --- | --- | --- |
| CPU preparation versus raster-resource reuse wait | PASS by source/build | DX12 runs `Texcache.Update` and `BuildPolygons` before `Commands.Begin(true)`; Vulkan does the same before `Frames.BeginFrame(true)` and retains `RendererFramesInFlights=1`. |
| Upload/resource lifetime safety | PASS by source/build | Decoded texture bytes are owned by pending requests until command recording; uploads record after Begin, then existing barriers, failure handling, spill/scratch storage, graveyard, and retirement paths remain authoritative. |
| Normal-frame fence failure behavior | PASS by source audit/build | DX12 fence waits are bounded at 5000 ms and report device-removed state before failing; the Vulkan frame wait remains bounded. |
| Structured 2D producer work | PASS by source/build | Engine dirty planes are accumulated per scanline, routed screen-plane changes are batched, and plane/line metadata generation is published once per frame. Fallback uploads only changed planes. |
| Renderer and latency telemetry | PASS by source/build | Explicit raster/presenter metrics and developer-only input-to-run-frame/input-to-present metrics use the requested names and compile cleanly with telemetry both OFF and ON. |
| Physical A/B and runtime acceptance matrix | OPEN / NOT RUN | Same-ROM DX12/Vulkan before/after runs, OpenGL Compute comparison, scale/VSync/HUD/low-latency matrix, and p50/p95/p99/max acceptance measurements were not run in this task. |

## Implementation delivered

### Explicit backends and texture recording

The DX12 and Vulkan render paths now perform CPU-side texture-cache update and
polygon preparation before waiting for the single reusable raster command
context/frame slot. Texture uploads encountered during that preparation phase
copy their decoded bytes into owned pending requests. The request metadata and
decoded-byte capacities are retained as a reusable pool across frames; they are
not destroyed by the normal per-frame drain. After the reusable command
list/frame has begun, those requests are recorded and upload barriers are
flushed before geometry submission.

This preserves the required ownership boundaries: logical texture allocation
and eviction still happen in the texture cache, destroyed handles remove stale
pending uploads, upload failures still stop submission through the existing
failure path, and GPU-visible resources remain protected by the existing
retirement/graveyard or staging/spill lifetime. The implementation does not
increase frames-in-flight, remove synchronization, or disable VSync.

DX12 `WaitForFence()` now uses a bounded 5000 ms wait. Timeout and wait errors
log the fence value and device-removed reason, and `Commands.Begin()` refuses
to record when the reuse wait fails. Teardown still has its separate idle-drain
path. Vulkan retains the one-slot frame policy and its bounded frame-fence
wait.

### Structured 2D producer

Structured screen-plane dirty state is accumulated as masks instead of
publishing a generation on every pixel write. Engine scanline writes retain a
per-engine changed-plane mask and commit it once per line to the engine and
routed logical screen planes. Plane, line-metadata, and capture-command changes
are published together at the end of the structured frame. This reduces
metadata/generation churn while retaining the existing routing and fallback
semantics.

### Measurement and CI contracts

The DX12 and Vulkan telemetry headers expose the following common metrics:

```text
raster_cpu_prepare_us
raster_reuse_wait_us
raster_record_submit_us
soft2d_total_us
structured2d_metadata_us
structured_pack_us
present_slot_wait_us
present_acquire_wait_us
```

`present_acquire_wait_us` is Vulkan-specific; `present_slot_wait_us` remains
the presenter-slot wait metric. The developer-only host markers additionally
report:

```text
frame_input_sample_to_runframe_begin_us
input_sample_to_present_end_us
```

The explicit late-fence audit is wired into both Windows and Ubuntu CI. The
audit checks preparation/Begin ordering, deferred upload recording, bounded
waits, one-slot Vulkan policy, structured dirty batching, metric names, and
the input marker call sites.

## Source and build validation

| Check | Status | Evidence |
| --- | --- | --- |
| Explicit renderer late-fence audit | PASS | `python tools/ci/audits/audit-explicit-renderer-late-fence.py` |
| Renderer telemetry zero-overhead audit | PASS | `python tools/ci/audits/audit-renderer-perf-zero-overhead.py` |
| Structured composition contract audit | PASS | `python tools/ci/audits/audit-structured-composition-contract.py` |
| Software raster parity audit | PASS | `python tools/ci/audits/audit-raster-software-parity.py` |
| Low-latency contract audit | PASS | `python tools/ci/audits/audit-low-latency-contract.py` |
| Renderer memory production-overhead audit | PASS | `python tools/ci/audits/audit-renderer-memory-production-overhead.py` |
| Whitespace/error check | PASS | `git diff --check`; only expected LF/CRLF conversion warnings were emitted |
| Existing Release build, DX12/Vulkan, developer OFF, renderer telemetry OFF | PASS | `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\release-mingw-x86_64 --jobs 1 --tail 120` |
| Debug build, DX12/Vulkan, developer ON | PASS | `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\debug-mingw-x86_64 --jobs 1 --tail 120` |
| Release build, DX12/Vulkan, developer ON, renderer telemetry ON | PASS | `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\rebuild-mingw-x86_64 --jobs 1 --tail 120` |

All three configured builds completed successfully with one job. The direct
build test set passed, including the 82 registered-language Classic HUD layout
cases, structured-capture dependency, Vulkan present timing, Vulkan memory
admission/telemetry, queue sharing, Linux/direct surface lifecycle, presenter
timeout, present-pacer fake dispatch, renderer fallback, Intel XeLL state
machine, and DX12 memory-admission tests.

The telemetry-ON configuration was compiled and linked with
`MELONPRIME_VULKAN_LATENCY_CAPTURE=ON`; the default Release configuration was
also compiled with developer features and renderer telemetry disabled. No
runtime claim is inferred from those compile configurations.

## Runtime boundary and remaining acceptance gates

The following remain `OPEN / NOT RUN` for this task:

- Physical same-ROM/location/action A/B on current DX12 and Vulkan binaries,
  with OpenGL Compute as the comparator.
- 1x/4x/16x scale, VSync OFF/ON, HUD OFF/ON, and vendor low-latency OFF/ON
  matrix runs.
- Warmed p50/p95/p99/max measurements for the requested raster wait,
  input-to-present, and frametime acceptance comparisons.
- Capture, savestate, map/scene transition, weapon/effect stress, renderer
  switch, SyncVal, debug-layer, device-loss, and physical AMD/Intel/Linux/
  macOS/BSD coverage.

The DX12 waitable-object/maximum-frame-latency policy and Vulkan presenter
slot/acquire waits were intentionally retained pending those measurements.
Therefore this record establishes the source/build fix and its measurement
hooks; it does not claim that physical “mossari”/smoothness acceptance has
already passed.
