#include "VulkanMemoryAdmission.h"

#include <cstdio>

using melonDS::Vk::EvaluateVulkanMemoryAdmission;
using melonDS::Vk::VulkanMemoryAdmissionRequest;
using melonDS::Vk::VulkanMemoryAdmissionSnapshot;

namespace
{

bool Require(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "vulkan-memory-admission-tests: FAIL: %s\n", message);
    return condition;
}

} // namespace

int main()
{
    bool ok = true;
    VkDeviceSize previousDemand = 0;
    for (const int scale : {1, 4, 8, 16})
    {
        const VkDeviceSize pixels =
            static_cast<VkDeviceSize>(256 * scale) * (192 * scale);
        const VkDeviceSize tileBytes =
            4ull * (scale >= 9 ? 32ull : (scale >= 5 ? 16ull : 8ull))
            * (scale >= 9 ? 32ull : (scale >= 5 ? 16ull : 8ull))
            * ((pixels / (scale >= 9 ? 32ull * 32ull
                : (scale >= 5 ? 16ull * 16ull : 8ull * 8ull))) * 16ull);
        const VkDeviceSize demand =
            tileBytes * 3ull + pixels * (3ull * 2ull * 4ull + 4ull);
        ok &= Require(demand > previousDemand,
            "1x/4x/8x/16x projected scale demand must grow monotonically");
        previousDemand = demand;
    }

    VulkanMemoryAdmissionSnapshot live{};
    live.HasLiveBudget = true;
    live.HeapCount = 2;
    live.MemoryTypeCount = 3;
    live.HeapSize[0] = 2ull * melonDS::Vk::MemoryMiB * 1024ull;
    live.HeapSize[1] = 8ull * melonDS::Vk::MemoryMiB * 1024ull;
    live.HeapBudget[0] = 1ull * melonDS::Vk::MemoryMiB * 1024ull;
    live.HeapBudget[1] = 6ull * melonDS::Vk::MemoryMiB * 1024ull;
    live.HeapUsage[0] = 100ull * melonDS::Vk::MemoryMiB;
    live.HeapUsage[1] = 2ull * melonDS::Vk::MemoryMiB * 1024ull;
    live.HeapDeviceLocal[0] = true;
    live.HeapDeviceLocal[1] = true;
    live.MemoryTypeHeapIndex[0] = 0;
    live.MemoryTypeHeapIndex[1] = 1;
    live.MemoryTypeHeapIndex[2] = 1;
    live.MaxMemoryAllocationSize = 512ull * melonDS::Vk::MemoryMiB;
    live.MaxMemoryAllocationCount = 64;
    live.CurrentAllocationCount = 4;

    const VulkanMemoryAdmissionRequest accepted{
        128ull * melonDS::Vk::MemoryMiB,
        0,
        128ull * melonDS::Vk::MemoryMiB,
        2,
        1,
    };
    ok &= Require(EvaluateVulkanMemoryAdmission(live, accepted).Accepted,
        "live multi-heap admission should use the mapped heap");

    auto largestRejected = accepted;
    largestRejected.LargestAllocation = 513ull * melonDS::Vk::MemoryMiB;
    ok &= Require(!EvaluateVulkanMemoryAdmission(live, largestRejected).Accepted,
        "maxMemoryAllocationSize boundary must refuse");

    auto countRejected = accepted;
    countRejected.AdditionalAllocationCount = 61;
    ok &= Require(!EvaluateVulkanMemoryAdmission(live, countRejected).Accepted,
        "maxMemoryAllocationCount boundary must refuse");

    auto budgetRejected = accepted;
    budgetRejected.ProjectedBytes = 4ull * melonDS::Vk::MemoryMiB * 1024ull;
    ok &= Require(!EvaluateVulkanMemoryAdmission(live, budgetRejected).Accepted,
        "live heap budget boundary must refuse without clamping");

    VulkanMemoryAdmissionSnapshot fallback = live;
    fallback.HasLiveBudget = false;
    fallback.HeapBudget[1] = fallback.HeapSize[1] * 3ull / 4ull;
    auto fallbackRequest = accepted;
    fallbackRequest.ProjectedBytes = fallback.HeapBudget[1] + 1;
    ok &= Require(!EvaluateVulkanMemoryAdmission(fallback, fallbackRequest).Accepted,
        "no-extension fallback must retain the 75% heuristic");

    if (!ok)
        return 1;
    std::printf("vulkan-memory-admission-tests: PASS\n");
    return 0;
}
