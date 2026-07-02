// MelonPrimeDS - macOS raw mouse input implementation (see header for design).

#ifdef __APPLE__

#include "MelonPrimeRawInputMacFilter.h"

#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>

#include <atomic>
#include <thread>
#include <mutex>

namespace MelonPrime {

struct MacRawInputFilter::Impl
{
    // Writer: HID runloop thread. Reader: emu thread (exchange-to-zero).
    std::atomic<int32_t> accX{ 0 };
    std::atomic<int32_t> accY{ 0 };
    std::atomic<bool>    available{ false };
    std::atomic<bool>    quit{ false };

    IOHIDManagerRef manager = nullptr;
    CFRunLoopRef    runLoop = nullptr;   // set by the HID thread once running
    std::thread     thread;

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

        IOHIDElementRef elem = IOHIDValueGetElement(value);
        if (!elem) return;

        // Generic Desktop X/Y relative counts only. Buttons/wheel are
        // intentionally ignored (Qt event path owns them; see header).
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
        const IOReturn openResult = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
        if (openResult == kIOReturnSuccess) {
            available.store(true, std::memory_order_release);
        }
        // Even on kIOReturnNotPermitted keep the runloop alive: if the user
        // grants permission and restarts the app, the next launch succeeds.
        // (TCC does not deliver mid-process grants for IOHID reliably.)

        while (!quit.load(std::memory_order_acquire)) {
            // Run until Stop() wakes us; 1s timeout as a safety net so a lost
            // wakeup cannot hang shutdown.
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);
        }

        available.store(false, std::memory_order_release);
        IOHIDManagerUnscheduleFromRunLoop(manager, runLoop, kCFRunLoopDefaultMode);
        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
        CFRelease(manager);
        manager = nullptr;
        runLoop = nullptr;
    }

    void Start()
    {
        thread = std::thread([this] { ThreadMain(); });
    }

    void Stop()
    {
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
    outDx = m->accX.exchange(0, std::memory_order_acquire);
    outDy = m->accY.exchange(0, std::memory_order_acquire);
}

void MacRawInputFilter::resetAll()
{
    m->accX.store(0, std::memory_order_release);
    m->accY.store(0, std::memory_order_release);
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
