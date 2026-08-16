/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "MelonPrimeVulkanPresenterTimeout.h"

namespace
{

constexpr std::uint64_t kOneSecondNanoseconds = 1000ull * 1000ull * 1000ull;

void SetAcquireTimeoutEnvironment(const char* value)
{
#if defined(_WIN32)
    if (_putenv_s("MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS", value ? value : "") != 0)
        throw std::runtime_error("_putenv_s failed");
#else
    const int result = value
        ? setenv("MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS", value, 1)
        : unsetenv("MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS");
    if (result != 0)
        throw std::runtime_error("setenv/unsetenv failed");
#endif
}

std::uint64_t ExpectedDefaultTimeout() noexcept
{
#if defined(__linux__)
    return kOneSecondNanoseconds;
#else
    return std::numeric_limits<std::uint64_t>::max();
#endif
}

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

} // namespace

int main()
{
    try
    {
        const std::uint64_t expectedDefault = ExpectedDefaultTimeout();

        SetAcquireTimeoutEnvironment("0");
        Require(
            MelonPrime::PresenterAcquireTimeoutNanoseconds() == 0,
            "acquire timeout environment override was not applied");
        Require(
            MelonPrime::PresenterImageFenceTimeoutNanoseconds() == expectedDefault,
            "image-fence timeout inherited the acquire override");

        SetAcquireTimeoutEnvironment("123456789");
        Require(
            MelonPrime::PresenterAcquireTimeoutNanoseconds() == 123456789,
            "acquire timeout did not follow the configured value");
        Require(
            MelonPrime::PresenterImageFenceTimeoutNanoseconds() == expectedDefault,
            "image-fence timeout changed with the acquire value");

        SetAcquireTimeoutEnvironment(nullptr);
        Require(
            MelonPrime::PresenterAcquireTimeoutNanoseconds() == expectedDefault,
            "acquire timeout default is incorrect");
        Require(
            MelonPrime::PresenterImageFenceTimeoutNanoseconds() == expectedDefault,
            "image-fence timeout default is incorrect");

        std::cout << "Vulkan presenter timeout policy tests: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        SetAcquireTimeoutEnvironment(nullptr);
        std::cerr << "Vulkan presenter timeout policy tests: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
