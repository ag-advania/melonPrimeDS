#ifndef MELON_PRIME_ARM9_HOOK_H
#define MELON_PRIME_ARM9_HOOK_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace melonDS { class NDS; }

class EmuInstance;

namespace MelonPrime {

class MelonPrimeCore;

// Cold-resolved ARM9 feature policy.  Runtime hook installation consumes this
// value only; it must not reinterpret Config::Table keys on the install edge.
struct Arm9HookActivationPlan {
    int8_t nativeAimHookMode = 0;
    int8_t lowLatencyAimMode = 0;
    bool immediateInputEdgeOverlay = false;
    bool nativeZoomToggle = false;
    bool nativeBipedFire = false;
    bool directAltFormTransform = false;
    bool nativeWeaponSwitch = false;
    bool shadowFreeze = false;
    bool noxusBladePersistence = false;
};

// Combined ARM9 instruction hook dispatcher.
//
// Owns the single SetARM9InstructionHook slot and dispatches to all registered
// MelonPrime runtime hooks in priority order.
//
// Match-scoped hooks (today: all listed hooks) are installed only while
// battle runtime latch via ARM9Hook_SetMatchHooksActive(true) from HandleBattleRuntimeEnter and
// cleared on isEndOfGame / !isInGame. Future out-of-match hooks can use a new
// ARM9HookScope bit without changing the match lifecycle.

enum ARM9HookScope : uint8_t
{
    ARM9HookScope_InMatch = 1u << 0,
};

void ARM9Hook_Install(
    melonDS::NDS* nds,
    uint8_t romGroupIndex,
    MelonPrimeCore* core,
    const Arm9HookActivationPlan& plan,
    uint8_t activeScope,
    EmuInstance* osdEmu = nullptr);

// Install or clear match-scoped hooks using the Core's cold-resolved plan.
void ARM9Hook_SetMatchHooksActive(
    melonDS::NDS* nds,
    uint8_t romGroupIndex,
    MelonPrimeCore* core,
    bool active,
    EmuInstance* osdEmu = nullptr);

void ARM9Hook_Uninstall(
    melonDS::NDS* nds,
    MelonPrimeCore* core,
    EmuInstance* osdEmu = nullptr);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_ARM9_HOOK_H
