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

#include <d3dcompiler.h>
#include <d3d12sdklayers.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

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
    const wchar_t* debugName) const
{
    DX12::ComPtr<ID3D12Resource> resource;
    if (!Device || width == 0 || height == 0 || arraySize == 0)
        return resource;

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
        DX12::Fail("CreateCommittedResource(texture2D)", hr);
        resource.Reset();
        return resource;
    }

    if (debugName) resource->SetName(debugName);
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

    if (FenceEvent)
    {
        CloseHandle(FenceEvent);
        FenceEvent = nullptr;
    }

    Device = nullptr;
    Queue = nullptr;
    FenceValue = 0;
    SubmittedValue = 0;
    Recording = false;
}

bool DX12CommandContext::WaitForFence(u64 value)
{
    if (!Fence || value == 0)
        return true;

    if (Fence->GetCompletedValue() >= value)
        return true;

    const HRESULT hr = Fence->SetEventOnCompletion(value, FenceEvent);
    if (FAILED(hr))
        return DX12::Fail("SetEventOnCompletion", hr);

    WaitForSingleObject(FenceEvent, INFINITE);
    return true;
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

ID3D12GraphicsCommandList* DX12CommandContext::Begin()
{
    if (!List || !Allocator)
        return nullptr;

    if (Recording)
        return List.Get();

    // The allocator can only be recycled once the GPU is done with everything
    // recorded from it.
    WaitForFence(SubmittedValue);

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

ID3D12GraphicsCommandList* DX12CommandContext::ResetList()
{
    if (FAILED(Allocator->Reset()))
        return nullptr;
    if (FAILED(List->Reset(Allocator.Get(), nullptr)))
        return nullptr;

    Recording = true;
    return List.Get();
}

bool DX12CommandContext::Submit()
{
    if (!Recording)
        return true;

    Recording = false;

    HRESULT hr = List->Close();
    if (FAILED(hr))
        return DX12::Fail("ID3D12GraphicsCommandList::Close", hr);

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
