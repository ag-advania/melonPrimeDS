/*
    Copyright 2016-2025 melonDS team
    (MelonPrime specific configuration extension)
*/

#include <QGroupBox>
#include <QLabel>
#include <QGridLayout>
#include <QTabWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QTimer>
#include <QGuiApplication>
#include <QClipboard>
#include <QFormLayout>
#include <QCheckBox>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QSlider>
#include <QPainter>
#include <algorithm>
#include <sstream>

#include "MelonPrimeInputConfig.h"
#include "MelonPrimeInputConfigInternal.h"
#include "ui_MelonPrimeInputConfig.h"
#include "Config.h"
#include "toml/toml.hpp"

// InputConfigDialog must be fully defined before including MapButton.h.
// MapButton accesses parentDialog directly, so a forward declaration is not enough.
#include "InputConfigDialog.h" 

#include "MapButton.h"
#include "Platform.h"
#include "VideoSettingsDialog.h"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeCustomHud.h"
#endif

using namespace melonDS;

MelonPrimeInputConfig::MelonPrimeInputConfig(EmuInstance* emu, QWidget* parent) :
    QWidget(parent),
    ui(new Ui::MelonPrimeInputConfig),
    emuInstance(emu)
{
    ui->setupUi(this);

    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    setupKeyBindings(instcfg, keycfg, joycfg);
    setupSensitivityAndToggles(instcfg);
    setupCollapsibleSections(instcfg);
    setupCustomHudWidgets(instcfg);
    setupPreviewConnections();
    setupCustomHudCode();

    snapshotVisualConfig();

    m_applyPreviewEnabled = true;
}



void MelonPrimeInputConfig::setupKeyBindings(Config::Table& instcfg, Config::Table& keycfg, Config::Table& joycfg)
{
    for (int i = 0; i < kMetroidHotkeyCount; ++i)
    {
        const char* btn = EmuInstance::hotkeyNames[kMetroidHotkeys[i].id];
        addonsMetroidKeyMap[i] = keycfg.GetInt(btn);
        addonsMetroidJoyMap[i] = joycfg.GetInt(btn);
    }

    for (int i = 0; i < kMetroidHotkey2Count; ++i)
    {
        const char* btn = EmuInstance::hotkeyNames[kMetroidHotkeys2[i].id];
        addonsMetroid2KeyMap[i] = keycfg.GetInt(btn);
        addonsMetroid2JoyMap[i] = joycfg.GetInt(btn);
    }

    populatePage(ui->tabAddonsMetroid,  kMetroidHotkeys,  kMetroidHotkeyCount,  addonsMetroidKeyMap,  addonsMetroidJoyMap);
    populatePage(ui->tabAddonsMetroid2, kMetroidHotkeys2, kMetroidHotkey2Count, addonsMetroid2KeyMap, addonsMetroid2JoyMap);
}

void MelonPrimeInputConfig::setupSensitivityAndToggles(Config::Table& instcfg)
{
    // Sensitivities
    ui->metroidMphSensitvitySpinBox->setValue(instcfg.GetDouble("Metroid.Sensitivity.Mph"));
    ui->metroidAimSensitvitySpinBox->setValue(instcfg.GetInt("Metroid.Sensitivity.Aim"));
    ui->metroidAimYAxisScaleSpinBox->setValue(instcfg.GetDouble("Metroid.Sensitivity.AimYAxisScale"));
    ui->metroidAimAdjustSpinBox->setValue(instcfg.GetDouble("Metroid.Aim.Adjust"));

    // Toggles
    ui->cbMetroidEnableSnapTap->setChecked(instcfg.GetBool("Metroid.Operation.SnapTap"));
    ui->cbMetroidUnlockAll->setChecked(instcfg.GetBool("Metroid.Data.Unlock"));
    ui->cbMetroidApplyHeadphone->setChecked(instcfg.GetBool("Metroid.Apply.Headphone"));
    ui->cbMetroidUseFirmwareName->setChecked(instcfg.GetBool("Metroid.Use.Firmware.Name"));

    // Hunter license
    ui->cbMetroidApplyHunter->setChecked(instcfg.GetBool("Metroid.HunterLicense.Hunter.Apply"));
    ui->comboMetroidSelectedHunter->setCurrentIndex(
        instcfg.GetInt("Metroid.HunterLicense.Hunter.Selected"));

    ui->cbMetroidApplyColor->setChecked(instcfg.GetBool("Metroid.HunterLicense.Color.Apply"));
    ui->comboMetroidSelectedColor->setCurrentIndex(
        instcfg.GetInt("Metroid.HunterLicense.Color.Selected"));

    // Volume
    ui->cbMetroidApplySfxVolume->setChecked(instcfg.GetBool("Metroid.Apply.SfxVolume"));
    ui->spinMetroidVolumeSFX->setValue(instcfg.GetInt("Metroid.Volume.SFX"));

    ui->cbMetroidApplyMusicVolume->setChecked(instcfg.GetBool("Metroid.Apply.MusicVolume"));
    ui->spinMetroidVolumeMusic->setValue(instcfg.GetInt("Metroid.Volume.Music"));

    // Other Metroid Settings 2 Tab
    ui->cbMetroidApplyJoy2KeySupport->setChecked(instcfg.GetBool("Metroid.Apply.joy2KeySupport"));
    ui->cbMetroidEnableStylusMode->setChecked(instcfg.GetBool("Metroid.Enable.stylusMode"));
    ui->cbMetroidDisableMphAimSmoothing->setChecked(instcfg.GetBool("Metroid.Aim.Disable.MphAimSmoothing"));
    ui->cbMetroidEnableAimAccumulator->setChecked(instcfg.GetBool("Metroid.Aim.Enable.Accumulator"));

    // Screen Sync Mode
    ui->comboMetroidScreenSyncMode->setCurrentIndex(instcfg.GetInt("Metroid.Screen.SyncMode"));
    ui->cbMetroidClipCursorToBottomScreenWhenNotInGame->setChecked(instcfg.GetBool("Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame"));
    ui->cbMetroidInGameTopScreenOnly->setChecked(instcfg.GetBool("Metroid.Visual.InGameTopScreenOnly"));

    // In-game scaling
    ui->cbMetroidInGameAspectRatio->setChecked(instcfg.GetBool("Metroid.Visual.InGameAspectRatio"));
    ui->comboMetroidInGameAspectRatioMode->setCurrentIndex(instcfg.GetInt("Metroid.Visual.InGameAspectRatioMode"));
}


void MelonPrimeInputConfig::setupCollapsibleSections(Config::Table& instcfg)
{
    // Custom HUD
    ui->cbMetroidEnableCustomHud->setChecked(instcfg.GetBool("Metroid.Visual.CustomHUD"));

    // --- Collapsible sections: remember expand/collapse state ---
    auto setupToggle = [&instcfg](QPushButton* btn, QWidget* section, const QString& label, const char* cfgKey) {
        bool expanded = instcfg.GetBool(cfgKey);
        section->setVisible(expanded);
        btn->setChecked(expanded);
        btn->setText((expanded ? QString::fromUtf8("▼ ") : QString::fromUtf8("▶ ")) + label);
        QObject::connect(btn, &QPushButton::toggled, [btn, section, label](bool checked) {
            section->setVisible(checked);
            btn->setText((checked ? QString::fromUtf8("▼ ") : QString::fromUtf8("▶ ")) + label);
        });
    };
    // Other Metroid Settings 2 tab
    setupToggle(ui->btnToggleInputSettings, ui->sectionInputSettings, "INPUT SETTINGS",   "Metroid.UI.SectionInputSettings");
    setupToggle(ui->btnToggleScreenSync,    ui->sectionScreenSync,    "SCREEN SYNC",      "Metroid.UI.SectionScreenSync");
    setupToggle(ui->btnToggleCursorClipSettings, ui->sectionCursorClipSettings, "CURSOR CLIP SETTINGS",  "Metroid.UI.SectionCursorClipSettings");
    setupToggle(ui->btnToggleInGameApply, ui->sectionInGameApply, "IN-GAME APPLY",  "Metroid.UI.SectionInGameApply");
    setupToggle(ui->btnToggleInGameAspectRatio, ui->sectionInGameAspectRatio, "IN-GAME ASPECT RATIO",  "Metroid.UI.SectionInGameAspectRatio");
    // Other Metroid Settings tab
    setupToggle(ui->btnToggleSensitivity, ui->sectionSensitivity, "SENSITIVITY",      "Metroid.UI.SectionSensitivity");
    setupToggle(ui->btnToggleGameplay,    ui->sectionGameplay,    "GAMEPLAY TOGGLES", "Metroid.UI.SectionGameplay");
    setupToggle(ui->btnToggleVideo,       ui->sectionVideo,       "VIDEO QUALITY",    "Metroid.UI.SectionVideo");
    setupToggle(ui->btnToggleVolume,      ui->sectionVolume,      "VOLUME",           "Metroid.UI.SectionVolume");
    setupToggle(ui->btnToggleLicense,     ui->sectionLicense,     "LICENSE APPLY",    "Metroid.UI.SectionLicense");
}


