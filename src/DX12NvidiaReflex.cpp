/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    The minimal NVAPI ABI declarations below are pinned to NVIDIA's public
    NVAPI headers at commit cd6918f60b3c9a0476fdfe7e89bb32330602049d
    (nvapi.h dated 2026-06-01). They are used only to dynamically call the
    NVIDIA display driver; no NVAPI binary is redistributed or linked.

    SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES.
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

#include "DX12NvidiaReflex.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "Platform.h"

namespace melonDS
{
namespace
{
using NvStatus = int;
using NvU8 = std::uint8_t;
using NvU32 = std::uint32_t;
using NvU64 = std::uint64_t;
using NvBool = NvU8;

constexpr NvStatus NvApiOk = 0;
constexpr u32 NvidiaVendorId = 0x10DE;
constexpr NvU32 MakeNvApiVersion(std::size_t size, NvU32 version)
{
    return static_cast<NvU32>(size) | (version << 16);
}

struct NvSetSleepModeParams
{
    NvU32 version;
    NvBool lowLatencyMode;
    NvBool lowLatencyBoost;
    NvU32 minimumIntervalUs;
    NvBool useMarkersToOptimize;
    NvBool useMinQueueTime;
    NvU8 reserved[30];
};

struct NvGetSleepStatusParams
{
    NvU32 version;
    NvBool lowLatencyMode;
    NvBool fullscreenVrr;
    NvBool controlPanelVsyncOn;
    NvU32 sleepIntervalUs;
    NvBool useGameSleep;
    NvBool fullscreenIndependentFlip;
    NvU8 frameGenerationMultiplier;
    NvU8 reserved[119];
};

struct NvLatencyMarkerParams
{
    NvU32 version;
    NvU64 frameId;
    NvU32 markerType;
    NvU64 reserved0;
    NvU8 reserved[56];
};

static_assert(sizeof(NvSetSleepModeParams) == 44);
static_assert(sizeof(NvGetSleepStatusParams) == 136);
static_assert(sizeof(NvLatencyMarkerParams) == 88);

constexpr NvU32 NvSetSleepModeParamsVersion = MakeNvApiVersion(sizeof(NvSetSleepModeParams), 1);
constexpr NvU32 NvGetSleepStatusParamsVersion = MakeNvApiVersion(sizeof(NvGetSleepStatusParams), 1);
constexpr NvU32 NvLatencyMarkerParamsVersion = MakeNvApiVersion(sizeof(NvLatencyMarkerParams), 1);

class NvApiLoader
{
public:
    using QueryInterfaceFn = void* (__cdecl*)(NvU32);
    using InitializeFn = NvStatus (__cdecl*)();
    using GetErrorMessageFn = NvStatus (__cdecl*)(NvStatus, char*);
    using GetSleepStatusFn = NvStatus (__cdecl*)(IUnknown*, NvGetSleepStatusParams*);
    using SetSleepModeFn = NvStatus (__cdecl*)(IUnknown*, NvSetSleepModeParams*);
    using SleepFn = NvStatus (__cdecl*)(IUnknown*);
    using SetLatencyMarkerFn = NvStatus (__cdecl*)(IUnknown*, NvLatencyMarkerParams*);

    static NvApiLoader& Get()
    {
        static NvApiLoader loader;
        return loader;
    }

    bool EnsureLoaded()
    {
        std::call_once(LoadOnce, [this]() { Load(); });
        return Ready;
    }

    std::string DescribeStatus(NvStatus status) const
    {
        char message[64]{};
        if (GetErrorMessage && GetErrorMessage(status, message) == NvApiOk && message[0] != '\0')
            return message;
        return "NVAPI status " + std::to_string(status);
    }

    NvU64 AllocateFrameId() noexcept
    {
        return NextFrameId.fetch_add(1, std::memory_order_relaxed);
    }

    InitializeFn Initialize = nullptr;
    GetErrorMessageFn GetErrorMessage = nullptr;
    GetSleepStatusFn GetSleepStatus = nullptr;
    SetSleepModeFn SetSleepMode = nullptr;
    SleepFn Sleep = nullptr;
    SetLatencyMarkerFn SetLatencyMarker = nullptr;
    std::string FailureReason;

private:
    template <typename T>
    static T Resolve(QueryInterfaceFn query, NvU32 id)
    {
        return reinterpret_cast<T>(query(id));
    }

