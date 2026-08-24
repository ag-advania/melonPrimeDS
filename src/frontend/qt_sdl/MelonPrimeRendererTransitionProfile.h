/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef MELONPRIME_RENDERER_TRANSITION_PROFILE_H
#define MELONPRIME_RENDERER_TRANSITION_PROFILE_H

#ifdef MELONPRIME_DS

// Phase timing for a runtime renderer/presentation-backend switch.
//
// A switch is a cold, user-initiated event (settings dialog, or the developer
// stress driver) whose cost is split across two threads: the GUI thread tears
// the screen panel down and builds a new one, and the emulation thread
// destroys the old 3D renderer and constructs the new one. Neither side can
// see the other's cost, so "switching is slow" was previously unattributable.
//
// Marks are logged at Info as they happen and accumulated into a single
// summary line at End(). This is not a hot path: one transition emits a
// handful of log lines, and nothing here runs per frame.
//
// Thread-safety: Begin/Mark/End take a mutex. Marks may come from the GUI
// thread and the emulation thread; each line records which phase ran, so an
// interleaved sequence is still readable.

namespace MelonPrime
{
namespace RendererTransitionProfile
{

// Opens a transition window. A second Begin() without an End() is treated as
// a nested/duplicate call and ignored, so an inner path cannot reset the
// outer transition's clock.
void Begin(int toRenderer);

// Records the time since the previous mark (or Begin). No-op when no
// transition is open, so instrumented code can be called outside a switch.
void Mark(const char* phase);

// Closes the transition and logs the accumulated total.
void End();

// True between Begin() and End(). Used to skip mark bookkeeping in paths that
// also run outside transitions.
bool IsActive() noexcept;

// Times one bounded piece of work and logs it on destruction, whether or not a
// transition is open. For GUI-side work that runs beside a transition rather
// than inside it -- the settings dialog's post-switch backend re-probe is the
// case this exists for.
class ScopedPhase
{
public:
    explicit ScopedPhase(const char* phase) noexcept;
    ~ScopedPhase();

    ScopedPhase(const ScopedPhase&) = delete;
    ScopedPhase& operator=(const ScopedPhase&) = delete;

private:
    const char* Phase;
    unsigned long long StartNs;
};

} // namespace RendererTransitionProfile
} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELONPRIME_RENDERER_TRANSITION_PROFILE_H
