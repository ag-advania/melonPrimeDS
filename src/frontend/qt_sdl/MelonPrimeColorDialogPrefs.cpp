#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeColorDialogPrefs.h"

#include "Config.h"

#include <QColorDialog>
#include <QtGlobal>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
constexpr const char* kCustomColorsArrayKey = "MelonPrime.ColorDialog.CustomColors";

using ColorSlotList = std::vector<std::string>;

int customColorSlotCount()
{
    return QColorDialog::customCount();
}

bool isValidHexRgbString(const std::string& s)
{
    if (s.size() != 7)
        return false;

    if (s[0] != '#')
        return false;

    for (size_t i = 1; i < s.size(); ++i) {
        const char c = s[i];
        const bool isDigit = (c >= '0' && c <= '9');
        const bool isLowerHex = (c >= 'a' && c <= 'f');
        const bool isUpperHex = (c >= 'A' && c <= 'F');
        if (!isDigit && !isLowerHex && !isUpperHex)
            return false;
    }

    return true;
}

std::string normalizeHexRgbString(const std::string& value)
{
    if (!isValidHexRgbString(value))
        return {};

    const QColor color(QString::fromStdString(value));
    if (!color.isValid())
        return {};

    return color.name(QColor::HexRgb).toStdString();
}

QColorDialog::ColorDialogOptions colorDialogOptions()
{
    QColorDialog::ColorDialogOptions options;

#if defined(Q_OS_MAC)
    // macOS native color dialog ignores QColorDialog::setCustomColor().
    // Use Qt's dialog only on macOS so the persisted custom color slots are visible.
    options |= QColorDialog::DontUseNativeDialog;
#endif

    return options;
}

ColorSlotList readPersistedCustomColors()
{
    Config::Table cfg = Config::GetGlobalTable();
    ColorSlotList palette(static_cast<size_t>(customColorSlotCount()));

    if (!cfg.HasKey(kCustomColorsArrayKey))
        return palette;

    Config::Array colors = cfg.GetArray(kCustomColorsArrayKey);
    const int count = std::min<int>(
        static_cast<int>(colors.Size()),
        customColorSlotCount());

    for (int i = 0; i < count; ++i)
        palette[static_cast<size_t>(i)] = normalizeHexRgbString(colors.GetString(i));

    return palette;
}

ColorSlotList captureCurrentCustomColors()
{
    ColorSlotList palette(static_cast<size_t>(customColorSlotCount()));

    for (int i = 0; i < customColorSlotCount(); ++i) {
        const QColor color = QColorDialog::customColor(i);
        if (!color.isValid())
            continue;

        palette[static_cast<size_t>(i)] = color.name(QColor::HexRgb).toStdString();
    }

    return palette;
}

void applyCustomColorsToDialog(const ColorSlotList& palette)
{
    const int count = std::min<int>(
        static_cast<int>(palette.size()),
        customColorSlotCount());

    for (int i = 0; i < count; ++i) {
        const std::string& value = palette[static_cast<size_t>(i)];
        if (value.empty())
            continue;

        QColorDialog::setCustomColor(i, QColor(QString::fromStdString(value)));
    }
}

void loadPersistedCustomColors()
{
    applyCustomColorsToDialog(readPersistedCustomColors());
}

void writePersistedCustomColors(const ColorSlotList& palette)
{
    Config::Table cfg = Config::GetGlobalTable();
    Config::Array colors = cfg.GetArray(kCustomColorsArrayKey);
    colors.Clear();

    const int count = std::min<int>(
        static_cast<int>(palette.size()),
        customColorSlotCount());

    for (int i = 0; i < count; ++i) {
        const std::string& value = palette[static_cast<size_t>(i)];
        if (value.empty())
            continue;

        colors.SetString(i, value);
    }
}

void persistCurrentCustomColorsIfChanged()
{
    const ColorSlotList before = readPersistedCustomColors();
    const ColorSlotList current = captureCurrentCustomColors();

    if (current == before)
        return;

    writePersistedCustomColors(current);
    Config::Save();
}

} // namespace

namespace MelonPrime::ColorDialogPrefs
{

QColor getColor(QWidget* parent, const QColor& initial, const QString& title)
{
    loadPersistedCustomColors();

    const QColor picked = QColorDialog::getColor(
        initial,
        parent,
        title,
        colorDialogOptions());

    if (picked.isValid())
        persistCurrentCustomColorsIfChanged();

    return picked;
}

} // namespace MelonPrime::ColorDialogPrefs

#endif // MELONPRIME_CUSTOM_HUD
