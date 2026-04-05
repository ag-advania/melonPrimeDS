#ifdef MELONPRIME_CUSTOM_HUD

#include "HudEditSidePanel.h"
#include "MelonPrimeCustomHud.h"
#include "EmuInstance.h"
#include <QColorDialog>
#include <QHBoxLayout>
#include <QFrame>

// ─── Construction ───────────────────────────────────────────────────────────

HudEditSidePanel::HudEditSidePanel(QWidget* parent, EmuInstance* emu)
    : QWidget(parent), m_emu(emu)
{
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "HudEditSidePanel { background: rgba(24,24,32,210); border: 1px solid #555; border-radius: 4px; }"
        "QLabel { color: #ccc; font-size: 9px; }"
        "QCheckBox { color: #ccc; font-size: 9px; }"
        "QComboBox { font-size: 9px; }"
        "QSpinBox { font-size: 9px; }"
        "QDoubleSpinBox { font-size: 9px; }"
        "QLineEdit { font-size: 9px; background: #333; color: #eee; border: 1px solid #555; }"
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

Config::Table& HudEditSidePanel::cfg()
{
    return m_emu->getLocalConfig();
}

// ─── Clear ──────────────────────────────────────────────────────────────────

void HudEditSidePanel::clearForm()
{
    while (m_form->rowCount() > 0)
        m_form->removeRow(0);
    m_rows.clear();
}

void HudEditSidePanel::clear()
{
    clearForm();
    m_currentElem = -1;
    m_title->clear();
    hide();
}

// ─── Reload values ──────────────────────────────────────────────────────────

void HudEditSidePanel::reloadValues()
{
    if (m_currentElem >= 0)
        populateForElement(m_currentElem);
}

// ─── Factory: CheckBox ──────────────────────────────────────────────────────

QCheckBox* HudEditSidePanel::addCheckBox(const QString& label, const char* key)
{
    auto* cb = new QCheckBox(this);
    cb->setChecked(cfg().GetBool(key));
    std::string k(key);
    connect(cb, &QCheckBox::toggled, this, [this, k](bool v) {
        if (m_populating) return;
        cfg().SetBool(k, v);
        MelonPrime::CustomHud_InvalidateConfigCache();
    });
    m_form->addRow(label, cb);
    m_rows.append(cb);
    return cb;
}

// ─── Factory: ComboBox ──────────────────────────────────────────────────────

QComboBox* HudEditSidePanel::addComboBox(const QString& label, const char* key, const QStringList& items)
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

QSpinBox* HudEditSidePanel::addSpinBox(const QString& label, const char* key, int min, int max)
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

QDoubleSpinBox* HudEditSidePanel::addDoubleSpinBox(const QString& label, const char* key, double min, double max, double step)
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

// ─── Factory: LineEdit ──────────────────────────────────────────────────────

QLineEdit* HudEditSidePanel::addLineEdit(const QString& label, const char* key)
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

QPushButton* HudEditSidePanel::addColorPicker(const QString& label, const char* keyR, const char* keyG, const char* keyB)
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

void HudEditSidePanel::addSubColor(const QString& label, const char* overallKey,
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

// ─── Separator ──────────────────────────────────────────────────────────────

void HudEditSidePanel::addSeparator()
{
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #555;");
    m_form->addRow(line);
    m_rows.append(line);
}

// ─── Built-ins: Show / Color / Anchor ───────────────────────────────────────

void HudEditSidePanel::addBuiltins(const char* showKey,
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

// ─── Main populate dispatch ─────────────────────────────────────────────────

static const char* kElementNames[] = {
    "HP", "HP Gauge", "Weapon/Ammo", "Weapon Icon", "Ammo Gauge",
    "Match Status", "Rank", "Time Left", "Time Limit",
    "Bomb Left", "Bomb Icon", "Radar"
};

void HudEditSidePanel::populateForElement(int idx)
{
    m_populating = true;
    clearForm();
    m_currentElem = idx;

    if (idx < 0 || idx >= 12) {
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
    }

    show();
    m_populating = false;
}

// ─── Per-element populate ───────────────────────────────────────────────────

void HudEditSidePanel::populateHP()
{
    addBuiltins(nullptr,
        "Metroid.Visual.HudHpTextColorR", "Metroid.Visual.HudHpTextColorG", "Metroid.Visual.HudHpTextColorB",
        "Metroid.Visual.HudHpAnchor");
    addLineEdit(QStringLiteral("Prefix"), "Metroid.Visual.HudHpPrefix");
    addComboBox(QStringLiteral("Align"), "Metroid.Visual.HudHpAlign",
        {QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
    addCheckBox(QStringLiteral("Auto Color"), "Metroid.Visual.HudHpTextAutoColor");
}

void HudEditSidePanel::populateHPGauge()
{
    addBuiltins("Metroid.Visual.HudHpGauge",
        "Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB",
        "Metroid.Visual.HudHpGaugePosAnchor");
    addComboBox(QStringLiteral("Position"), "Metroid.Visual.HudHpGaugeAnchor",
        {QStringLiteral("Below"), QStringLiteral("Above"), QStringLiteral("Right"), QStringLiteral("Left"), QStringLiteral("Center")});
    addSpinBox(QStringLiteral("Offset X"), "Metroid.Visual.HudHpGaugeOffsetX", -128, 128);
    addSpinBox(QStringLiteral("Offset Y"), "Metroid.Visual.HudHpGaugeOffsetY", -128, 128);
    addCheckBox(QStringLiteral("Auto Color"), "Metroid.Visual.HudHpGaugeAutoColor");
}

void HudEditSidePanel::populateWeaponAmmo()
{
    addBuiltins(nullptr,
        "Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB",
        "Metroid.Visual.HudWeaponAnchor");
    addLineEdit(QStringLiteral("Prefix"), "Metroid.Visual.HudAmmoPrefix");
    addComboBox(QStringLiteral("Align"), "Metroid.Visual.HudAmmoAlign",
        {QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
}

void HudEditSidePanel::populateWpnIcon()
{
    addBuiltins("Metroid.Visual.HudWeaponIconShow",
        nullptr, nullptr, nullptr,
        "Metroid.Visual.HudWeaponIconPosAnchor");
    addComboBox(QStringLiteral("Mode"), "Metroid.Visual.HudWeaponIconMode",
        {QStringLiteral("Relative"), QStringLiteral("Independent")});
    addSpinBox(QStringLiteral("Offset X"), "Metroid.Visual.HudWeaponIconOffsetX", -128, 128);
    addSpinBox(QStringLiteral("Offset Y"), "Metroid.Visual.HudWeaponIconOffsetY", -128, 128);
    addComboBox(QStringLiteral("Align X"), "Metroid.Visual.HudWeaponIconAnchorX",
        {QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
    addComboBox(QStringLiteral("Align Y"), "Metroid.Visual.HudWeaponIconAnchorY",
        {QStringLiteral("Top"), QStringLiteral("Center"), QStringLiteral("Bottom")});
    addCheckBox(QStringLiteral("Color Override"), "Metroid.Visual.HudWeaponIconColorOverlay");
}

void HudEditSidePanel::populateAmmoGauge()
{
    addBuiltins("Metroid.Visual.HudAmmoGauge",
        "Metroid.Visual.HudAmmoGaugeColorR", "Metroid.Visual.HudAmmoGaugeColorG", "Metroid.Visual.HudAmmoGaugeColorB",
        "Metroid.Visual.HudAmmoGaugePosAnchor");
    addComboBox(QStringLiteral("Position"), "Metroid.Visual.HudAmmoGaugeAnchor",
        {QStringLiteral("Below"), QStringLiteral("Above"), QStringLiteral("Right"), QStringLiteral("Left"), QStringLiteral("Center")});
    addSpinBox(QStringLiteral("Offset X"), "Metroid.Visual.HudAmmoGaugeOffsetX", -128, 128);
    addSpinBox(QStringLiteral("Offset Y"), "Metroid.Visual.HudAmmoGaugeOffsetY", -128, 128);
}

void HudEditSidePanel::populateMatchStatus()
{
    addBuiltins("Metroid.Visual.HudMatchStatusShow",
        "Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB",
        "Metroid.Visual.HudMatchStatusAnchor");
    addComboBox(QStringLiteral("Label Pos"), "Metroid.Visual.HudMatchStatusLabelPos",
        {QStringLiteral("Above"), QStringLiteral("Below"), QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Center")});
    addSpinBox(QStringLiteral("Label Ofs X"), "Metroid.Visual.HudMatchStatusLabelOfsX", -128, 128);
    addSpinBox(QStringLiteral("Label Ofs Y"), "Metroid.Visual.HudMatchStatusLabelOfsY", -128, 128);
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
}

void HudEditSidePanel::populateRank()
{
    addBuiltins("Metroid.Visual.HudRankShow",
        "Metroid.Visual.HudRankColorR", "Metroid.Visual.HudRankColorG", "Metroid.Visual.HudRankColorB",
        "Metroid.Visual.HudRankAnchor");
    addLineEdit(QStringLiteral("Prefix"), "Metroid.Visual.HudRankPrefix");
    addCheckBox(QStringLiteral("Ordinal"), "Metroid.Visual.HudRankShowOrdinal");
    addLineEdit(QStringLiteral("Suffix"), "Metroid.Visual.HudRankSuffix");
    addComboBox(QStringLiteral("Align"), "Metroid.Visual.HudRankAlign",
        {QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
}

void HudEditSidePanel::populateTimeLeft()
{
    addBuiltins("Metroid.Visual.HudTimeLeftShow",
        "Metroid.Visual.HudTimeLeftColorR", "Metroid.Visual.HudTimeLeftColorG", "Metroid.Visual.HudTimeLeftColorB",
        "Metroid.Visual.HudTimeLeftAnchor");
    addComboBox(QStringLiteral("Align"), "Metroid.Visual.HudTimeLeftAlign",
        {QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
}

void HudEditSidePanel::populateTimeLimit()
{
    addBuiltins("Metroid.Visual.HudTimeLimitShow",
        "Metroid.Visual.HudTimeLimitColorR", "Metroid.Visual.HudTimeLimitColorG", "Metroid.Visual.HudTimeLimitColorB",
        "Metroid.Visual.HudTimeLimitAnchor");
    addComboBox(QStringLiteral("Align"), "Metroid.Visual.HudTimeLimitAlign",
        {QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
}

void HudEditSidePanel::populateBombLeft()
{
    addBuiltins("Metroid.Visual.HudBombLeftShow",
        "Metroid.Visual.HudBombLeftColorR", "Metroid.Visual.HudBombLeftColorG", "Metroid.Visual.HudBombLeftColorB",
        "Metroid.Visual.HudBombLeftAnchor");
    addCheckBox(QStringLiteral("Show Number"), "Metroid.Visual.HudBombLeftTextShow");
    addComboBox(QStringLiteral("Align"), "Metroid.Visual.HudBombLeftAlign",
        {QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
    addLineEdit(QStringLiteral("Prefix"), "Metroid.Visual.HudBombLeftPrefix");
    addLineEdit(QStringLiteral("Suffix"), "Metroid.Visual.HudBombLeftSuffix");
}

void HudEditSidePanel::populateBombIcon()
{
    addBuiltins("Metroid.Visual.HudBombLeftIconShow",
        "Metroid.Visual.HudBombLeftIconColorR", "Metroid.Visual.HudBombLeftIconColorG", "Metroid.Visual.HudBombLeftIconColorB",
        "Metroid.Visual.HudBombLeftIconPosAnchor");
    addCheckBox(QStringLiteral("Color Overlay"), "Metroid.Visual.HudBombLeftIconColorOverlay");
    addComboBox(QStringLiteral("Mode"), "Metroid.Visual.HudBombLeftIconMode",
        {QStringLiteral("Relative"), QStringLiteral("Independent")});
    addSpinBox(QStringLiteral("Offset X"), "Metroid.Visual.HudBombLeftIconOfsX", -128, 128);
    addSpinBox(QStringLiteral("Offset Y"), "Metroid.Visual.HudBombLeftIconOfsY", -128, 128);
    addComboBox(QStringLiteral("Align X"), "Metroid.Visual.HudBombLeftIconAnchorX",
        {QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
    addComboBox(QStringLiteral("Align Y"), "Metroid.Visual.HudBombLeftIconAnchorY",
        {QStringLiteral("Top"), QStringLiteral("Center"), QStringLiteral("Bottom")});
}

void HudEditSidePanel::populateRadar()
{
    addBuiltins("Metroid.Visual.BtmOverlayEnable",
        nullptr, nullptr, nullptr,
        "Metroid.Visual.BtmOverlayAnchor");
    addDoubleSpinBox(QStringLiteral("Opacity"), "Metroid.Visual.BtmOverlayOpacity", 0.0, 1.0, 0.05);
    addSpinBox(QStringLiteral("Src Radius"), "Metroid.Visual.BtmOverlaySrcRadius", 10, 120);
}

#endif // MELONPRIME_CUSTOM_HUD
