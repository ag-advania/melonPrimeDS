#pragma once

#ifdef MELONPRIME_CUSTOM_HUD

#include <QColor>
#include <QString>

class QWidget;

namespace MelonPrime::ColorDialogPrefs
{
    QColor getColor(QWidget* parent, const QColor& initial, const QString& title);
}

#endif // MELONPRIME_CUSTOM_HUD
