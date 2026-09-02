#include "MelonPrimeRawHotkeyVkBinding.h"
#include "MelonPrimeQtKeyBinding.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>

namespace {

int RightModifierIdentity(int qtKey) noexcept
{
    return static_cast<int>(
        static_cast<std::uint32_t>(qtKey) | 0x80000000u);
}

bool ExpectExact(
    const char* label,
    const MelonPrime::SmallVkList& actual,
    std::initializer_list<UINT> expected)
{
    if (actual.size() != expected.size()) {
        std::cerr << label << ": expected " << expected.size()
                  << " VKs, got " << actual.size() << '\n';
        return false;
    }

    size_t index = 0;
    for (const UINT vk : expected) {
        if (actual.data[index] != vk) {
            std::cerr << label << ": VK mismatch at " << index
                      << ", expected " << vk << ", got "
                      << actual.data[index] << '\n';
            return false;
        }
        ++index;
    }
    return true;
}

} // namespace

int main()
{
    using MelonPrime::MapQtKeyIntToVks;

    // These are the canonical persisted identities produced by the Qt
    // binding path. Plain modifiers are the left/normal identity; the high
    // marker is the explicit right-side identity.
    if (!ExpectExact("plain Shift", MapQtKeyIntToVks(Qt::Key_Shift),
            { VK_LSHIFT })) return 1;
    if (!ExpectExact("right Shift",
            MapQtKeyIntToVks(RightModifierIdentity(Qt::Key_Shift)),
            { VK_RSHIFT })) return 1;
    if (!ExpectExact("plain Control", MapQtKeyIntToVks(Qt::Key_Control),
            { VK_LCONTROL })) return 1;
    if (!ExpectExact("right Control",
            MapQtKeyIntToVks(RightModifierIdentity(Qt::Key_Control)),
            { VK_RCONTROL })) return 1;
    if (!ExpectExact("plain Alt", MapQtKeyIntToVks(Qt::Key_Alt),
            { VK_LMENU })) return 1;
    if (!ExpectExact("right Alt",
            MapQtKeyIntToVks(RightModifierIdentity(Qt::Key_Alt)),
            { VK_RMENU })) return 1;

    // A modifier chord is not an OR-list of independent VK predicates.
    const int shiftK = static_cast<int>(Qt::Key_K)
        | static_cast<int>(Qt::ShiftModifier);
    if (!ExpectExact("Shift+K fallback", MapQtKeyIntToVks(shiftK), {}))
        return 1;

    if (!ExpectExact("F24", MapQtKeyIntToVks(Qt::Key_F24), { VK_F24 }))
        return 1;
    if (!ExpectExact("F25 fallback", MapQtKeyIntToVks(Qt::Key_F25), {}))
        return 1;

    MelonPrime::SmallVkList overflow;
    for (std::size_t i = 0; i <= MelonPrime::SmallVkList::kCapacity; ++i)
        (void)overflow.push_back(VK_F1 + static_cast<UINT>(i));
    if (!overflow.overflowed() || !overflow.empty()) {
        std::cerr << "SmallVkList overflow must select whole-mapping fallback\n";
        return 1;
    }

    std::cout << "raw-hotkey-vk-mapping-tests: PASS\n";
    return 0;
}
