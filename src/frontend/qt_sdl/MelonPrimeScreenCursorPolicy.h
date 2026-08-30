#pragma once

class ScreenPanel;

namespace MelonPrime::ScreenCursorPolicy {

void ClipCenter1px(ScreenPanel& panel);
void UpdateClipIfNeeded(ScreenPanel& panel);
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
// with ConfineToBottomScreen). Unlike ClipCenter1px() this never sets the aim
// capture request, grabs or warps: the pointer stays free inside the rect so
// stylus aiming and touch input keep working. Reached from UpdateClipIfNeeded()
// via ScreenPanel::shouldConfineCursorToTopScreenForPolicy().
void ConfineToTopScreen(ScreenPanel& panel);

// Pins the OS cursor at the stylus drag centre for as long as no click is held
// (Windows only; other platforms only set the cursor shape and fall back to
// ScreenPanel's warp-on-move parking). A press re-decides through
// UpdateClipIfNeeded(), which frees the pointer for the drag itself.
void PinAtStylusCenter(ScreenPanel& panel);

} // namespace MelonPrime::ScreenCursorPolicy
