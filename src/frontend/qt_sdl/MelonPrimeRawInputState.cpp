#ifdef _WIN32
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"
#include <cstring>
#include <algorithm>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#ifndef QWORD
typedef unsigned __int64 QWORD;
#endif

namespace MelonPrime {

    std::array<InputState::BtnLutEntry, 1024> InputState::s_btnLut;
    std::array<uint64_t, 32> InputState::s_btnToVkMask;
    std::array<InputState::VkRemapEntry, 256> InputState::s_vkRemap;
    uint16_t InputState::s_scancodeLShift = 0;
    uint16_t InputState::s_scancodeRShift = 0;
    std::once_flag InputState::s_initFlag;
    NtUserGetRawInputBuffer_t InputState::s_fnBestGetRawInputBuffer = ::GetRawInputBuffer;

    InputState::InputState() noexcept {
        for (auto& vk : m_vkDown) {
            vk.store(0, std::memory_order_relaxed);
        }
        m_mouseDeltaX.store(0, std::memory_order_relaxed);
        m_mouseDeltaY.store(0, std::memory_order_relaxed);

        std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));
        std::fill(std::begin(m_hkPrev), std::end(m_hkPrev), 0);
        m_boundHotkeys[0] = 0;
        m_boundHotkeys[1] = 0;
    }

    void InputState::InitializeTables() noexcept {
        std::call_once(s_initFlag, []() {
            // 1. NT APIの解決
            WinInternal::ResolveNtApis();

            // 2. 最適な GetRawInputBuffer を選択
            if (WinInternal::fnNtUserGetRawInputBuffer) {
                s_fnBestGetRawInputBuffer = WinInternal::fnNtUserGetRawInputBuffer;
            }

            // 3. ボタンフラグ → down/up ビットマップ LUT
            for (int i = 0; i < 1024; ++i) {
                uint8_t d = 0, u = 0;
                if (i & RI_MOUSE_BUTTON_1_DOWN) d |= 0x01;
                if (i & RI_MOUSE_BUTTON_1_UP)   u |= 0x01;
                if (i & RI_MOUSE_BUTTON_2_DOWN) d |= 0x02;
                if (i & RI_MOUSE_BUTTON_2_UP)   u |= 0x02;
                if (i & RI_MOUSE_BUTTON_3_DOWN) d |= 0x04;
                if (i & RI_MOUSE_BUTTON_3_UP)   u |= 0x04;
                if (i & RI_MOUSE_BUTTON_4_DOWN) d |= 0x08;
                if (i & RI_MOUSE_BUTTON_4_UP)   u |= 0x08;
                if (i & RI_MOUSE_BUTTON_5_DOWN) d |= 0x10;
                if (i & RI_MOUSE_BUTTON_5_UP)   u |= 0x10;
                s_btnLut[i] = { d, u };
            }

            // 4. ボタンビット(0..4) → m_vkDown[0] 用マスク変換テーブル
            //    downBits/upBits は 5bit (0x00..0x1F) なので 32エントリで十分
            for (uint32_t bits = 0; bits < 32; ++bits) {
                uint64_t mask = 0;
                for (int b = 0; b < 5; ++b) {
                    if (bits & (1u << b)) {
                        mask |= (1ULL << kBtnBitToVk[b]);
                    }
                }
                s_btnToVkMask[bits] = mask;
            }

            // 5. VK リマップテーブル
            std::fill(s_vkRemap.begin(), s_vkRemap.end(), VkRemapEntry{ 0, 0 });
            s_vkRemap[VK_CONTROL] = { VK_LCONTROL, VK_RCONTROL };
            s_vkRemap[VK_MENU] = { VK_LMENU,    VK_RMENU };
            s_vkRemap[VK_SHIFT] = { VK_LSHIFT,   VK_RSHIFT };

            s_scancodeLShift = static_cast<uint16_t>(MapVirtualKeyW(VK_LSHIFT, MAPVK_VK_TO_VSC));
            s_scancodeRShift = static_cast<uint16_t>(MapVirtualKeyW(VK_RSHIFT, MAPVK_VK_TO_VSC));
            });
    }

    // =========================================================================
    // Joy2Key ON時: 単発 WM_INPUT 処理
    // =========================================================================
    void InputState::processRawInput(HRAWINPUT hRaw) noexcept {
        alignas(16) uint8_t rawBuf[sizeof(RAWINPUT)];
        UINT size = sizeof(rawBuf);
        UINT result;

        if (LIKELY(WinInternal::fnNtUserGetRawInputData != nullptr)) {
            result = WinInternal::fnNtUserGetRawInputData(hRaw, RID_INPUT, rawBuf, &size, sizeof(RAWINPUTHEADER));
        }
        else {
            result = GetRawInputData(hRaw, RID_INPUT, rawBuf, &size, sizeof(RAWINPUTHEADER));
        }

        if (UNLIKELY(result == UINT(-1) || result == 0)) return;
        const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(rawBuf);

        switch (raw->header.dwType) {
        case RIM_TYPEMOUSE: {
            const RAWMOUSE& m = raw->data.mouse;

            // ★ 改善: fetch_add — CASループ完全排除
            //   x86: LOCK XADD 1命令。再試行なし。
            if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                if (m.lLastX) m_mouseDeltaX.fetch_add(static_cast<int32_t>(m.lLastX), std::memory_order_relaxed);
                if (m.lLastY) m_mouseDeltaY.fetch_add(static_cast<int32_t>(m.lLastY), std::memory_order_relaxed);
            }

            // ★ 改善: マウスボタン → m_vkDown[0] に統合
            //   s_btnToVkMask[] で LUT ビット→VKビットを一発変換
            const USHORT flags = m.usButtonFlags & 0x03FF;
            if (flags) {
                const auto& lut = s_btnLut[flags];
                const uint64_t downMask = s_btnToVkMask[lut.downBits];
                const uint64_t upMask = s_btnToVkMask[lut.upBits];
                if (downMask | upMask) {
                    uint64_t cur = m_vkDown[0].load(std::memory_order_relaxed);
                    uint64_t nxt;
                    do {
                        nxt = (cur | downMask) & ~upMask;
                    } while (cur != nxt && UNLIKELY(!m_vkDown[0].compare_exchange_weak(
                        cur, nxt, std::memory_order_release, std::memory_order_relaxed)));
                }
            }
            break;
        }
        case RIM_TYPEKEYBOARD: {
            const RAWKEYBOARD& kb = raw->data.keyboard;
            UINT vk = kb.VKey;

            if (UNLIKELY(vk == 0)) {
                vk = MapVirtualKeyW(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
            }

            if (LIKELY(vk > 0 && vk < 255)) {
                vk = remapVk(vk, kb.MakeCode, kb.Flags);
                const bool isDown = !(kb.Flags & RI_KEY_BREAK);
                setVkBit(vk, isDown);
            }
            break;
        }
        }
    }

    // =========================================================================
    // Joy2Key OFF時: バッチ処理 (ワーカースレッド)
    // =========================================================================
    void InputState::processRawInputBatched() noexcept {
        alignas(16) static thread_local uint8_t buffer[16384];

        // ローカルアキュムレータ
        int32_t localAccX = 0;
        int32_t localAccY = 0;

        // キーボード + マウスボタンの状態変化マスク (統合)
        uint64_t localKeyDeltaDown[4] = { 0 };
        uint64_t localKeyDeltaUp[4] = { 0 };
        bool hasMouseDelta = false;
        bool hasKeyChanges = false;

        for (;;) {
            UINT size = sizeof(buffer);
            UINT count = s_fnBestGetRawInputBuffer(reinterpret_cast<PRAWINPUT>(buffer), &size, sizeof(RAWINPUTHEADER));
            if (count == 0 || count == UINT(-1)) break;

            const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(buffer);

            for (UINT i = 0; i < count; ++i) {
                if (raw->header.dwType == RIM_TYPEMOUSE) {
                    const RAWMOUSE& m = raw->data.mouse;

                    if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                        localAccX = static_cast<int32_t>(static_cast<uint32_t>(localAccX) + static_cast<uint32_t>(m.lLastX));
                        localAccY = static_cast<int32_t>(static_cast<uint32_t>(localAccY) + static_cast<uint32_t>(m.lLastY));
                        hasMouseDelta = true;
                    }

                    // ★ 改善: ボタン変化も localKeyDelta に統合
                    const USHORT flags = m.usButtonFlags & 0x03FF;
                    if (flags) {
                        const auto& lut = s_btnLut[flags];
                        const uint64_t downMask = s_btnToVkMask[lut.downBits];
                        const uint64_t upMask = s_btnToVkMask[lut.upBits];
                        // ボタンの down/up をキーボードと同じデルタマスクに蓄積
                        localKeyDeltaDown[0] |= downMask;
                        localKeyDeltaDown[0] &= ~upMask;   // up が来たら down 取り消し
                        localKeyDeltaUp[0] |= upMask;
                        localKeyDeltaUp[0] &= ~downMask;   // down が来たら up 取り消し
                        hasKeyChanges = true;
                    }
                }
                else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                    const RAWKEYBOARD& kb = raw->data.keyboard;
                    UINT vk = kb.VKey;

                    if (UNLIKELY(vk == 0)) {
                        vk = MapVirtualKeyW(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
                    }

                    if (LIKELY(vk > 0 && vk < 255)) {
                        vk = remapVk(vk, kb.MakeCode, kb.Flags);
                        const int idx = vk >> 6;
                        const uint64_t bit = 1ULL << (vk & 63);

                        if (!(kb.Flags & RI_KEY_BREAK)) {
                            localKeyDeltaDown[idx] |= bit;
                            localKeyDeltaUp[idx] &= ~bit;
                        }
                        else {
                            localKeyDeltaUp[idx] |= bit;
                            localKeyDeltaDown[idx] &= ~bit;
                        }
                        hasKeyChanges = true;
                    }
                }
                raw = NEXTRAWINPUTBLOCK(raw);
            }
        }

        // --- Batch Commit ---

        // 1. Mouse Delta: fetch_add (CASループ不要)
        if (hasMouseDelta) {
            m_mouseDeltaX.fetch_add(localAccX, std::memory_order_relaxed);
            m_mouseDeltaY.fetch_add(localAccY, std::memory_order_release);
        }

        // 2. Keyboard + Mouse Buttons: 統合コミット
        if (hasKeyChanges) {
            for (int i = 0; i < 4; ++i) {
                if (localKeyDeltaDown[i] | localKeyDeltaUp[i]) {
                    uint64_t cur = m_vkDown[i].load(std::memory_order_relaxed);
                    uint64_t nxt;
                    do {
                        nxt = (cur | localKeyDeltaDown[i]) & ~localKeyDeltaUp[i];
                    } while (cur != nxt && UNLIKELY(!m_vkDown[i].compare_exchange_weak(
                        cur, nxt, std::memory_order_release, std::memory_order_relaxed)));
                }
            }
        }
    }

    // =========================================================================
    // マウスデルタ取得: exchange 2回 (CASループなし)
    // =========================================================================
    void InputState::fetchMouseDelta(int& outX, int& outY) noexcept {
        outX = m_mouseDeltaX.exchange(0, std::memory_order_acquire);
        outY = m_mouseDeltaY.exchange(0, std::memory_order_acquire);
    }

    void InputState::discardDeltas() noexcept {
        m_mouseDeltaX.store(0, std::memory_order_relaxed);
        m_mouseDeltaY.store(0, std::memory_order_relaxed);
    }

    void InputState::resetAllKeys() noexcept {
        for (auto& vk : m_vkDown) {
            vk.store(0, std::memory_order_release);
        }
        std::fill(std::begin(m_hkPrev), std::end(m_hkPrev), 0);
    }

    // ★ 改善: m_vkDown[0] 内のマウスボタンVKビットのみクリア
    void InputState::resetMouseButtons() noexcept {
        m_vkDown[0].fetch_and(~kMouseVkBitMask, std::memory_order_release);
    }

    // =========================================================================
    // ホットキー設定: マウスボタンも vkMask[0] に統合
    // =========================================================================
    void InputState::setHotkeyVks(int id, const std::vector<UINT>& vks) {
        if (UNLIKELY(id < 0 || static_cast<size_t>(id) >= kMaxHotkeyId)) return;

        HotkeyMask& mask = m_hkMask[id];
        std::memset(&mask, 0, sizeof(HotkeyMask));

        const int bword = id >> 6;
        const uint64_t bbit = 1ULL << (id & 63);

        if (vks.empty()) {
            m_boundHotkeys[bword] &= ~bbit;
            return;
        }

        // マウスボタンVKもキーボードVKも全て vkMask に統合
        for (const UINT vk : vks) {
            if (vk < 256) {
                mask.vkMask[vk >> 6] |= (1ULL << (vk & 63));
            }
        }
        mask.hasMask = true;
        m_boundHotkeys[bword] |= bbit;
    }

    // =========================================================================
    // ホットキーポーリング
    // ★ 改善: testHotkeyMask から mouse 分岐排除、m_hkPrev ブランチレス更新
    // =========================================================================
    void InputState::pollHotkeys(FrameHotkeyState& out) noexcept {
        uint64_t snapVk[4];
        for (int i = 0; i < 4; ++i)
            snapVk[i] = m_vkDown[i].load(std::memory_order_acquire);

        out = {};

        for (int w = 0; w < 2; ++w) {
            uint64_t bound = m_boundHotkeys[w];
            uint64_t newDown = 0;
            uint64_t newPressed = 0;

            while (bound) {
#if defined(_MSC_VER) && !defined(__clang__)
                unsigned long bitPos;
                _BitScanForward64(&bitPos, bound);
#else
                const int bitPos = __builtin_ctzll(bound);
#endif
                const int id = (w << 6) | static_cast<int>(bitPos);
                const uint64_t hbit = 1ULL << bitPos;
                const HotkeyMask& mask = m_hkMask[id];

                const bool isDown = testHotkeyMask(mask, snapVk);

                if (isDown) newDown |= hbit;

                // ★ ブランチレス edge detection + m_hkPrev 更新
                const bool wasDown = (m_hkPrev[w] & hbit) != 0;
                if (isDown && !wasDown) newPressed |= hbit;
                // m_hkPrev を常に最新に (同一キャッシュラインなのでコスト無視可)
                m_hkPrev[w] = (m_hkPrev[w] & ~hbit) | (isDown ? hbit : 0ULL);

                bound &= bound - 1;
            }
            out.down[w] = newDown;
            out.pressed[w] = newPressed;
        }
    }

    bool InputState::hotkeyDown(int id) const noexcept {
        if (UNLIKELY(static_cast<unsigned>(id) >= kMaxHotkeyId)) return false;
        const HotkeyMask& mask = m_hkMask[id];
        if (!mask.hasMask) return false;

        for (int i = 0; i < 4; ++i) {
            if (mask.vkMask[i]) {
                const uint64_t keys = m_vkDown[i].load(std::memory_order_acquire);
                if (keys & mask.vkMask[i]) return true;
            }
        }
        return false;
    }

    void InputState::resetHotkeyEdges() noexcept {
        uint64_t newPrev[2] = { 0, 0 };

        for (int w = 0; w < 2; ++w) {
            uint64_t bound = m_boundHotkeys[w];
            while (bound) {
#if defined(_MSC_VER) && !defined(__clang__)
                unsigned long bitPos;
                _BitScanForward64(&bitPos, bound);
#else
                const int bitPos = __builtin_ctzll(bound);
#endif
                const int id = (w << 6) | static_cast<int>(bitPos);
                if (hotkeyDown(id)) {
                    newPrev[w] |= (1ULL << bitPos);
                }
                bound &= bound - 1;
            }
        }
        m_hkPrev[0] = newPrev[0];
        m_hkPrev[1] = newPrev[1];
    }

} // namespace MelonPrime
#endif
