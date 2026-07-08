#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeHudConfigOnScreenEdit.h"
#include "MelonPrimeHudEditorFormBuilder.h"
#include "MelonPrimeHudPropSchema.inc"
#include "MelonPrimeHudRender.h"
#include "MelonPrimeLocalization.h"
#include "EmuInstance.h"
#include <QApplication>
#include <QFont>
#include <QFrame>

namespace
{
constexpr int kPanelWidth = 300;
} // namespace

// ─── Construction ───────────────────────────────────────────────────────────

MelonPrimeHudConfigOnScreenEdit::MelonPrimeHudConfigOnScreenEdit(QWidget* parent, EmuInstance* emu)
    : QWidget(parent), m_emu(emu)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Window);
    setPalette(QApplication::palette());

    QFont panelFont = font();
    panelFont.setPixelSize(9);
    setFont(panelFont);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(6, 4, 6, 4);
    outerLayout->setSpacing(2);

    m_title = new QLabel(this);
    QFont titleFont = panelFont;
    titleFont.setBold(true);
    titleFont.setPixelSize(10);
    m_title->setFont(titleFont);
    m_title->setAlignment(Qt::AlignCenter);
    outerLayout->addWidget(m_title);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setPalette(palette());
    m_scroll->setBackgroundRole(QPalette::Window);
    m_scroll->viewport()->setPalette(palette());
    m_scroll->viewport()->setBackgroundRole(QPalette::Window);
    m_scroll->viewport()->setAutoFillBackground(true);
    outerLayout->addWidget(m_scroll);

    m_inner = new QWidget();
    m_inner->setPalette(palette());
    m_inner->setBackgroundRole(QPalette::Window);
    m_inner->setAutoFillBackground(true);
    m_form = new QFormLayout(m_inner);
    m_form->setContentsMargins(0, 0, 0, 0);
    m_form->setSpacing(3);
    m_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_scroll->setWidget(m_inner);

    setFixedWidth(kPanelWidth);
    hide();
}

// ─── Config access ──────────────────────────────────────────────────────────

Config::Table& MelonPrimeHudConfigOnScreenEdit::cfg()
{
    return m_emu->getLocalConfig();
}

// ─── Clear ──────────────────────────────────────────────────────────────────

void MelonPrimeHudConfigOnScreenEdit::clearForm()
{
    while (m_form->rowCount() > 0)
        m_form->removeRow(0);
    m_rows.clear();
}

void MelonPrimeHudConfigOnScreenEdit::clear()
{
    clearForm();
    m_currentElem = -1;
    m_title->clear();
    hide();
}

// ─── Reload values ──────────────────────────────────────────────────────────

void MelonPrimeHudConfigOnScreenEdit::reloadValues()
{
    if (m_currentElem >= 0)
        populateForElement(m_currentElem);
}

// ─── Factory: CheckBox ──────────────────────────────────────────────────────

QWidget* MelonPrimeHudConfigOnScreenEdit::addCheckBox(const QString& label, const char* key)
{
    auto ctx = MelonPrime::HudEditorForm::MakeFactoryContext(
        *this, *m_form, m_rows, cfg(), m_populating, *this);
    return MelonPrime::HudEditorForm::AddBoolRadioRow(ctx, label, key);
}

// ─── Factory: ComboBox ──────────────────────────────────────────────────────

QComboBox* MelonPrimeHudConfigOnScreenEdit::addComboBox(const QString& label, const char* key, const QStringList& items)
{
    auto ctx = MelonPrime::HudEditorForm::MakeFactoryContext(
        *this, *m_form, m_rows, cfg(), m_populating, *this);
    return MelonPrime::HudEditorForm::AddComboBoxRow(ctx, label, key, items);
}

// ─── Factory: SpinBox ───────────────────────────────────────────────────────

QSpinBox* MelonPrimeHudConfigOnScreenEdit::addSpinBox(const QString& label, const char* key, int min, int max)
{
    auto ctx = MelonPrime::HudEditorForm::MakeFactoryContext(
        *this, *m_form, m_rows, cfg(), m_populating, *this);
    return MelonPrime::HudEditorForm::AddSpinBoxRow(ctx, label, key, min, max);
}

// ─── Factory: DoubleSpinBox ─────────────────────────────────────────────────

QDoubleSpinBox* MelonPrimeHudConfigOnScreenEdit::addDoubleSpinBox(const QString& label, const char* key, double min, double max, double step)
{
    auto ctx = MelonPrime::HudEditorForm::MakeFactoryContext(
        *this, *m_form, m_rows, cfg(), m_populating, *this);
    return MelonPrime::HudEditorForm::AddDoubleSpinBoxRow(ctx, label, key, min, max, step);
}

// ─── Factory: OpacitySlider ─────────────────────────────────────────────────

QSlider* MelonPrimeHudConfigOnScreenEdit::addOpacitySlider(const QString& label, const char* key)
{
    auto ctx = MelonPrime::HudEditorForm::MakeFactoryContext(
        *this, *m_form, m_rows, cfg(), m_populating, *this);
    return MelonPrime::HudEditorForm::AddOpacitySliderRow(ctx, label, key);
}

// ─── Factory: LineEdit ──────────────────────────────────────────────────────

QLineEdit* MelonPrimeHudConfigOnScreenEdit::addLineEdit(const QString& label, const char* key)
{
    auto ctx = MelonPrime::HudEditorForm::MakeFactoryContext(
        *this, *m_form, m_rows, cfg(), m_populating, *this);
    return MelonPrime::HudEditorForm::AddLineEditRow(ctx, label, key);
}

// ─── Factory: Color Picker ──────────────────────────────────────────────────

QPushButton* MelonPrimeHudConfigOnScreenEdit::addColorPicker(const QString& label, const char* keyR, const char* keyG, const char* keyB)
{
    auto ctx = MelonPrime::HudEditorForm::MakeFactoryContext(
        *this, *m_form, m_rows, cfg(), m_populating, *this);
    return MelonPrime::HudEditorForm::AddColorPickerRow(ctx, label, keyR, keyG, keyB);
}

