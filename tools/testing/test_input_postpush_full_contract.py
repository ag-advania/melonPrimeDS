#!/usr/bin/env python3
"""Source and state-model contract for the post-df820093 input audit."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def body(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    return text[begin : text.index(end, begin + len(start))]


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")


@dataclass
class LateState:
    previous: int = 0
    needs_baseline: bool = True

    def sample(self, held: int) -> tuple[int, int]:
        if self.needs_baseline:
            pressed = released = 0
            self.needs_baseline = False
        else:
            pressed = held & ~self.previous
            released = self.previous & ~held
        self.previous = held
        return pressed, released

    def disconnect(self) -> tuple[int, int]:
        released = self.previous
        self.previous = 0
        self.needs_baseline = True
        return 0, released


def check_state_model() -> None:
    state = LateState()
    assert state.sample(0b0010) == (0, 0)  # held during connect is baseline
    assert state.sample(0b0010) == (0, 0)
    assert state.sample(0b0110) == (0b0100, 0)
    assert state.sample(0b0110) == (0, 0)
    assert state.sample(0b0010) == (0, 0b0100)
    assert state.disconnect() == (0, 0b0010)
    assert state.sample(0b1000) == (0, 0)  # reconnect has no phantom press
    assert state.sample(0) == (0, 0b1000)


def check_panel_cumulative_model() -> None:
    total = [10, 20]
    cursor = [4, 9]

    # Reset captures a boundary; motion arriving before the next consumer
    # snapshot must remain visible rather than being discarded with the reset.
    baseline = tuple(total)
    total[0] += 3
    total[1] -= 2
    cursor[:] = baseline
    assert (total[0] - cursor[0], total[1] - cursor[1]) == (3, -2)


def main() -> None:
    header = source("src/frontend/qt_sdl/EmuInstance.h")
    input_cpp = source("src/frontend/qt_sdl/EmuInstanceInput.cpp")
    game_input = source("src/frontend/qt_sdl/MelonPrimeGameInput.cpp")
    emu_thread = source("src/frontend/qt_sdl/EmuThread.cpp")
    bridge = source("src/frontend/qt_sdl/MelonPrimeThreadBridge.h")
    mac = source("src/frontend/qt_sdl/MelonPrimeRawInputMacFilter.mm")
    linux = source("src/frontend/qt_sdl/MelonPrimeRawInputLinuxFilter.cpp")

    process = body(input_cpp, "void EmuInstance::inputProcess()", "#ifdef MELONPRIME_DS\n// ===")
    late = body(
        input_cpp,
        "void EmuInstance::inputRefreshJoystickState()",
        "#endif // MELONPRIME_DS",
    )
    close = body(
        input_cpp,
        "void EmuInstance::closeJoystick(bool publishLateRelease)",
        "// distinguish between left and right modifier keys",
    )

    require(header, "struct LateJoystickSnapshot", "late snapshot storage")
    require(header, "joystickLifecycleCheckCounter", "per-instance cadence")
    require(header, "activeJoystickBindings[HK_MAX + 12]", "fixed binding table")
    if "static uint8_t" in process or "static uint8_t" in late:
        raise AssertionError("controller lifecycle cadence must not be function-static")
    if "SDL_JoystickClose" in process or "SDL_JoystickClose" in late:
        raise AssertionError("poll path bypasses central close owner")
    for needle in (
        "SDL_GameControllerClose(controller)",
        "SDL_JoystickClose(joystick)",
        "lateJoystick.hotkeyHeld = 0",
        "lateJoystick.hotkeyReleased = publishLateRelease ? released : 0",
        "hasRumble = false",
        "hasAccelerometer = false",
        "hasGyroscope = false",
    ):
        require(close, needle, "central close owner")

    require(late, "i < activeJoystickBindingCount", "active-only late scan")
    if "i < HK_MAX" in late or "i < 12" in late:
        raise AssertionError("late poll regressed to full mapping scan")
    sample_at = late.index("bindingDown[i] = joystickButtonDown")
    unlock_at = late.index("SDL_UnlockMutex(joyMutex.get())", sample_at)
    assembly_at = late.index("uint16_t nextInputMask", unlock_at)
    if not sample_at < unlock_at < assembly_at:
        raise AssertionError("numeric controller mask assembly must stay outside SDL lock")
    for needle in (
        "nextHotkeyMask & ~previousLateJoystickHotkeyMask",
        "previousLateJoystickHotkeyMask & ~nextHotkeyMask",
        "lateJoystickNeedsBaseline",
    ):
        require(late, needle, "late edge transition")

    require(game_input, "emuInstance->keyHotkeyPress", "Qt gameplay edge")
    require(
        game_input,
        "emuInstance->lateJoystick.hotkeyPressed",
        "late controller gameplay edge",
    )
    late_poll = emu_thread.index("inputRefreshJoystickState();")
    if emu_thread.index("RunFrameHook(", late_poll) < late_poll:
        raise AssertionError("late joystick poll must precede RunFrameHook")

    for needle in (
        "std::atomic<uint64_t> m_panelAimTotal",
        "std::atomic<uint64_t> m_panelAimResetBaseline",
        "uint64_t m_panelAimCursor",
        "std::atomic<uint64_t> m_center",
        "m_guiRequests.load(std::memory_order_relaxed)",
    ):
        require(bridge, needle, "ThreadBridge contract")
    if "m_panelAimX" in bridge or "m_panelAimY" in bridge:
        raise AssertionError("panel aim regressed from packed cumulative total")

    for needle in (
        "DISPATCH_QUEUE_SERIAL",
        "mouse.handlerQueue = gcHandlerQueue",
        "std::atomic<CFRunLoopRef> runLoop",
        "std::atomic<uint64_t> gcTotal",
        "std::atomic<uint64_t> hidTotal",
    ):
        require(mac, needle, "macOS serialized producer")
    if ".fetch_add(" in mac:
        raise AssertionError("macOS raw event path regressed to fetch_add")

    require(linux, "lastSourceState", "Linux common-source cache")
    require(linux, "std::min(2, raw->valuators.mask_len * 8)", "Linux X/Y decode bound")

    check_state_model()
    check_panel_cumulative_model()
    print("post-df820093 input contract: PASS")


if __name__ == "__main__":
    main()