void MelonPrimeInputConfig::setupPreviewConnections()
{
    // --- Global (affects visual preview) ---
    connect(ui->cbMetroidEnableCustomHud, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) { applyVisualPreview(); });
    connect(ui->cbMetroidInGameAspectRatio, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) { applyVisualPreview(); });
    connect(ui->comboMetroidInGameAspectRatioMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyVisualPreview(); });
}


MelonPrimeInputConfig::~MelonPrimeInputConfig()
{
    delete ui;
}

QTabWidget* MelonPrimeInputConfig::getTabWidget()
{
    return ui->metroidTabWidget;
}

void MelonPrimeInputConfig::populatePage(QWidget* page, const HotkeyEntry* entries, int count, int* keymap, int* joymap)
{
    // This logic is copied from InputConfigDialog::populatePage but adapted for local use
    bool ishotkey = true;

    QHBoxLayout* main_layout = new QHBoxLayout();
    QGroupBox* group;
    QGridLayout* group_layout;

    group = new QGroupBox("Keyboard mappings:");
    main_layout->addWidget(group);
    group_layout = new QGridLayout();
    group_layout->setSpacing(1);
    for (int i = 0; i < count; ++i)
    {
        group_layout->addWidget(new QLabel(QString(entries[i].label) + ":"), i, 0);
        group_layout->addWidget(new KeyMapButton(&keymap[i], ishotkey), i, 1);
    }
    group_layout->setRowStretch(count, 1);
    group->setLayout(group_layout);
    group->setMinimumWidth(275);

    group = new QGroupBox("Joystick mappings:");
    main_layout->addWidget(group);
    group_layout = new QGridLayout();
    group_layout->setSpacing(1);
    for (int i = 0; i < count; ++i)
    {
        group_layout->addWidget(new QLabel(QString(entries[i].label) + ":"), i, 0);
        group_layout->addWidget(new JoyMapButton(&joymap[i], ishotkey), i, 1);
    }
    group_layout->setRowStretch(count, 1);
    group->setLayout(group_layout);
    group->setMinimumWidth(275);

    page->setLayout(main_layout);
}

void MelonPrimeInputConfig::on_metroidResetSensitivityValues_clicked()
{
    ui->metroidMphSensitvitySpinBox->setValue(-3);
    ui->metroidAimSensitvitySpinBox->setValue(63);
    ui->metroidAimYAxisScaleSpinBox->setValue(1.500000);
    ui->metroidAimAdjustSpinBox->setValue(0.010000);
}

void MelonPrimeInputConfig::on_metroidSetVideoQualityToLow_clicked()
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.UseGL", true);
    cfg.SetBool("Screen.VSync", false);
    cfg.SetInt("Screen.VSyncInterval", 1);
    cfg.SetInt("3D.Renderer", renderer3D_Software);
    cfg.SetBool("3D.Soft.Threaded", true);
    cfg.SetInt("3D.GL.ScaleFactor", 4);
    cfg.SetBool("3D.GL.BetterPolygons", true);
}

void MelonPrimeInputConfig::on_metroidSetVideoQualityToHigh_clicked()
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.UseGL", true);
    cfg.SetBool("Screen.VSync", false);
    cfg.SetInt("Screen.VSyncInterval", 1);
    cfg.SetInt("3D.Renderer", renderer3D_OpenGL);
    cfg.SetBool("3D.Soft.Threaded", true);
    cfg.SetInt("3D.GL.ScaleFactor", 4);
    cfg.SetBool("3D.GL.BetterPolygons", true);
}

void MelonPrimeInputConfig::on_metroidSetVideoQualityToHigh2_clicked()
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.UseGL", true);
    cfg.SetBool("Screen.VSync", false);
    cfg.SetInt("Screen.VSyncInterval", 1);
    cfg.SetInt("3D.Renderer", renderer3D_OpenGLCompute);
    cfg.SetBool("3D.Soft.Threaded", true);
    cfg.SetInt("3D.GL.ScaleFactor", 4);
    cfg.SetBool("3D.GL.BetterPolygons", true);
}

void MelonPrimeInputConfig::on_cbMetroidEnableSnapTap_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Metroid.Operation.SnapTap", state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidUnlockAll_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Metroid.Data.Unlock", state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidApplyHeadphone_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Metroid.Apply.Headphone", state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidUseFirmwareName_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Metroid.Use.Firmware.Name", state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidEnableCustomHud_stateChanged(int state)
{
    auto& cfg = emuInstance->getLocalConfig();
    cfg.SetBool("Metroid.Visual.CustomHUD", state != 0);
}

void MelonPrimeInputConfig::on_btnEditHudLayout_clicked()
{
    applyVisualPreview();
#ifdef MELONPRIME_CUSTOM_HUD
    MelonPrime::CustomHud_EnterEditMode(emuInstance, emuInstance->getLocalConfig());
#endif
    if (InputConfigDialog::currentDlg)
        InputConfigDialog::currentDlg->hide();
}


// ── HUD Widget Descriptors ──────────────────────────────────────────────────

