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

#include "MelonPrimeRendererTransitionProfile.h"

#ifdef MELONPRIME_DS

#include <chrono>
#include <mutex>

#include "Platform.h"

// Platform::Log lives in melonDS::Platform; the frontend spells it unqualified.
using namespace melonDS;

namespace MelonPrime
{
namespace RendererTransitionProfile
{

namespace
{

using Clock = std::chrono::steady_clock;

// One transition at a time: onUpdateVideoSettings() serialises the whole
// switch on the GUI thread, and the emulation threads it drives only mark
// phases inside that window.
std::mutex StateMutex;
bool Active = false;
Clock::time_point StartTime;
Clock::time_point LastMark;
int ToRenderer = -1;

double MillisSince(Clock::time_point from, Clock::time_point to) noexcept
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

} // namespace

void Begin(int toRenderer)
{
    std::lock_guard<std::mutex> lock(StateMutex);
    if (Active)
        return;

    Active = true;
    StartTime = Clock::now();
    LastMark = StartTime;
    ToRenderer = toRenderer;
}

void Mark(const char* phase)
{
    std::lock_guard<std::mutex> lock(StateMutex);
    if (!Active)
        return;

    const Clock::time_point now = Clock::now();
    Platform::Log(
        Platform::LogLevel::Info,
        "[RendererTransition] phase=%s ms=%.2f t+%.2f\n",
        phase ? phase : "?",
        MillisSince(LastMark, now),
        MillisSince(StartTime, now));
    LastMark = now;
}

void End()
{
    std::lock_guard<std::mutex> lock(StateMutex);
    if (!Active)
        return;

    Active = false;
    Platform::Log(
        Platform::LogLevel::Info,
        "[RendererTransition] to=%d total_ms=%.2f\n",
        ToRenderer,
        MillisSince(StartTime, Clock::now()));
}

ScopedPhase::ScopedPhase(const char* phase) noexcept
    : Phase(phase)
    , StartNs(static_cast<unsigned long long>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              Clock::now().time_since_epoch()).count()))
{
}

ScopedPhase::~ScopedPhase()
{
    const unsigned long long nowNs = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()).count());
    Platform::Log(
        Platform::LogLevel::Info,
        "[RendererTransition] phase=%s ms=%.2f\n",
        Phase ? Phase : "?",
        static_cast<double>(nowNs - StartNs) / 1000000.0);
}

bool IsActive() noexcept
{
    std::lock_guard<std::mutex> lock(StateMutex);
    return Active;
}

} // namespace RendererTransitionProfile
} // namespace MelonPrime

#endif // MELONPRIME_DS
