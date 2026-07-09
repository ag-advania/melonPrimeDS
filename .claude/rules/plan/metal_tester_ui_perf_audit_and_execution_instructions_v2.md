# MelonPrimeDS Metal — Tester UI Exposure / Performance Audit / Execution Instructions

Date: 2026-07-09  
Target branch: `highres_fonts_v3`  
Repo: `ag-advania/melonPrimeDS`  
Current inspected HEAD: `358ac689e44a31ea868e6770595f0b30bc750472`

---

## 0. Audit result

The branch visible from GitHub currently resolves to `358ac689e44a31ea868e6770595f0b30bc750472`.
A compare from the previous inspected HEAD to `highres_fonts_v3` is currently identical. If a newer push was expected, first confirm that it was pushed to `ag-advania/melonPrimeDS` / `highres_fonts_v3` and not to another branch or fork.

Current HEAD contains the Phase 8 Metal texture wrap/mirror/clamp work. It does **not** yet contain the requested tester-facing Metal UI exposure.

Current state:

- Standard `VideoSettingsDialog` still registers only `Software`, `OpenGL`, and `OpenGL Compute` renderer radio buttons.
- MelonPrime VIDEO QUALITY still has only `Low`, `High`, and `High2` slots.
- No `rb3DMetal`, `on_metroidSetVideoQualityToMetal_clicked()`, `Metal Test`, `MELONPRIME_METAL_PERF`, or macOS `.command` helper was found in the visible HEAD.
- Metal build gating exists and should be reused:
  - `MELONPRIME_ENABLE_METAL=OFF` by default.
  - `MELONPRIME_FORCE_DISABLE_METAL=ON` must override everything.
- `renderer3D_Metal` already routes presentation to `PresentationBackend::Metal` when compiled and normalized.
- Feature probing has been strengthened enough to test texture-array sampling.

---

## 1. Revised product decision

Previous guidance intentionally delayed UI exposure until visual parity was ready. That is no longer the desired workflow.

New direction:

> Expose Metal early as a tester-only, explicitly experimental option so the maintainer/tester can select it from UI and run real ROM tests without environment variables.

This is **not** a public-stability declaration.

The UI must communicate that Metal is experimental and may be incomplete, but it must be selectable in tester builds.

---

## 2. Hard rules

Do not violate these:

1. **Do not remap `High2` to Metal.**
   - `High2` remains OpenGL Compute.
   - On macOS, `High2` remains disabled because macOS OpenGL cannot run the compute renderer reliably.

2. **Do not replace OpenGL with Metal internally.**
   - Keep `renderer3D_OpenGL` and `renderer3D_OpenGLCompute` intact.
   - Add/use `renderer3D_Metal` as a separate renderer.

3. **Do not expose Metal in non-Metal builds.**
   - If `MELONPRIME_ENABLE_METAL=OFF`, no Metal UI, no Metal source, no Metal strings, no Metal framework linkage.
   - If `MELONPRIME_FORCE_DISABLE_METAL=ON`, same result even if `MELONPRIME_ENABLE_METAL=ON`.

4. **Do not market current Metal as High2/Compute-equivalent performance yet.**
   - It is a native Metal renderer scaffold with increasing native 3D coverage.
   - It is not yet proven faster than OpenGL/Software/High2 for real gameplay.
   - It is not a compute-shader renderer equivalent.

5. **Tester UI is allowed before Apple Silicon verification.**
   - Public/shipped exposure should still require Apple Silicon testing.
   - For this branch/test build, expose it clearly as `Experimental` / `Test`.

---

## 3. Performance audit: is Metal fast like Compute Shader yet?

Current answer: **No, not yet. Do not claim that.**

Metal can become the correct high-performance path for macOS, especially because macOS OpenGL Compute is not viable. However, current Phase 8 still has major performance blockers:

