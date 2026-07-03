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
#include <unordered_map>

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

    // Per-source-device axis info. Filter-thread-only (no locking needed).
    //
    // Absolute pointing devices — most importantly VirtualBox's integrated
    // tablet pointer — report absolute positions in raw_values, not deltas.
    // Feeding those into the aim path as deltas produced a constant
    // bottom-right drift (positions are always positive, drained through the
    // residual clamp a few units per frame). Convert absolute axes to deltas
    // by differencing successive values per device. This is also immune to
    // XWarpPointer / VBox host-position re-sync: warping the cursor never
    // changes the device's own axis state.
    struct AxisState {
        bool known = false;
        bool absolute[2] = { false, false };
        bool hasLast[2] = { false, false };
        double last[2] = { 0.0, 0.0 };
    };
    std::unordered_map<int, AxisState> axisStates;   // key: sourceid
    std::atomic<bool> absBaseInvalid{ false };       // set by resetAll()

    static void QueryAxisModes(Display* dpy, int sourceid, AxisState& st)
    {
        st.known = true;
        int count = 0;
        XIDeviceInfo* info = XIQueryDevice(dpy, sourceid, &count);
        if (!info)
            return;
        for (int i = 0; i < count; ++i) {
            for (int c = 0; c < info[i].num_classes; ++c) {
                const XIAnyClassInfo* cls = info[i].classes[c];
                if (cls->type != XIValuatorClass)
                    continue;
                const auto* v = reinterpret_cast<const XIValuatorClassInfo*>(cls);
                if (v->number == 0 || v->number == 1)
                    st.absolute[v->number] = (v->mode == XIModeAbsolute);
            }
        }
        XIFreeDeviceInfo(info);
    }

    void AccumulateRawMotion(Display* dpy, const XIRawEvent* raw)
    {
        // resetAll() (focus loss / layout change) invalidates the absolute
        // baselines so the first event after a gap re-seeds instead of
        // producing one huge catch-up delta.
        if (absBaseInvalid.exchange(false, std::memory_order_acq_rel)) {
            for (auto& kv : axisStates) {
                kv.second.hasLast[0] = false;
                kv.second.hasLast[1] = false;
            }
        }

        AxisState& st = axisStates[raw->sourceid];
        if (!st.known)
            QueryAxisModes(dpy, raw->sourceid, st);

        // XInput2 reports one value per set bit in valuators.mask, in axis
        // order. Only axes 0 (X) and 1 (Y) are aim input; higher axes are
        // scroll wheel / tilt on many drivers and must never reach the aim
        // path (the previous fallback fed wheel motion into X).
        const double* values = raw->raw_values;
        double d[2] = { 0.0, 0.0 };
        for (int axis = 0; axis < raw->valuators.mask_len * 8; ++axis) {
            if (!TestBit(raw->valuators.mask, axis))
                continue;
            const double value = *values++;
            if (axis > 1)
                continue;
            if (st.absolute[axis]) {
                if (st.hasLast[axis])
                    d[axis] = value - st.last[axis];
                st.last[axis] = value;
                st.hasLast[axis] = true;
            } else {
                d[axis] = value;
            }
        }

        const int32_t dx = static_cast<int32_t>(std::llround(d[0]));
        const int32_t dy = static_cast<int32_t>(std::llround(d[1]));

        if ((dx | dy) != 0)
            receivedMotion.store(true, std::memory_order_release);
        if (dx != 0)
            accX.fetch_add(dx, std::memory_order_release);
        if (dy != 0)
            accY.fetch_add(dy, std::memory_order_release);
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
                        AccumulateRawMotion(display, raw);
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
    // Re-seed absolute-device baselines on the next event so a focus gap
    // cannot produce one huge catch-up delta.
    m->absBaseInvalid.store(true, std::memory_order_release);
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
