#ifndef MELON_PRIME_HOTKEY_VK_BINDING_H
#define MELON_PRIME_HOTKEY_VK_BINDING_H

#ifdef _WIN32
#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include <windows.h>

namespace MelonPrime {

    class RawInputWinFilter;

    // =========================================================================
    // SmallVkList: stack-allocated VK code container (max 4 entries).
    //
    // Replaces std::vector<UINT> in the hotkey binding path.
    // Most Qt key mappings produce 1-2 VK codes (e.g. VK_LSHIFT + VK_RSHIFT).
    // The maximum observed is 2 (Shift/Ctrl/Alt produce L+R variants).
    //
    // Eliminates 28 heap allocations per BindMetroidHotkeysFromConfig() call
    // (one per hotkey binding). At unpause + config-change time this runs
    // on the emu thread, so avoiding allocator contention is beneficial.
    // =========================================================================
    struct SmallVkList {
        static constexpr size_t kCapacity = 4;
        std::array<UINT, kCapacity> data{};
        uint8_t count = 0;

        void push_back(UINT vk) {
            if (count < kCapacity) data[count++] = vk;
        }
        [[nodiscard]] bool empty() const { return count == 0; }
        [[nodiscard]] size_t size() const { return count; }
        [[nodiscard]] const UINT* begin() const { return data.data(); }
        [[nodiscard]] const UINT* end() const { return data.data() + count; }
    };

    // Qt key code -> VK code(s), stack-allocated
    SmallVkList MapQtKeyIntToVks(int qtKey);

    // Single hotkey bind
    void BindOneHotkeyFromConfig(RawInputWinFilter* filter, int instance,
        const std::string& hkPath, int hkId);

    // Batch bind all Metroid hotkeys
    void BindMetroidHotkeysFromConfig(RawInputWinFilter* filter, int instance);

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_HOTKEY_VK_BINDING_H
