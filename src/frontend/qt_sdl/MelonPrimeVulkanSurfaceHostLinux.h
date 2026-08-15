/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_VULKAN_SURFACE_HOST_LINUX_H
#define MELONPRIME_VULKAN_SURFACE_HOST_LINUX_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(__linux__)  // scatter-budget-exempt: Linux-only native Vulkan surface host

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include <QWidget>

#include "MelonPrimeVulkanSurface.h"

namespace MelonPrime
{

// Coordinates the short GUI-side native-surface publication critical section
// with the emulation thread's presentation lease. It is deliberately not a
// frame mutex: beginFrame()/endFrame() only update a counter and copy state,
// while the Vulkan frame itself runs without holding Mutex.
class VulkanSurfaceLifecycle final
{
public:
    enum class State : std::uint8_t
    {
        Hidden,
        WaitingForNativeSurface,
        Ready,
        Bound,
        RetireRequested,
        Retiring,
        DestroySafe,
    };

    void publishSnapshot(
        const VulkanSurface::NativeWindowSnapshot& snapshot,
        bool valid);
    void requestRetire();
    [[nodiscard]] bool waitForDestroySafe(std::chrono::milliseconds timeout);

    // Returns false when a lifecycle transition has stopped new presentation.
    // The returned snapshot is immutable for the caller's frame lease.
    [[nodiscard]] bool beginFrame(VulkanSurface::NativeWindowSnapshot& snapshot);
    void endFrame();

    void markBound(std::uint64_t generation);
    void markSurfaceLost();
    void beginRetiring();
    void markPresenterRetired();

    [[nodiscard]] bool retireRequested() const;
    [[nodiscard]] State state() const;

private:
    mutable std::mutex Mutex;
    std::condition_variable RetireCondition;
    State StateValue = State::Hidden;
    VulkanSurface::NativeWindowSnapshot Snapshot;
    std::uint64_t BoundGeneration = 0;
    std::uint32_t ActiveFrames = 0;
    bool PresenterActive = false;
};

// A QWidget lifecycle notification is deliberately emitted after Qt has
// processed the event. The native handles in the snapshot therefore describe
// the post-event state, while Generation remains the identity even when
// Wayland gives the same QWidget/WId a different wl_surface.
class VulkanSurfaceHostLinux final : public QWidget
{
public:
    enum class LifecycleEvent
    {
        Show,
        Hide,
        WinIdChange,
        SurfaceCreated,
        SurfaceAboutToBeDestroyed,
    };

    using LifecycleCallback = std::function<void(
        LifecycleEvent event,
        const VulkanSurface::NativeWindowSnapshot& snapshot)>;

    explicit VulkanSurfaceHostLinux(
        QWidget* parent,
        LifecycleCallback callback,
        std::shared_ptr<VulkanSurfaceLifecycle> lifecycle);

    VulkanSurfaceHostLinux(const VulkanSurfaceHostLinux&) = delete;
    VulkanSurfaceHostLinux& operator=(const VulkanSurfaceHostLinux&) = delete;

    [[nodiscard]] std::uint64_t nativeGeneration() const noexcept
    {
        return Generation;
    }

protected:
    bool event(QEvent* event) override;
    QPaintEngine* paintEngine() const override { return nullptr; }

private:
    [[nodiscard]] VulkanSurface::NativeWindowSnapshot captureSnapshot() const;
    void notifyLifecycle(
        LifecycleEvent event,
        const VulkanSurface::NativeWindowSnapshot& snapshot);

    LifecycleCallback Callback;
    std::shared_ptr<VulkanSurfaceLifecycle> Lifecycle;
    std::uint64_t Generation = 0;
    // QWidget::event(Show) can synchronously deliver PlatformSurface and
    // WinIdChange back to this widget while the outer event is still being
    // processed. Keep the guard so nested lifecycle notifications do not
    // issue a second retire handshake or publish an inconsistent transition.
    bool HandlingLifecycleEvent = false;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && __linux__  // scatter-budget-exempt: closing guard of the Linux-only host
#endif // MELONPRIME_VULKAN_SURFACE_HOST_LINUX_H
