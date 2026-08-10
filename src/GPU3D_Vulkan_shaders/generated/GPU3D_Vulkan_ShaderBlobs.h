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

extern const uint32_t Blob000[13328];
inline constexpr uint32_t Blob000_Words = 13328u;
extern const uint32_t Blob001[13112];
inline constexpr uint32_t Blob001_Words = 13112u;
extern const uint32_t Blob002[4752];
inline constexpr uint32_t Blob002_Words = 4752u;
extern const uint32_t Blob003[5375];
inline constexpr uint32_t Blob003_Words = 5375u;
extern const uint32_t Blob004[5375];
inline constexpr uint32_t Blob004_Words = 5375u;
extern const uint32_t Blob005[9096];
inline constexpr uint32_t Blob005_Words = 9096u;
extern const uint32_t Blob006[8888];
inline constexpr uint32_t Blob006_Words = 8888u;
extern const uint32_t Blob007[9183];
inline constexpr uint32_t Blob007_Words = 9183u;
extern const uint32_t Blob008[8975];
inline constexpr uint32_t Blob008_Words = 8975u;
extern const uint32_t Blob009[9257];
inline constexpr uint32_t Blob009_Words = 9257u;
extern const uint32_t Blob010[9049];
inline constexpr uint32_t Blob010_Words = 9049u;
extern const uint32_t Blob011[10793];
inline constexpr uint32_t Blob011_Words = 10793u;
extern const uint32_t Blob012[10585];
inline constexpr uint32_t Blob012_Words = 10585u;
extern const uint32_t Blob013[10655];
inline constexpr uint32_t Blob013_Words = 10655u;
extern const uint32_t Blob014[10447];
inline constexpr uint32_t Blob014_Words = 10447u;
extern const uint32_t Blob015[10742];
inline constexpr uint32_t Blob015_Words = 10742u;
extern const uint32_t Blob016[10534];
inline constexpr uint32_t Blob016_Words = 10534u;
extern const uint32_t Blob017[10816];
inline constexpr uint32_t Blob017_Words = 10816u;
extern const uint32_t Blob018[10608];
inline constexpr uint32_t Blob018_Words = 10608u;
extern const uint32_t Blob019[8830];
inline constexpr uint32_t Blob019_Words = 8830u;
extern const uint32_t Blob020[8622];
inline constexpr uint32_t Blob020_Words = 8622u;
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
extern const uint32_t Blob033[1397];
inline constexpr uint32_t Blob033_Words = 1397u;
extern const uint32_t Blob034[6247];
inline constexpr uint32_t Blob034_Words = 6247u;
extern const uint32_t Blob035[4989];
inline constexpr uint32_t Blob035_Words = 4989u;
extern const uint32_t Blob036[13328];
inline constexpr uint32_t Blob036_Words = 13328u;
extern const uint32_t Blob037[13112];
inline constexpr uint32_t Blob037_Words = 13112u;
extern const uint32_t Blob038[4760];
inline constexpr uint32_t Blob038_Words = 4760u;
extern const uint32_t Blob039[5379];
inline constexpr uint32_t Blob039_Words = 5379u;
extern const uint32_t Blob040[5379];
inline constexpr uint32_t Blob040_Words = 5379u;
extern const uint32_t Blob041[9096];
inline constexpr uint32_t Blob041_Words = 9096u;
extern const uint32_t Blob042[8888];
inline constexpr uint32_t Blob042_Words = 8888u;
extern const uint32_t Blob043[9183];
inline constexpr uint32_t Blob043_Words = 9183u;
extern const uint32_t Blob044[8975];
inline constexpr uint32_t Blob044_Words = 8975u;
extern const uint32_t Blob045[9257];
inline constexpr uint32_t Blob045_Words = 9257u;
extern const uint32_t Blob046[9049];
inline constexpr uint32_t Blob046_Words = 9049u;
extern const uint32_t Blob047[10789];
inline constexpr uint32_t Blob047_Words = 10789u;
extern const uint32_t Blob048[10581];
inline constexpr uint32_t Blob048_Words = 10581u;
extern const uint32_t Blob049[10651];
inline constexpr uint32_t Blob049_Words = 10651u;
extern const uint32_t Blob050[10443];
inline constexpr uint32_t Blob050_Words = 10443u;
extern const uint32_t Blob051[10738];
inline constexpr uint32_t Blob051_Words = 10738u;
extern const uint32_t Blob052[10530];
inline constexpr uint32_t Blob052_Words = 10530u;
extern const uint32_t Blob053[10812];
inline constexpr uint32_t Blob053_Words = 10812u;
extern const uint32_t Blob054[10604];
inline constexpr uint32_t Blob054_Words = 10604u;
extern const uint32_t Blob055[8830];
inline constexpr uint32_t Blob055_Words = 8830u;
extern const uint32_t Blob056[8622];
inline constexpr uint32_t Blob056_Words = 8622u;
extern const uint32_t Blob057[816];
inline constexpr uint32_t Blob057_Words = 816u;
extern const uint32_t Blob058[767];
inline constexpr uint32_t Blob058_Words = 767u;
extern const uint32_t Blob059[1062];
inline constexpr uint32_t Blob059_Words = 1062u;
extern const uint32_t Blob060[1306];
inline constexpr uint32_t Blob060_Words = 1306u;
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
extern const uint32_t Blob069[1397];
inline constexpr uint32_t Blob069_Words = 1397u;
extern const uint32_t Blob070[6247];
inline constexpr uint32_t Blob070_Words = 6247u;
extern const uint32_t Blob071[4989];
inline constexpr uint32_t Blob071_Words = 4989u;
extern const uint32_t Blob072[13332];
inline constexpr uint32_t Blob072_Words = 13332u;
extern const uint32_t Blob073[13116];
inline constexpr uint32_t Blob073_Words = 13116u;
extern const uint32_t Blob074[4760];
inline constexpr uint32_t Blob074_Words = 4760u;
extern const uint32_t Blob075[5375];
inline constexpr uint32_t Blob075_Words = 5375u;
extern const uint32_t Blob076[5375];
inline constexpr uint32_t Blob076_Words = 5375u;
extern const uint32_t Blob077[9100];
inline constexpr uint32_t Blob077_Words = 9100u;
extern const uint32_t Blob078[8892];
inline constexpr uint32_t Blob078_Words = 8892u;
extern const uint32_t Blob079[9187];
inline constexpr uint32_t Blob079_Words = 9187u;
extern const uint32_t Blob080[8979];
inline constexpr uint32_t Blob080_Words = 8979u;
extern const uint32_t Blob081[9261];
inline constexpr uint32_t Blob081_Words = 9261u;
extern const uint32_t Blob082[9053];
inline constexpr uint32_t Blob082_Words = 9053u;
extern const uint32_t Blob083[10797];
inline constexpr uint32_t Blob083_Words = 10797u;
extern const uint32_t Blob084[10589];
inline constexpr uint32_t Blob084_Words = 10589u;
extern const uint32_t Blob085[10659];
inline constexpr uint32_t Blob085_Words = 10659u;
extern const uint32_t Blob086[10451];
inline constexpr uint32_t Blob086_Words = 10451u;
extern const uint32_t Blob087[10746];
inline constexpr uint32_t Blob087_Words = 10746u;
extern const uint32_t Blob088[10538];
inline constexpr uint32_t Blob088_Words = 10538u;
extern const uint32_t Blob089[10820];
inline constexpr uint32_t Blob089_Words = 10820u;
extern const uint32_t Blob090[10612];
inline constexpr uint32_t Blob090_Words = 10612u;
extern const uint32_t Blob091[8834];
inline constexpr uint32_t Blob091_Words = 8834u;
extern const uint32_t Blob092[8626];
inline constexpr uint32_t Blob092_Words = 8626u;
extern const uint32_t Blob093[816];
inline constexpr uint32_t Blob093_Words = 816u;
extern const uint32_t Blob094[767];
inline constexpr uint32_t Blob094_Words = 767u;
extern const uint32_t Blob095[1062];
inline constexpr uint32_t Blob095_Words = 1062u;
extern const uint32_t Blob096[1306];
inline constexpr uint32_t Blob096_Words = 1306u;
extern const uint32_t Blob097[1144];
inline constexpr uint32_t Blob097_Words = 1144u;
extern const uint32_t Blob098[2074];
inline constexpr uint32_t Blob098_Words = 2074u;
extern const uint32_t Blob099[2023];
inline constexpr uint32_t Blob099_Words = 2023u;
extern const uint32_t Blob100[2931];
inline constexpr uint32_t Blob100_Words = 2931u;
extern const uint32_t Blob101[1653];
inline constexpr uint32_t Blob101_Words = 1653u;
extern const uint32_t Blob102[2573];
inline constexpr uint32_t Blob102_Words = 2573u;
extern const uint32_t Blob103[2526];
inline constexpr uint32_t Blob103_Words = 2526u;
extern const uint32_t Blob104[3426];
inline constexpr uint32_t Blob104_Words = 3426u;
extern const uint32_t Blob105[1397];
inline constexpr uint32_t Blob105_Words = 1397u;
extern const uint32_t Blob106[6247];
inline constexpr uint32_t Blob106_Words = 6247u;
extern const uint32_t Blob107[4989];
inline constexpr uint32_t Blob107_Words = 4989u;

} // namespace detail
} // namespace VulkanShaders
} // namespace melonDS

#endif // GPU3D_VULKAN_SHADERBLOBS_H
