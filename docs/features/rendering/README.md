# Rendering feature documentation

This directory contains the backend-specific rendering references. It is an
index, not another renderer implementation document.

## Choose by backend or question

| Question | Document |
| --- | --- |
| How is the Vulkan renderer structured and presented? | [Vulkan backend](vulkan-backend.md) |
| How is the Windows DirectX 12 renderer structured and presented? | [DirectX 12 backend](dx12-backend.md) |
| Why is the macOS OpenGL compute preset restricted? | [macOS compute renderer restriction](macos-compute-renderer-restriction.md) |
| What is the backend-neutral ownership contract? | [SRP and performance contract](../../architecture/srp-performance-contract.md) |
| How do I build on Windows? | [Windows MinGW build](../../development/build/windows-mingw.md) |
| How do I build/test on macOS? | [macOS build](../../development/build/macos.md), [macOS Vulkan build](../../development/build/macos-vulkan.md) |
| How is raster parity measured? | [Raster parity](../../development/rendering/raster-parity.md) |
| What is still being planned or audited? | [Vulkan/DX12 SRP refactor plan](../../plans/rendering/vulkan-dx12-srp-refactor.md) |

## Selection and fallback boundary

The host ultimately resolves the presentation route from the current screen
and 3D renderer configuration in Window.cpp. Platform gates and initialization
fallbacks belong to that source path; the backend pages document the
implementation contract after a route has been selected.

Do not infer runtime availability from a button or a compile definition alone:

- a platform may expose a build option but disable a backend at runtime;
- a backend may fall back when device, surface, shader, or swapchain
  initialization fails; and
- a saved renderer value is not by itself evidence that a frame was presented
  by that backend.

## Common rendering evidence

Keep these claims separate:

| Claim | Evidence needed |
| --- | --- |
| Source path is gated correctly | Static source/config audit |
| Backend compiles | Named local build |
| Backend initializes | Runtime startup log or test |
| Frames are presented by the backend | Runtime presentation trace |
| Visual output is correct | Renderer-specific parity or screenshot/test evidence |
| Performance or latency is acceptable | Timed run with expected frame count and profile metadata |

The Vulkan and DX12 pages contain their own ownership and performance
boundaries. Update this index when a new backend page becomes current; do not
move detailed tables here.
