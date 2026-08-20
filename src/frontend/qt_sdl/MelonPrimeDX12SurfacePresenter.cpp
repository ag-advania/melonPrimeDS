/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "MelonPrimeDX12SurfacePresenter.h"
#include "DX12GpuTimestamp.h"
#include "DX12Perf.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "Platform.h"

namespace MelonPrime
{
namespace
{
constexpr UINT kBufferCount = 2;
constexpr std::uint32_t kDrawConstantDwords = 20;

std::uint32_t AlignTexturePitch(std::uint32_t bytes) noexcept
{
    return (bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
}

struct DrawConstants
{
    float Axis[4];
    float Origin[4];
    float UvRect[4];
    float Tint[4];
    std::uint32_t Params[4]{};
};
static_assert(sizeof(DrawConstants) == kDrawConstantDwords * sizeof(std::uint32_t));

const char* kPresentShader = R"hlsl(
cbuffer DrawConstants : register(b0)
{
    float4 Axis;
    float4 Origin;
    float4 UvRect;
    float4 Tint;
    uint4 Params;
};

Texture2DArray<float4> Source : register(t0);
SamplerState PointSampler : register(s0);
SamplerState LinearSampler : register(s1);

struct VertexOutput
{
    float4 Position : SV_Position;
    float2 Uv : TEXCOORD0;
};

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 unit = float2(vertexId & 1u, (vertexId >> 1u) & 1u);
    const float2 pixel = Origin.xy + Axis.xy * unit.x + Axis.zw * unit.y;
    VertexOutput output;
    output.Position = float4(
        pixel.x * (2.0 / Origin.z) - 1.0,
        1.0 - pixel.y * (2.0 / Origin.w),
        0.0,
        1.0);
    output.Uv = lerp(UvRect.xy, UvRect.zw, unit);
    return output;
}

bool IsRadarPaletteColor(float3 rgb)
{
    static const uint Palette[15] = {
        0xC0F868u, 0xF8A8A8u, 0xE03030u,
        0xA0A0A0u, 0xC8C8C8u, 0x909090u,
        0xF88010u, 0xF8D0A0u, 0xD86800u,
        0x88E008u, 0xC8F880u, 0x68B800u,
        0x1098C8u, 0x28D8F8u, 0xA8A8A8u
    };
    const uint3 color = ((uint3)round(rgb * 255.0)) & uint3(0xf8u, 0xf8u, 0xf8u);
    const uint packed = (color.r << 16u) | (color.g << 8u) | color.b;
    uint matched = 0u;
    [unroll]
    for (uint index = 0; index < 15u; ++index)
        matched |= packed == Palette[index] ? 1u : 0u;
    return matched != 0u;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    if (Tint.a < 0.0)
    {
        const float2 centered = input.Uv * 2.0 - 1.0;
        const float distanceSquared = dot(centered, centered);
        if (distanceSquared > 1.0)
            discard;

        const float2 sourceUv = float2(
            (128.0 + centered.x * Tint.z) / 256.0,
            (Tint.y + centered.y * Tint.z) / 192.0);
        const float3 rgb = Source.Sample(LinearSampler, float3(sourceUv, 0.0)).rgb;
        if (!IsRadarPaletteColor(rgb))
            discard;

        const float alpha = Tint.x * (1.0 - smoothstep(0.95, 1.0, distanceSquared));
        return float4(rgb * alpha, alpha);
    }

    const float4 color = Params.x != 0u
        ? Source.Sample(LinearSampler, float3(input.Uv, 0.0))
        : Source.Sample(PointSampler, float3(input.Uv, 0.0));
    return color * Tint;
}
)hlsl";
} // namespace

DX12SurfacePresenter::~DX12SurfacePresenter()
{
    Shutdown();
}

bool DX12SurfacePresenter::Init(
    HWND window,
    std::uint64_t surfaceGeneration,
    std::uint32_t initialWidth,
    std::uint32_t initialHeight)
{
    Shutdown();
    if (!window)
    {
        Error = "DX12 presentation requires a native Win32 window";
        return false;
    }

    Context = &melonDS::DX12Context::Get();
    if (!Context->Acquire())
    {
        Error = Context->GetFailureReason();
        Context = nullptr;
        return false;
    }

    Window = window;
    SurfaceGeneration = surfaceGeneration;
    if (!Commands.Init(Context->GetDevice(), Context->GetQueue())
        || !Descriptors.Init(Context->GetDevice(), kDescriptorHeapCount, true)
        || !Descriptors.Allocate(
            kPersistentDescriptorCount, PersistentCpuBase, PersistentGpuBase)
        || !CreateGraphicsObjects())
    {
        if (Error.empty())
            Error = "DX12 presentation graphics object creation failed";
        Shutdown();
        return false;
    }

    std::uint32_t width = initialWidth;
    std::uint32_t height = initialHeight;
    if (width == 0 || height == 0)
    {
        RECT client{};
        GetClientRect(Window, &client);
        width = std::max<LONG>(1, client.right - client.left);
        height = std::max<LONG>(1, client.bottom - client.top);
    }
    if (!CreateSwapchain(width, height))
    {
        Shutdown();
        return false;
    }

    Initialized = true;
    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Info,
        "DX12 native presentation initialized path=GPU-layer-composition buffers=%u tearing=%d\n",
        kBufferCount,
        TearingSupported ? 1 : 0);
    return true;
}

void DX12SurfacePresenter::Quiesce() noexcept
{
    if (!Context)
        return;

    // The presenter and the DX12 renderer share this direct queue. Queue-wide
    // completion is required here because the direct SRV cache contains raw
    // resource identity and the renderer output lease is released immediately
    // after this hook returns. This is a transition-only wait, never a frame
    // path wait.
    if (!Commands.WaitQueueIdle())
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Warn,
            "DX12 presentation queue did not become idle at renderer transition\n");
    }
    OpenList = nullptr;
    FrameOpen = false;
    FrameReady = false;
    NativeSource = nullptr;
    NativeSourceDirect = false;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    PerfRecordStart = {};
