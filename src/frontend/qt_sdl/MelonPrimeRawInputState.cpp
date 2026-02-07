#ifdef _WIN32
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"
#include <cstring>
#include <algorithm>

// MinGW対策
#ifndef QWORD
typedef unsigned __int64 QWORD;
#endif

namespace MelonPrime {

    std::array<InputState::BtnLutEntry, 1024> InputState::s_btnLut;
    std::array<InputState::VkRemapEntry, 256> InputState::s_vkRemap;
    uint16_t InputState::s_scancodeLShift = 0;
    uint16_t InputState::s_scancodeRShift = 0;
    std::once_flag InputState::s_initFlag;

    // 初期値は標準API (user32.dll) に設定
    NtUserGetRawInputBuffer_t InputState::s_fnBestGetRawInputBuffer = ::GetRawInputBuffer;

    InputState::InputState() noexcept {
        for (auto& vk : m_vkDown) {
            vk.store(0, std::memory_order_relaxed);
        }
        m_mouseButtons.store(0, std::memory_order_relaxed);
        m_mouseDeltaCombined.store(0, std::memory_order_relaxed);

        std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));
        std::fill(std::begin(m_hkPrev), std::end(m_hkPrev), 0);
        m_boundHotkeys[0] = 0;
        m_boundHotkeys[1] = 0;
    }

    void InputState::InitializeTables() noexcept {
        std::call_once(s_initFlag, []() {
            // 1. NT APIの解決を試みる
            WinInternal::ResolveNtApis();

            // 2. 最適な関数を選択
            // NtUserGetRawInputBuffer が使えるならカーネルバッファ直読みAPIを採用
            if (WinInternal::fnNtUserGetRawInputBuffer) {
                s_fnBestGetRawInputBuffer = WinInternal::fnNtUserGetRawInputBuffer;
            }
            // そうでなければ初期値(::GetRawInputBuffer)のまま

            // 3. テーブル初期化
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

            std::fill(s_vkRemap.begin(), s_vkRemap.end(), VkRemapEntry{ 0, 0 });
            s_vkRemap[VK_CONTROL] = { VK_LCONTROL, VK_RCONTROL };
            s_vkRemap[VK_MENU] = { VK_LMENU,    VK_RMENU };
            s_vkRemap[VK_SHIFT] = { VK_LSHIFT,   VK_RSHIFT };

            s_scancodeLShift = static_cast<uint16_t>(MapVirtualKeyW(VK_LSHIFT, MAPVK_VK_TO_VSC));
            s_scancodeRShift = static_cast<uint16_t>(MapVirtualKeyW(VK_RSHIFT, MAPVK_VK_TO_VSC));
            });
    }

    // ===================================================================
    // Joy2Key ON時 (単発)
    // ===================================================================
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
            if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                const LONG dx = m.lLastX;
                const LONG dy = m.lLastY;
                if (dx | dy) {
                    MouseDeltaPack cur, nxt;
                    cur.combined = m_mouseDeltaCombined.load(std::memory_order_relaxed);
                    do {
                        nxt.s.x = cur.s.x + dx;
                        nxt.s.y = cur.s.y + dy;
                    } while (UNLIKELY(!m_mouseDeltaCombined.compare_exchange_weak(
                        cur.combined, nxt.combined, std::memory_order_release, std::memory_order_relaxed)));
                }
            }

            const USHORT flags = m.usButtonFlags & 0x03FF;
            if (flags) {
                const auto& lut = s_btnLut[flags];
                if (lut.downBits | lut.upBits) {
                    uint8_t cur = m_mouseButtons.load(std::memory_order_relaxed);
                    uint8_t nxt;
                    do {
                        nxt = (cur | lut.downBits) & ~lut.upBits;
                    } while (cur != nxt && UNLIKELY(!m_mouseButtons.compare_exchange_weak(
                        cur, nxt, std::memory_order_release, std::memory_order_relaxed)));
                }
            }
            break;
        }
        case RIM_TYPEKEYBOARD: {
            const RAWKEYBOARD& kb = raw->data.keyboard;
            UINT vk = kb.VKey;
            if (LIKELY(vk > 0 && vk < 255)) {
                vk = remapVk(vk, kb.MakeCode, kb.Flags);
                const bool isDown = !(kb.Flags & RI_KEY_BREAK);
                setVkBit(vk, isDown);
            }
            break;
        }
        }
    }

    // ===================================================================
    // Joy2Key OFF時 (Direct Polling / Batch)
    // ★ 最適化済み: 関数ポインタ経由でループ処理
    // ===================================================================
    void InputState::processRawInputBatched() noexcept {
        alignas(16) static thread_local uint8_t buffer[16384];

        // ローカルアキュムレータ (アトミック競合を回避するため)
        int32_t localAccX = 0;
        int32_t localAccY = 0;

        // キーボードの状態変化マスク
        // [0]: Down (OR mask), [1]: Up (AND mask 準備用)
        uint64_t localKeyDeltaDown[4] = { 0 };
        uint64_t localKeyDeltaUp[4] = { 0 };
        bool hasMouseDelta = false;
        bool hasKeyChanges = false;
        bool hasButtonChanges = false;

        // マウスボタンの最終状態を追跡
        // 初期値は現在のグローバル状態を取得しても良いが、
        // ここでは論理演算で差分だけ適用する方式をとる
        uint8_t currentBtnState = m_mouseButtons.load(std::memory_order_relaxed);
        uint8_t finalBtnState = currentBtnState;

        for (;;) {
            UINT size = sizeof(buffer);
            // NT API呼出し (オーバーヘッド最小)
            UINT count = s_fnBestGetRawInputBuffer(
                reinterpret_cast<PRAWINPUT>(buffer),
                &size,
                sizeof(RAWINPUTHEADER)
            );

            if (count == 0 || count == UINT(-1)) break;

            const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(buffer);

            for (UINT i = 0; i < count; ++i) {
                if (raw->header.dwType == RIM_TYPEMOUSE) {
                    const RAWMOUSE& m = raw->data.mouse;
                    // Move logic
                    if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                        localAccX += m.lLastX;
                        localAccY += m.lLastY;
                        hasMouseDelta = true;
                    }
                    // Button logic
                    const USHORT flags = m.usButtonFlags & 0x03FF;
                    if (flags) {
                        const auto& lut = s_btnLut[flags];
                        // ローカル変数で計算し続ける
                        finalBtnState = (finalBtnState & ~lut.upBits) | lut.downBits;
                        hasButtonChanges = true;
                    }
                }
                else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                    const RAWKEYBOARD& kb = raw->data.keyboard;
                    UINT vk = kb.VKey;
                    // 分岐予測ヒント: ほとんどのキーイベントは有効
                    if (LIKELY(vk > 0 && vk < 255)) {
                        vk = remapVk(vk, kb.MakeCode, kb.Flags);
                        const int idx = vk >> 6;
                        const uint64_t bit = 1ULL << (vk & 63);

                        if (!(kb.Flags & RI_KEY_BREAK)) { // Key Down
                            localKeyDeltaDown[idx] |= bit;
                            localKeyDeltaUp[idx] &= ~bit; // Down優先
                        }
                        else { // Key Up
                            localKeyDeltaUp[idx] |= bit;
                            localKeyDeltaDown[idx] &= ~bit;
                        }
                        hasKeyChanges = true;
                    }
                }
                raw = NEXTRAWINPUTBLOCK(raw);
            }
        }

        // --- Batch Commit (ここで初めてアトミック操作を行う) ---

        // 1. Mouse Delta Update
        if (hasMouseDelta) {
            // CASループの代わりに fetch_add 的なロジックが必要だが、
            // X/Yパック構造なので CASループは避けられない。
            // ただし、ループ回数は「バッファ処理回数」ではなく「1回」になるため高速。
            MouseDeltaPack cur, nxt;
            cur.combined = m_mouseDeltaCombined.load(std::memory_order_relaxed);
            do {
                nxt.s.x = cur.s.x + localAccX;
                nxt.s.y = cur.s.y + localAccY;
            } while (UNLIKELY(!m_mouseDeltaCombined.compare_exchange_weak(
                cur.combined, nxt.combined, std::memory_order_release, std::memory_order_relaxed)));
        }

        // 2. Mouse Button Update
        if (hasButtonChanges) {
            // 他のスレッドが書き込んでいないと仮定できるなら store でも良いが、
            // 安全のため交換ループを使用。ただし頻度は低い。
            uint8_t cur = m_mouseButtons.load(std::memory_order_relaxed);
            uint8_t nxt;
            do {
                // ローカルで計算した最終状態とマージするロジックが必要だが、
                // ボタンは絶対値なので、最新の finalBtnState を採用してよい場合が多い。
                // ここでは競合を考慮して「変化分」のみ適用するのは複雑になるため、
                // 単純にCASで最新状態を書きに行く
                nxt = finalBtnState;
            } while (!m_mouseButtons.compare_exchange_weak(
                cur, nxt, std::memory_order_release, std::memory_order_relaxed));
        }

        // 3. Keyboard Update
        if (hasKeyChanges) {
            for (int i = 0; i < 4; ++i) {
                if (localKeyDeltaDown[i] | localKeyDeltaUp[i]) {
                    uint64_t cur = m_vkDown[i].load(std::memory_order_relaxed);
                    uint64_t nxt;
                    do {
                        // Downビットを立て、Upビットを消す
                        nxt = (cur | localKeyDeltaDown[i]) & ~localKeyDeltaUp[i];
                    } while (cur != nxt && UNLIKELY(!m_vkDown[i].compare_exchange_weak(
                        cur, nxt, std::memory_order_release, std::memory_order_relaxed)));
                }
            }
        }
    }

    void InputState::fetchMouseDelta(int& outX, int& outY) noexcept {
        const uint64_t val = m_mouseDeltaCombined.exchange(0, std::memory_order_acquire);
        MouseDeltaPack p;
        p.combined = val;
        outX = p.s.x;
        outY = p.s.y;
    }

    void InputState::discardDeltas() noexcept {
        m_mouseDeltaCombined.store(0, std::memory_order_relaxed);
    }

    void InputState::resetAllKeys() noexcept {
        for (auto& vk : m_vkDown) {
            vk.store(0, std::memory_order_release);
        }
        m_mouseButtons.store(0, std::memory_order_release);
        std::fill(std::begin(m_hkPrev), std::end(m_hkPrev), 0);
    }

    void InputState::resetMouseButtons() noexcept {
        m_mouseButtons.store(0, std::memory_order_release);
    }

    void InputState::clearAllBindings() noexcept {
        std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));
        std::fill(std::begin(m_hkPrev), std::end(m_hkPrev), 0);
        m_boundHotkeys[0] = 0;
        m_boundHotkeys[1] = 0;
    }

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

        for (const UINT vk : vks) {
            if (vk >= VK_LBUTTON && vk <= VK_XBUTTON2) {
                int bit = -1;
                switch (vk) {
                case VK_LBUTTON:  bit = 0; break;
                case VK_RBUTTON:  bit = 1; break;
                case VK_MBUTTON:  bit = 2; break;
                case VK_XBUTTON1: bit = 3; break;
                case VK_XBUTTON2: bit = 4; break;
                }
                if (bit >= 0) mask.mouseMask |= static_cast<uint8_t>(1 << bit);
            }
            else if (vk < 256) {
                mask.vkMask[vk >> 6] |= (1ULL << (vk & 63));
            }
        }
        mask.hasMask = true;
        m_boundHotkeys[bword] |= bbit;
    }

    void InputState::pollHotkeys(FrameHotkeyState& out) noexcept {
        uint64_t snapVk[4];
        for (int i = 0; i < 4; ++i)
            snapVk[i] = m_vkDown[i].load(std::memory_order_acquire);
        const uint8_t snapMouse = m_mouseButtons.load(std::memory_order_acquire);

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

                const bool isDown = testHotkeyMask(mask, snapVk, snapMouse);

                if (isDown) newDown |= hbit;

                const bool prev = (m_hkPrev[w] & hbit) != 0;
                if (isDown && !prev) {
                    newPressed |= hbit;
                    m_hkPrev[w] |= hbit;
                }
                else if (!isDown && prev) {
                    m_hkPrev[w] &= ~hbit;
                }

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

        if (mask.mouseMask) {
            const uint8_t buttons = m_mouseButtons.load(std::memory_order_acquire);
            if (buttons & mask.mouseMask) return true;
        }
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