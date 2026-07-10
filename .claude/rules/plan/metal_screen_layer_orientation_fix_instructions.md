# Metal Screen Layer / Orientation Fix Instructions

Date: 2026-07-10 JST  
Repo/branch: `ag-advania/melonPrimeDS` / `highres_fonts_v3`  
Current visible HEAD: `2bf6003d0b370fa61550340f45f39704d673b10f`  
Commit title: `上下反転 trying to fix`

## 1. User-reported current behavior

Latest visual report:

```text
Before the latest fix:
- Top screen and bottom screen were each vertically flipped.

After the latest fix:
- The 3D image's vertical inversion appears fixed.
- But the top and bottom screens now appear swapped / physically misplaced.
```

This strongly suggests that two separate coordinate problems were fixed with one global change:

1. **Texture-content orientation**: whether a sampled image is upside down inside its screen rectangle.
2. **Screen-rectangle placement**: whether the top/bottom physical DS screen rectangles land in the correct window locations.

The latest change removed `p.y *= -1.0` from both the Metal screen shader and the UI shader. That can fix one texture-orientation issue, but it also changes the physical placement of every transformed screen rectangle. Do not keep treating those as the same problem.

## 2. Current progress summary

The Metal path has progressed substantially since the previous black/flicker audit:

- `RendererOutput` now carries a Metal-specific `FrameSerial` when Metal output exists.
- `GPU2D_Metal.{h,mm}` has been added as a Metal 2D scaffold / mirror path.
- `GPU_Metal.mm` now has Phase 2/3 GetLine integration and Phase 4 scaffolding.
- Current normal `MetalRenderer::GetOutput()` returns `SoftRenderer::GetOutput()`, meaning the visible normal frame is still the completed CPU BGRA frame produced after the Metal 3D GetLine path is composed through the existing soft 2D path.
- Metal final texture / native 3D visibility diagnostics exist, but full hires Metal 2D/final output is still not complete.

Do not treat this as a completed Compute Renderer replacement yet. It is now closer to a correct staged path:

```text
Current safe visible path:
Metal 3D target -> Metal GetLine/readback -> existing Soft 2D compositor -> CPU BGRA -> Metal presenter

Future hires path:
Metal 3D target + Metal 2D renderer -> Metal final 2-layer texture -> Metal presenter
```

## 3. Main diagnosis

The latest commit changed only the Metal presenter shader's Y mapping:

```cpp
// removed from mp_screen_vs
p.y *= -1.0;

// removed from mp_ui_vs
p.y *= -1.0;
```

That is too broad.

The presenter shader is responsible for placing already-composed DS screen textures into the app window using `screenMatrix[i]`. It should not be used as the primary place to fix native 3D texture orientation unless the whole presenter coordinate convention has been verified against `ScreenPanelGL` / `Screen.cpp`.

The current symptom indicates:

```text
3D texture orientation: probably improved
Physical top/bottom screen placement: likely regressed
```

Therefore, split the fix into three independent axes:

```text
A. Physical screen rectangle placement
B. Texture layer selection / top-bottom logical routing
C. Source texture orientation inside the rectangle
```

## 4. Required fix strategy

### 4.1 Revert the broad presenter Y-flip removal first

Restore the Metal screen presenter transform to match the established `ScreenPanelGL` behavior unless diagnostics prove the whole app-wide screen coordinate convention is different.

Target for `mp_screen_vs`:

```cpp
p = ((p * 2.0) / u.screenSize) - 1.0;
p.y *= -1.0;
```

Target for `mp_ui_vs`:

```cpp
p = ((p * 2.0) / u.screenSize) - 1.0;
p.y *= -1.0;
```

Reason:

- `screenMatrix[i]` is already computed by shared frontend layout code.
- Existing GL/native presenters expect that convention.
- Removing the flip from the global presenter transform changes physical screen placement, not just image orientation.
- UI/HUD/OSD placement should not be tied to a 3D texture orientation bug.

If after reverting this the old “each screen image is vertically flipped” returns, do **not** remove the presenter flip again. Fix the texture source orientation at the source-specific layer.

### 4.2 Diagnose layer routing before further changes

Use the existing checker-layer diagnostic before changing any `screenKind`, `ScreenSwap`, or layer assignment logic.

Run:

```sh
MELONPRIME_METAL_DIAG_FINAL_LAYERS=1 \
MELONPRIME_METAL_DIAG=1 \
tools/macos/run_metal_test.command /path/to/rom.nds
```

Expected diagnostic intent:

```text
layer0 = red/green checker
layer1 = blue/yellow checker
```

Record:

```text
screenKind[0]=?
screenKind[1]=?
numScreens=?
Which checker appears in the physical top screen?
Which checker appears in the physical bottom screen?
```

Acceptance:

```text
Physical top screen must show the same logical layer as ScreenPanelGL does.
Physical bottom screen must show the same logical layer as ScreenPanelGL does.
```

Do not guess. Use the checker.

### 4.3 Keep `screenKind` semantics byte-compatible with ScreenPanelGL

The Metal presenter currently uses:

```cpp
[encoder drawPrimitives:MTLPrimitiveTypeTriangle
             vertexStart:(screenKind[i] == 0 ? 0 : 6)
             vertexCount:6];
```

This means:

```text
screenKind == 0 -> vertices [0,6) -> texture array layer 0
screenKind != 0 -> vertices [6,12) -> texture array layer 1
```

Do not “fix” top/bottom swapping by randomly inverting this expression.

Only change it if the checker diagnostic proves that Metal's interpretation of layer 0/1 is opposite to ScreenPanelGL. If that happens, add a clearly named compatibility helper instead of inline inversion:

```cpp
static int MetalScreenLayerForScreenKind(int screenKind) noexcept
{
    // Must match ScreenPanelGL / kScreenVS/kScreenFS semantics.
    return screenKind == 0 ? 0 : 1;
}
```

Then use:

```cpp
const int layer = MetalScreenLayerForScreenKind(screenKind[i]);
const int vertexStart = layer == 0 ? 0 : 6;
```

If an inversion is truly required, document exactly why with the checker result and GL comparison.

### 4.4 Fix 3D image orientation at the 3D source boundary

If restoring the presenter flip makes the 3D content upside-down again, fix **only the 3D source**.

Valid places to fix 3D source orientation:

1. `MetalRenderer3D` native target generation / readback / GetLine path.
2. The Metal final pass when sampling the native 3D target.
3. A source-specific texcoord transform used only for native 3D textures.

Invalid fix:

```text
Changing the global screen presenter transform used by both top and bottom screens, CPU fallback, UI overlay, and OSD.
```

For the current Phase 2/3 visible path, the likely correct place is the Metal 3D GetLine/readback path, because the normal visible output is still `SoftRenderer::GetOutput()` after soft 2D composition.

The GetLine contract is line-oriented and expected to match software/OpenGL compositor semantics. If Metal 3D pixels are vertically inverted before entering that contract, the readback/writeout should invert row order there, not in the final app presenter.

### 4.5 Keep UI overlay orientation tied to frontend screen coordinates

Do not let the 3D texture orientation dictate UI shader orientation.

The UI shader should follow the same coordinate convention as the existing OSD/HUD overlay path. If HUD/OSD is misplaced after the latest commit, restore the UI `p.y *= -1.0` immediately.

Required check:

```text
OSD top-left margin appears top-left.
Custom HUD edit handles align with the displayed DS screen.
Touch/HUD bottom-image sampling still matches the physical bottom screen.
```

## 5. Specific code actions

### 5.1 `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`

Restore `p.y *= -1.0` in `mp_screen_vs`.

```cpp
"    p = ((p * 2.0) / u.screenSize) - 1.0;\n"
"    p.y *= -1.0;\n"
```

Restore `p.y *= -1.0` in `mp_ui_vs`.

```cpp
"    p = ((p * 2.0) / u.screenSize) - 1.0;\n"
"    p.y *= -1.0;\n"
```

Replace the current comment that claims Metal NDC Y already points down with a narrower comment:

```cpp
// Match ScreenPanelGL's screen-space-to-clip transform. The shared
// screenMatrix[] values are frontend pixel-space transforms; the presenter
// shader must preserve the same physical top/bottom screen placement as
// kScreenVS/kScreenFS. Source-specific upside-down textures must be fixed at
// their source/readback/final-pass boundary, not by changing the global
// presenter transform.
```

### 5.2 Add a one-shot placement diagnostic

In `ScreenPanelMetal::drawScreen()`, after receiving a valid output and before draw, add a low-noise log when diagnostics are enabled:

```cpp
if (MetalDiagEnabled() && !m->loggedScreenPlacementDiag)
{
    m->loggedScreenPlacementDiag = true;
    fprintf(stderr,
        "[MelonPrime] metal presenter placement: numScreens=%d "
        "screenKind0=%d screenKind1=%d "
        "matrix0=[%.1f %.1f %.1f %.1f %.1f %.1f] "
        "matrix1=[%.1f %.1f %.1f %.1f %.1f %.1f]\n",
        numScreens,
        numScreens > 0 ? screenKind[0] : -1,
        numScreens > 1 ? screenKind[1] : -1,
        numScreens > 0 ? screenMatrix[0][0] : 0.0f,
        numScreens > 0 ? screenMatrix[0][1] : 0.0f,
        numScreens > 0 ? screenMatrix[0][2] : 0.0f,
        numScreens > 0 ? screenMatrix[0][3] : 0.0f,
        numScreens > 0 ? screenMatrix[0][4] : 0.0f,
        numScreens > 0 ? screenMatrix[0][5] : 0.0f,
        numScreens > 1 ? screenMatrix[1][0] : 0.0f,
        numScreens > 1 ? screenMatrix[1][1] : 0.0f,
        numScreens > 1 ? screenMatrix[1][2] : 0.0f,
        numScreens > 1 ? screenMatrix[1][3] : 0.0f,
        numScreens > 1 ? screenMatrix[1][4] : 0.0f,
        numScreens > 1 ? screenMatrix[1][5] : 0.0f);
}
```

Add the field:

```cpp
bool loggedScreenPlacementDiag = false;
```

### 5.3 Add a 3D-source orientation diagnostic

In the Metal 3D GetLine/readback path, add a temporary diagnostic under `MELONPRIME_METAL_DIAG=1`:

```text
[MelonPrime] metal 3d orientation: topRowNonzero=<n> bottomRowNonzero=<n> source=<native/readback/getline>
```

Purpose:

- If the 3D source is upside-down, top/bottom row distribution will not match software reference during comparable scenes.
- This lets us fix readback row order without touching presenter layout.

If `MELONPRIME_METAL_GETLINE_DIFF=1` already compares soft/native lines, extend that diagnostic to explicitly report whether the mismatch looks like a vertical row reversal.

## 6. Test matrix

### 6.1 Baseline build

```sh
tools/macos/build_metal_test.command
cmake --build build-mac --parallel "$(sysctl -n hw.ncpu)"
```

### 6.2 Layer placement diagnostic

```sh
MELONPRIME_METAL_DIAG=1 \
MELONPRIME_METAL_DIAG_FINAL_LAYERS=1 \
tools/macos/run_metal_test.command /path/to/rom.nds
```

Expected:

```text
- Top physical screen shows the intended top logical layer.
- Bottom physical screen shows the intended bottom logical layer.
- OSD/HUD are not vertically mirrored.
```

### 6.3 Normal visible path

```sh
tools/macos/run_metal_test.command /path/to/rom.nds
```

Expected:

```text
- Top and bottom screens are physically in the correct positions.
- 3D content is not upside-down.
- Bottom/touch screen is not swapped with top screen.
- OSD and Custom HUD placement remain correct.
```

### 6.4 Native 3D bring-up diagnostic

```sh
MELONPRIME_METAL_DIAG=1 \
MELONPRIME_METAL_NATIVE_3D_VISIBLE=1 \
tools/macos/run_metal_test.command /path/to/rom.nds
```

Expected:

```text
- Native 3D layer appears only in the layer reported by routing diagnostics.
- If native 3D orientation is wrong, fix native source/readback/final-pass sampling, not global presenter placement.
```

### 6.5 Software/OpenGL comparison

Run the same scene in OpenGL/Compute and Metal, then compare:

```text
- Physical top/bottom screen location
- OSD/HUD location
- Touch/bottom-screen location
- 3D orientation
- ScreenSwap behavior across title/menu/gameplay
```

## 7. Acceptance criteria

This fix is accepted only when all are true:

```text
- Top physical screen and bottom physical screen are not swapped.
- Neither screen is vertically flipped as a whole.
- 3D content is not vertically flipped.
- OSD/HUD/UI overlay positions match existing GL/native behavior.
- layer diagnostic proves layer0/layer1 routing instead of relying on guessing.
- Any 3D-only orientation correction is isolated to 3D source/readback/final-pass sampling.
- No global presenter transform is changed merely to fix 3D texture orientation.
```

