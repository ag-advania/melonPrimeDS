// Custom HUD helpers for Screen.cpp fragments.
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
static void MelonPrimeHud_PositionEditPanel(
    QWidget* host,
    MelonPrimeHudConfigOnScreenEdit* panel,
    bool setMaximumHeight,
    bool adjustSize)
{
    if (!host || !panel)
        return;

    const int maxH = host->height() - 8;
    if (setMaximumHeight)
        panel->setMaximumHeight(maxH);
    if (adjustSize)
        panel->adjustSize();

    const int panelH = std::min(panel->height(), maxH);
    const int px = host->width() - panel->width() - 4;
    const int py = (host->height() - panelH) / 2;
    panel->move(host->mapToGlobal(QPoint(px, std::max(4, py))));
}

static void MelonPrimeHud_PrepareTopOverlay(
    QImage& overlay,
    int outW,
    int outH,
    QRect& prevDirty)
{
    if (overlay.width() != outW || overlay.height() != outH) {
        overlay = QImage(outW, outH, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        prevDirty = QRect();
        return;
    }

    if (!prevDirty.isEmpty()) {
        QPainter clrP(&overlay);
        clrP.setCompositionMode(QPainter::CompositionMode_Source);
        clrP.fillRect(prevDirty, Qt::transparent);
    }
}

static bool MelonPrimeHud_RefreshHudEpoch(uint32_t& cachedEpoch)
{
    const uint32_t epoch = MelonPrime::CustomHud_GetCacheEpoch();
    if (epoch == cachedEpoch)
        return false;

    cachedEpoch = epoch;
    return true;
}

static void MelonPrimeHud_RefreshHudEnabledIfNeeded(
    Config::Table& instcfg,
    uint32_t& cachedEpoch,
    bool& hudEnabled)
{
    if (MelonPrimeHud_RefreshHudEpoch(cachedEpoch))
        hudEnabled = MelonPrime::CustomHud_IsEnabled(instcfg);
}

static void MelonPrimeHud_RefreshRadarConfigIfNeeded(
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
    if (!MelonPrimeHud_RefreshHudEpoch(cachedEpoch))
        return;

    radarEnable    = instcfg.GetBool("Metroid.Visual.BtmOverlayEnable");
    radarAnchor    = instcfg.GetInt("Metroid.Visual.BtmOverlayAnchor");
    radarDstX      = instcfg.GetInt("Metroid.Visual.BtmOverlayDstX");
    radarDstY      = instcfg.GetInt("Metroid.Visual.BtmOverlayDstY");
    radarDstSize   = std::max(instcfg.GetInt("Metroid.Visual.BtmOverlayDstSize"), 1);
    radarOpacity   = std::clamp((float)instcfg.GetDouble("Metroid.Visual.BtmOverlayOpacity"), 0.0f, 1.0f);
    radarSrcRadius = instcfg.GetInt("Metroid.Visual.BtmOverlaySrcRadius");
    radarAnchorDsX = (radarAnchor % 3) * 128.0f;
    radarAnchorDsY = (radarAnchor / 3) * 96.0f;
}

template <typename CoreT>
static bool MelonPrimeHud_CanRenderForCore(CoreT* mp, bool editMode)
{
    return mp && mp->IsRomDetected() && (mp->IsInGame() || editMode);
}

template <typename CoreT>
static bool MelonPrimeHud_IsHudVisibleOrRestorePatch(
    EmuInstance* emuInstance,
    Config::Table& instcfg,
    CoreT* mp,
    bool hudEnabled,
    bool editMode)
{
    const bool hudVisible = hudEnabled || editMode;
    if (!hudVisible) {
        MelonPrime::CustomHud_EnsurePatchRestored(
            emuInstance, instcfg,
            mp->GetCurrentRom(), mp->GetPlayerPosition(),
            mp->IsInGame());
    }
    return hudVisible;
}

template <typename CoreT>
static QRect MelonPrimeHud_RenderTopOverlay(
    EmuInstance* emuInstance,
    Config::Table& instcfg,
    CoreT* mp,
    QImage& overlay,
    const QFont& overlayFont,
    float topStretchX,
    float hudScale,
    float hudOriginX,
    float hudOriginY)
{
    const float hudOriginXds = hudOriginX / hudScale;
    const float hudOriginYds = hudOriginY / hudScale;

    QPainter topP(&overlay);
    topP.setFont(overlayFont);
    return MelonPrime::CustomHud_Render(
        emuInstance, instcfg,
        mp->GetCurrentRom(), mp->GetAddrHot(),
        mp->GetPlayerPosition(),
        &topP, nullptr,
        &overlay, nullptr,
        mp->IsInGame(),
        topStretchX, hudScale,
        hudOriginXds, hudOriginYds);
}
#endif
