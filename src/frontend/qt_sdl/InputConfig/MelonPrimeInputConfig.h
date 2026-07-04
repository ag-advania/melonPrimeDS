/*
    Copyright 2016-2025 melonDS team

    This file is part of melonDS.
    (MelonPrime specific configuration extension)
*/

#ifndef MELONPRIMEINPUTCONFIG_H
#define MELONPRIMEINPUTCONFIG_H

#include <QWidget>
#include <QPointer>
#include <QList>
#include <QTabWidget>
#include <QPushButton>
#include <QVariantMap>
#include <unordered_map>
#include <vector>
#include <string>
#include <utility>
#include "EmuInstance.h"
#include "Config.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace Ui { class MelonPrimeInputConfig; }

// A hotkey binding: ID (from EmuInstance) paired with its display label.
struct HotkeyEntry
{
    int         id;
    const char* label;
};

static constexpr HotkeyEntry kMetroidHotkeys[] =
{
    {HK_MetroidMoveForward,         "[Metroid] (W) Move Forward"},
    {HK_MetroidMoveBack,            "[Metroid] (S) Move Back"},
    {HK_MetroidMoveLeft,            "[Metroid] (A) Move Left"},
    {HK_MetroidMoveRight,           "[Metroid] (D) Move Right"},
    {HK_MetroidShootScan,           "[Metroid] (Mouse Left) Shoot/Scan"},
    {HK_MetroidScanShoot,           "[Metroid] (V) Scan/Shoot, Map Zoom In"},
    {HK_MetroidZoom,                "[Metroid] (Mouse Right) Imperialist Zoom, Map Zoom Out, Morph Ball Boost"},
    {HK_MetroidJump,                "[Metroid] (Space) Jump"},
    {HK_MetroidMorphBall,           "[Metroid] (L. Ctrl) Transform"},
    {HK_MetroidHoldMorphBallBoost,  "[Metroid] (Shift) Hold to Fast Morph Ball Boost"},
    {HK_MetroidWeaponBeam,          "[Metroid] (Mouse 5, Side Top) Weapon Beam"},
    {HK_MetroidWeaponMissile,       "[Metroid] (Mouse 4, Side Bottom) Weapon Missile"},
    {HK_MetroidWeapon1,             "[Metroid] (1) Weapon 1. ShockCoil"},
    {HK_MetroidWeapon2,             "[Metroid] (2) Weapon 2. Magmaul"},
    {HK_MetroidWeapon3,             "[Metroid] (3) Weapon 3. Judicator"},
    {HK_MetroidWeapon4,             "[Metroid] (4) Weapon 4. Imperialist"},
    {HK_MetroidWeapon5,             "[Metroid] (5) Weapon 5. Battlehammer"},
    {HK_MetroidWeapon6,             "[Metroid] (6) Weapon 6. VoltDriver"},
    {HK_MetroidWeaponSpecial,       "[Metroid] (R) Affinity Weapon (Last used Weapon/Omega cannon)"},
    {HK_MetroidMenu,                "[Metroid] (Tab) Menu/Map"},
};

static constexpr HotkeyEntry kMetroidHotkeys2[] =
{
    {HK_MetroidIngameSensiUp,    "[Metroid] (PgUp) AimSensitivity Up"},
    {HK_MetroidIngameSensiDown,  "[Metroid] (PgDown) AimSensitivity Down"},
    {HK_MetroidWeaponNext,       "[Metroid] (J) Next Weapon in the sorted order"},
    {HK_MetroidWeaponPrevious,   "[Metroid] (K) Previous Weapon in the sorted order"},
    {HK_MetroidScanVisor,        "[Metroid] (C) Scan Visor"},
    {HK_MetroidUILeft,           "[Metroid] (Z) UI Left (Adventure Left Arrow / Hunter License L)"},
    {HK_MetroidUIRight,          "[Metroid] (X) UI Right (Adventure Right Arrow / Hunter License R)"},
    {HK_MetroidUIOk,             "[Metroid] (F) UI Ok"},
    {HK_MetroidUIYes,            "[Metroid] (G) UI Yes (Enter Starship)"},
    {HK_MetroidUINo,             "[Metroid] (H) UI No (Enter Starship)"},
    {HK_MetroidWeaponCheck,      "[Metroid] (Y) Weapon Check"},
};

// Compile-time counts for array sizing.
constexpr int kMetroidHotkeyCount  = static_cast<int>(sizeof(kMetroidHotkeys)  / sizeof(kMetroidHotkeys[0]));
constexpr int kMetroidHotkey2Count = static_cast<int>(sizeof(kMetroidHotkeys2) / sizeof(kMetroidHotkeys2[0]));

class MelonPrimeInputConfig : public QWidget
{
    Q_OBJECT

public:
    explicit MelonPrimeInputConfig(EmuInstance* emu, QWidget* parent = nullptr);
    ~MelonPrimeInputConfig();

    void saveConfig();
    void restoreVisualSnapshot();
    void snapshotVisualConfig();
    void refreshAfterHudEditSave();
    QTabWidget* getTabWidget();

protected:
    // Redirects wheel events on combo/spin/slider controls to the enclosing
    // scroll area when the control isn't focused, so scrolling the settings
    // page doesn't accidentally change a value the cursor happens to hover.
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void on_metroidResetSensitivityValues_clicked();
    void on_metroidSetVideoQualityToLow_clicked();
    void on_metroidSetVideoQualityToHigh_clicked();
    void on_metroidSetVideoQualityToHigh2_clicked();

