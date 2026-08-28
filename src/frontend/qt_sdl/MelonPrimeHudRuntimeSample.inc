// This file is a unity-build fragment included by MelonPrimeHudRender.cpp.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  Custom HUD runtime sampling
//
//  NDS RAM -> bounded snapshot.  Game-mode and goal semantics, the match
//  cache taken at join, scoreboard/enemy-target sampling, and the per-frame
//  runtime reads.  It changes when the game's own meaning changes: score
//  rules, ROM addresses, mode encodings.
//
//  Qt layout, text presentation, and per-instance aggregate ownership are
//  supplied by the adjacent unity fragments at the type-dependency points
//  below.  This fragment remains the RAM-facing sampling and game-semantics
//  owner.
// =========================================================================

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
    uint32_t scoreboardMatchSerial = 0;
};

#include "MelonPrimeHudBattleOwnedState.inc"

static void BumpCustomHudVisualGeneration()
{
    auto& generation = ActiveHudConfigState().visualGeneration;
    if (++generation == 0)
        generation = 1;
}

void CustomHud_OnMatchJoin(CustomHudConfigState& hudConfig, melonDS::u8* ram, const RomAddresses& rom)
{
    const ScopedHudConfigState active(hudConfig);
    BumpCustomHudVisualGeneration();
    auto& b = s_battleState;
    ++b.scoreboardMatchSerial;
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
struct MatchStatusResolvedState { uint8_t mode, xx; int currentValue, goalValue; bool isTimeMode; };

#include "MelonPrimeHudPresentationText.inc"

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
    bool isAdventureCameraScene;
    bool isAdventureScanVisor;
};

static inline bool HudRuntimeFrameCacheMatches(const HudRuntimeState& st);

// Match-scoreboard mode encoding is a separate protocol from BattleMode above.
// These values are the match runtime's dedicated 3..14 mode field; do not
// reuse the legacy BattleMode enum, whose values have different meanings.
enum class ScoreboardGameMode : uint8_t {
    Battle = 3,
    BattleTeams = 4,
    Survival = 5,
    SurvivalTeams = 6,
    Capture = 7,
    Bounty = 8,
    BountyTeams = 9,
    Nodes = 10,
    NodesTeams = 11,
    Defender = 12,
    DefenderTeams = 13,
    PrimeHunter = 14,
};

struct ScoreboardPlayerSnapshot {
    uint8_t slot = 0xFF;
    uint8_t teamIndex = 0xFF;
    uint8_t hunterId = 0xFF;
    uint8_t stars = 0xFF;
    uint8_t standing = 0xFF;
    uint8_t teamStanding = 0xFF;
    bool active = false;
    int32_t time = 0;
    int32_t kills = 0;
    int32_t deaths = 0;
    int32_t points = 0;
    QString displayName;
};

struct ScoreboardTeamSnapshot {
    int32_t time = 0;
    int32_t kills = 0;
    int32_t deaths = 0;
    int32_t points = 0;
};

struct ScoreboardSnapshot {
    uint32_t matchSerial = 0;
    uint8_t gameMode = 0;
    uint8_t activePlayers = 0;
    uint16_t teamFlags = 0;
    bool teams = false;
    bool visible = false;
    std::array<uint8_t, 4> resultSlots = {0xFF, 0xFF, 0xFF, 0xFF};
    std::array<ScoreboardPlayerSnapshot, 4> playersBySlot{};
    std::array<ScoreboardTeamSnapshot, 2> teamsByIndex{};
};

// Enemy Target HUD: "the opponent the local player last damaged".
//
// The ROM already owns this state -- HUD event 0x3D stores the victim slot and
// a 60-tick timer on a local hit, and a separate ROM routine decrements the
// timer and restores slot = -1 at zero. Custom HUD reads that pair and nothing
// else: it must never infer the target from HP deltas or the crosshair, and
// must never run its own countdown. Everything else (name, hunter, HP, score)
// is read live from the target's player struct and the match runtime while the
// panel is up, so damage taken during those two seconds is reflected.
struct EnemyTargetSnapshot {
    bool visible = false;
    uint8_t slot = 0xFF;
    int16_t timer = 0;
    uint8_t hunterId = 0xFF;
    uint8_t teamIndex = 0xFF;
    uint8_t gameMode = 0;
    bool teams = false;
    QString displayName;
    uint16_t hp = 0;
    uint16_t maxHp = 0;
    // Raw runtime values; MelonPrimeHudRenderDraw.inc turns them into a
    // ScoreboardVisualValue with the same MAX/tick rules as the scoreboard.
    int32_t rawValue = 0;
    int32_t rawGoal = 0;
    bool valueIsTime = false;
};

