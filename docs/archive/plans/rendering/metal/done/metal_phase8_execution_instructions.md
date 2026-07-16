# MelonPrimeDS Metal Backend Phase 8 — Execution Instructions

Target branch: `highres_fonts_v3`  
Current audited HEAD: `feed74e8ab7be737cfcdb845512b50325625c939`  
Scope: Metal backend Phase 8 continuation  
Audience: implementation team / execution agent

---

## 1. Current status

The latest pushed work has moved Phase 8 forward substantially, but Phase 8 is still **In progress**.

Current implemented pieces:

- `renderer3D_Metal` exists as a separate renderer identity.
- Metal is still compile-gated and should remain fully removable via the Metal kill switch path.
- High2 remains OpenGL Compute only. Do **not** remap High2 to Metal.
- Metal is not replacing OpenGL. It is a separate native renderer path.
- The previous renderer-routing bug has been fixed: `videoRenderer` should represent the normalized requested renderer, while `useOpenGL` should only represent whether a live OpenGL context is required.
- `VideoSettingsDialog::UsesGL()` / null button guard were hardened.
- `MetalRenderer3D` now has native Metal resource/pipeline scaffolding.
- Native opaque polygon vertex/index upload and rasterization have started.
- Texturing has started through the shared `Texcache<>` template via `TexcacheMetalLoader`.

Current non-goals / not yet ready:

- Do **not** expose Metal in user-facing UI yet.
- Do **not** add the MelonPrime Settings Metal button yet unless explicitly starting Phase 9 after all gates are closed.
- Do **not** add Metal as a visible standard Video Settings option yet unless explicitly starting Phase 9 after all gates are closed.
- Do **not** mark Phase 8 complete.
- Do **not** claim ROM parity.
- Do **not** claim Apple Silicon verification.

---

## 2. Non-negotiable design rules

### 2.1 Metal is not OpenGL replacement

Keep these identities separate:

```text
renderer3D_Software       = Software
renderer3D_OpenGL         = OpenGL
renderer3D_OpenGLCompute  = OpenGL Compute / High2
renderer3D_Metal          = native Metal
```

Never make OpenGL internally become Metal.

Never make High2 select Metal on macOS.

Never make `Screen.UseGL` mean Metal.

### 2.2 High2 remains Compute Shader only

`High2` means OpenGL Compute renderer. On macOS it should remain disabled unless/until OpenGL Compute is actually safe, which it currently is not.

If Metal becomes user-facing later, it must be a separate option:

```text
Low
High
High2
Metal
```

or in standard Video Settings:

```text
Software
OpenGL
OpenGL Compute
Metal
```

### 2.3 Keep Metal fully kill-switchable

All Metal code must remain behind the build-time Metal gate.

Required behavior when Metal is disabled or force-disabled:

- No Metal source files compiled.
- No Metal/QuartzCore framework link for Metal-specific code.
- No `renderer3D_Metal` selection path reachable.
- No Metal UI entries.
- No Metal config path becoming user-visible.
- No Metal symbols or strings in the default binary.
- macOS High2 behavior remains the pre-Metal safe behavior.

### 2.4 Phase 9 is blocked

Do not start Phase 9 / user-facing exposure until all of these are true:

- Phase 8 native renderer path is usable with real ROM execution.
- Integrated code path has been exercised, not only standalone Metal harnesses.
- ROM visual parity has been compared against Software/OpenGL.
- Apple Silicon has been verified.
- Metal presenter + renderer + HUD/OSD paths are confirmed stable.

---

## 3. Immediate next priorities

### Priority 1 — Strengthen Metal feature probe for texturing

The probe was previously strengthened to match the clear pipeline shape. The latest code now adds real texturing through `TexcacheMetalLoader`, so the probe should also cover the new texture requirements.

Add probe coverage for:

```text
MTLTextureType2DArray
MTLPixelFormatRGBA8Uint
texture2d_array<uint>
nearest sampler
fragment shader sampling from uint texture array
pipeline creation with the same basic shape used by the opaque textured pass
```

Acceptance criteria:

- Probe fails safely with a clear reason if `RGBA8Uint` array textures or uint texture sampling are unsupported.
- Probe still passes on Intel Iris Plus 655 if the real path works there.
- Default non-Metal build remains unchanged.
- No user-facing UI change.

