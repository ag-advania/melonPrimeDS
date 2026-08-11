#!/usr/bin/env python3
"""Guard Vulkan/DX12 low-latency marker ordering and pinned vendor ABIs."""

from pathlib import Path
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


def main() -> int:
    failures: list[str] = []
    emu = read("src/frontend/qt_sdl/EmuThread.cpp")
    dx12 = read("src/DX12NvidiaReflex.cpp")
    vulkan = read("src/VulkanNvidiaReflex.cpp")
    probe = read("src/VulkanFeatureProbe.cpp")
    amd = read("src/DX12AmdAntiLag2.cpp")

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

    if failures:
        print("Low-latency contract audit FAILED:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("Low-latency contract audit PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
