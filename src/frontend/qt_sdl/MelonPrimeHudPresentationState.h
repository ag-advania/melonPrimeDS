#ifndef MELON_PRIME_HUD_PRESENTATION_STATE_H
#define MELON_PRIME_HUD_PRESENTATION_STATE_H

#ifdef MELONPRIME_CUSTOM_HUD

// =========================================================================
//  Custom HUD presentation state query.
//
//  Host-side presentation facts a renderer front-end has to consult to decide
//  what it may draw -- currently just "is the layout editor open".
//
//  This is deliberately separate from MelonPrimeHudEdit.h. The editor header
//  carries <QMouseEvent> and <functional> for mouse routing and the selection
//  callback; a presenter that only asks whether edit mode is active has no
//  business taking on Qt event types. MelonPrimeHudEdit.h includes this header
//  rather than redeclaring the query, so there is exactly one declaration.
// =========================================================================

#include "MelonPrimeHudConfigState.h"

namespace MelonPrime {

    // Returns true while the HUD layout editor is active.
    bool CustomHud_IsEditMode(const CustomHudConfigState& hudConfig);

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_PRESENTATION_STATE_H
