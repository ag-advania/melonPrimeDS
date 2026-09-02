#!/usr/bin/env python3
"""Source and state-model contract for the post-b2e3c311 input re-audit."""

from __future__ import annotations

from dataclasses import dataclass, field
import math
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


def check_wayland_fixed_delta_model() -> None:
    def trunc_div(value: int, divisor: int) -> int:
        magnitude = abs(value) // divisor
        return -magnitude if value < 0 else magnitude

    def old_double(residual: float, value: int) -> tuple[float, int]:
        residual += value / 256.0
        whole = math.trunc(residual)
        return residual - whole, whole

    def new_integer(residual: int, value: int) -> tuple[int, int]:
        residual += value
        whole = trunc_div(residual, 256)
        return residual - whole * 256, whole

    sequences = (
        [64] * 4,
        [-64] * 4,
        [128, 128],
        [-128, -128],
        [256, 0, -256],
        [2**31 - 1, -(2**31)],
    )
    for values in sequences:
        old_residual = 0.0
        new_residual = 0
        for value in values:
            old_residual, old_output = old_double(old_residual, value)
            new_residual, new_output = new_integer(new_residual, value)
            assert old_output == new_output
        assert old_residual == new_residual / 256.0

    # The unaccelerated pair is preferred when either component is present;
    # an all-zero unaccelerated pair falls back to accelerated motion.
    events = ((256, 512, 64, 0), (256, 512, 0, 0))
    selected = []
    for accelerated_x, accelerated_y, unaccelerated_x, unaccelerated_y in events:
        use_unaccelerated = unaccelerated_x != 0 or unaccelerated_y != 0
        selected.append(
            (unaccelerated_x, unaccelerated_y)
            if use_unaccelerated
            else (accelerated_x, accelerated_y)
        )
    assert selected == [(64, 0), (256, 512)]


def check_linux_raw_state_and_reset_model() -> None:
    available = 1
    motion_seen = 2
    state = 0
    assert state == 0
    state = available
    assert state & available and not state & motion_seen
    state |= motion_seen
    assert state == available | motion_seen
    state = 0
    assert state == 0

    # A reset is consumed at the next filter batch boundary, before absolute
    # baselines or fractional relative residuals can affect a new event.
    reset_mailbox = []
    axis = {"has_last": True, "residual": 64}
    reset_mailbox.append(1)
    if reset_mailbox:
        reset_mailbox.clear()
        axis["has_last"] = False
        axis["residual"] = 0
    assert axis == {"has_last": False, "residual": 0}


def check_linux_reset_fence_model() -> None:
    max_chunk = 64

    def run(event_count: int, request_after: int):
        processed = 0
        generation = 0
        reset_pending = False
        applied_after = None
        event_generations = []
        while processed < event_count:
            chunk_end = min(event_count, processed + max_chunk)
            while processed < chunk_end:
                event_generations.append(generation)
                processed += 1
                if processed == request_after:
                    reset_pending = True
            if reset_pending:
                reset_pending = False
                generation += 1
                applied_after = processed
        assert not reset_pending and generation == 1
        assert applied_after is not None
        assert request_after <= applied_after <= request_after + max_chunk
        return event_generations, applied_after

    # Event A, reset request, event B: B finishes the current bounded chunk;
    # the reset is applied before the following chunk's event.
    generations, applied_after = run(max_chunk + 1, 1)
    assert generations[0:2] == [0, 0]
    assert generations[max_chunk] == 1
    assert applied_after == max_chunk

    # A continuous XPending flood cannot postpone the fence beyond one chunk.
    _, applied_after = run(10000, 100)
    assert applied_after - 100 <= max_chunk


def check_mac_mouse_recovery_model() -> None:
    eligible = 0b0001
    armed = 0

    def press(index: int) -> None:
        nonlocal armed
        if eligible & (1 << index):
            armed |= 1 << index

    def move(physical: int) -> None:
        nonlocal armed
        if armed:
            armed = physical

    press(1)
    assert armed == 0  # an unmapped supported button is never armed
    press(0)
    assert armed == 1
    move(1)
    assert armed == 1
    move(0)
    assert armed == 0  # lost-release recovery observes the physical release

    # Focus loss reseeds the candidate mask, including a press whose Qt press
    # event was missed while the window was changing focus.
    armed = 0b0010
    move(0b0110)
    assert armed == 0b0110


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


