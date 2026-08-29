#ifndef MELONPRIME_H
#define MELONPRIME_H

#include <functional>
#include <vector>
#include <cstdint>
#include <array>
#include <string_view>
#include <memory>
#include <cmath>
#include <cstring>
#include <atomic>

class QPoint;
class ScreenPanel;  // P-3: forward decl for cached panel pointer

#include "types.h"
#include "Config.h"
#include "MelonPrimeCompilerHints.h"  // Centralised macros (was inline here)
#include "MelonPrimePlatformInput.h"
#include "MelonPrimeInputSubscription.h"
#include "MelonPrimeThreadBridge.h"
#include "MelonPrimeRuntimeConfig.h"
#include "MelonPrimeGameSettings.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeBattleFlowState.h"
#include "MelonPrimeZoomState.h"
#ifdef MELONPRIME_DS
#include "MelonPrimeArm9Hook.h"
#include "MelonPrimePatchShadowFreezeRuntimeHook.h"
#include "MelonPrimePatchState.h"
#endif

class EmuInstance;
namespace melonDS { class NDS; }

namespace MelonPrime {

#ifdef MELONPRIME_DS
    struct MelonPrimeArm9HookState {
        struct DispatchEntry {
            uint32_t address = 0;
            uint16_t mask = 0;
        };

        static constexpr uint32_t Capacity = 32;
        std::array<DispatchEntry, Capacity> entries{};
        uint32_t count = 0;
        uint32_t lastAddress = 0;
        uint16_t lastMask = 0;
        uint8_t romGroupIndex = 0xFFu;
    };
#endif

#ifdef MELONPRIME_CUSTOM_HUD
    struct CustomHudConfigState;
#endif

#ifdef _WIN32
    class RawInputWinFilter;
    struct RawInputSubscription;
#endif

    enum InputCacheBit : uint64_t {
        IB_JUMP = 1ULL << 0,
        IB_SHOOT = 1ULL << 1,
        IB_ZOOM = 1ULL << 2,
        IB_MORPH = 1ULL << 3,
        IB_MORPH_BOOST = 1ULL << 4,
        IB_WEAPON_CHECK = 1ULL << 5,
        IB_MOVE_F = 1ULL << 6,
        IB_MOVE_B = 1ULL << 7,
        IB_MOVE_L = 1ULL << 8,
        IB_MOVE_R = 1ULL << 9,
        IB_MENU = 1ULL << 10,
        IB_SCAN_VISOR = 1ULL << 11,
        IB_UI_OK = 1ULL << 12,
        IB_UI_LEFT = 1ULL << 13,
        IB_UI_RIGHT = 1ULL << 14,
        IB_UI_YES = 1ULL << 15,
        IB_UI_NO = 1ULL << 16,
        IB_WEAPON_BEAM = 1ULL << 17,
        IB_WEAPON_MISSILE = 1ULL << 18,
        IB_WEAPON_1 = 1ULL << 19,
        IB_WEAPON_2 = 1ULL << 20,
        IB_WEAPON_3 = 1ULL << 21,
        IB_WEAPON_4 = 1ULL << 22,
        IB_WEAPON_5 = 1ULL << 23,
        IB_WEAPON_6 = 1ULL << 24,
        IB_WEAPON_SPECIAL = 1ULL << 25,
        IB_WEAPON_NEXT = 1ULL << 26,
        IB_WEAPON_PREV = 1ULL << 27,

        IB_MOVE_MASK = IB_MOVE_F | IB_MOVE_B | IB_MOVE_L | IB_MOVE_R,
        IB_WEAPON_ANY = IB_WEAPON_BEAM | IB_WEAPON_MISSILE |
        IB_WEAPON_1 | IB_WEAPON_2 | IB_WEAPON_3 |
        IB_WEAPON_4 | IB_WEAPON_5 | IB_WEAPON_6 |
        IB_WEAPON_SPECIAL,
        IB_UI_ANY = IB_UI_OK | IB_UI_LEFT | IB_UI_RIGHT | IB_UI_YES | IB_UI_NO,
    };

    struct alignas(64) FrameInputState {
        uint64_t down;
        uint64_t press;
        int32_t  mouseX;
        int32_t  mouseY;
        int32_t  wheelDelta;
        uint32_t moveIndex;
        uint32_t _pad[2];
    };
    static_assert(sizeof(FrameInputState) == 64);

    struct alignas(64) GameAddressesHot {
        melonDS::u32 isAltForm;
        melonDS::u32 jumpFlag;
        melonDS::u32 weaponChange;
        melonDS::u32 selectedWeapon;
        melonDS::u32 currentWeapon;
        melonDS::u32 aimX;
        melonDS::u32 aimY;
        melonDS::u32 havingWeapons;
        melonDS::u32 weaponAmmo;
        melonDS::u32 boostGauge;
        melonDS::u32 isBoosting;
        melonDS::u32 loadedSpecialWeapon;
        melonDS::u32 isInVisorOrMap;
        melonDS::u32 isMapOrUserActionPaused;
        melonDS::u32 inGame;
        melonDS::u32 currentMode;
        melonDS::u32 battleFlowState;
        melonDS::u32 chosenHunter;
        melonDS::u32 inGameSensi;
    };

    // =========================================================================
    // HotPointers: RAM pointers resolved once per game-join.
    // Tiered by access frequency for optimal cache-line packing.
    // =========================================================================
    struct alignas(64) HotPointers {
        // [Tier 0: Every Frame (Always) - frame gate check]
        uint16_t* inGame;

        // [Tier 1: Every Frame (Always)]
        // aimX/aimY are the touch-aim producer's newest history slot
        // (InputSlot+0x3E/+0x46). On a Dual control preset the ROM never reads
        // that chain, and dualAim/aimSens are the pair it does read.
        uint16_t* aimX;
        uint16_t* aimY;
        // Dual control presets only; null otherwise, which is also what selects
        // the write target in WriteAimDelta().
        //   dualAim[0] = player+0xE4 yaw, dualAim[1] = player+0xE8 pitch
        //   aimSens[0] = player+0x3F8,    aimSens[1] = player+0x3FC
        int32_t* dualAim;
        const int32_t* aimSens;
        uint8_t* isAltForm;
        uint8_t* jumpFlag;
        uint32_t* weaponAmmo;

        // [Tier 2: Every Frame (Often)]
        uint16_t* havingWeapons;
        uint8_t* currentWeapon;
        uint8_t* weaponChange;

        // [Tier 3: Conditional / Rare]
        // The match black-window state machine reads its own inputs through
        // MatchTransitionPtrs, not through this struct.
        uint8_t* currentMode;       // isEndOfGame poll (only while in-game init, pre-restore)
        uint8_t* battleFlowState;
        uint8_t* selectedWeapon;
        uint8_t* loadedSpecialWeapon;
        uint8_t* boostGauge;
        uint8_t* isBoosting;
        uint8_t* isInVisorOrMap;
        uint8_t* isMapOrUserActionPaused;

        // [Tier 4: Damage Notify Purple — local-player HP watcher / DD timer write]
        uint16_t* health;
        uint16_t* doubleDamageTimer;
        // Weavel proxy: only read when BIT_IS_WEAVEL is set. When the alt-form
        // proxy is active, observed HP = mainHp + proxyHp (avoids false notify
        // on transform HP-split).
        uint32_t* flags1;          // CPlayer +0x4C4, bit26 = Boosting, bit27 = CanTouchBoost
        int16_t*  altSteerDelta;   // input (CPlayer +0x464) +0x2A/+0x2C, alt-form steer delta X/Y
        uint32_t* moreFlags;       // CPlayer +0x4C8, bit5 = proxy active
        uint32_t* weavelProxyPtr;  // CPlayer +0xF24, ARM9 pointer to proxy entity
    };

