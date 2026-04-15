#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeHudConfigOnScreenEdit.h"
#include "MelonPrimeHudRender.h"
#include "EmuInstance.h"
#include <QColorDialog>
#include <QHBoxLayout>
#include <QFrame>
#include <QRadioButton>

// ─── Construction ───────────────────────────────────────────────────────────

MelonPrimeHudConfigOnScreenEdit::MelonPrimeHudConfigOnScreenEdit(QWidget* parent, EmuInstance* emu)
    : QWidget(parent), m_emu(emu)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "MelonPrimeHudConfigOnScreenEdit { background: rgba(24,24,32,210); border: 1px solid #555; border-radius: 4px; }"
        "QLabel { color: #000; font-size: 9px; }"
        "QRadioButton { color: #000; font-size: 9px; }"
        "QComboBox { font-size: 9px; color: #000; }"
        "QSpinBox { font-size: 9px; color: #000; }"
        "QDoubleSpinBox { font-size: 9px; color: #000; }"
        "QLineEdit { font-size: 9px; background: #fff; color: #000; border: 1px solid #555; }"
        "QPushButton { font-size: 9px; }"
    );

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(6, 4, 6, 4);
    outerLayout->setSpacing(2);

    m_title = new QLabel(this);
    m_title->setStyleSheet("color: #fff; font-size: 10px; font-weight: bold;");
    m_title->setAlignment(Qt::AlignCenter);
    outerLayout->addWidget(m_title);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setStyleSheet("QScrollArea { background: transparent; }");
    outerLayout->addWidget(m_scroll);

    m_inner = new QWidget();
    m_inner->setStyleSheet("background: transparent;");
    m_form = new QFormLayout(m_inner);
    m_form->setContentsMargins(0, 0, 0, 0);
    m_form->setSpacing(3);
    m_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_scroll->setWidget(m_inner);

    setFixedWidth(210);
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
    auto* container = new QWidget(this);
    auto* hlay = new QHBoxLayout(container);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(8);

    auto* on = new QRadioButton(QStringLiteral("ON"), container);
    auto* off = new QRadioButton(QStringLiteral("OFF"), container);
    on->setCursor(Qt::PointingHandCursor);
    off->setCursor(Qt::PointingHandCursor);

    const bool enabled = cfg().GetBool(key);
    on->setChecked(enabled);
    off->setChecked(!enabled);

    std::string k(key);
    connect(on, &QRadioButton::toggled, this, [this, k](bool checked) {
        if (m_populating || !checked) return;
        cfg().SetBool(k, true);
        MelonPrime::CustomHud_InvalidateConfigCache();
    });
    connect(off, &QRadioButton::toggled, this, [this, k](bool checked) {
        if (m_populating || !checked) return;
        cfg().SetBool(k, false);
        MelonPrime::CustomHud_InvalidateConfigCache();
    });

    hlay->addWidget(on, 0);
    hlay->addWidget(off, 0);
    hlay->addStretch(1);

    m_form->addRow(label, container);
    m_rows.append(container);
    return container;
}

// ─── Factory: ComboBox ──────────────────────────────────────────────────────

QComboBox* MelonPrimeHudConfigOnScreenEdit::addComboBox(const QString& label, const char* key, const QStringList& items)
{
    auto* cb = new QComboBox(this);
    cb->addItems(items);
    cb->setCurrentIndex(cfg().GetInt(key));
    std::string k(key);
    connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, k](int idx) {
        if (m_populating) return;
        cfg().SetInt(k, idx);
        MelonPrime::CustomHud_InvalidateConfigCache();
    });
    m_form->addRow(label, cb);
    m_rows.append(cb);
    return cb;
}

// ─── Factory: SpinBox ───────────────────────────────────────────────────────

QSpinBox* MelonPrimeHudConfigOnScreenEdit::addSpinBox(const QString& label, const char* key, int min, int max)
{
    auto* sb = new QSpinBox(this);
    sb->setRange(min, max);
    sb->setValue(cfg().GetInt(key));
    std::string k(key);
    connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, k](int v) {
        if (m_populating) return;
        cfg().SetInt(k, v);
        MelonPrime::CustomHud_InvalidateConfigCache();
    });
    m_form->addRow(label, sb);
    m_rows.append(sb);
    return sb;
}

// ─── Factory: DoubleSpinBox ─────────────────────────────────────────────────

QDoubleSpinBox* MelonPrimeHudConfigOnScreenEdit::addDoubleSpinBox(const QString& label, const char* key, double min, double max, double step)
{
    auto* sb = new QDoubleSpinBox(this);
    sb->setRange(min, max);
    sb->setSingleStep(step);
    sb->setDecimals(2);
    sb->setValue(cfg().GetDouble(key));
    std::string k(key);
    connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, k](double v) {
        if (m_populating) return;
        cfg().SetDouble(k, v);
        MelonPrime::CustomHud_InvalidateConfigCache();
    });
    m_form->addRow(label, sb);
    m_rows.append(sb);
    return sb;
}

