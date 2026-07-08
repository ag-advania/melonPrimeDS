#include "MelonPrimeHudEditorFormBuilder.h"

#include "Config.h"
#include "MelonPrimeColorDialogPrefs.h"
#include "MelonPrimeHudRender.h"
#include "MelonPrimeLocalization.h"

#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QtGlobal>

namespace MelonPrime::HudEditorForm {

namespace {

constexpr int kColorButtonWidth = 74;
constexpr int kRadioOnWidth = 48;
constexpr int kRadioOffWidth = 58;
constexpr int kSliderValueWidth = 38;
constexpr int kLineEditMinWidth = 78;

} // namespace

WidgetFactoryContext MakeFactoryContext(
    QWidget& parent,
    QFormLayout& form,
    QList<QWidget*>& rows,
    Config::Table& cfg,
    bool& populating,
    QObject& signalReceiver)
{
    return WidgetFactoryContext{ parent, form, rows, cfg, populating, signalReceiver };
}

void UpdateColorButton(QPushButton& button, int r, int g, int b)
{
    const QColor color(r, g, b);
    const int luma = (color.red() * 299 + color.green() * 587 + color.blue() * 114) / 1000;
    const QString textColor = (luma >= 128) ? QStringLiteral("#000") : QStringLiteral("#fff");
    const QString colorName = color.name();
    button.setMinimumWidth(kColorButtonWidth);
    button.setStyleSheet(QStringLiteral(
        "font-size: 9px; color: %1; background-color: %2; border: 1px solid #888;"
        "min-width: %3px; min-height: 16px; padding: 1px 4px;")
        .arg(textColor, colorName)
        .arg(kColorButtonWidth));
    button.setText(colorName);
}

void InvalidateHudConfigCache()
{
    CustomHud_InvalidateConfigCache();
}

void AppendLabeledRow(QFormLayout& form, QList<QWidget*>& rows,
                      const QString& label, QWidget& widget)
{
    form.addRow(UiText::Tr(label), &widget);
    rows.append(&widget);
}

void SetBoolIfEditing(Config::Table& cfg, bool populating,
                      const std::string& key, bool value)
{
    if (populating) return;
    cfg.SetBool(key, value);
    InvalidateHudConfigCache();
}

void SetIntIfEditing(Config::Table& cfg, bool populating,
                     const std::string& key, int value)
{
    if (populating) return;
    cfg.SetInt(key, value);
    InvalidateHudConfigCache();
}

void SetDoubleIfEditing(Config::Table& cfg, bool populating,
                        const std::string& key, double value)
{
    if (populating) return;
    cfg.SetDouble(key, value);
    InvalidateHudConfigCache();
}

void SetStringIfEditing(Config::Table& cfg, bool populating,
                        const std::string& key, const std::string& value)
{
    if (populating) return;
    cfg.SetString(key, value);
    InvalidateHudConfigCache();
}

QWidget* AddBoolRadioRow(WidgetFactoryContext& ctx,
                         const QString& label, const char* key)
{
    auto* container = new QWidget(&ctx.parent);
    auto* hlay = new QHBoxLayout(container);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(6);

    auto* rowLabel = new QLabel(UiText::Tr(label), container);
    rowLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* on = new QRadioButton(QStringLiteral("ON"), container);
    auto* off = new QRadioButton(QStringLiteral("OFF"), container);
    on->setMinimumWidth(kRadioOnWidth);
    off->setMinimumWidth(kRadioOffWidth);
    on->setCursor(Qt::PointingHandCursor);
    off->setCursor(Qt::PointingHandCursor);

    const bool enabled = ctx.cfg.GetBool(key);
    on->setChecked(enabled);
    off->setChecked(!enabled);

    std::string k(key);
    QObject::connect(on, &QRadioButton::toggled, &ctx.signalReceiver,
        [ctx, k](bool checked) {
            if (ctx.populating || !checked) return;
            SetBoolIfEditing(ctx.cfg, ctx.populating, k, true);
        });
    QObject::connect(off, &QRadioButton::toggled, &ctx.signalReceiver,
        [ctx, k](bool checked) {
            if (ctx.populating || !checked) return;
            SetBoolIfEditing(ctx.cfg, ctx.populating, k, false);
        });

    hlay->addWidget(rowLabel, 1);
    hlay->addWidget(on, 0);
    hlay->addWidget(off, 0);

    ctx.form.addRow(container);
    ctx.rows.append(container);
    return container;
}

QComboBox* AddComboBoxRow(WidgetFactoryContext& ctx,
                          const QString& label, const char* key,
                          const QStringList& items)
{
    auto* cb = new QComboBox(&ctx.parent);
    cb->setMinimumWidth(78);
    cb->addItems(UiText::TrList(items));
    cb->setCurrentIndex(ctx.cfg.GetInt(key));
    std::string k(key);
    QObject::connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
        &ctx.signalReceiver, [ctx, k](int idx) {
            SetIntIfEditing(ctx.cfg, ctx.populating, k, idx);
        });
    AppendLabeledRow(ctx.form, ctx.rows, label, *cb);
    return cb;
}

