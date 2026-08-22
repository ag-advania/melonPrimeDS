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

extern const uint32_t Blob000[14786];
inline constexpr uint32_t Blob000_Words = 14786u;
extern const uint32_t Blob001[14570];
inline constexpr uint32_t Blob001_Words = 14570u;
extern const uint32_t Blob002[4800];
inline constexpr uint32_t Blob002_Words = 4800u;
extern const uint32_t Blob003[6683];
inline constexpr uint32_t Blob003_Words = 6683u;
extern const uint32_t Blob004[6683];
inline constexpr uint32_t Blob004_Words = 6683u;
extern const uint32_t Blob005[9464];
inline constexpr uint32_t Blob005_Words = 9464u;
extern const uint32_t Blob006[9256];
inline constexpr uint32_t Blob006_Words = 9256u;
extern const uint32_t Blob007[9547];
inline constexpr uint32_t Blob007_Words = 9547u;
extern const uint32_t Blob008[9339];
inline constexpr uint32_t Blob008_Words = 9339u;
extern const uint32_t Blob009[9621];
inline constexpr uint32_t Blob009_Words = 9621u;
extern const uint32_t Blob010[9413];
inline constexpr uint32_t Blob010_Words = 9413u;
extern const uint32_t Blob011[11195];
inline constexpr uint32_t Blob011_Words = 11195u;
extern const uint32_t Blob012[10987];
inline constexpr uint32_t Blob012_Words = 10987u;
extern const uint32_t Blob013[11057];
inline constexpr uint32_t Blob013_Words = 11057u;
extern const uint32_t Blob014[10849];
inline constexpr uint32_t Blob014_Words = 10849u;
extern const uint32_t Blob015[11140];
inline constexpr uint32_t Blob015_Words = 11140u;
extern const uint32_t Blob016[10932];
inline constexpr uint32_t Blob016_Words = 10932u;
extern const uint32_t Blob017[11214];
inline constexpr uint32_t Blob017_Words = 11214u;
extern const uint32_t Blob018[11006];
inline constexpr uint32_t Blob018_Words = 11006u;
extern const uint32_t Blob019[9198];
inline constexpr uint32_t Blob019_Words = 9198u;
extern const uint32_t Blob020[8990];
inline constexpr uint32_t Blob020_Words = 8990u;
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
extern const uint32_t Blob033[1448];
inline constexpr uint32_t Blob033_Words = 1448u;
extern const uint32_t Blob034[6515];
inline constexpr uint32_t Blob034_Words = 6515u;
extern const uint32_t Blob035[5466];
inline constexpr uint32_t Blob035_Words = 5466u;
extern const uint32_t Blob036[4117];
inline constexpr uint32_t Blob036_Words = 4117u;
extern const uint32_t Blob037[47380];
inline constexpr uint32_t Blob037_Words = 47380u;
extern const uint32_t Blob038[14786];
inline constexpr uint32_t Blob038_Words = 14786u;
extern const uint32_t Blob039[14570];
inline constexpr uint32_t Blob039_Words = 14570u;
extern const uint32_t Blob040[4808];
inline constexpr uint32_t Blob040_Words = 4808u;
extern const uint32_t Blob041[6687];
inline constexpr uint32_t Blob041_Words = 6687u;
extern const uint32_t Blob042[6687];
inline constexpr uint32_t Blob042_Words = 6687u;
extern const uint32_t Blob043[9464];
inline constexpr uint32_t Blob043_Words = 9464u;
extern const uint32_t Blob044[9256];
inline constexpr uint32_t Blob044_Words = 9256u;
extern const uint32_t Blob045[9547];
inline constexpr uint32_t Blob045_Words = 9547u;
extern const uint32_t Blob046[9339];
inline constexpr uint32_t Blob046_Words = 9339u;
extern const uint32_t Blob047[9621];
inline constexpr uint32_t Blob047_Words = 9621u;
extern const uint32_t Blob048[9413];
inline constexpr uint32_t Blob048_Words = 9413u;
extern const uint32_t Blob049[11191];
inline constexpr uint32_t Blob049_Words = 11191u;
extern const uint32_t Blob050[10983];
inline constexpr uint32_t Blob050_Words = 10983u;
extern const uint32_t Blob051[11053];
inline constexpr uint32_t Blob051_Words = 11053u;
extern const uint32_t Blob052[10845];
inline constexpr uint32_t Blob052_Words = 10845u;
extern const uint32_t Blob053[11136];
inline constexpr uint32_t Blob053_Words = 11136u;
extern const uint32_t Blob054[10928];
inline constexpr uint32_t Blob054_Words = 10928u;
extern const uint32_t Blob055[11210];
inline constexpr uint32_t Blob055_Words = 11210u;
extern const uint32_t Blob056[11002];
inline constexpr uint32_t Blob056_Words = 11002u;
extern const uint32_t Blob057[9198];
inline constexpr uint32_t Blob057_Words = 9198u;
extern const uint32_t Blob058[8990];
inline constexpr uint32_t Blob058_Words = 8990u;
extern const uint32_t Blob059[816];
inline constexpr uint32_t Blob059_Words = 816u;
extern const uint32_t Blob060[767];
inline constexpr uint32_t Blob060_Words = 767u;
extern const uint32_t Blob061[1049];
inline constexpr uint32_t Blob061_Words = 1049u;
extern const uint32_t Blob062[1305];
inline constexpr uint32_t Blob062_Words = 1305u;
extern const uint32_t Blob063[1140];
inline constexpr uint32_t Blob063_Words = 1140u;
extern const uint32_t Blob064[2070];
inline constexpr uint32_t Blob064_Words = 2070u;
extern const uint32_t Blob065[2019];
inline constexpr uint32_t Blob065_Words = 2019u;
extern const uint32_t Blob066[2927];
inline constexpr uint32_t Blob066_Words = 2927u;
extern const uint32_t Blob067[1649];
inline constexpr uint32_t Blob067_Words = 1649u;
extern const uint32_t Blob068[2569];
inline constexpr uint32_t Blob068_Words = 2569u;
extern const uint32_t Blob069[2522];
inline constexpr uint32_t Blob069_Words = 2522u;
extern const uint32_t Blob070[3422];
inline constexpr uint32_t Blob070_Words = 3422u;
extern const uint32_t Blob071[1448];
inline constexpr uint32_t Blob071_Words = 1448u;
extern const uint32_t Blob072[6515];
inline constexpr uint32_t Blob072_Words = 6515u;
extern const uint32_t Blob073[5466];
inline constexpr uint32_t Blob073_Words = 5466u;
extern const uint32_t Blob074[4125];
inline constexpr uint32_t Blob074_Words = 4125u;
extern const uint32_t Blob075[47384];
inline constexpr uint32_t Blob075_Words = 47384u;
extern const uint32_t Blob076[14794];
inline constexpr uint32_t Blob076_Words = 14794u;
extern const uint32_t Blob077[14578];
inline constexpr uint32_t Blob077_Words = 14578u;
extern const uint32_t Blob078[4812];
inline constexpr uint32_t Blob078_Words = 4812u;
extern const uint32_t Blob079[6687];
inline constexpr uint32_t Blob079_Words = 6687u;
extern const uint32_t Blob080[6687];
inline constexpr uint32_t Blob080_Words = 6687u;
extern const uint32_t Blob081[9468];
inline constexpr uint32_t Blob081_Words = 9468u;
extern const uint32_t Blob082[9260];
inline constexpr uint32_t Blob082_Words = 9260u;
extern const uint32_t Blob083[9551];
inline constexpr uint32_t Blob083_Words = 9551u;
extern const uint32_t Blob084[9343];
inline constexpr uint32_t Blob084_Words = 9343u;
extern const uint32_t Blob085[9625];
inline constexpr uint32_t Blob085_Words = 9625u;
extern const uint32_t Blob086[9417];
inline constexpr uint32_t Blob086_Words = 9417u;
extern const uint32_t Blob087[11199];
inline constexpr uint32_t Blob087_Words = 11199u;
extern const uint32_t Blob088[10991];
inline constexpr uint32_t Blob088_Words = 10991u;
extern const uint32_t Blob089[11061];
inline constexpr uint32_t Blob089_Words = 11061u;
extern const uint32_t Blob090[10853];
inline constexpr uint32_t Blob090_Words = 10853u;
extern const uint32_t Blob091[11144];
inline constexpr uint32_t Blob091_Words = 11144u;
extern const uint32_t Blob092[10936];
inline constexpr uint32_t Blob092_Words = 10936u;
extern const uint32_t Blob093[11218];
inline constexpr uint32_t Blob093_Words = 11218u;
extern const uint32_t Blob094[11010];
inline constexpr uint32_t Blob094_Words = 11010u;
extern const uint32_t Blob095[9202];
inline constexpr uint32_t Blob095_Words = 9202u;
extern const uint32_t Blob096[8994];
inline constexpr uint32_t Blob096_Words = 8994u;
extern const uint32_t Blob097[820];
inline constexpr uint32_t Blob097_Words = 820u;
extern const uint32_t Blob098[771];
inline constexpr uint32_t Blob098_Words = 771u;
extern const uint32_t Blob099[1053];
inline constexpr uint32_t Blob099_Words = 1053u;
extern const uint32_t Blob100[1309];
inline constexpr uint32_t Blob100_Words = 1309u;
extern const uint32_t Blob101[1148];
inline constexpr uint32_t Blob101_Words = 1148u;
extern const uint32_t Blob102[2078];
inline constexpr uint32_t Blob102_Words = 2078u;
extern const uint32_t Blob103[2027];
inline constexpr uint32_t Blob103_Words = 2027u;
extern const uint32_t Blob104[2935];
inline constexpr uint32_t Blob104_Words = 2935u;
extern const uint32_t Blob105[1657];
inline constexpr uint32_t Blob105_Words = 1657u;
extern const uint32_t Blob106[2577];
inline constexpr uint32_t Blob106_Words = 2577u;
extern const uint32_t Blob107[2530];
inline constexpr uint32_t Blob107_Words = 2530u;
extern const uint32_t Blob108[3430];
inline constexpr uint32_t Blob108_Words = 3430u;
extern const uint32_t Blob109[1448];
inline constexpr uint32_t Blob109_Words = 1448u;
extern const uint32_t Blob110[6515];
inline constexpr uint32_t Blob110_Words = 6515u;
extern const uint32_t Blob111[5466];
inline constexpr uint32_t Blob111_Words = 5466u;
extern const uint32_t Blob112[4133];
inline constexpr uint32_t Blob112_Words = 4133u;
extern const uint32_t Blob113[47380];
inline constexpr uint32_t Blob113_Words = 47380u;

} // namespace detail
} // namespace VulkanShaders
} // namespace melonDS

#endif // GPU3D_VULKAN_SHADERBLOBS_H
