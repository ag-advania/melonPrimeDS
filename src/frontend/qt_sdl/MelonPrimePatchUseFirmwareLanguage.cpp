#ifdef MELONPRIME_DS

#include "MelonPrimePatchUseFirmwareLanguage.h"
#include "Config.h"
#include "NDS.h"
#include "SPI_Firmware.h"

namespace MelonPrime {

// ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
//
// Direct write to the game's internal language data field — no code cave.
// Reads DS firmware language from the NDS firmware settings and maps it to
// the game's internal language slot encoding, then ARM9Write8s it directly.
// Applied every frame while !isInGame, same pattern as ApplySelectedHunterStrict.
//
// cave38 from the original cheat code is the address of the game's language
// field byte.  Lower 6 bits [5:0] hold the language slot; upper 2 bits are
// unrelated flags that must be preserved.
//
// Firmware language → game language bits [5:0]:
//   0 Japanese → 0x05  (slot 5, Japanese/Korean)
//   1 English  → 0x00  (slot 0)
//   2 French   → 0x21
//   3 German   → 0x22
//   4 Italian  → 0x23
//   5 Spanish  → 0x24
//   6 Chinese  → 0x00  (not in MPH, fall back to English)
//   7 Reserved → 0x00
static constexpr uint8_t kFwToGameLang[8] = {
    0x05u, // Japanese
    0x00u, // English
    0x21u, // French
    0x22u, // German
    0x23u, // Italian
    0x24u, // Spanish
    0x00u, // Chinese (not in MPH)
    0x00u, // Reserved
};

// Game language field address per ROM (== cave38 in the original cheat).
// 0xFFFFFFFFu = not applicable for this ROM version.
static constexpr uint32_t kLangAddr[7] = {
    0x020E98E8u, // JP1_0
    0x020E98A8u, // JP1_1
    0x020E77A8u, // US1_0
    0x020E8268u, // US1_1
    0x020E8288u, // EU1_0
    0x020E8308u, // EU1_1
    0xFFFFFFFFu, // KR1_0 — temporarily excluded; restore to 0x020E10AAu when ready
};

void UseFirmwareLanguage_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex, uint32_t isInAdventureAddr)
{
    if (!cfg.GetBool("Metroid.BugFix.UseFirmwareLanguage")) return;
    if (romGroupIndex >= 7) return;

    const uint32_t addr = kLangAddr[romGroupIndex];
    if (addr == 0xFFFFFFFFu) return;

    // JP ROMs only: skip in adventure mode (isInAdventure byte == 0x02).
    // EU and US ROMs apply in all states including adventure.
    const bool isJp = (romGroupIndex == 0 || romGroupIndex == 1);
    if (isJp && nds->ARM9Read8(isInAdventureAddr) == 0x02) return;

    const uint8_t fwLang = nds->GetFirmware().GetEffectiveUserData().Settings & 0x7u;
    const uint8_t gameLangBits = kFwToGameLang[fwLang];

    const uint8_t oldVal = nds->ARM9Read8(addr);
    const uint8_t newVal = (oldVal & ~0x3Fu) | gameLangBits;
    if (newVal != oldVal)
        nds->ARM9Write8(addr, newVal);
}

void UseFirmwareLanguage_ResetPatchState()
{
    // No persistent state.
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
