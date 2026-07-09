# MelonPrime Metal Phase 8/9 Tester UI Exposure — Audit & Execution Instructions

Status: 2026-07-09  
Branch: `highres_fonts_v3`  
Audience: implementation team / execution team  
Goal: expose Metal quickly for tester use without corrupting the meaning of `High2`, while keeping the current kill-switch and default-build safety guarantees.

---

## 1. Executive decision

The next task is **not** to wait for full Phase 8 visual parity before any UI exists.

The next task is to add a **tester-facing experimental Metal option** so the maintainer can actually select Metal from the app UI and run real ROM tests without relying on environment variables.

This must still be treated as experimental:

```text
Metal UI exposure: allowed for tester/dev builds now
Public/release-quality Metal: not ready
Phase 8: still In progress
Phase 9 full release gate: still blocked by Apple Silicon + visual parity
High2 -> Metal remapping: forbidden
OpenGL -> Metal replacement: forbidden
```

The key distinction is:

```text
Expose Metal so testers can select it.
Do not claim Metal is finished, faster, or release-ready.
```

---

## 2. Current audit summary

### 2.1 What is already good

The current branch has made real progress:

- Metal is still behind the build-time gate:
  - `MELONPRIME_ENABLE_METAL`
  - `MELONPRIME_FORCE_DISABLE_METAL`
- Default Metal state remains conservative: enabled only when explicitly built in.
- `High2` is still OpenGL Compute and is not redirected to Metal.
- The Metal renderer is separate from OpenGL, not an internal replacement for OpenGL.
- Real ROM integration has now been exercised at least far enough to prove:
  - `MetalRenderer3D::RenderFrame()` runs.
  - `RenderNativeOpaquePolygons()` runs.
  - `Texcache<>` / `TexcacheMetalLoader` are reached from real `GPU3D::RenderPolygonRAM` / VRAM state.
- Texture-array feature probing has been strengthened.
- `TexRepeat` wrap/mirror/clamp support has been added for the opaque textured path.
- Default binary string checks are still reported as clean.

### 2.2 What is still not done

Do not treat Metal as performance-ready yet.

The current Metal path still has major missing pieces:

```text
GetLine()/display integration
final composite
visual parity
translucent polygons
shadow masks
line polygons
depth-func-equal
BetterPolygons
hi-res scale
toon/highlight substitution
edge marking
fog
Apple Silicon verification
```

Most importantly:

```text
The current native Metal pass is not yet the final visible renderer.
```

So adding UI now is for **testing access**, not for claiming user-facing completion.

---

## 3. Important performance audit: is Metal already "fast like Compute Shader"?

### 3.1 Short answer

No. Not yet.

Metal is a native GPU API and can become fast, but the current implementation should **not** be expected to perform like the old `OpenGLCompute` / `High2` path yet.

### 3.2 Why not yet

The current Metal work is still in a scaffold / partial native-port state. It has native GPU draw pieces, but it is not yet equivalent to a fully optimized GPU renderer.

Known performance blockers / non-final parts:

```text
MetalRenderer still inherits SoftRenderer.
MetalRenderer3D is installed into Rend3D, but software raster delegation still exists.
Native Metal output is not wired to GetLine()/final display yet.
Some output still follows CPU BGRA / software-visible paths.
RenderNativeOpaquePolygons currently creates Metal buffers per frame / per group.
Command buffers currently use waitUntilCompleted() in some paths.
The renderer is still missing final composite and several DS feature paths.
```

This means early Metal builds may be:

```text
slower than OpenGL
slower than Software in some scenes
not representative of final Metal performance
```

### 3.3 What must be done before calling Metal a fast renderer

Before making performance claims, implement or audit the following:

1. Remove avoidable per-frame allocations:
   - persistent/ring vertex buffers
   - persistent/ring index buffers
   - reusable uniform/config buffers
   - avoid one `newBufferWithBytes` per group where possible

2. Avoid render-thread stalls:
   - remove `waitUntilCompleted()` from normal frame path
   - use inflight frame fences/semaphores only where needed
   - only block in debug readback / explicit verification paths

3. Make final display path native:
   - native Metal color target -> final composite -> presenter
   - avoid unnecessary GPU -> CPU -> GPU round trips

