/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    The minimal driver ABI below is derived from AMD's public Anti-Lag 2 SDK
    header ffx_antilag2_dx12.h, revision v2.0.0a (390aa4a8c8655d0ae6e90079db2c85e103a96da3).

    SPDX-FileCopyrightText: Copyright (c) 2024 Advanced Micro Devices, Inc.
    SPDX-License-Identifier: MIT

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "DX12AmdAntiLag2.h"

#include <cstdint>
#include <cstdio>

#include "Platform.h"

namespace melonDS
{
namespace
{
constexpr u32 AmdVendorId = 0x1002u;
constexpr GUID AmdAntiLag2InterfaceId = {
    0x44085fbe, 0xe839, 0x40c5, {0xbf, 0x38, 0x0e, 0xbc, 0x5a, 0xb4, 0xd0, 0xa6}
};

struct AntiLagApiDataV1
{
    unsigned int Size;
    unsigned int Version;
    unsigned int Mode;
    const char* ControlString;
    unsigned int ControlStringLength;
    unsigned int MaxFps;
};

static_assert(sizeof(AntiLagApiDataV1) == 32, "AMD Anti-Lag 2 DX12 ABI requires a 64-bit build");

std::string DescribeHResult(HRESULT status)
{
    char text[32]{};
    std::snprintf(text, sizeof(text), "HRESULT 0x%08lX", static_cast<unsigned long>(status));
    return text;
}
} // namespace

struct DX12AmdAntiLag2::AmdExtAntiLagApi : IUnknown
{
    virtual HRESULT UpdateAntiLagState(void* data) = 0;
};

DX12AmdAntiLag2Support DX12AmdAntiLag2::Probe(ID3D12Device* device, u32 vendorId)
{
    DX12AmdAntiLag2 probe;
    if (!probe.Initialize(device, vendorId))
        return {false, probe.GetUnavailableReason()};
    probe.Shutdown();
    return {true, {}};
}

bool DX12AmdAntiLag2::Initialize(ID3D12Device* device, u32 vendorId)
{
    Shutdown();
    if (!device)
    {
        UnavailableReason = "AMD Radeon Anti-Lag 2 requires an initialized DirectX 12 device";
        return false;
    }
    if (vendorId != AmdVendorId)
    {
        UnavailableReason = "AMD Radeon Anti-Lag 2 requires a supported AMD Radeon GPU";
        return false;
    }

    HMODULE driver = GetModuleHandleA("amdxc64.dll");
    if (!driver)
    {
        UnavailableReason = "AMD DirectX 12 driver amdxc64.dll was not loaded";
        return false;
    }

    using CreateInterfaceFn = HRESULT (__cdecl*)(IUnknown*, REFIID, void**);
    const auto createInterface = reinterpret_cast<CreateInterfaceFn>(
        reinterpret_cast<void*>(GetProcAddress(driver, "AmdExtD3DCreateInterface")));
    if (!createInterface)
    {
        UnavailableReason = "AMD driver does not expose AmdExtD3DCreateInterface";
        return false;
    }

    HRESULT status = createInterface(
        device,
        AmdAntiLag2InterfaceId,
        reinterpret_cast<void**>(&Api));
    if (status != S_OK || !Api)
    {
        Api = nullptr;
        UnavailableReason = "AMD Radeon Anti-Lag 2 initialization failed: " + DescribeHResult(status);
        return false;
    }

    // AMD's SDK initializes the feature explicitly disabled. The configured
    // value is applied by the first per-frame Update immediately before input.
    AntiLagApiDataV1 data{};
    data.Size = sizeof(data);
    data.Version = 1;
    data.Mode = 2;
    status = Api->UpdateAntiLagState(&data);
    if (status != S_OK)
    {
        UnavailableReason = "AMD Radeon Anti-Lag 2 initial state failed: " + DescribeHResult(status);
        Api->Release();
        Api = nullptr;
        return false;
    }

    Available = true;
    AppliedEnabled = false;
    StateApplied = true;
    UnavailableReason.clear();
    Platform::Log(Platform::LogLevel::Info, "AMD Radeon Anti-Lag 2 DX12 initialized available=1\n");
    return true;
}

void DX12AmdAntiLag2::Shutdown() noexcept
{
    if (Api)
    {
        if (Available && (StateApplied ? AppliedEnabled : false))
        {
            AntiLagApiDataV1 data{};
            data.Size = sizeof(data);
            data.Version = 1;
            data.Mode = 2;
            Api->UpdateAntiLagState(&data);
        }
        Api->Release();
    }
    Api = nullptr;
    Available = false;
    StateApplied = false;
    AppliedEnabled = false;
}

bool DX12AmdAntiLag2::ApplyState()
{
    if (!Available || !Api)
        return false;
    if (StateApplied && AppliedEnabled == Enabled)
        return true;

    AntiLagApiDataV1 data{};
    data.Size = sizeof(data);
    data.Version = 1;
    data.Mode = Enabled ? 1u : 2u;
    data.MaxFps = 0;
    const HRESULT status = Api->UpdateAntiLagState(&data);
    if (status != S_OK)
    {
        DisableForRuntimeFailure("Update state", status);
        return false;
    }

    AppliedEnabled = Enabled;
    StateApplied = true;
    Platform::Log(
        Platform::LogLevel::Info,
        "AMD Radeon Anti-Lag 2 DX12 enabled=%d maxFPS=0\n",
        Enabled ? 1 : 0);
    return true;
}

void DX12AmdAntiLag2::BeginFrame()
{
    if (!ApplyState() || !Api)
        return;

    // The null update is AMD's documented latency-reducing insertion point.
    // It must be immediately before the application samples user input.
    const HRESULT status = Api->UpdateAntiLagState(nullptr);
    if (status != S_OK && status != S_FALSE)
        DisableForRuntimeFailure("Update frame", status);
}

void DX12AmdAntiLag2::DisableForRuntimeFailure(const char* operation, HRESULT status)
{
    UnavailableReason = std::string("AMD Radeon Anti-Lag 2 ") + operation
        + " failed: " + DescribeHResult(status);
    Platform::Log(
        Platform::LogLevel::Error,
        "AMD Radeon Anti-Lag 2 DX12 disabled operation=%s status=%#lx reason=%s\n",
        operation,
        static_cast<unsigned long>(status),
        UnavailableReason.c_str());
    Available = false;
    StateApplied = false;
    if (Api)
    {
        Api->Release();
        Api = nullptr;
    }
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
