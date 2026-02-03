#ifndef MELON_PRIME_RAW_INPUT_WIN_FILTER_H
#define MELON_PRIME_RAW_INPUT_WIN_FILTER_H

#ifdef _WIN32
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QtGlobal>
#include <windows.h>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>

class InputState;
class RawWorker;

/**
 * @brief Singleton filter for processing raw input on Windows.
 *
 * Two modes of operation:
 * - Joy2Key mode: Processes input on Qt's main thread via native event filter
 * - Worker mode: Dedicated high-priority thread for lowest latency
 */
class RawInputWinFilter final : public QAbstractNativeEventFilter
{
public:
    /**
     * @brief Acquire reference to singleton instance.
     * @param joy2KeySupport Enable Joy2Key compatibility mode
     * @param mainHwnd Main window handle for raw input registration
     * @return Pointer to singleton (never null on success)
     */
    [[nodiscard]] static RawInputWinFilter* Acquire(bool joy2KeySupport, HWND mainHwnd);

    /**
     * @brief Release reference. Destroys singleton when refcount reaches zero.
     */
    static void Release() noexcept;

    /**
     * @brief Get current reference count (for debugging).
     */
    [[nodiscard]] static int RefCount() noexcept;

    // QAbstractNativeEventFilter
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // Mode control
    void setJoy2KeySupport(bool enable);
    [[nodiscard]] bool getJoy2KeySupport() const noexcept;
    void setRawInputTarget(HWND hwnd);

    // Hotkey bindings
    void clearAllBindings();
    void setHotkeyVks(int id, const std::vector<UINT>& vks);

    // Hotkey state queries
    [[nodiscard]] bool hotkeyDown(int id) const noexcept;
    [[nodiscard]] bool hotkeyPressed(int id) noexcept;
    [[nodiscard]] bool hotkeyReleased(int id) noexcept;

    // Mouse input
    void fetchMouseDelta(int& outX, int& outY) noexcept;
    void discardDeltas() noexcept;

    // State reset
    void resetAllKeys() noexcept;
    void resetMouseButtons() noexcept;
    void resetHotkeyEdges() noexcept;

private:
    RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd);
    ~RawInputWinFilter() override;

    // Non-copyable, non-movable
    RawInputWinFilter(const RawInputWinFilter&) = delete;
    RawInputWinFilter& operator=(const RawInputWinFilter&) = delete;

    void switchMode(bool toJoy2Key);
    
    static void RegisterRawDevices(HWND hwnd) noexcept;

    // Singleton state
    static RawInputWinFilter* s_instance;
    static std::atomic<int> s_refCount;
    static std::once_flag s_initFlag;

    // Components
    std::unique_ptr<InputState> m_state;
    std::unique_ptr<RawWorker> m_worker;

    // Configuration
    HWND m_mainHwnd = nullptr;
    std::atomic<bool> m_joy2KeySupport{ false };
};

#endif // _WIN32
#endif // MELON_PRIME_RAW_INPUT_WIN_FILTER_H
