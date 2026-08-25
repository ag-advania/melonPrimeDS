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

#ifndef RENDERER_OUTPUT_RING_H
#define RENDERER_OUTPUT_RING_H

#include <atomic>
#include <memory>
#include <mutex>

#include "types.h"

namespace melonDS
{

// The presentation slot ring shared by the Vulkan and DX12 compositors.
//
// Both backends published frames through the same protocol, written out twice:
// a published-slot index, a monotonic serial, a per-slot presenter refcount,
// one mutex covering all three, and a scan that skips the published slot, any
// slot a presenter still holds, and any slot whose GPU work has not retired.
// Only that last test is backend-specific -- "has the GPU finished with this
// slot" is a fence question on one backend and a command-list question on the
// other -- so it is the one thing this class asks the caller.
//
// Nothing here knows what a VkImage, an ID3D12Resource, a command buffer or a
// fence is, and it must stay that way. The frame descriptor a slot publishes
// stays with the backend that can describe it.
//
// Threading: the emulation thread produces and the presentation path leases.
// The publication state is guarded by one mutex; the per-slot refcounts are
// atomic so a lease can be released without taking it.
class RendererOutputRing
{
public:
    static constexpr u32 InvalidSlot = ~0u;

    // One slot's presenter refcount, with a stable address.
    //
    // A renderer output lease carries a single void* for its release
    // callback, so the thing being refcounted has to be addressable on its
    // own rather than reached as "ring plus index".
    class LeaseCounter
    {
    public:
        // Release callback for RendererOutputLease. `opaque` is the
        // LeaseCounter* handed out by RendererOutputRing::LeaseCounterFor().
        static void Release(void* opaque) noexcept;

        [[nodiscard]] bool IsHeld() const noexcept
        {
            return Refs.load(std::memory_order_acquire) != 0;
        }

    private:
        friend class RendererOutputRing;
        std::atomic<u32> Refs{0};
    };

    // "Has the GPU finished with this slot?" Answered by the backend, and
    // called with the publication lock held -- which is where the pre-refactor
    // code evaluated the same test, so the lock scope is unchanged.
    using SlotReadyFn = bool (*)(void* userData, u32 slot);

    explicit RendererOutputRing(u32 slotCount);

    RendererOutputRing(const RendererOutputRing&) = delete;
    RendererOutputRing& operator=(const RendererOutputRing&) = delete;

    [[nodiscard]] u32 GetSlotCount() const noexcept { return SlotCount; }

    // The publication critical section.
    //
    // The caller holds this while it also fills in its own backend frame
    // descriptor, so the descriptor and the published slot index change
    // together: a presenter must never observe a new slot index carrying the
    // previous frame's fields. Every method below marked "locked" requires it.
    [[nodiscard]] std::unique_lock<std::mutex> LockPublication() const
    {
        return std::unique_lock<std::mutex>(Mutex);
    }

    // locked. -1 when nothing has been published into this ring yet.
    [[nodiscard]] int GetPublishedSlot() const noexcept { return PublishedSlot; }

    // locked. Drops the published frame without disturbing the serial
    // sequence, so a later frame is still ordered after every earlier one.
    void Unpublish() noexcept { PublishedSlot = -1; }

    // locked. The serial the next publication will carry, without consuming
    // it. The native path needs the value before the work that carries it is
    // recorded, and commits only if that work reaches a presentable slot.
    [[nodiscard]] u64 PeekNextSerial() const noexcept { return NextSerial; }

    // locked. Consumes a serial and publishes `slot`. Returns the serial the
    // caller must store in its frame descriptor.
    u64 PublishNext(u32 slot) noexcept;

    // locked. Commits a serial previously taken from PeekNextSerial().
    void PublishReserved(u32 slot) noexcept;

    // locked. Round-robin cursor over the ring.
    //
    // Only one of the two production paths uses it: a compositor that always
    // takes the next index in turn, and reports backpressure when that one
    // index happens to be leased. The scanning path below is the other
    // strategy, and shares the cursor so the two do not fight over the ring.
    [[nodiscard]] u32 TakeCursorSlot() noexcept;
    [[nodiscard]] u32 GetCursor() const noexcept { return Cursor; }
    void SetCursorAfter(u32 slot) noexcept;

    // locked. First slot at or after `startSlot` that is neither published,
    // nor leased, nor still busy on the GPU. InvalidSlot when every slot is
    // occupied -- which is backpressure, not a failure.
    [[nodiscard]] u32 FindFreeSlot(
        u32 startSlot, SlotReadyFn ready, void* userData) const noexcept;

    // locked. Whether a presenter is still reading this slot.
    [[nodiscard]] bool IsLeased(u32 slot) const noexcept;

    // locked. Takes a presenter reference on `slot` and returns the counter
    // whose address the lease release callback will be given.
    [[nodiscard]] LeaseCounter* AcquireLease(u32 slot) noexcept;

    // The address a lease release callback is handed. Does not take a
    // reference; AcquireLease() does that.
    [[nodiscard]] LeaseCounter* LeaseCounterFor(u32 slot) noexcept;

private:
    mutable std::mutex Mutex;
    std::unique_ptr<LeaseCounter[]> Leases;
    u32 SlotCount = 0;
    int PublishedSlot = -1;
    u32 Cursor = 0;
    u64 NextSerial = 1;
};

} // namespace melonDS

#endif // RENDERER_OUTPUT_RING_H