struct ScoreboardStaticPlayerSnapshot {
    uint8_t slot = 0xFF;
    uint8_t teamIndex = 0xFF;
    uint8_t hunterId = 0xFF;
    uint8_t stars = 0xFF;
    bool active = false;
    QString displayName;
};

struct ScoreboardMatchCache {
    melonDS::NDS* nds = nullptr;
    melonDS::u8* ram = nullptr;
    uint8_t romGroup = 0xFFu;
    uint32_t matchSerial = 0;
    bool valid = false;
    uint8_t gameMode = 0;
    uint8_t activePlayers = 0;
    uint16_t teamFlags = 0;
    bool teams = false;
    std::array<ScoreboardStaticPlayerSnapshot, 4> playersBySlot{};
};

struct HudRuntimeFrameCache {
    melonDS::NDS* nds = nullptr;
    melonDS::u8* ram = nullptr;
    uint32_t gameFrame = 0xFFFFFFFFu;
    uint32_t offP = 0;
    uint8_t romGroup = 0xFFu;
    bool baseValid = false;
    bool adventureValid = false;
    bool gameplayVisibilityValid = false;
    bool gameplayVisible = false;
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
    bool scoreboardValid = false;
    uint32_t scoreboardMatchSerial = 0;
    struct ScoreboardSnapshot scoreboard;
    bool enemyTargetValid = false;
    struct EnemyTargetSnapshot enemyTarget;
};

// Deliberately excludes EnemyTargetSnapshot::timer: it ticks every frame and
// changes nothing that is drawn, so keying on it would defeat the cache.
static EnemyTargetPlanKey MakeEnemyTargetPlanKey(const EnemyTargetSnapshot& s)
{
    EnemyTargetPlanKey key;
    key.slot = s.slot;
    key.hunterId = s.hunterId;
    key.teamIndex = s.teamIndex;
    key.gameMode = s.gameMode;
    key.teams = s.teams;
    key.displayName = s.displayName;
    key.hp = s.hp;
    key.maxHp = s.maxHp;
    key.rawValue = s.rawValue;
    key.rawGoal = s.rawGoal;
    key.valueIsTime = s.valueIsTime;
    return key;
}

#include "MelonPrimeHudFrameOwnedState.inc"

static constexpr uint32_t kScoreboardGameModeOffset = 0x000;
static constexpr uint32_t kScoreboardActivePlayersOffset = 0x007;
static constexpr uint32_t kScoreboardTeamFlagsOffset = 0x00E;
static constexpr uint32_t kScoreboardHuntersOffset = 0x03C;
static constexpr uint32_t kScoreboardStarsOffset = 0x0A0;
static constexpr uint32_t kScoreboardPlayerTimeOffset = 0x0B0;
static constexpr uint32_t kScoreboardTeamTimeOffset = 0x0C0;
static constexpr uint32_t kScoreboardPlayerKillsOffset = 0x160;
static constexpr uint32_t kScoreboardTeamKillsOffset = 0x170;
static constexpr uint32_t kScoreboardPlayerDeathsOffset = 0x180;
static constexpr uint32_t kScoreboardTeamDeathsOffset = 0x190;
static constexpr uint32_t kScoreboardPlayerPointsOffset = 0x230;
static constexpr uint32_t kScoreboardTeamPointsOffset = 0x240;
static constexpr uint32_t kScoreboardStandingsOffset = 0x250;
static constexpr uint32_t kScoreboardTeamStandingsOffset = 0x254;
static constexpr uint32_t kScoreboardResultSlotsOffset = 0x258;
static constexpr uint32_t kScoreboardPlayerTeamIndexOffset = 0x4AD;
static constexpr uint32_t kScoreboardPlayerActiveOffset = 0x84C;
static constexpr uint8_t kScoreboardPlayerActiveBit = 0x20;

