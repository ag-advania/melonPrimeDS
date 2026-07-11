// Custom HUD setup for ScreenPanel::ScreenPanel().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
Overlay[0] = QImage(256, 192, QImage::Format_ARGB32_Premultiplied);
Overlay[1] = QImage(256, 192, QImage::Format_ARGB32_Premultiplied);
Overlay[0].fill(0x00000000);
Overlay[1].fill(0x00000000);
// Load the HUD text font. CustomHud_ResolveBaseFont honors the selectable font setting
// (Metroid.Visual.HudFontMode: default MPH / system font / font file) and sets the family
// + style strategy; we only add the fixed HUD pixel size here. The font is later rebuilt
// per-frame via MelonPrimeHud_RefreshOverlayFontIfNeeded when the setting changes.
{
    auto& hudCfg = emuInstance->getLocalConfig();
    overlayFont = MelonPrime::CustomHud_ResolveBaseFont(hudCfg);
    overlayFont.setPixelSize(MelonPrime::CustomHud_ResolveFontPixelSize(hudCfg));
}
m_hudEditPanel = new MelonPrimeHudConfigOnScreenEdit(
    this,
    emuInstance,
    emuInstance->getEmuThread()->GetMelonPrimeCore()->HudConfigState());
MelonPrime::CustomHud_SetEditSelectionCallback([this](int idx) {
    if (!MelonPrime::CustomHud_IsEditMode() || idx < 0) {
        m_hudEditPanel->clear();
    } else {
        m_hudEditPanel->populateForElement(idx);
        MelonPrimeHud_PositionEditPanel(this, m_hudEditPanel, true, true);
        m_hudEditPanel->show();
        m_hudEditPanel->raise();
    }
});
#endif
