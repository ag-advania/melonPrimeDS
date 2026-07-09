# MelonPrimeDS Metal Flicker / Black 3D — SRP & Best-Practice Refactor Instructions

Date: 2026-07-09 JST  
Repo/branch: `ag-advania/melonPrimeDS` / `highres_fonts_v3`  
Audited HEAD: `1d377094a59aa6226dad2ff8fa993fce4af84e7c`  
Previous audit base: `70fb37a9f166761afe98e09a56bf51fe9176a4af`  
Purpose: replace the current ad-hoc Metal final-output path with a responsibility-separated renderer design that follows existing melonDS OpenGL/Compute behavior and common Metal render-loop practices.

---

## 0. Current user-visible bugs

Reported symptoms:

```text
- Screen flickers.
- The 3D video region is black / not visible.
```

Treat both as release-blocking for Metal tester mode.

The target behavior is not "make something Metal-shaped appear". The target behavior is:

```text
Metal mode selected
→ emulated frame is built by Metal renderer timing
→ visible DS screens come from a stable Metal final-output texture
→ 3D content is native Metal, not silent Software 3D fallback
→ internal resolution is visibly reflected in the native Metal path
```

---

## 1. High-level verdict

The latest push moved in the right direction by adding a two-layer Metal final-output texture, but it still violates core renderer architecture rules:

```text
RendererOutput/GetOutput() is doing frame construction.
Presenter paint timing is indirectly driving final composition.
The native 3D target can be black while the final pass still claims visibleSource=MetalFinalTexture.
Device ownership is split between renderer3D, final composer, and presenter.
3D routing is guessed with simplified EngineAUses3D / ScreenSwap logic instead of mirroring the existing renderer final-composite semantics.
```

These are exactly the kind of mistakes that produce flicker, alternating black frames, stale texture presentation, and misleading logs.

---

## 2. Non-negotiable design principle

### 2.1 Metal mode must be real Metal

When `renderer3D_Metal` is selected, the normal visible 3D path must not be Software 3D fallback.

Allowed temporarily:

```text
- CPU/Software 2D data upload for DS 2D layers, HUD, touch screen, or OSD while 2D Metal is incomplete.
- Software fallback only behind an explicit developer flag for emergency comparison.
```

Not allowed:

```text
- Presenting a complete CPU BGRA frame and calling it Metal mode.
- Returning CPU BGRA from Metal renderer silently.
- Using native Metal raw ColorTarget as if it were a complete final screen.
- Hiding black native 3D by falling back to Software without a log.
```

### 2.2 Getters must not render

`GetOutput()` / `GetFramebuffers()` must be cheap state queries.

They must not:

```text
- allocate textures,
- upload CPU screen data,
- encode Metal command buffers,
- wait for GPU completion,
- flip front/back buffers,
- build a new emulated frame.
```

They may only return the latest completed frame object.

### 2.3 Presenter must not build emulator frames

`ScreenPanelMetal::drawScreen()` may:

```text
- acquire CAMetalDrawable late,
- render a completed renderer-owned Metal texture to the drawable,
- draw OSD/HUD overlay,
- present the drawable.
```

It must not:

```text
- decide DS 3D routing,
- compose 2D/3D emulation output,
- mutate renderer front/back buffers,
- repair missing 3D output by choosing CPU fallback.
```

---

## 3. Existing OpenGL/Compute renderer is the primary reference

Do not invent a Metal-only screen model. Copy the architecture of existing `GLRenderer` / `ComputeRenderer3D` at the responsibility level.

Existing model:

```text
GLRenderer
├─ GLRenderer2D A
├─ GLRenderer2D B
├─ GLRenderer3D or ComputeRenderer3D
├─ intermediate textures:
│  ├─ OutputTex2D[0]
│  ├─ OutputTex2D[1]
│  └─ OutputTex3D
└─ final output:
   └─ FPOutputTex[front/back], texture2DArray with 2 layers
```

Frame flow:

```text
DrawScanline / VBlank renderer timing
→ 2D/3D renderers update intermediate outputs
→ RenderScreen(...) performs final two-screen composition
→ VBlank finishes final output for the emulated frame
→ GetFramebuffers() returns the completed front FPOutputTex
→ presenter samples only the completed texture
```

Metal should mirror this:

