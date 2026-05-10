# MelonPrime Click Handling — Investigation & Fix Log

**Status:** Fix applied 2026-05-10. Targets two distinct dropped-click symptoms.
**Branch:** `highres_fonts_v3`
**File:** [src/frontend/qt_sdl/MelonPrimeRawInputState.cpp](../../src/frontend/qt_sdl/MelonPrimeRawInputState.cpp)

## Symptoms reported

- **1回タップが時々無視される**：単発クリックがゲームに伝わらない瞬間がある。
- **高速連打の途中で1発抜ける**：連射していると、何発目かが反応しない。
- 発生タイミングは特定できず、ランダム感がある。

## Pipeline recap

Mouse input → `processRawInput` / `processRawInputBatched` →
- `m_mouseButtons` (uint8_t bitmask, current physical state)
- `m_mouseButtonPresses` (uint8_t bitmask, DOWN edges seen since last snapshot)
- `m_mouseButtonDeferredPresses` (uint8_t bitmask, taps deferred from prior frame)

→ `snapshotInputFrame` consumes these and produces `outHk.down` / `outHk.pressed`.

The defer logic was added because a "very short click" (DOWN+UP within one frame) leaves
`m_mouseButtons=0` at snapshot time but `m_mouseButtonPresses=bit`. To make rapid taps fire
distinct press edges instead of merging, the original logic forced `snap.mouse |= bit` and
then deferred bits whose hotkey was already in `m_hkPrev` so a release frame would form.

## Root causes (pre-fix)

### Bug A — Tap→Hold loses the press edge

Sequence:
1. Frame N: tap (DOWN+UP within frame). `pendingMousePresses=bit`, `physicalMouse=0`.
   `tapBits=bit`, `m_hkPrev=0` → **not** deferred. Fires. `m_hkPrev=hotkey`.
2. Frame N+1: user does a quick release+repress that ends with the button **physically held**.
   `m_mouseButtons=bit`, `m_mouseButtonPresses=bit`, `physicalMouse=bit`.
   `tapBits = bit & ~bit = 0` → defer logic does **nothing**. `activeMousePresses=bit`,
   `snap.mouse=bit`, `newDown=hotkey`, but `pressed = newDown & ~m_hkPrev = 0`. **Press edge lost.**

Old code only deferred `pendingMousePresses & ~physicalMouse` (i.e. bits whose physical button
is currently up). It had no recovery path for "new DOWN occurred but button is currently held".

### Bug B — Stacked taps collapse into one bit

The old code did:

```cpp
const uint8_t pendingMousePresses = static_cast<uint8_t>(
    m_mouseButtonPresses.exchange(0, std::memory_order_acquire) |
    m_mouseButtonDeferredPresses.exchange(0, std::memory_order_acquire));
```

When the previous frame deferred a tap (so `m_mouseButtonDeferredPresses = bit`) **and** the
current frame also has a fresh tap of the same button (`m_mouseButtonPresses = bit`), the OR
collapses two logical presses into a single bit. The bitmask representation has no way to
preserve "two queued presses for the same button". With 3+ rapid taps, this caused the 3rd
tap to merge into the 2nd's deferred resolution and silently drop.

## Fix