- The renderer still reports a `software raster delegate` in the Metal renderer init path.
- The native Metal pass is still not fully wired to final display output / visual parity.
- `GetLine()` / final composite / full display integration are still incomplete.
- Current native pass allocates per-frame/per-group shared buffers in the render path.
- The render path currently waits for command buffer completion, which can serialize CPU/GPU work and kill throughput if left in gameplay path.
- Translucency, shadows, fog, edge marking, line polygons, final pass, and other GLRenderer3D-equivalent paths remain incomplete.

Required stance:

```text
Metal is currently an experimental macOS native renderer path.
It is expected to become the macOS-friendly high-performance path, but it is not yet equivalent to OpenGL Compute/High2 and must be measured.
```

Before making any performance claim, add measurement and compare:

- Software renderer FPS / frame time.
- OpenGL renderer FPS / frame time.
- Metal experimental renderer FPS / frame time.
- CPU usage.
- GPU wait time.
- texture cache update cost.
- vertex/index upload bytes per frame.
- draw group count.
- command buffer wait time.

---

## 4. Required implementation — tester UI exposure

### 4.1 Standard Video Settings dialog

Add a new renderer radio button:

```text
Metal (Experimental)
```

Implementation requirements:

- Add `rb3DMetal` to `VideoSettingsDialog.ui` only under the Metal-capable UI layout.
- Register it in `VideoSettingsDialog.cpp` under the same compile gates:

```cpp
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    grp3DRenderer->addButton(ui->rb3DMetal, renderer3D_Metal);
#endif
```

- Runtime gate it with `MelonPrime::Metal::SupportsRequiredBaseline()`:

```cpp
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    if (!MelonPrime::Metal::SupportsRequiredBaseline()) {
        ui->rb3DMetal->setEnabled(false);
        ui->rb3DMetal->setToolTip(QString::fromStdString(
            MelonPrime::Metal::CachedFeatureInfo().unavailableReason));
    }
#endif
```

- If the feature probe passes, Metal must be selectable without env vars.
- Selecting it must set:

```cpp
cfg.SetInt("3D.Renderer", renderer3D_Metal);
```

- Keep the current null-check for `grp3DRenderer->button(oldRenderer)`.
- Update `setEnabled()` so Metal does not accidentally enable OpenGL-only controls.

Suggested logic:

```cpp
const bool softwareRenderer = renderer == renderer3D_Software;
const bool openGLRenderer = renderer == renderer3D_OpenGL;
const bool computeRenderer = renderer == renderer3D_OpenGLCompute;
const bool metalRenderer = renderer == renderer3D_Metal;

ui->cbGLDisplay->setEnabled(softwareRenderer);
ui->cbSoftwareThreaded->setEnabled(softwareRenderer);

// Do not show OpenGL scale controls as if they were Metal controls unless
// Metal explicitly reuses that key and the UI label says so.
ui->cbxGLResolution->setEnabled(openGLRenderer || computeRenderer);
ui->cbBetterPolygons->setEnabled(openGLRenderer);
ui->cbxComputeHiResCoords->setEnabled(computeRenderer);
```

If you temporarily reuse `3D.GL.ScaleFactor` for Metal scale, rename/tooltip the control in Metal mode or add a separate `3D.Metal.ScaleFactor`. Do not silently imply that an OpenGL-only option is a Metal option.

### 4.2 MelonPrime Settings VIDEO QUALITY

Add a separate button/preset:

```text
Metal Test
```

or

```text
Metal (Experimental)
```

Do not rename or repurpose `High2`.

Add slot:

```cpp
void MelonPrimeInputConfig::on_metroidSetVideoQualityToMetal_clicked()
{
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    if (!MelonPrime::Metal::SupportsRequiredBaseline())
        return;

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.UseGL", false);
    cfg.SetBool("Screen.VSync", false);
    cfg.SetInt("Screen.VSyncInterval", 1);
    cfg.SetInt("3D.Renderer", renderer3D_Metal);

    // Optional temporary sharing if current RenderSettings still reads this.
    // Prefer a future Metal-specific key if scale becomes user-facing.
    cfg.SetInt("3D.GL.ScaleFactor", 4);
    cfg.SetBool("3D.GL.BetterPolygons", false);
#endif
}
```

