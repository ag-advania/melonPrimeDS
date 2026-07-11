// Custom HUD edit-mode mouse-wheel handling for ScreenPanel::wheelEvent().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
auto* hudCore = melonPrimeCore();
if (hudCore && MelonPrime::CustomHud_IsEditMode(hudCore->HudConfigState())) {
    Config::Table& instcfg = emuInstance->getLocalConfig();
    MelonPrime::CustomHud_EditMouseWheel(hudCore->HudConfigState(), event->position(), event->angleDelta().y(), instcfg);
}
#endif
