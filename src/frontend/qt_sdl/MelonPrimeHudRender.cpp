#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeHudRender.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeCompilerHints.h"
#include "MelonPrimeConstants.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "Config.h"
#include "toml/toml.hpp"
#include "MelonPrime.h"

#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QColorDialog>
#include <QInputDialog>
#include <string>
#include <map>
#include <set>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace MelonPrime {

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

// Bomb icon cache — 4 icons (index 0-3 = bombs remaining)
static QImage s_bombIcons[4];
static QImage s_bombTintedIcons[4];
static QColor s_bombTintColor;
static int    s_bombIconHeight = 0;
static bool   s_bombTintCacheValid = false;

// Outline icon caches — forward-declared here so EnsureBombIconsLoaded can invalidate
static QImage s_weaponIconsOutline[9];
static QImage s_bombIconsOutline[4];
static QColor s_outlineTintColor;       // invalid QColor = needs regeneration
static int    s_outlineExpandR     = -1;
static int    s_outlineBaseIconH   = -1; // s_weaponIconHeight at last outline-gen

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

static void EnsureBombIconsLoaded(int dsH = 12, float hudScale = 1.0f)
{
    int targetH = qMax(1, (int)std::ceil((float)dsH * hudScale));
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
    s_outlineTintColor = QColor(); // invalidate outline cache
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

static void EnsureIconsLoaded(int dsH = 16, float hudScale = 1.0f)
{
    int targetH = qMax(1, (int)std::ceil((float)dsH * hudScale));
    if (LIKELY(s_weaponIconHeight == targetH && s_weaponIconHeight != 0)) return;
    static const char* kIconPaths[9] = {
        ":/mph-icon-pb", ":/mph-icon-volt", ":/mph-icon-missile",
        ":/mph-icon-battlehammer", ":/mph-icon-imperialist",
        ":/mph-icon-judicator", ":/mph-icon-magmaul",
        ":/mph-icon-shock", ":/mph-icon-omega"
    };
    for (int i = 0; i < 9; i++) {
        s_weaponIcons[i] = loadSvgToHeight(kIconPaths[i], targetH);
        s_weaponTintedIcons[i] = s_weaponIcons[i];
        s_weaponTintValid[i] = false;
    }
    s_weaponIconHeight = targetH;
    s_outlineTintColor = QColor(); // invalidate outline cache
}

// ── Outline icon dilation ────────────────────────────────────────────────────
// Applies max-dilation (same algorithm as BuildDilatedOutlineBitmap) to an icon.
// Output image is (sw+2R) × (sh+2R), filled with outlineColor at dilated alpha.
// Icon images are at output-pixel scale, so R == thickness (output pixels).
static QImage DilateAndTintIconForOutline(const QImage& src, const QColor& col, int R)
{
    if (src.isNull()) return src;
    const QImage s = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int sw = s.width(), sh = s.height();
    const int dw = sw + R * 2, dh = sh + R * 2;
    QImage dst(dw, dh, QImage::Format_ARGB32_Premultiplied);
    dst.fill(Qt::transparent);
    const int cr = col.red(), cg = col.green(), cb = col.blue();
    for (int dy = 0; dy < dh; ++dy) {
        QRgb* dstRow = reinterpret_cast<QRgb*>(dst.scanLine(dy));
        for (int dx = 0; dx < dw; ++dx) {
            const int sx0 = dx - R, sy0 = dy - R;
            int maxA = 0;
            for (int ky = -R; ky <= R && maxA < 255; ++ky) {
                const int sy = sy0 + ky;
                if (sy < 0 || sy >= sh) continue;
                const QRgb* srcRow = reinterpret_cast<const QRgb*>(s.constScanLine(sy));
                for (int kx = -R; kx <= R && maxA < 255; ++kx) {
                    const int sx = sx0 + kx;
                    if (sx < 0 || sx >= sw) continue;
                    const int a = qAlpha(srcRow[sx]);
                    if (a > maxA) maxA = a;
                }
            }
            if (maxA > 0)
                dstRow[dx] = qRgba(cr * maxA / 255, cg * maxA / 255, cb * maxA / 255, maxA);
        }
    }
    return dst;
}

// Regenerates dilated outline icon caches when color, expandR, or icon size changes.
// expandR == thickness in output pixels (icon image pixels == output pixels).
static void EnsureOutlineIconsUpdated(const QColor& outlineColor, int expandR)
{
    if (s_outlineTintColor == outlineColor &&
        s_outlineExpandR   == expandR      &&
        s_outlineBaseIconH == s_weaponIconHeight) return;
    s_outlineTintColor   = outlineColor;
    s_outlineExpandR     = expandR;
    s_outlineBaseIconH   = s_weaponIconHeight;
    for (int i = 0; i < 9; ++i)
        s_weaponIconsOutline[i] = DilateAndTintIconForOutline(s_weaponIcons[i], outlineColor, expandR);
    for (int i = 0; i < 4; ++i)
        s_bombIconsOutline[i] = DilateAndTintIconForOutline(s_bombIcons[i], outlineColor, expandR);
}

// =========================================================================
//  P-2: Static outline buffer — allocated once, fill(transparent) per frame.
//       Eliminates 196 KB heap alloc+dealloc every frame.
// =========================================================================
static QImage s_outlineBuf;

static QImage& GetOutlineBuffer(int w, int h)
{
    if (UNLIKELY(s_outlineBuf.isNull() || s_outlineBuf.width() != w || s_outlineBuf.height() != h))
        s_outlineBuf = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    return s_outlineBuf;
}

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
    bool   valid;
    QImage bitmap;
};

// textDrawScale: visual scale applied when drawing (textScalePct/100). Returned
// dimensions are in pre-hudScale DS space so gauge/alignment math stays correct.
static inline void MeasureTextCached(const QFontMetrics& fm, int fontPixelSize,
                                     TextMeasureCache& cache, const char* text,
                                     int& outW, int& outH, float textDrawScale = 1.0f)
{
    if (!cache.valid || cache.fontPixelSize != fontPixelSize || std::strcmp(cache.text, text) != 0) {
        cache.width = fm.horizontalAdvance(QString::fromUtf8(text));
        cache.height = fm.height();
        std::strncpy(cache.text, text, sizeof(cache.text) - 1);
        cache.text[sizeof(cache.text) - 1] = '\0';
        cache.fontPixelSize = fontPixelSize;
        cache.valid = true;
    }

    outW = static_cast<int>(cache.width  * textDrawScale);
    outH = static_cast<int>(cache.height * textDrawScale);
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
        cache.valid = true;
    }
}

