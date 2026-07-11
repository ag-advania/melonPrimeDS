#ifndef MELON_PRIME_ARM9_HOOK_H
#define MELON_PRIME_ARM9_HOOK_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

class EmuInstance;

namespace MelonPrime {

class MelonPrimeCore;

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
    Config::Table& cfg,
    uint8_t romGroupIndex,
    MelonPrimeCore* core,
    uint8_t activeScope,
    EmuInstance* osdEmu = nullptr);

// Install or clear match-scoped hooks (config-gated inside Install).
void ARM9Hook_SetMatchHooksActive(
    melonDS::NDS* nds,
    Config::Table& cfg,
    uint8_t romGroupIndex,
    MelonPrimeCore* core,
    bool active,
    EmuInstance* osdEmu = nullptr);

void ARM9Hook_Uninstall(
    melonDS::NDS* nds,
    MelonPrimeCore* core,
    EmuInstance* osdEmu = nullptr);

// Calls ResetPatchState for every registered hook.
void ARM9Hook_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_ARM9_HOOK_H
