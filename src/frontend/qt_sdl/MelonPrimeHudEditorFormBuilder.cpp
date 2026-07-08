#include "MelonPrimeHudEditorFormBuilder.h"

#include "Config.h"
#include "MelonPrimeHudRender.h"
#include "MelonPrimeLocalization.h"

#include <QColor>
#include <QPushButton>

namespace MelonPrime::HudEditorForm {

namespace {

constexpr int kColorButtonWidth = 74;

} // namespace

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

} // namespace MelonPrime::HudEditorForm
