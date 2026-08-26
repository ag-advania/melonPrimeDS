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

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "DX12Gpu2DComposer.h"

#include <cassert>

#include "CaptureProvenanceState.h"
#include "DX12CaptureBridge.h"
#include "DX12Context.h"
#include "DX12GpuTimestamp.h"
#include "DX12Perf.h"
#include "GpuStageMetrics.h"
#include "Platform.h"
#include "StructuredUploadPlan.h"

namespace melonDS
{

using namespace DX12Gpu2D;

// The root-signature binding contract the compose lists record against. Same
// aliases the renderer uses; both are compiled against one root signature.
constexpr u32 kUavTableSize = DX12RootSignatureLayout::UavTableSize;
constexpr u32 kRootParamUavTable = DX12RootSignatureLayout::ParamUavTable;

using DX12::AlignUp;
using DX12::DivRoundUp;

bool DX12Gpu2DComposer::CreateDescriptors(
    ID3D12Device* device, u32 uavTableSize, u32 framesInFlight)
{
    // One UAV block per presentation slot, one per native work slot, and four
    // per work slot for the structured path -- the same sizing the renderer
    // used before these rings moved here.
    if (!OutputUav.Init(device, uavTableSize * framesInFlight, false))
        return false;
    if (!WorkNativeUav.Init(device, uavTableSize * framesInFlight, false))
        return false;
    if (!WorkOutputUav.Init(device, uavTableSize * framesInFlight * 4u, false))
        return false;
    return true;
}

void DX12Gpu2DComposer::ShutdownDescriptors() noexcept
{
    OutputUav.Shutdown();
    WorkOutputUav.Shutdown();
    WorkNativeUav.Shutdown();
    OutputUavCpu = {};
    WorkNativeUavCpu = {};
    WorkOutputUavCpu = {};
}

void DX12Gpu2DComposer::ReleasePipelines() noexcept
{
    Compositor.Reset();
    CorrectCoverage.Reset();
    Native.Reset();
    NativeCapture.Reset();
}

bool DX12Gpu2DOutput::ComposeWorkSlot::EnsureDiagnosticResources(
    DX12Context& context,
    u64 outputBytes,
    u64 structuredBytes,
    bool needDiagnosticComposed,
    bool needStructuredReadback)
{
    if (needDiagnosticComposed && !DiagnosticComposed)
    {
        DiagnosticComposed = context.CreateBuffer(
            outputBytes, D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            L"MelonPrime DX12 diagnostic composed output");
        if (!DiagnosticComposed)
            return false;
    }
    if (!NativeReadback)
    {
        NativeReadback = context.CreateBuffer(
            outputBytes, D3D12_HEAP_TYPE_READBACK,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_FLAG_NONE,
            L"MelonPrime DX12 native GPU2D diagnostic readback");
        if (!NativeReadback)
            return false;
        if (FAILED(NativeReadback->Map(
                0, nullptr,
                reinterpret_cast<void**>(&NativeReadbackMapped)))
            || !NativeReadbackMapped)
        {
            NativeReadback.Reset();
            return false;
        }
    }
    if (needStructuredReadback && !StructuredReadback)
    {
        StructuredReadback = context.CreateBuffer(
            structuredBytes, D3D12_HEAP_TYPE_READBACK,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_FLAG_NONE,
            L"MelonPrime DX12 GPU2D Stage A diagnostic readback");
        if (!StructuredReadback)
            return false;
        if (FAILED(StructuredReadback->Map(
                0, nullptr,
                reinterpret_cast<void**>(&StructuredReadbackMapped)))
            || !StructuredReadbackMapped)
        {
            StructuredReadback.Reset();
            return false;
        }
    }
    return true;
}

DX12Gpu2DOutput::~DX12Gpu2DOutput()
{
    for (Slot& slot : Slots)
    {
        slot.Commands.WaitIdle();
        if (slot.StructuredStaging && slot.StructuredMapped)
        {
            D3D12_RANGE noWrite{0, 0};
            slot.StructuredStaging->Unmap(0, &noWrite);
            slot.StructuredMapped = nullptr;
        }
        slot.Descriptors.Shutdown();
        slot.Commands.Shutdown();
    }
    for (ComposeWorkSlot& slot : WorkSlots)
    {
        slot.Commands.WaitIdle();
        if (slot.NativeStaging && slot.NativeMapped)
        {
            D3D12_RANGE noWrite{0, 0};
            slot.NativeStaging->Unmap(0, &noWrite);
            slot.NativeMapped = nullptr;
        }
        if (slot.NativeReadback && slot.NativeReadbackMapped)
        {
            D3D12_RANGE noWrite{0, 0};
            slot.NativeReadback->Unmap(0, &noWrite);
            slot.NativeReadbackMapped = nullptr;
        }
        if (slot.StructuredReadback && slot.StructuredReadbackMapped)
        {
            D3D12_RANGE noWrite{0, 0};
            slot.StructuredReadback->Unmap(0, &noWrite);
            slot.StructuredReadbackMapped = nullptr;
        }
        slot.Descriptors.Shutdown();
        slot.Commands.Shutdown();
    }
    if (OwnsContextReference && Context)
        Context->Release();
}

bool DX12Gpu2DOutput::Create(
    DX12Context& context, u32 width, u32 height, u32 uavTableSize,
    u64 resourceGeneration, u64 epoch)
{
    if (!context.Acquire())
        return false;
    Context = &context;
    OwnsContextReference = true;
    ResourceGeneration = resourceGeneration;

    ID3D12Device* device = context.GetDevice();
    const u64 inputBytes = static_cast<u64>(kCompositionInputDwords) * sizeof(u32);
    const u64 screenBytes = static_cast<u64>(width) * height * sizeof(u32);
    D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport{};
    formatSupport.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    const bool directFormatSupported =
        SUCCEEDED(device->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT,
            &formatSupport,
            sizeof(formatSupport)))
        && (formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0
        && (formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) != 0
        && (formatSupport.Support1
                & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0
        && (formatSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
    DirectTextureEnabled = directFormatSupported;
    if (!DirectTextureEnabled)
    {
        Platform::Log(
            Platform::LogLevel::Warn,
            "DX12: compositor direct texture disabled: RGBA8 lacks sampled or typed UAV support\n");
    }
    for (Slot& slot : Slots)
    {
        // Native GPU2D records Stage A and the structured compositor in
        // the same command list, so it binds two complete UAV tables
        // before the slot is submitted. Keep enough ring space for both
        // bindings; the regular compositor path consumes one table.
        if (!slot.Commands.Init(device, context.GetQueue())
            || !slot.Descriptors.Init(device, uavTableSize * 2u, true))
            return false;
        slot.StructuredInput = context.CreateBuffer(
            inputBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            L"MelonPrime DX12 structured input slot");
        slot.StructuredStaging = context.CreateBuffer(
            inputBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_FLAG_NONE,
            L"MelonPrime DX12 structured staging slot");
        slot.Composed = context.CreateBuffer(
            screenBytes * 2u, D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            L"MelonPrime DX12 composed output slot");
        if (!slot.StructuredInput || !slot.StructuredStaging
            || !slot.Composed)
            return false;

        D3D12_RANGE noRead{0, 0};
        if (FAILED(slot.StructuredStaging->Map(
                0, &noRead, reinterpret_cast<void**>(&slot.StructuredMapped)))
            || !slot.StructuredMapped)
            return false;
    }

    for (ComposeWorkSlot& slot : WorkSlots)
    {
        if (!slot.Commands.Init(device, context.GetQueue())
            || !slot.Descriptors.Init(device, uavTableSize * 2u, true))
            return false;
        slot.NativeInput = context.CreateBuffer(
            kNativeGPU2DInputBytes, D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            L"MelonPrime DX12 native GPU2D work input slot");
        slot.NativeStaging = context.CreateBuffer(
            kNativeGPU2DInputBytes, D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_FLAG_NONE,
            L"MelonPrime DX12 native GPU2D work staging slot");
        slot.StructuredInput = context.CreateBuffer(
            inputBytes, D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            L"MelonPrime DX12 native GPU2D work structured slot");
        if (!slot.NativeInput || !slot.NativeStaging || !slot.StructuredInput)
            return false;
        D3D12_RANGE noRead{0, 0};
        if (FAILED(slot.NativeStaging->Map(
                0, &noRead, reinterpret_cast<void**>(&slot.NativeMapped)))
            || !slot.NativeMapped)
            return false;
    }

    if (DirectTextureEnabled)
    {
        for (Slot& slot : Slots)
        {
            slot.DirectTexture = context.CreateTexture2D(
                DXGI_FORMAT_R8G8B8A8_UNORM,
                width,
                height,
                2,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                L"MelonPrime DX12 direct compositor output slot");
            if (!slot.DirectTexture)
            {
                DirectTextureEnabled = false;
                break;
            }
        }
    }
    if (!DirectTextureEnabled)
    {
        for (Slot& slot : Slots)
        {
            slot.DirectTexture.Reset();
            slot.DirectTextureInShaderResource = false;
        }
    }

    for (Slot& slot : Slots)
    {
        slot.Frame.Buffer = slot.Composed.Get();
        slot.Frame.DirectTexture = DirectTextureEnabled
            ? slot.DirectTexture.Get() : nullptr;
        slot.Frame.TopOffset = 0;
        slot.Frame.BottomOffset = screenBytes;
        slot.Frame.Width = width;
        slot.Frame.Height = height;
        slot.Frame.Epoch = epoch;
        slot.Frame.ResourceGeneration = ResourceGeneration;
        slot.Frame.DirectContentValid = false;
    }
    return true;
}

bool DX12Gpu2DComposer::BindCompositionUavTable(
    const DX12Gpu2DComposeContext& ctx,
    ID3D12GraphicsCommandList* list,
    DX12DescriptorRing& descriptors,
    D3D12_CPU_DESCRIPTOR_HANDLE canonicalCpu)
{
    DX12Perf::ScopedCpuTimer descriptorTimer(DX12Perf::CpuMetric::DescriptorUpdate);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    if (!descriptors.Allocate(kUavTableSize, cpu, gpu))
        return false;

    if (!canonicalCpu.ptr)
        return false;
    ctx.Context->GetDevice()->CopyDescriptorsSimple(
        kUavTableSize,
        cpu,
        canonicalCpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorCopyCount, kUavTableSize);
    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorWriteCount, kUavTableSize);
    DX12Perf::AddCounter(DX12Perf::Counter::CompositorDescriptorUpdateCount, kUavTableSize);
    list->SetComputeRootDescriptorTable(kRootParamUavTable, gpu);
    return true;
}

bool DX12Gpu2DComposer::CanComposeNativeGPU2D(
    const DX12Gpu2DComposeContext& ctx) const noexcept
{
    return !ctx.RendererFailed
        && ctx.ShadersReady
        && ctx.Context
        && Native
        && Output
        && ctx.FinalFB;
}

bool DX12Gpu2DComposer::ComposeStructuredOutput(
    const DX12Gpu2DComposeContext& ctx,
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
    if (!ctx.ShadersReady)
        return false;
    if (ComposedOutputValid && ComposedGeneration == generation)
    {
        LastComposeResult = GPU2DComposeResult::Success;
        return true;
    }
    const std::shared_ptr<DX12Gpu2DOutput> state = Output;
    if (!ctx.Context || !ctx.CaptureSidecar || !Compositor
        || !state || !ctx.FinalFB || !ctx.Capture->GetSidecarBuffer())
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
    u32 slotIndex = 0;
    bool slotLeased = false;
    {
        const auto lock = state->Ring.LockPublication();
        slotIndex = state->Ring.TakeCursorSlot();
        slotLeased = state->Ring.IsLeased(slotIndex);
    }
    DX12Gpu2DOutput::Slot& slot = state->Slots[slotIndex];
    if (slotLeased)
    {
        // Presentation is still reading this slot. Reusing the last published
        // frame is preferable to blocking the emulation thread.
        DX12Perf::AddCounter(DX12Perf::Counter::CompositorDropCount);
        LastComposeResult = GPU2DComposeResult::Backpressure;
        return false;
    }
    ID3D12GraphicsCommandList* list = slot.Commands.TryBegin();
    if (!list)
    {
        // The GPU has not retired this ring slot after three frames. Keep the
        // previous output and let the emulator continue without a fence wait.
        DX12Perf::AddCounter(DX12Perf::Counter::CompositorDropCount);
        LastComposeResult = GPU2DComposeResult::Backpressure;
        return false;
    }
    RecordDX12GpuMetric(
        slot.Commands, GpuMetric::CaptureSidecar,
        DX12Perf::Counter::CaptureSidecarGpuTimeNs);
    RecordDX12GpuMetric(
        slot.Commands, GpuMetric::StructuredCompositor,
        DX12Perf::Counter::StructuredCompositorGpuTimeNs);
    RecordDX12GpuMetric(
        slot.Commands, GpuMetric::StructuredCompositor,
        DX12Perf::Counter::CompositorGpuTimeNs);
    slot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::StructuredCompositor, false));