    enum AimBlockBit : uint32_t {
        AIMBLK_CHECK_WEAPON = 1u << 0,
        AIMBLK_MORPHBALL_BOOST = 1u << 1,
        AIMBLK_CURSOR_MODE = 1u << 2,
        AIMBLK_NOT_IN_GAME = 1u << 3,
    };


#ifdef _WIN32
    struct FilterDeleter {
        void operator()(RawInputWinFilter* ptr);
    };
#endif

    // =========================================================================
    // MelonPrimeCore
    //
    // Public surface: lifecycle control (Initialize/RunFrameHook/OnEmu*),
    // ARM9 instruction-hook module contracts (MELONPRIME_DS), narrow
    // accessors, and the narrow setters EmuThread uses for runtime state
    // it owns (SetFastForwardState). Emulation runtime flags themselves
    // are private: GUI reads take MelonPrimeUiSnapshot through
    // ThreadBridge(), and GUI writes go through its mailbox.
    //
    // Private surface: cache-optimized member layout (see the banner at
    // "Cache-optimized member layout" below), inline hot-path helpers, and
    // the RunFrameHook call graph (hot methods, then COLD_FUNCTION-outlined
    // rare-path handlers). Member declaration order and cache-line grouping
    // are load-bearing (do-not-touch list, melonprime-refactoring.md V1
    // section 2.1) -- add new members at the end of their cache tier, never
    // reorder existing ones.
    // =========================================================================
    class MelonPrimeCore
    {
    public:
        explicit MelonPrimeCore(EmuInstance* instance);
        ~MelonPrimeCore();

        MelonPrimeCore(const MelonPrimeCore&) = delete;
        MelonPrimeCore& operator=(const MelonPrimeCore&) = delete;

        void Initialize();
        HOT_FUNCTION void RunFrameHook();
        void OnEmuStart();
        void OnEmuStop();
        void ResetRuntimeStateForBoot();
        void OnEmuPause();
        void OnEmuUnpause();
        void OnReset();
        void OnSavestateLoaded();
        void NotifyConfigChanged();

        void SetFrameAdvanceFunc(std::function<void()> func);

        [[nodiscard]] FORCE_INLINE bool IsInGame() const { return m_flags.test(StateFlags::BIT_IN_GAME); }
        // True from the frame the pre-match full black lifts until the frame
        // the post-match fade reaches full black. See
        // BattleFlow::UpdateMatchBetweenBlackouts.
        [[nodiscard]] FORCE_INLINE bool IsMatchBetweenBlackoutsActive() const {
            return m_flags.test(StateFlags::BIT_MATCH_BETWEEN_BLACKOUTS);
        }
        // The match window is the definition of "in a match" for the
        // force-software-outside-match feature; the ROM's in-game flag is not
        // consulted. Called every frame by EmuThread's renderer-switch edge
        // check, so it stays inline: one flags load, no call.
        // Only meaningful while IsForceSoftwareOutsideMatchEnabled() is true —
        // the window state machine is frozen (and the bit cleared) while the
        // feature is off.
        [[nodiscard]] FORCE_INLINE bool ShouldForceSoftwareRenderer() const {
            return !IsMatchBetweenBlackoutsActive();
        }
        // Cached "3D.ForceSoftwareOutsideMatch". The match window has exactly
        // one consumer (EmuThread's renderer switch), so while the feature is
        // off nothing reads it and RunFrameHook skips the state machine
        // entirely — that removes its per-frame transitionType MainRAM read.
        [[nodiscard]] FORCE_INLINE bool IsForceSoftwareOutsideMatchEnabled() const {
            return m_forceSoftwareOutsideMatch;
        }
        // Emulation-thread only; pushed by EmuThread whenever video settings
        // are (re)applied, which is where the config value is already read.
        COLD_FUNCTION void SetForceSoftwareOutsideMatchEnabled(bool enabled);
        [[nodiscard]] uint16_t GetInputMaskFast() const { return m_inputMaskFast; }
#if MELONPRIME_PLATFORM_RAW_FILTER_ENABLED
    // True when the platform raw filter owns aim deltas. ScreenPanel uses
    // this for threshold containment warps (fallback uses Qt/panel path).
    [[nodiscard]] bool IsPlatformRawAimActive() const;
#if defined(__APPLE__)
        // True when GCMouse (external mouse) owns raw aim. Internal trackpads
        // use IOHID and must not disassociate the OS cursor from motion.
        [[nodiscard]] bool IsGcMouseAimActive() const;
#endif
#if defined(__linux__)
        [[nodiscard]] bool IsLinuxRawAimActive() const { return IsPlatformRawAimActive(); }
#endif
#endif

#ifdef MELONPRIME_DS
        [[nodiscard]] int GetNativeAimHookMode() const noexcept { return m_nativeAimHookMode; }
        [[nodiscard]] MelonPrimeArm9HookState& Arm9HookState() noexcept
        {
            return m_arm9HookState;
        }
        [[nodiscard]] const Arm9HookActivationPlan& GetArm9HookActivationPlan() const noexcept
        {
            return m_arm9HookActivationPlan;
        }
        [[nodiscard]] MelonPrimePatchState& PatchState() noexcept
        {
            return m_patchState;
        }

        // -----------------------------------------------------------------
        // ARM9 instruction-hook module contracts (Foo_GetAddresses /
        // Foo_DispatchCheck[AndRedirect]). Grouped by domain; signatures are
        // fixed by MelonPrimeArm9Hook.cpp's dispatcher. Registered/dispatched
        // there, implemented in the matching .inc / .cpp module.
        // -----------------------------------------------------------------

        // --- Aim hooks (side-effect: inject mouse delta / sync aim basis) ---
        static uint32_t NativeAimDeltaHookRegisterInjection_GetAddresses(
            uint8_t romGroupIndex,
            uint32_t* out,
            uint32_t maxCount);
        void NativeAimDeltaHookRegisterInjection_DispatchCheck(
            melonDS::NDS* nds,
            uint32_t arm9ExecAddr,
            uint32_t regs[16]);

        static uint32_t NativeAimDeltaHookPostFoldWrite_GetAddresses(
            uint8_t romGroupIndex,
            uint32_t* out,
            uint32_t maxCount);
        void NativeAimDeltaHookPostFoldWrite_DispatchCheck(
            melonDS::NDS* nds,
            uint32_t arm9ExecAddr,
            uint32_t regs[16]);

        static uint32_t LowLatencyAimHook_GetAddresses(
            uint8_t romGroupIndex,
            uint32_t* out,
            uint32_t maxCount);
        void LowLatencyAimHook_DispatchCheck(
            melonDS::NDS* nds,
            uint32_t arm9ExecAddr,
            uint32_t regs[16]);

        // --- Input-overlay hook (side-effect: overlay edges into input struct) ---
        static uint32_t ImmediateInputEdgeOverlay_GetAddresses(
            uint8_t romGroupIndex,
            uint32_t* out,
            uint32_t maxCount);
        void ImmediateInputEdgeOverlay_DispatchCheck(
            melonDS::NDS* nds,
            uint32_t arm9ExecAddr,
            uint32_t regs[16]);

