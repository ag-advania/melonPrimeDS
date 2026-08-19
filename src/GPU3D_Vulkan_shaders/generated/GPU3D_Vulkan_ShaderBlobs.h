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

extern const uint32_t Blob000[14789];
inline constexpr uint32_t Blob000_Words = 14789u;
extern const uint32_t Blob001[14573];
inline constexpr uint32_t Blob001_Words = 14573u;
extern const uint32_t Blob002[4803];
inline constexpr uint32_t Blob002_Words = 4803u;
extern const uint32_t Blob003[6686];
inline constexpr uint32_t Blob003_Words = 6686u;
extern const uint32_t Blob004[6686];
inline constexpr uint32_t Blob004_Words = 6686u;
extern const uint32_t Blob005[9467];
inline constexpr uint32_t Blob005_Words = 9467u;
extern const uint32_t Blob006[9259];
inline constexpr uint32_t Blob006_Words = 9259u;
extern const uint32_t Blob007[9550];
inline constexpr uint32_t Blob007_Words = 9550u;
extern const uint32_t Blob008[9342];
inline constexpr uint32_t Blob008_Words = 9342u;
extern const uint32_t Blob009[9624];
inline constexpr uint32_t Blob009_Words = 9624u;
extern const uint32_t Blob010[9416];
inline constexpr uint32_t Blob010_Words = 9416u;
extern const uint32_t Blob011[11198];
inline constexpr uint32_t Blob011_Words = 11198u;
extern const uint32_t Blob012[10990];
inline constexpr uint32_t Blob012_Words = 10990u;
extern const uint32_t Blob013[11060];
inline constexpr uint32_t Blob013_Words = 11060u;
extern const uint32_t Blob014[10852];
inline constexpr uint32_t Blob014_Words = 10852u;
extern const uint32_t Blob015[11143];
inline constexpr uint32_t Blob015_Words = 11143u;
extern const uint32_t Blob016[10935];
inline constexpr uint32_t Blob016_Words = 10935u;
extern const uint32_t Blob017[11217];
inline constexpr uint32_t Blob017_Words = 11217u;
extern const uint32_t Blob018[11009];
inline constexpr uint32_t Blob018_Words = 11009u;
extern const uint32_t Blob019[9201];
inline constexpr uint32_t Blob019_Words = 9201u;
extern const uint32_t Blob020[8993];
inline constexpr uint32_t Blob020_Words = 8993u;
extern const uint32_t Blob021[812];
inline constexpr uint32_t Blob021_Words = 812u;
extern const uint32_t Blob022[763];
inline constexpr uint32_t Blob022_Words = 763u;
extern const uint32_t Blob023[1045];
inline constexpr uint32_t Blob023_Words = 1045u;
extern const uint32_t Blob024[1304];
inline constexpr uint32_t Blob024_Words = 1304u;
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
extern const uint32_t Blob033[1451];
inline constexpr uint32_t Blob033_Words = 1451u;
extern const uint32_t Blob034[6439];
inline constexpr uint32_t Blob034_Words = 6439u;
extern const uint32_t Blob035[5431];
inline constexpr uint32_t Blob035_Words = 5431u;
extern const uint32_t Blob036[4117];
inline constexpr uint32_t Blob036_Words = 4117u;
extern const uint32_t Blob037[27532];
inline constexpr uint32_t Blob037_Words = 27532u;
extern const uint32_t Blob038[14789];
inline constexpr uint32_t Blob038_Words = 14789u;
extern const uint32_t Blob039[14573];
inline constexpr uint32_t Blob039_Words = 14573u;
extern const uint32_t Blob040[4811];
inline constexpr uint32_t Blob040_Words = 4811u;
extern const uint32_t Blob041[6690];
inline constexpr uint32_t Blob041_Words = 6690u;
extern const uint32_t Blob042[6690];
inline constexpr uint32_t Blob042_Words = 6690u;
extern const uint32_t Blob043[9467];
inline constexpr uint32_t Blob043_Words = 9467u;
extern const uint32_t Blob044[9259];
inline constexpr uint32_t Blob044_Words = 9259u;
extern const uint32_t Blob045[9550];
inline constexpr uint32_t Blob045_Words = 9550u;
extern const uint32_t Blob046[9342];
inline constexpr uint32_t Blob046_Words = 9342u;
extern const uint32_t Blob047[9624];
inline constexpr uint32_t Blob047_Words = 9624u;
extern const uint32_t Blob048[9416];
inline constexpr uint32_t Blob048_Words = 9416u;
extern const uint32_t Blob049[11194];
inline constexpr uint32_t Blob049_Words = 11194u;
extern const uint32_t Blob050[10986];
inline constexpr uint32_t Blob050_Words = 10986u;
extern const uint32_t Blob051[11056];
inline constexpr uint32_t Blob051_Words = 11056u;
extern const uint32_t Blob052[10848];
inline constexpr uint32_t Blob052_Words = 10848u;
extern const uint32_t Blob053[11139];
inline constexpr uint32_t Blob053_Words = 11139u;
extern const uint32_t Blob054[10931];
inline constexpr uint32_t Blob054_Words = 10931u;
extern const uint32_t Blob055[11213];
inline constexpr uint32_t Blob055_Words = 11213u;
extern const uint32_t Blob056[11005];
inline constexpr uint32_t Blob056_Words = 11005u;
extern const uint32_t Blob057[9201];
inline constexpr uint32_t Blob057_Words = 9201u;
extern const uint32_t Blob058[8993];
inline constexpr uint32_t Blob058_Words = 8993u;
extern const uint32_t Blob059[816];
inline constexpr uint32_t Blob059_Words = 816u;
extern const uint32_t Blob060[767];
inline constexpr uint32_t Blob060_Words = 767u;
extern const uint32_t Blob061[1049];
inline constexpr uint32_t Blob061_Words = 1049u;
extern const uint32_t Blob062[1308];
inline constexpr uint32_t Blob062_Words = 1308u;
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
extern const uint32_t Blob071[1451];
inline constexpr uint32_t Blob071_Words = 1451u;
extern const uint32_t Blob072[6439];
inline constexpr uint32_t Blob072_Words = 6439u;
extern const uint32_t Blob073[5431];
inline constexpr uint32_t Blob073_Words = 5431u;
extern const uint32_t Blob074[4125];
inline constexpr uint32_t Blob074_Words = 4125u;
extern const uint32_t Blob075[27536];
inline constexpr uint32_t Blob075_Words = 27536u;
extern const uint32_t Blob076[14797];
inline constexpr uint32_t Blob076_Words = 14797u;
extern const uint32_t Blob077[14581];
inline constexpr uint32_t Blob077_Words = 14581u;
extern const uint32_t Blob078[4815];
inline constexpr uint32_t Blob078_Words = 4815u;
extern const uint32_t Blob079[6690];
inline constexpr uint32_t Blob079_Words = 6690u;
extern const uint32_t Blob080[6690];
inline constexpr uint32_t Blob080_Words = 6690u;
extern const uint32_t Blob081[9471];
inline constexpr uint32_t Blob081_Words = 9471u;
extern const uint32_t Blob082[9263];
inline constexpr uint32_t Blob082_Words = 9263u;
extern const uint32_t Blob083[9554];
inline constexpr uint32_t Blob083_Words = 9554u;
extern const uint32_t Blob084[9346];
inline constexpr uint32_t Blob084_Words = 9346u;
extern const uint32_t Blob085[9628];
inline constexpr uint32_t Blob085_Words = 9628u;
extern const uint32_t Blob086[9420];
inline constexpr uint32_t Blob086_Words = 9420u;
extern const uint32_t Blob087[11202];
inline constexpr uint32_t Blob087_Words = 11202u;
extern const uint32_t Blob088[10994];
inline constexpr uint32_t Blob088_Words = 10994u;
extern const uint32_t Blob089[11064];
inline constexpr uint32_t Blob089_Words = 11064u;
extern const uint32_t Blob090[10856];
inline constexpr uint32_t Blob090_Words = 10856u;
extern const uint32_t Blob091[11147];
inline constexpr uint32_t Blob091_Words = 11147u;
extern const uint32_t Blob092[10939];
inline constexpr uint32_t Blob092_Words = 10939u;
extern const uint32_t Blob093[11221];
inline constexpr uint32_t Blob093_Words = 11221u;
extern const uint32_t Blob094[11013];
inline constexpr uint32_t Blob094_Words = 11013u;
extern const uint32_t Blob095[9205];
inline constexpr uint32_t Blob095_Words = 9205u;
extern const uint32_t Blob096[8997];
inline constexpr uint32_t Blob096_Words = 8997u;
extern const uint32_t Blob097[820];
inline constexpr uint32_t Blob097_Words = 820u;
extern const uint32_t Blob098[771];
inline constexpr uint32_t Blob098_Words = 771u;
extern const uint32_t Blob099[1053];
inline constexpr uint32_t Blob099_Words = 1053u;
extern const uint32_t Blob100[1312];
inline constexpr uint32_t Blob100_Words = 1312u;
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
extern const uint32_t Blob109[1451];
inline constexpr uint32_t Blob109_Words = 1451u;
extern const uint32_t Blob110[6439];
inline constexpr uint32_t Blob110_Words = 6439u;
extern const uint32_t Blob111[5431];
inline constexpr uint32_t Blob111_Words = 5431u;
extern const uint32_t Blob112[4133];
inline constexpr uint32_t Blob112_Words = 4133u;
extern const uint32_t Blob113[27532];
inline constexpr uint32_t Blob113_Words = 27532u;

} // namespace detail
} // namespace VulkanShaders
} // namespace melonDS

#endif // GPU3D_VULKAN_SHADERBLOBS_H