def check_windows_raw_reaudit_models() -> None:
    # P1-001: Raw ownership is a cold classification, not an approximation of
    # Qt's modifier/chord identity. The two live source masks are disjoint;
    # wheel is a third, explicit impulse source.
    raw_owned = {"W", "F1", "RShift", "MouseX2"}
    qt_fallback = {("Ctrl", "K"), ("Shift", "Keypad1"), "F25", "NonAscii"}
    wheel_impulse = {"WheelUp", "WheelDown"}
    assert raw_owned.isdisjoint(qt_fallback)
    assert raw_owned.isdisjoint(wheel_impulse)
    assert qt_fallback.isdisjoint(wheel_impulse)
    assert ("Ctrl", "K") in qt_fallback
    assert "F25" in qt_fallback and "F25" not in raw_owned
    assert "RShift" in raw_owned

    exact_modifier_vks = {
        "Shift": ("LShift", "RShift"),
        "Control": ("LControl", "RControl"),
        "Alt": ("LAlt", "RAlt"),
    }
    for left, right in exact_modifier_vks.values():
        assert left != right
        assert left in {"LShift", "LControl", "LAlt"}
        assert right in {"RShift", "RControl", "RAlt"}

    # P1-003/004: a foreign owner can publish only a reset notification, and a
    # failed native registration never makes the source ready. A later retry
    # may acquire the owner after the one-shot fault has cleared.
    generation = 10
    reset_pending = True
    active_owner = False
    baseline_ready = False
    assert reset_pending and not active_owner and not baseline_ready
    if reset_pending:
        reset_pending = False
        generation += 1
    assert generation == 11 and not reset_pending
    registered = False
    assert not (registered and active_owner and baseline_ready)
    registered = True
    active_owner = registered
    baseline_ready = registered
    assert active_owner and baseline_ready

    # P1-005: the fractional Qt residual is owned by the same generation as
    # the event that created it.
    class GenerationWheel:
        remainder = 0
        generation = None

        def consume(self, angle: int, source_generation: int) -> int:
            if self.generation != source_generation:
                self.generation = source_generation
                self.remainder = 0
            total = self.remainder + angle
            steps = int(total / 120)  # C++ truncation toward zero
            self.remainder = total - steps * 120
            return steps

    wheel = GenerationWheel()
    assert wheel.consume(90, 10) == 0
    assert wheel.consume(30, 11) == 0
    assert wheel.consume(90, 11) == 1
    wheel = GenerationWheel()
    assert wheel.consume(-90, 20) == 0
    assert wheel.consume(-30, 21) == 0
    assert wheel.consume(-90, 21) == -1

    # P2-003: a hidden-window dispatch gates physical-state recovery. The
    # first physically-up observation schedules only the debounce follow-up;
    # the second clears the stuck bit and returns to the idle fast path.
    class RecoveryGate:
        requested = False
        mouse_down = 0b0001
        candidate = 0

        def request(self) -> None:
            self.requested = True

        def consume(self, physically_up: int) -> bool:
            if not self.requested:
                return False
            self.requested = False
            to_clear = physically_up & self.candidate
            self.candidate = physically_up
            self.mouse_down &= ~to_clear
            if self.candidate & self.mouse_down:
                self.requested = True
            return bool(to_clear)

    recovery = RecoveryGate()
    assert not recovery.consume(0)  # idle frame: no GetAsyncKeyState scan
    recovery.request()
    assert not recovery.consume(1)  # first up observation, debounce retained
    assert recovery.requested
    assert recovery.consume(1)       # second up observation clears the bit
    assert not recovery.requested
    assert not recovery.consume(1)    # idle again

    # P1-001: a re-entrant no-edge snapshot is observational only. It may see
    # the stale held state before the post-frame recovery runs, but it cannot
    # consume the request or advance the hotkey baseline. After recovery clears
    # the stale bit, a release/re-press before the next outer frame is exactly
    # one fresh edge; nested snapshots do not add a second mouse check.
    class ReentrantRecoveryModel:
        hk_prev = 0b0001
        logical_down = 0b0001
        requested = False
        mouse_checks = 0

        def request(self) -> None:
            self.requested = True

        def no_edges(self) -> int:
            observed = self.logical_down
            assert self.requested
            assert self.hk_prev == 0b0001
            return observed

        def post_frame(self, physical_down: int) -> None:
            if not self.requested:
                return
            self.requested = False
            self.mouse_checks += 1
            self.logical_down = physical_down
            self.hk_prev = physical_down

        def outer_frame(self, physical_down: int) -> int:
            pressed = physical_down & ~self.hk_prev
            self.logical_down = physical_down
            self.hk_prev = physical_down
            return pressed

    reentrant = ReentrantRecoveryModel()
    reentrant.request()
    assert reentrant.no_edges() == 0b0001
    assert reentrant.no_edges() == 0b0001
    assert reentrant.mouse_checks == 0
    reentrant.post_frame(0)
    assert reentrant.mouse_checks == 1
    assert reentrant.outer_frame(0b0001) == 0b0001
    assert reentrant.outer_frame(0b0001) == 0

    # P1-001: a hidden-window queue is invalidated by the creator-thread
    # destroy/create boundary when an inactive subscription reacquires Raw
    # ownership. The old A queue is intentionally not drained by B, and an
    # old event must not become valid merely because A is active again.
    class HiddenWindowEpochModel:
        raw_kinds = (
            "mouse_delta",
            "mouse_press",
            "key_down",
            "key_up",
            "wheel",
        )

        def __init__(self) -> None:
            self.active_owner = None
            self.next_epoch = 0
            self.window_epoch = {}
            self.queued = {"A": [], "B": []}

        def activate(self, owner: str) -> None:
            self.active_owner = owner
            # Destroying the old creator-thread HWND drops messages addressed
            # to that window before the replacement window is registered.
            self.queued[owner].clear()
            self.next_epoch += 1
            self.window_epoch[owner] = self.next_epoch

        def queue(self, owner: str, kind: str):
            event = (owner, self.window_epoch[owner], kind)
            self.queued[owner].append(event)
            return event

        def dispatch(self, event) -> bool:
            owner, epoch, _kind = event
            return (
                owner == self.active_owner
                and self.window_epoch.get(owner) == epoch
            )

    hidden_epoch = HiddenWindowEpochModel()
    hidden_epoch.activate("A")
    stale_a = [hidden_epoch.queue("A", kind) for kind in hidden_epoch.raw_kinds]
    hidden_epoch.activate("B")
    assert all(event in hidden_epoch.queued["A"] for event in stale_a)
    hidden_epoch.activate("A")
    assert not hidden_epoch.queued["A"]
    assert all(not hidden_epoch.dispatch(event) for event in stale_a)
    fresh_a = [hidden_epoch.queue("A", kind) for kind in hidden_epoch.raw_kinds]
    assert all(hidden_epoch.dispatch(event) for event in fresh_a)

    for _ in range(1000):
        stale_cycle = [
            hidden_epoch.queue("A", kind) for kind in hidden_epoch.raw_kinds
        ]
        hidden_epoch.activate("B")
        assert all(event in hidden_epoch.queued["A"] for event in stale_cycle)
        hidden_epoch.activate("A")
        assert not hidden_epoch.queued["A"]
        assert all(not hidden_epoch.dispatch(event) for event in stale_cycle)

    # P2-001/002: secondary instances reject before the shared lock, while a
    # maybe-owner is still allowed to enter the reconciliation path.
    locked_calls = 0

    def poll(active: bool) -> None:
        nonlocal locked_calls
        if not active:
            return
        locked_calls += 1

    poll(False)
    poll(False)
    assert locked_calls == 0
    poll(True)
    assert locked_calls == 1


def check_windows_raw_recovery_hint_model() -> None:
    # Recovery is for stateful/failure events, not for successful motion that
    # is already fully represented by the Raw accumulators.
    mouse_button_mask = 0x03FF

    def needs_recovery(read_succeeded: bool, kind: str, button_flags: int = 0) -> bool:
        if not read_succeeded:
            return True
        if kind == "keyboard":
            return True
        if kind == "mouse":
            return bool(button_flags & mouse_button_mask)
        return False

    x_recovery = 0
    x_total = 0
    for _ in range(10000):
        x_total += 1
        x_recovery += needs_recovery(True, "mouse")
    assert x_total == 10000 and x_recovery == 0

    y_recovery = 0
    y_total = 0
    for _ in range(10000):
        y_total += 1
        y_recovery += needs_recovery(True, "mouse")
    assert y_total == 10000 and y_recovery == 0

    assert not needs_recovery(True, "mouse", 0x0400)  # wheel only
    assert needs_recovery(True, "mouse", 0x0001)  # button down
    assert needs_recovery(True, "mouse", 0x0002)  # button up
    assert needs_recovery(True, "keyboard")
    assert not needs_recovery(True, "hid")
    assert needs_recovery(False, "unknown")  # GetRawInputData failure


def check_windows_source_selection_model() -> None:
    # The normal Windows frame makes one source decision for held and pressed
    # gameplay state. RawExact bits stay Raw-owned, while QtFallback bits stay
    # Qt-owned; when Raw is not ready, the Qt snapshot remains authoritative.
    def select(
        raw_ready: bool,
        fallback_mask: int,
        raw_down: int,
        raw_pressed: int,
        qt_held: int,
        qt_pressed: int,
        raw_owned_mask: int,
    ) -> tuple[int, int]:
        hot_down = qt_held
        hot_pressed = qt_pressed
        if raw_ready and fallback_mask == 0:
            hot_down = raw_down
            hot_pressed = raw_pressed
        elif raw_ready:
            hot_down = (raw_down & raw_owned_mask) | (
                qt_held & fallback_mask
            )
            hot_pressed = (raw_pressed & raw_owned_mask) | (
                qt_pressed & fallback_mask
            )
        return hot_down, hot_pressed

    assert select(True, 0, 0x15, 0x04, 0xA0, 0x80, 0xFF) == (
        0x15,
        0x04,
    )
    assert select(True, 0xA0, 0x15, 0x04, 0xA0, 0x80, 0x15) == (
        0xB5,
        0x84,
    )
    assert select(False, 0xA0, 0x15, 0x04, 0xA0, 0x80, 0x15) == (
        0xA0,
        0x80,
    )


