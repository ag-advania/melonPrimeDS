#ifndef MELON_PRIME_BATTLE_FLOW_STATE_H
#define MELON_PRIME_BATTLE_FLOW_STATE_H

#include <cstdint>

#include "MelonPrimeCompilerHints.h"

namespace MelonPrime::BattleFlow {

    constexpr uint8_t MODE_BATTLE_PRE     = 0x0Du;  // intermediate mode entered just before Battle
    constexpr uint8_t MODE_BATTLE_RUNTIME = 0x0Eu;
    constexpr uint8_t FLOW_ACTIVE_MATCH   = 0u;
    constexpr uint8_t FLOW_END_CAMERA     = 1u;
    constexpr uint8_t FLOW_RESULT         = 2u;

    // MASTER_BRIGHT mode transition fade fields.
    constexpr uint8_t TRANSITION_TYPE_DARKEN      = 1u;
    constexpr uint8_t TRANSITION_PHASE_FADE_OUT   = 1u;  // darkening, not yet fully black
    constexpr uint8_t TRANSITION_PHASE_FULL_BLACK = 2u;  // full black reached / callback boundary
    constexpr uint8_t TRANSITION_PHASE_FADE_IN    = 3u;  // coming back out of full black
    constexpr int32_t BRIGHTNESS_FULL_BLACK       = -16; // darken factor 16 == fully black

    // isEndOfGame: currentMode guard required (flowState alone is unsafe in menu).
    // After mode==0x0E is known, flow!=0 matches game active-match gates (0..3, only 0 is live).
    [[nodiscard]] FORCE_INLINE bool IsEndOfGame(uint8_t currentMode, uint8_t flowState) noexcept
    {
        if (currentMode != MODE_BATTLE_RUNTIME)
            return false;
        return flowState != FLOW_ACTIVE_MATCH;
    }

    // =========================================================================
    //  Match window between the pre-match and post-match full-black fades
    //
    //  Detects, as one continuous per-frame boolean:
    //      pre-match full black ends  -> true
    //      start presentation / live match / scoreboard / end camera
    //      post-match fade reaches full black -> false
    //      result-screen fade-in stays false
    //
    //  Full black alone cannot be told apart from other screen transitions, so
    //  entry additionally requires a Battle context and exit requires a
    //  post-match context. Exit arms on the post-match fade first so a mode
    //  commit on the full-black frame cannot make the end edge be missed.
    //
    //  Hot path: the update takes RAM pointers, not a snapshot, and reads
    //  lazily. transitionType is the cheapest discriminator -- with no darken
    //  transition running the screen cannot be fully black and the post-match
    //  fade cannot arm, so every other field is skipped. The mode/flow context
    //  is only read on frames that are actually fully black, and the
    //  post-match context is not re-read once armed.
    // =========================================================================

    // Resolved once per ROM detect; all fields are fixed globals.
    struct MatchTransitionPtrs {
        const int32_t* transitionBrightness;  // signed: full black is <= -16
        const uint8_t* currentMode;
        const uint8_t* pendingMode;
        const uint8_t* flowState;
        const uint8_t* transitionTargetMode;
        const uint8_t* transitionPhase;
        const uint8_t* transitionType;
    };

    struct MatchBlackWindowState {
        enum Phase : uint8_t {
            WAIT_START_BLACK = 0,   // waiting for the pre-match full black
            WAIT_START_BLACK_EXIT,  // in the pre-match full black, waiting for it to lift
            ACTIVE,                 // the target window
        };
        uint8_t phase = WAIT_START_BLACK;
        bool postMatchFadeArmed = false;
        // Set on every lifecycle reset: the first evaluated frame may land in
        // the middle of a match (savestate load), where the pre-match full
        // black can never be observed.
        bool needsBootstrap = true;
    };

    // phase is already known to be a darken phase; only brightness is left.
    [[nodiscard]] FORCE_INLINE bool IsBlackPhase(uint8_t transitionPhase) noexcept
    {
        return transitionPhase == TRANSITION_PHASE_FULL_BLACK
            || transitionPhase == TRANSITION_PHASE_FADE_IN;
    }

    [[nodiscard]] FORCE_INLINE bool IsPostMatchContext(const MatchTransitionPtrs& p) noexcept
    {
        return *p.currentMode == MODE_BATTLE_RUNTIME && *p.flowState == FLOW_RESULT;
    }

