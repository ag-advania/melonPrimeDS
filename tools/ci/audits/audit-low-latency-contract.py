#!/usr/bin/env python3
"""Guard Vulkan/DX12 low-latency marker ordering and pinned vendor ABIs."""

from pathlib import Path
import hashlib
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
    xell = read("src/DX12IntelXeLL.cpp")
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
                "Functions->Sleep(",
                "SendMarker(Marker::SimulationStart)",
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
            f"Marker::{marker}" in xell
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
        "IntelXeLLEnabled" in config
        and "onIntelXeLLModeChanged" in video_settings
        and "Intel Xe Low Latency (XeLL):" in video_settings,
        "Intel XeLL persisted config and UI toggle must remain wired",
        failures,
    )
    require(
        "DX12IntelXeLL.cpp" in cmake
        and "Bundling Intel XeLL runtime and notices" in cmake,
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

    if failures:
        print("Low-latency contract audit FAILED:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("Low-latency contract audit PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
