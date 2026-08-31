#!/usr/bin/env python3
"""Source and state-model contract for the post-a3675e28 input audit."""

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

    def sample(self, held: int, commit_edges: bool = True) -> int:
        if not commit_edges:
            return 0
        if self.needs_baseline:
            pressed = 0
            self.needs_baseline = False
        else:
            pressed = held & ~self.previous
        self.previous = held
        return pressed

    def disconnect(self) -> None:
        self.previous = 0
        self.needs_baseline = True


def check_state_model() -> None:
    state = LateState()
    assert state.sample(0b0010) == 0  # held during connect is baseline
    assert state.sample(0b0010) == 0
    assert state.sample(0b0110) == 0b0100
    assert state.sample(0b1110, commit_edges=False) == 0
    assert state.previous == 0b0110  # nested sample cannot consume the edge
    assert state.sample(0b1110) == 0b1000
    assert state.sample(0b0110) == 0
    assert state.sample(0b0010) == 0
    state.disconnect()
    assert state.sample(0b1000) == 0  # reconnect has no phantom press
    assert state.sample(0) == 0


def check_panel_cumulative_model() -> None:
    total = [10, 20]
    cursor = [4, 9]

    # Reset captures a boundary; motion arriving before the next consumer
    # snapshot must remain visible rather than being discarded with the reset.
    reset_generation = 1
    seen_generation = 0
    baseline = tuple(total)
    total[0] += 3
    total[1] -= 2
    if reset_generation != seen_generation:
        cursor[:] = baseline
        seen_generation = reset_generation
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
        "void EmuInstance::inputRefreshJoystickState(bool commitGameplayEdges)",
        "#endif // MELONPRIME_DS",
    )
    close = body(
        input_cpp,
        "void EmuInstance::closeJoystick()",
        "#ifdef MELONPRIME_DS\nvoid EmuInstance::resetLateJoystickGameplayState",
    )
    reset = body(
        input_cpp,
        "void EmuInstance::resetLateJoystickGameplayState()",
        "#endif\n\n\n// distinguish between left and right modifier keys",
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
        "joystickGameplayResetPending.store(true",
        "hasRumble = false",
        "hasAccelerometer = false",
        "hasGyroscope = false",
    ):
        require(close, needle, "central close owner")
    if "lateJoystick." in close:
        raise AssertionError("physical close must not write EmuThread gameplay state")
    for needle in (
        "lateJoystick.hotkeyHeld = 0",
        "lateJoystickNeedsBaseline = true",
    ):
        require(reset, needle, "EmuThread joystick reset owner")
    if "lateJoystick.hotkeyReleased" in late:
        raise AssertionError("unconsumed late joystick release state reappeared")
    require(process, "joystickGameplayResetPending.load", "reset load-first claim")
    require(process, "joystickGameplayResetPending.exchange", "reset claim")

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
        "lateJoystickNeedsBaseline",
        "if (!commitGameplayEdges)",
        "if (commitGameplayEdges)\n        previousLateJoystickHotkeyMask",
    ):
        require(late, needle, "late edge transition")

    if "keyHotkeyPress" in header or "lastKeyHotkeyMask" in header:
        raise AssertionError("early Qt gameplay edge baseline reappeared")
    require(game_input, "qtGameplayPressed", "late Qt gameplay edge")
    require(game_input, "m_qtGameplayHotkeyPrevious", "Qt gameplay baseline")
    require(game_input, "& ~qtWheelMask", "wheel exclusion from Qt level edge")
    require(
        game_input,
        "emuInstance->lateJoystick.hotkeyPressed",
        "late controller gameplay edge",
    )
    late_poll = emu_thread.index("inputRefreshJoystickState(")
    if emu_thread.index("RunFrameHook(", late_poll) < late_poll:
        raise AssertionError("late joystick poll must precede RunFrameHook")
    require(
        emu_thread,
        "!melonPrime->IsNestedFrameAdvanceForInput()",
        "reentrant edge commit gate",
    )

    for needle in (
        "std::atomic<uint64_t> m_panelAimTotal",
        "std::atomic<uint64_t> m_panelAimGuiResetBoundary",
        "std::atomic<uint32_t> m_panelAimGuiResetGeneration",
        "uint64_t m_panelAimCursor",
        "uint32_t m_panelAimGuiResetSeen",
        "std::atomic<uint64_t> m_center",
        "m_guiRequests.load(std::memory_order_relaxed)",
    ):
        require(bridge, needle, "ThreadBridge contract")
    if "m_panelAimX" in bridge or "m_panelAimY" in bridge:
        raise AssertionError("panel aim regressed from packed cumulative total")
    require(bridge, "ResetPanelAimDeltaFromEmu", "split Emu reset owner")

    for needle in (
        "DISPATCH_QUEUE_SERIAL",
        "mouse.handlerQueue = gcHandlerQueue",
        "std::atomic<CFRunLoopRef> runLoop",
        "std::atomic<uint64_t> gcTotal",
        "std::atomic<uint64_t> hidTotal",
        "std::atomic<uint32_t> backendBits",
        "backendBits.fetch_or",
        "backendBits.fetch_and",
    ):
        require(mac, needle, "macOS serialized producer")
    if ".fetch_add(" in mac:
        raise AssertionError("macOS raw event path regressed to fetch_add")
    for legacy in ("std::atomic<bool> available", "gcActive", "hidOpen", "RecomputeAvailable"):
        if legacy in mac:
            raise AssertionError(f"legacy macOS availability state reappeared: {legacy}")

    require(linux, "lastSourceState", "Linux common-source cache")
    require(linux, "std::min(2, raw->valuators.mask_len * 8)", "Linux X/Y decode bound")
    require(linux, "receivedMotionPublished", "Linux first-motion thread shadow")

    check_state_model()
    check_panel_cumulative_model()
    print("post-a3675e28 input contract: PASS")


if __name__ == "__main__":
    main()
