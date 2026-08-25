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

#include "DX12Context.h"

#include "DX12Perf.h"
#include "DX12ResourceFactory.h"
#include "DX12ShaderCompiler.h"

#include <d3d12sdklayers.h>

#include <cstdio>
#include <cstring>
#include <iterator>

#include "Platform.h"

namespace melonDS
{

// ---------------------------------------------------------------------------
// DX12Context
// ---------------------------------------------------------------------------

DX12Context& DX12Context::Get()
{
    static DX12Context context;
    return context;
}

DX12Context::~DX12Context()
{
    DestroyDevice();
}

bool DX12Context::Acquire()
{
    std::scoped_lock lock(Mutex);

    if (RefCount > 0)
    {
        if (!Device)
            return false;
        RefCount++;
        return true;
    }

    if (!CreateDevice())
    {
        DestroyDevice();
        return false;
    }

    RefCount = 1;
    return true;
}

void DX12Context::Release()
{
    std::scoped_lock lock(Mutex);

    if (RefCount <= 0)
        return;

    RefCount--;
    if (RefCount == 0)
        DestroyDevice();
}

bool DX12Context::PickAdapter(
    IDXGIFactory6* factory,
    DX12::ComPtr<IDXGIAdapter1>& outAdapter,
    DXGI_ADAPTER_DESC1& outDesc) const
{
    const auto& entry = DX12::LoadEntryPoints();

    // Native DX12 is a real-time GPU backend. Never silently accept WARP or
    // Microsoft Basic Render Driver: they can initialize successfully while
    // running at single-digit FPS, which is worse than the explicit software
    // renderer and makes a transient hardware-adapter conflict look healthy.
    for (UINT i = 0; ; i++)
    {
        DX12::ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr = factory->EnumAdapterByGpuPreference(
            i,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr))
            break;

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)))
            continue;
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            continue;

        // Probe without creating: D3D12CreateDevice with a null out-pointer
        // only reports whether the adapter supports the feature level.
        if (FAILED(entry.D3D12CreateDevice(
                adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
            continue;

        outAdapter = adapter;
        outDesc = desc;
        return true;
    }

    return false;
}

void DX12Context::QueryShaderModel()
{
    // Ask for the highest model first and walk down: the runtime fails the
    // query outright when the requested model is newer than it understands.
    static const D3D_SHADER_MODEL kCandidates[] = {
        static_cast<D3D_SHADER_MODEL>(0x67),
        static_cast<D3D_SHADER_MODEL>(0x66),
        static_cast<D3D_SHADER_MODEL>(0x65),
        static_cast<D3D_SHADER_MODEL>(0x60),
        static_cast<D3D_SHADER_MODEL>(0x51),
    };

    for (D3D_SHADER_MODEL candidate : kCandidates)
    {
        D3D12_FEATURE_DATA_SHADER_MODEL data{};
        data.HighestShaderModel = candidate;
        if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &data, sizeof(data))))
        {
            Profile.HighestShaderModel = static_cast<u32>(data.HighestShaderModel);
            return;
        }
    }

    Profile.HighestShaderModel = 0x51;
}

