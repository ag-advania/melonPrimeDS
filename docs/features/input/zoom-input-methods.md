# Zoom input methods

## What this selector changes

The Zoom selector chooses how a host Zoom pressed edge reaches Metroid Prime
Hunters. It does not change zoom sensitivity; that is the separate
`Metroid.Aim.ZoomScale.*` setting family.

| UI choice | `Metroid.Input.ZoomMethod` | Delivery path | Public build |
| --- | ---: | --- | --- |
| Standard Method | `0` | press the current control preset's Zoom binding | yes; default |
| retired value | `1` | treated as Standard and normalised on save | not displayed |
| New Method 2 | `2` | call `SetPlayerScopeZoom` at the weapon-action hook | no; developer-only |
| New Method 3 | `3` | DirectInvocation call from player input update | no; developer-only |

The three displayed choices form an exactly-one checkbox group. Selecting one
clears the other two, and attempting to clear the last selection restores it.
There is therefore no supported “all boxes unchecked” state. In a non-developer
build only Standard is available and the runtime ignores native values left by
a developer build.

`Config.cpp` owns the default (`0`) and range (`0..3`).
`MelonPrimeRuntimeConfig.cpp` resolves the saved value into two hot flags:

```text
value 0 or 1 -> neither native flag
value 2      -> nativeZoomToggle
value 3      -> directInvocationZoom
```

Disabling a native method clears its pending call and pressed-edge latch so an
old edge cannot be replayed after changing methods.

## Shared host input flow

The configured host key becomes `IB_ZOOM` in `ProjectDownState()`. Zoom is then
handled separately from the fast movement/B/L merge:

```text
UpdateInputState()
  -> ProjectDownState(): host key -> IB_ZOOM
ProcessMoveAndButtonsFastFromReset()
  -> movement plus ordinary B/L state
ApplyZoomBindingInput()
  -> alt-form handling
  -> spawn-window fallback
  -> selected Standard/New2/New3 path
```

The re-entrant frame-advance path calls the same zoom application routine. Both
native methods keep receiving released frames because they need release-edge
tracking and zoom-capability cleanup; Standard can return immediately when the
host key is up.

## Alt-Form is Morph Ball Boost, not scope zoom

Before selecting a zoom method, `ApplyZoomBindingInput()` checks the local
player's form. In Alt-Form, the host Zoom control drives the control preset's
`MorphBoost` binding and bypasses all scope-zoom methods. The native edge latch
is still synchronised so holding Zoom while leaving Alt-Form does not create a
late scope toggle.

This distinction is important for Dual-style presets: neither Zoom nor Morph
Ball Boost should be described as a fixed DS `R` key. Both use the binding
snapshot resolved from the active in-game control preset.

## Standard Method

Standard presses `m_presetBindings.Zoom` while the host Zoom control is held:

```cpp
m_inputMaskFast &= ~m_presetBindings.Zoom;
```

The game receives ordinary DS input and owns all capability checks, scope
state, HUD, sound, and release behaviour. Earlier implementations used a fixed
`INPUT_R` unless an experimental method was selected; that description is no
longer current. Touch R, Touch L, Dual R, and Dual L now use their own preset
binding without opting into another zoom method.

## New Method 2: native weapon-action toggle

New2 treats the host key as a pressed-edge toggle. Holding the key does not
repeat the request:

```text
up -> down   queue one desired-state request
held        no additional toggle
down -> up  update the latch only
```

The host side reads the local player, `player+0x850` bit 0 for current scope
state, and the current weapon pointer at `player+0x858`. Enabling scope requires
`weapon+0x08` bit `0x800`; disabling scope remains allowed even after the player
switches to a non-zoom-capable weapon. An idle failsafe queues scope-off if the
scope is active and the current weapon is no longer capable.

The request is consumed at the ROM's weapon action update and calls the game's
`SetPlayerScopeZoom(player, enabled)` through the trampoline at `0x02003F00`.
Scratch state begins at `0x02003F40`. The native setter, rather than the host,
owns the state flags, sound, Imperialist crosshair action, and FOV transition.

| ROM | local-player pointer global | setter | weapon-action hook |
| --- | ---: | ---: | ---: |
| JP1.0 | `0x020BE790` | `0x02015C98` | `0x02024174` |
| JP1.1 | `0x020BE750` | `0x02015C98` | `0x02024174` |
| US1.0 | `0x020BCA70` | `0x02015CB8` | `0x02024198` |
| US1.1 | `0x020BD2D0` | `0x02015CBC` | `0x02024198` |
| EU1.0 | `0x020BD2F0` | `0x02015CB0` | `0x02024190` |
| EU1.1 | `0x020BD370` | `0x02015CBC` | `0x02024198` |
| KR1.0 | `0x020B6240` | `0x0201CEBC` | `0x0200D07C` |

The pending request is cleared before the PC redirects. After the setter
returns, the trampoline branches back to the hook address; the second
dispatcher pass sees no pending request and resumes the original instruction
stream.

## New Method 3: DirectInvocation

New3 uses the same pressed-edge and native-setter semantics but consumes its
mailbox from the game's player input update after first running the original
`ProcessTouchInput` callee. It shares the DirectInvocation trampoline region
`0x02003F50..0x02003FBB` with native weapon/transform requests and does not own
a separate guest data block.

At the guest safe point it rejects scope-on when any of these are true:

- `player+0x4C4` bit 9: Alt-Form;
- `player+0x4C4` bit 11: Morphing;
- `player+0x4C4` bit 12: Unmorphing;
- the equipped weapon at `player+0x858` lacks `WeaponData+0x08` bit `0x800`.

The state checked is therefore the state acted on by the setter, not a
potentially stale host-side snapshot. Scope-off cleanup intentionally bypasses
the weapon capability check.

| ROM | player-input hook | original `ProcessTouchInput` | setter |
| --- | ---: | ---: | ---: |
| JP1.0 / JP1.1 | `0x020263DC` | `0x02026BFC` | `0x02015C98` |
| US1.0 | `0x02026400` | `0x02026C20` | `0x02015CB8` |
| US1.1 | `0x02026400` | `0x02026C20` | `0x02015CBC` |
| EU1.0 | `0x020263F8` | `0x02026C18` | `0x02015CB0` |
| EU1.1 | `0x02026400` | `0x02026C20` | `0x02015CBC` |
| KR1.0 | `0x0200CF1C` | `0x0200C29C` | `0x0201CEBC` |

## Local-player and spawn safety

New2 and New3 are match-scoped and refuse a native call while local-player HP
is zero. The gate exists at both producer and dispatcher because the player can
leave play between queue and consumption. An unresolved cached player pointer
counts as alive so cache reconstruction alone does not disable input.

HP becomes nonzero before respawn has finished rebuilding camera, model, gun,
animation, and HUD state. Two additional barriers cover that gap:

1. While `player+0xE1 != 0` (spawn invulnerability countdown), Zoom uses the
   Standard binding path and clears native pending state. The edge latch stays
   synchronised so a held key cannot toggle when the countdown ends.
2. The dispatcher drops an already-queued request on the first input hook after
   spawn, identified by
   `player+0xE1 == (uint8_t)([player+0x404]+0xE2) - 1`.

Requests are dropped or handled by Standard, never deferred for replay after
respawn. See [Input method selectors](../melonprime-settings/input-methods.md)
for the lifecycle shared with weapon and transform.

## Immediate Input Edge Overlay interaction

The developer-only Immediate Input Edge Overlay writes host action state into
the guest input structure at the action consumer. Its current zoom mask is:

```cpp
zoomMask = (!genericOverlay || m_enableNativeZoomToggle)
    ? 0
    : ReadBindingLow(player + 0x3E0);
```

Consequences:

- Standard uses the preset Zoom binding in both normal and overlay paths.
- New2 suppresses overlay zoom, leaving only the native setter request.
- New3 is not included in the suppression condition. With both options enabled,
  the overlay can inject the preset Zoom bit in addition to DirectInvocation.

The last case is the current implementation, not an intended isolation
guarantee. Test it explicitly for duplicate or conflicting actions when either
feature changes.

## Method comparison

| Property | Standard | New2 | New3 |
| --- | --- | --- | --- |
| guest sees ordinary bound input | yes | no | no, except current overlay interaction |
| uses active preset binding | yes | no | no |
| calls native setter | no | yes | yes |
| one action per pressed edge | game input semantics | host latch | host latch |
| Alt-Form behaviour | Morph Ball Boost binding | same outer fallback | same outer fallback |
| spawn window | game input | Standard fallback | Standard fallback |
| build availability | all | developer | developer |

## Ownership and evidence

Current melonPrimeDS source is authoritative for selectable values and runtime
behaviour:

- `src/frontend/qt_sdl/Config.cpp`
- `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp`
- `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigConfig.cpp`
- `src/frontend/qt_sdl/MelonPrimeRuntimeConfig.cpp`
- `src/frontend/qt_sdl/MelonPrimeGameInput.cpp`
- `src/frontend/qt_sdl/MelonPrimePatchNativeZoomToggleHook.inc`
- `src/frontend/qt_sdl/MelonPrimePatchDirectInvocationHook.inc`
- `src/frontend/qt_sdl/MelonPrimePatchImmediateInputEdgeOverlay.inc`
- `src/frontend/qt_sdl/MelonPrimeArm9Hook.cpp`

Supporting reverse-engineering context is maintained separately in:

- `C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\Input-Direct-Injection-Investigation\current\summary\Input-Direct-Injection-Consolidated-AllVersions.md`
- `C:\Users\Admin\Documents\git\mphCodex\mphAnalysis\Battle\TouchscreenInput\DirectInvocation\current\Direct-Invocation-Spawn-Freeze-Investigation-JP1_0.md`

Do not copy those investigations here wholesale. They are evidence and design
history; this page records the current melonPrimeDS contract.

## Verification matrix

- Save/reload each value and verify exactly one UI choice remains checked.
- Test all four in-game control presets; Standard must use each preset's Zoom
  binding, not fixed `R`.
- In Alt-Form, hold/release Zoom and verify Morph Ball Boost without a delayed
  scope toggle after unmorphing.
- For New2/New3, press, hold, release, and press again with Imperialist; each
  physical press must toggle once.
- Try Alt-Form, Morphing, Unmorphing, and a non-capable weapon; native scope-on
  must be rejected.
- Switch away while scoped; cleanup must call the native setter to turn scope
  off.
- Press while dead, on the spawn update, throughout spawn invulnerability, and
  one frame after it. Nothing may replay late or freeze the player update.
- Hold Zoom across match start, respawn, focus loss, and method changes.
- Repeat New2 and New3 with Immediate Input Edge Overlay both disabled and
  enabled, recording the current New3 dual-path interaction.
- Verify all ROM guards before treating hook installation as behavioural proof.
