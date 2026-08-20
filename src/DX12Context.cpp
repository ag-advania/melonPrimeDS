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

#include <d3dcompiler.h>
#include <d3d12sdklayers.h>

#include <cstdio>
#include <cstring>
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
#include <algorithm>
#include <iterator>
#include <limits>
#endif

#include "Platform.h"

namespace melonDS
{

namespace DX12
{

namespace
{

std::once_flag gLoaderOnce;
EntryPoints gEntryPoints{};
std::string gLoaderFailure;

void ResolveEntryPoints()
{
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    if (!d3d12)
    {
        gLoaderFailure = "d3d12.dll was not found";
        return;
    }

    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    if (!dxgi)
    {
        gLoaderFailure = "dxgi.dll was not found";
        return;
    }

    gEntryPoints.D3D12CreateDevice =
        reinterpret_cast<EntryPoints::PFN_D3D12CreateDevice>(
            reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12CreateDevice")));
    gEntryPoints.D3D12GetDebugInterface =
        reinterpret_cast<EntryPoints::PFN_D3D12GetDebugInterface>(
            reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12GetDebugInterface")));
    gEntryPoints.D3D12SerializeRootSignature =
        reinterpret_cast<EntryPoints::PFN_D3D12SerializeRootSignature>(
            reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12SerializeRootSignature")));
    gEntryPoints.CreateDXGIFactory2 =
        reinterpret_cast<EntryPoints::PFN_CreateDXGIFactory2>(
            reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory2")));

    if (!gEntryPoints.IsCoreReady())
    {
        gLoaderFailure = "d3d12.dll/dxgi.dll are missing required entry points";
        return;
    }

    // The compute renderer uses committed DXBC. The native presenter still
    // compiles its small vertex/pixel shader during initialization.
    // d3dcompiler_47.dll ships with every Windows version that has D3D12, but a
    // stripped system could still be missing it, so this stays a separate,
    // non-fatal-at-load failure.
    static const char* const kCompilerNames[] = { "d3dcompiler_47.dll", "d3dcompiler_46.dll" };
    for (const char* name : kCompilerNames)
    {
        HMODULE compiler = LoadLibraryA(name);
        if (!compiler) continue;

        gEntryPoints.D3DCompile =
            reinterpret_cast<EntryPoints::PFN_D3DCompile>(
                reinterpret_cast<void*>(GetProcAddress(compiler, "D3DCompile")));
        if (gEntryPoints.D3DCompile) break;
    }

    if (!gEntryPoints.IsShaderCompilerReady())
        gLoaderFailure = "d3dcompiler_47.dll was not found";
}

} // namespace

const EntryPoints& LoadEntryPoints()
{
    std::call_once(gLoaderOnce, ResolveEntryPoints);
    return gEntryPoints;
}

const char* LoaderFailureReason()
{
    std::call_once(gLoaderOnce, ResolveEntryPoints);
    return gLoaderFailure.c_str();
}

bool Fail(const char* context, HRESULT hr)
{
    char* message = nullptr;
    const DWORD len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&message),
        0,
        nullptr);

    Platform::Log(
        Platform::LogLevel::Error,
        "DX12: %s failed (hr=0x%08lX%s%s)\n",
        context,
        static_cast<unsigned long>(hr),
        (len && message) ? ": " : "",
        (len && message) ? message : "");

    if (message) LocalFree(message);
    return false;
}

} // namespace DX12

namespace
{

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
constexpr u32 kTimestampQueryCount = GpuMetricQueryCount;
constexpr auto kTimestampFrequencyRefreshInterval = std::chrono::seconds(1);
#endif

} // namespace

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

DX12::ComPtr<ID3DBlob> DX12Context::CompileShader(
    const std::string& source,
    const char* entryPoint,
    const char* target,
    const std::vector<std::pair<std::string, std::string>>& defines,
    const char* debugName) const
{
    const auto& entry = DX12::LoadEntryPoints();
    DX12::ComPtr<ID3DBlob> result;

    if (!entry.IsShaderCompilerReady())
        return result;

    std::vector<D3D_SHADER_MACRO> macros;
    macros.reserve(defines.size() + 1);
    for (const auto& def : defines)
        macros.push_back(D3D_SHADER_MACRO{ def.first.c_str(), def.second.c_str() });
    macros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });

    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS;
#if !defined(NDEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_ENABLE_STRICTNESS;
#endif

