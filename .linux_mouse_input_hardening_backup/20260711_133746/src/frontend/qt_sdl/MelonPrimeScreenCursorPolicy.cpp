#include "MelonPrimeScreenCursorPolicy.h"

#include "Screen.h"
#include "EmuInstance.h"
#include "MelonPrime.h"
#include "MelonPrimePlatformInput.h"

#include <QApplication>
#include <QCursor>
#include <QRect>
#include <Qt>

#include <algorithm>
#include <atomic>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

inline RECT getVirtualScreenRect()
{
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return RECT{ vx, vy, vx + vw, vy + vh };
}

RECT computeCenter1pxClipRectSafe(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    POINT tl{ rc.left, rc.top }, br{ rc.right, rc.bottom };
    ClientToScreen(hwnd, &tl);
    ClientToScreen(hwnd, &br);

    LONG cx = (tl.x + br.x) / 2;

    const RECT vs = getVirtualScreenRect();

    if (cx < vs.left)  cx = vs.left;
    if (cx >= vs.right) cx = vs.right - 1;

    LONG top = (tl.y > vs.top) ? tl.y : vs.top;
    LONG bottom = (br.y < vs.bottom) ? br.y : vs.bottom;

    if (top >= bottom) {
        top = vs.top;
        bottom = vs.bottom;
    }

    RECT clip{ cx, top, cx + 1, bottom };
    return clip;
}

inline RECT shrinkRectHeightToHalfCentered(RECT r)
{
    const LONG h = r.bottom - r.top;
    const LONG cy = (r.top + r.bottom) / 2;
    const LONG quarter = h / 4;
    r.top = cy - quarter;
    r.bottom = cy + quarter;
    return r;
}

RECT computeWidgetClipRectSafe(HWND hwnd, const QRect& widgetRect)
{
    POINT tl{ widgetRect.left(), widgetRect.top() };
    POINT br{ widgetRect.right() + 1, widgetRect.bottom() + 1 };
    ClientToScreen(hwnd, &tl);
    ClientToScreen(hwnd, &br);

    RECT clip{ tl.x, tl.y, br.x, br.y };
    const RECT vs = getVirtualScreenRect();

    clip.left = std::clamp<LONG>(clip.left, vs.left, vs.right - 1);
    clip.right = std::clamp<LONG>(clip.right, clip.left + 1, vs.right);
    clip.top = std::clamp<LONG>(clip.top, vs.top, vs.bottom - 1);
    clip.bottom = std::clamp<LONG>(clip.bottom, clip.top + 1, vs.bottom);

    return clip;
}

} // namespace

#endif // _WIN32

namespace MelonPrime::ScreenCursorPolicy {

void ContainAimCursorIfNeeded(ScreenPanel& panel)
{
    if (panel.isClosingForMelonPrime() || !qApp || qApp->closingDown())
        return;
    if (!panel.getClipWantedForMelonPrime())
        return;
    if (!panel.isActiveVisibleWindowForMelonPrime())
        return;

#if defined(__APPLE__)
    const auto* core = panel.melonPrimeCoreForPolicy();
    if (core && core->IsGcMouseAimActive())
        return;
#endif

    const QRect local = panel.aimContainmentLocalRectForPolicy();
    const QPoint globalTopLeft = panel.mapToGlobal(local.topLeft());
    const QRect globalRect(globalTopLeft, local.size());
    const QPoint global = QCursor::pos();
    if (globalRect.contains(global))
        return;

    const QPoint center = panel.aimContainmentCenterGlobalForPolicy();
    PlatformInput_WarpCursor(center.x(), center.y());
}

void ClipCenter1px(ScreenPanel& panel)
{
    if (panel.isClosingForMelonPrime() || !qApp || qApp->closingDown())
        return;
    panel.setClipWantedForMelonPrime(true);
    panel.setCursor(Qt::BlankCursor);
#if defined(__linux__)
    panel.resetAimMouseDelta();
#endif

#if defined(__APPLE__)
    if (panel.isActiveVisibleWindowForMelonPrime()) {
        const auto* core = panel.melonPrimeCoreForPolicy();
        const QPoint c = panel.aimContainmentCenterGlobalForPolicy();
        const bool gcMouseActive = core && core->IsGcMouseAimActive();
        PlatformInput_WarpCursor(c.x(), c.y());
        MacSetAimCursorCaptured(gcMouseActive);
    }
#elif defined(__linux__)
    // MELONPRIME_WAYLAND_POINTER_LOCK_V1: Wayland does not permit arbitrary global cursor warps.
    // Request compositor-native relative motion and pointer locking first.
    if (panel.setWaylandPointerLockForMelonPrime(true))
        return;

    if (panel.isActiveVisibleWindowForMelonPrime()) {
        const auto* core = panel.melonPrimeCoreForPolicy();
        if (!core || !core->IsPlatformRawAimActive()) {
            const QPoint c = panel.mapToGlobal(panel.rect().center());
            PlatformInput_WarpCursor(c.x(), c.y());
        }
    }
#elif !defined(_WIN32)
    if (panel.isActiveVisibleWindowForMelonPrime()) {
        const QPoint c = panel.mapToGlobal(panel.rect().center());
        PlatformInput_WarpCursor(c.x(), c.y());
    }
#endif

#ifdef _WIN32
    if (!panel.isActiveVisibleWindowForMelonPrime()) return;
    const HWND hwnd = reinterpret_cast<HWND>(panel.winId());
    RECT clip = computeCenter1pxClipRectSafe(hwnd);
    clip = shrinkRectHeightToHalfCentered(clip);
    ClipCursor(&clip);
#endif
}

void Unclip(ScreenPanel& panel)
{
    if (panel.isClosingForMelonPrime() || !qApp || qApp->closingDown())
        return;
    panel.setClipWantedForMelonPrime(false);
#if defined(__APPLE__)
    MacSetAimCursorCaptured(false);
#endif
#if defined(__linux__)
    panel.setWaylandPointerLockForMelonPrime(false);
    panel.resetAimMouseDelta();
#endif
#ifdef _WIN32
    ClipCursor(nullptr);
#endif
}

void ReleaseForClose(ScreenPanel& panel)
{
    panel.setClipWantedForMelonPrime(false);
#if defined(__APPLE__)
    MacSetAimCursorCaptured(false);
#endif
#if defined(__linux__)
    panel.setWaylandPointerLockForMelonPrime(false);
    panel.resetAimMouseDelta();
#endif
#ifdef _WIN32
    ClipCursor(nullptr);
#endif
}

void ConfineToBottomScreen(ScreenPanel& panel)
{
    if (panel.isClosingForMelonPrime() || !qApp || qApp->closingDown())
        return;
    panel.setCursor(Qt::ArrowCursor);
#if defined(__linux__)
    panel.setWaylandPointerLockForMelonPrime(false);
#endif
#ifdef _WIN32
    if (!panel.isActiveVisibleWindowForMelonPrime()) return;
    const auto bottomRect = panel.getBottomScreenWidgetRectForPolicy();
    if (!bottomRect.has_value()) { Unclip(panel); return; }
    const HWND hwnd = reinterpret_cast<HWND>(panel.winId());
    RECT clip = computeWidgetClipRectSafe(hwnd, *bottomRect);
    if (clip.left >= clip.right || clip.top >= clip.bottom) { Unclip(panel); return; }
    ClipCursor(&clip);
#endif
}

void UpdateClipIfNeeded(ScreenPanel& panel)
{
    if (panel.isClosingForMelonPrime() || !qApp || qApp->closingDown())
        return;

    auto* emu = panel.emuInstanceForPolicy();
    auto* thread = emu ? emu->getEmuThread() : nullptr;
    auto* core = thread ? thread->GetMelonPrimeCore() : nullptr;

    if (core && !core->isFocused.load(std::memory_order_acquire)) {
        panel.setCursor(Qt::ArrowCursor);
        Unclip(panel);
        return;
    }

    if (panel.getClipWantedForMelonPrime()) {
        ClipCenter1px(panel);
#if defined(__APPLE__)
        ContainAimCursorIfNeeded(panel);
#endif
        return;
    }

    if (panel.shouldConfineCursorToBottomScreenForPolicy()) {
        panel.clipCursorToBottomScreenForPolicy();
        return;
    }

    panel.setCursor(Qt::ArrowCursor);
    Unclip(panel);
}

} // namespace MelonPrime::ScreenCursorPolicy
