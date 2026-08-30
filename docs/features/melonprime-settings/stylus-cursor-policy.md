# Stylus cursor policy

This page owns the three cursor options that are coupled to stylus-mode
gameplay presentation:

- hide the cursor during gameplay;
- confine it to the top-screen rectangle on Windows; and
- hold it at the selected screen center while no stylus click is held.

The related bottom-screen clip and in-game top-screen-only layout settings
remain documented in [cursor-layout.md](cursor-layout.md). The broader
stylus/touch compatibility contract is in
[input-compatibility.md](input-compatibility.md).

## Controls

| UI control | Configuration key | Default | Contract |
| --- | --- | ---: | --- |
| Hide the Cursor During Gameplay Even in Stylus Mode | Metroid.Enable.stylusHideCursorInGame | false | Presentation only; the pointer remains free |
| Confine the Cursor to the Top Screen During a Match in Stylus Mode | Metroid.Enable.stylusConfineCursorToTopScreen | false | Windows OS-level ClipCursor to the rendered top-screen rectangle |
| Hold the Cursor at the Center While No Click Is Held During a Match in Stylus Mode | Metroid.Enable.stylusHoldCursorAtCenterWhenNotClicking | false | Host cursor warp while idle; each drag starts from the selected center |

All three are per-instance configuration values. They are independent of
guest save data, ROM patch words, and the Custom HUD property schema.

## Activation gate

The three options share one match-cursor policy edge. The current source
enables that edge only when all of these are true:

1. at least one of the hide, top-screen confinement, or hold-at-center
   options is enabled;
2. the MelonPrime core state exists;
3. the UI state reports stylus mode;
4. the panel is focused; and
5. the UI state is not in cursor mode.

In shorthand, the active state is:

    (hide OR confine OR hold) AND core-present AND stylus-mode AND focused AND NOT cursor-mode

When the edge changes, updateClipIfNeeded is called once to reconcile the
presentation and platform clip. A normal event path can still re-check the
state, but the cached OR avoids doing cursor work when all three options are
off.

This is a host/UI policy. It does not mean that a touch-screen aim patch,
native aim hook, or virtual stylus binding was installed.

## Hide cursor

When the shared active state is true and the hide option is enabled, the panel
uses a blank Qt/Windows cursor presentation. When the state ends, the arrow
presentation is restored.

The implementation intentionally does not use the process-global
Windows ShowCursor counter. This avoids changing cursor visibility for other
windows or for unrelated parts of the application. Hiding is therefore
visual only:

- it does not capture the pointer;
- it does not clip the pointer;
- it does not warp the pointer;
- it does not change stylus coordinates or aim deltas; and
- it does not write guest RAM or ROM.

## Top-screen confinement

When the shared active state is true and the top-screen option is enabled,
the panel calculates the current rendered top-screen widget rectangle and
passes its screen-space rectangle to Windows ClipCursor.

The rectangle is derived from the actual current screen layout, then
translated from panel/client coordinates to global screen coordinates and
clamped to the virtual desktop. It is recalculated when the cursor policy or
screen layout changes. A missing or invalid rectangle releases the clip
instead of applying a broad fallback rectangle.

The top-screen policy is intentionally different from aim capture:

| Policy | Pointer behavior | Aim capture request | Typical use |
| --- | --- | --- | --- |
| Aim capture | Centered/contained according to the platform aim path | Yes | Mouse-look aim |
| Stylus top-screen confinement | Free movement inside the top-screen rectangle | No | Stylus drag with visible or hidden cursor |
| Bottom-screen out-of-game clip | Free movement inside the bottom-screen rectangle | No | Menu/out-of-game touch use |

On non-Windows platforms this setting does not claim a portable OS-level hard
clip. The current policy can still apply the appropriate cursor shape, but
the Windows ClipCursor operation is not available there. Any platform-specific
pointer lock or grab behavior belongs to the platform input implementation,
not to this setting's documentation.

Top-screen confinement is mutually exclusive with the bottom-screen
out-of-game clip in the policy decision. The bottom clip is considered only
after the match-scoped top-screen policy does not request the pointer.

## Hold at center while not clicking

When the hold-at-center option is enabled, the screen panel keeps the host
pointer parked at the selected screen center whenever the shared stylus
policy is active and no left click is held. This is a host cursor policy, not
a guest-side coordinate reset and not an aim-capture request.

The policy is reconciled at these boundaries:

- when the stylus match policy becomes active;
- when a left-button press begins, so the idle pin is released for the drag;
- on mouse movement while no click is held; and
- immediately after a left-button release clears the held-click latch.

On Windows, the idle branch uses a one-pixel ClipCursor rectangle at the
selected center. This is the OS-level pin that keeps the pointer parked
without per-event warping. A left-button press re-runs the policy and removes
that idle pin; if top-screen confinement is enabled, the held drag is then
confined to the rendered top-screen rectangle instead.

On non-Windows platforms, the idle branch applies the ordinary cursor shape
and the screen panel's fallback parks the pointer with the host cursor-warp
helper when a relevant mouse event arrives. The implementation does not claim
that this is an OS-level hard pin on every window system.

For the fallback warp, if the event position is already the target, no warp is
issued. Otherwise the target is mapped to global screen coordinates and passed
to the host cursor-warp helper. Avoiding a second warp at the target prevents
the application's own warp event from repeatedly re-entering the handler.

The target is selected from the active rendered screen geometry:

- if top-screen confinement is currently active, use the center of the
  rendered top-screen rectangle;
- otherwise, use the center of the rendered bottom-screen rectangle; and
- if a screen rectangle cannot be found, fall back to the panel rectangle
  center.

