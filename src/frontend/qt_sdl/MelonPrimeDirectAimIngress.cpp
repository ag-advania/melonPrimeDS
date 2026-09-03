#include "MelonPrimeDirectAimIngress.h"

#include "MelonPrimeThreadBridge.h"

#ifdef _WIN32
#include "MelonPrimePointerWinFilter.h"
#endif

namespace MelonPrime {

DirectAimIngress::DirectAimIngress() noexcept = default;

DirectAimIngress::~DirectAimIngress()
{
    EndCapture();
}

void DirectAimIngress::BeginCapture(MelonPrimeThreadBridge& bridge,
                                    void* topLevelHwnd,
                                    void* surfaceHwnd)
{
    if (m_bridge)
        EndCapture();

    m_bridge = &bridge;
    m_arbiter.BeginCapture();
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    m_telemetry.Reset();
    ++m_telemetry.captureGenerations;
#endif
    // Capture start is a hard mailbox boundary: motion published under the
    // previous capture, owner, or window must never reach the new one.
    bridge.PublishDirectAimSourceFromGui(
        static_cast<uint8_t>(DirectAimHostSource::None));

#ifdef _WIN32
    if (!m_winFilter)
        m_winFilter = std::make_unique<PointerWinFilter>(*this);
    m_winFilter->ResetTelemetry();
    m_winFilter->SetTargetWindows(topLevelHwnd, surfaceHwnd);
    m_winFilter->Install();
#else
    (void)topLevelHwnd;
    (void)surfaceHwnd;
#endif
}

void DirectAimIngress::EndCapture()
{
#ifdef _WIN32
    if (m_bridge && m_winFilter)
        m_winFilter->ReportTelemetry();
    if (m_winFilter)
        m_winFilter->Remove();
#endif
    if (!m_bridge) {
        m_arbiter.EndCapture();
        return;
    }

    m_arbiter.EndCapture();
    m_bridge->PublishDirectAimSourceFromGui(
        static_cast<uint8_t>(DirectAimHostSource::None));
    m_bridge = nullptr;
}

void DirectAimIngress::PublishAuthority() noexcept
{
    m_bridge->PublishDirectAimSourceFromGui(
        static_cast<uint8_t>(m_arbiter.Authority()));
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    ++m_telemetry.sourceTransitions;
#endif
}

bool DirectAimIngress::SubmitAbsolute(DirectAimHostSource source,
                                      uint64_t pointerId,
                                      double x,
                                      double y) noexcept
{
    if (!m_bridge)
        return false;

    const auto result = m_arbiter.SubmitAbsolute(source, pointerId, x, y);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    switch (source) {
    case DirectAimHostSource::WinPointerPen:
        ++m_telemetry.winPointerPenSamples;
        break;
    case DirectAimHostSource::QtTablet:
        ++m_telemetry.qtTabletSamples;
        break;
    case DirectAimHostSource::InjectedAbsolutePointer:
        ++m_telemetry.injectedSamples;
        break;
    default:
        break;
    }
    if (!result.accepted)
        ++m_telemetry.duplicateSuppressed;
    if (result.seeded)
        ++m_telemetry.baselineResets;
#endif
    if (result.authorityChanged)
        PublishAuthority();
    if (!result.accepted || result.seeded)
        return result.accepted;

    m_bridge->AddDirectAimDeltaFromGui(result.dx, result.dy);
    return true;
}

void DirectAimIngress::DropBaseline(DirectAimBaselineReset reason) noexcept
{
    m_arbiter.DropBaseline(reason);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    ++m_telemetry.baselineResets;
#endif
}

bool DirectAimIngress::DropBaselineForSource(
    DirectAimHostSource source,
    uint64_t pointerId,
    DirectAimBaselineReset reason) noexcept
{
    const bool reset = m_arbiter.DropBaselineForSource(source, pointerId, reason);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (reset)
        ++m_telemetry.baselineResets;
#endif
    return reset;
}

bool DirectAimIngress::ReleaseAuthority(
    DirectAimHostSource source,
    uint64_t pointerId,
    DirectAimBaselineReset reason) noexcept
{
    if (!m_bridge)
        return false;
    const bool released = m_arbiter.ReleaseAuthority(source, pointerId, reason);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (released)
        ++m_telemetry.baselineResets;
#endif
    if (released)
        PublishAuthority();
    return released;
}

} // namespace MelonPrime
