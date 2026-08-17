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

            const bool sameGameFrame = m_hudVisualFrameValid
                && MelonPrimeHud_IsSameVisualGameFrame(
                    emuInstance, m_hudVisualFrameKey);
            HudVisualFrameKey visualKey{};
            if (sameGameFrame) {
                visualKey = MelonPrimeHud_MakeVisualFrameKey(
                    emuInstance, mp->HudConfigState(), m_hudCfgEpoch, m_hudFontEpoch,
                    topOutW, topOutH, topStretchX, hudScale,
                    m_hudOriginX, m_hudOriginY,
                    m_hudVisualRendererGeneration, m_hudEnabled, editMode);
            }
            const bool reuseVisual = m_hudVisualFrameValid
                && sameGameFrame
                && visualKey == m_hudVisualFrameKey;
            m_hudVisualFrameWasReused = reuseVisual;
            QRect curDirty = s_hudPrevDirtyS;
            if (reuseVisual) {
                // The retained QImage is still composited over the newly
                // painted game frame, but no clear, painter, HUD state read,
                // region hash, or CPU raster work is repeated.
                MelonPrimePerf::CountHudVisualReuse();
            } else {
                MelonPrimePerf::CountHudVisualRender();
                // OPT-DR1: Track dirty rect to limit clear + composite to HUD area only.
                MelonPrimeHud_PrepareTopOverlay(Overlay[0], topOutW, topOutH, s_hudPrevDirtyS);

                // Per-frame painter (must end before reading image)
                curDirty = MelonPrimeHud_RenderTopOverlay(
                    emuInstance, instcfg, mp,
                    Overlay[0], overlayFont,
                    topStretchX, hudScale,
                    m_hudOriginX, m_hudOriginY,
                    m_radarEnable ? MELONPRIME_HUD_BOTTOM_SCREEN_IMAGE : nullptr,
                    &Overlay[1], m_radarSrcRadius);
                s_hudPrevDirtyS = curDirty;
                if (!sameGameFrame) {
                    visualKey = MelonPrimeHud_MakeVisualFrameKey(
                        emuInstance, mp->HudConfigState(), m_hudCfgEpoch, m_hudFontEpoch,
                        topOutW, topOutH, topStretchX, hudScale,
                        m_hudOriginX, m_hudOriginY,
                        m_hudVisualRendererGeneration, m_hudEnabled, editMode);
                }
                m_hudVisualFrameKey = visualKey;
                m_hudVisualFrameValid = true;
            }

            // Composite only the dirty region (prev cleared + curr drawn).
            const QRect compositeRect = reuseVisual
                ? s_hudPrevDirtyS : s_hudPrevDirtyS.united(curDirty);
            if (!compositeRect.isEmpty()) {
                painter.resetTransform();
                painter.drawImage(QPoint(compositeRect.x(), compositeRect.y()),
                                  Overlay[0], compositeRect);
            }
        } else {
            m_hudVisualFrameValid = false;
            m_hudVisualFrameWasReused = false;
        }
    } else {
        m_hudVisualFrameValid = false;
        m_hudVisualFrameWasReused = false;
    }
}
#endif

#undef s_hudPrevDirtyS
