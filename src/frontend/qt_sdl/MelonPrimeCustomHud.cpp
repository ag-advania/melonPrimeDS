#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeCustomHud.h"
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
static QColor s_weaponTintColor;
static int    s_weaponIconHeight = 0;
static bool   s_weaponTintCacheValid = false;

// Bomb icon cache — 4 icons (index 0-3 = bombs remaining)
static QImage s_bombIcons[4];
static QImage s_bombTintedIcons[4];
static QColor s_bombTintColor;
static int    s_bombIconHeight = 0;
static bool   s_bombTintCacheValid = false;

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
}

static const QImage& GetWeaponIconForDraw(uint8_t weapon, bool useOverlay, const QColor& overlayColor)
{
    if (!useOverlay || (weapon != 0 && weapon != 2))
        return s_weaponIcons[weapon];

    if (!s_weaponTintCacheValid || s_weaponTintColor != overlayColor) {
        for (uint8_t idx : { static_cast<uint8_t>(0), static_cast<uint8_t>(2) }) {
            if (s_weaponIcons[idx].isNull())
                continue;

            QImage tinted = s_weaponIcons[idx].copy();
            QPainter tintPainter(&tinted);
            tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tintPainter.fillRect(tinted.rect(), overlayColor);
            tintPainter.end();
            s_weaponTintedIcons[idx] = std::move(tinted);
        }

        s_weaponTintColor = overlayColor;
        s_weaponTintCacheValid = true;
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
    }
    s_weaponTintCacheValid = false;
    s_weaponIconHeight = targetH;
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
    bool iconShow, iconColorOverlay, ammoGauge;
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
    int textScalePct; // text visual scale in percent (100 = 1×, bitmap always rendered at 6px)
    float lastStretchX; // topStretchX used for last anchor position computation
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
    weapon.iconColorOverlay = cfg.GetBool("Metroid.Visual.HudWeaponIconColorOverlay");
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
    c.textScalePct = std::max(10, cfg.GetInt("Metroid.Visual.HudTextScale"));
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
static void DrawMatchStatusText(QPainter* p, const QFontMetrics& fm, int fontPixelSize, float tds, const MatchStatusResolvedState& state, const MatchStatusHudConfig& c)
{
    static TextMeasureCache s_curTextCache = { 0, "", 0, 0, false }, s_sepTextCache = { 0, "", 0, 0, false }, s_goalTextCache = { 0, "", 0, 0, false };
    static TextBitmapCache s_curBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() }, s_sepBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() }, s_goalBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() }, s_labelBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() };
    static MatchStatusStringCache s_matchStringCache = { 0, 0, 0, 0, 0, false, false, false, "", "", "" };
    UpdateMatchStatusStrings(s_matchStringCache, state);
    auto eff = [&](const QColor& sub) -> const QColor& { return sub.isValid() ? sub : c.matchStatusColor; };
    int vx = c.matchStatusX, vy = c.matchStatusY, curW = 0, curH = 0;
    MeasureTextCached(fm, fontPixelSize, s_curTextCache, s_matchStringCache.curBuf, curW, curH, tds);
    PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_curBitmapCache, s_matchStringCache.curBuf, eff(c.matchStatusValueColor)); DrawCachedText(p, s_curBitmapCache, vx, vy, tds);
    if (s_matchStringCache.hasGoal) { int sepW = 0, sepH = 0, goalW = 0, goalH = 0; MeasureTextCached(fm, fontPixelSize, s_sepTextCache, s_matchStringCache.sepBuf, sepW, sepH, tds); MeasureTextCached(fm, fontPixelSize, s_goalTextCache, s_matchStringCache.goalBuf, goalW, goalH, tds); int x = vx + curW; PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_sepBitmapCache, s_matchStringCache.sepBuf, eff(c.matchStatusSepColor)); DrawCachedText(p, s_sepBitmapCache, x, vy, tds); x += sepW; PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_goalBitmapCache, s_matchStringCache.goalBuf, eff(c.matchStatusGoalColor)); DrawCachedText(p, s_goalBitmapCache, x, vy, tds); }
    const char* label = ResolveMatchStatusLabel(state.mode, c); if (label[0] == '\0') return; int lx = vx, ly = vy; switch (c.matchStatusLabelPos) { default: case 0: ly = vy - 10; break; case 1: ly = vy + 10; break; case 2: lx = vx - 50; break; case 3: lx = vx + 50; break; case 4: break; } lx += c.matchStatusLabelOfsX; ly += c.matchStatusLabelOfsY; PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_labelBitmapCache, label, eff(c.matchStatusLabelColor)); DrawCachedText(p, s_labelBitmapCache, lx, ly, tds);
}
static void DrawMatchStatusHud(QPainter* p, melonDS::u8* ram, const RomAddresses& rom, uint8_t playerPos, bool isAdventure, const CachedHudConfig& c)
{
    if (!c.matchStatus.matchStatusShow || isAdventure) return; MatchStatusResolvedState state = {}; if (!ComputeMatchStatusState(ram, rom, playerPos, state)) return; const QFontMetrics fm = p->fontMetrics(); const int fontPixelSize = p->font().pixelSize(); DrawMatchStatusText(p, fm, fontPixelSize, c.textScalePct / 100.0f, state, c.matchStatus);
}
static int CalcAlignedTextX(int anchorX, int align, int textW);
static void DrawCachedAlignedText(QPainter* p, const QFontMetrics& fm, int fontPixelSize, float tds, TextMeasureCache& measureCache, TextBitmapCache& bitmapCache, const char* text, const QColor& color, int anchorX, int align, int y)
{
    int textW = 0, textH = 0; MeasureTextCached(fm, fontPixelSize, measureCache, text, textW, textH, tds); const int textX = CalcAlignedTextX(anchorX, align, textW); PrepareTextBitmapCached(fm, p->font(), fontPixelSize, bitmapCache, text, color); DrawCachedText(p, bitmapCache, textX, y, tds);
}
// =========================================================================
static int CalcAlignedTextX(int anchorX, int align, int textW);
// =========================================================================
//  Bomb Left HUD
// =========================================================================
static void DrawBombLeft(QPainter* p, melonDS::u8* ram, const RomAddresses& rom, uint32_t offP, const CachedHudConfig& c, float tds, float hudScale)
{
    if (!c.bombLeft.bombLeftShow) return; uint8_t bombs = static_cast<uint8_t>((Read32(ram, rom.baseBomb + offP) >> 8) & 0xF);
    { static TextBitmapCache s_bombBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() }; const QFontMetrics fm = p->fontMetrics(); const int fontPixelSize = p->font().pixelSize(); char buf[64]; if (c.bombLeft.bombLeftTextShow) std::snprintf(buf, sizeof(buf), "%s%u%s", c.bombLeft.bombLeftPrefix, bombs, c.bombLeft.bombLeftSuffix); else std::snprintf(buf, sizeof(buf), "%s%s", c.bombLeft.bombLeftPrefix, c.bombLeft.bombLeftSuffix); if (buf[0] != '\0') { static TextMeasureCache s_bombMeasureCache = { 0, "", 0, 0, false }; int bombTextW = 0, bombTextH = 0; MeasureTextCached(fm, fontPixelSize, s_bombMeasureCache, buf, bombTextW, bombTextH, tds); PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_bombBitmapCache, buf, c.bombLeft.bombLeftColor); const int bombTextX = CalcAlignedTextX(c.bombLeft.bombLeftX, c.bombLeft.bombLeftAlign, bombTextW); DrawCachedText(p, s_bombBitmapCache, bombTextX, c.bombLeft.bombLeftY, tds); } }
    if (c.bombLeft.bombIconShow) { EnsureBombIconsLoaded(c.bombLeft.bombIconHeight, hudScale); const QImage& icon = GetBombIconForDraw(bombs, c.bombLeft.bombIconColorOverlay, c.bombLeft.bombIconColor); if (!icon.isNull()) { const float dw = icon.width() / hudScale; const float dh = icon.height() / hudScale; float ix = (c.bombLeft.bombIconMode == 0) ? c.bombLeft.bombLeftX + c.bombLeft.bombIconOfsX : c.bombLeft.bombIconPosX; float iy = (c.bombLeft.bombIconMode == 0) ? c.bombLeft.bombLeftY + c.bombLeft.bombIconOfsY : c.bombLeft.bombIconPosY; const int iconAlignX = c.bombLeft.bombIconAnchorX; const int iconAlignY = c.bombLeft.bombIconAnchorY; if (iconAlignX == 1) ix -= dw * 0.5f; else if (iconAlignX == 2) ix -= dw; if (iconAlignY == 1) iy -= dh * 0.5f; else if (iconAlignY == 2) iy -= dh; p->drawImage(QRectF(ix, iy, dw, dh), icon); } }
}
//  Rank & Time HUD
// =========================================================================
static void DrawRankAndTime(QPainter* p, melonDS::u8* ram, const RomAddresses& rom, uint8_t playerPos, bool isAdventure, const CachedHudConfig& c, float tds)
{
    if (isAdventure) return; const auto& hud = c.rankTime; const QFontMetrics fm = p->fontMetrics(); const int fontPixelSize = p->font().pixelSize();
    if (hud.rankShow) { static RankStringCache s_rankStringCache = { 0, 0, false, false, "" }; static TextBitmapCache s_rankCache = { 0, QColor(), "", 0, 0, false, QImage() }; static TextMeasureCache s_rankMeasure = { 0, "", 0, 0, false }; uint32_t rankWord = Read32(ram, rom.matchRank); uint8_t rankByte = (rankWord >> (playerPos * 8)) & 0xFF; if (rankByte <= 3) DrawCachedAlignedText(p, fm, fontPixelSize, tds, s_rankMeasure, s_rankCache, UpdateRankString(s_rankStringCache, rankByte, hud.rankShowOrdinal, hud), hud.rankColor, hud.rankX, hud.rankAlign, hud.rankY); }
    if (hud.timeLeftShow) { static TimeStringCache s_timeLeftStringCache = { 0, 0, false, "" }; static TextBitmapCache s_timeLeftCache = { 0, QColor(), "", 0, 0, false, QImage() }; static TextMeasureCache s_timeLeftMeasure = { 0, "", 0, 0, false }; int seconds = static_cast<int>(Read32(ram, rom.timeLeft)) / 60; DrawCachedAlignedText(p, fm, fontPixelSize, tds, s_timeLeftMeasure, s_timeLeftCache, UpdateTimeString(s_timeLeftStringCache, seconds, false), hud.timeLeftColor, hud.timeLeftX, hud.timeLeftAlign, hud.timeLeftY); }
    if (hud.timeLimitShow) { static TimeStringCache s_timeLimitStringCache = { 0, 0, false, "" }; static TextBitmapCache s_timeLimitCache = { 0, QColor(), "", 0, 0, false, QImage() }; static TextMeasureCache s_timeLimitMeasure = { 0, "", 0, 0, false }; int goalMinutes = s_battleState.valid ? s_battleState.timeLimitMinutes : LookupTimeLimitMin((Read32(ram, rom.battleSettings + 4) >> 8) & 0xFF); DrawCachedAlignedText(p, fm, fontPixelSize, tds, s_timeLimitMeasure, s_timeLimitCache, UpdateTimeString(s_timeLimitStringCache, goalMinutes, true), hud.timeLimitColor, hud.timeLimitX, hud.timeLimitAlign, hud.timeLimitY); }
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
}

// =========================================================================
//  Gauge drawing
// =========================================================================
static void DrawGauge(QPainter* p, int x, int y, float ratio,
                      const QColor& fillColor, int orientation,
                      int barLength, int barWidth)
{
    ratio = (ratio < 0.0f) ? 0.0f : (ratio > 1.0f) ? 1.0f : ratio;
    if (barLength <= 0) barLength = 28;
    if (barWidth  <= 0) barWidth  = 3;

    static const QColor bgColor(0, 0, 0, 128); // P-4: construct once

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
    static TextBitmapCache s_hpBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() };
    const QFontMetrics fm = p->fontMetrics();
    const int fontPixelSize = p->font().pixelSize();

    char buf[24];
    std::snprintf(buf, sizeof(buf), "%s%u", c.hp.hpPrefix, hp);
    int textW = 0, textH = 0;
    MeasureTextCached(fm, fontPixelSize, s_hpTextCache, buf, textW, textH, tds);
    const int textX = CalcAlignedTextX(c.hp.hpX, c.hp.hpAlign, textW);
    PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_hpBitmapCache, buf, hpTextColor);
    DrawCachedText(p, s_hpBitmapCache, textX, c.hp.hpY, tds);

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
        DrawGauge(p, gx, gy, ratio, gc, c.hp.hpGaugeOri, c.hp.hpGaugeLen, c.hp.hpGaugeWid);
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
    static TextBitmapCache s_ammoBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() };
    const QFontMetrics fm = p->fontMetrics();
    const int fontPixelSize = p->font().pixelSize();

    const WeaponInfo& wi = kWeaponTable[weapon];
    EnsureIconsLoaded(c.weapon.iconHeight, hudScale);
    const QImage& icon = GetWeaponIconForDraw(weapon, c.weapon.iconColorOverlay, c.weapon.ammoGaugeColor); // P-1

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
        DrawCachedText(p, s_ammoBitmapCache, textX, textY, tds);
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
        p->drawImage(QRectF(ix, iy, dw, dh), icon);
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
        DrawGauge(p, gx, gy, ratio, c.weapon.ammoGaugeColor, c.weapon.ammoGaugeOri, c.weapon.ammoGaugeLen, c.weapon.ammoGaugeWid);
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
        const float olT = static_cast<float>(c.crosshair.chOutlineThickness);
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
        s_editHudScale    = hudScale;
        s_editTopStretchX = topStretchX;
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

    const uint8_t hunterID = Read8(ram, addrHot.chosenHunter);
    bool isAlt   = Read8(ram, addrHot.isAltForm) == 0x02;

    {
        double op = localCfg.GetDouble("Metroid.Visual.HudHpOpacity");
        if (op < 1.0) topPaint->setOpacity(op);
        DrawHP(topPaint, currentHP, maxHP, c, textDrawScale);
        if (op < 1.0) topPaint->setOpacity(1.0);
    }

    // Bomb count: Samus/Sylux in alt form only
    {
        bool isBomber = (hunterID == static_cast<uint8_t>(HunterId::Samus) ||
                         hunterID == static_cast<uint8_t>(HunterId::Sylux));
        if (isBomber && isAlt) {
            double op = localCfg.GetDouble("Metroid.Visual.HudBombLeftOpacity");
            if (op < 1.0) topPaint->setOpacity(op);
            DrawBombLeft(topPaint, ram, rom, offP, c, textDrawScale, hudScale);
            if (op < 1.0) topPaint->setOpacity(1.0);
        }
    }

    // Match Status + Rank & Time HUDs (non-adventure only, visible in all camera modes)
    {
        bool isAdventure = Read8(ram, rom.isInAdventure) == 0x02;
        {
            double op = localCfg.GetDouble("Metroid.Visual.HudMatchStatusOpacity");
            if (op < 1.0) topPaint->setOpacity(op);
            DrawMatchStatusHud(topPaint, ram, rom, playerPosition, isAdventure, c);
            if (op < 1.0) topPaint->setOpacity(1.0);
        }
        {
            double op = localCfg.GetDouble("Metroid.Visual.HudRankOpacity");
            if (op < 1.0) topPaint->setOpacity(op);
            DrawRankAndTime(topPaint, ram, rom, playerPosition, isAdventure, c, textDrawScale);
            if (op < 1.0) topPaint->setOpacity(1.0);
        }
    }

    if (!isFirstPerson) return;

    uint8_t currentWeapon = Read8(ram, addrHot.currentWeapon);
    {
        double op = localCfg.GetDouble("Metroid.Visual.HudWeaponOpacity");
        if (op < 1.0) topPaint->setOpacity(op);
        DrawWeaponAmmo(topPaint, ram, currentWeapon,
                       Read16(ram, addrAmmoSpecial), addrAmmoMissile,
                       maxAmmoSpecial, maxAmmoMissile, c, textDrawScale, hudScale);
        if (op < 1.0) topPaint->setOpacity(1.0);
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
}

// =========================================================================
//  P-7: HUD Layout Editor
// =========================================================================

enum class EditPropType : uint8_t { Bool, Int, Float, String, SubColor, Color };

struct HudEditPropDesc {
    const char* label;       // tiny label e.g. "Align", "Pfx"
    EditPropType type;
    const char* cfgKey;      // config key. SubColor: overall-bool key
    int minVal, maxVal;      // Int range. Float: value*100 range (e.g. 0-100 for 0.0-1.0)
    int step;                // 0 = use default (1 for Int, 5 for Float)
    const char* extra1;      // SubColor: R key
    const char* extra2;      // SubColor: G key
    const char* extra3;      // SubColor: B key
};

struct HudEditElemDesc {
    const char* name;
    const char* anchorKey;
    const char* ofsXKey;
    const char* ofsYKey;
    const char* orientKey;   // nullptr if no orientation toggle
    const char* lengthKey;   // nullptr if no resize handles
    const char* widthKey;    // nullptr if no resize handles
    const char* posModeKey;  // nullptr if no PosMode switch
    const char* showKey;     // nullptr = always visible (no toggle)
    const char* colorRKey;   // nullptr = no color picker
    const char* colorGKey;
    const char* colorBKey;
    const HudEditPropDesc* props;   // additional properties (nullable)
    int propCount;                  // number of props
};

