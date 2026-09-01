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

    bool push_back(UINT vk) noexcept {
        // Never expose a partial mapping. If a future exact mapping needs
        // more than the fixed stack capacity, make the whole binding empty so
        // the caller selects the canonical Qt fallback path.
        if (overflowedFlag || count >= kCapacity) {
            overflowedFlag = true;
            count = 0;
            return false;
        }
        data[count++] = vk;
        return true;
    }
    [[nodiscard]] bool empty() const noexcept { return count == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return count; }
    [[nodiscard]] bool overflowed() const noexcept { return overflowedFlag; }
    [[nodiscard]] const UINT* begin() const noexcept { return data.data(); }
    [[nodiscard]] const UINT* end() const noexcept { return data.data() + count; }

private:
    bool overflowedFlag = false;
};

// Qt key code -> VK code(s), stack-allocated. This small pure mapping unit is
// shared by production binding and deterministic Windows mapping tests.
SmallVkList MapQtKeyIntToVks(int qtKey);

} // namespace MelonPrime

#endif // _WIN32
#endif // MELON_PRIME_RAW_HOTKEY_VK_MAPPING_H
