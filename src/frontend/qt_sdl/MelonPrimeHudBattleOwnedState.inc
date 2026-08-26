// This file is a unity-build fragment included by
// MelonPrimeHudRuntimeSample.inc.  Do not add it to CMakeLists.txt.

// =========================================================================
//  Per-instance battle-state ownership
//
//  The sampling fragment defines BattleMatchState's game-facing value type.
//  This adjacent fragment owns the one runtimeState slot that stores it and
//  the native-HUD restore edge that shares that lifetime.  Keeping the slot
//  and construction path here does not change allocation count or layout.
// =========================================================================

struct HudBattleOwnedState {
    BattleMatchState battle = { .valid = false };
    bool patchForceRestorePending = true;
};

static HudBattleOwnedState& HudBattleState()
{
    auto& slot = ActiveHudConfigState().runtimeState;
    if (!slot)
        slot = std::make_shared<HudBattleOwnedState>();
    return *std::static_pointer_cast<HudBattleOwnedState>(slot);
}

#define s_battleState (HudBattleState().battle)
#define s_hudPatchForceRestorePending (HudBattleState().patchForceRestorePending)
