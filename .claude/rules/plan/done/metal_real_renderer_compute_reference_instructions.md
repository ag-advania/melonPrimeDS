# MelonPrimeDS Metal Backend — Real Metal Renderer / Compute-Reference Instructions

Date: 2026-07-09 JST  
Repo/branch: `ag-advania/melonPrimeDS` / `highres_fonts_v3`  
Current context: Metal tester UI exists, but visible output is still not a real full Metal-rendered screen path.

## 0. Why this instruction replaces the previous wording

The previous wording around "normal mode" / "debug mode" was misleading and should not drive the implementation.

The project goal is not to expose an optional debug preview of a Metal texture. The goal is:

```text
When the user selects Metal, the emulator should render and present through the Metal GPU path.
```

In other words, Metal mode must not be a UI label wrapped around the software renderer. If the visible game frame still comes from the software renderer's completed 256x192 CPU framebuffer, then internal resolution will not visibly change and the user has no meaningful way to test Metal.

This instruction sets the new priority: **make Metal mode a real GPU renderer path, using the existing OpenGL / Compute renderer architecture as the reference.**

## 1. High-level answer: what Metal is supposed to be

Metal is a GPU API. It is not software rendering.

The intended destination is:

```text
Metal mode = macOS-native GPU renderer replacing the unavailable OpenGL Compute path on macOS.
```

It should become the macOS high-performance renderer path, comparable in role to High2 / OpenGL Compute on platforms where OpenGL Compute works.

However, the current implementation is not there yet because the visible output path is still mixed with the software delegate. The next work must close that gap instead of adding more disclaimers.

## 2. Existing Compute / OpenGL renderer behavior to use as the reference

Do not invent a Metal-specific screen-routing model from scratch. Follow the existing hardware renderer architecture.

The important existing pattern is in `GLRenderer` / Compute:

1. `GLRenderer` owns GPU-side 2D renderers for both engines:

```cpp
Rend2D_A = std::make_unique<GLRenderer2D>(GPU.GPU2D_A, *this);
Rend2D_B = std::make_unique<GLRenderer2D>(GPU.GPU2D_B, *this);
```

2. If Compute is selected, only the 3D renderer changes:

```cpp
if (IsCompute)
    Rend3D = std::make_unique<ComputeRenderer3D>(GPU.GPU3D, *this);
else
    Rend3D = std::make_unique<GLRenderer3D>(GPU.GPU3D, *this);
```

3. ScaleFactor is applied to both the 2D renderer path and the selected 3D renderer:

```cpp
SetScaleFactor(settings.ScaleFactor);
rend2dA->SetScaleFactor(settings.ScaleFactor);
rend2dB->SetScaleFactor(settings.ScaleFactor);
ComputeRenderer3D::SetRenderSettings(settings.ScaleFactor, settings.HiresCoordinates);
```

4. `GLRenderer::SetScaleFactor()` reallocates final GPU output textures using scaled dimensions:

```cpp
ScreenW = 256 * scale;
ScreenH = 192 * scale;
glTexImage3D(... FPOutputTex[i] ..., ScreenW, ScreenH, 2, ...);
```

5. `GLRenderer::RenderScreen()` performs the final GPU-side screen composition into `FPOutputTex[backbuf]`, using DS display registers such as `DispCntA`, `DispCntB`, `GPU.ScreenSwap`, display mode, brightness, capture state, and the renderer outputs.

6. `GLRenderer::GetFramebuffers()` does not return CPU framebuffers. It returns the final GPU texture object:

```cpp
*top = &FPOutputTex[frontbuf];
*bottom = nullptr;
return false;
```

7. The frontend presenter then treats that output as a hardware texture output, not as software CPU BGRA.

This is the shape Metal must copy.

## 3. Correct Metal target architecture

Do not make `RendererOutput::MetalTexture` point at raw `MetalRenderer3D::ColorTarget` as the normal Metal output.

That raw target is only the incomplete native 3D opaque pass. It is not a completed DS screen. It does not include:

- DS 2D layer composition,
- screen A / screen B routing,
- screen swap,
- brightness,
- capture paths,
- lower-screen presentation,
- HUD / overlay integration,
- translucent polygons,
- shadows,
- edge marking,
- fog,
- final composite.

The correct normal output for Metal mode should be:

```text
RendererOutput::MetalTexture(final composed two-screen Metal output texture)
```

