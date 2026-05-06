#ifdef MELONPRIME_DS

#include "MelonPrimeArm9Hook.h"
#include "MelonPrime.h"
#include "MelonPrimeCompilerHints.h"
#include "MelonPrimePatchShadowFreezeRuntimeHook.h"
#include "MelonPrimePatchFixNoxusBladePersistence.h"
#include "NDS.h"
#include "Platform.h"

#include <cstdint>

namespace MelonPrime {

namespace {

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
#define MP_ARM9_HOOK_LOG(...) \
    melonDS::Platform::Log(melonDS::Platform::LogLevel::Info, __VA_ARGS__)
#else
#define MP_ARM9_HOOK_LOG(...) do {} while (0)
#endif

enum DispatchMask : uint8_t
{
    Dispatch_NativeAimDelta             = 1u << 0,
    Dispatch_ShadowFreeze               = 1u << 1,
    Dispatch_NoxusBlade                 = 1u << 2,
    Dispatch_ImmediateInputEdgeOverlay  = 1u << 3,
    Dispatch_TransformGate              = 1u << 4,
    Dispatch_WeaponSwitch               = 1u << 5,
};

struct DispatchEntry
{
    uint32_t Address;
    uint8_t Mask;
};

static DispatchEntry s_dispatchEntries[melonDS::NDS::ARM9InstructionHookMaxAddresses] = {};
static uint32_t s_dispatchCount = 0;

static void ClearDispatchEntries() noexcept
{
    s_dispatchCount = 0;
    for (auto& entry : s_dispatchEntries)
        entry = {};
}

static void AddDispatchAddress(uint32_t address, uint8_t mask) noexcept
{
    for (uint32_t i = 0; i < s_dispatchCount; ++i)
    {
        if (s_dispatchEntries[i].Address == address)
        {
            s_dispatchEntries[i].Mask |= mask;
            return;
        }
    }

    if (s_dispatchCount >= melonDS::NDS::ARM9InstructionHookMaxAddresses)
    {
        MP_ARM9_HOOK_LOG(
            "ARM9Hook RegisterDrop: address=%08X mask=%02X count=%u max=%u\n",
            address,
            mask,
            s_dispatchCount,
            melonDS::NDS::ARM9InstructionHookMaxAddresses);
        return;
    }

    s_dispatchEntries[s_dispatchCount++] = {address, mask};
}

[[nodiscard]] static FORCE_INLINE uint8_t FindDispatchMask(uint32_t arm9ExecAddr) noexcept
{
    for (uint32_t i = 0; i < s_dispatchCount; ++i)
    {
        if (s_dispatchEntries[i].Address == arm9ExecAddr)
            return s_dispatchEntries[i].Mask;
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

    const uint8_t mask = FindDispatchMask(arm9ExecAddr);
    if (UNLIKELY(mask == 0))
        return false;

    if ((mask & Dispatch_NativeAimDelta) != 0)
    {
        if (auto* core = static_cast<MelonPrimeCore*>(userdata))
        {
            if (core->GetNativeAimHookMode() == 2)
                core->NativeAimDeltaHookPostFoldWrite_DispatchCheck(nds, arm9ExecAddr, regs);
            else
                core->NativeAimDeltaHookRegisterInjection_DispatchCheck(nds, arm9ExecAddr, regs);
        }
    }

    if ((mask & Dispatch_ImmediateInputEdgeOverlay) != 0)
    {
        if (auto* core = static_cast<MelonPrimeCore*>(userdata))
            core->ImmediateInputEdgeOverlay_DispatchCheck(nds, arm9ExecAddr, regs);
    }

    // Side-effect hook: runs regardless of whether a redirect follows.
    if ((mask & Dispatch_NoxusBlade) != 0)
        FixNoxusBladePersistence_DispatchCheck(nds, arm9ExecAddr, regs);

    // Redirect hooks: may change execution address.
    if ((mask & Dispatch_TransformGate) != 0)
    {
        if (auto* core = static_cast<MelonPrimeCore*>(userdata))
        {
            if (core->TransformGateHook_DispatchCheckAndRedirect(
                    nds, arm9ExecAddr, regs, redirectExecAddr))
                return true;
        }
    }

    if ((mask & Dispatch_WeaponSwitch) != 0)
    {
        if (auto* core = static_cast<MelonPrimeCore*>(userdata))
        {
            if (core->WeaponSwitchHook_DispatchCheckAndRedirect(
                    nds, arm9ExecAddr, regs, redirectExecAddr))
                return true;
        }
    }

    if ((mask & Dispatch_ShadowFreeze) != 0)
    {
        return ShadowFreezeRuntimeHook_DispatchCheckAndRedirect(
            nds, arm9ExecAddr, regs, redirectExecAddr);
    }

    return false;
}

} // anonymous namespace

void ARM9Hook_Install(
    melonDS::NDS* nds,
    Config::Table& cfg,
    uint8_t romGroupIndex,
    MelonPrimeCore* core)
{
    ClearDispatchEntries();

    if (!nds)
    {
        ShadowFreezeRuntimeHook_ClearState();
        FixNoxusBladePersistence_ClearState();
        return;
    }

    ShadowFreezeRuntimeHook_SetState(&cfg, romGroupIndex);
    FixNoxusBladePersistence_SetState(&cfg, romGroupIndex);

    uint32_t moduleAddresses[melonDS::NDS::ARM9InstructionHookMaxAddresses] = {};
    uint32_t moduleCount = 0;

    // Register addresses for BOTH hook modes so mid-game mode switching works.
    // DispatcherCallback checks GetNativeAimHookMode() at call time to route
    // to the correct handler; whichever mode's PCs don't match will early-return
    // inside their DispatchCheck without side effects.
    moduleCount = MelonPrimeCore::NativeAimDeltaHookRegisterInjection_GetAddresses(
        romGroupIndex, moduleAddresses, melonDS::NDS::ARM9InstructionHookMaxAddresses);
    for (uint32_t i = 0; i < moduleCount; ++i)
        AddDispatchAddress(moduleAddresses[i], Dispatch_NativeAimDelta);

    moduleCount = MelonPrimeCore::NativeAimDeltaHookPostFoldWrite_GetAddresses(
        romGroupIndex, moduleAddresses, melonDS::NDS::ARM9InstructionHookMaxAddresses);
    for (uint32_t i = 0; i < moduleCount; ++i)
        AddDispatchAddress(moduleAddresses[i], Dispatch_NativeAimDelta);

    moduleCount = ShadowFreezeRuntimeHook_GetAddresses(
        romGroupIndex,
        moduleAddresses,
        melonDS::NDS::ARM9InstructionHookMaxAddresses);
    for (uint32_t i = 0; i < moduleCount; ++i)
        AddDispatchAddress(moduleAddresses[i], Dispatch_ShadowFreeze);

    moduleCount = FixNoxusBladePersistence_GetAddresses(
        romGroupIndex,
        moduleAddresses,
        melonDS::NDS::ARM9InstructionHookMaxAddresses);
    for (uint32_t i = 0; i < moduleCount; ++i)
        AddDispatchAddress(moduleAddresses[i], Dispatch_NoxusBlade);

    moduleCount = MelonPrimeCore::ImmediateInputEdgeOverlay_GetAddresses(
        romGroupIndex,
        moduleAddresses,
        melonDS::NDS::ARM9InstructionHookMaxAddresses);
    for (uint32_t i = 0; i < moduleCount; ++i)
        AddDispatchAddress(moduleAddresses[i], Dispatch_ImmediateInputEdgeOverlay);

    moduleCount = MelonPrimeCore::TransformGateHook_GetAddresses(
        romGroupIndex,
        moduleAddresses,
        melonDS::NDS::ARM9InstructionHookMaxAddresses);
    for (uint32_t i = 0; i < moduleCount; ++i)
        AddDispatchAddress(moduleAddresses[i], Dispatch_TransformGate);

    moduleCount = MelonPrimeCore::WeaponSwitchHook_GetAddresses(
        romGroupIndex,
        moduleAddresses,
        melonDS::NDS::ARM9InstructionHookMaxAddresses);
    for (uint32_t i = 0; i < moduleCount; ++i)
        AddDispatchAddress(moduleAddresses[i], Dispatch_WeaponSwitch);

    uint32_t addresses[melonDS::NDS::ARM9InstructionHookMaxAddresses] = {};
    const uint32_t count = s_dispatchCount;
    for (uint32_t i = 0; i < count; ++i)
        addresses[i] = s_dispatchEntries[i].Address;

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
            s_dispatchEntries[i].Address,
            s_dispatchEntries[i].Mask);
    }

    if (count > 0)
    {
        nds->SetARM9InstructionHook(DispatcherCallback, core, addresses, count);

        // Force JIT cache invalidation for every registered address.
        // The death cleanup code (NoxusBlade hook) may have been JIT-compiled
        // during game initialization before hook registration, so its compiled
        // block has no hook call.  Writing back the same instruction value
        // triggers melonDS to discard and recompile the affected block with the
        // hook in place.
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t addr = addresses[i] & ~3u;
            nds->ARM9Write32(addr, nds->ARM9Read32(addr));
        }
    }
    else
        nds->ClearARM9InstructionHook();
}

void ARM9Hook_Uninstall(melonDS::NDS* nds)
{
    ClearDispatchEntries();
    ShadowFreezeRuntimeHook_ClearState();
    FixNoxusBladePersistence_ClearState();
    if (nds)
        nds->ClearARM9InstructionHook();
}

void ARM9Hook_ResetPatchState()
{
    ShadowFreezeRuntimeHook_ResetPatchState();
    FixNoxusBladePersistence_ResetPatchState();
}

} // namespace MelonPrime

#undef MP_ARM9_HOOK_LOG

#endif // MELONPRIME_DS
