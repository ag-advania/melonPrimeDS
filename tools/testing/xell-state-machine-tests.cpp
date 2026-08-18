/*
    Hardware-independent contract tests for MelonPrime's Intel XeLL wrapper.
    These tests inject a fake XeLL 1.3 API table; they never load or modify
    Intel's redistributable runtime and do not require an Intel GPU.
*/

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "DX12IntelXeLL.h"
#include "DX12LowLatencyPacing.h"
#include "Platform.h"

namespace melonDS::Platform
{
// DX12IntelXeLL.cpp normally resolves this from the Qt frontend. The vector
// executable is deliberately standalone, so discard logs in the test binary.
void Log(LogLevel, const char*, ...)
{
}
} // namespace melonDS::Platform

namespace
{
using namespace melonDS;

enum class Operation
{
    GetVersion,
    CreateContext,
    SetLoggingCallback,
    SetSleepMode,
    GetSleepMode,
    Sleep,
    Marker,
    DestroyContext,
};

struct Event
{
    Operation Op{};
    std::uint32_t FrameId = 0;
    DX12IntelXeLLMarker Marker = DX12IntelXeLLMarker::SimulationStart;
};

struct FakeXeLL
{
    static FakeXeLL* Current;

    std::vector<Event> Events;
    DX12IntelXeLLSleepParams RequestedSleep{};
    DX12IntelXeLLSleepParams ReportedSleep{};
    DX12IntelXeLLResult VersionResult = 0;
    DX12IntelXeLLResult CreateResult = 0;
    DX12IntelXeLLResult SetLoggingResult = 0;
    DX12IntelXeLLResult SetSleepResult = 0;
    DX12IntelXeLLResult GetSleepResult = 0;
    DX12IntelXeLLResult SleepResult = 0;
    DX12IntelXeLLResult MarkerResult = 0;
    DX12IntelXeLLMarker FailingMarker =
        static_cast<DX12IntelXeLLMarker>(-1);
    bool SleepMismatch = false;
    int DestroyCount = 0;

    FakeXeLL()
    {
        Current = this;
    }

    ~FakeXeLL()
    {
        if (Current == this)
            Current = nullptr;
    }

    static DX12IntelXeLLResult __cdecl GetVersion(DX12IntelXeLLVersion* version)
    {
        Current->Events.push_back({Operation::GetVersion});
        if (Current->VersionResult == 0 && version)
            *version = {1, 3, 2, 0};
        return Current->VersionResult;
    }

    static DX12IntelXeLLResult __cdecl CreateContext(
        ID3D12Device*,
        DX12IntelXeLLContext* context)
    {
        Current->Events.push_back({Operation::CreateContext});
        if (Current->CreateResult == 0 && context)
            *context = Current;
        return Current->CreateResult;
    }

    static DX12IntelXeLLResult __cdecl DestroyContext(DX12IntelXeLLContext)
    {
        Current->Events.push_back({Operation::DestroyContext});
        ++Current->DestroyCount;
        return 0;
    }

    static DX12IntelXeLLResult __cdecl SetLoggingCallback(
        DX12IntelXeLLContext,
        DX12IntelXeLLLoggingLevel,
        DX12IntelXeLLLogCallback)
    {
        Current->Events.push_back({Operation::SetLoggingCallback});
        return Current->SetLoggingResult;
    }

    static DX12IntelXeLLResult __cdecl SetSleepMode(
        DX12IntelXeLLContext,
        const DX12IntelXeLLSleepParams* params)
    {
        Current->Events.push_back({Operation::SetSleepMode});
        if (params)
        {
            Current->RequestedSleep = *params;
            Current->ReportedSleep = *params;
            if (Current->SleepMismatch)
                Current->ReportedSleep.MinimumIntervalUs += 1;
        }
        return Current->SetSleepResult;
    }

    static DX12IntelXeLLResult __cdecl GetSleepMode(
        DX12IntelXeLLContext,
        DX12IntelXeLLSleepParams* params)
    {
        Current->Events.push_back({Operation::GetSleepMode});
        if (Current->GetSleepResult == 0 && params)
            *params = Current->ReportedSleep;
        return Current->GetSleepResult;
    }

    static DX12IntelXeLLResult __cdecl Sleep(
        DX12IntelXeLLContext,
        std::uint32_t frameId)
    {
        Current->Events.push_back({Operation::Sleep, frameId});
        return Current->SleepResult;
    }

    static DX12IntelXeLLResult __cdecl AddMarker(
        DX12IntelXeLLContext,
        std::uint32_t frameId,
        DX12IntelXeLLMarker marker)
    {
        Current->Events.push_back({Operation::Marker, frameId, marker});
        if (marker == Current->FailingMarker)
            return Current->MarkerResult == 0 ? -1000 : Current->MarkerResult;
        return 0;
    }

