
#ifdef _WIN32

#include "MelonPrimeRawInputWinFilter.h"
#include <cassert>


//=====================================================
// Constructor
//=====================================================
RawInputWinFilter::RawInputWinFilter()
{
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
    for (auto& b : m_mbCompat)     b.store(0, std::memory_order_relaxed);
    for (auto& e : m_hkPrevAll)    e.store(0, std::memory_order_relaxed);

    std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));
    std::memset(m_usedVkTable.data(), 0, sizeof(m_usedVkTable));
    std::memset(m_usedVkMask, 0, sizeof(m_usedVkMask));
    std::memset(m_keyBitmap.data(), 0, sizeof(m_keyBitmap));

    runThread.store(true, std::memory_order_relaxed);
    hThread = CreateThread(nullptr, 0, ThreadFunc, this, 0, nullptr);
}


//=====================================================
RawInputWinFilter::~RawInputWinFilter()
{
    runThread.store(false, std::memory_order_relaxed);

    if (hiddenWnd)
        PostMessage(hiddenWnd, WM_CLOSE, 0, 0);

    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }
}


//=====================================================
// Mouse fast
//=====================================================
void RawInputWinFilter::onMouse_fast(RawInputWinFilter* self, RAWINPUT* raw)
{
    const RAWMOUSE& m = raw->data.mouse;

    //------------------------------------------
    // 1) まず dx/dy（最短）
    //------------------------------------------
    const LONG dx = m.lLastX;
    const LONG dy = m.lLastY;
    if (dx | dy)
        self->addDelta(dx, dy);

    //------------------------------------------
    // 2) ボタンフラグが 1つも立ってなければ終了
    //------------------------------------------
    const USHORT f = m.usButtonFlags;
    if (!(f & 0x03FF))   // 10bit以内に DOWN/UP 全て入ってる
        return;

    //------------------------------------------
    // 3) DOWN/UP ビットをテーブルで高速変換
    //------------------------------------------
    // RawInput のビット → (downBits, upBits) にマップ
    // 左=1, 右=2, 中=4, X1=8, X2=16

    uint8_t downBits = 0;
    uint8_t upBits = 0;

    // 定数マッピング：完全 branchless
    static constexpr struct {
        USHORT flagDown;
        USHORT flagUp;
        uint8_t bit;
    } tbl[5] = {
        { RI_MOUSE_LEFT_BUTTON_DOWN,   RI_MOUSE_LEFT_BUTTON_UP,   1  },
        { RI_MOUSE_RIGHT_BUTTON_DOWN,  RI_MOUSE_RIGHT_BUTTON_UP,  2  },
        { RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, 4  },
        { RI_MOUSE_BUTTON_4_DOWN,      RI_MOUSE_BUTTON_4_UP,      8  },
        { RI_MOUSE_BUTTON_5_DOWN,      RI_MOUSE_BUTTON_5_UP,      16 },
    };

    // ブランチレス抽出（5回の定数チェック → 分岐ゼロ）
#pragma unroll
    for (int i = 0; i < 5; i++)
    {
        downBits |= uint8_t((f & tbl[i].flagDown) ? tbl[i].bit : 0);
        upBits |= uint8_t((f & tbl[i].flagUp) ? tbl[i].bit : 0);
    }

    //------------------------------------------
    // 4) raw state 5bit 一括更新
    //------------------------------------------
    const uint8_t cur = self->m_state.mouseButtons.load(std::memory_order_relaxed);

    // nxt = (cur OR downBits) AND NOT upBits
    const uint8_t nxt = uint8_t((cur | downBits) & ~upBits);

    if (nxt != cur)
        self->m_state.mouseButtons.store(nxt, std::memory_order_relaxed);

    //------------------------------------------
    // 5) Qt fallback 更新（5個）
    //------------------------------------------
    self->m_mbCompat[0].store((nxt & 1) != 0, std::memory_order_relaxed);
    self->m_mbCompat[1].store((nxt & 2) != 0, std::memory_order_relaxed);
    self->m_mbCompat[2].store((nxt & 4) != 0, std::memory_order_relaxed);
    self->m_mbCompat[3].store((nxt & 8) != 0, std::memory_order_relaxed);
    self->m_mbCompat[4].store((nxt & 16) != 0, std::memory_order_relaxed);
}


//=====================================================
// Keyboard fast
//=====================================================
void RawInputWinFilter::onKeyboard_fast(RawInputWinFilter* self, RAWINPUT* raw)
{
    const RAWKEYBOARD& k = raw->data.keyboard;

    //===========================
    // 1) 押下/離上判定（最短化）
    //===========================
    const bool isDown = !(k.Flags & RI_KEY_BREAK);

    //===========================
    // 2) 仮VK取得（0～255保証無し）
    //===========================
    UINT vk = k.VKey;

    //===================================================
    // 3) 左右Shift/CTRL/ALT の高速判定（完全分岐最小化）
    //===================================================
    // Flags & E0: 1なら右、0なら左
    const UINT isRight = (k.Flags & RI_KEY_E0) >> 5;   // 0 or 4
    const UINT sc = k.MakeCode;

    // Shiftは MakeCode で確定、CTRL/ALTは E0 で確定
    static constexpr UINT lutShift[256] = {
        /* 0x00～0x29 */ 0,
        /* 0x2A */ VK_LSHIFT,
        /* 0x2B～0x35 */ 0,
        /* 0x36 */ VK_RSHIFT,
        /* 0x37～0xFF */ 0
    };

    if (vk == VK_SHIFT) {
        UINT v = lutShift[sc];
        if (v) vk = v;
    }
    else if (vk == VK_CONTROL) {
        // 左:VK_LCONTROL(0xA2), 右:VK_RCONTROL(0xA3)
        vk = VK_LCONTROL + isRight;
    }
    else if (vk == VK_MENU) {
        // 左:VK_LMENU(0xA4), 右:VK_RMENU(0xA5)
        vk = VK_LMENU + isRight;
    }

    //===========================
    // 4) vk < 256 だけ処理
    //===========================
    if (vk < 256)
    {
        // 互換API
        self->m_vkDownCompat[vk].store(isDown, std::memory_order_relaxed);

        // 64bit ブロックとビット位置
        const uint32_t w = vk >> 6;
        const uint64_t bit = 1ULL << (vk & 63);

        // 既存値ロード
        uint64_t cur = self->m_state.vkDown[w].load(std::memory_order_relaxed);

        // Down: OR、Up: AND NOT → 完全分岐レス
        uint64_t nxt = (isDown ? (cur | bit) : (cur & ~bit));

        // 保存
        self->m_state.vkDown[w].store(nxt, std::memory_order_relaxed);
    }
}



//=====================================================
DWORD WINAPI RawInputWinFilter::ThreadFunc(LPVOID p)
{
    reinterpret_cast<RawInputWinFilter*>(p)->threadLoop();
    return 0;
}


//=====================================================
// RawInput thread loop
//=====================================================
void RawInputWinFilter::threadLoop()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"MPH_RI_THREAD_CLASS";
    RegisterClassW(&wc);

    hiddenWnd = CreateWindowW(
        L"MPH_RI_THREAD_CLASS", L"",
        0, 0, 0, 0, 0,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        this
    );

    RAWINPUTDEVICE rid[2]{
        {1,2,0,hiddenWnd},
        {1,6,0,hiddenWnd}
    };
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

    MSG msg;

    while (runThread.load(std::memory_order_relaxed))
    {
        if (!PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            syncWithSendInput();
            continue;
        }

        if (msg.message == WM_INPUT)
        {
            UINT size = 0;

            GetRawInputData(
                reinterpret_cast<HRAWINPUT>(msg.lParam),
                RID_INPUT,
                nullptr,
                &size,
                sizeof(RAWINPUTHEADER)
            );

            if (size <= sizeof(m_rawBuf))
            {
                GetRawInputData(
                    reinterpret_cast<HRAWINPUT>(msg.lParam),
                    RID_INPUT,
                    m_rawBuf,
                    &size,
                    sizeof(RAWINPUTHEADER)
                );

                RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);

                if (raw->header.dwType == RIM_TYPEMOUSE)
                    onMouse_fast(this, raw);
                else
                    onKeyboard_fast(this, raw);
            }

            continue;
        }

        if (msg.message == WM_QUIT)
            break;
    }

    hiddenWnd = nullptr;
}


//=====================================================
LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(
    HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:   return 0;
    case WM_INPUT:    return 0;
    case WM_CLOSE:
    case WM_DESTROY:  PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}


//=====================================================
// Joy2Key 補正（bitmap + BFS）
//=====================================================
void RawInputWinFilter::syncWithSendInput()
{
    //===============================
    // 1) Keyboard 256byte 一括取得
    //===============================
    BYTE kb[256];
    GetKeyboardState(kb);

    //===============================
    // 2) raw→joy2key 補正：キー
    //===============================
    for (int blk = 0; blk < 4; blk++)
    {
        uint64_t mask = m_usedVkMask[blk];
        uint64_t cur = m_state.vkDown[blk].load(std::memory_order_relaxed);

        while (mask)
        {
            unsigned long bit;
            _BitScanForward64(&bit, mask);
            mask &= (mask - 1);

            const UINT vk = (blk << 6) | bit;

            // keyboard state bit
            const bool down = (kb[vk] & 0x80) != 0;
            const uint64_t b = (1ULL << bit);

            // rawとOS側の差分だけ更新
            const bool raw = (cur & b) != 0;
            if (raw != down)
            {
                cur = down ? (cur | b) : (cur & ~b);
                m_state.vkDown[blk].store(cur, std::memory_order_relaxed);
            }

            // Qt fallback
            m_vkDownCompat[vk].store(down, std::memory_order_relaxed);
        }
    }

    //===============================
    // 3) Mouse 補正（LUTで最短）
    //===============================
//===============================
// 3) Mouse 補正（完全 branchless）
//===============================
    {
        // vk + bit index テーブル
        static constexpr UINT vkTbl[5] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
        static constexpr uint8_t bitTbl[5] = { 1,2,4,8,16 };

        uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
        uint8_t next = cur;

#pragma unroll
        for (int i = 0; i < 5; i++)
        {
            const UINT vk = vkTbl[i];
            const uint8_t bit = bitTbl[i];

            //-----------------------------------------
            // mask = 1 or 0 → branchless スキップ
            //-----------------------------------------
            const uint8_t use = m_usedVkTable[vk];   // 0 or 1

            //-----------------------------------------
            // down = OS側
            //-----------------------------------------
            const uint8_t down = uint8_t((GetAsyncKeyState(vk) & 0x8000) != 0); // 0 or 1

            //-----------------------------------------
            // raw = 現在の mouseButtons の状態
            //-----------------------------------------
            const uint8_t raw = uint8_t((cur & bit) != 0); // 0 or 1

            //-----------------------------------------
            // XOR = 変化検出 (raw != down) → 0 or 1
            //-----------------------------------------
            const uint8_t chg = uint8_t(raw ^ down);

            //-----------------------------------------
            // use * chg → 本当に変更が必要な場合だけ 1
            //-----------------------------------------
            const uint8_t apply = uint8_t(use & chg);

            //-----------------------------------------
            // down=1 → OR  
            // down=0 → AND NOT  
            // ここも branchless
            //-----------------------------------------
            const uint8_t add = uint8_t(apply & down) * bit; // down=1 → bit, else 0
            const uint8_t remove = uint8_t(apply & !down) * bit; // down=0 → bit, else 0

            next = uint8_t((next | add) & ~remove);

            //-----------------------------------------
            // Qt fallback 更新（必要最小限 branchless）
            //-----------------------------------------
            m_mbCompat[i].store(down, std::memory_order_relaxed);
        }

        if (next != cur)
            m_state.mouseButtons.store(next, std::memory_order_relaxed);
    }

}



