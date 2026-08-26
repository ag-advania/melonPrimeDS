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

#ifndef DX12_COMMAND_CONTEXT_H
#define DX12_COMMAND_CONTEXT_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <array>
#include <chrono>
#include "DX12Common.h"
#include "GpuStageMetrics.h"

namespace melonDS
{

// One-command-list-per-frame recorder with fence-based CPU/GPU sync. The
// renderer records uploads and dispatches into the open list, submits once,
// and blocks on the fence only when the software 2D compositor actually asks
// for a scanline.
class DX12CommandContext
{
public:
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    static constexpr u32 TimestampQueryCount = GpuMetricQueryCount;
#endif

    bool Init(ID3D12Device* device, ID3D12CommandQueue* queue);
    void Shutdown();

    // Blocks until every previously submitted list finished, then opens a fresh
    // command list. Returns nullptr if the context is not initialized.
    //
    // `recordRasterBegin` is reserved for the main 3D renderer entry point.
    // Presenter/compositor command contexts use the default so their waits do
    // not contaminate the renderer's RasterBeginWait telemetry.
    ID3D12GraphicsCommandList* Begin(bool recordRasterBegin = false);

    // Opens the list only when its previous submission has already retired.
    // Never waits: compositor rings use this to drop a frame instead of
    // blocking VBlank when the GPU is more than their slot depth behind.
    ID3D12GraphicsCommandList* TryBegin();

    [[nodiscard]] ID3D12GraphicsCommandList* GetList() const noexcept { return List.Get(); }
    [[nodiscard]] bool IsRecording() const noexcept { return Recording; }
    [[nodiscard]] bool IsIdle() const noexcept;

    // Closes and submits the open list, then signals the fence. No-op when
    // nothing is being recorded.
    bool Submit();

    // Monotonic fence value of the most recent submitted list. Renderer
    // semantic capture provenance uses this to validate demand-driven
    // readback against the submission that produced the mirror.
    [[nodiscard]] u64 GetSubmittedValue() const noexcept
    {
        return SubmittedValue;
    }

    // Blocks until the last submitted list retired.
    void WaitIdle();

    // Blocks only on this context's most recently submitted fence. This is
    // the scoped completion wait used by demand-driven native capture
    // materialization; it never inserts a queue-wide/device-idle fence.
    [[nodiscard]] bool WaitForSubmittedValue() { return WaitForFence(SubmittedValue); }

    // Inserts a fresh fence into the shared command queue and waits for it.
    // Unlike WaitIdle(), this also covers work queued after this context's
    // most recent Submit(), notably DXGI Present operations.
    bool WaitQueueIdle();

    // Optional developer GPU timestamp seam. Query results belong to this
    // command context's fence, so a caller may read the previous submission
    // immediately after Begin()/TryBegin() has retired it and before the new
    // list is submitted.
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    [[nodiscard]] bool HasTimestampQueries() const noexcept
    {
        return TimestampQueriesEnabled && TimestampFrequency != 0;
    }
    void WriteTimestamp(u32 queryIndex) noexcept;
    [[nodiscard]] u64 ReadTimestampSpanNanoseconds(
        u32 startQuery, u32 endQuery) const noexcept;
#else
    [[nodiscard]] inline constexpr bool HasTimestampQueries() const noexcept
    {
        return false;
    }
    inline constexpr void WriteTimestamp(u32) noexcept {}
    [[nodiscard]] inline constexpr u64 ReadTimestampSpanNanoseconds(
        u32, u32) const noexcept
    {
        return 0;
    }
#endif

private:
    bool WaitForFence(u64 value, bool recordRasterBegin = false);
    ID3D12GraphicsCommandList* ResetList();
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    void RefreshTimestampFrequencyIfDue() noexcept;
    [[nodiscard]] bool ReadTimestampSnapshot() const noexcept;
#endif

    ID3D12Device* Device = nullptr;
    ID3D12CommandQueue* Queue = nullptr;
    DX12::ComPtr<ID3D12CommandAllocator> Allocator;
    DX12::ComPtr<ID3D12GraphicsCommandList> List;
    DX12::ComPtr<ID3D12Fence> Fence;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    DX12::ComPtr<ID3D12QueryHeap> TimestampQueryHeap;
    DX12::ComPtr<ID3D12Resource> TimestampReadback;
#endif
    HANDLE FenceEvent = nullptr;
    u64 FenceValue = 0;
    u64 SubmittedValue = 0;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    u64 TimestampFrequency = 0;
    std::chrono::steady_clock::time_point LastTimestampFrequencyRefresh{};
    u32 TimestampWrittenMask = 0;
    u32 LastTimestampWrittenMask = 0;
    mutable std::array<u64, TimestampQueryCount> TimestampSnapshotValues{};
    mutable bool TimestampSnapshotValid = false;
    bool TimestampQueriesEnabled = false;
#endif
    bool Recording = false;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_COMMAND_CONTEXT_H
