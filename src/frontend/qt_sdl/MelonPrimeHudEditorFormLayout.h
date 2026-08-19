#pragma once

#include <QFormLayout>
#include <QString>

class QLabel;
class QWidget;

namespace MelonPrime::HudEditorForm {

// Apply the layout contract shared by every translated QFormLayout label.
// The caller supplies already-translated text so this utility remains usable
// by geometry tests without pulling in the full localization catalog.
void ConfigureWrappedFormLabel(QLabel& label);

QLabel* CreateTranslatedFormLabel(QFormLayout& form, const QString& text);

// Bound wrapped labels to the viewport after the panel width is selected.
// This prevents an unbreakable future translation from turning QLabel's
// minimum-size hint into hidden horizontal overflow inside QScrollArea.
void ConstrainWrappedFormLabels(QWidget& root, int maximumWidth);

} // namespace MelonPrime::HudEditorForm
