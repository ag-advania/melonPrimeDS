#ifndef MELONPRIME_PLATFORM_INPUT_H
#define MELONPRIME_PLATFORM_INPUT_H

#include "MelonPrimePerfProbe.h"
#include "MelonPrimeInputSubscription.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <QCursor>

#if !defined(_WIN32)
#include <QGuiApplication>
#endif

#if defined(__APPLE__)
#include "MelonPrimeRawInputMacFilter.h"
#elif defined(__linux__)
#include "MelonPrimeRawInputLinuxFilter.h"
#endif

namespace MelonPrime {

#if defined(__APPLE__) || defined(__linux__)
#define MELONPRIME_PLATFORM_RAW_FILTER_ENABLED 1
#define MELONPRIME_RAW_FILTER_PTR(core) ((core)->m_platformRawFilter)
#define MELONPRIME_RAW_AIM_WAS_ACTIVE_PTR(core) (&(core)->m_platformRawAimWasActive)
#else
#define MELONPRIME_PLATFORM_RAW_FILTER_ENABLED 0
#define MELONPRIME_RAW_FILTER_PTR(core) (static_cast<void*>(nullptr))
#define MELONPRIME_RAW_AIM_WAS_ACTIVE_PTR(core) (static_cast<uint8_t*>(nullptr))
#endif

// Resolved once per UpdateInputState frame (V5 Phase 2).
enum class AimInputSource : uint8_t {
    WinRaw = 0,
    MacRaw,
    LinuxRaw,
    PanelDelta,
    QCursorFallback,
    None,
};

#if defined(__linux__)
inline bool PlatformInput_IsXcb()
{
    static const bool kIsXcb =
        QGuiApplication::platformName() == QStringLiteral("xcb");
    return kIsXcb;
}
#else
inline bool PlatformInput_IsXcb()
{
    return false;
}
#endif

#if defined(__APPLE__) || defined(__linux__)
#if defined(__APPLE__)
using PlatformRawFilter = MacRawInputFilter;
#else
using PlatformRawFilter = LinuxRawInputFilter;
#endif

inline bool PlatformInput_ShouldAcquireRawFilter()
{
#if defined(__APPLE__)
    return true;
#else
    return PlatformInput_IsXcb();
#endif
}

inline PlatformRawFilter* PlatformInput_AcquireRawFilter()
{
    return PlatformRawFilter::Acquire();
}

inline void PlatformInput_ReleaseRawFilter(PlatformRawFilter*& filter)
{
    if (!filter)
        return;

    PlatformRawFilter::Release();
    filter = nullptr;
}

inline bool PlatformInput_IsRawAvailable(const PlatformRawFilter* filter)
{
    return filter && filter->isAvailable();
}

inline bool PlatformInput_IsRawAimActive(const PlatformRawFilter* filter)
{
#if defined(__linux__)
    return filter && filter->isAvailable() && filter->hasReceivedMotion();
#else
    return filter && filter->isAvailable();
#endif
}

#if defined(__APPLE__)
inline bool PlatformInput_IsGcMouseAimActive(const PlatformRawFilter* filter)
{
    return filter && filter->isGcMouseActive();
}
#endif

inline void PlatformInput_FetchRawMouseDelta(PlatformRawFilter* filter,
                                             MelonPrimeInputSubscription& subscription,
                                             int32_t& outDx,
                                             int32_t& outDy)
{
    filter->fetchMouseDelta(subscription, outDx, outDy);
}

inline void PlatformInput_ResetRawFilter(
    PlatformRawFilter* filter, MelonPrimeInputSubscription& subscription)
{
    if (filter)
        filter->resetAll(subscription);
}

// Single resolution point for aim delta ownership (V5 Phase 2).
inline AimInputSource PlatformInput_ResolveAimSource(
    PlatformRawFilter* filter,
    MelonPrimeInputSubscription& subscription,
    bool hasPanel,
    bool& outHaveMouseDelta,
    int32_t& outDx,
    int32_t& outDy)
{
    outHaveMouseDelta = false;
    outDx = 0;
    outDy = 0;
    if (!PlatformInputOwnerService::IsOwner(subscription))
        return AimInputSource::None;

#if defined(__APPLE__)
    if (PlatformInput_IsRawAimActive(filter)) {
        PlatformInput_FetchRawMouseDelta(filter, subscription, outDx, outDy);
        outHaveMouseDelta = true;
        return AimInputSource::MacRaw;
    }
    return AimInputSource::QCursorFallback;
#else
    if (PlatformInput_IsRawAimActive(filter)) {
        PlatformInput_FetchRawMouseDelta(filter, subscription, outDx, outDy);
        outHaveMouseDelta = true;
        return AimInputSource::LinuxRaw;
    }
    if (hasPanel) {
        outHaveMouseDelta = true;
        return AimInputSource::PanelDelta;
    }
    return AimInputSource::QCursorFallback;
#endif
}

inline void PlatformInput_CountPerfAimSource(AimInputSource aimSrc)
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (!MelonPrimePerf::IsFrameActive())
        return;

