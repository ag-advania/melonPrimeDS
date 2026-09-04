// This file is a unity-build fragment included by MelonPrimeHudRender.cpp.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  P-7 forward declarations (full definitions follow CustomHud_Render)
// =========================================================================
// kEditElemCount is defined once in MelonPrimeHudRenderConfig.inc, next to the
// editRects array it sizes.
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
    bool hudEnabledSnapshot,
    float topStretchX, float hudScale,
    float hudOriginXds, float hudOriginYds,
    QPainter* directScoreboardPaint,
    NativePaintPerf* nativePaintPerf,
    bool overlayRetained,
    HudDirtyRegionSet* outDirty,
    HudDirtyRegionSet* outContent)
{
    MelonPrimePerf::CountCustomHudCall();
    MelonPrimePerf::ScopedHudPhase stateTimer(MelonPrimePerf::HudPhase::State);
    const ScopedHudConfigState active(hudConfig);
    const HudFrameTransform xf = MakeHudFrameTransform(topBuffer, topStretchX, hudScale,
                                                       hudOriginXds, hudOriginYds);

    HudDirtyRegionSet dirty;
    HudDirtyRegionSet content;
    // Single exit point: every early return still has to tell the presenter
    // which overlay regions changed, because the overlay is retained and
    // "drew nothing" is not the same as "shows nothing".
    const auto publish = [&](const QRect& bounds) -> QRect {
        if (outDirty)
            *outDirty = dirty;
        if (outContent)
            *outContent = content;
        const QRect dirtyBounds = dirty.Bounds();
        MelonPrimePerf::RecordHudDirtyRegions(
            static_cast<uint32_t>(dirty.Count()),
            dirty.PixelCount(),
            static_cast<uint64_t>(std::max(0, dirtyBounds.width()))
                * static_cast<uint64_t>(std::max(0, dirtyBounds.height())));
        return bounds;
    };

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
        // The editor paints the whole overlay and owns no per-element bounds,
        // so the retained record is dropped and the surface is repainted whole.
        if (topBuffer && overlayRetained) {
            HudDirtyRegionSet editClear;
            editClear.SetSingle(xf.overlayRect);
            HudClearOverlayRegions(*topBuffer, editClear);
        }
        s_hudRetained.Invalidate();
        DrawEditOverlay(topPaint, localCfg, topStretchX, btmBuffer);
        dirty.SetSingle(xf.overlayRect);
        content.SetSingle(xf.overlayRect);
        return publish(xf.overlayRect);  // edit mode covers full overlay
    }

    if (!isInGame)
        return publish(CustomHud_BlankRetainedOverlay(
            topBuffer, xf.overlayRect, dirty, content));

    HudRuntimeState st = {};
    if (!ReadHudRuntimeBaseState(emu, rom, playerPosition, st))
        return publish(CustomHud_BlankRetainedOverlay(
            topBuffer, xf.overlayRect, dirty, content));

    // Screen.cpp refreshed the authoritative enabled decision at the config
    // epoch boundary and used the same snapshot for visibility/restore. Keep
    // the render decision coherent with that boundary; NoHud restore was
    // already handled by the caller before entering this function.
    if (UNLIKELY(!hudEnabledSnapshot)) {
        CustomHud_RestoreNativeHudState(rom, st);
        return publish(CustomHud_BlankRetainedOverlay(
            topBuffer, xf.overlayRect, dirty, content));
    }

    // Read the shared Adventure/map-pause state before applying the common HUD
    // visibility decision used by HP and the other regular HUD elements.
    ReadHudRuntimeAdventurePauseState(rom, st);

    // P-3: Refresh config cache only when invalidated, or recompute anchor
    //       positions when topStretchX changes (window resize).
    //       Also refresh when hudScale changes - crosshair rects and textDrawScale
    //       are in actual output pixels divided by hudScale.
    // Refreshed BEFORE NoHudPatch_Sync so c.noHudMask is current.
    EnsureCachedConfigForFrame(localCfg, xf);
    const CachedHudConfig& c = s_cache;

    const bool hideForNativeUi = ShouldHideForGameplayState(
        st.isStartPressed,
        st.currentHP,
        st.isGameOver,
        st.isAdventure,
        st.isMapOrUserActionPaused);

    // Publish the complete gameplay-visibility result for the native radar
    // presenters. They may run before or after this painter path; either way,
    // all consumers on this game frame reuse the same state decision.
    if (HudRuntimeFrameCacheMatches(st))
    {
        s_hudRuntimeFrameCache.gameplayVisible =
            !hideForNativeUi && !st.isAdventureCameraScene;
        s_hudRuntimeFrameCache.gameplayVisibilityValid = true;
    }

    // Native UI needs the original helmet-layer instruction. Vulkan's held-TAB
    // path skips this overlay entirely and masks those layers presentation-only.
    uint16_t desiredNoHudMask = c.noHudMask;
    if (hideForNativeUi)
        desiredNoHudMask &= static_cast<uint16_t>(~(1u << NOHUD_HELMET));
    NoHudPatch_Sync(
        ActiveHudConfigState().noHudPatch, st.nds, st.romGroup, desiredNoHudMask);
    CustomHud_MarkNoHudPatchDesired();

    // Apply the transform/font before the regular HUD. The scoreboard follows
    // the same gameplay-state early return as HP: its own Show setting remains
    // independent, but it is hidden during START, death, game-over, and the
    // other states where the regular HUD is hidden.
    ApplyHudPainterTransform(topPaint, xf);
    EnsureHudFont(topPaint);

    // Adventure camera scenes blank the game's own HUD and crosshair; Custom HUD
    // follows. Deliberately not folded into hideForNativeUi: no native UI is
    // coming up here, so the helmet-layer suppression above must stay applied.
    if (hideForNativeUi || st.isAdventureCameraScene) {
        NotifyCrosshairZoomNotDrawn();
        return publish(CustomHud_BlankRetainedOverlay(
            topBuffer, xf.overlayRect, dirty, content));
    }

    ReadHudRuntimeVisibleState(rom, addrHot, st);

    // =====================================================================
    //  Sampling / planning half
    //
    //  Everything that reads guest RAM, resolves a snapshot, or loads an
    //  asset happens here, once, and produces a per-element visual stamp.
    //  Nothing below this point touches emulated memory, so the compositor is
    //  free to re-rasterise an element for z-order reasons without paying for
    //  a second sample.
    // =====================================================================
    HudComposePlan plan;
    HudElementDrawContext ctx;
    ctx.topPaint = topPaint;
    ctx.directScoreboardPaint = directScoreboardPaint;
    ctx.localCfg = &localCfg;
    ctx.config = &c;
    ctx.rom = &rom;
    ctx.state = &st;
    ctx.radarSource = btmBuffer;
    ctx.nativePaintPerf = nativePaintPerf;
    ctx.transform = xf;
    ctx.playerPosition = playerPosition;
    ctx.hudScale = hudScale;
    ctx.topStretchX = topStretchX;
    ctx.textDrawScale = c.textDrawScale;
    ctx.gameFrame = st.nds ? st.nds->NumFrames : 0;

    // Scoreboard work is profiled separately from state reads and the other
    // HUD painter work.  This also makes the expensive plan/raster boundary
    // visible in the aggregate developer report.
    stateTimer.Stop();
    if (c.scoreboard.show) {
        ctx.scoreboard = ReadHudRuntimeScoreboardSnapshot(rom, st);
        if (ctx.scoreboard) {
            EnsureScoreboardAssetsLoaded(c.scoreboard,
                                         std::min(c.scaleIcons, c.scaleScoreboard)
                                             * ScoreboardUserScale(c));
            auto& scoreboardPass = plan[HudElementId::Scoreboard];
            // Native Software paints the scoreboard onto the widget painter
            // rather than the retained overlay, so it repaints every presented
            // frame and owns no overlay pixels.
            scoreboardPass.detached = directScoreboardPaint != nullptr;
            scoreboardPass.present = !scoreboardPass.detached;
            // The scoreboard composites its retained panel image with source
            // composition, so it overwrites whatever it covers and does not
            // need the overlay wiped first. That is what lets it copy out a
            // single changed cell instead of the whole panel; if the panel
            // shrinks the compositor's shrink check wipes the remainder.
            scoreboardPass.selfClearing = !scoreboardPass.detached;
            scoreboardPass.revision = HudScoreboardRevision(
                *ctx.scoreboard, HudFrameState().scoreboardPlanGeneration);
        }
    }

    // Enemy Target: the ROM only publishes targetSlot/timer for ~2s after a
    // local hit, so most frames stop at the snapshot's early return.
    if (c.enemyTarget.show) {
        ctx.enemyTarget = ReadHudRuntimeEnemyTargetSnapshot(rom, st);
        if (ctx.enemyTarget) {
            EnsureEnemyTargetAssetsLoaded(c.enemyTarget,
                                          c.scaleIcons * EnemyTargetUserScale(c));
            auto& enemyPass = plan[HudElementId::EnemyTarget];
            enemyPass.present = true;
            enemyPass.revision = HudEnemyTargetRevision(*ctx.enemyTarget);
        }
    }

    // The crosshair is first in z-order so large zoom reticles sit behind HP,
    // ammo, radar, and the other Custom HUD elements.
    const bool drawCrosshairThisFrame = ShouldDrawCustomHudCrosshair(st);
    if (drawCrosshairThisFrame) {
        if (c.crosshair.cachedZoomAffectsVisual) {
            UpdateCrosshairZoomAmountForGameFrame(st.ram, rom, ctx.gameFrame);
            ctx.crosshairZoomAmount = ReadCrosshairZoomAmount(st.ram, rom);
        }
        auto& crosshairPass = plan[HudElementId::Crosshair];
        crosshairPass.present = true;
        // The crosshair commits its eased centre, zoom and animation phases as
        // part of drawing, so its stamp can only be taken afterwards: it
        // re-rasterises every frame and is reported dirty only when that stamp
        // actually moved. The raster is small and bounded, and this is what
        // keeps a motionless reticle from costing a GPU upload.
        crosshairPass.alwaysDirty = true;
    } else {
        // OPT-DR1: not drawn this frame (third person, alt form, transition,
        // scan visor). Reset the committed position so the next appearance
        // snaps; the composer clears the retained bounds from the presence
        // change, so the bbox this leaves behind is not needed.
        ClearPreviousCrosshairDirty(c.crosshair);
        s_chDirtyThisFrame = QRect();
    }

    {
        auto& hpPass = plan[HudElementId::Hp];
        hpPass.present = true;
        hpPass.revision = HudVisualRevision()
            .Mix(static_cast<uint32_t>(st.currentHP))
            .Mix(static_cast<uint32_t>(st.maxHP))
            .Value();
    }

    // Bomb count: Samus/Sylux in alt form only
    {
        const bool isBomber = (st.hunterID == static_cast<uint8_t>(HunterId::Samus) ||
                               st.hunterID == static_cast<uint8_t>(HunterId::Sylux));
        if (isBomber && st.isAlt) {
            ctx.bombCount = ReadHudRuntimeBombCount(rom, st);
            auto& bombPass = plan[HudElementId::Bomb];
            bombPass.present = true;
            bombPass.revision = HudVisualRevision()
                .Mix(static_cast<uint32_t>(ctx.bombCount))
                .Value();
        }
    }

    // Match Status + Rank & Time HUDs (non-adventure only, visible in all camera modes)
    if (c.matchStatus.matchStatusShow && !st.isAdventure) {
        ctx.matchStatusValid = ReadHudRuntimeMatchStatusState(
            rom, playerPosition, st, ctx.matchStatus);
        if (ctx.matchStatusValid) {
            auto& matchPass = plan[HudElementId::MatchStatus];
            matchPass.present = true;
            matchPass.revision = HudMatchStatusRevision(ctx.matchStatus);
        }
    }
    if (!st.isAdventure) {
        const auto& rankTime = c.rankTime;
        if (rankTime.rankShow)
            ctx.rankByte = ReadHudRuntimeRankByte(rom, playerPosition, st);
        if (rankTime.timeLeftShow)
            ctx.timeLeftSeconds = ReadHudRuntimeTimeLeftSeconds(rom, st);
        if (rankTime.timeLimitShow)
            ctx.timeLimitMinutes = ReadHudRuntimeTimeLimitMinutes(rom, st);
        if (rankTime.rankShow || rankTime.timeLeftShow || rankTime.timeLimitShow) {
            auto& rankTimePass = plan[HudElementId::RankTime];
            rankTimePass.present = true;
            // Seconds and minutes, not the 60 Hz tick they came from: the clock
            // text changes once a second, so the other 59 frames are genuinely
            // unchanged and must not be treated as a visual change.
            rankTimePass.revision = HudRankTimeRevision(
                ctx.rankByte, ctx.timeLeftSeconds, ctx.timeLimitMinutes);
        }
    }

    // Bottom screen overlay drawn on the top screen (visible in all camera
    // modes including alt form).
    if (c.radar.radarShow) {
        auto& radarPass = plan[HudElementId::Radar];
        radarPass.present = true;
        // With a colour-keyed source the crop mirrors live bottom-screen pixels
        // and cannot be stamped; without one (the GPU-native radar backends)
        // only the frame and outlines are drawn, and those are stable.
        radarPass.alwaysDirty = btmBuffer != nullptr;
        radarPass.revision = HudVisualRevision()
            .Mix(static_cast<uint32_t>((st.hunterID <= 6) ? st.hunterID : 0))
            .Value();
    }

    if (st.isFirstPerson) {
        const uint8_t weaponIdx = (st.currentWeapon < 9) ? st.currentWeapon : 0;
        const WeaponInfo& weaponInfo = kWeaponTable[weaponIdx];
        if (c.weaponInventory.show ||
            (weaponInfo.divisor > 0 && !weaponInfo.isMissile && weaponIdx != WeaponId::OmegaCannon))
            ctx.ammoSpecial = ReadHudRuntimeCurrentAmmoSpecial(rom, st);
        if (c.weaponInventory.show || weaponInfo.isMissile)
            ctx.ammoMissile = ReadHudRuntimeCurrentAmmoMissile(rom, st);

        auto& ammoPass = plan[HudElementId::WeaponAmmo];
        ammoPass.present = true;
        ammoPass.revision = HudVisualRevision()
            .Mix(static_cast<uint32_t>(st.currentWeapon))
            .Mix(static_cast<uint32_t>(ctx.ammoSpecial))
            .Mix(static_cast<uint32_t>(ctx.ammoMissile))
            .Mix(static_cast<uint32_t>(st.maxAmmoSpecial))
            .Mix(static_cast<uint32_t>(st.maxAmmoMissile))
            .Value();

        if (c.weaponInventory.show) {
            ctx.havingWeapons = ReadHudRuntimeHavingWeapons(addrHot, st);
            auto& inventoryPass = plan[HudElementId::WeaponInventory];
            inventoryPass.present = true;
            inventoryPass.revision = HudVisualRevision()
                .Mix(static_cast<uint32_t>(ctx.havingWeapons))
                .Mix(static_cast<uint32_t>(st.currentWeapon))
                .Mix(static_cast<uint32_t>(ctx.ammoSpecial))
                .Mix(static_cast<uint32_t>(ctx.ammoMissile))
                .Value();
        }
    }

    // =====================================================================
    //  Composition half
    // =====================================================================
    MelonPrimePerf::ScopedHudPhase drawTimer(MelonPrimePerf::HudPhase::QPainter);

    HudRetainedSurfaceKey surface;
    surface.overlayBits = topBuffer
        ? static_cast<const void*>(topBuffer->constBits()) : nullptr;
    surface.width = xf.overlayRect.width();
    surface.height = xf.overlayRect.height();
    surface.configEpoch = s_cacheEpoch;
    surface.visualGeneration = ActiveHudConfigState().visualGeneration;
    surface.fontPixelSize = s_frameFpx;
    surface.hudScale = hudScale;
    surface.stretchX = topStretchX;
    surface.originXds = hudOriginXds;
    surface.originYds = hudOriginYds;
    surface.editMode = false;

    const QRect bounds = CustomHud_ComposeRetained(
        topBuffer, plan, ctx, surface, overlayRetained, dirty, content);
    return publish(bounds);
}

