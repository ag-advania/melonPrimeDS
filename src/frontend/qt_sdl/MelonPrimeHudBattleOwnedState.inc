// This file is a unity-build fragment included by
// MelonPrimeHudRuntimeSample.inc.  Do not add it to CMakeLists.txt.

// =========================================================================
//  Per-instance battle-state ownership
//
//  The sampling fragment defines BattleMatchState's game-facing value type.
//  This adjacent fragment owns the one runtimeState slot that stores it and
//  the native-HUD restore edge that shares that lifetime.  The typed owner is
//  constructed by CustomHudConfigState's cold constructor.
// =========================================================================

struct HudBattleOwnedState {
    BattleMatchState battle = { .valid = false };
    bool patchForceRestorePending = true;
};

static FORCE_INLINE HudBattleOwnedState& HudBattleState() noexcept
{
    return *ActiveHudConfigState().runtimeState;
}

#define s_battleState (HudBattleState().battle)
#define s_hudPatchForceRestorePending (HudBattleState().patchForceRestorePending)
