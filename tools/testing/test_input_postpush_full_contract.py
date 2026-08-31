#!/usr/bin/env python3
"""Source and state-model contract for the post-cc6726f input re-audit."""

from __future__ import annotations

from dataclasses import dataclass, field
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


@dataclass
class ControllerConsumerState:
    command_held: int = 0
    command_previous: int = 0
    command_snapshot_valid: bool = False
    command_needs_baseline: bool = True
    gameplay: LateState = field(default_factory=LateState)

    def project_command(self, held: int) -> None:
        self.command_held = held
        self.command_snapshot_valid = True

    def project_gameplay(self, held: int, commit_edges: bool = True) -> int:
        return self.gameplay.sample(held, commit_edges)

    def command_edge(self, keyboard_held: int = 0) -> int:
        combined = keyboard_held | self.command_held
        if self.command_needs_baseline and self.command_snapshot_valid:
            self.command_previous |= self.command_held
            self.command_needs_baseline = False
        pressed = combined & ~self.command_previous
        self.command_previous = combined
        return pressed

    def disconnect(self) -> None:
        self.command_held = 0
        self.command_snapshot_valid = False
        self.command_needs_baseline = True
        self.gameplay.disconnect()


def check_controller_pause_model() -> None:
    pause = 0b0001
    gameplay_action = 0b0010
    keyboard_command = 0b0100
    state = ControllerConsumerState()

    # Initial/reconnect held state is a command and gameplay baseline, never a
    # phantom edge. A simultaneous unrelated Qt edge remains visible.
    state.project_command(pause)
    assert state.project_gameplay(pause) == 0
    assert state.command_edge(keyboard_command) == keyboard_command
    assert state.command_edge() == 0

    # Running release then press is sampled once late and observed by the next
    # outer command decision.
    state.project_command(0)
    assert state.project_gameplay(0) == 0
    assert state.command_edge() == 0
    state.project_command(pause | gameplay_action)
    assert state.project_gameplay(pause | gameplay_action) == (
        pause | gameplay_action
    )
    assert state.command_edge() == (pause | gameplay_action)  # pause

    gameplay_previous = state.gameplay.previous
    # Paused refresh projects command state only. Release/re-press resumes, but
    # the gameplay baseline and pending-edge owner are untouched.
    state.project_command(0)
    assert state.command_edge() == 0
    assert state.gameplay.previous == gameplay_previous
    state.project_command(pause | gameplay_action)
    assert state.command_edge() == (pause | gameplay_action)  # resume
    assert state.gameplay.previous == gameplay_previous

    # Held-through-resume cannot become a second gameplay press. A nested
    # refresh also cannot consume a newly appearing gameplay edge.
    assert state.project_gameplay(pause | gameplay_action) == 0
    assert state.project_gameplay(0b1111, commit_edges=False) == 0
    assert state.gameplay.previous == gameplay_previous
    assert state.project_gameplay(0b1111) == (0b1111 & ~gameplay_previous)

    state.disconnect()
    state.project_command(pause)
    assert state.command_edge() == 0
    assert state.project_gameplay(pause) == 0


