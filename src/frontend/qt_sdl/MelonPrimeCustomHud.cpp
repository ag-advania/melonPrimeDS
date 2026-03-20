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
    if (!lg.show || (lg.lengthX <= 0 && lg.lengthY <= 0)) return;
    int o = lg.offset, lx = lg.lengthX, ly = lg.lengthY, t = lg.thickness;

    QColor color = baseColor;
    color.setAlphaF(lg.opacity);

    // NOTE: QPainter::drawLine is endpoint-inclusive, so a line from A to B
    // draws |B-A|+1 pixels. To draw exactly N pixels, use end = start + N - 1.

    // Horizontal arms (use lengthX)
    if (lx > 0) {
        DrawArm(p, cx - o - lx, cy, cx - o - 1, cy, t, color, outline, outlineOpacity, olThickness);  // Left
        DrawArm(p, cx + o + 1, cy, cx + o + lx, cy, t, color, outline, outlineOpacity, olThickness);   // Right
    }
    // Vertical arms (use lengthY)
    if (ly > 0) {
        DrawArm(p, cx, cy + o + 1, cx, cy + o + ly, t, color, outline, outlineOpacity, olThickness);   // Bottom
        if (!tStyle)
            DrawArm(p, cx, cy - o - ly, cx, cy - o - 1, t, color, outline, outlineOpacity, olThickness); // Top
    }
}

static void DrawCrosshair(QPainter* p, melonDS::u8* ram,
                          const RomAddresses& rom,
                          const CrosshairSettings& cs)
{
    // Read crosshair screen position from dedicated addresses
    int cx = static_cast<int>(Read16(ram, rom.crosshairPosX));
    int cy = static_cast<int>(Read16(ram, rom.crosshairPosY));

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
    bool isInGame)
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

    // --- Write HudToggle: 0x01 = custom HUD active mode ---
    bool isStartPressed = Read8(ram, rom.startPressed) == 0x01;
    Write8(ram, rom.hudToggle, isStartPressed ? 0x11 : 0x01);

    // --- Visibility checks ---
    // bool isGameOver = Read8(ram, rom.gameOver) != 0x00;  // Not needed: exits first-person
    uint16_t currentHP = Read16(ram, rom.playerHP + offP);
    bool isDead        = (currentHP == 0);
    uint8_t viewMode   = Read8(ram, rom.baseViewMode + offP);
    bool isFirstPerson = (viewMode == 0x00);

    if (isStartPressed || isDead || !isFirstPerson) return;

    // =====================================================================
    //  HP
    // =====================================================================
    DrawHP(topPaint, currentHP);

    // =====================================================================
    //  Weapon + Ammo
    // =====================================================================
    uint8_t currentWeapon = Read8(ram, addrHot.currentWeapon);
    DrawWeaponAmmo(topPaint, ram, currentWeapon, Read16(ram, addrAmmoSpecial), addrAmmoMissile);

    // =====================================================================
    //  Crosshair
    // =====================================================================
    bool isAlt   = Read8(ram, addrHot.isAltForm) == 0x02;
    bool isTrans = (Read8(ram, addrHot.jumpFlag) & 0x10) != 0;
    if (!isTrans && !isAlt) {
        CrosshairSettings cs = ReadCrosshairConfig(localCfg);
        DrawCrosshair(topPaint, ram, rom, cs);
    }
}

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
