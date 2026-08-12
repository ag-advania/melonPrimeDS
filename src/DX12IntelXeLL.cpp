/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    The minimal ABI below is pinned to Intel's public XeSS SDK 3.0.2 / XeLL 1.3.2.10
    at commit 8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0.
    The unmodified runtime and its notices are distributed under
    res/third_party/intel-xell/.
*/

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "DX12IntelXeLL.h"

#include <cstdint>
#include <cstdio>
#include <new>
#include <string>

#include "Platform.h"

namespace melonDS
{
namespace
{
constexpr u32 IntelVendorId = 0x8086u;
constexpr wchar_t RuntimeName[] = L"libxell.dll";

using XeLLResult = std::int32_t;
using XeLLContext = void*;

constexpr XeLLResult XeLLSuccess = 0;

struct XeLLSleepParams
{
    std::uint32_t MinimumIntervalUs;
    // Bit 0: low latency; bit 1: boost (currently unsupported); rest reserved.
    std::uint32_t Flags;
};

struct XeLLVersion
{
    std::uint16_t Major;
    std::uint16_t Minor;
    std::uint16_t Patch;
    std::uint16_t Reserved;
};

static_assert(sizeof(XeLLSleepParams) == 8, "XeLL 1.3 sleep-parameter ABI changed");
static_assert(sizeof(XeLLVersion) == 8, "XeLL 1.3 version ABI changed");

std::string DescribeResult(XeLLResult result)
{
    switch (result)
    {
    case 0: return "success";
    case -1: return "unsupported device";
    case -2: return "unsupported driver";
    case -3: return "uninitialized";
    case -4: return "invalid argument";
    case -6: return "device error";
    case -7: return "not implemented";
    case -8: return "invalid context";
    case -10: return "unsupported configuration";
    case -1000: return "unknown internal failure";
    default:
        char text[32]{};
        std::snprintf(text, sizeof(text), "result %d", result);
        return text;
    }
}

std::wstring RuntimePath()
{
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return {};

    path.resize(length);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return {};
    path.resize(slash + 1);
    path.append(RuntimeName);
    return path;
}
} // namespace

struct DX12IntelXeLL::Api
{
    using CreateContextFn = XeLLResult (__cdecl*)(ID3D12Device*, XeLLContext*);
    using DestroyContextFn = XeLLResult (__cdecl*)(XeLLContext);
    using SetSleepModeFn = XeLLResult (__cdecl*)(XeLLContext, const XeLLSleepParams*);
    using GetSleepModeFn = XeLLResult (__cdecl*)(XeLLContext, XeLLSleepParams*);
    using SleepFn = XeLLResult (__cdecl*)(XeLLContext, std::uint32_t);
    using AddMarkerFn = XeLLResult (__cdecl*)(XeLLContext, std::uint32_t, std::int32_t);
    using GetVersionFn = XeLLResult (__cdecl*)(XeLLVersion*);

    HMODULE Module = nullptr;
    CreateContextFn CreateContext = nullptr;
    DestroyContextFn DestroyContext = nullptr;
    SetSleepModeFn SetSleepMode = nullptr;
    GetSleepModeFn GetSleepMode = nullptr;
    SleepFn Sleep = nullptr;
    AddMarkerFn AddMarker = nullptr;
    GetVersionFn GetVersion = nullptr;

    bool Load(std::string& reason)
    {
        const std::wstring path = RuntimePath();
        if (path.empty())
        {
            reason = "Intel XeLL could not resolve the application directory";
            return false;
        }

        Module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!Module)
        {
            reason = "Intel XeLL runtime libxell.dll is missing or could not be loaded";
            return false;
        }

#define MELONPRIME_XELL_LOAD(member, symbol) \
        member = reinterpret_cast<decltype(member)>( \
            reinterpret_cast<void*>(GetProcAddress(Module, symbol))); \
        if (!member) \
        { \
            reason = std::string("Intel XeLL runtime is missing ") + symbol; \
            return false; \
        }

        MELONPRIME_XELL_LOAD(CreateContext, "xellD3D12CreateContext");
        MELONPRIME_XELL_LOAD(DestroyContext, "xellDestroyContext");
        MELONPRIME_XELL_LOAD(SetSleepMode, "xellSetSleepMode");
        MELONPRIME_XELL_LOAD(GetSleepMode, "xellGetSleepMode");
        MELONPRIME_XELL_LOAD(Sleep, "xellSleep");
        MELONPRIME_XELL_LOAD(AddMarker, "xellAddMarkerData");
        MELONPRIME_XELL_LOAD(GetVersion, "xellGetVersion");
#undef MELONPRIME_XELL_LOAD
        return true;
    }

