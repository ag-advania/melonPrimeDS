# Low HP warning threshold

## Setting contract

| Item | Value |
| --- | --- |
| Mode key | Metroid.LowHpWarning.Mode |
| Mode default | 0 |
| Fixed key/default | Metroid.LowHpWarning.Fixed / 25 |
| Low key/default | Metroid.LowHpWarning.Low / 20 |
| Medium key/default | Metroid.LowHpWarning.Medium / 30 |
| High key/default | Metroid.LowHpWarning.High / 40 |
| Auto base key/default | Metroid.LowHpWarning.AutoBase / 30 |
| Numeric range | 0–255 |
| Source | MelonPrimePatchLowHpWarning.cpp |

The combo order is part of the ABI between the Qt UI and the patch:

| Mode | Meaning |
| ---: | --- |
| 0 | Disabled; leave vanilla threshold 25 untouched |
| 1 | Fixed; use Fixed for every damage level |
| 2 | Per Damage; select Low, Medium, or High from guest DamageLevel |
| 3 | Auto Scale; derive a threshold from AutoBase and DamageLevel |

The UI enables only the spin boxes used by the selected mode. Changing a
spinbox does not immediately write guest RAM; the patch reads the current
configuration at the match-join lifecycle edge.

## Threshold calculation

The guest instruction is the ARM compare template:

~~~text
vanilla: 0xE3500019 = cmp r0,#0x19
template: 0xE3500000
~~~

The source clamps every user value to 0–255 and writes:

~~~text
patchedWord = 0xE3500000 | threshold
~~~

Per Damage reads one byte from the ROM-specific DamageLevel location:

| DamageLevel | Selected value |
| ---: | --- |
| 0 | Low |
| 1 | Medium |
| 2 | High |
| other | Medium fallback |

Auto Scale uses integer rounding:

~~~text
Low:    (base * 3 + 2) / 4
Medium: base
High:   (base * 5 + 2) / 4
~~~

This is approximately 0.75x, 1.0x, and 1.25x. It is integer arithmetic, not
floating point; the rounding behavior matters for odd bases.

## Address table

| ROM | Compare instruction | DamageLevel byte |
| --- | --- | --- |
| JP1.0 | 0x02105CC0 | 0x020E9A48 |
| JP1.1 | 0x02105C80 | 0x020E9A08 |
| US1.0 | 0x02103B80 | 0x020E7908 |
| US1.1 | 0x02104640 | 0x020E83C8 |
| EU1.0 | 0x02104660 | 0x020E83E8 |
| EU1.1 | 0x021046E0 | 0x020E8468 |
| KR1.0 | 0x020FCB64 | 0x020E1204 |

## Safety and verification status

**OPEN / UNVERIFIED.** The current source comments state that the compare and
DamageLevel locations were carried over from an earlier investigation and are
not yet verified against confirmed runtime sites. This is why Mode 0 is a
true no-op rather than a write of the vanilla word.

Before applying a non-disabled mode, the implementation checks:

~~~text
(currentWord & 0xFFFFFF00) == 0xE3500000
~~~

This accepts vanilla or another threshold already produced by this feature.
It rejects an unrelated word. Restore accepts the same template and writes
0xE3500019 only when the current value is non-vanilla.

The registry gives the feature RF_OnLeave and RF_OnStop restore behavior.
There is no persistent patch state beyond the guest word; a new match reads
DamageLevel again.

The compare addresses and setup flow also appear in the local mphCodex
research document:

C:/Users/Admin/Documents/git/mphCodex/mnt/data/analysis/mphAnalysis/topics/system/SystemPatchImprovementsEtc/LowHpWarning/current/Low-HP-Warning-Sfx-Threshold-MelonPrime-Patch-Instructions-v4-SetupApply-AllVersions.md

That document is supporting reverse-engineering material, not closure of this
source-level verification status.

## Verification checklist

- Test all four mode values and all threshold clamps.
- Test Low/Medium/High with DamageLevel 0, 1, 2, and an unexpected byte.
- Confirm Auto Scale rounding at bases 1, 2, 3, 255.
- Confirm Mode 0 performs no RAM write.
- Confirm an unrelated compare word is rejected.
- Confirm leave/stop restores only a word matching the feature template.
- Capture physical warning sound/HUD behavior for every ROM group before
  changing the status from UNVERIFIED.

## References

- src/frontend/qt_sdl/MelonPrimePatchLowHpWarning.cpp
- src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp
- ../../architecture/gameplay/patch-system.md
