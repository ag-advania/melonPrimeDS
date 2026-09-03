#ifndef MELONPRIME_HUD_SCREEN_EDIT_PANEL_H
#define MELONPRIME_HUD_SCREEN_EDIT_PANEL_H

// On-screen HUD edit panel placement.
//
// Cold GUI-thread geometry: it runs when the editor opens and on host resize /
// move, never from a draw path. It was a unity-build fragment included only by
// Screen.cpp; it is a real function here so the panel's event handlers call a
// declared interface instead of depending on include position.

#ifdef MELONPRIME_CUSTOM_HUD

class QWidget;
class MelonPrimeHudConfigOnScreenEdit;

namespace MelonPrime::HudScreenEditPanel {

// Sizes and positions `panel` against `host`'s window bounds, preferring the
// area beside the top screen. GUI thread only.
//
//   setMaximumHeight  clamp the panel height to the host window (the editor
//                     open path and host resize); false while merely tracking
//                     a host move, which must not re-clamp a user-sized panel.
//   adjustSize        run the extra adjustSize()/constrain pass the editor
//                     open path needs before the panel is first shown.
void Position(QWidget* host,
              MelonPrimeHudConfigOnScreenEdit* panel,
              bool setMaximumHeight,
              bool adjustSize);

} // namespace MelonPrime::HudScreenEditPanel

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELONPRIME_HUD_SCREEN_EDIT_PANEL_H
