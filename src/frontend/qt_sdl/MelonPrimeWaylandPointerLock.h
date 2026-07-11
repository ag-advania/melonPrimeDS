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
    // Neither object is owned by this class.
    bool setLocked(void* displayHandle, void* surfaceHandle, bool enabled);

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
