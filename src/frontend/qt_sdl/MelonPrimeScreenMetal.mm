#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL) // scatter-budget-exempt: Metal build-gate, not input dispatch
// MELONPRIME_METAL_HIRES_PRESENT_FILTER_V2
// MELONPRIME_METAL_OUTPUT_LEASE_V1
// MELONPRIME_METAL_PRESENT_RATE_CONTROL_V1
// MELONPRIME_METAL_OPENEMU_NONBLOCKING_PRESENTER_V1

#include "MelonPrimeScreenMetal.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/NSView.h>
#import <AppKit/NSWindow.h>
#import <dispatch/dispatch.h>

#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <QGuiApplication>
#include <QEvent>
#include <QImage>
#include <QMetaObject>
#include <QPainter>
#include <QScreen>
#include <QWindow>

#include "NDS.h"
#include "GPU.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "MelonPrime.h"
#include "MelonPrimeHudRender.h"

#include "MelonPrimeMetalFeatureCheck.h"
#include "GPU_MetalStrictDiagnostics.h"
#include "MetalContext.h"

namespace {

// Byte-identical to the vertex data ScreenPanelGL uploads for kScreenVS/FS
// (Screen.cpp, screenVertexBuffer): position.xy in DS pixel space, then the
// texture-array coordinate consumed by mp_screen_fs. Vertices [0,6) sample
// layer 0, and vertices [6,12) sample layer 1.
constexpr float kScreenVertices[] = {
    0.f,   0.f,    0.f, 0.f, 0.f,
    0.f,   192.f,  0.f, 1.f, 0.f,
    256.f, 192.f,  1.f, 1.f, 0.f,
    0.f,   0.f,    0.f, 0.f, 0.f,
    256.f, 192.f,  1.f, 1.f, 0.f,
    256.f, 0.f,    1.f, 0.f, 0.f,

    0.f,   0.f,    0.f, 0.f, 1.f,
    0.f,   192.f,  0.f, 1.f, 1.f,
    256.f, 192.f,  1.f, 1.f, 1.f,
    0.f,   0.f,    0.f, 0.f, 1.f,
    256.f, 192.f,  1.f, 1.f, 1.f,
    256.f, 0.f,    1.f, 0.f, 1.f,
};

constexpr float kUiVertices[] = {
    0.f, 0.f, 0.f, 0.f,
    0.f, 1.f, 0.f, 1.f,
    1.f, 1.f, 1.f, 1.f,
    0.f, 0.f, 0.f, 0.f,
    1.f, 1.f, 1.f, 1.f,
    1.f, 0.f, 1.f, 0.f,
};

// Keep these in sync with Screen.cpp's local constants. They are intentionally
// duplicated here rather than exposed from Screen.cpp just for this presenter.
constexpr int kMetalOSDMargin = 6;
constexpr int kMetalLogoWidth = 192;

// Match ScreenPanelGL's screen-space-to-clip transform. Qt's QNSView is
// flipped and AppKit propagates that state to a replacement backing layer via
// CAMetalLayer.geometryFlipped. The shared matrix math remains identical to
// kScreenVS/kScreenFS; yFlipSign compensates only the host-layer orientation
// (+1 for a geometry-flipped layer, -1 for the normal Metal convention).
NSString* const kScreenShaderSource =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct VertexIn {\n"
     "    float2 position [[attribute(0)]];\n"
     "    float3 texcoord [[attribute(1)]];\n"
     "};\n"
     "struct VOut {\n"
     "    float4 position [[position]];\n"
     "    float3 texcoord;\n"
     "};\n"
     "struct ScreenUniforms {\n"
     "    float m[6];\n"
     "    float2 screenSize;\n"
     "    float yFlipSign;\n"
     "    float _pad;\n"
     "};\n"
     "vertex VOut mp_screen_vs(VertexIn in [[stage_in]],\n"
     "                         constant ScreenUniforms& u [[buffer(1)]]) {\n"
     "    float2 p = float2(\n"
     "        u.m[0] * in.position.x + u.m[2] * in.position.y + u.m[4],\n"
     "        u.m[1] * in.position.x + u.m[3] * in.position.y + u.m[5]);\n"
     "    p = ((p * 2.0) / u.screenSize) - 1.0;\n"
     "    p.y *= u.yFlipSign;\n"
     "    VOut out;\n"
     "    out.position = float4(p, 0.0, 1.0);\n"
     "    out.texcoord = in.texcoord;\n"
     "    return out;\n"
     "}\n"
     "fragment float4 mp_screen_fs(VOut in [[stage_in]],\n"
     "                             texture2d_array<float> tex [[texture(0)]],\n"
     "                             sampler samp [[sampler(0)]]) {\n"
     "    float4 c = tex.sample(samp, in.texcoord.xy, uint(in.texcoord.z + 0.5));\n"
     "    return float4(c.rgb, 1.0);\n"
     "}\n";

NSString* const kUiShaderSource =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct UiVertexIn {\n"
     "    float2 position [[attribute(0)]];\n"
     "    float2 texcoord [[attribute(1)]];\n"
     "};\n"
     "struct UiVOut {\n"
     "    float4 position [[position]];\n"
     "    float2 texcoord;\n"
     "};\n"
     "struct UiUniforms {\n"
     "    float4 rect;\n"
     "    float2 screenSize;\n"
     "    float yFlipSign;\n"
     "    float _pad;\n"
     "};\n"
     "vertex UiVOut mp_ui_vs(UiVertexIn in [[stage_in]],\n"
     "                        constant UiUniforms& u [[buffer(1)]]) {\n"
     "    float2 p = u.rect.xy + in.position * u.rect.zw;\n"
     "    p = ((p * 2.0) / u.screenSize) - 1.0;\n"
     "    p.y *= u.yFlipSign;\n"
     "    UiVOut out;\n"
     "    out.position = float4(p, 0.0, 1.0);\n"
     "    out.texcoord = in.texcoord;\n"
     "    return out;\n"
     "}\n"
     "fragment float4 mp_ui_fs(UiVOut in [[stage_in]],\n"
     "                         texture2d<float> tex [[texture(0)]],\n"
     "                         sampler samp [[sampler(0)]]) {\n"
     "    return tex.sample(samp, in.texcoord);\n"
     "}\n";

struct ScreenUniforms
{
    float m[6];
    float screenSize[2];
    float yFlipSign;
    float pad;
};
static_assert(sizeof(ScreenUniforms) == 40, "must match the MSL ScreenUniforms layout exactly");

bool MetalDiagEnabled()
{
    static const bool enabled = []() {
        const char* env = std::getenv("MELONPRIME_METAL_DIAG");
        return env && env[0] == '1';
    }();
    return enabled;
}

using PresenterClock = std::chrono::steady_clock;

bool MetalPresenterPerfEnabled()
{
    static const bool enabled = []() {
        const char* env = std::getenv("MELONPRIME_METAL_PERF");
        return env && env[0] == '1';
    }();
    return enabled;
}

double PresenterElapsedMs(PresenterClock::time_point start, PresenterClock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

struct PresenterPerfAccumulator
{
    uint32_t Frames = 0;
    double PresentMs = 0.0;
    uint32_t LastScale = 1;
    uint32_t LastTargetWidth = 0;
    uint32_t LastTargetHeight = 0;
    uint32_t SoftwareFallbackFrames = 0;
};

void SubmitPresenterPerf(int scale, NSUInteger width, NSUInteger height, double presentMs, bool softwareFallback)
{
    if (!MetalPresenterPerfEnabled())
        return;

    static PresenterPerfAccumulator acc;
    acc.Frames++;
    acc.PresentMs += presentMs;
    acc.LastScale = static_cast<uint32_t>(scale);
    acc.LastTargetWidth = static_cast<uint32_t>(width);
    acc.LastTargetHeight = static_cast<uint32_t>(height);
    if (softwareFallback)
        acc.SoftwareFallbackFrames++;

    constexpr uint32_t kReportFrames = 600;
    if (acc.Frames < kReportFrames)
        return;

    const double frames = static_cast<double>(acc.Frames);
    std::fprintf(stderr,
        "[MelonPrime] metal presenter: perf frames=%u presentMs=%.3f "
        "softwareFallback=%u visibleSource=%s scale=%u target=%ux%u\n",
        acc.Frames,
        acc.PresentMs / frames,
        acc.SoftwareFallbackFrames,
        acc.SoftwareFallbackFrames ? "SoftwareFallback" : "MetalFinalTexture",
        acc.LastScale,
        acc.LastTargetWidth,
        acc.LastTargetHeight);

    acc = {};
}

struct UiUniforms
{
    float rect[4];
    float screenSize[2];
    float yFlipSign;
    float pad;
};
static_assert(sizeof(UiUniforms) == 32, "must match the MSL UiUniforms layout exactly");

// OpenEmu deliberately permits only one display command buffer at a time.
// CAMetalLayer::nextDrawable() can otherwise block the emulation/render thread
// for more than one display interval when the compositor or GPU is behind.
// This nonblocking permit drops only the presenter refresh; the emulated frame
// and renderer-owned final texture remain available for the next refresh.
class NonblockingPresenterPermit
{
public:
    explicit NonblockingPresenterPermit(dispatch_semaphore_t semaphore) noexcept
        : semaphore_(semaphore)
        , acquired_(semaphore_ &&
                    dispatch_semaphore_wait(semaphore_, DISPATCH_TIME_NOW) == 0)
    {
    }

    NonblockingPresenterPermit(const NonblockingPresenterPermit&) = delete;
    NonblockingPresenterPermit& operator=(const NonblockingPresenterPermit&) = delete;

    ~NonblockingPresenterPermit()
    {
        releaseNow();
    }

    bool acquired() const noexcept
    {
        return acquired_;
    }

    void releaseOnCommandBufferCompletion(id<MTLCommandBuffer> commandBuffer)
    {
        if (!acquired_ || !semaphore_ || !commandBuffer)
            return;

        dispatch_semaphore_t semaphore = semaphore_;
        acquired_ = false;
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
            dispatch_semaphore_signal(semaphore);
        }];
    }

private:
    void releaseNow() noexcept
    {
        if (!acquired_ || !semaphore_)
            return;
        dispatch_semaphore_signal(semaphore_);
        acquired_ = false;
    }

    dispatch_semaphore_t semaphore_ = nullptr;
    bool acquired_ = false;
};

// MELONPRIME_METAL_OUTPUT_METADATA_VALIDATION_V1
// M1 (full-Metal-ification plan) added Width/Height/ArrayLength/Scale/
// Generation to RendererOutput, but nothing checked them against the actual
// id<MTLTexture> before this: an ERROR log was emitted for a bad array
// texture, then the draw proceeded anyway. Reject here instead so a shader
// never reads a slice that does not exist and a stale/mismatched texture
// never reaches the screen silently.
// MELONPRIME_METAL_OUTPUT_VALIDATION_FAIL_CLOSED_V1: the current (and only)
// producer, GPU_Metal.mm's GetOutput()/AcquireOutputLease(), always
// populates every one of these fields for a MetalTexture output (Width/
// Height/Scale from the live texture, ArrayLength fixed at 2, Generation and
// FrameSerial both starting from 1, never 0). Treating all-zero metadata as
// "not populated yet" (as an earlier version of this function did) is a
// fail-open hole: it accepts exactly the kind of malformed/uninitialized
// output that this validator exists to reject. A future producer that
// legitimately cannot populate one of these fields must be updated alongside
// this check, not accommodated by loosening it back to fail-open.
bool ValidateMetalRendererOutput(
    const melonDS::RendererOutput& output,
    id<MTLTexture> texture,
    id<MTLDevice> expectedDevice,
    const char** outReason)
{
    if (outReason)
        *outReason = nullptr;
    auto fail = [&](const char* reason) -> bool {
        if (outReason)
            *outReason = reason;
        return false;
    };

    // MELONPRIME_METAL_OUTPUT_FAULT_INJECTION_V1 (developer builds): force a
    // named validation failure so fail-closed presenter behaviour can be
    // exercised without hand-corrupting renderer state.
    //   MELONPRIME_METAL_FAULT_INJECT=nil_texture|wrong_device|wrong_type|
    //     wrong_array|wrong_format|sample_count|mipmap|depth|width|height|
    //     scale|generation|serial|producer|pixel_format_meta
    if (const char* fault = std::getenv("MELONPRIME_METAL_FAULT_INJECT"))
    {
        if (std::strcmp(fault, "nil_texture") == 0) return fail("fault:nil_texture");
        if (std::strcmp(fault, "wrong_device") == 0) return fail("fault:wrong_device");
        if (std::strcmp(fault, "wrong_type") == 0) return fail("fault:wrong_type");
        if (std::strcmp(fault, "wrong_array") == 0) return fail("fault:wrong_array");
        if (std::strcmp(fault, "wrong_format") == 0) return fail("fault:wrong_format");
        if (std::strcmp(fault, "sample_count") == 0) return fail("fault:sample_count");
        if (std::strcmp(fault, "mipmap") == 0) return fail("fault:mipmap");
        if (std::strcmp(fault, "depth") == 0) return fail("fault:depth");
        if (std::strcmp(fault, "width") == 0) return fail("fault:width");
        if (std::strcmp(fault, "height") == 0) return fail("fault:height");
        if (std::strcmp(fault, "scale") == 0) return fail("fault:scale");
        if (std::strcmp(fault, "generation") == 0) return fail("fault:generation");
        if (std::strcmp(fault, "serial") == 0) return fail("fault:serial");
        if (std::strcmp(fault, "producer") == 0) return fail("fault:producer");
        if (std::strcmp(fault, "pixel_format_meta") == 0)
            return fail("fault:pixel_format_meta");
    }

    if (!texture) return fail("texture is nil");
    if (texture.device != expectedDevice) return fail("texture.device != presenter device");
    if (texture.textureType != MTLTextureType2DArray) return fail("textureType is not 2DArray");
    if (texture.arrayLength != 2) return fail("texture.arrayLength != 2");
    if (texture.pixelFormat != MTLPixelFormatBGRA8Unorm)
        return fail("texture.pixelFormat != BGRA8Unorm");
    if (texture.sampleCount != 1) return fail("texture.sampleCount != 1");
    if (texture.mipmapLevelCount != 1) return fail("texture.mipmapLevelCount != 1");
    if (texture.depth != 1) return fail("texture.depth != 1");
    if (output.PixelFormat != melonDS::RendererPixelFormat::Bgra8Unorm)
        return fail("output.PixelFormat != Bgra8Unorm");
    if (output.PixelFormat == melonDS::RendererPixelFormat::Bgra8Unorm &&
        texture.pixelFormat != MTLPixelFormatBGRA8Unorm)
        return fail("output.PixelFormat metadata mismatches texture.pixelFormat");
    if (output.ArrayLength != 2) return fail("output.ArrayLength != 2");
    if (output.Width == 0) return fail("output.Width == 0");
    if (output.Height == 0) return fail("output.Height == 0");
    if (output.Scale == 0) return fail("output.Scale == 0");
    if (output.Generation == 0) return fail("output.Generation == 0");
    if (output.FrameSerial == 0) return fail("output.FrameSerial == 0");
    if (output.ProducerId == 0) return fail("output.ProducerId == 0");
    if (output.Width != static_cast<uint32_t>(texture.width))
        return fail("output.Width != texture.width");
    if (output.Height != static_cast<uint32_t>(texture.height))
        return fail("output.Height != texture.height");
    if (output.Width != 256u * output.Scale) return fail("output.Width != 256*Scale");
    if (output.Height != 192u * output.Scale) return fail("output.Height != 192*Scale");
    return true;
}

} // namespace

struct ScreenPanelMetal::Impl
{
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    dispatch_semaphore_t presenterInflightSemaphore = nullptr;
    CAMetalLayer* layer = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    id<MTLRenderPipelineState> uiPipeline = nil;
    id<MTLSamplerState> nearestSampler = nil;
    id<MTLSamplerState> linearSampler = nil;
    id<MTLBuffer> vertexBuffer = nil;
    id<MTLBuffer> uiVertexBuffer = nil;
    id<MTLTexture> uiTex = nil;
    int uiTexW = 0;
    int uiTexH = 0;

