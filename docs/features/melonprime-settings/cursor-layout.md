# Cursor containment and top-screen-only layout

## Controls

These settings are host visual/layout policies, but they use two persistence
surfaces. The bottom-screen clip and in-game top-screen-only layout use
Metroid.Visual.* keys generated from the HUD property schema. The three
stylus-cursor controls use explicit Metroid.Enable.* keys:

| Control | Key | Default | Platform/scope |
| --- | --- | --- | --- |
| Clip cursor to bottom screen when not in game | Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame | false | Windows clipping; host UI |
| In-game top screen only | Metroid.Visual.InGameTopScreenOnly | false | host screen layout |
| Hide stylus cursor in game | Metroid.Enable.stylusHideCursorInGame | false | cursor presentation; see input compatibility |
| Confine stylus cursor to top screen | Metroid.Enable.stylusConfineCursorToTopScreen | false | Windows cursor policy; see stylus cursor policy |
| Hold stylus cursor at center while not clicking | Metroid.Enable.stylusHoldCursorAtCenterWhenNotClicking | false | Host cursor warp while idle; see stylus cursor policy |

## Clip cursor to bottom screen

When enabled and the emulator is not in game, the cursor is clipped to the
visible bottom-screen rectangle. The policy is intended for menu/input use and
must not capture the cursor during active gameplay aim.

The Windows implementation computes the bottom-screen rectangle in the
top-level window's client coordinates and calls ClipCursor. If the rectangle
is invalid, or if a different cursor mode owns capture, it releases the clip
instead of applying a broad fallback.

Pressing Escape releases the clip. A later activation/show/click event can
reacquire it when the setting and state still request clipping. ClipCursor is
process/window behavior; it does not write guest RAM.

Non-Windows platforms use their own cursor policy or do not provide the
Windows ClipCursor operation. Do not describe the setting as a portable
OS-level hard clip without platform evidence.

## In-game top-screen only

When enabled during gameplay, the screen layout is forced to Top Only and the
screen sizing policy is forced to Natural. When the game leaves the in-game
state, the prior screen layout/sizing is restored rather than overwritten by
the temporary in-game policy.

The layout change is applied through the screen/layout callback and does not
patch guest code. It also does not mean that the lower screen's game state
ceases to update; it controls presentation.

When the user manually changes screen layout, the normal layout callback must
coexist with this policy. A test should cover enabling the option, changing
layout in-game, leaving the game, and reopening the settings dialog.

## Cursor presentation versus cursor containment

Hide stylus cursor in game sets the Qt/OS cursor shape to blank during the
relevant stylus gameplay state and restores the arrow when the state ends.
It does not call the global ShowCursor counter. Cursor containment is separate:
one can hide the visual pointer without clipping it, or clip it while still
showing the normal pointer.

This distinction matters when diagnosing “cursor disappeared” reports:

- visual hidden: cursor shape is blank;
- contained: pointer is constrained to a rectangle; and
- captured/aiming: the active input policy may warp or grab the pointer.

The three stylus-mode cursor options have a separate detailed contract:
[stylus-cursor-policy.md](stylus-cursor-policy.md). In particular, top-screen
confinement is match-scoped and mutually exclusive with the bottom-screen
out-of-game clip, while hold-at-center is an idle host warp rather than
continuous aim capture.

## Persistence and save behavior

All settings on this page are host configuration values. They do not affect
guest save data and do not require a ROM revision table. The bottom clip and
in-game top-only layout retain their visual snapshot/invalidation behavior;
the three stylus cursor keys are loaded and refreshed through the screen
cursor policy.

## Verification checklist

- Test cursor clipping in and out of game on Windows.
- Confirm Escape releases the clip and normal focus transitions do not leave a
  stale system clip.
- Test invalid/zero-sized window geometry.
- Test Top Only entering gameplay, leaving, and returning.
- Change manual layout while Top Only is active and confirm restoration.
- Test hidden cursor independently from containment and aim capture.
- Confirm no ARM9 patch or save-data write occurs.

## Evidence and related material

Current source:

- MelonPrimeScreenCursorPolicy.cpp
- MelonPrimeScreenCursorPolicy.h
- MelonPrimeHudPropSchema.inc
- InputConfig/MelonPrimeInputConfig.cpp
- Screen.cpp / screen layout callbacks

Related existing documentation:

- docs/development/ui/settings-and-edit-mode.md
- docs/features/melonprime-settings/input-compatibility.md
