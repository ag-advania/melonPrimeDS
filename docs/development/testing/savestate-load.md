# Savestate load test and runtime safety

This document is the source of truth for savestate-load diagnostics used by
the Custom HUD and renderer tests. A successful file parse is not enough: the
emulation thread must resume the matching gameplay scene, advance frames, and
show no ARM9 abort or renderer failure.

## Production load path

The normal frontend path is:

1. The UI sends `msg_LoadState` to `EmuThread`.
2. The emulation thread calls `EmuInstance::loadState()` at a message boundary.
3. On success it calls `MelonPrimeCore::OnSavestateLoaded()`.
4. The next ordinary frame executes the normal input, `RunFrameHook()`,
   `RunFrame()`, and renderer/presentation sequence.

The diagnostic path must not invent a second emulated frame. In particular, it
must not call `NDS::RunFrame()` directly from `handleMessages()` and must not
inject START into an in-match snapshot.

## 2026-08-18 incident and correction

The first Windows DX12 scoreboard smoke runs were invalidated. The diagnostic
hook had been called from `msg_EmuRun`, before the first normal frame applied
`videoSettingsDirty` and created/configured the selected renderer. The captured
log order was:

```text
[SavestateDiff] ...
Renderer transition begin ...
```

The temporary command also set `MELONPRIME_TEST_SAVESTATE_UNPAUSE=1`. That code
sent START and called `NDS::RunFrame()` from the message-drain path. This is not
the production load order and is unsafe for an in-match state. One such run
reported:

```text
ARM9: data abort (0202F6FC)
```

Its HUD counters and screenshots are rejected as runtime evidence. An
intermediate no-START version loaded the state before the first ordinary frame
and then reported repeated ARM9 aborts; it is also rejected because it did not
match the production running-state boundary. A subsequent attempt placed the
hook only at the end of `handleMessages()`, but the empty-queue fast return
prevented that call from running at all; it produced no load marker or frame
progress and was stopped. Both intermediate runs are failed diagnostic
experiments, not evidence that the normal UI load path is broken.

The correction is in
[`src/frontend/qt_sdl/EmuThread.cpp`](../../../src/frontend/qt_sdl/EmuThread.cpp):

- the developer-only diagnostic load is attempted once after a short warm-up
  of ordinary frames, from the same post-frame message-drain boundary used by
  the UI state-load request. When the queue is empty, the developer-only
  fast-path now reaches that boundary before returning; the shipping fast-path
  remains a zero-work return;
- the direct START pulse was removed;
- the legacy `MELONPRIME_TEST_SAVESTATE_UNPAUSE` variable is ignored with a
  warning;
- the no-state environment check is one-shot and therefore does not add a
  per-frame environment lookup.

Diagnostic states must be captured in the desired running gameplay state. Do
not use an unpause variable to alter a match state during a timing run.

## Validation status of the correction

The correction was compiled from base ref `8f60839d1e416ff33203df61c82d26150707fcfe`
with the working-tree `EmuThread.cpp` change. Both existing-tree Windows MinGW
builds passed on 2026-08-18:

- Release `build/release-mingw-x86_64`: build and every invoked post-build test
  target passed, including the 82 registered-language Classic On-Screen Edit
  cases.
  The executable SHA-256 was
  `862C51BDE9298B9C86E6593F227EB6FDD1A42E84E6A1C3A9A2CEDF73CE87C176`.
- Debug `build/debug-mingw-x86_64`: build and the same invoked test suite
  passed. The executable SHA-256 used for the final correction run was
  `4407C955DEF2838260D5171BE9090D4F91BC1FD5D4110786D6DD03F9F955E101`.

These are source/build checks. The Debug executable hash above is the build
used for the successful post-correction runtime check recorded below.

## Post-correction runtime result

The exact ROM and `.ml4` state were run through the DX12 raster-differential
runner after the fast-path boundary correction:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File \
  tools/testing/run-raster-differential.ps1 \
  -Renderer DX12 \
  -Executable build/debug-mingw-x86_64/melonPrimeDS.exe \
  -RomPath "C:\DSMPH\melonPrimeDS\all roms\allRoms\0367 - Metroid Prime - Hunters (USA) (Rev 1).nds" \
  -LoadSlot 4 -PostLoadSeconds 8
