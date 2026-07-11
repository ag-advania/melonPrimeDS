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
// ── OPT-DR1 / OPT-DR2: Dirty-rect tracking ───────────────────────────────────
// OPT-DR1: the crosshair moves every frame with the aim, so its prev∪curr pixel
//          bbox is tracked separately and unioned into the frame dirty rect.
// OPT-DR2: every other HUD primitive (text, icons, gauges, radar frame) unions
//          its *actual* device-space draw rect into s_drawnDirtyPx as it is
//          drawn (funnelled through the low-level draw helpers below). This
//          replaces the old fixed conservative per-element padding boxes, which
//          were sized for the worst-case text scale (e.g. HP ±90×65 DS units →
//          ~1000×730 px at 1080p) and uploaded in full every frame. The actual
//          glyph/icon bbox is many times smaller, shrinking the per-frame
//          overlay clear + GL texSubImage upload proportionally. Correct by
//          construction: every visible pixel passes through one funnel, so the
//          accumulated rect is a superset of what changed; the Screen-side
//          prev∪curr clear/upload contract is unchanged, so appearance /
//          disappearance / move / resize are all handled by the caller's union.
// ─────────────────────────────────────────────────────────────────────────────

// Crosshair position from the previous frame (pixel space, after resetTransform).
// INT_MIN = not drawn yet / reset.
// Union an already-device-space rect into the frame dirty accumulator (+1px AA pad).
static inline void AccumDirtyDevPx(const QRectF& dev)
{
    if (dev.isEmpty()) return;
    s_drawnDirtyPx |= QRect(QPoint(static_cast<int>(std::floor(dev.left()))   - 1,
                                   static_cast<int>(std::floor(dev.top()))    - 1),
                            QPoint(static_cast<int>(std::ceil (dev.right()))  + 1,
                                   static_cast<int>(std::ceil (dev.bottom())) + 1));
}

// Map a DS-space rect through the painter transform and union the device bbox.
static inline void AccumDirtyDs(const QPainter* p, const QRectF& ds)
{
    if (ds.isEmpty()) return;
    const QTransform& xf = p->transform();
    qreal x0, y0, x1, y1, x2, y2, x3, y3;
    xf.map(ds.left(),  ds.top(),    &x0, &y0);
    xf.map(ds.right(), ds.top(),    &x1, &y1);
    xf.map(ds.left(),  ds.bottom(), &x2, &y2);
    xf.map(ds.right(), ds.bottom(), &x3, &y3);
    const qreal minx = std::min(std::min(x0, x1), std::min(x2, x3));
    const qreal maxx = std::max(std::max(x0, x1), std::max(x2, x3));
    const qreal miny = std::min(std::min(y0, y1), std::min(y2, y3));
    const qreal maxy = std::max(std::max(y0, y1), std::max(y2, y3));
    AccumDirtyDevPx(QRectF(QPointF(minx, miny), QPointF(maxx, maxy)));
}

static void NotifyCrosshairZoomNotDrawn()
{
    s_chCrosshairDrawnLastFrame = false;
    s_chDisplayZoom = 0.0f;
    s_chDisplayZoomFrame = 0xFFFFFFFFu;
    s_chAimFrame = 0xFFFFFFFFu;
    s_chZoomReticleState = {};
}

static void ResetCrosshairZoomDisplay(float target = 0.0f)
{
    s_chDisplayZoom = target;
    s_chDisplayZoomFrame = 0xFFFFFFFFu;
    s_chAimFrame = 0xFFFFFFFFu;
    s_chZoomReticleState = {};
    s_chCrosshairDrawnLastFrame = false;
}
struct CrosshairHudConfig;
static void RebuildCrosshairOutlineStamp(const CrosshairHudConfig& ch);

// Text caches are keyed by string/color/pixel-size — NOT by font family. When the
// selectable HUD font changes (HudFontMode/Family/File), EnsureHudFont() bumps this
// generation so every cached glyph bitmap/measurement is treated as a miss and rebuilt.

// Glyph render magnification (output px / native bitmap px). 1.0 = native bitmap upscaled at
// draw time (MPH pixel font). >1.0 = re-rasterise the glyph at output resolution so system/file
// fonts stay crisp after auto-scale/hudScale instead of being a blurry upscale.
// Computed event-driven in RefreshCachedConfig (config change / resize only) — never per frame.
// Output-resolution font + metrics for the hi-DPI text path, rebuilt alongside s_textRenderScale
// only when the font or render scale changes. Avoids constructing a QFont/QFontMetrics per glyph
// cache-miss (e.g. ammo ticking during fire).

