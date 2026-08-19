# Renderer physical A/B provenance and phase re-audit — 2026-08-19

## Scope

This audit implements the remaining evidence-contract work from the
2026-08-19 physical renderer A/B re-audit instruction. It covers binary
provenance, deterministic phase boundaries, machine-readable artifacts, and
the summarizer. It does not reopen Vulkan retire-frame, OOM retry, or
frames-in-flight architecture.

## Implemented contract

The physical runner at
[`tools/testing/renderer-physical-ab.ps1`](../../tools/testing/renderer-physical-ab.ps1)
now requires `-ExpectedSourceHead`. Before launching the emulator it reads
`melonPrimeDS.exe --build-info-json`, hashes the executable and ROM, compares
the binary source SHA and checkout HEAD, and fails closed on a mismatch. The
explicit `-AllowUnverifiedBinary` override records `provenance_verified=false`
and every verification failure in the manifest.

The build-info JSON records the source SHA, branch, dirty state, provider,
build type, renderer telemetry gate, Vulkan latency-capture gate, GPU-memory
telemetry gate, and developer-feature gate. The run manifest and metadata also
record the checkout dirty-state policy, executable SHA-256, ROM SHA-256, and a
dedicated `build_gates` object.

The runner records `process_start`, `warmup_end`, `measurement_start`,
`measurement_end`, `grace_end` when used, and `process_exit` in one
PowerShell Stopwatch/QPC domain. The application frame CSV and Vulkan latency
CSV carry the matching QPC frequency and absolute timestamps. The savestate
fixture is copied into a run-specific directory and the `savestate-load`
action sends the production F1 load-state shortcut after startup; the action
marker is checked separately from the startup diagnostic marker.

[`tools/testing/summarize-renderer-physical-ab.py`](../../tools/testing/summarize-renderer-physical-ab.py)
selects frame rows and telemetry report windows mechanically from the
manifest's measurement interval. It emits `summary.json` and `summary.md`,
reports frame and stage p50/p95/p99/max/sample counts, reports texture and
presentation counters, and derives `raster_gpu_time_ns` from renderer GPU-span
telemetry when optional per-present GPU timestamps are absent. Vulkan output
includes the `raster_gpu_time_ns` versus `raster_reuse_wait_us` report-window
correlation.

## Validation performed

The following source/model checks passed on the Windows workspace:

| Check | Result |
| --- | --- |
| PowerShell parser for the physical runner | PASS |
| Python syntax compilation for the new audit and summarizer | PASS |
| Physical A/B contract audit | PASS |
| Deterministic summarizer interval/GPU-fallback test | PASS |
| Configured MinGW existing-tree build with developer, renderer telemetry, Vulkan, and DX12 enabled | PASS |
| Existing Vulkan frame-retire, memory-admission/telemetry, queue-sharing, surface-lifecycle, presenter, fallback, and DX12 memory tests | PASS |

The build is compile/model evidence, not physical GPU evidence. A
provenance-verified physical run was not claimed in this commit: no new
run-manifest/summary was generated for the final pushed source, and the
available historical physical artifacts must not be promoted to this source
without rerunning the runner with their exact ROM/state fixtures and binary
hashes.

## Physical acceptance status

The requested 108-case matrix remains open:

```text
3 renderers x 3 scales x 2 VSync x 2 HUD x 3 seeds = 108 cases
```

OpenGL Compute remains `BLOCKED / backend availability`; fallback output is
not accepted as OpenGL Compute evidence. Low-latency modes remain a separate
matrix. Therefore this audit does not claim DX12/Vulkan/OpenGL parity or full
physical acceptance. The next measurement priority remains the Vulkan
`raster_gpu_time_ns` and `raster_reuse_wait_us` correlation before any move to
Software 2D or presenter investigation.

## CI wiring

The focused contract audit and deterministic summarizer test are wired into
the Windows, Ubuntu, macOS, and BSD workflows:

- [`tools/ci/audits/audit-renderer-physical-ab-contract.py`](../../tools/ci/audits/audit-renderer-physical-ab-contract.py)
- [`tools/testing/summarize-renderer-physical-ab-tests.py`](../../tools/testing/summarize-renderer-physical-ab-tests.py)

These checks validate the evidence contract in CI; they do not convert CI
builds into physical renderer acceptance evidence.