QRect CustomHud_RenderCachedScoreboard(
    CustomHudConfigState& hudConfig,
    Config::Table& localCfg,
    QPainter* scoreboardPaint,
    float topStretchX,
    float hudScale,
    float hudOriginXds,
    float hudOriginYds,
    NativePaintPerf* nativePaintPerf)
{
    if (!scoreboardPaint || !scoreboardPaint->isActive())
        return QRect();

    const ScopedHudConfigState active(hudConfig);
    if (!s_cache.valid || !s_cache.scoreboard.show)
        return QRect();

    auto& hudFrame = HudFrameState();
    if (!hudFrame.scoreboardPlan.valid
        || !hudFrame.scoreboardPlan.structure.visible)
        return QRect();

    // The config epoch and plan were already validated by the visual-frame key.
    // Keep this cold fallback for a first repaint after a resize; it does not
    // touch live emulation state.
    const HudFrameTransform xf = MakeHudFrameTransform(
        nullptr, topStretchX, hudScale, hudOriginXds, hudOriginYds);
    EnsureCachedConfigForFrame(localCfg, xf);

    const QRect previousDirty = s_drawnDirtyPx;
    const QRect previousCrosshairDirty = s_chDirtyThisFrame;
    scoreboardPaint->save();
    scoreboardPaint->resetTransform();
    ApplyHudPainterTransform(scoreboardPaint, xf);
    DrawScoreboardPlan(scoreboardPaint, hudFrame.scoreboardPlan,
                       s_cache, hudScale, nullptr, nativePaintPerf);
    scoreboardPaint->restore();
    s_drawnDirtyPx = previousDirty;
    s_chDirtyThisFrame = previousCrosshairDirty;
    return QRect();
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
    MelonPrimePerf::ScopedHudElement element(
        MelonPrimePerf::HudElementMetric::Icon);
    MelonPrimePerf::CountHudElementImageDraw();

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
    MelonPrimePerf::ScopedHudElement element(
        MelonPrimePerf::HudElementMetric::Outline);

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
        MelonPrimePerf::CountHudElementImageDraw();
        topPaint->drawImage(c.radar.radarDstRect, *btmBuffer, s_radarSrcRect);
        AccumDirtyDs(topPaint, QRectF(c.radar.radarDstRect)); // OPT-DR2
    }

    // OPT-SR2: Manual restore — only the attributes actually changed.
    topPaint->setClipping(false);
    topPaint->setRenderHint(QPainter::Antialiasing, false);
    topPaint->setRenderHint(QPainter::SmoothPixmapTransform, false);
    topPaint->setOpacity(savedOpacity);
}
