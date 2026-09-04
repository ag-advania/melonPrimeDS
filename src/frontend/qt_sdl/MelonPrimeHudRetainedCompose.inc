// This file is a unity-build fragment included by MelonPrimeHudRender.cpp.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  Retained Custom HUD compositor
//
//  The overlay QImage survives between presented frames.  This fragment owns
//  what is done to it: which elements have to be re-rasterised, which overlay
//  regions are cleared first, in what order the survivors are re-applied, and
//  which regions a backend then has to upload.
//
//  Why this exists.  Presentation reuse used to be keyed on NDS::NumFrames:
//  a new emulated frame meant "clear the whole previous bounding box, redraw
//  every enabled element, upload the bounding box".  HP is top-left, the
//  scoreboard is centre, ammo is bottom-right, so that bounding box is most
//  of the window even on a frame where only the crosshair moved.  The pixel
//  work was proportional to the window, not to the change.
//
//  What replaces it:
//
//    - A per-element content stamp (HudElementPass::revision) built from the
//      values that decide that element's pixels.  Equal stamp, equal pixels.
//    - A per-element bounds record, so a change is localised.
//    - A bounded, allocation-free HudDirtyRegionSet instead of one bbox.
//    - Retained-mode overlap handling: after clearing a region, every element
//      whose pixels that region touched is re-applied in z-order, so an
//      unchanged element under a changed one is never left half-erased.
//
//  Correctness rules this fragment maintains:
//
//    - Element draws are pure functions of the draw context.  Re-running a
//      draw after a clear reproduces identical pixels, which is what makes
//      the repair pass below sound.
//    - The clear set always covers the previous bounds of every element being
//      redrawn, so a shrinking element cannot leave a tail behind.
//    - An element is only reported dirty when its pixels actually changed;
//      an element re-rasterised purely to restore z-order draws the same
//      pixels back and contributes nothing to the upload.
//
//  Thread: the presenting thread, under ScopedHudConfigState.  No locks, no
//  allocation, no config lookups, no guest RAM reads.
// =========================================================================

// Escape hatch. With the diff disabled the composer still tracks per-element
// bounds and produces a multi-region dirty set, but every present element is
// re-rasterised every frame, which is the historical behaviour. Kept so a
// pixel regression can be bisected against the diff itself rather than
// against the region bookkeeping.
#ifndef MELONPRIME_HUD_ELEMENT_DIFF
#define MELONPRIME_HUD_ELEMENT_DIFF 1
#endif

// Everything one element needs to draw itself. Resolved once per frame by
// CustomHud_Render; no entry here is read from guest RAM or from Config during
// a draw, so a repair pass costs rasterisation only.
struct HudElementDrawContext {
    QPainter* topPaint = nullptr;
    QPainter* directScoreboardPaint = nullptr;
    Config::Table* localCfg = nullptr;
    const CachedHudConfig* config = nullptr;
    const RomAddresses* rom = nullptr;
    HudRuntimeState* state = nullptr;
    QImage* radarSource = nullptr;
    NativePaintPerf* nativePaintPerf = nullptr;
    const ScoreboardSnapshot* scoreboard = nullptr;
    const EnemyTargetSnapshot* enemyTarget = nullptr;
    MatchStatusResolvedState matchStatus{};
    HudFrameTransform transform{};
    uint32_t gameFrame = 0;
    uint16_t ammoSpecial = 0;
    uint16_t ammoMissile = 0;
    uint16_t havingWeapons = 0;
    int timeLeftSeconds = 0;
    int timeLimitMinutes = 0;
    float crosshairZoomAmount = 0.0f;
    float textDrawScale = 1.0f;
    float hudScale = 1.0f;
    float topStretchX = 1.0f;
    uint8_t playerPosition = 0;
    uint8_t bombCount = 0;
    uint8_t rankByte = 0xFF;
    bool matchStatusValid = false;
    bool drawCrosshair = false;
};

