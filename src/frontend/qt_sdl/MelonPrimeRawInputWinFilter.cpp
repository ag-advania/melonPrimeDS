#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"
#include "MelonPrimeInputSubscription.h"
#include "Platform.h"
#include <QCoreApplication>
#include <algorithm>
#include <cstdlib>

namespace MelonPrime {

    namespace {
        std::atomic_bool s_testRegisterFailureConsumed{false};

        [[nodiscard]] bool ShouldForceRawRegisterFailure() noexcept
        {
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
            const char* const value =
                std::getenv("MELONPRIME_TEST_FORCE_RAW_REGISTER_FAILURE");
            if (!value || value[0] == '\0' || value[0] == '0')
                return false;
            return !s_testRegisterFailureConsumed.exchange(
                true, std::memory_order_acq_rel);
#else
            return false;
#endif
        }
    }

    struct RawInputSubscription {
        explicit RawInputSubscription(MelonPrimeInputSubscription* ownerState, bool joy2Key, HWND hwnd)
            : owner(ownerState)
            , state(std::make_unique<InputState>())
            , windowHandle(hwnd)
            , joy2KeySupport(joy2Key)
        {}

        MelonPrimeInputSubscription* owner = nullptr;
        std::unique_ptr<InputState> state;
        HWND windowHandle = nullptr;
        HWND hiddenWindow = nullptr;
        DWORD hiddenWindowCreatorThreadId = 0;
        bool joy2KeySupport = false;
        bool qtFilterRequested = false;
        bool baselineReady = false;
    };

    std::mutex          RawInputWinFilter::s_serviceMutex;
    std::atomic<int>    RawInputWinFilter::s_refCount{ 0 };
    RawInputWinFilter* RawInputWinFilter::s_instance = nullptr;
    std::once_flag      RawInputWinFilter::s_initFlag;

    void RawInputWinFilter::InitializeApiFuncs() {
        std::call_once(s_initFlag, []() {
            WinInternal::ResolveNtApis();
            });
    }

    RawInputWinFilter* RawInputWinFilter::Acquire() {
        std::lock_guard<std::mutex> lock(s_serviceMutex);
        if (s_refCount.fetch_add(1) == 0) {
            s_instance = new RawInputWinFilter();
        }
        return s_instance;
    }

    void RawInputWinFilter::Release() {
        std::lock_guard<std::mutex> lock(s_serviceMutex);
        if (s_refCount.fetch_sub(1) == 1) {
            delete s_instance;
            s_instance = nullptr;
        }
    }

    RawInputWinFilter::RawInputWinFilter()
        : m_hwndQtTarget(nullptr)
        , m_joy2KeySupport(false)
        , m_isRegistered(false)
    {
        InputState::InitializeTables();
        InitializeApiFuncs();
    }

    RawInputWinFilter::~RawInputWinFilter() {
        if (m_qtFilterInstalled) {
            if (auto* app = QCoreApplication::instance())
                app->removeNativeEventFilter(this);
        }
        UnregisterDevices();
        for (const auto& subscription : m_subscriptions)
            (void)DestroyHiddenWindow(subscription.get());
        if (m_hiddenWindowClassOwned) {
            UnregisterClassW(
                L"MelonPrimeRawInputSink", GetModuleHandle(nullptr));
            m_hiddenWindowClassOwned = false;
        }
        m_hiddenWindowClassRegistered = false;
    }

    RawInputSubscription* RawInputWinFilter::Subscribe(
        MelonPrimeInputSubscription* owner, bool joy2KeySupport, HWND windowHandle)
    {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        auto subscription = std::make_unique<RawInputSubscription>(
            owner, joy2KeySupport, windowHandle);
        auto* result = subscription.get();
        m_subscriptions.push_back(std::move(subscription));
        return result;
    }

    void RawInputWinFilter::Unsubscribe(RawInputSubscription* subscription)
    {
        if (!subscription)
            return;
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        if (m_activeSubscription.load(std::memory_order_acquire) == subscription) {
            if (subscription->owner)
                PlatformInputOwnerService::Release(*subscription->owner);
            DeactivateActiveRegistration(subscription);
        }
        (void)DestroyHiddenWindow(subscription);
        m_subscriptions.erase(
            std::remove_if(m_subscriptions.begin(), m_subscriptions.end(),
                [subscription](const auto& item) { return item.get() == subscription; }),
            m_subscriptions.end());
    }

    InputState* RawInputWinFilter::StateFor(RawInputSubscription* subscription) const noexcept
    {
        return subscription ? subscription->state.get() : nullptr;
    }

