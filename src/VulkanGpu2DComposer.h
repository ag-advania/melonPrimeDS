/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef VULKAN_GPU2D_COMPOSER_H
#define VULKAN_GPU2D_COMPOSER_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <array>
#include <memory>

#include "GPU2DNative.h"
#include "GPU3D.h"
#include "MelonPrimeStructuredComposition.h"
#include "RendererOutputRing.h"
#include "VulkanCommon.h"
#include "VulkanDevice.h"
#include "VulkanMemory.h"
#include "VulkanPresentedFrame.h"
#include "VulkanSync.h"

namespace melonDS
{

class VulkanCaptureBridge;
class CaptureProvenanceState;

// Buffer layout the GPU2D compositor consumes. Declared here rather than in the
// renderer's translation unit because these sizes describe the compositor's own
// resources; the renderer only needs them where it packs data for it.
namespace VulkanGpu2D
{

// The structured 2D frame the software renderer publishes, as the compositor
// consumes it: fourteen 256x192 planes (four per screen, four capture-source
// planes and two source-B planes), followed by two 192-entry line-metadata
// arrays and 192 four-word capture commands. The layout is mirrored by
// PresentationBuffers.glsl and StructuredComposition's plane numbering.
constexpr u32 StructuredPixelCount = 256u * 192u;
constexpr u32 StructuredPlaneCount = 14u;
constexpr u32 StructuredLineMetaCount = 2u * 192u;
constexpr u32 StructuredCaptureCommandCount = 192u * 4u;
constexpr u32 StructuredInputWords =
    StructuredPlaneCount * StructuredPixelCount
    + StructuredLineMetaCount
    + StructuredCaptureCommandCount;
constexpr VkDeviceSize StructuredInputBytes =
    static_cast<VkDeviceSize>(StructuredInputWords) * sizeof(u32);
constexpr VkDeviceSize NativeGPU2DInputBytes =
    static_cast<VkDeviceSize>(GPU2DNative::PackedFrameBytes());
constexpr VkDeviceSize NativeGPU2DOutputBytes =
    static_cast<VkDeviceSize>(GPU2DNative::ScreenPixelCount) * 2u * sizeof(u32);
constexpr VkFormat DirectCompositorFormat = VK_FORMAT_R8G8B8A8_UNORM;

} // namespace VulkanGpu2D

// The GPU2D compositor's resolution-dependent resource set on the Vulkan
// backend: recreated whenever the internal resolution changes, and held by
// shared_ptr so a lease the presenter still holds keeps its resources alive
// across that recreation.
//
// What lives here is everything the compositor allocates per resolution: the
// presentation slots with their composed buffers and direct images, and the
// blocking-ring work slots that Stage A and capture record against. The
// publication ring that hands a finished slot to the presenter lives here too,
// because it indexes these slots.
//
// The compositor that drives all of it is VulkanGpu2DComposer below; this class
// is the resources, not the behaviour.
//
// Members are public because this is a resource owner, not an abstraction.
class VulkanGpu2DOutput
{
public:
    // Three presentation slots and three work slots. One more than the
    // renderer's two frames in flight so a slot the presenter still holds does
    // not stall the next compose.
    static constexpr u32 FramesInFlight = 3;

    struct Slot
    {
        Vk::Buffer StructuredStaging;
        Vk::Buffer StructuredInput;
        Vk::Buffer Composed;
        Vk::Image DirectImageTop;
        Vk::Image DirectImageBottom;
        StructuredComposition::GenerationState UploadedContentGeneration{};
        bool StructuredUploadInitialized = false;
        u64 LastSubmittedFrame = 0;
        VulkanPresentedFrame Frame;
    };

    // Stage A/capture resources belong to the blocking command ring, not to
    // presentation.  A semantic frame therefore uses exactly one work set
    // whether or not a visible slot can be published.
    struct ComposeWorkSlot
    {
        Vk::Buffer NativeStaging;
        Vk::Buffer NativeInput;
        Vk::Buffer StructuredInput;
        // Developer-only resources are created on first diagnostic use.
        Vk::Buffer DiagnosticComposed;
        Vk::ReadbackBuffer NativeReadback;
        Vk::ReadbackBuffer StructuredReadback;
        GPU2DNative::FrameGeneration UploadedNativeGeneration{};
        GPU2DNative::SemanticLineCache SemanticLines{};
        bool NativeUploadInitialized = false;

        bool EnsureDiagnosticResources(
            const VulkanDevice& device,
            VkDeviceSize outputBytes,
            bool needDiagnosticComposed,
            bool needStructuredReadback);
    };

    bool Create(
        const VulkanDevice& device, u32 width, u32 height,
        u64 resourceGeneration, u64 epoch);

