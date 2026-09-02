#include "MelonPrimeRawHotkeyVkMapping.h"

#if defined(_WIN32)

#include "MelonPrimeDef.h"
#include "MelonPrimeQtKeyBinding.h"

namespace MelonPrime {

namespace {

constexpr int kQtKey_Shift = 0x01000020;
constexpr int kQtKey_Control = 0x01000021;
constexpr int kQtKey_Alt = 0x01000023;
constexpr int kQtKey_Tab = 0x01000001;
constexpr int kQtKey_PageUp = 0x01000016;
constexpr int kQtKey_PageDown = 0x01000017;
constexpr int kQtKey_Space = 0x20;
constexpr int kQtKey_F1 = 0x01000030;
constexpr int kQtKey_F24 = kQtKey_F1 + 23;
constexpr std::uint32_t kQtRightModifierMarker = 0x80000000u;
constexpr std::uint32_t kQtBindingModifierMask =
    static_cast<std::uint32_t>(Qt::ShiftModifier)
    | static_cast<std::uint32_t>(Qt::ControlModifier)
    | static_cast<std::uint32_t>(Qt::AltModifier)
    | static_cast<std::uint32_t>(Qt::MetaModifier)
    | static_cast<std::uint32_t>(Qt::KeypadModifier)
    | static_cast<std::uint32_t>(Qt::GroupSwitchModifier);

static inline bool TryAppendSpecialKey(int qt, SmallVkList& out) {
    switch (qt) {
    // The unmarked Qt modifier identity is the left/normal modifier. The
    // persisted right-side marker is handled separately below, so never
    // widen this mapping to both sides.
    case kQtKey_Shift:    out.push_back(VK_LSHIFT);   return true;
    case kQtKey_Control:  out.push_back(VK_LCONTROL); return true;
    case kQtKey_Alt:      out.push_back(VK_LMENU);    return true;
    case kQtKey_Tab:      out.push_back(VK_TAB);      return true;
    case kQtKey_PageUp:   out.push_back(VK_PRIOR);    return true;
    case kQtKey_PageDown: out.push_back(VK_NEXT);     return true;
    case kQtKey_Space:    out.push_back(VK_SPACE);    return true;
    default: return false;
    }
}

static inline bool TryAppendRightModifier(int qt, SmallVkList& out) {
    switch (qt) {
    case kQtKey_Shift:   out.push_back(VK_RSHIFT);   return true;
    case kQtKey_Control: out.push_back(VK_RCONTROL); return true;
    case kQtKey_Alt:     out.push_back(VK_RMENU);    return true;
    default: return false;
    }
}

} // namespace

SmallVkList MapQtKeyIntToVks(int qtKey) {
    SmallVkList vks;

    // Mouse wheel virtual keys have no Win32 VK; runtime injects one-frame
    // presses from the Qt wheel delta (see MelonPrimeGameInput).
    if (qtKey == InputKey::MouseWheelUp || qtKey == InputKey::MouseWheelDown)
        return vks;

    const std::uint32_t encoded = static_cast<std::uint32_t>(qtKey);

    // Mouse Buttons. The persisted mouse mark is intentionally checked
    // before the right-modifier marker because both occupy high bits.
    if ((encoded & 0xF0000000u)
        == static_cast<std::uint32_t>(InputKey::MouseMark)) {
        const int btn = static_cast<int>(
            encoded & ~static_cast<std::uint32_t>(InputKey::MouseMark));
        switch (btn) {
        case 0x01: vks.push_back(VK_LBUTTON);  break;
        case 0x02: vks.push_back(VK_RBUTTON);  break;
        case 0x04: vks.push_back(VK_MBUTTON);  break;
        case 0x08: vks.push_back(VK_XBUTTON1); break;
        case 0x10: vks.push_back(VK_XBUTTON2); break;
        }
        return vks;
    }

    const bool rightModifier = (encoded & kQtRightModifierMarker) != 0;
    const int baseKey = static_cast<int>(
        encoded & ~kQtRightModifierMarker);
    if (rightModifier)
        return TryAppendRightModifier(baseKey, vks) ? vks : SmallVkList{};

    // InputState's hotkey representation is an OR of the listed VKs. A
    // canonical Ctrl+K/Shift+K/etc. identity therefore cannot be mapped
    // exactly without introducing a new predicate evaluator. Returning an
    // empty list deliberately selects the Qt fallback classification.
    if ((encoded & kQtBindingModifierMask) != 0)
        return vks;

    if (TryAppendSpecialKey(baseKey, vks))
        return vks;

    // ASCII Alphanumeric (0-9, A-Z)
    if ((baseKey >= '0' && baseKey <= '9')
        || (baseKey >= 'A' && baseKey <= 'Z')) {
        vks.push_back(static_cast<UINT>(baseKey));
        return vks;
    }

    // F-Keys (F1 - F24). Win32 has no contiguous F25+ VK range; never
    // synthesize a reserved/NumLock alias by arithmetic beyond VK_F24.
    if (baseKey >= kQtKey_F1 && baseKey <= kQtKey_F24) {
        const int idx = (baseKey - kQtKey_F1);
        vks.push_back(static_cast<UINT>(VK_F1 + idx));
        return vks;
    }

    return vks;
}

} // namespace MelonPrime

#endif // _WIN32
