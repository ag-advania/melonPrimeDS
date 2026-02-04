#ifndef MELON_PRIME_HOTKEY_VK_BINDING_H
#define MELON_PRIME_HOTKEY_VK_BINDING_H

#ifdef _WIN32
#include <string>
#include <vector>
#include <windows.h>

// 前方宣言: 名前空間 MelonPrime 内のクラス
namespace MelonPrime {
    class RawInputWinFilter;
}

namespace melonDS {

    // QtキーコードをVKコード列に変換
    std::vector<UINT> MapQtKeyIntToVks(int qtKey);

    // 単一のホットキーをバインド
    void BindOneHotkeyFromConfig(MelonPrime::RawInputWinFilter* filter, int instance,
        const std::string& hkPath, int hkId);

    // 全Metroid用ホットキーを一括バインド
    void BindMetroidHotkeysFromConfig(MelonPrime::RawInputWinFilter* filter, int instance);

} // namespace melonDS
#endif // _WIN32
#endif // MELON_PRIME_HOTKEY_VK_BINDING_H