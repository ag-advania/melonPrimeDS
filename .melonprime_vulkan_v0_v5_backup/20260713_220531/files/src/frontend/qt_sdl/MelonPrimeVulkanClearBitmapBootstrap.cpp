#include "MelonPrimeVulkanClearBitmapBootstrap.h"

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
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "GPU3D_Vulkan.h"
#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanInstanceHost.h"
#include "Platform.h"
#include "main.h"
#include "../../Vulkan_shaders/generated/VulkanShaders.h"

namespace MelonPrime::Vulkan
{
namespace
{

constexpr std::uint32_t kSourceWidth = 256;
constexpr std::uint32_t kSourceHeight = 256;
constexpr std::uint32_t kRenderDispCnt = 1u << 14;
constexpr std::uint32_t kOpaquePolyId = 37;
constexpr std::uint32_t kOffsetX = 250;
constexpr std::uint32_t kOffsetY = 220;
constexpr std::uint32_t kRawAttr1 = kOpaquePolyId << 24;
constexpr std::uint32_t kRawAttr2 = (kOffsetX << 16) | (kOffsetY << 24);
constexpr std::array<int, 3> kScaleSequence{{1, 2, 4}};

struct BufferResource
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
};

struct ImageResource
{
    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
};

struct SamplePoint
{
    std::uint32_t X = 0;
    std::uint32_t Y = 0;
};

struct SampleRecord
{
    SamplePoint Output{};
    SamplePoint Source{};
    std::array<std::uint8_t, 4> ExpectedColor{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> ActualColor{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> ExpectedAttributes{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> ActualAttributes{{0, 0, 0, 0}};
    bool Wrapped = false;
    bool Matched = false;
};

struct ProbeResult
{
    bool Passed = false;
    bool ContractPassed = false;
    bool DirtyTrackingPassed = false;
    bool ColorSourceUploaded = false;
    bool DepthSourceUploaded = false;
    bool DrawSubmitted = false;
    bool ColorReadbackCompleted = false;
    bool AttributeReadbackCompleted = false;
    bool ColorSamplesMatched = false;
    bool AttributeSamplesMatched = false;
    bool RepeatWrapValidated = false;
    bool DepthWriteSubmitted = false;
    bool StencilWriteSubmitted = false;
    int Scale = 1;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
    std::vector<SampleRecord> Samples;
};

std::uint8_t ToUnorm8(float value)
{
    const float scaled = std::clamp(value, 0.0f, 1.0f) * 255.0f;
    return static_cast<std::uint8_t>(std::lround(scaled));
}

std::array<std::uint8_t, 4> ColorStorageBytes(
    const std::array<std::uint8_t, 4>& color6a5,
    VkFormat format)
{
    std::array<std::uint8_t, 4> logical{{
        ToUnorm8(static_cast<float>(color6a5[0]) / 63.0f),
        ToUnorm8(static_cast<float>(color6a5[1]) / 63.0f),
        ToUnorm8(static_cast<float>(color6a5[2]) / 63.0f),
        ToUnorm8(static_cast<float>(color6a5[3]) / 31.0f)}};
    if (format == VK_FORMAT_B8G8R8A8_UNORM)
        std::swap(logical[0], logical[2]);
    return logical;
}

std::array<std::uint8_t, 4> AttributeStorageBytes(
    std::uint8_t opaquePolyId,
    std::uint32_t packedDepth)
{
    return {{
        ToUnorm8(static_cast<float>(opaquePolyId) / 63.0f),
        0,
        static_cast<std::uint8_t>((packedDepth >> 24) ? 255u : 0u),
        255}};
}

bool PixelMatches(
    const std::array<std::uint8_t, 4>& actual,
    const std::array<std::uint8_t, 4>& expected)
{
    for (std::size_t index = 0; index < actual.size(); ++index)
    {
        const int delta = static_cast<int>(actual[index]) -
            static_cast<int>(expected[index]);
        if (std::abs(delta) > 1)
            return false;
    }
    return true;
}

bool ContractChecks()
{
    const auto state = melonDS::Vulkan::DecodeClearBitmapState(
        kRenderDispCnt, kRawAttr1, kRawAttr2);
    if (!state.Enabled || state.OffsetX != kOffsetX || state.OffsetY != kOffsetY ||
        state.OpaquePolyId != kOpaquePolyId ||
        state.Offset[0] != static_cast<float>(kOffsetX) / 256.0f ||
        state.Offset[1] != static_cast<float>(kOffsetY) / 256.0f ||
        state.PushConstants.OpaquePolyId != kOpaquePolyId)
    {
        return false;
    }

    const auto disabled = melonDS::Vulkan::DecodeClearBitmapState(
        0, kRawAttr1, kRawAttr2);
    if (disabled.Enabled)
        return false;

    const auto zeroColor = melonDS::Vulkan::DecodeClearBitmapColorTexel(0);
    const auto maxColor = melonDS::Vulkan::DecodeClearBitmapColorTexel(0xFFFFu);
    if (zeroColor != std::array<std::uint8_t, 4>{{0, 0, 0, 0}} ||
        maxColor != std::array<std::uint8_t, 4>{{63, 63, 63, 31}})
    {
        return false;
    }

    if (melonDS::Vulkan::DecodeClearBitmapDepthTexel(0) != 0x000001FFu ||
        melonDS::Vulkan::DecodeClearBitmapDepthTexel(0xFFFFu) != 0x01FFFFFFu)
    {
        return false;
    }

    for (int scale : kScaleSequence)
    {
        const auto contract = melonDS::Vulkan::BuildRasterTargetContract(
            scale, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_D24_UNORM_S8_UINT);
        if (!contract.Valid ||
            contract.Extent.width != static_cast<std::uint32_t>(256 * scale) ||
            contract.Extent.height != static_cast<std::uint32_t>(192 * scale))
        {
            return false;
        }
    }
    return true;
}

bool DirtyTrackingChecks()
{
    melonDS::Vulkan::ClearBitmapDirtyTracker tracker;
    if (tracker.PendingMask() != melonDS::Vulkan::ClearBitmapDirty_All)
        return false;
    if (tracker.ConsumeIfEnabled(false) != 0 ||
        tracker.PendingMask() != melonDS::Vulkan::ClearBitmapDirty_All)
    {
        return false;
    }
    if (tracker.ConsumeIfEnabled(true) != melonDS::Vulkan::ClearBitmapDirty_All ||
        tracker.PendingMask() != 0)
    {
        return false;
    }
    tracker.MarkDirty(melonDS::Vulkan::ClearBitmapDirty_Color);
    if (tracker.ConsumeIfEnabled(true) != melonDS::Vulkan::ClearBitmapDirty_Color)
        return false;
    tracker.MarkDirty(melonDS::Vulkan::ClearBitmapDirty_Depth);
    if (tracker.ConsumeIfEnabled(false) != 0 ||
        tracker.PendingMask() != melonDS::Vulkan::ClearBitmapDirty_Depth)
    {
        return false;
    }
    if (tracker.ConsumeIfEnabled(true) != melonDS::Vulkan::ClearBitmapDirty_Depth)
        return false;
    tracker.Reset();
    return tracker.PendingMask() == melonDS::Vulkan::ClearBitmapDirty_All;
}

QJsonArray ByteArray(const std::array<std::uint8_t, 4>& value)
{
    return QJsonArray{
        static_cast<int>(value[0]),
        static_cast<int>(value[1]),
        static_cast<int>(value[2]),
        static_cast<int>(value[3])};
}

QJsonObject SampleJson(const SampleRecord& sample)
{
    return QJsonObject{
        {"output_x", static_cast<int>(sample.Output.X)},
        {"output_y", static_cast<int>(sample.Output.Y)},
        {"source_x", static_cast<int>(sample.Source.X)},
        {"source_y", static_cast<int>(sample.Source.Y)},
        {"wrapped", sample.Wrapped},
        {"matched", sample.Matched},
        {"expected_color", ByteArray(sample.ExpectedColor)},
        {"actual_color", ByteArray(sample.ActualColor)},
        {"expected_attributes", ByteArray(sample.ExpectedAttributes)},
        {"actual_attributes", ByteArray(sample.ActualAttributes)},
    };
}

class ClearBitmapBootstrapProbe
{
public:
    ClearBitmapBootstrapProbe(
        std::shared_ptr<DeviceContext> context,
        QWindow* window,
        int scale)
        : Context(std::move(context)), Window(window), Scale(scale)
    {
        if (Context)
        {
            Device = Context->device();
            Functions = Context->functions();
        }
    }

    ~ClearBitmapBootstrapProbe()
    {
        Destroy();
    }

    ProbeResult Run()
    {
        ProbeResult result;
        result.Scale = Scale;
        result.ContractPassed = ContractChecks();
        result.DirtyTrackingPassed = DirtyTrackingChecks();
        State = melonDS::Vulkan::DecodeClearBitmapState(
            kRenderDispCnt, kRawAttr1, kRawAttr2);
        Contract = melonDS::Vulkan::BuildRasterTargetContract(
            Scale,
            Context->featureInfo().colorFormat,
            Context->featureInfo().depthStencilFormat);

        if (!result.ContractPassed || !result.DirtyTrackingPassed || !Contract.Valid)
        {
            result.FailureStage = "clear-bitmap contract validation";
            return result;
        }
        BuildSourceData();
        if (!CreateResources())
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.ColorSourceUploaded = true;
        result.DepthSourceUploaded = true;

        if (!RecordAndSubmit())
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.DrawSubmitted = true;
        result.DepthWriteSubmitted = true;
        result.StencilWriteSubmitted = true;

        if (!CollectSamples(result))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.ColorReadbackCompleted = true;
        result.AttributeReadbackCompleted = true;

        result.ColorSamplesMatched = true;
        result.AttributeSamplesMatched = true;
        for (const auto& sample : result.Samples)
        {
            result.ColorSamplesMatched = result.ColorSamplesMatched &&
                PixelMatches(sample.ActualColor, sample.ExpectedColor);
            result.AttributeSamplesMatched = result.AttributeSamplesMatched &&
                PixelMatches(sample.ActualAttributes, sample.ExpectedAttributes);
            result.RepeatWrapValidated = result.RepeatWrapValidated ||
                (sample.Wrapped && sample.Matched);
        }

        result.Passed = result.ContractPassed && result.DirtyTrackingPassed &&
            result.ColorSourceUploaded && result.DepthSourceUploaded &&
            result.DrawSubmitted && result.ColorReadbackCompleted &&
            result.AttributeReadbackCompleted && result.ColorSamplesMatched &&
            result.AttributeSamplesMatched && result.RepeatWrapValidated &&
            result.DepthWriteSubmitted && result.StencilWriteSubmitted;
        if (!result.Passed)
            result.FailureStage = "clear-bitmap repeat/offset readback comparison";
        return result;
    }

    const melonDS::Vulkan::ClearBitmapState& state() const
    {
        return State;
    }

    const melonDS::Vulkan::RasterTargetContract& contract() const
    {
        return Contract;
    }

private:
    bool Fail(const char* stage, VkResult result)
    {
        FailureStage = stage;
        FailureResult = result;
        return false;
    }

    std::uint32_t FindMemoryType(
        std::uint32_t typeBits,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred) const
    {
        VkPhysicalDeviceMemoryProperties properties{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(
            Context->physicalDevice(), &properties);
        for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            const VkMemoryPropertyFlags flags = properties.memoryTypes[index].propertyFlags;
            if ((typeBits & (1u << index)) &&
                (flags & required) == required &&
                (flags & preferred) == preferred)
            {
                return index;
            }
        }
        for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            const VkMemoryPropertyFlags flags = properties.memoryTypes[index].propertyFlags;
            if ((typeBits & (1u << index)) && (flags & required) == required)
                return index;
        }
        return std::numeric_limits<std::uint32_t>::max();
    }

    bool FormatSupports(VkFormat format, VkFormatFeatureFlags required) const
    {
        VkFormatProperties properties{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceFormatProperties(
            Context->physicalDevice(), format, &properties);
        return (properties.optimalTilingFeatures & required) == required;
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
        VkResult result = Functions->vkCreateBuffer(Device, &info, nullptr, &resource.Buffer);
        if (result != VK_SUCCESS)
            return Fail("vkCreateBuffer", result);

        VkMemoryRequirements requirements{};
        Functions->vkGetBufferMemoryRequirements(Device, resource.Buffer, &requirements);
        const std::uint32_t memoryType = FindMemoryType(
            requirements.memoryTypeBits, required, preferred);
        if (memoryType == std::numeric_limits<std::uint32_t>::max())
            return Fail("buffer memory type", VK_ERROR_FEATURE_NOT_PRESENT);

        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        result = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (result != VK_SUCCESS)
            return Fail("vkAllocateMemory(buffer)", result);
        result = Functions->vkBindBufferMemory(Device, resource.Buffer, resource.Memory, 0);
        if (result != VK_SUCCESS)
            return Fail("vkBindBufferMemory", result);
        return true;
    }

    bool CreateImage(
        std::uint32_t width,
        std::uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspect,
        ImageResource& resource)
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
        VkResult result = Functions->vkCreateImage(Device, &info, nullptr, &resource.Image);
        if (result != VK_SUCCESS)
            return Fail("vkCreateImage", result);

        VkMemoryRequirements requirements{};
        Functions->vkGetImageMemoryRequirements(Device, resource.Image, &requirements);
        const std::uint32_t memoryType = FindMemoryType(
            requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memoryType == std::numeric_limits<std::uint32_t>::max())
            return Fail("image memory type", VK_ERROR_FEATURE_NOT_PRESENT);

        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        result = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (result != VK_SUCCESS)
            return Fail("vkAllocateMemory(image)", result);
        result = Functions->vkBindImageMemory(Device, resource.Image, resource.Memory, 0);
        if (result != VK_SUCCESS)
            return Fail("vkBindImageMemory", result);

        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = resource.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange.aspectMask = aspect;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;
        result = Functions->vkCreateImageView(Device, &view, nullptr, &resource.View);
        if (result != VK_SUCCESS)
            return Fail("vkCreateImageView", result);
        return true;
    }

    bool CreateShaderModule(
        const std::uint32_t* code,
        std::size_t size,
        VkShaderModule& module)
    {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = code;
        const VkResult result = Functions->vkCreateShaderModule(
            Device, &info, nullptr, &module);
        if (result != VK_SUCCESS)
            return Fail("vkCreateShaderModule", result);
        return true;
    }

    bool UploadBuffer(const BufferResource& resource, const void* data, std::size_t size)
    {
        if (size > resource.Size)
            return Fail("upload buffer bounds", VK_ERROR_OUT_OF_HOST_MEMORY);
        void* mapped = nullptr;
        const VkResult result = Functions->vkMapMemory(
            Device, resource.Memory, 0, size, 0, &mapped);
        if (result != VK_SUCCESS)
            return Fail("vkMapMemory(upload)", result);
        std::memcpy(mapped, data, size);
        Functions->vkUnmapMemory(Device, resource.Memory);
        return true;
    }

    void BuildSourceData()
    {
        ColorSource.resize(static_cast<std::size_t>(kSourceWidth) * kSourceHeight * 4u);
        DepthSource.resize(static_cast<std::size_t>(kSourceWidth) * kSourceHeight);
        for (std::uint32_t y = 0; y < kSourceHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kSourceWidth; ++x)
            {
                const std::uint16_t r = static_cast<std::uint16_t>((x * 3u + y) & 31u);
                const std::uint16_t g = static_cast<std::uint16_t>((x + y * 5u) & 31u);
                const std::uint16_t b = static_cast<std::uint16_t>((x * 7u + y * 2u) & 31u);
                const std::uint16_t a = ((x ^ y) & 1u) ? 0x8000u : 0u;
                const std::uint16_t rawColor = static_cast<std::uint16_t>(
                    r | (g << 5) | (b << 10) | a);
                const auto decodedColor =
                    melonDS::Vulkan::DecodeClearBitmapColorTexel(rawColor);
                const std::size_t pixel = static_cast<std::size_t>(y) * kSourceWidth + x;
                std::memcpy(ColorSource.data() + pixel * 4u, decodedColor.data(), 4u);

                const std::uint16_t depth15 = static_cast<std::uint16_t>(
                    (x * 257u + y * 131u) & 0x7FFFu);
                const std::uint16_t fog = ((x + y) & 4u) ? 0x8000u : 0u;
                DepthSource[pixel] = melonDS::Vulkan::DecodeClearBitmapDepthTexel(
                    static_cast<std::uint16_t>(depth15 | fog));
            }
        }
    }

    bool CreateResources()
    {
        constexpr VkFormatFeatureFlags integerSourceFeatures =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        constexpr VkFormatFeatureFlags targetFeatures =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        if (!FormatSupports(VK_FORMAT_R8G8B8A8_UINT, integerSourceFeatures) ||
            !FormatSupports(VK_FORMAT_R32_UINT, integerSourceFeatures) ||
            !FormatSupports(Contract.ColorFormat, targetFeatures) ||
            !FormatSupports(Contract.AttributeFormat, targetFeatures))
        {
            return Fail("clear-bitmap image format features", VK_ERROR_FORMAT_NOT_SUPPORTED);
        }

        const VkDeviceSize sourceBytes =
            static_cast<VkDeviceSize>(kSourceWidth) * kSourceHeight * 4u;
        const VkDeviceSize targetBytes =
            static_cast<VkDeviceSize>(Contract.Extent.width) * Contract.Extent.height * 4u;
        const VkMemoryPropertyFlags hostVisible =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (!CreateBuffer(
                sourceBytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                hostVisible,
                0,
                ColorUpload) ||
            !CreateBuffer(
                sourceBytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                hostVisible,
                0,
                DepthUpload) ||
            !CreateBuffer(
                targetBytes,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                hostVisible,
                0,
                ColorReadback) ||
            !CreateBuffer(
                targetBytes,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                hostVisible,
                0,
                AttributeReadback))
        {
            return false;
        }
        if (!UploadBuffer(ColorUpload, ColorSource.data(), ColorSource.size()) ||
            !UploadBuffer(
                DepthUpload,
                DepthSource.data(),
                DepthSource.size() * sizeof(std::uint32_t)))
        {
            return false;
        }

        if (!CreateImage(
                kSourceWidth,
                kSourceHeight,
                VK_FORMAT_R8G8B8A8_UINT,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                ColorSourceImage) ||
            !CreateImage(
                kSourceWidth,
                kSourceHeight,
                VK_FORMAT_R32_UINT,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                DepthSourceImage) ||
            !CreateImage(
                Contract.Extent.width,
                Contract.Extent.height,
                Contract.ColorFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                ColorTarget) ||
            !CreateImage(
                Contract.Extent.width,
                Contract.Extent.height,
                Contract.AttributeFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                AttributeTarget) ||
            !CreateImage(
                Contract.Extent.width,
                Contract.Extent.height,
                Contract.DepthStencilFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                melonDS::Vulkan::DepthStencilAspectMask(Contract.DepthStencilFormat),
                DepthTarget))
        {
            return false;
        }

        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.maxLod = 0.0f;
        VkResult result = Functions->vkCreateSampler(Device, &samplerInfo, nullptr, &Sampler);
        if (result != VK_SUCCESS)
            return Fail("vkCreateSampler", result);

        VkDescriptorSetLayoutBinding bindings[2]{};
        for (std::uint32_t index = 0; index < 2; ++index)
        {
            bindings[index].binding = index;
            bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[index].descriptorCount = 1;
            bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptorLayoutInfo.bindingCount = 2;
        descriptorLayoutInfo.pBindings = bindings;
        result = Functions->vkCreateDescriptorSetLayout(
            Device, &descriptorLayoutInfo, nullptr, &DescriptorLayout);
        if (result != VK_SUCCESS)
            return Fail("vkCreateDescriptorSetLayout", result);

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 2;
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        result = Functions->vkCreateDescriptorPool(Device, &poolInfo, nullptr, &DescriptorPool);
        if (result != VK_SUCCESS)
            return Fail("vkCreateDescriptorPool", result);

        VkDescriptorSetAllocateInfo setAllocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setAllocate.descriptorPool = DescriptorPool;
        setAllocate.descriptorSetCount = 1;
        setAllocate.pSetLayouts = &DescriptorLayout;
        result = Functions->vkAllocateDescriptorSets(Device, &setAllocate, &DescriptorSet);
        if (result != VK_SUCCESS)
            return Fail("vkAllocateDescriptorSets", result);

        VkDescriptorImageInfo imageInfos[2]{};
        imageInfos[0].sampler = Sampler;
        imageInfos[0].imageView = ColorSourceImage.View;
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].sampler = Sampler;
        imageInfos[1].imageView = DepthSourceImage.View;
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet writes[2]{};
        for (std::uint32_t index = 0; index < 2; ++index)
        {
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = DescriptorSet;
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[index].pImageInfo = &imageInfos[index];
        }
        Functions->vkUpdateDescriptorSets(Device, 2, writes, 0, nullptr);

        VkAttachmentDescription attachments[3]{};
        for (int index = 0; index < 2; ++index)
        {
            attachments[index].format = index == 0
                ? Contract.ColorFormat : Contract.AttributeFormat;
            attachments[index].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[index].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[index].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[index].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[index].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[index].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[index].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }
        attachments[2].format = Contract.DepthStencilFormat;
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorReferences[2]{};
        colorReferences[0] = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        colorReferences[1] = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthReference{
            2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 2;
        subpass.pColorAttachments = colorReferences;
        subpass.pDepthStencilAttachment = &depthReference;

        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        renderPassInfo.attachmentCount = 3;
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 2;
        renderPassInfo.pDependencies = dependencies;
        result = Functions->vkCreateRenderPass(
            Device, &renderPassInfo, nullptr, &RenderPass);
        if (result != VK_SUCCESS)
            return Fail("vkCreateRenderPass", result);

        const VkImageView framebufferAttachments[] = {
            ColorTarget.View, AttributeTarget.View, DepthTarget.View};
        VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebufferInfo.renderPass = RenderPass;
        framebufferInfo.attachmentCount = 3;
        framebufferInfo.pAttachments = framebufferAttachments;
        framebufferInfo.width = Contract.Extent.width;
        framebufferInfo.height = Contract.Extent.height;
        framebufferInfo.layers = 1;
        result = Functions->vkCreateFramebuffer(
            Device, &framebufferInfo, nullptr, &Framebuffer);
        if (result != VK_SUCCESS)
            return Fail("vkCreateFramebuffer", result);

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(melonDS::Vulkan::ClearBitmapPushConstants);
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &DescriptorLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        result = Functions->vkCreatePipelineLayout(
            Device, &pipelineLayoutInfo, nullptr, &PipelineLayout);
        if (result != VK_SUCCESS)
            return Fail("vkCreatePipelineLayout", result);

        if (!CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanClearBitmapVertexSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanClearBitmapVertexSpirv),
                VertexShader) ||
            !CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanClearBitmapFragmentSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanClearBitmapFragmentSpirv),
                FragmentShader))
        {
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = VertexShader;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = FragmentShader;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterization{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkStencilOpState stencil{};
        stencil.failOp = VK_STENCIL_OP_REPLACE;
        stencil.passOp = VK_STENCIL_OP_REPLACE;
        stencil.depthFailOp = VK_STENCIL_OP_REPLACE;
        stencil.compareOp = VK_COMPARE_OP_ALWAYS;
        stencil.compareMask = 0xFFu;
        stencil.writeMask = 0xFFu;
        stencil.reference = 0xFFu;
        VkPipelineDepthStencilStateCreateInfo depthStencil{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        depthStencil.stencilTestEnable = VK_TRUE;
        depthStencil.front = stencil;
        depthStencil.back = stencil;

        VkPipelineColorBlendAttachmentState colorBlendAttachments[2]{};
        for (auto& attachment : colorBlendAttachments)
        {
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
        }
        VkPipelineColorBlendStateCreateInfo colorBlend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlend.attachmentCount = 2;
        colorBlend.pAttachments = colorBlendAttachments;
        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo pipelineInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterization;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamic;
        pipelineInfo.layout = PipelineLayout;
        pipelineInfo.renderPass = RenderPass;
        pipelineInfo.subpass = 0;
        result = Functions->vkCreateGraphicsPipelines(
            Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &Pipeline);
        if (result != VK_SUCCESS)
            return Fail("vkCreateGraphicsPipelines", result);

        VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolInfo.queueFamilyIndex = Context->featureInfo().graphicsQueueFamily;
        result = Functions->vkCreateCommandPool(
            Device, &commandPoolInfo, nullptr, &CommandPool);
        if (result != VK_SUCCESS)
            return Fail("vkCreateCommandPool", result);
        VkCommandBufferAllocateInfo commandAllocate{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandAllocate.commandPool = CommandPool;
        commandAllocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAllocate.commandBufferCount = 1;
        result = Functions->vkAllocateCommandBuffers(
            Device, &commandAllocate, &CommandBuffer);
        if (result != VK_SUCCESS)
            return Fail("vkAllocateCommandBuffers", result);
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        result = Functions->vkCreateFence(Device, &fenceInfo, nullptr, &Fence);
        if (result != VK_SUCCESS)
            return Fail("vkCreateFence", result);
        return true;
    }

    void TransitionSourceImage(
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkPipelineStageFlags srcStage,
        VkPipelineStageFlags dstStage)
    {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        Functions->vkCmdPipelineBarrier(
            CommandBuffer,
            srcStage,
            dstStage,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);
    }

    bool RecordAndSubmit()
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult result = Functions->vkBeginCommandBuffer(CommandBuffer, &begin);
        if (result != VK_SUCCESS)
            return Fail("vkBeginCommandBuffer", result);

        TransitionSourceImage(
            ColorSourceImage.Image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
        TransitionSourceImage(
            DepthSourceImage.Image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy sourceCopy{};
        sourceCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        sourceCopy.imageSubresource.mipLevel = 0;
        sourceCopy.imageSubresource.baseArrayLayer = 0;
        sourceCopy.imageSubresource.layerCount = 1;
        sourceCopy.imageExtent = {kSourceWidth, kSourceHeight, 1};
        Functions->vkCmdCopyBufferToImage(
            CommandBuffer,
            ColorUpload.Buffer,
            ColorSourceImage.Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &sourceCopy);
        Functions->vkCmdCopyBufferToImage(
            CommandBuffer,
            DepthUpload.Buffer,
            DepthSourceImage.Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &sourceCopy);

        TransitionSourceImage(
            ColorSourceImage.Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        TransitionSourceImage(
            DepthSourceImage.Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        VkClearValue clearValues[3]{};
        clearValues[2].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo renderPassBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderPassBegin.renderPass = RenderPass;
        renderPassBegin.framebuffer = Framebuffer;
        renderPassBegin.renderArea.extent = Contract.Extent;
        renderPassBegin.clearValueCount = 3;
        renderPassBegin.pClearValues = clearValues;
        Functions->vkCmdBeginRenderPass(
            CommandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
        Functions->vkCmdBindPipeline(
            CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
        Functions->vkCmdBindDescriptorSets(
            CommandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            PipelineLayout,
            0,
            1,
            &DescriptorSet,
            0,
            nullptr);
        Functions->vkCmdPushConstants(
            CommandBuffer,
            PipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(State.PushConstants),
            &State.PushConstants);

        VkViewport viewport{};
        viewport.width = static_cast<float>(Contract.Extent.width);
        viewport.height = static_cast<float>(Contract.Extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{};
        scissor.extent = Contract.Extent;
        Functions->vkCmdSetViewport(CommandBuffer, 0, 1, &viewport);
        Functions->vkCmdSetScissor(CommandBuffer, 0, 1, &scissor);
        Functions->vkCmdDraw(CommandBuffer, 3, 1, 0, 0);
        Functions->vkCmdEndRenderPass(CommandBuffer);

        VkBufferImageCopy targetCopy{};
        targetCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        targetCopy.imageSubresource.mipLevel = 0;
        targetCopy.imageSubresource.baseArrayLayer = 0;
        targetCopy.imageSubresource.layerCount = 1;
        targetCopy.imageExtent = {
            Contract.Extent.width, Contract.Extent.height, 1};
        Functions->vkCmdCopyImageToBuffer(
            CommandBuffer,
            ColorTarget.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            ColorReadback.Buffer,
            1,
            &targetCopy);
        Functions->vkCmdCopyImageToBuffer(
            CommandBuffer,
            AttributeTarget.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            AttributeReadback.Buffer,
            1,
            &targetCopy);

        VkBufferMemoryBarrier barriers[2]{};
        for (int index = 0; index < 2; ++index)
        {
            barriers[index].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barriers[index].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barriers[index].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            barriers[index].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[index].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[index].buffer = index == 0
                ? ColorReadback.Buffer : AttributeReadback.Buffer;
            barriers[index].offset = 0;
            barriers[index].size = VK_WHOLE_SIZE;
        }
        Functions->vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0,
            0,
            nullptr,
            2,
            barriers,
            0,
            nullptr);

        result = Functions->vkEndCommandBuffer(CommandBuffer);
        if (result != VK_SUCCESS)
            return Fail("vkEndCommandBuffer", result);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &CommandBuffer;
        result = Functions->vkQueueSubmit(Context->graphicsQueue(), 1, &submit, Fence);
        if (result != VK_SUCCESS)
            return Fail("vkQueueSubmit", result);
        result = Functions->vkWaitForFences(
            Device, 1, &Fence, VK_TRUE, 5'000'000'000ull);
        if (result != VK_SUCCESS)
            return Fail("vkWaitForFences", result);
        return true;
    }

    static std::array<std::uint8_t, 4> ReadPixel(
        const std::uint8_t* data,
        std::uint32_t width,
        const SamplePoint& point)
    {
        const std::size_t pixel =
            static_cast<std::size_t>(point.Y) * width + point.X;
        const std::size_t offset = pixel * 4u;
        return {{data[offset], data[offset + 1], data[offset + 2], data[offset + 3]}};
    }

    SamplePoint SourcePointFor(const SamplePoint& output) const
    {
        const std::uint32_t sourceX = static_cast<std::uint32_t>(
            std::floor((static_cast<double>(output.X) + 0.5) / Scale));
        const std::uint32_t sourceY = static_cast<std::uint32_t>(
            std::floor((static_cast<double>(output.Y) + 0.5) / Scale));
        return {
            (sourceX + State.OffsetX) & 0xFFu,
            (sourceY + State.OffsetY) & 0xFFu};
    }

    bool CollectSamples(ProbeResult& result)
    {
        void* colorMapped = nullptr;
        void* attributeMapped = nullptr;
        VkResult vkResult = Functions->vkMapMemory(
            Device,
            ColorReadback.Memory,
            0,
            ColorReadback.Size,
            0,
            &colorMapped);
        if (vkResult != VK_SUCCESS)
            return Fail("vkMapMemory(color readback)", vkResult);
        vkResult = Functions->vkMapMemory(
            Device,
            AttributeReadback.Memory,
            0,
            AttributeReadback.Size,
            0,
            &attributeMapped);
        if (vkResult != VK_SUCCESS)
        {
            Functions->vkUnmapMemory(Device, ColorReadback.Memory);
            return Fail("vkMapMemory(attribute readback)", vkResult);
        }

        const std::array<SamplePoint, 5> points{{
            {0, 0},
            {Contract.Extent.width - 1, 0},
            {0, Contract.Extent.height - 1},
            {Contract.Extent.width / 2, Contract.Extent.height / 2},
            {static_cast<std::uint32_t>(13 * Scale + Scale / 2),
             static_cast<std::uint32_t>(17 * Scale + Scale / 2)},
        }};
        const auto* colorBytes = static_cast<const std::uint8_t*>(colorMapped);
        const auto* attributeBytes = static_cast<const std::uint8_t*>(attributeMapped);
        for (const auto& output : points)
        {
            SampleRecord sample;
            sample.Output = output;
            sample.Source = SourcePointFor(output);
            const std::size_t sourcePixel =
                static_cast<std::size_t>(sample.Source.Y) * kSourceWidth + sample.Source.X;
            std::array<std::uint8_t, 4> color6a5{};
            std::memcpy(color6a5.data(), ColorSource.data() + sourcePixel * 4u, 4u);
            sample.ExpectedColor = ColorStorageBytes(color6a5, Contract.ColorFormat);
            sample.ExpectedAttributes = AttributeStorageBytes(
                State.OpaquePolyId, DepthSource[sourcePixel]);
            sample.ActualColor = ReadPixel(
                colorBytes, Contract.Extent.width, output);
            sample.ActualAttributes = ReadPixel(
                attributeBytes, Contract.Extent.width, output);

            const std::uint32_t unwrappedX = static_cast<std::uint32_t>(
                std::floor((static_cast<double>(output.X) + 0.5) / Scale)) +
                State.OffsetX;
            const std::uint32_t unwrappedY = static_cast<std::uint32_t>(
                std::floor((static_cast<double>(output.Y) + 0.5) / Scale)) +
                State.OffsetY;
            sample.Wrapped = unwrappedX >= kSourceWidth || unwrappedY >= kSourceHeight;
            sample.Matched = PixelMatches(sample.ActualColor, sample.ExpectedColor) &&
                PixelMatches(sample.ActualAttributes, sample.ExpectedAttributes);
            result.Samples.push_back(sample);
        }

        Functions->vkUnmapMemory(Device, AttributeReadback.Memory);
        Functions->vkUnmapMemory(Device, ColorReadback.Memory);
        return true;
    }

    void Destroy()
    {
        if (!Functions || !Device)
            return;
        if (Fence)
            Functions->vkDestroyFence(Device, Fence, nullptr);
        if (CommandPool)
            Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
        if (Pipeline)
            Functions->vkDestroyPipeline(Device, Pipeline, nullptr);
        if (VertexShader)
            Functions->vkDestroyShaderModule(Device, VertexShader, nullptr);
        if (FragmentShader)
            Functions->vkDestroyShaderModule(Device, FragmentShader, nullptr);
        if (PipelineLayout)
            Functions->vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        if (Framebuffer)
            Functions->vkDestroyFramebuffer(Device, Framebuffer, nullptr);
        if (RenderPass)
            Functions->vkDestroyRenderPass(Device, RenderPass, nullptr);
        if (DescriptorPool)
            Functions->vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
        if (DescriptorLayout)
            Functions->vkDestroyDescriptorSetLayout(Device, DescriptorLayout, nullptr);
        if (Sampler)
            Functions->vkDestroySampler(Device, Sampler, nullptr);
        DestroyImage(DepthTarget);
        DestroyImage(AttributeTarget);
        DestroyImage(ColorTarget);
        DestroyImage(DepthSourceImage);
        DestroyImage(ColorSourceImage);
        DestroyBuffer(AttributeReadback);
        DestroyBuffer(ColorReadback);
        DestroyBuffer(DepthUpload);
        DestroyBuffer(ColorUpload);
    }

    void DestroyImage(ImageResource& resource)
    {
        if (resource.View)
            Functions->vkDestroyImageView(Device, resource.View, nullptr);
        if (resource.Image)
            Functions->vkDestroyImage(Device, resource.Image, nullptr);
        if (resource.Memory)
            Functions->vkFreeMemory(Device, resource.Memory, nullptr);
        resource = {};
    }

    void DestroyBuffer(BufferResource& resource)
    {
        if (resource.Buffer)
            Functions->vkDestroyBuffer(Device, resource.Buffer, nullptr);
        if (resource.Memory)
            Functions->vkFreeMemory(Device, resource.Memory, nullptr);
        resource = {};
    }

    std::shared_ptr<DeviceContext> Context;
    QWindow* Window = nullptr;
    QVulkanDeviceFunctions* Functions = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    int Scale = 1;
    melonDS::Vulkan::RasterTargetContract Contract;
    melonDS::Vulkan::ClearBitmapState State;
    std::vector<std::uint8_t> ColorSource;
    std::vector<std::uint32_t> DepthSource;
    BufferResource ColorUpload;
    BufferResource DepthUpload;
    BufferResource ColorReadback;
    BufferResource AttributeReadback;
    ImageResource ColorSourceImage;
    ImageResource DepthSourceImage;
    ImageResource ColorTarget;
    ImageResource AttributeTarget;
    ImageResource DepthTarget;
    VkSampler Sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
    VkRenderPass RenderPass = VK_NULL_HANDLE;
    VkFramebuffer Framebuffer = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkShaderModule VertexShader = VK_NULL_HANDLE;
    VkShaderModule FragmentShader = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

QJsonObject ProbeJson(const ProbeResult& result)
{
    QJsonArray samples;
    for (const auto& sample : result.Samples)
        samples.append(SampleJson(sample));
    return QJsonObject{
        {"passed", result.Passed},
        {"scale", result.Scale},
        {"contract_passed", result.ContractPassed},
        {"dirty_tracking_passed", result.DirtyTrackingPassed},
        {"color_source_uploaded", result.ColorSourceUploaded},
        {"depth_source_uploaded", result.DepthSourceUploaded},
        {"draw_submitted", result.DrawSubmitted},
        {"color_readback_completed", result.ColorReadbackCompleted},
        {"attribute_readback_completed", result.AttributeReadbackCompleted},
        {"color_samples_matched", result.ColorSamplesMatched},
        {"attribute_samples_matched", result.AttributeSamplesMatched},
        {"repeat_wrap_validated", result.RepeatWrapValidated},
        {"depth_write_submitted", result.DepthWriteSubmitted},
        {"stencil_write_submitted", result.StencilWriteSubmitted},
        {"failure_stage", QString::fromStdString(result.FailureStage)},
        {"vk_result", static_cast<int>(result.FailureResult)},
        {"samples", samples},
    };
}

} // namespace

int RunClearBitmapBootstrapHarness(const QString& outputPath, int iterations)
{
    if (iterations <= 0)
        iterations = 1;

    FeatureInfo lastInfo;
    ProbeResult lastResult;
    melonDS::Vulkan::ClearBitmapState lastState;
    melonDS::Vulkan::RasterTargetContract lastContract;
    QJsonArray iterationResults;
    QJsonArray testedScales;
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
            const int scale = kScaleSequence[
                std::min<std::size_t>(iteration, kScaleSequence.size() - 1)];
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
                ClearBitmapBootstrapProbe probe(context, &window, scale);
                lastResult = probe.Run();
                lastState = probe.state();
                lastContract = probe.contract();
            }
            context.reset();
            window.destroy();
            iterationResults.append(ProbeJson(lastResult));
            testedScales.append(scale);
            if (!lastResult.Passed)
                break;
            ++completed;
        }
    }

    const bool passed = completed == iterations;
    if (passed)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Info,
            "[MelonPrime] Vulkan clear-bitmap bootstrap passed: iterations=%d offsets=%u,%u polyid=%u\n",
            completed,
            lastState.OffsetX,
            lastState.OffsetY,
            lastState.OpaquePolyId);
    }
    else
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan clear-bitmap bootstrap failed: stage=%s VkResult=%d completed=%d/%d\n",
            lastResult.FailureStage.c_str(),
            static_cast<int>(lastResult.FailureResult),
            completed,
            iterations);
    }

    const QJsonObject result{
        {"schema_version", 1},
        {"passed", passed},
        {"contract_version", static_cast<int>(melonDS::Vulkan::kClearBitmapContractVersion)},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"tested_scales", testedScales},
        {"clear_bitmap_enabled", lastState.Enabled},
        {"offset_x", static_cast<int>(lastState.OffsetX)},
        {"offset_y", static_cast<int>(lastState.OffsetY)},
        {"opaque_poly_id", static_cast<int>(lastState.OpaquePolyId)},
        {"target_width", static_cast<int>(lastContract.Extent.width)},
        {"target_height", static_cast<int>(lastContract.Extent.height)},
        {"contract_passed", lastResult.ContractPassed},
        {"dirty_tracking_passed", lastResult.DirtyTrackingPassed},
        {"color_source_uploaded", lastResult.ColorSourceUploaded},
        {"depth_source_uploaded", lastResult.DepthSourceUploaded},
        {"draw_submitted", lastResult.DrawSubmitted},
        {"color_readback_completed", lastResult.ColorReadbackCompleted},
        {"attribute_readback_completed", lastResult.AttributeReadbackCompleted},
        {"color_samples_matched", lastResult.ColorSamplesMatched},
        {"attribute_samples_matched", lastResult.AttributeSamplesMatched},
        {"repeat_wrap_validated", lastResult.RepeatWrapValidated},
        {"depth_write_submitted", lastResult.DepthWriteSubmitted},
        {"stencil_write_submitted", lastResult.StencilWriteSubmitted},
        {"software_game_rendering_preserved", true},
        {"native_ds_polygon_raster_integrated", false},
        {"failure_stage", QString::fromStdString(lastResult.FailureStage)},
        {"vk_result", static_cast<int>(lastResult.FailureResult)},
        {"iterations", iterationResults},
    };
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 2;
    file.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
    return passed ? 0 : 1;
}

} // namespace MelonPrime::Vulkan