struct TextMeasureCache {
    int  fontPixelSize;
    char text[64];
    int  width;
    int  height;
    bool valid;
    int  gen;        // s_textCacheGen at build time (font-family generation); last so
                     // existing positional aggregate initializers leave it 0.
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
    int    gen;      // s_textCacheGen at build time (font-family generation); last so
                     // existing positional aggregate initializers leave it 0.
};

struct HudElementTextCacheState {
    TextMeasureCache hpText{};
    TextBitmapCache hpBitmap{};
    TextBitmapCache hpOutline{};
    TextBitmapCache invText[9]{};
    TextBitmapCache invOutline[9]{};
    TextMeasureCache ammoText{};
    TextBitmapCache ammoBitmap{};
    TextBitmapCache ammoOutline{};
};

static HudElementTextCacheState& HudElementTextCaches()
{
    auto& slot = ActiveHudConfigState().textCacheState;
    if (!slot)
        slot = std::make_shared<HudElementTextCacheState>();
    return *std::static_pointer_cast<HudElementTextCacheState>(slot);
}

// textDrawScale: visual scale applied when drawing (textScalePct/100). Returned
// dimensions are in pre-hudScale DS space so gauge/alignment math stays correct.
// outW/outH are float to preserve sub-pixel precision for centering calculations.
static inline void MeasureTextCached(const QFontMetrics& fm, int fontPixelSize,
                                     TextMeasureCache& cache, const char* text,
                                     float& outW, float& outH, float textDrawScale = 1.0f)
{
    if (!cache.valid || cache.fontPixelSize != fontPixelSize || cache.gen != s_textCacheGen
        || std::strcmp(cache.text, text) != 0) {
        cache.width = fm.horizontalAdvance(QString::fromUtf8(text));
        cache.height = fm.height();
        std::strncpy(cache.text, text, sizeof(cache.text) - 1);
        cache.text[sizeof(cache.text) - 1] = '\0';
        cache.fontPixelSize = fontPixelSize;
        cache.gen = s_textCacheGen;
        cache.valid = true;
    }

    outW = cache.width  * textDrawScale;
    outH = cache.height * textDrawScale;
}

// Glyph bounds for the text bitmap. Underline/strikeout decorations sit outside the glyph ink
// box, so when the font carries them, expand to the full line box (ascent..descent, advance width)
// to avoid clipping. Bold/italic are already reflected in the styled QFontMetrics.
static inline QRect TextBitmapBounds(const QFontMetrics& m, const QFont& fnt, const QString& t)
{
    QRect b = m.boundingRect(t);
    if (fnt.underline() || fnt.strikeOut())
        b = b.united(QRect(0, -m.ascent(), std::max(1, m.horizontalAdvance(t)), m.height()));
    if (b.isEmpty())
        b = QRect(0, -m.ascent(), 1, m.height());
    return b;
}

static inline void PrepareTextBitmapCached(const QFontMetrics& fm, const QFont& font,
                                           int fontPixelSize, TextBitmapCache& cache,
                                           const char* text, const QColor& color)
{
    const float rs = s_textRenderScale;
    if (!cache.valid || cache.fontPixelSize != fontPixelSize || cache.gen != s_textCacheGen
        || cache.renderScale != rs
        || cache.color != color || std::strcmp(cache.text, text) != 0)
    {
        const QString qtext = QString::fromUtf8(text);

        // Hi-DPI path: rasterise the glyph at output resolution using the pre-built s_hiResFont /
        // s_hiResFm (constructed event-driven in RefreshCachedConfig, not here). originX/Y are then
        // in output-pixel space and DrawCachedText draws the bitmap 1:1. rs == 1 keeps native path.
        QRect bounds;
        if (rs > 1.0f) {
            bounds = TextBitmapBounds(s_hiResFm, s_hiResFont, qtext);

            cache.bitmap = QImage(bounds.width(), bounds.height(), QImage::Format_ARGB32_Premultiplied);
            cache.bitmap.fill(Qt::transparent);
            QPainter painter(&cache.bitmap);
            painter.setFont(s_hiResFont);
            painter.setPen(color);
            painter.drawText(QPoint(-bounds.left(), -bounds.top()), qtext);
        } else {
            bounds = TextBitmapBounds(fm, font, qtext);

            cache.bitmap = QImage(bounds.width(), bounds.height(), QImage::Format_ARGB32_Premultiplied);
            cache.bitmap.fill(Qt::transparent);
            QPainter painter(&cache.bitmap);
            painter.setFont(font);
            painter.setPen(color);
            painter.drawText(QPoint(-bounds.left(), -bounds.top()), qtext);
        }

        std::strncpy(cache.text, text, sizeof(cache.text) - 1);
        cache.text[sizeof(cache.text) - 1] = '\0';
        cache.fontPixelSize = fontPixelSize;
        cache.color = color;
        cache.originX = bounds.left();
        cache.originY = bounds.top();
        cache.renderScale = rs;
        cache.gen = s_textCacheGen;
        cache.valid = true;
    }
}

