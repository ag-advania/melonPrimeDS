# MelonPrime Metal Tester UI / Internal Resolution Audit and Next Instructions

Date: 2026-07-09 JST  
Repo/branch: `ag-advania/melonPrimeDS` / `highres_fonts_v3`  
Audited visible HEAD: `def57afa72adfb8845ba98bb65393db1f6c2fd0d`  
Previous audit base: `358ac689e44a31ea868e6770595f0b30bc750472`

## 1. Audit summary

The latest visible branch is **5 commits ahead** of the previous audit point. The pushed work covers:

- Standard Video Settings experimental Metal radio button.
- MelonPrime Settings `Video quality: Metal Test` button.
- `MELONPRIME_METAL_PERF=1` aggregate performance logging.
- macOS `.command` helpers for build/run.
- Metal backend documentation updates.

Overall verdict: **good tester-exposure direction**. The implementation now gives testers a UI path to select Metal without needing `MELONPRIME_FORCE_METAL_RENDERER=1` / `MELONPRIME_FORCE_METAL_PRESENTER=1`. Keep treating this as tester-only, not public-stable.

## 2. What is now correctly implemented

### 2.1 Standard Video Settings Metal option

`VideoSettingsDialog.cpp` dynamically creates `rb3DMetal` only in `MELONPRIME_DS && MELONPRIME_ENABLE_METAL` builds.

Current behavior is correct:

- The label is `Metal (Experimental)`.
- The button is registered as `renderer3D_Metal`.
- The button is gated by `MelonPrime::Metal::SupportsRequiredBaseline()`.
- If unsupported, it is disabled and shows the probe failure reason.
- `High2` / OpenGL Compute remains separate.
- The button is dynamic, not baked into `VideoSettingsDialog.ui`, so default/force-disabled builds should not accidentally gain Metal UI strings.

### 2.2 MelonPrime Settings Metal preset

`MelonPrimeInputConfig.cpp` now creates a dynamic `Video quality: Metal Test` button in macOS + Metal-enabled builds.

The slot does the important things correctly:

```cpp
cfg.SetBool("Screen.UseGL", false);
cfg.SetBool("Screen.VSync", false);
cfg.SetInt("Screen.VSyncInterval", 1);
cfg.SetInt("3D.Renderer", renderer3D_Metal);
cfg.SetBool("3D.Soft.Threaded", true);
cfg.SetInt("3D.GL.ScaleFactor", 4);
cfg.SetBool("3D.GL.BetterPolygons", false);
```

This preserves the critical rule: **Metal is not High2, and High2 is not redirected to Metal.**

### 2.3 Perf logging

`GPU3D_Metal.mm` now has `MELONPRIME_METAL_PERF=1` aggregate logging every 600 frames.

It tracks:

- average total frame time,
- native Metal 3D time,
- texcache time,
- command-buffer wait time,
- upload bytes,
- draw groups,
- draw calls,
- considered/textured polygon counts,
- whether the CPU/software fallback path is still active.

This is the right stance. Do **not** claim Metal is fast until the perf logs prove it under real ROM gameplay.

### 2.4 macOS command helpers

The following files now exist:

- `tools/macos/build_metal_test.command`
- `tools/macos/run_metal_test.command`

The build helper configures `build-mac-metal-test` with:

```sh
-DMELONPRIME_ENABLE_METAL=ON
-DMELONPRIME_FORCE_DISABLE_METAL=OFF
```

The run helper enables `MELONPRIME_METAL_PERF=1` by default and intentionally does not force Metal via env vars, which is correct for exercising the UI selection path.

## 3. Important current limitation: Metal internal resolution is not exposed in UI yet

The user wants to test Metal with selectable internal resolution.

Current state:

- `VideoSettingsDialog::setEnabled()` enables the resolution combo only for OpenGL/OpenGL Compute:

```cpp
ui->cbxGLResolution->setEnabled(openGLRenderer || computeRenderer);
```

- Metal is therefore selectable, but the internal resolution combo is disabled while Metal is selected.
- `Metal Test` preset already writes `3D.GL.ScaleFactor = 4`, so Metal can receive a scale factor indirectly.
- `MetalRenderer3D::ResizeTargets()` already allocates targets using `256 * ScaleFactor` and `192 * ScaleFactor`.
- `RenderNativeOpaquePolygons()` still has comments saying hi-res scale is not implemented / always native. The actual render-target allocation is scaled, but this must be verified before claiming proper high-resolution rendering.

Conclusion: **next task should be Metal internal-resolution UI exposure + scale correctness verification.**

## 4. Performance stance: do not call this Compute-Shader-fast yet

Metal is expected to become the macOS-friendly high-performance path, but the current implementation is not equivalent to High2/OpenGL Compute.

