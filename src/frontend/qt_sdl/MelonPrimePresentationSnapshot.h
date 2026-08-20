/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_PRESENTATION_SNAPSHOT_H
#define MELONPRIME_PRESENTATION_SNAPSHOT_H

#include <atomic>
#include <cstdint>

namespace MelonPrime
{

// Plain data published by a QWidget-owning GUI thread and consumed by the
// emulation/presenter thread. NativeHandle is the child surface's identity
// (HWND on Windows, WId/NSView where applicable). IdentityGeneration is the
// native-surface identity even when the window system reuses the numeric
// handle. GeometryRevision is deliberately separate: a resize, DPI change, or
// fullscreen transition must not make the presenter destroy and recreate the
// native surface.
struct NativeSurfaceSnapshot
{
    std::uintptr_t NativeHandle = 0;
    // Optional X11 display handles captured on the GUI thread for BSD/XCB.
    // They remain zero on Windows, macOS, and Linux, where the platform
    // adapter owns a richer native-surface lifecycle contract.
    std::uintptr_t XcbConnection = 0;
    std::uintptr_t XlibDisplay = 0;
    std::uint64_t IdentityGeneration = 0;
    std::uint64_t GeometryRevision = 0;
    std::uint32_t LogicalWidth = 0;
    std::uint32_t LogicalHeight = 0;
    std::uint32_t PhysicalWidth = 0;
    std::uint32_t PhysicalHeight = 0;
    bool Fullscreen = false;
    bool Valid = false;
};

// A small atomic-field snapshot. A sequence number is written around the
// fields so a consumer never combines an old handle with a new extent. The
// payload itself is never read concurrently; every field is atomic, which
// keeps this C++17 implementation free of seqlock data races.
class NativeSurfaceSnapshotStore
{
public:
    void Publish(const NativeSurfaceSnapshot& snapshot) noexcept
    {
        const std::uint64_t odd = Sequence.load(std::memory_order_relaxed) + 1u;
        Sequence.store(odd, std::memory_order_release);
        NativeHandle.store(snapshot.NativeHandle, std::memory_order_relaxed);
        XcbConnection.store(snapshot.XcbConnection, std::memory_order_relaxed);
        XlibDisplay.store(snapshot.XlibDisplay, std::memory_order_relaxed);
        IdentityGeneration.store(snapshot.IdentityGeneration, std::memory_order_relaxed);
        GeometryRevision.store(snapshot.GeometryRevision, std::memory_order_relaxed);
        LogicalWidth.store(snapshot.LogicalWidth, std::memory_order_relaxed);
        LogicalHeight.store(snapshot.LogicalHeight, std::memory_order_relaxed);
        PhysicalWidth.store(snapshot.PhysicalWidth, std::memory_order_relaxed);
        PhysicalHeight.store(snapshot.PhysicalHeight, std::memory_order_relaxed);
        Fullscreen.store(snapshot.Fullscreen ? 1u : 0u, std::memory_order_relaxed);
        Valid.store(snapshot.Valid ? 1u : 0u, std::memory_order_relaxed);
        Sequence.store(odd + 1u, std::memory_order_release);
    }

    [[nodiscard]] bool Read(NativeSurfaceSnapshot& snapshot) const noexcept
    {
        for (unsigned attempt = 0; attempt < 8; ++attempt)
        {
            const std::uint64_t before = Sequence.load(std::memory_order_acquire);
            if ((before & 1u) != 0)
                continue;

            NativeSurfaceSnapshot candidate;
            candidate.NativeHandle = NativeHandle.load(std::memory_order_relaxed);
            candidate.XcbConnection = XcbConnection.load(std::memory_order_relaxed);
            candidate.XlibDisplay = XlibDisplay.load(std::memory_order_relaxed);
            candidate.IdentityGeneration = IdentityGeneration.load(std::memory_order_relaxed);
            candidate.GeometryRevision = GeometryRevision.load(std::memory_order_relaxed);
            candidate.LogicalWidth = LogicalWidth.load(std::memory_order_relaxed);
            candidate.LogicalHeight = LogicalHeight.load(std::memory_order_relaxed);
            candidate.PhysicalWidth = PhysicalWidth.load(std::memory_order_relaxed);
            candidate.PhysicalHeight = PhysicalHeight.load(std::memory_order_relaxed);
            candidate.Fullscreen = Fullscreen.load(std::memory_order_relaxed) != 0;
            candidate.Valid = Valid.load(std::memory_order_relaxed) != 0;

            const std::uint64_t after = Sequence.load(std::memory_order_acquire);
            if (before == after && (after & 1u) == 0)
            {
                snapshot = candidate;
                return true;
            }
        }
        return false;
    }

private:
    std::atomic<std::uint64_t> Sequence{0};
    std::atomic<std::uintptr_t> NativeHandle{0};
    std::atomic<std::uintptr_t> XcbConnection{0};
    std::atomic<std::uintptr_t> XlibDisplay{0};
    std::atomic<std::uint64_t> IdentityGeneration{0};
    std::atomic<std::uint64_t> GeometryRevision{0};
    std::atomic<std::uint32_t> LogicalWidth{0};
    std::atomic<std::uint32_t> LogicalHeight{0};
    std::atomic<std::uint32_t> PhysicalWidth{0};
    std::atomic<std::uint32_t> PhysicalHeight{0};
    std::atomic<std::uint32_t> Fullscreen{0};
    std::atomic<std::uint32_t> Valid{0};
};

} // namespace MelonPrime

#endif // MELONPRIME_PRESENTATION_SNAPSHOT_H
