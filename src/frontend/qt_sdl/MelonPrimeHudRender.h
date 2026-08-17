#ifndef MELON_PRIME_HUD_RENDER_H
#define MELON_PRIME_HUD_RENDER_H

#ifdef MELONPRIME_CUSTOM_HUD

#include <cstdint>
#include <functional>
#include <memory>
#include <QMouseEvent>
#include <QRect>
#include "MelonPrimeDef.h"

class EmuInstance;
class QPainter;
class QImage;
class QFont;
class QString;
namespace Config { class Table; }

namespace MelonPrime {

    struct RomAddresses;
    struct GameAddressesHot;
    struct CustomHudConfigState;

    std::shared_ptr<CustomHudConfigState> CustomHud_CreateConfigState();

    // =========================================================================
    //  CustomHud_Render
    //
    //  Main entry point — call once per frame from the OSD/overlay render path.
    //  Draws HP, weapon icon + ammo count, and crosshair onto the overlay.
    //
    //  Parameters:
    //    emu            — EmuInstance for NDS memory access
    //    localCfg       — config table (reads Metroid.Visual.CustomHUD,
    //                     Metroid.Visual.CrosshairSize)
    //    rom            — ROM address table (resolved for current ROM)
    //    addrHot        — player-position-adjusted hot addresses
    //    playerPosition — current player position index (for +0xF30 offset)
    //    topPaint       — QPainter for the top-screen overlay
    //    btmPaint       — QPainter for the bottom-screen overlay
    //    topBuffer      — QImage backing the top overlay (cleared inside)
    //    btmBuffer      — QImage backing the bottom overlay (cleared inside)
    //    isInGame      — whether the game is currently in a match
    //    topStretchX    — widescreen X stretch factor (1.0=4:3, >1.0=wide)
    //    hudOriginXds   — left black-bar width in DS units (m_hudOriginX / hudScale).
    //                     Non-zero when game content is pillarboxed inside the window.
    //                     Shifts the painter so DS x=0 maps to the left game edge,
    //                     allowing elements at DS x<0 or x>256 to appear in black bars.
    //    hudOriginYds   — top black-bar height in DS units (m_hudOriginY / hudScale).
    // =========================================================================
    // Returns the dirty pixel rect of everything rendered into the overlay (in overlay space).
    // Screen.cpp uses this to limit the GPU texture upload and overlay clear to the HUD region.
    // Returns an empty QRect if nothing was drawn.
    QRect CustomHud_Render(
        CustomHudConfigState& hudConfig,
        EmuInstance* emu,
        Config::Table& localCfg,
        const RomAddresses& rom,
        const GameAddressesHot& addrHot,
        uint8_t playerPosition,
        QPainter* topPaint,
        QPainter* btmPaint,
        QImage* topBuffer,
        QImage* btmBuffer,
        bool isInGame,
        float topStretchX = 1.0f,
        float hudScale = 1.0f,
        float hudOriginXds = 0.0f,
        float hudOriginYds = 0.0f
    );

    // Returns true if the custom HUD setting is enabled in config.
    bool CustomHud_IsEnabled(Config::Table& localCfg);

    // Resolve the base HUD font (family + style strategy only; caller sets pixel size)
    // from the Metroid.Visual.HudFont* config keys:
    //   HudFontMode 0 = bundled MPH (default), 1 = system font family, 2 = font file.
    // Falls back to the bundled MPH font on empty/invalid selection.
    QFont CustomHud_ResolveBaseFont(Config::Table& localCfg);

    // Resolve the base render pixel size for the HUD font:
    //   mode 0 (MPH)  -> kCustomHudFontSize (6, the pixel font's native size).
    //   mode 1/2      -> Metroid.Visual.HudFontSize (clamped), so system/file fonts can
    //                    be rasterised larger and sharper. HudTextScale/auto-scale still apply.
    int CustomHud_ResolveFontPixelSize(Config::Table& localCfg);

    // Returns true when the shared HUD hide condition is active.
    bool CustomHud_ShouldHideForGameplayState(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition);

    // Returns true when the radar overlay should be drawn on the top screen.
    bool CustomHud_ShouldDrawRadarOverlay(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition);

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

    // Cold config-boundary query used to cache whether Vulkan should mask the
    // native helmet BG1-3 layers. Never call this from a per-frame hot path.
    bool CustomHud_IsHelmetLayerHideConfigured(Config::Table& localCfg);

    // Per-frame, before RunFrame: keep the native helmet layers off across the
    // spawn window. No-op unless the helmet hide patch is currently applied;
    // native UI frames are left untouched by this RAM/register clamp.
    void CustomHud_ClampHelmetLayersPreFrame(
        CustomHudConfigState& hudConfig,
        EmuInstance* emu,
        const RomAddresses& rom,
        uint8_t playerPosition);

    // Ensure the no-HUD patch is reverted when custom HUD is disabled.
    // Call every frame from Screen.cpp even when the HUD overlay is not rendered.
    void CustomHud_EnsurePatchRestored(
        CustomHudConfigState& hudConfig,
        EmuInstance* emu,
        Config::Table& localCfg,
        const RomAddresses& rom,
        uint8_t playerPosition,
        bool isInGame
    );

    // Reset patch tracking state (call on emu stop/reset).
    void CustomHud_ResetPatchState(CustomHudConfigState& hudConfig);

    // Reconcile host-side patch tracking after savestate RAM replacement.
    // Every native-HUD instruction is immediately rewritten from the current
    // configuration, independent of the settings used to save the state.
    void CustomHud_ReconcilePatchAfterSavestateLoad(
        CustomHudConfigState& hudConfig,
        EmuInstance* emu,
        Config::Table& localCfg,
        const RomAddresses& rom,
        uint8_t playerPosition);

    // Invalidate cached config (call when settings are saved).
    void CustomHud_InvalidateConfigCache(CustomHudConfigState& hudConfig);

    // Returns the current config cache generation counter.
    // Incremented every time the config cache is refreshed.
    // Screen.cpp uses this to skip re-reading config per-frame.
    uint32_t CustomHud_GetCacheEpoch(const CustomHudConfigState& hudConfig);

    // Generation for emulation-state transitions that can reuse the same
    // NDS::NumFrames value (match join, reset, ROM/savestate replacement).
    uint32_t CustomHud_GetVisualGeneration(const CustomHudConfigState& hudConfig);

    // Lightweight presentation identity used by renderer front-ends to skip
    // rebuilding an unchanged overlay.  The returned frame is the emulated
    // NDS frame, while `ndsIdentity` receives the instance identity pointer.
    uint32_t CustomHud_GetVisualGameFrame(EmuInstance* emu,
                                          const void** ndsIdentity);

    // Cache battle settings at match join (call from HandleGameJoinInit).
    void CustomHud_OnMatchJoin(CustomHudConfigState& hudConfig, uint8_t* ram, const RomAddresses& rom);

    // =========================================================================
    //  DrawBottomScreenOverlay
    //
    //  Render a region of the bottom DS screen onto the top-screen overlay.
    //  Controlled by Metroid.Visual.BtmOverlay* config keys.
    //
    //  Parameters:
    //    localCfg  — config table
    //    topPaint  — QPainter for the top-screen overlay
    //    btmBuffer — QImage of bottom screen (256x192 ARGB)
    //    hunterID  — current player character (MelonPrime::HunterId ordering)
    // =========================================================================
    void DrawBottomScreenOverlay(Config::Table& localCfg, QPainter* topPaint, QImage* btmBuffer, uint8_t hunterID);

    // =========================================================================
    //  HUD Layout Editor
    // =========================================================================

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

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    // Developer-only CLI hook: render deterministic HUD cases and write hashes.
    int CustomHud_RunGoldenHarness(const QString& outputPath);
#endif

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_RENDER_H
