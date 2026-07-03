#ifndef MELONPRIME_PLATFORM_INPUT_H
#define MELONPRIME_PLATFORM_INPUT_H

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
    return QGuiApplication::platformName() == QStringLiteral("xcb");
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
#endif // defined(__APPLE__) || defined(__linux__)

#if !defined(_WIN32)
inline void PlatformInput_WarpCursor(int x, int y)
{
#if defined(__APPLE__)
    MacWarpCursorGlobal(x, y);
#elif defined(__linux__)
    if (QGuiApplication::platformName() == QStringLiteral("xcb"))
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
