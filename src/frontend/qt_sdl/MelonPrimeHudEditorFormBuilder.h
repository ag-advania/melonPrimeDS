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
class QSlider;
class QLineEdit;
class QObject;

namespace Config {
class Table;
}

namespace MelonPrime::HudEditorForm {

// `populating` is a reference to the owning widget's live guard flag (e.g.
// `MelonPrimeHudConfigOnScreenEdit::m_populating`), not a value snapshot.
// Every member here is a reference to an object that outlives the connected
// Qt signal, so `WidgetFactoryContext` is safe to capture *by value* inside
// a `connect(...)` lambda — capturing it by reference would dangle once the
// caller's stack frame (where the context is normally constructed) returns.
struct WidgetFactoryContext {
    QWidget& parent;
    QFormLayout& form;
    QList<QWidget*>& rows;
    Config::Table& cfg;
    bool& populating;
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

QSlider* AddOpacitySliderRow(WidgetFactoryContext& ctx,
                             const QString& label, const char* key);

QLineEdit* AddLineEditRow(WidgetFactoryContext& ctx,
                          const QString& label, const char* key);

QPushButton* AddColorPickerRow(WidgetFactoryContext& ctx,
                               const QString& label,
                               const char* keyR, const char* keyG, const char* keyB);

void AddSubColorRow(WidgetFactoryContext& ctx,
                    const QString& label, const char* overallKey,
                    const char* keyR, const char* keyG, const char* keyB);

void AddColorOverlayRow(WidgetFactoryContext& ctx,
                        const QString& label, const char* enableKey,
                        const char* keyR, const char* keyG, const char* keyB);

[[nodiscard]] WidgetFactoryContext MakeFactoryContext(
    QWidget& parent,
    QFormLayout& form,
    QList<QWidget*>& rows,
    Config::Table& cfg,
    bool& populating,
    QObject& signalReceiver);

} // namespace MelonPrime::HudEditorForm