    VulkanDevice Device;
    std::array<Slot, FramesInFlight> Slots;
    std::array<ComposeWorkSlot, FramesInFlight> WorkSlots;
    bool DirectImageEnabled = false;
    // Published slot, serial sequence and the per-slot presenter refcounts.
    // Backend-neutral: the DX12 compositor runs the same ring protocol
    // against the same class.
    RendererOutputRing Ring{FramesInFlight};
    u64 ResourceGeneration = 0;
};

// Everything the compose passes read from outside the compositor.
//
// All of it is borrowed and non-owning, rebuilt by the renderer for each call.
// Spelling it out is the point: it is the whole contract between the 3D
// rasterizer and the GPU2D compositor, and FinalFB in particular is read-only
// here -- the compositor samples the finished 3D image and never writes it.
//
// The two callbacks are plain function pointers rather than std::function:
// this runs once per DS frame on the emulation thread and must not allocate.
struct VulkanGpu2DComposeContext
{
    const VulkanDevice* Device = nullptr;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    Vk::DescriptorPool* Descriptors = nullptr;

    // The three pipelines a compose dispatches. Looked up by the renderer,
    // which owns the shader-step-indexed array they live in.
    VkPipeline Compositor = VK_NULL_HANDLE;
    VkPipeline CaptureSidecar = VK_NULL_HANDLE;
    VkPipeline Native = VK_NULL_HANDLE;

    // The 3D rasterizer's finished image, read-only to the compositor. A
    // pointer rather than a handle because the compose records its layout
    // transition.
    Vk::Image* FinalFB = nullptr;
    // Its tail is the persistent GPU LCDC capture mirror.
    Vk::Buffer* BlendState = nullptr;

    VulkanCaptureBridge* Capture = nullptr;
    CaptureProvenanceState* Provenance = nullptr;
    GPU2DNative::HighResCaptureProvenanceTracker* HighResCapture = nullptr;

    int ScaleFactor = 0;
    int ScreenWidth = 0;
    int ScreenHeight = 0;
    u32 NativeWorkgroupWidth = 0;

    // False until the shader-compile steps have all run.
    bool ShadersReady = false;
    // The renderer has already latched a fatal failure; compose must not run.
    bool RendererFailed = false;
    // The 3D frame was aborted, so FinalFB holds nothing for this frame.
    bool AbortFrame = false;
    bool FinalFBHasContent = false;
    bool Initialized = false;

    // Latch a runtime failure on the renderer. Same semantics as calling it
    // directly: first failure wins, it logs, and it marks the compose result
    // fatal.
    void (*Fail)(void* user, const char* reason) = nullptr;
    // Writes one set-0 allocation. The set layout and pool are the
    // rasterizer's, and slot 0 is bound for its whole command buffer, so the
    // renderer owns the write.
    bool (*WriteDescriptorSet)(
        void* user, u32 frameIndex, u32 slot,
        VkBuffer presentationOutput, VkBuffer structuredInput,
        VkImageView directOutputTop, VkImageView directOutputBottom) = nullptr;
    void* User = nullptr;
};

// The GPU2D compositor on the Vulkan backend.
//
// It turns the software engines' structured 2D planes -- or the native GPU2D
// producer's packed frame -- into the composed screens the presenter shows,
// and it owns that whole responsibility: recording the compose command
// buffers, submitting them, and publishing the finished slot. The audit graded
// this FAIL on both backends for living inside Renderer3D; it does not any
// more, and it must not move back.
//
// Owned here: the compositor's own command ring, its resource set
// (VulkanGpu2DOutput above), and the publication state the renderer reads when
// it decides whether a VBlank needs a new compose.
//
// Borrowed, never owned, and enumerated in VulkanGpu2DComposeContext: the
// device, the shared descriptor-set layout and pool, the pipeline handles the
// renderer resolves out of its shader-step-indexed array, and read-only
// handles such as FinalFB. The pipelines stay in that array because splitting
// it would change how pipelines are built and resolution-specialized, which is
// a mechanism change rather than a responsibility move -- so the renderer
// resolves the three handles a compose dispatches and passes them in.
//
// Owning a resource and borrowing a handle are different things; the context
// struct is where the second kind is written down, so neither gets mistaken
// for the other.
//
// Members are public because this is a resource owner, not an abstraction.
class VulkanGpu2DComposer
{
public:
    // Mirrors the renderer's set-0 slot assignment: the native logical Stage A
    // writes the structured contract, the compositor writes the composed
    // buffer, and keeping them apart stops a Stage B descriptor update from
    // changing a set Stage A is still using.
    static constexpr u32 CompositorSetSlot = 1;
    static constexpr u32 NativeLogicalSetSlot = 2;
    static constexpr u32 FramesInFlight = VulkanGpu2DOutput::FramesInFlight;