not:

```text
RendererOutput::MetalTexture(raw 3D opaque ColorTarget)
```

## 4. Required next-step implementation plan

### 4.1 Create a Metal final screen output, analogous to GL `FPOutputTex`

Add Metal-owned final output textures to `MetalRenderer` or a dedicated Metal renderer state object.

Required shape:

```text
FinalOutputTex[2]      // double-buffered, matching GL FPOutputTex[2]
Each final output has 2 layers or two sibling textures:
  layer 0 / texture 0 = DS screen A output
  layer 1 / texture 1 = DS screen B output
Size = 256 * ScaleFactor by 192 * ScaleFactor
Format = BGRA8Unorm or the closest existing presenter-compatible format
Usage = RenderTarget | ShaderRead
```

Prefer a `MTLTextureType2DArray` with 2 layers if convenient, because that mirrors GL's `GL_TEXTURE_2D_ARRAY` model. If Metal presenter is simpler with two separate textures, that is acceptable temporarily, but the output contract must still represent both screens as one renderer output object.

### 4.2 `RendererOutput::MetalTexture` must represent final composed output

Change the meaning of `MetalRenderer::GetOutput()`.

Current incorrect/insufficient idea:

```cpp
return RendererOutput::MetalTexture(rend3d->GetColorTargetTexture());
```

Target idea:

```cpp
return RendererOutput::MetalTexture(finalOutputTextureForFrontBuffer);
```

If a second pointer is needed for two separate textures, extend `RendererOutput::MetalTexture(top, bottom)` or use `Top` for a texture array and `Bottom = nullptr`, matching the GL texture-array convention.

Do not return the raw 3D `ColorTarget` as the normal renderer output.

### 4.3 Implement a Metal final pass equivalent to `GLRenderer::RenderScreen()`

Port the structure of `GLRenderer::RenderScreen()` to Metal.

The first implementation can be incomplete, but it must be honest and GPU-driven.

Required minimum:

- Render both DS physical screens into Metal final output.
- Respect `GPU.ScreenSwap` / existing screen routing. Do not hardcode top screen or bottom screen.
- Feed native `MetalRenderer3D::ColorTarget` into whichever DS screen needs 3D output.
- Preserve the lower screen path; do not ignore it just because MPH usually uses the top screen for gameplay.
- Use `ScaleFactor` for output texture size and viewport/scissor.
- Ensure 1x/2x/4x visibly changes the native Metal 3D output.

Recommended staged port:

#### Stage A — visible native Metal 3D without pretending full parity

- Final output texture is produced by Metal.
- If a screen uses 3D, source that screen from native `MetalRenderer3D::ColorTarget`.
- If a screen uses only 2D and Metal 2D is not ported yet, use a temporary CPU/2D texture source only for 2D content, not the complete software-rendered frame.
- Do not use the software renderer's completed top/bottom CPU BGRA frame as the accepted Metal output.

This is acceptable only as an intermediate step if logs clearly say:

```text
visible 3D source = native Metal
2D source = temporary CPU/2D upload
complete CPU frame fallback = false
```

#### Stage B — Metal 2D screen composition

- Port enough of `GLRenderer2D` / final pass behavior to Metal so both screens are GPU-composited.
- Match the GL final pass behavior for display modes, brightness, screen swap, and auxiliary inputs.

#### Stage C — capture / parity / remaining DS effects

- Add capture paths, VRAM display capture, translucent polygons, shadows, lines, depth-equal, edge/fog/final composite.

## 5. Internal resolution requirement

Internal resolution is not complete until the visible Metal output changes with scale.

The current situation where `ColorTarget` reallocates to 1024x768 but the user still sees a 256x192 CPU-completed frame is not acceptable.

Acceptance requirement:

```text
Metal selected + 1x => visible native 3D output uses 256x192 source
Metal selected + 2x => visible native 3D output uses 512x384 source
Metal selected + 4x => visible native 3D output uses 1024x768 source
```

Required logs:

```text
[MelonPrime] metal renderer: final output scale=4 size=1024x768 layers=2
[MelonPrime] metal presenter: visible source=MetalFinalTexture scale=4 size=1024x768 screens=2
[MelonPrime] metal renderer3D: native 3D source scale=4 size=1024x768
```

Forbidden completion claim:

```text
ScaleFactor changed internally, but visible frame still comes from CPU BGRA 256x192.
```

