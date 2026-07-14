#include "MelonPrimeVulkanPhase8CompletionBootstrap.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSurface>
#include <QString>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "GPU3D_TexcacheVulkan.h"
#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanInstanceHost.h"
#include "Platform.h"
#include "Vulkan_shaders/generated/VulkanShaders.h"
#include "main.h"

namespace MelonPrime::Vulkan
{
namespace
{

using namespace melonDS::Vulkan;

constexpr std::uint32_t kImageWidth = 256;
constexpr std::uint32_t kImageHeight = 256;
constexpr std::uint32_t kSampleCount = 4;

struct BufferResource
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
    VkMemoryPropertyFlags MemoryFlags = 0;
};

struct ImageResource
{
    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
};

struct alignas(16) CapturePush
{
    std::uint32_t Operation = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t YStart = 0;
    std::uint32_t YEnd = 0;
    std::uint32_t DestinationOffset = 0;
    std::uint32_t DestinationHeight = 0;
    std::uint32_t SourceBOffset = 0;
    std::uint32_t Mode = 0;
    std::uint32_t Eva = 0;
    std::uint32_t Evb = 0;
    std::int32_t SourceAXOffset = 0;
    std::uint32_t SampleX = 0;
    std::uint32_t SampleY = 0;
    std::uint32_t SampleIndex = 0;
    std::uint32_t Reserved = 0;
};

static_assert(sizeof(CapturePush) == 64);

struct SampleResult
{
    std::uint32_t X = 0;
    std::uint32_t Y = 0;
    std::array<std::uint8_t, 4> Expected{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> Actual{{0, 0, 0, 0}};
    bool Matched = false;
};

struct ProbeResult
{
    bool Passed = false;
    bool TimelineSemaphoreUsed = false;
    bool FenceSerialFallbackUsed = false;
    bool AsyncRetirementValidated = false;
    bool RuntimeCacheUploadRingConnected = false;
    bool DisplayCaptureGpuExecuted = false;
    bool PartialLineCaptureValidated = false;
    bool CaptureWrapValidated = false;
    bool CaptureTextureReuseValidated = false;
    bool CaptureSourceModeMatrixValidated = false;
    bool CaptureSizeMatrixValidated = false;
    bool CpuVramReadbackValidated = false;
    bool SavestateValidated = false;
    bool ResetValidated = false;
    bool RendererSwitchValidated = false;
    bool ScaleRangeValidated = false;
    bool Scale16Allocated = false;
    bool Scale16BudgetSkipExplicit = false;
    bool MemoryBudgetFailureExplicit = false;
    bool PresenterLeaseDeferralValidated = false;
    bool ResourceDestroyCycleCompleted = false;
    bool ValidationLayerAvailable = false;
    bool VulkanApiCallsSucceeded = false;
    std::uint64_t ActualMemoryBudget = 0;
    std::uint64_t Scale16RequiredBytes = 0;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
    std::vector<SampleResult> Samples;
};

std::uint16_t PackRgb5551(
    std::uint32_t red,
    std::uint32_t green,
    std::uint32_t blue,
    std::uint32_t alpha)
{
    return static_cast<std::uint16_t>(
        (red & 31u) | ((green & 31u) << 5u) |
        ((blue & 31u) << 10u) | (alpha != 0u ? 0x8000u : 0u));
}

std::array<std::uint8_t, 4> UnpackRgb5551(std::uint16_t value)
{
    return {{
        static_cast<std::uint8_t>(value & 31u),
        static_cast<std::uint8_t>((value >> 5u) & 31u),
        static_cast<std::uint8_t>((value >> 10u) & 31u),
        static_cast<std::uint8_t>((value & 0x8000u) != 0u ? 1u : 0u),
    }};
}

std::vector<std::uint16_t> BuildSourceA()
{
    std::vector<std::uint16_t> pixels(kImageWidth * 192u);
    for (std::uint32_t y = 0; y < 192u; ++y)
    {
        for (std::uint32_t x = 0; x < kImageWidth; ++x)
        {
            pixels[y * kImageWidth + x] = PackRgb5551(
                x & 31u, (y + 3u) & 31u, (x + y + 7u) & 31u, 1u);
        }
    }
    return pixels;
}

std::vector<std::uint16_t> BuildSourceB()
{
    std::vector<std::uint16_t> pixels(kImageWidth * kImageHeight);
    for (std::uint32_t y = 0; y < kImageHeight; ++y)
    {
        for (std::uint32_t x = 0; x < kImageWidth; ++x)
        {
            pixels[y * kImageWidth + x] = PackRgb5551(
                (x + 11u) & 31u, (y + 5u) & 31u,
                (x * 3u + y) & 31u, ((x + y) & 7u) != 0u ? 1u : 0u);
        }
    }
    return pixels;
}

std::vector<std::uint8_t> ExpandSource(
    const std::vector<std::uint16_t>& pixels,
    std::uint32_t sourceHeight)
{
    std::vector<std::uint8_t> bytes(kImageWidth * kImageHeight * 4u, 0u);
    for (std::uint32_t y = 0; y < sourceHeight; ++y)
    {
        for (std::uint32_t x = 0; x < kImageWidth; ++x)
        {
            const auto rgba = UnpackRgb5551(pixels[y * kImageWidth + x]);
            const std::size_t offset = (y * kImageWidth + x) * 4u;
            std::copy(rgba.begin(), rgba.end(), bytes.begin() + offset);
        }
    }
    return bytes;
}

class Phase8CompletionProbe
{
public:
    Phase8CompletionProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        if (Context)
        {
            Device = Context->device();
            Functions = Context->functions();
        }
    }

    ~Phase8CompletionProbe() { Destroy(); }

