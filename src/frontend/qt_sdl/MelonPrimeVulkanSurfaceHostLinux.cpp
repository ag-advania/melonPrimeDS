#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(__linux__) // scatter-budget-exempt: native Vulkan presentation surface, not input dispatch

#include "MelonPrimeVulkanSurfaceHostLinux.h"

#include <utility>

#include <QEvent>
#include <QGuiApplication>

namespace MelonPrime
{

VulkanSurfaceHostLinux::VulkanSurfaceHostLinux(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    // The panel and the top-level window own all input. A Vulkan presentation
    // surface that accepted focus or mouse events would split hotkey and aim
    // handling across two widgets.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);

    // WA_PaintOnScreen keeps Qt from touching a window it does not own, which
    // is what X11 wants between Vulkan presents. Native Wayland has no
    // supported on-screen paint path at all, so the attribute is not set there
    // -- paintEngine() below already keeps Qt out of this surface.
    if (QGuiApplication::platformName() == QStringLiteral("xcb"))
        setAttribute(Qt::WA_PaintOnScreen, true);

    // Created hidden: the panel maps this surface only when it is about to
    // present a Vulkan frame, and hides it again before Qt paints software
    // frames into the parent underneath.
    hide();
}

void VulkanSurfaceHostLinux::setNativeSurfaceChangedCallback(std::function<void()> callback)
{
    nativeSurfaceChanged = std::move(callback);
}

QPaintEngine* VulkanSurfaceHostLinux::paintEngine() const
{
    // Vulkan owns every pixel here. Returning null makes any stray QPainter
    // construction fail loudly instead of fighting the swapchain for buffers.
    return nullptr;
}

bool VulkanSurfaceHostLinux::event(QEvent* event)
{
    const QEvent::Type type = event->type();

    // Dispatch first: the visibility state the callback observes has to be the
    // post-event one, otherwise a show would republish a stale (hidden) handle.
    const bool handled = QWidget::event(event);

    switch (type)
    {
    case QEvent::WinIdChange:
    case QEvent::Show:
    case QEvent::Hide:
        // Qt's Wayland backend destroys a window's wl_surface when it is
        // hidden and creates a fresh one on the next show, without necessarily
        // reporting a WId change. Any VkSurfaceKHR built from the previous
        // handle is dead by then, so treat all three as a new generation and
        // let the owner rebuild its swapchain.
        ++generation;
        if (nativeSurfaceChanged)
            nativeSurfaceChanged();
        break;
    default:
        break;
    }

    return handled;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && __linux__; scatter-budget-exempt: native Vulkan presentation surface, not input dispatch
