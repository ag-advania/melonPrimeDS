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
#include "MelonPrimeRawWinInternal.h" // NtUserMsgWaitForMultipleObjectsEx_t の定義用

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

        void discardDeltas();
        void setHotkeyVks(int id, const std::vector<UINT>& vks);
        void pollHotkeys(struct FrameHotkeyState& out);
        void resetAllKeys();
        void resetMouseButtons();
        void resetHotkeyEdges();
        void fetchMouseDelta(int& outX, int& outY);

    private:
        void StartWorkerThread();
        void StopWorkerThread();
        void InputThreadProc();

        void CreateHiddenWindow();
        void DestroyHiddenWindow();
        void RegisterDevices(HWND target, bool isThreaded);
        void UnregisterDevices();

        static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        static std::atomic<int> s_refCount;
        static RawInputWinFilter* s_instance;

        // 最適な待機関数を保持する関数ポインタ
        static NtUserMsgWaitForMultipleObjectsEx_t s_fnWait;

        // 最適な PeekMessage を保持する関数ポインタ
        // PeekMessageW 互換シグネチャ (5引数) に統一
        using PeekMessageFunc_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
        static PeekMessageFunc_t s_fnPeek;

        static std::once_flag s_initFlag;
        static void InitializeApiFuncs();

        std::unique_ptr<InputState> m_state;
        HWND m_hwndQtTarget;
        HWND m_hHiddenWnd;
        bool m_joy2KeySupport;
        bool m_isRegistered;

        std::thread m_workerThread;
        std::atomic<bool> m_stopThread{ false };
        HANDLE m_hStopEvent = nullptr;

        std::mutex m_startupMutex;
        std::condition_variable m_startupCv;
        bool m_threadInitialized = false;
    };

} // namespace MelonPrime
#endif // _WIN32
#endif