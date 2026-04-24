#ifdef MELONPRIME_DS

#include "MelonPrimePatchNoDoubleTapJump.h"
#include "NDS.h"

namespace MelonPrime {

// JP1_0, JP1_1, US1_0, US1_1, EU1_0, EU1_1, KR1_0
static constexpr uint32_t kNoDoubleTapJumpAddr[7] = {
    0x020253B0, 0x020253B0, 0x020253D4, 0x020253D4,
    0x020253CC, 0x020253D4, 0x0200E2C4,
};

static constexpr uint32_t kNoDoubleTapJumpRestore = 0x1A000004;
static constexpr uint32_t ARM_NOP                 = 0xE1A00000;

void NoDoubleTapJumpPatch_Apply(melonDS::NDS* nds, uint8_t romGroup)
{
    nds->ARM9Write32(kNoDoubleTapJumpAddr[romGroup], ARM_NOP);
}

void NoDoubleTapJumpPatch_Restore(melonDS::NDS* nds, uint8_t romGroup)
{
    nds->ARM9Write32(kNoDoubleTapJumpAddr[romGroup], kNoDoubleTapJumpRestore);
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
