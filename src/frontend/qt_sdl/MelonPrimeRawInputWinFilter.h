#ifndef MELON_PRIME_RAW_INPUT_WIN_FILTER_H
#define MELON_PRIME_RAW_INPUT_WIN_FILTER_H

#ifdef _WIN32
#include <windows.h>
#include <memory>
#include <vector>
#include <atomic>
#include <QAbstractNativeEventFilter>

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

        // --- Direct Polling Mode ---
        void poll();
        void discardDeltas();

        void setHotkeyVks(int id, const std::vector<UINT>& vks);
        void pollHotkeys(struct FrameHotkeyState& out);
        void resetAllKeys();
        void resetMouseButtons();
        void resetHotkeyEdges();
        void fetchMouseDelta(int& outX, int& outY);

    private:
        void CreateHiddenWindow();
        void DestroyHiddenWindow();
        void RegisterDevices();
        void UnregisterDevices();

        static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        static std::atomic<int> s_refCount;
        static RawInputWinFilter* s_instance;

        std::unique_ptr<InputState> m_state;
        HWND m_hwndQtTarget;
        HWND m_hHiddenWnd;
        bool m_joy2KeySupport;
        bool m_isRegistered;
    };

} // namespace MelonPrime
#endif // _WIN32
#endif