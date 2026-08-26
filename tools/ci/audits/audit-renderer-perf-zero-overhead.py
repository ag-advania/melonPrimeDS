#!/usr/bin/env python3
"""Audit the shared compile-time gate for Vulkan/DX12 renderer telemetry."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[3]
GATE = "MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def guarded_lines(source: str, macro: str) -> set[int]:
    """Return lines inside an active #if defined(macro) region.

    The audit only needs the simple, explicit guards used by the renderer
    sources. Tracking the target guard through #else prevents an OFF facade
    from accidentally being treated as instrumented code.
    """

    stack: list[bool] = []
    result: set[int] = set()
    target_if = re.compile(r"#\s*(?:if|ifdef)\b.*\b" + re.escape(macro) + r"\b")
    for number, line in enumerate(source.splitlines(), 1):
        stripped = line.strip()
        if re.match(r"#\s*(?:if|ifdef)\b", stripped):
            stack.append(bool(target_if.search(stripped)))
        elif re.match(r"#\s*else\b", stripped):
            if stack:
                stack[-1] = not stack[-1]
        elif re.match(r"#\s*endif\b", stripped):
            if stack:
                stack.pop()
        if any(stack):
            result.add(number)
    return result


def require_occurrences_guarded(
    source: str, token: str, filename: str, failures: list[str]
) -> None:
    guarded = guarded_lines(source, GATE)
    for number, line in enumerate(source.splitlines(), 1):
        if token in line and number not in guarded:
            failures.append(
                f"{filename}:{number}: {token!r} is outside the telemetry compile-time gate"
            )


def require_occurrences_unguarded(
    source: str, token: str, filename: str, failures: list[str]
) -> None:
    """Require correctness-critical tokens to stay outside the telemetry gate."""

    guarded = guarded_lines(source, GATE)
    for number, line in enumerate(source.splitlines(), 1):
        if token in line and number in guarded:
            failures.append(
                f"{filename}:{number}: correctness-critical {token!r} "
                "must remain outside renderer telemetry gate"
            )


def main() -> int:
    failures: list[str] = []
    cmake = read("src/frontend/qt_sdl/CMakeLists.txt")
    dx12_perf = read("src/DX12Perf.h")
    vulkan_perf = read("src/VulkanPerf.h")
    dx12_context_h = read("src/DX12Context.h")
    # The timestamp query pool travels with the command context that records
    # into it, so its telemetry-off facade is ratcheted there.
    dx12_command_context_h = read("src/DX12CommandContext.h")
    dx12_context_cpp = read("src/DX12Context.cpp")
    vulkan_sync_h = read("src/VulkanSync.h")
    vulkan_sync_cpp = read("src/VulkanSync.cpp")
    vulkan_loader_h = read("src/VulkanLoader.h")
    vulkan_loader_cpp = read("src/VulkanLoader.cpp")
    dx12_gpu_timestamp = read("src/DX12GpuTimestamp.h")
    vulkan_gpu_timestamp = read("src/VulkanGpuTimestamp.h")

    option = (
        'option(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY\n'
        '    "Compile Vulkan/DX12 renderer performance telemetry instrumentation"\n'
        '    OFF)'
    )
    require(option in cmake, "shared telemetry option must default to OFF", failures)
    require(
        "if (MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)" in cmake,
        "CMake must configure the shared telemetry definition under its option",
        failures,
    )
    require(
        "target_compile_definitions(core PRIVATE\n        MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=1)"
        in cmake,
        "core must receive the shared telemetry definition only when enabled",
        failures,
    )
    require(
        "target_compile_definitions(melonDS PRIVATE\n        MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=1)"
        in cmake,
        "melonDS must receive the shared telemetry definition only when enabled",
        failures,
    )
    require(
        "MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=ON" not in cmake,
        "the renderer telemetry option must not be forced ON by the project CMake",
        failures,
    )

    for filename, source in (
        ("src/DX12Perf.h", dx12_perf),
        ("src/VulkanPerf.h", vulkan_perf),
    ):
        require(
            f"#if defined({GATE})" in source,
            f"{filename} must contain the shared compile-time gate",
            failures,
        )
        require(
            "inline constexpr bool IsEnabled() noexcept" in source,
            f"{filename} OFF facade must make IsEnabled constexpr false",
            failures,
        )
        require_occurrences_guarded(source, "std::getenv(\"MELONPRIME_PERF\")", filename, failures)
        require_occurrences_guarded(source, "Clock::now()", filename, failures)

    require(
        "inline constexpr bool IsCompiledIn() noexcept" in dx12_perf
        and "inline constexpr bool IsCompiledIn() noexcept" in vulkan_perf,
        "both performance facades must expose compile-time presence",
        failures,
    )
    require(
        "class ScopedCpuTimer" in dx12_perf
        and "constexpr explicit ScopedCpuTimer" in dx12_perf
        and "class ScopedCpuTimer" in vulkan_perf
        and "constexpr explicit ScopedCpuTimer" in vulkan_perf,
        "both OFF facades must provide empty constexpr CPU timer types",
        failures,
    )
    require(
        "inline constexpr void MaybeReport() noexcept {}" in dx12_perf
        and "inline constexpr void MaybeReport() noexcept {}" in vulkan_perf,
        "both OFF facades must make reporting a compile-time no-op",
        failures,
    )

    for filename, source, tokens in (
        (
            "src/DX12Context.h",
            dx12_context_h,
            ("TimestampQueryHeap", "TimestampReadback", "TimestampFrequency",
             "TimestampWrittenMask", "TimestampSnapshotValues"),
        ),
        (
            "src/DX12Context.cpp",
            dx12_context_cpp,
            ("CreateQueryHeap", "ResolveQueryData", "GetTimestampFrequency",
             "std::chrono::steady_clock::now()"),
        ),
        (
            "src/VulkanSync.h",
            vulkan_sync_h,
            ("TimestampQueryPool", "TimestampQueriesEnabled", "float TimestampPeriodNs = 0.0f;"),
        ),
        (
            "src/VulkanSync.cpp",
            vulkan_sync_cpp,
            ("CreateQueryPool", "CmdResetQueryPool", "CmdWriteTimestamp",
             "GetQueryPoolResults"),
        ),
        (
            "src/VulkanLoader.h",
            vulkan_loader_h,
            ("CreateQueryPool", "DestroyQueryPool", "GetQueryPoolResults",
             "CmdResetQueryPool", "CmdWriteTimestamp"),
        ),
        (
            "src/VulkanLoader.cpp",
            vulkan_loader_cpp,
            ("vkCreateQueryPool", "vkDestroyQueryPool", "vkGetQueryPoolResults",
             "vkCmdResetQueryPool", "vkCmdWriteTimestamp"),
        ),
    ):
        for token in tokens:
            require_occurrences_guarded(source, token, filename, failures)

    # These operations establish the frame-submission contract and must exist
    # in shipping builds. The telemetry gate may remove timestamp query work,
    # but it must never remove the command/fence/semaphore resources that every
    # Vulkan frame needs.
    for token in (
        "CreateCommandPool",
        "AllocateCommandBuffers",
        "CreateFence",
        "CreateSemaphore",
        "ResetFences",
        "ResetCommandPool",
        "BeginCommandBuffer",
        "EndCommandBuffer",
        "QueueSubmit",
    ):
        require_occurrences_unguarded(
            vulkan_sync_cpp, token, "src/VulkanSync.cpp", failures
        )

    require(
        "bool FrameRing::HasValidCoreFrameResources() const noexcept" in vulkan_sync_cpp,
        "FrameRing must validate all mandatory per-slot handles after creation",
        failures,
    )
    require(
        "return CoreResourcesReady;" in vulkan_sync_h,
        "FrameRing::IsValid must not treat Frames.resize() alone as successful initialization",
        failures,
    )

    require(
        "inline constexpr void WriteTimestamp(u32) noexcept {}" in dx12_command_context_h
        and "inline constexpr u64 ReadTimestampSpanNanoseconds" in dx12_command_context_h,
        "DX12 timestamp methods must have an OFF inline facade",
        failures,
    )
    require(
        "inline constexpr void WriteTimestamp(VkPipelineStageFlagBits, u32) noexcept {}"
        in vulkan_sync_h,
        "Vulkan timestamp methods must have an OFF inline facade",
        failures,
    )
    require(
        "inline constexpr void RecordDX12GpuMetric" in dx12_gpu_timestamp
        and "inline constexpr void RecordVulkanGpuMetric" in vulkan_gpu_timestamp,
        "GPU timestamp helpers must be compile-time no-ops when telemetry is OFF",
        failures,
    )

    clock_sources = (
        "src/GPU3D_DX12.cpp",
        "src/frontend/qt_sdl/MelonPrimeDX12SurfacePresenter.cpp",
        "src/GPU3D_Vulkan.cpp",
        "src/frontend/qt_sdl/MelonPrimeVulkanPresenter.cpp",
        "src/VulkanPresentPacer.cpp",
    )
    for filename in clock_sources:
        source = read(filename)
        for token in ("DX12Perf::Clock::now()", "VulkanPerf::Clock::now()"):
            require_occurrences_guarded(source, token, filename, failures)

    require(
        "SetMaximumFrameLatency(1)" in read("src/frontend/qt_sdl/MelonPrimeDX12SurfacePresenter.cpp"),
        "DX12 generic frame-latency pacing must remain present",
        failures,
    )
    require(
        "BeginFrame()" in read("src/VulkanSync.cpp")
        and "WaitForLatestSubmittedFrame" in read("src/VulkanSync.cpp"),
        "Vulkan frame pacing synchronization must remain present",
        failures,
    )

    workflow_text = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (ROOT / ".github" / "workflows").glob("**/*")
        if path.is_file()
    ) if (ROOT / ".github" / "workflows").exists() else ""
    require(
        "MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=ON" not in workflow_text,
        "release/nightly workflows must not force renderer telemetry ON",
        failures,
    )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    print("PASS: Vulkan/DX12 renderer telemetry compile-time zero-overhead contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
