#pragma once

class ScreenPanel;

namespace MelonPrime::ScreenCursorPolicy {

void ClipCenter1px(ScreenPanel& panel);
void UpdateClipIfNeeded(ScreenPanel& panel);
void Unclip(ScreenPanel& panel);
void ContainAimCursorIfNeeded(ScreenPanel& panel);

} // namespace MelonPrime::ScreenCursorPolicy
