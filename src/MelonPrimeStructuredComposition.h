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

#ifndef MELONPRIME_STRUCTURED_COMPOSITION_H
#define MELONPRIME_STRUCTURED_COMPOSITION_H

#ifdef MELONPRIME_DS

#include "types.h"

// Canonical bit layout of the structured 2D composition contract.
//
// The software 2D renderer deliberately does not flatten the 3D layer into its
// output when a hardware 3D renderer is active. It instead publishes, per
// screen, three 256x192 planes plus one metadata word per scanline. A hardware
// compositor re-inserts its own internal-resolution 3D image at the marked
// slots. This is what lets DX12 and Vulkan show high-resolution 3D while the DS
// 2D layer/priority/window/mosaic rules stay exactly as the software renderer
// resolved them.
//
// Producer:  melonDS::SoftRenderer (src/GPU_Soft.cpp).
// Consumers: DX12Shaders::Compositor  (src/GPU3D_DX12_shaders.h)
//            MelonPrimeVulkanCompositorShader.comp
//
// Both consumers must agree with the producer bit-for-bit, so the numeric
// values live here and tools/ci/audits/audit-structured-composition-contract.py
// fails the build if any of the three files drifts.
namespace melonDS::StructuredComposition
{

// ---------------------------------------------------------------------------
// Plane layout
// ---------------------------------------------------------------------------
// Plane 0 ("below"): the 2D pixel underneath the 3D slot, or the final composed
//                    2D pixel when the line carries no 3D at all.
// Plane 1 ("above"): the 2D pixel layered on top of the 3D slot. Only meaningful
//                    when kControlAbovePlane is set.
// Plane 2 ("control"): the flags and blend coefficients described below.
inline constexpr u32 kPlaneBelow = 0;
inline constexpr u32 kPlaneAbove = 1;
inline constexpr u32 kPlaneControl = 2;
inline constexpr u32 kPlaneCount = 3;

inline constexpr u32 kScreenWidth = 256;
inline constexpr u32 kScreenHeight = 192;
inline constexpr u32 kScreenPixelCount = kScreenWidth * kScreenHeight;

// ---------------------------------------------------------------------------
// Control word (plane 2)
// ---------------------------------------------------------------------------
// Colors in every plane use the software renderer's internal 6-bit encoding
// (r6 | g6<<8 | b6<<16 | flags<<24), so the control word reuses the same field
// positions: its "alpha" byte carries the flags and its red/green bytes carry
// the blend coefficients.
//
//   bits 31..24  flag byte (see below)
//   bits 23..16  EVB - the second blend coefficient
//   bits 15..8   EVA - the first blend coefficient, also the brightness factor
//   bits  7..0   unused
inline constexpr u32 kControlFlagShift = 24;
inline constexpr u32 kControlFlagMask = 0xFFu;
inline constexpr u32 kControlEvbShift = 16;
inline constexpr u32 kControlEvaShift = 8;
inline constexpr u32 kControlBlendFactorMask = 0x1Fu;

// Low nibble of the flag byte: which DS blend rule joins the 3D slot with the
// surrounding 2D. These match GPU_ColorOp.h / SoftRenderer2D's own numbering.
//   0  replace                       - the 3D pixel is used as-is
//   1  ColorBlend4(above, 3D, eva, evb)
//   2  ColorBrightnessUp(3D, eva)
//   3  ColorBrightnessDown(3D, eva)
//   4  ColorBlend5(3D, below)        - per-pixel 3D alpha
//   7  no 3D contribution on this pixel
inline constexpr u32 kControlCompositionModeMask = 0x0Fu;
inline constexpr u32 kCompositionModeReplace = 0;
inline constexpr u32 kCompositionModeBlend4 = 1;
inline constexpr u32 kCompositionModeBrightnessUp = 2;
inline constexpr u32 kCompositionModeBrightnessDown = 3;
inline constexpr u32 kCompositionModeBlend5 = 4;
inline constexpr u32 kCompositionModePlain2D = 7;

// The pixel underneath the 3D slot is opaque black. Recorded at the producer
// because a compositor cannot tell "the 2D layer really is black here" from
// "nothing was written here" once the color has been packed.
inline constexpr u32 kControlOpaqueBlackBelow = 0x20u;
// This pixel has a 3D slot: the compositor must sample its own 3D image here.
// Without this flag plane 0 is already the final 2D color.
inline constexpr u32 kControlHas3DSlot = 0x40u;
// Plane 1 holds a real 2D pixel above the 3D slot.
inline constexpr u32 kControlAbovePlane = 0x80u;

// The producer's default for a pixel that carries no 3D at all.
inline constexpr u32 kControlPlain2D =
    kControlAbovePlane | kCompositionModePlain2D;

// ---------------------------------------------------------------------------
// Line metadata (one word per scanline per screen)
// ---------------------------------------------------------------------------
//   bits  4..0   master brightness factor, already clamped to 0..16
//   bits  9..8   master brightness mode (0 none, 1 up, 2 down)
//   bits 17..16  DS display mode (0 off, 1 regular, 2 VRAM, 3 FIFO)
//   bit  21      this line's 2D read back a display capture that contained 3D
//   bit  22      this VRAM-display line's capture block contained 3D
//   bits 31..23  GPU3D::RenderXPos as it stood while this scanline was drawn
//
// Bits 21 and 22 are provenance only. They exist for diagnostics and for
// display-capture work; a compositor must never branch its per-pixel output on
// them, because the per-pixel control word already says whether a 3D slot
// exists. Deciding "this screen probably needs 3D" from line counts is exactly
// the scene-shape guessing this contract replaced.
//
// The 3D X scroll is per scanline because that is where the DS applies it:
// SoftRenderer3D::GetLine() reads RenderXPos when the line is fetched, and
// OpenGL Compute keeps uSrcAOffset[line] for display capture. A frame-global
// value would smear a mid-frame scroll change across all 192 lines.
inline constexpr u32 kLineMetaBrightnessFactorMask = 0x1Fu;
inline constexpr u32 kLineMetaBrightnessModeShift = 8;
inline constexpr u32 kLineMetaBrightnessModeMask = 0x3u;
inline constexpr u32 kLineMetaDisplayModeShift = 16;
inline constexpr u32 kLineMetaDisplayModeMask = 0x3u;
inline constexpr u32 kLineMetaRegularCaptureUses3D = 1u << 21;
inline constexpr u32 kLineMetaVramCaptureUses3D = 1u << 22;
inline constexpr u32 kLineMetaRenderXPosShift = 23;
inline constexpr u32 kLineMetaRenderXPosMask = 0x1FFu;

inline constexpr u32 kBrightnessModeNone = 0;
inline constexpr u32 kBrightnessModeUp = 1;
inline constexpr u32 kBrightnessModeDown = 2;
inline constexpr u32 kBrightnessFactorLimit = 16;

inline constexpr u32 kDisplayModeOff = 0;
inline constexpr u32 kDisplayModeRegular = 1;
inline constexpr u32 kDisplayModeVram = 2;
inline constexpr u32 kDisplayModeFifo = 3;

// ---------------------------------------------------------------------------
// Raw pixel markers pushed back into the 2D BG/OBJ pipeline
// ---------------------------------------------------------------------------
// The software 2D engines consume a "3D line" whose pixels carry these markers
// instead of real color, so that their existing priority, window and blending
// logic decides where the 3D layer lands without ever seeing 3D color.
inline constexpr u32 k3DPlaceholderPixel = 0x20000000u;
inline constexpr u32 k3DLayerSlotPixel = 0x40000000u;

} // namespace melonDS::StructuredComposition

#endif // MELONPRIME_DS

#endif // MELONPRIME_STRUCTURED_COMPOSITION_H
