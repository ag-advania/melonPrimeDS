# MelonPrime Metal Internal Resolution Visibility Audit and Next Instructions

Date: 2026-07-09 JST  
Repo/branch: `ag-advania/melonPrimeDS` / `highres_fonts_v3`  
Audited HEAD: `731a55349b341f8473e4e6cd960025771c5c0f92`  
Previous audit base: `def57afa72adfb8845ba98bb65393db1f6c2fd0d`

## 1. Audit verdict

The latest push correctly exposes the internal-resolution combo while `Metal (Experimental)` is selected and correctly logs Metal target scale/size. However, the user's report is valid: **changing Metal internal resolution does not visibly change the rendered output yet**.

This is not primarily a UI bug anymore. It is a pipeline-connection bug/design gap:

- The UI writes `3D.GL.ScaleFactor`.
- `MetalRenderer::SetRenderSettings()` forwards that value to `MetalRenderer3D::SetScaleFactor()`.
- `MetalRenderer3D::ResizeTargets()` allocates larger Metal render targets.
- But the visible frame still comes from the Software delegate path.
- `MetalRenderer3D::GetLine()` still returns `Delegate.GetLine(line)`.
- `ScreenPanelMetal` still uploads fixed 256x192 CPU BGRA framebuffers into its own 256x192 presentation textures.
- Therefore the larger Metal render target exists, but it is not the thing the tester sees.

In short:

```text
Current behavior:
UI scale -> Metal target size changes -> visible output still Software 256x192

Tester expectation:
UI scale -> Metal target size changes -> Metal output is presented -> visible internal resolution changes
```

## 2. What the push fixed correctly

### 2.1 Metal resolution combo enablement

`VideoSettingsDialog::setEnabled()` now treats Metal as a renderer allowed to use the internal-resolution combo:

```cpp
ui->cbxGLResolution->setEnabled(openGLRenderer || computeRenderer || metalRenderer);
```

This is correct for tester UI.

Keep `BetterPolygons` and `Compute Hi-Res Coordinates` disabled for Metal until those paths are actually implemented.

### 2.2 Metal target scale logging

`MetalPerfFrame` / `MetalPerfAccumulator` now track:

```text
scale
target width
target height
```

and the perf log now includes:

```text
scale=<n> target=<w>x<h>
```

This is correct.

### 2.3 Resize target is using ScaleFactor

`MetalRenderer3D::ResizeTargets()` calculates:

```cpp
const NSUInteger width = static_cast<NSUInteger>(256 * ScaleFactor);
const NSUInteger height = static_cast<NSUInteger>(192 * ScaleFactor);
```

and logs target resize through:

```cpp
MetalPerfLogTargetResize(ScaleFactor, width, height);
```

This confirms the target allocation side is wired.

### 2.4 run command cleanup

`tools/macos/run_metal_test.command` now clears inherited `MELONPRIME_FORCE_METAL_RENDERER` / `MELONPRIME_FORCE_METAL_PRESENTER` unless `MELONPRIME_ALLOW_FORCE_METAL=1` is set.

This is correct because the default tester path should exercise the UI selection, not hidden environment overrides.

## 3. Root cause of "internal resolution does not change"

### 3.1 `GetLine()` still returns Software delegate output

Current `MetalRenderer3D::GetLine()`:

```cpp
u32* MetalRenderer3D::GetLine(int line)
{
    return Delegate.GetLine(line);
}
```

This means the final DS 3D line consumed by the software 2D compositor is still the software renderer's 256x192 output.

Changing `ScaleFactor` can resize `State->ColorTarget`, but the final visible frame does not sample `State->ColorTarget`.

### 3.2 `RenderFrame()` still renders Software first

Current `MetalRenderer3D::RenderFrame()` still calls:

```cpp
Delegate.RenderFrame();
perfFrame.CpuRendererFallback = true;
```

The native Metal pass is still a side/shadow pass:

```cpp
ClearNativeTarget();
RenderNativeOpaquePolygons();
```

This is useful for exercising the native path, but it does not replace visible output.

### 3.3 `ScreenPanelMetal` only presents CPU BGRA output

`ScreenPanelMetal::drawScreen()` currently gets:

```cpp
const melonDS::RendererOutput output = nds->GPU.GetRendererOutput();
hasCpuBuffersForFrame = (output.Kind == melonDS::RendererOutputKind::CpuBgra);
```

and only uploads/draws when `output.Kind == CpuBgra`:

```cpp
[m->screenTex[0] replaceRegion:... withBytes:output.Top ...];
[m->screenTex[1] replaceRegion:... withBytes:output.Bottom ...];
```

Also, those presenter textures are allocated at fixed `256x192`:

```cpp
width: 256
height: 192
```

So even if the Metal 3D renderer has a `1024x768` target, the presenter is still showing fixed-size CPU buffers.

### 3.4 `RendererOutputKind::MetalTexture` exists but is not used for this path

The abstraction already has:

```cpp
RendererOutputKind::MetalTexture
RendererOutput::MetalTexture(void* texture)
```

But `MetalRenderer`/`MetalRenderer3D` does not currently return the native `ColorTarget` through `Renderer::GetOutput()`, and `ScreenPanelMetal` does not present such an output.

This is the next required wiring.

## 4. Implementation goal for next push

Make Metal internal resolution **visibly testable**.

For the tester build, when `renderer3D_Metal` is selected and Metal has a valid native color target, the Metal presenter must be able to present that native Metal target instead of only presenting the 256x192 CPU BGRA buffers.

The goal is not full visual parity yet. The goal is:

```text
Selecting 1x / 2x / 4x in Video Settings visibly changes the Metal-rendered output path.
```

## 5. Required implementation strategy

### 5.1 Add a native Metal texture output path

Expose the current native 3D color target from `MetalRenderer3D`.

In `GPU3D_Metal.h`, add methods similar to:

```cpp
void* GetColorTargetTexture() const noexcept;
int GetTargetWidth() const noexcept;
int GetTargetHeight() const noexcept;
int GetScaleFactor() const noexcept;
```

In `GPU3D_Metal.mm`:

```cpp
void* MetalRenderer3D::GetColorTargetTexture() const noexcept
{
    return State ? (__bridge void*)State->ColorTarget : nullptr;
}

int MetalRenderer3D::GetTargetWidth() const noexcept
{
    return State && State->ColorTarget ? static_cast<int>(State->ColorTarget.width) : 0;
}

int MetalRenderer3D::GetTargetHeight() const noexcept
{
    return State && State->ColorTarget ? static_cast<int>(State->ColorTarget.height) : 0;
}

int MetalRenderer3D::GetScaleFactor() const noexcept
{
    return ScaleFactor;
}
```

Do not transfer ownership. The renderer owns the `id<MTLTexture>`; the presenter only borrows it for the draw call.

### 5.2 Override `MetalRenderer::GetOutput()`

Because `Renderer::GetOutput()` defaults to CPU BGRA if `GetFramebuffers()` returns true, `MetalRenderer` must explicitly override it.

Target behavior:

```cpp
RendererOutput MetalRenderer::GetOutput()
{
#if defined(MELONPRIME_ENABLE_METAL)
    auto* rend3d = dynamic_cast<MetalRenderer3D*>(Rend3D.get());
    if (rend3d)
    {
        if (void* tex = rend3d->GetColorTargetTexture())
            return RendererOutput::MetalTexture(tex);
    }
#endif
    return SoftRenderer::GetOutput();
}
```

This is intentionally tester-stage behavior. If the Metal target is unavailable, fall back to the existing CPU path rather than crashing.

### 5.3 Teach `ScreenPanelMetal` to draw `RendererOutputKind::MetalTexture`

In `ScreenPanelMetal::drawScreen()`, extend the `RendererOutput` branch:

```cpp
const melonDS::RendererOutput output = nds->GPU.GetRendererOutput();

if (output.Kind == melonDS::RendererOutputKind::MetalTexture)
{
    id<MTLTexture> metalTop = (__bridge id<MTLTexture>)output.Top;
    // draw metalTop directly with the existing screen pipeline
}
else if (output.Kind == melonDS::RendererOutputKind::CpuBgra)
{
    // existing CPU upload path
}
```

Important details:

- Do not copy the Metal target back to CPU for normal presentation.
- Do not downsample it into the existing 256x192 `screenTex`.
- Bind the native Metal target directly as `texture(0)`.
- Use the existing screen vertex/presenter shader, but account for the fact that the texture is `ScaleFactor * 256` by `ScaleFactor * 192`.
- Normalized texture coordinates `0..1` should work as long as the Metal target contains the full rendered scene.

### 5.4 Add a native-Metal-presenter log

When the presenter first sees `RendererOutputKind::MetalTexture`, log:

```text
[MelonPrime] metal presenter: first native Metal texture output size=<w>x<h>
```

Also add a one-line warning if it falls back to CPU:

```text
[MelonPrime] metal presenter: Metal renderer returned no native texture; falling back to CPU BGRA
```

Make these one-shot or low-noise.

### 5.5 Keep UI and config behavior as-is

Do not rename `3D.GL.ScaleFactor` yet. For this tester phase, it remains the shared hardware-renderer internal scale.

Keep:

```cpp
ui->cbxGLResolution->setEnabled(openGLRenderer || computeRenderer || metalRenderer);
```

Keep:

```cpp
cfg.SetInt("3D.GL.ScaleFactor", 4);
```

in the Metal Test preset.

### 5.6 Update comments that currently imply scale does not work

Current comments in `RenderNativeOpaquePolygons()` say the pass only fills the native-resolution corner or that hi-res scale is not implemented. After the output path is connected, update them accurately.

Use this wording if the presenter path works:

```text
ScaleFactor controls the Metal render-target size. DS screen-space coordinates are still native 256x192 but are mapped to NDC, so rasterization covers the full scaled render target. Full GL parity is still incomplete because translucent polygons, edge/fog/final composite, and 2D/3D composition correctness are not finished.
```

