// Custom HUD edit-mode mouse press handling for ScreenPanel::mousePressEvent().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
// HUD Layout Editor: intercept all mouse input while in edit mode.
if (UNLIKELY(MelonPrime::CustomHud_IsEditMode())) {
    Config::Table& instcfg = emuInstance->getLocalConfig();
    MelonPrime::CustomHud_UpdateEditContext(m_hudOriginX, m_hudOriginY, m_hudScale, m_topStretchX);
    MelonPrime::CustomHud_EditMousePress(event->pos(), event->button(), instcfg);
    if (!MelonPrime::CustomHud_IsEditMode() && InputConfigDialog::currentDlg)
        InputConfigDialog::currentDlg->refreshAfterHudEditSave();
    return;
}
#endif
