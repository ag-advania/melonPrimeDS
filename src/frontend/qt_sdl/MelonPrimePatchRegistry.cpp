#ifdef MELONPRIME_DS

#include "MelonPrimePatchRegistry.h"
#include "MelonPrimePatchState.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "EmuInstance.h"

#include "MelonPrimePatchAspectRatio.h"
#include "MelonPrimePatchOsdColor.h"
#include "MelonPrimePerfProbe.h"
#include "MelonPrimePatchLowHpWarning.h"
#include "MelonPrimePatchInstantAimFollow.h"
#include "MelonPrimePatchShowHeadshotOnline.h"
#include "MelonPrimePatchShowEnemyHpMeterOnline.h"
#include "MelonPrimePatchDisableDoubleDamageMultiplier.h"
#include "MelonPrimePatchNoPickingUpSpecificItems.h"
#include "MelonPrimePatchFixWifi.h"
#include "MelonPrimePatchUseFirmwareLanguage.h"
#include "MelonPrimePatchExpandStageMatrix.h"

namespace MelonPrime {

    namespace {

        // =====================================================================
        //  Thin adapters — map the unified PatchCtx onto each module's
        //  existing signature. Module internals are untouched.
        // =====================================================================

        // --- apply adapters ---------------------------------------------------
        void Apply_InGameAspectRatio(const PatchCtx& ctx)
        {
            InGameAspectRatio_ApplyOnce(ctx.state, ctx.emu, ctx.cfg, ctx.rom);
        }
        void Apply_OsdColor(const PatchCtx& ctx)
        {
            OsdColor_ApplyOnce(ctx.state, ctx.emu, ctx.cfg, ctx.rom);
        }
        void Apply_LowHpWarning(const PatchCtx& ctx)
        {
            LowHpWarning_ApplyOnce(ctx.nds, ctx.cfg, ctx.rom.romGroupIndex);
        }
        void Apply_InstantAimFollow(const PatchCtx& ctx)
        {
            InstantAimFollow_ApplyOnce(ctx.state, ctx.nds, ctx.cfg, ctx.rom.romGroupIndex);
        }
        void Apply_ShowHeadshotOnline(const PatchCtx& ctx)
        {
            ShowHeadshotOnline_ApplyOnce(ctx.state, ctx.nds, ctx.cfg, ctx.rom.romGroupIndex);
        }
        void Apply_ShowEnemyHpMeterOnline(const PatchCtx& ctx)
        {
            ShowEnemyHpMeterOnline_ApplyOnce(ctx.state, ctx.nds, ctx.cfg, ctx.rom.romGroupIndex);
        }
        void Apply_DisableDoubleDamageMultiplier(const PatchCtx& ctx)
        {
            DisableDoubleDamageMultiplier_ApplyOnce(ctx.state, ctx.nds, ctx.cfg, ctx.rom.romGroupIndex);
        }
        void Apply_NoPickingUpSpecificItems(const PatchCtx& ctx)
        {
            NoPickingUpSpecificItems_ApplyOnce(ctx.state, ctx.nds, ctx.cfg, ctx.rom.romGroupIndex);
        }
        void Apply_FixWifi(const PatchCtx& ctx)
        {
            FixWifi_ApplyOnce(ctx.nds, ctx.cfg, ctx.rom.romGroupIndex);
        }
        void Apply_UseFirmwareLanguage(const PatchCtx& ctx)
        {
            UseFirmwareLanguage_ApplyOnce(ctx.nds, ctx.cfg, ctx.rom.romGroupIndex,
                                          ctx.rom.isInAdventure);
        }
        void Apply_ExpandStageMatrix(const PatchCtx& ctx)
        {
            ExpandStageMatrix_ApplyIfLoaded(ctx.state, ctx.nds, ctx.cfg, ctx.rom.romGroupIndex);
        }