```

Result: **PASS for savestate liveness and raster differential**, not a
60/120/144/240 Hz performance A/B. The run used the developer executable
SHA-256
`4407C955DEF2838260D5171BE9090D4F91BC1FD5D4110786D6DD03F9F955E101` and
recorded the following order:

```text
Renderer transition complete previous=-1 actual=4
[SavestateDiff] ... loaded=1
[RasterDiffTransition] discarded=1 reason=savestate-load
```

It produced 392 advancing DX12 raster-differential frames. Every frame had
`mismatchedPixels=0`; there was no `ARM9: data abort`, `DEVICE_LOST`, renderer
fatal marker, or process hang. `CustomHud=true` was used
(`customHudForcedOff=0`). The save hash after the run remained equal to the
pre-test backup:
`A2FA055578D0962978C3889044F3A78B9EBE7F929203B7B2C6FCF8EDA934B723`.

This confirms the corrected diagnostic boundary is live and equivalent for
this test's state-load/liveness purpose. The earlier ARM9-abort result remains
rejected incident history because it came from the pre-correction startup
hook and synthetic extra-frame path. Cross-backend and controlled refresh-rate
HUD performance gates remain separate and open.

## State/ROM identity

Savestates do not provide a sufficient ROM identity check for this diagnostic
purpose. Every run must therefore record and verify the ROM and state pair
before launch. As a rejected Windows test example:

| Item | SHA-256 |
|---|---|
| `0367 - Metroid Prime - Hunters (USA) (Rev 1).nds` | `BCD9C2D408825589C35C6754C0EFB547CBAE78FBDA9CE7F69500A9CAB8E70B8F` |
| `0367 - Metroid Prime - Hunters (USA) (Rev 1).ml4` | `013EAA02EC3E16DF5BE3C2B2A1F3CB48CF6C1B956ADB83632CCBC70DD000B853` |

The working save file was restored after the test. Its SHA-256 matched the
pre-test backup:

```text
A2FA055578D0962978C3889044F3A78B9EBE7F929203B7B2C6FCF8EDA934B723
```

## Required runtime acceptance

Before using a state-load run for HUD/performance evidence, record:

- exact source SHA and developer executable SHA;
- renderer and presentation backend;
- exact ROM/state paths and hashes;
- `CustomHUD` and scoreboard/outline settings;
- the load marker `[SavestateDiff] ... loaded=1`;
- renderer setup log preceding the load marker;
- no `ARM9: data abort`, `DEVICE_LOST`, fatal renderer, or process hang;
- frame/performance output after the load marker, with a non-zero advancing
  frame count;
- save-file hash before and after, restoring it if the test touches it.

If the process does not reach the load marker and then produce advancing frames,
classify the run as `FAIL`/`NOT RUN`; do not report HUD counters, screenshots,
or frame timings from it. The post-correction liveness gate now passes for the
recorded DX12 run, but the Custom HUD 60/120/144/240 Hz matrix and cross-backend
pixel regression gates remain `NOT RUN`/`OPEN`.

## Reproducible diagnostic invocation

Use a developer build and a state captured in the intended live scene:

```powershell
$env:MELONPRIME_TEST_SAVESTATE = (Resolve-Path $statePath).Path
$env:MELONPRIME_PERF = '1'
Remove-Item Env:MELONPRIME_TEST_SAVESTATE_UNPAUSE -ErrorAction SilentlyContinue
& $developerExecutable $romPath
```

For the raster-differential runner, use
`tools/testing/run-raster-differential.ps1 -LoadSlot <slot>`. The runner now
clears the legacy unpause environment variable for the child process and fails
if the post-load log contains an ARM9 abort, `DEVICE_LOST`, or renderer-fatal
marker.

The `MELONPRIME_TEST_SAVESTATE_UNPAUSE` variable is intentionally not a valid
way to resume a state. Use a state saved while the match is already running.