    void Unload() noexcept
    {
        CreateContext = nullptr;
        DestroyContext = nullptr;
        SetSleepMode = nullptr;
        GetSleepMode = nullptr;
        Sleep = nullptr;
        AddMarker = nullptr;
        GetVersion = nullptr;
        if (Module)
            FreeLibrary(Module);
        Module = nullptr;
    }
};

DX12IntelXeLL::~DX12IntelXeLL()
{
    Shutdown();
}

DX12IntelXeLLSupport DX12IntelXeLL::Probe(ID3D12Device* device, u32 vendorId)
{
    DX12IntelXeLL probe;
    if (!probe.Initialize(device, vendorId))
        return {false, probe.GetUnavailableReason()};
    probe.Shutdown();
    return {true, {}};
}

bool DX12IntelXeLL::Initialize(ID3D12Device* device, u32 vendorId)
{
    Shutdown();
    if (!device)
    {
        UnavailableReason = "Intel XeLL requires an initialized DirectX 12 device";
        return false;
    }
    if (vendorId != IntelVendorId)
    {
        UnavailableReason = "Intel XeLL requires a supported Intel Arc GPU";
        return false;
    }

    Functions = new (std::nothrow) Api();
    if (!Functions)
    {
        UnavailableReason = "Intel XeLL could not allocate its API loader";
        return false;
    }
    if (!Functions->Load(UnavailableReason))
    {
        Functions->Unload();
        delete Functions;
        Functions = nullptr;
        return false;
    }

    XeLLVersion version{};
    const XeLLResult versionResult = Functions->GetVersion(&version);
    if (versionResult != XeLLSuccess)
    {
        UnavailableReason = "Intel XeLL version query failed: " + DescribeResult(versionResult);
        Shutdown();
        return false;
    }

    XeLLContext context = nullptr;
    const XeLLResult createResult = Functions->CreateContext(device, &context);
    if (createResult != XeLLSuccess || !context)
    {
        UnavailableReason = "Intel XeLL context creation failed: " + DescribeResult(createResult);
        Shutdown();
        return false;
    }

    Context = context;
    Available = true;
    UnavailableReason.clear();
    Platform::Log(
        Platform::LogLevel::Info,
        "Intel XeLL DX12 initialized available=1 version=%u.%u.%u\n",
        static_cast<unsigned>(version.Major),
        static_cast<unsigned>(version.Minor),
        static_cast<unsigned>(version.Patch));
    return true;
}

void DX12IntelXeLL::Shutdown() noexcept
{
    FinishFrame();
    if (Functions && Context && Functions->DestroyContext)
        Functions->DestroyContext(static_cast<XeLLContext>(Context));
    Context = nullptr;

    if (Functions)
    {
        Functions->Unload();
        delete Functions;
    }
    Functions = nullptr;
    Available = false;
    Enabled = false;
    StateApplied = false;
    FrameOpen = false;
    SimulationOpen = false;
    RenderSubmitStarted = false;
    RenderSubmitOpen = false;
    PresentStarted = false;
    PresentOpen = false;
    InputSampled = false;
}

bool DX12IntelXeLL::SetEnabled(bool enabled)
{
    if (!Available || !Functions || !Context)
        return false;
    if (StateApplied && Enabled == enabled)
        return true;

    XeLLSleepParams params{};
    params.MinimumIntervalUs = 0;
    params.Flags = enabled ? 1u : 0u;
    XeLLResult result = Functions->SetSleepMode(static_cast<XeLLContext>(Context), &params);
    if (result != XeLLSuccess)
    {
        DisableForRuntimeFailure("xellSetSleepMode", result);
        return false;
    }

    XeLLSleepParams applied{};
    result = Functions->GetSleepMode(static_cast<XeLLContext>(Context), &applied);
    if (result != XeLLSuccess)
    {
        DisableForRuntimeFailure("xellGetSleepMode", result);
        return false;
    }
    if (((applied.Flags & 1u) != 0) != enabled || applied.MinimumIntervalUs != 0)
    {
        DisableForRuntimeFailure("sleep-mode verification", -1000);
        return false;
    }

    Enabled = enabled;
    StateApplied = true;
    Platform::Log(
        Platform::LogLevel::Info,
        "Intel XeLL DX12 enabled=%d minimumIntervalUs=0\n",
        enabled ? 1 : 0);
    return true;
}

