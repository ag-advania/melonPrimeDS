#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeColorDialogPrefs.h"

#include "Config.h"

#include <QColor>
#include <QColorDialog>
#include <QString>

#include <algorithm>

namespace
{
constexpr int kMaxCustomColors = 16;
constexpr const char* kCustomColorsArrayKey = "MelonPrime.ColorDialog.CustomColors";

bool isValidCustomColorString(const std::string& s)
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

} // namespace

namespace MelonPrime::ColorDialogPrefs
{

void loadCustomColors()
{
    Config::Table cfg = Config::GetGlobalTable();
    Config::Array colors = cfg.GetArray(kCustomColorsArrayKey);

    const int count = static_cast<int>(std::min<size_t>(colors.Size(), kMaxCustomColors));
    for (int i = 0; i < count; ++i) {
        const std::string value = colors.GetString(i);
        if (!isValidCustomColorString(value))
            continue;

        const QColor color(QString::fromStdString(value));
        if (!color.isValid())
            continue;

        QColorDialog::setCustomColor(i, color);
    }
}

void saveCustomColors()
{
    Config::Table cfg = Config::GetGlobalTable();
    Config::Array colors = cfg.GetArray(kCustomColorsArrayKey);
    colors.Clear();

    for (int i = 0; i < kMaxCustomColors; ++i) {
        const QColor color = QColorDialog::customColor(i);
        if (!color.isValid())
            continue;

        colors.SetString(i, color.name(QColor::HexRgb).toStdString());
    }

    Config::Save();
}

QColor getColor(QWidget* parent, const QColor& initial, const QString& title)
{
    loadCustomColors();

    const QColor picked = QColorDialog::getColor(
        initial,
        parent,
        title,
        colorDialogOptions());

    if (picked.isValid())
        saveCustomColors();

    return picked;
}

} // namespace MelonPrime::ColorDialogPrefs

#endif // MELONPRIME_CUSTOM_HUD