Suggested implementation notes:

- Keep the probe minimal. It does not need to draw a full frame.
- It should at least create the texture and compile/create a pipeline that references `texture2d_array<uint>`.
- Prefer a small 1×1 or 2×2 array texture.
- Keep this inside the existing Metal probe file, not in renderer logic.

---

### Priority 2 — Run integrated ROM path

Current verification is mostly no-ROM smoke plus standalone Metal harnesses. The next major gate is to run an actual ROM and confirm the integrated path reaches the new code.

Confirm at minimum:

```text
MetalRenderer::Init()
MetalRenderer3D::Init()
MetalRenderer3D::RenderFrame()
RenderNativeOpaquePolygons()
TexcacheMetalLoader::GenerateTexture()
TexcacheMetalLoader::UploadTexture()
```

Acceptance criteria:

- The app launches a ROM with Metal-enabled test build.
- Forced Metal renderer/presenter path does not crash.
- Logs prove the integrated frame loop reaches the native Metal 3D path.
- If visuals are not yet wired to final display, that limitation must be explicitly logged/documented.
- No default-build behavior change.

Recommended temporary logging:

```text
[MelonPrime] metal renderer3D: first integrated Init
[MelonPrime] metal renderer3D: first RenderFrame
[MelonPrime] metal renderer3D: first opaque pass polygons=<n> textured=<n>
[MelonPrime] metal texcache: first texture upload width=<w> height=<h> layers=<n>
```

Keep logs one-shot to avoid performance noise.

---

### Priority 3 — Do not connect final display output yet without a guarded plan

The native Metal pass is still incomplete. Connecting it to `GetLine()` / final display output too early will make visual regressions user-visible and hard to triage.

Before connecting native Metal output to the final displayed frame, prepare a guarded plan for:

- How CPU/Software delegate output and native Metal output coexist during transition.
- How to compare Software vs Metal output during testing.
- How to disable native output instantly while keeping the renderer object alive.
- How to isolate final composite bugs from polygon/texture bugs.

Recommended approach:

```text
MELONPRIME_FORCE_METAL_RENDERER=1
MELONPRIME_FORCE_METAL_PRESENTER=1
MELONPRIME_METAL_NATIVE_3D_VISIBLE=0/1   # developer-only, if needed
```

Do not make this a normal user setting.

---

### Priority 4 — Implement texture wrap / mirror support

Latest texturing path explicitly uses clamp-to-edge and does not yet implement DS `TexRepeat` / mirror behavior. This is likely to cause visible ROM parity differences.

Implement or plan support for:

```text
repeat S/T
mirror S/T
clamp S/T
DS texture coordinate behavior matching GLRenderer3D::SetupPolygonTexture()
```

Acceptance criteria:

- Behavior is derived from the same texture parameter bits as the OpenGL renderer.
- Test cases cover clamp, repeat, and mirror.
- Any temporary limitation remains explicitly documented if not implemented immediately.

---

### Priority 5 — Continue Phase 8 feature parity incrementally

After the above, continue porting GLRenderer3D behavior in small isolated increments.

Remaining major areas:

```text
translucent polygons
shadow masks / shadows
line polygons
depth-func-equal behavior
BetterPolygons
hi-res scale factor
edge marking
fog
toon/highlight color substitution
VRAM display capture as texture
final composite pass
GetLine/display integration
ROM parity
```

Do not implement these all at once. Each should have its own small verification path and clear “not implemented” documentation until complete.

---

## 4. Specific cautions from latest audit

### 4.1 Opaque polygon grouping may affect parity

The current Metal path groups polygons by `(WBuffer, texture identity)`. This is reasonable for batching, but DS/OpenGL parity may depend on order in edge cases.

Watch for differences involving:

- equal depth
- polygon order dependencies
- translucent/opaque interactions once translucent is added
- attribute buffer behavior
- edge marking later

Do not assume unordered grouping is parity-safe until ROM testing confirms it.

### 4.2 Texturing is partial

The shared `Texcache<>` reuse is a good design choice. However, the current textured path still has explicitly documented gaps:

- no toon/highlight color substitution for blend mode 2
- no repeat/mirror handling yet
- no VRAM-display-capture-as-texture path