The purpose is to give every next stylus drag the full host-pointer range
without requiring the user to manually move the cursor back. While a left
click is held, the held-click latch stops the idle pin so the drag can use the
full movement range. The latch is intentionally independent of whether the
press registered a guest touch; a press outside the touch area must still
free the pointer for the drag. The policy does not reset guest touch state,
alter aim sensitivity, or install a guest patch.

Because this is a host warp, the exact visible transition depends on the
platform window system. On Windows the relevant primitive is ClipCursor for
the idle pin, while other platforms use the fallback warp path. It should be
tested as an idle movement, left-press, drag, and release-to-next-drag
sequence, not inferred from the config value alone.

## Combination matrix

| Hide | Top confine | Hold at center | Expected policy |
| ---: | ---: | ---: | --- |
| Off | Off | Off | Ordinary stylus cursor behavior |
| On | Off | Off | Cursor hidden while the shared stylus gameplay gate is active |
| Off | On | Off | Visible cursor confined to the top-screen rectangle on Windows |
| On | On | Off | Hidden cursor confined to the top-screen rectangle on Windows |
| Off | Off | On | While idle, the cursor is held at the bottom-screen rectangle center |
| On | Off | On | Hidden cursor plus bottom-screen idle center hold |
| Off | On | On | Top-screen confinement plus top-screen idle center hold |
| On | On | On | Hidden top-confined cursor plus top-screen idle center hold |

The hold-at-center option participates in the shared OR, so it can activate the
match policy by itself. When it is combined with top-screen confinement, the
idle pin uses the top-screen center and the held drag uses the top-screen
rectangle. It is not a separate post-release-only operation.

## Save and refresh behavior

The settings dialog loads these keys through the non-HUD setting binding table
and saves them with the other per-instance settings. In the current binding
layout they are appended as entries 51, 52, and 53 after top-screen touch and
touch-screen aim only.

After Save/OK, each open screen panel reloads the three values. The panel
recomputes the shared active edge and calls updateClipIfNeeded so a changed
hide, confinement, or hold option takes effect without restarting the
emulator. If the policy is active, the refresh also applies the idle center
rule to the current pointer position; it does not synthesize a click or guest
touch event.

The configuration defaults are declared in
src/frontend/qt_sdl/Config.cpp and the key constants are in
src/frontend/qt_sdl/MelonPrimeDef.h. The UI and binding order are in
src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.ui and
src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp.

## State transitions to test

| Transition | Expected result |
| --- | --- |
| Stylus off to on while focused | Shared policy becomes active if any of the three options is enabled |
| Focused to unfocused | Platform clip is suspended; persistent request is not accidentally erased |
| Gameplay/cursor mode transition | The shared stylus policy stops when its cursor-mode/focus gate is inactive |
| Top-only layout or window resize | Clip rectangle follows the new rendered top-screen geometry |
| Top rectangle unavailable | No broad fallback clip; pointer policy releases safely |
| Mouse move with no click held | Enabled hold-at-center policy warps to the selected center when needed |
| Left press after an idle pin | Idle pin is released; top confinement is re-decided for the held drag |
| Left release after a real or unregistered drag | Held-click latch clears, then the idle center policy is applied |
| Right-button release | Does not clear the left-click latch or re-decide the idle pin |
| Stylus off before release | No stylus match center hold; active latch is cleared with the policy |
| Settings Save/OK | New values reach every open panel |
| Settings Cancel | Do not assume an already committed non-visual side effect is rolled back |

## Boundaries and common mistakes

- “Hide” and “confine” are separate. A hidden pointer can still leave the
  window; a visible pointer can be confined to the top screen.
- Top-screen confinement is not aim capture. It must not reset or center the
  pointer during every aim update.
- Hold-at-center is not a sensitivity setting. It changes the host cursor
  position only while the stylus is idle; the held-click guard leaves an
  active drag alone.
- Top-screen touch and touch-screen aim only are separate input-routing
  features. Neither is implied by these cursor options.
- The bottom-screen clip remains an out-of-game policy. Do not reuse its
  setting key to describe the match-scoped top-screen behavior.
- A static source check can prove the gate, event boundaries, and platform
  branch. It cannot prove that every window manager honors the requested clip
  or warp.

## Source and research ownership

Current implementation:

- src/frontend/qt_sdl/Screen.cpp
- src/frontend/qt_sdl/Screen.h
- src/frontend/qt_sdl/MelonPrimeScreenCursorPolicy.cpp
- src/frontend/qt_sdl/MelonPrimeScreenCursorPolicy.h
- src/frontend/qt_sdl/MelonPrimePlatformInput.h
- src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp
- src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigConfig.cpp

The relevant existing repository references are
[aim-input.md](../../architecture/input/aim-input.md) and
[cursor-layout.md](cursor-layout.md). This host cursor contract has no
guest-address table; the current melonPrimeDS source is the authority rather
than a copied mphCodex note.

## Verification checklist

- Test all eight combinations in the matrix on Windows.
- Test focus loss, alt-tab, close, and window movement across monitors.
- Test top-screen, dual-screen, swapped, rotated, and resized layouts.
- Confirm the pointer remains usable when hide is off and top confinement is
  on.
- Confirm aim capture still uses its own path and is not affected by stylus
  top confinement.
- Confirm idle movement and post-release movement return the pointer to the
  selected center when the hold option is enabled.
- On Windows, confirm the idle pointer is pinned by the one-pixel clip and
  that a left press frees it for a drag.
- Confirm a left press outside the touch area still frees the idle pin.
- Confirm a held stylus drag is not continuously warped back to the center.
- Confirm the next stylus drag starts with the expected full range.
- Test non-Windows builds as “no Windows ClipCursor guarantee,” not as a
  silent pass for OS-level confinement.
- Separate source/static, build, and physical input results in reports.
