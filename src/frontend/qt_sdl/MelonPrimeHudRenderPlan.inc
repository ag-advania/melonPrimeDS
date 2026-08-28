// This file is a unity-build fragment included by MelonPrimeHudRender.cpp.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  Custom HUD render planning
//
//  Snapshot -> draw-ready plan.  Layout, text shaping, glyph-outline caches,
//  painter transform and dirty-rect bookkeeping live here; nothing in this
//  fragment reads emulated game memory or knows what a game mode means.
//  It changes for font, DPI, HUD scaling, layout and QPainter reasons.
//
//  The RAM-facing half is MelonPrimeHudRuntimeSample.inc, which is included
//  after this fragment: the per-instance aggregate HudFrameOwnedState holds
//  both the plan caches declared here and the sampling caches declared there,
//  so the plan types have to be complete first.  Plan *builders* (the
//  Build*RenderPlan functions) stay next to the draw code that owns them in
//  MelonPrimeHudRenderDraw.inc.
// =========================================================================

// Returns the effective outline: global override when enabled, otherwise the per-element one.
static inline const HudOutlineConfig& EffOL(const CachedHudConfig& c, const HudOutlineConfig& el) {
    return c.globalOutline.enable ? c.globalOutline : el;
}

struct HudFrameTransform {
    float topStretchX;
    float hudScale;
    float txDs;
    float tyDs;
    QRect overlayRect;
};

struct ScoreboardVisualValue {
    int32_t value = 0;
    bool isTime = false;
    bool isMax = false;
    bool valid = false;

    bool operator==(const ScoreboardVisualValue& other) const noexcept
    {
        return value == other.value
            && isTime == other.isTime
            && isMax == other.isMax
            && valid == other.valid;
    }
};

struct ScoreboardStructureKey {
    uint32_t matchSerial = 0;
    uint8_t gameMode = 0;
    uint8_t activePlayers = 0;
    uint16_t teamFlags = 0;
    bool teams = false;
    bool visible = false;
    std::array<uint8_t, 4> resultSlots = {0xFF, 0xFF, 0xFF, 0xFF};
    std::array<uint8_t, 4> teamIndex = {0xFF, 0xFF, 0xFF, 0xFF};
    std::array<bool, 4> active = {false, false, false, false};

    bool operator==(const ScoreboardStructureKey& other) const noexcept
    {
        return matchSerial == other.matchSerial
            && gameMode == other.gameMode
            && activePlayers == other.activePlayers
            && teamFlags == other.teamFlags
            && teams == other.teams
            && visible == other.visible
            && resultSlots == other.resultSlots
            && teamIndex == other.teamIndex
            && active == other.active;
    }
};

// The scoreboard is presented from the emulation thread, but its painter can
// be entered more than once for the same emulated frame (for example while a
// window is being refreshed).  Keep the final display strings and geometry in
// a bounded plan so the draw pass never repeats localization, elision, or
// width measurement for unchanged cells.
struct ScoreboardPlanText {
    QString display;
    QRectF rect;
    QColor color;
    ScoreboardVisualValue visual;
    QPainterPath outlinePath;
    bool outlinePathPrepared = false;
    qreal advance = 0.0;
    Qt::Alignment alignment = Qt::AlignLeft;
    bool rankFont = false;
    bool enabled = false;
};

struct ScoreboardPlanRow {
    bool team = false;
    uint8_t slot = 0xFF;
    uint8_t teamIndex = 0xFF;
    QColor color;
    ScoreboardPlanText name;
    ScoreboardPlanText rank;
    ScoreboardPlanText first;
    ScoreboardPlanText second;
    QRectF hunterIconRect;
    QRectF starsRect;
    qreal starsFullW = 0.0;
    qreal starsFullH = 0.0;
    uint8_t hunterId = 0xFF;
    uint8_t stars = 0xFF;
};

struct ScoreboardRenderPlan {
    bool valid = false;
    uint32_t configEpoch = 0;
    int textCacheGeneration = 0;
    int frameFontPx = 0;
    float hudScale = 0.0f;
    float stretchX = 0.0f;
    uint8_t localPlayerSlot = 0xFF;
    ScoreboardStructureKey structure{};
    int panelX = 0;
    int panelY = 0;
    int panelW = 1;
    int panelH = 0;
    int rowH = 12;
    int teamRowH = 12;
    int rowSpacing = 0;
    int columnSpacing = 0;
    int rightPadding = 0;
    int portraitW = 20;
    int playerW = 1;
    int firstW = 20;
    int secondW = 20;
    int rowCount = 0;
    std::array<ScoreboardPlanText, 3> headers{};
    std::array<ScoreboardPlanRow, 8> rows{};
};

struct EnemyTargetLayout {
    float panelX = 0.0f, panelY = 0.0f;
    float panelW = 1.0f, panelH = 1.0f;
    float portraitX = 0.0f, portraitY = 0.0f, portraitW = 0.0f, portraitH = 0.0f;
    float textX = 0.0f;
    float textW = 1.0f;
    float nameY = 0.0f;
    float bar1Y = 0.0f, bar2Y = 0.0f;
    float barLen = 0.0f, barH = 0.0f;
    float numberX = 0.0f, numberY = 0.0f;
    float scoreY = 0.0f;
    float lineH = 1.0f;
    bool hasPortrait = false;
    bool hasName = false;
    bool hasBar1 = false, hasBar2 = false;
    bool hasNumber = false;
    bool hasScore = false;
    QString numberText;
    QString scoreText;
    // Measured once while sizing the column; the draw pass reuses these
    // instead of asking QFontMetrics again for the same strings.
    float nameAdvance = 0.0f;
    float numberAdvance = 0.0f;
    float scoreAdvance = 0.0f;
};

// =========================================================================
//  Enemy Target render plan
//
//  The panel is only up for ~2s after a hit, but that window lands in the
//  middle of a firefight, so it is exactly where a per-frame hitch is least
//  affordable. Everything that survives a frame is cached here and rebuilt
//  only when something that changes the drawn result changes:
//
//    - QPainterPath::addText() is glyph-outline extraction; it is the single
//      most expensive call in this path and outlines are on by default.
//    - QFontMetrics::horizontalAdvance() shapes the string.
//    - The HP/score strings allocate.
//
//  Within the window the target slot, name, hunter, and goal are fixed and HP
//  only moves when damage lands, so the overwhelming majority of frames reuse
//  the plan whole and issue nothing but draw calls.
// =========================================================================
struct EnemyTargetPlanKey {
    uint8_t slot = 0xFF;
    uint8_t hunterId = 0xFF;
    uint8_t teamIndex = 0xFF;
    uint8_t gameMode = 0;
    bool teams = false;
    QString displayName;
    uint16_t hp = 0;
    uint16_t maxHp = 0;
    int32_t rawValue = 0;
    int32_t rawGoal = 0;
    bool valueIsTime = false;

    bool operator==(const EnemyTargetPlanKey& other) const noexcept
    {
        return slot == other.slot
            && hunterId == other.hunterId
            && teamIndex == other.teamIndex
            && gameMode == other.gameMode
            && teams == other.teams
            && hp == other.hp
            && maxHp == other.maxHp
            && rawValue == other.rawValue
            && rawGoal == other.rawGoal
            && valueIsTime == other.valueIsTime
            && displayName == other.displayName;
    }
};

struct EnemyTargetPlanText {
    QString text;
    QPainterPath outlinePath;
    bool outlinePathPrepared = false;
    float advance = 0.0f;
    bool enabled = false;
};

struct EnemyTargetRenderPlan {
    bool valid = false;
    uint32_t configEpoch = 0;
    int textCacheGeneration = 0;
    int frameFontPx = 0;
    float hudScale = 0.0f;
    float stretchX = 0.0f;
    EnemyTargetPlanKey key;
    EnemyTargetLayout layout;
    EnemyTargetPlanText name;
    EnemyTargetPlanText number;
    EnemyTargetPlanText score;
    QColor nameColor;
};

