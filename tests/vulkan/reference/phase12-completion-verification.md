# Phase 12 Vulkan UI / settings / presets / localization verification

Phase 12 adds capability-aware Vulkan and Vulkan Compute choices without changing renderer implementation.
The UI is generated only inside the Vulkan build gate. On macOS it additionally requires a MoltenVK build.
Device probe failures, missing presentation support, insufficient raster/compute limits, and the ROM-visible
acceptance gate each produce a distinct reason. The current ROM-visible Vulkan path intentionally remains
disabled until scoped pixel-parity acceptance is complete.

The harness validates renderer-ID safety, new hardware setting keys with legacy GL-key dual-write,
Cancel restoration contract, raster/compute presets, option enable matrix, every selectable language ID,
RTL metadata preservation, and dynamic language text regeneration.
