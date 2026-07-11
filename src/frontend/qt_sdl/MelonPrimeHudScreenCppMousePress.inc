// Custom HUD edit-mode mouse press handling for ScreenPanel::mousePressEvent().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
// HUD Layout Editor: intercept all mouse input while in edit mode.
auto* hudCore = melonPrimeCore();
if (UNLIKELY(hudCore && MelonPrime::CustomHud_IsEditMode(hudCore->HudConfigState()))) {
    Config::Table& instcfg = emuInstance->getLocalConfig();
    MelonPrime::CustomHud_UpdateEditContext(hudCore->HudConfigState(), m_hudOriginX, m_hudOriginY, m_hudScale, m_topStretchX);
    MelonPrime::CustomHud_EditMousePress(hudCore->HudConfigState(), event->pos(), event->button(), instcfg);
    if (!MelonPrime::CustomHud_IsEditMode(hudCore->HudConfigState()) && InputConfigDialog::currentDlg)
        InputConfigDialog::currentDlg->refreshAfterHudEditSave();
    return;
}
#endif
