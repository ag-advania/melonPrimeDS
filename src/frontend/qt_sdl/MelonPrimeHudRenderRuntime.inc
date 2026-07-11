// This file is a unity-build fragment included by MelonPrimeHudRender.cpp.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  Battle HUD — mode-specific score/time display
// =========================================================================

// Battle modes (8-bit read from addrBattleMode)
enum BattleMode : uint8_t {
    MODE_BATTLE        = 2,
    MODE_BOUNTY        = 3,
    MODE_CAPTURE       = 4,
    MODE_DEFENDER      = 5,
    MODE_NODES         = 6,
    MODE_PRIME_HUNTER  = 7,
    MODE_SURVIVAL      = 8,
};

static inline uint8_t NormalizeGoalKey(uint8_t xx)
{
    // Goal value is encoded in bits[5:2]. Other bits are flags and must not
    // affect the score/lives/octolith/node lookup tables.
    return static_cast<uint8_t>(xx & 0x3C);
}

// XX → goal: direct map from document (no index calculation)
static int LookupBattleGoal(uint8_t xx)
{
    switch (xx) {
    case 0x00: return 1;   case 0x04: return 5;   case 0x08: return 7;
    case 0x0C: return 10;  case 0x10: return 15;  case 0x14: return 20;
    case 0x18: return 25;  case 0x1C: return 30;  case 0x20: return 40;
    case 0x24: return 50;  case 0x28: return 60;  case 0x2C: return 70;
    case 0x30: return 80;  case 0x34: return 90;  case 0x38: return 100;
    default: return 0;
    }
}

static int LookupSurvivalGoal(uint8_t xx)
{
    switch (xx) {
    case 0x00: return 0;  case 0x04: return 1;  case 0x08: return 2;
    case 0x0C: return 3;  case 0x10: return 4;  case 0x14: return 5;
    case 0x18: return 6;  case 0x1C: return 7;  case 0x20: return 8;
    case 0x24: return 9;  case 0x28: return 10;
    default: return 0;
    }
}

static int LookupOctoGoal(uint8_t xx)
{
    switch (xx) {
    case 0x00: return 1;  case 0x04: return 2;  case 0x08: return 3;
    case 0x0C: return 4;  case 0x10: return 5;  case 0x14: return 6;
    case 0x18: return 7;  case 0x1C: return 8;  case 0x20: return 9;
    case 0x24: return 10; case 0x28: return 15; case 0x2C: return 20;
    case 0x30: return 25;
    default: return 0;
    }
}

static int LookupNodeGoal(uint8_t xx)
{
    switch (xx) {
    case 0x00: return 40;  case 0x04: return 50;  case 0x08: return 60;
    case 0x0C: return 70;  case 0x10: return 80;  case 0x14: return 90;
    case 0x18: return 100; case 0x1C: return 120; case 0x20: return 140;
    case 0x24: return 160; case 0x28: return 180; case 0x2C: return 190;
    case 0x30: return 200; case 0x34: return 250;
    default: return 0;
    }
}

// Defender/Prime Hunter time goal XY→seconds (from battleSettings+4)
static int LookupTimeGoalSec(uint8_t xy)
{
    switch (xy) {
    case 0x00: return 60;  case 0x02: return 90;  case 0x04: return 120;
    case 0x06: return 150; case 0x08: return 180; case 0x0A: return 210;
    case 0x0C: return 240; case 0x0E: return 270; case 0x10: return 300;
    case 0x12: return 360; case 0x14: return 420; case 0x16: return 480;
    case 0x18: return 540; case 0x1A: return 600;
    default: return 0;
    }
}

// Match time limit: battleSettings+4 byte1 (XX) → minutes
// Bit0 = unused/flag. Bits1-4 = time index (0-14). Bit5+ = flags (WiFi=+0x20, etc. — ignored).
static int LookupTimeLimitMin(uint8_t XX)
{
    static const int kMinutes[] = { 3, 5, 7, 9, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60 };
    int idx = (XX >> 1) & 0xF;
    return idx < 15 ? kMinutes[idx] : 0;
}

static int LookupGoal(uint8_t mode, uint8_t xx)
{
    xx = NormalizeGoalKey(xx);

    switch (mode) {
    case MODE_BATTLE:       return LookupBattleGoal(xx);
    case MODE_SURVIVAL:     return LookupSurvivalGoal(xx);
    case MODE_BOUNTY:
    case MODE_CAPTURE:      return LookupOctoGoal(xx);
    case MODE_NODES:        return LookupNodeGoal(xx);
    default:                return 0;
    }
}

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

// =========================================================================
//  Battle match cache — read once at match join, reused every frame
// =========================================================================
struct BattleMatchState {
    uint8_t  mode;
    uint8_t  keyXX;
    uint8_t  teamNibble;  // battleSettings+4 bits[3:0] (Z): bit i = player(i+1) team (0=red, 1=green)
    bool     isTeamGame;  // battleSettings+4 bits[7:4] (Y): any bit set = team play ON
    int      goalValue;
    int      timeLimitMinutes; // match time limit in minutes (from battleSettings+4 XX field)
    bool     isTimeMode;
    bool     valid;
};
static BattleMatchState s_battleState = { .valid = false };