// ─── Factory: OpacitySlider ─────────────────────────────────────────────────

QSlider* MelonPrimeHudConfigOnScreenEdit::addOpacitySlider(const QString& label, const char* key)
{
    auto* container = new QWidget(this);
    auto* hlay = new QHBoxLayout(container);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(4);

    auto* slider = new QSlider(Qt::Horizontal, container);
    slider->setRange(0, 100);
    int initVal = qRound(cfg().GetDouble(key) * 100.0);
    slider->setValue(initVal);

    auto* lbl = new QLabel(QString::number(initVal) + QStringLiteral("%"), container);
    lbl->setFixedWidth(32);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    hlay->addWidget(slider, 1);
    hlay->addWidget(lbl, 0);

    std::string k(key);
    connect(slider, &QSlider::valueChanged, this, [this, k, lbl](int v) {
        lbl->setText(QString::number(v) + QStringLiteral("%"));
        if (m_populating) return;
        cfg().SetDouble(k, v / 100.0);
        MelonPrime::CustomHud_InvalidateConfigCache();
    });

    m_form->addRow(label, container);
    m_rows.append(container);
    return slider;
}

// ─── Factory: LineEdit ──────────────────────────────────────────────────────

QLineEdit* MelonPrimeHudConfigOnScreenEdit::addLineEdit(const QString& label, const char* key)
{
    auto* le = new QLineEdit(this);
    le->setText(QString::fromStdString(cfg().GetString(key)));
    std::string k(key);
    connect(le, &QLineEdit::textChanged, this, [this, k](const QString& text) {
        if (m_populating) return;
        cfg().SetString(k, text.toStdString());
        MelonPrime::CustomHud_InvalidateConfigCache();
    });
    m_form->addRow(label, le);
    m_rows.append(le);
    return le;
}

// ─── Factory: Color Picker ──────────────────────────────────────────────────

static void updateColorButton(QPushButton* btn, int r, int g, int b)
{
    btn->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888; min-width: 30px; min-height: 16px;")
        .arg(QColor(r, g, b).name()));
    btn->setText(QColor(r, g, b).name());
}

QPushButton* MelonPrimeHudConfigOnScreenEdit::addColorPicker(const QString& label, const char* keyR, const char* keyG, const char* keyB)
{
    auto* btn = new QPushButton(this);
    int r = cfg().GetInt(keyR), g = cfg().GetInt(keyG), b = cfg().GetInt(keyB);
    updateColorButton(btn, r, g, b);
    std::string kR(keyR), kG(keyG), kB(keyB);
    connect(btn, &QPushButton::clicked, this, [this, btn, kR, kG, kB]() {
        QColor cur(cfg().GetInt(kR), cfg().GetInt(kG), cfg().GetInt(kB));
        QColor picked = QColorDialog::getColor(cur, this, QStringLiteral("Pick Color"));
        if (picked.isValid()) {
            cfg().SetInt(kR, picked.red());
            cfg().SetInt(kG, picked.green());
            cfg().SetInt(kB, picked.blue());
            updateColorButton(btn, picked.red(), picked.green(), picked.blue());
            MelonPrime::CustomHud_InvalidateConfigCache();
        }
    });
    m_form->addRow(label, btn);
    m_rows.append(btn);
    return btn;
}

// ─── Factory: Sub-Color (with "Overall" toggle) ─────────────────────────────

void MelonPrimeHudConfigOnScreenEdit::addSubColor(const QString& label, const char* overallKey,
    const char* keyR, const char* keyG, const char* keyB)
{
    auto* container = new QWidget(this);
    auto* hlay = new QHBoxLayout(container);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(2);

    auto* combo = new QComboBox(container);
    combo->addItem(QStringLiteral("Overall"));
    combo->addItem(QStringLiteral("Custom"));
    bool isOverall = cfg().GetBool(overallKey);
    combo->setCurrentIndex(isOverall ? 0 : 1);

    auto* btn = new QPushButton(container);
    int r = cfg().GetInt(keyR), g = cfg().GetInt(keyG), b = cfg().GetInt(keyB);
    updateColorButton(btn, r, g, b);
    btn->setEnabled(!isOverall);

    hlay->addWidget(combo, 1);
    hlay->addWidget(btn, 0);

    std::string kOver(overallKey), kR(keyR), kG(keyG), kB(keyB);
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, btn, kOver](int idx) {
        if (m_populating) return;
        cfg().SetBool(kOver, idx == 0);
        btn->setEnabled(idx != 0);
        MelonPrime::CustomHud_InvalidateConfigCache();
    });
    connect(btn, &QPushButton::clicked, this, [this, btn, kR, kG, kB]() {
        QColor cur(cfg().GetInt(kR), cfg().GetInt(kG), cfg().GetInt(kB));
        QColor picked = QColorDialog::getColor(cur, this, QStringLiteral("Pick Color"));
        if (picked.isValid()) {
            cfg().SetInt(kR, picked.red());
            cfg().SetInt(kG, picked.green());
            cfg().SetInt(kB, picked.blue());
            updateColorButton(btn, picked.red(), picked.green(), picked.blue());
            MelonPrime::CustomHud_InvalidateConfigCache();
        }
    });

    m_form->addRow(label, container);
    m_rows.append(container);
}

