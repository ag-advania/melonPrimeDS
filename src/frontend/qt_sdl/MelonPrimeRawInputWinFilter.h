#ifndef MELON_PRIME_RAW_INPUT_WIN_FILTER_H
#define MELON_PRIME_RAW_INPUT_WIN_FILTER_H

#ifdef _WIN32
#include <windows.h>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
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

        // Qtのイベントフィルタ (Joy2Key ON時のみ機能)
        bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

        void setJoy2KeySupport(bool enable);
        void setRawInputTarget(HWND hwnd);

        // --- Direct Polling Mode (Threaded) ---
        // スレッドが勝手に更新するため、メインスレッドからは何もしないが互換性維持のため残す
        void poll();

        void discardDeltas();
        void setHotkeyVks(int id, const std::vector<UINT>& vks);
        void pollHotkeys(struct FrameHotkeyState& out);
        void resetAllKeys();
        void resetMouseButtons();
        void resetHotkeyEdges();
        void fetchMouseDelta(int& outX, int& outY);

    private:
        // スレッド関連
        void StartWorkerThread();
        void StopWorkerThread();
        void InputThreadProc(); // スレッドのエントリーポイント

        // 内部ヘルパー
        void CreateHiddenWindow(); // スレッド内で呼ぶこと
        void DestroyHiddenWindow();
        void RegisterDevices(HWND target, bool isThreaded);
        void UnregisterDevices();

        static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        static std::atomic<int> s_refCount;
        static RawInputWinFilter* s_instance;

        std::unique_ptr<InputState> m_state;
        HWND m_hwndQtTarget;     // メインウィンドウ (Joy2Key用)
        HWND m_hHiddenWnd;       // スレッド専用ウィンドウ (Direct用)

        bool m_joy2KeySupport;
        bool m_isRegistered;

        // スレッド同期用
        std::thread m_workerThread;
        std::atomic<bool> m_stopThread{ false };
        std::mutex m_startupMutex;
        std::condition_variable m_startupCv;
        bool m_threadInitialized = false;
    };

} // namespace MelonPrime
#endif // _WIN32
#endif