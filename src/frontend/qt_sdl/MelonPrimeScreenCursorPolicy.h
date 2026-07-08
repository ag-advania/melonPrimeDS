#pragma once

class ScreenPanel;

namespace MelonPrime::ScreenCursorPolicy {

void ClipCenter1px(ScreenPanel& panel);
void UpdateClipIfNeeded(ScreenPanel& panel);
void Unclip(ScreenPanel& panel);
void ContainAimCursorIfNeeded(ScreenPanel& panel);

// Unconditional cursor-state release for panel shutdown. Unlike Unclip(),
// this does not early-return on isClosingForMelonPrime()/qApp closing-down —
// it is meant to be called from ScreenPanel::beginClose() itself (after
// `closing` is already set), so it must still run.
void ReleaseForClose(ScreenPanel& panel);

} // namespace MelonPrime::ScreenCursorPolicy