bool DX12Context::RefreshMemoryAdmission()
{
    if (!Adapter || !Device)
        return false;

    DX12::MemoryAdmissionSnapshot snapshot{};
    snapshot.DedicatedVideoMemory = Profile.DedicatedVideoMemory;

    D3D12_FEATURE_DATA_ARCHITECTURE architecture{};
    architecture.NodeIndex = 0;
    if (SUCCEEDED(Device->CheckFeatureSupport(
            D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture))))
    {
        snapshot.IsUMA = architecture.UMA != FALSE;
    }

    DX12::ComPtr<IDXGIAdapter3> adapter3;
    HRESULT hr = Adapter->QueryInterface(
        __uuidof(IDXGIAdapter3),
        reinterpret_cast<void**>(adapter3.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        Profile.MemoryAdmission = snapshot;
        Platform::Log(
            Platform::LogLevel::Info,
            "DX12: live local-memory budget unavailable (IDXGIAdapter3 QueryInterface "
            "hr=0x%08lX; UMA=%d; DedicatedVideoMemory is diagnostic only)\n",
            static_cast<unsigned long>(hr), snapshot.IsUMA ? 1 : 0);
        return true;
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    hr = adapter3->QueryVideoMemoryInfo(
        0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
    if (FAILED(hr))
    {
        Profile.MemoryAdmission = snapshot;
        Platform::Log(
            Platform::LogLevel::Info,
            "DX12: live local-memory budget unavailable (QueryVideoMemoryInfo "
            "hr=0x%08lX; UMA=%d)\n",
            static_cast<unsigned long>(hr), snapshot.IsUMA ? 1 : 0);
        return true;
    }

    snapshot.HasLiveBudget = true;
    snapshot.LocalBudget = info.Budget;
    snapshot.LocalCurrentUsage = info.CurrentUsage;
    snapshot.LocalAvailableForReservation = info.AvailableForReservation;
    snapshot.LocalCurrentReservation = info.CurrentReservation;
    Profile.MemoryAdmission = snapshot;
    Platform::Log(
        Platform::LogLevel::Debug,
        "DX12: live local-memory budget budget=%lluMB usage=%lluMB "
        "available-reservation=%lluMB current-reservation=%lluMB UMA=%d\n",
        static_cast<unsigned long long>(info.Budget >> 20),
        static_cast<unsigned long long>(info.CurrentUsage >> 20),
        static_cast<unsigned long long>(info.AvailableForReservation >> 20),
        static_cast<unsigned long long>(info.CurrentReservation >> 20),
        snapshot.IsUMA ? 1 : 0);
    return true;
}

bool DX12Context::AdmitScaleDependentResources(
    const DX12::ScaleFootprint& footprint, const char* reason) const
{
    const DX12::MemoryAdmissionResult result =
        DX12::EvaluateMemoryAdmission(Profile.MemoryAdmission, footprint);
    if (result.Accepted)
        return true;

    Platform::Log(
        Platform::LogLevel::Error,
        "DX12: scale admission refused reason=%s requested-default=%lluMB "
        "largest=%lluMB available=%lluMB reserve=%lluMB budget=%lluMB "
        "usage=%lluMB current-reservation=%lluMB dedicated-diagnostic=%lluMB "
        "boundary=%s UMA=%d\n",
        result.Reason.c_str(),
        static_cast<unsigned long long>(footprint.DefaultBytes >> 20),
        static_cast<unsigned long long>(footprint.LargestAllocation >> 20),
        static_cast<unsigned long long>(result.AvailableBytes >> 20),
        static_cast<unsigned long long>(result.SafetyReserve >> 20),
        static_cast<unsigned long long>(Profile.MemoryAdmission.LocalBudget >> 20),
        static_cast<unsigned long long>(Profile.MemoryAdmission.LocalCurrentUsage >> 20),
        static_cast<unsigned long long>(Profile.MemoryAdmission.LocalCurrentReservation >> 20),
        static_cast<unsigned long long>(Profile.DedicatedVideoMemory >> 20),
        reason ? reason : "scale resource recreation",
        Profile.MemoryAdmission.IsUMA ? 1 : 0);
    return false;
}

bool DX12Context::CreateDevice()
{
    FailureReason.clear();
    Profile = DeviceProfile{};

    const auto& entry = DX12::LoadEntryPoints();
    if (!entry.IsCoreReady())
    {
        FailureReason = DX12::LoaderFailureReason();
        if (FailureReason.empty())
            FailureReason = "DirectX 12 runtime was not found";
        return false;
    }
    if (!entry.IsShaderCompilerReady())
    {
        FailureReason = DX12::LoaderFailureReason();
        if (FailureReason.empty())
            FailureReason = "the Direct3D shader compiler was not found";
        return false;
    }

    UINT factoryFlags = 0;

#if defined(MELONPRIME_DX12_ENABLE_DEBUG_LAYER) || !defined(NDEBUG)
    // Debug layer is developer-build-only. GPU-based validation costs an order
    // of magnitude in frame time, so it stays off unless this is a debug build.
    if (entry.D3D12GetDebugInterface)
    {
        DX12::ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(entry.D3D12GetDebugInterface(IID_PPV_ARGS(debug.ReleaseAndGetAddressOf()))))
        {
            debug->EnableDebugLayer();
            DebugLayerEnabled = true;
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

#if !defined(NDEBUG)
            DX12::ComPtr<ID3D12Debug1> debug1;
            if (SUCCEEDED(debug->QueryInterface(IID_PPV_ARGS(debug1.ReleaseAndGetAddressOf()))))
                debug1->SetEnableGPUBasedValidation(TRUE);
#endif
        }
    }
#endif

    HRESULT hr = entry.CreateDXGIFactory2(
        factoryFlags, IID_PPV_ARGS(Factory.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        DX12::Fail("CreateDXGIFactory2", hr);
        FailureReason = "DXGI factory creation failed";
        return false;
    }

    DXGI_ADAPTER_DESC1 desc{};
    if (!PickAdapter(Factory.Get(), Adapter, desc))
    {
        FailureReason = "no Direct3D 12 feature level 11_0 adapter was found";
        return false;
    }

    hr = entry.D3D12CreateDevice(
        Adapter.Get(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(Device.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        DX12::Fail("D3D12CreateDevice", hr);
        FailureReason = "Direct3D 12 device creation failed";
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;

    hr = Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(Queue.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        DX12::Fail("CreateCommandQueue", hr);
        FailureReason = "Direct3D 12 command queue creation failed";
        return false;
    }

    // Feature level probe, purely for the init log.
    static const D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};
    levels.NumFeatureLevels = static_cast<UINT>(std::size(kLevels));
    levels.pFeatureLevelsRequested = kLevels;
    Profile.FeatureLevel = D3D_FEATURE_LEVEL_11_0;
    if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels))))
        Profile.FeatureLevel = levels.MaxSupportedFeatureLevel;

    QueryShaderModel();

    {
        char name[128] = {};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
        Profile.AdapterName = name;
    }
    Profile.VendorId = desc.VendorId;
    Profile.DeviceId = desc.DeviceId;
    Profile.AdapterLuid = static_cast<u64>(static_cast<u32>(desc.AdapterLuid.LowPart))
        | (static_cast<u64>(static_cast<u32>(desc.AdapterLuid.HighPart)) << 32u);
    LARGE_INTEGER driverVersion{};
    if (SUCCEEDED(Adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion)))
        Profile.DriverVersion = static_cast<u64>(driverVersion.QuadPart);
    Profile.DedicatedVideoMemory = desc.DedicatedVideoMemory;
    Profile.IsSoftwareAdapter = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

    // DedicatedVideoMemory is retained for diagnostics, but live admission is
    // sourced from IDXGIAdapter3 when the OS exposes it. Failure is fail-soft;
    // CreateCommittedResource remains the final authority on unsupported WDDM.
    RefreshMemoryAdmission();

    Platform::Log(
        Platform::LogLevel::Info,
        "DX12: adapter=\"%s\" vendor=%04X device=%04X driver=%016llX vram=%lluMB featureLevel=%X.%X shaderModel=%u.%u software=%d debugLayer=%d\n",
        Profile.AdapterName.c_str(),
        Profile.VendorId,
        Profile.DeviceId,
        static_cast<unsigned long long>(Profile.DriverVersion),
        static_cast<unsigned long long>(Profile.DedicatedVideoMemory >> 20),
        (static_cast<unsigned>(Profile.FeatureLevel) >> 12) & 0xF,
        (static_cast<unsigned>(Profile.FeatureLevel) >> 8) & 0xF,
        (Profile.HighestShaderModel >> 4) & 0xF,
        Profile.HighestShaderModel & 0xF,
        Profile.IsSoftwareAdapter ? 1 : 0,
        DebugLayerEnabled ? 1 : 0);

    return true;
}

