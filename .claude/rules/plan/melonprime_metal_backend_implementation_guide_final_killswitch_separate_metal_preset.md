# MelonPrime Metal Backend Implementation Guide — Final v7 with Master Kill Switch and Separate Metal Preset

**Status:** final audited / low-intrusion revision, 2026-07-09  
**Branch:** `highres_fonts_v3`  
**Target:** macOS Intel + Apple Silicon  
**Primary goal:** add an optional native Metal path for macOS without changing the meaning of the existing `High2` preset.  
**Safety goal:** even if the Metal implementation has a bug, maintainers can rebuild with one flag and get the same behavior as the pre-Metal code path.  
**Non-goal:** do not port the OpenGL compute renderer directly as the first Metal renderer.

---

## 0. Final decision

The Metal addition must be treated as an **experimental, fully removable feature block**.

This is not a user-facing setting and not a TOML/runtime toggle. It is a **build-time master kill switch** so that a bad Metal change can be disabled completely without touching UI, config files, or runtime state.

Required behavior:

```text
Metal disabled build = pre-Metal behavior.
Metal enabled build  = experimental Metal code exists and may be selected only through explicit Metal-only guarded code paths.
```

The default while developing should be conservative:

```text
MELONPRIME_ENABLE_METAL        = OFF by default
MELONPRIME_FORCE_DISABLE_METAL = OFF by default
```

If `MELONPRIME_FORCE_DISABLE_METAL=ON`, it wins over everything.

Preset semantics are fixed:

```text
High2 is OpenGL compute.
Metal is Metal.
Do not make the High2 button select Metal on macOS.
Add a separate Metal button/preset only when Metal support is compiled and stable.
```

---

## 1. Key audit conclusions

| Area | Final decision |
| --- | --- |
| Global disable | Add a build-time master kill switch. When OFF/forced-disabled, no Metal files compile, no Metal frameworks link, no Metal enum exists, no Metal config key is read, and no Metal UI path exists. |
| Default state | Keep Metal OFF by default until Intel Mac + Apple Silicon testing is stable. Developers opt in explicitly. |
| Presentation vs renderer | Split Metal presentation from Metal rendering. `ScreenPanelMetal` presents pixels; `MetalRenderer` produces pixels. Do not combine them. |
| Phase 1 scope | First add safety hardening and a no-behavior presentation split. Do not add `renderer3D_Metal` yet. |
| macOS compute crash | macOS must never construct `GLRenderer(*nds, true)` from stale TOML or invalid UI state. |
| Intel Mac support | Do not require Metal 4, Apple GPU family-only features, MetalFX, argument buffers tier 2, or unified memory. |
| Qt/AppKit | GUI thread owns `NSView`/`CAMetalLayer` mutation. Emu thread may only use retained/snapshotted Metal objects. |
| Layer strategy | Prefer attaching `CAMetalLayer` directly to `ScreenPanelMetal`'s own native `NSView`. Avoid child `NSView` initially because it can steal mouse/focus events. |
| Existing platforms | Windows/Linux remain unchanged. `High2` continues to mean OpenGL compute there. Metal UI is Apple-only and build-flag-gated. |
| macOS with Metal disabled | `High2` remains disabled / pre-Metal macOS behavior. The separate Metal preset/button is absent. No hidden runtime switch should enable Metal. |

---

## 2. Master kill switch contract

### 2.1 CMake options

Add two options:

```cmake
option(MELONPRIME_ENABLE_METAL
    "Enable experimental MelonPrime native Metal presenter/backend on macOS"
    OFF)

option(MELONPRIME_FORCE_DISABLE_METAL
    "Force-disable all MelonPrime Metal code, even if MELONPRIME_ENABLE_METAL is ON"
    OFF)
```

Resolve a single internal active flag:

```cmake
set(MELONPRIME_METAL_ACTIVE OFF)

if (MELONPRIME_FORCE_DISABLE_METAL)
    set(MELONPRIME_METAL_ACTIVE OFF)
elseif (MELONPRIME_ENABLE_METAL)
    if (NOT APPLE)
        message(FATAL_ERROR "MELONPRIME_ENABLE_METAL is only supported on Apple platforms")
    endif()
    set(MELONPRIME_METAL_ACTIVE ON)
endif()
```

Only when active:

```cmake
if (MELONPRIME_METAL_ACTIVE)
    target_compile_definitions(melonDS PRIVATE MELONPRIME_ENABLE_METAL=1)
endif()
```

Do **not** define `MELONPRIME_ENABLE_METAL` when the feature is disabled.

### 2.2 Disabled build requirements

When `MELONPRIME_METAL_ACTIVE` is false:

```text
No Metal `.mm` files are added to the target.
No Metal / QuartzCore / MetalKit framework is linked for this feature.
No `ScreenPanelMetal` declaration is visible to `Window.cpp`.
No `MelonPrimeMetalFeatureCheck` code is compiled.
No `MelonPrimeMetalDevice` code is compiled.
No `MelonPrimeGPU_Metal` code is compiled.
No `renderer3D_Metal` enum value exists.
No `case renderer3D_Metal` exists.
No Metal-specific UI label, tooltip, localization key, menu item, or preset button exists.
No Metal developer config key is read or written.
No stale experimental Metal config can affect startup.
```

### 2.3 Forbidden patterns

Do not gate Metal code with only `__APPLE__`:

```cpp
#ifdef __APPLE__
    // BAD: Metal symbols/config/UI may leak into disabled build.
#endif
```

Do not gate `renderer3D_Metal` with only `MELONPRIME_ENABLE_METAL` if the renderer is not ready:

```cpp
#ifdef MELONPRIME_ENABLE_METAL
    renderer3D_Metal // BAD until the renderer shell exists and is stable enough to construct safely.
#endif
```

Correct hierarchy:

```cpp
#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    // Metal presenter / feature probe may exist.
#endif

#if defined(MELONPRIME_DS) && defined(__APPLE__) && \
    defined(MELONPRIME_ENABLE_METAL) && defined(MELONPRIME_HAVE_RENDERER3D_METAL)
    // renderer3D_Metal enum/factory/explicit Metal preset may exist.
#endif
```

### 2.4 Emergency rollback rule

A maintainer must be able to do this:

```text
cmake -DMELONPRIME_FORCE_DISABLE_METAL=ON ...
```

and get a binary where Metal behavior is gone without editing source code.

If disabling the flag still leaves any observable Metal behavior, the implementation is too invasive.

---

## 3. Existing-code constraints that drive the design

### 3.1 Renderer enum is persisted behavior

`renderer3D_Software`, `renderer3D_OpenGL`, and `renderer3D_OpenGLCompute` are integer config values. Do not renumber them.

Future enum must use a stable explicit value:

```cpp
renderer3D_Metal = 3
```

Only add this value after `MELONPRIME_HAVE_RENDERER3D_METAL` exists.

### 3.2 `EmuInstance::usesOpenGL()` is currently too broad

Existing behavior treats this as OpenGL:

```cpp
Screen.UseGL || 3D.Renderer != renderer3D_Software
```

That breaks future Metal because a future `renderer3D_Metal` would accidentally request an OpenGL context.

Replace it with explicit backend policy:

```cpp
bool RendererRequiresOpenGLContext(int renderer)
{
#ifdef OGLRENDERER_ENABLED
    return renderer == renderer3D_OpenGL || renderer == renderer3D_OpenGLCompute;
#else
    return false;
#endif
}
```

