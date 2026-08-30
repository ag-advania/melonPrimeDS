# Input method selectors

## Controls

The Input Method section selects how weapon switching, biped firing, transform,
and zoom requests are generated:

| Control | Key | Default | Values |
| --- | --- | --- | --- |
| Weapon switch method | Metroid.Input.WeaponSwitchMethod | 0 | 0 legacy, 1 native, 2 native 2 |
| Biped fire method | Metroid.Input.BipedFireMethod | 0 | 0 legacy, 1 native |
| Transform method | Metroid.Input.AltFormTransformMethod | 0 | 0 legacy, 1 native gate, 2 native 2 |
| Transform method (legacy key) | Metroid.Input.Enable.DirectAltFormTransform | false | mirrors value 1 of the key above |
| Zoom method | Metroid.Input.ZoomMethod | 0 | 0 legacy, 1 retired alias, 2 native, 3 native 3 |

Each domain stores one integer, so its methods are mutually exclusive by
construction, and value 0 -- the behaviour you get when no new method is
selected -- is the **Standard Method**. It has its own checkbox rather than
being "nothing ticked", so the selector reads as exactly-one-of-N:

| Domain | Standard | New | New 2 | New 3 |
| --- | --- | --- | --- | --- |
| Weapon change | value 0 | value 1 | value 2 | — |
| Alt-Form transform | value 0 | value 1 | value 2 | — |
| Zoom | value 0 | — | value 2 | value 3 |

The dialog enforces the grouping directly: ticking one box clears the others in
its domain, and clearing the last ticked box puts it back rather than leaving
the group empty. The boxes stay checkboxes to match the rest of the dialog, but
they behave as a radio group. The Standard zoom box is available in every
build; only the experimental New Method 2 and New Method 3 boxes are
developer-build only. A release build therefore always runs the Standard zoom
path.

`Metroid.Input.AltFormTransformMethod` is authoritative. When it is absent, the
value is migrated once from the older boolean key: true becomes 1, false becomes
0. Saving writes both, so a build without Method 2 still resolves Method 1 or
legacy correctly.

The controls select paths; they do not change the keyboard bindings themselves.
The existing [Zoom input methods](../input/zoom-input-methods.md) page contains
the longer zoom design and test history.

## Shared action-consumer boundary

The native weapon, biped-fire, and zoom paths integrate at the guest's shared
Action Consumer. Its ROM-specific PC is:

| ROM | Action Consumer PC |
| --- | ---: |
| JP1.0 / JP1.1 | 0x02024174 |
| US1.0 / US1.1 | 0x02024198 |
| EU1.0 / EU1.1 | 0x02024190 / 0x02024198 |
| KR1.0 | 0x0200F6DC |

The hook dispatcher also shares the post-poll player input update for
immediate edges. This shared boundary is why enabling multiple native
methods must be tested for duplicate dispatch. The source owns the
single-dispatch rule.

## Weapon switch

Value 0 retains the legacy touch/menu-driven path. Value 1 hooks the native
TryEquipWeapon path. The native path uses a trampoline at 0x02003EA0 and a
scratch area at 0x02003EE0.

| ROM | Hook site | TryEquipWeapon target |
| --- | ---: | ---: |
| JP1.0 / JP1.1 | 0x02026BFC | 0x0200C5FC |
| US1.0 | 0x02026C20 | 0x0200C5FC |
| US1.1 | 0x02026C20 | 0x0200C5FC |
| EU1.0 | 0x02026C18 | 0x0200C600 |
| EU1.1 | 0x02026C20 | 0x0200C5FC |
| KR1.0 | 0x0200C29C | 0x02025DBC |

The US revisions currently share both values shown above. The complete expected
BL words and continuation values remain in
MelonPrimePatchWeaponSwitchHook.inc. Apply validates the expected call before
installing the match-scoped hook.

Acceptance requires that a native switch produces one guest equip request,
does not bypass ammo/weapon validity, and restores the legacy instruction
after leave/stop.

## Biped fire

Value 0 uses the legacy input path. Value 1 consumes a native fire edge at the
shared action boundary. The helper sets the fire result true for the native
edge; guest cooldown, ammo, projectile creation, HUD, and sound remain owned
by the game.