void MelonPrimeHudConfigOnScreenEdit::addColorOverlayRow(
    const QString& label, const char* enableKey,
    const char* keyR, const char* keyG, const char* keyB)
{
    auto* container = new QWidget(this);
    auto* hlay = new QHBoxLayout(container);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(4);

    auto* on = new QRadioButton(QStringLiteral("ON"), container);
    auto* off = new QRadioButton(QStringLiteral("OFF"), container);
    on->setCursor(Qt::PointingHandCursor);
    off->setCursor(Qt::PointingHandCursor);

    const bool enabled = cfg().GetBool(enableKey);
    on->setChecked(enabled);
    off->setChecked(!enabled);

    auto* btn = new QPushButton(container);
    int r = cfg().GetInt(keyR), g = cfg().GetInt(keyG), b = cfg().GetInt(keyB);
    updateColorButton(btn, r, g, b);
    btn->setEnabled(enabled);

    hlay->addWidget(on, 0);
    hlay->addWidget(off, 0);
    hlay->addWidget(btn, 1);

    std::string kE(enableKey), kR(keyR), kG(keyG), kB(keyB);
    connect(on, &QRadioButton::toggled, this, [this, btn, kE](bool checked) {
        if (m_populating || !checked) return;
        cfg().SetBool(kE, true);
        btn->setEnabled(true);
        MelonPrime::CustomHud_InvalidateConfigCache();
    });
    connect(off, &QRadioButton::toggled, this, [this, btn, kE](bool checked) {
        if (m_populating || !checked) return;
        cfg().SetBool(kE, false);
        btn->setEnabled(false);
        MelonPrime::CustomHud_InvalidateConfigCache();
    });
    connect(btn, &QPushButton::clicked, this, [this, btn, kR, kG, kB]() {
        QColor cur(cfg().GetInt(kR), cfg().GetInt(kG), cfg().GetInt(kB));
        QColor picked = QColorDialog::getColor(cur, this, QStringLiteral("Pick Color"));
        if (picked.isValid()) {
            cfg().SetInt(kR, picked.red());
            cfg().SetInt(kG, picked.green());
            cfg().SetInt(kB, picked.blue());
            updateColorButton(btn, picked.red(), picked.green(), picked.blue());
            MelonPrime::CustomHud_InvalidateConfigCache();
        }
    });

    m_form->addRow(label, container);
    m_rows.append(container);
}

// ─── Separator ──────────────────────────────────────────────────────────────

void MelonPrimeHudConfigOnScreenEdit::addSeparator()
{
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #555;");
    m_form->addRow(line);
    m_rows.append(line);
}

void MelonPrimeHudConfigOnScreenEdit::addOutlineGroup(const char* prefix)
{
    // prefix e.g. "HudHp" → keys "Metroid.Visual.HudHpOutline", "...OutlineColorR" etc.
    char kE[80], kR[80], kG[80], kB[80], kO[80], kT[80];
    std::snprintf(kE, sizeof(kE), "Metroid.Visual.%sOutline", prefix);
    std::snprintf(kR, sizeof(kR), "Metroid.Visual.%sOutlineColorR", prefix);
    std::snprintf(kG, sizeof(kG), "Metroid.Visual.%sOutlineColorG", prefix);
    std::snprintf(kB, sizeof(kB), "Metroid.Visual.%sOutlineColorB", prefix);
    std::snprintf(kO, sizeof(kO), "Metroid.Visual.%sOutlineOpacity", prefix);
    std::snprintf(kT, sizeof(kT), "Metroid.Visual.%sOutlineThickness", prefix);
    addSeparator();
    addCheckBox(QStringLiteral("Outline"), kE);
    addColorPicker(QStringLiteral("Outline Color"), kR, kG, kB);
    addOpacitySlider(QStringLiteral("Outline Opacity"), kO);
    addSpinBox(QStringLiteral("Outline Thick."), kT, 1, 10);
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
    auto* hdr = new QLabel(label, this);
    hdr->setStyleSheet("color: #fff; font-weight: bold; font-size: 9px;");
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

    m_title->setText(kElementNames[idx]);

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
        "Metroid.Visual.HudHpTextColorR", "Metroid.Visual.HudHpTextColorG", "Metroid.Visual.HudHpTextColorB",
        "Metroid.Visual.HudHpAnchor");
    addOffsetRows("Metroid.Visual.HudHpX", "Metroid.Visual.HudHpY", -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addLineEdit(QStringLiteral("Prefix"), "Metroid.Visual.HudHpPrefix");
    addAlign3Combo(QStringLiteral("Align"), "Metroid.Visual.HudHpAlign");
    addCheckBox(QStringLiteral("Auto Color"), "Metroid.Visual.HudHpTextAutoColor");
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.HudHpOpacity");
    addOutlineGroup("HudHp");
}