    // Cold: only reachable on the first frame after a lifecycle reset.
    COLD_FUNCTION inline void BootstrapMatchBlackWindow(
        MatchBlackWindowState& st, const MatchTransitionPtrs& p) noexcept
    {
        st.needsBootstrap = false;

        // Attaching mid-match: enter ACTIVE directly instead of waiting for a
        // pre-match full black that already happened. Outside a match
        // currentMode is not the Battle runtime, so a normal boot does not
        // bootstrap and the strict pre-match observation still applies.
        if (*p.currentMode != MODE_BATTLE_RUNTIME)
            return;

        const bool darkening = (*p.transitionType == TRANSITION_TYPE_DARKEN);
        const uint8_t transitionPhase = darkening ? *p.transitionPhase : 0u;
        const bool fullBlack = darkening
            && IsBlackPhase(transitionPhase)
            && *p.transitionBrightness <= BRIGHTNESS_FULL_BLACK;
        if (fullBlack)
            return;

        st.phase = MatchBlackWindowState::ACTIVE;
        st.postMatchFadeArmed = darkening
            && transitionPhase == TRANSITION_PHASE_FADE_OUT
            && IsPostMatchContext(p);
    }

    // One call per emulated frame. Returns the window state for that frame.
    //
    // Steady-state cost is one u8 read (transitionType). Everything else is
    // read only on the branch that needs it -- see the header comment above.
    [[nodiscard]] FORCE_INLINE bool UpdateMatchBetweenBlackouts(
        MatchBlackWindowState& st, const MatchTransitionPtrs& p) noexcept
    {
        if (UNLIKELY(st.needsBootstrap))
            BootstrapMatchBlackWindow(st, p);

        // No darken transition running: cannot be fully black, and the
        // post-match fade cannot arm. This is the common case both in a live
        // match and in menus, and it exits after a single RAM read.
        const bool darkening = (*p.transitionType == TRANSITION_TYPE_DARKEN);

        switch (st.phase) {
        case MatchBlackWindowState::ACTIVE: {
            if (LIKELY(!darkening))
                return true;

            const uint8_t transitionPhase = *p.transitionPhase;

            // Fade out is never fully black, so arming and the exit test are
            // mutually exclusive frames.
            if (transitionPhase == TRANSITION_PHASE_FADE_OUT) {
                if (!st.postMatchFadeArmed && IsPostMatchContext(p))
                    st.postMatchFadeArmed = true;
                return true;
            }

            if (!IsBlackPhase(transitionPhase)
                || *p.transitionBrightness > BRIGHTNESS_FULL_BLACK)
                return true;

            // Fully black: close the window only in a post-match context.
            if (st.postMatchFadeArmed || IsPostMatchContext(p)) {
                st.phase = MatchBlackWindowState::WAIT_START_BLACK;
                st.postMatchFadeArmed = false;
                return false;
            }
            return true;
        }

        case MatchBlackWindowState::WAIT_START_BLACK: {
            if (LIKELY(!darkening))
                return false;
            if (!IsBlackPhase(*p.transitionPhase)
                || *p.transitionBrightness > BRIGHTNESS_FULL_BLACK)
                return false;

            // Fully black. Only now is the mode/flow context worth reading.
            const bool startMatchContext = !IsPostMatchContext(p)
                && (*p.transitionTargetMode == MODE_BATTLE_PRE
                    || *p.transitionTargetMode == MODE_BATTLE_RUNTIME
                    || *p.currentMode == MODE_BATTLE_PRE
                    || *p.pendingMode == MODE_BATTLE_PRE
                    || *p.pendingMode == MODE_BATTLE_RUNTIME);
            if (startMatchContext)
                st.phase = MatchBlackWindowState::WAIT_START_BLACK_EXIT;
            return false;
        }

        case MatchBlackWindowState::WAIT_START_BLACK_EXIT:
            // Entry is the first frame the full black itself lifts, not the
            // first fully bright frame.
            if (darkening
                && IsBlackPhase(*p.transitionPhase)
                && *p.transitionBrightness <= BRIGHTNESS_FULL_BLACK)
                return false;
            st.phase = MatchBlackWindowState::ACTIVE;
            st.postMatchFadeArmed = false;
            return true;

        default:
            break;
        }

        st.phase = MatchBlackWindowState::WAIT_START_BLACK;
        st.postMatchFadeArmed = false;
        return false;
    }

} // namespace MelonPrime::BattleFlow

#endif // MELON_PRIME_BATTLE_FLOW_STATE_H