4. Add timing instrumentation:
   - CPU frame time
   - Metal command encoding time
   - GPU completion time where practical
   - texture-cache update time
   - number of polygons / groups / draw calls / texture uploads

5. Compare against:
   - Software
   - OpenGL
   - OpenGLCompute where available on non-macOS
   - current macOS High/Software fallback

### 3.4 Required UI wording

Do not label the button as "High2" or imply Compute Shader.

Recommended labels:

```text
Metal (Experimental)
Native Metal (Experimental)
Metal Test
```

Recommended tooltip:

```text
Experimental native Metal renderer for testing. Not the same as High2 / OpenGL Compute. Visual parity and performance are not final.
```

---

## 4. UI exposure requirements

### 4.1 Add Metal to standard melonDS Video Settings

Add a new renderer radio button to the standard Video Settings dialog:

```text
Software
OpenGL
OpenGL Compute
Metal (Experimental)
```

Requirements:

- `rb3DMetal` must be a new radio button.
- Register it in `grp3DRenderer` with `renderer3D_Metal`.
- Do not reuse `rb3DCompute`.
- Do not rename or change the meaning of `rb3DCompute`.
- The Metal radio button must only exist or be enabled when:
  - `MELONPRIME_ENABLE_METAL`
  - `__APPLE__`
  - runtime probe passes `MelonPrime::Metal::SupportsRequiredBaseline()`
- If the build has no Metal support, the UI must behave exactly as before.
- If probe fails, show the button disabled with a clear tooltip.

Recommended behavior:

```cpp
#if defined(MELONPRIME_ENABLE_METAL) && defined(__APPLE__)
grp3DRenderer->addButton(ui->rb3DMetal, renderer3D_Metal);
ui->rb3DMetal->setVisible(true);
ui->rb3DMetal->setEnabled(MelonPrime::Metal::SupportsRequiredBaseline());
ui->rb3DMetal->setToolTip(
    MelonPrime::Metal::SupportsRequiredBaseline()
        ? tr("Experimental native Metal renderer. Not the same as High2 / OpenGL Compute.")
        : tr("Metal is unavailable on this Mac or failed the runtime feature probe."));
#else
ui->rb3DMetal->setVisible(false);
#endif
```

### 4.2 Fix `VideoSettingsDialog::setEnabled()` for Metal

The existing GL settings are not all valid for Metal.

Required behavior:

```text
Software threaded checkbox: Software only
GL scale factor: OpenGL / OpenGLCompute only unless Metal scale is actually implemented
BetterPolygons: OpenGL only
Compute hi-res coords: OpenGLCompute only
VSync GL controls: OpenGL presentation only
Metal-specific controls: none for now, or disabled with tooltip
```

Do not let Metal accidentally enable OpenGL-only controls.

Recommended helper shape:

```cpp
const bool isSoftware = renderer == renderer3D_Software;
const bool isOpenGL = renderer == renderer3D_OpenGL;
const bool isCompute = renderer == renderer3D_OpenGLCompute;
const bool isMetal =
#if defined(MELONPRIME_ENABLE_METAL)
    renderer == renderer3D_Metal;
#else
    false;
#endif

ui->cbSoftwareThreaded->setEnabled(isSoftware);
ui->cbxGLResolution->setEnabled(isOpenGL || isCompute);
ui->cbBetterPolygons->setEnabled(isOpenGL);
ui->cbxComputeHiResCoords->setEnabled(isCompute);
```

If Metal scale factor is not implemented, do not expose scale as if it works.

### 4.3 Add Metal button to MelonPrime Settings VIDEO QUALITY

Add a separate button:

```text
Low
High
High2
Metal
```

Rules:

- `High2` remains `renderer3D_OpenGLCompute`.
- On macOS, `High2` remains disabled because it is OpenGL Compute.
- Metal is a separate button.
- Metal button should set:
  - `Screen.UseGL = false`
  - `3D.Renderer = renderer3D_Metal`
  - `Screen.VSync = false` initially, unless Metal presenter VSync is proven stable
  - keep existing GL-only options untouched or set harmless defaults
- Metal button should be visible only in Metal-enabled Apple builds.
- Metal button should be enabled only if the runtime probe passes.
- Tooltip must say experimental.

