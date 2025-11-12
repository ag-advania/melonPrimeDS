#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"

#if MP_TIMEPERIOD_1MS
#include <mmsystem.h> // timeBeginPeriod / timeEndPeriod
#if defined(_MSC_VER)
#pragma comment(lib, "winmm.lib")
#endif
#endif

#if defined(_MSC_VER)
#include <intrin.h>   // _BitScanForward64
#endif

// --- 5bitボタンマスク（分岐ゼロ） ---
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
    // WM_INPUTは専用スレッドで処理
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

// ---------------- QoS（MMCSS＋スロットリング無効＋ブースト許可＋（任意）Pコア固定） ----------------
void RawInputWinFilter::applyThreadQoS()
{
    // —— 優先度：常に Normal。MelonPrimeDS（エミュ本体）を上回らない ——
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_NORMAL);

    // 動的ブースト禁止：一時的な高優先度化を抑止（出過ぎ防止）
    ::SetThreadPriorityBoost(::GetCurrentThread(), TRUE);

    // —— スロットリング無効（優先度は変えない／実行権を奪わない） ——
#ifndef PROCESS_POWER_THROTTLING_CURRENT_VERSION
#  define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#  define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
    typedef struct _PROCESS_POWER_THROTTLING_STATE {
        ULONG Version;
        ULONG ControlMask;
        ULONG StateMask;
    } PROCESS_POWER_THROTTLING_STATE, * PPROCESS_POWER_THROTTLING_STATE;
#endif
#ifndef ProcessPowerThrottling
#  define ProcessPowerThrottling ((PROCESS_INFORMATION_CLASS)9)
#endif
    PROCESS_POWER_THROTTLING_STATE pps{};
    pps.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    pps.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    pps.StateMask = 0; // 無効（速度抑制なし：優先度は変わらない）
    (void)::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling, &pps, sizeof(pps));

#ifndef THREAD_POWER_THROTTLING_CURRENT_VERSION
#  define THREAD_POWER_THROTTLING_CURRENT_VERSION 1
#  define THREAD_POWER_THROTTLING_EXECUTION_SPEED 0x1
    typedef struct _THREAD_POWER_THROTTLING_STATE {
        ULONG Version;
        ULONG ControlMask;
        ULONG StateMask;
    } THREAD_POWER_THROTTLING_STATE, * PTHREAD_POWER_THROTTLING_STATE;
#endif
#ifndef ThreadPowerThrottling
#  define ThreadPowerThrottling ((THREAD_INFORMATION_CLASS)9)
#endif
    THREAD_POWER_THROTTLING_STATE pts{};
    pts.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    pts.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    pts.StateMask = 0; // 無効（これも優先度には影響しない）
    (void)::SetThreadInformation(::GetCurrentThread(), ThreadPowerThrottling, &pts, sizeof(pts));

    // —— MMCSS（デフォルトOFF、使う場合でも NORMAL 参加） ——
#if MP_ENABLE_MMCSS
    loadAvrt();
    if (m_pAvSetMmThreadCharacteristicsW) {
        m_mmcssHandle = m_pAvSetMmThreadCharacteristicsW(L"Games", &m_mmcssTaskIndex);
        if (m_mmcssHandle && m_pAvSetMmThreadPriority) {
            // MMCSS内でも NORMAL に固定（HIGHEST/CRITICAL などは使わない）
            (void)m_pAvSetMmThreadPriority(m_mmcssHandle, AVRT_PRIORITY_NORMAL);
        }
        // Win32のベース優先度は Normal のまま触らない
    }
#endif

#if MP_RAW_PIN_BIGCORE
    // Pコア固定は“同優先度内の実行効率”を上げるだけなのでそのまま
    tryPinToPerformanceCore();
#endif
}