Rewrote the defer block in [MelonPrimeRawInputState.cpp:511](../../src/frontend/qt_sdl/MelonPrimeRawInputState.cpp#L511).

### Key changes

1. **Read `m_mouseButtonDeferredPresses` and `m_mouseButtonPresses` separately** (no OR).
   This lets us detect bits present in both = "stacked" presses.

2. **Stacked bits**: deferred portion fires this frame, new portion is requeued for next frame.
   This forces 3+ rapid taps to spread across one frame each instead of collapsing.

3. **`forcePressEdge` mask**: new mechanism that detects "a press bit being activated this
   frame would normally produce a hotkey, but `m_hkPrev` already has it (so the natural
   `newDown & ~m_hkPrev` would mask it out)." For those hotkeys, we mask them out of
   `m_hkPrev` for this frame's edge calculation only:

   ```cpp
   outHk.pressed = newDown & ~(m_hkPrev & ~forcePressEdge);
   ```

   `forcePressEdge` is computed by diffing `scanBoundHotkeys` with vs. without the active
   press bits. This single diff covers both sub-cases:

   - **Tap→Hold**: bit is in `physicalMouse` and was newly DOWN this frame → its hotkey is
     "newly contributed" by the press → if it overlaps `m_hkPrev`, force the edge.
   - **Deferred-resolves-while-hkPrev-still-set**: after the stacked-tap chain, the deferred
     bit fires while `m_hkPrev` still carries the hotkey. Same recovery applies.

### Behavior matrix (post-fix)

| Scenario | Old behavior | New behavior |
|---|---|---|
| Single tap, hkPrev=0 | Fires | Fires |
| Tap → tap (rapid, 2 frames) | Both fire (tap2 delayed 1f) | Both fire (tap2 delayed 1f) |
| Tap → tap → tap (rapid, 3 frames) | **2 fires** (tap3 merged) | **3 fires** (each delayed 1f after the first) |
| Tap → hold (release+repress, ends held) | **Press edge lost** | Press edge fires via `forcePressEdge` |
| Pure hold (no new DOWN) | No press edge | No press edge (no false-fire) |
| Hold + tap on different button | Tap fires | Tap fires |

### Trade-off

Rapid taps after the first now have a consistent 1-frame (~16 ms @ 60fps) delay before
firing. This was already true in the old code's defer path; the new code just applies it
to more cases. For continuous-fire weapons the down-state is held in N, N+2, N+3, … so
firing remains continuous.

## Files touched

- [src/frontend/qt_sdl/MelonPrimeRawInputState.cpp](../../src/frontend/qt_sdl/MelonPrimeRawInputState.cpp) — `snapshotInputFrame` rewrite (~70 lines).

`m_mouseButtonPresses` / `m_mouseButtonDeferredPresses` data members and producer-side
`processRawInput` / `processRawInputBatched` were **not** modified — same bitmask layout,
same release-store semantics. The change is consumer-side only.

## Items intentionally NOT changed

- **`clearStuckMouseButtons` / `clearStuckKeys`** ([L429](../../src/frontend/qt_sdl/MelonPrimeRawInputState.cpp#L429),
  [L465](../../src/frontend/qt_sdl/MelonPrimeRawInputState.cpp#L465)). These call
  `GetAsyncKeyState` to clear logical state when the physical button is released. There is
  a theoretical false-positive risk under unusual OS conditions (workstation lock, mouse
  button swap settings, remote desktop) that could clear a still-held button. No user
  reproduction tied to this path; left untouched. If a future report shows symptoms during
  Alt+Tab / RDP / SwapMouseButton, revisit.
- **`drainMessagesOnly`** ([MelonPrimeRawInputWinFilter.cpp:75](../../src/frontend/qt_sdl/MelonPrimeRawInputWinFilter.cpp#L75)).
  Removes `WM_INPUT` from queue without dispatch. Comment notes P-35 was reverted because
  removing the prior `GetRawInputBuffer` caused stuck keys (FIX-1 shared-buffer semantics).
  Current placement (after `processRawInputBatched` inside `drainPendingMessages`) is the
  belt-and-suspenders that the comments say is required.
- **Counter-based press tracking** (replacing `uint8_t` bitmask with per-button counters).
  Considered but rejected: the stacked-bit approach achieves the same "spread N presses
  over N frames" outcome with no atomic-layout change and no producer-side modifications.

## Cross-references

- Defer logic original intent: see comments in `snapshotInputFrame` and the historical
  commits `12c62857`, `7bf0d0fc`, `59dc7b08`, `fdc345ae`.
- Click-related repo history: [melonprime-aim-input.md](melonprime-aim-input.md) §7,
  [melonprime-refactoring.md](melonprime-refactoring.md) §4.5 (R2 button-priority fix),
  §5.2 (OPT-S / fence aggregation), §15.5 (P-35 revert).
- Single-writer atomic discipline: [melonprime-refactoring.md](melonprime-refactoring.md)
  §3.2.
