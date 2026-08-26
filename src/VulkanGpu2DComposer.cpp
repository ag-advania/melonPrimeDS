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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "VulkanGpu2DComposer.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <numeric>

#include "CaptureProvenanceState.h"
#include "GpuStageMetrics.h"
#include "Platform.h"
#include "StructuredUploadPlan.h"
#include "VulkanCaptureBridge.h"
#include "VulkanDebugLabels.h"
#include "VulkanDescriptors.h"
#include "VulkanGpuTimestamp.h"
#include "VulkanPerf.h"

namespace melonDS
{

using namespace VulkanGpu2D;

using Vk::DivRoundUp;

bool VulkanGpu2DOutput::ComposeWorkSlot::EnsureDiagnosticResources(
    const VulkanDevice& device,
    VkDeviceSize outputBytes,
    bool needDiagnosticComposed,
    bool needStructuredReadback)
{
    if (needDiagnosticComposed && !DiagnosticComposed.IsValid()
        && !DiagnosticComposed.Create(device, outputBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan diagnostic composed output"))
        return false;
    if (!NativeReadback.IsValid()
        && !NativeReadback.Create(device, outputBytes,
            "MelonPrime Vulkan native GPU2D diagnostic readback"))
        return false;
    if (needStructuredReadback && !StructuredReadback.IsValid()
        && !StructuredReadback.Create(device, StructuredInputBytes,
            "MelonPrime Vulkan GPU2D Stage A diagnostic readback"))
        return false;
    return true;
}

bool VulkanGpu2DOutput::Create(
    const VulkanDevice& device, u32 width, u32 height,
    u64 resourceGeneration, u64 epoch)
{
    Device = device;
    ResourceGeneration = resourceGeneration;
    const VkDeviceSize screenBytes =
        static_cast<VkDeviceSize>(width) * height * sizeof(u32);

    VkFormatProperties directProperties{};
    Device.InstanceFns().GetPhysicalDeviceFormatProperties(
        Device.GetPhysicalDevice(), DirectCompositorFormat, &directProperties);
    DirectImageEnabled =
        (directProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0
        && (directProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    if (!DirectImageEnabled)
    {
        Platform::Log(
            Platform::LogLevel::Warn,
            "[Vulkan] compositor direct image disabled: RGBA8 lacks storage or sampled support\n");
    }

    for (u32 i = 0; i < Slots.size(); ++i)
    {
        Slot& slot = Slots[i];
        if (!slot.StructuredStaging.Create(Device,
                StructuredInputBytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                "MelonPrime Vulkan structured staging slot"))
            return false;
        if (!slot.StructuredStaging.Map())
            return false;
        if (!slot.StructuredInput.Create(Device,
                StructuredInputBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                    | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                    | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                "MelonPrime Vulkan structured input slot"))
            return false;
        if (!slot.Composed.Create(Device,
                screenBytes * 2u,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                "MelonPrime Vulkan composed output slot"))
            return false;
    }

    if (DirectImageEnabled)
    {
        for (Slot& slot : Slots)
        {
            Vk::Image::CreateInfo directInfo{};
            directInfo.Format = DirectCompositorFormat;
            directInfo.Width = width;
            directInfo.Height = height;
            directInfo.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            directInfo.ViewType = VK_IMAGE_VIEW_TYPE_2D;
            directInfo.DebugName = "MelonPrime Vulkan direct compositor output";
            if (!slot.DirectImageTop.Create(Device, directInfo)
                || !slot.DirectImageBottom.Create(Device, directInfo))
            {
                DirectImageEnabled = false;
                break;
            }
        }
    }
    if (!DirectImageEnabled)
    {
        for (Slot& slot : Slots)
        {
            slot.DirectImageTop.Destroy();
            slot.DirectImageBottom.Destroy();
        }
    }

    for (Slot& slot : Slots)
    {
        slot.Frame.Buffer = slot.Composed.GetHandle();
        slot.Frame.DirectImageTop = DirectImageEnabled
            ? slot.DirectImageTop.GetHandle() : VK_NULL_HANDLE;
        slot.Frame.DirectImageViewTop = DirectImageEnabled
            ? slot.DirectImageTop.GetView() : VK_NULL_HANDLE;
        slot.Frame.DirectImageBottom = DirectImageEnabled
            ? slot.DirectImageBottom.GetHandle() : VK_NULL_HANDLE;
        slot.Frame.DirectImageViewBottom = DirectImageEnabled
            ? slot.DirectImageBottom.GetView() : VK_NULL_HANDLE;
        slot.Frame.TopOffset = 0;
        slot.Frame.BottomOffset = screenBytes;
        slot.Frame.Width = width;
        slot.Frame.Height = height;
        slot.Frame.Epoch = epoch;
        slot.Frame.ResourceGeneration = ResourceGeneration;
        slot.Frame.DirectContentValid = false;
    }

    for (u32 i = 0; i < WorkSlots.size(); ++i)
    {
        ComposeWorkSlot& slot = WorkSlots[i];
        if (!slot.NativeStaging.Create(Device,
                NativeGPU2DInputBytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                "MelonPrime Vulkan native GPU2D work staging slot"))
            return false;
        if (!slot.NativeStaging.Map())
            return false;
        if (!slot.NativeInput.Create(Device,
                NativeGPU2DInputBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                "MelonPrime Vulkan native GPU2D work input slot"))
            return false;
        if (!slot.StructuredInput.Create(Device,
                StructuredInputBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                "MelonPrime Vulkan native GPU2D work structured slot"))
            return false;
    }
    return true;
}

bool VulkanGpu2DComposer::CanComposeNativeGPU2D(
    const VulkanGpu2DComposeContext& ctx) const noexcept
{
    return !ctx.RendererFailed
        && ctx.Initialized
        && ctx.ScaleFactor > 0
        && ctx.ShadersReady
        && ctx.Native != VK_NULL_HANDLE
        && ctx.Compositor != VK_NULL_HANDLE
        && Output
        && ctx.FinalFB && ctx.FinalFB->IsValid();
}

bool VulkanGpu2DComposer::ComposeStructuredOutput(
    const VulkanGpu2DComposeContext& ctx,
    const std::array<const u32*, 14>& planes,
    const std::array<const u32*, 2>& lineMeta,
    const u32* captureCommands,
    const StructuredComposition::ScreenRoutingView& screenRouting,
    u64 generation,
    const StructuredComposition::GenerationState& contentGeneration)
{
    LastComposeResult = GPU2DComposeResult::Unavailable;
    if (ctx.RendererFailed)
    {
        LastComposeResult = GPU2DComposeResult::Fatal;
        return false;
    }
    if (!ctx.Initialized || ctx.ScaleFactor <= 0)
        return false;
    if (!ctx.ShadersReady)
        return false;       // pipelines are still being compiled

    // The producer bumps its generation once per DS frame. Composing the same
    // one twice would repeat a whole composition dispatch for a result that
    // cannot have changed.
    if (ComposedOutputValid && ComposedGeneration == generation)
    {
        LastComposeResult = GPU2DComposeResult::Success;
        return true;
    }

    if (ctx.Compositor == VK_NULL_HANDLE
        || ctx.CaptureSidecar == VK_NULL_HANDLE
        || !Output || !ctx.FinalFB->IsValid())
    {
        ctx.Fail(ctx.User, "required compositor resources are unavailable");
        return false;
    }

    for (const u32* plane : planes)
    {
        if (!plane)
            return false;
    }
    for (const u32* meta : lineMeta)
    {
        if (!meta)
            return false;
    }
    if (!captureCommands)
        return false;
    const StructuredComposition::CaptureLineAnalysis captureAnalysis =
        StructuredComposition::AnalyzeCaptureDependencies(planes, captureCommands);

    const Vk::DeviceDispatch& fns = (*ctx.Device).Fns();
    // Semantic GPU2D admission is intentionally blocking at the command-ring
    // boundary.  Presentation backpressure may discard publication, but it
    // must not discard the DS display-capture state produced by this frame.
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const bool workSlotFencePending = Frames.NextFrameHasPendingSubmission();
    const auto workSlotWaitStart = std::chrono::steady_clock::now();
#endif
    Vk::FrameContext* frame = Frames.BeginFrame();
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const auto workSlotWaitEnd = std::chrono::steady_clock::now();
    if (workSlotFencePending)
    {
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::NativeGPU2DWorkSlotFenceWaitCount);
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::NativeGPU2DWorkSlotFenceWaitNs,
            static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                workSlotWaitEnd - workSlotWaitStart).count()));
    }
#endif
    if (!frame)
    {
        ctx.Fail(ctx.User, "native GPU2D semantic command-ring admission failed");
        return false;
    }
    VkCommandBuffer cmd = frame->CommandBuffer;
    const u32 frameIndex = Frames.GetFrameIndex();
    u32 nextSlot = FramesInFlight;
    {
        // A presenter lease makes the published slot immutable, but it must
        // not force the producer to reuse one fixed ring index.  Search every
        // unpublished slot whose previous GPU submission has retired.  The
        // command ring has already passed its non-blocking readiness probe,
        // so this remains a drop-only path when all resources are occupied.
        // The ring answers "published or leased"; this answers the only half
        // that needs Vulkan -- whether the slot's last submission has retired.
        struct SlotReadiness
        {
            VulkanGpu2DOutput* State;
            u64 CompletedFrame;
        } readiness{Output.get(), Frames.GetCompletedFrame()};
        const auto slotReady = +[](void* userData, u32 candidate) -> bool {
            auto* ctx = static_cast<SlotReadiness*>(userData);
            return ctx->State->Slots[candidate].LastSubmittedFrame
                <= ctx->CompletedFrame;
        };

        const auto lock = Output->Ring.LockPublication();
        const u32 candidate = Output->Ring.FindFreeSlot(
            frameIndex % FramesInFlight, slotReady, &readiness);
        if (candidate != RendererOutputRing::InvalidSlot)
            nextSlot = candidate;
    }
    if (nextSlot == FramesInFlight)
    {
        Frames.SubmitFrame((*ctx.Device).GetMainQueue());
        VulkanPerf::AddCounter(VulkanPerf::Counter::CompositorDropCount);
        LastComposeResult = GPU2DComposeResult::Backpressure;
        return false;
    }

    VulkanGpu2DOutput::Slot& outputSlot = Output->Slots[nextSlot];

    // Acquire the compositor ring slot before touching its mapped staging
    // buffer.  The slot's previous submission has been checked against the
    // completed command-ring frame above, so generation comparison alone is
    // never used as a reuse proof.
    RecordVulkanGpuMetric(
        Frames, GpuMetric::CaptureSidecar,
        VulkanPerf::Counter::CaptureSidecarGpuTimeNs);
    RecordVulkanGpuMetric(
        Frames, GpuMetric::StructuredCompositor,
        VulkanPerf::Counter::StructuredCompositorGpuTimeNs);
    RecordVulkanGpuMetric(
        Frames, GpuMetric::StructuredCompositor,
        VulkanPerf::Counter::CompositorGpuTimeNs);
    Frames.WriteTimestamp(
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        GpuMetricQueryIndex(GpuMetric::StructuredCompositor, false));

    // Which units changed, and which byte runs that collapses into, is a
    // content-generation question rather than a Vulkan one, so both backends
    // ask the same function.
    constexpr u32 logicalUnitCount = StructuredComposition::UploadUnitCount;
    const VkDeviceSize planeBytes =
        static_cast<VkDeviceSize>(StructuredPixelCount) * sizeof(u32);
    const VkDeviceSize lineMetaBytes = 192u * sizeof(u32);
    const VkDeviceSize captureCommandBytes =
        StructuredCaptureCommandCount * sizeof(u32);
    const StructuredComposition::StructuredUploadPlan structuredUpload =
        StructuredComposition::BuildStructuredUploadPlan(
            contentGeneration,
            outputSlot.UploadedContentGeneration,
            outputSlot.StructuredUploadInitialized,
            planeBytes,
            lineMetaBytes,
            captureCommandBytes);
    // Retained for the upload-shape counters below: a slot that had never
    // been written is a different event from one whose planes all changed.
    const bool fullUpload = !outputSlot.StructuredUploadInitialized;
    const auto& dirty = structuredUpload.Dirty;
    const auto& unitOffsets = structuredUpload.UnitOffsets;
    const auto& unitSizes = structuredUpload.UnitSizes;
    const auto& ranges = structuredUpload.Ranges;
    const std::size_t rangeCount = structuredUpload.RangeCount;
    const bool captureClassificationDirty =
        dirty[StructuredComposition::CaptureCommandUnit];
    const bool uploadRequired = structuredUpload.Required();
    VkDeviceSize packedBytes = 0;
    u32 routeRuns = 0;
    std::array<bool, 2> routeRunsCounted{};

    if (uploadRequired)
    {
        u32* staging = static_cast<u32*>(outputSlot.StructuredStaging.GetMappedPointer());
        if (!staging)
        {
            Frames.SubmitFrame((*ctx.Device).GetMainQueue());
            ctx.Fail(ctx.User, "the structured staging buffer is not mapped");
            return false;
        }

        {
            VulkanPerf::ScopedCpuTimer packTimer(VulkanPerf::CpuMetric::ComposePack);
            for (u32 unit = 0; unit < logicalUnitCount; ++unit)
            {
                if (!dirty[unit])
                    continue;
                if (unit < 8u)
                {
                    // The per-plane path preserves the same routing contract
                    // as PackRoutedScreenPlanes(staging, screenRouting).
                    const u32 screen = unit / StructuredComposition::kPlaneCount;
                    const u32 plane = unit % StructuredComposition::kPlaneCount;
                    const StructuredComposition::ScreenPackResult screenPack =
                        StructuredComposition::PackRoutedScreenPlane(
                            staging + static_cast<std::size_t>(unit) * StructuredPixelCount,
                            screen, plane, screenRouting);
                    if (!screenPack.Valid)
                    {
                        Frames.SubmitFrame((*ctx.Device).GetMainQueue());
                        return false;
                    }
                    if (!routeRunsCounted[screen])
                    {
                        routeRuns += screenPack.RouteRuns;
                        routeRunsCounted[screen] = true;
                    }
                }
                else if (unit < StructuredPlaneCount)
                {
                    std::memcpy(
                        staging + static_cast<std::size_t>(unit) * StructuredPixelCount,
                        planes[unit], static_cast<std::size_t>(StructuredPixelCount) * sizeof(u32));
                }
                else if (unit < 16u)
                {
                    std::memcpy(
                        staging + unitOffsets[unit] / sizeof(u32),
                        lineMeta[unit - 14u], 192u * sizeof(u32));
                }
                else
                {
                    u32* stagedCommands = staging + unitOffsets[unit] / sizeof(u32);
                    std::memcpy(stagedCommands, captureCommands, captureCommandBytes);
                    for (u32 line = 0; line < 192u; ++line)
                    {
                        const u32 commandBase =
                            line * StructuredComposition::kCaptureCommandWords;
                        stagedCommands[commandBase + 1u] &=
                            ~StructuredComposition::kCaptureCommandIndependent;
                        if (captureAnalysis.Independent[line] != 0u)
                        {
                            stagedCommands[commandBase + 1u] |=
                                StructuredComposition::kCaptureCommandIndependent;
                        }
                    }
                }
                packedBytes += unitSizes[unit];
            }
        }
        for (std::size_t i = 0; i < rangeCount; ++i)
        {
            if (!outputSlot.StructuredStaging.FlushRange(
                    ranges[i].Offset, ranges[i].Size))
            {
                Frames.SubmitFrame((*ctx.Device).GetMainQueue());
                ctx.Fail(ctx.User, "could not flush the structured staging buffer");
                return false;
            }
        }
        VulkanPerf::AddCounter(VulkanPerf::Counter::StructuredPackBytes, packedBytes);
        VulkanPerf::AddCounter(VulkanPerf::Counter::StructuredInputBytesPacked, packedBytes);
        VulkanPerf::AddCounter(VulkanPerf::Counter::StructuredRouteRuns, routeRuns);
    }

    {
        if (uploadRequired)
        {
            std::array<VkBufferCopy, logicalUnitCount> copies{};
            for (std::size_t i = 0; i < rangeCount; ++i)
            {
                copies[i].srcOffset = ranges[i].Offset;
                copies[i].dstOffset = ranges[i].Offset;
                copies[i].size = ranges[i].Size;
            }
            fns.CmdCopyBuffer(cmd,
                outputSlot.StructuredStaging.GetHandle(),
                outputSlot.StructuredInput.GetHandle(),
                static_cast<u32>(rangeCount), copies.data());

            const VkBuffer structured = outputSlot.StructuredInput.GetHandle();
            Vk::BufferBarrier((*ctx.Device), cmd, &structured, 1,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        }
    }

    // ctx.FinalFB was written by FinalPass and read by Resolve in a *different*
    // submission. A pipeline barrier's first synchronization scope includes
    // everything already submitted to the same queue, so naming the producing
    // stage and access here is what makes those writes available to the
    // compositor's reads. The layout does not change -- ctx.FinalFB lives in
    // GENERAL for its whole lifetime -- so this is a dependency, not a
    // transition.
    ctx.FinalFB->RecordLayoutTransition(cmd,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

    const bool directImageOutput = outputSlot.DirectImageTop.IsValid()
        && outputSlot.DirectImageBottom.IsValid();
    if (directImageOutput)
    {
        const auto beginDirectWrite = [&](Vk::Image& image) {
            const VkImageLayout previous = image.GetLayout();
            image.RecordLayoutTransition(
                cmd,
                VK_IMAGE_LAYOUT_GENERAL,
                previous == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                    : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                previous == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_WRITE_BIT);
        };
        beginDirectWrite(outputSlot.DirectImageTop);
        beginDirectWrite(outputSlot.DirectImageBottom);
    }

    // Stage A: native GPU2D evaluates one 256x192 logical pixel per thread and
    // writes the structured composition contract. It never writes a scaled
    // color for the presenter.
    if (!ctx.WriteDescriptorSet(
            ctx.User,
            frameIndex, NativeLogicalSetSlot, outputSlot.StructuredInput.GetHandle(),
            outputSlot.StructuredInput.GetHandle(), VK_NULL_HANDLE, VK_NULL_HANDLE))
    {
        Frames.SubmitFrame((*ctx.Device).GetMainQueue());
        ctx.Fail(ctx.User, "could not write the native logical GPU2D descriptor set");
        return false;
    }

    VkDescriptorSet nativeSet = ctx.Descriptors->GetRasterizerSet(frameIndex, NativeLogicalSetSlot);
    fns.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.PipelineLayout,
        Vk::RasterizerSetIndex, 1, &nativeSet, 0, nullptr);

    Vk::RasterizerPushConstants push{};
    // Reused as "this frame's 3D image is real", matching the DX12 compositor.
    // Zero when GPU3D aborted the frame (RenderFrame() never ran) or when
    // nothing has been rendered into ctx.FinalFB yet; the shader then leaves every
    // 3D slot showing the 2D pixel underneath, which is what the software
    // renderer produces from an all-transparent 3D line.
    push.TexWidth = (ctx.AbortFrame || !ctx.FinalFBHasContent) ? 0u : 1u;
    // The padding word is unused by the presentation stages otherwise and is
    // deliberately reused as a mode bit so the shared 32-byte push contract
    // does not change for the rasterizer pipelines.
    push.Padding = 16u;
    fns.CmdPushConstants(cmd, ctx.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, Vk::PushConstantSize, &push);

    Frames.WriteTimestamp(
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        GpuMetricQueryIndex(GpuMetric::CaptureSidecar, false));
    fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        ctx.CaptureSidecar);
    const VkBuffer captureSidecar = ctx.Capture->GetSidecarHandle();
    u32 sidecarDispatchCount = 0;
    u32 sidecarBarrierCount = 0;
    for (u32 captureLine = 0; captureLine < 192u;)
    {
        if ((captureCommands[captureLine * 4u + 1u]
                & StructuredComposition::kCaptureCommandValid) == 0u)
        {
            ++captureLine;
            continue;
        }

        if (captureAnalysis.Independent[captureLine] != 0u)
        {
            const u32 runStart = captureLine;
            do
            {
                ++captureLine;
            }
            while (captureLine < 192u
                && captureAnalysis.Independent[captureLine] != 0u);
            push.TexHeight = runStart;
            push.TexIsCapture = 1u;
            fns.CmdPushConstants(cmd, ctx.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                0, Vk::PushConstantSize, &push);
            fns.CmdDispatch(cmd,
                DivRoundUp(static_cast<u32>(ctx.ScreenWidth), 8u),
                DivRoundUp(static_cast<u32>(ctx.ScaleFactor), 8u),
                captureLine - runStart);
            ++sidecarDispatchCount;
            Vk::BufferBarrier((*ctx.Device), cmd, &captureSidecar, 1,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            ++sidecarBarrierCount;
            continue;
        }

        push.TexHeight = captureLine;
        push.TexIsCapture = 0u;
        fns.CmdPushConstants(cmd, ctx.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, Vk::PushConstantSize, &push);
        fns.CmdDispatch(cmd,
            DivRoundUp(static_cast<u32>(ctx.ScreenWidth), 8u),
            DivRoundUp(static_cast<u32>(ctx.ScaleFactor), 8u),
            1u);
        ++sidecarDispatchCount;
        Vk::BufferBarrier((*ctx.Device), cmd, &captureSidecar, 1,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        ++sidecarBarrierCount;
        ++captureLine;
    }
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::CaptureValidLineCount, captureAnalysis.ValidLineCount);
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::CaptureIndependentLineCount,
        captureAnalysis.IndependentLineCount);
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::CaptureLegacyOrderedLineCount,
        captureAnalysis.LegacyOrderedLineCount);
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::CaptureSidecarDispatchCount, sidecarDispatchCount);
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::CaptureSidecarBarrierCount, sidecarBarrierCount);
    Frames.WriteTimestamp(
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        GpuMetricQueryIndex(GpuMetric::CaptureSidecar, true));

    // Do not carry the sidecar's per-dispatch addressing mode into the
    // compositor push state. The compositor currently ignores these words,
    // but keeping the shared block in its neutral mode makes the boundary
    // explicit and prevents a future compositor shader from observing stale
    // capture coordinates.
    push.TexHeight = 0u;
    push.TexIsCapture = 0u;
    fns.CmdPushConstants(cmd, ctx.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, Vk::PushConstantSize, &push);
    Vk::BeginCommandDebugLabel(fns, cmd, "Vulkan.Structured.Compositor");
    fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        ctx.Compositor);
    // One dispatch covers both screens in the slot's device-local buffer.
    fns.CmdDispatch(cmd,
        DivRoundUp(static_cast<u32>(ctx.ScreenWidth), 8u),
        DivRoundUp(static_cast<u32>(ctx.ScreenHeight) * 2u, 8u),
        1);
    Vk::EndCommandDebugLabel(fns, cmd);
    Frames.WriteTimestamp(
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        GpuMetricQueryIndex(GpuMetric::StructuredCompositor, true));

    if (directImageOutput)
    {
        const auto finishDirectRead = [&](Vk::Image& image) {
            image.RecordLayoutTransition(
                cmd,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT);
        };
        finishDirectRead(outputSlot.DirectImageTop);
        finishDirectRead(outputSlot.DirectImageBottom);
        VulkanPerf::AddCounter(VulkanPerf::Counter::DirectCompositorImageFrames);
    }
    else
    {
        VulkanPerf::AddCounter(VulkanPerf::Counter::FallbackCompositorBufferFrames);
    }

    const u64 submittedComposeFrame = Frames.GetCurrentRecordingFrameNumber();
    bool composeSubmitted = false;
    {
        VulkanPerf::ScopedCpuTimer submitTimer(VulkanPerf::CpuMetric::QueueSubmit);
        composeSubmitted = Frames.SubmitFrame((*ctx.Device).GetMainQueue());
    }
    if (!composeSubmitted)
    {
        ctx.Fail(ctx.User, "compositor command submission failed");
        return false;
    }
    outputSlot.LastSubmittedFrame = submittedComposeFrame;

    if (uploadRequired)
    {
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::StructuredInputBytesUploaded,
            static_cast<u64>(std::accumulate(
                ranges.begin(), ranges.begin() + rangeCount, VkDeviceSize{0},
                [](VkDeviceSize total,
                   const StructuredComposition::StructuredUploadRange& range) {
                    return total + range.Size;
                })));
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::StructuredInputCopyRegionCount,
            static_cast<u64>(rangeCount));
        VulkanPerf::AddCounter(
            fullUpload
                ? VulkanPerf::Counter::StructuredInputFullUploadCount
                : VulkanPerf::Counter::StructuredInputPartialUploadCount);
    }
    outputSlot.UploadedContentGeneration = contentGeneration;
    outputSlot.StructuredUploadInitialized = true;

    {
        const auto lock = Output->Ring.LockPublication();
        outputSlot.Frame.Serial = Output->Ring.PublishNext(nextSlot);
        outputSlot.Frame.Generation = generation;
        outputSlot.Frame.DirectContentValid = directImageOutput;
        ComposedGeneration = generation;
        PublishedOutputGeneration = generation;
        ComposedOutputValid = true;
    }
    LastComposeResult = GPU2DComposeResult::Success;
    return true;
}

bool VulkanGpu2DComposer::ComposeNativeGPU2D(
    const VulkanGpu2DComposeContext& ctx,
    const GPU2DNative::FrameInput& input,
    u64 generation,
    bool finalFBValid,
    const u32* expectedTop,
    const u32* expectedBottom)
{
    LastComposeResult = GPU2DComposeResult::Unavailable;
    const bool exactValidation = expectedTop != nullptr && expectedBottom != nullptr;
    const bool stageDiagnostics = GPU2DNative::StageDiagnosticsEnabled();
    const bool diagnosticReadback = exactValidation || stageDiagnostics;
    if (exactValidation && ctx.ScaleFactor != 1)
    {
        ctx.Fail(ctx.User, "native GPU2D exact validation requires scale=1");
        return false;
    }
    if (ctx.RendererFailed)
    {
        LastComposeResult = GPU2DComposeResult::Fatal;
        return false;
    }
    if (!ctx.Initialized || ctx.ScaleFactor <= 0)
        return false;
    if (!ctx.ShadersReady)
        return false;
    // The native pipeline handle is resolved by the renderer, which owns the
    // shader-step array the resolution-specialized variants live in.
    const u32 nativeWorkgroupWidth = ctx.NativeWorkgroupWidth;
    if (ctx.Native == VK_NULL_HANDLE
        || !Output || !ctx.FinalFB->IsValid())
    {
        ctx.Fail(ctx.User, "required native GPU2D resources are unavailable");
        return false;
    }

    const Vk::DeviceDispatch& fns = (*ctx.Device).Fns();
    // Semantic GPU2D admission is intentionally blocking at the command-ring
    // boundary. Presentation backpressure may discard publication, but it
    // must not discard the DS display-capture state produced by this frame.
    Vk::FrameContext* frame = Frames.BeginFrame();
    if (!frame)
    {
        ctx.Fail(ctx.User, "native GPU2D semantic command-ring admission failed");
        return false;
    }
    VkCommandBuffer cmd = frame->CommandBuffer;
    const u32 frameIndex = Frames.GetFrameIndex();
    u32 nextSlot = FramesInFlight;
    {
        // The ring answers "published or leased"; this answers the only half
        // that needs Vulkan -- whether the slot's last submission has retired.
        struct SlotReadiness
        {
            VulkanGpu2DOutput* State;
            u64 CompletedFrame;
        } readiness{Output.get(), Frames.GetCompletedFrame()};
        const auto slotReady = +[](void* userData, u32 candidate) -> bool {
            auto* ctx = static_cast<SlotReadiness*>(userData);
            return ctx->State->Slots[candidate].LastSubmittedFrame
                <= ctx->CompletedFrame;
        };

        const auto lock = Output->Ring.LockPublication();
        const u32 candidate = Output->Ring.FindFreeSlot(
            frameIndex % FramesInFlight, slotReady, &readiness);
        if (candidate != RendererOutputRing::InvalidSlot)
            nextSlot = candidate;
    }
    VulkanGpu2DOutput::Slot* outputSlot = nextSlot < FramesInFlight
        ? &Output->Slots[nextSlot] : nullptr;
    VulkanGpu2DOutput::ComposeWorkSlot& workSlot =
        Output->WorkSlots[frameIndex % Output->WorkSlots.size()];
    bool presentationAvailable = outputSlot != nullptr;
    const bool forcedPresentationStall = presentationAvailable
        && GPU2DNative::ConsumeForcedPresentationStallFrame();
    if (forcedPresentationStall)
    {
        outputSlot = nullptr;
        nextSlot = FramesInFlight;
        presentationAvailable = false;
    }
    // Presentation backpressure is allowed to drop a visible frame, but must
    // never drop DS display-capture semantics. The persistent LCDC capture
    // mirror is emulated hardware state, not a presentation cache.
    Vk::Buffer& nativeStaging = workSlot.NativeStaging;
    Vk::Buffer& nativeInputBuffer = workSlot.NativeInput;
    Vk::Buffer& structuredOutputBuffer = workSlot.StructuredInput;
    GPU2DNative::FrameGeneration& uploadedNativeGeneration =
        workSlot.UploadedNativeGeneration;
    bool& nativeUploadInitialized = workSlot.NativeUploadInitialized;
    const bool hadDiagnosticReadback = workSlot.NativeReadback.IsValid();
    const bool hadFallbackComposed = workSlot.DiagnosticComposed.IsValid();
    if (diagnosticReadback
        && !workSlot.EnsureDiagnosticResources(
            (*ctx.Device), NativeGPU2DOutputBytes, outputSlot == nullptr, stageDiagnostics))
    {
        Frames.SubmitFrame((*ctx.Device).GetMainQueue());
        ctx.Fail(ctx.User, "could not create lazy native GPU2D diagnostic resources");
        return false;
    }
    if (diagnosticReadback && !hadDiagnosticReadback
        && workSlot.NativeReadback.IsValid())
    {
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::NativeGPU2DDiagnosticReadbackCreateCount);
    }
    if (!hadFallbackComposed && workSlot.DiagnosticComposed.IsValid())
    {
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::NativeGPU2DFallbackComposedCreateCount);
    }
    Vk::ReadbackBuffer& nativeReadback = workSlot.NativeReadback;
    Vk::ReadbackBuffer& structuredReadback = workSlot.StructuredReadback;
    u64 rendererSerial = 0;
    if (outputSlot)
    {
        const auto lock = Output->Ring.LockPublication();
        rendererSerial = Output->Ring.PeekNextSerial();
    }
    RecordVulkanGpuMetric(
        Frames, GpuMetric::NativeGPU2DLogical,
        VulkanPerf::Counter::NativeGPU2DLogicalGpuTimeNs);
    RecordVulkanGpuMetric(
        Frames, GpuMetric::NativeGPU2DCapture,
        VulkanPerf::Counter::NativeGPU2DCaptureGpuTimeNs);
    RecordVulkanGpuMetric(
        Frames, GpuMetric::NativeGPU2DResolve,
        VulkanPerf::Counter::NativeGPU2DResolveGpuTimeNs);
    RecordVulkanGpuMetric(
        Frames, GpuMetric::NativeGPU2DRaw,
        VulkanPerf::Counter::NativeGPU2DObjRawGpuNs);
    RecordVulkanGpuMetric(
        Frames, GpuMetric::NativeGPU2DResolve,
        VulkanPerf::Counter::CompositorGpuTimeNs);

    u64 pendingCompletionValue = ctx.Provenance->PeekNextSubmissionSerial();
    if (pendingCompletionValue == 0u)
        pendingCompletionValue = 1u;
    const NativeCaptureStateIdentity pendingCaptureIdentity{
        true,
        CaptureOwner::NativeVulkan,
        ctx.Provenance->GetEpoch(),
        input.Generation.Frame,
        input.Generation.CaptureGeneration,
        pendingCompletionValue,
    };
    ctx.HighResCapture->BeginFrame(
        input, pendingCaptureIdentity, static_cast<u32>(ctx.ScaleFactor));
    const GPU2DNative::UploadDecision uploadDecision =
        GPU2DNative::DetermineUploadDecision(
            nativeUploadInitialized, ctx.Provenance->GetEpoch(), ctx.Provenance->GetSemanticEpoch(),
            ctx.Provenance->GetSemanticFrame(), ctx.Provenance->GetSemanticCaptureGeneration(),
            input.Generation);
    const bool semanticFrameContiguous =
        uploadDecision.SemanticFrameContiguous;
    const bool semanticCaptureGenerationRegressed =
        uploadDecision.CaptureGenerationRegressed;
    const bool fullNativeUpload = uploadDecision.RequiresFullUpload();
    const GPU2DNative::SemanticLinePlan semanticLinePlan =
        GPU2DNative::BuildSemanticLinePlan(
            input, workSlot.SemanticLines,
            fullNativeUpload || input.CaptureEnable != 0u);
    VulkanPerf::SetCounter(
        VulkanPerf::Counter::NativeGPU2DWorkgroupWidth,
        ctx.NativeWorkgroupWidth);
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::NativeGPU2DSemanticRowsDirty,
        semanticLinePlan.DirtyRows);
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::NativeGPU2DSemanticRowsReused,
        semanticLinePlan.ReusedRows);
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::NativeGPU2DSemanticRunCount,
        semanticLinePlan.RunCount);
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::NativeGPU2DObjPrepareGroups,
        semanticLinePlan.DirtyRows * (256u / ctx.NativeWorkgroupWidth));
    GPU2DNative::UploadPlan uploadPlan = GPU2DNative::BuildUploadPlan(
        input, uploadedNativeGeneration, fullNativeUpload);
    // Hundreds of sub-kilobyte timeline ranges are common in menu transitions.
    // Vulkan command recording is faster when a small unchanged gap is copied
    // with its neighbours than when each range becomes a separate command.
    // PackFrameRanges serializes the enlarged ranges from the current exact
    // input, so this changes transfer granularity, never GPU2D semantics.
    GPU2DNative::CoalesceUploadPlan(uploadPlan, 4u * 1024u);
    VulkanPerf::AddCounter(
        fullNativeUpload
            ? VulkanPerf::Counter::NativeGPU2DFullUploadFrames
            : VulkanPerf::Counter::NativeGPU2DPartialUploadFrames);
    VulkanPerf::AddCounter(
        fullNativeUpload
            ? VulkanPerf::Counter::NativeGPU2DFullUploadBytes
            : VulkanPerf::Counter::NativeGPU2DPartialUploadBytes,
        uploadPlan.TotalBytes);
    switch (uploadDecision.Reason)
    {
    case GPU2DNative::FullUploadReason::FirstUse:
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::NativeGPU2DFullUploadFirstUseCount);
        break;
    case GPU2DNative::FullUploadReason::EpochChange:
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::NativeGPU2DFullUploadEpochChangeCount);
        break;
    case GPU2DNative::FullUploadReason::SemanticFrameGap:
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::NativeGPU2DFullUploadSemanticFrameGapCount);
        break;
    case GPU2DNative::FullUploadReason::CaptureGenerationRegression:
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::NativeGPU2DFullUploadCaptureRegressionCount);
        break;
    case GPU2DNative::FullUploadReason::None:
        break;
    }
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const u64 packStartNs = static_cast<u64>(std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
    u32* staging = static_cast<u32*>(nativeStaging.GetMappedPointer());
    bool packedNativeInput = staging != nullptr;
    if (packedNativeInput)
    {
        packedNativeInput = fullNativeUpload
            ? GPU2DNative::PackFrame(input, staging, GPU2DNative::PackedFrameWords)
            : GPU2DNative::PackFrameRanges(
                input, staging, GPU2DNative::PackedFrameWords, uploadPlan);
    }
    if (packedNativeInput)
    {
        packedNativeInput = GPU2DNative::PackHighResCaptureProvenance(
            staging, GPU2DNative::PackedFrameWords,
            ctx.HighResCapture->States(), input, pendingCompletionValue);
    }
    if (packedNativeInput)
    {
        for (u32 i = 0; i < uploadPlan.Count; ++i)
        {
            const GPU2DNative::DirtyRange& range = uploadPlan.Ranges[i];
            if (!nativeStaging.FlushRange(range.Offset, range.Size))
            {
                packedNativeInput = false;
                break;
            }
        }
    }
    if (!packedNativeInput)
    {
        ctx.HighResCapture->AbortFrame();
        Frames.SubmitFrame((*ctx.Device).GetMainQueue());
        ctx.Fail(ctx.User, "the native GPU2D input staging upload failed");
        return false;
    }
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const u64 packEndNs = static_cast<u64>(std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    VulkanPerf::AddCounter(VulkanPerf::Counter::NativeGPU2DPackNs,
        packEndNs - packStartNs);
#endif
    VulkanPerf::AddCounter(VulkanPerf::Counter::RecorderBlocksScanned,
        input.Recorder.BlocksScanned);
    VulkanPerf::AddCounter(VulkanPerf::Counter::RecorderBytesScanned,
        input.Recorder.BytesScanned);
    VulkanPerf::AddCounter(VulkanPerf::Counter::RecorderBlocksCopied,
        input.Recorder.BlocksCopied);
    VulkanPerf::AddCounter(VulkanPerf::Counter::RecorderBytesCopied,
        input.Recorder.BytesCopied);
    VulkanPerf::AddCounter(VulkanPerf::Counter::CaptureCPU2DLines,
        input.Recorder.CaptureCPU2DLines);
    VulkanPerf::AddCounter(VulkanPerf::Counter::CaptureCPU2DNs,
        input.Recorder.CaptureCPU2DNs);
    VulkanPerf::AddCounter(VulkanPerf::Counter::GPU2DRecorderNs,
        input.Recorder.GPU2DRecorderNs);
    VulkanPerf::AddCounter(VulkanPerf::Counter::TimelineRowDedupNs,
        input.Recorder.TimelineRowDedupNs);
    VulkanPerf::AddCounter(VulkanPerf::Counter::SpriteTimelineRowDedupNs,
        input.Recorder.SpriteTimelineRowDedupNs);
    VulkanPerf::AddCounter(VulkanPerf::Counter::NativeGPU2DInputPackBytes,
        uploadPlan.TotalBytes);
    VulkanPerf::AddCounter(VulkanPerf::Counter::NativeGPU2DVRAMUploadBytes,
        uploadPlan.EngineMemoryBytes + uploadPlan.FIFOBytes
            + uploadPlan.LCDVRAMBytes);
    VulkanPerf::AddCounter(VulkanPerf::Counter::NativeGPU2DPaletteUploadBytes,
        uploadPlan.PaletteBytes);
    VulkanPerf::AddCounter(VulkanPerf::Counter::NativeGPU2DOAMUploadBytes,
        uploadPlan.OAMBytes);
    VulkanPerf::AddCounter(VulkanPerf::Counter::MappedReadWordCalls,
        input.Recorder.MappedReadWordCalls);
    VulkanPerf::AddCounter(VulkanPerf::Counter::MappedReadFastPathCalls,
        input.Recorder.MappedReadFastPathCalls);
    VulkanPerf::AddCounter(VulkanPerf::Counter::MappedReadSlowPathCalls,
        input.Recorder.MappedReadSlowPathCalls);
    VulkanPerf::AddCounter(VulkanPerf::Counter::NativeCaptureHistoryScanLines,
        input.Recorder.NativeCaptureHistoryScanLines);
    VulkanPerf::AddCounter(VulkanPerf::Counter::NativeMappingBuildCalls,
        input.Recorder.NativeMappingBuildCalls);
    VulkanPerf::AddCounter(VulkanPerf::Counter::NativeMappingRowsUploaded,
        input.Recorder.NativeMappingRowsUploaded);
    VulkanPerf::AddCounter(VulkanPerf::Counter::NativeMappingBytesUploaded,
        input.Recorder.NativeMappingBytesUploaded);
    VulkanPerf::AddCounter(VulkanPerf::Counter::BGOverlayFastPath,
        input.Recorder.BGOverlayFastPath);
    VulkanPerf::AddCounter(VulkanPerf::Counter::BGOverlaySlowPath,
        input.Recorder.BGOverlaySlowPath);
    VulkanPerf::AddCounter(VulkanPerf::Counter::OBJOverlayFastPath,
        input.Recorder.OBJOverlayFastPath);
    VulkanPerf::AddCounter(VulkanPerf::Counter::OBJOverlaySlowPath,
        input.Recorder.OBJOverlaySlowPath);

    for (u32 i = 0; i < uploadPlan.Count; ++i)
    {
        const GPU2DNative::DirtyRange& range = uploadPlan.Ranges[i];
        VkBufferCopy copy{};
        copy.srcOffset = range.Offset;
        copy.dstOffset = range.Offset;
        copy.size = range.Size;
        fns.CmdCopyBuffer(cmd,
            nativeStaging.GetHandle(), nativeInputBuffer.GetHandle(),
            1, &copy);
    }
    const VkBuffer nativeInput = nativeInputBuffer.GetHandle();
    Vk::BufferBarrier((*ctx.Device), cmd, &nativeInput, 1,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

    // Keep the LCDC capture mirror GPU-resident. The CPU frame snapshot is
    // still the authoritative source for core-visible VRAM writes, but only
    // changed serialized LCD ranges are copied into the persistent tail of
    // ctx.BlendState-> Native capture commands then update that tail in
    // line order below, without a mandatory CPU readback.
    const VkBuffer nativeCapture = ctx.BlendState->GetHandle();
    const VkBuffer captureSidecar = ctx.Capture->GetSidecarHandle();
    const VkBuffer structuredOutput = structuredOutputBuffer.GetHandle();
    Vk::BufferBarrier((*ctx.Device), cmd, &nativeCapture, 1,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    const u32 lcdBegin = GPU2DNative::PackedLCDVRAMBase * sizeof(u32);
    const u32 lcdEnd = GPU2DNative::PackedRouteBase * sizeof(u32);
    const VkDeviceSize captureBase = static_cast<VkDeviceSize>(ctx.ScreenWidth)
        * static_cast<VkDeviceSize>(ctx.ScreenHeight) * sizeof(u32);
    const bool mirrorNeedsFullCopy = ctx.Provenance->MirrorNeedsFullCopy()
        || !semanticFrameContiguous
        || semanticCaptureGenerationRegressed;
    const auto copyCoherentCaptureRange = [&](u32 requestedBegin, u32 requestedEnd) {
        if (requestedEnd <= requestedBegin)
            return;
        for (u32 physicalIndex = 0;
            physicalIndex < CapturePhysicalBlockCount;
            ++physicalIndex)
        {
            const u32 blockBegin = lcdBegin
                + physicalIndex * CapturePhysicalBlockBytes;
            const u32 blockEnd = blockBegin + CapturePhysicalBlockBytes;
            const u32 begin = std::max(requestedBegin, blockBegin);
            const u32 end = std::min(requestedEnd, blockEnd);
            if (end <= begin)
                continue;

            const bool nativeOwner = IsNativeCaptureOwner(
                input.LCDVRAMProvenance[physicalIndex].Owner);
            if (nativeOwner)
            {
                // A native-owned block survives the FrameRecorder rollover;
                // copying its old CPU bytes would erase the GPU capture
                // mirror before the next semantic dispatch consumes it.
                GPU2DNative::RecordNativeOwnedCaptureCopySkipped();
                continue;
            }

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
            // Keep this assertion adjacent to the actual host upload. Any
            // future refactor that lets a native-owned block reach this point
            // would replay stale CPU VRAM into the persistent capture mirror.
            assert(!nativeOwner);
            if (nativeOwner)
            {
                GPU2DNative::RecordNativeOwnedHostReupload();
                continue;
            }
#endif

            VkBufferCopy captureCopy{};
            captureCopy.srcOffset = begin;
            captureCopy.dstOffset = captureBase + (begin - lcdBegin);
            captureCopy.size = end - begin;
            fns.CmdCopyBuffer(cmd, nativeStaging.GetHandle(),
                ctx.BlendState->GetHandle(), 1, &captureCopy);
        }
    };
    if (mirrorNeedsFullCopy)
        copyCoherentCaptureRange(lcdBegin, lcdEnd);
    else
    {
        for (u32 i = 0; i < input.DirtyRangeCount; ++i)
        {
            const GPU2DNative::DirtyRange& range = input.DirtyRanges[i];
            const u32 begin = std::max(range.Offset, lcdBegin);
            const u32 end = std::min(range.Offset + range.Size, lcdEnd);
            if (end <= begin)
                continue;
            copyCoherentCaptureRange(begin, end);
        }
    }
    Vk::BufferBarrier((*ctx.Device), cmd, &nativeCapture, 1,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    Vk::BufferBarrier((*ctx.Device), cmd, &captureSidecar, 1,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    ctx.FinalFB->RecordLayoutTransition(cmd,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

    const bool directImageOutput = outputSlot
        && outputSlot->DirectImageTop.IsValid()
        && outputSlot->DirectImageBottom.IsValid();
    const bool compositorDirectOutput = directImageOutput
        && (!diagnosticReadback
            || (GPU2DNative::DirectOutputDiagnosticsEnabled() && ctx.ScaleFactor == 1));
    if (compositorDirectOutput)
    {
        const auto beginDirectWrite = [&](Vk::Image& image) {
            const VkImageLayout previous = image.GetLayout();
            image.RecordLayoutTransition(
                cmd,
                VK_IMAGE_LAYOUT_GENERAL,
                previous == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                    : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                previous == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_WRITE_BIT);
        };
        beginDirectWrite(outputSlot->DirectImageTop);
        beginDirectWrite(outputSlot->DirectImageBottom);
    }

    // Stage A writes the logical 2D planes into StructuredInput. Keep this
    // descriptor set separate from the compositor set, which is updated only
    // after the logical dispatch has completed.
    if (!ctx.WriteDescriptorSet(
            ctx.User,
            frameIndex, NativeLogicalSetSlot, structuredOutputBuffer.GetHandle(),
            nativeInputBuffer.GetHandle(), VK_NULL_HANDLE, VK_NULL_HANDLE))
    {
        Frames.SubmitFrame((*ctx.Device).GetMainQueue());
        ctx.Fail(ctx.User, "could not write the native logical GPU2D descriptor set");
        return false;
    }

    VkDescriptorSet nativeSet = ctx.Descriptors->GetRasterizerSet(frameIndex, NativeLogicalSetSlot);
    fns.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.PipelineLayout,
        Vk::RasterizerSetIndex, 1, &nativeSet, 0, nullptr);

    Vk::RasterizerPushConstants push{};
    push.TexWidth = finalFBValid ? 1u : 0u;
    push.Padding = (compositorDirectOutput ? 1u : 0u)
        | (exactValidation ? 2u : 0u);
    fns.CmdPushConstants(cmd, ctx.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, Vk::PushConstantSize, &push);

    Vk::BeginCommandDebugLabel(fns, cmd, "Vulkan.Native.GPU2D");
    fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        ctx.Native);
    Frames.WriteTimestamp(
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        GpuMetricQueryIndex(GpuMetric::NativeGPU2DLogical, false));
    if (input.CaptureEnable != 0u)
    {
        Frames.WriteTimestamp(
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            GpuMetricQueryIndex(GpuMetric::NativeGPU2DCapture, false));
        const bool batchIndependentCapture =
            GPU2DNative::CanBatchIndependentCaptureFrame(input, finalFBValid);
        if (batchIndependentCapture)
        {
            VulkanPerf::AddCounter(
                VulkanPerf::Counter::NativeGPU2DCaptureRunCount);
            // The destination remains LCDC-only for the full frame, so it
            // cannot feed its own writes back through BG/OBJ. Build all
            // logical lines, publish them to the capture shader, then capture
            // every active scanline with one Y-expanded dispatch.
            const bool fuseObjRawLogical =
                GPU2DNative::CanFuseObjRawLogicalFrame(input);
            push.CaptureYOffset = 0;
            push.Padding = 32u | (fuseObjRawLogical ? (16u | 64u) : 0u);
            fns.CmdPushConstants(cmd, ctx.PipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, Vk::PushConstantSize, &push);
            fns.CmdDispatch(cmd, DivRoundUp(256u, nativeWorkgroupWidth), 384u, 1u);
            if (!fuseObjRawLogical)
            {
                Vk::BufferBarrier((*ctx.Device), cmd, &nativeCapture, 1,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT);
                push.Padding = 16u;
                fns.CmdPushConstants(cmd, ctx.PipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0, Vk::PushConstantSize, &push);
                fns.CmdDispatch(cmd, DivRoundUp(256u, nativeWorkgroupWidth), 384u, 1u);
            }

            Vk::BufferBarrier((*ctx.Device), cmd, &structuredOutput, 1,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT);

            push.Padding = 4u | 128u;
            fns.CmdPushConstants(cmd, ctx.PipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, Vk::PushConstantSize, &push);
            fns.CmdDispatch(cmd,
                DivRoundUp(static_cast<u32>(ctx.ScreenWidth), nativeWorkgroupWidth),
                GPU2DNative::ScreenHeight * static_cast<u32>(ctx.ScaleFactor), 1u);
            const VkBuffer captureOutputs[2] = {
                nativeCapture, captureSidecar};
            Vk::BufferBarrier((*ctx.Device), cmd, captureOutputs, 2,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            VulkanPerf::AddCounter(
                VulkanPerf::Counter::NativeGPU2DCaptureDispatchCount);
            VulkanPerf::AddCounter(
                VulkanPerf::Counter::NativeGPU2DCaptureBarrierCount, 1u);
            VulkanPerf::AddCounter(VulkanPerf::Counter::NativeGPU2DDispatchCount,
                fuseObjRawLogical ? 2u : 3u);
        }
        else
        {
            const GPU2DNative::CaptureRunPlan capturePlan =
                GPU2DNative::BuildCaptureRunPlan(input, finalFBValid);
            VulkanPerf::AddCounter(
                VulkanPerf::Counter::NativeGPU2DCaptureRunCount,
                capturePlan.RunCount);
            u64 dispatchCount = 0u;
            u64 captureDispatchCount = 0u;
            u64 captureBarrierCount = 0u;
            for (u32 runIndex = 0u; runIndex < capturePlan.RunCount; ++runIndex)
            {
                const GPU2DNative::CaptureLineRun& run =
                    capturePlan.Runs[runIndex];
                const bool fuseObjRawLogical =
                    GPU2DNative::CanFuseObjRawCaptureRun(input, run);
                if (run.Independent)
                {
                    // Rows for the two routed screens are disjoint in the
                    // structured buffer. Build each contiguous range, then
                    // publish the whole independent capture run once.
                    for (u32 screen = 0u; screen < 2u; ++screen)
                    {
                        push.CaptureYOffset = static_cast<s32>(
                            screen * GPU2DNative::ScreenHeight + run.LineBase);
                        push.Padding = 32u | 256u
                            | (fuseObjRawLogical ? (16u | 64u) : 0u);
                        fns.CmdPushConstants(cmd, ctx.PipelineLayout,
                            VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            Vk::PushConstantSize, &push);
                        fns.CmdDispatch(cmd,
                            DivRoundUp(256u, nativeWorkgroupWidth),
                            run.LineCount, 1u);
                        ++dispatchCount;
                    }
                    if (!fuseObjRawLogical)
                    {
                        Vk::BufferBarrier((*ctx.Device), cmd, &nativeCapture, 1,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_WRITE_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT);
                        ++captureBarrierCount;
                        for (u32 screen = 0u; screen < 2u; ++screen)
                        {
                            push.CaptureYOffset = static_cast<s32>(
                                screen * GPU2DNative::ScreenHeight + run.LineBase);
                            push.Padding = 16u | 256u;
                            fns.CmdPushConstants(cmd, ctx.PipelineLayout,
                                VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                Vk::PushConstantSize, &push);
                            fns.CmdDispatch(cmd,
                                DivRoundUp(256u, nativeWorkgroupWidth),
                                run.LineCount, 1u);
                            ++dispatchCount;
                        }
                    }

                    const VkBuffer logicalOutputs[2] = {
                        structuredOutput, nativeCapture};
                    Vk::BufferBarrier((*ctx.Device), cmd, logicalOutputs, 2,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                    ++captureBarrierCount;
                    push.CaptureYOffset = static_cast<s32>(run.LineBase);
                    push.Padding = 4u | 128u | 512u;
                    fns.CmdPushConstants(cmd, ctx.PipelineLayout,
                        VK_SHADER_STAGE_COMPUTE_BIT, 0,
                        Vk::PushConstantSize, &push);
                    fns.CmdDispatch(cmd,
                        DivRoundUp(static_cast<u32>(ctx.ScreenWidth), nativeWorkgroupWidth),
                        run.LineCount * static_cast<u32>(ctx.ScaleFactor), 1u);
                    ++dispatchCount;
                    ++captureDispatchCount;
                    const VkBuffer captureOutputs[2] = {
                        nativeCapture, captureSidecar};
                    Vk::BufferBarrier((*ctx.Device), cmd, captureOutputs, 2,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                    ++captureBarrierCount;
                    continue;
                }

                const u32 line = run.LineBase;
                const bool captureLineActive =
                    GPU2DNative::IsEffectiveCaptureLine(input, line);
                push.CaptureYOffset = static_cast<s32>(line);
                push.Padding = 32u | 8u
                    | (fuseObjRawLogical ? (16u | 64u) : 0u);
                fns.CmdPushConstants(cmd, ctx.PipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, Vk::PushConstantSize, &push);
                fns.CmdDispatch(cmd,
                    DivRoundUp(256u, nativeWorkgroupWidth), 2u, 1u);
                ++dispatchCount;
                if (!fuseObjRawLogical)
                {
                    Vk::BufferBarrier((*ctx.Device), cmd, &nativeCapture, 1,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT);
                    ++captureBarrierCount;
                    push.Padding = 16u | 8u;
                    fns.CmdPushConstants(cmd, ctx.PipelineLayout,
                        VK_SHADER_STAGE_COMPUTE_BIT, 0,
                        Vk::PushConstantSize, &push);
                    fns.CmdDispatch(cmd,
                        DivRoundUp(256u, nativeWorkgroupWidth), 2u, 1u);
                    ++dispatchCount;
                }
                if (captureLineActive)
                {
                    const VkBuffer logicalOutputs[2] = {
                        structuredOutput, nativeCapture};
                    Vk::BufferBarrier((*ctx.Device), cmd, logicalOutputs, 2,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                    ++captureBarrierCount;
                    push.Padding = 4u;
                    fns.CmdPushConstants(cmd, ctx.PipelineLayout,
                        VK_SHADER_STAGE_COMPUTE_BIT, 0,
                        Vk::PushConstantSize, &push);
                    fns.CmdDispatch(cmd,
                        DivRoundUp(static_cast<u32>(ctx.ScreenWidth), nativeWorkgroupWidth),
                        static_cast<u32>(ctx.ScaleFactor), 1u);
                    ++dispatchCount;
                    ++captureDispatchCount;
                    const VkBuffer captureOutputs[2] = {
                        nativeCapture, captureSidecar};
                    Vk::BufferBarrier((*ctx.Device), cmd, captureOutputs, 2,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                    ++captureBarrierCount;
                }
            }
            VulkanPerf::AddCounter(
                VulkanPerf::Counter::NativeGPU2DDispatchCount, dispatchCount);
            VulkanPerf::AddCounter(
                VulkanPerf::Counter::NativeGPU2DCaptureDispatchCount,
                captureDispatchCount);
            VulkanPerf::AddCounter(
                VulkanPerf::Counter::NativeGPU2DCaptureBarrierCount,
                captureBarrierCount);
        }
        Frames.WriteTimestamp(
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            GpuMetricQueryIndex(GpuMetric::NativeGPU2DCapture, true));
    }
    else
    {
        Frames.WriteTimestamp(
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            GpuMetricQueryIndex(GpuMetric::NativeGPU2DRaw, false));
        u64 dispatchCount = 0u;
        for (u32 runIndex = 0u;
            runIndex < semanticLinePlan.RunCount; ++runIndex)
        {
            const GPU2DNative::SemanticLineRun& run =
                semanticLinePlan.Runs[runIndex];
            const bool fuseObjRawLogical =
                GPU2DNative::CanFuseObjRawLogicalRun(input, run);
            push.CaptureYOffset = static_cast<s32>(run.RowBase);
            push.Padding = 32u | 256u
                | (fuseObjRawLogical ? (16u | 64u) : 0u);
            fns.CmdPushConstants(cmd, ctx.PipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, Vk::PushConstantSize, &push);
            fns.CmdDispatch(cmd, DivRoundUp(256u, nativeWorkgroupWidth),
                run.RowCount, 1u);
            ++dispatchCount;
            if (!fuseObjRawLogical)
            {
                Vk::BufferBarrier((*ctx.Device), cmd, &nativeCapture, 1,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

                push.Padding = 16u | 256u;
                fns.CmdPushConstants(cmd, ctx.PipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0,
                    Vk::PushConstantSize, &push);
                fns.CmdDispatch(cmd, DivRoundUp(256u, nativeWorkgroupWidth),
                    run.RowCount, 1u);
                ++dispatchCount;
            }
        }
        Frames.WriteTimestamp(
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            GpuMetricQueryIndex(GpuMetric::NativeGPU2DRaw, true));
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::NativeGPU2DDispatchCount, dispatchCount);
    }
    Frames.WriteTimestamp(
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        GpuMetricQueryIndex(GpuMetric::NativeGPU2DLogical, true));
    Vk::EndCommandDebugLabel(fns, cmd);

    // Stage A output is now the input to the existing high-resolution
    // compositor. This barrier is the only dependency between the two stages
    // in the normal path; capture adds the line-order barriers above for its
    // persistent LCDC mirror.
    Vk::BufferBarrier((*ctx.Device), cmd, &structuredOutput, 1,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    Vk::BufferBarrier((*ctx.Device), cmd, &captureSidecar, 1,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    if (stageDiagnostics)
    {
        // Stage A is the structured native logical contract.  The copy is
        // developer-only and is deliberately taken before Stage B consumes
        // the planes, so a blank can be attributed without guessing from the
        // final presenter image.
        Vk::BufferBarrier((*ctx.Device), cmd, &structuredOutput, 1,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferCopy structuredCopy{};
        structuredCopy.size = StructuredInputBytes;
        fns.CmdCopyBuffer(
            cmd,
            structuredOutputBuffer.GetHandle(),
            structuredReadback.GetHandle(), 1, &structuredCopy);
        const VkBuffer structuredReadbackHandle = structuredReadback.GetHandle();
        Vk::BufferBarrier((*ctx.Device), cmd, &structuredReadbackHandle, 1,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
        Vk::BufferBarrier((*ctx.Device), cmd, &structuredOutput, 1,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // Stage B is presentation work.  A backpressured semantic frame still
    // runs every Stage A/capture transition above, but does not rasterize a
    // visible image that cannot be published. Developer diagnostics keep the
    // old Stage B path and render into a lazily-created scratch buffer.
    bool directOutputReadback = false;
    if (outputSlot || diagnosticReadback)
    {
    Vk::Buffer& composedBuffer = outputSlot
        ? outputSlot->Composed : workSlot.DiagnosticComposed;
    if (!ctx.WriteDescriptorSet(
            ctx.User,
            frameIndex, CompositorSetSlot,
            composedBuffer.GetHandle(),
            structuredOutputBuffer.GetHandle(),
            compositorDirectOutput ? outputSlot->DirectImageTop.GetView() : VK_NULL_HANDLE,
            compositorDirectOutput ? outputSlot->DirectImageBottom.GetView() : VK_NULL_HANDLE))
    {
        Frames.SubmitFrame((*ctx.Device).GetMainQueue());
        ctx.Fail(ctx.User, "could not write the structured compositor descriptor set");
        return false;
    }
    VkDescriptorSet compositorSet = ctx.Descriptors->GetRasterizerSet(frameIndex, CompositorSetSlot);
    fns.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.PipelineLayout,
        Vk::RasterizerSetIndex, 1, &compositorSet, 0, nullptr);
    push.TexWidth = finalFBValid ? 1u : 0u;
    push.CaptureYOffset = 0;
    push.Padding = compositorDirectOutput ? 1u : 0u;
    fns.CmdPushConstants(cmd, ctx.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, Vk::PushConstantSize, &push);
    fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        ctx.Compositor);
    Frames.WriteTimestamp(
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        GpuMetricQueryIndex(GpuMetric::NativeGPU2DResolve, false));
    fns.CmdDispatch(cmd,
        DivRoundUp(static_cast<u32>(ctx.ScreenWidth), 8u),
        DivRoundUp(static_cast<u32>(ctx.ScreenHeight) * 2u, 8u), 1u);
    Frames.WriteTimestamp(
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        GpuMetricQueryIndex(GpuMetric::NativeGPU2DResolve, true));

    directOutputReadback = compositorDirectOutput
        && diagnosticReadback
        && GPU2DNative::DirectOutputDiagnosticsEnabled();
    if (compositorDirectOutput)
    {
        const auto finishDirectRead = [&](Vk::Image& image) {
            image.RecordLayoutTransition(
                cmd,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT);
        };
        if (directOutputReadback)
        {
            const auto copyDirectImage = [&](Vk::Image& image, VkDeviceSize offset) {
                image.RecordLayoutTransition(
                    cmd,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);
                VkBufferImageCopy copy{};
                copy.bufferOffset = offset;
                copy.bufferRowLength = 0;
                copy.bufferImageHeight = 0;
                copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.imageSubresource.mipLevel = 0;
                copy.imageSubresource.baseArrayLayer = 0;
                copy.imageSubresource.layerCount = 1;
                copy.imageExtent = {
                    static_cast<u32>(ctx.ScreenWidth),
                    static_cast<u32>(ctx.ScreenHeight), 1u};
                fns.CmdCopyImageToBuffer(
                    cmd, image.GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    nativeReadback.GetHandle(), 1, &copy);
                image.RecordLayoutTransition(
                    cmd,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT);
            };
            const VkDeviceSize screenBytes = static_cast<VkDeviceSize>(ctx.ScreenWidth)
                * static_cast<VkDeviceSize>(ctx.ScreenHeight) * sizeof(u32);
            copyDirectImage(outputSlot->DirectImageTop, 0);
            copyDirectImage(outputSlot->DirectImageBottom, screenBytes);
            const VkBuffer readback = nativeReadback.GetHandle();
            Vk::BufferBarrier((*ctx.Device), cmd, &readback, 1,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
        }
        else
        {
            finishDirectRead(outputSlot->DirectImageTop);
            finishDirectRead(outputSlot->DirectImageBottom);
        }
        VulkanPerf::AddCounter(VulkanPerf::Counter::DirectCompositorImageFrames);
    }
    else
    {
        VulkanPerf::AddCounter(VulkanPerf::Counter::FallbackCompositorBufferFrames);
    }

    if (diagnosticReadback && !directOutputReadback)
    {
        const VkBuffer composed = composedBuffer.GetHandle();
        Vk::BufferBarrier((*ctx.Device), cmd, &composed, 1,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        VkBufferCopy outputCopy{};
        outputCopy.size = NativeGPU2DOutputBytes;
        fns.CmdCopyBuffer(cmd,
            composedBuffer.GetHandle(),
            nativeReadback.GetHandle(),
            1, &outputCopy);

        const VkBuffer readback = nativeReadback.GetHandle();
        Vk::BufferBarrier((*ctx.Device), cmd, &readback, 1,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
        // Restore the storage-buffer access contract before the slot is
        // recycled. The exact-validation wait below is the only CPU/GPU
        // stall in this mode; production never records this copy.
        Vk::BufferBarrier((*ctx.Device), cmd, &composed, 1,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    }
    }

    const u64 submittedNativeFrame = Frames.GetCurrentRecordingFrameNumber();
    if (!Frames.SubmitFrame((*ctx.Device).GetMainQueue()))
    {
        ctx.HighResCapture->AbortFrame();
        ctx.Fail(ctx.User, "native GPU2D command submission failed");
        return false;
    }
    // Keep capture provenance independent of presentation frame-ring reuse.
    // Readback is ordered after this submission on the same queue; the
    // renderer-global serial is the identity validated by cross-frame sync.
    ctx.Provenance->CommitSubmissionSerial(pendingCompletionValue);
    ctx.Provenance->SetCompletionValue(pendingCompletionValue);
    ctx.HighResCapture->CommitFrame(pendingCaptureIdentity);
    if (outputSlot)
        outputSlot->LastSubmittedFrame = submittedNativeFrame;

    if (diagnosticReadback)
    {
        const VkResult waitResult = fns.WaitForFences(
            (*ctx.Device).GetHandle(), 1, &frame->InFlightFence, VK_TRUE,
            1000000000ull /* 1 s: developer exact gate only */);
        VulkanPerf::AddCounter(VulkanPerf::Counter::NativeGPU2DReadbackCount);
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::NativeGPU2DReadbackBytes, NativeGPU2DOutputBytes);
        if (waitResult != VK_SUCCESS)
        {
            ctx.Fail(ctx.User,
                ("native GPU2D exact readback did not complete: "
                    + Vk::FormatResult(waitResult)).c_str());
            return false;
        }
        if (!nativeReadback.Invalidate(0, NativeGPU2DOutputBytes))
        {
            ctx.Fail(ctx.User, "native GPU2D exact readback invalidation failed");
            return false;
        }
        const u8* source = nativeReadback.GetData();
        if (!source)
        {
            ctx.Fail(ctx.User, "native GPU2D exact readback is not mapped");
            return false;
        }

        std::unique_ptr<u32[]> actual(new u32[2u * GPU2DNative::ScreenPixelCount]);
        for (u32 i = 0; i < 2u * GPU2DNative::ScreenPixelCount; ++i)
        {
            u32 bgra8 = 0;
            std::memcpy(&bgra8, source + static_cast<size_t>(i) * sizeof(u32), sizeof(u32));
            const u32 red = directOutputReadback ? (bgra8 & 0xFFu) : (bgra8 >> 16u);
            const u32 green = (bgra8 >> 8u) & 0xFFu;
            const u32 blue = directOutputReadback ? (bgra8 >> 16u) : (bgra8 & 0xFFu);
            actual[i] = ((red & 0xFFu) >> 2u)
                | (((green >> 2u) & 0x3Fu) << 8u)
                | (((blue >> 2u) & 0x3Fu) << 16u);
        }
        const u32* structured = nullptr;
        if (stageDiagnostics)
        {
            if (!structuredReadback.Invalidate(0, StructuredInputBytes))
            {
                ctx.Fail(ctx.User, "native GPU2D Stage A readback invalidation failed");
                return false;
            }
            structured = reinterpret_cast<const u32*>(
                structuredReadback.GetData());
            if (!structured)
            {
                ctx.Fail(ctx.User, "native GPU2D Stage A readback is not mapped");
                return false;
            }
            GPU2DNative::LogStageSnapshot(
                "Vulkan", input.Generation.Frame, input.Generation.Frame,
                rendererSerial, generation,
                presentationAvailable ? nextSlot : frameIndex, input, structured,
                actual.get(), actual.get() + GPU2DNative::ScreenPixelCount,
                directOutputReadback ? "direct_image" : "composed_buffer",
                expectedTop, expectedBottom);
        }

        if (exactValidation)
        {
            const GPU2DNative::CompareResult result = GPU2DNative::CompareExact(
                expectedTop, expectedBottom,
                actual.get(), actual.get() + GPU2DNative::ScreenPixelCount);
            VulkanPerf::AddCounter(
                VulkanPerf::Counter::NativeGPU2DMismatchCount,
                result.TotalMismatchCount);
            if (!result.Exact())
            {
                if (result.SampleCount != 0u)
                {
                    const GPU2DNative::Mismatch& sample = result.Samples[0];
                    const u32 engine = input.ScreenSource[
                        sample.Screen * GPU2DNative::ScreenHeight + sample.Y] & 1u;
                    const GPU2DNative::LineState& state = input.Lines[
                        engine * GPU2DNative::ScreenHeight + sample.Y];
                    Platform::Log(Platform::LogLevel::Error,
                        "Vulkan native GPU2D exact mismatch frame=%llu total=%u top=%u bottom=%u "
                        "first=screen%u(%u,%u) expected=0x%08X actual=0x%08X engine=%u "
                        "DispCnt=0x%08X Layer=0x%08X OBJ=0x%08X Blank=0x%08X Unit=0x%08X "
                        "BGCnt=0x%08X/0x%08X/0x%08X/0x%08X "
                        "BGPos3=%u,%u WinRegs=0x%08X BlendCnt=0x%08X Master=0x%08X "
                        "Screens=%u/%u LineScreens=%u ExpectedRow0=%08X/%08X "
                        "Capture=0x%08X Route=%u/%u PaletteA0=0x%02X%02X PaletteB0=0x%02X%02X "
                        "BG0=0x%02X%02X%02X%02X BG4000=0x%02X%02X BG4040=0x%02X%02X\n",
                        static_cast<unsigned long long>(generation),
                        result.TotalMismatchCount, result.TopMismatchCount,
                        result.BottomMismatchCount, sample.Screen, sample.X, sample.Y,
                        sample.Expected, sample.Actual, engine, state.DispCnt,
                        state.LayerEnable, state.OBJEnable, state.ForcedBlank,
                        state.UnitEnabled, state.BGCnt[0], state.BGCnt[1],
                        state.BGCnt[2], state.BGCnt[3], state.BGXPos[3],
                        state.BGYPos[3], state.WinRegs, state.BlendCnt,
                        state.MasterBrightness, input.ScreensEnabled,
                        input.ScreenSwap, state.ScreensEnabled,
                        expectedTop[0], expectedBottom[0], state.CaptureCnt,
                        input.ScreenSource[0u * GPU2DNative::ScreenHeight + sample.Y],
                        input.ScreenSource[1u * GPU2DNative::ScreenHeight + sample.Y],
                        input.Palette[1u], input.Palette[0u],
                        input.Palette[1025u], input.Palette[1024u],
                        input.Engine[engine].BGVRAM[0],
                        input.Engine[engine].BGVRAM[1],
                        input.Engine[engine].BGVRAM[2],
                        input.Engine[engine].BGVRAM[3],
                        input.Engine[engine].BGVRAM[0x4001u],
                        input.Engine[engine].BGVRAM[0x4000u],
                        input.Engine[engine].BGVRAM[0x4041u],
                        input.Engine[engine].BGVRAM[0x4040u]);
                }
                ctx.Fail(ctx.User, "native GPU2D exact differential mismatch");
                return false;
            }
        }
    }

    nativeUploadInitialized = true;
    uploadedNativeGeneration = input.Generation;
    ctx.Provenance->RecordSemanticSubmission(
        input.Generation.Frame, input.Generation.CaptureGeneration);
    VulkanPerf::AddCounter(VulkanPerf::Counter::NativeGPU2DFrames);
    GPU2DNative::LogSemanticIdentity(
        "Vulkan", input.Generation.Frame, input.Generation.CaptureGeneration,
        ctx.Provenance->GetEpoch(), outputSlot != nullptr, forcedPresentationStall,
        mirrorNeedsFullCopy,
        outputSlot != nullptr ? nextSlot : FramesInFlight);
    if (!outputSlot)
    {
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::NativeGPU2DPresentationBackpressureFrames);
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::NativeGPU2DSemanticOnlyFrames);
        LastComposeResult = GPU2DComposeResult::SemanticOnly;
        return false;
    }
    {
        const auto lock = Output->Ring.LockPublication();
        outputSlot->Frame.Serial = rendererSerial;
        outputSlot->Frame.Generation = generation;
        outputSlot->Frame.Epoch = ctx.Provenance->GetEpoch();
        outputSlot->Frame.DirectContentValid = compositorDirectOutput;
        Output->Ring.PublishReserved(nextSlot);
        ComposedGeneration = generation;
        PublishedOutputGeneration = generation;
        ComposedOutputValid = true;
    }
    if (stageDiagnostics)
    {
        GPU2DNative::LogPresentedIdentity(
            "Vulkan", input.Generation.Frame, outputSlot->Frame.Serial,
            generation, outputSlot->Frame.Epoch, nextSlot);
    }
    LastComposeResult = GPU2DComposeResult::Success;
    return true;
}

// ---------------------------------------------------------------------------
// Output resource lifecycle
//
// These were the renderer's. Moving them is the whole point of this pass: the
// compositor already declared itself the owner of the output resource set and
// of publication state, and a declared owner that does not create, release or
// reset its own state is only half an owner.
// ---------------------------------------------------------------------------

bool VulkanGpu2DComposer::RecreateOutput(
    const VulkanDevice& device, u32 width, u32 height, u64 epoch)
{
    // Build the candidate first and adopt it only once Create() has fully
    // succeeded. A partially initialized set must never be reachable as the
    // active Output: the presenter and the compose path both read it without
    // asking whether it finished being built.
    auto candidate = std::make_shared<VulkanGpu2DOutput>();
    if (!candidate->Create(
            device, width, height, NextOutputResourceGeneration, epoch))
        return false;

    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Info,
        "%s compositor output created %ux%u resourceGeneration=%llu epoch=%llu\n",
        "Vulkan", width, height,
        static_cast<unsigned long long>(NextOutputResourceGeneration),
        static_cast<unsigned long long>(epoch));

    NextOutputResourceGeneration++;
    Output = std::move(candidate);

    // A new resource set has published nothing yet.
    ComposedOutputValid = false;
    ComposedGeneration = 0;
    PublishedOutputGeneration = 0;
    return true;
}

void VulkanGpu2DComposer::ReleaseOutput() noexcept
{
    // Detach, do not destroy. A RendererOutputLease captured its own
    // shared_ptr to this set, so a presenter still reading the old resources
    // across a resolution change keeps them alive until it releases.
    Output.reset();

    // The publication state described the set that just went away.
    ComposedOutputValid = false;
    ComposedGeneration = 0;
    PublishedOutputGeneration = 0;

    // NextOutputResourceGeneration deliberately survives: it is a lifetime
    // identity, and reusing a number would let a presenter mistake a new set
    // for one it had already cached descriptors against.
}

void VulkanGpu2DComposer::ResetForRendererEpoch(
    u64 epoch, bool preservePresentation) noexcept
{
    bool keepPublishedOutput = false;
    int publishedSlot = -1;
    if (Output)
    {
        const auto lock = Output->Ring.LockPublication();
        publishedSlot = Output->Ring.GetPublishedSlot();
        keepPublishedOutput = preservePresentation
            && ComposedOutputValid
            && publishedSlot >= 0
            && static_cast<std::size_t>(publishedSlot) < Output->Slots.size();
        if (!keepPublishedOutput)
        {
            Output->Ring.Unpublish();
            ComposedOutputValid = false;
            ComposedGeneration = 0;
            PublishedOutputGeneration = 0;
        }
        else
            ComposedOutputValid = true;
        for (std::size_t slotIndex = 0; slotIndex < Output->Slots.size(); ++slotIndex)
        {
            if (keepPublishedOutput && static_cast<int>(slotIndex) == publishedSlot)
            {
                // This slot is the last complete presentation surface. Keep
                // its resource identity and frame metadata; only unpublished
                // ring slots are reset for the next complete frame.
                continue;
            }
            VulkanGpu2DOutput::Slot& slot = Output->Slots[slotIndex];
            slot.UploadedContentGeneration = {};
            slot.StructuredUploadInitialized = false;
            slot.Frame.DirectContentValid = false;
            slot.Frame.Epoch = epoch;
        }
        for (VulkanGpu2DOutput::ComposeWorkSlot& slot : Output->WorkSlots)
        {
            slot.UploadedNativeGeneration = {};
            slot.SemanticLines.Reset();
            slot.NativeUploadInitialized = false;
        }
    }
    if (!keepPublishedOutput && !Output)
    {
        ComposedOutputValid = false;
        ComposedGeneration = 0;
        PublishedOutputGeneration = 0;
    }
}

void VulkanGpu2DComposer::MarkFatal() noexcept
{
    LastComposeResult = GPU2DComposeResult::Fatal;
}

RendererOutput VulkanGpu2DComposer::GetComposedOutput() const
{
    const std::shared_ptr<VulkanGpu2DOutput> state = Output;
    if (!state || !ComposedOutputValid)
        return {};

    const auto lock = state->Ring.LockPublication();
    if (state->Ring.GetPublishedSlot() < 0)
        return {};
    const VulkanPresentedFrame& frame =
        state->Slots[state->Ring.GetPublishedSlot()].Frame;
    return RendererOutput::VulkanBuffer(
        const_cast<VulkanPresentedFrame*>(&frame), frame.Width, frame.Height,
        frame.Serial, frame.Epoch);
}

RendererOutputLease VulkanGpu2DComposer::AcquireComposedOutputLease()
{
    const std::shared_ptr<VulkanGpu2DOutput> state = Output;
    if (!state || !ComposedOutputValid)
        return {};

    const auto lock = state->Ring.LockPublication();
    const int slotIndex = state->Ring.GetPublishedSlot();
    if (slotIndex < 0)
        return {};

    VulkanGpu2DOutput::Slot& slot = state->Slots[slotIndex];
    auto* leaseCounter = state->Ring.AcquireLease(static_cast<u32>(slotIndex));
    GPU2DNative::LogPresentedIdentity(
        "Vulkan", slot.Frame.Generation, slot.Frame.Serial,
        slot.Frame.Generation, slot.Frame.Epoch, static_cast<u32>(slotIndex));

    // The lease captures `state`, not a raw pointer. That is what keeps a
    // resource set alive across a resolution change while the presenter is
    // still reading it.
    return RendererOutputLease(
        RendererOutput::VulkanBuffer(
            &slot.Frame, slot.Frame.Width, slot.Frame.Height,
            slot.Frame.Serial, slot.Frame.Epoch),
        leaseCounter,
        &RendererOutputRing::LeaseCounter::Release,
        state);
}


} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