    void Load()
    {
#if defined(_WIN64)
        constexpr const char* libraryName = "nvapi64.dll";
#else
        constexpr const char* libraryName = "nvapi.dll";
#endif
        Module = LoadLibraryA(libraryName);
        if (!Module)
        {
            FailureReason = std::string(libraryName) + " was not found";
            return;
        }

        const auto query = reinterpret_cast<QueryInterfaceFn>(
            reinterpret_cast<void*>(GetProcAddress(Module, "nvapi_QueryInterface")));
        if (!query)
        {
            FailureReason = "nvapi_QueryInterface was not found";
            return;
        }

        Initialize = Resolve<InitializeFn>(query, 0x0150E828);
        GetErrorMessage = Resolve<GetErrorMessageFn>(query, 0x6C2D048C);
        GetSleepStatus = Resolve<GetSleepStatusFn>(query, 0xAEF96CA1);
        SetSleepMode = Resolve<SetSleepModeFn>(query, 0xAC1CA9E0);
        Sleep = Resolve<SleepFn>(query, 0x852CD1D2);
        SetLatencyMarker = Resolve<SetLatencyMarkerFn>(query, 0xD9984C05);
        if (!Initialize || !GetSleepStatus || !SetSleepMode || !Sleep || !SetLatencyMarker)
        {
            FailureReason = "the NVIDIA driver does not expose the required Reflex functions";
            return;
        }

        const NvStatus status = Initialize();
        if (status != NvApiOk)
        {
            FailureReason = "NVAPI initialization failed: " + DescribeStatus(status);
            return;
        }

        Ready = true;
    }

    std::once_flag LoadOnce;
    HMODULE Module = nullptr; // Kept loaded for process lifetime, like DX12Context's entry points.
    std::atomic<NvU64> NextFrameId{1};
    bool Ready = false;
};

const char* ModeName(DX12NvidiaReflexMode mode)
{
    switch (mode)
    {
    case DX12NvidiaReflexMode::Off: return "Off";
    case DX12NvidiaReflexMode::On: return "On";
    case DX12NvidiaReflexMode::OnBoost: return "On+Boost";
    }
    return "Off";
}

DX12NvidiaReflexMode ClampMode(int mode)
{
    if (mode <= static_cast<int>(DX12NvidiaReflexMode::Off))
        return DX12NvidiaReflexMode::Off;
    if (mode >= static_cast<int>(DX12NvidiaReflexMode::OnBoost))
        return DX12NvidiaReflexMode::OnBoost;
    return DX12NvidiaReflexMode::On;
}
} // namespace

DX12NvidiaReflexSupport DX12NvidiaReflex::Probe(ID3D12Device* device, u32 vendorId)
{
    if (!device)
        return {false, "NVIDIA Reflex requires an initialized DirectX 12 device"};
    if (vendorId != NvidiaVendorId)
        return {false, "NVIDIA Reflex requires a supported NVIDIA GPU"};

    auto& nvapi = NvApiLoader::Get();
    if (!nvapi.EnsureLoaded())
        return {false, nvapi.FailureReason};

    NvGetSleepStatusParams statusParams{};
    statusParams.version = NvGetSleepStatusParamsVersion;
    const NvStatus status = nvapi.GetSleepStatus(device, &statusParams);
    if (status != NvApiOk)
        return {false, "NVIDIA Reflex is unavailable: " + nvapi.DescribeStatus(status)};

    return {true, {}};
}

bool DX12NvidiaReflex::Initialize(ID3D12Device* device, u32 vendorId)
{
    Shutdown();
    Device = device;
    const DX12NvidiaReflexSupport support = Probe(device, vendorId);
    Available = support.Available;
    UnavailableReason = support.Reason;

    if (Available)
    {
        Platform::Log(Platform::LogLevel::Info, "NVIDIA Reflex initialized available=1\n");
    }
    else
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "NVIDIA Reflex initialized available=0 reason=%s\n",
            UnavailableReason.c_str());
    }
    return Available;
}

void DX12NvidiaReflex::Shutdown() noexcept
{
    FinishFrame();
    Device = nullptr;
    Available = false;
    ModeApplied = false;
    FrameOpen = false;
    InputSampled = false;
    SimulationOpen = false;
    RenderSubmitOpen = false;
    PresentOpen = false;
}

