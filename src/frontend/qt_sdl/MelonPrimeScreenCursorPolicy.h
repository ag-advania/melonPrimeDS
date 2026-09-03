#pragma once

#include <cstdint>

class ScreenPanel;

namespace MelonPrime::ScreenCursorPolicy {

// MELONPRIME_AIM_CAPTURE_REQUEST_VS_ACTIVE_V1
// Aim capture has two separable concerns, and conflating them is a real bug
// source: the request is what gives the emulation thread its input device,
// while the active state is only what the OS cursor does about it.
//
//   request  (persistent)   RequestAimCapture   <-> Unclip
//   active   (transient)    ReconcileAimCapture <-> Suspend
//
// The request publishes clipWanted -> captureWanted, which is what
// ShouldOwnRelativeAimInput() consults to hand Raw Input to this instance.
// Skipping it because a particular capture wants no cursor confinement leaves
// relative mouse aim with no owner at all. Reconciliation performs no request
// write, so a repeated pass costs no publication.

// What an active aim capture does to the OS cursor. The request above is
// identical in both modes; only this differs.
enum class AimConfinement : uint8_t
{
    // Relative devices. The pointer position carries no information, so pin it
    // where it can neither leave the window nor be clicked through.
    CenterPin = 0,
    // Absolute devices (pen, injected absolute pointer). The pointer position
    // *is* the signal: pinning it to one pixel would flatten every delta to
    // zero. Bound it to the aim area instead.
    AimAreaBounds,
};

// Publishes the persistent aim-capture request, then reconciles once so a
// press edge takes effect within the same GUI turn.
void RequestAimCapture(ScreenPanel& panel);

// Applies presentation and platform confinement for the request that is
// already standing. Never writes the request itself.
void ReconcileAimCapture(ScreenPanel& panel);

void UpdateClipIfNeeded(ScreenPanel& panel);

// Clears the aim capture request and releases the active platform state.
void Unclip(ScreenPanel& panel);

// Temporarily release the active platform capture while preserving the aim
// capture request. Used for focus loss, modal windows, hide/show, and
// parent/window-state transitions.
void Suspend(ScreenPanel& panel);

void ContainAimCursorIfNeeded(ScreenPanel& panel);

// Unconditional cursor-state release for panel shutdown. Unlike Unclip(),
// this does not early-return on isClosingForMelonPrime()/qApp closing-down —
// it is meant to be called from ScreenPanel::beginClose() itself (after
// `closing` is already set), so it must still run.
void ReleaseForClose(ScreenPanel& panel);

// Confines the OS cursor to the bottom-screen widget rect (Windows only;
// other platforms only set the arrow cursor). Called from
// ScreenPanel::clipCursorToBottomScreen(), itself only reachable via
// clipCursorToBottomScreenForPolicy() -> ScreenCursorPolicy::UpdateClipIfNeeded().
void ConfineToBottomScreen(ScreenPanel& panel);

// Confines the OS cursor to the top-screen widget rect for the stylus-mode
// match options (Windows only; other platforms only set the cursor shape, as
// with ConfineToBottomScreen). Unlike RequestAimCapture() this never sets the
// aim capture request, grabs or warps: the pointer stays free inside the rect so
// stylus aiming and touch input keep working. Reached from UpdateClipIfNeeded()
// via ScreenPanel::shouldConfineCursorToTopScreenForPolicy().
void ConfineToTopScreen(ScreenPanel& panel);

// Pins the OS cursor at the stylus drag centre for as long as no click is held
// (Windows only; other platforms only set the cursor shape and fall back to
// ScreenPanel's warp-on-move parking). A press re-decides through
// UpdateClipIfNeeded(), which frees the pointer for the drag itself.
void PinAtStylusCenter(ScreenPanel& panel);

} // namespace MelonPrime::ScreenCursorPolicy