namespace {

enum class HWType { Bool, Int, Float, String, Anchor9, Align3, Color3 };

struct HudWidgetProp {
    const char* label;
    HWType      type;
    const char* cfgKey;
    int         min, max, step;
    const char* cfgKeyG;   // Color3 only
    const char* cfgKeyB;   // Color3 only
};

struct HudSubSec {
    const char* title;
    const char* cfgToggleKey;
    const HudWidgetProp* props;
    int propCount;
    const HudSubSec* children;   // nested sub-sections (e.g., Rank/Time inside "Rank / Time")
    int childCount;
};

struct HudMainSec {
    const char* title;
    const char* cfgToggleKey;
    const HudWidgetProp* directProps;   // props shown at group level (before sub-sections)
    int directPropCount;
    const HudSubSec* subs;
    int subCount;
    int previewKind; // 0=None, 1=Crosshair, 2=HpAmmo, 3=MatchStatus, 4=Radar
};

#define P_BOOL(lbl, key)              { lbl, HWType::Bool,   key, 0,0,0, nullptr, nullptr }
#define P_INT(lbl, key, lo, hi, s)    { lbl, HWType::Int,    key, lo, hi, s, nullptr, nullptr }
#define P_FLOAT(lbl, key)             { lbl, HWType::Float,  key, 0,100,5, nullptr, nullptr }
#define P_STR(lbl, key)               { lbl, HWType::String, key, 0,0,0, nullptr, nullptr }
#define P_ANC(lbl, key)               { lbl, HWType::Anchor9,key, 0,8,1, nullptr, nullptr }
#define P_ALN(lbl, key)               { lbl, HWType::Align3, key, 0,2,1, nullptr, nullptr }
#define P_CLR(lbl, kR, kG, kB)        { lbl, HWType::Color3, kR, 0,255,1, kG, kB }

// --- Section 1: Text Scale ---
static const HudWidgetProp kSecTextScale[] = {
    P_INT("Text Scale %", "Metroid.Visual.HudTextScale", 10, 300, 10),
};

// --- Section 2: Crosshair ---
static const HudWidgetProp kSecCrosshair[] = {
    P_CLR("Color", "Metroid.Visual.CrosshairColorR", "Metroid.Visual.CrosshairColorG", "Metroid.Visual.CrosshairColorB"),
    P_BOOL("Outline", "Metroid.Visual.CrosshairOutline"),
    P_FLOAT("Outline Opacity", "Metroid.Visual.CrosshairOutlineOpacity"),
    P_INT("Outline Thickness", "Metroid.Visual.CrosshairOutlineThickness", 1, 10, 1),
    P_BOOL("Center Dot", "Metroid.Visual.CrosshairCenterDot"),
    P_FLOAT("Dot Opacity", "Metroid.Visual.CrosshairDotOpacity"),
    P_INT("Dot Thickness", "Metroid.Visual.CrosshairDotThickness", 1, 10, 1),
    P_BOOL("T-Style", "Metroid.Visual.CrosshairTStyle"),
};

// --- Section 3: Crosshair Inner ---
static const HudWidgetProp kSecCrosshairInner[] = {
    P_BOOL("Show", "Metroid.Visual.CrosshairInnerShow"),
    P_FLOAT("Opacity", "Metroid.Visual.CrosshairInnerOpacity"),
    P_INT("Length X", "Metroid.Visual.CrosshairInnerLengthX", 0, 64, 1),
    P_INT("Length Y", "Metroid.Visual.CrosshairInnerLengthY", 0, 64, 1),
    P_BOOL("Link XY", "Metroid.Visual.CrosshairInnerLinkXY"),
    P_INT("Thickness", "Metroid.Visual.CrosshairInnerThickness", 1, 10, 1),
    P_INT("Offset", "Metroid.Visual.CrosshairInnerOffset", 0, 64, 1),
};

// --- Section 4: Crosshair Outer ---
static const HudWidgetProp kSecCrosshairOuter[] = {
    P_BOOL("Show", "Metroid.Visual.CrosshairOuterShow"),
    P_FLOAT("Opacity", "Metroid.Visual.CrosshairOuterOpacity"),
    P_INT("Length X", "Metroid.Visual.CrosshairOuterLengthX", 0, 64, 1),
    P_INT("Length Y", "Metroid.Visual.CrosshairOuterLengthY", 0, 64, 1),
    P_BOOL("Link XY", "Metroid.Visual.CrosshairOuterLinkXY"),
    P_INT("Thickness", "Metroid.Visual.CrosshairOuterThickness", 1, 10, 1),
    P_INT("Offset", "Metroid.Visual.CrosshairOuterOffset", 0, 64, 1),
};

// --- Section 5: HP ---
static const HudWidgetProp kSecHp[] = {
    P_ANC("Anchor", "Metroid.Visual.HudHpAnchor"),
    P_INT("Offset X", "Metroid.Visual.HudHpX", -256, 256, 1),
    P_INT("Offset Y", "Metroid.Visual.HudHpY", -256, 256, 1),
    P_STR("Prefix", "Metroid.Visual.HudHpPrefix"),
    P_ALN("Align", "Metroid.Visual.HudHpAlign"),
    P_BOOL("Auto Color", "Metroid.Visual.HudHpTextAutoColor"),
    P_CLR("Color", "Metroid.Visual.HudHpTextColorR", "Metroid.Visual.HudHpTextColorG", "Metroid.Visual.HudHpTextColorB"),
    P_FLOAT("Opacity", "Metroid.Visual.HudHpOpacity"),
};

// --- Section 6: HP Gauge ---
static const HudWidgetProp kSecHpGauge[] = {
    P_BOOL("Enable", "Metroid.Visual.HudHpGauge"),
    P_BOOL("Auto Color", "Metroid.Visual.HudHpGaugeAutoColor"),
    P_INT("Orientation", "Metroid.Visual.HudHpGaugeOrientation", 0, 1, 1),
    P_INT("Length", "Metroid.Visual.HudHpGaugeLength", 1, 192, 1),
    P_INT("Width", "Metroid.Visual.HudHpGaugeWidth", 1, 20, 1),
    P_INT("Offset X", "Metroid.Visual.HudHpGaugeOffsetX", -128, 128, 1),
    P_INT("Offset Y", "Metroid.Visual.HudHpGaugeOffsetY", -128, 128, 1),
    P_CLR("Color", "Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB"),
    P_INT("Gauge Anchor", "Metroid.Visual.HudHpGaugeAnchor", 0, 4, 1),
    P_INT("Pos Mode", "Metroid.Visual.HudHpGaugePosMode", 0, 1, 1),
    P_ANC("Pos Anchor", "Metroid.Visual.HudHpGaugePosAnchor"),
    P_INT("Pos X", "Metroid.Visual.HudHpGaugePosX", -256, 256, 1),
    P_INT("Pos Y", "Metroid.Visual.HudHpGaugePosY", -256, 256, 1),
    P_FLOAT("Opacity", "Metroid.Visual.HudHpGaugeOpacity"),
};

// --- Section 7: Weapon / Ammo ---
static const HudWidgetProp kSecWeaponAmmo[] = {
    P_ANC("Anchor", "Metroid.Visual.HudWeaponAnchor"),
    P_INT("Offset X", "Metroid.Visual.HudWeaponX", -256, 256, 1),
    P_INT("Offset Y", "Metroid.Visual.HudWeaponY", -256, 256, 1),
    P_STR("Prefix", "Metroid.Visual.HudAmmoPrefix"),
    P_ALN("Align", "Metroid.Visual.HudAmmoAlign"),
    P_CLR("Color", "Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB"),
    P_INT("Weapon Layout", "Metroid.Visual.HudWeaponLayout", 0, 1, 1),
    P_FLOAT("Opacity", "Metroid.Visual.HudWeaponOpacity"),
};

// --- Section 8: Weapon Icon ---
static const HudWidgetProp kSecWpnIcon[] = {
    P_BOOL("Show", "Metroid.Visual.HudWeaponIconShow"),
    P_ANC("Pos Anchor", "Metroid.Visual.HudWeaponIconPosAnchor"),
    P_INT("Pos X", "Metroid.Visual.HudWeaponIconPosX", -256, 256, 1),
    P_INT("Pos Y", "Metroid.Visual.HudWeaponIconPosY", -256, 256, 1),
    P_INT("Icon Mode", "Metroid.Visual.HudWeaponIconMode", 0, 1, 1),
    P_BOOL("Color Overlay", "Metroid.Visual.HudWeaponIconColorOverlay"),
    P_INT("Icon Offset X", "Metroid.Visual.HudWeaponIconOffsetX", -128, 128, 1),
    P_INT("Icon Offset Y", "Metroid.Visual.HudWeaponIconOffsetY", -128, 128, 1),
    P_INT("Icon Anchor X", "Metroid.Visual.HudWeaponIconAnchorX", 0, 2, 1),
    P_INT("Icon Anchor Y", "Metroid.Visual.HudWeaponIconAnchorY", 0, 2, 1),
    P_FLOAT("Opacity", "Metroid.Visual.HudWpnIconOpacity"),
};

// --- Section 9: Ammo Gauge ---
static const HudWidgetProp kSecAmmoGauge[] = {
    P_BOOL("Enable", "Metroid.Visual.HudAmmoGauge"),
    P_INT("Orientation", "Metroid.Visual.HudAmmoGaugeOrientation", 0, 1, 1),
    P_INT("Length", "Metroid.Visual.HudAmmoGaugeLength", 1, 192, 1),
    P_INT("Width", "Metroid.Visual.HudAmmoGaugeWidth", 1, 20, 1),
    P_INT("Offset X", "Metroid.Visual.HudAmmoGaugeOffsetX", -128, 128, 1),
    P_INT("Offset Y", "Metroid.Visual.HudAmmoGaugeOffsetY", -128, 128, 1),
    P_CLR("Color", "Metroid.Visual.HudAmmoGaugeColorR", "Metroid.Visual.HudAmmoGaugeColorG", "Metroid.Visual.HudAmmoGaugeColorB"),
    P_INT("Gauge Anchor", "Metroid.Visual.HudAmmoGaugeAnchor", 0, 4, 1),
    P_INT("Pos Mode", "Metroid.Visual.HudAmmoGaugePosMode", 0, 1, 1),
    P_ANC("Pos Anchor", "Metroid.Visual.HudAmmoGaugePosAnchor"),
    P_INT("Pos X", "Metroid.Visual.HudAmmoGaugePosX", -256, 256, 1),
    P_INT("Pos Y", "Metroid.Visual.HudAmmoGaugePosY", -256, 256, 1),
    P_FLOAT("Opacity", "Metroid.Visual.HudAmmoGaugeOpacity"),
};

// --- Section 10: Match Status ---
static const HudWidgetProp kSecMatchStatus[] = {
    P_BOOL("Show", "Metroid.Visual.HudMatchStatusShow"),
    P_ANC("Anchor", "Metroid.Visual.HudMatchStatusAnchor"),
    P_INT("Offset X", "Metroid.Visual.HudMatchStatusX", -256, 256, 1),
    P_INT("Offset Y", "Metroid.Visual.HudMatchStatusY", -256, 256, 1),
    P_CLR("Color", "Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB"),
    P_INT("Label Pos", "Metroid.Visual.HudMatchStatusLabelPos", 0, 3, 1),
    P_INT("Label Offset X", "Metroid.Visual.HudMatchStatusLabelOfsX", -64, 64, 1),
    P_INT("Label Offset Y", "Metroid.Visual.HudMatchStatusLabelOfsY", -64, 64, 1),
    P_STR("Label: Points", "Metroid.Visual.HudMatchStatusLabelPoints"),
    P_STR("Label: Octoliths", "Metroid.Visual.HudMatchStatusLabelOctoliths"),
    P_STR("Label: Lives", "Metroid.Visual.HudMatchStatusLabelLives"),
    P_STR("Label: Ring Time", "Metroid.Visual.HudMatchStatusLabelRingTime"),
    P_STR("Label: Prime Time", "Metroid.Visual.HudMatchStatusLabelPrimeTime"),
    P_BOOL("Label Color: Overall", "Metroid.Visual.HudMatchStatusLabelColorOverall"),
    P_CLR("Label Color", "Metroid.Visual.HudMatchStatusLabelColorR", "Metroid.Visual.HudMatchStatusLabelColorG", "Metroid.Visual.HudMatchStatusLabelColorB"),
    P_BOOL("Value Color: Overall", "Metroid.Visual.HudMatchStatusValueColorOverall"),
    P_CLR("Value Color", "Metroid.Visual.HudMatchStatusValueColorR", "Metroid.Visual.HudMatchStatusValueColorG", "Metroid.Visual.HudMatchStatusValueColorB"),
    P_BOOL("Sep Color: Overall", "Metroid.Visual.HudMatchStatusSepColorOverall"),
    P_CLR("Sep Color", "Metroid.Visual.HudMatchStatusSepColorR", "Metroid.Visual.HudMatchStatusSepColorG", "Metroid.Visual.HudMatchStatusSepColorB"),
    P_BOOL("Goal Color: Overall", "Metroid.Visual.HudMatchStatusGoalColorOverall"),
    P_CLR("Goal Color", "Metroid.Visual.HudMatchStatusGoalColorR", "Metroid.Visual.HudMatchStatusGoalColorG", "Metroid.Visual.HudMatchStatusGoalColorB"),
    P_FLOAT("Opacity", "Metroid.Visual.HudMatchStatusOpacity"),
};

// --- Section 11: Rank ---
static const HudWidgetProp kSecRank[] = {
    P_BOOL("Show", "Metroid.Visual.HudRankShow"),
    P_ANC("Anchor", "Metroid.Visual.HudRankAnchor"),
    P_INT("Offset X", "Metroid.Visual.HudRankX", -256, 256, 1),
    P_INT("Offset Y", "Metroid.Visual.HudRankY", -256, 256, 1),
    P_ALN("Align", "Metroid.Visual.HudRankAlign"),
    P_CLR("Color", "Metroid.Visual.HudRankColorR", "Metroid.Visual.HudRankColorG", "Metroid.Visual.HudRankColorB"),
    P_STR("Prefix", "Metroid.Visual.HudRankPrefix"),
    P_STR("Suffix", "Metroid.Visual.HudRankSuffix"),
    P_BOOL("Ordinal", "Metroid.Visual.HudRankShowOrdinal"),
    P_FLOAT("Opacity", "Metroid.Visual.HudRankOpacity"),
};

// --- Section 12: Time Left ---
static const HudWidgetProp kSecTimeLeft[] = {
    P_BOOL("Show", "Metroid.Visual.HudTimeLeftShow"),
    P_ANC("Anchor", "Metroid.Visual.HudTimeLeftAnchor"),
    P_INT("Offset X", "Metroid.Visual.HudTimeLeftX", -256, 256, 1),
    P_INT("Offset Y", "Metroid.Visual.HudTimeLeftY", -256, 256, 1),
    P_ALN("Align", "Metroid.Visual.HudTimeLeftAlign"),
    P_CLR("Color", "Metroid.Visual.HudTimeLeftColorR", "Metroid.Visual.HudTimeLeftColorG", "Metroid.Visual.HudTimeLeftColorB"),
    P_FLOAT("Opacity", "Metroid.Visual.HudTimeLeftOpacity"),
};

// --- Section 13: Time Limit ---
static const HudWidgetProp kSecTimeLimit[] = {
    P_BOOL("Show", "Metroid.Visual.HudTimeLimitShow"),
    P_ANC("Anchor", "Metroid.Visual.HudTimeLimitAnchor"),
    P_INT("Offset X", "Metroid.Visual.HudTimeLimitX", -256, 256, 1),
    P_INT("Offset Y", "Metroid.Visual.HudTimeLimitY", -256, 256, 1),
    P_ALN("Align", "Metroid.Visual.HudTimeLimitAlign"),
    P_CLR("Color", "Metroid.Visual.HudTimeLimitColorR", "Metroid.Visual.HudTimeLimitColorG", "Metroid.Visual.HudTimeLimitColorB"),
    P_FLOAT("Opacity", "Metroid.Visual.HudTimeLimitOpacity"),
};

// --- Section 14: Bomb Left ---
static const HudWidgetProp kSecBombLeft[] = {
    P_BOOL("Show", "Metroid.Visual.HudBombLeftShow"),
    P_ANC("Anchor", "Metroid.Visual.HudBombLeftAnchor"),
    P_INT("Offset X", "Metroid.Visual.HudBombLeftX", -256, 256, 1),
    P_INT("Offset Y", "Metroid.Visual.HudBombLeftY", -256, 256, 1),
    P_ALN("Align", "Metroid.Visual.HudBombLeftAlign"),
    P_CLR("Color", "Metroid.Visual.HudBombLeftColorR", "Metroid.Visual.HudBombLeftColorG", "Metroid.Visual.HudBombLeftColorB"),
    P_BOOL("Show Text", "Metroid.Visual.HudBombLeftTextShow"),
    P_STR("Prefix", "Metroid.Visual.HudBombLeftPrefix"),
    P_STR("Suffix", "Metroid.Visual.HudBombLeftSuffix"),
    P_FLOAT("Opacity", "Metroid.Visual.HudBombLeftOpacity"),
};

// --- Section 15: Bomb Icon ---
static const HudWidgetProp kSecBombIcon[] = {
    P_BOOL("Show", "Metroid.Visual.HudBombLeftIconShow"),
    P_ANC("Pos Anchor", "Metroid.Visual.HudBombLeftIconPosAnchor"),
    P_INT("Pos X", "Metroid.Visual.HudBombLeftIconPosX", -256, 256, 1),
    P_INT("Pos Y", "Metroid.Visual.HudBombLeftIconPosY", -256, 256, 1),
    P_INT("Icon Mode", "Metroid.Visual.HudBombLeftIconMode", 0, 1, 1),
    P_BOOL("Color Overlay", "Metroid.Visual.HudBombLeftIconColorOverlay"),
    P_CLR("Icon Color", "Metroid.Visual.HudBombLeftIconColorR", "Metroid.Visual.HudBombLeftIconColorG", "Metroid.Visual.HudBombLeftIconColorB"),
    P_INT("Icon Offset X", "Metroid.Visual.HudBombLeftIconOfsX", -128, 128, 1),
    P_INT("Icon Offset Y", "Metroid.Visual.HudBombLeftIconOfsY", -128, 128, 1),
    P_INT("Icon Anchor X", "Metroid.Visual.HudBombLeftIconAnchorX", 0, 2, 1),
    P_INT("Icon Anchor Y", "Metroid.Visual.HudBombLeftIconAnchorY", 0, 2, 1),
    P_FLOAT("Opacity", "Metroid.Visual.HudBombIconOpacity"),
};

// --- Section 16: Radar ---
static const HudWidgetProp kSecRadar[] = {
    P_BOOL("Enable", "Metroid.Visual.BtmOverlayEnable"),
    P_ANC("Anchor", "Metroid.Visual.BtmOverlayAnchor"),
    P_INT("Dst X", "Metroid.Visual.BtmOverlayDstX", -256, 256, 1),
    P_INT("Dst Y", "Metroid.Visual.BtmOverlayDstY", -256, 256, 1),
    P_INT("Dst Size", "Metroid.Visual.BtmOverlayDstSize", 16, 128, 1),
    P_INT("Src Radius", "Metroid.Visual.BtmOverlaySrcRadius", 10, 96, 1),
    P_FLOAT("Opacity", "Metroid.Visual.BtmOverlayOpacity"),
};

#define _P(arr) arr, static_cast<int>(sizeof(arr)/sizeof(arr[0]))
#define SUB(title, key, arr)               { title, key, _P(arr), nullptr, 0 }
#define SUB_NEST(title, key, ch)           { title, key, nullptr, 0, ch, static_cast<int>(sizeof(ch)/sizeof(ch[0])) }

// ── CROSSHAIR sub-sections ──
static const HudSubSec kSubsCrosshair[] = {
    SUB("Inner Lines",  "Metroid.UI.SectionHudCrosshairInner",  kSecCrosshairInner),
    SUB("Outer Lines",  "Metroid.UI.SectionHudCrosshairOuter",  kSecCrosshairOuter),
};

// ── HP / AMMO sub-sections ──
static const HudSubSec kSubsHpAmmo[] = {
    SUB("HP Number Position",     "Metroid.UI.SectionHudHp",         kSecHp),
    SUB("Ammo Number Position",   "Metroid.UI.SectionHudWeaponAmmo", kSecWeaponAmmo),
    SUB("Weapon Icon",            "Metroid.UI.SectionHudWpnIcon",    kSecWpnIcon),
    SUB("HP Gauge",               "Metroid.UI.SectionHudHpGauge",    kSecHpGauge),
    SUB("Ammo Gauge",             "Metroid.UI.SectionHudAmmoGauge",  kSecAmmoGauge),
};

// ── Rank/Time sub-sub-sections ──
static const HudSubSec kSubsRankTime[] = {
    SUB("Rank",       "Metroid.UI.SectionHudRank",      kSecRank),
    SUB("Time Left",  "Metroid.UI.SectionHudTimeLeft",  kSecTimeLeft),
    SUB("Time Limit", "Metroid.UI.SectionHudTimeLimit", kSecTimeLimit),
};

// ── MATCH STATUS HUD sub-sections ──
static const HudSubSec kSubsMatchStatus[] = {
    SUB("Score",                   "Metroid.UI.SectionHudMatchStatus", kSecMatchStatus),
    SUB_NEST("Rank / Time",      "Metroid.UI.SectionHudRankTime",   kSubsRankTime),
    SUB("Bomb Left",               "Metroid.UI.SectionHudBombLeft",   kSecBombLeft),
    SUB("Bomb Icon",               "Metroid.UI.SectionHudBombIcon",   kSecBombIcon),
};

// ── Main section groups ──
static const HudMainSec kHudMainSections[] = {
    { "TEXT SCALE",        "Metroid.UI.SectionHudTextScale",
      _P(kSecTextScale), nullptr, 0, /*preview*/ 0 },
    { "CROSSHAIR",         "Metroid.UI.SectionHudCrosshair",
      _P(kSecCrosshair), kSubsCrosshair, static_cast<int>(sizeof(kSubsCrosshair)/sizeof(kSubsCrosshair[0])), /*preview*/ 1 },
    { "HP / AMMO",         "Metroid.UI.SectionHudHpAmmo",
      nullptr, 0, kSubsHpAmmo, static_cast<int>(sizeof(kSubsHpAmmo)/sizeof(kSubsHpAmmo[0])), /*preview*/ 2 },
    { "MATCH STATUS HUD",  "Metroid.UI.SectionHudMatchStatusGrp",
      nullptr, 0, kSubsMatchStatus, static_cast<int>(sizeof(kSubsMatchStatus)/sizeof(kSubsMatchStatus[0])), /*preview*/ 3 },
    { "HUD RADAR",         "Metroid.UI.SectionHudRadar",
      _P(kSecRadar), nullptr, 0, /*preview*/ 4 },
};
static constexpr int kHudMainSectionCount = static_cast<int>(sizeof(kHudMainSections) / sizeof(kHudMainSections[0]));

static const char* kAnchorLabels[] = {
    "Top Left", "Top Center", "Top Right",
    "Middle Left", "Middle Center", "Middle Right",
    "Bottom Left", "Bottom Center", "Bottom Right",
};

static const char* kAlignLabels[] = { "Left", "Center", "Right" };

// Helper: make objectName from config key (dots → underscores)
static QString cfgKeyToObjName(const char* key)
{
    QString s = QString::fromUtf8(key);
    s.replace('.', '_');
    return s;
}

// ── Preview Widgets ─────────────────────────────────────────────────────────

class HudPreviewWidget : public QWidget
{
public:
    explicit HudPreviewWidget(EmuInstance* emu, QWidget* parent = nullptr)
        : QWidget(parent), m_emu(emu)
    {
        setFixedWidth(160);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }
    void refreshPreview() { update(); }
protected:
    EmuInstance* m_emu;
    Config::Table& cfg() { return m_emu->getLocalConfig(); }

