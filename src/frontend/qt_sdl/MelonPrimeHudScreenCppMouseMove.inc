// Custom HUD edit-mode mouse move handling for ScreenPanel::mouseMoveEvent().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
if (UNLIKELY(MelonPrime::CustomHud_IsEditMode())) {
    Config::Table& instcfg = emuInstance->getLocalConfig();
    MelonPrime::CustomHud_UpdateEditContext(m_hudOriginX, m_hudOriginY, m_hudScale, m_topStretchX);
    MelonPrime::CustomHud_EditMouseMove(event->pos(), instcfg);
    return;
}
#endif
