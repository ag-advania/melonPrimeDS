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

#include "VulkanFeatureProbe.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace melonDS::Vk
{

namespace
{

// --- constants mirrored from the OpenGL compute rasterizer -----------------
//
// src/GPU3D_Compute.{h,cpp} is the algorithmic source of truth, but its header
// includes OpenGLSupport.h, so these translation units cannot include it. The
// values below are copies with the derivation spelled out; if GPU3D_Compute
// changes its tiling, this block has to change with it.

constexpr u32 CoarseTileCountX = 8;             // GPU3D_Compute.h
constexpr u32 BinStride = 2048 / 32;            // GPU3D_Compute.h
constexpr u32 CoarseBinStride = BinStride / 32; // GPU3D_Compute.h
constexpr u32 MaxVariants = 256;                // GPU3D_Compute.h

// sizeof(ComputeRenderer3D::BinResultHeader): VariantWorkCount[256*4] +
// SortedWorkOffset[256] + VariantWorkRealCount[256] + SortWorkWorkCount[4],
// all u32.
constexpr VkDeviceSize BinResultHeaderBytes =
    (static_cast<VkDeviceSize>(MaxVariants) * 4 + MaxVariants + MaxVariants + 4) * 4;

// sizeof(SpanSetupX) and sizeof(SpanSetupY) from GPU3D_Compute.h: 24 and 31
// 4-byte scalars respectively, no padding.
constexpr VkDeviceSize SpanSetupXBytes = 24 * 4;
constexpr VkDeviceSize SpanSetupYBytes = 31 * 4;
constexpr VkDeviceSize MaxYSpanSetups = 6144 * 2;   // GPU3D_Compute.h

// GPU3D_Compute.cpp: maxYSpanIndices = 64 * 2048 * ScaleFactor.
constexpr VkDeviceSize YSpanIndicesPerScale = 64 * 2048;

// RenderPolygons[2048], u32-sized fields; the polygon SSBO mirrors it.
constexpr VkDeviceSize RenderPolygonCount = 2048;
constexpr VkDeviceSize RenderPolygonBytes = 12 * 4;  // GPU3D_Compute.h RenderPolygon

// GPU3D_Texcache.h: texture arrays are capped at 64 layers and 1024x1024.
constexpr u32 TexcacheMaxArrayLayers = 64;
constexpr u32 TexcacheMaxDimension = 1024;

// std140 pads every element of a scalar array to 16 bytes, so MetaUniform's
// ToonTable[4*34] alone occupies 136*16 = 2176 bytes; with the surrounding
// scalars the block lands just above 2 KiB. 4096 is that rounded to the next
// power of two and is the figure the probe demands.
constexpr u32 MetaUniformBytes = 4096;

// Formats the backend creates images and texel buffer views with.
constexpr VkFormat TexcacheFormat = VK_FORMAT_R8G8B8A8_UINT;    // usampler2DArray
constexpr VkFormat ClearBitmapFormat = VK_FORMAT_R32_UINT;      // usampler2D, GL_R32UI equivalent
constexpr VkFormat CaptureFormat = VK_FORMAT_R8G8B8A8_UNORM;    // sampler2DArray

// Fraction of a device-local heap the rasterizer is allowed to claim. The
// texcache, the swapchain, the compositor targets and whatever else is
// resident on the GPU all come out of the same heap, and a Vulkan allocation
// that fits on paper but evicts the desktop compositor is not a working
// configuration. 3/4 is a budget, not a measurement, and is reported as such.
constexpr VkDeviceSize DeviceMemoryBudgetNumerator = 3;
constexpr VkDeviceSize DeviceMemoryBudgetDenominator = 4;

std::string FormatU64(VkDeviceSize value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    return std::string(buffer);
}

std::string FormatMiB(VkDeviceSize bytes)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.1f MiB",
        static_cast<double>(bytes) / (1024.0 * 1024.0));
    return std::string(buffer);
}

std::string FormatLimit(const char* what, u64 actual, u64 required)
{
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer), "%s = %llu (need %llu)",
        what,
        static_cast<unsigned long long>(actual),
        static_cast<unsigned long long>(required));
    return std::string(buffer);
}

