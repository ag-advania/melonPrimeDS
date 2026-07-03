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

Run and capture stderr/stdout. The wrapper starts the app with
`MELONPRIME_PERF=1`, writes the raw log under `artifacts/perf-baseline/`, then
automatically writes a `.summary.txt` file after the app exits:

```zsh
.claude/skills/collect-perf-baseline.sh --label macos -- \
  build-mac/melonPrimeDS.app/Contents/MacOS/melonPrimeDS
```

## Windows

Use the checked-in MinGW build wrapper from a Windows shell:

```bat
.\.claude\skills\build-mingw.bat
```

Run from the repo root or the built binary directory:

```bat
powershell -ExecutionPolicy Bypass -File .\.claude\skills\collect-perf-baseline.ps1 `
  -Label windows `
  -Binary build\release-mingw-x86_64\src\frontend\qt_sdl\melonPrimeDS.exe
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
.claude/skills/collect-perf-baseline.sh --label linux-vm -- \
  build-linux/melonPrimeDS
```

## Summarize

Run from the repo root for each log:

```bash
python3 .claude/skills/summarize-melonprime-perf.py artifacts/perf-baseline/<log>.log
```

Record the shutdown `p50/p95/p99/max`, the `draw` value from "section median of
1 Hz averages", and the counter rates into the files below. The collection
wrappers call the summarizer with `--markdown-platform`, so each `.summary.txt`
contains ready-to-paste V6/V5 table rows plus the counter cells for that
platform.

- `.claude/rules/melonprime-full-refactor-plan-v6.md` §8
- `.claude/rules/completed/melonprime-full-refactor-plan-v5.md` Phase 0 table

When one or more summaries are ready, apply them mechanically:

```bash
python3 .claude/skills/apply-perf-baseline.py \
  --macos artifacts/perf-baseline/macos-perf-YYYYMMDD-HHMMSS.summary.txt \
  --windows artifacts/perf-baseline/windows-perf-YYYYMMDD-HHMMSS.summary.txt \
  --linux artifacts/perf-baseline/linux-vm-perf-YYYYMMDD-HHMMSS.summary.txt
```

Omit any platform that has not been measured yet; the script updates only the
provided platform rows/cells.

For S24, compare two runs from the same platform:

```bash
python3 .claude/skills/compare-perf-repro.py \
  artifacts/perf-baseline/macos-perf-run1.summary.txt \
  artifacts/perf-baseline/macos-perf-run2.summary.txt
```

The command exits non-zero if p50 or p99 differs by more than +/-10%.

For S22 release-trace checks, build with developer features disabled and verify
that the release binary does not contain perf probe strings:

```bash
strings <release-binary> | rg 'MelonPrimePerf|MELONPRIME_PERF|\\[MelonPrimePerf\\]' || true
nm <release-binary> 2>/dev/null | rg 'MelonPrimePerf|MELONPRIME_PERF' || true
```
