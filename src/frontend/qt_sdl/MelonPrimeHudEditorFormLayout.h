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

// True when the widget is rendered with Qt's native Windows style
// (windowsvista/windows11). This editor's fixed-pixel row budgets (radio
// pair + color swatch, etc.) were sized with zero slack against that one
// style's chrome. Fusion, the native macOS style, and the offscreen
// fallback used on Linux/BSD all render a few px wider, so composite-row
// builders use this to add a small safety margin outside native Windows
// chrome instead of shrinking every platform to the tightest case.
bool UsesNativeWindowsChrome(const QWidget& widget);

} // namespace MelonPrime::HudEditorForm
