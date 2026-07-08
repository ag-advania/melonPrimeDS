#pragma once

#ifdef MELONPRIME_CUSTOM_HUD

#include <QColor>
#include <QColorDialog>
#include <QString>
#include <QWidget>

namespace MelonPrime::ColorDialogPrefs
{
    QColor getColor(QWidget* parent, const QColor& initial, const QString& title);
    void loadCustomColors();
    void saveCustomColors();
}

#endif // MELONPRIME_CUSTOM_HUD
