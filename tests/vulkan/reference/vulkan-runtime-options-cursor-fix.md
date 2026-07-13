# Vulkan runtime options and cursor fix

Marker: `MELONPRIME_VULKAN_RUNTIME_OPTIONS_CURSOR_FIX_V1`

Acceptance points:

- The embedded `QVulkanWindow` and its container inherit the cursor selected by the MelonPrime cursor policy at match start and focus transitions.
- Vulkan renderer settings prefer `3D.Hardware.*`, retain legacy fallback, and are applied without restarting the process.
- Internal resolution changes the compatibility bridge output dimensions from 256x192 to `256*scale x 192*scale`.
- Better Polygons changes polygon triangulation from first-vertex fan to center fan.
- High Resolution Coordinates switches polygon vertices from integer `FinalPosition` to 4-bit fractional `HiresPosition`.
- Software 2D, display capture, master brightness, HUD and OSD remain the correctness baseline.
- Only final pixels that exactly match the native Software 3D layer are replaced by the high-resolution polygon result; unsupported mixed pixels remain Software.