const char* DeviceTypeName(VkPhysicalDeviceType type) noexcept
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
    default:                                     return "other";
    }
}

// Checks one numeric limit and records the finding either way.
bool RequireLimit(ProbeReport& report, const char* requirement, u64 actual, u64 required)
{
    const std::string detail = FormatLimit(requirement, actual, required);
    if (actual >= required)
    {
        report.Pass(requirement, detail);
        return true;
    }
    report.Fail(requirement, detail);
    return false;
}

// Format feature check. `features` are the bits the backend needs; missing bits
// are named individually so the log says which capability is absent.
struct FormatFeatureBit { VkFormatFeatureFlagBits Bit; const char* Name; };

constexpr FormatFeatureBit FormatFeatureNames[] = {
    { VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,              "SAMPLED_IMAGE" },
    { VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT,              "STORAGE_IMAGE" },
    { VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT,       "UNIFORM_TEXEL_BUFFER" },
    { VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT,       "STORAGE_TEXEL_BUFFER" },
    { VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,           "COLOR_ATTACHMENT" },
    { VK_FORMAT_FEATURE_BLIT_SRC_BIT,                   "BLIT_SRC" },
    { VK_FORMAT_FEATURE_BLIT_DST_BIT,                   "BLIT_DST" },
    { VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "SAMPLED_IMAGE_FILTER_LINEAR" },
    { VK_FORMAT_FEATURE_TRANSFER_SRC_BIT,               "TRANSFER_SRC" },
    { VK_FORMAT_FEATURE_TRANSFER_DST_BIT,               "TRANSFER_DST" },
};

std::string DescribeMissingFeatures(VkFormatFeatureFlags missing)
{
    std::string out;
    for (const FormatFeatureBit& entry : FormatFeatureNames)
    {
        if (missing & entry.Bit)
        {
            if (!out.empty())
                out += ", ";
            out += entry.Name;
        }
    }
    if (out.empty())
        out = "<unnamed feature bits>";
    return out;
}

enum class FormatDomain { OptimalTiling, Buffer };

bool RequireFormat(
    const InstanceDispatch& fns,
    VkPhysicalDevice device,
    ProbeReport& report,
    const char* requirement,
    VkFormat format,
    FormatDomain domain,
    VkFormatFeatureFlags needed)
{
    VkFormatProperties props{};
    fns.GetPhysicalDeviceFormatProperties(device, format, &props);

    const VkFormatFeatureFlags have =
        (domain == FormatDomain::Buffer) ? props.bufferFeatures : props.optimalTilingFeatures;

    const VkFormatFeatureFlags missing = needed & ~have;
    if (missing == 0)
    {
        report.Pass(requirement, "supported");
        return true;
    }

    report.Fail(requirement,
        std::string("missing ") + DescribeMissingFeatures(missing));
    return false;
}

} // namespace


// ---------------------------------------------------------------------------
// ProbeReport
// ---------------------------------------------------------------------------

void ProbeReport::Pass(std::string requirement, std::string detail)
{
    Findings.push_back(ProbeFinding{true, std::move(requirement), std::move(detail)});
}

void ProbeReport::Fail(std::string requirement, std::string detail)
{
    Findings.push_back(ProbeFinding{false, std::move(requirement), std::move(detail)});
    FailureCount++;
}

std::string ProbeReport::FirstFailure() const
{
    for (const ProbeFinding& finding : Findings)
    {
        if (!finding.Passed)
            return finding.Requirement + ": " + finding.Detail;
    }
    return std::string();
}

std::string ProbeReport::AllFailures() const
{
    std::string out;
    for (const ProbeFinding& finding : Findings)
    {
        if (finding.Passed)
            continue;
        if (!out.empty())
            out += "; ";
        out += finding.Requirement + ": " + finding.Detail;
    }
    return out;
}

void ProbeReport::Log(const char* prefix, Platform::LogLevel passLevel) const
{
    for (const ProbeFinding& finding : Findings)
    {
        Platform::Log(
            finding.Passed ? passLevel : Platform::LogLevel::Error,
            "[Vulkan] %s %s %s -- %s\n",
            prefix ? prefix : "",
            finding.Passed ? "ok  " : "FAIL",
            finding.Requirement.c_str(),
            finding.Detail.c_str());
    }
}