This is not a “fire every frame” patch. Test press, hold, release, cooldown,
empty ammo, weapon changes, and pause/menu transitions.

## Transform

When Direct Alt-form Transform is false, transform continues through the
legacy simulated input path. When true, the native transform gate hooks the
guest TransformRequest condition/call pair.

| ROM | Compare site | Transform call site |
| --- | ---: | ---: |
| JP1.0 / JP1.1 | 0x02023B3C / 0x02023B74 | 0x02025F94 / 0x02025FCC |
| US1.0 / US1.1 | 0x02023B60 / 0x02023B98 | 0x02025FB8 / 0x02025FF0 |
| EU1.0 / EU1.1 | 0x02023B58 / 0x02023B90 | 0x02025FB0 / 0x02025FE8 |
| KR1.0 | 0x02011598 / 0x020115D0 | 0x0200EE54 / 0x0200EE8C |

The paired values in the source table are alternatives within the
version-specific control flow, not a license to patch both regions blindly.
Guard failure must leave the legacy path intact.

## New Method 2 (DirectInvocation)

Value 2 of the weapon and transform selectors is a separate path shared by both.
Value 3 of the zoom selector uses the same path. It follows the mphCodex
DirectInvocation specification: a host pressed edge writes a guest mailbox, and
the request is consumed inside the game's own player input update, at the same `ProcessTouchInput` call site the Method-1 weapon hook
uses. The trampoline runs the original callee first and only then makes the
native call, so the frame's existing touch/aim processing is preserved.

| ROM | Hook site | ProcessTouchInput | Transform | TryEquipWeapon | HUD dispatch |
| --- | ---: | ---: | ---: | ---: | ---: |
| JP1.0 / JP1.1 | 0x020263DC | 0x02026BFC | 0x02016338 | 0x0200C5FC | 0x0202D06C |
| US1.0 | 0x02026400 | 0x02026C20 | 0x02016358 | 0x0200C5FC | 0x0202D090 |
| US1.1 | 0x02026400 | 0x02026C20 | 0x0201635C | 0x0200C5FC | 0x0202D090 |
| EU1.0 | 0x020263F8 | 0x02026C18 | 0x02016350 | 0x0200C600 | 0x0202D088 |
| EU1.1 | 0x02026400 | 0x02026C20 | 0x0201635C | 0x0200C5FC | 0x0202D090 |
| KR1.0 | 0x0200CF1C | 0x0200C29C | 0x0201C408 | 0x02025DBC | 0x02035EAC |

Transform calls the native request with force = 0 and, only when the call is
accepted and the Morphing bit 0x800 is set afterwards, raises HUD action 0x16 —
the same condition the touch path uses, so an Unmorph raises nothing. Weapon
calls TryEquipWeapon with flags = 0.

The path never synthesises a touch, never runs the touch hit test, never sets or
clears NoAimInput (player+0x4C4 bit 0x01000000), never opens the radial menu, and
never writes CurrentWeapon or the form bit directly. Requests are per pressed
edge with a short frame TTL; they are dropped rather than replayed after focus
loss, leaving the match, or a lifecycle reset.

Known divergence from the specification: the quick-slot "same weapon requested"
branch, which raises HUD action 0x3C without re-equipping, is not reproduced.
`MelonPrimeCore::SwitchWeapon` returns before queuing when the requested weapon
is already held, so the guest never receives that request.

### Lifecycle

Like every other native path, this one is match-scoped and follows the same
three conventions:

- **The battle-runtime latch gates it end to end.** Hooks are installed on the
  first frame where the local player is in play (HP != 0) after
  `mode == MODE_BATTLE_RUNTIME && flow == FLOW_ACTIVE_MATCH` has been seen, so
  requests are neither queued nor serviced outside it. Without that gate a
  press during the join/countdown could be held by its TTL and then fire on the
  very first battle-runtime frame. `HandleBattleRuntimeEnter` also drops any
  pending request outright.
- **The trampoline is authored on the cold match boundary**, from
  `ApplyOnBattleRuntimeEnter`, exactly as the Method-1 weapon trampoline is.
  The first dispatch must not have to write 27 words of guest code, and
  invalidate the JIT blocks covering them, from inside the hook callback.