// Match-goal fields the scoreboard never needed: the Enemy Target panel shows
// the target's progress as value/goal, so it reads the goal straight from the
// match runtime block instead of deriving one.
static constexpr uint32_t kScoreboardPointGoalOffset = 0x014;
static constexpr uint32_t kScoreboardTimeGoalOffset = 0x020;

static inline uint32_t ScoreboardRuntimeBase(const RomAddresses& rom)
{
    // The match runtime block is anchored at the same address as the
    // adventure flag on all seven supported ROMs; the scoreboard uses +0.
    return rom.isInAdventure;
}

static inline uint32_t ScoreboardPlayerNameBase(const RomAddresses& rom)
{
    // baseChosenHunter + 4 is the fixed license-name base shared by all
    // seven supported ROM address tables; keep that fact in one helper.
    return rom.baseChosenHunter + 4u;
}

static inline bool IsKnownScoreboardMode(uint8_t mode)
{
    return mode >= static_cast<uint8_t>(ScoreboardGameMode::Battle)
        && mode <= static_cast<uint8_t>(ScoreboardGameMode::PrimeHunter);
}

struct ScoreboardNameCode {
    uint16_t code;
    const char* utf8;
};

static const ScoreboardNameCode kScoreboardNameCodes[] = {
#include "MelonPrimeHudNameGlyphs.inc"
};

static const char* LookupScoreboardNameGlyph(uint16_t code)
{
    size_t lo = 0;
    size_t hi = sizeof(kScoreboardNameCodes) / sizeof(kScoreboardNameCodes[0]);
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (kScoreboardNameCodes[mid].code < code)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < sizeof(kScoreboardNameCodes) / sizeof(kScoreboardNameCodes[0])
        && kScoreboardNameCodes[lo].code == code)
        return kScoreboardNameCodes[lo].utf8;
    return nullptr;
}

static QString DecodeScoreboardPlayerName(const melonDS::u8* ram, uint32_t address)
{
    std::array<uint8_t, 0x15> raw{};
    std::string utf8;
    utf8.reserve(20);
    // The native record is 20 payload bytes followed by a forced terminator;
    // never let an unterminated record consume the next byte in RAM.
    raw.fill(0);
    for (size_t i = 0; i < raw.size() - 1; ++i) {
        raw[i] = Read8(ram, address + static_cast<uint32_t>(i));
        if (raw[i] == 0)
            break;
    }
    raw.back() = 0;

    // License names use printable ASCII plus the same sorted C2..C7
    // two-byte glyph table as mphSaveViewerEditor. Unknown/control bytes are
    // replaced instead of being passed to UTF-8 decoding as raw game data.
    size_t i = 0;
    while (i < raw.size()) {
        const uint8_t b = raw[i];
        if (b == 0)
            break;
        if (b >= 0x20 && b <= 0x7E) {
            utf8.push_back(static_cast<char>(b));
            ++i;
            continue;
        }
        if (b >= 0xC2 && b <= 0xC7 && i + 1 < raw.size()) {
            const uint16_t code = static_cast<uint16_t>((b << 8) | raw[i + 1]);
            const char* glyph = LookupScoreboardNameGlyph(code);
            utf8 += glyph ? glyph : "?";
            i += 2;
            continue;
        }
        utf8 += "?";
        ++i;
    }
    return QString::fromUtf8(utf8.c_str());
}

static inline bool ScoreboardMatchCacheMatches(const HudRuntimeState& st)
{
    const auto& cache = s_scoreboardMatchCache;
    return cache.valid
        && cache.nds == st.nds
        && cache.ram == st.ram
        && cache.romGroup == st.romGroup
        && cache.matchSerial == s_battleState.scoreboardMatchSerial;
}