Then:

```cpp
bool EmuInstance::usesOpenGL()
{
    const int requested = globalCfg.GetInt("3D.Renderer");
    const int renderer = MelonPrime::NormalizeRendererForPlatform(requested);

    return globalCfg.GetBool("Screen.UseGL") ||
           MelonPrime::RendererRequiresOpenGLContext(renderer);
}
```

When `MELONPRIME_ENABLE_METAL` is disabled, this still behaves like pre-Metal because no Metal renderer value can exist.

### 3.3 `MainWindow::createScreenPanel()` currently has only two paths

Current structure is effectively:

```cpp
hasOGL = Screen.UseGL || 3D.Renderer != renderer3D_Software;
if (hasOGL) panel = new ScreenPanelGL(...);
else        panel = new ScreenPanelNative(...);
```

Metal requires a presentation-backend split, but the split must preserve the exact old behavior when Metal is disabled:

```cpp
#if !defined(MELONPRIME_ENABLE_METAL)
    // Old behavior only: NativeQt or OpenGL.
#endif
```

### 3.4 `EmuThread` currently assumes GL for hardware presentation

Replace `bool useOpenGL` with an explicit active backend:

```cpp
enum class ActiveVideoBackend
{
    NativeQt,
    OpenGL,
#if defined(MELONPRIME_ENABLE_METAL)
    Metal,
#endif
};
```

When Metal is disabled, the `Metal` enumerator can be absent entirely.

---

## 4. SRP architecture

### 4.1 Responsibility table

| Component | Owns | Must not own |
| --- | --- | --- |
| `MelonPrimeVideoBackend.*` | Platform-safe renderer normalization, presentation-backend policy, fallback choices, master-switch-safe config policy. | Rendering commands, Qt widget internals, Metal object lifecycle. |
| `MelonPrimeMetalFeatureCheck.*` | Runtime Metal availability, device name, capability flags. Compiled only when Metal is active. | Widgets, config mutation, drawing. |
| `MelonPrimeMetalDevice.*` | `MTLDevice` and command-queue helpers. Compiled only when Metal is active. | `CAMetalLayer`, Qt events, renderer choice. |
| `MelonPrimeScreenMetal.*` | Per-window Metal presentation: `CAMetalLayer`, drawable size, CPU framebuffer/OSD/HUD presentation. Compiled only when Metal is active. | DS emulation, 3D renderer implementation, preset slots. |
| `MelonPrimeGPU_Metal.*` | Future `melonDS::Renderer` implementation shell. Compiled only when renderer macro exists. | AppKit view/layer mutation, settings UI. |
| `MelonPrimeGPU2D_Metal.*` | Future 2D renderer implementation. | Window presentation, config UI. |
| `MelonPrimeGPU3D_Metal.*` | Future 3D renderer implementation. | Qt events, `CAMetalLayer`, preset-button slot behavior. |
| `MelonPrimeRendererOutput.*` | Explicit renderer output kind and ownership rules. | Objective-C object lifecycle. |
| `MainWindow` seam | Create the correct panel for the resolved presentation backend. | Metal internals. |
| `EmuInstance` seam | Know whether GL context is required; dispatch window operations by active panel type. | Renderer construction details, AppKit mutation. |
| `EmuThread` seam | Frame lifecycle orchestration by active backend. | `NSView` mutation, pipeline creation. |
| `MelonPrimeInputConfig` seam | User-facing presets, separate Metal button, labels, and tooltips. | Renderer factory, feature-probe internals. |

### 4.2 File organization

Do not put Metal internals into `.inc` fragments.

```text
src/MelonPrimeRendererOutput.h
src/MelonPrimeVideoBackend.h
src/MelonPrimeVideoBackend.cpp

src/MelonPrimeMetalFeatureCheck.h
src/MelonPrimeMetalFeatureCheck.mm
src/MelonPrimeMetalDevice.h
src/MelonPrimeMetalDevice.mm

src/MelonPrimeGPU_Metal.h
src/MelonPrimeGPU_Metal.mm
src/MelonPrimeGPU2D_Metal.h
src/MelonPrimeGPU2D_Metal.mm
src/MelonPrimeGPU3D_Metal.h
src/MelonPrimeGPU3D_Metal.mm

src/frontend/qt_sdl/MelonPrimeScreenMetal.h
src/frontend/qt_sdl/MelonPrimeScreenMetal.mm
```

Allowed seam-only `.inc` files:

```text
MelonPrimeWindowScreenPanelFactorySeam.inc
MelonPrimeEmuThreadVideoBackendSeam.inc
```

Never put these into `.inc` files:

```text
MTLDevice ownership
CAMetalLayer ownership
command encoders
texture cache
pipeline state creation
shader source
Objective-C++ lifetime logic
```

### 4.3 Namespace policy

```cpp
namespace melonDS {
class MetalRenderer final : public Renderer { /* ... */ };
class MetalRenderer2D final : public Renderer2D { /* ... */ };
class MetalRenderer3D final : public Renderer3D { /* ... */ };
}

namespace MelonPrime::Metal {
struct FeatureInfo;
const FeatureInfo& CachedFeatureInfo();
bool SupportsRequiredBaseline();
}
```

Reason:

```text
Renderer classes plug into melonDS renderer interfaces.
Metal probes / device helpers / platform policy are MelonPrime-owned support code.
```

---

## 5. Build plumbing

### 5.1 Active source list

Pseudo-CMake:

```cmake
if (MELONPRIME_METAL_ACTIVE)
    target_sources(melonDS PRIVATE
        src/MelonPrimeMetalFeatureCheck.mm
        src/MelonPrimeMetalDevice.mm
        src/frontend/qt_sdl/MelonPrimeScreenMetal.mm
    )

    find_library(METAL_LIB Metal REQUIRED)
    find_library(QUARTZCORE_LIB QuartzCore REQUIRED)
    find_library(APPKIT_LIB AppKit REQUIRED)
    find_library(FOUNDATION_LIB Foundation REQUIRED)

    target_link_libraries(melonDS PRIVATE
        ${METAL_LIB}
        ${QUARTZCORE_LIB}
        ${APPKIT_LIB}
        ${FOUNDATION_LIB}
    )
endif()
```

Do not call `find_library(... REQUIRED)` for Metal frameworks when the active flag is false. A disabled feature must not break CI or local builds.

### 5.2 Objective-C ARC policy

New MelonPrime `.mm` files should use ARC:

```cmake
if (MELONPRIME_METAL_ACTIVE)
    set_source_files_properties(
        src/MelonPrimeMetalFeatureCheck.mm
        src/MelonPrimeMetalDevice.mm
        src/frontend/qt_sdl/MelonPrimeScreenMetal.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )
endif()
```

Rules:

```text
New MelonPrime Metal files: ARC.
Do not import Objective-C headers from public C++ headers.
Use pimpl to hide Objective-C types.
Do not mix Dolphin-style manual retain wrappers unless the whole module is deliberately non-ARC.
```

### 5.3 Disabled-build CI check

Add a CI/job variant or local verification command for macOS:

```text
cmake -DMELONPRIME_FORCE_DISABLE_METAL=ON ...
cmake --build ...
```

Expected:

```text
No Objective-C++ Metal files are compiled.
No direct Metal feature code is linked from MelonPrime.
The app launches with the same NativeQt/OpenGL choices as before.
`High2` remains disabled on macOS exactly as before; a separate Metal preset/button may appear only when stable Metal renderer support is explicitly compiled in.
```

Optional sanity checks:

```text
Search build logs for MelonPrimeScreenMetal.mm — must be absent.
Search build logs for MelonPrimeMetalFeatureCheck.mm — must be absent.
Search symbols for ScreenPanelMetal — must be absent in disabled build.
```

Do not rely solely on framework presence in `otool -L`, because Qt/AppKit dependencies may bring system frameworks indirectly. Prefer source/build-log/symbol checks for MelonPrime-owned Metal code.

---

## 6. Phase 0 — safety hardening before Metal

Goal: eliminate known macOS compute-renderer crash paths before adding new rendering code.

### 6.1 Runtime renderer normalization

Add `src/MelonPrimeVideoBackend.h/.cpp`.

Phase 0 version must not mention `renderer3D_Metal`:

```cpp
namespace MelonPrime {

int NormalizeRendererForPlatform(int requested)
{
#if defined(__APPLE__)
#ifdef OGLRENDERER_ENABLED
    if (requested == renderer3D_OpenGLCompute)
        return renderer3D_OpenGL;
#endif
#endif

    switch (requested)
    {
    case renderer3D_Software:
        return requested;
#ifdef OGLRENDERER_ENABLED
    case renderer3D_OpenGL:
        return requested;
#ifndef __APPLE__
    case renderer3D_OpenGLCompute:
        return requested;
#endif
#endif
    default:
        return renderer3D_Software;
    }
}

bool RendererRequiresOpenGLContext(int renderer)
{
#ifdef OGLRENDERER_ENABLED
    return renderer == renderer3D_OpenGL || renderer == renderer3D_OpenGLCompute;
#else
    return false;
#endif
}

} // namespace MelonPrime
```

Future Metal-aware version must be guarded, but **must not remap OpenGL compute to Metal**:

```cpp
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL) && defined(MELONPRIME_HAVE_RENDERER3D_METAL)
    if (requested == renderer3D_Metal)
        return MelonPrime::Metal::SupportsRequiredBaseline() ? renderer3D_Metal : renderer3D_OpenGL;
#endif
```

Important semantic rule:

```text
renderer3D_OpenGLCompute means OpenGL compute only.
renderer3D_Metal means Metal only.
Do not silently translate High2 / OpenGLCompute into Metal.
```

### 6.2 Use the normalizer before renderer construction

In `EmuThread::updateRenderer()`:

```cpp
const int requestedRenderer = cfg.GetInt("3D.Renderer");
videoRenderer = MelonPrime::NormalizeRendererForPlatform(requestedRenderer);

if (requestedRenderer != videoRenderer)
{
    Platform::Log(Platform::LogLevel::Warn,
        "Renderer %d is unavailable on this platform; using renderer %d.\n",
        requestedRenderer, videoRenderer);
}
```

Then switch on `videoRenderer`.

### 6.3 Replace crash-only switch default

Do not use this for persisted/user-editable config:

```cpp
default: __builtin_unreachable();
```

Use:

```cpp
default:
    Platform::Log(Platform::LogLevel::Error,
        "Invalid 3D renderer %d; falling back to software renderer.\n",
        videoRenderer);
    nds->SetRenderer(std::make_unique<SoftRenderer>(*nds));
    videoRenderer = renderer3D_Software;
    break;
```

### 6.4 Do not save-normalize yet

Runtime-only normalization is safer:

```text
stale config -> safe runtime fallback -> no immediate Config::Save()
```

Startup config mutation is a separate behavior change and should be a separate commit.

### 6.5 Phase 0 acceptance

```text
macOS stale TOML renderer3D_OpenGLCompute does not crash.
macOS stale invalid renderer value does not crash.
Windows/Linux `High2` remains OpenGL compute.
EmuInstance::usesOpenGL() no longer treats every non-software renderer as OpenGL.
No Metal frameworks linked yet.
MELONPRIME_FORCE_DISABLE_METAL=ON build is identical to pre-Metal behavior.
```

---

## 7. Phase 1 — presentation backend split, no Metal behavior yet

Goal: split presentation selection from OpenGL before introducing Metal.

### 7.1 Add backend enum

```cpp
namespace MelonPrime {

enum class PresentationBackend
{
    NativeQt,
    OpenGL,
#if defined(MELONPRIME_ENABLE_METAL)
    Metal,
#endif
};

PresentationBackend ResolvePresentationBackend(Config::Table& cfg);
bool IsOpenGLPresentation(PresentationBackend backend);
bool IsMetalPresentation(PresentationBackend backend);
bool IsHardwarePresentation(PresentationBackend backend);

} // namespace MelonPrime
```

If the Metal enumerator creates too much conditional noise, keep the enum unconditionally but ensure the resolver can never return `Metal` when the master switch is disabled. The stricter compiled-out version is preferred.

### 7.2 Resolver must ignore Metal config when disabled

```cpp
PresentationBackend ResolvePresentationBackend(Config::Table& cfg)
{
#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    // Developer-only experimental path may inspect Metroid.Video.PresentationBackend.
    // It may return PresentationBackend::Metal only after feature probe passes.
#else
    // Metal is compiled out: preserve old behavior.
    return cfg.GetBool("Screen.UseGL")
        ? PresentationBackend::OpenGL
        : PresentationBackend::NativeQt;
#endif
}
```

Do not parse, normalize, or save this key when Metal is disabled:

```text
Metroid.Video.PresentationBackend
```

A stale key copied from an experimental build must have no effect in a disabled build.

### 7.3 Do not overload `Screen.UseGL`

Keep `Screen.UseGL` meaning OpenGL presentation.

For developer-only Metal presenter testing:

```cpp
inline constexpr const char* PresentationBackendKey =
    "Metroid.Video.PresentationBackend"; // 0=Auto, 1=NativeQt, 2=OpenGL, 3=Metal
```

Only read this key when `MELONPRIME_ENABLE_METAL` is compiled in. Do not expose it in public UI until the presenter is stable.

### 7.4 Panel factory change

Refactor `MainWindow::createScreenPanel()` from `hasOGL` to a backend enum.

Recommended fields:

```cpp
MelonPrime::PresentationBackend presentationBackend = MelonPrime::PresentationBackend::NativeQt;
bool hasOGL = false;   // legacy compatibility only
bool hasMetal = false; // compiled only when Metal is active, or always false otherwise
```

Pseudo-flow:

```cpp
presentationBackend = MelonPrime::ResolvePresentationBackend(globalCfg);

switch (presentationBackend)
{
case PresentationBackend::OpenGL:
    create ScreenPanelGL;
    hasOGL = true;
    hasMetal = false;
    break;

#if defined(MELONPRIME_ENABLE_METAL)
case PresentationBackend::Metal:
    create ScreenPanelMetal;
    if (!panelMetal->initMetal())
        fallback to OpenGL or NativeQt according to policy;
    hasOGL = false;
    hasMetal = true;
    break;
#endif

case PresentationBackend::NativeQt:
default:
    create ScreenPanelNative;
    hasOGL = false;
    hasMetal = false;
    break;
}
```