        // --- restore adapters -------------------------------------------------
        void Restore_OsdColor(const PatchCtx& ctx)
        {
            OsdColor_RestoreOnce(ctx.state, ctx.nds, ctx.rom);
        }
        void Restore_LowHpWarning(const PatchCtx& ctx)
        {
            LowHpWarning_RestoreOnce(ctx.nds, ctx.rom.romGroupIndex);
        }
        void Restore_InstantAimFollow(const PatchCtx& ctx)
        {
            InstantAimFollow_RestoreOnce(ctx.state, ctx.nds, ctx.rom.romGroupIndex);
        }
        void Restore_ShowHeadshotOnline(const PatchCtx& ctx)
        {
            ShowHeadshotOnline_RestoreOnce(ctx.state, ctx.nds, ctx.rom.romGroupIndex);
        }
        void Restore_ShowEnemyHpMeterOnline(const PatchCtx& ctx)
        {
            ShowEnemyHpMeterOnline_RestoreOnce(ctx.state, ctx.nds, ctx.rom.romGroupIndex);
        }
        void Restore_DisableDoubleDamageMultiplier(const PatchCtx& ctx)
        {
            DisableDoubleDamageMultiplier_RestoreOnce(ctx.state, ctx.nds, ctx.rom.romGroupIndex);
        }
        void Restore_NoPickingUpSpecificItems(const PatchCtx& ctx)
        {
            NoPickingUpSpecificItems_RestoreOnce(ctx.state, ctx.nds, ctx.rom.romGroupIndex);
        }

        void Reset_LowHpWarning(MelonPrimePatchState&) { LowHpWarning_ResetPatchState(); }
        void Reset_FixWifi(MelonPrimePatchState&) { FixWifi_ResetPatchState(); }
        void Reset_UseFirmwareLanguage(MelonPrimePatchState&) { UseFirmwareLanguage_ResetPatchState(); }

        // =====================================================================
        //  Registry entry table
        //
        //  - Iteration order defines apply order. The table order preserves all
        //    three pre-registry apply-site orderings exactly:
        //      GameJoin       = entry 1     (InGameAspectRatio)
        //      BattleRuntime  = entries 2-8 (OsdColor .. NoPickingUp)
        //      ConfigReload   = entries 4-8 (InstantAimFollow .. NoPickingUp)
        //      OutOfGameFrame = entries 9-11 (FixWifi .. ExpandStageMatrix)
        //  - Restore order also follows the table. Safe: all modules write disjoint ARM9
        //    addresses, so restore order between modules cannot matter.
        //  - Mutable module state is supplied through PatchCtx::state and is
        //    owned by the calling MelonPrimeCore. The table and patch address
        //    descriptions remain immutable process-shared data.
        //  - ARM9 instruction hooks are managed by ARM9Hook_Install (a separate
        //    registry); Custom HUD patch state is HUD-owned. Both are
        //    intentionally outside this table.
        // =====================================================================

        struct PatchEntry {
            const char* name;
            uint8_t applySites;                  // PatchApplySite bitmask
            uint8_t restoreFlags;                // PatchRestoreFlag bitmask
            void (*apply)(const PatchCtx&);
            void (*restore)(const PatchCtx&);    // null = no RAM restore step
            void (*resetState)(MelonPrimePatchState&); // per-instance state reset
        };

