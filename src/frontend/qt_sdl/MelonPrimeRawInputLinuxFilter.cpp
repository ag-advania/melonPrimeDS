// MelonPrimeDS - Linux raw mouse input implementation (XInput2 RawMotion).

#ifdef __linux__

#include "MelonPrimeRawInputLinuxFilter.h"

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <poll.h>
#include <thread>

namespace MelonPrime {

void LinuxWarpCursorGlobal(int x, int y)
{
    static std::once_flag s_initOnce;
    static thread_local Display* s_warpDisplay = nullptr;

    std::call_once(s_initOnce, [] { (void)XInitThreads(); });

    if (!s_warpDisplay)
        s_warpDisplay = XOpenDisplay(nullptr);
    if (!s_warpDisplay)
        return;

    const Window root = DefaultRootWindow(s_warpDisplay);
    XWarpPointer(s_warpDisplay, None, root, 0, 0, 0, 0, x, y);
    XFlush(s_warpDisplay);
}

struct LinuxRawInputFilter::Impl
{
    std::atomic<int32_t> accX{ 0 };
    std::atomic<int32_t> accY{ 0 };
    std::atomic<bool>    available{ false };
    std::atomic<bool>    receivedMotion{ false };
    std::atomic<bool>    quit{ false };

    std::thread thread;

    static bool TestBit(const unsigned char* mask, int bit)
    {
        return (mask[bit / 8] & (1 << (bit % 8))) != 0;
    }

    static void AccumulateRawMotion(Impl* self, const XIRawEvent* raw)
    {
        const double* values = raw->raw_values;
        double axisX = 0.0;
        double axisY = 0.0;
        bool hasAxisX = false;
        bool hasAxisY = false;

        // XInput2 reports one value for each set bit in valuators.mask. Map
        // valuator 0/1 explicitly to X/Y so single-axis motion cannot turn a
        // Y-only event into horizontal aim.
        double firstRel[2] = { 0.0, 0.0 };
        int firstRelCount = 0;

        for (int axis = 0; axis < raw->valuators.mask_len * 8; ++axis) {
            if (!TestBit(raw->valuators.mask, axis))
                continue;

            const double value = *values++;
            if (firstRelCount < 2)
                firstRel[firstRelCount++] = value;

            if (axis == 0) {
                axisX += value;
                hasAxisX = true;
            } else if (axis == 1) {
                axisY += value;
                hasAxisY = true;
            }
        }

        const double dxRaw = (hasAxisX || hasAxisY) ? axisX : (firstRelCount >= 1 ? firstRel[0] : 0.0);
        const double dyRaw = (hasAxisX || hasAxisY) ? axisY : (firstRelCount >= 2 ? firstRel[1] : 0.0);
        const int32_t dx = static_cast<int32_t>(std::llround(dxRaw));
        const int32_t dy = static_cast<int32_t>(std::llround(dyRaw));

        if ((dx | dy) != 0)
            self->receivedMotion.store(true, std::memory_order_release);
        if (dx != 0)
            self->accX.fetch_add(dx, std::memory_order_release);
        if (dy != 0)
            self->accY.fetch_add(dy, std::memory_order_release);
    }

    void ThreadMain()
    {
        (void)XInitThreads();

        Display* display = XOpenDisplay(nullptr);
        if (!display) {
            std::fprintf(stderr,
                "[MelonPrime] linux input: XOpenDisplay failed; using QCursor fallback\n");
            return;
        }

        int xiOpcode = 0;
        int event = 0;
        int error = 0;
        if (!XQueryExtension(display, "XInputExtension", &xiOpcode, &event, &error)) {
            std::fprintf(stderr,
                "[MelonPrime] linux input: XInputExtension missing; using QCursor fallback\n");
            XCloseDisplay(display);
            return;
        }

        int major = 2;
        int minor = 0;
        if (XIQueryVersion(display, &major, &minor) != Success) {
            std::fprintf(stderr,
                "[MelonPrime] linux input: XIQueryVersion failed; using QCursor fallback\n");
            XCloseDisplay(display);
            return;
        }

        unsigned char maskBits[(XI_LASTEVENT + 7) / 8] = {};
        XIEventMask mask{};
        mask.deviceid = XIAllMasterDevices;
        mask.mask_len = sizeof(maskBits);
        mask.mask = maskBits;
        XISetMask(mask.mask, XI_RawMotion);

        const Window root = DefaultRootWindow(display);
        if (XISelectEvents(display, root, &mask, 1) != Success) {
            std::fprintf(stderr,
                "[MelonPrime] linux input: XISelectEvents(RawMotion) failed; using QCursor fallback\n");
            XCloseDisplay(display);
            return;
        }
        XFlush(display);
        available.store(true, std::memory_order_release);
        std::fprintf(stderr, "[MelonPrime] linux input: XInput2 RawMotion active\n");

        const int fd = ConnectionNumber(display);
        while (!quit.load(std::memory_order_acquire)) {
            while (XPending(display) > 0) {
                XEvent ev;
                XNextEvent(display, &ev);

                if (ev.xcookie.type != GenericEvent
                    || ev.xcookie.extension != xiOpcode
                    || ev.xcookie.evtype != XI_RawMotion)
                    continue;

                if (XGetEventData(display, &ev.xcookie)) {
                    const auto* raw = static_cast<const XIRawEvent*>(ev.xcookie.data);
                    if (raw)
                        AccumulateRawMotion(this, raw);
                    XFreeEventData(display, &ev.xcookie);
                }
            }

            pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN;
            (void)poll(&pfd, 1, 100);
        }

        available.store(false, std::memory_order_release);
        XCloseDisplay(display);
    }

    void Start()
    {
        thread = std::thread([this] { ThreadMain(); });
    }

    void Stop()
    {
        quit.store(true, std::memory_order_release);
        if (thread.joinable())
            thread.join();
    }
};

LinuxRawInputFilter::LinuxRawInputFilter() : m(new Impl)
{
    m->Start();
    for (int i = 0; i < 50; ++i) {
        if (m->available.load(std::memory_order_acquire))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

LinuxRawInputFilter::~LinuxRawInputFilter()
{
    m->Stop();
    delete m;
}

bool LinuxRawInputFilter::isAvailable() const
{
    return m->available.load(std::memory_order_acquire);
}

bool LinuxRawInputFilter::hasReceivedMotion() const
{
    return m->receivedMotion.load(std::memory_order_acquire);
}

void LinuxRawInputFilter::fetchMouseDelta(int32_t& outDx, int32_t& outDy)
{
    outDx = m->accX.exchange(0, std::memory_order_acquire);
    outDy = m->accY.exchange(0, std::memory_order_acquire);
}

void LinuxRawInputFilter::resetAll()
{
    m->accX.store(0, std::memory_order_release);
    m->accY.store(0, std::memory_order_release);
    m->receivedMotion.store(false, std::memory_order_release);
}

namespace {
    std::mutex s_singletonMutex;
    LinuxRawInputFilter* s_instance = nullptr;
    int s_refCount = 0;
}

LinuxRawInputFilter* LinuxRawInputFilter::Acquire()
{
    std::lock_guard<std::mutex> lock(s_singletonMutex);
    if (!s_instance)
        s_instance = new LinuxRawInputFilter();
    ++s_refCount;
    return s_instance;
}

void LinuxRawInputFilter::Release()
{
    std::lock_guard<std::mutex> lock(s_singletonMutex);
    if (s_refCount > 0 && --s_refCount == 0) {
        delete s_instance;
        s_instance = nullptr;
    }
}

} // namespace MelonPrime

#endif // __linux__