QSpinBox* AddSpinBoxRow(WidgetFactoryContext& ctx,
                        const QString& label, const char* key,
                        int min, int max)
{
    auto* sb = new QSpinBox(&ctx.parent);
    sb->setMinimumWidth(64);
    sb->setRange(min, max);
    sb->setValue(ctx.cfg.GetInt(key));
    std::string k(key);
    QObject::connect(sb, QOverload<int>::of(&QSpinBox::valueChanged),
        &ctx.signalReceiver, [ctx, k](int v) {
            SetIntIfEditing(ctx.cfg, ctx.populating, k, v);
        });
    AppendLabeledRow(ctx.form, ctx.rows, label, *sb);
    return sb;
}

QDoubleSpinBox* AddDoubleSpinBoxRow(WidgetFactoryContext& ctx,
                                    const QString& label, const char* key,
                                    double min, double max, double step)
{
    auto* sb = new QDoubleSpinBox(&ctx.parent);
    sb->setMinimumWidth(64);
    sb->setRange(min, max);
    sb->setSingleStep(step);
    sb->setDecimals(2);
    sb->setValue(ctx.cfg.GetDouble(key));
    std::string k(key);
    QObject::connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        &ctx.signalReceiver, [ctx, k](double v) {
            SetDoubleIfEditing(ctx.cfg, ctx.populating, k, v);
        });
    AppendLabeledRow(ctx.form, ctx.rows, label, *sb);
    return sb;
}

QSlider* AddOpacitySliderRow(WidgetFactoryContext& ctx,
                             const QString& label, const char* key)
{
    auto* container = new QWidget(&ctx.parent);
    auto* hlay = new QHBoxLayout(container);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(4);

    auto* slider = new QSlider(Qt::Horizontal, container);
    slider->setRange(0, 100);
    const int initVal = qRound(ctx.cfg.GetDouble(key) * 100.0);
    slider->setValue(initVal);

    auto* lbl = new QLabel(QString::number(initVal) + QStringLiteral("%"), container);
    lbl->setFixedWidth(kSliderValueWidth);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    hlay->addWidget(slider, 1);
    hlay->addWidget(lbl, 0);

    std::string k(key);
    QObject::connect(slider, &QSlider::valueChanged, &ctx.signalReceiver,
        [ctx, k, lbl](int v) {
            lbl->setText(QString::number(v) + QStringLiteral("%"));
            SetDoubleIfEditing(ctx.cfg, ctx.populating, k, v / 100.0);
        });

    ctx.form.addRow(UiText::Tr(label), container);
    ctx.rows.append(container);
    return slider;
}

QLineEdit* AddLineEditRow(WidgetFactoryContext& ctx,
                          const QString& label, const char* key)
{
    auto* le = new QLineEdit(&ctx.parent);
    le->setMinimumWidth(kLineEditMinWidth);
    le->setText(QString::fromStdString(ctx.cfg.GetString(key)));
    std::string k(key);
    QObject::connect(le, &QLineEdit::textChanged, &ctx.signalReceiver,
        [ctx, k](const QString& text) {
            SetStringIfEditing(ctx.cfg, ctx.populating, k, text.toStdString());
        });
    AppendLabeledRow(ctx.form, ctx.rows, label, *le);
    return le;
}

namespace {

// Opens the color picker for the current keyR/keyG/keyB value, and on a
// valid pick writes it back to config + refreshes the swatch button.
// Shared by AddColorPickerRow / AddSubColorRow / AddColorOverlayRow, all of
// which otherwise repeat this exact sequence.
void PickAndApplyColor(QWidget& dialogParent, Config::Table& cfg, QPushButton& btn,
                       const std::string& keyR, const std::string& keyG, const std::string& keyB)
{
    const QColor cur(cfg.GetInt(keyR), cfg.GetInt(keyG), cfg.GetInt(keyB));
    const QColor picked = ColorDialogPrefs::getColor(&dialogParent, cur, UiText::Tr("Pick Color"));
    if (!picked.isValid())
        return;

    cfg.SetInt(keyR, picked.red());
    cfg.SetInt(keyG, picked.green());
    cfg.SetInt(keyB, picked.blue());
    UpdateColorButton(btn, picked.red(), picked.green(), picked.blue());
    InvalidateHudConfigCache();
}

} // namespace

