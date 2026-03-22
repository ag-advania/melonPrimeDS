#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeCustomHud.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeGameRomAddrTable.h"
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

// Remap X coordinate from DS center (128) for widescreen stretch
static inline int RemapXFromCenter(int x, float stretchX)
{
    constexpr float kCenterX = 128.0f;
    return static_cast<int>(std::lround(kCenterX + (static_cast<float>(x) - kCenterX) * stretchX));
}

// =========================================================================
//  Config keys
// =========================================================================
static constexpr const char* kCfgCustomHud = "Metroid.Visual.CustomHUD";

// Crosshair — General
static constexpr const char* kCfgChColorR           = "Metroid.Visual.CrosshairColorR";

// HUD element positions
static constexpr const char* kCfgHudHpX       = "Metroid.Visual.HudHpX";
static constexpr const char* kCfgHudHpY       = "Metroid.Visual.HudHpY";
static constexpr const char* kCfgHudFontSize  = "Metroid.Visual.HudFontSize";
static constexpr const char* kCfgHudHpPrefix  = "Metroid.Visual.HudHpPrefix";
static constexpr const char* kCfgHudWeaponX   = "Metroid.Visual.HudWeaponX";
static constexpr const char* kCfgHudWeaponY   = "Metroid.Visual.HudWeaponY";
static constexpr const char* kCfgHudAmmoPrefix = "Metroid.Visual.HudAmmoPrefix";
static constexpr const char* kCfgHudWeaponIconShow    = "Metroid.Visual.HudWeaponIconShow";
static constexpr const char* kCfgHudWeaponIconMode    = "Metroid.Visual.HudWeaponIconMode"; // 0=offset, 1=independent
static constexpr const char* kCfgHudWeaponIconOffsetX = "Metroid.Visual.HudWeaponIconOffsetX";
static constexpr const char* kCfgHudWeaponIconOffsetY = "Metroid.Visual.HudWeaponIconOffsetY";
static constexpr const char* kCfgHudWeaponIconPosX    = "Metroid.Visual.HudWeaponIconPosX";
static constexpr const char* kCfgHudWeaponIconPosY    = "Metroid.Visual.HudWeaponIconPosY";

// Gauge settings
static constexpr const char* kCfgHudHpGauge            = "Metroid.Visual.HudHpGauge";
static constexpr const char* kCfgHudHpGaugeOrientation = "Metroid.Visual.HudHpGaugeOrientation";
static constexpr const char* kCfgHudHpGaugeLength      = "Metroid.Visual.HudHpGaugeLength";
static constexpr const char* kCfgHudHpGaugeWidth       = "Metroid.Visual.HudHpGaugeWidth";
static constexpr const char* kCfgHudHpGaugeOffsetX     = "Metroid.Visual.HudHpGaugeOffsetX";
static constexpr const char* kCfgHudHpGaugeOffsetY     = "Metroid.Visual.HudHpGaugeOffsetY";
static constexpr const char* kCfgHudHpGaugeAutoColor   = "Metroid.Visual.HudHpGaugeAutoColor";
static constexpr const char* kCfgHudHpGaugeColorR      = "Metroid.Visual.HudHpGaugeColorR";
static constexpr const char* kCfgHudHpGaugeColorG      = "Metroid.Visual.HudHpGaugeColorG";
static constexpr const char* kCfgHudHpGaugeColorB      = "Metroid.Visual.HudHpGaugeColorB";
static constexpr const char* kCfgHudHpGaugeAnchor      = "Metroid.Visual.HudHpGaugeAnchor"; // 0=below, 1=above, 2=right, 3=left

static constexpr const char* kCfgHudAmmoGauge            = "Metroid.Visual.HudAmmoGauge";
static constexpr const char* kCfgHudAmmoGaugeOrientation = "Metroid.Visual.HudAmmoGaugeOrientation";
static constexpr const char* kCfgHudAmmoGaugeLength      = "Metroid.Visual.HudAmmoGaugeLength";
static constexpr const char* kCfgHudAmmoGaugeWidth       = "Metroid.Visual.HudAmmoGaugeWidth";
static constexpr const char* kCfgHudAmmoGaugeOffsetX     = "Metroid.Visual.HudAmmoGaugeOffsetX";
static constexpr const char* kCfgHudAmmoGaugeOffsetY     = "Metroid.Visual.HudAmmoGaugeOffsetY";
static constexpr const char* kCfgHudAmmoGaugeColorR      = "Metroid.Visual.HudAmmoGaugeColorR";
static constexpr const char* kCfgHudAmmoGaugeColorG      = "Metroid.Visual.HudAmmoGaugeColorG";
static constexpr const char* kCfgHudAmmoGaugeColorB      = "Metroid.Visual.HudAmmoGaugeColorB";
static constexpr const char* kCfgHudAmmoGaugeAnchor      = "Metroid.Visual.HudAmmoGaugeAnchor";

