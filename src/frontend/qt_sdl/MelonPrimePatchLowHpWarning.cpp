#ifdef MELONPRIME_DS

#include "MelonPrimePatchLowHpWarning.h"
#include "Config.h"
#include "MelonPrimeDef.h"
#include "NDS.h"

#include <algorithm>

namespace MelonPrime {

// Mode values (must match the settings UI combobox order).
enum LowHpWarningMode {
    MODE_DISABLED   = 0,  // revert to vanilla (HP < 25)
    MODE_FIXED      = 1,  // one threshold for every Damage level
    MODE_PER_DAMAGE = 2,  // Low / Medium / High thresholds chosen by current DamageLevel
    MODE_AUTO_SCALE = 3,  // base threshold scaled per DamageLevel (0.75 / 1.0 / 1.25)
};

static constexpr uint32_t kCmpBase = 0xE3500000u; // cmp r0,#imm template (vanilla = 0xE3500019, HP < 25)
static constexpr uint32_t kCmpVanilla = 0xE3500019u;

// Per-ROM addresses, indexed by romGroupIndex.
// JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
//   cmp        : low-HP-warning `cmp r0,#0x19` (vanilla word 0xE3500019); the
//                immediate is rewritten to the chosen threshold.
//   damageRead : runtime DamageLevel byte (0=Low, 1=Medium, 2=High).
//                NOTE: only used by Per Damage / Auto Scale modes; these values are
//                carried over from the earlier investigation and are NOT yet verified
//                against the confirmed cmp addresses. Fixed mode does not use them.
struct LowHpAddrs { uint32_t cmp, damageRead; };
static constexpr LowHpAddrs kAddr[7] = {
    {0x02105CC0u, 0x020E9A48u}, // JP1_0
    {0x02105C80u, 0x020E9A08u}, // JP1_1
    {0x02103B80u, 0x020E7908u}, // US1_0
    {0x02104640u, 0x020E83C8u}, // US1_1
    {0x02104660u, 0x020E83E8u}, // EU1_0
    {0x021046E0u, 0x020E8468u}, // EU1_1
    {0x020FCB64u, 0x020E1204u}, // KR1_0
};

static inline uint32_t ClampThreshold(int v)
{
    return static_cast<uint32_t>(std::clamp(v, 0, 255));
}

void LowHpWarning_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex)
{
    if (romGroupIndex >= 7) return;

    const int mode = cfg.GetInt(MelonPrime::CfgKey::LowHpWarningMode);
    // Disabled: leave the game's code untouched (do NOT write vanilla). The cmp
    // addresses are still under verification; writing on the default config would
    // risk corrupting an unrelated location until they are confirmed.
    if (mode == MODE_DISABLED) return;

    const LowHpAddrs& a = kAddr[romGroupIndex];

    uint32_t threshold;
    switch (mode) {
    case MODE_FIXED:
        threshold = ClampThreshold(cfg.GetInt(MelonPrime::CfgKey::LowHpWarningFixed));
        break;
    case MODE_PER_DAMAGE: {
        const int dl = nds->ARM9Read8(a.damageRead);
        const char* key = (dl == 0) ? MelonPrime::CfgKey::LowHpWarningLow
                        : (dl == 2) ? MelonPrime::CfgKey::LowHpWarningHigh
                        :             MelonPrime::CfgKey::LowHpWarningMedium; // 1 or unexpected
        threshold = ClampThreshold(cfg.GetInt(key));
        break;
    }
    case MODE_AUTO_SCALE: {
        const int base = std::clamp(cfg.GetInt(MelonPrime::CfgKey::LowHpWarningAutoBase), 0, 255);
        const int dl = nds->ARM9Read8(a.damageRead);
        int scaled;
        if (dl == 0)      scaled = (base * 3 + 2) / 4;   // Low  ~0.75x (rounded)
        else if (dl == 2) scaled = (base * 5 + 2) / 4;   // High ~1.25x (rounded)
        else              scaled = base;                 // Medium 1.0x
        threshold = ClampThreshold(scaled);
        break;
    }
    default:
        return; // unknown mode → no-op
    }

    const uint32_t word = kCmpBase | threshold;
    nds->ARM9Write32(a.cmp, word);
}

void LowHpWarning_RestoreOnce(melonDS::NDS* nds, uint8_t romGroupIndex)
{
    if (!nds || romGroupIndex >= 7)
        return;

    const uint32_t addr = kAddr[romGroupIndex].cmp;
    const uint32_t current = nds->ARM9Read32(addr);
    if ((current & 0xFFFFFF00u) == kCmpBase && current != kCmpVanilla)
        nds->ARM9Write32(addr, kCmpVanilla);
}

void LowHpWarning_ResetPatchState()
{
    // No persistent state: re-applied once per match join from HandleGameJoinInit,
    // which re-reads the current DamageLevel.
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
