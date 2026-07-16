#pragma once

// First-frame Vulkan/GPU trace budget (S77-1). Logs only until budget is consumed.

#include <cstdarg>
#include <cstdio>

namespace MelonPrime::FirstVulkanFrameTrace
{

inline bool& budgetActive() noexcept
{
    static bool active = true;
    return active;
}

inline void log(const char* fmt, ...) noexcept
{
    if (!budgetActive())
        return;

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

inline void consumeBudget() noexcept
{
    budgetActive() = false;
}

} // namespace MelonPrime::FirstVulkanFrameTrace
