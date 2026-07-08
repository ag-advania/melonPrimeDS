#include "MelonPrimeHudEditorFormBuilder.h"

#include "Config.h"
#include "MelonPrimeHudRender.h"
#include "MelonPrimeLocalization.h"

#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>

namespace MelonPrime::HudEditorForm {

namespace {

constexpr int kColorButtonWidth = 74;
constexpr int kRadioOnWidth = 48;
constexpr int kRadioOffWidth = 58;

} // namespace

WidgetFactoryContext MakeFactoryContext(
    QWidget& parent,
    QFormLayout& form,
    QList<QWidget*>& rows,
    Config::Table& cfg,
    bool populating,
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
        [&ctx, k](bool checked) {
            if (ctx.populating || !checked) return;
            SetBoolIfEditing(ctx.cfg, ctx.populating, k, true);
        });
    QObject::connect(off, &QRadioButton::toggled, &ctx.signalReceiver,
        [&ctx, k](bool checked) {
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
        &ctx.signalReceiver, [&ctx, k](int idx) {
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
        &ctx.signalReceiver, [&ctx, k](int v) {
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
        &ctx.signalReceiver, [&ctx, k](double v) {
            SetDoubleIfEditing(ctx.cfg, ctx.populating, k, v);
        });
    AppendLabeledRow(ctx.form, ctx.rows, label, *sb);
    return sb;
}

} // namespace MelonPrime::HudEditorForm
