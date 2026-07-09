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
#include <QFontComboBox>
#include <QFontDialog>
#include <QFileDialog>
#include <QPushButton>
#include <QTimer>
#include <QGuiApplication>
#include <QClipboard>
#include <QFormLayout>
#include <QCheckBox>
#include "../MelonPrimeColorDialogPrefs.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSlider>
#include <QPainter>
#include <QPainterPath>
#include <QImageReader>
#include <QVariant>
#include <QApplication>
#include <QEvent>
#include <QWheelEvent>
#include <QAbstractSpinBox>
#include <QAbstractScrollArea>
#include <algorithm>
#include <sstream>

#include "MelonPrimeInputConfig.h"
#include "MelonPrimeInputConfigInternal.h"
#include "ui_MelonPrimeInputConfig.h"
#include "Config.h"
#include "MelonPrimeDef.h"
#include "../MelonPrimeHudGeometry.h"
#include "../MelonPrimeHudPropSchema.inc"
#include "MelonPrimeLocalization.h"
#include "toml/toml.hpp"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudRender.h"
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#define MELONPRIME_CHECKBOX_STATE_CHANGED_SIGNAL &QCheckBox::checkStateChanged
#else
#define MELONPRIME_CHECKBOX_STATE_CHANGED_SIGNAL QOverload<int>::of(&QCheckBox::stateChanged)
#endif

// InputConfigDialog must be fully defined before including MapButton.h.
// MapButton accesses parentDialog directly, so a forward declaration is not enough.
#include "InputConfigDialog.h" 

#include "MapButton.h"
#include "../Window.h"
#include "Platform.h"
#include "VideoSettingsDialog.h"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudRender.h"
#endif

using namespace melonDS;

namespace {
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    constexpr bool kDeveloperOnlyFeaturesEnabled = true;
#else
    constexpr bool kDeveloperOnlyFeaturesEnabled = false;
#endif

    [[nodiscard]] int ClampLowLatencyAimMode(int mode) noexcept
    {
        const int clamped = std::clamp(
            mode,
            MelonPrime::LowLatencyAimMode::Off,
            MelonPrime::LowLatencyAimMode::InstantAimFollow);
        if (!kDeveloperOnlyFeaturesEnabled
            && clamped == MelonPrime::LowLatencyAimMode::InstantAimFollow)
            return MelonPrime::LowLatencyAimMode::ImmediateSync;
        return clamped;
    }

    void SetComboCurrentData(QComboBox* combo, int value)
    {
        if (!combo)
            return;
        const int index = combo->findData(value);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    }

    void ConfigureScreenSyncControlsForPlatform(
        QComboBox* combo,
        QLabel* description)
    {
#ifndef _WIN32
        if (combo) {
            if (combo->count() > 2)
                combo->removeItem(2);
            combo->setToolTip(MelonPrime::UiText::Tr(
                "Screen Sync Mode: Off = no sync call, glFinish = wait for GL commands to complete"));
        }
        if (description) {
            description->setText(MelonPrime::UiText::Tr(
                "Off: No sync (lowest latency, but the display may look choppy). glFinish: Smoother display by waiting for rendering to fully complete each frame. Automatically disabled during FastForward/SlowMo."));
        }
#else
        (void)combo;
        (void)description;
#endif
    }

#ifdef __APPLE__ // scatter-budget-exempt: video-quality preset UI gate, not input dispatch
    // macOS OpenGL does not support the compute renderer path used by the
    // High2 preset (Screen.cpp / GPU3D_Compute), so the preset button is
    // disabled to avoid selecting a renderer that can crash on this platform.
    // See melonprime_macos_compute_renderer_restriction.md for background.
    void DisableMacComputeVideoQualityButton(Ui::MelonPrimeInputConfig* ui)
    {
        if (!ui || !ui->metroidSetVideoQualityToHigh2)
            return;

        ui->metroidSetVideoQualityToHigh2->setEnabled(false);
        ui->metroidSetVideoQualityToHigh2->setToolTip(
            MelonPrime::UiText::Tr(
                "High2 / Compute render is unavailable on macOS because macOS OpenGL does not support the required compute renderer path."));
    }
#endif

}

MelonPrimeInputConfig::MelonPrimeInputConfig(EmuInstance* emu, QWidget* parent) :
    QWidget(parent),
    ui(new Ui::MelonPrimeInputConfig),
    emuInstance(emu)
{
    ui->setupUi(this);

#ifdef __APPLE__ // scatter-budget-exempt: video-quality preset UI gate, not input dispatch
    DisableMacComputeVideoQualityButton(ui);
#endif

    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    MelonPrime::UiText::SetMenuLanguageSelection(
        MelonPrime::UiText::NormalizeMenuLanguageConfig(
            instcfg.GetInt(MelonPrime::CfgKey::MenuLanguage)));
    setupMenuLanguageControl(instcfg);
    setupKeyBindings(instcfg, keycfg, joycfg);
    setupSensitivityAndToggles(instcfg);
    setupInputMethodSection(instcfg);
    setupCollapsibleSections(instcfg);
    setupCustomHudWidgets(instcfg);
    setupPreviewConnections();
    setupCustomHudCode();
    MelonPrime::UiText::LocalizeWidgetTree(this);
#ifdef __APPLE__ // scatter-budget-exempt: video-quality preset UI gate, not input dispatch
    DisableMacComputeVideoQualityButton(ui);
#endif
    ConfigureScreenSyncControlsForPlatform(
        ui->comboMetroidScreenSyncMode,
        ui->labelMetroidScreenSyncDesc);

    snapshotVisualConfig();

    // Must run after every widget (UI-defined and programmatic) exists so the
    // guard reaches all of them.
    installWheelScrollGuards();

    m_applyPreviewEnabled = true;
}

void MelonPrimeInputConfig::installWheelScrollGuards()
{
    // Combo boxes, spin boxes and sliders default to Qt::WheelFocus, which both
    // steals focus on hover and changes their value when the wheel is turned.
    // Demote them to Qt::StrongFocus (click/tab only) and install this object as
    // an event filter so wheel scrolling over an unfocused control is forwarded
    // to the surrounding scroll area instead. QFontComboBox derives from
    // QComboBox, and QSpinBox/QDoubleSpinBox from QAbstractSpinBox, so these
    // three queries cover every wheel-sensitive settings control.
    const auto guard = [this](QWidget* w)
    {
        if (w->focusPolicy() == Qt::WheelFocus)
            w->setFocusPolicy(Qt::StrongFocus);
        w->installEventFilter(this);
    };

    for (QComboBox* w : findChildren<QComboBox*>())
        guard(w);
    for (QAbstractSpinBox* w : findChildren<QAbstractSpinBox*>())
        guard(w);
    for (QSlider* w : findChildren<QSlider*>())
        guard(w);
}

