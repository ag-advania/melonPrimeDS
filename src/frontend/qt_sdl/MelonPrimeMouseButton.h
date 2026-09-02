#ifndef MELONPRIME_MOUSE_BUTTON_H
#define MELONPRIME_MOUSE_BUTTON_H

#ifdef MELONPRIME_DS
#include <QMouseEvent>
#include <array>
#include <cstddef>

namespace MelonPrime {

// Keep this list identical to the runtime mouse-button mask array. Qt can
// describe more ExtraButton values, but Raw Input and the runtime binding
// masks intentionally support only these five physical buttons.
inline constexpr std::array<Qt::MouseButton, 5> kSupportedMouseButtons = {
    Qt::LeftButton,
    Qt::RightButton,
    Qt::MiddleButton,
    Qt::XButton1,
    Qt::XButton2,
};

inline constexpr std::array<const char*, 5> kSupportedMouseButtonNames = {
    "LeftButton",
    "RightButton",
    "MiddleButton",
    "BackButton",
    "ForwardButton",
};

inline constexpr std::size_t kSupportedMouseButtonCount =
    kSupportedMouseButtons.size();

static_assert(kSupportedMouseButtonCount == kSupportedMouseButtonNames.size());

[[nodiscard]] constexpr int MouseButtonIndex(Qt::MouseButton button) noexcept
{
    for (std::size_t i = 0; i < kSupportedMouseButtonCount; ++i) {
        if (kSupportedMouseButtons[i] == button)
            return static_cast<int>(i);
    }
    return -1;
}

[[nodiscard]] constexpr bool IsSupportedMouseButton(
    Qt::MouseButton button) noexcept
{
    return MouseButtonIndex(button) >= 0;
}

[[nodiscard]] constexpr const char* MouseButtonName(
    Qt::MouseButton button) noexcept
{
    const int index = MouseButtonIndex(button);
    return index >= 0 ? kSupportedMouseButtonNames[static_cast<std::size_t>(index)]
                      : nullptr;
}

} // namespace MelonPrime
#endif // MELONPRIME_DS

#endif // MELONPRIME_MOUSE_BUTTON_H