When Metal is disabled, the compiled code must reduce to the existing OpenGL/NativeQt paths.

Do not leave `panel == nullptr` after Metal init failure.

### 7.5 GL-specific functions stay GL-specific

These methods remain GL-only:

```cpp
getOGLContext()
initOpenGL()
deinitOpenGL()
setGLSwapInterval()
makeCurrentGL()
releaseGL()
```

Add Metal equivalents only inside the Metal compile gate:

```cpp
#if defined(MELONPRIME_ENABLE_METAL)
bool MainWindow::hasMetalPresentation() const;
void MainWindow::initMetal();      // GUI-thread only if it mutates NSView/layer
void MainWindow::deinitMetal();    // GUI-thread only if it mutates NSView/layer
void MainWindow::setMetalVSync(bool enabled);
#endif
```

Do not make `makeCurrentGL()` silently do Metal work.

### 7.6 Phase 1 acceptance

```text
Default behavior remains unchanged.
OpenGL path still creates ScreenPanelGL.
Software NativeQt path still creates ScreenPanelNative.
No Metal files are required for this phase to pass.
MELONPRIME_FORCE_DISABLE_METAL=ON build ignores all stale Metal developer config.
```

---

## 8. Phase 2 — Metal feature probe and CMake plumbing

This phase exists only when `MELONPRIME_ENABLE_METAL` is active.

### 8.1 FeatureInfo

```cpp
namespace MelonPrime::Metal {

struct FeatureInfo {
    bool hasDevice = false;
    bool supportsRequiredBaseline = false;
    bool isLowPower = false;
    bool isRemovable = false;
    bool hasUnifiedMemory = false;
    uint64_t recommendedMaxBufferLength = 0;
    std::string deviceName;
    std::string unavailableReason;
};

const FeatureInfo& CachedFeatureInfo();
bool IsRuntimeAvailable();
bool SupportsRequiredBaseline();
void LogFeatureInfoOnce();

} // namespace MelonPrime::Metal
```

### 8.2 Device selection

Start simple:

```objc
id<MTLDevice> device = MTLCreateSystemDefaultDevice();
```

Do not implement low-power/discrete-GPU selection in the first patch. If later needed, add a MelonPrime setting and adapter list. Do not guess.

### 8.3 Dynamic selector checks

When using newer selectors, check first:

```objc
if ([device respondsToSelector:@selector(setShouldMaximizeConcurrentCompilation:)])
    [device setShouldMaximizeConcurrentCompilation:YES];
```

Use this pattern for optional properties/methods that may not exist on older Intel/OCLP systems.

### 8.4 Baseline check

Required baseline:

```text
MTLCreateSystemDefaultDevice() != nil
command queue creation succeeds
BGRA8Unorm drawable path works
basic render pipeline compilation succeeds in init test
```

Do not require:

```text
argumentBuffersSupport == MTLArgumentBuffersTier2
hasUnifiedMemory == true
supportsFamily: Apple*
Metal 4 APIs
```

### 8.5 Phase 2 acceptance

```text
Mac build links Metal frameworks only when MELONPRIME_ENABLE_METAL is active.
Non-Apple builds do not see Objective-C or Metal headers.
MELONPRIME_FORCE_DISABLE_METAL=ON builds do not see Objective-C or Metal headers either.
Feature probe returns a reason string instead of crashing when no device exists.
No renderer enum changes yet.
```

---

## 9. Phase 3 — EmuThread backend lifecycle

### 9.1 Replace `bool useOpenGL`

```cpp
enum class ActiveVideoBackend
{
    NativeQt,
    OpenGL,
#if defined(MELONPRIME_ENABLE_METAL)
    Metal,
#endif
};

ActiveVideoBackend videoBackend = ActiveVideoBackend::NativeQt;
```

When Metal is disabled, no code path may assign `ActiveVideoBackend::Metal`.

### 9.2 Frame-loop rules

```cpp
if (videoBackend == ActiveVideoBackend::OpenGL)
    emuInstance->makeCurrentGL();
```

Never call GL context functions for Metal.

The frame draw call stays centralized:

```cpp
emuInstance->drawScreen();
```

But repaint signal should be NativeQt-only:

```cpp
if (winUpdateCount >= winUpdateFreq && videoBackend == ActiveVideoBackend::NativeQt)
{
    emit windowUpdate();
    winUpdateCount = 0;
}
```

Reason:

```text
ScreenPanelNative::drawScreen() only copies buffers and needs repaint().
ScreenPanelGL::drawScreen() presents directly.
ScreenPanelMetal::drawScreen() presents directly.
```

### 9.3 Fast-forward / VSync handling

Current GL fast-forward logic calls `setVSyncGL(false/true)`. Split it:

```cpp
if (videoBackend == ActiveVideoBackend::OpenGL)
    emuInstance->setVSyncGL(...);
#if defined(MELONPRIME_ENABLE_METAL)
else if (videoBackend == ActiveVideoBackend::Metal)
    emuInstance->setMetalVSync(...);
#endif
```

On Metal, map VSync to `CAMetalLayer.displaySyncEnabled` only when available. If the selector is unavailable, ignore with one log line.

### 9.4 Phase 3 acceptance

```text
Metal path never calls makeCurrentGL/releaseGL/setGLSwapInterval.
NativeQt still emits repaint updates.
OpenGL behavior unchanged.
Fast-forward does not call Metal APIs on non-Metal paths.
MELONPRIME_FORCE_DISABLE_METAL=ON removes all Metal branches from this lifecycle.
```

---

## 10. Phase 4 — ScreenPanelMetal presenter

This entire phase must be behind the master switch:

```cpp
#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
// ScreenPanelMetal declaration and implementation
#endif
```

If the switch is OFF, `ScreenPanelMetal.h` must not be included by `Window.cpp`, and `MainWindow::createScreenPanel()` must not contain a compiled Metal branch.

### 10.1 Scope

Phase 4 is only presentation:

```text
Software renderer CPU BGRA -> ScreenPanelMetal -> CAMetalLayer
```

Do **not** add `renderer3D_Metal` yet.

### 10.2 Class skeleton

```cpp
class ScreenPanelMetal final : public ScreenPanel
{
    Q_OBJECT

public:
    explicit ScreenPanelMetal(QWidget* parent);
    ~ScreenPanelMetal() override;

    bool initMetal();
    void deinitMetal();
    void setSwapIntervalLikeVSync(bool enabled);
    void drawScreen() override;

protected:
    QPaintEngine* paintEngine() const override { return nullptr; }
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupScreenLayout() override;
    void recreateLayerForCurrentWinIdGuiThread();
    void updateDrawableSizeGuiThread();
    bool ensureFrameResourcesRenderThread();

    struct Impl;
    std::unique_ptr<Impl> m;
};
```

Preserve base input/focus behavior:

```cpp
bool ScreenPanelMetal::event(QEvent* event)
{
    if (event->type() == QEvent::WinIdChange)
        recreateLayerForCurrentWinIdGuiThread();
    return ScreenPanel::event(event);
}
```

### 10.3 Impl must be per-window

```objc
struct ScreenPanelMetal::Impl
{
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    CAMetalLayer* layer = nil;

    id<MTLTexture> screenTex[2] = { nil, nil };
    id<MTLRenderPipelineState> screenPipeline = nil;
    id<MTLSamplerState> nearestSampler = nil;
    id<MTLSamplerState> linearSampler = nil;

    int drawableW = 0;
    int drawableH = 0;
    qreal scale = 1.0;
    bool layerReady = false;
    bool resourcesReady = false;
    bool vsync = true;

    QMutex layerMutex;
};
```

