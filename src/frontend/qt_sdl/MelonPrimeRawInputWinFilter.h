#ifndef MELON_PRIME_RAW_INPUT_WIN_FILTER_H
#define MELON_PRIME_RAW_INPUT_WIN_FILTER_H

#ifdef _WIN32
#include <windows.h>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <QAbstractNativeEventFilter>
#include "MelonPrimeRawWinInternal.h"

#ifndef QS_RAWINPUT
#define QS_RAWINPUT 0x0400
#endif

namespace MelonPrime {

    class InputState;
    struct FrameHotkeyState;

    // =========================================================================
    // PeekMessageW-compatible function pointer type (5 args).
    // Used as the unified s_fnPeek signature — either resolves to
    // NtUserPeekMessage wrapper (bProcessSideEffects=FALSE, hook bypass)
    // or falls back to PeekMessageW.
    // =========================================================================
    using PeekMessageW_t = BOOL(WINAPI*)(
        LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);

    // =========================================================================
    // RawInputWinFilter — Low-latency raw input with two modes:
    //
    //   Joy2Key OFF (default):
    //     Dedicated input thread sleeps at 0% CPU via MsgWaitForMultipleObjectsEx.
    //     Wakes instantly on HID input, batch-reads via GetRawInputBuffer,
    //     commits to lock-free atomics. EmuThread reads atomics only.
    //
    //   Joy2Key ON:
    //     Qt native event filter intercepts WM_INPUT on the main thread.
    //     Required for compatibility with Joy2Key virtual key injection.
    //
    // Hot-loop design:
    //     s_fnWait / s_fnPeek are resolved once at startup (InitializeApiFuncs).
    //     The loop body contains zero conditional branches for API dispatch —
    //     only indirect calls through pre-resolved function pointers.
    // =========================================================================
    class RawInputWinFilter : public QAbstractNativeEventFilter {
    public:
        static RawInputWinFilter* Acquire(bool joy2KeySupport, void* windowHandle);
        static void Release();

        explicit RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd);
        ~RawInputWinFilter();

        // Qt event filter (Joy2Key ON mode only)
        bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

        void setJoy2KeySupport(bool enable);
        void setRawInputTarget(HWND hwnd);

        // Called from EmuThread every frame.
        //   Thread mode: focus-check + discard if unfocused.
        //   Joy2Key mode: no-op.
        void Poll();

        void discardDeltas();
        void setHotkeyVks(int id, const std::vector<UINT>& vks);
        void pollHotkeys(FrameHotkeyState& out);
        void resetAllKeys();
        void resetMouseButtons();
        void resetHotkeyEdges();
        void fetchMouseDelta(int& outX, int& outY);

    private:
        // --- Hidden window management ---
        void CreateHiddenWindow();
        void DestroyHiddenWindow();
        void RegisterDevices(HWND target, bool useInputSink);
        void UnregisterDevices();
        static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        // --- Dedicated input thread (Joy2Key OFF) ---
        void StartInputThread();
        void StopInputThread();
        void InputThreadProc();
        [[nodiscard]] bool CheckFocused() const;

        std::thread          m_inputThread;
        HANDLE               m_hStopEvent   = nullptr;
        std::atomic<bool>    m_threadReady{ false };
        std::atomic<bool>    m_stopThread{ false };

        // --- Pre-resolved API function pointers (zero branches in hot loop) ---
        //     Resolved once in InitializeApiFuncs(), never written again.
        static NtUserMsgWaitForMultipleObjectsEx_t  s_fnWait;   // MsgWaitForMultipleObjectsEx
        static PeekMessageW_t                        s_fnPeek;   // PeekMessageW or NtUserPeekMessage wrapper

        // --- Singleton / ref-count ---
        static std::atomic<int>    s_refCount;
        static RawInputWinFilter*  s_instance;
        static std::once_flag      s_initFlag;
        static void InitializeApiFuncs();

        // NtUserPeekMessage → PeekMessageW adapter (bProcessSideEffects=FALSE baked in)
        static BOOL WINAPI NtPeekAdapter(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);

        // --- Instance state ---
        std::unique_ptr<InputState> m_state;
        HWND  m_hwndQtTarget;      // Focus check target (Qt main window)
        HWND  m_hHiddenWnd;        // Message sink window (owned by input thread)
        bool  m_joy2KeySupport;
        bool  m_isRegistered;
    };

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_RAW_INPUT_WIN_FILTER_H