Also:

- Disable/hide the button when `MELONPRIME_ENABLE_METAL` is not compiled.
- Disable with tooltip when runtime probe fails.
- Tooltip must say this is experimental and not equivalent to High2 yet.

Suggested tooltip:

```text
Experimental native Metal renderer for macOS testing. Not High2/OpenGL Compute. Some 3D effects and performance work are still incomplete.
```

---

## 5. Required implementation — performance instrumentation

Add a low-noise perf mode:

```text
MELONPRIME_METAL_PERF=1
```

When enabled, log aggregated data every 300 or 600 frames, not every frame.

Minimum metrics:

```text
[MelonPrime] metal perf: frames=600 avgFrameMs=... avg3dMs=... avgTexcacheMs=... avgUploadBytes=... avgGroups=... avgDraws=... avgWaitMs=... cpuRendererFallback=...
```

Measure separately:

- `MetalRenderer3D::RenderFrame()` total time.
- `RenderNativeOpaquePolygons()` time.
- `Texcache->Update()` time.
- vertex/index buffer allocation/upload byte count.
- number of considered polygons.
- number of textured polygons.
- draw groups.
- command buffer wait time.

Important:

- Keep perf logging compiled only in Metal-active builds or behind `MELONPRIME_DS` / `MELONPRIME_ENABLE_METAL`.
- Do not leave spam logs on by default.
- Do not use perf logs to claim speed until visible output is actually Metal-rendered and comparable.

### 5.1 Immediate perf red flags to track

The current path uses `waitUntilCompleted()` after committing the command buffer. This is acceptable for early correctness testing, but it can destroy async GPU throughput.

Do not remove it blindly yet. Instead:

1. Measure `avgWaitMs` with `MELONPRIME_METAL_PERF=1`.
2. Only later replace it with a multi-buffered / deferred readback-safe path once display integration requires it.

Current per-frame `newBufferWithBytes` calls are also acceptable for early testing but not final performance. Log bytes and count first.

---

## 6. Required implementation — macOS build `.command` files

Add command files so the tester can build/run without remembering CMake flags.

Recommended location:

```text
tools/macos/build_metal_test.command
```

and optionally:

```text
tools/macos/run_metal_test.command
```

Make them executable:

```bash
chmod +x tools/macos/build_metal_test.command
chmod +x tools/macos/run_metal_test.command
```

### 6.1 `build_metal_test.command`

Create this file:

```bash
#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
REPO_ROOT="${SCRIPT_DIR:h:h}"
cd "$REPO_ROOT"

BUILD_DIR="build-mac-metal-test"

cmake -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMELONPRIME_ENABLE_METAL=ON \
  -DMELONPRIME_FORCE_DISABLE_METAL=OFF

cmake --build "$BUILD_DIR" --parallel "$(sysctl -n hw.ncpu)"

echo ""
echo "Metal test build complete."
echo "Build dir: $REPO_ROOT/$BUILD_DIR"
echo ""
```

If the project already requires extra Qt/CMake/vcpkg flags on macOS, preserve the existing documented macOS build flags and append the Metal flags above.

### 6.2 `run_metal_test.command`

Create this optional runner:

```bash
#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
REPO_ROOT="${SCRIPT_DIR:h:h}"
cd "$REPO_ROOT"

APP="$REPO_ROOT/build-mac-metal-test/src/frontend/qt_sdl/melonDS.app/Contents/MacOS/melonDS"

if [[ ! -x "$APP" ]]; then
  echo "Metal test binary not found: $APP"
  echo "Run tools/macos/build_metal_test.command first."
  exit 1
fi

export MELONPRIME_METAL_PERF="${MELONPRIME_METAL_PERF:-1}"

# Do not force renderer/presenter by default anymore; the point of this phase
# is to test the UI-selectable Metal option. Developers can still opt in:
# export MELONPRIME_FORCE_METAL_RENDERER=1
# export MELONPRIME_FORCE_METAL_PRESENTER=1

exec "$APP" "$@"
```

Acceptance:

- Double-clicking `build_metal_test.command` or running it from Terminal builds a Metal-enabled app.
- `MELONPRIME_FORCE_DISABLE_METAL=ON` builds must still remove Metal entirely.
- The runner must not force Metal by default once UI exposure is implemented; it should let the tester select Metal from UI.

---

## 7. Acceptance checklist

### Build / gating

- [ ] Default macOS build with `MELONPRIME_ENABLE_METAL=OFF` has no Metal UI and no Metal strings.
- [ ] `MELONPRIME_FORCE_DISABLE_METAL=ON` removes all Metal UI/source/framework paths even if `MELONPRIME_ENABLE_METAL=ON`.
- [ ] `MELONPRIME_ENABLE_METAL=ON` builds cleanly on Intel Mac.
- [ ] `tools/macos/build_metal_test.command` builds successfully.

### UI

- [ ] Standard Video Settings shows `Metal (Experimental)` in Metal-enabled macOS build.
- [ ] MelonPrime VIDEO QUALITY shows `Metal Test` or `Metal (Experimental)` in Metal-enabled macOS build.
- [ ] Selecting Metal sets `3D.Renderer = renderer3D_Metal`.
- [ ] Selecting Metal does not require `MELONPRIME_FORCE_METAL_RENDERER` or `MELONPRIME_FORCE_METAL_PRESENTER`.
- [ ] `High2` remains OpenGL Compute and remains disabled on macOS.
- [ ] OpenGL-only controls are disabled or relabeled when Metal is selected.
- [ ] If Metal probe fails, Metal UI is disabled with an explanatory tooltip.

### Runtime

- [ ] App launches with Metal build.
- [ ] User can select Metal from UI and restart/apply settings as needed.
- [ ] Real MPH ROM boots with Metal selected.
- [ ] Logs confirm `renderer3D_Metal` and `PresentationBackend::Metal` are active without env forcing.
- [ ] No crash during 5+ minutes of boot/title/gameplay smoke.
- [ ] `MELONPRIME_METAL_PERF=1` logs aggregated performance data.

### Performance honesty

- [ ] Do not claim Metal is faster than High2/Compute yet.
- [ ] Do not call it compute-shader-equivalent.
- [ ] Report actual measured frame time / CPU / wait time.
- [ ] Keep `Metal (Experimental)` wording until visual parity and performance are proven.

---

## 8. Suggested commit sequence

Keep this split so review stays easy:

1. `Expose experimental Metal renderer in Video Settings`
   - Add `rb3DMetal`.
   - Register in button group.
   - Runtime gate by `SupportsRequiredBaseline()`.
   - Fix control enable logic for Metal.

2. `Add MelonPrime Metal Test video preset`
   - Add MelonPrime VIDEO QUALITY button.
   - Add slot setting `renderer3D_Metal`.
   - Add tooltip and runtime gate.
   - Keep High2 unchanged.

3. `Add Metal perf logging for tester builds`
   - Add `MELONPRIME_METAL_PERF=1` aggregation.
   - Log no more than every 300/600 frames.

4. `Add macOS Metal test command scripts`
   - Add `tools/macos/build_metal_test.command`.
   - Optionally add `tools/macos/run_metal_test.command`.
   - Ensure executable bit is set if possible.

5. `Document experimental Metal tester workflow`
   - Update `.claude/rules/melonprime-metal-backend-plan.md`.
   - Mention UI is tester exposure, not public stability.

---

## 9. Final note for implementer

The maintainer explicitly wants to test this from the UI now. Do not block tester UI exposure on full Phase 8 parity or Apple Silicon. Instead, expose it clearly as experimental, keep all default/force-disable gates safe, add perf logging, and provide a one-click macOS build command.

