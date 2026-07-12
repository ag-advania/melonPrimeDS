# Expected baseline logs

Local baseline logs must identify all of the following without relying on a
window title or persisted configuration alone:

```text
requested renderer
normalized renderer (when a policy layer is active)
actual renderer
internal scale
Better Polygons
High Resolution Coordinates
GPU and driver
frame/capture serial when available
draw or dispatch count when diagnostics are enabled
upload and readback byte counts when diagnostics are enabled
```

Phase 0 does not add runtime logging. If a field is unavailable in the current
OpenGL implementation, record it as `unavailable` in `manifest.local.json`;
later Vulkan phases must not reinterpret unavailable data as a passing result.