void CustomHud_OnMatchJoin(melonDS::u8* ram, const RomAddresses& rom)
{
    auto& b = s_battleState;
    b.mode = Read8(ram, rom.battleMode);
    b.isTimeMode = false;
    b.goalValue = 0;
    b.timeLimitMinutes = 0;

    if (b.mode < MODE_BATTLE || b.mode > MODE_SURVIVAL) {
        b.valid = false;
        return;
    }

    uint32_t settings = Read32(ram, rom.battleSettings);
    uint8_t xx = NormalizeGoalKey(static_cast<uint8_t>(settings >> 20));
    b.keyXX = xx;

    // Time limit in minutes: battleSettings+4 format 0000XXYZ, XX=time limit index
    // Z nibble (bits[3:0]) = team composition: bit i = player(i+1) team (0=red, 1=green)
    {
        uint32_t ts4 = Read32(ram, rom.battleSettings + 4);
        uint8_t XX = (ts4 >> 8) & 0xFF;
        b.timeLimitMinutes = LookupTimeLimitMin(XX);
        b.teamNibble  = ts4 & 0x0F;          // Z nibble: team assignment per player
        b.isTeamGame  = (ts4 & 0x10) != 0;  // Y nibble bit0 (bit4 of byte): 1=team play ON
    }

    switch (b.mode) {
    case MODE_BATTLE:
    case MODE_NODES:
    case MODE_BOUNTY:
    case MODE_CAPTURE:
        b.goalValue = LookupGoal(b.mode, xx);
        break;
    case MODE_SURVIVAL:
        b.goalValue = LookupGoal(b.mode, xx) + 1; // table = max deaths, lives = deaths + 1
        break;
    case MODE_DEFENDER:
    case MODE_PRIME_HUNTER: {
        uint32_t timeSetting = Read32(ram, rom.battleSettings + 4);
        // Time goal index: Y[3:1] (bits[7:5]) + XX[0] (bit8), skipping Y[0] (bit4 = team-play flag)
        // Shift out bit4 entirely: >> 5 gives [XX0,Y3,Y2,Y1], << 1 produces even values for the lookup table
        uint8_t timeGoalRaw = static_cast<uint8_t>(((timeSetting >> 5) & 0x0F) << 1);
        b.goalValue = LookupTimeGoalSec(timeGoalRaw);
        b.isTimeMode = true;
        break;
    }
    }

    b.valid = true;
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
struct MatchStatusResolvedState { uint8_t mode, xx; int currentValue, goalValue; bool isTimeMode; };
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
static bool ComputeMatchStatusState(melonDS::u8* ram, const RomAddresses& rom, uint8_t playerPos, MatchStatusResolvedState& outState)
{
    const BattleMatchState* match = s_battleState.valid ? &s_battleState : nullptr; uint8_t mode = match ? match->mode : Read8(ram, rom.battleMode); if (mode < MODE_BATTLE || mode > MODE_SURVIVAL) return false;
    uint32_t playerOfs = static_cast<uint32_t>(playerPos) * 4; int currentValue = 0; int goalValue = match ? match->goalValue : 0; bool isTimeMode = match ? match->isTimeMode : false; uint8_t xx = match ? match->keyXX : 0;
    uint32_t ts4Local = match ? 0u : Read32(ram, rom.battleSettings + 4); uint8_t teamNibble = match ? match->teamNibble : static_cast<uint8_t>(ts4Local & 0x0F); bool isTeamGame = match ? match->isTeamGame : ((ts4Local & 0x10) != 0);
    auto teamSum = [&](uint32_t baseAddr) -> int { if (isTeamGame && playerPos < 4) { uint8_t myTeam = (teamNibble >> playerPos) & 1; int sum = 0; for (int p = 0; p < 4; ++p) if (((teamNibble >> p) & 1) == myTeam) sum += static_cast<int>(Read32(ram, baseAddr + p * 4)); return sum; } return static_cast<int>(Read32(ram, baseAddr + playerOfs)); };
    switch (mode) { case MODE_BATTLE: case MODE_NODES: case MODE_BOUNTY: case MODE_CAPTURE: currentValue = teamSum(rom.basePoint); break; case MODE_SURVIVAL: currentValue = teamSum(rom.basePoint - 0xB0); break; case MODE_DEFENDER: case MODE_PRIME_HUNTER: currentValue = teamSum(rom.basePoint - 0x180) / 60; break; default: return false; }
    if (!match) { uint32_t settings = Read32(ram, rom.battleSettings); xx = NormalizeGoalKey(static_cast<uint8_t>(settings >> 20)); switch (mode) { case MODE_BATTLE: case MODE_NODES: case MODE_BOUNTY: case MODE_CAPTURE: goalValue = LookupGoal(mode, xx); break; case MODE_SURVIVAL: goalValue = LookupGoal(mode, xx); break; case MODE_DEFENDER: case MODE_PRIME_HUNTER: { uint32_t timeSetting = Read32(ram, rom.battleSettings + 4); uint8_t timeGoalRaw = static_cast<uint8_t>(((timeSetting >> 5) & 0x0F) << 1); goalValue = LookupTimeGoalSec(timeGoalRaw); isTimeMode = true; break; } default: return false; } }
    if (mode == MODE_SURVIVAL && goalValue > 0) { currentValue = goalValue - currentValue; if (currentValue < 0) currentValue = 0; }
    outState = { mode, xx, currentValue, goalValue, isTimeMode }; return true;
}
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

struct HudRuntimeState {
    melonDS::NDS* nds;
    melonDS::u8* ram;
    uint32_t offP;
    uint8_t romGroup;
    uint16_t currentHP;
    uint16_t maxHP;
    uint16_t maxAmmoSpecial;
    uint16_t maxAmmoMissile;
    uint8_t hunterID;
    uint8_t currentWeapon;
    bool isStartPressed;
    bool isGameOver;
    bool isFirstPerson;
    bool isAlt;
    bool isTrans;
    bool isAdventure;
    bool isMapOrUserActionPaused;
};

struct HudRuntimeFrameCache {
    melonDS::NDS* nds = nullptr;
    melonDS::u8* ram = nullptr;
    uint32_t gameFrame = 0xFFFFFFFFu;
    uint32_t offP = 0;
    uint8_t romGroup = 0xFFu;
    bool baseValid = false;
    bool adventureValid = false;
    bool visibleValid = false;
    bool currentAmmoSpecialValid = false;
    bool currentAmmoMissileValid = false;
    bool havingWeaponsValid = false;
    bool bombCountValid = false;
    bool matchStatusValid = false;
    bool rankByteValid = false;
    bool timeLeftSecondsValid = false;
    bool timeLimitMinutesValid = false;
    HudRuntimeState state = {};
    uint16_t currentAmmoSpecial = 0;
    uint16_t currentAmmoMissile = 0;
    uint16_t havingWeapons = 0;
    uint8_t bombCount = 0;
    MatchStatusResolvedState matchStatus = {};
    uint8_t rankByte = 0xFFu;
    int timeLeftSeconds = 0;
    int timeLimitMinutes = 0;
};

static HudRuntimeFrameCache s_hudRuntimeFrameCache;

static inline bool HudRuntimeFrameCacheMatches(melonDS::NDS* nds,
                                               melonDS::u8* ram,
                                               uint32_t gameFrame,
                                               uint32_t offP,
                                               uint8_t romGroup)
{
    return s_hudRuntimeFrameCache.nds == nds
        && s_hudRuntimeFrameCache.ram == ram
        && s_hudRuntimeFrameCache.gameFrame == gameFrame
        && s_hudRuntimeFrameCache.offP == offP
        && s_hudRuntimeFrameCache.romGroup == romGroup;
}

static inline bool HudRuntimeFrameCacheMatches(const HudRuntimeState& st)
{
    return st.nds
        && HudRuntimeFrameCacheMatches(st.nds, st.ram, st.nds->NumFrames,
                                       st.offP, st.romGroup);
}

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
    s_chPrevGeomScale = 1.0;
    s_chPrevZoomT = 0.0;
    s_chPrevScopeT = 0.0;
    NotifyCrosshairZoomNotDrawn();
}

