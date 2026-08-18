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

#ifndef VULKAN_FEATURE_PROBE_H
#define VULKAN_FEATURE_PROBE_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <string>
#include <vector>

#include "VulkanCommon.h"
#include "VulkanDescriptors.h"
#include "VulkanLoader.h"
#include "VulkanMemoryAdmission.h"

namespace melonDS::Vk
{

// ---------------------------------------------------------------------------
// The one place that decides whether a Vulkan runtime and a physical device
// can run the MelonPrime compute rasterizer, and the one place that explains
// why not.
//
// Every check appends a ProbeFinding with a human-readable requirement name and
// the actual value observed, so a rejection is never "Vulkan unavailable" -- it
// is e.g. "maxStorageBufferRange: device reports 134217728 bytes, 805306368
// needed for 16x internal resolution".
//
// There is no silent fallback anywhere in this file: a failed requirement makes
// the device ineligible, and the caller is expected to surface the reason.
// ---------------------------------------------------------------------------

inline constexpr u32 MinimumApiVersion = VK_API_VERSION_1_1;

// Baseline the renderer targets. 1.1 rather than 1.0 because the backend uses
// the core (non-KHR) Get*Properties2/Features2 entry points and relies on 1.1's
// guaranteed VK_KHR_maintenance1/2/3 and 16-bit storage promotion. 1.1 rather
// than 1.2/1.3 because 1.1 is what the oldest still-supported Windows and Linux
// drivers on the target hardware expose, and nothing in the compute rasterizer
// needs a later core feature: dynamic rendering, timeline semaphores and
// Synchronization2 are all deliberately unused.

// Highest internal resolution multiplier the frontend offers.
inline constexpr int MaxSupportedScaleFactor = 16;

// Minimum compute workgroup invocations any eligible device must support.
// GPU3D_Compute dispatches its rasterise stage as TileSize x TileSize, and
// TileSize is 8 at 1x-4x, 16 at 5x-8x and 32 from 9x up, so 256 covers
// everything through 8x and the probe derives the higher scale limits from the
// same number instead of assuming them.
inline constexpr u32 MinComputeWorkGroupInvocations = 256;

// The DS 3D framebuffer at 16x is 4096x3072; a device that cannot make a
// 4096-wide image cannot present the top scale at all.
inline constexpr u32 MinImageDimension2D = 4096;


// Result of a single requirement check.
struct ProbeFinding
{
    bool Passed = false;
    std::string Requirement;    // "Vulkan API version", "VK_KHR_swapchain", ...
    std::string Detail;         // observed value / why it failed
};

// Ordered list of findings. Findings are appended in check order, so the log
// reads as the sequence of things that were verified.
class ProbeReport
{
public:
    void Pass(std::string requirement, std::string detail);
    void Fail(std::string requirement, std::string detail);

    [[nodiscard]] bool Ok() const noexcept { return FailureCount == 0; }
    [[nodiscard]] u32 GetFailureCount() const noexcept { return FailureCount; }
    [[nodiscard]] const std::vector<ProbeFinding>& GetFindings() const noexcept { return Findings; }

    // First failure, formatted as "<requirement>: <detail>". Empty when Ok().
    // This is the string the frontend shows as the renderer failure reason.
    [[nodiscard]] std::string FirstFailure() const;

    // All failures joined with "; ". Empty when Ok().
    [[nodiscard]] std::string AllFailures() const;

    // Writes every finding to the log. Failures go out at Error level and
    // passes at `passLevel`, so a successful startup can log at Info while a
    // rejection is always loud.
    void Log(const char* prefix, Platform::LogLevel passLevel) const;

    void Clear() noexcept;

private:
    std::vector<ProbeFinding> Findings;
    u32 FailureCount = 0;
};


// Queue family assignment.
//
// The renderer wants a single family that can do graphics, compute and present,
// and a single queue from it. That is not a simplification for its own sake:
// with one queue there is no queue-family ownership transfer on any resource,
// so every barrier in the frame is a plain memory/layout barrier and the
// compute output can be read by the presenter with nothing more than an
// execution+memory dependency. Every desktop driver on the target platforms
// exposes such a family.
//
// If no universal family exists, the split members below are filled in and
// RequiresOwnershipTransfer() reports true; the renderer must then perform real
// VkImageMemoryBarrier / VkBufferMemoryBarrier release+acquire pairs with
// srcQueueFamilyIndex/dstQueueFamilyIndex set.
struct QueueFamilySelection
{
    static constexpr u32 InvalidFamily = ~0u;

    u32 UniversalFamily = InvalidFamily;
    u32 GraphicsFamily = InvalidFamily;
    u32 ComputeFamily = InvalidFamily;
    u32 PresentFamily = InvalidFamily;

    // True when present support could not be evaluated because the probe ran
    // without a surface (the settings dialog runs headless). In that state
    // PresentFamily is InvalidFamily and the caller must re-run the queue
    // selection once it owns a surface.
    bool PresentUnknown = true;

    [[nodiscard]] bool HasUniversalFamily() const noexcept { return UniversalFamily != InvalidFamily; }

