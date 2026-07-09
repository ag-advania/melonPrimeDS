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

#include "MelonPrimeHudEditorSidePanelRows.inc"

// ─── Row-table dispatcher (V7 Phase 2) ──────────────────────────────────────

void MelonPrimeHudConfigOnScreenEdit::populateFromRowTable(
    const MelonPrime::HudEditorSidePanel::Row* rows, int count)
{
    using MelonPrime::HudEditorSidePanel::RowKind;
    for (int i = 0; i < count; ++i) {
        const auto& r = rows[i];
        switch (r.kind) {
        case RowKind::Bool:
            addCheckBox(r.label, r.key1);
            break;
        case RowKind::Combo:
            addComboBox(r.label, r.key1, r.items);
            break;
        case RowKind::Spin:
            addSpinBox(r.label, r.key1, r.iMin, r.iMax);
            break;
        case RowKind::DoubleSpin:
            addDoubleSpinBox(r.label, r.key1, r.dMin, r.dMax, r.dStep);
            break;
        case RowKind::Opacity:
            addOpacitySlider(r.label, r.key1);
            break;
        case RowKind::LineEdit:
            addLineEdit(r.label, r.key1);
            break;
        case RowKind::Color:
            addColorPicker(r.label, r.key1, r.key2, r.key3);
            break;
        case RowKind::SubColor:
            addSubColor(r.label, r.key1, r.key2, r.key3, r.key4);
            break;
        case RowKind::ColorOverlay:
            addColorOverlayRow(r.label, r.key1, r.key2, r.key3, r.key4);
            break;
        case RowKind::Separator:
            addSeparator();
            break;
        case RowKind::SectionHeader:
            addSectionHeader(r.label);
            break;
        case RowKind::Builtins:
            addBuiltins(r.key1, r.key2, r.key3, r.key4, r.key5);
            break;
        case RowKind::OffsetRows:
            addOffsetRows(r.key1, r.key2, r.iMin, r.iMax, r.label, r.label2);
            break;
        case RowKind::Align3:
            addAlign3Combo(r.label, r.key1);
            break;
        case RowKind::OutlineGroup:
            addOutlineGroupSection(r.label, r.key1, r.key2, r.key3, r.key4, r.key5, r.key6);
            break;
        case RowKind::GaugePosition:
            addGaugePositionRows(r.key1, r.key2, r.key3, r.key4, r.key5, r.key6, r.key7, r.key8, r.key9);
            break;
        }
    }
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
    populateFromRowTable(kRowsHP, MP_SIDE_ROW_COUNT(kRowsHP));
}

void MelonPrimeHudConfigOnScreenEdit::populateHPGauge()
{
    populateFromRowTable(kRowsHPGauge, MP_SIDE_ROW_COUNT(kRowsHPGauge));
}

void MelonPrimeHudConfigOnScreenEdit::populateWeaponAmmo()
{
    populateFromRowTable(kRowsWeaponAmmo, MP_SIDE_ROW_COUNT(kRowsWeaponAmmo));
}

void MelonPrimeHudConfigOnScreenEdit::populateWpnIcon()
{
    populateFromRowTable(kRowsWpnIcon, MP_SIDE_ROW_COUNT(kRowsWpnIcon));
    // Per-weapon icon tint (enable + color per weapon). Bespoke: driven by a
    // fixed 9-entry array walked in a loop, kept out of the row table per
    // MelonPrimeHudEditorSidePanelRows.inc's header comment.
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
    populateFromRowTable(kRowsAmmoGauge, MP_SIDE_ROW_COUNT(kRowsAmmoGauge));
}

void MelonPrimeHudConfigOnScreenEdit::populateMatchStatus()
{
    populateFromRowTable(kRowsMatchStatus, MP_SIDE_ROW_COUNT(kRowsMatchStatus));
}

void MelonPrimeHudConfigOnScreenEdit::populateRank()
{
    populateFromRowTable(kRowsRank, MP_SIDE_ROW_COUNT(kRowsRank));
}

void MelonPrimeHudConfigOnScreenEdit::populateTimeLeft()
{
    populateFromRowTable(kRowsTimeLeft, MP_SIDE_ROW_COUNT(kRowsTimeLeft));
}

void MelonPrimeHudConfigOnScreenEdit::populateTimeLimit()
{
    populateFromRowTable(kRowsTimeLimit, MP_SIDE_ROW_COUNT(kRowsTimeLimit));
}

void MelonPrimeHudConfigOnScreenEdit::populateBombLeft()
{
    populateFromRowTable(kRowsBombLeft, MP_SIDE_ROW_COUNT(kRowsBombLeft));
}

void MelonPrimeHudConfigOnScreenEdit::populateBombIcon()
{
    populateFromRowTable(kRowsBombIcon, MP_SIDE_ROW_COUNT(kRowsBombIcon));
}

void MelonPrimeHudConfigOnScreenEdit::populateForCrosshair()
{
    m_populating = true;
    clearForm();
    m_currentElem = -2; // -2 = crosshair (not a regular element)
    m_title->setText(MelonPrime::UiText::Tr("Crosshair"));

    populateFromRowTable(kRowsCrosshair, MP_SIDE_ROW_COUNT(kRowsCrosshair));

    m_populating = false;
}

void MelonPrimeHudConfigOnScreenEdit::populateWeaponInventory()
{
    populateFromRowTable(kRowsWeaponInventory, MP_SIDE_ROW_COUNT(kRowsWeaponInventory));
}

void MelonPrimeHudConfigOnScreenEdit::populateRadar()
{
    populateFromRowTable(kRowsRadar, MP_SIDE_ROW_COUNT(kRowsRadar));
}

#endif // MELONPRIME_CUSTOM_HUD
