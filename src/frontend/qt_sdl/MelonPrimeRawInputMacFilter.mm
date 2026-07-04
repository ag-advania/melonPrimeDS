// MelonPrimeDS - macOS raw mouse input implementation (see header for design).
//
// Backend priority:
//   1. GCMouse (GameController framework, macOS 11+) — raw unaccelerated
//      deltas, delivered only while the app is frontmost, and requires NO
//      TCC permission (no Input Monitoring / Accessibility prompt). This is
//      the sanctioned game-input path and the default here.
//   2. IOHIDManager — global HID capture. Needs the Input Monitoring
//      permission, and for unsigned/ad-hoc dev builds the TCC grant breaks on
//      every rebuild (the grant is keyed to the old code signature), which in
//      practice made this backend silently unavailable. Kept as a fallback
//      for macOS < 11.
//   3. (Caller-side) QCursor center-delta fallback when neither is active.

#ifdef __APPLE__

#include "MelonPrimeRawInputMacFilter.h"

#import <GameController/GameController.h>
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdio>

namespace MelonPrime {

void MacWarpCursorGlobal(int x, int y)
{
    // No TCC permission required (unlike CGEventPost, which Qt's
    // QCursor::setPos uses and which needs Accessibility). See header.
    CGWarpMouseCursorPosition(CGPointMake(static_cast<CGFloat>(x),
                                          static_cast<CGFloat>(y)));
}

void MacSetAimCursorCaptured(bool captured)
{
    static bool sCaptured = false;
    if (captured == sCaptured)
        return;
    sCaptured = captured;
    if (captured) {
        CGAssociateMouseAndMouseCursorPosition(false);
        CGDisplayHideCursor(kCGDirectMainDisplay);
    } else {
        CGDisplayShowCursor(kCGDirectMainDisplay);
        CGAssociateMouseAndMouseCursorPosition(true);
    }
}

struct MacRawInputFilter::Impl
{
    // Writers: GC handler queue or HID runloop thread (one backend active at
    // a time). Reader: emu thread (P-48a load-first delta, single consumer).
    std::atomic<int32_t> accX{ 0 };
    std::atomic<int32_t> accY{ 0 };
    int32_t lastReadX = 0;
    int32_t lastReadY = 0;
    // GC deltas are float; carry the fractional part so slow motions are not
    // truncated away. Touched only from the GC handler queue.
    float gcFracX = 0.0f;
    float gcFracY = 0.0f;

    std::atomic<bool> available{ false };
    std::atomic<bool> quit{ false };

    // --- GCMouse backend ---
    // gcActive gates the IOHID callback so the two backends can never both
    // feed the accumulators (double-counted deltas).
    std::atomic<bool> gcActive{ false };
    std::atomic<bool> hidOpen{ false };
    bool usingGC = false;
    id gcConnectObserver = nil;
    id gcDisconnectObserver = nil;

    // --- IOHID fallback backend ---
    IOHIDManagerRef manager = nullptr;
    CFRunLoopRef    runLoop = nullptr;
    std::thread     thread;

    // ---------------- GCMouse ----------------

    void AccumulateGC(float dx, float dy)
    {
        gcFracX += dx;
        gcFracY += dy;
        const int32_t ix = static_cast<int32_t>(gcFracX); // trunc toward zero
        const int32_t iy = static_cast<int32_t>(gcFracY);
        if (ix != 0) { gcFracX -= static_cast<float>(ix); accX.fetch_add(ix, std::memory_order_release); }
        if (iy != 0) { gcFracY -= static_cast<float>(iy); accY.fetch_add(iy, std::memory_order_release); }
    }

    void AttachGCMouse(GCMouse* mouse) API_AVAILABLE(macos(11.0))
    {
        if (!mouse || !mouse.mouseInput) return;
        Impl* self = this;
        mouse.mouseInput.mouseMovedHandler =
            ^(GCMouseInput*, float deltaX, float deltaY) {
                // GameController uses +Y-up (thumbstick convention); the aim
                // pipeline expects screen convention (+Y = mouse moved down),
                // matching Windows Raw Input. Hence the Y negation.
                self->AccumulateGC(deltaX, -deltaY);
            };
    }

    void RecomputeAvailable()
    {
        available.store(gcActive.load(std::memory_order_acquire)
                        || hidOpen.load(std::memory_order_acquire),
                        std::memory_order_release);
    }

