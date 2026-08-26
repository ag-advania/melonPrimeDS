#ifdef MELONPRIME_DS

#include "MelonPrimePatchAimSmoothing.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "NDS.h"

#include <cstdint>

namespace MelonPrime {

namespace {

// "b +0x18": jumps exactly to the final strh instruction, skipping all smoothing.
constexpr uint32_t kJumpInstr = 0xEA000006u;

// Apply one axis. Only writes when both original words are still in place, so a
// second apply, a foreign patch, or a savestate loaded mid-match cannot leave
// half of the pair rewritten.
inline void ApplyAxis(melonDS::NDS* nds,
                      melonDS::u32 addr,
                      uint32_t orig1,
                      uint32_t orig2,
                      uint32_t patched1)
{
    if (nds->ARM9Read32(addr) == orig1 &&
        nds->ARM9Read32(addr + 4) == orig2) {
        nds->ARM9Write32(addr, patched1);
        nds->ARM9Write32(addr + 4, kJumpInstr);
    }
}

// Restore one axis, with the mirror-image precondition: only our own patched
// pair is reverted.
inline void RestoreAxis(melonDS::NDS* nds,
                        melonDS::u32 addr,
                        uint32_t orig1,
                        uint32_t orig2,
                        uint32_t patched1)
{
    if (nds->ARM9Read32(addr) == patched1 &&
        nds->ARM9Read32(addr + 4) == kJumpInstr) {
        nds->ARM9Write32(addr, orig1);
        nds->ARM9Write32(addr + 4, orig2);
    }
}

} // namespace

void AimSmoothing_ApplyOrRestore(melonDS::NDS* nds,
                                 const RomAddresses& rom,
                                 bool disableMphAimSmoothing)
{
    if (!nds || rom.aimPatchAddrX == 0) return;

    if (disableMphAimSmoothing) {
        ApplyAxis(nds, rom.aimPatchAddrX,
                  rom.aimPatchOrigX1, rom.aimPatchOrigX2, rom.aimPatchX1);
        ApplyAxis(nds, rom.aimPatchAddrY,
                  rom.aimPatchOrigY1, rom.aimPatchOrigY2, rom.aimPatchY1);
    } else {
        RestoreAxis(nds, rom.aimPatchAddrX,
                    rom.aimPatchOrigX1, rom.aimPatchOrigX2, rom.aimPatchX1);
        RestoreAxis(nds, rom.aimPatchAddrY,
                    rom.aimPatchOrigY1, rom.aimPatchOrigY2, rom.aimPatchY1);
    }
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
