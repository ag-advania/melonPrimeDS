// This file is a unity-build fragment included by MelonPrimeHudRender.cpp.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  P-7 forward declarations (full definitions follow CustomHud_Render)
// =========================================================================
static constexpr int kEditElemCount   = 14; // 0-11 = HUD elements, 12 = Weapon Inventory, 13 = Crosshair
static bool          s_editMode       = false;
static QRectF        s_editRects[kEditElemCount];
static float         s_editHudScale    = 1.0f;
static float         s_editTopStretchX = 1.0f;
static bool          s_editPreviewMode = false;
static RomAddresses     s_editRomCopy       = {};
static GameAddressesHot s_editAddrHotCopy   = {};
static uint8_t          s_editPlayerPosCopy = 0;
static QRectF ComputeEditBounds(int idx, Config::Table& cfg, float topStretchX);
static void   DrawEditOverlay(QPainter* p, Config::Table& cfg, float topStretchX, QImage* btmBuffer);

// =========================================================================
//  CustomHud_Render — main entry point
// =========================================================================
HOT_FUNCTION QRect CustomHud_Render(
    EmuInstance* emu, Config::Table& localCfg,
    const RomAddresses& rom, const GameAddressesHot& addrHot,
    uint8_t playerPosition,
    QPainter* topPaint, QPainter* btmPaint,
    QImage* topBuffer, QImage* btmBuffer,
    bool isInGame,
    float topStretchX, float hudScale,
    float hudOriginXds, float hudOriginYds)
{
    const HudFrameTransform xf = MakeHudFrameTransform(topBuffer, topStretchX, hudScale,
                                                       hudOriginXds, hudOriginYds);

    // OPT-DR1: Reset crosshair dirty accumulator for this frame.
    s_chDirtyThisFrame = QRect();

    // Edit mode: draw element overlay every frame, skip normal HUD rendering.
    if (UNLIKELY(s_editMode)) {
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
    if (!ReadHudRuntimeState(emu, rom, addrHot, playerPosition, st))
        return QRect();

    ApplyNoHudPatch(st.nds, st.romGroup);

    // P-3: Refresh config cache only when invalidated, or recompute anchor
    //       positions when topStretchX changes (window resize).
    //       Also refresh when hudScale changes — crosshair rects and textDrawScale
    //       are in actual output pixels divided by hudScale.
    EnsureCachedConfigForFrame(localCfg, xf);
    const CachedHudConfig& c = s_cache;

    // OPT-DR1: Refresh static dirty rect when transform or config changed.
    EnsureStaticDirtyForFrame(c, xf);

    const uint32_t addrAmmoSpecial = rom.currentAmmoSpecial + st.offP;
    const uint32_t addrAmmoMissile = rom.currentAmmoMissile + st.offP;

    Write8(st.ram, rom.hudToggle, st.isStartPressed ? 0x11 : 0x01);

    if (ShouldHideForGameplayState(st.isStartPressed, st.currentHP, st.isGameOver))
        return QRect();

    // hudScale = scaleY: DS Y-unit maps to hudScale px.
    // Buffer now covers the full window. Translate has two parts:
    //   (topStretchX-1)*128 — centres the DS canvas when content is wider/narrower than 4:3
    //   hudOriginXds        — shifts right by the left black-bar width (DS units), so DS x=0
    //                          lands at the game-content left edge inside the full-window buffer.
    ApplyHudPainterTransform(topPaint, xf);

    // Font locked at kCustomHudFontSize (6px) — optimal for this TTF.
    // Visual text size is controlled by textDrawScale applied at draw time.
    EnsureHudFont(topPaint);
    const float textDrawScale = c.textDrawScale;

    DrawHP(topPaint, st.currentHP, st.maxHP, c, textDrawScale);

    // Bomb count: Samus/Sylux in alt form only
    {
        const bool isBomber = (st.hunterID == static_cast<uint8_t>(HunterId::Samus) ||
                               st.hunterID == static_cast<uint8_t>(HunterId::Sylux));
        if (isBomber && st.isAlt) {
            DrawBombLeft(topPaint, st.ram, rom, st.offP, c, textDrawScale, hudScale);
        }
    }

    // Match Status + Rank & Time HUDs (non-adventure only, visible in all camera modes)
    DrawMatchStatusHud(topPaint, st.ram, rom, playerPosition, st.isAdventure, c);
    DrawRankAndTime(topPaint, st.ram, rom, playerPosition, st.isAdventure, c, textDrawScale);

    // Draw bottom screen overlay on top screen (visible in all camera modes incl. alt form)
    DrawBottomScreenOverlay(localCfg, topPaint, btmBuffer, (st.hunterID <= 6) ? st.hunterID : 0);

    if (!st.isFirstPerson) {
        // OPT-DR1: Crosshair not drawn in third-person / alt-form.
        // If it was drawn last frame, include its previous bbox so Screen.cpp clears it.
        ClearPreviousCrosshairDirty(c.crosshair);
        return CurrentHudDirtyRect(xf);
    }

    DrawWeaponAmmo(topPaint, st.ram, st.currentWeapon,
                   Read16(st.ram, addrAmmoSpecial), addrAmmoMissile,
                   st.maxAmmoSpecial, st.maxAmmoMissile, c, textDrawScale, hudScale);

    {
        const uint16_t havingWeapons = static_cast<uint16_t>(
            Read16(st.ram, addrHot.havingWeapons));
        DrawWeaponInventory(topPaint, st.ram, rom, playerPosition,
                            havingWeapons, st.currentWeapon, c, textDrawScale, hudScale);
    }

    if (!st.isTrans && !st.isAlt) {
        DrawCrosshair(topPaint, st.ram, rom, c, hudScale, topStretchX);
    } else {
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
        topPaint->drawEllipse(QRectF(c.radar.radarDstRect).adjusted(-expandDS, -expandDS, expandDS, expandDS));
    }

    // SVG frame outline — independent settings via frameOutline
    if (frameOL) {
        const float fExpandDS = (c.lastHudScale > 0.0f)
                                ? static_cast<float>(std::max(1, _fol.thickness)) / c.lastHudScale : 0.0f;
        topPaint->setOpacity(_fol.opacity);
        const QRectF& fr = c.radar.frameDstRect;
        topPaint->drawImage(QRectF(fr.x() - fExpandDS,
                                   fr.y() - fExpandDS,
                                   fr.width()  + fExpandDS * 2.0f,
                                   fr.height() + fExpandDS * 2.0f),
                            s_radarFrameOutline);
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
        const int srcCenterX  = kBtmOverlaySrcCenterX;
        const int srcCenterY  = kBtmOverlaySrcCenterY[hunterID];
        const int srcRadius   = c.radar.radarSrcRadius;
        const float bufScaleX = static_cast<float>(btmBuffer->width()) / 256.0f;
        const float bufScaleY = static_cast<float>(btmBuffer->height()) / 192.0f;

        QRect srcRect(static_cast<int>((srcCenterX - srcRadius) * bufScaleX),
                      static_cast<int>((srcCenterY - srcRadius) * bufScaleY),
                      static_cast<int>(srcRadius * 2 * bufScaleX),
                      static_cast<int>(srcRadius * 2 * bufScaleY));

        topPaint->setOpacity(c.radar.radarOpacity);
        topPaint->setClipPath(c.radar.radarClipPath);
        topPaint->drawImage(c.radar.radarDstRect, *btmBuffer, srcRect);
    }

    // OPT-SR2: Manual restore — only the attributes actually changed.
    topPaint->setClipping(false);
    topPaint->setRenderHint(QPainter::Antialiasing, false);
    topPaint->setRenderHint(QPainter::SmoothPixmapTransform, false);
    topPaint->setOpacity(savedOpacity);
}

