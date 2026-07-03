# MelonPrime Performance Baseline Procedure

Purpose: collect the V6 Phase 0 baseline with `MELONPRIME_PERF=1` on macOS,
Windows, and Linux VM without committing ROMs, screenshots, or raw logs.

## Responsibilities

| Step | AI can do | User must do |
|---|---|---|
| Build dev binaries | Yes | No |
| Start the app with `MELONPRIME_PERF=1` | Yes, when local OS is available | Yes, on real Windows/Linux/macOS runs not exposed to AI |
| Load ROM and enter an in-game scenario | No | Yes |
| Run the 10 minute soak and quit cleanly | No | Yes |
| Parse logs and update V5/V6 tables | Yes | Provide logs if AI cannot access the machine |

Keep logs under `artifacts/perf-baseline/` or another untracked directory. Do
not commit ROMs, saves, screenshots, or raw perf logs.

## Scenario

Use the same ROM, settings, resolution, renderer, HUD settings, and gameplay
state for every platform.

1. Start a developer build with `MELONPRIME_PERF=1`.
2. Enter an in-game battle or bot match and wait for normal 60 fps gameplay.
3. Play or leave the same controlled scene running for 10 minutes.
4. Repeat once on at least one platform for S24 reproducibility. p50 and p99
   should stay within +/-10%.
5. Quit the emulator cleanly so the shutdown histogram is written.

## macOS

Configure with developer features if the build tree is missing or stale:

```zsh
cmake -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libarchive)" \
  -DUSE_QT6=ON -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON
cmake --build build-mac --parallel 4
```

Run and capture stderr/stdout:

```zsh
mkdir -p artifacts/perf-baseline
MELONPRIME_PERF=1 build-mac/melonPrimeDS.app/Contents/MacOS/melonPrimeDS \
  2>&1 | tee artifacts/perf-baseline/macos-perf-$(date +%Y%m%d-%H%M%S).log
```

## Windows

Use the checked-in MinGW build wrapper from a Windows shell:

```bat
.\.claude\skills\build-mingw.bat
```

Run from the repo root or the built binary directory:

```bat
mkdir artifacts\perf-baseline
set MELONPRIME_PERF=1
build\release-mingw-x86_64\src\frontend\qt_sdl\melonPrimeDS.exe > artifacts\perf-baseline\windows-perf.log 2>&1
```

If the binary path differs, use the path printed by the build wrapper.

## Linux VM

Use the VM flow in `.claude/rules/linux-vm-build.md`. For FPS aim testing,
choose an Xorg session and turn VirtualBox mouse integration off.

Build in the guest:

```bash
bash /mnt/mp/tools/linux-vm/guest/guest-build-only.sh
```

Run in the guest:

```bash
cd /mnt/mp
mkdir -p artifacts/perf-baseline
MELONPRIME_PERF=1 build-linux/melonPrimeDS \
  2>&1 | tee artifacts/perf-baseline/linux-vm-perf-$(date +%Y%m%d-%H%M%S).log
```

## Summarize

Run from the repo root for each log:

```bash
python3 .claude/skills/summarize-melonprime-perf.py artifacts/perf-baseline/<log>.log
```

Record the shutdown `p50/p95/p99/max`, the `draw` value from "section median of
1 Hz averages", and the counter rates into:

- `.claude/rules/melonprime-full-refactor-plan-v6.md` §8
- `.claude/rules/completed/melonprime-full-refactor-plan-v5.md` Phase 0 table

For S22 release-trace checks, build with developer features disabled and verify
that the release binary does not contain perf probe strings:

```bash
strings <release-binary> | rg 'MelonPrimePerf|MELONPRIME_PERF|\\[MelonPrimePerf\\]' || true
nm <release-binary> 2>/dev/null | rg 'MelonPrimePerf|MELONPRIME_PERF' || true
```