static const HudEditPropDesc kPropsHp[] = {
    {"Prefix",    EditPropType::String, "Metroid.Visual.HudHpPrefix", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Align",     EditPropType::Int,    "Metroid.Visual.HudHpAlign", 0, 2, 1, nullptr, nullptr, nullptr},
    {"Auto Color",EditPropType::Bool,   "Metroid.Visual.HudHpTextAutoColor", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",   EditPropType::Float,  "Metroid.Visual.HudHpOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsHpGauge[] = {
    {"Auto Color",EditPropType::Bool, "Metroid.Visual.HudHpGaugeAutoColor", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",   EditPropType::Float,"Metroid.Visual.HudHpGaugeOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsWeaponAmmo[] = {
    {"Prefix", EditPropType::String, "Metroid.Visual.HudAmmoPrefix", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Align",  EditPropType::Int,    "Metroid.Visual.HudAmmoAlign", 0, 2, 1, nullptr, nullptr, nullptr},
    {"Opacity",EditPropType::Float,  "Metroid.Visual.HudWeaponOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsWpnIcon[] = {
    {"Mode",   EditPropType::Int,  "Metroid.Visual.HudWeaponIconMode", 0, 1, 1, nullptr, nullptr, nullptr},
    {"Tint",   EditPropType::Bool, "Metroid.Visual.HudWeaponIconColorOverlay", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",EditPropType::Float,"Metroid.Visual.HudWpnIconOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsAmmoGauge[] = {
    {"Opacity",EditPropType::Float,"Metroid.Visual.HudAmmoGaugeOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsMatchStatus[] = {
    {"Label Pos",EditPropType::Int,    "Metroid.Visual.HudMatchStatusLabelPos", 0, 3, 1, nullptr, nullptr, nullptr},
    {"Points",   EditPropType::String, "Metroid.Visual.HudMatchStatusLabelPoints", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Octolith", EditPropType::String, "Metroid.Visual.HudMatchStatusLabelOctoliths", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Lives",    EditPropType::String, "Metroid.Visual.HudMatchStatusLabelLives", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Rings",    EditPropType::String, "Metroid.Visual.HudMatchStatusLabelRingTime", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Primes",   EditPropType::String, "Metroid.Visual.HudMatchStatusLabelPrimeTime", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Label",    EditPropType::SubColor,"Metroid.Visual.HudMatchStatusLabelColorOverall", 0, 0, 0,
        "Metroid.Visual.HudMatchStatusLabelColorR","Metroid.Visual.HudMatchStatusLabelColorG","Metroid.Visual.HudMatchStatusLabelColorB"},
    {"Value",    EditPropType::SubColor,"Metroid.Visual.HudMatchStatusValueColorOverall", 0, 0, 0,
        "Metroid.Visual.HudMatchStatusValueColorR","Metroid.Visual.HudMatchStatusValueColorG","Metroid.Visual.HudMatchStatusValueColorB"},
    {"Separator",EditPropType::SubColor,"Metroid.Visual.HudMatchStatusSepColorOverall", 0, 0, 0,
        "Metroid.Visual.HudMatchStatusSepColorR","Metroid.Visual.HudMatchStatusSepColorG","Metroid.Visual.HudMatchStatusSepColorB"},
    {"Goal", EditPropType::SubColor,"Metroid.Visual.HudMatchStatusGoalColorOverall", 0, 0, 0,
        "Metroid.Visual.HudMatchStatusGoalColorR","Metroid.Visual.HudMatchStatusGoalColorG","Metroid.Visual.HudMatchStatusGoalColorB"},
    {"Opacity",  EditPropType::Float,"Metroid.Visual.HudMatchStatusOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsRank[] = {
    {"Prefix", EditPropType::String, "Metroid.Visual.HudRankPrefix", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Suffix", EditPropType::String, "Metroid.Visual.HudRankSuffix", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Align",  EditPropType::Int,    "Metroid.Visual.HudRankAlign", 0, 2, 1, nullptr, nullptr, nullptr},
    {"Ordinal",EditPropType::Bool,   "Metroid.Visual.HudRankShowOrdinal", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",EditPropType::Float,  "Metroid.Visual.HudRankOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsTimeLeft[] = {
    {"Align",  EditPropType::Int,   "Metroid.Visual.HudTimeLeftAlign", 0, 2, 1, nullptr, nullptr, nullptr},
    {"Opacity",EditPropType::Float, "Metroid.Visual.HudTimeLeftOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsTimeLimit[] = {
    {"Align",  EditPropType::Int,   "Metroid.Visual.HudTimeLimitAlign", 0, 2, 1, nullptr, nullptr, nullptr},
    {"Opacity",EditPropType::Float, "Metroid.Visual.HudTimeLimitOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsBombLeft[] = {
    {"Text",   EditPropType::Bool,   "Metroid.Visual.HudBombLeftTextShow", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Prefix", EditPropType::String, "Metroid.Visual.HudBombLeftPrefix", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Suffix", EditPropType::String, "Metroid.Visual.HudBombLeftSuffix", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Align",  EditPropType::Int,    "Metroid.Visual.HudBombLeftAlign", 0, 2, 1, nullptr, nullptr, nullptr},
    {"Opacity",EditPropType::Float,  "Metroid.Visual.HudBombLeftOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsBombIcon[] = {
    {"Mode",   EditPropType::Int,  "Metroid.Visual.HudBombLeftIconMode", 0, 1, 1, nullptr, nullptr, nullptr},
    {"Tint",   EditPropType::Bool, "Metroid.Visual.HudBombLeftIconColorOverlay", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",EditPropType::Float,"Metroid.Visual.HudBombIconOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsRadar[] = {
    {"Opacity",   EditPropType::Float,"Metroid.Visual.BtmOverlayOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Src Radius",EditPropType::Int,  "Metroid.Visual.BtmOverlaySrcRadius", 10, 96, 1, nullptr, nullptr, nullptr},
};

// ── Crosshair edit-mode props ────────────────────────────────────────────
static const HudEditPropDesc kPropsCrosshairMain[] = {
    {"Outline",          EditPropType::Bool,  "Metroid.Visual.CrosshairOutline", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Outline Opacity",  EditPropType::Float, "Metroid.Visual.CrosshairOutlineOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Outline Thick.",   EditPropType::Int,   "Metroid.Visual.CrosshairOutlineThickness", 1, 10, 1, nullptr, nullptr, nullptr},
    {"Center Dot",       EditPropType::Bool,  "Metroid.Visual.CrosshairCenterDot", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Dot Opacity",      EditPropType::Float, "Metroid.Visual.CrosshairDotOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Dot Thick.",       EditPropType::Int,   "Metroid.Visual.CrosshairDotThickness", 1, 10, 1, nullptr, nullptr, nullptr},
    {"T-Style",          EditPropType::Bool,  "Metroid.Visual.CrosshairTStyle", 0, 0, 0, nullptr, nullptr, nullptr},
};
static constexpr int kCrosshairMainCount = 7;

static const HudEditPropDesc kPropsCrosshairInner[] = {
    {"Show",      EditPropType::Bool,  "Metroid.Visual.CrosshairInnerShow", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",   EditPropType::Float, "Metroid.Visual.CrosshairInnerOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Length X",  EditPropType::Int,   "Metroid.Visual.CrosshairInnerLengthX", 0, 64, 1, nullptr, nullptr, nullptr},
    {"Length Y",  EditPropType::Int,   "Metroid.Visual.CrosshairInnerLengthY", 0, 64, 1, nullptr, nullptr, nullptr},
    {"Link XY",  EditPropType::Bool,  "Metroid.Visual.CrosshairInnerLinkXY", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Thickness", EditPropType::Int,   "Metroid.Visual.CrosshairInnerThickness", 1, 10, 1, nullptr, nullptr, nullptr},
    {"Offset",    EditPropType::Int,   "Metroid.Visual.CrosshairInnerOffset", 0, 64, 1, nullptr, nullptr, nullptr},
};
static constexpr int kCrosshairInnerCount = 7;

static const HudEditPropDesc kPropsCrosshairOuter[] = {
    {"Show",      EditPropType::Bool,  "Metroid.Visual.CrosshairOuterShow", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",   EditPropType::Float, "Metroid.Visual.CrosshairOuterOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Length X",  EditPropType::Int,   "Metroid.Visual.CrosshairOuterLengthX", 0, 64, 1, nullptr, nullptr, nullptr},
    {"Length Y",  EditPropType::Int,   "Metroid.Visual.CrosshairOuterLengthY", 0, 64, 1, nullptr, nullptr, nullptr},
    {"Link XY",  EditPropType::Bool,  "Metroid.Visual.CrosshairOuterLinkXY", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Thickness", EditPropType::Int,   "Metroid.Visual.CrosshairOuterThickness", 1, 10, 1, nullptr, nullptr, nullptr},
    {"Offset",    EditPropType::Int,   "Metroid.Visual.CrosshairOuterOffset", 0, 64, 1, nullptr, nullptr, nullptr},
};
static constexpr int kCrosshairOuterCount = 7;

// Crosshair panel state
static bool s_crosshairPanelOpen  = false;
static bool s_innerSectionOpen    = false;
static bool s_outerSectionOpen    = false;
static int  s_crosshairPanelScroll = 0;

static int CountCrosshairRows() {
    return 1 + kCrosshairMainCount + 2; // color + main props + Inner header + Outer header
}

static const HudEditPropDesc* GetCrosshairPropForRow(int rowIdx, bool& isColorRow,
                                                     bool& isInnerHeader, bool& isOuterHeader)
{
    isColorRow = isInnerHeader = isOuterHeader = false;
    if (rowIdx == 0) { isColorRow = true; return nullptr; }
    int idx = rowIdx - 1;
    if (idx < kCrosshairMainCount) return &kPropsCrosshairMain[idx];
    idx -= kCrosshairMainCount;
    if (idx == 0) { isInnerHeader = true; return nullptr; }
    idx -= 1;
    if (idx == 0) { isOuterHeader = true; return nullptr; }
    return nullptr;
}

static const HudEditElemDesc kEditElems[kEditElemCount] = {
    {   // 0: HP text
        "HP",
        "Metroid.Visual.HudHpAnchor",
        "Metroid.Visual.HudHpX",
        "Metroid.Visual.HudHpY",
        nullptr, nullptr, nullptr, nullptr,
        nullptr, // showKey: always visible
        "Metroid.Visual.HudHpTextColorR",
        "Metroid.Visual.HudHpTextColorG",
        "Metroid.Visual.HudHpTextColorB",
        kPropsHp, 4
    },
    {   // 1: HP Gauge (independent position)
        "HP Gauge",
        "Metroid.Visual.HudHpGaugePosAnchor",
        "Metroid.Visual.HudHpGaugePosX",
        "Metroid.Visual.HudHpGaugePosY",
        "Metroid.Visual.HudHpGaugeOrientation",
        "Metroid.Visual.HudHpGaugeLength",
        "Metroid.Visual.HudHpGaugeWidth",
        "Metroid.Visual.HudHpGaugePosMode",
        "Metroid.Visual.HudHpGauge", // showKey
        "Metroid.Visual.HudHpGaugeColorR",
        "Metroid.Visual.HudHpGaugeColorG",
        "Metroid.Visual.HudHpGaugeColorB",
        kPropsHpGauge, 2
    },
    {   // 2: Weapon / Ammo text
        "Weapon/Ammo",
        "Metroid.Visual.HudWeaponAnchor",
        "Metroid.Visual.HudWeaponX",
        "Metroid.Visual.HudWeaponY",
        nullptr, nullptr, nullptr, nullptr,
        nullptr, // showKey: always visible
        "Metroid.Visual.HudAmmoTextColorR",
        "Metroid.Visual.HudAmmoTextColorG",
        "Metroid.Visual.HudAmmoTextColorB",
        kPropsWeaponAmmo, 3
    },
    {   // 3: Weapon icon
        "Wpn\nIcon",
        "Metroid.Visual.HudWeaponIconPosAnchor",
        "Metroid.Visual.HudWeaponIconPosX",
        "Metroid.Visual.HudWeaponIconPosY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudWeaponIconShow", // showKey
        nullptr, nullptr, nullptr, // no color picker
        kPropsWpnIcon, 3
    },
    {   // 4: Ammo gauge (independent position)
        "Ammo Gauge",
        "Metroid.Visual.HudAmmoGaugePosAnchor",
        "Metroid.Visual.HudAmmoGaugePosX",
        "Metroid.Visual.HudAmmoGaugePosY",
        "Metroid.Visual.HudAmmoGaugeOrientation",
        "Metroid.Visual.HudAmmoGaugeLength",
        "Metroid.Visual.HudAmmoGaugeWidth",
        "Metroid.Visual.HudAmmoGaugePosMode",
        "Metroid.Visual.HudAmmoGauge", // showKey
        "Metroid.Visual.HudAmmoGaugeColorR",
        "Metroid.Visual.HudAmmoGaugeColorG",
        "Metroid.Visual.HudAmmoGaugeColorB",
        kPropsAmmoGauge, 1
    },
    {   // 5: Match Status
        "Match Status",
        "Metroid.Visual.HudMatchStatusAnchor",
        "Metroid.Visual.HudMatchStatusX",
        "Metroid.Visual.HudMatchStatusY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudMatchStatusShow", // showKey
        "Metroid.Visual.HudMatchStatusColorR",
        "Metroid.Visual.HudMatchStatusColorG",
        "Metroid.Visual.HudMatchStatusColorB",
        kPropsMatchStatus, 11
    },
    {   // 6: Rank
        "Rank",
        "Metroid.Visual.HudRankAnchor",
        "Metroid.Visual.HudRankX",
        "Metroid.Visual.HudRankY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudRankShow", // showKey
        "Metroid.Visual.HudRankColorR",
        "Metroid.Visual.HudRankColorG",
        "Metroid.Visual.HudRankColorB",
        kPropsRank, 5
    },
    {   // 7: Time Left
        "Time Left",
        "Metroid.Visual.HudTimeLeftAnchor",
        "Metroid.Visual.HudTimeLeftX",
        "Metroid.Visual.HudTimeLeftY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudTimeLeftShow", // showKey
        "Metroid.Visual.HudTimeLeftColorR",
        "Metroid.Visual.HudTimeLeftColorG",
        "Metroid.Visual.HudTimeLeftColorB",
        kPropsTimeLeft, 2
    },
    {   // 8: Time Limit
        "Time Limit",
        "Metroid.Visual.HudTimeLimitAnchor",
        "Metroid.Visual.HudTimeLimitX",
        "Metroid.Visual.HudTimeLimitY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudTimeLimitShow", // showKey
        "Metroid.Visual.HudTimeLimitColorR",
        "Metroid.Visual.HudTimeLimitColorG",
        "Metroid.Visual.HudTimeLimitColorB",
        kPropsTimeLimit, 2
    },
    {   // 9: Bomb Left text
        "Bomb Left",
        "Metroid.Visual.HudBombLeftAnchor",
        "Metroid.Visual.HudBombLeftX",
        "Metroid.Visual.HudBombLeftY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudBombLeftShow", // showKey
        "Metroid.Visual.HudBombLeftColorR",
        "Metroid.Visual.HudBombLeftColorG",
        "Metroid.Visual.HudBombLeftColorB",
        kPropsBombLeft, 5
    },
    {   // 10: Bomb icon
        "Bmb\nIcon",
        "Metroid.Visual.HudBombLeftIconPosAnchor",
        "Metroid.Visual.HudBombLeftIconPosX",
        "Metroid.Visual.HudBombLeftIconPosY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudBombLeftIconShow", // showKey
        "Metroid.Visual.HudBombLeftIconColorR",
        "Metroid.Visual.HudBombLeftIconColorG",
        "Metroid.Visual.HudBombLeftIconColorB",
        kPropsBombIcon, 3
    },
    {   // 11: Radar overlay
        "Radar",
        "Metroid.Visual.BtmOverlayAnchor",
        "Metroid.Visual.BtmOverlayDstX",
        "Metroid.Visual.BtmOverlayDstY",
        nullptr, // orientKey: not applicable
        "Metroid.Visual.BtmOverlayDstSize", // lengthKey (square resize)
        "Metroid.Visual.BtmOverlayDstSize", // widthKey (same key = coupled)
        nullptr, // posModeKey
        "Metroid.Visual.BtmOverlayEnable", // showKey
        nullptr, nullptr, nullptr, // no color picker
        kPropsRadar, 2
    },
};

// ── Edit mode static state ──────────────────────────────────────────────────
static EmuInstance* s_editEmu         = nullptr;
static int          s_editSelected    = -1;
static int          s_editHovered     = -1;
static bool         s_dragging        = false;
static bool         s_anchorPickerOpen = false;  // expanded 3×3 anchor grid visible
static QPointF      s_dragStartDS;
static int          s_dragStartOfsX   = 0;
static int          s_dragStartOfsY   = 0;
static bool         s_resizingLength  = false;
static bool         s_resizingWidth   = false;
static int          s_resizeStartVal  = 0;
static QPointF      s_resizeStartDS;
static std::map<std::string, int> s_editSnapshot;
static std::set<std::string>      s_editSnapshotBools; // keys that need SetBool on restore
static std::map<std::string, double>      s_editSnapshotDoubles;
static std::map<std::string, std::string> s_editSnapshotStrings;
static std::function<void(int)>           s_editSelectionCb; // Side panel callback

static void NotifySelectionChanged(int idx)
{
    if (s_editSelectionCb) s_editSelectionCb(idx);
}

// Context updated by render pass and mouse handlers
static float        s_editOriginX     = 0.0f;
static float        s_editOriginY     = 0.0f;

// Save/Cancel/Reset button rects in DS-space
static const QRectF kEditSaveRect  (10.0f,  1.0f, 74.0f, 12.0f);
static const QRectF kEditCancelRect(88.0f,  1.0f, 74.0f, 12.0f);
static const QRectF kEditResetRect (166.0f, 1.0f, 74.0f, 12.0f);

// Text Scale control and Crosshair button (below button bar)
static const QRectF kEditTextScaleRect(10.0f, 15.0f, 74.0f, 10.0f);
static const QRectF kEditCrosshairBtnRect(88.0f, 15.0f, 74.0f, 10.0f);

// Crosshair panel rect (left side, below control bar)
static constexpr float kCrosshairPanelX = 2.0f;
static constexpr float kCrosshairPanelY = 28.0f;
static constexpr int kCrosshairMaxVisible = 10;

// Properties panel layout constants
static constexpr float kPropRowH    = 8.0f;   // height of each property row (taller for readability)
static constexpr float kPropLabelW  = 52.0f;  // width of label area (wider for full names like "Inner Line Opacity")
static constexpr float kPropCtrlW   = 30.0f;  // width of control area
static constexpr float kPropPanelW  = kPropLabelW + kPropCtrlW + 4.0f; // total width = 86
static constexpr float kAnchorGridCellW = (kPropPanelW - 4.0f) / 3.0f; // 3-col grid cell width
static constexpr float kAnchorGridCellH = 10.0f;  // grid cell height
static constexpr float kAnchorGridH = kAnchorGridCellH * 3 + 4.0f; // expanded anchor picker height
static int s_editPropScroll = 0;  // scroll offset for properties panel
static constexpr int kPropMaxVisible = 8; // max visible rows before scrolling

// Crosshair side panel (Inner/Outer) — positioned to the right of the main panel
static constexpr float kCrosshairSidePanelX  = kCrosshairPanelX + kPropPanelW + 2.0f;  // = 82
static constexpr float kCrosshairPreviewX    = kCrosshairSidePanelX + kPropPanelW + 2.0f; // = 162
static constexpr int   kCrosshairPreviewSize = 64;

// ── Coordinate conversion ───────────────────────────────────────────────────
static inline QPointF WidgetToDS(const QPointF& pt)
{
    const float dsX = static_cast<float>((pt.x() - s_editOriginX) / s_editHudScale)
                      - (s_editTopStretchX - 1.0f) * 128.0f;
    const float dsY = static_cast<float>((pt.y() - s_editOriginY) / s_editHudScale);
    return QPointF(dsX, dsY);
}

// ── Snapshot / restore ──────────────────────────────────────────────────────
static void SnapshotEditConfig(Config::Table& cfg)
{
    s_editSnapshot.clear();
    s_editSnapshotBools.clear();
    for (int i = 0; i < kEditElemCount; ++i) {
        const HudEditElemDesc& d = kEditElems[i];
        s_editSnapshot[d.anchorKey] = cfg.GetInt(d.anchorKey);
        s_editSnapshot[d.ofsXKey]   = cfg.GetInt(d.ofsXKey);
        s_editSnapshot[d.ofsYKey]   = cfg.GetInt(d.ofsYKey);
        if (d.orientKey)  s_editSnapshot[d.orientKey]  = cfg.GetInt(d.orientKey);
        if (d.lengthKey)  s_editSnapshot[d.lengthKey]  = cfg.GetInt(d.lengthKey);
        if (d.widthKey && d.widthKey != d.lengthKey)
            s_editSnapshot[d.widthKey] = cfg.GetInt(d.widthKey);
        if (d.posModeKey) s_editSnapshot[d.posModeKey] = cfg.GetInt(d.posModeKey);
        if (d.showKey) {
            s_editSnapshot[d.showKey] = cfg.GetBool(d.showKey) ? 1 : 0;
            s_editSnapshotBools.insert(d.showKey);
        }
        if (d.colorRKey) {
            s_editSnapshot[d.colorRKey] = cfg.GetInt(d.colorRKey);
            s_editSnapshot[d.colorGKey] = cfg.GetInt(d.colorGKey);
            s_editSnapshot[d.colorBKey] = cfg.GetInt(d.colorBKey);
        }
    }
    s_editSnapshotDoubles.clear();
    s_editSnapshotStrings.clear();
    for (int i = 0; i < kEditElemCount; ++i) {
        const HudEditElemDesc& d = kEditElems[i];
        for (int p = 0; p < d.propCount; ++p) {
            const HudEditPropDesc& pr = d.props[p];
            switch (pr.type) {
            case EditPropType::Bool:
                s_editSnapshot[pr.cfgKey] = cfg.GetBool(pr.cfgKey) ? 1 : 0;
                s_editSnapshotBools.insert(pr.cfgKey);
                break;
            case EditPropType::Int:
                s_editSnapshot[pr.cfgKey] = cfg.GetInt(pr.cfgKey);
                break;
            case EditPropType::Float:
                s_editSnapshotDoubles[pr.cfgKey] = cfg.GetDouble(pr.cfgKey);
                break;
            case EditPropType::String:
                s_editSnapshotStrings[pr.cfgKey] = cfg.GetString(pr.cfgKey);
                break;
            case EditPropType::SubColor:
                s_editSnapshot[pr.cfgKey] = cfg.GetBool(pr.cfgKey) ? 1 : 0; // overall bool
                s_editSnapshotBools.insert(pr.cfgKey);
                s_editSnapshot[pr.extra1] = cfg.GetInt(pr.extra1);
                s_editSnapshot[pr.extra2] = cfg.GetInt(pr.extra2);
                s_editSnapshot[pr.extra3] = cfg.GetInt(pr.extra3);
                break;
            }
        }
    }
}

static void RestoreEditSnapshot(Config::Table& cfg)
{
    for (const auto& kv : s_editSnapshot) {
        if (s_editSnapshotBools.count(kv.first))
            cfg.SetBool(kv.first, kv.second != 0);
        else
            cfg.SetInt(kv.first, kv.second);
    }
    for (const auto& kv : s_editSnapshotDoubles)
        cfg.SetDouble(kv.first, kv.second);
    for (const auto& kv : s_editSnapshotStrings)
        cfg.SetString(kv.first, kv.second);
}

static void ResetEditToDefaults(Config::Table& cfg)
{
    toml::value defData(toml::table{});
    Config::Table defaults(defData, "Instance0");
    for (int i = 0; i < kEditElemCount; ++i) {
        const HudEditElemDesc& d = kEditElems[i];
        cfg.SetInt(d.anchorKey, defaults.GetInt(d.anchorKey));
        cfg.SetInt(d.ofsXKey,   defaults.GetInt(d.ofsXKey));
        cfg.SetInt(d.ofsYKey,   defaults.GetInt(d.ofsYKey));
        if (d.orientKey)  cfg.SetInt(d.orientKey,  defaults.GetInt(d.orientKey));
        if (d.lengthKey)  cfg.SetInt(d.lengthKey,  defaults.GetInt(d.lengthKey));
        if (d.widthKey && d.widthKey != d.lengthKey)
            cfg.SetInt(d.widthKey, defaults.GetInt(d.widthKey));
        if (d.posModeKey) cfg.SetInt(d.posModeKey, defaults.GetInt(d.posModeKey));
        if (d.showKey)    cfg.SetBool(d.showKey,   defaults.GetBool(d.showKey));
        if (d.colorRKey) {
            cfg.SetInt(d.colorRKey, defaults.GetInt(d.colorRKey));
            cfg.SetInt(d.colorGKey, defaults.GetInt(d.colorGKey));
            cfg.SetInt(d.colorBKey, defaults.GetInt(d.colorBKey));
        }
        for (int p = 0; p < d.propCount; ++p) {
            const HudEditPropDesc& pr = d.props[p];
            switch (pr.type) {
            case EditPropType::Bool:
                cfg.SetBool(pr.cfgKey, defaults.GetBool(pr.cfgKey));
                break;
            case EditPropType::Int:
                cfg.SetInt(pr.cfgKey, defaults.GetInt(pr.cfgKey));
                break;
            case EditPropType::Float:
                cfg.SetDouble(pr.cfgKey, defaults.GetDouble(pr.cfgKey));
                break;
            case EditPropType::String:
                cfg.SetString(pr.cfgKey, defaults.GetString(pr.cfgKey));
                break;
            case EditPropType::SubColor:
                cfg.SetBool(pr.cfgKey, defaults.GetBool(pr.cfgKey));
                cfg.SetInt(pr.extra1, defaults.GetInt(pr.extra1));
                cfg.SetInt(pr.extra2, defaults.GetInt(pr.extra2));
                cfg.SetInt(pr.extra3, defaults.GetInt(pr.extra3));
                break;
            }
        }
    }
}

// ── Bounding rect computation ───────────────────────────────────────────────
static QRectF ComputeEditBounds(int idx, Config::Table& cfg, float topStretchX)
{
    const HudEditElemDesc& d = kEditElems[idx];
    const int anchor = cfg.GetInt(d.anchorKey);
    const int ofsX   = cfg.GetInt(d.ofsXKey);
    const int ofsY   = cfg.GetInt(d.ofsYKey);
    int fx, fy;
    ApplyAnchor(anchor, ofsX, ofsY, fx, fy, topStretchX);

    // Gauge: sized by stored length x width
    if (d.lengthKey != nullptr) {
        const int len = std::max(4, cfg.GetInt(d.lengthKey));
        const int wid = std::max(1, cfg.GetInt(d.widthKey));
        const int ori = (d.orientKey != nullptr) ? cfg.GetInt(d.orientKey) : 0;
        if (ori == 1)
            return QRectF(fx, fy, wid, len);
        return QRectF(fx, fy, len, wid);
    }

    // Weapon icon: ~24x24
    if (idx == 3)
        return QRectF(fx - 12.0, fy - 12.0, 24.0, 24.0);

    // Bomb icon: ~16x16
    if (idx == 10)
        return QRectF(fx - 8.0, fy - 8.0, 16.0, 16.0);

    // Radar: dstSize x dstSize
    if (idx == 11) {
        const float sz = static_cast<float>(cfg.GetInt("Metroid.Visual.BtmOverlayDstSize"));
        return QRectF(fx, fy, sz, sz);
    }

    // Text elements: auto-adjust box size based on text scale
    float tds = std::max(0.5f, cfg.GetInt("Metroid.Visual.HudTextScale") / 100.0f);
    return QRectF(fx - 30.0 * tds, fy - 6.0 * tds, 60.0 * tds, 12.0 * tds);
}

// ── Anchor picker placement ─────────────────────────────────────────────────
// Count built-in property rows (Show, Color, Anchor) for an element
static int CountBuiltinRows(const HudEditElemDesc& d) {
    int n = 1; // anchor always present
    if (d.showKey)   ++n;
    if (d.colorRKey) ++n;
    return n;
}

static QRectF ComputePropsPanelRect(const QRectF& elemRect, int totalRowCount)
{
    int visCount = std::min(totalRowCount, kPropMaxVisible);
    float h = visCount * kPropRowH + 4.0f + (s_anchorPickerOpen ? kAnchorGridH : 0.0f);
    float px = static_cast<float>(elemRect.right()) + 4.0f;
    float py = static_cast<float>(elemRect.top());
    if (px + kPropPanelW > 256.0f) px = static_cast<float>(elemRect.left()) - kPropPanelW - 2.0f;
    if (py + h > 192.0f) py = 192.0f - h;
    if (px < 0.0f) px = 0.0f;
    if (py < 26.0f) py = 26.0f;
    return QRectF(px, py, kPropPanelW, h);
}

static QRectF GetOrientToggleRect(int idx)
{
    const QRectF& r = s_editRects[idx];
    QRectF rect(r.right() - 8.0f, r.top() - 11.0f, 10.0f, 10.0f);
    if (rect.top() < 0.0f) rect.moveTop(r.top());
    return rect;
}

static void GetResizeHandles(int idx, Config::Table& cfg,
                              QRectF& lenHandle, QRectF& widHandle)
{
    const QRectF& r = s_editRects[idx];
    const int ori = kEditElems[idx].orientKey ? cfg.GetInt(kEditElems[idx].orientKey) : 0;
    // For square-coupled resize (lengthKey == widthKey), only show one handle
    const bool squareResize = kEditElems[idx].lengthKey && kEditElems[idx].widthKey
                              && strcmp(kEditElems[idx].lengthKey, kEditElems[idx].widthKey) == 0;
    if (ori == 1) {
        lenHandle = QRectF(r.center().x() - 3.0f, r.bottom() - 3.0f, 6.0f, 6.0f);
        widHandle = squareResize ? QRectF() : QRectF(r.right() - 3.0f, r.center().y() - 3.0f, 6.0f, 6.0f);
    } else {
        lenHandle = QRectF(r.right() - 3.0f, r.center().y() - 3.0f, 6.0f, 6.0f);
        widHandle = squareResize ? QRectF() : QRectF(r.center().x() - 3.0f, r.bottom() - 3.0f, 6.0f, 6.0f);
    }
}

// ── DrawEditOverlay ─────────────────────────────────────────────────────────
// Guard to prevent re-entrant QColorDialog
static bool s_colorDialogOpen = false;

static void DrawEditOverlay(QPainter* p, Config::Table& cfg, float topStretchX)
{
    if (!p) return;

    const float leftX = -(topStretchX - 1.0f) * 128.0f;
    p->fillRect(QRectF(leftX, 0.0f, 256.0f * topStretchX, 192.0f), QColor(0, 0, 0, 80));
    p->setPen(Qt::NoPen);

    // Element bounding boxes with live previews
    const float tds = std::max(0.5f, cfg.GetInt("Metroid.Visual.HudTextScale") / 100.0f);
    QFont smallFont = p->font();
    smallFont.setPixelSize(4);
    QFont elemFont = p->font();    // element box font scales with text scale
    elemFont.setPixelSize(std::max(3, static_cast<int>(4.0f * tds)));
    QFont normalFont = p->font();
    normalFont.setPixelSize(6);

    for (int i = 0; i < kEditElemCount; ++i) {
        const QRectF& r = s_editRects[i];
        if (r.isEmpty()) continue;
        const bool sel = (i == s_editSelected);
        const bool hov = (i == s_editHovered);
        const HudEditElemDesc& d = kEditElems[i];

        // Hidden elements get grey fill
        const bool hidden = d.showKey && !cfg.GetBool(d.showKey);
        if (hidden) {
            p->fillRect(r, sel ? QColor(0x88, 0x88, 0x88, 150)
                        : hov ? QColor(0x88, 0x88, 0x88, 100)
                              : QColor(0x88, 0x88, 0x88, 50));
        } else {
            p->fillRect(r, sel ? QColor(0x44, 0xAA, 0xFF, 150)
                        : hov ? QColor(0x44, 0x88, 0xBB, 100)
                              : QColor(0x44, 0x88, 0xBB, 70));
        }
        p->setBrush(Qt::NoBrush);
        p->setPen(sel ? QPen(Qt::white, 0.8)
                      : QPen(QColor(0x88, 0xCC, 0xFF, 180), 0.4));
        p->drawRect(r);

        // Live preview inside element box
        p->setFont(elemFont);
        const bool isGauge = (d.lengthKey != nullptr); // idx 1,4
        const bool isWpnIcon  = (i == 3);
        const bool isBmbIcon  = (i == 10);
        const bool isRadar    = (i == 11);

        if (isGauge && !hidden) {
            // Gauge: fill 50% with gauge color
            QColor gc(255, 255, 255);
            if (d.colorRKey)
                gc = QColor(cfg.GetInt(d.colorRKey), cfg.GetInt(d.colorGKey), cfg.GetInt(d.colorBKey));
            gc.setAlpha(180);
            const int ori = (d.orientKey != nullptr) ? cfg.GetInt(d.orientKey) : 0;
            QRectF fillR = r;
            if (ori == 1)
                fillR.setHeight(r.height() * 0.5);
            else
                fillR.setWidth(r.width() * 0.5);
            p->fillRect(fillR, gc);
            // Divider line at 50%
            p->setPen(QPen(Qt::white, 0.3));
            if (ori == 1)
                p->drawLine(QPointF(r.left(), fillR.bottom()), QPointF(r.right(), fillR.bottom()));
            else
                p->drawLine(QPointF(fillR.right(), r.top()), QPointF(fillR.right(), r.bottom()));
        } else if (isWpnIcon && !hidden) {
            // Weapon icon: draw cached icon
            EnsureIconsLoaded();
            const QImage& icon = s_weaponIcons[0]; // power beam
            if (!icon.isNull()) {
                p->drawImage(r, icon);
            } else {
                p->setPen(Qt::white);
                p->drawText(r, Qt::AlignCenter, QStringLiteral("WPN"));
            }
        } else if (isBmbIcon && !hidden) {
            // Bomb icon: draw cached icon
            EnsureBombIconsLoaded();
            const QImage& icon = s_bombIcons[3]; // 3 bombs
            if (!icon.isNull()) {
                p->drawImage(r, icon);
            } else {
                p->setPen(Qt::white);
                p->drawText(r, Qt::AlignCenter, QStringLiteral("BMB"));
            }
        } else if (isRadar && !hidden) {
            // Radar: circle outline
            p->setPen(QPen(QColor(0x66, 0xDD, 0x66, 200), 0.5));
            p->setBrush(QColor(0x22, 0x88, 0x22, 80));
            float sz = std::min(static_cast<float>(r.width()), static_cast<float>(r.height()));
            QRectF circR(r.center().x() - sz * 0.5, r.center().y() - sz * 0.5, sz, sz);
            p->drawEllipse(circR);
            p->setBrush(Qt::NoBrush);
        } else {
            // Text elements: draw sample text with element color
            QColor tc(255, 255, 255);
            if (d.colorRKey)
                tc = QColor(cfg.GetInt(d.colorRKey), cfg.GetInt(d.colorGKey), cfg.GetInt(d.colorBKey));
            p->setPen(tc);

            const char* sampleText = nullptr;
            switch (i) {
            case 0:  sampleText = "100";     break; // HP
            case 2:  sampleText = "PWR 50";  break; // Weapon/Ammo
            case 5:  sampleText = "1st | 5"; break; // Match Status
            case 6:  sampleText = "#1";      break; // Rank
            case 7:  sampleText = "2:30";    break; // Time Left
            case 8:  sampleText = "5:00";    break; // Time Limit
            case 9:  sampleText = "x3";      break; // Bomb Left
            default: sampleText = d.name;    break; // Fallback
            }

            if (r.height() > r.width() * 1.3) {
                p->save();
                p->translate(r.center());
                p->rotate(-90);
                QRectF textRect(-r.height() / 2.0, -r.width() / 2.0, r.height(), r.width());
                p->drawText(textRect, Qt::AlignCenter, QString::fromUtf8(sampleText));
                p->restore();
            } else {
                p->drawText(r, Qt::AlignCenter, QString::fromUtf8(sampleText));
            }
        }
        p->setFont(normalFont);
    }

    // Selected element extras
    if (s_editSelected >= 0 && s_editSelected < kEditElemCount) {
        const HudEditElemDesc& d = kEditElems[s_editSelected];
        const QRectF& selRect    = s_editRects[s_editSelected];

        // Orientation toggle (gauges only)
        if (d.orientKey != nullptr) {
            QRectF orientRect = GetOrientToggleRect(s_editSelected);
            p->fillRect(orientRect, QColor(80, 50, 120, 210));
            p->setPen(QPen(QColor(200, 170, 255), 0.7));
            p->drawRect(orientRect);
            // L-shaped bidirectional corner arrow (show/hide H↔V toggle concept)
            float lx = static_cast<float>(orientRect.left())  + 1.5f;
            float rx = static_cast<float>(orientRect.right()) - 2.0f;
            float ty = static_cast<float>(orientRect.top())   + 1.5f;
            float by = static_cast<float>(orientRect.bottom())- 2.0f;
            QPen ap(QColor(220, 190, 255), 0.8f);
            ap.setCapStyle(Qt::RoundCap);
            p->setPen(ap);
            p->drawLine(QPointF(lx, by), QPointF(rx, by));  // horizontal arm
            p->drawLine(QPointF(rx, by), QPointF(rx, ty));  // vertical arm (corner up)
            p->drawLine(QPointF(lx, by), QPointF(lx + 1.8f, by - 1.5f)); // arrowhead left
            p->drawLine(QPointF(lx, by), QPointF(lx + 1.8f, by + 1.5f));
            p->drawLine(QPointF(rx, ty), QPointF(rx - 1.5f, ty + 1.8f)); // arrowhead top
            p->drawLine(QPointF(rx, ty), QPointF(rx + 1.5f, ty + 1.8f));
        }

        // Resize handles
        if (d.lengthKey != nullptr) {
            QRectF lenH, widH;
            GetResizeHandles(s_editSelected, cfg, lenH, widH);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(255, 0, 200));
            p->drawRect(lenH);
            if (!widH.isEmpty())
                p->drawRect(widH);
            p->setBrush(Qt::NoBrush);
        }

        // Unified properties panel (includes built-in Show/Color rows + element props)
        const int builtinRows = CountBuiltinRows(d);
        const int totalRows = builtinRows + d.propCount;
        if (totalRows > 0) {
            QRectF panelRect = ComputePropsPanelRect(selRect, totalRows);
            p->fillRect(panelRect, QColor(15, 15, 35, 230));
            p->setPen(QPen(QColor(80, 80, 140), 0.4));
            p->drawRect(panelRect);

            int visCount = std::min(totalRows, kPropMaxVisible);
            int scrollOfs = std::min(s_editPropScroll, std::max(0, totalRows - kPropMaxVisible));
            for (int vi = 0; vi < visCount; ++vi) {
                int rowIdx = vi + scrollOfs; // unified row index (0-based)
                if (rowIdx >= totalRows) break;
                float rowY = static_cast<float>(panelRect.top()) + 2.0f + vi * kPropRowH;
                float rowX = static_cast<float>(panelRect.left()) + 2.0f;
                float ctrlX = rowX + kPropLabelW;
                float ctrlW = kPropCtrlW;

                // Determine if this is a built-in row or a prop row
                int builtinIdx = rowIdx; // which built-in row (0=Show, 1=Color, 2=Anchor, adjusted)
                bool isShowRow = false, isColorRow = false, isAnchorRow = false;
                if (d.showKey && builtinIdx == 0) {
                    isShowRow = true;
                } else {
                    if (d.showKey) --builtinIdx;
                    if (d.colorRKey && builtinIdx == 0) {
                        isColorRow = true;
                    } else {
                        if (d.colorRKey) --builtinIdx;
                        if (builtinIdx == 0 && rowIdx < builtinRows) {
                            isAnchorRow = true;
                        }
                    }
                }

                if (isShowRow) {
                    // Show row
                    p->setFont(smallFont);
                    p->setPen(QColor(160, 160, 200));
                    p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                                 Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Show"));
                    const bool visible = cfg.GetBool(d.showKey);
                    QRectF btnR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                    p->fillRect(btnR, visible ? QColor(30, 120, 30, 200) : QColor(120, 30, 30, 200));
                    p->setPen(Qt::white);
                    p->drawText(btnR, Qt::AlignCenter, visible ? QStringLiteral("ON") : QStringLiteral("OFF"));
                } else if (isColorRow) {
                    // Color row
                    p->setFont(smallFont);
                    p->setPen(QColor(160, 160, 200));
                    p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                                 Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Color"));
                    QColor curColor(cfg.GetInt(d.colorRKey), cfg.GetInt(d.colorGKey), cfg.GetInt(d.colorBKey));
                    QRectF swR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                    p->fillRect(swR, curColor);
                    p->setPen(QPen(Qt::white, 0.3));
                    p->drawRect(swR);
                } else if (isAnchorRow) {
                    // Anchor row: show current value + "Chng" toggle button
                    static const char* const kAnchorArrow[9] = {
                        "\xe2\x86\x96", "\xe2\x86\x91", "\xe2\x86\x97", // ↖ ↑ ↗
                        "\xe2\x86\x90", "\xc2\xb7",      "\xe2\x86\x92", // ← · →
                        "\xe2\x86\x99", "\xe2\x86\x93", "\xe2\x86\x98", // ↙ ↓ ↘
                    };
                    int anchor = cfg.GetInt(d.anchorKey);
                    float halfW = ctrlW / 2.0f;
                    QRectF valR(ctrlX, rowY + 1.0f, halfW - 1.0f, kPropRowH - 2.0f);
                    QRectF btnR(ctrlX + halfW, rowY + 1.0f, halfW, kPropRowH - 2.0f);
                    p->setFont(smallFont);
                    p->setPen(QColor(160, 160, 200));
                    p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                                Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Anchor"));
                    p->fillRect(valR, QColor(30, 30, 60, 180));
                    p->fillRect(btnR, s_anchorPickerOpen ? QColor(60, 100, 40, 230) : QColor(60, 60, 100, 200));
                    p->setFont(normalFont);
                    p->setPen(Qt::white);
                    p->drawText(valR, Qt::AlignCenter,
                        QString::fromUtf8(anchor >= 0 && anchor < 9 ? kAnchorArrow[anchor] : "?"));
                    p->setFont(smallFont);
                    p->drawText(btnR, Qt::AlignCenter, QStringLiteral("Chng"));
                } else {
                    // Regular property row
                    int propIdx = rowIdx - builtinRows;
                    if (propIdx < 0 || propIdx >= d.propCount) continue;
                    const HudEditPropDesc& pr = d.props[propIdx];

                    // Label
                    p->setFont(smallFont);
                    p->setPen(QColor(160, 160, 200));
                    p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                                 Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(pr.label));

                    switch (pr.type) {
                    case EditPropType::Bool: {
                        bool val = cfg.GetBool(pr.cfgKey);
                        QRectF btnR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                        p->fillRect(btnR, val ? QColor(30, 120, 30, 200) : QColor(120, 30, 30, 200));
                        p->setPen(Qt::white);
                        p->drawText(btnR, Qt::AlignCenter, val ? QStringLiteral("ON") : QStringLiteral("OFF"));
                        break;
                    }
                    case EditPropType::Int: {
                        int val = cfg.GetInt(pr.cfgKey);
                        float arrowW = 8.0f;
                        QRectF leftArr(ctrlX, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                        QRectF rightArr(ctrlX + ctrlW - arrowW, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                        QRectF valR(ctrlX + arrowW, rowY + 1.0f, ctrlW - 2.0f * arrowW, kPropRowH - 2.0f);
                        p->fillRect(leftArr, QColor(60, 60, 100, 200));
                        p->fillRect(rightArr, QColor(60, 60, 100, 200));
                        p->fillRect(valR, QColor(30, 30, 60, 180));
                        p->setPen(Qt::white);
                        p->drawText(leftArr, Qt::AlignCenter, QStringLiteral("\u25C0"));
                        p->drawText(rightArr, Qt::AlignCenter, QStringLiteral("\u25B6"));
                        p->drawText(valR, Qt::AlignCenter, QString::number(val));
                        break;
                    }
                    case EditPropType::Float: {
                        double val = cfg.GetDouble(pr.cfgKey);
                        float arrowW = 8.0f;
                        QRectF leftArr(ctrlX, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                        QRectF rightArr(ctrlX + ctrlW - arrowW, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                        QRectF valR(ctrlX + arrowW, rowY + 1.0f, ctrlW - 2.0f * arrowW, kPropRowH - 2.0f);
                        p->fillRect(leftArr, QColor(60, 60, 100, 200));
                        p->fillRect(rightArr, QColor(60, 60, 100, 200));
                        p->fillRect(valR, QColor(30, 30, 60, 180));
                        p->setPen(Qt::white);
                        p->drawText(leftArr, Qt::AlignCenter, QStringLiteral("\u25C0"));
                        p->drawText(rightArr, Qt::AlignCenter, QStringLiteral("\u25B6"));
                        p->drawText(valR, Qt::AlignCenter, QString::number(val, 'f', 2));
                        break;
                    }
                    case EditPropType::String: {
                        std::string val = cfg.GetString(pr.cfgKey);
                        QRectF btnR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                        p->fillRect(btnR, QColor(40, 40, 70, 200));
                        p->setPen(QColor(200, 200, 255));
                        QString display = QString::fromStdString(val);
                        if (display.length() > 6) display = display.left(5) + QStringLiteral("\u2026");
                        p->drawText(btnR, Qt::AlignCenter, display.isEmpty() ? QStringLiteral("...") : display);
                        break;
                    }
                    case EditPropType::SubColor: {
                        bool overall = cfg.GetBool(pr.cfgKey);
                        float ovrW = 14.0f;
                        QRectF ovrR(ctrlX, rowY + 1.0f, ovrW, kPropRowH - 2.0f);
                        p->fillRect(ovrR, overall ? QColor(80, 80, 40, 200) : QColor(40, 40, 60, 200));
                        p->setPen(overall ? QColor(255, 255, 150) : QColor(120, 120, 160));
                        p->drawText(ovrR, Qt::AlignCenter, QStringLiteral("OVR"));
                        QColor clr(cfg.GetInt(pr.extra1), cfg.GetInt(pr.extra2), cfg.GetInt(pr.extra3));
                        QRectF swR(ctrlX + ovrW + 2.0f, rowY + 1.0f, ctrlW - ovrW - 2.0f, kPropRowH - 2.0f);
                        p->fillRect(swR, overall ? QColor(80, 80, 80) : clr);
                        p->setPen(QPen(Qt::white, 0.3));
                        p->drawRect(swR);
                        break;
                    }
                    case EditPropType::Color: break; // handled above as built-in
                    }
                }
            }
            // Scroll indicator
            if (totalRows > kPropMaxVisible) {
                int scrollOfsI = std::min(s_editPropScroll, std::max(0, totalRows - kPropMaxVisible));
                float rowAreaH = visCount * kPropRowH;  // only the row portion, not the anchor grid
                float indH = rowAreaH * kPropMaxVisible / totalRows;
                float indY = static_cast<float>(panelRect.top()) + 2.0f +
                    (rowAreaH - indH) * scrollOfsI / (totalRows - kPropMaxVisible);
                QRectF indR(panelRect.right() - 2.0f, indY, 1.5f, indH);
                p->fillRect(indR, QColor(120, 120, 180, 150));
            }
            // Anchor picker 3×3 grid (shown when s_anchorPickerOpen)
            if (s_anchorPickerOpen) {
                static const char* const kAnchorArrowGrid[9] = {
                    "\xe2\x86\x96", "\xe2\x86\x91", "\xe2\x86\x97",
                    "\xe2\x86\x90", "\xc2\xb7",      "\xe2\x86\x92",
                    "\xe2\x86\x99", "\xe2\x86\x93", "\xe2\x86\x98",
                };
                int curAnchor = cfg.GetInt(d.anchorKey);
                float gridLeft = static_cast<float>(panelRect.left()) + 2.0f;
                float gridTop  = static_cast<float>(panelRect.top()) + 2.0f + visCount * kPropRowH + 2.0f;
                float cellW = (kPropPanelW - 4.0f) / 3.0f;
                float cellH = kAnchorGridCellH;
                p->setFont(normalFont);
                for (int a = 0; a < 9; ++a) {
                    int col = a % 3, row = a / 3;
                    QRectF cell(gridLeft + col * cellW, gridTop + row * cellH, cellW - 1.0f, cellH - 1.0f);
                    bool sel = (a == curAnchor);
                    p->fillRect(cell, sel ? QColor(60, 160, 60, 230) : QColor(40, 40, 90, 210));
                    p->setPen(sel ? QPen(Qt::yellow, 0.6) : QPen(QColor(80, 80, 140), 0.3));
                    p->drawRect(cell);
                    p->setPen(sel ? QColor(255, 255, 120) : Qt::white);
                    p->drawText(cell, Qt::AlignCenter, QString::fromUtf8(kAnchorArrowGrid[a]));
                }
            }
            p->setFont(normalFont);
        }
    }

    // Save / Cancel / Reset buttons
    p->setFont(normalFont);
    p->fillRect(kEditSaveRect,   QColor(30, 100, 30, 220));
    p->fillRect(kEditCancelRect, QColor(100, 30, 30, 220));
    p->fillRect(kEditResetRect,  QColor(30,  30, 100, 220));
    p->setPen(QPen(QColor(150, 220, 150), 0.4));
    p->drawRect(kEditSaveRect);
    p->setPen(QPen(QColor(220, 150, 150), 0.4));
    p->drawRect(kEditCancelRect);
    p->setPen(QPen(QColor(150, 150, 220), 0.4));
    p->drawRect(kEditResetRect);
    p->setPen(Qt::white);
    p->drawText(kEditSaveRect,   Qt::AlignCenter, QStringLiteral("\u2713 Save"));
    p->drawText(kEditCancelRect, Qt::AlignCenter, QStringLiteral("\u2717 Cancel"));
    p->drawText(kEditResetRect,  Qt::AlignCenter, QStringLiteral("\u21ba Reset"));

    // ── Text Scale control ──────────────────────────────────────────────
    {
        int txSc = cfg.GetInt("Metroid.Visual.HudTextScale");
        p->fillRect(kEditTextScaleRect, QColor(40, 40, 80, 220));
        p->setPen(QPen(QColor(140, 140, 200), 0.4));
        p->drawRect(kEditTextScaleRect);
        p->setFont(smallFont);
        p->setPen(QColor(160, 160, 200));
        float tsX = static_cast<float>(kEditTextScaleRect.left()) + 2.0f;
        float tsY = static_cast<float>(kEditTextScaleRect.top());
        float tsW = static_cast<float>(kEditTextScaleRect.width());
        float tsH = static_cast<float>(kEditTextScaleRect.height());
        p->drawText(QRectF(tsX, tsY, 26.0f, tsH), Qt::AlignLeft | Qt::AlignVCenter,
                    QStringLiteral("Scale"));
        // ◀ value ▶
        float ctrlX = tsX + 26.0f;
        float ctrlW = tsW - 28.0f;
        float btnW = 8.0f;
        QRectF decR(ctrlX, tsY + 1.0f, btnW, tsH - 2.0f);
        QRectF valR(ctrlX + btnW, tsY + 1.0f, ctrlW - btnW * 2, tsH - 2.0f);
        QRectF incR(ctrlX + ctrlW - btnW, tsY + 1.0f, btnW, tsH - 2.0f);
        p->fillRect(decR, QColor(60, 60, 100, 200));
        p->fillRect(valR, QColor(30, 30, 60, 180));
        p->fillRect(incR, QColor(60, 60, 100, 200));
        p->setPen(Qt::white);
        p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
        p->drawText(valR, Qt::AlignCenter, QString::number(txSc) + QStringLiteral("%"));
        p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
    }

    // ── Crosshair toggle button ─────────────────────────────────────────
    {
        p->fillRect(kEditCrosshairBtnRect,
                    s_crosshairPanelOpen ? QColor(60, 100, 40, 230) : QColor(40, 40, 80, 220));
        p->setPen(QPen(QColor(140, 200, 140), 0.4));
        p->drawRect(kEditCrosshairBtnRect);
        p->setFont(normalFont);
        p->setPen(Qt::white);
        p->drawText(kEditCrosshairBtnRect, Qt::AlignCenter,
                    s_crosshairPanelOpen ? QStringLiteral("Crosshair \u25bc")
                                         : QStringLiteral("Crosshair \u25b6"));
    }

    // ── Crosshair panel (when open) ─────────────────────────────────────
    if (s_crosshairPanelOpen) {
        const int totalRows = CountCrosshairRows(); // always 10
        const int visCount  = totalRows;            // no scrolling needed
        const float panelH  = visCount * kPropRowH + 4.0f;
        const QRectF panelRect(kCrosshairPanelX, kCrosshairPanelY, kPropPanelW, panelH);

        p->fillRect(panelRect, QColor(20, 20, 40, 230));
        p->setPen(QPen(QColor(80, 80, 160), 0.5));
        p->drawRect(panelRect);

        const float rowX  = kCrosshairPanelX + 2.0f;
        const float ctrlX = rowX + kPropLabelW;
        const float ctrlW = kPropCtrlW;

        p->setFont(smallFont);

        for (int vi = 0; vi < visCount; ++vi) {
            const int rowIdx = vi;
            const float rowY = kCrosshairPanelY + 2.0f + vi * kPropRowH;
            bool isColorRow, isInnerHdr, isOuterHdr;
            const HudEditPropDesc* pr = GetCrosshairPropForRow(rowIdx, isColorRow, isInnerHdr, isOuterHdr);

            if (isColorRow) {
                int r = cfg.GetInt("Metroid.Visual.CrosshairColorR");
                int g = cfg.GetInt("Metroid.Visual.CrosshairColorG");
                int b = cfg.GetInt("Metroid.Visual.CrosshairColorB");
                p->setPen(QColor(160, 160, 200));
                p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                            Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Color"));
                QRectF swR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                p->fillRect(swR, QColor(r, g, b));
                p->setPen(QPen(Qt::white, 0.3));
                p->drawRect(swR);
            } else if (isInnerHdr) {
                // Header that opens inner side panel to the right
                QRectF hdrR(rowX, rowY, kPropLabelW + ctrlW, kPropRowH);
                p->fillRect(hdrR, s_innerSectionOpen ? QColor(60, 100, 40, 200)
                                                     : QColor(50, 50, 100, 200));
                p->setPen(Qt::white);
                p->drawText(hdrR, Qt::AlignCenter,
                    s_innerSectionOpen ? QStringLiteral("Inner \u25c4")
                                       : QStringLiteral("Inner \u25ba"));
            } else if (isOuterHdr) {
                // Header that opens outer side panel to the right
                QRectF hdrR(rowX, rowY, kPropLabelW + ctrlW, kPropRowH);
                p->fillRect(hdrR, s_outerSectionOpen ? QColor(60, 100, 40, 200)
                                                     : QColor(50, 50, 100, 200));
                p->setPen(Qt::white);
                p->drawText(hdrR, Qt::AlignCenter,
                    s_outerSectionOpen ? QStringLiteral("Outer \u25c4")
                                       : QStringLiteral("Outer \u25ba"));
            } else if (pr) {
                p->setPen(QColor(160, 160, 200));
                p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                            Qt::AlignLeft | Qt::AlignVCenter,
                            QString::fromLatin1(pr->label));
                switch (pr->type) {
                case EditPropType::Bool: {
                    bool val = cfg.GetBool(pr->cfgKey);
                    QRectF swR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                    p->fillRect(swR, val ? QColor(40, 120, 40, 200) : QColor(80, 30, 30, 200));
                    p->setPen(Qt::white);
                    p->drawText(swR, Qt::AlignCenter, val ? QStringLiteral("ON") : QStringLiteral("OFF"));
                    break;
                }
                case EditPropType::Int: {
                    int val = cfg.GetInt(pr->cfgKey);
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF valR2(ctrlX + btnW2, rowY + 1.0f, ctrlW - btnW2 * 2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    p->fillRect(decR, QColor(60, 60, 100, 200));
                    p->fillRect(valR2, QColor(30, 30, 60, 180));
                    p->fillRect(incR, QColor(60, 60, 100, 200));
                    p->setPen(Qt::white);
                    p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                    p->drawText(valR2, Qt::AlignCenter, QString::number(val));
                    p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                    break;
                }
                case EditPropType::Float: {
                    double val = cfg.GetDouble(pr->cfgKey);
                    int pct = static_cast<int>(val * 100.0 + 0.5);
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF valR2(ctrlX + btnW2, rowY + 1.0f, ctrlW - btnW2 * 2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    p->fillRect(decR, QColor(60, 60, 100, 200));
                    p->fillRect(valR2, QColor(30, 30, 60, 180));
                    p->fillRect(incR, QColor(60, 60, 100, 200));
                    p->setPen(Qt::white);
                    p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                    p->drawText(valR2, Qt::AlignCenter, QString::number(pct) + QStringLiteral("%"));
                    p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                    break;
                }
                default: break;
                }
            }
        }

        // ── Inner / Outer side panel ──────────────────────────────────
        {
            const HudEditPropDesc* sideProps = nullptr;
            int sidePropCount = 0;
            if (s_innerSectionOpen) {
                sideProps = kPropsCrosshairInner; sidePropCount = kCrosshairInnerCount;
            } else if (s_outerSectionOpen) {
                sideProps = kPropsCrosshairOuter; sidePropCount = kCrosshairOuterCount;
            }
            if (sideProps) {
                const float sidePanelH = sidePropCount * kPropRowH + 4.0f;
                const QRectF sideRect(kCrosshairSidePanelX, kCrosshairPanelY, kPropPanelW, sidePanelH);
                p->fillRect(sideRect, QColor(20, 30, 50, 230));
                p->setPen(QPen(QColor(80, 120, 160), 0.5));
                p->drawRect(sideRect);

                const float sRowX  = kCrosshairSidePanelX + 2.0f;
                const float sCtrlX = sRowX + kPropLabelW;

                for (int i = 0; i < sidePropCount; ++i) {
                    const HudEditPropDesc& pr2 = sideProps[i];
                    const float rowY = kCrosshairPanelY + 2.0f + i * kPropRowH;
                    p->setPen(QColor(160, 160, 200));
                    p->drawText(QRectF(sRowX, rowY, kPropLabelW, kPropRowH),
                                Qt::AlignLeft | Qt::AlignVCenter,
                                QString::fromLatin1(pr2.label));
                    switch (pr2.type) {
                    case EditPropType::Bool: {
                        bool val = cfg.GetBool(pr2.cfgKey);
                        QRectF swR(sCtrlX, rowY + 1.0f, kPropCtrlW, kPropRowH - 2.0f);
                        p->fillRect(swR, val ? QColor(40, 120, 40, 200) : QColor(80, 30, 30, 200));
                        p->setPen(Qt::white);
                        p->drawText(swR, Qt::AlignCenter, val ? QStringLiteral("ON") : QStringLiteral("OFF"));
                        break;
                    }
                    case EditPropType::Int: {
                        int val = cfg.GetInt(pr2.cfgKey);
                        float btnW2 = 8.0f;
                        QRectF decR(sCtrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                        QRectF valR2(sCtrlX + btnW2, rowY + 1.0f, kPropCtrlW - btnW2 * 2, kPropRowH - 2.0f);
                        QRectF incR(sCtrlX + kPropCtrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                        p->fillRect(decR, QColor(60, 60, 100, 200));
                        p->fillRect(valR2, QColor(30, 30, 60, 180));
                        p->fillRect(incR, QColor(60, 60, 100, 200));
                        p->setPen(Qt::white);
                        p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                        p->drawText(valR2, Qt::AlignCenter, QString::number(val));
                        p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                        break;
                    }
                    case EditPropType::Float: {
                        double val = cfg.GetDouble(pr2.cfgKey);
                        int pct = static_cast<int>(val * 100.0 + 0.5);
                        float btnW2 = 8.0f;
                        QRectF decR(sCtrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                        QRectF valR2(sCtrlX + btnW2, rowY + 1.0f, kPropCtrlW - btnW2 * 2, kPropRowH - 2.0f);
                        QRectF incR(sCtrlX + kPropCtrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                        p->fillRect(decR, QColor(60, 60, 100, 200));
                        p->fillRect(valR2, QColor(30, 30, 60, 180));
                        p->fillRect(incR, QColor(60, 60, 100, 200));
                        p->setPen(Qt::white);
                        p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                        p->drawText(valR2, Qt::AlignCenter, QString::number(pct) + QStringLiteral("%"));
                        p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                        break;
                    }
                    default: break;
                    }
                }
            }
        }

        // ── Crosshair preview ─────────────────────────────────────────
        {
            const float pvX = kCrosshairPreviewX;
            const float pvY = kCrosshairPanelY;
            const float pvS = static_cast<float>(kCrosshairPreviewSize);
            const int   pvCX = static_cast<int>(pvX + pvS * 0.5f);
            const int   pvCY = static_cast<int>(pvY + pvS * 0.5f);

            p->fillRect(QRectF(pvX, pvY, pvS, pvS), QColor(10, 10, 25, 210));
            p->setPen(QPen(QColor(80, 80, 120), 0.5));
            p->drawRect(QRectF(pvX, pvY, pvS, pvS));

            const int cr  = cfg.GetInt("Metroid.Visual.CrosshairColorR");
            const int cg  = cfg.GetInt("Metroid.Visual.CrosshairColorG");
            const int cb2 = cfg.GetInt("Metroid.Visual.CrosshairColorB");
            const bool tStyle    = cfg.GetBool("Metroid.Visual.CrosshairTStyle");
            const bool innerShow = cfg.GetBool("Metroid.Visual.CrosshairInnerShow");
            const bool outerShow = cfg.GetBool("Metroid.Visual.CrosshairOuterShow");
            const bool centerDot = cfg.GetBool("Metroid.Visual.CrosshairCenterDot");
            constexpr int PVS = 2; // preview scale: each DS-unit drawn as 2 units

            p->setClipRect(QRectF(pvX + 1.0f, pvY + 1.0f, pvS - 2.0f, pvS - 2.0f));
            if (outerShow) {
                const int oLenX   = cfg.GetInt("Metroid.Visual.CrosshairOuterLengthX");
                const int oLenY   = cfg.GetInt("Metroid.Visual.CrosshairOuterLengthY");
                const int oThick  = cfg.GetInt("Metroid.Visual.CrosshairOuterThickness");
                const int oOffset = cfg.GetInt("Metroid.Visual.CrosshairOuterOffset");
                const float oOpac = static_cast<float>(cfg.GetDouble("Metroid.Visual.CrosshairOuterOpacity"));
                QRect outerRects[4]; int nOuter = 0;
                CollectArmRects(outerRects, nOuter, pvCX, pvCY,
                                oLenX * PVS, oLenY * PVS, oOffset * PVS,
                                std::max(1, oThick * PVS), tStyle);
                QColor outerColor(cr, cg, cb2, static_cast<int>(oOpac * 255.0f));
                for (int i = 0; i < nOuter; ++i) p->fillRect(outerRects[i], outerColor);
            }
            if (innerShow) {
                const int iLenX   = cfg.GetInt("Metroid.Visual.CrosshairInnerLengthX");
                const int iLenY   = cfg.GetInt("Metroid.Visual.CrosshairInnerLengthY");
                const int iThick  = cfg.GetInt("Metroid.Visual.CrosshairInnerThickness");
                const int iOffset = cfg.GetInt("Metroid.Visual.CrosshairInnerOffset");
                const float iOpac = static_cast<float>(cfg.GetDouble("Metroid.Visual.CrosshairInnerOpacity"));
                QRect innerRects[4]; int nInner = 0;
                CollectArmRects(innerRects, nInner, pvCX, pvCY,
                                iLenX * PVS, iLenY * PVS, iOffset * PVS,
                                std::max(1, iThick * PVS), tStyle);
                QColor innerColor(cr, cg, cb2, static_cast<int>(iOpac * 255.0f));
                for (int i = 0; i < nInner; ++i) p->fillRect(innerRects[i], innerColor);
            }
            if (centerDot) {
                const int dotThick = cfg.GetInt("Metroid.Visual.CrosshairDotThickness");
                const float dotOpac = static_cast<float>(cfg.GetDouble("Metroid.Visual.CrosshairDotOpacity"));
                const int dHalf = std::max(1, dotThick * PVS) / 2;
                QColor dotColor(cr, cg, cb2, static_cast<int>(dotOpac * 255.0f));
                p->fillRect(QRect(pvCX - dHalf, pvCY - dHalf, dHalf * 2 + 1, dHalf * 2 + 1), dotColor);
            }
            p->setClipping(false);

            p->setFont(smallFont);
            p->setPen(QColor(100, 100, 140));
            p->drawText(QRectF(pvX, pvY + pvS - kPropRowH, pvS, kPropRowH),
                        Qt::AlignCenter, QStringLiteral("preview"));
        }

        p->setFont(normalFont);
    }
}

// ── Public API ──────────────────────────────────────────────────────────────

void CustomHud_EnterEditMode(EmuInstance* emu, Config::Table& cfg)
{
    s_editEmu        = emu;
    s_editSelected   = -1;
    s_editHovered    = -1;
    s_dragging       = false;
    s_resizingLength = false;
    s_resizingWidth  = false;
    SnapshotEditConfig(cfg);
    s_editMode = true;
    CustomHud_InvalidateConfigCache();
    NotifySelectionChanged(-1);

    if (auto* core = emu->getEmuThread()->GetMelonPrimeCore())
        core->isCursorMode = true;
}

void CustomHud_ExitEditMode(bool save, Config::Table& cfg)
{
    if (!save) {
        RestoreEditSnapshot(cfg);
    }
    CustomHud_InvalidateConfigCache();
    if (save) Config::Save();

    s_editMode       = false;
    s_editSelected   = -1;
    s_editHovered    = -1;
    s_dragging       = false;
    s_anchorPickerOpen = false;
    s_crosshairPanelOpen = false;
    s_innerSectionOpen   = false;
    s_outerSectionOpen   = false;
    s_crosshairPanelScroll = 0;
    s_resizingLength = false;
    s_resizingWidth  = false;
    s_editEmu        = nullptr;
    NotifySelectionChanged(-1);
    // Caller (Screen.cpp) is responsible for re-showing the settings dialog.
}

bool CustomHud_IsEditMode()
{
    return s_editMode;
}

void CustomHud_SetEditSelectionCallback(std::function<void(int)> cb)
{
    s_editSelectionCb = std::move(cb);
}

int CustomHud_GetSelectedElement()
{
    return s_editSelected;
}

void CustomHud_UpdateEditContext(float originX, float originY,
                                  float hudScale, float topStretchX)
{
    s_editOriginX     = originX;
    s_editOriginY     = originY;
    s_editHudScale    = hudScale;
    s_editTopStretchX = topStretchX;
}

void CustomHud_EditMousePress(QPointF pt, Qt::MouseButton btn, Config::Table& cfg)
{
    if (btn != Qt::LeftButton) return;
    const QPointF ds = WidgetToDS(pt);

    // Priority 1: Save / Cancel / Reset
    if (kEditSaveRect.contains(ds)) {
        CustomHud_ExitEditMode(true, cfg);
        return;
    }
    if (kEditCancelRect.contains(ds)) {
        CustomHud_ExitEditMode(false, cfg);
        return;
    }
    if (kEditResetRect.contains(ds)) {
        ResetEditToDefaults(cfg);
        s_editSelected = -1;
        CustomHud_InvalidateConfigCache();
        NotifySelectionChanged(-1);
        return;
    }

    // Priority 1b: Text Scale ◀/▶
    if (kEditTextScaleRect.contains(ds)) {
        float tsX = static_cast<float>(kEditTextScaleRect.left()) + 2.0f + 26.0f;
        float tsW = static_cast<float>(kEditTextScaleRect.width()) - 28.0f;
        float btnW = 8.0f;
        float tsY = static_cast<float>(kEditTextScaleRect.top());
        float tsH = static_cast<float>(kEditTextScaleRect.height());
        QRectF decR(tsX, tsY + 1.0f, btnW, tsH - 2.0f);
        QRectF incR(tsX + tsW - btnW, tsY + 1.0f, btnW, tsH - 2.0f);
        int val = cfg.GetInt("Metroid.Visual.HudTextScale");
        if (decR.contains(ds)) {
            cfg.SetInt("Metroid.Visual.HudTextScale", std::max(50, val - 10));
            CustomHud_InvalidateConfigCache();
        } else if (incR.contains(ds)) {
            cfg.SetInt("Metroid.Visual.HudTextScale", std::min(300, val + 10));
            CustomHud_InvalidateConfigCache();
        }
        return;
    }

    // Priority 1c: Crosshair panel toggle
    if (kEditCrosshairBtnRect.contains(ds)) {
        s_crosshairPanelOpen = !s_crosshairPanelOpen;
        s_crosshairPanelScroll = 0;
        return;
    }

    // Priority 1d: Crosshair panel clicks (when open)
    if (s_crosshairPanelOpen) {
        const int totalRows = CountCrosshairRows();
        const int visCount  = std::min(totalRows, kCrosshairMaxVisible);
        const float panelH  = visCount * kPropRowH + 4.0f;
        const QRectF panelRect(kCrosshairPanelX, kCrosshairPanelY, kPropPanelW, panelH);

        if (panelRect.contains(ds)) {
            const float rowX  = kCrosshairPanelX + 2.0f;
            const float ctrlX = rowX + kPropLabelW;
            const float ctrlW = kPropCtrlW;
            const int scrollOfs = std::min(s_crosshairPanelScroll,
                                           std::max(0, totalRows - kCrosshairMaxVisible));

            for (int vi = 0; vi < visCount; ++vi) {
                const int rowIdx = vi + scrollOfs;
                const float rowY = kCrosshairPanelY + 2.0f + vi * kPropRowH;
                QRectF rowRect(rowX, rowY, kPropLabelW + ctrlW, kPropRowH);
                if (!rowRect.contains(ds)) continue;

                bool isColorRow, isInnerHdr, isOuterHdr;
                const HudEditPropDesc* pr = GetCrosshairPropForRow(rowIdx, isColorRow,
                                                                   isInnerHdr, isOuterHdr);
                if (isColorRow) {
                    if (s_colorDialogOpen) return;
                    s_colorDialogOpen = true;
                    int r = cfg.GetInt("Metroid.Visual.CrosshairColorR");
                    int g = cfg.GetInt("Metroid.Visual.CrosshairColorG");
                    int b = cfg.GetInt("Metroid.Visual.CrosshairColorB");
                    QColor chosen = QColorDialog::getColor(QColor(r, g, b), nullptr,
                        QStringLiteral("Crosshair Color"));
                    s_colorDialogOpen = false;
                    if (chosen.isValid()) {
                        cfg.SetInt("Metroid.Visual.CrosshairColorR", chosen.red());
                        cfg.SetInt("Metroid.Visual.CrosshairColorG", chosen.green());
                        cfg.SetInt("Metroid.Visual.CrosshairColorB", chosen.blue());
                        CustomHud_InvalidateConfigCache();
                    }
                    return;
                }
                if (isInnerHdr) {
                    s_innerSectionOpen = !s_innerSectionOpen;
                    if (s_innerSectionOpen) s_outerSectionOpen = false;
                    return;
                }
                if (isOuterHdr) {
                    s_outerSectionOpen = !s_outerSectionOpen;
                    if (s_outerSectionOpen) s_innerSectionOpen = false;
                    return;
                }
                if (!pr) return;

                switch (pr->type) {
                case EditPropType::Bool:
                    cfg.SetBool(pr->cfgKey, !cfg.GetBool(pr->cfgKey));
                    CustomHud_InvalidateConfigCache();
                    return;
                case EditPropType::Int: {
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    int val = cfg.GetInt(pr->cfgKey);
                    int step = pr->step > 0 ? pr->step : 1;
                    if (decR.contains(ds))
                        cfg.SetInt(pr->cfgKey, std::max(pr->minVal, val - step));
                    else if (incR.contains(ds))
                        cfg.SetInt(pr->cfgKey, std::min(pr->maxVal, val + step));
                    CustomHud_InvalidateConfigCache();
                    return;
                }
                case EditPropType::Float: {
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    int pct = static_cast<int>(cfg.GetDouble(pr->cfgKey) * 100.0 + 0.5);
                    int step = pr->step > 0 ? pr->step : 5;
                    if (decR.contains(ds))
                        cfg.SetDouble(pr->cfgKey, std::max(pr->minVal, pct - step) / 100.0);
                    else if (incR.contains(ds))
                        cfg.SetDouble(pr->cfgKey, std::min(pr->maxVal, pct + step) / 100.0);
                    CustomHud_InvalidateConfigCache();
                    return;
                }
                default: return;
                }
            }
            return; // absorbed by panel
        }

        // Side panel clicks (Inner / Outer props)
        {
            const HudEditPropDesc* sideClickProps = nullptr;
            int sideClickCount = 0;
            if (s_innerSectionOpen) {
                sideClickProps = kPropsCrosshairInner; sideClickCount = kCrosshairInnerCount;
            } else if (s_outerSectionOpen) {
                sideClickProps = kPropsCrosshairOuter; sideClickCount = kCrosshairOuterCount;
            }
            if (sideClickProps) {
                const float sidePanelH = sideClickCount * kPropRowH + 4.0f;
                const QRectF sideRect(kCrosshairSidePanelX, kCrosshairPanelY, kPropPanelW, sidePanelH);
                if (sideRect.contains(ds)) {
                    const float sRowX  = kCrosshairSidePanelX + 2.0f;
                    const float sCtrlX = sRowX + kPropLabelW;
                    for (int i = 0; i < sideClickCount; ++i) {
                        const float rowY = kCrosshairPanelY + 2.0f + i * kPropRowH;
                        QRectF rowRect(sRowX, rowY, kPropLabelW + kPropCtrlW, kPropRowH);
                        if (!rowRect.contains(ds)) continue;
                        const HudEditPropDesc& pr2 = sideClickProps[i];
                        switch (pr2.type) {
                        case EditPropType::Bool:
                            cfg.SetBool(pr2.cfgKey, !cfg.GetBool(pr2.cfgKey));
                            CustomHud_InvalidateConfigCache();
                            return;
                        case EditPropType::Int: {
                            float btnW2 = 8.0f;
                            QRectF decR(sCtrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                            QRectF incR(sCtrlX + kPropCtrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                            int val = cfg.GetInt(pr2.cfgKey);
                            int step = pr2.step > 0 ? pr2.step : 1;
                            if (decR.contains(ds))
                                cfg.SetInt(pr2.cfgKey, std::max(pr2.minVal, val - step));
                            else if (incR.contains(ds))
                                cfg.SetInt(pr2.cfgKey, std::min(pr2.maxVal, val + step));
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        case EditPropType::Float: {
                            float btnW2 = 8.0f;
                            QRectF decR(sCtrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                            QRectF incR(sCtrlX + kPropCtrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                            int pct = static_cast<int>(cfg.GetDouble(pr2.cfgKey) * 100.0 + 0.5);
                            int step = pr2.step > 0 ? pr2.step : 5;
                            if (decR.contains(ds))
                                cfg.SetDouble(pr2.cfgKey, std::max(pr2.minVal, pct - step) / 100.0);
                            else if (incR.contains(ds))
                                cfg.SetDouble(pr2.cfgKey, std::min(pr2.maxVal, pct + step) / 100.0);
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        default: return;
                        }
                    }
                    return; // absorbed by side panel
                }
            }
        }
    }

    // Priority 2–4: only when an element is already selected
    if (s_editSelected >= 0 && s_editSelected < kEditElemCount) {
        const HudEditElemDesc& d = kEditElems[s_editSelected];

        // Priority 2: Unified properties panel (Show/Color/Anchor built-in rows + element props)
        {
            const int builtinRows = CountBuiltinRows(d);
            const int totalRows = builtinRows + d.propCount;
            if (totalRows > 0) {
                QRectF panelRect = ComputePropsPanelRect(s_editRects[s_editSelected], totalRows);
                if (panelRect.contains(ds)) {
                    int visCount = std::min(totalRows, kPropMaxVisible);
                    int scrollOfs = std::min(s_editPropScroll, std::max(0, totalRows - kPropMaxVisible));
                    float panelInnerY = static_cast<float>(panelRect.top()) + 2.0f;
                    float rowX = static_cast<float>(panelRect.left()) + 2.0f;
                    for (int vi = 0; vi < visCount; ++vi) {
                        int rowIdx = vi + scrollOfs;
                        if (rowIdx >= totalRows) break;
                        float rowY = panelInnerY + vi * kPropRowH;
                        float ctrlX = rowX + kPropLabelW;
                        float ctrlW = kPropCtrlW;
                        QRectF rowRect(ctrlX, rowY, ctrlW, kPropRowH);
                        if (!rowRect.contains(ds)) continue;

                        // Determine built-in vs prop row
                        int builtinIdx = rowIdx;
                        bool isShowRow = false, isColorRow = false, isAnchorRow = false;
                        if (d.showKey && builtinIdx == 0) {
                            isShowRow = true;
                        } else {
                            if (d.showKey) --builtinIdx;
                            if (d.colorRKey && builtinIdx == 0) {
                                isColorRow = true;
                            } else {
                                if (d.colorRKey) --builtinIdx;
                                if (builtinIdx == 0 && rowIdx < builtinRows) {
                                    isAnchorRow = true;
                                }
                            }
                        }

                        if (isShowRow) {
                            cfg.SetBool(d.showKey, !cfg.GetBool(d.showKey));
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        if (isColorRow) {
                            if (s_colorDialogOpen) return;
                            QColor cur(cfg.GetInt(d.colorRKey), cfg.GetInt(d.colorGKey), cfg.GetInt(d.colorBKey));
                            s_colorDialogOpen = true;
                            QColor picked = QColorDialog::getColor(cur, nullptr, QStringLiteral("Pick Color"));
                            s_colorDialogOpen = false;
                            if (picked.isValid()) {
                                cfg.SetInt(d.colorRKey, picked.red());
                                cfg.SetInt(d.colorGKey, picked.green());
                                cfg.SetInt(d.colorBKey, picked.blue());
                                CustomHud_InvalidateConfigCache();
                            }
                            return;
                        }
                        if (isAnchorRow) {
                            float halfW = ctrlW / 2.0f;
                            QRectF btnR(ctrlX + halfW, rowRect.top(), halfW, kPropRowH);
                            if (btnR.contains(ds)) {
                                s_anchorPickerOpen = !s_anchorPickerOpen;
                            }
                            return;
                        }

                        // Regular property row
                        int propIdx = rowIdx - builtinRows;
                        if (propIdx < 0 || propIdx >= d.propCount) continue;
                        const HudEditPropDesc& pr = d.props[propIdx];

                        switch (pr.type) {
                        case EditPropType::Bool:
                            cfg.SetBool(pr.cfgKey, !cfg.GetBool(pr.cfgKey));
                            CustomHud_InvalidateConfigCache();
                            return;
                        case EditPropType::Int: {
                            float arrowW = 8.0f;
                            int step = pr.step > 0 ? pr.step : 1;
                            int val = cfg.GetInt(pr.cfgKey);
                            if (ds.x() < ctrlX + arrowW) val = std::max(pr.minVal, val - step);
                            else if (ds.x() > ctrlX + ctrlW - arrowW) val = std::min(pr.maxVal, val + step);
                            cfg.SetInt(pr.cfgKey, val);
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        case EditPropType::Float: {
                            float arrowW = 8.0f;
                            double step = (pr.step > 0 ? pr.step : 5) / 100.0;
                            double val = cfg.GetDouble(pr.cfgKey);
                            double minV = pr.minVal / 100.0, maxV = pr.maxVal / 100.0;
                            if (ds.x() < ctrlX + arrowW) val = std::max(minV, val - step);
                            else if (ds.x() > ctrlX + ctrlW - arrowW) val = std::min(maxV, val + step);
                            cfg.SetDouble(pr.cfgKey, val);
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        case EditPropType::String: {
                            if (s_colorDialogOpen) return;
                            s_colorDialogOpen = true;
                            QString cur = QString::fromStdString(cfg.GetString(pr.cfgKey));
                            bool ok = false;
                            QString txt = QInputDialog::getText(nullptr,
                                QStringLiteral("Edit ") + QString::fromUtf8(pr.label),
                                QString::fromUtf8(pr.label), QLineEdit::Normal, cur, &ok);
                            s_colorDialogOpen = false;
                            if (ok) {
                                cfg.SetString(pr.cfgKey, txt.toStdString());
                                CustomHud_InvalidateConfigCache();
                            }
                            return;
                        }
                        case EditPropType::SubColor: {
                            float ovrW = 14.0f;
                            if (ds.x() < ctrlX + ovrW) {
                                cfg.SetBool(pr.cfgKey, !cfg.GetBool(pr.cfgKey));
                                CustomHud_InvalidateConfigCache();
                            } else if (!s_colorDialogOpen) {
                                QColor cur(cfg.GetInt(pr.extra1), cfg.GetInt(pr.extra2), cfg.GetInt(pr.extra3));
                                s_colorDialogOpen = true;
                                QColor picked = QColorDialog::getColor(cur, nullptr,
                                    QStringLiteral("Pick ") + QString::fromUtf8(pr.label) + QStringLiteral(" Color"));
                                s_colorDialogOpen = false;
                                if (picked.isValid()) {
                                    cfg.SetInt(pr.extra1, picked.red());
                                    cfg.SetInt(pr.extra2, picked.green());
                                    cfg.SetInt(pr.extra3, picked.blue());
                                    cfg.SetBool(pr.cfgKey, false);
                                    CustomHud_InvalidateConfigCache();
                                }
                            }
                            return;
                        }
                        case EditPropType::Color: break; // handled above as built-in
                        }
                    }
                    // Anchor grid click (when picker is open, grid is below row area)
                    if (s_anchorPickerOpen) {
                        float gridLeft = static_cast<float>(panelRect.left()) + 2.0f;
                        float gridTop  = static_cast<float>(panelRect.top()) + 2.0f +
                                         visCount * kPropRowH + 2.0f;
                        float cellW = (kPropPanelW - 4.0f) / 3.0f;
                        float cellH = kAnchorGridCellH;
                        for (int a = 0; a < 9; ++a) {
                            int col = a % 3, row = a / 3;
                            QRectF cell(gridLeft + col * cellW, gridTop + row * cellH, cellW, cellH);
                            if (cell.contains(ds)) {
                                const int oldAnchor = cfg.GetInt(d.anchorKey);
                                if (a != oldAnchor) {
                                    const int oldOfsX = cfg.GetInt(d.ofsXKey);
                                    const int oldOfsY = cfg.GetInt(d.ofsYKey);
                                    int finalX, finalY, newBaseX, newBaseY;
                                    ApplyAnchor(oldAnchor, oldOfsX, oldOfsY, finalX, finalY, s_editTopStretchX);
                                    ApplyAnchor(a, 0, 0, newBaseX, newBaseY, s_editTopStretchX);
                                    cfg.SetInt(d.anchorKey, a);
                                    cfg.SetInt(d.ofsXKey, finalX - newBaseX);
                                    cfg.SetInt(d.ofsYKey, finalY - newBaseY);
                                    CustomHud_InvalidateConfigCache();
                                }
                                s_anchorPickerOpen = false;
                                return;
                            }
                        }
                    }
                    return; // absorbed by panel
                }
            }
        }

        // Priority 5: Orientation toggle
        if (d.orientKey != nullptr && GetOrientToggleRect(s_editSelected).contains(ds)) {
            cfg.SetInt(d.orientKey, cfg.GetInt(d.orientKey) == 0 ? 1 : 0);
            CustomHud_InvalidateConfigCache();
            s_editRects[s_editSelected] = ComputeEditBounds(s_editSelected, cfg, s_editTopStretchX);
            return;
        }

        // Priority 6: Resize handles
        if (d.lengthKey != nullptr) {
            QRectF lenH, widH;
            GetResizeHandles(s_editSelected, cfg, lenH, widH);
            if (lenH.contains(ds)) {
                s_resizingLength = true;
                s_resizeStartVal = cfg.GetInt(d.lengthKey);
                s_resizeStartDS  = ds;
                return;
            }
            if (widH.contains(ds)) {
                s_resizingWidth  = true;
                s_resizeStartVal = cfg.GetInt(d.widthKey);
                s_resizeStartDS  = ds;
                return;
            }
        }
    }

    // Priority 5: Element drag
    for (int i = 0; i < kEditElemCount; ++i) {
        if (!s_editRects[i].contains(ds)) continue;
        const HudEditElemDesc& di = kEditElems[i];

        // Auto-switch gauge PosMode from text-relative (0) to independent (1)
        if (di.posModeKey != nullptr && cfg.GetInt(di.posModeKey) == 0) {
            if (UNLIKELY(!s_cache.valid)) {
                RefreshCachedConfig(cfg, s_editTopStretchX);
                s_cache.valid = true;
            } else if (s_cache.lastStretchX != s_editTopStretchX) {
                RecomputeAnchorPositions(s_editTopStretchX);
            }
            int visualX = 0, visualY = 0;
            if (i == 1) { visualX = s_cache.hp.hpGaugePosX;       visualY = s_cache.hp.hpGaugePosY; }
            if (i == 4) { visualX = s_cache.weapon.ammoGaugePosX;  visualY = s_cache.weapon.ammoGaugePosY; }
            cfg.SetInt(di.posModeKey, 1);
            cfg.SetInt(di.anchorKey, 0);
            cfg.SetInt(di.ofsXKey, visualX);
            cfg.SetInt(di.ofsYKey, visualY);
            CustomHud_InvalidateConfigCache();
        }

        s_editSelected  = i;
        s_editPropScroll = 0;
        s_anchorPickerOpen = false;
        s_dragging      = true;
        s_dragStartDS   = ds;
        s_dragStartOfsX = cfg.GetInt(di.ofsXKey);
        s_dragStartOfsY = cfg.GetInt(di.ofsYKey);
        s_editHovered   = i;
        NotifySelectionChanged(i);
        return;
    }

    s_editSelected = -1;  // deselect
    s_anchorPickerOpen = false;
    NotifySelectionChanged(-1);
}

void CustomHud_EditMouseMove(QPointF pt, Config::Table& cfg)
{
    const QPointF ds = WidgetToDS(pt);

    if (s_dragging && s_editSelected >= 0) {
        const HudEditElemDesc& d = kEditElems[s_editSelected];
        const int newOfsX = static_cast<int>(std::round(s_dragStartOfsX + ds.x() - s_dragStartDS.x()));
        const int newOfsY = static_cast<int>(std::round(s_dragStartOfsY + ds.y() - s_dragStartDS.y()));
        cfg.SetInt(d.ofsXKey, std::max(-512, std::min(512, newOfsX)));
        cfg.SetInt(d.ofsYKey, std::max(-512, std::min(512, newOfsY)));
        CustomHud_InvalidateConfigCache();
        return;
    }

    if ((s_resizingLength || s_resizingWidth) && s_editSelected >= 0) {
        const HudEditElemDesc& d = kEditElems[s_editSelected];
        const int ori = d.orientKey ? cfg.GetInt(d.orientKey) : 0;
        const bool squareResize = d.lengthKey && d.widthKey
                                  && strcmp(d.lengthKey, d.widthKey) == 0;
        // Radar gets bigger clamp range
        const int maxLen = squareResize ? 192 : 128;
        const int minLen = squareResize ? 16  : 4;
        const int maxWid = squareResize ? 192 : 20;
        const int minWid = squareResize ? 16  : 1;

        if (s_resizingLength) {
            const double delta = (ori == 1) ? ds.y() - s_resizeStartDS.y()
                                            : ds.x() - s_resizeStartDS.x();
            const int newVal = std::max(minLen, std::min(maxLen,
                static_cast<int>(std::round(s_resizeStartVal + delta))));
            cfg.SetInt(d.lengthKey, newVal);
            if (squareResize) cfg.SetInt(d.widthKey, newVal);
            CustomHud_InvalidateConfigCache();
        } else {
            const double delta = (ori == 1) ? ds.x() - s_resizeStartDS.x()
                                            : ds.y() - s_resizeStartDS.y();
            const int newVal = std::max(minWid, std::min(maxWid,
                static_cast<int>(std::round(s_resizeStartVal + delta))));
            cfg.SetInt(d.widthKey, newVal);
            if (squareResize) cfg.SetInt(d.lengthKey, newVal);
            CustomHud_InvalidateConfigCache();
        }
        return;
    }

    s_editHovered = -1;
    for (int i = 0; i < kEditElemCount; ++i) {
        if (s_editRects[i].contains(ds)) { s_editHovered = i; break; }
    }
}

void CustomHud_EditMouseRelease(QPointF pt, Qt::MouseButton btn, Config::Table& cfg)
{
    Q_UNUSED(pt);
    Q_UNUSED(btn);
    Q_UNUSED(cfg);
    s_dragging       = false;
    s_resizingLength = false;
    s_resizingWidth  = false;
}

void CustomHud_EditMouseWheel(QPointF pt, int delta, Config::Table& cfg)
{
    Q_UNUSED(cfg);
    if (!s_editMode) return;
    QPointF ds = WidgetToDS(pt);

    // Crosshair panel scroll
    if (s_crosshairPanelOpen) {
        const int totalRows = CountCrosshairRows();
        if (totalRows > kCrosshairMaxVisible) {
            const int visCount = std::min(totalRows, kCrosshairMaxVisible);
            const float panelH = visCount * kPropRowH + 4.0f;
            const QRectF panelRect(kCrosshairPanelX, kCrosshairPanelY, kPropPanelW, panelH);
            if (panelRect.contains(ds)) {
                int maxScroll = totalRows - kCrosshairMaxVisible;
                if (delta > 0) s_crosshairPanelScroll = std::max(0, s_crosshairPanelScroll - 1);
                else if (delta < 0) s_crosshairPanelScroll = std::min(maxScroll, s_crosshairPanelScroll + 1);
                return;
            }
        }
    }

    // Element props panel scroll
    if (s_editSelected < 0) return;
    const HudEditElemDesc& d = kEditElems[s_editSelected];
    const int totalRows = CountBuiltinRows(d) + d.propCount;
    if (totalRows <= kPropMaxVisible) return;

    QRectF panelRect = ComputePropsPanelRect(s_editRects[s_editSelected], totalRows);
    if (!panelRect.contains(ds)) return;

    int maxScroll = totalRows - kPropMaxVisible;
    if (delta > 0) s_editPropScroll = std::max(0, s_editPropScroll - 1);
    else if (delta < 0) s_editPropScroll = std::min(maxScroll, s_editPropScroll + 1);
}

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD





