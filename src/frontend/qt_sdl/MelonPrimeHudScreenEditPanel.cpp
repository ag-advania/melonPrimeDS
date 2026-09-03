#include "MelonPrimeHudScreenEditPanel.h"

#ifdef MELONPRIME_CUSTOM_HUD

#include <algorithm>
#include <optional>

#include <QLayout>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

#include "MelonPrimeHudConfigOnScreenEdit.h"
#include "MelonPrimeHudEditorFormLayout.h"
#include "MelonPrimeHudEditorPanelGeometry.h"
#include "Screen.h"

namespace MelonPrime::HudScreenEditPanel {

void Position(
    QWidget* host,
    MelonPrimeHudConfigOnScreenEdit* panel,
    bool setMaximumHeight,
    bool adjustSize)
{
    if (!host || !panel)
        return;

    QWidget* boundsWidget = host->window();
    const QRect bounds = boundsWidget
        ? QRect(boundsWidget->mapToGlobal(QPoint(0, 0)), boundsWidget->size())
        : QRect(host->mapToGlobal(QPoint(0, 0)), host->size());
    const int usableWidth = MelonPrime::HudEditorPanelGeometry::UsableWidth(bounds.width());

    // The panel starts from its natural content size, keeps the historical
    // width only as a preferred baseline for short English labels, and then
    // yields to the actual window width.  The width is established before
    // activating the layout so WrapLongRows can make the final height honest.
    panel->setMinimumWidth(0);
    panel->setMaximumWidth(usableWidth);
    // The natural hint is measured before the form's vertical scrollbar exists,
    // so reserve its width here; otherwise the scrollbar takes it back out of
    // the form the moment the content scrolls.
    const int naturalWidth = std::max(panel->sizeHint().width(), panel->minimumSizeHint().width())
        + MelonPrime::HudEditorForm::ReservedVerticalScrollBarWidth(*panel);
    const int desiredWidth = naturalWidth > MelonPrime::HudEditorPanelGeometry::kPreferredWidth
        ? naturalWidth
        : MelonPrime::HudEditorPanelGeometry::kPreferredWidth;
    const int panelW = std::min(desiredWidth, usableWidth);
    if (panel->width() != panelW)
        panel->resize(panelW, panel->height());
    if (panel->layout())
        panel->layout()->activate();
    MelonPrime::HudEditorForm::ConstrainPanelWidth(*panel, panelW, 2);
    if (adjustSize)
    {
        panel->adjustSize();
        if (panel->width() != panelW)
            panel->resize(panelW, panel->height());
        if (panel->layout())
            panel->layout()->activate();
        MelonPrime::HudEditorForm::ConstrainPanelWidth(*panel, panelW, 1);
    }

    const int maxH = std::max(120, bounds.height()
        - (2 * MelonPrime::HudEditorPanelGeometry::kSafeMargin));
    if (setMaximumHeight)
        panel->setMaximumHeight(maxH);

    const int desiredH = setMaximumHeight ? panel->sizeHint().height() : panel->height();
    const int panelH = std::min(desiredH, maxH);
    if (panel->height() != panelH)
        panel->resize(panel->width(), panelH);

    const auto* screenPanel = qobject_cast<const ScreenPanel*>(host);
    const std::optional<QRect> topScreenLocal = screenPanel
        ? screenPanel->getTopScreenWidgetRect()
        : std::nullopt;
    const QRect targetLocal = topScreenLocal.value_or(host->rect());
    const QRect targetGlobal(host->mapToGlobal(targetLocal.topLeft()), targetLocal.size());

    const int outsideTopX = targetGlobal.right() + 1 + 12;
    const int insideTopX = targetGlobal.right() + 1 - panel->width() - 24;
    const int preferredX = (outsideTopX + panel->width() <= bounds.right() + 1
        - MelonPrime::HudEditorPanelGeometry::kSafeMargin)
        ? outsideTopX
        : insideTopX;
    const int minX = bounds.left() + MelonPrime::HudEditorPanelGeometry::kSafeMargin;
    const int maxX = bounds.right() - panel->width() + 1 - MelonPrime::HudEditorPanelGeometry::kSafeMargin;
    const int px = std::clamp(preferredX, minX, std::max(minX, maxX));

    const int preferredY = bounds.top() + (bounds.height() - panelH) / 2;
    const int minY = bounds.top() + MelonPrime::HudEditorPanelGeometry::kSafeMargin;
    const int maxY = bounds.bottom() - panelH + 1 - MelonPrime::HudEditorPanelGeometry::kSafeMargin;
    const int py = std::clamp(preferredY, minY, std::max(minY, maxY));
    panel->move(QPoint(px, py));
}

} // namespace MelonPrime::HudScreenEditPanel

#endif // MELONPRIME_CUSTOM_HUD