// One frame's intent plus the results of executing it.
struct HudComposePlan {
    std::array<HudElementPass, kHudElementIdCount> pass{};
    // Where the element ended up: what it occupies (newBounds, used for the
    // retained record and for the composite) and which of those pixels it
    // actually rewrote (touchedBounds, used for the upload). They differ only
    // for a self-clearing element that repainted part of itself.
    std::array<QRect, kHudElementIdCount> newBounds{};
    std::array<QRect, kHudElementIdCount> touchedBounds{};
    std::array<std::uint64_t, kHudElementIdCount> postRevision{};
    std::array<bool, kHudElementIdCount> redraw{};
    // Whether the element's retained pixels were wiped before it was redrawn.
    // A cleared element has to repaint all of itself; an uncleared one may
    // repaint only what changed.
    std::array<bool, kHudElementIdCount> cleared{};

    HudElementPass& operator[](HudElementId id) noexcept
    {
        return pass[HudElementIndex(id)];
    }
};

// A draw pass may need at most one repair round in practice (an element grows
// over a neighbour that was not scheduled). The third attempt exists only so a
// pathological layout degrades into the full redraw below instead of looping.
static constexpr int kHudComposeMaxPasses = 3;

// =========================================================================
//  Visual stamps
//
//  Each helper hashes exactly the values that reach pixels, after the same
//  transformation the draw applies.  The match clock is the reason this
//  matters: its raw field advances 60 times a second while the text it
//  produces changes once, so the stamp hashes the displayed second and 59 of
//  every 60 frames are correctly recognised as unchanged.
// =========================================================================

static void HudMixScoreboardVisual(HudVisualRevision& revision,
                                   std::int32_t rawValue, bool isTime,
                                   std::uint8_t gameMode)
{
    const ScoreboardVisualValue visual =
        MakeScoreboardVisualValue(rawValue, isTime, gameMode);
    revision.Mix(visual.value).Mix(visual.isTime).Mix(visual.isMax).Mix(visual.valid);
}

static std::uint64_t HudScoreboardRevision(const ScoreboardSnapshot& snapshot,
                                           std::uint32_t planGeneration)
{
    const ScoreboardMetricSpec metric = ResolveScoreboardMetric(snapshot.gameMode);
    HudVisualRevision revision;
    revision.Mix(planGeneration)
            .Mix(snapshot.matchSerial)
            .Mix(static_cast<std::uint32_t>(snapshot.gameMode))
            .Mix(static_cast<std::uint32_t>(snapshot.activePlayers))
            .Mix(static_cast<std::uint32_t>(snapshot.teamFlags))
            .Mix(snapshot.teams)
            .Mix(snapshot.visible);
    for (const std::uint8_t slot : snapshot.resultSlots)
        revision.Mix(static_cast<std::uint32_t>(slot));

    for (const auto& player : snapshot.playersBySlot) {
        revision.Mix(static_cast<std::uint32_t>(player.slot))
                .Mix(static_cast<std::uint32_t>(player.teamIndex))
                .Mix(static_cast<std::uint32_t>(player.hunterId))
                .Mix(static_cast<std::uint32_t>(player.stars))
                .Mix(static_cast<std::uint32_t>(player.standing))
                .Mix(static_cast<std::uint32_t>(player.teamStanding))
                .Mix(player.active);
        // Only the two metric columns the mode actually shows are hashed, and
        // each through the same visual conversion the cell uses.
        HudMixScoreboardVisual(revision,
            metric.firstIsTime ? player.time : player.points,
            metric.firstIsTime, snapshot.gameMode);
        HudMixScoreboardVisual(revision,
            metric.secondIsTime ? player.time
                : (metric.secondIsKills ? player.kills : player.deaths),
            metric.secondIsTime, snapshot.gameMode);
    }

    for (const auto& team : snapshot.teamsByIndex) {
        HudMixScoreboardVisual(revision,
            metric.firstIsTime ? team.time : team.points,
            metric.firstIsTime, snapshot.gameMode);
        HudMixScoreboardVisual(revision,
            metric.secondIsTime ? team.time
                : (metric.secondIsKills ? team.kills : team.deaths),
            metric.secondIsTime, snapshot.gameMode);
    }
    return revision.Value();
}