## 8. Do not do

```text
- Do not keep removing `p.y *= -1.0` globally just because it fixes 3D orientation.
- Do not invert `screenKind` blindly.
- Do not swap layer0/layer1 without checker diagnostics.
- Do not tie UI overlay orientation to native 3D orientation.
- Do not claim hires/Compute replacement is complete while normal `GetOutput()` still returns `SoftRenderer::GetOutput()`.
- Do not hide CPU-composited output behind misleading `MetalFinalTexture` labels.
```

## 9. Recommended next implementation order

```text
1. Restore presenter and UI Y flip to GL-compatible transform.
2. Run final-layer checker diagnostic.
3. Confirm physical screen placement and layer mapping.
4. If 3D is upside-down, fix Metal 3D GetLine/readback row order or native 3D sampling only.
5. Verify OSD/HUD/touch alignment.
6. Only after this is stable, continue Phase 4 Metal 2D/hires final texture work.
```

## 10. Progress log

### 2026-07-10 JST Phase 1 — presenter/UI Y flip restored

Status: implemented.

- Restored `p.y *= -1.0` in `mp_screen_vs` so Metal presenter placement follows the existing `ScreenPanelGL` screen-space-to-clip convention.
- Restored `p.y *= -1.0` in `mp_ui_vs` so OSD/HUD overlay placement stays tied to frontend screen coordinates, not native 3D texture orientation.
- Replaced the broad Metal-NDC comment with the source-boundary rule from this plan: physical presenter placement stays global/GL-compatible; upside-down source textures must be fixed at readback/final-pass/source sampling boundaries.
- Verified with `tools/macos/build_metal_test.command`.
- Verified with `cmake --build build-mac --parallel "$(sysctl -n hw.ncpu)"`.

### 2026-07-10 JST Phase 2 — presenter placement diagnostic

Status: implemented.

- Added a local `MELONPRIME_METAL_DIAG=1` gate in `ScreenPanelMetal` for presenter-only diagnostics.
- Added one-shot placement logging after a valid renderer output is available and before drawable acquisition/draw.
- The log records `numScreens`, `screenKind[0..1]`, and both frontend `screenMatrix[]` affine transforms so checker-layer results can be tied to physical top/bottom placement without guessing or changing `screenKind` semantics.
- Verified with `git diff --check`.
- Verified with `tools/macos/build_metal_test.command`.
- Verified with `cmake --build build-mac --parallel "$(sysctl -n hw.ncpu)"` (`ninja: no work to do` for the default build tree).

### 2026-07-10 JST Phase 3 — Metal 3D source orientation diagnostic

Status: implemented.

- Added a `MELONPRIME_METAL_DIAG=1` diagnostic after native Metal color-target readback into `NativeLineBuffer`.
- The diagnostic reports `topRowNonzero` and `bottomRowNonzero` from the GetLine-bound line buffer with `source=native/readback/getline`, keeping the signal at the 3D source boundary instead of the global presenter.
- Extended `MELONPRIME_METAL_GETLINE_DIFF=1` to also compare each software line against the vertically reversed native line and report `verticalReverseDiffPixels` plus a `verticalReverseCandidate` flag.
- Verified with `git diff --check`.
- Verified with `tools/macos/build_metal_test.command`.
- Verified with `cmake --build build-mac --parallel "$(sysctl -n hw.ncpu)"` (`ninja: no work to do` for the default build tree).

### 2026-07-10 JST Phase 4 — available diagnostics / row-order assessment

Status: completed with ROM-limited runtime coverage.

- Checked the workspace for `.nds` / `.srl` files; none are present, so the required checker-layer visual run cannot be completed in this environment.
- Confirmed the Metal test binary contains the new diagnostics: `metal presenter placement`, `metal 3d orientation`, and `verticalReverseCandidate`.
- Confirmed the presenter still uses the existing `vertexStart:(screenKind[i] == 0 ? 0 : 6)` mapping; no blind layer inversion was made.
- Result: there is not enough runtime evidence in this workspace to apply a 3D row-order inversion. Per the plan, the next code change must wait for checker/diff evidence instead of guessing.