    MelonPrimePerf::InputSource perfSrc =
        MelonPrimePerf::InputSource::QCursorFallback;
    switch (aimSrc) {
    case AimInputSource::MacRaw:
        perfSrc = MelonPrimePerf::InputSource::MacRaw;
        break;
    case AimInputSource::LinuxRaw:
        perfSrc = MelonPrimePerf::InputSource::LinuxRaw;
        break;
    case AimInputSource::PanelDelta:
        perfSrc = MelonPrimePerf::InputSource::PanelDelta;
        break;
    default:
        break;
    }
    MelonPrimePerf::CountInputSource(perfSrc);
#else
    (void)aimSrc;
#endif
}

// macOS/Linux aim delta path (V5 Phase 2 facade implementation).
template<typename AimPanel>
inline void PlatformInput_UpdateMouseDeltaMacLinux(
    PlatformRawFilter* filter,
    MelonPrimeInputSubscription& subscription,
    AimPanel* panel,
    uint8_t& platformRawAimWasActive,
    bool& haveMouseDelta,
    int32_t& mouseX,
    int32_t& mouseY,
    int32_t centerX,
    int32_t centerY)
{
    const bool hasPanel = (panel != nullptr);
    const AimInputSource aimSrc = PlatformInput_ResolveAimSource(
        filter, subscription, hasPanel, haveMouseDelta, mouseX, mouseY);

#if defined(__linux__)
    const bool rawActive = (aimSrc == AimInputSource::LinuxRaw);
    if (rawActive && !platformRawAimWasActive && panel)
        panel->resetAimMouseDelta();
    platformRawAimWasActive = rawActive ? 1 : 0;

    if (aimSrc == AimInputSource::PanelDelta)
        panel->getAimMouseDelta(mouseX, mouseY);

    {
        static const bool s_inputDbg =
            std::getenv("MELONPRIME_INPUT_DEBUG") != nullptr;
        if (s_inputDbg) {
            subscription.debugSumX += mouseX;
            subscription.debugSumY += mouseY;
            if (++subscription.debugFrames >= 60) {
                const char* srcName = "qcur";
                if (aimSrc == AimInputSource::LinuxRaw)
                    srcName = "raw";
                else if (aimSrc == AimInputSource::PanelDelta)
                    srcName = "panel";
                fprintf(stderr,
                    "[MelonPrime] linux aim: src=%s have=%d sum60=(%d,%d) "
                    "rawAvail=%d rawMotion=%d panel=%d\n",
                    srcName,
                    haveMouseDelta ? 1 : 0,
                    static_cast<int>(subscription.debugSumX),
                    static_cast<int>(subscription.debugSumY),
                    PlatformInput_IsRawAvailable(filter) ? 1 : 0,
                    PlatformInput_IsRawAimActive(filter) ? 1 : 0,
                    panel ? 1 : 0);
                subscription.debugSumX = subscription.debugSumY = 0;
                subscription.debugFrames = 0;
            }
        }
    }
#endif

    if (aimSrc != AimInputSource::None && !haveMouseDelta) {
#if defined(__linux__)
        if (!panel)
#endif
        {
            const QPoint currentPos = QCursor::pos();
            mouseX = currentPos.x() - centerX;
            mouseY = currentPos.y() - centerY;
        }
    }

    PlatformInput_CountPerfAimSource(aimSrc);
}