// Crosshair — General (continued)
static constexpr const char* kCfgChColorG           = "Metroid.Visual.CrosshairColorG";
static constexpr const char* kCfgChColorB           = "Metroid.Visual.CrosshairColorB";
static constexpr const char* kCfgChOutline          = "Metroid.Visual.CrosshairOutline";
static constexpr const char* kCfgChOutlineOpacity   = "Metroid.Visual.CrosshairOutlineOpacity";
static constexpr const char* kCfgChOutlineThickness = "Metroid.Visual.CrosshairOutlineThickness";
static constexpr const char* kCfgChCenterDot        = "Metroid.Visual.CrosshairCenterDot";
static constexpr const char* kCfgChDotOpacity       = "Metroid.Visual.CrosshairDotOpacity";
static constexpr const char* kCfgChDotThickness     = "Metroid.Visual.CrosshairDotThickness";
static constexpr const char* kCfgChTStyle           = "Metroid.Visual.CrosshairTStyle";

// Crosshair — Inner Lines
static constexpr const char* kCfgChInnerShow      = "Metroid.Visual.CrosshairInnerShow";
static constexpr const char* kCfgChInnerOpacity   = "Metroid.Visual.CrosshairInnerOpacity";
static constexpr const char* kCfgChInnerLengthX   = "Metroid.Visual.CrosshairInnerLengthX";
static constexpr const char* kCfgChInnerLengthY   = "Metroid.Visual.CrosshairInnerLengthY";
static constexpr const char* kCfgChInnerThickness = "Metroid.Visual.CrosshairInnerThickness";
static constexpr const char* kCfgChInnerOffset    = "Metroid.Visual.CrosshairInnerOffset";

// Crosshair — Outer Lines
static constexpr const char* kCfgChOuterShow      = "Metroid.Visual.CrosshairOuterShow";
static constexpr const char* kCfgChOuterOpacity   = "Metroid.Visual.CrosshairOuterOpacity";
static constexpr const char* kCfgChOuterLengthX   = "Metroid.Visual.CrosshairOuterLengthX";
static constexpr const char* kCfgChOuterLengthY   = "Metroid.Visual.CrosshairOuterLengthY";
static constexpr const char* kCfgChOuterThickness = "Metroid.Visual.CrosshairOuterThickness";
static constexpr const char* kCfgChOuterOffset    = "Metroid.Visual.CrosshairOuterOffset";

bool CustomHud_IsEnabled(Config::Table& localCfg)
{
    return localCfg.GetBool(kCfgCustomHud);
}

// =========================================================================
//  NoHUD Patch Data — per ROM version
//
//  17 ARM instructions are NOP'd (E1A00000) to disable the game's HUD
//  rendering functions. Restore writes back the original STR instructions.
//  The 18th entry (addrHudToggle) is handled separately via Write8.
// =========================================================================
static constexpr int NOHUD_PATCH_COUNT = 17;
static constexpr uint32_t ARM_NOP = 0xE1A00000;

struct HudPatchEntry {
    uint32_t addr;
    uint32_t restoreValue;
};

