# MelonPrime multi-instance ROM-identity harness

This is the manual A/B harness for verifying that ROM detection inputs remain
owned by each `EmuInstance`. Use a developer build so instance/thread
diagnostics are present. Set `MELONPRIME_STRICT_THREAD_ASSERTS=1` only when a
hard assertion is desired; the default records violations without stopping the
emulator.

## Fixture

- Instance A: MPH US 1.0, one HUD/font/scale and hook option set.
- Instance B: MPH EU 1.1 or an unsupported ROM, with a different HUD/font/scale
  and hook option set.
- Keep both instances in the same melonPrimeDS process.

## Acceptance sequence

1. Load the US ROM into A and record A's identity generation, checksum,
   `gameCode`, `romVersion`, classification result, and resolved addresses.
2. Load the EU ROM (or an unsupported ROM) into B and record the same values.
3. While B remains loaded, eject A and reload the US ROM into A. Verify that A's
   generation advances and A's fields/flags/addresses are rebuilt, while B's
   generation and detection state do not change.
4. Eject B and reload its EU/unsupported ROM. Verify the symmetric property:
   B changes only its own identity and A remains unchanged.
5. Enter a match in A, then B; reset only B; stop A and continue B. Confirm
   that each instance's hook/patch/HUD lifecycle follows its own ROM identity.
6. Move focus A to B and back while changing weapons and zoom state. Repeat
   with different window sizes and with A's HUD editor open.

The production POD/publish/clear model is covered by
`melonprime_rom_identity_tests` in `src/frontend/qt_sdl/CMakeLists.txt`; the
two-window runtime sequence still requires a manual or multi-window harness.

## Evidence to retain

- Console lines prefixed with `[MelonPrime][instance=N]`, including the identity
  generation and ROM classification result at each load/eject edge.
- ARM9 hook address/mask dumps from a developer build with
  `MELONPRIME_ARM9_HOOK_DEBUG_LOG` enabled.
- Patch apply/restore OSD messages for each instance.
- Screenshots after each resize/editor/zoom transition.
- Thread-check warnings, especially the Phase 0 sensitivity-hotkey
  `Config::Save` warning that Phase 5 must remove.

## Historical failure shape

- A process-global checksum/game-code/revision/generation could be overwritten
  by B while A was still running, causing A's next detection decision to use
  B's ROM identity.
- The fixed path must not regress into process-global ROM identity or a
  cross-instance shared detection latch.

Run `tools/ci/audits/audit-melonprime-instance-state.ps1 -List` alongside this
harness to retain the static-state inventory for the same revision.
