/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_VULKAN_PRESENTER_TIMEOUT_H
#define MELONPRIME_VULKAN_PRESENTER_TIMEOUT_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cstdint>

namespace MelonPrime
{

// The environment override is deliberately scoped to vkAcquireNextImageKHR.
std::uint64_t PresenterAcquireTimeoutNanoseconds() noexcept;

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // MELONPRIME_VULKAN_PRESENTER_TIMEOUT_H