static bool EnsureScoreboardMatchCache(const RomAddresses& rom,
                                       const HudRuntimeState& st)
{
    if (!st.ram || !st.nds)
        return false;
    if (ScoreboardMatchCacheMatches(st))
        return true;

    const melonDS::u8* ram = st.ram;
    const uint32_t runtimeBase = ScoreboardRuntimeBase(rom);
    ScoreboardMatchCache cache{};
    cache.nds = st.nds;
    cache.ram = st.ram;
    cache.romGroup = st.romGroup;
    cache.matchSerial = s_battleState.scoreboardMatchSerial;
    cache.gameMode = Read8(ram, runtimeBase + kScoreboardGameModeOffset);
    if (!IsKnownScoreboardMode(cache.gameMode))
        return false;

    cache.activePlayers = static_cast<uint8_t>(std::min<int>(
        Read8(ram, runtimeBase + kScoreboardActivePlayersOffset), 4));
    // During the transition into a match this field is briefly zero. Do not
    // freeze an empty roster into the match cache; retry on the next frame.
    if (cache.activePlayers == 0)
        return false;

    cache.teamFlags = Read16(ram, runtimeBase + kScoreboardTeamFlagsOffset);
    // The runtime TeamFlags bit is authoritative for whether Team rows and
    // TeamStandings are meaningful; do not infer this solely from GameMode.
    cache.teams = (cache.teamFlags & 0x0002u) != 0;

    const uint32_t nameBase = ScoreboardPlayerNameBase(rom);
    int activeSlotCount = 0;
    for (int slot = 0; slot < 4; ++slot) {
        const uint32_t playerBase = rom.playerStructStart
                                  + static_cast<uint32_t>(slot) * Consts::PLAYER_STRUCT_SIZE;
        auto& player = cache.playersBySlot[slot];
        player.slot = static_cast<uint8_t>(slot);
        player.active = (Read8(ram, playerBase + kScoreboardPlayerActiveOffset)
                         & kScoreboardPlayerActiveBit) != 0;
        player.teamIndex = Read8(ram, playerBase + kScoreboardPlayerTeamIndexOffset);
        player.hunterId = Read8(ram, runtimeBase + kScoreboardHuntersOffset
                                         + static_cast<uint32_t>(slot));
        player.stars = Read8(ram, runtimeBase + kScoreboardStarsOffset
                                     + static_cast<uint32_t>(slot));
        if (player.active)
            ++activeSlotCount;
        player.displayName = DecodeScoreboardPlayerName(
            ram, nameBase + static_cast<uint32_t>(slot) * 0x15);
    }
    // ActivePlayers can become non-zero one frame before the player records
    // are published. Retry rather than freezing an empty match roster.
    if (activeSlotCount == 0)
        return false;

    s_scoreboardMatchCache = cache;
    return true;
}

static inline int32_t ReadScoreboardS32(const melonDS::u8* ram, uint32_t address)
{
    return static_cast<int32_t>(Read32(ram, address));
}

