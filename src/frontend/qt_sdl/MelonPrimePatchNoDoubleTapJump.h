#ifndef MELON_PRIME_PATCH_NO_DOUBLE_TAP_JUMP_H
#define MELON_PRIME_PATCH_NO_DOUBLE_TAP_JUMP_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace melonDS { class NDS; }

namespace MelonPrime {

    void NoDoubleTapJumpPatch_Apply(melonDS::NDS* nds, uint8_t romGroup);
    void NoDoubleTapJumpPatch_Restore(melonDS::NDS* nds, uint8_t romGroup);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_NO_DOUBLE_TAP_JUMP_H