QPushButton* AddColorPickerRow(WidgetFactoryContext& ctx,
                               const QString& label,
                               const char* keyR, const char* keyG, const char* keyB)
{
    auto* btn = new QPushButton(&ctx.parent);
    UpdateColorButton(*btn, ctx.cfg.GetInt(keyR), ctx.cfg.GetInt(keyG), ctx.cfg.GetInt(keyB));

    std::string kR(keyR), kG(keyG), kB(keyB);
    QObject::connect(btn, &QPushButton::clicked, &ctx.signalReceiver, [ctx, btn, kR, kG, kB]() {
        PickAndApplyColor(ctx.parent, ctx.cfg, *btn, kR, kG, kB);
    });

    AppendLabeledRow(ctx.form, ctx.rows, label, *btn);
    return btn;
}

void AddSubColorRow(WidgetFactoryContext& ctx,
                    const QString& label, const char* overallKey,
                    const char* keyR, const char* keyG, const char* keyB)
{
    auto* container = new QWidget(&ctx.parent);
    auto* hlay = new QHBoxLayout(container);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(2);

    auto* combo = new QComboBox(container);
    combo->addItem(UiText::Tr("Overall"));
    combo->addItem(UiText::Tr("Custom"));
    const bool isOverall = ctx.cfg.GetBool(overallKey);
    combo->setCurrentIndex(isOverall ? 0 : 1);

    auto* btn = new QPushButton(container);
    UpdateColorButton(*btn, ctx.cfg.GetInt(keyR), ctx.cfg.GetInt(keyG), ctx.cfg.GetInt(keyB));
    btn->setEnabled(!isOverall);

    hlay->addWidget(combo, 1);
    hlay->addWidget(btn, 0);

    std::string kOver(overallKey), kR(keyR), kG(keyG), kB(keyB);
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        &ctx.signalReceiver, [ctx, btn, kOver](int idx) {
            if (ctx.populating) return;
            ctx.cfg.SetBool(kOver, idx == 0);
            btn->setEnabled(idx != 0);
            InvalidateHudConfigCache();
        });
    QObject::connect(btn, &QPushButton::clicked, &ctx.signalReceiver, [ctx, btn, kR, kG, kB]() {
        PickAndApplyColor(ctx.parent, ctx.cfg, *btn, kR, kG, kB);
    });

    ctx.form.addRow(UiText::Tr(label), container);
    ctx.rows.append(container);
}

void AddColorOverlayRow(WidgetFactoryContext& ctx,
                        const QString& label, const char* enableKey,
                        const char* keyR, const char* keyG, const char* keyB)
{
    auto* container = new QWidget(&ctx.parent);
    auto* hlay = new QHBoxLayout(container);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(4);

    auto* rowLabel = new QLabel(UiText::Tr(label), container);
    rowLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* on = new QRadioButton(QStringLiteral("ON"), container);
    auto* off = new QRadioButton(QStringLiteral("OFF"), container);
    on->setMinimumWidth(kRadioOnWidth);
    off->setMinimumWidth(kRadioOffWidth);
    on->setCursor(Qt::PointingHandCursor);
    off->setCursor(Qt::PointingHandCursor);

    const bool enabled = ctx.cfg.GetBool(enableKey);
    on->setChecked(enabled);
    off->setChecked(!enabled);

    auto* btn = new QPushButton(container);
    UpdateColorButton(*btn, ctx.cfg.GetInt(keyR), ctx.cfg.GetInt(keyG), ctx.cfg.GetInt(keyB));
    btn->setEnabled(enabled);

    hlay->addWidget(rowLabel, 1);
    hlay->addWidget(on, 0);
    hlay->addWidget(off, 0);
    hlay->addWidget(btn, 1);

    std::string kE(enableKey), kR(keyR), kG(keyG), kB(keyB);
    QObject::connect(on, &QRadioButton::toggled, &ctx.signalReceiver, [ctx, btn, kE](bool checked) {
        if (ctx.populating || !checked) return;
        ctx.cfg.SetBool(kE, true);
        btn->setEnabled(true);
        InvalidateHudConfigCache();
    });
    QObject::connect(off, &QRadioButton::toggled, &ctx.signalReceiver, [ctx, btn, kE](bool checked) {
        if (ctx.populating || !checked) return;
        ctx.cfg.SetBool(kE, false);
        btn->setEnabled(false);
        InvalidateHudConfigCache();
    });
    QObject::connect(btn, &QPushButton::clicked, &ctx.signalReceiver, [ctx, btn, kR, kG, kB]() {
        PickAndApplyColor(ctx.parent, ctx.cfg, *btn, kR, kG, kB);
    });

    ctx.form.addRow(container);
    ctx.rows.append(container);
}

} // namespace MelonPrime::HudEditorForm
