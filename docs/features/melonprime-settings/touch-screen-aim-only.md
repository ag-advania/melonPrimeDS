# Touch-screen aim-only and Standard-transform exception

## Setting contract

These two settings are a parent/child pair in **Input Settings**:

| UI control | Configuration key | Default |
| --- | --- | ---: |
| Use the Whole Touch Screen for Aiming | Metroid.Enable.touchScreenAimOnly | false |
| Turn It Off Briefly While a Standard-Method Transform Fires | Metroid.Enable.touchScreenAimOnlySuspendForTransform | true |

The child default is intentionally true, but it has no independent effect. The
dialog enables it only while **Stylus Mode** and the parent option are checked.
The patch module applies the parent only when both of these are true:

```text
Metroid.Enable.stylusMode
AND Metroid.Enable.touchScreenAimOnly
```

If Stylus Mode is turned off while the aim-only value remains saved, the patch
is restored. A checked but disabled child widget therefore means “remember this
choice for the next active parent state,” not “the exception is active now.”

## What the parent patch changes

The in-match player touch dispatcher tests three bottom-screen HUD areas:

| Region | IDs | Normal action |
| --- | --- | --- |
| Transform / Unmorph | 4 | Request a form change |
| Weapon quick slots | 0–2 | Select a slot weapon, including the touch zoom shortcut |
| Weapon radial menu | 3 | Open/process the radial menu |

Each ROM call site is immediately followed by `cmp r0,#0`. Replacing the
hit-test call with `0xE3A00000` (`mov r0,#0`) takes the existing non-hit branch.
The touch can then continue as aim input without setting `NoAimInput` or
dispatching the corresponding HUD action.

This is deliberately narrower than disabling the shared hit-test function or
clearing `NoAimInput` globally. The shared function is used by other UI/menu
paths, and forcibly clearing the bit would not prevent the HUD action itself.

The patch does not alter the separate double-tap jump or touch boost gesture
handlers. Those gestures can therefore remain active while the three HUD areas
are neutralized.

## ROM patch table

All three applied words are `0xE3A00000`. The table records the guarded
original call word for every site; matching an address alone is not sufficient
authorization to write it.

| ROM | Transform ID4 | Quick slots ID0–2 | Weapon menu ID3 |
| --- | --- | --- | --- |
| JP1.0 | `0x02026C70 / 0xEB004943` | `0x02026E40 / 0xEB0048CF` | `0x02026F70 / 0xEB004883` |
| JP1.1 | `0x02026C70 / 0xEB004943` | `0x02026E40 / 0xEB0048CF` | `0x02026F70 / 0xEB004883` |
| US1.0 | `0x02026C94 / 0xEB0048F7` | `0x02026E64 / 0xEB004883` | `0x02026F94 / 0xEB004837` |
| US1.1 | `0x02026C94 / 0xEB0048D0` | `0x02026E64 / 0xEB00485C` | `0x02026F94 / 0xEB004810` |
| EU1.0 | `0x02026C8C / 0xEB0048D0` | `0x02026E5C / 0xEB00485C` | `0x02026F8C / 0xEB004810` |
| EU1.1 | `0x02026C94 / 0xEB0048D0` | `0x02026E64 / 0xEB00485C` | `0x02026F94 / 0xEB004810` |
| KR1.0 | `0x0200C308 / 0xEB007E20` | `0x0200C4C8 / 0xEB007DB0` | `0x0200C5E8 / 0xEB007D68` |

Each cell is `address / original word`. `StaticWordPatch` treats the three
words as one ROM-owned span: apply accepts original or already-applied values,
and an unknown foreign word rejects the operation instead of partially
patching the dispatcher. Restore writes back only owned applied words.

## Why Standard transform needs an exception

The Standard transform method does not call the native transform routine
directly. It simulates a tap on touch region ID4 at the preset-mirrored center
of the Transform button. The parent patch makes that exact hit-test return
false, so without an exception Standard transform cannot fire while aim-only
is active.

The child option brackets only that synthetic tap:

```text
HandleRareMorph()
  -> TouchScreenAimOnly_RestoreOnce()
  -> release touch
  -> advance two frames
  -> press preset-mirrored Transform center
  -> advance two frames
  -> release touch
  -> advance two frames
  -> TouchScreenAimOnly_ApplyOnce()
```

Reapplication goes through the patch owner and re-reads the current config. It
does not blindly rewrite the three words. This matters if the user changed
Stylus Mode or the parent setting while the sequence was running.

The exception applies only when execution reaches the Standard touch path.
Transform New Method and New Method 2 do not synthesize this tap and therefore
do not need the hit-test restored. During the spawn-invulnerability window,
native transform methods intentionally fall back to Standard; the same bracket
then protects that temporary Standard path.

Standard weapon switching is different. Its legacy implementation drives the
weapon-change state and separately brackets the no-double-tap-jump patch; it
does not depend on Transform region ID4. The new child setting must not be
described as a general “temporarily disable aim-only for all input.”

## Lifecycle and ownership

`MelonPrimePatchRegistry` applies or restores the parent at `BattleRuntime` and
`ConfigReload`, with restore on leave and stop. The temporary exception is an
in-frame call through `TouchScreenAimOnly_RestoreOnce` and
`TouchScreenAimOnly_ApplyOnce`; it does not add a second patch table or writer.

Relevant state boundaries:

| Boundary | Expected result |
| --- | --- |
| Stylus off | Parent patch restored even if the saved aim-only value is true |
| Stylus on, parent off | No guest words owned/applied |
| Stylus on, parent on | All three guarded hit-tests neutralized |
| Standard transform, child on | Restore for the tap sequence, then guarded reapply |
| Standard transform, child off | Transform region remains neutralized; the simulated transform does not fire |
| Native transform method | No temporary restore in the normal native path |
| Leave/stop | Restore all owned words and reset patch state |

## Verification checklist

- Confirm the UI child is editable only when Stylus Mode and the parent are
  checked, while its saved value survives a temporary disabled state.
- On each ROM, verify all three original words before enabling the parent and
  verify all three restore on disable, leave, and stop.
- Test Transform, all three quick slots, and the radial menu with the parent
  off/on; the three HUD actions should stop only in the active parent state.
- Confirm free touch aiming still works over the former HUD rectangles.
- Confirm double-tap jump and the touch boost gesture remain separate.
- Test Standard transform with the child on and off, including left-handed
  presets whose Transform X coordinate is mirrored.
- Test New Method and New Method 2 transform with the child on; neither should
  require a synthetic touch or a temporary patch restore.
- Test a selected native transform method during the spawn-invulnerability
  fallback, where Standard temporarily owns the request.
- Force one foreign word at a site in a disposable test environment and verify
  the complete apply is rejected rather than partially written.
- Report static guard inspection, build results, and physical gameplay results
  separately.

## Source and research ownership

Current melonPrimeDS implementation:

- `src/frontend/qt_sdl/MelonPrimePatchTouchScreenAimOnly.cpp`
- `src/frontend/qt_sdl/MelonPrimeInGame.cpp`
- `src/frontend/qt_sdl/MelonPrimeRuntimeConfig.cpp`
- `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp`
- `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigConfig.cpp`
- `src/frontend/qt_sdl/Config.cpp`

The reverse-engineering evidence remains single-sourced in mphCodex:

- `C:\Users\Admin\Documents\git\mphCodex\mphAnalysis\Battle\TouchscreenInput\current\InMatch-Touchscreen-AimOnly-Patch-Map-AllVersions.md`
- `C:\Users\Admin\Documents\git\mphCodex\mphAnalysis\Battle\TouchscreenInput\current\InMatch-Touchscreen-Reaction-Areas-AimOnly-Investigation-AllVersions.md`
- `C:\Users\Admin\Documents\git\mphCodex\mphAnalysis\Battle\TouchscreenInput\evidence\functions\02026BFC-player-touch-ui-action-dispatch-JP1_0.md`

Those files explain the guest hit-test and action flow; this page owns the
current melonPrimeDS configuration, lifecycle, and temporary-restore behavior.
