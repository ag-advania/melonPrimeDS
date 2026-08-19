/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef GPU2D_FRAME_DUMP_H
#define GPU2D_FRAME_DUMP_H

#include "types.h"

namespace melonDS
{

// Developer-only canonical 256x192 Top/Bottom dump used by independent
// renderer parity checks. Release builds compile this as a no-op; production
// renderers never perform the readback or write a dump.
void DumpGPU2DFrame(const u32* top, const u32* bottom) noexcept;

}

#endif // GPU2D_FRAME_DUMP_H
