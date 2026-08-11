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
extern const uint32_t Blob003[6192];
inline constexpr uint32_t Blob003_Words = 6192u;
extern const uint32_t Blob004[6192];
inline constexpr uint32_t Blob004_Words = 6192u;
extern const uint32_t Blob005[9313];
inline constexpr uint32_t Blob005_Words = 9313u;
extern const uint32_t Blob006[9105];
inline constexpr uint32_t Blob006_Words = 9105u;
extern const uint32_t Blob007[9400];
inline constexpr uint32_t Blob007_Words = 9400u;
extern const uint32_t Blob008[9192];
inline constexpr uint32_t Blob008_Words = 9192u;
extern const uint32_t Blob009[9474];
inline constexpr uint32_t Blob009_Words = 9474u;
extern const uint32_t Blob010[9266];
inline constexpr uint32_t Blob010_Words = 9266u;
extern const uint32_t Blob011[11010];
inline constexpr uint32_t Blob011_Words = 11010u;
extern const uint32_t Blob012[10802];
inline constexpr uint32_t Blob012_Words = 10802u;
extern const uint32_t Blob013[10872];
inline constexpr uint32_t Blob013_Words = 10872u;
extern const uint32_t Blob014[10664];
inline constexpr uint32_t Blob014_Words = 10664u;
extern const uint32_t Blob015[10959];
inline constexpr uint32_t Blob015_Words = 10959u;
extern const uint32_t Blob016[10751];
inline constexpr uint32_t Blob016_Words = 10751u;
extern const uint32_t Blob017[11033];
inline constexpr uint32_t Blob017_Words = 11033u;
extern const uint32_t Blob018[10825];
inline constexpr uint32_t Blob018_Words = 10825u;
extern const uint32_t Blob019[9047];
inline constexpr uint32_t Blob019_Words = 9047u;
extern const uint32_t Blob020[8839];
inline constexpr uint32_t Blob020_Words = 8839u;
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
extern const uint32_t Blob033[1404];
inline constexpr uint32_t Blob033_Words = 1404u;
extern const uint32_t Blob034[6254];
inline constexpr uint32_t Blob034_Words = 6254u;
extern const uint32_t Blob035[5131];
inline constexpr uint32_t Blob035_Words = 5131u;
extern const uint32_t Blob036[14789];
inline constexpr uint32_t Blob036_Words = 14789u;
extern const uint32_t Blob037[14573];
inline constexpr uint32_t Blob037_Words = 14573u;
extern const uint32_t Blob038[4811];
inline constexpr uint32_t Blob038_Words = 4811u;
extern const uint32_t Blob039[6196];
inline constexpr uint32_t Blob039_Words = 6196u;
extern const uint32_t Blob040[6196];
inline constexpr uint32_t Blob040_Words = 6196u;
extern const uint32_t Blob041[9313];
inline constexpr uint32_t Blob041_Words = 9313u;
extern const uint32_t Blob042[9105];
inline constexpr uint32_t Blob042_Words = 9105u;
extern const uint32_t Blob043[9400];
inline constexpr uint32_t Blob043_Words = 9400u;
extern const uint32_t Blob044[9192];
inline constexpr uint32_t Blob044_Words = 9192u;
extern const uint32_t Blob045[9474];
inline constexpr uint32_t Blob045_Words = 9474u;
extern const uint32_t Blob046[9266];
inline constexpr uint32_t Blob046_Words = 9266u;
extern const uint32_t Blob047[11006];
inline constexpr uint32_t Blob047_Words = 11006u;
extern const uint32_t Blob048[10798];
inline constexpr uint32_t Blob048_Words = 10798u;
extern const uint32_t Blob049[10868];
inline constexpr uint32_t Blob049_Words = 10868u;
extern const uint32_t Blob050[10660];
inline constexpr uint32_t Blob050_Words = 10660u;
extern const uint32_t Blob051[10955];
inline constexpr uint32_t Blob051_Words = 10955u;
extern const uint32_t Blob052[10747];
inline constexpr uint32_t Blob052_Words = 10747u;
extern const uint32_t Blob053[11029];
inline constexpr uint32_t Blob053_Words = 11029u;
extern const uint32_t Blob054[10821];
inline constexpr uint32_t Blob054_Words = 10821u;
extern const uint32_t Blob055[9047];
inline constexpr uint32_t Blob055_Words = 9047u;
extern const uint32_t Blob056[8839];
inline constexpr uint32_t Blob056_Words = 8839u;
extern const uint32_t Blob057[816];
inline constexpr uint32_t Blob057_Words = 816u;
extern const uint32_t Blob058[767];
inline constexpr uint32_t Blob058_Words = 767u;
extern const uint32_t Blob059[1049];
inline constexpr uint32_t Blob059_Words = 1049u;
extern const uint32_t Blob060[1308];
inline constexpr uint32_t Blob060_Words = 1308u;
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
extern const uint32_t Blob069[1404];
inline constexpr uint32_t Blob069_Words = 1404u;
extern const uint32_t Blob070[6254];
inline constexpr uint32_t Blob070_Words = 6254u;
extern const uint32_t Blob071[5131];
inline constexpr uint32_t Blob071_Words = 5131u;
extern const uint32_t Blob072[14797];
inline constexpr uint32_t Blob072_Words = 14797u;
extern const uint32_t Blob073[14581];
inline constexpr uint32_t Blob073_Words = 14581u;
extern const uint32_t Blob074[4815];
inline constexpr uint32_t Blob074_Words = 4815u;
extern const uint32_t Blob075[6200];
inline constexpr uint32_t Blob075_Words = 6200u;
extern const uint32_t Blob076[6200];
inline constexpr uint32_t Blob076_Words = 6200u;
extern const uint32_t Blob077[9317];
inline constexpr uint32_t Blob077_Words = 9317u;
extern const uint32_t Blob078[9109];
inline constexpr uint32_t Blob078_Words = 9109u;
extern const uint32_t Blob079[9404];
inline constexpr uint32_t Blob079_Words = 9404u;
extern const uint32_t Blob080[9196];
inline constexpr uint32_t Blob080_Words = 9196u;
extern const uint32_t Blob081[9478];
inline constexpr uint32_t Blob081_Words = 9478u;
extern const uint32_t Blob082[9270];
inline constexpr uint32_t Blob082_Words = 9270u;
extern const uint32_t Blob083[11014];
inline constexpr uint32_t Blob083_Words = 11014u;
extern const uint32_t Blob084[10806];
inline constexpr uint32_t Blob084_Words = 10806u;
extern const uint32_t Blob085[10876];
inline constexpr uint32_t Blob085_Words = 10876u;
extern const uint32_t Blob086[10668];
inline constexpr uint32_t Blob086_Words = 10668u;
extern const uint32_t Blob087[10963];
inline constexpr uint32_t Blob087_Words = 10963u;
extern const uint32_t Blob088[10755];
inline constexpr uint32_t Blob088_Words = 10755u;
extern const uint32_t Blob089[11037];
inline constexpr uint32_t Blob089_Words = 11037u;
extern const uint32_t Blob090[10829];
inline constexpr uint32_t Blob090_Words = 10829u;
extern const uint32_t Blob091[9051];
inline constexpr uint32_t Blob091_Words = 9051u;
extern const uint32_t Blob092[8843];
inline constexpr uint32_t Blob092_Words = 8843u;
extern const uint32_t Blob093[820];
inline constexpr uint32_t Blob093_Words = 820u;
extern const uint32_t Blob094[771];
inline constexpr uint32_t Blob094_Words = 771u;
extern const uint32_t Blob095[1053];
inline constexpr uint32_t Blob095_Words = 1053u;
extern const uint32_t Blob096[1312];
inline constexpr uint32_t Blob096_Words = 1312u;
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
extern const uint32_t Blob105[1404];
inline constexpr uint32_t Blob105_Words = 1404u;
extern const uint32_t Blob106[6254];
inline constexpr uint32_t Blob106_Words = 6254u;
extern const uint32_t Blob107[5131];
inline constexpr uint32_t Blob107_Words = 5131u;

} // namespace detail
} // namespace VulkanShaders
} // namespace melonDS

#endif // GPU3D_VULKAN_SHADERBLOBS_H
