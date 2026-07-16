# MphCodex Reference Workflow

`C:\Users\Admin\Documents\git\mphCodex` is the companion research repository
for MPH reverse-engineering notes, disassembly dumps, address databases, and
implementation handoff notes used by this repo.

Note: this checkout exists on the Windows dev machine only. On other machines
(e.g. the macOS clone of MelonPrimeDS), the path is unavailable — ask the user
where (or whether) mphCodex is checked out before relying on it.

Use it as read-mostly source material when a MelonPrimeDS change depends on
game behavior, ARM addresses, ROM-version offsets, or prior investigation
results. Do not edit files in `mphCodex` unless the user explicitly asks for
that repo to be updated.

## Entry Points

- Start with `C:\Users\Admin\Documents\git\mphCodex\CLAUDE.md`.
- Then read the linked rule files:
  - `.claude/rules/reverse-engineering-analysis.md`
  - `.claude/rules/arm-assembly-constraints.md`
- For investigation flow and reusable commands, read:
  - `tools/調査スキル.md`
  - `tools/調査コマンド集.md`

## Main Data Areas

- Address and struct notes:
  - `mnt/data/mphCodeDatabase/`
  - `mnt/data/mphAnalysis/`
  - `mnt/data/gameFunctionAnalysis/<VERSION>/`
- Full disassembly dumps:
  - `mnt/data/mphDump/JP1_0.txt`
  - `mnt/data/mphDump/JP1_1.txt`
  - `mnt/data/mphDump/US1_0.txt`
  - `mnt/data/mphDump/US1_1.txt`
  - `mnt/data/mphDump/EU1_0.txt`
  - `mnt/data/mphDump/EU1_1.txt`
  - `mnt/data/mphDump/KR1_0.txt`
- Patch and implementation notes:
  - `mnt/data/codes/`
  - `mnt/data/codesForMelonPrimeDS/`
  - `mnt/data/mphAim/`
  - topic folders such as `mnt/data/antiLag/`
- MphRead reference source and extracted notes:
  - `mnt/data/MphReadSource/`

## How To Find The Right Note

1. Search markdown first. Prefer existing curated notes over raw dump scans.

   ```powershell
   rg -n "Noxus|Shadow Freeze|0203E488|alt attack timer" `
     C:\Users\Admin\Documents\git\mphCodex\mnt\data `
     -g "*.md" -g "*.txt" `
     --glob "!mphDump/*.txt"
   ```

2. If a topic has numbered folders or files (`#1`, `#2`, `#5`, etc.), treat the
   user-specified number as authoritative. Otherwise, inspect README files and
   modification dates before assuming which one is latest.

3. Use `mnt/data/gameFunctionAnalysis/<VERSION>/` when you already know an ARM
   function address. These files are usually the fastest bridge from one
   address to role, registers, side effects, and patch risks.

4. Use `mnt/data/analysis/mphAnalysis/_JP1_0/` for broader struct and behavior maps.
   Common anchors include player struct, hunter data, damage flow, weapon data,
   input flow, and patch-specific investigation reports. HUD / NoHud work usually
   starts in that folder with:
   - `HUD-Selective-Hide-JP1_0.md` / `HUD-Selective-Hide-AllVersions.md`
   - `HUD_NoHud_Patch_Analysis-JP1_0.md` / `HUD_NoHud_Patch_Reverse_Analysis-AllVersions.md`
   - per-element notes (`HUD-HP-Ammo-Display-JP1_0.md`, `HUD-Crosshair-JP1_0.md`, etc.)
   - `addresses-JP1_0.md` for cross-topic address lookup

5. Use `mnt/data/MphReadSource/` when behavior needs source-level confirmation.
   It is especially useful for projectile, hunter, affliction, model, and draw
   flow semantics.

6. For **match-end detection** (`isEndOfGame`: `currentMode`, `flowState` in MelonPrimeDS), start with
   `mnt/data/mphAnalysis/_Commons/試合中かmenuかの判定/`. The current implementation
   handoff is folder `5_End-Match-Detection-Condition-Update-FlowState1Or2-AllVersions`
   (`MphEndMatchScoreboardCameraDetection.h`, address maps). MelonPrime runtime notes:
   [../architecture/gameplay/battle-flow-state.md](../architecture/gameplay/battle-flow-state.md).

## Raw Dump Handling

The files in `mnt/data/mphDump/*.txt` are large and may be UTF-16LE. Do not open
or read the whole dump casually. Prefer targeted streaming searches.

Example PowerShell address context:

```powershell
Select-String `
  -Path C:\Users\Admin\Documents\git\mphCodex\mnt\data\mphDump\JP1_0.txt `
  -Pattern "0203E488" `
  -Encoding Unicode `
  -Context 20,60
```

Example Python streaming scan:

```python
from pathlib import Path

path = Path(r"C:\Users\Admin\Documents\git\mphCodex\mnt\data\mphDump\JP1_0.txt")
needle = "0203E488"

with path.open("r", encoding="utf-16le", errors="ignore") as f:
    for lineno, line in enumerate(f, 1):
        if needle in line:
            print(lineno, line.rstrip())
```

## Address And Version Discipline

- Keep the ROM version attached to every address (`JP1_0`, `US1_1`, `KR1_0`,
  etc.).
- Do not copy a JP address into MelonPrimeDS as an all-version address without
  finding the matching per-version entries or deriving them from the relevant
  dump.
- Cheat-code style values may need normalization:
  - `22xxxxxx` byte writes often map back to `02xxxxxx`.
  - `12xxxxxx` halfword writes often map back to `02xxxxxx`.
  - Short RAM notes such as `DC5D4` may mean `020DC5D4` when context is main
    RAM.
- Treat post-`021017E8` function boundaries as candidates unless control flow
  and known references agree.

## Applying Findings In This Repo

- Map confirmed addresses into the local patch/hook tables in
  `src/frontend/qt_sdl/`, keeping the existing per-version table style.
- Prefer existing MelonPrimeDS patch helpers and runtime hook patterns over new
  one-off code.
- Preserve the evidence trail in comments only when it helps future address
  audits; long investigation detail belongs in markdown, not hot-path source.
- After implementing, build with `tools\build\windows\build-mingw.bat` or the
  existing-build variant as described in `docs/development/build/overview.md`.

## Useful Cross-Checks

- If a note claims a function role, verify at least one of:
  - matching nearby calls in `mphDump/<VERSION>.txt`,
  - struct offsets from `mphAnalysis/_JP1_0/`,
  - MphRead semantic match,
  - all-version address map in `codesForMelonPrimeDS/` or topic notes.
- If a patch point is near damage, input, projectile, or death/respawn logic,
  check whether the state is per-player, local-player only, match-global, or
  visual-only before writing RAM from a hook.
