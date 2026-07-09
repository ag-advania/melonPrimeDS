#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL) // scatter-budget-exempt: Metal build-gate, not input dispatch

#include "MelonPrimeScreenMetal.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/NSView.h>
#import <AppKit/NSWindow.h>

#include <cmath>
#include <cstdio>

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#include "NDS.h"
#include "GPU.h"
#include "EmuInstance.h"
#include "EmuThread.h"

#include "MelonPrimeMetalFeatureCheck.h"

namespace {

// Byte-identical to the vertex data ScreenPanelGL uploads for kScreenVS/FS
// (Screen.cpp, screenVertexBuffer): position.xy in DS pixel space, then a
// texcoord whose .z picks the array layer there -- kept here as an unused
// 3rd float so this data (and the 5-float stride) matches 1:1 rather than
// diverging into a second hand-maintained copy. Vertices [0,6) sample the
// "top" texture unit, [6,12) the "bottom" one (selected by which texture is
// bound, not by texcoord.z, since this presenter uses two textures instead
// of a texture array).
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

// Mirrors kScreenVS/kScreenFS (main_shaders.h) 1:1: same affine-then-NDC
// math, same Y flip, so screenMatrix[i] (already computed by
// ScreenPanel::setupScreenLayout()) produces the identical on-screen
// transform as the GL/Native paths.
NSString* const kScreenShaderSource =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct VertexIn {\n"
     "    float2 position [[attribute(0)]];\n"
     "    float3 texcoord [[attribute(1)]];\n"
     "};\n"
     "struct VOut {\n"
     "    float4 position [[position]];\n"
     "    float2 texcoord;\n"
     "};\n"
     "struct ScreenUniforms {\n"
     "    float m[6];\n"
     "    float2 screenSize;\n"
     "};\n"
     "vertex VOut mp_screen_vs(VertexIn in [[stage_in]],\n"
     "                         constant ScreenUniforms& u [[buffer(1)]]) {\n"
     "    float2 p = float2(\n"
     "        u.m[0] * in.position.x + u.m[2] * in.position.y + u.m[4],\n"
     "        u.m[1] * in.position.x + u.m[3] * in.position.y + u.m[5]);\n"
     "    p = ((p * 2.0) / u.screenSize) - 1.0;\n"
     "    p.y *= -1.0;\n"
     "    VOut out;\n"
     "    out.position = float4(p, 0.0, 1.0);\n"
     "    out.texcoord = in.texcoord.xy;\n"
     "    return out;\n"
     "}\n"
     "fragment float4 mp_screen_fs(VOut in [[stage_in]],\n"
     "                             texture2d<float> tex [[texture(0)]],\n"
     "                             sampler samp [[sampler(0)]]) {\n"
     "    float4 c = tex.sample(samp, in.texcoord);\n"
     "    return float4(c.rgb, 1.0);\n"
     "}\n";

struct ScreenUniforms
{
    float m[6];
    float screenSize[2];
};
static_assert(sizeof(ScreenUniforms) == 32, "must match the MSL ScreenUniforms layout exactly");

} // namespace

struct ScreenPanelMetal::Impl
{
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    CAMetalLayer* layer = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    id<MTLSamplerState> nearestSampler = nil;
    id<MTLSamplerState> linearSampler = nil;
    id<MTLBuffer> vertexBuffer = nil;
    id<MTLTexture> screenTex[2] = { nil, nil }; // [0]=top, [1]=bottom

    QMutex layoutMutex;
    int drawableW = 1;
    int drawableH = 1;
    qreal scale = 1.0;

    bool resourcesReady = false;
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
        m->device = MTLCreateSystemDefaultDevice();
        if (!m->device)
        {
            fprintf(stderr, "[MelonPrime] metal presenter: MTLCreateSystemDefaultDevice() returned nil\n");
            return false;
        }

        m->queue = [m->device newCommandQueue];
        if (!m->queue)
        {
            fprintf(stderr, "[MelonPrime] metal presenter: newCommandQueue failed\n");
            return false;
        }

        // Attach CAMetalLayer directly to this widget's own native NSView
        // (winId() on macOS is the NSView* itself), matching how
        // ScreenPanelGL::getWindowInfo() uses winId() for its
        // NSOpenGLContext. No child NSView -- that risks stealing
        // MelonPrime's mouse/focus event handling on the panel.
        NSView* view = (__bridge NSView*)reinterpret_cast<void*>(static_cast<uintptr_t>(winId()));
        if (!view)
        {
            fprintf(stderr, "[MelonPrime] metal presenter: winId() produced no NSView\n");
            return false;
        }
        view.wantsLayer = YES;

        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.device = m->device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        view.layer = layer;
        m->layer = layer;

        NSError* error = nil;
        id<MTLLibrary> library = [m->device newLibraryWithSource:kScreenShaderSource options:nil error:&error];
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

        m->vertexBuffer = [m->device newBufferWithBytes:kScreenVertices
                                                   length:sizeof(kScreenVertices)
                                                  options:MTLResourceStorageModeShared];

