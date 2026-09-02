// MelonPrimeDS - native Wayland relative pointer + pointer lock support.

#include "MelonPrimeWaylandPointerLock.h"

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)

#include <wayland-client.h>

#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "MelonPrimeThreadBridge.h"
#include "MelonPrimeWaylandPointerLockMath.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace MelonPrime {

struct WaylandPointerLock::Impl
{
    explicit Impl(MelonPrimeThreadBridge* target) : deltaTarget(target) {}

    // Borrowed GUI-thread target. RelativeMotion runs on the thread that
    // dispatches the borrowed Wayland display and never resolves policy or
    // core ownership on the event-hot path.
    MelonPrimeThreadBridge* deltaTarget = nullptr;

    wl_display* display = nullptr;            // borrowed from Qt
    wl_registry* registry = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;

    zwp_relative_pointer_manager_v1* relativeManager = nullptr;
    zwp_pointer_constraints_v1* pointerConstraints = nullptr;
    zwp_relative_pointer_v1* relativePointer = nullptr;
    zwp_locked_pointer_v1* lockedPointer = nullptr;
    wl_surface* lockedSurface = nullptr;       // borrowed from Qt

    bool initialized = false;
    bool supported = false;
    bool lockRequested = false;
    bool lockActive = false;
    bool supportLogEmitted = false;

    std::int64_t residualX256 = 0;
    std::int64_t residualY256 = 0;

    // Cursor position hint (surface-local, see set_cursor_position_hint in
    // the pointer-constraints protocol), refreshed on every enabled=true
    // SetLocked() call and reused as the release hint by DestroyLockObjects().
    double hintX = 0.0;
    double hintY = 0.0;

    static void RegistryGlobal(
        void* data,
        wl_registry* registryObject,
        std::uint32_t name,
        const char* interface,
        std::uint32_t version)
    {
        auto& self = *static_cast<Impl*>(data);

        if (std::strcmp(interface, wl_seat_interface.name) == 0 && !self.seat)
        {
            const std::uint32_t bindVersion = std::min<std::uint32_t>(version, 7u);
            self.seat = static_cast<wl_seat*>(
                wl_registry_bind(registryObject, name, &wl_seat_interface, bindVersion));
            if (self.seat)
                wl_seat_add_listener(self.seat, &SeatListener, &self);
            return;
        }

        if (std::strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0
            && !self.relativeManager)
        {
            self.relativeManager = static_cast<zwp_relative_pointer_manager_v1*>(
                wl_registry_bind(
                    registryObject,
                    name,
                    &zwp_relative_pointer_manager_v1_interface,
                    std::min<std::uint32_t>(version, 1u)));
            return;
        }

        if (std::strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0
            && !self.pointerConstraints)
        {
            self.pointerConstraints = static_cast<zwp_pointer_constraints_v1*>(
                wl_registry_bind(
                    registryObject,
                    name,
                    &zwp_pointer_constraints_v1_interface,
                    std::min<std::uint32_t>(version, 1u)));
        }
    }

    static void RegistryGlobalRemove(void*, wl_registry*, std::uint32_t)
    {
        // Bound protocol objects remain valid until explicitly destroyed. A
        // compositor normally exposes these globals for the whole connection.
    }

    static void SeatCapabilities(void* data, wl_seat* seatObject, std::uint32_t capabilities)
    {
        auto& self = *static_cast<Impl*>(data);
        const bool hasPointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;

        if (hasPointer && !self.pointer)
        {
            self.pointer = wl_seat_get_pointer(seatObject);
            if (self.pointer)
                wl_pointer_add_listener(self.pointer, &PointerListener, &self);
        }

        if (!hasPointer && self.pointer)
        {
            self.DestroyLockObjects();
            if (wl_proxy_get_version(reinterpret_cast<wl_proxy*>(self.pointer))
                >= WL_POINTER_RELEASE_SINCE_VERSION)
            {
                wl_pointer_release(self.pointer);
            }
            else
            {
                wl_pointer_destroy(self.pointer);
            }
            self.pointer = nullptr;
        }

        // MELONPRIME_LINUX_MOUSE_INPUT_HARDENING_V2:
        // A seat can lose and later regain pointer capability after hotplug.
        self.supported =
            self.seat
            && self.pointer
            && self.relativeManager
            && self.pointerConstraints;
    }

    static void SeatName(void*, wl_seat*, const char*) {}

