#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeSapphireGpu2DAdapter requires the Vulkan build gate"
#endif

#include "types.h"

namespace melonDS
{
class GPU;

namespace MelonPrimeSapphireGpu2DAdapter
{

void ForwardRegisterWrite8(GPU& gpu, u32 engineNum, u32 addr, u8 val) noexcept;
void ForwardRegisterWrite16(GPU& gpu, u32 engineNum, u32 addr, u16 val) noexcept;
void ForwardRegisterWrite32(GPU& gpu, u32 engineNum, u32 addr, u32 val) noexcept;
void ForwardWindowCheck(GPU& gpu, u32 line) noexcept;
void ForwardVBlank(GPU& gpu) noexcept;
void ForwardVBlankEnd(GPU& gpu) noexcept;

} // namespace MelonPrimeSapphireGpu2DAdapter
} // namespace melonDS