// ─── Factory: Sub-Color (with "Overall" toggle) ─────────────────────────────

void MelonPrimeHudConfigOnScreenEdit::addSubColor(const QString& label, const char* overallKey,
    const char* keyR, const char* keyG, const char* keyB)
{
    auto ctx = MelonPrime::HudEditorForm::MakeFactoryContext(
        *this, *m_form, m_rows, cfg(), m_populating, *this);
    MelonPrime::HudEditorForm::AddSubColorRow(ctx, label, overallKey, keyR, keyG, keyB);
}

void MelonPrimeHudConfigOnScreenEdit::addColorOverlayRow(
    const QString& label, const char* enableKey,
    const char* keyR, const char* keyG, const char* keyB)
{
    auto ctx = MelonPrime::HudEditorForm::MakeFactoryContext(
        *this, *m_form, m_rows, cfg(), m_populating, *this);
    MelonPrime::HudEditorForm::AddColorOverlayRow(ctx, label, enableKey, keyR, keyG, keyB);
}

// ─── Separator ──────────────────────────────────────────────────────────────

void MelonPrimeHudConfigOnScreenEdit::addSeparator()
{
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    m_form->addRow(line);
    m_rows.append(line);
}

// MP_OUTLINE_KEYS(pfx) expands a HUD element prefix to the six outline config-key
// macros (enable, color R/G/B, opacity, thickness) defined in MelonPrimeHudPropSchema.inc.
// The token paste yields e.g. MP_HUD_PROP_KEY_HudHpOutline, which the preprocessor then
// rescans into the Metroid.Visual.HudHpOutline string literal. A wrong prefix pastes an
// undefined macro name -> compile error (the intended drift guard).
#define MP_OUTLINE_KEYS(pfx)                  \
    MP_HUD_PROP_KEY_##pfx##Outline,           \
    MP_HUD_PROP_KEY_##pfx##OutlineColorR,     \
    MP_HUD_PROP_KEY_##pfx##OutlineColorG,     \
    MP_HUD_PROP_KEY_##pfx##OutlineColorB,     \
    MP_HUD_PROP_KEY_##pfx##OutlineOpacity,    \
    MP_HUD_PROP_KEY_##pfx##OutlineThickness

void MelonPrimeHudConfigOnScreenEdit::addOutlineGroup(const char* enableKey,
    const char* colorR, const char* colorG, const char* colorB,
    const char* opacityKey, const char* thicknessKey)
{
    addOutlineGroupSection(QString(), enableKey, colorR, colorG, colorB, opacityKey, thicknessKey);
}

void MelonPrimeHudConfigOnScreenEdit::addOutlineGroupSection(const QString& sectionLabel,
    const char* enableKey, const char* colorR, const char* colorG, const char* colorB,
    const char* opacityKey, const char* thicknessKey)
{
    addSeparator();
    if (!sectionLabel.isEmpty())
        addSectionHeader(sectionLabel);
    addCheckBox(QStringLiteral("Outline"), enableKey);
    addColorPicker(QStringLiteral("Outline Color"), colorR, colorG, colorB);
    addOpacitySlider(QStringLiteral("Outline Opacity"), opacityKey);
    addSpinBox(QStringLiteral("Outline Thick."), thicknessKey, 1, 10);
}

// ─── Built-ins: Show / Color / Anchor ───────────────────────────────────────

void MelonPrimeHudConfigOnScreenEdit::addBuiltins(const char* showKey,
    const char* colorR, const char* colorG, const char* colorB,
    const char* anchorKey)
{
    static const QStringList kAnchorItems = {
        QStringLiteral("Top Left"), QStringLiteral("Top Center"), QStringLiteral("Top Right"),
        QStringLiteral("Mid Left"), QStringLiteral("Mid Center"), QStringLiteral("Mid Right"),
        QStringLiteral("Bot Left"), QStringLiteral("Bot Center"), QStringLiteral("Bot Right")
    };
    if (showKey)
        addCheckBox(QStringLiteral("Show"), showKey);
    if (colorR)
        addColorPicker(QStringLiteral("Color"), colorR, colorG, colorB);
    addComboBox(QStringLiteral("Anchor"), anchorKey, kAnchorItems);
    addSeparator();
}

void MelonPrimeHudConfigOnScreenEdit::addOffsetRows(const char* keyX, const char* keyY, int min, int max,
    const QString& labelX, const QString& labelY)
{
    addSpinBox(labelX, keyX, min, max);
    addSpinBox(labelY, keyY, min, max);
}