void MelonPrimeHudConfigOnScreenEdit::populateHPGauge()
{
    addBuiltins("Metroid.Visual.HudHpGauge",
        "Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB",
        "Metroid.Visual.HudHpGaugePosAnchor");
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.HudHpGaugeOpacity");
    addComboBox(QStringLiteral("Orientation"), "Metroid.Visual.HudHpGaugeOrientation",
        {QStringLiteral("Horizontal"), QStringLiteral("Vertical")});
    addComboBox(QStringLiteral("Align"), "Metroid.Visual.HudHpGaugeAlign",
        {QStringLiteral("Start"), QStringLiteral("Center"), QStringLiteral("End")});
    addSpinBox(QStringLiteral("Length"), "Metroid.Visual.HudHpGaugeLength", 1, 192);
    addSpinBox(QStringLiteral("Width"), "Metroid.Visual.HudHpGaugeWidth", 1, 20);
    addCheckBox(QStringLiteral("Auto Color"), "Metroid.Visual.HudHpGaugeAutoColor");
    addGaugePositionRows("Metroid.Visual.HudHpGaugePosMode",
        "Metroid.Visual.HudHpGaugeAnchor", "Metroid.Visual.HudHpGaugeOffsetX", "Metroid.Visual.HudHpGaugeOffsetY",
        "Metroid.Visual.HudHpGaugePosX", "Metroid.Visual.HudHpGaugePosY",
        "Metroid.Visual.HudHpTextAnchor", "Metroid.Visual.HudHpTextOffsetX", "Metroid.Visual.HudHpTextOffsetY");
    addOutlineGroup("HudHpGauge");
}