        // --- Fire & zoom hooks (native biped fire overlay / zoom toggle) ---
        // NativeBipedFire registers the same post-poll ActionConsumer PC as the
        // overlay above and contributes only the Fire current/pressed/released
        // bits; the write itself lives in ImmediateInputEdgeOverlay_DispatchCheck.
        static uint32_t NativeBipedFireHook_GetAddresses(
            uint8_t romGroupIndex,
            uint32_t* out,
            uint32_t maxCount);
        [[nodiscard]] uint16_t NativeBipedFire_ResolveOverlayEdges(
            uint16_t fireMask,
            uint16_t& pressedOut,
            uint16_t& releasedOut) const noexcept;
        // Developer-only instrumentation for the Fire overlay. All three are
        // empty in release builds (no diagnostic PC is registered there).
        void NativeBipedFireHook_DiagnosticRecordOverlay(
            const uint8_t* mainRAM,
            uint32_t playerBase,
            uint16_t fireOverlayMask,
            uint16_t prePressed,
            uint16_t postPressed) noexcept;
        bool NativeBipedFireHook_DiagnosticCheck(
            melonDS::NDS* nds,
            uint32_t arm9ExecAddr,
            uint32_t regs[16]);
        void NativeBipedFireHook_DiagnosticReport();

        static uint32_t NativeZoomToggleHook_GetAddresses(
            uint8_t romGroupIndex,
            uint32_t* out,
            uint32_t maxCount);
        bool NativeZoomToggleHook_DispatchCheckAndRedirect(
            melonDS::NDS* nds,
            uint32_t arm9ExecAddr,
            uint32_t regs[16],
            uint32_t& redirectExecAddr);

        // --- Transform & weapon-switch hooks (redirect into native routines) ---
        static uint32_t TransformGateHook_GetAddresses(
            uint8_t romGroupIndex,
            uint32_t* out,
            uint32_t maxCount);
        bool TransformGateHook_DispatchCheckAndRedirect(
            melonDS::NDS* nds,
            uint32_t arm9ExecAddr,
            uint32_t regs[16],
            uint32_t& redirectExecAddr);

        static uint32_t WeaponSwitchHook_GetAddresses(
            uint8_t romGroupIndex,
            uint32_t* out,
            uint32_t maxCount);
        bool WeaponSwitchHook_DispatchCheckAndRedirect(
            melonDS::NDS* nds,
            uint32_t arm9ExecAddr,
            uint32_t regs[16],
            uint32_t& redirectExecAddr);
        [[nodiscard]] static bool WeaponSwitchHook_IsRomSupported(uint8_t romGroupIndex);
        [[nodiscard]] static bool WeaponSwitchHook_IsSiteValid(
            melonDS::NDS* nds,
            uint8_t romGroupIndex);
#endif

#ifdef MELONPRIME_CUSTOM_HUD
        [[nodiscard]] const RomAddresses& GetCurrentRom() const { return m_currentRom; }
        [[nodiscard]] const GameAddressesHot& GetAddrHot() const { return m_addrHot; }
        [[nodiscard]] uint8_t GetPlayerPosition() const { return m_playerPosition; }
        [[nodiscard]] uint8_t GetHunterID() const { return m_hunterID; }
        [[nodiscard]] bool IsRomDetected() const { return m_flags.test(StateFlags::BIT_ROM_DETECTED); }
        [[nodiscard]] bool IsMetroidMenuHeld() const noexcept { return IsDown(IB_MENU); }
        [[nodiscard]] CustomHudConfigState& HudConfigState() noexcept
        {
            return *m_hudConfigState;
        }
#endif

        [[nodiscard]] MelonPrimeThreadBridge& ThreadBridge() noexcept { return m_threadBridge; }
        [[nodiscard]] const MelonPrimeThreadBridge& ThreadBridge() const noexcept { return m_threadBridge; }

        // EmuThread-owned runtime state. GUI consumers use ThreadBridge().
        //
        // Private on purpose: the emulation thread is the only writer.
        //   GUI read    -> MelonPrimeUiSnapshot via ThreadBridge().ReadForGui()
        //   GUI write   -> the existing ThreadBridge mailbox
        //                  (RequestCursorModeFromGui / ConsumeCursorModeForEmu)
        //   config write-> ApplyRuntimeConfigSnapshot, the sole writer of
        //                  screenSyncMode and isStylusMode
        //   EmuThread   -> SetFastForwardState() below, the only narrow setter
        //
        // Declaration order is deliberately untouched -- these five stay exactly
        // where they were in the member layout; only their access changed. Do not
        // answer a new external need with a public runtime-config getter; see
        // docs/architecture/srp-performance-contract.md.
    private:
        bool isCursorMode = true;
        bool isStylusMode = false;
        bool m_snapTapMode = false;     // Cached from BIT_SNAP_TAP; avoids bitmask test in hot path
        bool isFastForward = false;     // Set by EmuThread; Screen Sync skips when true
        int  screenSyncMode = 0;       // 0=Off, 1=glFinish, 2=DwmFlush

    public:
        // EmuThread-only runtime write. Fast-forward/slow-motion is decided on
        // the emulation thread and mirrored into the UI snapshot from there; a
        // plain store, so this is the same codegen as the direct field write it
        // replaces. Never add allocation, logging, config lookup, an atomic, or
        // virtual dispatch here.
        FORCE_INLINE void SetFastForwardState(bool active) noexcept { isFastForward = active; }

        void NotifyLayoutChange();  // P-3: impl in .cpp (needs complete EmuInstance type)

        // P-22: Drain WM_INPUT queue after RunFrame.
        void DeferredDrainInput();

    private:
        // =================================================================
        // Cache-optimized member layout
        //   CL0:  m_input         (R/W every frame, 64B)
        //   CL1+: m_ptrs          (R every frame)
        //   Hot scalars + emuInstance (R/W every frame, same CL cluster)
        //   Warm: fnAdvance, frameAdvanceFunc, rawFilter
        //   Cold: m_addrHot, m_currentRom (init-only, pushed to end)
        // =================================================================

        // --- CL0: Input State (R/W every frame) ---
        alignas(64) FrameInputState m_input{};

        // --- CL1+: RAM Pointers (R every frame) ---
        alignas(64) HotPointers m_ptrs{};

        // --- Hot Scalars + Core Pointers (R/W every frame) ---
        EmuInstance* emuInstance;
        Config::Table& localCfg;
        Config::Table& globalCfg;

        uint16_t m_inputMaskFast = 0xFFFF;
        uint16_t m_snapState = 0;
        uint32_t m_aimBlockBits = 0;

        // =============================================================
        // Fixed-point aim pipeline (Q14 = 14-bit fractional).
        // Pure integer path replaces float: IMUL x2 + SAR x2 vs CVTSI2SS x2 + MULSS x2 + CVTTSS2SI x2
        // =============================================================
        static constexpr int AIM_FRAC_BITS = 14;
        static constexpr int64_t AIM_ONE_FP = 1LL << AIM_FRAC_BITS;

        // P-18: Direct path uses >> 12 instead of >> 14 then << 2.
        // This preserves 2 extra fractional bits, giving 4x resolution (±1 vs ±4).
        static constexpr int AIM_DIRECT_BITS = AIM_FRAC_BITS - 2;  // 12

        // P-18c: Residual clamp. Set to the largest residual whose per-frame
        // output still fits the signed-16-bit aim register. The direct path emits
        // resX >> AIM_DIRECT_BITS, so the ceiling is INT16_MAX << AIM_DIRECT_BITS.
        // This is the maximally-permissive safe bound:
        //   * It no longer clips real flicks. Below this ceiling the residual
        //     accumulation is linear, so a fast flick and a slow flick of the
        //     same physical distance emit the same total motion. The old
        //     128-unit clamp discarded the excess on high-sensitivity fast
        //     flicks (residual was clamped, then ~zeroed by the carry subtract,
        //     so the overflow was lost, not carried) — that made fast aiming
        //     register less rotation than slow aiming.
        //   * It still prevents the int16 output from wrapping on a pathological
        //     single-frame delta spike (alt-tab/sensor burst), which would flip
        //     the aim direction. Such a spike is capped here, not spread across
        //     frames.
        static constexpr int64_t AIM_MAX_RESIDUAL = 32767LL << AIM_DIRECT_BITS;

        int32_t  m_aimFixedScaleX = 164;
        int32_t  m_aimFixedScaleY = 218;
        int32_t  m_aimEffectiveFixedScaleX = 164;
        int32_t  m_aimEffectiveFixedScaleY = 218;
        int64_t  m_aimFixedAdjust = 8192;
        int64_t  m_aimFixedSnapThresh = AIM_ONE_FP;

        // P-17: Sub-pixel residual accumulators (Q14 fixed-point).
        // Carry fractional remainder across frames for smooth slow-speed aiming.
        int64_t  m_aimResidualX = 0;
        int64_t  m_aimResidualY = 0;

        // P-30: Hot bools grouped with residuals (same cache line fetch).
        // ProcessAimInputMouse reads these every frame right after residuals.
        bool     m_disableMphAimSmoothing = false;
        bool     m_enableAimAccumulator = false;
        bool     m_enableZoomAimScale = false;
        uint32_t m_zoomAimScaleQ14 = static_cast<uint32_t>(AIM_ONE_FP);
        uint32_t m_activeZoomAimScaleQ14 = static_cast<uint32_t>(AIM_ONE_FP);
        bool     m_enableNativeAimDeltaHook = false; // true when mode != 0
        int8_t   m_nativeAimHookMode = 0;  // 0=off 1=RegisterInject 2=FoldDerived
        int8_t   m_lowLatencyAimMode = 0;  // 0=off 1=ImmediateSync 2=MoonLikeAim 3=legacy FpsCameraLock(dev-only)
        int32_t  m_moonLikeAimNormalStepQ12 = 0x0165;
        int32_t  m_moonLikeAimFastStepQ12 = 0x058F;
        int32_t  m_moonLikeAimFastThresholdQ12 = 0x042E;
        bool     m_enableImmediateInputEdgeOverlay = false;
        bool     m_enableDirectAltFormTransform = false;
        bool     m_enableNativeBipedFire = false;
        bool     m_enableNativeZoomToggle = false;
#ifdef MELONPRIME_DS
        bool     m_enableNativeWeaponSwitch = false;
#endif
        int16_t  m_nativeAimDeltaX = 0;
        int16_t  m_nativeAimDeltaY = 0;
        // ImmediateInputEdgeOverlay edge state, tracked per ACTION rather than
        // per binding bit. Like the Fire latch below, it is resolved once per
        // frame (UpdateImmediateInputEdgeOverlayInput) because the ROM action
        // consumer this feeds runs once per player per frame — recomputing the
        // edge on each hook entry erases the Pressed bit before the local
        // player's own entry reads it.
        //
        // Action-level (not bit-level) tracking keeps the edge independent of
        // the binding masks and of m_immediateOverlayPreserveMask, both of
        // which are only final later in the frame; the hook expands the
        // resolved actions onto the masks it sees.
        uint8_t  m_immediateOverlayPrevActions = 0;
        uint8_t  m_immediateOverlayFrameHeld = 0;
        uint8_t  m_immediateOverlayFramePressed = 0;
        uint8_t  m_immediateOverlayFrameReleased = 0;
        bool     m_immediateOverlayLatchValid = false;
        // Last observed *LIST_HookLocalPlayerPtrGlobal. A change means the ROM
        // handed us a different local Player*, so both edge latches must
        // re-baseline instead of emitting an edge against the old entity's
        // state (stale-edge policy).
        uint32_t m_overlayLocalPlayerPtr = 0;
        uint16_t m_immediateOverlayPreserveMask = 0;
        // ---------------------------------------------------------------
        // Control-preset button snapshot.
        //
        // MPH keeps the local player's control assignments at player+0x364 as
        // {uint16 Button; uint16 PressFlags} entries; only the Button half is a
        // DS KEYINPUT mask. The four presets bind the same action to different
        // buttons, so anything MelonPrime synthesizes has to come from here --
        // a hardcoded button only ever matched Touch R:
        //
        //            move        jump           fire  zoom    boost
        //   Touch R  D-pad       A|B|X|Y        L     R       R
        //   Touch L  Y/A/X/B     D-pad          R     L       L
        //   Dual R   D-pad       R              L     Select  R
        //   Dual L   Y/A/X/B     L              R     Select  L
        //
        // Taken once per game join, so the per-frame path only reads it. The
        // defaults are the historical Touch R hardcodes, which keeps the
        // out-of-game synthesis (Adventure map) working before the player
        // struct is readable.
        struct PresetButtonBindings {
            // DS KEYINPUT bit masks. Declared here rather than reused from the
            // INPUT_* enum because that enum lives in MelonPrimeInternal.h,
            // which sits above this header; MelonPrimeGameInput.cpp sees both
            // and static_asserts that they agree. MPH's own ButtonFlags uses
            // this same layout, which is why a preset word can be ANDed
            // straight against a KEYINPUT mask.
            static constexpr uint16_t BtnA     = 0x0001;
            static constexpr uint16_t BtnB     = 0x0002;
            static constexpr uint16_t BtnRight = 0x0010;
            static constexpr uint16_t BtnLeft  = 0x0020;
            static constexpr uint16_t BtnUp    = 0x0040;
            static constexpr uint16_t BtnDown  = 0x0080;
            static constexpr uint16_t BtnR     = 0x0100;
            static constexpr uint16_t BtnL     = 0x0200;

            // DS mask to press for each 4-bit move index (F,B,L,R), opposite
            // pairs cancelled. Precomputed so the hot path stays one table read.
            uint16_t MoveMask[16] = {
                0x0000, 0x0040, 0x0080, 0x0000,
                0x0020, 0x0060, 0x00A0, 0x0020,
                0x0010, 0x0050, 0x0090, 0x0010,
                0x0000, 0x0040, 0x0080, 0x0000,
            };
            uint16_t MoveL = BtnLeft;
            uint16_t MoveR = BtnRight;
            uint16_t MoveF = BtnUp;
            uint16_t MoveB = BtnDown;
            uint16_t MoveAll = BtnLeft | BtnRight | BtnUp | BtnDown;
            uint16_t Fire = BtnL;
            uint16_t Jump = BtnB;
            uint16_t Zoom = BtnR;
            uint16_t MorphBoost = BtnR;