//=====================================================
void RawInputWinFilter::resetAllKeys()
{
    for (auto& v : m_state.vkDown)
        v.store(0, std::memory_order_relaxed);

    for (auto& c : m_vkDownCompat)
        c.store(0, std::memory_order_relaxed);
}


//=====================================================
void RawInputWinFilter::resetMouseButtons()
{
    m_state.mouseButtons.store(0, std::memory_order_relaxed);

    for (auto& m : m_mbCompat)
        m.store(0, std::memory_order_relaxed);
}


//=====================================================
void RawInputWinFilter::resetHotkeyEdges()
{
    for (auto& h : m_hkPrevAll)
        h.store(0, std::memory_order_relaxed);
}


//=====================================================
bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    //---------------------------------------
    // 0) Fast path: hk in range
    //---------------------------------------
    if ((unsigned)hk < kMaxHotkeyId)
    {
        const HotkeyMask& m = m_hkMask[hk];

        if (m.hasMask)
        {
            // 256bit の AND を ILP で並列処理
            const uint64_t x0 = m_state.vkDown[0].load(std::memory_order_relaxed) & m.vkMask[0];
            const uint64_t x1 = m_state.vkDown[1].load(std::memory_order_relaxed) & m.vkMask[1];
            const uint64_t x2 = m_state.vkDown[2].load(std::memory_order_relaxed) & m.vkMask[2];
            const uint64_t x3 = m_state.vkDown[3].load(std::memory_order_relaxed) & m.vkMask[3];

            // OR 1回の最短判定
            if ((x0 | x1 | x2 | x3) != 0)
                return true;

            // mouse 5bit判定も branchlessに近づける
            const uint8_t mb = m_state.mouseButtons.load(std::memory_order_relaxed);
            if ((m.mouseMask & mb) != 0)
                return true;

            return false;
        }
    }

    //---------------------------------------
    // 1) fallback path（旧互換）
    //---------------------------------------
    auto it = m_hkToVk.find(hk);
    if (it == m_hkToVk.end())
        return false;

    // vk 1個ずつ走査
    for (UINT vk : it->second)
    {
        // mouse: vk < 8 → index化
        if (vk < 8)
        {
            static constexpr uint8_t tbl[8] = { 255,0,1,255,2,3,4,255 };
            uint8_t idx = tbl[vk];
            if (idx < 5 && getMouseButton(idx))
                return true;
        }
        // keyboard
        else if (vk < 256)
        {
            if (getVkState(vk))
                return true;
        }
    }

    return false;
}



bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    // 入力取得（hotkeyDown は1回で済ませる）
    const bool now = hotkeyDown(hk);

    // prev = oldValue; now → store
    uint32_t prev = m_hkPrevAll[hk & 1023].exchange(uint32_t(now), std::memory_order_acq_rel);

    // prev=0 & now=1 → pressed
    return (!prev & now);
}


bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    const bool now = hotkeyDown(hk);

    uint32_t prev = m_hkPrevAll[hk & 1023].exchange(uint32_t(now), std::memory_order_acq_rel);

    // prev=1 & now=0 → released
    return (prev & !now);
}



//=====================================================
void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks)
{
    if ((unsigned)hk < kMaxHotkeyId)
    {
        HotkeyMask& m = m_hkMask[hk];
        std::memset(&m, 0, sizeof(m));

        for (UINT vk : vks)
        {
            if (vk == 0) continue;
            addVkToMask(m, vk);

            if (vk < 256)
            {
                m_usedVkTable[vk] = 1;
                m_usedVkMask[vk >> 6] |= (1ULL << (vk & 63));
            }
        }
    }
    else
    {
        m_hkToVk[hk] = vks;

        for (UINT vk : vks)
        {
            if (vk < 256)
            {
                m_usedVkTable[vk] = 1;
                m_usedVkMask[vk >> 6] |= (1ULL << (vk & 63));
            }
        }
    }
}

#endif // _WIN32