    InputState* RawInputWinFilter::ActiveState() const noexcept
    {
        return StateFor(m_activeSubscription.load(std::memory_order_acquire));
    }

    bool RawInputWinFilter::UpdateOwner(RawInputSubscription* subscription, bool eligible)
    {
        // The emulation thread calls this on every frame. Once this
        // subscription is already active and still owns the process-wide
        // capture token, no registration or generation state can change, so
        // avoid taking the recursive mutex on the steady-state path. The
        // locked path below remains authoritative for focus/owner changes.
        if (subscription && subscription->owner && eligible
            && m_activeSubscription.load(std::memory_order_acquire) == subscription
            && PlatformInputOwnerService::IsOwner(*subscription->owner))
            return true;

        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        if (!subscription)
            return false;
        if (!subscription->owner)
            return false;
        const bool owns = PlatformInputOwnerService::Update(*subscription->owner, eligible);
        if (!owns) {
            if (m_activeSubscription.load(std::memory_order_acquire) == subscription) {
                DeactivateActiveRegistration(subscription);
            }
            return false;
        }

        auto* previous = m_activeSubscription.load(std::memory_order_acquire);
        if (previous != subscription) {
            if (previous)
                DeactivateActiveRegistration(previous);
            m_activeSubscription.store(subscription, std::memory_order_release);
            if (!ReconfigureActiveRegistration(subscription, true))
                return false;
        }
        return true;
    }

    bool RawInputWinFilter::ReconfigureActiveRegistration(
        RawInputSubscription* subscription, bool generationAlreadyAdvanced)
    {
        if (!subscription
            || m_activeSubscription.load(std::memory_order_acquire) != subscription)
            return false;

        // Registration change is an input-timeline boundary.
        // Raw mouse delta, button edge history and wheel impulses must enter the new
        // registration generation together. Never preserve edge history across a new
        // OS registration without syncing the physical baseline first.
        // Drain the old registration first, then make the new generation
        // not-ready until its physical baseline and edge history are synchronized.
        if (m_isRegistered
            && subscription->joy2KeySupport) {
            // Qt-target registration has no hidden queue to drain.
        }
        else if (m_isRegistered
            && subscription->hiddenWindowCreatorThreadId == GetCurrentThreadId()) {
            drainPendingMessages();
        }
        subscription->baselineReady = false;
        if (!generationAlreadyAdvanced && subscription->owner)
            PlatformInputOwnerService::BeginRegistrationGeneration(*subscription->owner);

        UnregisterDevices();
        if (!ApplyOwnerRegistration(subscription)) {
            // A failed native registration is not a usable Raw source. Clear
            // all transient state and release the process owner so the caller
            // immediately falls back to the Qt/panel source.
            subscription->state->discardDeltas();
            subscription->state->resetAll();
            subscription->baselineReady = false;
            m_activeSubscription.store(nullptr, std::memory_order_release);
            if (subscription->owner)
                PlatformInputOwnerService::Release(*subscription->owner);
            if (m_qtFilterInstalled) {
                if (auto* app = QCoreApplication::instance())
                    app->removeNativeEventFilter(this);
                m_qtFilterInstalled = false;
            }
            return false;
        }

        // No delta, button edge, deferred click, or wheel pulse from the old
        // registration may cross into this one. syncPhysicalState() establishes
        // the held baseline so a held XButton is down but not pressed.
        subscription->state->discardDeltas();
        subscription->state->resetAll();
        subscription->state->syncPhysicalState();
        subscription->baselineReady = true;
        return true;
    }

    void RawInputWinFilter::DeactivateActiveRegistration(
        RawInputSubscription* subscription)
    {
        if (!subscription
            || m_activeSubscription.load(std::memory_order_acquire) != subscription)
            return;

        // Registration change is an input-timeline boundary. Capture anything
        // still owned by the old registration before unregistering it, then
        // invalidate all host-side transient input state.
        if (m_isRegistered && subscription->joy2KeySupport) {
            // Qt-target registration has no hidden queue to drain.
        }
        else if (m_isRegistered
            && subscription->hiddenWindowCreatorThreadId == GetCurrentThreadId()) {
            drainPendingMessages();
        }
        subscription->baselineReady = false;
        if (subscription->owner)
            PlatformInputOwnerService::RequestRegistrationReset(*subscription->owner);
        UnregisterDevices();
        subscription->state->discardDeltas();
        subscription->state->resetAll();
        m_activeSubscription.store(nullptr, std::memory_order_release);
        if (m_qtFilterInstalled) {
            if (auto* app = QCoreApplication::instance())
                app->removeNativeEventFilter(this);
            m_qtFilterInstalled = false;
        }
    }

