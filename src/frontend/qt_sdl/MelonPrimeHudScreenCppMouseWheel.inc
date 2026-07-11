// Custom HUD edit-mode mouse-wheel handling for ScreenPanel::wheelEvent().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
if (MelonPrime::CustomHud_IsEditMode()) {
    Config::Table& instcfg = emuInstance->getLocalConfig();
    if (auto* mp = melonPrimeCore())
        MelonPrime::CustomHud_EditMouseWheel(mp->HudConfigState(), event->position(), event->angleDelta().y(), instcfg);
}
#endif