static bool ReadHudRuntimeScoreboardSnapshot(const RomAddresses& rom,
                                             const HudRuntimeState& st,
                                             ScoreboardSnapshot& out)
{
    if (!s_hudRuntimeFrameCache.baseValid || !st.ram
        || !HudRuntimeFrameCacheMatches(st))
        return false;
    if (HudRuntimeFrameCacheMatches(s_hudRuntimeFrameCache.state)
        && s_hudRuntimeFrameCache.scoreboardValid
        && s_hudRuntimeFrameCache.scoreboardMatchSerial
               == s_battleState.scoreboardMatchSerial) {
        out = s_hudRuntimeFrameCache.scoreboard;
        return true;
    }

    const melonDS::u8* ram = st.ram;
    if (!EnsureScoreboardMatchCache(rom, st))
        return false;
    const auto& match = s_scoreboardMatchCache;
    const uint32_t runtimeBase = ScoreboardRuntimeBase(rom);
    ScoreboardSnapshot snapshot{};
    snapshot.matchSerial = s_battleState.scoreboardMatchSerial;
    snapshot.gameMode = match.gameMode;
    snapshot.activePlayers = match.activePlayers;
    snapshot.teamFlags = match.teamFlags;
    snapshot.teams = match.teams;
    // The Custom HUD scoreboard is independent of the native START/TAB
    // scoreboard, but its frame timing follows the regular HUD visibility
    // gate. Adventure remains excluded from the snapshot itself.
    snapshot.visible = !st.isAdventure;

    // Team aggregates are authoritative runtime values. Never recalculate or
    // sort them from player rows; only select the aggregate for TeamIndex.
    if (snapshot.teams) {
        for (int team = 0; team < 2; ++team) {
            const uint32_t teamOffset = static_cast<uint32_t>(team) * sizeof(uint32_t);
            snapshot.teamsByIndex[team].time = ReadScoreboardS32(
                ram, runtimeBase + kScoreboardTeamTimeOffset + teamOffset);
            snapshot.teamsByIndex[team].kills = ReadScoreboardS32(
                ram, runtimeBase + kScoreboardTeamKillsOffset + teamOffset);
            snapshot.teamsByIndex[team].deaths = ReadScoreboardS32(
                ram, runtimeBase + kScoreboardTeamDeathsOffset + teamOffset);
            snapshot.teamsByIndex[team].points = ReadScoreboardS32(
                ram, runtimeBase + kScoreboardTeamPointsOffset + teamOffset);
        }
    }

    for (int slot = 0; slot < 4; ++slot) {
        const auto& staticPlayer = match.playersBySlot[slot];
        auto& player = snapshot.playersBySlot[slot];
        player = {};
        player.slot = staticPlayer.slot;
        player.active = staticPlayer.active;
        player.teamIndex = staticPlayer.teamIndex;
        player.hunterId = staticPlayer.hunterId;
        player.stars = staticPlayer.stars;
        player.displayName = staticPlayer.displayName;
        if (!player.active)
            continue;

        const uint32_t playerOffset = static_cast<uint32_t>(slot) * sizeof(uint32_t);
        player.time = ReadScoreboardS32(
            ram, runtimeBase + kScoreboardPlayerTimeOffset + playerOffset);
        player.kills = ReadScoreboardS32(
            ram, runtimeBase + kScoreboardPlayerKillsOffset + playerOffset);
        player.deaths = ReadScoreboardS32(
            ram, runtimeBase + kScoreboardPlayerDeathsOffset + playerOffset);
        player.points = ReadScoreboardS32(
            ram, runtimeBase + kScoreboardPlayerPointsOffset + playerOffset);
        player.standing = Read8(
            ram, runtimeBase + kScoreboardStandingsOffset
                       + static_cast<uint32_t>(slot));
        player.teamStanding = Read8(
            ram, runtimeBase + kScoreboardTeamStandingsOffset
                       + static_cast<uint32_t>(slot));
    }

    for (int displayIndex = 0; displayIndex < 4; ++displayIndex) {
        const uint8_t slot = Read8(ram, runtimeBase + kScoreboardResultSlotsOffset
                                           + static_cast<uint32_t>(displayIndex));
        if (slot < 4) {
            snapshot.resultSlots[displayIndex] = slot;
        }
    }

    out = snapshot;
    s_hudRuntimeFrameCache.scoreboard = snapshot;
    s_hudRuntimeFrameCache.scoreboardValid = true;
    s_hudRuntimeFrameCache.scoreboardMatchSerial = s_battleState.scoreboardMatchSerial;
    return true;
}

// Survival counts down lives; Defender/Prime Hunter count up held time. Every
// other mode scores in points. Kept separate from ResolveScoreboardMetric(),
// which answers a different question (which two scoreboard columns to show).
static inline bool EnemyTargetModeShowsLives(uint8_t gameMode)
{
    return gameMode == static_cast<uint8_t>(ScoreboardGameMode::Survival)
        || gameMode == static_cast<uint8_t>(ScoreboardGameMode::SurvivalTeams);
}

static inline bool EnemyTargetModeUsesTime(uint8_t gameMode)
{
    return gameMode == static_cast<uint8_t>(ScoreboardGameMode::Defender)
        || gameMode == static_cast<uint8_t>(ScoreboardGameMode::DefenderTeams)
        || gameMode == static_cast<uint8_t>(ScoreboardGameMode::PrimeHunter);
}