def check_controller_mapping_equivalence_model() -> None:
    # kind, physical index, predicate, input bits, hotkey bits. Duplicate
    # sources intentionally fan out to multiple logical bindings.
    rules = [
        ("button", 2, 0, 0x001, 0x000),
        ("button", 2, 0, 0x004, 0x010),
        ("hat", 0, 0x1, 0x008, 0x020),
        ("axis", 1, 0, 0x010, 0x040),
        ("axis", 1, 1, 0x020, 0x080),
        ("axis", 3, 2, 0x040, 0x100),
    ]

    def is_down(kind: str, predicate: int, value: int) -> bool:
        if kind == "button":
            return value != 0
        if kind == "hat":
            return (value & predicate) != 0
        if predicate == 0:
            return value > 16384
        if predicate == 1:
            return value < -16384
        return predicate == 2 and value > 0

    scenarios = [
        {("button", 2): 1, ("hat", 0): 1, ("axis", 1): 20000, ("axis", 3): 1},
        {("button", 2): 0, ("hat", 0): 0, ("axis", 1): -20000, ("axis", 3): 0},
        {("button", 2): 1, ("hat", 0): 2, ("axis", 1): 0, ("axis", 3): 32767},
    ]
    sources = list(dict.fromkeys((kind, index) for kind, index, *_ in rules))
    source_index = {item: index for index, item in enumerate(sources)}
    fanout = [
        (source_index[(kind, index)], kind, predicate, input_bits, hotkey_bits)
        for kind, index, predicate, input_bits, hotkey_bits in rules
    ]
    assert all(index < len(sources) for index, *_ in fanout)

    for physical in scenarios:
        legacy_input, legacy_hotkeys = 0xFFF, 0
        for kind, index, predicate, input_bits, hotkey_bits in rules:
            if is_down(kind, predicate, physical[(kind, index)]):
                legacy_input &= ~input_bits
                legacy_hotkeys |= hotkey_bits

        sampled = [physical[item] for item in sources]
        compiled_input, compiled_hotkeys = 0xFFF, 0
        for index, kind, predicate, input_bits, hotkey_bits in fanout:
            if is_down(kind, predicate, sampled[index]):
                compiled_input &= ~input_bits
                compiled_hotkeys |= hotkey_bits
        assert (compiled_input, compiled_hotkeys) == (
            legacy_input,
            legacy_hotkeys,
        )
        assert len(sampled) == len(sources) < len(rules)


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

    # The stable reader retries whenever a reset generation changes around
    # its total snapshot. Exercise the five meaningful race positions.
    def stable_read(race: str) -> int:
        total_value = 12
        boundary = 10
        generation = 0
        seen = 0
        local_cursor = 4
        fired = False
        while True:
            if race == "before_g1" and not fired:
                generation = 1
                fired = True
            g1 = generation
            if g1 != seen:
                local_cursor = boundary
                seen = g1
            if race == "after_g1" and not fired:
                generation = 1
                fired = True
            current = total_value
            if race == "after_total" and not fired:
                generation = 1
                fired = True
            g2 = generation
            if race == "after_g2" and not fired:
                generation = 1
                fired = True
            if g1 == g2:
                return current - local_cursor

    assert stable_read("before_g1") == 2
    assert stable_read("after_g1") == 2
    assert stable_read("after_total") == 2
    # A reset published after the completed snapshot applies next time; the
    # already returned pre-reset delta is not replayed by this read.
    assert stable_read("after_g2") == 8
    assert stable_read("none") == 8


def check_qt_event_edge_model() -> None:
    pending = 0

    def publish(bits: int, auto_repeat: bool = False) -> None:
        nonlocal pending
        if not auto_repeat:
            pending |= bits

    def snapshot(reentrant: bool) -> int:
        nonlocal pending
        if reentrant:
            return 0
        claimed = pending
        pending = 0
        return claimed

    publish(0b0010)  # press; release changes only the level snapshot
    assert snapshot(False) == 0b0010
    assert snapshot(False) == 0
    publish(0b0100)
    assert snapshot(True) == 0
    assert snapshot(False) == 0b0100
    publish(0b1000, auto_repeat=True)
    assert snapshot(False) == 0


def check_packed_wrap_model() -> None:
    previous_x = 0xFFFFFFFE
    current_x = (previous_x + 5) & 0xFFFFFFFF
    delta = (current_x - previous_x) & 0xFFFFFFFF
    assert delta == 5


def check_mac_handoff_model() -> None:
    gc_owner = False
    gc_handler_live = False
    gc_producer_enabled = False

    # Connect transaction: IOHID is excluded before GC can publish.
    gc_owner = True
    gc_producer_enabled = True
    gc_handler_live = True
    assert not (gc_handler_live and not gc_owner)

    # Disconnect removes new enqueue first. A previously queued callback may
    # finish while GC still owns production; after the drain transaction, a
    # late callback is rejected and IOHID may resume.
    gc_handler_live = False
    queued_gc_published = gc_owner and gc_producer_enabled
    gc_producer_enabled = False
    gc_owner = False
    late_gc_published = gc_owner and gc_producer_enabled
    hid_allowed = not gc_owner
    assert queued_gc_published
    assert not late_gc_published
    assert hid_allowed


