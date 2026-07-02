// MelonPrimeDS - macOS raw mouse input (RawInput-equivalent aim path)
//
// IOHIDManager-based unaccelerated mouse delta capture. This is the macOS
// counterpart of the Windows Raw Input aim path: it reads relative X/Y counts
// straight from the HID reports, so pointer acceleration and cursor-warp
// suppression never distort the aim delta.
//
// Scope (v1, intentional):
//   - Mouse X/Y deltas only. Buttons and keyboard stay on the existing
//     Qt event / SDL hotkey path (EmuInstance::onMousePress etc.), which
//     already maintains hotkeyMask. Feeding button edges from HID as well
//     would double-fire press edges against the Qt path; if HID buttons are
//     ever wanted, the Qt mouse-hotkey path must be gated off first.
//   - Threading: one writer (the HID runloop thread) accumulates deltas into
//     atomics; the emu thread fetches-and-clears once per frame at snapshot
//     time. This mirrors the Joy2Key-ON single-writer model of the Windows
//     filter, not its hidden-window batched drain.
//
// Permission: opening the HID manager requires the "Input Monitoring" TCC
// permission (macOS 10.15+). The first launch triggers the system prompt.
// When permission is denied or no mouse is present, isAvailable() stays
// false and callers must fall back to the QCursor delta path.

#ifndef MELONPRIME_RAW_INPUT_MAC_FILTER_H
#define MELONPRIME_RAW_INPUT_MAC_FILTER_H

#ifdef __APPLE__

#include <cstdint>

namespace MelonPrime {

class MacRawInputFilter
{
public:
    // Refcounted process-wide singleton (mirrors RawInputWinFilter::Acquire).
    static MacRawInputFilter* Acquire();
    static void Release();

    // True once the HID manager opened successfully (permission granted and
    // schedule on the runloop thread completed). May flip to true shortly
    // after startup; callers should check per-frame and fall back otherwise.
    bool isAvailable() const;

    // Fetch-and-clear the accumulated relative mouse delta.
    // Emu-thread only (frame-start snapshot semantics).
    void fetchMouseDelta(int32_t& outDx, int32_t& outDy);

    // Drop any accumulated delta (focus loss / emu start / layout change).
    void resetAll();

private:
    MacRawInputFilter();
    ~MacRawInputFilter();
    MacRawInputFilter(const MacRawInputFilter&) = delete;
    MacRawInputFilter& operator=(const MacRawInputFilter&) = delete;

    struct Impl;
    Impl* m;
};

} // namespace MelonPrime

#endif // __APPLE__
#endif // MELONPRIME_RAW_INPUT_MAC_FILTER_H
