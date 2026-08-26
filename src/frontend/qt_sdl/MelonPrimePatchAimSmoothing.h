#ifndef MELON_PRIME_PATCH_AIM_SMOOTHING_H
#define MELON_PRIME_PATCH_AIM_SMOOTHING_H

#ifdef MELONPRIME_DS

namespace melonDS { class NDS; }

namespace MelonPrime {

    struct RomAddresses;

    // Safe anti-smoothing ARM9 instruction patch.
    //
    // MPH folds the raw aim delta through a 4-frame moving average. This
    // rewrites two instruction pairs so the read is taken from +0x3C / +0x44
    // (which the game's zero-overwrite mechanism leaves alone) and the average
    // is branched past, landing directly on the final `strh`.
    //
    // Both directions are guarded: apply only writes when the exact original
    // words are still in place, and restore only writes when the exact patched
    // words are still in place, so a savestate or a foreign patch can never be
    // half-rewritten. A ROM group without a resolved address is a no-op.
    //
    // `disableMphAimSmoothing` true applies the patch; false restores vanilla.
    void AimSmoothing_ApplyOrRestore(
        melonDS::NDS* nds,
        const RomAddresses& rom,
        bool disableMphAimSmoothing);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_AIM_SMOOTHING_H
