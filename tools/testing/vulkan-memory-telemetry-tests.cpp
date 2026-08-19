/*
    Pure tests for the Vulkan allocation telemetry model. No Vulkan loader,
    device, driver, or window system is needed.
*/

#include <cstdio>
#include <cstdlib>

#include "VulkanMemoryTelemetry.h"

using melonDS::Vk::VulkanMemoryTelemetry;
using melonDS::Vk::VulkanMemoryTelemetryBucketCount;

namespace
{

void Require(bool condition, const char* message)
{
    if (condition)
        return;
    std::fprintf(stderr, "vulkan-memory-telemetry-tests: %s\n", message);
    std::exit(EXIT_FAILURE);
}

} // namespace

int main()
{
    constexpr VkDeviceSize MiB = 1024ull * 1024ull;
    VulkanMemoryTelemetry telemetry;

    auto snapshot = telemetry.GetSnapshot();
    Require(snapshot.CurrentAllocationCount == 0, "initial allocation count");
    Require(snapshot.PeakAllocationCount == 0, "initial peak count");
    Require(snapshot.TotalAllocationCount == 0, "initial allocation total");

    telemetry.RecordAllocation(0, 512ull * 1024ull);
    telemetry.RecordAllocation(1, 5ull * MiB);
    telemetry.RecordAllocation(0, 2ull * 1024ull * MiB);
    snapshot = telemetry.GetSnapshot();

    Require(snapshot.CurrentAllocationCount == 3, "current allocation count");
    Require(snapshot.PeakAllocationCount == 3, "peak allocation count");
    Require(snapshot.TotalAllocationCount == 3, "allocation total");
    Require(snapshot.CurrentBytes[0] == 512ull * 1024ull + 2ull * 1024ull * MiB,
        "heap 0 current bytes");
    Require(snapshot.CurrentBytes[1] == 5ull * MiB, "heap 1 current bytes");
    Require(snapshot.PeakBytes[0] == snapshot.CurrentBytes[0], "heap 0 peak bytes");
    Require(snapshot.LargestAllocation == 2ull * 1024ull * MiB,
        "largest allocation");
    Require(snapshot.AllocationSizeBuckets[0] == 1, "small allocation bucket");
    Require(snapshot.AllocationSizeBuckets[2] == 1, "medium allocation bucket");
    Require(snapshot.AllocationSizeBuckets[6] == 1, "large allocation bucket");

    telemetry.RecordFree(1, 5ull * MiB);
    snapshot = telemetry.GetSnapshot();
    Require(snapshot.CurrentAllocationCount == 2, "count after free");
    Require(snapshot.CurrentBytes[1] == 0, "heap bytes after free");
    Require(snapshot.PeakBytes[1] == 5ull * MiB, "peak survives free");
    Require(snapshot.TotalFreeCount == 1, "free total");
    Require(snapshot.TotalFreedBytes == 5ull * MiB, "freed bytes");

    telemetry.RecordFree(0, 512ull * 1024ull);
    telemetry.RecordFree(0, 2ull * 1024ull * MiB);
    snapshot = telemetry.GetSnapshot();
    Require(snapshot.CurrentAllocationCount == 0, "all allocations released");
    Require(snapshot.CurrentBytes[0] == 0, "all heap bytes released");
    Require(snapshot.PeakAllocationCount == 3, "peak remains after release");
    Require(snapshot.TotalFreeCount == 3, "all frees counted");
    Require(snapshot.TotalAllocatedBytes == 512ull * 1024ull + 5ull * MiB
            + 2ull * 1024ull * MiB,
        "total allocated bytes");
    Require(snapshot.TotalFreedBytes == snapshot.TotalAllocatedBytes,
        "total freed bytes");
    Require(snapshot.AllocationSizeBuckets.size() == VulkanMemoryTelemetryBucketCount,
        "bucket count is stable");

    telemetry.RecordFree(99, 1);
    Require(telemetry.GetSnapshot().CurrentAllocationCount == 0,
        "free underflow is clamped");

    std::puts("vulkan-memory-telemetry-tests: PASS");
    return EXIT_SUCCESS;
}
