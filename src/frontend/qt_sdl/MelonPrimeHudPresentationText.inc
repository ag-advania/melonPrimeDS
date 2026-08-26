// This file is a unity-build fragment included by
// MelonPrimeHudRuntimeSample.inc after MatchStatusResolvedState is complete.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  Runtime-value presentation text
//
//  These bounded buffers turn already-resolved HUD values into display text.
//  They belong to presentation planning, not to NDS RAM sampling.  The
//  aggregate that stores them remains the same single frameState allocation.
// =========================================================================

static void FormatTime(char* buf, int bufSize, int seconds)
{
    int m = seconds / 60;
    int s = seconds % 60;
    std::snprintf(buf, bufSize, "%d:%02d", m, s);
}

static void FormatMinuteTime(char* buf, int bufSize, int minutes)
{
    std::snprintf(buf, bufSize, "%d:00", std::max(0, minutes));
}

static const char* ResolveMatchStatusLabel(uint8_t mode, const MatchStatusHudConfig& c)
{
    switch (mode) {
    case MODE_BATTLE:
    case MODE_NODES: return c.matchStatusLabelPoints;
    case MODE_BOUNTY:
    case MODE_CAPTURE: return c.matchStatusLabelOctoliths;
    case MODE_SURVIVAL: return c.matchStatusLabelLives;
    case MODE_DEFENDER: return c.matchStatusLabelRingTime;
    case MODE_PRIME_HUNTER: return c.matchStatusLabelPrimeTime;
    default: return "";
    }
}

struct MatchStatusStringCache { uint32_t configEpoch; uint8_t mode, xx; int currentValue, goalValue; bool hasGoal, isTimeMode, valid; char curBuf[24], sepBuf[4], goalBuf[24]; };
struct RankStringCache { uint32_t configEpoch; uint8_t rankByte; bool showOrdinal, valid; char buf[64]; };
struct TimeStringCache { uint32_t configEpoch; int value; bool valid; char buf[16]; };

static inline void UpdateMatchStatusStrings(MatchStatusStringCache& cache, const MatchStatusResolvedState& state)
{
    const bool hasGoal = state.isTimeMode || state.goalValue > 0;
    if (cache.valid && cache.configEpoch == s_cacheEpoch && cache.currentValue == state.currentValue && cache.goalValue == state.goalValue && cache.isTimeMode == state.isTimeMode && cache.mode == state.mode && cache.xx == state.xx && cache.hasGoal == hasGoal) return;
    cache.currentValue = state.currentValue; cache.goalValue = state.goalValue; cache.isTimeMode = state.isTimeMode; cache.mode = state.mode; cache.xx = state.xx; cache.hasGoal = hasGoal; cache.configEpoch = s_cacheEpoch;
    if (state.isTimeMode) { FormatTime(cache.curBuf, sizeof(cache.curBuf), state.currentValue); FormatTime(cache.goalBuf, sizeof(cache.goalBuf), state.goalValue); std::strncpy(cache.sepBuf, "/", sizeof(cache.sepBuf)); cache.sepBuf[sizeof(cache.sepBuf)-1]='\0'; }
    else if (state.goalValue > 0) { std::snprintf(cache.curBuf, sizeof(cache.curBuf), "%d", state.currentValue); std::snprintf(cache.goalBuf, sizeof(cache.goalBuf), "%d", state.goalValue); std::strncpy(cache.sepBuf, " / ", sizeof(cache.sepBuf)); cache.sepBuf[sizeof(cache.sepBuf)-1]='\0'; }
    else { std::snprintf(cache.curBuf, sizeof(cache.curBuf), "%d (XX=0x%02X)", state.currentValue, state.xx); cache.goalBuf[0]='\0'; cache.sepBuf[0]='\0'; }
    cache.valid = true;
}

static inline const char* UpdateRankString(RankStringCache& cache, uint8_t rankByte, bool showOrdinal, const RankTimeHudConfig& c)
{
    if (!cache.valid || cache.configEpoch != s_cacheEpoch || cache.rankByte != rankByte || cache.showOrdinal != showOrdinal) {
        static const char* kOrdinals[4] = { "st", "nd", "rd", "th" };
        if (showOrdinal) std::snprintf(cache.buf, sizeof(cache.buf), "%s%u%s%s", c.rankPrefix, rankByte + 1u, kOrdinals[rankByte], c.rankSuffix);
        else std::snprintf(cache.buf, sizeof(cache.buf), "%s%u%s", c.rankPrefix, rankByte + 1u, c.rankSuffix);
        cache.rankByte = rankByte; cache.showOrdinal = showOrdinal; cache.configEpoch = s_cacheEpoch; cache.valid = true;
    }
    return cache.buf;
}

static inline const char* UpdateTimeString(TimeStringCache& cache, int value, bool minutesOnly)
{
    if (!cache.valid || cache.configEpoch != s_cacheEpoch || cache.value != value) { if (minutesOnly) FormatMinuteTime(cache.buf, sizeof(cache.buf), value); else FormatTime(cache.buf, sizeof(cache.buf), value); cache.value = value; cache.configEpoch = s_cacheEpoch; cache.valid = true; }
    return cache.buf;
}