void DX12Context::DestroyDevice()
{
    Queue.Reset();
    Device.Reset();
    Adapter.Reset();
    Factory.Reset();
    DebugLayerEnabled = false;
}

// The two entry points below are facades. The device owner is where callers
// already have a reference, but the implementations belong to the modules that
// own those responsibilities.

DX12::ComPtr<ID3DBlob> DX12Context::CompileShader(
    const std::string& source,
    const char* entryPoint,
    const char* target,
    const std::vector<std::pair<std::string, std::string>>& defines,
    const char* debugName) const
{
    return DX12ShaderCompiler::Compile(
        source, entryPoint, target, defines, debugName);
}

DX12::ComPtr<ID3D12Resource> DX12Context::CreateBuffer(
    u64 size,
    D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState,
    D3D12_RESOURCE_FLAGS flags,
    const wchar_t* debugName) const
{
    return DX12ResourceFactory(Device.Get())
        .CreateBuffer(size, heapType, initialState, flags, debugName);
}

DX12::ComPtr<ID3D12Resource> DX12Context::CreateTexture2D(
    DXGI_FORMAT format,
    u32 width,
    u32 height,
    u32 arraySize,
    D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState,
    const wchar_t* debugName,
    HRESULT* outResult) const
{
    return DX12ResourceFactory(Device.Get()).CreateTexture2D(
        format, width, height, arraySize, flags, initialState, debugName,
        outResult);
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