#endif
}

void DX12SurfacePresenter::Shutdown() noexcept
{
    if (Context)
        Commands.WaitQueueIdle();

    CloseFrameLatencyWaitable();

    for (LayerTexture& layer : Layers)
    {
        if (layer.Upload && layer.UploadMapped)
        {
            D3D12_RANGE noWrite{0, 0};
            layer.Upload->Unmap(0, &noWrite);
        }
        layer = {};
    }

    NativeSource = nullptr;
    NativeSourceDirect = false;
    PersistentCpuBase = {};
    PersistentGpuBase = {};
    PersistentDescriptorCount = kPersistentDescriptorCount;
    CurrentDirectResourceGeneration = 0;
    for (DirectSrvCacheEntry& entry : DirectSrvCache)
        entry = {};
    OpenList = nullptr;
    OpaquePipeline.Reset();
    BlendedPipeline.Reset();
    RootSignature.Reset();
    RtvHeap.Reset();
    Descriptors.Shutdown();
    for (auto& buffer : BackBuffers)
        buffer.Reset();
    Swapchain.Reset();
    Commands.Shutdown();
    Width = 0;
    Height = 0;
    RtvIncrement = 0;
    SwapchainFlags = 0;
    TearingSupported = false;
    Initialized = false;
    FrameOpen = false;
    FrameReady = false;
    FirstPresentLogged = false;
    PresentWaitStateLogged = false;
    LastPresentWaitEnabled = true;
    LastBeginBackpressure = false;
    PresentModeLogged = false;
    LastPresentVsync = false;
    PresentResultLogged = false;
    LastPresentResult = S_OK;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    PerfRecordStart = {};
#endif
    Window = nullptr;
    SurfaceGeneration = 0;
    SwapchainGeneration = 0;
    if (Context)
    {
        Context->Release();
        Context = nullptr;
    }
}

void DX12SurfacePresenter::CloseFrameLatencyWaitable() noexcept
{
    if (!FrameLatencyWaitable)
        return;

    ::CloseHandle(FrameLatencyWaitable);
    FrameLatencyWaitable = nullptr;
}

