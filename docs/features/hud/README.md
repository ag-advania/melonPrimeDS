# Custom HUD documentation

This directory is the user-facing route for the Custom HUD. It deliberately
does not reproduce the generated property table or the full runtime source
reference.

## Entry points

| Question | Document |
| --- | --- |
| How do I configure, preview, import, export, or edit the HUD? | [Custom HUD settings guide](custom-hud-settings.md) |
| How does the editor and dialog work internally? | [Settings UI and edit mode](../../development/ui/settings-and-edit-mode.md) |
| What are every generated HUD property, default, type, and surface? | [Generated HUD property schema](../../generated/hud/MelonPrimeHudPropSchemaPhase2a.md) |
| How is the HUD loaded and rendered at runtime? | [Custom HUD runtime](../../development/hud/custom-hud-runtime.md) |

## Focused behavior references

- [Zoom crosshair](../custom-hud-zoom-crosshair.md)
- [Color-dialog preferences](../custom-hud-color-dialog-prefs.md)
- [Adventure camera scenes](custom-hud-adventure-camera-scene.md)
- [Adventure Scan Visor](custom-hud-adventure-scan-visor.md)
- [Enemy Target](custom-hud-enemy-target.md)
- [Scoreboard and per-player hunter colors](custom-hud-scoreboard.md)
- [Helmet spawn flash](custom-hud-helmet-spawn-flash.md)

The focused pages own behavior that needs its own runtime or rendering
explanation. The settings guide owns the common workflow and tells readers
which page is authoritative for a particular question.

## Change boundary

Add a new visual property to
src/frontend/qt_sdl/MelonPrimeHudPropSchema.inc first, regenerate the derived
dialog/edit-mode/schema outputs, and then update the guide only when the user
workflow or section map changes. Do not hand-copy generated rows into this
directory.
