#ifndef MELON_PRIME_HUD_RADAR_H
#define MELON_PRIME_HUD_RADAR_H

#ifdef MELONPRIME_CUSTOM_HUD

// =========================================================================
//  Custom HUD radar source preprocessing.
//
//  The CPU-composited front-ends need the bottom screen keyed down to the
//  radar palette before it can be magnified into the radar circle.  That is
//  a source-preparation responsibility, not a drawing one, and it changes
//  for radar-palette reasons rather than for HUD layout reasons.
// =========================================================================

#include <cstdint>

#include "MelonPrimeHudConfigState.h"

class QImage;

namespace MelonPrime {

    // Radar colour key for the CPU-composited renderer front-ends (software,
    // the Vulkan software path and Metal). The radar magnifies a crop of the
    // bottom screen, so everything that is not a known radar palette colour has
    // to be keyed out first; passing the raw bottom screen shows the whole map
    // area inside the radar circle instead of just the blips.
    //
    // `scratch` is resized/cleared as needed and only the radar crop is written.
    // Returns `scratch` on success, or nullptr when there is nothing to key.
    // The renderer-side GL and Vulkan presenters do the same filtering on the
    // GPU from MelonPrime::kRadarPaletteColors; keep all four in sync.
    QImage* CustomHud_PrepareRadarColorKeySource(
        const QImage* bottomScreen,
        QImage* scratch,
        uint8_t hunterID,
        int sourceRadius);

    // Cold config-boundary query: radar source crop radius in DS pixels, or 0
    // when the radar overlay is switched off. Feed the result straight to
    // CustomHud_PrepareRadarColorKeySource, which no-ops on 0. Call this when
    // the HUD config epoch changes (CustomHud_GetCacheEpoch), not per frame.
    int CustomHud_ResolveRadarColorKeyRadius(Config::Table& localCfg);

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_RADAR_H
