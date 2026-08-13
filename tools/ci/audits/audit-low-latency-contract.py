#!/usr/bin/env python3
"""Guard Vulkan/DX12 low-latency ordering, pacing ownership, and pinned ABIs."""

from pathlib import Path
import hashlib
import struct
import sys


ROOT = Path(__file__).resolve().parents[3]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def ordered(source: str, tokens: list[str]) -> bool:
    cursor = 0
    for token in tokens:
        cursor = source.find(token, cursor)
        if cursor < 0:
            return False
        cursor += len(token)
    return True


def function_body(source: str, signature: str, next_signature: str) -> str:
    start = source.find(signature)
    end = source.find(next_signature, start + len(signature))
    if start < 0 or end < 0:
        return ""
    return source[start:end]


def pe_exports(path: Path) -> set[str]:
    """Read the PE export-name table without platform-specific SDK tools."""
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe : pe + 4] != b"PE\0\0":
        raise ValueError("invalid PE signature")
    sections_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    directory_offset = optional + (112 if magic == 0x20B else 96)
    export_rva = struct.unpack_from("<I", data, directory_offset)[0]
    section_table = optional + optional_size
    sections: list[tuple[int, int, int]] = []
    for index in range(sections_count):
        section = section_table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", data, section + 8
        )
        sections.append((virtual_address, max(virtual_size, raw_size), raw_offset))

    def file_offset(rva: int) -> int:
        for virtual_address, size, raw_offset in sections:
            if virtual_address <= rva < virtual_address + size:
                return raw_offset + rva - virtual_address
        raise ValueError(f"unmapped RVA 0x{rva:X}")

    export_offset = file_offset(export_rva)
    names_count = struct.unpack_from("<I", data, export_offset + 24)[0]
    names_rva = struct.unpack_from("<I", data, export_offset + 32)[0]
    names_offset = file_offset(names_rva)
    result: set[str] = set()
    for index in range(names_count):
        name_rva = struct.unpack_from("<I", data, names_offset + index * 4)[0]
        name_offset = file_offset(name_rva)
        name_end = data.index(b"\0", name_offset)
        result.add(data[name_offset:name_end].decode("ascii"))
    return result


