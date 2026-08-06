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
#include <string>

#include "DX12Context.h"

namespace MelonPrime
{

// Native DXGI presentation for the DX12 renderer. Frame composition remains
// shared with the Qt panels; this class owns only the final D3D12 upload,
// flip-model swapchain and real IDXGISwapChain::Present boundary.
class DX12SurfacePresenter
{
public:
    DX12SurfacePresenter() = default;
    ~DX12SurfacePresenter();

    DX12SurfacePresenter(const DX12SurfacePresenter&) = delete;
    DX12SurfacePresenter& operator=(const DX12SurfacePresenter&) = delete;

    bool Init(HWND window);
    void Shutdown() noexcept;

    // Uploads one tightly packed BGRA8 frame and submits the copy. Present()
    // is deliberately separate so the Reflex Present markers can bracket the
    // actual DXGI call rather than CPU composition or command recording.
    bool UploadFrame(
        const void* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowBytes,
        bool vsync);
    bool Present(bool vsync);

    [[nodiscard]] bool IsInitialized() const noexcept { return Initialized; }
    [[nodiscard]] const std::string& LastError() const noexcept { return Error; }

private:
    bool CreateSwapchain(std::uint32_t width, std::uint32_t height);
    bool Resize(std::uint32_t width, std::uint32_t height);
    bool AcquireBackBuffers();
    bool EnsureUpload(std::uint32_t width, std::uint32_t height);
    bool WaitForPresentSlot();
    bool Fail(const char* operation, HRESULT result);

    melonDS::DX12Context* Context = nullptr;
    HWND Window = nullptr;
    melonDS::DX12CommandContext Commands;
    melonDS::DX12::ComPtr<IDXGISwapChain3> Swapchain;
    melonDS::DX12::ComPtr<ID3D12Resource> BackBuffers[2];
    melonDS::DX12::ComPtr<ID3D12Resource> Upload;
    std::uint8_t* UploadMapped = nullptr;
    HANDLE FrameLatencyWaitable = nullptr; // Owned by DXGI; never CloseHandle().
    std::uint64_t UploadCapacity = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t UploadRowPitch = 0;
    UINT SwapchainFlags = 0;
    bool TearingSupported = false;
    bool Initialized = false;
    bool FrameReady = false;
    bool FirstPresentLogged = false;
    std::string Error;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // MELONPRIME_DX12_SURFACE_PRESENTER_H