    bool RawInputWinFilter::ApplyOwnerRegistration(RawInputSubscription* subscription)
    {
        if (!subscription)
            return false;
        UnregisterDevices();
        m_hwndQtTarget = subscription->windowHandle;
        m_joy2KeySupport = subscription->joy2KeySupport;
        bool registered = false;
        if (m_joy2KeySupport) {
            registered = RegisterDevices(m_hwndQtTarget, false);
        } else {
            const bool windowReady = CreateHiddenWindow(subscription);
            registered = windowReady
                && RegisterDevices(subscription->hiddenWindow, true);
        }

        const bool wantQtFilter = registered
            && m_joy2KeySupport && subscription->qtFilterRequested;
        if (wantQtFilter != m_qtFilterInstalled) {
            if (auto* app = QCoreApplication::instance()) {
                if (wantQtFilter)
                    app->installNativeEventFilter(this);
                else
                    app->removeNativeEventFilter(this);
                m_qtFilterInstalled = wantQtFilter;
            }
        }
        return registered;
    }

    // =========================================================================
    // drainMessagesOnly — PeekMessage loop without GetRawInputBuffer.
    //
    // WARNING: Do NOT use this in DeferredDrain. P-35 attempted to use this
    // there but was reverted because GetRawInputBuffer and GetRawInputData
    // share internal buffer state (FIX-1). Without the prior GetRawInputBuffer
    // call from drainPendingMessages, PeekMessage dispatch can invalidate
    // HRAWINPUT handles → key-up events lost → stuck keys.
    //
    // This function is only safe where the caller ALREADY captured all pending
    // raw input via GetRawInputBuffer, or where data loss is acceptable.
    // =========================================================================
    FORCE_INLINE void RawInputWinFilter::drainMessagesOnly(
        RawInputSubscription* subscription) noexcept {
        const HWND hiddenWindow = subscription ? subscription->hiddenWindow : nullptr;
        if (!hiddenWindow)
            return;
        MSG msg;
        if (LIKELY(WinInternal::fnNtUserPeekMessage != nullptr)) {
            while (WinInternal::fnNtUserPeekMessage(
                &msg, hiddenWindow, WM_INPUT, WM_INPUT, PM_REMOVE, FALSE)) {}
        }
        else {
            while (PeekMessageW(
                &msg, hiddenWindow, WM_INPUT, WM_INPUT, PM_REMOVE)) {}
        }
    }

    // =========================================================================
    // REFACTORED (R1): drainPendingMessages -- shared WM_INPUT drain helper,
    // used by DeferredDrain() and resetAll(). The processRawInputBatched
    // (GetRawInputBuffer) call before the PeekMessage loop is required by the
    // FIX-1 shared-buffer semantics (see DeferredDrain banner below).
    // =========================================================================
    FORCE_INLINE void RawInputWinFilter::drainPendingMessages() noexcept {
        auto* const subscription =
            m_activeSubscription.load(std::memory_order_acquire);
        if (auto* state = ActiveState(); state && !m_joy2KeySupport) {
            state->processRawInputBatched();
        }
        drainMessagesOnly(subscription);
    }

    // =========================================================================
    // P-22: PollAndSnapshot — drain deferred to DeferredDrain().
    //
    // processRawInputBatched (GetRawInputBuffer) reads pending raw input
    // in batch. Any WM_INPUT dispatched later (by SDL or drain) is caught
    // by HiddenWndProc → processRawInput (P-19). So data is never lost
    // regardless of when draining happens.
    //
    // Deferring the drain removes 2-10 PeekMessage syscalls from the
    // latency-critical input→RunFrame path.
    // =========================================================================
    void RawInputWinFilter::PollAndSnapshot(
        RawInputSubscription* subscription,
        FrameHotkeyState& outHk, int& outMouseX, int& outMouseY,
        int& outWheelSteps)
    {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        auto* const state = StateFor(subscription);
        if (!state
            || m_activeSubscription.load(std::memory_order_acquire) != subscription
            || !subscription->owner
            || !PlatformInputOwnerService::IsOwner(*subscription->owner)) {
            outHk = {};
            outMouseX = 0;
            outMouseY = 0;
            outWheelSteps = 0;
            return;
        }

        if (!m_joy2KeySupport) {
            state->processRawInputBatched();
            // Drain deferred — see DeferredDrain()
        }

        state->snapshotInputFrame(outHk, outMouseX, outMouseY, outWheelSteps);
        outHk.generation = subscription->owner->generation;
        outHk.baselineReady = subscription->baselineReady;
    }