    QColor readColor(const char* kR, const char* kG, const char* kB, float opacity = 1.0f) {
        auto& c = cfg();
        return QColor(c.GetInt(kR), c.GetInt(kG), c.GetInt(kB), qBound(0, static_cast<int>(opacity * 255), 255));
    }

    QFont hudFont() {
        return getMphHudFont(cfg().GetInt("Metroid.Visual.HudTextScale"));
    }

    void drawBackground(QPainter& p, const QRect& r) {
        p.fillRect(r, QColor(20, 20, 30));
        p.setPen(QColor(60, 60, 80));
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }
};

class CrosshairPreviewWidget : public HudPreviewWidget
{
public:
    using HudPreviewWidget::HudPreviewWidget;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect();
        drawBackground(p, r);

        auto& c = cfg();
        QColor col = readColor("Metroid.Visual.CrosshairColorR", "Metroid.Visual.CrosshairColorG", "Metroid.Visual.CrosshairColorB");

        int cx = r.center().x();
        int cy = r.center().y();
        float scale = 2.0f;

        auto drawArms = [&](const char* showKey, const char* opacityKey, const char* lxKey, const char* lyKey,
                            const char* thickKey, const char* ofsKey) {
            if (!c.GetBool(showKey)) return;
            float opacity = c.GetDouble(opacityKey);
            int lx = c.GetInt(lxKey);
            int ly = c.GetInt(lyKey);
            int thick = c.GetInt(thickKey);
            int ofs = c.GetInt(ofsKey);
            QColor ac = col;
            ac.setAlphaF(opacity);
            int t = qMax(1, static_cast<int>(thick * scale));
            int ht = t / 2;
            bool tStyle = c.GetBool("Metroid.Visual.CrosshairTStyle");
            p.fillRect(QRect(cx + static_cast<int>(ofs * scale), cy - ht, static_cast<int>(lx * scale), t), ac);
            p.fillRect(QRect(cx - static_cast<int>(ofs * scale) - static_cast<int>(lx * scale), cy - ht, static_cast<int>(lx * scale), t), ac);
            p.fillRect(QRect(cx - ht, cy + static_cast<int>(ofs * scale), t, static_cast<int>(ly * scale)), ac);
            if (!tStyle)
                p.fillRect(QRect(cx - ht, cy - static_cast<int>(ofs * scale) - static_cast<int>(ly * scale), t, static_cast<int>(ly * scale)), ac);
        };

