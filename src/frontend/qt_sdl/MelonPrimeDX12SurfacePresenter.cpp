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

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "Platform.h"

namespace MelonPrime
{
namespace
{
constexpr UINT kBufferCount = 2;

std::uint32_t AlignTexturePitch(std::uint32_t bytes) noexcept
{
    return (bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
}
} // namespace

DX12SurfacePresenter::~DX12SurfacePresenter()
{
    Shutdown();
}

bool DX12SurfacePresenter::Init(HWND window)
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
    if (!Commands.Init(Context->GetDevice(), Context->GetQueue()))
    {
        Error = "DX12 presentation command context creation failed";
        Shutdown();
        return false;
    }

    RECT client{};
    GetClientRect(Window, &client);
    const std::uint32_t width = std::max<LONG>(1, client.right - client.left);
    const std::uint32_t height = std::max<LONG>(1, client.bottom - client.top);
    if (!CreateSwapchain(width, height))
    {
        Shutdown();
        return false;
    }

    Initialized = true;
    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Info,
        "DX12 native presentation initialized path=DXGI-flip-discard buffers=%u tearing=%d\n",
        kBufferCount,
        TearingSupported ? 1 : 0);
    return true;
}

void DX12SurfacePresenter::Shutdown() noexcept
{
    if (Context)
        Commands.WaitQueueIdle();
    if (Upload && UploadMapped)
    {
        D3D12_RANGE noWrite{0, 0};
        Upload->Unmap(0, &noWrite);
    }
    UploadMapped = nullptr;
    Upload.Reset();
    for (auto& buffer : BackBuffers)
        buffer.Reset();
    Swapchain.Reset();
    Commands.Shutdown();
    FrameLatencyWaitable = nullptr;
    UploadCapacity = 0;
    UploadRowPitch = 0;
    Width = 0;
    Height = 0;
    SwapchainFlags = 0;
    TearingSupported = false;
    Initialized = false;
    FrameReady = false;
    FirstPresentLogged = false;
    Window = nullptr;
    if (Context)
    {
        Context->Release();
        Context = nullptr;
    }
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
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing,
                sizeof(allowTearing))))
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
        Context->GetQueue(),
        Window,
        &desc,
        nullptr,
        nullptr,
        baseSwapchain.ReleaseAndGetAddressOf());
    if (FAILED(hr))
        return Fail("CreateSwapChainForHwnd", hr);
    hr = baseSwapchain->QueryInterface(IID_PPV_ARGS(Swapchain.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return Fail("IDXGISwapChain3", hr);

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
    return AcquireBackBuffers();
}

bool DX12SurfacePresenter::AcquireBackBuffers()
{
    for (UINT index = 0; index < kBufferCount; ++index)
    {
        HRESULT hr = Swapchain->GetBuffer(
            index,
            IID_PPV_ARGS(BackBuffers[index].ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            return Fail("IDXGISwapChain::GetBuffer", hr);
    }
    return true;
}

bool DX12SurfacePresenter::Resize(std::uint32_t width, std::uint32_t height)
{
    if (width == Width && height == Height)
        return true;
    if (!Swapchain || width == 0 || height == 0)
        return false;

    // Present is queued after UploadFrame()'s command-list fence. Insert a
    // new queue fence before releasing the old swap-chain buffers; waiting
    // only for the upload fence leaves those buffers live on the display
    // queue and triggers the D3D12 debug layer's final-release corruption
    // break during resize or renderer switching.
    if (!Commands.WaitQueueIdle())
    {
        Error = "DX12 presentation queue did not become idle before resize";
        return false;
    }
    for (auto& buffer : BackBuffers)
        buffer.Reset();
    FrameReady = false;

    const HRESULT hr = Swapchain->ResizeBuffers(
        kBufferCount,
        width,
        height,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        SwapchainFlags);
    if (FAILED(hr))
        return Fail("IDXGISwapChain::ResizeBuffers", hr);

    Width = width;
    Height = height;
    FrameLatencyWaitable = Swapchain->GetFrameLatencyWaitableObject();
    return FrameLatencyWaitable && AcquireBackBuffers();
}

bool DX12SurfacePresenter::EnsureUpload(std::uint32_t width, std::uint32_t height)
{
    const std::uint32_t rowPitch = AlignTexturePitch(width * sizeof(std::uint32_t));
    const std::uint64_t required = static_cast<std::uint64_t>(rowPitch) * height;
    if (Upload && UploadMapped && UploadCapacity >= required && UploadRowPitch == rowPitch)
        return true;

    Commands.WaitIdle();
    if (Upload && UploadMapped)
    {
        D3D12_RANGE noWrite{0, 0};
        Upload->Unmap(0, &noWrite);
    }
    UploadMapped = nullptr;
    Upload.Reset();

    Upload = Context->CreateBuffer(
        required,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 native presentation upload");
    if (!Upload)
    {
        Error = "DX12 presentation upload allocation failed";
        return false;
    }

    D3D12_RANGE noRead{0, 0};
    const HRESULT hr = Upload->Map(0, &noRead, reinterpret_cast<void**>(&UploadMapped));
    if (FAILED(hr) || !UploadMapped)
        return Fail("DX12 presentation upload map", hr);
    UploadCapacity = required;
    UploadRowPitch = rowPitch;
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

bool DX12SurfacePresenter::UploadFrame(
    const void* pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t rowBytes,
    bool vsync)
{
    (void)vsync;
    if (!Initialized || !pixels || width == 0 || height == 0)
        return false;
    if (rowBytes < static_cast<std::size_t>(width) * sizeof(std::uint32_t))
    {
        Error = "DX12 presentation received an invalid source row pitch";
        return false;
    }
    if (!WaitForPresentSlot() || !Resize(width, height) || !EnsureUpload(width, height))
        return false;

    const auto* source = static_cast<const std::uint8_t*>(pixels);
    const std::size_t copyBytes = static_cast<std::size_t>(width) * sizeof(std::uint32_t);
    for (std::uint32_t row = 0; row < height; ++row)
    {
        std::memcpy(
            UploadMapped + static_cast<std::size_t>(row) * UploadRowPitch,
            source + static_cast<std::size_t>(row) * rowBytes,
            copyBytes);
    }

    ID3D12GraphicsCommandList* list = Commands.Begin();
    if (!list)
    {
        Error = "DX12 presentation command list could not begin";
        return false;
    }

    ID3D12Resource* backBuffer = BackBuffers[Swapchain->GetCurrentBackBufferIndex()].Get();
    D3D12_RESOURCE_BARRIER toCopy{};
    toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy.Transition.pResource = backBuffer;
    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    list->ResourceBarrier(1, &toCopy);

    D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
    sourceLocation.pResource = Upload.Get();
    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sourceLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sourceLocation.PlacedFootprint.Footprint.Width = width;
    sourceLocation.PlacedFootprint.Footprint.Height = height;
    sourceLocation.PlacedFootprint.Footprint.Depth = 1;
    sourceLocation.PlacedFootprint.Footprint.RowPitch = UploadRowPitch;

    D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
    destinationLocation.pResource = backBuffer;
    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destinationLocation.SubresourceIndex = 0;
    list->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);

    std::swap(toCopy.Transition.StateBefore, toCopy.Transition.StateAfter);
    list->ResourceBarrier(1, &toCopy);
    if (!Commands.Submit())
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
    const HRESULT hr = Swapchain->Present(syncInterval, flags);
    FrameReady = false;
    if (hr == DXGI_STATUS_OCCLUDED)
        return true;
    if (FAILED(hr))
        return Fail("IDXGISwapChain::Present", hr);
    if (!FirstPresentLogged)
    {
        FirstPresentLogged = true;
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Info,
            "DX12 native presentation first Present succeeded size=%ux%u vsync=%d tearing=%d\n",
            Width,
            Height,
            vsync ? 1 : 0,
            flags != 0 ? 1 : 0);
        // This is a one-time diagnostics boundary. Flush it so redirected CI
        // and local smoke-test logs prove that the real DXGI call completed
        // even when the GUI process is terminated by a test harness.
        std::fflush(stdout);
    }
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