Recommended slot:

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

    // Keep these explicit but do not pretend they are Metal controls.
    cfg.SetBool("3D.Soft.Threaded", true);
    cfg.SetInt("3D.GL.ScaleFactor", 4);
    cfg.SetBool("3D.GL.BetterPolygons", true);
#else
    return;
#endif
}
```

### 4.4 Config and restart behavior

Changing renderer from OpenGL/Software to Metal should use the same renderer dirty/reinit flow as other renderer changes.

Acceptance:

```text
Selecting Metal from Video Settings persists 3D.Renderer=renderer3D_Metal.
Selecting Metal from MelonPrime Settings persists 3D.Renderer=renderer3D_Metal.
Screen.UseGL is false for Metal.
Metal does not create a GL context.
updateRenderer() reaches renderer3D_Metal without env vars.
If Metal Init fails, fallback is safe and visible in log/OSD.
Cancel in Video Settings restores previous renderer.
```

---

## 5. Build command files to add

Create explicit macOS `.command` files so testers can build and run the Metal test build without remembering CMake flags.

Recommended location:

```text
tools/macos/build_melonprime_metal_test.command
tools/macos/run_melonprime_metal_test.command
tools/macos/build_melonprime_metal_force_disabled.command
```

If the repo already has a preferred scripts directory, use that instead, but keep names obvious.

### 5.1 `build_melonprime_metal_test.command`

Purpose: configure and build the Metal-enabled test binary.

Suggested content:

```zsh
#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/build-mac-metal-test"

cd "$REPO_ROOT"

cmake -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMELONPRIME_ENABLE_METAL=ON \
  -DMELONPRIME_FORCE_DISABLE_METAL=OFF

cmake --build "$BUILD_DIR" --parallel "$(sysctl -n hw.ncpu)"

echo
echo "Metal test build complete:"
echo "$BUILD_DIR"
```

### 5.2 `run_melonprime_metal_test.command`

Purpose: launch the Metal-enabled build.

After UI exposure lands, this should not require force env vars. Still allow an optional env fallback for emergency testing.

Suggested content:

```zsh
#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/build-mac-metal-test"

cd "$REPO_ROOT"

APP_PATH="$(find "$BUILD_DIR" -name "melonDS.app" -maxdepth 6 | head -n 1)"

if [[ -z "${APP_PATH}" ]]; then
  echo "melonDS.app not found. Run build_melonprime_metal_test.command first."
  exit 1
fi

echo "Launching: $APP_PATH"
open "$APP_PATH" --args "$@"
```

Optional force-run version for pre-UI fallback:

```zsh
MELONPRIME_FORCE_METAL_RENDERER=1 MELONPRIME_FORCE_METAL_PRESENTER=1 open "$APP_PATH" --args "$@"
```

Do not make the force env the normal path after the UI is added.

### 5.3 `build_melonprime_metal_force_disabled.command`

Purpose: verify the emergency kill switch still returns to pre-Metal behavior.

Suggested content:

```zsh
#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/build-mac-metal-disabled"

cd "$REPO_ROOT"

cmake -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMELONPRIME_ENABLE_METAL=ON \
  -DMELONPRIME_FORCE_DISABLE_METAL=ON

cmake --build "$BUILD_DIR" --parallel "$(sysctl -n hw.ncpu)"

