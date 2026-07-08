#pragma once

#include <QFormLayout>
#include <QList>
#include <QString>
#include <string>

class QWidget;
class QPushButton;

namespace Config {
class Table;
}

namespace MelonPrime::HudEditorForm {

void UpdateColorButton(QPushButton& button, int r, int g, int b);

void InvalidateHudConfigCache();

void AppendLabeledRow(QFormLayout& form, QList<QWidget*>& rows,
                      const QString& label, QWidget& widget);

void SetBoolIfEditing(Config::Table& cfg, bool populating,
                      const std::string& key, bool value);

void SetIntIfEditing(Config::Table& cfg, bool populating,
                     const std::string& key, int value);

void SetDoubleIfEditing(Config::Table& cfg, bool populating,
                        const std::string& key, double value);

void SetStringIfEditing(Config::Table& cfg, bool populating,
                        const std::string& key, const std::string& value);

} // namespace MelonPrime::HudEditorForm