    DX12::ComPtr<ID3DBlob> errors;
    const HRESULT hr = entry.D3DCompile(
        source.data(),
        source.size(),
        debugName,
        macros.data(),
        nullptr,
        entryPoint,
        target,
        flags,
        0,
        result.ReleaseAndGetAddressOf(),
        errors.ReleaseAndGetAddressOf());

    if (FAILED(hr))
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "DX12: shader \"%s\" (%s/%s) failed to compile: %s\n",
            debugName ? debugName : "?",
            entryPoint,
            target,
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no compiler output");
        result.Reset();
        return result;
    }

    if (errors && errors->GetBufferSize() > 1)
    {
        Platform::Log(
            Platform::LogLevel::Warn,
            "DX12: shader \"%s\" compiled with warnings: %s\n",
            debugName ? debugName : "?",
            static_cast<const char*>(errors->GetBufferPointer()));
    }

    return result;
}

DX12::ComPtr<ID3D12Resource> DX12Context::CreateBuffer(
    u64 size,
    D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState,
    D3D12_RESOURCE_FLAGS flags,
    const wchar_t* debugName) const
{
    DX12::ComPtr<ID3D12Resource> resource;
    if (!Device || size == 0)
        return resource;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heapType;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;

    const HRESULT hr = Device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        nullptr,
        IID_PPV_ARGS(resource.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        DX12::Fail("CreateCommittedResource(buffer)", hr);
        resource.Reset();
        return resource;
    }

    if (debugName) resource->SetName(debugName);
    return resource;
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
    DX12::ComPtr<ID3D12Resource> resource;
    if (outResult)
        *outResult = E_FAIL;
    if (!Device || width == 0 || height == 0 || arraySize == 0)
    {
        if (outResult)
            *outResult = !Device ? E_FAIL : E_INVALIDARG;
        return resource;
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = static_cast<UINT16>(arraySize);
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;

    const HRESULT hr = Device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        nullptr,
        IID_PPV_ARGS(resource.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        if (outResult)
            *outResult = hr;
        DX12::Fail("CreateCommittedResource(texture2D)", hr);
        resource.Reset();
        return resource;
    }

    if (debugName) resource->SetName(debugName);
    if (outResult)
        *outResult = S_OK;
    return resource;
}

// ---------------------------------------------------------------------------
// DX12DescriptorRing
// ---------------------------------------------------------------------------

bool DX12DescriptorRing::Init(ID3D12Device* device, u32 descriptorCount, bool shaderVisible)
{
    Shutdown();

    if (!device || descriptorCount == 0)
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = descriptorCount;
    desc.Flags = shaderVisible
        ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
        : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;

    const HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(Heap.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return DX12::Fail("CreateDescriptorHeap", hr);

    Increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CpuStart = Heap->GetCPUDescriptorHandleForHeapStart();
    GpuStart = shaderVisible ? Heap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
    Capacity = descriptorCount;
    Head = 0;
    ShaderVisible = shaderVisible;
    return true;
}

void DX12DescriptorRing::Shutdown()
{
    Heap.Reset();
    CpuStart = {};
    GpuStart = {};
    Increment = 0;
    Capacity = 0;
    Head = 0;
    ShaderVisible = false;
}

bool DX12DescriptorRing::Allocate(
    u32 count,
    D3D12_CPU_DESCRIPTOR_HANDLE& cpu,
    D3D12_GPU_DESCRIPTOR_HANDLE& gpu) noexcept
{
    if (!Heap || count == 0 || Head + count > Capacity)
        return false;

    cpu.ptr = CpuStart.ptr + static_cast<SIZE_T>(Head) * Increment;
    gpu.ptr = ShaderVisible ? (GpuStart.ptr + static_cast<UINT64>(Head) * Increment) : 0;
    Head += count;
    return true;
}

// ---------------------------------------------------------------------------
// DX12CommandContext
// ---------------------------------------------------------------------------

bool DX12CommandContext::Init(ID3D12Device* device, ID3D12CommandQueue* queue)
{
    Shutdown();

    if (!device || !queue)
        return false;

    Device = device;
    Queue = queue;

    HRESULT hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(Allocator.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return DX12::Fail("CreateCommandAllocator", hr);

    hr = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        Allocator.Get(),
        nullptr,
        IID_PPV_ARGS(List.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return DX12::Fail("CreateCommandList", hr);

    // Created open; close it so Begin() can uniformly Reset().
    List->Close();

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(Fence.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return DX12::Fail("CreateFence", hr);

    FenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!FenceEvent)
    {
        Platform::Log(Platform::LogLevel::Error, "DX12: fence event creation failed\n");
        return false;
    }

    FenceValue = 0;
    SubmittedValue = 0;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    TimestampFrequency = 0;
    LastTimestampFrequencyRefresh = {};
    TimestampWrittenMask = 0;
    LastTimestampWrittenMask = 0;
    TimestampSnapshotValues = {};
    TimestampSnapshotValid = false;
    TimestampQueriesEnabled = false;

    if (DX12Perf::IsEnabled())
    {
        D3D12_QUERY_HEAP_DESC queryDesc{};
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryDesc.Count = kTimestampQueryCount;
        queryDesc.NodeMask = 0;

        u64 frequency = 0;
        HRESULT queryResult = device->CreateQueryHeap(
            &queryDesc, IID_PPV_ARGS(TimestampQueryHeap.ReleaseAndGetAddressOf()));
        if (SUCCEEDED(queryResult)
            && SUCCEEDED(queue->GetTimestampFrequency(&frequency))
            && frequency != 0)
        {
            D3D12_HEAP_PROPERTIES readbackHeap{};
            readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
            readbackHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            readbackHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

            D3D12_RESOURCE_DESC readbackDesc{};
            readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            readbackDesc.Width = static_cast<UINT64>(kTimestampQueryCount) * sizeof(u64);
            readbackDesc.Height = 1;
            readbackDesc.DepthOrArraySize = 1;
            readbackDesc.MipLevels = 1;
            readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
            readbackDesc.SampleDesc.Count = 1;
            readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            queryResult = device->CreateCommittedResource(
                &readbackHeap,
                D3D12_HEAP_FLAG_NONE,
                &readbackDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(TimestampReadback.ReleaseAndGetAddressOf()));
            if (SUCCEEDED(queryResult))
            {
                TimestampFrequency = frequency;
                LastTimestampFrequencyRefresh = std::chrono::steady_clock::now();
                TimestampQueriesEnabled = true;
            }
        }
        if (!TimestampQueriesEnabled)
        {
            TimestampQueryHeap.Reset();
            TimestampReadback.Reset();
        }
    }
#endif
    Recording = false;
    return true;
}

void DX12CommandContext::Shutdown()
{
    if (Fence && FenceEvent)
        WaitIdle();

    List.Reset();
    Allocator.Reset();
    Fence.Reset();
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    TimestampReadback.Reset();
    TimestampQueryHeap.Reset();
#endif

    if (FenceEvent)
    {
        CloseHandle(FenceEvent);
        FenceEvent = nullptr;
    }

    Device = nullptr;
    Queue = nullptr;
    FenceValue = 0;
    SubmittedValue = 0;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    TimestampFrequency = 0;
    LastTimestampFrequencyRefresh = {};
    TimestampWrittenMask = 0;
    LastTimestampWrittenMask = 0;
    TimestampSnapshotValues = {};
    TimestampSnapshotValid = false;
    TimestampQueriesEnabled = false;
#endif
    Recording = false;
}

bool DX12CommandContext::WaitForFence(u64 value, bool recordRasterBegin)
{
    if (!Fence || value == 0)
    {
        if (recordRasterBegin)
            DX12Perf::RecordRasterBeginNoWait();
        return true;
    }

    if (Fence->GetCompletedValue() >= value)
    {
        if (recordRasterBegin)
            DX12Perf::RecordRasterBeginNoWait();
        return true;
    }

    const HRESULT hr = Fence->SetEventOnCompletion(value, FenceEvent);
    if (FAILED(hr))
        return DX12::Fail("SetEventOnCompletion", hr);

    DX12Perf::ScopedRasterBeginWait rasterWait(recordRasterBegin);
    constexpr DWORD kFenceWaitTimeoutMs = 5000;
    const DWORD waitResult = WaitForSingleObject(FenceEvent, kFenceWaitTimeoutMs);
    if (waitResult == WAIT_OBJECT_0)
        return true;

    const HRESULT removedReason = Device
        ? Device->GetDeviceRemovedReason() : E_FAIL;
    Platform::Log(
        Platform::LogLevel::Error,
        "DX12: raster reuse fence did not retire within %lu ms (wait=%lu, removed=0x%08lX)\n",
        static_cast<unsigned long>(kFenceWaitTimeoutMs),
        static_cast<unsigned long>(waitResult),
        static_cast<unsigned long>(removedReason));
    return false;
}

void DX12CommandContext::WaitIdle()
{
    WaitForFence(SubmittedValue);
}

bool DX12CommandContext::WaitQueueIdle()
{
    if (!Queue || !Fence || !FenceEvent)
        return true;

    // Finish any list owned by this context before placing the queue-wide
    // retirement fence. The additional signal is intentional even when
    // Submit() just signalled: DXGI Present is issued after Submit() and uses
    // the same direct queue, so waiting only for SubmittedValue can release a
    // swap-chain buffer while presentation still references it.
    if (Recording && !Submit())
        return false;

    const u64 queueIdleValue = ++FenceValue;
    const HRESULT hr = Queue->Signal(Fence.Get(), queueIdleValue);
    if (FAILED(hr))
        return DX12::Fail("ID3D12CommandQueue::Signal(queue idle)", hr);

    SubmittedValue = queueIdleValue;
    return WaitForFence(queueIdleValue);
}

ID3D12GraphicsCommandList* DX12CommandContext::Begin(bool recordRasterBegin)
{
    if (!List || !Allocator)
        return nullptr;

    if (Recording)
        return List.Get();

    // The allocator can only be recycled once the GPU is done with everything
    // recorded from it.
    if (!WaitForFence(SubmittedValue, recordRasterBegin))
        return nullptr;

    return ResetList();
}

ID3D12GraphicsCommandList* DX12CommandContext::TryBegin()
{
    if (!List || !Allocator)
        return nullptr;
    if (Recording)
        return List.Get();
    if (SubmittedValue != 0 && Fence->GetCompletedValue() < SubmittedValue)
        return nullptr;

    return ResetList();
}

bool DX12CommandContext::IsIdle() const noexcept
{
    return !Recording
        && (!Fence || SubmittedValue == 0 || Fence->GetCompletedValue() >= SubmittedValue);
}

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)

void DX12CommandContext::RefreshTimestampFrequencyIfDue() noexcept
{
    if (!TimestampQueriesEnabled || !Queue)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (LastTimestampFrequencyRefresh != std::chrono::steady_clock::time_point{}
        && now - LastTimestampFrequencyRefresh < kTimestampFrequencyRefreshInterval)
    {
        return;
    }

    // The query frequency can change with the adapter clock domain. Refresh
    // it at a report-friendly cadence, not once per metric or once per frame;
    // the timestamp profiler is already developer-only.
    LastTimestampFrequencyRefresh = now;
    u64 frequency = 0;
    if (SUCCEEDED(Queue->GetTimestampFrequency(&frequency)) && frequency != 0)
        TimestampFrequency = frequency;
}

#endif

ID3D12GraphicsCommandList* DX12CommandContext::ResetList()
{
    if (FAILED(Allocator->Reset()))
        return nullptr;
    if (FAILED(List->Reset(Allocator.Get(), nullptr)))
        return nullptr;

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    TimestampWrittenMask = 0;
    TimestampSnapshotValid = false;
    RefreshTimestampFrequencyIfDue();
#endif
    Recording = true;
    return List.Get();
}

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)

void DX12CommandContext::WriteTimestamp(u32 queryIndex) noexcept
{
    if (!TimestampQueriesEnabled || !Recording || queryIndex >= kTimestampQueryCount)
        return;
    List->EndQuery(
        TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
    TimestampWrittenMask |= (1u << queryIndex);
}

bool DX12CommandContext::ReadTimestampSnapshot() const noexcept
{
    if (!TimestampQueriesEnabled || TimestampFrequency == 0
        || LastTimestampWrittenMask == 0 || !TimestampReadback)
    {
        return false;
    }
    if (TimestampSnapshotValid)
        return true;

    const D3D12_RANGE readRange{
        0,
        static_cast<SIZE_T>(kTimestampQueryCount * sizeof(u64))};
    void* mapped = nullptr;
    if (FAILED(TimestampReadback->Map(0, &readRange, &mapped)) || !mapped)
        return false;

    const auto* values = static_cast<const u64*>(mapped);
    std::copy_n(values, kTimestampQueryCount, TimestampSnapshotValues.begin());
    TimestampReadback->Unmap(0, nullptr);
    TimestampSnapshotValid = true;
    return true;
}

u64 DX12CommandContext::ReadTimestampSpanNanoseconds(
    u32 startQuery, u32 endQuery) const noexcept
{
    if (!TimestampQueriesEnabled || TimestampFrequency == 0
        || startQuery >= kTimestampQueryCount || endQuery >= kTimestampQueryCount
        || startQuery > endQuery
        || (LastTimestampWrittenMask & (1u << startQuery)) == 0
        || (LastTimestampWrittenMask & (1u << endQuery)) == 0)
    {
        return 0;
    }

    // The first metric maps the complete retired query snapshot. All other
    // metrics from this completed submission reuse it, so a report with three
    // GPU spans pays one Map/Unmap pair instead of one pair per span.
    if (!ReadTimestampSnapshot())
        return 0;
    const u64 start = TimestampSnapshotValues[startQuery];
    const u64 end = TimestampSnapshotValues[endQuery];
    if (end < start)
        return 0;

    const long double nanoseconds =
        static_cast<long double>(end - start) * 1'000'000'000.0L
        / static_cast<long double>(TimestampFrequency);
    if (!(nanoseconds > 0.0L)
        || nanoseconds >= static_cast<long double>((std::numeric_limits<u64>::max)()))
    {
        return 0;
    }
    return static_cast<u64>(nanoseconds + 0.5L);
}

#endif

bool DX12CommandContext::Submit()
{
    if (!Recording)
        return true;

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    if (TimestampQueriesEnabled && TimestampWrittenMask != 0)
    {
        u32 firstQuery = kTimestampQueryCount;
        u32 lastQuery = 0;
        for (u32 queryIndex = 0; queryIndex < kTimestampQueryCount; ++queryIndex)
        {
            if ((TimestampWrittenMask & (1u << queryIndex)) == 0)
                continue;
            firstQuery = std::min(firstQuery, queryIndex);
            lastQuery = std::max(lastQuery, queryIndex);
        }
        // Resolve one contiguous range. Unwritten slots inside the range are
        // harmless and keeping them in the same copy is cheaper than issuing
        // one ResolveQueryData command for every metric endpoint.
        if (firstQuery <= lastQuery)
        {
            List->ResolveQueryData(
                TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                firstQuery, lastQuery - firstQuery + 1u, TimestampReadback.Get(),
                static_cast<UINT64>(firstQuery) * sizeof(u64));
        }
    }
#endif

    HRESULT hr = List->Close();
    if (FAILED(hr))
    {
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
        LastTimestampWrittenMask = 0;
        TimestampSnapshotValid = false;
#endif
        Recording = false;
        return DX12::Fail("ID3D12GraphicsCommandList::Close", hr);
    }

    Recording = false;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    TimestampSnapshotValid = false;
    LastTimestampWrittenMask = TimestampWrittenMask;
#endif

    ID3D12CommandList* lists[] = { List.Get() };
    Queue->ExecuteCommandLists(1, lists);

    FenceValue++;
    hr = Queue->Signal(Fence.Get(), FenceValue);
    if (FAILED(hr))
        return DX12::Fail("ID3D12CommandQueue::Signal", hr);

    SubmittedValue = FenceValue;
    return true;
}

// ---------------------------------------------------------------------------
// DX12UploadRing
// ---------------------------------------------------------------------------

bool DX12UploadRing::Init(const DX12Context& context, u64 size)
{
    Shutdown();

    Buffer = context.CreateBuffer(
        size,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 upload ring");
    if (!Buffer)
        return false;

    D3D12_RANGE noRead{ 0, 0 };
    void* mapped = nullptr;
    const HRESULT hr = Buffer->Map(0, &noRead, &mapped);
    if (FAILED(hr))
    {
        Buffer.Reset();
        return DX12::Fail("Map(upload ring)", hr);
    }

    Mapped = static_cast<u8*>(mapped);
    Capacity = size;
    Head = 0;
    return true;
}

void DX12UploadRing::Shutdown()
{
    if (Buffer && Mapped)
    {
        D3D12_RANGE written{ 0, 0 };
        Buffer->Unmap(0, &written);
    }
    Buffer.Reset();
    Mapped = nullptr;
    Capacity = 0;
    Head = 0;
}

void* DX12UploadRing::Allocate(u64 size, u64 alignment, u64& outOffset) noexcept
{
    if (!Mapped || size == 0)
        return nullptr;

    if (alignment == 0) alignment = 1;
    const u64 aligned = (Head + alignment - 1) & ~(alignment - 1);
    if (aligned + size > Capacity)
        return nullptr;

    outOffset = aligned;
    Head = aligned + size;
    return Mapped + aligned;
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
