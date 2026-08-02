#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(__APPLE__) // scatter-budget-exempt: macOS Vulkan presentation layer owner, not input dispatch

// MelonPrime macOS Vulkan presentation surface (MoltenVK).
//
// Vulkan on macOS runs through MoltenVK, whose only presentable surface is a
// CAMetalLayer. This header exposes the plain-C++ half of that adapter so
// Screen.cpp (a .cpp translation unit with no Objective-C) can own the layer
// lifecycle; the AppKit/QuartzCore half lives in MelonPrimeVulkanSurfaceMacOS.mm.
//
// The layer is added as a sublayer of the panel's own NSView. Vulkan and Metal
// never share a layer: each renderer builds its own panel, so the two backends
// coexist without touching each other's presentation state.
//
// Threading: the layer functions below mutate AppKit/CoreAnimation objects and
// must be called on the GUI thread. RunInAutoreleasePool() is the exception and
// is safe anywhere. VkSurfaceKHR creation (through CreateVulkanSurface() in
// MelonPrimeVulkanSurface.h) may also run off the GUI thread, because MoltenVK
// only records the layer pointer there.

#include <functional>

namespace MelonPrime::VulkanMacOS
{

// Runs body inside an @autoreleasepool.
//
// MoltenVK hands back autoreleased Objective-C objects (Metal drawables,
// command buffers) from the Vulkan entry points the presenter calls every
// frame. The emulation thread is a plain QThread with no run loop, so it has
// no pool of its own and those temporaries would accumulate for the lifetime
// of the session. Wrapping one frame keeps the lifetime bounded.
//
// Safe to call from any thread.
void RunInAutoreleasePool(const std::function<void()>& body);

// Creates a CAMetalLayer suitable for MoltenVK presentation (when
// existingLayer is null) and adds it as a sublayer of the NSView behind
// nativeViewHandle, which is QWidget::winId() cast to void*.
//
// It is a *sublayer*, not the view's own hosting layer, on purpose. Replacing
// the view's layer would leave Qt with nothing to paint into, and the panel
// still needs QPainter for the splash screen, the software-rendered screens,
// and the OSD. As a sublayer, Vulkan composites above Qt's own drawing and
// neither backend disturbs the other. Giving Vulkan a separate native child
// widget would work too, but a second NSView takes part in AppKit hit testing
// and can be handed mouse events after its QPlatformWindow is gone.
//
// Qt recreates the native view across fullscreen transitions and screen
// changes, so this is safe to call repeatedly: the same layer is re-parented
// rather than replaced, which keeps any VkSurfaceKHR created from it valid.
// Returns the retained layer to store in VulkanNativeWindowInfo::window, or
// nullptr when no NSView is available yet.
void* CreateOrAttachLayer(void* existingLayer, void* nativeViewHandle);

// Applies the layer's position within the host view and its backing-store
// scale. Passing the logical size and the device pixel ratio keeps the
// MoltenVK swapchain extent equal to the surface extent, so the presenter
// never has to rescale.
void UpdateLayerGeometry(void* layer, double contentsScale, int widthPoints, int heightPoints);

// Shows or hides the Vulkan output. While hidden, the panel's own Qt painting
// is what the user sees.
void SetLayerHidden(void* layer, bool hidden);

// Detaches the layer from its host view and releases the reference taken by
// CreateOrAttachLayer(). Call only after the VkSurfaceKHR created from the
// layer has been destroyed.
void DestroyLayer(void* layer);

} // namespace MelonPrime::VulkanMacOS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && __APPLE__; scatter-budget-exempt: macOS Vulkan presentation layer owner, not input dispatch
