/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    The ABI below is pinned to Intel's public XeSS SDK 3.0.2 / XeLL 1.3.2.10
    at commit 8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0.
    The unmodified runtime and its notices are distributed under
    res/third_party/intel-xell/.
*/

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "DX12IntelXeLL.h"

#include <cstdio>
#include <limits>
#include <string>

#include "Platform.h"

namespace melonDS
{
namespace
{
constexpr u32 IntelVendorId = 0x8086u;
constexpr wchar_t RuntimeName[] = L"libxell.dll";
constexpr DX12IntelXeLLResult XeLLSuccess = 0;

static_assert(sizeof(DX12IntelXeLLSleepParams) == 8,
    "XeLL 1.3 sleep-parameter ABI changed");
static_assert(sizeof(DX12IntelXeLLVersion) == 8,
    "XeLL 1.3 version ABI changed");
static_assert(sizeof(DX12IntelXeLLMarker) == 4,
    "XeLL 1.3 marker ABI changed");
static_assert(sizeof(DX12IntelXeLLLoggingLevel) == 4,
    "XeLL 1.3 logging-level ABI changed");

std::string DescribeResult(DX12IntelXeLLResult result)
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
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
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

bool RuntimeFilePresent()
{
    const std::wstring path = RuntimePath();
    if (path.empty())
        return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

template <typename Function>
bool LoadFunction(
    HMODULE module,
    Function& output,
    const char* name,
    std::string& reason)
{
    output = reinterpret_cast<Function>(
        reinterpret_cast<void*>(GetProcAddress(module, name)));
    if (output)
        return true;
    reason = std::string("Intel XeLL runtime is missing ") + name;
    return false;
}

bool LoadProductionApi(
    DX12IntelXeLLApi& api,
    void*& moduleHandle,
    std::string& reason)
{
    const std::wstring path = RuntimePath();
    if (path.empty())
    {
        reason = "Intel XeLL could not resolve the application directory";
        return false;
    }

    HMODULE module = LoadLibraryExW(
        path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module)
    {
        reason = "Intel XeLL runtime libxell.dll is missing or could not be loaded";
        return false;
    }

    const bool loaded =
        LoadFunction(module, api.CreateContext, "xellD3D12CreateContext", reason)
        && LoadFunction(module, api.DestroyContext, "xellDestroyContext", reason)
        && LoadFunction(module, api.SetSleepMode, "xellSetSleepMode", reason)
        && LoadFunction(module, api.GetSleepMode, "xellGetSleepMode", reason)
        && LoadFunction(module, api.Sleep, "xellSleep", reason)
        && LoadFunction(module, api.AddMarker, "xellAddMarkerData", reason)
        && LoadFunction(module, api.GetVersion, "xellGetVersion", reason)
        && LoadFunction(
            module,
            api.SetLoggingCallback,
            "xellSetLoggingCallback",
            reason);
    if (!loaded)
    {
        FreeLibrary(module);
        return false;
    }

    moduleHandle = module;
    return true;
}

void UnloadRuntime(void*& moduleHandle) noexcept
{
    if (moduleHandle)
        FreeLibrary(static_cast<HMODULE>(moduleHandle));
    moduleHandle = nullptr;
}

void __cdecl XeLLLogCallback(
    const char* message,
    DX12IntelXeLLLoggingLevel loggingLevel)
{
    Platform::LogLevel level = Platform::LogLevel::Error;
    switch (loggingLevel)
    {
    case DX12IntelXeLLLoggingLevel::Debug: level = Platform::LogLevel::Debug; break;
    case DX12IntelXeLLLoggingLevel::Info: level = Platform::LogLevel::Info; break;
    case DX12IntelXeLLLoggingLevel::Warning: level = Platform::LogLevel::Warn; break;
    case DX12IntelXeLLLoggingLevel::Error: level = Platform::LogLevel::Error; break;
    }
    Platform::Log(level, "Intel XeLL runtime: %s\n", message ? message : "(null)");
}
} // namespace

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
    RuntimePresent = RuntimeFilePresent();
    SupportedByProbe = vendorId == IntelVendorId;

