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

#include <cstdint>
#include <functional>
#include <memory>
#include <shared_mutex>

#include <QWidget>

#include "MelonPrimeVulkanSurface.h"

namespace MelonPrime
{

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
        std::shared_ptr<std::shared_mutex> lifecycleLock);

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
    std::shared_ptr<std::shared_mutex> LifecycleLock;
    std::uint64_t Generation = 0;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && __linux__  // scatter-budget-exempt: closing guard of the Linux-only host
#endif // MELONPRIME_VULKAN_SURFACE_HOST_LINUX_H