static inline void DrawCachedText(QPainter* p, const TextBitmapCache& cache, int x, int baselineY,
                                   float textDrawScale = 1.0f)
{
    if (!cache.valid || cache.bitmap.isNull()) return;
    if (textDrawScale == 1.0f) {
        p->drawImage(QPoint(x + cache.originX, baselineY + cache.originY), cache.bitmap);
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

static void BuildDilatedOutlineBitmap(TextBitmapCache& dst,
                                       const TextBitmapCache& src,
                                       const QColor& color, int R)
{
    const QImage& s = src.bitmap;
    const int sw = s.width(), sh = s.height();
    const int dw = sw + R * 2, dh = sh + R * 2;

    dst.bitmap = QImage(dw, dh, QImage::Format_ARGB32_Premultiplied);
    dst.bitmap.fill(Qt::transparent);

    const int cr = color.red(), cg = color.green(), cb = color.blue();

    for (int dy = 0; dy < dh; ++dy) {
        QRgb* dstRow = reinterpret_cast<QRgb*>(dst.bitmap.scanLine(dy));
        for (int dx = 0; dx < dw; ++dx) {
            const int sx0 = dx - R, sy0 = dy - R;
            int maxA = 0;
            for (int ky = -R; ky <= R && maxA < 255; ++ky) {
                const int sy = sy0 + ky;
                if (sy < 0 || sy >= sh) continue;
                const QRgb* srcRow = reinterpret_cast<const QRgb*>(s.constScanLine(sy));
                for (int kx = -R; kx <= R && maxA < 255; ++kx) {
                    const int sx = sx0 + kx;
                    if (sx < 0 || sx >= sw) continue;
                    const int a = qAlpha(srcRow[sx]);
                    if (a > maxA) maxA = a;
                }
            }
            if (maxA > 0)
                dstRow[dx] = qRgba(cr * maxA / 255, cg * maxA / 255, cb * maxA / 255, maxA);
        }
    }

    // Adjust origin to account for the expansion
    dst.originX     = src.originX - R;
    dst.originY     = src.originY - R;
    dst.expandR     = R;
    dst.fontPixelSize = src.fontPixelSize;
    dst.color       = color;
    std::strncpy(dst.text, src.text, sizeof(dst.text) - 1);
    dst.text[sizeof(dst.text) - 1] = '\0';
    dst.valid       = true;
}

// expandR: how many native font-bitmap pixels to expand.
// Computed as max(1, ceil(thickness / hudScale)) so that 1 thickness = ~1 output pixel.
static inline void PrepareOutlineBitmapCached(TextBitmapCache& outlineCache,
                                               const TextBitmapCache& mainCache,
                                               const QColor& outlineColor, int expandR)
{
    if (!mainCache.valid || mainCache.bitmap.isNull()) return;
    if (outlineCache.valid &&
        outlineCache.expandR       == expandR &&
        outlineCache.color         == outlineColor &&
        outlineCache.fontPixelSize == mainCache.fontPixelSize &&
        std::strcmp(outlineCache.text, mainCache.text) == 0) return;
    BuildDilatedOutlineBitmap(outlineCache, mainCache, outlineColor, expandR);
}

// Draw text with a dilation-based outline (single pass — no gap, no jaggies).
static inline void DrawCachedTextOutlined(QPainter* p,
                                          const TextBitmapCache& cache,
                                          const TextBitmapCache& outlineCache,
                                          int x, int baselineY,
                                          float tds,
                                          float opacity, float outlineOpacity)
{
    if (!cache.valid || cache.bitmap.isNull()) return;
    if (outlineOpacity > 0.0f && outlineCache.valid && !outlineCache.bitmap.isNull()) {
        p->setOpacity(outlineOpacity);
        DrawCachedText(p, outlineCache, x, baselineY, tds);
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

// =========================================================================
//  P-3: Cached HUD config — refreshed only when config generation changes.
//       Avoids ~50 hash-map lookups per frame → single generation compare.
// =========================================================================
struct HpHudConfig {
    int hpX, hpY, hpAlign;          // final (computed from anchor + offset + stretch)
    int hpAnchor, hpOfsX, hpOfsY;   // raw config values
    char hpPrefix[48];
    bool hpTextAutoColor, hpGauge, hpAutoColor;
    QColor hpTextColor;
    int hpGaugeOri, hpGaugeLen, hpGaugeWid;
    int hpGaugeOfsX, hpGaugeOfsY, hpGaugeAnchor;
    int hpGaugePosMode, hpGaugePosX, hpGaugePosY;  // final
    int hpGaugePosAnchor, hpGaugePosOfsX, hpGaugePosOfsY;  // raw
    QColor hpGaugeColor;
};
struct WeaponHudConfig {
    int wpnX, wpnY, ammoAlign;           // final
    int wpnAnchor, wpnOfsX, wpnOfsY;     // raw
    char ammoPrefix[48];
    QColor ammoTextColor;
    bool iconShow, ammoGauge;
    bool iconOverlayEnable[9];
    QColor iconOverlayColor[9];
    int iconHeight;
    int iconMode, iconOfsX, iconOfsY, iconPosX, iconPosY;  // iconPosX/Y = final
    int iconPosAnchor, iconPosOfsX, iconPosOfsY;            // raw
    int iconAnchorX, iconAnchorY;
    int ammoGaugeOri, ammoGaugeLen, ammoGaugeWid;
    int ammoGaugeOfsX, ammoGaugeOfsY, ammoGaugeAnchor;
    int ammoGaugePosMode, ammoGaugePosX, ammoGaugePosY;     // final
    int ammoGaugePosAnchor, ammoGaugePosOfsX, ammoGaugePosOfsY;  // raw
    QColor ammoGaugeColor;
};
struct CrosshairHudConfig {
    QColor chColor;
    bool chOutline, chCenterDot, chTStyle;
    double chOutlineOpacity, chDotOpacity;
    int chOutlineThickness, chDotThickness;
    bool chInnerShow;
    double chInnerOpacity;
    int chInnerLengthX, chInnerLengthY, chInnerThickness, chInnerOffset;
    bool chOuterShow;
    double chOuterOpacity;
    int chOuterLengthX, chOuterLengthY, chOuterThickness, chOuterOffset;
    double chScale; // visual scale multiplier (1.0 = 100%)
};
struct MatchStatusHudConfig {
    bool matchStatusShow;
    int matchStatusX, matchStatusY;                     // final
    int matchStatusAnchor, matchStatusOfsX, matchStatusOfsY;  // raw
    int matchStatusLabelOfsX, matchStatusLabelOfsY;
    int matchStatusLabelPos;
    char matchStatusLabelPoints[64], matchStatusLabelOctoliths[64], matchStatusLabelLives[64];
    char matchStatusLabelRingTime[64], matchStatusLabelPrimeTime[64];
    QColor matchStatusColor, matchStatusLabelColor, matchStatusValueColor, matchStatusSepColor, matchStatusGoalColor;
};
struct BombLeftHudConfig {
    bool bombLeftShow, bombLeftTextShow;
    int bombLeftX, bombLeftY, bombLeftAlign;                  // final
    int bombLeftAnchor, bombLeftOfsX, bombLeftOfsY;            // raw
    QColor bombLeftColor;
    char bombLeftPrefix[48];
    char bombLeftSuffix[48];
    bool bombIconShow, bombIconColorOverlay;
    QColor bombIconColor;
    int bombIconHeight;
    int bombIconMode, bombIconOfsX, bombIconOfsY, bombIconPosX, bombIconPosY;  // iconPosX/Y = final
    int bombIconPosAnchor, bombIconPosOfsX, bombIconPosOfsY;                    // raw
    int bombIconAnchorX, bombIconAnchorY;
};
struct RankTimeHudConfig {
    bool rankShow;
    int rankX, rankY, rankAlign;                       // final
    int rankAnchor, rankOfsX, rankOfsY;                // raw
    QColor rankColor;
    char rankPrefix[48];
    bool rankShowOrdinal;
    char rankSuffix[48];
    bool timeLeftShow;
    int timeLeftX, timeLeftY, timeLeftAlign;            // final
    int timeLeftAnchor, timeLeftOfsX, timeLeftOfsY;     // raw
    QColor timeLeftColor;
    bool timeLimitShow;
    int timeLimitX, timeLimitY, timeLimitAlign;         // final
    int timeLimitAnchor, timeLimitOfsX, timeLimitOfsY;  // raw
    QColor timeLimitColor;
};
struct HudOutlineConfig {
    bool   enable;
    QColor color;
    float  opacity;
    int    thickness;
};
struct RadarOverlayConfig {
    bool radarShow;
    int radarDstX, radarDstY, radarDstSize;             // final
    int radarAnchor, radarOfsX, radarOfsY;               // raw
    int radarSrcRadius;
    double radarOpacity;
    QRect radarDstRect;
    QPainterPath radarClipPath;
};
struct CachedHudConfig {
    HpHudConfig hp;
    WeaponHudConfig weapon;
    CrosshairHudConfig crosshair;
    MatchStatusHudConfig matchStatus;
    BombLeftHudConfig bombLeft;
    RankTimeHudConfig rankTime;
    RadarOverlayConfig radar;
    HudOutlineConfig outline;
    int textScalePct; // text visual scale in percent (100 = 1×, bitmap always rendered at 6px)
    float lastStretchX; // topStretchX used for last anchor position computation
    float lastHudScale;  // hudScale (scaleY) at last render — needed for outline thickness conversion
    // Per-element opacity values (cached to avoid per-frame config lookups)
    float hpOpacity, weaponOpacity, matchStatusOpacity, rankOpacity, bombLeftOpacity;
    float hpGaugeOpacity, wpnIconOpacity, ammoGaugeOpacity;
    float bombIconOpacity, timeLeftOpacity, timeLimitOpacity;
    bool valid;
};
static CachedHudConfig s_cache = { .valid = false };
static uint32_t s_cacheEpoch = 1;

static inline void CopyConfigString(char* dst, size_t dstSize, const std::string& value)
{
    std::strncpy(dst, value.c_str(), dstSize - 1);
    dst[dstSize - 1] = '\0';
}
static inline QColor ReadRgbColor(Config::Table& cfg, const char* keyR, const char* keyG, const char* keyB)
{
    return QColor(cfg.GetInt(keyR), cfg.GetInt(keyG), cfg.GetInt(keyB));
}
static inline QColor ReadOptionalSubColor(Config::Table& cfg, const char* keyOverall,
                                          const char* keyR, const char* keyG, const char* keyB)
{
    if (cfg.GetBool(keyOverall)) return QColor();
    return ReadRgbColor(cfg, keyR, keyG, keyB);
}

// ApplyAnchor: converts anchor index + offset into final coordinates.
// anchor 0=TL 1=TC 2=TR 3=ML 4=MC 5=MR 6=BL 7=BC 8=BR
// topStretchX = scaleX/scaleY: >1 when wider than 4:3, <1 when narrower.
// Adjusts X anchor bases so left/right edges track the actual visible area.
// With the painter set to scale(hudScale,hudScale) + translate((topStretchX-1)*128, 0),
// the visible X range in DS coords is [(1-sx)*128 .. 128*(sx+1)].
static inline void ApplyAnchor(int anchor, int offsetX, int offsetY,
                                int& outX, int& outY, float topStretchX = 1.0f)
{
    static constexpr int H = 192;
    const int col = anchor % 3;
    const float baseX = (col == 0) ? -(topStretchX - 1.0f) * 128.0f
                      : (col == 1) ?  128.0f
                      :               128.0f * (topStretchX + 1.0f);
    const int baseY = (anchor / 3 == 0) ? 0 : (anchor / 3 == 1) ? H / 2 : H;
    outX = static_cast<int>(baseX) + offsetX;
    outY = baseY + offsetY;
}

static void LoadHpConfig(HpHudConfig& hp, Config::Table& cfg)
{
    hp.hpAnchor = cfg.GetInt("Metroid.Visual.HudHpAnchor");
    hp.hpOfsX = cfg.GetInt("Metroid.Visual.HudHpX");
    hp.hpOfsY = cfg.GetInt("Metroid.Visual.HudHpY");
    hp.hpAlign = cfg.GetInt("Metroid.Visual.HudHpAlign");
    CopyConfigString(hp.hpPrefix, sizeof(hp.hpPrefix), cfg.GetString("Metroid.Visual.HudHpPrefix"));
    hp.hpTextAutoColor = cfg.GetBool("Metroid.Visual.HudHpTextAutoColor");
    hp.hpTextColor = ReadRgbColor(cfg, "Metroid.Visual.HudHpTextColorR", "Metroid.Visual.HudHpTextColorG", "Metroid.Visual.HudHpTextColorB");
    hp.hpGauge = cfg.GetBool("Metroid.Visual.HudHpGauge");
    hp.hpGaugeOri = cfg.GetInt("Metroid.Visual.HudHpGaugeOrientation");
    hp.hpGaugeLen = cfg.GetInt("Metroid.Visual.HudHpGaugeLength");
    hp.hpGaugeWid = cfg.GetInt("Metroid.Visual.HudHpGaugeWidth");
    hp.hpGaugeOfsX = cfg.GetInt("Metroid.Visual.HudHpGaugeOffsetX");
    hp.hpGaugeOfsY = cfg.GetInt("Metroid.Visual.HudHpGaugeOffsetY");
    hp.hpGaugeAnchor = cfg.GetInt("Metroid.Visual.HudHpGaugeAnchor");
    hp.hpGaugePosMode = cfg.GetInt("Metroid.Visual.HudHpGaugePosMode");
    hp.hpGaugePosAnchor = cfg.GetInt("Metroid.Visual.HudHpGaugePosAnchor");
    hp.hpGaugePosOfsX = cfg.GetInt("Metroid.Visual.HudHpGaugePosX");
    hp.hpGaugePosOfsY = cfg.GetInt("Metroid.Visual.HudHpGaugePosY");
    hp.hpAutoColor = cfg.GetBool("Metroid.Visual.HudHpGaugeAutoColor");
    hp.hpGaugeColor = ReadRgbColor(cfg, "Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB");
}
static void LoadWeaponConfig(WeaponHudConfig& weapon, Config::Table& cfg)
{
    weapon.wpnAnchor = cfg.GetInt("Metroid.Visual.HudWeaponAnchor");
    weapon.wpnOfsX = cfg.GetInt("Metroid.Visual.HudWeaponX");
    weapon.wpnOfsY = cfg.GetInt("Metroid.Visual.HudWeaponY");
    weapon.ammoAlign = cfg.GetInt("Metroid.Visual.HudAmmoAlign");
    CopyConfigString(weapon.ammoPrefix, sizeof(weapon.ammoPrefix), cfg.GetString("Metroid.Visual.HudAmmoPrefix"));
    weapon.ammoTextColor = ReadRgbColor(cfg, "Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB");
    weapon.iconShow = cfg.GetBool("Metroid.Visual.HudWeaponIconShow");
    {
        static const char* kWpnNames[9] = {
            "PowerBeam","VoltDriver","Missile","BattleHammer",
            "Imperialist","Judicator","Magmaul","ShockCoil","OmegaCannon"
        };
        for (int i = 0; i < 9; i++) {
            char kE[80], kR[80], kG[80], kB[80];
            std::snprintf(kE, sizeof(kE), "Metroid.Visual.HudWeaponIconColorOverlay%s", kWpnNames[i]);
            std::snprintf(kR, sizeof(kR), "Metroid.Visual.HudWeaponIconOverlayColorR%s", kWpnNames[i]);
            std::snprintf(kG, sizeof(kG), "Metroid.Visual.HudWeaponIconOverlayColorG%s", kWpnNames[i]);
            std::snprintf(kB, sizeof(kB), "Metroid.Visual.HudWeaponIconOverlayColorB%s", kWpnNames[i]);
            weapon.iconOverlayEnable[i] = cfg.GetBool(kE);
            weapon.iconOverlayColor[i]  = ReadRgbColor(cfg, kR, kG, kB);
        }
    }
    weapon.iconHeight = cfg.GetInt("Metroid.Visual.HudWeaponIconHeight");
    weapon.iconMode = cfg.GetInt("Metroid.Visual.HudWeaponIconMode");
    weapon.iconOfsX = cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetX");
    weapon.iconOfsY = cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetY");
    weapon.iconPosAnchor = cfg.GetInt("Metroid.Visual.HudWeaponIconPosAnchor");
    weapon.iconPosOfsX = cfg.GetInt("Metroid.Visual.HudWeaponIconPosX");
    weapon.iconPosOfsY = cfg.GetInt("Metroid.Visual.HudWeaponIconPosY");
    weapon.iconAnchorX = cfg.GetInt("Metroid.Visual.HudWeaponIconAnchorX");
    weapon.iconAnchorY = cfg.GetInt("Metroid.Visual.HudWeaponIconAnchorY");
    weapon.ammoGauge = cfg.GetBool("Metroid.Visual.HudAmmoGauge");
    weapon.ammoGaugeOri = cfg.GetInt("Metroid.Visual.HudAmmoGaugeOrientation");
    weapon.ammoGaugeLen = cfg.GetInt("Metroid.Visual.HudAmmoGaugeLength");
    weapon.ammoGaugeWid = cfg.GetInt("Metroid.Visual.HudAmmoGaugeWidth");
    weapon.ammoGaugeOfsX = cfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetX");
    weapon.ammoGaugeOfsY = cfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetY");
    weapon.ammoGaugeAnchor = cfg.GetInt("Metroid.Visual.HudAmmoGaugeAnchor");
    weapon.ammoGaugePosMode = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosMode");
    weapon.ammoGaugePosAnchor = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosAnchor");
    weapon.ammoGaugePosOfsX = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosX");
    weapon.ammoGaugePosOfsY = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosY");
    weapon.ammoGaugeColor = ReadRgbColor(cfg, "Metroid.Visual.HudAmmoGaugeColorR", "Metroid.Visual.HudAmmoGaugeColorG", "Metroid.Visual.HudAmmoGaugeColorB");
}
static void LoadCrosshairConfig(CrosshairHudConfig& crosshair, Config::Table& cfg)
{
    crosshair.chColor = ReadRgbColor(cfg, "Metroid.Visual.CrosshairColorR", "Metroid.Visual.CrosshairColorG", "Metroid.Visual.CrosshairColorB");
    crosshair.chOutline = cfg.GetBool("Metroid.Visual.CrosshairOutline");
    crosshair.chOutlineOpacity = cfg.GetDouble("Metroid.Visual.CrosshairOutlineOpacity");
    crosshair.chOutlineThickness = cfg.GetInt("Metroid.Visual.CrosshairOutlineThickness"); if (crosshair.chOutlineThickness <= 0) crosshair.chOutlineThickness = 1;
    crosshair.chCenterDot = cfg.GetBool("Metroid.Visual.CrosshairCenterDot");
    crosshair.chDotOpacity = cfg.GetDouble("Metroid.Visual.CrosshairDotOpacity");
    crosshair.chDotThickness = cfg.GetInt("Metroid.Visual.CrosshairDotThickness"); if (crosshair.chDotThickness <= 0) crosshair.chDotThickness = 1;
    crosshair.chTStyle = cfg.GetBool("Metroid.Visual.CrosshairTStyle");
    crosshair.chInnerShow = cfg.GetBool("Metroid.Visual.CrosshairInnerShow");
    crosshair.chInnerOpacity = cfg.GetDouble("Metroid.Visual.CrosshairInnerOpacity");
    crosshair.chInnerLengthX = cfg.GetInt("Metroid.Visual.CrosshairInnerLengthX");
    crosshair.chInnerLengthY = cfg.GetInt("Metroid.Visual.CrosshairInnerLengthY");
    crosshair.chInnerThickness = cfg.GetInt("Metroid.Visual.CrosshairInnerThickness"); if (crosshair.chInnerThickness <= 0) crosshair.chInnerThickness = 1;
    crosshair.chInnerOffset = cfg.GetInt("Metroid.Visual.CrosshairInnerOffset");
    crosshair.chOuterShow = cfg.GetBool("Metroid.Visual.CrosshairOuterShow");
    crosshair.chOuterOpacity = cfg.GetDouble("Metroid.Visual.CrosshairOuterOpacity");
    crosshair.chOuterLengthX = cfg.GetInt("Metroid.Visual.CrosshairOuterLengthX");
    crosshair.chOuterLengthY = cfg.GetInt("Metroid.Visual.CrosshairOuterLengthY");
    crosshair.chOuterThickness = cfg.GetInt("Metroid.Visual.CrosshairOuterThickness"); if (crosshair.chOuterThickness <= 0) crosshair.chOuterThickness = 1;
    crosshair.chOuterOffset = cfg.GetInt("Metroid.Visual.CrosshairOuterOffset");
    crosshair.chScale = cfg.GetInt("Metroid.Visual.CrosshairScale") / 100.0;
    if (crosshair.chScale <= 0.0) crosshair.chScale = 1.0;
}
static void LoadMatchStatusConfig(MatchStatusHudConfig& matchStatus, Config::Table& cfg)
{
    matchStatus.matchStatusShow = cfg.GetBool("Metroid.Visual.HudMatchStatusShow");
    matchStatus.matchStatusAnchor = cfg.GetInt("Metroid.Visual.HudMatchStatusAnchor");
    matchStatus.matchStatusOfsX = cfg.GetInt("Metroid.Visual.HudMatchStatusX");
    matchStatus.matchStatusOfsY = cfg.GetInt("Metroid.Visual.HudMatchStatusY");
    matchStatus.matchStatusLabelOfsX = cfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsX");
    matchStatus.matchStatusLabelOfsY = cfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsY");
    matchStatus.matchStatusLabelPos = cfg.GetInt("Metroid.Visual.HudMatchStatusLabelPos");
    CopyConfigString(matchStatus.matchStatusLabelPoints, sizeof(matchStatus.matchStatusLabelPoints), cfg.GetString("Metroid.Visual.HudMatchStatusLabelPoints"));
    CopyConfigString(matchStatus.matchStatusLabelOctoliths, sizeof(matchStatus.matchStatusLabelOctoliths), cfg.GetString("Metroid.Visual.HudMatchStatusLabelOctoliths"));
    CopyConfigString(matchStatus.matchStatusLabelLives, sizeof(matchStatus.matchStatusLabelLives), cfg.GetString("Metroid.Visual.HudMatchStatusLabelLives"));
    CopyConfigString(matchStatus.matchStatusLabelRingTime, sizeof(matchStatus.matchStatusLabelRingTime), cfg.GetString("Metroid.Visual.HudMatchStatusLabelRingTime"));
    CopyConfigString(matchStatus.matchStatusLabelPrimeTime, sizeof(matchStatus.matchStatusLabelPrimeTime), cfg.GetString("Metroid.Visual.HudMatchStatusLabelPrimeTime"));
    matchStatus.matchStatusColor = ReadRgbColor(cfg, "Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB");
    matchStatus.matchStatusLabelColor = ReadOptionalSubColor(cfg, "Metroid.Visual.HudMatchStatusLabelColorOverall", "Metroid.Visual.HudMatchStatusLabelColorR", "Metroid.Visual.HudMatchStatusLabelColorG", "Metroid.Visual.HudMatchStatusLabelColorB");
    matchStatus.matchStatusValueColor = ReadOptionalSubColor(cfg, "Metroid.Visual.HudMatchStatusValueColorOverall", "Metroid.Visual.HudMatchStatusValueColorR", "Metroid.Visual.HudMatchStatusValueColorG", "Metroid.Visual.HudMatchStatusValueColorB");
    matchStatus.matchStatusSepColor = ReadOptionalSubColor(cfg, "Metroid.Visual.HudMatchStatusSepColorOverall", "Metroid.Visual.HudMatchStatusSepColorR", "Metroid.Visual.HudMatchStatusSepColorG", "Metroid.Visual.HudMatchStatusSepColorB");
    matchStatus.matchStatusGoalColor = ReadOptionalSubColor(cfg, "Metroid.Visual.HudMatchStatusGoalColorOverall", "Metroid.Visual.HudMatchStatusGoalColorR", "Metroid.Visual.HudMatchStatusGoalColorG", "Metroid.Visual.HudMatchStatusGoalColorB");
}
static void LoadBombLeftConfig(BombLeftHudConfig& bombLeft, Config::Table& cfg)
{
    bombLeft.bombLeftShow = cfg.GetBool("Metroid.Visual.HudBombLeftShow");
    bombLeft.bombLeftTextShow = cfg.GetBool("Metroid.Visual.HudBombLeftTextShow");
    bombLeft.bombLeftAnchor = cfg.GetInt("Metroid.Visual.HudBombLeftAnchor");
    bombLeft.bombLeftOfsX = cfg.GetInt("Metroid.Visual.HudBombLeftX");
    bombLeft.bombLeftOfsY = cfg.GetInt("Metroid.Visual.HudBombLeftY");
    bombLeft.bombLeftAlign = cfg.GetInt("Metroid.Visual.HudBombLeftAlign");
    bombLeft.bombLeftColor = ReadRgbColor(cfg, "Metroid.Visual.HudBombLeftColorR", "Metroid.Visual.HudBombLeftColorG", "Metroid.Visual.HudBombLeftColorB");
    CopyConfigString(bombLeft.bombLeftPrefix, sizeof(bombLeft.bombLeftPrefix), cfg.GetString("Metroid.Visual.HudBombLeftPrefix"));
    CopyConfigString(bombLeft.bombLeftSuffix, sizeof(bombLeft.bombLeftSuffix), cfg.GetString("Metroid.Visual.HudBombLeftSuffix"));
    bombLeft.bombIconShow = cfg.GetBool("Metroid.Visual.HudBombLeftIconShow");
    bombLeft.bombIconColorOverlay = cfg.GetBool("Metroid.Visual.HudBombLeftIconColorOverlay");
    bombLeft.bombIconHeight = cfg.GetInt("Metroid.Visual.HudBombIconHeight");
    bombLeft.bombIconColor = ReadRgbColor(cfg, "Metroid.Visual.HudBombLeftIconColorR", "Metroid.Visual.HudBombLeftIconColorG", "Metroid.Visual.HudBombLeftIconColorB");
    bombLeft.bombIconMode = cfg.GetInt("Metroid.Visual.HudBombLeftIconMode");
    bombLeft.bombIconOfsX = cfg.GetInt("Metroid.Visual.HudBombLeftIconOfsX");
    bombLeft.bombIconOfsY = cfg.GetInt("Metroid.Visual.HudBombLeftIconOfsY");
    bombLeft.bombIconPosAnchor = cfg.GetInt("Metroid.Visual.HudBombLeftIconPosAnchor");
    bombLeft.bombIconPosOfsX = cfg.GetInt("Metroid.Visual.HudBombLeftIconPosX");
    bombLeft.bombIconPosOfsY = cfg.GetInt("Metroid.Visual.HudBombLeftIconPosY");
    bombLeft.bombIconAnchorX = cfg.GetInt("Metroid.Visual.HudBombLeftIconAnchorX");
    bombLeft.bombIconAnchorY = cfg.GetInt("Metroid.Visual.HudBombLeftIconAnchorY");
}
static void LoadRankTimeConfig(RankTimeHudConfig& rankTime, Config::Table& cfg)
{
    rankTime.rankShow = cfg.GetBool("Metroid.Visual.HudRankShow");
    rankTime.rankAnchor = cfg.GetInt("Metroid.Visual.HudRankAnchor");
    rankTime.rankOfsX = cfg.GetInt("Metroid.Visual.HudRankX");
    rankTime.rankOfsY = cfg.GetInt("Metroid.Visual.HudRankY");
    rankTime.rankAlign = cfg.GetInt("Metroid.Visual.HudRankAlign");
    rankTime.rankColor = ReadRgbColor(cfg, "Metroid.Visual.HudRankColorR", "Metroid.Visual.HudRankColorG", "Metroid.Visual.HudRankColorB");
    CopyConfigString(rankTime.rankPrefix, sizeof(rankTime.rankPrefix), cfg.GetString("Metroid.Visual.HudRankPrefix"));
    rankTime.rankShowOrdinal = cfg.GetBool("Metroid.Visual.HudRankShowOrdinal");
    CopyConfigString(rankTime.rankSuffix, sizeof(rankTime.rankSuffix), cfg.GetString("Metroid.Visual.HudRankSuffix"));
    rankTime.timeLeftShow = cfg.GetBool("Metroid.Visual.HudTimeLeftShow");
    rankTime.timeLeftAnchor = cfg.GetInt("Metroid.Visual.HudTimeLeftAnchor");
    rankTime.timeLeftOfsX = cfg.GetInt("Metroid.Visual.HudTimeLeftX");
    rankTime.timeLeftOfsY = cfg.GetInt("Metroid.Visual.HudTimeLeftY");
    rankTime.timeLeftAlign = cfg.GetInt("Metroid.Visual.HudTimeLeftAlign");
    rankTime.timeLeftColor = ReadRgbColor(cfg, "Metroid.Visual.HudTimeLeftColorR", "Metroid.Visual.HudTimeLeftColorG", "Metroid.Visual.HudTimeLeftColorB");
    rankTime.timeLimitShow = cfg.GetBool("Metroid.Visual.HudTimeLimitShow");
    rankTime.timeLimitAnchor = cfg.GetInt("Metroid.Visual.HudTimeLimitAnchor");
    rankTime.timeLimitOfsX = cfg.GetInt("Metroid.Visual.HudTimeLimitX");
    rankTime.timeLimitOfsY = cfg.GetInt("Metroid.Visual.HudTimeLimitY");
    rankTime.timeLimitAlign = cfg.GetInt("Metroid.Visual.HudTimeLimitAlign");
    rankTime.timeLimitColor = ReadRgbColor(cfg, "Metroid.Visual.HudTimeLimitColorR", "Metroid.Visual.HudTimeLimitColorG", "Metroid.Visual.HudTimeLimitColorB");
}
static void LoadRadarOverlayConfig(RadarOverlayConfig& radar, Config::Table& cfg)
{
    radar.radarShow = cfg.GetBool("Metroid.Visual.BtmOverlayEnable");
    radar.radarAnchor = cfg.GetInt("Metroid.Visual.BtmOverlayAnchor");
    radar.radarOfsX = cfg.GetInt("Metroid.Visual.BtmOverlayDstX");
    radar.radarOfsY = cfg.GetInt("Metroid.Visual.BtmOverlayDstY");
    radar.radarDstSize = std::max(cfg.GetInt("Metroid.Visual.BtmOverlayDstSize"), 1);
    radar.radarOpacity = std::clamp(cfg.GetDouble("Metroid.Visual.BtmOverlayOpacity"), 0.0, 1.0);
    radar.radarSrcRadius = std::max(cfg.GetInt("Metroid.Visual.BtmOverlaySrcRadius"), 1);
    // radarDstRect and radarClipPath are recomputed in RecomputeAnchorPositions()
}
// Recompute all final X/Y positions from stored anchor + offset + topStretchX.
// Called when config is refreshed or when topStretchX changes (window resize).
static void RecomputeAnchorPositions(float topStretchX)
{
    auto& c = s_cache;
    const float sx = topStretchX;
    c.lastStretchX = sx;

    // HP
    ApplyAnchor(c.hp.hpAnchor, c.hp.hpOfsX, c.hp.hpOfsY, c.hp.hpX, c.hp.hpY, sx);
    ApplyAnchor(c.hp.hpGaugePosAnchor, c.hp.hpGaugePosOfsX, c.hp.hpGaugePosOfsY, c.hp.hpGaugePosX, c.hp.hpGaugePosY, sx);

    // Weapon
    ApplyAnchor(c.weapon.wpnAnchor, c.weapon.wpnOfsX, c.weapon.wpnOfsY, c.weapon.wpnX, c.weapon.wpnY, sx);
    ApplyAnchor(c.weapon.iconPosAnchor, c.weapon.iconPosOfsX, c.weapon.iconPosOfsY, c.weapon.iconPosX, c.weapon.iconPosY, sx);
    ApplyAnchor(c.weapon.ammoGaugePosAnchor, c.weapon.ammoGaugePosOfsX, c.weapon.ammoGaugePosOfsY, c.weapon.ammoGaugePosX, c.weapon.ammoGaugePosY, sx);

    // Match status
    ApplyAnchor(c.matchStatus.matchStatusAnchor, c.matchStatus.matchStatusOfsX, c.matchStatus.matchStatusOfsY, c.matchStatus.matchStatusX, c.matchStatus.matchStatusY, sx);

    // Bomb left
    ApplyAnchor(c.bombLeft.bombLeftAnchor, c.bombLeft.bombLeftOfsX, c.bombLeft.bombLeftOfsY, c.bombLeft.bombLeftX, c.bombLeft.bombLeftY, sx);
    ApplyAnchor(c.bombLeft.bombIconPosAnchor, c.bombLeft.bombIconPosOfsX, c.bombLeft.bombIconPosOfsY, c.bombLeft.bombIconPosX, c.bombLeft.bombIconPosY, sx);

    // Rank & Time
    ApplyAnchor(c.rankTime.rankAnchor, c.rankTime.rankOfsX, c.rankTime.rankOfsY, c.rankTime.rankX, c.rankTime.rankY, sx);
    ApplyAnchor(c.rankTime.timeLeftAnchor, c.rankTime.timeLeftOfsX, c.rankTime.timeLeftOfsY, c.rankTime.timeLeftX, c.rankTime.timeLeftY, sx);
    ApplyAnchor(c.rankTime.timeLimitAnchor, c.rankTime.timeLimitOfsX, c.rankTime.timeLimitOfsY, c.rankTime.timeLimitX, c.rankTime.timeLimitY, sx);

    // Radar overlay
    ApplyAnchor(c.radar.radarAnchor, c.radar.radarOfsX, c.radar.radarOfsY, c.radar.radarDstX, c.radar.radarDstY, sx);
    c.radar.radarDstRect = QRect(c.radar.radarDstX, c.radar.radarDstY, c.radar.radarDstSize, c.radar.radarDstSize);
    c.radar.radarClipPath = QPainterPath();
    c.radar.radarClipPath.addEllipse(c.radar.radarDstRect);
}

static void RefreshCachedConfig(Config::Table& cfg, float topStretchX = 1.0f)
{
    auto& c = s_cache;
    LoadHpConfig(c.hp, cfg);
    LoadWeaponConfig(c.weapon, cfg);
    LoadCrosshairConfig(c.crosshair, cfg);
    LoadMatchStatusConfig(c.matchStatus, cfg);
    LoadBombLeftConfig(c.bombLeft, cfg);
    LoadRankTimeConfig(c.rankTime, cfg);
    LoadRadarOverlayConfig(c.radar, cfg);
    c.outline.enable    = cfg.GetBool("Metroid.Visual.HudOutline");
    c.outline.color     = ReadRgbColor(cfg, "Metroid.Visual.HudOutlineColorR", "Metroid.Visual.HudOutlineColorG", "Metroid.Visual.HudOutlineColorB");
    c.outline.opacity   = (float)std::clamp(cfg.GetDouble("Metroid.Visual.HudOutlineOpacity"), 0.0, 1.0);
    c.outline.thickness = std::max(1, cfg.GetInt("Metroid.Visual.HudOutlineThickness"));
    c.textScalePct        = std::max(10, cfg.GetInt("Metroid.Visual.HudTextScale"));
    c.hpOpacity           = (float)cfg.GetDouble("Metroid.Visual.HudHpOpacity");
    c.weaponOpacity       = (float)cfg.GetDouble("Metroid.Visual.HudWeaponOpacity");
    c.matchStatusOpacity  = (float)cfg.GetDouble("Metroid.Visual.HudMatchStatusOpacity");
    c.rankOpacity         = (float)cfg.GetDouble("Metroid.Visual.HudRankOpacity");
    c.bombLeftOpacity     = (float)cfg.GetDouble("Metroid.Visual.HudBombLeftOpacity");
    c.hpGaugeOpacity      = (float)cfg.GetDouble("Metroid.Visual.HudHpGaugeOpacity");
    c.wpnIconOpacity      = (float)cfg.GetDouble("Metroid.Visual.HudWpnIconOpacity");
    c.ammoGaugeOpacity    = (float)cfg.GetDouble("Metroid.Visual.HudAmmoGaugeOpacity");
    c.bombIconOpacity     = (float)cfg.GetDouble("Metroid.Visual.HudBombIconOpacity");
    c.timeLeftOpacity     = (float)cfg.GetDouble("Metroid.Visual.HudTimeLeftOpacity");
    c.timeLimitOpacity    = (float)cfg.GetDouble("Metroid.Visual.HudTimeLimitOpacity");
    RecomputeAnchorPositions(topStretchX);
    ++s_cacheEpoch;
    if (s_cacheEpoch == 0) s_cacheEpoch = 1;
}

// =========================================================================
//  Battle HUD — mode-specific score/time display
// =========================================================================

// Battle modes (8-bit read from addrBattleMode)
enum BattleMode : uint8_t {
    MODE_BATTLE        = 2,
    MODE_BOUNTY        = 3,
    MODE_CAPTURE       = 4,
    MODE_DEFENDER      = 5,
    MODE_NODES         = 6,
    MODE_PRIME_HUNTER  = 7,
    MODE_SURVIVAL      = 8,
};

// XX → goal: direct map from document (no index calculation)
static int LookupBattleGoal(uint8_t xx)
{
    switch (xx) {
    case 0x00: return 1;   case 0x04: return 5;   case 0x08: return 7;
    case 0x0C: return 10;  case 0x10: return 15;  case 0x14: return 20;
    case 0x18: return 25;  case 0x1C: return 30;  case 0x20: return 40;
    case 0x24: return 50;  case 0x28: return 60;  case 0x2C: return 70;
    case 0x30: return 80;  case 0x34: return 90;  case 0x38: return 100;
    default: return 0;
    }
}

static int LookupSurvivalGoal(uint8_t xx)
{
    switch (xx) {
    case 0x00: return 0;  case 0x04: return 1;  case 0x08: return 2;
    case 0x0C: return 3;  case 0x10: return 4;  case 0x14: return 5;
    case 0x18: return 6;  case 0x1C: return 7;  case 0x20: return 8;
    case 0x24: return 9;  case 0x28: return 10;
    default: return 0;
    }
}

static int LookupOctoGoal(uint8_t xx)
{
    switch (xx) {
    case 0x00: return 1;  case 0x04: return 2;  case 0x08: return 3;
    case 0x0C: return 4;  case 0x10: return 5;  case 0x14: return 6;
    case 0x18: return 7;  case 0x1C: return 8;  case 0x20: return 9;
    case 0x24: return 10; case 0x28: return 15; case 0x2C: return 20;
    case 0x30: return 25;
    default: return 0;
    }
}

static int LookupNodeGoal(uint8_t xx)
{
    switch (xx) {
    case 0x00: return 40;  case 0x04: return 50;  case 0x08: return 60;
    case 0x0C: return 70;  case 0x10: return 80;  case 0x14: return 90;
    case 0x18: return 100; case 0x1C: return 120; case 0x20: return 140;
    case 0x24: return 160; case 0x28: return 180; case 0x2C: return 190;
    case 0x30: return 200; case 0x34: return 250;
    default: return 0;
    }
}

// Defender/Prime Hunter time goal XY→seconds (from battleSettings+4)
static int LookupTimeGoalSec(uint8_t xy)
{
    switch (xy) {
    case 0x00: return 60;  case 0x02: return 90;  case 0x04: return 120;
    case 0x06: return 150; case 0x08: return 180; case 0x0A: return 210;
    case 0x0C: return 240; case 0x0E: return 270; case 0x10: return 300;
    case 0x12: return 360; case 0x14: return 420; case 0x16: return 480;
    case 0x18: return 540; case 0x1A: return 600;
    default: return 0;
    }
}

// Match time limit: battleSettings+4 byte1 (XX) → minutes
// Bit0 = unused/flag. Bits1-4 = time index (0-14). Bit5+ = flags (WiFi=+0x20, etc. — ignored).
static int LookupTimeLimitMin(uint8_t XX)
{
    static const int kMinutes[] = { 3, 5, 7, 9, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60 };
    int idx = (XX >> 1) & 0xF;
    return idx < 15 ? kMinutes[idx] : 0;
}

static int LookupGoal(uint8_t mode, uint8_t xx)
{
    switch (mode) {
    case MODE_BATTLE:       return LookupBattleGoal(xx);
    case MODE_SURVIVAL:     return LookupSurvivalGoal(xx);
    case MODE_BOUNTY:
    case MODE_CAPTURE:      return LookupOctoGoal(xx);
    case MODE_NODES:        return LookupNodeGoal(xx);
    default:                return 0;
    }
}

static void FormatTime(char* buf, int bufSize, int seconds)
{
    int m = seconds / 60;
    int s = seconds % 60;
    std::snprintf(buf, bufSize, "%d:%02d", m, s);
}

static void FormatMinuteTime(char* buf, int bufSize, int minutes)
{
    std::snprintf(buf, bufSize, "%d:00", std::max(0, minutes));
}

// =========================================================================
//  Battle match cache — read once at match join, reused every frame
// =========================================================================
struct BattleMatchState {
    uint8_t  mode;
    uint8_t  keyXX;
    uint8_t  teamNibble;  // battleSettings+4 bits[3:0] (Z): bit i = player(i+1) team (0=red, 1=green)
    bool     isTeamGame;  // battleSettings+4 bits[7:4] (Y): any bit set = team play ON
    int      goalValue;
    int      timeLimitMinutes; // match time limit in minutes (from battleSettings+4 XX field)
    bool     isTimeMode;
    bool     valid;
};
static BattleMatchState s_battleState = { .valid = false };

void CustomHud_OnMatchJoin(melonDS::u8* ram, const RomAddresses& rom)
{
    auto& b = s_battleState;
    b.mode = Read8(ram, rom.battleMode);
    b.isTimeMode = false;
    b.goalValue = 0;
    b.timeLimitMinutes = 0;

    if (b.mode < MODE_BATTLE || b.mode > MODE_SURVIVAL) {
        b.valid = false;
        return;
    }

    uint32_t settings = Read32(ram, rom.battleSettings);
    uint8_t xx = (settings >> 20) & 0xFE; // bit 0 is a flag bit, mask it out
    b.keyXX = xx;

    // Time limit in minutes: battleSettings+4 format 0000XXYZ, XX=time limit index
    // Z nibble (bits[3:0]) = team composition: bit i = player(i+1) team (0=red, 1=green)
    {
        uint32_t ts4 = Read32(ram, rom.battleSettings + 4);
        uint8_t XX = (ts4 >> 8) & 0xFF;
        b.timeLimitMinutes = LookupTimeLimitMin(XX);
        b.teamNibble  = ts4 & 0x0F;          // Z nibble: team assignment per player
        b.isTeamGame  = (ts4 & 0x10) != 0;  // Y nibble bit0 (bit4 of byte): 1=team play ON
    }

    switch (b.mode) {
    case MODE_BATTLE:
    case MODE_NODES:
    case MODE_BOUNTY:
    case MODE_CAPTURE:
        b.goalValue = LookupGoal(b.mode, xx);
        break;
    case MODE_SURVIVAL:
        b.goalValue = LookupGoal(b.mode, xx) + 1; // table = max deaths, lives = deaths + 1
        break;
    case MODE_DEFENDER:
    case MODE_PRIME_HUNTER: {
        uint32_t timeSetting = Read32(ram, rom.battleSettings + 4);
        // Time goal index: Y[3:1] (bits[7:5]) + XX[0] (bit8), skipping Y[0] (bit4 = team-play flag)
        // Shift out bit4 entirely: >> 5 gives [XX0,Y3,Y2,Y1], << 1 produces even values for the lookup table
        uint8_t timeGoalRaw = static_cast<uint8_t>(((timeSetting >> 5) & 0x0F) << 1);
        b.goalValue = LookupTimeGoalSec(timeGoalRaw);
        b.isTimeMode = true;
        break;
    }
    }

    b.valid = true;
}
static const char* ResolveMatchStatusLabel(uint8_t mode, const MatchStatusHudConfig& c)
{
    switch (mode) {
    case MODE_BATTLE:
    case MODE_NODES: return c.matchStatusLabelPoints;
    case MODE_BOUNTY:
    case MODE_CAPTURE: return c.matchStatusLabelOctoliths;
    case MODE_SURVIVAL: return c.matchStatusLabelLives;
    case MODE_DEFENDER: return c.matchStatusLabelRingTime;
    case MODE_PRIME_HUNTER: return c.matchStatusLabelPrimeTime;
    default: return "";
    }
}
struct MatchStatusResolvedState { uint8_t mode, xx; int currentValue, goalValue; bool isTimeMode; };
struct MatchStatusStringCache { uint32_t configEpoch; uint8_t mode, xx; int currentValue, goalValue; bool hasGoal, isTimeMode, valid; char curBuf[24], sepBuf[4], goalBuf[24]; };
struct RankStringCache { uint32_t configEpoch; uint8_t rankByte; bool showOrdinal, valid; char buf[64]; };
struct TimeStringCache { uint32_t configEpoch; int value; bool valid; char buf[16]; };
static inline void UpdateMatchStatusStrings(MatchStatusStringCache& cache, const MatchStatusResolvedState& state)
{
    const bool hasGoal = state.isTimeMode || state.goalValue > 0;
    if (cache.valid && cache.configEpoch == s_cacheEpoch && cache.currentValue == state.currentValue && cache.goalValue == state.goalValue && cache.isTimeMode == state.isTimeMode && cache.mode == state.mode && cache.xx == state.xx && cache.hasGoal == hasGoal) return;
    cache.currentValue = state.currentValue; cache.goalValue = state.goalValue; cache.isTimeMode = state.isTimeMode; cache.mode = state.mode; cache.xx = state.xx; cache.hasGoal = hasGoal; cache.configEpoch = s_cacheEpoch;
    if (state.isTimeMode) { FormatTime(cache.curBuf, sizeof(cache.curBuf), state.currentValue); FormatTime(cache.goalBuf, sizeof(cache.goalBuf), state.goalValue); std::strncpy(cache.sepBuf, "/", sizeof(cache.sepBuf)); cache.sepBuf[sizeof(cache.sepBuf)-1]='\0'; }
    else if (state.goalValue > 0) { std::snprintf(cache.curBuf, sizeof(cache.curBuf), "%d", state.currentValue); std::snprintf(cache.goalBuf, sizeof(cache.goalBuf), "%d", state.goalValue); std::strncpy(cache.sepBuf, " / ", sizeof(cache.sepBuf)); cache.sepBuf[sizeof(cache.sepBuf)-1]='\0'; }
    else { std::snprintf(cache.curBuf, sizeof(cache.curBuf), "%d (XX=0x%02X)", state.currentValue, state.xx); cache.goalBuf[0]='\0'; cache.sepBuf[0]='\0'; }
    cache.valid = true;
}
static inline const char* UpdateRankString(RankStringCache& cache, uint8_t rankByte, bool showOrdinal, const RankTimeHudConfig& c)
{
    if (!cache.valid || cache.configEpoch != s_cacheEpoch || cache.rankByte != rankByte || cache.showOrdinal != showOrdinal) {
        static const char* kOrdinals[4] = { "st", "nd", "rd", "th" };
        if (showOrdinal) std::snprintf(cache.buf, sizeof(cache.buf), "%s%u%s%s", c.rankPrefix, rankByte + 1u, kOrdinals[rankByte], c.rankSuffix);
        else std::snprintf(cache.buf, sizeof(cache.buf), "%s%u%s", c.rankPrefix, rankByte + 1u, c.rankSuffix);
        cache.rankByte = rankByte; cache.showOrdinal = showOrdinal; cache.configEpoch = s_cacheEpoch; cache.valid = true;
    }
    return cache.buf;
}
static inline const char* UpdateTimeString(TimeStringCache& cache, int value, bool minutesOnly)
{
    if (!cache.valid || cache.configEpoch != s_cacheEpoch || cache.value != value) { if (minutesOnly) FormatMinuteTime(cache.buf, sizeof(cache.buf), value); else FormatTime(cache.buf, sizeof(cache.buf), value); cache.value = value; cache.configEpoch = s_cacheEpoch; cache.valid = true; }
    return cache.buf;
}
static bool ComputeMatchStatusState(melonDS::u8* ram, const RomAddresses& rom, uint8_t playerPos, MatchStatusResolvedState& outState)
{
    const BattleMatchState* match = s_battleState.valid ? &s_battleState : nullptr; uint8_t mode = match ? match->mode : Read8(ram, rom.battleMode); if (mode < MODE_BATTLE || mode > MODE_SURVIVAL) return false;
    uint32_t playerOfs = static_cast<uint32_t>(playerPos) * 4; int currentValue = 0; int goalValue = match ? match->goalValue : 0; bool isTimeMode = match ? match->isTimeMode : false; uint8_t xx = match ? match->keyXX : 0;
    uint32_t ts4Local = match ? 0u : Read32(ram, rom.battleSettings + 4); uint8_t teamNibble = match ? match->teamNibble : static_cast<uint8_t>(ts4Local & 0x0F); bool isTeamGame = match ? match->isTeamGame : ((ts4Local & 0x10) != 0);
    auto teamSum = [&](uint32_t baseAddr) -> int { if (isTeamGame && playerPos < 4) { uint8_t myTeam = (teamNibble >> playerPos) & 1; int sum = 0; for (int p = 0; p < 4; ++p) if (((teamNibble >> p) & 1) == myTeam) sum += static_cast<int>(Read32(ram, baseAddr + p * 4)); return sum; } return static_cast<int>(Read32(ram, baseAddr + playerOfs)); };
    switch (mode) { case MODE_BATTLE: case MODE_NODES: case MODE_BOUNTY: case MODE_CAPTURE: currentValue = teamSum(rom.basePoint); break; case MODE_SURVIVAL: currentValue = teamSum(rom.basePoint - 0xB0); break; case MODE_DEFENDER: case MODE_PRIME_HUNTER: currentValue = teamSum(rom.basePoint - 0x180) / 60; break; default: return false; }
    if (!match) { uint32_t settings = Read32(ram, rom.battleSettings); xx = (settings >> 20) & 0xFE; switch (mode) { case MODE_BATTLE: case MODE_NODES: case MODE_BOUNTY: case MODE_CAPTURE: goalValue = LookupGoal(mode, xx); break; case MODE_SURVIVAL: goalValue = LookupGoal(mode, xx); break; case MODE_DEFENDER: case MODE_PRIME_HUNTER: { uint32_t timeSetting = Read32(ram, rom.battleSettings + 4); uint8_t timeGoalRaw = static_cast<uint8_t>(((timeSetting >> 5) & 0x0F) << 1); goalValue = LookupTimeGoalSec(timeGoalRaw); isTimeMode = true; break; } default: return false; } }
    if (mode == MODE_SURVIVAL && goalValue > 0) { currentValue = goalValue - currentValue; if (currentValue < 0) currentValue = 0; }
    outState = { mode, xx, currentValue, goalValue, isTimeMode }; return true;
}
static void DrawMatchStatusText(QPainter* p, const QFontMetrics& fm, int fontPixelSize, float tds,
                                const MatchStatusResolvedState& state, const MatchStatusHudConfig& c,
                                float overallOpacity = 1.0f, const HudOutlineConfig& ol = {false,QColor(),0.0f,1},
                                float hudScale = 1.0f)
{
    static TextMeasureCache s_curTextCache = { 0, "", 0, 0, false }, s_sepTextCache = { 0, "", 0, 0, false }, s_goalTextCache = { 0, "", 0, 0, false };
    static TextBitmapCache s_curBitmapCache = { 0, QColor(), "", 0, 0, 0, false, QImage() }, s_sepBitmapCache = { 0, QColor(), "", 0, 0, 0, false, QImage() }, s_goalBitmapCache = { 0, QColor(), "", 0, 0, 0, false, QImage() }, s_labelBitmapCache = { 0, QColor(), "", 0, 0, 0, false, QImage() };
    static TextBitmapCache s_curOlCache = { 0, QColor(), "", 0, 0, 0, false, QImage() }, s_sepOlCache = { 0, QColor(), "", 0, 0, 0, false, QImage() }, s_goalOlCache = { 0, QColor(), "", 0, 0, 0, false, QImage() }, s_labelOlCache = { 0, QColor(), "", 0, 0, 0, false, QImage() };
    static MatchStatusStringCache s_matchStringCache = { 0, 0, 0, 0, 0, false, false, false, "", "", "" };
    UpdateMatchStatusStrings(s_matchStringCache, state);
    auto eff = [&](const QColor& sub) -> const QColor& { return sub.isValid() ? sub : c.matchStatusColor; };
    const bool useOutline = ol.enable && ol.opacity > 0.0f;
    const int expandR = useOutline ? std::max(1, (int)std::ceil((float)ol.thickness / hudScale)) : 0;
    auto drawText = [&](TextBitmapCache& bmp, TextBitmapCache& olBmp, int x, int y) {
        if (useOutline) {
            PrepareOutlineBitmapCached(olBmp, bmp, ol.color, expandR);
            DrawCachedTextOutlined(p, bmp, olBmp, x, y, tds, overallOpacity, ol.opacity);
        } else {
            if (overallOpacity < 1.0f) p->setOpacity(overallOpacity);
            DrawCachedText(p, bmp, x, y, tds);
            p->setOpacity(1.0f);
        }
    };
    int vx = c.matchStatusX, vy = c.matchStatusY, curW = 0, curH = 0;
    MeasureTextCached(fm, fontPixelSize, s_curTextCache, s_matchStringCache.curBuf, curW, curH, tds);
    PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_curBitmapCache, s_matchStringCache.curBuf, eff(c.matchStatusValueColor));
    drawText(s_curBitmapCache, s_curOlCache, vx, vy);
    if (s_matchStringCache.hasGoal) {
        int sepW = 0, sepH = 0, goalW = 0, goalH = 0;
        MeasureTextCached(fm, fontPixelSize, s_sepTextCache, s_matchStringCache.sepBuf, sepW, sepH, tds);
        MeasureTextCached(fm, fontPixelSize, s_goalTextCache, s_matchStringCache.goalBuf, goalW, goalH, tds);
        int x = vx + curW;
        PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_sepBitmapCache, s_matchStringCache.sepBuf, eff(c.matchStatusSepColor));
        drawText(s_sepBitmapCache, s_sepOlCache, x, vy);
        x += sepW;
        PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_goalBitmapCache, s_matchStringCache.goalBuf, eff(c.matchStatusGoalColor));
        drawText(s_goalBitmapCache, s_goalOlCache, x, vy);
    }
    const char* label = ResolveMatchStatusLabel(state.mode, c);
    if (label[0] == '\0') return;
    int lx = vx, ly = vy;
    switch (c.matchStatusLabelPos) { default: case 0: ly = vy - 10; break; case 1: ly = vy + 10; break; case 2: lx = vx - 50; break; case 3: lx = vx + 50; break; case 4: break; }
    lx += c.matchStatusLabelOfsX; ly += c.matchStatusLabelOfsY;
    PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_labelBitmapCache, label, eff(c.matchStatusLabelColor));
    drawText(s_labelBitmapCache, s_labelOlCache, lx, ly);
}
static void DrawMatchStatusHud(QPainter* p, melonDS::u8* ram, const RomAddresses& rom, uint8_t playerPos, bool isAdventure, const CachedHudConfig& c)
{
    if (!c.matchStatus.matchStatusShow || isAdventure) return; MatchStatusResolvedState state = {}; if (!ComputeMatchStatusState(ram, rom, playerPos, state)) return; const QFontMetrics fm = p->fontMetrics(); const int fontPixelSize = p->font().pixelSize(); DrawMatchStatusText(p, fm, fontPixelSize, c.textScalePct / 100.0f, state, c.matchStatus, c.matchStatusOpacity, c.outline, c.lastHudScale);
}
static int CalcAlignedTextX(int anchorX, int align, int textW);
static void DrawCachedAlignedText(QPainter* p, const QFontMetrics& fm, int fontPixelSize, float tds,
                                   TextMeasureCache& measureCache, TextBitmapCache& bitmapCache,
                                   TextBitmapCache& outlineCache,
                                   const char* text, const QColor& color,
                                   int anchorX, int align, int y,
                                   float opacity, const HudOutlineConfig& ol, float hudScale = 1.0f)
{
    int textW = 0, textH = 0;
    MeasureTextCached(fm, fontPixelSize, measureCache, text, textW, textH, tds);
    const int textX = CalcAlignedTextX(anchorX, align, textW);
    PrepareTextBitmapCached(fm, p->font(), fontPixelSize, bitmapCache, text, color);
    if (ol.enable && ol.opacity > 0.0f) {
        const int expandR = std::max(1, (int)std::ceil((float)ol.thickness / hudScale));
        PrepareOutlineBitmapCached(outlineCache, bitmapCache, ol.color, expandR);
        DrawCachedTextOutlined(p, bitmapCache, outlineCache, textX, y, tds, opacity, ol.opacity);
    } else {
        if (opacity < 1.0f) p->setOpacity(opacity);
        DrawCachedText(p, bitmapCache, textX, y, tds);
        p->setOpacity(1.0f);
    }
}
// =========================================================================
static int CalcAlignedTextX(int anchorX, int align, int textW);
// =========================================================================
//  Bomb Left HUD
// =========================================================================
static void DrawBombLeft(QPainter* p, melonDS::u8* ram, const RomAddresses& rom, uint32_t offP, const CachedHudConfig& c, float tds, float hudScale)
{
    if (!c.bombLeft.bombLeftShow) return; uint8_t bombs = static_cast<uint8_t>((Read32(ram, rom.baseBomb + offP) >> 8) & 0xF);
    {
        static TextBitmapCache s_bombBitmapCache = { 0, QColor(), "", 0, 0, 0, false, QImage() };
        static TextBitmapCache s_bombOutlineCache = { 0, QColor(), "", 0, 0, 0, false, QImage() };
        static TextMeasureCache s_bombMeasureCache = { 0, "", 0, 0, false };
        const QFontMetrics fm = p->fontMetrics(); const int fontPixelSize = p->font().pixelSize();
        char buf[64];
        if (c.bombLeft.bombLeftTextShow) std::snprintf(buf, sizeof(buf), "%s%u%s", c.bombLeft.bombLeftPrefix, bombs, c.bombLeft.bombLeftSuffix);
        else std::snprintf(buf, sizeof(buf), "%s%s", c.bombLeft.bombLeftPrefix, c.bombLeft.bombLeftSuffix);
        if (buf[0] != '\0') {
            int bombTextW = 0, bombTextH = 0;
            MeasureTextCached(fm, fontPixelSize, s_bombMeasureCache, buf, bombTextW, bombTextH, tds);
            PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_bombBitmapCache, buf, c.bombLeft.bombLeftColor);
            const int bombTextX = CalcAlignedTextX(c.bombLeft.bombLeftX, c.bombLeft.bombLeftAlign, bombTextW);
            if (c.outline.enable && c.outline.opacity > 0.0f) {
                const int expandR = std::max(1, (int)std::ceil((float)c.outline.thickness / hudScale));
                PrepareOutlineBitmapCached(s_bombOutlineCache, s_bombBitmapCache, c.outline.color, expandR);
                DrawCachedTextOutlined(p, s_bombBitmapCache, s_bombOutlineCache, bombTextX, c.bombLeft.bombLeftY, tds,
                                       c.bombLeftOpacity, c.outline.opacity);
            } else {
                if (c.bombLeftOpacity < 1.0f) p->setOpacity(c.bombLeftOpacity);
                DrawCachedText(p, s_bombBitmapCache, bombTextX, c.bombLeft.bombLeftY, tds);
                p->setOpacity(1.0f);
            }
        }
    }
    if (c.bombLeft.bombIconShow) {
        EnsureBombIconsLoaded(c.bombLeft.bombIconHeight, hudScale);
        const QImage& icon = GetBombIconForDraw(bombs, c.bombLeft.bombIconColorOverlay, c.bombLeft.bombIconColor);
        if (!icon.isNull()) {
            const float dw = icon.width() / hudScale; const float dh = icon.height() / hudScale;
            float ix = (c.bombLeft.bombIconMode == 0) ? c.bombLeft.bombLeftX + c.bombLeft.bombIconOfsX : c.bombLeft.bombIconPosX;
            float iy = (c.bombLeft.bombIconMode == 0) ? c.bombLeft.bombLeftY + c.bombLeft.bombIconOfsY : c.bombLeft.bombIconPosY;
            const int iconAlignX = c.bombLeft.bombIconAnchorX; const int iconAlignY = c.bombLeft.bombIconAnchorY;
            if (iconAlignX == 1) ix -= dw * 0.5f; else if (iconAlignX == 2) ix -= dw;
            if (iconAlignY == 1) iy -= dh * 0.5f; else if (iconAlignY == 2) iy -= dh;
            if (c.outline.enable && c.outline.opacity > 0.0f) {
                const int expandR = std::max(1, c.outline.thickness);
                EnsureOutlineIconsUpdated(c.outline.color, expandR);
                const int bombIdx = (bombs >= 0 && bombs <= 3) ? bombs : 0;
                DrawImageOutlined(p, icon, s_bombIconsOutline[bombIdx],
                                  QRectF(ix, iy, dw, dh), (float)expandR / hudScale,
                                  c.bombIconOpacity, c.outline.opacity);
            } else {
                if (c.bombIconOpacity < 1.0f) p->setOpacity(c.bombIconOpacity);
                p->drawImage(QRectF(ix, iy, dw, dh), icon);
                p->setOpacity(1.0f);
            }
        }
    }
}
//  Rank & Time HUD
// =========================================================================
static void DrawRankAndTime(QPainter* p, melonDS::u8* ram, const RomAddresses& rom, uint8_t playerPos, bool isAdventure, const CachedHudConfig& c, float tds)
{
    if (isAdventure) return; const auto& hud = c.rankTime; const QFontMetrics fm = p->fontMetrics(); const int fontPixelSize = p->font().pixelSize();
    if (hud.rankShow) { static RankStringCache s_rankStringCache = { 0, 0, false, false, "" }; static TextBitmapCache s_rankCache = { 0, QColor(), "", 0, 0, 0, false, QImage() }; static TextBitmapCache s_rankOutlineCache = { 0, QColor(), "", 0, 0, 0, false, QImage() }; static TextMeasureCache s_rankMeasure = { 0, "", 0, 0, false }; uint32_t rankWord = Read32(ram, rom.matchRank); uint8_t rankByte = (rankWord >> (playerPos * 8)) & 0xFF; if (rankByte <= 3) DrawCachedAlignedText(p, fm, fontPixelSize, tds, s_rankMeasure, s_rankCache, s_rankOutlineCache, UpdateRankString(s_rankStringCache, rankByte, hud.rankShowOrdinal, hud), hud.rankColor, hud.rankX, hud.rankAlign, hud.rankY, c.rankOpacity, c.outline, c.lastHudScale); }
    if (hud.timeLeftShow) { static TimeStringCache s_timeLeftStringCache = { 0, 0, false, "" }; static TextBitmapCache s_timeLeftCache = { 0, QColor(), "", 0, 0, 0, false, QImage() }; static TextBitmapCache s_timeLeftOutlineCache = { 0, QColor(), "", 0, 0, 0, false, QImage() }; static TextMeasureCache s_timeLeftMeasure = { 0, "", 0, 0, false }; int seconds = static_cast<int>(Read32(ram, rom.timeLeft)) / 60; DrawCachedAlignedText(p, fm, fontPixelSize, tds, s_timeLeftMeasure, s_timeLeftCache, s_timeLeftOutlineCache, UpdateTimeString(s_timeLeftStringCache, seconds, false), hud.timeLeftColor, hud.timeLeftX, hud.timeLeftAlign, hud.timeLeftY, c.timeLeftOpacity, c.outline, c.lastHudScale); }
    if (hud.timeLimitShow) { static TimeStringCache s_timeLimitStringCache = { 0, 0, false, "" }; static TextBitmapCache s_timeLimitCache = { 0, QColor(), "", 0, 0, 0, false, QImage() }; static TextBitmapCache s_timeLimitOutlineCache = { 0, QColor(), "", 0, 0, 0, false, QImage() }; static TextMeasureCache s_timeLimitMeasure = { 0, "", 0, 0, false }; int goalMinutes = s_battleState.valid ? s_battleState.timeLimitMinutes : LookupTimeLimitMin((Read32(ram, rom.battleSettings + 4) >> 8) & 0xFF); DrawCachedAlignedText(p, fm, fontPixelSize, tds, s_timeLimitMeasure, s_timeLimitCache, s_timeLimitOutlineCache, UpdateTimeString(s_timeLimitStringCache, goalMinutes, true), hud.timeLimitColor, hud.timeLimitX, hud.timeLimitAlign, hud.timeLimitY, c.timeLimitOpacity, c.outline, c.lastHudScale); }
}
// =========================================================================
//  Config key (only for IsEnabled — hot path uses s_cache)
// =========================================================================
static constexpr const char* kCfgCustomHud = "Metroid.Visual.CustomHUD";
bool CustomHud_IsEnabled(Config::Table& localCfg)
{
    return localCfg.GetBool(kCfgCustomHud);
}

