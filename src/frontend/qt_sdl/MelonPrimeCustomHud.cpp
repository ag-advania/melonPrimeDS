#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeCustomHud.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeCompilerHints.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "Config.h"

#include <QPainter>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <string>
#include <cmath>
#include <cstdio>

namespace MelonPrime {

// =========================================================================
//  P-1: Static icon cache — loaded once, reused every frame.
//       Eliminates QImage resource I/O + format conversion from hot path.
// =========================================================================
static QImage s_weaponIcons[9];
static bool   s_iconsLoaded = false;

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
    }
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

// =========================================================================
//  P-3: Cached HUD config — refreshed only when config generation changes.
//       Avoids ~50 hash-map lookups per frame → single generation compare.
// =========================================================================
struct CachedHudConfig {
    // HP
    int    hpX, hpY, hudFontSize;
    char   hpPrefix[12];
    bool   hpGauge, hpAutoColor;
    int    hpGaugeOri, hpGaugeLen, hpGaugeWid;
    int    hpGaugeOfsX, hpGaugeOfsY, hpGaugeAnchor;
    QColor hpGaugeColor;
    // Weapon / Ammo
    int    wpnX, wpnY;
    char   ammoPrefix[12];
    bool   iconShow, ammoGauge;
    int    iconMode, iconOfsX, iconOfsY, iconPosX, iconPosY;
    int    ammoGaugeOri, ammoGaugeLen, ammoGaugeWid;
    int    ammoGaugeOfsX, ammoGaugeOfsY, ammoGaugeAnchor;
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
    c.hudFontSize = cfg.GetInt("Metroid.Visual.HudFontSize");
    { auto s = cfg.GetString("Metroid.Visual.HudHpPrefix");
      std::strncpy(c.hpPrefix, s.c_str(), sizeof(c.hpPrefix)-1);
      c.hpPrefix[sizeof(c.hpPrefix)-1] = '\0'; }
    c.hpGauge      = cfg.GetBool("Metroid.Visual.HudHpGauge");
    c.hpGaugeOri   = cfg.GetInt("Metroid.Visual.HudHpGaugeOrientation");
    c.hpGaugeLen   = cfg.GetInt("Metroid.Visual.HudHpGaugeLength");
    c.hpGaugeWid   = cfg.GetInt("Metroid.Visual.HudHpGaugeWidth");
    c.hpGaugeOfsX  = cfg.GetInt("Metroid.Visual.HudHpGaugeOffsetX");
    c.hpGaugeOfsY  = cfg.GetInt("Metroid.Visual.HudHpGaugeOffsetY");
    c.hpGaugeAnchor = cfg.GetInt("Metroid.Visual.HudHpGaugeAnchor");
    c.hpAutoColor  = cfg.GetBool("Metroid.Visual.HudHpGaugeAutoColor");
    c.hpGaugeColor = QColor(cfg.GetInt("Metroid.Visual.HudHpGaugeColorR"),
                            cfg.GetInt("Metroid.Visual.HudHpGaugeColorG"),
                            cfg.GetInt("Metroid.Visual.HudHpGaugeColorB"));
    // Weapon / Ammo
    c.wpnX = cfg.GetInt("Metroid.Visual.HudWeaponX");
    c.wpnY = cfg.GetInt("Metroid.Visual.HudWeaponY");
    { auto s = cfg.GetString("Metroid.Visual.HudAmmoPrefix");
      std::strncpy(c.ammoPrefix, s.c_str(), sizeof(c.ammoPrefix)-1);
      c.ammoPrefix[sizeof(c.ammoPrefix)-1] = '\0'; }
    c.iconShow = cfg.GetBool("Metroid.Visual.HudWeaponIconShow");
    c.iconMode = cfg.GetInt("Metroid.Visual.HudWeaponIconMode");
    c.iconOfsX = cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetX");
    c.iconOfsY = cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetY");
    c.iconPosX = cfg.GetInt("Metroid.Visual.HudWeaponIconPosX");
    c.iconPosY = cfg.GetInt("Metroid.Visual.HudWeaponIconPosY");
    c.ammoGauge     = cfg.GetBool("Metroid.Visual.HudAmmoGauge");
    c.ammoGaugeOri  = cfg.GetInt("Metroid.Visual.HudAmmoGaugeOrientation");
    c.ammoGaugeLen  = cfg.GetInt("Metroid.Visual.HudAmmoGaugeLength");
    c.ammoGaugeWid  = cfg.GetInt("Metroid.Visual.HudAmmoGaugeWidth");
    c.ammoGaugeOfsX = cfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetX");
    c.ammoGaugeOfsY = cfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetY");
    c.ammoGaugeAnchor = cfg.GetInt("Metroid.Visual.HudAmmoGaugeAnchor");
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

static inline QColor HpGaugeColor(uint16_t hp)
{
    if (hp <= 25)      return QColor(255, 0, 0);
    else if (hp <= 50) return QColor(255, 165, 0);
    else               return QColor(56, 192, 8);
}

static void CalcGaugePos(int textX, int textY, int anchor,
                         int ofsX, int ofsY, int gaugeLen, int gaugeWid, int ori,
                         int& outX, int& outY)
{
    constexpr int kTextW = 30, kTextH = 8;
    switch (anchor) {
    case 0: outX = textX + ofsX;           outY = textY + 2 + ofsY; break;
    case 1: outX = textX + ofsX;           outY = textY - kTextH - (ori==0?gaugeWid:gaugeLen) + ofsY; break;
    case 2: outX = textX + kTextW + ofsX;  outY = textY - kTextH/2 - (ori==0?gaugeWid:gaugeLen)/2 + ofsY; break;
    case 3: outX = textX - (ori==0?gaugeLen:gaugeWid) + ofsX; outY = textY - kTextH/2 - (ori==0?gaugeWid:gaugeLen)/2 + ofsY; break;
    case 4: outX = textX + kTextW/2 - (ori==0?gaugeLen:gaugeWid)/2 + ofsX; outY = textY - kTextH/2 - (ori==0?gaugeWid:gaugeLen)/2 + ofsY; break;
    default: outX = textX + ofsX;          outY = textY + 2 + ofsY; break;
    }
}

// =========================================================================
//  P-5: DrawHP — stack-buffer text, reads CachedHudConfig directly
// =========================================================================
static inline void DrawHP(QPainter* p, uint16_t hp, uint16_t maxHP,
                           const CachedHudConfig& c)
{
    if (hp <= 25)       p->setPen(QColor(255, 0, 0));
    else if (hp <= 50)  p->setPen(QColor(255, 165, 0));
    else                p->setPen(QColor(255, 255, 255));

    char buf[24];
    std::snprintf(buf, sizeof(buf), "%s%u", c.hpPrefix, hp);
    p->drawText(QPoint(c.hpX, c.hpY), buf);

    if (c.hpGauge && maxHP > 0) {
        float ratio = static_cast<float>(hp) / static_cast<float>(maxHP);
        QColor gc = c.hpAutoColor ? HpGaugeColor(hp) : c.hpGaugeColor;
        int gx, gy;
        CalcGaugePos(c.hpX, c.hpY, c.hpGaugeAnchor, c.hpGaugeOfsX, c.hpGaugeOfsY,
                     c.hpGaugeLen, c.hpGaugeWid, c.hpGaugeOri, gx, gy);
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
// =========================================================================
static void DrawWeaponAmmo(QPainter* p, melonDS::u8* ram,
                           uint8_t weapon, uint16_t ammoSpecial, uint32_t addrMissile,
                           uint16_t maxAmmoSpecial, uint16_t maxAmmoMissile,
                           const CachedHudConfig& c)
{
    if (weapon > 8) return;
    p->setPen(Qt::white);

    const WeaponInfo& wi = kWeaponTable[weapon];
    const QImage& icon = s_weaponIcons[weapon]; // P-1

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

    int textX = c.wpnX, textY = c.wpnY + 8;
    if (hasAmmo) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%s%02d", c.ammoPrefix, ammo);
        p->drawText(QPoint(textX, textY), buf);
    }

    if (c.iconShow && !icon.isNull()) {
        if (c.iconMode == 0)
            p->drawImage(QPoint(c.wpnX + c.iconOfsX, c.wpnY + c.iconOfsY), icon);
        else
            p->drawImage(QPoint(c.iconPosX, c.iconPosY), icon);
    }

    if (c.ammoGauge && hasAmmo && maxAmmo > 0) {
        float ratio = static_cast<float>(ammo) / static_cast<float>(maxAmmo);
        int gx, gy;
        CalcGaugePos(textX, textY, c.ammoGaugeAnchor, c.ammoGaugeOfsX, c.ammoGaugeOfsY,
                     c.ammoGaugeLen, c.ammoGaugeWid, c.ammoGaugeOri, gx, gy);
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

    // === Pass 1: Outline (reused buffer) ===
    if (c.chOutline && c.chOutlineOpacity > 0.0) {
        QImage& olBuf = GetOutlineBuffer();
        olBuf.fill(Qt::transparent);
        {
            QPainter olP(&olBuf);
            olP.setRenderHint(QPainter::Antialiasing, false);
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
        p->drawImage(0, 0, olBuf);
        p->setOpacity(1.0);
    }

    // === Pass 2: Center dot ===
    if (c.chCenterDot) {
        p->setPen(Qt::NoPen);
        QColor dotColor = c.chColor;
        dotColor.setAlphaF(c.chDotOpacity);
        p->setBrush(dotColor);
        p->drawRect(cx - dotHalf, cy - dotHalf, dotHalf * 2 + 1, dotHalf * 2 + 1);
        p->setBrush(Qt::NoBrush);
    }

    // === Pass 3: Inner fills (one setPen) ===
    if (nInner > 0) {
        QColor clr = c.chColor; clr.setAlphaF(c.chInnerOpacity);
        QPen pen(clr); pen.setWidth(c.chInnerThickness); p->setPen(pen);
        for (int i = 0; i < nInner; i++)
            p->drawLine(innerArms[i].x1, innerArms[i].y1, innerArms[i].x2, innerArms[i].y2);
    }

    // === Pass 4: Outer fills (one setPen) ===
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

    if (c.hudFontSize > 0) {
        QFont f = topPaint->font();
        f.setPixelSize(c.hudFontSize);
        topPaint->setFont(f);
    }

    DrawHP(topPaint, currentHP, maxHP, c);

    if (!isFirstPerson) return;

    uint8_t currentWeapon = Read8(ram, addrHot.currentWeapon);
    DrawWeaponAmmo(topPaint, ram, currentWeapon,
                   Read16(ram, addrAmmoSpecial), addrAmmoMissile,
                   maxAmmoSpecial, maxAmmoMissile, c);

    bool isAlt   = Read8(ram, addrHot.isAltForm) == 0x02;
    bool isTrans = (Read8(ram, addrHot.jumpFlag) & 0x10) != 0;
    if (!isTrans && !isAlt)
        DrawCrosshair(topPaint, ram, rom, c, topStretchX);
}

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
