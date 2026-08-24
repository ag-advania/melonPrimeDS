#include "MelonPrimeHudEditorFormLayout.h"

#include <algorithm>
#include <QLabel>
#include <QScrollArea>
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

} // namespace MelonPrime::HudEditorForm
