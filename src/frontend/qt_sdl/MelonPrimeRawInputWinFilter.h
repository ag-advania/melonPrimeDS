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

    // =========================================================================
    // PeekMessageW-compatible function pointer type (5 args).
    // Resolves once to either NtPeekAdapter (hook bypass) or PeekMessageW.
    // Eliminates per-call branching in the Poll() hot path.
    // =========================================================================
    using PeekMessageW_t = BOOL(WINAPI*)(
        LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);

    // =========================================================================
    // RawInputWinFilter — Frame-synchronous raw input with two modes:
    //
    //   Joy2Key OFF (default):
    //     Poll() called every frame from EmuThread.
    //     Batch-reads via GetRawInputBuffer, drains WM_INPUT via s_fnPeek,
    //     safety-net re-read catches arrivals during drain.
    //     All API dispatch is through pre-resolved function pointers (zero branches).
    //
    //   Joy2Key ON:
    //     Qt native event filter intercepts WM_INPUT on the main thread.
    //
    // Poll() hot path (Joy2Key OFF):
    //   1. processRawInputBatched()      — main harvest from OS buffer
    //   2. s_fnPeek drain WM_INPUT       — remove stale messages (branchless)
    //   3. processRawInputBatched()      — safety-net re-read (near-zero on empty)
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

        // Frame-synchronous update — called from RunFrameHook every frame.
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
        void RegisterDevices(HWND target, bool useHiddenWindow);
        void UnregisterDevices();
        static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        // --- Pre-resolved API (zero branches in hot path) ---
        static PeekMessageW_t  s_fnPeek;  // NtPeekAdapter or PeekMessageW

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
        HWND  m_hHiddenWnd;        // Message sink window
        bool  m_joy2KeySupport;
        bool  m_isRegistered;
    };

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_RAW_INPUT_WIN_FILTER_H