    DX12IntelXeLLApi Api() const
    {
        return {
            &CreateContext,
            &DestroyContext,
            &SetSleepMode,
            &GetSleepMode,
            &Sleep,
            &AddMarker,
            &GetVersion,
            &SetLoggingCallback,
        };
    }
};

FakeXeLL* FakeXeLL::Current = nullptr;

[[noreturn]] void Fail(const std::string& message)
{
    std::fprintf(stderr, "XeLL state-machine test FAILED: %s\n", message.c_str());
    std::exit(1);
}

void Require(bool condition, const std::string& message)
{
    if (!condition)
        Fail(message);
}

ID3D12Device* FakeDevice()
{
    return reinterpret_cast<ID3D12Device*>(static_cast<std::uintptr_t>(1));
}

void Initialize(DX12IntelXeLL& xell, FakeXeLL& fake)
{
    Require(
        xell.InitializeForTesting(FakeDevice(), 0x8086u, fake.Api()),
        "fake context initialization must succeed");
}

std::vector<DX12IntelXeLLMarker> Markers(const FakeXeLL& fake)
{
    std::vector<DX12IntelXeLLMarker> markers;
    for (const Event& event : fake.Events)
    {
        if (event.Op == Operation::Marker)
            markers.push_back(event.Marker);
    }
    return markers;
}

void RequireCompleteMarkerOrder(const FakeXeLL& fake, const char* scenario)
{
    const std::vector<DX12IntelXeLLMarker> expected = {
        DX12IntelXeLLMarker::SimulationStart,
        DX12IntelXeLLMarker::InputSample,
        DX12IntelXeLLMarker::SimulationEnd,
        DX12IntelXeLLMarker::RenderSubmitStart,
        DX12IntelXeLLMarker::RenderSubmitEnd,
        DX12IntelXeLLMarker::PresentStart,
        DX12IntelXeLLMarker::PresentEnd,
    };
    Require(Markers(fake) == expected,
        std::string(scenario) + " must emit the complete ordered marker set");
}

void RequireNoPresentMarkers(const FakeXeLL& fake, const char* scenario)
{
    for (const Event& event : fake.Events)
    {
        if (event.Op != Operation::Marker)
            continue;
        Require(event.Marker != DX12IntelXeLLMarker::PresentStart
                && event.Marker != DX12IntelXeLLMarker::PresentEnd,
            std::string(scenario)
                + " must not synthesize Present markers");
    }
}

void TestNormalFrame()
{
    FakeXeLL fake;
    DX12IntelXeLL xell;
    Initialize(xell, fake);
    Require(xell.SetSleepMode(true, 16667), "sleep mode must apply");
    const auto status = xell.GetStatus();
    Require(status.Requested && status.RuntimePresent && status.SupportedByProbe,
        "status must distinguish request/runtime/vendor probe");
    Require(status.ContextCreated && status.SleepModeApplied && status.ActualEnabled,
        "status must expose the effective active state");
    Require(status.MinimumIntervalUs == 16667,
        "verified XeLL minimum interval must be reported");

    xell.BeginFrame();
    xell.MarkInputSample();
    xell.MarkRenderSubmitStart();
    xell.MarkRenderSubmitEnd();
    xell.MarkPresentStart();
    xell.MarkPresentEnd();
    xell.FinishFrame();
    RequireCompleteMarkerOrder(fake, "normal frame");
    xell.Shutdown();
    Require(fake.DestroyCount == 1, "normal shutdown must destroy one context");
}

void TestNoPresentFrame()
{
    FakeXeLL fake;
    DX12IntelXeLL xell;
    Initialize(xell, fake);
    Require(xell.SetEnabled(true), "no-present mode must apply");
    xell.BeginFrame();
    xell.MarkInputSample();
    xell.MarkRenderSubmitStart();
    xell.MarkRenderSubmitEnd();
    xell.FinishFrame();

    const std::vector<DX12IntelXeLLMarker> expected = {
        DX12IntelXeLLMarker::SimulationStart,
        DX12IntelXeLLMarker::InputSample,
        DX12IntelXeLLMarker::SimulationEnd,
        DX12IntelXeLLMarker::RenderSubmitStart,
        DX12IntelXeLLMarker::RenderSubmitEnd,
    };
    Require(Markers(fake) == expected,
        "no-present frame must close only the phases that started");
    RequireNoPresentMarkers(fake, "no-present frame");
}

void TestAbortedFrame()
{
    FakeXeLL fake;
    DX12IntelXeLL xell;
    Initialize(xell, fake);
    Require(xell.SetEnabled(true), "aborted-frame mode must apply");
    xell.BeginFrame();
    xell.FinishFrame();

    const std::vector<DX12IntelXeLLMarker> expected = {
        DX12IntelXeLLMarker::SimulationStart,
        DX12IntelXeLLMarker::SimulationEnd,
    };
    Require(Markers(fake) == expected,
        "an immediately aborted frame must not synthesize input or Present");
    RequireNoPresentMarkers(fake, "immediately aborted frame");
}

void TestInterruptedPresentCleanup()
{
    FakeXeLL fake;
    DX12IntelXeLL xell;
    Initialize(xell, fake);
    Require(xell.SetEnabled(true), "interrupted-present mode must apply");
    xell.BeginFrame();
    xell.MarkInputSample();
    xell.MarkPresentStart();
    xell.FinishFrame();

    RequireCompleteMarkerOrder(fake, "interrupted Present cleanup");
    int presentStarts = 0;
    int presentEnds = 0;
    for (const Event& event : fake.Events)
    {
        if (event.Op != Operation::Marker)
            continue;
        presentStarts += event.Marker == DX12IntelXeLLMarker::PresentStart;
        presentEnds += event.Marker == DX12IntelXeLLMarker::PresentEnd;
    }
    Require(presentStarts == 1 && presentEnds == 1,
        "interrupted Present cleanup must not duplicate marker pairs");
}

void TestMonotonicFrameIds()
{
    FakeXeLL fake;
    DX12IntelXeLL xell;
    Initialize(xell, fake);
    Require(xell.SetEnabled(false), "pass-through mode must apply");
    for (std::uint32_t expected = 1; expected <= 4; ++expected)
    {
        xell.BeginFrame();
        xell.FinishFrame();
    }

    std::uint32_t expected = 1;
    for (const Event& event : fake.Events)
    {
        if (event.Op == Operation::Sleep)
            Require(event.FrameId == expected++, "frame IDs must increase without reuse");
    }
    Require(expected == 5, "four Sleep calls must be observed");
}

void TestInitializationFailures()
{
    {
        FakeXeLL fake;
        fake.VersionResult = -2;
        DX12IntelXeLL xell;
        Require(!xell.InitializeForTesting(FakeDevice(), 0x8086u, fake.Api()),
            "GetVersion failure must reject initialization");
        Require(fake.DestroyCount == 0, "no context exists after version failure");
    }
    {
        FakeXeLL fake;
        fake.CreateResult = -1;
        DX12IntelXeLL xell;
        Require(!xell.InitializeForTesting(FakeDevice(), 0x8086u, fake.Api()),
            "CreateContext failure must reject initialization");
        Require(!xell.GetStatus().ContextCreated,
            "failed context creation must remain visible in status");
    }
    {
        FakeXeLL fake;
        DX12IntelXeLLApi incomplete = fake.Api();
        incomplete.Sleep = nullptr;
        DX12IntelXeLL xell;
        Require(!xell.InitializeForTesting(FakeDevice(), 0x8086u, incomplete),
            "missing required symbol must reject the API table");
    }
    {
        FakeXeLL fake;
        DX12IntelXeLL xell;
        Require(!xell.InitializeForTesting(FakeDevice(), 0x10DEu, fake.Api()),
            "non-Intel vendor must stay on the negative path");
        Require(!xell.SetEnabled(true) && !xell.IsActive(),
            "a persisted On setting must remain harmless after a negative probe");
        Require(!xell.GetStatus().SupportedByProbe,
            "negative vendor probe must be reported separately");
    }
}

void TestSleepModeFailures()
{
    for (int kind = 0; kind < 3; ++kind)
    {
        FakeXeLL fake;
        DX12IntelXeLL xell;
        Initialize(xell, fake);
        if (kind == 0)
            fake.SetSleepResult = -6;
        else if (kind == 1)
            fake.GetSleepResult = -6;
        else
            fake.SleepMismatch = true;
        Require(!xell.SetSleepMode(true, 12345),
            "sleep-mode failure or mismatch must disable XeLL");
        Require(!xell.IsAvailable() && !xell.IsActive(),
            "failed sleep-mode state must never remain active");
        xell.Shutdown();
        Require(fake.DestroyCount == 1,
            "context must still be destroyed after sleep-mode failure");
    }
}

void TestFrameFailures()
{
    {
        FakeXeLL fake;
        DX12IntelXeLL xell;
        Initialize(xell, fake);
        Require(xell.SetEnabled(true), "Sleep failure setup must apply");
        fake.SleepResult = -6;
        xell.BeginFrame();
        Require(!xell.IsAvailable(), "Sleep failure must disable future frame calls");
        const std::size_t calls = fake.Events.size();
        xell.BeginFrame();
        Require(fake.Events.size() == calls, "disabled XeLL must issue no next-frame calls");
    }

    const DX12IntelXeLLMarker markers[] = {
        DX12IntelXeLLMarker::SimulationStart,
        DX12IntelXeLLMarker::InputSample,
        DX12IntelXeLLMarker::SimulationEnd,
        DX12IntelXeLLMarker::RenderSubmitStart,
        DX12IntelXeLLMarker::RenderSubmitEnd,
        DX12IntelXeLLMarker::PresentStart,
        DX12IntelXeLLMarker::PresentEnd,
    };
    for (DX12IntelXeLLMarker failing : markers)
    {
        FakeXeLL fake;
        DX12IntelXeLL xell;
        Initialize(xell, fake);
        Require(xell.SetEnabled(true), "marker failure setup must apply");
        fake.FailingMarker = failing;
        fake.MarkerResult = -1000;
        xell.BeginFrame();
        xell.MarkInputSample();
        xell.MarkRenderSubmitStart();
        xell.MarkRenderSubmitEnd();
        xell.MarkPresentStart();
        xell.MarkPresentEnd();
        xell.FinishFrame();
        Require(!xell.IsAvailable() && !xell.IsActive(),
            "any marker failure must fail closed without crashing");
        xell.Shutdown();
        Require(fake.DestroyCount == 1,
            "marker failure must not leak its context");
    }
}

void TestPacingResolver()
{
    using Authority = DX12LowLatencyPacingAuthority;
    using Policy = DX12IntelXeLLPacingPolicy;

    const auto unavailable = ResolveDX12LowLatencyPacing(
        false, false, false, Policy::IntelRecommended);
    Require(unavailable.Authority == Authority::GenericHost
            && !unavailable.BypassHostLimiter
            && !unavailable.BypassPresentWait
            && !unavailable.XeLLOwnsFrameCap,
        "an inactive XeLL backend must never bypass generic pacing");

    const auto compatibility = ResolveDX12LowLatencyPacing(
        false, false, true, Policy::Compatibility);
    Require(compatibility.Authority == Authority::IntelXeLL
            && !compatibility.BypassHostLimiter
            && !compatibility.BypassPresentWait
            && !compatibility.XeLLOwnsFrameCap,
        "Compatibility must preserve both generic pacing stages");

    const auto present = ResolveDX12LowLatencyPacing(
        false, false, true, Policy::BypassPresentWait);
    Require(!present.BypassHostLimiter && present.BypassPresentWait
            && !present.XeLLOwnsFrameCap,
        "present-wait experiment must bypass only the DXGI wait");

    const auto host = ResolveDX12LowLatencyPacing(
        false, false, true, Policy::BypassHostLimiter);
    Require(host.BypassHostLimiter && !host.BypassPresentWait
            && !host.XeLLOwnsFrameCap,
        "host-limiter experiment must bypass only the hybrid limiter");

    const auto cap = ResolveDX12LowLatencyPacing(
        false, false, true, Policy::XeLLFrameCap);
    Require(cap.BypassHostLimiter && !cap.BypassPresentWait
            && cap.XeLLOwnsFrameCap,
        "XeLL cap experiment must transfer the frame cap from the host");

    const auto recommended = ResolveDX12LowLatencyPacing(
        false, false, true, Policy::IntelRecommended);
    Require(recommended.BypassHostLimiter && recommended.BypassPresentWait
            && recommended.XeLLOwnsFrameCap,
        "Intel-recommended experiment must give XeLL sole pacing authority");
    Require(!ShouldBypassDX12HostLimiter(host, false)
            && ShouldBypassDX12HostLimiter(host, true),
        "pure host-wait bypass must remain limited to normal speed");
    Require(ShouldBypassDX12HostLimiter(cap, false)
            && ShouldBypassDX12HostLimiter(recommended, false),
        "XeLL-owned caps must remain authoritative across speed transitions");

    const auto reflex = ResolveDX12LowLatencyPacing(
        true, true, true, Policy::IntelRecommended);
    Require(reflex.Authority == Authority::NvidiaReflex
            && !reflex.BypassHostLimiter
            && reflex.BypassPresentWait
            && !reflex.XeLLOwnsFrameCap,
        "active NVIDIA Reflex must keep the FPS cap and avoid a second driver wait");
    Require(ResolveDX12LowLatencyPacing(false, true, true, Policy::IntelRecommended).Authority
            == Authority::AmdAntiLag2,
        "active AMD Anti-Lag 2 must win over XeLL");
    Require(DX12IntelXeLLPacingPolicyFromConfig(-1) == Policy::Compatibility
            && DX12IntelXeLLPacingPolicyFromConfig(99) == Policy::Compatibility,
        "invalid persisted policies must clamp to Compatibility");
}
} // namespace

int main()
{
    TestNormalFrame();
    TestNoPresentFrame();
    TestAbortedFrame();
    TestInterruptedPresentCleanup();
    TestMonotonicFrameIds();
    TestInitializationFailures();
    TestSleepModeFailures();
    TestFrameFailures();
    TestPacingResolver();
    std::puts("Intel XeLL fake API state-machine tests PASS");
    return 0;
}
