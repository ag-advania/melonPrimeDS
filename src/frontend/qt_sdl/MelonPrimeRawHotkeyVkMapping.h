#ifndef MELON_PRIME_RAW_HOTKEY_VK_MAPPING_H
#define MELON_PRIME_RAW_HOTKEY_VK_MAPPING_H

#ifdef _WIN32

#include <array>
#include <cstddef>
#include <cstdint>
#include <windows.h>

namespace MelonPrime {

// Stack-allocated VK code container used by the binding compiler. The
// mapping is intentionally one VK for normal identities, with capacity for
// future exact mappings that still do not need heap allocation.
struct SmallVkList {
    static constexpr std::size_t kCapacity = 4;
    std::array<UINT, kCapacity> data{};
    std::uint8_t count = 0;

    void push_back(UINT vk) {
        if (count < kCapacity)
            data[count++] = vk;
    }
    [[nodiscard]] bool empty() const { return count == 0; }
    [[nodiscard]] std::size_t size() const { return count; }
    [[nodiscard]] const UINT* begin() const { return data.data(); }
    [[nodiscard]] const UINT* end() const { return data.data() + count; }
};

// Qt key code -> VK code(s), stack-allocated. This small pure mapping unit is
// shared by production binding and deterministic Windows mapping tests.
SmallVkList MapQtKeyIntToVks(int qtKey);

} // namespace MelonPrime

#endif // _WIN32
#endif // MELON_PRIME_RAW_HOTKEY_VK_MAPPING_H
