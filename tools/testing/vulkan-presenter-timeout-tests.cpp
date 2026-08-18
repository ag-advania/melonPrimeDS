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

void SetEnvironment(const char* name, const char* value)
{
#if defined(_WIN32)
    if (_putenv_s(name, value ? value : "") != 0)
        throw std::runtime_error("_putenv_s failed");
#else
    const int result = value
        ? setenv(name, value, 1)
        : unsetenv(name);
    if (result != 0)
        throw std::runtime_error("setenv/unsetenv failed");
#endif
}

void SetNormalAcquireTimeoutEnvironment(const char* value)
{
    SetEnvironment("MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS", value);
}

void SetLowLatencyAcquireTimeoutEnvironment(const char* value)
{
    SetEnvironment("MELONPRIME_VULKAN_LOW_LATENCY_ACQUIRE_TIMEOUT_NS", value);
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

        SetNormalAcquireTimeoutEnvironment("123456789");
        MelonPrime::ResetPresenterAcquireTimeoutCachesForTesting();
        Require(
            MelonPrime::PresenterAcquireTimeoutNanoseconds() == 123456789,
            "normal acquire timeout environment override was not applied");

        SetNormalAcquireTimeoutEnvironment("0");
        Require(
            MelonPrime::PresenterAcquireTimeoutNanoseconds() == 123456789,
            "normal acquire timeout was not cached");

        SetNormalAcquireTimeoutEnvironment(nullptr);
        MelonPrime::ResetPresenterAcquireTimeoutCachesForTesting();
        Require(
            MelonPrime::PresenterAcquireTimeoutNanoseconds() == expectedDefault,
            "normal acquire timeout default is incorrect");

        SetLowLatencyAcquireTimeoutEnvironment("250000");
        MelonPrime::ResetPresenterAcquireTimeoutCachesForTesting();
        Require(
            MelonPrime::PresenterLowLatencyAcquireTimeoutNanoseconds() == 250000,
            "low-latency acquire timeout environment override was not applied");

        SetLowLatencyAcquireTimeoutEnvironment("-1");
        MelonPrime::ResetPresenterAcquireTimeoutCachesForTesting();
        Require(
            MelonPrime::PresenterLowLatencyAcquireTimeoutNanoseconds() == 500000,
            "negative low-latency acquire timeout did not use the fallback");

        SetLowLatencyAcquireTimeoutEnvironment("0");
        MelonPrime::ResetPresenterAcquireTimeoutCachesForTesting();
        Require(
            MelonPrime::PresenterLowLatencyAcquireTimeoutNanoseconds() == 0,
            "zero low-latency acquire timeout was not accepted");

        SetLowLatencyAcquireTimeoutEnvironment("1000000");
        Require(
            MelonPrime::PresenterLowLatencyAcquireTimeoutNanoseconds() == 0,
            "low-latency acquire timeout was not cached");

        SetLowLatencyAcquireTimeoutEnvironment(nullptr);
        MelonPrime::ResetPresenterAcquireTimeoutCachesForTesting();
        Require(
            MelonPrime::PresenterLowLatencyAcquireTimeoutNanoseconds() == 500000,
            "low-latency acquire timeout default is incorrect");

        std::cout << "Vulkan presenter timeout policy tests: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        SetNormalAcquireTimeoutEnvironment(nullptr);
        SetLowLatencyAcquireTimeoutEnvironment(nullptr);
        std::cerr << "Vulkan presenter timeout policy tests: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