    [[nodiscard]] bool RequiresOwnershipTransfer() const noexcept
    {
        if (HasUniversalFamily())
            return false;
        if (GraphicsFamily == InvalidFamily || ComputeFamily == InvalidFamily)
            return false;   // incomplete selection; the probe already failed
        if (GraphicsFamily != ComputeFamily)
            return true;
        return !PresentUnknown && PresentFamily != InvalidFamily && PresentFamily != GraphicsFamily;
    }

    // Distinct families that must appear in VkDeviceQueueCreateInfo.
    [[nodiscard]] std::vector<u32> GetDistinctFamilies() const;
};


// Memory footprint and limit demand of the compute rasterizer at one internal
// resolution. Mirrors ComputeRenderer3D::SetRenderSettings; the derivation is
// duplicated rather than shared because GPU3D_Compute.h drags in the OpenGL
// headers, which core's Vulkan translation units must not see.
struct ResolutionBudget
{
    int ScaleFactor = 1;

    u32 TileSize = 8;
    u32 ScreenWidth = 256;
    u32 ScreenHeight = 192;
    u32 TilesPerLine = 32;
    u32 TileLines = 24;
    u32 MaxWorkTiles = 0;

    // Invocations in the rasterise stage's workgroup (TileSize x TileSize).
    u32 ComputeInvocationsPerGroup = 64;

    // Elements in the SetupIndices uniform texel buffer.
    u32 TexelBufferElements = 0;

    // Largest single VkBuffer, which is what maxStorageBufferRange gates.
    VkDeviceSize LargestStorageBuffer = 0;

    // Largest allocation and projected scale-dependent device-local demand.
    // These are admission inputs, not a silent scale clamp: a rejected scale
    // remains a hard failure with the observed budget in the log.
    VkDeviceSize LargestDeviceAllocation = 0;
    VkDeviceSize ProjectedDeviceLocalBytes = 0;
    u32 ProjectedAllocationCount = 0;

    // Sum of every rasterizer allocation, which is what the device-local heap
    // has to hold.
    VkDeviceSize TotalDeviceBytes = 0;

    [[nodiscard]] static ResolutionBudget ForScaleFactor(int scaleFactor) noexcept;
};


// Everything the probe learned about one physical device.
struct DeviceProbeResult
{
    VkPhysicalDevice Handle = VK_NULL_HANDLE;

    VkPhysicalDeviceProperties Properties{};
    VkPhysicalDeviceFeatures Features{};
    VkPhysicalDeviceMemoryProperties MemoryProperties{};

    std::string DeviceName;
    std::string VendorName;
    std::string ApiVersionText;
    std::string DriverVersionText;

    QueueFamilySelection Queues;
    ProbeReport Report;

    // Highest internal resolution this device's limits and device-local memory
    // can actually run, 0 when the device is not eligible at all.
    int MaxScaleFactor = 0;

    // Sum of the sizes of all VK_MEMORY_HEAP_DEVICE_LOCAL_BIT heaps.
    VkDeviceSize DeviceLocalMemory = 0;

    VulkanMemoryAdmissionSnapshot MemoryAdmission;

    // VK_KHR_portability_subset was reported. The device is then a portability
    // implementation (MoltenVK), and the extension MUST be enabled at device
    // creation -- the spec makes that mandatory, not optional.
    bool RequiresPortabilitySubset = false;

    // Optional, reported for the startup log rather than required.
    bool HasDebugMarkerSupport = false;
    bool HasNvLowLatency2 = false;
    bool HasAmdAntiLag = false;
    bool HasDeviceFault = false;

    // Higher is better. Discrete GPUs outrank integrated, which outrank
    // software rasterizers; ties break on supported internal resolution.
    int Score = 0;

    [[nodiscard]] bool IsEligible() const noexcept { return Report.Ok() && MaxScaleFactor > 0; }
};


class FeatureProbe
{
public:
    // Loader + instance-level requirements. Fills `report` with: runtime
    // present, instance API version, and availability of every instance
    // extension in `requiredInstanceExtensions`.
    //
    // `library` must already be Open(); this does not open it, because the
    // caller owns the loader's lifetime.
    static bool CheckInstanceRequirements(
        const Library& library,
        const std::vector<const char*>& requiredInstanceExtensions,
        ProbeReport& report);

    // Runs every device-level check.
    //
    // `surface` may be VK_NULL_HANDLE, in which case present support is not
    // checked and QueueFamilySelection::PresentUnknown stays true. That is the
    // headless settings-dialog path; it is reported as "not checked", never as
    // "supported".
    static DeviceProbeResult ProbeDevice(
        const InstanceDispatch& fns,
        VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface);

    // Refreshes optional memory-budget and allocation-limit capabilities for a
    // live logical device without probing queues or recreating the instance.
    static VulkanMemoryAdmissionSnapshot QueryMemoryAdmission(
        const InstanceDispatch& fns,
        VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceProperties& properties,
        const VkPhysicalDeviceMemoryProperties& memoryProperties);

    // Device extensions every eligible device must expose.
    static std::vector<const char*> GetRequiredDeviceExtensions(bool needPresent);

    // Availability query used by both the probe and device creation.
    static bool EnumerateDeviceExtensions(
        const InstanceDispatch& fns,
        VkPhysicalDevice physicalDevice,
        std::vector<VkExtensionProperties>& out);

    static bool HasExtension(
        const std::vector<VkExtensionProperties>& available, const char* name) noexcept;
};

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_FEATURE_PROBE_H
