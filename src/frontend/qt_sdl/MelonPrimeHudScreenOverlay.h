#ifndef MELONPRIME_HUD_SCREEN_OVERLAY_H
#define MELONPRIME_HUD_SCREEN_OVERLAY_H

// Custom HUD overlay helpers shared by the screen-panel presenters.
//
// These were unity-build fragments included only by Screen.cpp. They take
// every input as a parameter and touch no panel-local state, so they are a
// real module rather than a textual fragment: each presenter translation unit
// (Screen.cpp's Software/OpenGL panels, MelonPrimeScreenDX12.cpp) includes
// this header directly instead of depending on include position inside one
// giant TU.
//
// Everything here is `inline` and parameter-driven. The bodies are unchanged
// from the fragment they replace, so the compiler still inlines them into the
// same call sites; nothing was added to the HUD draw path.
//
// Thread: the caller's presentation thread. These functions read HUD config
// state and rasterize into a caller-owned QImage; they perform no QWidget
// mutation and take no locks of their own.

#ifdef MELONPRIME_CUSTOM_HUD

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <QFont>
#include <QImage>
#include <QPainter>
#include <QRect>

#include <cmath>

#include "Config.h"
#include "EmuInstance.h"
#include "MelonPrimeHudConfigState.h"
#include "MelonPrimeHudDirtyRegions.h"
#include "MelonPrimeHudPatchLifecycle.h"
#include "MelonPrimeHudRender.h"
#include "MelonPrimePerfProbe.h"
// Canonical Custom HUD config keys. Included rather than mirrored as string
// literals, per the config-key ownership rule; this header is registered in
// tools/ci/audits/check-inc-ownership.ps1's multi-parent map alongside the
// other consumers.
#include "MelonPrimeHudPropSchema.inc"

// Make sure the retained overlay exists at the requested size.
//
// Clearing is deliberately *not* done here any more. The Custom HUD renderer
// owns the retained image: it is the only place that knows which regions are
// about to be redrawn, and clearing a bounding box here would throw away the
// pixels of every element that did not change. All this does is allocate.
//
// Returns true when the image had to be created or resized, which means every
// retained pixel -- and every per-element bound that described it -- is gone
// and the next render must start from a full recompose.
inline bool MelonPrimeHud_EnsureTopOverlay(QImage& overlay, int outW, int outH)
{
    if (overlay.width() == outW
        && overlay.height() == outH
        && overlay.format() == QImage::Format_ARGB32_Premultiplied)
        return false;

    MelonPrimePerf::ScopedHudPhase clearTimer(MelonPrimePerf::HudPhase::Clear);
    overlay = QImage(outW, outH, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    return true;
}

// Blit a region set out of the retained overlay onto a presentation painter.
// Used by the presenters that composite on the CPU (native Software, and the
// DX12 panel's retained HUD image).
inline void MelonPrimeHud_CompositeRegions(
    QPainter& painter,
    const QImage& overlay,
    const HudDirtyRegionSet& regions)
{
    for (int i = 0; i < regions.Count(); ++i) {
        const QRect region =
            regions.Region(i) & QRect(0, 0, overlay.width(), overlay.height());
        if (region.isEmpty())
            continue;
        const std::uint64_t pixels =
            static_cast<std::uint64_t>(region.width())
            * static_cast<std::uint64_t>(region.height());
        MelonPrimePerf::AddHudOverlayComposite(pixels * sizeof(QRgb), pixels);
        painter.drawImage(QPoint(region.x(), region.y()), overlay, region);
    }
}

// Translate a logical, top-left-origin overlay rectangle into an OpenGL
// scissor box: physical framebuffer pixels, bottom-left origin.
//
// The edges are grown outward (floor the near edges, ceil the far ones) so a
// fractional device pixel ratio can never scissor away the outermost row or
// column of a HUD element, then clamped to the framebuffer.
inline bool MelonPrimeHud_LogicalRectToScissor(
    const QRect& logical,
    float factor,
    int framebufferWidth,
    int framebufferHeight,
    int& outX,
    int& outY,
    int& outWidth,
    int& outHeight)
{
    if (logical.isEmpty() || factor <= 0.0f
        || framebufferWidth <= 0 || framebufferHeight <= 0)
        return false;

    const int left = static_cast<int>(std::floor(logical.left() * factor));
    const int right = static_cast<int>(std::ceil((logical.right() + 1) * factor));
    const int top = static_cast<int>(std::floor(logical.top() * factor));
    const int bottom = static_cast<int>(std::ceil((logical.bottom() + 1) * factor));

    const int clampedLeft = std::max(0, std::min(left, framebufferWidth));
    const int clampedRight = std::max(clampedLeft, std::min(right, framebufferWidth));
    const int clampedTop = std::max(0, std::min(top, framebufferHeight));
    const int clampedBottom = std::max(clampedTop, std::min(bottom, framebufferHeight));

    outWidth = clampedRight - clampedLeft;
    outHeight = clampedBottom - clampedTop;
    if (outWidth <= 0 || outHeight <= 0)
        return false;

    outX = clampedLeft;
    // GL framebuffer coordinates count up from the bottom edge.
    outY = framebufferHeight - clampedBottom;
    return true;
}

// OPT-DR3 (retired): a whole-region content hash used to sit here so a large
// but unchanged dirty rectangle -- a held zoom scope was the motivating case --
// could skip its GPU upload. It was a defence against the bounding-box dirty
// contract, and the per-element visual stamps replace it exactly: an unchanged
// element no longer reaches the upload path at all, so hashing multiple
// megabytes of overlay every frame to discover the same thing would be pure
// memory bandwidth. Do not reintroduce a per-frame hash here; if a region ever
// needs one, it belongs to the element that produced it.

inline bool MelonPrimeHud_RefreshHudEpoch(
    const MelonPrime::CustomHudConfigState& hudConfig,
    uint32_t& cachedEpoch)
{
    const uint32_t epoch = MelonPrime::CustomHud_GetCacheEpoch(hudConfig);
    if (epoch == cachedEpoch)
        return false;

    cachedEpoch = epoch;
    return true;
}

inline void MelonPrimeHud_RefreshHudEnabledIfNeeded(
    const MelonPrime::CustomHudConfigState& hudConfig,
    Config::Table& instcfg,
    uint32_t& cachedEpoch,
    bool& hudEnabled)
{
    if (MelonPrimeHud_RefreshHudEpoch(hudConfig, cachedEpoch))
        hudEnabled = MelonPrime::CustomHud_IsEnabled(instcfg);
}

// Rebuild overlayFont from the selectable HUD-font setting when the config epoch changes.
// Cheap: only runs on a settings change (CustomHud_InvalidateConfigCache bumps the epoch).
inline void MelonPrimeHud_RefreshOverlayFontIfNeeded(
    const MelonPrime::CustomHudConfigState& hudConfig,
    Config::Table& instcfg,
    uint32_t& cachedEpoch,
    QFont& overlayFont)
{
    if (!MelonPrimeHud_RefreshHudEpoch(hudConfig, cachedEpoch))
        return;
    overlayFont = MelonPrime::CustomHud_ResolveBaseFont(instcfg);
    overlayFont.setPixelSize(MelonPrime::CustomHud_ResolveFontPixelSize(instcfg));
}

inline void MelonPrimeHud_RefreshRadarConfigIfNeeded(
    const MelonPrime::CustomHudConfigState& hudConfig,
    Config::Table& instcfg,
    uint32_t& cachedEpoch,
    bool& radarEnable,
    int& radarAnchor,
    int& radarDstX,
    int& radarDstY,
    int& radarDstSize,
    float& radarOpacity,
    int& radarSrcRadius,
    float& radarAnchorDsX,
    float& radarAnchorDsY)
{
    if (!MelonPrimeHud_RefreshHudEpoch(hudConfig, cachedEpoch))
        return;

    radarEnable    = instcfg.GetBool(MP_HUD_PROP_KEY_BtmOverlayEnable);
    radarAnchor    = instcfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayAnchor);
    radarDstX      = instcfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstX);
    radarDstY      = instcfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstY);
    radarDstSize   = std::max(instcfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstSize), 1);
    radarOpacity   = std::clamp((float)instcfg.GetDouble(MP_HUD_PROP_KEY_BtmOverlayOpacity), 0.0f, 1.0f);
    radarSrcRadius = instcfg.GetInt(MP_HUD_PROP_KEY_BtmOverlaySrcRadius);
    radarAnchorDsX = (radarAnchor % 3) * 128.0f;
    radarAnchorDsY = (radarAnchor / 3) * 96.0f;
}

template <typename CoreT>
inline bool MelonPrimeHud_CanRenderForCore(CoreT* mp, bool editMode)
{
    return mp && mp->IsRomDetected() && (mp->IsInGame() || editMode);
}

template <typename CoreT>
inline bool MelonPrimeHud_IsHudVisibleOrRestorePatch(
    EmuInstance* emuInstance,
    CoreT* mp,
    bool hudEnabled,
    bool editMode)
{
    const bool hudVisible = hudEnabled || editMode;
    if (!hudVisible) {
        MelonPrime::CustomHud_EnsurePatchRestored(
            mp->HudConfigState(), emuInstance,
            mp->GetCurrentRom(), mp->GetPlayerPosition());
    }
    return hudVisible;
}

// What one Custom HUD render did to the retained overlay.
//
// `dirty` is what changed and therefore what a backend has to upload; `content`
// is where the HUD currently has pixels and therefore what it has to
// composite. They are different sets on purpose: the game frame under the HUD
// is repainted every presentation, so the whole HUD must be blended back over
// it even on a frame where nothing changed.
struct HudOverlayUpdate {
    HudDirtyRegionSet dirty;
    HudDirtyRegionSet content;
    QRect dirtyBounds;
};

template <typename CoreT>
inline HudOverlayUpdate MelonPrimeHud_RenderTopOverlay(
    EmuInstance* emuInstance,
    Config::Table& instcfg,
    CoreT* mp,
    QImage& overlay,
    const QFont& overlayFont,
    bool hudEnabledSnapshot,
    float topStretchX,
    float hudScale,
    float hudOriginX,
    float hudOriginY,
    bool overlayRetained,
    const QImage* bottomScreen = nullptr,
    QImage* filteredBottomScreen = nullptr,
    int radarSourceRadius = 0,
    QPainter* directScoreboardPaint = nullptr,
    MelonPrime::NativePaintPerf* nativePaintPerf = nullptr)
{
    const float hudOriginXds = hudOriginX / hudScale;
    const float hudOriginYds = hudOriginY / hudScale;

    HudOverlayUpdate update;
    QPainter topP(&overlay);
    topP.setFont(overlayFont);
    QImage* radarSource = MelonPrime::CustomHud_PrepareRadarColorKeySource(
        bottomScreen,
        filteredBottomScreen,
        mp->GetHunterID(),
        radarSourceRadius);
    // `auto` rather than a fixed width: the measurement and shipping probe
    // builds declare different tick types for the same expression.
    const auto hudRenderStart = MelonPrimePerf::ReadTicksIfActive();
    update.dirtyBounds = MelonPrime::CustomHud_Render(
        mp->HudConfigState(),
        emuInstance, instcfg,
        mp->GetCurrentRom(), mp->GetAddrHot(),
        mp->GetPlayerPosition(),
        &topP, nullptr,
        &overlay, radarSource,
        mp->IsInGame(),
        hudEnabledSnapshot,
        topStretchX, hudScale,
        hudOriginXds, hudOriginYds,
        directScoreboardPaint,
        nativePaintPerf,
        overlayRetained,
        &update.dirty,
        &update.content);
    if (hudRenderStart)
        MelonPrimePerf::AddCustomHudRenderTicks(MelonPrimePerf::ReadTicksIfActive() - hudRenderStart);
    if (MelonPrimePerf::IsFrameActive() && !update.dirty.IsEmpty())
    {
        MelonPrimePerf::AddHudDirtyArea(static_cast<int>(update.dirty.PixelCount()));
        MelonPrimePerf::CountCustomHudDrawn();
    }
    return update;
}

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELONPRIME_HUD_SCREEN_OVERLAY_H
