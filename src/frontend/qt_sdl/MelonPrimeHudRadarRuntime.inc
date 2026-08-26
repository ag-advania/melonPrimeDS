// This file is a unity-build fragment included by
// MelonPrimeHudRenderRuntime.inc.  Do not add it to CMakeLists.txt.

// =========================================================================
//  CPU radar color-key preprocessing
// =========================================================================

static bool CustomHud_IsRadarPaletteColor(uint32_t rgb)
{
    const uint32_t quantizedRgb = rgb & kRadarPaletteQuantizationMask;
    for (const uint32_t paletteColor : kRadarPaletteColors) {
        if (quantizedRgb == paletteColor)
            return true;
    }
    return false;
}

int CustomHud_ResolveRadarColorKeyRadius(Config::Table& localCfg)
{
    if (!localCfg.GetBool(MP_HUD_PROP_KEY_BtmOverlayEnable))
        return 0;
    return localCfg.GetInt(MP_HUD_PROP_KEY_BtmOverlaySrcRadius);
}

QImage* CustomHud_PrepareRadarColorKeySource(
    const QImage* bottomScreen,
    QImage* scratch,
    uint8_t hunterID,
    int sourceRadius)
{
    if (!bottomScreen || bottomScreen->isNull() || bottomScreen->depth() != 32
        || !scratch || sourceRadius <= 0)
        return nullptr;

    const int width = bottomScreen->width();
    const int height = bottomScreen->height();
    if (scratch->width() != width
        || scratch->height() != height
        || scratch->format() != QImage::Format_ARGB32_Premultiplied)
    {
        *scratch = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
        scratch->fill(Qt::transparent);
    }

    const int safeHunterID = std::min<int>(hunterID, kHunterCount - 1);
    const float scaleX = static_cast<float>(width) / 256.0f;
    const float scaleY = static_cast<float>(height) / 192.0f;
    const QRect sourceRect(
        static_cast<int>((kBtmOverlaySrcCenterX - sourceRadius) * scaleX),
        static_cast<int>((kBtmOverlaySrcCenterY[safeHunterID] - sourceRadius) * scaleY),
        static_cast<int>(sourceRadius * 2 * scaleX),
        static_cast<int>(sourceRadius * 2 * scaleY));
    const QRect clippedRect = sourceRect.intersected(bottomScreen->rect());
    if (clippedRect.isEmpty())
        return nullptr;

    for (int y = clippedRect.top(); y <= clippedRect.bottom(); ++y) {
        const auto* sourceLine = reinterpret_cast<const QRgb*>(bottomScreen->constScanLine(y));
        auto* destinationLine = reinterpret_cast<QRgb*>(scratch->scanLine(y));
        for (int x = clippedRect.left(); x <= clippedRect.right(); ++x) {
            const uint32_t rgb = sourceLine[x] & 0x00FFFFFFu;
            destinationLine[x] = CustomHud_IsRadarPaletteColor(rgb)
                ? static_cast<QRgb>(0xFF000000u | rgb)
                : static_cast<QRgb>(0u);
        }
    }
    return scratch;
}
