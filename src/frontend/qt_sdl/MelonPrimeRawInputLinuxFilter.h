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

class LinuxRawInputFilter
{
public:
    static LinuxRawInputFilter* Acquire();
    static void Release();

    bool isAvailable() const;
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