        drawArms("Metroid.Visual.CrosshairInnerShow", "Metroid.Visual.CrosshairInnerOpacity",
                 "Metroid.Visual.CrosshairInnerLengthX", "Metroid.Visual.CrosshairInnerLengthY",
                 "Metroid.Visual.CrosshairInnerThickness", "Metroid.Visual.CrosshairInnerOffset");
        drawArms("Metroid.Visual.CrosshairOuterShow", "Metroid.Visual.CrosshairOuterOpacity",
                 "Metroid.Visual.CrosshairOuterLengthX", "Metroid.Visual.CrosshairOuterLengthY",
                 "Metroid.Visual.CrosshairOuterThickness", "Metroid.Visual.CrosshairOuterOffset");

        if (c.GetBool("Metroid.Visual.CrosshairCenterDot")) {
            float dotOp = c.GetDouble("Metroid.Visual.CrosshairDotOpacity");
            int dotThick = c.GetInt("Metroid.Visual.CrosshairDotThickness");
            QColor dc = col; dc.setAlphaF(dotOp);
            int ds = qMax(1, static_cast<int>(dotThick * scale));
            p.fillRect(QRect(cx - ds/2, cy - ds/2, ds, ds), dc);
        }
    }
};

class HpAmmoPreviewWidget : public HudPreviewWidget
{
public:
    using HudPreviewWidget::HudPreviewWidget;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect();
        drawBackground(p, r);

