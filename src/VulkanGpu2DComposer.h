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

#include "GPU2DNative.h"
#include "MelonPrimeStructuredComposition.h"
#include "RendererOutputRing.h"
#include "VulkanCommon.h"
#include "VulkanDevice.h"
#include "VulkanMemory.h"
#include "VulkanPresentedFrame.h"

namespace melonDS
{

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

// The GPU2D compositor's own GPU resources on the Vulkan backend: its
// resolution-dependent resource set, recreated whenever the internal
// resolution changes and held by shared_ptr so a lease the presenter still
// holds keeps its resources alive across that recreation.
//
// The compositor turns the software engines' structured 2D planes -- or the
// native GPU2D producer's packed frame -- into the composed screens the
// presenter shows. That is a different responsibility from 3D rasterization,
// and the audit graded it FAIL on both backends for living inside Renderer3D.
//
// What lives here is everything the compositor allocates: the presentation
// slots and their composed buffers and direct images, the blocking-ring work
// slots that Stage A and capture use, and the publication ring that hands a
// finished slot to the presenter.
//
// What does *not* live here is the compute pipelines. On this backend they are
// entries in one shader-step-indexed array shared with the rasterizer, and
// splitting that array would change how pipelines are built and compiled --
// a mechanism change, which does not belong in a responsibility move. The
// renderer therefore still records the compose dispatches; it records them
// against resources this class owns.
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

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_GPU2D_COMPOSER_H
