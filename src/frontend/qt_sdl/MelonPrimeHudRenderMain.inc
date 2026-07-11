// This file is a unity-build fragment included by MelonPrimeHudRender.cpp.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  P-7 forward declarations (full definitions follow CustomHud_Render)
// =========================================================================
static constexpr int kEditElemCount   = 14; // 0-11 = HUD elements, 12 = Weapon Inventory, 13 = Crosshair
static QRectF ComputeEditBounds(int idx, Config::Table& cfg, float topStretchX);
static void   DrawEditOverlay(QPainter* p, Config::Table& cfg, float topStretchX, QImage* btmBuffer);

// =========================================================================
//  CustomHud_Render — main entry point
// =========================================================================
HOT_FUNCTION QRect CustomHud_Render(
    CustomHudConfigState& hudConfig,
    EmuInstance* emu, Config::Table& localCfg,
    const RomAddresses& rom, const GameAddressesHot& addrHot,
    uint8_t playerPosition,
    QPainter* topPaint, QPainter* btmPaint,
    QImage* topBuffer, QImage* btmBuffer,
    bool isInGame,
    float topStretchX, float hudScale,
    float hudOriginXds, float hudOriginYds)
{
    const ScopedHudConfigState active(hudConfig);
    const HudFrameTransform xf = MakeHudFrameTransform(topBuffer, topStretchX, hudScale,
                                                       hudOriginXds, hudOriginYds);

    // OPT-DR1: Reset crosshair dirty accumulator for this frame.
    s_chDirtyThisFrame = QRect();
    // OPT-DR2: Reset the actual-drawn-pixel accumulator for this frame.
    s_drawnDirtyPx = QRect();

    // Edit mode: draw element overlay every frame, skip normal HUD rendering.
    if (UNLIKELY(s_editMode)) {
        HudRuntimeState st = {};
        if (isInGame && ReadHudRuntimeBaseState(emu, rom, playerPosition, st))
            CustomHud_RestoreNativeHudState(rom, st);

        s_editHudScale      = hudScale;
        s_editTopStretchX   = topStretchX;
        s_editRomCopy       = rom;
        s_editAddrHotCopy   = addrHot;
        s_editPlayerPosCopy = playerPosition;
        EnsureCachedConfigForFrame(localCfg, xf);
        ApplyHudPainterTransform(topPaint, xf);
        EnsureHudFont(topPaint);
        for (int i = 0; i < kEditElemCount; ++i)
            s_editRects[i] = ComputeEditBounds(i, localCfg, topStretchX);
        DrawEditOverlay(topPaint, localCfg, topStretchX, btmBuffer);
        return xf.overlayRect;  // edit mode covers full overlay
    }

    if (!isInGame) return QRect();

    HudRuntimeState st = {};
    if (!ReadHudRuntimeBaseState(emu, rom, playerPosition, st))
        return QRect();

    // Defensive: caller (Screen.cpp) gates on the epoch-cached m_hudEnabled,
    // which can be stale right after the user toggles the setting. Re-check
    // the live config so we never apply NoHudPatch / write hudToggle while
    // CustomHUD is actually disabled.
    if (!CustomHud_IsEnabled(localCfg)) {
        CustomHud_RestoreNativeHudState(rom, st);
        return QRect();
    }

    if (!st.isStartPressed && st.currentHP != 0 && !st.isGameOver)
        ReadHudRuntimeAdventurePauseState(rom, st);

    // P-3: Refresh config cache only when invalidated, or recompute anchor
    //       positions when topStretchX changes (window resize).
    //       Also refresh when hudScale changes — crosshair rects and textDrawScale
    //       are in actual output pixels divided by hudScale.
    // Refreshed BEFORE NoHudPatch_Sync so c.noHudMask is current.
    EnsureCachedConfigForFrame(localCfg, xf);
    const CachedHudConfig& c = s_cache;

    // Sync BEFORE the gameplay-state early return so the helmet bit is already
    // applied (and tracked) during loading/spawn frames — the pre-frame layer
    // clamp in RunFrameHook gates on the applied mask. Per-element patches NOP
    // individual ARM draw instructions, so the start menu / death / game-over
    // screens are unaffected by leaving them applied.
    NoHudPatch_Sync(ActiveHudConfigState().noHudPatch, st.nds, st.romGroup, c.noHudMask);
    CustomHud_MarkNoHudPatchDesired();

    if (ShouldHideForGameplayState(st.isStartPressed,
                                   st.currentHP,
                                   st.isGameOver,
                                   st.isAdventure,
                                   st.isMapOrUserActionPaused)) {
        NotifyCrosshairZoomNotDrawn();
        return QRect();
    }

    ReadHudRuntimeVisibleState(rom, addrHot, st);

    // hudScale = scaleY: DS Y-unit maps to hudScale px.
    // Buffer now covers the full window. Translate has two parts:
    //   (topStretchX-1)*128 — centres the DS canvas when content is wider/narrower than 4:3
    //   hudOriginXds        — shifts right by the left black-bar width (DS units), so DS x=0
    //                          lands at the game-content left edge inside the full-window buffer.
    ApplyHudPainterTransform(topPaint, xf);

    // Font base size = the resolved HUD font px (6px for MPH; Metroid.Visual.HudFontSize for
    // system/file fonts). EnsureHudFont adopts the painter's overlayFont size set by Screen.cpp.
    // Visual text size is controlled by textDrawScale applied at draw time.
    EnsureHudFont(topPaint);
    const float textDrawScale = c.textDrawScale;
    // s_textRenderScale and the hi-DPI font were built event-driven in RefreshCachedConfig;
    // nothing to recompute per frame here.

    // Draw crosshair/scope first so large zoom reticles sit behind HP, ammo,
    // radar, and other Custom HUD elements.
    const bool drawCrosshairThisFrame = st.isFirstPerson && !st.isTrans && !st.isAlt;
    if (drawCrosshairThisFrame) {
        const uint32_t gameFrame = st.nds ? st.nds->NumFrames : 0;
        float crosshairZoomAmount = 0.0f;
        if (c.crosshair.cachedZoomAffectsVisual) {
            UpdateCrosshairZoomAmountForGameFrame(
                st.ram, rom, gameFrame);
            crosshairZoomAmount = ReadCrosshairZoomAmount(st.ram, rom);
        }
        DrawCrosshair(topPaint, st.ram, rom, c, hudScale, topStretchX,
                      gameFrame, crosshairZoomAmount);
    }

    DrawHP(topPaint, st.currentHP, st.maxHP, c, textDrawScale);

    // Bomb count: Samus/Sylux in alt form only
    {
        const bool isBomber = (st.hunterID == static_cast<uint8_t>(HunterId::Samus) ||
                               st.hunterID == static_cast<uint8_t>(HunterId::Sylux));
        if (isBomber && st.isAlt) {
            DrawBombLeft(topPaint, ReadHudRuntimeBombCount(rom, st),
                         c, textDrawScale, hudScale);
        }
    }

    // Match Status + Rank & Time HUDs (non-adventure only, visible in all camera modes)
    DrawMatchStatusHud(topPaint, st.ram, rom, playerPosition, st.isAdventure, c, &st);
    DrawRankAndTime(topPaint, st.ram, rom, playerPosition, st.isAdventure, c, textDrawScale, &st);

    // Draw bottom screen overlay on top screen (visible in all camera modes incl. alt form)
    if (c.radar.radarShow)
        DrawBottomScreenOverlay(localCfg, topPaint, btmBuffer, (st.hunterID <= 6) ? st.hunterID : 0);

    if (!st.isFirstPerson) {
        // OPT-DR1: Crosshair not drawn in third-person / alt-form.
        // If it was drawn last frame, include its previous bbox so Screen.cpp clears it.
        ClearPreviousCrosshairDirty(c.crosshair);
        return CurrentHudDirtyRect(xf);
    }

    const uint8_t weaponIdx = (st.currentWeapon < 9) ? st.currentWeapon : 0;
    const WeaponInfo& weaponInfo = kWeaponTable[weaponIdx];
    uint16_t currentAmmoSpecial = 0;
    uint16_t currentAmmoMissile = 0;
    if (c.weaponInventory.show ||
        (weaponInfo.divisor > 0 && !weaponInfo.isMissile && weaponIdx != WeaponId::OmegaCannon))
        currentAmmoSpecial = ReadHudRuntimeCurrentAmmoSpecial(rom, st);
    if (c.weaponInventory.show ||
        weaponInfo.isMissile)
        currentAmmoMissile = ReadHudRuntimeCurrentAmmoMissile(rom, st);

    DrawWeaponAmmo(topPaint, st.currentWeapon,
                   currentAmmoSpecial, currentAmmoMissile,
                   st.maxAmmoSpecial, st.maxAmmoMissile, c, textDrawScale, hudScale);

    if (c.weaponInventory.show) {
        const uint16_t havingWeapons = ReadHudRuntimeHavingWeapons(addrHot, st);
        DrawWeaponInventory(topPaint,
                            havingWeapons, st.currentWeapon,
                            currentAmmoSpecial, currentAmmoMissile,
                            c, textDrawScale, hudScale);
    }

    if (!drawCrosshairThisFrame) {
        // OPT-DR1: Crosshair not drawn this frame (transition/alt) but was last frame.
        ClearPreviousCrosshairDirty(c.crosshair);
    }

    return CurrentHudDirtyRect(xf);
}

