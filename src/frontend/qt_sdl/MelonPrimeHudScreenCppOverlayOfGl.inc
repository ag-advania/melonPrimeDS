// Custom HUD overlay-of-GL path for ScreenPanelGL::drawScreen().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
        // --- Custom HUD Overlay (uses OSD shader for proper alpha blending) ---
        {
            auto* mp = emuThread->GetMelonPrimeCore();
            auto& instcfg = emuInstance->getLocalConfig();
            if (mp && mp->IsRomDetected() && (mp->IsInGame() || MelonPrime::CustomHud_IsEditMode()))
            {
                // Use layout values cached in setupScreenLayout() — no sqrt per-frame.
                const float hudScale    = m_hudScale;
                const float topStretchX = m_topStretchX;

                MelonPrimeHud_RefreshRadarConfigIfNeeded(
                    instcfg, m_radarCfgEpoch,
                    m_radarEnable, m_radarAnchor,
                    m_radarDstX, m_radarDstY, m_radarDstSize,
                    m_radarOpacity, m_radarSrcRadius);

                if (MelonPrimeHud_IsHudVisibleOrRestorePatch(emuInstance, instcfg, mp))
                {
                // Overlay covers the full logical window so HUD elements placed in black-bar
                // regions (DS x < 0 or > 256 for pillarboxed 4:3) remain visible.
                const int fullLogW = static_cast<int>(w / factor);
                const int fullLogH = static_cast<int>(h / factor);
                const int topOutW = std::max(1, fullLogW);
                const int topOutH = std::max(1, fullLogH);

                // OPT-DR1: Track dirty rect to limit CPU clear + GL texture upload.
                static QRect s_hudPrevDirtyGL;
                MelonPrimeHud_PrepareTopOverlay(Overlay[0], topOutW, topOutH, s_hudPrevDirtyGL);

                // Per-frame painter (must end before GL upload reads image bits)
                const QRect curDirty = MelonPrimeHud_RenderTopOverlay(
                    emuInstance, instcfg, mp,
                    Overlay[0], overlayFont,
                    topStretchX, hudScale,
                    m_hudOriginX, m_hudOriginY);

                glBindTexture(GL_TEXTURE_2D, overlayTextures[0]);
                // Reallocate GL texture when the buffer size changes (window resize).
                // OPT-TX1: GL_BGRA matches QImage native byte order — driver fast-path.
                if (topOutW != overlayTexW || topOutH != overlayTexH) {
                    // Full upload on size change — texture must be re-allocated.
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, topOutW, topOutH, 0,
                        GL_BGRA, GL_UNSIGNED_BYTE, Overlay[0].constBits());
                    overlayTexW = topOutW;
                    overlayTexH = topOutH;
                } else {
                    // OPT-DR1: Partial upload — only upload the union of prev (cleared)
                    // and curr (drawn) dirty rects using GL_UNPACK_ROW_LENGTH.
                    const QRect uploadRect = s_hudPrevDirtyGL.united(curDirty);
                    if (!uploadRect.isEmpty()) {
                        glPixelStorei(GL_UNPACK_ROW_LENGTH, topOutW);
                        glPixelStorei(GL_UNPACK_SKIP_PIXELS, uploadRect.x());
                        glPixelStorei(GL_UNPACK_SKIP_ROWS,   uploadRect.y());
                        glTexSubImage2D(GL_TEXTURE_2D, 0,
                            uploadRect.x(), uploadRect.y(),
                            uploadRect.width(), uploadRect.height(),
                            GL_BGRA, GL_UNSIGNED_BYTE, Overlay[0].constBits());
                        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
                        glPixelStorei(GL_UNPACK_SKIP_ROWS,   0);
                    }
                }
                s_hudPrevDirtyGL = curDirty;


                // Switch to OSD shader for HUD overlay compositing.
                // Overlay is full-window sized so pos=(0,0), size=(fullLogW, fullLogH).
                glUseProgram(osdShader);
                glUniform2f(osdScreenSizeULoc, w, h);
                glUniform1f(osdScaleFactorULoc, factor);

                glBindBuffer(GL_ARRAY_BUFFER, osdVertexBuffer);
                glBindVertexArray(osdVertexArray);
                glActiveTexture(GL_TEXTURE0);
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

                if (fullLogW > 0 && fullLogH > 0)
                {
                    const float texScaleX = (float)topOutW / (float)fullLogW;
                    const float texScaleY = (float)topOutH / (float)fullLogH;
                    glUniform2i(osdPosULoc, 0, 0);
                    glUniform2i(osdSizeULoc, fullLogW, fullLogH);
                    glUniform2f(osdTexScaleULoc, texScaleX, texScaleY);
                    glDrawArrays(GL_TRIANGLES, 0, 2 * 3);
                }

                glDisable(GL_BLEND);

                // --- Bottom Screen Overlay (GL-native, circle, high-res with opacity) ---
                if (m_radarEnable &&
                    MelonPrime::CustomHud_ShouldDrawRadarOverlay(
                        emuInstance, mp->GetCurrentRom(), mp->GetPlayerPosition()))
                {
                    // Use cached config values — no hash-map lookups per-frame.
                    int dstX    = m_radarDstX;
                    int dstY    = m_radarDstY;
                    const int dstSize = m_radarDstSize;
                    const float opacity = m_radarOpacity;

                    // Use cached hudScale as rScaleY (already the top-screen scaleY).
                    const float rScaleY = m_hudScale;

                    // Find top screen matrix (needed for anchor → window coord transform).
                    const float* topMtx = nullptr;
                    for (int i = 0; i < numScreens; i++)
                        if (screenKind[i] == 0) { topMtx = screenMatrix[i]; break; }
                    if (topMtx)
                    {
                        const float dsAX = (m_radarAnchor % 3) * 128.0f;
                        const float dsAY = (m_radarAnchor / 3) * 96.0f;
                        const float wAX  = topMtx[0]*dsAX + topMtx[1]*dsAY + topMtx[4];
                        const float wAY  = topMtx[2]*dsAX + topMtx[3]*dsAY + topMtx[5];
                        // Decompose into overlay-base + internal-offset so the int
                        // truncation of the origin matches the HUD overlay texture
                        // placement (both use int(m_hudOriginX) as the base).
                        const int ovrBaseX = static_cast<int>(m_hudOriginX);
                        const int ovrBaseY = static_cast<int>(m_hudOriginY);
                        dstX = ovrBaseX + static_cast<int>((wAX - m_hudOriginX) + dstX * rScaleY);
                        dstY = ovrBaseY + static_cast<int>((wAY - m_hudOriginY) + dstY * rScaleY);

                        // dstX/dstY are now in window pixels. Use rScaleY for both dims (circle, not ellipse).
                        const float wDstX0 = dstX;
                        const float wDstY0 = dstY;
                        const float wDstX1 = dstX + rScaleY * dstSize;
                        const float wDstY1 = dstY + rScaleY * dstSize;

                        // Full bottom screen as source (texcoords 0-1)
                        const float verts[6 * 4] =
                        {
                            wDstX0, wDstY0,  0.0f, 0.0f,
                            wDstX0, wDstY1,  0.0f, 1.0f,
                            wDstX1, wDstY1,  1.0f, 1.0f,
                            wDstX0, wDstY0,  0.0f, 0.0f,
                            wDstX1, wDstY1,  1.0f, 1.0f,
                            wDstX1, wDstY0,  1.0f, 0.0f,
                        };

                        glUseProgram(btmOverlayShader);
                        glBindVertexArray(btmOverlayVertexArray);
                        glBindBuffer(GL_ARRAY_BUFFER, btmOverlayVertexBuffer);
                        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

                        glUniform2f(btmOverlayScreenSizeULoc, w / factor, h / factor);
                        glUniform1f(btmOverlayOpacityULoc, opacity);

                        const uint8_t hunterID = mp->GetHunterID();
                        glUniform2f(btmOverlaySrcCenterULoc,
                            MelonPrime::kBtmOverlaySrcCenterX / 256.0f,
                            MelonPrime::kBtmOverlaySrcCenterY[hunterID] / 192.0f);
                        glUniform1f(btmOverlaySrcRadiusULoc, m_radarSrcRadius / 256.0f);

                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D_ARRAY, activeScreenTexture);
                        // OPT-RL1: Radar always needs GL_LINEAR. Skip redundant
                        // glTexParameteri when the texture already has it.
                        // lastFilter tracks screenTexture's state; for GPU
                        // renderer textures we always set (not tracked).
                        if (activeScreenTexture != screenTexture || lastFilter != GL_LINEAR) {
                            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                            if (activeScreenTexture == screenTexture)
                                lastFilter = GL_LINEAR;
                        }

                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                        glDrawArrays(GL_TRIANGLES, 0, 6);

                        glDisable(GL_BLEND);
                    } // if (topMtx)
                } // if (m_radarEnable)

                // Restore screen shader state
                glUseProgram(screenShaderProgram);
                glBindBuffer(GL_ARRAY_BUFFER, screenVertexBuffer);
                glBindVertexArray(screenVertexArray);
                glBindTexture(GL_TEXTURE_2D_ARRAY, screenTexture);
                } // if (hudVisible)
            }
        }
#endif