void ProbeReport::Clear() noexcept
{
    Findings.clear();
    FailureCount = 0;
}


// ---------------------------------------------------------------------------
// QueueFamilySelection
// ---------------------------------------------------------------------------

std::vector<u32> QueueFamilySelection::GetDistinctFamilies() const
{
    std::vector<u32> families;

    const auto add = [&families](u32 family) {
        if (family == InvalidFamily)
            return;
        if (std::find(families.begin(), families.end(), family) == families.end())
            families.push_back(family);
    };

    if (HasUniversalFamily())
    {
        add(UniversalFamily);
        return families;
    }

    add(GraphicsFamily);
    add(ComputeFamily);
    add(PresentFamily);
    return families;
}


// ---------------------------------------------------------------------------
// ResolutionBudget
// ---------------------------------------------------------------------------

ResolutionBudget ResolutionBudget::ForScaleFactor(int scaleFactor) noexcept
{
    ResolutionBudget budget;

    if (scaleFactor < 1)
        scaleFactor = 1;
    budget.ScaleFactor = scaleFactor;

    const u32 scale = static_cast<u32>(scaleFactor);

    // GPU3D_Compute.cpp SetRenderSettings(): tiles grow one step at 5x and
    // again at 9x, so TileSize is 8 / 16 / 32 and the tile grid shift is
    // 3 / 4 / 5.
    const u32 range = (scale >= 5 ? 1u : 0u) + (scale >= 9 ? 1u : 0u);
    const u32 tileShift = 3 + range;

    budget.TileSize = 8u << range;
    budget.ScreenWidth = 256 * scale;
    budget.ScreenHeight = 192 * scale;
    budget.TilesPerLine = budget.ScreenWidth >> tileShift;
    budget.TileLines = budget.ScreenHeight >> tileShift;
    budget.MaxWorkTiles = (budget.TilesPerLine * budget.TileLines) << 4;
    budget.ComputeInvocationsPerGroup = budget.TileSize * budget.TileSize;

    const VkDeviceSize screenPixels =
        static_cast<VkDeviceSize>(budget.ScreenWidth) * budget.ScreenHeight;
    const VkDeviceSize workTiles = budget.MaxWorkTiles;
    const VkDeviceSize tileGrid =
        static_cast<VkDeviceSize>(budget.TilesPerLine) * budget.TileLines;

    // One of ColorTiles / DepthTiles / AttrTiles. 4 bytes per pixel per work
    // tile; this is by far the largest allocation and the one that decides
    // whether a scale factor is reachable at all.
    const VkDeviceSize tileMemoryBytes =
        4 * static_cast<VkDeviceSize>(budget.TileSize) * budget.TileSize * workTiles;

    // ResultBuffer: colour + depth + attributes, two layers each.
    const VkDeviceSize resultBytes = 4 * 3 * 2 * screenPixels;

    const VkDeviceSize binResultBytes =
        BinResultHeaderBytes + tileGrid * (CoarseBinStride + BinStride + BinStride) * 4;

    // Unsorted + sorted work descriptors, uvec2 each.
    const VkDeviceSize workDescBytes = workTiles * 2 * 4 * 2;

    const VkDeviceSize ySpanIndices = YSpanIndicesPerScale * scale;
    budget.TexelBufferElements = static_cast<u32>(
        std::min<VkDeviceSize>(ySpanIndices, 0xFFFFFFFFull));

    const VkDeviceSize xSpanSetupBytes = SpanSetupXBytes * ySpanIndices;
    const VkDeviceSize ySpanSetupBytes = SpanSetupYBytes * MaxYSpanSetups;
    const VkDeviceSize setupIndicesBytes = ySpanIndices * 8;   // RGBA16UI
    const VkDeviceSize polygonBytes = RenderPolygonBytes * RenderPolygonCount;
    const VkDeviceSize finalFramebufferBytes = 4 * screenPixels;

    // The compositor writes both screens, at the internal resolution, as BGRA8.
    // This is a storage buffer like the rest, so maxStorageBufferRange gates it
    // too, and it is the only presentation-stage allocation that grows with the
    // scale factor -- the native capture resolve and the structured 2D input are
    // fixed at 256x192 by definition.
    const VkDeviceSize composeOutputBytes = 4 * 2 * screenPixels;

    // Retained Display Capture: four physical VRAM banks, two generations per
    // bank for same-bank read-before-write, and ScaleFactor^2 samples for each
    // native 256x256 cell.
    const VkDeviceSize captureSidecarBytes =
        4ull * 2ull * 256ull * 256ull * scale * scale * sizeof(u32);

    budget.LargestStorageBuffer = std::max({
        tileMemoryBytes,
        resultBytes,
        binResultBytes,
        workDescBytes,
        xSpanSetupBytes,
        ySpanSetupBytes,
        polygonBytes,
        composeOutputBytes,
        captureSidecarBytes,
    });

    budget.TotalDeviceBytes =
        tileMemoryBytes * 3
        + resultBytes
        + binResultBytes
        + workDescBytes
        + xSpanSetupBytes
        + ySpanSetupBytes
        + setupIndicesBytes
        + polygonBytes
        + finalFramebufferBytes
        + composeOutputBytes
        + captureSidecarBytes
        + MetaUniformBytes;

    return budget;
}