Do not use static/global variables for:

```text
CAMetalLayer
current drawable
current command buffer
screen textures
window size
filter mode
layout matrices
```

### 10.4 Layer ownership strategy

Preferred initial strategy: attach `CAMetalLayer` directly to `ScreenPanelMetal`'s own native `NSView`.

Reason:

```text
ScreenPanel already owns MelonPrime mouse/focus/touch behavior.
A child NSView can steal events and break input.
The existing ScreenPanelGL model uses the panel widget itself as the native rendering surface.
```

Direct layer pattern:

```objc
NSView* view = reinterpret_cast<NSView*>(winId());
view.wantsLayer = YES;

CAMetalLayer* layer = [CAMetalLayer layer];
layer.device = device;
layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
layer.framebufferOnly = YES;

view.layer = layer;
```

Mandatory follow-up:

```text
Handle QEvent::WinIdChange.
Reapply layer after native handle changes.
Do not mutate view/layer from the emu thread.
```

Fallback strategy: use a dedicated native child `NSView` only if direct layer replacement is unstable under Qt. If this path is used, explicitly test:

```text
mouse aim
cursor lock/unlock
focus in/out
tablet/touch input
fullscreen transitions
secondary windows
```

### 10.5 GUI-thread and render-thread rules

GUI thread only:

```text
creating/destroying NSView
calling winId() for native handle creation/replacement
setting view.layer
setting view.wantsLayer
changing Qt widget attributes
updating layer.drawableSize from QWidget resize/screen-change events
```

Render thread allowed:

```text
snapshot/retain layer pointer under lock
nextDrawable
command buffer creation
texture upload
render encoder
present + commit
```

Debug assert for GUI-only functions:

```cpp
Q_ASSERT(QThread::currentThread() == qApp->thread());
```

### 10.6 Drawable size / HiDPI

Use Qt logical pixels and Metal drawable pixels separately:

```objc
const qreal scale = devicePixelRatioFromScreen();
const int drawableW = std::max(1, int(std::ceil(width() * scale)));
const int drawableH = std::max(1, int(std::ceil(height() * scale)));

layer.contentsScale = scale;
layer.drawableSize = CGSizeMake(drawableW, drawableH);
```

Update on:

```text
QWidget resize
QEvent::WinIdChange
screen/backing scale change if observed
fullscreen transition
```

### 10.7 CPU framebuffer upload

For Phase 4, use persistent 256x192 textures and upload each frame.

Recommended descriptor:

```objc
MTLTextureDescriptor* desc =
    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                       width:256
                                                      height:192
                                                   mipmapped:NO];
desc.usage = MTLTextureUsageShaderRead;
```

Upload:

```objc
[texture replaceRegion:MTLRegionMake2D(0, 0, 256, 192)
           mipmapLevel:0
             withBytes:data
           bytesPerRow:256 * 4];
```

Do not force unified-memory assumptions. If Intel validation complains or performance is poor, change upload strategy only inside `MelonPrimeScreenMetal.mm`.

### 10.8 Frame draw rules

```objc
@autoreleasepool {
    CAMetalLayer* layer = snapshotLayerSafely();
    if (!layer)
        return;

    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable)
        return;

    id<MTLCommandBuffer> cmd = [queue commandBuffer];
    if (!cmd)
        return;

    // upload CPU buffers, encode screen quads, OSD/HUD overlays as applicable
    [cmd presentDrawable:drawable];
    [cmd commit];
}
```

Rules:

```text
Do not hold drawable across frames.
Do not call waitUntilCompleted per frame.
Do not allocate pipeline/sampler/texture per frame.
Handle nil drawable by skipping that frame, not by crashing.
Use @autoreleasepool around per-frame Objective-C work.
```

### 10.9 Phase 4 acceptance

```text
Metal presenter can be forced only in a Metal-enabled developer build.
No ROM: clear/splash path does not crash.
Software renderer gameplay appears through Metal.
Resize/fullscreen/HiDPI do not crash.
Nearest/linear filter toggles work.
No per-frame pipeline creation.
No per-frame texture creation.
No unbounded autorelease growth.
No GL calls occur in Metal path.
Secondary windows either work or Metal presenter is explicitly limited to main window with a logged fallback.
MELONPRIME_FORCE_DISABLE_METAL=ON removes ScreenPanelMetal completely.
```

---

## 11. Phase 5 — OSD and Custom HUD presenter parity

Do not claim the Metal presenter is generally usable until these are handled or explicitly disabled with a warning.

### 11.1 OSD

Existing OSD paths are implemented separately for NativeQt and GL. Metal needs its own upload/draw path:

```text
OSD QImage bitmap -> MTLTexture cache by OSD item id -> alpha blended quad
```

Rules:

```text
Reuse ScreenPanel::osdUpdate() logic if possible.
Do not call GL OSD code.
Delete Metal OSD textures when OSD items expire.
Use BGRA upload path for QImage ARGB32_Premultiplied where byte order matches.
```

### 11.2 Custom HUD

Custom HUD must be a separate subphase:

```text
Phase 5A: OSD only
Phase 5B: Custom HUD overlay texture upload
Phase 5C: Custom HUD edit panel compatibility
```

Do not mix HUD rendering into `MelonPrimeGPU3D_Metal`. HUD belongs to presenter/compositor side.

---

## 12. Phase 6 — renderer output abstraction

The existing API is ambiguous:

```cpp
bool GetFramebuffers(void** top, void** bottom);
```

Current meaning:

```text
true  -> CPU BGRA buffers
false -> GL path assumes top is OpenGL texture id
```

Metal must not pass Metal objects through that contract.

### 12.1 Add explicit output kind

```cpp
#ifdef MELONPRIME_DS
namespace melonDS {

enum class RendererOutputKind
{
    CpuBGRA,
    OpenGLTexture2DArray,
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL) && defined(MELONPRIME_HAVE_RENDERER3D_METAL)
    MetalTexturePair,
#endif
};

struct RendererOutput
{
    RendererOutputKind kind = RendererOutputKind::CpuBGRA;
    void* top = nullptr;
    void* bottom = nullptr;
    int width = 256;
    int height = 192;
    int scale = 1;
};

} // namespace melonDS
#endif
```

### 12.2 Default bridge

```cpp
#ifdef MELONPRIME_DS
virtual RendererOutput GetRendererOutput()
{
    void* top = nullptr;
    void* bottom = nullptr;
    if (GetFramebuffers(&top, &bottom))
        return { RendererOutputKind::CpuBGRA, top, bottom, 256, 192, 1 };
    return { RendererOutputKind::OpenGLTexture2DArray, top, bottom, 256, 192, 1 };
}
#endif
```

### 12.3 Presenter validation

```text
ScreenPanelGL accepts CpuBGRA and OpenGLTexture2DArray.
ScreenPanelMetal accepts CpuBGRA and, later, MetalTexturePair.
ScreenPanelNative accepts CpuBGRA only.
Wrong kind -> log once, clear black for frame, do not crash.
```

### 12.4 Future Metal texture lifetime rule

When `MetalTexturePair` is added:

```text
Renderer owns the MTLTexture objects.
RendererOutput contains borrowed opaque pointers only.
Presenter must not release them.
Textures remain valid until at least the next renderer SwapBuffers or explicit frame fence.
Presenter must encode and commit before allowing renderer to recycle them.
```

If this is hard to guarantee, do not expose `MetalTexturePair`; continue presenting CPU BGRA until a proper frame ownership object exists.

---

## 13. Phase 7 — Metal renderer shell

Add `renderer3D_Metal` only when an actual renderer class exists and the master switch is active.

### 13.1 Enum

Use an explicit serialized value:

```cpp
enum
{
    renderer3D_Software = 0,
#ifdef OGLRENDERER_ENABLED
    renderer3D_OpenGL = 1,
    renderer3D_OpenGLCompute = 2,
#endif
#if defined(MELONPRIME_DS) && defined(__APPLE__) && \
    defined(MELONPRIME_ENABLE_METAL) && defined(MELONPRIME_HAVE_RENDERER3D_METAL)
    renderer3D_Metal = 3,
#endif
    renderer3D_Max
};
```

Rules:

```text
Do not renumber existing values.
Do not rely on renderer3D_Max for persisted TOML validation.
Do not expose Metal in standard Video Settings as “Compute”.
When MELONPRIME_FORCE_DISABLE_METAL=ON, renderer3D_Metal must not exist.
```

### 13.2 Renderer shell

```cpp
class MetalRenderer final : public Renderer
{
public:
    explicit MetalRenderer(melonDS::NDS& nds);
    ~MetalRenderer() override;

    bool Init() override;
    void Reset() override;
    void Stop() override;
    void SetRenderSettings(RendererSettings& settings) override;

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;
    void VBlank() override;
    void VBlankEnd() override;
    void AllocCapture(u32 bank, u32 start, u32 len) override;
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override;

    bool GetFramebuffers(void** top, void** bottom) override;
#ifdef MELONPRIME_DS
    RendererOutput GetRendererOutput() override;
#endif
};
```

Initial shell may delegate some work to software for correctness, but the public enum must not be enabled until the object owns a real renderer lifecycle and can fail gracefully.

### 13.3 Factory case

```cpp
#if defined(MELONPRIME_DS) && defined(__APPLE__) && \
    defined(MELONPRIME_ENABLE_METAL) && defined(MELONPRIME_HAVE_RENDERER3D_METAL)
case renderer3D_Metal:
    nds->SetRenderer(std::make_unique<melonDS::MetalRenderer>(*nds));
    break;
#endif
```

If `MetalRenderer::Init()` fails, fall back to regular OpenGL or software with a visible OSD/log message. Do not crash and do not continue with a half-initialized renderer.

---

## 14. Phase 8 — port regular OpenGL 3D path first

Port `GLRenderer3D`, not `ComputeRenderer3D`, first.

Reason:

```text
GLRenderer3D maps to render pipelines and draw calls.
ComputeRenderer3D depends on GL 4.3 compute/SSBO/image semantics that need a separate Metal compute design.
Regular renderer parity is easier to validate against existing OpenGL and software output.
```

Suggested port order:

```text
1. clear pass
2. vertex/index upload
3. opaque polygons
4. translucent polygons
5. texture cache
6. edge marking
7. fog/toon final pass
8. display capture
9. BetterPolygons
10. hires coordinates
```

Use existing dirty/coherency APIs:

```cpp
GPU.MakeVRAMFlat_TextureCoherent(...);
GPU.MakeVRAMFlat_TexPalCoherent(...);
GPU.MakeVRAMFlat_ABGCoherent(...);
GPU.MakeVRAMFlat_AOBJCoherent(...);
```

Correctness comes before dirty-region micro-optimization.

---

## 15. Phase 9 — add a separate macOS Metal preset/button

Only after Intel Mac + Apple Silicon renderer parity.

Do **not** repurpose `High2`. `High2` currently means OpenGL compute. Changing it to Metal would make the label, tooltip, localization, saved config meaning, and user expectation ambiguous.

Final UI rule:

```text
Low   = software / low-cost path
High  = regular OpenGL path
High2 = OpenGL compute path
Metal = native Metal path, macOS only, build-flag-gated
```

### 15.1 Keep High2 semantics unchanged

`on_metroidSetVideoQualityToHigh2_clicked()` must remain OpenGL-compute-specific:

```cpp
void MelonPrimeInputConfig::on_metroidSetVideoQualityToHigh2_clicked()
{
    auto& cfg = emuInstance->getGlobalConfig();

#if defined(__APPLE__)
    // macOS OpenGL does not support the existing OpenGL compute path safely.
    // Do not redirect this to Metal: High2 means OpenGL compute.
    return;
#else
    cfg.SetBool("Screen.UseGL", true);
    cfg.SetBool("Screen.VSync", false);
    cfg.SetInt("Screen.VSyncInterval", 1);
    cfg.SetBool("3D.Soft.Threaded", true);
    cfg.SetInt("3D.GL.ScaleFactor", 4);
    cfg.SetBool("3D.GL.BetterPolygons", true);
    cfg.SetInt("3D.Renderer", renderer3D_OpenGLCompute);
#endif
}
```

On macOS, the High2 button should stay disabled with an OpenGL-compute-specific tooltip. Example wording:

```text
High2 uses the OpenGL compute renderer, which is unavailable on macOS.
Use the Metal preset instead if this build includes Metal support.
```

When Metal is force-disabled, omit the second sentence or keep it generic:

```text
High2 uses the OpenGL compute renderer, which is unavailable on macOS.
```

### 15.2 Add a separate Metal button

Add a new MelonPrime Settings video-quality button only when the feature is compiled and the renderer shell exists:

```cpp
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL) && defined(MELONPRIME_HAVE_RENDERER3D_METAL)
    ui->metroidSetVideoQualityToMetal->setVisible(true);
    ui->metroidSetVideoQualityToMetal->setEnabled(MelonPrime::Metal::SupportsRequiredBaseline());
#else
    // Prefer not creating the widget in disabled builds. If the .ui file contains it, hide it.
    ui->metroidSetVideoQualityToMetal->setVisible(false);
    ui->metroidSetVideoQualityToMetal->setEnabled(false);
#endif
```

Preferred label:

```text
Metal
```

Preferred tooltip:

```text
Use the native Metal renderer on macOS. Experimental; intended as the macOS alternative to High2's OpenGL compute path.
```

Do not name this button `High2` or `High2 Metal`. If a quality tier label is needed later, use an explicitly different name such as:

```text
Metal
Metal High
Native Metal
```

### 15.3 Metal button slot

```cpp
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL) && defined(MELONPRIME_HAVE_RENDERER3D_METAL)
void MelonPrimeInputConfig::on_metroidSetVideoQualityToMetal_clicked()
{
    if (!MelonPrime::Metal::SupportsRequiredBaseline())
        return;

    auto& cfg = emuInstance->getGlobalConfig();

    cfg.SetBool("Screen.UseGL", false);
    cfg.SetBool("Screen.VSync", false);
    cfg.SetInt("Screen.VSyncInterval", 1);
    cfg.SetBool("3D.Soft.Threaded", true);

    // Keep using the existing scale/better-polygons keys only if the Metal renderer
    // intentionally consumes them as shared renderer-quality settings. Do not imply
    // these keys are OpenGL-only in the UI text.
    cfg.SetInt("3D.GL.ScaleFactor", 4);
    cfg.SetBool("3D.GL.BetterPolygons", true);

    cfg.SetInt("3D.Renderer", renderer3D_Metal);
}
#endif
```