// Enemy Target snapshot.
//
// Deliberately NOT derived from HP deltas, the crosshair, or a Custom-HUD-side
// countdown: the ROM's own targetSlot/targetTimer pair is the source of truth,
// and it is only read here. Vanilla suppresses the state generation entirely on
// Nintendo WFC, so online play additionally needs the existing
// ShowEnemyHpMeterOnline patch -- Custom HUD does not second-guess that gate.
//
// The per-match roster cache already holds the decoded name, hunter, and team
// for all four slots, so the only per-frame RAM reads are the target pair, the
// live HP pair, and the mode value/goal.
static bool ReadHudRuntimeEnemyTargetSnapshot(const RomAddresses& rom,
                                              const HudRuntimeState& st,
                                              EnemyTargetSnapshot& out)
{
    if (!s_hudRuntimeFrameCache.baseValid || !st.ram
        || !HudRuntimeFrameCacheMatches(st))
        return false;
    if (s_hudRuntimeFrameCache.enemyTargetValid) {
        out = s_hudRuntimeFrameCache.enemyTarget;
        return out.visible;
    }

    const melonDS::u8* ram = st.ram;
    EnemyTargetSnapshot snapshot{};
    // Cache the miss too: a frame with no target must not re-read RAM on a
    // repeated paint of the same emulated frame.
    s_hudRuntimeFrameCache.enemyTargetValid = true;
    s_hudRuntimeFrameCache.enemyTarget = snapshot;

    // Adventure has no opponents; the panel follows the scoreboard's exclusion.
    if (st.isAdventure)
        return false;

    // The ROM writes and clears this pair together -- event 0x3D sets both, the
    // timer update clears the slot at zero -- so the timer alone settles whether
    // there is anything to show. Testing it before reading the slot keeps the
    // steady-state frame (no recent hit, which is most of a match) down to a
    // single RAM read. The slot is still range-checked below: it indexes an
    // array, so it is validated rather than trusted.
    const int16_t timer = static_cast<int16_t>(Read16(ram, rom.enemyTargetTimer));
    if (timer <= 0)
        return false;
    const int16_t slot = static_cast<int16_t>(Read16(ram, rom.enemyTargetSlot));
    if (slot < 0 || slot >= 4)
        return false;

    if (!EnsureScoreboardMatchCache(rom, st))
        return false;
    const auto& match = s_scoreboardMatchCache;
    const auto& player = match.playersBySlot[slot];
    if (!player.active)
        return false;

    snapshot.visible = true;
    snapshot.slot = static_cast<uint8_t>(slot);
    snapshot.timer = timer;
    snapshot.hunterId = player.hunterId;
    snapshot.teamIndex = player.teamIndex;
    snapshot.displayName = player.displayName;
    snapshot.gameMode = match.gameMode;
    snapshot.teams = match.teams;

    const uint32_t playerBase = rom.playerStructStart
                              + static_cast<uint32_t>(slot) * Consts::PLAYER_STRUCT_SIZE;
    snapshot.hp = Read16(ram, playerBase + 0xDAu);
    snapshot.maxHp = Read16(ram, playerBase + 0xDCu);

    const uint32_t runtimeBase = ScoreboardRuntimeBase(rom);
    const bool teamValue = snapshot.teams && snapshot.teamIndex <= 1;
    const uint32_t slotOffset = static_cast<uint32_t>(slot) * sizeof(uint32_t);
    const uint32_t teamOffset = static_cast<uint32_t>(
        snapshot.teamIndex <= 1 ? snapshot.teamIndex : 0) * sizeof(uint32_t);

    if (EnemyTargetModeShowsLives(snapshot.gameMode)) {
        // Survival shows lives left, which the runtime stores as a goal minus
        // the accumulated team deaths rather than as a counter of its own.
        const int32_t goal = ReadScoreboardS32(
            ram, runtimeBase + kScoreboardPointGoalOffset);
        const int32_t deaths = ReadScoreboardS32(
            ram, runtimeBase + kScoreboardTeamDeathsOffset + teamOffset);
        snapshot.rawValue = std::max<int32_t>(0, goal - deaths);
        snapshot.rawGoal = goal;
    } else if (EnemyTargetModeUsesTime(snapshot.gameMode)) {
        snapshot.valueIsTime = true;
        snapshot.rawValue = ReadScoreboardS32(
            ram, runtimeBase + kScoreboardPlayerTimeOffset + slotOffset);
        snapshot.rawGoal = ReadScoreboardS32(
            ram, runtimeBase + kScoreboardTimeGoalOffset);
    } else {
        // Team modes score as a team, so the panel shows the target's team
        // aggregate rather than their personal points.
        const uint32_t pointsBase = teamValue
            ? runtimeBase + kScoreboardTeamPointsOffset + teamOffset
            : runtimeBase + kScoreboardPlayerPointsOffset + slotOffset;
        snapshot.rawValue = ReadScoreboardS32(ram, pointsBase);
        snapshot.rawGoal = ReadScoreboardS32(
            ram, runtimeBase + kScoreboardPointGoalOffset);
    }

    out = snapshot;
    s_hudRuntimeFrameCache.enemyTarget = snapshot;
    return true;
}

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
    s_hudRuntimeFrameCache.gameplayVisibilityValid = false;
    s_hudRuntimeFrameCache.gameplayVisible = false;
    s_hudRuntimeFrameCache.visibleValid = false;
    s_hudRuntimeFrameCache.currentAmmoSpecialValid = false;
    s_hudRuntimeFrameCache.currentAmmoMissileValid = false;
    s_hudRuntimeFrameCache.havingWeaponsValid = false;
    s_hudRuntimeFrameCache.bombCountValid = false;
    s_hudRuntimeFrameCache.matchStatusValid = false;
    s_hudRuntimeFrameCache.rankByteValid = false;
    s_hudRuntimeFrameCache.timeLeftSecondsValid = false;
    s_hudRuntimeFrameCache.timeLimitMinutesValid = false;
    s_hudRuntimeFrameCache.scoreboardValid = false;
    s_hudRuntimeFrameCache.scoreboard = {};
    s_hudRuntimeFrameCache.enemyTargetValid = false;
    s_hudRuntimeFrameCache.enemyTarget = {};
    s_hudRuntimeFrameCache.state = out;
    return true;
}