bool DX12SurfacePresenter::CreateGraphicsObjects()
{
    ID3D12Device* device = Context ? Context->GetDevice() : nullptr;
    const auto& entry = melonDS::DX12::LoadEntryPoints();
    if (!device || !entry.D3D12SerializeRootSignature)
        return false;

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = kDrawConstantDwords;
    params[0].Constants.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (UINT index = 0; index < 2; ++index)
    {
        samplers[index].Filter = index == 0
            ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samplers[index].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[index].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[index].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[index].MipLODBias = 0.0f;
        samplers[index].MaxAnisotropy = 1;
        samplers[index].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[index].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        samplers[index].MinLOD = 0.0f;
        samplers[index].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[index].ShaderRegister = index;
        samplers[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = 2;
    rootDesc.pStaticSamplers = samplers;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    melonDS::DX12::ComPtr<ID3DBlob> blob;
    melonDS::DX12::ComPtr<ID3DBlob> errors;
    HRESULT hr = entry.D3D12SerializeRootSignature(
        &rootDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        blob.ReleaseAndGetAddressOf(),
        errors.ReleaseAndGetAddressOf());
    if (FAILED(hr))
        return Fail("D3D12SerializeRootSignature(presenter)", hr);
    hr = device->CreateRootSignature(
        0,
        blob->GetBufferPointer(),
        blob->GetBufferSize(),
        IID_PPV_ARGS(RootSignature.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return Fail("CreateRootSignature(presenter)", hr);

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = kBufferCount;
    hr = device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(RtvHeap.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return Fail("CreateDescriptorHeap(RTV)", hr);
    RtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    return CreatePipeline(false, OpaquePipeline) && CreatePipeline(true, BlendedPipeline);
}

bool DX12SurfacePresenter::CreatePipeline(
    bool blended,
    melonDS::DX12::ComPtr<ID3D12PipelineState>& output)
{
    auto vertex = Context->CompileShader(kPresentShader, "VSMain", "vs_5_1", {}, "DX12 presenter VS");
    auto pixel = Context->CompileShader(kPresentShader, "PSMain", "ps_5_1", {}, "DX12 presenter PS");
    if (!vertex || !pixel)
        return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = RootSignature.Get();
    desc.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    desc.BlendState.AlphaToCoverageEnable = FALSE;
    desc.BlendState.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC& target = desc.BlendState.RenderTarget[0];
    target.BlendEnable = blended ? TRUE : FALSE;
    target.LogicOpEnable = FALSE;
    target.SrcBlend = D3D12_BLEND_ONE;
    target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D12_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D12_BLEND_ONE;
    target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    target.LogicOp = D3D12_LOGIC_OP_NOOP;
    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    desc.SampleMask = std::numeric_limits<UINT>::max();
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FrontCounterClockwise = FALSE;
    desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.InputLayout = {nullptr, 0};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;

    const HRESULT hr = Context->GetDevice()->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(output.ReleaseAndGetAddressOf()));
    return SUCCEEDED(hr) || Fail("CreateGraphicsPipelineState(presenter)", hr);
}

bool DX12SurfacePresenter::CreateSwapchain(std::uint32_t width, std::uint32_t height)
{
    IDXGIFactory6* factory = Context ? Context->GetFactory() : nullptr;
    if (!factory)
    {
        Error = "DX12 presentation has no DXGI factory";
        return false;
    }

    melonDS::DX12::ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(factory5.ReleaseAndGetAddressOf()))))
    {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
        {
            TearingSupported = allowTearing == TRUE;
        }
    }

    SwapchainFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (TearingSupported)
        SwapchainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kBufferCount;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = SwapchainFlags;

    melonDS::DX12::ComPtr<IDXGISwapChain1> baseSwapchain;
    HRESULT hr = factory->CreateSwapChainForHwnd(
        Context->GetQueue(), Window, &desc, nullptr, nullptr,
        baseSwapchain.ReleaseAndGetAddressOf());
    if (FAILED(hr))
        return Fail("CreateSwapChainForHwnd", hr);
    hr = baseSwapchain->QueryInterface(IID_PPV_ARGS(Swapchain.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return Fail("IDXGISwapChain3", hr);

    if (!ApplySdrColorSpace())
        return false;

    factory->MakeWindowAssociation(Window, DXGI_MWA_NO_ALT_ENTER);
    hr = Swapchain->SetMaximumFrameLatency(1);
    if (FAILED(hr))
        return Fail("SetMaximumFrameLatency", hr);
    FrameLatencyWaitable = Swapchain->GetFrameLatencyWaitableObject();
    if (!FrameLatencyWaitable)
    {
        Error = "DXGI did not provide a frame-latency waitable object";
        return false;
    }

    Width = width;
    Height = height;
    ++SwapchainGeneration;
    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Info,
        "DX12 swapchain recreated surfaceGeneration=%llu hwnd=%p "
        "swapchainGeneration=%llu extent=%ux%u format=DXGI_FORMAT_B8G8R8A8_UNORM "
        "colorSpace=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709\n",
        static_cast<unsigned long long>(SurfaceGeneration),
        static_cast<void*>(Window),
        static_cast<unsigned long long>(SwapchainGeneration),
        Width,
        Height);
    melonDS::DX12Perf::SetCounter(
        melonDS::DX12Perf::Counter::DX12BackBufferCount, kBufferCount);
    melonDS::DX12Perf::SetCounter(
        melonDS::DX12Perf::Counter::DX12PresenterLogicalDepth, 1u);
    return AcquireBackBuffers();
}

bool DX12SurfacePresenter::AcquireBackBuffers()
{
    if (!RtvHeap)
        return false;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = RtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < kBufferCount; ++index)
    {
        const HRESULT hr = Swapchain->GetBuffer(
            index, IID_PPV_ARGS(BackBuffers[index].ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            return Fail("IDXGISwapChain::GetBuffer", hr);
        BackBufferRtvs[index] = rtv;
        Context->GetDevice()->CreateRenderTargetView(BackBuffers[index].Get(), nullptr, rtv);
        rtv.ptr += RtvIncrement;
    }
    return true;
}

bool DX12SurfacePresenter::Resize(std::uint32_t width, std::uint32_t height)
{
    if (width == Width && height == Height)
        return true;
    if (!Swapchain || width == 0 || height == 0)
        return false;
    if (!Commands.WaitQueueIdle())
    {
        Error = "DX12 presentation queue did not become idle before resize";
        return false;
    }
    for (auto& buffer : BackBuffers)
        buffer.Reset();
    FrameReady = false;

    const HRESULT hr = Swapchain->ResizeBuffers(
        kBufferCount, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, SwapchainFlags);
    if (FAILED(hr))
        return Fail("IDXGISwapChain::ResizeBuffers", hr);

    if (!ApplySdrColorSpace())
        return false;

    Width = width;
    Height = height;
    ++SwapchainGeneration;
    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Info,
        "DX12 swapchain resized surfaceGeneration=%llu hwnd=%p "
        "swapchainGeneration=%llu extent=%ux%u format=DXGI_FORMAT_B8G8R8A8_UNORM "
        "colorSpace=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709\n",
        static_cast<unsigned long long>(SurfaceGeneration),
        static_cast<void*>(Window),
        static_cast<unsigned long long>(SwapchainGeneration),
        Width,
        Height);
    CloseFrameLatencyWaitable();
    FrameLatencyWaitable = Swapchain->GetFrameLatencyWaitableObject();
    return FrameLatencyWaitable && AcquireBackBuffers();
}


bool DX12SurfacePresenter::ApplySdrColorSpace()
{
    if (!Swapchain)
    {
        Error = "DXGI SDR color-space setup has no swapchain";
        return false;
    }

    constexpr DXGI_COLOR_SPACE_TYPE colorSpace =
        DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    UINT support = 0;
    HRESULT hr = Swapchain->CheckColorSpaceSupport(colorSpace, &support);
    if (FAILED(hr)
        || (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0)
    {
        return Fail("IDXGISwapChain3::CheckColorSpaceSupport(SDR)",
            FAILED(hr) ? hr : DXGI_ERROR_UNSUPPORTED);
    }
    hr = Swapchain->SetColorSpace1(colorSpace);
    if (FAILED(hr))
        return Fail("IDXGISwapChain3::SetColorSpace1(SDR)", hr);
    return true;
}


D3D12_CPU_DESCRIPTOR_HANDLE DX12SurfacePresenter::PersistentCpuAt(
    std::uint32_t index) const noexcept
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = PersistentCpuBase;
    handle.ptr += static_cast<SIZE_T>(index) * Descriptors.GetIncrement();
    return handle;
}


D3D12_GPU_DESCRIPTOR_HANDLE DX12SurfacePresenter::PersistentGpuAt(
    std::uint32_t index) const noexcept
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = PersistentGpuBase;
    handle.ptr += static_cast<UINT64>(index) * Descriptors.GetIncrement();
    return handle;
}


bool DX12SurfacePresenter::CreateLayerPersistentSrv(Layer layerId)
{
    if (!Context || layerId == Layer::Count)
        return false;

    LayerTexture& layer = Layers[static_cast<std::size_t>(layerId)];
    ID3D12Resource* source = layer.Texture.Get();
    if (!source)
        return false;
    if (layer.PersistentSrvValid && layer.PersistentSrvResource == source)
        return true;

    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture2DArray.MostDetailedMip = 0;
    desc.Texture2DArray.MipLevels = 1;
    desc.Texture2DArray.FirstArraySlice = 0;
    desc.Texture2DArray.ArraySize = 1;
    desc.Texture2DArray.PlaneSlice = 0;
    desc.Texture2DArray.ResourceMinLODClamp = 0.0f;

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const auto descriptorStart = melonDS::DX12Perf::Clock::now();
#endif
    Context->GetDevice()->CreateShaderResourceView(
        source, &desc, PersistentCpuAt(static_cast<std::uint32_t>(layerId)));
    layer.PersistentDescriptorIndex = static_cast<std::uint32_t>(layerId);
    layer.PersistentSrvResource = source;
    layer.PersistentSrvValid = true;
    melonDS::DX12Perf::AddCounter(
        melonDS::DX12Perf::Counter::PresenterSrvCreateCount);
    melonDS::DX12Perf::AddCounter(
        melonDS::DX12Perf::Counter::PresenterDescriptorPersistentCreateCount);
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    if (melonDS::DX12Perf::IsEnabled())
    {
        melonDS::DX12Perf::AddCounter(
            melonDS::DX12Perf::Counter::PresenterDescriptorCpuTimeNs,
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                melonDS::DX12Perf::Clock::now() - descriptorStart).count()));
    }
#endif
    return true;
}


const DX12SurfacePresenter::DirectSrvCacheEntry* DX12SurfacePresenter::FindDirectSrv(
    ID3D12Resource* resource,
    std::uint32_t arraySlice,
    std::uint64_t resourceGeneration) const noexcept
{
    for (const DirectSrvCacheEntry& entry : DirectSrvCache)
    {
        if (entry.Valid && entry.Resource == resource
            && entry.ArraySlice == arraySlice
            && entry.ResourceGeneration == resourceGeneration)
        {
            return &entry;
        }
    }
    return nullptr;
}


bool DX12SurfacePresenter::EnsureDirectSrv(
    ID3D12Resource* resource,
    std::uint32_t arraySlice,
    std::uint64_t resourceGeneration)
{
    if (!Context || !resource || resourceGeneration == 0
        || arraySlice >= kDirectArraySliceCount)
    {
        return false;
    }
    if (FindDirectSrv(resource, arraySlice, resourceGeneration))
    {
        melonDS::DX12Perf::AddCounter(
            melonDS::DX12Perf::Counter::PresenterDescriptorCacheHitCount);
        return true;
    }

    melonDS::DX12Perf::AddCounter(
        melonDS::DX12Perf::Counter::PresenterDescriptorCacheMissCount);
    DirectSrvCacheEntry* freeEntry = nullptr;
    for (DirectSrvCacheEntry& entry : DirectSrvCache)
    {
        if (!entry.Valid)
        {
            freeEntry = &entry;
            break;
        }
    }
    if (!freeEntry)
        return false;

    const std::uint32_t entryIndex = static_cast<std::uint32_t>(
        freeEntry - DirectSrvCache.data());
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture2DArray.MostDetailedMip = 0;
    desc.Texture2DArray.MipLevels = 1;
    desc.Texture2DArray.FirstArraySlice = arraySlice;
    desc.Texture2DArray.ArraySize = 1;
    desc.Texture2DArray.PlaneSlice = 0;
    desc.Texture2DArray.ResourceMinLODClamp = 0.0f;

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const auto descriptorStart = melonDS::DX12Perf::Clock::now();
#endif
    Context->GetDevice()->CreateShaderResourceView(
        resource, &desc,
        PersistentCpuAt(kPersistentFallbackDescriptorCount + entryIndex));
    freeEntry->Resource = resource;
    freeEntry->ArraySlice = arraySlice;
    freeEntry->ResourceGeneration = resourceGeneration;
    freeEntry->DescriptorIndex = kPersistentFallbackDescriptorCount + entryIndex;
    freeEntry->Valid = true;
    melonDS::DX12Perf::AddCounter(
        melonDS::DX12Perf::Counter::PresenterSrvCreateCount);
    melonDS::DX12Perf::AddCounter(
        melonDS::DX12Perf::Counter::PresenterDescriptorPersistentCreateCount);
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    if (melonDS::DX12Perf::IsEnabled())
    {
        melonDS::DX12Perf::AddCounter(
            melonDS::DX12Perf::Counter::PresenterDescriptorCpuTimeNs,
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                melonDS::DX12Perf::Clock::now() - descriptorStart).count()));
    }
#endif
    return true;
}


bool DX12SurfacePresenter::PrepareDirectOutputDescriptors(
    const melonDS::DX12PresentedFrame& frame)
{
    if (!Initialized || !Context)
        return false;
    if (!frame.HasDirectSampledOutput() || frame.ResourceGeneration == 0)
        return true;

    if (CurrentDirectResourceGeneration != frame.ResourceGeneration)
    {
        bool hadEntries = false;
        for (const DirectSrvCacheEntry& entry : DirectSrvCache)
            hadEntries |= entry.Valid;
        if (hadEntries)
            Commands.WaitIdle();
        for (DirectSrvCacheEntry& entry : DirectSrvCache)
            entry = {};
        CurrentDirectResourceGeneration = frame.ResourceGeneration;
        melonDS::DX12Perf::AddCounter(
            melonDS::DX12Perf::Counter::PresenterDescriptorCacheInvalidateCount);
    }

    // The two array slices are the only direct presenter views. A full cache
    // is not fatal: DrawLayer() keeps the transient tail as a safe fallback.
    EnsureDirectSrv(frame.DirectTexture, 0, frame.ResourceGeneration);
    EnsureDirectSrv(frame.DirectTexture, 1, frame.ResourceGeneration);
    return true;
}


void DX12SurfacePresenter::InvalidateDirectDescriptorCache() noexcept
{
    for (DirectSrvCacheEntry& entry : DirectSrvCache)
        entry = {};
    CurrentDirectResourceGeneration = 0;
    melonDS::DX12Perf::AddCounter(
        melonDS::DX12Perf::Counter::PresenterDescriptorCacheInvalidateCount);
}

bool DX12SurfacePresenter::EnsureLayerTexture(
    Layer layerId,
    std::uint32_t width,
    std::uint32_t height)
{
    LayerTexture& layer = Layers[static_cast<std::size_t>(layerId)];
    if (layer.Texture && layer.Width == width && layer.Height == height)
    {
        return layer.PersistentSrvValid && layer.PersistentSrvResource == layer.Texture.Get()
            ? true
            : CreateLayerPersistentSrv(layerId);
    }

    if (layerId == Layer::Hud)
        melonDS::DX12Perf::AddCounter(melonDS::DX12Perf::Counter::HudTextureRecreateCount);

    layer.Texture = Context->CreateTexture2D(
        DXGI_FORMAT_B8G8R8A8_UNORM,
        width,
        height,
        1,
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST,
        L"MelonPrime DX12 presentation layer");
    if (!layer.Texture)
    {
        Error = "DX12 presentation layer texture allocation failed";
        return false;
    }
    layer.Width = width;
    layer.Height = height;
    layer.State = D3D12_RESOURCE_STATE_COPY_DEST;
    layer.Valid = false;
    layer.PersistentSrvValid = false;
    layer.PersistentSrvResource = nullptr;
    return CreateLayerPersistentSrv(layerId);
}

bool DX12SurfacePresenter::EnsureLayerUpload(
    LayerTexture& layer,
    std::uint32_t width,
    std::uint32_t height)
{
    const std::uint32_t rowPitch = AlignTexturePitch(width * sizeof(std::uint32_t));
    const std::uint64_t required = static_cast<std::uint64_t>(rowPitch) * height;
    if (layer.Upload && layer.UploadMapped && layer.UploadCapacity >= required
        && layer.UploadRowPitch == rowPitch)
    {
        return true;
    }

    if (layer.Upload && layer.UploadMapped)
    {
        D3D12_RANGE noWrite{0, 0};
        layer.Upload->Unmap(0, &noWrite);
    }
    layer.UploadMapped = nullptr;
    layer.Upload.Reset();
    layer.Upload = Context->CreateBuffer(
        required,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 presentation layer upload");
    if (!layer.Upload)
    {
        Error = "DX12 presentation layer upload allocation failed";
        return false;
    }
    D3D12_RANGE noRead{0, 0};
    const HRESULT hr = layer.Upload->Map(
        0, &noRead, reinterpret_cast<void**>(&layer.UploadMapped));
    if (FAILED(hr) || !layer.UploadMapped)
        return Fail("DX12 presentation layer upload map", hr);
    layer.UploadCapacity = required;
    layer.UploadRowPitch = rowPitch;
    return true;
}

bool DX12SurfacePresenter::WaitForPresentSlot()
{
    if (!FrameLatencyWaitable)
        return false;
    for (;;)
    {
        const DWORD result = WaitForSingleObjectEx(FrameLatencyWaitable, 1000, TRUE);
        if (result == WAIT_OBJECT_0)
            return true;
        if (result != WAIT_IO_COMPLETION)
        {
            Error = result == WAIT_TIMEOUT
                ? "DXGI frame-latency wait timed out"
                : "DXGI frame-latency wait failed";
            return false;
        }
    }
}

bool DX12SurfacePresenter::BeginFrame(
    std::uint32_t width,
    std::uint32_t height,
    bool waitForPresentSlot)
{
    LastBeginBackpressure = false;
    if (!Initialized || FrameOpen || width == 0 || height == 0)
        return false;
    if (!PresentWaitStateLogged || LastPresentWaitEnabled != waitForPresentSlot)
    {
        PresentWaitStateLogged = true;
        LastPresentWaitEnabled = waitForPresentSlot;
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Info,
            "DX12 presentation pacing frameLatencyWaitActive=%d policyValidation=%s\n",
            waitForPresentSlot ? 1 : 0,
            waitForPresentSlot ? "compatibility" : "developer-experiment");
    }
    if (waitForPresentSlot)
    {
        melonDS::DX12Perf::ScopedCpuTimer waitTimer(
            melonDS::DX12Perf::CpuMetric::PresentSlotWait);
        const auto waitStart = std::chrono::steady_clock::now();
        if (!WaitForPresentSlot())
        {
            // A saturated DXGI frame-latency slot is presenter backpressure,
            // not a renderer/runtime failure. The caller keeps the last good
            // native frame and retries on the next emulated frame.
            LastBeginBackpressure = true;
            return false;
        }
        const auto waitNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - waitStart).count();
        melonDS::DX12Perf::AddCounter(
            melonDS::DX12Perf::Counter::PresentWaitNs,
            static_cast<std::uint64_t>(waitNs > 0 ? waitNs : 0));
    }
    if (!Resize(width, height))
        return false;

    {
        melonDS::DX12Perf::ScopedCpuTimer waitTimer(
            melonDS::DX12Perf::CpuMetric::PresentBeginWait);
        OpenList = Commands.Begin();
    }
    if (!OpenList)
    {
        Error = "DX12 presentation command list could not begin";
        return false;
    }
    RecordDX12GpuMetric(
        Commands, melonDS::GpuMetric::PresenterRenderPass,
        melonDS::DX12Perf::Counter::PresenterRenderPassGpuTimeNs);
    RecordDX12GpuMetric(
        Commands, melonDS::GpuMetric::TotalQueueSpan,
        melonDS::DX12Perf::Counter::TotalQueueGpuSpanNs);
    Commands.WriteTimestamp(
        melonDS::GpuMetricQueryIndex(melonDS::GpuMetric::TotalQueueSpan, false));
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    if (melonDS::DX12Perf::IsEnabled())
        PerfRecordStart = melonDS::DX12Perf::Clock::now();
#endif
    // Keep the fixed SRV prefix intact; only the transient fallback tail is
    // recycled at the frame boundary.
    Descriptors.Reset(PersistentDescriptorCount);
    NativeSource = nullptr;
    NativeSourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    NativeSourceDirect = false;
    for (LayerTexture& texture : Layers)
    {
        texture.DirectTexture = nullptr;
        texture.DirectArraySlice = 0;
        texture.DirectResourceGeneration = 0;
        texture.UsesDirect = false;
    }

    const UINT index = Swapchain->GetCurrentBackBufferIndex();
    ID3D12Resource* backBuffer = BackBuffers[index].Get();
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    OpenList->ResourceBarrier(1, &barrier);

    Commands.WriteTimestamp(
        melonDS::GpuMetricQueryIndex(melonDS::GpuMetric::PresenterRenderPass, false));
    const float clear[4]{0.0f, 0.0f, 0.0f, 1.0f};
    OpenList->OMSetRenderTargets(1, &BackBufferRtvs[index], FALSE, nullptr);
    OpenList->ClearRenderTargetView(BackBufferRtvs[index], clear, 0, nullptr);
    ID3D12DescriptorHeap* heaps[]{Descriptors.GetHeap()};
    OpenList->SetDescriptorHeaps(1, heaps);
    OpenList->SetGraphicsRootSignature(RootSignature.Get());
    OpenList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    const D3D12_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(Width), static_cast<LONG>(Height)};
    OpenList->RSSetViewports(1, &viewport);
    OpenList->RSSetScissorRects(1, &scissor);
    FrameOpen = true;
    FrameReady = false;
    return true;
}

void DX12SurfacePresenter::TransitionLayer(
    LayerTexture& layer,
    D3D12_RESOURCE_STATES after)
{
    if (!OpenList || !layer.Texture || layer.State == after)
        return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = layer.Texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = layer.State;
    barrier.Transition.StateAfter = after;
    OpenList->ResourceBarrier(1, &barrier);
    layer.State = after;
}

void DX12SurfacePresenter::TransitionNativeSource(D3D12_RESOURCE_STATES after)
{
    if (!OpenList || !NativeSource || NativeSourceState == after)
        return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = NativeSource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = NativeSourceState;
    barrier.Transition.StateAfter = after;
    OpenList->ResourceBarrier(1, &barrier);
    NativeSourceState = after;
}

bool DX12SurfacePresenter::UploadLayer(
    Layer layerId,
    const void* pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t rowBytes)
{
    melonDS::DX12Perf::ScopedCpuTimer hudTimer(
        melonDS::DX12Perf::CpuMetric::HudUpload, layerId == Layer::Hud);
    if (!FrameOpen || !pixels || width == 0 || height == 0
        || rowBytes < static_cast<std::size_t>(width) * sizeof(std::uint32_t))
    {
        return false;
    }
    if (!EnsureLayerTexture(layerId, width, height))
        return false;
    LayerTexture& layer = Layers[static_cast<std::size_t>(layerId)];
    if (!EnsureLayerUpload(layer, width, height))
        return false;

    const auto* source = static_cast<const std::uint8_t*>(pixels);
    const std::size_t copyBytes = static_cast<std::size_t>(width) * sizeof(std::uint32_t);
    if (layerId == Layer::Hud)
    {
        melonDS::DX12Perf::AddCounter(
            melonDS::DX12Perf::Counter::HudUploadBytes,
            static_cast<std::uint64_t>(copyBytes) * height);
    }
    for (std::uint32_t row = 0; row < height; ++row)
    {
        std::memcpy(
            layer.UploadMapped + static_cast<std::size_t>(row) * layer.UploadRowPitch,
            source + static_cast<std::size_t>(row) * rowBytes,
            copyBytes);
    }

    TransitionLayer(layer, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
    sourceLocation.pResource = layer.Upload.Get();
    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sourceLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sourceLocation.PlacedFootprint.Footprint.Width = width;
    sourceLocation.PlacedFootprint.Footprint.Height = height;
    sourceLocation.PlacedFootprint.Footprint.Depth = 1;
    sourceLocation.PlacedFootprint.Footprint.RowPitch = layer.UploadRowPitch;

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = layer.Texture.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    OpenList->CopyTextureRegion(&destination, 0, 0, 0, &sourceLocation, nullptr);
    TransitionLayer(layer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    layer.Valid = true;
    return true;
}

bool DX12SurfacePresenter::UploadLayerRegion(
    Layer layer,
    const void* pixels,
    std::uint32_t sourceX,
    std::uint32_t sourceY,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t rowBytes)
{
    if (!pixels || rowBytes < (static_cast<std::size_t>(sourceX) + width) * sizeof(std::uint32_t))
        return false;
    const auto* source = static_cast<const std::uint8_t*>(pixels)
        + static_cast<std::size_t>(sourceY) * rowBytes
        + static_cast<std::size_t>(sourceX) * sizeof(std::uint32_t);
    return UploadLayer(layer, source, width, height, rowBytes);
}

bool DX12SurfacePresenter::UploadLayerFromBuffer(
    Layer layerId,
    const melonDS::DX12PresentedFrame& frame,
    std::uint64_t sourceOffset)
{
    if (!FrameOpen || !frame.Buffer || frame.Width == 0 || frame.Height == 0)
        return false;
    if (!EnsureLayerTexture(layerId, frame.Width, frame.Height))
        return false;
    melonDS::DX12Perf::AddCounter(
        melonDS::DX12Perf::Counter::PresentedScreenCopyBytes,
        static_cast<std::uint64_t>(frame.Width) * frame.Height * sizeof(std::uint32_t));

    if (NativeSource && NativeSource != frame.Buffer)
        TransitionNativeSource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (NativeSource != frame.Buffer)
    {
        NativeSource = frame.Buffer;
        NativeSourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        NativeSourceDirect = false;
    }
    TransitionNativeSource(D3D12_RESOURCE_STATE_COPY_SOURCE);

    LayerTexture& layer = Layers[static_cast<std::size_t>(layerId)];
    layer.DirectTexture = nullptr;
    layer.DirectArraySlice = 0;
    layer.UsesDirect = false;
    TransitionLayer(layer, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = frame.Buffer;
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint.Offset = sourceOffset;
    source.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    source.PlacedFootprint.Footprint.Width = frame.Width;
    source.PlacedFootprint.Footprint.Height = frame.Height;
    source.PlacedFootprint.Footprint.Depth = 1;
    source.PlacedFootprint.Footprint.RowPitch = AlignTexturePitch(frame.Width * sizeof(std::uint32_t));

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = layer.Texture.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    OpenList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    TransitionLayer(layer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    layer.Valid = true;
    return true;
}

bool DX12SurfacePresenter::UploadLayerFromTexture(
    Layer layerId,
    const melonDS::DX12PresentedFrame& frame)
{
    if (!FrameOpen || !OpenList || !frame.HasDirectSampledOutput()
        || frame.Width == 0 || frame.Height == 0
        || (layerId != Layer::ScreenTop && layerId != Layer::ScreenBottom))
    {
        return false;
    }

    if (NativeSource && NativeSource != frame.DirectTexture)
        return false;
    if (NativeSource != frame.DirectTexture)
    {
        NativeSource = frame.DirectTexture;
        // The renderer submitted the compositor UAV->PS transition before it
        // published this leased frame. The presenter does not own that
        // resource's producer transition; record the state it receives and
        // leave it in PS_RESOURCE for the graphics draws and next producer.
        NativeSourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        NativeSourceDirect = true;
    }

    LayerTexture& layer = Layers[static_cast<std::size_t>(layerId)];
    layer.DirectTexture = frame.DirectTexture;
    layer.DirectArraySlice = layerId == Layer::ScreenTop ? 0u : 1u;
    layer.DirectResourceGeneration = frame.ResourceGeneration;
    layer.Width = frame.Width;
    layer.Height = frame.Height;
    layer.UsesDirect = true;
    layer.Valid = true;
    return true;
}

bool DX12SurfacePresenter::ResolveLayerSrv(
    Layer layerId,
    D3D12_GPU_DESCRIPTOR_HANDLE& gpu)
{
    LayerTexture& layer = Layers[static_cast<std::size_t>(layerId)];
    if (!layer.Valid)
        return false;

    if (layer.UsesDirect)
    {
        const DirectSrvCacheEntry* entry = FindDirectSrv(
            layer.DirectTexture, layer.DirectArraySlice, layer.DirectResourceGeneration);
        if (entry)
        {
            gpu = PersistentGpuAt(entry->DescriptorIndex);
            return true;
        }
    }
    else if (layer.Texture && layer.PersistentSrvValid
        && layer.PersistentSrvResource == layer.Texture.Get())
    {
        gpu = PersistentGpuAt(layer.PersistentDescriptorIndex);
        return true;
    }

    // Cache invariant failures and direct-cache overflow retain the old
    // transient path. It starts after the persistent prefix and is reset only
    // at BeginFrame(), so no persistent/in-flight descriptor is overwritten.
    melonDS::DX12Perf::AddCounter(
        melonDS::DX12Perf::Counter::PresenterDescriptorFallbackCount);
    return AllocateLayerSrv(layerId, gpu);
}


bool DX12SurfacePresenter::AllocateLayerSrv(
    Layer layerId,
    D3D12_GPU_DESCRIPTOR_HANDLE& gpu)
{
    LayerTexture& layer = Layers[static_cast<std::size_t>(layerId)];
    ID3D12Resource* source = layer.UsesDirect ? layer.DirectTexture : layer.Texture.Get();
    if (!layer.Valid || !source)
        return false;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    if (!Descriptors.Allocate(1, cpu, gpu))
    {
        Error = "DX12 presentation descriptor heap exhausted";
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = layer.UsesDirect
        ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    // All presentation layers are one-slice Texture2D resources, while the
    // compositor's direct output is a two-slice Texture2DArray. Bind both as
    // a one-slice array so the pixel shader has one resource type for either
    // source and selects the direct screen through FirstArraySlice.
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture2DArray.MostDetailedMip = 0;
    desc.Texture2DArray.MipLevels = 1;
    desc.Texture2DArray.FirstArraySlice = layer.UsesDirect
        ? layer.DirectArraySlice : 0u;
    desc.Texture2DArray.ArraySize = 1;
    desc.Texture2DArray.PlaneSlice = 0;
    desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const auto descriptorStart = melonDS::DX12Perf::Clock::now();
#endif
    Context->GetDevice()->CreateShaderResourceView(source, &desc, cpu);
    melonDS::DX12Perf::AddCounter(
        melonDS::DX12Perf::Counter::PresenterSrvCreateCount);
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    if (melonDS::DX12Perf::IsEnabled())
    {
        melonDS::DX12Perf::AddCounter(
            melonDS::DX12Perf::Counter::PresenterDescriptorCpuTimeNs,
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                melonDS::DX12Perf::Clock::now() - descriptorStart).count()));
    }
#endif
    return true;
}

void DX12SurfacePresenter::DrawLayer(
    Layer layerId,
    const Quad& quad,
    Blend blend,
    bool linearFilter)
{
    if (!FrameOpen || !OpenList)
        return;
    D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    if (!ResolveLayerSrv(layerId, srv))
        return;

    DrawConstants constants{};
    std::memcpy(constants.Axis, quad.Axis, sizeof(constants.Axis));
    std::memcpy(constants.Origin, quad.Origin, sizeof(constants.Origin));
    std::memcpy(constants.UvRect, quad.UvRect, sizeof(constants.UvRect));
    std::memcpy(constants.Tint, quad.Tint, sizeof(constants.Tint));
    constants.Params[0] = linearFilter ? 1u : 0u;

    OpenList->SetPipelineState(
        blend == Blend::Premultiplied ? BlendedPipeline.Get() : OpaquePipeline.Get());
    OpenList->SetGraphicsRoot32BitConstants(0, kDrawConstantDwords, &constants, 0);
    OpenList->SetGraphicsRootDescriptorTable(1, srv);
    OpenList->DrawInstanced(4, 1, 0, 0);
}

void DX12SurfacePresenter::DrawRadar(
    const Quad& quad,
    float opacity,
    std::uint32_t sourceCenterY,
    std::uint32_t sourceRadius)
{
    Quad radar = quad;
    radar.Tint[0] = opacity;
    radar.Tint[1] = static_cast<float>(sourceCenterY);
    radar.Tint[2] = static_cast<float>(sourceRadius);
    radar.Tint[3] = -1.0f;
    DrawLayer(Layer::ScreenBottom, radar, Blend::Premultiplied, true);
}

bool DX12SurfacePresenter::EndFrame()
{
    if (!FrameOpen || !OpenList)
        return false;
    if (!NativeSourceDirect)
        TransitionNativeSource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    const UINT index = Swapchain->GetCurrentBackBufferIndex();
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = BackBuffers[index].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    OpenList->ResourceBarrier(1, &barrier);
    Commands.WriteTimestamp(
        melonDS::GpuMetricQueryIndex(melonDS::GpuMetric::PresenterRenderPass, true));
    Commands.WriteTimestamp(
        melonDS::GpuMetricQueryIndex(melonDS::GpuMetric::TotalQueueSpan, true));

    OpenList = nullptr;
    FrameOpen = false;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    if (PerfRecordStart != std::chrono::steady_clock::time_point{})
    {
        melonDS::DX12Perf::AddDuration(
            melonDS::DX12Perf::CpuMetric::PresentRecord, PerfRecordStart);
        PerfRecordStart = {};
    }
#endif
    bool submitted = false;
    {
        melonDS::DX12Perf::ScopedCpuTimer submitTimer(
            melonDS::DX12Perf::CpuMetric::QueueSubmit);
        submitted = Commands.Submit();
    }
    if (!submitted)
    {
        Error = "DX12 presentation command submission failed";
        return false;
    }
    FrameReady = true;
    return true;
}

bool DX12SurfacePresenter::Present(bool vsync)
{
    if (!Initialized || !FrameReady || !Swapchain)
        return false;

    const UINT syncInterval = vsync ? 1u : 0u;
    const UINT flags = !vsync && TearingSupported ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    melonDS::DX12Perf::SetCounter(
        melonDS::DX12Perf::Counter::DX12VsyncEnabled, vsync ? 1u : 0u);
    melonDS::DX12Perf::SetCounter(
        melonDS::DX12Perf::Counter::DX12PresentMode,
        vsync ? 1u : (flags != 0 ? 2u : 0u));
    melonDS::DX12Perf::SetCounter(
        melonDS::DX12Perf::Counter::DX12BackBufferCount, kBufferCount);
    melonDS::DX12Perf::SetCounter(
        melonDS::DX12Perf::Counter::DX12PresenterLogicalDepth, 1u);
    if (!PresentModeLogged || LastPresentVsync != vsync)
    {
        PresentModeLogged = true;
        LastPresentVsync = vsync;
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Info,
            "DX12 presentation mode vsync=%d syncInterval=%u allowTearing=%d "
            "frameLatencyWaitActive=%d\n",
            vsync ? 1 : 0,
            syncInterval,
            flags != 0 ? 1 : 0,
            LastPresentWaitEnabled ? 1 : 0);
    }
    const HRESULT hr = Swapchain->Present(syncInterval, flags);
    FrameReady = false;
    if (!PresentResultLogged || LastPresentResult != hr)
    {
        PresentResultLogged = true;
        LastPresentResult = hr;
        melonDS::Platform::Log(
            FAILED(hr) ? melonDS::Platform::LogLevel::Error
                       : melonDS::Platform::LogLevel::Info,
            "DX12 presentation result HRESULT=0x%08lX success=%d occluded=%d\n",
            static_cast<unsigned long>(hr),
            SUCCEEDED(hr) ? 1 : 0,
            hr == DXGI_STATUS_OCCLUDED ? 1 : 0);
    }
    if (hr == DXGI_STATUS_OCCLUDED)
        return true;
    if (FAILED(hr))
        return Fail("IDXGISwapChain::Present", hr);
    if (!FirstPresentLogged)
    {
        FirstPresentLogged = true;
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Info,
            "DX12 native presentation first Present succeeded size=%ux%u vsync=%d tearing=%d path=GPU-native\n",
            Width,
            Height,
            vsync ? 1 : 0,
            flags != 0 ? 1 : 0);
        std::fflush(stdout);
    }
    melonDS::DX12Perf::MaybeReport();
    return true;
}

bool DX12SurfacePresenter::Fail(const char* operation, HRESULT result)
{
    Error = std::string(operation) + " failed (HRESULT="
        + std::to_string(static_cast<unsigned long>(result)) + ")";
    melonDS::DX12::Fail(operation, result);
    return false;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