    // Which units changed, and which byte runs that collapses into, is a
    // content-generation question rather than a D3D12 one, so both backends
    // ask the same function.
    constexpr u32 logicalUnitCount = StructuredComposition::UploadUnitCount;
    const u64 planeBytes = static_cast<u64>(kStructuredPixelCount) * sizeof(u32);
    const u64 lineMetaBytes = 192u * sizeof(u32);
    const u64 captureCommandBytes =
        192u * StructuredComposition::kCaptureCommandWords * sizeof(u32);
    const StructuredComposition::StructuredUploadPlan structuredUpload =
        StructuredComposition::BuildStructuredUploadPlan(
            contentGeneration,
            slot.UploadedContentGeneration,
            slot.StructuredUploadInitialized,
            planeBytes,
            lineMetaBytes,
            captureCommandBytes);
    // Retained for the upload-shape counters below: a slot that had never
    // been written is a different event from one whose planes all changed.
    const bool fullUpload = !slot.StructuredUploadInitialized;
    const auto& dirty = structuredUpload.Dirty;
    const auto& unitOffsets = structuredUpload.UnitOffsets;
    const auto& unitSizes = structuredUpload.UnitSizes;
    const auto& ranges = structuredUpload.Ranges;
    const std::size_t rangeCount = structuredUpload.RangeCount;
    const bool captureClassificationDirty =
        dirty[StructuredComposition::CaptureCommandUnit];
    const bool uploadRequired = structuredUpload.Required();
    u64 packedBytes = 0;
    u32 routeRuns = 0;
    std::array<bool, 2> routeRunsCounted{};

    if (uploadRequired)
    {
        u32* staging = slot.StructuredMapped;
        if (!staging)
        {
            slot.Commands.Submit();
            ctx.Fail(ctx.User, "the compositor staging slot is not mapped");
            return false;
        }
        {
            DX12Perf::ScopedCpuTimer packTimer(DX12Perf::CpuMetric::ComposePack);
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
                            staging + static_cast<std::size_t>(unit) * kStructuredPixelCount,
                            screen, plane, screenRouting);
                    if (!screenPack.Valid)
                    {
                        slot.Commands.Submit();
                        return false;
                    }
                    if (!routeRunsCounted[screen])
                    {
                        routeRuns += screenPack.RouteRuns;
                        routeRunsCounted[screen] = true;
                    }
                }
                else if (unit < 14u)
                {
                    std::memcpy(
                        staging + static_cast<std::size_t>(unit) * kStructuredPixelCount,
                        planes[unit], static_cast<std::size_t>(kStructuredPixelCount) * sizeof(u32));
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
        DX12Perf::AddCounter(DX12Perf::Counter::StructuredPackBytes, packedBytes);
        DX12Perf::AddCounter(DX12Perf::Counter::StructuredInputBytesPacked, packedBytes);
        DX12Perf::AddCounter(DX12Perf::Counter::StructuredRouteRuns, routeRuns);
    }

