#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(__linux__) // scatter-budget-exempt: native Vulkan presentation surface, not input dispatch

#include <cstdint>
#include <functional>

#include <QWidget>

namespace MelonPrime
{

// Dedicated native child surface for Linux Vulkan presentation.
//
// ScreenPanelVulkan used to hand its own native window to both Vulkan WSI and
// QPainter. That works while one of them is the only producer, but the panel
// alternates: Vulkan drives the swapchain during a match, and the software
// renderer's CpuBgra frames are painted by Qt outside one. Handing the same
// wl_surface back to Qt's backing store after Vulkan had committed buffers to
// it left the last presented Vulkan frame on screen -- the post-match recap
// never appeared even though the game and its audio had moved on.
//
// This widget owns a separate native window that only Vulkan ever draws into,
// stacked above the panel. The panel keeps Qt's ordinary backing store for its
// software output, and the two producers never contend for one surface.
//
// Input is deliberately not this widget's business: it takes no focus, is
// transparent to mouse events, and never carries the Wayland pointer lock
// (pointer constraints belong to the top-level window surface).
class VulkanSurfaceHostLinux final : public QWidget
{
public:
    explicit VulkanSurfaceHostLinux(QWidget* parent);

    // Invoked on the GUI thread after the underlying native surface may have
    // been replaced, so the owner can re-resolve and republish its handles.
    void setNativeSurfaceChangedCallback(std::function<void()> callback);

    // Bumped whenever the native surface may have been recreated. A
    // VkSurfaceKHR built under an older generation must not be reused.
    [[nodiscard]] std::uint64_t nativeGeneration() const noexcept { return generation; }

protected:
    QPaintEngine* paintEngine() const override;
    bool event(QEvent* event) override;

private:
    std::function<void()> nativeSurfaceChanged;
    std::uint64_t generation = 1;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && __linux__; scatter-budget-exempt: native Vulkan presentation surface, not input dispatch
