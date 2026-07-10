# MelonPrime Video Quality — macOS Compute Renderer Restriction

**Status:** Implemented 2026-07-09.  
**Branch:** `highres_fonts_v3`

## Summary

The MelonPrime Settings `VIDEO QUALITY` section has three one-click presets
(`Low`, `High`, `High2`). `High2` sets `3D.Renderer = renderer3D_OpenGLCompute`
(the OpenGL compute-shader renderer). macOS's OpenGL implementation does not
support the compute renderer path reliably, so selecting it there can crash.

This mirrors the existing restriction in the plain melonDS Video Settings
dialog ([VideoSettingsDialog.cpp](../../src/frontend/qt_sdl/VideoSettingsDialog.cpp)),
which already does:

```cpp
#ifdef __APPLE__
    ui->rb3DCompute->setEnabled(false);
#endif
```

## Implementation

File: [InputConfig/MelonPrimeInputConfig.cpp](../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp)

1. `DisableMacComputeVideoQualityButton(Ui::MelonPrimeInputConfig*)` — anonymous-namespace
   helper next to `ConfigureScreenSyncControlsForPlatform`, guarded by
   `#ifdef __APPLE__`. Disables `ui->metroidSetVideoQualityToHigh2` and sets an
   explanatory tooltip.
2. Called twice in the `MelonPrimeInputConfig` constructor:
   - immediately after `ui->setupUi(this)`, and
   - again immediately after `MelonPrime::UiText::LocalizeWidgetTree(this)`,
     since widget-tree localization can otherwise overwrite the tooltip text.
3. `on_metroidSetVideoQualityToHigh2_clicked()` itself early-returns under
   `#ifdef __APPLE__` as defense in depth, in case the slot is ever invoked
   directly (auto-connect, future refactor, etc.) instead of only through the
   disabled button.

`Low` (`renderer3D_Software`) and `High` (`renderer3D_OpenGL`) are unaffected
on all platforms; `High2` remains fully available on Windows/Linux.

## Deliberately not changed

- `InputConfig/MelonPrimeInputConfig.ui` — not touched. A `.ui`-level
  `enabled="false"` would apply to all platforms; platform gating stays in the
  `.cpp` via `#ifdef __APPLE__`, consistent with the rest of the codebase's
  platform-scatter convention (see [melonprime-aim-input.md](melonprime-aim-input.md)
  and [repo-architecture.md](repo-architecture.md) Platform Input section for
  the analogous pattern in input code).
- `VideoSettingsDialog.cpp` — already had the equivalent restriction for its
  own compute-renderer radio button; not part of this change.
- ~~No normalization of an already-saved `3D.Renderer = renderer3D_OpenGLCompute`
  config value on macOS...~~ **Closed 2026-07-09** by
  [melonprime-metal-backend-plan.md](melonprime-metal-backend-plan.md) Phase 0
  (`MelonPrimeVideoBackend::NormalizeRendererForPlatform()`, wired into
  `EmuThread::updateRenderer()`'s existing `.inc` hook and
  `EmuInstance::usesOpenGL()`). A stale/imported/hand-edited
  `3D.Renderer=renderer3D_OpenGLCompute` value on macOS now normalizes to
  regular OpenGL before it ever reaches `nds->SetRenderer(...)`, instead of
  only being blocked at the UI-selection level described above.
