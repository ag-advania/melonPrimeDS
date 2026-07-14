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

#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <array>
#include <cstddef>

#include "types.h"
#include "VulkanStructuredControlAbi.h"

namespace melonDS
{

// MELONPRIME_SAPPHIRE_VULKAN_STRUCTURED_2D_R17
// One engine scanline emitted by the existing software GPU2D evaluator.
// This is CPU-produced DS layer metadata, never a GPU-to-CPU readback path.
struct SapphireStructured2DLine
{
    const u32* Plane0 = nullptr;
    const u32* Plane1 = nullptr;
    const u32* Control = nullptr;
    // Native 256-wide engine output is retained only for DS display modes that
    // have no structured BG/OBJ pair (VRAM/FIFO/off/forced blank).
    const u32* NativeFinal = nullptr;
    u32 Line = 0;
    u32 Engine = 0;
    u32 DispCnt = 0;
    u16 MasterBrightness = 0;
    bool EngineEnabled = false;
    bool ForcedBlank = false;
    bool ScreensEnabled = false;
};

// Immutable complete output of the software GPU2D layer evaluator. Arrays
// remain in engine A/B order; physical top/bottom mapping is applied only by
// the frontend after the complete-frame ScreenSwap latch is available.
struct SapphireStructured2DFrameSnapshot
{
    static constexpr std::size_t PixelCount = 256u * 192u;
    static constexpr std::size_t LineCount = 192u;
    static constexpr std::size_t EngineCount = 2u;

    u64 FrameSerial = 0;
    u64 Generation = 0;
    bool ScreenSwap = false;
    bool Complete = false;
    std::array<u32, EngineCount * PixelCount> Plane0{};
    std::array<u32, EngineCount * PixelCount> Plane1{};
    std::array<u32, EngineCount * PixelCount> Control{};
    std::array<u32, EngineCount * PixelCount> NativeFinal{};
    std::array<u32, EngineCount * LineCount> LineMeta{};
    std::array<u32, EngineCount * LineCount> LineState{};
};

inline constexpr u32 SapphireStructured2DControlEffectMask = VulkanStructuredControlAbi::SourceEffectMask;
inline constexpr u32 SapphireStructured2DControlEvaShift = VulkanStructuredControlAbi::SourceEvaShift;
inline constexpr u32 SapphireStructured2DControlEvbShift = VulkanStructuredControlAbi::SourceEvbShift;
inline constexpr u32 SapphireStructured2DControlEvyShift = VulkanStructuredControlAbi::SourceEvyShift;
inline constexpr u32 SapphireStructured2DControlDirect3DBit = VulkanStructuredControlAbi::SourceDirect3DBit;
inline constexpr u32 SapphireStructured2DControlValidBit = VulkanStructuredControlAbi::SourceValidBit;

} // namespace melonDS

#endif