// Index matches RomGroup enum: JP1.0=0, JP1.1=1, US1.0=2, US1.1=3, EU1.0=4, EU1.1=5, KR1.0=6
static constexpr HudPatchEntry kHudPatch[7][NOHUD_PATCH_COUNT] = {
    // JP1.0
    { {0x02008E78,0xE5840018},{0x02008F0C,0xE5840018},{0x0202A494,0xE5841000},{0x0202A554,0xE584C000},
      {0x0202A5FC,0xE584C000},{0x0202A6AC,0xE5840000},{0x0202A6B4,0xE5840000},{0x0202F7B4,0xE5801000},
      {0x0202F814,0xE5801000},{0x0202F870,0xE5801000},{0x0202F938,0xE5823000},{0x02030E58,0xE5812000},
      {0x020311F8,0xE5801000},{0x020565F8,0xE5801000},{0x020568C4,0xE5801000},{0x02058C50,0xE5801000},
      {0x0205A958,0xE5813000} },
    // JP1.1
    { {0x02008E78,0xE5840018},{0x02008F0C,0xE5840018},{0x0202A494,0xE5841000},{0x0202A554,0xE584C000},
      {0x0202A5FC,0xE584C000},{0x0202A6AC,0xE5840000},{0x0202A6B4,0xE5840000},{0x0202F7B4,0xE5801000},
      {0x0202F814,0xE5801000},{0x0202F870,0xE5801000},{0x0202F938,0xE5823000},{0x02030E58,0xE5812000},
      {0x020311F8,0xE5801000},{0x020565F8,0xE5801000},{0x020568C4,0xE5801000},{0x02058C50,0xE5801000},
      {0x0205A958,0xE5813000} },
    // US1.0
    { {0x02008E78,0xE5840018},{0x02008F0C,0xE5840018},{0x0202A4B8,0xE5841000},{0x0202A578,0xE584C000},
      {0x0202A620,0xE584C000},{0x0202A6D0,0xE5840000},{0x0202A6D8,0xE5840000},{0x0202F79C,0xE5801000},
      {0x0202F7FC,0xE5801000},{0x0202F858,0xE5801000},{0x0202F920,0xE5823000},{0x02030E40,0xE5812000},
      {0x0203111C,0xE5801000},{0x02054BA8,0xE5801000},{0x02054E74,0xE5801000},{0x020571AC,0xE5801000},
      {0x02058D20,0xE5813000} },
    // US1.1
    { {0x02008E78,0xE5840018},{0x02008F0C,0xE5840018},{0x0202A4B8,0xE5841000},{0x0202A578,0xE584C000},
      {0x0202A620,0xE584C000},{0x0202A6D0,0xE5840000},{0x0202A6D8,0xE5840000},{0x0202F76C,0xE5801000},
      {0x0202F7CC,0xE5801000},{0x0202F828,0xE5801000},{0x0202F8F0,0xE5823000},{0x02030E0C,0xE5812000},
      {0x020310E8,0xE5801000},{0x020553C8,0xE5801000},{0x02055694,0xE5801000},{0x020579C0,0xE5801000},
      {0x02059534,0xE5813000} },
    // EU1.0
    { {0x02008E7C,0xE5840018},{0x02008F10,0xE5840018},{0x0202A4B0,0xE5841000},{0x0202A570,0xE584C000},
      {0x0202A618,0xE584C000},{0x0202A6C8,0xE5840000},{0x0202A6D0,0xE5840000},{0x0202F764,0xE5801000},
      {0x0202F7C4,0xE5801000},{0x0202F820,0xE5801000},{0x0202F8E8,0xE5823000},{0x02030E04,0xE5812000},
      {0x020310E0,0xE5801000},{0x0205539C,0xE5801000},{0x02055668,0xE5801000},{0x02057994,0xE5801000},
      {0x020594E8,0xE5813000} },
    // EU1.1
    { {0x02008E78,0xE5840018},{0x02008F0C,0xE5840018},{0x0202A4B8,0xE5841000},{0x0202A578,0xE584C000},
      {0x0202A620,0xE584C000},{0x0202A6D0,0xE5840000},{0x0202A6D8,0xE5840000},{0x0202F76C,0xE5801000},
      {0x0202F7CC,0xE5801000},{0x0202F828,0xE5801000},{0x0202F8F0,0xE5823000},{0x02030E0C,0xE5812000},
      {0x020310E8,0xE5801000},{0x020553C8,0xE5801000},{0x02055694,0xE5801000},{0x020579C0,0xE5801000},
      {0x02059534,0xE5813000} },
    // KR1.0
    { {0x0203302C,0xE5801000},{0x0203336C,0xE5812000},{0x020345F8,0xE5801000},{0x0203472C,0xE5801000},
      {0x02034788,0xE5801000},{0x020347DC,0xE5801000},{0x02034800,0xE5801000},{0x0203487C,0xE5812000},
      {0x0203489C,0xE5823000},{0x02038FB8,0xE5841000},{0x02039054,0xE584C000},{0x02039100,0xE584C000},
      {0x020391BC,0xE5840000},{0x02050764,0xE5801000},{0x02050A24,0xE5801000},{0x02053F54,0xE5803000},
      {0x02054DCC,0xE5801000} },
};

// Patch state tracking
static bool s_hudPatchApplied = false;

static void ApplyNoHudPatch(melonDS::NDS* nds, uint8_t romGroup)
{
    if (s_hudPatchApplied) return;
    for (int i = 0; i < NOHUD_PATCH_COUNT; i++) {
        nds->ARM9Write32(kHudPatch[romGroup][i].addr, ARM_NOP);
    }
    s_hudPatchApplied = true;
}

static void RestoreHudPatch(melonDS::NDS* nds, uint8_t romGroup)
{
    if (!s_hudPatchApplied) return;
    for (int i = 0; i < NOHUD_PATCH_COUNT; i++) {
        nds->ARM9Write32(kHudPatch[romGroup][i].addr, kHudPatch[romGroup][i].restoreValue);
    }
    s_hudPatchApplied = false;
}

// Called on emu stop/reset to ensure clean state
void CustomHud_ResetPatchState()
{
    s_hudPatchApplied = false;
}