static std::uint64_t HudEnemyTargetRevision(const EnemyTargetSnapshot& snapshot)
{
    // Deliberately excludes `timer`: it counts down every frame and decides
    // only whether the panel is up, which is already the presence flag.
    HudVisualRevision revision;
    revision.Mix(static_cast<std::uint32_t>(snapshot.slot))
            .Mix(static_cast<std::uint32_t>(snapshot.hunterId))
            .Mix(static_cast<std::uint32_t>(snapshot.teamIndex))
            .Mix(static_cast<std::uint32_t>(snapshot.gameMode))
            .Mix(snapshot.teams)
            .Mix(static_cast<std::uint32_t>(snapshot.hp))
            .Mix(static_cast<std::uint32_t>(snapshot.maxHp))
            .Mix(static_cast<std::uint64_t>(qHash(snapshot.displayName)));
    HudMixScoreboardVisual(revision, snapshot.rawValue, snapshot.valueIsTime,
                           snapshot.gameMode);
    HudMixScoreboardVisual(revision, snapshot.rawGoal, snapshot.valueIsTime,
                           snapshot.gameMode);
    return revision.Value();
}

static std::uint64_t HudMatchStatusRevision(const MatchStatusResolvedState& state)
{
    HudVisualRevision revision;
    revision.Mix(static_cast<std::uint32_t>(state.mode))
            .Mix(static_cast<std::uint32_t>(state.xx))
            .Mix(state.currentValue)
            .Mix(state.goalValue)
            .Mix(state.isTimeMode);
    return revision.Value();
}

static std::uint64_t HudRankTimeRevision(std::uint8_t rankByte,
                                         int timeLeftSeconds,
                                         int timeLimitMinutes)
{
    HudVisualRevision revision;
    revision.Mix(static_cast<std::uint32_t>(rankByte))
            .Mix(timeLeftSeconds)
            .Mix(timeLimitMinutes);
    return revision.Value();
}

