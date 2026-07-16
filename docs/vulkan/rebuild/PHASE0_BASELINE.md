# Phase 0 — Pre-rebuild baseline (frozen)

Frozen at tag `vulkan-pre-sapphire-rebuild` → commit `90bf8333a`.

## Reference Sapphire sources

| Repository | Pin | Local path |
|---|---|---|
| `SapphireRhodonite/melonDS-android` | tag `0.7.0.rc4` (`2c10e59d`) | `C:\Users\Admin\Documents\git\melonDS-android` |
| `SapphireRhodonite/melonDS-android-lib` | `d77944275fa61f9b79cfcead2c3e98993429a023` | `C:\Users\Admin\Documents\git\melonDS-android-lib` |

## Cold-start reproduction (pre-rebuild)

| Field | Value |
|---|---|
| ROM | Fixed by `tools/test_sapphire_vulkan_cold_start_regression_s79.py` |
| Window | Windowed 1× |
| HUD / filter / repair | Off in test config |
| Exit code | `3221225477` (`0xC0000005` ACCESS_VIOLATION) |
| Fault address | `0xFFFFFFFFFFFFFFFF` (read) |
| Crash register hint | `rax = -1` (invalid vtable load) |
| Stable RVA family | `~0xF941` (Release LTO, diagnostic symbols optional) |
| Last known good trace | `FinishFrame(263)` complete; crash in CPU path after callback returns |
| Framebuffer canaries | Not triggered |
| JIT disabled | Still crashes → not GPU2D heuristic alone |

## Vulkan dependency graph (pre-rebuild production path)

```text
EmuThread::RunFrame
  → GPU timing (StartHBlank / FinishFrame)
  → GPU::PublishSapphire2DFrameIfReady (post-RunFrame)
  → MelonPrimeVulkanFrontendSession::completeProducerFrame
       → BuildCompletedSapphireFrameTuple / BuildDesktopSapphireFrameInput
       → SapphireVulkanFrameLatch (generated core + desktop sidecar)
       → FrameQueue (generated core)
       → VulkanOutput::prepareFrame / composeAndSubmitFrame
  → MelonPrimeScreenVulkan::present
       → VulkanSurfacePresenter::presentFrame
```

## Layers removed from production path during rebuild

- `ActiveGPU2DPath` legacy switching
- `SapphireGpu2DState` dual activation (rebuild uses exact-pin GPU2D only)
- Desktop temporal repair / per-line repair (not in Sapphire vendor path)
- Presenter frame history outside Sapphire FrameQueue
- Snapshot ABI desktop field extensions (sidecar only)

## Stage hashes (vendor parity)

Verify with:

```text
python tools/vendor_sapphire.py --verify-upstream-snapshots
python tools/generate_sapphire_frame_queue.py --verify
python tools/generate_sapphire_frame_latch.py --verify
```

Manifest: `tools/sapphire_vendor_manifest.json`, `src/frontend/qt_sdl/VulkanReference/SAPPHIRE_SOURCE_MANIFEST.md`.
