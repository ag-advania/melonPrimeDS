/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#include "GPU2DFrameDump.h"

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
#include <cstdio>
#include <cstdlib>
#endif

namespace melonDS
{

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
namespace
{
constexpr u32 FrameDumpWidth = 256u;
constexpr u32 FrameDumpHeight = 192u;
constexpr u32 FrameDumpPixels = FrameDumpWidth * FrameDumpHeight;

struct FrameDumpHeader
{
    char Magic[8];
    u32 Version;
    u32 Frame;
    u32 Width;
    u32 Height;
    u64 TopHash;
    u64 BottomHash;
};

u64 HashFrame(const u32* pixels) noexcept
{
    u64 hash = 1469598103934665603ull;
    for (u32 i = 0; i < FrameDumpPixels; ++i)
    {
        u32 value = pixels[i];
        for (u32 byte = 0; byte < sizeof(value); ++byte)
        {
            hash ^= static_cast<u8>(value);
            hash *= 1099511628211ull;
            value >>= 8u;
        }
    }
    return hash;
}

bool DumpTriggerReady() noexcept
{
    const char* trigger = std::getenv("MELONPRIME_TEST_GPU2D_FRAME_DUMP_AFTER_SAVESTATE");
    if (!trigger || trigger[0] == '\0')
        return true;

    std::FILE* marker = std::fopen(trigger, "rb");
    if (!marker)
        return false;
    std::fclose(marker);
    return true;
}
} // namespace
#endif

void DumpGPU2DFrame(const u32* top, const u32* bottom) noexcept
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    static bool initialized = false;
    static std::FILE* output = nullptr;
    static u32 frame = 0;
    static u32 canonical[2u * FrameDumpPixels]{};
    if (!initialized)
    {
        initialized = true;
        const char* path = std::getenv("MELONPRIME_TEST_GPU2D_FRAME_DUMP");
        if (path && path[0] != '\0')
            output = std::fopen(path, "wb");
    }
    if (!output)
        return;
    if (!DumpTriggerReady())
        return;

    u32 limit = 1u;
    if (const char* value = std::getenv("MELONPRIME_TEST_GPU2D_FRAME_DUMP_LIMIT"))
    {
        const unsigned long parsed = std::strtoul(value, nullptr, 10);
        if (parsed != 0ul)
            limit = static_cast<u32>(parsed);
    }
    if (frame >= limit)
        return;

    const u32* sources[2] = {top, bottom};
    for (u32 screen = 0; screen < 2u; ++screen)
    {
        for (u32 i = 0; i < FrameDumpPixels; ++i)
        {
            const u32 color = sources[screen][i];
            canonical[screen * FrameDumpPixels + i] =
                (((color >> 16u) & 0xFFu) >> 2u)
                | ((((color >> 8u) & 0xFFu) >> 2u) << 8u)
                | (((color & 0xFFu) >> 2u) << 16u);
        }
    }

    const FrameDumpHeader header = {
        {'M', 'P', '2', 'D', 'D', 'U', 'M', 'P'},
        1u,
        frame,
        FrameDumpWidth,
        FrameDumpHeight,
        HashFrame(canonical),
        HashFrame(canonical + FrameDumpPixels),
    };
    std::fwrite(&header, sizeof(header), 1, output);
    std::fwrite(canonical, sizeof(canonical), 1, output);
    std::fflush(output);
    ++frame;
#else
    (void)top;
    (void)bottom;
#endif
}

}
