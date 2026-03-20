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

namespace MelonPrime {

// =========================================================================
//  Config keys
// =========================================================================
static constexpr const char* kCfgCustomHud = "Metroid.Visual.CustomHUD";

// Crosshair — General
static constexpr const char* kCfgChColorR           = "Metroid.Visual.CrosshairColorR";
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
static constexpr const char* kCfgChInnerLength    = "Metroid.Visual.CrosshairInnerLength";
static constexpr const char* kCfgChInnerThickness = "Metroid.Visual.CrosshairInnerThickness";
static constexpr const char* kCfgChInnerOffset    = "Metroid.Visual.CrosshairInnerOffset";

// Crosshair — Outer Lines
static constexpr const char* kCfgChOuterShow      = "Metroid.Visual.CrosshairOuterShow";
static constexpr const char* kCfgChOuterOpacity   = "Metroid.Visual.CrosshairOuterOpacity";
static constexpr const char* kCfgChOuterLength    = "Metroid.Visual.CrosshairOuterLength";
static constexpr const char* kCfgChOuterThickness = "Metroid.Visual.CrosshairOuterThickness";
static constexpr const char* kCfgChOuterOffset    = "Metroid.Visual.CrosshairOuterOffset";

bool CustomHud_IsEnabled(Config::Table& localCfg)
{
    return localCfg.GetBool(kCfgCustomHud);
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

static inline void UpdateHudToggle(melonDS::u8* ram,
                                   uint32_t addrHudToggle, bool isStartPressed)
{
    Write8(ram, addrHudToggle, isStartPressed ? 0x11 : 0x01);
}

static inline void DrawHP(QPainter* p, uint16_t hp)
{
    if (hp <= 25)       p->setPen(QColor(255, 0, 0));
    else if (hp <= 50)  p->setPen(QColor(255, 165, 0));
    else                p->setPen(QColor(255, 255, 255));
    p->drawText(QPoint(4, 188), (std::string("hp ") + std::to_string(hp)).c_str());
}

static void DrawWeaponAmmo(QPainter* p, melonDS::u8* ram,
                           uint8_t weapon, uint16_t ammoSpecial, uint32_t addrMissile)
{
    p->setPen(Qt::white);
    uint16_t ammo = ammoSpecial;
    QImage icon;
    switch (weapon) {
    case 0: ammo = ammoSpecial;        icon = LoadIcon(":/mph-icon-pb"); break;
    case 1: ammo = ammoSpecial / 0x5;  icon = LoadIcon(":/mph-icon-volt"); break;
    case 2: {
        icon = LoadIcon(":/mph-icon-missile");
        uint16_t m = Read16(ram, addrMissile);
        p->drawText(QPoint(15, 173), std::to_string(m / 0x0A).c_str());
        p->drawImage(QPoint(4, 165), icon);
        return;
    }
    case 3: ammo = ammoSpecial / 0x4;  icon = LoadIcon(":/mph-icon-battlehammer"); break;
    case 4: ammo = ammoSpecial / 0x14; icon = LoadIcon(":/mph-icon-imperialist"); break;
    case 5: ammo = ammoSpecial / 0x5;  icon = LoadIcon(":/mph-icon-judicator"); break;
    case 6: ammo = ammoSpecial / 0xA;  icon = LoadIcon(":/mph-icon-magmaul"); break;
    case 7: ammo = ammoSpecial / 0xA;  icon = LoadIcon(":/mph-icon-shock"); break;
    case 8: ammo = 1;                  icon = LoadIcon(":/mph-icon-omega"); break;
    default: break;
    }
    if (weapon != 0) {
        p->drawText(QPoint(15, 173), std::to_string(ammo).c_str());
        p->drawImage(QPoint(5, 165), icon);
    }
}

// =========================================================================
//  Crosshair — Valorant/CSGO style: Inner Lines + Outer Lines
// =========================================================================
struct LineGroup {
    bool   show;
    double opacity;
    int    length;
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

static void DrawArm(QPainter* p, int x1, int y1, int x2, int y2,
                    int thickness, const QColor& color,
                    bool outline, double outlineOpacity, int olThickness)
{
    if (outline) {
        QColor olColor(0, 0, 0);
        olColor.setAlphaF(outlineOpacity);
        QPen olPen(olColor);
        olPen.setWidth(thickness + olThickness * 2);
        p->setPen(olPen);
        p->drawLine(x1, y1, x2, y2);
    }
    QPen pen(color);
    pen.setWidth(thickness);
    p->setPen(pen);
    p->drawLine(x1, y1, x2, y2);
}

static void DrawLineGroup(QPainter* p, int cx, int cy,
                          const LineGroup& lg, const QColor& baseColor,
                          bool outline, double outlineOpacity, int olThickness, bool tStyle)
{
    if (!lg.show || lg.length <= 0) return;
    int o = lg.offset, l = lg.length, t = lg.thickness;

    // Apply line group opacity to base color
    QColor color = baseColor;
    color.setAlphaF(lg.opacity);

    DrawArm(p, cx - o - l, cy, cx - o, cy, t, color, outline, outlineOpacity, olThickness);
    DrawArm(p, cx + o, cy, cx + o + l, cy, t, color, outline, outlineOpacity, olThickness);
    DrawArm(p, cx, cy + o, cx, cy + o + l, t, color, outline, outlineOpacity, olThickness);
    if (!tStyle)
        DrawArm(p, cx, cy - o - l, cx, cy - o, t, color, outline, outlineOpacity, olThickness);
}

static void DrawCrosshair(QPainter* p, melonDS::u8* ram,
                          const GameAddressesHot& addrHot,
                          const CrosshairSettings& cs)
{
    float chX = static_cast<float>(Read8(ram, addrHot.aimX + 0x27E));
    float chY = static_cast<float>(Read8(ram, addrHot.aimX + 0x280));
    chX = (chX < 0) ? chX + 254 : chX;
    int cx = static_cast<int>(chX), cy = static_cast<int>(chY);

    // Outer lines (behind)
    DrawLineGroup(p, cx, cy, cs.outer, cs.color,
                  cs.outline, cs.outlineOpacity, cs.outlineThickness, cs.tStyle);
    // Inner lines
    DrawLineGroup(p, cx, cy, cs.inner, cs.color,
                  cs.outline, cs.outlineOpacity, cs.outlineThickness, cs.tStyle);

    // Center dot
    if (cs.centerDot) {
        int dh = cs.dotThickness / 2;
        if (cs.outline) {
            p->setPen(Qt::NoPen);
            QColor olColor(0, 0, 0);
            olColor.setAlphaF(cs.outlineOpacity);
            p->setBrush(olColor);
            int oh = dh + cs.outlineThickness;
            p->drawRect(cx - oh, cy - oh, oh * 2 + 1, oh * 2 + 1);
        }
        p->setPen(Qt::NoPen);
        QColor dotColor = cs.color;
        dotColor.setAlphaF(cs.dotOpacity);
        p->setBrush(dotColor);
        p->drawRect(cx - dh, cy - dh, dh * 2 + 1, dh * 2 + 1);
        p->setBrush(Qt::NoBrush);
    }
}

static CrosshairSettings ReadCrosshairConfig(Config::Table& cfg)
{
    CrosshairSettings cs;

    int r = cfg.GetInt(kCfgChColorR);
    int g = cfg.GetInt(kCfgChColorG);
    int b = cfg.GetInt(kCfgChColorB);
    if (r == 0 && g == 0 && b == 0) { r = 255; g = 255; b = 255; }
    cs.color = QColor(r, g, b);

    cs.outline          = cfg.GetBool(kCfgChOutline);
    cs.outlineOpacity   = cfg.GetDouble(kCfgChOutlineOpacity);
    if (cs.outlineOpacity <= 0.0) cs.outlineOpacity = 0.5;
    cs.outlineThickness = cfg.GetInt(kCfgChOutlineThickness);
    if (cs.outlineThickness <= 0) cs.outlineThickness = 1;

    cs.centerDot    = cfg.GetBool(kCfgChCenterDot);
    cs.dotOpacity   = cfg.GetDouble(kCfgChDotOpacity);
    if (cs.dotOpacity <= 0.0) cs.dotOpacity = 1.0;
    cs.dotThickness = cfg.GetInt(kCfgChDotThickness);
    if (cs.dotThickness <= 0) cs.dotThickness = 1;

    cs.tStyle = cfg.GetBool(kCfgChTStyle);

    cs.inner.show      = cfg.GetBool(kCfgChInnerShow);
    cs.inner.opacity   = cfg.GetDouble(kCfgChInnerOpacity);
    if (cs.inner.opacity <= 0.0) cs.inner.opacity = 0.8;
    cs.inner.length    = cfg.GetInt(kCfgChInnerLength);
    cs.inner.thickness = cfg.GetInt(kCfgChInnerThickness);
    if (cs.inner.thickness <= 0) cs.inner.thickness = 1;
    cs.inner.offset    = cfg.GetInt(kCfgChInnerOffset);

    cs.outer.show      = cfg.GetBool(kCfgChOuterShow);
    cs.outer.opacity   = cfg.GetDouble(kCfgChOuterOpacity);
    if (cs.outer.opacity <= 0.0) cs.outer.opacity = 0.35;
    cs.outer.length    = cfg.GetInt(kCfgChOuterLength);
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
    bool isInGame)
{
    if (!isInGame) return;

    melonDS::NDS* nds = emu->getNDS();
    melonDS::u8* ram = nds->MainRAM;
    const uint32_t offP = static_cast<uint32_t>(playerPosition) * Consts::PLAYER_ADDR_INC;

    if (!CustomHud_IsEnabled(localCfg)) {
        uint8_t vm = Read8(ram, rom.baseViewMode + offP);
        Write8(ram, rom.hudToggle, (vm == 0x00) ? 0x1F : 0x11);
        return;
    }

    const uint32_t addrAmmoSpecial = rom.currentAmmoSpecial + offP;
    const uint32_t addrAmmoMissile = rom.currentAmmoMissile + offP;

    bool isStartPressed = Read8(ram, rom.startPressed) == 0x01;
    UpdateHudToggle(ram, rom.hudToggle, isStartPressed);

    // bool isGameOver = Read8(ram, rom.gameOver) != 0x00;
    uint16_t currentHP = Read16(ram, rom.playerHP + offP);
    bool isDead        = (currentHP == 0);
    uint8_t viewMode   = Read8(ram, rom.baseViewMode + offP);
    bool isFirstPerson = (viewMode == 0x00);

    if (isStartPressed || isDead || !isFirstPerson) return;

    DrawHP(topPaint, currentHP);

    uint8_t currentWeapon = Read8(ram, addrHot.currentWeapon);
    DrawWeaponAmmo(topPaint, ram, currentWeapon, Read16(ram, addrAmmoSpecial), addrAmmoMissile);

    bool isAlt   = Read8(ram, addrHot.isAltForm) == 0x02;
    bool isTrans = (Read8(ram, addrHot.jumpFlag) & 0x10) != 0;
    if (!isTrans && !isAlt) {
        CrosshairSettings cs = ReadCrosshairConfig(localCfg);
        DrawCrosshair(topPaint, ram, addrHot, cs);
    }
}

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