    bool StartGC()
    {
        if (@available(macOS 11.0, *)) {
            for (GCMouse* m in GCMouse.mice)
                AttachGCMouse(m);

            Impl* self = this;
            gcConnectObserver = [[NSNotificationCenter defaultCenter]
                addObserverForName:GCMouseDidConnectNotification
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification* note) {
                            if (@available(macOS 11.0, *)) {
                                self->AttachGCMouse((GCMouse*)note.object);
                                self->gcActive.store(true, std::memory_order_release);
                                self->RecomputeAvailable();
                                fprintf(stderr, "[MelonPrime] mac input: GCMouse connected\n");
                            }
                        }];
            gcDisconnectObserver = [[NSNotificationCenter defaultCenter]
                addObserverForName:GCMouseDidDisconnectNotification
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification*) {
                            if (@available(macOS 11.0, *)) {
                                if (GCMouse.mice.count == 0) {
                                    self->gcActive.store(false, std::memory_order_release);
                                    self->RecomputeAvailable();
                                }
                            }
                        }];

            usingGC = true;
            if (GCMouse.mice.count > 0) {
                gcActive.store(true, std::memory_order_release);
                RecomputeAvailable();
            }
            fprintf(stderr, "[MelonPrime] mac input: GCMouse backend (%lu mice)\n",
                    (unsigned long)GCMouse.mice.count);
            return true;
        }
        return false;
    }

    void StopGC()
    {
        if (@available(macOS 11.0, *)) {
            for (GCMouse* m in GCMouse.mice)
                if (m.mouseInput) m.mouseInput.mouseMovedHandler = nil;
        }
        if (gcConnectObserver)
            [[NSNotificationCenter defaultCenter] removeObserver:gcConnectObserver];
        if (gcDisconnectObserver)
            [[NSNotificationCenter defaultCenter] removeObserver:gcDisconnectObserver];
        gcConnectObserver = nil;
        gcDisconnectObserver = nil;
    }

    // ---------------- IOHID fallback ----------------

    static CFMutableDictionaryRef MatchingDict(uint32_t usagePage, uint32_t usage)
    {
        CFMutableDictionaryRef dict = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 2,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        if (!dict) return nullptr;
        CFNumberRef pageNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usagePage);
        CFNumberRef usageNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
        CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsagePageKey), pageNum);
        CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsageKey), usageNum);
        CFRelease(pageNum);
        CFRelease(usageNum);
        return dict;
    }

    static void InputValueCallback(void* context, IOReturn result,
                                   void* /*sender*/, IOHIDValueRef value)
    {
        if (result != kIOReturnSuccess) return;
        auto* self = static_cast<Impl*>(context);

        // GCMouse owns the accumulators while a GC mouse is connected —
        // prevents double-counting the same hardware motion.
        if (self->gcActive.load(std::memory_order_relaxed)) return;

        IOHIDElementRef elem = IOHIDValueGetElement(value);
        if (!elem) return;
        if (IOHIDElementGetUsagePage(elem) != kHIDPage_GenericDesktop) return;

        const uint32_t usage = IOHIDElementGetUsage(elem);
        if (usage != kHIDUsage_GD_X && usage != kHIDUsage_GD_Y) return;

        // Absolute-mode digitizers report non-relative X/Y; skip those so a
        // tablet or touchscreen cannot inject huge bogus deltas.
        if (!IOHIDElementIsRelative(elem)) return;

        const CFIndex delta = IOHIDValueGetIntegerValue(value);
        if (delta == 0) return;

        if (usage == kHIDUsage_GD_X)
            self->accX.fetch_add(static_cast<int32_t>(delta), std::memory_order_release);
        else
            self->accY.fetch_add(static_cast<int32_t>(delta), std::memory_order_release);
    }

    void ThreadMain()
    {
        // Grace period: if a GCMouse shows up (normal mouse users), never
        // open IOHID at all — that keeps the Input Monitoring permission
        // prompt from appearing for setups that do not need the fallback.
        for (int i = 0; i < 12 && !quit.load(std::memory_order_acquire); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (gcActive.load(std::memory_order_acquire)) {
                fprintf(stderr, "[MelonPrime] mac input: IOHID fallback not needed (GCMouse active)\n");
                return;
            }
        }
        if (quit.load(std::memory_order_acquire)) return;

        manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
        if (!manager) return;

        // Mice and pointer devices (some report as Pointer, e.g. trackballs).
        CFMutableDictionaryRef mouse = MatchingDict(kHIDPage_GenericDesktop, kHIDUsage_GD_Mouse);
        CFMutableDictionaryRef pointer = MatchingDict(kHIDPage_GenericDesktop, kHIDUsage_GD_Pointer);
        const void* dicts[2] = { mouse, pointer };
        CFArrayRef matching = CFArrayCreate(kCFAllocatorDefault, dicts, 2, &kCFTypeArrayCallBacks);
        IOHIDManagerSetDeviceMatchingMultiple(manager, matching);
        if (matching) CFRelease(matching);
        if (mouse) CFRelease(mouse);
        if (pointer) CFRelease(pointer);

        IOHIDManagerRegisterInputValueCallback(manager, InputValueCallback, this);

        runLoop = CFRunLoopGetCurrent();
        IOHIDManagerScheduleWithRunLoop(manager, runLoop, kCFRunLoopDefaultMode);

        // Triggers the Input Monitoring permission prompt on first use.
        IOReturn openResult = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
        fprintf(stderr, "[MelonPrime] mac input: IOHID backend open %s (0x%x)\n",
                openResult == kIOReturnSuccess ? "ok" : "failed",
                static_cast<unsigned>(openResult));
        if (openResult == kIOReturnSuccess) {
            hidOpen.store(true, std::memory_order_release);
            RecomputeAvailable();
        }

        // Retry while unavailable: a permission granted after launch can make
        // a later open succeed on some macOS versions (an app restart remains
        // the reliable path and stays documented).
        int retryCountdown = 3;
        while (!quit.load(std::memory_order_acquire)) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);
            if (openResult != kIOReturnSuccess && --retryCountdown <= 0) {
                retryCountdown = 3;
                openResult = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
                if (openResult == kIOReturnSuccess) {
                    fprintf(stderr, "[MelonPrime] mac input: IOHID backend recovered\n");
                    hidOpen.store(true, std::memory_order_release);
                    RecomputeAvailable();
                }
            }
        }

        hidOpen.store(false, std::memory_order_release);
        RecomputeAvailable();
        IOHIDManagerUnscheduleFromRunLoop(manager, runLoop, kCFRunLoopDefaultMode);
        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
        CFRelease(manager);
        manager = nullptr;
        runLoop = nullptr;
    }

    void Start()
    {
        StartGC();
        // Hybrid: the IOHID thread waits a grace period and only opens the
        // HID manager when no GCMouse is active (e.g. trackpad-only setups,
        // whose trackpads do expose relative Mouse-usage HID devices).
        thread = std::thread([this] { ThreadMain(); });
    }

    void Stop()
    {
        if (usingGC)
            StopGC();
        quit.store(true, std::memory_order_release);
        if (runLoop) CFRunLoopStop(runLoop);
        if (thread.joinable()) thread.join();
    }
};