// =========================================================================
//  Element dispatch
//
//  A fixed enum and a switch, deliberately: no virtual dispatch, no function
//  table, no std::function. The compiler keeps this a jump table and every
//  callee stays inlinable.
// =========================================================================
// `fullRepaint` tells an element that its retained pixels were cleared, so a
// partial repaint of itself would leave holes. Only the scoreboard, which can
// repaint a single cell out of its panel, distinguishes the two.
static void DrawHudElementById(HudElementId id, const HudElementDrawContext& ctx,
                               bool fullRepaint)
{
    const CachedHudConfig& c = *ctx.config;
    QPainter* p = ctx.topPaint;

    switch (id) {
    case HudElementId::Scoreboard: {
        if (!ctx.scoreboard)
            break;
        MelonPrimePerf::ScopedHudElement element(
            MelonPrimePerf::HudElementMetric::Scoreboard);
        if (ctx.directScoreboardPaint) {
            // Native Software repaints the scoreboard straight onto the widget
            // painter so the original scoreboard-before-HUD draw order is kept
            // without a second image composite. Those pixels never land on the
            // retained overlay, so they must not enter its dirty accounting.
            const QRect overlayDirtyBefore = s_drawnDirtyPx;
            ctx.directScoreboardPaint->save();
            ctx.directScoreboardPaint->setFont(p->font());
            ctx.directScoreboardPaint->resetTransform();
            ApplyHudPainterTransform(ctx.directScoreboardPaint, ctx.transform);
            EnsureHudFont(ctx.directScoreboardPaint);
            // The widget painter is repainted from scratch every presented
            // frame, so this target always needs the whole scoreboard.
            DrawScoreboard(ctx.directScoreboardPaint, *ctx.scoreboard, c,
                           ctx.hudScale, ctx.playerPosition, ctx.nativePaintPerf,
                           true);
            ctx.directScoreboardPaint->restore();
            s_drawnDirtyPx = overlayDirtyBefore;
        } else {
            DrawScoreboard(p, *ctx.scoreboard, c, ctx.hudScale,
                           ctx.playerPosition, ctx.nativePaintPerf, fullRepaint);
        }
        break;
    }
    case HudElementId::EnemyTarget: {
        if (!ctx.enemyTarget)
            break;
        MelonPrimePerf::ScopedHudElement element(
            MelonPrimePerf::HudElementMetric::EnemyTarget);
        DrawEnemyTarget(p, *ctx.enemyTarget, c, ctx.hudScale);
        break;
    }
    case HudElementId::Crosshair: {
        MelonPrimePerf::ScopedHudElement element(
            MelonPrimePerf::HudElementMetric::Crosshair);
        DrawCrosshair(p, ctx.state->ram, *ctx.rom, c, ctx.hudScale,
                      ctx.topStretchX, ctx.gameFrame, ctx.crosshairZoomAmount);
        break;
    }
    case HudElementId::Hp: {
        MelonPrimePerf::ScopedHudElement element(
            MelonPrimePerf::HudElementMetric::Hp);
        DrawHP(p, ctx.state->currentHP, ctx.state->maxHP, c, ctx.textDrawScale);
        break;
    }
    case HudElementId::Bomb: {
        MelonPrimePerf::ScopedHudElement element(
            MelonPrimePerf::HudElementMetric::Bomb);
        DrawBombLeft(p, ctx.bombCount, c, ctx.textDrawScale, ctx.hudScale);
        break;
    }
    case HudElementId::MatchStatus: {
        MelonPrimePerf::ScopedHudElement element(
            MelonPrimePerf::HudElementMetric::MatchStatus);
        DrawMatchStatusResolved(p, ctx.matchStatus, c);
        break;
    }
    case HudElementId::RankTime: {
        MelonPrimePerf::ScopedHudElement element(
            MelonPrimePerf::HudElementMetric::RankTime);
        DrawRankAndTimeResolved(p, c, ctx.textDrawScale, ctx.rankByte,
                                ctx.timeLeftSeconds, ctx.timeLimitMinutes);
        break;
    }
    case HudElementId::Radar: {
        MelonPrimePerf::ScopedHudElement element(
            MelonPrimePerf::HudElementMetric::RadarCpuOverlay);
        DrawBottomScreenOverlay(*ctx.localCfg, p, ctx.radarSource,
                                (ctx.state->hunterID <= 6) ? ctx.state->hunterID : 0);
        break;
    }
    case HudElementId::WeaponAmmo: {
        MelonPrimePerf::ScopedHudElement element(
            MelonPrimePerf::HudElementMetric::WeaponAmmo);
        DrawWeaponAmmo(p, ctx.state->currentWeapon,
                       ctx.ammoSpecial, ctx.ammoMissile,
                       ctx.state->maxAmmoSpecial, ctx.state->maxAmmoMissile,
                       c, ctx.textDrawScale, ctx.hudScale);
        break;
    }
    case HudElementId::WeaponInventory: {
        MelonPrimePerf::ScopedHudElement element(
            MelonPrimePerf::HudElementMetric::WeaponInventory);
        DrawWeaponInventory(p, ctx.havingWeapons, ctx.state->currentWeapon,
                            ctx.ammoSpecial, ctx.ammoMissile,
                            c, ctx.textDrawScale, ctx.hudScale);
        break;
    }
    case HudElementId::Count:
        break;
    }
}

// The crosshair commits its geometry as it draws, so its stamp can only be
// taken afterwards. That is enough: if the committed geometry and the drawn
// bounds both match the retained record, the pixels just written are the
// pixels that were already there, and the region does not need uploading.
static std::uint64_t HudCrosshairPostRevision()
{
    HudVisualRevision revision;
    revision.Mix(static_cast<std::int32_t>(s_chPrevPxCx))
            .Mix(static_cast<std::int32_t>(s_chPrevPxCy))
            .Mix(static_cast<float>(s_chPrevGeomScale))
            .Mix(static_cast<float>(s_chPrevZoomT))
            .Mix(static_cast<float>(s_chPrevScopeT))
            .Mix(static_cast<float>(s_chDisplayZoom))
            // Animated crosshair styles advance a phase every frame, so an
            // animating reticle never produces a repeat stamp and is never
            // wrongly held back from the upload.
            .Mix(s_chGlitchFxPhase)
            .Mix(s_chCyberPhase)
            .Mix(s_chDigitalPhase)
            .Mix(s_chTacticalPhase)
            .Mix(s_chLidarPhase)
            .Mix(s_chGlitchPhase);
    return revision.Value();
}

static std::uint64_t HudElementPostRevision(HudElementId id,
                                            const HudElementDrawContext& ctx,
                                            std::uint64_t plannedRevision)
{
    (void)ctx;
    if (id == HudElementId::Crosshair)
        return HudCrosshairPostRevision();
    return plannedRevision;
}