// =========================================================================
//  Internal helpers
// =========================================================================
static inline QImage LoadIcon(const char* resource)
{
    QImage img(resource);
    if (img.isNull()) return img;
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// =========================================================================
//  Gauge drawing — horizontal or vertical bar
//  orientation: 0=horizontal, 1=vertical
// =========================================================================
static void DrawGauge(QPainter* p, int x, int y, float ratio,
                      const QColor& fillColor, int orientation,
                      int barLength, int barWidth)
{
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    if (barLength <= 0) barLength = 28;
    if (barWidth  <= 0) barWidth  = 3;

    QColor bgColor(0, 0, 0, 128);

    if (orientation == 0) {
        // Horizontal bar
        p->fillRect(x, y, barLength, barWidth, bgColor);
        int fillW = static_cast<int>(barLength * ratio);
        if (fillW > 0) p->fillRect(x, y, fillW, barWidth, fillColor);
    } else {
        // Vertical bar (fills bottom to top)
        p->fillRect(x, y, barWidth, barLength, bgColor);
        int fillH = static_cast<int>(barLength * ratio);
        if (fillH > 0) p->fillRect(x, y + barLength - fillH, barWidth, fillH, fillColor);
    }
}

static inline QColor HpGaugeColor(uint16_t hp)
{
    if (hp <= 25)      return QColor(255, 0, 0);
    else if (hp <= 50) return QColor(255, 165, 0);
    else               return QColor(56, 192, 8); // Sylux Hud Color
}

// Anchor: 0=below, 1=above, 2=right, 3=left, 4=center
// Computes gauge base position relative to text, then adds user offset.
static void CalcGaugePos(int textX, int textY, int anchor,
                         int ofsX, int ofsY, int gaugeLen, int gaugeWid, int ori,
                         int& outX, int& outY)
{
    // Text metrics approximate: ~30px wide, baseline at y, ascent ~8px
    constexpr int kTextW = 30, kTextH = 8;
    switch (anchor) {
    case 0: // Below
        outX = textX + ofsX;
        outY = textY + 2 + ofsY;
        break;
    case 1: // Above
        outX = textX + ofsX;
        outY = textY - kTextH - (ori == 0 ? gaugeWid : gaugeLen) + ofsY;
        break;
    case 2: // Right
        outX = textX + kTextW + ofsX;
        outY = textY - kTextH / 2 - (ori == 0 ? gaugeWid : gaugeLen) / 2 + ofsY;
        break;
    case 3: // Left
        outX = textX - (ori == 0 ? gaugeLen : gaugeWid) + ofsX;
        outY = textY - kTextH / 2 - (ori == 0 ? gaugeWid : gaugeLen) / 2 + ofsY;
        break;
    case 4: // Center (overlaps text center)
        outX = textX + kTextW / 2 - (ori == 0 ? gaugeLen : gaugeWid) / 2 + ofsX;
        outY = textY - kTextH / 2 - (ori == 0 ? gaugeWid : gaugeLen) / 2 + ofsY;
        break;
    default:
        outX = textX + ofsX;
        outY = textY + 2 + ofsY;
        break;
    }
}

static inline void DrawHP(QPainter* p, uint16_t hp, uint16_t maxHP, int x, int y,
                           const std::string& prefix,
                           bool showGauge, int gaugeOri, int gaugeLen, int gaugeWid,
                           int gaugeOfsX, int gaugeOfsY, int gaugeAnchor,
                           bool autoColor, const QColor& gaugeColor)
{
    if (hp <= 25)       p->setPen(QColor(255, 0, 0));
    else if (hp <= 50)  p->setPen(QColor(255, 165, 0));
    else                p->setPen(QColor(255, 255, 255));
    p->drawText(QPoint(x, y), (prefix + std::to_string(hp)).c_str());

    if (showGauge && maxHP > 0) {
        float ratio = static_cast<float>(hp) / static_cast<float>(maxHP);
        QColor gc = autoColor ? HpGaugeColor(hp) : gaugeColor;
        int gx, gy;
        CalcGaugePos(x, y, gaugeAnchor, gaugeOfsX, gaugeOfsY, gaugeLen, gaugeWid, gaugeOri, gx, gy);
        DrawGauge(p, gx, gy, ratio, gc, gaugeOri, gaugeLen, gaugeWid);
    }
}

static void DrawWeaponAmmo(QPainter* p, melonDS::u8* ram,
                           uint8_t weapon, uint16_t ammoSpecial, uint32_t addrMissile,
                           uint16_t maxAmmoSpecial, uint16_t maxAmmoMissile,
                           int baseX, int baseY,
                           const std::string& ammoPrefix,
                           bool showIcon, int iconMode,
                           int iconOfsX, int iconOfsY,
                           int iconPosX, int iconPosY,
                           bool showGauge, int gaugeOri, int gaugeLen, int gaugeWid,
                           int gaugeOfsX, int gaugeOfsY, int gaugeAnchor,
                           const QColor& gaugeColor)
{
    p->setPen(Qt::white);
    uint16_t ammo = 0;
    bool hasAmmo = true;
    QImage icon;

    switch (weapon) {
    case 0:
        hasAmmo = false;
        icon = LoadIcon(":/mph-icon-pb");
        break;
    case 1: ammo = ammoSpecial / 0x5;  icon = LoadIcon(":/mph-icon-volt"); break;
    case 2: {
        icon = LoadIcon(":/mph-icon-missile");
        uint16_t m = Read16(ram, addrMissile);
        ammo = m / 0x0A;
        break;
    }
    case 3: ammo = ammoSpecial / 0x4;  icon = LoadIcon(":/mph-icon-battlehammer"); break;
    case 4: ammo = ammoSpecial / 0x14; icon = LoadIcon(":/mph-icon-imperialist"); break;
    case 5: ammo = ammoSpecial / 0x5;  icon = LoadIcon(":/mph-icon-judicator"); break;
    case 6: ammo = ammoSpecial / 0xA;  icon = LoadIcon(":/mph-icon-magmaul"); break;
    case 7: ammo = ammoSpecial / 0xA;  icon = LoadIcon(":/mph-icon-shock"); break;
    case 8: ammo = 1;                  icon = LoadIcon(":/mph-icon-omega"); break;
    default: return;
    }

    uint16_t maxAmmo = 0;
    if (hasAmmo) {
        switch (weapon) {
        case 1: maxAmmo = maxAmmoSpecial / 0x5;  break;
        case 2: maxAmmo = maxAmmoMissile / 0x0A; break;
        case 3: maxAmmo = maxAmmoSpecial / 0x4;  break;
        case 4: maxAmmo = maxAmmoSpecial / 0x14; break;
        case 5: maxAmmo = maxAmmoSpecial / 0x5;  break;
        case 6: maxAmmo = maxAmmoSpecial / 0xA;  break;
        case 7: maxAmmo = maxAmmoSpecial / 0xA;  break;
        case 8: maxAmmo = 1; break;
        default: break;
        }
    }

    // Ammo text always at baseX, baseY+8 (baseline), zero-padded to min 2 digits
    int textX = baseX, textY = baseY + 8;
    if (hasAmmo) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d", ammo);
        p->drawText(QPoint(textX, textY), (ammoPrefix + buf).c_str());
    }

    // Icon position: offset mode (relative to ammo text) or independent (absolute)
    if (showIcon) {
        if (iconMode == 0)
            p->drawImage(QPoint(baseX + iconOfsX, baseY + iconOfsY), icon);
        else
            p->drawImage(QPoint(iconPosX, iconPosY), icon);
    }

    if (showGauge && hasAmmo && maxAmmo > 0) {
        float ratio = static_cast<float>(ammo) / static_cast<float>(maxAmmo);
        int gx, gy;
        CalcGaugePos(textX, textY, gaugeAnchor, gaugeOfsX, gaugeOfsY, gaugeLen, gaugeWid, gaugeOri, gx, gy);
        DrawGauge(p, gx, gy, ratio, gaugeColor, gaugeOri, gaugeLen, gaugeWid);
    }
}



