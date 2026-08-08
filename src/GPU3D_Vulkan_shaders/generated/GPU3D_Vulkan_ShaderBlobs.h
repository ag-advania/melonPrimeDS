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

extern const uint32_t Blob000[13225];
inline constexpr uint32_t Blob000_Words = 13225u;
extern const uint32_t Blob001[13009];
inline constexpr uint32_t Blob001_Words = 13009u;
extern const uint32_t Blob002[4752];
inline constexpr uint32_t Blob002_Words = 4752u;
extern const uint32_t Blob003[5375];
inline constexpr uint32_t Blob003_Words = 5375u;
extern const uint32_t Blob004[5375];
inline constexpr uint32_t Blob004_Words = 5375u;
extern const uint32_t Blob005[9040];
inline constexpr uint32_t Blob005_Words = 9040u;
extern const uint32_t Blob006[8832];
inline constexpr uint32_t Blob006_Words = 8832u;
extern const uint32_t Blob007[9127];
inline constexpr uint32_t Blob007_Words = 9127u;
extern const uint32_t Blob008[8919];
inline constexpr uint32_t Blob008_Words = 8919u;
extern const uint32_t Blob009[9201];
inline constexpr uint32_t Blob009_Words = 9201u;
extern const uint32_t Blob010[8993];
inline constexpr uint32_t Blob010_Words = 8993u;
extern const uint32_t Blob011[9789];
inline constexpr uint32_t Blob011_Words = 9789u;
extern const uint32_t Blob012[9581];
inline constexpr uint32_t Blob012_Words = 9581u;
extern const uint32_t Blob013[9651];
inline constexpr uint32_t Blob013_Words = 9651u;
extern const uint32_t Blob014[9443];
inline constexpr uint32_t Blob014_Words = 9443u;
extern const uint32_t Blob015[9738];
inline constexpr uint32_t Blob015_Words = 9738u;
extern const uint32_t Blob016[9530];
inline constexpr uint32_t Blob016_Words = 9530u;
extern const uint32_t Blob017[9812];
inline constexpr uint32_t Blob017_Words = 9812u;
extern const uint32_t Blob018[9604];
inline constexpr uint32_t Blob018_Words = 9604u;
extern const uint32_t Blob019[8774];
inline constexpr uint32_t Blob019_Words = 8774u;
extern const uint32_t Blob020[8566];
inline constexpr uint32_t Blob020_Words = 8566u;
extern const uint32_t Blob021[812];
inline constexpr uint32_t Blob021_Words = 812u;
extern const uint32_t Blob022[763];
inline constexpr uint32_t Blob022_Words = 763u;
extern const uint32_t Blob023[1058];
inline constexpr uint32_t Blob023_Words = 1058u;
extern const uint32_t Blob024[1302];
inline constexpr uint32_t Blob024_Words = 1302u;
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
extern const uint32_t Blob033[2210];
inline constexpr uint32_t Blob033_Words = 2210u;
extern const uint32_t Blob034[4483];
inline constexpr uint32_t Blob034_Words = 4483u;
extern const uint32_t Blob035[13225];
inline constexpr uint32_t Blob035_Words = 13225u;
extern const uint32_t Blob036[13009];
inline constexpr uint32_t Blob036_Words = 13009u;
extern const uint32_t Blob037[4760];
inline constexpr uint32_t Blob037_Words = 4760u;
extern const uint32_t Blob038[5379];
inline constexpr uint32_t Blob038_Words = 5379u;
extern const uint32_t Blob039[5379];
inline constexpr uint32_t Blob039_Words = 5379u;
extern const uint32_t Blob040[9040];
inline constexpr uint32_t Blob040_Words = 9040u;
extern const uint32_t Blob041[8832];
inline constexpr uint32_t Blob041_Words = 8832u;
extern const uint32_t Blob042[9127];
inline constexpr uint32_t Blob042_Words = 9127u;
extern const uint32_t Blob043[8919];
inline constexpr uint32_t Blob043_Words = 8919u;
extern const uint32_t Blob044[9201];
inline constexpr uint32_t Blob044_Words = 9201u;
extern const uint32_t Blob045[8993];
inline constexpr uint32_t Blob045_Words = 8993u;
extern const uint32_t Blob046[9789];
inline constexpr uint32_t Blob046_Words = 9789u;
extern const uint32_t Blob047[9581];
inline constexpr uint32_t Blob047_Words = 9581u;
extern const uint32_t Blob048[9651];
inline constexpr uint32_t Blob048_Words = 9651u;
extern const uint32_t Blob049[9443];
inline constexpr uint32_t Blob049_Words = 9443u;
extern const uint32_t Blob050[9738];
inline constexpr uint32_t Blob050_Words = 9738u;
extern const uint32_t Blob051[9530];
inline constexpr uint32_t Blob051_Words = 9530u;
extern const uint32_t Blob052[9812];
inline constexpr uint32_t Blob052_Words = 9812u;
extern const uint32_t Blob053[9604];
inline constexpr uint32_t Blob053_Words = 9604u;
extern const uint32_t Blob054[8774];
inline constexpr uint32_t Blob054_Words = 8774u;
extern const uint32_t Blob055[8566];
inline constexpr uint32_t Blob055_Words = 8566u;
extern const uint32_t Blob056[816];
inline constexpr uint32_t Blob056_Words = 816u;
extern const uint32_t Blob057[767];
inline constexpr uint32_t Blob057_Words = 767u;
extern const uint32_t Blob058[1062];
inline constexpr uint32_t Blob058_Words = 1062u;
extern const uint32_t Blob059[1306];
inline constexpr uint32_t Blob059_Words = 1306u;
extern const uint32_t Blob060[1140];
inline constexpr uint32_t Blob060_Words = 1140u;
extern const uint32_t Blob061[2070];
inline constexpr uint32_t Blob061_Words = 2070u;
extern const uint32_t Blob062[2019];
inline constexpr uint32_t Blob062_Words = 2019u;
extern const uint32_t Blob063[2927];
inline constexpr uint32_t Blob063_Words = 2927u;
extern const uint32_t Blob064[1649];
inline constexpr uint32_t Blob064_Words = 1649u;
extern const uint32_t Blob065[2569];
inline constexpr uint32_t Blob065_Words = 2569u;
extern const uint32_t Blob066[2522];
inline constexpr uint32_t Blob066_Words = 2522u;
extern const uint32_t Blob067[3422];
inline constexpr uint32_t Blob067_Words = 3422u;
extern const uint32_t Blob068[2210];
inline constexpr uint32_t Blob068_Words = 2210u;
extern const uint32_t Blob069[4483];
inline constexpr uint32_t Blob069_Words = 4483u;
extern const uint32_t Blob070[13229];
inline constexpr uint32_t Blob070_Words = 13229u;
extern const uint32_t Blob071[13013];
inline constexpr uint32_t Blob071_Words = 13013u;
extern const uint32_t Blob072[4760];
inline constexpr uint32_t Blob072_Words = 4760u;
extern const uint32_t Blob073[5375];
inline constexpr uint32_t Blob073_Words = 5375u;
extern const uint32_t Blob074[5375];
inline constexpr uint32_t Blob074_Words = 5375u;
extern const uint32_t Blob075[9044];
inline constexpr uint32_t Blob075_Words = 9044u;
extern const uint32_t Blob076[8836];
inline constexpr uint32_t Blob076_Words = 8836u;
extern const uint32_t Blob077[9131];
inline constexpr uint32_t Blob077_Words = 9131u;
extern const uint32_t Blob078[8923];
inline constexpr uint32_t Blob078_Words = 8923u;
extern const uint32_t Blob079[9205];
inline constexpr uint32_t Blob079_Words = 9205u;
extern const uint32_t Blob080[8997];
inline constexpr uint32_t Blob080_Words = 8997u;
extern const uint32_t Blob081[9793];
inline constexpr uint32_t Blob081_Words = 9793u;
extern const uint32_t Blob082[9585];
inline constexpr uint32_t Blob082_Words = 9585u;
extern const uint32_t Blob083[9655];
inline constexpr uint32_t Blob083_Words = 9655u;
extern const uint32_t Blob084[9447];
inline constexpr uint32_t Blob084_Words = 9447u;
extern const uint32_t Blob085[9742];
inline constexpr uint32_t Blob085_Words = 9742u;
extern const uint32_t Blob086[9534];
inline constexpr uint32_t Blob086_Words = 9534u;
extern const uint32_t Blob087[9816];
inline constexpr uint32_t Blob087_Words = 9816u;
extern const uint32_t Blob088[9608];
inline constexpr uint32_t Blob088_Words = 9608u;
extern const uint32_t Blob089[8778];
inline constexpr uint32_t Blob089_Words = 8778u;
extern const uint32_t Blob090[8570];
inline constexpr uint32_t Blob090_Words = 8570u;
extern const uint32_t Blob091[816];
inline constexpr uint32_t Blob091_Words = 816u;
extern const uint32_t Blob092[767];
inline constexpr uint32_t Blob092_Words = 767u;
extern const uint32_t Blob093[1062];
inline constexpr uint32_t Blob093_Words = 1062u;
extern const uint32_t Blob094[1306];
inline constexpr uint32_t Blob094_Words = 1306u;
extern const uint32_t Blob095[1144];
inline constexpr uint32_t Blob095_Words = 1144u;
extern const uint32_t Blob096[2074];
inline constexpr uint32_t Blob096_Words = 2074u;
extern const uint32_t Blob097[2023];
inline constexpr uint32_t Blob097_Words = 2023u;
extern const uint32_t Blob098[2931];
inline constexpr uint32_t Blob098_Words = 2931u;
extern const uint32_t Blob099[1653];
inline constexpr uint32_t Blob099_Words = 1653u;
extern const uint32_t Blob100[2573];
inline constexpr uint32_t Blob100_Words = 2573u;
extern const uint32_t Blob101[2526];
inline constexpr uint32_t Blob101_Words = 2526u;
extern const uint32_t Blob102[3426];
inline constexpr uint32_t Blob102_Words = 3426u;
extern const uint32_t Blob103[2210];
inline constexpr uint32_t Blob103_Words = 2210u;
extern const uint32_t Blob104[4483];
inline constexpr uint32_t Blob104_Words = 4483u;

} // namespace detail
} // namespace VulkanShaders
} // namespace melonDS

#endif // GPU3D_VULKAN_SHADERBLOBS_H
