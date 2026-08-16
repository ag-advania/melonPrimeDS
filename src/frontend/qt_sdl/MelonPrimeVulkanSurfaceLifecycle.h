/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_VULKAN_SURFACE_LIFECYCLE_H
#define MELONPRIME_VULKAN_SURFACE_LIFECYCLE_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "MelonPrimeVulkanSurface.h"

namespace MelonPrime
{

// Coordinates native-surface publication with the emulation thread's
// presentation lease. This is deliberately not a frame mutex:
// beginFrame()/endFrame() only update a counter and copy state, while the
// Vulkan frame itself runs without holding Mutex.
//
// The post-event publication API is the only operation that may make a
// snapshot presentation-ready. Snapshot, HostShown, and StateValue are
// updated under one lock so an emulation-thread reader can never observe
// HostShown=true paired with the previous generation's snapshot.
class VulkanSurfaceLifecycle final
{
public:
    enum class State : std::uint8_t
    {
        Hidden,
        WaitingForNativeSurface,
        WaitingForShow,
        Ready,
        Bound,
        RetireRequested,
        Retiring,
        DestroySafe,
    };

    void publishSnapshotState(
        const VulkanSurface::NativeWindowSnapshot& snapshot,
        bool valid,
        bool hostShown);
    void requestRetire();

    // Native QWidget transitions invalidate presentation eligibility before Qt
    // processes the event. A pre-Show snapshot may still be valid as a native
    // handle, but it must remain in WaitingForShow until the completed Show
    // snapshot is atomically published.
    void requestNativeTransitionRetire();

    // Timed waits remain available for non-destructive lifecycle diagnostics.
    // Native destruction uses the no-timeout overload below as a hard barrier.
    [[nodiscard]] bool waitForDestroySafe(std::chrono::milliseconds timeout);
    void waitForDestroySafe();

    // Returns false when a lifecycle transition has stopped new presentation.
    // The returned snapshot is immutable for the caller's frame lease.
    [[nodiscard]] bool beginFrame(VulkanSurface::NativeWindowSnapshot& snapshot);
    void endFrame();

    void markBound(std::uint64_t generation);
    void markSurfaceLost();
    void beginRetiring();
    void markPresenterRetired();

    [[nodiscard]] bool retireRequested() const;
    [[nodiscard]] bool presentationReady() const;
    [[nodiscard]] State state() const;

private:
    mutable std::mutex Mutex;
    std::condition_variable RetireCondition;
    State StateValue = State::Hidden;
    VulkanSurface::NativeWindowSnapshot Snapshot;
    std::uint64_t BoundGeneration = 0;
    std::uint32_t ActiveFrames = 0;
    // This is native presentation-object ownership, not merely GPU work:
    // true means the presenter still owns the VkSwapchainKHR/VkSurfaceKHR
    // pair that can reference the published native window.
    bool PresenterActive = false;
    bool HostShown = false;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // MELONPRIME_VULKAN_SURFACE_LIFECYCLE_H
