#ifndef MELON_PRIME_HUD_EDIT_H
#define MELON_PRIME_HUD_EDIT_H

#ifdef MELONPRIME_CUSTOM_HUD

// =========================================================================
//  HUD Layout Editor
//
//  On-screen edit session lifecycle, mouse routing and selection.  This is
//  the only Custom HUD header that needs Qt event types; renderer front-ends
//  that merely ask CustomHud_IsEditMode() still include it, but nothing that
//  only draws or only patches has to.
// =========================================================================

#include <functional>
#include <QMouseEvent>

#include "MelonPrimeDef.h"
#include "MelonPrimeHudConfigState.h"

class EmuInstance;

namespace MelonPrime {

    // Enter interactive HUD layout editing mode. Frees the cursor and draws
    // draggable element boxes on the top screen. Call from the UI thread only.
    void CustomHud_EnterEditMode(CustomHudConfigState& hudConfig, EmuInstance* emu, Config::Table& cfg);

    // Commit (save=true) or discard (save=false) changes and leave edit mode.
    void CustomHud_ExitEditMode(CustomHudConfigState& hudConfig, bool save, Config::Table& cfg);

    // Returns true while the HUD layout editor is active.
    bool CustomHud_IsEditMode(const CustomHudConfigState& hudConfig);

    // Returns the style captured when the current HUD layout edit session opened.
    // The value is resolved once per edit session, not from the renderer hot path.
    OnScreenEditStyle CustomHud_GetOnScreenEditStyle(const CustomHudConfigState& hudConfig);

    // Crosshair remains on its dedicated editor path regardless of the generic
    // Classic/Retro preference.
    bool CustomHud_IsCrosshairElement(int elementIndex);

    // Update the coordinate context used by mouse handlers (call each render).
    void CustomHud_UpdateEditContext(CustomHudConfigState& hudConfig,
                                     float originX, float originY,
                                     float hudScale, float topStretchX);

    // Forward mouse events from the screen panel to the layout editor.
    void CustomHud_EditMousePress  (CustomHudConfigState& hudConfig, QPointF pt, Qt::MouseButton btn, Config::Table& cfg);
    void CustomHud_EditMouseMove   (CustomHudConfigState& hudConfig, QPointF pt, Config::Table& cfg);
    void CustomHud_EditMouseRelease(CustomHudConfigState& hudConfig, QPointF pt, Qt::MouseButton btn, Config::Table& cfg);
    void CustomHud_EditMouseWheel(CustomHudConfigState& hudConfig, QPointF pt, int delta, Config::Table& cfg);

    // Register a callback invoked whenever the selected element changes in edit mode.
    // Pass nullptr to clear. The int argument is the element index (-1 = none).
    void CustomHud_SetEditSelectionCallback(CustomHudConfigState& hudConfig, std::function<void(int)> cb);

    // Returns the currently selected element index (-1 = none).
    int  CustomHud_GetSelectedElement(const CustomHudConfigState& hudConfig);

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_EDIT_H
