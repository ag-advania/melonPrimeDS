// Custom HUD layout cache update for ScreenPanel::setupScreenLayout().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
// Cache scale/origin so paint paths and mouse handlers avoid sqrt per-frame.
m_hudScale = 1.0f; m_topStretchX = 1.0f; m_hudOriginX = 0.0f; m_hudOriginY = 0.0f;
m_hudTopMatrixValid = false;
for (int i = 0; i < numScreens; i++) {
    if (screenKind[i] != 0) continue;
    const float* mtx = screenMatrix[i];
    const float sx = std::sqrt(mtx[0]*mtx[0] + mtx[1]*mtx[1]);
    const float sy = std::sqrt(mtx[2]*mtx[2] + mtx[3]*mtx[3]);
    m_hudOriginX = mtx[4]; m_hudOriginY = mtx[5];
    if (sx > 0.0f && sy > 0.0f) { m_hudScale = sy; m_topStretchX = sx / sy; }
    std::copy(mtx, mtx + 6, m_hudTopMatrix);
    m_hudTopMatrixValid = true;
    break;
}
#endif