    void on_cbMetroidEnableSnapTap_stateChanged(int state);
    void on_cbMetroidUnlockAll_stateChanged(int state);
    void on_cbMetroidApplyHeadphone_stateChanged(int state);
    void on_cbMetroidUseFirmwareName_stateChanged(int state);
    void on_cbMetroidEnableCustomHud_stateChanged(int state);
    void on_cbMetroidEnableStylusMode_stateChanged(int state);
    void on_cbMetroidDisableMphAimSmoothing_stateChanged(int state);
    void on_cbMetroidExpandStageMatrix_stateChanged(int state);
    void on_comboMetroidLowHpWarningMode_currentIndexChanged(int index);
    void on_btnEditHudLayout_clicked();
    void applyVisualPreview();

private:
    Ui::MelonPrimeInputConfig* ui;
    EmuInstance* emuInstance;

    // Enable only the spinboxes relevant to the selected Low HP Warning mode.
    void updateLowHpWarningControls(int mode);

    int addonsMetroidKeyMap[kMetroidHotkeyCount];
    int addonsMetroidJoyMap[kMetroidHotkeyCount];

    int addonsMetroid2KeyMap[kMetroidHotkey2Count];
    int addonsMetroid2JoyMap[kMetroidHotkey2Count];

    // Constructor setup helpers
    void setupKeyBindings(Config::Table& instcfg, Config::Table& keycfg, Config::Table& joycfg);
    void setupMenuLanguageControl(Config::Table& instcfg);
    void setupSensitivityAndToggles(Config::Table& instcfg);
    void setupInputMethodSection(Config::Table& instcfg);
    void setupCollapsibleSections(Config::Table& instcfg);
    void setupPreviewConnections();
    void setupCustomHudCode();
    void setupCustomHudWidgets(Config::Table& instcfg);
    // Install the wheel-event guard on every wheel-sensitive control so hovering
    // a combo/spin/slider while scrolling does not change its value.
    void installWheelScrollGuards();
    void updateAimControlsForStylusMode(bool stylusEnabled);

    // ── Non-HUD settings binding table (Phase 5b) ───────────────────────────
    // One row per symmetric simple setting; drives both load and save so the
    // two sides can never drift. Storage key names are unchanged.
    enum class SettingKind { CheckBool, ComboIndexInt, SpinInt, DoubleSpinDouble };
    struct SettingBinding { const char* key; SettingKind kind; QWidget* widget; };
    // Built once in setupSensitivityAndToggles (after setupUi) in code order.
    std::vector<SettingBinding> m_settingBindings;
    void buildSettingBindings();
    // Apply load over the half-open binding index range [begin, end).
    // Ranges preserve the original load order so interleaved special logic
    // (slot side-effects, parent/child enable wiring) stays in place.
    void loadBindingsRange(Config::Table& instcfg, int begin, int end);
    // Save every binding (order-independent; each writes one disjoint key).
    void saveBindings(Config::Table& instcfg);

    void populatePage(QWidget* page, const HotkeyEntry* entries, int count, int* keymap, int* joymap);
    QString buildCustomHudCode() const;
    bool applyCustomHudCode(const QString& code, QString* errorMessage = nullptr);
    void refreshCustomHudCodeOutput();
    void setCustomHudCodeStatus(const QString& text, bool isError);
    void invalidateHudAndRefreshPreviews();
    // True when anchor UI widgets from snapshotVisualConfig() are still alive
    // (tab pages may be reparented into InputConfigDialog; reject/destroy can
    // invalidate raw ui-> pointers without clearing m_visualSnapshot).
    bool visualSnapshotTargetsAlive() const;

    QVariantMap m_visualSnapshot;
    bool m_applyPreviewEnabled = false;
    bool m_applyPreviewActive = false;
    QPushButton* m_btnToggleInputMethod = nullptr;
    QWidget* m_sectionInputMethod = nullptr;
    QCheckBox* m_cbMetroidUseNewWeaponSwitchMethod = nullptr;
    QCheckBox* m_cbMetroidUseNewBipedFireMethod = nullptr;
    QCheckBox* m_cbMetroidUseNewTransformMethod = nullptr;
    QCheckBox* m_cbMetroidUseNewZoomMethod = nullptr;
    QCheckBox* m_cbMetroidUseNewZoomMethod2 = nullptr;
    QLabel* m_lblMetroidLowLatencyAimMode = nullptr;
    QComboBox* m_comboMetroidLowLatencyAimMode = nullptr;
    QLabel* m_lblMetroidLowLatencyAimDesc = nullptr;
    QCheckBox* m_cbMetroidZoomAimScaleEnable = nullptr;
    QLabel* m_lblMetroidZoomAimScalePct = nullptr;
    QSpinBox* m_spinMetroidZoomAimScalePct = nullptr;
    QLabel* m_lblMetroidZoomAimScaleDesc = nullptr;
    QWidget* m_menuLanguageGroup = nullptr;
    QLabel* m_lblMenuLanguage = nullptr;
    QComboBox* m_comboMenuLanguage = nullptr;

    // Programmatic HUD settings widgets (config key → widget; QPointer for cancel/destroy safety)
    std::unordered_map<std::string, QPointer<QWidget>> m_hudWidgets;
    // Section toggle buttons (button, config key for collapse state)
    std::vector<std::pair<QPushButton*, std::string>> m_hudToggles;
    // Preview widgets that need refresh on config change
    QList<QWidget*> m_hudPreviews;
};

#endif // MELONPRIMEINPUTCONFIG_H