static inline bool ShouldHideForGameplayState(bool isStartPressed, uint16_t currentHP, bool isGameOver)
{
    return isStartPressed || currentHP == 0 || isGameOver;
}

bool CustomHud_ShouldHideForGameplayState(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition)
{
    if (!emu) return true;

    melonDS::NDS* nds = emu->getNDS();
    melonDS::u8* ram = nds ? nds->MainRAM : nullptr;
    if (!ram) return true;

    const uint32_t offP = static_cast<uint32_t>(playerPosition) * Consts::PLAYER_ADDR_INC;
    return ShouldHideForGameplayState(Read8(ram, rom.startPressed) == 0x01,
                                      Read16(ram, rom.playerHP + offP),
                                      Read8(ram, rom.gameOver) != 0x00);
}

bool CustomHud_ShouldDrawRadarOverlay(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition)
{
    return !CustomHud_ShouldHideForGameplayState(emu, rom, playerPosition);
}
// =========================================================================
//  NoHUD Patch
// =========================================================================
static constexpr int NOHUD_PATCH_COUNT = 17;
static constexpr uint32_t ARM_NOP = 0xE1A00000;

struct HudPatchEntry { uint32_t addr, restoreValue; };

static constexpr HudPatchEntry kHudPatch[7][NOHUD_PATCH_COUNT] = {
    {{0x02008E78,0xE5840018},{0x02008F0C,0xE5840018},{0x0202A494,0xE5841000},{0x0202A554,0xE584C000},
     {0x0202A5FC,0xE584C000},{0x0202A6AC,0xE5840000},{0x0202A6B4,0xE5840000},{0x0202F7B4,0xE5801000},
     {0x0202F814,0xE5801000},{0x0202F870,0xE5801000},{0x0202F938,0xE5823000},{0x02030E58,0xE5812000},
     {0x020311F8,0xE5801000},{0x020565F8,0xE5801000},{0x020568C4,0xE5801000},{0x02058C50,0xE5801000},
     {0x0205A958,0xE5813000}},
    {{0x02008E78,0xE5840018},{0x02008F0C,0xE5840018},{0x0202A494,0xE5841000},{0x0202A554,0xE584C000},
     {0x0202A5FC,0xE584C000},{0x0202A6AC,0xE5840000},{0x0202A6B4,0xE5840000},{0x0202F7B4,0xE5801000},
     {0x0202F814,0xE5801000},{0x0202F870,0xE5801000},{0x0202F938,0xE5823000},{0x02030E58,0xE5812000},
     {0x020311F8,0xE5801000},{0x020565F8,0xE5801000},{0x020568C4,0xE5801000},{0x02058C50,0xE5801000},
     {0x0205A958,0xE5813000}},
    {{0x02008E78,0xE5840018},{0x02008F0C,0xE5840018},{0x0202A4B8,0xE5841000},{0x0202A578,0xE584C000},
     {0x0202A620,0xE584C000},{0x0202A6D0,0xE5840000},{0x0202A6D8,0xE5840000},{0x0202F79C,0xE5801000},
     {0x0202F7FC,0xE5801000},{0x0202F858,0xE5801000},{0x0202F920,0xE5823000},{0x02030E40,0xE5812000},
     {0x0203111C,0xE5801000},{0x02054BA8,0xE5801000},{0x02054E74,0xE5801000},{0x020571AC,0xE5801000},
     {0x02058D20,0xE5813000}},
    {{0x02008E78,0xE5840018},{0x02008F0C,0xE5840018},{0x0202A4B8,0xE5841000},{0x0202A578,0xE584C000},
     {0x0202A620,0xE584C000},{0x0202A6D0,0xE5840000},{0x0202A6D8,0xE5840000},{0x0202F76C,0xE5801000},
     {0x0202F7CC,0xE5801000},{0x0202F828,0xE5801000},{0x0202F8F0,0xE5823000},{0x02030E0C,0xE5812000},
     {0x020310E8,0xE5801000},{0x020553C8,0xE5801000},{0x02055694,0xE5801000},{0x020579C0,0xE5801000},
     {0x02059534,0xE5813000}},
    {{0x02008E7C,0xE5840018},{0x02008F10,0xE5840018},{0x0202A4B0,0xE5841000},{0x0202A570,0xE584C000},
     {0x0202A618,0xE584C000},{0x0202A6C8,0xE5840000},{0x0202A6D0,0xE5840000},{0x0202F764,0xE5801000},
     {0x0202F7C4,0xE5801000},{0x0202F820,0xE5801000},{0x0202F8E8,0xE5823000},{0x02030E04,0xE5812000},
     {0x020310E0,0xE5801000},{0x0205539C,0xE5801000},{0x02055668,0xE5801000},{0x02057994,0xE5801000},
     {0x020594E8,0xE5813000}},
    {{0x02008E78,0xE5840018},{0x02008F0C,0xE5840018},{0x0202A4B8,0xE5841000},{0x0202A578,0xE584C000},
     {0x0202A620,0xE584C000},{0x0202A6D0,0xE5840000},{0x0202A6D8,0xE5840000},{0x0202F76C,0xE5801000},
     {0x0202F7CC,0xE5801000},{0x0202F828,0xE5801000},{0x0202F8F0,0xE5823000},{0x02030E0C,0xE5812000},
     {0x020310E8,0xE5801000},{0x020553C8,0xE5801000},{0x02055694,0xE5801000},{0x020579C0,0xE5801000},
     {0x02059534,0xE5813000}},
    {{0x0203302C,0xE5801000},{0x0203336C,0xE5812000},{0x020345F8,0xE5801000},{0x0203472C,0xE5801000},
     {0x02034788,0xE5801000},{0x020347DC,0xE5801000},{0x02034800,0xE5801000},{0x0203487C,0xE5812000},
     {0x0203489C,0xE5823000},{0x02038FB8,0xE5841000},{0x02039054,0xE584C000},{0x02039100,0xE584C000},
     {0x020391BC,0xE5840000},{0x02050764,0xE5801000},{0x02050A24,0xE5801000},{0x02053F54,0xE5803000},
     {0x02054DCC,0xE5801000}},
};