echo
echo "Force-disabled Metal build complete:"
echo "$BUILD_DIR"
```

### 5.4 File permissions

The implementation should either commit them executable or document:

```zsh
chmod +x tools/macos/*.command
```

---

## 6. Immediate performance instrumentation task

Because the maintainer wants to know whether Metal is actually becoming faster, add lightweight logs behind a developer/test gate.

Do not spam normal users.

Recommended gate:

```cpp
MELONPRIME_METAL_PERF=1
```

Log at low frequency, for example once per second:

```text
[MelonPrime] metal perf:
  polygons=<n>
  considered=<n>
  textured=<n>
  groups=<n>
  drawCalls=<n>
  textureUploads=<n>
  vertexBytes=<n>
  indexBytes=<n>
  cpuBuildMs=<n>
  encodeMs=<n>
  waitMs=<n>
```

Minimum required counters:

```text
RenderNativeOpaquePolygons CPU build time
number of draw groups
number of Metal draw calls
vertex buffer bytes
index buffer bytes
Texcache::Update time
texture uploads per frame if easy
whether waitUntilCompleted() ran
```

This is necessary before claiming Metal is faster.

---

## 7. Early optimization directions

Do not block UI exposure on these, but track them immediately after UI exposure.

### 7.1 Remove per-frame / per-group allocation hotspots

Current Metal code still creates buffers dynamically in the frame path. Replace with persistent ring buffers later.

Target design:

```text
FrameResource[3]
  vertexBuffer
  indexBuffer
  uniformBuffer
  commandBuffer state
```

### 7.2 Avoid `waitUntilCompleted()` in normal frame path

`waitUntilCompleted()` is fine for:

```text
feature probe
debug readback
one-shot verification
explicit parity harness
```

It should not be part of normal rendering once output is visible.

### 7.3 Keep CPU fallback safe

While optimizing, keep fallback intact:

```text
Metal init failure -> log + safe fallback
Metal unavailable -> disabled UI
force-disable build -> no Metal symbols/strings/UI
```

---

## 8. Acceptance checklist for this next task

### 8.1 UI acceptance

- [ ] Standard Video Settings shows `Metal (Experimental)` on supported Metal test builds.
- [ ] Standard Video Settings does not show/enable Metal on default or force-disabled builds.
- [ ] MelonPrime Settings shows a separate `Metal` video-quality button.
- [ ] `High2` remains OpenGL Compute only.
- [ ] macOS `High2` remains disabled.
- [ ] Selecting Metal does not set `Screen.UseGL=true`.
- [ ] Selecting Metal reaches `renderer3D_Metal` without env vars.
- [ ] Metal probe failure disables Metal UI with tooltip.
- [ ] Cancel in Video Settings restores previous config.

### 8.2 Build command acceptance

- [ ] `build_melonprime_metal_test.command` configures and builds Metal test tree.
- [ ] `run_melonprime_metal_test.command` launches the built app.
- [ ] `build_melonprime_metal_force_disabled.command` verifies force-disabled tree.
- [ ] Command files are executable or documented with chmod.
- [ ] Scripts work from Finder double-click and from Terminal.

### 8.3 Safety acceptance

- [ ] Default build has no Metal UI.
- [ ] Force-disabled build has no Metal UI.
- [ ] Force-disabled build has no `renderer3D_Metal` path visible to UI.
- [ ] Default binary string check remains clean.
- [ ] Windows/Linux builds unaffected.

### 8.4 Performance honesty acceptance

- [ ] UI labels say experimental.
- [ ] No text claims Metal is already faster.
- [ ] Perf logging exists behind `MELONPRIME_METAL_PERF=1`.
- [ ] Known perf blockers are documented:
  - software delegate still involved
  - final display not native yet
  - per-frame/per-group allocations
  - `waitUntilCompleted()` in debug/current paths

---

## 9. Suggested commit sequence

### Commit 1

```text
Expose experimental Metal renderer in video settings
```

- Add `rb3DMetal`.
- Register it with `renderer3D_Metal`.
- Gate visibility/enabled state by Metal build + runtime probe.
- Fix/adjust renderer control enabling for Metal.

### Commit 2

```text
Add MelonPrime Metal video quality preset
```

- Add separate Metal button.
- Keep High2 unchanged.
- Set `Screen.UseGL=false`, `3D.Renderer=renderer3D_Metal`.
- Tooltip says experimental.

### Commit 3

```text
Add macOS Metal test build command files
```

- Add build/run/force-disabled `.command` files.
- Document chmod and expected build directories.

### Commit 4

```text
Add Metal renderer perf test logging
```

- Add `MELONPRIME_METAL_PERF=1` logs.
- Include frame/group/draw/upload/timing counters.
- Keep logs disabled by default.

---

## 10. Final rule for the execution team

Expose Metal now so testers can select it.

But keep the semantics clean:

```text
High2 = OpenGL Compute
Metal = native Metal experimental renderer
OpenGL = existing OpenGL renderer
Software = existing software renderer
```

Do not claim Metal is faster yet.

Make it selectable, measurable, and safely disableable.
