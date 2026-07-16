// MelonPrimeDS - native Wayland relative pointer + pointer lock support.

#include "MelonPrimeWaylandPointerLock.h"

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)

#include <wayland-client.h>

#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace MelonPrime {

namespace {

std::int32_t TakeIntegralDelta(double& residual, double value)
{
    residual += value;

    // Truncate toward zero while preserving the fractional remainder. This avoids
    // losing sub-pixel Wayland deltas without introducing a directional bias.
    const double integral = std::trunc(residual);
    residual -= integral;

    const double lo = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    const double hi = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    return static_cast<std::int32_t>(std::clamp(integral, lo, hi));
}

} // namespace

struct WaylandPointerLock::Impl
{
    explicit Impl(DeltaCallback cb) : callback(std::move(cb)) {}

    DeltaCallback callback;

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

    double residualX = 0.0;
    double residualY = 0.0;

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
        if (!self.lockActive || !self.callback)
            return;

        const double acceleratedX = wl_fixed_to_double(dx);
        const double acceleratedY = wl_fixed_to_double(dy);
        const double unacceleratedX = wl_fixed_to_double(dxUnaccelerated);
        const double unacceleratedY = wl_fixed_to_double(dyUnaccelerated);

        // Match the existing XInput2 path by preferring non-accelerated motion.
        // Some compositors report zero for both non-accelerated components, so
        // retain accelerated motion as a compatibility fallback.
        const bool haveUnaccelerated =
            unacceleratedX != 0.0 || unacceleratedY != 0.0;
        const double sourceX = haveUnaccelerated ? unacceleratedX : acceleratedX;
        const double sourceY = haveUnaccelerated ? unacceleratedY : acceleratedY;

        const std::int32_t outX = TakeIntegralDelta(self.residualX, sourceX);
        const std::int32_t outY = TakeIntegralDelta(self.residualY, sourceY);
        if ((outX | outY) != 0)
            self.callback(outX, outY);
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
                "[MelonPrime] Wayland pointer lock: destroying lock objects (was surface=%p active=%d)\n",
                static_cast<void*>(lockedSurface), lockActive ? 1 : 0);

        lockActive = false;
        lockRequested = false;
        lockedSurface = nullptr;
        residualX = 0.0;
        residualY = 0.0;

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

    bool SetLocked(wl_display* newDisplay, wl_surface* surface, bool enabled)
    {
        const bool debug = std::getenv("MELONPRIME_WAYLAND_LOCK_DEBUG") != nullptr;

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
        residualX = 0.0;
        residualY = 0.0;
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

WaylandPointerLock::WaylandPointerLock(DeltaCallback callback)
    : m_impl(std::make_unique<Impl>(std::move(callback)))
{
}

WaylandPointerLock::~WaylandPointerLock()
{
    // MELONPRIME_LINUX_MOUSE_INPUT_HARDENING_V2
    if (m_impl)
        m_impl->Shutdown();
}

bool WaylandPointerLock::setLocked(
    void* displayHandle,
    void* surfaceHandle,
    bool enabled)
{
    return m_impl->SetLocked(
        static_cast<wl_display*>(displayHandle),
        static_cast<wl_surface*>(surfaceHandle),
        enabled);
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