def main() -> None:
    header = source("src/frontend/qt_sdl/EmuInstance.h")
    input_cpp = source("src/frontend/qt_sdl/EmuInstanceInput.cpp")
    game_input = source("src/frontend/qt_sdl/MelonPrimeGameInput.cpp")
    emu_thread = source("src/frontend/qt_sdl/EmuThread.cpp")
    bridge = source("src/frontend/qt_sdl/MelonPrimeThreadBridge.h")
    mac = source("src/frontend/qt_sdl/MelonPrimeRawInputMacFilter.mm")
    linux = source("src/frontend/qt_sdl/MelonPrimeRawInputLinuxFilter.cpp")
    platform = source("src/frontend/qt_sdl/MelonPrimePlatformInput.h")
    screen = source("src/frontend/qt_sdl/Screen.cpp")
    window = source("src/frontend/qt_sdl/Window.cpp")

    process = body(
        input_cpp,
        "void EmuInstance::inputProcess(bool guestFrameWillRun)",
        "#ifdef MELONPRIME_DS\n// ===",
    )
    late = body(
        input_cpp,
        "void EmuInstance::inputRefreshJoystickState(bool commitGameplayEdges)",
        "#endif // MELONPRIME_DS",
    )
    close = body(
        input_cpp,
        "void EmuInstance::closeJoystick()",
        "#ifdef MELONPRIME_DS\nvoid EmuInstance::resetJoystickConsumerState",
    )
    reset = body(
        input_cpp,
        "void EmuInstance::resetJoystickConsumerState()",
        "#endif\n\n\n// distinguish between left and right modifier keys",
    )
    sample_locked = body(
        input_cpp,
        "bool EmuInstance::sampleJoystickPhysicalLocked(",
        "bool EmuInstance::sampleJoystickPhysical(",
    )
    projection = body(
        input_cpp,
        "EmuInstance::projectJoystickPhysicalSnapshot(",
        "void EmuInstance::projectJoystickCommandState(",
    )
    command_projection = body(
        input_cpp,
        "void EmuInstance::projectJoystickCommandState(",
        "void EmuInstance::projectJoystickGameplayState(",
    )
    gameplay_projection = body(
        input_cpp,
        "void EmuInstance::projectJoystickGameplayState(",
        "void EmuInstance::refreshJoystickCommandState(",
    )

    require(header, "struct LateJoystickSnapshot", "late snapshot storage")
    require(header, "joystickLifecycleCheckCounter", "per-instance cadence")
    require(header, "kMaxJoystickCompiledEntries = 2 * (HK_MAX + 12)", "fixed table capacity")
    require(header, "joystickPhysicalSources[kMaxJoystickCompiledEntries]", "fixed physical-source table")
    require(header, "joystickFanoutRules[kMaxJoystickCompiledEntries]", "fixed fanout table")
    require(header, "qtGameplayPressPending", "Qt event edge mailbox")
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
        "controllerCommandHotkeyMask = 0",
        "controllerCommandNeedsBaseline = true",
        "lateJoystick.hotkeyHeld = 0",
        "lateJoystickNeedsBaseline = true",
    ):
        require(reset, needle, "EmuThread joystick reset owner")
    if "lateJoystick.hotkeyReleased" in late:
        raise AssertionError("unconsumed late joystick release state reappeared")
    require(input_cpp, "consumeJoystickResetPending", "reset load-first helper")
    require(input_cpp, "setJoystickLocked", "non-recursive joystick helper")

    require(sample_locked, "i < snapshot.sourceCount", "unique physical-source sample")
    require(projection, "i < joystickFanoutRuleCount", "compiled fanout projection")
    require(projection, "rule.sourceIndex < snapshot.sourceCount", "fanout index invariant")
    if "i < HK_MAX" in sample_locked + projection or "i < 12" in sample_locked + projection:
        raise AssertionError("late poll regressed to full mapping scan")
    require(sample_locked, "SDL_JoystickGetButton", "button physical sample")
    require(sample_locked, "SDL_JoystickGetHat", "hat physical sample")
    require(sample_locked, "SDL_JoystickGetAxis", "axis physical sample")
    if "SDL_LockMutex" in projection or "SDL_UnlockMutex" in projection:
        raise AssertionError("numeric controller projection must stay outside SDL lock")
    require(header, "int32_t sourceValue[kMaxJoystickCompiledEntries];", "uninitialized fixed scratch")
    if "JoystickPhysicalSnapshot physical{}" in input_cpp:
        raise AssertionError("controller fixed scratch regained full zero initialization")
    for needle in (
        "projected.hotkeyMask & ~previousLateJoystickHotkeyMask",
        "lateJoystickNeedsBaseline",
        "if (!commitGameplayEdges)",
        "if (commitGameplayEdges)\n        previousLateJoystickHotkeyMask",
    ):
        require(gameplay_projection, needle, "late edge transition")
    require(command_projection, "controllerCommandHotkeyMask", "separate command projection")
    require(process, "if (!guestFrameWillRun)", "paused command refresh gate")
    require(process, "refreshJoystickCommandState", "paused command refresh")
    require(process, "if (!joystick && lifecycleCheckDue)", "throttled absent-device probe")
    require(process, "controllerCommandHotkeyMask", "global command snapshot")
    require(process, "controllerCommandNeedsBaseline", "no-phantom command baseline")
    if "lateJoystick.hotkeyHeld" in process:
        raise AssertionError("global commands regressed to guest-frame gameplay state")
    for forbidden in (
        "previousLateJoystickHotkeyMask",
        "lateJoystickNeedsBaseline",
        "qtGameplayPressPending",
    ):
        if forbidden in body(
            input_cpp,
            "void EmuInstance::refreshJoystickCommandState()",
            "#endif\n\n\n// distinguish between left and right modifier keys",
        ):
            raise AssertionError(f"paused command refresh mutates gameplay owner: {forbidden}")
    require(late, "projectJoystickCommandState(projected)", "running command projection")
    require(late, "projectJoystickGameplayState(projected", "running gameplay projection")
    if "SDL_JoystickUpdate" in late:
        raise AssertionError("late wrapper must reuse the single physical sampler")

    if "keyHotkeyPress" in header or "lastKeyHotkeyMask" in header:
        raise AssertionError("early Qt gameplay edge baseline reappeared")
    require(game_input, "qtGameplayPressed", "late Qt gameplay edge")
    require(game_input, "qtGameplayPressPending.exchange", "normal-frame event claim")
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
    require(bridge, "generationBefore == generationAfter", "stable reset snapshot")
    require(bridge, "m_guiInputPolicy", "coherent GUI input policy")
    require(bridge, "ReadGuiInputPolicyForEmu", "single GUI policy snapshot API")
    for legacy_accessor in (
        "FocusedForEmu()",
        "CaptureWantedForEmu()",
        "PanelAvailableForEmu()",
    ):
        if legacy_accessor in bridge:
            raise AssertionError(f"split GUI policy accessor reappeared: {legacy_accessor}")
    require(game_input, "guiPolicy.focused", "policy snapshot focus reuse")
    require(game_input, "guiPolicy.captureWanted", "policy snapshot capture reuse")
    require(game_input, "guiPolicy.panelAvailable", "policy snapshot panel reuse")
    require(bridge, "m_guiWorkRevision", "edge-driven GUI reconciliation")
    require(bridge, "m_stylusPointer.load(std::memory_order_relaxed)", "changed-only stylus publication")

    for needle in (
        "DISPATCH_QUEUE_SERIAL",
        "mouse.handlerQueue = gcHandlerQueue",
        "std::atomic<CFRunLoopRef> runLoop",
        "std::atomic<uint64_t> gcTotal",
        "std::atomic<uint64_t> hidTotal",
        "std::atomic<uint32_t> backendBits",
        "backendBits.fetch_or",
        "backendBits.fetch_and",
        "dispatch_queue_set_specific",
        "DispatchGcSync",
        "gcProducerEnabled",
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
    require(linux, "std::atomic<uint64_t> total", "Linux packed cumulative total")
    if "std::atomic<int64_t> accX" in linux or "std::atomic<int64_t> accY" in linux:
        raise AssertionError("Linux split X/Y accumulators reappeared")
    require(platform, "bool resolvedOwner", "single owner resolution")
    if "PlatformInputOwnerService::IsOwner(" in platform:
        raise AssertionError("Aim source resolver re-read the process owner")
    require(game_input, "m_rawAimActiveThisFrame = resolvedAim.rawActive", "frame result reuse")
    require(screen, "isMelonPrimeInputSurfaceAuthority", "primary surface guard")
    require(window, "inputSurfaceAuthority", "primary keyboard authority")
    require(screen, "GuiWorkRevisionForGui", "draw-side revision gate")

    check_state_model()
    check_controller_pause_model()
    check_controller_mapping_equivalence_model()
    check_panel_cumulative_model()
    check_qt_event_edge_model()
    check_packed_wrap_model()
    check_mac_handoff_model()
    print("post-cc6726f input re-audit contract: PASS")


if __name__ == "__main__":
    main()