static bool s_hudPatchApplied = false;

static void ApplyNoHudPatch(melonDS::NDS* nds, uint8_t romGroup)
{
    if (s_hudPatchApplied) return;
    for (int i = 0; i < NOHUD_PATCH_COUNT; i++)
        nds->ARM9Write32(kHudPatch[romGroup][i].addr, ARM_NOP);
    s_hudPatchApplied = true;
}

static void RestoreHudPatch(melonDS::NDS* nds, uint8_t romGroup)
{
    if (!s_hudPatchApplied) return;
    for (int i = 0; i < NOHUD_PATCH_COUNT; i++)
        nds->ARM9Write32(kHudPatch[romGroup][i].addr, kHudPatch[romGroup][i].restoreValue);
    s_hudPatchApplied = false;
}

void CustomHud_ResetPatchState()
{
    s_hudPatchApplied = false;
    s_cache.valid = false;
    s_battleState.valid = false;
}

// P-3: Called from settings dialog save to trigger config re-read next frame
void CustomHud_InvalidateConfigCache()
{
    s_cache.valid = false;
    s_bombTintCacheValid = false;
    for (int i = 0; i < 9; i++) s_weaponTintValid[i] = false;
}

uint32_t CustomHud_GetCacheEpoch()
{
    return s_cacheEpoch;
}

// =========================================================================
//  Gauge drawing
// =========================================================================
static void DrawGauge(QPainter* p, int x, int y, float ratio,
                      const QColor& fillColor, int orientation,
                      int barLength, int barWidth,
                      const HudOutlineConfig* outline = nullptr,
                      float hudScale = 1.0f)
{
    ratio = (ratio < 0.0f) ? 0.0f : (ratio > 1.0f) ? 1.0f : ratio;
    if (barLength <= 0) barLength = 28;
    if (barWidth  <= 0) barWidth  = 3;

    static const QColor bgColor(0, 0, 0, 128); // P-4: construct once

    if (outline && outline->enable && outline->opacity > 0.0f) {
        const float expand = static_cast<float>(outline->thickness) / hudScale;
        QColor outlineColor = outline->color;
        outlineColor.setAlphaF(outline->opacity);
        if (orientation == 0) {
            p->fillRect(QRectF(x - expand, y - expand, barLength + expand * 2.0f, barWidth + expand * 2.0f), outlineColor);
        } else {
            p->fillRect(QRectF(x - expand, y - expand, barWidth + expand * 2.0f, barLength + expand * 2.0f), outlineColor);
        }
    }

    if (orientation == 0) {
        p->fillRect(x, y, barLength, barWidth, bgColor);
        int fillW = static_cast<int>(barLength * ratio);
        if (fillW > 0) p->fillRect(x, y, fillW, barWidth, fillColor);
    } else {
        p->fillRect(x, y, barWidth, barLength, bgColor);
        int fillH = static_cast<int>(barLength * ratio);
        if (fillH > 0) p->fillRect(x, y + barLength - fillH, barWidth, fillH, fillColor);
    }
}