        auto& c = cfg();
        p.setFont(hudFont());

        {
            float op = c.GetDouble("Metroid.Visual.HudHpOpacity");
            QColor col = readColor("Metroid.Visual.HudHpTextColorR", "Metroid.Visual.HudHpTextColorG", "Metroid.Visual.HudHpTextColorB", op);
            p.setPen(col);
            QString prefix = QString::fromStdString(c.GetString("Metroid.Visual.HudHpPrefix"));
            p.drawText(8, r.height() - 24, prefix + "100");
        }

        if (c.GetBool("Metroid.Visual.HudHpGauge")) {
            float op = c.GetDouble("Metroid.Visual.HudHpGaugeOpacity");
            QColor gc = readColor("Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB", op);
            int len = c.GetInt("Metroid.Visual.HudHpGaugeLength");
            int w = c.GetInt("Metroid.Visual.HudHpGaugeWidth");
            int orient = c.GetInt("Metroid.Visual.HudHpGaugeOrientation");
            int drawLen = qMin(len, r.width() - 16);
            if (orient == 0)
                p.fillRect(QRect(8, r.height() - 12, drawLen, qMax(w, 2)), gc);
            else
                p.fillRect(QRect(8, r.height() - 12 - qMin(len, r.height()-30), qMax(w, 2), qMin(len, r.height()-30)), gc);
        }

        {
            float op = c.GetDouble("Metroid.Visual.HudWeaponOpacity");
            QColor col = readColor("Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB", op);
            p.setPen(col);
            QString prefix = QString::fromStdString(c.GetString("Metroid.Visual.HudAmmoPrefix"));
            p.drawText(r.width() - 80, r.height() - 24, prefix + "50");
        }

        if (c.GetBool("Metroid.Visual.HudAmmoGauge")) {
            float op = c.GetDouble("Metroid.Visual.HudAmmoGaugeOpacity");
            QColor gc = readColor("Metroid.Visual.HudAmmoGaugeColorR", "Metroid.Visual.HudAmmoGaugeColorG", "Metroid.Visual.HudAmmoGaugeColorB", op);
            int len = c.GetInt("Metroid.Visual.HudAmmoGaugeLength");
            int w = c.GetInt("Metroid.Visual.HudAmmoGaugeWidth");
            int orient = c.GetInt("Metroid.Visual.HudAmmoGaugeOrientation");
            int drawLen = qMin(len, r.width() - 16);
            if (orient == 0)
                p.fillRect(QRect(r.width() - 8 - drawLen, r.height() - 12, drawLen, qMax(w, 2)), gc);
            else
                p.fillRect(QRect(r.width() - 8 - qMax(w, 2), r.height() - 12 - qMin(len, r.height()-30), qMax(w, 2), qMin(len, r.height()-30)), gc);
        }
    }
};

class MatchStatusPreviewWidget : public HudPreviewWidget
{
public:
    using HudPreviewWidget::HudPreviewWidget;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect();
        drawBackground(p, r);

        auto& c = cfg();
        p.setFont(hudFont());

        if (c.GetBool("Metroid.Visual.HudMatchStatusShow")) {
            float op = c.GetDouble("Metroid.Visual.HudMatchStatusOpacity");
            QColor col = readColor("Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB", op);
            p.setPen(col);
            p.drawText(8, 16, "3 / 7");
        }

        if (c.GetBool("Metroid.Visual.HudRankShow")) {
            float op = c.GetDouble("Metroid.Visual.HudRankOpacity");
            QColor col = readColor("Metroid.Visual.HudRankColorR", "Metroid.Visual.HudRankColorG", "Metroid.Visual.HudRankColorB", op);
            p.setPen(col);
            QString prefix = QString::fromStdString(c.GetString("Metroid.Visual.HudRankPrefix"));
            QString suffix = QString::fromStdString(c.GetString("Metroid.Visual.HudRankSuffix"));
            p.drawText(8, 32, prefix + "1st" + suffix);
        }

        if (c.GetBool("Metroid.Visual.HudTimeLeftShow")) {
            float op = c.GetDouble("Metroid.Visual.HudTimeLeftOpacity");
            QColor col = readColor("Metroid.Visual.HudTimeLeftColorR", "Metroid.Visual.HudTimeLeftColorG", "Metroid.Visual.HudTimeLeftColorB", op);
            p.setPen(col);
            p.drawText(8, 48, "4:32");
        }

        if (c.GetBool("Metroid.Visual.HudBombLeftShow") && c.GetBool("Metroid.Visual.HudBombLeftTextShow")) {
            float op = c.GetDouble("Metroid.Visual.HudBombLeftOpacity");
            QColor col = readColor("Metroid.Visual.HudBombLeftColorR", "Metroid.Visual.HudBombLeftColorG", "Metroid.Visual.HudBombLeftColorB", op);
            p.setPen(col);
            QString prefix = QString::fromStdString(c.GetString("Metroid.Visual.HudBombLeftPrefix"));
            p.drawText(r.width() - 60, r.height() - 16, prefix + "3");
        }
    }
};

class RadarPreviewWidget : public HudPreviewWidget
{
public:
    using HudPreviewWidget::HudPreviewWidget;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect();
        drawBackground(p, r);