If the `3D.GL.*` names become misleading for user-facing text, do not rename the config keys in the Metal commit. Treat any config-key migration as a separate compatibility change.

### 15.4 UI and localization rule

Metal strings must be independent localization entries:

```text
Metal
Use the native Metal renderer on macOS.
High2 uses the OpenGL compute renderer, which is unavailable on macOS.
```

Do not reuse the High2 translation for Metal.

### 15.5 Acceptance

```text
macOS with MELONPRIME_FORCE_DISABLE_METAL=ON: no Metal button exists; High2 disabled exactly like the pre-Metal macOS path.
macOS before stable Metal renderer: no Metal button exists, or it is hidden/disabled under compile guard; High2 disabled.
macOS after stable Metal renderer: Metal button exists and selects renderer3D_Metal; High2 remains OpenGL-compute-specific and disabled.
Windows/Linux: High2 continues to select OpenGL compute; Metal button absent.
Stale macOS renderer3D_OpenGLCompute config normalizes to regular OpenGL or software, never Metal.
Stale renderer3D_Metal config selects Metal only when compiled and supported; otherwise it falls back safely.
```

Do not expose a runtime Metal toggle as the emergency safety mechanism. The emergency path is build-time `MELONPRIME_FORCE_DISABLE_METAL=ON`.

Standard Video Settings should get a separate `Metal` option if exposed there. Do not repurpose the existing `Compute` radio button.

---

## 16. Phase 10 — optional Metal compute-style renderer

Only after the regular Metal renderer is correct.

Mapping reminders:

```text
SSBO              -> device buffer
UBO               -> constant buffer
image load/store  -> texture read/write or device buffer
readonly image    -> texture read
writeonly image   -> writable texture or buffer
barrier()         -> threadgroup_barrier only for intra-threadgroup sync
glMemoryBarrier   -> encoder boundary / explicit resource ordering / blit sync for CPU visibility
glDispatchCompute -> dispatchThreads or dispatchThreadgroups
```

Do not assume one OpenGL barrier maps to one Metal call.

---

## 17. Best-practice checklist

### 17.1 Create once

```text
MTLDevice
MTLCommandQueue
MTLLibrary
MTLRenderPipelineState
MTLSamplerState
static quad buffers
persistent screen textures
OSD texture cache
```

### 17.2 Per frame only

```text
@autoreleasepool scope
nextDrawable
command buffer
render pass descriptor
encoder
small uniform updates
present + commit
```

### 17.3 Never per frame

```text
pipeline creation
shader library compilation
sampler creation
screen texture allocation unless size changed
command queue creation
QString/NSString churn for labels in release builds
waitUntilCompleted
```

### 17.4 Intel memory model

```text
Use replaceRegion for CPU texture uploads first.
Use Shared buffers for dynamic CPU-written buffers.
If Managed buffers are used, call didModifyRange before GPU use.
If GPU->CPU readback is added, synchronize with a blit encoder before CPU reads.
Avoid readback in the first Metal renderer.
```

### 17.5 Frame pacing

```text
Do not waitUntilCompleted every frame.
Use a small frames-in-flight limit only if measurements require it.
Handle nil drawable.
Do not hold CAMetalDrawable across frames.
```

### 17.6 Runtime optional APIs

```text
Check respondsToSelector for newer properties/methods.
Do not compile-gate only by macOS SDK version and assume runtime support.
Log unsupported optional features once.
```

---

## 18. Bug-risk checklist before coding

### 18.1 Crashes prevented by design

```text
stale macOS OpenGL compute config -> normalized before renderer factory
invalid renderer integer -> logged software fallback
renderer3D_Metal before enum exists -> compile-gated by MELONPRIME_HAVE_RENDERER3D_METAL
Metal path accidentally calling GL context functions -> ActiveVideoBackend split
NSView mutation on emu thread -> GUI-thread asserts and phase rules
nil drawable -> skip frame
per-frame autorelease growth -> explicit @autoreleasepool
secondary window sharing one global layer -> per-window Impl
Metal regression emergency -> rebuild with MELONPRIME_FORCE_DISABLE_METAL=ON; no runtime config dependency
```

### 18.2 Remaining known risks

| Risk | Mitigation |
| --- | --- |
| Qt overwrites `NSView.layer` after `WinIdChange` / fullscreen / screen move. | Handle `QEvent::WinIdChange`, resize/fullscreen events, and reapply layer on GUI thread. |
| Direct layer replacement breaks Qt background or style behavior. | `ScreenPanelMetal` should follow GL panel attributes: native window, no system background, no Qt paint engine. |
| Child `NSView` fallback steals mouse events. | Keep child view fallback behind a local experiment; test all MelonPrime input paths before adopting. |
| Multi-window + future `MetalTexturePair` causes texture lifetime race. | Keep CPU BGRA output for multi-window until a frame-fence ownership object exists. |
| OSD/HUD missing causes false “it works” reports. | Presenter acceptance requires OSD/HUD status to be explicit. |
| Intel GPU validation errors around storage modes. | Keep upload code localized; start with `replaceRegion`; test with Metal API Validation. |
| Disabled build still reads stale Metal config. | Resolver must not read Metal keys unless `MELONPRIME_ENABLE_METAL` is defined. |
| Disabled build still compiles Objective-C++ files. | CMake source list must be inside `if (MELONPRIME_METAL_ACTIVE)`. |

---

## 19. Testing matrix

### 19.1 Required machines

```text
Intel MacBookPro15,2 / Intel Iris Plus Graphics 655 / x86_64 / macOS 15.x
Apple Silicon M1 or later / arm64 / current macOS
Windows build
Linux build
```

### 19.2 Kill-switch tests

```text
macOS build with MELONPRIME_FORCE_DISABLE_METAL=ON succeeds.
Build log contains no MelonPrimeScreenMetal.mm.
Build log contains no MelonPrimeMetalFeatureCheck.mm.
Binary has no ScreenPanelMetal symbol from MelonPrime.
Stale Metroid.Video.PresentationBackend=Metal has no effect.
Stale `renderer3D_Metal` integer, if copied from an experimental build, falls back safely.
`High2` remains disabled/pre-Metal on macOS and never maps to Metal.
Windows/Linux behavior unchanged.
```

### 19.3 Phase 0 safety tests

```text
macOS stale TOML renderer3D_OpenGLCompute -> regular OpenGL or software fallback, no crash
macOS invalid renderer value -> software fallback, no crash
Windows/Linux `High2` -> OpenGL compute unchanged
EmuInstance::usesOpenGL() returns false for future Metal renderer
```

### 19.4 Metal presenter tests

```text
No-ROM splash / black clear
Software renderer + Metal presenter
Resize
Fullscreen
HiDPI Retina scaling
Screen layout: Natural / Vertical / Horizontal / Hybrid
Screen sizing: Top only / Bottom only / Even
Screen swap
Nearest/linear filtering
OSD messages
Custom HUD overlay
Custom HUD edit mode
In-game top screen only
Secondary window create/close
Window close while emu thread is active
WinIdChange / screen move / display scale change
Mouse aim / cursor lock / Escape unlock
Tablet/touch input
```

