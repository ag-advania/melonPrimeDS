# Online enemy target information

## Setting contract

| Item | Value |
| --- | --- |
| Key | Metroid.GameFeature.ShowEnemyHpMeterOnline |
| Default | false |
| UI | Show Enemy Target Information Online |
| Source | MelonPrimePatchShowEnemyHpMeterOnline.cpp (legacy internal name) |
| Patch kind | Three guarded words, or two on KR |

The option removes the guest's online early-return so target slot, timer, and
target-information state can be produced during online play. The persisted key
and patch filename retain the older “HpMeter” name for compatibility, but the
visible label deliberately describes the broader data path.

Any HP-like value is only a rough hit indicator: online enemy HP is not
authoritative and is generally not updated as a reliable continuously
synchronized health value. The UI must not promise a real health bar.

## ROM patch table

All apply words are NOP 0xE1A00000.

| ROM | Address | Revert value |
| --- | --- | --- |
| JP1.0 / JP1.1 | 0x0202DBE4 | 0x128DD044 |
| JP1.0 / JP1.1 | 0x0202DBE8 | 0x18BD4030 |
| JP1.0 / JP1.1 | 0x0202DBEC | 0x112FFF1E |
| US1.0 / US1.1 | 0x0202DBC0 | 0x128DD044 |
| US1.0 / US1.1 | 0x0202DBC4 | 0x18BD4030 |
| US1.0 / US1.1 | 0x0202DBC8 | 0x112FFF1E |
| EU1.0 | 0x0202DBB8 | 0x128DD044 |
| EU1.0 | 0x0202DBBC | 0x18BD4030 |
| EU1.0 | 0x0202DBC0 | 0x112FFF1E |
| KR1.0 | 0x02036904 | 0x128DD044 |
| KR1.0 | 0x02036908 | 0x18BD8030 |

The patch is guarded as a complete word span by StaticWordPatch. It restores
on match leave and emulator stop and can be reapplied after ConfigReload.

## Behavior boundary

- It is a local target-information/rendering change.
- It does not make remote HP synchronized.
- It does not change weapon damage, hit registration, or server authority.
- A visible bar that remains stale is an expected limitation of the guest
  data path, not proof that the patch failed.

## Verification checklist

- Verify the full span guard on every ROM group.
- Test enabled/disabled transitions and restoration.
- Test a local wireless match and an online match separately.
- Record whether the value changes after a confirmed hit and whether it
  updates for each remote hunter.
- Keep runtime result labeled as rough indicator unless continuous HP updates
  are independently demonstrated.

## References

- src/frontend/qt_sdl/MelonPrimePatchShowEnemyHpMeterOnline.cpp
- src/frontend/qt_sdl/MelonPrimePatchRegistry.cpp
- C:/Users/Admin/Documents/git/mphCodex/mnt/data/analysis/mphAnalysis/_JP1_0/current/Enemy-HP-Meter-All-Versions.md
