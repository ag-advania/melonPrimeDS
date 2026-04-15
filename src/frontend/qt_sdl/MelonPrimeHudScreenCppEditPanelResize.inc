// Custom HUD edit-panel repositioning for ScreenPanel::resizeEvent().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
if (m_hudEditPanel && m_hudEditPanel->isVisible()) {
    MelonPrimeHud_PositionEditPanel(this, m_hudEditPanel, true, false);
}
#endif