            // Left-handed touch layout. The in-match HUD rectangles are the
            // same table for every preset; the ROM just mirrors the X centre
            // (GetTouchRegionCenter / TouchRegionHit do `centerX = 256 - centerX`
            // when the flag is set), so any touch point MelonPrime synthesizes
            // has to be mirrored the same way. The ROM's own runtime layout
            // check is `record[0x00] & 0x200`, which is what this mirrors:
            // Touch R 0x0076 / Dual R 0x007C are normal, Touch L 0x0276 /
            // Dual L 0x027C are mirrored.
            bool MirrorTouchX = false;

            // Touch-style aim path. The ROM's biped and alt-form aim dispatchers
            // both branch on `record[0x00] & 0x2`: set means the touch producer
            // feeds aim (InputSlot+0x2A/+0x2C), clear means the Dual digital
            // accumulator does (player+0xE4/+0xE8). Touch R 0x0076 and Touch L
            // 0x0276 have the bit; Dual R 0x007C and Dual L 0x027C do not.
            bool UsesTouchAim = true;

            // Reduce a binding to a single button. The ROM only tests
            // `binding & field`, so one bit is enough, and pressing the whole
            // mask would press buttons the preset binds to other actions too
            // (Touch R jump is A|B|X|Y). Keeping the historical button when the
            // binding contains it makes Touch R bit-for-bit unchanged.
            [[nodiscard]] static FORCE_INLINE uint16_t PickButton(
                uint16_t binding, uint16_t preferred) noexcept
            {
                if (binding == 0)
                    return preferred;
                if (binding & preferred)
                    return preferred;
                return static_cast<uint16_t>(binding & (~binding + 1u));
            }

            // Mirror a touch X coordinate written for the normal (right-handed)
            // HUD layout onto the layout this preset actually uses.
            [[nodiscard]] FORCE_INLINE int MirrorX(int x) const noexcept
            {
                return MirrorTouchX ? (256 - x) : x;
            }

            // Byte offsets inside a 0x9C control record. Kept here rather than
            // at the call site so the record layout has exactly one owner; they
            // are the player-struct offsets minus 0x364, because player init
            // copies this same record to player+0x364.
            enum RecordOffset : uint32_t {
                Off_ControlMode = 0x00,
                Off_MoveLeft    = 0x04,
                Off_MoveRight   = 0x08,
                Off_MoveUp      = 0x0C,
                Off_MoveDown    = 0x10,
                Off_Fire        = 0x34,
                Off_Jump        = 0x38,
                Off_MorphBoost  = 0x50,
                Off_Zoom        = 0x7C,
                RecordSize      = 0x9C,
            };

            // Derive the snapshot from a control record in main RAM. `recordBase`
            // is either ControlPresetTable[id] or the copy at player+0x364.
            // An unreadable field reads as 0 and PickButton() then keeps the
            // Touch R default for that action.
            void BuildFromRecord(const uint8_t* mainRAM, uint32_t recordBase) noexcept
            {
                const auto read16 = [mainRAM, recordBase](uint32_t off) -> uint16_t {
                    const uint32_t addr = recordBase + off;
                    if (!mainRAM || addr < 0x02000000u || addr > 0x023FFFFEu)
                        return 0;
                    uint16_t v = 0;
                    std::memcpy(&v, mainRAM + (addr & 0x3FFFFFu), sizeof(v));
                    return v;
                };

                const uint16_t mode = read16(Off_ControlMode);
                MirrorTouchX = (mode & 0x0200u) != 0;
                UsesTouchAim = (mode & 0x0002u) != 0;
                MoveL = PickButton(read16(Off_MoveLeft), BtnLeft);
                MoveR = PickButton(read16(Off_MoveRight), BtnRight);
                MoveF = PickButton(read16(Off_MoveUp), BtnUp);
                MoveB = PickButton(read16(Off_MoveDown), BtnDown);
                MoveAll = static_cast<uint16_t>(MoveL | MoveR | MoveF | MoveB);
                Fire = PickButton(read16(Off_Fire), BtnL);
                Jump = PickButton(read16(Off_Jump), BtnB);
                Zoom = PickButton(read16(Off_Zoom), BtnR);
                MorphBoost = PickButton(read16(Off_MorphBoost), BtnR);

                // Index bits are Forward/Back/Left/Right; a held opposite pair
                // cancels, matching the D-pad table this replaces.
                for (uint32_t i = 0; i < 16; ++i) {
                    const bool f = (i & 1u) != 0;
                    const bool b = (i & 2u) != 0;
                    const bool l = (i & 4u) != 0;
                    const bool r = (i & 8u) != 0;
                    uint16_t m = 0;
                    if (f != b) m = static_cast<uint16_t>(m | (f ? MoveF : MoveB));
                    if (l != r) m = static_cast<uint16_t>(m | (l ? MoveL : MoveR));
                    MoveMask[i] = m;
                }
            }
        } m_presetBindings{};
        uint8_t  m_directTransformPendingFrames = 0;
        // Native Biped Fire host-side edge latch (Prev*/LatchValid) plus the
        // per-frame resolved input the overlay applies (Frame*).
        //
        // The ROM action consumer runs once per player per frame, so the hook
        // is entered up to four times with only one of those entries belonging
        // to the local player. The edge must therefore be resolved exactly once
        // on the frame path and then applied idempotently at every entry —
        // resolving it inside the hook lets an earlier player's entry consume
        // the Pressed edge before the local player's entry reads it.
        //
        // LatchValid=false means the next frame re-baselines from the current
        // host state instead of emitting an edge, so resuming with Shoot
        // already held cannot inject a stale Pressed.
        // See MelonPrimePatchNativeBipedFireHook.inc.
        bool     m_nativeBipedFirePrevHeld = false;
        bool     m_nativeBipedFirePrevAltForm = false;
        bool     m_nativeBipedFireLatchValid = false;
        bool     m_nativeBipedFireFrameHeld = false;
        bool     m_nativeBipedFireFramePressed = false;
        bool     m_nativeBipedFireFrameReleased = false;
        bool     m_nativeZoomTogglePrevDown = false;
        // Cached zoom-enabled state, updated whenever we read it.
        // While zoom is known disabled (the common steady state) the per-frame
        // auto-disable path can skip the IsZoomEnabled MainRAM read entirely:
        // the game only flips zoom from disabled→enabled in response to a
        // player edge press, which we observe directly via pressedEdge.
        bool     m_nativeZoomLastKnownEnabled = false;
#ifdef MELONPRIME_DS
        struct NativeZoomPendingCall {
            uint32_t FunctionAddr = 0;
            uint32_t Player = 0;
            uint8_t Enabled = 0;
            bool Pending = false;

            FORCE_INLINE void Clear() noexcept
            {
                FunctionAddr = 0;
                Player = 0;
                Enabled = 0;
                Pending = false;
            }
        } m_nativeZoomPending{};

        struct WeaponSwitchPendingRequest {
            uint8_t WeaponId = 0xFF;
            uint8_t RetryCount = 0;
            uint8_t FallbackFrames = 0;

            FORCE_INLINE void Clear() noexcept
            {
                WeaponId = 0xFF;
                RetryCount = 0;
                FallbackFrames = 0;
            }

            [[nodiscard]] FORCE_INLINE bool IsValid() const noexcept
            {
                return WeaponId <= 8 && RetryCount != 0;
            }
        } m_weaponSwitchPending{};
#endif