static bool ReadHudRuntimeBaseState(EmuInstance* emu,
                                    const RomAddresses& rom,
                                    uint8_t playerPosition,
                                    HudRuntimeState& out)
{
    if (!emu) return false;

    melonDS::NDS* nds = emu->getNDS();
    melonDS::u8* ram = nds ? nds->MainRAM : nullptr;
    if (!ram) return false;

    const uint32_t offP = static_cast<uint32_t>(playerPosition) * Consts::PLAYER_ADDR_INC;
    const uint32_t gameFrame = nds->NumFrames;
    if (HudRuntimeFrameCacheMatches(nds, ram, gameFrame, offP, rom.romGroupIndex)
        && s_hudRuntimeFrameCache.baseValid) {
        out = s_hudRuntimeFrameCache.state;
        return true;
    }

    out.nds = nds;
    out.ram = ram;
    out.offP = offP;
    out.romGroup = rom.romGroupIndex;
    out.currentHP = Read16(ram, rom.playerHP + offP);
    out.isStartPressed = Read8(ram, rom.startPressed) == 0x01;
    out.isGameOver = Read8(ram, rom.gameOver) != 0x00;

    s_hudRuntimeFrameCache.nds = nds;
    s_hudRuntimeFrameCache.ram = ram;
    s_hudRuntimeFrameCache.gameFrame = gameFrame;
    s_hudRuntimeFrameCache.offP = offP;
    s_hudRuntimeFrameCache.romGroup = rom.romGroupIndex;
    s_hudRuntimeFrameCache.baseValid = true;
    s_hudRuntimeFrameCache.adventureValid = false;
    s_hudRuntimeFrameCache.visibleValid = false;
    s_hudRuntimeFrameCache.currentAmmoSpecialValid = false;
    s_hudRuntimeFrameCache.currentAmmoMissileValid = false;
    s_hudRuntimeFrameCache.havingWeaponsValid = false;
    s_hudRuntimeFrameCache.bombCountValid = false;
    s_hudRuntimeFrameCache.matchStatusValid = false;
    s_hudRuntimeFrameCache.rankByteValid = false;
    s_hudRuntimeFrameCache.timeLeftSecondsValid = false;
    s_hudRuntimeFrameCache.timeLimitMinutesValid = false;
    s_hudRuntimeFrameCache.state = out;
    return true;
}

static void ReadHudRuntimeAdventurePauseState(const RomAddresses& rom,
                                             HudRuntimeState& out)
{
    if (HudRuntimeFrameCacheMatches(out) && s_hudRuntimeFrameCache.adventureValid) {
        out.isAdventure = s_hudRuntimeFrameCache.state.isAdventure;
        out.isMapOrUserActionPaused =
            s_hudRuntimeFrameCache.state.isMapOrUserActionPaused;
        return;
    }

    out.isAdventure = Read8(out.ram, rom.isInAdventure) == 0x02;
    out.isMapOrUserActionPaused = out.isAdventure &&
        Read8(out.ram, rom.isMapOrUserActionPaused) == 0x01;

    if (HudRuntimeFrameCacheMatches(out)) {
        s_hudRuntimeFrameCache.state.isAdventure = out.isAdventure;
        s_hudRuntimeFrameCache.state.isMapOrUserActionPaused =
            out.isMapOrUserActionPaused;
        s_hudRuntimeFrameCache.adventureValid = true;
    }
}