Keep these limitations visible in the plan doc and commit messages.

### 4.3 Standalone harnesses are useful but not enough

Standalone Metal harness verification is valuable and should continue, but it is not a replacement for integrated ROM testing.

Use standalone harnesses for:

- shader compile
- pipeline creation
- math validation
- small isolated sampling/blending checks

Use ROM tests for:

- actual `GPU3D::RenderPolygonRAM` traversal
- real texture parameter coverage
- real frame lifecycle
- visual parity
- presenter integration
- performance and stability

---

## 5. Required verification after each new Phase 8 increment

Run at least:

```bash
cmake --build build-mac-metal-test --parallel 4
cmake --build build-mac --parallel 4
```

Run the existing audit suite:

```powershell
./tools/audit-config-defaults.ps1
./tools/check-inc-ownership.ps1
./tools/audit-metroid-literal-budget.ps1 -Budget 1
./tools/audit-platform-scatter-budget.ps1 -Budget 22
./tools/audit-color-dialog-prefs.ps1
./tools/audit-melonprime-srp-performance.ps1
python tools/generate-hud-prop-schema.py
```

Then confirm generated-file diff is empty where expected.

For Metal-enabled binary:

- confirm Metal symbols/strings exist only there.
- confirm forced Metal no-ROM smoke still launches/quits.
- if ROM available, confirm integrated Metal renderer logs are reached.

For default binary:

- confirm no Metal strings leak.
- confirm normal OpenGL/default behavior remains unchanged.

---

## 6. Minimum ROM test plan once ROM is available

Use a known-good MPH ROM setup and compare:

```text
Software renderer baseline
OpenGL renderer baseline
Metal renderer forced path
```

Capture or inspect:

- title/menu rendering
- in-game room with simple opaque geometry
- textured geometry
- repeated/mirrored textures if available
- translucent effects
- foggy areas
- edge-marked objects
- HUD/OSD overlay
- weapon effects
- scan/zoom if relevant
- map/menu screens
```

For each scene, record:

```text
scene name
renderer used
whether Metal visible output is enabled
observations
screenshot/video if possible
known missing feature if difference is expected
unexpected difference if not explained by known missing features
```

Do not claim parity from no-ROM smoke.

---

## 7. Phase 9 gate checklist

Before exposing Metal in standard Video Settings or MelonPrime Settings, all items below must be checked:

```text
[ ] Metal renderer path runs with a real ROM.
[ ] Metal output is actually displayed, not just rendered offscreen.
[ ] Software/OpenGL/Metal visual comparison has been performed.
[ ] Known missing features are either fixed or intentionally accepted behind an experimental label.
[ ] Apple Silicon has been tested.
[ ] Intel Mac has been tested.
[ ] Metal presenter works with HUD/OSD/Custom HUD.
[ ] High2 still maps only to OpenGL Compute.
[ ] OpenGL is still OpenGL.
[ ] Screen.UseGL is not reused for Metal.
[ ] Default build has no Metal code/symbol/string leak.
[ ] MELONPRIME_FORCE_DISABLE_METAL fully removes Metal behavior.
```

If any item is unchecked, do not start Phase 9.

---

## 8. Commit-message guidance

Commit messages should keep the current honest style:

- State exactly what was implemented.
- State what remains unimplemented.
- State what was verified.
- Distinguish standalone harness verification from integrated ROM verification.
- Distinguish no-ROM smoke from actual renderer execution.
- Never call Phase 8 complete until native display integration and ROM parity are actually verified.

Example format:

```text
Metal backend Phase 8: <specific increment>

Implemented:
- ...

Not implemented:
- ...

Verified:
- build-mac-metal-test
- build-mac
- standalone Metal harness: ...
- no-ROM smoke: ...

Not verified:
- ROM integrated path, if applicable
- Apple Silicon, if applicable
```

---

## 9. Final instruction

Continue Phase 8 as a developer-only, compile-gated Metal renderer port.

Do not expose Metal to users yet.

Do not reuse High2.

Do not replace OpenGL.

Do not connect incomplete native Metal output to the visible frame without a guarded developer-only switch and a rollback path.

Prioritize integrated ROM execution, texture probe strengthening, and parity-focused incremental implementation.
