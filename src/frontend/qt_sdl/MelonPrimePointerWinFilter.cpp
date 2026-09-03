#ifdef _WIN32

// Windows 8 introduced the pointer input stack used below. Qt already targets
// a newer baseline, but the bump is stated here so this translation unit does
// not depend on another header having raised it first.
#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0602)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#if !defined(WINVER) || (WINVER < 0x0602)
#undef WINVER
#define WINVER 0x0602
#endif

#include "MelonPrimePointerWinFilter.h"

#include "MelonPrimeDirectAimIngress.h"

#include <QCoreApplication>

#include <windows.h>
#include <windowsx.h>

namespace MelonPrime {

namespace {

[[nodiscard]] inline bool IsTargetWindow(HWND hwnd,
                                         void* topLevel,
                                         void* surface) noexcept
{
    void* const raw = static_cast<void*>(hwnd);
    return raw != nullptr && (raw == topLevel || raw == surface);
}

} // namespace

PointerWinFilter::PointerWinFilter(DirectAimIngress& ingress) noexcept
    : m_ingress(ingress)
{
}

PointerWinFilter::~PointerWinFilter()
{
    Remove();
}

void PointerWinFilter::SetTargetWindows(void* topLevelHwnd,
                                        void* surfaceHwnd) noexcept
{
    m_topLevelHwnd = topLevelHwnd;
    m_surfaceHwnd = surfaceHwnd;
}

void PointerWinFilter::Install()
{
    if (m_installed)
        return;
    auto* const app = QCoreApplication::instance();
    if (!app)
        return;
    app->installNativeEventFilter(this);
    m_installed = true;
}

void PointerWinFilter::Remove()
{
    if (!m_installed)
        return;
    if (auto* const app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
    m_installed = false;
}

bool PointerWinFilter::nativeEventFilter(const QByteArray& eventType,
                                         void* message,
                                         qintptr* result)
{
    // Every Windows event type Qt reports carries a MSG*, so the observation
    // never consumes the message and never rewrites the result.
    Q_UNUSED(eventType)
    Q_UNUSED(result)

    const MSG* const msg = static_cast<const MSG*>(message);
    if (!msg || !IsTargetWindow(msg->hwnd, m_topLevelHwnd, m_surfaceHwnd))
        return false;

    switch (msg->message) {
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE: {
        const UINT32 pointerId =
            static_cast<UINT32>(GET_POINTERID_WPARAM(msg->wParam));
        POINTER_INPUT_TYPE type = PT_POINTER;
        if (!GetPointerType(pointerId, &type) || type != PT_PEN)
            return false;

        POINTER_PEN_INFO penInfo{};
        if (!GetPointerPenInfo(pointerId, &penInfo))
            return false;

        // Pressure, tilt and rotation are deliberately unused: direct aim is
        // gated by the touch action, not by pen contact, so a hovering pen
        // aims exactly like a moving mouse.
        m_ingress.SubmitAbsolute(
            DirectAimHostSource::WinPointerPen,
            pointerId,
            static_cast<double>(penInfo.pointerInfo.ptPixelLocation.x),
            static_cast<double>(penInfo.pointerInfo.ptPixelLocation.y));
        return false;
    }
    case WM_POINTERUP:
    case WM_POINTERLEAVE:
    case WM_POINTERCAPTURECHANGED:
        m_ingress.DropBaseline(DirectAimBaselineReset::PointerLeave);
        return false;
    case WM_MOUSEMOVE: {
        INPUT_MESSAGE_SOURCE source{};
        if (!GetCurrentInputMessageSource(&source))
            return false;
        // A pen or touch contact also promotes to mouse messages. Those belong
        // to the pointer route above; counting them here would aim twice for
        // one physical movement.
        if (source.deviceType == IMDT_TOUCH
            || source.deviceType == IMDT_PEN)
            return false;

        if (source.originId == IMO_INJECTED) {
            // Generic injected absolute pointer. This is not assumed to be any
            // particular driver: no process, path, IPC, VID/PID or version
            // detection is performed anywhere in this path.
            m_ingress.SubmitAbsolute(
                DirectAimHostSource::InjectedAbsolutePointer,
                0,
                static_cast<double>(msg->pt.x),
                static_cast<double>(msg->pt.y));
        }
        else {
            // Anything left is an ordinary mouse: hardware, or an origin the
            // OS could not classify. Its movement is transported by Raw Input,
            // so latch the authority only -- the two are never summed -- and
            // let the owner reconcile the center clip that Raw aim expects.
            m_ingress.LatchRawRelative();
        }
        return false;
    }
    case WM_KILLFOCUS:
    case WM_CAPTURECHANGED:
        m_ingress.DropBaseline(DirectAimBaselineReset::FocusLoss);
        return false;
    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
        m_ingress.DropBaseline(DirectAimBaselineReset::Lifecycle);
        return false;
    default:
        return false;
    }
}

} // namespace MelonPrime

#endif // _WIN32
