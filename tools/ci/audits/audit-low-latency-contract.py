#!/usr/bin/env python3
"""Guard Vulkan/DX12 low-latency marker ordering and pinned vendor ABIs."""

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
        and "ResolveDX12LowLatencyPacing" in pacing
        and "ShouldBypassDX12HostLimiter" in emu
        and "ShouldBypassPresentWait" in screen,
        "developer-only XeLL pacing matrix must default to Compatibility and gate real waits",
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
