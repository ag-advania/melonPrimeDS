#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <string>

namespace MelonPrime::VulkanFeatureCheck
{

struct Result
{
    bool Available = false;
    std::string Reason;
};

// Performs the loader, instance, physical-device, queue, and logical-device
// probe. Presentation support is checked later against the actual Qt surface.
const Result& Probe();
bool IsRuntimeAvailable();
const std::string& UnavailableReason();
void ReportRuntimeFailure(std::string reason);
void ResetProbeForRetry();
void ResetProbeForTesting();

} // namespace MelonPrime::VulkanFeatureCheck

#endif
