#ifndef MELON_PRIME_RAW_WORKER_H
#define MELON_PRIME_RAW_WORKER_H

#ifdef _WIN32
#include <windows.h>
#include <atomic>

namespace MelonPrime {

    class InputState;

    class RawWorker final {
    public:
        explicit RawWorker(InputState& state) noexcept;
        ~RawWorker();

        RawWorker(const RawWorker&) = delete;
        RawWorker& operator=(const RawWorker&) = delete;

        void start();
        void stop() noexcept;

        [[nodiscard]] bool isRunning() const noexcept {
            return m_runThread.load(std::memory_order_acquire);
        }

    private:
        static unsigned __stdcall ThreadEntry(void* param) noexcept;
        void threadLoop() noexcept;
        static void RegisterRawDevices(HWND hwnd) noexcept;
        static void UnregisterRawDevices() noexcept;

        InputState& m_state;
        std::atomic<bool> m_runThread{ false };
        HANDLE m_hThread = nullptr;
        HWND m_hiddenWnd = nullptr;

        static ATOM s_windowClass;
        static std::atomic<LONG> s_classRefCount;
    };

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_RAW_WORKER_H