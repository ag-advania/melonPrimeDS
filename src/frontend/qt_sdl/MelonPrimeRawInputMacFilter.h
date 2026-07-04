// MelonPrimeDS - macOS raw mouse input (RawInput-equivalent aim path)
//
// Unaccelerated mouse delta capture, macOS counterpart of the Windows Raw
// Input aim path. Two backends (see the .mm for details):
//   1. GCMouse (GameController framework, macOS 11+) — raw deltas, delivered
//      while the app is frontmost, NO TCC permission required. Default.
//   2. IOHIDManager — global capture, requires the Input Monitoring TCC
//      permission (and unsigned dev builds lose the grant on every rebuild).
//      Fallback for macOS < 11.
//
// Scope (intentional):
//   - Mouse X/Y deltas only. Buttons and keyboard stay on the existing
//     Qt event / SDL hotkey path (EmuInstance::onMousePress etc.), which
//     already maintains hotkeyMask. Feeding button edges from here as well
//     would double-fire press edges against the Qt path; if raw buttons are
//     ever wanted, the Qt mouse-hotkey path must be gated off first.
//   - Threading: one writer (GC handler queue or the HID runloop thread)
//     accumulates deltas into atomics; the emu thread fetches-and-clears
//     once per frame at snapshot time.
//
// When no backend is active (no mouse / permission denied on the IOHID
// path), isAvailable() stays false and callers fall back to the QCursor
// center-delta path.

#ifndef MELONPRIME_RAW_INPUT_MAC_FILTER_H
#define MELONPRIME_RAW_INPUT_MAC_FILTER_H

#ifdef __APPLE__

#include <cstdint>

namespace MelonPrime {

// Warp the cursor to a global position WITHOUT requiring any TCC permission.
// Qt's QCursor::setPos is implemented with CGEventPost on macOS, which is
// silently dropped unless the app has the Accessibility permission — a failed
// recenter warp makes the QCursor fallback aim path spin continuously (the
// cursor-minus-center delta is re-applied every frame). This helper uses
// CGWarpMouseCursorPosition, which needs no permission. Qt global coordinates
// and CG global coordinates share the same top-left-origin point space.
void MacWarpCursorGlobal(int x, int y);

// Park the OS cursor for raw aim: disassociate hardware motion from cursor
// position and hide the cursor until capture is released (unclip / focus loss).
void MacSetAimCursorCaptured(bool captured);

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

    // True while a GCMouse device is connected (external mouse). Internal
    // trackpads use the IOHID fallback and must not use cursor disassociation.
    bool isGcMouseActive() const;

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
