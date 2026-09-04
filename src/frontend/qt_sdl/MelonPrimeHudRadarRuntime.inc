// This file is a unity-build fragment included by
// MelonPrimeHudRenderRuntime.inc.  Do not add it to CMakeLists.txt.

// =========================================================================
//  CPU radar color-key preprocessing
// =========================================================================

// One load, one shift and one mask per pixel. This used to be a linear scan of
// the fifteen palette entries, run for every pixel of the radar crop on every
// frame the radar was visible; the packed table makes membership constant time
// without adding a branch or a cache-hostile 32 KiB lookup.
static FORCE_INLINE bool CustomHud_IsRadarPaletteColor(uint32_t rgb)
{
    const uint16_t index =
        RadarPaletteLutIndex(rgb & kRadarPaletteQuantizationMask);
    return ((kRadarPaletteLut[index >> 6] >> (index & 63)) & 1u) != 0u;
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

    // Developer-only: this loop is the CPU radar's whole cost, and its price is
    // set by the crop area rather than by anything configurable, so the pixel
    // count belongs next to the timing.
    const auto filterStart = MelonPrimePerf::ReadTicksIfActive();
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
    if (filterStart) {
        MelonPrimePerf::RecordRadarCpuFilter(
            static_cast<uint64_t>(clippedRect.width())
                * static_cast<uint64_t>(clippedRect.height()),
            MelonPrimePerf::ReadTicksIfActive() - filterStart);
    }
    return scratch;
}