static void ReadHudRuntimeVisibleState(const RomAddresses& rom,
                                       const GameAddressesHot& addrHot,
                                       HudRuntimeState& out)
{
    if (HudRuntimeFrameCacheMatches(out) && s_hudRuntimeFrameCache.visibleValid) {
        out.maxHP = s_hudRuntimeFrameCache.state.maxHP;
        out.maxAmmoSpecial = s_hudRuntimeFrameCache.state.maxAmmoSpecial;
        out.maxAmmoMissile = s_hudRuntimeFrameCache.state.maxAmmoMissile;
        out.hunterID = s_hudRuntimeFrameCache.state.hunterID;
        out.currentWeapon = s_hudRuntimeFrameCache.state.currentWeapon;
        out.isFirstPerson = s_hudRuntimeFrameCache.state.isFirstPerson;
        out.isAlt = s_hudRuntimeFrameCache.state.isAlt;
        out.isTrans = s_hudRuntimeFrameCache.state.isTrans;
        return;
    }

    melonDS::u8* ram = out.ram;
    const uint32_t offP = out.offP;
    const uint8_t viewMode = Read8(ram, rom.baseViewMode + offP);
    const bool isFirstPerson = (viewMode == 0x00);

    out.maxHP = Read16(ram, rom.maxHP + offP);
    out.maxAmmoSpecial = isFirstPerson ? Read16(ram, rom.maxAmmoSpecial + offP) : 0;
    out.maxAmmoMissile = isFirstPerson ? Read16(ram, rom.maxAmmoMissile + offP) : 0;
    out.hunterID = Read8(ram, addrHot.chosenHunter);
    out.currentWeapon = isFirstPerson ? Read8(ram, addrHot.currentWeapon) : 0;
    out.isFirstPerson = isFirstPerson;
    out.isAlt = Read8(ram, addrHot.isAltForm) == 0x02;
    out.isTrans = isFirstPerson && (Read8(ram, addrHot.jumpFlag) & 0x10) != 0;

    if (HudRuntimeFrameCacheMatches(out)) {
        s_hudRuntimeFrameCache.state.maxHP = out.maxHP;
        s_hudRuntimeFrameCache.state.maxAmmoSpecial = out.maxAmmoSpecial;
        s_hudRuntimeFrameCache.state.maxAmmoMissile = out.maxAmmoMissile;
        s_hudRuntimeFrameCache.state.hunterID = out.hunterID;
        s_hudRuntimeFrameCache.state.currentWeapon = out.currentWeapon;
        s_hudRuntimeFrameCache.state.isFirstPerson = out.isFirstPerson;
        s_hudRuntimeFrameCache.state.isAlt = out.isAlt;
        s_hudRuntimeFrameCache.state.isTrans = out.isTrans;
        s_hudRuntimeFrameCache.visibleValid = true;
    }
}

static uint16_t ReadHudRuntimeCurrentAmmoSpecial(const RomAddresses& rom,
                                                 const HudRuntimeState& st)
{
    if (HudRuntimeFrameCacheMatches(st)
        && s_hudRuntimeFrameCache.currentAmmoSpecialValid)
        return s_hudRuntimeFrameCache.currentAmmoSpecial;

    const uint16_t value = Read16(st.ram, rom.currentAmmoSpecial + st.offP);
    if (HudRuntimeFrameCacheMatches(st)) {
        s_hudRuntimeFrameCache.currentAmmoSpecial = value;
        s_hudRuntimeFrameCache.currentAmmoSpecialValid = true;
    }
    return value;
}

static uint16_t ReadHudRuntimeCurrentAmmoMissile(const RomAddresses& rom,
                                                 const HudRuntimeState& st)
{
    if (HudRuntimeFrameCacheMatches(st)
        && s_hudRuntimeFrameCache.currentAmmoMissileValid)
        return s_hudRuntimeFrameCache.currentAmmoMissile;

    const uint16_t value = Read16(st.ram, rom.currentAmmoMissile + st.offP);
    if (HudRuntimeFrameCacheMatches(st)) {
        s_hudRuntimeFrameCache.currentAmmoMissile = value;
        s_hudRuntimeFrameCache.currentAmmoMissileValid = true;
    }
    return value;
}

static uint16_t ReadHudRuntimeHavingWeapons(const GameAddressesHot& addrHot,
                                            const HudRuntimeState& st)
{
    if (HudRuntimeFrameCacheMatches(st)
        && s_hudRuntimeFrameCache.havingWeaponsValid)
        return s_hudRuntimeFrameCache.havingWeapons;

    const uint16_t value = static_cast<uint16_t>(Read16(st.ram, addrHot.havingWeapons));
    if (HudRuntimeFrameCacheMatches(st)) {
        s_hudRuntimeFrameCache.havingWeapons = value;
        s_hudRuntimeFrameCache.havingWeaponsValid = true;
    }
    return value;
}

static uint8_t ReadHudRuntimeBombCount(const RomAddresses& rom,
                                       const HudRuntimeState& st)
{
    if (HudRuntimeFrameCacheMatches(st)
        && s_hudRuntimeFrameCache.bombCountValid)
        return s_hudRuntimeFrameCache.bombCount;

    const uint8_t value =
        static_cast<uint8_t>((Read32(st.ram, rom.baseBomb + st.offP) >> 8) & 0xF);
    if (HudRuntimeFrameCacheMatches(st)) {
        s_hudRuntimeFrameCache.bombCount = value;
        s_hudRuntimeFrameCache.bombCountValid = true;
    }
    return value;
}

static bool ReadHudRuntimeMatchStatusState(const RomAddresses& rom,
                                           uint8_t playerPos,
                                           const HudRuntimeState& st,
                                           MatchStatusResolvedState& out)
{
    if (HudRuntimeFrameCacheMatches(st)
        && s_hudRuntimeFrameCache.matchStatusValid) {
        out = s_hudRuntimeFrameCache.matchStatus;
        return true;
    }

    if (!ComputeMatchStatusState(st.ram, rom, playerPos, out))
        return false;

    if (HudRuntimeFrameCacheMatches(st)) {
        s_hudRuntimeFrameCache.matchStatus = out;
        s_hudRuntimeFrameCache.matchStatusValid = true;
    }
    return true;
}

