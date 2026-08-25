/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// Protocol tests for the presentation slot ring shared by the Vulkan and DX12
// compositors.
//
// This ring decides which slot a frame is composed into and which slot a
// presenter is allowed to read. Getting it wrong does not crash: it shows a
// half-composed frame, or a frame from two frames ago, on someone's screen.
// That is exactly the class of bug that per-frame visual comparison is needed
// to catch, so the rules are pinned down here where they can be checked
// without a GPU.

#include <cstdio>

#include "RendererOutputRing.h"

using melonDS::RendererOutputRing;

namespace
{

int gFailures = 0;

void Check(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    ++gFailures;
}

// Backend readiness stub. The ring asks "has the GPU finished with this slot";
// here the answer is whatever the test says it is.
struct Readiness
{
    bool Ready[4]{true, true, true, true};
};

bool ReadyFn(void* userData, melonDS::u32 slot)
{
    return static_cast<Readiness*>(userData)->Ready[slot];
}

void TestInitialState()
{
    RendererOutputRing ring(3);
    const auto lock = ring.LockPublication();
    Check(ring.GetSlotCount() == 3, "the ring reports its slot count");
    Check(ring.GetPublishedSlot() == -1, "a fresh ring has published nothing");
    Check(ring.PeekNextSerial() == 1, "serials start at 1");
    Check(!ring.IsLeased(0) && !ring.IsLeased(1) && !ring.IsLeased(2),
        "a fresh ring holds no leases");
}

void TestSerialSequence()
{
    RendererOutputRing ring(3);
    const auto lock = ring.LockPublication();

    Check(ring.PublishNext(0) == 1, "the first publication takes serial 1");
    Check(ring.GetPublishedSlot() == 0, "publishing sets the published slot");
    Check(ring.PublishNext(1) == 2, "serials advance by one");
    Check(ring.GetPublishedSlot() == 1, "the published slot follows");

    // The native path reserves a serial before the work carrying it is
    // recorded, then commits only if that work reached a presentable slot.
    const melonDS::u64 reserved = ring.PeekNextSerial();
    Check(reserved == 3, "peeking does not consume a serial");
    Check(ring.PeekNextSerial() == 3, "peeking twice returns the same serial");
    ring.PublishReserved(2);
    Check(ring.GetPublishedSlot() == 2, "committing publishes the slot");
    Check(ring.PeekNextSerial() == 4, "committing consumes the reserved serial");

    // Dropping a frame must not renumber the ones after it: the presenter's
    // monotonic gate compares serials across renderer transitions.
    ring.Unpublish();
    Check(ring.GetPublishedSlot() == -1, "unpublishing drops the frame");
    Check(ring.PeekNextSerial() == 4, "unpublishing does not rewind the serial");
}

void TestFreeSlotSkipsPublished()
{
    RendererOutputRing ring(3);
    Readiness readiness;
    const auto lock = ring.LockPublication();

    ring.PublishNext(1);
    const melonDS::u32 slot = ring.FindFreeSlot(1, &ReadyFn, &readiness);
    Check(slot != 1, "the published slot is never handed out for writing");
    Check(slot == 2, "the scan wraps forward from the requested start");
}

void TestFreeSlotSkipsLeased()
{
    RendererOutputRing ring(3);
    Readiness readiness;
    const auto lock = ring.LockPublication();

    ring.PublishNext(0);
    auto* lease = ring.AcquireLease(0);
    Check(lease != nullptr, "leasing the published slot returns a counter");
    Check(ring.IsLeased(0), "the leased slot reports as held");

    // Slot 0 is both published and leased; 1 is busy on the GPU.
    readiness.Ready[1] = false;
    Check(ring.FindFreeSlot(0, &ReadyFn, &readiness) == 2,
        "the scan skips published, leased and busy slots");

    readiness.Ready[2] = false;
    Check(ring.FindFreeSlot(0, &ReadyFn, &readiness) == RendererOutputRing::InvalidSlot,
        "a fully occupied ring reports backpressure rather than reusing a slot");

    // Releasing the lease frees the slot again -- but it is still published,
    // so it stays off limits until something else is published.
    RendererOutputRing::LeaseCounter::Release(lease);
    Check(!ring.IsLeased(0), "releasing a lease clears the slot");
    Check(ring.FindFreeSlot(0, &ReadyFn, &readiness) == RendererOutputRing::InvalidSlot,
        "a released but still-published slot is not reused");

    readiness.Ready[1] = true;
    Check(ring.FindFreeSlot(0, &ReadyFn, &readiness) == 1,
        "a slot becomes available once its GPU work retires");
}

void TestNestedLeases()
{
    RendererOutputRing ring(3);
    const auto lock = ring.LockPublication();

    ring.PublishNext(2);
    auto* first = ring.AcquireLease(2);
    auto* second = ring.AcquireLease(2);
    Check(first == second, "leases on one slot share a counter");
    Check(ring.IsLeased(2), "the slot is held");

    RendererOutputRing::LeaseCounter::Release(first);
    Check(ring.IsLeased(2), "one release does not free a doubly-held slot");
    RendererOutputRing::LeaseCounter::Release(second);
    Check(!ring.IsLeased(2), "the last release frees the slot");
}

void TestCursor()
{
    RendererOutputRing ring(3);
    const auto lock = ring.LockPublication();

    Check(ring.GetCursor() == 0, "the cursor starts at slot 0");
    Check(ring.TakeCursorSlot() == 0, "taking returns the current slot");
    Check(ring.TakeCursorSlot() == 1, "the cursor advances");
    Check(ring.TakeCursorSlot() == 2, "the cursor advances");
    Check(ring.TakeCursorSlot() == 0, "the cursor wraps");

    ring.SetCursorAfter(1);
    Check(ring.GetCursor() == 2, "the cursor can be parked after a slot");
    ring.SetCursorAfter(2);
    Check(ring.GetCursor() == 0, "parking wraps too");
}

void TestDegenerateRing()
{
    // Not a configuration the renderers create, but the ring must not index
    // out of an empty array if one ever does.
    RendererOutputRing ring(0);
    Readiness readiness;
    const auto lock = ring.LockPublication();
    Check(ring.TakeCursorSlot() == RendererOutputRing::InvalidSlot,
        "an empty ring hands out no slot");
    Check(ring.FindFreeSlot(0, &ReadyFn, &readiness) == RendererOutputRing::InvalidSlot,
        "an empty ring finds no free slot");
    Check(!ring.IsLeased(0), "an empty ring has nothing leased");
    Check(ring.AcquireLease(0) == nullptr, "an empty ring leases nothing");
    RendererOutputRing::LeaseCounter::Release(nullptr);
}

} // namespace

int main()
{
    TestInitialState();
    TestSerialSequence();
    TestFreeSlotSkipsPublished();
    TestFreeSlotSkipsLeased();
    TestNestedLeases();
    TestCursor();
    TestDegenerateRing();

    if (gFailures != 0)
    {
        std::printf("renderer-output-ring-tests: FAIL (%d)\n", gFailures);
        return 1;
    }
    std::printf("renderer-output-ring-tests: PASS\n");
    return 0;
}