MacRawInputFilter::MacRawInputFilter() : m(new Impl)
{
    m->Start();
}

MacRawInputFilter::~MacRawInputFilter()
{
    m->Stop();
    delete m;
}

bool MacRawInputFilter::isAvailable() const
{
    return m->available.load(std::memory_order_acquire);
}

void MacRawInputFilter::fetchMouseDelta(int32_t& outDx, int32_t& outDy)
{
    const int32_t curX = m->accX.load(std::memory_order_acquire);
    const int32_t curY = m->accY.load(std::memory_order_acquire);
    outDx = curX - m->lastReadX;
    outDy = curY - m->lastReadY;
    m->lastReadX = curX;
    m->lastReadY = curY;
}

void MacRawInputFilter::resetAll()
{
    m->accX.store(0, std::memory_order_release);
    m->accY.store(0, std::memory_order_release);
    m->lastReadX = 0;
    m->lastReadY = 0;
}

// ---------------------------------------------------------------------------
// Refcounted singleton (mirrors RawInputWinFilter::Acquire/Release).
// ---------------------------------------------------------------------------
namespace {
    std::mutex s_singletonMutex;
    MacRawInputFilter* s_instance = nullptr;
    int s_refCount = 0;
}

MacRawInputFilter* MacRawInputFilter::Acquire()
{
    std::lock_guard<std::mutex> lock(s_singletonMutex);
    if (!s_instance)
        s_instance = new MacRawInputFilter();
    ++s_refCount;
    return s_instance;
}

void MacRawInputFilter::Release()
{
    std::lock_guard<std::mutex> lock(s_singletonMutex);
    if (s_refCount > 0 && --s_refCount == 0) {
        delete s_instance;
        s_instance = nullptr;
    }
}

} // namespace MelonPrime

#endif // __APPLE__