static uint8_t ReadHudRuntimeRankByte(const RomAddresses& rom,
                                      uint8_t playerPos,
                                      const HudRuntimeState& st)
{
    if (HudRuntimeFrameCacheMatches(st)
        && s_hudRuntimeFrameCache.rankByteValid)
        return s_hudRuntimeFrameCache.rankByte;

    const uint32_t rankWord = Read32(st.ram, rom.matchRank);
    const uint8_t value = static_cast<uint8_t>((rankWord >> (playerPos * 8)) & 0xFF);
    if (HudRuntimeFrameCacheMatches(st)) {
        s_hudRuntimeFrameCache.rankByte = value;
        s_hudRuntimeFrameCache.rankByteValid = true;
    }
    return value;
}

static int ReadHudRuntimeTimeLeftSeconds(const RomAddresses& rom,
                                         const HudRuntimeState& st)
{
    if (HudRuntimeFrameCacheMatches(st)
        && s_hudRuntimeFrameCache.timeLeftSecondsValid)
        return s_hudRuntimeFrameCache.timeLeftSeconds;

    const int value = static_cast<int>(Read32(st.ram, rom.timeLeft)) / 60;
    if (HudRuntimeFrameCacheMatches(st)) {
        s_hudRuntimeFrameCache.timeLeftSeconds = value;
        s_hudRuntimeFrameCache.timeLeftSecondsValid = true;
    }
    return value;
}

static int ReadHudRuntimeTimeLimitMinutes(const RomAddresses& rom,
                                          const HudRuntimeState& st)
{
    if (HudRuntimeFrameCacheMatches(st)
        && s_hudRuntimeFrameCache.timeLimitMinutesValid)
        return s_hudRuntimeFrameCache.timeLimitMinutes;

    const int value = s_battleState.valid
        ? s_battleState.timeLimitMinutes
        : LookupTimeLimitMin((Read32(st.ram, rom.battleSettings + 4) >> 8) & 0xFF);
    if (HudRuntimeFrameCacheMatches(st)) {
        s_hudRuntimeFrameCache.timeLimitMinutes = value;
        s_hudRuntimeFrameCache.timeLimitMinutesValid = true;
    }
    return value;
}

