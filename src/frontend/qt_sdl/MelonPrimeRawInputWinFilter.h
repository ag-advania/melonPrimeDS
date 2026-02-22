#ifndef MELON_PRIME_RAW_INPUT_WIN_FILTER_H
#define MELON_PRIME_RAW_INPUT_WIN_FILTER_H

#ifdef _WIN32
#include <windows.h>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <QAbstractNativeEventFilter>
#include "MelonPrimeRawWinInternal.h"

namespace MelonPrime {

    class InputState;
    struct FrameHotkeyState;

    class RawInputWinFilter : public QAbstractNativeEventFilter {
    public:
        static RawInputWinFilter* Acquire(bool joy2KeySupport, void* windowHandle);
        static void Release();

        explicit RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd);
        ~RawInputWinFilter();

        bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

        void setJoy2KeySupport(bool enable);
        void setRawInputTarget(HWND hwnd);

        // Frame-synchronous update
        void Poll();

        // Merged Poll + snapshot in single call
        void PollAndSnapshot(FrameHotkeyState& outHk, int& outMouseX, int& outMouseY);

        void discardDeltas();

        // R2: Primary interface -- zero-allocation path from SmallVkList
        void setHotkeyVks(int id, const UINT* vks, size_t count);

        // Compatibility overload (delegates to pointer+count)
        void setHotkeyVks(int id, const std::vector<UINT>& vks);

        void pollHotkeys(FrameHotkeyState& out);
        void snapshotInputFrame(FrameHotkeyState& outHk, int& outMouseX, int& outMouseY);
        void resetAllKeys();
        void resetMouseButtons();
        void resetHotkeyEdges();
        void fetchMouseDelta(int& outX, int& outY);

    private:
        void CreateHiddenWindow();
        void DestroyHiddenWindow();
        void RegisterDevices(HWND target, bool useHiddenWindow);
        void UnregisterDevices();
        static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        /// Drain pending WM_INPUT messages from the hidden window queue.
        /// Shared between Poll() and PollAndSnapshot() to eliminate duplication.
        void drainPendingMessages() noexcept;

        static std::atomic<int>    s_refCount;
        static RawInputWinFilter* s_instance;
        static std::once_flag      s_initFlag;
        static void InitializeApiFuncs();

        std::unique_ptr<InputState> m_state;
        HWND  m_hwndQtTarget;
        HWND  m_hHiddenWnd;
        bool  m_joy2KeySupport;
        bool  m_isRegistered;
    };

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_RAW_INPUT_WIN_FILTER_H