// ---------------- （任意）高性能コアへピン止め ----------------
void RawInputWinFilter::tryPinToPerformanceCore()
{
    // RelationProcessorCore で最小 EfficiencyClass のコアの1CPUに張り付け
    DWORD bytes = 0;
    if (!::GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes) &&
        ::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return;
    }
    std::vector<BYTE> buf(bytes);
    if (!::GetLogicalProcessorInformationEx(RelationProcessorCore,
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()),
        &bytes)) {
        return;
    }

    WORD   bestGroup = 0;
    KAFFINITY bestMask = 0;
    BYTE   bestEff = 0xFF; // 0 が最も高性能

    BYTE* p = buf.data();
    BYTE* end = buf.data() + bytes;
    while (p < end) {
        auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(p);
        if (info->Relationship == RelationProcessorCore) {
            const PROCESSOR_RELATIONSHIP& pr = info->Processor;
            const BYTE eff = pr.EfficiencyClass;

            for (WORD i = 0; i < pr.GroupCount; ++i) {
                const GROUP_AFFINITY& ga = pr.GroupMask[i];
                if (!ga.Mask) continue;

                KAFFINITY one = 0;
#if defined(_MSC_VER)
                unsigned long idx = 0;
                unsigned long long m = ga.Mask;
                if (_BitScanForward64(&idx, m)) {
                    one = (KAFFINITY)(1ull << idx);
                }
                else {
                    continue;
                }
#else
                int idx = __builtin_ctzll((unsigned long long)ga.Mask);
                one = (KAFFINITY)(1ull << idx);
#endif

                if (eff < bestEff) {
                    bestEff = eff;
                    bestGroup = ga.Group;
                    bestMask = one;
                    if (bestEff == 0) break;
                }
            }
        }
        p += info->Size;
    }

    if (bestMask == 0) return;

    GROUP_AFFINITY ga{};
    ga.Group = bestGroup;
    ga.Mask = bestMask;

    GROUP_AFFINITY oldGa{};
    (void)::SetThreadGroupAffinity(::GetCurrentThread(), &ga, &oldGa);
    // 失敗しても静かにスキップ
}

// ---------------- Win32 Window ----------------
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
    case WM_INPUT: {
        // 通常はinputThreadMain側で直処理
        const RAWINPUT* raw = nullptr;
        if (readRawInputToBuf(lp, raw)) handleRawInput(*raw);
        return 0;
    }
    case MP_RAW_REREGISTER:
        registerRawInput(hWnd);
        return 0;
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