        constexpr PatchEntry kPatchRegistry[] = {
            { "InGameAspectRatio",
              PatchSite_GameJoin, RF_None,
              &Apply_InGameAspectRatio, nullptr,
              &InGameAspectRatio_ResetPatchState },
            // OsdColor is additionally re-applied per frame in RunFrameHook's
            // isInGame branch after battle runtime latch (game-state-dependent);
            // that call bypasses the registry by design.
            { "OsdColor",
              PatchSite_BattleRuntime, RF_OnLeave,
              &Apply_OsdColor, &Restore_OsdColor,
              &OsdColor_ResetPatchState },
            { "LowHpWarning",
              PatchSite_BattleRuntime, RF_OnLeave | RF_OnStop,
              &Apply_LowHpWarning, &Restore_LowHpWarning,
              &Reset_LowHpWarning },
            { "InstantAimFollow",
              PatchSite_BattleRuntime | PatchSite_ConfigReload, RF_OnLeave | RF_OnStop,
              &Apply_InstantAimFollow, &Restore_InstantAimFollow,
              &InstantAimFollow_ResetPatchState },
            { "ShowHeadshotOnline",
              PatchSite_BattleRuntime | PatchSite_ConfigReload, RF_OnLeave | RF_OnStop,
              &Apply_ShowHeadshotOnline, &Restore_ShowHeadshotOnline,
              &ShowHeadshotOnline_ResetPatchState },
            { "ShowEnemyHpMeterOnline",
              PatchSite_BattleRuntime | PatchSite_ConfigReload, RF_OnLeave | RF_OnStop,
              &Apply_ShowEnemyHpMeterOnline, &Restore_ShowEnemyHpMeterOnline,
              &ShowEnemyHpMeterOnline_ResetPatchState },
            { "DisableDoubleDamageMultiplier",
              PatchSite_BattleRuntime | PatchSite_ConfigReload, RF_OnLeave | RF_OnStop,
              &Apply_DisableDoubleDamageMultiplier, &Restore_DisableDoubleDamageMultiplier,
              &DisableDoubleDamageMultiplier_ResetPatchState },
            { "NoPickingUpSpecificItems",
              PatchSite_BattleRuntime | PatchSite_ConfigReload, RF_OnLeave | RF_OnStop,
              &Apply_NoPickingUpSpecificItems, &Restore_NoPickingUpSpecificItems,
              &NoPickingUpSpecificItems_ResetPatchState },
            { "FixWifi",
              PatchSite_OutOfGameFrame, RF_None,
              &Apply_FixWifi, nullptr,
              &Reset_FixWifi },
            { "UseFirmwareLanguage",
              PatchSite_OutOfGameFrame, RF_None,
              &Apply_UseFirmwareLanguage, nullptr,
              &Reset_UseFirmwareLanguage },
            { "ExpandStageMatrix",  // pattern C: ApplyIfLoaded self-guards; reset is a documented no-op
              PatchSite_OutOfGameFrame, RF_None,
              &Apply_ExpandStageMatrix, nullptr,
              &ExpandStageMatrix_ResetPatchState },
        };

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        [[nodiscard]] static const char* DevPatchApplySiteLabel(uint8_t siteMask) noexcept
        {
            switch (siteMask) {
            case PatchSite_GameJoin:       return "GameJoin";
            case PatchSite_ConfigReload:   return "ConfigReload";
            case PatchSite_OutOfGameFrame: return "OutOfGameFrame";
            case PatchSite_BattleRuntime:  return "BattleRuntime";
            default:                       return "site";
            }
        }

        static void DevOsdPatchApplied(EmuInstance* emu, uint8_t siteMask, uint32_t count) noexcept
        {
            if (!emu || count == 0)
                return;
            emu->osdAddMessage(
                0,
                "Patches: applied (%s, %u entries)",
                DevPatchApplySiteLabel(siteMask),
                count);
        }

        static void DevOsdPatchRestored(EmuInstance* emu, const char* reason, uint32_t count) noexcept
        {
            if (!emu || count == 0)
                return;
            emu->osdAddMessage(0, "Patches: restored (%s, %u entries)", reason, count);
        }
#endif

    } // namespace

    void Patches_Apply(uint8_t siteMask, const PatchCtx& ctx)
    {
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (siteMask & PatchSite_OutOfGameFrame)
            MelonPrimePerf::CountOutOfGamePatchApply();
#endif
        uint32_t applied = 0;
        for (const PatchEntry& entry : kPatchRegistry) {
            if (entry.applySites & siteMask) {
                entry.apply(ctx);
                ++applied;
            }
        }
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (siteMask != PatchSite_OutOfGameFrame)
            DevOsdPatchApplied(ctx.emu, siteMask, applied);
#endif
    }

    void Patches_RestoreOnLeave(const PatchCtx& ctx)
    {
        uint32_t restored = 0;
        for (const PatchEntry& entry : kPatchRegistry) {
            if ((entry.restoreFlags & RF_OnLeave) && entry.restore) {
                entry.restore(ctx);
                ++restored;
            }
        }
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        DevOsdPatchRestored(ctx.emu, "match end", restored);
#endif
    }

    void Patches_RestoreOnStop(const PatchCtx& ctx)
    {
        uint32_t restored = 0;
        for (const PatchEntry& entry : kPatchRegistry) {
            if ((entry.restoreFlags & RF_OnStop) && entry.restore) {
                entry.restore(ctx);
                ++restored;
            }
        }
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        DevOsdPatchRestored(ctx.emu, "emu stop", restored);
#endif
    }

    void Patches_ResetAll(MelonPrimePatchState& state)
    {
        for (const PatchEntry& entry : kPatchRegistry) {
            if (entry.resetState)
                entry.resetState(state);
        }
    }

} // namespace MelonPrime

#endif // MELONPRIME_DS