// ---------------------------------------------------------------------------
// FeatureProbe
// ---------------------------------------------------------------------------

bool FeatureProbe::CheckInstanceRequirements(
    const Library& library,
    const std::vector<const char*>& requiredInstanceExtensions,
    ProbeReport& report)
{
    if (!library.IsOpen())
    {
        report.Fail("Vulkan runtime",
            library.GetFailureReason().empty()
                ? std::string("the Vulkan loader has not been opened")
                : library.GetFailureReason());
        return false;
    }
    report.Pass("Vulkan runtime", library.GetLibraryName());

    const u32 instanceVersion = library.GetInstanceVersion();
    if (instanceVersion < MinimumApiVersion)
    {
        report.Fail("Vulkan instance version",
            "runtime reports " + FormatApiVersion(instanceVersion)
            + ", " + FormatApiVersion(MinimumApiVersion) + " required");
        return false;
    }
    report.Pass("Vulkan instance version", FormatApiVersion(instanceVersion));

    // Enumerate the implicit (layer-less) instance extension set. Layers can
    // add more, but the backend must not depend on a layer being installed, so
    // only the null-layer set counts.
    u32 count = 0;
    VkResult res = library.Global().EnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    if (res != VK_SUCCESS)
    {
        report.Fail("Instance extension enumeration", FormatResult(res));
        return false;
    }

    std::vector<VkExtensionProperties> available(count);
    if (count > 0)
    {
        res = library.Global().EnumerateInstanceExtensionProperties(nullptr, &count, available.data());
        // VK_INCOMPLETE here would mean the list grew between the two calls,
        // which would silently hide an extension; treat it as a failure rather
        // than probing a truncated list.
        if (res != VK_SUCCESS)
        {
            report.Fail("Instance extension enumeration", FormatResult(res));
            return false;
        }
    }

    bool ok = true;
    for (const char* name : requiredInstanceExtensions)
    {
        if (!name)
            continue;

        const bool found = HasExtension(available, name);
        if (found)
            report.Pass(name, "present");
        else
            report.Fail(name, "not exposed by the Vulkan runtime");
        ok = ok && found;
    }

    return ok;
}


std::vector<const char*> FeatureProbe::GetRequiredDeviceExtensions(bool needPresent)
{
    std::vector<const char*> extensions;

    // The only hard device extension. Everything else the backend uses is
    // Vulkan 1.1 core, which is the point of the 1.1 baseline: no
    // VK_KHR_maintenance*, no VK_KHR_get_memory_requirements2, no
    // VK_KHR_bind_memory2 in the required set.
    if (needPresent)
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    return extensions;
}


bool FeatureProbe::EnumerateDeviceExtensions(
    const InstanceDispatch& fns,
    VkPhysicalDevice physicalDevice,
    std::vector<VkExtensionProperties>& out)
{
    out.clear();

    u32 count = 0;
    VkResult res = fns.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    if (res != VK_SUCCESS)
        return false;

    out.resize(count);
    if (count == 0)
        return true;

    res = fns.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, out.data());
    if (res != VK_SUCCESS)
    {
        out.clear();
        return false;
    }
    return true;
}


