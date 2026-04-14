# MelonPrime Low-Risk Performance Opportunities

This note tracks low-risk optimization candidates that are likely safe to apply incrementally.
It is based on current hot-path code in:
- `MelonPrime.cpp`
- `MelonPrimeInGame.cpp`
- `MelonPrimeGameInput.cpp`
- `MelonPrimeGameWeapon.cpp`
- `MelonPrimeRawInputState.*`

## Priority A (very low risk, immediate candidates)

## A1. Cache `NDS*` in re-entrant `RunFrameHook` path
- Location: `src/frontend/qt_sdl/MelonPrime.cpp:315-329`
- Current: re-entrant stylus branch calls `emuInstance->getNDS()` directly on touch path.
- Change: take `auto* const nds = emuInstance->getNDS();` once in the re-entrant block and reuse it.
- Why low risk: pure pointer-chase reduction, no behavior change.

## A2. Cache `NDS*` in cursor-mode touch block
- Location: `src/frontend/qt_sdl/MelonPrime.cpp:386-390`
- Current: two `emuInstance->getNDS()` calls in `if (isCursorMode)` block.
- Change: cache pointer once before touch/release branch.
- Why low risk: same API calls, fewer repeated lookups.

## A3. Cache `NDS*` in `ProcessAimInputStylus()`
- Location: `src/frontend/qt_sdl/MelonPrimeGameInput.cpp:217-224`
- Current: calls `emuInstance->getNDS()` in both branch arms.
- Change: `auto* const nds = emuInstance->getNDS();` then branch on touching.
- Why low risk: identical call sequence.

## A4. Reuse hot bits in `HandleInGameLogic()`
- Location: `src/frontend/qt_sdl/MelonPrimeInGame.cpp:29-99`
- Current: repeated helper calls (`IsPressed/IsDown/IsAnyPressed`) re-read `m_input.down/press`.
- Change: cache `const uint64_t press = m_input.press; const uint64_t down = m_input.down;` and use bit-tests in this function.
- Why low risk: read-only snapshot from same frame, removes repeated loads/method calls.

## Priority B (still low risk, needs careful review)

## B1. Reduce repeated `m_flags.test(...)` in `RunFrameHook`
- Location: `src/frontend/qt_sdl/MelonPrime.cpp:348-412`
- Current: repeated tests of `BIT_ROM_DETECTED`, `BIT_IN_GAME_INIT`, `BIT_IN_ADVENTURE`, `BIT_PAUSED`.
- Change: cache selected booleans when they are not mutated between uses.
- Risk note: some bits are mutated in-body, so cached values must only be used in safe spans.

## B2. Fold stylus block to avoid one extra nested branch
- Location: `src/frontend/qt_sdl/MelonPrimeInGame.cpp:86-90`
- Current: nested `if (isStylusMode) { if (!blockStylus) ... }`.
- Change: single condition `if (isStylusMode && !blockStylus)`.
- Why low risk: same semantics, slightly cleaner branch layout.

## B3. Early `isCursorMode` guard in main hook touch path
- Location: `src/frontend/qt_sdl/MelonPrime.cpp:386-392`
- Current: always evaluates branch and then updates START mask.
- Change: keep START mask logic as-is, but isolate cursor touch handling behind tightly scoped fast branch and cached `nds`.
- Why low risk: structure-only cleanup; behavior unchanged.

## Priority C (small gain, safe but likely micro)

## C1. Keep `ProcessWeaponSwitch()` hot path comment in sync
- Location: `src/frontend/qt_sdl/MelonPrimeGameWeapon.cpp:163-166`
- Current: comment still mentions fold-expression hotkey mask; implementation now uses direct bit extraction.
- Change: update comment only.
- Why useful: avoids future regressions when refactoring by keeping assumptions explicit.

## C2. Add explicit contiguous-bit assert for `ProjectPressMask` layout coupling
- Location: `src/frontend/qt_sdl/MelonPrimeGameInput.cpp` and `MelonPrime.h` enum layout
- Current: relies on contiguous mapping assumptions plus static_asserts on hotkey IDs.
- Change: add narrow static_asserts near internal projection helpers for IB layout assumptions.
- Why low risk: compile-time safety net only.

## Out of scope / avoid for now

- `DeferredDrain` simplification that removes `processRawInputBatched` safety net:
  high regression risk due to known shared-buffer semantics in RawInput path.
- Any change to memory-ordering in `InputState` atomics:
  correctness-sensitive and easy to break under weak ordering.

## Suggested execution order

1. A1 + A2 + A3 (pointer-chase cleanup)
2. A4 (local bit snapshot in `HandleInGameLogic`)
3. B1/B2/B3 (branch-shape cleanup with careful review)
4. C1/C2 (maintainability and compile-time guards)