        MTLSamplerDescriptor* nearestDesc = [[MTLSamplerDescriptor alloc] init];
        nearestDesc.minFilter = MTLSamplerMinMagFilterNearest;
        nearestDesc.magFilter = MTLSamplerMinMagFilterNearest;
        m->nearestSampler = [m->device newSamplerStateWithDescriptor:nearestDesc];

        MTLSamplerDescriptor* linearDesc = [[MTLSamplerDescriptor alloc] init];
        linearDesc.minFilter = MTLSamplerMinMagFilterLinear;
        linearDesc.magFilter = MTLSamplerMinMagFilterLinear;
        m->linearSampler = [m->device newSamplerStateWithDescriptor:linearDesc];

        // Two persistent 256x192 BGRA8 textures (top/bottom), matching the
        // format ScreenPanelGL's GL_TEXTURE_2D_ARRAY uses. This GPU reports
        // hasUnifiedMemory=true (see MelonPrimeMetalFeatureCheck.mm probe
        // output), so Shared storage is correct here, not just convenient.
        MTLTextureDescriptor* texDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                width:256
                                                               height:192
                                                            mipmapped:NO];
        texDesc.usage = MTLTextureUsageShaderRead;
        texDesc.storageMode = MTLStorageModeShared;
        m->screenTex[0] = [m->device newTextureWithDescriptor:texDesc];
        m->screenTex[1] = [m->device newTextureWithDescriptor:texDesc];

        m->resourcesReady = (m->pipeline != nil && m->vertexBuffer != nil &&
                              m->screenTex[0] != nil && m->screenTex[1] != nil &&
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

void ScreenPanelMetal::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();
    updateDrawableSizeGuiThread();
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

    @autoreleasepool
    {
        m->layoutMutex.lock();
        CAMetalLayer* layer = m->layer;
        const int w = m->drawableW;
        const int h = m->drawableH;
        const qreal scale = m->scale;
        m->layoutMutex.unlock();

        if (!layer || w <= 0 || h <= 0)
            return;

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (!drawable)
            return;

        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.colorAttachments[0].texture = drawable.texture;
        passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLCommandBuffer> cmdBuffer = [m->queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:passDesc];
        [encoder setViewport:(MTLViewport){0.0, 0.0, static_cast<double>(w), static_cast<double>(h), 0.0, 1.0}];

        if (emuThread->emuIsActive())
        {
            auto nds = emuInstance->getNDS();

            void* topBuf = nullptr;
            void* bottomBuf = nullptr;
            const bool hasCpuBuffers = nds->GPU.GetFramebuffers(&topBuf, &bottomBuf);

            // Phase 4 scope is CPU-buffer presentation only (design doc: "no
            // renderer3D_Metal yet"). A hardware (GL-texture) renderer output
            // has no meaning to this presenter -- NormalizeRendererForPlatform
            // already forces Software while the Metal presenter is
            // force-selected (see MelonPrimeVideoBackend.cpp), so this should
            // never actually be false here; skip the frame defensively rather
            // than dereference topBuf as a GLuint*.
            if (hasCpuBuffers)
            {
                [m->screenTex[0] replaceRegion:MTLRegionMake2D(0, 0, 256, 192)
                                    mipmapLevel:0
                                      withBytes:topBuf
                                    bytesPerRow:256 * 4];
                [m->screenTex[1] replaceRegion:MTLRegionMake2D(0, 0, 256, 192)
                                    mipmapLevel:0
                                      withBytes:bottomBuf
                                    bytesPerRow:256 * 4];

                [encoder setRenderPipelineState:m->pipeline];
                [encoder setVertexBuffer:m->vertexBuffer offset:0 atIndex:0];
                id<MTLSamplerState> sampler = filter ? m->linearSampler : m->nearestSampler;
                [encoder setFragmentSamplerState:sampler atIndex:0];

                ScreenUniforms uniforms{};
                uniforms.screenSize[0] = static_cast<float>(w) / static_cast<float>(scale);
                uniforms.screenSize[1] = static_cast<float>(h) / static_cast<float>(scale);

                for (int i = 0; i < numScreens; i++)
                {
                    for (int c = 0; c < 6; c++)
                        uniforms.m[c] = screenMatrix[i][c];

                    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
                    [encoder setFragmentTexture:m->screenTex[screenKind[i]] atIndex:0];
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                 vertexStart:(screenKind[i] == 0 ? 0 : 6)
                                 vertexCount:6];
                }
            }
        }

        // OSD/HUD/splash are not drawn by this presenter yet (Metal-plan
        // Phase 5); still run osdUpdate() so item expiry/rendered-flag
        // bookkeeping doesn't leak while this presenter is active.
        osdUpdate();

        [encoder endEncoding];
        [cmdBuffer presentDrawable:drawable];
        [cmdBuffer commit];
    }
}

#endif // __APPLE__ && MELONPRIME_ENABLE_METAL
