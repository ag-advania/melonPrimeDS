# Current-SHA hosted CI follow-up — 2026-08-13

The workflows below were manually dispatched against the audited implementation
SHA `4a503debf15abd4120e1bf4e19629f396800bf33` on 2026-08-13. They are
cross-platform build/integration evidence; they do not change the local
NVIDIA/Windows Formal Phase 3 A/B result or convert unavailable hardware into
synthetic runtime results.

| Platform/workflow | Run | Result | Evidence | Remaining limitation |
|---|---|---|---|---|
| Ubuntu | [31707409936](https://github.com/ag-advania/melonPrimeDS/actions/runs/31707409936) | PASS | Audits, x86_64, aarch64, and bundled Linux artifacts all passed. The x86_64 job built the Vulkan debug validation configuration and passed the Xvfb smoke. | The smoke selected Mesa llvmpipe (CPU); AMD/Intel Linux hardware was not exercised. |
| macOS | [31707412897](https://github.com/ag-advania/melonPrimeDS/actions/runs/31707412897) | PASS | x86_64, arm64, and universal jobs passed. The universal bundle verified pinned MoltenVK v1.4.0, `libMoltenVK.dylib`, the license notice, both architectures, and `codesign --verify --deep --strict`. | No macOS runtime session was executed. |
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
smoke and macOS bundle-verification portions only. DPI, AMD Anti-Lag runtime,
Intel Vulkan runtime, and physical macOS/MoltenVK runtime remain `NOT RUN`.