    // =========================================================================
    // V2: PollAndSnapshotNoEdges — re-entrant path helper.
    //
    // Same drain/capture semantics as PollAndSnapshot, but preserves hkPrev so
    // the next outer frame still sees press edges correctly.
    // =========================================================================
    void RawInputWinFilter::PollAndSnapshotNoEdges(
        RawInputSubscription* subscription,
        FrameHotkeyState& outHk, int& outMouseX, int& outMouseY,
        int& outWheelSteps)
    {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        auto* const state = StateFor(subscription);
        if (!state
            || m_activeSubscription.load(std::memory_order_acquire) != subscription
            || !subscription->owner
            || !PlatformInputOwnerService::IsOwner(*subscription->owner)) {
            outHk = {};
            outMouseX = 0;
            outMouseY = 0;
            outWheelSteps = 0;
            return;
        }

        if (!m_joy2KeySupport) {
            state->processRawInputBatched();
            // Drain deferred — see DeferredDrain()
        }

        state->snapshotInputFrameNoEdges(
            outHk, outMouseX, outMouseY, outWheelSteps);
        outHk.generation = subscription->owner->generation;
        outHk.baselineReady = subscription->baselineReady;
    }

    // =========================================================================
    // P-22 / P-32: DeferredDrain — drain WM_INPUT queue AFTER drawScreen.
    //
    // CRITICAL: drainPendingMessages (not drainMessagesOnly) is required here.
    //
    // GetRawInputBuffer and GetRawInputData may share an internal buffer
    // (FIX-1 "shared-buffer semantics"). When PeekMessage(PM_REMOVE) dispatches
    // WM_INPUT, the corresponding raw data becomes invisible to GetRawInputBuffer
    // and GetRawInputData may also fail if the buffer was already consumed by a
    // prior GetRawInputBuffer call.
    //
    // processRawInputBatched (GetRawInputBuffer) inside drainPendingMessages
    // rescues any raw input that arrived since PollAndSnapshot, BEFORE
    // PeekMessage can dispatch and potentially invalidate the data.
    // Without this safety net, key-up events can be lost → stuck keys.
    //
    // P-35 attempted to remove this call but was REVERTED because it caused
    // stuck keys under the shared-buffer scenario described in FIX-1.
    // The "extra" GetRawInputBuffer is the essential belt-and-suspenders
    // guard against data loss.
    // =========================================================================
    void RawInputWinFilter::DeferredDrain(RawInputSubscription* subscription) noexcept {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        auto* state = StateFor(subscription);
        if (!state
            || m_activeSubscription.load(std::memory_order_acquire) != subscription
            || !subscription->owner
            || !PlatformInputOwnerService::IsOwner(*subscription->owner))
            return;
        if (!m_joy2KeySupport) {
            drainPendingMessages();
        }
        // P-48: Stuck-state recovery moved here from snapshotInputFrame.
        // Runs in BOTH modes (joy2key included — only the message drain is
        // hidden-window-specific). Ordering matters: drain first so genuine
        // UP events captured above clear buttons normally before the
        // GetAsyncKeyState-based recovery scan runs.
        state->clearStuckPostFrame();
    }

    // =========================================================================
    // LateLatchMouseDelta — flush kernel buffer + fetch delta before aim write.
    //
    // Called just before ProcessAimInputMouse() to capture mouse events that
    // arrived after PollAndSnapshot (during morph/weapon/move processing).
    //
    // Joy2key path: events arrive via nativeEventFilter → already in accumulator;
    // processRawInputBatched is skipped.
    //
    // Hidden-window path: processRawInputBatched drains the kernel buffer first,
    // then fetchMouseDelta reads whatever HiddenWndProc or the batch collected.
    // =========================================================================
    void RawInputWinFilter::LateLatchMouseDelta(
        RawInputSubscription* subscription, int& accX, int& accY) noexcept {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        auto* state = StateFor(subscription);
        if (!state
            || m_activeSubscription.load(std::memory_order_acquire) != subscription
            || !subscription->owner
            || !PlatformInputOwnerService::IsOwner(*subscription->owner))
            return;
        if (!m_joy2KeySupport) {
            state->processRawInputBatched();
        }
        int lateX = 0, lateY = 0;
        state->fetchMouseDelta(lateX, lateY);
        accX += lateX;
        accY += lateY;
    }

