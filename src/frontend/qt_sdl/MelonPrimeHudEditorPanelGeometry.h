#pragma once

namespace MelonPrime::HudEditorPanelGeometry {

inline constexpr int kSafeMargin = 4;
inline constexpr int kPreferredWidth = 300;

constexpr int UsableWidth(int windowWidth) noexcept
{
    const int width = windowWidth - (2 * kSafeMargin);
    return width > 0 ? width : 1;
}

constexpr int FinalWidth(int windowWidth, int naturalWidth) noexcept
{
    const int desiredWidth = naturalWidth > kPreferredWidth
        ? naturalWidth
        : kPreferredWidth;
    const int usableWidth = UsableWidth(windowWidth);
    return desiredWidth < usableWidth ? desiredWidth : usableWidth;
}

} // namespace MelonPrime::HudEditorPanelGeometry