// =========================================================================
//  Crosshair — Valorant/CSGO style: Inner Lines + Outer Lines
// =========================================================================
struct LineGroup {
    bool   show;
    double opacity;
    int    lengthX;
    int    lengthY;
    int    thickness;
    int    offset;
};

struct CrosshairSettings {
    QColor    color;
    bool      outline;
    double    outlineOpacity;
    int       outlineThickness;
    bool      centerDot;
    double    dotOpacity;
    int       dotThickness;
    bool      tStyle;
    LineGroup inner;
    LineGroup outer;
};

// ---- Low-level draw helpers ----

static void DrawArmFill(QPainter* p, int x1, int y1, int x2, int y2,
                        int thickness, const QColor& color)
{
    QPen pen(color);
    pen.setWidth(thickness);
    p->setPen(pen);
    p->drawLine(x1, y1, x2, y2);
}

// ---- Arm coordinate collector ----
// NOTE: QPainter::drawLine is endpoint-inclusive, so a line from A to B
// draws |B-A|+1 pixels. To draw exactly N pixels, use end = start + N - 1.

struct ArmCoords { int x1, y1, x2, y2; };

static int CollectArms(ArmCoords* out, int cx, int cy,
                       const LineGroup& lg, bool tStyle)
{
    int n = 0, o = lg.offset, lx = lg.lengthX, ly = lg.lengthY;
    if (lx > 0) {
        out[n++] = { cx - o - lx, cy, cx - o - 1, cy };  // Left
        out[n++] = { cx + o + 1,  cy, cx + o + lx, cy };  // Right
    }
    if (ly > 0) {
        out[n++] = { cx, cy + o + 1,  cx, cy + o + ly };  // Bottom
        if (!tStyle)
            out[n++] = { cx, cy - o - ly, cx, cy - o - 1 }; // Top
    }
    return n;
}

