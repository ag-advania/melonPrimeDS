#ifndef MELON_PRIME_RAW_INPUT_WIN_FILTER_H
#define MELON_PRIME_RAW_INPUT_WIN_FILTER_H

#ifdef _WIN32
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <windows.h>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>

namespace MelonPrime {

    class InputState;
    class RawWorker;

    class RawInputWinFilter final : public QAbstractNativeEventFilter
    {
    public:
        static RawInputWinFilter* Acquire(bool joy2KeySupport, HWND mainHwnd);
        static void Release() noexcept;
        static int RefCount() noexcept;

        bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

        void setJoy2KeySupport(bool enable);
        [[nodiscard]] bool getJoy2KeySupport() const noexcept;
        void setRawInputTarget(HWND hwnd);

        void clearAllBindings();
        void setHotkeyVks(int id, const std::vector<UINT>& vks);

        [[nodiscard]] bool hotkeyDown(int id) const noexcept;
        [[nodiscard]] bool hotkeyPressed(int id) noexcept;
        [[nodiscard]] bool hotkeyReleased(int id) noexcept;

        void fetchMouseDelta(int& outX, int& outY) noexcept;
        void discardDeltas() noexcept;

        void resetAllKeys() noexcept;
        void resetMouseButtons() noexcept;
        void resetHotkeyEdges() noexcept;

    private:
        RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd);
        ~RawInputWinFilter() override;

        RawInputWinFilter(const RawInputWinFilter&) = delete;
        RawInputWinFilter& operator=(const RawInputWinFilter&) = delete;

        void switchMode(bool toJoy2Key);
        static void RegisterRawDevices(HWND hwnd) noexcept;

        static RawInputWinFilter* s_instance;
        static std::atomic<int> s_refCount;
        static std::once_flag s_initFlag;

        std::unique_ptr<InputState> m_state;
        std::unique_ptr<RawWorker> m_worker;

        HWND m_mainHwnd = nullptr;
        std::atomic<bool> m_joy2KeySupport{ false };
    };

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_RAW_INPUT_WIN_FILTER_H