bool MelonPrimeInputConfig::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::Wheel)
    {
        QWidget* w = qobject_cast<QWidget*>(obj);
        if (w && !w->hasFocus())
        {
            // The user is scrolling the page, not adjusting this control.
            // Forward the wheel to the enclosing scroll area's viewport so the
            // page scrolls, and swallow it here so the value never changes.
            for (QWidget* p = w->parentWidget(); p; p = p->parentWidget())
            {
                if (auto* area = qobject_cast<QAbstractScrollArea*>(p))
                {
                    QApplication::sendEvent(area->viewport(), event);
                    break;
                }
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
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

void MelonPrimeInputConfig::setupMenuLanguageControl(Config::Table& instcfg)
{
    if (m_menuLanguageGroup)
        return;

    m_menuLanguageGroup = new QGroupBox(QStringLiteral("Language"), ui->scrollSettingsContents);
    m_menuLanguageGroup->setObjectName(QStringLiteral("groupMetroidMenuLanguage"));
    auto* layout = new QVBoxLayout(m_menuLanguageGroup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_lblMenuLanguage = new QLabel(QStringLiteral("Menu Language"), m_menuLanguageGroup);
    m_comboMenuLanguage = new QComboBox(m_menuLanguageGroup);
    m_comboMenuLanguage->setObjectName(QStringLiteral("comboMetroidMenuLanguage"));

    const MelonPrime::UiText::MenuLangId systemLang = MelonPrime::UiText::DetectSystemMenuLanguage();
    const QString systemLabel = MelonPrime::UiText::MenuLanguageDisplayName(systemLang);
    m_comboMenuLanguage->addItem(
        QStringLiteral("System default (%1)").arg(systemLabel),
        MelonPrime::UiText::kMenuLanguageSystemDefault);
    m_comboMenuLanguage->insertSeparator(1);

    for (MelonPrime::UiText::MenuLangId lang : MelonPrime::UiText::AllSelectableMenuLanguages())
    {
        m_comboMenuLanguage->addItem(
            MelonPrime::UiText::MenuLanguageDisplayName(lang),
            static_cast<int>(lang));
    }

    SetComboCurrentData(
        m_comboMenuLanguage,
        MelonPrime::UiText::NormalizeMenuLanguageConfig(
            instcfg.GetInt(MelonPrime::CfgKey::MenuLanguage)));

    layout->addWidget(m_lblMenuLanguage);
    layout->addWidget(m_comboMenuLanguage);

    ui->metroidVLayout->insertWidget(0, m_menuLanguageGroup);

    connect(
        m_comboMenuLanguage,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this](int) {
            MelonPrime::UiText::SetMenuLanguageSelection(
                m_comboMenuLanguage->currentData().toInt());
            MelonPrime::UiText::LocalizeWidgetTree(this);
            if (InputConfigDialog::currentDlg)
                MelonPrime::UiText::LocalizeMelonDsDialog(InputConfigDialog::currentDlg);
            if (auto* mw = qobject_cast<MainWindow*>(window()); mw && mw->panel)
                mw->panel->reloadNoRomSplashLocalization();
        });
}

// Non-HUD settings binding table (Phase 5b)
// Single source of truth for symmetric simple settings: load and save both
// iterate the same rows so the two sides can never drift. Only plain mirrors
// live here (setChecked<->SetBool(checkState==Checked), setCurrentIndex<->
// SetInt(currentIndex), setValue<->SetInt/SetDouble(value)). Anything with a
// transform / dev-only gate / migration / invalidate-couple / non-mirror save
// stays out of the table and keeps its original code.
//
// Order is IDENTICAL to the original setupSensitivityAndToggles load order so
// the segmented load below reproduces the exact sequence of widget loads
// (slot side-effects and parent/child enable wiring are observable). The named
// segment-boundary constants index this table.
void MelonPrimeInputConfig::buildSettingBindings()
{
    using K = SettingKind;
    namespace C = MelonPrime::CfgKey;
    m_settingBindings = {
        // Segment 1: sensitivities + toggles, up to (not incl.) the dynamic
        // Low-Latency Aim Mode combo block.
        { C::MphSens,        K::DoubleSpinDouble, ui->metroidMphSensitvitySpinBox },     // 0
        { C::AimSens,        K::SpinInt,          ui->metroidAimSensitvitySpinBox },     // 1
        { C::AimYScale,      K::DoubleSpinDouble, ui->metroidAimYAxisScaleSpinBox },     // 2
        { C::AimAdjust,      K::DoubleSpinDouble, ui->metroidAimAdjustSpinBox },         // 3
        { C::SnapTap,        K::CheckBool,        ui->cbMetroidEnableSnapTap },          // 4
        { C::DataUnlock,     K::CheckBool,        ui->cbMetroidUnlockAll },              // 5
        { C::Headphone,      K::CheckBool,        ui->cbMetroidApplyHeadphone },         // 6
        { C::UseFwName,      K::CheckBool,        ui->cbMetroidUseFirmwareName },        // 7
        { C::HunterApply,    K::CheckBool,        ui->cbMetroidApplyHunter },            // 8
        { C::HunterSel,      K::ComboIndexInt,    ui->comboMetroidSelectedHunter },      // 9
        { C::LicColorApply,  K::CheckBool,        ui->cbMetroidApplyColor },             // 10
        { C::LicColorSel,    K::ComboIndexInt,    ui->comboMetroidSelectedColor },       // 11
        { C::SfxVolApply,    K::CheckBool,        ui->cbMetroidApplySfxVolume },         // 12
        { C::SfxVol,         K::SpinInt,          ui->spinMetroidVolumeSFX },            // 13
        { C::MusicVolApply,  K::CheckBool,        ui->cbMetroidApplyMusicVolume },       // 14
        { C::MusicVol,       K::SpinInt,          ui->spinMetroidVolumeMusic },          // 15
        { C::Joy2Key,        K::CheckBool,        ui->cbMetroidApplyJoy2KeySupport },    // 16
        { C::StylusMode,     K::CheckBool,        ui->cbMetroidEnableStylusMode },       // 17
        { C::DisableMphAimSmoothing, K::CheckBool, ui->cbMetroidDisableMphAimSmoothing }, // 18
        { C::AimAccumulator, K::CheckBool,        ui->cbMetroidEnableAimAccumulator },   // 19
        { C::ZoomAimScaleEnable, K::CheckBool,    m_cbMetroidZoomAimScaleEnable },       // 20
        { C::ZoomAimScalePct, K::SpinInt,         m_spinMetroidZoomAimScalePct },         // 21
        // Segment 2: Screen Sync Mode (after the native-hook block).
        { C::ScreenSyncMode, K::ComboIndexInt,    ui->comboMetroidScreenSyncMode },      // 22
        // Segment 3a: bug fixes (before the developer-only tooltip block).
        { C::WifiBitset,     K::CheckBool,        ui->cbMetroidFixWifiBitset },          // 23
        { C::FixShadowFreeze, K::CheckBool,       ui->cbMetroidFixShadowFreeze },        // 24
        { C::FixNoxusBladePersistence, K::CheckBool, ui->cbMetroidFixNoxusBladePersistence }, // 25
        // Segment 3b: more bug fixes + game features (before the
        // ExpandStageMatrixExtra enable line). ExpandStageMatrix MUST precede
        // ExpandStageMatrixExtra so its slot doesn't clobber the loaded Extra.
        { C::UseFirmwareLanguage, K::CheckBool,   ui->cbMetroidUseFirmwareLanguage },    // 26
        { C::ShowHeadshotOnline, K::CheckBool,    ui->cbMetroidShowHeadshotOnline },     // 27
        { C::ShowEnemyHpMeterOnline, K::CheckBool, ui->cbMetroidShowEnemyHpMeterOnline }, // 28
        { C::ExpandStageMatrix, K::CheckBool,     ui->cbMetroidExpandStageMatrix },      // 29
        { C::ExpandStageMatrixExtra, K::CheckBool, ui->cbMetroidExpandStageMatrixExtra }, // 30
        // Segment 3c: double-damage pair (before the parent/child sync wiring).
        { C::DisableDoubleDamageMultiplier, K::CheckBool, ui->cbMetroidDisableDoubleDamageMultiplier }, // 31
        { C::DamageNotifyPurple, K::CheckBool,    ui->cbMetroidDamageNotifyPurple },     // 32
        // Segment 3d: pickup toggles (before the pickup parent/child wiring).
        { C::PowerUpPickupNoEffectPowerUps, K::CheckBool, ui->cbMetroidDisablePickupPowerUps }, // 33
        { C::PowerUpPickupNoEffectDoubleDamage, K::CheckBool, ui->cbMetroidDisablePickupDoubleDamage }, // 34
        { C::PowerUpPickupNoEffectCloak, K::CheckBool, ui->cbMetroidDisablePickupCloak }, // 35
        { C::PowerUpPickupNoEffectDeathalt, K::CheckBool, ui->cbMetroidDisablePickupDeathalt }, // 36
        // Segment 3e: in-game scaling.
        { C::InGameAspectRatio, K::CheckBool,     ui->cbMetroidInGameAspectRatio },      // 37
        { C::InGameAspectRatioMode, K::ComboIndexInt, ui->comboMetroidInGameAspectRatioMode }, // 38
        // Segment 3f: Low HP warning (before updateLowHpWarningControls).
        { C::LowHpWarningMode,   K::ComboIndexInt, ui->comboMetroidLowHpWarningMode },   // 39
        { C::LowHpWarningFixed,  K::SpinInt,       ui->spinMetroidLowHpWarningFixed },   // 40
        { C::LowHpWarningLow,    K::SpinInt,       ui->spinMetroidLowHpWarningLow },     // 41
        { C::LowHpWarningMedium, K::SpinInt,       ui->spinMetroidLowHpWarningMedium },  // 42
        { C::LowHpWarningHigh,   K::SpinInt,       ui->spinMetroidLowHpWarningHigh },    // 43
        { C::LowHpWarningAutoBase, K::SpinInt,     ui->spinMetroidLowHpWarningAutoBase }, // 44
    };
}

void MelonPrimeInputConfig::loadBindingsRange(Config::Table& instcfg, int begin, int end)
{
    for (int i = begin; i < end; ++i) {
        const SettingBinding& b = m_settingBindings[i];
        switch (b.kind) {
        case SettingKind::CheckBool:
            static_cast<QCheckBox*>(b.widget)->setChecked(instcfg.GetBool(b.key));
            break;
        case SettingKind::ComboIndexInt:
        {
            auto* combo = static_cast<QComboBox*>(b.widget);
            int index = instcfg.GetInt(b.key);
#ifndef _WIN32
            if (combo == ui->comboMetroidScreenSyncMode && index == 2)
                index = 0;
#endif
            combo->setCurrentIndex((index >= 0 && index < combo->count()) ? index : 0);
            break;
        }
        case SettingKind::SpinInt:
            static_cast<QSpinBox*>(b.widget)->setValue(instcfg.GetInt(b.key));
            break;
        case SettingKind::DoubleSpinDouble:
            static_cast<QDoubleSpinBox*>(b.widget)->setValue(instcfg.GetDouble(b.key));
            break;
        }
    }
}

void MelonPrimeInputConfig::saveBindings(Config::Table& instcfg)
{
    for (const SettingBinding& b : m_settingBindings) {
        switch (b.kind) {
        case SettingKind::CheckBool:
            instcfg.SetBool(b.key,
                static_cast<QCheckBox*>(b.widget)->checkState() == Qt::Checked);
            break;
        case SettingKind::ComboIndexInt:
            instcfg.SetInt(b.key, static_cast<QComboBox*>(b.widget)->currentIndex());
            break;
        case SettingKind::SpinInt:
            instcfg.SetInt(b.key, static_cast<QSpinBox*>(b.widget)->value());
            break;
        case SettingKind::DoubleSpinDouble:
            instcfg.SetDouble(b.key, static_cast<QDoubleSpinBox*>(b.widget)->value());
            break;
        }
    }
}

void MelonPrimeInputConfig::setupSensitivityAndToggles(Config::Table& instcfg)
{
    if (!m_cbMetroidZoomAimScaleEnable) {
        m_cbMetroidZoomAimScaleEnable = new QCheckBox(
            QStringLiteral("Scale aim sensitivity while zoomed"),
            ui->sectionSensitivity);
        m_cbMetroidZoomAimScaleEnable->setObjectName(
            QStringLiteral("cbMetroidZoomAimScaleEnable"));

        m_spinMetroidZoomAimScalePct = new QSpinBox(ui->sectionSensitivity);
        m_spinMetroidZoomAimScalePct->setObjectName(
            QStringLiteral("spinMetroidZoomAimScalePct"));
        m_spinMetroidZoomAimScalePct->setRange(10, 300);
        m_spinMetroidZoomAimScalePct->setSingleStep(1);
        m_spinMetroidZoomAimScalePct->setSuffix(QStringLiteral("%"));

        m_lblMetroidZoomAimScalePct = new QLabel(
            QStringLiteral("Zoom Aim Scale %"),
            ui->sectionSensitivity);
        m_lblMetroidZoomAimScalePct->setObjectName(
            QStringLiteral("lblMetroidZoomAimScalePct"));
        m_lblMetroidZoomAimScalePct->setBuddy(m_spinMetroidZoomAimScalePct);

        m_lblMetroidZoomAimScaleDesc = new QLabel(
            QStringLiteral(
                "Applies only while the game's native zoom state is active. "
                "100% keeps normal mouse sensitivity; lower values slow down zoom aiming and higher values speed it up."),
            ui->sectionSensitivity);
        m_lblMetroidZoomAimScaleDesc->setObjectName(
            QStringLiteral("lblMetroidZoomAimScaleDesc"));
        m_lblMetroidZoomAimScaleDesc->setWordWrap(true);
        m_lblMetroidZoomAimScaleDesc->setStyleSheet(QStringLiteral("QLabel { margin-left: 20px; }"));

        if (auto* form = qobject_cast<QFormLayout*>(ui->sectionSensitivity->layout())) {
            form->insertRow(14, m_cbMetroidZoomAimScaleEnable);
            form->insertRow(15, m_lblMetroidZoomAimScalePct, m_spinMetroidZoomAimScalePct);
            form->insertRow(16, m_lblMetroidZoomAimScaleDesc);
        }

        connect(
            m_cbMetroidZoomAimScaleEnable,
            &QCheckBox::toggled,
            this,
            [this](bool) {
                updateAimControlsForStylusMode(ui->cbMetroidEnableStylusMode->isChecked());
            });
    }

    buildSettingBindings();

    // Segment 1 [0,22): sensitivities + toggles, up to the dynamic
    // Low-Latency Aim Mode combo block. (setChecked on stylus/smoothing fires
    // slots that cross-read each other; order is preserved by the table order.)
    loadBindingsRange(instcfg, 0, 22);
    if (!m_comboMetroidLowLatencyAimMode) {
        m_comboMetroidLowLatencyAimMode = new QComboBox(ui->sectionSensitivity);
        m_comboMetroidLowLatencyAimMode->addItem(
            QStringLiteral("Off"),
            MelonPrime::LowLatencyAimMode::Off);
        m_comboMetroidLowLatencyAimMode->addItem(
            QStringLiteral("Immediate Sync"),
            MelonPrime::LowLatencyAimMode::ImmediateSync);
        m_comboMetroidLowLatencyAimMode->addItem(
            QStringLiteral("MoonLike Aim"),
            MelonPrime::LowLatencyAimMode::MoonLikeAim);
        if constexpr (kDeveloperOnlyFeaturesEnabled) {
            m_comboMetroidLowLatencyAimMode->addItem(
                QStringLiteral("Instant Aim Follow (Developer Only)"),
                MelonPrime::LowLatencyAimMode::InstantAimFollow);
        }
        m_comboMetroidLowLatencyAimMode->setToolTip(
            QStringLiteral("Controls how the game's current aim direction follows the target aim direction."));
        int lowLatencyAimMode = ClampLowLatencyAimMode(
            instcfg.GetInt(MelonPrime::CfgKey::LowLatencyAimMode));
        // Legacy key migration. Keep until the first post-V3 release gives
        // old configs a save cycle; see the Phase 4 migration ledger.
        // Do not add new reads.
        // Old configs only had the InstantAimFollow bool; map it onto the new
        // public replacement when the new key is still at its Off default.
        if (lowLatencyAimMode == MelonPrime::LowLatencyAimMode::Off
            && instcfg.GetBool(MelonPrime::CfgKey::InstantAimFollow))
            lowLatencyAimMode = MelonPrime::LowLatencyAimMode::ImmediateSync;
        SetComboCurrentData(m_comboMetroidLowLatencyAimMode, lowLatencyAimMode);

        m_lblMetroidLowLatencyAimMode = new QLabel(QStringLiteral("Low-Latency Aim Mode"), ui->sectionSensitivity);
        m_lblMetroidLowLatencyAimMode->setToolTip(m_comboMetroidLowLatencyAimMode->toolTip());
        QString lowLatencyAimDesc = QStringLiteral(
            "Immediate Sync uses the low-latency ARM9 hook to sync currentAim to targetAim at the hook point and rebuild the aim basis. "
            "MoonLike Aim applies small aim movements immediately and limits only large aim jumps with a max-step chase. "
            "Requires Disable Aim Smoothing.");
        m_lblMetroidLowLatencyAimDesc = new QLabel(lowLatencyAimDesc, ui->sectionSensitivity);
        m_lblMetroidLowLatencyAimDesc->setObjectName(QStringLiteral("lblMetroidLowLatencyAimDesc"));
        m_lblMetroidLowLatencyAimDesc->setWordWrap(true);
        m_lblMetroidLowLatencyAimDesc->setStyleSheet(QStringLiteral("QLabel { margin-left: 20px; }"));

        if (auto* form = qobject_cast<QFormLayout*>(ui->sectionSensitivity->layout())) {
            form->insertRow(3, m_lblMetroidLowLatencyAimMode, m_comboMetroidLowLatencyAimMode);
            form->insertRow(4, m_lblMetroidLowLatencyAimDesc);
        }

        connect(
            m_comboMetroidLowLatencyAimMode,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                updateAimControlsForStylusMode(ui->cbMetroidEnableStylusMode->isChecked());
            });
    }
    const int nativeAimHookMode =
        kDeveloperOnlyFeaturesEnabled ? instcfg.GetInt(MelonPrime::CfgKey::NativeAimHookMode) : 0;
    ui->cbMetroidEnableNativeAimPostFoldWrite->setChecked(
        kDeveloperOnlyFeaturesEnabled && nativeAimHookMode == 2);
    ui->cbMetroidEnableNativeAimRegisterInjection->setChecked(
        kDeveloperOnlyFeaturesEnabled && nativeAimHookMode == 1);
    ui->cbMetroidEnableImmediateInputEdgeOverlay->setChecked(
        kDeveloperOnlyFeaturesEnabled && instcfg.GetBool(MelonPrime::CfgKey::ImmediateInputEdgeOverlay));
    ui->cbMetroidEnableDirectAltFormTransform->setChecked(instcfg.GetBool(MelonPrime::CfgKey::DirectAltFormTransform));
    connect(
        ui->cbMetroidEnableNativeAimRegisterInjection,
        MELONPRIME_CHECKBOX_STATE_CHANGED_SIGNAL,
        this,
        [this](auto state) {
            if (state == Qt::Checked && ui->cbMetroidEnableNativeAimPostFoldWrite->isChecked())
                ui->cbMetroidEnableNativeAimPostFoldWrite->setChecked(false);
            updateAimControlsForStylusMode(ui->cbMetroidEnableStylusMode->isChecked());
        });
    connect(
        ui->cbMetroidEnableNativeAimPostFoldWrite,
        MELONPRIME_CHECKBOX_STATE_CHANGED_SIGNAL,
        this,
        [this](auto state) {
            if (state == Qt::Checked && ui->cbMetroidEnableNativeAimRegisterInjection->isChecked())
                ui->cbMetroidEnableNativeAimRegisterInjection->setChecked(false);
            updateAimControlsForStylusMode(ui->cbMetroidEnableStylusMode->isChecked());
        });
    updateAimControlsForStylusMode(ui->cbMetroidEnableStylusMode->isChecked());

    // Segment 2 [22,23): Screen Sync Mode.
    loadBindingsRange(instcfg, 22, 23);
    // Clip/TopScreen stay outside the table: their save side is coupled to an
    // invalidate (old != new comparison), so they keep their original form.
    ui->cbMetroidClipCursorToBottomScreenWhenNotInGame->setChecked(instcfg.GetBool(MP_HUD_PROP_KEY_ClipCursorToBottomScreenWhenNotInGame));
    ui->cbMetroidInGameTopScreenOnly->setChecked(instcfg.GetBool(MP_HUD_PROP_KEY_InGameTopScreenOnly));

    // Segment 3a [23,26): bug fixes (before the developer-only tooltip block).
    loadBindingsRange(instcfg, 23, 26);
    if constexpr (kDeveloperOnlyFeaturesEnabled) {
        ui->cbMetroidEnableNativeAimPostFoldWrite->setToolTip("Developer-only option enabled in this build.");
        ui->cbMetroidEnableNativeAimRegisterInjection->setToolTip("Developer-only option enabled in this build.");
        ui->cbMetroidEnableImmediateInputEdgeOverlay->setToolTip("Developer-only option enabled in this build.");
    }
    else {
        ui->cbMetroidEnableNativeAimPostFoldWrite->setToolTip(
            "Developer-only option. Build with MELONPRIME_ENABLE_DEVELOPER_FEATURES to enable it.");
    }
    // Segment 3b [26,31): more bug fixes + game features. ExpandStageMatrix is
    // loaded before ExpandStageMatrixExtra (table order) so its slot doesn't
    // clobber the loaded Extra value; the Extra enable line follows after.
    loadBindingsRange(instcfg, 26, 31);
    ui->cbMetroidExpandStageMatrixExtra->setEnabled(ui->cbMetroidExpandStageMatrix->isChecked());
    // Segment 3c [31,33): double-damage pair (before the parent/child wiring).
    loadBindingsRange(instcfg, 31, 33);

    // Parent-child: Damage Notify Purple requires Disable Double Damage Multiplier
    // so the purple flash never becomes a real 2x boost.
    //   parent ON  -> child enabled, user picks freely
    //   parent OFF -> child disabled and forced OFF
    //   child  ON  -> parent auto-enabled (user wants them basically together)
    //   child  OFF -> parent untouched (user might want 1x without the flash)
    auto syncDamageNotifyPurpleEnableState = [this](bool ddMultiplierOff) {
        const bool parentOn = !ddMultiplierOff;
        if (!parentOn)
            ui->cbMetroidDamageNotifyPurple->setChecked(false);
        ui->cbMetroidDamageNotifyPurple->setEnabled(parentOn);
        ui->lblMetroidDamageNotifyPurpleDesc->setEnabled(parentOn);
    };
    connect(
        ui->cbMetroidDisableDoubleDamageMultiplier,
        &QCheckBox::toggled,
        this,
        [syncDamageNotifyPurpleEnableState](bool checked) {
            syncDamageNotifyPurpleEnableState(!checked);
        });
    connect(
        ui->cbMetroidDamageNotifyPurple,
        &QCheckBox::toggled,
        this,
        [this](bool checked) {
            if (checked && !ui->cbMetroidDisableDoubleDamageMultiplier->isChecked())
                ui->cbMetroidDisableDoubleDamageMultiplier->setChecked(true);
        });
    syncDamageNotifyPurpleEnableState(
        !ui->cbMetroidDisableDoubleDamageMultiplier->isChecked());

    // Segment 3d [33,37): pickup effect toggles (before the pickup parent/child
    // wiring). The parent (PowerUps) is loaded first, matching original order.
    loadBindingsRange(instcfg, 33, 37);
    auto updatePickupPowerUpChildren = [this](bool disableAllPowerUps, bool syncChildren) {
        if (syncChildren) {
            ui->cbMetroidDisablePickupDoubleDamage->setChecked(disableAllPowerUps);
            ui->cbMetroidDisablePickupCloak->setChecked(disableAllPowerUps);
            ui->cbMetroidDisablePickupDeathalt->setChecked(disableAllPowerUps);
        }
        ui->cbMetroidDisablePickupDoubleDamage->setEnabled(!disableAllPowerUps);
        ui->cbMetroidDisablePickupCloak->setEnabled(!disableAllPowerUps);
        ui->cbMetroidDisablePickupDeathalt->setEnabled(!disableAllPowerUps);
    };
    connect(
        ui->cbMetroidDisablePickupPowerUps,
        &QCheckBox::toggled,
        this,
        [updatePickupPowerUpChildren](bool disableAllPowerUps) {
            updatePickupPowerUpChildren(disableAllPowerUps, true);
        });
    const bool disableAllPowerUps = ui->cbMetroidDisablePickupPowerUps->isChecked();
    updatePickupPowerUpChildren(disableAllPowerUps, disableAllPowerUps);

    // Segment 3e [37,39): in-game scaling.
    // Segment 3f [39,45): Low HP warning thresholds.
    // Both are contiguous with no interleaved special logic, so apply [37,45).
    loadBindingsRange(instcfg, 37, 45);
    updateLowHpWarningControls(ui->comboMetroidLowHpWarningMode->currentIndex());
}


void MelonPrimeInputConfig::setupInputMethodSection(Config::Table& instcfg)
{
    if (m_btnToggleInputMethod
        || m_sectionInputMethod
        || m_cbMetroidUseNewWeaponSwitchMethod
        || m_cbMetroidUseNewBipedFireMethod
        || m_cbMetroidUseNewTransformMethod
        || m_cbMetroidUseNewZoomMethod
        || m_cbMetroidUseNewZoomMethod2)
    {
        return;
    }

    auto* parentLayout = qobject_cast<QVBoxLayout*>(ui->sectionInputSettings->parentWidget()->layout());
    if (!parentLayout)
        return;

    m_btnToggleInputMethod = new QPushButton(this);
    m_btnToggleInputMethod->setCheckable(true);
    m_btnToggleInputMethod->setChecked(false);
    m_btnToggleInputMethod->setFlat(true);
    m_btnToggleInputMethod->setText(QString::fromUtf8("▶ INPUT METHOD"));
    m_btnToggleInputMethod->setStyleSheet(
        "QPushButton { text-align: left; font-weight: bold; padding: 4px; } "
        "QPushButton::checked { font-weight: bold; }");

    m_sectionInputMethod = new QWidget(this);
    auto* sectionLayout = new QVBoxLayout(m_sectionInputMethod);
    auto* developerLayout = ui->vboxDeveloperOnly;
    int developerInsertIndex = developerLayout->indexOf(ui->cbMetroidEnableNativeAimRegisterInjection);
    if (developerInsertIndex < 0)
        developerInsertIndex = developerLayout->count();
    auto addDeveloperSpacing = [&developerLayout, &developerInsertIndex]() {
        developerLayout->insertSpacing(developerInsertIndex++, 6);
    };
    auto addDeveloperWidget = [&developerLayout, &developerInsertIndex](QWidget* widget) {
        developerLayout->insertWidget(developerInsertIndex++, widget);
    };
    auto moveExistingWidget = [](QWidget* widget) {
        if (!widget)
            return;
        if (QWidget* const parent = widget->parentWidget()) {
            if (QLayout* const layout = parent->layout())
                layout->removeWidget(widget);
        }
    };

    moveExistingWidget(ui->cbMetroidEnableNativeAimPostFoldWrite);
    moveExistingWidget(ui->lblMetroidNativeAimHookModeDesc);
    ui->cbMetroidEnableNativeAimPostFoldWrite->setParent(ui->sectionDeveloperOnly);
    ui->lblMetroidNativeAimHookModeDesc->setParent(ui->sectionDeveloperOnly);
    ui->cbMetroidEnableNativeAimPostFoldWrite->setEnabled(false);
    ui->lblMetroidNativeAimHookModeDesc->setEnabled(false);
    ui->lblMetroidNativeAimHookModeDesc->setText(
        "PostFold Write hooks after TouchInputProcessor and covers all AltForms including spec108=0 (Samus/Kanden/Noxus/Spire). Developer build only.");
    ui->lblMetroidNativeAimHookModeDesc->setStyleSheet("QLabel { margin-left: 20px; }");
    addDeveloperSpacing();
    addDeveloperWidget(ui->cbMetroidEnableNativeAimPostFoldWrite);
    addDeveloperWidget(ui->lblMetroidNativeAimHookModeDesc);

    m_cbMetroidUseNewWeaponSwitchMethod = new QCheckBox(
        "Use New Method for Weapon Change",
        m_sectionInputMethod);
    m_cbMetroidUseNewWeaponSwitchMethod->setToolTip(
        "Checked: use the native ARM9 game function hook. "
        "Unchecked: use the older touch/menu simulation path.");
    m_cbMetroidUseNewWeaponSwitchMethod->setChecked(
        std::clamp(instcfg.GetInt(MelonPrime::CfgKey::WeaponSwitchMethod), 0, 1)
            == MelonPrime::WeaponSwitchMethod::NewNative);
    sectionLayout->addWidget(m_cbMetroidUseNewWeaponSwitchMethod);

    auto* desc = new QLabel(
        "Checked uses the game's native TryEquipWeapon path through an ARM9 hook. "
        "Unchecked keeps the older simulated touch/menu weapon switching path for compatibility testing.",
        m_sectionInputMethod);
    desc->setObjectName(QStringLiteral("lblMetroidWeaponSwitchMethodDesc"));
    desc->setWordWrap(true);
    desc->setStyleSheet("QLabel { margin-left: 20px; }");
    sectionLayout->addWidget(desc);

    m_cbMetroidUseNewBipedFireMethod = new QCheckBox(
        "Use New Method for Biped Fire",
        ui->sectionDeveloperOnly);
    m_cbMetroidUseNewBipedFireMethod->setToolTip(
        "Checked: inject a native fire edge inside the game's Biped fire update. "
        "Unchecked: use the older fixed input/overlay path.");
    m_cbMetroidUseNewBipedFireMethod->setChecked(
        kDeveloperOnlyFeaturesEnabled
            && std::clamp(instcfg.GetInt(MelonPrime::CfgKey::BipedFireMethod), 0, 1)
                == MelonPrime::BipedFireMethod::NewNativeEdge);
    m_cbMetroidUseNewBipedFireMethod->setEnabled(kDeveloperOnlyFeaturesEnabled);
    addDeveloperSpacing();
    addDeveloperWidget(m_cbMetroidUseNewBipedFireMethod);

    auto* fireDesc = new QLabel(
        "Checked sets the fire input-helper result true at the game's Biped fire edge hook, "
        "letting the original cooldown, ammo, projectile, HUD, and SFX path run naturally. "
        "Legacy Method keeps the older DS input/ImmediateInputEdgeOverlay fire path.",
        ui->sectionDeveloperOnly);
    fireDesc->setObjectName(QStringLiteral("lblMetroidBipedFireMethodDesc"));
    fireDesc->setWordWrap(true);
    fireDesc->setEnabled(kDeveloperOnlyFeaturesEnabled);
    fireDesc->setStyleSheet("QLabel { margin-left: 20px; }");
    addDeveloperWidget(fireDesc);

    m_cbMetroidUseNewTransformMethod = new QCheckBox(
        "Use New Method for Alt-Form Transform",
        m_sectionInputMethod);
    m_cbMetroidUseNewTransformMethod->setToolTip(
        "Checked: use the native ARM9 TransformRequest hook. "
        "Unchecked: use the older touch/menu simulation path.");
    m_cbMetroidUseNewTransformMethod->setChecked(
        instcfg.GetBool(MelonPrime::CfgKey::DirectAltFormTransform));
    sectionLayout->addSpacing(6);
    sectionLayout->addWidget(m_cbMetroidUseNewTransformMethod);

    moveExistingWidget(ui->cbMetroidEnableDirectAltFormTransform);
    moveExistingWidget(ui->lblMetroidDirectAltFormTransformDesc);
    ui->cbMetroidEnableDirectAltFormTransform->hide();
    ui->lblMetroidDirectAltFormTransformDesc->setText(
        "New Method redirects a short native input gate into the game's TransformRequest path. "
        "Legacy Method keeps the older simulated touch/menu transform path.");
    ui->lblMetroidDirectAltFormTransformDesc->setStyleSheet("QLabel { margin-left: 20px; }");
    sectionLayout->addWidget(ui->lblMetroidDirectAltFormTransformDesc);

    const int zoomMethod = std::clamp(instcfg.GetInt(MelonPrime::CfgKey::ZoomInputMethod), 0, 2);

    m_cbMetroidUseNewZoomMethod = new QCheckBox(
        "Use New Method for Zoom",
        ui->sectionDeveloperOnly);
    m_cbMetroidUseNewZoomMethod->setToolTip(
        "Checked: use the current in-game zoom binding from the player's control preset. "
        "Unchecked: use the older fixed R-button path.");
    m_cbMetroidUseNewZoomMethod->setChecked(
        kDeveloperOnlyFeaturesEnabled
        && zoomMethod == MelonPrime::ZoomInputMethod::NewPresetBinding);
    m_cbMetroidUseNewZoomMethod->setEnabled(kDeveloperOnlyFeaturesEnabled);

    m_cbMetroidUseNewZoomMethod2 = new QCheckBox(
        "Use New Method 2 for Zoom",
        ui->sectionDeveloperOnly);
    m_cbMetroidUseNewZoomMethod2->setToolTip(
        "Checked: toggle native weapon zoom by calling the game's SetPlayerScopeZoom setter. "
        "Unchecked with New Method also off: use Legacy fixed R-button input.");
    m_cbMetroidUseNewZoomMethod2->setChecked(
        kDeveloperOnlyFeaturesEnabled
        && zoomMethod == MelonPrime::ZoomInputMethod::NewNativeToggle);
    m_cbMetroidUseNewZoomMethod2->setEnabled(kDeveloperOnlyFeaturesEnabled);

    addDeveloperSpacing();
    addDeveloperWidget(m_cbMetroidUseNewZoomMethod);
    addDeveloperWidget(m_cbMetroidUseNewZoomMethod2);

    connect(
        m_cbMetroidUseNewZoomMethod,
        MELONPRIME_CHECKBOX_STATE_CHANGED_SIGNAL,
        this,
        [this](auto state) {
            if (state == Qt::Checked && m_cbMetroidUseNewZoomMethod2)
                m_cbMetroidUseNewZoomMethod2->setChecked(false);
        });
    connect(
        m_cbMetroidUseNewZoomMethod2,
        MELONPRIME_CHECKBOX_STATE_CHANGED_SIGNAL,
        this,
        [this](auto state) {
            if (state == Qt::Checked && m_cbMetroidUseNewZoomMethod)
                m_cbMetroidUseNewZoomMethod->setChecked(false);
        });

    auto* zoomDesc = new QLabel(
        "New Method reads the game's zoom binding table, so Touch and Dual presets can map zoom to different DS buttons. "
        "It is also slightly lower latency than Legacy Method. "
        "If both boxes are unchecked, Legacy Method always drives the fixed R button like the older input path.",
        ui->sectionDeveloperOnly);
    zoomDesc->setObjectName(QStringLiteral("lblMetroidZoomMethodDesc"));
    zoomDesc->setWordWrap(true);
    zoomDesc->setEnabled(kDeveloperOnlyFeaturesEnabled);
    zoomDesc->setStyleSheet("QLabel { margin-left: 20px; }");

    auto* zoom2Desc = new QLabel(
        "New Method 2 toggles native zoom state through SetPlayerScopeZoom on each press. "
        "Mutually exclusive with New Method for Zoom.",
        ui->sectionDeveloperOnly);
    zoom2Desc->setObjectName(QStringLiteral("lblMetroidZoomMethod2Desc"));
    zoom2Desc->setWordWrap(true);
    zoom2Desc->setEnabled(kDeveloperOnlyFeaturesEnabled);
    zoom2Desc->setStyleSheet("QLabel { margin-left: 20px; }");

    addDeveloperWidget(zoomDesc);
    addDeveloperWidget(zoom2Desc);

    int insertIndex = parentLayout->indexOf(ui->sectionInputSettings);
    if (insertIndex < 0)
        insertIndex = parentLayout->count();
    else
        ++insertIndex;

    parentLayout->insertWidget(insertIndex, m_btnToggleInputMethod);
    parentLayout->insertWidget(insertIndex + 1, m_sectionInputMethod);
    updateAimControlsForStylusMode(ui->cbMetroidEnableStylusMode->isChecked());
}



void MelonPrimeInputConfig::updateAimControlsForStylusMode(bool stylusEnabled)
{
    const bool enableAimControls = !stylusEnabled;
    ui->metroidAimSensitvitySpinBox->setEnabled(enableAimControls);
    ui->metroidAimSensitvityLabel->setEnabled(enableAimControls);
    ui->metroidAimYAxisScaleSpinBox->setEnabled(enableAimControls);
    ui->metroidAimYAxisScaleLabel->setEnabled(enableAimControls);
    ui->metroidAimYAxisScaleLabel2->setEnabled(enableAimControls);
    ui->metroidAimAdjustSpinBox->setEnabled(enableAimControls);
    ui->metroidAimAdjustLabel->setEnabled(enableAimControls);
    ui->cbMetroidEnableAimAccumulator->setEnabled(enableAimControls);
    if (m_cbMetroidZoomAimScaleEnable)
        m_cbMetroidZoomAimScaleEnable->setEnabled(enableAimControls);
    const bool enableZoomAimScale =
        enableAimControls
        && m_cbMetroidZoomAimScaleEnable
        && m_cbMetroidZoomAimScaleEnable->isChecked();
    if (m_lblMetroidZoomAimScalePct)
        m_lblMetroidZoomAimScalePct->setEnabled(enableZoomAimScale);
    if (m_spinMetroidZoomAimScalePct)
        m_spinMetroidZoomAimScalePct->setEnabled(enableZoomAimScale);
    if (m_lblMetroidZoomAimScaleDesc)
        m_lblMetroidZoomAimScaleDesc->setEnabled(enableZoomAimScale);
    const bool enableLowLatencyAimMode =
        enableAimControls && ui->cbMetroidDisableMphAimSmoothing->isChecked();
    if (m_lblMetroidLowLatencyAimMode)
        m_lblMetroidLowLatencyAimMode->setEnabled(enableLowLatencyAimMode);
    if (m_comboMetroidLowLatencyAimMode)
        m_comboMetroidLowLatencyAimMode->setEnabled(enableLowLatencyAimMode);
    if (m_lblMetroidLowLatencyAimDesc)
        m_lblMetroidLowLatencyAimDesc->setEnabled(enableLowLatencyAimMode);
    const bool enableAimHooks =
        kDeveloperOnlyFeaturesEnabled
        && enableAimControls
        && ui->cbMetroidDisableMphAimSmoothing->isChecked();
    const bool enableRegisterInjection = enableAimHooks;
    ui->cbMetroidEnableNativeAimPostFoldWrite->setEnabled(enableAimHooks);
    ui->lblMetroidNativeAimHookModeDesc->setEnabled(enableAimHooks);
    ui->cbMetroidEnableNativeAimRegisterInjection->setEnabled(enableRegisterInjection);
    ui->lblMetroidNativeAimRegisterInjectionDesc->setEnabled(enableRegisterInjection);
    ui->cbMetroidEnableImmediateInputEdgeOverlay->setEnabled(kDeveloperOnlyFeaturesEnabled && enableAimControls);
    ui->lblMetroidImmediateInputEdgeOverlayDesc->setEnabled(kDeveloperOnlyFeaturesEnabled && enableAimControls);
    if (m_cbMetroidUseNewTransformMethod)
        m_cbMetroidUseNewTransformMethod->setEnabled(true);
    if (m_cbMetroidUseNewBipedFireMethod)
        m_cbMetroidUseNewBipedFireMethod->setEnabled(kDeveloperOnlyFeaturesEnabled);
    if (m_cbMetroidUseNewZoomMethod)
        m_cbMetroidUseNewZoomMethod->setEnabled(true);
    if (m_cbMetroidUseNewZoomMethod2)
        m_cbMetroidUseNewZoomMethod2->setEnabled(kDeveloperOnlyFeaturesEnabled);
    ui->lblMetroidDirectAltFormTransformDesc->setEnabled(true);
}

void MelonPrimeInputConfig::setupCollapsibleSections(Config::Table& instcfg)
{
    // Custom HUD
    ui->cbMetroidEnableCustomHud->setChecked(instcfg.GetBool(MP_HUD_PROP_KEY_CustomHUD));

    // --- Collapsible sections: remember expand/collapse state ---
    auto setupToggle = [&instcfg](QPushButton* btn, QWidget* section, const QString& label, const char* cfgKey) {
        bool expanded = instcfg.GetBool(cfgKey);
        section->setVisible(expanded);
        btn->setChecked(expanded);
        btn->setText((expanded ? QString::fromUtf8("▼ ") : QString::fromUtf8("▶ ")) + label);
        QObject::connect(btn, &QPushButton::toggled, [btn, section, label](bool checked) {
            section->setVisible(checked);
            btn->setText((checked ? QString::fromUtf8("▼ ") : QString::fromUtf8("▶ ")) + MelonPrime::UiText::Tr(label));
        });
    };
    // Other Metroid Settings 2 tab
    setupToggle(ui->btnToggleInputSettings, ui->sectionInputSettings, "INPUT SETTINGS",   MelonPrime::CfgKey::SectionInputSettings);
    if (m_btnToggleInputMethod && m_sectionInputMethod) {
        setupToggle(
            m_btnToggleInputMethod,
            m_sectionInputMethod,
            "INPUT METHOD",
            MelonPrime::CfgKey::SectionInputMethod);
    }
    setupToggle(ui->btnToggleScreenSync,    ui->sectionScreenSync,    "SCREEN SYNC",      MelonPrime::CfgKey::SectionScreenSync);
    setupToggle(ui->btnToggleCursorClipSettings, ui->sectionCursorClipSettings, "CURSOR CLIP SETTINGS",  MelonPrime::CfgKey::SectionCursorClipSettings);
    setupToggle(ui->btnToggleInGameApply, ui->sectionInGameApply, "IN-GAME APPLY",  MelonPrime::CfgKey::SectionInGameApply);
    setupToggle(ui->btnToggleInGameAspectRatio, ui->sectionInGameAspectRatio, "IN-GAME ASPECT RATIO",  MelonPrime::CfgKey::SectionInGameAspectRatio);
    setupToggle(ui->btnToggleLowHpWarning, ui->sectionLowHpWarning, "LOW HP WARNING",  MelonPrime::CfgKey::SectionLowHpWarning);
    // Other Metroid Settings tab
    setupToggle(ui->btnToggleSensitivity, ui->sectionSensitivity, "SENSITIVITY",      MelonPrime::CfgKey::SectionSensitivity);
    setupToggle(ui->btnToggleBugFix,        ui->sectionBugFix,        "BUG FIXES",                   MelonPrime::CfgKey::SectionBugFix);
    setupToggle(ui->btnToggleGameFeature,   ui->sectionGameFeature,   "GAME FEATURE IMPROVEMENTS",   MelonPrime::CfgKey::SectionGameFeature);
    setupToggle(ui->btnToggleDisableFeatures, ui->sectionDisableFeatures, "DISABLE FEATURES",         MelonPrime::CfgKey::SectionDisableFeatures);
    setupToggle(ui->btnToggleDisablePickingUpSpecificItems, ui->sectionDisablePickingUpSpecificItems,
                "Power-Up Pickup Effects", MelonPrime::CfgKey::SectionPowerUpPickupEffects);
    setupToggle(ui->btnToggleGameplay,      ui->sectionGameplay,      "GAMEPLAY TOGGLES",             MelonPrime::CfgKey::SectionGameplay);
    setupToggle(ui->btnToggleVideo,       ui->sectionVideo,       "VIDEO QUALITY",    MelonPrime::CfgKey::SectionVideo);
    setupToggle(ui->btnToggleVolume,      ui->sectionVolume,      "VOLUME",           MelonPrime::CfgKey::SectionVolume);
    setupToggle(ui->btnToggleLicense,     ui->sectionLicense,     "LICENSE APPLY",    MelonPrime::CfgKey::SectionLicense);
    // Restore note: remove this toggle if the DEVELOPER ONLY section is removed.
    if constexpr (kDeveloperOnlyFeaturesEnabled) {
        setupToggle(ui->btnToggleDeveloperOnly, ui->sectionDeveloperOnly, "DEVELOPER ONLY", MelonPrime::CfgKey::SectionDeveloperOnly);
    } else {
        // Non-developer build: hide the section entirely instead of greying it out.
        // Child widgets parented to sectionDeveloperOnly are hidden along with it.
        ui->btnToggleDeveloperOnly->setVisible(false);
        ui->sectionDeveloperOnly->setVisible(false);
    }
}


void MelonPrimeInputConfig::setupPreviewConnections()
{
    // --- Global (affects visual preview) ---
    connect(ui->cbMetroidEnableCustomHud, MELONPRIME_CHECKBOX_STATE_CHANGED_SIGNAL, this, [this](auto) { applyVisualPreview(); });
    connect(ui->cbMetroidInGameAspectRatio, MELONPRIME_CHECKBOX_STATE_CHANGED_SIGNAL, this, [this](auto) { applyVisualPreview(); });
    connect(ui->comboMetroidInGameAspectRatioMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyVisualPreview(); });
}


MelonPrimeInputConfig::~MelonPrimeInputConfig()
{
    if (emuInstance)
        MelonPrime::UiText::SetMenuLanguageSelection(
            MelonPrime::UiText::NormalizeMenuLanguageConfig(
                emuInstance->getLocalConfig().GetInt(MelonPrime::CfgKey::MenuLanguage)));
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

    group = new QGroupBox("Keyboard && mouse mappings:");
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
    ui->metroidAimYAxisScaleSpinBox->setValue(1.514700);
    ui->metroidAimAdjustSpinBox->setValue(0.010000);
    if (m_cbMetroidZoomAimScaleEnable)
        m_cbMetroidZoomAimScaleEnable->setChecked(false);
    if (m_spinMetroidZoomAimScalePct)
        m_spinMetroidZoomAimScalePct->setValue(75);
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
#ifdef __APPLE__ // scatter-budget-exempt: video-quality preset UI gate, not input dispatch
    // Defense in depth: the button is disabled on macOS, but guard the slot
    // itself in case it is ever invoked directly (auto-connect, future
    // callers, etc). macOS OpenGL does not support the compute renderer.
    return;
#else
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.UseGL", true);
    cfg.SetBool("Screen.VSync", false);
    cfg.SetInt("Screen.VSyncInterval", 1);
    cfg.SetInt("3D.Renderer", renderer3D_OpenGLCompute);
    cfg.SetBool("3D.Soft.Threaded", true);
    cfg.SetInt("3D.GL.ScaleFactor", 4);
    cfg.SetBool("3D.GL.BetterPolygons", true);
#endif
}

void MelonPrimeInputConfig::on_cbMetroidEnableSnapTap_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool(MelonPrime::CfgKey::SnapTap, state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidUnlockAll_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool(MelonPrime::CfgKey::DataUnlock, state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidApplyHeadphone_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool(MelonPrime::CfgKey::Headphone, state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidUseFirmwareName_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool(MelonPrime::CfgKey::UseFwName, state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidEnableCustomHud_stateChanged(int state)
{
    auto& cfg = emuInstance->getLocalConfig();
    cfg.SetBool(MP_HUD_PROP_KEY_CustomHUD, state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidEnableStylusMode_stateChanged(int state)
{
    updateAimControlsForStylusMode(state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidDisableMphAimSmoothing_stateChanged(int)
{
    updateAimControlsForStylusMode(ui->cbMetroidEnableStylusMode->isChecked());
}

void MelonPrimeInputConfig::on_cbMetroidExpandStageMatrix_stateChanged(int state)
{
    const bool checked = (state == Qt::Checked);
    ui->cbMetroidExpandStageMatrixExtra->setEnabled(checked);
    if (!checked)
        ui->cbMetroidExpandStageMatrixExtra->setChecked(false);
}

void MelonPrimeInputConfig::updateLowHpWarningControls(int mode)
{
    // Mode: 0=Disabled, 1=Fixed, 2=Per Damage, 3=Auto Scale
    const bool fixed     = (mode == 1);
    const bool perDamage = (mode == 2);
    const bool autoScale = (mode == 3);
    ui->lblMetroidLowHpWarningFixed->setEnabled(fixed);
    ui->spinMetroidLowHpWarningFixed->setEnabled(fixed);
    ui->lblMetroidLowHpWarningLow->setEnabled(perDamage);
    ui->spinMetroidLowHpWarningLow->setEnabled(perDamage);
    ui->lblMetroidLowHpWarningMedium->setEnabled(perDamage);
    ui->spinMetroidLowHpWarningMedium->setEnabled(perDamage);
    ui->lblMetroidLowHpWarningHigh->setEnabled(perDamage);
    ui->spinMetroidLowHpWarningHigh->setEnabled(perDamage);
    ui->lblMetroidLowHpWarningAutoBase->setEnabled(autoScale);
    ui->spinMetroidLowHpWarningAutoBase->setEnabled(autoScale);
}

void MelonPrimeInputConfig::on_comboMetroidLowHpWarningMode_currentIndexChanged(int index)
{
    updateLowHpWarningControls(index);
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


// HUD widget descriptors

namespace {

#include "MelonPrimeInputConfigHudTables.inc"

#include "MelonPrimeInputConfigHudPreviews.inc"

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
#include "MelonPrimeInputConfigCustomHudBuild.inc"
}


#include "MelonPrimeInputConfigCustomHudCode.inc"

void MelonPrimeInputConfig::refreshAfterHudEditSave()
{
    // Edit HUD Layout wrote directly to config. Reload all widget values so
    // that clicking OK in the settings dialog doesn't overwrite the new positions.
    if (!visualSnapshotTargetsAlive())
        return;

    auto& cfg = emuInstance->getLocalConfig();
    m_applyPreviewEnabled = false;
    for (auto& [key, widget] : m_hudWidgets) {
        if (!widget)
            continue;
        widget->blockSignals(true);
        if (auto* cb = qobject_cast<QCheckBox*>(widget.data()))
            cb->setChecked(cfg.GetBool(key));
        else if (auto* sb = qobject_cast<QSpinBox*>(widget.data()))
            sb->setValue(cfg.GetInt(key));
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(widget.data()))
            dsb->setValue(cfg.GetDouble(key));
        else if (auto* le = qobject_cast<QLineEdit*>(widget.data()))
            le->setText(QString::fromStdString(cfg.GetString(key)));
        else if (auto* fc = qobject_cast<QFontComboBox*>(widget.data()))   // before QComboBox: stores family string
            fc->setCurrentFont(QFont(QString::fromStdString(cfg.GetString(key))));
        else if (auto* combo = qobject_cast<QComboBox*>(widget.data()))
            combo->setCurrentIndex(cfg.GetInt(key));
        widget->blockSignals(false);
    }
    m_applyPreviewEnabled = true;
    snapshotVisualConfig();
}
