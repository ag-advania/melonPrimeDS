#ifndef MELON_PRIME_PATCH_NO_HUD_H
#define MELON_PRIME_PATCH_NO_HUD_H

#ifdef MELONPRIME_CUSTOM_HUD

#include <cstdint>

namespace melonDS { class NDS; }

namespace MelonPrime {

    void NoHudPatch_Apply(melonDS::NDS* nds, uint8_t romGroup);
    void NoHudPatch_Restore(melonDS::NDS* nds, uint8_t romGroup);
    void NoHudPatch_ForceRestore(melonDS::NDS* nds, uint8_t romGroup);
    void NoHudPatch_ResetState();
    bool NoHudPatch_IsApplied();

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_PATCH_NO_HUD_H