    QImage uiOverlay;
    QImage bottomImage;
    // MELONPRIME_METAL_UI_OVERLAY_DIRTY_RECT_V1 (mirrors the GL path's
    // OPT-DR1 contract in MelonPrimeHudScreenCppOverlayOfGl.inc):
    // overlayPrevPainted = region painted into uiOverlay last frame (the only
    // region that needs CPU-side clearing this frame); uiTexContentRect =
    // region of uiTex still holding non-transparent texels (the region a new
    // upload must cover to erase stale content before the quad is drawn).
    QRect overlayPrevPainted;
    QRect uiTexContentRect;

    void* attachedView = nullptr; // weak NSView*, owned by Qt
    QMutex layoutMutex;
    int drawableW = 1;
    int drawableH = 1;
    qreal scale = 1.0;
    float presenterYFlipSign = -1.0f;

    bool resourcesReady = false;
    bool loggedLayerOrientation = false;
    bool loggedFirstDraw = false;
    bool loggedFirstUiOverlay = false;
    bool loggedFirstNativeTextureOutput = false;
    bool loggedLayerReattach = false;
    bool loggedScreenPlacementDiag = false;
    uint64_t lastPresentedFrame = 0;
    // MELONPRIME_METAL_PRESENT_METALTEXTURE_ONLY_V1 (PR-9): "no output of any
    // kind yet" is still legitimate for the first handful of frames, before
    // the visible-output pipeline's own compose command buffers have
    // completed once. This is a short, generic startup window -- it has
    // nothing to do with CpuBgra (which this presenter never accepts as
    // displayable content; see the CpuBgra branch below) and is not extended
    // just because a CpuBgra frame happens to be arriving.
    uint32_t presentedFrameCount = 0;
    // MELONPRIME_METAL_OUTPUT_METADATA_VALIDATION_V1: holds the ring-slot
    // lease of the last frame that passed ValidateMetalRendererOutput(). Kept
    // alive (not released) across frames so the underlying MTLTexture's ring
    // slot cannot be reused by the renderer while we are still presenting it
    // -- exactly the same contract AcquireOutputLease()'s single-frame hold
    // already relies on, just extended across however many frames a Metal
    // renderer goes without producing a new valid one. A Metal-selected
    // renderer with no CpuBgra fallback in production means "retain the
    // previous good frame" needs a real held reference, not a raw pointer.
    melonDS::RendererOutputLease lastGoodMetalLease;
    // MELONPRIME_METAL_OUTPUT_PRODUCER_ID_V1: identity of the producer that
    // created the currently retained lease. Cleared together with the lease
    // whenever the active Metal producer changes. Generation/FrameSerial
    // rollbacks from the same producer are rejected without clearing these
    // (the retained lease remains the last known-good).
    uint64_t lastGoodProducerId = 0;
    uint64_t lastGoodGeneration = 0;
    uint64_t lastGoodFrameSerial = 0;
    bool loggedRetainedLastGoodFrame = false;
    bool loggedGenerationRollback = false;
    bool loggedFrameSerialRollback = false;
    bool loggedMetalNeverProduced = false;
    bool loggedInvalidOutputMetadata = false;
    // MELONPRIME_METAL_PRESENT_METALTEXTURE_ONLY_V1 (PR-9): a Metal-selected
    // renderer's own AcquireOutputLease() (GPU_Metal.mm) never returns
    // CpuBgra -- only MetalTexture or an empty/None lease. A CpuBgra output
    // reaching this presenter while Metal is selected therefore means `Rend`
    // is not actually the MetalRenderer instance the config expects (e.g. a
    // renderer construction/switch race), which is a strict-mode violation:
    // logged once here, never displayed, never uploaded to any CPU-backed
    // screen texture (that path no longer exists in this presenter at all).
    bool loggedCpuBgraRejected = false;