bool DX12NvidiaReflex::SetMode(int mode)
{
    const DX12NvidiaReflexMode requestedMode = ClampMode(mode);
    if (!Available || !Device)
    {
        Mode = requestedMode;
        return false;
    }
    if (ModeApplied && Mode == requestedMode)
        return true;

    NvSetSleepModeParams params{};
    params.version = NvSetSleepModeParamsVersion;
    params.lowLatencyMode = requestedMode != DX12NvidiaReflexMode::Off;
    params.lowLatencyBoost = requestedMode == DX12NvidiaReflexMode::OnBoost;
    params.minimumIntervalUs = 0;
    params.useMarkersToOptimize = 1;
    params.useMinQueueTime = 0;

    auto& nvapi = NvApiLoader::Get();
    const NvStatus status = nvapi.SetSleepMode(Device, &params);
    if (status != NvApiOk)
    {
        DisableForRuntimeFailure("SetSleepMode", status);
        Mode = requestedMode;
        return false;
    }

    Mode = requestedMode;
    ModeApplied = true;
    Platform::Log(
        Platform::LogLevel::Info,
        "NVIDIA Reflex mode=%s lowLatency=%d boost=%d minimumIntervalUs=0\n",
        ModeName(Mode),
        params.lowLatencyMode != 0,
        params.lowLatencyBoost != 0);
    return true;
}

void DX12NvidiaReflex::BeginFrame()
{
    if (!Available || !Device)
        return;
    if (FrameOpen)
        FinishFrame();

    auto& nvapi = NvApiLoader::Get();
    const NvStatus sleepStatus = nvapi.Sleep(Device);
    if (sleepStatus != NvApiOk)
    {
        DisableForRuntimeFailure("Sleep", sleepStatus);
        return;
    }

    FrameId = nvapi.AllocateFrameId();
    FrameOpen = true;
    InputSampled = false;
    SimulationOpen = false;
}

void DX12NvidiaReflex::MarkInputSample()
{
    if (!FrameOpen || InputSampled || SimulationOpen)
        return;
    if (SendMarker(Marker::InputSample))
        InputSampled = true;
}

void DX12NvidiaReflex::MarkSimulationStart()
{
    if (!FrameOpen || !InputSampled || SimulationOpen)
        return;
    if (SendMarker(Marker::SimulationStart))
        SimulationOpen = true;
}

void DX12NvidiaReflex::MarkRenderSubmitStart()
{
    if (!FrameOpen || RenderSubmitOpen)
        return;
    if (SimulationOpen)
    {
        SendMarker(Marker::SimulationEnd);
        SimulationOpen = false;
    }
    if (Available && SendMarker(Marker::RenderSubmitStart))
        RenderSubmitOpen = true;
}

void DX12NvidiaReflex::MarkRenderSubmitEnd()
{
    if (!FrameOpen || !RenderSubmitOpen)
        return;
    SendMarker(Marker::RenderSubmitEnd);
    RenderSubmitOpen = false;
}

void DX12NvidiaReflex::EndRenderPhase()
{
    MarkRenderSubmitEnd();
    if (FrameOpen && SimulationOpen)
    {
        SendMarker(Marker::SimulationEnd);
        SimulationOpen = false;
    }
}

void DX12NvidiaReflex::MarkPresentStart()
{
    if (!FrameOpen || PresentOpen)
        return;
    EndRenderPhase();
    if (Available && SendMarker(Marker::PresentStart))
        PresentOpen = true;
}

void DX12NvidiaReflex::MarkPresentEnd()
{
    if (!FrameOpen || !PresentOpen)
        return;
    SendMarker(Marker::PresentEnd);
    PresentOpen = false;
}

void DX12NvidiaReflex::FinishFrame()
{
    MarkPresentEnd();
    EndRenderPhase();
    FrameOpen = false;
    InputSampled = false;
    SimulationOpen = false;
    RenderSubmitOpen = false;
    PresentOpen = false;
}

bool DX12NvidiaReflex::SendMarker(Marker marker)
{
    if (!Available || !Device || !FrameOpen)
        return false;

    NvLatencyMarkerParams params{};
    params.version = NvLatencyMarkerParamsVersion;
    params.frameId = FrameId;
    params.markerType = static_cast<NvU32>(marker);

    auto& nvapi = NvApiLoader::Get();
    const NvStatus status = nvapi.SetLatencyMarker(Device, &params);
    if (status != NvApiOk)
    {
        DisableForRuntimeFailure("SetLatencyMarker", status);
        return false;
    }
    return true;
}

void DX12NvidiaReflex::DisableForRuntimeFailure(const char* operation, int status)
{
    auto& nvapi = NvApiLoader::Get();
    UnavailableReason = std::string("NVIDIA Reflex ") + operation + " failed: "
        + nvapi.DescribeStatus(status);
    Platform::Log(
        Platform::LogLevel::Error,
        "NVIDIA Reflex disabled operation=%s status=%d reason=%s\n",
        operation,
        status,
        UnavailableReason.c_str());
    Available = false;
    ModeApplied = false;
    FrameOpen = false;
    InputSampled = false;
    SimulationOpen = false;
    RenderSubmitOpen = false;
    PresentOpen = false;
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
