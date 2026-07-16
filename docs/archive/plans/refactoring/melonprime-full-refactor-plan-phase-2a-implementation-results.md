# Phase 2a Implementation Results

Implemented Phase 2a, no build and no commit.

## Module Results

| Module | Result | LOC |
|---|---|---:|
| `ShowHeadshotOnline` | Converted to `StaticWordPatch`; 1-word all-ROM table | `104 -> 61` |
| `ShowEnemyHpMeterOnline` | Converted; preserved KR1.0 2-word count | `142 -> 89` |
| `DisableDoubleDamageMultiplier` | Converted; 8-word all-ROM table | `174 -> 125` |
| `InstantAimFollow` | Converted; config gate kept, preserved KR1.0 1-word count | `170 -> 115` |
| `NoPickingUpSpecificItems` | Not converted: mask-selected partial application, `legacySkipVal`, per-word skip-on-failure, applied-mask state | `224 -> 224` |
| `FixWifi` | Not converted: base+offset data, first-word canary, no persistent state, no restore path | `108 -> 108` |

## Behavior Verification

| Module | Branch check against `HEAD` |
|---|---|
| `ShowHeadshotOnline` | invalid ROM, same-ROM reapply, ROM switch, verify-fail abort, disabled restore, reset-state-only all match |
| `ShowEnemyHpMeterOnline` | same branches match; helper span count preserves `3/3/3/3/3/3/2` verification/write behavior |
| `DisableDoubleDamageMultiplier` | same branches match; all 8 words are verified before any write, as before |
| `InstantAimFollow` | same branches match; `shouldApply` logic unchanged, helper preserves `5/5/5/5/5/5/1` counts |

## Semantic Notes

The shared helper covers the identical all-or-nothing state machine:

- `current == expected || current == already`
- abort the whole patch on any bad word
- restore prefers the stored applied ROM
- reset only clears state

It also supports `count == 0` unsupported ROM spans, but none of the converted modules use that path, so there is no behavior change there.

## Updated Docs

- `melonprime-patch-system.md`
  - implementation pattern now shows `StaticWordPatch`

- `melonprime-full-refactor-plan.md`
  - Phase 2 row now says `2a+2b実装完了（ビルド検証待ち）`

## `git diff --stat`

Tracked output:

```text
6 files changed, 103 insertions(+), 298 deletions(-)
```

## New File

```text
src/frontend/qt_sdl/MelonPrimePatchCommon.h
```

## Checks

- Re-read `MelonPrimePatchRegistry.cpp`; no edits needed.
- `git diff --check` found no whitespace errors.
- It only emitted the existing Windows LF/CRLF normalization warnings.
- Build not run, per your constraint.

## Suspicious but Untouched

- `FixWifi`
  - Still relies on “first word already applied means all 51 words are correct.”

- `NoPickingUpSpecificItems`
  - Intentionally skips individual failed word transitions instead of aborting the whole module.
