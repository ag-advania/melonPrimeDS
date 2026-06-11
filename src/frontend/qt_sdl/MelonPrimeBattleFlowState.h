#ifndef MELON_PRIME_BATTLE_FLOW_STATE_H
#define MELON_PRIME_BATTLE_FLOW_STATE_H

#include <cstdint>

#include "MelonPrimeCompilerHints.h"

namespace MelonPrime::BattleFlow {

    constexpr uint8_t MODE_BATTLE_RUNTIME = 0x0Eu;
    constexpr uint8_t FLOW_ACTIVE_MATCH   = 0u;
    constexpr uint8_t FLOW_END_CAMERA     = 1u;
    constexpr uint8_t FLOW_RESULT         = 2u;

    // isEndOfGame: currentMode guard required (flowState==2 alone is unsafe).
    [[nodiscard]] FORCE_INLINE bool IsEndOfGame(uint8_t currentMode, uint8_t flowState) noexcept
    {
        return currentMode == MODE_BATTLE_RUNTIME
            && (flowState == FLOW_END_CAMERA || flowState == FLOW_RESULT);
    }

} // namespace MelonPrime::BattleFlow

#endif // MELON_PRIME_BATTLE_FLOW_STATE_H