    void RawInputWinFilter::setJoy2KeySupport(
        RawInputSubscription* subscription, bool enable) {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        if (!subscription || subscription->joy2KeySupport == enable)
            return;
        subscription->joy2KeySupport = enable;
        if (m_activeSubscription.load(std::memory_order_acquire) == subscription)
            (void)ReconfigureActiveRegistration(subscription, false);
    }

    void RawInputWinFilter::setRawInputTarget(
        RawInputSubscription* subscription, HWND hwnd) {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        if (!subscription || subscription->windowHandle == hwnd)
            return;
        subscription->windowHandle = hwnd;
        if (m_activeSubscription.load(std::memory_order_acquire) == subscription)
            (void)ReconfigureActiveRegistration(subscription, false);
    }

    void RawInputWinFilter::setQtFilterRequested(
        RawInputSubscription* subscription, bool enable) {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        if (!subscription || subscription->qtFilterRequested == enable)
            return;
        subscription->qtFilterRequested = enable;
        if (m_activeSubscription.load(std::memory_order_acquire) == subscription)
            (void)ReconfigureActiveRegistration(subscription, false);
    }

    bool RawInputWinFilter::CreateHiddenWindow(
        RawInputSubscription* subscription) {
        if (!subscription)
            return false;
        const DWORD currentThreadId = GetCurrentThreadId();
        if (subscription->hiddenWindow) {
            if (subscription->hiddenWindowCreatorThreadId == currentThreadId)
                return true;
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Warn,
                "[MelonPrime] refusing foreign-thread Raw hidden HWND reuse\n");
            return false;
        }

        if (!m_hiddenWindowClassRegistered) {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = HiddenWndProc;
            wc.hInstance = GetModuleHandle(nullptr);
            wc.lpszClassName = L"MelonPrimeRawInputSink";
            const ATOM atom = RegisterClassW(&wc);
            if (atom != 0) {
                m_hiddenWindowClassRegistered = true;
                m_hiddenWindowClassOwned = true;
            }
            else if (GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
                m_hiddenWindowClassRegistered = true;
            }
            else {
                return false;
            }
        }

        HINSTANCE const instance = GetModuleHandle(nullptr);
        HWND const hiddenWindow = CreateWindowW(
            L"MelonPrimeRawInputSink", L"", 0,
            0, 0, 0, 0,
            HWND_MESSAGE, nullptr, instance, subscription);
        if (!hiddenWindow)
            return false;