        // Warm scalars (checked per frame but not in aim hot path)
        bool     m_isRunningHook = false;
        bool     m_isWeaponCheckActive = false;
        bool     m_isLayoutChangePending = true;
        std::atomic_bool m_configReloadPending{ false };
        // P-47: Set by FrameAdvanceOnce; cleared after PollAndSnapshot.
        // True  → LateLatch must call processRawInputBatched (events may have
        //          arrived during the FrameAdvance window: ~32–96 ms).
        // False → PollAndSnapshot was just called; kernel buffer is still
        //          empty on a normal frame (~40–100 ns window). Skip the
        //          GetRawInputBuffer syscall entirely (~500–2000 cyc saved).
        bool     m_didFrameAdvanceSinceSnapshot = false;
        // True when the V-default ScanShoot key (HK_MetroidScanShoot) is held this
        // frame. Used to keep the shoot/scan/map-expand input working during the
        // Adventure map/user-action pause while the Mouse-Left ShootScan key stays
        // touch-only (a left click must not fire there). Set in UpdateInputStateImpl.
        bool     m_scanShootKeyDown = false;
        // Loaded only at the cold config boundary. Vulkan reads this scalar
        // before every frame so an F8 load cannot expose one unmasked frame
        // while the ARM patch tracker is still being re-established.
        bool     m_enableMorphBoostSwipe = true; // MELONPRIME_MORPH_BOOST_MODE_CONTROLS_V14
        bool     m_enableMorphBoostCustomRawThreshold = false;
        // MELONPRIME_MORPH_BALL_BOOST_ASSIST_SENSITIVITY_AUDIT_FIX_V6
        // Warm: consumed by HandleMorphBallBoost during active in-game frames.
        // The value is derived on the cold config path and remains per-instance.
        int32_t  m_morphBoostAssistThresholdSq = 0x1FA4;
        uint8_t  m_morphBoostSwipePulseState = 0; // MELONPRIME_MORPH_BOOST_SHIFT_CADENCE_SWIPE_V10
        uint8_t  m_morphBoostSwipePulseElapsedFrames = 0;
        // Current raw movement is read directly from FrameInputState; no
        // delayed duplicate sample is stored. // MELONPRIME_MORPH_BOOST_CURRENT_FRAME_RAW_V13

        // Cold: float intermediates (config change only)
        float    m_aimSensiFactor = 0.01f;
        float    m_aimCombinedY = 0.013333333f;
        float    m_aimAdjust = 0.5f;
        // Phase 5: EmuThread-owned raw aim config used by the sensitivity
        // hotkey. Config::Table remains a cold reload/persistence boundary.
        int      m_runtimeAimSensitivity = 1;
        float    m_runtimeAimYScale = 1.0f;
        MenuGameSettingsSnapshot m_menuGameSettings{};

        // --- Damage Notify Purple ---
        // Briefly drives the local player's Double Damage timer (CPlayer +0x4B0) to
        // 10 frames whenever the player's *effective* HP drops, producing a short
        // purple flash so opponents can see "this player just got hit". Intended
        // to be paired with the existing Disable Double Damage Multiplier feature
        // so the flash does not become a real 2x boost.
        //
        // Weavel-aware: when the alt-form proxy is active (CPlayer +0x4C8 bit5),
        // observed HP = mainHp + proxyHp so the transform-time HP split (100 →
        // 50/50) does not produce a false notification.
        // proxyActive attach AND detach edges are baseline-only frames so
        // mainHp ↔ mainHp+proxyHp metric switches (e.g. 199 → 299 on attach,
        // 299 → 199 on detach) never look like damage. See spec:
        //   29-Damage-Notify-Purple-NonHook-AI-Implementation-Instructions-v4
        struct DamageNotifyPurpleState {
            uint16_t previousObservedHp = 0;
            uint16_t previousMainHp = 0;
            uint16_t previousProxyHp = 0;
            bool     initialized = false;
            bool     previousProxyActive = false;
        } m_damageNotifyPurpleState{};
        bool m_damageNotifyPurpleEnabled = false;

        struct alignas(4) StateFlags {
            uint32_t packed = 0;
            static constexpr uint32_t BIT_ROM_DETECTED = 1u << 0;
            static constexpr uint32_t BIT_IN_GAME = 1u << 1;
            static constexpr uint32_t BIT_IN_GAME_INIT = 1u << 2;
            static constexpr uint32_t BIT_END_OF_GAME_PATCH_RESTORED = 1u << 12;
            // Latched on first mode==0x0E && flow==0 after join; battle patches/hooks apply here.
            static constexpr uint32_t BIT_BATTLE_RUNTIME_MODE = 1u << 15;
            static constexpr uint32_t BIT_PAUSED = 1u << 3;
            static constexpr uint32_t BIT_IN_ADVENTURE = 1u << 4;
            static constexpr uint32_t BIT_WAS_IN_GAME_RENDERER = 1u << 5;
            static constexpr uint32_t BIT_IS_SAMUS = 1u << 6;
            static constexpr uint32_t BIT_IS_WEAVEL = 1u << 7;
            static constexpr uint32_t BIT_IS_ALT_FORM = 1u << 8;
            static constexpr uint32_t BIT_SNAP_TAP = 1u << 9;
            static constexpr uint32_t BIT_JOY2KEY = 1u << 10;
            static constexpr uint32_t BIT_STYLUS_MODE = 1u << 11;
            static constexpr uint32_t BIT_LAST_FOCUSED = 1u << 13;
            static constexpr uint32_t BIT_BLOCK_STYLUS = 1u << 14;
            // Between the pre-match and post-match full blacks; see
            // IsMatchBetweenBlackoutsActive().
            static constexpr uint32_t BIT_MATCH_BETWEEN_BLACKOUTS = 1u << 16;

            FORCE_INLINE void set(uint32_t bit) { packed |= bit; }
            FORCE_INLINE void clear(uint32_t bit) { packed &= ~bit; }
            FORCE_INLINE void assign(uint32_t bit, bool val) { packed = (packed & ~bit) | (val ? bit : 0u); }
            [[nodiscard]] FORCE_INLINE bool test(uint32_t bit) const { return (packed & bit) != 0; }
        } m_flags{};

        // Drives BIT_MATCH_BETWEEN_BLACKOUTS. Reset with the flags on every
        // ROM / session / instance lifecycle transition; the pointers are
        // resolved once per ROM detect.
        BattleFlow::MatchBlackWindowState m_matchBlackWindow{};
        BattleFlow::MatchTransitionPtrs m_matchTransitionPtrs{};
        // Host-side epoch for successful savestate loads. The pending bit is
        // consumed at the next normal RunFrameHook before loaded RAM is read.
        uint64_t m_timelineGeneration = 0;
        bool m_postSavestateReconcilePending = false;
        // Gate for the two above. Written only by
        // SetForceSoftwareOutsideMatchEnabled (emulation thread, cold).
        bool m_forceSoftwareOutsideMatch = false;

        // --- Warm: Frame advance + platform ---
        std::function<void()> m_frameAdvanceFunc;
        using AdvanceMethod = void (MelonPrimeCore::*)();
        AdvanceMethod m_fnAdvance = &MelonPrimeCore::FrameAdvanceDefault;

#ifdef _WIN32
        std::unique_ptr<RawInputWinFilter, FilterDeleter> m_rawFilter;
        RawInputSubscription* m_rawInputSubscription = nullptr;
        void* m_cachedHwnd = nullptr;
#endif

        struct AimData {
            int centerX = 0;
            int centerY = 0;
        } m_aimData;

