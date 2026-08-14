/*
    Developer-only regression tests for the Vulkan renderer fallback latch.

    This test intentionally reports a runtime failure before asking the
    production renderer-selection code to resolve a forced Vulkan request.
    That keeps the test independent of the host GPU while covering the exact
    sticky-failure path that prevents a fallback/reselect loop.
*/

#include <cstdio>
#include <cstdlib>

#include "EmuInstance.h"
#include "MelonPrimeVideoBackend.h"
#include "MelonPrimeVulkanFeatureCheck.h"

namespace
{

bool gVulkanRuntimeAvailable = true;
int gRuntimeAvailabilityChecks = 0;

} // namespace

// Keep the selector test deterministic and GPU-independent. The process-level
// stress script exercises the real feature checker; this binary focuses on
// the production normalization contract for both the forced-success and
// sticky-failure states.
namespace MelonPrime::VulkanFeatureCheck
{

bool IsRuntimeAvailable()
{
    gRuntimeAvailabilityChecks++;
    return gVulkanRuntimeAvailable;
}

void ReportRuntimeFailure(std::string)
{
    gVulkanRuntimeAvailable = false;
}

} // namespace MelonPrime::VulkanFeatureCheck

namespace
{

bool SetEnvironment(const char* name, const char* value)
{
#if defined(_WIN32)
    return _putenv_s(name, value) == 0;
#else
    return setenv(name, value, 1) == 0;
#endif
}

bool ClearEnvironment(const char* name)
{
#if defined(_WIN32)
    return _putenv_s(name, "") == 0;
#else
    return unsetenv(name) == 0;
#endif
}

bool Require(bool condition, const char* message)
{
    if (condition)
        return true;

    std::fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

} // namespace

int main()
{
    using MelonPrime::VideoBackend::NormalizeRendererForPlatform;
    using MelonPrime::VideoBackend::PresentationBackend;
    using MelonPrime::VideoBackend::ResolvePresentationBackend;

    if (!Require(
            SetEnvironment("MELONPRIME_FORCE_VULKAN_RENDERER", "1"),
            "could not enable the forced Vulkan test override"))
    {
        return 1;
    }

    bool passed = true;
    passed = Require(
                 NormalizeRendererForPlatform(renderer3D_Software) ==
                     renderer3D_Vulkan,
                 "the forced Vulkan override did not select Vulkan when available") &&
             passed;

    // No Vulkan probe is needed after this call: IsRuntimeAvailable() must
    // immediately return false from the sticky runtime-failed state.
    MelonPrime::VulkanFeatureCheck::ReportRuntimeFailure(
        "test injected Vulkan runtime failure");

    passed = Require(
                 NormalizeRendererForPlatform(renderer3D_Vulkan) ==
                     renderer3D_Software,
                 "a persisted Vulkan renderer was not normalized after failure") &&
             passed;
    passed = Require(
                 NormalizeRendererForPlatform(renderer3D_Software) ==
                     renderer3D_Software,
                 "the forced Vulkan override re-selected Vulkan after failure") &&
             passed;
    passed = Require(
                 gRuntimeAvailabilityChecks >= 2,
                 "renderer normalization did not consult the runtime availability latch") &&
             passed;
    passed = Require(
                 ResolvePresentationBackend(false, renderer3D_Software) ==
                     PresentationBackend::NativeQt,
                 "fallback presentation did not return to NativeQt") &&
             passed;
    passed = ClearEnvironment("MELONPRIME_FORCE_VULKAN_RENDERER") && passed;

    if (!passed)
        return 1;

    std::puts("PASS: Vulkan failure latch blocks forced renderer re-selection");
    return 0;
}
