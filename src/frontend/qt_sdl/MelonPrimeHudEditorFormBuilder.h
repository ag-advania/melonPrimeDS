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
//
// LIFETIME RULE — do not violate when adding members: every field must be a
// reference (or a trivially-copyable primitive that is itself semantically
// a reference, like `bool&`) to an object owned by the HUD editor widget
// for its whole lifetime. Never add:
//   - a value member that owns data (QString, std::string, std::vector, ...)
//   - a reference/pointer to a temporary, a by-value function parameter, or
//     any other object whose lifetime does not outlast the panel
// A `WidgetFactoryContext` instance is expected to be captured by value into
// long-lived `connect(...)` lambdas; anything that violates this turns every
// such lambda into a dangling-reference bug the first time its signal fires
// after the constructing stack frame returns (see the Phase 12 fix in
// melonprime-srp-refactor-v3-progress.md for the bug this pattern replaced).
struct WidgetFactoryContext {
    QWidget& parent;
    QFormLayout& form;
    QList<QWidget*>& rows;
    Config::Table& cfg;
    bool& populating;
    QObject& signalReceiver;
};

// ─── Shared helpers ─────────────────────────────────────────────────────────

void UpdateColorButton(QPushButton& button, int r, int g, int b);

void InvalidateHudConfigCache();

void AppendLabeledRow(QFormLayout& form, QList<QWidget*>& rows,
                      const QString& label, QWidget& widget);

// ─── Config write helpers (all no-op while `populating` is true) ───────────

void SetBoolIfEditing(Config::Table& cfg, bool populating,
                      const std::string& key, bool value);

void SetIntIfEditing(Config::Table& cfg, bool populating,
                     const std::string& key, int value);

void SetDoubleIfEditing(Config::Table& cfg, bool populating,
                        const std::string& key, double value);

void SetStringIfEditing(Config::Table& cfg, bool populating,
                        const std::string& key, const std::string& value);

// ─── Plain value-widget row factories ───────────────────────────────────────

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

// ─── Color row factories (all route through ColorDialogPrefs::getColor —
// never call QColorDialog directly; enforced by audit-color-dialog-prefs.ps1) ─

QPushButton* AddColorPickerRow(WidgetFactoryContext& ctx,
                               const QString& label,
                               const char* keyR, const char* keyG, const char* keyB);

void AddSubColorRow(WidgetFactoryContext& ctx,
                    const QString& label, const char* overallKey,
                    const char* keyR, const char* keyG, const char* keyB);

void AddColorOverlayRow(WidgetFactoryContext& ctx,
                        const QString& label, const char* enableKey,
                        const char* keyR, const char* keyG, const char* keyB);

// ─── Context construction ───────────────────────────────────────────────────

[[nodiscard]] WidgetFactoryContext MakeFactoryContext(
    QWidget& parent,
    QFormLayout& form,
    QList<QWidget*>& rows,
    Config::Table& cfg,
    bool& populating,
    QObject& signalReceiver);

} // namespace MelonPrime::HudEditorForm
