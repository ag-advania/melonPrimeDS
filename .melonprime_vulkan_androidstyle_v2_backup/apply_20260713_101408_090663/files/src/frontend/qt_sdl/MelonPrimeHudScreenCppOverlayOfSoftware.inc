// Custom HUD overlay-of-software path for ScreenPanelNative::paintEvent().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
#define s_hudPrevDirtyS m_hudPrevDirty
{
    auto* mp = emuThread->GetMelonPrimeCore();
    const bool editMode = mp && MelonPrime::CustomHud_IsEditMode(mp->HudConfigState());
    if (MelonPrimeHud_CanRenderForCore(mp, editMode))
    {
        auto& instcfg = emuInstance->getLocalConfig();
        // Use layout values cached in setupScreenLayout() - no sqrt per-frame.
        const float hudScale    = m_hudScale;
        const float topStretchX = m_topStretchX;

        MelonPrimeHud_RefreshHudEnabledIfNeeded(mp->HudConfigState(), instcfg, m_hudCfgEpoch, m_hudEnabled);
        MelonPrimeHud_RefreshOverlayFontIfNeeded(mp->HudConfigState(), instcfg, m_hudFontEpoch, overlayFont);
        MelonPrimeHud_RefreshRadarConfigIfNeeded(
            mp->HudConfigState(), instcfg, m_radarCfgEpoch,
            m_radarEnable, m_radarAnchor,
            m_radarDstX, m_radarDstY, m_radarDstSize,
            m_radarOpacity, m_radarSrcRadius,
            m_radarAnchorDsX, m_radarAnchorDsY);

        if (MelonPrimeHud_IsHudVisibleOrRestorePatch(emuInstance, instcfg, mp, m_hudEnabled, editMode))
        {
            // Overlay covers the full widget so HUD elements placed in black-bar
            // regions (DS x < 0 or > 256 for pillarboxed 4:3) remain visible.
            const int fullW = this->width();
            const int fullH = this->height();
            const int topOutW = std::max(1, fullW);
            const int topOutH = std::max(1, fullH);

            // OPT-DR1: Track dirty rect to limit clear + composite to HUD area only.
            MelonPrimeHud_PrepareTopOverlay(Overlay[0], topOutW, topOutH, s_hudPrevDirtyS);

            // Per-frame painter (must end before reading image)
            const QRect curDirty = MelonPrimeHud_RenderTopOverlay(
                emuInstance, instcfg, mp,
                Overlay[0], overlayFont,
                topStretchX, hudScale,
                m_hudOriginX, m_hudOriginY);

            // Composite only the dirty region (prev cleared + curr drawn).
            const QRect compositeRect = s_hudPrevDirtyS.united(curDirty);
            if (!compositeRect.isEmpty()) {
                painter.resetTransform();
                painter.drawImage(QPoint(compositeRect.x(), compositeRect.y()),
                                  Overlay[0], compositeRect);
            }
            s_hudPrevDirtyS = curDirty;
        }

        if (m_radarEnable && m_hudTopMatrixValid &&
            MelonPrime::CustomHud_ShouldDrawRadarOverlay(
                emuInstance, mp->GetCurrentRom(), mp->GetPlayerPosition()))
        {
            const float* topMtx = m_hudTopMatrix;
            const float worldAnchorX =
                topMtx[0] * m_radarAnchorDsX + topMtx[1] * m_radarAnchorDsY + topMtx[4];
            const float worldAnchorY =
                topMtx[2] * m_radarAnchorDsX + topMtx[3] * m_radarAnchorDsY + topMtx[5];
            const int baseX = static_cast<int>(m_hudOriginX);
            const int baseY = static_cast<int>(m_hudOriginY);
            const int dstX = baseX + static_cast<int>(
                (worldAnchorX - m_hudOriginX) + m_radarDstX * m_hudScale);
            const int dstY = baseY + static_cast<int>(
                (worldAnchorY - m_hudOriginY) + m_radarDstY * m_hudScale);
            const int dstPixels = std::max(1,
                static_cast<int>(m_radarDstSize * m_hudScale));
            const std::uint8_t hunterId = mp->GetHunterID();
            const QPointF sourceCenter(
                MelonPrime::kBtmOverlaySrcCenterX,
                MelonPrime::kBtmOverlaySrcCenterY[hunterId]);
            // MELONPRIME_VULKAN_RADAR_FRAME_SOURCE_V1
            // Source coordinates are authored in native 256x192 DS pixels.
            // Scale them when the displayed Vulkan compatibility frame is 2x+
            // so the crop remains on the same radar region.
            const QRectF sourceRect(
                (sourceCenter.x() - m_radarSrcRadius) * hudRadarSourceScaleX,
                (sourceCenter.y() - m_radarSrcRadius) * hudRadarSourceScaleY,
                m_radarSrcRadius * 2.0 * hudRadarSourceScaleX,
                m_radarSrcRadius * 2.0 * hudRadarSourceScaleY);
            const QRectF destination(dstX, dstY, dstPixels, dstPixels);

            painter.save();
            painter.resetTransform();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setOpacity(std::clamp(m_radarOpacity, 0.0f, 1.0f));
            QPainterPath clip;
            clip.addEllipse(destination);
            painter.setClipPath(clip);
            painter.drawImage(destination, hudRadarSource, sourceRect);
            painter.restore();
        }
    }
}
#endif

#undef s_hudPrevDirtyS