def main() -> int:
    failures: list[str] = []
    emu = read("src/frontend/qt_sdl/EmuThread.cpp")
    dx12 = read("src/DX12NvidiaReflex.cpp")
    vulkan = read("src/VulkanNvidiaReflex.cpp")
    vulkan_pacer = read("src/VulkanPresentPacer.cpp")
    vulkan_pacer_header = read("src/VulkanPresentPacer.h")
    vulkan_latency_capture = read("src/VulkanPresentLatencyCapture.cpp")
    vulkan_pacing_policy = read("src/VulkanPresentPacingPolicy.h")
    vulkan_timing_tests = read("tools/testing/vulkan-present-timing-tests.cpp")
    vulkan_presenter = read("src/frontend/qt_sdl/MelonPrimeVulkanPresenter.cpp")
    vulkan_compat = read("src/VulkanModernPresentCompat.h")
    vulkan_device = read("src/VulkanDevice.cpp")
    dx12_context = read("src/DX12Context.cpp")
    vulkan_loader = read("src/VulkanLoader.cpp")
    probe = read("src/VulkanFeatureProbe.cpp")
    amd = read("src/DX12AmdAntiLag2.cpp")
    xell = read("src/DX12IntelXeLL.cpp")
    xell_header = read("src/DX12IntelXeLL.h")
    pacing = read("src/DX12LowLatencyPacing.h")
    xell_tests = read("tools/testing/xell-state-machine-tests.cpp")
    presenter = read("src/frontend/qt_sdl/MelonPrimeDX12SurfacePresenter.cpp")
    main_source = read("src/frontend/qt_sdl/main.cpp")
    screen = read("src/frontend/qt_sdl/Screen.cpp")
    video_settings = read("src/frontend/qt_sdl/VideoSettingsDialog.cpp")
    config = read("src/frontend/qt_sdl/Config.cpp")
    cmake = read("src/frontend/qt_sdl/CMakeLists.txt")

    require(
        ordered(
            emu,
            [
                "if (UNLIKELY(videoSettingsDirty))",
                "applyPendingVideoSettings();",
                "BeginReflexFrame();",
                "beginVulkanLowLatencyFrame(",
            ],
        ),
        "Pending renderer settings must be applied before DX12/Vulkan low-latency Begin",
        failures,
    )
    require(
        ordered(
            vulkan_presenter,
            [
                "SetLowLatencyPreferences(reflexMode, antiLag2Enabled);",
                "PresentPacer.BeginFrame(",
                "Reflex.IsActive(), AntiLag.IsActive(), normalSpeed, targetFrameIntervalNs)",
                "Reflex.BeginFrame();",
                "AntiLag.BeginFrame(LowLatencyFrameIndex);",
            ],
        ),
        "Vulkan must select one pacing authority and run generic wait before vendor/input markers",
        failures,
    )
    require(
        all(token in vulkan_pacer for token in (
            "VulkanPacingAuthority::NvidiaReflex",
            "VulkanPacingAuthority::AmdAntiLag2",
            "VulkanPacingAuthority::GenericPresentTiming",
            "VulkanPacingAuthority::GenericHost",
            "MaxPresentWaitNs = 2'000'000",
            "LastPresentedId == LastWaitedId",
            "VK_ERROR_OUT_OF_DATE_KHR",
            "VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT",
            "PrepareRetryWithoutTiming",
        )),
        "Vulkan generic pacing must keep priority, bounded timeout, skipped-present, reset, and timing-queue guards",
        failures,
    )
    require(
        "bypassVulkanHostLimiter" not in emu
        and "ShouldBypassHostLimiter" not in vulkan_pacer,
        "Vulkan latency waits must never bypass the exact host FPS limiter",
        failures,
    )
    require(
        "if (oldRenderer == renderer3D_Vulkan)" in video_settings
        and "if (oldRenderer == renderer3D_DX12)" in video_settings
        and ordered(
            function_body(
                video_settings,
                "void VideoSettingsDialog::onChange3DRenderer(int renderer)",
                "void VideoSettingsDialog::on_cbGLDisplay_stateChanged(int state)",
            ),
            [
                "emit updateVideoSettings(true);",
                "setEnabled();",
            ],
        ),
        "Video Settings must not probe a foreign native backend before its synchronous transition",
        failures,
    )
    require(
        "ReleaseRetainedDeviceForBackendTransition();" in read("src/frontend/qt_sdl/Window.cpp")
        and "retired.swap(ProcessLifetimeDevice());" in vulkan_device
        and "SharedDevice.reset();" in vulkan_device
        and "DXGI_ADAPTER_FLAG_SOFTWARE" in dx12_context
        and "if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)" in dx12_context,
        "quiesced Vulkan-to-DX12 transitions must release the retained device and reject software DX12 adapters",
        failures,
    )
    require(
        "{MelonPrime::CfgKey::VulkanPresentPacingPolicy, 0}" in config
        and "TelemetryOnly = 0" in vulkan_pacing_policy
        and "JustInTimeFifoLatestReady" in vulkan_pacer
        and "PresentPacer.ShouldUseFifoLatestReady()" in vulkan_presenter,
        "Vulkan behavioural pacing must default to telemetry-only and gate FIFO latest-ready",
        failures,
    )
    # VK_EXT_present_timing depends on VK_KHR_swapchain, VK_KHR_present_id2,
    # VK_KHR_get_surface_capabilities2 and VK_KHR_calibrated_timestamps -- not on
    # VK_KHR_present_wait2. The two were once resolved by a single condition, so a
    # driver exposing only present timing lost target-time scheduling entirely.
    require(
        "PresentWait2Surface" not in function_body(
            vulkan_pacing_policy,
            "constexpr VulkanJitFallbackReason ClassifyVulkanTargetFallback(",
            "constexpr VulkanPacingDecision ResolveVulkanPresentPacing(",
        )
        and "bool BoundedPresentWait = false;" in vulkan_pacing_policy
        and "bool TargetTimeScheduling = false;" in vulkan_pacing_policy,
        "Vulkan target-time scheduling must not require VK_KHR_present_wait2",
        failures,
    )
    require(
        "if (!decision.BoundedPresentWait)" in vulkan_pacer
        and "if (!LastDecision.TargetTimeScheduling)" in vulkan_pacer
        and "ResolveVulkanPresentPacing(" in vulkan_pacer
        and "PresentWait2Surface" not in function_body(
            vulkan_pacer,
            "u64 VulkanPresentPacer::EvaluateTargetTime(u64 sequence) noexcept",
            "u64 VulkanPresentPacer::PreparePresent(",
        ),
        "the pacer must resolve wait and target scheduling from one shared, independent decision",
        failures,
    )
    require(
        "TestTargetTimeDoesNotRequirePresentWait2" in vulkan_timing_tests
        and "caps.PresentWait2Surface = false;" in vulkan_timing_tests
        and "TestBoundedWaitWithoutPresentTiming" in vulkan_timing_tests,
        "the capability matrix must test target-time scheduling without present_wait2",
        failures,
    )
    require(
        "TimingQueueSizeFor(imageCount)" in vulkan_pacer
        and "TimingResultsQueryEnabled" in vulkan_pacer
        and "MaxTimingQueueRecoveries" in vulkan_pacer_header
        and "TimingQueueRecoveryPending = recoverable;" in vulkan_pacer,
        "a full present timing queue must keep draining and recover a bounded number of times",
        failures,
    )
    # A present that requests timing needs a results-queue slot. Enabling
    # metadata after a failed initial allocation attaches timing to a swapchain
    # that cannot service it, and arms a recovery whose trigger -- a drained
    # report -- can never arrive.
    require(
        "[[nodiscard]] bool ApplyTimingQueueSize(u32 size);" in vulkan_pacer_header
        and ordered(
            function_body(
                vulkan_pacer,
                "void VulkanPresentPacer::OnSwapchainCreated(",
                "void VulkanPresentPacer::OnSwapchainDestroyed() noexcept",
            ),
            [
                "if (ApplyTimingQueueSize(TimingQueueSizeFor(imageCount)))",
                "TimingMetadataEnabled = true;",
                "TimingResultsQueryEnabled = true;",
                "else",
                "TimingQueueRecoveryPending = false;",
            ],
        ),
        "a failed initial timing queue allocation must leave timing metadata disabled",
        failures,
    )
    require(
        "TimingQueueAllocated" in vulkan_pacer_header
        and "TimingQueueRecoveryPending = false;" in function_body(
            vulkan_pacer,
            "if (TimingQueueRecoveryPending && completedReportCount > 0 && TimingQueueAllocated)",
            "    // Nothing left to poll for",
        )
        and "TimingQueueRecoveryPending && completedReportCount > 0 && TimingQueueAllocated"
            in vulkan_pacer,
        "queue growth must be distinguished from the initial allocation",
        failures,
    )
    require(
        all(token in vulkan_pacer for token in (
            "OutstandingTimedPresents >= TimingQueueSize",
            "present timing results queue at capacity",
            "completedReportCount",
            "reportComplete == VK_TRUE",
        )),
        "timing metadata must pause before a full queue forces a retry-present sync hazard",
        failures,
    )
    require(
        "!TimingMetadataEnabled && !TimingQueueRecoveryPending" in vulkan_pacer
        and "TimingResultsQueryEnabled = false;" in vulkan_pacer,
        "timing polling must stop once metadata is off for good and the queue has drained",
        failures,
    )
    # VK_ERROR_DEVICE_LOST and VK_ERROR_OUT_OF_DATE_KHR are different failure
    # classes: rebuilding a swapchain on a lost device just repeats the failure.
    require(
        "enum class VulkanPacerBeginResult" in vulkan_pacing_policy
        and "VulkanPacerActionFor" in vulkan_pacing_policy
        and "return VulkanPacerBeginResult::SwapchainOutOfDate;" in vulkan_pacer
        and "return VulkanPacerBeginResult::DeviceLost;" in vulkan_pacer
        and "pacerAction.FailRenderer" in vulkan_presenter
        and 'Fail("vkWaitForPresent2KHR", VK_ERROR_DEVICE_LOST);' in vulkan_presenter
        and "TestBeginResultRouting" in vulkan_timing_tests,
        "present-wait device loss must not share the swapchain-out-of-date result",
        failures,
    )
    # An empty poll does not prove the results queue is drained: presentation
    # timing feedback is asynchronous and only promised to arrive in finite time.
    require(
        "OutstandingTimedPresents" in vulkan_pacer_header
        and "if (metadata.TimingAttached)" in vulkan_pacer
        and "++OutstandingTimedPresents;" in vulkan_pacer
        and "OutstandingTimedPresents == 0 && reportCount == 0" in vulkan_pacer,
        "stopping timing polling must be decided from outstanding timed presents",
        failures,
    )
    # The A/B capture must be measurable without developer features, because
    # developer features change what the pacer asks the driver for.
    require(
        "MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE" in cmake
        and "MELONPRIME_VULKAN_LATENCY_CAPTURE=1" in cmake
        and "MELONPRIME_VULKAN_LATENCY_CAPTURE" not in read("src/VulkanPresentPacer.cpp")
        and "MELONPRIME_VULKAN_LATENCY_CAPTURE"
            not in function_body(
                vulkan_pacer,
                "VkPresentStageFlagsEXT VulkanPresentPacer::RequestedStageQueries()",
                "bool VulkanPresentPacer::ApplyTimingQueueSize(u32 size)",
            ),
        "the latency capture flag must be independent of pacing and stage-query behaviour",
        failures,
    )
    # --- absolute / relative target scheduling ------------------------------
    # A surface may support presentAtRelativeTime and not presentAtAbsoluteTime,
    # which is the case on the driver this path was validated against. Requiring
    # absolute made the JustInTime policies silently equal to PresentWait there.
    require(
        "enum class VulkanTargetSchedulingMode" in vulkan_pacing_policy
        and "SelectVulkanTargetSchedulingMode" in vulkan_pacing_policy
        and "bool RelativeTimingDevice = false;" in vulkan_pacing_policy
        and "bool RelativeTimingSurface = false;" in vulkan_pacing_policy
        and ordered(
            function_body(
                vulkan_pacing_policy,
                "constexpr VulkanTargetSchedulingMode SelectVulkanTargetSchedulingMode(",
                "struct VulkanPacingDecision",
            )
            or function_body(
                vulkan_pacing_policy,
                "constexpr VulkanTargetSchedulingMode SelectVulkanTargetSchedulingMode(",
                "constexpr bool VulkanPolicyRequestsTargetTime(",
            ),
            [
                "AbsoluteTimingDevice && caps.AbsoluteTimingSurface",
                "VulkanTargetSchedulingMode::Absolute",
                "RelativeTimingDevice && caps.RelativeTimingSurface",
                "VulkanTargetSchedulingMode::Relative",
            ],
        ),
        "target scheduling must model absolute and relative modes with absolute preferred",
        failures,
    )
    require(
        "NoTargetTimingModeDevice" in vulkan_pacing_policy
        and "NoTargetTimingModeSurface" in vulkan_pacing_policy
        and "AbsoluteTimingUnsupportedSurface" not in vulkan_pacing_policy,
        "falling back from absolute to relative must not be reported as a failure reason",
        failures,
    )
    require(
        "VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT" in vulkan_pacer
        and "target.Mode == VulkanTargetSchedulingMode::Relative" in vulkan_pacer
        and "EvaluateRelativeTargetDuration" in vulkan_pacer
        and "EvaluateAbsoluteTargetTime" in vulkan_pacer,
        "the relative flag must be attached only in relative mode, from a separate evaluator",
        failures,
    )
    require(
        "class VulkanRelativeCadence" in read("src/VulkanPresentTimingModel.h")
        and "VariableRefreshInterval" in read("src/VulkanPresentTimingModel.h")
        and "RelativeCadence.Commit();" in vulkan_pacer
        and "RelativeCadence.Abandon();" in vulkan_pacer
        and "RelativeCadence.Reset();" in vulkan_pacer,
        "relative cadence must be transactional and reset on mode and lifecycle changes",
        failures,
    )
    require(
        "absoluteCapable || relativeCapable" in vulkan_pacer,
        "FIFO_LATEST_READY must accept a relative-capable scheduler",
        failures,
    )
    require(
        "TestRelativeFallbackWhenSurfaceLacksAbsolute" in vulkan_timing_tests
        and "caps.AbsoluteTimingSurface = false;" in vulkan_timing_tests
        and "TestRelativeCadence60On144" in vulkan_timing_tests
        and "TestRelativeSuppressedByVendorAndSpeed" in vulkan_timing_tests,
        "the relative scheduling path must be covered by the pure capability and cadence tests",
        failures,
    )
    require(
        "target_mode,target_value_ns" in read("src/VulkanPresentLatencyCapture.cpp"),
        "the A/B capture must record which target mode a frame actually used",
        failures,
    )
    # A periodic log line cannot prove a target was a whole number of refresh
    # intervals: the interval it prints is the current one, which may have moved
    # on since the target was generated. Capture the generating inputs instead.
    require(
        all(token in read("src/VulkanPresentLatencyCapture.cpp") for token in (
            "target_generation_refresh_interval_ns",
            "relative_quanta",
            "relative_accumulator_before_ns",
            "relative_accumulator_after_ns",
        ))
        and "TestRelativeCadenceReportsItsInputs" in vulkan_timing_tests,
        "the capture must let the relative cadence be re-derived per present",
        failures,
    )
    require(
        "MaxQuantizableInterval" in read("src/VulkanPresentTimingModel.h")
        and "TestRelativeCadenceRejectsAbsurdRefreshInterval" in vulkan_timing_tests,
        "a malformed refresh interval must not overflow the cadence accumulator",
        failures,
    )
    # The A/B "target scheduling active" ratio decides whether a policy is
    # judged to have scheduled. It must therefore be sourced from what the
    # accepted present carried, not from the resolver's permission for that
    # frame -- a bootstrap frame or a queue-full retry has permission and no
    # target, and counting those as hits would inflate the ratio.
    require(
        "ResolveVulkanAppliedTarget" in vulkan_pacing_policy
        and "struct VulkanAppliedTarget" in vulkan_pacing_policy
        and "CaptureState(const PresentMetadata& metadata) const noexcept;"
            in vulkan_pacer_header
        and "PresentPacer.CaptureState(genericPresentMetadata)" in vulkan_presenter
        and "snapshot.TargetTimeScheduling = LastDecision.TargetTimeScheduling"
            not in vulkan_pacer
        and ordered(
            function_body(
                vulkan_pacer,
                "VulkanPresentPacer::StateSnapshot VulkanPresentPacer::CaptureState(",
                "} // namespace melonDS",
            ),
            [
                "metadata.TimingAttached, metadata.TargetMode, metadata.TargetValueNs",
                "snapshot.TargetTimeScheduling = applied.Applied;",
                "snapshot.TargetMode = static_cast<int>(applied.Mode);",
                "snapshot.TargetValueNs = applied.ValueNs;",
            ],
        )
        and "TestAppliedTargetReflectsThePresent" in vulkan_timing_tests,
        "the A/B capture must record the target the accepted present actually carried",
        failures,
    )
    require(
        "metadata.RelativeRequest = VulkanRelativeCadence::Request{};" in vulkan_pacer
        and ordered(
            function_body(
                vulkan_pacer,
                "bool VulkanPresentPacer::PrepareRetryWithoutTiming(",
                "void VulkanPresentPacer::NotifyPresentResult(",
            ),
            [
                "metadata.TimingAttached = false;",
                "metadata.TargetValueNs = 0;",
                "metadata.TargetMode = VulkanTargetSchedulingMode::None;",
                "metadata.RelativeRequest = VulkanRelativeCadence::Request{};",
            ],
        ),
        "a queue-full retry must clear every target field the capture reads",
        failures,
    )
    # A rejected vkQueuePresentKHR still enqueues the semaphore waits it was
    # given. A retry that re-waits them asks for a second signal nothing will
    # produce: the present queue deadlocks and validation reports
    # VUID-vkQueuePresentKHR-pWaitSemaphores-03268 (observed 20 times over 22
    # swapchain rebuilds in the minimize/restore event-matrix phase). Ordering
    # is preserved without them because the first call's wait is already ahead
    # of the retry on the same present queue.
    require(
        ordered(
            function_body(
                vulkan_pacer,
                "bool VulkanPresentPacer::PrepareRetryWithoutTiming(",
                "void VulkanPresentPacer::NotifyPresentResult(",
            ),
            [
                "present.waitSemaphoreCount = 0;",
                "present.pWaitSemaphores = nullptr;",
            ],
        ),
        "a queue-full retry must drop the wait semaphores the rejected present "
        "already enqueued waits for",
        failures,
    )
    require(
        all(token in read("tools/perf/aggregate-vulkan-latency.py") for token in (
            "if applied and row_mode != 0:",
            "_check_row",
            "relative_quanta",
            "INVALID",
        )),
        "the aggregator must reject captures whose target columns contradict each other",
        failures,
    )
    # VK_NV_low_latency2 puts PRESENT_END "when vkQueuePresentKHR returns". Any
    # pacer or capture bookkeeping placed before the end markers is folded into
    # both the Reflex latency report and the host input-to-present figure, which
    # moves the boundary the A/B is measuring.
    require(
        ordered(
            function_body(
                vulkan_presenter,
                # Both anchors are unique in this file, so the window is exactly
                # the present span and its immediate bookkeeping.
                "std::unique_lock<std::mutex> queueLock(Device.GetQueueMutex());",
                "if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)",
            ),
            [
                # The lock is taken before the span and released after it, so
                # neither queue contention nor the release lands inside the
                # measured present. unique_lock with an explicit unlock is what
                # makes that boundary checkable here rather than implied by a
                # closing brace.
                "queueLock(Device.GetQueueMutex());",
                # AMD PRESENT must be associated with the queue operation
                # after queue ownership is acquired, not behind contention.
                "AntiLag.EndFrame(LowLatencyFrameIndex);",
                "Reflex.MarkPresentStart();",
                "LatencyCapture.MarkPresentStart();",
                "res = fns.QueuePresentKHR(",
                "PresentPacer.PrepareRetryWithoutTiming(",
                "res = fns.QueuePresentKHR(",
                "LatencyCapture.MarkPresentEnd();",
                "Reflex.MarkPresentEnd();",
                "queueLock.unlock();",
                # Bookkeeping stays outside the span.
                "PresentPacer.NotifyPresentResult(",
                "LatencyCapture.Commit(",
                "Reflex.NotifyPresented();",
            ],
        ),
        "present markers must bracket only QueuePresent, inside the queue lock",
        failures,
    )
    require(
        all(token in vulkan_latency_capture for token in (
            "swapchain_generation",
            "p.SwapchainGeneration",
        ))
        and vulkan_pacer_header.count("u64 SwapchainGeneration = 0;") >= 2
        and ordered(
            function_body(
                vulkan_pacer,
                "void VulkanPresentPacer::OnSwapchainCreated(",
                "void VulkanPresentPacer::OnSwapchainDestroyed() noexcept",
            ),
            ["++SwapchainGeneration;", "ResetTimingLifecycle();"],
        )
        and "snapshot.SwapchainGeneration = SwapchainGeneration;" in vulkan_pacer
        and all(token in read("tools/perf/aggregate-vulkan-latency.py") for token in (
            "missing required swapchain_generation column",
            "swapchain_generation changed",
            "swapchain_recreations_in_window",
            "lifecycle counters reset",
        )),
        "latency capture must identify swapchain generations and invalidate reset-crossing runs",
        failures,
    )
    event_matrix = read("tools/testing/vulkan-present-event-matrix.ps1")
    require(
        all(token in event_matrix for token in (
            "[switch]$ValidateSync",
            "validate_sync = true",
            "CURRENT-VALIDATION-ENABLED",
            "SYNC-HAZARD",
            "$err",
        )),
        "the event matrix must support confirmed Synchronization Validation and scan stderr",
        failures,
    )
    # The bounded wait is skipped whenever there is nothing to wait on, so the
    # policy's permission is not evidence that vkWaitForPresent2KHR ran.
    require(
        "bounded_wait_attempted" in read("src/VulkanPresentLatencyCapture.cpp")
        and "BoundedWaitAttempted" in vulkan_pacer_header
        and "WaitAttemptedThisFrame = true;" in vulkan_pacer
        and "WaitAttemptedThisFrame = false;" in vulkan_pacer
        and "snapshot.BoundedWaitAttempted = WaitAttemptedThisFrame;" in vulkan_pacer
        and "bounded_wait_attempted_ratio" in read("tools/perf/aggregate-vulkan-latency.py"),
        "the capture must separate an allowed bounded wait from one that actually ran",
        failures,
    )
    # wait_timeout_count is cumulative for the whole run, so the warm-up value
    # has to be subtracted before a rate is computed -- otherwise warm-up
    # timeouts are charged to the measured window and can fake a breach of the
    # runbook threshold. The rate is per attempted wait, not per frame.
    require(
        all(token in read("tools/perf/aggregate-vulkan-latency.py") for token in (
            "wait_timeouts_at_warmup",
            "wait_timeouts_in_window",
            "wait_timeout_rate",
            "self.wait_timeouts_in_window / self.bounded_wait_attempted",
        )),
        "the wait timeout rate must exclude warm-up and divide by attempted waits",
        failures,
    )
    require(
        "MaxTimeDomainEnumerateAttempts" in vulkan_pacer
        and "if (result != VK_INCOMPLETE)" in vulkan_pacer
        and "if (!enumerated)" in vulkan_pacer,
        "time-domain enumeration must retry a bounded number of times on VK_INCOMPLETE",
        failures,
    )
    # --- target-time presentation scheduling contract -----------------------
    # The JustInTime policies are named after requesting a presentation
    # deadline. These checks exist because the path once carried the name while
    # leaving VkPresentTimingInfoEXT::targetTime at zero, which is indis-
    # tinguishable from telemetry-only at runtime unless somebody reads the log.
    require(
        all(token in vulkan_pacer for token in (
            "metadata.Timing.targetTime = target.ValueNs;",
            "VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT",
            "metadata.Timing.timeDomainId = TargetTimeDomainId;",
            "metadata.Timing.targetTimeDomainPresentStage =",
        )),
        "the Vulkan JustInTime path must request a real target presentation time",
        failures,
    )
    require(
        "TargetFrameIntervalNs = normalSpeed ? targetFrameIntervalNs : 0;" in vulkan_pacer
        and "storedFrametimeStep * 1'000'000'000.0" in emu
        and "16'666'667" not in vulkan_pacer
        and "16'666'667" not in vulkan_presenter,
        "the presentation target interval must come from the frame limiter, never a 60 FPS constant",
        failures,
    )
    require(
        "properties.timingPropertiesCounter != TimingPropertiesCounter" in vulkan_pacer
        and "properties.timeDomainsCounter != TimeDomainsCounter" in vulkan_pacer
        and "RefreshTimeDomains()" in vulkan_pacer,
        "swapchain timing properties and time domains must be re-queried when their counters change",
        failures,
    )
    require(
        "if (result == VK_NOT_READY)" in vulkan_pacer
        and "TimingPropertiesRetryPending = true;" in vulkan_pacer
        and "TimeDomainsRetryPending = true;" in vulkan_pacer,
        "VK_NOT_READY must be a pending retry state, never a fatal timing failure",
        failures,
    )
    require(
        ordered(
            function_body(
                vulkan_pacer,
                "void VulkanPresentPacer::OnSwapchainDestroyed() noexcept",
                "VulkanPacingCapabilities VulkanPresentPacer::BuildCapabilities(",
            ),
            ["ResetTimingLifecycle();"],
        )
        and "TimingModel.Reset();" in vulkan_pacer
        and "TimingModel.ClearTimeDomain();" in vulkan_pacer,
        "swapchain destruction must reset the whole timing and time-domain model",
        failures,
    )
    require(
        all(token in vulkan_pacing_policy for token in (
            "VulkanJitFallbackReason::VendorLatencyApiOwnsPacing",
            "if (reflexActive)",
            "if (antiLagActive)",
        ))
        and ordered(
            vulkan_pacing_policy,
            [
                "VulkanPacingAuthority::NvidiaReflex, false, noMode, false",
                "VulkanPacingAuthority::AmdAntiLag2, false, noMode, false",
            ],
        )
        and "TestVendorLatencyApisWin" in vulkan_timing_tests,
        "active Reflex or Anti-Lag 2 must keep both generic mechanisms off",
        failures,
    )
    require(
        "bool IsFifoFamily(VkPresentModeKHR mode) noexcept" in vulkan_pacer
        and "caps.FifoPresentMode = FifoFamilyPresentMode;" in vulkan_pacer
        and "VulkanJitFallbackReason::NonFifoPresentMode" in vulkan_pacing_policy
        and "if (!caps.FifoPresentMode)" in vulkan_pacing_policy,
        "target-time scheduling must stay inactive outside the FIFO present-mode family",
        failures,
    )
    require(
        ordered(
            function_body(
                vulkan_pacer,
                "bool VulkanPresentPacer::ShouldUseFifoLatestReady() const noexcept",
                "void VulkanPresentPacer::ResetTimingLifecycle() noexcept",
            ),
            [
                "VulkanPresentPacingPolicy::JustInTimeFifoLatestReady",
                "TargetSchedulingLifecycleFailed",
                "PresentTimingAbsolute",
                "TimeDomainQueryAvailable",
            ],
        ),
        "FIFO_LATEST_READY must be gated on the target-time scheduling capability path",
        failures,
    )
    require(
        "VK_ERROR_DEVICE_LOST" in vulkan_pacer
        and "TimingModel.AbandonPresent();" in vulkan_pacer
        and "TimingModel.CommitPresent();" in vulkan_pacer,
        "device loss must surface and rejected presents must not consume a presentation sequence",
        failures,
    )
    require(
        "VulkanPresentTimingModel" in read("src/VulkanPresentTimingModel.h")
        and "melonprime_vulkan_present_timing_tests" in cmake
        and "melonprime_vulkan_present_timing_check" in cmake,
        "the pure presentation timing model must be built and executed by every Vulkan build",
        failures,
    )
    require(
        "header version\n    359 (2026)" in vulkan_compat
        and "VK_KHR_PRESENT_ID_2_EXTENSION_NAME" in vulkan_compat
        and "VK_EXT_PRESENT_TIMING_EXTENSION_NAME" in vulkan_compat,
        "modern Vulkan compatibility declarations must remain pinned and complete",
        failures,
    )
    require(
        "GenericPresentTimingRequested" in vulkan_device
        and "!shared->GenericPresentTimingRequested" in vulkan_device
        and "out.WaitForPresent2KHR = reinterpret_cast" in vulkan_loader
        and "MELONPRIME_VK_LOAD_DEVICE(WaitForPresent2KHR" not in vulkan_loader,
        "optional generic WSI support and entry points must remain fail-soft",
        failures,
    )
    require(
        ordered(
            emu,
            [
                "BeginIntelXeLLFrame();",
                "MarkIntelXeLLInputSample();",
                "inputRefreshJoystickState();",
            ],
        ),
        "Intel XeLL must Sleep/start Simulation -> Input Sample -> input",
        failures,
    )
    require(
        ordered(
            xell,
            [
                "void DX12IntelXeLL::BeginFrame()",
                "Functions.Sleep(",
                "SendMarker(DX12IntelXeLLMarker::SimulationStart)",
            ],
        ),
        "Intel XeLL must call xellSleep before the first marker for a frame",
        failures,
    )
    require(
        ordered(
            screen,
            [
                "BeginIntelXeLLPresent();",
                "presenter.Present(vsync)",
                "EndIntelXeLLPresent();",
            ],
        ),
        "Intel XeLL PRESENT markers must bracket the DXGI Present call",
        failures,
    )
    require(
        all(
            f"DX12IntelXeLLMarker::{marker}" in xell
            for marker in (
                "SimulationStart",
                "SimulationEnd",
                "RenderSubmitStart",
                "RenderSubmitEnd",
                "PresentStart",
                "PresentEnd",
                "InputSample",
            )
        )
        and "if (!RenderSubmitStarted)\n        MarkRenderSubmitStart();" in xell
        and "if (!PresentStarted)\n        MarkPresentStart();" in xell,
        "Intel XeLL must deliver the complete required marker set on early-exit frames",
        failures,
    )

    require(
        ordered(
            emu,
            [
                "BeginReflexFrame();",
                "MarkReflexInputSample();",
                "inputRefreshJoystickState();",
                "RunFrameHook();",
                "SetKeyMask(",
                "MarkReflexSimulationStart();",
            ],
        ),
        "DX12 Reflex must be Sleep -> Input Sample -> input -> Simulation Start",
        failures,
    )
    require(
        ordered(
            emu,
            [
                "beginVulkanLowLatencyFrame(",
                "markVulkanReflexInputSample();",
                "inputRefreshJoystickState();",
                "RunFrameHook();",
                "SetKeyMask(",
                "markVulkanReflexSimulationStart();",
            ],
        ),
        "Vulkan Reflex must be Sleep -> Input Sample -> input -> Simulation Start",
        failures,
    )

    begin = function_body(
        dx12,
        "void DX12NvidiaReflex::BeginFrame()",
        "void DX12NvidiaReflex::MarkInputSample()",
    )
    require(begin != "", "DX12 BeginFrame body was not found", failures)
    require(
        "Marker::SimulationStart" not in begin,
        "DX12 BeginFrame must perform Sleep only, not open Simulation",
        failures,
    )
    require(
        "void DX12NvidiaReflex::MarkSimulationStart()" in dx12
        and "!InputSampled" in dx12,
        "DX12 must gate Simulation Start on an earlier Input Sample",
        failures,
    )
    require(
        "void VulkanNvidiaReflex::MarkSimulationStart()" in vulkan
        and "!InputSampled" in vulkan,
        "Vulkan must gate Simulation Start on an earlier Input Sample",
        failures,
    )
    require(
        "if (!IsFramePathAvailable())" in vulkan
        and "return IsFramePathAvailable() && FrameOpen;" in read("src/VulkanNvidiaReflex.h"),
        "Vulkan must keep Sleep, markers and Present ID correlation live in Off mode",
        failures,
    )

    for token in (
        "hasTimelineExtension",
        "timelineFeatures.timelineSemaphore == VK_TRUE",
        "hasPresentIdExtension",
        "presentIdFeatures.presentId == VK_TRUE",
        "antiLagFeatures.antiLag == VK_TRUE",
    ):
        require(token in probe, f"Vulkan UI feature probe is missing {token}", failures)

    require(
        "cd6918f60b3c9a0476fdfe7e89bb32330602049d" in dx12,
        "NVAPI ABI source commit is not pinned",
        failures,
    )
    require(
        "390aa4a8c8655d0ae6e90079db2c85e103a96da3" in amd
        and "v2.0.4" in amd,
        "AMD Anti-Lag 2 v2.0.4 ABI recheck is not recorded",
        failures,
    )
    require(
        "8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0" in xell
        and "XeLL 1.3.2.10" in xell,
        "Intel XeLL ABI source/version is not pinned",
        failures,
    )
    require(
        "{MelonPrime::CfgKey::IntelXeLLEnabled, false}" in config
        and "onIntelXeLLModeChanged" in video_settings
        and "Intel Xe Low Latency (XeLL):" in video_settings,
        "Intel XeLL persisted config/UI must remain wired and default Off",
        failures,
    )
    require(
        "InitializeForTesting" in xell_header
        and "DX12IntelXeLLApi" in xell_header
        and "FakeXeLL" in xell_tests
        and "TestInitializationFailures" in xell_tests
        and "TestSleepModeFailures" in xell_tests
        and "TestFrameFailures" in xell_tests
        and "TestPacingResolver" in xell_tests,
        "Intel XeLL production state machine must remain injectable and fake-tested",
        failures,
    )
    require(
        "xellSetLoggingCallback" in xell
        and "DX12IntelXeLLLoggingLevel::Warning" in xell
        and "DX12IntelXeLLLoggingLevel::Error" in xell,
        "Intel XeLL runtime logging callback and build-sensitive level are missing",
        failures,
    )
    require(
        all(token in xell_header for token in (
            "RuntimePresent", "SupportedByProbe", "ContextCreated",
            "SleepModeApplied", "ActualEnabled", "MinimumIntervalUs",
        ))
        and "hardwareValidation=pending" in xell,
        "Intel XeLL diagnostics must distinguish requested/runtime/support/context/actual state",
        failures,
    )
    require(
        "periodic-summary" in xell
        and "DX12 presentation result HRESULT=" in presenter,
        "developer telemetry must include periodic XeLL state and Present results",
        failures,
    )
    require(
        "{MelonPrime::CfgKey::IntelXeLLPacingPolicy, 0}" in config
        and "IntelXeLLPacingPolicy =" in emu
        and "MELONPRIME_ENABLE_DEVELOPER_FEATURES" in emu
        and "Compatibility" in pacing
        and "NvidiaReflex, false, true, false" in pacing
        and "ResolveDX12LowLatencyPacing" in pacing
        and "ShouldBypassDX12HostLimiter" in emu
        and "ShouldBypassPresentWait" in screen,
        "DX12 pacing must avoid duplicate Reflex/DXGI waits while XeLL experiments default to Compatibility",
        failures,
    )
    require(
        "MELONPRIME_TEST_XELL_NEGATIVE_PATH" in main_source
        and "production negative-path test" in main_source
        and "lblIntelXeLL->setEnabled(intelXeLLEnabled)" in video_settings
        and "a persisted On setting must remain harmless" in xell_tests,
        "non-Intel runtime/UI/persisted-On negative path must remain testable",
        failures,
    )
    require(
        "DX12IntelXeLL.cpp" in cmake
        and "Bundling Intel XeLL runtime and notices" in cmake
        and "melonprime_xell_state_machine_check ALL" in cmake,
        "Intel XeLL source/runtime deployment is missing from CMake",
        failures,
    )

    xell_runtime = ROOT / "res/third_party/intel-xell/libxell.dll"
    require(xell_runtime.is_file(), "Intel XeLL runtime DLL is missing", failures)
    if xell_runtime.is_file():
        digest = hashlib.sha256(xell_runtime.read_bytes()).hexdigest()
        require(
            digest == "d2030dcd694fda8f2ec7e044b13e6db8f0b56d4ba9113a5efad334e3f3ded8c7",
            "Intel XeLL runtime DLL does not match pinned XeSS SDK 3.0.2",
            failures,
        )
        required_exports = {
            "xellD3D12CreateContext",
            "xellDestroyContext",
            "xellSetSleepMode",
            "xellGetSleepMode",
            "xellSleep",
            "xellAddMarkerData",
            "xellGetVersion",
            "xellSetLoggingCallback",
        }
        try:
            missing_exports = required_exports - pe_exports(xell_runtime)
            require(
                not missing_exports,
                "Intel XeLL runtime lacks required exports: "
                + ", ".join(sorted(missing_exports)),
                failures,
            )
        except (IndexError, struct.error, UnicodeDecodeError, ValueError) as error:
            failures.append(f"Intel XeLL PE export audit failed: {error}")

    for filename in ("LICENSE.txt", "third-party-programs.txt", "README.md"):
        require(
            (xell_runtime.parent / filename).is_file(),
            f"Intel XeLL redistribution file is missing: {filename}",
            failures,
        )

    if failures:
        print("Low-latency contract audit FAILED:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("Low-latency contract audit PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