    if (!device)
    {
        UnavailableReason = "Intel XeLL requires an initialized DirectX 12 device";
        LogStatus("initialize-failed");
        return false;
    }
    if (!RuntimePresent)
    {
        UnavailableReason = "Intel XeLL runtime libxell.dll is missing";
        LogStatus("initialize-failed");
        return false;
    }
    if (!SupportedByProbe)
    {
        UnavailableReason = "Intel XeLL requires a supported Intel Arc GPU";
        LogStatus("initialize-failed");
        return false;
    }

    DX12IntelXeLLApi api{};
    void* module = nullptr;
    if (!LoadProductionApi(api, module, UnavailableReason))
    {
        LogStatus("initialize-failed");
        return false;
    }

    if (!InitializeWithApi(device, vendorId, api, true))
    {
        UnloadRuntime(module);
        return false;
    }
    RuntimeModule = module;
    return true;
}

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
bool DX12IntelXeLL::InitializeForTesting(
    ID3D12Device* device,
    u32 vendorId,
    const DX12IntelXeLLApi& api)
{
    return InitializeWithApi(device, vendorId, api, true);
}
#endif

bool DX12IntelXeLL::InitializeWithApi(
    ID3D12Device* device,
    u32 vendorId,
    const DX12IntelXeLLApi& api,
    bool runtimePresent)
{
    Shutdown();
    RuntimePresent = runtimePresent;
    SupportedByProbe = vendorId == IntelVendorId;
    if (!device)
    {
        UnavailableReason = "Intel XeLL requires an initialized DirectX 12 device";
        LogStatus("initialize-failed");
        return false;
    }
    if (!SupportedByProbe)
    {
        UnavailableReason = "Intel XeLL requires a supported Intel Arc GPU";
        LogStatus("initialize-failed");
        return false;
    }

    std::string missingSymbol;
    if (!ValidateApi(api, missingSymbol))
    {
        UnavailableReason = "Intel XeLL API table is missing " + missingSymbol;
        LogStatus("initialize-failed");
        return false;
    }
    Functions = api;

    DX12IntelXeLLVersion version{};
    const DX12IntelXeLLResult versionResult = Functions.GetVersion(&version);
    if (versionResult != XeLLSuccess)
    {
        UnavailableReason = "Intel XeLL version query failed: "
            + DescribeResult(versionResult);
        Functions = {};
        LogStatus("initialize-failed");
        return false;
    }
    RuntimeVersion = version;

    DX12IntelXeLLContext context = nullptr;
    const DX12IntelXeLLResult createResult =
        Functions.CreateContext(device, &context);
    if (createResult != XeLLSuccess || !context)
    {
        UnavailableReason = "Intel XeLL context creation failed: "
            + DescribeResult(createResult);
        Functions = {};
        LogStatus("initialize-failed");
        return false;
    }

    Context = context;
    ContextCreated = true;
    Available = true;
    UnavailableReason.clear();

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    constexpr DX12IntelXeLLLoggingLevel minimumLogLevel =
        DX12IntelXeLLLoggingLevel::Warning;
#else
    constexpr DX12IntelXeLLLoggingLevel minimumLogLevel =
        DX12IntelXeLLLoggingLevel::Error;
#endif
    const DX12IntelXeLLResult loggingResult = Functions.SetLoggingCallback(
        static_cast<DX12IntelXeLLContext>(Context),
        minimumLogLevel,
        XeLLLogCallback);
    if (loggingResult != XeLLSuccess)
    {
        Platform::Log(
            Platform::LogLevel::Warn,
            "Intel XeLL logging callback unavailable status=%d reason=%s\n",
            loggingResult,
            DescribeResult(loggingResult).c_str());
    }

    LogStatus("initialized-statically-verified-hardware-validation-pending");
    return true;
}

bool DX12IntelXeLL::ValidateApi(
    const DX12IntelXeLLApi& api,
    std::string& missingSymbol) const
{
#define MELONPRIME_XELL_REQUIRE(member, symbol) \
    if (!api.member) { missingSymbol = symbol; return false; }
    MELONPRIME_XELL_REQUIRE(CreateContext, "xellD3D12CreateContext");
    MELONPRIME_XELL_REQUIRE(DestroyContext, "xellDestroyContext");
    MELONPRIME_XELL_REQUIRE(SetSleepMode, "xellSetSleepMode");
    MELONPRIME_XELL_REQUIRE(GetSleepMode, "xellGetSleepMode");
    MELONPRIME_XELL_REQUIRE(Sleep, "xellSleep");
    MELONPRIME_XELL_REQUIRE(AddMarker, "xellAddMarkerData");
    MELONPRIME_XELL_REQUIRE(GetVersion, "xellGetVersion");
    MELONPRIME_XELL_REQUIRE(SetLoggingCallback, "xellSetLoggingCallback");
#undef MELONPRIME_XELL_REQUIRE
    return true;
}

void DX12IntelXeLL::Shutdown() noexcept
{
    FinishFrame();
    if (ContextCreated)
        LogStatus("shutdown");
    if (Context && Functions.DestroyContext)
        Functions.DestroyContext(static_cast<DX12IntelXeLLContext>(Context));
    Context = nullptr;
    ContextCreated = false;

    UnloadRuntime(RuntimeModule);
    Functions = {};
    RuntimeVersion = {};
    RuntimePresent = false;
    SupportedByProbe = false;
    Requested = false;
    Available = false;
    Enabled = false;
    StateApplied = false;
    MinimumIntervalUs = 0;
    FrameOpen = false;
    SimulationOpen = false;
    RenderSubmitStarted = false;
    RenderSubmitOpen = false;
    PresentStarted = false;
    PresentOpen = false;
    InputSampled = false;
}

bool DX12IntelXeLL::SetSleepMode(
    bool enabled,
    std::uint32_t minimumIntervalUs)
{
    Requested = enabled;
    if (!Available || !Context || !Functions.SetSleepMode)
    {
        LogStatus("sleep-mode-not-applied");
        return false;
    }
    if (StateApplied
        && Enabled == enabled
        && MinimumIntervalUs == minimumIntervalUs)
    {
        return true;
    }

    DX12IntelXeLLSleepParams params{};
    params.MinimumIntervalUs = minimumIntervalUs;
    params.Flags = enabled ? 1u : 0u;
    DX12IntelXeLLResult result = Functions.SetSleepMode(
        static_cast<DX12IntelXeLLContext>(Context), &params);
    if (result != XeLLSuccess)
    {
        DisableForRuntimeFailure("xellSetSleepMode", result);
        return false;
    }

    DX12IntelXeLLSleepParams applied{};
    result = Functions.GetSleepMode(
        static_cast<DX12IntelXeLLContext>(Context), &applied);
    if (result != XeLLSuccess)
    {
        DisableForRuntimeFailure("xellGetSleepMode", result);
        return false;
    }
    if (((applied.Flags & 1u) != 0) != enabled
        || applied.MinimumIntervalUs != minimumIntervalUs)
    {
        DisableForRuntimeFailure("sleep-mode verification", -1000);
        return false;
    }

    Enabled = enabled;
    StateApplied = true;
    MinimumIntervalUs = minimumIntervalUs;
    UnavailableReason.clear();
    LogStatus("sleep-mode-applied");
    return true;
}

DX12IntelXeLLStatus DX12IntelXeLL::GetStatus() const
{
    return {
        Requested,
        RuntimePresent,
        SupportedByProbe,
        ContextCreated,
        StateApplied,
        IsActive(),
        MinimumIntervalUs,
        FrameId,
        RuntimeVersion,
        UnavailableReason,
    };
}

void DX12IntelXeLL::BeginFrame()
{
    // Intel recommends that Sleep and markers remain active in pass-through
    // mode. Only xellSetSleepMode controls whether latency reduction is on.
    if (!Available || !Context || !Functions.Sleep)
        return;
    if (FrameOpen)
        FinishFrame();

    // XeLL identifies frames with uint32_t. Reusing an ID after wrap would
    // violate the increasing-ID contract, so a context is disabled after the
    // final representable ID instead of silently recycling zero.
    if (FrameId == std::numeric_limits<u32>::max())
    {
        DisableForRuntimeFailure("frame ID exhausted", -1000);
        return;
    }
    ++FrameId;
    const DX12IntelXeLLResult result = Functions.Sleep(
        static_cast<DX12IntelXeLLContext>(Context), FrameId);
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
    SimulationOpen = SendMarker(DX12IntelXeLLMarker::SimulationStart);
    if (Available && FrameId % 600u == 0)
        LogStatus("periodic-summary");
}

void DX12IntelXeLL::MarkInputSample()
{
    if (!FrameOpen || InputSampled)
        return;
    if (SendMarker(DX12IntelXeLLMarker::InputSample))
        InputSampled = true;
}

void DX12IntelXeLL::MarkRenderSubmitStart()
{
    if (!FrameOpen || RenderSubmitStarted)
        return;
    if (!InputSampled)
        MarkInputSample();
    if (SimulationOpen)
    {
        SendMarker(DX12IntelXeLLMarker::SimulationEnd);
        SimulationOpen = false;
    }
    if (Available && SendMarker(DX12IntelXeLLMarker::RenderSubmitStart))
    {
        RenderSubmitStarted = true;
        RenderSubmitOpen = true;
    }
}

void DX12IntelXeLL::MarkRenderSubmitEnd()
{
    if (!FrameOpen || !RenderSubmitOpen)
        return;
    SendMarker(DX12IntelXeLLMarker::RenderSubmitEnd);
    RenderSubmitOpen = false;
}

void DX12IntelXeLL::EndRenderPhase()
{
    if (!FrameOpen)
        return;
    if (!RenderSubmitStarted)
        MarkRenderSubmitStart();
    MarkRenderSubmitEnd();
    if (FrameOpen && SimulationOpen)
    {
        SendMarker(DX12IntelXeLLMarker::SimulationEnd);
        SimulationOpen = false;
    }
}

void DX12IntelXeLL::MarkPresentStart()
{
    if (!FrameOpen || PresentStarted)
        return;
    EndRenderPhase();
    if (Available && SendMarker(DX12IntelXeLLMarker::PresentStart))
    {
        PresentStarted = true;
        PresentOpen = true;
    }
}

void DX12IntelXeLL::MarkPresentEnd()
{
    if (!FrameOpen || !PresentOpen)
        return;
    SendMarker(DX12IntelXeLLMarker::PresentEnd);
    PresentOpen = false;
}

void DX12IntelXeLL::FinishFrame()
{
    if (!FrameOpen)
        return;
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

bool DX12IntelXeLL::SendMarker(DX12IntelXeLLMarker marker)
{
    if (!Available || !Context || !Functions.AddMarker || !FrameOpen)
        return false;
    const DX12IntelXeLLResult result = Functions.AddMarker(
        static_cast<DX12IntelXeLLContext>(Context), FrameId, marker);
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
    Available = false;
    StateApplied = false;
    Enabled = false;
    FrameOpen = false;
    SimulationOpen = false;
    RenderSubmitStarted = false;
    RenderSubmitOpen = false;
    PresentStarted = false;
    PresentOpen = false;
    InputSampled = false;
    LogStatus("runtime-failure");
}

void DX12IntelXeLL::LogStatus(const char* event) const
{
    Platform::Log(
        Available ? Platform::LogLevel::Info : Platform::LogLevel::Warn,
        "IntelXeLL event=%s requested=%d runtimePresent=%d supportedByProbe=%d "
        "contextCreated=%d sleepModeApplied=%d actualEnabled=%d "
        "minimumIntervalUs=%u frameId=%u runtimeVersion=%u.%u.%u "
        "hardwareValidation=pending reason=\"%s\"\n",
        event,
        Requested ? 1 : 0,
        RuntimePresent ? 1 : 0,
        SupportedByProbe ? 1 : 0,
        ContextCreated ? 1 : 0,
        StateApplied ? 1 : 0,
        IsActive() ? 1 : 0,
        MinimumIntervalUs,
        FrameId,
        static_cast<unsigned>(RuntimeVersion.Major),
        static_cast<unsigned>(RuntimeVersion.Minor),
        static_cast<unsigned>(RuntimeVersion.Patch),
        UnavailableReason.c_str());
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
