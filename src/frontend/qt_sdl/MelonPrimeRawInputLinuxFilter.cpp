// MelonPrimeDS - Linux raw mouse input implementation (XInput2 RawMotion).

#ifdef __linux__

#include "MelonPrimeRawInputLinuxFilter.h"
#include "MelonPrimeInputSubscription.h"

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#if defined(MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY)
#include <cstdlib>
#endif
#include <cerrno>
#include <mutex>
#include <poll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>

#if defined(MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY)
// MELONPRIME_INPUT_DEBUG=1 enables 1 Hz pipeline diagnostics on stderr.
static bool MelonPrimeInputDebug()
{
    static const bool enabled = std::getenv("MELONPRIME_INPUT_DEBUG") != nullptr;
    return enabled;
}
#endif

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

    // Before the warp: the position jump (or VBox's follow-up re-sync) must
    // never be differenced into aim motion by the absolute-device path.
    LinuxRawInputFilter::NotifyCursorWarp();

    const Window root = DefaultRootWindow(s_warpDisplay);
    XWarpPointer(s_warpDisplay, None, root, 0, 0, 0, 0, x, y);
    XFlush(s_warpDisplay);
}

struct LinuxRawInputFilter::Impl
{
    static_assert(std::atomic<uint64_t>::is_always_lock_free,
        "Linux packed raw-motion publication requires lock-free uint64 atomics");
    static_assert(std::atomic<uint8_t>::is_always_lock_free,
        "Linux raw-input state publication requires a lock-free uint8 atomic");
    std::atomic<uint64_t> total{ 0 };
    std::atomic<uint8_t> stateBits{ 0 };
    std::atomic<bool>    quit{ false };
    // Filter-thread-only shadow avoids an atomic load on every XI2 event.
    bool receivedMotionPublished = false;

    int resetFd = -1;
    int wakeFd = -1;
    // Only used if the cold reset eventfd cannot be created, or if an
    // exceptional eventfd write fails. It is consumed at a filter-loop
    // boundary and is never read from AccumulateRawMotion.
    std::atomic_bool resetFallbackRequested{ false };
    std::atomic_bool resetFallbackActive{ false };

    std::thread thread;

    Impl()
    {
        resetFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (resetFd < 0)
            resetFallbackActive.store(true, std::memory_order_relaxed);
        wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    }

    ~Impl()
    {
        if (resetFd >= 0)
            close(resetFd);
        if (wakeFd >= 0)
            close(wakeFd);
    }

    static uint64_t PackTotals(uint32_t x, uint32_t y) noexcept
    {
        return static_cast<uint64_t>(x)
            | (static_cast<uint64_t>(y) << 32);
    }

    static uint32_t TotalX(uint64_t packed) noexcept
    {
        return static_cast<uint32_t>(packed);
    }

    static uint32_t TotalY(uint64_t packed) noexcept
    {
        return static_cast<uint32_t>(packed >> 32);
    }

    static bool TestBit(const unsigned char* mask, int bit)
    {
        return (mask[bit / 8] & (1 << (bit % 8))) != 0;
    }

    // MELONPRIME_LINUX_MOUSE_INPUT_HARDENING_V2
    static int32_t TakeIntegralDelta(double& residual, double delta)
    {
        residual += delta;
        const double integral = std::trunc(residual);
        residual -= integral;
        return static_cast<int32_t>(integral);
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
        // Absolute axes report device units (VBox tablet: 0..32767 across the
        // screen). Scale converts a device-unit diff into screen pixels so
        // sensitivity matches relative mice (~17x too fast otherwise —
        // observed as an instant pitch slam on game join).
        double scale[2] = { 1.0, 1.0 };
        // Preserve sub-pixel motion across XI_RawMotion events.
        double residual[2] = { 0.0, 0.0 };
    };
    std::unordered_map<int, AxisState> axisStates;   // key: sourceid
    int lastSourceId = -1;
    AxisState* lastSourceState = nullptr;

