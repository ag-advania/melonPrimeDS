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

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "DX12Gpu2DComposer.h"

namespace melonDS
{

bool DX12Gpu2DComposer::CreateDescriptors(
    ID3D12Device* device, u32 uavTableSize, u32 framesInFlight)
{
    // One UAV block per presentation slot, one per native work slot, and four
    // per work slot for the structured path -- the same sizing the renderer
    // used before these rings moved here.
    if (!OutputUav.Init(device, uavTableSize * framesInFlight, false))
        return false;
    if (!WorkNativeUav.Init(device, uavTableSize * framesInFlight, false))
        return false;
    if (!WorkOutputUav.Init(device, uavTableSize * framesInFlight * 4u, false))
        return false;
    return true;
}

void DX12Gpu2DComposer::ShutdownDescriptors() noexcept
{
    OutputUav.Shutdown();
    WorkOutputUav.Shutdown();
    WorkNativeUav.Shutdown();
    OutputUavCpu = {};
    WorkNativeUavCpu = {};
    WorkOutputUavCpu = {};
}

void DX12Gpu2DComposer::ReleasePipelines() noexcept
{
    Compositor.Reset();
    CorrectCoverage.Reset();
    Native.Reset();
    NativeCapture.Reset();
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
