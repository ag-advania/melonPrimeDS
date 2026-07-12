#include "MelonPrimeVulkanPolygonBatchBootstrap.h"

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSurface>
#include <QString>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QWindow>

#include <cstddef>
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

namespace MelonPrime::Vulkan
{
namespace
{

using melonDS::Vulkan::VulkanPackedPolygon;
using melonDS::Vulkan::VulkanPackedVertex;
using melonDS::Vulkan::VulkanRasterBatch;
using melonDS::Vulkan::VulkanRasterBatchOptions;
using melonDS::Vulkan::VulkanRasterBatchPlan;
using melonDS::Vulkan::VulkanRasterPipelineKey;
using melonDS::Vulkan::VulkanRasterPolygonFlag_Shadow;
using melonDS::Vulkan::VulkanRasterPolygonFlag_ShadowMask;
using melonDS::Vulkan::VulkanRasterPolygonFlag_Textured;
using melonDS::Vulkan::VulkanRasterPolygonFlag_Translucent;
using melonDS::Vulkan::VulkanRasterPolygonFlag_WBuffer;
using melonDS::Vulkan::VulkanRasterPrimitive;
using melonDS::Vulkan::VulkanRasterRenderMode;
using melonDS::Vulkan::VulkanRasterSamplerAxisMode;
using melonDS::Vulkan::VulkanRasterTextureKey;
using melonDS::Vulkan::VulkanRasterToonMode;
using melonDS::Vulkan::VulkanRasterUpload;

struct BufferResource
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    std::uint32_t MemoryType = std::numeric_limits<std::uint32_t>::max();
};

struct ContractResult
{
    bool Passed = false;
    bool LayoutPassed = false;
    bool SourceOrderPassed = false;
    bool AdjacentOnlyPassed = false;
    bool FrameWideRegroupRejected = false;
    bool TextureSamplerPassed = false;
    bool PipelineKeyPassed = false;
    bool InvalidOrderRejected = false;
    bool InvalidRangeRejected = false;
    VulkanRasterUpload Upload;
    VulkanRasterBatchPlan Plan;
    std::string FailureReason;
};

struct ProbeResult
{
    bool Passed = false;
    bool UploadSubmitted = false;
    bool ReadbackCompleted = false;
    bool PayloadMatched = false;
    bool DeviceLocalBuffer = false;
    std::size_t PayloadBytes = 0;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

void AddPolygon(
    VulkanRasterUpload& upload,
    std::uint32_t sourceOrder,
    VulkanRasterPrimitive primitive,
    std::uint32_t flags,
    std::uint32_t attr,
    std::uint32_t texParam,
    std::uint32_t texPalette,
    std::uint32_t textureLayer,
    std::uint32_t textureRepeat)
{
    VulkanPackedPolygon polygon;
    polygon.SourceOrder = sourceOrder;
    polygon.Primitive = static_cast<std::uint32_t>(primitive);
    polygon.VertexOffset = static_cast<std::uint32_t>(upload.Vertices.size());
    polygon.VertexCount = primitive == VulkanRasterPrimitive::Lines ? 2u : 3u;
    polygon.IndexOffset = static_cast<std::uint32_t>(upload.Indices.size());
    polygon.IndexCount = primitive == VulkanRasterPrimitive::Lines ? 2u : 3u;
    polygon.EdgeIndexOffset = static_cast<std::uint32_t>(upload.EdgeIndices.size());
    polygon.EdgeIndexCount = primitive == VulkanRasterPrimitive::Lines ? 2u : 6u;
    polygon.Attr = attr;
    polygon.TexParam = texParam;
    polygon.TexPalette = texPalette;
    polygon.TextureLayer = textureLayer;
    polygon.TextureRepeat = textureRepeat;
    polygon.Flags = flags;

    upload.Vertices.resize(upload.Vertices.size() + polygon.VertexCount);
    upload.Indices.resize(upload.Indices.size() + polygon.IndexCount);
    upload.EdgeIndices.resize(upload.EdgeIndices.size() + polygon.EdgeIndexCount);
    upload.Polygons.push_back(polygon);
}

VulkanRasterUpload BuildSyntheticUpload()
{
    VulkanRasterUpload upload;
    upload.SourcePolygonCount = 13;
    upload.SkippedDegenerateCount = 1;

    const std::uint32_t opaqueAttr = (31u << 16) | (5u << 24);
    const std::uint32_t texturedAttr = opaqueAttr | (1u << 4);
    const std::uint32_t textureParam =
        (4u << 26) | (2u << 20) | (3u << 23) | (0xBu << 16) | 0x1234u;

    AddPolygon(upload, 0, VulkanRasterPrimitive::Triangles, 0,
        opaqueAttr, 0, 0, 0xFFFFFFFFu, 0);
    AddPolygon(upload, 1, VulkanRasterPrimitive::Triangles, 0,
        opaqueAttr, 0, 0, 0xFFFFFFFFu, 0);

    AddPolygon(upload, 2, VulkanRasterPrimitive::Triangles,
        VulkanRasterPolygonFlag_Textured | VulkanRasterPolygonFlag_WBuffer,
        texturedAttr, textureParam, 0x45u, 7u, 0xBu);
    AddPolygon(upload, 3, VulkanRasterPrimitive::Triangles,
        VulkanRasterPolygonFlag_Textured | VulkanRasterPolygonFlag_WBuffer,
        texturedAttr, textureParam, 0x45u, 7u, 0xBu);

    // Same key as source 0/1, but A/B/A must never be regrouped.
    AddPolygon(upload, 4, VulkanRasterPrimitive::Triangles, 0,
        opaqueAttr, 0, 0, 0xFFFFFFFFu, 0);
    // Same key again, but source 5 was degenerate and the source-order gap must
    // terminate the previous range.
    AddPolygon(upload, 6, VulkanRasterPrimitive::Triangles, 0,
        opaqueAttr, 0, 0, 0xFFFFFFFFu, 0);

    // Pipeline key boundary: depth-equal plus highlight mode.
    AddPolygon(upload, 7, VulkanRasterPrimitive::Triangles,
        VulkanRasterPolygonFlag_Textured,
        (31u << 16) | (1u << 14) | (2u << 4),
        textureParam, 0x45u, 7u, 0xBu);

    AddPolygon(upload, 8, VulkanRasterPrimitive::Lines, 0,
        opaqueAttr, 0, 0, 0xFFFFFFFFu, 0);
    AddPolygon(upload, 9, VulkanRasterPrimitive::Lines, 0,
        opaqueAttr, 0, 0, 0xFFFFFFFFu, 0);

    AddPolygon(upload, 10, VulkanRasterPrimitive::Triangles,
        VulkanRasterPolygonFlag_Translucent,
        (12u << 16) | (1u << 11) | (1u << 15) | (11u << 24),
        0, 0, 0xFFFFFFFFu, 0);
    AddPolygon(upload, 11, VulkanRasterPrimitive::Triangles,
        VulkanRasterPolygonFlag_ShadowMask,
        (31u << 16) | (12u << 24), 0, 0, 0xFFFFFFFFu, 0);
    AddPolygon(upload, 12, VulkanRasterPrimitive::Triangles,
        VulkanRasterPolygonFlag_Shadow | VulkanRasterPolygonFlag_Translucent,
        (31u << 16) | (1u << 11) | (13u << 24),
        0, 0, 0xFFFFFFFFu, 0);

    upload.Valid = true;
    return upload;
}

bool SamePipeline(const VulkanRasterBatch& left, const VulkanRasterBatch& right)
{
    return std::memcmp(
        &left.PipelineKey, &right.PipelineKey, sizeof(VulkanRasterPipelineKey)) == 0;
}

ContractResult RunContractChecks()
{
    ContractResult result;
    result.LayoutPassed =
        sizeof(VulkanRasterPipelineKey) == 64 &&
        sizeof(VulkanRasterTextureKey) == 32 &&
        sizeof(VulkanRasterBatch) == 128 &&
        alignof(VulkanRasterBatch) == 16 &&
        offsetof(VulkanRasterBatch, PipelineKey) == 32 &&
        offsetof(VulkanRasterBatch, TextureKey) == 96;

    result.Upload = BuildSyntheticUpload();
    VulkanRasterBatchOptions options;
    options.RenderDispCnt = 1u << 1; // highlight mode
    options.ColorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    options.AttributeFormat = VK_FORMAT_R8G8B8A8_UNORM;
    options.DepthStencilFormat = VK_FORMAT_D24_UNORM_S8_UINT;

    if (!melonDS::Vulkan::BuildVulkanRasterBatchPlan(
            result.Upload, options, result.Plan, &result.FailureReason))
    {
        return result;
    }

    result.SourceOrderPassed =
        result.Plan.SourceOrderPreserved && result.Plan.Valid &&
        result.Plan.SourcePolygonCount == 13 &&
        result.Plan.Batches.size() == 9 &&
        result.Plan.Batches[0].FirstSourceOrder == 0 &&
        result.Plan.Batches[0].LastSourceOrder == 1 &&
        result.Plan.Batches[1].FirstSourceOrder == 2 &&
        result.Plan.Batches[1].LastSourceOrder == 3 &&
        result.Plan.Batches[2].FirstSourceOrder == 4 &&
        result.Plan.Batches[3].FirstSourceOrder == 6 &&
        result.Plan.Batches[8].LastSourceOrder == 12;

    result.AdjacentOnlyPassed =
        result.Plan.AdjacentOnly &&
        result.Plan.Batches[0].PolygonCount == 2 &&
        result.Plan.Batches[0].IndexOffset == 0 &&
        result.Plan.Batches[0].IndexCount == 6 &&
        result.Plan.Batches[1].PolygonCount == 2 &&
        result.Plan.Batches[3].PolygonCount == 1 &&
        result.Plan.Batches[5].PolygonCount == 2 &&
        result.Plan.Batches[5].PipelineKey.Primitive ==
            static_cast<std::uint32_t>(VulkanRasterPrimitive::Lines);

    result.FrameWideRegroupRejected =
        SamePipeline(result.Plan.Batches[0], result.Plan.Batches[2]) &&
        result.Plan.Batches[0].FirstPolygon == 0 &&
        result.Plan.Batches[2].FirstPolygon == 4 &&
        result.Plan.Batches[0].PolygonCount == 2 &&
        result.Plan.Batches[2].PolygonCount == 1;

    const VulkanRasterTextureKey& texture = result.Plan.Batches[1].TextureKey;
    result.TextureSamplerPassed =
        texture.Enabled == 1 &&
        texture.TextureLayer == 7 &&
        texture.TexPalette == 0x45u &&
        texture.TextureRepeat == 0xBu &&
        texture.SamplerS == static_cast<std::uint32_t>(
            VulkanRasterSamplerAxisMode::Repeat) &&
        texture.SamplerT == static_cast<std::uint32_t>(
            VulkanRasterSamplerAxisMode::Mirror) &&
        texture.TextureFormat == 4;

    result.PipelineKeyPassed =
        result.Plan.Batches[1].PipelineKey.WBuffer == 1 &&
        result.Plan.Batches[4].PipelineKey.DepthEqual == 1 &&
        result.Plan.Batches[4].PipelineKey.ToonMode ==
            static_cast<std::uint32_t>(VulkanRasterToonMode::Highlight) &&
        result.Plan.Batches[6].PipelineKey.RenderMode ==
            static_cast<std::uint32_t>(VulkanRasterRenderMode::Translucent) &&
        result.Plan.Batches[6].PipelineKey.DepthWrite == 1 &&
        result.Plan.Batches[6].PipelineKey.FogAttrWrite == 1 &&
        result.Plan.Batches[7].PipelineKey.RenderMode ==
            static_cast<std::uint32_t>(VulkanRasterRenderMode::ShadowMask) &&
        result.Plan.Batches[7].PipelineKey.ShadowStage == 1 &&
        result.Plan.Batches[7].PipelineKey.DepthWrite == 0 &&
        result.Plan.Batches[8].PipelineKey.RenderMode ==
            static_cast<std::uint32_t>(VulkanRasterRenderMode::Shadow) &&
        result.Plan.Batches[8].PipelineKey.ShadowStage == 2 &&
        result.Plan.Batches[8].PipelineKey.NeedsOpaquePass == 1;

    VulkanRasterUpload badOrder = result.Upload;
    badOrder.Polygons[1].SourceOrder = badOrder.Polygons[0].SourceOrder;
    VulkanRasterBatchPlan rejectedOrder;
    std::string rejectedOrderReason;
    result.InvalidOrderRejected = !melonDS::Vulkan::BuildVulkanRasterBatchPlan(
        badOrder, options, rejectedOrder, &rejectedOrderReason);

    VulkanRasterUpload badRange = result.Upload;
    badRange.Polygons.back().IndexOffset =
        static_cast<std::uint32_t>(badRange.Indices.size() + 1u);
    VulkanRasterBatchPlan rejectedRange;
    std::string rejectedRangeReason;
    result.InvalidRangeRejected = !melonDS::Vulkan::BuildVulkanRasterBatchPlan(
        badRange, options, rejectedRange, &rejectedRangeReason);

    result.Passed = result.LayoutPassed && result.SourceOrderPassed &&
        result.AdjacentOnlyPassed && result.FrameWideRegroupRejected &&
        result.TextureSamplerPassed && result.PipelineKeyPassed &&
        result.InvalidOrderRejected && result.InvalidRangeRejected;
    return result;
}

std::vector<std::uint8_t> BuildPayload(const VulkanRasterBatchPlan& plan)
{
    std::vector<std::uint8_t> payload(plan.Batches.size() * sizeof(VulkanRasterBatch));
    if (!payload.empty())
        std::memcpy(payload.data(), plan.Batches.data(), payload.size());
    return payload;
}

class PolygonBatchProbe
{
public:
    PolygonBatchProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        if (Context)
        {
            Device = Context->device();
            Functions = Context->functions();
        }
    }

    ~PolygonBatchProbe()
    {
        Destroy();
    }

    ProbeResult Run(const std::vector<std::uint8_t>& payload)
    {
        ProbeResult result;
        result.PayloadBytes = payload.size();
        if (payload.empty())
        {
            result.FailureStage = "empty polygon batch payload";
            return result;
        }
        if (!CreateResources(payload))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.DeviceLocalBuffer = DeviceLocal.MemoryType !=
            std::numeric_limits<std::uint32_t>::max();
        if (!RecordAndSubmit(payload.size()))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.UploadSubmitted = true;
        if (!Readback(payload, result.PayloadMatched))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.ReadbackCompleted = true;
        result.Passed = result.UploadSubmitted && result.ReadbackCompleted &&
            result.PayloadMatched && result.DeviceLocalBuffer;
        if (!result.Passed)
            result.FailureStage = "device-local polygon batch payload comparison";
        return result;
    }

private:
    bool Fail(const char* stage, VkResult value)
    {
        FailureStage = stage;
        FailureResult = value;
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

    bool CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred,
        BufferResource& resource)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult result = Functions->vkCreateBuffer(Device, &info, nullptr, &resource.Buffer);
        if (result != VK_SUCCESS)
            return Fail("vkCreateBuffer", result);

        VkMemoryRequirements requirements{};
        Functions->vkGetBufferMemoryRequirements(Device, resource.Buffer, &requirements);
        resource.MemoryType = FindMemoryType(requirements.memoryTypeBits, required, preferred);
        if (resource.MemoryType == std::numeric_limits<std::uint32_t>::max())
            return Fail("Vulkan polygon batch memory type", VK_ERROR_FEATURE_NOT_PRESENT);

        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = resource.MemoryType;
        result = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (result != VK_SUCCESS)
            return Fail("vkAllocateMemory", result);
        result = Functions->vkBindBufferMemory(Device, resource.Buffer, resource.Memory, 0);
        if (result != VK_SUCCESS)
            return Fail("vkBindBufferMemory", result);
        return true;
    }

    bool CreateResources(const std::vector<std::uint8_t>& payload)
    {
        const VkDeviceSize size = static_cast<VkDeviceSize>(payload.size());
        if (!CreateBuffer(
                size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                0,
                Staging) ||
            !CreateBuffer(
                size,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                DeviceLocal) ||
            !CreateBuffer(
                size,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                0,
                ReadbackBuffer))
        {
            return false;
        }

        void* mapped = nullptr;
        VkResult result = Functions->vkMapMemory(Device, Staging.Memory, 0, size, 0, &mapped);
        if (result != VK_SUCCESS)
            return Fail("vkMapMemory(staging)", result);
        std::memcpy(mapped, payload.data(), payload.size());
        Functions->vkUnmapMemory(Device, Staging.Memory);

        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = Context->featureInfo().graphicsQueueFamily;
        result = Functions->vkCreateCommandPool(Device, &poolInfo, nullptr, &CommandPool);
        if (result != VK_SUCCESS)
            return Fail("vkCreateCommandPool", result);

        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = CommandPool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        result = Functions->vkAllocateCommandBuffers(Device, &allocate, &CommandBuffer);
        if (result != VK_SUCCESS)
            return Fail("vkAllocateCommandBuffers", result);

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        result = Functions->vkCreateFence(Device, &fenceInfo, nullptr, &Fence);
        if (result != VK_SUCCESS)
            return Fail("vkCreateFence", result);
        return true;
    }

    bool RecordAndSubmit(std::size_t payloadBytes)
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult result = Functions->vkBeginCommandBuffer(CommandBuffer, &begin);
        if (result != VK_SUCCESS)
            return Fail("vkBeginCommandBuffer", result);

        VkBufferCopy copy{};
        copy.size = static_cast<VkDeviceSize>(payloadBytes);
        Functions->vkCmdCopyBuffer(CommandBuffer, Staging.Buffer, DeviceLocal.Buffer, 1, &copy);

        VkBufferMemoryBarrier deviceBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        deviceBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        deviceBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        deviceBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        deviceBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        deviceBarrier.buffer = DeviceLocal.Buffer;
        deviceBarrier.offset = 0;
        deviceBarrier.size = VK_WHOLE_SIZE;
        Functions->vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 1, &deviceBarrier, 0, nullptr);

        Functions->vkCmdCopyBuffer(
            CommandBuffer, DeviceLocal.Buffer, ReadbackBuffer.Buffer, 1, &copy);

        VkBufferMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.buffer = ReadbackBuffer.Buffer;
        hostBarrier.offset = 0;
        hostBarrier.size = VK_WHOLE_SIZE;
        Functions->vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &hostBarrier, 0, nullptr);

        result = Functions->vkEndCommandBuffer(CommandBuffer);
        if (result != VK_SUCCESS)
            return Fail("vkEndCommandBuffer", result);

        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &CommandBuffer;
        result = Functions->vkQueueSubmit(Context->graphicsQueue(), 1, &submit, Fence);
        if (result != VK_SUCCESS)
            return Fail("vkQueueSubmit", result);
        result = Functions->vkWaitForFences(Device, 1, &Fence, VK_TRUE, 5'000'000'000ull);
        if (result != VK_SUCCESS)
            return Fail("vkWaitForFences", result);
        return true;
    }

    bool Readback(const std::vector<std::uint8_t>& expected, bool& matched)
    {
        void* mapped = nullptr;
        const VkResult result = Functions->vkMapMemory(
            Device, ReadbackBuffer.Memory, 0, expected.size(), 0, &mapped);
        if (result != VK_SUCCESS)
            return Fail("vkMapMemory(readback)", result);
        matched = std::memcmp(mapped, expected.data(), expected.size()) == 0;
        Functions->vkUnmapMemory(Device, ReadbackBuffer.Memory);
        return true;
    }

    void DestroyBuffer(BufferResource& resource)
    {
        if (resource.Buffer)
            Functions->vkDestroyBuffer(Device, resource.Buffer, nullptr);
        if (resource.Memory)
            Functions->vkFreeMemory(Device, resource.Memory, nullptr);
        resource = {};
    }

    void Destroy()
    {
        if (!Functions || !Device)
            return;
        if (Fence)
            Functions->vkDestroyFence(Device, Fence, nullptr);
        if (CommandPool)
            Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
        DestroyBuffer(ReadbackBuffer);
        DestroyBuffer(DeviceLocal);
        DestroyBuffer(Staging);
    }

    std::shared_ptr<DeviceContext> Context;
    QWindow* Window = nullptr;
    QVulkanDeviceFunctions* Functions = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    BufferResource Staging;
    BufferResource DeviceLocal;
    BufferResource ReadbackBuffer;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

} // namespace

int RunPolygonBatchBootstrapHarness(const QString& outputPath, int iterations)
{
    if (iterations <= 0)
        iterations = 1;

    const ContractResult contract = RunContractChecks();
    const std::vector<std::uint8_t> payload = BuildPayload(contract.Plan);
    FeatureInfo lastInfo;
    ProbeResult lastProbe;
    int completed = 0;

    auto& host = static_cast<MelonApplication*>(qApp)->vulkanInstanceHost();
    if (!contract.Passed)
    {
        lastProbe.FailureStage = contract.FailureReason.empty()
            ? "Vulkan adjacent polygon batch contract"
            : contract.FailureReason;
    }
    else if (!host.ensureCreated())
    {
        lastProbe.FailureStage = host.unavailableReason();
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
                lastProbe.FailureStage = lastInfo.unavailableReason;
                window.destroy();
                break;
            }

            {
                PolygonBatchProbe probe(context, &window);
                lastProbe = probe.Run(payload);
            }
            context.reset();
            window.destroy();
            if (!lastProbe.Passed)
                break;
            ++completed;
        }
    }

    const bool passed = contract.Passed && completed == iterations;
    if (passed)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Info,
            "[MelonPrime] Vulkan polygon batch bootstrap passed: iterations=%d polygons=%zu batches=%zu bytes=%zu\n",
            completed,
            contract.Upload.Polygons.size(),
            contract.Plan.Batches.size(),
            payload.size());
    }
    else
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan polygon batch bootstrap failed: stage=%s VkResult=%d\n",
            lastProbe.FailureStage.c_str(),
            static_cast<int>(lastProbe.FailureResult));
    }

    const QJsonObject result{
        {"schema_version", 1},
        {"passed", passed},
        {"contract_version", static_cast<int>(
            melonDS::Vulkan::kPolygonBatchContractVersion)},
        {"iterations_requested", iterations},
        {"iterations_completed", completed},
        {"pipeline_key_size", static_cast<int>(sizeof(VulkanRasterPipelineKey))},
        {"texture_key_size", static_cast<int>(sizeof(VulkanRasterTextureKey))},
        {"batch_record_size", static_cast<int>(sizeof(VulkanRasterBatch))},
        {"source_polygon_count", static_cast<int>(contract.Upload.SourcePolygonCount)},
        {"emitted_polygon_count", static_cast<int>(contract.Upload.Polygons.size())},
        {"batch_count", static_cast<int>(contract.Plan.Batches.size())},
        {"payload_bytes", static_cast<qint64>(payload.size())},
        {"layout_validated", contract.LayoutPassed},
        {"source_order_preserved", contract.SourceOrderPassed},
        {"adjacent_only_batching_validated", contract.AdjacentOnlyPassed},
        {"frame_wide_regrouping_rejected", contract.FrameWideRegroupRejected},
        {"texture_sampler_key_validated", contract.TextureSamplerPassed},
        {"pipeline_key_validated", contract.PipelineKeyPassed},
        {"invalid_source_order_rejected", contract.InvalidOrderRejected},
        {"invalid_buffer_range_rejected", contract.InvalidRangeRejected},
        {"upload_submitted", lastProbe.UploadSubmitted},
        {"readback_completed", lastProbe.ReadbackCompleted},
        {"device_local_buffer_used", lastProbe.DeviceLocalBuffer},
        {"payload_matched", lastProbe.PayloadMatched},
        {"software_game_rendering_preserved", true},
        {"native_ds_polygon_raster_integrated", false},
        {"failure_stage", QString::fromStdString(lastProbe.FailureStage)},
        {"vk_result", static_cast<int>(lastProbe.FailureResult)},
        {"device_name", QString::fromStdString(lastInfo.deviceName)},
    };

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 2;
    file.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
    return passed ? 0 : 1;
}

} // namespace MelonPrime::Vulkan