// ---------------- RawInput 登録 ----------------
bool RawInputWinFilter::registerRawInput(HWND target)
{
    RAWINPUTDEVICE rid[2]{};
    rid[0].usUsagePage = 0x01; // Generic Desktop Controls
    rid[0].usUsage = 0x02; // Mouse
    rid[0].dwFlags = m_useInputSink ? RIDEV_INPUTSINK : 0; // NOLEGACY/NOHOTKEYSは不使用
    rid[0].hwndTarget = target;

    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x06; // Keyboard
    rid[1].dwFlags = m_useInputSink ? RIDEV_INPUTSINK : 0;
    rid[1].hwndTarget = target;

    return !!::RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

// ---------------- スレッド本体（外周1回＋待機は“新規到着まで”ブロック） ----------------
void RawInputWinFilter::inputThreadMain()
{
#if MP_TIMEPERIOD_1MS
    timeBeginPeriod(1); // 入力スレッドの寿命中だけ
#endif

    applyThreadQoS();

    // message-only window
    m_hinst = (HINSTANCE)::GetModuleHandleW(nullptr);
    const wchar_t kCls[] = L"MelonPrime_RI_CLASS_EVT_MMCSS_A_LIGHT";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &RawInputWinFilter::WndProcThunk;
    wc.hInstance = m_hinst;
    wc.lpszClassName = kCls;
    ::RegisterClassExW(&wc); // 既登録でもOK

    HWND h = ::CreateWindowExW(0, kCls, L"MelonPrime_RI_WINDOW_EVT_MMCSS_A_LIGHT", 0,
        0, 0, 0, 0,
        HWND_MESSAGE, nullptr, m_hinst, this);
    m_hwndMsg = h;

    if (!h) { m_thrRunning.store(true, std::memory_order_release); goto exit_thread; }
    if (!registerRawInput(h)) { ::DestroyWindow(h); m_thrRunning.store(true, std::memory_order_release); goto exit_thread; }

    m_thrRunning.store(true, std::memory_order_release);

    MSG msg;
    for (;;) {
        if (m_thrQuit.load(std::memory_order_acquire)) break;

        bool handledRaw = false;

        // 1) WM_INPUT は“外周1回”だけ処理（1件）
        if (::PeekMessageW(&msg, h, WM_INPUT, WM_INPUT, PM_REMOVE)) {
            const RAWINPUT* raw = nullptr;
            if (readRawInputToBuf(msg.lParam, raw)) handleRawInput(*raw);
            handledRaw = true;
        }

        // 2) その他のメッセージ（Translate不要）
        while (::PeekMessageW(&msg, h, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto exit_thread;
            ::DispatchMessageW(&msg);
        }

        // 3) バックログ検知：まだWM_INPUTが残っていれば軽く譲って即ループ（スピン防止）
        if (handledRaw && ::PeekMessageW(&msg, h, WM_INPUT, WM_INPUT, PM_NOREMOVE)) {
            ::Sleep(0); // 同優先度の他スレへ譲る（最短yield）
            continue;
        }

        // 4) 新規到着までブロック（※ MWMO_INPUTAVAILABLE は付けない）
        (void)::MsgWaitForMultipleObjectsEx(
            0, nullptr, INFINITE,
            QS_RAWINPUT | QS_POSTMESSAGE,
            MWMO_ALERTABLE);
    }

exit_thread:
    // MMCSS 離脱
    if (m_mmcssHandle && m_pAvRevertMmThreadCharacteristics) {
        (void)m_pAvRevertMmThreadCharacteristics(m_mmcssHandle);
        m_mmcssHandle = nullptr;
    }
    if (m_hAvrt) {
        ::FreeLibrary(m_hAvrt);
        m_hAvrt = nullptr;
        m_pAvSetMmThreadCharacteristicsW = nullptr;
        m_pAvRevertMmThreadCharacteristics = nullptr;
        m_pAvSetMmThreadPriority = nullptr;
    }
#if MP_TIMEPERIOD_1MS
    timeEndPeriod(1);
#endif
    return;
}

// ---------------- RawInput 読みラッパ（FastPath＋フォールバック） ----------------
FORCE_INLINE bool RawInputWinFilter::readRawInputToBuf(LPARAM lp, const RAWINPUT*& out) noexcept
{
#if MP_RAW_FAST_GETRAWINPUT
    UINT size = sizeof(m_rawBuf);
    UINT got = ::GetRawInputData((HRAWINPUT)lp, RID_INPUT, m_rawBuf, &size, sizeof(RAWINPUTHEADER));
    if (got == (UINT)-1) {
        // 典型：バッファ不足。size に必要サイズが入る（仕様）
        if (size == 0) return false;
        if (size <= sizeof(m_rawBuf)) return false; // 理論上ほぼ来ない
        m_dynRawBuf.resize(size);
        UINT sz2 = size;
        if (::GetRawInputData((HRAWINPUT)lp, RID_INPUT, m_dynRawBuf.data(), &sz2, sizeof(RAWINPUTHEADER)) == (UINT)-1)
            return false;
        out = reinterpret_cast<const RAWINPUT*>(m_dynRawBuf.data());
        return true;
    }
    // 成功（got==コピー済みバイト数）
    out = reinterpret_cast<const RAWINPUT*>(m_rawBuf);
    return true;
#else
    // 旧来の2段呼び（保険）
    UINT need = 0;
    if (::GetRawInputData((HRAWINPUT)lp, RID_INPUT, nullptr, &need, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return false;
    if (need > sizeof(m_rawBuf)) {
        m_dynRawBuf.resize(need);
        UINT sz = need;
        if (::GetRawInputData((HRAWINPUT)lp, RID_INPUT, m_dynRawBuf.data(), &sz, sizeof(RAWINPUTHEADER)) == (UINT)-1)
            return false;
        out = reinterpret_cast<const RAWINPUT*>(m_dynRawBuf.data());
        return true;
    }
    UINT sz = need;
    if (::GetRawInputData((HRAWINPUT)lp, RID_INPUT, m_rawBuf, &sz, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return false;
    out = reinterpret_cast<const RAWINPUT*>(m_rawBuf);
    return true;
#endif
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
    // 相対移動のみ
    if ((m.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
        const LONG dx_ = m.lLastX;
        const LONG dy_ = m.lLastY;
        if ((dx_ | dy_) != 0) {
            (void)m_dx.fetch_add((int32_t)dx_, std::memory_order_relaxed);
            (void)m_dy.fetch_add((int32_t)dy_, std::memory_order_relaxed);
        }
    }

    // ボタン
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

    // 左右修正（低コスト）
    switch (vk) {
    case VK_SHIFT: {
        const UINT sc = kb.MakeCode;
        vk = (sc == 0x36) ? VK_RSHIFT : (sc == 0x2A ? VK_LSHIFT : VK_SHIFT);
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

// ---------- Hotkey ----------
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
    const size_t n = (vks.size() > 16) ? 16 : vks.size(); // 過剰は切る
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