// Crosshair projection inputs: 124 bytes of guest RAM per game frame.
//
// The player is resolved the way the ROM's own crosshair renderer resolves it,
// by following the local-player pointer, so a stale or wrong player index
// cannot make this project somebody else's aim. Returns false when the pointer
// does not currently address a readable player struct, which happens on
// mid-transition frames.
static bool ReadCrosshairProjectionInput(
    const melonDS::u8* ram,
    const RomAddresses& rom,
    CrosshairProjection::Input& out)
{
    const uint32_t playerBase =
        Read32(ram, rom.crosshairLocalPlayerPointer);
    if (!CrosshairProjection::CanReadPlayerProjectionState(playerBase))
        return false;

    const uint32_t sourceBase =
        playerBase + CrosshairProjection::kPlayerProjectionSourceOffset;
    const uint32_t viewBase =
        playerBase + CrosshairProjection::kPlayerViewTransformOffset;

    for (int i = 0; i < CrosshairProjection::kSourceWordCount; ++i) {
        out.source[i] = static_cast<int32_t>(
            Read32(ram, sourceBase + static_cast<uint32_t>(i) * 4u));
    }
    for (int i = 0; i < CrosshairProjection::kViewWordCount; ++i) {
        out.view[i] = static_cast<int32_t>(
            Read32(ram, viewBase + static_cast<uint32_t>(i) * 4u));
    }
    for (int i = 0; i < CrosshairProjection::kProjectionWordCount; ++i) {
        out.projection[i] = static_cast<int32_t>(
            Read32(ram, rom.crosshairProjectionMatrix
                        + static_cast<uint32_t>(i) * 4u));
    }
    return true;
}

// Pre-frame helmet-clamp sampling, owned by the emulation thread.
//
// This deliberately does NOT go through ReadHudRuntimeBaseState(). That
// function reads and publishes the per-instance HudRuntimeFrameCache, which is
// presentation-owned: with the software renderer the Custom HUD is painted from
// ScreenPanelNative::paintEvent() on the GUI thread, and renderLock does not
// cover RunFrameHook(). Sharing the cache across those two threads would race
// over its non-trivial members -- the scoreboard and enemy-target snapshots
// hold QStrings and are reassigned wholesale on every frame rollover.
//
// The clamp needs five bytes of guest RAM and no cache, so it reads them
// directly. That also keeps ~9 QString destroy/construct pairs per in-game
// frame off the emulation hot path.
struct HudHelmetClampState {
    melonDS::NDS* nds = nullptr;
    uint8_t romGroup = 0xFFu;
    bool hideForNativeUi = false;
};

static bool ReadHudHelmetClampState(EmuInstance* emu,
                                    const RomAddresses& rom,
                                    uint8_t playerPosition,
                                    HudHelmetClampState& out)
{
    if (!emu) return false;

    melonDS::NDS* nds = emu->getNDS();
    melonDS::u8* ram = nds ? nds->MainRAM : nullptr;
    if (!ram) return false;

    out.nds = nds;
    out.romGroup = rom.romGroupIndex;

    // Same predicate, same order, as the painter path's native-UI hide rule;
    // only the Adventure half is read lazily, because it is the rarer case.
    const uint32_t offP = static_cast<uint32_t>(playerPosition) * Consts::PLAYER_ADDR_INC;
    if (Read8(ram, rom.startPressed) == 0x01
        || Read8(ram, rom.gameOver) != 0x00
        || Read16(ram, rom.playerHP + offP) == 0)
    {
        out.hideForNativeUi = true;
        return true;
    }

    out.hideForNativeUi = Read8(ram, rom.isInAdventure) == 0x02
        && Read8(ram, rom.isMapOrUserActionPaused) == 0x01;
    return true;
}

// Adventure camera scenes (elevator rides, cutscene pans, ...) run a
// CameraSequence whose flags byte carries BlockInput (bit 2 / 0x04). The native
// upper-HUD function tests exactly that byte and returns before the crosshair
// renderer, which is why the game's own reticle disappears during those scenes.
// Reading the same decision input keeps Custom HUD in sync on every ROM version
// instead of enumerating scene IDs: the pointer holder is the one global that
// the sequence activate/end code writes and clears (see
// docs/features/hud/custom-hud-adventure-camera-scene.md).
// The pointer is validated against main RAM because it is read straight from
// emulated memory and may be stale/garbage on a mid-transition frame.
static inline bool IsCameraSequenceBlockingInput(const melonDS::u8* ram,
                                                 const RomAddresses& rom)
{
    const uint32_t sequence = Read32(ram, rom.currentCameraSequence);
    if (sequence < 0x02000000u || sequence >= 0x02400000u)
        return false;
    return (Read8(ram, sequence) & 0x04u) != 0u;
}

static void ReadHudRuntimeAdventurePauseState(const RomAddresses& rom,
                                             HudRuntimeState& out)
{
    if (HudRuntimeFrameCacheMatches(out) && s_hudRuntimeFrameCache.adventureValid) {
        out.isAdventure = s_hudRuntimeFrameCache.state.isAdventure;
        out.isMapOrUserActionPaused =
            s_hudRuntimeFrameCache.state.isMapOrUserActionPaused;
        out.isAdventureCameraScene =
            s_hudRuntimeFrameCache.state.isAdventureCameraScene;
        out.isAdventureScanVisor =
            s_hudRuntimeFrameCache.state.isAdventureScanVisor;
        return;
    }

    out.isAdventure = Read8(out.ram, rom.isInAdventure) == 0x02;
    out.isMapOrUserActionPaused = out.isAdventure &&
        Read8(out.ram, rom.isMapOrUserActionPaused) == 0x01;
    out.isAdventureCameraScene = out.isAdventure &&
        IsCameraSequenceBlockingInput(out.ram, rom);
    // != 0 rather than == 1: the switch only ever writes 0/1, but the native
    // regular-HUD gate compares against 0, so this matches the game exactly.
    out.isAdventureScanVisor = out.isAdventure &&
        Read8(out.ram, rom.scanVisorState) != 0x00;

    if (HudRuntimeFrameCacheMatches(out)) {
        s_hudRuntimeFrameCache.state.isAdventure = out.isAdventure;
        s_hudRuntimeFrameCache.state.isMapOrUserActionPaused =
            out.isMapOrUserActionPaused;
        s_hudRuntimeFrameCache.state.isAdventureCameraScene =
            out.isAdventureCameraScene;
        s_hudRuntimeFrameCache.state.isAdventureScanVisor =
            out.isAdventureScanVisor;
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
