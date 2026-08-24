#include "MelonPrimeHudEditorFormLayout.h"

#include <algorithm>
#include <QLabel>
#include <QLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyle>
#include <QWidget>

namespace MelonPrime::HudEditorForm {

bool UsesNativeWindowsChrome(const QWidget& widget)
{
    const QStyle* style = widget.style();
    if (!style)
        return false;
    const QString name = style->objectName();
    return name.compare(QStringLiteral("windowsvista"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("windows11"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("windows"), Qt::CaseInsensitive) == 0;
}

void ConfigureWrappedFormLabel(QLabel& label)
{
    label.setWordWrap(true);
    label.setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label.setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    label.setMinimumWidth(0);
}

QLabel* CreateTranslatedFormLabel(QFormLayout& form, const QString& text)
{
    auto* label = new QLabel(text, form.parentWidget());
    ConfigureWrappedFormLabel(*label);
    return label;
}

void ConstrainWrappedFormLabels(QWidget& root, int maximumWidth)
{
    const int width = maximumWidth > 0 ? maximumWidth : 1;
    // Reserve a generic share of the row for its field. Short labels keep
    // their natural width; long labels get a bounded width early enough for
    // QFormLayout::WrapLongRows to move the field below them.
    const int labelWidth = std::max(1, (width * 2) / 3);
    for (QLabel* label : root.findChildren<QLabel*>())
    {
        label->setMaximumWidth(labelWidth);
        label->setMinimumHeight(0);
        const int wrappedHeight = label->heightForWidth(labelWidth);
        if (wrappedHeight > 0)
            label->setMinimumHeight(wrappedHeight);
        label->updateGeometry();
    }

    // QScrollArea can have measured the child before its vertical scrollbar
    // appeared. Pull the child back to the current viewport so the next form
    // activation cannot retain a one-scrollbar-width horizontal overhang.
    if (auto* scroll = root.findChild<QScrollArea*>())
    {
        if (auto* widget = scroll->widget())
        {
            const int viewportWidth = scroll->viewport()->width();
            const int targetWidth = viewportWidth > 0
                ? std::min(width, viewportWidth)
                : width;
            if (widget->width() > targetWidth)
                widget->resize(targetWidth, widget->height());
        }
    }
}

int ReservedVerticalScrollBarWidth(const QWidget& root)
{
    const QScrollArea* scroll = root.findChild<QScrollArea*>();
    if (!scroll)
        return 0;
    const Qt::ScrollBarPolicy policy = scroll->verticalScrollBarPolicy();
    // AlwaysOff never takes width; AlwaysOn is already inside sizeHint().
    if (policy != Qt::ScrollBarAsNeeded)
        return 0;
    const QScrollBar* bar = scroll->verticalScrollBar();
    if (!bar)
        return 0;
    return std::max(0, bar->sizeHint().width());
}

void ConstrainPanelWidth(QWidget& root, int targetWidth, int passes)
{
    const int width = targetWidth > 0 ? targetWidth : 1;
    QScrollArea* scroll = root.findChild<QScrollArea*>();
    for (int pass = 0; pass < passes; ++pass)
    {
        // The viewport can still be wider than the target while the panel is
        // pinned open by a latched minimum. Bounding the labels against that
        // transient width would keep them too wide to ever let the panel back
        // down, so the target width always wins.
        int constrainWidth = width;
        if (scroll)
        {
            const int viewportWidth = scroll->viewport()->width();
            if (viewportWidth > 0)
                constrainWidth = std::min(width, viewportWidth);
        }
        ConstrainWrappedFormLabels(root, constrainWidth);

        root.setMinimumWidth(0);
        if (QLayout* layout = root.layout())
            layout->activate();
        if (root.width() != width)
            root.resize(width, root.height());
        if (QLayout* layout = root.layout())
            layout->activate();
    }
}

} // namespace MelonPrime::HudEditorForm
