#ifndef MELON_PRIME_RAW_WORKER_H
#define MELON_PRIME_RAW_WORKER_H

#ifdef _WIN32
#include <windows.h>
#include <atomic>

class InputState;

/**
 * @brief Dedicated worker thread for low-latency raw input processing.
 * 
 * Creates a message-only window on a high-priority thread pinned to
 * the last available CPU core for minimal interference with game logic.
 */
class RawWorker final {
public:
    explicit RawWorker(InputState& state) noexcept;
    ~RawWorker();

    // Non-copyable, non-movable
    RawWorker(const RawWorker&) = delete;
    RawWorker& operator=(const RawWorker&) = delete;
    RawWorker(RawWorker&&) = delete;
    RawWorker& operator=(RawWorker&&) = delete;

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
    
    // Shared window class management
    static ATOM s_windowClass;
    static std::atomic<LONG> s_classRefCount;
};

#endif // _WIN32
#endif // MELON_PRIME_RAW_WORKER_H
