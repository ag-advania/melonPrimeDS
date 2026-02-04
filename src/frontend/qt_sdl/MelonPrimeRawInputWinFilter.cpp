#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeInputState.h"
#include "MelonPrimeRawWorker.h"

namespace MelonPrime {

    RawInputWinFilter* RawInputWinFilter::s_instance = nullptr;
    std::atomic<int> RawInputWinFilter::s_refCount{ 0 };
    std::once_flag RawInputWinFilter::s_initFlag;

    RawInputWinFilter* RawInputWinFilter::Acquire(bool joy2KeySupport, HWND mainHwnd) {
        int count = s_refCount.load(std::memory_order_acquire);
        while (count >= 0) {
            if (count == 0) {
                int expected = 0;
                if (s_refCount.compare_exchange_strong(expected, -1, std::memory_order_acq_rel, std::memory_order_acquire)) {
                    try {
                        s_instance = new RawInputWinFilter(joy2KeySupport, mainHwnd);
                        s_refCount.store(1, std::memory_order_release);
                        return s_instance;
                    }
                    catch (...) {
                        s_refCount.store(0, std::memory_order_release);
                        throw;
                    }
                }
                count = s_refCount.load(std::memory_order_acquire);
            }
            else {
                if (s_refCount.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
                    while (!s_instance) YieldProcessor();
                    return s_instance;
                }
            }
        }
        return nullptr;
    }

    void RawInputWinFilter::Release() noexcept {
        if (s_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            RawInputWinFilter* ptr = s_instance;
            s_instance = nullptr;
            delete ptr;
        }
    }

    int RawInputWinFilter::RefCount() noexcept {
        int c = s_refCount.load(std::memory_order_acquire);
        return (c < 0) ? 0 : c;
    }

    RawInputWinFilter::RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd)
        : m_mainHwnd(mainHwnd), m_joy2KeySupport(joy2KeySupport)
    {
        std::call_once(s_initFlag, &InputState::InitializeTables);

        m_state = std::make_unique<InputState>();
        m_worker = std::make_unique<RawWorker>(*m_state);

        if (m_joy2KeySupport) {
            if (m_mainHwnd) RegisterRawDevices(m_mainHwnd);
        }
        else {
            m_worker->start();
        }
    }

    RawInputWinFilter::~RawInputWinFilter() {
        if (m_worker) m_worker->stop();
        UnregisterRawDevices();
    }

    void RawInputWinFilter::setJoy2KeySupport(bool enable) {
        if (m_joy2KeySupport.load(std::memory_order_acquire) == enable) return;
        m_joy2KeySupport.store(enable, std::memory_order_release);
        switchMode(enable);
    }

    bool RawInputWinFilter::getJoy2KeySupport() const noexcept {
        return m_joy2KeySupport.load(std::memory_order_acquire);
    }

    void RawInputWinFilter::setRawInputTarget(HWND hwnd) {
        if (m_mainHwnd == hwnd) return;
        m_mainHwnd = hwnd;
        if (m_joy2KeySupport.load(std::memory_order_acquire) && m_mainHwnd) {
            RegisterRawDevices(m_mainHwnd);
        }
    }

    void RawInputWinFilter::switchMode(bool toJoy2Key) {
        if (toJoy2Key) {
            m_worker->stop();
            if (m_mainHwnd) RegisterRawDevices(m_mainHwnd);
        }
        else {
            UnregisterRawDevices();
            m_worker->start();
        }
        m_state->resetAllKeys();
        m_state->discardDeltas();
    }

    void RawInputWinFilter::RegisterRawDevices(HWND hwnd) noexcept {
        if (!hwnd) return;
        const RAWINPUTDEVICE rid[2] = {
            { 0x01, 0x02, RIDEV_INPUTSINK, hwnd },
            { 0x01, 0x06, RIDEV_INPUTSINK, hwnd }
        };
        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
    }

    void RawInputWinFilter::UnregisterRawDevices() noexcept {
        const RAWINPUTDEVICE rid[2] = {
            { 0x01, 0x02, RIDEV_REMOVE, nullptr },
            { 0x01, 0x06, RIDEV_REMOVE, nullptr }
        };
        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
    }

    bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*) {
        const MSG* msg = static_cast<const MSG*>(message);
        if (!msg) return false;

        // フォーカス喪失時のリセット処理
        if (msg->message == WM_ACTIVATEAPP) {
            if (msg->wParam == FALSE) {
                m_state->resetAllKeys();
                m_state->resetMouseButtons();
                m_state->discardDeltas();
            }
            return false;
        }

        // Joy2Keyモード（メインスレッド処理）の時だけ処理
        if (m_joy2KeySupport.load(std::memory_order_relaxed)) {
            if (msg->message == WM_INPUT) {
                // 【修正】GetRawInputBuffer ではなく、lParam を使って個別に処理する
                // nativeEventFilter に来た時点でメッセージはキューから出ているため、
                // バッファ取得API(GetRawInputBuffer)ではこの入力を取得できません。
                // 確実に lParam (HRAWINPUT) を渡して処理する必要があります。
                m_state->processRawInput(reinterpret_cast<HRAWINPUT>(msg->lParam));
            }
        }

        return false;
    }

    void RawInputWinFilter::fetchMouseDelta(int& outX, int& outY) noexcept { m_state->fetchMouseDelta(outX, outY); }
    void RawInputWinFilter::discardDeltas() noexcept { m_state->discardDeltas(); }
    void RawInputWinFilter::resetAllKeys() noexcept { m_state->resetAllKeys(); }
    void RawInputWinFilter::resetMouseButtons() noexcept { m_state->resetMouseButtons(); }
    void RawInputWinFilter::resetHotkeyEdges() noexcept { m_state->resetHotkeyEdges(); }
    void RawInputWinFilter::clearAllBindings() { m_state->clearAllBindings(); }
    void RawInputWinFilter::setHotkeyVks(int id, const std::vector<UINT>& vks) { m_state->setHotkeyVks(id, vks); }
    bool RawInputWinFilter::hotkeyDown(int id) const noexcept { return m_state->hotkeyDown(id); }
    bool RawInputWinFilter::hotkeyPressed(int id) noexcept { return m_state->hotkeyPressed(id); }
    bool RawInputWinFilter::hotkeyReleased(int id) noexcept { return m_state->hotkeyReleased(id); }

} // namespace MelonPrime
#endif