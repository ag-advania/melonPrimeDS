// This file is a unity-build fragment included by
// MelonPrimeHudRenderRuntime.inc.  Do not add it to CMakeLists.txt.

// =========================================================================
//  Native HUD patch lifecycle
// =========================================================================

bool CustomHud_IsHelmetLayerHideConfigured(Config::Table& localCfg)
{
    return localCfg.GetBool(kCfgCustomHud)
        && localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Helmet);
}
// One-shot guard: when CustomHUD transitions to disabled, perform a full
// ForceRestore on the next in-game frame regardless of s_hudPatchApplied,
// so any out-of-sync RAM state (savestate / persistent RAM / JIT corner
// cases) is normalized exactly once. Reset to true when CustomHUD becomes
// enabled or on emu start/stop, so a future disable triggers it again.

static void CustomHud_MarkNoHudPatchDesired()
{
    s_hudPatchForceRestorePending = true;
}

static void CustomHud_RestoreNativeHudState(const RomAddresses& /*rom*/,
                                            const HudRuntimeState& st)
{
    const bool needForceRestore = s_hudPatchForceRestorePending;
    auto& patch = ActiveHudConfigState().noHudPatch;
    if (NoHudPatch_GetAppliedMask(patch) == 0 && !needForceRestore)
        return;

    if (needForceRestore) {
        NoHudPatch_RestoreAll(patch, st.nds, st.romGroup);
        s_hudPatchForceRestorePending = false;
    } else {
        NoHudPatch_Sync(patch, st.nds, st.romGroup, 0);
    }
}

void CustomHud_ClampHelmetLayersPreFrame(
    CustomHudConfigState& hudConfig,
    EmuInstance* emu,
    const RomAddresses& rom,
    uint8_t playerPosition)
{
    const ScopedHudConfigState active(hudConfig);
    // Gate on the tracker, not the config: the helmet bit is only ever applied
    // by Render() while CustomHUD is enabled, and this avoids a per-frame toml
    // lookup on the hot path.
    auto& patch = ActiveHudConfigState().noHudPatch;
    const uint16_t appliedMask = NoHudPatch_GetAppliedMask(patch);
    if (!(appliedMask & (1u << NOHUD_HELMET)))
        return;

    HudHelmetClampState st = {};
    if (!ReadHudHelmetClampState(emu, rom, playerPosition, st))
        return;

    // Restore the native helmet instruction before native UI is rendered.
    // Vulkan applies its stronger guarantee presentation-only in the scanline
    // renderer, without changing the emulated register state.
    if (st.hideForNativeUi)
    {
        NoHudPatch_Sync(
            patch,
            st.nds,
            st.romGroup,
            static_cast<uint16_t>(appliedMask & ~(1u << NOHUD_HELMET)));
        return;
    }

    NoHudPatch_ClampHelmetLayers(st.nds, st.romGroup);
}

void CustomHud_EnsurePatchRestored(
    CustomHudConfigState& hudConfig,
    EmuInstance* emu,
    Config::Table& localCfg,
    const RomAddresses& rom,
    uint8_t playerPosition,
    bool isInGame)
{
    const ScopedHudConfigState active(hudConfig);
    if (isInGame && CustomHud_IsEnabled(localCfg)) {
        // HUD is enabled — Render() handles patches. Re-arm the force-restore
        // for the next disable event.
        CustomHud_MarkNoHudPatchDesired();
        return;
    }

    HudRuntimeState st = {};
    if (!ReadHudRuntimeBaseState(emu, rom, playerPosition, st))
        return;

    CustomHud_RestoreNativeHudState(rom, st);
}

void CustomHud_ResetPatchState(CustomHudConfigState& hudConfig)
{
    const ScopedHudConfigState active(hudConfig);
    BumpCustomHudVisualGeneration();
    NoHudPatch_ResetState(ActiveHudConfigState().noHudPatch);
    s_cache.valid = false;
    s_battleState.valid = false;
    s_scoreboardMatchCache = {};
    s_hudRuntimeFrameCache.scoreboardValid = false;
    s_hudRuntimeFrameCache.scoreboardMatchSerial = 0;
    s_hudRuntimeFrameCache.scoreboard = {};
    s_hudRuntimeFrameCache.enemyTargetValid = false;
    s_hudRuntimeFrameCache.enemyTarget = {};
    s_chPrevPxCx = INT_MIN;
    s_chPrevPxCy = INT_MIN;
    s_chStickyPxCx = INT_MIN;
    s_chStickyPxCy = INT_MIN;
    s_chCentreHold.Reset();
    ResetCrosshairZoomDisplay(0.0f);
    s_hudPatchForceRestorePending = true;
}

void CustomHud_ReconcilePatchAfterSavestateLoad(
    CustomHudConfigState& hudConfig,
    EmuInstance* emu,
    Config::Table& localCfg,
    const RomAddresses& rom,
    uint8_t playerPosition)
{
    const ScopedHudConfigState active(hudConfig);
    BumpCustomHudVisualGeneration();
    auto& patch = ActiveHudConfigState().noHudPatch;

    // Do not trust the normal applied-mask tracker here. A loaded state can
    // contain either original or patched ARM9 HUD instructions, regardless of
    // the CustomHUD setting that is active now.
    NoHudPatch_InvalidateState(patch);
    s_hudPatchForceRestorePending = true;

    HudRuntimeState st = {};
    if (!ReadHudRuntimeBaseState(emu, rom, playerPosition, st))
        return;

    if (!CustomHud_IsEnabled(localCfg)) {
        CustomHud_RestoreNativeHudState(rom, st);
        return;
    }

    if (!st.isStartPressed && st.currentHP != 0 && !st.isGameOver)
        ReadHudRuntimeAdventurePauseState(rom, st);

    uint16_t desiredMask = LoadNoHudMaskFromConfig(localCfg);
    if (ShouldHideForGameplayState(
            st.isStartPressed,
            st.currentHP,
            st.isGameOver,
            st.isAdventure,
            st.isMapOrUserActionPaused))
    {
        desiredMask &= static_cast<uint16_t>(~(1u << NOHUD_HELMET));
    }

    // Perform the full reconciliation before msg_LoadState returns. Waiting
    // for the next presentation callback would fix the persistent mismatch,
    // but could still expose one native-HUD frame immediately after F8.
    NoHudPatch_Sync(patch, st.nds, st.romGroup, desiredMask);
    CustomHud_MarkNoHudPatchDesired();
}
