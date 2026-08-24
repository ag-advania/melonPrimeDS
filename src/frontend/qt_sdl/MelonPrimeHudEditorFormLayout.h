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

// Width the panel has to add to its natural content width so the vertical
// scrollbar its form will eventually show does not eat into the form itself.
// QScrollArea::sizeHint() only accounts for that scrollbar when the policy is
// ScrollBarAlwaysOn, so a panel sized from the natural hint alone loses exactly
// one scrollbar width the moment the content scrolls -- and on hosts with wide
// default fonts that is enough to squeeze the widest fixed-width row past the
// viewport edge. Returns 0 when no scroll area is present or its vertical
// scrollbar is disabled or already priced into the hint.
int ReservedVerticalScrollBarWidth(const QWidget& root);

// Bound the wrapped labels and settle the panel at exactly targetWidth.
// Each pass re-bounds the labels against the width the panel is going to have
// (never a transiently oversized viewport) and clears the latched layout
// minimum before re-activating, because QLayout::SetDefaultConstraint only ever
// raises a widget's minimum size: once an unbounded label has pushed that
// minimum past the window, every later resize() back to the target width is
// clamped straight back up again.
void ConstrainPanelWidth(QWidget& root, int targetWidth, int passes);

// True when the widget is rendered with Qt's native Windows style
// (windowsvista/windows11). This editor's fixed-pixel row budgets (radio
// pair + color swatch, etc.) were sized with zero slack against that one
// style's chrome. Fusion, the native macOS style, and the offscreen
// fallback used on Linux/BSD all render a few px wider, so composite-row
// builders use this to add a small safety margin outside native Windows
// chrome instead of shrinking every platform to the tightest case.
bool UsesNativeWindowsChrome(const QWidget& widget);

} // namespace MelonPrime::HudEditorForm
