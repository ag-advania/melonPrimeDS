#ifndef MELON_PRIME_HOTKEY_VK_BINDING_H
#define MELON_PRIME_HOTKEY_VK_BINDING_H

#ifdef _WIN32
#include <string>
#include <vector>
#include <windows.h>

namespace MelonPrime {

    class RawInputWinFilter;

    // QtキーコードをVKコード列に変換
    std::vector<UINT> MapQtKeyIntToVks(int qtKey);

    // 単一のホットキーをバインド
    void BindOneHotkeyFromConfig(RawInputWinFilter* filter, int instance,
        const std::string& hkPath, int hkId);

    // 全Metroid用ホットキーを一括バインド
    void BindMetroidHotkeysFromConfig(RawInputWinFilter* filter, int instance);

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_HOTKEY_VK_BINDING_H