    // Records and submits one composed frame. Structured takes the software
    // engines' 2D planes; native takes the GPU2D producer's packed frame and
    // runs Stage A itself.
    bool ComposeStructuredOutput(
        const VulkanGpu2DComposeContext& ctx,
        const std::array<const u32*, 14>& planes,
        const std::array<const u32*, 2>& lineMeta,
        const u32* captureCommands,
        const StructuredComposition::ScreenRoutingView& screenRouting,
        u64 generation,
        const StructuredComposition::GenerationState& contentGeneration);
    bool ComposeNativeGPU2D(
        const VulkanGpu2DComposeContext& ctx,
        const GPU2DNative::FrameInput& input,
        u64 generation,
        bool finalFBValid,
        const u32* expectedTop,
        const u32* expectedBottom);
    [[nodiscard]] bool CanComposeNativeGPU2D(
        const VulkanGpu2DComposeContext& ctx) const noexcept;

    // --- output resource lifecycle -----------------------------------------
    //
    // The compositor declares itself the owner of the output resource set and
    // of publication state, so it is the thing that creates, releases and
    // resets them. The renderer asks for an operation; it does not reach in.

    // Builds a new resource set for `width` x `height` and, only once it is
    // fully created, makes it the active one. A failed create leaves the
    // previous set exactly as it was: a half-initialized set must never become
    // visible to the presenter or to a compose.
    bool RecreateOutput(
        const VulkanDevice& device, u32 width, u32 height, u64 epoch);

    // Drops the active resource set and the publication state that described
    // it. A lease the presenter still holds keeps its own resources alive
    // through the shared_ptr it captured -- releasing here detaches, it does
    // not destroy out from under a lease.
    void ReleaseOutput() noexcept;

    // A renderer reset or savestate load. With preservePresentation the last
    // complete published surface keeps its resource identity, frame metadata
    // and serial, and only the unpublished slots are rewound for the next
    // frame; without it, nothing is published afterwards.
    void ResetForRendererEpoch(u64 epoch, bool preservePresentation) noexcept;

    // The renderer latched a runtime failure. The compositor cannot produce a
    // frame any more, and says so in its own vocabulary -- the failure itself,
    // and its reason, stay the renderer's.
    void MarkFatal() noexcept;

    [[nodiscard]] bool HasValidOutput() const noexcept
    {
        return static_cast<bool>(Output);
    }
    [[nodiscard]] u64 GetPublishedOutputGeneration() const noexcept
    {
        return PublishedOutputGeneration;
    }
    [[nodiscard]] GPU2DComposeResult GetLastComposeResult() const noexcept
    {
        return LastComposeResult;
    }

    // The published frame, and a lease on it. Both were the renderer reading
    // through Output, the ring and the published slot; the compositor owns all
    // three, so it answers.
    [[nodiscard]] RendererOutput GetComposedOutput() const;
    [[nodiscard]] RendererOutputLease AcquireComposedOutputLease();

    // The compositor records into its own command buffers and fences rather
    // than sharing the rasterizer's. It has to: the structured 2D planes are
    // only complete after all 192 scanlines have been drawn, which is long
    // after RenderFrame() closed and submitted its command buffer, and reusing
    // the rasterizer's slot would reset the fence GetLine()'s capture readback
    // is still waiting on. One frame in flight, for the same reason the
    // rasterizer keeps one. Its three output slots additionally carry their own
    // structured input, so VBlank can submit without waiting for the prior slot.
    Vk::FrameRing Frames;

    // Recreated per resolution, so it is held by pointer.
    //
    // Public because the rasterizer's own set-0 write binds slot 0's structured
    // input buffer -- the compositor and the rasterizer share one descriptor
    // set layout, and hiding this behind an accessor pair would describe a
    // boundary that the descriptor contract does not have. Reading it is
    // allowed; every mutation of it goes through the operations above.
    std::shared_ptr<VulkanGpu2DOutput> Output;

private:
    // Published only after the compositor submission has been accepted. GPU
    // completion is ordered by the shared queue; the presenter lease owns the
    // slot until its copy command retires.
    //
    // Private because these four are the publication state itself. The
    // renderer used to set them directly, which made the declared owner and
    // the mutating owner two different things.
    bool ComposedOutputValid = false;
    u64 ComposedGeneration = 0;
    u64 PublishedOutputGeneration = 0;
    GPU2DComposeResult LastComposeResult = GPU2DComposeResult::Unavailable;

    // Lifetime identity of the output resource set, so a presenter can cache
    // descriptors against a resource generation rather than a content one. It
    // advances only when a new set is created, and never rewinds -- releasing
    // a set does not hand its number back.
    u64 NextOutputResourceGeneration = 1;
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_GPU2D_COMPOSER_H
