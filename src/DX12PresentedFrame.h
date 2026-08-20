/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef DX12_PRESENTED_FRAME_H
#define DX12_PRESENTED_FRAME_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "DX12Common.h"

namespace melonDS
{

// Opaque renderer -> presenter handoff. The fallback resource contains two
// tightly packed BGRA8 screens; when the direct path is available, the
// compositor slot's sampleable RGBA8 texture is the primary output. All
// resources belong to a leased compositor ring slot. Renderer and presenter
// submit to the same process-wide command queue, so queue order supplies the
// GPU dependency and RendererOutputLease supplies CPU lifetime.
struct DX12PresentedFrame
{
    ID3D12Resource* Buffer = nullptr;
    ID3D12Resource* DirectTexture = nullptr;
    u64 TopOffset = 0;
    u64 BottomOffset = 0;
    u32 Width = 0;
    u32 Height = 0;
    // Serial is the published frame sequence. Generation is the structured /
    // composition content generation and may change every DS frame. Keep the
    // resource lifetime generation separate: it changes only when the
    // renderer recreates the compositor output resources.
    u64 Serial = 0;
    u64 Generation = 0;
    u64 Epoch = 0;
    u64 ResourceGeneration = 0;
    // The texture is allocated for the slot even when developer readback
    // selects the fallback composed buffer.  Do not advertise stale direct
    // contents merely because the handle is non-null.
    bool DirectContentValid = false;

    [[nodiscard]] bool HasDirectSampledOutput() const noexcept
    {
        return DirectContentValid && DirectTexture != nullptr;
    }
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_PRESENTED_FRAME_H
