# MelonPrime HUD Preview Drift Phase 3a

Verified on 2026-07-02 during V3 Phase 3a.

## Surfaces

| Surface | Current behavior | Phase 3 risk |
|---|---|---|
| Runtime HUD | `MelonPrimeHudRenderDraw.inc` consumes `CachedHudConfig`; hot path and dirty-rect logic are performance sensitive. | Do not redesign. Only pure geometry helpers may be extracted if call order and formulas stay identical. |
| In-game edit preview | `DrawEditHudPreview()` in `MelonPrimeHudConfigOnScreenDraw.inc` already calls runtime draw functions for Preview ON. Selection bounds use local geometry approximations. | Keep runtime draw calls intact. Shared helpers are useful for anchor/text/gauge bounds only. |
| Settings dialog previews | `InputConfig/MelonPrimeInputConfigHudPreviews.inc` has independent QPainter drawing and reads `Config::Table` directly in each widget. | Main drift source: duplicate anchor, alignment, gauge and crosshair geometry plus scattered config reads. |

## Drift Points

| Area | Runtime/edit source | Dialog preview source | Existing drift |
|---|---|---|---|
| Anchor resolution | `ApplyAnchor(anchor, ofsX, ofsY, outX, outY, topStretchX)` in `MelonPrimeHudRenderConfig.inc`. | `HudPreviewWidget::dsPos()` uses `(anchor % 3) * 128` / `(anchor / 3) * 96` and assumes `topStretchX = 1`. | Same for normal 256x192 preview; dialog has no top-stretch path. |
| Text horizontal alignment | `CalcAlignedTextX()` in `MelonPrimeHudRenderDraw.inc`. | `HudPreviewWidget::alignedX()`. | Formula matches for 0/1/2 alignment. |
| Gauge rectangle alignment | `DrawGauge()` / `CalcGaugePos()` in `MelonPrimeHudRenderDraw.inc`. | `HudPreviewWidget::drawGaugeDS()` plus local relative positioning in HP/ammo preview. | Basic align formula matches; dialog intentionally draws simplified 50% gauges without runtime ramp/outline behavior. |
| Crosshair base arms | `LoadCrosshairConfig()` precomputes cached rects, `DrawCrosshair()` renders those rects with zoom/fx gates. | `drawBaseCrosshair()` recomputes arms from config at paint time. | Same key set, but dialog has no cached rects, dirty rect, jump/fade/game-state gating, or animated FX. |
| Zoom scope | `DrawZoomScopeReticle()` and transition helpers use cached zoom scope config and game zoom amount. | `ZoomScopePreviewWidget` draws fixed fully-scoped scope and optional faded base crosshair. | Intentional preview-only behavior; useful for settings inspection but not a frame-accurate runtime mirror. |
| Outlines | Runtime uses `HudOutlineConfig`, cached text/image outlines, and crosshair outline stamps. | Dialog preview draws simplified outlines directly. | Intentional simplification; do not silently “fix” during geometry extraction. |

## Phase 3b/3c Scope

Safe extraction target:

- Anchor resolution.
- Text alignment.
- Gauge rectangle alignment.
- Small config-read snapshot helpers for dialog previews.

Out of scope for this phase unless separately reviewed:

- Runtime crosshair cache layout.
- Dirty-rect accumulation.
- Zoom transition FX.
- Text/image cache rendering.
- Dialog preview visual simplifications that are currently intentional.

## Phase 3 Implementation Decision

Implemented in V3 Phase 3:

- Added `MelonPrimeHudGeometry.h` as a Qt-free pure helper owner.
- Runtime `ApplyAnchor()`, `CalcAlignedTextX()`, and `CalcGaugePos()` now delegate to the shared helper.
- Settings dialog preview `dsPos()`, `alignedX()`, and gauge alignment now delegate to the same helper while preserving the dialog preview's original integer rounding behavior.
- In-game edit preview inherits the shared behavior through the runtime helper wrappers it already calls.

Deferred:

- Moving runtime `CachedHudConfig` into a public/shared preview snapshot. The type is currently internal to the render unity translation unit and tied to cache invalidation, dirty rects, font cache generation, and crosshair precomputed rects. Exposing it for dialog previews would be a higher-risk architectural move than the current Phase 3 target.
- Replacing the dialog previews' simplified drawing with runtime draw calls. Phase 3a identified several intentional preview-only simplifications; changing them would be a visual/UX change rather than a refactor.
