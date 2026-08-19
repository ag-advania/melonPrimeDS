/*
    Pure policy tests for the split graphics/present queue A/B. The runtime
    must not select an EXCLUSIVE split path until the SyncVal gate is explicit.
*/

#include <cstdio>
#include <cstdlib>

#include "VulkanQueueSharingExperiment.h"

using melonDS::Vk::MakeVulkanQueueSharingPlan;
using melonDS::Vk::VulkanQueueSharingMode;

namespace
{

void Require(bool condition, const char* message)
{
    if (condition)
        return;
    std::fprintf(stderr, "vulkan-queue-sharing-experiment-tests: %s\n", message);
    std::exit(EXIT_FAILURE);
}

} // namespace

int main()
{
    const auto universal = MakeVulkanQueueSharingPlan(false, true, true);
    Require(universal.Mode == VulkanQueueSharingMode::Exclusive,
        "universal queue remains exclusive");
    Require(!universal.UseOwnershipTransfer(),
        "universal queue has no ownership transfer");

    const auto splitDefault = MakeVulkanQueueSharingPlan(true, false, false);
    Require(splitDefault.Mode == VulkanQueueSharingMode::Concurrent,
        "split queue defaults to concurrent");
    Require(!splitDefault.UseOwnershipTransfer(),
        "split default has no ownership transfer");

    const auto splitWithoutGate = MakeVulkanQueueSharingPlan(true, true, false);
    Require(splitWithoutGate.Mode == VulkanQueueSharingMode::Concurrent,
        "request without SyncVal gate stays concurrent");
    Require(!splitWithoutGate.UseOwnershipTransfer(),
        "request without SyncVal gate cannot enable transfer");

    const auto splitExperiment = MakeVulkanQueueSharingPlan(true, true, true);
    Require(splitExperiment.Mode == VulkanQueueSharingMode::Exclusive,
        "gated split experiment selects exclusive");
    Require(splitExperiment.UseOwnershipTransfer(),
        "gated split experiment requires ownership transfer");

    const auto splitGateOnly = MakeVulkanQueueSharingPlan(true, false, true);
    Require(splitGateOnly.Mode == VulkanQueueSharingMode::Concurrent,
        "SyncVal gate alone does not change production mode");

    std::puts("vulkan-queue-sharing-experiment-tests: PASS");
    return EXIT_SUCCESS;
}