        subscription->hiddenWindow = hiddenWindow;
        subscription->hiddenWindowCreatorThreadId = currentThreadId;
        return true;
    }

    bool RawInputWinFilter::DestroyHiddenWindow(
        RawInputSubscription* subscription) {
        if (!subscription || !subscription->hiddenWindow)
            return true;
        if (subscription->hiddenWindowCreatorThreadId != GetCurrentThreadId()) {
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Warn,
                "[MelonPrime] refusing cross-thread Raw hidden HWND destroy\n");
            return false;
        }
        const HWND hiddenWindow = subscription->hiddenWindow;
        if (IsWindow(hiddenWindow))
            DestroyWindow(hiddenWindow);
        subscription->hiddenWindow = nullptr;
        subscription->hiddenWindowCreatorThreadId = 0;
        return true;
    }

    // =========================================================================
    // P-19: HiddenWndProc — process raw input on dispatch.
    //
    // Problem: SDL_JoystickUpdate calls PeekMessage(NULL, ..., PM_REMOVE)
    // which dispatches ALL pending messages including WM_INPUT. Once dispatched,
    // the message is REMOVED from the queue — GetRawInputBuffer can never see it.
    //
    // Old approach (return 0): Data lost. PeekMessage already consumed the message.
    //
    // Correct approach: Read the raw input data via processRawInput(HRAWINPUT)
    // before returning. This captures the data that would otherwise be lost.
    // DefWindowProcW is NOT called, so there's no double-read.
    //
    // No race condition:
    //   - GetRawInputBuffer reads first → message gone → PeekMessage skips it
    //   - PeekMessage dispatches first → processRawInput reads it → safe
    //
    // Both paths run on the emu thread (hidden window owned by emu thread),
    // so they serialize naturally — no concurrent access issue.
    // =========================================================================
    LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(
                hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        if (msg == WM_INPUT) {
            if (s_instance) {
                std::lock_guard<std::recursive_mutex> lock(
                    s_instance->m_subscriptionMutex);
                auto* const subscription = reinterpret_cast<RawInputSubscription*>(
                    GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (subscription
                    && s_instance->m_activeSubscription.load(
                        std::memory_order_acquire) == subscription) {
                    auto* const state = s_instance->StateFor(subscription);
                    if (state)
                        state->processRawInput(reinterpret_cast<HRAWINPUT>(lParam));
                }
            }
            return 0;
        }
        if (msg == WM_NCDESTROY) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool RawInputWinFilter::RegisterDevices(HWND target, bool useHiddenWindow) {
        if (m_isRegistered) return true;
        if (!target) {
            SetLastError(ERROR_INVALID_WINDOW_HANDLE);
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Warn,
                "[MelonPrime] Raw Input registration skipped: invalid target HWND "
                "error=%lu\n",
                static_cast<unsigned long>(GetLastError()));
            return false;
        }
        if (ShouldForceRawRegisterFailure()) {
            SetLastError(ERROR_ACCESS_DENIED);
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Warn,
                "[MelonPrime] Raw Input registration fault injection failed "
                "error=%lu\n",
                static_cast<unsigned long>(GetLastError()));
            return false;
        }
        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x02;
        rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x06;

        const DWORD flags = useHiddenWindow ? RIDEV_INPUTSINK : 0;
        rid[0].dwFlags = flags; rid[0].hwndTarget = target;
        rid[1].dwFlags = flags; rid[1].hwndTarget = target;

        if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE))) {
            const DWORD error = GetLastError();
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Warn,
                "[MelonPrime] Raw Input registration failed error=%lu\n",
                static_cast<unsigned long>(error));
            m_isRegistered = false;
            return false;
        }
        m_isRegistered = true;
        return true;
    }

    void RawInputWinFilter::UnregisterDevices() {
        if (!m_isRegistered) return;
        RAWINPUTDEVICE rid[2];
        rid[0] = { 0x01, 0x02, RIDEV_REMOVE, nullptr };
        rid[1] = { 0x01, 0x06, RIDEV_REMOVE, nullptr };
        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
        m_isRegistered = false;
    }

    bool RawInputWinFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_INPUT) {
            std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
            if (!m_joy2KeySupport)
                return false;
            if (auto* state = ActiveState())
                state->processRawInput(reinterpret_cast<HRAWINPUT>(msg->lParam));
        }
        return false;
    }

    // =========================================================================
    // R2: setHotkeyVks — added pointer+count overload for zero-allocation path.
    // =========================================================================
    void RawInputWinFilter::setHotkeyVks(
        RawInputSubscription* subscription, int id, const UINT* vks, size_t count) {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        if (auto* state = StateFor(subscription))
            state->setHotkeyVks(id, vks, count);
    }

    void RawInputWinFilter::setHotkeyVks(
        RawInputSubscription* subscription, int id, const std::vector<UINT>& vks) {
        setHotkeyVks(subscription, id, vks.data(), vks.size());
    }

    void RawInputWinFilter::discardDeltas(RawInputSubscription* subscription) {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        if (auto* state = StateFor(subscription)) state->discardDeltas();
    }
    // P-9: Combined reset — single call replaces resetAllKeys + resetMouseButtons.
    //
    // Hidden-window mode can have WM_INPUT messages queued after the last
    // snapshot. Drain/capture them before clearing state so an old DOWN cannot
    // be replayed into m_state on the next DeferredDrain/PollAndSnapshot cycle.
    void RawInputWinFilter::resetAll(RawInputSubscription* subscription)
    {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        if (m_activeSubscription.load(std::memory_order_acquire) == subscription
            && !m_joy2KeySupport
            && subscription
            && subscription->hiddenWindowCreatorThreadId == GetCurrentThreadId()) {
            drainPendingMessages();
        }
        if (auto* state = StateFor(subscription)) state->resetAll();
    }
    void RawInputWinFilter::resetHotkeyEdges(RawInputSubscription* subscription) {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        if (auto* state = StateFor(subscription)) state->resetHotkeyEdges();
    }
    void RawInputWinFilter::fetchMouseDelta(
        RawInputSubscription* subscription, int& outX, int& outY) {
        std::lock_guard<std::recursive_mutex> lock(m_subscriptionMutex);
        if (auto* state = StateFor(subscription)) state->fetchMouseDelta(outX, outY);
        else outX = outY = 0;
    }

} // namespace MelonPrime
#endif
