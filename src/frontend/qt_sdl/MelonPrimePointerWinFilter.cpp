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

#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace MelonPrime {

namespace {

[[nodiscard]] inline bool IsTargetWindow(HWND hwnd,
                                         void* topLevel,
                                         void* surface) noexcept
{
    void* const raw = static_cast<void*>(hwnd);
    return raw != nullptr && (raw == topLevel || raw == surface);
}

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
[[nodiscard]] bool IsDirectAimPerfEnabled() noexcept
{
    static const bool enabled = [] {
        const char* value = std::getenv("MELONPRIME_PERF");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

struct NativeFilterTiming final
{
    DirectAimWinFilterTelemetry& telemetry;
    LARGE_INTEGER start{};
    bool active = false;

    NativeFilterTiming(DirectAimWinFilterTelemetry& value, bool enabled) noexcept
        : telemetry(value)
    {
        active = enabled && QueryPerformanceCounter(&start) != FALSE;
    }

    ~NativeFilterTiming()
    {
        if (!active)
            return;
        LARGE_INTEGER end{};
        if (QueryPerformanceCounter(&end) == FALSE
            || end.QuadPart < start.QuadPart)
            return;
        telemetry.nativeFilterQpcTicks += static_cast<std::uint64_t>(
            end.QuadPart - start.QuadPart);
        ++telemetry.timedMessages;
    }
};
#endif

} // namespace

PointerWinFilter::PointerWinFilter(DirectAimIngress& ingress) noexcept
    : m_ingress(ingress)
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    m_measurementEnabled = IsDirectAimPerfEnabled();
#endif
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

void PointerWinFilter::ResetTelemetry() noexcept
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    m_telemetry = DirectAimWinFilterTelemetry{};
#endif
}

void PointerWinFilter::ReportTelemetry() const noexcept
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (!m_measurementEnabled)
        return;

    LARGE_INTEGER frequency{};
    const bool haveFrequency = QueryPerformanceFrequency(&frequency) != FALSE
        && frequency.QuadPart > 0;
    const double averageUs = haveFrequency && m_telemetry.timedMessages != 0
        ? static_cast<double>(m_telemetry.nativeFilterQpcTicks) * 1000000.0
            / static_cast<double>(frequency.QuadPart)
            / static_cast<double>(m_telemetry.timedMessages)
        : 0.0;
    std::fprintf(
        stderr,
        "[MelonPrimeDirectAimPerf] native_filter "
        "target_messages=%llu pointer_messages=%llu mousemove_messages=%llu "
        "pointer_type_calls=%llu pointer_pen_info_calls=%llu "
        "input_message_source_calls=%llu fast_rejected_mousemoves=%llu "
        "accepted_samples=%llu rejected_samples=%llu "
        "timed_messages=%llu qpc_ticks=%llu average_us=%.3f\n",
        static_cast<unsigned long long>(m_telemetry.targetMessages),
        static_cast<unsigned long long>(m_telemetry.pointerMessages),
        static_cast<unsigned long long>(m_telemetry.mouseMoveMessages),
        static_cast<unsigned long long>(m_telemetry.pointerTypeCalls),
        static_cast<unsigned long long>(m_telemetry.pointerPenInfoCalls),
        static_cast<unsigned long long>(m_telemetry.inputMessageSourceCalls),
        static_cast<unsigned long long>(m_telemetry.fastRejectedMouseMoves),
        static_cast<unsigned long long>(m_telemetry.acceptedSamples),
        static_cast<unsigned long long>(m_telemetry.rejectedSamples),
        static_cast<unsigned long long>(m_telemetry.timedMessages),
        static_cast<unsigned long long>(m_telemetry.nativeFilterQpcTicks),
        averageUs);
#endif
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

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (m_measurementEnabled)
        ++m_telemetry.targetMessages;
    const NativeFilterTiming timing(m_telemetry, m_measurementEnabled);
#endif

    switch (msg->message) {
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE: {
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (m_measurementEnabled)
            ++m_telemetry.pointerMessages;
#endif
        const std::uint64_t pointerId = static_cast<std::uint64_t>(
            GET_POINTERID_WPARAM(msg->wParam));
        POINTER_INPUT_TYPE type = PT_POINTER;
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (m_measurementEnabled)
            ++m_telemetry.pointerTypeCalls;
#endif
        if (!GetPointerType(static_cast<UINT32>(pointerId), &type)
            || type != PT_PEN) {
            RecordSample(false);
            return false;
        }

        POINTER_PEN_INFO penInfo{};
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (m_measurementEnabled)
            ++m_telemetry.pointerPenInfoCalls;
#endif
        if (!GetPointerPenInfo(static_cast<UINT32>(pointerId), &penInfo)) {
            RecordSample(false);
            return false;
        }

        // Pressure, tilt and rotation are deliberately unused: direct aim is
        // gated by the touch action, not by pen contact, so a hovering pen
        // aims exactly like a moving mouse.
        RecordSample(m_ingress.SubmitAbsolute(
            DirectAimHostSource::WinPointerPen,
            pointerId,
            static_cast<double>(penInfo.pointerInfo.ptPixelLocation.x),
            static_cast<double>(penInfo.pointerInfo.ptPixelLocation.y)));
        return false;
    }
    case WM_POINTERUP:
    case WM_POINTERLEAVE:
    case WM_POINTERCAPTURECHANGED: {
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (m_measurementEnabled)
            ++m_telemetry.pointerMessages;
#endif
        // A terminal touch pointer must not reset or release a pen authority.
        // Only query the pointer stack while its source is the current owner.
        if (m_ingress.Authority() != DirectAimHostSource::WinPointerPen)
            return false;
        const std::uint64_t pointerId = static_cast<std::uint64_t>(
            GET_POINTERID_WPARAM(msg->wParam));
        POINTER_INPUT_TYPE type = PT_POINTER;
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (m_measurementEnabled)
            ++m_telemetry.pointerTypeCalls;
#endif
        if (!GetPointerType(static_cast<UINT32>(pointerId), &type)
            || type != PT_PEN)
            return false;
        if (msg->message == WM_POINTERUP) {
            // Contact-up ends the current position segment, but hover remains
            // the same source and must not hand the capture to a lower route.
            (void)m_ingress.DropBaselineForSource(
                DirectAimHostSource::WinPointerPen,
                pointerId,
                DirectAimBaselineReset::PointerLeave);
        } else {
            // Leave/capture-loss is source departure, not merely a baseline
            // reset. A later source may seed and become authoritative.
            (void)m_ingress.ReleaseAuthority(
                DirectAimHostSource::WinPointerPen,
                pointerId,
                DirectAimBaselineReset::PointerLeave);
        }
        return false;
    }
    case WM_MOUSEMOVE: {
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (m_measurementEnabled)
            ++m_telemetry.mouseMoveMessages;
#endif
        // A native pen or Qt tablet authority already owns absolute motion.
        // Reject synthesized mouse messages before the source query; when no
        // absolute authority exists, retain the query for generic injected
        // pointer discovery.
        const auto authority = m_ingress.Authority();
        if (authority == DirectAimHostSource::WinPointerPen
            || authority == DirectAimHostSource::QtTablet) {
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
            if (m_measurementEnabled)
                ++m_telemetry.fastRejectedMouseMoves;
#endif
            return false;
        }
        INPUT_MESSAGE_SOURCE source{};
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (m_measurementEnabled)
            ++m_telemetry.inputMessageSourceCalls;
#endif
        if (!GetCurrentInputMessageSource(&source))
            return false;
        // A pen or touch contact also promotes to mouse messages. Those belong
        // to the pointer route above; counting them here would aim twice for
        // one physical movement.
        if (source.deviceType == IMDT_TOUCH
            || source.deviceType == IMDT_PEN)
            return false;

        // An ordinary mouse is left entirely alone here: its motion is
        // transported by Raw Input, and the frame projection falls back to
        // that on every frame this ingress reports no motion. Touching it
        // would be the difference between a pen and a mouse coexisting and a
        // pen locking the mouse out for the rest of the capture.
        if (source.originId != IMO_INJECTED)
            return false;

        // Generic injected absolute pointer. This is not assumed to be any
        // particular driver: no process, path, IPC, VID/PID or version
        // detection is performed anywhere in this path.
        RecordSample(m_ingress.SubmitAbsolute(
            DirectAimHostSource::InjectedAbsolutePointer,
            0,
            static_cast<double>(msg->pt.x),
            static_cast<double>(msg->pt.y)));
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