void DX12IntelXeLL::BeginFrame()
{
    if (!Available || !Functions || !Context)
        return;
    if (FrameOpen)
        FinishFrame();

    ++FrameId;
    const XeLLResult result = Functions->Sleep(static_cast<XeLLContext>(Context), FrameId);
    if (result != XeLLSuccess)
    {
        DisableForRuntimeFailure("xellSleep", result);
        return;
    }

    FrameOpen = true;
    InputSampled = false;
    RenderSubmitStarted = false;
    RenderSubmitOpen = false;
    PresentStarted = false;
    PresentOpen = false;
    SimulationOpen = SendMarker(Marker::SimulationStart);
}

void DX12IntelXeLL::MarkInputSample()
{
    if (!FrameOpen || InputSampled)
        return;
    if (SendMarker(Marker::InputSample))
        InputSampled = true;
}

void DX12IntelXeLL::MarkRenderSubmitStart()
{
    if (!FrameOpen || RenderSubmitStarted)
        return;
    // Intel requires every marker type for every frame. InputSample is placed
    // at the real late-input boundary in normal frames; an early transition
    // gets a zero-work fallback before the simulation phase closes.
    if (!InputSampled)
        MarkInputSample();
    if (SimulationOpen)
    {
        SendMarker(Marker::SimulationEnd);
        SimulationOpen = false;
    }
    if (Available && SendMarker(Marker::RenderSubmitStart))
    {
        RenderSubmitStarted = true;
        RenderSubmitOpen = true;
    }
}

void DX12IntelXeLL::MarkRenderSubmitEnd()
{
    if (!FrameOpen || !RenderSubmitOpen)
        return;
    SendMarker(Marker::RenderSubmitEnd);
    RenderSubmitOpen = false;
}

void DX12IntelXeLL::EndRenderPhase()
{
    if (!FrameOpen)
        return;
    // A frame with no DS 3D work still needs the required RenderSubmit marker
    // pair. Emit a zero-length phase only when the real submission boundary
    // was never reached.
    if (!RenderSubmitStarted)
        MarkRenderSubmitStart();
    MarkRenderSubmitEnd();
    if (FrameOpen && SimulationOpen)
    {
        SendMarker(Marker::SimulationEnd);
        SimulationOpen = false;
    }
}

void DX12IntelXeLL::MarkPresentStart()
{
    if (!FrameOpen || PresentStarted)
        return;
    EndRenderPhase();
    if (Available && SendMarker(Marker::PresentStart))
    {
        PresentStarted = true;
        PresentOpen = true;
    }
}

void DX12IntelXeLL::MarkPresentEnd()
{
    if (!FrameOpen || !PresentOpen)
        return;
    SendMarker(Marker::PresentEnd);
    PresentOpen = false;
}

void DX12IntelXeLL::FinishFrame()
{
    if (!FrameOpen)
        return;
    // Renderer transitions and failed/omitted presentation paths still close
    // the frame with the complete marker set required by XeLL. On the normal
    // path these methods are no-ops because the real Present was already
    // bracketed in ScreenPanelDX12::drawScreen().
    if (!InputSampled)
        MarkInputSample();
    EndRenderPhase();
    if (!PresentStarted)
        MarkPresentStart();
    MarkPresentEnd();
    FrameOpen = false;
    SimulationOpen = false;
    RenderSubmitStarted = false;
    RenderSubmitOpen = false;
    PresentStarted = false;
    PresentOpen = false;
    InputSampled = false;
}

bool DX12IntelXeLL::SendMarker(Marker marker)
{
    if (!Available || !Functions || !Context || !FrameOpen)
        return false;
    const XeLLResult result = Functions->AddMarker(
        static_cast<XeLLContext>(Context), FrameId, static_cast<std::int32_t>(marker));
    if (result != XeLLSuccess)
    {
        DisableForRuntimeFailure("xellAddMarkerData", result);
        return false;
    }
    return true;
}

void DX12IntelXeLL::DisableForRuntimeFailure(const char* operation, int status)
{
    UnavailableReason = std::string("Intel XeLL ") + operation
        + " failed: " + DescribeResult(status);
    Platform::Log(
        Platform::LogLevel::Error,
        "Intel XeLL DX12 disabled operation=%s status=%d reason=%s\n",
        operation,
        status,
        UnavailableReason.c_str());
    Available = false;
    StateApplied = false;
    FrameOpen = false;
    SimulationOpen = false;
    RenderSubmitStarted = false;
    RenderSubmitOpen = false;
    PresentStarted = false;
    PresentOpen = false;
    InputSampled = false;
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
