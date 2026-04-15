// Custom HUD overlay-of-software path for ScreenPanelNative::paintEvent().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
{
    auto* mp = emuThread->GetMelonPrimeCore();
    const bool editMode = MelonPrime::CustomHud_IsEditMode();
    if (MelonPrimeHud_CanRenderForCore(mp, editMode))
    {
        auto& instcfg = emuInstance->getLocalConfig();
        // Use layout values cached in setupScreenLayout() - no sqrt per-frame.
        const float hudScale    = m_hudScale;
        const float topStretchX = m_topStretchX;

        MelonPrimeHud_RefreshHudEnabledIfNeeded(instcfg, m_hudCfgEpoch, m_hudEnabled);

        if (MelonPrimeHud_IsHudVisibleOrRestorePatch(emuInstance, instcfg, mp, m_hudEnabled, editMode))
        {
            // Overlay covers the full widget so HUD elements placed in black-bar
            // regions (DS x < 0 or > 256 for pillarboxed 4:3) remain visible.
            const int fullW = this->width();
            const int fullH = this->height();
            const int topOutW = std::max(1, fullW);
            const int topOutH = std::max(1, fullH);

            // OPT-DR1: Track dirty rect to limit clear + composite to HUD area only.
            static QRect s_hudPrevDirtyS;
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
    }
}
#endif
