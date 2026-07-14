#pragma once

#include "types.h"
#include "VulkanStructuredControlAbi.inc"

namespace melonDS::VulkanStructuredControlAbi
{

inline constexpr u32 SourceEffectMask = MP_VK_SOURCE_CONTROL_EFFECT_MASK;
inline constexpr u32 SourceEvaShift = MP_VK_SOURCE_CONTROL_EVA_SHIFT;
inline constexpr u32 SourceEvbShift = MP_VK_SOURCE_CONTROL_EVB_SHIFT;
inline constexpr u32 SourceEvyShift = MP_VK_SOURCE_CONTROL_EVY_SHIFT;
inline constexpr u32 SourceDirect3DBit = MP_VK_SOURCE_CONTROL_DIRECT_3D_BIT;
inline constexpr u32 SourceValidBit = MP_VK_SOURCE_CONTROL_VALID_BIT;

inline constexpr u32 PackedEffectMask = MP_VK_PACKED_CONTROL_EFFECT_MASK;
inline constexpr u32 PackedNo3DCoverageFlag = MP_VK_PACKED_CONTROL_NO_3D_COVERAGE_FLAG;
inline constexpr u32 PackedProtectedBlackFlag = MP_VK_PACKED_CONTROL_PROTECTED_BLACK_FLAG;
inline constexpr u32 Packed3DSlotFlag = MP_VK_PACKED_CONTROL_3D_SLOT_FLAG;
inline constexpr u32 PackedAbove3DFlag = MP_VK_PACKED_CONTROL_ABOVE_3D_FLAG;

inline constexpr u32 PixelAlpha(u32 pixel) noexcept { return pixel >> 24u; }
inline constexpr bool Is3DLayer(u32 pixel) noexcept
{
    return (PixelAlpha(pixel) & 0x40u) != 0u;
}
inline constexpr bool IsOpaqueBlack2D(u32 pixel) noexcept
{
    const u32 alpha = PixelAlpha(pixel);
    return (pixel & 0x00FFFFFFu) == 0u
        && alpha != 0u
        && (alpha & 0x40u) == 0u;
}

// GPU2D_Soft produces a compact bit-field. The Sapphire compositor consumes
// an RGBA-byte word: G=EVA/EVY, B=EVB, A=effect plus structured ownership.
inline constexpr u32 ConvertSourceControlToPacked(
    u32 sourceControl,
    bool has3DSlot,
    bool hasAbove3D,
    bool protectedBlack,
    bool no3DCoverage = false) noexcept
{
    if ((sourceControl & SourceValidBit) == 0u)
        return 0u;

    const u32 effect = sourceControl & SourceEffectMask;
    const u32 eva = (sourceControl >> SourceEvaShift) & 0x1Fu;
    const u32 evb = (sourceControl >> SourceEvbShift) & 0x1Fu;
    const u32 evy = (sourceControl >> SourceEvyShift) & 0x1Fu;
    const u32 coefficientG = (effect == 2u || effect == 3u) ? evy : eva;
    const u32 coefficientB = effect == 1u ? evb : 0u;
    const u32 ownership = has3DSlot
        ? (Packed3DSlotFlag | (hasAbove3D ? PackedAbove3DFlag : 0u))
        : PackedAbove3DFlag;
    const u32 alpha = effect
        | ownership
        | (protectedBlack ? PackedProtectedBlackFlag : 0u)
        | (no3DCoverage ? PackedNo3DCoverageFlag : 0u);
    return (coefficientG << 8u) | (coefficientB << 16u) | (alpha << 24u);
}

static_assert(ConvertSourceControlToPacked(SourceValidBit, false, false, false)
    == 0x80000000u);
static_assert(ConvertSourceControlToPacked(
    SourceValidBit | 1u | (8u << SourceEvaShift) | (12u << SourceEvbShift),
    true, false, false) == 0x410C0800u);

} // namespace melonDS::VulkanStructuredControlAbi
