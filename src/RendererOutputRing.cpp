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

#include "RendererOutputRing.h"

#include <cassert>

namespace melonDS
{

void RendererOutputRing::LeaseCounter::Release(void* opaque) noexcept
{
    auto* counter = static_cast<LeaseCounter*>(opaque);
    if (!counter)
        return;
    const u32 previous = counter->Refs.fetch_sub(1, std::memory_order_release);
    assert(previous > 0);
    (void)previous;
}

RendererOutputRing::RendererOutputRing(u32 slotCount)
    : Leases(slotCount != 0 ? new LeaseCounter[slotCount] : nullptr)
    , SlotCount(slotCount)
{
}

u64 RendererOutputRing::PublishNext(u32 slot) noexcept
{
    const u64 serial = NextSerial++;
    PublishedSlot = static_cast<int>(slot);
    return serial;
}

void RendererOutputRing::PublishReserved(u32 slot) noexcept
{
    ++NextSerial;
    PublishedSlot = static_cast<int>(slot);
}

u32 RendererOutputRing::TakeCursorSlot() noexcept
{
    if (SlotCount == 0)
        return InvalidSlot;
    const u32 slot = Cursor;
    Cursor = (Cursor + 1u) % SlotCount;
    return slot;
}

void RendererOutputRing::SetCursorAfter(u32 slot) noexcept
{
    if (SlotCount == 0)
        return;
    Cursor = (slot + 1u) % SlotCount;
}

u32 RendererOutputRing::FindFreeSlot(
    u32 startSlot, SlotReadyFn ready, void* userData) const noexcept
{
    if (SlotCount == 0)
        return InvalidSlot;
    for (u32 offset = 0; offset < SlotCount; ++offset)
    {
        const u32 candidate = (startSlot + offset) % SlotCount;
        // A leased slot is being read right now, and the published slot is
        // what a presenter would pick up next. Overwriting either is how a
        // half-composed frame reaches the screen.
        if (static_cast<int>(candidate) == PublishedSlot)
            continue;
        if (Leases[candidate].IsHeld())
            continue;
        if (ready && !ready(userData, candidate))
            continue;
        return candidate;
    }
    return InvalidSlot;
}

bool RendererOutputRing::IsLeased(u32 slot) const noexcept
{
    return slot < SlotCount && Leases[slot].IsHeld();
}

RendererOutputRing::LeaseCounter* RendererOutputRing::AcquireLease(u32 slot) noexcept
{
    if (slot >= SlotCount)
        return nullptr;
    LeaseCounter& counter = Leases[slot];
    counter.Refs.fetch_add(1, std::memory_order_relaxed);
    return &counter;
}

RendererOutputRing::LeaseCounter* RendererOutputRing::LeaseCounterFor(u32 slot) noexcept
{
    return slot < SlotCount ? &Leases[slot] : nullptr;
}

} // namespace melonDS
