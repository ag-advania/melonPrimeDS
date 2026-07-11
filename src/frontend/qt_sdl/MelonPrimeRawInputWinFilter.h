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
    struct RawInputSubscription;
    struct MelonPrimeInputSubscription;

    class RawInputWinFilter : public QAbstractNativeEventFilter {
    public:
        static RawInputWinFilter* Acquire();
        static void Release();

        RawInputWinFilter();
        ~RawInputWinFilter();

        RawInputSubscription* Subscribe(MelonPrimeInputSubscription* owner, bool joy2KeySupport, HWND windowHandle);
        void Unsubscribe(RawInputSubscription* subscription);
        bool UpdateOwner(RawInputSubscription* subscription, bool eligible);

        bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

        void setJoy2KeySupport(RawInputSubscription* subscription, bool enable);
        void setRawInputTarget(RawInputSubscription* subscription, HWND hwnd);
        void setQtFilterRequested(RawInputSubscription* subscription, bool enable);

        // Merged Poll + snapshot in single call
        void PollAndSnapshot(RawInputSubscription* subscription, FrameHotkeyState& outHk, int& outMouseX, int& outMouseY);

        // Re-entrant path: same as PollAndSnapshot but does not advance hkPrev.
        void PollAndSnapshotNoEdges(RawInputSubscription* subscription, FrameHotkeyState& outHk, int& outMouseX, int& outMouseY);

        // P-22: Drain WM_INPUT queue after RunFrame (non-latency-critical).
        void DeferredDrain(RawInputSubscription* subscription) noexcept;

        // Late-latch: flush kernel buffer + fetch fresh delta just before aim write.
        void LateLatchMouseDelta(RawInputSubscription* subscription, int& accX, int& accY) noexcept;

        void discardDeltas(RawInputSubscription* subscription);

        // R2: Primary interface -- zero-allocation path from SmallVkList
        void setHotkeyVks(RawInputSubscription* subscription, int id, const UINT* vks, size_t count);

        // Compatibility overload (delegates to pointer+count)
        void setHotkeyVks(RawInputSubscription* subscription, int id, const std::vector<UINT>& vks);

        void resetAll(RawInputSubscription* subscription);
        void resetHotkeyEdges(RawInputSubscription* subscription);
        void fetchMouseDelta(RawInputSubscription* subscription, int& outX, int& outY);

    private:
        void CreateHiddenWindow();
        void DestroyHiddenWindow();
        void RegisterDevices(HWND target, bool useHiddenWindow);
        void UnregisterDevices();
        void ApplyOwnerRegistration(RawInputSubscription* subscription);
        InputState* StateFor(RawInputSubscription* subscription) const noexcept;
        InputState* ActiveState() const noexcept;
        static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        /// Drain pending WM_INPUT messages from the hidden window queue.
        /// Used by DeferredDrain() and resetAll(). Runs processRawInputBatched
        /// (GetRawInputBuffer) before the PeekMessage loop per FIX-1.
        void drainPendingMessages() noexcept;

        /// P-35 (REVERTED): PeekMessage-only drain (no GetRawInputBuffer).
        /// WARNING: Not safe for DeferredDrain — shared-buffer semantics
        /// require GetRawInputBuffer before PeekMessage. See FIX-1.
        void drainMessagesOnly() noexcept;

        static std::mutex          s_serviceMutex; // process-service: singleton lifecycle lock
        static std::atomic<int>    s_refCount; // process-service: collector subscription count
        static RawInputWinFilter* s_instance; // process-service: OS event collector
        static std::once_flag      s_initFlag; // process-service: immutable API resolution
        static void InitializeApiFuncs();

        std::vector<std::unique_ptr<RawInputSubscription>> m_subscriptions;
        std::atomic<RawInputSubscription*> m_activeSubscription{nullptr};
        std::recursive_mutex m_subscriptionMutex;
        HWND  m_hwndQtTarget;
        HWND  m_hHiddenWnd;
        bool  m_joy2KeySupport;
        bool  m_isRegistered;
        bool  m_qtFilterInstalled = false;
    };

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_RAW_INPUT_WIN_FILTER_H
