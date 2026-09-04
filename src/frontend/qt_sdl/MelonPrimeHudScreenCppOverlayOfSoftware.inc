// Custom HUD overlay-of-software path for ScreenPanelNative::paintEvent().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
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

        if (MelonPrimeHud_IsHudVisibleOrRestorePatch(emuInstance, mp, m_hudEnabled, editMode))
        {
            // Overlay covers the full widget so HUD elements placed in black-bar
            // regions (DS x < 0 or > 256 for pillarboxed 4:3) remain visible.
            const int fullW = this->width();
            const int fullH = this->height();
            const int topOutW = std::max(1, fullW);
            const int topOutH = std::max(1, fullH);

            const HudVisualFrameIdentity visualIdentity =
                MelonPrimeHud_ProbeVisualFrameIdentity(emuInstance);
            MelonPrimePerf::CountHudVisualIdentityProbe();
            const bool sameGameFrame = m_hudVisualFrameValid
                && MelonPrimeHud_IsSameVisualGameFrame(
                    visualIdentity, m_hudVisualFrameKey);
            HudVisualFrameKey visualKey{};
            if (sameGameFrame) {
                visualKey = MelonPrimeHud_MakeVisualFrameKey(
                    visualIdentity, mp->HudConfigState(), m_hudCfgEpoch, m_hudFontEpoch,
                    topOutW, topOutH, topStretchX, hudScale,
                    m_hudOriginX, m_hudOriginY,
                    m_hudVisualRendererGeneration, m_hudEnabled, editMode);
                MelonPrimePerf::CountHudVisualStampCheck();
            }
            const bool reuseVisual = m_hudVisualFrameValid
                && sameGameFrame
                && visualKey == m_hudVisualFrameKey;
            m_hudVisualFrameWasReused = reuseVisual;
            if (reuseVisual) {
                // The retained QImage is still composited over the newly
                // painted game frame, but no clear, painter, HUD state read,
                // region hash, or CPU raster work is repeated. Nothing changed,
                // so nothing is dirty; the content set is still current.
                MelonPrimePerf::CountHudVisualReuse();
                m_hudDirtyRegions.Reset();
                MelonPrimePerf::CountHudRetainedOnlyFrame();
            } else {
                MelonPrimePerf::CountHudVisualRender();
                // The renderer owns clearing the retained overlay: it is the
                // only place that knows which per-element regions are about to
                // be redrawn. Allocation here, and only a reallocation drops
                // the retained pixels.
                if (MelonPrimeHud_EnsureTopOverlay(Overlay[0], topOutW, topOutH))
                    m_hudOverlayRetained = false;

                // Per-frame painter (must end before reading image)
                const HudOverlayUpdate hudUpdate = MelonPrimeHud_RenderTopOverlay(
                    emuInstance, instcfg, mp,
                    Overlay[0], overlayFont, m_hudEnabled,
                    topStretchX, hudScale,
                    m_hudOriginX, m_hudOriginY,
                    m_hudOverlayRetained,
                    m_radarEnable ? MELONPRIME_HUD_BOTTOM_SCREEN_IMAGE : nullptr,
                    &Overlay[1], m_radarSrcRadius,
#ifdef MELONPRIME_HUD_NATIVE_PAINT_PERF
                    &painter, &m_nativePaintPerf);
#else
                    nullptr, nullptr);
#endif
                m_hudDirtyRegions = hudUpdate.dirty;
                m_hudContentRegions = hudUpdate.content;
                m_hudOverlayRetained = true;
                if (!sameGameFrame) {
                    visualKey = MelonPrimeHud_MakeVisualFrameKey(
                        visualIdentity, mp->HudConfigState(), m_hudCfgEpoch, m_hudFontEpoch,
                        topOutW, topOutH, topStretchX, hudScale,
                        m_hudOriginX, m_hudOriginY,
                        m_hudVisualRendererGeneration, m_hudEnabled, editMode);
                }
                MelonPrimePerf::CountHudVisualStampCommit();
                m_hudVisualFrameKey = visualKey;
                m_hudVisualFrameValid = true;
            }

            // Native Software has no retained composed target. Repaint the
            // cached scoreboard directly before the retained overlay is
            // copied, preserving the original scoreboard-before-other-HUD
            // draw order without a second scoreboard image composite.
#if !defined(MELONPRIME_HUD_SKIP_RETAINED_TARGET_REUSE_COMPOSITE) \
    || !MELONPRIME_HUD_SKIP_RETAINED_TARGET_REUSE_COMPOSITE
            if (reuseVisual) {
                MelonPrime::CustomHud_RenderCachedScoreboard(
                    mp->HudConfigState(), instcfg, &painter,
                    topStretchX, hudScale,
                    m_hudOriginX / hudScale, m_hudOriginY / hudScale,
                    &m_nativePaintPerf);
            }
#endif

#if defined(MELONPRIME_HUD_SKIP_RETAINED_TARGET_REUSE_COMPOSITE) \
    && MELONPRIME_HUD_SKIP_RETAINED_TARGET_REUSE_COMPOSITE
            // DX12 retains both this target and the uploaded HUD layer, so the
            // target only needs the regions whose pixels actually changed. On
            // a reused visual frame it already holds the right pixels and the
            // dirty set is empty, so this collapses to nothing.
            const HudDirtyRegionSet& hudCompositeRegions = m_hudDirtyRegions;
#else
            // Native Software repaints the game frame underneath every time,
            // so the *whole* HUD has to be blended back over it -- the dirty
            // set would only restore the part that changed.
            const HudDirtyRegionSet& hudCompositeRegions = m_hudContentRegions;
#endif
            if (!hudCompositeRegions.IsEmpty()) {
                painter.resetTransform();
#ifdef MELONPRIME_HUD_NATIVE_PAINT_PERF
                const std::uint64_t compositePixels = hudCompositeRegions.PixelCount();
                m_nativePaintPerf.RecordHudOverlayComposite(
                    compositePixels * sizeof(QRgb), compositePixels);
#endif
                MelonPrimeHud_CompositeRegions(painter, Overlay[0], hudCompositeRegions);
            }
        } else {
            m_hudVisualFrameValid = false;
            m_hudVisualFrameWasReused = false;
            m_hudOverlayRetained = false;
            m_hudDirtyRegions.Reset();
            m_hudContentRegions.Reset();
        }
    } else {
        m_hudVisualFrameValid = false;
        m_hudVisualFrameWasReused = false;
        m_hudOverlayRetained = false;
        m_hudDirtyRegions.Reset();
        m_hudContentRegions.Reset();
    }
}
#endif