        // --- Cold: Init-only data (pushed to end) ---
        GameAddressesHot m_addrHot{};
        RomAddresses m_currentRom{};
        melonDS::u8  m_playerPosition = 0;
        uint8_t      m_hunterID = 0;

        // =================================================================
        // Transient input-state reset cluster (Phase 4-1)
        //
        // Six lifecycle sites (OnEmuStart / ResetRuntimeStateForBoot /
        // OnEmuStop / RunFrameHook focus-loss / RunFrameHook game-leave /
        // HandleGameJoinInit) each clear an overlapping-but-different subset
        // of these transient fields. ResetTransientInputState(parts) clears
        // exactly the requested subset so each site keeps its historical
        // behavior verbatim; the bitmask just removes the copy-paste.
        //
        // NOTE: TR_AimResiduals also zeroes m_nativeAimDeltaX/Y — those two
        // pairs always travelled together at every site that touched them.
        // TR_WeaponSwitchPending is MELONPRIME_DS-only (the field is too).
        // =================================================================
        // Overlay-managed actions, in the order the hook expands them onto the
        // player's binding masks. Host-input space, so it stays valid whatever
        // the control preset binds each action to.
        enum OverlayAction : uint8_t {
            OVA_MOVE_L = 1u << 0,
            OVA_MOVE_R = 1u << 1,
            OVA_MOVE_F = 1u << 2,
            OVA_MOVE_B = 1u << 3,
            OVA_FIRE   = 1u << 4,
            OVA_JUMP   = 1u << 5,
            OVA_ZOOM   = 1u << 6,
        };

        enum TransientReset : uint8_t {
            TR_AimResiduals      = 1u << 0,  // m_aimResidualX/Y + m_nativeAimDeltaX/Y
            TR_OverlayHeld       = 1u << 1,  // immediate-overlay action edge state
            TR_DirectTransform   = 1u << 2,  // m_directTransformPendingFrames
            TR_BipedFire         = 1u << 3,  // native biped-fire edge latch
            TR_WeaponSwitchPending = 1u << 4, // m_weaponSwitchPending (DS only)
        };
        FORCE_INLINE void ResetTransientInputState(uint8_t parts) noexcept {
            if (parts & TR_AimResiduals) {
                m_aimResidualX = 0;
                m_aimResidualY = 0;
                m_nativeAimDeltaX = 0;
                m_nativeAimDeltaY = 0;
            }
            if (parts & TR_OverlayHeld) {
                m_immediateOverlayPrevActions = 0;
                m_immediateOverlayFrameHeld = 0;
                m_immediateOverlayFramePressed = 0;
                m_immediateOverlayFrameReleased = 0;
                m_immediateOverlayLatchValid = false;
                m_overlayLocalPlayerPtr = 0;
            }
            if (parts & TR_DirectTransform)
                m_directTransformPendingFrames = 0;
            if (parts & TR_BipedFire) {
                m_nativeBipedFirePrevHeld = false;
                m_nativeBipedFirePrevAltForm = false;
                m_nativeBipedFireLatchValid = false;
                m_nativeBipedFireFrameHeld = false;
                m_nativeBipedFireFramePressed = false;
                m_nativeBipedFireFrameReleased = false;
                m_overlayLocalPlayerPtr = 0;
            }
#ifdef MELONPRIME_DS
            if (parts & TR_WeaponSwitchPending)
                m_weaponSwitchPending.Clear();
#endif
        }

#if MELONPRIME_PLATFORM_RAW_FILTER_ENABLED
    // Non-Windows raw mouse input. Cold-section member per the
    // MelonPrime.h layout rule; guarded so Windows layout is untouched.
    // Owned via PlatformInput_AcquireRawFilter/ReleaseRawFilter.
        PlatformRawFilter* m_platformRawFilter = nullptr;
        // Edge detect panel→raw transition for stale panel delta discard (V5 W2).
        uint8_t m_platformRawAimWasActive = 0;
#endif

        MelonPrimeInputSubscription m_inputSubscription{};
        MelonPrimeThreadBridge m_threadBridge{};
        uint64_t m_layoutGenerationSeen = 0;

        ZoomStatus::ZoomCapabilityCache m_zoomAimCanZoomCache{};
        // Native Biped Fire developer diagnostics. Sampled during the frame
        // by the overlay and the fire-edge diagnostic PC, reported once per
        // trigger pull on the following frame. Unconditionally present so the
        // class layout does not depend on the developer-features switch; only
        // written when that switch is on.
        uint16_t m_nbfDiagOverlayHits = 0;
        uint16_t m_nbfDiagFireUpdateHits = 0;
        uint16_t m_nbfDiagFireMask = 0;
        uint16_t m_nbfDiagLiveBinding = 0;
        uint16_t m_nbfDiagPrePressed = 0;
        uint16_t m_nbfDiagPostPressed = 0;
        uint16_t m_nbfDiagSeenPressed = 0;
        uint8_t  m_nbfDiagHelperResult = 0;
        bool     m_nbfDiagArmed = false;

#ifdef MELONPRIME_DS
        MelonPrimeArm9HookState m_arm9HookState{};
        Arm9HookActivationPlan m_arm9HookActivationPlan{};
        MelonPrimePatchState m_patchState{};
#endif
#ifdef MELONPRIME_CUSTOM_HUD
        std::shared_ptr<CustomHudConfigState> m_hudConfigState;
#endif

        // =================================================================
        // Inline helpers
        // =================================================================
        // InputReset: clears DS input mask for fresh per-frame rebuild.
        // Called every frame at the top of RunFrameHook.
        //
        // NOTE: m_aimResidualX/Y must NOT be zeroed here!
        // Residuals accumulate across frames (P-17/P-18). Zeroing them here
        // would defeat sub-pixel accumulation entirely — every frame would
        // start from 0, making P-17/P-18 non-functional.
        //
        // Residuals are only zeroed on actual state resets:
        //   - OnEmuStart/InputReset → OnEmuStart calls its own reset
        //   - RecalcAimFixedPoint  → sensitivity change invalidates old residuals
        //   - Layout change        → coordinate system reset
        //   - Aim block / focus loss → stale residuals would cause jump
        FORCE_INLINE void InputReset() {
            m_inputMaskFast = 0xFFFF;
            m_immediateOverlayPreserveMask = 0;
        }

        // True when nothing downstream will smooth or deadzone the delta, so the
        // host must emit the finer direct-path values rather than the legacy
        // ones shaped for the DS-side smoothing.
        //
        // Two independent ways to get there: the AimSmoothing patch rewrites the
        // touch producer's history fold, or the active preset is Dual, whose aim
        // field is downstream of that producer and never goes through it at all.
        [[nodiscard]] FORCE_INLINE bool AimBypassesDsSmoothing() const noexcept {
            return m_disableMphAimSmoothing || m_ptrs.dualAim != nullptr;
        }