Use this wording if the target still does not display correctly:

```text
ScaleFactor currently changes target allocation, but visible scaled output is still incomplete. Do not advertise Metal internal resolution as working until the presenter samples the native Metal target correctly.
```

## 6. Critical warnings

### 6.1 Do not implement this via `GetLine()`

`GetLine(int line)` is a 256x192 software-compositor interface. It is the wrong place to expose a high-resolution Metal texture.

Using `GetLine()` for Metal internal resolution would either:

- downsample back to 256x192, hiding the internal-resolution benefit, or
- require a large redesign of the software 2D compositor.

For tester visibility, use `RendererOutput::MetalTexture` + `ScreenPanelMetal`.

### 6.2 Do not claim full Metal parity

Even if internal resolution becomes visible, Metal is still incomplete:

- Software delegate still runs.
- CPU fallback still exists.
- Opaque polygons only.
- No translucent polygons.
- No shadow masks/shadows.
- No line polygons.
- No depth-func-equal.
- No BetterPolygons.
- No toon/highlight substitution.
- No edge marking.
- No fog.
- No final composite parity.
- Apple Silicon still unverified.

UI labels should remain:

```text
Metal (Experimental)
Video quality: Metal Test
```

### 6.3 Do not call it High2 or Compute

High2 remains OpenGL Compute. Metal is a separate renderer.

Never rename it to:

```text
High2 Metal
Metal Compute
Fast Metal
Compute Metal
```

## 7. Test plan

### 7.1 Build

Run:

```sh
tools/macos/build_metal_test.command
cmake --build build-mac --parallel "$(sysctl -n hw.ncpu)"
```

Expected:

- Metal test build passes.
- Normal macOS build passes.
- Default/non-Metal build remains free of Metal UI strings if the existing string audit is available.

### 7.2 UI scale test

Run:

```sh
tools/macos/run_metal_test.command /path/to/rom.nds
```

In the UI:

1. Select `Metal (Experimental)` or click `Video quality: Metal Test`.
2. Open Video Settings.
3. Change internal resolution:
   - 1x = 256x192
   - 2x = 512x384
   - 4x = 1024x768
4. Confirm logs include:
   - `target scale=1 size=256x192`
   - `target scale=2 size=512x384`
   - `target scale=4 size=1024x768`
5. Confirm the visible Metal path changes image sharpness / aliasing as scale changes.

### 7.3 Presenter path test

Expected new log:

```text
[MelonPrime] metal presenter: first native Metal texture output size=1024x768
```

If the log still shows only CPU BGRA path, the internal-resolution work is not complete.

### 7.4 Perf log test

With `MELONPRIME_METAL_PERF=1`, confirm:

```text
[MelonPrime] metal perf: frames=600 scale=4 target=1024x768 ...
```

The `scale` and `target` values must match the UI selection.

### 7.5 Fallback test

Force an unsupported path or temporarily return `nullptr` from `GetColorTargetTexture()` and confirm the presenter falls back safely to CPU BGRA instead of crashing.

## 8. Acceptance criteria

The next push is acceptable when all are true:

- Metal internal resolution combo is enabled when Metal is selected.
- Changing the combo changes `3D.GL.ScaleFactor`.
- `MetalRenderer3D::SetScaleFactor()` is reached.
- `ResizeTargets()` logs the expected target size.
- `MetalRenderer::GetOutput()` returns `RendererOutputKind::MetalTexture` when the native Metal target is valid.
- `ScreenPanelMetal` presents `RendererOutputKind::MetalTexture` directly.
- Visible output changes when selecting 1x / 2x / 4x.
- The app falls back to CPU BGRA safely if Metal target is unavailable.
- No High2-to-Metal remapping is introduced.
- UI remains clearly experimental.
- Normal/non-Metal build remains unaffected.

## 9. What not to do in this pass

Do not do these yet:

- Do not rename `3D.GL.ScaleFactor` globally.
- Do not introduce `3D.Metal.ScaleFactor` yet.
- Do not remove the Software delegate yet.
- Do not remove CPU fallback.
- Do not claim final visual parity.
- Do not implement final fog/edge/translucency/composite in the same patch unless separately scoped.
- Do not route High2 to Metal.
- Do not require Apple Silicon before tester-only visibility work, but keep public release blocked until Apple Silicon is verified.

## 10. Suggested commit message

```text
Metal backend: present native scaled target in tester UI
```

Suggested body:

```text
Expose the MetalRenderer3D color target through RendererOutput::MetalTexture
and teach ScreenPanelMetal to sample it directly in Metal-enabled tester
builds. This makes the existing internal-resolution combo visibly affect
the Metal path instead of only resizing an unused shadow target.

Keep CPU BGRA fallback, keep High2/OpenGL Compute separate, and keep the
UI labelled experimental. Add one-shot presenter logs for native Metal
texture output size and fallback behavior.
```