QComboBox* MelonPrimeHudConfigOnScreenEdit::addAlign3Combo(const QString& label, const char* key)
{
    return addComboBox(label, key,
        {QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
}

void MelonPrimeHudConfigOnScreenEdit::addGaugePositionRows(const char* posModeKey,
    const char* gaugeAnchorKey, const char* gaugeOffsetXKey, const char* gaugeOffsetYKey,
    const char* gaugePosXKey, const char* gaugePosYKey,
    const char* textAnchorKey, const char* textOffsetXKey, const char* textOffsetYKey)
{
    static const QStringList kGaugeSides = {
        QStringLiteral("Below"), QStringLiteral("Above"), QStringLiteral("Right"),
        QStringLiteral("Left"), QStringLiteral("Center")
    };

    addComboBox(QStringLiteral("Position Mode"), posModeKey,
        {QStringLiteral("Gauge \u2192 Text"), QStringLiteral("Independent"), QStringLiteral("Text \u2192 Gauge")});
    addComboBox(QStringLiteral("Gauge Side"), gaugeAnchorKey, kGaugeSides);
    addOffsetRows(gaugeOffsetXKey, gaugeOffsetYKey, -128, 128,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addOffsetRows(gaugePosXKey, gaugePosYKey, -256, 256,
        QStringLiteral("Gauge X"), QStringLiteral("Gauge Y"));
    addComboBox(QStringLiteral("Text Side"), textAnchorKey, kGaugeSides);
    addOffsetRows(textOffsetXKey, textOffsetYKey, -128, 128,
        QStringLiteral("Text Offset X"), QStringLiteral("Text Offset Y"));
}

void MelonPrimeHudConfigOnScreenEdit::addSectionHeader(const QString& label)
{
    auto* hdr = new QLabel(MelonPrime::UiText::Tr(label), this);
    QFont headerFont = hdr->font();
    headerFont.setBold(true);
    hdr->setFont(headerFont);
    m_form->addRow(hdr);
    m_rows.append(hdr);
}

// ─── Main populate dispatch ─────────────────────────────────────────────────

static const char* kElementNames[] = {
    "HP", "HP Gauge", "Weapon/Ammo", "Weapon Icon", "Ammo Gauge",
    "Match Status", "Rank", "Time Left", "Time Limit",
    "Bomb Left", "Bomb Icon", "Radar", "Weapon Inventory", "Crosshair"
};

void MelonPrimeHudConfigOnScreenEdit::populateForElement(int idx)
{
    m_populating = true;
    clearForm();
    m_currentElem = idx;

    if (idx < 0 || idx >= 14) {
        hide();
        m_populating = false;
        return;
    }

    m_title->setText(MelonPrime::UiText::Tr(kElementNames[idx]));

    switch (idx) {
    case 0:  populateHP(); break;
    case 1:  populateHPGauge(); break;
    case 2:  populateWeaponAmmo(); break;
    case 3:  populateWpnIcon(); break;
    case 4:  populateAmmoGauge(); break;
    case 5:  populateMatchStatus(); break;
    case 6:  populateRank(); break;
    case 7:  populateTimeLeft(); break;
    case 8:  populateTimeLimit(); break;
    case 9:  populateBombLeft(); break;
    case 10: populateBombIcon(); break;
    case 11: populateRadar(); break;
    case 12: populateWeaponInventory(); break;
    case 13: populateForCrosshair(); return; // already clears/sets m_populating internally
    }

    // NOTE: do not call show() here — the caller (Screen.cpp callback)
    // positions the widget first, then shows it to avoid a visual flash.
    m_populating = false;
}

// ─── Per-element populate ───────────────────────────────────────────────────

void MelonPrimeHudConfigOnScreenEdit::populateHP()
{
    addBuiltins(nullptr,
        MP_HUD_PROP_KEY_HudHpTextColorR, MP_HUD_PROP_KEY_HudHpTextColorG, MP_HUD_PROP_KEY_HudHpTextColorB,
        MP_HUD_PROP_KEY_HudHpAnchor);
    addOffsetRows(MP_HUD_PROP_KEY_HudHpX, MP_HUD_PROP_KEY_HudHpY, -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addLineEdit(QStringLiteral("Prefix"), MP_HUD_PROP_KEY_HudHpPrefix);
    addAlign3Combo(QStringLiteral("Align"), MP_HUD_PROP_KEY_HudHpAlign);
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_HudHpOpacity);
    addOutlineGroupSection(QStringLiteral("HP Outline"), MP_OUTLINE_KEYS(HudHp));
}

void MelonPrimeHudConfigOnScreenEdit::populateHPGauge()
{
    addBuiltins(MP_HUD_PROP_KEY_HudHpGauge,
        MP_HUD_PROP_KEY_HudHpGaugeColorR, MP_HUD_PROP_KEY_HudHpGaugeColorG, MP_HUD_PROP_KEY_HudHpGaugeColorB,
        MP_HUD_PROP_KEY_HudHpGaugePosAnchor);
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_HudHpGaugeOpacity);
    addComboBox(QStringLiteral("Orientation"), MP_HUD_PROP_KEY_HudHpGaugeOrientation,
        {QStringLiteral("Horizontal"), QStringLiteral("Vertical")});
    addComboBox(QStringLiteral("Align"), MP_HUD_PROP_KEY_HudHpGaugeAlign,
        {QStringLiteral("Start"), QStringLiteral("Center"), QStringLiteral("End")});
    addSpinBox(QStringLiteral("Length"), MP_HUD_PROP_KEY_HudHpGaugeLength, 1, 192);
    addSpinBox(QStringLiteral("Width"), MP_HUD_PROP_KEY_HudHpGaugeWidth, 1, 20);
    addGaugePositionRows(MP_HUD_PROP_KEY_HudHpGaugePosMode,
        MP_HUD_PROP_KEY_HudHpGaugeAnchor, MP_HUD_PROP_KEY_HudHpGaugeOffsetX, MP_HUD_PROP_KEY_HudHpGaugeOffsetY,
        MP_HUD_PROP_KEY_HudHpGaugePosX, MP_HUD_PROP_KEY_HudHpGaugePosY,
        MP_HUD_PROP_KEY_HudHpTextAnchor, MP_HUD_PROP_KEY_HudHpTextOffsetX, MP_HUD_PROP_KEY_HudHpTextOffsetY);
    addOutlineGroupSection(QStringLiteral("HP Gauge Outline"), MP_OUTLINE_KEYS(HudHpGauge));
}

void MelonPrimeHudConfigOnScreenEdit::populateWeaponAmmo()
{
    addBuiltins(nullptr,
        MP_HUD_PROP_KEY_HudAmmoTextColorR, MP_HUD_PROP_KEY_HudAmmoTextColorG, MP_HUD_PROP_KEY_HudAmmoTextColorB,
        MP_HUD_PROP_KEY_HudWeaponAnchor);
    addOffsetRows(MP_HUD_PROP_KEY_HudWeaponX, MP_HUD_PROP_KEY_HudWeaponY, -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addLineEdit(QStringLiteral("Prefix"), MP_HUD_PROP_KEY_HudAmmoPrefix);
    addAlign3Combo(QStringLiteral("Align"), MP_HUD_PROP_KEY_HudAmmoAlign);
    addComboBox(QStringLiteral("Layout"), MP_HUD_PROP_KEY_HudWeaponLayout,
        {QStringLiteral("Standard"), QStringLiteral("Alternative")});
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_HudWeaponOpacity);
    addOutlineGroupSection(QStringLiteral("Ammo Outline"), MP_OUTLINE_KEYS(HudWeapon));
}

void MelonPrimeHudConfigOnScreenEdit::populateWpnIcon()
{
    addBuiltins(MP_HUD_PROP_KEY_HudWeaponIconShow,
        nullptr, nullptr, nullptr,
        MP_HUD_PROP_KEY_HudWeaponIconPosAnchor);
    addOffsetRows(MP_HUD_PROP_KEY_HudWeaponIconPosX, MP_HUD_PROP_KEY_HudWeaponIconPosY, -256, 256,
        QStringLiteral("Pos X"), QStringLiteral("Pos Y"));
    addComboBox(QStringLiteral("Mode"), MP_HUD_PROP_KEY_HudWeaponIconMode,
        {QStringLiteral("Relative"), QStringLiteral("Independent")});
    addSpinBox(QStringLiteral("Height"), MP_HUD_PROP_KEY_HudWeaponIconHeight, 4, 64);
    addOffsetRows(MP_HUD_PROP_KEY_HudWeaponIconOffsetX, MP_HUD_PROP_KEY_HudWeaponIconOffsetY, -128, 128,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addAlign3Combo(QStringLiteral("Align X"), MP_HUD_PROP_KEY_HudWeaponIconAnchorX);
    addComboBox(QStringLiteral("Align Y"), MP_HUD_PROP_KEY_HudWeaponIconAnchorY,
        {QStringLiteral("Top"), QStringLiteral("Center"), QStringLiteral("Bottom")});
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_HudWpnIconOpacity);
    addOutlineGroupSection(QStringLiteral("Weapon Icon Outline"), MP_OUTLINE_KEYS(HudWeaponIcon));
    addSeparator();
    addSectionHeader(QStringLiteral("Weapon Icon Color Overlay"));
    // Per-weapon icon tint (enable + color per weapon).
    // Keys reference MelonPrimeHudPropSchema.inc macros so a typo fails to compile.
    struct WeaponTintRow { const char* label; const char* enableKey; const char* rKey; const char* gKey; const char* bKey; };
    static const WeaponTintRow kWeaponTints[9] = {
        {"Power Beam",    MP_HUD_PROP_KEY_HudWeaponIconColorOverlayPowerBeam,    MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRPowerBeam,    MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGPowerBeam,    MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBPowerBeam},
        {"Volt Driver",   MP_HUD_PROP_KEY_HudWeaponIconColorOverlayVoltDriver,   MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRVoltDriver,   MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGVoltDriver,   MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBVoltDriver},
        {"Missile",       MP_HUD_PROP_KEY_HudWeaponIconColorOverlayMissile,      MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRMissile,      MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGMissile,      MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBMissile},
        {"Battle Hammer", MP_HUD_PROP_KEY_HudWeaponIconColorOverlayBattleHammer, MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRBattleHammer, MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGBattleHammer, MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBBattleHammer},
        {"Imperialist",   MP_HUD_PROP_KEY_HudWeaponIconColorOverlayImperialist,  MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRImperialist,  MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGImperialist,  MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBImperialist},
        {"Judicator",     MP_HUD_PROP_KEY_HudWeaponIconColorOverlayJudicator,    MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRJudicator,    MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGJudicator,    MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBJudicator},
        {"Magmaul",       MP_HUD_PROP_KEY_HudWeaponIconColorOverlayMagmaul,      MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRMagmaul,      MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGMagmaul,      MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBMagmaul},
        {"Shock Coil",    MP_HUD_PROP_KEY_HudWeaponIconColorOverlayShockCoil,    MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRShockCoil,    MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGShockCoil,    MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBShockCoil},
        {"Omega Cannon",  MP_HUD_PROP_KEY_HudWeaponIconColorOverlayOmegaCannon,  MP_HUD_PROP_KEY_HudWeaponIconOverlayColorROmegaCannon,  MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGOmegaCannon,  MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBOmegaCannon},
    };
    for (const auto& w : kWeaponTints)
        addColorOverlayRow(QString::fromUtf8(w.label), w.enableKey, w.rKey, w.gKey, w.bKey);
}

void MelonPrimeHudConfigOnScreenEdit::populateAmmoGauge()
{
    addBuiltins(MP_HUD_PROP_KEY_HudAmmoGauge,
        MP_HUD_PROP_KEY_HudAmmoGaugeColorR, MP_HUD_PROP_KEY_HudAmmoGaugeColorG, MP_HUD_PROP_KEY_HudAmmoGaugeColorB,
        MP_HUD_PROP_KEY_HudAmmoGaugePosAnchor);
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_HudAmmoGaugeOpacity);
    addComboBox(QStringLiteral("Orientation"), MP_HUD_PROP_KEY_HudAmmoGaugeOrientation,
        {QStringLiteral("Horizontal"), QStringLiteral("Vertical")});
    addComboBox(QStringLiteral("Align"), MP_HUD_PROP_KEY_HudAmmoGaugeAlign,
        {QStringLiteral("Start"), QStringLiteral("Center"), QStringLiteral("End")});
    addSpinBox(QStringLiteral("Length"), MP_HUD_PROP_KEY_HudAmmoGaugeLength, 1, 192);
    addSpinBox(QStringLiteral("Width"), MP_HUD_PROP_KEY_HudAmmoGaugeWidth, 1, 20);
    addGaugePositionRows(MP_HUD_PROP_KEY_HudAmmoGaugePosMode,
        MP_HUD_PROP_KEY_HudAmmoGaugeAnchor, MP_HUD_PROP_KEY_HudAmmoGaugeOffsetX, MP_HUD_PROP_KEY_HudAmmoGaugeOffsetY,
        MP_HUD_PROP_KEY_HudAmmoGaugePosX, MP_HUD_PROP_KEY_HudAmmoGaugePosY,
        MP_HUD_PROP_KEY_HudAmmoTextAnchor, MP_HUD_PROP_KEY_HudAmmoTextOffsetX, MP_HUD_PROP_KEY_HudAmmoTextOffsetY);
    addOutlineGroupSection(QStringLiteral("Ammo Gauge Outline"), MP_OUTLINE_KEYS(HudAmmoGauge));
}

void MelonPrimeHudConfigOnScreenEdit::populateMatchStatus()
{
    addBuiltins(MP_HUD_PROP_KEY_HudMatchStatusShow,
        MP_HUD_PROP_KEY_HudMatchStatusColorR, MP_HUD_PROP_KEY_HudMatchStatusColorG, MP_HUD_PROP_KEY_HudMatchStatusColorB,
        MP_HUD_PROP_KEY_HudMatchStatusAnchor);
    addOffsetRows(MP_HUD_PROP_KEY_HudMatchStatusX, MP_HUD_PROP_KEY_HudMatchStatusY, -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_HudMatchStatusOpacity);
    addComboBox(QStringLiteral("Label Pos"), MP_HUD_PROP_KEY_HudMatchStatusLabelPos,
        {QStringLiteral("Above"), QStringLiteral("Below"), QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Center")});
    addOffsetRows(MP_HUD_PROP_KEY_HudMatchStatusLabelOfsX, MP_HUD_PROP_KEY_HudMatchStatusLabelOfsY, -128, 128,
        QStringLiteral("Label Ofs X"), QStringLiteral("Label Ofs Y"));
    addSeparator();
    addSectionHeader(QStringLiteral("Score Labels"));
    addLineEdit(QStringLiteral("Battle"), MP_HUD_PROP_KEY_HudMatchStatusLabelPoints);
    addLineEdit(QStringLiteral("Bounty"), MP_HUD_PROP_KEY_HudMatchStatusLabelOctoliths);
    addLineEdit(QStringLiteral("Survival"), MP_HUD_PROP_KEY_HudMatchStatusLabelLives);
    addLineEdit(QStringLiteral("Defender"), MP_HUD_PROP_KEY_HudMatchStatusLabelRingTime);
    addLineEdit(QStringLiteral("Prime"), MP_HUD_PROP_KEY_HudMatchStatusLabelPrimeTime);
    addSeparator();
    addSectionHeader(QStringLiteral("Score Colors"));
    addSubColor(QStringLiteral("Label"),
        MP_HUD_PROP_KEY_HudMatchStatusLabelColorOverall,
        MP_HUD_PROP_KEY_HudMatchStatusLabelColorR,
        MP_HUD_PROP_KEY_HudMatchStatusLabelColorG,
        MP_HUD_PROP_KEY_HudMatchStatusLabelColorB);
    addSubColor(QStringLiteral("Value"),
        MP_HUD_PROP_KEY_HudMatchStatusValueColorOverall,
        MP_HUD_PROP_KEY_HudMatchStatusValueColorR,
        MP_HUD_PROP_KEY_HudMatchStatusValueColorG,
        MP_HUD_PROP_KEY_HudMatchStatusValueColorB);
    addSubColor(QStringLiteral("Slash"),
        MP_HUD_PROP_KEY_HudMatchStatusSepColorOverall,
        MP_HUD_PROP_KEY_HudMatchStatusSepColorR,
        MP_HUD_PROP_KEY_HudMatchStatusSepColorG,
        MP_HUD_PROP_KEY_HudMatchStatusSepColorB);
    addSubColor(QStringLiteral("Goal"),
        MP_HUD_PROP_KEY_HudMatchStatusGoalColorOverall,
        MP_HUD_PROP_KEY_HudMatchStatusGoalColorR,
        MP_HUD_PROP_KEY_HudMatchStatusGoalColorG,
        MP_HUD_PROP_KEY_HudMatchStatusGoalColorB);
    addOutlineGroupSection(QStringLiteral("Score Outline"), MP_OUTLINE_KEYS(HudMatchStatus));
}

void MelonPrimeHudConfigOnScreenEdit::populateRank()
{
    addBuiltins(MP_HUD_PROP_KEY_HudRankShow,
        MP_HUD_PROP_KEY_HudRankColorR, MP_HUD_PROP_KEY_HudRankColorG, MP_HUD_PROP_KEY_HudRankColorB,
        MP_HUD_PROP_KEY_HudRankAnchor);
    addOffsetRows(MP_HUD_PROP_KEY_HudRankX, MP_HUD_PROP_KEY_HudRankY, -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addLineEdit(QStringLiteral("Prefix"), MP_HUD_PROP_KEY_HudRankPrefix);
    addCheckBox(QStringLiteral("Ordinal"), MP_HUD_PROP_KEY_HudRankShowOrdinal);
    addLineEdit(QStringLiteral("Suffix"), MP_HUD_PROP_KEY_HudRankSuffix);
    addAlign3Combo(QStringLiteral("Align"), MP_HUD_PROP_KEY_HudRankAlign);
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_HudRankOpacity);
    addOutlineGroupSection(QStringLiteral("Rank Outline"), MP_OUTLINE_KEYS(HudRank));
}

void MelonPrimeHudConfigOnScreenEdit::populateTimeLeft()
{
    addBuiltins(MP_HUD_PROP_KEY_HudTimeLeftShow,
        MP_HUD_PROP_KEY_HudTimeLeftColorR, MP_HUD_PROP_KEY_HudTimeLeftColorG, MP_HUD_PROP_KEY_HudTimeLeftColorB,
        MP_HUD_PROP_KEY_HudTimeLeftAnchor);
    addOffsetRows(MP_HUD_PROP_KEY_HudTimeLeftX, MP_HUD_PROP_KEY_HudTimeLeftY, -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_HudTimeLeftOpacity);
    addAlign3Combo(QStringLiteral("Align"), MP_HUD_PROP_KEY_HudTimeLeftAlign);
    addOutlineGroupSection(QStringLiteral("Time Left Outline"), MP_OUTLINE_KEYS(HudTimeLeft));
}

void MelonPrimeHudConfigOnScreenEdit::populateTimeLimit()
{
    addBuiltins(MP_HUD_PROP_KEY_HudTimeLimitShow,
        MP_HUD_PROP_KEY_HudTimeLimitColorR, MP_HUD_PROP_KEY_HudTimeLimitColorG, MP_HUD_PROP_KEY_HudTimeLimitColorB,
        MP_HUD_PROP_KEY_HudTimeLimitAnchor);
    addOffsetRows(MP_HUD_PROP_KEY_HudTimeLimitX, MP_HUD_PROP_KEY_HudTimeLimitY, -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_HudTimeLimitOpacity);
    addAlign3Combo(QStringLiteral("Align"), MP_HUD_PROP_KEY_HudTimeLimitAlign);
    addOutlineGroupSection(QStringLiteral("Time Limit Outline"), MP_OUTLINE_KEYS(HudTimeLimit));
}

void MelonPrimeHudConfigOnScreenEdit::populateBombLeft()
{
    addBuiltins(MP_HUD_PROP_KEY_HudBombLeftShow,
        MP_HUD_PROP_KEY_HudBombLeftColorR, MP_HUD_PROP_KEY_HudBombLeftColorG, MP_HUD_PROP_KEY_HudBombLeftColorB,
        MP_HUD_PROP_KEY_HudBombLeftAnchor);
    addOffsetRows(MP_HUD_PROP_KEY_HudBombLeftX, MP_HUD_PROP_KEY_HudBombLeftY, -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addCheckBox(QStringLiteral("Show Number"), MP_HUD_PROP_KEY_HudBombLeftTextShow);
    addAlign3Combo(QStringLiteral("Align"), MP_HUD_PROP_KEY_HudBombLeftAlign);
    addLineEdit(QStringLiteral("Prefix"), MP_HUD_PROP_KEY_HudBombLeftPrefix);
    addLineEdit(QStringLiteral("Suffix"), MP_HUD_PROP_KEY_HudBombLeftSuffix);
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_HudBombLeftOpacity);
    addOutlineGroupSection(QStringLiteral("Bomb Left Outline"), MP_OUTLINE_KEYS(HudBombLeft));
}

void MelonPrimeHudConfigOnScreenEdit::populateBombIcon()
{
    addBuiltins(MP_HUD_PROP_KEY_HudBombLeftIconShow,
        MP_HUD_PROP_KEY_HudBombLeftIconColorR, MP_HUD_PROP_KEY_HudBombLeftIconColorG, MP_HUD_PROP_KEY_HudBombLeftIconColorB,
        MP_HUD_PROP_KEY_HudBombLeftIconPosAnchor);
    addOffsetRows(MP_HUD_PROP_KEY_HudBombLeftIconPosX, MP_HUD_PROP_KEY_HudBombLeftIconPosY, -256, 256,
        QStringLiteral("Pos X"), QStringLiteral("Pos Y"));
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_HudBombIconOpacity);
    addCheckBox(QStringLiteral("Color Overlay"), MP_HUD_PROP_KEY_HudBombLeftIconColorOverlay);
    addComboBox(QStringLiteral("Mode"), MP_HUD_PROP_KEY_HudBombLeftIconMode,
        {QStringLiteral("Relative"), QStringLiteral("Independent")});
    addSpinBox(QStringLiteral("Height"), MP_HUD_PROP_KEY_HudBombIconHeight, 4, 64);
    addOffsetRows(MP_HUD_PROP_KEY_HudBombLeftIconOfsX, MP_HUD_PROP_KEY_HudBombLeftIconOfsY, -128, 128,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addAlign3Combo(QStringLiteral("Align X"), MP_HUD_PROP_KEY_HudBombLeftIconAnchorX);
    addComboBox(QStringLiteral("Align Y"), MP_HUD_PROP_KEY_HudBombLeftIconAnchorY,
        {QStringLiteral("Top"), QStringLiteral("Center"), QStringLiteral("Bottom")});
    addOutlineGroupSection(QStringLiteral("Bomb Icon Outline"), MP_OUTLINE_KEYS(HudBombIcon));
}

void MelonPrimeHudConfigOnScreenEdit::populateForCrosshair()
{
    m_populating = true;
    clearForm();
    m_currentElem = -2; // -2 = crosshair (not a regular element)
    m_title->setText(MelonPrime::UiText::Tr("Crosshair"));

    addColorPicker(QStringLiteral("Color"),
        MP_HUD_PROP_KEY_CrosshairColorR,
        MP_HUD_PROP_KEY_CrosshairColorG,
        MP_HUD_PROP_KEY_CrosshairColorB);
    addSpinBox(QStringLiteral("Scale %"), MP_HUD_PROP_KEY_CrosshairScale, 100, 800);
    addCheckBox(QStringLiteral("Outline"), MP_HUD_PROP_KEY_CrosshairOutline);
    addOpacitySlider(QStringLiteral("Outline Opacity"), MP_HUD_PROP_KEY_CrosshairOutlineOpacity);
    addSpinBox(QStringLiteral("Outline Thick."), MP_HUD_PROP_KEY_CrosshairOutlineThickness, 1, 10);
    addCheckBox(QStringLiteral("Center Dot"), MP_HUD_PROP_KEY_CrosshairCenterDot);
    addOpacitySlider(QStringLiteral("Dot Opacity"), MP_HUD_PROP_KEY_CrosshairDotOpacity);
    addSpinBox(QStringLiteral("Dot Thick."), MP_HUD_PROP_KEY_CrosshairDotThickness, 1, 10);
    addComboBox(QStringLiteral("Dot Shape"), MP_HUD_PROP_KEY_CrosshairDotShape,
        {QStringLiteral("Square"), QStringLiteral("Circle")});
    addCheckBox(QStringLiteral("Custom Dot Color"), MP_HUD_PROP_KEY_CrosshairDotCustomColor);
    addColorPicker(QStringLiteral("Dot Color"),
        MP_HUD_PROP_KEY_CrosshairDotColorR,
        MP_HUD_PROP_KEY_CrosshairDotColorG,
        MP_HUD_PROP_KEY_CrosshairDotColorB);
    addCheckBox(QStringLiteral("T-Style"), MP_HUD_PROP_KEY_CrosshairTStyle);

    addSeparator();
    addSectionHeader(QStringLiteral("Zoom Crosshair"));
    addCheckBox(QStringLiteral("Zoom Stage"), MP_HUD_PROP_KEY_CrosshairZoomStageEnable);
    addSpinBox(QStringLiteral("Zoom Base Scale %"), MP_HUD_PROP_KEY_CrosshairZoomScale, 10, 200);
    addSpinBox(QStringLiteral("Zoom Base Opacity %"), MP_HUD_PROP_KEY_CrosshairZoomOpacity, 0, 100);
    addCheckBox(QStringLiteral("Zoom Scope"), MP_HUD_PROP_KEY_CrosshairZoomScopeEnable);
    addSpinBox(QStringLiteral("Scope Radius"), MP_HUD_PROP_KEY_CrosshairZoomScopeRadius, 4, 1024);
    addSpinBox(QStringLiteral("Scope Gap"), MP_HUD_PROP_KEY_CrosshairZoomScopeGap, 0, 64);
    addSpinBox(QStringLiteral("Scope Thick."), MP_HUD_PROP_KEY_CrosshairZoomScopeThickness, 1, 12);
    addCheckBox(QStringLiteral("Scope Center Dot"), MP_HUD_PROP_KEY_CrosshairZoomScopeCenterDot);
    addComboBox(QStringLiteral("Scope Dot Shape"), MP_HUD_PROP_KEY_CrosshairZoomScopeDotShape,
        {QStringLiteral("Square"), QStringLiteral("Circle")});
    addSpinBox(QStringLiteral("Scope Dot Size"), MP_HUD_PROP_KEY_CrosshairZoomScopeDotSize, 1, 32);
    addSpinBox(QStringLiteral("Scope Dot Opacity %"), MP_HUD_PROP_KEY_CrosshairZoomScopeDotOpacity, 0, 100);
    addCheckBox(QStringLiteral("Custom Scope Dot Color"), MP_HUD_PROP_KEY_CrosshairZoomScopeDotCustomColor);
    addColorPicker(QStringLiteral("Scope Dot Color"),
        MP_HUD_PROP_KEY_CrosshairZoomScopeDotColorR,
        MP_HUD_PROP_KEY_CrosshairZoomScopeDotColorG,
        MP_HUD_PROP_KEY_CrosshairZoomScopeDotColorB);
    addSpinBox(QStringLiteral("Scope Opacity %"), MP_HUD_PROP_KEY_CrosshairZoomScopeOpacity, 0, 100);
    addCheckBox(QStringLiteral("Zoom Transition"), MP_HUD_PROP_KEY_CrosshairZoomTransitionEnable);
    addComboBox(QStringLiteral("Transition Style"), MP_HUD_PROP_KEY_CrosshairZoomTransitionStyle,
        {QStringLiteral("Fade"), QStringLiteral("Staged"), QStringLiteral("Glitch"),
         QStringLiteral("Glitch2"), QStringLiteral("Snap"), QStringLiteral("Digital"),
         QStringLiteral("Pulse Wave"), QStringLiteral("Magic Circle"), QStringLiteral("SF Movie"),
         QStringLiteral("Tactical Lock"), QStringLiteral("Sniper Optics"),
         QStringLiteral("Drone LIDAR"), QStringLiteral("Beam Charge")});
    addSpinBox(QStringLiteral("Transition Speed %"), MP_HUD_PROP_KEY_CrosshairZoomTransitionSpeed, 25, 400);
    addCheckBox(QStringLiteral("Pulse Ring"), MP_HUD_PROP_KEY_CrosshairZoomTransitionPulseEnable);
    addSpinBox(QStringLiteral("Pulse Strength %"), MP_HUD_PROP_KEY_CrosshairZoomTransitionPulseStrength, 0, 100);

    addSeparator();
    // Inner Lines
    addSectionHeader(QStringLiteral("Inner Lines"));
    addCheckBox(QStringLiteral("Show"), MP_HUD_PROP_KEY_CrosshairInnerShow);
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_CrosshairInnerOpacity);
    addSpinBox(QStringLiteral("Length X"), MP_HUD_PROP_KEY_CrosshairInnerLengthX, 0, 64);
    addSpinBox(QStringLiteral("Length Y"), MP_HUD_PROP_KEY_CrosshairInnerLengthY, 0, 64);
    addCheckBox(QStringLiteral("Link XY"), MP_HUD_PROP_KEY_CrosshairInnerLinkXY);
    addSpinBox(QStringLiteral("Thickness"), MP_HUD_PROP_KEY_CrosshairInnerThickness, 1, 10);
    addSpinBox(QStringLiteral("Offset"), MP_HUD_PROP_KEY_CrosshairInnerOffset, 0, 64);

    addSeparator();
    // Outer Lines
    addSectionHeader(QStringLiteral("Outer Lines"));
    addCheckBox(QStringLiteral("Show"), MP_HUD_PROP_KEY_CrosshairOuterShow);
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_CrosshairOuterOpacity);
    addSpinBox(QStringLiteral("Length X"), MP_HUD_PROP_KEY_CrosshairOuterLengthX, 0, 64);
    addSpinBox(QStringLiteral("Length Y"), MP_HUD_PROP_KEY_CrosshairOuterLengthY, 0, 64);
    addCheckBox(QStringLiteral("Link XY"), MP_HUD_PROP_KEY_CrosshairOuterLinkXY);
    addSpinBox(QStringLiteral("Thickness"), MP_HUD_PROP_KEY_CrosshairOuterThickness, 1, 10);
    addSpinBox(QStringLiteral("Offset"), MP_HUD_PROP_KEY_CrosshairOuterOffset, 0, 64);

    m_populating = false;
}

void MelonPrimeHudConfigOnScreenEdit::populateWeaponInventory()
{
    addBuiltins(MP_HUD_PROP_KEY_HudWeaponInventoryShow,
        MP_HUD_PROP_KEY_HudWeaponInventoryColorR,
        MP_HUD_PROP_KEY_HudWeaponInventoryColorG,
        MP_HUD_PROP_KEY_HudWeaponInventoryColorB,
        MP_HUD_PROP_KEY_HudWeaponInventoryAnchor);
    addOffsetRows(MP_HUD_PROP_KEY_HudWeaponInventoryX, MP_HUD_PROP_KEY_HudWeaponInventoryY, -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addComboBox(QStringLiteral("Orientation"), MP_HUD_PROP_KEY_HudWeaponInventoryOrientation,
        {QStringLiteral("Horizontal"), QStringLiteral("Vertical")});
    addAlign3Combo(QStringLiteral("Align"), MP_HUD_PROP_KEY_HudWeaponInventoryAlign);
    addSpinBox(QStringLiteral("Icon Height"), MP_HUD_PROP_KEY_HudWeaponInventoryIconHeight, 4, 48);
    addSpinBox(QStringLiteral("Spacing"), MP_HUD_PROP_KEY_HudWeaponInventorySpacing, 0, 32);
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_HudWeaponInventoryOpacity);
    addOpacitySlider(QStringLiteral("Not Owned Opacity"), MP_HUD_PROP_KEY_HudWeaponInventoryNotOwnedOpacity);
    addOutlineGroupSection(QStringLiteral("Weapon Inventory Outline"), MP_OUTLINE_KEYS(HudWeaponInventory));
    addOutlineGroupSection(QStringLiteral("Weapon Inventory Icon Outline"), MP_OUTLINE_KEYS(HudWeaponInventoryIcon));
    addSeparator();
    addSectionHeader(QStringLiteral("Weapon Inventory Highlight"));
    addCheckBox(QStringLiteral("Highlight Current Weapon"), MP_HUD_PROP_KEY_HudWeaponInventoryHighlightEnable);
    addColorPicker(QStringLiteral("Highlight Color"),
        MP_HUD_PROP_KEY_HudWeaponInventoryHighlightColorR,
        MP_HUD_PROP_KEY_HudWeaponInventoryHighlightColorG,
        MP_HUD_PROP_KEY_HudWeaponInventoryHighlightColorB);
    addOpacitySlider(QStringLiteral("Highlight Opacity"), MP_HUD_PROP_KEY_HudWeaponInventoryHighlightOpacity);
    addDoubleSpinBox(QStringLiteral("Highlight Thickness"), MP_HUD_PROP_KEY_HudWeaponInventoryHighlightThickness, 0.1, 8.0, 0.25);
    addSpinBox(QStringLiteral("Highlight Padding"), MP_HUD_PROP_KEY_HudWeaponInventoryHighlightPadding, 0, 16);
    addSpinBox(QStringLiteral("Highlight Corner Radius"), MP_HUD_PROP_KEY_HudWeaponInventoryHighlightCornerRadius, 0, 16);
    addSpinBox(QStringLiteral("Highlight Offset Left"),   MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetLeft,   -16, 32);
    addSpinBox(QStringLiteral("Highlight Offset Right"),  MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetRight,  -16, 32);
    addSpinBox(QStringLiteral("Highlight Offset Top"),    MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetTop,    -16, 32);
    addSpinBox(QStringLiteral("Highlight Offset Bottom"), MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetBottom, -16, 32);
}

void MelonPrimeHudConfigOnScreenEdit::populateRadar()
{
    addBuiltins(MP_HUD_PROP_KEY_BtmOverlayEnable,
        nullptr, nullptr, nullptr,
        MP_HUD_PROP_KEY_BtmOverlayAnchor);
    addOffsetRows(MP_HUD_PROP_KEY_BtmOverlayDstX, MP_HUD_PROP_KEY_BtmOverlayDstY, -256, 256,
        QStringLiteral("Dst X"), QStringLiteral("Dst Y"));
    addSpinBox(QStringLiteral("Dst Size"), MP_HUD_PROP_KEY_BtmOverlayDstSize, 16, 128);
    addOpacitySlider(QStringLiteral("Opacity"), MP_HUD_PROP_KEY_BtmOverlayOpacity);
    addSpinBox(QStringLiteral("Src Radius"), MP_HUD_PROP_KEY_BtmOverlaySrcRadius, 10, 120);
    addColorPicker(QStringLiteral("Radar Color"),
        MP_HUD_PROP_KEY_BtmOverlayRadarColorR,
        MP_HUD_PROP_KEY_BtmOverlayRadarColorG,
        MP_HUD_PROP_KEY_BtmOverlayRadarColorB);
    addCheckBox(QStringLiteral("Use Hunter Color"), MP_HUD_PROP_KEY_BtmOverlayRadarColorUseHunter);
    addOutlineGroupSection(QStringLiteral("Radar Outline"), MP_OUTLINE_KEYS(BtmOverlay));
    addOutlineGroupSection(QStringLiteral("Frame Outline"), MP_OUTLINE_KEYS(BtmOverlayFrame));
}

#endif // MELONPRIME_CUSTOM_HUD
