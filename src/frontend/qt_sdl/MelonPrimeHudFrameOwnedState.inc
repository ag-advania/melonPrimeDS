// This file is a unity-build fragment included by
// MelonPrimeHudRuntimeSample.inc after all aggregate member types are
// complete.  Do not add it to CMakeLists.txt.

// =========================================================================
//  Per-instance frame-state aggregate
//
//  This is a lifetime/aggregation boundary only.  It deliberately keeps the
//  exact member order and cache layout used by the former inline owner. The
//  typed owner is constructed by CustomHudConfigState's cold constructor.
// =========================================================================

struct HudFrameOwnedState {
    HudRuntimeFrameCache frame;
    ScoreboardMatchCache scoreboardMatch;
    ScoreboardRenderPlan scoreboardPlan;
    struct ScoreboardRasterCache {
        QImage image;
        uint32_t planGeneration = 0;
        float hudScale = 0.0f;
        float originX = 0.0f;
        float originY = 0.0f;
        float widthDs = 0.0f;
        float heightDs = 0.0f;
        bool valid = false;
    } scoreboardRaster;
    uint32_t scoreboardPlanGeneration = 0;
    EnemyTargetRenderPlan enemyTargetPlan;
    struct ScoreboardFontCache {
        QFont boardFont;
        QFont rankFont;
        QFontMetrics boardFm{QFont{}};
        QFontMetrics rankFm{QFont{}};
        uint32_t configEpoch = 0;
        int baseFontGeneration = 0;
        int baseFontPx = 0;
        float hudScale = 0.0f;
        bool valid = false;
    } scoreboardFont;
    struct EnemyTargetFontCache {
        QFont font;
        QFontMetrics fm{QFont{}};
        uint32_t configEpoch = 0;
        int baseFontGeneration = 0;
        int baseFontPx = 0;
        float hudScale = 0.0f;
        bool valid = false;
    } enemyTargetFont;
    TextMeasureCache curText{}, sepText{}, goalText{};
    TextBitmapCache curBitmap{}, sepBitmap{}, goalBitmap{}, labelBitmap{};
    TextBitmapCache curOutline{}, sepOutline{}, goalOutline{}, labelOutline{};
    MatchStatusStringCache matchString{};
    TextBitmapCache bombBitmap{}, bombOutline{};
    TextMeasureCache bombMeasure{};
    RankStringCache rankString{};
    TextBitmapCache rankBitmap{}, rankOutline{};
    TextMeasureCache rankMeasure{};
    TimeStringCache timeLeftString{};
    TextBitmapCache timeLeftBitmap{}, timeLeftOutline{};
    TextMeasureCache timeLeftMeasure{};
    TimeStringCache timeLimitString{};
    TextBitmapCache timeLimitBitmap{}, timeLimitOutline{};
    TextMeasureCache timeLimitMeasure{};
};

static FORCE_INLINE HudFrameOwnedState& HudFrameState() noexcept
{
    return *ActiveHudConfigState().frameState;
}

#define s_hudRuntimeFrameCache (HudFrameState().frame)
#define s_scoreboardMatchCache (HudFrameState().scoreboardMatch)

// Dynamic snapshots retain QString storage across the normal game-frame
// rollover. Clear their payload only at a cold ownership boundary (ROM,
// match, config, savestate, or reset).
static void ClearHudRuntimeDynamicSnapshotPayload()
{
    auto& hudFrame = HudFrameState();
    hudFrame.frame.scoreboardValid = false;
    hudFrame.frame.scoreboardMatchSerial = 0;
    hudFrame.frame.scoreboard = {};
    hudFrame.scoreboardPlan.valid = false;
    hudFrame.scoreboardRaster.valid = false;
    hudFrame.scoreboardRaster.image = {};
    ++hudFrame.scoreboardPlanGeneration;
    hudFrame.frame.enemyTargetValid = false;
    hudFrame.frame.enemyTarget = {};
    hudFrame.enemyTargetPlan.valid = false;
}