    void ResetAxisTransientState() noexcept
    {
        for (auto& kv : axisStates) {
            kv.second.hasLast[0] = false;
            kv.second.hasLast[1] = false;
            kv.second.residual[0] = 0.0;
            kv.second.residual[1] = 0.0;
        }
    }

    static bool SignalEventFd(int fd) noexcept
    {
        if (fd < 0)
            return false;

        constexpr std::uint64_t token = 1;
        for (;;) {
            const ssize_t written = write(fd, &token, sizeof(token));
            if (written == static_cast<ssize_t>(sizeof(token)))
                return true;
            if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return true; // An earlier signal is already pending.
            if (written < 0 && errno == EINTR)
                continue;
            return false;
        }
    }

    static bool DrainEventFd(int fd) noexcept
    {
        if (fd < 0)
            return false;

        bool signaled = false;
        for (;;) {
            std::uint64_t value = 0;
            const ssize_t count = read(fd, &value, sizeof(value));
            if (count == static_cast<ssize_t>(sizeof(value))) {
                signaled = true;
                continue;
            }
            if (count < 0 && errno == EINTR)
                continue;
            return signaled;
        }
    }

    void DrainResetMailbox() noexcept
    {
        bool resetRequested = DrainEventFd(resetFd);
        // The fallback RMW is cold and only exists when the eventfd path is
        // unavailable or has reported an exceptional write failure. A normal
        // fd-backed reset-none poll does not touch this atomic.
        if (resetFd < 0
            || resetFallbackActive.load(std::memory_order_acquire)) {
            resetRequested = resetFallbackRequested.exchange(
                false, std::memory_order_acq_rel) || resetRequested;
        }
        if (resetRequested)
            ResetAxisTransientState();
    }

    void RequestAxisReset() noexcept
    {
        if (SignalEventFd(resetFd))
            return;

        resetFallbackActive.store(true, std::memory_order_release);
        resetFallbackRequested.store(true, std::memory_order_release);
        // If reset eventfd creation/write failed, the wake fd still gives the
        // filter thread an immediate boundary without adding a normal-path
        // atomic poll or read.
        (void)SignalEventFd(wakeFd);
    }

    void DrainWakeMailbox() noexcept
    {
        (void)DrainEventFd(wakeFd);
    }

    static bool QueryAxisModes(Display* dpy, int sourceid, AxisState& st)
    {
        int count = 0;
        XIDeviceInfo* info = XIQueryDevice(dpy, sourceid, &count);
        if (!info)
            return false;

        st.absolute[0] = st.absolute[1] = false;
        st.scale[0] = st.scale[1] = 1.0;
        const int screen = DefaultScreen(dpy);
        const double screenDim[2] = {
            static_cast<double>(DisplayWidth(dpy, screen)),
            static_cast<double>(DisplayHeight(dpy, screen)),
        };
        for (int i = 0; i < count; ++i) {
            for (int c = 0; c < info[i].num_classes; ++c) {
                const XIAnyClassInfo* cls = info[i].classes[c];
                if (cls->type != XIValuatorClass)
                    continue;
                const auto* v = reinterpret_cast<const XIValuatorClassInfo*>(cls);
                if (v->number != 0 && v->number != 1)
                    continue;
                st.absolute[v->number] = (v->mode == XIModeAbsolute);
                if (st.absolute[v->number] && v->max > v->min)
                    st.scale[v->number] = screenDim[v->number] / (v->max - v->min);
            }
        }
        XIFreeDeviceInfo(info);
        st.known = true;
        return true;
    }

    void InvalidateAxisCapabilities()
    {
        // Hierarchy/device changes are cold. Clear every source so reused XI2
        // ids cannot inherit another device's mode, scale, or abs baseline.
        axisStates.clear();
        lastSourceId = -1;
        lastSourceState = nullptr;
    }

#if defined(MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY)
    // 1 Hz debug counters (filter thread only).
    long long dbgEvents = 0;
    double dbgSumX = 0.0, dbgSumY = 0.0;
    std::chrono::steady_clock::time_point dbgLast = std::chrono::steady_clock::now();
#endif

    void AccumulateRawMotion(Display* dpy, const XIRawEvent* raw)
    {
        // XInput event streams normally repeat one source. Keep the generic
        // map for arbitrary XI2 ids, but pay its hash only when the source
        // changes. unordered_map rehash preserves element references/pointers.
        if (raw->sourceid != lastSourceId || !lastSourceState) {
            lastSourceId = raw->sourceid;
            lastSourceState = &axisStates[raw->sourceid];
        }
        AxisState& st = *lastSourceState;
        if (!st.known) {
            const bool querySucceeded =
                QueryAxisModes(dpy, raw->sourceid, st);
            // Capability UNKNOWN is not equivalent to relative motion. Drop
            // this ambiguous event and retry the query on the next event.
            if (!querySucceeded)
                return;
#if defined(MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY)
            if (querySucceeded && MelonPrimeInputDebug())
                std::fprintf(stderr,
                    "[MelonPrime] linux input: raw source %d axis modes: X=%s Y=%s\n",
                    raw->sourceid,
                    st.absolute[0] ? "abs" : "rel",
                    st.absolute[1] ? "abs" : "rel");
#endif
        }

        // XInput2 reports one value per set bit in valuators.mask, in axis
        // order, in BOTH arrays: raw_values (untransformed device values) and
        // valuators.values (transformed). Only axes 0 (X) and 1 (Y) are aim
        // input; higher axes are scroll wheel / tilt on many drivers and must
        // never reach the aim path.
        //
        // For ABSOLUTE devices use valuators.values: some virtual pointers
        // (VirtualBox tablet) deliver zeros in raw_values while the real
        // position is only present in the transformed array. For RELATIVE
        // devices prefer raw_values (unaccelerated), falling back to the
        // transformed value when the raw one is zero.
        const double* rawVals = raw->raw_values;
        const double* xfVals  = raw->valuators.values;
        double d[2] = { 0.0, 0.0 };
        // raw_values/values are packed by set-bit order. Decode only through
        // Y: consuming a present X before Y preserves the packed pointer, and
        // no value after axis 1 can affect aim.
        const int axisLimit = std::min(2, raw->valuators.mask_len * 8);
        for (int axis = 0; axis < axisLimit; ++axis) {
            if (!TestBit(raw->valuators.mask, axis))
                continue;
            const double rawValue = *rawVals++;
            const double xfValue  = *xfVals++;
            if (axis > 1)
                continue;
            if (st.absolute[axis]) {
                const double value = (xfValue != 0.0) ? xfValue : rawValue;
                if (st.hasLast[axis]) {
                    const double diffPx = (value - st.last[axis]) * st.scale[axis];
                    // Teleport guard: a window re-entry / warp re-sync shows up
                    // as one huge positional jump. Re-seed instead of slamming
                    // the aim (observed: a single +18786-unit event pinned the
                    // pitch on game join).
                    if (diffPx > -300.0 && diffPx < 300.0)
                        d[axis] = diffPx;
                    else
                        st.residual[axis] = 0.0;
                }
                st.last[axis] = value;
                st.hasLast[axis] = true;
            } else {
                d[axis] = (rawValue != 0.0) ? rawValue : xfValue;
            }
        }

        const int32_t dx = TakeIntegralDelta(st.residual[0], d[0]);
        const int32_t dy = TakeIntegralDelta(st.residual[1], d[1]);

        if ((dx | dy) != 0 && !receivedMotionPublished) {
#if defined(MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY)
            if (MelonPrimeInputDebug())
                std::fprintf(stderr,
                    "[MelonPrime] linux input: first raw motion (src %d, dx=%d dy=%d)\n",
                    raw->sourceid, dx, dy);
#endif
            stateBits.store(
                static_cast<uint8_t>(LinuxRawInputFilter::StateAvailable
                    | LinuxRawInputFilter::StateMotionSeen),
                std::memory_order_release);
            receivedMotionPublished = true;
        }
        // One filter-thread writer publishes coherent modulo-32-bit X/Y in a
        // single lock-free atomic. The frame reader needs one acquire load.
        if ((dx | dy) != 0) {
            const uint64_t current = total.load(std::memory_order_relaxed);
            total.store(
                PackTotals(
                    TotalX(current) + static_cast<uint32_t>(dx),
                    TotalY(current) + static_cast<uint32_t>(dy)),
                std::memory_order_release);
        }

#if defined(MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY)
        if (MelonPrimeInputDebug()) {
            ++dbgEvents;
            dbgSumX += d[0];
            dbgSumY += d[1];
            const auto now = std::chrono::steady_clock::now();
            if (now - dbgLast >= std::chrono::seconds(1)) {
                std::fprintf(stderr,
                    "[MelonPrime] linux input: raw 1s: events=%lld sumX=%.1f sumY=%.1f\n",
                    dbgEvents, dbgSumX, dbgSumY);
                dbgEvents = 0; dbgSumX = dbgSumY = 0.0;
                dbgLast = now;
            }
        }
#endif
    }

    static constexpr int kMaxXiEventsPerChunk = 64;

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

        unsigned char rawMaskBits[(XI_LASTEVENT + 7) / 8] = {};
        unsigned char lifecycleMaskBits[(XI_LASTEVENT + 7) / 8] = {};
        XIEventMask masks[2]{};
        masks[0].deviceid = XIAllMasterDevices;
        masks[0].mask_len = sizeof(rawMaskBits);
        masks[0].mask = rawMaskBits;
        XISetMask(masks[0].mask, XI_RawMotion);
        masks[1].deviceid = XIAllDevices;
        masks[1].mask_len = sizeof(lifecycleMaskBits);
        masks[1].mask = lifecycleMaskBits;
        XISetMask(masks[1].mask, XI_HierarchyChanged);
        XISetMask(masks[1].mask, XI_DeviceChanged);

        const Window root = DefaultRootWindow(display);
        if (XISelectEvents(display, root, masks, 2) != Success) {
            std::fprintf(stderr,
                "[MelonPrime] linux input: XISelectEvents failed; using QCursor fallback\n");
            XCloseDisplay(display);
            return;
        }
        XFlush(display);
        stateBits.store(
            LinuxRawInputFilter::StateAvailable,
            std::memory_order_release);
        std::fprintf(stderr, "[MelonPrime] linux input: XInput2 RawMotion active\n");

        const int fd = ConnectionNumber(display);
        while (!quit.load(std::memory_order_acquire)) {
            // Keep a flood from starving lifecycle reset notifications. A
            // reset request arriving during this chunk is applied at the
            // following poll boundary, before the next chunk is decoded.
            int processed = 0;
            while (processed < kMaxXiEventsPerChunk
                && XPending(display) > 0) {
                XEvent ev;
                XNextEvent(display, &ev);
                ++processed;

                if (ev.xcookie.type != GenericEvent
                    || ev.xcookie.extension != xiOpcode)
                    continue;

                if (XGetEventData(display, &ev.xcookie)) {
                    if (ev.xcookie.evtype == XI_RawMotion) {
                        const auto* raw =
                            static_cast<const XIRawEvent*>(ev.xcookie.data);
                        if (raw)
                            AccumulateRawMotion(display, raw);
                    }
                    else if (ev.xcookie.evtype == XI_HierarchyChanged
                        || ev.xcookie.evtype == XI_DeviceChanged) {
                        InvalidateAxisCapabilities();
                    }
                    XFreeEventData(display, &ev.xcookie);
                }
            }

            const bool moreX = XPending(display) > 0;
            pollfd pollFds[3]{};
            pollFds[0].fd = fd;
            pollFds[0].events = POLLIN;
            int pollCount = 1;
            int resetPollIndex = -1;
            if (resetFd >= 0) {
                resetPollIndex = pollCount;
                pollFds[pollCount].fd = resetFd;
                pollFds[pollCount].events = POLLIN;
                ++pollCount;
            }
            int wakePollIndex = -1;
            if (wakeFd >= 0) {
                wakePollIndex = pollCount;
                pollFds[pollCount].fd = wakeFd;
                pollFds[pollCount].events = POLLIN;
                ++pollCount;
            }
            const int pollTimeout = moreX ? 0 : (wakeFd >= 0 ? -1 : 100);
            const int pollResult = poll(pollFds, pollCount, pollTimeout);
            if (pollResult > 0) {
                if (resetPollIndex >= 0
                    && (pollFds[resetPollIndex].revents
                        & (POLLIN | POLLERR | POLLHUP)) != 0)
                    DrainResetMailbox();
                if (wakePollIndex >= 0
                    && (pollFds[wakePollIndex].revents
                        & (POLLIN | POLLERR | POLLHUP)) != 0) {
                    DrainWakeMailbox();
                    if (resetFd < 0
                        || resetFallbackActive.load(std::memory_order_acquire))
                        DrainResetMailbox();
                }
            }
            else if (pollResult == 0 && resetFd < 0 && wakeFd < 0) {
                // Last-resort cold fallback if both eventfds failed. This is
                // bounded by the lifecycle poll timeout, never by XI events.
                DrainResetMailbox();
            }
        }