static inline QColor HpGaugeColor(uint16_t hp, const QColor& safeColor)
{
    if (hp <= 25)      return QColor(255, 0, 0);
    else if (hp <= 50) return QColor(255, 165, 0);
    else               return safeColor;
}

static int CalcAlignedTextX(int anchorX, int align, int textW)
{
    switch (align) {
    case 1: return anchorX - textW / 2;
    case 2: return anchorX - textW;
    default: return anchorX;
    }
}

static void CalcGaugePos(int textX, int textY, int textW, int textH, int anchor,
                         int ofsX, int ofsY, int gaugeLen, int gaugeWid, int ori,
                         int& outX, int& outY)
{
    switch (anchor) {
    case 0: outX = textX + ofsX;           outY = textY + 2 + ofsY; break;
    case 1: outX = textX + ofsX;           outY = textY - textH - (ori==0?gaugeWid:gaugeLen) + ofsY; break;
    case 2: outX = textX + textW + ofsX;   outY = textY - textH/2 - (ori==0?gaugeWid:gaugeLen)/2 + ofsY; break;
    case 3: outX = textX - (ori==0?gaugeLen:gaugeWid) + ofsX; outY = textY - textH/2 - (ori==0?gaugeWid:gaugeLen)/2 + ofsY; break;
    case 4: outX = textX + textW/2 - (ori==0?gaugeLen:gaugeWid)/2 + ofsX; outY = textY - textH/2 - (ori==0?gaugeWid:gaugeLen)/2 + ofsY; break;
    default: outX = textX + ofsX;          outY = textY + 2 + ofsY; break;
    }
}

// =========================================================================
//  P-5: DrawHP — stack-buffer text, reads CachedHudConfig directly
// =========================================================================
static inline void DrawHP(QPainter* p, uint16_t hp, uint16_t maxHP,
                           const CachedHudConfig& c, float tds)
{
    const QColor hpTextColor = c.hp.hpTextAutoColor ? HpGaugeColor(hp, c.hp.hpTextColor) : c.hp.hpTextColor;

    static TextMeasureCache s_hpTextCache = { 0, "", 0, 0, false };
    static TextBitmapCache s_hpBitmapCache = { 0, QColor(), "", 0, 0, 0, false, QImage() };
    static TextBitmapCache s_hpOutlineCache = { 0, QColor(), "", 0, 0, 0, false, QImage() };
    const QFontMetrics fm = p->fontMetrics();
    const int fontPixelSize = p->font().pixelSize();

    char buf[24];
    std::snprintf(buf, sizeof(buf), "%s%u", c.hp.hpPrefix, hp);
    int textW = 0, textH = 0;
    MeasureTextCached(fm, fontPixelSize, s_hpTextCache, buf, textW, textH, tds);
    const int textX = CalcAlignedTextX(c.hp.hpX, c.hp.hpAlign, textW);
    PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_hpBitmapCache, buf, hpTextColor);
    if (c.outline.enable && c.outline.opacity > 0.0f) {
        const int expandR = std::max(1, (int)std::ceil((float)c.outline.thickness / c.lastHudScale));
        PrepareOutlineBitmapCached(s_hpOutlineCache, s_hpBitmapCache, c.outline.color, expandR);
        DrawCachedTextOutlined(p, s_hpBitmapCache, s_hpOutlineCache, textX, c.hp.hpY, tds,
                               c.hpOpacity, c.outline.opacity);
    } else {
        if (c.hpOpacity < 1.0f) p->setOpacity(c.hpOpacity);
        DrawCachedText(p, s_hpBitmapCache, textX, c.hp.hpY, tds);
        if (c.hpOpacity < 1.0f) p->setOpacity(1.0f);
    }

    if (c.hp.hpGauge && maxHP > 0) {
        float ratio = static_cast<float>(hp) / static_cast<float>(maxHP);
        QColor gc = c.hp.hpAutoColor ? HpGaugeColor(hp, c.hp.hpGaugeColor) : c.hp.hpGaugeColor;
        int gx, gy;
        if (c.hp.hpGaugePosMode == 1) {
            gx = c.hp.hpGaugePosX;
            gy = c.hp.hpGaugePosY;
        } else {
            CalcGaugePos(textX, c.hp.hpY, textW, textH, c.hp.hpGaugeAnchor, c.hp.hpGaugeOfsX, c.hp.hpGaugeOfsY,
                         c.hp.hpGaugeLen, c.hp.hpGaugeWid, c.hp.hpGaugeOri, gx, gy);
        }
        if (c.hpGaugeOpacity < 1.0f) p->setOpacity(c.hpGaugeOpacity);
        DrawGauge(p, gx, gy, ratio, gc, c.hp.hpGaugeOri, c.hp.hpGaugeLen, c.hp.hpGaugeWid,
                  &c.outline, c.lastHudScale);
        if (c.hpGaugeOpacity < 1.0f) p->setOpacity(1.0);
    }
}

// =========================================================================
//  P-6: Weapon divisor table — replaces two switch statements
// =========================================================================
struct WeaponInfo { uint16_t divisor; bool isMissile; };

static constexpr WeaponInfo kWeaponTable[9] = {
    {0,    false}, {0x5,  false}, {0xA,  true },
    {0x4,  false}, {0x14, false}, {0x5,  false},
    {0xA,  false}, {0xA,  false}, {1,    false},
};

// =========================================================================
//  P-7: DrawWeaponAmmo — cached icon, table lookup, stack text
static void DrawWeaponAmmo(QPainter* p, melonDS::u8* ram,
                           uint8_t weapon, uint16_t ammoSpecial, uint32_t addrMissile,
                           uint16_t maxAmmoSpecial, uint16_t maxAmmoMissile,
                           const CachedHudConfig& c, float tds, float hudScale)
{
    static TextMeasureCache s_ammoTextCache = { 0, "", 0, 0, false };
    static TextBitmapCache s_ammoBitmapCache = { 0, QColor(), "", 0, 0, 0, false, QImage() };
    static TextBitmapCache s_ammoOutlineCache = { 0, QColor(), "", 0, 0, 0, false, QImage() };
    const QFontMetrics fm = p->fontMetrics();
    const int fontPixelSize = p->font().pixelSize();

    const WeaponInfo& wi = kWeaponTable[weapon];
    EnsureIconsLoaded(c.weapon.iconHeight, hudScale);
    const bool iconOvEnable = (weapon < 9) ? c.weapon.iconOverlayEnable[weapon] : false;
    const QColor iconOvColor = (weapon < 9) ? c.weapon.iconOverlayColor[weapon] : QColor();
    const QImage& icon = GetWeaponIconForDraw(weapon, iconOvEnable, iconOvColor); // P-1

    uint16_t ammo = 0, maxAmmo = 0;
    bool hasAmmo = (wi.divisor > 0);

    if (hasAmmo) {
        if (wi.isMissile) {
            ammo    = Read16(ram, addrMissile) / wi.divisor;
            maxAmmo = maxAmmoMissile / wi.divisor;
        } else if (weapon == 8) {
            ammo = 1; maxAmmo = 1;
        } else {
            ammo    = ammoSpecial / wi.divisor;
            maxAmmo = maxAmmoSpecial / wi.divisor;
        }
    }

    int textX = c.weapon.wpnX, textY = c.weapon.wpnY;
    int textW = 0, textH = fm.height();
    if (hasAmmo) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%s%02u", c.weapon.ammoPrefix, ammo);
        MeasureTextCached(fm, fontPixelSize, s_ammoTextCache, buf, textW, textH, tds);
        textX = CalcAlignedTextX(c.weapon.wpnX, c.weapon.ammoAlign, textW);
        PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_ammoBitmapCache, buf, c.weapon.ammoTextColor);
        if (c.outline.enable && c.outline.opacity > 0.0f) {
            const int expandR = std::max(1, (int)std::ceil((float)c.outline.thickness / hudScale));
            PrepareOutlineBitmapCached(s_ammoOutlineCache, s_ammoBitmapCache, c.outline.color, expandR);
            DrawCachedTextOutlined(p, s_ammoBitmapCache, s_ammoOutlineCache, textX, textY, tds,
                                   c.weaponOpacity, c.outline.opacity);
        } else {
            if (c.weaponOpacity < 1.0f) p->setOpacity(c.weaponOpacity);
            DrawCachedText(p, s_ammoBitmapCache, textX, textY, tds);
            p->setOpacity(1.0f);
        }
    }

    if (c.weapon.iconShow && !icon.isNull()) {
        const float dw = icon.width()  / hudScale;
        const float dh = icon.height() / hudScale;
        float ix = (c.weapon.iconMode == 0) ? c.weapon.wpnX + c.weapon.iconOfsX : c.weapon.iconPosX;
        float iy = (c.weapon.iconMode == 0) ? c.weapon.wpnY + c.weapon.iconOfsY : c.weapon.iconPosY;
        if (c.weapon.iconAnchorX == 1) ix -= dw * 0.5f;
        else if (c.weapon.iconAnchorX == 2) ix -= dw;
        if (c.weapon.iconAnchorY == 1) iy -= dh * 0.5f;
        else if (c.weapon.iconAnchorY == 2) iy -= dh;
        if (c.outline.enable && c.outline.opacity > 0.0f) {
            const int expandR = std::max(1, c.outline.thickness);
            EnsureOutlineIconsUpdated(c.outline.color, expandR);
            const uint8_t wpnIdx = (weapon < 9) ? weapon : 0;
            DrawImageOutlined(p, icon, s_weaponIconsOutline[wpnIdx],
                              QRectF(ix, iy, dw, dh), (float)expandR / hudScale,
                              c.wpnIconOpacity, c.outline.opacity);
        } else {
            if (c.wpnIconOpacity < 1.0f) p->setOpacity(c.wpnIconOpacity);
            p->drawImage(QRectF(ix, iy, dw, dh), icon);
            p->setOpacity(1.0f);
        }
    }

    if (c.weapon.ammoGauge && hasAmmo && maxAmmo > 0) {
        float ratio = static_cast<float>(ammo) / static_cast<float>(maxAmmo);
        int gx, gy;
        if (c.weapon.ammoGaugePosMode == 1) {
            gx = c.weapon.ammoGaugePosX;
            gy = c.weapon.ammoGaugePosY;
        } else {
            CalcGaugePos(textX, textY, textW, textH, c.weapon.ammoGaugeAnchor, c.weapon.ammoGaugeOfsX, c.weapon.ammoGaugeOfsY,
                         c.weapon.ammoGaugeLen, c.weapon.ammoGaugeWid, c.weapon.ammoGaugeOri, gx, gy);
        }
        if (c.ammoGaugeOpacity < 1.0f) p->setOpacity(c.ammoGaugeOpacity);
        DrawGauge(p, gx, gy, ratio, c.weapon.ammoGaugeColor, c.weapon.ammoGaugeOri, c.weapon.ammoGaugeLen, c.weapon.ammoGaugeWid,
                  &c.outline, hudScale);
        if (c.ammoGaugeOpacity < 1.0f) p->setOpacity(1.0);
    }
}

// =========================================================================
//  Crosshair
// =========================================================================

// Arms stored as rects now (not line endpoints) to avoid pen-centering artifacts.
static void CollectArmRects(QRect* out, int& n, int cx, int cy,
                            int lengthX, int lengthY, int offset,
                            int thickness, bool tStyle)
{
    const int halfT = thickness / 2;
    n = 0;
    if (lengthX > 0) {
        // Left arm
        out[n++] = QRect(cx - offset - lengthX, cy - halfT, lengthX, thickness);
        // Right arm
        out[n++] = QRect(cx + offset + 1, cy - halfT, lengthX, thickness);
    }
    if (lengthY > 0) {
        // Down arm
        out[n++] = QRect(cx - halfT, cy + offset + 1, thickness, lengthY);
        // Up arm (skip if T-style)
        if (!tStyle)
            out[n++] = QRect(cx - halfT, cy - offset - lengthY, thickness, lengthY);
    }
}

// Float version of CollectArmRects — uses QRectF for sub-pixel precision and
// applies chScale to all geometry (arm lengths, offset, thickness).
static void CollectArmRectsF(QRectF* out, int& n, float cx, float cy,
                              float lengthX, float lengthY, float offset,
                              float thickness, bool tStyle)
{
    const float halfT = thickness * 0.5f;
    n = 0;
    if (lengthX > 0.0f) {
        out[n++] = QRectF(cx - offset - lengthX, cy - halfT, lengthX, thickness);
        out[n++] = QRectF(cx + offset,           cy - halfT, lengthX, thickness);
    }
    if (lengthY > 0.0f) {
        out[n++] = QRectF(cx - halfT, cy + offset,           thickness, lengthY);
        if (!tStyle)
            out[n++] = QRectF(cx - halfT, cy - offset - lengthY, thickness, lengthY);
    }
}

