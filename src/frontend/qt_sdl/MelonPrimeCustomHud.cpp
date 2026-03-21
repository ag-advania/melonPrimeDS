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
static constexpr const char* kCfgHudHpX     = "Metroid.Visual.HudHpX";
static constexpr const char* kCfgHudHpY     = "Metroid.Visual.HudHpY";
static constexpr const char* kCfgHudWeaponX = "Metroid.Visual.HudWeaponX";
static constexpr const char* kCfgHudWeaponY = "Metroid.Visual.HudWeaponY";

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

static inline void DrawHP(QPainter* p, uint16_t hp, int x, int y)
{
    if (hp <= 25)       p->setPen(QColor(255, 0, 0));
    else if (hp <= 50)  p->setPen(QColor(255, 165, 0));
    else                p->setPen(QColor(255, 255, 255));
    p->drawText(QPoint(x, y), (std::string("hp ") + std::to_string(hp)).c_str());
}

static void DrawWeaponAmmo(QPainter* p, melonDS::u8* ram,
                           uint8_t weapon, uint16_t ammoSpecial, uint32_t addrMissile,
                           int baseX, int baseY)
{
    p->setPen(Qt::white);
    uint16_t ammo = 0;
    bool hasAmmo = true;
    QImage icon;

    switch (weapon) {
    case 0: // Power Beam — no ammo display, icon only
        hasAmmo = false;
        icon = LoadIcon(":/mph-icon-pb");
        break;
    case 1: ammo = ammoSpecial / 0x5;  icon = LoadIcon(":/mph-icon-volt"); break;
    case 2: { // Missiles — reads from separate address
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

    // Layout: ammo text on top, weapon icon below
    if (hasAmmo) {
        p->drawText(QPoint(baseX, baseY + 8), std::to_string(ammo).c_str());
        p->drawImage(QPoint(baseX, baseY + 10), icon);
    } else {
        // Power Beam: icon only (no ammo text), same position as other weapons
        p->drawImage(QPoint(baseX, baseY + 10), icon);
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
    // Render all outlines to an off-screen buffer as opaque black,
    // then composite the buffer onto the main painter at outlineOpacity.
    // This prevents overlapping outlines from doubling up in darkness.
    if (cs.outline && cs.outlineOpacity > 0.0) {
        // Use the overlay size (256x192 for DS top screen)
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
        } // olP destroyed — safe to read olBuf

        // Composite the outline buffer at the desired opacity
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
    //  HP — always visible when alive (including altForm/transform)
    // =====================================================================
    int hpX = localCfg.GetInt(kCfgHudHpX);
    int hpY = localCfg.GetInt(kCfgHudHpY);
    DrawHP(topPaint, currentHP, hpX, hpY);

    // =====================================================================
    //  Weapon/Ammo + Crosshair — first-person only
    // =====================================================================
    if (!isFirstPerson) return;

    int wpnX = localCfg.GetInt(kCfgHudWeaponX);
    int wpnY = localCfg.GetInt(kCfgHudWeaponY);
    uint8_t currentWeapon = Read8(ram, addrHot.currentWeapon);
    DrawWeaponAmmo(topPaint, ram, currentWeapon, Read16(ram, addrAmmoSpecial), addrAmmoMissile, wpnX, wpnY);

    bool isAlt   = Read8(ram, addrHot.isAltForm) == 0x02;
    bool isTrans = (Read8(ram, addrHot.jumpFlag) & 0x10) != 0;
    if (!isTrans && !isAlt) {
        CrosshairSettings cs = ReadCrosshairConfig(localCfg);
        DrawCrosshair(topPaint, ram, rom, cs, topStretchX);
    }
}

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
