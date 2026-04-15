// Custom HUD edit-mode mouse release handling for ScreenPanel::mouseReleaseEvent().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
if (UNLIKELY(MelonPrime::CustomHud_IsEditMode())) {
    Config::Table& instcfg = emuInstance->getLocalConfig();
    MelonPrime::CustomHud_EditMouseRelease(event->pos(), event->button(), instcfg);
    return;
}
#endif