        auto& c = cfg();
        if (!c.GetBool("Metroid.Visual.BtmOverlayEnable")) return;

        float op = c.GetDouble("Metroid.Visual.BtmOverlayOpacity");
        int sz = c.GetInt("Metroid.Visual.BtmOverlayDstSize");
        int drawSz = qMin(sz, qMin(r.width(), r.height()) - 16);

        int cx = r.center().x();
        int cy = r.center().y();

        QColor fill(40, 80, 40, static_cast<int>(op * 180));
        QColor border(80, 160, 80, static_cast<int>(op * 255));

        p.setPen(QPen(border, 2));
        p.setBrush(fill);
        p.drawEllipse(QPoint(cx, cy), drawSz / 2, drawSz / 2);

        p.setPen(QPen(border, 1));
        p.drawLine(cx - 4, cy, cx + 4, cy);
        p.drawLine(cx, cy - 4, cx, cy + 4);
    }
};

} // anonymous namespace


void MelonPrimeInputConfig::invalidateHudAndRefreshPreviews()
{
#ifdef MELONPRIME_CUSTOM_HUD
    MelonPrime::CustomHud_InvalidateConfigCache();
#endif
    for (auto* pw : m_hudPreviews) pw->update();
}

void MelonPrimeInputConfig::setupCustomHudWidgets(Config::Table& instcfg)
{
    QVBoxLayout* vlay = ui->crosshairVLayout;
    const int spacerIdx = vlay->count() - 1;
    int insertPos = spacerIdx;

    // ── Widget factory: creates one widget for a HudWidgetProp and adds it to a QFormLayout ──
    auto createPropWidget = [&](QWidget* parent, QFormLayout* form, const HudWidgetProp& p) {
        QString objName = cfgKeyToObjName(p.cfgKey);

        switch (p.type) {
        case HWType::Bool: {
            auto* cb = new QCheckBox(parent);
            cb->setObjectName(objName);
            cb->setChecked(instcfg.GetBool(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), cb);
            m_hudWidgets[p.cfgKey] = cb;
            connect(cb, &QCheckBox::toggled, this, [this, key = std::string(p.cfgKey)](bool val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetBool(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::Int: {
            auto* rowW = new QWidget(parent);
            auto* hlay = new QHBoxLayout(rowW);
            hlay->setContentsMargins(0, 0, 0, 0);
            hlay->setSpacing(4);

            auto* slider = new QSlider(Qt::Horizontal, rowW);
            slider->setRange(p.min, p.max);
            slider->setSingleStep(p.step);
            slider->setPageStep(p.step * 5);
            slider->setValue(instcfg.GetInt(p.cfgKey));

            auto* sb = new QSpinBox(rowW);
            sb->setObjectName(objName);
            sb->setRange(p.min, p.max);
            sb->setSingleStep(p.step);
            sb->setValue(instcfg.GetInt(p.cfgKey));
            sb->setFixedWidth(58);

            hlay->addWidget(slider, 1);
            hlay->addWidget(sb);

            // keep slider and spinbox in sync
            connect(slider, &QSlider::valueChanged, sb, &QSpinBox::setValue);
            connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);

            form->addRow(QString::fromUtf8(p.label), rowW);
            m_hudWidgets[p.cfgKey] = sb;
            connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::Float: {
            auto* dsb = new QDoubleSpinBox(parent);
            dsb->setObjectName(objName);
            dsb->setRange(0.0, 1.0);
            dsb->setSingleStep(0.05);
            dsb->setDecimals(2);
            dsb->setValue(instcfg.GetDouble(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), dsb);
            m_hudWidgets[p.cfgKey] = dsb;
            connect(dsb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, key = std::string(p.cfgKey)](double val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetDouble(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::String: {
            auto* le = new QLineEdit(parent);
            le->setObjectName(objName);
            le->setText(QString::fromStdString(instcfg.GetString(p.cfgKey)));
            form->addRow(QString::fromUtf8(p.label), le);
            m_hudWidgets[p.cfgKey] = le;
            connect(le, &QLineEdit::textChanged, this, [this, key = std::string(p.cfgKey)](const QString& val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetString(key, val.toStdString());
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::Anchor9: {
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            for (int a = 0; a < 9; ++a)
                combo->addItem(QString::fromUtf8(kAnchorLabels[a]));
            combo->setCurrentIndex(instcfg.GetInt(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::Align3: {
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            for (int a = 0; a < 3; ++a)
                combo->addItem(QString::fromUtf8(kAlignLabels[a]));
            combo->setCurrentIndex(instcfg.GetInt(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::Color3: {
            // Outer container (vertical: preset combo on top, RGB spinboxes below)
            auto* colWidget = new QWidget(parent);
            auto* vlay = new QVBoxLayout(colWidget);
            vlay->setContentsMargins(0, 0, 0, 0);
            vlay->setSpacing(2);

            // ── Preset combo ──
            auto* presetCombo = new QComboBox(colWidget);
            static const int kPaletteCount = ArrayCount(kHudColorPalette);
            // Build pixmap icon for each preset colour
            for (int pi = 0; pi < kPaletteCount; ++pi) {
                const PresetColor& pc = kHudColorPalette[pi];
                QPixmap pm(14, 14);
                pm.fill(QColor(pc.r, pc.g, pc.b));
                presetCombo->addItem(QIcon(pm), QString::fromUtf8(kHudColorPaletteNames[pi]));
            }
            presetCombo->addItem(QStringLiteral("Custom"));   // index == kPaletteCount

            // ── RGB spinboxes row ──
            auto* rgbRow = new QWidget(colWidget);
            auto* hlay = new QHBoxLayout(rgbRow);
            hlay->setContentsMargins(0, 0, 0, 0);
            hlay->setSpacing(4);

            auto* sbR = new QSpinBox(rgbRow); sbR->setRange(0, 255); sbR->setPrefix(QStringLiteral("R:")); sbR->setObjectName(cfgKeyToObjName(p.cfgKey));
            auto* sbG = new QSpinBox(rgbRow); sbG->setRange(0, 255); sbG->setPrefix(QStringLiteral("G:")); sbG->setObjectName(cfgKeyToObjName(p.cfgKeyG));
            auto* sbB = new QSpinBox(rgbRow); sbB->setRange(0, 255); sbB->setPrefix(QStringLiteral("B:")); sbB->setObjectName(cfgKeyToObjName(p.cfgKeyB));

            sbR->setValue(instcfg.GetInt(p.cfgKey));
            sbG->setValue(instcfg.GetInt(p.cfgKeyG));
            sbB->setValue(instcfg.GetInt(p.cfgKeyB));

            auto* swatch = new QPushButton(rgbRow);
            swatch->setFixedSize(24, 24);
            auto updateSwatch = [sbR, sbG, sbB, swatch, presetCombo]() {
                swatch->setStyleSheet(QString("background-color: rgb(%1,%2,%3); border: 1px solid #888;")
                    .arg(sbR->value()).arg(sbG->value()).arg(sbB->value()));
                // Sync preset combo
                int match = kPaletteCount; // default: Custom
                for (int pi = 0; pi < kPaletteCount; ++pi) {
                    const PresetColor& pc = kHudColorPalette[pi];
                    if (sbR->value() == pc.r && sbG->value() == pc.g && sbB->value() == pc.b) {
                        match = pi; break;
                    }
                }
                const bool old = presetCombo->blockSignals(true);
                presetCombo->setCurrentIndex(match);
                presetCombo->blockSignals(old);
            };
            updateSwatch();

            hlay->addWidget(sbR); hlay->addWidget(sbG); hlay->addWidget(sbB); hlay->addWidget(swatch);

            vlay->addWidget(presetCombo);
            vlay->addWidget(rgbRow);
            form->addRow(QString::fromUtf8(p.label), colWidget);

            m_hudWidgets[p.cfgKey]  = sbR;
            m_hudWidgets[p.cfgKeyG] = sbG;
            m_hudWidgets[p.cfgKeyB] = sbB;

            // Preset combo → update spinboxes
            connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [sbR, sbG, sbB, updateSwatch](int idx) {
                    if (idx < 0 || idx >= kPaletteCount) return;
                    const PresetColor& pc = kHudColorPalette[idx];
                    const bool oldR = sbR->blockSignals(true);
                    const bool oldG = sbG->blockSignals(true);
                    const bool oldB = sbB->blockSignals(true);
                    sbR->setValue(pc.r); sbG->setValue(pc.g); sbB->setValue(pc.b);
                    sbR->blockSignals(oldR); sbG->blockSignals(oldG); sbB->blockSignals(oldB);
                    updateSwatch();
                });
            // Spinbox → after preset combo sets spinboxes, fire config update via spinbox signal below

            auto connectColorSpin = [this, updateSwatch](QSpinBox* sb, const std::string& key) {
                connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, key, updateSwatch](int val) {
                    updateSwatch();
                    if (!m_applyPreviewEnabled) return;
                    emuInstance->getLocalConfig().SetInt(key, val);
                    invalidateHudAndRefreshPreviews();
                });
            };
            connectColorSpin(sbR, std::string(p.cfgKey));
            connectColorSpin(sbG, std::string(p.cfgKeyG));
            connectColorSpin(sbB, std::string(p.cfgKeyB));

            connect(swatch, &QPushButton::clicked, this, [this, sbR, sbG, sbB, updateSwatch]() {
                QColor init(sbR->value(), sbG->value(), sbB->value());
                QColor c = QColorDialog::getColor(init, this, QStringLiteral("Pick Color"));
                if (c.isValid()) {
                    sbR->setValue(c.red());
                    sbG->setValue(c.green());
                    sbB->setValue(c.blue());
                    updateSwatch();
                }
            });
            break;
        }
        } // switch
    };

    // ── Helper: populate a QFormLayout from a props array ──
    auto populateProps = [&](QWidget* parent, QFormLayout* form, const HudWidgetProp* props, int count) {
        for (int i = 0; i < count; ++i)
            createPropWidget(parent, form, props[i]);
    };

    // ── Helper: create a collapsible sub-section toggle + body ──
    // Returns the section body widget. Adds toggle+body to destLayout.
    auto makeSubToggle = [&](QVBoxLayout* destLayout, const char* title, const char* cfgKey, int indent) -> QWidget*
    {
        auto* btn = new QPushButton(this);
        btn->setCheckable(true);
        btn->setFlat(true);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { text-align: left; font-weight: bold; padding: 3px; padding-left: %1px; } "
            "QPushButton::checked { font-weight: bold; }").arg(indent));

        bool expanded = instcfg.GetBool(cfgKey);
        btn->setChecked(expanded);
        QString label = QString::fromUtf8(title);
        btn->setText((expanded ? QString::fromUtf8("\u25BC ") : QString::fromUtf8("\u25B6 ")) + label);

        connect(btn, &QPushButton::toggled, [btn, label](bool checked) {
            btn->setText((checked ? QString::fromUtf8("\u25BC ") : QString::fromUtf8("\u25B6 ")) + label);
        });

        m_hudToggles.push_back({btn, std::string(cfgKey)});
        destLayout->addWidget(btn);

        auto* body = new QWidget(this);
        body->setVisible(expanded);
        connect(btn, &QPushButton::toggled, body, &QWidget::setVisible);
        destLayout->addWidget(body);

        return body;
    };

    // ── Build sub-section recursively (handles HudSubSec with optional children) ──
    std::function<void(QVBoxLayout*, const HudSubSec&, int)> buildSubSection;
    buildSubSection = [&](QVBoxLayout* destLayout, const HudSubSec& sub, int indent)
    {
        QWidget* body = makeSubToggle(destLayout, sub.title, sub.cfgToggleKey, indent);
        auto* bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(indent + 8, 2, 2, 2);
        bodyLayout->setSpacing(2);

        // Direct props
        if (sub.props && sub.propCount > 0) {
            auto* form = new QFormLayout();
            form->setContentsMargins(0, 0, 0, 0);
            form->setVerticalSpacing(4);
            populateProps(body, form, sub.props, sub.propCount);
            bodyLayout->addLayout(form);
        }

        // Nested children (e.g., Rank/Time Left/Time Limit inside "Rank / Time")
        for (int ci = 0; ci < sub.childCount; ++ci)
            buildSubSection(bodyLayout, sub.children[ci], indent + 8);
    };

    // ── Build main sections ──
    for (int si = 0; si < kHudMainSectionCount; ++si) {
        const HudMainSec& sec = kHudMainSections[si];

        // --- Main toggle button (top-level, bold + large) ---
        auto* mainBtn = new QPushButton(this);
        mainBtn->setCheckable(true);
        mainBtn->setFlat(true);
        mainBtn->setStyleSheet(QStringLiteral(
            "QPushButton { text-align: left; font-weight: bold; font-size: 11pt; padding: 6px 4px; } "
            "QPushButton::checked { font-weight: bold; }"));

        bool expanded = instcfg.GetBool(sec.cfgToggleKey);
        mainBtn->setChecked(expanded);
        QString label = QString::fromUtf8(sec.title);
        mainBtn->setText((expanded ? QString::fromUtf8("\u25BC ") : QString::fromUtf8("\u25B6 ")) + label);

        connect(mainBtn, &QPushButton::toggled, [mainBtn, label](bool checked) {
            mainBtn->setText((checked ? QString::fromUtf8("\u25BC ") : QString::fromUtf8("\u25B6 ")) + label);
        });

        m_hudToggles.push_back({mainBtn, std::string(sec.cfgToggleKey)});
        vlay->insertWidget(insertPos++, mainBtn);

        // --- Main section body with HBoxLayout: left = sub-sections, right = preview ---
        auto* mainBody = new QWidget(this);
        mainBody->setVisible(expanded);
        connect(mainBtn, &QPushButton::toggled, mainBody, &QWidget::setVisible);

        auto* hbox = new QHBoxLayout(mainBody);
        hbox->setContentsMargins(0, 0, 0, 0);
        hbox->setSpacing(4);

        // Left column: direct properties + sub-sections
        auto* leftCol = new QWidget(mainBody);
        auto* bodyLayout = new QVBoxLayout(leftCol);
        bodyLayout->setContentsMargins(8, 4, 4, 4);
        bodyLayout->setSpacing(2);

        // Direct properties (e.g., crosshair color/outline at group level)
        if (sec.directProps && sec.directPropCount > 0) {
            auto* form = new QFormLayout();
            form->setContentsMargins(0, 0, 0, 0);
            form->setVerticalSpacing(4);
            populateProps(leftCol, form, sec.directProps, sec.directPropCount);
            bodyLayout->addLayout(form);
        }

        // Sub-sections
        for (int si2 = 0; si2 < sec.subCount; ++si2)
            buildSubSection(bodyLayout, sec.subs[si2], 8);

        bodyLayout->addStretch();
        hbox->addWidget(leftCol, 1);

        // Right column: preview widget
        HudPreviewWidget* preview = nullptr;
        switch (sec.previewKind) {
        case 1: preview = new CrosshairPreviewWidget(emuInstance, mainBody); break;
        case 2: preview = new HpAmmoPreviewWidget(emuInstance, mainBody); break;
        case 3: preview = new MatchStatusPreviewWidget(emuInstance, mainBody); break;
        case 4: preview = new RadarPreviewWidget(emuInstance, mainBody); break;
        }
        if (preview) {
            hbox->addWidget(preview, 0);
            m_hudPreviews.push_back(preview);
        }

        vlay->insertWidget(insertPos++, mainBody);
    }
}


#include "MelonPrimeInputConfigCustomHudCode.inc"

void MelonPrimeInputConfig::refreshAfterHudEditSave()
{
    snapshotVisualConfig();
}

