#ifndef MELONPRIME_PLATFORM_INPUT_H
#define MELONPRIME_PLATFORM_INPUT_H

#include "MelonPrimePerfProbe.h"

#include <cstdint>

#if !defined(_WIN32)
#include <QCursor>
#include <QGuiApplication>
#endif

#if defined(__APPLE__)
#include "MelonPrimeRawInputMacFilter.h"
#elif defined(__linux__)
#include "MelonPrimeRawInputLinuxFilter.h"
#endif

namespace MelonPrime {

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

inline void PlatformInput_FetchRawMouseDelta(PlatformRawFilter* filter,
                                             int32_t& outDx,
                                             int32_t& outDy)
{
    filter->fetchMouseDelta(outDx, outDy);
}

inline void PlatformInput_ResetRawFilter(PlatformRawFilter* filter)
{
    if (filter)
        filter->resetAll();
}

// Single resolution point for aim delta ownership (V5 Phase 2).
inline AimInputSource PlatformInput_ResolveAimSource(
    PlatformRawFilter* filter,
    bool hasPanel,
    bool& outHaveMouseDelta,
    int32_t& outDx,
    int32_t& outDy)
{
    outHaveMouseDelta = false;
    outDx = 0;
    outDy = 0;

#if defined(__APPLE__)
    if (PlatformInput_IsRawAimActive(filter)) {
        PlatformInput_FetchRawMouseDelta(filter, outDx, outDy);
        outHaveMouseDelta = true;
        return AimInputSource::MacRaw;
    }
    return AimInputSource::QCursorFallback;
#else
    if (PlatformInput_IsRawAimActive(filter)) {
        PlatformInput_FetchRawMouseDelta(filter, outDx, outDy);
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
#endif // defined(__APPLE__) || defined(__linux__)

#if !defined(_WIN32)
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
#endif // !defined(_WIN32)

} // namespace MelonPrime

#endif // MELONPRIME_PLATFORM_INPUT_H