// ---- Main crosshair draw: 4-pass layered rendering ----
//
//  Layer order (bottom to top):
//    Pass 1 — Outline (all elements rendered as one flat layer via off-screen buffer)
//    Pass 2 — Center dot fill
//    Pass 3 — Inner line fills
//    Pass 4 — Outer line fills (topmost)

static void DrawCrosshair(QPainter* p, melonDS::u8* ram,
                          const RomAddresses& rom,
                          const CrosshairSettings& cs,
                          float stretchX)
{
    int cx = static_cast<int>(Read8(ram, rom.crosshairPosX));
    int cy = static_cast<int>(Read8(ram, rom.crosshairPosY));

    // Widescreen X correction: the widescreen hack widens the 3D horizontal
    // FOV by stretchX, compressing objects toward center. The game's aim
    // system still calculates in 4:3 space, so divide X displacement by
    // stretchX to match the actual 3D scene projection.
    if (stretchX > 1.0f) {
        cx = static_cast<int>(std::lround(128.0f + (cx - 128.0f) / stretchX));
    }

    // Collect arm coordinates
    ArmCoords innerArms[4], outerArms[4];
    int nInner = 0, nOuter = 0;
    if (cs.inner.show)
        nInner = CollectArms(innerArms, cx, cy, cs.inner, cs.tStyle);
    if (cs.outer.show)
        nOuter = CollectArms(outerArms, cx, cy, cs.outer, cs.tStyle);

    int dotHalf = cs.dotThickness / 2;

    // === Pass 1: ALL outlines as one flat layer ===
    if (cs.outline && cs.outlineOpacity > 0.0) {
        QImage olBuf(256, 192, QImage::Format_ARGB32_Premultiplied);
        olBuf.fill(Qt::transparent);
        {
            QPainter olP(&olBuf);
            olP.setRenderHint(QPainter::Antialiasing, false);
            QColor solidBlack(0, 0, 0, 255);

            // Dot outline
            if (cs.centerDot) {
                olP.setPen(Qt::NoPen);
                olP.setBrush(solidBlack);
                int oh = dotHalf + cs.outlineThickness;
                olP.drawRect(cx - oh, cy - oh, oh * 2 + 1, oh * 2 + 1);
                olP.setBrush(Qt::NoBrush);
            }
            // Inner line outlines
            for (int i = 0; i < nInner; i++) {
                QPen pen(solidBlack);
                pen.setWidth(cs.inner.thickness + cs.outlineThickness * 2);
                olP.setPen(pen);
                olP.drawLine(innerArms[i].x1, innerArms[i].y1,
                             innerArms[i].x2, innerArms[i].y2);
            }
            // Outer line outlines
            for (int i = 0; i < nOuter; i++) {
                QPen pen(solidBlack);
                pen.setWidth(cs.outer.thickness + cs.outlineThickness * 2);
                olP.setPen(pen);
                olP.drawLine(outerArms[i].x1, outerArms[i].y1,
                             outerArms[i].x2, outerArms[i].y2);
            }
        }

        p->setOpacity(cs.outlineOpacity);
        p->drawImage(0, 0, olBuf);
        p->setOpacity(1.0);
    }

    // === Pass 2: Center dot fill ===
    if (cs.centerDot) {
        p->setPen(Qt::NoPen);
        QColor dotColor = cs.color;
        dotColor.setAlphaF(cs.dotOpacity);
        p->setBrush(dotColor);
        p->drawRect(cx - dotHalf, cy - dotHalf, dotHalf * 2 + 1, dotHalf * 2 + 1);
        p->setBrush(Qt::NoBrush);
    }

    // === Pass 3: Inner line fills ===
    if (nInner > 0) {
        QColor innerColor = cs.color;
        innerColor.setAlphaF(cs.inner.opacity);
        for (int i = 0; i < nInner; i++)
            DrawArmFill(p, innerArms[i].x1, innerArms[i].y1,
                        innerArms[i].x2, innerArms[i].y2,
                        cs.inner.thickness, innerColor);
    }

    // === Pass 4: Outer line fills (topmost layer) ===
    if (nOuter > 0) {
        QColor outerColor = cs.color;
        outerColor.setAlphaF(cs.outer.opacity);
        for (int i = 0; i < nOuter; i++)
            DrawArmFill(p, outerArms[i].x1, outerArms[i].y1,
                        outerArms[i].x2, outerArms[i].y2,
                        cs.outer.thickness, outerColor);
    }
}

static CrosshairSettings ReadCrosshairConfig(Config::Table& cfg)
{
    CrosshairSettings cs;

    int r = cfg.GetInt(kCfgChColorR);
    int g = cfg.GetInt(kCfgChColorG);
    int b = cfg.GetInt(kCfgChColorB);
    cs.color = QColor(r, g, b);

    cs.outline          = cfg.GetBool(kCfgChOutline);
    cs.outlineOpacity   = cfg.GetDouble(kCfgChOutlineOpacity);
    cs.outlineThickness = cfg.GetInt(kCfgChOutlineThickness);
    if (cs.outlineThickness <= 0) cs.outlineThickness = 1;

    cs.centerDot    = cfg.GetBool(kCfgChCenterDot);
    cs.dotOpacity   = cfg.GetDouble(kCfgChDotOpacity);
    cs.dotThickness = cfg.GetInt(kCfgChDotThickness);
    if (cs.dotThickness <= 0) cs.dotThickness = 1;

    cs.tStyle = cfg.GetBool(kCfgChTStyle);

    cs.inner.show      = cfg.GetBool(kCfgChInnerShow);
    cs.inner.opacity   = cfg.GetDouble(kCfgChInnerOpacity);
    cs.inner.lengthX   = cfg.GetInt(kCfgChInnerLengthX);
    cs.inner.lengthY   = cfg.GetInt(kCfgChInnerLengthY);
    cs.inner.thickness = cfg.GetInt(kCfgChInnerThickness);
    if (cs.inner.thickness <= 0) cs.inner.thickness = 1;
    cs.inner.offset    = cfg.GetInt(kCfgChInnerOffset);

    cs.outer.show      = cfg.GetBool(kCfgChOuterShow);
    cs.outer.opacity   = cfg.GetDouble(kCfgChOuterOpacity);
    cs.outer.lengthX   = cfg.GetInt(kCfgChOuterLengthX);
    cs.outer.lengthY   = cfg.GetInt(kCfgChOuterLengthY);
    cs.outer.thickness = cfg.GetInt(kCfgChOuterThickness);
    if (cs.outer.thickness <= 0) cs.outer.thickness = 1;
    cs.outer.offset    = cfg.GetInt(kCfgChOuterOffset);

    return cs;
}

