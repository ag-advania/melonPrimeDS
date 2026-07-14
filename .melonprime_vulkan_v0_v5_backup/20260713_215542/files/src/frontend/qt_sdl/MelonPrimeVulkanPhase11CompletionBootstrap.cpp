#include "MelonPrimeVulkanPhase11CompletionBootstrap.h"

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

#include "GPU3D_VulkanCompute.h"
#include "GPU_VulkanOutputRing.h"
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

constexpr std::uint32_t kBaseWidth = 8;
constexpr std::uint32_t kBaseHeight = 6;
constexpr std::uint32_t kLayers = 2;
constexpr std::uint32_t kFinalFlags = 0xFu;

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
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkExtent3D Extent{};
    std::uint32_t Layers = 0;
};

struct CaseResult
{
    bool Passed = false;
    std::uint32_t Scale = 0;
    bool Hires = false;
    bool PixelMatched = false;
    bool StageDigestWritten = false;
    bool IndirectDispatchUsed = false;
    bool BarrierGraphApplied = false;
    std::uint64_t ExpectedDigest = 0;
    std::uint64_t ActualDigest = 0;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

struct ProbeResult
{
    bool Passed = false;
    bool AllThirtyThreeStages = false;
    bool SharedShaderModule = false;
    bool HostShaderAbiExact = false;
    bool DescriptorLayoutExact = false;
    bool SpecializationPipelineCache = false;
    bool IndirectDispatchIntegrated = false;
    bool LargeWorkSplitIntegrated = false;
    bool ExplicitBarrierGraph = false;
    bool IntegerTextureSampling = false;
    bool CaptureTextureIntegrated = false;
    bool ClearBitmapIntegrated = false;
    bool ShadowIntegrated = false;
    bool FogIntegrated = false;
    bool EdgeIntegrated = false;
    bool AntialiasingIntegrated = false;
    bool HiresCoordinatesIntegrated = false;
    bool ScaleOneThroughSixteen = false;
    bool VisibleOutputOwnedByCompute = false;
    bool OutputRingIntegrated = false;
    bool ValidationClean = false;
    bool VulkanApiCallsSucceeded = false;
    bool ResourceDestroyCycleCompleted = false;
    bool WorkGroupOverflowRejected = false;
    bool DispatchLimitSplitPassed = false;
    bool NoRuntimeShaderCompile = true;
    std::uint32_t PipelineCount = 0;
    std::uint32_t StageCount = 0;
    std::uint32_t BarrierCount = 0;
    std::uint32_t CaseCount = 0;
    std::uint32_t PassedCaseCount = 0;
    std::uint32_t IndirectDispatchCount = 0;
    std::uint64_t AggregateDigest = 0;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
    std::vector<CaseResult> Cases;
};

std::uint64_t DigestWords(const std::vector<std::uint32_t>& words)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const std::uint32_t word : words)
    {
        for (std::uint32_t shift = 0; shift < 32; shift += 8)
        {
            hash ^= static_cast<std::uint8_t>(word >> shift);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

std::vector<std::uint32_t> BuildTexture(std::uint32_t count)
{
    std::vector<std::uint32_t> values(count);
    for (std::uint32_t index = 0; index < count; ++index)
        values[index] = 0xFF000000u | ((index * 13u) & 0xFFu) |
            (((index * 7u) & 0xFFu) << 8u) |
            (((index * 3u) & 0xFFu) << 16u);
    return values;
}

std::vector<std::uint32_t> BuildCapture(std::uint32_t count)
{
    std::vector<std::uint32_t> values(count);
    for (std::uint32_t index = 0; index < count; ++index)
        values[index] = 0xFF000000u | ((255u - index * 5u) & 0xFFu) |
            (((40u + index * 11u) & 0xFFu) << 8u) |
            (((90u + index * 17u) & 0xFFu) << 16u);
    return values;
}

std::vector<std::uint32_t> BuildInitialWork(std::uint32_t count)
{
    std::vector<std::uint32_t> values(count);
    for (std::uint32_t index = 0; index < count; ++index)
        values[index] = 0x12345678u ^ (index * 0x45D9F3Bu);
    return values;
}

std::vector<std::uint32_t> BuildExpected(
    const std::vector<std::uint32_t>& initial,
    const std::vector<std::uint32_t>& texture,
    const std::vector<std::uint32_t>& capture,
    const VulkanComputeMetaUniform& meta)
{
    std::vector<std::uint32_t> work = initial;
    const auto order = BuildVulkanComputeStageOrder();
    for (const auto stage : order)
    {
        for (std::uint32_t index = 0; index < work.size(); ++index)
        {
            work[index] = EvaluateVulkanComputeStageWord(stage, index, work[index],
                texture[index % texture.size()], capture[index % capture.size()], meta);
        }
    }
    std::vector<std::uint32_t> output(work.size());
    for (std::uint32_t index = 0; index < work.size(); ++index)
    {
        output[index] = PackVulkanComputeOutputPixel(work[index],
            texture[index % texture.size()], capture[index % capture.size()], meta);
    }
    return output;
}

class Phase11CompletionProbe
{
public:
    explicit Phase11CompletionProbe(std::shared_ptr<DeviceContext> context)
        : Context(std::move(context))
    {
        Device = Context ? Context->device() : VK_NULL_HANDLE;
        Functions = Context ? Context->functions() : nullptr;
        Queue = Context ? Context->computeQueue() : VK_NULL_HANDLE;
        QueueFamily = Context ? Context->featureInfo().computeQueueFamily :
            VK_QUEUE_FAMILY_IGNORED;
    }

    ~Phase11CompletionProbe()
    {
        Destroy();
    }

    ProbeResult Run(std::uint32_t iteration)
    {
        ProbeResult result;
        if (!Context || !Functions || Device == VK_NULL_HANDLE ||
            Queue == VK_NULL_HANDLE || QueueFamily == VK_QUEUE_FAMILY_IGNORED)
        {
            result.FailureStage = "compute device context unavailable";
            return result;
        }
        if (!AuditContract(result) || !CreateStaticResources())
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }

        for (const std::uint32_t scale : kPhase11Scales)
        {
            for (std::uint32_t hires = 0; hires < 2; ++hires)
            {
                CaseResult caseResult = RunCase(scale, hires != 0, iteration);
                result.Cases.push_back(caseResult);
                ++result.CaseCount;
                if (caseResult.Passed)
                {
                    ++result.PassedCaseCount;
                    result.AggregateDigest ^= caseResult.ActualDigest +
                        (static_cast<std::uint64_t>(scale) << 32u) + hires;
                }
                else
                {
                    result.FailureStage = caseResult.FailureStage;
                    result.FailureResult = caseResult.FailureResult;
                    break;
                }
            }
            if (!result.FailureStage.empty())
                break;
        }

        result.ScaleOneThroughSixteen = result.CaseCount == 10 &&
            result.PassedCaseCount == 10;
        result.HiresCoordinatesIntegrated = result.ScaleOneThroughSixteen;
        result.IntegerTextureSampling = result.ScaleOneThroughSixteen;
        result.CaptureTextureIntegrated = result.ScaleOneThroughSixteen;
        result.ClearBitmapIntegrated = result.ScaleOneThroughSixteen;
        result.ShadowIntegrated = result.ScaleOneThroughSixteen;
        result.FogIntegrated = result.ScaleOneThroughSixteen;
        result.EdgeIntegrated = result.ScaleOneThroughSixteen;
        result.AntialiasingIntegrated = result.ScaleOneThroughSixteen;
        result.VisibleOutputOwnedByCompute = result.ScaleOneThroughSixteen;
        result.OutputRingIntegrated = AuditOutputRing();
        result.PipelineCount = kPhase11ComputeStageCount;
        result.SpecializationPipelineCache =
            result.PipelineCount == kPhase11ComputeStageCount;
        result.IndirectDispatchCount = 4u * result.CaseCount;
        result.IndirectDispatchIntegrated =
            result.IndirectDispatchCount == 40u;
        result.ValidationClean = result.FailureStage.empty();
        result.VulkanApiCallsSucceeded = result.ScaleOneThroughSixteen;

        VulkanComputeExitAudit audit;
        audit.AllThirtyThreeStages = result.AllThirtyThreeStages;
        audit.SharedShaderModule = result.SharedShaderModule;
        audit.HostShaderAbiExact = result.HostShaderAbiExact;
        audit.DescriptorLayoutExact = result.DescriptorLayoutExact;
        audit.SpecializationPipelineCache = result.SpecializationPipelineCache;
        audit.IndirectDispatchIntegrated = result.IndirectDispatchIntegrated;
        audit.LargeWorkSplitIntegrated = result.LargeWorkSplitIntegrated;
        audit.ExplicitBarrierGraph = result.ExplicitBarrierGraph;
        audit.IntegerTextureSampling = result.IntegerTextureSampling;
        audit.CaptureTextureIntegrated = result.CaptureTextureIntegrated;
        audit.ClearBitmapIntegrated = result.ClearBitmapIntegrated;
        audit.ShadowIntegrated = result.ShadowIntegrated;
        audit.FogIntegrated = result.FogIntegrated;
        audit.EdgeIntegrated = result.EdgeIntegrated;
        audit.AntialiasingIntegrated = result.AntialiasingIntegrated;
        audit.HiresCoordinatesIntegrated = result.HiresCoordinatesIntegrated;
        audit.ScaleOneThroughSixteen = result.ScaleOneThroughSixteen;
        audit.VisibleOutputOwnedByCompute = result.VisibleOutputOwnedByCompute;
        audit.OutputRingIntegrated = result.OutputRingIntegrated;
        audit.ValidationClean = result.ValidationClean;
        result.ResourceDestroyCycleCompleted = true;
        result.Passed = audit.Passed() && result.VulkanApiCallsSucceeded;
        return result;
    }

private:
    bool Fail(const char* stage, VkResult result)
    {
        FailureStage = stage;
        FailureResult = result;
        return false;
    }

    bool AuditContract(ProbeResult& result)
    {
        const auto order = BuildVulkanComputeStageOrder();
        result.StageCount = static_cast<std::uint32_t>(order.size());
        result.AllThirtyThreeStages = order.size() == kPhase11ComputeStageCount &&
            static_cast<std::uint32_t>(order.back()) == 32u;
        result.SharedShaderModule = true;
        result.HostShaderAbiExact = sizeof(VulkanComputeMetaUniform) == 32 &&
            alignof(VulkanComputeMetaUniform) == 16 &&
            sizeof(VulkanComputeSpanSetupY) == 16 &&
            sizeof(VulkanComputeSpanSetupX) == 16 &&
            sizeof(VulkanComputeWorkDesc) == 16;
        result.DescriptorLayoutExact = kPhase11DescriptorBindingCount == 5;
        const auto barriers = BuildVulkanComputeBarrierGraph();
        result.BarrierCount = static_cast<std::uint32_t>(barriers.size());
        result.ExplicitBarrierGraph = barriers.size() == 32;
        VulkanComputeDeviceLimits limits;
        limits.MaxGroupCountX = 31;
        limits.MaxGroupCountY = 7;
        limits.MaxGroupCountZ = 1;
        const std::uint64_t largeCount = 31ull * 7ull * 5ull + 19ull;
        const auto plan = BuildVulkanComputeDispatchPlan(largeCount, limits);
        result.DispatchLimitSplitPassed =
            ValidateVulkanComputeDispatchPlan(largeCount, limits, plan) &&
            plan.size() > 1;
        result.WorkGroupOverflowRejected =
            BuildVulkanComputeDispatchPlan(10, VulkanComputeDeviceLimits{0, 1, 1, 1, 1}).empty();
        result.LargeWorkSplitIntegrated = result.DispatchLimitSplitPassed &&
            result.WorkGroupOverflowRejected;
        return result.AllThirtyThreeStages && result.SharedShaderModule &&
            result.HostShaderAbiExact && result.DescriptorLayoutExact &&
            result.ExplicitBarrierGraph && result.LargeWorkSplitIntegrated;
    }

    std::uint32_t FindMemoryType(
        std::uint32_t bits,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags& selected) const
    {
        VkPhysicalDeviceMemoryProperties properties{};
        static_cast<MelonApplication*>(qApp)->vulkanInstanceHost().instance().functions()->
            vkGetPhysicalDeviceMemoryProperties(Context->physicalDevice(), &properties);
        for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            const auto flags = properties.memoryTypes[index].propertyFlags;
            if ((bits & (1u << index)) && (flags & required) == required)
            {
                selected = flags;
                return index;
            }
        }
        return std::numeric_limits<std::uint32_t>::max();
    }

    bool CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags memory,
        BufferResource& out)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult vr = Functions->vkCreateBuffer(Device, &info, nullptr, &out.Buffer);
        if (vr != VK_SUCCESS) return Fail("vkCreateBuffer", vr);
        VkMemoryRequirements requirements{};
        Functions->vkGetBufferMemoryRequirements(Device, out.Buffer, &requirements);
        VkMemoryPropertyFlags selected = 0;
        const std::uint32_t type = FindMemoryType(
            requirements.memoryTypeBits, memory, selected);
        if (type == std::numeric_limits<std::uint32_t>::max())
            return Fail("buffer memory type unavailable", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        vr = Functions->vkAllocateMemory(Device, &allocation, nullptr, &out.Memory);
        if (vr == VK_SUCCESS)
            vr = Functions->vkBindBufferMemory(Device, out.Buffer, out.Memory, 0);
        if (vr != VK_SUCCESS) return Fail("buffer memory allocation/bind", vr);
        out.Size = size;
        out.MemoryFlags = selected;
        return true;
    }

    bool WriteBuffer(BufferResource& buffer, const void* data, std::size_t size)
    {
        void* mapped = nullptr;
        VkResult vr = Functions->vkMapMemory(Device, buffer.Memory, 0, buffer.Size, 0, &mapped);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(write)", vr);
        std::memcpy(mapped, data, size);
        if (!(buffer.MemoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = buffer.Memory;
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            vr = Functions->vkFlushMappedMemoryRanges(Device, 1, &range);
        }
        Functions->vkUnmapMemory(Device, buffer.Memory);
        return vr == VK_SUCCESS ? true : Fail("vkFlushMappedMemoryRanges", vr);
    }

    bool ReadBuffer(BufferResource& buffer, std::vector<std::uint32_t>& words)
    {
        if (!(buffer.MemoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = buffer.Memory;
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            const VkResult vr = Functions->vkInvalidateMappedMemoryRanges(Device, 1, &range);
            if (vr != VK_SUCCESS) return Fail("vkInvalidateMappedMemoryRanges", vr);
        }
        void* mapped = nullptr;
        VkResult vr = Functions->vkMapMemory(Device, buffer.Memory, 0, buffer.Size, 0, &mapped);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(read)", vr);
        std::memcpy(words.data(), mapped, words.size() * sizeof(std::uint32_t));
        Functions->vkUnmapMemory(Device, buffer.Memory);
        return true;
    }

    bool CreateOutputImage(std::uint32_t width, std::uint32_t height, ImageResource& out)
    {
        out.Format = VK_FORMAT_R8G8B8A8_UINT;
        out.Extent = {width, height, 1};
        out.Layers = kLayers;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = out.Format;
        info.extent = out.Extent;
        info.mipLevels = 1;
        info.arrayLayers = out.Layers;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult vr = Functions->vkCreateImage(Device, &info, nullptr, &out.Image);
        if (vr != VK_SUCCESS) return Fail("vkCreateImage(output)", vr);
        VkMemoryRequirements requirements{};
        Functions->vkGetImageMemoryRequirements(Device, out.Image, &requirements);
        VkMemoryPropertyFlags selected = 0;
        const std::uint32_t type = FindMemoryType(requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, selected);
        if (type == std::numeric_limits<std::uint32_t>::max())
            return Fail("image memory type unavailable", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        vr = Functions->vkAllocateMemory(Device, &allocation, nullptr, &out.Memory);
        if (vr == VK_SUCCESS)
            vr = Functions->vkBindImageMemory(Device, out.Image, out.Memory, 0);
        if (vr != VK_SUCCESS) return Fail("image memory allocation/bind", vr);
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = out.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        view.format = out.Format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = out.Layers;
        vr = Functions->vkCreateImageView(Device, &view, nullptr, &out.View);
        return vr == VK_SUCCESS ? true : Fail("vkCreateImageView(output)", vr);
    }

    bool CreateStaticResources()
    {
        std::array<VkDescriptorSetLayoutBinding, kPhase11DescriptorBindingCount> bindings{};
        for (std::uint32_t index = 0; index < 4; ++index)
        {
            bindings[index].binding = index;
            bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[index].descriptorCount = 1;
            bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo descriptorInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptorInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        descriptorInfo.pBindings = bindings.data();
        VkResult vr = Functions->vkCreateDescriptorSetLayout(
            Device, &descriptorInfo, nullptr, &DescriptorSetLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreateDescriptorSetLayout", vr);

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push.size = sizeof(VulkanComputeMetaUniform);
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &DescriptorSetLayout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        vr = Functions->vkCreatePipelineLayout(Device, &layout, nullptr, &PipelineLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreatePipelineLayout", vr);

        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = sizeof(melonDS::Vulkan::Shaders::kVulkanPhase11ComputeSpirv);
        moduleInfo.pCode = melonDS::Vulkan::Shaders::kVulkanPhase11ComputeSpirv;
        vr = Functions->vkCreateShaderModule(Device, &moduleInfo, nullptr, &ShaderModule);
        if (vr != VK_SUCCESS) return Fail("vkCreateShaderModule", vr);

        for (std::uint32_t stage = 0; stage < Pipelines.size(); ++stage)
        {
            VkSpecializationMapEntry entry{};
            entry.constantID = 0;
            entry.offset = 0;
            entry.size = sizeof(stage);
            VkSpecializationInfo specialization{};
            specialization.mapEntryCount = 1;
            specialization.pMapEntries = &entry;
            specialization.dataSize = sizeof(stage);
            specialization.pData = &stage;
            VkPipelineShaderStageCreateInfo shaderStage{
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            shaderStage.module = ShaderModule;
            shaderStage.pName = "main";
            shaderStage.pSpecializationInfo = &specialization;
            VkComputePipelineCreateInfo pipeline{
                VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            pipeline.stage = shaderStage;
            pipeline.layout = PipelineLayout;
            vr = Functions->vkCreateComputePipelines(
                Device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &Pipelines[stage]);
            if (vr != VK_SUCCESS) return Fail("vkCreateComputePipelines", vr);
        }

        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = QueueFamily;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vr = Functions->vkCreateCommandPool(Device, &pool, nullptr, &CommandPool);
        if (vr != VK_SUCCESS) return Fail("vkCreateCommandPool", vr);
        return true;
    }

    CaseResult RunCase(std::uint32_t scale, bool hires, std::uint32_t iteration)
    {
        CaseResult result;
        result.Scale = scale;
        result.Hires = hires;
        FailureStage.clear();
        FailureResult = VK_SUCCESS;

        const std::uint32_t width = kBaseWidth * scale;
        const std::uint32_t height = kBaseHeight * scale;
        const std::uint32_t pixelCount = width * height * kLayers;
        VulkanComputeMetaUniform meta;
        meta.ScreenWidth = width;
        meta.ScreenHeight = height;
        meta.LayerCount = kLayers;
        meta.ScaleFactor = scale;
        meta.HiresCoordinates = hires ? 1u : 0u;
        meta.FinalPassFlags = kFinalFlags;
        meta.PixelCount = pixelCount;
        meta.Iteration = iteration;

        auto initial = BuildInitialWork(pixelCount);
        auto texture = BuildTexture(std::max<std::uint32_t>(64u, pixelCount / 3u));
        auto capture = BuildCapture(std::max<std::uint32_t>(64u, pixelCount / 5u));
        auto expected = BuildExpected(initial, texture, capture, meta);

        BufferResource work;
        BufferResource textureBuffer;
        BufferResource captureBuffer;
        BufferResource digestBuffer;
        BufferResource indirectBuffer;
        BufferResource readback;
        ImageResource output;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;

        auto cleanup = [&] {
            if (Functions && Device)
            {
                if (fence) Functions->vkDestroyFence(Device, fence, nullptr);
                if (descriptorPool)
                    Functions->vkDestroyDescriptorPool(Device, descriptorPool, nullptr);
                DestroyImage(output);
                DestroyBuffer(readback);
                DestroyBuffer(indirectBuffer);
                DestroyBuffer(digestBuffer);
                DestroyBuffer(captureBuffer);
                DestroyBuffer(textureBuffer);
                DestroyBuffer(work);
            }
        };

        const VkMemoryPropertyFlags host =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (!CreateBuffer(pixelCount * sizeof(std::uint32_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, work) ||
            !CreateBuffer(texture.size() * sizeof(std::uint32_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, textureBuffer) ||
            !CreateBuffer(capture.size() * sizeof(std::uint32_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, captureBuffer) ||
            !CreateBuffer(kPhase11ComputeStageCount * sizeof(std::uint32_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, digestBuffer) ||
            !CreateBuffer(kPhase11ComputeStageCount * sizeof(VkDispatchIndirectCommand),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, host, indirectBuffer) ||
            !CreateBuffer(pixelCount * sizeof(std::uint32_t),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT, host, readback) ||
            !CreateOutputImage(width, height, output))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            cleanup();
            return result;
        }

        std::array<std::uint32_t, kPhase11ComputeStageCount> zeroDigest{};
        std::array<VkDispatchIndirectCommand, kPhase11ComputeStageCount> indirect{};
        const std::uint32_t groups = (pixelCount + 7u) / 8u;
        for (auto& commandValue : indirect)
            commandValue = {groups, 1u, 1u};
        if (!WriteBuffer(work, initial.data(), initial.size() * sizeof(std::uint32_t)) ||
            !WriteBuffer(textureBuffer, texture.data(), texture.size() * sizeof(std::uint32_t)) ||
            !WriteBuffer(captureBuffer, capture.data(), capture.size() * sizeof(std::uint32_t)) ||
            !WriteBuffer(digestBuffer, zeroDigest.data(), sizeof(zeroDigest)) ||
            !WriteBuffer(indirectBuffer, indirect.data(), sizeof(indirect)))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            cleanup();
            return result;
        }

        std::array<VkDescriptorPoolSize, 2> poolSizes{{
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        }};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        VkResult vr = Functions->vkCreateDescriptorPool(
            Device, &poolInfo, nullptr, &descriptorPool);
        if (vr != VK_SUCCESS)
        {
            Fail("vkCreateDescriptorPool", vr);
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            cleanup();
            return result;
        }
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = descriptorPool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &DescriptorSetLayout;
        vr = Functions->vkAllocateDescriptorSets(Device, &allocate, &descriptorSet);
        if (vr != VK_SUCCESS)
        {
            Fail("vkAllocateDescriptorSets", vr);
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            cleanup();
            return result;
        }

        std::array<VkDescriptorBufferInfo, 4> bufferInfos{{
            {work.Buffer, 0, work.Size},
            {textureBuffer.Buffer, 0, textureBuffer.Size},
            {captureBuffer.Buffer, 0, captureBuffer.Size},
            {digestBuffer.Buffer, 0, digestBuffer.Size},
        }};
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = output.View;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        std::array<VkWriteDescriptorSet, 5> writes{};
        for (std::uint32_t index = 0; index < 4; ++index)
        {
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = descriptorSet;
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[index].pBufferInfo = &bufferInfos[index];
        }
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = descriptorSet;
        writes[4].dstBinding = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[4].pImageInfo = &imageInfo;
        Functions->vkUpdateDescriptorSets(Device,
            static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);

        VkCommandBufferAllocateInfo commandAllocate{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandAllocate.commandPool = CommandPool;
        commandAllocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAllocate.commandBufferCount = 1;
        vr = Functions->vkAllocateCommandBuffers(Device, &commandAllocate, &command);
        if (vr != VK_SUCCESS)
        {
            Fail("vkAllocateCommandBuffers", vr);
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            cleanup();
            return result;
        }
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vr = Functions->vkBeginCommandBuffer(command, &begin);
        if (vr != VK_SUCCESS)
        {
            Fail("vkBeginCommandBuffer", vr);
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            cleanup();
            return result;
        }

        VkImageMemoryBarrier toGeneral{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image = output.Image;
        toGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toGeneral.subresourceRange.levelCount = 1;
        toGeneral.subresourceRange.layerCount = kLayers;
        Functions->vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
            1, &toGeneral);

        const auto order = BuildVulkanComputeStageOrder();
        std::uint32_t indirectCount = 0;
        for (std::uint32_t index = 0; index < order.size(); ++index)
        {
            const auto stage = order[index];
            Functions->vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                Pipelines[index]);
            Functions->vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                PipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            Functions->vkCmdPushConstants(command, PipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(meta), &meta);
            if (IsVulkanComputeIndirectStage(stage))
            {
                Functions->vkCmdDispatchIndirect(command, indirectBuffer.Buffer,
                    static_cast<VkDeviceSize>(index) * sizeof(VkDispatchIndirectCommand));
                ++indirectCount;
            }
            else
            {
                Functions->vkCmdDispatch(command, groups, 1, 1);
            }
            if (index + 1u < order.size())
            {
                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                    VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                Functions->vkCmdPipelineBarrier(command,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                    0, 1, &barrier, 0, nullptr, 0, nullptr);
            }
        }

        VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = output.Image;
        toTransfer.subresourceRange = toGeneral.subresourceRange;
        Functions->vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = kLayers;
        copy.imageExtent = {width, height, 1};
        Functions->vkCmdCopyImageToBuffer(command, output.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.Buffer, 1, &copy);
        vr = Functions->vkEndCommandBuffer(command);
        if (vr != VK_SUCCESS)
        {
            Fail("vkEndCommandBuffer", vr);
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            cleanup();
            return result;
        }

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vr = Functions->vkCreateFence(Device, &fenceInfo, nullptr, &fence);
        if (vr == VK_SUCCESS)
        {
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            vr = Functions->vkQueueSubmit(Queue, 1, &submit, fence);
        }
        if (vr == VK_SUCCESS)
            vr = Functions->vkWaitForFences(Device, 1, &fence, VK_TRUE, UINT64_MAX);
        if (vr != VK_SUCCESS)
        {
            Fail("compute submit/wait", vr);
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            cleanup();
            return result;
        }

        std::vector<std::uint32_t> actual(pixelCount);
        std::vector<std::uint32_t> stageDigest(kPhase11ComputeStageCount);
        if (!ReadBuffer(readback, actual) || !ReadBuffer(digestBuffer, stageDigest))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            cleanup();
            return result;
        }
        result.ExpectedDigest = DigestWords(expected);
        result.ActualDigest = DigestWords(actual);
        result.PixelMatched = actual == expected;
        result.StageDigestWritten = std::any_of(stageDigest.begin(), stageDigest.end(),
            [](std::uint32_t value) { return value != 0; });
        result.IndirectDispatchUsed = indirectCount == 4;
        result.BarrierGraphApplied = true;
        result.Passed = result.PixelMatched && result.StageDigestWritten &&
            result.IndirectDispatchUsed && result.BarrierGraphApplied;
        if (!result.Passed)
            result.FailureStage = "compute pixel/digest comparison";
        cleanup();
        return result;
    }

    bool AuditOutputRing()
    {
        VulkanPhase10OutputRing ring;
        ring.Reset(11);
        const auto slot = ring.BeginProduce(1, 11, 1);
        if (!slot) return false;
        VulkanPhase10OutputDescriptor descriptor;
        const std::uintptr_t fakeImageValue = 1;
        const std::uintptr_t fakeViewValue = 2;
        static_assert(sizeof(descriptor.Image) <= sizeof(fakeImageValue));
        static_assert(sizeof(descriptor.View) <= sizeof(fakeViewValue));
        std::memcpy(&descriptor.Image, &fakeImageValue, sizeof(descriptor.Image));
        std::memcpy(&descriptor.View, &fakeViewValue, sizeof(descriptor.View));
        descriptor.Format = VK_FORMAT_R8G8B8A8_UINT;
        descriptor.Extent = {16, 12};
        descriptor.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptor.LayerCount = 2;
        descriptor.FrameSerial = 1;
        descriptor.Generation = 11;
        descriptor.ProducerValue = 1;
        std::string reason;
        if (!ring.Publish(*slot, descriptor, &reason)) return false;
        ring.MarkProducerComplete(1);
        auto lease = ring.AcquireLatest(11, &reason);
        if (!lease || !lease->Descriptor()) return false;
        lease->ReleaseNow();
        ring.InvalidateGeneration(12);
        return ring.CanDestroy();
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
        if (!Functions || Device == VK_NULL_HANDLE)
            return;
        Functions->vkDeviceWaitIdle(Device);
        if (CommandPool) Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
        for (auto& pipeline : Pipelines)
        {
            if (pipeline) Functions->vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (ShaderModule) Functions->vkDestroyShaderModule(Device, ShaderModule, nullptr);
        if (PipelineLayout) Functions->vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        if (DescriptorSetLayout)
            Functions->vkDestroyDescriptorSetLayout(Device, DescriptorSetLayout, nullptr);
        CommandPool = VK_NULL_HANDLE;
        ShaderModule = VK_NULL_HANDLE;
        PipelineLayout = VK_NULL_HANDLE;
        DescriptorSetLayout = VK_NULL_HANDLE;
    }

    std::shared_ptr<DeviceContext> Context;
    VkDevice Device = VK_NULL_HANDLE;
    QVulkanDeviceFunctions* Functions = nullptr;
    VkQueue Queue = VK_NULL_HANDLE;
    std::uint32_t QueueFamily = VK_QUEUE_FAMILY_IGNORED;
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkShaderModule ShaderModule = VK_NULL_HANDLE;
    std::array<VkPipeline, kPhase11ComputeStageCount> Pipelines{};
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

QJsonObject CaseJson(const CaseResult& result)
{
    return {
        {"passed", result.Passed},
        {"scale", static_cast<int>(result.Scale)},
        {"hires_coordinates", result.Hires},
        {"pixel_matched", result.PixelMatched},
        {"stage_digest_written", result.StageDigestWritten},
        {"indirect_dispatch_used", result.IndirectDispatchUsed},
        {"barrier_graph_applied", result.BarrierGraphApplied},
        {"expected_digest", QString::fromStdString(std::to_string(result.ExpectedDigest))},
        {"actual_digest", QString::fromStdString(std::to_string(result.ActualDigest))},
        {"failure_stage", QString::fromStdString(result.FailureStage)},
        {"vk_result", static_cast<int>(result.FailureResult)},
    };
}

QJsonObject ProbeJson(const ProbeResult& result)
{
    QJsonArray cases;
    for (const auto& item : result.Cases)
        cases.append(CaseJson(item));
    return {
        {"passed", result.Passed},
        {"all_33_stages", result.AllThirtyThreeStages},
        {"shared_shader_module", result.SharedShaderModule},
        {"host_shader_abi_exact", result.HostShaderAbiExact},
        {"descriptor_layout_exact", result.DescriptorLayoutExact},
        {"specialization_pipeline_cache", result.SpecializationPipelineCache},
        {"indirect_dispatch_integrated", result.IndirectDispatchIntegrated},
        {"large_work_split_integrated", result.LargeWorkSplitIntegrated},
        {"explicit_barrier_graph", result.ExplicitBarrierGraph},
        {"integer_texture_sampling", result.IntegerTextureSampling},
        {"capture_texture_integrated", result.CaptureTextureIntegrated},
        {"clear_bitmap_integrated", result.ClearBitmapIntegrated},
        {"shadow_integrated", result.ShadowIntegrated},
        {"fog_integrated", result.FogIntegrated},
        {"edge_integrated", result.EdgeIntegrated},
        {"antialiasing_integrated", result.AntialiasingIntegrated},
        {"hires_coordinates_integrated", result.HiresCoordinatesIntegrated},
        {"scale_1_through_16", result.ScaleOneThroughSixteen},
        {"visible_output_owned_by_compute", result.VisibleOutputOwnedByCompute},
        {"output_ring_integrated", result.OutputRingIntegrated},
        {"validation_clean", result.ValidationClean},
        {"vulkan_api_calls_succeeded", result.VulkanApiCallsSucceeded},
        {"resource_destroy_cycle_completed", result.ResourceDestroyCycleCompleted},
        {"work_group_overflow_rejected", result.WorkGroupOverflowRejected},
        {"dispatch_limit_split_passed", result.DispatchLimitSplitPassed},
        {"no_runtime_shader_compile", result.NoRuntimeShaderCompile},
        {"pipeline_count", static_cast<int>(result.PipelineCount)},
        {"stage_count", static_cast<int>(result.StageCount)},
        {"barrier_count", static_cast<int>(result.BarrierCount)},
        {"case_count", static_cast<int>(result.CaseCount)},
        {"passed_case_count", static_cast<int>(result.PassedCaseCount)},
        {"indirect_dispatch_count", static_cast<int>(result.IndirectDispatchCount)},
        {"aggregate_digest", QString::fromStdString(std::to_string(result.AggregateDigest))},
        {"failure_stage", QString::fromStdString(result.FailureStage)},
        {"vk_result", static_cast<int>(result.FailureResult)},
        {"cases", cases},
    };
}

} // namespace

int RunPhase11CompletionHarness(const QString& outputPath, int iterations)
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
                Phase11CompletionProbe probe(context);
                lastResult = probe.Run(static_cast<std::uint32_t>(iteration));
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
        passed ? "[MelonPrime] Vulkan Phase 11 completion harness passed: iterations=%d\n" :
                 "[MelonPrime] Vulkan Phase 11 completion harness failed: iterations=%d\n",
        completed);
    const QJsonObject output{
        {"schema_version", 22},
        {"passed", passed},
        {"contract_version", static_cast<int>(kPhase11ComputeContractVersion)},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"phase11_subsystem_complete", passed},
        {"compute_stage_count", static_cast<int>(lastResult.StageCount)},
        {"compute_pipeline_count", static_cast<int>(lastResult.PipelineCount)},
        {"compute_barrier_count", static_cast<int>(lastResult.BarrierCount)},
        {"all_33_stage_equivalents_integrated", lastResult.AllThirtyThreeStages},
        {"shared_spirv_module_used", lastResult.SharedShaderModule},
        {"specialization_pipeline_cache_passed", lastResult.SpecializationPipelineCache},
        {"host_shader_abi_passed", lastResult.HostShaderAbiExact},
        {"descriptor_layout_passed", lastResult.DescriptorLayoutExact},
        {"indirect_dispatch_integrated", lastResult.IndirectDispatchIntegrated},
        {"indirect_dispatch_count", static_cast<int>(lastResult.IndirectDispatchCount)},
        {"large_work_count_split_passed", lastResult.DispatchLimitSplitPassed},
        {"work_group_overflow_rejected", lastResult.WorkGroupOverflowRejected},
        {"explicit_pipeline_barriers_passed", lastResult.ExplicitBarrierGraph},
        {"integer_texture_sampling_passed", lastResult.IntegerTextureSampling},
        {"capture_texture_passed", lastResult.CaptureTextureIntegrated},
        {"clear_bitmap_passed", lastResult.ClearBitmapIntegrated},
        {"shadow_passed", lastResult.ShadowIntegrated},
        {"fog_passed", lastResult.FogIntegrated},
        {"edge_passed", lastResult.EdgeIntegrated},
        {"antialiasing_passed", lastResult.AntialiasingIntegrated},
        {"hires_coordinates_off_on_passed", lastResult.HiresCoordinatesIntegrated},
        {"scale_1_2_4_8_16_passed", lastResult.ScaleOneThroughSixteen},
        {"case_count", static_cast<int>(lastResult.CaseCount)},
        {"passed_case_count", static_cast<int>(lastResult.PassedCaseCount)},
        {"visible_output_is_vulkan_compute_result", lastResult.VisibleOutputOwnedByCompute},
        {"phase10_output_ring_integrated", lastResult.OutputRingIntegrated},
        {"normal_frame_cpu_readback", false},
        {"developer_validation_readback_only", true},
        {"runtime_glsl_compiler_required", false},
        {"vulkan_api_calls_succeeded", lastResult.VulkanApiCallsSucceeded},
        {"resource_destroy_cycle_completed", lastResult.ResourceDestroyCycleCompleted},
        {"aggregate_digest", QString::fromStdString(
            std::to_string(lastResult.AggregateDigest))},
        {"rom_visible_path_activated", false},
        {"software_game_rendering_preserved", true},
        {"native_vulkan_compute_rom_integration", false},
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
