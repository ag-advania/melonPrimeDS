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

    QWidget* boundsWidget = host->window();
    const QRect bounds = boundsWidget
        ? QRect(boundsWidget->mapToGlobal(QPoint(0, 0)), boundsWidget->size())
        : QRect(host->mapToGlobal(QPoint(0, 0)), host->size());
    const int maxH = std::max(120, bounds.height() - 8);
    if (setMaximumHeight)
        panel->setMaximumHeight(maxH);
    if (adjustSize)
        panel->adjustSize();

    const int desiredH = setMaximumHeight ? panel->sizeHint().height() : panel->height();
    const int panelH = std::min(desiredH, maxH);
    if (panel->height() != panelH)
        panel->resize(panel->width(), panelH);

    const auto* screenPanel = qobject_cast<const ScreenPanel*>(host);
    const std::optional<QRect> topScreenLocal = screenPanel
        ? screenPanel->getTopScreenWidgetRect()
        : std::nullopt;
    const QRect targetLocal = topScreenLocal.value_or(host->rect());
    const QRect targetGlobal(host->mapToGlobal(targetLocal.topLeft()), targetLocal.size());

    const int outsideTopX = targetGlobal.right() + 1 + 12;
    const int insideTopX = targetGlobal.right() + 1 - panel->width() - 24;
    const int preferredX = (outsideTopX + panel->width() <= bounds.right() + 1 - 4)
        ? outsideTopX
        : insideTopX;
    const int minX = bounds.left() + 4;
    const int maxX = bounds.right() - panel->width() + 1 - 4;
    const int px = std::clamp(preferredX, minX, std::max(minX, maxX));

    const int preferredY = bounds.top() + (bounds.height() - panelH) / 2;
    const int minY = bounds.top() + 4;
    const int maxY = bounds.bottom() - panelH + 1 - 4;
    const int py = std::clamp(preferredY, minY, std::max(minY, maxY));
    panel->move(QPoint(px, py));
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
static uint64_t MelonPrimeHud_HashImageRegion(const QImage& img, const QRect& r)
{
    uint64_t h = 1469598103934665603ull; // FNV-1a offset basis
    const int left  = std::max(0, r.left());
    const int right = std::min(img.width()  - 1, r.right());
    const int top   = std::max(0, r.top());
    const int bot   = std::min(img.height() - 1, r.bottom());
    if (left > right || top > bot)
        return h;

    const int rowBytes = (right - left + 1) * 4; // ARGB32 = 4 bytes/pixel
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

static bool MelonPrimeHud_RefreshHudEpoch(
    const MelonPrime::CustomHudConfigState& hudConfig,
    uint32_t& cachedEpoch)
{
    const uint32_t epoch = MelonPrime::CustomHud_GetCacheEpoch(hudConfig);
    if (epoch == cachedEpoch)
        return false;

    cachedEpoch = epoch;
    return true;
}

static void MelonPrimeHud_RefreshHudEnabledIfNeeded(
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
static void MelonPrimeHud_RefreshOverlayFontIfNeeded(
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

static void MelonPrimeHud_RefreshRadarConfigIfNeeded(
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
            mp->HudConfigState(), emuInstance, instcfg,
            mp->GetCurrentRom(), mp->GetPlayerPosition(),
            mp->IsInGame());
    }
    return hudVisible;
}

static bool MelonPrimeHud_IsRadarPaletteColor(uint32_t rgb)
{
    const uint32_t quantizedRgb = rgb & MelonPrime::kRadarPaletteQuantizationMask;
    for (const uint32_t paletteColor : MelonPrime::kRadarPaletteColors) {
        if (quantizedRgb == paletteColor)
            return true;
    }
    return false;
}

static QImage* MelonPrimeHud_PrepareSoftwareRadarSource(
    const QImage* bottomScreen,
    QImage* filteredBottomScreen,
    uint8_t hunterID,
    int sourceRadius)
{
    if (!bottomScreen || bottomScreen->isNull() || bottomScreen->depth() != 32
        || !filteredBottomScreen || sourceRadius <= 0)
        return nullptr;

    const int width = bottomScreen->width();
    const int height = bottomScreen->height();
    if (filteredBottomScreen->width() != width
        || filteredBottomScreen->height() != height
        || filteredBottomScreen->format() != QImage::Format_ARGB32_Premultiplied)
    {
        *filteredBottomScreen = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
        filteredBottomScreen->fill(Qt::transparent);
    }

    const int safeHunterID = std::min<int>(hunterID, MelonPrime::kHunterCount - 1);
    const float scaleX = static_cast<float>(width) / 256.0f;
    const float scaleY = static_cast<float>(height) / 192.0f;
    const QRect sourceRect(
        static_cast<int>((MelonPrime::kBtmOverlaySrcCenterX - sourceRadius) * scaleX),
        static_cast<int>((MelonPrime::kBtmOverlaySrcCenterY[safeHunterID] - sourceRadius) * scaleY),
        static_cast<int>(sourceRadius * 2 * scaleX),
        static_cast<int>(sourceRadius * 2 * scaleY));
    const QRect clippedRect = sourceRect.intersected(bottomScreen->rect());
    if (clippedRect.isEmpty())
        return nullptr;

    for (int y = clippedRect.top(); y <= clippedRect.bottom(); ++y) {
        const auto* sourceLine = reinterpret_cast<const QRgb*>(bottomScreen->constScanLine(y));
        auto* destinationLine = reinterpret_cast<QRgb*>(filteredBottomScreen->scanLine(y));
        for (int x = clippedRect.left(); x <= clippedRect.right(); ++x) {
            const uint32_t rgb = sourceLine[x] & 0x00FFFFFFu;
            destinationLine[x] = MelonPrimeHud_IsRadarPaletteColor(rgb)
                ? static_cast<QRgb>(0xFF000000u | rgb)
                : static_cast<QRgb>(0u);
        }
    }
    return filteredBottomScreen;
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
    float hudOriginY,
    const QImage* bottomScreen = nullptr,
    QImage* filteredBottomScreen = nullptr,
    int radarSourceRadius = 0,
    bool forceHideForMenuHeld = false)
{
    const float hudOriginXds = hudOriginX / hudScale;
    const float hudOriginYds = hudOriginY / hudScale;

    QPainter topP(&overlay);
    topP.setFont(overlayFont);
    QImage* radarSource = MelonPrimeHud_PrepareSoftwareRadarSource(
        bottomScreen,
        filteredBottomScreen,
        mp->GetHunterID(),
        radarSourceRadius);
    const Uint64 hudRenderStart = MelonPrimePerf::ReadTicksIfActive();
    const QRect hudDirty = MelonPrime::CustomHud_Render(
        mp->HudConfigState(),
        emuInstance, instcfg,
        mp->GetCurrentRom(), mp->GetAddrHot(),
        mp->GetPlayerPosition(),
        &topP, nullptr,
        &overlay, radarSource,
        mp->IsInGame(),
        topStretchX, hudScale,
        hudOriginXds, hudOriginYds,
        forceHideForMenuHeld);
    if (hudRenderStart)
        MelonPrimePerf::AddCustomHudRenderTicks(MelonPrimePerf::ReadTicksIfActive() - hudRenderStart);
    if (MelonPrimePerf::IsFrameActive() && !hudDirty.isEmpty())
        MelonPrimePerf::AddHudDirtyArea(hudDirty.width() * hudDirty.height());
    return hudDirty;
}
#endif
