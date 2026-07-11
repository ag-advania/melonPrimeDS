#ifndef MELON_PRIME_PATCH_REGISTRY_H
#define MELON_PRIME_PATCH_REGISTRY_H

#ifdef MELONPRIME_DS

#include <cstdint>

class EmuInstance;
namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {

    struct RomAddresses;
    struct MelonPrimePatchState;

    // =========================================================================
    //  Patch registry — single data-driven lifecycle for static write-patches.
    //
    //  Each registry-managed module keeps its existing public functions
    //  (Foo_ApplyOnce / Foo_RestoreOnce / Foo_ResetPatchState); this registry
    //  only centralizes WHO calls them and WHEN. Adding a new write-patch =
    //  one entry in kPatchRegistry (MelonPrimePatchRegistry.cpp); the
    //  restore-on-stop/leave and reset-on-start/boot/stop wiring is automatic.
    //
    //  Intentionally OUTSIDE this registry:
    //   - ARM9 instruction hooks: managed by ARM9Hook_Install/Uninstall/
    //     ResetPatchState (their own registry in MelonPrimeArm9Hook.cpp).
    //   - Custom HUD patch state (CustomHud_*): HUD-owned lifecycle.
    //   - NoDoubleTapJump: transient patch wrapped around weapon-switch
    //     frames in MelonPrimeGameWeapon.cpp.
    //   - The per-frame OsdColor_ApplyOnce re-evaluation in RunFrameHook's
    //     isInGame branch (registry covers only its game-join apply).
    // =========================================================================

    // Unified context for all apply/restore signatures. Built by the caller
    // at each lifecycle site; holds references to caller-owned objects and
    // must not outlive the call.
    struct PatchCtx {
        melonDS::NDS* nds;
        EmuInstance* emu;
        Config::Table& cfg;
        const RomAddresses& rom;
        MelonPrimePatchState& state;
    };

    // Lifecycle sites at which registry entries apply (bitmask).
    enum PatchApplySite : uint8_t {
        PatchSite_GameJoin       = 1u << 0,  // HandleGameJoinInit (once per in-game join)
        PatchSite_ConfigReload   = 1u << 1,  // ApplyConfigReload (only when ROM detected)
        PatchSite_OutOfGameFrame = 1u << 2,  // RunFrameHook !isInGame && focused (per frame; entries self-guard)
        PatchSite_BattleRuntime  = 1u << 3,  // first frame with mode==0x0E && flow==0 after join
    };

    // Restore behavior flags (bitmask). Entries without flags have no RAM
    // restore step (reset-only lifecycle).
    enum PatchRestoreFlag : uint8_t {
        RF_None    = 0,
        RF_OnLeave = 1u << 0,  // restored in RunFrameHook's game-leave block
        RF_OnStop  = 1u << 1,  // restored in OnEmuStart / OnEmuStop
    };

    // Applies every entry whose applySites mask intersects siteMask, in
    // registry order.
    void Patches_Apply(uint8_t siteMask, const PatchCtx& ctx);

    // Restores entries flagged RF_OnLeave / RF_OnStop, in registry order.
    void Patches_RestoreOnLeave(const PatchCtx& ctx);
    void Patches_RestoreOnStop(const PatchCtx& ctx);

    // Resets every entry's per-module patch state (s_applied flags etc.).
    // Does NOT touch emulated RAM.
    void Patches_ResetAll(MelonPrimePatchState& state);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_REGISTRY_H
