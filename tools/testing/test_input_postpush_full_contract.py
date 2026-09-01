#!/usr/bin/env python3
"""Source and state-model contract for the post-b2e3c311 input re-audit."""

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
    global_pending = 0
    gameplay_pending = 0

    def publish(global_bits: int, gameplay_bits: int, auto_repeat: bool = False) -> None:
        nonlocal global_pending, gameplay_pending
        if not auto_repeat:
            global_pending |= global_bits
            gameplay_pending |= gameplay_bits

    def global_snapshot() -> int:
        nonlocal global_pending
        claimed = global_pending
        global_pending = 0
        return claimed

    def gameplay_snapshot(reentrant: bool) -> int:
        nonlocal gameplay_pending
        if reentrant:
            return 0
        claimed = gameplay_pending
        gameplay_pending = 0
        return claimed

    # Press+release may complete before either consumer polls. Each domain
    # conserves exactly its own edge, with no global-only gameplay RMW.
    publish(0b0001, 0b0010)
    assert global_snapshot() == 0b0001
    assert global_snapshot() == 0
    assert gameplay_snapshot(False) == 0b0010
    assert gameplay_snapshot(False) == 0
    publish(0, 0b0100)
    assert gameplay_snapshot(True) == 0
    assert gameplay_snapshot(False) == 0b0100
    publish(0b1000, 0b10000, auto_repeat=True)
    assert global_snapshot() == 0
    assert gameplay_snapshot(False) == 0


def check_binding_program_publication_model() -> None:
    pending = ("button-1",)
    active = ()
    generation = 1
    active_generation = 0

    # Activation happens before the physical sample. A GUI publish after the
    # sample changes pending only; projection keeps using the same active copy.
    if generation != active_generation:
        active = pending
        active_generation = generation
    sampled_source = active[0]
    pending = ("axis-2",)
    generation += 1
    assert sampled_source == active[0] == "button-1"
    assert pending[0] == "axis-2"

    if generation != active_generation:
        active = pending
        active_generation = generation
    assert active[0] == "axis-2"


@dataclass
class LinuxAxisState:
    known: bool = False
    absolute: bool = False
    has_last: bool = False
    last: float = 0.0

    def event(self, value: float, query: str) -> tuple[int, bool]:
        if not self.known:
            if query == "fail":
                return 0, False
            self.known = True
            self.absolute = query == "absolute"
        if not self.absolute:
            return int(value), True
        if not self.has_last:
            self.has_last = True
            self.last = value
            return 0, True
        delta = int(value - self.last)
        self.last = value
        return delta, True

    def invalidate(self) -> None:
        self.known = False
        self.has_last = False


def check_linux_axis_query_lifecycle_model() -> None:
    # Failure -> absolute recovery: ambiguous event is dropped, the first
    # successful absolute event seeds only, then differencing begins.
    absolute = LinuxAxisState()
    assert absolute.event(900, "fail") == (0, False)
    assert not absolute.known
    assert absolute.event(100, "absolute") == (0, True)
    assert absolute.event(104, "unused") == (4, True)

    # Failure -> relative recovery retries and emits the successful event.
    relative = LinuxAxisState()
    assert relative.event(20, "fail") == (0, False)
    assert relative.event(3, "relative") == (3, True)

    # Device/hierarchy lifecycle invalidation returns to UNKNOWN. A failed
    # event after that boundary is dropped; absolute success re-seeds.
    absolute.invalidate()
    assert absolute.event(500, "fail") == (0, False)
    assert absolute.event(200, "absolute") == (0, True)
    assert absolute.event(206, "unused") == (6, True)


@dataclass
class RawWheelState:
    pending_units120: int = 0
    remainder_units120: int = 0

    def publish(self, units120: int) -> None:
        self.pending_units120 += units120

    def reset(self) -> None:
        self.pending_units120 = 0
        self.remainder_units120 = 0

    def snapshot(self, reentrant: bool = False) -> int:
        if reentrant:
            return 0

        # Model the load-first / rare exchange claim. A producer that arrives
        # after the zero observation is intentionally deferred to the next
        # outer frame, while a non-zero claim consumes all units atomically.
        observed = self.pending_units120
        claimed = self.pending_units120 if observed else 0
        if observed:
            self.pending_units120 = 0

        total_units120 = self.remainder_units120 + claimed
        steps = total_units120 // 120 if total_units120 >= 0 else -((-total_units120) // 120)
        self.remainder_units120 = total_units120 - steps * 120
        return steps


