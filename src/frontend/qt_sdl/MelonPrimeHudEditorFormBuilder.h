#pragma once

#include <QFormLayout>
#include <QList>
#include <QString>
#include <string>

class QWidget;
class QPushButton;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QObject;

namespace Config {
class Table;
}

namespace MelonPrime::HudEditorForm {

struct WidgetFactoryContext {
    QWidget& parent;
    QFormLayout& form;
    QList<QWidget*>& rows;
    Config::Table& cfg;
    bool populating;
    QObject& signalReceiver;
};

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

QWidget* AddBoolRadioRow(WidgetFactoryContext& ctx,
                         const QString& label, const char* key);

QComboBox* AddComboBoxRow(WidgetFactoryContext& ctx,
                          const QString& label, const char* key,
                          const QStringList& items);

QSpinBox* AddSpinBoxRow(WidgetFactoryContext& ctx,
                        const QString& label, const char* key,
                        int min, int max);

QDoubleSpinBox* AddDoubleSpinBoxRow(WidgetFactoryContext& ctx,
                                    const QString& label, const char* key,
                                    double min, double max, double step);

[[nodiscard]] WidgetFactoryContext MakeFactoryContext(
    QWidget& parent,
    QFormLayout& form,
    QList<QWidget*>& rows,
    Config::Table& cfg,
    bool populating,
    QObject& signalReceiver);

} // namespace MelonPrime::HudEditorForm
