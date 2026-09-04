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

#include "Config.h"
#include "EmuInstance.h"
#include "MelonPrimeHudConfigState.h"
#include "MelonPrimeHudPatchLifecycle.h"
#include "MelonPrimeHudRender.h"
#include "MelonPrimePerfProbe.h"
// Canonical Custom HUD config keys. Included rather than mirrored as string
// literals, per the config-key ownership rule; this header is registered in
// tools/ci/audits/check-inc-ownership.ps1's multi-parent map alongside the
// other consumers.
#include "MelonPrimeHudPropSchema.inc"

inline void MelonPrimeHud_PrepareTopOverlay(
    QImage& overlay,
    int outW,
    int outH,
    QRect& prevDirty)
{
    MelonPrimePerf::ScopedHudPhase clearTimer(MelonPrimePerf::HudPhase::Clear);
    if (overlay.width() != outW || overlay.height() != outH) {
        overlay = QImage(outW, outH, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        prevDirty = QRect();
        return;
    }

    if (!prevDirty.isEmpty()) {
        // OPT-HUD-1: Direct scanline clear — avoids QPainter construction +
        // raster-engine setup (~500–2000 cyc/frame when HUD is visible).
        // ARGB32_Premultiplied transparent == 0x00000000, so memset-to-0 is correct.
        const int left  = std::max(0, prevDirty.left());
        const int right = std::min(overlay.width()  - 1, prevDirty.right());
        const int top   = std::max(0, prevDirty.top());
        const int bot   = std::min(overlay.height() - 1, prevDirty.bottom());
        if (left <= right && top <= bot) {
            const std::size_t clearBytes =
                static_cast<std::size_t>(right - left + 1) * sizeof(QRgb);
            for (int y = top; y <= bot; ++y)
                std::memset(reinterpret_cast<QRgb*>(overlay.scanLine(y)) + left,
                            0, clearBytes);
        }
    }
}

// OPT-DR3: FNV-1a hash of an overlay sub-region, used to skip a redundant GPU
// upload when the rendered HUD pixels are byte-identical to the last uploaded
// region. The motivating case is holding a zoom scope still: the large scope
// reticle bbox is otherwise re-uploaded via glTexSubImage2D every frame even
// though its pixels never change. Hashing the region (cache-warm CPU read) is
// cheaper than a multi-MB PCIe upload + driver sync at high internal
// resolutions. Pixel-exact, so it can never leave the HUD stale.
inline uint64_t MelonPrimeHud_HashImageRegion(const QImage& img, const QRect& r)
{
    MelonPrimePerf::ScopedHudPhase hashTimer(MelonPrimePerf::HudPhase::Hash);
    uint64_t h = 1469598103934665603ull; // FNV-1a offset basis
    const int left  = std::max(0, r.left());
    const int right = std::min(img.width()  - 1, r.right());
    const int top   = std::max(0, r.top());
    const int bot   = std::min(img.height() - 1, r.bottom());
    if (left > right || top > bot)
        return h;

    const int rowBytes = (right - left + 1) * 4; // ARGB32 = 4 bytes/pixel
    MelonPrimePerf::CountHudRegionHash(
        static_cast<std::size_t>(rowBytes) * static_cast<std::size_t>(bot - top + 1));
    for (int y = top; y <= bot; ++y) {
        const uchar* row = img.scanLine(y) + left * 4;
        int i = 0;
        for (; i + 8 <= rowBytes; i += 8) {
            uint64_t chunk;
            std::memcpy(&chunk, row + i, 8);
            h = (h ^ chunk) * 1099511628211ull;
        }
        for (; i < rowBytes; ++i)
            h = (h ^ static_cast<uint64_t>(row[i])) * 1099511628211ull;
    }
    // Fold geometry so a same-pixels region move is never mistaken for unchanged.
    h ^= (static_cast<uint64_t>(r.width()) << 32) ^ static_cast<uint64_t>(r.height());
    return h;
}

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

template <typename CoreT>
inline QRect MelonPrimeHud_RenderTopOverlay(
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
    const QImage* bottomScreen = nullptr,
    QImage* filteredBottomScreen = nullptr,
    int radarSourceRadius = 0)
{
    const float hudOriginXds = hudOriginX / hudScale;
    const float hudOriginYds = hudOriginY / hudScale;

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
    const QRect hudDirty = MelonPrime::CustomHud_Render(
        mp->HudConfigState(),
        emuInstance, instcfg,
        mp->GetCurrentRom(), mp->GetAddrHot(),
        mp->GetPlayerPosition(),
        &topP, nullptr,
        &overlay, radarSource,
        mp->IsInGame(),
        hudEnabledSnapshot,
        topStretchX, hudScale,
        hudOriginXds, hudOriginYds);
    if (hudRenderStart)
        MelonPrimePerf::AddCustomHudRenderTicks(MelonPrimePerf::ReadTicksIfActive() - hudRenderStart);
    if (MelonPrimePerf::IsFrameActive() && !hudDirty.isEmpty())
    {
        MelonPrimePerf::AddHudDirtyArea(hudDirty.width() * hudDirty.height());
        MelonPrimePerf::CountCustomHudDrawn();
    }
    return hudDirty;
}

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELONPRIME_HUD_SCREEN_OVERLAY_H