static void DrawMatchStatusText(QPainter* p, const QFontMetrics& fm, int fontPixelSize, float tds,
                                const MatchStatusResolvedState& state, const MatchStatusHudConfig& c,
                                float overallOpacity = 1.0f, const HudOutlineConfig& ol = {false,QColor(),0.0f,1},
                                float hudScale = 1.0f)
{
    static TextMeasureCache s_curTextCache = { 0, "", 0, 0, false }, s_sepTextCache = { 0, "", 0, 0, false }, s_goalTextCache = { 0, "", 0, 0, false };
    static TextBitmapCache s_curBitmapCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() }, s_sepBitmapCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() }, s_goalBitmapCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() }, s_labelBitmapCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
    static TextBitmapCache s_curOlCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() }, s_sepOlCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() }, s_goalOlCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() }, s_labelOlCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
    static MatchStatusStringCache s_matchStringCache = { 0, 0, 0, 0, 0, false, false, false, "", "", "" };
    UpdateMatchStatusStrings(s_matchStringCache, state);
    // OPT-HUD-6: use s_frameFont (cached by EnsureHudFont) — no p->font() copy per-frame.
    auto eff = [&](const QColor& sub) -> const QColor& { return sub.isValid() ? sub : c.matchStatusColor; };
    const bool useOutline = ol.enable && ol.opacity > 0.0f;
    const float renderScale = tds * hudScale;
    auto drawText = [&](TextBitmapCache& bmp, TextBitmapCache& olBmp, float x, float y) {
        if (useOutline) {
            PrepareOutlineBitmapCached(olBmp, bmp, ol.color, ol.thickness, renderScale);
            DrawCachedTextOutlined(p, bmp, olBmp, x, y, tds, overallOpacity, ol.opacity);
        } else {
            if (overallOpacity < 1.0f) p->setOpacity(overallOpacity);
            DrawCachedText(p, bmp, x, y, tds);
            p->setOpacity(1.0f);
        }
    };
    float curW = 0.0f, curH = 0.0f;
    MeasureTextCached(fm, fontPixelSize, s_curTextCache, s_matchStringCache.curBuf, curW, curH, tds);
    PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_curBitmapCache, s_matchStringCache.curBuf, eff(c.matchStatusValueColor));
    drawText(s_curBitmapCache, s_curOlCache, static_cast<float>(c.matchStatusX), static_cast<float>(c.matchStatusY));
    if (s_matchStringCache.hasGoal) {
        float sepW = 0.0f, sepH = 0.0f, goalW = 0.0f, goalH = 0.0f;
        MeasureTextCached(fm, fontPixelSize, s_sepTextCache, s_matchStringCache.sepBuf, sepW, sepH, tds);
        MeasureTextCached(fm, fontPixelSize, s_goalTextCache, s_matchStringCache.goalBuf, goalW, goalH, tds);
        float x = c.matchStatusX + curW;
        PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_sepBitmapCache, s_matchStringCache.sepBuf, eff(c.matchStatusSepColor));
        drawText(s_sepBitmapCache, s_sepOlCache, x, static_cast<float>(c.matchStatusY));
        x += sepW;
        PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_goalBitmapCache, s_matchStringCache.goalBuf, eff(c.matchStatusGoalColor));
        drawText(s_goalBitmapCache, s_goalOlCache, x, static_cast<float>(c.matchStatusY));
    }
    const char* label = ResolveMatchStatusLabel(state.mode, c);
    if (label[0] == '\0') return;
    float lx = static_cast<float>(c.matchStatusX), ly = static_cast<float>(c.matchStatusY);
    switch (c.matchStatusLabelPos) { default: case 0: ly -= 10; break; case 1: ly += 10; break; case 2: lx -= 50; break; case 3: lx += 50; break; case 4: break; }
    lx += c.matchStatusLabelOfsX; ly += c.matchStatusLabelOfsY;
    PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_labelBitmapCache, label, eff(c.matchStatusLabelColor));
    drawText(s_labelBitmapCache, s_labelOlCache, lx, ly);
}
static void DrawMatchStatusHud(QPainter* p, melonDS::u8* ram, const RomAddresses& rom,
                               uint8_t playerPos, bool isAdventure,
                               const CachedHudConfig& c,
                               const HudRuntimeState* st = nullptr)
{
    if (!c.matchStatus.matchStatusShow || isAdventure)
        return;

    MatchStatusResolvedState state = {};
    const bool hasState = st
        ? ReadHudRuntimeMatchStatusState(rom, playerPos, *st, state)
        : ComputeMatchStatusState(ram, rom, playerPos, state);
    if (!hasState)
        return;

    DrawMatchStatusText(p, s_frameFm, s_frameFpx, c.textDrawScale,
                        state, c.matchStatus, c.matchStatusOpacity,
                        EffOL(c, c.matchStatus.outline), c.lastHudScale);
}
static float CalcAlignedTextX(int anchorX, int align, float textW);
static void DrawCachedAlignedText(QPainter* p, const QFontMetrics& fm, int fontPixelSize, float tds,
                                   TextMeasureCache& measureCache, TextBitmapCache& bitmapCache,
                                   TextBitmapCache& outlineCache,
                                   const char* text, const QColor& color,
                                   int anchorX, int align, int y,
                                   float opacity, const HudOutlineConfig& ol, float hudScale = 1.0f)
{
    float textW = 0.0f, textH = 0.0f;
    MeasureTextCached(fm, fontPixelSize, measureCache, text, textW, textH, tds);
    const float textX = CalcAlignedTextX(anchorX, align, textW);
    // OPT-HUD-6: use s_frameFont (cached by EnsureHudFont) — no p->font() copy per-frame.
    PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, bitmapCache, text, color);
    if (ol.enable && ol.opacity > 0.0f) {
        const float renderScale = tds * hudScale;
        PrepareOutlineBitmapCached(outlineCache, bitmapCache, ol.color, ol.thickness, renderScale);
        DrawCachedTextOutlined(p, bitmapCache, outlineCache, textX, y, tds, opacity, ol.opacity);
    } else {
        if (opacity < 1.0f) p->setOpacity(opacity);
        DrawCachedText(p, bitmapCache, textX, y, tds);
        p->setOpacity(1.0f);
    }
}
// =========================================================================
//  Bomb Left HUD
// =========================================================================
static void DrawBombLeft(QPainter* p, uint8_t bombs,
                         const CachedHudConfig& c, float tds, float hudScale)
{
    if (!c.bombLeft.bombLeftShow) return;
    {
        static TextBitmapCache s_bombBitmapCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
        static TextBitmapCache s_bombOutlineCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
        static TextMeasureCache s_bombMeasureCache = { 0, "", 0, 0, false };
        const QFontMetrics& fm = s_frameFm; const int fontPixelSize = s_frameFpx; // P-9
        // OPT-HUD-6: use s_frameFont (cached by EnsureHudFont) — no p->font() copy per-frame.
        char buf[64];
        if (c.bombLeft.bombLeftTextShow) std::snprintf(buf, sizeof(buf), "%s%u%s", c.bombLeft.bombLeftPrefix, bombs, c.bombLeft.bombLeftSuffix);
        else std::snprintf(buf, sizeof(buf), "%s%s", c.bombLeft.bombLeftPrefix, c.bombLeft.bombLeftSuffix);
        if (buf[0] != '\0') {
            float bombTextW = 0.0f, bombTextH = 0.0f;
            MeasureTextCached(fm, fontPixelSize, s_bombMeasureCache, buf, bombTextW, bombTextH, tds);
            PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_bombBitmapCache, buf, c.bombLeft.bombLeftColor);
            const float bombTextX = CalcAlignedTextX(c.bombLeft.bombLeftX, c.bombLeft.bombLeftAlign, bombTextW);
            { const HudOutlineConfig& _ol = EffOL(c, c.bombLeft.outline);
            if (_ol.enable && _ol.opacity > 0.0f) {
                const float renderScale = tds * hudScale;
                PrepareOutlineBitmapCached(s_bombOutlineCache, s_bombBitmapCache, _ol.color, _ol.thickness, renderScale);
                DrawCachedTextOutlined(p, s_bombBitmapCache, s_bombOutlineCache, bombTextX, c.bombLeft.bombLeftY, tds,
                                       c.bombLeftOpacity, _ol.opacity);
            } else {
                if (c.bombLeftOpacity < 1.0f) p->setOpacity(c.bombLeftOpacity);
                DrawCachedText(p, s_bombBitmapCache, bombTextX, c.bombLeft.bombLeftY, tds);
                p->setOpacity(1.0f);
            } }
        }
    }
    if (c.bombLeft.bombIconShow) {
        EnsureBombIconsLoaded(static_cast<int>(c.bombLeft.bombIconHeight * c.scaleIcons));
        const QImage& icon = GetBombIconForDraw(bombs, c.bombLeft.bombIconColorOverlay, c.bombLeft.bombIconColor);
        if (!icon.isNull()) {
            const float dw = icon.width() / hudScale; const float dh = icon.height() / hudScale;
            float ix = (c.bombLeft.bombIconMode == 0) ? c.bombLeft.bombLeftX + c.bombLeft.bombIconOfsX : c.bombLeft.bombIconPosX;
            float iy = (c.bombLeft.bombIconMode == 0) ? c.bombLeft.bombLeftY + c.bombLeft.bombIconOfsY : c.bombLeft.bombIconPosY;
            const int iconAlignX = c.bombLeft.bombIconAnchorX; const int iconAlignY = c.bombLeft.bombIconAnchorY;
            if (iconAlignX == 1) ix -= dw * 0.5f; else if (iconAlignX == 2) ix -= dw;
            if (iconAlignY == 1) iy -= dh * 0.5f; else if (iconAlignY == 2) iy -= dh;
            { const HudOutlineConfig& _ol = EffOL(c, c.bombLeft.iconOutline);
            if (_ol.enable && _ol.opacity > 0.0f) {
                const int expandR = std::max(1, _ol.thickness);
                EnsureBombOutlineIconsUpdated(_ol.color, expandR);
                const int bombIdx = (bombs >= 0 && bombs <= 3) ? bombs : 0;
                DrawImageOutlined(p, icon, s_bombIconsOutline[bombIdx],
                                  QRectF(ix, iy, dw, dh), (float)expandR / hudScale,
                                  c.bombIconOpacity, _ol.opacity);
            } else {
                const QRectF bombIconRect(ix, iy, dw, dh);
                if (c.bombIconOpacity < 1.0f) p->setOpacity(c.bombIconOpacity);
                p->drawImage(bombIconRect, icon);
                AccumDirtyDs(p, bombIconRect); // OPT-DR2
                p->setOpacity(1.0f);
            } }
        }
    }
}
//  Rank & Time HUD
// =========================================================================
static void DrawRankAndTime(QPainter* p, melonDS::u8* ram, const RomAddresses& rom,
                            uint8_t playerPos, bool isAdventure,
                            const CachedHudConfig& c, float tds,
                            const HudRuntimeState* st = nullptr)
{
    if (isAdventure)
        return;

    const auto& hud = c.rankTime;

    if (hud.rankShow) {
        static RankStringCache s_rankStringCache = { 0, 0, false, false, "" };
        static TextBitmapCache s_rankCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
        static TextBitmapCache s_rankOutlineCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
        static TextMeasureCache s_rankMeasure = { 0, "", 0, 0, false };

        const uint8_t rankByte = st
            ? ReadHudRuntimeRankByte(rom, playerPos, *st)
            : static_cast<uint8_t>((Read32(ram, rom.matchRank) >> (playerPos * 8)) & 0xFF);
        if (rankByte <= 3) {
            DrawCachedAlignedText(p, s_frameFm, s_frameFpx, tds,
                                  s_rankMeasure, s_rankCache, s_rankOutlineCache,
                                  UpdateRankString(s_rankStringCache, rankByte, hud.rankShowOrdinal, hud),
                                  hud.rankColor, hud.rankX, hud.rankAlign, hud.rankY,
                                  c.rankOpacity, EffOL(c, c.rankTime.rankOutline), c.lastHudScale);
        }
    }

    if (hud.timeLeftShow) {
        static TimeStringCache s_timeLeftStringCache = { 0, 0, false, "" };
        static TextBitmapCache s_timeLeftCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
        static TextBitmapCache s_timeLeftOutlineCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
        static TextMeasureCache s_timeLeftMeasure = { 0, "", 0, 0, false };

        const int seconds = st
            ? ReadHudRuntimeTimeLeftSeconds(rom, *st)
            : static_cast<int>(Read32(ram, rom.timeLeft)) / 60;
        DrawCachedAlignedText(p, s_frameFm, s_frameFpx, tds,
                              s_timeLeftMeasure, s_timeLeftCache, s_timeLeftOutlineCache,
                              UpdateTimeString(s_timeLeftStringCache, seconds, false),
                              hud.timeLeftColor, hud.timeLeftX, hud.timeLeftAlign, hud.timeLeftY,
                              c.timeLeftOpacity, EffOL(c, c.rankTime.timeLeftOutline), c.lastHudScale);
    }

    if (hud.timeLimitShow) {
        static TimeStringCache s_timeLimitStringCache = { 0, 0, false, "" };
        static TextBitmapCache s_timeLimitCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
        static TextBitmapCache s_timeLimitOutlineCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
        static TextMeasureCache s_timeLimitMeasure = { 0, "", 0, 0, false };

        const int goalMinutes = st
            ? ReadHudRuntimeTimeLimitMinutes(rom, *st)
            : (s_battleState.valid
                ? s_battleState.timeLimitMinutes
                : LookupTimeLimitMin((Read32(ram, rom.battleSettings + 4) >> 8) & 0xFF));
        DrawCachedAlignedText(p, s_frameFm, s_frameFpx, tds,
                              s_timeLimitMeasure, s_timeLimitCache, s_timeLimitOutlineCache,
                              UpdateTimeString(s_timeLimitStringCache, goalMinutes, true),
                              hud.timeLimitColor, hud.timeLimitX, hud.timeLimitAlign, hud.timeLimitY,
                              c.timeLimitOpacity, EffOL(c, c.rankTime.timeLimitOutline), c.lastHudScale);
    }
}
// =========================================================================
//  Config key (only for IsEnabled — hot path uses s_cache)
// =========================================================================
static constexpr const char* kCfgCustomHud = MP_HUD_PROP_KEY_CustomHUD;
bool CustomHud_IsEnabled(Config::Table& localCfg)
{
    return localCfg.GetBool(kCfgCustomHud);
}

static inline bool ShouldHideForGameplayState(bool isStartPressed,
                                             uint16_t currentHP,
                                             bool isGameOver,
                                             bool isAdventure,
                                             bool isMapOrUserActionPaused)
{
    return isStartPressed ||
           (isAdventure && isMapOrUserActionPaused) ||
           currentHP == 0 ||
           isGameOver;
}

bool CustomHud_ShouldHideForGameplayState(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition)
{
    if (!emu) return true;

    melonDS::NDS* nds = emu->getNDS();
    melonDS::u8* ram = nds ? nds->MainRAM : nullptr;
    if (!ram) return true;

    const uint32_t offP = static_cast<uint32_t>(playerPosition) * Consts::PLAYER_ADDR_INC;
    const bool isStartPressed = Read8(ram, rom.startPressed) == 0x01;
    const uint16_t currentHP = Read16(ram, rom.playerHP + offP);
    const bool isGameOver = Read8(ram, rom.gameOver) != 0x00;
    if (isStartPressed || currentHP == 0 || isGameOver)
        return true;

    const bool isAdventure = Read8(ram, rom.isInAdventure) == 0x02;
    if (!isAdventure)
        return false;

    return Read8(ram, rom.isMapOrUserActionPaused) == 0x01;
}

bool CustomHud_ShouldDrawRadarOverlay(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition)
{
    return !CustomHud_ShouldHideForGameplayState(emu, rom, playerPosition);
}
// One-shot guard: when CustomHUD transitions to disabled, perform a full
// ForceRestore on the next in-game frame regardless of s_hudPatchApplied,
// so any out-of-sync RAM state (savestate / persistent RAM / JIT corner
// cases) is normalized exactly once. Reset to true when CustomHUD becomes
// enabled or on emu start/stop, so a future disable triggers it again.
static bool s_hudPatchForceRestorePending = true;

