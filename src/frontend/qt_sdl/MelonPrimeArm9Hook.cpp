#ifdef MELONPRIME_DS

#include "MelonPrimeArm9Hook.h"
#include "MelonPrime.h"
#include "MelonPrimeCompilerHints.h"
#include "MelonPrimeDef.h"
#include "EmuInstance.h"
#include "MelonPrimePatchShadowFreezeRuntimeHook.h"
#include "MelonPrimePatchFixNoxusBladePersistence.h"
#include "NDS.h"

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_ARM9_HOOK_DEBUG_LOG)
#include "Platform.h"
#endif

#include <cstdint>

namespace MelonPrime {

namespace {

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_ARM9_HOOK_DEBUG_LOG)
#define MP_ARM9_HOOK_LOG(...) \
    melonDS::Platform::Log(melonDS::Platform::LogLevel::Info, __VA_ARGS__)
#else
#define MP_ARM9_HOOK_LOG(...) do {} while (0)
#endif

enum DispatchMask : uint16_t
{
    Dispatch_NativeAimDelta             = 1u << 0,
    Dispatch_ShadowFreeze               = 1u << 1,
    Dispatch_NoxusBlade                 = 1u << 2,
    Dispatch_ImmediateInputEdgeOverlay  = 1u << 3,
    Dispatch_TransformGate              = 1u << 4,
    Dispatch_WeaponSwitch               = 1u << 5,
    Dispatch_NativeZoomToggle           = 1u << 6,
    Dispatch_NativeBipedFire            = 1u << 7,
    Dispatch_LowLatencyAim              = 1u << 8,
};

static_assert(
    MelonPrimeArm9HookState::Capacity
        == melonDS::NDS::ARM9InstructionHookMaxAddresses);

static void ClearDispatchEntries(MelonPrimeArm9HookState& state) noexcept
{
    state.count = 0;
    state.lastAddress = 0;
    state.lastMask = 0;
    for (auto& entry : state.entries)
        entry = {};
}

static void AddDispatchAddress(
    MelonPrimeArm9HookState& state,
    uint32_t address,
    uint16_t mask) noexcept
{
    for (uint32_t i = 0; i < state.count; ++i)
    {
        if (state.entries[i].address == address)
        {
            state.entries[i].mask |= mask;
            return;
        }
    }

    if (state.count >= MelonPrimeArm9HookState::Capacity)
    {
        MP_ARM9_HOOK_LOG(
            "ARM9Hook RegisterDrop: address=%08X mask=%02X count=%u max=%u\n",
            address,
            static_cast<unsigned>(mask),
            state.count,
            melonDS::NDS::ARM9InstructionHookMaxAddresses);
        return;
    }

    state.entries[state.count++] = {address, mask};
}

[[nodiscard]] static FORCE_INLINE uint16_t FindDispatchMask(
    MelonPrimeArm9HookState& state,
    uint32_t arm9ExecAddr) noexcept
{
    if (LIKELY(arm9ExecAddr == state.lastAddress))
        return state.lastMask;

    for (uint32_t i = 0; i < state.count; ++i)
    {
        if (state.entries[i].address == arm9ExecAddr)
        {
            state.lastAddress = arm9ExecAddr;
            state.lastMask = state.entries[i].mask;
            return state.entries[i].mask;
        }
    }
    return 0;
}

static bool DispatcherCallback(
    melonDS::NDS* nds,
    void* userdata,
    uint32_t arm9ExecAddr,
    uint32_t regs[16],
    uint32_t& redirectExecAddr)
{
    redirectExecAddr = 0;

    // ARM9Hook_Install() always passes a non-null MelonPrimeCore as userdata,
    // and ClearARM9InstructionHook() detaches the dispatcher entirely, so when
    // we get here core is by construction non-null. Skip the per-handler null
    // checks that the previous design carried.
    auto* const core = static_cast<MelonPrimeCore*>(userdata);
    const uint16_t mask = FindDispatchMask(core->Arm9HookState(), arm9ExecAddr);
    if (UNLIKELY(mask == 0))
        return false;

    if ((mask & Dispatch_NativeAimDelta) != 0)
    {
        if (core->GetNativeAimHookMode() == 2)
            core->NativeAimDeltaHookPostFoldWrite_DispatchCheck(nds, arm9ExecAddr, regs);
        else
            core->NativeAimDeltaHookRegisterInjection_DispatchCheck(nds, arm9ExecAddr, regs);
    }

    if ((mask & Dispatch_LowLatencyAim) != 0)
        core->LowLatencyAimHook_DispatchCheck(nds, arm9ExecAddr, regs);

    if ((mask & Dispatch_NativeZoomToggle) != 0)
    {
        if (core->NativeZoomToggleHook_DispatchCheckAndRedirect(
                nds, arm9ExecAddr, regs, redirectExecAddr))
            return true;
    }

    if ((mask & Dispatch_NativeBipedFire) != 0)
    {
        if (core->NativeBipedFireHook_DispatchCheckAndRedirect(
                nds, arm9ExecAddr, regs, redirectExecAddr))
            return true;
    }

    if ((mask & Dispatch_ImmediateInputEdgeOverlay) != 0)
        core->ImmediateInputEdgeOverlay_DispatchCheck(nds, arm9ExecAddr, regs);

    // Side-effect hook: runs regardless of whether a redirect follows.
    if ((mask & Dispatch_NoxusBlade) != 0)
        FixNoxusBladePersistence_DispatchCheck(nds, arm9ExecAddr, regs);

    // Redirect hooks: may change execution address.
    if ((mask & Dispatch_TransformGate) != 0)
    {
        if (core->TransformGateHook_DispatchCheckAndRedirect(
                nds, arm9ExecAddr, regs, redirectExecAddr))
            return true;
    }

    if ((mask & Dispatch_WeaponSwitch) != 0)
    {
        if (core->WeaponSwitchHook_DispatchCheckAndRedirect(
                nds, arm9ExecAddr, regs, redirectExecAddr))
            return true;
    }

    if ((mask & Dispatch_ShadowFreeze) != 0)
    {
        return ShadowFreezeRuntimeHook_DispatchCheckAndRedirect(
            nds, arm9ExecAddr, regs, redirectExecAddr);
    }

    return false;
}

[[nodiscard]] static bool InstalledDispatcherMatches(
    melonDS::NDS* nds,
    MelonPrimeCore* core,
    const uint32_t* addresses,
    uint32_t count) noexcept
{
    if (!nds
        || nds->ARM9InstructionHook != DispatcherCallback
        || nds->ARM9InstructionHookUserData != core
        || nds->ARM9InstructionHookAddrCount != count)
    {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        if (nds->ARM9InstructionHookAddresses[i] != addresses[i])
            return false;
    }
    return true;
}

[[nodiscard]] static bool HasInstalledInstructionHook(melonDS::NDS* nds) noexcept
{
    return nds
        && (nds->ARM9InstructionHook != nullptr
            || nds->ARM9InstructionHookAddrCount != 0);
}

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
static void DevOsdHookRegistered(EmuInstance* emu, uint32_t pcCount) noexcept
{
    if (emu)
        emu->osdAddMessage(0, "ARM9 hooks: registered (%u PCs)", pcCount);
}

static void DevOsdHookUnregistered(EmuInstance* emu) noexcept
{
    if (emu)
        emu->osdAddMessage(0, "ARM9 hooks: unregistered");
}
#endif

} // anonymous namespace

