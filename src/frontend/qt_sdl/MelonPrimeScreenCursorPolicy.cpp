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

    // MELONPRIME_CURSOR_CAPTURE_STATE_V3:
    // clipWanted is the persistent request; platform locks/grabs are the
    // transient active state. This mirrors the request-vs-active split used by
    // mature emulator render widgets.
    panel.setClipWantedForMelonPrime(true);

    const auto* core = panel.melonPrimeCoreForPolicy();
    const auto ui = core ? core->ThreadBridge().ReadForGui()
                         : MelonPrimeUiSnapshot{};
    if (!panel.isActiveVisibleWindowForMelonPrime()
        || (core && !ui.focused))
    {
        panel.setCursor(Qt::ArrowCursor);
        Suspend(panel);
        return;
    }

#if defined(__linux__)
    panel.resetAimMouseDelta();
#endif
    panel.setCursor(Qt::BlankCursor);

#if defined(__APPLE__)
    const QPoint c = panel.aimContainmentCenterGlobalForPolicy();
    const bool gcMouseActive = core && core->IsGcMouseAimActive();
    PlatformInput_WarpCursor(c.x(), c.y());
    MacSetAimCursorCaptured(gcMouseActive);
#elif defined(__linux__)
    // Native Wayland is the preferred backend because global warps and X11
    // grabs are not valid Wayland confinement primitives.
    if (panel.setWaylandPointerLockForMelonPrime(true))
        return;

    if (PlatformInput_IsXcb()) {
        // MELONPRIME_LINUX_RAW_GRAB_RELEASE_FIX_V1
        // QWidget::grabMouse() is only needed by the Qt position-difference
        // fallback. XI_RawMotion is collected on a separate X connection; an
        // active Qt/X11 pointer grab can prevent that collector from receiving
        // further raw events.
        if (ui.rawAimActive) {
            QWidget* const grabber = QWidget::mouseGrabber();
            if (grabber == &panel)
                panel.releaseMouse();
            else if (grabber != nullptr) {
                // Never interfere with a modal/menu grab.
                panel.setCursor(Qt::ArrowCursor);
                return;
            }
        } else {
            QWidget* const grabber = QWidget::mouseGrabber();
            if (grabber == nullptr)
                panel.grabMouse(QCursor(Qt::BlankCursor));

            // Never steal a modal/menu grab. The request remains set and a later
            // activation/click event will retry capture.
            if (QWidget::mouseGrabber() != &panel) {
                panel.setCursor(Qt::ArrowCursor);
                return;
            }
        }

        const QPoint c = panel.mapToGlobal(panel.rect().center());
        PlatformInput_WarpCursor(c.x(), c.y());
    }
#elif !defined(_WIN32)
    const QPoint c = panel.mapToGlobal(panel.rect().center());
    PlatformInput_WarpCursor(c.x(), c.y());
#endif

#ifdef _WIN32
    const HWND hwnd = reinterpret_cast<HWND>(panel.winId());
    RECT clip = computeCenter1pxClipRectSafe(hwnd);
    clip = shrinkRectHeightToHalfCentered(clip);
    ClipCursor(&clip);
#endif
}

void Suspend(ScreenPanel& panel)
{
    // Release only the active backend state. clipWanted intentionally remains
    // unchanged so activation/show/click can reacquire it safely.
#if defined(__APPLE__)
    MacSetAimCursorCaptured(false);
#endif
#if defined(__linux__)
    panel.setWaylandPointerLockForMelonPrime(false);
    if (QWidget::mouseGrabber() == &panel)
        panel.releaseMouse();
    panel.resetAimMouseDelta();
#endif
#ifdef _WIN32
    ClipCursor(nullptr);
#endif
    panel.setCursor(Qt::ArrowCursor);
}

void Unclip(ScreenPanel& panel)
{
    if (panel.isClosingForMelonPrime() || !qApp || qApp->closingDown())
        return;
    panel.setClipWantedForMelonPrime(false);
    Suspend(panel);
}

void ReleaseForClose(ScreenPanel& panel)
{
    panel.setClipWantedForMelonPrime(false);
    Suspend(panel);
}

void ConfineToBottomScreen(ScreenPanel& panel)
{
    if (panel.isClosingForMelonPrime() || !qApp || qApp->closingDown())
        return;
    panel.setCursor(Qt::ArrowCursor);
#if defined(__linux__)
    panel.setWaylandPointerLockForMelonPrime(false);
    if (QWidget::mouseGrabber() == &panel)
        panel.releaseMouse();
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

    const auto ui = core ? core->ThreadBridge().ReadForGui()
                         : MelonPrimeUiSnapshot{};
    if (core && !ui.focused) {
        // Temporary focus loss must not erase the persistent capture request.
        Suspend(panel);
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