static inline void DrawCachedText(QPainter* p, const TextBitmapCache& cache, float x, float baselineY,
                                   float textDrawScale = 1.0f)
{
    if (!cache.valid || cache.bitmap.isNull()) return;
    if (cache.renderScale > 1.0f) {
        // Hi-DPI: bitmap was rasterised at output resolution; draw it 1:1 in pixel space so the
        // glyphs stay crisp (no upscaling). Map the DS-space anchor through the painter transform,
        // then add the output-pixel origin. Mirrors the hi-res outline path.
        const QTransform& xf = p->transform();
        const float anchorPxX = xf.m11() * x + xf.m21() * baselineY + xf.dx();
        const float anchorPxY = xf.m12() * x + xf.m22() * baselineY + xf.dy();
        const QTransform saved = p->transform();
        p->resetTransform();
        p->drawImage(QPointF(anchorPxX + cache.originX, anchorPxY + cache.originY), cache.bitmap);
        p->setTransform(saved);
        AccumDirtyDevPx(QRectF(anchorPxX + cache.originX, anchorPxY + cache.originY,
                               cache.bitmap.width(), cache.bitmap.height())); // OPT-DR2
        return;
    }
    if (textDrawScale == 1.0f) {
        p->drawImage(QPointF(x + cache.originX, baselineY + cache.originY), cache.bitmap);
        AccumDirtyDs(p, QRectF(x + cache.originX, baselineY + cache.originY,
                               cache.bitmap.width(), cache.bitmap.height())); // OPT-DR2
    } else {
        const float ox = cache.originX * textDrawScale;
        const float oy = cache.originY * textDrawScale;
        const float dw = cache.bitmap.width()  * textDrawScale;
        const float dh = cache.bitmap.height() * textDrawScale;
        p->drawImage(QRectF(x + ox, baselineY + oy, dw, dh), cache.bitmap);
        AccumDirtyDs(p, QRectF(x + ox, baselineY + oy, dw, dh)); // OPT-DR2
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
    float effRs = renderScale;
    if (src.renderScale > 1.0f && !src.bitmap.isNull()) {
        // Hi-DPI main text: src is already at output resolution (originX/Y in output px).
        // Dilate it directly — no extra upscale — and inherit its render scale so the draw
        // routine takes the hi-res 1:1 path.
        DilateSeparableTinted(dst.bitmap, src.bitmap, color, R);
        dst.originX     = src.originX - R;
        dst.originY     = src.originY - R;
        effRs           = src.renderScale;
    } else if (renderScale > 1.0f && !src.bitmap.isNull()) {
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
    dst.renderScale = effRs;
    dst.fontPixelSize = src.fontPixelSize;
    dst.color       = color;
    dst.gen         = src.gen;
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
    // When the main text is hi-DPI, the outline inherits the main cache's render scale
    // (it dilates the already-output-res bitmap), so compare against that for cache validity.
    const float effRs = (mainCache.renderScale > 1.0f) ? mainCache.renderScale : renderScale;
    if (outlineCache.valid &&
        outlineCache.expandR       == thickness &&
        outlineCache.renderScale   == effRs &&
        outlineCache.color         == outlineColor &&
        outlineCache.fontPixelSize == mainCache.fontPixelSize &&
        outlineCache.gen           == mainCache.gen &&
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
            AccumDirtyDevPx(QRectF(pxX, pxY, outlineCache.bitmap.width(),
                                   outlineCache.bitmap.height())); // OPT-DR2
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
        const QRectF olRect(dst.x() - expandDS, dst.y() - expandDS,
                            dst.width()  + expandDS * 2.0f,
                            dst.height() + expandDS * 2.0f);
        p->setOpacity(outlineOpacity);
        p->drawImage(olRect, outlineIcon);
        AccumDirtyDs(p, olRect); // OPT-DR2 (covers the icon rect too)
    } else {
        AccumDirtyDs(p, dst); // OPT-DR2
    }
    p->setOpacity(opacity < 1.0f ? opacity : 1.0f);
    p->drawImage(dst, icon);
    p->setOpacity(1.0f);
}