// Transparent in ARGB32_Premultiplied is a zero word, so the clear is a
// scanline memset rather than a QPainter fill.
static void HudClearOverlayRegion(QImage& overlay, const QRect& rect)
{
    const QRect clipped = rect & QRect(0, 0, overlay.width(), overlay.height());
    if (clipped.isEmpty())
        return;
    const std::size_t clearBytes =
        static_cast<std::size_t>(clipped.width()) * sizeof(QRgb);
    const int left = clipped.left();
    const int bottom = clipped.bottom();
    for (int y = clipped.top(); y <= bottom; ++y) {
        std::memset(reinterpret_cast<QRgb*>(overlay.scanLine(y)) + left,
                    0, clearBytes);
    }
}

static void HudClearOverlayRegions(QImage& overlay, const HudDirtyRegionSet& regions)
{
    if (regions.IsEmpty())
        return;
    MelonPrimePerf::ScopedHudPhase clearTimer(MelonPrimePerf::HudPhase::Clear);
    MelonPrimePerf::AddHudClearPixels(regions.PixelCount());
    for (int i = 0; i < regions.Count(); ++i)
        HudClearOverlayRegion(overlay, regions.Region(i));
}

// Re-apply every element whose pixels the pending clear (or an element drawn
// below it) would disturb. `boundsFor` supplies the area a scheduled element
// is expected to occupy: the retained bounds while planning, the measured
// bounds when validating an executed pass.
template <typename BoundsFn>
static bool HudExpandOverlapRedraw(const HudRetainedComposition& retained,
                                   HudComposePlan& plan,
                                   HudDirtyRegionSet& clearSet,
                                   BoundsFn boundsFor)
{
    bool expanded = false;
    bool changed = true;
    while (changed) {
        changed = false;
        HudDirtyRegionSet below;
        for (std::size_t i = 0; i < kHudElementIdCount; ++i) {
            if (plan.redraw[i]) {
                below.Add(boundsFor(i));
                continue;
            }
            const HudElementVisualStamp& stamp = retained.elements[i];
            if (!stamp.present || stamp.bounds.isEmpty())
                continue;
            // Erased by the clear, or overdrawn by an element that belongs
            // underneath this one. Either way its pixels must be re-applied.
            if (!clearSet.Intersects(stamp.bounds) && !below.Intersects(stamp.bounds))
                continue;
            plan.redraw[i] = true;
            clearSet.Add(stamp.bounds);
            below.Add(stamp.bounds);
            changed = true;
            expanded = true;
        }
    }
    return expanded;
}

static void HudRunDrawPass(HudComposePlan& plan, const HudElementDrawContext& ctx)
{
    for (std::size_t i = 0; i < kHudElementIdCount; ++i) {
        plan.newBounds[i] = QRect();
        plan.touchedBounds[i] = QRect();
        if (!plan.redraw[i])
            continue;

        const HudElementPass& entry = plan.pass[i];
        if (!entry.present && !entry.detached)
            continue;

        const QRect drawnBefore = s_drawnDirtyPx;
        const QRect crosshairBefore = s_chDirtyThisFrame;
        s_drawnDirtyPx = QRect();
        s_chDirtyThisFrame = QRect();
        s_hudElementOccupancy = QRect();

        DrawHudElementById(static_cast<HudElementId>(i), ctx, plan.cleared[i]);

        const QRect touched =
            (s_drawnDirtyPx | s_chDirtyThisFrame) & ctx.transform.overlayRect;
        // An element that rewrote only part of itself declares what it still
        // occupies; everything else occupies exactly what it drew.
        const QRect occupied = s_hudElementOccupancy.isNull()
            ? touched
            : (s_hudElementOccupancy & ctx.transform.overlayRect);
        s_hudElementOccupancy = QRect();
        plan.newBounds[i] = entry.detached ? QRect() : occupied;
        plan.touchedBounds[i] = entry.detached ? QRect() : touched;
        plan.postRevision[i] = HudElementPostRevision(
            static_cast<HudElementId>(i), ctx, entry.revision);

        s_drawnDirtyPx = drawnBefore | s_drawnDirtyPx;
        s_chDirtyThisFrame = crosshairBefore | s_chDirtyThisFrame;
    }
}