// =========================================================================
//  CustomHud_Render — main entry point
// =========================================================================
void CustomHud_Render(
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

    // --- If CustomHUD is disabled, restore patches and bail ---
    if (!CustomHud_IsEnabled(localCfg)) {
        if (s_hudPatchApplied) {
            RestoreHudPatch(nds, romGroup);
            // Restore HudToggle: 0x1F for first-person, 0x11 for transform/camera
            uint8_t vm = Read8(ram, rom.baseViewMode + offP);
            Write8(ram, rom.hudToggle, (vm == 0x00) ? 0x1F : 0x11);
        }
        return;
    }

    // --- Apply NoHUD patch (only once) ---
    ApplyNoHudPatch(nds, romGroup);

    // --- Resolve player-relative addresses ---
    const uint32_t addrAmmoSpecial = rom.currentAmmoSpecial + offP;
    const uint32_t addrAmmoMissile = rom.currentAmmoMissile + offP;
    const uint16_t maxHP           = Read16(ram, rom.maxHP + offP);
    const uint16_t maxAmmoSpecial  = Read16(ram, rom.maxAmmoSpecial + offP);
    const uint16_t maxAmmoMissile  = Read16(ram, rom.maxAmmoMissile + offP);

    // --- Write HudToggle: 0x01 = custom HUD active mode ---
    bool isStartPressed = Read8(ram, rom.startPressed) == 0x01;
    Write8(ram, rom.hudToggle, isStartPressed ? 0x11 : 0x01);

    // --- Visibility checks ---
    uint16_t currentHP = Read16(ram, rom.playerHP + offP);
    bool isDead        = (currentHP == 0);
    bool isGameOver    = Read8(ram, rom.gameOver) != 0x00;
    uint8_t viewMode   = Read8(ram, rom.baseViewMode + offP);
    bool isFirstPerson = (viewMode == 0x00);

    if (isStartPressed || isDead || isGameOver) return;

    // =====================================================================
    //  Font size — shared for HP and Ammo
    // =====================================================================
    int hudFontSize = localCfg.GetInt(kCfgHudFontSize);
    if (hudFontSize > 0) {
        QFont f = topPaint->font();
        f.setPixelSize(hudFontSize);
        topPaint->setFont(f);
    }

    // =====================================================================
    //  HP — always visible when alive (including altForm/transform)
    // =====================================================================
    int hpX = localCfg.GetInt(kCfgHudHpX);
    int hpY = localCfg.GetInt(kCfgHudHpY);
    std::string hpPrefix = localCfg.GetString(kCfgHudHpPrefix);
    bool hpGauge    = localCfg.GetBool(kCfgHudHpGauge);
    int  hpGaugeOri = localCfg.GetInt(kCfgHudHpGaugeOrientation);
    int  hpGaugeLen = localCfg.GetInt(kCfgHudHpGaugeLength);
    int  hpGaugeWid = localCfg.GetInt(kCfgHudHpGaugeWidth);
    int  hpGaugeOX  = localCfg.GetInt(kCfgHudHpGaugeOffsetX);
    int  hpGaugeOY  = localCfg.GetInt(kCfgHudHpGaugeOffsetY);
    int  hpGaugeAnc = localCfg.GetInt(kCfgHudHpGaugeAnchor);
    bool hpAutoClr  = localCfg.GetBool(kCfgHudHpGaugeAutoColor);
    QColor hpGaugeClr(localCfg.GetInt(kCfgHudHpGaugeColorR),
                      localCfg.GetInt(kCfgHudHpGaugeColorG),
                      localCfg.GetInt(kCfgHudHpGaugeColorB));
    DrawHP(topPaint, currentHP, maxHP, hpX, hpY, hpPrefix,
           hpGauge, hpGaugeOri, hpGaugeLen, hpGaugeWid,
           hpGaugeOX, hpGaugeOY, hpGaugeAnc, hpAutoClr, hpGaugeClr);

    // =====================================================================
    //  Weapon/Ammo + Crosshair — first-person only
    // =====================================================================
    if (!isFirstPerson) return;

    int wpnX = localCfg.GetInt(kCfgHudWeaponX);
    int wpnY = localCfg.GetInt(kCfgHudWeaponY);
    std::string ammoPrefix = localCfg.GetString(kCfgHudAmmoPrefix);
    bool iconShow    = localCfg.GetBool(kCfgHudWeaponIconShow);
    int  iconMode    = localCfg.GetInt(kCfgHudWeaponIconMode);
    int  iconOfsX    = localCfg.GetInt(kCfgHudWeaponIconOffsetX);
    int  iconOfsY    = localCfg.GetInt(kCfgHudWeaponIconOffsetY);
    int  iconPosX    = localCfg.GetInt(kCfgHudWeaponIconPosX);
    int  iconPosY    = localCfg.GetInt(kCfgHudWeaponIconPosY);
    bool ammoGauge   = localCfg.GetBool(kCfgHudAmmoGauge);
    int  ammoGaugeOri = localCfg.GetInt(kCfgHudAmmoGaugeOrientation);
    int  ammoGaugeLen = localCfg.GetInt(kCfgHudAmmoGaugeLength);
    int  ammoGaugeWid = localCfg.GetInt(kCfgHudAmmoGaugeWidth);
    int  ammoGaugeOX  = localCfg.GetInt(kCfgHudAmmoGaugeOffsetX);
    int  ammoGaugeOY  = localCfg.GetInt(kCfgHudAmmoGaugeOffsetY);
    int  ammoGaugeAnc = localCfg.GetInt(kCfgHudAmmoGaugeAnchor);
    QColor ammoGaugeClr(localCfg.GetInt(kCfgHudAmmoGaugeColorR),
                        localCfg.GetInt(kCfgHudAmmoGaugeColorG),
                        localCfg.GetInt(kCfgHudAmmoGaugeColorB));
    uint8_t currentWeapon = Read8(ram, addrHot.currentWeapon);
    DrawWeaponAmmo(topPaint, ram, currentWeapon, Read16(ram, addrAmmoSpecial), addrAmmoMissile,
                   maxAmmoSpecial, maxAmmoMissile,
                   wpnX, wpnY, ammoPrefix,
                   iconShow, iconMode, iconOfsX, iconOfsY, iconPosX, iconPosY,
                   ammoGauge, ammoGaugeOri, ammoGaugeLen, ammoGaugeWid,
                   ammoGaugeOX, ammoGaugeOY, ammoGaugeAnc, ammoGaugeClr);

    bool isAlt   = Read8(ram, addrHot.isAltForm) == 0x02;
    bool isTrans = (Read8(ram, addrHot.jumpFlag) & 0x10) != 0;
    if (!isTrans && !isAlt) {
        CrosshairSettings cs = ReadCrosshairConfig(localCfg);
        DrawCrosshair(topPaint, ram, rom, cs, topStretchX);
    }
}

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
