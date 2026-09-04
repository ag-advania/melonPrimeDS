// Custom HUD overlay-of-GL path for ScreenPanelGL::drawScreen().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
        // --- Custom HUD Overlay (uses OSD shader for proper alpha blending) ---
        {
            auto* mp = emuThread->GetMelonPrimeCore();
            const bool editMode = mp && MelonPrime::CustomHud_IsEditMode(mp->HudConfigState());
            if (MelonPrimeHud_CanRenderForCore(mp, editMode))
            {
                auto& instcfg = emuInstance->getLocalConfig();
                // Use layout values cached in setupScreenLayout() — no sqrt per-frame.
                const float hudScale    = m_hudScale;
                const float topStretchX = m_topStretchX;

                MelonPrimeHud_RefreshHudEnabledIfNeeded(mp->HudConfigState(), instcfg, m_hudCfgEpoch, m_hudEnabled);
                MelonPrimeHud_RefreshOverlayFontIfNeeded(mp->HudConfigState(), instcfg, m_hudFontEpoch, overlayFont);
                MelonPrimeHud_RefreshRadarConfigIfNeeded(
                    mp->HudConfigState(),
                    instcfg, m_radarCfgEpoch,
                    m_radarEnable, m_radarAnchor,
                    m_radarDstX, m_radarDstY, m_radarDstSize,
                    m_radarOpacity, m_radarSrcRadius,
                    m_radarAnchorDsX, m_radarAnchorDsY);

                if (MelonPrimeHud_IsHudVisibleOrRestorePatch(emuInstance, mp, m_hudEnabled, editMode))
                {
                // Overlay covers the full logical window so HUD elements placed in black-bar
                // regions (DS x < 0 or > 256 for pillarboxed 4:3) remain visible.
                const int fullLogW = static_cast<int>(w / factor);
                const int fullLogH = static_cast<int>(h / factor);
                const int topOutW = std::max(1, fullLogW);
                const int topOutH = std::max(1, fullLogH);

                const HudVisualFrameIdentity visualIdentity =
                    MelonPrimeHud_ProbeVisualFrameIdentity(emuInstance);
                MelonPrimePerf::CountHudVisualIdentityProbe();
                const bool sameGameFrame = m_hudVisualFrameValid
                    && MelonPrimeHud_IsSameVisualGameFrame(
                        visualIdentity, m_hudVisualFrameKey);
                HudVisualFrameKey visualKey{};
                if (sameGameFrame) {
                    visualKey = MelonPrimeHud_MakeVisualFrameKey(
                        visualIdentity, mp->HudConfigState(), m_hudCfgEpoch, m_hudFontEpoch,
                        topOutW, topOutH, topStretchX, hudScale,
                        m_hudOriginX, m_hudOriginY,
                        m_hudVisualRendererGeneration, m_hudEnabled, editMode);
                    MelonPrimePerf::CountHudVisualStampCheck();
                }
                const bool reuseVisual = m_hudVisualFrameValid
                    && sameGameFrame
                    && visualKey == m_hudVisualFrameKey
                    && overlayTextures[0] != 0
                    && overlayTexW == topOutW
                    && overlayTexH == topOutH;
                m_hudVisualFrameWasReused = reuseVisual;
                bool textureReallocated = false;
                if (reuseVisual) {
                    // The GL texture is retained and is composited below. No
                    // CPU clear, QPainter, HUD read, or texture upload is
                    // needed for an identical visual frame.
                    MelonPrimePerf::CountHudVisualReuse();
                    m_hudDirtyRegions.Reset();
                    MelonPrimePerf::CountHudRetainedOnlyFrame();
                } else {
                    MelonPrimePerf::CountHudVisualRender();
                    // Only allocation happens here. The renderer owns clearing
                    // the retained overlay, because it is the only place that
                    // knows which per-element regions are about to be redrawn.
                    if (MelonPrimeHud_EnsureTopOverlay(Overlay[0], topOutW, topOutH))
                        m_hudOverlayRetained = false;

                    // Per-frame painter (must end before GL upload reads image bits)
                    const HudOverlayUpdate hudUpdate = MelonPrimeHud_RenderTopOverlay(
                        emuInstance, instcfg, mp,
                        Overlay[0], overlayFont, m_hudEnabled,
                        topStretchX, hudScale,
                        m_hudOriginX, m_hudOriginY,
                        m_hudOverlayRetained);
                    m_hudDirtyRegions = hudUpdate.dirty;
                    m_hudContentRegions = hudUpdate.content;
                    m_hudOverlayRetained = true;
                    if (!sameGameFrame) {
                        visualKey = MelonPrimeHud_MakeVisualFrameKey(
                            visualIdentity, mp->HudConfigState(), m_hudCfgEpoch, m_hudFontEpoch,
                            topOutW, topOutH, topStretchX, hudScale,
                            m_hudOriginX, m_hudOriginY,
                            m_hudVisualRendererGeneration, m_hudEnabled, editMode);
                    }
                    MelonPrimePerf::CountHudVisualStampCommit();
                    m_hudVisualFrameKey = visualKey;
                    m_hudVisualFrameValid = true;
                    // A resized window means a new GL texture, so everything
                    // the overlay holds has to go up regardless of the diff.
                    textureReallocated = topOutW != overlayTexW || topOutH != overlayTexH;
                }
                if (MelonPrimePerf::IsFrameActive() && !m_hudDirtyRegions.IsEmpty())
                    MelonPrimePerf::AddHudDirtyArea(
                        static_cast<int>(m_hudDirtyRegions.PixelCount()));
                bool hudGlStateChanged = false;

                // The HUD has to be composited on *every* presentation, whether
                // or not anything changed: the game frame underneath is redrawn
                // each time, so a frame that skipped the composite would simply
                // have no HUD on it. Only the upload below is conditional.
                const bool hasHudContent = !m_hudContentRegions.IsEmpty();
                const bool hudNeedsUpload =
                    textureReallocated || !m_hudDirtyRegions.IsEmpty();
                if ((hasHudContent || hudNeedsUpload)
                    && overlayTextures[0] != 0
                    && fullLogW > 0 && fullLogH > 0) {
                    MelonPrimePerf::ScopedHudPhase compositeTimer(
                        MelonPrimePerf::HudPhase::Composite);
                    glBindTexture(GL_TEXTURE_2D, overlayTextures[0]);
                    // Reallocate GL texture when the buffer size changes (window resize).
                    // OPT-TX1: GL_BGRA matches QImage native byte order — driver fast-path.
                    if (textureReallocated) {
                        MelonPrimePerf::ScopedHudPhase uploadTimer(
                            MelonPrimePerf::HudPhase::UploadPrepare);
                        MelonPrimePerf::ScopedHudPhase gpuUploadTimer(
                            MelonPrimePerf::HudPhase::GpuUpload);
                        MelonPrimePerf::CountHudUploadCall();
                        // Full upload on size change — texture must be re-allocated.
                        const uint64_t fullBytes =
                            static_cast<uint64_t>(topOutW) * static_cast<uint64_t>(topOutH) * 4u;
                        MelonPrimePerf::AddGlUploadBytes(fullBytes);
                        MelonPrimePerf::AddHudUploadRegion(
                            static_cast<uint64_t>(topOutW) * static_cast<uint64_t>(topOutH),
                            fullBytes);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, topOutW, topOutH, 0,
                            GL_BGRA, GL_UNSIGNED_BYTE, Overlay[0].constBits());
                        overlayTexW = topOutW;
                        overlayTexH = topOutH;
                    } else if (!m_hudDirtyRegions.IsEmpty()) {
                        MelonPrimePerf::ScopedHudPhase uploadTimer(
                            MelonPrimePerf::HudPhase::UploadPrepare);
                        MelonPrimePerf::ScopedHudPhase gpuUploadTimer(
                            MelonPrimePerf::HudPhase::GpuUpload);
                        // One glTexSubImage2D per dirty region rather than one
                        // over their bounding box. The bounding box of HP,
                        // scoreboard and ammo is most of the window; the regions
                        // themselves are the elements that changed.
                        //
                        // OPT-DR1 / OPT-HUD-3: a direct pointer into the QImage
                        // data eliminates GL_UNPACK_SKIP_PIXELS and
                        // GL_UNPACK_SKIP_ROWS (4 driver calls -> 0). Only
                        // GL_UNPACK_ROW_LENGTH is needed so the driver advances
                        // by a full row between upload rows.
                        glPixelStorei(GL_UNPACK_ROW_LENGTH, topOutW);
                        for (int regionIndex = 0;
                             regionIndex < m_hudDirtyRegions.Count(); ++regionIndex) {
                            const QRect uploadRect =
                                m_hudDirtyRegions.Region(regionIndex)
                                & QRect(0, 0, topOutW, topOutH);
                            if (uploadRect.isEmpty())
                                continue;
                            MelonPrimePerf::CountHudUploadCall();
                            const uint64_t uploadPixels =
                                static_cast<uint64_t>(uploadRect.width())
                                * static_cast<uint64_t>(uploadRect.height());
                            MelonPrimePerf::AddGlUploadBytes(uploadPixels * 4u);
                            MelonPrimePerf::AddHudUploadRegion(
                                uploadPixels, uploadPixels * 4u);
                            const uchar* uploadPtr = Overlay[0].constBits()
                                + uploadRect.y() * Overlay[0].bytesPerLine()
                                + uploadRect.x() * 4;
                            glTexSubImage2D(GL_TEXTURE_2D, 0,
                                uploadRect.x(), uploadRect.y(),
                                uploadRect.width(), uploadRect.height(),
                                GL_BGRA, GL_UNSIGNED_BYTE, uploadPtr);
                        }
                        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                    }

                    // Switch to OSD shader for HUD overlay compositing. This
                    // remains active on a visual-frame reuse; only the upload
                    // work above is skipped.
                    // Overlay is full-window sized so pos=(0,0), size=(fullLogW, fullLogH).
                    hudGlStateChanged = true;
                    glUseProgram(osdShader);
                    glUniform2f(osdScreenSizeULoc, w, h);
                    glUniform1f(osdScaleFactorULoc, factor);

                    glBindBuffer(GL_ARRAY_BUFFER, osdVertexBuffer);
                    glBindVertexArray(osdVertexArray);
                    glActiveTexture(GL_TEXTURE0);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

                    const float texScaleX = (float)topOutW / (float)fullLogW;
                    const float texScaleY = (float)topOutH / (float)fullLogH;
                    glUniform2i(osdPosULoc, 0, 0);
                    glUniform2i(osdSizeULoc, fullLogW, fullLogH);
                    glUniform2f(osdTexScaleULoc, texScaleX, texScaleY);

                    // The quad still covers the window so the shader's UV
                    // mapping is untouched, but the scissor limits rasterisation
                    // to the regions the HUD actually occupies. Everything
                    // outside them samples transparent texels and blends to a
                    // no-op, which is fragment, texture-cache and framebuffer
                    // read-modify-write bandwidth spent for nothing.
                    std::uint64_t compositePixels = 0;
                    bool scissorEnabled = false;
                    const bool hudTextureUsable = overlayTexW == topOutW
                        && overlayTexH == topOutH;
                    for (int regionIndex = 0;
                         hudTextureUsable && regionIndex < m_hudContentRegions.Count();
                         ++regionIndex) {
                        int scissorX = 0, scissorY = 0, scissorW = 0, scissorH = 0;
                        if (!MelonPrimeHud_LogicalRectToScissor(
                                m_hudContentRegions.Region(regionIndex),
                                factor, static_cast<int>(w), static_cast<int>(h),
                                scissorX, scissorY, scissorW, scissorH))
                            continue;
                        if (!scissorEnabled) {
                            glEnable(GL_SCISSOR_TEST);
                            scissorEnabled = true;
                        }
                        glScissor(scissorX, scissorY, scissorW, scissorH);
                        compositePixels += static_cast<std::uint64_t>(scissorW)
                            * static_cast<std::uint64_t>(scissorH);
                        glDrawArrays(GL_TRIANGLES, 0, 2 * 3);
                    }
                    if (scissorEnabled) {
                        // Never leak the scissor into the radar pass or back
                        // into the screen shader below.
                        glDisable(GL_SCISSOR_TEST);
                    }
                    MelonPrimePerf::AddHudOverlayComposite(
                        compositePixels * sizeof(QRgb), compositePixels);

                    glDisable(GL_BLEND);
                }

                // --- Bottom Screen Overlay (GL-native, circle, high-res with opacity) ---
                if (m_radarEnable &&
                    m_hudTopMatrixValid &&
                    MelonPrime::CustomHud_ShouldDrawRadarOverlay(
                        mp->HudConfigState(),
                        emuInstance, mp->GetCurrentRom(), mp->GetPlayerPosition()))
                {
                    // Use cached config values — no hash-map lookups per-frame.
                    const int dstX = m_radarDstX;
                    const int dstY = m_radarDstY;
                    const int dstSize = m_radarDstSize;
                    const float opacity = m_radarOpacity;
                    // Use cached hudScale as rScaleY (already the top-screen scaleY).
                    const float rScaleY = m_hudScale;
                    const uint8_t hunterID = mp->GetHunterID();
                    auto& radarCache = m_hudRadarGl;
                    const bool matrixChanged = !radarCache.geometryValid
                        || radarCache.topMatrix[0] != m_hudTopMatrix[0]
                        || radarCache.topMatrix[1] != m_hudTopMatrix[1]
                        || radarCache.topMatrix[2] != m_hudTopMatrix[2]
                        || radarCache.topMatrix[3] != m_hudTopMatrix[3]
                        || radarCache.topMatrix[4] != m_hudTopMatrix[4]
                        || radarCache.topMatrix[5] != m_hudTopMatrix[5];
                    const bool surfaceSizeChanged = !radarCache.geometryValid
                        || radarCache.surfaceWidth != w
                        || radarCache.surfaceHeight != h
                        || radarCache.surfaceScale != factor;
                    const bool geometryChanged = !radarCache.geometryValid
                        || radarCache.configEpoch != m_radarCfgEpoch
                        || radarCache.layoutGeneration != m_hudVisualRendererGeneration
                        || surfaceSizeChanged
                        || radarCache.hudScale != rScaleY
                        || radarCache.hudOriginX != m_hudOriginX
                        || radarCache.hudOriginY != m_hudOriginY
                        || radarCache.anchorDsX != m_radarAnchorDsX
                        || radarCache.anchorDsY != m_radarAnchorDsY
                        || radarCache.dstX != dstX
                        || radarCache.dstY != dstY
                        || radarCache.dstSize != dstSize
                        || matrixChanged;
                    const bool screenSizeChanged = !radarCache.screenSizeValid
                        || surfaceSizeChanged;
                    const bool opacityChanged = !radarCache.opacityValid
                        || radarCache.opacity != opacity;
                    const bool sourceChanged = !radarCache.sourceValid
                        || radarCache.srcRadius != m_radarSrcRadius
                        || radarCache.hunterId != hunterID;

                    glUseProgram(btmOverlayShader);
                    hudGlStateChanged = true;
                    glBindVertexArray(btmOverlayVertexArray);
                    glBindBuffer(GL_ARRAY_BUFFER, btmOverlayVertexBuffer);
                    if (geometryChanged) {
                        const float* topMtx = m_hudTopMatrix;
                        const float wAX = topMtx[0] * m_radarAnchorDsX
                            + topMtx[1] * m_radarAnchorDsY + topMtx[4];
                        const float wAY = topMtx[2] * m_radarAnchorDsX
                            + topMtx[3] * m_radarAnchorDsY + topMtx[5];
                        // Decompose into overlay-base + internal-offset so the
                        // int truncation matches the HUD texture placement.
                        const int ovrBaseX = static_cast<int>(m_hudOriginX);
                        const int ovrBaseY = static_cast<int>(m_hudOriginY);
                        const int resolvedDstX = ovrBaseX + static_cast<int>(
                            (wAX - m_hudOriginX) + dstX * rScaleY);
                        const int resolvedDstY = ovrBaseY + static_cast<int>(
                            (wAY - m_hudOriginY) + dstY * rScaleY);
                        const float wDstX0 = resolvedDstX;
                        const float wDstY0 = resolvedDstY;
                        const float wDstX1 = resolvedDstX + rScaleY * dstSize;
                        const float wDstY1 = resolvedDstY + rScaleY * dstSize;
                        // Full bottom screen as source (texcoords 0-1).
                        const float verts[6 * 4] = {
                            wDstX0, wDstY0, 0.0f, 0.0f,
                            wDstX0, wDstY1, 0.0f, 1.0f,
                            wDstX1, wDstY1, 1.0f, 1.0f,
                            wDstX0, wDstY0, 0.0f, 0.0f,
                            wDstX1, wDstY1, 1.0f, 1.0f,
                            wDstX1, wDstY0, 1.0f, 0.0f,
                        };
                        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                        MelonPrimePerf::CountHudRadarVboUpload();
                        radarCache.geometryValid = true;
                        radarCache.configEpoch = m_radarCfgEpoch;
                        radarCache.layoutGeneration = m_hudVisualRendererGeneration;
                        radarCache.surfaceWidth = w;
                        radarCache.surfaceHeight = h;
                        radarCache.surfaceScale = factor;
                        radarCache.hudScale = rScaleY;
                        radarCache.hudOriginX = m_hudOriginX;
                        radarCache.hudOriginY = m_hudOriginY;
                        radarCache.anchorDsX = m_radarAnchorDsX;
                        radarCache.anchorDsY = m_radarAnchorDsY;
                        radarCache.dstX = dstX;
                        radarCache.dstY = dstY;
                        radarCache.dstSize = dstSize;
                        for (int i = 0; i < 6; ++i)
                            radarCache.topMatrix[i] = m_hudTopMatrix[i];
                    }
                    if (screenSizeChanged) {
                        const float screenWidth = w / factor;
                        const float screenHeight = h / factor;
                        glUniform2f(btmOverlayScreenSizeULoc,
                            screenWidth, screenHeight);
                        radarCache.screenWidth = screenWidth;
                        radarCache.screenHeight = screenHeight;
                        radarCache.screenSizeValid = true;
                    }
                    if (opacityChanged) {
                        glUniform1f(btmOverlayOpacityULoc, opacity);
                        radarCache.opacity = opacity;
                        radarCache.opacityValid = true;
                    }
                    if (sourceChanged) {
                        glUniform2f(btmOverlaySrcCenterULoc,
                            MelonPrime::kBtmOverlaySrcCenterX / 256.0f,
                            MelonPrime::kBtmOverlaySrcCenterY[hunterID] / 192.0f);
                        glUniform1f(btmOverlaySrcRadiusULoc,
                            m_radarSrcRadius / 256.0f);
                        radarCache.srcRadius = m_radarSrcRadius;
                        radarCache.hunterId = hunterID;
                        radarCache.sourceValid = true;
                    }
                    // A config edge invalidates the geometry cache as a whole;
                    // keep the edge signature current after the one upload.
                    radarCache.configEpoch = m_radarCfgEpoch;
                    radarCache.layoutGeneration = m_hudVisualRendererGeneration;

                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D_ARRAY, activeScreenTexture);
                    // Sampler state is owned by the cold GL lifecycle, so
                    // radar does not mutate the renderer-owned screen texture.
                    glBindSampler(0, m_hudRadarSampler);

                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                    glDrawArrays(GL_TRIANGLES, 0, 6);

                    glDisable(GL_BLEND);
                    glBindSampler(0, 0);
                } // if (m_radarEnable)

                if (hudGlStateChanged) {
                    // Restore screen shader state
                    glUseProgram(screenShaderProgram);
                    glBindBuffer(GL_ARRAY_BUFFER, screenVertexBuffer);
                    glBindVertexArray(screenVertexArray);
                    glBindTexture(GL_TEXTURE_2D_ARRAY, screenTexture);
                }
                } // if (hudVisible)
                else {
                    m_hudVisualFrameValid = false;
                    m_hudVisualFrameWasReused = false;
                    m_hudOverlayRetained = false;
                    m_hudDirtyRegions.Reset();
                    m_hudContentRegions.Reset();
                }
            }
            else {
                m_hudVisualFrameValid = false;
                m_hudVisualFrameWasReused = false;
                m_hudOverlayRetained = false;
                m_hudDirtyRegions.Reset();
                m_hudContentRegions.Reset();
            }
        }
#endif