    // Accessed under layoutMutex. CAMetalLayer itself is updated only through
    // a queued GUI-thread invocation.
    bool displaySyncRequested = false;
};

ScreenPanelMetal::ScreenPanelMetal(QWidget* parent)
    : ScreenPanel(parent)
    , m(std::make_unique<Impl>())
{
    // Same "own the native surface directly" attribute set ScreenPanelGL
    // uses; Metal presents to the layer itself, not through Qt's paint
    // engine.
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_KeyCompression, false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(screenGetMinSize(1));
}

ScreenPanelMetal::~ScreenPanelMetal()
{
    // Objective-C members are ARC-managed (see CMakeLists.txt -fobjc-arc for
    // this file); dropping `m` releases device/queue/layer/pipeline/etc.
    // Do not touch `m->layer`'s owning NSView here -- Qt is already tearing
    // the widget down by the time a QWidget destructor body runs, and this
    // presenter never retains the NSView itself.
}

qreal ScreenPanelMetal::devicePixelRatioFromScreenLocal() const
{
    const QWindow* handle = window() ? window()->windowHandle() : nullptr;
    const QScreen* screenForRatio = handle ? handle->screen() : nullptr;
    if (!screenForRatio)
        screenForRatio = QGuiApplication::primaryScreen();
    return screenForRatio ? screenForRatio->devicePixelRatio() : static_cast<qreal>(1);
}

bool ScreenPanelMetal::initMetal()
{
    if (!MelonPrime::Metal::SupportsRequiredBaseline())
    {
        fprintf(stderr, "[MelonPrime] metal presenter: SupportsRequiredBaseline() is false, refusing to init\n");
        return false;
    }

    @autoreleasepool
    {
        // MELONPRIME_METAL_SHARED_CONTEXT_V1 (Phase M2): pull the same
        // process-wide device the renderer uses instead of independently
        // discovering one. On a dual-GPU Mac two separate
        // MTLCreateSystemDefaultDevice() calls are not guaranteed to agree,
        // which is exactly what the device-mismatch guard below exists for.
        m->device = (__bridge id<MTLDevice>)melonDS::MelonPrimeSharedMetalDeviceHandle();
        if (!m->device)
        {
            fprintf(stderr, "[MelonPrime] metal presenter: shared Metal device is nil\n");
            return false;
        }

        m->queue = [m->device newCommandQueue];
        if (!m->queue)
        {
            fprintf(stderr, "[MelonPrime] metal presenter: newCommandQueue failed\n");
            return false;
        }

        m->presenterInflightSemaphore = dispatch_semaphore_create(1);
        if (!m->presenterInflightSemaphore)
        {
            fprintf(stderr, "[MelonPrime] metal presenter: dispatch_semaphore_create failed\n");
            return false;
        }

        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.device = m->device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;

        // CAMetalLayer defaults to display-synchronised presentation. That
        // silently imposed a second ~60Hz limiter even when melonDS's own
        // frame limiter and Screen.VSync were disabled. Start unsynchronised;
        // drawScreen() below enables it only when the configured VSync policy
        // actually requests it.
        if ([layer respondsToSelector:@selector(setDisplaySyncEnabled:)])
            layer.displaySyncEnabled = NO;
        layer.presentsWithTransaction = NO;
        m->displaySyncRequested = false;
        m->layer = layer;
        if (!attachLayerToCurrentViewGuiThread())
            return false;

        NSError* error = nil;
        NSMutableString* shaderSource = [NSMutableString stringWithString:kScreenShaderSource];
        [shaderSource appendString:kUiShaderSource];
        id<MTLLibrary> library = [m->device newLibraryWithSource:shaderSource options:nil error:&error];
        if (!library)
        {
            fprintf(stderr, "[MelonPrime] metal presenter: screen shader compile failed\n");
            return false;
        }

        id<MTLFunction> vertexFn = [library newFunctionWithName:@"mp_screen_vs"];
        id<MTLFunction> fragmentFn = [library newFunctionWithName:@"mp_screen_fs"];
        if (!vertexFn || !fragmentFn)
        {
            fprintf(stderr, "[MelonPrime] metal presenter: screen shader functions missing after compile\n");
            return false;
        }

        MTLVertexDescriptor* vertexDesc = [MTLVertexDescriptor vertexDescriptor];
        vertexDesc.attributes[0].format = MTLVertexFormatFloat2;
        vertexDesc.attributes[0].offset = 0;
        vertexDesc.attributes[0].bufferIndex = 0;
        vertexDesc.attributes[1].format = MTLVertexFormatFloat3;
        vertexDesc.attributes[1].offset = 2 * sizeof(float);
        vertexDesc.attributes[1].bufferIndex = 0;
        vertexDesc.layouts[0].stride = 5 * sizeof(float);
        vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDesc.vertexFunction = vertexFn;
        pipelineDesc.fragmentFunction = fragmentFn;
        pipelineDesc.vertexDescriptor = vertexDesc;
        pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

        m->pipeline = [m->device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
        if (!m->pipeline)
        {
            fprintf(stderr, "[MelonPrime] metal presenter: render pipeline state creation failed\n");
            return false;
        }

        id<MTLFunction> uiVertexFn = [library newFunctionWithName:@"mp_ui_vs"];
        id<MTLFunction> uiFragmentFn = [library newFunctionWithName:@"mp_ui_fs"];
        if (!uiVertexFn || !uiFragmentFn)
        {
            fprintf(stderr, "[MelonPrime] metal presenter: UI shader functions missing after compile\n");
            return false;
        }

        MTLVertexDescriptor* uiVertexDesc = [MTLVertexDescriptor vertexDescriptor];
        uiVertexDesc.attributes[0].format = MTLVertexFormatFloat2;
        uiVertexDesc.attributes[0].offset = 0;
        uiVertexDesc.attributes[0].bufferIndex = 0;
        uiVertexDesc.attributes[1].format = MTLVertexFormatFloat2;
        uiVertexDesc.attributes[1].offset = 2 * sizeof(float);
        uiVertexDesc.attributes[1].bufferIndex = 0;
        uiVertexDesc.layouts[0].stride = 4 * sizeof(float);
        uiVertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* uiPipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
        uiPipelineDesc.vertexFunction = uiVertexFn;
        uiPipelineDesc.fragmentFunction = uiFragmentFn;
        uiPipelineDesc.vertexDescriptor = uiVertexDesc;
        uiPipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        uiPipelineDesc.colorAttachments[0].blendingEnabled = YES;
        uiPipelineDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        uiPipelineDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        uiPipelineDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        uiPipelineDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        uiPipelineDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        uiPipelineDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;

        m->uiPipeline = [m->device newRenderPipelineStateWithDescriptor:uiPipelineDesc error:&error];
        if (!m->uiPipeline)
        {
            fprintf(stderr, "[MelonPrime] metal presenter: UI pipeline state creation failed\n");
            return false;
        }

        m->vertexBuffer = [m->device newBufferWithBytes:kScreenVertices
                                                   length:sizeof(kScreenVertices)
                                                  options:MTLResourceStorageModeShared];
        m->uiVertexBuffer = [m->device newBufferWithBytes:kUiVertices
                                                   length:sizeof(kUiVertices)
                                                  options:MTLResourceStorageModeShared];

        MTLSamplerDescriptor* nearestDesc = [[MTLSamplerDescriptor alloc] init];
        nearestDesc.minFilter = MTLSamplerMinMagFilterNearest;
        nearestDesc.magFilter = MTLSamplerMinMagFilterNearest;
        m->nearestSampler = [m->device newSamplerStateWithDescriptor:nearestDesc];

        MTLSamplerDescriptor* linearDesc = [[MTLSamplerDescriptor alloc] init];
        linearDesc.minFilter = MTLSamplerMinMagFilterLinear;
        linearDesc.magFilter = MTLSamplerMinMagFilterLinear;
        m->linearSampler = [m->device newSamplerStateWithDescriptor:linearDesc];

        // MELONPRIME_METAL_PRESENT_METALTEXTURE_ONLY_V1 (PR-9): this
        // presenter used to also own a CPU-upload BGRA8 2D-array texture
        // (`screenTex`) for a software-composite fallback path. That path is
        // removed: the Metal presenter now only ever binds the
        // renderer-owned MetalTexture obtained through
        // AcquireRendererOutputLease(); it never uploads a DS top/bottom CPU
        // framebuffer of its own.
        m->resourcesReady = (m->pipeline != nil && m->uiPipeline != nil &&
                              m->vertexBuffer != nil && m->uiVertexBuffer != nil &&
                              m->nearestSampler != nil && m->linearSampler != nil);
    }

    if (m->resourcesReady)
    {
        updateDrawableSizeGuiThread();
        fprintf(stderr, "[MelonPrime] metal presenter: initialized drawable=%dx%d scale=%.2f\n",
                m->drawableW, m->drawableH, static_cast<double>(m->scale));
    }

    return m->resourcesReady;
}

bool ScreenPanelMetal::attachLayerToCurrentViewGuiThread()
{
    if (!m->layer)
        return false;

    // winId() on macOS is the NSView*. Qt can recreate it during fullscreen,
    // screen moves, or other native-handle transitions, so reapply the
    // CAMetalLayer whenever the widget reports WinIdChange/visibility changes.
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(static_cast<uintptr_t>(winId()));
    if (!view)
    {
        fprintf(stderr, "[MelonPrime] metal presenter: winId() produced no NSView\n");
        return false;
    }

    view.wantsLayer = YES;
    if (view.layer != m->layer)
    {
        view.layer = m->layer;
        if (m->attachedView && !m->loggedLayerReattach)
        {
            m->loggedLayerReattach = true;
            fprintf(stderr, "[MelonPrime] metal presenter: reattached CAMetalLayer after native view change\n");
        }
    }

    const bool viewFlipped = [view isFlipped];
    const bool geometryFlipped = [m->layer isGeometryFlipped];
    const bool contentsFlipped = [m->layer contentsAreFlipped];
    const float yFlipSign = geometryFlipped ? 1.0f : -1.0f;

    m->layoutMutex.lock();
    m->presenterYFlipSign = yFlipSign;
    m->layoutMutex.unlock();

    if (!m->loggedLayerOrientation)
    {
        m->loggedLayerOrientation = true;
        std::fprintf(stderr,
            "[MelonPrime] metal presenter orientation: view.isFlipped=%d "
            "layer.geometryFlipped=%d layer.contentsAreFlipped=%d yFlipSign=%.1f\n",
            viewFlipped ? 1 : 0,
            geometryFlipped ? 1 : 0,
            contentsFlipped ? 1 : 0,
            static_cast<double>(yFlipSign));
    }

    m->attachedView = (__bridge void*)view;
    return true;
}

void ScreenPanelMetal::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();
    attachLayerToCurrentViewGuiThread();
    updateDrawableSizeGuiThread();
}

bool ScreenPanelMetal::event(QEvent* event)
{
    const bool handled = ScreenPanel::event(event);
    switch (event->type())
    {
    case QEvent::WinIdChange:
    case QEvent::Show:
    case QEvent::WindowStateChange:
        attachLayerToCurrentViewGuiThread();
        updateDrawableSizeGuiThread();
        break;
    default:
        break;
    }
    return handled;
}

void ScreenPanelMetal::updateDrawableSizeGuiThread()
{
    if (!m->layer)
        return;

    const qreal scale = devicePixelRatioFromScreenLocal();
    const int w = std::max(1, static_cast<int>(std::ceil(static_cast<qreal>(width()) * scale)));
    const int h = std::max(1, static_cast<int>(std::ceil(static_cast<qreal>(height()) * scale)));

    m->layoutMutex.lock();
    m->drawableW = w;
    m->drawableH = h;
    m->scale = scale;
    m->layoutMutex.unlock();

    // CALayer property writes belong on the GUI thread; this function is
    // only ever called from initMetal()/setupScreenLayout(), both GUI-thread
    // paths (ScreenPanel::resizeEvent() -> setupScreenLayout()).
    m->layer.contentsScale = scale;
    m->layer.drawableSize = CGSizeMake(w, h);
}

void ScreenPanelMetal::drawScreen()
{
    refreshClipForGameStateChange();

    if (!m->resourcesReady)
        return;

    auto emuThread = emuInstance->getEmuThread();

    // Mirror the OpenGL speed-control policy for the native Metal presenter.
    // The emulator's own limiter remains authoritative. CAMetalLayer VSync is
    // used only when Screen.VSync is enabled and no temporary speed override
    // is active. Fast Forward Toggle/Hold and Slow Motion therefore remove the
    // display-refresh cap immediately and restore the user's VSync setting
    // when released.
    const bool speedOverride =
        emuThread->emuIsActive() &&
        (emuInstance->inputHotkeyDown(HK_FastForward) ||
         emuInstance->fastForwardToggled ||
         emuInstance->inputHotkeyDown(HK_SlowMo) ||
         emuInstance->slowmoToggled);
    const bool configuredVSync =
        emuInstance->getGlobalConfig().GetBool("Screen.VSync");
    const bool desiredDisplaySync = configuredVSync && !speedOverride;

    bool queueDisplaySyncChange = false;
    m->layoutMutex.lock();
    if (m->displaySyncRequested != desiredDisplaySync)
    {
        m->displaySyncRequested = desiredDisplaySync;
        queueDisplaySyncChange = true;
    }
    m->layoutMutex.unlock();

    if (queueDisplaySyncChange)
    {
        QMetaObject::invokeMethod(
            this,
            [this, desiredDisplaySync, configuredVSync, speedOverride]() {
                if (!m || !m->layer)
                    return;
                if ([m->layer respondsToSelector:@selector(setDisplaySyncEnabled:)])
                    m->layer.displaySyncEnabled = desiredDisplaySync ? YES : NO;
                std::fprintf(
                    stderr,
                    "[MelonPrime] metal presenter: displaySync=%d configuredVSync=%d "
                    "speedOverride=%d limitFPS=%d\n",
                    desiredDisplaySync ? 1 : 0,
                    configuredVSync ? 1 : 0,
                    speedOverride ? 1 : 0,
                    emuInstance->doLimitFPS ? 1 : 0);
            },
            Qt::QueuedConnection);
    }

    @autoreleasepool
    {
        m->layoutMutex.lock();
        CAMetalLayer* layer = m->layer;
        const int w = m->drawableW;
        const int h = m->drawableH;
        const qreal scale = m->scale;
        const float yFlipSign = m->presenterYFlipSign;
        m->layoutMutex.unlock();

        if (!layer || w <= 0 || h <= 0)
            return;

        // Match OpenEmu's display policy: never let a backed-up CAMetalLayer
        // stall emulation/audio. If the previous presenter command buffer is
        // still in flight, keep the newest renderer output and skip this draw.
        NonblockingPresenterPermit presenterPermit(m->presenterInflightSemaphore);
        if (!presenterPermit.acquired())
            return;

        m->presentedFrameCount++;
        const auto presentStart = PresenterClock::now();

        if (!m->loggedFirstDraw)
        {
            m->loggedFirstDraw = true;
            fprintf(stderr, "[MelonPrime] metal presenter: first draw drawable=%dx%d scale=%.2f active=%d\n",
                    w, h, static_cast<double>(scale), emuThread->emuIsActive() ? 1 : 0);
        }

        bool hasHudCpuBuffersForFrame = false;
        void* topCpuBufForFrame = nullptr;
        void* bottomCpuBufForFrame = nullptr;
        id<MTLTexture> finalMetalTextureForFrame = nil;
        melonDS::RendererOutputLease rendererOutputLease;

        if (emuThread->emuIsActive())
        {
            auto nds = emuInstance->getNDS();

            rendererOutputLease = nds->GPU.AcquireRendererOutputLease();
            const melonDS::RendererOutput& output = rendererOutputLease.Output;
            const int selectedRenderer =
                emuInstance->getGlobalConfig().GetInt("3D.Renderer");
            const bool metalRendererSelected =
                selectedRenderer == renderer3D_Metal ||
                selectedRenderer == renderer3D_MetalCompute;
            // MELONPRIME_METAL_LEASE_LIFECYCLE_V1: a retained last-known-good
            // texture is only meaningful while Metal is the active renderer.
            // Without this, switching away from Metal (Software/OpenGL) and
            // back, or reconstructing the renderer at a new scale, could
            // present a stale texture from a previous renderer instance the
            // moment Metal becomes selected again, before that instance has
            // produced its own first frame.
            if (!metalRendererSelected)
            {
                m->lastGoodMetalLease.ReleaseNow();
                m->lastGoodProducerId = 0;
                m->lastGoodGeneration = 0;
                m->lastGoodFrameSerial = 0;
            }

            // MELONPRIME_METAL_PRESENT_METALTEXTURE_ONLY_V1 (PR-9): short
            // "still initializing" window -- legitimate only for the first
            // handful of frames, before the visible-output pipeline's own
            // compose command buffers have completed once. Once elapsed with
            // no MetalTexture ever produced, that is an explicit renderer
            // selection failure (see the fallback chain below), not a
            // transient to keep waiting through.
            constexpr uint32_t kStartupGraceFrames = 60;

            if (output.Kind == melonDS::RendererOutputKind::MetalTexture)
            {
                // MELONPRIME_METAL_OUTPUT_ORDERING_CONTRACT_V1: within one
                // producer, Generation and FrameSerial are monotonic.
                // A rollback is rejected (lease released, last-good kept) --
                // never accepted as a new last-good after clearing trackers.
                bool rejectedByOrdering = false;
                if (m->lastGoodProducerId != 0 &&
                    output.ProducerId == m->lastGoodProducerId)
                {
                    if (output.Generation != 0 &&
                        output.Generation < m->lastGoodGeneration)
                    {
                        rendererOutputLease.ReleaseNow();
                        rejectedByOrdering = true;
                        if (!m->loggedGenerationRollback)
                        {
                            m->loggedGenerationRollback = true;
                            fprintf(stderr,
                                    "[MelonPrime] metal presenter: rejected Metal output "
                                    "(generation rollback %llu -> %llu); keeping last "
                                    "known-good (further occurrences not logged)\n",
                                    static_cast<unsigned long long>(m->lastGoodGeneration),
                                    static_cast<unsigned long long>(output.Generation));
                        }
                    }
                    else if (output.Generation == m->lastGoodGeneration &&
                             output.FrameSerial != 0 &&
                             m->lastGoodFrameSerial != 0 &&
                             output.FrameSerial < m->lastGoodFrameSerial)
                    {
                        rendererOutputLease.ReleaseNow();
                        rejectedByOrdering = true;
                        if (!m->loggedFrameSerialRollback)
                        {
                            m->loggedFrameSerialRollback = true;
                            fprintf(stderr,
                                    "[MelonPrime] metal presenter: rejected Metal output "
                                    "(frame serial rollback %llu -> %llu); keeping last "
                                    "known-good (further occurrences not logged)\n",
                                    static_cast<unsigned long long>(m->lastGoodFrameSerial),
                                    static_cast<unsigned long long>(output.FrameSerial));
                        }
                    }
                }

                id<MTLTexture> candidateTexture = (__bridge id<MTLTexture>)output.Top;
                const char* invalidReason = nullptr;
                const bool validated =
                    !rejectedByOrdering &&
                    rendererOutputLease.Context &&
                    ValidateMetalRendererOutput(
                        output, candidateTexture, m->device, &invalidReason);

                if (validated)
                {
                    // Capture metadata before the lease move clears Output on
                    // the source (ReleaseNow / move both zero Output).
                    const uint64_t acceptedProducerId = output.ProducerId;
                    const uint64_t acceptedGeneration = output.Generation;
                    const uint64_t acceptedFrameSerial = output.FrameSerial;

                    // Producer changed: only drop the old lease *after* the new
                    // output has validated, so an invalid first frame from the
                    // new producer does not leave us with neither last-good nor
                    // a usable new texture this frame (M-04).
                    if (m->lastGoodProducerId != 0 &&
                        acceptedProducerId != m->lastGoodProducerId)
                    {
                        m->lastGoodMetalLease.ReleaseNow();
                        m->loggedRetainedLastGoodFrame = false;
                        m->lastPresentedFrame = 0;
                    }

                    // A new valid frame supersedes whatever lease we were
                    // retaining. Safe to release that old one synchronously
                    // right here: NonblockingPresenterPermit above only lets
                    // us reach this point once the *previous* frame's
                    // presenter command buffer has completed, so nothing can
                    // still be sampling the texture that lease was guarding.
                    m->lastGoodMetalLease = std::move(rendererOutputLease);
                    m->lastGoodProducerId = acceptedProducerId;
                    m->lastGoodGeneration = acceptedGeneration;
                    m->lastGoodFrameSerial = acceptedFrameSerial;
                    finalMetalTextureForFrame = candidateTexture;
                    m->loggedRetainedLastGoodFrame = false;

                    if (!m->loggedFirstNativeTextureOutput)
                    {
                        m->loggedFirstNativeTextureOutput = true;
                        fprintf(stderr,
                                "[MelonPrime] metal presenter: source texture type=%lu layers=%zu size=%zux%zu screenKind=%d,%d numScreens=%d visibleSource=MetalFinalTexture scale=%zu softwareFallback=0\n",
                                static_cast<unsigned long>(finalMetalTextureForFrame.textureType),
                                static_cast<size_t>(finalMetalTextureForFrame.arrayLength),
                                static_cast<size_t>(finalMetalTextureForFrame.width),
                                static_cast<size_t>(finalMetalTextureForFrame.height),
                                numScreens > 0 ? screenKind[0] : -1,
                                numScreens > 1 ? screenKind[1] : -1,
                                numScreens,
                                static_cast<size_t>(std::max<NSUInteger>(1, finalMetalTextureForFrame.width / 256)));
                    }
                    if (acceptedFrameSerial != 0 &&
                        acceptedFrameSerial != m->lastPresentedFrame)
                    {
                        m->lastPresentedFrame = acceptedFrameSerial;
                        if (acceptedFrameSerial <= 3 || (acceptedFrameSerial % 600) == 0)
                        {
                            fprintf(stderr,
                                    "[MelonPrime] metal frame: present frame=%llu front texture=%p\n",
                                    static_cast<unsigned long long>(acceptedFrameSerial),
                                    (__bridge void*)finalMetalTextureForFrame);
                        }
                    }

                    hasHudCpuBuffersForFrame = nds->GPU.GetFramebuffers(&topCpuBufForFrame, &bottomCpuBufForFrame) &&
                                               topCpuBufForFrame && bottomCpuBufForFrame;
                }
                else if (!rejectedByOrdering && !m->loggedInvalidOutputMetadata)
                {
                    m->loggedInvalidOutputMetadata = true;
                    fprintf(stderr,
                            "[MelonPrime] metal presenter: rejected Metal output (%s); "
                            "falling back to the last known-good frame instead of "
                            "presenting an unvalidated texture (further occurrences "
                            "not logged individually)\n",
                            invalidReason ? invalidReason : "unknown");
                }
            }
            else if (output.Kind == melonDS::RendererOutputKind::CpuBgra)
            {
                // MELONPRIME_METAL_PRESENT_METALTEXTURE_ONLY_V1 (PR-9): this
                // presenter accepts MetalTexture (or an empty/None lease)
                // only. A Metal-selected renderer's own AcquireOutputLease()
                // (GPU_Metal.mm) never returns CpuBgra -- reaching here with
                // metalRendererSelected true means `Rend` was not actually a
                // MetalRenderer this frame (construction/switch race), a
                // strict-mode violation. There is no longer any CPU-backed
                // screen texture to upload this content into; it is dropped,
                // never displayed, and the fallback chain below decides what
                // to show instead (last known-good MetalTexture, or nothing).
                rendererOutputLease.ReleaseNow();
                if (metalRendererSelected)
                {
                    if (!m->loggedCpuBgraRejected)
                    {
                        m->loggedCpuBgraRejected = true;
                        fprintf(stderr,
                                "[MelonPrime] metal presenter: STRICT VIOLATION rejected "
                                "CpuBgra output while Metal renderer selected -- this "
                                "presenter is MetalTexture-only (PR-9); fallbackReason=%u "
                                "(further occurrences not logged individually)\n",
                                static_cast<unsigned>(output.FallbackReason));
                    }
                    melonDS::MetalStrictGpuOnlyViolation(
                        "MelonPrimeScreenMetal::presentFrame",
                        "Metal renderer selected but AcquireOutputLease produced a "
                        "CpuBgra output; the presenter accepts MetalTexture (or "
                        "empty/None) only and never uploads it to a screen texture");
                }
            }

            if (!finalMetalTextureForFrame)
            {
                // No valid new Metal texture this frame (CpuBgra, if any,
                // was just rejected above without becoming displayable
                // content) -- genuinely nothing new to show (e.g. very early
                // startup, before the first frame of any kind has completed).
                // Prefer the last known-good Metal frame over blanking, but
                // only when that lease still belongs to a tracked producer
                // (ProducerId cleared on renderer switch / emu stop).
                id<MTLTexture> retained =
                    (m->lastGoodProducerId != 0)
                        ? (__bridge id<MTLTexture>)m->lastGoodMetalLease.Output.Top
                        : nil;
                if (retained)
                {
                    finalMetalTextureForFrame = retained;
                    if (!m->loggedRetainedLastGoodFrame)
                    {
                        m->loggedRetainedLastGoodFrame = true;
                        fprintf(stderr,
                                "[MelonPrime] metal presenter: no new output at all "
                                "this frame; retaining last known-good Metal texture "
                                "(further occurrences not logged individually)\n");
                    }
                }
                else if (metalRendererSelected && m->presentedFrameCount > kStartupGraceFrames)
                {
                    // Never produced a single valid MetalTexture frame, and
                    // the startup grace window has elapsed: a genuine,
                    // sustained initialization failure, not a transient.
                    // Fail closed: return below without presenting a new
                    // drawable at all (never substitute stale or
                    // CPU-composited content for a real MetalTexture frame).
                    if (!m->loggedMetalNeverProduced)
                    {
                        m->loggedMetalNeverProduced = true;
                        fprintf(stderr,
                                "[MelonPrime] metal presenter: ERROR no valid MetalTexture "
                                "output produced within the startup grace window; failing "
                                "closed to black\n");
                    }
                    melonDS::MetalStrictGpuOnlyViolation(
                        "MelonPrimeScreenMetal::presentFrame",
                        "Metal renderer selected but no valid MetalTexture output was "
                        "ever produced within the startup grace window");
                }
                // else: still within the startup grace window and nothing
                // retained either -- nothing to present this frame.
            }

            if (!finalMetalTextureForFrame)
                return;

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
        }
        else
        {
            // Emulation is not active (ROM closed, stopped) -- a retained
            // texture from the previous ROM/session must never be shown once
            // a new one starts. See MELONPRIME_METAL_LEASE_LIFECYCLE_V1 above.
            m->lastGoodMetalLease.ReleaseNow();
            m->lastGoodProducerId = 0;
            m->lastGoodGeneration = 0;
            m->lastGoodFrameSerial = 0;
        }

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (!drawable)
            return;

        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.colorAttachments[0].texture = drawable.texture;
        passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLCommandBuffer> cmdBuffer = [m->queue commandBuffer];
        if (!cmdBuffer)
            return;
        id<MTLRenderCommandEncoder> encoder =
            [cmdBuffer renderCommandEncoderWithDescriptor:passDesc];
        if (!encoder)
            return;
        [encoder setViewport:(MTLViewport){0.0, 0.0, static_cast<double>(w), static_cast<double>(h), 0.0, 1.0}];

        // MELONPRIME_METAL_PRESENT_METALTEXTURE_ONLY_V1 (PR-9): the only
        // source this presenter ever binds for the DS screens is the
        // renderer-owned MetalTexture from AcquireRendererOutputLease() (or
        // its retained last known-good). There is no CPU-upload path left
        // here at all -- if `finalMetalTextureForFrame` is nil the function
        // already returned above, before the drawable/encoder were created.
        if (emuThread->emuIsActive() && finalMetalTextureForFrame)
        {
            [encoder setRenderPipelineState:m->pipeline];
            [encoder setVertexBuffer:m->vertexBuffer offset:0 atIndex:0];
            const bool highResolutionSource =
                finalMetalTextureForFrame.width > 256 &&
                finalMetalTextureForFrame.height > 192;
            // A scaled Metal final texture is a supersampled source. Use
            // linear downsampling even when the user disables ordinary
            // 1x screen filtering; nearest downsampling can make 2x/3x/4x
            // appear indistinguishable from native resolution.
            id<MTLSamplerState> sampler =
                (filter || highResolutionSource) ? m->linearSampler : m->nearestSampler;
            [encoder setFragmentSamplerState:sampler atIndex:0];
            [encoder setFragmentTexture:finalMetalTextureForFrame atIndex:0];

            ScreenUniforms uniforms{};
            uniforms.screenSize[0] = static_cast<float>(w) / static_cast<float>(scale);
            uniforms.screenSize[1] = static_cast<float>(h) / static_cast<float>(scale);
            uniforms.yFlipSign = yFlipSign;

            for (int i = 0; i < numScreens; i++)
            {
                for (int c = 0; c < 6; c++)
                    uniforms.m[c] = screenMatrix[i][c];

                [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                             vertexStart:(screenKind[i] == 0 ? 0 : 6)
                             vertexCount:6];
            }
        }

        osdUpdate();

        const int logicalW = std::max(1, static_cast<int>(std::ceil(static_cast<double>(w) / static_cast<double>(scale))));
        const int logicalH = std::max(1, static_cast<int>(std::ceil(static_cast<double>(h) / static_cast<double>(scale))));
        bool overlayHasContent = false;
        QRect overlayCurPainted;

        if (m->uiOverlay.width() != logicalW || m->uiOverlay.height() != logicalH)
        {
            m->uiOverlay = QImage(logicalW, logicalH, QImage::Format_ARGB32_Premultiplied);
            // QImage's constructor leaves the pixels uninitialized; one full
            // clear at (re)allocation replaces the previous per-frame clear.
            m->uiOverlay.fill(Qt::transparent);
            m->overlayPrevPainted = QRect();
        }
        else if (!m->overlayPrevPainted.isEmpty())
        {
            // Mirrors the GL path's OPT-DR1/OPT-HUD-1: only the region painted
            // last frame can hold non-transparent pixels, so a scanline memset
            // of that rect replaces the full-image fill (which is a multi-MB
            // memset per frame at Retina window sizes). Premultiplied
            // transparent is all-zero bytes, so memset(0) is exact.
            const QRect clearRect = m->overlayPrevPainted.intersected(
                QRect(0, 0, m->uiOverlay.width(), m->uiOverlay.height()));
            if (!clearRect.isEmpty())
            {
                const std::size_t clearBytes =
                    static_cast<std::size_t>(clearRect.width()) * 4u;
                for (int y = clearRect.top(); y <= clearRect.bottom(); y++)
                {
                    std::memset(
                        m->uiOverlay.scanLine(y) +
                            static_cast<std::size_t>(clearRect.left()) * 4u,
                        0, clearBytes);
                }
            }
        }

        QPainter overlayPainter(&m->uiOverlay);

#ifdef MELONPRIME_CUSTOM_HUD
        if (emuThread->emuIsActive())
        {
            auto* mp = emuThread->GetMelonPrimeCore();
            // MELONPRIME_MAC_METAL_HUD_INSTANCE_STATE_FIX_V1
            // HUD editor/render/patch state became per-MelonPrimeCore. Keep the
            // Metal presenter on the same instance-owned state as the Qt/GL path.
            const bool editMode =
                mp && MelonPrime::CustomHud_IsEditMode(mp->HudConfigState());
            if (mp && mp->IsRomDetected() && (mp->IsInGame() || editMode))
            {
                auto& instcfg = emuInstance->getLocalConfig();
                const bool hudEnabled = MelonPrime::CustomHud_IsEnabled(instcfg);
                const bool hudVisible = hudEnabled || editMode;
                if (!hudVisible)
                {
                    MelonPrime::CustomHud_EnsurePatchRestored(
                        mp->HudConfigState(),
                        emuInstance, instcfg,
                        mp->GetCurrentRom(), mp->GetPlayerPosition(),
                        mp->IsInGame());
                }
                else
                {
                    if (m->bottomImage.width() != 256 || m->bottomImage.height() != 192)
                        m->bottomImage = QImage(256, 192, QImage::Format_ARGB32_Premultiplied);

                    if (hasHudCpuBuffersForFrame && bottomCpuBufForFrame)
                        std::memcpy(m->bottomImage.bits(), bottomCpuBufForFrame, 256 * 192 * 4);

                    if (overlayFont.family().isEmpty())
                        overlayFont = MelonPrime::CustomHud_ResolveBaseFont(instcfg);
                    overlayFont.setPixelSize(MelonPrime::CustomHud_ResolveFontPixelSize(instcfg));
                    overlayPainter.setFont(overlayFont);

                    const QRect dirty = MelonPrime::CustomHud_Render(
                        mp->HudConfigState(),
                        emuInstance, instcfg,
                        mp->GetCurrentRom(), mp->GetAddrHot(),
                        mp->GetPlayerPosition(),
                        &overlayPainter, nullptr,
                        &m->uiOverlay, hasHudCpuBuffersForFrame ? &m->bottomImage : nullptr,
                        mp->IsInGame(),
                        m_hudTopMatrixValid ? m_topStretchX : 1.0f,
                        m_hudScale,
                        (m_hudScale != 0.0f) ? (m_hudOriginX / m_hudScale) : 0.0f,
                        (m_hudScale != 0.0f) ? (m_hudOriginY / m_hudScale) : 0.0f);
                    overlayHasContent = overlayHasContent || !dirty.isEmpty();
                    overlayCurPainted = overlayCurPainted.united(dirty);
                }
            }
        }
#endif

        if (!emuThread->emuIsActive())
        {
            osdMutex.lock();
            const QRect logoRect(splashPos[3], QSize(kMetalLogoWidth, kMetalLogoWidth));
            overlayPainter.drawPixmap(logoRect, splashLogo);
            overlayCurPainted = overlayCurPainted.united(logoRect);
            for (int i = 0; i < 3; i++)
            {
                overlayPainter.drawImage(splashPos[i], splashText[i].bitmap);
                overlayCurPainted = overlayCurPainted.united(
                    QRect(splashPos[i], splashText[i].bitmap.size()));
            }
            osdMutex.unlock();
            overlayHasContent = true;
        }

#ifdef MELONPRIME_DS
        if (osdEnabled && !osdItems.empty())
#else
        if (osdEnabled)
#endif
        {
            osdMutex.lock();
            uint32_t y = kMetalOSDMargin;
            for (auto it = osdItems.begin(); it != osdItems.end(); )
            {
                OSDItem& item = *it;
                overlayPainter.drawImage(kMetalOSDMargin, y, item.bitmap);
                overlayCurPainted = overlayCurPainted.united(QRect(
                    kMetalOSDMargin, static_cast<int>(y),
                    item.bitmap.width(), item.bitmap.height()));
                y += item.bitmap.height();
                it++;
            }
            osdMutex.unlock();
            overlayHasContent = true;
        }

        overlayPainter.end();

        // Clamp before any texture work: an out-of-bounds replaceRegion is a
        // Metal validation failure, not a silent no-op.
        overlayCurPainted = overlayCurPainted.intersected(
            QRect(0, 0, logicalW, logicalH));
        m->overlayPrevPainted = overlayCurPainted;

        if (overlayHasContent && !overlayCurPainted.isEmpty())
        {
            if (!m->loggedFirstUiOverlay)
            {
                m->loggedFirstUiOverlay = true;
                fprintf(stderr, "[MelonPrime] metal presenter: first UI overlay %dx%d active=%d\n",
                        logicalW, logicalH, emuThread->emuIsActive() ? 1 : 0);
            }

            bool uiTexRealloc = false;
            if (m->uiTexW != logicalW || m->uiTexH != logicalH || !m->uiTex)
            {
                MTLTextureDescriptor* uiDesc =
                    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                        width:logicalW
                                                                       height:logicalH
                                                                    mipmapped:NO];
                uiDesc.usage = MTLTextureUsageShaderRead;
                uiDesc.storageMode = MTLStorageModeShared;
                m->uiTex = [m->device newTextureWithDescriptor:uiDesc];
                m->uiTexW = logicalW;
                m->uiTexH = logicalH;
                uiTexRealloc = true;
            }

            if (m->uiTex)
            {
                // Partial upload, mirroring the GL path's OPT-DR1 contract:
                // cover this frame's painted region plus whatever stale
                // texels the texture still holds (the CPU image is already
                // transparent there, so the same upload erases them). A fresh
                // texture has undefined contents and needs one full upload.
                QRect uploadRect = overlayCurPainted.united(
                    m->uiTexContentRect.intersected(
                        QRect(0, 0, logicalW, logicalH)));
                if (uiTexRealloc)
                    uploadRect = QRect(0, 0, logicalW, logicalH);
                m->uiTexContentRect = overlayCurPainted;

                const uchar* uploadPtr = m->uiOverlay.constBits() +
                    static_cast<std::size_t>(uploadRect.y()) *
                        static_cast<std::size_t>(m->uiOverlay.bytesPerLine()) +
                    static_cast<std::size_t>(uploadRect.x()) * 4u;
                [m->uiTex replaceRegion:MTLRegionMake2D(
                                            uploadRect.x(), uploadRect.y(),
                                            uploadRect.width(), uploadRect.height())
                             mipmapLevel:0
                               withBytes:uploadPtr
                             bytesPerRow:m->uiOverlay.bytesPerLine()];

                UiUniforms uiUniforms{};
                uiUniforms.rect[0] = 0.0f;
                uiUniforms.rect[1] = 0.0f;
                uiUniforms.rect[2] = static_cast<float>(logicalW);
                uiUniforms.rect[3] = static_cast<float>(logicalH);
                uiUniforms.screenSize[0] = static_cast<float>(logicalW);
                uiUniforms.screenSize[1] = static_cast<float>(logicalH);
                uiUniforms.yFlipSign = yFlipSign;

                [encoder setRenderPipelineState:m->uiPipeline];
                [encoder setVertexBuffer:m->uiVertexBuffer offset:0 atIndex:0];
                [encoder setVertexBytes:&uiUniforms length:sizeof(uiUniforms) atIndex:1];
                [encoder setFragmentTexture:m->uiTex atIndex:0];
                [encoder setFragmentSamplerState:m->nearestSampler atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
            }
        }

        [encoder endEncoding];

        // Keep the producer ring slot immutable until this separate presenter
        // queue has finished sampling the texture for both DS screens.
        if (rendererOutputLease.Context && rendererOutputLease.ReleaseFn)
        {
            void* leaseContext = rendererOutputLease.Context;
            void (*leaseRelease)(void*) = rendererOutputLease.ReleaseFn;
            rendererOutputLease.Context = nullptr;
            rendererOutputLease.ReleaseFn = nullptr;
            [cmdBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
                leaseRelease(leaseContext);
            }];
        }

        // Completion, not nextDrawable(), releases the one-frame presenter gate.
        // Every earlier return is covered by NonblockingPresenterPermit's destructor.
        presenterPermit.releaseOnCommandBufferCompletion(cmdBuffer);
        [cmdBuffer presentDrawable:drawable];
        [cmdBuffer commit];

        if (emuThread->emuIsActive() && finalMetalTextureForFrame)
        {
            const NSUInteger sourceW = finalMetalTextureForFrame.width;
            const NSUInteger sourceH = finalMetalTextureForFrame.height;
            const int sourceScale = static_cast<int>(std::max<NSUInteger>(1, sourceW / 256));
            SubmitPresenterPerf(sourceScale, sourceW, sourceH,
                PresenterElapsedMs(presentStart, PresenterClock::now()),
                /*softwareFallback=*/false);
        }
    }
}

#endif // __APPLE__ && MELONPRIME_ENABLE_METAL