static inline QRect OverlayRectFor(const QImage* topBuffer)
{
    const int overlayW = topBuffer ? topBuffer->width()  : 1;
    const int overlayH = topBuffer ? topBuffer->height() : 1;
    return QRect(0, 0, overlayW, overlayH);
}

static inline HudFrameTransform MakeHudFrameTransform(const QImage* topBuffer,
                                                      float topStretchX,
                                                      float hudScale,
                                                      float hudOriginXds,
                                                      float hudOriginYds)
{
    return {
        topStretchX,
        hudScale,
        (topStretchX - 1.0f) * 128.0f + hudOriginXds,
        hudOriginYds,
        OverlayRectFor(topBuffer),
    };
}

static void EnsureCachedConfigForFrame(Config::Table& cfg, const HudFrameTransform& xf)
{
    if (UNLIKELY(!s_cache.valid) || s_cache.lastHudScale != xf.hudScale) {
        RefreshCachedConfig(cfg, xf.topStretchX, xf.hudScale);
        s_cache.valid = true;
    } else if (s_cache.lastStretchX != xf.topStretchX) {
        RecomputeAnchorPositions(xf.topStretchX);
    }
}

static void ApplyHudPainterTransform(QPainter* p, const HudFrameTransform& xf)
{
    p->scale(xf.hudScale, xf.hudScale);
    if (xf.txDs != 0.0f || xf.tyDs != 0.0f)
        p->translate(xf.txDs, xf.tyDs);
}

static void EnsureHudFont(QPainter* p)
{
    // Screen.cpp gives each painter an overlayFont already sized to the resolved HUD font
    // pixel size (kCustomHudFontSize for MPH, Metroid.Visual.HudFontSize for system/file).
    // Adopt that size directly instead of forcing a fixed constant, so a custom font size
    // takes effect. A family/size change resets s_frameFpx to 0 in CustomHud_InvalidateConfigCache,
    // which lands here and re-caches.
    const int px = p->font().pixelSize();
    if (LIKELY(s_frameFpx == px && px > 0))
        return;

    // P-9 / OPT-HUD-6 / OPT-HUD-7: Cache font + metrics once for the current HUD font size.
    // Bump s_textCacheGen so content-keyed glyph bitmaps (not keyed by family) are re-rasterised.
    s_frameFont = p->font();
    s_frameFpx  = (px > 0) ? px : kCustomHudFontSize;
    if (px <= 0) {
        s_frameFont.setPixelSize(s_frameFpx);
        p->setFont(s_frameFont);
    }
    s_frameFm   = QFontMetrics(s_frameFont);
    ++s_textCacheGen;
}

// OPT-DR2: the dirty rect is the union of everything actually drawn this frame.
// s_drawnDirtyPx is accumulated by the low-level draw funnels as each primitive is
// drawn; s_chDirtyThisFrame carries the crosshair's prev∪curr bbox (it draws in
// pixel space with its own tracking). Clamped to the overlay so the caller's
// clear/upload stays in bounds.
static inline QRect CurrentHudDirtyRect(const HudFrameTransform& xf)
{
    return (s_drawnDirtyPx | s_chDirtyThisFrame) & xf.overlayRect;
}

static void ClearPreviousCrosshairDirty(const CrosshairHudConfig& ch)
{
    if (s_chPrevPxCx == INT_MIN) {
        NotifyCrosshairZoomNotDrawn();
        return;
    }
    s_chDirtyThisFrame = CrosshairBboxPx(s_chPrevPxCx, s_chPrevPxCy, ch,
                                         s_chPrevGeomScale, s_chPrevZoomT, s_chPrevScopeT);
    s_chPrevPxCx = INT_MIN;
    s_chPrevPxCy = INT_MIN;
    // The crosshair is not on screen, so the next appearance must snap to its
    // real position rather than ease out of a stale committed one.
    s_chStickyPxCx = INT_MIN;
    s_chStickyPxCy = INT_MIN;
    s_chCentreHold.Reset();
    s_chPrevGeomScale = 1.0;
    s_chPrevZoomT = 0.0;
    s_chPrevScopeT = 0.0;
    NotifyCrosshairZoomNotDrawn();
}