bool FeatureProbe::HasExtension(
    const std::vector<VkExtensionProperties>& available, const char* name) noexcept
{
    if (!name)
        return false;

    for (const VkExtensionProperties& entry : available)
    {
        if (std::strncmp(entry.extensionName, name, VK_MAX_EXTENSION_NAME_SIZE) == 0)
            return true;
    }
    return false;
}


DeviceProbeResult FeatureProbe::ProbeDevice(
    const InstanceDispatch& fns,
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface)
{
    DeviceProbeResult result;
    result.Handle = physicalDevice;

    if (physicalDevice == VK_NULL_HANDLE)
    {
        result.Report.Fail("Physical device", "null handle");
        return result;
    }

    fns.GetPhysicalDeviceProperties(physicalDevice, &result.Properties);
    fns.GetPhysicalDeviceFeatures(physicalDevice, &result.Features);
    fns.GetPhysicalDeviceMemoryProperties(physicalDevice, &result.MemoryProperties);

    result.DeviceName = result.Properties.deviceName;
    result.VendorName = FormatVendor(result.Properties.vendorID);
    result.ApiVersionText = FormatApiVersion(result.Properties.apiVersion);
    result.DriverVersionText =
        FormatDriverVersion(result.Properties.vendorID, result.Properties.driverVersion);

    const VkPhysicalDeviceLimits& limits = result.Properties.limits;

    // --- API version --------------------------------------------------------
    if (result.Properties.apiVersion < MinimumApiVersion)
    {
        result.Report.Fail("Device API version",
            result.ApiVersionText + " (need " + FormatApiVersion(MinimumApiVersion) + ")");
    }
    else
    {
        result.Report.Pass("Device API version", result.ApiVersionText);
    }

    // --- device-local memory ------------------------------------------------
    // Summed over heaps rather than taken from the first one: a discrete GPU
    // can expose several device-local heaps, and on rebar/UMA systems a
    // host-visible heap is also device-local.
    for (u32 i = 0; i < result.MemoryProperties.memoryHeapCount; i++)
    {
        if (result.MemoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            result.DeviceLocalMemory += result.MemoryProperties.memoryHeaps[i].size;
    }
    if (result.DeviceLocalMemory == 0)
    {
        result.Report.Fail("Device-local memory", "no VK_MEMORY_HEAP_DEVICE_LOCAL_BIT heap");
    }
    else
    {
        result.Report.Pass("Device-local memory", FormatMiB(result.DeviceLocalMemory));
    }

    // --- queue families -----------------------------------------------------
    {
        u32 familyCount = 0;
        fns.GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);

        std::vector<VkQueueFamilyProperties> families(familyCount);
        if (familyCount > 0)
            fns.GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

        const bool checkPresent = (surface != VK_NULL_HANDLE)
            && fns.GetPhysicalDeviceSurfaceSupportKHR != nullptr;

        QueueFamilySelection& queues = result.Queues;
        queues.PresentUnknown = !checkPresent;

        for (u32 i = 0; i < familyCount; i++)
        {
            const VkQueueFlags flags = families[i].queueFlags;
            if (families[i].queueCount == 0)
                continue;

            const bool graphics = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
            const bool compute = (flags & VK_QUEUE_COMPUTE_BIT) != 0;

            bool present = false;
            if (checkPresent)
            {
                VkBool32 supported = VK_FALSE;
                const VkResult res =
                    fns.GetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supported);
                present = (res == VK_SUCCESS) && (supported == VK_TRUE);
            }

            if (graphics && queues.GraphicsFamily == QueueFamilySelection::InvalidFamily)
                queues.GraphicsFamily = i;
            if (compute && queues.ComputeFamily == QueueFamilySelection::InvalidFamily)
                queues.ComputeFamily = i;
            if (present && queues.PresentFamily == QueueFamilySelection::InvalidFamily)
                queues.PresentFamily = i;

            // A universal family must satisfy everything the frame needs. When
            // no surface is available yet the present requirement is dropped
            // and re-checked later, which is why PresentUnknown is tracked
            // instead of being folded into the decision.
            const bool universal = graphics && compute && (!checkPresent || present);
            if (universal && queues.UniversalFamily == QueueFamilySelection::InvalidFamily)
                queues.UniversalFamily = i;
        }

        if (queues.HasUniversalFamily())
        {
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer),
                "family %u supports graphics+compute%s",
                queues.UniversalFamily, checkPresent ? "+present" : " (present not checked)");
            result.Report.Pass("Queue families", buffer);
        }
        else if (queues.GraphicsFamily == QueueFamilySelection::InvalidFamily
                 || queues.ComputeFamily == QueueFamilySelection::InvalidFamily)
        {
            result.Report.Fail("Queue families",
                "no queue family exposes both graphics and compute operations");
        }
        else if (checkPresent && queues.PresentFamily == QueueFamilySelection::InvalidFamily)
        {
            result.Report.Fail("Queue families",
                "no queue family can present to the window surface");
        }
        else
        {
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer),
                "split families graphics=%u compute=%u present=%u; "
                "resources will need queue-family ownership transfers",
                queues.GraphicsFamily, queues.ComputeFamily, queues.PresentFamily);
            result.Report.Pass("Queue families", buffer);
        }
    }

    // --- device extensions --------------------------------------------------
    {
        std::vector<VkExtensionProperties> available;
        if (!EnumerateDeviceExtensions(fns, physicalDevice, available))
        {
            result.Report.Fail("Device extension enumeration",
                "vkEnumerateDeviceExtensionProperties failed");
        }
        else
        {
            const bool needPresent = (surface != VK_NULL_HANDLE);
            for (const char* name : GetRequiredDeviceExtensions(needPresent))
            {
                if (HasExtension(available, name))
                    result.Report.Pass(name, "present");
                else
                    result.Report.Fail(name, "not exposed by this device");
            }

            // Mandatory to enable when present, per the portability subset
            // specification: an implementation that exposes it must have it
            // enabled at vkCreateDevice or device creation is invalid.
            result.RequiresPortabilitySubset =
                HasExtension(available, "VK_KHR_portability_subset");

            result.HasDebugMarkerSupport = HasExtension(available, VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
            result.HasNvLowLatency2 = HasExtension(available, "VK_NV_low_latency2");
            result.HasAmdAntiLag = HasExtension(available, "VK_AMD_anti_lag");
        }
    }

    // --- surface capabilities ----------------------------------------------
    if (surface != VK_NULL_HANDLE)
    {
        if (!fns.GetPhysicalDeviceSurfaceFormatsKHR || !fns.GetPhysicalDeviceSurfacePresentModesKHR)
        {
            result.Report.Fail("Surface support",
                "VK_KHR_surface entry points are not loaded");
        }
        else
        {
            u32 formatCount = 0;
            VkResult res = fns.GetPhysicalDeviceSurfaceFormatsKHR(
                physicalDevice, surface, &formatCount, nullptr);
            u32 presentModeCount = 0;
            VkResult res2 = fns.GetPhysicalDeviceSurfacePresentModesKHR(
                physicalDevice, surface, &presentModeCount, nullptr);

            if (res != VK_SUCCESS || res2 != VK_SUCCESS)
            {
                result.Report.Fail("Surface support",
                    "format/present-mode query failed: " + FormatResult(res != VK_SUCCESS ? res : res2));
            }
            else if (formatCount == 0 || presentModeCount == 0)
            {
                result.Report.Fail("Surface support",
                    "the surface exposes no usable format/present mode pair");
            }
            else
            {
                char buffer[96];
                std::snprintf(buffer, sizeof(buffer),
                    "%u surface formats, %u present modes", formatCount, presentModeCount);
                result.Report.Pass("Surface support", buffer);
            }
        }
    }
    else
    {
        // Explicitly recorded so a headless probe never reads as "present works".
        result.Report.Pass("Surface support", "not checked (probe ran without a surface)");
    }

    // --- formats ------------------------------------------------------------
    // FinalFB is written with imageStore by the final pass and read with
    // imageLoad by both presentation stages, so STORAGE_IMAGE is the load-
    // bearing capability. SAMPLED and the two transfer bits are still required
    // because the presenter (phase 10-12) needs a path from this image to the
    // swapchain and refusing a device here is cheaper than failing at present
    // time.
    RequireFormat(fns, physicalDevice, result.Report,
        "R8G8B8A8_UNORM storage image", FinalFramebufferFormat, FormatDomain::OptimalTiling,
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
        | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
        | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT
        | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);

    RequireFormat(fns, physicalDevice, result.Report,
        "R16G16B16A16_UINT uniform texel buffer", SetupIndicesFormat, FormatDomain::Buffer,
        VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT);

    RequireFormat(fns, physicalDevice, result.Report,
        "R8G8B8A8_UINT texture array", TexcacheFormat, FormatDomain::OptimalTiling,
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);

    RequireFormat(fns, physicalDevice, result.Report,
        "R32_UINT clear bitmap", ClearBitmapFormat, FormatDomain::OptimalTiling,
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);

    // The capture textures are sampled with linear filtering when the capture
    // source resolution differs from the render resolution.
    RequireFormat(fns, physicalDevice, result.Report,
        "R8G8B8A8_UNORM capture texture", CaptureFormat, FormatDomain::OptimalTiling,
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
        | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
        | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);

    // --- descriptor limits --------------------------------------------------
    // Every required value comes from the binding tables in VulkanDescriptors.h,
    // so adding a binding there automatically tightens the check here.
    RequireLimit(result.Report, "maxBoundDescriptorSets",
        limits.maxBoundDescriptorSets, DescriptorSetCount);
    RequireLimit(result.Report, "maxPushConstantsSize",
        limits.maxPushConstantsSize, PushConstantSize);
    RequireLimit(result.Report, "maxPerStageDescriptorUniformBuffers",
        limits.maxPerStageDescriptorUniformBuffers, DescriptorDemand::UniformBuffers);
    RequireLimit(result.Report, "maxPerStageDescriptorStorageBuffers",
        limits.maxPerStageDescriptorStorageBuffers, DescriptorDemand::StorageBuffers);
    RequireLimit(result.Report, "maxDescriptorSetStorageBuffers",
        limits.maxDescriptorSetStorageBuffers, DescriptorDemand::StorageBuffers);
    RequireLimit(result.Report, "maxPerStageDescriptorStorageImages",
        limits.maxPerStageDescriptorStorageImages, DescriptorDemand::StorageImages);
    RequireLimit(result.Report, "maxPerStageDescriptorSampledImages",
        limits.maxPerStageDescriptorSampledImages, DescriptorDemand::SampledImages);
    RequireLimit(result.Report, "maxPerStageDescriptorSamplers",
        limits.maxPerStageDescriptorSamplers, DescriptorDemand::Samplers);
    RequireLimit(result.Report, "maxPerStageResources",
        limits.maxPerStageResources, RasterizerBindingCount + TextureBindingCount);
    RequireLimit(result.Report, "maxUniformBufferRange",
        limits.maxUniformBufferRange, MetaUniformBytes);

    // --- image and compute limits ------------------------------------------
    RequireLimit(result.Report, "maxImageDimension2D",
        limits.maxImageDimension2D, MinImageDimension2D);
    RequireLimit(result.Report, "maxImageArrayLayers",
        limits.maxImageArrayLayers, TexcacheMaxArrayLayers);
    RequireLimit(result.Report, "maxComputeWorkGroupInvocations",
        limits.maxComputeWorkGroupInvocations, MinComputeWorkGroupInvocations);

    // ClearCoarseBinMask dispatches a 1D workgroup of 64 invocations and
    // BinCombined one of CoarseTileCountX * CoarseTileCountY (up to 48), so the
    // X dimension has to reach 64 independently of the tile size.
    RequireLimit(result.Report, "maxComputeWorkGroupSize[0]",
        limits.maxComputeWorkGroupSize[0], 64);
    RequireLimit(result.Report, "maxComputeWorkGroupSize[1]",
        limits.maxComputeWorkGroupSize[1], 16);

    // Sanity floor: the texcache builds 1024x1024 array textures.
    RequireLimit(result.Report, "maxImageDimension2D (texcache)",
        limits.maxImageDimension2D, TexcacheMaxDimension);

    // --- internal resolution ------------------------------------------------
    // Walk the scale factors upward and stop at the first one the device
    // cannot satisfy. Reporting the actual ceiling is the whole point: the
    // frontend can then refuse an unreachable setting with a real number
    // instead of allocating and crashing.
    const VkDeviceSize memoryBudget =
        result.DeviceLocalMemory / DeviceMemoryBudgetDenominator * DeviceMemoryBudgetNumerator;

    std::string scaleLimitReason;
    for (int scale = 1; scale <= MaxSupportedScaleFactor; scale++)
    {
        const ResolutionBudget budget = ResolutionBudget::ForScaleFactor(scale);

        if (budget.ScreenWidth > limits.maxImageDimension2D
            || budget.ScreenHeight > limits.maxImageDimension2D)
        {
            scaleLimitReason = "maxImageDimension2D = " + FormatU64(limits.maxImageDimension2D);
            break;
        }
        if (budget.LargestStorageBuffer > limits.maxStorageBufferRange)
        {
            scaleLimitReason = "maxStorageBufferRange = " + FormatU64(limits.maxStorageBufferRange)
                + ", largest buffer needs " + FormatU64(budget.LargestStorageBuffer);
            break;
        }
        if (budget.TexelBufferElements > limits.maxTexelBufferElements)
        {
            scaleLimitReason = "maxTexelBufferElements = " + FormatU64(limits.maxTexelBufferElements)
                + ", SetupIndices needs " + FormatU64(budget.TexelBufferElements);
            break;
        }
        if (budget.ComputeInvocationsPerGroup > limits.maxComputeWorkGroupInvocations
            || budget.TileSize > limits.maxComputeWorkGroupSize[0]
            || budget.TileSize > limits.maxComputeWorkGroupSize[1])
        {
            scaleLimitReason = "compute workgroup limits cannot host a "
                + FormatU64(budget.TileSize) + "x" + FormatU64(budget.TileSize) + " tile";
            break;
        }
        if (budget.TilesPerLine > limits.maxComputeWorkGroupCount[0]
            || budget.TileLines > limits.maxComputeWorkGroupCount[1])
        {
            scaleLimitReason = "maxComputeWorkGroupCount is too small for the tile grid";
            break;
        }
        if (budget.TotalDeviceBytes > memoryBudget)
        {
            scaleLimitReason = "device-local memory budget " + FormatMiB(memoryBudget)
                + " below the " + FormatMiB(budget.TotalDeviceBytes) + " the rasterizer needs";
            break;
        }

        result.MaxScaleFactor = scale;
    }

    if (result.MaxScaleFactor == 0)
    {
        result.Report.Fail("Internal resolution",
            "the device cannot run even 1x: " + scaleLimitReason);
    }
    else
    {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer),
            "up to %dx supported%s%s",
            result.MaxScaleFactor,
            result.MaxScaleFactor < MaxSupportedScaleFactor ? "; limited by " : "",
            result.MaxScaleFactor < MaxSupportedScaleFactor ? scaleLimitReason.c_str() : "");
        result.Report.Pass("Internal resolution", buffer);
    }

    // --- score --------------------------------------------------------------
    // Only computed for eligible devices; an ineligible one is never chosen, so
    // its score is meaningless and stays at 0.
    if (result.IsEligible())
    {
        switch (result.Properties.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   result.Score = 1000; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: result.Score = 500;  break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    result.Score = 250;  break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            result.Score = 25;   break;
        default:                                     result.Score = 100;  break;
        }

        // Reachable internal resolution breaks ties between two GPUs of the
        // same class, which is the difference the user actually sees.
        result.Score += result.MaxScaleFactor * 10;

        // A little weight for VRAM, capped so a 48 GB workstation card cannot
        // outrank a faster device on memory size alone.
        const int gigabytes = static_cast<int>(
            std::min<VkDeviceSize>(result.DeviceLocalMemory >> 30, 32));
        result.Score += gigabytes;
    }

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] probed %s (%s, %s) API %s driver %s: %s\n",
        result.DeviceName.c_str(),
        result.VendorName.c_str(),
        DeviceTypeName(result.Properties.deviceType),
        result.ApiVersionText.c_str(),
        result.DriverVersionText.c_str(),
        result.IsEligible() ? "eligible" : "REJECTED");

    return result;
}

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
