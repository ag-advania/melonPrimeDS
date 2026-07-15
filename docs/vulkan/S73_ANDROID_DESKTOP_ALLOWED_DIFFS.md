# Android / Desktop Allowed Differences (S73-12)

Production Vulkan composition must stay Sapphire-exact. Desktop may differ only at platform boundaries.

## Allowed

- `ANativeWindow` ↔ Qt `VkSurfaceKHR` / `VulkanSurfacePresenter`
- Android Vulkan loader ↔ Desktop `volk`
- Android filesystem/logging ↔ Desktop `Platform` adapter
- Android overlay-less present ↔ Desktop final CustomHUD pass
- `DesktopSapphireFrameSidecar` validation metadata (never fed into Sapphire algorithms)
- `BuildDesktopSapphireFrameInput` pointer/lifetime/generation validation

## Not allowed

- Extra `SoftPackedFrameSnapshot` fields
- Desktop temporal repair modes (`CurrentFrameOnly`, provenance repair, legacy heuristic gates)
- Partial temporal disable (FrameLatch off while VulkanOutput previous-source on)
- Composition condition forks in `VulkanOutput.cpp`
- Capture class / screen owner / class4 threshold changes

## Pipeline modes

- **SapphireExact** (production default): end-to-end temporal pipeline enabled
- **AllTemporalDisabled** (debug only, `NDEBUG` off): all temporal layers disabled together

## Verification

```bash
python3 tools/check_vulkan_output_header_contract.py
python3 tools/generate_sapphire_frame_latch.py --verify
python3 tools/check_sapphire_frame_latch_parity.py --upstream-tag 0.7.0.rc4
python3 tools/check_sapphire_vulkan_output_exact.py
python3 tools/test_sapphire_vulkan_golden_snapshot_s73.py
python3 tools/test_sapphire_vulkan_flicker_s73.py
python3 tools/test_sapphire_latch_golden_fixture.py
python3 tools/test_sapphire_latch_flicker_fixture.py
```
