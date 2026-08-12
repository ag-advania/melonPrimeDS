/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_DX12_SURFACE_PRESENTER_H
#define MELONPRIME_DX12_SURFACE_PRESENTER_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <string>

#include "DX12Context.h"
#include "DX12PresentedFrame.h"

namespace MelonPrime
{

// Native, GPU-composited DXGI presentation. The two DS screens stay on the
// shared D3D12 device; only CPU-authored HUD/OSD layers use upload heaps.
class DX12SurfacePresenter
{
public:
    enum class Layer : std::uint32_t
    {
        ScreenTop,
        ScreenBottom,
        Hud,
        Osd,
        Count,
    };

    enum class Blend : std::uint32_t
    {
        Opaque,
        Premultiplied,
    };

    struct Quad
    {
        float Axis[4]{1.0f, 0.0f, 0.0f, 1.0f};
        float Origin[4]{0.0f, 0.0f, 1.0f, 1.0f};
        float UvRect[4]{0.0f, 0.0f, 1.0f, 1.0f};
        float Tint[4]{1.0f, 1.0f, 1.0f, 1.0f};
    };

    DX12SurfacePresenter() = default;
    ~DX12SurfacePresenter();

    DX12SurfacePresenter(const DX12SurfacePresenter&) = delete;
    DX12SurfacePresenter& operator=(const DX12SurfacePresenter&) = delete;

    bool Init(HWND window);
    void Shutdown() noexcept;

    bool BeginFrame(
        std::uint32_t width,
        std::uint32_t height,
        bool waitForPresentSlot = true);
    bool UploadLayer(
        Layer layer,
        const void* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowBytes);
    bool UploadLayerRegion(
        Layer layer,
        const void* pixels,
        std::uint32_t sourceX,
        std::uint32_t sourceY,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowBytes);
    bool UploadLayerFromBuffer(
        Layer layer,
        const melonDS::DX12PresentedFrame& frame,
        std::uint64_t sourceOffset);
    void BeginComposition() noexcept {}
    void DrawLayer(Layer layer, const Quad& quad, Blend blend, bool linearFilter);
    void DrawRadar(
        const Quad& quad,
        float opacity,
        std::uint32_t sourceCenterY,
        std::uint32_t sourceRadius);
    bool EndFrame();

    // Kept separate so Reflex markers bracket the actual DXGI Present call.
    bool Present(bool vsync);

    [[nodiscard]] bool IsInitialized() const noexcept { return Initialized; }
    [[nodiscard]] const std::string& LastError() const noexcept { return Error; }
    [[nodiscard]] std::uint32_t GetWidth() const noexcept { return Width; }
    [[nodiscard]] std::uint32_t GetHeight() const noexcept { return Height; }

private:
    struct LayerTexture
    {
        melonDS::DX12::ComPtr<ID3D12Resource> Texture;
        melonDS::DX12::ComPtr<ID3D12Resource> Upload;
        std::uint8_t* UploadMapped = nullptr;
        std::uint64_t UploadCapacity = 0;
        std::uint32_t UploadRowPitch = 0;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COPY_DEST;
        bool Valid = false;
    };

    bool CreateSwapchain(std::uint32_t width, std::uint32_t height);
    bool CreateGraphicsObjects();
    bool CreatePipeline(bool blended, melonDS::DX12::ComPtr<ID3D12PipelineState>& output);
    bool Resize(std::uint32_t width, std::uint32_t height);
    bool AcquireBackBuffers();
    void CloseFrameLatencyWaitable() noexcept;
    bool EnsureLayerTexture(Layer layer, std::uint32_t width, std::uint32_t height);
    bool EnsureLayerUpload(LayerTexture& layer, std::uint32_t width, std::uint32_t height);
    bool WaitForPresentSlot();
    bool AllocateLayerSrv(Layer layer, D3D12_GPU_DESCRIPTOR_HANDLE& gpu);
    void TransitionLayer(LayerTexture& layer, D3D12_RESOURCE_STATES after);
    void TransitionNativeSource(D3D12_RESOURCE_STATES after);
    bool Fail(const char* operation, HRESULT result);

    melonDS::DX12Context* Context = nullptr;
    HWND Window = nullptr;
    melonDS::DX12CommandContext Commands;
    melonDS::DX12DescriptorRing Descriptors;
    melonDS::DX12::ComPtr<IDXGISwapChain3> Swapchain;
    melonDS::DX12::ComPtr<ID3D12Resource> BackBuffers[2];
    melonDS::DX12::ComPtr<ID3D12DescriptorHeap> RtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE BackBufferRtvs[2]{};
    melonDS::DX12::ComPtr<ID3D12RootSignature> RootSignature;
    melonDS::DX12::ComPtr<ID3D12PipelineState> OpaquePipeline;
    melonDS::DX12::ComPtr<ID3D12PipelineState> BlendedPipeline;
    LayerTexture Layers[static_cast<std::size_t>(Layer::Count)];
    ID3D12GraphicsCommandList* OpenList = nullptr;
    ID3D12Resource* NativeSource = nullptr;
    D3D12_RESOURCE_STATES NativeSourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    HANDLE FrameLatencyWaitable = nullptr;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    UINT RtvIncrement = 0;
    UINT SwapchainFlags = 0;
    bool TearingSupported = false;
    bool Initialized = false;
    bool FrameOpen = false;
    bool FrameReady = false;
    bool FirstPresentLogged = false;
    bool PresentWaitStateLogged = false;
    bool LastPresentWaitEnabled = true;
    bool PresentModeLogged = false;
    bool LastPresentVsync = false;
    bool PresentResultLogged = false;
    HRESULT LastPresentResult = S_OK;
    std::chrono::steady_clock::time_point PerfRecordStart{};
    std::string Error;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // MELONPRIME_DX12_SURFACE_PRESENTER_H
