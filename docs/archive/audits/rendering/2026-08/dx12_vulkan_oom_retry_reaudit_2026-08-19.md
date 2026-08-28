# DX12/Vulkan OOM retry re-audit — 2026-08-19

## Scope

- Instruction: `.codex/MelonPrimeDS_DX12_Vulkan_OOM_retry再監査_追加修正指示_2026-08-19.md`
- Audit baseline: `f961e785440f2f718401f3719af518dcc8119493` (`develop_hud`)
- This record covers the residual Vulkan deferred-destruction frame-tag issue and
  the documentation/audit corrections requested by the instruction.

## Result

| Gate | Status | Evidence |
| --- | --- | --- |
| P2 Vulkan deferred-destroy frame tag | PASS by source/build/model | `FrameRing::GetResourceRetireFrame()` selects the current recording frame, the last submitted frame, or the completed frame according to lifecycle state. `VulkanTextureHeap::RetireEntry()` no longer uses `GetAbsoluteFrame()` directly. |
| P2 forced-OOM retry ordering | PASS by model/build | The fake-dispatch test forces the first materialization to fail, collects the completed frame, verifies old image/view/memory destruction before the second allocation, and verifies exactly one retry with no terminal runtime-failure path. |
| P3 comments and explicit late-fence audit | PASS by source/audit | Texcache ordering comments and the Vulkan reset comment describe the current pre-fence materialization/post-fence upload contract. The late-fence audit enforces the FrameRing helper policy, call-site usage, and model-test target. |
| Physical DX12/Vulkan A/B and hardware acceptance | OPEN / NOT RUN | No same-ROM physical GPU comparison, capture, driver matrix, or warmed frame-time run was performed in this audit. |

## Implemented contract

`AbsoluteFrame` is the scheduler's next frame number and is not a valid
deferred-resource lifetime tag. The new FrameRing policy is:

1. While recording, retire against the current frame's `SubmittedFrame`.
2. After submission and outside recording, retire against the last submitted
   frame's `SubmittedFrame`.
3. Before any submission exists, retire against `CompletedFrame` (zero in the
   no-submission case).

Vulkan texture retirement uses `GetResourceRetireFrame()`. Scratch upload
resources are recorded during the current command recording and therefore use
`GetCurrentRecordingFrameNumber()` explicitly. The existing bounded OOM path
still collects the completed frame before its single retry; device loss and
other non-retryable failures remain terminal.

## Validation

Static audits:

```text
python tools/ci/audits/audit-explicit-renderer-late-fence.py
PASS: explicit DX12/Vulkan late-fence and structured producer contract

python tools/ci/audits/audit-renderer-memory-production-overhead.py
PASS: GPU memory production-overhead contract

python tools/ci/audits/audit-renderer-perf-zero-overhead.py
PASS: Vulkan/DX12 renderer telemetry compile-time zero-overhead contract

python tools/ci/audits/audit-structured-composition-contract.py
structured composition contract OK

python tools/ci/audits/audit-raster-software-parity.py
PASS: confirmed Software parity and bounded hot-path rules are ratcheted for Metal, Vulkan and DX12

git diff --check
PASS (only normal LF-to-CRLF conversion warnings were emitted by Git)
```

Build and model-test validation:

- Measurement configuration (`build/rebuild-mingw-x86_64`, developer and
  renderer telemetry enabled, Vulkan latency capture enabled):
  `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\rebuild-mingw-x86_64 --jobs 1 --tail 180`
  — build succeeded; `vulkan-frame-retire-tests: PASS`.
- Shipping configuration (`build/release-mingw-shipping-x86_64`, developer and
  renderer telemetry disabled):
  `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\release-mingw-shipping-x86_64 --jobs 1 --tail 220`
  — build succeeded; telemetry remains compiled out; `vulkan-frame-retire-tests: PASS`.

These source, model, and build results do not establish physical GPU runtime
coverage. The physical A/B gate remains explicitly open.