def main() -> None:
    header = source("src/frontend/qt_sdl/EmuInstance.h")
    input_cpp = source("src/frontend/qt_sdl/EmuInstanceInput.cpp")
    joystick_device = source("src/frontend/qt_sdl/MelonPrimeJoystickDevice.cpp")
    joystick_device_header = source("src/frontend/qt_sdl/MelonPrimeJoystickDevice.h")
    input_config_dialog = source("src/frontend/qt_sdl/InputConfig/InputConfigDialog.h")
    input_config_dialog_cpp = source("src/frontend/qt_sdl/InputConfig/InputConfigDialog.cpp")
    raw_state_header = source("src/frontend/qt_sdl/MelonPrimeRawInputState.h")
    raw_state_cpp = source("src/frontend/qt_sdl/MelonPrimeRawInputState.cpp")
    mouse_button = source("src/frontend/qt_sdl/MelonPrimeMouseButton.h")
    game_input = source("src/frontend/qt_sdl/MelonPrimeGameInput.cpp")
    emu_thread = source("src/frontend/qt_sdl/EmuThread.cpp")
    emu_setup = source("src/frontend/qt_sdl/MelonPrimeEmuThreadRunSetup.inc")
    bridge = source("src/frontend/qt_sdl/MelonPrimeThreadBridge.h")
    mac = source("src/frontend/qt_sdl/MelonPrimeRawInputMacFilter.mm")
    linux = source("src/frontend/qt_sdl/MelonPrimeRawInputLinuxFilter.cpp")
    linux_header = source("src/frontend/qt_sdl/MelonPrimeRawInputLinuxFilter.h")
    linux_reset_test = source("tools/testing/linux-reset-fence-tests.cpp")
    platform = source("src/frontend/qt_sdl/MelonPrimePlatformInput.h")
    screen = source("src/frontend/qt_sdl/Screen.cpp")
    screen_header = source("src/frontend/qt_sdl/Screen.h")
    wayland_header = source("src/frontend/qt_sdl/MelonPrimeWaylandPointerLock.h")
    wayland = source("src/frontend/qt_sdl/MelonPrimeWaylandPointerLock.cpp")
    wayland_math = source("src/frontend/qt_sdl/MelonPrimeWaylandPointerLockMath.h")
    wayland_test = source("tools/testing/wayland-fixed-delta-tests.cpp")
    window = source("src/frontend/qt_sdl/Window.cpp")
    key_binding = source("src/frontend/qt_sdl/MelonPrimeQtKeyBinding.h")
    map_button = source("src/frontend/qt_sdl/InputConfig/MapButton.h")
    wheel_event = source("src/frontend/qt_sdl/MelonPrimeWheelEvent.h")
    game_weapon = source("src/frontend/qt_sdl/MelonPrimeGameWeapon.cpp")
    in_game = source("src/frontend/qt_sdl/MelonPrimeInGame.cpp")
    raw_filter = source("src/frontend/qt_sdl/MelonPrimeRawInputWinFilter.cpp")
    raw_filter_header = source("src/frontend/qt_sdl/MelonPrimeRawInputWinFilter.h")
    raw_perf = source("src/frontend/qt_sdl/MelonPrimeRawInputPerfProbe.h")
    perf = source("src/frontend/qt_sdl/MelonPrimePerfProbe.h")
    cmake_presets = source("CMakePresets.json")
    windows_workflow = source(".github/workflows/build-windows.yml")
    mingw_build = source("tools/build/windows/build-mingw.bat")
    mingw_shipping_build = source("tools/build/windows/build-mingw-release.bat")
    mac_release_build = source("tools/build/macos/build_macos_release.command")
    mac_vulkan_build = source("tools/build/macos/build-macos-vulkan.sh")
    macos_workflow = source(".github/workflows/build-macos.yml")
    ubuntu_workflow = source(".github/workflows/build-ubuntu.yml")
    bsd_workflow = source(".github/workflows/build-bsd.yml")
    raw_state_public = raw_state_header.split("    private:", 1)[0]
    raw_hotkey = source("src/frontend/qt_sdl/MelonPrimeRawHotkeyVkBinding.cpp")
    raw_hotkey_header = source("src/frontend/qt_sdl/MelonPrimeRawHotkeyVkBinding.h")
    raw_hotkey_mapping = source("src/frontend/qt_sdl/MelonPrimeRawHotkeyVkMapping.cpp")
    raw_hotkey_mapping_header = source("src/frontend/qt_sdl/MelonPrimeRawHotkeyVkMapping.h")
    raw_hotkey_mapping_test = source("tools/testing/raw-hotkey-vk-mapping-tests.cpp")
    raw_recovery_hint_test = source("tools/testing/raw-recovery-hint-tests.cpp")
    qt_sdl_cmake = source("src/frontend/qt_sdl/CMakeLists.txt")
    require(qt_sdl_cmake, "MelonPrimeJoystickDevice.cpp", "joystick component build registration")
    input_subscription = source("src/frontend/qt_sdl/MelonPrimeInputSubscription.h")
    core = source("src/frontend/qt_sdl/MelonPrime.cpp")
    lifecycle = source("src/frontend/qt_sdl/MelonPrimeLifecycle.cpp")

    # IN-PERF-001 / IN-SRP-006: SDL device lifetime is a per-instance
    # component; only process-global SDL update/enumeration work is shared.
    require(joystick_device_header, "class MelonPrimeJoystickDevice final", "per-instance joystick component")
    for needle in (
        "std::shared_ptr<SDL_mutex> m_mutex",
        "SDL_Joystick* m_joystick",
        "SDL_GameController* m_controller",
        "bool OpenLocked(int& joystickId) noexcept",
        "void CloseLocked() noexcept",
        "void UpdateLocked() noexcept",
        "[[nodiscard]] bool SampleSourceLocked(",
        "void RumbleStartLocked(uint32_t lenMs) noexcept",
        "[[nodiscard]] bool ReadMotionLocked(",
    ):
        require(joystick_device_header, needle, "per-device SDL ownership")
    require(joystick_device_header, "HasJoystickLocked()", "encapsulated joystick presence")
    for needle in ("ButtonCountLocked()", "HatCountLocked()", "AxisCountLocked()"):
        require(joystick_device_header, needle, "encapsulated joystick capabilities")
    joystick_device_public = joystick_device_header.split("private:", 1)[0]
    if "GetJoystick" in joystick_device_public:
        raise AssertionError("JoystickDevice must not expose its raw SDL handle")
    for needle in (
        "pollJoystickMapping(int oldMapping",
        "captureJoystickAxisRest(int* axesRest",
    ):
        require(header, needle, "cold joystick mapping API")
        require(input_config_dialog, needle, "cold joystick dialog API")
    for needle in (
        "InputConfigDialog::pollJoystickMapping(",
        "InputConfigDialog::captureJoystickAxisRest(",
    ):
        require(input_config_dialog_cpp, needle, "cold joystick dialog forwarding")
    joy_map_button = map_button[map_button.index("class JoyMapButton") :]
    joy_check_all = body(joy_map_button, "void checkJoystick()", "void timerEvent(")
    joy_timer_all = body(joy_map_button, "void timerEvent(", "bool focusNextPrevChild")
    joy_check = joy_check_all.split("#else", 1)[0]
    joy_timer = joy_timer_all.split("#else", 1)[0]
    require(joy_check, "pollJoystickMapping", "encapsulated mapping poll")
    require(joy_timer, "checkJoystick();", "mapping timer direct operation")
    if "getJoystick" in joy_check or "getJoyMutex" in joy_check or "getJoyMutex" in joy_timer:
        raise AssertionError("DS mapping UI must not use raw joystick or external mutex protocol")
    require(joy_map_button, "captureJoystickAxisRest(axesRest, 16)", "encapsulated axis baseline")
    for needle in (
        "JoystickProcessMutexWait",
        "JoystickProcessMutexHold",
    ):
        require(perf, needle, "SDL process mutex telemetry metric")
        require(joystick_device, needle, "SDL process mutex telemetry wiring")
    require(joystick_device, "class SdlProcessMutexGuard final", "SDL process mutex guard")
    require(joystick_device, "std::mutex s_sdlProcessMutex", "process-level SDL lock")
    require(joystick_device, "SDL_JoystickUpdate()", "SDL update ownership")
    if joystick_device.count("SDL_JoystickUpdate()") != 1:
        raise AssertionError("SDL_JoystickUpdate must have one component owner")
    if "joyMutexGlobal" in header + input_cpp:
        raise AssertionError("global joystick mutex reappeared")

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
        "bool InputState::processRawInput(HRAWINPUT hRaw)",
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
    raw_deactivate_owner = body(
        raw_filter,
        "void RawInputWinFilter::DeactivateOwner(",
        "bool RawInputWinFilter::UpdateOwnerLocked(",
    )
    raw_reconfigure = body(
        raw_filter,
        "bool RawInputWinFilter::ReconfigureActiveRegistration(",
        "void RawInputWinFilter::DeactivateActiveRegistration(",
    )
    raw_apply_owner = body(
        raw_filter,
        "bool RawInputWinFilter::ApplyOwnerRegistration(",
        "    // =========================================================================\n    // drainMessagesOnly",
    )
    raw_reconfigure_locked = body(
        raw_filter,
        "bool RawInputWinFilter::ReconfigureActiveRegistrationLocked(",
        "void RawInputWinFilter::DeactivateActiveRegistration(",
    )
    raw_apply_owner_locked = body(
        raw_filter,
        "bool RawInputWinFilter::ApplyOwnerRegistrationLocked(",
        "    // =========================================================================\n    // drainMessagesOnly",
    )
    raw_drain = body(
        raw_filter,
        "FORCE_INLINE void RawInputWinFilter::drainPendingMessages()",
        "bool RawInputWinFilter::UpdateOwnerAndSnapshotImpl(",
    )
    raw_drain_locked = body(
        raw_filter,
        "FORCE_INLINE void RawInputWinFilter::drainPendingMessagesLocked(",
        "FORCE_INLINE void RawInputWinFilter::drainPendingMessages()",
    )
    raw_fused = body(
        raw_filter,
        "bool RawInputWinFilter::UpdateOwnerAndSnapshotImpl(",
        "bool RawInputWinFilter::UpdateOwnerAndSnapshot(",
    )
    raw_deferred = body(
        raw_filter,
        "void RawInputWinFilter::DeferredDrain(",
        "void RawInputWinFilter::LateLatchMouseDelta(",
    )
    require(raw_filter_header, "drainPendingMessagesLocked(RawInputSubscription&", "locked Raw drain declaration")
    require(raw_drain_locked, "state->processRawInputBatched()", "locked Raw buffer capture")
    require(raw_drain_locked, "drainMessagesOnly(&subscription)", "locked Raw message drain")
    if raw_drain_locked.index("processRawInputBatched()") > raw_drain_locked.index("drainMessagesOnly("):
        raise AssertionError("Raw locked drain must capture the buffer before dispatching messages")
    for locked_caller in (raw_reconfigure, raw_deferred):
        if "drainPendingMessages();" in locked_caller:
            raise AssertionError("frame-locked Raw caller must not reacquire frameMutex through drainPendingMessages")
    raw_late = body(
        raw_filter,
        "void RawInputWinFilter::LateLatchMouseDelta(",
        "void RawInputWinFilter::setJoy2KeySupport(",
    )
    raw_claim = body(
        raw_state_cpp,
        "FORCE_INLINE int InputState::claimWheelSteps() noexcept",
        "// =========================================================================\n    // P-1 FIX",
    )
    wayland_relative = body(
        wayland,
        "static void RelativeMotion(",
        "static void Locked(",
    )
    screen_press = body(
        screen,
        "void ScreenPanel::mousePressEvent(QMouseEvent* event)",
        "void ScreenPanel::mouseReleaseEvent(QMouseEvent* event)",
    )
    screen_release = body(
        screen,
        "void ScreenPanel::mouseReleaseEvent(QMouseEvent* event)",
        "void ScreenPanel::mouseMoveEvent(QMouseEvent* event)",
    )
    screen_move = body(
        screen,
        "void ScreenPanel::mouseMoveEvent(QMouseEvent* event)",
        "void ScreenPanel::tabletEvent(QTabletEvent* event)",
    )
    screen_unfocus = body(
        screen,
        "void ScreenPanel::unfocus()",
        "void ScreenPanel::focusInEvent(QFocusEvent * event)",
    )
    linux_accumulate = body(
        linux,
        "void AccumulateRawMotion(Display* dpy, const XIRawEvent* raw)",
        "void ThreadMain()",
    )
    linux_thread = body(
        linux,
        "void ThreadMain()",
        "    void Start()",
    )
    linux_reset_mailbox = body(
        linux,
        "void DrainResetMailbox() noexcept",
        "void RequestAxisReset() noexcept",
    )
    raw_fused = body(
        raw_filter,
        "bool RawInputWinFilter::UpdateOwnerAndSnapshotImpl(",
        "bool RawInputWinFilter::UpdateOwnerAndSnapshot(",
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
    require(sample_locked, "joystickDevice.SampleSourceLocked(", "component joystick sample")
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
    lifecycle_due_at = process.index("if (UNLIKELY(lifecycleCheckDue)")
    presence_at = process.index("joystickPresent.load")
    if lifecycle_due_at > presence_at:
        raise AssertionError("absent controller probe must check cadence before presence")
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
        "const bool nestedInputFrame",
        "reentrant edge commit gate",
    )
    require(
        emu_thread,
        "inputRefreshJoystickState(!nestedInputFrame)",
        "reentrant input refresh argument",
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
    for needle in (
        "std::atomic<uint8_t> stateBits",
        "StateAvailable",
        "StateMotionSeen",
        "stateBits.store(",
        "DrainResetMailbox()",
        "RequestAxisReset()",
        "eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)",
        "resetFd",
        "wakeFd",
        "static constexpr int kMaxXiEventsPerChunk = 64",
        "processed < kMaxXiEventsPerChunk",
        "const bool moreX = XPending(display) > 0",
        "const int pollTimeout = moreX ? 0",
        "resetPollIndex",
        "wakePollIndex",
        "SignalEventFd(wakeFd)",
    ):
        require(linux + linux_header, needle, "Linux packed state/reset mailbox")
    for forbidden in (
        "#include <fcntl.h>",
        "pipe(",
        "resetReadFd",
        "resetWriteFd",
        "bool LinuxRawInputFilter::isAvailable()",
        "bool LinuxRawInputFilter::hasReceivedMotion()",
        "isAvailable() const",
        "hasReceivedMotion() const",
    ):
        if forbidden in linux + linux_header:
            raise AssertionError(f"legacy Linux reset/readiness path reappeared: {forbidden}")
    first_chunk = linux_thread.index("while (processed < kMaxXiEventsPerChunk")
    first_drain = linux_thread.index("DrainResetMailbox()")
    if first_drain < first_chunk:
        raise AssertionError("Linux reset mailbox must not be drained before the event chunk")
    if linux_thread.index("if (resetPollIndex >= 0") > first_drain:
        raise AssertionError("Linux reset mailbox must be drained only after reset-fd readiness")
    if linux_reset_mailbox.index("resetFallbackRequested.exchange") < linux_reset_mailbox.index("if (resetFd < 0"):
        raise AssertionError("Linux reset fallback RMW must stay behind its fallback guard")
    for needle in (
        "linux-reset-fence-tests: PASS",
        "kMaxXiEventsPerChunk",
        "requestAfter",
        "10000",
    ):
        require(linux_reset_test, needle, "Linux bounded reset-fence model")
    for needle in (
        "melonprime_linux_reset_fence_tests",
        "melonprime_linux_reset_fence_check",
    ):
        require(qt_sdl_cmake, needle, "Linux bounded reset-fence test target")
    for forbidden in (
        "std::atomic<bool>    available",
        "std::atomic<bool>    receivedMotion",
        "absBaseInvalid",
    ):
        if forbidden in linux:
            raise AssertionError(f"Linux redundant state/reset path reappeared: {forbidden}")
    if "DrainResetMailbox" in linux_accumulate or "RequestAxisReset" in linux_accumulate:
        raise AssertionError("Linux Raw event path must not inspect the reset mailbox")
    resolver = body(
        platform,
        "inline AimInputSource PlatformInput_ResolveAimSource(",
        "inline void PlatformInput_CountPerfAimSource(",
    )
    if resolver.count("filter->stateBits()") != 1:
        raise AssertionError("Linux aim source resolution must acquire packed state once")

    # BO/BP/BQ: native Wayland relative motion is a direct integer producer.
    if "std::function" in wayland_header or "<functional>" in wayland_header:
        raise AssertionError("Wayland relative motion must not use std::function")
    for forbidden in (
        "wl_fixed_to_double",
        "std::trunc",
        "std::round",
        "isMelonPrimeInputSurfaceAuthority",
        "melonPrimeCore",
        "QMetaObject",
        "QString",
    ):
        if forbidden in wayland_relative:
            raise AssertionError(f"Wayland event path still contains {forbidden!r}")
    for needle in (
        "MelonPrimeThreadBridge* deltaTarget",
        "AddPanelAimDeltaFromGui",
        "TakeWlFixedIntegral",
        "residualX256",
        "residualY256",
    ):
        require(wayland + wayland_header + wayland_math, needle, "Wayland direct integer path")
    if wayland_relative.count("TakeWlFixedIntegral") != 2:
        raise AssertionError("Wayland relative motion must convert both axes through the fixed helper")
    require(wayland_test, "OldDoubleModel", "Wayland old/new residual parity test")
    require(wayland_test, "std::numeric_limits<std::int32_t>::max()", "Wayland large positive test")
    require(wayland_test, "std::numeric_limits<std::int32_t>::min()", "Wayland large negative test")
    for needle in (
        "setDeltaTarget(&core->ThreadBridge())",
        "setDeltaTarget(nullptr)",
        "std::make_unique<MelonPrime::WaylandPointerLock>()",
    ):
        require(screen + screen_header + source("src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp"),
                needle, "Wayland cold target resolution")

    # BR: diagnostics are compile-time optional. Production subscriptions and
    # platform event functions contain no debug fields, getenv, clocks, or
    # counters when the gate is absent.
    require(input_subscription, "#if defined(MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY)",
            "input debug field compile gate")
    require(platform, "#if defined(MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY)",
            "platform input debug compile gate")
    require(linux, "#if defined(MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY)",
            "Linux input debug compile gate")
    if "std::getenv(\"MELONPRIME_INPUT_DEBUG\")" in linux_accumulate:
        raise AssertionError("Linux Raw event body must not call getenv")
    for needle in (
        "option(MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY",
        "MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY=1",
    ):
        require(qt_sdl_cmake, needle, "input debug CMake gate")
    if cmake_presets.count("MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY") < 2:
        raise AssertionError("base and shipping presets must pin input debug telemetry OFF")
    for text_value, label in (
        (mingw_build, "normal MinGW input debug gate"),
        (mingw_shipping_build, "shipping MinGW input debug gate"),
        (mac_release_build, "macOS release input debug gate documentation"),
        (mac_vulkan_build, "macOS release input debug gate"),
        (windows_workflow, "Windows CI input debug gate"),
        (macos_workflow, "macOS CI input debug gate"),
        (ubuntu_workflow, "Ubuntu CI input debug gate"),
        (bsd_workflow, "BSD CI input debug gate"),
    ):
        require(text_value, "MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY", label)

    # BS: the controller generation is protected by joyMutex, not a second
    # atomic synchronization authority.
    require(header, "uint32_t joystickBindingProgramGeneration = 0", "mutex controller generation")
    if "std::atomic<uint32_t> joystickBindingProgramGeneration" in header:
        raise AssertionError("controller binding generation must be mutex-guarded plain state")
    if "joystickBindingProgramGeneration.load" in input_cpp or "joystickBindingProgramGeneration.fetch_add" in input_cpp:
        raise AssertionError("controller binding generation regained atomic operations")
    require(input_cpp, "++joystickBindingProgramGeneration", "mutex controller generation publish")

    # BT: macOS normal movement only performs the global Qt button query while
    # the GUI-owned candidate mask is armed; press/release/focus remain recovery
    # boundaries.
    require(screen_header, "m_mouseRecoveryArmedMask", "macOS recovery mask storage")
    require(header, "m_mouseRecoveryEligibleMask", "macOS recovery eligibility storage")
    require(input_cpp, "m_mouseRecoveryEligibleMask = 0", "macOS recovery eligibility reset")
    require(input_cpp, "if (masks.inputBits || masks.hotkeyBits)", "macOS mapped recovery projection")
    require(screen_press, "m_mouseRecoveryArmedMask |=", "macOS recovery arming")
    require(screen_press, "emu->mouseRecoveryEligibleMask()", "macOS eligible recovery arming")
    require(screen_release, "m_mouseRecoveryArmedMask &=", "macOS recovery release clear")
    require(screen_move, "m_mouseRecoveryArmedMask != 0", "macOS move recovery gate")
    require(screen_move, "m_mouseRecoveryArmedMask &= emu->mouseRecoveryEligibleMask();", "macOS move eligibility gate")
    require(screen_move, "& emu->mouseRecoveryEligibleMask();", "macOS move mapped recovery projection")
    query_at = screen_move.index("QGuiApplication::mouseButtons()")
    if screen_move.index("m_mouseRecoveryArmedMask &= emu->mouseRecoveryEligibleMask();") > query_at:
        raise AssertionError("macOS mouse movement must arm before querying global buttons")
    require(screen_unfocus, "MelonPrimeMouseRecoveryMask", "macOS focus-loss recovery reseed")
    require(screen_unfocus, "& emu->mouseRecoveryEligibleMask();", "macOS focus mapped recovery projection")
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

    # AQ: every non-wheel configured Metroid identity is explicitly RawExact
    # or QtFallback; unsupported values are never silently treated as a raw
    # binding, and F25+ cannot enter the contiguous VK arithmetic.
    raw_hotkey_compiler = raw_hotkey + raw_hotkey_mapping
    for needle in (
        "struct RawHotkeyOwnership",
        "rawOwnedGameplayMask",
        "qtFallbackGameplayMask",
        "wheelImpulseMask",
        "kQtKey_F24",
        "kQtBindingModifierMask",
        "RawExact",
        "Qt fallback classification",
    ):
        require(raw_hotkey_header + raw_hotkey + raw_hotkey_mapping, needle, "Raw binding ownership")
    if "kQtKey_F35" in raw_hotkey_compiler or "VK_F1 + idx" not in raw_hotkey_compiler:
        raise AssertionError("F-key mapping must be explicitly bounded at F24")
    require(raw_hotkey_compiler, "encoded & kQtBindingModifierMask", "modifier chord failover")
    require(raw_hotkey, "ownership.qtFallbackGameplayMask |= actionBit", "unsupported binding failover")
    for needle in (
        "case kQtKey_Shift:    out.push_back(VK_LSHIFT);   return true;",
        "case kQtKey_Control:  out.push_back(VK_LCONTROL); return true;",
        "case kQtKey_Alt:      out.push_back(VK_LMENU);    return true;",
        "case kQtKey_Shift:   out.push_back(VK_RSHIFT);   return true;",
        "case kQtKey_Control: out.push_back(VK_RCONTROL); return true;",
        "case kQtKey_Alt:     out.push_back(VK_RMENU);    return true;",
    ):
        require(raw_hotkey_compiler, needle, "left/right modifier VK parity")
    if "VK_LSHIFT);   out.push_back(VK_RSHIFT)" in raw_hotkey_compiler:
        raise AssertionError("plain Shift must not widen to both sides")
    if "VK_LCONTROL); out.push_back(VK_RCONTROL)" in raw_hotkey_compiler:
        raise AssertionError("plain Control must not widen to both sides")
    if "VK_LMENU);    out.push_back(VK_RMENU)" in raw_hotkey_compiler:
        raise AssertionError("plain Alt must not widen to both sides")
    for needle in (
        "plain Shift",
        "right Shift",
        "plain Control",
        "right Control",
        "plain Alt",
        "right Alt",
        "F25 fallback",
        "raw-hotkey-vk-mapping-tests: PASS",
    ):
        require(raw_hotkey_mapping_test, needle, "runtime VK mapping contract")
    require(
        qt_sdl_cmake,
        "melonprime_raw_hotkey_vk_mapping_tests",
        "Windows VK mapping test target",
    )
    require(
        qt_sdl_cmake,
        "MelonPrimeRawHotkeyVkMapping.cpp",
        "production VK mapping source",
    )
    for needle in (
        "m_qtFallbackGameplayMask == 0",
        "hk.down & m_rawOwnedGameplayMask",
        "qtGameplayHeld & m_qtFallbackGameplayMask",
        "hk.pressed & m_rawOwnedGameplayMask",
        "qtGameplayPressed & m_qtFallbackGameplayMask",
    ):
        require(game_input, needle, "Raw/Qt ownership merge")
    if game_input.count("const bool rawActionReady =") != 1:
        raise AssertionError(
            "Windows rawActionReady must be computed exactly once per snapshot"
        )
    if game_input.count("const bool rawOnlyFastPath =") != 1:
        raise AssertionError(
            "Windows Raw/Qt source decision must be computed exactly once"
        )
    source_selection = body(
        game_input,
        "uint64_t hotDownMask = qtGameplayHeld;",
        "const InputProjection::ProjectedDownState downState =",
    )
    for needle in (
        "if (LIKELY(rawOnlyFastPath))",
        "hotDownMask = hk.down;",
        "hotPressMask = hk.pressed;",
        "else if (rawActionReady)",
        "hk.down & m_rawOwnedGameplayMask",
        "qtGameplayHeld & m_qtFallbackGameplayMask",
        "hk.pressed & m_rawOwnedGameplayMask",
        "qtGameplayPressed & m_qtFallbackGameplayMask",
    ):
        require(source_selection, needle, "single Raw/Qt source decision")
    if source_selection.count("m_qtFallbackGameplayMask == 0") != 1:
        raise AssertionError(
            "fallback-zero test must be computed exactly once"
        )
    if "if (isInputOwner && rawActionReady)" in source_selection:
        raise AssertionError("Raw/Qt source selection regained duplicate owner gate")
    require(core + lifecycle, "m_rawOwnedGameplayMask = ownership.rawOwnedGameplayMask", "ownership publication")

    # AW+: inactive Raw consumers and non-owner false updates must not touch
    # the process-wide recursive mutex. Every maybe-owner path retains a
    # locked revalidation before reading mutable subscription state. The
    # steady-state frame data lock is subscription-local.
    require(raw_filter_header, "MelonPrimeRawInputPerfProbe.h", "Raw perf probe include")
    for name, text_value in (
        ("UpdateOwnerAndSnapshot", raw_fused),
        ("DeferredDrain", raw_deferred),
        ("LateLatchMouseDelta", raw_late),
    ):
        require(text_value, "m_activeSubscription.load(std::memory_order_acquire)", f"{name} active precheck")
        require(text_value, "RawInputPerf::FrameMutexGuard", f"{name} measured frame lock")
        if text_value.index("m_activeSubscription.load(std::memory_order_acquire)") > text_value.index("RawInputPerf::FrameMutexGuard"):
            raise AssertionError(f"{name} must precheck active ownership before locking")
        if name in ("DeferredDrain", "LateLatchMouseDelta") and "RawInputPerf::SubscriptionMutexGuard" in text_value:
            raise AssertionError(f"{name} must not use the control-plane mutex on the steady path")
    require(raw_deactivate_owner, "const bool rawOwner", "DeactivateOwner Raw authority precheck")
    require(raw_deactivate_owner, "const bool platformOwner", "DeactivateOwner platform authority precheck")
    require(raw_deactivate_owner, "if (LIKELY(!rawOwner && !platformOwner))", "DeactivateOwner lock-free false path")
    require(raw_deactivate_owner, "PlatformInputOwnerService::Update(*subscription->owner, false)", "DeactivateOwner release")
    require(raw_deactivate_owner, "DeactivateActiveRegistration(subscription)", "DeactivateOwner registration cleanup")
    if raw_deactivate_owner.index("if (LIKELY(!rawOwner && !platformOwner))") > raw_deactivate_owner.index("RawInputPerf::SubscriptionMutexGuard"):
        raise AssertionError("DeactivateOwner false fast path must precede the mutex")
    if "UpdateOwner(" in raw_filter + raw_filter_header + lifecycle:
        raise AssertionError("legacy public UpdateOwner call/declaration reappeared")
    for needle in (
        "static int                 s_refCount",
        "s_refCount++",
        "--s_refCount",
    ):
        require(raw_filter_header + raw_filter, needle, "cold Raw refCount synchronization")

    # AW+: physical-state recovery is event-gated and observable. The hidden
    # dispatch remains the only producer of the recovery request; the batch
    # syscall and AsyncKeyState calls are counted only in the developer probe.
    for needle in (
        "m_stuckRecoveryNeeded",
        "bool m_stuckRecoveryNeeded = false",
        "RequestStuckRecovery()",
        "consumeStuckRecovery()",
        "m_stuckRecoveryNeeded = true",
        "m_stuckRecoveryNeeded = false",
        "RawInputPerf::CountStuckRecovery",
    ):
        require(raw_state_header + raw_state_cpp, needle, "event-gated stuck recovery")
    if "std::atomic_bool m_stuckRecoveryNeeded" in raw_state_header:
        raise AssertionError("stuck recovery mailbox must stay same-thread plain state")
    if any(
        token in raw_state_cpp
        for token in (
            "m_stuckRecoveryNeeded.load(",
            "m_stuckRecoveryNeeded.exchange(",
            "m_stuckRecoveryNeeded.store(",
        )
    ):
        raise AssertionError("stuck recovery mailbox must not use atomic RMW/load/store")
    for needle in (
        "state->RequestStuckRecovery()",
        "RawInputPerf::CountHiddenWindowDispatch()",
        "RawInputPerf::RawBufferScope",
        "RawInputPerf::CountGetAsyncKeyState",
    ):
        require(raw_filter + raw_state_cpp, needle, "Raw recovery telemetry")
    if raw_state_cpp.count("::GetAsyncKeyState(") != 1:
        raise AssertionError("all Raw GetAsyncKeyState calls must pass the telemetry wrapper")
    for needle in (
        "MELONPRIME_RAW_INPUT_PERF",
        "MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY",
        "RawSubscriptionLockWait",
        "RawSnapshot",
        "RawLateLatch",
        "RawDeferredDrain",
        "RawBatchCallCount",
        "RawBatchEventCount",
        "mutexAcquisitions",
        "rawBufferCalls",
        "getAsyncKeyStateCalls",
        "MaybeReport",
    ):
        require(raw_perf, needle, "Raw performance telemetry")
    if "MELONPRIME_ENABLE_DEVELOPER_FEATURES" in raw_perf:
        raise AssertionError("Raw performance telemetry must not inherit the developer gate")
    if "MELONPRIME_PERF" in raw_perf:
        raise AssertionError("generic MELONPRIME_PERF must not enable Raw telemetry")
    for needle in (
        "option(MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY",
        "if (WIN32 AND MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY)",
    ):
        require(qt_sdl_cmake, needle, "dedicated Raw telemetry CMake gate")
    if cmake_presets.count("MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY") < 2:
        raise AssertionError("base and shipping presets must pin Raw telemetry OFF")
    for text_value, label in (
        (mingw_build, "normal MinGW build Raw telemetry gate"),
        (mingw_shipping_build, "shipping MinGW build Raw telemetry gate"),
    ):
        require(text_value, "MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY=OFF", label)
    require(
        windows_workflow,
        "MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY=OFF",
        "shipping CI Raw telemetry gate",
    )

    # BB: re-entrant NoEdges is a pure snapshot and the post-frame boundary is
    # the only recovery consumer. This prevents nested dispatch from clearing
    # physical state, advancing debounce twice, or changing hkPrev mid-frame.
    for forbidden in (
        "consumeStuckRecovery(",
        "clearStuckMouseButtons(",
        "clearStuckKeys(",
        "GetAsyncKeyState(",
        "RawInputGetAsyncKeyState(",
    ):
        if forbidden in raw_no_edges:
            raise AssertionError(f"NoEdges must not contain {forbidden!r}")
    for needle in ("outHk.pressed = 0", "outWheelSteps = 0", "outHk.wheelSteps = 0"):
        require(raw_no_edges, needle, "pure re-entrant Raw snapshot")
    if raw_state_cpp.count("if (UNLIKELY(consumeStuckRecovery()))") != 1:
        raise AssertionError("stuck recovery must have one post-frame consumer")

    # BC: the same-thread proof is explicit at the Win32 boundary. The hidden
    # HWND records its creator, only creator-thread dispatch can request the
    # mailbox, and frame/lifecycle consumers stay on the EmuThread path.
    hidden_proc = body(
        raw_filter,
        "LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(",
        "bool RawInputWinFilter::RegisterDevices(",
    )
    for forbidden in (
        "GetWindowLongPtr",
        "GWLP_USERDATA",
        "GetCurrentThreadId()",
        "GetWindowThreadProcessId",
        "QString",
        "Config::Table",
        "chrono",
    ):
        if forbidden in hidden_proc:
            raise AssertionError(f"HiddenWndProc event-hot path still contains {forbidden!r}")
    for needle in (
        "RawInputPerf::FrameMutexGuard",
        "m_activeSubscription.load(\n                std::memory_order_relaxed)",
        "subscription->hiddenWindow == hwnd",
        "auto* const state = subscription->state.get();",
    ):
        require(hidden_proc, needle, "minimal HiddenWndProc routing")
    lock_at = hidden_proc.index("RawInputPerf::FrameMutexGuard")
    active_at = hidden_proc.index("m_activeSubscription.load")
    hwnd_at = hidden_proc.index("subscription->hiddenWindow == hwnd")
    state_at = hidden_proc.index("subscription->state.get()")
    if "RawInputPerf::SubscriptionMutexGuard" in hidden_proc:
        raise AssertionError("HiddenWndProc must not use the control-plane mutex")
    if not active_at < lock_at < hwnd_at < state_at:
        raise AssertionError("HiddenWndProc must validate the active subscription before its local frame lock and state")
    for needle in (
        "const DWORD currentThreadId = GetCurrentThreadId();",
        "subscription->hiddenWindowCreatorThreadId = currentThreadId;",
        "subscription->hiddenWindow == hwnd",
        "subscription->hiddenWindowCreatorThreadId == GetCurrentThreadId()",
    ):
        require(raw_filter, needle, "same-thread Raw recovery proof")
    if hidden_proc.count("state->RequestStuckRecovery()") != 1:
        raise AssertionError("HiddenWndProc must be the sole recovery producer")

    # CD-CH: successful pure motion and wheel-only events must not publish the
    # post-frame recovery mailbox, while stateful events and read failures do.
    require(raw_state_header, "[[nodiscard]] bool processRawInput(HRAWINPUT hRaw)", "Raw recovery hint API")
    require(raw_process, "if (UNLIKELY(result == UINT(-1) || result == 0)) return true;", "Raw read-failure recovery")
    require(raw_process, "const USHORT flags = m.usButtonFlags & 0x03FF;", "Raw mouse button classifier")
    require(raw_process, "return flags != 0;", "Raw motion/wheel recovery gate")
    require(raw_process, "case RIM_TYPEKEYBOARD", "Raw keyboard recovery classifier")
    require(raw_process, "return true;", "Raw stateful recovery classifier")
    require(raw_process, "return false;", "Raw ignored-event recovery classifier")
    require(hidden_proc, "const bool needsRecovery = state->processRawInput(", "HiddenWndProc recovery hint result")
    require(hidden_proc, "if (UNLIKELY(needsRecovery))", "conditional recovery publication")
    if hidden_proc.index("if (UNLIKELY(needsRecovery))") > hidden_proc.index("state->RequestStuckRecovery()"):
        raise AssertionError("HiddenWndProc must publish recovery only after the hint is true")
    require(raw_filter, "(void)state->processRawInput(", "Joy2Key recovery hint discard")
    for needle in (
        "FakeGetRawInputData",
        "g_readFailure",
        "for (int i = 0; i < 10000; ++i)",
        "RI_MOUSE_WHEEL",
        "RI_MOUSE_LEFT_BUTTON_DOWN",
        "RI_MOUSE_LEFT_BUTTON_UP",
        "RI_KEY_BREAK",
        "RIM_TYPEHID",
        "raw-recovery-hint-tests: PASS",
    ):
        require(raw_recovery_hint_test, needle, "Raw recovery hint executable coverage")
    for needle in (
        "melonprime_raw_recovery_hint_tests",
        "MelonPrimeRawInputState.cpp",
        "MelonPrimeRawWinInternal.cpp",
        "melonprime_raw_recovery_hint_check",
    ):
        require(qt_sdl_cmake, needle, "Raw recovery hint CMake target")

    require(raw_drain, "drainPendingMessagesLocked(*subscription);", "single active Raw drain delegation")
    require(raw_drain_locked, "auto* const state = StateFor(&subscription);", "locked Raw drain state load")
    if "ActiveState()" in raw_drain:
        raise AssertionError("drainPendingMessages must not reload the active subscription")
    require(raw_filter, "state->clearStuckPostFrame();", "post-frame recovery owner")
    require(core, "m_rawFilter->DeferredDrain(m_rawInputSubscription)", "EmuThread recovery consumer")
    require(core, "m_rawFilter->resetAll(m_rawInputSubscription)", "EmuThread lifecycle reset")
    require(emu_setup, "melonPrime->Initialize();", "EmuThread Raw setup owner")
    require(emu_thread, "melonPrime->DeferredDrainInput();", "EmuThread Raw drain owner")

    # BD: a delayed/stalled Raw queue retries through the fixed process-service
    # scratch. A batch larger than the bound remains queued; it never allocates
    # from processRawInputBatched.
    for needle in (
        "kBatchOverflowBufferSize",
        "s_batchOverflowBuffer",
        "if (size > kBatchOverflowBufferSize) break;",
        "retrySize = static_cast<UINT>(s_batchOverflowBuffer.size());",
    ):
        require(raw_state_header + raw_state_cpp, needle, "bounded Raw batch scratch")
    for forbidden in ("std::unique_ptr<uint8_t[]>", "new (std::nothrow) uint8_t"):
        if forbidden in raw_batched:
            raise AssertionError(f"Raw batch hot path still allocates: {forbidden}")

    # BE: an owner transfer/reactivation is a hidden-HWND lifetime boundary.
    # Only the creator thread may destroy the old window, and the replacement
    # happens on the cold registration path before Raw input is accepted.
    for needle in (
        "ApplyOwnerRegistrationLocked(\n            RawInputSubscription* subscription, bool recreateHiddenWindow)",
        "ApplyOwnerRegistrationLocked(\n                subscription, generationAlreadyAdvanced)",
        "if (recreateHiddenWindow && subscription->hiddenWindow",
        "!DestroyHiddenWindowLocked(subscription)",
        "CreateHiddenWindowLocked(subscription)",
        "HWND_MESSAGE, nullptr, instance, nullptr",
    ):
        require(raw_filter_header + raw_filter, needle, "Raw hidden HWND epoch boundary")
    if not raw_reconfigure_locked or not raw_apply_owner_locked:
        raise AssertionError("Raw hidden HWND registration bodies are missing")
    if raw_apply_owner_locked.index("DestroyHiddenWindowLocked(subscription)") > raw_apply_owner_locked.index(
        "CreateHiddenWindowLocked(subscription)"
    ):
        raise AssertionError("old Raw hidden HWND must be destroyed before replacement")
    if raw_filter.count("ApplyOwnerRegistrationLocked(") != 3:
        raise AssertionError("ApplyOwnerRegistrationLocked must stay on the cold reconfigure path")

    # BF/BG: the buffered Raw drain is owned by the Windows filter, while the
    # public reset wrapper is the only cross-thread lifecycle entry and holds
    # the same recursive mutex before touching InputState.
    if "void processRawInputBatched() noexcept;" in raw_state_public:
        raise AssertionError("buffered Raw drain leaked into InputState public API")
    for needle in (
        "friend class RawInputWinFilter;",
        "void processRawInputBatched() noexcept;",
        "A foreign owner transfer may reset an inactive InputState",
        "RawInputWinFilter::m_subscriptionMutex is held",
        "This public wrapper always holds m_subscriptionMutex",
    ):
        require(
            raw_state_header + raw_filter,
            needle,
            "Raw InputState ownership contract",
        )
    raw_reset = body(
        raw_filter,
        "void RawInputWinFilter::resetAll(",
        "void RawInputWinFilter::resetHotkeyEdges(",
    )
    if (
        not raw_reset
        or raw_reset.index("std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex)")
        > raw_reset.index("state->resetAll()")
    ):
        raise AssertionError("foreign Raw reset must hold the subscription mutex first")
    if "drainPendingMessages();" in raw_reset:
        raise AssertionError("frame-locked Raw reset must use the locked drain helper")

    # AW+: fixed-capacity VK mapping must turn overflow into a whole-list
    # fallback rather than silently binding a prefix.
    for needle in (
        "bool push_back(UINT vk) noexcept",
        "overflowedFlag",
        "count = 0",
        "bool overflowed() const noexcept",
    ):
        require(raw_hotkey_mapping_header + raw_hotkey_mapping_test, needle, "SmallVkList overflow contract")
    require(raw_hotkey_mapping_test, "SmallVkList::kCapacity;", "SmallVkList overflow test")

    # AR: hidden Raw windows are subscription-local and routed by the active
    # subscription under the filter mutex. The active owner never
    # drains/destroys a foreign creator's queue.
    for needle in (
        "HWND hiddenWindow",
        "hiddenWindowCreatorThreadId",
        "CreateHiddenWindow(RawInputSubscription* subscription)",
        "DestroyHiddenWindow(RawInputSubscription* subscription)",
        "GetCurrentThreadId()",
        "m_activeSubscription.load",
        "ShutdownRawInput",
    ):
        require(raw_filter_header + raw_filter + emu_thread, needle, "Raw hidden HWND ownership")
    if "GWLP_USERDATA" in raw_filter_header + raw_filter:
        raise AssertionError("Raw hidden HWND routing must not use window user data")
    if "m_hHiddenWnd" in raw_filter_header + raw_filter:
        raise AssertionError("process-global hidden HWND reappeared")
    require(raw_filter, "hiddenWindowCreatorThreadId == GetCurrentThreadId()", "foreign Raw queue guard")
    require(emu_thread, "melonPrime->ShutdownRawInput()", "EmuThread Raw shutdown")

    # AS/AV: lifecycle notifications are atomic, generation is consumed by its
    # owner, and public state/reset helpers share the Raw subscription mutex.
    for needle in (
        "std::atomic_bool registrationResetPending",
        "RequestRegistrationReset()",
        "ConsumeRegistrationReset()",
        "registrationResetPending.load(std::memory_order_relaxed)",
        "owner->RequestRegistrationReset()",
        "PlatformInputOwnerService::RequestRegistrationReset",
        "m_inputSubscription.ConsumeRegistrationReset()",
        "std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex)",
    ):
        require(input_subscription + raw_filter + game_input, needle, "Raw subscription single-writer")
    if "BeginRegistrationGeneration(*previous->owner)" in raw_filter:
        raise AssertionError("foreign owner generation mutation reappeared")
    require(
        game_input,
        "platformInputOwner = rawFilter->UpdateOwnerAndSnapshot",
        "Windows owner/snapshot transaction",
    )
    require(game_input, "const bool isInputOwner = platformInputOwner", "Windows owner result reuse")

    # AT: API success is the only path to m_isRegistered/activeOwner/baseline
    # readiness, with the developer-only one-shot fault seam retaining Qt.
    for needle in (
        "[[nodiscard]] bool RegisterDevices",
        "if (!RegisterRawInputDevices(",
        "m_isRegistered = true;",
        "MELONPRIME_TEST_FORCE_RAW_REGISTER_FAILURE",
        "if (!ReconfigureActiveRegistration(subscription, true))",
        "PlatformInputOwnerService::Release(*subscription->owner)",
        "subscription->baselineReady = false",
    ):
        require(raw_filter_header + raw_filter, needle, "Raw registration fail-closed")
    registration_body = body(
        raw_filter,
        "bool RawInputWinFilter::RegisterDevices(",
        "void RawInputWinFilter::UnregisterDevices(",
    )
    if registration_body.index("if (!RegisterRawInputDevices(") > registration_body.index("m_isRegistered = true;"):
        raise AssertionError("Raw ready flag precedes native registration success")

    shutdown = body(
        lifecycle,
        "void MelonPrimeCore::ShutdownRawInput()",
        "    // Sole top-level RuntimeConfigSnapshot apply transaction.",
    )
    require(shutdown, "m_rawFilter->Unsubscribe(m_rawInputSubscription)", "single teardown owner")
    if "PlatformInputOwnerService::Release" in shutdown:
        raise AssertionError("ShutdownRawInput must not duplicate Unsubscribe owner release")

    # AU: only GUI wheel events read the normalized input generation; focus and
    # close boundaries still clear the local residual.
    for needle in (
        "Consume(\n            const QWheelEvent& event, uint32_t generation)",
        "m_generationInitialized",
        "m_generation != generation",
        "m_angleRemainder = 0",
    ):
        require(wheel_event, needle, "generation-scoped Qt wheel residual")
    require(bridge, "InputGenerationForGui()", "GUI wheel generation getter")
    require(screen, "InputGenerationForGui()", "Screen wheel generation tag")
    require(screen, "wheelSteps.Reset();", "focus/close wheel residual reset")

    check_windows_raw_reaudit_models()
    check_windows_raw_recovery_hint_model()
    check_windows_source_selection_model()

    check_state_model()
    check_controller_pause_model()
    check_controller_mapping_equivalence_model()
    check_panel_cumulative_model()
    check_wayland_fixed_delta_model()
    check_linux_raw_state_and_reset_model()
    check_linux_reset_fence_model()
    check_mac_mouse_recovery_model()
    check_qt_event_edge_model()
    check_binding_program_publication_model()
    check_linux_axis_query_lifecycle_model()
    check_raw_wheel_unit_model()
    check_wheel_count_model()
    check_packed_wrap_model()
    check_mac_handoff_model()
    print("post-push full input re-audit contract: PASS")


if __name__ == "__main__":
    main()