### 19.5 Metal renderer tests

```text
Regular gameplay parity vs OpenGL renderer
Texture-heavy rooms
Transparency-heavy scenes
Edge marking
Fog/toon output
Display capture
Savestate load/save around renderer switch
ROM boot/reset/stop/reboot
Fast-forward
Slow motion
Screen Sync Off
Screen Sync glFinish equivalent or documented no-op
Input latency smoke test
```

### 19.6 Debug tools

```text
Xcode Metal API Validation
Metal System Trace in Instruments
MTL_DEBUG_LAYER=1 where applicable
No validation errors during gameplay
No command buffer leaks
No autorelease pool growth
No layer mutation from the wrong thread
```

---

## 20. Acceptance gates

### 20.1 Before merging Phase 0

```text
OpenGL compute cannot be constructed on macOS.
Renderer switch default cannot crash from bad config.
usesOpenGL() no longer means “any non-software renderer”.
Windows/Linux unchanged.
No Metal files are compiled yet.
```

### 20.2 Before merging kill-switch scaffolding

```text
MELONPRIME_ENABLE_METAL defaults OFF.
MELONPRIME_FORCE_DISABLE_METAL overrides ON.
Disabled build compiles no Metal `.mm` files.
Disabled build links no MelonPrime-owned Metal path.
Disabled build ignores stale Metal config.
Disabled build has no Metal UI or renderer enum.
```

### 20.3 Before merging presentation split

```text
OpenGL and NativeQt behavior unchanged.
No Metal code required.
Panel factory is backend-driven, not hasOGL-driven.
Disabled build reduces to NativeQt/OpenGL only.
```

### 20.4 Before merging Metal presenter

```text
ScreenPanelMetal is selected through PresentationBackend, not hasOGL.
Metal presenter works with software CPU buffers.
No GL calls in Metal path.
No Qt/AppKit layer mutation outside GUI thread.
No per-frame pipeline/texture creation.
OSD and Custom HUD status is explicitly documented.
Rebuilding with MELONPRIME_FORCE_DISABLE_METAL=ON removes ScreenPanelMetal and returns to the pre-Metal panel factory path.
```

### 20.5 Before exposing the Metal preset/button

```text
renderer3D_Metal exists and is stable.
Intel Mac renders stable gameplay.
Apple Silicon renders stable gameplay.
A separate Metal button exists on macOS only when compiled and supported.
High2 on macOS remains OpenGL-compute-specific and disabled.
High2 on macOS with MELONPRIME_FORCE_DISABLE_METAL=ON remains disabled/pre-Metal behavior, and the Metal button is absent.
High2 on Windows/Linux still selects OpenGL compute.
Stale macOS OpenGLCompute config normalizes to regular OpenGL or software, never Metal.
Stale renderer3D_Metal config selects Metal only when compiled and supported; otherwise fallback is graceful.
```

---

## 21. Recommended commit sequence

1. **Add Metal master kill switch scaffolding**
   - add `MELONPRIME_ENABLE_METAL` default OFF
   - add `MELONPRIME_FORCE_DISABLE_METAL`
   - ensure disabled builds compile/link no Metal files/frameworks
   - add CI/local build check for disabled mode

2. **Harden macOS renderer normalization**
   - normalize stale compute config
   - remove `__builtin_unreachable()` for renderer config
   - fix `usesOpenGL()` to check actual GL requirement
   - no Metal files yet

3. **Split MelonPrime presentation backend selection**
   - add `PresentationBackend`
   - refactor `MainWindow::createScreenPanel()`
   - preserve GL/NativeQt behavior
   - ignore Metal developer config when Metal is disabled

4. **Add MelonPrime Metal feature probe and CMake plumbing**
   - activate only under `MELONPRIME_ENABLE_METAL`
   - frameworks only when active
   - cached `FeatureInfo`

5. **Make EmuThread video backend aware**
   - replace `bool useOpenGL`
   - split VSync handling
   - NativeQt-only repaint signal
   - no Metal enum branch in disabled builds

6. **Add ScreenPanelMetal skeleton**
   - per-window pimpl
   - direct `NSView` layer lifecycle
   - clear frame
   - removable by kill switch

7. **Present CPU framebuffers through Metal**
   - BGRA upload
   - screen quads
   - HiDPI/layout/filtering

8. **Add Metal OSD and HUD presentation path**
   - OSD texture cache
   - Custom HUD overlay
   - edit mode smoke test

9. **Add explicit renderer output kind**
   - output validation
   - wrong-kind safe black frame

10. **Add MelonPrime Metal renderer shell**
    - `MELONPRIME_HAVE_RENDERER3D_METAL`
    - `renderer3D_Metal = 3`
    - fallback if unavailable

11. **Port regular 3D renderer to Metal**
    - correctness-first `GLRenderer3D` parity

12. **Add separate macOS Metal preset/button**
    - keep High2 as OpenGL compute only
    - add independent Metal label/tooltips/localization
    - stale OpenGLCompute config never maps to Metal
    - stale renderer3D_Metal maps to Metal only when available
    - kill switch removes Metal UI and keeps macOS High2 disabled/pre-Metal

13. **Optional: evaluate Metal compute-style renderer**
    - only after regular Metal renderer parity

---

## 22. Commit message candidates

```text
Add MelonPrime Metal master kill switch
```

```text
Harden macOS renderer selection against OpenGL compute
```

```text
Split MelonPrime presentation backend selection
```

```text
Add MelonPrime Metal feature probe on macOS
```

```text
Make EmuThread video backend aware
```

```text
Add MelonPrime Metal screen panel skeleton
```

```text
Present CPU framebuffers through Metal on macOS
```

```text
Add Metal OSD and HUD presentation path
```

```text
Add explicit renderer output kind for MelonPrime
```

```text
Add MelonPrime Metal renderer shell
```

```text
Add separate MelonPrime Metal preset on macOS
```

---

## 23. Final implementation rule

Keep responsibilities strict:

```text
ScreenPanelMetal presents pixels.
MetalRenderer produces pixels.
MelonPrimeVideoBackend chooses safe backends.
MelonPrimeInputConfig writes user intent only.
MainWindow chooses the panel type.
EmuInstance exposes backend-specific window operations.
EmuThread orchestrates frame lifecycle.
```

Master-switch rule:

```text
Every Metal-related file, enum, branch, feature probe, framework link, and config read must be removable by MELONPRIME_FORCE_DISABLE_METAL=ON.
If disabling the macro still leaves observable Metal behavior, the implementation is too invasive.
```

If one class starts doing two jobs, split it before adding more Metal code.

---

## 24. References reviewed

```text
Apple Developer: Metal overview / Metal 4 support notes.
Qt QWidget documentation: winId() native handle and QEvent::WinIdChange behavior.
Dolphin Emulator: Source/Core/VideoBackends/Metal.
Dolphin Emulator: MTLMain.mm, MTLGfx.mm, CMakeLists.txt.
DagorEngine: drv3d_Metal/metalview.mm.
high_impact: src/render_metal.m.
melonPrimeDS branch highres_fonts_v3: MainWindow::createScreenPanel(), EmuInstance::usesOpenGL(), EmuThread::updateRenderer(), renderer enum in EmuInstance.h.
```