// Fold this frame's results back into the retained record and report which
// overlay regions actually changed.
static void HudCommitComposition(HudRetainedComposition& retained,
                                 const HudComposePlan& plan,
                                 const HudRetainedSurfaceKey& surface,
                                 bool fullRedraw,
                                 HudDirtyRegionSet& outDirty)
{
    for (std::size_t i = 0; i < kHudElementIdCount; ++i) {
        HudElementVisualStamp& stamp = retained.elements[i];
        const HudElementPass& entry = plan.pass[i];
        const bool present = entry.present && !entry.detached
            && !plan.newBounds[i].isEmpty();

        if (!plan.redraw[i]) {
            // Untouched pixels, untouched record.
            continue;
        }

        const bool contentChanged = fullRedraw
            || present != stamp.present
            || (present && (plan.newBounds[i] != stamp.bounds
                            || plan.postRevision[i] != stamp.revision));
        if (contentChanged) {
            if (!plan.cleared[i] && present && stamp.present
                && plan.newBounds[i] == stamp.bounds) {
                // Nothing wiped these pixels and the element still covers the
                // same rectangle, so only what it rewrote can have changed.
                outDirty.Add(plan.touchedBounds[i]);
            } else {
                if (stamp.present)
                    outDirty.Add(stamp.bounds);
                if (present)
                    outDirty.Add(plan.newBounds[i]);
            }
            MelonPrimePerf::CountHudElementVisualChange();
        }

        stamp.present = present;
        stamp.bounds = present ? plan.newBounds[i] : QRect();
        stamp.revision = present ? plan.postRevision[i] : 0;
    }

    retained.surface = surface;
    retained.valid = true;
}