template<typename AimPanel>
inline void PlatformInput_ResetAfterLayoutWarpMacLinux(
    PlatformRawFilter* filter,
    MelonPrimeInputSubscription& subscription,
    AimPanel* panel)
{
    PlatformInput_ResetRawFilter(filter, subscription);
#if defined(__linux__)
    if (panel)
        panel->resetAimMouseDelta();
#endif
}

#endif // defined(__APPLE__) || defined(__linux__)

#if !defined(_WIN32)
inline bool PlatformInput_ShouldWarpCursorAfterAim(
    const void* filterOpaque)
{
#if defined(__linux__)
    (void)filterOpaque;
    return false;
#elif defined(__APPLE__)
    return !PlatformInput_IsRawAimActive(
        static_cast<const PlatformRawFilter*>(filterOpaque));
#else
    (void)filterOpaque;
    return true;
#endif
}

template<typename AimPanel>
inline void PlatformInput_UpdateMouseDelta(
    void* filterOpaque,
    MelonPrimeInputSubscription& subscription,
    AimPanel* panel,
    uint8_t* platformRawAimWasActive,
    bool& haveMouseDelta,
    int32_t& mouseX,
    int32_t& mouseY,
    int32_t centerX,
    int32_t centerY)
{
#if defined(__APPLE__) || defined(__linux__)
    uint8_t localWasActive =
        platformRawAimWasActive ? *platformRawAimWasActive : 0;
    PlatformInput_UpdateMouseDeltaMacLinux(
        static_cast<PlatformRawFilter*>(filterOpaque),
        subscription,
        panel,
        localWasActive,
        haveMouseDelta,
        mouseX,
        mouseY,
        centerX,
        centerY);
    if (platformRawAimWasActive)
        *platformRawAimWasActive = localWasActive;
#else
    (void)filterOpaque;
    (void)platformRawAimWasActive;
    (void)panel;
    if (!haveMouseDelta) {
        const QPoint currentPos = QCursor::pos();
        mouseX = currentPos.x() - centerX;
        mouseY = currentPos.y() - centerY;
    }
#endif
}

template<typename AimPanel>
inline void PlatformInput_ResetAfterLayoutWarp(
    void* filterOpaque,
    MelonPrimeInputSubscription& subscription,
    AimPanel* panel)
{
#if defined(__APPLE__) || defined(__linux__)
    PlatformInput_ResetAfterLayoutWarpMacLinux(
        static_cast<PlatformRawFilter*>(filterOpaque), subscription, panel);
#else
    (void)filterOpaque;
    (void)subscription;
    (void)panel;
#endif
}

template<typename AimPanel>
inline void PlatformInput_ResetPanelAfterWarp(AimPanel* panel)
{
#if defined(__linux__)
    if (panel)
        panel->resetAimMouseDelta();
#endif
}

#endif // !defined(_WIN32)

// NOTE: this function compiles on Windows too (it is not gated by the
// !defined(_WIN32) block above), so its #else fallback (QCursor::setPos) is
// reachable there. On Windows the correct cursor-confinement mechanism is
// ClipCursor, not a per-call warp -- see MelonPrimeScreenCursorPolicy.cpp,
// the canonical owner of Windows clip/warp/release. No current call site
// reaches this function on Windows (all callers of the aim-containment warp
// path are __APPLE__-gated). If a future Windows caller is added here,
// treat it as a regression: it would silently bypass the ClipCursor
// discipline documented in melonprime-aim-input.md, and needs review before
// landing, not a quiet QCursor::setPos fallback.
inline void PlatformInput_WarpCursor(int x, int y)
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    MelonPrimePerf::CountWarp();
#endif
#if defined(__APPLE__)
    MacWarpCursorGlobal(x, y);
#elif defined(__linux__)
    if (PlatformInput_IsXcb())
        LinuxWarpCursorGlobal(x, y);
    else
        QCursor::setPos(x, y);
#else
    QCursor::setPos(x, y);
#endif
}

} // namespace MelonPrime

#endif // MELONPRIME_PLATFORM_INPUT_H
