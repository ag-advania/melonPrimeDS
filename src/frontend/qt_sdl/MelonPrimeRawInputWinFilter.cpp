#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"

// 5bitボタンマスク（分岐ゼロ）
namespace {
    FORCE_INLINE uint8_t mask_down_from_flags(USHORT f) noexcept {
        // LDOWN:0, RDOWN:2, MDOWN:4, X1DOWN:6, X2DOWN:8
        return uint8_t(((f >> 0) & 1)
            | (((f >> 2) & 1) << 1)
            | (((f >> 4) & 1) << 2)
            | (((f >> 6) & 1) << 3)
            | (((f >> 8) & 1) << 4));
    }
    FORCE_INLINE uint8_t mask_up_from_flags(USHORT f) noexcept {
        // LUP:1, RUP:3, MUP:5, X1UP:7, X2UP:9
        return uint8_t(((f >> 1) & 1)
            | (((f >> 3) & 1) << 1)
            | (((f >> 5) & 1) << 2)
            | (((f >> 7) & 1) << 3)
            | (((f >> 9) & 1) << 4));
    }
}

// ---------------- 初期化／終了 ----------------
RawInputWinFilter::RawInputWinFilter()
{
    for (auto& w : m_hkPrev) w.store(0, std::memory_order_relaxed);
    startInputThread(false);
}

RawInputWinFilter::~RawInputWinFilter()
{
    stopInputThread();
}

bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void*, qintptr*)
{
    // WM_INPUT は専用スレッドで受けるため常に false
    return false;
}

// ---------------- avrt.dll ロード ----------------
void RawInputWinFilter::loadAvrt()
{
    if (m_hAvrt) return;
    m_hAvrt = ::LoadLibraryW(L"avrt.dll");
    if (!m_hAvrt) return;

    m_pAvSetMmThreadCharacteristicsW = reinterpret_cast<PFN_AvSetMmThreadCharacteristicsW>(
        ::GetProcAddress(m_hAvrt, "AvSetMmThreadCharacteristicsW"));
    m_pAvRevertMmThreadCharacteristics = reinterpret_cast<PFN_AvRevertMmThreadCharacteristics>(
        ::GetProcAddress(m_hAvrt, "AvRevertMmThreadCharacteristics"));
    m_pAvSetMmThreadPriority = reinterpret_cast<PFN_AvSetMmThreadPriority>(
        ::GetProcAddress(m_hAvrt, "AvSetMmThreadPriority"));
}

// ---------------- スレッド制御 ----------------
bool RawInputWinFilter::startInputThread(bool useInputSink)
{
    if (m_thrRunning.load(std::memory_order_acquire)) return true;
    m_useInputSink = useInputSink;
    m_thrQuit.store(false, std::memory_order_release);

    m_thr = std::thread([this] { inputThreadMain(); });

    // 起動完了待ち（軽量）
    while (!m_thrRunning.load(std::memory_order_acquire)) {
        ::Sleep(1);
    }
    return true;
}

void RawInputWinFilter::stopInputThread()
{
    if (!m_thr.joinable()) return;
    m_thrQuit.store(true, std::memory_order_release);
    if (m_hwndMsg) ::PostMessageW(m_hwndMsg, WM_CLOSE, 0, 0);
    m_thr.join();
    m_thrRunning.store(false, std::memory_order_release);
    m_hwndMsg = nullptr;
}

// ---------------- QoS（MMCSS “Games” 適用） ----------------
void RawInputWinFilter::applyThreadQoS()
{
    // MMCSS 参加
    loadAvrt();
    if (m_pAvSetMmThreadCharacteristicsW) {
        m_mmcssHandle = m_pAvSetMmThreadCharacteristicsW(L"Games", &m_mmcssTaskIndex);
        if (m_mmcssHandle && m_pAvSetMmThreadPriority) {
            // 高めだが余裕を残す。さらに詰めるなら CRITICAL でも可。
            (void)m_pAvSetMmThreadPriority(m_mmcssHandle, AVRT_PRIORITY_HIGH);
        }
    }
    // Win32スレッド優先度も上げる（MMCSSと併用可）
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
}

LRESULT CALLBACK RawInputWinFilter::WndProcThunk(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    RawInputWinFilter* self =
        reinterpret_cast<RawInputWinFilter*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return ::DefWindowProcW(hWnd, msg, wp, lp);
    }
    if (!self) return ::DefWindowProcW(hWnd, msg, wp, lp);
    return self->WndProc(hWnd, msg, wp, lp);
}

LRESULT RawInputWinFilter::WndProc(HWND hWnd, UINT msg, WPARAM, LPARAM lp)
{
    switch (msg) {
    case WM_INPUT:
    {
        const RAWINPUT* raw = nullptr;
        if (readRawInputToBuf(lp, raw)) {
            handleRawInput(*raw);
        }
        return 0;
    }
    case WM_CLOSE:
        ::DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        return ::DefWindowProcW(hWnd, msg, 0, lp);
    }
}

