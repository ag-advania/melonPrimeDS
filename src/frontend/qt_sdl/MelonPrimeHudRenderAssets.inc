// This file is a unity-build fragment included by MelonPrimeHudRender.cpp.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  P-1: Static icon cache — loaded at a specific pixel height, reloaded when
//       the config-driven size changes.  SVGs are rasterised via QImageReader.
// =========================================================================

// Render an SVG resource to a QImage of the requested height (aspect-preserving).
static QImage loadSvgToHeight(const char* path, int h)
{
    if (h <= 0) h = 16;
    QString qpath = QString::fromLatin1(path);
    QImageReader reader(qpath);
    QSize sz = reader.size();
    if (sz.isEmpty()) sz = QSize(h, h);
    int w = qMax(1, sz.width() * h / qMax(1, sz.height()));
    reader.setScaledSize(QSize(w, h));
    QImage img = reader.read();
    return img.isNull() ? img : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

static QImage s_weaponIcons[9];
static QImage s_weaponTintedIcons[9];
static QColor s_weaponTintColors[9];
static bool   s_weaponTintValid[9] = {};
static int    s_weaponIconHeight = 0;
static float  s_weaponIconMaxAspect = 1.0f;

// Bomb icon cache — 4 icons (index 0-3 = bombs remaining)
static QImage s_bombIcons[4];
static QImage s_bombTintedIcons[4];
static QColor s_bombTintColor;
static int    s_bombIconHeight = 0;
static bool   s_bombTintCacheValid = false;

// Outline icon caches — forward-declared here so EnsureBombIconsLoaded can invalidate
static QImage s_weaponIconsOutline[9];
static QImage s_bombIconsOutline[4];
static QColor s_outlineWeaponColor;
static int    s_outlineWeaponExpandR   = -1;
static int    s_outlineWeaponBaseIconH = -1;
static QColor s_outlineBombColor;
static int    s_outlineBombExpandR     = -1;
static int    s_outlineBombBaseIconH   = -1;
// Weapon Inventory icon outline cache (separate from DrawWeaponAmmo's weapon outline)
static QImage s_invIconsOutline[9];
static QColor s_outlineInvColor;
static int    s_outlineInvExpandR     = -1;
static int    s_outlineInvBaseIconH   = -1;

static const QImage& GetBombIconForDraw(int bombs, bool useOverlay, const QColor& overlayColor)
{
    const int idx = (bombs >= 0 && bombs <= 3) ? bombs : 0;
    if (!useOverlay)
        return s_bombIcons[idx];
    if (!s_bombTintCacheValid || s_bombTintColor != overlayColor) {
        for (int i = 0; i < 4; ++i) {
            if (s_bombIcons[i].isNull()) { s_bombTintedIcons[i] = s_bombIcons[i]; continue; }
            QImage tinted = s_bombIcons[i].copy();
            QPainter tp(&tinted);
            tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tp.fillRect(tinted.rect(), overlayColor);
            tp.end();
            s_bombTintedIcons[i] = std::move(tinted);
        }
        s_bombTintColor = overlayColor;
        s_bombTintCacheValid = true;
    }
    return s_bombTintedIcons[idx];
}

static void EnsureBombIconsLoaded(int pixelH = 12)
{
    int targetH = qMax(1, pixelH);
    if (LIKELY(s_bombIconHeight == targetH && s_bombIconHeight != 0)) return;
    static const char* kPaths[4] = {
        ":/mph-icon-bombs0", ":/mph-icon-bombs1",
        ":/mph-icon-bombs2", ":/mph-icon-bombs3"
    };
    for (int i = 0; i < 4; ++i) {
        s_bombIcons[i] = loadSvgToHeight(kPaths[i], targetH);
        s_bombTintedIcons[i] = s_bombIcons[i];
    }
    s_bombTintCacheValid = false;
    s_bombIconHeight = targetH;
    s_outlineBombColor = QColor(); // invalidate bomb outline cache
}

static const QImage& GetWeaponIconForDraw(uint8_t weapon, bool useOverlay, const QColor& overlayColor)
{
    if (weapon >= 9) return s_weaponIcons[0];
    if (!useOverlay)
        return s_weaponIcons[weapon];

    if (!s_weaponTintValid[weapon] || s_weaponTintColors[weapon] != overlayColor) {
        if (!s_weaponIcons[weapon].isNull()) {
            QImage tinted = s_weaponIcons[weapon].copy();
            QPainter tp(&tinted);
            tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tp.fillRect(tinted.rect(), overlayColor);
            tp.end();
            s_weaponTintedIcons[weapon] = std::move(tinted);
        }
        s_weaponTintColors[weapon] = overlayColor;
        s_weaponTintValid[weapon] = true;
    }
    return s_weaponTintedIcons[weapon];
}

static void EnsureIconsLoaded(int pixelH = 16)
{
    int targetH = qMax(1, pixelH);
    if (LIKELY(s_weaponIconHeight == targetH && s_weaponIconHeight != 0)) return;
    static const char* kIconPaths[9] = {
        ":/mph-icon-pb", ":/mph-icon-volt", ":/mph-icon-missile",
        ":/mph-icon-battlehammer", ":/mph-icon-imperialist",
        ":/mph-icon-judicator", ":/mph-icon-magmaul",
        ":/mph-icon-shock", ":/mph-icon-omega"
    };
    float maxAspect = 1.0f;
    for (int i = 0; i < 9; i++) {
        s_weaponIcons[i] = loadSvgToHeight(kIconPaths[i], targetH);
        s_weaponTintedIcons[i] = s_weaponIcons[i];
        s_weaponTintValid[i] = false;
        if (!s_weaponIcons[i].isNull() && s_weaponIcons[i].height() > 0) {
            const float aspect = static_cast<float>(s_weaponIcons[i].width()) /
                                 static_cast<float>(s_weaponIcons[i].height());
            if (aspect > maxAspect) maxAspect = aspect;
        }
    }
    s_weaponIconHeight = targetH;
    s_weaponIconMaxAspect = maxAspect;
    s_outlineWeaponColor = QColor(); // invalidate weapon outline cache
    s_outlineInvColor    = QColor(); // invalidate inventory icon outline cache
}

// ── P-12: Separable max-filter dilation ──────────────────────────────────────
// Two-pass (horizontal then vertical) max filter reduces per-pixel cost from
// O((2R+1)²) to O(2(2R+1)). For R=1 this is ~1.5× faster; for R=3, ~3× faster.
// Called only on config changes (not per frame).
static void DilateSeparableTinted(QImage& dstImg, const QImage& srcImg,
                                   const QColor& col, int R)
{
    const int sw = srcImg.width(), sh = srcImg.height();
    const int dw = sw + R * 2, dh = sh + R * 2;
    dstImg = QImage(dw, dh, QImage::Format_ARGB32_Premultiplied);
    dstImg.fill(Qt::transparent);
    if (sw == 0 || sh == 0) return;

    // Pass 1: horizontal max — temp[dw × sh]
    std::vector<uint8_t> hMax(static_cast<size_t>(dw) * sh, 0);
    for (int y = 0; y < sh; ++y) {
        const QRgb* srcRow = reinterpret_cast<const QRgb*>(srcImg.constScanLine(y));
        uint8_t* hRow = hMax.data() + y * dw;
        for (int dx = 0; dx < dw; ++dx) {
            const int x0 = dx - R;
            int maxA = 0;
            const int kxMin = std::max(0, x0 - R);
            const int kxMax = std::min(sw - 1, x0 + R);
            for (int kx = kxMin; kx <= kxMax; ++kx) {
                const int a = qAlpha(srcRow[kx]);
                if (a > maxA) { maxA = a; if (maxA == 255) break; }
            }
            hRow[dx] = static_cast<uint8_t>(maxA);
        }
    }

    // Pass 2: vertical max + tint → dstImg[dw × dh]
    const int cr = col.red(), cg = col.green(), cb = col.blue();
    for (int dy = 0; dy < dh; ++dy) {
        QRgb* dstRow = reinterpret_cast<QRgb*>(dstImg.scanLine(dy));
        const int y0 = dy - R;
        const int kyMin = std::max(0, y0 - R);
        const int kyMax = std::min(sh - 1, y0 + R);
        for (int dx = 0; dx < dw; ++dx) {
            int maxA = 0;
            for (int ky = kyMin; ky <= kyMax; ++ky) {
                const int a = hMax[ky * dw + dx];
                if (a > maxA) { maxA = a; if (maxA == 255) break; }
            }
            if (maxA > 0)
                dstRow[dx] = qRgba(cr * maxA / 255, cg * maxA / 255, cb * maxA / 255, maxA);
        }
    }
}

// ── Outline icon dilation ────────────────────────────────────────────────────
// Applies max-dilation to an icon using P-12 separable filter.
// Output image is (sw+2R) × (sh+2R), filled with outlineColor at dilated alpha.
// Icon images are at output-pixel scale, so R == thickness (output pixels).
static QImage DilateAndTintIconForOutline(const QImage& src, const QColor& col, int R)
{
    if (src.isNull()) return src;
    const QImage s = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage dst;
    DilateSeparableTinted(dst, s, col, R);
    return dst;
}

// Per-element dilated outline caches (weapon icons and bomb icons use separate colors).
static void EnsureWeaponOutlineIconsUpdated(const QColor& color, int expandR)
{
    if (s_outlineWeaponColor   == color   &&
        s_outlineWeaponExpandR == expandR &&
        s_outlineWeaponBaseIconH == s_weaponIconHeight) return;
    s_outlineWeaponColor     = color;
    s_outlineWeaponExpandR   = expandR;
    s_outlineWeaponBaseIconH = s_weaponIconHeight;
    for (int i = 0; i < 9; ++i)
        s_weaponIconsOutline[i] = DilateAndTintIconForOutline(s_weaponIcons[i], color, expandR);
}
static void EnsureInvOutlineIconsUpdated(const QColor& color, int expandR)
{
    if (s_outlineInvColor     == color   &&
        s_outlineInvExpandR   == expandR &&
        s_outlineInvBaseIconH == s_weaponIconHeight) return;
    s_outlineInvColor     = color;
    s_outlineInvExpandR   = expandR;
    s_outlineInvBaseIconH = s_weaponIconHeight;
    for (int i = 0; i < 9; ++i)
        s_invIconsOutline[i] = DilateAndTintIconForOutline(s_weaponIcons[i], color, expandR);
}
static void EnsureBombOutlineIconsUpdated(const QColor& color, int expandR)
{
    if (s_outlineBombColor   == color   &&
        s_outlineBombExpandR == expandR &&
        s_outlineBombBaseIconH == s_bombIconHeight) return;
    s_outlineBombColor     = color;
    s_outlineBombExpandR   = expandR;
    s_outlineBombBaseIconH = s_bombIconHeight;
    for (int i = 0; i < 4; ++i)
        s_bombIconsOutline[i] = DilateAndTintIconForOutline(s_bombIcons[i], color, expandR);
}

// =========================================================================
//  Radar frame SVG cache — loaded once per size, re-tinted on color change.
//  The SVG viewBox is 76×76 matching the actual bottom screen radar art size.
//  The frame is drawn proportionally: 76 / (srcRadius*2) * dstSize DS pixels.
// =========================================================================
static QImage s_radarFrame;
static QImage s_radarFrameTinted;
static QColor s_radarFrameTintColor;
static int    s_radarFrameTargetH = 0;
// Cached dilated outline of the frame (same technique as weapon icons)
static QImage s_radarFrameOutline;
static QColor s_radarFrameOutlineColor;
static int    s_radarFrameOutlineR = -1;

// P-13: Pre-computed hunter QColor table — avoids per-frame QColor construction
//       from bit-shifting. Initialized once on first use.
static const QColor& HunterFrameQColor(int idx)
{
    static const QColor table[kHunterCount] = {
        QColor((kHunterFrameColor[0] >> 16) & 0xFF, (kHunterFrameColor[0] >> 8) & 0xFF, kHunterFrameColor[0] & 0xFF),
        QColor((kHunterFrameColor[1] >> 16) & 0xFF, (kHunterFrameColor[1] >> 8) & 0xFF, kHunterFrameColor[1] & 0xFF),
        QColor((kHunterFrameColor[2] >> 16) & 0xFF, (kHunterFrameColor[2] >> 8) & 0xFF, kHunterFrameColor[2] & 0xFF),
        QColor((kHunterFrameColor[3] >> 16) & 0xFF, (kHunterFrameColor[3] >> 8) & 0xFF, kHunterFrameColor[3] & 0xFF),
        QColor((kHunterFrameColor[4] >> 16) & 0xFF, (kHunterFrameColor[4] >> 8) & 0xFF, kHunterFrameColor[4] & 0xFF),
        QColor((kHunterFrameColor[5] >> 16) & 0xFF, (kHunterFrameColor[5] >> 8) & 0xFF, kHunterFrameColor[5] & 0xFF),
        QColor((kHunterFrameColor[6] >> 16) & 0xFF, (kHunterFrameColor[6] >> 8) & 0xFF, kHunterFrameColor[6] & 0xFF),
    };
    return table[idx];
}

// P-13: Cached effective radar color — avoids QColor construction + comparison
//       every frame when hunter/config hasn't changed.
static QColor  s_effectiveRadarColor;
static uint8_t s_effectiveRadarHunterID = 0xFF;
static bool    s_effectiveRadarUseHunter = false;

// Bottom screen radar art size in pixels (= SVG viewBox width/height).
static constexpr int kRadarArtSize = 76;

static void EnsureRadarFrameLoaded(int dstSize, int srcRadius, float hudScale,
                                    const QColor& tintColor, const QColor& outlineColor,
                                    int outlineThickness)
{
    // Map 76 bottom-screen pixels to top-screen DS pixels proportionally,
    // then scale to output pixels via hudScale.
    const float dsFrameSize = static_cast<float>(kRadarArtSize) * dstSize / static_cast<float>(srcRadius * 2);
    const int targetH = qMax(1, (int)std::ceil(dsFrameSize * hudScale));
    if (s_radarFrameTargetH != targetH || s_radarFrameTargetH == 0) {
        s_radarFrame = loadSvgToHeight(":/mph-icon-radar-frame", targetH);
        s_radarFrameTargetH = targetH;
        s_radarFrameTintColor = QColor(); // force re-tint
        s_radarFrameOutlineColor = QColor(); // force re-outline
        s_radarFrameOutlineR = -1;
    }
    if (s_radarFrameTintColor != tintColor) {
        if (!s_radarFrame.isNull()) {
            s_radarFrameTinted = s_radarFrame.copy();
            QPainter tp(&s_radarFrameTinted);
            tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tp.fillRect(s_radarFrameTinted.rect(), tintColor);
            tp.end();
        }
        s_radarFrameTintColor = tintColor;
    }
    // Use proper max-filter dilation for the outline (same as weapon/bomb icons).
    // outlineThickness == 0 means the frame outline is disabled; avoid building
    // a never-used dilation bitmap on the first radar frame after config refresh.
    if (outlineThickness <= 0) {
        if (s_radarFrameOutlineR != 0 || !s_radarFrameOutline.isNull()) {
            s_radarFrameOutline = QImage();
            s_radarFrameOutlineColor = QColor();
            s_radarFrameOutlineR = 0;
        }
        return;
    }

    const int expandR = std::max(1, outlineThickness);
    if (s_radarFrameOutlineColor != outlineColor || s_radarFrameOutlineR != expandR) {
        if (!s_radarFrame.isNull()) {
            s_radarFrameOutline = DilateAndTintIconForOutline(s_radarFrame, outlineColor, expandR);
        }
        s_radarFrameOutlineColor = outlineColor;
        s_radarFrameOutlineR = expandR;
    }
}

// =========================================================================
//  P-17: Crosshair outline stamp — small pre-rendered ARGB image of the
//        crosshair outline shape, built once on config change and composited
//        with a single drawImage each frame.  Eliminates per-frame QPainter
//        creation + all fillRect calls in DrawCrosshair.
// =========================================================================
static QImage s_chOutlineStamp;
static QPoint s_chStampOrigin;   // offset from (pxCx, pxCy) to stamp top-left
static bool   s_chStampValid = false;

// ── OPT-DR1: Dirty-rect tracking ─────────────────────────────────────────────
// Crosshair moves every frame with the aim.  Track previous pixel-space centre
// so each frame only the union of (prev ∪ curr) crosshair bbox is dirty.
// All other HUD elements have fixed positions (config-driven); their dirty rect
// is pre-computed once in RefreshStaticHudDirty() and reused every frame.
// ─────────────────────────────────────────────────────────────────────────────

// Crosshair position from the previous frame (pixel space, after resetTransform).
// INT_MIN = not drawn yet / reset.
static int   s_chPrevPxCx = INT_MIN;
static int   s_chPrevPxCy = INT_MIN;
// Accumulated dirty rect for the crosshair in the current frame (prev ∪ curr pos).
static QRect s_chDirtyThisFrame;

// Static dirty rect: covers all enabled non-crosshair HUD elements.
// Recomputed in RefreshStaticHudDirty() whenever the config cache or transform changes.
static QRect  s_staticHudDirtyPx;
static QRect  s_staticHudFirstPersonDirtyPx;
static float  s_staticDirtyHudScale = -1.0f;
static float  s_staticDirtyTxDs    = 1e30f;
static float  s_staticDirtyTyDs    = 1e30f;
// Set to true by RefreshCachedConfig/RecomputeAnchorPositions to force recomputation.
static bool   s_staticDirtyStale   = true;

// Forward-declared; called from RefreshCachedConfig after crosshair geometry is final.
struct CrosshairHudConfig;
static void RebuildCrosshairOutlineStamp(const CrosshairHudConfig& ch);

struct TextMeasureCache {
    int  fontPixelSize;
    char text[64];
    int  width;
    int  height;
    bool valid;
};

struct TextBitmapCache {
    int    fontPixelSize;
    QColor color;
    char   text[64];
    int    originX;
    int    originY;
    int    expandR;  // 0 for normal text; >0 for dilated outline bitmaps
    float  renderScale; // scale at which bitmap was rendered (for hi-res outline)
    bool   valid;
    QImage bitmap;
};

// textDrawScale: visual scale applied when drawing (textScalePct/100). Returned
// dimensions are in pre-hudScale DS space so gauge/alignment math stays correct.
// outW/outH are float to preserve sub-pixel precision for centering calculations.
static inline void MeasureTextCached(const QFontMetrics& fm, int fontPixelSize,
                                     TextMeasureCache& cache, const char* text,
                                     float& outW, float& outH, float textDrawScale = 1.0f)
{
    if (!cache.valid || cache.fontPixelSize != fontPixelSize || std::strcmp(cache.text, text) != 0) {
        cache.width = fm.horizontalAdvance(QString::fromUtf8(text));
        cache.height = fm.height();
        std::strncpy(cache.text, text, sizeof(cache.text) - 1);
        cache.text[sizeof(cache.text) - 1] = '\0';
        cache.fontPixelSize = fontPixelSize;
        cache.valid = true;
    }

    outW = cache.width  * textDrawScale;
    outH = cache.height * textDrawScale;
}

static inline void PrepareTextBitmapCached(const QFontMetrics& fm, const QFont& font,
                                           int fontPixelSize, TextBitmapCache& cache,
                                           const char* text, const QColor& color)
{
    if (!cache.valid || cache.fontPixelSize != fontPixelSize
        || cache.color != color || std::strcmp(cache.text, text) != 0)
    {
        const QString qtext = QString::fromUtf8(text);
        QRect bounds = fm.boundingRect(qtext);
        if (bounds.isEmpty())
            bounds = QRect(0, -fm.ascent(), 1, fm.height());

        cache.bitmap = QImage(bounds.width(), bounds.height(), QImage::Format_ARGB32_Premultiplied);
        cache.bitmap.fill(Qt::transparent);

        QPainter painter(&cache.bitmap);
        painter.setFont(font);
        painter.setPen(color);
        painter.drawText(QPoint(-bounds.left(), -bounds.top()), qtext);

        std::strncpy(cache.text, text, sizeof(cache.text) - 1);
        cache.text[sizeof(cache.text) - 1] = '\0';
        cache.fontPixelSize = fontPixelSize;
        cache.color = color;
        cache.originX = bounds.left();
        cache.originY = bounds.top();
        cache.renderScale = 1.0f;
        cache.valid = true;
    }
}

static inline void DrawCachedText(QPainter* p, const TextBitmapCache& cache, float x, float baselineY,
                                   float textDrawScale = 1.0f)
{
    if (!cache.valid || cache.bitmap.isNull()) return;
    if (textDrawScale == 1.0f) {
        p->drawImage(QPointF(x + cache.originX, baselineY + cache.originY), cache.bitmap);
    } else {
        const float ox = cache.originX * textDrawScale;
        const float oy = cache.originY * textDrawScale;
        const float dw = cache.bitmap.width()  * textDrawScale;
        const float dh = cache.bitmap.height() * textDrawScale;
        p->drawImage(QRectF(x + ox, baselineY + oy, dw, dh), cache.bitmap);
    }
}

// ── Outline bitmap generation ─────────────────────────────────────────────────
// Creates a dilated (expanded) version of the source alpha channel.
// Each pixel of the result is the outline color with alpha = max alpha within
// a box of radius R in the source bitmap. The result is R pixels wider on each
// side, so originX/Y are adjusted outward by R.
// This produces a gap-free, smooth outline regardless of thickness setting.

// P-12: Uses separable max-filter dilation for O(R) per pixel instead of O(R²).
static void BuildDilatedOutlineBitmap(TextBitmapCache& dst,
                                       const TextBitmapCache& src,
                                       const QColor& color, int R,
                                       float renderScale = 1.0f)
{
    if (renderScale > 1.0f && !src.bitmap.isNull()) {
        // Scale the source bitmap to output resolution, dilate at that
        // resolution with exactly `thickness` pixels, then store.
        // originX/Y are in native (6px font) space and will be converted
        // to output-pixel space so the draw routine can place them directly.
        const int hiW = std::max(1, (int)std::ceil(src.bitmap.width()  * renderScale));
        const int hiH = std::max(1, (int)std::ceil(src.bitmap.height() * renderScale));
        QImage scaled = src.bitmap.scaled(hiW, hiH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        scaled = scaled.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        DilateSeparableTinted(dst.bitmap, scaled, color, R);
        dst.originX     = (int)std::floor(src.originX * renderScale) - R;
        dst.originY     = (int)std::floor(src.originY * renderScale) - R;
    } else {
        DilateSeparableTinted(dst.bitmap, src.bitmap, color, R);
        dst.originX     = src.originX - R;
        dst.originY     = src.originY - R;
    }
    dst.expandR     = R;
    dst.renderScale = renderScale;
    dst.fontPixelSize = src.fontPixelSize;
    dst.color       = color;
    std::strncpy(dst.text, src.text, sizeof(dst.text) - 1);
    dst.text[sizeof(dst.text) - 1] = '\0';
    dst.valid       = true;
}

// thickness: desired outline width in output pixels.
// renderScale: tds * hudScale — the effective magnification from native bitmap
//              to final output pixels.  When > 1, the source bitmap is upscaled
//              before dilation so the outline is computed at output resolution.
static inline void PrepareOutlineBitmapCached(TextBitmapCache& outlineCache,
                                               const TextBitmapCache& mainCache,
                                               const QColor& outlineColor,
                                               int thickness,
                                               float renderScale = 1.0f)
{
    if (!mainCache.valid || mainCache.bitmap.isNull()) return;
    if (outlineCache.valid &&
        outlineCache.expandR       == thickness &&
        outlineCache.renderScale   == renderScale &&
        outlineCache.color         == outlineColor &&
        outlineCache.fontPixelSize == mainCache.fontPixelSize &&
        std::strcmp(outlineCache.text, mainCache.text) == 0) return;
    BuildDilatedOutlineBitmap(outlineCache, mainCache, outlineColor, thickness, renderScale);
}

// Draw text with a dilation-based outline (single pass — no gap, no jaggies).
// When the outline bitmap was rendered at hi-res (renderScale > 1), it is
// drawn directly in pixel coordinates (painter transform temporarily reset)
// so the dilation radius maps 1:1 to output pixels — no scaling artefacts.
static inline void DrawCachedTextOutlined(QPainter* p,
                                          const TextBitmapCache& cache,
                                          const TextBitmapCache& outlineCache,
                                          float x, float baselineY,
                                          float tds,
                                          float opacity, float outlineOpacity)
{
    if (!cache.valid || cache.bitmap.isNull()) return;
    if (outlineOpacity > 0.0f && outlineCache.valid && !outlineCache.bitmap.isNull()) {
        p->setOpacity(outlineOpacity);
        if (outlineCache.renderScale > 1.0f) {
            // Hi-res path: outline bitmap is at output resolution.
            // The normal text path draws at DS-space position:
            //   (x + originX*tds, baselineY + originY*tds)
            // and the painter transform scales that to pixels.
            // The hi-res outline bitmap's originX/Y are in output pixels
            // (= src.originX * renderScale - R).  To find the correct
            // pixel position we map the DS-space text anchor through
            // the painter transform and add the output-pixel origin offset.
            const QTransform& xf = p->transform();
            const float dsX = static_cast<float>(x);
            const float dsY = static_cast<float>(baselineY);
            // Map DS text anchor to output pixels
            const float anchorPxX = xf.m11() * dsX + xf.m21() * dsY + xf.dx();
            const float anchorPxY = xf.m12() * dsX + xf.m22() * dsY + xf.dy();
            // outlineCache.originX/Y are in output-pixel offset from the anchor
            const float pxX = anchorPxX + outlineCache.originX;
            const float pxY = anchorPxY + outlineCache.originY;
            const QTransform savedXform = p->transform();
            p->resetTransform();
            p->drawImage(QPointF(pxX, pxY), outlineCache.bitmap);
            p->setTransform(savedXform);
        } else {
            DrawCachedText(p, outlineCache, x, baselineY, tds);
        }
    }
    p->setOpacity(opacity < 1.0f ? opacity : 1.0f);
    DrawCachedText(p, cache, x, baselineY, tds);
    p->setOpacity(1.0f);
}

// Draw an image with a dilation-based outline.
// outlineIcon must be a DilateAndTintIconForOutline result: (sw+2R)×(sh+2R) pixels.
// expandDS = R / hudScale (DS-space expansion to match the R-pixel dilation).
static inline void DrawImageOutlined(QPainter* p,
                                     const QImage& icon,
                                     const QImage& outlineIcon,
                                     const QRectF& dst,
                                     float expandDS,
                                     float opacity, float outlineOpacity)
{
    if (icon.isNull()) return;
    if (outlineOpacity > 0.0f && !outlineIcon.isNull()) {
        p->setOpacity(outlineOpacity);
        p->drawImage(QRectF(dst.x() - expandDS, dst.y() - expandDS,
                            dst.width()  + expandDS * 2.0f,
                            dst.height() + expandDS * 2.0f),
                     outlineIcon);
    }
    p->setOpacity(opacity < 1.0f ? opacity : 1.0f);
    p->drawImage(dst, icon);
    p->setOpacity(1.0f);
}

