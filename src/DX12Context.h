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

#ifndef DX12_CONTEXT_H
#define DX12_CONTEXT_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
#include <array>
#include <chrono>
#endif
#include <mutex>
#include <string>
#include <vector>

#include "DX12CommandContext.h"
#include "DX12Common.h"
#include "DX12DescriptorRing.h"
#include "DX12MemoryAdmission.h"
#include "DX12UploadRing.h"
#include "GpuStageMetrics.h"

namespace melonDS
{

// Process-wide DirectX 12 device owner, shaped after VulkanContext: the
// settings-dialog feature probe and the renderer both Acquire()/Release() the
// same device instead of each creating their own.
//
// This class owns the factory, the adapter, the device, the main queue and the
// device profile -- and nothing else. Shader compilation, committed-resource
// creation, descriptor rings, command contexts and the upload ring used to
// live here too; each is now its own module, and what remains of them here is
// a forwarding call kept so the existing call sites do not have to change.
class DX12Context
{
public:
    struct DeviceProfile
    {
        std::string AdapterName;
        u32 VendorId = 0;
        u32 DeviceId = 0;
        u64 AdapterLuid = 0;
        u64 DriverVersion = 0;
        u64 DedicatedVideoMemory = 0;
        DX12::MemoryAdmissionSnapshot MemoryAdmission;
        D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_11_0;
        // Highest shader model the device reports, as a packed 0xMm value
        // (0x51 == 5.1, 0x60 == 6.0). D3DCompile only emits DXBC up to 5.1, so
        // this is diagnostic only.
        u32 HighestShaderModel = 0x51;
        bool IsSoftwareAdapter = false;
    };

    static DX12Context& Get();

    DX12Context(const DX12Context&) = delete;
    DX12Context& operator=(const DX12Context&) = delete;

    // Reference-counted. The first Acquire() creates the device; the last
    // Release() destroys it. Returns false when DX12 is unavailable, in which
    // case no reference is taken.
    bool Acquire();
    void Release();

    [[nodiscard]] bool IsReady() const noexcept { return Device.Get() != nullptr; }
    [[nodiscard]] IDXGIFactory6* GetFactory() const noexcept { return Factory.Get(); }
    [[nodiscard]] ID3D12Device* GetDevice() const noexcept { return Device.Get(); }
    [[nodiscard]] ID3D12CommandQueue* GetQueue() const noexcept { return Queue.Get(); }
    [[nodiscard]] const DeviceProfile& GetDeviceProfile() const noexcept { return Profile; }
    [[nodiscard]] const std::string& GetFailureReason() const noexcept { return FailureReason; }

    // QueryInterface/QueryVideoMemoryInfo is optional and fail-soft. Refresh
    // only at device init, scale recreation and renderer-switch boundaries.
    bool RefreshMemoryAdmission();
    [[nodiscard]] bool AdmitScaleDependentResources(
        const DX12::ScaleFootprint& footprint, const char* reason) const;

    // Facade over DX12ShaderCompiler. Kept on the device owner because that
    // is where callers already hold a reference; the compile flags, the
    // macro assembly and the diagnostics live in DX12ShaderCompiler.cpp and
    // change without this class being touched.
    DX12::ComPtr<ID3DBlob> CompileShader(
        const std::string& source,
        const char* entryPoint,
        const char* target,
        const std::vector<std::pair<std::string, std::string>>& defines,
        const char* debugName) const;

    // Facade over DX12ResourceFactory, for the same reason as CompileShader
    // above. All of them log and return an empty ComPtr on failure; callers
    // must null-check.
    DX12::ComPtr<ID3D12Resource> CreateBuffer(
        u64 size,
        D3D12_HEAP_TYPE heapType,
        D3D12_RESOURCE_STATES initialState,
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
        const wchar_t* debugName = nullptr) const;

    DX12::ComPtr<ID3D12Resource> CreateTexture2D(
        DXGI_FORMAT format,
        u32 width,
        u32 height,
        u32 arraySize,
        D3D12_RESOURCE_FLAGS flags,
        D3D12_RESOURCE_STATES initialState,
        const wchar_t* debugName = nullptr,
        HRESULT* outResult = nullptr) const;

private:
    DX12Context() = default;
    ~DX12Context();

    bool CreateDevice();
    void DestroyDevice();
    bool PickAdapter(IDXGIFactory6* factory, DX12::ComPtr<IDXGIAdapter1>& outAdapter, DXGI_ADAPTER_DESC1& outDesc) const;
    void QueryShaderModel();

    mutable std::mutex Mutex;
    int RefCount = 0;

    DX12::ComPtr<IDXGIFactory6> Factory;
    DX12::ComPtr<IDXGIAdapter1> Adapter;
    DX12::ComPtr<ID3D12Device> Device;
    DX12::ComPtr<ID3D12CommandQueue> Queue;

    DeviceProfile Profile;
    std::string FailureReason;
    bool DebugLayerEnabled = false;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_CONTEXT_H
