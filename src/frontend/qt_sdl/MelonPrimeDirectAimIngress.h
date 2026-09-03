#ifndef MELONPRIME_DIRECT_AIM_INGRESS_H
#define MELONPRIME_DIRECT_AIM_INGRESS_H

// GUI-thread owner of the direct-aim host source normalizer.
//
// Tablet direct aim is opt-in so the existing Raw Mouse path remains the
// steady-state fast path for users who do not need pen input. Nothing in this
// object is constructed, installed, or consulted while the option is off.

#include "MelonPrimeDirectAimSource.h"

#ifdef _WIN32
#include <memory>
#endif

namespace MelonPrime {

class MelonPrimeThreadBridge;
#ifdef _WIN32
class PointerWinFilter;
#endif

class DirectAimIngress
{
public:
    DirectAimIngress() noexcept;
    ~DirectAimIngress();

    DirectAimIngress(const DirectAimIngress&) = delete;
    DirectAimIngress& operator=(const DirectAimIngress&) = delete;

    // Cold path. Called from the touch-action press edge that starts a
    // direct-aim capture, only when tablet input is allowed.
    void BeginCapture(MelonPrimeThreadBridge& bridge,
                      void* topLevelHwnd,
                      void* surfaceHwnd);
    // Cold path. Capture release, focus loss, option change, teardown.
    void EndCapture();

    [[nodiscard]] bool Active() const noexcept { return m_bridge != nullptr; }
    [[nodiscard]] DirectAimHostSource Authority() const noexcept
    {
        return m_arbiter.Authority();
    }

    // Hot path (one per input event): normalize, arbitrate, publish.
    void SubmitAbsolute(DirectAimHostSource source,
                        uint32_t pointerId,
                        double x,
                        double y) noexcept;
    // Cold path: pointer leave/up, focus loss, layout or DPI change.
    void DropBaseline(DirectAimBaselineReset reason) noexcept;

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    [[nodiscard]] const DirectAimTelemetry& Telemetry() const noexcept
    {
        return m_telemetry;
    }
#endif

private:
    void PublishAuthority() noexcept;

    DirectAimSourceArbiter m_arbiter;
    MelonPrimeThreadBridge* m_bridge = nullptr;
#ifdef _WIN32
    std::unique_ptr<PointerWinFilter> m_winFilter;
#endif
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    DirectAimTelemetry m_telemetry;
#endif
};

} // namespace MelonPrime

#endif // MELONPRIME_DIRECT_AIM_INGRESS_H
