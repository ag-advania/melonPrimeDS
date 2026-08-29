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

// Applies the stylus-mode in-game cursor hide. Presentation only: unlike
// ClipCenter1px() this never sets the capture request, grabs, warps or clips,
// so stylus aiming and touch input keep working with the pointer free.
void ApplyStylusHiddenCursor(ScreenPanel& panel, bool hidden);

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

} // namespace MelonPrime::ScreenCursorPolicy