static void CustomHud_MarkNoHudPatchDesired()
{
    s_hudPatchForceRestorePending = true;
}

static void CustomHud_RestoreNativeHudState(const RomAddresses& /*rom*/,
                                            const HudRuntimeState& st)
{
    const bool needForceRestore = s_hudPatchForceRestorePending;
    if (NoHudPatch_GetAppliedMask() == 0 && !needForceRestore)
        return;

    if (needForceRestore) {
        NoHudPatch_RestoreAll(st.nds, st.romGroup);
        s_hudPatchForceRestorePending = false;
    } else {
        NoHudPatch_Sync(st.nds, st.romGroup, 0);
    }
}

void CustomHud_ClampHelmetLayersPreFrame(
    EmuInstance* emu,
    const RomAddresses& rom,
    uint8_t playerPosition)
{
    // Gate on the tracker, not the config: the helmet bit is only ever applied
    // by Render() while CustomHUD is enabled, and this avoids a per-frame toml
    // lookup on the hot path.
    if (!(NoHudPatch_GetAppliedMask() & (1u << NOHUD_HELMET)))
        return;

    HudRuntimeState st = {};
    if (!ReadHudRuntimeBaseState(emu, rom, playerPosition, st))
        return;

    // Native pause/score/death/game-over UI needs the normal layer state.
    if (st.isStartPressed || st.isGameOver || st.currentHP == 0)
        return;
    ReadHudRuntimeAdventurePauseState(rom, st);
    if (st.isAdventure && st.isMapOrUserActionPaused)
        return;

    NoHudPatch_ClampHelmetLayers(st.nds, st.romGroup);
}

void CustomHud_EnsurePatchRestored(
    EmuInstance* emu,
    Config::Table& localCfg,
    const RomAddresses& rom,
    uint8_t playerPosition,
    bool isInGame)
{
    if (isInGame && CustomHud_IsEnabled(localCfg)) {
        // HUD is enabled — Render() handles patches. Re-arm the force-restore
        // for the next disable event.
        CustomHud_MarkNoHudPatchDesired();
        return;
    }

    HudRuntimeState st = {};
    if (!ReadHudRuntimeBaseState(emu, rom, playerPosition, st))
        return;

    CustomHud_RestoreNativeHudState(rom, st);
}

void CustomHud_ResetPatchState(CustomHudConfigState& hudConfig)
{
    const ScopedHudConfigState active(hudConfig);
    NoHudPatch_ResetState();
    s_cache.valid = false;
    s_battleState.valid = false;
    s_chPrevPxCx = INT_MIN;
    s_chPrevPxCy = INT_MIN;
    ResetCrosshairZoomDisplay(0.0f);
    s_hudPatchForceRestorePending = true;
}

// P-3: Called from settings dialog save to trigger config re-read next frame
static void CustomHud_InvalidateConfigCache()
{
    s_cache.valid = false;
    s_bombTintCacheValid = false;
    for (int i = 0; i < 9; i++) s_weaponTintValid[i] = false;
    s_effectiveRadarHunterID = 0xFF; // P-13: force re-evaluation of effective radar color
    // Force EnsureHudFont() to re-read the painter font next frame; Screen.cpp rebuilds
    // overlayFont from CustomHud_ResolveBaseFont when the HUD-font setting changes, and the
    // rebuild bumps s_textCacheGen so stale glyph bitmaps are re-rasterised.
    s_frameFpx = 0;
    // Bump epoch immediately so Screen.cpp's m_hudEnabled cache refreshes on the
    // next frame. Without this, when CustomHUD is toggled off, the stale cache
    // keeps Render() running for one extra frame and re-applies NoHudPatch.
    if (++s_cacheEpoch == 0) s_cacheEpoch = 1;
}

void CustomHud_InvalidateConfigCache(CustomHudConfigState& hudConfig)
{
    const ScopedHudConfigState active(hudConfig);
    CustomHud_InvalidateConfigCache();
}

uint32_t CustomHud_GetCacheEpoch(const CustomHudConfigState& hudConfig)
{
    return hudConfig.cacheEpoch;
}