bool RawInputWinFilter::registerRawInput(HWND target)
{
    RAWINPUTDEVICE rid[2]{};
    rid[0].usUsagePage = 0x01; // Generic Desktop Controls
    rid[0].usUsage = 0x02; // Mouse
    rid[0].dwFlags = m_useInputSink ? RIDEV_INPUTSINK : 0; // ※ NOLEGACY/NOHOTKEYS は使わない
    rid[0].hwndTarget = target;

    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x06; // Keyboard
    rid[1].dwFlags = m_useInputSink ? RIDEV_INPUTSINK : 0;
    rid[1].hwndTarget = target;

    return !!::RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

void RawInputWinFilter::inputThreadMain()
{
    // QoS 適用（このスレッド上で実行することが重要）
    applyThreadQoS();

    // メッセージ専用ウィンドウ生成
    m_hinst = (HINSTANCE)::GetModuleHandleW(nullptr);
    const wchar_t kCls[] = L"MPDS_RI_CLASS_MIN_SB_MMCSS";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &RawInputWinFilter::WndProcThunk;
    wc.hInstance = m_hinst;
    wc.lpszClassName = kCls;
    ::RegisterClassExW(&wc); // 既登録でもOK

    HWND h = ::CreateWindowExW(0, kCls, L"MPDS_RI_WINDOW_MIN_SB_MMCSS", 0,
        0, 0, 0, 0,
        HWND_MESSAGE, nullptr, m_hinst, this);
    m_hwndMsg = h;

    if (!h) { m_thrRunning.store(true, std::memory_order_release); goto exit_thread; }
    if (!registerRawInput(h)) { ::DestroyWindow(h); m_thrRunning.store(true, std::memory_order_release); goto exit_thread; }

    m_thrRunning.store(true, std::memory_order_release);

    // ループ：WM_INPUT 即時処理（キュー滞留ゼロ）
    MSG m;
    while (!m_thrQuit.load(std::memory_order_acquire)) {
        while (::PeekMessageW(&m, h, WM_INPUT, WM_INPUT, PM_REMOVE)) {
            if (m.message == WM_QUIT) goto exit_thread;
            ::TranslateMessage(&m);
            ::DispatchMessageW(&m);
        }
        if (::PeekMessageW(&m, h, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) break;
            ::TranslateMessage(&m);
            ::DispatchMessageW(&m);
            continue;
        }
        ::SwitchToThread(); // 軽く譲る
    }

exit_thread:
    // MMCSS から離脱（同一スレッドで行う）
    if (m_mmcssHandle && m_pAvRevertMmThreadCharacteristics) {
        (void)m_pAvRevertMmThreadCharacteristics(m_mmcssHandle);
        m_mmcssHandle = nullptr;
    }
    return;
}

// ---------------- RawInput 読みラッパ ----------------
FORCE_INLINE bool RawInputWinFilter::readRawInputToBuf(LPARAM lp, const RAWINPUT*& out) noexcept
{
    UINT need = 0;
    if (::GetRawInputData((HRAWINPUT)lp, RID_INPUT, nullptr, &need, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return false;
    if (need > sizeof(m_rawBuf)) return false; // 256B超なら拡張方針に
    UINT size = need;
    if (::GetRawInputData((HRAWINPUT)lp, RID_INPUT, m_rawBuf, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return false;
    out = reinterpret_cast<const RAWINPUT*>(m_rawBuf);
    return true;
}

// ---------------- RawInput 処理 ----------------
void RawInputWinFilter::handleRawInput(const RAWINPUT& raw) noexcept
{
    if (raw.header.dwType == RIM_TYPEMOUSE) {
        handleRawMouse(raw.data.mouse);
    }
    else if (raw.header.dwType == RIM_TYPEKEYBOARD) {
        handleRawKeyboard(raw.data.keyboard);
    }
}

void RawInputWinFilter::handleRawMouse(const RAWMOUSE& m) noexcept
{
    // 相対移動（ABSでない場合のみ）
    if ((m.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
        const LONG dx_ = m.lLastX;
        const LONG dy_ = m.lLastY;
        if ((dx_ | dy_) != 0) {
            (void)m_dx.fetch_add((int32_t)dx_, std::memory_order_relaxed);
            (void)m_dy.fetch_add((int32_t)dy_, std::memory_order_relaxed);
        }
    }

    // ボタン（分岐ゼロ合成）
    const USHORT f = m.usButtonFlags;
    if (f) {
        const uint8_t downMask = mask_down_from_flags(f);
        const uint8_t upMask = mask_up_from_flags(f);
        if ((downMask | upMask) != 0) {
            const uint8_t cur = m_state.mouse.load(std::memory_order_relaxed);
            const uint8_t nxt = (uint8_t)((cur | downMask) & (uint8_t)~upMask);
            m_state.mouse.store(nxt, std::memory_order_relaxed);
        }
    }
}

void RawInputWinFilter::handleRawKeyboard(const RAWKEYBOARD& kb) noexcept
{
    UINT vk = kb.VKey;
    const USHORT flags = kb.Flags;
    const bool isUp = (flags & RI_KEY_BREAK) != 0;

    // 左右修正
    switch (vk) {
    case VK_SHIFT: {
        const UINT ex = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
        vk = ex ? ex : VK_SHIFT;
        break;
    }
    case VK_CONTROL: vk = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL; break;
    case VK_MENU:    vk = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;    break;
    default: break;
    }

    if (vk < 256) {
        const uint64_t bit = 1ULL << (vk & 63);
        auto& word = m_state.vk[vk >> 6];
        if (!isUp) (void)word.fetch_or(bit, std::memory_order_relaxed);
        else        (void)word.fetch_and(~bit, std::memory_order_relaxed);
    }
}

// ---------------- API（取得／リセット／Hotkey） ----------------
void RawInputWinFilter::fetchMouseDelta(int& outDx, int& outDy)
{
    outDx = m_dx.exchange(0, std::memory_order_relaxed);
    outDy = m_dy.exchange(0, std::memory_order_relaxed);
}

void RawInputWinFilter::discardDeltas()
{
    (void)m_dx.exchange(0, std::memory_order_relaxed);
    (void)m_dy.exchange(0, std::memory_order_relaxed);
}

uint8_t RawInputWinFilter::mouseButtons() const noexcept
{
    return m_state.mouse.load(std::memory_order_relaxed);
}

bool RawInputWinFilter::vkDown(uint32_t vk) const noexcept
{
    if (vk >= 256) return false;
    const uint64_t w = m_state.vk[vk >> 6].load(std::memory_order_relaxed);
    return (w & (1ULL << (vk & 63))) != 0ULL;
}

void RawInputWinFilter::addVkToMask(HotkeyMask& m, UINT vk) noexcept
{
    if (vk < 8) {
        const uint8_t id = kMouseButtonLUT[vk];
        if (id != 0xFF) { m.mouseMask |= (uint8_t)(1u << id); m.hasMask = 1; }
        return;
    }
    if (vk < 256) {
        m.vkMask[vk >> 6] |= (1ULL << (vk & 63));
        m.hasMask = 1;
    }
}

void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks)
{
    if ((unsigned)hk >= kMaxHotkeyId) return;
    HotkeyMask& m = m_hkMask[hk];
    std::memset(&m, 0, sizeof(m));
    const size_t n = (vks.size() > 16) ? 16 : vks.size();
    for (size_t i = 0; i < n; ++i) addVkToMask(m, vks[i]);
}

bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    if ((unsigned)hk >= kMaxHotkeyId) return false;
    const HotkeyMask& m = m_hkMask[hk];
    if (!m.hasMask) return false;

    const bool vkHit =
        ((m_state.vk[0].load(std::memory_order_relaxed) & m.vkMask[0]) |
            (m_state.vk[1].load(std::memory_order_relaxed) & m.vkMask[1]) |
            (m_state.vk[2].load(std::memory_order_relaxed) & m.vkMask[2]) |
            (m_state.vk[3].load(std::memory_order_relaxed) & m.vkMask[3])) != 0ULL;

    const bool mouseHit = (m_state.mouse.load(std::memory_order_relaxed) & m.mouseMask) != 0u;

    return vkHit | mouseHit;
}

bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    if ((unsigned)hk >= kMaxHotkeyId) return false;
    const bool now = hotkeyDown(hk);
    const size_t idx = ((unsigned)hk) >> 6;
    const uint64_t bit = 1ULL << (hk & 63);
    if (now) {
        const uint64_t prev = m_hkPrev[idx].fetch_or(bit, std::memory_order_acq_rel);
        return (prev & bit) == 0;
    }
    else {
        (void)m_hkPrev[idx].fetch_and(~bit, std::memory_order_acq_rel);
        return false;
    }
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    if ((unsigned)hk >= kMaxHotkeyId) return false;
    const bool now = hotkeyDown(hk);
    const size_t idx = ((unsigned)hk) >> 6;
    const uint64_t bit = 1ULL << (hk & 63);
    if (!now) {
        const uint64_t prev = m_hkPrev[idx].fetch_and(~bit, std::memory_order_acq_rel);
        return (prev & bit) != 0;
    }
    else {
        (void)m_hkPrev[idx].fetch_or(bit, std::memory_order_acq_rel);
        return false;
    }
}

bool RawInputWinFilter::getVkState(uint32_t vk) const noexcept
{
    if (vk >= 256) return false;
    const uint64_t w = m_state.vk[vk >> 6].load(std::memory_order_relaxed);
    return (w & (1ULL << (vk & 63))) != 0ULL;
}

// ---- リセット系 ----
void RawInputWinFilter::resetMouseButtons()
{
    m_state.mouse.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetAllKeys()
{
    for (auto& w : m_state.vk) w.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetHotkeyEdges()
{
    for (auto& w : m_hkPrev) w.store(0, std::memory_order_relaxed);
}

#endif // _WIN32
