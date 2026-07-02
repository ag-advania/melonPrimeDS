// MelonPrimeDS - Linux raw mouse input implementation (XInput2 RawMotion).

#ifdef __linux__

#include "MelonPrimeRawInputLinuxFilter.h"

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#include <atomic>
#include <cmath>
#include <mutex>
#include <poll.h>
#include <thread>

namespace MelonPrime {

struct LinuxRawInputFilter::Impl
{
    std::atomic<int32_t> accX{ 0 };
    std::atomic<int32_t> accY{ 0 };
    std::atomic<bool>    available{ false };
    std::atomic<bool>    quit{ false };

    std::thread thread;

    static bool TestBit(const unsigned char* mask, int bit)
    {
        return (mask[bit / 8] & (1 << (bit % 8))) != 0;
    }

    static void AccumulateRawMotion(Impl* self, const XIRawEvent* raw)
    {
        const double* values = raw->raw_values;
        int32_t dx = 0;
        int32_t dy = 0;

        for (int axis = 0; axis < raw->valuators.mask_len * 8; ++axis) {
            if (!TestBit(raw->valuators.mask, axis))
                continue;

            const double value = *values++;
            if (axis == 0)
                dx += static_cast<int32_t>(std::llround(value));
            else if (axis == 1)
                dy += static_cast<int32_t>(std::llround(value));
        }

        if (dx != 0)
            self->accX.fetch_add(dx, std::memory_order_release);
        if (dy != 0)
            self->accY.fetch_add(dy, std::memory_order_release);
    }

    void ThreadMain()
    {
        Display* display = XOpenDisplay(nullptr);
        if (!display)
            return;

        int xiOpcode = 0;
        int event = 0;
        int error = 0;
        if (!XQueryExtension(display, "XInputExtension", &xiOpcode, &event, &error)) {
            XCloseDisplay(display);
            return;
        }

        int major = 2;
        int minor = 0;
        if (XIQueryVersion(display, &major, &minor) != Success) {
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
            XCloseDisplay(display);
            return;
        }
        XFlush(display);
        available.store(true, std::memory_order_release);

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

void LinuxRawInputFilter::fetchMouseDelta(int32_t& outDx, int32_t& outDy)
{
    outDx = m->accX.exchange(0, std::memory_order_acquire);
    outDy = m->accY.exchange(0, std::memory_order_acquire);
}

void LinuxRawInputFilter::resetAll()
{
    m->accX.store(0, std::memory_order_release);
    m->accY.store(0, std::memory_order_release);
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