void ARM9Hook_Install(
    melonDS::NDS* nds,
    Config::Table& cfg,
    uint8_t romGroupIndex,
    MelonPrimeCore* core,
    uint8_t activeScope,
    EmuInstance* osdEmu)
{
    auto& state = core->Arm9HookState();
    ClearDispatchEntries(state);

    if (!nds)
    {
        ShadowFreezeRuntimeHook_ClearState();
        FixNoxusBladePersistence_ClearState();
        return;
    }

    if ((activeScope & ARM9HookScope_InMatch) == 0)
    {
        ShadowFreezeRuntimeHook_ClearState();
        FixNoxusBladePersistence_ClearState();
        if (HasInstalledInstructionHook(nds))
        {
            nds->ClearARM9InstructionHook();
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
            DevOsdHookUnregistered(osdEmu);
#endif
        }
        return;
    }

    uint32_t moduleAddresses[melonDS::NDS::ARM9InstructionHookMaxAddresses] = {};
    uint32_t moduleCount = 0;

    auto addModuleAddresses = [&](uint16_t mask) {
        for (uint32_t i = 0; i < moduleCount; ++i)
            AddDispatchAddress(state, moduleAddresses[i], mask);
    };

    int nativeAimHookMode = cfg.GetInt(CfgKey::NativeAimHookMode);
#ifndef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    nativeAimHookMode = 0;
#endif
    if (!cfg.GetBool(CfgKey::DisableMphAimSmoothing))
        nativeAimHookMode = 0;

    // Register only hooks that can actually run for the current config. Each
    // registered ARM9 PC becomes a JIT trampoline call site, so leaving disabled
    // features registered is visible in the in-game hot path.
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    const bool enableImmediateOverlay = cfg.GetBool(CfgKey::ImmediateInputEdgeOverlay);
    const bool enableNativeZoomToggle =
        cfg.GetInt(CfgKey::ZoomInputMethod) == ZoomInputMethod::NewNativeToggle;
    const bool enableNativeBipedFire =
        cfg.GetInt(CfgKey::BipedFireMethod) != BipedFireMethod::LegacyInput;
#else
    constexpr bool enableImmediateOverlay = false;
    constexpr bool enableNativeZoomToggle = false;
    constexpr bool enableNativeBipedFire = false;
#endif
    const bool enableNoxusBlade =
        cfg.GetBool(MelonPrime::CfgKey::FixNoxusBladePersistence);
    const bool enableShadowFreeze = cfg.GetBool(CfgKey::FixShadowFreeze);
    const bool enableTransformGate = cfg.GetBool(CfgKey::DirectAltFormTransform);
    const bool enableWeaponSwitch =
        cfg.GetInt(CfgKey::WeaponSwitchMethod) != WeaponSwitchMethod::LegacyTouch;
    int lowLatencyAimMode = cfg.GetInt(CfgKey::LowLatencyAimMode);
    if (lowLatencyAimMode == LowLatencyAimMode::Off
        && cfg.GetBool(CfgKey::InstantAimFollow))
        lowLatencyAimMode = LowLatencyAimMode::ImmediateSync;
#ifndef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    if (lowLatencyAimMode == LowLatencyAimMode::InstantAimFollow)
        lowLatencyAimMode = LowLatencyAimMode::ImmediateSync;
#endif
    const bool enableLowLatencyAim =
        cfg.GetBool(CfgKey::DisableMphAimSmoothing)
        && !cfg.GetBool(CfgKey::StylusMode)
        && (lowLatencyAimMode == LowLatencyAimMode::ImmediateSync
            || lowLatencyAimMode == LowLatencyAimMode::MoonLikeAim);

    if (enableShadowFreeze)
        ShadowFreezeRuntimeHook_SetState(&cfg, romGroupIndex);
    else
        ShadowFreezeRuntimeHook_ClearState();

    if (enableNoxusBlade)
        FixNoxusBladePersistence_SetState(&cfg, romGroupIndex);
    else
        FixNoxusBladePersistence_ClearState();

    if (nativeAimHookMode == 1)
    {
        moduleCount = MelonPrimeCore::NativeAimDeltaHookRegisterInjection_GetAddresses(
            romGroupIndex, moduleAddresses, melonDS::NDS::ARM9InstructionHookMaxAddresses);
        addModuleAddresses(Dispatch_NativeAimDelta);
    }
    else if (nativeAimHookMode == 2)
    {
        moduleCount = MelonPrimeCore::NativeAimDeltaHookPostFoldWrite_GetAddresses(
            romGroupIndex, moduleAddresses, melonDS::NDS::ARM9InstructionHookMaxAddresses);
        addModuleAddresses(Dispatch_NativeAimDelta);
    }

    if (enableLowLatencyAim)
    {
        moduleCount = MelonPrimeCore::LowLatencyAimHook_GetAddresses(
            romGroupIndex, moduleAddresses, melonDS::NDS::ARM9InstructionHookMaxAddresses);
        addModuleAddresses(Dispatch_LowLatencyAim);
    }

    if (enableShadowFreeze)
    {
        moduleCount = ShadowFreezeRuntimeHook_GetAddresses(
            romGroupIndex,
            moduleAddresses,
            melonDS::NDS::ARM9InstructionHookMaxAddresses);
        addModuleAddresses(Dispatch_ShadowFreeze);
    }

    if (enableNoxusBlade)
    {
        moduleCount = FixNoxusBladePersistence_GetAddresses(
            romGroupIndex,
            moduleAddresses,
            melonDS::NDS::ARM9InstructionHookMaxAddresses);
        addModuleAddresses(Dispatch_NoxusBlade);
    }

    if (enableImmediateOverlay)
    {
        moduleCount = MelonPrimeCore::ImmediateInputEdgeOverlay_GetAddresses(
            romGroupIndex,
            moduleAddresses,
            melonDS::NDS::ARM9InstructionHookMaxAddresses);
        addModuleAddresses(Dispatch_ImmediateInputEdgeOverlay);
    }

    if (enableNativeZoomToggle)
    {
        moduleCount = MelonPrimeCore::NativeZoomToggleHook_GetAddresses(
            romGroupIndex,
            moduleAddresses,
            melonDS::NDS::ARM9InstructionHookMaxAddresses);
        addModuleAddresses(Dispatch_NativeZoomToggle);
    }

    if (enableNativeBipedFire)
    {
        moduleCount = MelonPrimeCore::NativeBipedFireHook_GetAddresses(
            romGroupIndex,
            moduleAddresses,
            melonDS::NDS::ARM9InstructionHookMaxAddresses);
        addModuleAddresses(Dispatch_NativeBipedFire);
    }

    if (enableTransformGate)
    {
        moduleCount = MelonPrimeCore::TransformGateHook_GetAddresses(
            romGroupIndex,
            moduleAddresses,
            melonDS::NDS::ARM9InstructionHookMaxAddresses);
        addModuleAddresses(Dispatch_TransformGate);
    }

    if (enableWeaponSwitch)
    {
        moduleCount = MelonPrimeCore::WeaponSwitchHook_GetAddresses(
            romGroupIndex,
            moduleAddresses,
            melonDS::NDS::ARM9InstructionHookMaxAddresses);
        addModuleAddresses(Dispatch_WeaponSwitch);
    }

    uint32_t addresses[melonDS::NDS::ARM9InstructionHookMaxAddresses] = {};
    const uint32_t count = state.count;
    for (uint32_t i = 0; i < count; ++i)
        addresses[i] = state.entries[i].address;

    MP_ARM9_HOOK_LOG(
        "ARM9Hook Install: rom=%u count=%u max=%u\n",
        romGroupIndex,
        count,
        melonDS::NDS::ARM9InstructionHookMaxAddresses);
    for (uint32_t i = 0; i < count; ++i)
    {
        MP_ARM9_HOOK_LOG(
            "ARM9Hook Address[%u]: %08X mask=%02X\n",
            i,
            state.entries[i].address,
            static_cast<unsigned>(state.entries[i].mask));
    }

    if (count > 0)
    {
        const bool hadHook = HasInstalledInstructionHook(nds);
        const bool hookInstallChanged =
            !InstalledDispatcherMatches(nds, core, addresses, count);

        // Always re-attach after match-end Clear: skipping SetARM9InstructionHook when
        // the address list matches leaves JIT blocks compiled without trampolines.
        nds->SetARM9InstructionHook(DispatcherCallback, core, addresses, count);

        // Re-touch every hook PC so affected JIT blocks recompile with trampolines.
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t addr = addresses[i] & ~3u;
            nds->ARM9Write32(addr, nds->ARM9Read32(addr));
        }

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (!hadHook || hookInstallChanged)
            DevOsdHookRegistered(osdEmu, count);
#endif
    }
    else if (HasInstalledInstructionHook(nds))
    {
        nds->ClearARM9InstructionHook();
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        DevOsdHookUnregistered(osdEmu);
#endif
    }
}

