// Custom HUD edit-mode mouse release handling for ScreenPanel::mouseReleaseEvent().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
auto* hudCore = melonPrimeCore();
if (UNLIKELY(hudCore && MelonPrime::CustomHud_IsEditMode(hudCore->HudConfigState()))) {
    Config::Table& instcfg = emuInstance->getLocalConfig();
    MelonPrime::CustomHud_EditMouseRelease(hudCore->HudConfigState(), event->pos(), event->button(), instcfg);
    return;
}
#endif
