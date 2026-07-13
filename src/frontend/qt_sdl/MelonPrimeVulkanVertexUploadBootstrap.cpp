#include "MelonPrimeVulkanVertexUploadBootstrap.h"

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

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "GPU3D.h"
#include "GPU3D_Vulkan.h"
#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanInstanceHost.h"
#include "Platform.h"
#include "main.h"

namespace MelonPrime::Vulkan
{
namespace
{

using melonDS::Polygon;
using melonDS::Vertex;
using melonDS::Vulkan::VulkanPackedPolygon;
using melonDS::Vulkan::VulkanPackedVertex;
using melonDS::Vulkan::VulkanRasterBuildOptions;
using melonDS::Vulkan::VulkanRasterUpload;

constexpr std::array<std::uint16_t, 11> kExpectedIndices{{
    0, 1, 2,
    3, 4, 5, 3, 5, 6,
    7, 8,
}};

constexpr std::array<std::uint16_t, 16> kExpectedEdgeIndices{{
    0, 1, 1, 2, 2, 0,
    3, 4, 4, 5, 5, 6, 6, 3,
    7, 8,
}};

struct BufferResource
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    std::uint32_t MemoryType = std::numeric_limits<std::uint32_t>::max();
};

struct SyntheticScene
{
    std::array<Vertex, 3> TriangleVertices{};
    std::array<Vertex, 3> DegenerateVertices{};
    std::array<Vertex, 4> QuadVertices{};
    std::array<Vertex, 3> LineVertices{};
    std::array<Polygon, 4> Polygons{};
    std::array<Polygon*, 4> PolygonPointers{};
    std::array<std::uint32_t, 4> TextureLayers{{7u, 0u, 11u, 0xFFFFFFFFu}};
};

struct ContractResult
{
    bool Passed = false;
    bool LayoutPassed = false;
    bool SourceOrderPassed = false;
    bool TriangleFanPassed = false;
    bool LineDuplicatePassed = false;
    bool ScalePathPassed = false;
    bool MetadataPassed = false;
    VulkanRasterUpload Upload;
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

void FillVertex(
    Vertex& vertex,
    std::int32_t finalX,
    std::int32_t finalY,
    std::int32_t hiresX,
    std::int32_t hiresY,
    std::int32_t red,
    std::int32_t green,
    std::int32_t blue,
    std::int16_t s,
    std::int16_t t)
{
    vertex.FinalPosition[0] = finalX;
    vertex.FinalPosition[1] = finalY;
    vertex.HiresPosition[0] = hiresX;
    vertex.HiresPosition[1] = hiresY;
    vertex.FinalColor[0] = red;
    vertex.FinalColor[1] = green;
    vertex.FinalColor[2] = blue;
    vertex.TexCoords[0] = s;
    vertex.TexCoords[1] = t;
}

void BindPolygonVertices(Polygon& polygon, Vertex* vertices, std::size_t count)
{
    polygon.NumVertices = static_cast<std::uint32_t>(count);
    for (std::size_t index = 0; index < count; ++index)
        polygon.Vertices[index] = &vertices[index];
}

void BuildSyntheticScene(SyntheticScene& scene)
{

    FillVertex(scene.TriangleVertices[0], 10, 20, 160, 320, 62, 32, 2, -16, 24);
    FillVertex(scene.TriangleVertices[1], 30, 40, 480, 640, 20, 40, 60, 32, -48);
    FillVertex(scene.TriangleVertices[2], 50, 60, 800, 960, 12, 22, 42, 64, 80);
    Polygon& triangle = scene.Polygons[0];
    BindPolygonVertices(triangle, scene.TriangleVertices.data(), scene.TriangleVertices.size());
    triangle.FinalZ[0] = 0x1FFFF;
    triangle.FinalZ[1] = 0x1234;
    triangle.FinalZ[2] = 0xABCD;
    triangle.FinalW[0] = 0x2345;
    triangle.FinalW[1] = 0x3456;
    triangle.FinalW[2] = 0x4567;
    triangle.WBuffer = true;
    triangle.FacingView = true;
    triangle.Attr = (17u << 16) | (5u << 24) | (1u << 14) | (1u << 11) | 0x50u;
    triangle.TexParam = (4u << 26) | (2u << 20) | (3u << 23) | (0xBu << 16) | 0x1234u;
    triangle.TexPalette = 0x456u;
    triangle.Type = 0;

    FillVertex(scene.DegenerateVertices[0], 1, 1, 16, 16, 2, 2, 2, 0, 0);
    FillVertex(scene.DegenerateVertices[1], 2, 2, 32, 32, 4, 4, 4, 0, 0);
    FillVertex(scene.DegenerateVertices[2], 3, 3, 48, 48, 6, 6, 6, 0, 0);
    Polygon& degenerate = scene.Polygons[1];
    BindPolygonVertices(degenerate, scene.DegenerateVertices.data(), scene.DegenerateVertices.size());
    degenerate.Degenerate = true;

    FillVertex(scene.QuadVertices[0], 5, 7, 80, 112, 10, 12, 14, 1, 2);
    FillVertex(scene.QuadVertices[1], 15, 7, 240, 112, 16, 18, 20, 3, 4);
    FillVertex(scene.QuadVertices[2], 15, 17, 240, 272, 22, 24, 26, 5, 6);
    FillVertex(scene.QuadVertices[3], 5, 17, 80, 272, 28, 30, 32, 7, 8);
    Polygon& quad = scene.Polygons[2];
    BindPolygonVertices(quad, scene.QuadVertices.data(), scene.QuadVertices.size());
    for (std::size_t index = 0; index < scene.QuadVertices.size(); ++index)
    {
        quad.FinalZ[index] = 0x3000 + static_cast<std::int32_t>(index * 0x100);
        quad.FinalW[index] = 0x4000 + static_cast<std::int32_t>(index * 0x100);
    }
    quad.Translucent = true;
    quad.Attr = (12u << 16) | (9u << 24) | (1u << 15);
    quad.TexParam = (2u << 26) | (1u << 20) | (1u << 23) | (0x3u << 16) | 0x20u;
    quad.TexPalette = 0x99u;
    quad.Type = 0;

    FillVertex(scene.LineVertices[0], 70, 80, 1120, 1280, 6, 8, 10, 0, 0);
    FillVertex(scene.LineVertices[1], 70, 80, 1120, 1280, 12, 14, 16, 1, 1);
    FillVertex(scene.LineVertices[2], 90, 100, 1440, 1600, 18, 20, 22, 2, 2);
    Polygon& line = scene.Polygons[3];
    BindPolygonVertices(line, scene.LineVertices.data(), scene.LineVertices.size());
    line.FinalZ[0] = 0x1000;
    line.FinalZ[1] = 0x1100;
    line.FinalZ[2] = 0x1200;
    line.FinalW[0] = 0x2000;
    line.FinalW[1] = 0x2100;
    line.FinalW[2] = 0x2200;
    line.Attr = (31u << 16) | (3u << 24);
    line.Type = 1;

    for (std::size_t index = 0; index < scene.Polygons.size(); ++index)
        scene.PolygonPointers[index] = &scene.Polygons[index];
}

template <typename ExpectedContainer>
bool EqualVector(const std::vector<std::uint16_t>& actual, const ExpectedContainer& expected)
{
    return actual.size() == expected.size() &&
        std::memcmp(actual.data(), expected.data(), expected.size() * sizeof(expected[0])) == 0;
}

ContractResult RunContractChecks()
{
    ContractResult result;
    result.LayoutPassed =
        sizeof(VulkanPackedVertex) == 28 &&
        sizeof(VulkanPackedPolygon) == 64 &&
        alignof(VulkanPackedPolygon) == 16 &&
        offsetof(VulkanPackedVertex, TextureSize) == 24 &&
        offsetof(VulkanPackedPolygon, Flags) == 52;

    SyntheticScene scene;
    BuildSyntheticScene(scene);
    VulkanRasterBuildOptions options;
    options.ScaleFactor = 2;
    options.TextureLayers = scene.TextureLayers.data();
    options.TextureLayerCount = scene.TextureLayers.size();
    if (!melonDS::Vulkan::BuildVulkanRasterUpload(
            scene.PolygonPointers.data(), scene.PolygonPointers.size(),
            options, result.Upload, &result.FailureReason))
    {
        return result;
    }

    result.SourceOrderPassed =
        result.Upload.SourcePolygonCount == 4 &&
        result.Upload.SkippedDegenerateCount == 1 &&
        result.Upload.Polygons.size() == 3 &&
        result.Upload.Polygons[0].SourceOrder == 0 &&
        result.Upload.Polygons[1].SourceOrder == 2 &&
        result.Upload.Polygons[2].SourceOrder == 3;

    result.TriangleFanPassed =
        result.Upload.Vertices.size() == 9 &&
        result.Upload.Indices.size() == kExpectedIndices.size() &&
        result.Upload.EdgeIndices.size() == kExpectedEdgeIndices.size() &&
        EqualVector(result.Upload.Indices, kExpectedIndices) &&
        EqualVector(result.Upload.EdgeIndices, kExpectedEdgeIndices) &&
        result.Upload.Polygons[0].IndexCount == 3 &&
        result.Upload.Polygons[1].IndexCount == 6;

    const VulkanPackedVertex& triangle0 = result.Upload.Vertices[0];
    const std::uint32_t expectedPosition = 20u | (40u << 16);
    const std::uint32_t expectedDepth = 0xFFFFu | (0x2345u << 16);
    const std::uint32_t expectedColor = 31u | (16u << 8) | (1u << 16) | (17u << 24);
    result.MetadataPassed =
        triangle0.PositionXY == expectedPosition &&
        triangle0.DepthZW == expectedDepth &&
        triangle0.ColorRgba == expectedColor &&
        ((triangle0.PolygonFlags >> 16) & 0xFFu) == 1u &&
        triangle0.TextureLayer == 7u &&
        triangle0.TextureSize == (32u | (64u << 16)) &&
        result.Upload.FullPrecisionDepthW.size() == result.Upload.Vertices.size() &&
        result.Upload.FullPrecisionDepthW[0].Depth == 0x1FFFFu &&
        result.Upload.FullPrecisionDepthW[0].W == 0x2345u &&
        result.Upload.Polygons[0].Attr == scene.Polygons[0].Attr &&
        result.Upload.Polygons[0].TexParam == scene.Polygons[0].TexParam &&
        result.Upload.Polygons[0].TexPalette == scene.Polygons[0].TexPalette &&
        result.Upload.Polygons[0].TextureRepeat == 0xBu &&
        (result.Upload.Polygons[0].Flags & melonDS::Vulkan::VulkanRasterPolygonFlag_WBuffer) != 0 &&
        (result.Upload.Polygons[0].Flags & melonDS::Vulkan::VulkanRasterPolygonFlag_FacingView) != 0 &&
        (result.Upload.Polygons[1].Flags & melonDS::Vulkan::VulkanRasterPolygonFlag_Translucent) != 0 &&
        (result.Upload.Polygons[2].Flags & melonDS::Vulkan::VulkanRasterPolygonFlag_Line) != 0;

    result.LineDuplicatePassed =
        result.Upload.Polygons[2].VertexCount == 2 &&
        result.Upload.Polygons[2].IndexCount == 2 &&
        result.Upload.Vertices[7].PositionXY == (140u | (160u << 16)) &&
        result.Upload.Vertices[8].PositionXY == (180u | (200u << 16));

    VulkanRasterUpload scaleOne;
    VulkanRasterBuildOptions scaleOneOptions;
    scaleOneOptions.ScaleFactor = 1;
    const Polygon* triangleOnly[] = {scene.PolygonPointers[0]};
    std::string scaleFailure;
    result.ScalePathPassed =
        melonDS::Vulkan::BuildVulkanRasterUpload(
            triangleOnly, 1, scaleOneOptions, scaleOne, &scaleFailure) &&
        scaleOne.Vertices.size() == 3 &&
        scaleOne.FullPrecisionDepthW.size() == scaleOne.Vertices.size() &&
        scaleOne.Vertices[0].PositionXY == (10u | (20u << 16));

    result.Passed = result.LayoutPassed && result.SourceOrderPassed &&
        result.TriangleFanPassed && result.LineDuplicatePassed &&
        result.ScalePathPassed && result.MetadataPassed && result.Upload.Valid;
    return result;
}

std::size_t AlignUp(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

template <typename T>
void AppendSegment(std::vector<std::uint8_t>& payload, const std::vector<T>& values)
{
    const std::size_t offset = AlignUp(payload.size(), 16);
    payload.resize(offset, 0);
    const std::size_t bytes = values.size() * sizeof(T);
    const std::size_t oldSize = payload.size();
    payload.resize(oldSize + bytes);
    if (bytes)
        std::memcpy(payload.data() + oldSize, values.data(), bytes);
}

std::vector<std::uint8_t> BuildPayload(const VulkanRasterUpload& upload)
{
    std::vector<std::uint8_t> payload;
    AppendSegment(payload, upload.Vertices);
    AppendSegment(payload, upload.Indices);
    AppendSegment(payload, upload.EdgeIndices);
    AppendSegment(payload, upload.Polygons);
    return payload;
}

class VertexUploadProbe
{
public:
    VertexUploadProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        if (Context)
        {
            Device = Context->device();
            Functions = Context->functions();
        }
    }

    ~VertexUploadProbe()
    {
        Destroy();
    }

    ProbeResult Run(const std::vector<std::uint8_t>& payload)
    {
        ProbeResult result;
        result.PayloadBytes = payload.size();
        if (payload.empty())
        {
            result.FailureStage = "empty packed upload payload";
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
            result.FailureStage = "device-local packed payload comparison";
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
            return Fail("Vulkan vertex upload memory type", VK_ERROR_FEATURE_NOT_PRESENT);

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
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
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
        VkResult result = Functions->vkMapMemory(
            Device, Staging.Memory, 0, size, 0, &mapped);
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

        Functions->vkCmdCopyBuffer(CommandBuffer, DeviceLocal.Buffer, ReadbackBuffer.Buffer, 1, &copy);

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

int RunVertexUploadBootstrapHarness(const QString& outputPath, int iterations)
{
    if (iterations <= 0)
        iterations = 1;

    const ContractResult contract = RunContractChecks();
    const std::vector<std::uint8_t> payload = BuildPayload(contract.Upload);
    FeatureInfo lastInfo;
    ProbeResult lastProbe;
    int completed = 0;

    auto& host = static_cast<MelonApplication*>(qApp)->vulkanInstanceHost();
    if (!contract.Passed)
    {
        lastProbe.FailureStage = contract.FailureReason.empty()
            ? "Vulkan packed vertex/index contract"
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
                VertexUploadProbe probe(context, &window);
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
            "[MelonPrime] Vulkan vertex upload bootstrap passed: iterations=%d vertices=%zu indices=%zu edge_indices=%zu polygons=%zu bytes=%zu\n",
            completed,
            contract.Upload.Vertices.size(),
            contract.Upload.Indices.size(),
            contract.Upload.EdgeIndices.size(),
            contract.Upload.Polygons.size(),
            payload.size());
    }
    else
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan vertex upload bootstrap failed: stage=%s VkResult=%d\n",
            lastProbe.FailureStage.c_str(),
            static_cast<int>(lastProbe.FailureResult));
    }

    const QJsonObject result{
        {"schema_version", 1},
        {"passed", passed},
        {"contract_version", static_cast<int>(melonDS::Vulkan::kVertexUploadContractVersion)},
        {"iterations_requested", iterations},
        {"iterations_completed", completed},
        {"packed_vertex_size", static_cast<int>(sizeof(VulkanPackedVertex))},
        {"packed_polygon_size", static_cast<int>(sizeof(VulkanPackedPolygon))},
        {"source_polygon_count", static_cast<int>(contract.Upload.SourcePolygonCount)},
        {"emitted_polygon_count", static_cast<int>(contract.Upload.Polygons.size())},
        {"skipped_degenerate_count", static_cast<int>(contract.Upload.SkippedDegenerateCount)},
        {"vertex_count", static_cast<int>(contract.Upload.Vertices.size())},
        {"index_count", static_cast<int>(contract.Upload.Indices.size())},
        {"edge_index_count", static_cast<int>(contract.Upload.EdgeIndices.size())},
        {"payload_bytes", static_cast<qint64>(payload.size())},
        {"layout_validated", contract.LayoutPassed},
        {"source_order_preserved", contract.SourceOrderPassed},
        {"triangle_fan_validated", contract.TriangleFanPassed},
        {"line_duplicate_filter_validated", contract.LineDuplicatePassed},
        {"scale_1_and_hires_scale_2_validated", contract.ScalePathPassed},
        {"polygon_metadata_validated", contract.MetadataPassed},
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