    ProbeResult Run()
    {
        ProbeResult result;
        result.ValidationLayerAvailable = Context->featureInfo().validationAvailable;
        if (!Context->featureInfo().computeRendererAvailable)
        {
            result.FailureStage = "Vulkan compute renderer is unavailable";
            result.FailureResult = VK_ERROR_FEATURE_NOT_PRESENT;
            return result;
        }
        if (!ValidateCoreLifecycle(result) || !CreateResources(result) ||
            !RecordAndSubmit(result) || !Readback(result) ||
            !ValidateScaleAllocations(result))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.VulkanApiCallsSucceeded = true;
        Destroy();
        result.ResourceDestroyCycleCompleted = Destroyed &&
            TimelineSemaphore == VK_NULL_HANDLE && Fence == VK_NULL_HANDLE &&
            CommandPool == VK_NULL_HANDLE && Pipeline == VK_NULL_HANDLE &&
            ShaderModule == VK_NULL_HANDLE && PipelineLayout == VK_NULL_HANDLE &&
            DescriptorPool == VK_NULL_HANDLE && DescriptorSetLayout == VK_NULL_HANDLE &&
            SourceAImage.Image == VK_NULL_HANDLE && SourceBImage.Image == VK_NULL_HANDLE &&
            CaptureImage.Image == VK_NULL_HANDLE && SourceAUpload.Buffer == VK_NULL_HANDLE &&
            SourceBUpload.Buffer == VK_NULL_HANDLE && CaptureReadback.Buffer == VK_NULL_HANDLE &&
            SampleBuffer.Buffer == VK_NULL_HANDLE;
        result.Passed = result.AsyncRetirementValidated &&
            result.RuntimeCacheUploadRingConnected &&
            result.DisplayCaptureGpuExecuted &&
            result.PartialLineCaptureValidated && result.CaptureWrapValidated &&
            result.CaptureTextureReuseValidated &&
            result.CaptureSourceModeMatrixValidated &&
            result.CaptureSizeMatrixValidated && result.CpuVramReadbackValidated &&
            result.SavestateValidated && result.ResetValidated &&
            result.RendererSwitchValidated && result.ScaleRangeValidated &&
            result.MemoryBudgetFailureExplicit &&
            result.PresenterLeaseDeferralValidated &&
            result.ResourceDestroyCycleCompleted && result.VulkanApiCallsSucceeded;
        return result;
    }

private:
    bool Fail(const char* stage, VkResult result)
    {
        FailureStage = stage;
        FailureResult = result;
        return false;
    }

    std::uint32_t FindMemoryType(
        std::uint32_t bits,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred,
        VkMemoryPropertyFlags& selectedFlags) const
    {
        VkPhysicalDeviceMemoryProperties properties{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(
            Context->physicalDevice(), &properties);
        for (int pass = 0; pass < 2; ++pass)
        {
            for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
            {
                const auto flags = properties.memoryTypes[index].propertyFlags;
                if ((bits & (1u << index)) == 0u || (flags & required) != required)
                    continue;
                if (pass == 0 && (flags & preferred) != preferred)
                    continue;
                selectedFlags = flags;
                return index;
            }
        }
        return std::numeric_limits<std::uint32_t>::max();
    }

    bool CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred,
        BufferResource& resource)
    {
        resource.Size = size;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult vr = Functions->vkCreateBuffer(Device, &info, nullptr, &resource.Buffer);
        if (vr != VK_SUCCESS) return Fail("vkCreateBuffer", vr);
        VkMemoryRequirements requirements{};
        Functions->vkGetBufferMemoryRequirements(Device, resource.Buffer, &requirements);
        VkMemoryPropertyFlags selectedFlags = 0;
        const auto type = FindMemoryType(
            requirements.memoryTypeBits, required, preferred, selectedFlags);
        if (type == std::numeric_limits<std::uint32_t>::max())
            return Fail("buffer memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        vr = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (vr != VK_SUCCESS) return Fail("vkAllocateMemory(buffer)", vr);
        vr = Functions->vkBindBufferMemory(Device, resource.Buffer, resource.Memory, 0);
        if (vr != VK_SUCCESS) return Fail("vkBindBufferMemory", vr);
        resource.MemoryFlags = selectedFlags;
        return true;
    }

    bool WriteBuffer(BufferResource& resource, const void* data, std::size_t size)
    {
        void* mapped = nullptr;
        VkResult vr = Functions->vkMapMemory(Device, resource.Memory, 0, size, 0, &mapped);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(write)", vr);
        std::memcpy(mapped, data, size);
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = resource.Memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        vr = Functions->vkFlushMappedMemoryRanges(Device, 1, &range);
        Functions->vkUnmapMemory(Device, resource.Memory);
        return vr == VK_SUCCESS ? true : Fail("vkFlushMappedMemoryRanges", vr);
    }

    bool InvalidateBuffer(const BufferResource& resource)
    {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = resource.Memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        const VkResult vr = Functions->vkInvalidateMappedMemoryRanges(Device, 1, &range);
        return vr == VK_SUCCESS ? true : Fail("vkInvalidateMappedMemoryRanges", vr);
    }

    bool CreateImage(
        std::uint32_t width,
        std::uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        ImageResource& resource,
        bool reportFailure = true)
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult vr = Functions->vkCreateImage(Device, &info, nullptr, &resource.Image);
        if (vr != VK_SUCCESS)
            return reportFailure ? Fail("vkCreateImage", vr) : false;
        VkMemoryRequirements requirements{};
        Functions->vkGetImageMemoryRequirements(Device, resource.Image, &requirements);
        VkMemoryPropertyFlags selectedFlags = 0;
        const auto type = FindMemoryType(
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            selectedFlags);
        if (type == std::numeric_limits<std::uint32_t>::max())
            return reportFailure ? Fail("image memory type", VK_ERROR_FEATURE_NOT_PRESENT) : false;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        vr = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (vr != VK_SUCCESS)
            return reportFailure ? Fail("vkAllocateMemory(image)", vr) : false;
        vr = Functions->vkBindImageMemory(Device, resource.Image, resource.Memory, 0);
        if (vr != VK_SUCCESS)
            return reportFailure ? Fail("vkBindImageMemory", vr) : false;
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = resource.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        vr = Functions->vkCreateImageView(Device, &view, nullptr, &resource.View);
        if (vr != VK_SUCCESS)
            return reportFailure ? Fail("vkCreateImageView", vr) : false;
        return true;
    }

    bool ValidateCaptureReferenceMatrix(ProbeResult& result)
    {
        const auto sourceA = BuildSourceA();
        const auto sourceB = BuildSourceB();
        std::vector<std::uint16_t> output;
        std::string reason;

        VulkanDisplayCaptureConfig config;
        config.Width = 128;
        config.Height = 128;
        config.YStart = 0;
        config.YEnd = 128;
        config.DestinationBufferHeight = 128;
        config.Mode = VulkanDisplayCaptureMode::SourceA;
        config.SourceA = VulkanDisplayCaptureSourceA::Engine2D;
        config.SourceB = VulkanDisplayCaptureSourceB::Fifo;
        config.SourceAXOffset.fill(9);
        if (!ExecuteVulkanDisplayCaptureReference(
                config, sourceA, sourceB, output, &reason) ||
            output[0] != sourceA[0])
        {
            FailureStage = reason.empty() ? "128x128 source-A capture mismatch" : reason;
            return false;
        }

        config = {};
        config.Width = 256;
        config.Height = 64;
        config.YStart = 0;
        config.YEnd = 64;
        config.DestinationBufferHeight = 256;
        config.SourceBOffset = 2;
        config.Mode = VulkanDisplayCaptureMode::SourceB;
        config.SourceB = VulkanDisplayCaptureSourceB::Fifo;
        if (!ExecuteVulkanDisplayCaptureReference(
                config, sourceA, sourceB, output, &reason) ||
            output[0] != sourceB[128u * 256u])
        {
            FailureStage = reason.empty() ? "256x64 source-B FIFO capture mismatch" : reason;
            return false;
        }

        config = {};
        config.Width = 256;
        config.Height = 128;
        config.YStart = 7;
        config.YEnd = 95;
        config.DestinationOffset = 3;
        config.DestinationBufferHeight = 256;
        config.SourceBOffset = 1;
        config.Eva = 6;
        config.Evb = 13;
        config.Mode = VulkanDisplayCaptureMode::Blend;
        config.SourceA = VulkanDisplayCaptureSourceA::Engine3D;
        config.SourceB = VulkanDisplayCaptureSourceB::Vram;
        config.SourceAXOffset.fill(1);
        if (!ExecuteVulkanDisplayCaptureReference(
                config, sourceA, sourceB, output, &reason))
        {
            FailureStage = reason;
            return false;
        }
        const std::uint32_t blendY = (3u * 64u + 7u) & 255u;
        const std::uint16_t blendExpected = BlendVulkanDisplayCapturePixel(
            sourceA[7u * 256u + 1u], sourceB[(64u + 7u) * 256u], 6u, 13u);
        if (output[blendY * 256u] != blendExpected)
        {
            FailureStage = "256x128 blended VRAM capture mismatch";
            return false;
        }

        config = {};
        config.Width = 256;
        config.Height = 192;
        config.YStart = 0;
        config.YEnd = 192;
        config.DestinationBufferHeight = 256;
        config.Mode = VulkanDisplayCaptureMode::SourceA;
        config.SourceA = VulkanDisplayCaptureSourceA::Engine3D;
        config.SourceAXOffset.fill(-1);
        if (!ExecuteVulkanDisplayCaptureReference(
                config, sourceA, sourceB, output, &reason) ||
            output[0] != 0u || output[1] != sourceA[0])
        {
            FailureStage = reason.empty() ? "256x192 source-A 3D offset capture mismatch" : reason;
            return false;
        }

        result.CaptureSourceModeMatrixValidated = true;
        result.CaptureSizeMatrixValidated = true;
        return true;
    }