// =========================================================================
//  Radar frame SVG drawing — separated from the bottom screen crop overlay.
//  Uses the same position/size as the radar overlay (radarDstRect center).
// =========================================================================
// P-15: Radar frame SVG drawing — uses pre-computed frameDstRect from config cache.
// P-16: No save/restore — only modifies opacity (caller handles state).
static void DrawRadarFrame(QPainter* topPaint, const CachedHudConfig& c)
{
    if (s_radarFrameTinted.isNull()) return;

    topPaint->setOpacity(c.radar.radarOpacity);
    topPaint->drawImage(c.radar.frameDstRect, s_radarFrameTinted);
    AccumDirtyDs(topPaint, c.radar.frameDstRect); // OPT-DR2
}

static void DrawRadarCombinedOutlines(QPainter* topPaint, const CachedHudConfig& c)
{
    const HudOutlineConfig& _ol = EffOL(c, c.radar.outline);
    const HudOutlineConfig& _fol = EffOL(c, c.radar.frameOutline);
    const bool circleOL = _ol.enable && _ol.opacity > 0.0f;
    const bool frameOL  = _fol.enable && _fol.opacity > 0.0f && !s_radarFrameOutline.isNull();
    if (!circleOL && !frameOL) return;

    // OPT-SR2: No save/restore — caller (DrawBottomScreenOverlay) manages state.
    topPaint->setRenderHint(QPainter::Antialiasing, true);
    topPaint->setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (circleOL) {
        const float expandDS = (c.lastHudScale > 0.0f)
                               ? static_cast<float>(std::max(1, _ol.thickness)) / c.lastHudScale : 0.0f;
        topPaint->setOpacity(_ol.opacity);
        topPaint->setPen(Qt::NoPen);
        topPaint->setBrush(_ol.colorWithAlpha); // P-14
        const QRectF circleRect = QRectF(c.radar.radarDstRect).adjusted(-expandDS, -expandDS, expandDS, expandDS);
        topPaint->drawEllipse(circleRect);
        AccumDirtyDs(topPaint, circleRect); // OPT-DR2
    }

    // SVG frame outline — independent settings via frameOutline
    if (frameOL) {
        const float fExpandDS = (c.lastHudScale > 0.0f)
                                ? static_cast<float>(std::max(1, _fol.thickness)) / c.lastHudScale : 0.0f;
        topPaint->setOpacity(_fol.opacity);
        const QRectF& fr = c.radar.frameDstRect;
        const QRectF frameOlRect(fr.x() - fExpandDS,
                                 fr.y() - fExpandDS,
                                 fr.width()  + fExpandDS * 2.0f,
                                 fr.height() + fExpandDS * 2.0f);
        topPaint->drawImage(frameOlRect, s_radarFrameOutline);
        AccumDirtyDs(topPaint, frameOlRect); // OPT-DR2
    }
}