// P-8: Reads directly from CachedHudConfig — no ReadCrosshairConfig() copy.
// hudScale / topStretchX are passed to size the outline buffer at output resolution.
static void DrawCrosshair(QPainter* p, melonDS::u8* ram,
                          const RomAddresses& rom,
                          const CachedHudConfig& c,
                          float hudScale, float topStretchX)
{
    const float cx = static_cast<float>(Read8(ram, rom.crosshairPosX));
    const float cy = static_cast<float>(Read8(ram, rom.crosshairPosY));
    const float cs = static_cast<float>(c.crosshair.chScale);

    QRectF innerRects[4], outerRects[4];
    int nInner = 0, nOuter = 0;
    if (c.crosshair.chInnerShow)
        CollectArmRectsF(innerRects, nInner, cx, cy,
                         c.crosshair.chInnerLengthX  * cs, c.crosshair.chInnerLengthY  * cs,
                         c.crosshair.chInnerOffset   * cs, c.crosshair.chInnerThickness * cs,
                         c.crosshair.chTStyle);
    if (c.crosshair.chOuterShow)
        CollectArmRectsF(outerRects, nOuter, cx, cy,
                         c.crosshair.chOuterLengthX  * cs, c.crosshair.chOuterLengthY  * cs,
                         c.crosshair.chOuterOffset   * cs, c.crosshair.chOuterThickness * cs,
                         c.crosshair.chTStyle);

    // Outline pass: render into a pixel-resolution buffer so the outline is
    // sharp even at hudScale > 1.  The outline painter inherits p's transform
    // so DS-space coordinates map directly to output pixels in the buffer.
    if (c.crosshair.chOutline && c.crosshair.chOutlineOpacity > 0.0) {
        // Divide by hudScale so thickness=1 means 1 output pixel regardless of resolution.
        const float olT = static_cast<float>(c.crosshair.chOutlineThickness) / hudScale;
        const float dotH = c.crosshair.chCenterDot
                         ? (c.crosshair.chDotThickness * cs + olT * 2.0f) * 0.5f : 0.0f;

        // DS-space bounding box
        float minX = cx - dotH, maxX = cx + dotH;
        float minY = cy - dotH, maxY = cy + dotH;
        auto expandBoundsF = [&](const QRectF& r, float pad) {
            if (r.left()   - pad < minX) minX = r.left()   - pad;
            if (r.right()  + pad > maxX) maxX = r.right()  + pad;
            if (r.top()    - pad < minY) minY = r.top()    - pad;
            if (r.bottom() + pad > maxY) maxY = r.bottom() + pad;
        };
        for (int i = 0; i < nInner; i++) expandBoundsF(innerRects[i], olT);
        for (int i = 0; i < nOuter; i++) expandBoundsF(outerRects[i], olT);

        // Pixel-resolution buffer matching the full overlay
        const int bw = qMax(1, (int)std::ceil(topStretchX * 256.0f * hudScale));
        const int bh = qMax(1, (int)std::ceil(192.0f * hudScale));
        QImage& olBuf = GetOutlineBuffer(bw, bh);

        // Convert DS dirty rect → pixel rect using the painter's current transform
        QRectF dsDirty(minX - 1.0f, minY - 1.0f, maxX - minX + 2.0f, maxY - minY + 2.0f);
        QRect pixDirty = p->transform().mapRect(dsDirty).toAlignedRect().intersected(olBuf.rect());

        {
            QPainter olP(&olBuf);
            olP.setRenderHint(QPainter::Antialiasing, false);
            // Clear dirty region (no transform yet — pixDirty is in pixel coords)
            olP.setCompositionMode(QPainter::CompositionMode_Source);
            olP.fillRect(pixDirty, Qt::transparent);
            // Draw in DS space by inheriting the same transform as p
            olP.setCompositionMode(QPainter::CompositionMode_SourceOver);
            olP.setTransform(p->transform());
            static const QColor solidBlack(0, 0, 0, 255);

            for (int i = 0; i < nInner; i++)
                olP.fillRect(innerRects[i].adjusted(-olT, -olT, olT, olT), solidBlack);
            for (int i = 0; i < nOuter; i++)
                olP.fillRect(outerRects[i].adjusted(-olT, -olT, olT, olT), solidBlack);

            if (c.crosshair.chCenterDot) {
                float halfW = dotH;
                olP.fillRect(QRectF(cx - halfW, cy - halfW, halfW * 2.0f, halfW * 2.0f), solidBlack);
            }
        }
        // Composite: reset to pixel coords and blit just the dirty region
        p->save();
        p->resetTransform();
        p->setOpacity(c.crosshair.chOutlineOpacity);
        p->drawImage(pixDirty.topLeft(), olBuf, pixDirty);
        p->restore();
    }

    // Inner arms (drawn in DS space — painter transform handles scaling)
    if (nInner > 0) {
        QColor clr = c.crosshair.chColor; clr.setAlphaF(c.crosshair.chInnerOpacity);
        for (int i = 0; i < nInner; i++)
            p->fillRect(innerRects[i], clr);
    }

    // Outer arms
    if (nOuter > 0) {
        QColor clr = c.crosshair.chColor; clr.setAlphaF(c.crosshair.chOuterOpacity);
        for (int i = 0; i < nOuter; i++)
            p->fillRect(outerRects[i], clr);
    }

    // Center dot
    if (c.crosshair.chCenterDot) {
        QColor dotColor = c.crosshair.chColor;
        dotColor.setAlphaF(c.crosshair.chDotOpacity);
        const float dh = c.crosshair.chDotThickness * cs * 0.5f;
        p->fillRect(QRectF(cx - dh, cy - dh, c.crosshair.chDotThickness * cs, c.crosshair.chDotThickness * cs), dotColor);
    }
}

// =========================================================================
//  P-7 forward declarations (full definitions follow CustomHud_Render)
// =========================================================================
static constexpr int kEditElemCount   = 12;
static bool          s_editMode       = false;
static QRectF        s_editRects[kEditElemCount];
static float         s_editHudScale    = 1.0f;
static float         s_editTopStretchX = 1.0f;
static bool          s_editPreviewMode = false;
static RomAddresses     s_editRomCopy       = {};
static GameAddressesHot s_editAddrHotCopy   = {};
static uint8_t          s_editPlayerPosCopy = 0;
static QRectF ComputeEditBounds(int idx, Config::Table& cfg, float topStretchX);
static void   DrawEditOverlay(QPainter* p, Config::Table& cfg, float topStretchX);

// =========================================================================
//  CustomHud_Render — main entry point
// =========================================================================
HOT_FUNCTION void CustomHud_Render(
    EmuInstance* emu, Config::Table& localCfg,
    const RomAddresses& rom, const GameAddressesHot& addrHot,
    uint8_t playerPosition,
    QPainter* topPaint, QPainter* btmPaint,
    QImage* topBuffer, QImage* btmBuffer,
    bool isInGame,
    float topStretchX, float hudScale)
{
    // Edit mode: draw element overlay every frame, skip normal HUD rendering.
    if (UNLIKELY(s_editMode)) {
        s_editHudScale      = hudScale;
        s_editTopStretchX   = topStretchX;
        s_editRomCopy       = rom;
        s_editAddrHotCopy   = addrHot;
        s_editPlayerPosCopy = playerPosition;
        if (UNLIKELY(!s_cache.valid)) {
            RefreshCachedConfig(localCfg, topStretchX);
            s_cache.valid = true;
        } else if (s_cache.lastStretchX != topStretchX) {
            RecomputeAnchorPositions(topStretchX);
        }
        topPaint->scale(hudScale, hudScale);
        if (topStretchX != 1.0f)
            topPaint->translate((topStretchX - 1.0f) * 128.0f, 0.0f);
        if (topPaint->font().pixelSize() != kCustomHudFontSize) {
            QFont f = topPaint->font();
            f.setPixelSize(kCustomHudFontSize);
            topPaint->setFont(f);
        }
        for (int i = 0; i < kEditElemCount; ++i)
            s_editRects[i] = ComputeEditBounds(i, localCfg, topStretchX);
        DrawEditOverlay(topPaint, localCfg, topStretchX);
        return;
    }

    if (!isInGame) return;

    melonDS::NDS* nds = emu->getNDS();
    melonDS::u8* ram = nds->MainRAM;
    const uint32_t offP = static_cast<uint32_t>(playerPosition) * Consts::PLAYER_ADDR_INC;
    const uint8_t romGroup = rom.romGroupIndex;

    if (!CustomHud_IsEnabled(localCfg)) {
        if (s_hudPatchApplied) {
            RestoreHudPatch(nds, romGroup);
            uint8_t vm = Read8(ram, rom.baseViewMode + offP);
            Write8(ram, rom.hudToggle, (vm == 0x00) ? 0x1F : 0x11);
        }
        return;
    }

    ApplyNoHudPatch(nds, romGroup);

    // P-3: Refresh config cache only when invalidated, or recompute anchor
    //       positions when topStretchX changes (window resize).
    if (UNLIKELY(!s_cache.valid)) {
        RefreshCachedConfig(localCfg, topStretchX);
        s_cache.valid = true;
    } else if (s_cache.lastStretchX != topStretchX) {
        RecomputeAnchorPositions(topStretchX);
    }
    const CachedHudConfig& c = s_cache;

    const uint32_t addrAmmoSpecial = rom.currentAmmoSpecial + offP;
    const uint32_t addrAmmoMissile = rom.currentAmmoMissile + offP;
    const uint16_t maxHP           = Read16(ram, rom.maxHP + offP);
    const uint16_t maxAmmoSpecial  = Read16(ram, rom.maxAmmoSpecial + offP);
    const uint16_t maxAmmoMissile  = Read16(ram, rom.maxAmmoMissile + offP);

    bool isStartPressed = Read8(ram, rom.startPressed) == 0x01;
    Write8(ram, rom.hudToggle, isStartPressed ? 0x11 : 0x01);

    uint16_t currentHP = Read16(ram, rom.playerHP + offP);
    bool isGameOver    = Read8(ram, rom.gameOver) != 0x00;
    uint8_t viewMode   = Read8(ram, rom.baseViewMode + offP);
    bool isFirstPerson = (viewMode == 0x00);

    if (ShouldHideForGameplayState(isStartPressed, currentHP, isGameOver)) return;

    // hudScale = scaleY: DS Y-unit maps to hudScale px. Buffer is scaleX*256 × scaleY*192
    // (full displayed area). Translate centres the DS canvas in X when topStretchX != 1:
    //   topStretchX > 1: buffer wider than 256 DS units → centre DS zone (widescreen)
    //   topStretchX < 1: buffer narrower than 256 DS units → centre DS zone (narrow window)
    //   topStretchX = 1: exact 4:3, no shift needed
    topPaint->scale(hudScale, hudScale);
    if (topStretchX != 1.0f)
        topPaint->translate((topStretchX - 1.0f) * 128.0f, 0.0f);

    // Font locked at kCustomHudFontSize (6px) — optimal for this TTF.
    // Visual text size is controlled by textDrawScale applied at draw time.
    if (topPaint->font().pixelSize() != kCustomHudFontSize) {
        QFont f = topPaint->font();
        f.setPixelSize(kCustomHudFontSize);
        topPaint->setFont(f);
    }
    const float textDrawScale = c.textScalePct / 100.0f;
    s_cache.lastHudScale = hudScale;

    const uint8_t hunterID = Read8(ram, addrHot.chosenHunter);
    bool isAlt   = Read8(ram, addrHot.isAltForm) == 0x02;

    {
        DrawHP(topPaint, currentHP, maxHP, c, textDrawScale);
    }

    // Bomb count: Samus/Sylux in alt form only
    {
        bool isBomber = (hunterID == static_cast<uint8_t>(HunterId::Samus) ||
                         hunterID == static_cast<uint8_t>(HunterId::Sylux));
        if (isBomber && isAlt) {
            DrawBombLeft(topPaint, ram, rom, offP, c, textDrawScale, hudScale);
        }
    }

    // Match Status + Rank & Time HUDs (non-adventure only, visible in all camera modes)
    {
        bool isAdventure = Read8(ram, rom.isInAdventure) == 0x02;
        {
            DrawMatchStatusHud(topPaint, ram, rom, playerPosition, isAdventure, c);
        }
        {
            DrawRankAndTime(topPaint, ram, rom, playerPosition, isAdventure, c, textDrawScale);
        }
    }

    if (!isFirstPerson) return;

    uint8_t currentWeapon = Read8(ram, addrHot.currentWeapon);
    {
        DrawWeaponAmmo(topPaint, ram, currentWeapon,
                       Read16(ram, addrAmmoSpecial), addrAmmoMissile,
                       maxAmmoSpecial, maxAmmoMissile, c, textDrawScale, hudScale);
    }

    bool isTrans = (Read8(ram, addrHot.jumpFlag) & 0x10) != 0;
    if (!isTrans && !isAlt)
        DrawCrosshair(topPaint, ram, rom, c, hudScale, topStretchX);

    // Draw bottom screen overlay on top screen
    DrawBottomScreenOverlay(localCfg, topPaint, btmBuffer, (hunterID <= 6) ? hunterID : 0);
}

void DrawBottomScreenOverlay(Config::Table& localCfg, QPainter* topPaint, QImage* btmBuffer, uint8_t hunterID)
{
    if (UNLIKELY(!s_cache.valid)) {
        RefreshCachedConfig(localCfg, 1.0f);
        s_cache.valid = true;
    }

    const CachedHudConfig& c = s_cache;
    if (!c.radar.radarShow) return;
    if (!topPaint || !btmBuffer || btmBuffer->isNull()) return;

    const int srcCenterX  = kBtmOverlaySrcCenterX;
    const int srcCenterY  = kBtmOverlaySrcCenterY[hunterID];
    const int srcRadius   = c.radar.radarSrcRadius;
    const float bufScaleX = static_cast<float>(btmBuffer->width()) / 256.0f;
    const float bufScaleY = static_cast<float>(btmBuffer->height()) / 192.0f;

    QRect srcRect(static_cast<int>((srcCenterX - srcRadius) * bufScaleX),
                  static_cast<int>((srcCenterY - srcRadius) * bufScaleY),
                  static_cast<int>(srcRadius * 2 * bufScaleX),
                  static_cast<int>(srcRadius * 2 * bufScaleY));

    topPaint->save();
    topPaint->setRenderHint(QPainter::SmoothPixmapTransform, true);
    topPaint->setOpacity(c.radar.radarOpacity);
    topPaint->setClipPath(c.radar.radarClipPath);
    topPaint->drawImage(c.radar.radarDstRect, *btmBuffer, srcRect);
    topPaint->restore();

    // Outline ring around the radar circle
    if (c.outline.enable && c.outline.opacity > 0.0f) {
        // penW in DS units: thickness output pixels / hudScale → DS units (painter already scaled)
        const float penW = (c.lastHudScale > 0.0f)
                           ? static_cast<float>(c.outline.thickness * 2) / c.lastHudScale
                           : static_cast<float>(c.outline.thickness * 2);
        QColor olc = c.outline.color;
        olc.setAlphaF(c.outline.opacity);
        topPaint->save();
        topPaint->setRenderHint(QPainter::Antialiasing, true);
        topPaint->setPen(QPen(olc, penW));
        topPaint->setBrush(Qt::NoBrush);
        // Shrink rect by half pen width to keep ring inside the clip circle
        const float half = penW * 0.5f;
        topPaint->drawEllipse(QRectF(c.radar.radarDstRect).adjusted(half, half, -half, -half));
        topPaint->restore();
    }
}

// =========================================================================
//  P-7: HUD Layout Editor — implementation lives in a separate file.
//  This is a unity-build include: HudConfigScreen shares all statics above.
// =========================================================================
#include "MelonPrimeHudConfigScreen.cpp"

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD





