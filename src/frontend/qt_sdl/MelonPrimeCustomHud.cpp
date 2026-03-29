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
struct CachedHudConfig {
    // HP
    int    hpX, hpY, hpAlign;
    char   hpPrefix[48];
    bool   hpTextAutoColor, hpGauge, hpAutoColor;
    QColor hpTextColor;
    int    hpGaugeOri, hpGaugeLen, hpGaugeWid;
    int    hpGaugeOfsX, hpGaugeOfsY, hpGaugeAnchor;
    int    hpGaugePosMode, hpGaugePosX, hpGaugePosY;
    QColor hpGaugeColor;
    // Weapon / Ammo
    int    wpnX, wpnY, ammoAlign;
    char   ammoPrefix[48];
    QColor ammoTextColor;
    bool   iconShow, iconColorOverlay, ammoGauge;
    int    iconMode, iconOfsX, iconOfsY, iconPosX, iconPosY;
    int    iconAnchorX, iconAnchorY; // 0=Left/Top  1=Center  2=Right/Bottom
    int    ammoGaugeOri, ammoGaugeLen, ammoGaugeWid;
    int    ammoGaugeOfsX, ammoGaugeOfsY, ammoGaugeAnchor;
    int    ammoGaugePosMode, ammoGaugePosX, ammoGaugePosY;
    QColor ammoGaugeColor;
    // Crosshair — general
    QColor chColor;
    bool   chOutline, chCenterDot, chTStyle;
    double chOutlineOpacity, chDotOpacity;
    int    chOutlineThickness, chDotThickness;
    // Crosshair — inner
    bool   chInnerShow;
    double chInnerOpacity;
    int    chInnerLengthX, chInnerLengthY, chInnerThickness, chInnerOffset;
    // Crosshair — outer
    bool   chOuterShow;
    double chOuterOpacity;
    int    chOuterLengthX, chOuterLengthY, chOuterThickness, chOuterOffset;
    // Match Status HUD
    bool   matchStatusShow;
    int    matchStatusX, matchStatusY;
    int    matchStatusLabelOfsX, matchStatusLabelOfsY;
    int    matchStatusLabelPos; // 0=Above,1=Below,2=Left,3=Right,4=Center
    char   matchStatusLabelPoints[64], matchStatusLabelOctoliths[64], matchStatusLabelLives[64];
    char   matchStatusLabelRingTime[64], matchStatusLabelPrimeTime[64];
    QColor matchStatusColor;       // overall (fallback)
    QColor matchStatusLabelColor;  // invalid = use matchStatusColor
    QColor matchStatusValueColor;  // invalid = use matchStatusColor
    QColor matchStatusSepColor;    // invalid = use matchStatusColor
    QColor matchStatusGoalColor;   // invalid = use matchStatusColor
    // Rank & Time HUD
    bool   rankShow;
    int    rankX, rankY;
    QColor rankColor;
    char   rankPrefix[48];
    bool   rankShowOrdinal;
    char   rankSuffix[48];
    bool   timeLeftShow;
    int    timeLeftX, timeLeftY;
    QColor timeLeftColor;
    bool   timeLimitShow;
    int    timeLimitX, timeLimitY;
    QColor timeLimitColor;
    // Cache invalidation
    bool valid;
};

static CachedHudConfig s_cache = { .valid = false };

static void RefreshCachedConfig(Config::Table& cfg)
{
    auto& c = s_cache;
    // HP
    c.hpX = cfg.GetInt("Metroid.Visual.HudHpX");
    c.hpY = cfg.GetInt("Metroid.Visual.HudHpY");
    c.hpAlign = cfg.GetInt("Metroid.Visual.HudHpAlign");
    { auto s = cfg.GetString("Metroid.Visual.HudHpPrefix");
      std::strncpy(c.hpPrefix, s.c_str(), sizeof(c.hpPrefix)-1);
      c.hpPrefix[sizeof(c.hpPrefix)-1] = '\0'; }
    c.hpTextAutoColor = cfg.GetBool("Metroid.Visual.HudHpTextAutoColor");
    c.hpTextColor = QColor(cfg.GetInt("Metroid.Visual.HudHpTextColorR"),
                           cfg.GetInt("Metroid.Visual.HudHpTextColorG"),
                           cfg.GetInt("Metroid.Visual.HudHpTextColorB"));
    c.hpGauge      = cfg.GetBool("Metroid.Visual.HudHpGauge");
    c.hpGaugeOri   = cfg.GetInt("Metroid.Visual.HudHpGaugeOrientation");
    c.hpGaugeLen   = cfg.GetInt("Metroid.Visual.HudHpGaugeLength");
    c.hpGaugeWid   = cfg.GetInt("Metroid.Visual.HudHpGaugeWidth");
    c.hpGaugeOfsX  = cfg.GetInt("Metroid.Visual.HudHpGaugeOffsetX");
    c.hpGaugeOfsY  = cfg.GetInt("Metroid.Visual.HudHpGaugeOffsetY");
    c.hpGaugeAnchor = cfg.GetInt("Metroid.Visual.HudHpGaugeAnchor");
    c.hpGaugePosMode = cfg.GetInt("Metroid.Visual.HudHpGaugePosMode");
    c.hpGaugePosX  = cfg.GetInt("Metroid.Visual.HudHpGaugePosX");
    c.hpGaugePosY  = cfg.GetInt("Metroid.Visual.HudHpGaugePosY");
    c.hpAutoColor  = cfg.GetBool("Metroid.Visual.HudHpGaugeAutoColor");
    c.hpGaugeColor = QColor(cfg.GetInt("Metroid.Visual.HudHpGaugeColorR"),
                            cfg.GetInt("Metroid.Visual.HudHpGaugeColorG"),
                            cfg.GetInt("Metroid.Visual.HudHpGaugeColorB"));
    // Weapon / Ammo
    c.wpnX = cfg.GetInt("Metroid.Visual.HudWeaponX");
    c.wpnY = cfg.GetInt("Metroid.Visual.HudWeaponY");
    c.ammoAlign = cfg.GetInt("Metroid.Visual.HudAmmoAlign");
    { auto s = cfg.GetString("Metroid.Visual.HudAmmoPrefix");
      std::strncpy(c.ammoPrefix, s.c_str(), sizeof(c.ammoPrefix)-1);
      c.ammoPrefix[sizeof(c.ammoPrefix)-1] = '\0'; }
    c.ammoTextColor = QColor(cfg.GetInt("Metroid.Visual.HudAmmoTextColorR"),
                              cfg.GetInt("Metroid.Visual.HudAmmoTextColorG"),
                              cfg.GetInt("Metroid.Visual.HudAmmoTextColorB"));
    c.iconShow = cfg.GetBool("Metroid.Visual.HudWeaponIconShow");
    c.iconColorOverlay = cfg.GetBool("Metroid.Visual.HudWeaponIconColorOverlay");
    c.iconMode = cfg.GetInt("Metroid.Visual.HudWeaponIconMode");
    c.iconOfsX = cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetX");
    c.iconOfsY = cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetY");
    c.iconPosX    = cfg.GetInt("Metroid.Visual.HudWeaponIconPosX");
    c.iconPosY    = cfg.GetInt("Metroid.Visual.HudWeaponIconPosY");
    c.iconAnchorX = cfg.GetInt("Metroid.Visual.HudWeaponIconAnchorX");
    c.iconAnchorY = cfg.GetInt("Metroid.Visual.HudWeaponIconAnchorY");
    c.ammoGauge     = cfg.GetBool("Metroid.Visual.HudAmmoGauge");
    c.ammoGaugeOri  = cfg.GetInt("Metroid.Visual.HudAmmoGaugeOrientation");
    c.ammoGaugeLen  = cfg.GetInt("Metroid.Visual.HudAmmoGaugeLength");
    c.ammoGaugeWid  = cfg.GetInt("Metroid.Visual.HudAmmoGaugeWidth");
    c.ammoGaugeOfsX = cfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetX");
    c.ammoGaugeOfsY = cfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetY");
    c.ammoGaugeAnchor = cfg.GetInt("Metroid.Visual.HudAmmoGaugeAnchor");
    c.ammoGaugePosMode = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosMode");
    c.ammoGaugePosX  = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosX");
    c.ammoGaugePosY  = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosY");
    c.ammoGaugeColor = QColor(cfg.GetInt("Metroid.Visual.HudAmmoGaugeColorR"),
                              cfg.GetInt("Metroid.Visual.HudAmmoGaugeColorG"),
                              cfg.GetInt("Metroid.Visual.HudAmmoGaugeColorB"));
    // Crosshair — general
    c.chColor = QColor(cfg.GetInt("Metroid.Visual.CrosshairColorR"),
                       cfg.GetInt("Metroid.Visual.CrosshairColorG"),
                       cfg.GetInt("Metroid.Visual.CrosshairColorB"));
    c.chOutline          = cfg.GetBool("Metroid.Visual.CrosshairOutline");
    c.chOutlineOpacity   = cfg.GetDouble("Metroid.Visual.CrosshairOutlineOpacity");
    c.chOutlineThickness = cfg.GetInt("Metroid.Visual.CrosshairOutlineThickness");
    if (c.chOutlineThickness <= 0) c.chOutlineThickness = 1;
    c.chCenterDot    = cfg.GetBool("Metroid.Visual.CrosshairCenterDot");
    c.chDotOpacity   = cfg.GetDouble("Metroid.Visual.CrosshairDotOpacity");
    c.chDotThickness = cfg.GetInt("Metroid.Visual.CrosshairDotThickness");
    if (c.chDotThickness <= 0) c.chDotThickness = 1;
    c.chTStyle       = cfg.GetBool("Metroid.Visual.CrosshairTStyle");
    // Inner
    c.chInnerShow    = cfg.GetBool("Metroid.Visual.CrosshairInnerShow");
    c.chInnerOpacity = cfg.GetDouble("Metroid.Visual.CrosshairInnerOpacity");
    c.chInnerLengthX = cfg.GetInt("Metroid.Visual.CrosshairInnerLengthX");
    c.chInnerLengthY = cfg.GetInt("Metroid.Visual.CrosshairInnerLengthY");
    c.chInnerThickness = cfg.GetInt("Metroid.Visual.CrosshairInnerThickness");
    if (c.chInnerThickness <= 0) c.chInnerThickness = 1;
    c.chInnerOffset  = cfg.GetInt("Metroid.Visual.CrosshairInnerOffset");
    // Outer
    c.chOuterShow    = cfg.GetBool("Metroid.Visual.CrosshairOuterShow");
    c.chOuterOpacity = cfg.GetDouble("Metroid.Visual.CrosshairOuterOpacity");
    c.chOuterLengthX = cfg.GetInt("Metroid.Visual.CrosshairOuterLengthX");
    c.chOuterLengthY = cfg.GetInt("Metroid.Visual.CrosshairOuterLengthY");
    c.chOuterThickness = cfg.GetInt("Metroid.Visual.CrosshairOuterThickness");
    if (c.chOuterThickness <= 0) c.chOuterThickness = 1;
    c.chOuterOffset  = cfg.GetInt("Metroid.Visual.CrosshairOuterOffset");
    // Match Status HUD
    c.matchStatusShow     = cfg.GetBool("Metroid.Visual.HudMatchStatusShow");
    c.matchStatusX        = cfg.GetInt("Metroid.Visual.HudMatchStatusX");
    c.matchStatusY        = cfg.GetInt("Metroid.Visual.HudMatchStatusY");
    c.matchStatusLabelOfsX = cfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsX");
    c.matchStatusLabelOfsY = cfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsY");
    c.matchStatusLabelPos  = cfg.GetInt("Metroid.Visual.HudMatchStatusLabelPos");
    // Per-mode label strings
    auto copyLabel = [](char* dst, size_t sz, const std::string& s) {
        std::strncpy(dst, s.c_str(), sz - 1); dst[sz - 1] = '\0';
    };
    copyLabel(c.matchStatusLabelPoints,    sizeof(c.matchStatusLabelPoints),    cfg.GetString("Metroid.Visual.HudMatchStatusLabelPoints"));
    copyLabel(c.matchStatusLabelOctoliths, sizeof(c.matchStatusLabelOctoliths), cfg.GetString("Metroid.Visual.HudMatchStatusLabelOctoliths"));
    copyLabel(c.matchStatusLabelLives,     sizeof(c.matchStatusLabelLives),     cfg.GetString("Metroid.Visual.HudMatchStatusLabelLives"));
    copyLabel(c.matchStatusLabelRingTime,  sizeof(c.matchStatusLabelRingTime),  cfg.GetString("Metroid.Visual.HudMatchStatusLabelRingTime"));
    copyLabel(c.matchStatusLabelPrimeTime, sizeof(c.matchStatusLabelPrimeTime), cfg.GetString("Metroid.Visual.HudMatchStatusLabelPrimeTime"));
    c.matchStatusColor = QColor(cfg.GetInt("Metroid.Visual.HudMatchStatusColorR"),
                           cfg.GetInt("Metroid.Visual.HudMatchStatusColorG"),
                           cfg.GetInt("Metroid.Visual.HudMatchStatusColorB"));
    // Sub-colors: invalid QColor means "use overall matchStatusColor"
    auto readSubColor = [&](const char* keyOverall,
                            const char* keyR, const char* keyG, const char* keyB) -> QColor {
        if (cfg.GetBool(keyOverall)) return QColor(); // invalid = inherit
        return QColor(cfg.GetInt(keyR), cfg.GetInt(keyG), cfg.GetInt(keyB));
    };
    c.matchStatusLabelColor = readSubColor(
        "Metroid.Visual.HudMatchStatusLabelColorOverall",
        "Metroid.Visual.HudMatchStatusLabelColorR",
        "Metroid.Visual.HudMatchStatusLabelColorG",
        "Metroid.Visual.HudMatchStatusLabelColorB");
    c.matchStatusValueColor = readSubColor(
        "Metroid.Visual.HudMatchStatusValueColorOverall",
        "Metroid.Visual.HudMatchStatusValueColorR",
        "Metroid.Visual.HudMatchStatusValueColorG",
        "Metroid.Visual.HudMatchStatusValueColorB");
    c.matchStatusSepColor = readSubColor(
        "Metroid.Visual.HudMatchStatusSepColorOverall",
        "Metroid.Visual.HudMatchStatusSepColorR",
        "Metroid.Visual.HudMatchStatusSepColorG",
        "Metroid.Visual.HudMatchStatusSepColorB");
    c.matchStatusGoalColor = readSubColor(
        "Metroid.Visual.HudMatchStatusGoalColorOverall",
        "Metroid.Visual.HudMatchStatusGoalColorR",
        "Metroid.Visual.HudMatchStatusGoalColorG",
        "Metroid.Visual.HudMatchStatusGoalColorB");
    // Rank & Time HUD
    c.rankShow  = cfg.GetBool("Metroid.Visual.HudRankShow");
    c.rankX     = cfg.GetInt("Metroid.Visual.HudRankX");
    c.rankY     = cfg.GetInt("Metroid.Visual.HudRankY");
    c.rankColor = QColor(cfg.GetInt("Metroid.Visual.HudRankColorR"),
                         cfg.GetInt("Metroid.Visual.HudRankColorG"),
                         cfg.GetInt("Metroid.Visual.HudRankColorB"));
    { auto s = cfg.GetString("Metroid.Visual.HudRankPrefix");
      std::strncpy(c.rankPrefix, s.c_str(), sizeof(c.rankPrefix)-1);
      c.rankPrefix[sizeof(c.rankPrefix)-1] = '\0'; }
    c.rankShowOrdinal = cfg.GetBool("Metroid.Visual.HudRankShowOrdinal");
    { auto s = cfg.GetString("Metroid.Visual.HudRankSuffix");
      std::strncpy(c.rankSuffix, s.c_str(), sizeof(c.rankSuffix)-1);
      c.rankSuffix[sizeof(c.rankSuffix)-1] = '\0'; }
    c.timeLeftShow  = cfg.GetBool("Metroid.Visual.HudTimeLeftShow");
    c.timeLeftX     = cfg.GetInt("Metroid.Visual.HudTimeLeftX");
    c.timeLeftY     = cfg.GetInt("Metroid.Visual.HudTimeLeftY");
    c.timeLeftColor = QColor(cfg.GetInt("Metroid.Visual.HudTimeLeftColorR"),
                              cfg.GetInt("Metroid.Visual.HudTimeLeftColorG"),
                              cfg.GetInt("Metroid.Visual.HudTimeLeftColorB"));
    c.timeLimitShow  = cfg.GetBool("Metroid.Visual.HudTimeLimitShow");
    c.timeLimitX     = cfg.GetInt("Metroid.Visual.HudTimeLimitX");
    c.timeLimitY     = cfg.GetInt("Metroid.Visual.HudTimeLimitY");
    c.timeLimitColor = QColor(cfg.GetInt("Metroid.Visual.HudTimeLimitColorR"),
                               cfg.GetInt("Metroid.Visual.HudTimeLimitColorG"),
                               cfg.GetInt("Metroid.Visual.HudTimeLimitColorB"));
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
    {
        uint32_t ts4 = Read32(ram, rom.battleSettings + 4);
        uint8_t XX = (ts4 >> 8) & 0xFF;
        b.timeLimitMinutes = LookupTimeLimitMin(XX);
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
        uint8_t timeGoalRaw = (timeSetting >> 4) & 0x1F;
        b.goalValue = LookupTimeGoalSec(timeGoalRaw);
        b.isTimeMode = true;
        break;
    }
    }

    b.valid = true;
}

static const char* ResolveMatchStatusLabel(uint8_t mode, const CachedHudConfig& c)
{
    switch (mode) {
    case MODE_BATTLE:
    case MODE_NODES:        return c.matchStatusLabelPoints;
    case MODE_BOUNTY:
    case MODE_CAPTURE:      return c.matchStatusLabelOctoliths;
    case MODE_SURVIVAL:     return c.matchStatusLabelLives;
    case MODE_DEFENDER:     return c.matchStatusLabelRingTime;
    case MODE_PRIME_HUNTER: return c.matchStatusLabelPrimeTime;
    default:                return "";
    }
}

static void DrawMatchStatusHud(QPainter* p, melonDS::u8* ram,
                                const RomAddresses& rom, uint8_t playerPos,
                                bool isAdventure, const CachedHudConfig& c)
{
    if (!c.matchStatusShow || isAdventure) return;

    static TextMeasureCache s_curTextCache  = { 0, "", 0, 0, false };
    static TextMeasureCache s_sepTextCache  = { 0, "", 0, 0, false };
    static TextMeasureCache s_goalTextCache = { 0, "", 0, 0, false };
    const QFontMetrics fm = p->fontMetrics();
    const int fontPixelSize = p->font().pixelSize();

    const BattleMatchState* match = s_battleState.valid ? &s_battleState : nullptr;
    uint8_t mode = match ? match->mode : Read8(ram, rom.battleMode);
    if (mode < MODE_BATTLE || mode > MODE_SURVIVAL) return;

    uint32_t playerOfs = static_cast<uint32_t>(playerPos) * 4;
    int currentValue = 0;
    int goalValue = match ? match->goalValue : 0;
    bool isTimeMode = match ? match->isTimeMode : false;
    uint8_t xx = match ? match->keyXX : 0;

    switch (mode) {
    case MODE_BATTLE:
    case MODE_NODES:
    case MODE_BOUNTY:
    case MODE_CAPTURE:
        currentValue = static_cast<int>(Read32(ram, rom.basePoint + playerOfs));
        break;
    case MODE_SURVIVAL:
        currentValue = static_cast<int>(Read32(ram, rom.basePoint - 0xB0 + playerOfs));
        break;
    case MODE_DEFENDER:
    case MODE_PRIME_HUNTER:
        currentValue = static_cast<int>(Read32(ram, rom.basePoint - 0x180 + playerOfs)) / 60;
        break;
    default:
        return;
    }

    if (!match) {
        uint32_t settings = Read32(ram, rom.battleSettings);
        xx = (settings >> 20) & 0xFE;

        switch (mode) {
        case MODE_BATTLE:
        case MODE_NODES:
        case MODE_BOUNTY:
        case MODE_CAPTURE:
            goalValue = LookupGoal(mode, xx);
            break;
        case MODE_SURVIVAL:
            goalValue = LookupGoal(mode, xx);
            break;
        case MODE_DEFENDER:
        case MODE_PRIME_HUNTER: {
            uint32_t timeSetting = Read32(ram, rom.battleSettings + 4);
            uint8_t timeGoalRaw = (timeSetting >> 4) & 0x1F;
            goalValue = LookupTimeGoalSec(timeGoalRaw);
            isTimeMode = true;
            break;
        }
        default:
            return;
        }
    }

    if (mode == MODE_SURVIVAL && goalValue > 0) {
        currentValue = goalValue - currentValue;
        if (currentValue < 0) currentValue = 0;
    }

    char curBuf[24] = {}, sepBuf[4] = {}, goalBuf[24] = {};
    bool hasGoal = false;
    if (isTimeMode) {
        FormatTime(curBuf, sizeof(curBuf), currentValue);
        FormatTime(goalBuf, sizeof(goalBuf), goalValue);
        std::strncpy(sepBuf, "/", sizeof(sepBuf));
        hasGoal = true;
    } else if (goalValue > 0) {
        std::snprintf(curBuf, sizeof(curBuf), "%d", currentValue);
        std::snprintf(goalBuf, sizeof(goalBuf), "%d", goalValue);
        std::strncpy(sepBuf, " / ", sizeof(sepBuf));
        hasGoal = true;
    } else {
        std::snprintf(curBuf, sizeof(curBuf), "%d (XX=0x%02X)", currentValue, xx);
    }

    auto eff = [&](const QColor& sub) -> const QColor& {
        return sub.isValid() ? sub : c.matchStatusColor;
    };

    int vx = c.matchStatusX;
    int vy = c.matchStatusY;
    int curW = 0, curH = 0;
    MeasureTextCached(fm, fontPixelSize, s_curTextCache, curBuf, curW, curH);

    static TextBitmapCache s_curBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() };
    static TextBitmapCache s_sepBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() };
    static TextBitmapCache s_goalBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() };
    static TextBitmapCache s_labelBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() };

    PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_curBitmapCache, curBuf, eff(c.matchStatusValueColor));
    DrawCachedText(p, s_curBitmapCache, vx, vy);

    if (hasGoal) {
        int sepW = 0, sepH = 0, goalW = 0, goalH = 0;
        MeasureTextCached(fm, fontPixelSize, s_sepTextCache, sepBuf, sepW, sepH);
        MeasureTextCached(fm, fontPixelSize, s_goalTextCache, goalBuf, goalW, goalH);

        int x = vx + curW;
        PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_sepBitmapCache, sepBuf, eff(c.matchStatusSepColor));
        DrawCachedText(p, s_sepBitmapCache, x, vy);
        x += sepW;
        PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_goalBitmapCache, goalBuf, eff(c.matchStatusGoalColor));
        DrawCachedText(p, s_goalBitmapCache, x, vy);
    }

    const char* label = ResolveMatchStatusLabel(mode, c);
    if (label[0] == '\0') return;

    int lx, ly;
    switch (c.matchStatusLabelPos) {
    default:
    case 0:
        lx = vx;
        ly = vy - 10;
        break;
    case 1:
        lx = vx;
        ly = vy + 10;
        break;
    case 2:
        lx = vx - 50;
        ly = vy;
        break;
    case 3:
        lx = vx + 50;
        ly = vy;
        break;
    case 4:
        lx = vx;
        ly = vy;
        break;
    }
    lx += c.matchStatusLabelOfsX;
    ly += c.matchStatusLabelOfsY;

    PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_labelBitmapCache, label, eff(c.matchStatusLabelColor));
    DrawCachedText(p, s_labelBitmapCache, lx, ly);
}

// =========================================================================
//  Rank & Time HUD
// =========================================================================
static void DrawRankAndTime(QPainter* p, melonDS::u8* ram,
                             const RomAddresses& rom, uint8_t playerPos,
                             bool isAdventure, const CachedHudConfig& c)
{
    if (isAdventure) return;
    const QFontMetrics fm = p->fontMetrics();
    const int fontPixelSize = p->font().pixelSize();

    // Rank (e.g. "1st" / "2nd" / "3rd" / "4th", with configurable prefix/suffix/ordinal)
    if (c.rankShow) {
        static const char* kOrdinals[4] = { "st", "nd", "rd", "th" };
        static TextBitmapCache s_rankCache = { 0, QColor(), "", 0, 0, false, QImage() };
        uint32_t rankWord = Read32(ram, rom.matchRank);
        uint8_t rankByte = (rankWord >> (playerPos * 8)) & 0xFF;
        if (rankByte <= 3) {
            char rankBuf[64] = {};
            if (c.rankShowOrdinal)
                snprintf(rankBuf, sizeof(rankBuf), "%s%u%s%s", c.rankPrefix, rankByte + 1u, kOrdinals[rankByte], c.rankSuffix);
            else
                snprintf(rankBuf, sizeof(rankBuf), "%s%u%s", c.rankPrefix, rankByte + 1u, c.rankSuffix);
            PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_rankCache, rankBuf, c.rankColor);
            DrawCachedText(p, s_rankCache, c.rankX, c.rankY);
        }
    }

    // Time Left (raw u32 / 60 = seconds)
    if (c.timeLeftShow) {
        static TextBitmapCache s_timeLeftCache = { 0, QColor(), "", 0, 0, false, QImage() };
        uint32_t raw = Read32(ram, rom.timeLeft);
        int seconds = static_cast<int>(raw) / 60;
        char buf[16] = {};
        FormatTime(buf, sizeof(buf), seconds);
        PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_timeLeftCache, buf, c.timeLeftColor);
        DrawCachedText(p, s_timeLeftCache, c.timeLeftX, c.timeLeftY);
    }

    // Time Limit displays the match time limit in minutes with :00 fixed.
    if (c.timeLimitShow) {
        static TextBitmapCache s_timeLimitCache = { 0, QColor(), "", 0, 0, false, QImage() };
        int goalMinutes = 0;
        if (s_battleState.valid) {
            goalMinutes = s_battleState.timeLimitMinutes;
        } else {
            uint32_t ts4 = Read32(ram, rom.battleSettings + 4);
            uint8_t XX = (ts4 >> 8) & 0xFF;
            goalMinutes = LookupTimeLimitMin(XX);
        }
        char buf[16] = {};
        FormatMinuteTime(buf, sizeof(buf), goalMinutes);
        PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_timeLimitCache, buf, c.timeLimitColor);
        DrawCachedText(p, s_timeLimitCache, c.timeLimitX, c.timeLimitY);
    }
}

// =========================================================================
//  Config key (only for IsEnabled — hot path uses s_cache)
// =========================================================================
static constexpr const char* kCfgCustomHud = "Metroid.Visual.CustomHUD";

bool CustomHud_IsEnabled(Config::Table& localCfg)
{
    return localCfg.GetBool(kCfgCustomHud);
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
    const QColor hpTextColor = c.hpTextAutoColor ? HpGaugeColor(hp, c.hpTextColor) : c.hpTextColor;

    static TextMeasureCache s_hpTextCache = { 0, "", 0, 0, false };
    static TextBitmapCache s_hpBitmapCache = { 0, QColor(), "", 0, 0, false, QImage() };
    const QFontMetrics fm = p->fontMetrics();
    const int fontPixelSize = p->font().pixelSize();

    char buf[24];
    std::snprintf(buf, sizeof(buf), "%s%u", c.hpPrefix, hp);
    int textW = 0, textH = 0;
    MeasureTextCached(fm, fontPixelSize, s_hpTextCache, buf, textW, textH);
    const int textX = CalcAlignedTextX(c.hpX, c.hpAlign, textW);
    PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_hpBitmapCache, buf, hpTextColor);
    DrawCachedText(p, s_hpBitmapCache, textX, c.hpY);

    if (c.hpGauge && maxHP > 0) {
        float ratio = static_cast<float>(hp) / static_cast<float>(maxHP);
        QColor gc = c.hpAutoColor ? HpGaugeColor(hp, c.hpGaugeColor) : c.hpGaugeColor;
        int gx, gy;
        if (c.hpGaugePosMode == 1) {
            gx = c.hpGaugePosX;
            gy = c.hpGaugePosY;
        } else {
            CalcGaugePos(textX, c.hpY, textW, textH, c.hpGaugeAnchor, c.hpGaugeOfsX, c.hpGaugeOfsY,
                         c.hpGaugeLen, c.hpGaugeWid, c.hpGaugeOri, gx, gy);
        }
        DrawGauge(p, gx, gy, ratio, gc, c.hpGaugeOri, c.hpGaugeLen, c.hpGaugeWid);
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
    const QImage& icon = GetWeaponIconForDraw(weapon, c.iconColorOverlay, c.ammoGaugeColor); // P-1

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

    int textX = c.wpnX, textY = c.wpnY;
    int textW = 0, textH = fm.height();
    if (hasAmmo) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%s%02u", c.ammoPrefix, ammo);
        MeasureTextCached(fm, fontPixelSize, s_ammoTextCache, buf, textW, textH);
        textX = CalcAlignedTextX(c.wpnX, c.ammoAlign, textW);
        PrepareTextBitmapCached(fm, p->font(), fontPixelSize, s_ammoBitmapCache, buf, c.ammoTextColor);
        DrawCachedText(p, s_ammoBitmapCache, textX, textY);
    }

    if (c.iconShow && !icon.isNull()) {
        int ix = (c.iconMode == 0) ? c.wpnX + c.iconOfsX : c.iconPosX;
        int iy = (c.iconMode == 0) ? c.wpnY + c.iconOfsY : c.iconPosY;
        if (c.iconAnchorX == 1) ix -= icon.width() / 2;
        else if (c.iconAnchorX == 2) ix -= icon.width();
        if (c.iconAnchorY == 1) iy -= icon.height() / 2;
        else if (c.iconAnchorY == 2) iy -= icon.height();
        p->drawImage(QPoint(ix, iy), icon);
    }

    if (c.ammoGauge && hasAmmo && maxAmmo > 0) {
        float ratio = static_cast<float>(ammo) / static_cast<float>(maxAmmo);
        int gx, gy;
        if (c.ammoGaugePosMode == 1) {
            gx = c.ammoGaugePosX;
            gy = c.ammoGaugePosY;
        } else {
            CalcGaugePos(textX, textY, textW, textH, c.ammoGaugeAnchor, c.ammoGaugeOfsX, c.ammoGaugeOfsY,
                         c.ammoGaugeLen, c.ammoGaugeWid, c.ammoGaugeOri, gx, gy);
        }
        DrawGauge(p, gx, gy, ratio, c.ammoGaugeColor, c.ammoGaugeOri, c.ammoGaugeLen, c.ammoGaugeWid);
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
    if (c.chInnerShow)
        nInner = CollectArms(innerArms, cx, cy, c.chInnerLengthX, c.chInnerLengthY, c.chInnerOffset, c.chTStyle);
    if (c.chOuterShow)
        nOuter = CollectArms(outerArms, cx, cy, c.chOuterLengthX, c.chOuterLengthY, c.chOuterOffset, c.chTStyle);

    int dotHalf = c.chDotThickness / 2;

    if (c.chOutline && c.chOutlineOpacity > 0.0) {
        int minX = cx, maxX = cx, minY = cy, maxY = cy;
        auto expandBounds = [&](int x, int y, int pad) {
            if (x - pad < minX) minX = x - pad;
            if (x + pad > maxX) maxX = x + pad;
            if (y - pad < minY) minY = y - pad;
            if (y + pad > maxY) maxY = y + pad;
        };

        if (c.chCenterDot) {
            expandBounds(cx, cy, dotHalf + c.chOutlineThickness);
        }
        for (int i = 0; i < nInner; i++) {
            int pad = (c.chInnerThickness + c.chOutlineThickness * 2 + 1) / 2;
            expandBounds(innerArms[i].x1, innerArms[i].y1, pad);
            expandBounds(innerArms[i].x2, innerArms[i].y2, pad);
        }
        for (int i = 0; i < nOuter; i++) {
            int pad = (c.chOuterThickness + c.chOutlineThickness * 2 + 1) / 2;
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

            if (c.chCenterDot) {
                olP.setPen(Qt::NoPen);
                olP.setBrush(solidBlack);
                int oh = dotHalf + c.chOutlineThickness;
                olP.drawRect(cx - oh, cy - oh, oh * 2 + 1, oh * 2 + 1);
                olP.setBrush(Qt::NoBrush);
            }
            if (nInner > 0) {
                QPen pen(solidBlack);
                pen.setWidth(c.chInnerThickness + c.chOutlineThickness * 2);
                olP.setPen(pen);
                for (int i = 0; i < nInner; i++)
                    olP.drawLine(innerArms[i].x1, innerArms[i].y1, innerArms[i].x2, innerArms[i].y2);
            }
            if (nOuter > 0) {
                QPen pen(solidBlack);
                pen.setWidth(c.chOuterThickness + c.chOutlineThickness * 2);
                olP.setPen(pen);
                for (int i = 0; i < nOuter; i++)
                    olP.drawLine(outerArms[i].x1, outerArms[i].y1, outerArms[i].x2, outerArms[i].y2);
            }
        }
        p->setOpacity(c.chOutlineOpacity);
        p->drawImage(dirtyRect.topLeft(), olBuf, dirtyRect);
        p->setOpacity(1.0);
    }

    if (c.chCenterDot) {
        p->setPen(Qt::NoPen);
        QColor dotColor = c.chColor;
        dotColor.setAlphaF(c.chDotOpacity);
        p->setBrush(dotColor);
        p->drawRect(cx - dotHalf, cy - dotHalf, dotHalf * 2 + 1, dotHalf * 2 + 1);
        p->setBrush(Qt::NoBrush);
    }
    if (nInner > 0) {
        QColor clr = c.chColor; clr.setAlphaF(c.chInnerOpacity);
        QPen pen(clr); pen.setWidth(c.chInnerThickness); p->setPen(pen);
        for (int i = 0; i < nInner; i++)
            p->drawLine(innerArms[i].x1, innerArms[i].y1, innerArms[i].x2, innerArms[i].y2);
    }

    if (nOuter > 0) {
        QColor clr = c.chColor; clr.setAlphaF(c.chOuterOpacity);
        QPen pen(clr); pen.setWidth(c.chOuterThickness); p->setPen(pen);
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
    bool isDead        = (currentHP == 0);
    bool isGameOver    = Read8(ram, rom.gameOver) != 0x00;
    uint8_t viewMode   = Read8(ram, rom.baseViewMode + offP);
    bool isFirstPerson = (viewMode == 0x00);

    if (isStartPressed || isDead || isGameOver) return;

    if (topPaint->font().pixelSize() != kCustomHudFontSize) {
        QFont f = topPaint->font();
        f.setPixelSize(kCustomHudFontSize);
        topPaint->setFont(f);
    }

    DrawHP(topPaint, currentHP, maxHP, c);

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

    bool isAlt   = Read8(ram, addrHot.isAltForm) == 0x02;
    bool isTrans = (Read8(ram, addrHot.jumpFlag) & 0x10) != 0;
    if (!isTrans && !isAlt)
        DrawCrosshair(topPaint, ram, rom, c, topStretchX);

    // Draw bottom screen overlay on top screen
    const uint8_t hunterID = Read8(ram, addrHot.chosenHunter);
    DrawBottomScreenOverlay(localCfg, topPaint, btmBuffer, (hunterID <= 6) ? hunterID : 0);
}

void DrawBottomScreenOverlay(Config::Table& localCfg, QPainter* topPaint, QImage* btmBuffer, uint8_t hunterID)
{
    if (!localCfg.GetBool("Metroid.Visual.BtmOverlayEnable")) return;
    if (!topPaint || !btmBuffer || btmBuffer->isNull()) return;

    int dstX = localCfg.GetInt("Metroid.Visual.BtmOverlayDstX");
    int dstY = localCfg.GetInt("Metroid.Visual.BtmOverlayDstY");
    int dstSize = std::max(localCfg.GetInt("Metroid.Visual.BtmOverlayDstSize"), 1);
    double opacity = localCfg.GetDouble("Metroid.Visual.BtmOverlayOpacity");

    const int srcCenterX  = kBtmOverlaySrcCenterX;
    const int srcCenterY  = kBtmOverlaySrcCenterY[hunterID];
    const int srcRadius   = std::max(localCfg.GetInt("Metroid.Visual.BtmOverlaySrcRadius"), 1);
    const float bufScaleX = static_cast<float>(btmBuffer->width()) / 256.0f;
    const float bufScaleY = static_cast<float>(btmBuffer->height()) / 192.0f;

    QRect srcRect(static_cast<int>((srcCenterX - srcRadius) * bufScaleX),
                  static_cast<int>((srcCenterY - srcRadius) * bufScaleY),
                  static_cast<int>(srcRadius * 2 * bufScaleX),
                  static_cast<int>(srcRadius * 2 * bufScaleY));
    QRect dstRect(dstX, dstY, dstSize, dstSize);

    topPaint->save();
    topPaint->setRenderHint(QPainter::SmoothPixmapTransform, true);
    topPaint->setOpacity(std::clamp(opacity, 0.0, 1.0));

    // Clip to circle
    QPainterPath circlePath;
    circlePath.addEllipse(dstRect);
    topPaint->setClipPath(circlePath);

    topPaint->drawImage(dstRect, *btmBuffer, srcRect);
    topPaint->restore();
}

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD


