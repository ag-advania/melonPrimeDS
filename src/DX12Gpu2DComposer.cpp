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

#include "DX12Context.h"
#include "Platform.h"

namespace melonDS
{

using namespace DX12Gpu2D;

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

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