- **The weapon path keeps the Method-1 spawn-window guard.** While spawn
  invincibility is still counting down the native equip is unsafe, so that one
  request goes to the legacy route instead.

Method 2 and the Method-1 weapon hook share one hook PC. When both are enabled
and both have a pending request on the same frame, Method 1 dispatches and the
Method-2 request is retried on the next frame within its TTL.

### Zoom (New Method 3)

Value 3 of the zoom selector routes through the same DirectInvocation
dispatcher. It calls the same `SetPlayerScopeZoom(player, enabled)` the native
toggle uses, but from the player input update, and it first applies the gates
the bottom-screen touch shortcut applies:

- reject while `player+0x4C4` has bit 9 (Alt-Form), bit 11 (Morphing), or
  bit 12 (Unmorphing) set
- require the currently equipped weapon (`player+0x858`) to have
  `WeaponData+0x08` bit 11 set

The current state is `player+0x850` bit 0, and a press toggles it. The
standalone request has no tapped quick slot, so the capability gate reads the
equipped weapon rather than a slot weapon; this matches the non-touch zoom input
path the ROM already has.

| ROM | SetPlayerScopeZoom |
| --- | ---: |
| JP1.0 / JP1.1 | 0x02015C98 |
| US1.0 | 0x02015CB8 |
| US1.1 | 0x02015CBC |
| EU1.0 | 0x02015CB0 |
| EU1.1 | 0x02015CBC |
| KR1.0 | 0x0201CEBC |

The setter owns the zoom sound and the Imperialist crosshair action, so nothing
here dispatches either separately. As with Method 2, a scope left on when the
equipped weapon stops being zoom-capable is turned off through the same setter;
that cleanup request skips the capability gate on purpose.

Gates are evaluated inside the hook, at the guest safe point, not when the
request is queued, so the state the setter acts on is the state that was
checked.

## ROM code cave reservations

The native paths that call a guest function place a trampoline in the shared zero-filled cave at
0x02003E9C..0x02003FBB. The reservation table, the compile-time overlap rule, the ban on
PC-relative request loads, and the battle-runtime authoring rule are canonical in
[patch-system.md](../../architecture/gameplay/patch-system.md) under "Guest trampolines and the
shared ROM code cave" — read that before adding or moving one. DirectInvocation owns
0x02003F50..0x02003FBB and no data block: its dispatcher hands the request over in r0-r2.

## Zoom

Value 0 is the Standard zoom path. Value 1 is a retired configuration value that
behaves as value 0. Value 2 uses the native SetPlayerScopeZoom path from the
weapon action update. Value 3 is New Method 3: the same setter called from the
player input update, described under DirectInvocation above.

| ROM | SetPlayerScopeZoom site |
| --- | ---: |
| JP1.0 / JP1.1 | 0x02015C98 |
| US1.0 | 0x02015CB8 |
| US1.1 | 0x02015CBC |
| EU1.0 | 0x02015CB0 |
| EU1.1 | 0x02015CBC |
| KR1.0 | 0x0201CEBC |

The native weapon-action path uses the shared action consumer for JP/US/EU
and 0x0200D07C for KR, with a trampoline at 0x02003F00 and scratch area at
0x02003F40. The activation edge, guest scope call, and release behavior must
be tested independently from zoom sensitivity scaling.

## Native methods are gated on the local player being in play

Every native method -- weapon New / New 2, transform New / New 2, zoom New 2 /
New 3 -- refuses to fire while the local player's HP is 0. That covers two
states, not just one: killed and waiting to respawn, and not yet spawned after
the match starts. A native call reaches past whatever the game does with a
player who is not in play, so the request is dropped rather than deferred: it is
not queued, and a request already queued when the player goes down is cleared
instead of firing on respawn.

The gate is applied twice on purpose, at the host queue site and again in the
ARM9 dispatch, because the queue-to-dispatch window is several frames wide and
the player can leave play inside it.

Standard Method is unaffected. It is the game reacting to the emulator's own
simulated touch/menu input, so the game's own handling of a dead player already
applies.

