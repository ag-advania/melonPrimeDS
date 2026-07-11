#ifndef MELON_PRIME_PATCH_NO_HUD_H
#define MELON_PRIME_PATCH_NO_HUD_H

#ifdef MELONPRIME_CUSTOM_HUD

#include <cstdint>

namespace melonDS { class NDS; }

namespace MelonPrime {

    struct NoHudPatchState {
        uint16_t appliedMask = 0;
    };

    // Individual default-HUD elements that can be hidden via ARM9 patches.
    // Bit indices for the desiredMask passed to NoHudPatch_Sync.
    //
    // Score rows correspond to the per-mode top-screen status callback that
    // is dispatched by `020E0670` (mode index). NOP'ing each mode's bl to
    // the draw helper hides only that mode's "label + value" row without
    // touching the other modes.
    enum NoHudElement : uint8_t {
        NOHUD_HELMET             = 0,
        NOHUD_AMMO               = 1,
        NOHUD_WEAPONICON         = 2,
        NOHUD_HP                 = 3,
        NOHUD_CROSSHAIR          = 4,
        NOHUD_SCORE_BATTLE       = 5,  // mode=2 (Battle / point)
        NOHUD_SCORE_SURVIVAL     = 6,  // mode=3 (Survival / lives)
        NOHUD_SCORE_PRIMEHUNTER  = 7,  // mode=4 (Prime Hunter / hold time)
        NOHUD_SCORE_BOUNTY       = 8,  // mode=5 (Bounty / score)
        NOHUD_SCORE_CAPTURE      = 9,  // mode=6 (Capture / octolith count)
        NOHUD_SCORE_DEFENDER     = 10, // mode=7 (Defender / hold time)
        NOHUD_SCORE_NODE         = 11, // mode=8 (Node / score)
        NOHUD_BOMB               = 12, // bomb-count HUD (Boost HUD untouched)
        NOHUD_BOOST              = 13, // Samus Morph Ball Boost icon + BOOST label
        NOHUD_CLOAK              = 14, // Cloak icon + CLOAK label
        NOHUD_DOUBLE_DAMAGE      = 15, // Double Damage icon + label
        NOHUD_ELEMENT_COUNT      = 16,
    };

    inline constexpr uint16_t NOHUD_MASK_ALL =
        static_cast<uint16_t>((1u << NOHUD_ELEMENT_COUNT) - 1u);

    // Reconcile ARM9 RAM with `desiredMask` (bit i = NoHudElement i).
    // Writes the patch value for set bits, the original value for clear
    // bits. Only writes addresses whose state actually changes since the
    // last sync, so steady-state cost is zero.
    void NoHudPatch_Sync(NoHudPatchState& state, melonDS::NDS* nds, uint8_t romGroup,
                         uint16_t desiredMask);

    // Host-side helmet layer clamp. Clears hudToggle bits 0x0E and the
    // already-reflected main-DISPCNT bits 0x0E00 (top-screen BG1-3 — the
    // helmet/visor layers). The game's own clamp (the patched helmet site)
    // early-outs during spawn/death states, letting init writers briefly
    // restore those layers before the next HUD update; calling this every
    // frame before RunFrame closes that window. BG0/OBJ are untouched, so
    // other HUD elements and the pause/score UI are unaffected.
    void NoHudPatch_ClampHelmetLayers(melonDS::NDS* nds, uint8_t romGroup);

    // Force-restore every element regardless of the internal tracker.
    // Used when CustomHUD is disabled to guarantee a clean ARM9 state
    // even if the tracker is out of sync (savestate / persistent RAM /
    // JIT corner cases).
    void NoHudPatch_RestoreAll(NoHudPatchState& state, melonDS::NDS* nds, uint8_t romGroup);

    // Reset the internal tracker without touching ARM9 RAM. Called on
    // emu start/stop where the binary is reloaded fresh.
    void NoHudPatch_ResetState(NoHudPatchState& state);

    // Returns the currently-tracked applied mask.
    uint16_t NoHudPatch_GetAppliedMask(const NoHudPatchState& state);

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_PATCH_NO_HUD_H