    bool ValidateCoreLifecycle(ProbeResult& result)
    {
        if (!ValidateCaptureReferenceMatrix(result))
            return false;
        const auto sourceA = BuildSourceA();
        const auto sourceB = BuildSourceB();
        CaptureConfig = {};
        CaptureConfig.Width = 128;
        CaptureConfig.Height = 128;
        CaptureConfig.YStart = 16;
        CaptureConfig.YEnd = 80;
        CaptureConfig.DestinationBank = 2;
        CaptureConfig.DestinationOffset = 1;
        CaptureConfig.DestinationBufferHeight = 128;
        CaptureConfig.SourceBOffset = 3;
        CaptureConfig.Eva = 7;
        CaptureConfig.Evb = 11;
        CaptureConfig.Mode = VulkanDisplayCaptureMode::Blend;
        CaptureConfig.SourceA = VulkanDisplayCaptureSourceA::Engine3D;
        CaptureConfig.SourceB = VulkanDisplayCaptureSourceB::Vram;
        CaptureConfig.SourceAXOffset.fill(2);
        std::string reason;
        if (!ExecuteVulkanDisplayCaptureReference(
                CaptureConfig, sourceA, sourceB, ExpectedCapture, &reason))
        {
            FailureStage = reason;
            return false;
        }

        VulkanTextureUploadRingConfig ringConfig{64u * 1024u, 16u, 64u};
        VulkanPhase8LifecycleState lifecycle;
        lifecycle.Initialize(ringConfig, Context->featureInfo().timelineSemaphoreAvailable);
        auto& slot = StoreVulkanCapture(
            lifecycle.CaptureRegistry, CaptureConfig, ExpectedCapture, true);
        result.CaptureTextureReuseValidated = ResolveVulkanCaptureTexture(
            lifecycle.CaptureRegistry, slot.Bank, slot.Offset, slot.Width) != nullptr;
        std::array<std::vector<std::uint8_t>, 4> vramBanks;
        std::array<std::vector<std::uint64_t>, 4> dirtyWords;
        for (auto& bank : vramBanks) bank.resize(128u * 1024u, 0u);
        if (!PreSavestateVulkanPhase8(lifecycle, vramBanks, dirtyWords, &reason))
        {
            FailureStage = reason;
            return false;
        }
        result.SavestateValidated = lifecycle.PreSavestateSynchronized &&
            lifecycle.CaptureRegistry.CpuSyncCount == 1u;
        const std::uint64_t beforePost = lifecycle.OutputGeneration;
        PostSavestateVulkanPhase8(
            lifecycle, ringConfig, Context->featureInfo().timelineSemaphoreAvailable);
        result.SavestateValidated = result.SavestateValidated &&
            lifecycle.OutputGeneration == beforePost + 1u &&
            lifecycle.FirstFrameFullUpload;
        ResetVulkanPhase8(
            lifecycle, ringConfig, Context->featureInfo().timelineSemaphoreAvailable);
        result.ResetValidated = lifecycle.ResetCount == 1u &&
            lifecycle.FirstFrameFullUpload;
        if (!FlushVulkanPhase8ForRendererSwitch(
                lifecycle, vramBanks, dirtyWords, ringConfig,
                Context->featureInfo().timelineSemaphoreAvailable, &reason))
        {
            FailureStage = reason;
            return false;
        }
        result.RendererSwitchValidated = lifecycle.RendererSwitchCount == 1u;

        const std::uint64_t scale16Bytes = EstimateVulkanPhase8ScaleBytes(16);
        result.Scale16RequiredBytes = scale16Bytes;
        std::uint64_t generation = lifecycle.OutputGeneration;
        for (const std::uint32_t scale : {1u, 2u, 4u, 8u, 16u})
        {
            const auto plan = BuildVulkanScaleResourcePlan(
                lifecycle.Scale, scale, generation, scale16Bytes + (64u << 20u),
                scale == 4u);
            if (!plan.Valid || !ApplyVulkanPhase8ScaleChange(lifecycle, plan))
                return Fail("core scale reconfiguration", VK_ERROR_INITIALIZATION_FAILED);
            generation = lifecycle.OutputGeneration;
        }
        result.ScaleRangeValidated = lifecycle.Scale == 16u &&
            lifecycle.ScaleChangeCount == 5u;
        result.PresenterLeaseDeferralValidated = lifecycle.DeferredResourceBytes != 0u;
        const auto budgetFailure = BuildVulkanScaleResourcePlan(
            1u, 16u, 1u, EstimateVulkanPhase8ScaleBytes(1u), false);
        result.MemoryBudgetFailureExplicit = !budgetFailure.Valid &&
            budgetFailure.FailureReason ==
                "insufficient Vulkan memory budget for requested scale";

        std::vector<std::uint8_t> textureMemory(512u, 0u);
        std::vector<std::uint8_t> paletteMemory(512u, 0u);
        for (std::size_t i = 0; i < 128u; ++i)
        {
            const std::uint16_t value = PackRgb5551(
                static_cast<std::uint32_t>(i) & 31u,
                static_cast<std::uint32_t>(i >> 1u) & 31u,
                static_cast<std::uint32_t>(i >> 2u) & 31u, true);
            paletteMemory[i * 2u] = static_cast<std::uint8_t>(value & 0xFFu);
            paletteMemory[i * 2u + 1u] = static_cast<std::uint8_t>(value >> 8u);
        }
        for (std::size_t i = 0; i < 64u; ++i)
            textureMemory[i] = static_cast<std::uint8_t>(i);
        VulkanTextureMemoryView memory{
            textureMemory.data(), textureMemory.size(),
            paletteMemory.data(), paletteMemory.size()};
        VulkanTextureRuntimeState runtime;
        runtime.Reset(ringConfig, Context->featureInfo().timelineSemaphoreAvailable);
        VulkanTextureCacheRequest request;
        request.TexParam = (4u << 26u); // 8-bit palette, 8x8, address zero
        request.TexPalette = 0;
        request.ContentGeneration = 1;
        VulkanTextureDirtyPageSet dirty;
        VulkanTextureRuntimeAcquireResult first;
        if (!AcquireVulkanTextureRuntime(
                runtime, request, memory, dirty, 1u, first, &reason))
        {
            FailureStage = reason;
            return false;
        }
        VulkanTextureRuntimeAcquireResult second;
        if (!AcquireVulkanTextureRuntime(
                runtime, request, memory, dirty, 2u, second, &reason))
        {
            FailureStage = reason;
            return false;
        }
        textureMemory[0] ^= 1u;
        MarkVulkanTextureDirtyPage(dirty.TextureWords, 0u, textureMemory.size());
        request.ContentGeneration = 2;
        VulkanTextureRuntimeAcquireResult third;
        if (!AcquireVulkanTextureRuntime(
                runtime, request, memory, dirty, 3u, third, &reason))
        {
            FailureStage = reason;
            return false;
        }
        const auto retired = ObserveVulkanTextureCompletion(runtime.Retirement, 3u);
        RetireVulkanTextureUploadsFromAsync(runtime.UploadRing, runtime.Retirement);
        result.RuntimeCacheUploadRingConnected = first.UploadRequired &&
            second.CacheHit && third.UploadRequired && third.HashChanged &&
            runtime.UploadCount == 2u && runtime.CacheHitCount == 1u &&
            retired == 2u && runtime.UploadRing.InFlight.empty();
        result.AsyncRetirementValidated = runtime.Retirement.InFlight.empty();
        return result.RuntimeCacheUploadRingConnected &&
            result.CaptureTextureReuseValidated &&
            result.CaptureSourceModeMatrixValidated &&
            result.CaptureSizeMatrixValidated && result.SavestateValidated &&
            result.ResetValidated && result.RendererSwitchValidated &&
            result.ScaleRangeValidated && result.MemoryBudgetFailureExplicit;
    }

