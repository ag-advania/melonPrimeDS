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