void MelonPrimeHudConfigOnScreenEdit::populateWeaponAmmo()
{
    addBuiltins(nullptr,
        "Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB",
        "Metroid.Visual.HudWeaponAnchor");
    addOffsetRows("Metroid.Visual.HudWeaponX", "Metroid.Visual.HudWeaponY", -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addLineEdit(QStringLiteral("Prefix"), "Metroid.Visual.HudAmmoPrefix");
    addAlign3Combo(QStringLiteral("Align"), "Metroid.Visual.HudAmmoAlign");
    addComboBox(QStringLiteral("Layout"), "Metroid.Visual.HudWeaponLayout",
        {QStringLiteral("Standard"), QStringLiteral("Alternative")});
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.HudWeaponOpacity");
    addOutlineGroup("HudWeapon");
}

void MelonPrimeHudConfigOnScreenEdit::populateWpnIcon()
{
    addBuiltins("Metroid.Visual.HudWeaponIconShow",
        nullptr, nullptr, nullptr,
        "Metroid.Visual.HudWeaponIconPosAnchor");
    addOffsetRows("Metroid.Visual.HudWeaponIconPosX", "Metroid.Visual.HudWeaponIconPosY", -256, 256,
        QStringLiteral("Pos X"), QStringLiteral("Pos Y"));
    addComboBox(QStringLiteral("Mode"), "Metroid.Visual.HudWeaponIconMode",
        {QStringLiteral("Relative"), QStringLiteral("Independent")});
    addSpinBox(QStringLiteral("Height"), "Metroid.Visual.HudWeaponIconHeight", 4, 64);
    addOffsetRows("Metroid.Visual.HudWeaponIconOffsetX", "Metroid.Visual.HudWeaponIconOffsetY", -128, 128,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addAlign3Combo(QStringLiteral("Align X"), "Metroid.Visual.HudWeaponIconAnchorX");
    addComboBox(QStringLiteral("Align Y"), "Metroid.Visual.HudWeaponIconAnchorY",
        {QStringLiteral("Top"), QStringLiteral("Center"), QStringLiteral("Bottom")});
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.HudWpnIconOpacity");
    addOutlineGroup("HudWeaponIcon");
    addSeparator();
    // Per-weapon icon tint (enable + color per weapon)
    struct { const char* label; const char* wpn; } kWeapons[9] = {
        {"Power Beam",    "PowerBeam"},
        {"Volt Driver",   "VoltDriver"},
        {"Missile",       "Missile"},
        {"Battle Hammer", "BattleHammer"},
        {"Imperialist",   "Imperialist"},
        {"Judicator",     "Judicator"},
        {"Magmaul",       "Magmaul"},
        {"Shock Coil",    "ShockCoil"},
        {"Omega Cannon",  "OmegaCannon"},
    };
    char kE[80], kR[80], kG[80], kB[80];
    for (auto& w : kWeapons) {
        std::snprintf(kE, sizeof(kE), "Metroid.Visual.HudWeaponIconColorOverlay%s", w.wpn);
        std::snprintf(kR, sizeof(kR), "Metroid.Visual.HudWeaponIconOverlayColorR%s", w.wpn);
        std::snprintf(kG, sizeof(kG), "Metroid.Visual.HudWeaponIconOverlayColorG%s", w.wpn);
        std::snprintf(kB, sizeof(kB), "Metroid.Visual.HudWeaponIconOverlayColorB%s", w.wpn);
        addColorOverlayRow(QString::fromUtf8(w.label), kE, kR, kG, kB);
    }
}

void MelonPrimeHudConfigOnScreenEdit::populateAmmoGauge()
{
    addBuiltins("Metroid.Visual.HudAmmoGauge",
        "Metroid.Visual.HudAmmoGaugeColorR", "Metroid.Visual.HudAmmoGaugeColorG", "Metroid.Visual.HudAmmoGaugeColorB",
        "Metroid.Visual.HudAmmoGaugePosAnchor");
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.HudAmmoGaugeOpacity");
    addComboBox(QStringLiteral("Orientation"), "Metroid.Visual.HudAmmoGaugeOrientation",
        {QStringLiteral("Horizontal"), QStringLiteral("Vertical")});
    addComboBox(QStringLiteral("Align"), "Metroid.Visual.HudAmmoGaugeAlign",
        {QStringLiteral("Start"), QStringLiteral("Center"), QStringLiteral("End")});
    addSpinBox(QStringLiteral("Length"), "Metroid.Visual.HudAmmoGaugeLength", 1, 192);
    addSpinBox(QStringLiteral("Width"), "Metroid.Visual.HudAmmoGaugeWidth", 1, 20);
    addGaugePositionRows("Metroid.Visual.HudAmmoGaugePosMode",
        "Metroid.Visual.HudAmmoGaugeAnchor", "Metroid.Visual.HudAmmoGaugeOffsetX", "Metroid.Visual.HudAmmoGaugeOffsetY",
        "Metroid.Visual.HudAmmoGaugePosX", "Metroid.Visual.HudAmmoGaugePosY",
        "Metroid.Visual.HudAmmoTextAnchor", "Metroid.Visual.HudAmmoTextOffsetX", "Metroid.Visual.HudAmmoTextOffsetY");
    addOutlineGroup("HudAmmoGauge");
}

void MelonPrimeHudConfigOnScreenEdit::populateMatchStatus()
{
    addBuiltins("Metroid.Visual.HudMatchStatusShow",
        "Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB",
        "Metroid.Visual.HudMatchStatusAnchor");
    addOffsetRows("Metroid.Visual.HudMatchStatusX", "Metroid.Visual.HudMatchStatusY", -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.HudMatchStatusOpacity");
    addComboBox(QStringLiteral("Label Pos"), "Metroid.Visual.HudMatchStatusLabelPos",
        {QStringLiteral("Above"), QStringLiteral("Below"), QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Center")});
    addOffsetRows("Metroid.Visual.HudMatchStatusLabelOfsX", "Metroid.Visual.HudMatchStatusLabelOfsY", -128, 128,
        QStringLiteral("Label Ofs X"), QStringLiteral("Label Ofs Y"));
    addSeparator();
    addLineEdit(QStringLiteral("Battle"), "Metroid.Visual.HudMatchStatusLabelPoints");
    addLineEdit(QStringLiteral("Bounty"), "Metroid.Visual.HudMatchStatusLabelOctoliths");
    addLineEdit(QStringLiteral("Survival"), "Metroid.Visual.HudMatchStatusLabelLives");
    addLineEdit(QStringLiteral("Defender"), "Metroid.Visual.HudMatchStatusLabelRingTime");
    addLineEdit(QStringLiteral("Prime"), "Metroid.Visual.HudMatchStatusLabelPrimeTime");
    addSeparator();
    addSubColor(QStringLiteral("Label"),
        "Metroid.Visual.HudMatchStatusLabelColorOverall",
        "Metroid.Visual.HudMatchStatusLabelColorR",
        "Metroid.Visual.HudMatchStatusLabelColorG",
        "Metroid.Visual.HudMatchStatusLabelColorB");
    addSubColor(QStringLiteral("Value"),
        "Metroid.Visual.HudMatchStatusValueColorOverall",
        "Metroid.Visual.HudMatchStatusValueColorR",
        "Metroid.Visual.HudMatchStatusValueColorG",
        "Metroid.Visual.HudMatchStatusValueColorB");
    addSubColor(QStringLiteral("Slash"),
        "Metroid.Visual.HudMatchStatusSepColorOverall",
        "Metroid.Visual.HudMatchStatusSepColorR",
        "Metroid.Visual.HudMatchStatusSepColorG",
        "Metroid.Visual.HudMatchStatusSepColorB");
    addSubColor(QStringLiteral("Goal"),
        "Metroid.Visual.HudMatchStatusGoalColorOverall",
        "Metroid.Visual.HudMatchStatusGoalColorR",
        "Metroid.Visual.HudMatchStatusGoalColorG",
        "Metroid.Visual.HudMatchStatusGoalColorB");
    addOutlineGroup("HudMatchStatus");
}

void MelonPrimeHudConfigOnScreenEdit::populateRank()
{
    addBuiltins("Metroid.Visual.HudRankShow",
        "Metroid.Visual.HudRankColorR", "Metroid.Visual.HudRankColorG", "Metroid.Visual.HudRankColorB",
        "Metroid.Visual.HudRankAnchor");
    addOffsetRows("Metroid.Visual.HudRankX", "Metroid.Visual.HudRankY", -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addLineEdit(QStringLiteral("Prefix"), "Metroid.Visual.HudRankPrefix");
    addCheckBox(QStringLiteral("Ordinal"), "Metroid.Visual.HudRankShowOrdinal");
    addLineEdit(QStringLiteral("Suffix"), "Metroid.Visual.HudRankSuffix");
    addAlign3Combo(QStringLiteral("Align"), "Metroid.Visual.HudRankAlign");
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.HudRankOpacity");
    addOutlineGroup("HudRank");
}

void MelonPrimeHudConfigOnScreenEdit::populateTimeLeft()
{
    addBuiltins("Metroid.Visual.HudTimeLeftShow",
        "Metroid.Visual.HudTimeLeftColorR", "Metroid.Visual.HudTimeLeftColorG", "Metroid.Visual.HudTimeLeftColorB",
        "Metroid.Visual.HudTimeLeftAnchor");
    addOffsetRows("Metroid.Visual.HudTimeLeftX", "Metroid.Visual.HudTimeLeftY", -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.HudTimeLeftOpacity");
    addAlign3Combo(QStringLiteral("Align"), "Metroid.Visual.HudTimeLeftAlign");
    addOutlineGroup("HudTimeLeft");
}

void MelonPrimeHudConfigOnScreenEdit::populateTimeLimit()
{
    addBuiltins("Metroid.Visual.HudTimeLimitShow",
        "Metroid.Visual.HudTimeLimitColorR", "Metroid.Visual.HudTimeLimitColorG", "Metroid.Visual.HudTimeLimitColorB",
        "Metroid.Visual.HudTimeLimitAnchor");
    addOffsetRows("Metroid.Visual.HudTimeLimitX", "Metroid.Visual.HudTimeLimitY", -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.HudTimeLimitOpacity");
    addAlign3Combo(QStringLiteral("Align"), "Metroid.Visual.HudTimeLimitAlign");
    addOutlineGroup("HudTimeLimit");
}

void MelonPrimeHudConfigOnScreenEdit::populateBombLeft()
{
    addBuiltins("Metroid.Visual.HudBombLeftShow",
        "Metroid.Visual.HudBombLeftColorR", "Metroid.Visual.HudBombLeftColorG", "Metroid.Visual.HudBombLeftColorB",
        "Metroid.Visual.HudBombLeftAnchor");
    addOffsetRows("Metroid.Visual.HudBombLeftX", "Metroid.Visual.HudBombLeftY", -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addCheckBox(QStringLiteral("Show Number"), "Metroid.Visual.HudBombLeftTextShow");
    addAlign3Combo(QStringLiteral("Align"), "Metroid.Visual.HudBombLeftAlign");
    addLineEdit(QStringLiteral("Prefix"), "Metroid.Visual.HudBombLeftPrefix");
    addLineEdit(QStringLiteral("Suffix"), "Metroid.Visual.HudBombLeftSuffix");
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.HudBombLeftOpacity");
    addOutlineGroup("HudBombLeft");
}

void MelonPrimeHudConfigOnScreenEdit::populateBombIcon()
{
    addBuiltins("Metroid.Visual.HudBombLeftIconShow",
        "Metroid.Visual.HudBombLeftIconColorR", "Metroid.Visual.HudBombLeftIconColorG", "Metroid.Visual.HudBombLeftIconColorB",
        "Metroid.Visual.HudBombLeftIconPosAnchor");
    addOffsetRows("Metroid.Visual.HudBombLeftIconPosX", "Metroid.Visual.HudBombLeftIconPosY", -256, 256,
        QStringLiteral("Pos X"), QStringLiteral("Pos Y"));
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.HudBombIconOpacity");
    addCheckBox(QStringLiteral("Color Overlay"), "Metroid.Visual.HudBombLeftIconColorOverlay");
    addComboBox(QStringLiteral("Mode"), "Metroid.Visual.HudBombLeftIconMode",
        {QStringLiteral("Relative"), QStringLiteral("Independent")});
    addSpinBox(QStringLiteral("Height"), "Metroid.Visual.HudBombIconHeight", 4, 64);
    addOffsetRows("Metroid.Visual.HudBombLeftIconOfsX", "Metroid.Visual.HudBombLeftIconOfsY", -128, 128,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addAlign3Combo(QStringLiteral("Align X"), "Metroid.Visual.HudBombLeftIconAnchorX");
    addComboBox(QStringLiteral("Align Y"), "Metroid.Visual.HudBombLeftIconAnchorY",
        {QStringLiteral("Top"), QStringLiteral("Center"), QStringLiteral("Bottom")});
    addOutlineGroup("HudBombIcon");
}

void MelonPrimeHudConfigOnScreenEdit::populateForCrosshair()
{
    m_populating = true;
    clearForm();
    m_currentElem = -2; // -2 = crosshair (not a regular element)
    m_title->setText(QStringLiteral("Crosshair"));

    addColorPicker(QStringLiteral("Color"),
        "Metroid.Visual.CrosshairColorR",
        "Metroid.Visual.CrosshairColorG",
        "Metroid.Visual.CrosshairColorB");
    addSpinBox(QStringLiteral("Scale %"), "Metroid.Visual.CrosshairScale", 100, 800);
    addCheckBox(QStringLiteral("Outline"), "Metroid.Visual.CrosshairOutline");
    addOpacitySlider(QStringLiteral("Outline Opacity"), "Metroid.Visual.CrosshairOutlineOpacity");
    addSpinBox(QStringLiteral("Outline Thick."), "Metroid.Visual.CrosshairOutlineThickness", 1, 10);
    addCheckBox(QStringLiteral("Center Dot"), "Metroid.Visual.CrosshairCenterDot");
    addOpacitySlider(QStringLiteral("Dot Opacity"), "Metroid.Visual.CrosshairDotOpacity");
    addSpinBox(QStringLiteral("Dot Thick."), "Metroid.Visual.CrosshairDotThickness", 1, 10);
    addCheckBox(QStringLiteral("T-Style"), "Metroid.Visual.CrosshairTStyle");

    addSeparator();
    // Inner Lines
    addSectionHeader(QStringLiteral("Inner Lines"));
    addCheckBox(QStringLiteral("Show"), "Metroid.Visual.CrosshairInnerShow");
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.CrosshairInnerOpacity");
    addSpinBox(QStringLiteral("Length X"), "Metroid.Visual.CrosshairInnerLengthX", 0, 64);
    addSpinBox(QStringLiteral("Length Y"), "Metroid.Visual.CrosshairInnerLengthY", 0, 64);
    addCheckBox(QStringLiteral("Link XY"), "Metroid.Visual.CrosshairInnerLinkXY");
    addSpinBox(QStringLiteral("Thickness"), "Metroid.Visual.CrosshairInnerThickness", 1, 10);
    addSpinBox(QStringLiteral("Offset"), "Metroid.Visual.CrosshairInnerOffset", 0, 64);

    addSeparator();
    // Outer Lines
    addSectionHeader(QStringLiteral("Outer Lines"));
    addCheckBox(QStringLiteral("Show"), "Metroid.Visual.CrosshairOuterShow");
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.CrosshairOuterOpacity");
    addSpinBox(QStringLiteral("Length X"), "Metroid.Visual.CrosshairOuterLengthX", 0, 64);
    addSpinBox(QStringLiteral("Length Y"), "Metroid.Visual.CrosshairOuterLengthY", 0, 64);
    addCheckBox(QStringLiteral("Link XY"), "Metroid.Visual.CrosshairOuterLinkXY");
    addSpinBox(QStringLiteral("Thickness"), "Metroid.Visual.CrosshairOuterThickness", 1, 10);
    addSpinBox(QStringLiteral("Offset"), "Metroid.Visual.CrosshairOuterOffset", 0, 64);

    m_populating = false;
}

void MelonPrimeHudConfigOnScreenEdit::populateWeaponInventory()
{
    addBuiltins("Metroid.Visual.HudWeaponInventoryShow",
        "Metroid.Visual.HudWeaponInventoryColorR",
        "Metroid.Visual.HudWeaponInventoryColorG",
        "Metroid.Visual.HudWeaponInventoryColorB",
        "Metroid.Visual.HudWeaponInventoryAnchor");
    addOffsetRows("Metroid.Visual.HudWeaponInventoryX", "Metroid.Visual.HudWeaponInventoryY", -256, 256,
        QStringLiteral("Offset X"), QStringLiteral("Offset Y"));
    addComboBox(QStringLiteral("Orientation"), "Metroid.Visual.HudWeaponInventoryOrientation",
        {QStringLiteral("Horizontal"), QStringLiteral("Vertical")});
    addAlign3Combo(QStringLiteral("Align"), "Metroid.Visual.HudWeaponInventoryAlign");
    addSpinBox(QStringLiteral("Icon Height"), "Metroid.Visual.HudWeaponInventoryIconHeight", 4, 48);
    addSpinBox(QStringLiteral("Spacing"), "Metroid.Visual.HudWeaponInventorySpacing", 0, 32);
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.HudWeaponInventoryOpacity");
    addOpacitySlider(QStringLiteral("Not Owned Opacity"), "Metroid.Visual.HudWeaponInventoryNotOwnedOpacity");
    addSeparator();
    addOutlineGroup("HudWeaponInventory");
    addOutlineGroup("HudWeaponInventoryIcon");
    addSeparator();
    addCheckBox(QStringLiteral("Highlight Current Weapon"), "Metroid.Visual.HudWeaponInventoryHighlightEnable");
    addColorPicker(QStringLiteral("Highlight Color"),
        "Metroid.Visual.HudWeaponInventoryHighlightColorR",
        "Metroid.Visual.HudWeaponInventoryHighlightColorG",
        "Metroid.Visual.HudWeaponInventoryHighlightColorB");
    addOpacitySlider(QStringLiteral("Highlight Opacity"), "Metroid.Visual.HudWeaponInventoryHighlightOpacity");
    addDoubleSpinBox(QStringLiteral("Highlight Thickness"), "Metroid.Visual.HudWeaponInventoryHighlightThickness", 0.1, 8.0, 0.25);
    addSpinBox(QStringLiteral("Highlight Padding"), "Metroid.Visual.HudWeaponInventoryHighlightPadding", 0, 16);
    addSpinBox(QStringLiteral("Highlight Corner Radius"), "Metroid.Visual.HudWeaponInventoryHighlightCornerRadius", 0, 16);
    addSpinBox(QStringLiteral("Highlight Offset Left"),   "Metroid.Visual.HudWeaponInventoryHighlightSizeOffsetLeft",   -16, 32);
    addSpinBox(QStringLiteral("Highlight Offset Right"),  "Metroid.Visual.HudWeaponInventoryHighlightSizeOffsetRight",  -16, 32);
    addSpinBox(QStringLiteral("Highlight Offset Top"),    "Metroid.Visual.HudWeaponInventoryHighlightSizeOffsetTop",    -16, 32);
    addSpinBox(QStringLiteral("Highlight Offset Bottom"), "Metroid.Visual.HudWeaponInventoryHighlightSizeOffsetBottom", -16, 32);
}

void MelonPrimeHudConfigOnScreenEdit::populateRadar()
{
    addBuiltins("Metroid.Visual.BtmOverlayEnable",
        nullptr, nullptr, nullptr,
        "Metroid.Visual.BtmOverlayAnchor");
    addOffsetRows("Metroid.Visual.BtmOverlayDstX", "Metroid.Visual.BtmOverlayDstY", -256, 256,
        QStringLiteral("Dst X"), QStringLiteral("Dst Y"));
    addSpinBox(QStringLiteral("Dst Size"), "Metroid.Visual.BtmOverlayDstSize", 16, 128);
    addOpacitySlider(QStringLiteral("Opacity"), "Metroid.Visual.BtmOverlayOpacity");
    addSpinBox(QStringLiteral("Src Radius"), "Metroid.Visual.BtmOverlaySrcRadius", 10, 120);
    addColorPicker(QStringLiteral("Radar Color"),
        "Metroid.Visual.BtmOverlayRadarColorR",
        "Metroid.Visual.BtmOverlayRadarColorG",
        "Metroid.Visual.BtmOverlayRadarColorB");
    addCheckBox(QStringLiteral("Use Hunter Color"), "Metroid.Visual.BtmOverlayRadarColorUseHunter");
    addOutlineGroup("BtmOverlay");
    addOutlineGroup("BtmOverlayFrame");
}

#endif // MELONPRIME_CUSTOM_HUD