    // The relative-pointer object is associated with this wl_pointer. The
    // compositor may still emit ordinary pointer events (buttons, axes, focus),
    // so install a complete no-op listener instead of leaving event slots null.
    static void PointerEnter(
        void*, wl_pointer*, std::uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t) {}
    static void PointerLeave(void*, wl_pointer*, std::uint32_t, wl_surface*) {}
    static void PointerMotion(
        void*, wl_pointer*, std::uint32_t, wl_fixed_t, wl_fixed_t) {}
    static void PointerButton(
        void*, wl_pointer*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {}
    static void PointerAxis(
        void*, wl_pointer*, std::uint32_t, std::uint32_t, wl_fixed_t) {}
#if defined(WL_POINTER_FRAME_SINCE_VERSION)
    static void PointerFrame(void*, wl_pointer*) {}
#endif
#if defined(WL_POINTER_AXIS_SOURCE_SINCE_VERSION)
    static void PointerAxisSource(void*, wl_pointer*, std::uint32_t) {}
#endif
#if defined(WL_POINTER_AXIS_STOP_SINCE_VERSION)
    static void PointerAxisStop(
        void*, wl_pointer*, std::uint32_t, std::uint32_t) {}
#endif
#if defined(WL_POINTER_AXIS_DISCRETE_SINCE_VERSION)
    static void PointerAxisDiscrete(
        void*, wl_pointer*, std::uint32_t, std::int32_t) {}
#endif
#if defined(WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
    static void PointerAxisValue120(
        void*, wl_pointer*, std::uint32_t, std::int32_t) {}
#endif
#if defined(WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION)
    static void PointerAxisRelativeDirection(
        void*, wl_pointer*, std::uint32_t, std::uint32_t) {}
#endif

    static void RelativeMotion(
        void* data,
        zwp_relative_pointer_v1*,
        std::uint32_t,
        std::uint32_t,
        wl_fixed_t dx,
        wl_fixed_t dy,
        wl_fixed_t dxUnaccelerated,
        wl_fixed_t dyUnaccelerated)
    {
        auto& self = *static_cast<Impl*>(data);
        if (!self.lockActive || !self.deltaTarget)
            return;

        // Match the existing XInput2 path by preferring non-accelerated motion.
        // Some compositors report zero for both non-accelerated components, so
        // retain accelerated motion as a compatibility fallback.
        const bool haveUnaccelerated =
            dxUnaccelerated != 0 || dyUnaccelerated != 0;
        const auto sourceX = haveUnaccelerated ? dxUnaccelerated : dx;
        const auto sourceY = haveUnaccelerated ? dyUnaccelerated : dy;

        const std::int32_t outX = TakeWlFixedIntegral(
            self.residualX256, static_cast<std::int32_t>(sourceX));
        const std::int32_t outY = TakeWlFixedIntegral(
            self.residualY256, static_cast<std::int32_t>(sourceY));
        if ((outX | outY) != 0)
            self.deltaTarget->AddPanelAimDeltaFromGui(outX, outY);
    }

    static void Locked(void* data, zwp_locked_pointer_v1*)
    {
        auto& self = *static_cast<Impl*>(data);
        self.lockActive = true;
        if (std::getenv("MELONPRIME_WAYLAND_LOCK_DEBUG"))
            std::fprintf(stderr, "[MelonPrime] Wayland pointer lock: ENGAGED surface=%p\n",
                static_cast<void*>(self.lockedSurface));
    }

    static void Unlocked(void* data, zwp_locked_pointer_v1*)
    {
        auto& self = *static_cast<Impl*>(data);
        self.lockActive = false;
        if (std::getenv("MELONPRIME_WAYLAND_LOCK_DEBUG"))
            std::fprintf(stderr, "[MelonPrime] Wayland pointer lock: released (compositor Unlocked event) surface=%p\n",
                static_cast<void*>(self.lockedSurface));
    }

    static const wl_registry_listener RegistryListener;
    static const wl_seat_listener SeatListener;
    static const wl_pointer_listener PointerListener;
    static const zwp_relative_pointer_v1_listener RelativePointerListener;
    static const zwp_locked_pointer_v1_listener LockedPointerListener;

    void DestroyLockObjects()
    {
        if (lockRequested && std::getenv("MELONPRIME_WAYLAND_LOCK_DEBUG"))
            std::fprintf(stderr,
                "[MelonPrime] Wayland pointer lock: destroying lock objects (was surface=%p active=%d hint=%.1f,%.1f)\n",
                static_cast<void*>(lockedSurface), lockActive ? 1 : 0, hintX, hintY);

        // Tell the compositor where to warp the (invisible) cursor once this
        // lock releases -- per the protocol, this is the mechanism meant to
        // "avoid pointer jumps" on unlock. Without it the cursor stays
        // wherever it was frozen (often near a window edge from before the
        // lock engaged), so a later re-lock's brief unlocked gap can let it
        // escape the window on the very next fast motion. Must happen before
        // lockedPointer/lockedSurface are torn down, and needs a commit on
        // the locked surface for the double-buffered hint to take effect.
        if (lockedPointer && lockedSurface)
        {
            zwp_locked_pointer_v1_set_cursor_position_hint(
                lockedPointer, wl_fixed_from_double(hintX), wl_fixed_from_double(hintY));
            wl_surface_commit(lockedSurface);
        }

        lockActive = false;
        lockRequested = false;
        lockedSurface = nullptr;
        residualX256 = 0;
        residualY256 = 0;

        if (lockedPointer)
        {
            zwp_locked_pointer_v1_destroy(lockedPointer);
            lockedPointer = nullptr;
        }
        if (relativePointer)
        {
            zwp_relative_pointer_v1_destroy(relativePointer);
            relativePointer = nullptr;
        }

        if (display)
            wl_display_flush(display);
    }

    void Shutdown()
    {
        DestroyLockObjects();

        if (pointer)
        {
            if (wl_proxy_get_version(reinterpret_cast<wl_proxy*>(pointer))
                >= WL_POINTER_RELEASE_SINCE_VERSION)
            {
                wl_pointer_release(pointer);
            }
            else
            {
                wl_pointer_destroy(pointer);
            }
            pointer = nullptr;
        }

        if (seat)
        {
            if (wl_proxy_get_version(reinterpret_cast<wl_proxy*>(seat))
                >= WL_SEAT_RELEASE_SINCE_VERSION)
            {
                wl_seat_release(seat);
            }
            else
            {
                wl_seat_destroy(seat);
            }
            seat = nullptr;
        }

        if (relativeManager)
        {
            zwp_relative_pointer_manager_v1_destroy(relativeManager);
            relativeManager = nullptr;
        }
        if (pointerConstraints)
        {
            zwp_pointer_constraints_v1_destroy(pointerConstraints);
            pointerConstraints = nullptr;
        }
        if (registry)
        {
            wl_registry_destroy(registry);
            registry = nullptr;
        }

        if (display)
            wl_display_flush(display);

        display = nullptr;
        initialized = false;
        supported = false;
    }

    bool Initialize(wl_display* newDisplay)
    {
        if (initialized && display == newDisplay)
            return supported;

        Shutdown();
        if (!newDisplay)
            return false;

        display = newDisplay;
        registry = wl_display_get_registry(display);
        if (!registry)
        {
            Shutdown();
            return false;
        }

        wl_registry_add_listener(registry, &RegistryListener, this);

        // First roundtrip discovers globals; the second receives wl_seat
        // capabilities and creates the wl_pointer used by both protocols.
        if (wl_display_roundtrip(display) < 0 || wl_display_roundtrip(display) < 0)
        {
            std::fprintf(stderr,
                "[MelonPrime] Wayland pointer lock: registry initialization failed\n");
            Shutdown();
            return false;
        }

        initialized = true;
        supported = seat && pointer && relativeManager && pointerConstraints;

        if (!supportLogEmitted)
        {
            std::fprintf(stderr,
                supported
                    ? "[MelonPrime] Wayland relative pointer + pointer lock available\n"
                    : "[MelonPrime] Wayland compositor lacks relative-pointer or pointer-constraints; using Qt fallback\n");
            supportLogEmitted = true;
        }

        return supported;
    }

    bool SetLocked(wl_display* newDisplay, wl_surface* surface, bool enabled,
        int hintSurfaceX, int hintSurfaceY)
    {
        const bool debug = std::getenv("MELONPRIME_WAYLAND_LOCK_DEBUG") != nullptr;

        if (enabled)
        {
            hintX = static_cast<double>(hintSurfaceX);
            hintY = static_cast<double>(hintSurfaceY);
        }

        if (!enabled)
        {
            if (debug)
                std::fprintf(stderr, "[MelonPrime] Wayland pointer lock: SetLocked(false)\n");
            DestroyLockObjects();
            return true;
        }

        const bool initSupported = newDisplay && surface && Initialize(newDisplay);
        if (!initSupported)
        {
            if (debug)
                std::fprintf(stderr,
                    "[MelonPrime] Wayland pointer lock: SetLocked(true) rejected "
                    "(display=%p surface=%p)\n",
                    static_cast<void*>(newDisplay), static_cast<void*>(surface));
            return false;
        }

        if (lockRequested && lockedSurface == surface && lockedPointer && relativePointer)
            return true;

        if (debug)
            std::fprintf(stderr,
                "[MelonPrime] Wayland pointer lock: SetLocked(true) new request "
                "surface=%p (previous lockedSurface=%p lockRequested=%d)\n",
                static_cast<void*>(surface), static_cast<void*>(lockedSurface),
                lockRequested ? 1 : 0);

        DestroyLockObjects();

        relativePointer =
            zwp_relative_pointer_manager_v1_get_relative_pointer(relativeManager, pointer);
        if (!relativePointer)
            return false;
        zwp_relative_pointer_v1_add_listener(
            relativePointer, &RelativePointerListener, this);

        lockedPointer = zwp_pointer_constraints_v1_lock_pointer(
            pointerConstraints,
            surface,
            pointer,
            nullptr,
            ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        if (!lockedPointer)
        {
            zwp_relative_pointer_v1_destroy(relativePointer);
            relativePointer = nullptr;
            return false;
        }
        zwp_locked_pointer_v1_add_listener(
            lockedPointer, &LockedPointerListener, this);

        lockedSurface = surface;
        lockRequested = true;
        lockActive = false;
        residualX256 = 0;
        residualY256 = 0;
        wl_display_flush(display);
        return true;
    }
};

const wl_registry_listener WaylandPointerLock::Impl::RegistryListener = {
    &WaylandPointerLock::Impl::RegistryGlobal,
    &WaylandPointerLock::Impl::RegistryGlobalRemove,
};

const wl_seat_listener WaylandPointerLock::Impl::SeatListener = {
    &WaylandPointerLock::Impl::SeatCapabilities,
    &WaylandPointerLock::Impl::SeatName,
};

const wl_pointer_listener WaylandPointerLock::Impl::PointerListener = {
    &WaylandPointerLock::Impl::PointerEnter,
    &WaylandPointerLock::Impl::PointerLeave,
    &WaylandPointerLock::Impl::PointerMotion,
    &WaylandPointerLock::Impl::PointerButton,
    &WaylandPointerLock::Impl::PointerAxis,
#if defined(WL_POINTER_FRAME_SINCE_VERSION)
    &WaylandPointerLock::Impl::PointerFrame,
#endif
#if defined(WL_POINTER_AXIS_SOURCE_SINCE_VERSION)
    &WaylandPointerLock::Impl::PointerAxisSource,
#endif
#if defined(WL_POINTER_AXIS_STOP_SINCE_VERSION)
    &WaylandPointerLock::Impl::PointerAxisStop,
#endif
#if defined(WL_POINTER_AXIS_DISCRETE_SINCE_VERSION)
    &WaylandPointerLock::Impl::PointerAxisDiscrete,
#endif
#if defined(WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
    &WaylandPointerLock::Impl::PointerAxisValue120,
#endif
#if defined(WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION)
    &WaylandPointerLock::Impl::PointerAxisRelativeDirection,
#endif
};

const zwp_relative_pointer_v1_listener
WaylandPointerLock::Impl::RelativePointerListener = {
    &WaylandPointerLock::Impl::RelativeMotion,
};

const zwp_locked_pointer_v1_listener
WaylandPointerLock::Impl::LockedPointerListener = {
    &WaylandPointerLock::Impl::Locked,
    &WaylandPointerLock::Impl::Unlocked,
};

WaylandPointerLock::WaylandPointerLock(MelonPrimeThreadBridge* deltaTarget)
    : m_impl(std::make_unique<Impl>(deltaTarget))
{
}

WaylandPointerLock::~WaylandPointerLock()
{
    // MELONPRIME_LINUX_MOUSE_INPUT_HARDENING_V2
    if (m_impl)
        m_impl->Shutdown();
}

void WaylandPointerLock::setDeltaTarget(
    MelonPrimeThreadBridge* deltaTarget) noexcept
{
    m_impl->deltaTarget = deltaTarget;
}

bool WaylandPointerLock::setLocked(
    void* displayHandle,
    void* surfaceHandle,
    bool enabled,
    int hintSurfaceX,
    int hintSurfaceY)
{
    return m_impl->SetLocked(
        static_cast<wl_display*>(displayHandle),
        static_cast<wl_surface*>(surfaceHandle),
        enabled,
        hintSurfaceX,
        hintSurfaceY);
}

bool WaylandPointerLock::isLockRequested() const noexcept
{
    return m_impl->lockRequested;
}

bool WaylandPointerLock::isLockActive() const noexcept
{
    return m_impl->lockActive;
}

bool WaylandPointerLock::isSupported() const noexcept
{
    return m_impl->supported;
}

} // namespace MelonPrime

#endif // __linux__ && MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK
