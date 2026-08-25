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

#include <algorithm>
#include <array>
#include <chrono>
#include "DX12CommandContext.h"
#include "DX12Perf.h"
#include "Platform.h"

namespace melonDS
{

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
namespace
{
constexpr u32 kTimestampQueryCount = GpuMetricQueryCount;
constexpr auto kTimestampFrequencyRefreshInterval = std::chrono::seconds(1);
} // namespace
#endif

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
    (void)WaitForSubmittedValue();
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

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
