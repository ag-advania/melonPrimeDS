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

#include <mutex>
#include <string>
#include <vector>

#include "DX12Common.h"

namespace melonDS
{

// Process-wide DirectX 12 device owner, shaped after VulkanContext: the
// settings-dialog feature probe and the renderer both Acquire()/Release() the
// same device instead of each creating their own.
class DX12Context
{
public:
    struct DeviceProfile
    {
        std::string AdapterName;
        u32 VendorId = 0;
        u32 DeviceId = 0;
        u64 DriverVersion = 0;
        u64 DedicatedVideoMemory = 0;
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

    // Compiles HLSL with the runtime d3dcompiler_47.dll. `target` is a shader
    // model string such as "cs_5_1". Returns nullptr and logs on failure.
    DX12::ComPtr<ID3DBlob> CompileShader(
        const std::string& source,
        const char* entryPoint,
        const char* target,
        const std::vector<std::pair<std::string, std::string>>& defines,
        const char* debugName) const;

    // Committed-resource helpers. All of them log and return an empty ComPtr on
    // failure; callers must null-check.
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
        const wchar_t* debugName = nullptr) const;

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

// Simple linear allocator over one shader-visible CBV/SRV/UAV descriptor heap.
// The compute renderer rebuilds its descriptor tables every frame, so a bump
// allocator that resets per frame is both sufficient and the cheapest option.
class DX12DescriptorRing
{
public:
    bool Init(ID3D12Device* device, u32 descriptorCount, bool shaderVisible);
    void Shutdown();

    void Reset() noexcept { Head = 0; }

    // Allocates `count` contiguous descriptors. Returns false when the heap is
    // exhausted (the caller should treat that as a hard error, not a hint to
    // grow: the renderer sizes the heap for its worst frame).
    bool Allocate(u32 count, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu) noexcept;

    [[nodiscard]] ID3D12DescriptorHeap* GetHeap() const noexcept { return Heap.Get(); }
    [[nodiscard]] u32 GetIncrement() const noexcept { return Increment; }

private:
    DX12::ComPtr<ID3D12DescriptorHeap> Heap;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuStart{};
    D3D12_GPU_DESCRIPTOR_HANDLE GpuStart{};
    u32 Increment = 0;
    u32 Capacity = 0;
    u32 Head = 0;
    bool ShaderVisible = false;
};

// One-command-list-per-frame recorder with fence-based CPU/GPU sync. The
// renderer records uploads and dispatches into the open list, submits once,
// and blocks on the fence only when the software 2D compositor actually asks
// for a scanline.
class DX12CommandContext
{
public:
    bool Init(ID3D12Device* device, ID3D12CommandQueue* queue);
    void Shutdown();

    // Blocks until every previously submitted list finished, then opens a fresh
    // command list. Returns nullptr if the context is not initialized.
    ID3D12GraphicsCommandList* Begin();

    // Opens the list only when its previous submission has already retired.
    // Never waits: compositor rings use this to drop a frame instead of
    // blocking VBlank when the GPU is more than their slot depth behind.
    ID3D12GraphicsCommandList* TryBegin();

    [[nodiscard]] ID3D12GraphicsCommandList* GetList() const noexcept { return List.Get(); }
    [[nodiscard]] bool IsRecording() const noexcept { return Recording; }

    // Closes and submits the open list, then signals the fence. No-op when
    // nothing is being recorded.
    bool Submit();

    // Blocks until the last submitted list retired.
    void WaitIdle();

    // Inserts a fresh fence into the shared command queue and waits for it.
    // Unlike WaitIdle(), this also covers work queued after this context's
    // most recent Submit(), notably DXGI Present operations.
    bool WaitQueueIdle();

private:
    bool WaitForFence(u64 value, bool recordRasterBegin = false);
    ID3D12GraphicsCommandList* ResetList();

    ID3D12Device* Device = nullptr;
    ID3D12CommandQueue* Queue = nullptr;
    DX12::ComPtr<ID3D12CommandAllocator> Allocator;
    DX12::ComPtr<ID3D12GraphicsCommandList> List;
    DX12::ComPtr<ID3D12Fence> Fence;
    HANDLE FenceEvent = nullptr;
    u64 FenceValue = 0;
    u64 SubmittedValue = 0;
    bool Recording = false;
};

// Linear sub-allocator over a persistently mapped UPLOAD buffer. Texture and
// constant uploads bump-allocate from here; the renderer resets it once per
// frame after the GPU has retired the previous frame's list.
class DX12UploadRing
{
public:
    bool Init(const DX12Context& context, u64 size);
    void Shutdown();

    void Reset() noexcept { Head = 0; }

    // Returns a mapped CPU pointer plus the matching buffer offset, aligned to
    // `alignment`. Returns nullptr when the remaining space is insufficient;
    // callers use a retained spill upload rather than waiting mid-frame.
    void* Allocate(u64 size, u64 alignment, u64& outOffset) noexcept;

    [[nodiscard]] ID3D12Resource* GetBuffer() const noexcept { return Buffer.Get(); }
    [[nodiscard]] u64 GetCapacity() const noexcept { return Capacity; }

private:
    DX12::ComPtr<ID3D12Resource> Buffer;
    u8* Mapped = nullptr;
    u64 Capacity = 0;
    u64 Head = 0;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_CONTEXT_H
