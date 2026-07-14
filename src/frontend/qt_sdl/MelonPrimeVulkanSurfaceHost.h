#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanSurfaceHost requires the Vulkan build gate"
#endif

#include <QPointer>
#include <QSize>
#include <QWidget>
#include <QtGlobal>
#include <volk.h>

#include "types.h"

enum class VulkanWindowSystem : quint8
{
    None = 0,
    Win32,
    Xcb,
    Xlib,
    Wayland,
    Metal,
};

class MelonPrimeVulkanSurfaceHost final
{
public:
    MelonPrimeVulkanSurfaceHost() = default;
    ~MelonPrimeVulkanSurfaceHost() = default;

    MelonPrimeVulkanSurfaceHost(const MelonPrimeVulkanSurfaceHost&) = delete;
    MelonPrimeVulkanSurfaceHost& operator=(const MelonPrimeVulkanSurfaceHost&) = delete;

    bool createForWidget(QWidget& widget, VkInstance instance);
    void destroy(VkInstance instance);
    [[nodiscard]] bool matchesWidget(QWidget& widget) const;
    [[nodiscard]] VkSurfaceKHR surface() const noexcept { return surfaceHandle; }
    [[nodiscard]] QSize pixelSize() const;
    [[nodiscard]] melonDS::u64 generation() const noexcept { return surfaceGeneration; }
    [[nodiscard]] VulkanWindowSystem windowSystem() const noexcept { return system; }

private:
    QPointer<QWidget> widget;
    VkSurfaceKHR surfaceHandle{VK_NULL_HANDLE};
    VulkanWindowSystem system{VulkanWindowSystem::None};
    quintptr nativeDisplayIdentity{};
    quintptr nativeWindowIdentity{};
    void* platformLayer{};
    melonDS::u64 surfaceGeneration{};
};
