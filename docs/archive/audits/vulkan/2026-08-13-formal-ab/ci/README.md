# Current-SHA hosted CI follow-up — 2026-08-13

The original cross-platform build workflows were manually dispatched against
the audited implementation SHA `4a503debf15abd4120e1bf4e19629f396800bf33`
on 2026-08-13. A later workflow-only update added a hosted macOS arm64
MoltenVK runtime smoke; run `31711999894` checked out
`56eddfb61ca6d87fd921da8f1937324bd86cd9a2` and kept the same runtime source
implementation. These are cross-platform build/integration evidence; they do
not change the local NVIDIA/Windows Formal Phase 3 A/B result or convert
unavailable hardware into synthetic runtime results.

| Platform/workflow | Run | Result | Evidence | Remaining limitation |
|---|---|---|---|---|
| Ubuntu | [31707409936](https://github.com/ag-advania/melonPrimeDS/actions/runs/31707409936) | PASS | Audits, x86_64, aarch64, and bundled Linux artifacts all passed. The x86_64 job built the Vulkan debug validation configuration and passed the Xvfb smoke. | The smoke selected Mesa llvmpipe (CPU); AMD/Intel Linux hardware was not exercised. |
| macOS bundle | [31707412897](https://github.com/ag-advania/melonPrimeDS/actions/runs/31707412897) | PASS | x86_64, arm64, and universal jobs passed. The universal bundle verified pinned MoltenVK v1.4.0, `libMoltenVK.dylib`, the license notice, both architectures, and `codesign --verify --deep --strict`. | This run was bundle-only; see the hosted runtime smoke below. |
| macOS runtime smoke | [31711999894](https://github.com/ag-advania/melonPrimeDS/actions/runs/31711999894) | PASS | arm64 job ran the bundled app on `macos-26-arm64`; MoltenVK created a Vulkan instance, `VK_EXT_metal_surface` surface, swapchain, and `Apple Paravirtual device` presenter. VSync-off selected `IMMEDIATE`; no VUID/SYNC-HAZARD/DEVICE_LOST/runtime-failure finding. The uploaded artifact is `melonprime-moltenvk-runtime.log`. | Hosted Apple Paravirtual/Apple Virtual environment, no ROM gameplay session or physical retail-Mac coverage. |
| Windows | [31707415459](https://github.com/ag-advania/melonPrimeDS/actions/runs/31707415459) | PASS | Current-SHA Vulkan-enabled Windows release build completed. | This is not a replacement for the local NVIDIA formal capture or AMD/Intel runtime evidence. |

## Ubuntu validation-smoke excerpt

The x86_64 job ran `xvfb-run` with the validation layer enabled and required
the following runtime milestones: Vulkan instance creation, presentation
surface creation, present-mode selection, and presenter readiness. The log
reported `VK_KHR_xcb_surface`, `selected-present-mode=IMMEDIATE`, and
`presenter ready` for the generic `TelemetryOnly` path. The workflow's negative
check for `VUID-` and `Vulkan/validation` output passed. The selected device was
`llvmpipe (LLVM 15.0.7, 256 bits) (Mesa, CPU)`.

The hosted CI checks therefore close the current-SHA Linux build/validation
smoke, macOS bundle verification, and hosted macOS/MoltenVK startup/WSI smoke.
DPI, AMD Anti-Lag runtime, Intel Vulkan runtime, Linux hardware/vendor runtime,
and physical retail-Mac/full-ROM coverage remain `NOT RUN`.

## macOS MoltenVK runtime-smoke excerpt

The arm64 job in [31711999894](https://github.com/ag-advania/melonPrimeDS/actions/runs/31711999894)
ran the bundled executable on the hosted `macos-26-arm64` image with no ROM,
which is sufficient to exercise application startup, Vulkan instance/device
selection, the `CAMetalLayer`/`VK_EXT_metal_surface` WSI, swapchain creation,
and presenter initialization. The log reported an `Apple Paravirtual device`,
`renderer requested=Vulkan presenter actual=Vulkan`,
`selected-present-mode=IMMEDIATE`, and `presenter ready`. The workflow rejected
`VUID-`, `SYNC-HAZARD`, `DEVICE_LOST`, and `[Vulkan] runtime failure` findings;
the MoltenVK optional-feature warnings about primitive restart are retained in
the uploaded smoke log and are not treated as validation-layer findings.