Reasons:

1. `MetalRenderer3D::RenderFrame()` still calls `Delegate.RenderFrame()` every frame.
2. `GetLine()` still returns `Delegate.GetLine(line)`, so final visible output is not native Metal 3D yet.
3. Native Metal opaque pass is still a side/shadow pass.
4. `ClearNativeTarget()` and `RenderNativeOpaquePolygons()` both use `waitUntilCompleted()`, which is a major synchronization point.
5. Per-frame vertex/index buffers are created with `newBufferWithBytes`, and one index buffer is created per draw group.
6. Several GL parity paths are still missing: translucent polygons, shadows, line polygons, depth-equal, BetterPolygons, toon/highlight substitution, edge marking, fog, final composite.

Therefore, UI labels and docs must stay honest:

- Good: `Metal (Experimental)`, `Video quality: Metal Test`.
- Bad: `High2 Metal`, `Compute Metal`, `Fast Metal`, `Metal High2`.

## 5. Next implementation instructions

### Goal

Allow testers to choose internal resolution while Metal is selected, without pretending parity/performance is complete.

### Required changes

#### 5.1 Enable the existing resolution combo for Metal

In `VideoSettingsDialog::setEnabled()`, change the resolution enable condition.

Current:

```cpp
ui->cbxGLResolution->setEnabled(openGLRenderer || computeRenderer);
```

Target:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_METAL)
const bool metalRenderer = renderer == renderer3D_Metal;
#else
const bool metalRenderer = false;
#endif

ui->cbxGLResolution->setEnabled(openGLRenderer || computeRenderer || metalRenderer);
```

Keep these unchanged:

```cpp
ui->cbBetterPolygons->setEnabled(openGLRenderer);
ui->cbxComputeHiResCoords->setEnabled(computeRenderer);
```

Do not enable BetterPolygons or Compute Hi-Res Coordinates for Metal yet.

#### 5.2 Improve the resolution label/tooltip wording

The combo object is named `cbxGLResolution`, but it is now shared by OpenGL/Compute/Metal. Avoid a larger UI refactor for now, but make the visible text honest.

Options:

- Keep existing item text if it already says `1x native`, `2x native`, etc.
- Add/update tooltip:

```text
Internal 3D render scale. Used by OpenGL/OpenGL Compute and experimental Metal. Metal scale support is still under test.
```

Do not rename config keys yet.

#### 5.3 Reuse `3D.GL.ScaleFactor` for now

For the tester phase, reuse the existing scale key:

```text
3D.GL.ScaleFactor
```

Reason: the renderer settings pipeline already passes this value into renderer settings. Creating a new `3D.Metal.ScaleFactor` now would require migration and more UI state logic. That can wait until Metal is stable.

Add a comment near the Metal UI code:

```cpp
// Tester-phase shortcut: Metal intentionally reuses 3D.GL.ScaleFactor
// as the shared hardware-renderer internal scale. Rename/split only after
// Metal becomes stable enough to need separate defaults/migration.
```

#### 5.4 Verify backend scale behavior

`ResizeTargets()` already uses:

```cpp
const NSUInteger width = static_cast<NSUInteger>(256 * ScaleFactor);
const NSUInteger height = static_cast<NSUInteger>(192 * ScaleFactor);
```

Add one low-noise log under `MELONPRIME_METAL_PERF=1` or first-resize only:

```text
[MelonPrime] metal renderer3D: target scale=<n> size=<w>x<h>
```

Expected sizes:

```text
1x = 256x192
2x = 512x384
3x = 768x576
4x = 1024x768
```

#### 5.5 Resolve the comment/behavior mismatch around hi-res scale

`RenderNativeOpaquePolygons()` currently documents hi-res scale as not implemented and says the pass only fills the native-resolution corner. That may be stale or at least too ambiguous, because the target allocation is scaled and the shader maps native DS coordinates to NDC.

Do not blindly delete the caveat. Instead verify and then update the comment.

Required verification:

1. Run Metal at 1x, 2x, and 4x.
2. Confirm `ResizeTargets()` logs the expected target size.
3. Confirm the native opaque pass still draws with no crash.
4. If a debug readback or visual hook is available, confirm geometry covers the full scaled target, not just a 256x192 corner.

After verification, update the comment to one of these:

If scale works:

```text
ScaleFactor controls render-target size. DS screen-space coordinates remain native 256x192 and are mapped to NDC, so rasterization covers the full scaled render target. Pixel-level visual parity is still pending because final Metal output is not yet displayed.
```

If scale does not work:

```text
ScaleFactor currently only allocates larger targets; native geometry/final output still need explicit scale integration before this behaves like real internal resolution.
```

#### 5.6 Add perf scale fields

Extend `MELONPRIME_METAL_PERF=1` logging with:

```text
scale=<n> target=<w>x<h>
```

This lets testers compare 1x/2x/4x without guessing what was actually active.

#### 5.7 Update MelonPrime Metal preset

Keep the Metal preset default at 4x for now:

```cpp
cfg.SetInt("3D.GL.ScaleFactor", 4);
```

But once the combo is enabled for Metal, testers can change it manually afterward.

Add tooltip text:

```text
Sets Metal renderer with 4x internal scale by default. You can change the internal resolution in Video Settings while Metal is selected.
```

#### 5.8 Update command files

Update `tools/macos/run_metal_test.command` so inherited force-env vars do not accidentally bypass the new UI path.

Target behavior:

```sh
export MELONPRIME_METAL_PERF="${MELONPRIME_METAL_PERF:-1}"

if [[ "${MELONPRIME_ALLOW_FORCE_METAL:-0}" != "1" ]]; then
  unset MELONPRIME_FORCE_METAL_RENDERER
  unset MELONPRIME_FORCE_METAL_PRESENTER
fi
```

Rationale: the command file should test the UI selection path by default. Developers can still force Metal with:

```sh
MELONPRIME_ALLOW_FORCE_METAL=1 \
MELONPRIME_FORCE_METAL_RENDERER=1 \
MELONPRIME_FORCE_METAL_PRESENTER=1 \
tools/macos/run_metal_test.command /path/to/rom.nds
```

Also verify the files are executable:

```sh
git update-index --chmod=+x tools/macos/build_metal_test.command tools/macos/run_metal_test.command
```

## 6. Required test matrix

### 6.1 Build checks

Run:

```sh
tools/macos/build_metal_test.command
cmake --build build-mac --parallel "$(sysctl -n hw.ncpu)"
```

Expected:

- Metal test build succeeds.
- Normal `build-mac` succeeds.
- Default/non-Metal binary still has no Metal UI strings if that audit is available.

### 6.2 UI checks

On macOS Metal-enabled build:

1. Open standard Video Settings.
2. Confirm `Metal (Experimental)` exists.
3. Select Metal.
4. Confirm internal resolution combo remains enabled.
5. Change 1x → 2x → 4x.
6. Confirm no crash and renderer settings update.

On default/non-Metal build:

1. Confirm no `Metal (Experimental)` radio.
2. Confirm no `Video quality: Metal Test` button.

### 6.3 MelonPrime Settings checks

1. Open MelonPrime Settings.
2. Confirm `Video quality: Metal Test` exists only on macOS Metal-enabled builds.
3. Click it.
4. Confirm config values:

```text
Screen.UseGL=false
Screen.VSync=false
3D.Renderer=renderer3D_Metal
3D.Soft.Threaded=true
3D.GL.ScaleFactor=4
3D.GL.BetterPolygons=false
```

5. Open standard Video Settings.
6. Confirm Metal is selected and resolution combo can be changed.

### 6.4 Runtime/perf checks

Run a ROM through:

```sh
tools/macos/run_metal_test.command /path/to/rom.nds
```

Then test 1x, 2x, 4x from the UI.

Expected perf log should include:

```text
scale=<n> target=<w>x<h>
avgFrameMs=...
avg3dMs=...
avgTexcacheMs=...
avgWaitMs=...
cpuRendererFallback=.../600
```

Do not compare Metal against High2 yet as a final performance claim. At this stage compare only as raw measurements.

## 7. Acceptance criteria for this next push

This task is complete when all of the following are true:

- `Metal (Experimental)` still appears only in Metal-enabled macOS builds.
- `Video quality: Metal Test` still appears only in Metal-enabled macOS builds.
- `High2` remains OpenGL Compute only and still disabled on macOS.
- Selecting Metal in standard Video Settings enables the internal resolution combo.
- Changing the combo updates `3D.GL.ScaleFactor` and reaches `MetalRenderer3D::SetScaleFactor()` / `ResizeTargets()`.
- `MELONPRIME_METAL_PERF=1` logs scale and target size.
- `run_metal_test.command` tests the UI path by default and does not accidentally inherit force-Metal env vars unless explicitly allowed.
- Normal build remains unaffected.
- Documentation says this is tester-only and experimental.

## 8. Do not do yet

Do not do these in the next push unless separately requested:

- Do not rename `3D.GL.ScaleFactor` globally.
- Do not introduce `3D.Metal.ScaleFactor` yet.
- Do not remove the Software delegate.
- Do not connect Metal output to final display unless there is a separate visual parity plan.
- Do not call Metal faster than OpenGL/High2 in docs or UI.
- Do not redirect High2 to Metal.
- Do not expose Metal as a stable public preset.