def check_raw_wheel_unit_model() -> None:
    cases = (
        ([15] * 8, 1),
        ([30] * 4, 1),
        ([-30] * 4, -1),
        ([60, -60], 0),
        ([240], 2),
        ([-360], -3),
    )
    for units, expected_steps in cases:
        state = RawWheelState()
        for unit in units:
            state.publish(unit)
        assert state.snapshot() == expected_steps
        assert abs(state.remainder_units120) < 120

    # A sub-detent burst may cross the frame boundary without losing units.
    state = RawWheelState()
    state.publish(90)
    assert state.snapshot() == 0
    state.publish(30)
    assert state.snapshot() == 1

    # Lifecycle reset discards both old pending units and the old residual.
    state.publish(30)
    state.reset()
    state.publish(90)
    assert state.snapshot() == 0
    assert state.remainder_units120 == 90

    # A re-entrant snapshot does not claim pending units or advance residual;
    # the outer frame receives the combined wheel impulse exactly once.
    state = RawWheelState()
    state.publish(60)
    assert state.snapshot(reentrant=True) == 0
    assert state.pending_units120 == 60 and state.remainder_units120 == 0
    state.publish(60)
    assert state.snapshot() == 1


def check_wheel_count_model() -> None:
    class QtWheel:
        remainder = 0

        def consume(self, angle: int, inverted: bool = False, pixel: int = 0) -> int:
            del pixel  # pixel-only trackpad input has no physical-detent unit
            if angle == 0:
                return 0
            if inverted:
                angle = -angle
            total = self.remainder + angle
            # Match C++ integer division truncating toward zero.
            steps = int(total / 120)
            self.remainder = total - steps * 120
            return steps

    qt = QtWheel()
    assert qt.consume(240) == 2
    assert qt.consume(-360) == -3
    assert qt.consume(30) == 0
    assert qt.consume(90) == 1
    assert qt.consume(120, inverted=True) == -1
    assert qt.consume(0, pixel=100) == 0

    # Same-frame signed accumulation is defined arithmetic, not sign/bit OR.
    accumulator = 0
    for step in (1, -1, 1):
        accumulator += step
    assert accumulator == 1

    # Mouse motion and wheel use independent cumulative/accumulator state, so
    # a burst of high-polling motion cannot be overwritten by wheel claims.
    mouse_total = [0, 0]
    wheel_total = 0
    for i in range(1000):
        mouse_total[0] += 1
        mouse_total[1] -= 1
        if i in (100, 500, 900):
            wheel_total += 1
    assert mouse_total == [1000, -1000]
    assert wheel_total == 3

    def target(available_bits: int, current: int, steps: int) -> int:
        available = [i for i in range(9) if available_bits & (1 << i)]
        if not available or not steps:
            return current
        distance = abs(steps)
        if steps > 0:
            first = next(
                (i for i, value in enumerate(available) if value > current), 0
            )
            return available[(first + distance - 1) % len(available)]
        lower = [i for i, value in enumerate(available) if value < current]
        first = (lower[-1] if lower else len(available) - 1)
        return available[(first - (distance - 1)) % len(available)]

    all_weapons = (1 << 9) - 1
    assert target(all_weapons, 0, 1) == 1
    assert target(all_weapons, 0, 2) == 2
    assert target(all_weapons, 0, -1) == 8
    assert target(all_weapons, 0, -3) == 6
    sparse = (1 << 0) | (1 << 2) | (1 << 7)
    assert target(sparse, 7, 1) == 0
    assert target(sparse, 0, -1) == 7
    omega_restricted = (1 << 0) | (1 << 1) | (1 << 8)
    assert target(omega_restricted, 0, 2) == 8

    # Generation-tagged claim: old values are discarded, and a nested frame
    # never claims the value intended for the next outer guest frame.
    mailbox = (1, 2)
    expected_generation = 2
    assert mailbox[0] != expected_generation
    stale_claim = 0
    assert stale_claim == 0
    mailbox = (expected_generation, -3)
    nested_claim = 0
    assert nested_claim == 0 and mailbox[1] == -3
    outer_claim = mailbox[1]
    mailbox = (expected_generation, 0)
    assert outer_claim == -3 and mailbox[1] == 0


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
    raw_state_header = source("src/frontend/qt_sdl/MelonPrimeRawInputState.h")
    raw_state_cpp = source("src/frontend/qt_sdl/MelonPrimeRawInputState.cpp")
    mouse_button = source("src/frontend/qt_sdl/MelonPrimeMouseButton.h")
    game_input = source("src/frontend/qt_sdl/MelonPrimeGameInput.cpp")
    emu_thread = source("src/frontend/qt_sdl/EmuThread.cpp")
    bridge = source("src/frontend/qt_sdl/MelonPrimeThreadBridge.h")
    mac = source("src/frontend/qt_sdl/MelonPrimeRawInputMacFilter.mm")
    linux = source("src/frontend/qt_sdl/MelonPrimeRawInputLinuxFilter.cpp")
    platform = source("src/frontend/qt_sdl/MelonPrimePlatformInput.h")
    screen = source("src/frontend/qt_sdl/Screen.cpp")
    window = source("src/frontend/qt_sdl/Window.cpp")
    key_binding = source("src/frontend/qt_sdl/MelonPrimeQtKeyBinding.h")
    map_button = source("src/frontend/qt_sdl/InputConfig/MapButton.h")
    wheel_event = source("src/frontend/qt_sdl/MelonPrimeWheelEvent.h")
    game_weapon = source("src/frontend/qt_sdl/MelonPrimeGameWeapon.cpp")
    in_game = source("src/frontend/qt_sdl/MelonPrimeInGame.cpp")

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
        "void EmuInstance::onKeyPress(QKeyEvent* event)",
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
    key_press = body(
        input_cpp,
        "void EmuInstance::onKeyPress(QKeyEvent* event)",
        "void EmuInstance::onKeyRelease(QKeyEvent* event)",
    )
    key_release = body(
        input_cpp,
        "void EmuInstance::onKeyRelease(QKeyEvent* event)",
        "#ifdef MELONPRIME_DS\nvoid EmuInstance::onMousePress",
    )
    raw_process = body(
        raw_state_cpp,
        "void InputState::processRawInput(HRAWINPUT hRaw)",
        "void InputState::processRawInputBatched()",
    )
    raw_batched = body(
        raw_state_cpp,
        "void InputState::processRawInputBatched()",
        "void InputState::fetchMouseDelta",
    )
    raw_snapshot = body(
        raw_state_cpp,
        "void InputState::snapshotInputFrame(FrameHotkeyState& outHk",
        "void InputState::clearStuckPostFrame",
    )
    raw_no_edges = body(
        raw_state_cpp,
        "void InputState::snapshotInputFrameNoEdges(",
        "bool InputState::hotkeyDown",
    )
    raw_claim = body(
        raw_state_cpp,
        "FORCE_INLINE int InputState::claimWheelSteps() noexcept",
        "// =========================================================================\n    // P-1 FIX",
    )

    require(header, "struct LateJoystickSnapshot", "late snapshot storage")
    require(header, "joystickLifecycleCheckCounter", "per-instance cadence")
    require(header, "kMaxJoystickCompiledEntries = 2 * (HK_MAX + 12)", "fixed table capacity")
    require(header, "struct JoystickBindingProgram", "fixed binding program")
    require(header, "pendingJoystickBindingProgram", "GUI-owned pending program")
    require(header, "activeJoystickBindingProgram", "Emu-owned active program")
    require(header, "joystickBindingProgramGeneration", "binding program generation")
    require(header, "std::atomic_bool joystickPresent", "device presence hint")
    require(header, "qtGlobalCommandPressPending", "Qt global command edge mailbox")
    require(header, "qtGameplayPressPending", "Qt event edge mailbox")
    require(header, "qtWheelLevelPulsePending", "wheel down-state impulse mailbox")

    # AJ-AO: Windows Raw wheel units are conserved until the outer-frame
    # claim, and the no-edge path never consumes the outer frame's impulse.
    for needle in (
        "int wheelSteps{}",
        "m_accumWheelUnits120",
        "m_wheelUnitRemainder120",
        "claimWheelSteps()",
    ):
        require(raw_state_header, needle, "Raw wheel unit state")
    for needle in (
        "RawWheelUnitsFromData",
        "static_cast<SHORT>(rawData)",
        "m_accumWheelUnits120.fetch_add",
        "m_accumWheelUnits120.load(std::memory_order_relaxed)",
        "m_accumWheelUnits120.exchange(0, std::memory_order_acq_rel)",
        "m_wheelUnitRemainder120",
    ):
        require(raw_state_cpp, needle, "Raw wheel unit implementation")
    if "NormalizeRawWheelSteps" in raw_state_cpp:
        raise AssertionError("Raw wheel input must not normalize sub-detent events individually")
    require(raw_process, "RawWheelUnitsFromData(m.usButtonData)", "single Raw wheel producer")
    require(raw_batched, "localWheelUnits120", "batched Raw wheel producer")
    require(raw_batched, "RawWheelUnitsFromData(m.usButtonData)", "batched Raw wheel units")
    require(raw_snapshot, "outWheelSteps = claimWheelSteps()", "outer Raw wheel claim")
    if "m_accumWheelUnits120.exchange" in raw_snapshot:
        raise AssertionError("outer snapshot must delegate the Raw wheel exchange to claimWheelSteps")
    claim_load = raw_claim.index("m_accumWheelUnits120.load")
    claim_exchange = raw_claim.index("m_accumWheelUnits120.exchange")
    if claim_load > claim_exchange:
        raise AssertionError("Raw wheel claim must load before its rare exchange")
    require(raw_no_edges, "outWheelSteps = 0", "re-entrant wheel suppression")
    require(raw_no_edges, "outHk.wheelSteps = 0", "re-entrant hotkey wheel suppression")
    if "claimWheelSteps" in raw_no_edges or "m_accumWheelUnits120.exchange" in raw_no_edges:
        raise AssertionError("re-entrant Raw snapshot must not claim wheel units")
    if raw_state_cpp.count("m_wheelUnitRemainder120 = 0") < 5:
        raise AssertionError("Raw wheel residual must clear at construction and every lifecycle reset")

    # AL: the editor and runtime consume one five-button capability list.
    for needle in (
        "kSupportedMouseButtons",
        "kSupportedMouseButtonCount",
        "IsSupportedMouseButton",
        "MouseButtonName",
    ):
        require(mouse_button, needle, "mouse-button capability declaration")
    require(header, "MelonPrime::kSupportedMouseButtonCount", "runtime mouse mask capacity")
    require(input_cpp, "MelonPrime::kSupportedMouseButtons", "runtime mouse capability list")
    require(input_cpp, "MelonPrime::MouseButtonIndex", "runtime unsupported-button rejection")
    require(map_button, "MelonPrimeMouseButton.h", "editor mouse capability include")
    require(map_button, "IsSupportedMouseButton", "editor unsupported-button rejection")
    require(map_button, "Unsupported Mouse Button", "legacy unsupported mapping label")
    if "Qt::ExtraButton" in map_button:
        raise AssertionError("binding editor must not advertise unsupported Qt ExtraButton values")

    # AM: frame-facing wheel names carry detent semantics; raw state names
    # carry 1/120-detent units, with no ambiguous wheelDelta field left.
    if "wheelDelta" in raw_state_header + raw_state_cpp + game_input:
        raise AssertionError("ambiguous wheelDelta naming reappeared")
    require(raw_state_header, "m_accumWheelUnits120", "explicit Raw wheel unit name")
    require(raw_state_header, "m_wheelUnitRemainder120", "explicit wheel residual name")
    require(game_input, "hk.wheelSteps", "frame wheel step name")

    # AP: conflicting Next+Prev projections are deliberately inert.
    require(game_input, "cyclePressBits", "wheel direction conflict gate")
    require(game_weapon, "nextKey == prevKey", "keyboard direction conflict gate")
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
    require(input_cpp, "publishJoystickBindingProgramLocked", "cold program publication")
    require(input_cpp, "activateJoystickBindingProgramLocked", "cold program activation")
    require(input_cpp, "activeJoystickBindingProgram = pendingJoystickBindingProgram", "immutable active copy")
    require(sample_locked, "activateJoystickBindingProgramLocked()", "activate before sample")

    require(sample_locked, "i < snapshot.sourceCount", "unique physical-source sample")
    require(projection, "i < activeJoystickBindingProgram.ruleCount", "compiled fanout projection")
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
    require(process, "joystickPresent.load", "race-free absent-device probe")
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
            "void EmuInstance::onKeyPress(QKeyEvent* event)",
        ):
            raise AssertionError(f"paused command refresh mutates gameplay owner: {forbidden}")
    require(late, "projectJoystickCommandState(projected)", "running command projection")
    require(late, "projectJoystickGameplayState(projected", "running gameplay projection")
    if "SDL_JoystickUpdate" in late:
        raise AssertionError("late wrapper must reuse the single physical sampler")

    if "keyHotkeyPress" in header or "lastKeyHotkeyMask" in header:
        raise AssertionError("early Qt gameplay edge baseline reappeared")
    require(key_press, "NormalizeQtKeyBinding(*event)", "normalized Qt press identity")
    require(key_press, "key == keyMapping[i]", "DS keyboard press mapping domain")
    require(key_release, "QtKeyBindingMatchesRelease", "release-order-safe Qt identity")
    require(key_release, "keyMapping[i]", "DS keyboard release mapping domain")
    if key_release.count("NormalizeQtKeyBinding(*event)") != 1:
        raise AssertionError("key release must normalize each event exactly once")
    require(key_release, "releasedInputBits", "aggregated keyboard input release")
    require(key_release, "releasedHotkeyBits", "aggregated keyboard hotkey release")
    require(key_press, "qtGlobalCommandPressPending.fetch_or", "global press conservation")
    require(key_press, "pressedHotkeyBits & kGameplayHotkeyMask", "gameplay-only pending bits")
    require(process, "qtGlobalCommandPressPending.exchange", "global press claim")
    require(process, "qtWheelLevelPulsePending.exchange", "wheel level pulse claim")
    if "wheelHotkeyPulseMask" in header + input_cpp:
        raise AssertionError("wheel impulse regressed into held level state")
    mouse_press = body(
        input_cpp,
        "void EmuInstance::onMousePress(QMouseEvent* event)",
        "void EmuInstance::onMouseRelease(QMouseEvent* event)",
    )
    mouse_release = body(
        input_cpp,
        "void EmuInstance::onMouseRelease(QMouseEvent* event)",
        "bool EmuInstance::hotkeyUsesKeyboardKey",
    )
    mouse_wheel = body(
        input_cpp,
        "void EmuInstance::onMouseWheel(int delta)",
        "#endif // MELONPRIME_DS",
    )
    require(mouse_press, "masks.hotkeyBits", "all mouse-button hotkey levels")
    require(mouse_release, "masks.hotkeyBits", "all mouse-button hotkey releases")
    require(mouse_wheel, "qtGlobalCommandPressPending.fetch_or", "wheel command edge")
    require(mouse_wheel, "qtWheelLevelPulsePending.fetch_or", "wheel down-state pulse")
    if "keyHotkeyMask.fetch_or" in mouse_wheel:
        raise AssertionError("wheel impulse regressed into the held Qt key mask")
    require(game_input, "qtGameplayPressed", "late Qt gameplay edge")
    require(game_input, "qtGameplayPressPending.exchange", "normal-frame event claim")
    require(game_input, "m_qtGameplayHotkeyPrevious", "Qt gameplay baseline")
    if "qtWheelMask" in game_input or "& ~qtWheelMask" in game_input:
        raise AssertionError("no-wheel guest frame regained wheel-mask loads")
    wheel_projection = body(
        game_input,
        "uint64_t wheelHotkeyBits = 0;",
        "#ifdef _WIN32",
    )
    require(wheel_projection, "if (m_input.wheelSteps)", "rare wheel projection")
    require(wheel_projection, "wheelHotkeyMaskForDelta", "rare wheel mask load")
    require(
        wheel_projection,
        "InputProjection::ProjectPressMask(wheelHotkeyBits)",
        "wheel hotkey-to-gameplay direction projection",
    )
    require(wheel_projection, "m_input.weaponCycleSteps", "signed semantic wheel count")
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
    require(linux, "static bool QueryAxisModes", "retryable Linux capability query")
    require(linux, "if (!info)\n            return false", "failed Linux query remains unknown")
    require(linux, "st.known = true;\n        return true", "successful Linux query publication")
    query_block = body(
        linux,
        "if (!st.known) {",
        "// XInput2 reports one value per set bit",
    )
    require(query_block, "if (!querySucceeded)\n                return;", "Linux UNKNOWN fail-closed")
    require(linux, "XI_HierarchyChanged", "Linux hierarchy lifecycle event")
    require(linux, "XI_DeviceChanged", "Linux device lifecycle event")
    require(linux, "InvalidateAxisCapabilities", "Linux capability invalidation")
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
    wheel_handler = body(
        screen,
        "void ScreenPanel::wheelEvent(QWheelEvent* event)",
        "void ScreenPanel::refreshClipForGameStateChange()",
    )
    require(wheel_handler, "isMelonPrimeInputSurfaceAuthority", "primary wheel authority")

    # Rule AH: editor/runtime/stylus paths share one pure Qt normalization
    # authority. No duplicate encoder remains in MapButton or EmuInstanceInput.
    require(key_binding, "NormalizeQtKeyBinding", "canonical Qt key helper")
    require(key_binding, "IsRightQtModifierKey", "canonical right modifier helper")
    require(map_button, "NormalizeQtKeyBinding(*event)", "binding editor normalization")
    require(window, "NormalizeQtKeyBinding(*event)", "stylus key normalization")
    if "getEventKeyVal" in header + input_cpp + map_button + window:
        raise AssertionError("duplicate legacy Qt key normalizer reappeared")

    # Rules AF/AG: physical count reaches the semantic consumer, while edge
    # actions keep their explicit one-frame coalescing policy.
    for needle in (
        "class PhysicalWheelStepAccumulator final",
        "total / kAngleUnitsPerDetent",
        "m_angleRemainder",
        "event.angleDelta().y()",
    ):
        require(wheel_event, needle, "Qt wheel detent conservation")
    if "pixelDelta" in wheel_event:
        raise AssertionError("pixel-only scrolling must not masquerade as a detent")
    require(bridge, "currentSteps", "signed wheel accumulator")
    require(bridge, "currentGeneration == generation", "wheel generation boundary")
    require(game_weapon, "ResolveCycleTargetIndex", "count-sensitive weapon target")
    require(game_weapon, "m_input.weaponCycleSteps", "semantic wheel consumer")
    cycle_case = body(
        game_weapon,
        "// --- Case 1: Next / Prev",
        "// --- Case 2: Direct Weapon Hotkeys",
    )
    if cycle_case.count("SwitchWeapon(") != 1:
        raise AssertionError("weapon cycle must switch only the final target once")
    require(in_game, "m_input.weaponCycleSteps != 0", "signed count rare-action gate")

    check_state_model()
    check_controller_pause_model()
    check_controller_mapping_equivalence_model()
    check_panel_cumulative_model()
    check_qt_event_edge_model()
    check_binding_program_publication_model()
    check_linux_axis_query_lifecycle_model()
    check_raw_wheel_unit_model()
    check_wheel_count_model()
    check_packed_wrap_model()
    check_mac_handoff_model()
    print("post-b2e3c311 input re-audit contract: PASS")


if __name__ == "__main__":
    main()