        // Deliver one frame's aim delta to whichever field the active control
        // preset's aim path actually consumes.
        //
        // Touch presets: the producer sums the four history samples at
        // InputSlot+0x38..0x46 into +0x2A/+0x2C, and the other three are zero
        // while MelonPrime drives aim, so writing the newest slot arrives 1:1.
        // The ROM then multiplies by player+0x3F8/+0x3FC itself.
        //
        // Dual presets: that whole chain is skipped. The aim branch takes
        // player+0xE4/+0xE8 straight to the yaw/pitch updaters, already carrying
        // the same sensitivity product, so it has to be applied here. The ROM
        // rewrites these two fields later in the frame from its own digital
        // accumulator, which is what keeps the preset's D-pad/face-button aim
        // working on the frames MelonPrime has no delta to deliver.
        FORCE_INLINE void WriteAimDelta(int32_t outX, int32_t outY) noexcept {
            if (LIKELY(m_ptrs.dualAim == nullptr)) {
                *m_ptrs.aimX = static_cast<uint16_t>(outX);
                *m_ptrs.aimY = static_cast<uint16_t>(outY);
                return;
            }
            m_ptrs.dualAim[0] = static_cast<int32_t>(
                static_cast<int64_t>(outX) * m_ptrs.aimSens[0]);
            m_ptrs.dualAim[1] = static_cast<int32_t>(
                static_cast<int64_t>(outY) * m_ptrs.aimSens[1]);
        }

        // Mask variant, for buttons that come from the control preset and are
        // therefore only known at runtime.
        FORCE_INLINE void InputSetMaskBranchless(uint16_t mask, bool released) {
            const uint16_t keep = static_cast<uint16_t>(
                mask & (0u - static_cast<uint16_t>(released)));
            m_inputMaskFast = static_cast<uint16_t>((m_inputMaskFast & ~mask) | keep);
        }

        FORCE_INLINE void InputSetBranchless(uint16_t bit, bool released) {
            const uint16_t mask = 1u << bit;
            m_inputMaskFast = (m_inputMaskFast & ~mask) | (static_cast<uint16_t>(released) * mask);
        }

        FORCE_INLINE void ResetMorphBoostSwipePulseState() noexcept { // MELONPRIME_MORPH_BOOST_SHIFT_CADENCE_SWIPE_V10
            m_morphBoostSwipePulseState = 0;
            m_morphBoostSwipePulseElapsedFrames = 0;
        }

        FORCE_INLINE void SetAimBlockBranchless(uint32_t bitMask, bool enable) noexcept {
            m_aimBlockBits = (m_aimBlockBits & ~bitMask) | (enable ? bitMask : 0u);
        }

        [[nodiscard]] FORCE_INLINE bool IsPlayerAltForm() const noexcept {
            return m_ptrs.isAltForm
                && *m_ptrs.isAltForm == 0x02;
        }

        [[nodiscard]] FORCE_INLINE bool IsPlayerTransforming() const noexcept {
            return m_ptrs.jumpFlag
                && ((*m_ptrs.jumpFlag & 0x10) != 0);
        }

        template <typename T>
        [[nodiscard]] FORCE_INLINE T* GetRamPointer(melonDS::u8* ram, melonDS::u32 addr) {
            return reinterpret_cast<T*>(&ram[addr & 0x3FFFFF]);
        }

        [[nodiscard]] FORCE_INLINE bool IsDown(uint64_t bit) const { return (m_input.down & bit) != 0; }
        [[nodiscard]] FORCE_INLINE bool IsPressed(uint64_t bit) const { return (m_input.press & bit) != 0; }
        [[nodiscard]] FORCE_INLINE bool IsAnyPressed(uint64_t mask) const { return (m_input.press & mask) != 0; }

        // =================================================================
        // Methods
        // =================================================================
        HOT_FUNCTION void UpdateInputState(bool focused);
        HOT_FUNCTION void UpdateInputStateReentrant(bool focused);  // re-entrant FrameAdvance path
        template <bool kReentrant> FORCE_INLINE void UpdateInputStateImpl(bool focused);
        HOT_FUNCTION void HandleInGameLogic();
        COLD_FUNCTION void ReloadDamageNotifyPurpleConfig();
        HOT_FUNCTION void DamageNotifyPurpleTick();
        template <bool kInputMaskReset> FORCE_INLINE void ProcessMoveAndButtonsFastImpl();
        HOT_FUNCTION void ProcessMoveAndButtonsFast();
        HOT_FUNCTION void ProcessMoveAndButtonsFastFromReset();
        // Movement-only (D-pad from moveIndex; buttons/aim untouched). Used on
        // out-of-game screens such as the Adventure planet/region map so WASD can
        // navigate without synthesizing fire/jump.
        HOT_FUNCTION void ProcessMovementOnlyFromReset();
        // Single frame-path entry for both post-poll overlays. Resolves the
        // shared local-player baseline once, then the two edge latches.
        HOT_FUNCTION void ApplyPostPollOverlayInput();
        HOT_FUNCTION void UpdateNativeBipedFireInput(bool localPlayerChanged);
        HOT_FUNCTION void UpdateImmediateInputEdgeOverlayInput(bool localPlayerChanged);
        HOT_FUNCTION void ApplyZoomBindingInput();
        HOT_FUNCTION void UpdateNativeZoomToggleInput();
        HOT_FUNCTION void ProcessAimInputMouse();
        HOT_FUNCTION bool ProcessWeaponSwitch();
        HOT_FUNCTION bool HandleMorphBallBoost();

        // --- Cold Path Handlers (Outlined) ---
        COLD_FUNCTION void HandleRareMorph();
        COLD_FUNCTION void HandleRareWeaponSwitch();
        COLD_FUNCTION void HandleRareWeaponCheckStart();
        COLD_FUNCTION void HandleRareWeaponCheckEnd();
        COLD_FUNCTION void HandleAimEarlyReset();  // P-29b
        COLD_FUNCTION void HandleAdventureMode();

        COLD_FUNCTION void HandleGameJoinInit();
        COLD_FUNCTION void HandleBattleRuntimeEnter();
        COLD_FUNCTION void DetectRomAndSetAddresses();
        void ReconcileMenuGameSettings();

        void ApplyRuntimeAimSensitivity(int sensitivity);
        void RecalcAimFixedPoint();
        void RecalcAimEffectiveFixedScale();
        void UpdateZoomAimEffectiveScale();
        FORCE_INLINE void HandleGlobalHotkeys();
        void ProcessAimInputStylus(melonDS::NDS* nds);
        [[nodiscard]] bool SwitchWeapon(uint8_t weaponId);
        [[nodiscard]] bool SwitchWeaponLegacyTouchFallback(uint8_t weaponId);
        [[nodiscard]] bool CanRequestWeaponSwitch(uint8_t weaponId, bool showMessage);
#ifdef MELONPRIME_DS
        void QueueWeaponSwitchRequest(uint8_t weaponId) noexcept;
#endif
        void ShowCursor(bool show);
        void PublishUiSnapshot() noexcept;
        void FrameAdvanceTwice();
        FORCE_INLINE void FrameAdvanceOnce() { m_didFrameAdvanceSinceSnapshot = true; (this->*m_fnAdvance)(); }
        void FrameAdvanceDefault();
        void FrameAdvanceCustom();
        QPoint GetAdjustedCenter();

        void SetupRawInput();
        void ApplyJoy2KeySupportAndQtFilter(bool enable, bool doReset = true);
        void ApplyRuntimeConfigSnapshot(const RuntimeConfigSnapshot& snapshot);
        void ApplyAimConfigSnapshot(const AimConfigSnapshot& snapshot);
        void ReloadAimConfigFromTable(Config::Table& cfg);
        void ReloadConfigFlags();
        COLD_FUNCTION void ApplyConfigReload();
    };

} // namespace MelonPrime

#endif // MELONPRIME_H