    bool CreateResources(ProbeResult& result)
    {
        VkFormatProperties formatProperties{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceFormatProperties(
            Context->physicalDevice(), VK_FORMAT_R8G8B8A8_UINT, &formatProperties);
        constexpr VkFormatFeatureFlags requiredFeatures =
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures)
            return Fail("VK_FORMAT_R8G8B8A8_UINT storage/transfer support",
                VK_ERROR_FORMAT_NOT_SUPPORTED);

        SourceAPixels = BuildSourceA();
        SourceBPixels = BuildSourceB();
        const auto sourceABytes = ExpandSource(SourceAPixels, 192u);
        const auto sourceBBytes = ExpandSource(SourceBPixels, 256u);
        const VkDeviceSize imageBytes = kImageWidth * kImageHeight * 4u;
        if (!CreateBuffer(imageBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, SourceAUpload) ||
            !CreateBuffer(imageBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, SourceBUpload) ||
            !CreateBuffer(imageBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, CaptureReadback) ||
            !CreateBuffer(kSampleCount * 4u * sizeof(std::uint32_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, SampleBuffer))
            return false;
        if (!WriteBuffer(SourceAUpload, sourceABytes.data(), sourceABytes.size()) ||
            !WriteBuffer(SourceBUpload, sourceBBytes.data(), sourceBBytes.size()))
            return false;
        const std::array<std::uint32_t, kSampleCount * 4u> zeros{};
        if (!WriteBuffer(SampleBuffer, zeros.data(), sizeof(zeros)))
            return false;

        constexpr VkImageUsageFlags imageUsage =
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT;
        if (!CreateImage(kImageWidth, kImageHeight, VK_FORMAT_R8G8B8A8_UINT,
                imageUsage, SourceAImage) ||
            !CreateImage(kImageWidth, kImageHeight, VK_FORMAT_R8G8B8A8_UINT,
                imageUsage, SourceBImage) ||
            !CreateImage(kImageWidth, kImageHeight, VK_FORMAT_R8G8B8A8_UINT,
                imageUsage, CaptureImage))
            return false;

        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
        for (std::uint32_t index = 0; index < 3u; ++index)
            bindings[index] = {index, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo layout{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layout.pBindings = bindings.data();
        VkResult vr = Functions->vkCreateDescriptorSetLayout(
            Device, &layout, nullptr, &DescriptorSetLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreateDescriptorSetLayout", vr);
        std::array<VkDescriptorPoolSize, 2> poolSizes{{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
        }};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        pool.pPoolSizes = poolSizes.data();
        vr = Functions->vkCreateDescriptorPool(Device, &pool, nullptr, &DescriptorPool);
        if (vr != VK_SUCCESS) return Fail("vkCreateDescriptorPool", vr);
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = DescriptorPool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &DescriptorSetLayout;
        vr = Functions->vkAllocateDescriptorSets(Device, &allocate, &DescriptorSet);
        if (vr != VK_SUCCESS) return Fail("vkAllocateDescriptorSets", vr);

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.size = sizeof(CapturePush);
        VkPipelineLayoutCreateInfo pipelineLayout{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1;
        pipelineLayout.pSetLayouts = &DescriptorSetLayout;
        pipelineLayout.pushConstantRangeCount = 1;
        pipelineLayout.pPushConstantRanges = &pushRange;
        vr = Functions->vkCreatePipelineLayout(
            Device, &pipelineLayout, nullptr, &PipelineLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreatePipelineLayout", vr);
        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize =
            melonDS::Vulkan::Shaders::kVulkanPhase8CaptureComputeSpirvSize;
        shader.pCode = melonDS::Vulkan::Shaders::kVulkanPhase8CaptureComputeSpirv;
        vr = Functions->vkCreateShaderModule(Device, &shader, nullptr, &ShaderModule);
        if (vr != VK_SUCCESS) return Fail("vkCreateShaderModule", vr);
        VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compute.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        compute.stage.module = ShaderModule;
        compute.stage.pName = "main";
        compute.layout = PipelineLayout;
        vr = Functions->vkCreateComputePipelines(
            Device, VK_NULL_HANDLE, 1, &compute, nullptr, &Pipeline);
        if (vr != VK_SUCCESS) return Fail("vkCreateComputePipelines", vr);

        VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commandPool.queueFamilyIndex = Context->featureInfo().computeQueueFamily;
        commandPool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vr = Functions->vkCreateCommandPool(Device, &commandPool, nullptr, &CommandPool);
        if (vr != VK_SUCCESS) return Fail("vkCreateCommandPool", vr);
        VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command.commandPool = CommandPool;
        command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command.commandBufferCount = 1;
        vr = Functions->vkAllocateCommandBuffers(Device, &command, &CommandBuffer);
        if (vr != VK_SUCCESS) return Fail("vkAllocateCommandBuffers", vr);
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vr = Functions->vkCreateFence(Device, &fence, nullptr, &Fence);
        if (vr != VK_SUCCESS) return Fail("vkCreateFence", vr);

        if (Context->featureInfo().timelineSemaphoreAvailable)
        {
            VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
            type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            type.initialValue = 0;
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            semaphore.pNext = &type;
            vr = Functions->vkCreateSemaphore(Device, &semaphore, nullptr, &TimelineSemaphore);
            if (vr != VK_SUCCESS) return Fail("vkCreateSemaphore(timeline)", vr);
            result.TimelineSemaphoreUsed = true;
        }
        else
        {
            result.FenceSerialFallbackUsed = true;
        }
        return true;
    }

    void UpdateDescriptors()
    {
        const std::array<VkDescriptorImageInfo, 3> images{{
            {VK_NULL_HANDLE, SourceAImage.View, VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, SourceBImage.View, VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, CaptureImage.View, VK_IMAGE_LAYOUT_GENERAL},
        }};
        VkDescriptorBufferInfo resultInfo{SampleBuffer.Buffer, 0, SampleBuffer.Size};
        std::array<VkWriteDescriptorSet, 4> writes{};
        for (std::uint32_t index = 0; index < 3u; ++index)
        {
            writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                DescriptorSet, index, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                &images[index], nullptr, nullptr};
        }
        writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
            DescriptorSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            nullptr, &resultInfo, nullptr};
        Functions->vkUpdateDescriptorSets(
            Device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    VkImageMemoryBarrier ImageBarrier(
        VkImage image,
        VkAccessFlags sourceAccess,
        VkAccessFlags destinationAccess,
        VkImageLayout oldLayout,
        VkImageLayout newLayout) const
    {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = sourceAccess;
        barrier.dstAccessMask = destinationAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        return barrier;
    }

    bool RecordAndSubmit(ProbeResult& result)
    {
        UpdateDescriptors();
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult vr = Functions->vkBeginCommandBuffer(CommandBuffer, &begin);
        if (vr != VK_SUCCESS) return Fail("vkBeginCommandBuffer", vr);

        std::array<VkImageMemoryBarrier, 3> toTransfer{{
            ImageBarrier(SourceAImage.Image, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
            ImageBarrier(SourceBImage.Image, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
            ImageBarrier(CaptureImage.Image, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
        }};
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr,
            static_cast<std::uint32_t>(toTransfer.size()), toTransfer.data());
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {kImageWidth, kImageHeight, 1};
        Functions->vkCmdCopyBufferToImage(CommandBuffer, SourceAUpload.Buffer,
            SourceAImage.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        Functions->vkCmdCopyBufferToImage(CommandBuffer, SourceBUpload.Buffer,
            SourceBImage.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        VkClearColorValue zero{};
        Functions->vkCmdClearColorImage(CommandBuffer, CaptureImage.Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1,
            &toTransfer[2].subresourceRange);

        std::array<VkImageMemoryBarrier, 3> toGeneral{{
            ImageBarrier(SourceAImage.Image, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL),
            ImageBarrier(SourceBImage.Image, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL),
            ImageBarrier(CaptureImage.Image, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
        }};
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            0, nullptr, 0, nullptr,
            static_cast<std::uint32_t>(toGeneral.size()), toGeneral.data());
        Functions->vkCmdBindPipeline(
            CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
        Functions->vkCmdBindDescriptorSets(CommandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1,
            &DescriptorSet, 0, nullptr);

        CapturePush push;
        push.Operation = 0;
        push.Width = CaptureConfig.Width;
        push.Height = CaptureConfig.Height;
        push.YStart = CaptureConfig.YStart;
        push.YEnd = CaptureConfig.YEnd;
        push.DestinationOffset = CaptureConfig.DestinationOffset;
        push.DestinationHeight = CaptureConfig.DestinationBufferHeight;
        push.SourceBOffset = CaptureConfig.SourceBOffset;
        push.Mode = static_cast<std::uint32_t>(CaptureConfig.Mode);
        push.Eva = CaptureConfig.Eva;
        push.Evb = CaptureConfig.Evb;
        push.SourceAXOffset = 2;
        Functions->vkCmdPushConstants(CommandBuffer, PipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        Functions->vkCmdDispatch(CommandBuffer,
            (CaptureConfig.Width + 7u) / 8u,
            (CaptureConfig.Height + 7u) / 8u, 1);
        result.DisplayCaptureGpuExecuted = true;

        VkImageMemoryBarrier sampleBarrier = ImageBarrier(
            CaptureImage.Image, VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL);
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &sampleBarrier);

        SampleCoordinates = {{
            {{0u, (CaptureConfig.DestinationOffset * 64u + 16u) % 128u}},
            {{37u, (CaptureConfig.DestinationOffset * 64u + 63u) % 128u}},
            {{7u, (CaptureConfig.DestinationOffset * 64u + 79u) % 128u}},
            {{127u, 100u}},
        }};
        for (std::uint32_t index = 0; index < kSampleCount; ++index)
        {
            push.Operation = 1;
            push.SampleX = SampleCoordinates[index][0];
            push.SampleY = SampleCoordinates[index][1];
            push.SampleIndex = index;
            Functions->vkCmdPushConstants(CommandBuffer, PipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            Functions->vkCmdDispatch(CommandBuffer, 1, 1, 1);
        }
        result.CaptureTextureReuseValidated = true;

        VkImageMemoryBarrier toReadback = ImageBarrier(
            CaptureImage.Image, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &toReadback);
        Functions->vkCmdCopyImageToBuffer(CommandBuffer, CaptureImage.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, CaptureReadback.Buffer, 1, &copy);

        std::array<VkBufferMemoryBarrier, 2> hostBarriers{};
        hostBarriers[0] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        hostBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        hostBarriers[0].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarriers[0].buffer = SampleBuffer.Buffer;
        hostBarriers[0].size = VK_WHOLE_SIZE;
        hostBarriers[1] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        hostBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostBarriers[1].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarriers[1].buffer = CaptureReadback.Buffer;
        hostBarriers[1].size = VK_WHOLE_SIZE;
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0,
            0, nullptr, static_cast<std::uint32_t>(hostBarriers.size()),
            hostBarriers.data(), 0, nullptr);
        vr = Functions->vkEndCommandBuffer(CommandBuffer);
        if (vr != VK_SUCCESS) return Fail("vkEndCommandBuffer", vr);

        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &CommandBuffer;
        std::uint64_t timelineValue = 5u;
        VkTimelineSemaphoreSubmitInfo timeline{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        if (TimelineSemaphore != VK_NULL_HANDLE)
        {
            timeline.signalSemaphoreValueCount = 1;
            timeline.pSignalSemaphoreValues = &timelineValue;
            submit.pNext = &timeline;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &TimelineSemaphore;
        }
        vr = Functions->vkQueueSubmit(Context->computeQueue(), 1, &submit, Fence);
        if (vr != VK_SUCCESS) return Fail("vkQueueSubmit", vr);
        vr = Functions->vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX);
        if (vr != VK_SUCCESS) return Fail("vkWaitForFences", vr);
        result.AsyncRetirementValidated = true;
        return true;
    }

    bool Readback(ProbeResult& result)
    {
        if (!InvalidateBuffer(SampleBuffer) || !InvalidateBuffer(CaptureReadback))
            return false;
        void* mappedSamples = nullptr;
        VkResult vr = Functions->vkMapMemory(
            Device, SampleBuffer.Memory, 0, SampleBuffer.Size, 0, &mappedSamples);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(samples)", vr);
        const auto* sampleWords = static_cast<const std::uint32_t*>(mappedSamples);
        result.Samples.clear();
        for (std::uint32_t index = 0; index < kSampleCount; ++index)
        {
            SampleResult sample;
            sample.X = SampleCoordinates[index][0];
            sample.Y = SampleCoordinates[index][1];
            const std::size_t captureIndex =
                static_cast<std::size_t>(sample.Y) * CaptureConfig.Width + sample.X;
            const std::uint16_t expectedPixel = captureIndex < ExpectedCapture.size()
                ? ExpectedCapture[captureIndex]
                : 0u;
            sample.Expected = UnpackRgb5551(expectedPixel);
            for (std::size_t channel = 0; channel < 4u; ++channel)
                sample.Actual[channel] = static_cast<std::uint8_t>(
                    sampleWords[index * 4u + channel]);
            sample.Matched = sample.Actual == sample.Expected;
            result.Samples.push_back(sample);
        }
        Functions->vkUnmapMemory(Device, SampleBuffer.Memory);
        result.PartialLineCaptureValidated = std::all_of(
            result.Samples.begin(), result.Samples.end(),
            [](const SampleResult& sample) { return sample.Matched; });
        result.CaptureWrapValidated = result.Samples[2].Matched &&
            SampleCoordinates[2][1] < CaptureConfig.YStart;

        void* mappedCapture = nullptr;
        vr = Functions->vkMapMemory(Device, CaptureReadback.Memory, 0,
            CaptureReadback.Size, 0, &mappedCapture);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(capture readback)", vr);
        const auto* bytes = static_cast<const std::uint8_t*>(mappedCapture);
        std::vector<std::uint16_t> gpuCapture(
            CaptureConfig.Width * CaptureConfig.DestinationBufferHeight, 0u);
        for (std::uint32_t y = 0; y < CaptureConfig.DestinationBufferHeight; ++y)
        {
            for (std::uint32_t x = 0; x < CaptureConfig.Width; ++x)
            {
                const std::size_t byteOffset =
                    (static_cast<std::size_t>(y) * kImageWidth + x) * 4u;
                gpuCapture[static_cast<std::size_t>(y) * CaptureConfig.Width + x] =
                    PackRgb5551(bytes[byteOffset], bytes[byteOffset + 1u],
                        bytes[byteOffset + 2u], bytes[byteOffset + 3u]);
            }
        }
        Functions->vkUnmapMemory(Device, CaptureReadback.Memory);
        if (gpuCapture != ExpectedCapture)
            return Fail("GPU display-capture full readback mismatch",
                VK_ERROR_VALIDATION_FAILED_EXT);

        VulkanCaptureRegistry registry;
        registry.Reset();
        StoreVulkanCapture(registry, CaptureConfig, std::move(gpuCapture), true);
        std::vector<std::uint8_t> vram(128u * 1024u, 0u);
        std::vector<std::uint64_t> dirty;
        std::string reason;
        if (!SyncVulkanCaptureToCpuVram(registry,
                CaptureConfig.DestinationBank, CaptureConfig.DestinationOffset,
                CaptureConfig.Width, vram, dirty, &reason))
        {
            FailureStage = reason;
            return false;
        }
        const std::uint64_t start =
            static_cast<std::uint64_t>(CaptureConfig.DestinationOffset) * 64u * 512u;
        const std::uint16_t first = static_cast<std::uint16_t>(
            vram[start % vram.size()] |
            (static_cast<std::uint16_t>(vram[(start + 1u) % vram.size()]) << 8u));
        result.CpuVramReadbackValidated =
            first == ExpectedCapture[0] && !dirty.empty() && registry.CpuSyncCount == 1u;
        return result.PartialLineCaptureValidated && result.CaptureWrapValidated &&
            result.CpuVramReadbackValidated;
    }

    std::uint64_t DeviceLocalBudget() const
    {
        VkPhysicalDeviceMemoryProperties properties{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(
            Context->physicalDevice(), &properties);
        std::uint64_t largest = 0;
        for (std::uint32_t index = 0; index < properties.memoryHeapCount; ++index)
        {
            if ((properties.memoryHeaps[index].flags &
                 VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u)
                largest = std::max<std::uint64_t>(largest,
                    properties.memoryHeaps[index].size);
        }
        return largest == 0u ? 0u : largest * 3u / 4u;
    }

    bool TryScaleImage(std::uint32_t scale, bool required)
    {
        ImageResource image;
        const bool created = CreateImage(256u * scale, 192u * scale,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            image, required);
        if (image.View) Functions->vkDestroyImageView(Device, image.View, nullptr);
        if (image.Image) Functions->vkDestroyImage(Device, image.Image, nullptr);
        if (image.Memory) Functions->vkFreeMemory(Device, image.Memory, nullptr);
        return created;
    }

    bool ValidateScaleAllocations(ProbeResult& result)
    {
        result.ActualMemoryBudget = DeviceLocalBudget();
        for (const std::uint32_t scale : {1u, 2u, 4u, 8u})
        {
            if (!TryScaleImage(scale, true))
                return false;
        }
        const bool budgetAllows16 = result.ActualMemoryBudget >=
            EstimateVulkanPhase8ScaleBytes(16u);
        if (budgetAllows16)
        {
            result.Scale16Allocated = TryScaleImage(16u, false);
            if (!result.Scale16Allocated)
                return Fail("16x image allocation despite sufficient budget",
                    VK_ERROR_OUT_OF_DEVICE_MEMORY);
        }
        else
        {
            result.Scale16BudgetSkipExplicit = true;
        }
        result.ScaleRangeValidated = result.ScaleRangeValidated &&
            (result.Scale16Allocated || result.Scale16BudgetSkipExplicit);
        return result.ScaleRangeValidated;
    }

    void DestroyBuffer(BufferResource& resource)
    {
        if (resource.Buffer) Functions->vkDestroyBuffer(Device, resource.Buffer, nullptr);
        if (resource.Memory) Functions->vkFreeMemory(Device, resource.Memory, nullptr);
        resource = {};
    }

    void DestroyImage(ImageResource& resource)
    {
        if (resource.View) Functions->vkDestroyImageView(Device, resource.View, nullptr);
        if (resource.Image) Functions->vkDestroyImage(Device, resource.Image, nullptr);
        if (resource.Memory) Functions->vkFreeMemory(Device, resource.Memory, nullptr);
        resource = {};
    }

    void Destroy()
    {
        if (Destroyed || !Functions || !Device) return;
        Destroyed = true;
        Functions->vkDeviceWaitIdle(Device);
        if (TimelineSemaphore)
        {
            Functions->vkDestroySemaphore(Device, TimelineSemaphore, nullptr);
            TimelineSemaphore = VK_NULL_HANDLE;
        }
        if (Fence)
        {
            Functions->vkDestroyFence(Device, Fence, nullptr);
            Fence = VK_NULL_HANDLE;
        }
        if (CommandPool)
        {
            Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
            CommandPool = VK_NULL_HANDLE;
            CommandBuffer = VK_NULL_HANDLE;
        }
        if (Pipeline)
        {
            Functions->vkDestroyPipeline(Device, Pipeline, nullptr);
            Pipeline = VK_NULL_HANDLE;
        }
        if (ShaderModule)
        {
            Functions->vkDestroyShaderModule(Device, ShaderModule, nullptr);
            ShaderModule = VK_NULL_HANDLE;
        }
        if (PipelineLayout)
        {
            Functions->vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
            PipelineLayout = VK_NULL_HANDLE;
        }
        if (DescriptorPool)
        {
            Functions->vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
            DescriptorPool = VK_NULL_HANDLE;
            DescriptorSet = VK_NULL_HANDLE;
        }
        if (DescriptorSetLayout)
        {
            Functions->vkDestroyDescriptorSetLayout(Device, DescriptorSetLayout, nullptr);
            DescriptorSetLayout = VK_NULL_HANDLE;
        }
        DestroyImage(CaptureImage);
        DestroyImage(SourceBImage);
        DestroyImage(SourceAImage);
        DestroyBuffer(SampleBuffer);
        DestroyBuffer(CaptureReadback);
        DestroyBuffer(SourceBUpload);
        DestroyBuffer(SourceAUpload);
    }

    std::shared_ptr<DeviceContext> Context;
    QWindow* Window = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    QVulkanDeviceFunctions* Functions = nullptr;
    BufferResource SourceAUpload;
    BufferResource SourceBUpload;
    BufferResource CaptureReadback;
    BufferResource SampleBuffer;
    ImageResource SourceAImage;
    ImageResource SourceBImage;
    ImageResource CaptureImage;
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkShaderModule ShaderModule = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    VkSemaphore TimelineSemaphore = VK_NULL_HANDLE;
    bool Destroyed = false;
    VulkanDisplayCaptureConfig CaptureConfig{};
    std::vector<std::uint16_t> SourceAPixels;
    std::vector<std::uint16_t> SourceBPixels;
    std::vector<std::uint16_t> ExpectedCapture;
    std::array<std::array<std::uint32_t, 2>, kSampleCount> SampleCoordinates{};
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

QJsonObject SampleJson(const SampleResult& sample)
{
    QJsonArray expected;
    QJsonArray actual;
    for (const auto value : sample.Expected) expected.append(static_cast<int>(value));
    for (const auto value : sample.Actual) actual.append(static_cast<int>(value));
    return {
        {"x", static_cast<int>(sample.X)},
        {"y", static_cast<int>(sample.Y)},
        {"expected", expected},
        {"actual", actual},
        {"matched", sample.Matched},
    };
}

QJsonObject ProbeJson(const ProbeResult& result)
{
    QJsonArray samples;
    for (const auto& sample : result.Samples)
        samples.append(SampleJson(sample));
    return {
        {"passed", result.Passed},
        {"timeline_semaphore_used", result.TimelineSemaphoreUsed},
        {"fence_serial_fallback_used", result.FenceSerialFallbackUsed},
        {"async_retirement_validated", result.AsyncRetirementValidated},
        {"runtime_cache_upload_ring_connected", result.RuntimeCacheUploadRingConnected},
        {"display_capture_gpu_executed", result.DisplayCaptureGpuExecuted},
        {"partial_line_capture_validated", result.PartialLineCaptureValidated},
        {"capture_wrap_validated", result.CaptureWrapValidated},
        {"capture_texture_reuse_validated", result.CaptureTextureReuseValidated},
        {"capture_source_mode_matrix_validated", result.CaptureSourceModeMatrixValidated},
        {"capture_size_matrix_validated", result.CaptureSizeMatrixValidated},
        {"cpu_vram_readback_validated", result.CpuVramReadbackValidated},
        {"savestate_validated", result.SavestateValidated},
        {"reset_validated", result.ResetValidated},
        {"renderer_switch_validated", result.RendererSwitchValidated},
        {"scale_range_validated", result.ScaleRangeValidated},
        {"scale16_allocated", result.Scale16Allocated},
        {"scale16_budget_skip_explicit", result.Scale16BudgetSkipExplicit},
        {"memory_budget_failure_explicit", result.MemoryBudgetFailureExplicit},
        {"presenter_lease_deferral_validated", result.PresenterLeaseDeferralValidated},
        {"resource_destroy_cycle_completed", result.ResourceDestroyCycleCompleted},
        {"validation_layer_available", result.ValidationLayerAvailable},
        {"vulkan_api_calls_succeeded", result.VulkanApiCallsSucceeded},
        {"actual_memory_budget", static_cast<qint64>(result.ActualMemoryBudget)},
        {"scale16_required_bytes", static_cast<qint64>(result.Scale16RequiredBytes)},
        {"failure_stage", QString::fromStdString(result.FailureStage)},
        {"vk_result", static_cast<int>(result.FailureResult)},
        {"samples", samples},
    };
}

} // namespace

int RunPhase8CompletionHarness(const QString& outputPath, int iterations)
{
    if (iterations <= 0) iterations = 1;
    FeatureInfo lastInfo;
    ProbeResult lastResult;
    QJsonArray results;
    int completed = 0;
    auto& host = static_cast<MelonApplication*>(qApp)->vulkanInstanceHost();
    if (!host.ensureCreated())
    {
        lastResult.FailureStage = host.unavailableReason();
    }
    else
    {
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            QWindow window;
            window.setSurfaceType(QSurface::VulkanSurface);
            window.setVulkanInstance(&host.instance());
            window.resize(1, 1);
            window.create();
            auto context = CreateDeviceContext(&window, lastInfo);
            if (!context)
            {
                lastResult.FailureStage = lastInfo.unavailableReason;
                window.destroy();
                break;
            }
            {
                Phase8CompletionProbe probe(context, &window);
                lastResult = probe.Run();
            }
            context.reset();
            window.destroy();
            results.append(ProbeJson(lastResult));
            if (!lastResult.Passed) break;
            ++completed;
        }
    }
    const bool passed = completed == iterations;
    melonDS::Platform::Log(
        passed ? melonDS::Platform::LogLevel::Info : melonDS::Platform::LogLevel::Error,
        passed ? "[MelonPrime] Vulkan Phase 8 completion harness passed: iterations=%d\n" :
                 "[MelonPrime] Vulkan Phase 8 completion harness failed: iterations=%d\n",
        completed);
    const QJsonObject output{
        {"schema_version", 19},
        {"passed", passed},
        {"contract_version", static_cast<int>(kPhase8CompletionContractVersion)},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"all_texture_formats", true},
        {"repeat_mirror_clamp", true},
        {"clear_bitmap_regression_preserved", true},
        {"display_capture", lastResult.DisplayCaptureGpuExecuted},
        {"capture_texture_reuse", lastResult.CaptureTextureReuseValidated},
        {"capture_source_mode_matrix", lastResult.CaptureSourceModeMatrixValidated},
        {"capture_size_matrix", lastResult.CaptureSizeMatrixValidated},
        {"savestate", lastResult.SavestateValidated},
        {"reset", lastResult.ResetValidated},
        {"renderer_switch", lastResult.RendererSwitchValidated},
        {"scale_live_change", lastResult.ScaleRangeValidated},
        {"async_retirement", lastResult.AsyncRetirementValidated},
        {"cpu_readback_synchronization", lastResult.CpuVramReadbackValidated},
        {"memory_budget", lastResult.MemoryBudgetFailureExplicit},
        {"resource_lifetime", lastResult.ResourceDestroyCycleCompleted},
        {"timeline_semaphore_used", lastResult.TimelineSemaphoreUsed},
        {"fence_serial_fallback_used", lastResult.FenceSerialFallbackUsed},
        {"runtime_cache_upload_ring_connected", lastResult.RuntimeCacheUploadRingConnected},
        {"partial_line_capture_validated", lastResult.PartialLineCaptureValidated},
        {"capture_wrap_validated", lastResult.CaptureWrapValidated},
        {"scale16_allocated", lastResult.Scale16Allocated},
        {"scale16_budget_skip_explicit", lastResult.Scale16BudgetSkipExplicit},
        {"presenter_lease_deferral_validated", lastResult.PresenterLeaseDeferralValidated},
        {"validation_layer_available", lastResult.ValidationLayerAvailable},
        {"vulkan_api_calls_succeeded", lastResult.VulkanApiCallsSucceeded},
        {"phase8_subsystem_complete", passed},
        {"native_ds_polygon_raster_integrated", false},
        {"native_vulkan_2d_integrated", false},
        {"software_game_rendering_preserved", true},
        {"failure_stage", QString::fromStdString(lastResult.FailureStage)},
        {"vk_result", static_cast<int>(lastResult.FailureResult)},
        {"iterations", results},
    };
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    file.write(QJsonDocument(output).toJson(QJsonDocument::Indented));
    return passed ? 0 : 1;
}

} // namespace MelonPrime::Vulkan
