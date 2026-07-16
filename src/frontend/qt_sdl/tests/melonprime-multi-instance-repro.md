# MelonPrime multi-instance reproduction harness

This is the Phase 0 manual harness for state-ownership work. Use a developer
build so instance/thread diagnostics are present. Set
`MELONPRIME_STRICT_THREAD_ASSERTS=1` only when a hard assertion is desired;
the default records violations without stopping the emulator.

## Fixture

- Instance A: MPH US 1.0, one HUD/font/scale and hook option set.
- Instance B: MPH JP 1.1, a different HUD/font/scale and hook option set.
- Keep both instances in the same melonPrimeDS process.

## Sequence

1. Start A, then B.
2. Enter a match in A, then B.
3. Reload only A's configuration.
4. Reset only B.
5. Stop A and continue B.
6. Restart A, then stop only B.
7. Move focus A to B and back while changing weapons and zoom state.
8. Repeat with different window sizes and with A's HUD editor open.

## Evidence to retain

- Console lines prefixed with `[MelonPrime][instance=N]`.
- ARM9 hook address/mask dumps from a developer build with
  `MELONPRIME_ARM9_HOOK_DEBUG_LOG` enabled.
- Patch apply/restore OSD messages for each instance.
- Screenshots after each resize/editor/zoom transition.
- Thread-check warnings, especially the Phase 0 sensitivity-hotkey
  `Config::Save` warning that Phase 5 must remove.

## Expected pre-fix failures

- Hook, patch, HUD, zoom-cache, or input-consumer state may follow the other
  instance because it is file-static/process-shared.
- The sensitivity hotkey reports a GUI-thread ownership violation.

Run `tools/ci/audits/audit-melonprime-instance-state.ps1 -List` alongside this
harness to retain the static-state inventory for the same revision.
