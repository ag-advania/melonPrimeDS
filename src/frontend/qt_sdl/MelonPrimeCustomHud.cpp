#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeCustomHud.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeCompilerHints.h"
#include "MelonPrimeConstants.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "Config.h"

#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace MelonPrime {

// =========================================================================
//  P-1: Static icon cache — loaded once, reused every frame.
//       Eliminates QImage resource I/O + format conversion from hot path.
// =========================================================================
static QImage s_weaponIcons[9];
static QImage s_weaponTintedIcons[9];
static QColor s_weaponTintColor;
static bool   s_iconsLoaded = false;
static bool   s_weaponTintCacheValid = false;

// Bomb icon cache — 4 icons (index 0-3 = bombs remaining)
static QImage s_bombIcons[4];
static QImage s_bombTintedIcons[4];
static QColor s_bombTintColor;
static bool   s_bombIconsLoaded = false;
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

static void EnsureBombIconsLoaded()
{
    if (LIKELY(s_bombIconsLoaded)) return;
    static const char* kPaths[4] = {
        ":/mph-icon-bombs0", ":/mph-icon-bombs1",
        ":/mph-icon-bombs2", ":/mph-icon-bombs3"
    };
    for (int i = 0; i < 4; ++i) {
        QImage img(kPaths[i]);
        s_bombIcons[i] = img.isNull() ? img
            : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        s_bombTintedIcons[i] = s_bombIcons[i];
    }
    s_bombTintCacheValid = false;
    s_bombIconsLoaded = true;
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

static void EnsureIconsLoaded()
{
    if (LIKELY(s_iconsLoaded)) return;
    static const char* kIconPaths[9] = {
        ":/mph-icon-pb", ":/mph-icon-volt", ":/mph-icon-missile",
        ":/mph-icon-battlehammer", ":/mph-icon-imperialist",
        ":/mph-icon-judicator", ":/mph-icon-magmaul",
        ":/mph-icon-shock", ":/mph-icon-omega"
    };
    for (int i = 0; i < 9; i++) {
        QImage img(kIconPaths[i]);
        s_weaponIcons[i] = img.isNull() ? img
            : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        s_weaponTintedIcons[i] = s_weaponIcons[i];
    }
    s_weaponTintCacheValid = false;
    s_iconsLoaded = true;
}

// =========================================================================
//  P-2: Static outline buffer — allocated once, fill(transparent) per frame.
//       Eliminates 196 KB heap alloc+dealloc every frame.
// =========================================================================
static QImage s_outlineBuf;

static QImage& GetOutlineBuffer()
{
    if (UNLIKELY(s_outlineBuf.isNull()))
        s_outlineBuf = QImage(256, 192, QImage::Format_ARGB32_Premultiplied);
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

static inline void MeasureTextCached(const QFontMetrics& fm, int fontPixelSize,
                                     TextMeasureCache& cache, const char* text,
                                     int& outW, int& outH)
{
    if (!cache.valid || cache.fontPixelSize != fontPixelSize || std::strcmp(cache.text, text) != 0) {
        cache.width = fm.horizontalAdvance(QString::fromUtf8(text));
        cache.height = fm.height();
        std::strncpy(cache.text, text, sizeof(cache.text) - 1);
        cache.text[sizeof(cache.text) - 1] = '\0';
        cache.fontPixelSize = fontPixelSize;
        cache.valid = true;
    }

    outW = cache.width;
    outH = cache.height;
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

static inline void DrawCachedText(QPainter* p, const TextBitmapCache& cache, int x, int baselineY)
{
    if (!cache.valid || cache.bitmap.isNull()) return;
    p->drawImage(QPoint(x + cache.originX, baselineY + cache.originY), cache.bitmap);
}

// =========================================================================
//  P-3: Cached HUD config — refreshed only when config generation changes.
//       Avoids ~50 hash-map lookups per frame → single generation compare.
// =========================================================================
struct HpHudConfig {
    int hpX, hpY, hpAlign;
    char hpPrefix[48];
    bool hpTextAutoColor, hpGauge, hpAutoColor;
    QColor hpTextColor;
    int hpGaugeOri, hpGaugeLen, hpGaugeWid;
    int hpGaugeOfsX, hpGaugeOfsY, hpGaugeAnchor;
    int hpGaugePosMode, hpGaugePosX, hpGaugePosY;
    QColor hpGaugeColor;
};
struct WeaponHudConfig {
    int wpnX, wpnY, ammoAlign;
    char ammoPrefix[48];
    QColor ammoTextColor;
    bool iconShow, iconColorOverlay, ammoGauge;
    int iconMode, iconOfsX, iconOfsY, iconPosX, iconPosY;
    int iconAnchorX, iconAnchorY;
    int ammoGaugeOri, ammoGaugeLen, ammoGaugeWid;
    int ammoGaugeOfsX, ammoGaugeOfsY, ammoGaugeAnchor;
    int ammoGaugePosMode, ammoGaugePosX, ammoGaugePosY;
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
};
struct MatchStatusHudConfig {
    bool matchStatusShow;
    int matchStatusX, matchStatusY;
    int matchStatusLabelOfsX, matchStatusLabelOfsY;
    int matchStatusLabelPos;
    char matchStatusLabelPoints[64], matchStatusLabelOctoliths[64], matchStatusLabelLives[64];
    char matchStatusLabelRingTime[64], matchStatusLabelPrimeTime[64];
    QColor matchStatusColor, matchStatusLabelColor, matchStatusValueColor, matchStatusSepColor, matchStatusGoalColor;
};
struct BombLeftHudConfig {
    bool bombLeftShow, bombLeftTextShow;
    int bombLeftX, bombLeftY, bombLeftAlign;
    QColor bombLeftColor;
    char bombLeftPrefix[48];
    char bombLeftSuffix[48];
    bool bombIconShow, bombIconColorOverlay;
    QColor bombIconColor;
    int bombIconMode, bombIconOfsX, bombIconOfsY, bombIconPosX, bombIconPosY, bombIconAnchorX, bombIconAnchorY;
};
struct RankTimeHudConfig {
    bool rankShow;
    int rankX, rankY, rankAlign;
    QColor rankColor;
    char rankPrefix[48];
    bool rankShowOrdinal;
    char rankSuffix[48];
    bool timeLeftShow;
    int timeLeftX, timeLeftY, timeLeftAlign;
    QColor timeLeftColor;
    bool timeLimitShow;
    int timeLimitX, timeLimitY, timeLimitAlign;
    QColor timeLimitColor;
};
struct RadarOverlayConfig {
    bool radarShow;
    int radarDstX, radarDstY, radarDstSize;
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
static void LoadHpConfig(HpHudConfig& hp, Config::Table& cfg)
{
    hp.hpX = cfg.GetInt("Metroid.Visual.HudHpX");
    hp.hpY = cfg.GetInt("Metroid.Visual.HudHpY");
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
    hp.hpGaugePosX = cfg.GetInt("Metroid.Visual.HudHpGaugePosX");
    hp.hpGaugePosY = cfg.GetInt("Metroid.Visual.HudHpGaugePosY");
    hp.hpAutoColor = cfg.GetBool("Metroid.Visual.HudHpGaugeAutoColor");
    hp.hpGaugeColor = ReadRgbColor(cfg, "Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB");
}
static void LoadWeaponConfig(WeaponHudConfig& weapon, Config::Table& cfg)
{
    weapon.wpnX = cfg.GetInt("Metroid.Visual.HudWeaponX");
    weapon.wpnY = cfg.GetInt("Metroid.Visual.HudWeaponY");
    weapon.ammoAlign = cfg.GetInt("Metroid.Visual.HudAmmoAlign");
    CopyConfigString(weapon.ammoPrefix, sizeof(weapon.ammoPrefix), cfg.GetString("Metroid.Visual.HudAmmoPrefix"));
    weapon.ammoTextColor = ReadRgbColor(cfg, "Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB");
    weapon.iconShow = cfg.GetBool("Metroid.Visual.HudWeaponIconShow");
    weapon.iconColorOverlay = cfg.GetBool("Metroid.Visual.HudWeaponIconColorOverlay");
    weapon.iconMode = cfg.GetInt("Metroid.Visual.HudWeaponIconMode");
    weapon.iconOfsX = cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetX");
    weapon.iconOfsY = cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetY");
    weapon.iconPosX = cfg.GetInt("Metroid.Visual.HudWeaponIconPosX");
    weapon.iconPosY = cfg.GetInt("Metroid.Visual.HudWeaponIconPosY");
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
    weapon.ammoGaugePosX = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosX");
    weapon.ammoGaugePosY = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosY");
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
}
static void LoadMatchStatusConfig(MatchStatusHudConfig& matchStatus, Config::Table& cfg)
{
    matchStatus.matchStatusShow = cfg.GetBool("Metroid.Visual.HudMatchStatusShow");
    matchStatus.matchStatusX = cfg.GetInt("Metroid.Visual.HudMatchStatusX");
    matchStatus.matchStatusY = cfg.GetInt("Metroid.Visual.HudMatchStatusY");
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
    bombLeft.bombLeftX = cfg.GetInt("Metroid.Visual.HudBombLeftX");
    bombLeft.bombLeftY = cfg.GetInt("Metroid.Visual.HudBombLeftY");
    bombLeft.bombLeftAlign = cfg.GetInt("Metroid.Visual.HudBombLeftAlign");
    bombLeft.bombLeftColor = ReadRgbColor(cfg, "Metroid.Visual.HudBombLeftColorR", "Metroid.Visual.HudBombLeftColorG", "Metroid.Visual.HudBombLeftColorB");
    CopyConfigString(bombLeft.bombLeftPrefix, sizeof(bombLeft.bombLeftPrefix), cfg.GetString("Metroid.Visual.HudBombLeftPrefix"));
    CopyConfigString(bombLeft.bombLeftSuffix, sizeof(bombLeft.bombLeftSuffix), cfg.GetString("Metroid.Visual.HudBombLeftSuffix"));
    bombLeft.bombIconShow = cfg.GetBool("Metroid.Visual.HudBombLeftIconShow");
    bombLeft.bombIconColorOverlay = cfg.GetBool("Metroid.Visual.HudBombLeftIconColorOverlay");
    bombLeft.bombIconColor = ReadRgbColor(cfg, "Metroid.Visual.HudBombLeftIconColorR", "Metroid.Visual.HudBombLeftIconColorG", "Metroid.Visual.HudBombLeftIconColorB");
    bombLeft.bombIconMode = cfg.GetInt("Metroid.Visual.HudBombLeftIconMode");
    bombLeft.bombIconOfsX = cfg.GetInt("Metroid.Visual.HudBombLeftIconOfsX");
    bombLeft.bombIconOfsY = cfg.GetInt("Metroid.Visual.HudBombLeftIconOfsY");
    bombLeft.bombIconPosX = cfg.GetInt("Metroid.Visual.HudBombLeftIconPosX");
    bombLeft.bombIconPosY = cfg.GetInt("Metroid.Visual.HudBombLeftIconPosY");
    bombLeft.bombIconAnchorX = cfg.GetInt("Metroid.Visual.HudBombLeftIconAnchorX");
    bombLeft.bombIconAnchorY = cfg.GetInt("Metroid.Visual.HudBombLeftIconAnchorY");
}
static void LoadRankTimeConfig(RankTimeHudConfig& rankTime, Config::Table& cfg)
{
    rankTime.rankShow = cfg.GetBool("Metroid.Visual.HudRankShow");
    rankTime.rankX = cfg.GetInt("Metroid.Visual.HudRankX");
    rankTime.rankY = cfg.GetInt("Metroid.Visual.HudRankY");
    rankTime.rankAlign = cfg.GetInt("Metroid.Visual.HudRankAlign");
    rankTime.rankColor = ReadRgbColor(cfg, "Metroid.Visual.HudRankColorR", "Metroid.Visual.HudRankColorG", "Metroid.Visual.HudRankColorB");
    CopyConfigString(rankTime.rankPrefix, sizeof(rankTime.rankPrefix), cfg.GetString("Metroid.Visual.HudRankPrefix"));
    rankTime.rankShowOrdinal = cfg.GetBool("Metroid.Visual.HudRankShowOrdinal");
    CopyConfigString(rankTime.rankSuffix, sizeof(rankTime.rankSuffix), cfg.GetString("Metroid.Visual.HudRankSuffix"));
    rankTime.timeLeftShow = cfg.GetBool("Metroid.Visual.HudTimeLeftShow");
    rankTime.timeLeftX = cfg.GetInt("Metroid.Visual.HudTimeLeftX");
    rankTime.timeLeftY = cfg.GetInt("Metroid.Visual.HudTimeLeftY");
    rankTime.timeLeftAlign = cfg.GetInt("Metroid.Visual.HudTimeLeftAlign");
    rankTime.timeLeftColor = ReadRgbColor(cfg, "Metroid.Visual.HudTimeLeftColorR", "Metroid.Visual.HudTimeLeftColorG", "Metroid.Visual.HudTimeLeftColorB");
    rankTime.timeLimitShow = cfg.GetBool("Metroid.Visual.HudTimeLimitShow");
    rankTime.timeLimitX = cfg.GetInt("Metroid.Visual.HudTimeLimitX");
    rankTime.timeLimitY = cfg.GetInt("Metroid.Visual.HudTimeLimitY");
    rankTime.timeLimitAlign = cfg.GetInt("Metroid.Visual.HudTimeLimitAlign");
    rankTime.timeLimitColor = ReadRgbColor(cfg, "Metroid.Visual.HudTimeLimitColorR", "Metroid.Visual.HudTimeLimitColorG", "Metroid.Visual.HudTimeLimitColorB");
}
static void LoadRadarOverlayConfig(RadarOverlayConfig& radar, Config::Table& cfg)
{
    radar.radarShow = cfg.GetBool("Metroid.Visual.BtmOverlayEnable");
    radar.radarDstX = cfg.GetInt("Metroid.Visual.BtmOverlayDstX");
    radar.radarDstY = cfg.GetInt("Metroid.Visual.BtmOverlayDstY");
    radar.radarDstSize = std::max(cfg.GetInt("Metroid.Visual.BtmOverlayDstSize"), 1);
    radar.radarOpacity = std::clamp(cfg.GetDouble("Metroid.Visual.BtmOverlayOpacity"), 0.0, 1.0);
    radar.radarSrcRadius = std::max(cfg.GetInt("Metroid.Visual.BtmOverlaySrcRadius"), 1);
    radar.radarDstRect = QRect(radar.radarDstX, radar.radarDstY, radar.radarDstSize, radar.radarDstSize);
    radar.radarClipPath = QPainterPath();
    radar.radarClipPath.addEllipse(radar.radarDstRect);
}
static void RefreshCachedConfig(Config::Table& cfg)
{
    auto& c = s_cache;
    LoadHpConfig(c.hp, cfg);
    LoadWeaponConfig(c.weapon, cfg);
    LoadCrosshairConfig(c.crosshair, cfg);
    LoadMatchStatusConfig(c.matchStatus, cfg);
    LoadBombLeftConfig(c.bombLeft, cfg);
    LoadRankTimeConfig(c.rankTime, cfg);
    LoadRadarOverlayConfig(c.radar, cfg);
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
static void DrawMatchStatusText(QPainter* p, const QFontMetrics& fm, int fontPixelSize, const MatchStatusResolvedState& state, const MatchStatusHudConfig& c)
{
    static TextMeasureCache s_curTextCache = { 0, "", 0, 0, false }, s_sepTextCache = { 0, "", 0, 0, false }, s_goalTextCache = { 0, "", 0, 0, false };
    static TextBitmapCache s_curBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() }, s_sepBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() }, s_goalBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() }, s_labelBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() };
    static MatchStatusStringCache s_matchStringCache = { 0, 0, 0, 0, 0, false, false, false, "", "", "" };
    UpdateMatchStatusStrings(s_matchStringCache, state);
    auto eff = [&](const QColor& sub) -> const QColor& { return sub.isValid() ? sub : c.matchStatusColor; };
    int vx = c.matchStatusX, vy = c.matchStatusY, curW = 0, curH = 0;
    MeasureTextCached(fm, fontPixelSize, s_curTextCache, s_matchStringCache.curBuf, curW, curH);
    PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_curBitmapCache, s_matchStringCache.curBuf, eff(c.matchStatusValueColor)); DrawCachedText(p, s_curBitmapCache, vx, vy);
    if (s_matchStringCache.hasGoal) { int sepW = 0, sepH = 0, goalW = 0, goalH = 0; MeasureTextCached(fm, fontPixelSize, s_sepTextCache, s_matchStringCache.sepBuf, sepW, sepH); MeasureTextCached(fm, fontPixelSize, s_goalTextCache, s_matchStringCache.goalBuf, goalW, goalH); int x = vx + curW; PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_sepBitmapCache, s_matchStringCache.sepBuf, eff(c.matchStatusSepColor)); DrawCachedText(p, s_sepBitmapCache, x, vy); x += sepW; PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_goalBitmapCache, s_matchStringCache.goalBuf, eff(c.matchStatusGoalColor)); DrawCachedText(p, s_goalBitmapCache, x, vy); }
    const char* label = ResolveMatchStatusLabel(state.mode, c); if (label[0] == '\0') return; int lx = vx, ly = vy; switch (c.matchStatusLabelPos) { default: case 0: ly = vy - 10; break; case 1: ly = vy + 10; break; case 2: lx = vx - 50; break; case 3: lx = vx + 50; break; case 4: break; } lx += c.matchStatusLabelOfsX; ly += c.matchStatusLabelOfsY; PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_labelBitmapCache, label, eff(c.matchStatusLabelColor)); DrawCachedText(p, s_labelBitmapCache, lx, ly);
}
static void DrawMatchStatusHud(QPainter* p, melonDS::u8* ram, const RomAddresses& rom, uint8_t playerPos, bool isAdventure, const CachedHudConfig& c)
{
    if (!c.matchStatus.matchStatusShow || isAdventure) return; MatchStatusResolvedState state = {}; if (!ComputeMatchStatusState(ram, rom, playerPos, state)) return; const QFontMetrics fm = p->fontMetrics(); const int fontPixelSize = p->font().pixelSize(); DrawMatchStatusText(p, fm, fontPixelSize, state, c.matchStatus);
}
static int CalcAlignedTextX(int anchorX, int align, int textW);
static void DrawCachedAlignedText(QPainter* p, const QFontMetrics& fm, int fontPixelSize, TextMeasureCache& measureCache, TextBitmapCache& bitmapCache, const char* text, const QColor& color, int anchorX, int align, int y)
{
    int textW = 0, textH = 0; MeasureTextCached(fm, fontPixelSize, measureCache, text, textW, textH); const int textX = CalcAlignedTextX(anchorX, align, textW); PrepareTextBitmapCached(fm, p->font(), fontPixelSize, bitmapCache, text, color); DrawCachedText(p, bitmapCache, textX, y);
}
// =========================================================================
static int CalcAlignedTextX(int anchorX, int align, int textW);
// =========================================================================
//  Bomb Left HUD
// =========================================================================
static void DrawBombLeft(QPainter* p, melonDS::u8* ram, const RomAddresses& rom, uint32_t offP, const CachedHudConfig& c)
{
    if (!c.bombLeft.bombLeftShow) return; uint8_t bombs = static_cast<uint8_t>((Read32(ram, rom.baseBomb + offP) >> 8) & 0xF);
    { static TextBitmapCache s_bombBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() }; const QFontMetrics fm = p->fontMetrics(); const int fontPixelSize = p->font().pixelSize(); char buf[64]; if (c.bombLeft.bombLeftTextShow) std::snprintf(buf, sizeof(buf), "%s%u%s", c.bombLeft.bombLeftPrefix, bombs, c.bombLeft.bombLeftSuffix); else std::snprintf(buf, sizeof(buf), "%s%s", c.bombLeft.bombLeftPrefix, c.bombLeft.bombLeftSuffix); if (buf[0] != '\0') { PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_bombBitmapCache, buf, c.bombLeft.bombLeftColor); const int bombTextX = CalcAlignedTextX(c.bombLeft.bombLeftX, c.bombLeft.bombLeftAlign, s_bombBitmapCache.bitmap.width()); DrawCachedText(p, s_bombBitmapCache, bombTextX, c.bombLeft.bombLeftY); } }
    if (c.bombLeft.bombIconShow) { EnsureBombIconsLoaded(); const QImage& icon = GetBombIconForDraw(bombs, c.bombLeft.bombIconColorOverlay, c.bombLeft.bombIconColor); if (!icon.isNull()) { int ix = (c.bombLeft.bombIconMode == 0) ? c.bombLeft.bombLeftX + c.bombLeft.bombIconOfsX : c.bombLeft.bombIconPosX; int iy = (c.bombLeft.bombIconMode == 0) ? c.bombLeft.bombLeftY + c.bombLeft.bombIconOfsY : c.bombLeft.bombIconPosY; if (c.bombLeft.bombIconAnchorX == 1) ix -= icon.width() / 2; else if (c.bombLeft.bombIconAnchorX == 2) ix -= icon.width(); if (c.bombLeft.bombIconAnchorY == 1) iy -= icon.height() / 2; else if (c.bombLeft.bombIconAnchorY == 2) iy -= icon.height(); p->drawImage(QPoint(ix, iy), icon); } }
}
//  Rank & Time HUD
// =========================================================================
static void DrawRankAndTime(QPainter* p, melonDS::u8* ram, const RomAddresses& rom, uint8_t playerPos, bool isAdventure, const CachedHudConfig& c)
{
    if (isAdventure) return; const auto& hud = c.rankTime; const QFontMetrics fm = p->fontMetrics(); const int fontPixelSize = p->font().pixelSize();
    if (hud.rankShow) { static RankStringCache s_rankStringCache = { 0, 0, false, false, "" }; static TextBitmapCache s_rankCache = { 0, QColor(), "", 0, 0, false, QImage() }; static TextMeasureCache s_rankMeasure = { 0, "", 0, 0, false }; uint32_t rankWord = Read32(ram, rom.matchRank); uint8_t rankByte = (rankWord >> (playerPos * 8)) & 0xFF; if (rankByte <= 3) DrawCachedAlignedText(p, fm, fontPixelSize, s_rankMeasure, s_rankCache, UpdateRankString(s_rankStringCache, rankByte, hud.rankShowOrdinal, hud), hud.rankColor, hud.rankX, hud.rankAlign, hud.rankY); }
    if (hud.timeLeftShow) { static TimeStringCache s_timeLeftStringCache = { 0, 0, false, "" }; static TextBitmapCache s_timeLeftCache = { 0, QColor(), "", 0, 0, false, QImage() }; static TextMeasureCache s_timeLeftMeasure = { 0, "", 0, 0, false }; int seconds = static_cast<int>(Read32(ram, rom.timeLeft)) / 60; DrawCachedAlignedText(p, fm, fontPixelSize, s_timeLeftMeasure, s_timeLeftCache, UpdateTimeString(s_timeLeftStringCache, seconds, false), hud.timeLeftColor, hud.timeLeftX, hud.timeLeftAlign, hud.timeLeftY); }
    if (hud.timeLimitShow) { static TimeStringCache s_timeLimitStringCache = { 0, 0, false, "" }; static TextBitmapCache s_timeLimitCache = { 0, QColor(), "", 0, 0, false, QImage() }; static TextMeasureCache s_timeLimitMeasure = { 0, "", 0, 0, false }; int goalMinutes = s_battleState.valid ? s_battleState.timeLimitMinutes : LookupTimeLimitMin((Read32(ram, rom.battleSettings + 4) >> 8) & 0xFF); DrawCachedAlignedText(p, fm, fontPixelSize, s_timeLimitMeasure, s_timeLimitCache, UpdateTimeString(s_timeLimitStringCache, goalMinutes, true), hud.timeLimitColor, hud.timeLimitX, hud.timeLimitAlign, hud.timeLimitY); }
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
                           const CachedHudConfig& c)
{
    const QColor hpTextColor = c.hp.hpTextAutoColor ? HpGaugeColor(hp, c.hp.hpTextColor) : c.hp.hpTextColor;

    static TextMeasureCache s_hpTextCache = { 0, "", 0, 0, false };
    static TextBitmapCache s_hpBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() };
    const QFontMetrics fm = p->fontMetrics();
    const int fontPixelSize = p->font().pixelSize();

    char buf[24];
    std::snprintf(buf, sizeof(buf), "%s%u", c.hp.hpPrefix, hp);
    int textW = 0, textH = 0;
    MeasureTextCached(fm, fontPixelSize, s_hpTextCache, buf, textW, textH);
    const int textX = CalcAlignedTextX(c.hp.hpX, c.hp.hpAlign, textW);
    PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_hpBitmapCache, buf, hpTextColor);
    DrawCachedText(p, s_hpBitmapCache, textX, c.hp.hpY);

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
                           const CachedHudConfig& c)
{
    static TextMeasureCache s_ammoTextCache = { 0, "", 0, 0, false };
    static TextBitmapCache s_ammoBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() };
    const QFontMetrics fm = p->fontMetrics();
    const int fontPixelSize = p->font().pixelSize();

    const WeaponInfo& wi = kWeaponTable[weapon];
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
        MeasureTextCached(fm, fontPixelSize, s_ammoTextCache, buf, textW, textH);
        textX = CalcAlignedTextX(c.weapon.wpnX, c.weapon.ammoAlign, textW);
        PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_ammoBitmapCache, buf, c.weapon.ammoTextColor);
        DrawCachedText(p, s_ammoBitmapCache, textX, textY);
    }

    if (c.weapon.iconShow && !icon.isNull()) {
        int ix = (c.weapon.iconMode == 0) ? c.weapon.wpnX + c.weapon.iconOfsX : c.weapon.iconPosX;
        int iy = (c.weapon.iconMode == 0) ? c.weapon.wpnY + c.weapon.iconOfsY : c.weapon.iconPosY;
        if (c.weapon.iconAnchorX == 1) ix -= icon.width() / 2;
        else if (c.weapon.iconAnchorX == 2) ix -= icon.width();
        if (c.weapon.iconAnchorY == 1) iy -= icon.height() / 2;
        else if (c.weapon.iconAnchorY == 2) iy -= icon.height();
        p->drawImage(QPoint(ix, iy), icon);
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
struct ArmCoords { int x1, y1, x2, y2; };

static int CollectArms(ArmCoords* out, int cx, int cy,
                       int lengthX, int lengthY, int offset, bool tStyle)
{
    int n = 0;
    if (lengthX > 0) {
        out[n++] = { cx - offset - lengthX, cy, cx - offset - 1, cy };
        out[n++] = { cx + offset + 1,       cy, cx + offset + lengthX, cy };
    }
    if (lengthY > 0) {
        out[n++] = { cx, cy + offset + 1,  cx, cy + offset + lengthY };
        if (!tStyle)
            out[n++] = { cx, cy - offset - lengthY, cx, cy - offset - 1 };
    }
    return n;
}

// P-8: Reads directly from CachedHudConfig — no ReadCrosshairConfig() copy.
// P-9: QPen set once per thickness group, not per arm.
static void DrawCrosshair(QPainter* p, melonDS::u8* ram,
                          const RomAddresses& rom,
                          const CachedHudConfig& c,
                          float stretchX)
{
    int cx = static_cast<int>(Read8(ram, rom.crosshairPosX));
    int cy = static_cast<int>(Read8(ram, rom.crosshairPosY));

    if (stretchX > 1.0f)
        cx = static_cast<int>(std::lround(128.0f + (cx - 128.0f) / stretchX));

    ArmCoords innerArms[4], outerArms[4];
    int nInner = 0, nOuter = 0;
    if (c.crosshair.chInnerShow)
        nInner = CollectArms(innerArms, cx, cy, c.crosshair.chInnerLengthX, c.crosshair.chInnerLengthY, c.crosshair.chInnerOffset, c.crosshair.chTStyle);
    if (c.crosshair.chOuterShow)
        nOuter = CollectArms(outerArms, cx, cy, c.crosshair.chOuterLengthX, c.crosshair.chOuterLengthY, c.crosshair.chOuterOffset, c.crosshair.chTStyle);

    int dotHalf = c.crosshair.chDotThickness / 2;

    if (c.crosshair.chOutline && c.crosshair.chOutlineOpacity > 0.0) {
        int minX = cx, maxX = cx, minY = cy, maxY = cy;
        auto expandBounds = [&](int x, int y, int pad) {
            if (x - pad < minX) minX = x - pad;
            if (x + pad > maxX) maxX = x + pad;
            if (y - pad < minY) minY = y - pad;
            if (y + pad > maxY) maxY = y + pad;
        };

        if (c.crosshair.chCenterDot) {
            expandBounds(cx, cy, dotHalf + c.crosshair.chOutlineThickness);
        }
        for (int i = 0; i < nInner; i++) {
            int pad = (c.crosshair.chInnerThickness + c.crosshair.chOutlineThickness * 2 + 1) / 2;
            expandBounds(innerArms[i].x1, innerArms[i].y1, pad);
            expandBounds(innerArms[i].x2, innerArms[i].y2, pad);
        }
        for (int i = 0; i < nOuter; i++) {
            int pad = (c.crosshair.chOuterThickness + c.crosshair.chOutlineThickness * 2 + 1) / 2;
            expandBounds(outerArms[i].x1, outerArms[i].y1, pad);
            expandBounds(outerArms[i].x2, outerArms[i].y2, pad);
        }

        QImage& olBuf = GetOutlineBuffer();
        const int bufMaxX = olBuf.width() - 1;
        const int bufMaxY = olBuf.height() - 1;
        if (minX < 0) minX = 0;
        if (minY < 0) minY = 0;
        if (maxX > bufMaxX) maxX = bufMaxX;
        if (maxY > bufMaxY) maxY = bufMaxY;
        QRect dirtyRect(minX, minY, maxX - minX + 1, maxY - minY + 1);

        {
            QPainter olP(&olBuf);
            olP.setRenderHint(QPainter::Antialiasing, false);
            olP.setCompositionMode(QPainter::CompositionMode_Source);
            olP.fillRect(dirtyRect, Qt::transparent);
            olP.setCompositionMode(QPainter::CompositionMode_SourceOver);
            static const QColor solidBlack(0, 0, 0, 255);

            if (c.crosshair.chCenterDot) {
                olP.setPen(Qt::NoPen);
                olP.setBrush(solidBlack);
                int oh = dotHalf + c.crosshair.chOutlineThickness;
                olP.drawRect(cx - oh, cy - oh, oh * 2 + 1, oh * 2 + 1);
                olP.setBrush(Qt::NoBrush);
            }
            if (nInner > 0) {
                QPen pen(solidBlack);
                pen.setWidth(c.crosshair.chInnerThickness + c.crosshair.chOutlineThickness * 2);
                olP.setPen(pen);
                for (int i = 0; i < nInner; i++)
                    olP.drawLine(innerArms[i].x1, innerArms[i].y1, innerArms[i].x2, innerArms[i].y2);
            }
            if (nOuter > 0) {
                QPen pen(solidBlack);
                pen.setWidth(c.crosshair.chOuterThickness + c.crosshair.chOutlineThickness * 2);
                olP.setPen(pen);
                for (int i = 0; i < nOuter; i++)
                    olP.drawLine(outerArms[i].x1, outerArms[i].y1, outerArms[i].x2, outerArms[i].y2);
            }
        }
        p->setOpacity(c.crosshair.chOutlineOpacity);
        p->drawImage(dirtyRect.topLeft(), olBuf, dirtyRect);
        p->setOpacity(1.0);
    }

    if (c.crosshair.chCenterDot) {
        p->setPen(Qt::NoPen);
        QColor dotColor = c.crosshair.chColor;
        dotColor.setAlphaF(c.crosshair.chDotOpacity);
        p->setBrush(dotColor);
        p->drawRect(cx - dotHalf, cy - dotHalf, dotHalf * 2 + 1, dotHalf * 2 + 1);
        p->setBrush(Qt::NoBrush);
    }
    if (nInner > 0) {
        QColor clr = c.crosshair.chColor; clr.setAlphaF(c.crosshair.chInnerOpacity);
        QPen pen(clr); pen.setWidth(c.crosshair.chInnerThickness); p->setPen(pen);
        for (int i = 0; i < nInner; i++)
            p->drawLine(innerArms[i].x1, innerArms[i].y1, innerArms[i].x2, innerArms[i].y2);
    }

    if (nOuter > 0) {
        QColor clr = c.crosshair.chColor; clr.setAlphaF(c.crosshair.chOuterOpacity);
        QPen pen(clr); pen.setWidth(c.crosshair.chOuterThickness); p->setPen(pen);
        for (int i = 0; i < nOuter; i++)
            p->drawLine(outerArms[i].x1, outerArms[i].y1, outerArms[i].x2, outerArms[i].y2);
    }
}

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
    float topStretchX)
{
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

    EnsureIconsLoaded();                     // P-1: one-time init
    ApplyNoHudPatch(nds, romGroup);

    // P-3: Refresh config cache only when invalidated
    if (UNLIKELY(!s_cache.valid)) {
        RefreshCachedConfig(localCfg);
        s_cache.valid = true;
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

    if (topPaint->font().pixelSize() != kCustomHudFontSize) {
        QFont f = topPaint->font();
        f.setPixelSize(kCustomHudFontSize);
        topPaint->setFont(f);
    }

    const uint8_t hunterID = Read8(ram, addrHot.chosenHunter);
    bool isAlt   = Read8(ram, addrHot.isAltForm) == 0x02;

    DrawHP(topPaint, currentHP, maxHP, c);

    // Bomb count: Samus/Sylux in alt form only
    {
        bool isBomber = (hunterID == static_cast<uint8_t>(HunterId::Samus) ||
                         hunterID == static_cast<uint8_t>(HunterId::Sylux));
        if (isBomber && isAlt)
            DrawBombLeft(topPaint, ram, rom, offP, c);
    }

    // Match Status + Rank & Time HUDs (non-adventure only, visible in all camera modes)
    {
        bool isAdventure = Read8(ram, rom.isInAdventure) == 0x02;
        DrawMatchStatusHud(topPaint, ram, rom, playerPosition, isAdventure, c);
        DrawRankAndTime(topPaint, ram, rom, playerPosition, isAdventure, c);
    }

    if (!isFirstPerson) return;

    uint8_t currentWeapon = Read8(ram, addrHot.currentWeapon);
    DrawWeaponAmmo(topPaint, ram, currentWeapon,
                   Read16(ram, addrAmmoSpecial), addrAmmoMissile,
                   maxAmmoSpecial, maxAmmoMissile, c);

    bool isTrans = (Read8(ram, addrHot.jumpFlag) & 0x10) != 0;
    if (!isTrans && !isAlt)
        DrawCrosshair(topPaint, ram, rom, c, topStretchX);

    // Draw bottom screen overlay on top screen
    DrawBottomScreenOverlay(localCfg, topPaint, btmBuffer, (hunterID <= 6) ? hunterID : 0);
}

void DrawBottomScreenOverlay(Config::Table& localCfg, QPainter* topPaint, QImage* btmBuffer, uint8_t hunterID)
{
    if (UNLIKELY(!s_cache.valid)) {
        RefreshCachedConfig(localCfg);
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

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD




