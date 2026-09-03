#ifndef MELONPRIME_DIRECT_AIM_SOURCE_H
#define MELONPRIME_DIRECT_AIM_SOURCE_H

// MelonPrime direct-aim host source normalizer.
//
// Absolute pen coordinates are positions, not mouse deltas. Seed the first
// sample after every capture/source/generation boundary and difference only
// subsequent samples to prevent a one-frame aim jump.
//
// A single physical pen movement may surface as WM_POINTER, QTabletEvent, and
// a synthesized mouse message. The capture-generation authority admits one
// logical source only; never sum those paths.
//
// This header is deliberately free of Qt, Win32, and gameplay dependencies so
// the state machine is unit-testable on every build host.

#include <cstdint>

namespace MelonPrime {

// Ordered by the source priority contract: a lower enumerator wins an
// authority contest. None is the unlatched sentinel and loses to everything.
//
// Only absolute host sources arbitrate here. The relative mouse deliberately
// does not participate: it keeps its own Raw Input transport, and the frame
// projection falls back to it on every frame this mailbox reports no motion.
// That is what lets a mouse and a pen stay usable inside one capture.
enum class DirectAimHostSource : uint8_t
{
    None = 0,
    WinPointerPen = 1,
    QtTablet = 2,
    InjectedAbsolutePointer = 3,
};

[[nodiscard]] constexpr bool DirectAimSourceIsAbsolute(
    DirectAimHostSource source) noexcept
{
    return source != DirectAimHostSource::None;
}

[[nodiscard]] constexpr uint8_t DirectAimSourceRank(
    DirectAimHostSource source) noexcept
{
    return (source == DirectAimHostSource::None)
        ? 0xFFu
        : static_cast<uint8_t>(source);
}

// Why a baseline was dropped. Developer telemetry only; the state machine
// itself treats every reason identically.
enum class DirectAimBaselineReset : uint8_t
{
    CaptureBegin = 0,
    CaptureEnd,
    SourceChange,
    PointerChange,
    PointerLeave,
    FocusLoss,
    Lifecycle,
};

// Capture-generation source authority plus absolute-to-relative
// normalization. GUI-thread owned; no atomics, no allocation, no locking.
class DirectAimSourceArbiter
{
public:
    struct SubmitResult
    {
        // The sample belongs to the capture's authority source.
        bool accepted = false;
        // The authority latched or was pre-empted by a higher-priority source.
        bool authorityChanged = false;
        // First sample after a boundary: the delta is intentionally zero.
        bool seeded = false;
        int32_t dx = 0;
        int32_t dy = 0;
    };

    void BeginCapture() noexcept
    {
        ++m_generation;
        m_captureActive = true;
        m_authority = DirectAimHostSource::None;
        DropBaseline(DirectAimBaselineReset::CaptureBegin);
    }

    void EndCapture() noexcept
    {
        if (!m_captureActive)
            return;
        m_captureActive = false;
        m_authority = DirectAimHostSource::None;
        DropBaseline(DirectAimBaselineReset::CaptureEnd);
    }

    // Pointer leave/up, focus loss, DPI or layout change, owner transfer.
    // The authority stays latched for the capture; only the position baseline
    // is dropped so re-entry seeds instead of jumping.
    void DropBaseline(DirectAimBaselineReset reason) noexcept
    {
        m_baselineValid = false;
        m_pendingX = 0.0;
        m_pendingY = 0.0;
        m_lastResetReason = reason;
    }

    [[nodiscard]] bool CaptureActive() const noexcept { return m_captureActive; }
    [[nodiscard]] DirectAimHostSource Authority() const noexcept { return m_authority; }
    [[nodiscard]] uint32_t Generation() const noexcept { return m_generation; }
    [[nodiscard]] bool BaselineValid() const noexcept { return m_baselineValid; }
    [[nodiscard]] DirectAimBaselineReset LastResetReason() const noexcept
    {
        return m_lastResetReason;
    }

    SubmitResult SubmitAbsolute(DirectAimHostSource source,
                                uint32_t pointerId,
                                double x,
                                double y) noexcept
    {
        SubmitResult result{};
        if (!m_captureActive || !DirectAimSourceIsAbsolute(source))
            return result;

        result.authorityChanged = TakeAuthority(source);
        if (m_authority != source)
            return result; // duplicate route for an already-latched source

        result.accepted = true;

        if (!m_baselineValid
            || m_baselineSource != source
            || m_baselinePointerId != pointerId)
        {
            if (m_baselineValid && m_baselinePointerId != pointerId)
                m_lastResetReason = DirectAimBaselineReset::PointerChange;
            else if (m_baselineValid)
                m_lastResetReason = DirectAimBaselineReset::SourceChange;
            m_baselineValid = true;
            m_baselineSource = source;
            m_baselinePointerId = pointerId;
            m_lastX = x;
            m_lastY = y;
            m_pendingX = 0.0;
            m_pendingY = 0.0;
            result.seeded = true;
            return result;
        }

        m_pendingX = ClampPending(m_pendingX + (x - m_lastX));
        m_pendingY = ClampPending(m_pendingY + (y - m_lastY));
        m_lastX = x;
        m_lastY = y;

        // Truncation toward zero with the remainder carried forward keeps slow
        // sub-pixel pen motion from being quantized away, without drift.
        const int32_t dx = static_cast<int32_t>(m_pendingX);
        const int32_t dy = static_cast<int32_t>(m_pendingY);
        m_pendingX -= static_cast<double>(dx);
        m_pendingY -= static_cast<double>(dy);
        result.dx = dx;
        result.dy = dy;
        return result;
    }

private:
    // A capture-length ceiling on the carried remainder. Absolute coordinate
    // spaces are screen-sized, so anything past this is a boundary artifact
    // that must not be replayed as one enormous aim step.
    static constexpr double kPendingLimit = 1.0e6;

    [[nodiscard]] static double ClampPending(double value) noexcept
    {
        if (value > kPendingLimit)
            return kPendingLimit;
        if (value < -kPendingLimit)
            return -kPendingLimit;
        return value;
    }

    bool TakeAuthority(DirectAimHostSource source) noexcept
    {
        if (m_authority == source)
            return false;
        if (DirectAimSourceRank(source) >= DirectAimSourceRank(m_authority))
            return false;
        m_authority = source;
        DropBaseline(DirectAimBaselineReset::SourceChange);
        return true;
    }

    double m_lastX = 0.0;
    double m_lastY = 0.0;
    double m_pendingX = 0.0;
    double m_pendingY = 0.0;
    uint32_t m_generation = 0;
    uint32_t m_baselinePointerId = 0;
    DirectAimHostSource m_authority = DirectAimHostSource::None;
    DirectAimHostSource m_baselineSource = DirectAimHostSource::None;
    DirectAimBaselineReset m_lastResetReason = DirectAimBaselineReset::Lifecycle;
    bool m_captureActive = false;
    bool m_baselineValid = false;
};

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
// Developer-only counters. Release builds never format or log per event.
struct DirectAimTelemetry
{
    uint32_t captureGenerations = 0;
    uint32_t winPointerPenSamples = 0;
    uint32_t qtTabletSamples = 0;
    uint32_t injectedSamples = 0;
    uint32_t duplicateSuppressed = 0;
    uint32_t baselineResets = 0;
    uint32_t sourceTransitions = 0;

    void Reset() noexcept { *this = DirectAimTelemetry{}; }
};
#endif

} // namespace MelonPrime

#endif // MELONPRIME_DIRECT_AIM_SOURCE_H
