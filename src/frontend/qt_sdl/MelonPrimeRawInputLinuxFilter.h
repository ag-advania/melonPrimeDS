// MelonPrimeDS - Linux raw mouse input (RawInput-equivalent aim path)
//
// XInput2 RawMotion capture for X11 sessions. This mirrors the macOS HID path:
// only relative mouse X/Y deltas are captured here; buttons and keyboard stay
// on the existing Qt/SDL hotkey path to avoid duplicate press edges.
//
// Wayland does not expose global raw mouse motion to normal clients. On Wayland,
// XInput2 failure, or missing libXi support, callers fall back to the QCursor
// center-delta path.

#ifndef MELONPRIME_RAW_INPUT_LINUX_FILTER_H
#define MELONPRIME_RAW_INPUT_LINUX_FILTER_H

#ifdef __linux__

#include <cstdint>

namespace MelonPrime {

// Recenter the OS cursor for the fallback (QCursor-delta) aim path and after
// each consumed aim delta. Uses XWarpPointer on a thread-local Display so the
// emu thread can warp without marshaling to Qt's GUI thread (same role as
// MacWarpCursorGlobal). QCursor::setPos can fail to recenter under VirtualBox
// guest mouse integration or when invoked off the GUI thread — a failed
// recenter re-applies the cursor-minus-center delta every frame and spins aim.
void LinuxWarpCursorGlobal(int x, int y);

class LinuxRawInputFilter
{
public:
    static LinuxRawInputFilter* Acquire();
    static void Release();

    // Called by LinuxWarpCursorGlobal: re-seeds absolute-device baselines so
    // a warp that propagates into the pointer position (or its VirtualBox
    // re-sync) is never differenced into aim motion.
    static void NotifyCursorWarp();

    bool isAvailable() const;
    bool hasReceivedMotion() const;
    void fetchMouseDelta(int32_t& outDx, int32_t& outDy);
    void resetAll();

private:
    LinuxRawInputFilter();
    ~LinuxRawInputFilter();
    LinuxRawInputFilter(const LinuxRawInputFilter&) = delete;
    LinuxRawInputFilter& operator=(const LinuxRawInputFilter&) = delete;

    struct Impl;
    Impl* m;
};

} // namespace MelonPrime

#endif // __linux__
#endif // MELONPRIME_RAW_INPUT_LINUX_FILTER_H