    DX12Perf::ScopedCpuTimer recordTimer(DX12Perf::CpuMetric::ComposeRecord);

    slot.Descriptors.Reset();
    ctx.InvalidateSrvCache(ctx.User);

    if (uploadRequired)
    {
        for (std::size_t i = 0; i < rangeCount; ++i)
        {
            list->CopyBufferRegion(
                slot.StructuredInput.Get(), ranges[i].Offset,
                slot.StructuredStaging.Get(), ranges[i].Offset, ranges[i].Size);
        }
    }
    DX12TransitionBuffer(
        list,
        slot.StructuredInput.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (slot.DirectTexture && slot.DirectTextureInShaderResource)
    {
        DX12TransitionBuffer(
            list,
            slot.DirectTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        slot.DirectTextureInShaderResource = false;
    }

    // The 3D final pass was submitted immediately before this list on the same
    // queue. This cross-list UAV barrier makes those writes visible without a
    // CPU fence wait.
    DX12InsertUavBarrier(list, ctx.FinalFB);
    DX12InsertUavBarrier(list, ctx.Capture->GetSidecarBuffer());

    ID3D12DescriptorHeap* heaps[] = { slot.Descriptors.GetHeap() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(ctx.RootSignature);
    if (!BindCompositionUavTable(
            ctx,
            list, slot.Descriptors, OutputUavCpu[slotIndex]))
    {
        DX12TransitionBuffer(
            list,
            slot.StructuredInput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_DEST);
        slot.Commands.Submit();
        ctx.Fail(ctx.User, "could not bind the compositor descriptor table");
        return false;
    }

    DX12DispatchUniform constants = ctx.Dispatch;
    // The 3D X scroll now travels per scanline in the structured line
    // metadata, so the compositor no longer needs it as a frame-global value.
    constants.TexWidth = ctx.AbortFrame ? 0u : 1u;
    constants.Pad = slot.DirectTexture ? 1u : 0u;
    slot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::CaptureSidecar, false));
    list->SetPipelineState(ctx.CaptureSidecar);
    u32 sidecarDispatchCount = 0;
    u32 sidecarBarrierCount = 0;
    for (u32 captureLine = 0; captureLine < 192u;)
    {
        if ((captureCommands[captureLine * StructuredComposition::kCaptureCommandWords + 1u]
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
            constants.TexHeight = runStart;
            constants.Pad = (slot.DirectTexture ? 1u : 0u) | 2u;
            DX12SetDispatchConstants(list, constants);
            list->Dispatch(
                DivRoundUp(static_cast<u32>(ctx.ScreenWidth), 8u),
                DivRoundUp(static_cast<u32>(ctx.ScaleFactor), 8u),
                captureLine - runStart);
            ++sidecarDispatchCount;
            DX12InsertUavBarrier(list, ctx.Capture->GetSidecarBuffer());
            ++sidecarBarrierCount;
            continue;
        }

        constants.TexHeight = captureLine;
        constants.Pad = slot.DirectTexture ? 1u : 0u;
        DX12SetDispatchConstants(list, constants);
        list->Dispatch(
            DivRoundUp(static_cast<u32>(ctx.ScreenWidth), 8u),
            DivRoundUp(static_cast<u32>(ctx.ScaleFactor), 8u),
            1u);
        ++sidecarDispatchCount;
        DX12InsertUavBarrier(list, ctx.Capture->GetSidecarBuffer());
        ++sidecarBarrierCount;
        ++captureLine;
    }
    DX12Perf::AddCounter(
        DX12Perf::Counter::CaptureValidLineCount, captureAnalysis.ValidLineCount);
    DX12Perf::AddCounter(
        DX12Perf::Counter::CaptureIndependentLineCount,
        captureAnalysis.IndependentLineCount);
    DX12Perf::AddCounter(
        DX12Perf::Counter::CaptureLegacyOrderedLineCount,
        captureAnalysis.LegacyOrderedLineCount);
    DX12Perf::AddCounter(
        DX12Perf::Counter::CaptureSidecarDispatchCount, sidecarDispatchCount);
    DX12Perf::AddCounter(
        DX12Perf::Counter::CaptureSidecarBarrierCount, sidecarBarrierCount);
    slot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::CaptureSidecar, true));

    constants.TexHeight = 0u;
    constants.Pad = slot.DirectTexture ? 1u : 0u;
    DX12SetDispatchConstants(list, constants);
    list->SetPipelineState(Compositor.Get());
    list->Dispatch(
        DivRoundUp(static_cast<u32>(ctx.ScreenWidth), 8u),
        DivRoundUp(static_cast<u32>(ctx.ScreenHeight) * 2u, 8u),
        1u);
    slot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::StructuredCompositor, true));
    if (slot.DirectTexture)
    {
        DX12InsertUavBarrier(list, slot.DirectTexture.Get());
        DX12TransitionBuffer(
            list,
            slot.DirectTexture.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        slot.DirectTextureInShaderResource = true;
        DX12Perf::AddCounter(DX12Perf::Counter::DirectCompositorImageFrames);
    }
    else
    {
        DX12InsertUavBarrier(list, slot.Composed.Get());
        DX12Perf::AddCounter(DX12Perf::Counter::FallbackCompositorBufferFrames);
    }
    DX12TransitionBuffer(
        list,
        slot.StructuredInput.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);

    bool submitted = false;
    {
        DX12Perf::ScopedCpuTimer submitTimer(DX12Perf::CpuMetric::QueueSubmit);
        submitted = slot.Commands.Submit();
    }
    if (!submitted)
    {
        ctx.Fail(ctx.User, "compositor command submission failed");
        return false;
    }

    if (uploadRequired)
    {
        u64 uploadedBytes = 0;
        for (std::size_t i = 0; i < rangeCount; ++i)
            uploadedBytes += ranges[i].Size;
        DX12Perf::AddCounter(
            DX12Perf::Counter::StructuredInputBytesUploaded, uploadedBytes);
        DX12Perf::AddCounter(
            DX12Perf::Counter::StructuredInputCopyRegionCount,
            static_cast<u64>(rangeCount));
        DX12Perf::AddCounter(
            fullUpload
                ? DX12Perf::Counter::StructuredInputFullUploadCount
                : DX12Perf::Counter::StructuredInputPartialUploadCount);
    }
    slot.UploadedContentGeneration = contentGeneration;
    slot.StructuredUploadInitialized = true;

    {
        const auto lock = state->Ring.LockPublication();
        slot.Frame.Serial = state->Ring.PublishNext(slotIndex);
        slot.Frame.Generation = generation;
        slot.Frame.DirectContentValid = slot.DirectTexture.Get() != nullptr;
        ComposedGeneration = generation;
        PublishedOutputGeneration = generation;
        ComposedOutputValid = true;
    }
    LastComposeResult = GPU2DComposeResult::Success;
    return true;
}

bool DX12Gpu2DComposer::ComposeNativeGPU2D(
    const DX12Gpu2DComposeContext& ctx,
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
    if (!ctx.ShadersReady)
        return false;
    const std::shared_ptr<DX12Gpu2DOutput> state = Output;
    if (!ctx.Context || !Native || !Compositor
        || !state || !ctx.FinalFB)
    {
        ctx.Fail(ctx.User, "required native GPU2D resources are unavailable");
        return false;
    }

    const u32 workIndex = static_cast<u32>(
        input.Generation.Frame % kCompositorFramesInFlight);
    DX12Gpu2DOutput::ComposeWorkSlot& workSlot = state->WorkSlots[workIndex];
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const bool workSlotFencePending = !workSlot.Commands.IsIdle();
    const auto workSlotWaitStart = std::chrono::steady_clock::now();
#endif
    ID3D12GraphicsCommandList* list = workSlot.Commands.Begin();
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const auto workSlotWaitEnd = std::chrono::steady_clock::now();
    if (workSlotFencePending)
    {
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DWorkSlotFenceWaitCount);
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DWorkSlotFenceWaitNs,
            static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                workSlotWaitEnd - workSlotWaitStart).count()));
    }
#endif
    if (!list)
    {
        ctx.Fail(ctx.User, "native GPU2D semantic command admission failed");
        return false;
    }

    // "Is this slot free" splits cleanly in two: the ring answers published
    // and leased, and this answers the only half that needs D3D12 -- whether
    // the slot's own command list, and the work slot it was last composed
    // against, have both retired.
    struct SlotReadiness
    {
        DX12Gpu2DOutput* State;
        u32 WorkIndex;
    } readiness{state.get(), workIndex};
    const auto slotReady = +[](void* userData, u32 candidate) -> bool {
        auto* ctx = static_cast<SlotReadiness*>(userData);
        DX12Gpu2DOutput::Slot& candidateSlot = ctx->State->Slots[candidate];
        if (!candidateSlot.Commands.IsIdle())
            return false;
        if (candidateSlot.PresentationWorkSlot >= 0
            && candidateSlot.PresentationWorkSlot != static_cast<int>(ctx->WorkIndex)
            && !ctx->State->WorkSlots[static_cast<u32>(
                candidateSlot.PresentationWorkSlot)].Commands.IsIdle())
        {
            return false;
        }
        return true;
    };

    u32 slotIndex = kCompositorFramesInFlight;
    {
        const auto lock = state->Ring.LockPublication();
        const u32 candidate = state->Ring.FindFreeSlot(
            state->Ring.GetCursor(), slotReady, &readiness);
        if (candidate != RendererOutputRing::InvalidSlot)
        {
            slotIndex = candidate;
            state->Ring.SetCursorAfter(candidate);
        }
    }
    DX12Gpu2DOutput::Slot* outputSlot = slotIndex < kCompositorFramesInFlight
        ? &state->Slots[slotIndex] : nullptr;
    bool presentationAvailable = outputSlot != nullptr;
    const bool forcedPresentationStall = presentationAvailable
        && GPU2DNative::ConsumeForcedPresentationStallFrame();
    if (forcedPresentationStall)
    {
        outputSlot = nullptr;
        slotIndex = kCompositorFramesInFlight;
        presentationAvailable = false;
    }
    // Presentation backpressure is allowed to drop a visible frame, but must
    // never drop DS display-capture semantics. The persistent LCDC capture
    // mirror is emulated hardware state, not a presentation cache.
    auto& nativeStaging = workSlot.NativeStaging;
    auto& nativeInput = workSlot.NativeInput;
    auto& structuredInput = workSlot.StructuredInput;
    u32*& nativeMapped = workSlot.NativeMapped;
    auto& uploadedNativeGeneration = workSlot.UploadedNativeGeneration;
    bool& nativeUploadInitialized = workSlot.NativeUploadInitialized;
    const u64 composedOutputBytes = static_cast<u64>(ctx.ScreenWidth)
        * static_cast<u64>(ctx.ScreenHeight) * 2ull * sizeof(u32);
    const u64 diagnosticRowPitch = AlignUp(
        static_cast<u64>(ctx.ScreenWidth) * sizeof(u32),
        D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    const u64 diagnosticReadbackBytes = std::max(
        composedOutputBytes,
        diagnosticRowPitch * static_cast<u64>(ctx.ScreenHeight) * 2ull);
    const bool hadDiagnosticReadback = workSlot.NativeReadback.Get() != nullptr;
    const bool hadFallbackComposed = workSlot.DiagnosticComposed.Get() != nullptr;
    if (diagnosticReadback
        && !workSlot.EnsureDiagnosticResources(
            *ctx.Context, diagnosticReadbackBytes,
            static_cast<u64>(kCompositionInputDwords) * sizeof(u32),
            outputSlot == nullptr, stageDiagnostics))
    {
        workSlot.Commands.Submit();
        ctx.Fail(ctx.User, "could not create lazy native GPU2D diagnostic resources");
        return false;
    }
    if (diagnosticReadback && !hadDiagnosticReadback
        && workSlot.NativeReadback.Get() != nullptr)
    {
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DDiagnosticReadbackCreateCount);
    }
    if (!hadFallbackComposed && workSlot.DiagnosticComposed.Get() != nullptr)
    {
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DFallbackComposedCreateCount);
    }
    if (diagnosticReadback && outputSlot == nullptr
        && !ctx.BuildWorkDiagnosticUav(ctx.User, workIndex))
    {
        workSlot.Commands.Submit();
        ctx.Fail(ctx.User, "could not build native GPU2D diagnostic descriptors");
        return false;
    }
    auto& nativeReadback = workSlot.NativeReadback;
    auto& structuredReadback = workSlot.StructuredReadback;
    const u32 descriptorIndex = workIndex;
    u64 rendererSerial = 0;
    if (outputSlot)
    {
        const auto lock = state->Ring.LockPublication();
        rendererSerial = state->Ring.PeekNextSerial();
    }
    RecordDX12GpuMetric(
        workSlot.Commands, GpuMetric::NativeGPU2DLogical,
        DX12Perf::Counter::NativeGPU2DLogicalGpuTimeNs);
    RecordDX12GpuMetric(
        workSlot.Commands, GpuMetric::NativeGPU2DCapture,
        DX12Perf::Counter::NativeGPU2DCaptureGpuTimeNs);
    RecordDX12GpuMetric(
        workSlot.Commands, GpuMetric::NativeGPU2DResolve,
        DX12Perf::Counter::NativeGPU2DResolveGpuTimeNs);
    RecordDX12GpuMetric(
        workSlot.Commands, GpuMetric::NativeGPU2DRaw,
        DX12Perf::Counter::NativeGPU2DObjRawGpuNs);
    RecordDX12GpuMetric(
        workSlot.Commands, GpuMetric::NativeGPU2DResolve,
        DX12Perf::Counter::CompositorGpuTimeNs);

    u64 pendingCompletionValue = ctx.Provenance->PeekNextSubmissionSerial();
    if (pendingCompletionValue == 0u)
        pendingCompletionValue = 1u;
    const NativeCaptureStateIdentity pendingCaptureIdentity{
        true,
        CaptureOwner::NativeDX12,
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
    DX12Perf::SetCounter(
        DX12Perf::Counter::NativeGPU2DWorkgroupWidth, 256u);
    DX12Perf::AddCounter(
        DX12Perf::Counter::NativeGPU2DSemanticRowsDirty,
        semanticLinePlan.DirtyRows);
    DX12Perf::AddCounter(
        DX12Perf::Counter::NativeGPU2DSemanticRowsReused,
        semanticLinePlan.ReusedRows);
    DX12Perf::AddCounter(
        DX12Perf::Counter::NativeGPU2DSemanticRunCount,
        semanticLinePlan.RunCount);
    DX12Perf::AddCounter(
        DX12Perf::Counter::NativeGPU2DObjPrepareGroups,
        semanticLinePlan.DirtyRows);
    const GPU2DNative::UploadPlan uploadPlan = GPU2DNative::BuildUploadPlan(
        input, uploadedNativeGeneration, fullNativeUpload);
    DX12Perf::AddCounter(
        fullNativeUpload
            ? DX12Perf::Counter::NativeGPU2DFullUploadFrames
            : DX12Perf::Counter::NativeGPU2DPartialUploadFrames);
    DX12Perf::AddCounter(
        fullNativeUpload
            ? DX12Perf::Counter::NativeGPU2DFullUploadBytes
            : DX12Perf::Counter::NativeGPU2DPartialUploadBytes,
        uploadPlan.TotalBytes);
    switch (uploadDecision.Reason)
    {
    case GPU2DNative::FullUploadReason::FirstUse:
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DFullUploadFirstUseCount);
        break;
    case GPU2DNative::FullUploadReason::EpochChange:
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DFullUploadEpochChangeCount);
        break;
    case GPU2DNative::FullUploadReason::SemanticFrameGap:
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DFullUploadSemanticFrameGapCount);
        break;
    case GPU2DNative::FullUploadReason::CaptureGenerationRegression:
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DFullUploadCaptureRegressionCount);
        break;
    case GPU2DNative::FullUploadReason::None:
        break;
    }
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const u64 packStartNs = static_cast<u64>(std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
    u32* staging = nativeMapped;
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
    if (!packedNativeInput)
    {
        ctx.HighResCapture->AbortFrame();
        workSlot.Commands.Submit();
        ctx.Fail(ctx.User, "the native GPU2D input staging upload failed");
        return false;
    }
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const u64 packEndNs = static_cast<u64>(std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DPackNs,
        packEndNs - packStartNs);
#endif
    DX12Perf::AddCounter(DX12Perf::Counter::RecorderBlocksScanned,
        input.Recorder.BlocksScanned);
    DX12Perf::AddCounter(DX12Perf::Counter::RecorderBytesScanned,
        input.Recorder.BytesScanned);
    DX12Perf::AddCounter(DX12Perf::Counter::RecorderBlocksCopied,
        input.Recorder.BlocksCopied);
    DX12Perf::AddCounter(DX12Perf::Counter::RecorderBytesCopied,
        input.Recorder.BytesCopied);
    DX12Perf::AddCounter(DX12Perf::Counter::CaptureCPU2DLines,
        input.Recorder.CaptureCPU2DLines);
    DX12Perf::AddCounter(DX12Perf::Counter::CaptureCPU2DNs,
        input.Recorder.CaptureCPU2DNs);
    DX12Perf::AddCounter(DX12Perf::Counter::GPU2DRecorderNs,
        input.Recorder.GPU2DRecorderNs);
    DX12Perf::AddCounter(DX12Perf::Counter::TimelineRowDedupNs,
        input.Recorder.TimelineRowDedupNs);
    DX12Perf::AddCounter(DX12Perf::Counter::SpriteTimelineRowDedupNs,
        input.Recorder.SpriteTimelineRowDedupNs);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DInputPackBytes,
        uploadPlan.TotalBytes);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DVRAMUploadBytes,
        uploadPlan.EngineMemoryBytes + uploadPlan.FIFOBytes
            + uploadPlan.LCDVRAMBytes);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DPaletteUploadBytes,
        uploadPlan.PaletteBytes);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DOAMUploadBytes,
        uploadPlan.OAMBytes);
    DX12Perf::AddCounter(DX12Perf::Counter::MappedReadWordCalls,
        input.Recorder.MappedReadWordCalls);
    DX12Perf::AddCounter(DX12Perf::Counter::MappedReadFastPathCalls,
        input.Recorder.MappedReadFastPathCalls);
    DX12Perf::AddCounter(DX12Perf::Counter::MappedReadSlowPathCalls,
        input.Recorder.MappedReadSlowPathCalls);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeCaptureHistoryScanLines,
        input.Recorder.NativeCaptureHistoryScanLines);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeMappingBuildCalls,
        input.Recorder.NativeMappingBuildCalls);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeMappingRowsUploaded,
        input.Recorder.NativeMappingRowsUploaded);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeMappingBytesUploaded,
        input.Recorder.NativeMappingBytesUploaded);
    DX12Perf::AddCounter(DX12Perf::Counter::BGOverlayFastPath,
        input.Recorder.BGOverlayFastPath);
    DX12Perf::AddCounter(DX12Perf::Counter::BGOverlaySlowPath,
        input.Recorder.BGOverlaySlowPath);
    DX12Perf::AddCounter(DX12Perf::Counter::OBJOverlayFastPath,
        input.Recorder.OBJOverlayFastPath);
    DX12Perf::AddCounter(DX12Perf::Counter::OBJOverlaySlowPath,
        input.Recorder.OBJOverlaySlowPath);

    workSlot.Descriptors.Reset();
    ctx.InvalidateSrvCache(ctx.User);
    if (nativeUploadInitialized)
    {
        DX12TransitionBuffer(
            list,
            nativeInput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_DEST);
    }
    for (u32 i = 0; i < uploadPlan.Count; ++i)
    {
        const GPU2DNative::DirtyRange& range = uploadPlan.Ranges[i];
        list->CopyBufferRegion(
            nativeInput.Get(), range.Offset,
            nativeStaging.Get(), range.Offset, range.Size);
    }
    DX12TransitionBuffer(
        list,
        nativeInput.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // The tail of ctx.BlendState is the persistent GPU LCDC capture mirror.
    // Synchronize only changed serialized LCD ranges from the mapped input;
    // native capture commands below update it in scanline order.
    DX12InsertUavBarrier(list, ctx.BlendState);
    DX12TransitionBuffer(
        list,
        ctx.BlendState,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    const u32 lcdBegin = GPU2DNative::PackedLCDVRAMBase * sizeof(u32);
    const u32 lcdEnd = GPU2DNative::PackedRouteBase * sizeof(u32);
    const u64 captureBase = (static_cast<u64>(ctx.ScreenWidth)
        * static_cast<u64>(ctx.ScreenHeight)) * sizeof(u32);
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

            list->CopyBufferRegion(
                ctx.BlendState, captureBase + (begin - lcdBegin),
                nativeStaging.Get(), begin, end - begin);
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
    DX12TransitionBuffer(
        list,
        ctx.BlendState,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (outputSlot && outputSlot->DirectTexture
        && outputSlot->DirectTextureInShaderResource)
    {
        DX12TransitionBuffer(
            list,
            outputSlot->DirectTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        outputSlot->DirectTextureInShaderResource = false;
    }

    DX12InsertUavBarrier(list, ctx.FinalFB);
    DX12InsertUavBarrier(list, ctx.Capture->GetSidecarBuffer());

    // The logical Stage A owns the structured output resource for this slot.
    // It is kept separate from NativeInput so the shader can read the packed
    // frame while filling the first fourteen compositor planes.
    DX12TransitionBuffer(
        list,
        structuredInput.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap* heaps[] = { workSlot.Descriptors.GetHeap() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(ctx.RootSignature);
    if (!BindCompositionUavTable(
            ctx,
            list, workSlot.Descriptors, WorkNativeUavCpu[workIndex]))
    {
        DX12TransitionBuffer(
            list,
            structuredInput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_DEST);
        workSlot.Commands.Submit();
        ctx.Fail(ctx.User, "could not bind the native logical GPU2D descriptor table");
        return false;
    }

    DX12DispatchUniform constants = ctx.Dispatch;
    constants.TexWidth = finalFBValid ? 1u : 0u;
    constants.Pad = 16u;
    workSlot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::NativeGPU2DLogical, false));
    if (input.CaptureEnable != 0u)
    {
        workSlot.Commands.WriteTimestamp(
            GpuMetricQueryIndex(GpuMetric::NativeGPU2DCapture, false));
        if (!NativeCapture)
        {
            ctx.Fail(ctx.User, "native GPU2D capture pipeline is unavailable");
            return false;
        }
        const bool batchIndependentCapture =
            GPU2DNative::CanBatchIndependentCaptureFrame(input, finalFBValid);
        if (batchIndependentCapture)
        {
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DCaptureRunCount);
            // The destination remains LCDC-only for the full frame, so it
            // cannot feed its own writes back through BG/OBJ. Submit all
            // logical lines before one frame-wide capture dispatch.
            const bool fuseObjRawLogical =
                GPU2DNative::CanFuseObjRawLogicalFrame(input);
            list->SetPipelineState(Native.Get());
            constants.InterpSpanCount = 0u;
            constants.Pad = 32u
                | (fuseObjRawLogical ? (16u | 64u) : 0u);
            DX12SetDispatchConstants(list, constants);
            list->Dispatch(1u, 384u, 1u);
            if (!fuseObjRawLogical)
            {
                DX12InsertUavBarrier(list, ctx.BlendState);
                constants.Pad = 16u;
                DX12SetDispatchConstants(list, constants);
                list->Dispatch(1u, 384u, 1u);
            }

            DX12InsertUavBarrier(list, structuredInput.Get());

            constants.Pad = 4u | 128u;
            DX12SetDispatchConstants(list, constants);
            list->SetPipelineState(NativeCapture.Get());
            list->Dispatch(
                DivRoundUp(static_cast<u32>(ctx.ScreenWidth), 256u),
                GPU2DNative::ScreenHeight * static_cast<u32>(ctx.ScaleFactor), 1u);
            ID3D12Resource* captureOutputs[2] = {
                ctx.BlendState, ctx.Capture->GetSidecarBuffer()};
            DX12InsertUavBarriers(list, captureOutputs, 2u);
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DCaptureDispatchCount);
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DCaptureBarrierCount, 1u);
            DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DDispatchCount,
                fuseObjRawLogical ? 2u : 3u);
        }
        else
        {
            const GPU2DNative::CaptureRunPlan capturePlan =
                GPU2DNative::BuildCaptureRunPlan(input, finalFBValid);
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DCaptureRunCount,
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
                    list->SetPipelineState(Native.Get());
                    for (u32 screen = 0u; screen < 2u; ++screen)
                    {
                        constants.InterpSpanCount =
                            screen * GPU2DNative::ScreenHeight + run.LineBase;
                        constants.Pad = 32u | 256u
                            | (fuseObjRawLogical ? (16u | 64u) : 0u);
                        DX12SetDispatchConstants(list, constants);
                        list->Dispatch(1u, run.LineCount, 1u);
                        ++dispatchCount;
                    }
                    if (!fuseObjRawLogical)
                    {
                        DX12InsertUavBarrier(list, ctx.BlendState);
                        ++captureBarrierCount;
                        for (u32 screen = 0u; screen < 2u; ++screen)
                        {
                            constants.InterpSpanCount =
                                screen * GPU2DNative::ScreenHeight + run.LineBase;
                            constants.Pad = 16u | 256u;
                            DX12SetDispatchConstants(list, constants);
                            list->Dispatch(1u, run.LineCount, 1u);
                            ++dispatchCount;
                        }
                    }

                    ID3D12Resource* logicalOutputs[2] = {
                        structuredInput.Get(), ctx.BlendState};
                    DX12InsertUavBarriers(list, logicalOutputs, 2u);
                    ++captureBarrierCount;
                    constants.InterpSpanCount = run.LineBase;
                    constants.Pad = 4u | 128u | 512u;
                    DX12SetDispatchConstants(list, constants);
                    list->SetPipelineState(NativeCapture.Get());
                    list->Dispatch(
                        DivRoundUp(static_cast<u32>(ctx.ScreenWidth), 256u),
                        run.LineCount * static_cast<u32>(ctx.ScaleFactor), 1u);
                    ++dispatchCount;
                    ++captureDispatchCount;
                    ID3D12Resource* captureOutputs[2] = {
                        ctx.BlendState, ctx.Capture->GetSidecarBuffer()};
                    DX12InsertUavBarriers(list, captureOutputs, 2u);
                    ++captureBarrierCount;
                    continue;
                }

                const u32 lineNumber = run.LineBase;
                const bool captureLineActive =
                    GPU2DNative::IsEffectiveCaptureLine(input, lineNumber);
                constants.InterpSpanCount = lineNumber;
                constants.Pad = 32u | 8u
                    | (fuseObjRawLogical ? (16u | 64u) : 0u);
                DX12SetDispatchConstants(list, constants);
                list->SetPipelineState(Native.Get());
                list->Dispatch(1u, 2u, 1u);
                ++dispatchCount;
                if (!fuseObjRawLogical)
                {
                    DX12InsertUavBarrier(list, ctx.BlendState);
                    ++captureBarrierCount;
                    constants.Pad = 16u | 8u;
                    DX12SetDispatchConstants(list, constants);
                    list->Dispatch(1u, 2u, 1u);
                    ++dispatchCount;
                }
                if (captureLineActive)
                {
                    ID3D12Resource* logicalOutputs[2] = {
                        structuredInput.Get(), ctx.BlendState};
                    DX12InsertUavBarriers(list, logicalOutputs, 2u);
                    ++captureBarrierCount;
                    constants.Pad = 4u;
                    DX12SetDispatchConstants(list, constants);
                    list->SetPipelineState(NativeCapture.Get());
                    list->Dispatch(
                        DivRoundUp(static_cast<u32>(ctx.ScreenWidth), 256u),
                        static_cast<u32>(ctx.ScaleFactor), 1u);
                    ++dispatchCount;
                    ++captureDispatchCount;
                    ID3D12Resource* captureOutputs[2] = {
                        ctx.BlendState, ctx.Capture->GetSidecarBuffer()};
                    DX12InsertUavBarriers(list, captureOutputs, 2u);
                    ++captureBarrierCount;
                }
            }
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DDispatchCount, dispatchCount);
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DCaptureDispatchCount,
                captureDispatchCount);
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DCaptureBarrierCount,
                captureBarrierCount);
        }
        workSlot.Commands.WriteTimestamp(
            GpuMetricQueryIndex(GpuMetric::NativeGPU2DCapture, true));
    }
    else
    {
        list->SetPipelineState(Native.Get());
        workSlot.Commands.WriteTimestamp(
            GpuMetricQueryIndex(GpuMetric::NativeGPU2DRaw, false));
        u64 dispatchCount = 0u;
        for (u32 runIndex = 0u;
            runIndex < semanticLinePlan.RunCount; ++runIndex)
        {
            const GPU2DNative::SemanticLineRun& run =
                semanticLinePlan.Runs[runIndex];
            const bool fuseObjRawLogical =
                GPU2DNative::CanFuseObjRawLogicalRun(input, run);
            constants.InterpSpanCount = run.RowBase;
            constants.Pad = 32u | 256u
                | (fuseObjRawLogical ? (16u | 64u) : 0u);
            DX12SetDispatchConstants(list, constants);
            list->Dispatch(1u, run.RowCount, 1u);
            ++dispatchCount;
            if (!fuseObjRawLogical)
            {
                DX12InsertUavBarrier(list, ctx.BlendState);
                constants.Pad = 16u | 256u;
                DX12SetDispatchConstants(list, constants);
                list->Dispatch(1u, run.RowCount, 1u);
                ++dispatchCount;
            }
        }
        workSlot.Commands.WriteTimestamp(
            GpuMetricQueryIndex(GpuMetric::NativeGPU2DRaw, true));
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DDispatchCount, dispatchCount);
    }
    workSlot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::NativeGPU2DLogical, true));

    DX12InsertUavBarrier(list, structuredInput.Get());
    DX12InsertUavBarrier(list, ctx.Capture->GetSidecarBuffer());
    if (stageDiagnostics)
    {
        // Developer-only Stage A readback.  Keep the structured planes in a
        // UAV state for the compositor after the copy; shipping never creates
        // or touches this readback resource.
        DX12TransitionBuffer(
            list,
            structuredInput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(
            structuredReadback.Get(), 0,
            structuredInput.Get(), 0,
            static_cast<u64>(kCompositionInputDwords) * sizeof(u32));
        DX12TransitionBuffer(
            list,
            structuredInput.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    bool compositorDirectOutput = false;
    bool directOutputReadback = false;
    // A semantic-only frame has no visible consumer. Stage A and every
    // capture/provenance transition above still execute, while the discarded
    // Stage B compositor is skipped unless a developer diagnostic requested
    // an observable result.
    if (outputSlot || diagnosticReadback)
    {
    auto& composedOutput = outputSlot
        ? outputSlot->Composed : workSlot.DiagnosticComposed;
    compositorDirectOutput = outputSlot && outputSlot->DirectTexture
        && (!diagnosticReadback
            || (GPU2DNative::DirectOutputDiagnosticsEnabled() && ctx.ScaleFactor == 1));
    if (!BindCompositionUavTable(
            ctx,
            list, workSlot.Descriptors,
            WorkOutputUavCpu[workIndex * 4u
                + (outputSlot ? slotIndex : 3u)]))
    {
        DX12TransitionBuffer(
            list,
            structuredInput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_DEST);
        workSlot.Commands.Submit();
        ctx.Fail(ctx.User, "could not bind the structured compositor descriptor table");
        return false;
    }
    constants.InterpSpanCount = 0u;
    constants.Pad = compositorDirectOutput ? 1u : 0u;
    DX12SetDispatchConstants(list, constants);
    list->SetPipelineState(Compositor.Get());
    workSlot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::NativeGPU2DResolve, false));
    list->Dispatch(
        DivRoundUp(static_cast<u32>(ctx.ScreenWidth), 8u),
        DivRoundUp(static_cast<u32>(ctx.ScreenHeight) * 2u, 8u), 1u);
    workSlot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::NativeGPU2DResolve, true));

    directOutputReadback = compositorDirectOutput
        && diagnosticReadback
        && GPU2DNative::DirectOutputDiagnosticsEnabled();
    if (compositorDirectOutput)
    {
        DX12InsertUavBarrier(list, outputSlot->DirectTexture.Get());
        if (directOutputReadback)
        {
            DX12TransitionBuffer(
                list,
                outputSlot->DirectTexture.Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
            const UINT rowPitch = static_cast<UINT>(
                AlignUp(static_cast<u64>(ctx.ScreenWidth) * sizeof(u32),
                    D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
            const u64 screenBytes = static_cast<u64>(rowPitch)
                * static_cast<u64>(ctx.ScreenHeight);
            for (UINT screen = 0; screen < 2u; ++screen)
            {
                D3D12_TEXTURE_COPY_LOCATION destination{};
                destination.pResource = nativeReadback.Get();
                destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                destination.PlacedFootprint.Offset = screenBytes * screen;
                destination.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                destination.PlacedFootprint.Footprint.Width = static_cast<UINT>(ctx.ScreenWidth);
                destination.PlacedFootprint.Footprint.Height = static_cast<UINT>(ctx.ScreenHeight);
                destination.PlacedFootprint.Footprint.Depth = 1;
                destination.PlacedFootprint.Footprint.RowPitch = rowPitch;

                D3D12_TEXTURE_COPY_LOCATION source{};
                source.pResource = outputSlot->DirectTexture.Get();
                source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                source.SubresourceIndex = screen;
                list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            }
            DX12TransitionBuffer(
                list,
                outputSlot->DirectTexture.Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        else
        {
            DX12TransitionBuffer(
                list,
                outputSlot->DirectTexture.Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        outputSlot->DirectTextureInShaderResource = true;
        DX12Perf::AddCounter(DX12Perf::Counter::DirectCompositorImageFrames);
    }
    else
    {
        DX12InsertUavBarrier(list, composedOutput.Get());
        DX12Perf::AddCounter(DX12Perf::Counter::FallbackCompositorBufferFrames);
    }

    if (diagnosticReadback && !directOutputReadback)
    {
        DX12InsertUavBarrier(list, composedOutput.Get());
        DX12TransitionBuffer(
            list,
            composedOutput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(
            nativeReadback.Get(), 0,
            composedOutput.Get(), 0,
            composedOutputBytes);
        DX12TransitionBuffer(
            list,
            composedOutput.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    }
    DX12TransitionBuffer(
        list,
        structuredInput.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);

    if (!workSlot.Commands.Submit())
    {
        ctx.HighResCapture->AbortFrame();
        ctx.Fail(ctx.User, "native GPU2D command submission failed");
        return false;
    }
    // Each semantic slot owns a separate DX12 command context and local fence.
    // Its fence values are therefore not comparable across slots.  The
    // renderer-global serial is the provenance identity; direct queue ordering
    // guarantees that a later scoped capture copy observes this submission.
    ctx.Provenance->CommitSubmissionSerial(pendingCompletionValue);
    ctx.Provenance->SetCompletionValue(pendingCompletionValue);
    ctx.HighResCapture->CommitFrame(pendingCaptureIdentity);

    if (diagnosticReadback)
    {
        workSlot.Commands.WaitIdle();
        if (!workSlot.NativeReadbackMapped)
        {
            ctx.Fail(ctx.User, "native GPU2D exact readback mapping failed");
            return false;
        }
        const u8* source = workSlot.NativeReadbackMapped;
        const u32 representative = GPU2DNative::RepresentativeSubpixel(
            static_cast<u32>(ctx.ScaleFactor));
        const u64 sourceRowPitch = directOutputReadback
            ? diagnosticRowPitch
            : static_cast<u64>(ctx.ScreenWidth) * sizeof(u32);
        const u64 sourceScreenBytes = sourceRowPitch
            * static_cast<u64>(ctx.ScreenHeight);
        std::unique_ptr<u32[]> actual(new u32[2u * GPU2DNative::ScreenPixelCount]);
        for (u32 screen = 0; screen < 2u; ++screen)
        {
            for (u32 y = 0; y < GPU2DNative::ScreenHeight; ++y)
            {
                for (u32 x = 0; x < GPU2DNative::ScreenWidth; ++x)
                {
                    const u64 sourceOffset = static_cast<u64>(screen)
                            * sourceScreenBytes
                        + static_cast<u64>(y * static_cast<u32>(ctx.ScaleFactor)
                                + representative)
                            * sourceRowPitch
                        + static_cast<u64>(x * static_cast<u32>(ctx.ScaleFactor)
                                + representative)
                            * sizeof(u32);
                    u32 bgra8 = 0;
                    std::memcpy(&bgra8, source + sourceOffset, sizeof(u32));
                    const u32 red = directOutputReadback
                        ? (bgra8 & 0xFFu) : (bgra8 >> 16u);
                    const u32 green = (bgra8 >> 8u) & 0xFFu;
                    const u32 blue = directOutputReadback
                        ? (bgra8 >> 16u) : (bgra8 & 0xFFu);
                    const u32 index = screen * GPU2DNative::ScreenPixelCount
                        + y * GPU2DNative::ScreenWidth + x;
                    actual[index] = ((red & 0xFFu) >> 2u)
                        | (((green >> 2u) & 0x3Fu) << 8u)
                        | (((blue >> 2u) & 0x3Fu) << 16u);
                }
            }
        }
        DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DReadbackCount);
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DReadbackBytes,
            directOutputReadback ? diagnosticReadbackBytes : composedOutputBytes);

        if (stageDiagnostics)
        {
            if (!workSlot.StructuredReadbackMapped)
            {
                ctx.Fail(ctx.User, "native GPU2D Stage A readback mapping failed");
                return false;
            }
            GPU2DNative::LogStageSnapshot(
                "DX12", input.Generation.Frame, input.Generation.Frame,
                rendererSerial, generation, descriptorIndex, input,
                workSlot.StructuredReadbackMapped,
                actual.get(), actual.get() + GPU2DNative::ScreenPixelCount,
                directOutputReadback ? "direct_image" : "composed_buffer",
                expectedTop, expectedBottom);
        }

        if (exactValidation)
        {
            const GPU2DNative::CompareResult result = GPU2DNative::CompareExact(
                expectedTop, expectedBottom,
                actual.get(), actual.get() + GPU2DNative::ScreenPixelCount);
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DMismatchCount,
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
                        "DX12 native GPU2D exact mismatch frame=%llu total=%u top=%u bottom=%u "
                        "first=screen%u(%u,%u) expected=0x%08X actual=0x%08X engine=%u "
                        "DispCnt=0x%08X Layer=0x%08X BGCnt0=0x%08X WinRegs=0x%08X "
                        "BlendCnt=0x%08X Master=0x%08X Screens=%u/%u LineScreens=%u "
                        "ExpectedRow8=%08X/%08X Capture=0x%08X\n",
                        static_cast<unsigned long long>(generation),
                        result.TotalMismatchCount, result.TopMismatchCount,
                        result.BottomMismatchCount, sample.Screen, sample.X, sample.Y,
                        sample.Expected, sample.Actual, engine, state.DispCnt,
                        state.LayerEnable, state.BGCnt[0], state.WinRegs,
                        state.BlendCnt, state.MasterBrightness, input.ScreensEnabled,
                        input.ScreenSwap, state.ScreensEnabled,
                        expectedTop[8u * GPU2DNative::ScreenWidth],
                        expectedBottom[8u * GPU2DNative::ScreenWidth], state.CaptureCnt);

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
    DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DFrames);
    GPU2DNative::LogSemanticIdentity(
        "DX12", input.Generation.Frame, input.Generation.CaptureGeneration,
        ctx.Provenance->GetEpoch(), outputSlot != nullptr, forcedPresentationStall,
        mirrorNeedsFullCopy,
        outputSlot != nullptr ? slotIndex : kCompositorFramesInFlight);
    if (!outputSlot)
    {
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DPresentationBackpressureFrames);
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DSemanticOnlyFrames);
        LastComposeResult = GPU2DComposeResult::SemanticOnly;
        return false;
    }
    {
        const auto lock = state->Ring.LockPublication();
        outputSlot->Frame.Serial = rendererSerial;
        outputSlot->Frame.Generation = generation;
        outputSlot->Frame.Epoch = ctx.Provenance->GetEpoch();
        outputSlot->Frame.DirectContentValid = compositorDirectOutput;
        outputSlot->PresentationWorkSlot = static_cast<int>(workIndex);
        state->Ring.PublishReserved(slotIndex);
        ComposedGeneration = generation;
        PublishedOutputGeneration = generation;
        ComposedOutputValid = true;
    }
    if (stageDiagnostics)
    {
        GPU2DNative::LogPresentedIdentity(
            "DX12", input.Generation.Frame, outputSlot->Frame.Serial,
            generation, outputSlot->Frame.Epoch, slotIndex);
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

bool DX12Gpu2DComposer::RecreateOutput(
    DX12Context& context, u32 width, u32 height, u32 uavTableSize, u64 epoch)
{
    // Build the candidate first and adopt it only once Create() has fully
    // succeeded. The renderer used to publish the shared_ptr and then call
    // Create() through it, which left a half-initialized set reachable as the
    // active Output on the failure path -- the presenter and the compose path
    // both read it without asking whether it finished being built.
    auto candidate = std::make_shared<DX12Gpu2DOutput>();
    if (!candidate->Create(
            context, width, height, uavTableSize,
            NextOutputResourceGeneration, epoch))
        return false;

    NextOutputResourceGeneration++;
    Output = std::move(candidate);

    // A new resource set has published nothing yet.
    ComposedOutputValid = false;
    ComposedGeneration = 0;
    PublishedOutputGeneration = 0;
    return true;
}

void DX12Gpu2DComposer::ReleaseOutput() noexcept
{
    // Detach, do not destroy. A RendererOutputLease captured its own
    // shared_ptr to this set, so a presenter still reading the old resources
    // across a resolution change keeps them alive until it releases.
    Output.reset();

    // The descriptor contents described the set that just went away. Rewinding
    // the rings forgets that; it does not end the heaps, which are sized from
    // the root-signature layout and outlive any one resolution.
    OutputUav.Reset();
    WorkOutputUav.Reset();
    WorkNativeUav.Reset();
    OutputUavCpu.fill({});
    WorkOutputUavCpu.fill({});
    WorkNativeUavCpu.fill({});

    // The publication state described the set that just went away.
    ComposedOutputValid = false;
    ComposedGeneration = 0;
    PublishedOutputGeneration = 0;

    // NextOutputResourceGeneration deliberately survives: it is a lifetime
    // identity, and reusing a number would let a presenter mistake a new set
    // for one it had already cached descriptors against.
}

void DX12Gpu2DComposer::ResetForRendererEpoch(
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
                // Keep the last complete presentation surface alive. The
                // unpublished ring slots are rebuilt for the next full frame.
                continue;
            }
            DX12Gpu2DOutput::Slot& slot = Output->Slots[slotIndex];
            slot.UploadedContentGeneration = {};
            slot.StructuredUploadInitialized = false;
            slot.Frame.DirectContentValid = false;
            slot.Frame.Epoch = epoch;
            slot.PresentationWorkSlot = -1;
        }
        for (DX12Gpu2DOutput::ComposeWorkSlot& slot : Output->WorkSlots)
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

void DX12Gpu2DComposer::MarkFatal() noexcept
{
    LastComposeResult = GPU2DComposeResult::Fatal;
}

RendererOutput DX12Gpu2DComposer::GetComposedOutput() const
{
    const std::shared_ptr<DX12Gpu2DOutput> state = Output;
    if (!state || !ComposedOutputValid)
        return {};

    const auto lock = state->Ring.LockPublication();
    const int slotIndex = state->Ring.GetPublishedSlot();
    if (slotIndex < 0)
        return {};
    const DX12PresentedFrame& frame = state->Slots[slotIndex].Frame;
    return RendererOutput::DX12Buffer(
        const_cast<DX12PresentedFrame*>(&frame), frame.Width, frame.Height,
        frame.Serial, frame.Epoch);
}

RendererOutputLease DX12Gpu2DComposer::AcquireComposedOutputLease()
{
    const std::shared_ptr<DX12Gpu2DOutput> state = Output;
    if (!state || !ComposedOutputValid)
        return {};

    const auto lock = state->Ring.LockPublication();
    const int slotIndex = state->Ring.GetPublishedSlot();
    if (slotIndex < 0)
        return {};

    DX12Gpu2DOutput::Slot& slot = state->Slots[slotIndex];
    auto* leaseCounter = state->Ring.AcquireLease(static_cast<u32>(slotIndex));
    GPU2DNative::LogPresentedIdentity(
        "DX12", slot.Frame.Generation, slot.Frame.Serial,
        slot.Frame.Generation, slot.Frame.Epoch, static_cast<u32>(slotIndex));

    // The lease captures `state`, not a raw pointer. That is what keeps a
    // resource set alive across a resolution change while the presenter is
    // still reading it.
    return RendererOutputLease(
        RendererOutput::DX12Buffer(
            &slot.Frame, slot.Frame.Width, slot.Frame.Height,
            slot.Frame.Serial, slot.Frame.Epoch),
        leaseCounter,
        &RendererOutputRing::LeaseCounter::Release,
        state);
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