HP is read through the local player's cached pointer; an unresolved pointer
counts as alive, so a native path is never disabled just because the pointer
cache has not been rebuilt yet.

## Spawn barrier

The battle-runtime latch is a *match* boundary; respawn is a *player* boundary,
and the two are not the same. Spawn restores HP early but keeps initialising
camera, model, animation, gun and HUD state well past that point, and the same
player runtime update then falls through to the player input update and its
hook sites. So "HP is not 0 and the hook was reached" is satisfied inside the
very update that spawned the player, and a native call made there lands on
half-initialised state.

Every native method therefore drops its request on the first input hook of that
update. The boundary needs no host latch: Spawn stores the hunter's configured
invulnerability into `player+0xE1`, and the same update decrements it exactly
once before reaching the input code, so on that hook and only there

```text
player+0xE1 == (uint8_t)([player+0x404] + 0xE2) - 1
```

The next update reads `configured - 2`, so this costs one input frame rather
than the whole invulnerability window — a native transform or zoom is available
again immediately after. The request is dropped rather than deferred, because
replaying a pressed edge from before the respawn is the behaviour to avoid.

Structure offsets are identical on all seven ROMs, so this needs no per-version
address table.

This barrier is the only spawn guard the native methods have. Both weapon
methods previously refused the whole `player+0xE1 != 0` window on the producer
side and rerouted to the legacy path; that was wider than the ROM's own
boundary and silently swapped method for the duration, so it was replaced by
this shared one. The mphCodex investigation recommends keeping the wider
weapon-only guard as a safety margin, so if a weapon switch during spawn
invulnerability turns out to still be unsafe, restoring a full-window refusal
for that path is the documented fallback.

Evidence: mphCodex `Direct-Invocation-Spawn-Freeze-Investigation-JP1_0.md`.

## Lifecycle and interactions

Native hooks are match-scoped. Configuration changes are consumed by
NotifyConfigChanged and reconciled by the ARM9 hook installer. A hook being
installed is not proof that the input edge reached the guest; inspect the
behavioral path as well.

Stylus mode can intentionally bypass native aim-related controls. Joy2Key and
SnapTap can change the host edge sequence before the native hook sees it.
Immediate Input Edge Overlay is a developer diagnostic that shares a post-poll
boundary and must not create duplicate fire/zoom/transform actions.

## Verification checklist

- Test each selector with the other selectors at their defaults.
- Test combinations of native weapon/zoom/fire and transform.
- Test Method 2 for weapon and transform separately, then together, then
  combined with Method 1 on the other selector (shared hook PC).
- Verify each pair is exclusive in the dialog and after a save/reload cycle.
- Die, then press each bound action while down: no weapon change, transform or
  zoom may occur, and none may fire late on respawn.
- Press each bound action on the exact spawn/respawn frame: dropped, no freeze.
- Press each bound action on the frame after: the native path runs normally.
- Hold an action across match start and across a respawn: no late replay.
- For zoom Method 3, test Alt-Form, mid-transform, and a non-zoom weapon; each
  must refuse the toggle rather than zoom.
- Verify one action per physical edge, not one action per frame.
- Test cooldown, ammo, invalid weapon, pause, menu, morph, and respawn paths.
- Verify per-ROM guards and leave/stop restoration.
- Test with Joy2Key, SnapTap, and stylus settings recorded.
- For zoom, separate activation method from zoom sensitivity scale.

## Evidence and related material

Current source:

- MelonPrimePatchWeaponSwitchHook.inc
- MelonPrimePatchNativeBipedFireHook.inc
- MelonPrimePatchImmediateTransformGateHook.inc
- MelonPrimePatchDirectInvocationHook.inc
- MelonPrimePatchNativeZoomToggleHook.inc
- MelonPrimeArm9Hook.cpp

Detailed existing docs:

- docs/features/input/zoom-input-methods.md
- docs/architecture/gameplay/patches/immediate-input-edge-overlay.md
- docs/architecture/input/aim-input.md

Supporting reverse-engineering material:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\Input-Direct-Injection-Investigation\current\summary\Input-Direct-Injection-Consolidated-AllVersions.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\CallingRawFunctions\changeWeapon\current\README.md