## 6. Fallback policy

Metal mode must not silently fall back to a completed software frame and still claim to be Metal.

Allowed:

```text
Metal presenter uses temporary CPU-uploaded 2D content while native Metal 3D is visible.
```

Not allowed:

```text
Metal mode displays the complete software renderer top/bottom frame as the normal output.
```

If complete software fallback is needed for crash avoidance, require an explicit developer flag:

```text
MELONPRIME_METAL_ALLOW_SOFTWARE_FALLBACK=1
```

Without that flag, failure to produce native Metal final output should be visible and logged loudly:

```text
[MelonPrime] metal renderer: ERROR no native Metal final output; refusing silent software fallback
```

## 7. What to remove or stop doing

Remove or avoid these patterns:

```cpp
RendererOutput::MetalTexture(rend3d->GetColorTargetTexture()) // as normal final output
```

```cpp
if (hasCpuBuffersForFrame)
    sourceTexture = m->screenTex[screenKind[i]]; // as normal Metal selected output
```

```text
CPU BGRA base frame remains the normal visible output in Metal mode
```

These patterns hide the fact that Metal is not actually driving the visible game output.

## 8. Presenter rule

`ScreenPanelMetal` should present the renderer's final Metal output texture, analogous to how the GL presenter consumes the GL texture array.

Do not make `ScreenPanelMetal` decide DS 3D routing by `screenKind == 0` or other top-screen assumptions.

The renderer should output a completed two-screen Metal texture. The presenter should only place those two screen layers/textures according to the existing `screenMatrix`, `screenKind`, layout, and HUD/OSD overlay rules.

In other words:

```text
Renderer decides what each DS screen contains.
Presenter decides where each DS screen appears in the window.
```

## 9. Performance requirement

Metal is expected to become the macOS replacement for the Compute Renderer role, but it will not be fast while it still runs the software delegate and then uploads/composites around it.

Performance work starts with removing the normal visible dependency on:

```cpp
Delegate.RenderFrame();
Delegate.GetLine();
SoftRenderer::GetOutput();
complete CPU BGRA top/bottom frame upload;
```

Temporary use of the delegate may remain behind a clearly named fallback/dev flag, but it must not be the normal Metal selected path.

Required perf log fields:

```text
native3dMs
finalPassMs
presentMs
uploadBytes
softwareFallback=0/1
visibleSource=MetalFinalTexture|SoftwareFallback
scale=<n>
target=<w>x<h>
```

Acceptance condition for the next milestone:

```text
Metal selected, visibleSource=MetalFinalTexture, softwareFallback=0, scale changes visible output.
```

## 10. Build and test commands

Run:

```sh
tools/macos/build_metal_test.command
tools/macos/run_metal_test.command /path/to/rom.nds
```

Test matrix:

```text
1. Metal selected, 1x, MPH boot/gameplay visible.
2. Metal selected, 2x, native 3D visibly sharper or at least final Metal output size/log changes.
3. Metal selected, 4x, native 3D visibly sharper or at least final Metal output size/log changes.
4. Top/bottom screen layout changes still show both screens.
5. If DS screen swap occurs, renderer output remains correct.
6. CustomHUD still appears through presenter overlay.
7. No silent complete software fallback unless MELONPRIME_METAL_ALLOW_SOFTWARE_FALLBACK=1.
```

## 11. Acceptance criteria

This task is complete only when all are true:

- Selecting `Metal (Experimental)` no longer displays the normal complete software-rendered CPU frame as the accepted output.
- `RendererOutput::MetalTexture` points to a final composed Metal screen output, not raw 3D-only `ColorTarget`.
- Both DS screens are represented in Metal output.
- The presenter does not guess top/bottom 3D routing; it presents the renderer's two-screen output.
- Internal resolution visibly affects the Metal-rendered 3D output.
- Logs prove `visibleSource=MetalFinalTexture` and `softwareFallback=0` in normal Metal mode.
- Complete software fallback is only available through an explicit developer flag.
- Existing OpenGL and Compute renderer behavior remains unchanged.

## 12. Commit message suggestion

```text
Metal backend: make Metal mode present final GPU output
```

or, if split into smaller commits:

```text
Metal backend: add final two-screen Metal output target
Metal backend: route presenter through Metal final output
Metal backend: make internal scale visible in Metal mode
```
