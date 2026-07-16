#ifndef MELONPRIME_WAYLAND_POINTER_LOCK_H
#define MELONPRIME_WAYLAND_POINTER_LOCK_H

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)

#include <cstdint>
#include <functional>
#include <memory>

namespace MelonPrime {

class WaylandPointerLock final
{
public:
    using DeltaCallback = std::function<void(std::int32_t, std::int32_t)>;

    explicit WaylandPointerLock(DeltaCallback callback);
    ~WaylandPointerLock();

    WaylandPointerLock(const WaylandPointerLock&) = delete;
    WaylandPointerLock& operator=(const WaylandPointerLock&) = delete;

    // displayHandle must be wl_display*, surfaceHandle must be wl_surface*.
    // Neither object is owned by this class. hintSurfaceX/Y are the point
    // (in the locked surface's local coordinate space, e.g. the DS panel's
    // center mapped into the top-level window) the compositor should warp
    // the (invisible) cursor to whenever the lock is released -- this keeps
    // the cursor away from the window edge if a later re-lock exposes it
    // during a brief gap. Ignored when enabled is false; the most recent
    // hint from a prior enabled=true call is reused for that release.
    bool setLocked(void* displayHandle, void* surfaceHandle, bool enabled,
        int hintSurfaceX = 0, int hintSurfaceY = 0);

    [[nodiscard]] bool isLockRequested() const noexcept;
    [[nodiscard]] bool isLockActive() const noexcept;
    [[nodiscard]] bool isSupported() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace MelonPrime

#endif // __linux__ && MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK

#endif // MELONPRIME_WAYLAND_POINTER_LOCK_H