void DrawBottomScreenOverlay(Config::Table& localCfg, QPainter* topPaint, QImage* btmBuffer, uint8_t hunterID)
{
    if (UNLIKELY(!s_cache.valid)) {
        RefreshCachedConfig(localCfg, 1.0f);
        s_cache.valid = true;
    }

    const CachedHudConfig& c = s_cache;
    if (!c.radar.radarShow) return;
    if (!topPaint) return;

    // P-13: Resolve effective radar color with caching — only recompute QColor
    //        when hunterID or useHunter config actually changes.
    {
        const bool useHunter = c.radar.radarFrameColorUseHunter;
        if (useHunter != s_effectiveRadarUseHunter ||
            (useHunter  && hunterID != s_effectiveRadarHunterID) ||
            (!useHunter && s_effectiveRadarColor != c.radar.radarFrameColor)) {
            s_effectiveRadarColor = (useHunter && hunterID < kHunterCount)
                                    ? HunterFrameQColor(hunterID)
                                    : c.radar.radarFrameColor;
            s_effectiveRadarUseHunter = useHunter;
            s_effectiveRadarHunterID  = hunterID;
        }
    }

    // Build frame caches up front (tinted + outline), then draw in final order below.
    {
        { const HudOutlineConfig& _fol = EffOL(c, c.radar.frameOutline);
        const int expandR = (_fol.enable && _fol.opacity > 0.0f) ? std::max(1, _fol.thickness) : 0;
        EnsureRadarFrameLoaded(c.radar.radarDstSize, c.radar.radarSrcRadius, c.lastHudScale,
                               s_effectiveRadarColor, _fol.color, expandR); }
    }

    // Draw order (back-to-front):
    //  ③ (back):  Combined outlines (expanded circle + dilated SVG frame)
    //  ② (mid):   Radar SVG frame
    //  ① (front): Radar HUD crop circle from bottom screen

    // OPT-SR2: Manual state management replaces 2× QPainter::save()/restore().
    // save() clones entire painter state to heap (~200-500 cyc each); here we
    // only track the attributes actually modified by ③②①.
    const qreal savedOpacity = topPaint->opacity();

    // ③ Combined outlines (backmost) — sets RenderHints, opacity, pen, brush.
    DrawRadarCombinedOutlines(topPaint, c);

    // SmoothPixmapTransform already set by ③ if outlines are enabled;
    // ensure it's on even when outlines are skipped.
    topPaint->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // ② Radar SVG frame.
    DrawRadarFrame(topPaint, c);

    // ① Radar HUD (crop from bottom screen, frontmost), if available.
    if (btmBuffer && !btmBuffer->isNull()) {
        const int srcRadius = c.radar.radarSrcRadius;
        const int bufW = btmBuffer->width();
        const int bufH = btmBuffer->height();

        // Cache the source crop rect. srcRect depends only on hunterID,
        // radarSrcRadius (config), and the bottom-screen buffer size — none of
        // which change every frame in steady state. Recompute only when one of
        // those inputs changes; otherwise reuse the cached QRect and skip 2 float
        // divisions + 4 casts each visible-radar frame. The cache key covers all
        // inputs of the formula, so it is self-correcting (no external
        // invalidation needed on hunter swap / config change / resolution change).

        if (hunterID != s_radarSrcHunterID || srcRadius != s_radarSrcRadius ||
            bufW != s_radarSrcBufW || bufH != s_radarSrcBufH) {
            const int srcCenterX  = kBtmOverlaySrcCenterX;
            const int srcCenterY  = kBtmOverlaySrcCenterY[hunterID];
            const float bufScaleX = static_cast<float>(bufW) / 256.0f;
            const float bufScaleY = static_cast<float>(bufH) / 192.0f;

            s_radarSrcRect = QRect(static_cast<int>((srcCenterX - srcRadius) * bufScaleX),
                                   static_cast<int>((srcCenterY - srcRadius) * bufScaleY),
                                   static_cast<int>(srcRadius * 2 * bufScaleX),
                                   static_cast<int>(srcRadius * 2 * bufScaleY));
            s_radarSrcHunterID = hunterID;
            s_radarSrcRadius   = srcRadius;
            s_radarSrcBufW     = bufW;
            s_radarSrcBufH     = bufH;
        }

        topPaint->setOpacity(c.radar.radarOpacity);
        topPaint->setClipPath(c.radar.radarClipPath);
        topPaint->drawImage(c.radar.radarDstRect, *btmBuffer, s_radarSrcRect);
        AccumDirtyDs(topPaint, QRectF(c.radar.radarDstRect)); // OPT-DR2
    }

    // OPT-SR2: Manual restore — only the attributes actually changed.
    topPaint->setClipping(false);
    topPaint->setRenderHint(QPainter::Antialiasing, false);
    topPaint->setRenderHint(QPainter::SmoothPixmapTransform, false);
    topPaint->setOpacity(savedOpacity);
}

