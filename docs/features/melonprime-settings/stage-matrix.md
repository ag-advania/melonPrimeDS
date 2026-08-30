# Stage and mode matrix expansion

## Setting contract

| Item | Value |
| --- | --- |
| Base key | Metroid.GameFeature.ExpandStageMatrix |
| Base default | false |
| Extra key | Metroid.GameFeature.ExpandStageMatrixExtra |
| Extra default | false |
| UI dependency | Extra is disabled unless Base is checked |
| Source | MelonPrimePatchExpandStageMatrix.cpp |
| Patch kind | Guarded RAM-data byte writes; no ARM instruction patch |

The feature changes the stage/mode availability matrix while the stage-select
menu's matrix is resident. It does not create new stage assets or modify the
rules of a selected stage; the guest's existing setup data is reused.

## Matrix addressing

The source uses:

~~~text
column = setupMode - 2
address = matrixBase + row * 0x0D + column
~~~

Columns are:

| Column | setupMode | Mode |
| ---: | ---: | --- |
| 0 | 2 | Battle |
| 1 | 3 | Bounty |
| 2 | 4 | Capture |
| 3 | 5 | Defender |
| 4 | 6 | Node |
| 5 | 7 | Prime Hunter |
| 6 | 8 | Survival |

All enabled cells are written as 0x01. When a selected feature is disabled,
the same cells are reconciled to 0x00.

## ROM matrix bases and guard functions

| ROM | Matrix base | Count recompute | Compatibility check | Count literal | Check literal | Count prologue |
| --- | --- | --- | --- | --- | --- | --- |
| JP1.0 | 0x02145678 | 0x021377C0 | 0x0213831C | 0x0213783C | 0x02138370 | 0xE92D40F0 |
| JP1.1 | 0x02145638 | 0x02137780 | 0x021382DC | 0x021377FC | 0x02138330 | 0xE92D40F0 |
| US1.0 | 0x02143384 | 0x02135588 | 0x021360E4 | 0x02135604 | 0x02136138 | 0xE92D40F0 |
| US1.1 | 0x02143E8C | 0x0213604C | 0x02136BA8 | 0x021360C8 | 0x02136BFC | 0xE92D40F0 |
| EU1.0 | 0x02143F74 | 0x02136164 | 0x02136CC0 | 0x021361E0 | 0x02136D14 | 0xE92D40F0 |
| EU1.1 | 0x02143F18 | 0x021360D8 | 0x02136C34 | 0x02136154 | 0x02136C88 | 0xE92D40F0 |
| KR1.0 | 0x02136898 | 0x0212B3F8 | 0x0212A9CC | 0x0212B468 | 0x0212AA1C | 0xE92D40F8 |

The compatibility-check prologue is 0xE92D4070 for all seven ROM groups.

## Enabled cells

Base expansion:

| Row | Column | Mode / stage | UI-described content |
| ---: | ---: | --- | --- |
| 3 | 3 | Defender / High Ground | Items present; defender ring at bottom-left of Magmaul area |
| 17 | 3 | Defender / Elder Passage | Items present; defender ring at bottom-left of Magmaul area |
| 18 | 1 | Bounty / Fuel Stack | Octolith present; items available |
| 22 | 2 | Capture / Celestial Gateway | Octolith present; jumpers and items available |
| 23 | 1 | Bounty / Alinos Gateway | Octolith present; affinity weapon below slope |

Extra expansion:

| Row | Column | Mode / stage | UI-described content |
| ---: | ---: | --- | --- |
| 7 | 0,4,5,6 | Battle, Node, Prime Hunter, Survival / Transfer Lock Wide | Normal gravity |
| 8 | 3 | Defender / Transfer Lock | No defender rings; Volt Driver and Battlehammer only |
| 10 | 3 | Defender / Compressor Room | No defender rings, jumpers, or items |
| 11 | 3 | Defender / Incubator | No defender rings; items and jumpers present |
| 18 | 3 | Defender / Fuel Stack | No defender rings; no floating; Imperialist only |
| 21 | 3 | Defender / Head Shot | No defender rings; normal gravity; two jumpers; Alt Form corridor does not launch |

The Extra switch requests the nine extra cells only when the Base switch is
also enabled. The source computes desiredExtra as Base && Extra.

## Three-point loaded-state guard

The matrix is loaded asynchronously by the guest, so a ROM-group match alone
is not sufficient. The source requires all of the following:

1. Ten fixed u32 prelude words at matrixBase - 0x28:

   ~~~text
   0x00000014 0x00000019 0x0000001E 0x00000028 0x00000032
   0x0000003C 0x00000046 0x00000050 0x0000005A 0x00000064
   ~~~

2. The first 32 bytes at matrixBase match the current prefix signature:

   ~~~text
   01 00 00 01 01 01 01 01 00 00 00 00 00 01 00 01
   01 01 01 01 01 00 00 00 00 00 01 00 00 01 01 01
   ~~~

3. Both guard functions have their expected prologues and both literal
   addresses contain matrixBase.

The first matrix byte is outside every feature cell, so it serves as a cheap
load/unload sentinel. The source first checks a five-word readiness signature,
then performs the complete guard. A failed candidate enters bounded retry
cooldown rather than performing the full read set on every frame.

## Reconciliation state machine

The per-instance state tracks ROM group, status, candidateSeen,
appliedBase, appliedExtra, pendingRestore, and retry timing.

- A new ROM, savestate/load invalidation, or guest unload returns the state to
  Unknown/WaitingForLoad.
- A valid full guard moves it to Verified.
- While Verified, the first matrix byte is checked cheaply for unload.
- A changed configuration sets pendingRestore and reconciles base and extra
  cells to their desired 0x01/0x00 values.
- The feature is rechecked out of game by the direct registry dispatch.
- Patches_ResetAll resets host bookkeeping; it does not blindly restore
  arbitrary guest bytes.

## Verification checklist

- Test every ROM table entry, including KR's distinct prologue and base.
- Mutate each of the three guard layers and confirm no cell is written.
- Test a partial matrix load and confirm retry/cooldown behavior.
- Test Base off, Extra on; desiredExtra must remain false.
- Test Base on/off and Extra on/off after the matrix is already resident.
- Test guest unload/reload and savestate reconciliation.
- Confirm each cell is written to the computed matrix address, not to an ARM
  code site.

## References

- src/frontend/qt_sdl/MelonPrimePatchExpandStageMatrix.cpp
- src/frontend/qt_sdl/MelonPrimeStageMatrixValidation.h
- src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp
- C:/Users/Admin/Documents/git/mphCodex/mnt/data/analysis/mphAnalysis/_Commons/!MainFunc/Menu-Integrated-AllVersions-v56/current/03_StageMatrixLoadedGuard-v56.hpp
- C:/Users/Admin/Documents/git/mphCodex/mnt/data/analysis/mphAnalysis/topics/gameplay/SaveFlags/current/unique_sources/021_8_Unlock-Bitflag-Correspondence-JP1_0_02145514-stage-row-to-unlock-bit-table-JP1_0.md
