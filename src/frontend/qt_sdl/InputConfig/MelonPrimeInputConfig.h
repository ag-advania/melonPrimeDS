/*
    Copyright 2016-2025 melonDS team

    This file is part of melonDS.
    (MelonPrime specific configuration extension)
*/

#ifndef MELONPRIMEINPUTCONFIG_H
#define MELONPRIMEINPUTCONFIG_H

#include <QWidget>
#include <QTabWidget>
#include <initializer_list>
#include "EmuInstance.h"

namespace Ui { class MelonPrimeInputConfig; }

static constexpr std::initializer_list<int> hk_tabAddonsMetroid =
{
    HK_MetroidMoveForward,
    HK_MetroidMoveBack,
    HK_MetroidMoveLeft,
    HK_MetroidMoveRight,
    HK_MetroidShootScan,
    HK_MetroidScanShoot,
    HK_MetroidZoom,
    HK_MetroidJump,
    HK_MetroidMorphBall,
    HK_MetroidHoldMorphBallBoost,
    HK_MetroidWeaponBeam,
    HK_MetroidWeaponMissile,
    HK_MetroidWeapon1,
    HK_MetroidWeapon2,
    HK_MetroidWeapon3,
    HK_MetroidWeapon4,
    HK_MetroidWeapon5,
    HK_MetroidWeapon6,
    HK_MetroidWeaponSpecial,
    HK_MetroidMenu,
};

static constexpr std::initializer_list<const char*> hk_tabAddonsMetroid_labels =
{
    "[Metroid] (W) Move Forward",
    "[Metroid] (S) Move Back",
    "[Metroid] (A) Move Left",
    "[Metroid] (D) Move Right",
    "[Metroid] (Mouse Left) Shoot/Scan, Map Zoom In",
    "[Metroid] (V) Scan/Shoot, Map Zoom In",
    "[Metroid] (Mouse Right) Imperialist Zoom, Map Zoom Out, Morph Ball Boost",
    "[Metroid] (Space) Jump",
    "[Metroid] (L. Ctrl) Transform",
    "[Metroid] (Shift) Hold to Fast Morph Ball Boost",
    "[Metroid] (Mouse 5, Side Top) Weapon Beam",
    "[Metroid] (Mouse 4, Side Bottom) Weapon Missile",
    "[Metroid] (1) Weapon 1. ShockCoil",
    "[Metroid] (2) Weapon 2. Magmaul",
    "[Metroid] (3) Weapon 3. Judicator",
    "[Metroid] (4) Weapon 4. Imperialist",
    "[Metroid] (5) Weapon 5. Battlehammer",
    "[Metroid] (6) Weapon 6. VoltDriver",
    "[Metroid] (R) Affinity Weapon (Last used Weapon/Omega cannon)",
    "[Metroid] (Tab) Menu/Map",
};

static_assert(hk_tabAddonsMetroid.size() == hk_tabAddonsMetroid_labels.size());

static constexpr std::initializer_list<int> hk_tabAddonsMetroid2 =
{
    HK_MetroidIngameSensiUp,
    HK_MetroidIngameSensiDown,
    HK_MetroidWeaponNext,
    HK_MetroidWeaponPrevious,
    HK_MetroidScanVisor,
    HK_MetroidUILeft,
    HK_MetroidUIRight,
    HK_MetroidUIOk,
    HK_MetroidUIYes,
    HK_MetroidUINo,
    HK_MetroidWeaponCheck,
};

static constexpr std::initializer_list<const char*> hk_tabAddonsMetroid2_labels =
{
    "[Metroid] (PgUp) AimSensitivity Up",
    "[Metroid] (PgDown) AimSensitivity Down",
    "[Metroid] (J) Next Weapon in the sorted order",
    "[Metroid] (K) Previous Weapon in the sorted order",
    "[Metroid] (C) Scan Visor",
    "[Metroid] (Z) UI Left (Adventure Left Arrow / Hunter License L)",
    "[Metroid] (X) UI Right (Adventure Right Arrow / Hunter License R)",
    "[Metroid] (F) UI Ok",
    "[Metroid] (G) UI Yes (Enter Starship)",
    "[Metroid] (H) UI No (Enter Starship)",
    "[Metroid] (Y) Weapon Check",
};

static_assert(hk_tabAddonsMetroid2.size() == hk_tabAddonsMetroid2_labels.size());

class MelonPrimeInputConfig : public QWidget
{
    Q_OBJECT

public:
    explicit MelonPrimeInputConfig(EmuInstance* emu, QWidget* parent = nullptr);
    ~MelonPrimeInputConfig();

    void saveConfig();
    QTabWidget* getTabWidget();

private slots:
    void on_metroidResetSensitivityValues_clicked();
    void on_metroidSetVideoQualityToLow_clicked();
    void on_metroidSetVideoQualityToHigh_clicked();
    void on_metroidSetVideoQualityToHigh2_clicked();

    void on_cbMetroidEnableSnapTap_stateChanged(int state);
    void on_cbMetroidUnlockAll_stateChanged(int state);
    void on_cbMetroidApplyHeadphone_stateChanged(int state);
    void on_cbMetroidUseFirmwareName_stateChanged(int state);

private:
    Ui::MelonPrimeInputConfig* ui;
    EmuInstance* emuInstance;

    int addonsMetroidKeyMap[hk_tabAddonsMetroid.size()];
    int addonsMetroidJoyMap[hk_tabAddonsMetroid.size()];

    int addonsMetroid2KeyMap[hk_tabAddonsMetroid2.size()];
    int addonsMetroid2JoyMap[hk_tabAddonsMetroid2.size()];

    void populatePage(QWidget* page, const std::initializer_list<const char*>& labels, int* keymap, int* joymap);
};

#endif // MELONPRIMEINPUTCONFIG_H