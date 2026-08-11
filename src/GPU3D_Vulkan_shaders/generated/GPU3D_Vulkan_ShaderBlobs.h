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

/*
    GENERATED FILE -- DO NOT EDIT.

    Produced by tools/vulkan/compile-shaders.py from the GLSL sources in
    src/GPU3D_Vulkan_shaders/. Edit the .comp/.glsl sources and rerun the
    generator; commit source and generated output together.

    Holds every compute pipeline of the Vulkan rasterizer, compiled for
    SPIR-V 1.3 (vulkan1.1), for each of the three tile-geometry buckets.
    Pipeline indices match ComputeRenderer3D::ShaderCompileStep() in
    src/GPU3D_Compute.cpp one for one.

    The SPIR-V words live in per-stage translation units rather than in the
    header so that including the public header stays cheap: the full module set
    is a few megabytes of integer literals and would otherwise be re-parsed by
    every consumer.
*/

#ifndef GPU3D_VULKAN_SHADERBLOBS_H
#define GPU3D_VULKAN_SHADERBLOBS_H

#include <cstdint>

// Internal to the generated shader module set. Consumers should include
// GPU3D_Vulkan_ShaderModules.h instead.

namespace melonDS
{
namespace VulkanShaders
{
namespace detail
{

extern const uint32_t Blob000[14362];
inline constexpr uint32_t Blob000_Words = 14362u;
extern const uint32_t Blob001[14146];
inline constexpr uint32_t Blob001_Words = 14146u;
extern const uint32_t Blob002[4800];
inline constexpr uint32_t Blob002_Words = 4800u;
extern const uint32_t Blob003[6189];
inline constexpr uint32_t Blob003_Words = 6189u;
extern const uint32_t Blob004[6189];
inline constexpr uint32_t Blob004_Words = 6189u;
extern const uint32_t Blob005[9310];
inline constexpr uint32_t Blob005_Words = 9310u;
extern const uint32_t Blob006[9102];
inline constexpr uint32_t Blob006_Words = 9102u;
extern const uint32_t Blob007[9397];
inline constexpr uint32_t Blob007_Words = 9397u;
extern const uint32_t Blob008[9189];
inline constexpr uint32_t Blob008_Words = 9189u;
extern const uint32_t Blob009[9471];
inline constexpr uint32_t Blob009_Words = 9471u;
extern const uint32_t Blob010[9263];
inline constexpr uint32_t Blob010_Words = 9263u;
extern const uint32_t Blob011[11007];
inline constexpr uint32_t Blob011_Words = 11007u;
extern const uint32_t Blob012[10799];
inline constexpr uint32_t Blob012_Words = 10799u;
extern const uint32_t Blob013[10869];
inline constexpr uint32_t Blob013_Words = 10869u;
extern const uint32_t Blob014[10661];
inline constexpr uint32_t Blob014_Words = 10661u;
extern const uint32_t Blob015[10956];
inline constexpr uint32_t Blob015_Words = 10956u;
extern const uint32_t Blob016[10748];
inline constexpr uint32_t Blob016_Words = 10748u;
extern const uint32_t Blob017[11030];
inline constexpr uint32_t Blob017_Words = 11030u;
extern const uint32_t Blob018[10822];
inline constexpr uint32_t Blob018_Words = 10822u;
extern const uint32_t Blob019[9044];
inline constexpr uint32_t Blob019_Words = 9044u;
extern const uint32_t Blob020[8836];
inline constexpr uint32_t Blob020_Words = 8836u;
extern const uint32_t Blob021[812];
inline constexpr uint32_t Blob021_Words = 812u;
extern const uint32_t Blob022[763];
inline constexpr uint32_t Blob022_Words = 763u;
extern const uint32_t Blob023[1045];
inline constexpr uint32_t Blob023_Words = 1045u;
extern const uint32_t Blob024[1301];
inline constexpr uint32_t Blob024_Words = 1301u;
extern const uint32_t Blob025[1140];
inline constexpr uint32_t Blob025_Words = 1140u;
extern const uint32_t Blob026[2070];
inline constexpr uint32_t Blob026_Words = 2070u;
extern const uint32_t Blob027[2019];
inline constexpr uint32_t Blob027_Words = 2019u;
extern const uint32_t Blob028[2927];
inline constexpr uint32_t Blob028_Words = 2927u;
extern const uint32_t Blob029[1649];
inline constexpr uint32_t Blob029_Words = 1649u;
extern const uint32_t Blob030[2569];
inline constexpr uint32_t Blob030_Words = 2569u;
extern const uint32_t Blob031[2522];
inline constexpr uint32_t Blob031_Words = 2522u;
extern const uint32_t Blob032[3422];
inline constexpr uint32_t Blob032_Words = 3422u;
extern const uint32_t Blob033[1401];
inline constexpr uint32_t Blob033_Words = 1401u;
extern const uint32_t Blob034[6251];
inline constexpr uint32_t Blob034_Words = 6251u;
extern const uint32_t Blob035[5128];
inline constexpr uint32_t Blob035_Words = 5128u;
extern const uint32_t Blob036[14362];
inline constexpr uint32_t Blob036_Words = 14362u;
extern const uint32_t Blob037[14146];
inline constexpr uint32_t Blob037_Words = 14146u;
extern const uint32_t Blob038[4808];
inline constexpr uint32_t Blob038_Words = 4808u;
extern const uint32_t Blob039[6193];
inline constexpr uint32_t Blob039_Words = 6193u;
extern const uint32_t Blob040[6193];
inline constexpr uint32_t Blob040_Words = 6193u;
extern const uint32_t Blob041[9310];
inline constexpr uint32_t Blob041_Words = 9310u;
extern const uint32_t Blob042[9102];
inline constexpr uint32_t Blob042_Words = 9102u;
extern const uint32_t Blob043[9397];
inline constexpr uint32_t Blob043_Words = 9397u;
extern const uint32_t Blob044[9189];
inline constexpr uint32_t Blob044_Words = 9189u;
extern const uint32_t Blob045[9471];
inline constexpr uint32_t Blob045_Words = 9471u;
extern const uint32_t Blob046[9263];
inline constexpr uint32_t Blob046_Words = 9263u;
extern const uint32_t Blob047[11003];
inline constexpr uint32_t Blob047_Words = 11003u;
extern const uint32_t Blob048[10795];
inline constexpr uint32_t Blob048_Words = 10795u;
extern const uint32_t Blob049[10865];
inline constexpr uint32_t Blob049_Words = 10865u;
extern const uint32_t Blob050[10657];
inline constexpr uint32_t Blob050_Words = 10657u;
extern const uint32_t Blob051[10952];
inline constexpr uint32_t Blob051_Words = 10952u;
extern const uint32_t Blob052[10744];
inline constexpr uint32_t Blob052_Words = 10744u;
extern const uint32_t Blob053[11026];
inline constexpr uint32_t Blob053_Words = 11026u;
extern const uint32_t Blob054[10818];
inline constexpr uint32_t Blob054_Words = 10818u;
extern const uint32_t Blob055[9044];
inline constexpr uint32_t Blob055_Words = 9044u;
extern const uint32_t Blob056[8836];
inline constexpr uint32_t Blob056_Words = 8836u;
extern const uint32_t Blob057[816];
inline constexpr uint32_t Blob057_Words = 816u;
extern const uint32_t Blob058[767];
inline constexpr uint32_t Blob058_Words = 767u;
extern const uint32_t Blob059[1049];
inline constexpr uint32_t Blob059_Words = 1049u;
extern const uint32_t Blob060[1305];
inline constexpr uint32_t Blob060_Words = 1305u;
extern const uint32_t Blob061[1140];
inline constexpr uint32_t Blob061_Words = 1140u;
extern const uint32_t Blob062[2070];
inline constexpr uint32_t Blob062_Words = 2070u;
extern const uint32_t Blob063[2019];
inline constexpr uint32_t Blob063_Words = 2019u;
extern const uint32_t Blob064[2927];
inline constexpr uint32_t Blob064_Words = 2927u;
extern const uint32_t Blob065[1649];
inline constexpr uint32_t Blob065_Words = 1649u;
extern const uint32_t Blob066[2569];
inline constexpr uint32_t Blob066_Words = 2569u;
extern const uint32_t Blob067[2522];
inline constexpr uint32_t Blob067_Words = 2522u;
extern const uint32_t Blob068[3422];
inline constexpr uint32_t Blob068_Words = 3422u;
extern const uint32_t Blob069[1401];
inline constexpr uint32_t Blob069_Words = 1401u;
extern const uint32_t Blob070[6251];
inline constexpr uint32_t Blob070_Words = 6251u;
extern const uint32_t Blob071[5128];
inline constexpr uint32_t Blob071_Words = 5128u;
extern const uint32_t Blob072[14370];
inline constexpr uint32_t Blob072_Words = 14370u;
extern const uint32_t Blob073[14154];
inline constexpr uint32_t Blob073_Words = 14154u;
extern const uint32_t Blob074[4812];
inline constexpr uint32_t Blob074_Words = 4812u;
extern const uint32_t Blob075[6197];
inline constexpr uint32_t Blob075_Words = 6197u;
extern const uint32_t Blob076[6197];
inline constexpr uint32_t Blob076_Words = 6197u;
extern const uint32_t Blob077[9314];
inline constexpr uint32_t Blob077_Words = 9314u;
extern const uint32_t Blob078[9106];
inline constexpr uint32_t Blob078_Words = 9106u;
extern const uint32_t Blob079[9401];
inline constexpr uint32_t Blob079_Words = 9401u;
extern const uint32_t Blob080[9193];
inline constexpr uint32_t Blob080_Words = 9193u;
extern const uint32_t Blob081[9475];
inline constexpr uint32_t Blob081_Words = 9475u;
extern const uint32_t Blob082[9267];
inline constexpr uint32_t Blob082_Words = 9267u;
extern const uint32_t Blob083[11011];
inline constexpr uint32_t Blob083_Words = 11011u;
extern const uint32_t Blob084[10803];
inline constexpr uint32_t Blob084_Words = 10803u;
extern const uint32_t Blob085[10873];
inline constexpr uint32_t Blob085_Words = 10873u;
extern const uint32_t Blob086[10665];
inline constexpr uint32_t Blob086_Words = 10665u;
extern const uint32_t Blob087[10960];
inline constexpr uint32_t Blob087_Words = 10960u;
extern const uint32_t Blob088[10752];
inline constexpr uint32_t Blob088_Words = 10752u;
extern const uint32_t Blob089[11034];
inline constexpr uint32_t Blob089_Words = 11034u;
extern const uint32_t Blob090[10826];
inline constexpr uint32_t Blob090_Words = 10826u;
extern const uint32_t Blob091[9048];
inline constexpr uint32_t Blob091_Words = 9048u;
extern const uint32_t Blob092[8840];
inline constexpr uint32_t Blob092_Words = 8840u;
extern const uint32_t Blob093[820];
inline constexpr uint32_t Blob093_Words = 820u;
extern const uint32_t Blob094[771];
inline constexpr uint32_t Blob094_Words = 771u;
extern const uint32_t Blob095[1053];
inline constexpr uint32_t Blob095_Words = 1053u;
extern const uint32_t Blob096[1309];
inline constexpr uint32_t Blob096_Words = 1309u;
extern const uint32_t Blob097[1148];
inline constexpr uint32_t Blob097_Words = 1148u;
extern const uint32_t Blob098[2078];
inline constexpr uint32_t Blob098_Words = 2078u;
extern const uint32_t Blob099[2027];
inline constexpr uint32_t Blob099_Words = 2027u;
extern const uint32_t Blob100[2935];
inline constexpr uint32_t Blob100_Words = 2935u;
extern const uint32_t Blob101[1657];
inline constexpr uint32_t Blob101_Words = 1657u;
extern const uint32_t Blob102[2577];
inline constexpr uint32_t Blob102_Words = 2577u;
extern const uint32_t Blob103[2530];
inline constexpr uint32_t Blob103_Words = 2530u;
extern const uint32_t Blob104[3430];
inline constexpr uint32_t Blob104_Words = 3430u;
extern const uint32_t Blob105[1401];
inline constexpr uint32_t Blob105_Words = 1401u;
extern const uint32_t Blob106[6251];
inline constexpr uint32_t Blob106_Words = 6251u;
extern const uint32_t Blob107[5128];
inline constexpr uint32_t Blob107_Words = 5128u;

} // namespace detail
} // namespace VulkanShaders
} // namespace melonDS

#endif // GPU3D_VULKAN_SHADERBLOBS_H