        stateBits.store(0, std::memory_order_release);
        XCloseDisplay(display);
    }

    void Start()
    {
        thread = std::thread([this] { ThreadMain(); });
    }

    void Stop()
    {
        quit.store(true, std::memory_order_release);
        if (!SignalEventFd(wakeFd))
            (void)SignalEventFd(resetFd);
        if (thread.joinable())
            thread.join();
    }
};

LinuxRawInputFilter::LinuxRawInputFilter() : m(new Impl)
{
    m->Start();
    for (int i = 0; i < 50; ++i) {
        if ((m->stateBits.load(std::memory_order_acquire)
                & StateAvailable) != 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

LinuxRawInputFilter::~LinuxRawInputFilter()
{
    m->Stop();
    delete m;
}

uint8_t LinuxRawInputFilter::stateBits() const noexcept
{
    return m->stateBits.load(std::memory_order_acquire);
}

void LinuxRawInputFilter::fetchMouseDelta(
    MelonPrimeInputSubscription& subscription, int32_t& outDx, int32_t& outDy)
{
    const uint64_t current = m->total.load(std::memory_order_acquire);
    const uint32_t curX = Impl::TotalX(current);
    const uint32_t curY = Impl::TotalY(current);
    if (subscription.cursorNeedsSync) {
        subscription.lastReadX = curX;
        subscription.lastReadY = curY;
        subscription.cursorNeedsSync = false;
        outDx = outDy = 0;
        return;
    }
    outDx = static_cast<int32_t>(
        curX - static_cast<uint32_t>(subscription.lastReadX));
    outDy = static_cast<int32_t>(
        curY - static_cast<uint32_t>(subscription.lastReadY));
    subscription.lastReadX = curX;
    subscription.lastReadY = curY;
}

void LinuxRawInputFilter::resetAll(MelonPrimeInputSubscription& subscription)
{
    const uint64_t current = m->total.load(std::memory_order_acquire);
    subscription.lastReadX = Impl::TotalX(current);
    subscription.lastReadY = Impl::TotalY(current);
    subscription.cursorNeedsSync = false;
    // receivedMotion is intentionally NOT cleared: it is a static property of
    // the session ("this X connection actually delivers raw motion") used to
    // gate raw-vs-fallback aim. Clearing it on every focus loss would flap
    // the aim source. Only the accumulators and abs baselines are transient.
    // Re-seed absolute-device baselines on the next event so a focus gap
    // cannot produce one huge catch-up delta.
    m->RequestAxisReset();
}

namespace {
    std::mutex s_singletonMutex;
    LinuxRawInputFilter* s_instance = nullptr;
    int s_refCount = 0;
}

void LinuxRawInputFilter::NotifyCursorWarp()
{
    std::lock_guard<std::mutex> lock(s_singletonMutex);
    if (s_instance)
        s_instance->m->RequestAxisReset();
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
