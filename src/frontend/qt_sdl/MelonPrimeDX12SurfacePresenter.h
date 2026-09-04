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

#include <array>
#include <cstddef>
#include <cstdint>
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
#include <chrono>
#endif
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

    bool Init(
        HWND window,
        std::uint64_t surfaceGeneration = 0,
        std::uint32_t initialWidth = 0,
        std::uint32_t initialHeight = 0);
    void Shutdown() noexcept;

    // Renderer-transition boundary. Wait for both presenter submissions and
    // DXGI work on the shared queue before descriptor identity or renderer
    // output leases are released.
    void Quiesce() noexcept;

    // The DXGI frame-latency wait is presentation pacing, so the presenter
    // asks the low-latency controller whether the active vendor authority
    // already performs an equivalent wait. Callers no longer decide.
    bool BeginFrame(std::uint32_t width, std::uint32_t height);
    // The screen panel supplies the renderer-owned identity before presenter
    // admission. A newer epoch may restart the serial sequence; within one
    // epoch, serials must never move backwards.
    void SetPresentedFrameIdentity(
        std::uint64_t serial, std::uint64_t epoch) noexcept
    {
        PendingPresentedFrameSerial = serial;
        PendingPresentedFrameEpoch = epoch;
    }
    [[nodiscard]] bool IsPresentedFrameIdentityMonotonic() const noexcept
    {
        if (PendingPresentedFrameSerial == 0)
            return true;
        return PendingPresentedFrameEpoch > LastPresentedFrameEpoch
            || (PendingPresentedFrameEpoch == LastPresentedFrameEpoch
                && (LastPresentedFrameSerial == 0
                    || PendingPresentedFrameSerial >= LastPresentedFrameSerial));
    }
    // Must run after acquiring the renderer output lease and before BeginFrame
    // opens the presenter command list. Resource-generation changes are the
    // only cold path allowed to quiesce the shared queue for descriptor reuse.
    bool PrepareDirectOutputDescriptors(const melonDS::DX12PresentedFrame& frame);
    // Renderer transition/destruction hook. It clears identity only; the
    // persistent heap prefix remains allocated for the next output generation.
    void InvalidateDirectDescriptorCache() noexcept;
    bool UploadLayer(
        Layer layer,
        const void* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowBytes);
    // Replaces the layer texture with the given sub-rectangle of the source
    // image: the texture is (re)created at width x height and holds only that
    // patch. Use it when the layer *is* the patch; use UploadLayerRegions()
    // when the layer is a retained full-surface image being updated in place.
    bool UploadLayerRegion(
        Layer layer,
        const void* pixels,
        std::uint32_t sourceX,
        std::uint32_t sourceY,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowBytes);

    // A rectangle of a layer, in layer texels.
    struct LayerRegion
    {
        std::uint32_t X = 0;
        std::uint32_t Y = 0;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
    };

    // Upper bound on the regions one UploadLayerRegions() call will stage. It
    // matches the HUD's dirty-region cap; more rectangles than this stop paying
    // for themselves against the copies they save.
    static constexpr std::uint32_t kMaxLayerRegions = 8;

    // Update sub-rectangles of a *retained, full-surface* layer in place.
    //
    // The layer texture is created once at imageWidth x imageHeight and kept
    // across frames, so a steady frame costs no resource creation, no SRV
    // rebuild and no full-surface copy -- only the rows inside `regions` are
    // staged and copied. This is the same contract the Vulkan presenter offers,
    // and it is what lets a HUD that changed one number cost one small copy
    // instead of a texture re-creation plus a full bounding-box upload.
    //
    // Returns false without recording any GPU work when the layer has no
    // content yet (seed it with UploadLayer() first) or when the staging buffer
    // cannot hold the requested regions; the caller falls back to a full upload.
    bool UploadLayerRegions(
        Layer layer,
        const void* pixels,
        std::uint32_t imageWidth,
        std::uint32_t imageHeight,
        std::size_t rowBytes,
        const LayerRegion* regions,
        std::uint32_t regionCount);

    // Dimensions the layer texture currently holds. A caller that keeps a
    // full-surface layer uses this to notice a resize.
    [[nodiscard]] bool LayerMatchesSize(
        Layer layer, std::uint32_t width, std::uint32_t height) const noexcept
    {
        const LayerTexture& texture = Layers[static_cast<std::size_t>(layer)];
        return texture.Valid && texture.Width == width && texture.Height == height;
    }
    bool UploadLayerFromBuffer(
        Layer layer,
        const melonDS::DX12PresentedFrame& frame,
        std::uint64_t sourceOffset);
    // Binds one slice of the compositor's sampleable texture directly. The
    // resource is already in PIXEL_SHADER_RESOURCE state and is retained by
    // the caller's renderer output lease.
    bool UploadLayerFromTexture(
        Layer layer,
        const melonDS::DX12PresentedFrame& frame);
    void BeginComposition() noexcept {}
    void DrawLayer(Layer layer, const Quad& quad, Blend blend, bool linearFilter);
    void DrawRadar(
        const Quad& quad,
        float opacity,
        std::uint32_t sourceCenterY,
        std::uint32_t sourceRadius);
    bool EndFrame();

    // Kept separate from EndFrame() so the vendor Present markers this
    // function fires bracket the DXGI Present alone.
    bool Present(bool vsync);

    [[nodiscard]] bool IsInitialized() const noexcept { return Initialized; }
    [[nodiscard]] const std::string& LastError() const noexcept { return Error; }
    [[nodiscard]] bool LastBeginWasBackpressure() const noexcept
    {
        return LastBeginBackpressure;
    }
    [[nodiscard]] std::uint64_t GetSurfaceGeneration() const noexcept
    {
        return SurfaceGeneration;
    }
    [[nodiscard]] std::uint64_t GetSwapchainGeneration() const noexcept
    {
        return SwapchainGeneration;
    }
    [[nodiscard]] std::uint32_t GetWidth() const noexcept { return Width; }
    [[nodiscard]] std::uint32_t GetHeight() const noexcept { return Height; }
    [[nodiscard]] bool HasLayerContent(Layer layer) const noexcept
    {
        return Layers[static_cast<std::size_t>(layer)].Valid;
    }

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
        ID3D12Resource* DirectTexture = nullptr;
        std::uint32_t DirectArraySlice = 0;
        std::uint64_t DirectResourceGeneration = 0;
        std::uint32_t PersistentDescriptorIndex = 0;
        ID3D12Resource* PersistentSrvResource = nullptr;
        D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COPY_DEST;
        bool Valid = false;
        bool PersistentSrvValid = false;
        bool UsesDirect = false;
    };

    struct DirectSrvCacheEntry
    {
        ID3D12Resource* Resource = nullptr;
        std::uint32_t ArraySlice = 0;
        std::uint64_t ResourceGeneration = 0;
        std::uint32_t DescriptorIndex = 0;
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
    bool CreateLayerPersistentSrv(Layer layer);
    bool EnsureDirectSrv(
        ID3D12Resource* resource,
        std::uint32_t arraySlice,
        std::uint64_t resourceGeneration);
    const DirectSrvCacheEntry* FindDirectSrv(
        ID3D12Resource* resource,
        std::uint32_t arraySlice,
        std::uint64_t resourceGeneration) const noexcept;
    bool ResolveLayerSrv(Layer layer, D3D12_GPU_DESCRIPTOR_HANDLE& gpu);
    bool WaitForPresentSlot();
    bool ApplySdrColorSpace();
    bool AllocateLayerSrv(Layer layer, D3D12_GPU_DESCRIPTOR_HANDLE& gpu);
    D3D12_CPU_DESCRIPTOR_HANDLE PersistentCpuAt(std::uint32_t index) const noexcept;
    D3D12_GPU_DESCRIPTOR_HANDLE PersistentGpuAt(std::uint32_t index) const noexcept;
    void TransitionLayer(LayerTexture& layer, D3D12_RESOURCE_STATES after);
    void TransitionNativeSource(D3D12_RESOURCE_STATES after);
    bool Fail(const char* operation, HRESULT result);

    melonDS::DX12Context* Context = nullptr;
    HWND Window = nullptr;
    std::uint64_t SurfaceGeneration = 0;
    std::uint64_t SwapchainGeneration = 0;
    melonDS::DX12CommandContext Commands;
    melonDS::DX12DescriptorRing Descriptors;
    // The compositor has a three-slot output ring and each direct texture is a
    // two-slice array, so six direct SRVs cover one output resource generation.
    // Slots 0..3 are the fixed fallback layers; slots 4..9 are this cache.
    static constexpr std::uint32_t kDirectCompositorSlotCount = 3;
    static constexpr std::uint32_t kDirectArraySliceCount = 2;
    static constexpr std::uint32_t kDirectDescriptorCacheCount =
        kDirectCompositorSlotCount * kDirectArraySliceCount;
    static constexpr std::uint32_t kPersistentFallbackDescriptorCount =
        static_cast<std::uint32_t>(Layer::Count);
    static constexpr std::uint32_t kPersistentDescriptorCount =
        kPersistentFallbackDescriptorCount + kDirectDescriptorCacheCount;
    static constexpr std::uint32_t kDescriptorHeapCount = 64;
    static_assert(kDirectDescriptorCacheCount == 6);
    D3D12_CPU_DESCRIPTOR_HANDLE PersistentCpuBase{};
    D3D12_GPU_DESCRIPTOR_HANDLE PersistentGpuBase{};
    std::uint32_t PersistentDescriptorCount = kPersistentDescriptorCount;
    std::array<DirectSrvCacheEntry, kDirectDescriptorCacheCount> DirectSrvCache{};
    std::uint64_t CurrentDirectResourceGeneration = 0;
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
    bool NativeSourceDirect = false;
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
    bool LastBeginBackpressure = false;
    bool PresentModeLogged = false;
    bool LastPresentVsync = false;
    bool PresentResultLogged = false;
    HRESULT LastPresentResult = S_OK;
    std::uint64_t PendingPresentedFrameSerial = 0;
    std::uint64_t PendingPresentedFrameEpoch = 0;
    std::uint64_t LastPresentedFrameSerial = 0;
    std::uint64_t LastPresentedFrameEpoch = 0;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    std::chrono::steady_clock::time_point PerfRecordStart{};
#endif
    std::string Error;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // MELONPRIME_DX12_SURFACE_PRESENTER_H
