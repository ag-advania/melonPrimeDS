#ifndef MELONPRIME_POINTER_WIN_FILTER_H
#define MELONPRIME_POINTER_WIN_FILTER_H

#ifdef _WIN32

#include <cstdint>

#include <QAbstractNativeEventFilter>

namespace MelonPrime {

class DirectAimIngress;

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
// Aggregate-only native filter evidence. It is reset per capture and emitted
// once at capture end when MELONPRIME_PERF=1; no event allocates or logs.
struct DirectAimWinFilterTelemetry
{
    std::uint64_t targetMessages = 0;
    std::uint64_t pointerMessages = 0;
    std::uint64_t mouseMoveMessages = 0;
    std::uint64_t pointerTypeCalls = 0;
    std::uint64_t pointerPenInfoCalls = 0;
    std::uint64_t inputMessageSourceCalls = 0;
    std::uint64_t acceptedSamples = 0;
    std::uint64_t rejectedSamples = 0;
    std::uint64_t fastRejectedMouseMoves = 0;
    std::uint64_t nativeFilterQpcTicks = 0;
    std::uint64_t timedMessages = 0;
};
#endif

// Windows Pointer/Pen ingress for direct aim.
//
// Responsibility is narrow on purpose: receive native messages, confirm the
// target window, classify the pointer, and hand a fixed POD sample to the
// normalizer. It performs no config lookup, no aim transform, no device
// enumeration, no vendor detection, no allocation, and no per-event logging.
//
// The filter is owned by the capturing GUI surface and installed only while a
// tablet-enabled direct-aim capture is held, so a mouse-only session never
// pays for it.
class PointerWinFilter final : public QAbstractNativeEventFilter
{
public:
    explicit PointerWinFilter(DirectAimIngress& ingress) noexcept;
    ~PointerWinFilter() override;

    PointerWinFilter(const PointerWinFilter&) = delete;
    PointerWinFilter& operator=(const PointerWinFilter&) = delete;

    // Both handles are accepted: pointer and mouse messages are delivered to
    // the top-level window, or to the render surface when it is native.
    void SetTargetWindows(void* topLevelHwnd, void* surfaceHwnd) noexcept;

    // Installs/removes on the GUI thread. Install is idempotent.
    void Install();
    void Remove();
    [[nodiscard]] bool Installed() const noexcept { return m_installed; }

    // Developer/performance builds keep aggregate counters only. These are
    // cold lifecycle operations; the native event path does not format them.
    void ResetTelemetry() noexcept;
    void ReportTelemetry() const noexcept;

    bool nativeEventFilter(const QByteArray& eventType,
                           void* message,
                           qintptr* result) override;

private:
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    void RecordSample(bool accepted) noexcept
    {
        if (!m_measurementEnabled)
            return;
        if (accepted)
            ++m_telemetry.acceptedSamples;
        else
            ++m_telemetry.rejectedSamples;
    }
#else
    void RecordSample(bool) noexcept {}
#endif

    DirectAimIngress& m_ingress;
    void* m_topLevelHwnd = nullptr;
    void* m_surfaceHwnd = nullptr;
    bool m_installed = false;
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    bool m_measurementEnabled = false;
    DirectAimWinFilterTelemetry m_telemetry;
#else
    static constexpr bool m_measurementEnabled = false;
#endif
};

} // namespace MelonPrime

#endif // _WIN32
#endif // MELONPRIME_POINTER_WIN_FILTER_H