void ARM9Hook_SetMatchHooksActive(
    melonDS::NDS* nds,
    Config::Table& cfg,
    uint8_t romGroupIndex,
    MelonPrimeCore* core,
    bool active,
    EmuInstance* osdEmu)
{
    ARM9Hook_Install(
        nds,
        cfg,
        romGroupIndex,
        core,
        active ? ARM9HookScope_InMatch : 0,
        osdEmu);
}

void ARM9Hook_Uninstall(
    melonDS::NDS* nds,
    MelonPrimeCore* core,
    EmuInstance* osdEmu)
{
    auto& state = core->Arm9HookState();
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    // emuInstance->reset() clears the NDS hook slot before OnEmuStart, but
    // Instance state still reflects MelonPrime's last registered hook set.
    const bool hadHooks =
        state.count > 0 || HasInstalledInstructionHook(nds);
#endif
    ClearDispatchEntries(state);
    ShadowFreezeRuntimeHook_ClearState();
    FixNoxusBladePersistence_ClearState();
    if (nds)
        nds->ClearARM9InstructionHook();
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (osdEmu && hadHooks)
        DevOsdHookUnregistered(osdEmu);
#endif
}

void ARM9Hook_ResetPatchState()
{
    ShadowFreezeRuntimeHook_ResetPatchState();
    FixNoxusBladePersistence_ResetPatchState();
}

} // namespace MelonPrime

#undef MP_ARM9_HOOK_LOG

#endif // MELONPRIME_DS