// =========================================================================
//  CustomHud_ComposeRetained
//
//  Runs one retained composition into `overlay` and reports both what changed
//  (outDirty, for the backend upload) and where the HUD currently has pixels
//  (outContent, for the final composite). Returns the dirty bounding box so
//  callers that still speak in one rectangle keep working.
// =========================================================================
static QRect CustomHud_ComposeRetained(QImage* overlay,
                                       HudComposePlan& plan,
                                       const HudElementDrawContext& ctx,
                                       const HudRetainedSurfaceKey& surface,
                                       bool overlayRetained,
                                       HudDirtyRegionSet& outDirty,
                                       HudDirtyRegionSet& outContent)
{
    HudRetainedComposition& retained = s_hudRetained;
    outDirty.Reset();
    outContent.Reset();

    bool fullRedraw = !overlayRetained || !retained.valid
        || retained.surface != surface || overlay == nullptr;
#if !MELONPRIME_HUD_ELEMENT_DIFF
    fullRedraw = true;
#endif

    HudDirtyRegionSet clearSet;
    plan.cleared.fill(false);
    if (fullRedraw) {
        for (std::size_t i = 0; i < kHudElementIdCount; ++i) {
            plan.redraw[i] = plan.pass[i].present || plan.pass[i].detached;
            plan.cleared[i] = true;
        }
        // The retained pixels either belong to a different surface or cannot
        // be trusted, so start from a clean overlay rather than from per-element
        // bounds that may describe a layout that no longer exists. "Cannot be
        // trusted" is not "is already zero": a panel may hand back an overlay it
        // never reallocated, and leaving that unclear would show the previous
        // HUD underneath this one.
        clearSet.SetSingle(ctx.transform.overlayRect);
        retained.Invalidate();
        MelonPrimePerf::CountHudFullRecompose();
    } else {
        for (std::size_t i = 0; i < kHudElementIdCount; ++i) {
            const HudElementPass& entry = plan.pass[i];
            const HudElementVisualStamp& stamp = retained.elements[i];
            plan.redraw[i] = entry.detached
                || entry.present != stamp.present
                || (entry.present
                    && (entry.alwaysDirty || entry.revision != stamp.revision));
        }
        for (std::size_t i = 0; i < kHudElementIdCount; ++i) {
            // A self-clearing element rewrites whatever it still occupies, so
            // wiping it first would only force it to repaint all of itself.
            if (plan.redraw[i] && retained.elements[i].present
                && !plan.pass[i].selfClearing)
                clearSet.Add(retained.elements[i].bounds);
        }
        // Plan the overlap fan-out against the retained bounds; the pass below
        // validates it against what was really drawn.
        HudExpandOverlapRedraw(retained, plan, clearSet,
            [&](std::size_t index) { return retained.elements[index].bounds; });
        MelonPrimePerf::CountHudPartialRecompose();
    }

    if (overlay == nullptr) {
        // No retained surface to compose into (edit-mode probes and the golden
        // harness pass a painter without a backing image). Draw once and let
        // the caller keep using the bounding rectangle.
        plan.cleared.fill(true);
        HudRunDrawPass(plan, ctx);
        return (s_drawnDirtyPx | s_chDirtyThisFrame) & ctx.transform.overlayRect;
    }

    const auto refreshClearedFlags = [&]() {
        for (std::size_t i = 0; i < kHudElementIdCount; ++i) {
            if (fullRedraw) {
                plan.cleared[i] = true;
                continue;
            }
            const HudElementVisualStamp& stamp = retained.elements[i];
            plan.cleared[i] = !plan.pass[i].selfClearing
                || (stamp.present && clearSet.Intersects(stamp.bounds));
        }
    };

    for (int attempt = 0; attempt < kHudComposeMaxPasses; ++attempt) {
        refreshClearedFlags();
        HudClearOverlayRegions(*overlay, clearSet);
        HudRunDrawPass(plan, ctx);

        if (fullRedraw)
            break;

        // A self-clearing element was allowed to skip its clear on the promise
        // that it still covers everything it used to. If it shrank instead, the
        // pixels it no longer reaches are stale and have to be wiped.
        bool repairNeeded = false;
        for (std::size_t i = 0; i < kHudElementIdCount; ++i) {
            if (!plan.redraw[i] || plan.cleared[i])
                continue;
            const HudElementVisualStamp& stamp = retained.elements[i];
            if (!stamp.present || stamp.bounds.isEmpty())
                continue;
            if (plan.newBounds[i].contains(stamp.bounds))
                continue;
            clearSet.Add(stamp.bounds);
            repairNeeded = true;
        }
        repairNeeded |= HudExpandOverlapRedraw(
            retained, plan, clearSet,
            [&](std::size_t index) { return plan.newBounds[index]; });
        if (!repairNeeded)
            break;

        MelonPrimePerf::CountHudComposeRepairPass();
        if (attempt + 1 == kHudComposeMaxPasses) {
            // Give up on incremental repair and rebuild the whole overlay; the
            // draws are pure, so a full pass is always a valid answer.
            fullRedraw = true;
            clearSet.SetSingle(ctx.transform.overlayRect);
            for (std::size_t i = 0; i < kHudElementIdCount; ++i) {
                plan.redraw[i] = plan.pass[i].present || plan.pass[i].detached;
                plan.cleared[i] = true;
            }
            HudClearOverlayRegions(*overlay, clearSet);
            HudRunDrawPass(plan, ctx);
            MelonPrimePerf::CountHudComposeRepairFallback();
            break;
        }
    }

    HudCommitComposition(retained, plan, surface, fullRedraw, outDirty);
    outDirty.IntersectWith(ctx.transform.overlayRect);
    retained.CollectContent(outContent);
    outContent.IntersectWith(ctx.transform.overlayRect);
    return outDirty.Bounds();
}

// Drop every retained HUD pixel from the overlay. Used by the paths that
// decide mid-frame that the Custom HUD must not be visible (native UI is up,
// the HUD was switched off, the session left a match): the overlay is retained,
// so "draw nothing" is not the same as "show nothing".
static QRect CustomHud_BlankRetainedOverlay(QImage* overlay,
                                            const QRect& overlayRect,
                                            HudDirtyRegionSet& outDirty,
                                            HudDirtyRegionSet& outContent)
{
    HudRetainedComposition& retained = s_hudRetained;
    outDirty.Reset();
    outContent.Reset();

    if (retained.valid) {
        for (const auto& element : retained.elements) {
            if (element.present)
                outDirty.Add(element.bounds);
        }
    }
    retained.Invalidate();
    outDirty.IntersectWith(overlayRect);
    if (overlay != nullptr)
        HudClearOverlayRegions(*overlay, outDirty);
    return outDirty.Bounds();
}