```text
MetalRenderer
├─ MetalRenderer3D                 # native DS 3D render target, not final screen
├─ Metal2DSourceProvider           # temporary CPU upload now; native Metal 2D later
├─ MetalFinalComposer              # composes two DS screens into final 2-layer texture
├─ MetalFrameCoordinator           # owns frame index, completed front buffer, in-flight state
└─ GetOutput()                     # returns completed front texture only
```

---

## 4. SRP target architecture

### 4.1 `MetalRenderer3D`

Responsibility:

```text
Port DS 3D rendering to native Metal.
```

Owns:

```text
- native 3D ColorTarget
- AttrTarget
- DepthStencilTarget
- 3D pipelines
- 3D texture cache
- 3D draw submission
```

Does not own:

```text
- final two-screen composition
- presenter/drawable
- UI scale combo
- HUD/OSD
- CPU fallback policy
- top/bottom screen routing policy
```

Required APIs:

```cpp
id<MTLTexture> GetNative3DColorTarget() const noexcept;
id<MTLDevice> GetMetalDevice() const noexcept;
int GetScaleFactor() const noexcept;
int GetTargetWidth() const noexcept;
int GetTargetHeight() const noexcept;
Metal3DDiagnostics GetLastDiagnostics() const noexcept;
```

Current issue:

```text
The 3D path can return a valid texture that is still entirely black. A valid texture pointer is not proof of valid 3D output.
```

Therefore `MetalRenderer3D` must expose diagnostics, not only a texture pointer.

---

### 4.2 `MetalFinalComposer`

Responsibility:

```text
Build a GL/Compute-like final two-screen Metal texture for one completed emulated frame.
```

Owns:

```text
- FinalOutputTex[bufferCount], each a 2-layer BGRA8 texture array
- final-pass pipeline(s)
- CPU-upload textures for temporary 2D layer source
- final-pass uniforms mirroring GLRenderer::sFinalPassConfig
- optional capture/downscale resources later
```

Does not own:

```text
- native 3D polygon rendering
- CAMetalLayer drawable acquisition
- Qt widget painting
- frame scheduling
```

Required design:

```cpp
class MetalFinalComposer final
{
public:
    bool Init(id<MTLDevice> device);
    bool Resize(int scale);
    bool ComposeFrame(const MetalFrameInputs& inputs, MetalFrameOutput* output);

private:
    // Persistent objects only. No per-frame pipeline creation.
};
```

`ComposeFrame()` must be called from renderer frame timing, not from `GetOutput()` and not from `ScreenPanelMetal::drawScreen()`.

---

### 4.3 `MetalFrameCoordinator`

Responsibility:

```text
Own frame lifetime, completed-front index, in-flight GPU work, and synchronization policy.
```

Owns:

```text
- frame serial number
- front buffer index
- back buffer index
- optional triple-buffer inflight semaphore
- completed-frame flag
- last output diagnostics
```

Rules:

```text
- Flip front buffer exactly once per composed emulated frame.
- Do not flip on presenter draw.
- Do not flip on GetOutput().
- Do not expose a texture still being written by GPU unless command buffer dependency guarantees it is safe.
```

Initial implementation may use a conservative `waitUntilCompleted()` only to stabilize correctness. After black/flicker is fixed, remove blocking waits and use command-buffer completion handlers or a small in-flight ring.

---

### 4.4 `MetalRenderer`

Responsibility:

```text
Top-level renderer orchestration.
```

Owns:

```text
- MetalRenderer3D
- temporary 2D source bridge
- MetalFinalComposer
- MetalFrameCoordinator
```

Correct lifecycle:

```cpp
void MetalRenderer::VBlank()
{
    // Match GLRenderer frame completion semantics.
    // CPU 2D fallback may still run temporarily, but Software 3D must not be the visible Metal path.
    SoftRenderer::VBlank(); // if needed for temporary 2D/HUD source only
    ComposeFinalOutputForCompletedFrame();
}

RendererOutput MetalRenderer::GetOutput()
{
    return FrameCoordinator.GetCompletedRendererOutput();
}
```

If `VBlank()` is not the best hook for this codebase, use the exact hook that corresponds to the existing OpenGL/Compute frame-completion point. Do not use GUI/presenter timing.

---

### 4.5 `ScreenPanelMetal`

Responsibility:

```text
Present the latest completed renderer output to CAMetalLayer and draw UI overlay.
```

Owns:

```text
- CAMetalLayer
- drawable command queue / present pipeline
- screen layout transform
- OSD/HUD overlay texture
```

Does not own:

```text
- emulator final composition
- 3D routing
- renderer fallback policy
- front/back flip
```

Correct draw flow:

```cpp
void ScreenPanelMetal::drawScreen()
{
    RendererOutput output = nds->GPU.GetRendererOutput(); // getter only
    if (output.Kind != RendererOutputKind::MetalTexture)
        show explicit error/diagnostic; do not silently masquerade as Metal.

    acquire drawable late;
    encode drawable pass sampling completed output texture array;
    encode UI overlay if needed;
    presentDrawable on command buffer;
    commit;
}
```

---

## 5. Metal best-practice requirements

These are not optional style preferences; they map directly to the current flicker/black-output risks.

### 5.1 Persistent objects

Create and reuse expensive Metal objects:

```text
- MTLDevice
- MTLCommandQueue
- MTLLibrary
- MTLRenderPipelineState
- MTLDepthStencilState
- MTLSamplerState
- persistent render targets
```

Do not create pipelines or long-lived resources in the render loop unless scale/device changed.

### 5.2 One device graph

All renderer-owned textures that the presenter samples must belong to the same `id<MTLDevice>` as the presenter's `CAMetalLayer.device`.

Required change:

```text
Introduce a shared Metal context/device owner.
```

Suggested:

```cpp
class MelonPrimeMetalContext final
{
public:
    id<MTLDevice> Device() const;
    id<MTLCommandQueue> RendererQueue() const;
    id<MTLCommandQueue> PresenterQueue() const; // may be same queue initially
};
```

Rules:

```text
- Do not create independent MTLCreateSystemDefaultDevice() instances in renderer3D, final composer, and presenter without coordination.
- If ScreenPanelMetal owns the layer, set layer.device to the shared device.
- If the renderer is initialized first, presenter must adopt that device.
- Device mismatch must be treated as a fatal Metal-mode setup error, not a per-frame fallback.
```

### 5.3 Command buffers

Target:

```text
- Prefer one renderer command buffer per emulated frame for offscreen work/final composition.
- Presenter command buffer should only blit/draw the completed final texture to the drawable and present it.
- Do not submit command buffers from getters.
```

Short-term stabilization:

```text
- `waitUntilCompleted()` is allowed only while diagnosing black/flicker.
- Every wait must be logged under `MELONPRIME_METAL_DIAG=1` with reason and duration.
```

After correctness:

```text
- Use completion handlers / in-flight semaphore / ring buffering.
- Remove per-frame blocking waits from normal mode.
```

### 5.4 Drawable handling

Presenter must acquire drawables late and release them quickly.

Correct shape:

```text
prepare UI overlay if needed
get RendererOutput
acquire nextDrawable immediately before drawable pass
encode drawable pass
presentDrawable on the command buffer before commit
commit
```

Do not:

```text
- hold CAMetalDrawable across emulator frame work,
- wait for GPU completion before registering presentation,
- do offscreen final composition after acquiring the drawable.
```

### 5.5 Load/store actions

Use load/store intentionally:

```text
- final-output layer render pass that fully covers the target: LoadActionDontCare or Clear, StoreActionStore.
- native 3D target clear: Clear once, then render native polygons in the same command buffer if possible.
- depth/stencil if not sampled later: StoreActionDontCare after correctness is confirmed.
- drawable: StoreActionStore.
```

Avoid accidental flicker from undefined contents by never sampling a texture whose write pass used `StoreActionDontCare`.

### 5.6 Buffering model

Initial stable option:

```text
double buffer final output, block until composition is complete before exposing front texture.
```

Preferred final option:

```text
triple buffer final output / dynamic CPU-upload buffers, with an in-flight semaphore.
```

Do not allocate new per-frame vertex/index buffers forever. The current native 3D path creates buffers per group/frame; that may be acceptable for correctness bring-up, but add a TODO and move to ring/staging buffers after black/flicker is solved.

---

## 6. Existing Metal app / emulator lessons

### 6.1 Dolphin lesson

Dolphin's macOS Metal backend is useful as a product-level reference because it is a native GPU backend, not a CPU-frame wrapper. The relevant lesson is architectural:

```text
Native backend should own GPU resources, produce completed GPU frames, and present those frames through a stable presenter path.
```

Do not copy Dolphin code directly. Copy the separation of concerns:

```text
Video backend builds frames.
Presenter/window layer presents frames.
UI does not assemble emulated output.
Intermediate render targets are not advertised as final output.
```

### 6.2 MetalKit / CAMetalLayer lesson

Even if this project uses Qt + custom `NSView` rather than `MTKView`, the pattern is the same:

```text
Offscreen render work first.
Acquire drawable late.
Encode only display pass into drawable.
Schedule present on command buffer.
Commit.
```

### 6.3 Game-engine lesson

Common Metal game engines use a frame graph / render graph concept:

```text
Scene pass → post/final pass → UI pass → present
```

MelonPrimeDS should map that as:

```text
DS 3D native pass → DS 2D/3D final composite → HUD/OSD overlay → present
```

The final composite is a renderer stage, not a widget stage.

---

## 7. Required patch plan

### Phase A — stop flicker by restoring frame ownership

#### A1. Move final composition out of `GetOutput()`

Current bad pattern:

```text
ScreenPanelMetal::drawScreen()
  → GPU.GetRendererOutput()
    → MetalRenderer::GetOutput()
      → creates command buffer
      → uploads CPU textures
      → composes final output
      → waits
      → flips front buffer
```

Replace with:

```text
emulator frame/VBlank timing
  → MetalRenderer::ComposeFinalOutputForCompletedFrame()
    → creates command buffer
    → composes final output
    → marks completed front buffer

ScreenPanelMetal::drawScreen()
  → GPU.GetRendererOutput()
    → returns completed front texture only
```

Acceptance for A1:

```text
- GetOutput has no commandBuffer calls.
- GetOutput has no replaceRegion calls.
- GetOutput has no waitUntilCompleted calls.
- GetOutput does not change FrontBuffer.
- FrontBuffer changes only in ComposeFinalOutputForCompletedFrame() after successful command completion.
```

#### A2. Add frame serial logging

Add one-shot / rate-limited logs:

```text
[MelonPrime] metal frame: compose frame=<n> back=<b> front=<f> scale=<s> target=<w>x<h>
[MelonPrime] metal frame: present frame=<n> front=<f> texture=<ptr>
```

If the same emulated frame number is composed multiple times, that is a bug.

---

### Phase B — prove where black 3D starts

Add `MELONPRIME_METAL_DIAG=1`.

#### B1. Native 3D readback probe

After native 3D pass completes:

```text
native3D.nonzeroPixels
native3D.firstNonzeroXY
native3D.firstNonzeroBGRA
native3D.checksum
native3D.consideredPolygons
native3D.texturedPolygons
native3D.groups
native3D.draws
native3D.discardedReasonCounts if cheap
```

If draws > 0 and nonzeroPixels == 0:

```text
bug is inside MetalRenderer3D native pass
```

Likely causes to test:

```text
- viewport not set on native 3D pass
- depth compare rejects everything
- clip-space position math wrong
- winding/culling issue if cull state later added
- texture sample returns zero
- fragment alpha discard too aggressive
```

Immediate specific check: set an explicit viewport in `RenderNativeOpaquePolygons()`:

```objc
[encoder setViewport:(MTLViewport){0.0, 0.0,
    static_cast<double>(State->ColorTarget.width),
    static_cast<double>(State->ColorTarget.height), 0.0, 1.0}];
```

Do this even if Metal defaults appear to work. Make the pass explicit.

#### B2. Final texture readback probe

After final composer completes:

```text
final.layer0.nonzeroPixels
final.layer1.nonzeroPixels
final.layer0.checksum
final.layer1.checksum
final.native3DLayer
final.usedNative3D=true/false
```

If native3D is nonzero but final layer is black:

```text
bug is in MetalFinalComposer source selection, texture type, sampler, layer, or shader.
```

#### B3. Presenter probe

If final texture is nonzero but screen is black:

```text
bug is in ScreenPanelMetal presenter shader/layout/layer sampling/drawable path.
```

Add log:

```text
[MelonPrime] metal presenter: source texture type=array layers=2 size=<w>x<h> screenKind=<...> numScreens=<...>
```

---

### Phase C — correct final composition semantics

Current `EngineAUses3D()` / `EngineAOutputLayer()` is too narrow and too guessed.

Do not rely on:

```cpp
dispMode == 1 && bit3 && bit8
GPU.ScreenSwap ? 0 : 1
```

as the full definition of DS final output.

Correct target is to mirror `GLRenderer::RenderScreen()` semantics:

```text
- display mode A
- display mode B
- per-line ScreenSwap
- master brightness A/B
- aux input / VRAM display / FIFO behavior
- native 3D source where Engine A uses 3D
```

Short-term acceptable subset for MPH testing:

```text
- emit logs with all DispCnt/ScreenSwap values
- explicitly state which subset is supported
- fail visibly with diagnostic color if routing is unsupported
```

Do not silently choose black.

Suggested unsupported-routing debug color:

```text
magenta checkerboard = final composer unsupported route
red = native 3D missing/nonzero failure
blue = device mismatch
```

This avoids confusing "black screen" with a normal game frame.

---

### Phase D — use diagnostic colors before real scene

Add temporary diagnostic modes:

```text
MELONPRIME_METAL_DIAG_SOLID_NATIVE3D=1
MELONPRIME_METAL_DIAG_FINAL_LAYERS=1
```

#### D1. Solid native3D test

After clear, before real polygons, draw a fixed colored triangle or full-screen color into native3D.

Expected:

```text
- If solid native3D appears in final output: presenter/final path works; real 3D pass is broken.
- If solid native3D is still black: final composer/presenter path is broken.
```

#### D2. Final layer color test

Make final composer write:

```text
layer 0 = red/green checker
layer 1 = blue/yellow checker
```

Expected:

```text
- both DS screens stable, no flicker
- screen swap/layout visible and predictable
```

Only after this passes, trust real 2D/3D composition debugging.

---

## 8. Specific current-code risks to address

### 8.1 `GetOutput()` currently performs work

This is the top flicker risk. Fix first.

### 8.2 `waitUntilCompleted()` in normal path

Current waits may hide synchronization bugs and cause stutter. During diagnosis they are acceptable, but they must be removed or made debug-only after stability.

### 8.3 split devices

Renderer3D, final composer, and presenter must use the same device. If not, Metal texture sharing is invalid for direct sampling.

### 8.4 `texture2DArray` correctness

Presenter now expects `texture2d_array<float>`. Ensure final output is always `MTLTextureType2DArray` with `arrayLength=2`.

Add assert/log:

```objc
if (tex.textureType != MTLTextureType2DArray || tex.arrayLength < 2) fatal/log error;
```

### 8.5 layer selection vs `screenKind`

Presenter shader samples layer `texcoord.z`; final texture layer mapping must match existing GL presenter's meaning of top/bottom layers.

Do not assume:

```text
layer 0 = top forever
layer 1 = bottom forever
```

without validating against `screenKind[]`, `screenMatrix[]`, and GL final output behavior.

### 8.6 current native pass has no explicit viewport in shown code

Add viewport to native 3D encoder.

### 8.7 alpha/discard policy

Native pass currently discards only almost-zero alpha and draws many translucent-class polygons for bring-up. That is acceptable for visibility, but log alpha class counts:

```text
opaqueAlpha31
translucentAlpha1to30
zeroAlpha
shadowSkipped
lineSkipped
```

If the scene is mostly skipped shadows/lines/translucent, black may be expected until those paths are ported.

---

## 9. Minimal class/API patch shape

### 9.1 Header sketch

```cpp
class MetalRenderer : public SoftRenderer
{
public:
    bool Init() override;
    void SetRenderSettings(RendererSettings& settings) override;
    void VBlank() override;
    RendererOutput GetOutput() override;

private:
    struct MetalSharedContext;
    struct MetalFinalComposer;
    struct MetalFrameCoordinator;

    std::unique_ptr<MetalSharedContext> MetalContext;
    std::unique_ptr<MetalFinalComposer> FinalComposer;
    std::unique_ptr<MetalFrameCoordinator> FrameCoordinator;

    bool ComposeFinalOutputForCompletedFrame();
};
```

### 9.2 `GetOutput()` target

```cpp
RendererOutput MetalRenderer::GetOutput()
{
    if (!FrameCoordinator || !FrameCoordinator->HasCompletedFrame())
        return GetSoftwareFallbackOutput(); // only logs; no hidden fallback unless env-enabled

    return RendererOutput::MetalTexture(FrameCoordinator->CompletedTexture());
}
```

### 9.3 frame-composition target

```cpp
void MetalRenderer::VBlank()
{
    SoftRenderer::VBlank(); // temporary 2D source / HUD support only

    if (!ComposeFinalOutputForCompletedFrame())
        FrameCoordinator->MarkFrameInvalidWithReason(...);
}
```

---

## 10. Acceptance criteria

Do not accept the next push unless all of these are true:

```text
[Architecture]
- GetOutput() is a getter only.
- ScreenPanelMetal does not compose emulated output.
- Front/back flip happens exactly once per completed emulated frame.
- Renderer3D/final composer/presenter use one shared MTLDevice.

[Visual]
- No flicker for at least 2 minutes on title/menu/gameplay.
- 3D region is not black when the scene has nonzero native3D pixels.
- If native3D is black, logs prove whether the failure is native3D, final composer, or presenter.
- Unsupported routes show diagnostic color/log, not silent black.

[Internal resolution]
- 1x logs final target 256x192.
- 2x logs final target 512x384.
- 4x logs final target 1024x768.
- visible Metal output changes sharpness/aliasing with scale.

[Fallback honesty]
- softwareFallback=0 in normal Metal mode.
- Any fallback requires `MELONPRIME_METAL_ALLOW_SOFTWARE_FALLBACK=1` and visible log.

[Performance hygiene]
- no per-frame pipeline creation.
- no per-frame texture reallocation except scale/device change.
- no waitUntilCompleted in normal path after diagnostic phase.
```

---

## 11. Test matrix

### 11.1 Diagnostic order

Run in this order:

```text
1. Metal presenter final-layer color test
2. Solid native3D color test
3. Real native3D nonzero readback test
4. Final compositor real native3D layer readback test
5. Full UI/presenter display test
6. Internal resolution 1x/2x/4x visual test
```

Do not debug real game output before synthetic layer tests pass.

### 11.2 Environment variables

```sh
MELONPRIME_METAL_PERF=1
MELONPRIME_METAL_DIAG=1
MELONPRIME_METAL_DIAG_FINAL_LAYERS=1
MELONPRIME_METAL_DIAG_SOLID_NATIVE3D=1
```

Use only one diagnostic override at a time unless the code explicitly supports combining them.

### 11.3 Required logs

```text
[MelonPrime] metal context: device=<name> registry=shared
[MelonPrime] metal frame: compose frame=<n> back=<b> front=<f> scale=<s> target=<w>x<h>
[MelonPrime] metal 3d diag: nonzero=<n> checksum=<hex> considered=<n> draws=<n>
[MelonPrime] metal final diag: layer0.nonzero=<n> layer1.nonzero=<n> native3DLayer=<0|1> usedNative3D=<0|1>
[MelonPrime] metal presenter: present frame=<n> texture=<ptr> layers=2 size=<w>x<h>
```

---

## 12. What not to do

Do not:

```text
- keep building final output inside GetOutput()
- put more routing logic into ScreenPanelMetal
- use CPU top/bottom complete frame as normal Metal output
- call raw native3D ColorTarget a final screen
- silence black output by falling back to CPU
- rely on `EngineAUses3D()` without proving it matches GL final pass behavior
- keep adding UI features before flicker/black are solved
```

---

## 13. Immediate implementation order for the next push

1. Create/introduce shared Metal context and ensure one device is used across renderer3D/final/presenter.
2. Move final composition out of `GetOutput()` into renderer frame/VBlank timing.
3. Make `GetOutput()` a pure getter returning the last completed final texture.
4. Add diagnostic solid final-layer mode.
5. Add diagnostic solid native3D mode.
6. Add native3D and final texture readback summaries.
7. Add explicit viewport to native 3D render pass.
8. Rework or at least heavily instrument EngineA/ScreenSwap routing against existing GL final-pass semantics.
9. Verify no flicker and non-black 3D before touching performance optimization.
10. Only after correctness, remove blocking waits and move to ring/triple buffering.

---

## 14. Practical bottom line

The current implementation is close enough to expose the right failure, but it is still not architecturally stable.

The next push must be a cleanup/refactor push, not a feature push:

```text
SRP first.
Frame ownership first.
Diagnostics first.
Only then performance.
```

Metal can become the Compute Renderer replacement on macOS, but only if it behaves like a real renderer backend: stable frame lifecycle, GPU-owned final output, honest diagnostics, and no silent software masking.

---

## 15. Execution progress

### 2026-07-10 JST — Phase A complete

- Moved Metal final-output composition out of `MetalRenderer::GetOutput()` and into renderer frame timing via `MetalRenderer::VBlank()`.
- Added `ComposeFinalOutputForCompletedFrame()` as the only path that encodes the final pass, waits for completion, advances `FrontBuffer`, and marks a completed frame.
- Kept `GetOutput()` as a completed-frame getter only; it no longer creates command buffers, uploads CPU textures, waits, or flips buffers.
- Added frame serial metadata to `RendererOutput::MetalTexture()` plus rate-limited compose/present logs:
  - `[MelonPrime] metal frame: compose frame=<n> back=<b> front=<f> scale=<s> target=<w>x<h>`
  - `[MelonPrime] metal frame: present frame=<n> front texture=<ptr>`
- Verified with `cmake --build build-mac-metal-test --parallel 4` on the local Intel Mac Metal-enabled build. Only pre-existing warnings were observed.
- Follow-up guard fix after review: `RendererOutput` frame serial metadata and the Metal output enum are now guarded by `MELONPRIME_DS && MELONPRIME_ENABLE_METAL`, so the default/non-Metal core shape stays upstream-compatible. Verified both `build-mac-metal-test` and `build-mac`, then pushed as `2fe71e20`.

### 2026-07-10 JST — Phase B complete

- Added `MELONPRIME_METAL_DIAG=1` readback probes for native 3D and final two-layer output.
- `MetalRenderer3D` now exposes `Metal3DDiagnostics GetLastDiagnostics() const noexcept`.
- Native 3D diagnostics report non-black pixel count, first non-black XY/BGRA, checksum, considered/textured polygon counts, group count, and draw count:
  - `[MelonPrime] metal 3d diag: nonzero=<n> first=(<x>,<y>) firstBGRA=<b,g,r,a> checksum=<hex> considered=<n> textured=<n> groups=<n> draws=<n> valid=<0|1>`
- Final texture diagnostics report layer 0/1 non-black counts/checksums plus native-3D layer routing:
  - `[MelonPrime] metal final diag: layer0.nonzero=<n> layer1.nonzero=<n> layer0.checksum=<hex> layer1.checksum=<hex> native3DLayer=<0|1> usedNative3D=<0|1> native3D.nonzero=<n> valid=<0|1>/<0|1>`
- Presenter diagnostics now include final texture type, layer count, screenKind, and numScreens, and log an error if the renderer output is not a 2-layer texture array.
- Added an explicit viewport to `MetalRenderer3D::RenderNativeOpaquePolygons()`.
- Verified `cmake --build build-mac-metal-test --parallel 4`; default `cmake --build build-mac --parallel 4` reported no work to do.

### 2026-07-10 JST — Phase C complete

- Added final-composer routing analysis for DispCnt A/B, display modes, ScreenSwap, ScreensEnabled, Engine A 3D bits, Engine A native-3D use, and the chosen native-3D output layer.
- Added route-change logging:
  - `[MelonPrime] metal final route: dispA=<hex> modeA=<n> dispB=<hex> modeB=<n> screenSwap=<0|1> screensEnabled=<0|1> engineA3DBits=<0|1> engineAUses3D=<0|1> native3DLayer=<0|1> supportedSubset=normal2d_plus_engineA_bg0_3d unsupported=<0|1> reason=<text>`
- Short-term supported subset is explicitly documented in the log as `normal2d_plus_engineA_bg0_3d`.
- If Engine A advertises 3D bits outside that supported subset, the affected layer is cleared magenta instead of silently using CPU-complete output.
- If Engine A needs native 3D but no native target exists, the affected layer is cleared red.
- If Engine A needs native 3D but the native texture belongs to the wrong Metal device, the affected layer is cleared blue.
- Verified `cmake --build build-mac-metal-test --parallel 4`; default `cmake --build build-mac --parallel 4` reported no work to do.
