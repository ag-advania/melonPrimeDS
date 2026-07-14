#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeHudRender.h"
#include "MelonPrimePatchNoHud.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeCompilerHints.h"
#include "MelonPrimeConstants.h"
#include "MelonPrimeZoomStatus.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "Config.h"
#include "toml/toml.hpp"
#include "MelonPrime.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeHudGeometry.h"
#include "MelonPrimePerfProbe.h"
#include "MelonPrimeLocalization.h"
#include "MelonPrimeColorDialogPrefs.h"
#include "MelonPrimeHudPropSchema.inc"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QInputDialog>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstring>

namespace MelonPrime {

// Bottom-screen radar art size in pixels (= SVG viewBox width/height).
static constexpr int kRadarArtSize = 76;

// Cached HUD config structs, loaders, scaling, and anchor recomputation.
#include "MelonPrimeHudRenderConfig.inc"

// Asset, icon, radar-frame, text, and outline caches/helpers.
#include "MelonPrimeHudRenderAssets.inc"

// Battle/match state, frame runtime helpers, hide rules, and NoHUD patching.
#include "MelonPrimeHudRenderRuntime.inc"

// Primitive and element drawing: gauges, HP, weapons, inventory, crosshair.
#include "MelonPrimeHudRenderDraw.inc"

// CustomHud_Render and radar overlay entry points.
#include "MelonPrimeHudRenderMain.inc"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
bool CustomHud_SyncVulkanPatch(
    CustomHudConfigState& hudConfig,
    EmuInstance* emu,
    Config::Table& localCfg,
    const RomAddresses& rom,
    uint8_t playerPosition,
    bool isInGame)
{
    const ScopedHudConfigState active(hudConfig);
    HudRuntimeState state{};
    if (!ReadHudRuntimeBaseState(emu, rom, playerPosition, state))
        return false;

    if (!isInGame || !CustomHud_IsEnabled(localCfg))
    {
        CustomHud_RestoreNativeHudState(rom, state);
        return false;
    }

    uint16_t mask = 0;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Helmet))
        mask |= 1u << NOHUD_HELMET;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Ammo))
        mask |= 1u << NOHUD_AMMO;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_WeaponIcon))
        mask |= 1u << NOHUD_WEAPONICON;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_HP))
        mask |= 1u << NOHUD_HP;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Crosshair))
        mask |= 1u << NOHUD_CROSSHAIR;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScoreBattle))
        mask |= 1u << NOHUD_SCORE_BATTLE;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScoreSurvival))
        mask |= 1u << NOHUD_SCORE_SURVIVAL;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScorePrimeHunter))
        mask |= 1u << NOHUD_SCORE_PRIMEHUNTER;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScoreBounty))
        mask |= 1u << NOHUD_SCORE_BOUNTY;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScoreCapture))
        mask |= 1u << NOHUD_SCORE_CAPTURE;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScoreDefender))
        mask |= 1u << NOHUD_SCORE_DEFENDER;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScoreNode))
        mask |= 1u << NOHUD_SCORE_NODE;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Bomb))
        mask |= 1u << NOHUD_BOMB;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Boost))
        mask |= 1u << NOHUD_BOOST;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Cloak))
        mask |= 1u << NOHUD_CLOAK;
    if (localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_DoubleDamage))
        mask |= 1u << NOHUD_DOUBLE_DAMAGE;

    NoHudPatch_Sync(
        ActiveHudConfigState().noHudPatch,
        state.nds,
        state.romGroup,
        mask);
    CustomHud_MarkNoHudPatchDesired();

    if (!state.isStartPressed && state.currentHP != 0 && !state.isGameOver)
        ReadHudRuntimeAdventurePauseState(rom, state);
    return !ShouldHideForGameplayState(
        state.isStartPressed,
        state.currentHP,
        state.isGameOver,
        state.isAdventure,
        state.isMapOrUserActionPaused);
}
#endif

// =========================================================================
//  P-7: HUD Layout Editor unity fragment.
//  The on-screen editor shares the runtime HUD statics and helpers above.
// =========================================================================
#include "MelonPrimeHudConfigOnScreenUnity.inc"

// Developer-only golden hash harness.
#include "MelonPrimeHudGoldenHarness.inc"

#undef s_cacheEpoch
#undef s_cache

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
