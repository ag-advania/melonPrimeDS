// Custom HUD setup for ScreenPanel::ScreenPanel().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
Overlay[0] = QImage(256, 192, QImage::Format_ARGB32_Premultiplied);
Overlay[1] = QImage(256, 192, QImage::Format_ARGB32_Premultiplied);
Overlay[0].fill(0x00000000);
Overlay[1].fill(0x00000000);
// Load custom font for HUD text.
// Anti-aliasing is enabled so the font looks sharp at high output resolutions.
// The font is a TTF (vector), so it renders cleanly at any pixel size.
{
    int fontId = QFontDatabase::addApplicationFont(":/mph-font");
    if (fontId >= 0) {
        QFontDatabase fontDB;
        QString family = fontDB.applicationFontFamilies(fontId).at(0);
        overlayFont = QFont(family, 6);
        overlayFont.setStyleStrategy(QFont::NoAntialias);
        overlayFont.setHintingPreference(QFont::PreferFullHinting);
    }
}
m_hudEditPanel = new MelonPrimeHudConfigOnScreenEdit(this, emuInstance);
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
