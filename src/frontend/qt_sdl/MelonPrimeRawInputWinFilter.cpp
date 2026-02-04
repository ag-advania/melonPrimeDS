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
        while (count > 0) {
            if (s_refCount.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return s_instance;
            }
        }

        int expected = 0;
        if (s_refCount.compare_exchange_strong(expected, -1, std::memory_order_acq_rel, std::memory_order_acquire)) {
            s_instance = new RawInputWinFilter(joy2KeySupport, mainHwnd);
            s_refCount.store(1, std::memory_order_release);
            return s_instance;
        }

        while ((count = s_refCount.load(std::memory_order_acquire)) < 0) {
            YieldProcessor();
        }

        s_refCount.fetch_add(1, std::memory_order_acq_rel);
        return s_instance;
    }

    void RawInputWinFilter::Release() noexcept {
        if (s_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            RawInputWinFilter* instance = s_instance;
            s_instance = nullptr;
            delete instance;
        }
    }

    int RawInputWinFilter::RefCount() noexcept {
        const int count = s_refCount.load(std::memory_order_acquire);
        return count > 0 ? count : 0;
    }

    RawInputWinFilter::RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd)
        : m_mainHwnd(mainHwnd), m_joy2KeySupport(joy2KeySupport)
    {
        std::call_once(s_initFlag, &InputState::InitializeTables);
        m_state = std::make_unique<InputState>();
        m_worker = std::make_unique<RawWorker>(*m_state);

        if (joy2KeySupport && m_mainHwnd) {
            RegisterRawDevices(m_mainHwnd);
        }
        else {
            m_worker->start();
        }
    }

    RawInputWinFilter::~RawInputWinFilter() {
        if (m_worker) m_worker->stop();
    }

    void RawInputWinFilter::setJoy2KeySupport(bool enable) {
        if (m_joy2KeySupport.load(std::memory_order_acquire) == enable) return;
        m_joy2KeySupport.store(enable, std::memory_order_release);
        switchMode(enable);
    }

    void RawInputWinFilter::switchMode(bool toJoy2Key) {
        if (toJoy2Key) {
            m_worker->stop();
            if (m_mainHwnd) RegisterRawDevices(m_mainHwnd);
        }
        else {
            m_worker->start();
        }
        m_state->resetAllKeys();
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

    void RawInputWinFilter::RegisterRawDevices(HWND hwnd) noexcept {
        if (!hwnd) return;
        const RAWINPUTDEVICE rid[2] = {
            { 0x01, 0x02, RIDEV_INPUTSINK, hwnd },
            { 0x01, 0x06, RIDEV_INPUTSINK, hwnd }
        };
        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
    }

    bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*) {
        if (!m_joy2KeySupport.load(std::memory_order_acquire)) return false;
        const auto* msg = static_cast<const MSG*>(message);
        if (!msg) return false;

        switch (msg->message) {
        case WM_ACTIVATEAPP:
            if (msg->wParam == FALSE) m_state->resetAllKeys();
            break;
        case WM_INPUT:
            m_state->processRawInput(reinterpret_cast<HRAWINPUT>(msg->lParam));
            break;
        }
        return false;
    }

    void RawInputWinFilter::clearAllBindings() { m_state->clearAllBindings(); }
    void RawInputWinFilter::setHotkeyVks(int id, const std::vector<UINT>& vks) { m_state->setHotkeyVks(id, vks); }
    bool RawInputWinFilter::hotkeyDown(int id) const noexcept { return m_state->hotkeyDown(id); }
    bool RawInputWinFilter::hotkeyPressed(int id) noexcept { return m_state->hotkeyPressed(id); }
    bool RawInputWinFilter::hotkeyReleased(int id) noexcept { return m_state->hotkeyReleased(id); }
    void RawInputWinFilter::fetchMouseDelta(int& outX, int& outY) noexcept { m_state->fetchMouseDelta(outX, outY); }
    void RawInputWinFilter::discardDeltas() noexcept { m_state->discardDeltas(); }
    void RawInputWinFilter::resetAllKeys() noexcept { m_state->resetAllKeys(); }
    void RawInputWinFilter::resetMouseButtons() noexcept { m_state->resetMouseButtons(); }
    void RawInputWinFilter::resetHotkeyEdges() noexcept { m_state->resetHotkeyEdges(); }

} // namespace MelonPrime
#endif