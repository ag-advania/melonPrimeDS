#ifndef MELONPRIME_QT_KEY_BINDING_H
#define MELONPRIME_QT_KEY_BINDING_H

#include <cstdint>

#include <QKeyEvent>

[[nodiscard]] inline bool IsQtModifierKey(int key) noexcept
{
    return key == Qt::Key_Control
        || key == Qt::Key_Alt
        || key == Qt::Key_AltGr
        || key == Qt::Key_Shift
        || key == Qt::Key_Meta;
}

// Qt has no cross-platform left/right modifier flag, so retain the native
// scan-code convention used by the persisted binding format.
[[nodiscard]] inline bool IsRightQtModifierKey(
    const QKeyEvent& event) noexcept
{
#ifdef __WIN32__
    const quint32 scan = event.nativeScanCode();
    return scan == 0x11D || scan == 0x138 || scan == 0x36;
#elif __APPLE__
    const quint32 scan = event.nativeVirtualKey();
    return scan == 0x36 || scan == 0x3C || scan == 0x3D || scan == 0x3E;
#else
    const quint32 scan = event.nativeScanCode();
    return scan == 0x69 || scan == 0x6C || scan == 0x3E;
#endif
}

// Canonical persisted Qt keyboard identity shared by the binding editor and
// runtime producers. This is a pure integer projection with no QObject state.
[[nodiscard]] inline int NormalizeQtKeyBinding(
    const QKeyEvent& event) noexcept
{
    int key = event.key();
    if (!IsQtModifierKey(key))
        key |= static_cast<int>(event.modifiers());
    else if (IsRightQtModifierKey(event))
        key |= static_cast<int>(0x80000000u);
    return key;
}

[[nodiscard]] inline bool QtKeyBindingMatchesRelease(
    int binding, int normalized, int baseKey, bool isModifier) noexcept
{
    if (binding == normalized)
        return true;
    if (isModifier
        || (static_cast<std::uint32_t>(binding) & 0xF0000000u)
            == 0x10000000u) {
        return false;
    }

    constexpr int kQtModifierBits = static_cast<int>(Qt::ShiftModifier)
        | static_cast<int>(Qt::ControlModifier)
        | static_cast<int>(Qt::AltModifier)
        | static_cast<int>(Qt::MetaModifier)
        | static_cast<int>(Qt::KeypadModifier)
        | static_cast<int>(Qt::GroupSwitchModifier);
    // A modifier may be released before this physical key. Match the base key
    // as well so a Ctrl+K-style binding cannot remain held.
    return (binding & ~kQtModifierBits
        & ~static_cast<int>(0x80000000u)) == baseKey;
}

#endif // MELONPRIME_QT_KEY_BINDING_H
