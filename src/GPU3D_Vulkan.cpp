/*
    Copyright 2016-2025 melonDS team

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

#include "GPU3D_Vulkan.h"

#include <volk.h>
#include "VulkanDesktopCompat.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "GPU.h"
#include "GPU3D_AcceleratedFrontend.h"
#include <sapphire/GPU3D_Vulkan_BinCombinedShaderData.h>
#include <sapphire/GPU3D_Vulkan_CalculateWorkOffsetsShaderData.h>
#include <sapphire/GPU3D_Vulkan_CaptureLineExportShaderData.h>
#include <sapphire/GPU3D_Vulkan_DepthBlendShaderData.h>
#include <sapphire/GPU3D_Vulkan_FinalPassShaderData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsEdgeFogShaderData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsEdgeShaderData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsFinalShaderVertexData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsFogShaderData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsClearShaderData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsNoColorShaderData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulateOpaqueAlphaPlainShaderFragmentData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulateOpaqueAlphaToonShaderFragmentData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulatePlainShaderFragmentData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulateShaderFragmentData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulateToonShaderFragmentData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectShaderFragmentData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsRasterNoFragDepthShaderFragmentData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsRasterShaderFragmentData.h>
#include <sapphire/GPU3D_Vulkan_GraphicsRasterShaderVertexData.h>
#include <sapphire/GPU3D_Vulkan_InterpSpansShaderData.h>
#include <sapphire/GPU3D_Vulkan_SortWorkShaderData.h>
#include <sapphire/GPU3D_Vulkan_TriRasterBaseShaderData.h>
#include <sapphire/GPU3D_Vulkan_TriRasterCompatShaderData.h>
#include <sapphire/GPU3D_Vulkan_TriRasterShaderData.h>
#include "Platform.h"
#include "VulkanContext.h"
#include "version.h"

namespace MelonDSAndroid
{
bool isFastForwardActive();
bool areRendererDebugToolsEnabled();
bool isRenderer3DDebugFeatureEnabled(melonDS::u32 featureFlag);
bool areRenderer3DDebugControlsActive();
melonDS::u32 getVulkanDiagnosticFlags();
}

namespace melonDS
{
using namespace Vulkan::GeneratedShaders;
using Platform::Log;
using Platform::LogLevel;

namespace
{
constexpr u32 kPipelineCacheFileVersion = 4;
constexpr u32 kVulkanDiagnosticDisablePassiveRepeatCoverageExpand = 1u << 0u;
constexpr float kTriangleAreaEpsilon = 0.000001f;
constexpr float kTileOverlapEpsilon = 0.00001f;
constexpr u32 kWorkSortDispatchX = 0u;
constexpr u32 kWorkSortDispatchY = 1u;
constexpr u32 kWorkSortDispatchZ = 2u;
constexpr u32 kWorkRasterDispatchX = 3u;
constexpr u32 kWorkRasterDispatchY = 4u;
constexpr u32 kWorkRasterDispatchZ = 5u;
constexpr u32 kWorkActiveTileCount = 6u;
constexpr u32 kWorkActiveGroupCount = 7u;
constexpr u32 kWorkTileOffsetsBase = 8u;
constexpr u32 kCpuActiveTileDispatchMaxCoveragePercent = 90u;
constexpr u32 kRenderer3DDebugFeatureRendererOutput = 1u << 0u;
constexpr u32 kRenderer3DDebugFeatureTrianglePolygons = 1u << 1u;
constexpr u32 kRenderer3DDebugFeatureLinePolygons = 1u << 2u;
constexpr u32 kRenderer3DDebugFeatureOpaquePolygons = 1u << 3u;
constexpr u32 kRenderer3DDebugFeatureTranslucentPolygons = 1u << 4u;
constexpr u32 kRenderer3DDebugFeatureShadowMaskPolygons = 1u << 5u;
constexpr u32 kRenderer3DDebugFeatureShadowPolygons = 1u << 6u;
constexpr u32 kRenderer3DDebugFeatureTexturedPolygons = 1u << 7u;
constexpr u32 kRenderer3DDebugFeatureUntexturedPolygons = 1u << 8u;
constexpr u32 kRenderer3DDebugFeatureModulatePolygons = 1u << 9u;
constexpr u32 kRenderer3DDebugFeatureDecalPolygons = 1u << 10u;
constexpr u32 kRenderer3DDebugFeatureToonHighlightPolygons = 1u << 11u;
constexpr u32 kRenderer3DDebugFeatureWBufferPolygons = 1u << 12u;
constexpr u32 kRenderer3DDebugFeatureZBufferPolygons = 1u << 13u;
constexpr u32 kRenderer3DDebugFeatureDepthWritePolygons = 1u << 14u;
constexpr u32 kRenderer3DDebugFeatureFogWritePolygons = 1u << 15u;
constexpr u32 kRenderer3DDebugFeatureUpperBand = 1u << 16u;
constexpr u32 kRenderer3DDebugFeatureMiddleBand = 1u << 17u;
constexpr u32 kRenderer3DDebugFeatureLowerBand = 1u << 18u;

bool Renderer3DDebugShouldDrawPolygon(
    const AcceleratedPolygonMeta& polygonMeta,
    bool isLine,
    bool polygonTextured,
    bool highlightEnabled)
{
    if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureRendererOutput))
        return false;

    if (isLine)
    {
        if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureLinePolygons))
            return false;
    }
    else if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureTrianglePolygons))
    {
        return false;
    }

    if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadowMask))
    {
        if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureShadowMaskPolygons))
            return false;
    }
    else if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadow))
    {
        if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureShadowPolygons))
            return false;
    }
    else
    {
        const u32 alpha5 = polygonMeta.Alpha5;
        const bool translucent = HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagTranslucent)
            || (alpha5 != 0u && alpha5 < 0x1Fu);
        if (translucent)
        {
            if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureTranslucentPolygons))
                return false;
        }
        else if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureOpaquePolygons))
        {
            return false;
        }
    }

    if (polygonTextured)
    {
        if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureTexturedPolygons))
            return false;
    }
    else if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureUntexturedPolygons))
    {
        return false;
    }

    const u32 blendMode = (polygonMeta.PolyAttr >> 4u) & 0x3u;
    if (blendMode == 2u)
    {
        (void)highlightEnabled;
        if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureToonHighlightPolygons))
            return false;
    }
    else if (polygonTextured && (blendMode & 0x1u) != 0u)
    {
        if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureDecalPolygons))
            return false;
    }
    else if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureModulatePolygons))
    {
        return false;
    }

    if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagWBuffer))
    {
        if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureWBufferPolygons))
            return false;
    }
    else if (!MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureZBufferPolygons))
    {
        return false;
    }

    if ((polygonMeta.PolyAttr & (1u << 11u)) != 0u
        && !MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureDepthWritePolygons))
    {
        return false;
    }

    if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagFogWrite)
        && !MelonDSAndroid::isRenderer3DDebugFeatureEnabled(kRenderer3DDebugFeatureFogWritePolygons))
    {
        return false;
    }

    return true;
}

bool Renderer3DDebugYBoundsEnabled(u32 packedYBounds, u32 targetHeight)
{
    if (!MelonDSAndroid::areRenderer3DDebugControlsActive())
        return true;

    u32 yTop = packedYBounds & 0xFFFFu;
    u32 yBottom = (packedYBounds >> 16u) & 0xFFFFu;
    yTop = std::min(yTop, targetHeight);
    yBottom = std::min(yBottom, targetHeight);
    if (yBottom <= yTop)
        yBottom = std::min(targetHeight, yTop + 1u);

    const u32 upperEnd = targetHeight / 3u;
    const u32 middleEnd = (targetHeight * 2u) / 3u;
    bool touchesAnyBand = false;
    const auto allowsBand = [&](u32 bandTop, u32 bandBottom, u32 featureFlag) -> bool {
        if (yTop >= bandBottom || yBottom <= bandTop)
            return false;
        touchesAnyBand = true;
        return MelonDSAndroid::isRenderer3DDebugFeatureEnabled(featureFlag);
    };

    if (allowsBand(0u, upperEnd, kRenderer3DDebugFeatureUpperBand))
        return true;
    if (allowsBand(upperEnd, middleEnd, kRenderer3DDebugFeatureMiddleBand))
        return true;
    if (allowsBand(middleEnd, targetHeight, kRenderer3DDebugFeatureLowerBand))
        return true;

    return !touchesAnyBand;
}

void logCaptureDebugState(
    const char* stage,
    bool exactCaptureOnly,
    bool captureLinePending,
    bool captureLineReady,
    bool hasCpuFrame,
    bool exactLineFresh,
    bool usedPreviousValidFill,
    bool usedFallbackFill,
    const std::array<u32, 256 * 192>& lineCache,
    u32& remainingLogs)
{
    if (!MelonDSAndroid::areRendererDebugToolsEnabled() || remainingLogs == 0)
        return;

    const u32 topLeft = lineCache[0];
    const u32 topMid = lineCache[128];
    const u32 center = lineCache[(96u * 256u) + 128u];
    Platform::Log(
        Platform::LogLevel::Warn,
        "VulkanCapture[%s]: exact=%u pending=%u ready=%u hasCpu=%u fresh=%u fallback=%u topLeft=%08X topMid=%08X center=%08X remaining=%u",
        stage,
        exactCaptureOnly ? 1u : 0u,
        captureLinePending ? 1u : 0u,
        captureLineReady ? 1u : 0u,
        hasCpuFrame ? 1u : 0u,
        exactLineFresh ? 1u : 0u,
        usedPreviousValidFill ? 2u : (usedFallbackFill ? 1u : 0u),
        topLeft,
        topMid,
        center,
        remainingLogs
    );
    remainingLogs--;
}

u64 fnv1a64(const char* value)
{
    constexpr u64 kOffsetBasis = 14695981039346656037ull;
    constexpr u64 kPrime = 1099511628211ull;

    u64 hash = kOffsetBasis;
    if (value == nullptr)
        return hash;

    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != 0; cursor++)
    {
        hash ^= static_cast<u64>(*cursor);
        hash *= kPrime;
    }

    return hash;
}

inline float edgeFunction2D(float ax, float ay, float bx, float by, float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

struct EdgeEquation2D
{
    float a;
    float b;
    float c;
};

inline EdgeEquation2D makeEdgeEquation2D(float ax, float ay, float bx, float by)
{
    return {
        by - ay,
        ax - bx,
        (bx * ay) - (ax * by),
    };
}

inline float evaluateEdge2D(const EdgeEquation2D& edge, float px, float py)
{
    return edge.a * px + edge.b * py + edge.c;
}

inline bool edgeSeparatesTile(float e0, float e1, float e2, float e3, bool positiveArea)
{
    if (positiveArea)
        return e0 < -kTileOverlapEpsilon && e1 < -kTileOverlapEpsilon && e2 < -kTileOverlapEpsilon && e3 < -kTileOverlapEpsilon;
    return e0 > kTileOverlapEpsilon && e1 > kTileOverlapEpsilon && e2 > kTileOverlapEpsilon && e3 > kTileOverlapEpsilon;
}

VkBufferCreateInfo makeBufferCreateInfo(VkDeviceSize size, VkBufferUsageFlags usage)
{
    VkBufferCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.size = size;
    createInfo.usage = usage;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    return createInfo;
}

VkMemoryAllocateInfo makeMemoryAllocateInfo(VkDeviceSize allocationSize, u32 memoryTypeIndex)
{
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = allocationSize;
    allocateInfo.memoryTypeIndex = memoryTypeIndex;
    return allocateInfo;
}

VkDeviceSize growBufferCapacity(VkDeviceSize requiredSize, VkDeviceSize currentCapacity)
{
    if (currentCapacity == 0)
        return requiredSize;
    const VkDeviceSize doubled = currentCapacity <= (UINT64_MAX / 2u)
        ? currentCapacity * 2u
        : UINT64_MAX;
    return std::max(requiredSize, doubled);
}

VkWriteDescriptorSet makeImageDescriptorWrite(
    VkDescriptorSet descriptorSet,
    u32 binding,
    const VkDescriptorImageInfo* imageInfo,
    u32 descriptorCount,
    VkDescriptorType descriptorType)
{
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.descriptorCount = descriptorCount;
    write.descriptorType = descriptorType;
    write.pImageInfo = imageInfo;
    return write;
}

VkWriteDescriptorSet makeBufferDescriptorWrite(
    VkDescriptorSet descriptorSet,
    u32 binding,
    const VkDescriptorBufferInfo* bufferInfo,
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
{
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = descriptorType;
    write.pBufferInfo = bufferInfo;
    return write;
}

template <typename FindMemoryTypeFn>
bool createBufferAllocation(
    VkDevice device,
    FindMemoryTypeFn&& findMemoryType,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags preferredProperties,
    VkMemoryPropertyFlags fallbackProperties,
    VkBuffer& buffer,
    VkDeviceMemory& memory,
    void** mappedMemory = nullptr)
{
    VkBufferCreateInfo bufferCreateInfo = makeBufferCreateInfo(size, usage);
    if (vkCreateBuffer(device, &bufferCreateInfo, nullptr, &buffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

    u32 memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, preferredProperties);
    if (memoryTypeIndex == UINT32_MAX && fallbackProperties != preferredProperties)
        memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, fallbackProperties);
    if (memoryTypeIndex == UINT32_MAX)
    {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo memoryAllocateInfo = makeMemoryAllocateInfo(memoryRequirements.size, memoryTypeIndex);
    if (vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &memory) != VK_SUCCESS)
    {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS)
    {
        vkFreeMemory(device, memory, nullptr);
        vkDestroyBuffer(device, buffer, nullptr);
        memory = VK_NULL_HANDLE;
        buffer = VK_NULL_HANDLE;
        return false;
    }

    if (mappedMemory != nullptr && vkMapMemory(device, memory, 0, size, 0, mappedMemory) != VK_SUCCESS)
    {
        vkFreeMemory(device, memory, nullptr);
        vkDestroyBuffer(device, buffer, nullptr);
        memory = VK_NULL_HANDLE;
        buffer = VK_NULL_HANDLE;
        *mappedMemory = nullptr;
        return false;
    }

    return true;
}

VkShaderModule createShaderModule(VkDevice device, const u32* spirvWords, size_t spirvLength)
{
    if (device == VK_NULL_HANDLE || spirvWords == nullptr || spirvLength == 0 || (spirvLength & 3u) != 0u)
        return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo shaderCreateInfo{};
    shaderCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderCreateInfo.codeSize = spirvLength;
    shaderCreateInfo.pCode = spirvWords;

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &shaderCreateInfo, nullptr, &shaderModule) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    return shaderModule;
}

u32 ConvertBgraToRgba8(u32 packedColor)
{
    const u32 rb = packedColor & 0x00FF00FFu;
    const u32 g = packedColor & 0x0000FF00u;
    const u32 a = packedColor & 0xFF000000u;
    return ((rb & 0x000000FFu) << 16) | g | ((rb & 0x00FF0000u) >> 16) | a;
}

u32 PackOpenGlAttrToLogical(u32 packedColor)
{
    const u32 polyIdByte = packedColor & 0xFFu;
    const u32 edgeByte = (packedColor >> 8u) & 0xFFu;
    const u32 fogByte = (packedColor >> 16u) & 0xFFu;

    const u32 polyId = ((polyIdByte * 63u) + 127u) / 255u;
    u32 attr = (polyId & 0x3Fu) << 24u;
    if (fogByte >= 0x80u)
        attr |= 1u << 15u;
    if (edgeByte >= 0x80u)
    {
        attr |= 0xFu;
        attr |= 0x10u << 8u;
    }
    return attr;
}

u32 BitCastFloatToU32(float value)
{
    static_assert(sizeof(float) == sizeof(u32));
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
}

class VulkanRenderer3D::IVulkan3DBackend
{
public:
    explicit IVulkan3DBackend(VulkanRenderer3D& renderer) noexcept
        : Renderer(renderer)
    {
    }

    virtual ~IVulkan3DBackend() = default;

    virtual BackendMode mode() const noexcept = 0;
    virtual void Reset(melonDS::GPU& gpu) = 0;
    virtual void VCount144(melonDS::GPU& gpu) = 0;
    virtual void RenderFrame(melonDS::GPU& gpu) = 0;
    virtual void RestartFrame(melonDS::GPU& gpu) = 0;
    virtual u32* GetLine(int line) = 0;
    virtual void SetupAccelFrame() = 0;
    virtual void PrepareCaptureFrame() = 0;
    virtual void Blit(const melonDS::GPU& gpu) = 0;
    virtual void Stop(const melonDS::GPU& gpu) = 0;

protected:
    void activate() noexcept
    {
        Renderer.activateBackendMode(mode());
    }

    VulkanRenderer3D& Renderer;
};

class VulkanRenderer3D::SimpleGraphicsBackend final : public VulkanRenderer3D::IVulkan3DBackend
{
public:
    using IVulkan3DBackend::IVulkan3DBackend;

    BackendMode mode() const noexcept override
    {
        return BackendMode::GraphicsHardware;
    }

    void Reset(melonDS::GPU& gpu) override
    {
        activate();
        Renderer.ResetActiveBackend(gpu);
    }

    void VCount144(melonDS::GPU& gpu) override
    {
        activate();
        Renderer.VCount144ActiveBackend(gpu);
    }

    void RenderFrame(melonDS::GPU& gpu) override
    {
        activate();
        Renderer.RenderFrameActiveBackend(gpu);
    }

    void RestartFrame(melonDS::GPU& gpu) override
    {
        activate();
        Renderer.RestartFrameActiveBackend(gpu);
    }

    u32* GetLine(int line) override
    {
        activate();
        return Renderer.GetLineActiveBackend(line);
    }

    void SetupAccelFrame() override
    {
        activate();
        Renderer.SetupAccelFrameActiveBackend();
    }

    void PrepareCaptureFrame() override
    {
        activate();
        Renderer.PrepareCaptureFrameActiveBackend();
    }

    void Blit(const melonDS::GPU& gpu) override
    {
        activate();
        Renderer.BlitActiveBackend(gpu);
    }

    void Stop(const melonDS::GPU& gpu) override
    {
        activate();
        Renderer.StopActiveBackend(gpu);
    }
};

std::unique_ptr<VulkanRenderer3D> VulkanRenderer3D::New(melonDS::GPU3D& gpu3D) noexcept
{
    return std::make_unique<VulkanRenderer3D>(gpu3D);
}

VulkanRenderer3D::VulkanRenderer3D(melonDS::GPU3D& gpu3D) noexcept
    : Renderer3D(gpu3D)
    , Texcache(gpu3D.GPU, TexcacheVulkanLoader())
    , SimpleGraphicsBackendInstance(std::make_unique<SimpleGraphicsBackend>(*this))
{
    clearLineCache();
}

// MELONPRIME_SAPPHIRE_VULKAN_STRUCTURED_2D_A2
void VulkanRenderer3D::BeginStructured2DFrame(u64 frameSerial)
{
    Structured2DPendingSerial = frameSerial;
    Structured2DFrameValid = false;
    Structured2DLineReceived.fill(0);
}

void VulkanRenderer3D::SubmitStructured2DLine(
    const SapphireStructured2DLine& line)
{
    if (Structured2DPendingSerial == 0
        || line.Engine >= 2
        || line.Line >= 192
        || line.Plane0 == nullptr
        || line.Plane1 == nullptr
        || line.Control == nullptr
        || line.NativeFinal == nullptr)
    {
        return;
    }

    const size_t lineIndex = static_cast<size_t>(line.Engine) * 192u + line.Line;
    const size_t pixelOffset = lineIndex * 256u;
    std::memcpy(Structured2DPlane0.data() + pixelOffset, line.Plane0, 256u * sizeof(u32));
    std::memcpy(Structured2DPlane1.data() + pixelOffset, line.Plane1, 256u * sizeof(u32));
    std::memcpy(Structured2DControl.data() + pixelOffset, line.Control, 256u * sizeof(u32));
    std::memcpy(Structured2DNativeFinal.data() + pixelOffset, line.NativeFinal, 256u * sizeof(u32));
    Structured2DLineMeta[lineIndex] = line.DispCnt;
    Structured2DLineState[lineIndex] =
        static_cast<u32>(line.MasterBrightness)
        | (line.ScreensEnabled ? (1u << 16u) : 0u)
        | (line.EngineEnabled ? (1u << 17u) : 0u)
        | (line.ForcedBlank ? (1u << 18u) : 0u);
    Structured2DLineReceived[lineIndex] = 1;
}

void VulkanRenderer3D::EndStructured2DFrame(u64 frameSerial, bool screenSwap)
{
    bool complete = frameSerial != 0 && frameSerial == Structured2DPendingSerial;
    if (complete)
    {
        for (u8 received : Structured2DLineReceived)
        {
            if (!received)
            {
                complete = false;
                break;
            }
        }
    }

    Structured2DFrameValid = complete;
    if (complete)
    {
        Structured2DFrameSerial = frameSerial;
        Structured2DScreenSwap = screenSwap;
        // The desktop frontend remains the sole consumer of this temporary
        // CPU snapshot until R17 moves ownership to GPU2D SoftRenderer.
    }
    Structured2DPendingSerial = 0;
}

VulkanRenderer3D::~VulkanRenderer3D()
{
    destroyVulkan();
}

VulkanRenderer3D::IVulkan3DBackend& VulkanRenderer3D::activeBackend() noexcept
{
    refreshActiveBackendMode();
    return *SimpleGraphicsBackendInstance;
}

void VulkanRenderer3D::activateBackendMode(BackendMode mode) noexcept
{
    ActiveBackendMode = mode;
}

void VulkanRenderer3D::Reset(melonDS::GPU& gpu)
{
    activeBackend().Reset(gpu);
}

void VulkanRenderer3D::ResetActiveBackend(melonDS::GPU& gpu)
{
    (void)gpu;
    Texcache.Reset();
    HasCpuFrame = false;
    FrameIdentical = false;
    LastSubmittedRenderPolygonCount = 0;
    LastSubmittedRenderContext = nullptr;
    SkipRenderAtVCount215 = false;
    InEarlySubmitAttempt = false;
    CurrentEarlySubmitContextWaitNs = 0;
    CaptureReadbackPending = false;
    PendingCaptureReadbackContext = nullptr;
    resetCaptureLineState();
    clearLineCache();
    LastValidExactCaptureLineCache.fill(0);
    HasLastValidExactCapture = false;
    LastValidExactCaptureScreenSwap = false;
    CurrentCaptureScreenSwapHint = false;
    HasCurrentCaptureScreenSwapHint = false;
    CurrentRenderScreenSwap = false;
    CaptureLineExportCount = 0;
    EarlySubmitAttemptCount = 0;
    EarlySubmitHitCount = 0;
    EarlySubmitMissCount = 0;
    EarlySubmitSkipVCount215Count = 0;
}

void VulkanRenderer3D::VCount144(melonDS::GPU& gpu)
{
    activeBackend().VCount144(gpu);
}

void VulkanRenderer3D::VCount144ActiveBackend(melonDS::GPU& gpu)
{
    SkipRenderAtVCount215 = false;

    if (!Threaded)
        return;

    const u32 captureCnt = gpu.CaptureCnt;
    const bool captureEnabled = (captureCnt & (1u << 31u)) != 0u;
    const u32 captureMode = (captureCnt >> 29u) & 0x3u;
    const bool captureNeedsSourceA = captureEnabled && (captureMode != 1u);
    if (!captureNeedsSourceA)
        return;

    if (!ensureInitialized())
        return;

    const VulkanDeviceProfile& deviceProfile = VulkanContext::Get().GetDeviceProfile();
    if (deviceProfile.IsAdreno || deviceProfile.IsArmMali || deviceProfile.IsPowerVR)
        return;

    EarlySubmitAttemptCount++;
    InEarlySubmitAttempt = true;
    CurrentEarlySubmitContextWaitNs = 0;
    const u64 earlySubmitStartNs = PerfNowNs();
    RenderFrameActiveBackend(gpu);
    EarlySubmitCpuWindow.Add(PerfNowNs() - earlySubmitStartNs);
    EarlySubmitContextWaitCpuWindow.Add(CurrentEarlySubmitContextWaitNs);
    InEarlySubmitAttempt = false;
    CurrentEarlySubmitContextWaitNs = 0;
    SkipRenderAtVCount215 = Initialized && ColorImageInitialized;
    if (SkipRenderAtVCount215)
        EarlySubmitHitCount++;
    else
        EarlySubmitMissCount++;
}

void VulkanRenderer3D::RenderFrame(melonDS::GPU& gpu)
{
    activeBackend().RenderFrame(gpu);
}

void VulkanRenderer3D::RenderFrameActiveBackend(melonDS::GPU& gpu)
{
    refreshActiveBackendMode();
    CurrentRenderScreenSwap = gpu.ScreenSwap;

    if (SkipRenderAtVCount215 && gpu.VCount == 215u)
    {
        SkipRenderAtVCount215 = false;
        EarlySubmitSkipVCount215Count++;
        return;
    }

    const u64 renderStartNs = PerfNowNs();
    auto renderPerfScope = MakeScopeExit([&]() {
        RenderCpuWindow.Add(PerfNowNs() - renderStartNs);
        logPerformanceIfNeeded();
    });

    if (Initialized && ActiveBackendMode == BackendMode::GraphicsHardware)
        (void)waitForTextureCacheMutationSafePoint();
    u8 clearBitmapDirty = 0;
    const bool textureCacheChanged = Texcache.Update(clearBitmapDirty);
    WarmTextureCache(gpu);

    const u32 scale = static_cast<u32>(std::max(1, ScaleFactor));
    const u32 targetWidth = 256u * scale;
    const u32 targetHeight = 192u * scale;
    const u32 captureCnt = gpu.CaptureCnt;
    const bool captureEnabled = (captureCnt & (1u << 31u)) != 0u;
    const u32 captureMode = (captureCnt >> 29u) & 0x3u;
    const u32 captureSizeMode = (captureCnt >> 20u) & 0x3u;
    const bool captureSource3d = (captureCnt & (1u << 24u)) != 0u;
    const bool sourceAContributes = captureMode == 0u
        || ((captureMode >= 2u) && ((captureCnt & 0x1Fu) != 0u));
    const bool bg0Uses3d = (gpu.GPU2D_A.DispCnt & 0x0108u) == 0x0108u;
    const bool captureNeedsCpuReadback = false;
    const bool captureNeedsGpuCaptureLineBase =
        captureEnabled
        && (captureMode != 1u)
        && (captureSource3d || (bg0Uses3d && sourceAContributes));
    const auto updateExactCaptureFallbackColor = [&]() {
        const u32 clearColor = Debug3dClearMagenta ? 0xFFFF00FFu : buildClearColorRgba8(gpu);
        const u32 r = clearColor & 0xFFu;
        const u32 g = (clearColor >> 8u) & 0xFFu;
        const u32 b = (clearColor >> 16u) & 0xFFu;
        const u32 a = (clearColor >> 24u) & 0xFFu;
        ExactCaptureFallbackPackedColor =
            (r >> 2u)
            | ((g >> 2u) << 8u)
            | ((b >> 2u) << 16u)
            | ((a >> 3u) << 24u);
        ExactCaptureFallbackValid = true;
    };
    FrameIdentical = !textureCacheChanged && gpu.GPU3D.RenderFrameIdentical;
    const bool needsZeroGeometryRefresh =
        gpu.GPU3D.RenderNumPolygons == 0u && LastSubmittedRenderPolygonCount != 0u;
    const bool canReuseIdenticalFrame = FrameIdentical
        && Initialized
        && ColorImageInitialized
        && ColorImage != VK_NULL_HANDLE
        && ColorImageView != VK_NULL_HANDLE
        && ColorImageWidth == targetWidth
        && ColorImageHeight == targetHeight
        && !needsZeroGeometryRefresh;
    if (canReuseIdenticalFrame)
    {
        if (ActiveBackendMode == BackendMode::GraphicsHardware && captureNeedsGpuCaptureLineBase)
        {
            updateExactCaptureFallbackColor();
            if (!CaptureLinePending && !CaptureLineReady)
                (void)submitGraphicsCaptureExportForCurrentFrame();
        }
        return;
    }

    if (ActiveBackendMode == BackendMode::GraphicsHardware)
    {
        ExactCaptureLineCachePrepared = false;
        ExactCaptureLineCacheFresh = false;
        HasCpuFrame = false;
    }

    if (!ensureInitialized())
    {
        HasCpuFrame = false;
        return;
    }

    const VulkanDeviceProfile& deviceProfile = VulkanContext::Get().GetDeviceProfile();
    const bool hadCpuFrame = HasCpuFrame;
    bool deferGpuCaptureLineExport = false;
    if (ActiveBackendMode == BackendMode::GraphicsHardware && captureNeedsGpuCaptureLineBase)
    {
        const auto captureLineSlotBusy = [&](u32 slot) {
            return (CaptureLinePending
                    && PendingCaptureLineContext == nullptr
                    && PendingCaptureLineBufferSlot == static_cast<int>(slot))
                || (CaptureLineReady
                    && ReadyCaptureLineBufferSlot == static_cast<int>(slot));
        };

        u32 desiredSlot = ActiveCaptureLineBufferSlot;
        if (captureLineSlotBusy(desiredSlot))
        {
            const u32 alternateSlot = (desiredSlot + 1u) % CaptureLineBufferSlotCount;
            if (!captureLineSlotBusy(alternateSlot))
                desiredSlot = alternateSlot;
            else
                deferGpuCaptureLineExport = true;
        }

        if (!deferGpuCaptureLineExport)
            selectActiveCaptureLineBufferSlot(desiredSlot);
    }
    const bool captureNeedsGpuCaptureLine = captureNeedsGpuCaptureLineBase && !deferGpuCaptureLineExport;
    struct FrameRasterSelection
    {
        RasterExecutionProfile Profile;
        RasterDispatchPath DispatchPath;
        bool UseCpuTileBinning;
    };
    const auto selectFrameRaster = [&](const VulkanDeviceProfile& profile) -> FrameRasterSelection {
        if (Threaded && profile.IsAdreno)
        {
            return {
                RasterExecutionProfile::AdrenoCpuDense,
                RasterDispatchPath::DirectTiles,
                true,
            };
        }

        if (Threaded && profile.IsMaliG52Class)
        {
            return {
                RasterExecutionProfile::MaliCpuDense,
                RasterDispatchPath::DirectTiles,
                true,
            };
        }

        if (profile.IsArmMali)
        {
            return {
                RasterExecutionProfile::MaliDenseScan,
                RasterDispatchPath::DirectTiles,
                false,
            };
        }

        if (ActiveTextureSamplingPath == TextureSamplingPath::NonUniform)
        {
            return {
                RasterExecutionProfile::GeneralNonUniform,
                RasterDispatchPath::DirectTiles,
                false,
            };
        }

        return {
            RasterExecutionProfile::LegacyFallback,
            RasterDispatchPath::LegacyWorklist,
            false,
        };
    };
    const FrameRasterSelection frameRasterSelection = selectFrameRaster(deviceProfile);
    ActiveRasterExecutionProfile = frameRasterSelection.Profile;
    ActiveRasterDispatchPath = frameRasterSelection.DispatchPath;
    CpuTileBinningEnabled = frameRasterSelection.UseCpuTileBinning;
    if (!captureNeedsGpuCaptureLineBase)
    {
        ActiveCapturePathMode = CapturePathMode::Disabled;
        CapturePathModeCounts[static_cast<size_t>(CapturePathMode::Disabled)]++;
    }

    if (!ensureRenderTarget(targetWidth, targetHeight))
    {
        HasCpuFrame = false;
        return;
    }

    if (ActiveBackendMode != BackendMode::GraphicsHardware && !ensureResultBuffer(targetWidth, targetHeight))
    {
        HasCpuFrame = false;
        return;
    }

    if (!Threaded && !waitForReadbackSource())
    {
        HasCpuFrame = false;
        return;
    }

    const bool useThreadedRenderContexts = Threaded;
    RenderContext* renderContext = nullptr;
    if (useThreadedRenderContexts)
    {
        renderContext = tryAcquireReadyRenderContext();
        bool renderContextReady = renderContext != nullptr;
        if (!renderContextReady)
        {
            const bool useNonBlockingAcquire = MelonDSAndroid::isFastForwardActive() || PostFastForwardDrainFrames > 0;
            if (useNonBlockingAcquire)
            {
                renderContext = &acquireNextRenderContext();
                renderContextReady = renderContext != nullptr && tryAcquireRenderContext(*renderContext);
            }
            else
            {
                const u64 contextWaitStartNs = PerfNowNs();
                renderContext = &acquireNextRenderContext();
                renderContextReady = renderContext != nullptr && waitForRenderContext(*renderContext);
                if (InEarlySubmitAttempt)
                    CurrentEarlySubmitContextWaitNs += (PerfNowNs() - contextWaitStartNs);
            }
        }
        if (PostFastForwardDrainFrames > 0)
            PostFastForwardDrainFrames--;
        if (!renderContextReady)
        {
            HasCpuFrame = false;
            return;
        }
    }

    buildTriangleList(gpu);
    if (!ensureTriangleBuffer(renderContext, Triangles.size()))
    {
        HasCpuFrame = false;
        return;
    }

    if (!ensureToonBuffer(renderContext))
    {
        HasCpuFrame = false;
        return;
    }

    if (ActiveBackendMode == BackendMode::GraphicsHardware
        && !ensureGraphicsVertexBuffer(renderContext, GraphicsVertices.size()))
    {
        HasCpuFrame = false;
        return;
    }

    if (ActiveBackendMode == BackendMode::GraphicsHardware
        && (!ensureGraphicsSceneVertexBuffer(renderContext, GraphicsSceneVertices.size())
            || !ensureGraphicsEdgeIndexBuffer(renderContext, SharedGraphicsScene.EdgeIndices.size())))
    {
        HasCpuFrame = false;
        return;
    }

    if (ActiveBackendMode == BackendMode::GraphicsHardware)
    {
        if (!ensureGraphicsClearBuffer(renderContext) || !updateGraphicsClearBuffer(renderContext, gpu))
        {
            HasCpuFrame = false;
            return;
        }
    }

    if (!ensureCaptureLineBuffer(renderContext))
    {
        HasCpuFrame = false;
        return;
    }

    if (ActiveBackendMode != BackendMode::GraphicsHardware)
    {
        if (!ensureResultBuffer(targetWidth, targetHeight))
        {
            HasCpuFrame = false;
            return;
        }

        const bool useContextCpuTileBinning = renderContext != nullptr && useCpuTileBinning();
        if (!useContextCpuTileBinning && !ensureBinMaskBuffer(Triangles.size(), targetWidth, targetHeight))
        {
            HasCpuFrame = false;
            return;
        }

        if (!useContextCpuTileBinning && !ensureGroupListBuffer(Triangles.size(), targetWidth, targetHeight))
        {
            HasCpuFrame = false;
            return;
        }

        if (useContextCpuTileBinning && !ensureCpuBinBuffers(*renderContext, Triangles.size(), targetWidth, targetHeight))
        {
            HasCpuFrame = false;
            return;
        }

        if (useContextCpuTileBinning && !ensureCpuSpanSetupBuffer(*renderContext, Triangles.size()))
        {
            HasCpuFrame = false;
            return;
        }

        if (!useContextCpuTileBinning && !ensureSpanSetupBuffer(Triangles.size()))
        {
            HasCpuFrame = false;
            return;
        }

        if (useContextCpuTileBinning && !ensureCpuWorkOffsetBuffer(*renderContext, targetWidth, targetHeight, Triangles.size()))
        {
            HasCpuFrame = false;
            return;
        }

        if (!useContextCpuTileBinning && !ensureWorkOffsetBuffer(targetWidth, targetHeight, Triangles.size()))
        {
            HasCpuFrame = false;
            return;
        }

        updateDescriptorSet(renderContext);
    }
    else
    {
        updateGraphicsDescriptorSet(renderContext);
    }

    if (captureEnabled)
    {
        CaptureEnabledCount++;
        CaptureModeCounts[captureMode]++;
        CaptureSizeModeCounts[captureSizeMode]++;
        if (captureSource3d)
            CaptureSource3dCount++;
    }

    const u32 clearColor = Debug3dClearMagenta ? 0xFFFF00FFu : buildClearColorRgba8(gpu);
    updateExactCaptureFallbackColor();
    const u32 clearDepth = ((gpu.GPU3D.RenderClearAttr2 & 0x7FFFu) * 0x200u) + 0x1FFu;
    if (!dispatchRasterAndReadback(
            renderContext,
            clearColor,
            clearDepth,
            gpu.GPU3D.RenderDispCnt,
            gpu.GPU3D.RenderAlphaRef,
            gpu.GPU3D.RenderFogColor,
            gpu.GPU3D.RenderFogOffset,
            gpu.GPU3D.RenderFogShift,
            gpu.GPU3D.RenderClearAttr1 & 0x3F008000u,
            gpu.GPU3D.RenderFogDensityTable,
            gpu.GPU3D.RenderEdgeTable,
            gpu.GPU3D.RenderToonTable,
            captureNeedsCpuReadback,
            captureNeedsGpuCaptureLine))
    {
        HasCpuFrame = false;
        return;
    }

    if (ActiveBackendMode == BackendMode::GraphicsHardware
        && (captureNeedsGpuCaptureLine || deferGpuCaptureLineExport))
    {
        // graphics_hw must let PrepareCaptureFrame() latch the exact DS-sized
        // export for the current frame. Reusing a previous CPU buffer here is
        // fine temporarily, but it must be cleared before the exact capture is
        // consumed so that GPU2D_Soft never mixes old and new lines.
        HasCpuFrame = hadCpuFrame;
    }
    else if (!captureNeedsCpuReadback)
        HasCpuFrame = false;

    if (!captureNeedsGpuCaptureLineBase)
    {
        if (!captureNeedsCpuReadback)
            clearRawReadbackState();
        resetCaptureLineState();
    }

    LastSubmittedRenderPolygonCount = gpu.GPU3D.RenderNumPolygons;
}

void VulkanRenderer3D::RestartFrame(melonDS::GPU& gpu)
{
    activeBackend().RestartFrame(gpu);
}

void VulkanRenderer3D::RestartFrameActiveBackend(melonDS::GPU& gpu)
{
    (void)gpu;
}

u32* VulkanRenderer3D::GetLine(int line)
{
    return activeBackend().GetLine(line);
}

u32* VulkanRenderer3D::GetLineActiveBackend(int line)
{
    const bool exactCaptureOnly = ActiveBackendMode == BackendMode::GraphicsHardware;
    const bool needsExactCaptureLatch = exactCaptureOnly && !ExactCaptureLineCachePrepared;

    if (line < 0)
        line = 0;
    else if (line > 191)
        line = 191;

    // Match OpenGL capture contract: latch the full 256x192 source only on the
    // first requested line. Some capture scenes do not request 3D on line 0,
    // so graphics_hw must latch on the first actual GetLine() consumer, not
    // only when that consumer happens to ask for row 0.
    if (line == 0 || needsExactCaptureLatch)
    {
        bool usedFallbackFill = false;
        bool usedPreviousValidFill = false;
        if (CaptureLinePending)
        {
            if (!finalizeCaptureLineFrame())
                resetCaptureLineState();
        }
        else if (CaptureLineReady && ReadyCaptureLineData == nullptr)
        {
            resetCaptureLineState();
        }

        const auto readyCaptureMatchesHint = [&]() {
            return !exactCaptureOnly
                || !HasCurrentCaptureScreenSwapHint
                || ReadyCaptureLineScreenSwap == CurrentCaptureScreenSwapHint;
        };

        if (!HasCpuFrame && CaptureLineReady && ReadyCaptureLineData != nullptr && readyCaptureMatchesHint())
        {
            HasCpuFrame = copyReadyCaptureLineToLineCache();
        }

        if (exactCaptureOnly && !ExactCaptureLineCachePrepared)
        {
            // graphics_hw should only expose an exact capture produced for this
            // frame. Preserve a line cache already latched for this frame
            // (exact export or same-frame fallback), but never expose stale CPU
            // data carried from an older frame.
            HasCpuFrame = false;
        }

        if (!HasCpuFrame && !exactCaptureOnly && CaptureReadbackPending && finalizeCaptureReadback(true))
            convertReadbackToLineCache();

        if (!HasCpuFrame)
        {
            if (CaptureLineReady && ReadyCaptureLineData != nullptr && readyCaptureMatchesHint())
            {
                // Latch capture export to a stable CPU buffer. Returning the
                // persistently mapped GPU buffer directly can flicker when threaded
                // contexts rotate and overwrite capture lines mid-composition.
                HasCpuFrame = copyReadyCaptureLineToLineCache();
            }
            else if (!exactCaptureOnly && readbackColorTargetToCpu(true))
            {
                convertReadbackToLineCache();
            }
            else
            {
                if (exactCaptureOnly && restoreLastValidExactCaptureToLineCache())
                {
                    HasCpuFrame = true;
                    usedPreviousValidFill = true;
                }
                else if (exactCaptureOnly && ExactCaptureFallbackValid)
                {
                    fillLineCacheWithCaptureFallbackColor();
                    HasCpuFrame = true;
                    usedFallbackFill = true;
                }
                else
                {
                    clearLineCache();
                }
            }
        }

        logCaptureDebugState(
            "GetLine",
            exactCaptureOnly,
            CaptureLinePending,
            CaptureLineReady,
            HasCpuFrame,
            ExactCaptureLineCacheFresh,
            usedPreviousValidFill,
            usedFallbackFill,
            LineCache,
            CaptureDebugLogsRemaining
        );
    }

    return &LineCache[static_cast<size_t>(line) * 256u];
}

void VulkanRenderer3D::SetupAccelFrame()
{
    activeBackend().SetupAccelFrame();
}

void VulkanRenderer3D::SetupAccelFrameActiveBackend()
{
}

void VulkanRenderer3D::PrepareCaptureFrame()
{
    activeBackend().PrepareCaptureFrame();
}

void VulkanRenderer3D::SetCaptureScreenSwapHint(bool screenSwap)
{
    CurrentCaptureScreenSwapHint = screenSwap;
    HasCurrentCaptureScreenSwapHint = true;
}

void VulkanRenderer3D::BeginCaptureFrame()
{
    BeginCaptureFrameActiveBackend();
}

void VulkanRenderer3D::PrepareCaptureFrameActiveBackend()
{
    CapturePrepareRequestCount++;
    const bool exactCaptureOnly = ActiveBackendMode == BackendMode::GraphicsHardware;

    if (exactCaptureOnly && !ExactCaptureLineCachePrepared)
        HasCpuFrame = false;

    if (CaptureLinePending || CaptureLineReady)
    {
        if (finalizeCaptureLineFrame(exactCaptureOnly))
        {
            const bool readyMatchesHint =
                !exactCaptureOnly
                || !HasCurrentCaptureScreenSwapHint
                || ReadyCaptureLineScreenSwap == CurrentCaptureScreenSwapHint;
            HasCpuFrame = readyMatchesHint && copyReadyCaptureLineToLineCache();
            return;
        }
        if (CaptureLinePending)
            return;
    }

    if (!HasCpuFrame && CaptureReadbackPending)
    {
        if (finalizeCaptureReadback(false))
        {
            convertReadbackToLineCache();
            return;
        }
        if (CaptureReadbackPending)
            return;
    }

    if (HasCpuFrame && RawReadbackWidth > 0 && RawReadbackHeight > 0 && !RawReadbackRgba.empty())
    {
        convertReadbackToLineCache();
        return;
    }

    if (exactCaptureOnly && HasCpuFrame && ExactCaptureLineCachePrepared)
        return;

    // graphics_hw must not submit a second late capture export or force a
    // fallback readback from here. The exact capture line export for this
    // frame is the only valid source for software 2D composition.
    if (exactCaptureOnly)
    {
        if (restoreLastValidExactCaptureToLineCache())
        {
            HasCpuFrame = true;
        }
        else if (ExactCaptureFallbackValid)
        {
            fillLineCacheWithCaptureFallbackColor();
            HasCpuFrame = true;
        }
        else
        {
            clearLineCache();
        }
        return;
    }

    if (!readbackColorTargetToCpu(true))
    {
        HasCpuFrame = false;
        return;
    }

    convertReadbackToLineCache();
}

void VulkanRenderer3D::BeginCaptureFrameActiveBackend()
{
    if (ActiveBackendMode != BackendMode::GraphicsHardware)
        return;

    if (!ExactCaptureLineCachePrepared)
        HasCpuFrame = false;
    ExactCaptureLineCacheFresh = false;

    if (MelonDSAndroid::areRendererDebugToolsEnabled() && CaptureDebugLogsRemaining > 0u)
    {
        Log(
            LogLevel::Warn,
            "VulkanCapture[FrameStart]: pending=%u ready=%u hasCpu=%u fresh=%u fallbackValid=%u mode=%s",
            CaptureLinePending ? 1u : 0u,
            CaptureLineReady ? 1u : 0u,
            HasCpuFrame ? 1u : 0u,
            ExactCaptureLineCacheFresh ? 1u : 0u,
            ExactCaptureFallbackValid ? 1u : 0u,
            backendModeName(ActiveBackendMode));
        CaptureDebugLogsRemaining--;
    }
}

void VulkanRenderer3D::Blit(const melonDS::GPU& gpu)
{
    activeBackend().Blit(gpu);
}

void VulkanRenderer3D::BlitActiveBackend(const melonDS::GPU& gpu)
{
    CurrentRenderScreenSwap = gpu.ScreenSwap;

    if (ActiveBackendMode != BackendMode::GraphicsHardware
        || !Initialized
        || !ColorImageInitialized
        || Device == VK_NULL_HANDLE
        || Queue == VK_NULL_HANDLE
        || CommandBuffer == VK_NULL_HANDLE
        || FrameFence == VK_NULL_HANDLE)
    {
        return;
    }

    const u32 captureCnt = gpu.CaptureCnt;
    const bool captureEnabled = (captureCnt & (1u << 31u)) != 0u;
    const u32 captureMode = (captureCnt >> 29u) & 0x3u;
    const bool captureSource3d = (captureCnt & (1u << 24u)) != 0u;
    const bool sourceAContributes = captureMode == 0u
        || ((captureMode >= 2u) && ((captureCnt & 0x1Fu) != 0u));
    const bool bg0Uses3d = (gpu.GPU2D_A.DispCnt & 0x0108u) == 0x0108u;
    const bool captureNeedsGpuCaptureLine =
        captureEnabled
        && (captureMode != 1u)
        && (captureSource3d || (bg0Uses3d && sourceAContributes));
    if (!captureNeedsGpuCaptureLine)
        return;

    // Match the OpenGL timing contract more closely: prime the DS-sized export
    // during VBlank, before GPU2D_Soft starts the next frame's capture path.
    // This avoids relying on a later GetLine() consumer to resurrect the
    // correct frame after packed buffers have already been composed.
    //
    // If the main render submission already left an exact capture export
    // pending/ready for this frame, keep that state intact. Resetting it here
    // can discard the same-frame DS-sized source and force software 2D to
    // consume a stale/empty replacement on the next capture pass.
    if (CaptureLinePending || CaptureLineReady)
        return;

    resetCaptureLineState();
    ExactCaptureLineCachePrepared = false;
    ExactCaptureLineCacheFresh = false;
    HasCpuFrame = false;
    (void)submitGraphicsCaptureExportForCurrentFrame();
}

void VulkanRenderer3D::Stop(const melonDS::GPU& gpu)
{
    activeBackend().Stop(gpu);
}

void VulkanRenderer3D::StopActiveBackend(const melonDS::GPU& gpu)
{
    (void)gpu;
    Texcache.Reset();
    destroyVulkan();
    InitFailed = false;
    HasCpuFrame = false;
    SkipRenderAtVCount215 = false;
    InEarlySubmitAttempt = false;
    CurrentEarlySubmitContextWaitNs = 0;
    LastSubmittedRenderPolygonCount = 0;
    ExactCaptureFallbackPackedColor = 0;
    ExactCaptureFallbackValid = false;
    resetCaptureLineState();
    clearLineCache();
    LastValidExactCaptureLineCache.fill(0);
    HasLastValidExactCapture = false;
    LastValidExactCaptureScreenSwap = false;
    CurrentCaptureScreenSwapHint = false;
    HasCurrentCaptureScreenSwapHint = false;
    CurrentRenderScreenSwap = false;
    CaptureLineExportCount = 0;
    EarlySubmitAttemptCount = 0;
    EarlySubmitHitCount = 0;
    EarlySubmitMissCount = 0;
    EarlySubmitSkipVCount215Count = 0;
}

void VulkanRenderer3D::SetRenderSettings(
    bool threaded,
    bool betterPolygons,
    int scale,
    bool useSimplePipeline,
    bool conservativeCoverageEnabled,
    float conservativeCoveragePx,
    float conservativeCoverageDepthBias,
    bool conservativeCoverageApplyRepeat,
    bool conservativeCoverageApplyClamp,
    bool debug3dClearMagenta,
    melonDS::GPU& gpu) noexcept
{
    SetThreaded(threaded, gpu);

    const int oldScale = ScaleFactor;
    BetterPolygons = betterPolygons;
    ScaleFactor = std::max(1, scale);
    (void)useSimplePipeline;
    UseSimplePipeline = true;
    CoverageFixEnabled = conservativeCoverageEnabled;
    CoverageFixPx = std::clamp(conservativeCoveragePx, 0.0f, 2.0f);
    CoverageFixDepthBias = std::clamp(conservativeCoverageDepthBias, 0.0f, 0.01f);
    CoverageFixApplyRepeat = conservativeCoverageApplyRepeat;
    CoverageFixApplyClamp = conservativeCoverageApplyClamp;
    Debug3dClearMagenta = debug3dClearMagenta;

    if (Initialized && oldScale != ScaleFactor)
    {
        // The next frame creates the new-size target and waits only for the
        // shared graphics queue tail before retiring the old attachments.
        InvalidatePresentationState(true);
    }
}

void VulkanRenderer3D::SetThreaded(bool threaded, melonDS::GPU& gpu) noexcept
{
    (void)gpu;
    const bool enableThreaded = threaded;
    if (Threaded == enableThreaded)
        return;

    if (Initialized)
        (void)waitForDeviceIdle("threaded mode change");

    Threaded = enableThreaded;
    NextRenderContextIndex = 0;
    InvalidatePresentationState(false);
    if (MelonDSAndroid::areRendererDebugToolsEnabled())
        Log(LogLevel::Warn, "VulkanRenderer3D: threaded rendering %s", Threaded ? "enabled" : "disabled");
}

void VulkanRenderer3D::SetBackendMode(BackendMode mode) noexcept
{
    (void)mode;
    const bool modeChanged = RequestedBackendMode != BackendMode::GraphicsHardware;
    RequestedBackendMode = BackendMode::GraphicsHardware;
    refreshActiveBackendMode();
    if (modeChanged)
        InvalidatePresentationState(true);
    if (MelonDSAndroid::areRendererDebugToolsEnabled())
    {
        Log(
            LogLevel::Warn,
            "VulkanRuntime[Backend]: backendConfigured=%s backendActive=%s simplePipeline=%d",
            backendModeName(RequestedBackendMode),
            backendModeName(ActiveBackendMode),
            UseSimplePipeline ? 1 : 0
        );
    }
}

bool VulkanRenderer3D::IsThreaded() const noexcept
{
    return Threaded;
}

std::vector<u32> VulkanRenderer3D::CaptureColorTargetForDebug()
{
    if (!ensureInitialized() || ColorImage == VK_NULL_HANDLE || ColorImageWidth == 0 || ColorImageHeight == 0)
        return {};

    if (!readbackColorTargetToCpu(false))
        return {};

    return RawReadbackRgba;
}

std::vector<u32> VulkanRenderer3D::CaptureTopDepthForDebug()
{
    if (!ensureInitialized() || ColorImageWidth == 0 || ColorImageHeight == 0)
        return {};

    if (ActiveBackendMode == BackendMode::GraphicsHardware)
    {
        std::vector<u32> depthPixels;
        if (!readbackGraphicsDepthImageToCpu(depthPixels))
            return {};
        return depthPixels;
    }

    if (ResultBuffer == VK_NULL_HANDLE || !readbackResultBufferToCpu())
        return {};

    const size_t pixelCount = static_cast<size_t>(ColorImageWidth) * static_cast<size_t>(ColorImageHeight);
    if (RawResultReadback.size() < pixelCount * ResultLayerCount)
        return {};

    const auto depthBegin = RawResultReadback.begin() + static_cast<std::ptrdiff_t>(pixelCount * 2u);
    return std::vector<u32>(depthBegin, depthBegin + static_cast<std::ptrdiff_t>(pixelCount));
}

std::vector<u32> VulkanRenderer3D::CaptureTopAttrForDebug()
{
    if (!ensureInitialized() || ColorImageWidth == 0 || ColorImageHeight == 0)
        return {};

    if (ActiveBackendMode == BackendMode::GraphicsHardware)
    {
        std::vector<u32> attrPixels;
        if (!readbackGraphicsAttrImageToCpu(attrPixels))
            return {};
        return attrPixels;
    }

    if (ResultBuffer == VK_NULL_HANDLE || !readbackResultBufferToCpu())
        return {};

    const size_t pixelCount = static_cast<size_t>(ColorImageWidth) * static_cast<size_t>(ColorImageHeight);
    if (RawResultReadback.size() < pixelCount * ResultLayerCount)
        return {};

    const auto attrBegin = RawResultReadback.begin() + static_cast<std::ptrdiff_t>(pixelCount * 4u);
    return std::vector<u32>(attrBegin, attrBegin + static_cast<std::ptrdiff_t>(pixelCount));
}

std::vector<u32> VulkanRenderer3D::CaptureTopCoverageForDebug()
{
    const std::vector<u32> topAttr = CaptureTopAttrForDebug();
    if (topAttr.empty())
        return {};

    std::vector<u32> coverage(topAttr.size(), 0u);
    for (size_t i = 0; i < topAttr.size(); i++)
        coverage[i] = (topAttr[i] >> 8u) & 0x1Fu;
    return coverage;
}

Vulkan3DFrameView VulkanRenderer3D::GetVulkan3DFrameView() const noexcept
{
    Vulkan3DFrameView view{};
    view.ColorImage = ColorImage;
    view.ColorImageView = ColorImageView;
    view.ColorLayout = ColorImageLayout;
    view.Width = ColorImageWidth;
    view.Height = ColorImageHeight;
    view.Scale = static_cast<u32>(std::max(ScaleFactor, 1));
    view.FrameSerial = Structured2DFrameSerial;
    view.Generation = GPU3D.GetCurrentRendererGeneration();
    view.CompletionSemaphore = CompletionTimelineSemaphore;
    view.CompletionValue = LastCompletionTimelineValue;
    view.Valid = ColorImageInitialized
        && ColorImage != VK_NULL_HANDLE
        && ColorImageView != VK_NULL_HANDLE
        && ColorImageWidth != 0
        && ColorImageHeight != 0
        && ColorImageLayout != VK_IMAGE_LAYOUT_UNDEFINED
        && Structured2DFrameValid
        && Structured2DFrameSerial != 0
        && view.Generation != 0;
    return view;
}

bool VulkanRenderer3D::EnsureVulkanReadyForValidation()
{
    if (!ensureInitialized())
        return false;

    constexpr u32 kValidationWidth = 256;
    constexpr u32 kValidationHeight = 192;

    if (!ensureRenderTarget(kValidationWidth, kValidationHeight))
        return false;
    if (!ensureResultBuffer(kValidationWidth, kValidationHeight))
        return false;
    if (!ensureTriangleBuffer(nullptr, 1))
        return false;
    if (!ensureGraphicsVertexBuffer(nullptr, 1))
        return false;
    if (!ensureGraphicsSceneVertexBuffer(nullptr, 1))
        return false;
    if (!ensureGraphicsEdgeIndexBuffer(nullptr, 1))
        return false;
    if (!ensureBinMaskBuffer(0, kValidationWidth, kValidationHeight))
        return false;
    if (!ensureGroupListBuffer(0, kValidationWidth, kValidationHeight))
        return false;
    if (!ensureSpanSetupBuffer(1))
        return false;
    if (!ensureWorkOffsetBuffer(kValidationWidth, kValidationHeight, 1))
        return false;
    if (!ensureToonBuffer(nullptr))
        return false;
    if (!ensureCaptureLineBuffer(nullptr))
        return false;

    const VulkanDeviceProfile& deviceProfile = VulkanContext::Get().GetDeviceProfile();
    if (ActiveBackendMode == BackendMode::GraphicsHardware && deviceProfile.IsMaliG52Class)
    {
        // The synthetic validation draw below is not part of the graphics_hw
        // runtime path and triggers Mali-G52 driver faults. Real validation
        // still happens on the first prepared frame once gameplay starts.
        return true;
    }

    Triangles.clear();
    GraphicsVertices.clear();
    ActiveTextureDescriptorCount = 0;
    ActiveTextureDescriptors.fill(VkDescriptorImageInfo{});
    updateDescriptorSet(nullptr);

    std::array<u8, 34> fogDensity{};
    std::array<u16, 8> edgeColors{};
    std::array<u16, 32> toonTable{};

    return dispatchRasterAndReadback(
        nullptr,
        0xFF000000u,
        0x00FFFFFFu,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        fogDensity.data(),
        edgeColors.data(),
        toonTable.data(),
        false
    );
}

bool VulkanRenderer3D::ensureInitialized()
{
    if (Initialized)
        return true;

    if (InitFailed)
        return false;

    if (!VulkanContext::Get().Acquire())
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to acquire shared Vulkan context");
        InitFailed = true;
        return false;
    }

    ContextAcquired = true;
    Instance = VulkanContext::Get().GetInstance();
    PhysicalDevice = VulkanContext::Get().GetPhysicalDevice();
    Device = VulkanContext::Get().GetDevice();
    Queue = VulkanContext::Get().GetQueue();
    QueueFamilyIndex = VulkanContext::Get().GetQueueFamilyIndex();
    ResetQueryPool = VulkanContext::Get().GetResetQueryPool();
    TimestampPeriodNs = VulkanContext::Get().GetTimestampPeriod();
    TimestampQueriesSupported = VulkanContext::Get().SupportsTimestamps();
    ActiveTextureSamplingPath = resolveTextureSamplingPath();

    if (!createCommandObjects() || !createSyncObjects() || !createDescriptorObjects() || !createComputePipeline())
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to initialize Vulkan resources");
        destroyVulkan();
        InitFailed = true;
        return false;
    }

    if (!createGraphicsDescriptorObjects())
    {
        Log(LogLevel::Error, "VulkanRenderer3D: graphics descriptor initialization failed");
        destroyVulkan();
        InitFailed = true;
        return false;
    }
    else if (!createGraphicsPipelines())
    {
        Log(LogLevel::Error, "VulkanRenderer3D: graphics pipeline initialization failed");
        destroyVulkan();
        InitFailed = true;
        return false;
    }

    Initialized = true;
    refreshActiveBackendMode();
    return true;
}

void VulkanRenderer3D::destroyVulkan()
{
    if (Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(Device);

    CaptureReadbackPending = false;
    PendingCaptureReadbackContext = nullptr;
    resetCaptureLineState();
    destroyReadbackBuffer();
    destroyCaptureReadbackImage();
    destroyResultReadbackBuffer();
    destroyTriangleBuffer(nullptr);
    destroyGraphicsVertexBuffer(nullptr);
    destroyGraphicsSceneVertexBuffer(nullptr);
    destroyGraphicsEdgeIndexBuffer(nullptr);
    destroyBinMaskBuffer();
    destroyGroupListBuffer();
    destroySpanSetupBuffer();
    destroyWorkOffsetBuffer();
    destroyToonBuffer(nullptr);
    destroyGraphicsClearBuffer(nullptr);
    destroyAllCaptureLineBuffers();
    destroyResultBuffer();
    destroyFallbackTexture();
    destroyRenderTarget();

    if (InterpPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device, InterpPipeline, nullptr);
        InterpPipeline = VK_NULL_HANDLE;
    }

    if (BinPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device, BinPipeline, nullptr);
        BinPipeline = VK_NULL_HANDLE;
    }

    if (WorkOffsetsPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device, WorkOffsetsPipeline, nullptr);
        WorkOffsetsPipeline = VK_NULL_HANDLE;
    }

    if (SortPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device, SortPipeline, nullptr);
        SortPipeline = VK_NULL_HANDLE;
    }

    if (DepthBlendPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device, DepthBlendPipeline, nullptr);
        DepthBlendPipeline = VK_NULL_HANDLE;
    }

    for (VkPipeline& rasterPipeline : RasterPipelines)
    {
        if (rasterPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, rasterPipeline, nullptr);
            rasterPipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& finalPipeline : FinalPipelines)
    {
        if (finalPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, finalPipeline, nullptr);
            finalPipeline = VK_NULL_HANDLE;
        }
    }

    if (CaptureLineExportPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device, CaptureLineExportPipeline, nullptr);
        CaptureLineExportPipeline = VK_NULL_HANDLE;
    }

    if (GraphicsFinalFogPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device, GraphicsFinalFogPipeline, nullptr);
        GraphicsFinalFogPipeline = VK_NULL_HANDLE;
    }

    if (GraphicsFinalEdgePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device, GraphicsFinalEdgePipeline, nullptr);
        GraphicsFinalEdgePipeline = VK_NULL_HANDLE;
    }
    if (GraphicsFinalEdgeFogPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device, GraphicsFinalEdgeFogPipeline, nullptr);
        GraphicsFinalEdgeFogPipeline = VK_NULL_HANDLE;
    }

    if (GraphicsClearPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device, GraphicsClearPipeline, nullptr);
        GraphicsClearPipeline = VK_NULL_HANDLE;
    }

    if (GraphicsStencilBitClearPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device, GraphicsStencilBitClearPipeline, nullptr);
        GraphicsStencilBitClearPipeline = VK_NULL_HANDLE;
    }

    for (VkPipeline& pipeline : GraphicsShadowBlendBgZeroPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsShadowBlendPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsEdgeMarkPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsShadowClearPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsShadowMaskBgZeroPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsShadowMaskPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsTranslucentPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsBgZeroTranslucentPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsOpaquePipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsOpaqueFragmentDepthPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsOpaqueUiOverlayPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsOpaqueFastModulatePipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsOpaqueFastModulateToonPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsOpaqueFastModulatePlainPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsOpaqueFastModulateOpaqueAlphaToonPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    for (VkPipeline& pipeline : GraphicsOpaqueFastModulateOpaqueAlphaPlainPipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }

    savePipelineCache();
    if (ComputePipelineCache != VK_NULL_HANDLE)
    {
        vkDestroyPipelineCache(Device, ComputePipelineCache, nullptr);
        ComputePipelineCache = VK_NULL_HANDLE;
    }
    ComputePipelineCacheFile.clear();

    if (PipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        PipelineLayout = VK_NULL_HANDLE;
    }

    if (GraphicsPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Device, GraphicsPipelineLayout, nullptr);
        GraphicsPipelineLayout = VK_NULL_HANDLE;
    }

    if (GraphicsAttachmentSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(Device, GraphicsAttachmentSampler, nullptr);
        GraphicsAttachmentSampler = VK_NULL_HANDLE;
    }

    if (GraphicsFinalRenderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(Device, GraphicsFinalRenderPass, nullptr);
        GraphicsFinalRenderPass = VK_NULL_HANDLE;
    }

    if (GraphicsRasterRenderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(Device, GraphicsRasterRenderPass, nullptr);
        GraphicsRasterRenderPass = VK_NULL_HANDLE;
    }

    if (DescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
        DescriptorPool = VK_NULL_HANDLE;
    }

    if (GraphicsDescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(Device, GraphicsDescriptorPool, nullptr);
        GraphicsDescriptorPool = VK_NULL_HANDLE;
    }

    DescriptorSet = VK_NULL_HANDLE;
    SingleTextureDescriptorSets.fill(VK_NULL_HANDLE);
    invalidateAllDescriptorSetCaches();
    GraphicsDescriptorSet = VK_NULL_HANDLE;
    invalidateAllGraphicsDescriptorSetCaches();
    for (RenderContext& renderContext : RenderContexts)
    {
        renderContext.DescriptorSet = VK_NULL_HANDLE;
        renderContext.SingleTextureDescriptorSets.fill(VK_NULL_HANDLE);
        renderContext.GraphicsDescriptorSet = VK_NULL_HANDLE;
    }

    if (DescriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(Device, DescriptorSetLayout, nullptr);
        DescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (GraphicsDescriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(Device, GraphicsDescriptorSetLayout, nullptr);
        GraphicsDescriptorSetLayout = VK_NULL_HANDLE;
    }

    for (RenderContext& renderContext : RenderContexts)
    {
        destroyCpuSpanSetupBuffer(renderContext);
        destroyCpuBinBuffers(renderContext);
        destroyCpuWorkOffsetBuffer(renderContext);
        destroyTriangleBuffer(&renderContext);
        destroyGraphicsVertexBuffer(&renderContext);
        destroyGraphicsSceneVertexBuffer(&renderContext);
        destroyGraphicsEdgeIndexBuffer(&renderContext);
        destroyToonBuffer(&renderContext);
        destroyGraphicsClearBuffer(&renderContext);
        destroyCaptureLineBuffer(&renderContext);
        if (renderContext.TimestampQueryPool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(Device, renderContext.TimestampQueryPool, nullptr);
            renderContext.TimestampQueryPool = VK_NULL_HANDLE;
        }
        if (renderContext.FrameFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(Device, renderContext.FrameFence, nullptr);
            renderContext.FrameFence = VK_NULL_HANDLE;
        }
        if (renderContext.CommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(Device, renderContext.CommandPool, nullptr);
            renderContext.CommandPool = VK_NULL_HANDLE;
        }
        renderContext.CommandBuffer = VK_NULL_HANDLE;
    }

    if (CompletionTimelineSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(Device, CompletionTimelineSemaphore, nullptr);
        CompletionTimelineSemaphore = VK_NULL_HANDLE;
    }
    NextCompletionTimelineValue = 0;
    LastCompletionTimelineValue = 0;

    if (FrameFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(Device, FrameFence, nullptr);
        FrameFence = VK_NULL_HANDLE;
    }
    if (TimestampQueryPool != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(Device, TimestampQueryPool, nullptr);
        TimestampQueryPool = VK_NULL_HANDLE;
    }

    if (CommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(Device, CommandPool, nullptr);
        CommandPool = VK_NULL_HANDLE;
    }

    CommandBuffer = VK_NULL_HANDLE;
    NextRenderContextIndex = 0;

    if (ContextAcquired)
    {
        VulkanContext::Get().Release();
        ContextAcquired = false;
    }

    Instance = VK_NULL_HANDLE;
    PhysicalDevice = VK_NULL_HANDLE;
    Device = VK_NULL_HANDLE;
    Queue = VK_NULL_HANDLE;
    QueueFamilyIndex = 0;
    ResetQueryPool = nullptr;
    TimestampPeriodNs = 0.0f;
    TimestampQueriesSupported = false;
    Initialized = false;
    ColorImageInitialized = false;
    GraphicsReady = false;
    ActiveTextureDescriptorCount = 0;
    ActiveTextureDescriptors.fill(VkDescriptorImageInfo{});
    ActiveTextureSamplingPath = TextureSamplingPath::CompatDynamicUniform;
}

bool VulkanRenderer3D::createCommandObjects()
{
    if (!createCommandObjects(CommandPool, CommandBuffer))
        return false;

    for (RenderContext& renderContext : RenderContexts)
    {
        if (!createCommandObjects(renderContext.CommandPool, renderContext.CommandBuffer))
            return false;
    }

    return true;
}

bool VulkanRenderer3D::createCommandObjects(VkCommandPool& commandPool, VkCommandBuffer& commandBuffer)
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = QueueFamilyIndex;

    if (vkCreateCommandPool(Device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create command pool");
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(Device, &cmdAllocInfo, &commandBuffer) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate command buffer");
        return false;
    }

    return true;
}

bool VulkanRenderer3D::createSyncObjects()
{
    if (!createFence(FrameFence))
        return false;
    if (!createTimestampQueryPool(TimestampQueryPool))
        return false;

    for (RenderContext& renderContext : RenderContexts)
    {
        if (!createFence(renderContext.FrameFence))
            return false;
        if (!createTimestampQueryPool(renderContext.TimestampQueryPool))
            return false;
    }

    if (VulkanContext::Get().SupportsTimelineSemaphores())
    {
        VkSemaphoreTypeCreateInfo timelineInfo{};
        timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timelineInfo.initialValue = 0;
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreInfo.pNext = &timelineInfo;
        if (vkCreateSemaphore(Device, &semaphoreInfo, nullptr, &CompletionTimelineSemaphore) != VK_SUCCESS)
            return false;
    }

    return true;
}

bool VulkanRenderer3D::createFence(VkFence& fence)
{
    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateFence(Device, &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create frame fence");
        return false;
    }

    return true;
}

bool VulkanRenderer3D::createTimestampQueryPool(VkQueryPool& queryPool)
{
    if (!TimestampQueriesSupported)
        return true;

    VkQueryPoolCreateInfo queryPoolCreateInfo{};
    queryPoolCreateInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolCreateInfo.queryCount = TimestampQueryCount;

    if (vkCreateQueryPool(Device, &queryPoolCreateInfo, nullptr, &queryPool) != VK_SUCCESS)
    {
        Log(LogLevel::Warn, "VulkanRenderer3D: failed to create timestamp query pool");
        queryPool = VK_NULL_HANDLE;
    }

    return true;
}

bool VulkanRenderer3D::waitForRenderContext(RenderContext& context)
{
    if (Device == VK_NULL_HANDLE || context.FrameFence == VK_NULL_HANDLE)
        return false;

    const VkResult fenceStatus = vkGetFenceStatus(Device, context.FrameFence);
    if (fenceStatus == VK_NOT_READY)
        ContextMissCount++;

    const u64 waitStartNs = PerfNowNs();
    const VkResult waitResult = vkWaitForFences(Device, 1, &context.FrameFence, VK_TRUE, UINT64_MAX);
    if (waitResult != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: render context fence wait failed (%d)", static_cast<int>(waitResult));
        return false;
    }

    const u64 waitDurationNs = PerfNowNs() - waitStartNs;
    FenceWaitCpuWindow.Add(waitDurationNs);
    if (waitDurationNs >= 1000000ull)
        LateFrameCount++;
    consumeGpuTiming(&context);
    return context.CommandPool == VK_NULL_HANDLE
        || vkResetCommandPool(Device, context.CommandPool, 0) == VK_SUCCESS;
}

bool VulkanRenderer3D::tryAcquireRenderContext(RenderContext& context, bool countMisses)
{
    if (Device == VK_NULL_HANDLE || context.FrameFence == VK_NULL_HANDLE)
        return false;

    const VkResult fenceStatus = vkGetFenceStatus(Device, context.FrameFence);
    if (fenceStatus == VK_SUCCESS)
    {
        FenceWaitCpuWindow.Add(0);
        consumeGpuTiming(&context);
        return context.CommandPool == VK_NULL_HANDLE
            || vkResetCommandPool(Device, context.CommandPool, 0) == VK_SUCCESS;
    }

    if (fenceStatus == VK_NOT_READY)
    {
        if (countMisses)
        {
            ContextMissCount++;
            DroppedFrameCount++;
        }
        return false;
    }

    Log(LogLevel::Error, "VulkanRenderer3D: render context fence status failed (%d)", static_cast<int>(fenceStatus));
    return false;
}

VulkanRenderer3D::RenderContext* VulkanRenderer3D::tryAcquireReadyRenderContext() noexcept
{
    if (!Threaded || Device == VK_NULL_HANDLE)
        return nullptr;

    for (size_t i = 0; i < AsyncRenderContextCount; i++)
    {
        const size_t contextIndex = (NextRenderContextIndex + i) % AsyncRenderContextCount;
        RenderContext& context = RenderContexts[contextIndex];
        if (!tryAcquireRenderContext(context, false))
            continue;

        NextRenderContextIndex = (contextIndex + 1) % AsyncRenderContextCount;
        return &context;
    }

    return nullptr;
}

bool VulkanRenderer3D::waitForAllRenderContexts()
{
    for (RenderContext& renderContext : RenderContexts)
    {
        if (!waitForRenderContext(renderContext))
            return false;
    }

    return true;
}

bool VulkanRenderer3D::waitForReadbackSource()
{
    if (Device == VK_NULL_HANDLE)
        return false;

    const bool usePrimaryFrameFence =
        !Threaded
        || LastSubmittedRenderContext == nullptr;
    if (usePrimaryFrameFence)
    {
        if (FrameFence == VK_NULL_HANDLE)
            return false;

        const VkResult fenceStatus = vkGetFenceStatus(Device, FrameFence);
        if (fenceStatus == VK_SUCCESS)
        {
            FenceWaitCpuWindow.Add(0);
            consumeGpuTiming(nullptr);
            return true;
        }
        if (fenceStatus != VK_NOT_READY)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: frame fence status failed (%d)", static_cast<int>(fenceStatus));
            return false;
        }

        const u64 waitStartNs = PerfNowNs();
        const VkResult waitResult = vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX);
        if (waitResult != VK_SUCCESS)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: frame fence wait failed (%d)", static_cast<int>(waitResult));
            return false;
        }

        const u64 waitDurationNs = PerfNowNs() - waitStartNs;
        FenceWaitCpuWindow.Add(waitDurationNs);
        if (waitDurationNs >= 1000000ull)
            LateFrameCount++;
        consumeGpuTiming(nullptr);
        return true;
    }

    // Render and presentation share one queue. Waiting for the newest submitted
    // render context fence guarantees all older submissions are finished too.
    if (LastSubmittedRenderContext != nullptr)
        return waitForRenderContext(*LastSubmittedRenderContext);

    return waitForAllRenderContexts();
}

bool VulkanRenderer3D::waitForTextureCacheMutationSafePoint()
{
    if (Device == VK_NULL_HANDLE)
        return false;

    bool ok = true;
    if (FrameFence != VK_NULL_HANDLE)
    {
        const VkResult fenceStatus = vkGetFenceStatus(Device, FrameFence);
        if (fenceStatus == VK_SUCCESS)
        {
            FenceWaitCpuWindow.Add(0);
            consumeGpuTiming(nullptr);
        }
        else if (fenceStatus == VK_NOT_READY)
        {
            const u64 waitStartNs = PerfNowNs();
            const VkResult waitResult = vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX);
            if (waitResult == VK_SUCCESS)
            {
                const u64 waitDurationNs = PerfNowNs() - waitStartNs;
                FenceWaitCpuWindow.Add(waitDurationNs);
                if (waitDurationNs >= 1000000ull)
                    LateFrameCount++;
                consumeGpuTiming(nullptr);
            }
            else
            {
                Log(LogLevel::Error, "VulkanRenderer3D: texture cache frame fence wait failed (%d)", static_cast<int>(waitResult));
                ok = false;
            }
        }
        else
        {
            Log(LogLevel::Error, "VulkanRenderer3D: texture cache frame fence status failed (%d)", static_cast<int>(fenceStatus));
            ok = false;
        }
    }

    if (Threaded)
    {
        for (RenderContext& renderContext : RenderContexts)
            ok = waitForRenderContext(renderContext) && ok;
    }

    return ok;
}

bool VulkanRenderer3D::finalizeCaptureReadback(bool blocking)
{
    if (!CaptureReadbackPending)
        return HasCpuFrame;

    bool waitOk = false;
    if (PendingCaptureReadbackContext != nullptr)
    {
        if (blocking)
            waitOk = waitForRenderContext(*PendingCaptureReadbackContext);
        else if (Device != VK_NULL_HANDLE && PendingCaptureReadbackContext->FrameFence != VK_NULL_HANDLE)
        {
            const VkResult fenceStatus = vkGetFenceStatus(Device, PendingCaptureReadbackContext->FrameFence);
            if (fenceStatus == VK_SUCCESS)
            {
                FenceWaitCpuWindow.Add(0);
                consumeGpuTiming(PendingCaptureReadbackContext);
                waitOk = true;
            }
            else if (fenceStatus == VK_NOT_READY)
            {
                return false;
            }
            else
            {
                CaptureReadbackPending = false;
                PendingCaptureReadbackContext = nullptr;
                return false;
            }
        }
        else
        {
            CaptureReadbackPending = false;
            PendingCaptureReadbackContext = nullptr;
            return false;
        }
    }
    else
    {
        if (blocking)
            waitOk = waitForReadbackSource();
        else if (Device != VK_NULL_HANDLE && FrameFence != VK_NULL_HANDLE)
        {
            const VkResult fenceStatus = vkGetFenceStatus(Device, FrameFence);
            if (fenceStatus == VK_SUCCESS)
            {
                FenceWaitCpuWindow.Add(0);
                consumeGpuTiming(nullptr);
                waitOk = true;
            }
            else if (fenceStatus == VK_NOT_READY)
            {
                return false;
            }
            else
            {
                CaptureReadbackPending = false;
                PendingCaptureReadbackContext = nullptr;
                return false;
            }
        }
        else
            return false;
    }

    if (!waitOk || ReadbackMapped == nullptr || RawReadbackWidth == 0 || RawReadbackHeight == 0)
    {
        clearRawReadbackState();
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(RawReadbackWidth) * static_cast<size_t>(RawReadbackHeight);
    if (RawReadbackRgba.size() != pixelCount)
        RawReadbackRgba.resize(pixelCount);
    std::memcpy(RawReadbackRgba.data(), ReadbackMapped, pixelCount * sizeof(u32));

    CaptureReadbackPending = false;
    PendingCaptureReadbackContext = nullptr;
    HasCpuFrame = true;
    ColorImageInitialized = true;
    return true;
}

void VulkanRenderer3D::syncActiveCaptureLineBufferSlot()
{
    CaptureLineBuffer = CaptureLineBuffers[ActiveCaptureLineBufferSlot];
    CaptureLineMemory = CaptureLineMemories[ActiveCaptureLineBufferSlot];
    CaptureLineBufferSize = CaptureLineBufferSizes[ActiveCaptureLineBufferSlot];
    CaptureLineMapped = CaptureLineMappedSlots[ActiveCaptureLineBufferSlot];
}

void VulkanRenderer3D::storeActiveCaptureLineBufferSlot()
{
    CaptureLineBuffers[ActiveCaptureLineBufferSlot] = CaptureLineBuffer;
    CaptureLineMemories[ActiveCaptureLineBufferSlot] = CaptureLineMemory;
    CaptureLineBufferSizes[ActiveCaptureLineBufferSlot] = CaptureLineBufferSize;
    CaptureLineMappedSlots[ActiveCaptureLineBufferSlot] = CaptureLineMapped;
}

void VulkanRenderer3D::selectActiveCaptureLineBufferSlot(u32 slot)
{
    slot %= CaptureLineBufferSlotCount;
    if (ActiveCaptureLineBufferSlot == slot)
        return;

    storeActiveCaptureLineBufferSlot();
    ActiveCaptureLineBufferSlot = slot;
    syncActiveCaptureLineBufferSlot();
}

void VulkanRenderer3D::resetCaptureLineState()
{
    CaptureLinePending = false;
    CaptureLineReady = false;
    CaptureLineDataIsRgba8 = false;
    PendingCaptureLineContext = nullptr;
    ReadyCaptureLineData = nullptr;
    PendingCaptureLineBufferSlot = -1;
    ReadyCaptureLineBufferSlot = -1;
    PendingCaptureLineScreenSwap = false;
    ReadyCaptureLineScreenSwap = false;
}

void VulkanRenderer3D::clearRawReadbackState()
{
    CaptureReadbackPending = false;
    PendingCaptureReadbackContext = nullptr;
    RawReadbackWidth = 0;
    RawReadbackHeight = 0;
    RawReadbackRgba.clear();
}

bool VulkanRenderer3D::finalizeCaptureLineFrame(bool blocking)
{
    if (!CaptureLinePending)
        return CaptureLineReady;

    bool waitOk = false;
    if (PendingCaptureLineContext != nullptr)
    {
        if (blocking)
            waitOk = waitForRenderContext(*PendingCaptureLineContext);
        else if (Device != VK_NULL_HANDLE && PendingCaptureLineContext->FrameFence != VK_NULL_HANDLE)
        {
            const VkResult fenceStatus = vkGetFenceStatus(Device, PendingCaptureLineContext->FrameFence);
            if (fenceStatus == VK_SUCCESS)
            {
                FenceWaitCpuWindow.Add(0);
                consumeGpuTiming(PendingCaptureLineContext);
                waitOk = true;
            }
            else if (fenceStatus == VK_NOT_READY)
            {
                return false;
            }
            else
            {
                resetCaptureLineState();
                return false;
            }
        }
        else
        {
            resetCaptureLineState();
            return false;
        }
    }
    else
    {
        if (blocking)
            waitOk = waitForReadbackSource();
        else if (Device != VK_NULL_HANDLE && FrameFence != VK_NULL_HANDLE)
        {
            const VkResult fenceStatus = vkGetFenceStatus(Device, FrameFence);
            if (fenceStatus == VK_SUCCESS)
            {
                FenceWaitCpuWindow.Add(0);
                consumeGpuTiming(nullptr);
                waitOk = true;
            }
            else if (fenceStatus == VK_NOT_READY)
            {
                return false;
            }
            else
            {
                resetCaptureLineState();
                return false;
            }
        }
        else
            return false;
    }

    if (!waitOk)
    {
        resetCaptureLineState();
        return false;
    }

    if (PendingCaptureLineContext != nullptr)
    {
        ReadyCaptureLineData = reinterpret_cast<const u32*>(PendingCaptureLineContext->CaptureLineMapped);
        ReadyCaptureLineBufferSlot = -1;
    }
    else if (PendingCaptureLineBufferSlot >= 0)
    {
        ReadyCaptureLineData = reinterpret_cast<const u32*>(
            CaptureLineMappedSlots[static_cast<size_t>(PendingCaptureLineBufferSlot)]);
        ReadyCaptureLineBufferSlot = PendingCaptureLineBufferSlot;
    }
    else
    {
        ReadyCaptureLineData = reinterpret_cast<const u32*>(CaptureLineMapped);
        ReadyCaptureLineBufferSlot = static_cast<int>(ActiveCaptureLineBufferSlot);
    }

    CaptureLinePending = false;
    PendingCaptureLineContext = nullptr;
    PendingCaptureLineBufferSlot = -1;
    ReadyCaptureLineScreenSwap = PendingCaptureLineScreenSwap;
    PendingCaptureLineScreenSwap = false;
    CaptureLineReady = ReadyCaptureLineData != nullptr;
    return CaptureLineReady;
}

bool VulkanRenderer3D::waitForDeviceIdle(const char* reason)
{
    if (Device == VK_NULL_HANDLE)
        return false;

    const VkResult waitResult = vkDeviceWaitIdle(Device);
    if (waitResult != VK_SUCCESS)
    {
        Log(
            LogLevel::Error,
            "VulkanRenderer3D: vkDeviceWaitIdle failed while waiting for %s (%d)",
            reason != nullptr ? reason : "device idle",
            static_cast<int>(waitResult)
        );
        return false;
    }

    return true;
}

bool VulkanRenderer3D::waitForQueueTail(const char* reason)
{
    if (Device == VK_NULL_HANDLE || Queue == VK_NULL_HANDLE)
        return false;

    VkFence tailFence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(Device, &fenceInfo, nullptr, &tailFence) != VK_SUCCESS)
        return false;

    VkResult submitResult = VK_ERROR_INITIALIZATION_FAILED;
    {
        std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitResult = vkQueueSubmit(Queue, 1, &submitInfo, tailFence);
    }

    const VkResult waitResult = submitResult == VK_SUCCESS
        ? vkWaitForFences(Device, 1, &tailFence, VK_TRUE, UINT64_MAX)
        : submitResult;
    vkDestroyFence(Device, tailFence, nullptr);
    if (waitResult != VK_SUCCESS)
    {
        Log(
            LogLevel::Error,
            "VulkanRenderer3D: graphics queue tail wait failed while waiting for %s (%d)",
            reason != nullptr ? reason : "queue completion",
            static_cast<int>(waitResult));
        return false;
    }
    return true;
}

void VulkanRenderer3D::TransitionImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2 dstAccess,
    VkImageAspectFlags aspectMask)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = static_cast<VkAccessFlags>(srcAccess);
    barrier.dstAccessMask = static_cast<VkAccessFlags>(dstAccess);
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        commandBuffer,
        static_cast<VkPipelineStageFlags>(srcStage),
        static_cast<VkPipelineStageFlags>(dstStage),
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
}

VulkanRenderer3D::RenderContext& VulkanRenderer3D::acquireNextRenderContext() noexcept
{
    RenderContext& renderContext = RenderContexts[NextRenderContextIndex];
    NextRenderContextIndex = (NextRenderContextIndex + 1) % AsyncRenderContextCount;
    return renderContext;
}

void VulkanRenderer3D::requestPostFastForwardDrain()
{
    if (!Threaded)
        return;

    PostFastForwardDrainFrames = static_cast<u32>(AsyncRenderContextCount * 3);
}

void VulkanRenderer3D::consumeGpuTiming(RenderContext* context)
{
    VkQueryPool queryPool = context != nullptr ? context->TimestampQueryPool : TimestampQueryPool;
    bool& timestampPending = context != nullptr ? context->TimestampPending : TimestampPending;

    if (!timestampPending || queryPool == VK_NULL_HANDLE || TimestampPeriodNs <= 0.0f)
        return;

    u64 timestamps[TimestampQueryCount]{};
    const VkResult queryResult = vkGetQueryPoolResults(
        Device,
        queryPool,
        0,
        TimestampQueryCount,
        sizeof(timestamps),
        timestamps,
        sizeof(u64),
        VK_QUERY_RESULT_64_BIT
    );
    if (queryResult == VK_SUCCESS && timestamps[TimestampQueryCount - 1] >= timestamps[0])
    {
        auto toGpuNs = [&](u32 startIndex, u32 endIndex) -> u64 {
            if (endIndex >= TimestampQueryCount || timestamps[endIndex] < timestamps[startIndex])
                return 0;
            return static_cast<u64>(static_cast<double>(timestamps[endIndex] - timestamps[startIndex]) * static_cast<double>(TimestampPeriodNs));
        };

        const u64 gpuTimeNs = toGpuNs(0, TimestampQueryCount - 1);
        GpuWindow.Add(gpuTimeNs);
        if (ActiveBackendMode == BackendMode::GraphicsHardware)
        {
            InterpGpuWindow.Add(toGpuNs(0, 3));
            BinGpuWindow.Add(toGpuNs(3, 4));
            WorkOffsetsGpuWindow.Add(0);
            SortGpuWindow.Add(0);
            RasterGpuWindow.Add(toGpuNs(0, 4));
            DepthBlendGpuWindow.Add(toGpuNs(4, 6));
            FinalGpuWindow.Add(toGpuNs(6, 7));
            CaptureLineExportGpuWindow.Add(toGpuNs(7, 8));
        }
        else
        {
            InterpGpuWindow.Add(toGpuNs(0, 1));
            BinGpuWindow.Add(toGpuNs(1, 2));
            WorkOffsetsGpuWindow.Add(toGpuNs(2, 3));
            SortGpuWindow.Add(toGpuNs(3, 4));
            RasterGpuWindow.Add(toGpuNs(4, 5));
            DepthBlendGpuWindow.Add(toGpuNs(5, 6));
            FinalGpuWindow.Add(toGpuNs(6, 7));
            CaptureLineExportGpuWindow.Add(toGpuNs(7, 8));
        }
    }

    timestampPending = false;
}

void VulkanRenderer3D::logPerformanceIfNeeded()
{
    if (!MelonDSAndroid::areRendererDebugToolsEnabled())
        return;

    if (!RenderCpuWindow.Ready())
        return;

    const PerfSampleWindow<120>::Summary renderSummary = RenderCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary waitSummary = FenceWaitCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary gpuSummary = GpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary triangleSummary = TriangleCountWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary passSummary = PassCountWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary interpCpuSummary = InterpCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary binCpuSummary = BinCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary workOffsetsCpuSummary = WorkOffsetsCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary sortCpuSummary = SortCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary rasterCpuSummary = RasterCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary graphicsSceneBuildCpuSummary = GraphicsSceneBuildCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary graphicsMainCpuSummary = GraphicsMainCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary graphicsAlphaCpuSummary = GraphicsAlphaCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary depthBlendCpuSummary = DepthBlendCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary finalCpuSummary = FinalCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary captureLineExportCpuSummary = CaptureLineExportCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary cpuActiveTileSummary = CpuActiveTileCountWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary cpuTileCountSummary = CpuTileCountWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary cpuActiveGroupSummary = CpuActiveGroupCountWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary cpuActiveDispatchSummary = CpuActiveDispatchWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary interpGpuSummary = InterpGpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary binGpuSummary = BinGpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary workOffsetsGpuSummary = WorkOffsetsGpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary sortGpuSummary = SortGpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary rasterGpuSummary = RasterGpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary depthBlendGpuSummary = DepthBlendGpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary finalGpuSummary = FinalGpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary captureLineExportGpuSummary = CaptureLineExportGpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary earlySubmitCpuSummary = EarlySubmitCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary earlySubmitWaitSummary = EarlySubmitContextWaitCpuWindow.SummarizeAndReset();
    const double cpuTileCoveragePercent = cpuTileCountSummary.MeanNs > 0
        ? (static_cast<double>(cpuActiveTileSummary.MeanNs) * 100.0 / static_cast<double>(cpuTileCountSummary.MeanNs))
        : 0.0;
    const auto pickDominantEnumIndex = [](const auto& counts, size_t fallbackIndex) -> size_t {
        size_t dominantIndex = fallbackIndex;
        u64 dominantCount = 0;
        for (size_t i = 0; i < counts.size(); i++)
        {
            if (counts[i] >= dominantCount)
            {
                dominantCount = counts[i];
                dominantIndex = i;
            }
        }
        return dominantIndex;
    };
    const RasterExecutionProfile dominantRasterProfile = static_cast<RasterExecutionProfile>(pickDominantEnumIndex(
        RasterExecutionProfileCounts,
        static_cast<size_t>(ActiveRasterExecutionProfile)
    ));
    const RasterTileLoopMode dominantTileLoopMode = static_cast<RasterTileLoopMode>(pickDominantEnumIndex(
        RasterTileLoopModeCounts,
        static_cast<size_t>(ActiveRasterTileLoopMode)
    ));
    const CapturePathMode dominantCapturePathMode = static_cast<CapturePathMode>(pickDominantEnumIndex(
        CapturePathModeCounts,
        static_cast<size_t>(ActiveCapturePathMode)
    ));
    const char* activePathName = ActiveBackendMode == BackendMode::GraphicsHardware
        ? "simple_graphics"
        : (CpuDirectTilesPathCount > 0 && DirectTilesPathCount == 0 && LegacyWorklistPathCount == 0
            ? "cpu_direct_tiles"
            : (LegacyWorklistPathCount > 0 && DirectTilesPathCount == 0 ? "legacy" : "direct_tiles"));

    if (ActiveBackendMode == BackendMode::GraphicsHardware)
    {
        Log(
            LogLevel::Warn,
            "VulkanPerf[GPU3D]: backendConfigured=%s backendActive=%s path=%s descriptorPath=%s captureSource=%s scale=%d render cpu avg=%.3fms p95=%.3fms max=%.3fms wait avg=%.3fms p95=%.3fms max=%.3fms gpu avg=%.3fms p95=%.3fms max=%.3fms triangles avg=%llu passes avg=%llu p95=%llu opaqueDraws=%u needOpaqueDraws=%u alphaShadowDraws=%u contextMisses=%llu late=%llu dropped=%llu readbackColor=%llu readbackResult=%llu capturePrepare=%llu captureEnabled=%llu captureSrc3d=%llu capMode=%llu/%llu/%llu/%llu capSize=%llu/%llu/%llu/%llu capExport=%llu capExportCpu avg=%.3fms p95=%.3fms capExportGpu avg=%.3fms p95=%.3fms earlySubmit hit=%llu/%llu miss=%llu skip215=%llu cpu avg=%.3fms p95=%.3fms wait avg=%.3fms p95=%.3fms",
            backendModeName(RequestedBackendMode),
            backendModeName(ActiveBackendMode),
            activePathName,
            textureSamplingPathName(ActiveTextureSamplingPath),
            capturePathModeName(dominantCapturePathMode),
            std::max(1, ScaleFactor),
            PerfNsToMs(renderSummary.MeanNs),
            PerfNsToMs(renderSummary.P95Ns),
            PerfNsToMs(renderSummary.MaxNs),
            PerfNsToMs(waitSummary.MeanNs),
            PerfNsToMs(waitSummary.P95Ns),
            PerfNsToMs(waitSummary.MaxNs),
            PerfNsToMs(gpuSummary.MeanNs),
            PerfNsToMs(gpuSummary.P95Ns),
            PerfNsToMs(gpuSummary.MaxNs),
            static_cast<unsigned long long>(triangleSummary.MeanNs),
            static_cast<unsigned long long>(passSummary.MeanNs),
            static_cast<unsigned long long>(passSummary.P95Ns),
            LastGraphicsOpaqueDrawCount,
            LastGraphicsNeedOpaqueDrawCount,
            LastGraphicsAlphaDrawCount,
            static_cast<unsigned long long>(ContextMissCount),
            static_cast<unsigned long long>(LateFrameCount),
            static_cast<unsigned long long>(DroppedFrameCount),
            static_cast<unsigned long long>(ReadbackColorRequestCount),
            static_cast<unsigned long long>(ReadbackResultRequestCount),
            static_cast<unsigned long long>(CapturePrepareRequestCount),
            static_cast<unsigned long long>(CaptureEnabledCount),
            static_cast<unsigned long long>(CaptureSource3dCount),
            static_cast<unsigned long long>(CaptureModeCounts[0]),
            static_cast<unsigned long long>(CaptureModeCounts[1]),
            static_cast<unsigned long long>(CaptureModeCounts[2]),
            static_cast<unsigned long long>(CaptureModeCounts[3]),
            static_cast<unsigned long long>(CaptureSizeModeCounts[0]),
            static_cast<unsigned long long>(CaptureSizeModeCounts[1]),
            static_cast<unsigned long long>(CaptureSizeModeCounts[2]),
            static_cast<unsigned long long>(CaptureSizeModeCounts[3]),
            static_cast<unsigned long long>(CaptureLineExportCount),
            PerfNsToMs(captureLineExportCpuSummary.MeanNs),
            PerfNsToMs(captureLineExportCpuSummary.P95Ns),
            PerfNsToMs(captureLineExportGpuSummary.MeanNs),
            PerfNsToMs(captureLineExportGpuSummary.P95Ns),
            static_cast<unsigned long long>(EarlySubmitHitCount),
            static_cast<unsigned long long>(EarlySubmitAttemptCount),
            static_cast<unsigned long long>(EarlySubmitMissCount),
            static_cast<unsigned long long>(EarlySubmitSkipVCount215Count),
            PerfNsToMs(earlySubmitCpuSummary.MeanNs),
            PerfNsToMs(earlySubmitCpuSummary.P95Ns),
            PerfNsToMs(earlySubmitWaitSummary.MeanNs),
            PerfNsToMs(earlySubmitWaitSummary.P95Ns)
        );
        Log(
            LogLevel::Warn,
            "VulkanPerf[GPU3DPasses]: sceneBuild cpu avg=%.3fms p95=%.3fms opaque cpu avg=%.3fms p95=%.3fms opaque gpu avg=%.3fms p95=%.3fms opaqueDraw gpu avg=%.3fms p95=%.3fms edge gpu avg=%.3fms p95=%.3fms alphaShadow cpu avg=%.3fms p95=%.3fms alphaShadow gpu avg=%.3fms p95=%.3fms final cpu avg=%.3fms p95=%.3fms final gpu avg=%.3fms p95=%.3fms captureExport cpu avg=%.3fms p95=%.3fms captureExport gpu avg=%.3fms p95=%.3fms opaqueDraws=%u needOpaqueDraws=%u alphaShadowDraws=%u opaqueW=%u opaqueZ=%u opaqueTex=%u opaqueNoTex=%u opaqueMod=%u opaqueDecal=%u opaqueToon=%u opaqueHighlight=%u opaqueLinear=%u opaqueRepeat=%u opaqueMirror=%u opaqueRepeatS=%u opaqueRepeatT=%u opaqueMirrorS=%u opaqueMirrorT=%u opaqueClampS=%u opaqueClampT=%u opaqueFullAlpha=%u highresRepeatModel=%u activeTextures=%u triangles=%zu pipelines=%u",
            PerfNsToMs(graphicsSceneBuildCpuSummary.MeanNs),
            PerfNsToMs(graphicsSceneBuildCpuSummary.P95Ns),
            PerfNsToMs(graphicsMainCpuSummary.MeanNs),
            PerfNsToMs(graphicsMainCpuSummary.P95Ns),
            PerfNsToMs(rasterGpuSummary.MeanNs),
            PerfNsToMs(rasterGpuSummary.P95Ns),
            PerfNsToMs(interpGpuSummary.MeanNs),
            PerfNsToMs(interpGpuSummary.P95Ns),
            PerfNsToMs(binGpuSummary.MeanNs),
            PerfNsToMs(binGpuSummary.P95Ns),
            PerfNsToMs(graphicsAlphaCpuSummary.MeanNs),
            PerfNsToMs(graphicsAlphaCpuSummary.P95Ns),
            PerfNsToMs(depthBlendGpuSummary.MeanNs),
            PerfNsToMs(depthBlendGpuSummary.P95Ns),
            PerfNsToMs(finalCpuSummary.MeanNs),
            PerfNsToMs(finalCpuSummary.P95Ns),
            PerfNsToMs(finalGpuSummary.MeanNs),
            PerfNsToMs(finalGpuSummary.P95Ns),
            PerfNsToMs(captureLineExportCpuSummary.MeanNs),
            PerfNsToMs(captureLineExportCpuSummary.P95Ns),
            PerfNsToMs(captureLineExportGpuSummary.MeanNs),
            PerfNsToMs(captureLineExportGpuSummary.P95Ns),
            LastGraphicsOpaqueDrawCount,
            LastGraphicsNeedOpaqueDrawCount,
            LastGraphicsAlphaDrawCount,
            LastGraphicsOpaqueWDrawCount,
            LastGraphicsOpaqueZDrawCount,
            LastGraphicsOpaqueTexturedDrawCount,
            LastGraphicsOpaqueUntexturedDrawCount,
            LastGraphicsOpaqueModulateDrawCount,
            LastGraphicsOpaqueDecalDrawCount,
            LastGraphicsOpaqueToonDrawCount,
            LastGraphicsOpaqueHighlightDrawCount,
            LastGraphicsOpaqueLinearDrawCount,
            LastGraphicsOpaqueRepeatDrawCount,
            LastGraphicsOpaqueMirrorDrawCount,
            LastGraphicsOpaqueRepeatSDrawCount,
            LastGraphicsOpaqueRepeatTDrawCount,
            LastGraphicsOpaqueMirrorSDrawCount,
            LastGraphicsOpaqueMirrorTDrawCount,
            LastGraphicsOpaqueClampSDrawCount,
            LastGraphicsOpaqueClampTDrawCount,
            LastGraphicsOpaqueFullAlphaDrawCount,
            LastGraphicsOpaqueHighresRepeatModelDrawCount,
            ActiveTextureDescriptorCount,
            Triangles.size(),
            static_cast<u32>(
                GraphicsOpaquePipelineCount
                + GraphicsOpaquePipelineCount
                + GraphicsOpaquePipelineCount
                + GraphicsOpaquePipelineCount
                + GraphicsOpaquePipelineCount
                + GraphicsOpaquePipelineCount
                + GraphicsTranslucentPipelineCount
                + GraphicsBgZeroTranslucentPipelineCount
                + GraphicsShadowMaskPipelineCount
                + GraphicsShadowMaskBgZeroPipelineCount
                + GraphicsShadowClearPipelineCount
                + GraphicsShadowBlendBgZeroPipelineCount
                + GraphicsShadowBlendPipelineCount)
        );
    }
    else
    {
        Log(
            LogLevel::Warn,
            "VulkanPerf[GPU3D]: backendConfigured=%s backendActive=%s path=%s rasterProfile=%s descriptorPath=%s tileLoopMode=%s captureSource=%s scale=%d render cpu avg=%.3fms p95=%.3fms max=%.3fms wait avg=%.3fms p95=%.3fms max=%.3fms gpu avg=%.3fms p95=%.3fms max=%.3fms triangles avg=%llu passes avg=%llu p95=%llu cpuTiles avg=%llu/%llu (%.1f%%) cpuGroups avg=%llu activeDispatch=%llu%% contextMisses=%llu late=%llu dropped=%llu readbackColor=%llu readbackResult=%llu capturePrepare=%llu captureEnabled=%llu captureSrc3d=%llu capMode=%llu/%llu/%llu/%llu capSize=%llu/%llu/%llu/%llu capExport=%llu capExportCpu avg=%.3fms p95=%.3fms capExportGpu avg=%.3fms p95=%.3fms rasterSpec tex=%llu alpha=%llu shade=%llu all=%llu earlySubmit hit=%llu/%llu miss=%llu skip215=%llu cpu avg=%.3fms p95=%.3fms wait avg=%.3fms p95=%.3fms",
            backendModeName(RequestedBackendMode),
            backendModeName(ActiveBackendMode),
            activePathName,
            rasterExecutionProfileName(dominantRasterProfile),
            textureSamplingPathName(ActiveTextureSamplingPath),
            rasterTileLoopModeName(dominantTileLoopMode),
            capturePathModeName(dominantCapturePathMode),
            std::max(1, ScaleFactor),
            PerfNsToMs(renderSummary.MeanNs),
            PerfNsToMs(renderSummary.P95Ns),
            PerfNsToMs(renderSummary.MaxNs),
            PerfNsToMs(waitSummary.MeanNs),
            PerfNsToMs(waitSummary.P95Ns),
            PerfNsToMs(waitSummary.MaxNs),
            PerfNsToMs(gpuSummary.MeanNs),
            PerfNsToMs(gpuSummary.P95Ns),
            PerfNsToMs(gpuSummary.MaxNs),
            static_cast<unsigned long long>(triangleSummary.MeanNs),
            static_cast<unsigned long long>(passSummary.MeanNs),
            static_cast<unsigned long long>(passSummary.P95Ns),
            static_cast<unsigned long long>(cpuActiveTileSummary.MeanNs),
            static_cast<unsigned long long>(cpuTileCountSummary.MeanNs),
            cpuTileCoveragePercent,
            static_cast<unsigned long long>(cpuActiveGroupSummary.MeanNs),
            static_cast<unsigned long long>(cpuActiveDispatchSummary.MeanNs),
            static_cast<unsigned long long>(ContextMissCount),
            static_cast<unsigned long long>(LateFrameCount),
            static_cast<unsigned long long>(DroppedFrameCount),
            static_cast<unsigned long long>(ReadbackColorRequestCount),
            static_cast<unsigned long long>(ReadbackResultRequestCount),
            static_cast<unsigned long long>(CapturePrepareRequestCount),
            static_cast<unsigned long long>(CaptureEnabledCount),
            static_cast<unsigned long long>(CaptureSource3dCount),
            static_cast<unsigned long long>(CaptureModeCounts[0]),
            static_cast<unsigned long long>(CaptureModeCounts[1]),
            static_cast<unsigned long long>(CaptureModeCounts[2]),
            static_cast<unsigned long long>(CaptureModeCounts[3]),
            static_cast<unsigned long long>(CaptureSizeModeCounts[0]),
            static_cast<unsigned long long>(CaptureSizeModeCounts[1]),
            static_cast<unsigned long long>(CaptureSizeModeCounts[2]),
            static_cast<unsigned long long>(CaptureSizeModeCounts[3]),
            static_cast<unsigned long long>(CaptureLineExportCount),
            PerfNsToMs(captureLineExportCpuSummary.MeanNs),
            PerfNsToMs(captureLineExportCpuSummary.P95Ns),
            PerfNsToMs(captureLineExportGpuSummary.MeanNs),
            PerfNsToMs(captureLineExportGpuSummary.P95Ns),
            static_cast<unsigned long long>(RasterSpecializedTextureModeCount),
            static_cast<unsigned long long>(RasterSpecializedTranslucencyModeCount),
            static_cast<unsigned long long>(RasterSpecializedShadeModeCount),
            static_cast<unsigned long long>(RasterSpecializedAllModesCount),
            static_cast<unsigned long long>(EarlySubmitHitCount),
            static_cast<unsigned long long>(EarlySubmitAttemptCount),
            static_cast<unsigned long long>(EarlySubmitMissCount),
            static_cast<unsigned long long>(EarlySubmitSkipVCount215Count),
            PerfNsToMs(earlySubmitCpuSummary.MeanNs),
            PerfNsToMs(earlySubmitCpuSummary.P95Ns),
            PerfNsToMs(earlySubmitWaitSummary.MeanNs),
            PerfNsToMs(earlySubmitWaitSummary.P95Ns)
        );
        Log(
            LogLevel::Warn,
            "VulkanPerf[GPU3DPasses]: interp cpu avg=%.3fms gpu avg=%.3fms bin cpu avg=%.3fms gpu avg=%.3fms work cpu avg=%.3fms gpu avg=%.3fms sort cpu avg=%.3fms gpu avg=%.3fms raster cpu avg=%.3fms gpu avg=%.3fms resolve cpu avg=%.3fms gpu avg=%.3fms final cpu avg=%.3fms gpu avg=%.3fms captureExport cpu avg=%.3fms gpu avg=%.3fms",
            PerfNsToMs(interpCpuSummary.MeanNs),
            PerfNsToMs(interpGpuSummary.MeanNs),
            PerfNsToMs(binCpuSummary.MeanNs),
            PerfNsToMs(binGpuSummary.MeanNs),
            PerfNsToMs(workOffsetsCpuSummary.MeanNs),
            PerfNsToMs(workOffsetsGpuSummary.MeanNs),
            PerfNsToMs(sortCpuSummary.MeanNs),
            PerfNsToMs(sortGpuSummary.MeanNs),
            PerfNsToMs(rasterCpuSummary.MeanNs),
            PerfNsToMs(rasterGpuSummary.MeanNs),
            PerfNsToMs(depthBlendCpuSummary.MeanNs),
            PerfNsToMs(depthBlendGpuSummary.MeanNs),
            PerfNsToMs(finalCpuSummary.MeanNs),
            PerfNsToMs(finalGpuSummary.MeanNs),
            PerfNsToMs(captureLineExportCpuSummary.MeanNs),
            PerfNsToMs(captureLineExportGpuSummary.MeanNs)
        );
    }
    ContextMissCount = 0;
    LateFrameCount = 0;
    DroppedFrameCount = 0;
    CpuDirectTilesPathCount = 0;
    DirectTilesPathCount = 0;
    LegacyWorklistPathCount = 0;
    ReadbackColorRequestCount = 0;
    ReadbackResultRequestCount = 0;
    CapturePrepareRequestCount = 0;
    CaptureEnabledCount = 0;
    CaptureSource3dCount = 0;
    CaptureModeCounts.fill(0);
    CaptureSizeModeCounts.fill(0);
    RasterExecutionProfileCounts.fill(0);
    RasterTileLoopModeCounts.fill(0);
    CapturePathModeCounts.fill(0);
    CaptureLineExportCount = 0;
    RasterSpecializedShadeModeCount = 0;
    RasterSpecializedTextureModeCount = 0;
    RasterSpecializedTranslucencyModeCount = 0;
    RasterSpecializedAllModesCount = 0;
    EarlySubmitAttemptCount = 0;
    EarlySubmitHitCount = 0;
    EarlySubmitMissCount = 0;
    EarlySubmitSkipVCount215Count = 0;
}

bool VulkanRenderer3D::useCpuTileBinning() const noexcept
{
    return CpuTileBinningEnabled;
}

bool VulkanRenderer3D::prepareCpuTileBins(RenderContext& context, const RasterPushConstants& pushConstants)
{
    if (context.SpanSetupMapped == nullptr
        || context.BinMaskMapped == nullptr
        || context.GroupListMapped == nullptr
        || context.WorkOffsetMapped == nullptr
        || pushConstants.width == 0
        || pushConstants.height == 0)
    {
        return false;
    }

    constexpr u32 kTileSize = 8u;
    const u32 tilesPerLine = (pushConstants.width + (kTileSize - 1u)) / kTileSize;
    const u32 tileLineCount = (pushConstants.height + (kTileSize - 1u)) / kTileSize;
    const u32 tileCount = std::max<u32>(1u, tilesPerLine * tileLineCount);
    const u32 triangleBase = std::min<u32>(pushConstants.triangleBase, static_cast<u32>(Triangles.size()));
    const u32 triangleCount = std::min<u32>(
        pushConstants.triangleCount,
        static_cast<u32>(Triangles.size()) - triangleBase
    );
    const u32 groupCount = std::max<u32>(1u, (triangleCount + 31u) / 32u);
    const size_t requiredSpanSetupCount = std::max<size_t>(1u, static_cast<size_t>(triangleCount));
    const size_t requiredBinMaskWords = static_cast<size_t>(tileCount) * static_cast<size_t>(groupCount);
    const size_t requiredGroupListWords = static_cast<size_t>(tileCount) * static_cast<size_t>(groupCount + 1u);
    const u32 maxGroupListEntries = tileCount * groupCount;
    const u32 requiredWorkOffsetWords = kWorkTileOffsetsBase + (tileCount + 1u) + tileCount + maxGroupListEntries;

    if (context.SpanSetupBufferSize < static_cast<VkDeviceSize>(requiredSpanSetupCount * sizeof(SpanSetupGpu))
        || context.BinMaskBufferSize < static_cast<VkDeviceSize>(requiredBinMaskWords * sizeof(u32))
        || context.GroupListBufferSize < static_cast<VkDeviceSize>(requiredGroupListWords * sizeof(u32))
        || context.WorkOffsetBufferSize < static_cast<VkDeviceSize>(requiredWorkOffsetWords * sizeof(u32)))
    {
        return false;
    }

    auto* spanSetups = reinterpret_cast<SpanSetupGpu*>(context.SpanSetupMapped);
    auto* binMaskValues = reinterpret_cast<u32*>(context.BinMaskMapped);
    auto* groupListValues = reinterpret_cast<u32*>(context.GroupListMapped);
    auto* workOffsetValues = reinterpret_cast<u32*>(context.WorkOffsetMapped);
    std::memset(binMaskValues, 0, requiredBinMaskWords * sizeof(u32));
    std::memset(groupListValues, 0, static_cast<size_t>(tileCount) * sizeof(u32));
    std::memset(workOffsetValues, 0, static_cast<size_t>(requiredWorkOffsetWords) * sizeof(u32));

    const auto variantMatchesPass = [](u32 triangleVariantKey, u32 passVariantKey) noexcept -> bool {
        constexpr u32 kVariantWildcard = 0xFFFFFFFFu;
        constexpr u32 kVariantPipelineMask = (1u << 8u) - 1u;
        if (passVariantKey == kVariantWildcard)
            return true;
        return (triangleVariantKey & kVariantPipelineMask) == (passVariantKey & kVariantPipelineMask);
    };

    for (u32 localTriangleIdx = 0; localTriangleIdx < triangleCount; localTriangleIdx++)
    {
        const u32 triangleIdx = triangleBase + localTriangleIdx;
        const TriangleGpu& tri = Triangles[triangleIdx];
        SpanSetupGpu& span = spanSetups[localTriangleIdx];
        const float p0x = tri.x0;
        const float p0y = tri.y0;
        const float p1x = tri.x1;
        const float p1y = tri.y1;
        const float p2x = tri.x2;
        const float p2y = tri.y2;

        float minX = std::min(std::min(p0x, p1x), p2x);
        float minY = std::min(std::min(p0y, p1y), p2y);
        float maxX = std::max(std::max(p0x, p1x), p2x);
        float maxY = std::max(std::max(p0y, p1y), p2y);
        minX = std::clamp(minX, -1.0f, static_cast<float>(pushConstants.width) + 1.0f);
        maxX = std::clamp(maxX, -1.0f, static_cast<float>(pushConstants.width) + 1.0f);
        minY = std::clamp(minY, -1.0f, static_cast<float>(pushConstants.height) + 1.0f);
        maxY = std::clamp(maxY, -1.0f, static_cast<float>(pushConstants.height) + 1.0f);

        const u32 boundsYMin = tri.yBounds & 0xFFFFu;
        const u32 boundsYMax = (tri.yBounds >> 16u) & 0xFFFFu;
        u32 geomYMin = static_cast<u32>(std::max(0, static_cast<int>(std::floor(minY))));
        u32 geomYMax = static_cast<u32>(std::max(0, static_cast<int>(std::ceil(maxY))));
        geomYMin = std::min(geomYMin, pushConstants.height);
        geomYMax = std::min(geomYMax, pushConstants.height);

        const u32 yMin = std::max(boundsYMin, geomYMin);
        const u32 yMax = std::min(boundsYMax, geomYMax);
        span.minX = minX;
        span.minY = minY;
        span.maxX = maxX;
        span.maxY = maxY;
        span.yMin = yMin;
        span.yMax = yMax;
        span.variantKey = tri.variantKey & ((1u << 8u) - 1u);
        span.valid = variantMatchesPass(tri.variantKey, pushConstants.variantKey) ? 1u : 0u;
        if (maxX < minX || maxY < minY || yMax <= yMin)
            span.valid = 0u;
        if (span.valid == 0u)
            continue;

        const float triangleArea = edgeFunction2D(p0x, p0y, p1x, p1y, p2x, p2y);
        if (std::abs(triangleArea) < kTriangleAreaEpsilon)
        {
            span.valid = 0u;
            continue;
        }
        const bool positiveArea = triangleArea > 0.0f;
        const float edge0dx = p2x - p1x;
        const float edge0dy = p2y - p1y;
        const float edge1dx = p0x - p2x;
        const float edge1dy = p0y - p2y;
        const float edge2dx = p1x - p0x;
        const float edge2dy = p1y - p0y;
        const float edge0LengthSquared = edge0dx * edge0dx + edge0dy * edge0dy;
        const float edge1LengthSquared = edge1dx * edge1dx + edge1dy * edge1dy;
        const float edge2LengthSquared = edge2dx * edge2dx + edge2dy * edge2dy;
        span.edgeInv0 = edge0LengthSquared > 1e-12f ? 1.0f / std::sqrt(edge0LengthSquared) : 0.0f;
        span.edgeInv1 = edge1LengthSquared > 1e-12f ? 1.0f / std::sqrt(edge1LengthSquared) : 0.0f;
        span.edgeInv2 = edge2LengthSquared > 1e-12f ? 1.0f / std::sqrt(edge2LengthSquared) : 0.0f;

        const int minTileX = std::clamp(static_cast<int>(std::floor(minX / static_cast<float>(kTileSize))), 0, static_cast<int>(tilesPerLine) - 1);
        const int maxTileX = std::clamp(static_cast<int>(std::floor(maxX / static_cast<float>(kTileSize))), 0, static_cast<int>(tilesPerLine) - 1);
        const int minTileY = std::clamp(static_cast<int>(yMin / kTileSize), 0, static_cast<int>(tileLineCount) - 1);
        const int maxTileY = std::clamp(static_cast<int>((yMax - 1u) / kTileSize), 0, static_cast<int>(tileLineCount) - 1);

        const EdgeEquation2D edge0 = makeEdgeEquation2D(p0x, p0y, p1x, p1y);
        const EdgeEquation2D edge1 = makeEdgeEquation2D(p1x, p1y, p2x, p2y);
        const EdgeEquation2D edge2 = makeEdgeEquation2D(p2x, p2y, p0x, p0y);
        constexpr float kTileSizeF = static_cast<float>(kTileSize);
        const float frameWidth = static_cast<float>(pushConstants.width);
        const float frameHeight = static_cast<float>(pushConstants.height);
        const float startTileMinX = static_cast<float>(minTileX * static_cast<int>(kTileSize));
        const float edge0StepX = edge0.a * kTileSizeF;
        const float edge1StepX = edge1.a * kTileSizeF;
        const float edge2StepX = edge2.a * kTileSizeF;
        const u32 groupIdx = localTriangleIdx / 32u;
        const u32 groupBit = 1u << (localTriangleIdx & 31u);
        for (int tileY = minTileY; tileY <= maxTileY; tileY++)
        {
            const size_t rowTileBase = static_cast<size_t>(tileY) * static_cast<size_t>(tilesPerLine);
            const float tileMinY = static_cast<float>(tileY * static_cast<int>(kTileSize));
            const float tileMaxY = std::min(tileMinY + kTileSizeF, frameHeight);
            const float tileHeight = tileMaxY - tileMinY;
            float tileMinX = startTileMinX;
            float edge0MinMin = evaluateEdge2D(edge0, tileMinX, tileMinY);
            float edge0MinMax = edge0MinMin + edge0.b * tileHeight;
            float edge1MinMin = evaluateEdge2D(edge1, tileMinX, tileMinY);
            float edge1MinMax = edge1MinMin + edge1.b * tileHeight;
            float edge2MinMin = evaluateEdge2D(edge2, tileMinX, tileMinY);
            float edge2MinMax = edge2MinMin + edge2.b * tileHeight;
            for (int tileX = minTileX; tileX <= maxTileX; tileX++)
            {
                const float tileMaxX = std::min(tileMinX + kTileSizeF, frameWidth);
                const float tileWidth = tileMaxX - tileMinX;
                const float edge0MaxMin = edge0MinMin + edge0.a * tileWidth;
                const float edge0MaxMax = edge0MinMax + edge0.a * tileWidth;
                const float edge1MaxMin = edge1MinMin + edge1.a * tileWidth;
                const float edge1MaxMax = edge1MinMax + edge1.a * tileWidth;
                const float edge2MaxMin = edge2MinMin + edge2.a * tileWidth;
                const float edge2MaxMax = edge2MinMax + edge2.a * tileWidth;
                if (edgeSeparatesTile(edge0MinMin, edge0MaxMin, edge0MinMax, edge0MaxMax, positiveArea)
                    || edgeSeparatesTile(edge1MinMin, edge1MaxMin, edge1MinMax, edge1MaxMax, positiveArea)
                    || edgeSeparatesTile(edge2MinMin, edge2MaxMin, edge2MinMax, edge2MaxMax, positiveArea))
                {
                    tileMinX += kTileSizeF;
                    edge0MinMin += edge0StepX;
                    edge0MinMax += edge0StepX;
                    edge1MinMin += edge1StepX;
                    edge1MinMax += edge1StepX;
                    edge2MinMin += edge2StepX;
                    edge2MinMax += edge2StepX;
                    continue;
                }

                const size_t linearTile = rowTileBase + static_cast<size_t>(tileX);
                const size_t maskIndex = linearTile * static_cast<size_t>(groupCount) + static_cast<size_t>(groupIdx);
                const u32 previousMask = binMaskValues[maskIndex];
                binMaskValues[maskIndex] = previousMask | groupBit;
                if (previousMask == 0u)
                {
                    const u32 slot = groupListValues[linearTile]++;
                    if (slot < groupCount)
                    {
                        const size_t listIndex =
                            static_cast<size_t>(tileCount)
                            + linearTile * static_cast<size_t>(groupCount)
                            + static_cast<size_t>(slot);
                        groupListValues[listIndex] = groupIdx;
                    }
                }

                tileMinX += kTileSizeF;
                edge0MinMin += edge0StepX;
                edge0MinMax += edge0StepX;
                edge1MinMin += edge1StepX;
                edge1MinMax += edge1StepX;
                edge2MinMin += edge2StepX;
                edge2MinMax += edge2StepX;
            }
        }
    }

    const size_t tileGroupListBase = static_cast<size_t>(tileCount);
    const size_t compactGroupListBase = static_cast<size_t>(kWorkTileOffsetsBase)
        + static_cast<size_t>(tileCount + 1u)
        + static_cast<size_t>(tileCount);
    u32 runningGroupOffset = 0u;
    u32 activeTileCount = 0u;
    for (u32 linearTile = 0; linearTile < tileCount; linearTile++)
    {
        workOffsetValues[kWorkTileOffsetsBase + linearTile] = runningGroupOffset;

        const u32 tileGroupCount = std::min(groupListValues[linearTile], groupCount);
        if (tileGroupCount == 0u)
            continue;

        runningGroupOffset += tileGroupCount;
        activeTileCount++;
    }
    workOffsetValues[kWorkTileOffsetsBase + tileCount] = runningGroupOffset;

    {
        u32 compactGroupOffset = 0u;
        u32 compactActiveTileIndex = 0u;
        for (u32 linearTile = 0; linearTile < tileCount; linearTile++)
        {
            const u32 tileGroupCount = std::min(groupListValues[linearTile], groupCount);
            if (tileGroupCount == 0u)
                continue;

            workOffsetValues[kWorkTileOffsetsBase + tileCount + 1u + compactActiveTileIndex] = linearTile;
            const size_t sourceOffset = tileGroupListBase + static_cast<size_t>(linearTile) * static_cast<size_t>(groupCount);
            const size_t destinationOffset = compactGroupListBase + static_cast<size_t>(compactGroupOffset);
            std::memcpy(
                &workOffsetValues[destinationOffset],
                &groupListValues[sourceOffset],
                static_cast<size_t>(tileGroupCount) * sizeof(u32));
            compactGroupOffset += tileGroupCount;
            compactActiveTileIndex++;
        }
    }

    workOffsetValues[kWorkSortDispatchX] = std::max<u32>(1u, (activeTileCount + 63u) / 64u);
    workOffsetValues[kWorkSortDispatchY] = 1u;
    workOffsetValues[kWorkSortDispatchZ] = 1u;
    workOffsetValues[kWorkRasterDispatchX] = std::max<u32>(1u, activeTileCount);
    workOffsetValues[kWorkRasterDispatchY] = 1u;
    workOffsetValues[kWorkRasterDispatchZ] = 1u;
    workOffsetValues[kWorkActiveTileCount] = activeTileCount;
    workOffsetValues[kWorkActiveGroupCount] = runningGroupOffset;

    return true;
}

bool VulkanRenderer3D::createDescriptorObjects()
{
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(PhysicalDevice, &deviceProperties);
    const u32 textureDescriptorBindingCount = getTextureBindingDescriptorCount();
    if (deviceProperties.limits.maxPerStageDescriptorSampledImages < textureDescriptorBindingCount
        || deviceProperties.limits.maxDescriptorSetSampledImages < textureDescriptorBindingCount)
    {
        Log(
            LogLevel::Error,
            "VulkanRenderer3D: descriptor limits too low (per-stage=%u, set=%u, required=%u, path=%s)",
            deviceProperties.limits.maxPerStageDescriptorSampledImages,
            deviceProperties.limits.maxDescriptorSetSampledImages,
            textureDescriptorBindingCount,
            textureSamplingPathName(ActiveTextureSamplingPath)
        );
        return false;
    }

    VkDescriptorSetLayoutBinding imageBinding{};
    imageBinding.binding = 0;
    imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    imageBinding.descriptorCount = 1;
    imageBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding triangleBinding{};
    triangleBinding.binding = 1;
    triangleBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    triangleBinding.descriptorCount = 1;
    triangleBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding textureBinding{};
    textureBinding.binding = 2;
    textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureBinding.descriptorCount = textureDescriptorBindingCount;
    textureBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding resultBinding{};
    resultBinding.binding = 3;
    resultBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    resultBinding.descriptorCount = 1;
    resultBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding binMaskBinding{};
    binMaskBinding.binding = 4;
    binMaskBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binMaskBinding.descriptorCount = 1;
    binMaskBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding groupListBinding{};
    groupListBinding.binding = 5;
    groupListBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    groupListBinding.descriptorCount = 1;
    groupListBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding toonBinding{};
    toonBinding.binding = 6;
    toonBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    toonBinding.descriptorCount = 1;
    toonBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding spanSetupBinding{};
    spanSetupBinding.binding = 7;
    spanSetupBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    spanSetupBinding.descriptorCount = 1;
    spanSetupBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding workOffsetBinding{};
    workOffsetBinding.binding = 8;
    workOffsetBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    workOffsetBinding.descriptorCount = 1;
    workOffsetBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding captureLineBinding{};
    captureLineBinding.binding = 9;
    captureLineBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    captureLineBinding.descriptorCount = 1;
    captureLineBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::array<VkDescriptorSetLayoutBinding, 10> bindings = {
        imageBinding,
        triangleBinding,
        textureBinding,
        resultBinding,
        binMaskBinding,
        groupListBinding,
        toonBinding,
        spanSetupBinding,
        workOffsetBinding,
        captureLineBinding};

    VkDescriptorSetLayoutCreateInfo layoutCreateInfo{};
    layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCreateInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutCreateInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(Device, &layoutCreateInfo, nullptr, &DescriptorSetLayout) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create descriptor set layout");
        return false;
    }

    const bool singleTexturePath = usesSingleDescriptorTexturePath();
    const u32 descriptorSetsPerContext = singleTexturePath ? MaxTextureDescriptors : 1u;
    const u32 descriptorSetCount = static_cast<u32>((AsyncRenderContextCount + 1u) * descriptorSetsPerContext);
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = descriptorSetCount;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 8u * descriptorSetCount;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].descriptorCount = textureDescriptorBindingCount * descriptorSetCount;

    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.maxSets = descriptorSetCount;
    poolCreateInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolCreateInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(Device, &poolCreateInfo, nullptr, &DescriptorPool) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create descriptor pool");
        return false;
    }

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts(descriptorSetCount, DescriptorSetLayout);
    std::vector<VkDescriptorSet> descriptorSets(descriptorSetCount, VK_NULL_HANDLE);

    VkDescriptorSetAllocateInfo descriptorAllocInfo{};
    descriptorAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAllocInfo.descriptorPool = DescriptorPool;
    descriptorAllocInfo.descriptorSetCount = descriptorSetCount;
    descriptorAllocInfo.pSetLayouts = descriptorSetLayouts.data();

    if (vkAllocateDescriptorSets(Device, &descriptorAllocInfo, descriptorSets.data()) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate descriptor set");
        return false;
    }

    DescriptorSet = VK_NULL_HANDLE;
    SingleTextureDescriptorSets.fill(VK_NULL_HANDLE);
    for (RenderContext& renderContext : RenderContexts)
    {
        renderContext.DescriptorSet = VK_NULL_HANDLE;
        renderContext.SingleTextureDescriptorSets.fill(VK_NULL_HANDLE);
    }

    size_t descriptorCursor = 0;
    if (singleTexturePath)
    {
        for (u32 descriptorIndex = 0; descriptorIndex < MaxTextureDescriptors; descriptorIndex++)
            SingleTextureDescriptorSets[descriptorIndex] = descriptorSets[descriptorCursor++];
        DescriptorSet = SingleTextureDescriptorSets[FallbackTextureDescriptorIndex];

        for (RenderContext& renderContext : RenderContexts)
        {
            for (u32 descriptorIndex = 0; descriptorIndex < MaxTextureDescriptors; descriptorIndex++)
                renderContext.SingleTextureDescriptorSets[descriptorIndex] = descriptorSets[descriptorCursor++];
            renderContext.DescriptorSet = renderContext.SingleTextureDescriptorSets[FallbackTextureDescriptorIndex];
        }
    }
    else
    {
        DescriptorSet = descriptorSets[descriptorCursor++];
        for (RenderContext& renderContext : RenderContexts)
            renderContext.DescriptorSet = descriptorSets[descriptorCursor++];
    }

    if (descriptorCursor != descriptorSets.size())
    {
        Log(LogLevel::Error, "VulkanRenderer3D: descriptor set allocation mismatch");
        return false;
    }

    invalidateAllDescriptorSetCaches();

    if (!createFallbackTexture())
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create fallback texture");
        return false;
    }

    return true;
}

bool VulkanRenderer3D::createGraphicsDescriptorObjects()
{
    const u32 textureDescriptorBindingCount = getTextureBindingDescriptorCount();

    VkDescriptorSetLayoutBinding triangleBinding{};
    triangleBinding.binding = 0;
    triangleBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    triangleBinding.descriptorCount = 1;
    triangleBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding textureBinding{};
    textureBinding.binding = 1;
    textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureBinding.descriptorCount = textureDescriptorBindingCount;
    textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding toonBinding{};
    toonBinding.binding = 2;
    toonBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    toonBinding.descriptorCount = 1;
    toonBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding attrBinding{};
    attrBinding.binding = 3;
    attrBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    attrBinding.descriptorCount = 1;
    attrBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding = 4;
    depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding clearBinding{};
    clearBinding.binding = 5;
    clearBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    clearBinding.descriptorCount = 1;
    clearBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 6> bindings = {
        triangleBinding,
        textureBinding,
        toonBinding,
        attrBinding,
        depthBinding,
        clearBinding,
    };

    VkDescriptorSetLayoutCreateInfo layoutCreateInfo{};
    layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCreateInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutCreateInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(Device, &layoutCreateInfo, nullptr, &GraphicsDescriptorSetLayout) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics descriptor set layout");
        return false;
    }

    const u32 descriptorSetCount = static_cast<u32>(AsyncRenderContextCount + 1u);
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 3u * descriptorSetCount;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = (textureDescriptorBindingCount + 2u) * descriptorSetCount;

    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.maxSets = descriptorSetCount;
    poolCreateInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolCreateInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(Device, &poolCreateInfo, nullptr, &GraphicsDescriptorPool) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics descriptor pool");
        return false;
    }

    std::vector<VkDescriptorSetLayout> setLayouts(descriptorSetCount, GraphicsDescriptorSetLayout);
    std::vector<VkDescriptorSet> descriptorSets(descriptorSetCount, VK_NULL_HANDLE);

    VkDescriptorSetAllocateInfo descriptorAllocInfo{};
    descriptorAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAllocInfo.descriptorPool = GraphicsDescriptorPool;
    descriptorAllocInfo.descriptorSetCount = descriptorSetCount;
    descriptorAllocInfo.pSetLayouts = setLayouts.data();

    if (vkAllocateDescriptorSets(Device, &descriptorAllocInfo, descriptorSets.data()) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate graphics descriptor sets");
        return false;
    }

    size_t cursor = 0;
    GraphicsDescriptorSet = descriptorSets[cursor++];
    for (RenderContext& renderContext : RenderContexts)
        renderContext.GraphicsDescriptorSet = descriptorSets[cursor++];

    invalidateAllGraphicsDescriptorSetCaches();
    return true;
}

bool VulkanRenderer3D::selectGraphicsDepthStencilFormat()
{
    const std::array<VkFormat, 3> candidates = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
    };

    for (const VkFormat candidate : candidates)
    {
        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(PhysicalDevice, candidate, &formatProperties);
        constexpr VkFormatFeatureFlags requiredFeatures =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
            | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT
            | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((formatProperties.optimalTilingFeatures & requiredFeatures) == requiredFeatures)
        {
            GraphicsDepthStencilFormat = candidate;
            return true;
        }
    }

    GraphicsDepthStencilFormat = VK_FORMAT_UNDEFINED;
    return false;
}

std::string VulkanRenderer3D::buildPipelineCacheFileName(TextureSamplingPath samplingPath) const
{
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(PhysicalDevice, &deviceProperties);

    const u64 versionHash = fnv1a64(MELONDS_VERSION);
    const char* samplingPathSuffix = textureSamplingPathName(samplingPath);
    char cacheFileName[256]{};
    std::snprintf(
        cacheFileName,
        sizeof(cacheFileName),
        "vulkan_pipeline_cache_v%u_%08x_%08x_%08x_%016llx_%s.bin",
        kPipelineCacheFileVersion,
        deviceProperties.vendorID,
        deviceProperties.deviceID,
        deviceProperties.driverVersion,
        static_cast<unsigned long long>(versionHash),
        samplingPathSuffix
    );
    return cacheFileName;
}

bool VulkanRenderer3D::createPipelineCache(TextureSamplingPath samplingPath)
{
    if (ComputePipelineCache != VK_NULL_HANDLE)
        return true;

    ComputePipelineCacheFile = buildPipelineCacheFileName(samplingPath);

    std::vector<u8> cacheData;
    if (Platform::FileHandle* cacheFile = Platform::OpenLocalFile(ComputePipelineCacheFile, Platform::FileMode::Read))
    {
        const u64 cacheSize = Platform::FileLength(cacheFile);
        if (cacheSize > 0 && cacheSize <= (256ull * 1024ull * 1024ull))
        {
            cacheData.resize(static_cast<size_t>(cacheSize));
            if (Platform::FileRead(cacheData.data(), 1, cacheSize, cacheFile) != cacheSize)
            {
                Log(LogLevel::Warn, "VulkanRenderer3D: failed to read pipeline cache %s", ComputePipelineCacheFile.c_str());
                cacheData.clear();
            }
            else
            {
                Log(
                    LogLevel::Info,
                    "VulkanRenderer3D: loaded pipeline cache (%s, %llu bytes)",
                    ComputePipelineCacheFile.c_str(),
                    static_cast<unsigned long long>(cacheSize)
                );
            }
        }

        Platform::CloseFile(cacheFile);
    }

    VkPipelineCacheCreateInfo cacheCreateInfo{};
    cacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheCreateInfo.initialDataSize = cacheData.size();
    cacheCreateInfo.pInitialData = cacheData.empty() ? nullptr : cacheData.data();

    VkResult cacheResult = vkCreatePipelineCache(Device, &cacheCreateInfo, nullptr, &ComputePipelineCache);
    if (cacheResult != VK_SUCCESS && !cacheData.empty())
    {
        Log(
            LogLevel::Warn,
            "VulkanRenderer3D: pipeline cache data rejected, recreating empty cache (%d)",
            static_cast<int>(cacheResult)
        );
        cacheCreateInfo.initialDataSize = 0;
        cacheCreateInfo.pInitialData = nullptr;
        cacheResult = vkCreatePipelineCache(Device, &cacheCreateInfo, nullptr, &ComputePipelineCache);
    }

    if (cacheResult != VK_SUCCESS)
    {
        Log(LogLevel::Warn, "VulkanRenderer3D: failed to create pipeline cache (%d)", static_cast<int>(cacheResult));
        ComputePipelineCache = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void VulkanRenderer3D::savePipelineCache()
{
    if (Device == VK_NULL_HANDLE || ComputePipelineCache == VK_NULL_HANDLE || ComputePipelineCacheFile.empty())
        return;

    size_t cacheSize = 0;
    if (vkGetPipelineCacheData(Device, ComputePipelineCache, &cacheSize, nullptr) != VK_SUCCESS || cacheSize == 0)
        return;

    std::vector<u8> cacheData(cacheSize);
    if (vkGetPipelineCacheData(Device, ComputePipelineCache, &cacheSize, cacheData.data()) != VK_SUCCESS || cacheSize == 0)
        return;

    Platform::FileHandle* cacheFile = Platform::OpenLocalFile(ComputePipelineCacheFile, Platform::FileMode::ReadWrite);
    if (cacheFile == nullptr)
    {
        Log(LogLevel::Warn, "VulkanRenderer3D: failed to open pipeline cache for writing (%s)", ComputePipelineCacheFile.c_str());
        return;
    }

    const u64 written = Platform::FileWrite(cacheData.data(), 1, cacheSize, cacheFile);
    Platform::FileFlush(cacheFile);
    Platform::CloseFile(cacheFile);

    if (written != cacheSize)
    {
        Log(
            LogLevel::Warn,
            "VulkanRenderer3D: incomplete pipeline cache write (%s %llu/%llu)",
            ComputePipelineCacheFile.c_str(),
            static_cast<unsigned long long>(written),
            static_cast<unsigned long long>(cacheSize)
        );
        return;
    }

    Log(
        LogLevel::Info,
        "VulkanRenderer3D: saved pipeline cache (%s, %llu bytes)",
        ComputePipelineCacheFile.c_str(),
        static_cast<unsigned long long>(cacheSize)
    );
}

bool VulkanRenderer3D::createComputePipeline()
{
    if (melonDS_gpu3d_vulkan_interp_spans_comp_spv_len == 0
        || melonDS_gpu3d_vulkan_bin_combined_comp_spv_len == 0
        || melonDS_gpu3d_vulkan_calc_work_offsets_comp_spv_len == 0
        || melonDS_gpu3d_vulkan_sort_work_comp_spv_len == 0
        || melonDS_gpu3d_vulkan_tri_raster_comp_spv_len == 0
        || melonDS_gpu3d_vulkan_tri_raster_base_comp_spv_len == 0
        || melonDS_gpu3d_vulkan_tri_raster_compat_comp_spv_len == 0
        || melonDS_gpu3d_vulkan_depth_blend_comp_spv_len == 0
        || melonDS_gpu3d_vulkan_final_pass_comp_spv_len == 0
        || melonDS_gpu3d_vulkan_capture_line_export_comp_spv_len == 0)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: empty SPIR-V blob(s)");
        return false;
    }

    const TextureSamplingPath samplingPath = ActiveTextureSamplingPath;
    if (!createPipelineCache(samplingPath))
    {
        Log(
            LogLevel::Warn,
            "VulkanRenderer3D: pipeline cache unavailable, continuing without persistent cache"
        );
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(RasterPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &DescriptorSetLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(Device, &pipelineLayoutCreateInfo, nullptr, &PipelineLayout) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create pipeline layout");
        return false;
    }

    const auto createPipelineFromSpirv = [&](const u32* spirvWords,
                                             size_t spirvLength,
                                             const char* pipelineName,
                                             const VkSpecializationInfo* specializationInfo,
                                             VkPipeline* outPipeline) -> bool {
        VkShaderModuleCreateInfo shaderCreateInfo{};
        shaderCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderCreateInfo.codeSize = spirvLength;
        shaderCreateInfo.pCode = spirvWords;

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(Device, &shaderCreateInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: failed to create %s shader module", pipelineName);
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStage{};
        shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStage.module = shaderModule;
        shaderStage.pName = "main";
        shaderStage.pSpecializationInfo = specializationInfo;

        VkComputePipelineCreateInfo pipelineCreateInfo{};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = shaderStage;
        pipelineCreateInfo.layout = PipelineLayout;

        const VkResult pipelineResult = vkCreateComputePipelines(
            Device,
            ComputePipelineCache,
            1,
            &pipelineCreateInfo,
            nullptr,
            outPipeline
        );

        vkDestroyShaderModule(Device, shaderModule, nullptr);

        if (pipelineResult != VK_SUCCESS)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: failed to create %s compute pipeline (%d)", pipelineName, static_cast<int>(pipelineResult));
            return false;
        }

        return true;
    };

    if (!createPipelineFromSpirv(
            melonDS_gpu3d_vulkan_interp_spans_comp_spv,
            melonDS_gpu3d_vulkan_interp_spans_comp_spv_len,
            "interp_spans",
            nullptr,
            &InterpPipeline))
    {
        return false;
    }

    if (!createPipelineFromSpirv(
            melonDS_gpu3d_vulkan_bin_combined_comp_spv,
            melonDS_gpu3d_vulkan_bin_combined_comp_spv_len,
            "bin",
            nullptr,
            &BinPipeline))
    {
        return false;
    }

    if (!createPipelineFromSpirv(
            melonDS_gpu3d_vulkan_calc_work_offsets_comp_spv,
            melonDS_gpu3d_vulkan_calc_work_offsets_comp_spv_len,
            "work_offsets",
            nullptr,
            &WorkOffsetsPipeline))
    {
        return false;
    }

    if (!createPipelineFromSpirv(
            melonDS_gpu3d_vulkan_sort_work_comp_spv,
            melonDS_gpu3d_vulkan_sort_work_comp_spv_len,
            "sort",
            nullptr,
            &SortPipeline))
    {
        return false;
    }

    if (!createPipelineFromSpirv(
            melonDS_gpu3d_vulkan_depth_blend_comp_spv,
            melonDS_gpu3d_vulkan_depth_blend_comp_spv_len,
            "depth_blend",
            nullptr,
            &DepthBlendPipeline))
    {
        return false;
    }

    struct RasterSpecializationData
    {
        u32 sceneMode;
        u32 expectWBufferMode;
        u32 expectShadeMode;
        u32 expectTextureMode;
        u32 expectTranslucencyMode;
    };

    std::array<VkSpecializationMapEntry, 5> rasterSpecializationEntries{};
    rasterSpecializationEntries[0].constantID = 0;
    rasterSpecializationEntries[0].offset = offsetof(RasterSpecializationData, sceneMode);
    rasterSpecializationEntries[0].size = sizeof(u32);
    rasterSpecializationEntries[1].constantID = 1;
    rasterSpecializationEntries[1].offset = offsetof(RasterSpecializationData, expectWBufferMode);
    rasterSpecializationEntries[1].size = sizeof(u32);
    rasterSpecializationEntries[2].constantID = 2;
    rasterSpecializationEntries[2].offset = offsetof(RasterSpecializationData, expectShadeMode);
    rasterSpecializationEntries[2].size = sizeof(u32);
    rasterSpecializationEntries[3].constantID = 3;
    rasterSpecializationEntries[3].offset = offsetof(RasterSpecializationData, expectTextureMode);
    rasterSpecializationEntries[3].size = sizeof(u32);
    rasterSpecializationEntries[4].constantID = 4;
    rasterSpecializationEntries[4].offset = offsetof(RasterSpecializationData, expectTranslucencyMode);
    rasterSpecializationEntries[4].size = sizeof(u32);

    const char* rasterSceneModeNames[] = {"dense_no_boundary", "dense_boundary", "sparse_active"};
    const char* rasterWModeNames[] = {"z", "w", "any"};
    const char* rasterShadeModeNames[] = {"modulate", "decal", "toon", "highlight", "shadow", "any"};
    const char* rasterTextureModeNames[] = {"notexture", "texture", "anytex"};
    const char* rasterTranslucencyModeNames[] = {"opaque", "translucent", "anyalpha"};
    constexpr u32 kCreateRasterShadeModeAny = 5u;
    constexpr u32 kCreateRasterTextureModeAny = 2u;
    constexpr u32 kCreateRasterTranslucencyModeAny = 2u;
    const u32* triRasterSpirv = melonDS_gpu3d_vulkan_tri_raster_comp_spv;
    size_t triRasterSpirvLen = melonDS_gpu3d_vulkan_tri_raster_comp_spv_len;
    if (samplingPath == TextureSamplingPath::BaseSingleDescriptor)
    {
        triRasterSpirv = melonDS_gpu3d_vulkan_tri_raster_base_comp_spv;
        triRasterSpirvLen = melonDS_gpu3d_vulkan_tri_raster_base_comp_spv_len;
    }
    else if (samplingPath == TextureSamplingPath::CompatDynamicUniform)
    {
        triRasterSpirv = melonDS_gpu3d_vulkan_tri_raster_compat_comp_spv;
        triRasterSpirvLen = melonDS_gpu3d_vulkan_tri_raster_compat_comp_spv_len;
    }

    const VulkanContext& context = VulkanContext::Get();
    Log(
        LogLevel::Warn,
        "VulkanRuntime[Capabilities]: swapchain=1 timeline=%d dynamicIndexing=%d nonUniform=%d path=%s forceTimelineOff=%d forceDynamicOff=%d",
        context.SupportsTimelineSemaphores() ? 1 : 0,
        context.SupportsDynamicTextureIndexing() ? 1 : 0,
        context.SupportsNonUniformTextureIndexing() ? 1 : 0,
        textureSamplingPathName(samplingPath),
        context.IsTimelineSemaphoreForcedOff() ? 1 : 0,
        context.IsDynamicTextureIndexingForcedOff() ? 1 : 0
    );
    Log(
        LogLevel::Warn,
        "VulkanRenderer3D: using %s texture sampling path",
        samplingPath == TextureSamplingPath::NonUniform
            ? "non-uniform descriptor indexing"
            : (samplingPath == TextureSamplingPath::CompatDynamicUniform
                ? "compatibility (dynamic-uniform descriptor indexing)"
                : "base switch-descriptor compatibility")
    );
    auto makeRasterPipelineIndex = [&](u32 sceneMode,
                                       u32 rasterWMode,
                                       u32 rasterShadeMode,
                                       u32 rasterTextureMode,
                                       u32 rasterTranslucencyMode) -> u32
    {
        return ((((sceneMode * RasterWModeCount) + rasterWMode) * RasterShadeModeCount + rasterShadeMode) * RasterTextureModeCount + rasterTextureMode)
            * RasterTranslucencyModeCount
            + rasterTranslucencyMode;
    };
    for (u32 rasterSceneMode = 0; rasterSceneMode < RasterSceneModeCount; rasterSceneMode++)
    {
        for (u32 rasterWMode = 0; rasterWMode < RasterWModeCount; rasterWMode++)
        {
            const u32 rasterShadeMode = kCreateRasterShadeModeAny;
            const u32 rasterTextureMode = kCreateRasterTextureModeAny;
            const u32 rasterTranslucencyMode = kCreateRasterTranslucencyModeAny;
            RasterSpecializationData rasterSpecializationData{};
            rasterSpecializationData.sceneMode = rasterSceneMode;
            rasterSpecializationData.expectWBufferMode = rasterWMode;
            rasterSpecializationData.expectShadeMode = rasterShadeMode;
            rasterSpecializationData.expectTextureMode = rasterTextureMode;
            rasterSpecializationData.expectTranslucencyMode = rasterTranslucencyMode;

            VkSpecializationInfo rasterSpecializationInfo{};
            rasterSpecializationInfo.mapEntryCount = static_cast<u32>(rasterSpecializationEntries.size());
            rasterSpecializationInfo.pMapEntries = rasterSpecializationEntries.data();
            rasterSpecializationInfo.dataSize = sizeof(rasterSpecializationData);
            rasterSpecializationInfo.pData = &rasterSpecializationData;

            const u32 rasterPipelineIndex = makeRasterPipelineIndex(
                rasterSceneMode,
                rasterWMode,
                rasterShadeMode,
                rasterTextureMode,
                rasterTranslucencyMode
            );
            char rasterPipelineName[128]{};
            std::snprintf(
                rasterPipelineName,
                sizeof(rasterPipelineName),
                "raster_%s_%s_%s_%s_%s",
                rasterSceneModeNames[rasterSceneMode],
                rasterWModeNames[rasterWMode],
                rasterShadeModeNames[rasterShadeMode],
                rasterTextureModeNames[rasterTextureMode],
                rasterTranslucencyModeNames[rasterTranslucencyMode]
            );

            if (!createPipelineFromSpirv(
                    triRasterSpirv,
                    triRasterSpirvLen,
                    rasterPipelineName,
                    &rasterSpecializationInfo,
                    &RasterPipelines[rasterPipelineIndex]))
            {
                return false;
            }
        }
    }

    struct FinalSpecializationData
    {
        u32 enableEdgeMarking;
        u32 enableFog;
        u32 enableAntiAliasing;
    };

    std::array<VkSpecializationMapEntry, 3> finalSpecializationEntries{};
    finalSpecializationEntries[0].constantID = 0;
    finalSpecializationEntries[0].offset = offsetof(FinalSpecializationData, enableEdgeMarking);
    finalSpecializationEntries[0].size = sizeof(u32);
    finalSpecializationEntries[1].constantID = 1;
    finalSpecializationEntries[1].offset = offsetof(FinalSpecializationData, enableFog);
    finalSpecializationEntries[1].size = sizeof(u32);
    finalSpecializationEntries[2].constantID = 2;
    finalSpecializationEntries[2].offset = offsetof(FinalSpecializationData, enableAntiAliasing);
    finalSpecializationEntries[2].size = sizeof(u32);

    for (u32 finalPipelineIndex = 0; finalPipelineIndex < FinalPipelineVariantCount; finalPipelineIndex++)
    {
        FinalSpecializationData finalSpecializationData{};
        finalSpecializationData.enableEdgeMarking = finalPipelineIndex & 0x1u;
        finalSpecializationData.enableFog = (finalPipelineIndex >> 1u) & 0x1u;
        finalSpecializationData.enableAntiAliasing = (finalPipelineIndex >> 2u) & 0x1u;

        VkSpecializationInfo finalSpecializationInfo{};
        finalSpecializationInfo.mapEntryCount = static_cast<u32>(finalSpecializationEntries.size());
        finalSpecializationInfo.pMapEntries = finalSpecializationEntries.data();
        finalSpecializationInfo.dataSize = sizeof(finalSpecializationData);
        finalSpecializationInfo.pData = &finalSpecializationData;

        char finalPipelineName[32]{};
        std::snprintf(finalPipelineName, sizeof(finalPipelineName), "final_%u", finalPipelineIndex);
        if (!createPipelineFromSpirv(
                melonDS_gpu3d_vulkan_final_pass_comp_spv,
                melonDS_gpu3d_vulkan_final_pass_comp_spv_len,
                finalPipelineName,
                &finalSpecializationInfo,
                &FinalPipelines[finalPipelineIndex]))
        {
            return false;
        }
    }

    if (!createPipelineFromSpirv(
            melonDS_gpu3d_vulkan_capture_line_export_comp_spv,
            melonDS_gpu3d_vulkan_capture_line_export_comp_spv_len,
            "capture_line_export",
            nullptr,
            &CaptureLineExportPipeline))
    {
        return false;
    }

    return true;
}

bool VulkanRenderer3D::createGraphicsPipelines()
{
    GraphicsReady = false;

    if (GraphicsDescriptorSetLayout == VK_NULL_HANDLE)
        return false;

    if (melonDS_gpu3d_vulkan_graphics_raster_vert_spv_len == 0
        || melonDS_gpu3d_vulkan_graphics_raster_frag_spv_len == 0
        || melonDS_gpu3d_vulkan_graphics_no_color_frag_spv_len == 0
        || melonDS_gpu3d_vulkan_graphics_clear_frag_spv_len == 0
        || melonDS_gpu3d_vulkan_graphics_final_vert_spv_len == 0
        || melonDS_gpu3d_vulkan_graphics_edge_frag_spv_len == 0
        || melonDS_gpu3d_vulkan_graphics_fog_frag_spv_len == 0)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: empty graphics SPIR-V blob(s)");
        return false;
    }

    if (!selectGraphicsDepthStencilFormat())
    {
        Log(LogLevel::Error, "VulkanRenderer3D: no supported graphics depth/stencil format");
        return false;
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(RasterPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &GraphicsDescriptorSetLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(Device, &pipelineLayoutCreateInfo, nullptr, &GraphicsPipelineLayout) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics pipeline layout");
        return false;
    }

    VkSamplerCreateInfo samplerCreateInfo{};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
    samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.maxLod = 0.0f;
    if (vkCreateSampler(Device, &samplerCreateInfo, nullptr, &GraphicsAttachmentSampler) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics attachment sampler");
        return false;
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = ColorTargetFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription attrAttachment = colorAttachment;
    attrAttachment.format = AttrTargetFormat;
    attrAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthColorAttachment{};
    depthColorAttachment.format = DepthTargetFormat;
    depthColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthColorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    depthColorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthStencilAttachment{};
    depthStencilAttachment.format = GraphicsDepthStencilFormat;
    depthStencilAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthStencilAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthStencilAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthStencilAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthStencilAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentReference, 3> colorRefs{};
    colorRefs[0] = {0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    colorRefs[1] = {1u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    colorRefs[2] = {2u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthStencilRef{3u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription rasterSubpass{};
    rasterSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    rasterSubpass.colorAttachmentCount = static_cast<u32>(colorRefs.size());
    rasterSubpass.pColorAttachments = colorRefs.data();
    rasterSubpass.pDepthStencilAttachment = &depthStencilRef;

    std::array<VkAttachmentDescription, 4> rasterAttachments = {
        colorAttachment,
        attrAttachment,
        depthColorAttachment,
        depthStencilAttachment,
    };

    std::array<VkSubpassDependency, 2> rasterDependencies{};
    rasterDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    rasterDependencies[0].dstSubpass = 0;
    rasterDependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    rasterDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    rasterDependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    rasterDependencies[1].srcSubpass = 0;
    rasterDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    rasterDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    rasterDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    rasterDependencies[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    rasterDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rasterRenderPassCreateInfo{};
    rasterRenderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rasterRenderPassCreateInfo.attachmentCount = static_cast<u32>(rasterAttachments.size());
    rasterRenderPassCreateInfo.pAttachments = rasterAttachments.data();
    rasterRenderPassCreateInfo.subpassCount = 1;
    rasterRenderPassCreateInfo.pSubpasses = &rasterSubpass;
    rasterRenderPassCreateInfo.dependencyCount = static_cast<u32>(rasterDependencies.size());
    rasterRenderPassCreateInfo.pDependencies = rasterDependencies.data();

    if (vkCreateRenderPass(Device, &rasterRenderPassCreateInfo, nullptr, &GraphicsRasterRenderPass) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics raster render pass");
        return false;
    }

    VkAttachmentDescription finalColorAttachment{};
    finalColorAttachment.format = ColorTargetFormat;
    finalColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    finalColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    finalColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    finalColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    finalColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    finalColorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    finalColorAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkAttachmentReference finalColorRef{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription finalSubpass{};
    finalSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    finalSubpass.colorAttachmentCount = 1;
    finalSubpass.pColorAttachments = &finalColorRef;

    std::array<VkSubpassDependency, 2> finalDependencies{};
    finalDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    finalDependencies[0].dstSubpass = 0;
    finalDependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    finalDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    finalDependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    finalDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    finalDependencies[1].srcSubpass = 0;
    finalDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    finalDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    finalDependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    finalDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    finalDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

    VkRenderPassCreateInfo finalRenderPassCreateInfo{};
    finalRenderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    finalRenderPassCreateInfo.attachmentCount = 1;
    finalRenderPassCreateInfo.pAttachments = &finalColorAttachment;
    finalRenderPassCreateInfo.subpassCount = 1;
    finalRenderPassCreateInfo.pSubpasses = &finalSubpass;
    finalRenderPassCreateInfo.dependencyCount = static_cast<u32>(finalDependencies.size());
    finalRenderPassCreateInfo.pDependencies = finalDependencies.data();

    if (vkCreateRenderPass(Device, &finalRenderPassCreateInfo, nullptr, &GraphicsFinalRenderPass) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics final render pass");
        return false;
    }

    VkShaderModule rasterVertModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_raster_vert_spv, melonDS_gpu3d_vulkan_graphics_raster_vert_spv_len);
    VkShaderModule rasterFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_raster_frag_spv, melonDS_gpu3d_vulkan_graphics_raster_frag_spv_len);
    VkShaderModule rasterNoFragDepthFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_frag_spv, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_frag_spv_len);
    VkShaderModule rasterNoFragDepthDirectFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_frag_spv, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_frag_spv_len);
    VkShaderModule rasterNoFragDepthDirectFastModulateFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_frag_spv, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_frag_spv_len);
    VkShaderModule rasterNoFragDepthDirectFastModulateToonFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_toon_frag_spv, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_toon_frag_spv_len);
    VkShaderModule rasterNoFragDepthDirectFastModulatePlainFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_plain_frag_spv, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_plain_frag_spv_len);
    VkShaderModule rasterNoFragDepthDirectFastModulateOpaqueAlphaToonFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_opaque_alpha_toon_frag_spv, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_opaque_alpha_toon_frag_spv_len);
    VkShaderModule rasterNoFragDepthDirectFastModulateOpaqueAlphaPlainFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_opaque_alpha_plain_frag_spv, melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_opaque_alpha_plain_frag_spv_len);
    VkShaderModule noColorFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_no_color_frag_spv, melonDS_gpu3d_vulkan_graphics_no_color_frag_spv_len);
    VkShaderModule clearFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_clear_frag_spv, melonDS_gpu3d_vulkan_graphics_clear_frag_spv_len);
    VkShaderModule finalVertModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_final_vert_spv, melonDS_gpu3d_vulkan_graphics_final_vert_spv_len);
    VkShaderModule edgeFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_edge_frag_spv, melonDS_gpu3d_vulkan_graphics_edge_frag_spv_len);
    VkShaderModule edgeFogFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_edge_fog_frag_spv, melonDS_gpu3d_vulkan_graphics_edge_fog_frag_spv_len);
    VkShaderModule fogFragModule = createShaderModule(Device, melonDS_gpu3d_vulkan_graphics_fog_frag_spv, melonDS_gpu3d_vulkan_graphics_fog_frag_spv_len);

    if (rasterVertModule == VK_NULL_HANDLE
        || rasterFragModule == VK_NULL_HANDLE
        || rasterNoFragDepthFragModule == VK_NULL_HANDLE
        || rasterNoFragDepthDirectFragModule == VK_NULL_HANDLE
        || rasterNoFragDepthDirectFastModulateFragModule == VK_NULL_HANDLE
        || rasterNoFragDepthDirectFastModulateToonFragModule == VK_NULL_HANDLE
        || rasterNoFragDepthDirectFastModulatePlainFragModule == VK_NULL_HANDLE
        || rasterNoFragDepthDirectFastModulateOpaqueAlphaToonFragModule == VK_NULL_HANDLE
        || rasterNoFragDepthDirectFastModulateOpaqueAlphaPlainFragModule == VK_NULL_HANDLE
        || noColorFragModule == VK_NULL_HANDLE
        || clearFragModule == VK_NULL_HANDLE
        || finalVertModule == VK_NULL_HANDLE
        || edgeFragModule == VK_NULL_HANDLE
        || edgeFogFragModule == VK_NULL_HANDLE
        || fogFragModule == VK_NULL_HANDLE)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics shader modules");
        if (rasterVertModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, rasterVertModule, nullptr);
        if (rasterFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, rasterFragModule, nullptr);
        if (rasterNoFragDepthFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, rasterNoFragDepthFragModule, nullptr);
        if (rasterNoFragDepthDirectFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, rasterNoFragDepthDirectFragModule, nullptr);
        if (rasterNoFragDepthDirectFastModulateFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, rasterNoFragDepthDirectFastModulateFragModule, nullptr);
        if (rasterNoFragDepthDirectFastModulateToonFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, rasterNoFragDepthDirectFastModulateToonFragModule, nullptr);
        if (rasterNoFragDepthDirectFastModulatePlainFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, rasterNoFragDepthDirectFastModulatePlainFragModule, nullptr);
        if (rasterNoFragDepthDirectFastModulateOpaqueAlphaToonFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, rasterNoFragDepthDirectFastModulateOpaqueAlphaToonFragModule, nullptr);
        if (rasterNoFragDepthDirectFastModulateOpaqueAlphaPlainFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, rasterNoFragDepthDirectFastModulateOpaqueAlphaPlainFragModule, nullptr);
        if (noColorFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, noColorFragModule, nullptr);
        if (clearFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, clearFragModule, nullptr);
        if (finalVertModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, finalVertModule, nullptr);
        if (edgeFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, edgeFragModule, nullptr);
        if (edgeFogFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, edgeFogFragModule, nullptr);
        if (fogFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(Device, fogFragModule, nullptr);
        return false;
    }

    auto makeBlendAttachment = [](
                                   VkColorComponentFlags writeMask,
                                   bool blendEnable,
                                   VkBlendFactor srcColor,
                                   VkBlendFactor dstColor,
                                   VkBlendOp colorOp,
                                   VkBlendFactor srcAlpha,
                                   VkBlendFactor dstAlpha,
                                   VkBlendOp alphaOp) {
        VkPipelineColorBlendAttachmentState state{};
        state.colorWriteMask = writeMask;
        state.blendEnable = blendEnable ? VK_TRUE : VK_FALSE;
        state.srcColorBlendFactor = srcColor;
        state.dstColorBlendFactor = dstColor;
        state.colorBlendOp = colorOp;
        state.srcAlphaBlendFactor = srcAlpha;
        state.dstAlphaBlendFactor = dstAlpha;
        state.alphaBlendOp = alphaOp;
        return state;
    };

    auto createRasterPipeline = [&](VkShaderModule fragmentModule,
                                    const VkSpecializationInfo* fragmentSpecializationInfo,
                                    const std::array<VkPipelineColorBlendAttachmentState, 3>& blendAttachments,
                                    bool depthWriteEnable,
                                    VkCompareOp depthCompareOp,
                                    bool stencilTestEnable,
                                    VkStencilOp stencilFailOp,
                                    VkStencilOp stencilDepthFailOp,
                                    VkStencilOp stencilPassOp,
                                    VkCompareOp stencilCompareOp,
                                    VkPipeline* outPipeline,
                                    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) -> bool {
        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = rasterVertModule;
        shaderStages[0].pName = "main";
        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragmentModule;
        shaderStages[1].pName = "main";
        shaderStages[1].pSpecializationInfo = fragmentSpecializationInfo;

        const VkVertexInputBindingDescription vertexBindingDescription = {
            0u,
            static_cast<u32>(sizeof(GraphicsVertexGpu)),
            VK_VERTEX_INPUT_RATE_VERTEX,
        };
        const std::array<VkVertexInputAttributeDescription, 5> vertexAttributeDescriptions = {{
            {0u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT, 0u},
            {1u, 0u, VK_FORMAT_R32G32_SFLOAT, 16u},
            {2u, 0u, VK_FORMAT_R8G8B8A8_UINT, 24u},
            {3u, 0u, VK_FORMAT_R32G32B32A32_UINT, 28u},
            {4u, 0u, VK_FORMAT_R32G32B32_UINT, 44u},
        }};
        VkPipelineVertexInputStateCreateInfo vertexInputState{};
        vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputState.vertexBindingDescriptionCount = 1;
        vertexInputState.pVertexBindingDescriptions = &vertexBindingDescription;
        vertexInputState.vertexAttributeDescriptionCount = static_cast<u32>(vertexAttributeDescriptions.size());
        vertexInputState.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
        inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyState.topology = topology;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizationState{};
        rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationState.cullMode = VK_CULL_MODE_NONE;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationState.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampleState{};
        multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkStencilOpState stencilState{};
        stencilState.failOp = stencilFailOp;
        stencilState.passOp = stencilPassOp;
        stencilState.depthFailOp = stencilDepthFailOp;
        stencilState.compareOp = stencilCompareOp;
        stencilState.compareMask = 0xFFu;
        stencilState.writeMask = 0xFFu;
        stencilState.reference = 0u;

        VkPipelineDepthStencilStateCreateInfo depthStencilState{};
        depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilState.depthTestEnable = VK_TRUE;
        depthStencilState.depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencilState.depthCompareOp = depthCompareOp;
        depthStencilState.stencilTestEnable = stencilTestEnable ? VK_TRUE : VK_FALSE;
        depthStencilState.front = stencilState;
        depthStencilState.back = stencilState;

        VkPipelineColorBlendStateCreateInfo colorBlendState{};
        colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendState.attachmentCount = static_cast<u32>(blendAttachments.size());
        colorBlendState.pAttachments = blendAttachments.data();

        const std::array<VkDynamicState, 5> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<u32>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stageCount = 2;
        pipelineCreateInfo.pStages = shaderStages;
        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.layout = GraphicsPipelineLayout;
        pipelineCreateInfo.renderPass = GraphicsRasterRenderPass;
        pipelineCreateInfo.subpass = 0;

        return vkCreateGraphicsPipelines(Device, ComputePipelineCache, 1, &pipelineCreateInfo, nullptr, outPipeline) == VK_SUCCESS;
    };

    auto createFullscreenRasterPipeline = [&](VkShaderModule fragmentModule,
                                              const std::array<VkPipelineColorBlendAttachmentState, 3>& blendAttachments,
                                              bool depthWriteEnable,
                                              VkCompareOp depthCompareOp,
                                              bool stencilTestEnable,
                                              VkStencilOp stencilFailOp,
                                              VkStencilOp stencilDepthFailOp,
                                              VkStencilOp stencilPassOp,
                                              VkCompareOp stencilCompareOp,
                                              VkPipeline* outPipeline) -> bool {
        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = finalVertModule;
        shaderStages[0].pName = "main";
        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragmentModule;
        shaderStages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInputState{};
        vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
        inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizationState{};
        rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationState.cullMode = VK_CULL_MODE_NONE;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationState.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampleState{};
        multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkStencilOpState stencilState{};
        stencilState.failOp = stencilFailOp;
        stencilState.passOp = stencilPassOp;
        stencilState.depthFailOp = stencilDepthFailOp;
        stencilState.compareOp = stencilCompareOp;
        stencilState.compareMask = 0xFFu;
        stencilState.writeMask = 0xFFu;
        stencilState.reference = 0u;

        VkPipelineDepthStencilStateCreateInfo depthStencilState{};
        depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilState.depthTestEnable = VK_TRUE;
        depthStencilState.depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencilState.depthCompareOp = depthCompareOp;
        depthStencilState.stencilTestEnable = stencilTestEnable ? VK_TRUE : VK_FALSE;
        depthStencilState.front = stencilState;
        depthStencilState.back = stencilState;

        VkPipelineColorBlendStateCreateInfo colorBlendState{};
        colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendState.attachmentCount = static_cast<u32>(blendAttachments.size());
        colorBlendState.pAttachments = blendAttachments.data();

        const std::array<VkDynamicState, 5> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<u32>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stageCount = 2;
        pipelineCreateInfo.pStages = shaderStages;
        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.layout = GraphicsPipelineLayout;
        pipelineCreateInfo.renderPass = GraphicsRasterRenderPass;
        pipelineCreateInfo.subpass = 0;

        return vkCreateGraphicsPipelines(Device, ComputePipelineCache, 1, &pipelineCreateInfo, nullptr, outPipeline) == VK_SUCCESS;
    };

    auto createFinalPipeline = [&](VkShaderModule fragmentModule,
                                   VkPipelineColorBlendAttachmentState blendAttachment,
                                   VkPipeline* outPipeline) -> bool {
        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = finalVertModule;
        shaderStages[0].pName = "main";
        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragmentModule;
        shaderStages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInputState{};
        vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
        inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizationState{};
        rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationState.cullMode = VK_CULL_MODE_NONE;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationState.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampleState{};
        multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlendState{};
        colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendState.attachmentCount = 1;
        colorBlendState.pAttachments = &blendAttachment;

        const std::array<VkDynamicState, 3> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<u32>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stageCount = 2;
        pipelineCreateInfo.pStages = shaderStages;
        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.layout = GraphicsPipelineLayout;
        pipelineCreateInfo.renderPass = GraphicsFinalRenderPass;
        pipelineCreateInfo.subpass = 0;

        return vkCreateGraphicsPipelines(Device, ComputePipelineCache, 1, &pipelineCreateInfo, nullptr, outPipeline) == VK_SUCCESS;
    };

    struct RasterFragmentSpecialization
    {
        u32 depthInterpolationMode;
        u32 translucentPass;
        u32 edgeMarkPass;
    };

    std::array<VkSpecializationMapEntry, 3> rasterSpecializationEntries{};
    rasterSpecializationEntries[0] = {0u, offsetof(RasterFragmentSpecialization, depthInterpolationMode), sizeof(u32)};
    rasterSpecializationEntries[1] = {1u, offsetof(RasterFragmentSpecialization, translucentPass), sizeof(u32)};
    rasterSpecializationEntries[2] = {2u, offsetof(RasterFragmentSpecialization, edgeMarkPass), sizeof(u32)};

    struct NoColorFragmentSpecialization
    {
        u32 writeFragDepth;
        u32 edgeMarkPass;
    };

    std::array<VkSpecializationMapEntry, 2> noColorSpecializationEntries{};
    noColorSpecializationEntries[0] = {0u, offsetof(NoColorFragmentSpecialization, writeFragDepth), sizeof(u32)};
    noColorSpecializationEntries[1] = {1u, offsetof(NoColorFragmentSpecialization, edgeMarkPass), sizeof(u32)};

    const auto makeOpaqueIndex = [](u32 wMode, u32 depthCompareMode) {
        return (wMode * GraphicsDepthCompareModeCount) + depthCompareMode;
    };
    const auto makeBgZeroTranslucentIndex = [](u32 wMode, u32 depthCompareMode, u32 depthWriteMode, u32 fogWriteMode) {
        return (((wMode * GraphicsDepthCompareModeCount) + depthCompareMode) * GraphicsDepthWriteModeCount + depthWriteMode)
            * GraphicsFogWriteModeCount
            + fogWriteMode;
    };
    const auto makeTranslucentIndex = [](u32 wMode, u32 depthCompareMode, u32 depthWriteMode, u32 fogWriteMode, u32 alphaBlendMode) {
        return ((((wMode * GraphicsDepthCompareModeCount) + depthCompareMode) * GraphicsDepthWriteModeCount + depthWriteMode)
            * GraphicsFogWriteModeCount
            + fogWriteMode)
            * GraphicsAlphaBlendModeCount
            + alphaBlendMode;
    };

    const auto colorWriteAll = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    const auto colorWriteB = VK_COLOR_COMPONENT_B_BIT;
    const auto colorWriteR = VK_COLOR_COMPONENT_R_BIT;
    const auto colorWriteNone = 0u;

    const std::array<VkPipelineColorBlendAttachmentState, 3> clearBlendAttachments = {
        makeBlendAttachment(colorWriteAll, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
        makeBlendAttachment(colorWriteAll, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
        makeBlendAttachment(colorWriteR, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
    };
    if (!createFullscreenRasterPipeline(
            clearFragModule,
            clearBlendAttachments,
            true,
            VK_COMPARE_OP_ALWAYS,
            true,
            VK_STENCIL_OP_REPLACE,
            VK_STENCIL_OP_REPLACE,
            VK_STENCIL_OP_REPLACE,
            VK_COMPARE_OP_ALWAYS,
            &GraphicsClearPipeline))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics clear pipeline");
        return false;
    }

    for (u32 wMode = 0; wMode < GraphicsWModeCount; wMode++)
    {
        for (u32 depthCompareMode = 0; depthCompareMode < GraphicsDepthCompareModeCount; depthCompareMode++)
        {
            RasterFragmentSpecialization opaqueSpecializationData{};
            opaqueSpecializationData.depthInterpolationMode = wMode;
            opaqueSpecializationData.translucentPass = 0u;
            opaqueSpecializationData.edgeMarkPass = 0u;

            VkSpecializationInfo opaqueSpecializationInfo{};
            opaqueSpecializationInfo.mapEntryCount = static_cast<u32>(rasterSpecializationEntries.size());
            opaqueSpecializationInfo.pMapEntries = rasterSpecializationEntries.data();
            opaqueSpecializationInfo.dataSize = sizeof(opaqueSpecializationData);
            opaqueSpecializationInfo.pData = &opaqueSpecializationData;

            const std::array<VkPipelineColorBlendAttachmentState, 3> opaqueBlendAttachments = {
                makeBlendAttachment(colorWriteAll, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
                makeBlendAttachment(colorWriteAll, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
                makeBlendAttachment(colorWriteR, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
            };

            const bool useDirectWBufferTextureIndexing =
                wMode != 0u && VulkanContext::Get().SupportsDynamicTextureIndexing();
            VkShaderModule opaqueFragModule = rasterFragModule;
            if (wMode != 0u)
                opaqueFragModule = useDirectWBufferTextureIndexing ? rasterNoFragDepthDirectFragModule : rasterNoFragDepthFragModule;

            if (!createRasterPipeline(
                    opaqueFragModule,
                    &opaqueSpecializationInfo,
                    opaqueBlendAttachments,
                    true,
                    depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                    true,
                    VK_STENCIL_OP_KEEP,
                    VK_STENCIL_OP_KEEP,
                    VK_STENCIL_OP_REPLACE,
                    VK_COMPARE_OP_ALWAYS,
                    &GraphicsOpaquePipelines[makeOpaqueIndex(wMode, depthCompareMode)]))
            {
                Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics opaque pipeline");
                return false;
            }
            if (wMode != 0u
                && !createRasterPipeline(
                    rasterFragModule,
                    &opaqueSpecializationInfo,
                    opaqueBlendAttachments,
                    true,
                    depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                    true,
                    VK_STENCIL_OP_KEEP,
                    VK_STENCIL_OP_KEEP,
                    VK_STENCIL_OP_REPLACE,
                    VK_COMPARE_OP_ALWAYS,
                    &GraphicsOpaqueFragmentDepthPipelines[makeOpaqueIndex(wMode, depthCompareMode)]))
            {
                Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics opaque fragment-depth pipeline");
                return false;
            }
            if (depthCompareMode == 0u
                && !createRasterPipeline(
                    opaqueFragModule,
                    &opaqueSpecializationInfo,
                    opaqueBlendAttachments,
                    false,
                    VK_COMPARE_OP_ALWAYS,
                    true,
                    VK_STENCIL_OP_KEEP,
                    VK_STENCIL_OP_KEEP,
                    VK_STENCIL_OP_REPLACE,
                    VK_COMPARE_OP_ALWAYS,
                    &GraphicsOpaqueUiOverlayPipelines[wMode]))
            {
                Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics opaque UI overlay pipeline");
                return false;
            }
            if (useDirectWBufferTextureIndexing)
            {
                if (!createRasterPipeline(
                        rasterNoFragDepthDirectFastModulateFragModule,
                        &opaqueSpecializationInfo,
                        opaqueBlendAttachments,
                        true,
                        depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                        true,
                        VK_STENCIL_OP_KEEP,
                        VK_STENCIL_OP_KEEP,
                        VK_STENCIL_OP_REPLACE,
                        VK_COMPARE_OP_ALWAYS,
                        &GraphicsOpaqueFastModulatePipelines[makeOpaqueIndex(wMode, depthCompareMode)]))
                {
                    Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics opaque fast-modulate pipeline");
                    return false;
                }
                if (!createRasterPipeline(
                        rasterNoFragDepthDirectFastModulateToonFragModule,
                        &opaqueSpecializationInfo,
                        opaqueBlendAttachments,
                        true,
                        depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                        true,
                        VK_STENCIL_OP_KEEP,
                        VK_STENCIL_OP_KEEP,
                        VK_STENCIL_OP_REPLACE,
                        VK_COMPARE_OP_ALWAYS,
                        &GraphicsOpaqueFastModulateToonPipelines[makeOpaqueIndex(wMode, depthCompareMode)]))
                {
                    Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics opaque fast-modulate-toon pipeline");
                    return false;
                }
                if (!createRasterPipeline(
                        rasterNoFragDepthDirectFastModulatePlainFragModule,
                        &opaqueSpecializationInfo,
                        opaqueBlendAttachments,
                        true,
                        depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                        true,
                        VK_STENCIL_OP_KEEP,
                        VK_STENCIL_OP_KEEP,
                        VK_STENCIL_OP_REPLACE,
                        VK_COMPARE_OP_ALWAYS,
                        &GraphicsOpaqueFastModulatePlainPipelines[makeOpaqueIndex(wMode, depthCompareMode)]))
                {
                    Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics opaque fast-modulate-plain pipeline");
                    return false;
                }
                if (!createRasterPipeline(
                        rasterNoFragDepthDirectFastModulateOpaqueAlphaToonFragModule,
                        &opaqueSpecializationInfo,
                        opaqueBlendAttachments,
                        true,
                        depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                        true,
                        VK_STENCIL_OP_KEEP,
                        VK_STENCIL_OP_KEEP,
                        VK_STENCIL_OP_REPLACE,
                        VK_COMPARE_OP_ALWAYS,
                        &GraphicsOpaqueFastModulateOpaqueAlphaToonPipelines[makeOpaqueIndex(wMode, depthCompareMode)]))
                {
                    Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics opaque fast-modulate-opaque-alpha-toon pipeline");
                    return false;
                }
                if (!createRasterPipeline(
                        rasterNoFragDepthDirectFastModulateOpaqueAlphaPlainFragModule,
                        &opaqueSpecializationInfo,
                        opaqueBlendAttachments,
                        true,
                        depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                        true,
                        VK_STENCIL_OP_KEEP,
                        VK_STENCIL_OP_KEEP,
                        VK_STENCIL_OP_REPLACE,
                        VK_COMPARE_OP_ALWAYS,
                        &GraphicsOpaqueFastModulateOpaqueAlphaPlainPipelines[makeOpaqueIndex(wMode, depthCompareMode)]))
                {
                    Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics opaque fast-modulate-opaque-alpha-plain pipeline");
                    return false;
                }
            }

            for (u32 depthWriteMode = 0; depthWriteMode < GraphicsDepthWriteModeCount; depthWriteMode++)
            {
                for (u32 fogWriteMode = 0; fogWriteMode < GraphicsFogWriteModeCount; fogWriteMode++)
                {
                    RasterFragmentSpecialization translucentSpecializationData{};
                    translucentSpecializationData.depthInterpolationMode = wMode;
                    translucentSpecializationData.translucentPass = 1u;
                    translucentSpecializationData.edgeMarkPass = 0u;

                    VkSpecializationInfo translucentSpecializationInfo{};
                    translucentSpecializationInfo.mapEntryCount = static_cast<u32>(rasterSpecializationEntries.size());
                    translucentSpecializationInfo.pMapEntries = rasterSpecializationEntries.data();
                    translucentSpecializationInfo.dataSize = sizeof(translucentSpecializationData);
                    translucentSpecializationInfo.pData = &translucentSpecializationData;

                    const std::array<VkPipelineColorBlendAttachmentState, 3> translucentBlendAttachments = {
                        makeBlendAttachment(colorWriteAll, true, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_MAX),
                        makeBlendAttachment(fogWriteMode != 0u ? colorWriteB : colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
                        makeBlendAttachment(depthWriteMode != 0u ? colorWriteR : colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
                    };
                    const std::array<VkPipelineColorBlendAttachmentState, 3> translucentReplaceBlendAttachments = {
                        makeBlendAttachment(colorWriteAll, true, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_MAX),
                        makeBlendAttachment(fogWriteMode != 0u ? colorWriteB : colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
                        makeBlendAttachment(depthWriteMode != 0u ? colorWriteR : colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
                    };
                    const std::array<VkPipelineColorBlendAttachmentState, 3> bgZeroTranslucentBlendAttachments = {
                        makeBlendAttachment(colorWriteAll, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
                        makeBlendAttachment(fogWriteMode != 0u ? colorWriteB : colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
                        makeBlendAttachment(depthWriteMode != 0u ? colorWriteR : colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
                    };

                    if (!createRasterPipeline(
                            rasterFragModule,
                            &translucentSpecializationInfo,
                            translucentBlendAttachments,
                            depthWriteMode != 0u,
                            depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                            true,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_REPLACE,
                            VK_COMPARE_OP_NOT_EQUAL,
                            &GraphicsTranslucentPipelines[makeTranslucentIndex(wMode, depthCompareMode, depthWriteMode, fogWriteMode, 1u)]))
                    {
                        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics translucent pipeline");
                        return false;
                    }

                    if (!createRasterPipeline(
                            rasterFragModule,
                            &translucentSpecializationInfo,
                            translucentReplaceBlendAttachments,
                            depthWriteMode != 0u,
                            depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                            true,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_REPLACE,
                            VK_COMPARE_OP_NOT_EQUAL,
                            &GraphicsTranslucentPipelines[makeTranslucentIndex(wMode, depthCompareMode, depthWriteMode, fogWriteMode, 0u)]))
                    {
                        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics translucent-replace pipeline");
                        return false;
                    }

                    if (!createRasterPipeline(
                            rasterFragModule,
                            &translucentSpecializationInfo,
                            bgZeroTranslucentBlendAttachments,
                            depthWriteMode != 0u,
                            depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                            true,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_INVERT,
                            VK_COMPARE_OP_EQUAL,
                            &GraphicsBgZeroTranslucentPipelines[makeBgZeroTranslucentIndex(wMode, depthCompareMode, depthWriteMode, fogWriteMode)]))
                    {
                        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics bg-zero translucent pipeline");
                        return false;
                    }

                    if (!createRasterPipeline(
                            rasterFragModule,
                            &translucentSpecializationInfo,
                            translucentBlendAttachments,
                            depthWriteMode != 0u,
                            depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                            true,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_INVERT,
                            VK_COMPARE_OP_EQUAL,
                            &GraphicsShadowBlendBgZeroPipelines[makeTranslucentIndex(wMode, depthCompareMode, depthWriteMode, fogWriteMode, 1u)]))
                    {
                        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics bg-zero shadow-blend pipeline");
                        return false;
                    }

                    if (!createRasterPipeline(
                            rasterFragModule,
                            &translucentSpecializationInfo,
                            translucentReplaceBlendAttachments,
                            depthWriteMode != 0u,
                            depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                            true,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_INVERT,
                            VK_COMPARE_OP_EQUAL,
                            &GraphicsShadowBlendBgZeroPipelines[makeTranslucentIndex(wMode, depthCompareMode, depthWriteMode, fogWriteMode, 0u)]))
                    {
                        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics bg-zero shadow-blend-replace pipeline");
                        return false;
                    }

                    if (!createRasterPipeline(
                            rasterFragModule,
                            &translucentSpecializationInfo,
                            translucentBlendAttachments,
                            depthWriteMode != 0u,
                            depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                            true,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_REPLACE,
                            VK_COMPARE_OP_EQUAL,
                            &GraphicsShadowBlendPipelines[makeTranslucentIndex(wMode, depthCompareMode, depthWriteMode, fogWriteMode, 1u)]))
                    {
                        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics shadow-blend pipeline");
                        return false;
                    }

                    if (!createRasterPipeline(
                            rasterFragModule,
                            &translucentSpecializationInfo,
                            translucentReplaceBlendAttachments,
                            depthWriteMode != 0u,
                            depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                            true,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_KEEP,
                            VK_STENCIL_OP_REPLACE,
                            VK_COMPARE_OP_EQUAL,
                            &GraphicsShadowBlendPipelines[makeTranslucentIndex(wMode, depthCompareMode, depthWriteMode, fogWriteMode, 0u)]))
                    {
                        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics shadow-blend-replace pipeline");
                        return false;
                    }
                }
            }
        }

        RasterFragmentSpecialization edgeMarkSpecializationData{};
        edgeMarkSpecializationData.depthInterpolationMode = wMode;
        edgeMarkSpecializationData.translucentPass = 0u;
        edgeMarkSpecializationData.edgeMarkPass = 1u;

        VkSpecializationInfo edgeMarkSpecializationInfo{};
        edgeMarkSpecializationInfo.mapEntryCount = static_cast<u32>(rasterSpecializationEntries.size());
        edgeMarkSpecializationInfo.pMapEntries = rasterSpecializationEntries.data();
        edgeMarkSpecializationInfo.dataSize = sizeof(edgeMarkSpecializationData);
        edgeMarkSpecializationInfo.pData = &edgeMarkSpecializationData;

        const std::array<VkPipelineColorBlendAttachmentState, 3> edgeMarkBlendAttachments = {
            makeBlendAttachment(colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
            makeBlendAttachment(VK_COLOR_COMPONENT_G_BIT, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
            makeBlendAttachment(colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
        };
        if (!createRasterPipeline(
                rasterFragModule,
                &edgeMarkSpecializationInfo,
                edgeMarkBlendAttachments,
                false,
                VK_COMPARE_OP_ALWAYS,
                false,
                VK_STENCIL_OP_KEEP,
                VK_STENCIL_OP_KEEP,
                VK_STENCIL_OP_KEEP,
                VK_COMPARE_OP_ALWAYS,
                &GraphicsEdgeMarkPipelines[wMode],
                VK_PRIMITIVE_TOPOLOGY_LINE_LIST))
        {
            Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics edge-mark pipeline");
            return false;
        }

        NoColorFragmentSpecialization noColorSpecializationData{};
        noColorSpecializationData.writeFragDepth = wMode;
        noColorSpecializationData.edgeMarkPass = 0u;

        VkSpecializationInfo noColorSpecializationInfo{};
        noColorSpecializationInfo.mapEntryCount = static_cast<u32>(noColorSpecializationEntries.size());
        noColorSpecializationInfo.pMapEntries = noColorSpecializationEntries.data();
        noColorSpecializationInfo.dataSize = sizeof(noColorSpecializationData);
        noColorSpecializationInfo.pData = &noColorSpecializationData;

        const std::array<VkPipelineColorBlendAttachmentState, 3> noColorBlendAttachments = {
            makeBlendAttachment(colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
            makeBlendAttachment(colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
            makeBlendAttachment(colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
        };

        if (!createRasterPipeline(
                noColorFragModule,
                &noColorSpecializationInfo,
                noColorBlendAttachments,
                false,
                VK_COMPARE_OP_LESS,
                true,
                VK_STENCIL_OP_KEEP,
                VK_STENCIL_OP_INVERT,
                VK_STENCIL_OP_KEEP,
                VK_COMPARE_OP_EQUAL,
                &GraphicsShadowMaskBgZeroPipelines[wMode]))
        {
            Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics shadow-mask-bgzero pipeline");
            return false;
        }

        if (!createRasterPipeline(
                noColorFragModule,
                &noColorSpecializationInfo,
                noColorBlendAttachments,
                false,
                VK_COMPARE_OP_LESS,
                true,
                VK_STENCIL_OP_KEEP,
                VK_STENCIL_OP_REPLACE,
                VK_STENCIL_OP_KEEP,
                VK_COMPARE_OP_ALWAYS,
                &GraphicsShadowMaskPipelines[wMode]))
        {
            Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics shadow-mask pipeline");
            return false;
        }

        for (u32 depthCompareMode = 0; depthCompareMode < GraphicsDepthCompareModeCount; depthCompareMode++)
        {
            if (!createRasterPipeline(
                    noColorFragModule,
                    &noColorSpecializationInfo,
                    noColorBlendAttachments,
                    false,
                    depthCompareMode != 0u ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                    true,
                    VK_STENCIL_OP_KEEP,
                    VK_STENCIL_OP_KEEP,
                    VK_STENCIL_OP_ZERO,
                    VK_COMPARE_OP_EQUAL,
                    &GraphicsShadowClearPipelines[makeOpaqueIndex(wMode, depthCompareMode)]))
            {
                Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics shadow-clear pipeline");
                return false;
            }
        }
    }

    NoColorFragmentSpecialization stencilClearSpecializationData{};
    stencilClearSpecializationData.writeFragDepth = 0u;
    stencilClearSpecializationData.edgeMarkPass = 0u;
    VkSpecializationInfo stencilClearSpecializationInfo{};
    stencilClearSpecializationInfo.mapEntryCount = static_cast<u32>(noColorSpecializationEntries.size());
    stencilClearSpecializationInfo.pMapEntries = noColorSpecializationEntries.data();
    stencilClearSpecializationInfo.dataSize = sizeof(stencilClearSpecializationData);
    stencilClearSpecializationInfo.pData = &stencilClearSpecializationData;
    const std::array<VkPipelineColorBlendAttachmentState, 3> stencilClearBlendAttachments = {
        makeBlendAttachment(colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
        makeBlendAttachment(colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
        makeBlendAttachment(colorWriteNone, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD),
    };
    if (!createRasterPipeline(
            noColorFragModule,
            &stencilClearSpecializationInfo,
            stencilClearBlendAttachments,
            false,
            VK_COMPARE_OP_ALWAYS,
            true,
            VK_STENCIL_OP_KEEP,
            VK_STENCIL_OP_KEEP,
            VK_STENCIL_OP_REPLACE,
            VK_COMPARE_OP_ALWAYS,
            &GraphicsStencilBitClearPipeline))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics stencil-clear pipeline");
        return false;
    }

    constexpr VkColorComponentFlags colorWriteRgb =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT;

    const VkPipelineColorBlendAttachmentState finalEdgeBlendAttachment = makeBlendAttachment(
        colorWriteRgb,
        true,
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD);
    if (!createFinalPipeline(edgeFragModule, finalEdgeBlendAttachment, &GraphicsFinalEdgePipeline))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics final-edge pipeline");
        return false;
    }

    const VkPipelineColorBlendAttachmentState finalEdgeFogBlendAttachment = makeBlendAttachment(
        colorWriteRgb,
        true,
        VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD);
    if (!createFinalPipeline(edgeFogFragModule, finalEdgeFogBlendAttachment, &GraphicsFinalEdgeFogPipeline))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics final-edge-fog pipeline");
        return false;
    }

    const VkPipelineColorBlendAttachmentState finalFogBlendAttachment = makeBlendAttachment(
        colorWriteRgb,
        true,
        VK_BLEND_FACTOR_CONSTANT_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_CONSTANT_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD);
    if (!createFinalPipeline(fogFragModule, finalFogBlendAttachment, &GraphicsFinalFogPipeline))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics final-fog pipeline");
        return false;
    }

    vkDestroyShaderModule(Device, rasterVertModule, nullptr);
    vkDestroyShaderModule(Device, rasterFragModule, nullptr);
    vkDestroyShaderModule(Device, rasterNoFragDepthFragModule, nullptr);
    vkDestroyShaderModule(Device, rasterNoFragDepthDirectFragModule, nullptr);
    vkDestroyShaderModule(Device, rasterNoFragDepthDirectFastModulateFragModule, nullptr);
    vkDestroyShaderModule(Device, rasterNoFragDepthDirectFastModulateToonFragModule, nullptr);
    vkDestroyShaderModule(Device, rasterNoFragDepthDirectFastModulatePlainFragModule, nullptr);
    vkDestroyShaderModule(Device, rasterNoFragDepthDirectFastModulateOpaqueAlphaToonFragModule, nullptr);
    vkDestroyShaderModule(Device, rasterNoFragDepthDirectFastModulateOpaqueAlphaPlainFragModule, nullptr);
    vkDestroyShaderModule(Device, noColorFragModule, nullptr);
    vkDestroyShaderModule(Device, clearFragModule, nullptr);
    vkDestroyShaderModule(Device, finalVertModule, nullptr);
    vkDestroyShaderModule(Device, edgeFragModule, nullptr);
    vkDestroyShaderModule(Device, edgeFogFragModule, nullptr);
    vkDestroyShaderModule(Device, fogFragModule, nullptr);

    GraphicsReady = true;
    savePipelineCache();
    return true;
}

bool VulkanRenderer3D::ensureRenderTarget(u32 width, u32 height)
{
    if (width == 0 || height == 0)
        return false;

    const bool graphicsAttachmentsReady = !GraphicsReady
        || (AttrImage != VK_NULL_HANDLE
            && DepthImage != VK_NULL_HANDLE
            && DepthStencilImage != VK_NULL_HANDLE
            && GraphicsRasterFramebuffer != VK_NULL_HANDLE
            && GraphicsFinalFramebuffer != VK_NULL_HANDLE);
    if (ColorImage != VK_NULL_HANDLE
        && ColorImageWidth == width
        && ColorImageHeight == height
        && graphicsAttachmentsReady)
    {
        return true;
    }

    const auto supportsFormat = [&](VkFormat format, VkFormatFeatureFlags requiredFeatures) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(PhysicalDevice, format, &properties);
        return (properties.optimalTilingFeatures & requiredFeatures) == requiredFeatures;
    };
    constexpr VkFormatFeatureFlags colorFeatures =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
        | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
        | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
        | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT
        | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    constexpr VkFormatFeatureFlags attrFeatures =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
        | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
        | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
        | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    constexpr VkFormatFeatureFlags depthFeatures =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
        | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
        | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
        | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT
        | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (!supportsFormat(ColorTargetFormat, colorFeatures)
        || (GraphicsReady
            && (!supportsFormat(AttrTargetFormat, attrFeatures)
                || !supportsFormat(DepthTargetFormat, depthFeatures))))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: required render target format features are unavailable");
        return false;
    }

    VkImage newColorImage = VK_NULL_HANDLE;
    VkDeviceMemory newColorMemory = VK_NULL_HANDLE;
    VkImageView newColorView = VK_NULL_HANDLE;
    VkImage newAttrImage = VK_NULL_HANDLE;
    VkDeviceMemory newAttrMemory = VK_NULL_HANDLE;
    VkImageView newAttrView = VK_NULL_HANDLE;
    VkImage newDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory newDepthMemory = VK_NULL_HANDLE;
    VkImageView newDepthView = VK_NULL_HANDLE;
    VkImage newDepthStencilImage = VK_NULL_HANDLE;
    VkDeviceMemory newDepthStencilMemory = VK_NULL_HANDLE;
    VkImageView newDepthStencilView = VK_NULL_HANDLE;
    VkFramebuffer newRasterFramebuffer = VK_NULL_HANDLE;
    VkFramebuffer newFinalFramebuffer = VK_NULL_HANDLE;

    const auto destroyImage = [&](VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(Device, view, nullptr);
        if (image != VK_NULL_HANDLE)
            vkDestroyImage(Device, image, nullptr);
        if (memory != VK_NULL_HANDLE)
            vkFreeMemory(Device, memory, nullptr);
        view = VK_NULL_HANDLE;
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    };
    const auto destroyNewTarget = [&]() {
        if (newFinalFramebuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(Device, newFinalFramebuffer, nullptr);
        if (newRasterFramebuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(Device, newRasterFramebuffer, nullptr);
        newFinalFramebuffer = VK_NULL_HANDLE;
        newRasterFramebuffer = VK_NULL_HANDLE;
        destroyImage(newDepthStencilImage, newDepthStencilMemory, newDepthStencilView);
        destroyImage(newDepthImage, newDepthMemory, newDepthView);
        destroyImage(newAttrImage, newAttrMemory, newAttrView);
        destroyImage(newColorImage, newColorMemory, newColorView);
    };
    const auto createImage = [&] (
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspectMask,
        VkImage& image,
        VkDeviceMemory& memory,
        VkImageView& view) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(Device, &imageInfo, nullptr, &image) != VK_SUCCESS)
            return false;

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(Device, image, &requirements);
        const u32 memoryType = VulkanContext::Get().FindMemoryType(
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memoryType == UINT32_MAX)
        {
            destroyImage(image, memory, view);
            return false;
        }

        VkMemoryAllocateInfo allocationInfo{};
        allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocationInfo.allocationSize = requirements.size;
        allocationInfo.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(Device, &allocationInfo, nullptr, &memory) != VK_SUCCESS
            || vkBindImageMemory(Device, image, memory, 0) != VK_SUCCESS)
        {
            destroyImage(image, memory, view);
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectMask;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(Device, &viewInfo, nullptr, &view) != VK_SUCCESS)
        {
            destroyImage(image, memory, view);
            return false;
        }
        return true;
    };

    const VkImageUsageFlags colorUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
        | VK_IMAGE_USAGE_STORAGE_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!createImage(
            ColorTargetFormat,
            colorUsage,
            VK_IMAGE_ASPECT_COLOR_BIT,
            newColorImage,
            newColorMemory,
            newColorView))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create color target");
        destroyNewTarget();
        return false;
    }

    if (GraphicsReady)
    {
        const VkImageUsageFlags attrUsage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_STORAGE_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        const VkImageUsageFlags depthUsage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_STORAGE_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        const VkImageUsageFlags depthStencilUsage =
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (!createImage(
                AttrTargetFormat,
                attrUsage,
                VK_IMAGE_ASPECT_COLOR_BIT,
                newAttrImage,
                newAttrMemory,
                newAttrView)
            || !createImage(
                DepthTargetFormat,
                depthUsage,
                VK_IMAGE_ASPECT_COLOR_BIT,
                newDepthImage,
                newDepthMemory,
                newDepthView)
            || !createImage(
                GraphicsDepthStencilFormat,
                depthStencilUsage,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                newDepthStencilImage,
                newDepthStencilMemory,
                newDepthStencilView))
        {
            Log(LogLevel::Error, "VulkanRenderer3D: failed to create graphics attachments");
            destroyNewTarget();
            return false;
        }

        const std::array<VkImageView, 4> rasterAttachments = {
            newColorView,
            newAttrView,
            newDepthView,
            newDepthStencilView,
        };
        VkFramebufferCreateInfo rasterInfo{};
        rasterInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        rasterInfo.renderPass = GraphicsRasterRenderPass;
        rasterInfo.attachmentCount = static_cast<u32>(rasterAttachments.size());
        rasterInfo.pAttachments = rasterAttachments.data();
        rasterInfo.width = width;
        rasterInfo.height = height;
        rasterInfo.layers = 1;
        if (vkCreateFramebuffer(Device, &rasterInfo, nullptr, &newRasterFramebuffer) != VK_SUCCESS)
        {
            destroyNewTarget();
            return false;
        }

        VkFramebufferCreateInfo finalInfo{};
        finalInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        finalInfo.renderPass = GraphicsFinalRenderPass;
        finalInfo.attachmentCount = 1;
        finalInfo.pAttachments = &newColorView;
        finalInfo.width = width;
        finalInfo.height = height;
        finalInfo.layers = 1;
        if (vkCreateFramebuffer(Device, &finalInfo, nullptr, &newFinalFramebuffer) != VK_SUCCESS)
        {
            destroyNewTarget();
            return false;
        }
    }

    const bool replacingTarget = ColorImage != VK_NULL_HANDLE;
    if (replacingTarget && !waitForQueueTail("retire resized render target"))
    {
        destroyNewTarget();
        return false;
    }

    if (replacingTarget)
    {
        destroyReadbackBuffer();
        destroyCaptureReadbackImage();
        destroyResultReadbackBuffer();
        destroyBinMaskBuffer();
        destroyGroupListBuffer();
        destroySpanSetupBuffer();
        destroyWorkOffsetBuffer();
        destroyResultBuffer();
    }
    destroyRenderTarget();

    ColorImage = std::exchange(newColorImage, VK_NULL_HANDLE);
    ColorImageMemory = std::exchange(newColorMemory, VK_NULL_HANDLE);
    ColorImageView = std::exchange(newColorView, VK_NULL_HANDLE);
    AttrImage = std::exchange(newAttrImage, VK_NULL_HANDLE);
    AttrImageMemory = std::exchange(newAttrMemory, VK_NULL_HANDLE);
    AttrImageView = std::exchange(newAttrView, VK_NULL_HANDLE);
    DepthImage = std::exchange(newDepthImage, VK_NULL_HANDLE);
    DepthImageMemory = std::exchange(newDepthMemory, VK_NULL_HANDLE);
    DepthImageView = std::exchange(newDepthView, VK_NULL_HANDLE);
    DepthStencilImage = std::exchange(newDepthStencilImage, VK_NULL_HANDLE);
    DepthStencilImageMemory = std::exchange(newDepthStencilMemory, VK_NULL_HANDLE);
    DepthStencilImageView = std::exchange(newDepthStencilView, VK_NULL_HANDLE);
    GraphicsRasterFramebuffer = std::exchange(newRasterFramebuffer, VK_NULL_HANDLE);
    GraphicsFinalFramebuffer = std::exchange(newFinalFramebuffer, VK_NULL_HANDLE);
    ColorImageWidth = width;
    ColorImageHeight = height;
    ColorImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    AttrImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    DepthImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    DepthStencilImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ColorImageInitialized = false;

    invalidateAllDescriptorSetCaches();
    invalidateAllGraphicsDescriptorSetCaches();
    updateDescriptorSet(nullptr);
    updateGraphicsDescriptorSet(nullptr);
    return true;
}

void VulkanRenderer3D::destroyRenderTarget()
{
    invalidateAllDescriptorSetCaches();
    invalidateAllGraphicsDescriptorSetCaches();

    if (GraphicsFinalFramebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(Device, GraphicsFinalFramebuffer, nullptr);
        GraphicsFinalFramebuffer = VK_NULL_HANDLE;
    }

    if (GraphicsRasterFramebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(Device, GraphicsRasterFramebuffer, nullptr);
        GraphicsRasterFramebuffer = VK_NULL_HANDLE;
    }

    if (DepthStencilImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(Device, DepthStencilImageView, nullptr);
        DepthStencilImageView = VK_NULL_HANDLE;
    }

    if (DepthStencilImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(Device, DepthStencilImage, nullptr);
        DepthStencilImage = VK_NULL_HANDLE;
    }

    if (DepthStencilImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, DepthStencilImageMemory, nullptr);
        DepthStencilImageMemory = VK_NULL_HANDLE;
    }

    if (DepthImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(Device, DepthImageView, nullptr);
        DepthImageView = VK_NULL_HANDLE;
    }

    if (DepthImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(Device, DepthImage, nullptr);
        DepthImage = VK_NULL_HANDLE;
    }

    if (DepthImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, DepthImageMemory, nullptr);
        DepthImageMemory = VK_NULL_HANDLE;
    }

    if (AttrImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(Device, AttrImageView, nullptr);
        AttrImageView = VK_NULL_HANDLE;
    }

    if (AttrImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(Device, AttrImage, nullptr);
        AttrImage = VK_NULL_HANDLE;
    }

    if (AttrImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, AttrImageMemory, nullptr);
        AttrImageMemory = VK_NULL_HANDLE;
    }

    if (ColorImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(Device, ColorImageView, nullptr);
        ColorImageView = VK_NULL_HANDLE;
    }

    if (ColorImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(Device, ColorImage, nullptr);
        ColorImage = VK_NULL_HANDLE;
    }

    if (ColorImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, ColorImageMemory, nullptr);
        ColorImageMemory = VK_NULL_HANDLE;
    }

    ColorImageWidth = 0;
    ColorImageHeight = 0;
    ColorImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    AttrImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    DepthImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    DepthStencilImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ColorImageInitialized = false;
}

bool VulkanRenderer3D::ensureTriangleBuffer(RenderContext* context, size_t triangleCount)
{
    static_assert(sizeof(TriangleGpu) == 120, "TriangleGpu layout must match std430 shader struct");
    VkBuffer& triangleBuffer = context != nullptr ? context->TriangleBuffer : TriangleBuffer;
    VkDeviceMemory& triangleMemory = context != nullptr ? context->TriangleMemory : TriangleMemory;
    VkDeviceSize& triangleBufferSize = context != nullptr ? context->TriangleBufferSize : TriangleBufferSize;
    void*& triangleMapped = context != nullptr ? context->TriangleMapped : TriangleMapped;
    const size_t requiredTriangleCount = std::max<size_t>(1, triangleCount);
    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(requiredTriangleCount * sizeof(TriangleGpu));
    if (triangleBuffer != VK_NULL_HANDLE && triangleBufferSize >= requiredSize)
    {
        updateDescriptorSet(context);
        return true;
    }

    const VkDeviceSize allocationSize = growBufferCapacity(requiredSize, triangleBufferSize);
    destroyTriangleBuffer(context);

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            allocationSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            triangleBuffer,
            triangleMemory,
            &triangleMapped))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate triangle buffer");
        destroyTriangleBuffer(context);
        return false;
    }

    triangleBufferSize = allocationSize;
    updateDescriptorSet(context);
    return true;
}

void VulkanRenderer3D::destroyTriangleBuffer(RenderContext* context)
{
    if (context != nullptr)
        invalidateDescriptorSetCache(context);
    else
        invalidateAllDescriptorSetCaches();

    VkBuffer& triangleBuffer = context != nullptr ? context->TriangleBuffer : TriangleBuffer;
    VkDeviceMemory& triangleMemory = context != nullptr ? context->TriangleMemory : TriangleMemory;
    VkDeviceSize& triangleBufferSize = context != nullptr ? context->TriangleBufferSize : TriangleBufferSize;
    void*& triangleMapped = context != nullptr ? context->TriangleMapped : TriangleMapped;

    if (triangleMapped != nullptr)
    {
        vkUnmapMemory(Device, triangleMemory);
        triangleMapped = nullptr;
    }

    if (triangleBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, triangleBuffer, nullptr);
        triangleBuffer = VK_NULL_HANDLE;
    }

    if (triangleMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, triangleMemory, nullptr);
        triangleMemory = VK_NULL_HANDLE;
    }

    triangleBufferSize = 0;
}

bool VulkanRenderer3D::ensureGraphicsVertexBuffer(RenderContext* context, size_t vertexCount)
{
    static_assert(sizeof(GraphicsVertexGpu) == 56u, "GraphicsVertexGpu layout must match vertex shader inputs");
    static_assert(offsetof(GraphicsVertexGpu, x) == 0u, "GraphicsVertexGpu.x offset mismatch");
    static_assert(offsetof(GraphicsVertexGpu, u) == 16u, "GraphicsVertexGpu.u offset mismatch");
    static_assert(offsetof(GraphicsVertexGpu, colorRgba8) == 24u, "GraphicsVertexGpu.colorRgba8 offset mismatch");
    static_assert(offsetof(GraphicsVertexGpu, flags) == 28u, "GraphicsVertexGpu.flags offset mismatch");
    static_assert(offsetof(GraphicsVertexGpu, texHeight) == 44u, "GraphicsVertexGpu.texHeight offset mismatch");
    VkBuffer& graphicsVertexBuffer = context != nullptr ? context->GraphicsVertexBuffer : GraphicsVertexBuffer;
    VkDeviceMemory& graphicsVertexMemory = context != nullptr ? context->GraphicsVertexMemory : GraphicsVertexMemory;
    VkDeviceSize& graphicsVertexBufferSize = context != nullptr ? context->GraphicsVertexBufferSize : GraphicsVertexBufferSize;
    void*& graphicsVertexMapped = context != nullptr ? context->GraphicsVertexMapped : GraphicsVertexMapped;
    const size_t requiredVertexCount = std::max<size_t>(1, vertexCount);
    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(requiredVertexCount * sizeof(GraphicsVertexGpu));
    if (graphicsVertexBuffer != VK_NULL_HANDLE && graphicsVertexBufferSize >= requiredSize)
        return true;

    const VkDeviceSize allocationSize = growBufferCapacity(requiredSize, graphicsVertexBufferSize);
    destroyGraphicsVertexBuffer(context);

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            allocationSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            graphicsVertexBuffer,
            graphicsVertexMemory,
            &graphicsVertexMapped))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate graphics vertex buffer");
        destroyGraphicsVertexBuffer(context);
        return false;
    }

    graphicsVertexBufferSize = allocationSize;
    return true;
}

void VulkanRenderer3D::destroyGraphicsVertexBuffer(RenderContext* context)
{
    VkBuffer& graphicsVertexBuffer = context != nullptr ? context->GraphicsVertexBuffer : GraphicsVertexBuffer;
    VkDeviceMemory& graphicsVertexMemory = context != nullptr ? context->GraphicsVertexMemory : GraphicsVertexMemory;
    VkDeviceSize& graphicsVertexBufferSize = context != nullptr ? context->GraphicsVertexBufferSize : GraphicsVertexBufferSize;
    void*& graphicsVertexMapped = context != nullptr ? context->GraphicsVertexMapped : GraphicsVertexMapped;

    if (graphicsVertexMapped != nullptr)
    {
        vkUnmapMemory(Device, graphicsVertexMemory);
        graphicsVertexMapped = nullptr;
    }

    if (graphicsVertexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, graphicsVertexBuffer, nullptr);
        graphicsVertexBuffer = VK_NULL_HANDLE;
    }

    if (graphicsVertexMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, graphicsVertexMemory, nullptr);
        graphicsVertexMemory = VK_NULL_HANDLE;
    }

    graphicsVertexBufferSize = 0;
}

bool VulkanRenderer3D::ensureGraphicsSceneVertexBuffer(RenderContext* context, size_t vertexCount)
{
    static_assert(sizeof(GraphicsVertexGpu) == 56u, "GraphicsVertexGpu layout must match vertex shader inputs");
    VkBuffer& buffer = context != nullptr ? context->GraphicsSceneVertexBuffer : GraphicsSceneVertexBuffer;
    VkDeviceMemory& memory = context != nullptr ? context->GraphicsSceneVertexMemory : GraphicsSceneVertexMemory;
    VkDeviceSize& capacity = context != nullptr ? context->GraphicsSceneVertexBufferSize : GraphicsSceneVertexBufferSize;
    void*& mapped = context != nullptr ? context->GraphicsSceneVertexMapped : GraphicsSceneVertexMapped;
    const size_t requiredVertexCount = std::max<size_t>(1, vertexCount);
    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(requiredVertexCount * sizeof(GraphicsVertexGpu));
    if (buffer != VK_NULL_HANDLE && capacity >= requiredSize)
        return true;

    const VkDeviceSize allocationSize = growBufferCapacity(requiredSize, capacity);
    destroyGraphicsSceneVertexBuffer(context);

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            allocationSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            buffer,
            memory,
            &mapped))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate graphics scene vertex buffer");
        destroyGraphicsSceneVertexBuffer(context);
        return false;
    }

    capacity = allocationSize;
    return true;
}

void VulkanRenderer3D::destroyGraphicsSceneVertexBuffer(RenderContext* context)
{
    VkBuffer& buffer = context != nullptr ? context->GraphicsSceneVertexBuffer : GraphicsSceneVertexBuffer;
    VkDeviceMemory& memory = context != nullptr ? context->GraphicsSceneVertexMemory : GraphicsSceneVertexMemory;
    VkDeviceSize& capacity = context != nullptr ? context->GraphicsSceneVertexBufferSize : GraphicsSceneVertexBufferSize;
    void*& mapped = context != nullptr ? context->GraphicsSceneVertexMapped : GraphicsSceneVertexMapped;
    if (mapped != nullptr)
    {
        vkUnmapMemory(Device, memory);
        mapped = nullptr;
    }

    if (buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }

    if (memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }

    capacity = 0;
}

bool VulkanRenderer3D::ensureGraphicsEdgeIndexBuffer(RenderContext* context, size_t indexCount)
{
    VkBuffer& buffer = context != nullptr ? context->GraphicsEdgeIndexBuffer : GraphicsEdgeIndexBuffer;
    VkDeviceMemory& memory = context != nullptr ? context->GraphicsEdgeIndexMemory : GraphicsEdgeIndexMemory;
    VkDeviceSize& capacity = context != nullptr ? context->GraphicsEdgeIndexBufferSize : GraphicsEdgeIndexBufferSize;
    void*& mapped = context != nullptr ? context->GraphicsEdgeIndexMapped : GraphicsEdgeIndexMapped;
    const size_t requiredIndexCount = std::max<size_t>(1, indexCount);
    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(requiredIndexCount * sizeof(u16));
    if (buffer != VK_NULL_HANDLE && capacity >= requiredSize)
        return true;

    const VkDeviceSize allocationSize = growBufferCapacity(requiredSize, capacity);
    destroyGraphicsEdgeIndexBuffer(context);

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            allocationSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            buffer,
            memory,
            &mapped))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate graphics edge index buffer");
        destroyGraphicsEdgeIndexBuffer(context);
        return false;
    }

    capacity = allocationSize;
    return true;
}

void VulkanRenderer3D::destroyGraphicsEdgeIndexBuffer(RenderContext* context)
{
    VkBuffer& buffer = context != nullptr ? context->GraphicsEdgeIndexBuffer : GraphicsEdgeIndexBuffer;
    VkDeviceMemory& memory = context != nullptr ? context->GraphicsEdgeIndexMemory : GraphicsEdgeIndexMemory;
    VkDeviceSize& capacity = context != nullptr ? context->GraphicsEdgeIndexBufferSize : GraphicsEdgeIndexBufferSize;
    void*& mapped = context != nullptr ? context->GraphicsEdgeIndexMapped : GraphicsEdgeIndexMapped;
    if (mapped != nullptr)
    {
        vkUnmapMemory(Device, memory);
        mapped = nullptr;
    }

    if (buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }

    if (memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }

    capacity = 0;
}

bool VulkanRenderer3D::ensureCpuBinBuffers(RenderContext& context, size_t triangleCount, u32 width, u32 height)
{
    if (width == 0 || height == 0)
        return false;

    constexpr u32 TileSize = 8;
    const u32 tilesPerLine = (width + (TileSize - 1u)) / TileSize;
    const u32 tileLines = (height + (TileSize - 1u)) / TileSize;
    const u32 tileCount = std::max<u32>(1u, tilesPerLine * tileLines);
    const u32 groupCount = std::max<u32>(1u, static_cast<u32>((triangleCount + 31u) / 32u));
    const VkDeviceSize requiredBinMaskSize = static_cast<VkDeviceSize>(tileCount) * static_cast<VkDeviceSize>(groupCount) * sizeof(u32);
    const VkDeviceSize requiredGroupListSize = static_cast<VkDeviceSize>(tileCount) * static_cast<VkDeviceSize>(groupCount + 1u) * sizeof(u32);

    if (context.BinMaskBuffer != VK_NULL_HANDLE
        && context.BinMaskMapped != nullptr
        && context.BinMaskBufferSize >= requiredBinMaskSize
        && context.GroupListBuffer != VK_NULL_HANDLE
        && context.GroupListMapped != nullptr
        && context.GroupListBufferSize >= requiredGroupListSize)
    {
        updateDescriptorSet(&context);
        return true;
    }

    const VkDeviceSize binMaskAllocationSize = growBufferCapacity(
        requiredBinMaskSize, context.BinMaskBufferSize);
    const VkDeviceSize groupListAllocationSize = growBufferCapacity(
        requiredGroupListSize, context.GroupListBufferSize);
    destroyCpuBinBuffers(context);

    const auto createMappedStorageBuffer = [&](VkDeviceSize bufferSize,
                                               VkBuffer& buffer,
                                               VkDeviceMemory& memory,
                                               void*& mappedMemory) -> bool {
        return createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            buffer,
            memory,
            &mappedMemory);
    };

    if (!createMappedStorageBuffer(
            binMaskAllocationSize,
            context.BinMaskBuffer,
            context.BinMaskMemory,
            context.BinMaskMapped))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create CPU bin mask buffer");
        destroyCpuBinBuffers(context);
        return false;
    }

    if (!createMappedStorageBuffer(
            groupListAllocationSize,
            context.GroupListBuffer,
            context.GroupListMemory,
            context.GroupListMapped))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create CPU group list buffer");
        destroyCpuBinBuffers(context);
        return false;
    }

    context.BinMaskBufferSize = binMaskAllocationSize;
    context.GroupListBufferSize = groupListAllocationSize;
    updateDescriptorSet(&context);
    return true;
}

bool VulkanRenderer3D::ensureCpuSpanSetupBuffer(RenderContext& context, size_t triangleCount)
{
    static_assert(sizeof(SpanSetupGpu) == 44u, "SpanSetupGpu layout must match std430 shader struct");
    const size_t requiredTriangleCount = std::max<size_t>(1, triangleCount);
    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(requiredTriangleCount * sizeof(SpanSetupGpu));

    if (context.SpanSetupBuffer != VK_NULL_HANDLE
        && context.SpanSetupMapped != nullptr
        && context.SpanSetupBufferSize >= requiredSize)
    {
        updateDescriptorSet(&context);
        return true;
    }

    const VkDeviceSize allocationSize = growBufferCapacity(
        requiredSize, context.SpanSetupBufferSize);
    destroyCpuSpanSetupBuffer(context);

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            allocationSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            context.SpanSetupBuffer,
            context.SpanSetupMemory,
            &context.SpanSetupMapped))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate CPU span setup buffer");
        destroyCpuSpanSetupBuffer(context);
        return false;
    }

    context.SpanSetupBufferSize = allocationSize;
    updateDescriptorSet(&context);
    return true;
}

void VulkanRenderer3D::destroyCpuSpanSetupBuffer(RenderContext& context)
{
    invalidateDescriptorSetCache(&context);

    if (context.SpanSetupMapped != nullptr)
    {
        vkUnmapMemory(Device, context.SpanSetupMemory);
        context.SpanSetupMapped = nullptr;
    }

    if (context.SpanSetupBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, context.SpanSetupBuffer, nullptr);
        context.SpanSetupBuffer = VK_NULL_HANDLE;
    }

    if (context.SpanSetupMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, context.SpanSetupMemory, nullptr);
        context.SpanSetupMemory = VK_NULL_HANDLE;
    }

    context.SpanSetupBufferSize = 0;
}

void VulkanRenderer3D::destroyCpuBinBuffers(RenderContext& context)
{
    invalidateDescriptorSetCache(&context);

    if (context.BinMaskMapped != nullptr)
    {
        vkUnmapMemory(Device, context.BinMaskMemory);
        context.BinMaskMapped = nullptr;
    }
    if (context.GroupListMapped != nullptr)
    {
        vkUnmapMemory(Device, context.GroupListMemory);
        context.GroupListMapped = nullptr;
    }

    if (context.BinMaskBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, context.BinMaskBuffer, nullptr);
        context.BinMaskBuffer = VK_NULL_HANDLE;
    }
    if (context.BinMaskMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, context.BinMaskMemory, nullptr);
        context.BinMaskMemory = VK_NULL_HANDLE;
    }
    context.BinMaskBufferSize = 0;

    if (context.GroupListBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, context.GroupListBuffer, nullptr);
        context.GroupListBuffer = VK_NULL_HANDLE;
    }
    if (context.GroupListMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, context.GroupListMemory, nullptr);
        context.GroupListMemory = VK_NULL_HANDLE;
    }
    context.GroupListBufferSize = 0;
}

bool VulkanRenderer3D::ensureCpuWorkOffsetBuffer(RenderContext& context, u32 width, u32 height, size_t triangleCount)
{
    if (width == 0 || height == 0)
        return false;

    constexpr u32 kTileSize = 8u;
    const u32 tilesPerLine = (width + (kTileSize - 1u)) / kTileSize;
    const u32 tileLines = (height + (kTileSize - 1u)) / kTileSize;
    const u32 tileCount = std::max<u32>(1u, tilesPerLine * tileLines);
    const u32 groupCount = std::max<u32>(1u, static_cast<u32>((triangleCount + 31u) / 32u));
    constexpr u32 kWorkOffsetHeaderWords = 8u;
    const u32 maxGroupListEntries = tileCount * groupCount;
    const u32 requiredWords = kWorkOffsetHeaderWords + (tileCount + 1u) + tileCount + maxGroupListEntries;
    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(requiredWords) * sizeof(u32);

    if (context.WorkOffsetBuffer != VK_NULL_HANDLE
        && context.WorkOffsetMapped != nullptr
        && context.WorkOffsetBufferSize >= requiredSize)
    {
        updateDescriptorSet(&context);
        return true;
    }

    const VkDeviceSize allocationSize = growBufferCapacity(
        requiredSize, context.WorkOffsetBufferSize);
    destroyCpuWorkOffsetBuffer(context);

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            allocationSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            context.WorkOffsetBuffer,
            context.WorkOffsetMemory,
            &context.WorkOffsetMapped))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate CPU work offset buffer");
        destroyCpuWorkOffsetBuffer(context);
        return false;
    }

    context.WorkOffsetBufferSize = allocationSize;
    updateDescriptorSet(&context);
    return true;
}

void VulkanRenderer3D::destroyCpuWorkOffsetBuffer(RenderContext& context)
{
    invalidateDescriptorSetCache(&context);

    if (context.WorkOffsetMapped != nullptr)
    {
        vkUnmapMemory(Device, context.WorkOffsetMemory);
        context.WorkOffsetMapped = nullptr;
    }

    if (context.WorkOffsetBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, context.WorkOffsetBuffer, nullptr);
        context.WorkOffsetBuffer = VK_NULL_HANDLE;
    }

    if (context.WorkOffsetMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, context.WorkOffsetMemory, nullptr);
        context.WorkOffsetMemory = VK_NULL_HANDLE;
    }

    context.WorkOffsetBufferSize = 0;
}

bool VulkanRenderer3D::ensureResultBuffer(u32 width, u32 height)
{
    if (width == 0 || height == 0)
        return false;

    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * ResultLayerCount * sizeof(u32);
    if (ResultBuffer != VK_NULL_HANDLE && ResultBufferSize == requiredSize)
    {
        updateDescriptorSet(nullptr);
        return true;
    }

    if (ResultBuffer != VK_NULL_HANDLE && !waitForQueueTail("resize result buffer"))
        return false;

    destroyResultBuffer();

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            requiredSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            0,
            ResultBuffer,
            ResultMemory))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate result buffer");
        destroyResultBuffer();
        return false;
    }

    ResultBufferSize = requiredSize;
    updateDescriptorSet(nullptr);
    return true;
}

void VulkanRenderer3D::destroyResultBuffer()
{
    invalidateAllDescriptorSetCaches();

    if (ResultBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, ResultBuffer, nullptr);
        ResultBuffer = VK_NULL_HANDLE;
    }

    if (ResultMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, ResultMemory, nullptr);
        ResultMemory = VK_NULL_HANDLE;
    }

    ResultBufferSize = 0;
}

bool VulkanRenderer3D::ensureBinMaskBuffer(size_t triangleCount, u32 width, u32 height)
{
    if (width == 0 || height == 0)
        return false;

    constexpr u32 TileSize = 8;
    const u32 tilesPerLine = (width + (TileSize - 1u)) / TileSize;
    const u32 tileLines = (height + (TileSize - 1u)) / TileSize;
    const u32 tileCount = std::max<u32>(1u, tilesPerLine * tileLines);
    const u32 groupCount = std::max<u32>(1u, static_cast<u32>((triangleCount + 31u) / 32u));

    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(tileCount) * static_cast<VkDeviceSize>(groupCount) * sizeof(u32);
    if (BinMaskBuffer != VK_NULL_HANDLE && BinMaskBufferSize >= requiredSize)
    {
        updateDescriptorSet(nullptr);
        return true;
    }

    if (BinMaskBuffer != VK_NULL_HANDLE && !waitForQueueTail("resize bin mask buffer"))
        return false;

    destroyBinMaskBuffer();

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            requiredSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            0,
            BinMaskBuffer,
            BinMaskMemory))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate bin mask buffer");
        destroyBinMaskBuffer();
        return false;
    }

    BinMaskBufferSize = requiredSize;
    updateDescriptorSet(nullptr);
    return true;
}

void VulkanRenderer3D::destroyBinMaskBuffer()
{
    invalidateAllDescriptorSetCaches();

    if (BinMaskBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, BinMaskBuffer, nullptr);
        BinMaskBuffer = VK_NULL_HANDLE;
    }

    if (BinMaskMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, BinMaskMemory, nullptr);
        BinMaskMemory = VK_NULL_HANDLE;
    }

    BinMaskBufferSize = 0;
}

bool VulkanRenderer3D::ensureGroupListBuffer(size_t triangleCount, u32 width, u32 height)
{
    if (width == 0 || height == 0)
        return false;

    constexpr u32 TileSize = 8;
    const u32 tilesPerLine = (width + (TileSize - 1u)) / TileSize;
    const u32 tileLines = (height + (TileSize - 1u)) / TileSize;
    const u32 tileCount = std::max<u32>(1u, tilesPerLine * tileLines);
    const u32 groupCount = std::max<u32>(1u, static_cast<u32>((triangleCount + 31u) / 32u));
    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(tileCount) * static_cast<VkDeviceSize>(groupCount + 1u) * sizeof(u32);

    if (GroupListBuffer != VK_NULL_HANDLE && GroupListBufferSize >= requiredSize)
    {
        updateDescriptorSet(nullptr);
        return true;
    }

    if (GroupListBuffer != VK_NULL_HANDLE && !waitForQueueTail("resize group list buffer"))
        return false;

    destroyGroupListBuffer();

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            requiredSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            0,
            GroupListBuffer,
            GroupListMemory))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate group list buffer");
        destroyGroupListBuffer();
        return false;
    }

    GroupListBufferSize = requiredSize;
    updateDescriptorSet(nullptr);
    return true;
}

void VulkanRenderer3D::destroyGroupListBuffer()
{
    invalidateAllDescriptorSetCaches();

    if (GroupListBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, GroupListBuffer, nullptr);
        GroupListBuffer = VK_NULL_HANDLE;
    }

    if (GroupListMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, GroupListMemory, nullptr);
        GroupListMemory = VK_NULL_HANDLE;
    }

    GroupListBufferSize = 0;
}

bool VulkanRenderer3D::ensureSpanSetupBuffer(size_t triangleCount)
{
    const size_t requiredTriangleCount = std::max<size_t>(1, triangleCount);
    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(requiredTriangleCount * sizeof(SpanSetupGpu));

    if (SpanSetupBuffer != VK_NULL_HANDLE && SpanSetupBufferSize >= requiredSize)
    {
        updateDescriptorSet(nullptr);
        return true;
    }

    if (SpanSetupBuffer != VK_NULL_HANDLE && !waitForQueueTail("resize span setup buffer"))
        return false;

    destroySpanSetupBuffer();

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            requiredSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            0,
            SpanSetupBuffer,
            SpanSetupMemory))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate span setup buffer");
        destroySpanSetupBuffer();
        return false;
    }

    SpanSetupBufferSize = requiredSize;
    updateDescriptorSet(nullptr);
    return true;
}

void VulkanRenderer3D::destroySpanSetupBuffer()
{
    invalidateAllDescriptorSetCaches();

    if (SpanSetupBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, SpanSetupBuffer, nullptr);
        SpanSetupBuffer = VK_NULL_HANDLE;
    }

    if (SpanSetupMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, SpanSetupMemory, nullptr);
        SpanSetupMemory = VK_NULL_HANDLE;
    }

    SpanSetupBufferSize = 0;
}

bool VulkanRenderer3D::ensureWorkOffsetBuffer(u32 width, u32 height, size_t triangleCount)
{
    if (width == 0 || height == 0)
        return false;

    constexpr u32 TileSize = 8;
    const u32 tilesPerLine = (width + (TileSize - 1u)) / TileSize;
    const u32 tileLines = (height + (TileSize - 1u)) / TileSize;
    const u32 tileCount = std::max<u32>(1u, tilesPerLine * tileLines);
    const u32 groupCount = std::max<u32>(1u, static_cast<u32>((triangleCount + 31u) / 32u));
    constexpr u32 WorkOffsetHeaderWords = 8u;
    const u32 maxGroupListEntries = tileCount * groupCount;
    const u32 requiredWords = WorkOffsetHeaderWords + (tileCount + 1u) + tileCount + maxGroupListEntries;
    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(requiredWords) * sizeof(u32);

    if (WorkOffsetBuffer != VK_NULL_HANDLE && WorkOffsetBufferSize >= requiredSize)
    {
        updateDescriptorSet(nullptr);
        return true;
    }

    if (WorkOffsetBuffer != VK_NULL_HANDLE && !waitForQueueTail("resize work offset buffer"))
        return false;

    destroyWorkOffsetBuffer();

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            requiredSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            0,
            WorkOffsetBuffer,
            WorkOffsetMemory))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate work offset buffer");
        destroyWorkOffsetBuffer();
        return false;
    }

    WorkOffsetBufferSize = requiredSize;
    updateDescriptorSet(nullptr);
    return true;
}

void VulkanRenderer3D::destroyWorkOffsetBuffer()
{
    invalidateAllDescriptorSetCaches();

    if (WorkOffsetBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, WorkOffsetBuffer, nullptr);
        WorkOffsetBuffer = VK_NULL_HANDLE;
    }

    if (WorkOffsetMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, WorkOffsetMemory, nullptr);
        WorkOffsetMemory = VK_NULL_HANDLE;
    }

    WorkOffsetBufferSize = 0;
}

bool VulkanRenderer3D::ensureToonBuffer(RenderContext* context)
{
    VkBuffer& toonBuffer = context != nullptr ? context->ToonBuffer : ToonBuffer;
    VkDeviceMemory& toonMemory = context != nullptr ? context->ToonMemory : ToonMemory;
    VkDeviceSize& toonBufferSize = context != nullptr ? context->ToonBufferSize : ToonBufferSize;
    void*& toonMapped = context != nullptr ? context->ToonMapped : ToonMapped;
    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(ToonTableEntryCount) * sizeof(u32);
    if (toonBuffer != VK_NULL_HANDLE && toonBufferSize >= requiredSize)
    {
        updateDescriptorSet(context);
        return true;
    }

    destroyToonBuffer(context);

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            requiredSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            toonBuffer,
            toonMemory,
            &toonMapped))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate toon buffer");
        destroyToonBuffer(context);
        return false;
    }

    toonBufferSize = requiredSize;
    updateDescriptorSet(context);
    return true;
}

void VulkanRenderer3D::destroyToonBuffer(RenderContext* context)
{
    if (context != nullptr)
        invalidateDescriptorSetCache(context);
    else
        invalidateAllDescriptorSetCaches();

    VkBuffer& toonBuffer = context != nullptr ? context->ToonBuffer : ToonBuffer;
    VkDeviceMemory& toonMemory = context != nullptr ? context->ToonMemory : ToonMemory;
    VkDeviceSize& toonBufferSize = context != nullptr ? context->ToonBufferSize : ToonBufferSize;
    void*& toonMapped = context != nullptr ? context->ToonMapped : ToonMapped;

    if (toonMapped != nullptr)
    {
        vkUnmapMemory(Device, toonMemory);
        toonMapped = nullptr;
    }

    if (toonBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, toonBuffer, nullptr);
        toonBuffer = VK_NULL_HANDLE;
    }

    if (toonMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, toonMemory, nullptr);
        toonMemory = VK_NULL_HANDLE;
    }

    toonBufferSize = 0;
}

bool VulkanRenderer3D::updateToonBuffer(RenderContext* context, const u16* toonTable)
{
    const VkBuffer toonBuffer = context != nullptr ? context->ToonBuffer : ToonBuffer;
    const VkDeviceMemory toonMemory = context != nullptr ? context->ToonMemory : ToonMemory;
    const void* toonMapped = context != nullptr ? context->ToonMapped : ToonMapped;

    if (toonBuffer == VK_NULL_HANDLE || toonMemory == VK_NULL_HANDLE || toonMapped == nullptr)
        return false;

    u32* packedToon = reinterpret_cast<u32*>(const_cast<void*>(toonMapped));
    for (u32 i = 0; i < ToonTableEntryCount; i++)
    {
        const u32 toonColor = toonTable != nullptr ? static_cast<u32>(toonTable[i]) : 0u;
        u32 r = (toonColor << 1u) & 0x3Eu;
        u32 g = (toonColor >> 4u) & 0x3Eu;
        u32 b = (toonColor >> 9u) & 0x3Eu;
        if (r) r++;
        if (g) g++;
        if (b) b++;
        packedToon[i] = (r & 0x3Fu) | ((g & 0x3Fu) << 8u) | ((b & 0x3Fu) << 16u);
    }

    return true;
}

bool VulkanRenderer3D::ensureGraphicsClearBuffer(RenderContext* context)
{
    VkBuffer& clearBuffer = context != nullptr ? context->ClearBuffer : ClearBuffer;
    VkDeviceMemory& clearMemory = context != nullptr ? context->ClearMemory : ClearMemory;
    VkDeviceSize& clearBufferSize = context != nullptr ? context->ClearBufferSize : ClearBufferSize;
    void*& clearMapped = context != nullptr ? context->ClearMapped : ClearMapped;
    constexpr VkDeviceSize requiredSize = static_cast<VkDeviceSize>(256u * 192u * 3u) * sizeof(u32);

    if (clearBuffer != VK_NULL_HANDLE && clearBufferSize >= requiredSize && clearMapped != nullptr)
    {
        updateGraphicsDescriptorSet(context);
        return true;
    }

    destroyGraphicsClearBuffer(context);

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            requiredSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            clearBuffer,
            clearMemory,
            &clearMapped))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate graphics clear buffer");
        destroyGraphicsClearBuffer(context);
        return false;
    }

    clearBufferSize = requiredSize;
    updateGraphicsDescriptorSet(context);
    return true;
}

void VulkanRenderer3D::destroyGraphicsClearBuffer(RenderContext* context)
{
    if (context != nullptr)
        invalidateGraphicsDescriptorSetCache(context);
    else
        invalidateAllGraphicsDescriptorSetCaches();

    VkBuffer& clearBuffer = context != nullptr ? context->ClearBuffer : ClearBuffer;
    VkDeviceMemory& clearMemory = context != nullptr ? context->ClearMemory : ClearMemory;
    VkDeviceSize& clearBufferSize = context != nullptr ? context->ClearBufferSize : ClearBufferSize;
    void*& clearMapped = context != nullptr ? context->ClearMapped : ClearMapped;

    if (clearMapped != nullptr && clearMemory != VK_NULL_HANDLE)
    {
        vkUnmapMemory(Device, clearMemory);
        clearMapped = nullptr;
    }

    if (clearBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, clearBuffer, nullptr);
        clearBuffer = VK_NULL_HANDLE;
    }

    if (clearMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, clearMemory, nullptr);
        clearMemory = VK_NULL_HANDLE;
    }

    clearBufferSize = 0;
}

bool VulkanRenderer3D::updateGraphicsClearBuffer(RenderContext* context, const melonDS::GPU& gpu)
{
    const void* clearMapped = context != nullptr ? context->ClearMapped : ClearMapped;
    const VkBuffer clearBuffer = context != nullptr ? context->ClearBuffer : ClearBuffer;
    const VkDeviceMemory clearMemory = context != nullptr ? context->ClearMemory : ClearMemory;

    if (clearBuffer == VK_NULL_HANDLE || clearMemory == VK_NULL_HANDLE || clearMapped == nullptr)
        return false;

    u32* clearWords = reinterpret_cast<u32*>(const_cast<void*>(clearMapped));
    constexpr size_t clearPixelCount = 256u * 192u;
    u32* clearColorWords = clearWords;
    u32* clearAttrWords = clearWords + clearPixelCount;
    u32* clearDepthWords = clearWords + (clearPixelCount * 2u);

    const u32 clearAttr1 = gpu.GPU3D.RenderClearAttr1;
    const u32 clearAttr2 = gpu.GPU3D.RenderClearAttr2;
    const u32 clearPolyId = (clearAttr1 >> 24u) & 0x3Fu;
    const u32 clearFogFlag = (clearAttr1 >> 15u) & 0x1u;
    const u32 clearColor = Debug3dClearMagenta ? 0xFFFF00FFu : buildClearColorRgba8(gpu);
    const u32 clearDepth = ((clearAttr2 & 0x7FFFu) * 0x200u) + 0x1FFu;
    const u32 clearPolyIdByte = ((clearPolyId * 255u) + 31u) / 63u;
    const u32 clearFogByte = clearFogFlag != 0u ? 0xFFu : 0u;
    const u32 plainAttr = clearPolyIdByte | (clearFogByte << 16u) | 0xFF000000u;
    const u32 clearDepthBits = BitCastFloatToU32(static_cast<float>(clearDepth) * (1.0f / 16777215.0f));

    if ((gpu.GPU3D.RenderDispCnt & (1u << 14u)) == 0u)
    {
        std::fill_n(clearColorWords, clearPixelCount, clearColor);
        std::fill_n(clearAttrWords, clearPixelCount, plainAttr);
        std::fill_n(clearDepthWords, clearPixelCount, clearDepthBits);
        return true;
    }

    u8 baseXOff = (clearAttr2 >> 16u) & 0xFFu;
    u8 yOff = (clearAttr2 >> 24u) & 0xFFu;

    for (u32 y = 0; y < 192u; y++)
    {
        u8 xOff = baseXOff;
        for (u32 x = 0; x < 256u; x++)
        {
            const size_t offset = static_cast<size_t>(y) * 256u + x;
            const u16 colorSource = gpu.ReadVRAMFlat_Texture<u16>(0x40000u + (static_cast<u32>(yOff) << 9u) + (static_cast<u32>(xOff) << 1u));
            const u16 depthSource = gpu.ReadVRAMFlat_Texture<u16>(0x60000u + (static_cast<u32>(yOff) << 9u) + (static_cast<u32>(xOff) << 1u));

            u32 r = (static_cast<u32>(colorSource) << 1u) & 0x3Eu;
            u32 g = (static_cast<u32>(colorSource) >> 4u) & 0x3Eu;
            u32 b = (static_cast<u32>(colorSource) >> 9u) & 0x3Eu;
            if (r) r++;
            if (g) g++;
            if (b) b++;
            const u32 a = (colorSource & 0x8000u) != 0u ? 0x1Fu : 0u;

            const u32 r8 = (r << 2u) | (r >> 4u);
            const u32 g8 = (g << 2u) | (g >> 4u);
            const u32 b8 = (b << 2u) | (b >> 4u);
            const u32 a8 = (a << 3u) | (a >> 2u);
            clearColorWords[offset] = Debug3dClearMagenta ? 0xFFFF00FFu : (r8 | (g8 << 8u) | (b8 << 16u) | (a8 << 24u));

            const u32 fogByte = (depthSource & 0x8000u) != 0u ? 0xFFu : 0u;
            clearAttrWords[offset] = clearPolyIdByte | (fogByte << 16u) | 0xFF000000u;

            const u32 pixelDepth = ((static_cast<u32>(depthSource & 0x7FFFu)) * 0x200u) + 0x1FFu;
            clearDepthWords[offset] = BitCastFloatToU32(static_cast<float>(pixelDepth) * (1.0f / 16777215.0f));

            xOff++;
        }

        yOff++;
    }

    return true;
}

bool VulkanRenderer3D::ensureCaptureLineBuffer(RenderContext* context)
{
    if (context == nullptr)
        syncActiveCaptureLineBufferSlot();

    VkBuffer& captureLineBuffer = context != nullptr ? context->CaptureLineBuffer : CaptureLineBuffer;
    VkDeviceMemory& captureLineMemory = context != nullptr ? context->CaptureLineMemory : CaptureLineMemory;
    VkDeviceSize& captureLineBufferSize = context != nullptr ? context->CaptureLineBufferSize : CaptureLineBufferSize;
    void*& captureLineMapped = context != nullptr ? context->CaptureLineMapped : CaptureLineMapped;
    constexpr VkDeviceSize requiredSize = static_cast<VkDeviceSize>(256u * 192u) * sizeof(u32);

    if (captureLineBuffer != VK_NULL_HANDLE && captureLineBufferSize >= requiredSize && captureLineMapped != nullptr)
    {
        updateDescriptorSet(context);
        return true;
    }

    destroyCaptureLineBuffer(context);

    if (!createBufferAllocation(
            Device,
            [&](u32 typeBits, VkMemoryPropertyFlags properties) { return findMemoryType(typeBits, properties); },
            requiredSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            captureLineBuffer,
            captureLineMemory,
            &captureLineMapped))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate capture line buffer");
        destroyCaptureLineBuffer(context);
        return false;
    }

    std::memset(captureLineMapped, 0, static_cast<size_t>(requiredSize));
    captureLineBufferSize = requiredSize;
    if (context == nullptr)
        storeActiveCaptureLineBufferSlot();
    updateDescriptorSet(context);
    return true;
}

void VulkanRenderer3D::destroyCaptureLineBuffer(RenderContext* context)
{
    if (context != nullptr)
        invalidateDescriptorSetCache(context);
    else
        invalidateAllDescriptorSetCaches();

    VkBuffer& captureLineBuffer = context != nullptr ? context->CaptureLineBuffer : CaptureLineBuffer;
    VkDeviceMemory& captureLineMemory = context != nullptr ? context->CaptureLineMemory : CaptureLineMemory;
    VkDeviceSize& captureLineBufferSize = context != nullptr ? context->CaptureLineBufferSize : CaptureLineBufferSize;
    void*& captureLineMapped = context != nullptr ? context->CaptureLineMapped : CaptureLineMapped;

    if (captureLineMapped != nullptr && captureLineMemory != VK_NULL_HANDLE)
    {
        vkUnmapMemory(Device, captureLineMemory);
        captureLineMapped = nullptr;
    }

    if (captureLineBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, captureLineBuffer, nullptr);
        captureLineBuffer = VK_NULL_HANDLE;
    }

    if (captureLineMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, captureLineMemory, nullptr);
        captureLineMemory = VK_NULL_HANDLE;
    }

    captureLineBufferSize = 0;

    if (context == nullptr)
    {
        storeActiveCaptureLineBufferSlot();
        resetCaptureLineState();
    }
}

void VulkanRenderer3D::destroyAllCaptureLineBuffers()
{
    const u32 previousSlot = ActiveCaptureLineBufferSlot;
    for (u32 slot = 0; slot < CaptureLineBufferSlotCount; slot++)
    {
        selectActiveCaptureLineBufferSlot(slot);
        destroyCaptureLineBuffer(nullptr);
    }
    ActiveCaptureLineBufferSlot = std::min<u32>(previousSlot, CaptureLineBufferSlotCount - 1u);
    syncActiveCaptureLineBufferSlot();
    resetCaptureLineState();
}

bool VulkanRenderer3D::createFallbackTexture()
{
    destroyFallbackTexture();

    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UINT;
    imageCreateInfo.extent.width = 1;
    imageCreateInfo.extent.height = 1;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(Device, &imageCreateInfo, nullptr, &FallbackTextureImage) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create fallback texture image");
        return false;
    }

    VkMemoryRequirements imageRequirements{};
    vkGetImageMemoryRequirements(Device, FallbackTextureImage, &imageRequirements);

    VkMemoryAllocateInfo imageMemoryAllocateInfo{};
    imageMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageMemoryAllocateInfo.allocationSize = imageRequirements.size;
    imageMemoryAllocateInfo.memoryTypeIndex = findMemoryType(imageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (imageMemoryAllocateInfo.memoryTypeIndex == UINT32_MAX)
        imageMemoryAllocateInfo.memoryTypeIndex = findMemoryType(imageRequirements.memoryTypeBits, 0);
    if (imageMemoryAllocateInfo.memoryTypeIndex == UINT32_MAX
        || vkAllocateMemory(Device, &imageMemoryAllocateInfo, nullptr, &FallbackTextureMemory) != VK_SUCCESS
        || vkBindImageMemory(Device, FallbackTextureImage, FallbackTextureMemory, 0) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate fallback texture memory");
        destroyFallbackTexture();
        return false;
    }

    VkImageViewCreateInfo imageViewCreateInfo{};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = FallbackTextureImage;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_UINT;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(Device, &imageViewCreateInfo, nullptr, &FallbackTextureView) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create fallback texture view");
        destroyFallbackTexture();
        return false;
    }

    VkSamplerCreateInfo samplerCreateInfo{};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
    samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.anisotropyEnable = VK_FALSE;
    samplerCreateInfo.maxAnisotropy = 1.0f;
    samplerCreateInfo.compareEnable = VK_FALSE;
    samplerCreateInfo.minLod = 0.0f;
    samplerCreateInfo.maxLod = 0.0f;
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    if (vkCreateSampler(Device, &samplerCreateInfo, nullptr, &FallbackTextureSampler) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create fallback texture sampler");
        destroyFallbackTexture();
        return false;
    }

    VkBufferCreateInfo stagingCreateInfo{};
    stagingCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingCreateInfo.size = sizeof(u32);
    stagingCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Device, &stagingCreateInfo, nullptr, &FallbackTextureStagingBuffer) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create fallback staging buffer");
        destroyFallbackTexture();
        return false;
    }

    VkMemoryRequirements stagingRequirements{};
    vkGetBufferMemoryRequirements(Device, FallbackTextureStagingBuffer, &stagingRequirements);

    VkMemoryAllocateInfo stagingMemoryAllocateInfo{};
    stagingMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingMemoryAllocateInfo.allocationSize = stagingRequirements.size;
    stagingMemoryAllocateInfo.memoryTypeIndex = findMemoryType(
        stagingRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if (stagingMemoryAllocateInfo.memoryTypeIndex == UINT32_MAX
        || vkAllocateMemory(Device, &stagingMemoryAllocateInfo, nullptr, &FallbackTextureStagingMemory) != VK_SUCCESS
        || vkBindBufferMemory(Device, FallbackTextureStagingBuffer, FallbackTextureStagingMemory, 0) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate fallback staging memory");
        destroyFallbackTexture();
        return false;
    }

    void* mappedMemory = nullptr;
    if (vkMapMemory(Device, FallbackTextureStagingMemory, 0, sizeof(u32), 0, &mappedMemory) != VK_SUCCESS)
    {
        destroyFallbackTexture();
        return false;
    }

    *reinterpret_cast<u32*>(mappedMemory) = 0x1F3F3F3Fu;
    vkUnmapMemory(Device, FallbackTextureStagingMemory);

    if (vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS
        || vkResetFences(Device, 1, &FrameFence) != VK_SUCCESS
        || vkResetCommandBuffer(CommandBuffer, 0) != VK_SUCCESS)
    {
        destroyFallbackTexture();
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(CommandBuffer, &beginInfo) != VK_SUCCESS)
    {
        destroyFallbackTexture();
        return false;
    }

    VkImageMemoryBarrier toTransferBarrier{};
    toTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferBarrier.srcAccessMask = 0;
    toTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferBarrier.image = FallbackTextureImage;
    toTransferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransferBarrier.subresourceRange.baseMipLevel = 0;
    toTransferBarrier.subresourceRange.levelCount = 1;
    toTransferBarrier.subresourceRange.baseArrayLayer = 0;
    toTransferBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toTransferBarrier
    );

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = {0, 0, 0};
    copyRegion.imageExtent.width = 1;
    copyRegion.imageExtent.height = 1;
    copyRegion.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(
        CommandBuffer,
        FallbackTextureStagingBuffer,
        FallbackTextureImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion
    );

    VkImageMemoryBarrier toGeneralBarrier{};
    toGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneralBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toGeneralBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toGeneralBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneralBarrier.image = FallbackTextureImage;
    toGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toGeneralBarrier.subresourceRange.baseMipLevel = 0;
    toGeneralBarrier.subresourceRange.levelCount = 1;
    toGeneralBarrier.subresourceRange.baseArrayLayer = 0;
    toGeneralBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toGeneralBarrier
    );

    if (vkEndCommandBuffer(CommandBuffer) != VK_SUCCESS)
    {
        destroyFallbackTexture();
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &CommandBuffer;
    {
        std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
        const VkResult submitResult = vkQueueSubmit(Queue, 1, &submitInfo, FrameFence);
        if (submitResult != VK_SUCCESS)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: fallback texture vkQueueSubmit failed (%d)", static_cast<int>(submitResult));
            destroyFallbackTexture();
            return false;
        }
    }

    if (vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
    {
        destroyFallbackTexture();
        return false;
    }

    return true;
}

void VulkanRenderer3D::destroyFallbackTexture()
{
    invalidateAllDescriptorSetCaches();

    if (FallbackTextureSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(Device, FallbackTextureSampler, nullptr);
        FallbackTextureSampler = VK_NULL_HANDLE;
    }

    if (FallbackTextureView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(Device, FallbackTextureView, nullptr);
        FallbackTextureView = VK_NULL_HANDLE;
    }

    if (FallbackTextureImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(Device, FallbackTextureImage, nullptr);
        FallbackTextureImage = VK_NULL_HANDLE;
    }

    if (FallbackTextureMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, FallbackTextureMemory, nullptr);
        FallbackTextureMemory = VK_NULL_HANDLE;
    }

    if (FallbackTextureStagingBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, FallbackTextureStagingBuffer, nullptr);
        FallbackTextureStagingBuffer = VK_NULL_HANDLE;
    }

    if (FallbackTextureStagingMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, FallbackTextureStagingMemory, nullptr);
        FallbackTextureStagingMemory = VK_NULL_HANDLE;
    }
}

bool VulkanRenderer3D::createReadbackBuffer(u32 width, u32 height)
{
    ReadbackSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * sizeof(u32);

    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = ReadbackSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(Device, &bufferCreateInfo, nullptr, &ReadbackBuffer) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create readback buffer");
        return false;
    }

    VkMemoryRequirements bufferMemoryRequirements{};
    vkGetBufferMemoryRequirements(Device, ReadbackBuffer, &bufferMemoryRequirements);

    VkMemoryAllocateInfo memoryAllocateInfo{};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.allocationSize = bufferMemoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = findMemoryType(
        bufferMemoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (memoryAllocateInfo.memoryTypeIndex == UINT32_MAX)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: unable to find memory type for readback buffer");
        destroyReadbackBuffer();
        return false;
    }

    if (vkAllocateMemory(Device, &memoryAllocateInfo, nullptr, &ReadbackMemory) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate readback memory");
        destroyReadbackBuffer();
        return false;
    }

    if (vkBindBufferMemory(Device, ReadbackBuffer, ReadbackMemory, 0) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to bind readback memory");
        destroyReadbackBuffer();
        return false;
    }

    if (vkMapMemory(Device, ReadbackMemory, 0, ReadbackSize, 0, &ReadbackMapped) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to map readback memory");
        destroyReadbackBuffer();
        return false;
    }

    RawReadbackRgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    RawReadbackWidth = width;
    RawReadbackHeight = height;
    return true;
}

void VulkanRenderer3D::destroyReadbackBuffer()
{
    if (ReadbackMapped != nullptr && ReadbackMemory != VK_NULL_HANDLE)
    {
        vkUnmapMemory(Device, ReadbackMemory);
        ReadbackMapped = nullptr;
    }

    if (ReadbackBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, ReadbackBuffer, nullptr);
        ReadbackBuffer = VK_NULL_HANDLE;
    }

    if (ReadbackMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, ReadbackMemory, nullptr);
        ReadbackMemory = VK_NULL_HANDLE;
    }

    ReadbackSize = 0;
    RawReadbackWidth = 0;
    RawReadbackHeight = 0;
    RawReadbackRgba.clear();
}

bool VulkanRenderer3D::ensureCaptureReadbackImage()
{
    constexpr u32 kCaptureReadbackWidth = 256u;
    constexpr u32 kCaptureReadbackHeight = 192u;

    if (CaptureReadbackImage != VK_NULL_HANDLE)
        return true;

    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageCreateInfo.extent.width = kCaptureReadbackWidth;
    imageCreateInfo.extent.height = kCaptureReadbackHeight;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(Device, &imageCreateInfo, nullptr, &CaptureReadbackImage) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create capture readback image");
        return false;
    }

    VkMemoryRequirements imageMemoryRequirements{};
    vkGetImageMemoryRequirements(Device, CaptureReadbackImage, &imageMemoryRequirements);

    VkMemoryAllocateInfo memoryAllocateInfo{};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.allocationSize = imageMemoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = VulkanContext::Get().FindMemoryType(
        imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryAllocateInfo.memoryTypeIndex == UINT32_MAX)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: unable to find memory type for capture readback image");
        destroyCaptureReadbackImage();
        return false;
    }

    if (vkAllocateMemory(Device, &memoryAllocateInfo, nullptr, &CaptureReadbackMemory) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate capture readback image memory");
        destroyCaptureReadbackImage();
        return false;
    }

    if (vkBindImageMemory(Device, CaptureReadbackImage, CaptureReadbackMemory, 0) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to bind capture readback image memory");
        destroyCaptureReadbackImage();
        return false;
    }

    CaptureReadbackImageInitialized = false;
    CaptureReadbackImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void VulkanRenderer3D::destroyCaptureReadbackImage()
{
    if (CaptureReadbackImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(Device, CaptureReadbackImage, nullptr);
        CaptureReadbackImage = VK_NULL_HANDLE;
    }

    if (CaptureReadbackMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, CaptureReadbackMemory, nullptr);
        CaptureReadbackMemory = VK_NULL_HANDLE;
    }

    CaptureReadbackImageInitialized = false;
    CaptureReadbackImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

bool VulkanRenderer3D::createResultReadbackBuffer()
{
    ResultReadbackSize = ResultBufferSize;

    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = ResultReadbackSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(Device, &bufferCreateInfo, nullptr, &ResultReadbackBuffer) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to create result readback buffer");
        return false;
    }

    VkMemoryRequirements bufferMemoryRequirements{};
    vkGetBufferMemoryRequirements(Device, ResultReadbackBuffer, &bufferMemoryRequirements);

    VkMemoryAllocateInfo memoryAllocateInfo{};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.allocationSize = bufferMemoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = findMemoryType(
        bufferMemoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if (memoryAllocateInfo.memoryTypeIndex == UINT32_MAX)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: unable to find memory type for result readback buffer");
        destroyResultReadbackBuffer();
        return false;
    }

    if (vkAllocateMemory(Device, &memoryAllocateInfo, nullptr, &ResultReadbackMemory) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to allocate result readback memory");
        destroyResultReadbackBuffer();
        return false;
    }

    if (vkBindBufferMemory(Device, ResultReadbackBuffer, ResultReadbackMemory, 0) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to bind result readback memory");
        destroyResultReadbackBuffer();
        return false;
    }

    if (vkMapMemory(Device, ResultReadbackMemory, 0, ResultReadbackSize, 0, &ResultReadbackMapped) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to map result readback memory");
        destroyResultReadbackBuffer();
        return false;
    }

    RawResultReadback.assign(static_cast<size_t>(ResultReadbackSize / sizeof(u32)), 0u);
    return true;
}

void VulkanRenderer3D::destroyResultReadbackBuffer()
{
    if (ResultReadbackMapped != nullptr && ResultReadbackMemory != VK_NULL_HANDLE)
    {
        vkUnmapMemory(Device, ResultReadbackMemory);
        ResultReadbackMapped = nullptr;
    }

    if (ResultReadbackBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Device, ResultReadbackBuffer, nullptr);
        ResultReadbackBuffer = VK_NULL_HANDLE;
    }

    if (ResultReadbackMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, ResultReadbackMemory, nullptr);
        ResultReadbackMemory = VK_NULL_HANDLE;
    }

    ResultReadbackSize = 0;
    RawResultReadback.clear();
}

bool VulkanRenderer3D::descriptorImageInfoEquals(const VkDescriptorImageInfo& lhs, const VkDescriptorImageInfo& rhs)
{
    return lhs.sampler == rhs.sampler
        && lhs.imageView == rhs.imageView
        && lhs.imageLayout == rhs.imageLayout;
}

bool VulkanRenderer3D::usesSingleDescriptorTexturePath() const noexcept
{
    // Base fallback now uses a switch-selected descriptor array in a single raster pass.
    return false;
}

u32 VulkanRenderer3D::getTextureBindingDescriptorCount() const noexcept
{
    return MaxTextureDescriptors;
}

VulkanRenderer3D::BackendMode VulkanRenderer3D::resolveRequestedBackendMode() const noexcept
{
    return BackendMode::GraphicsHardware;
}

void VulkanRenderer3D::refreshActiveBackendMode() noexcept
{
    ActiveBackendMode = resolveRequestedBackendMode();
}

void VulkanRenderer3D::InvalidatePresentationState(bool discardColorTarget) noexcept
{
    FrameIdentical = false;
    HasCpuFrame = false;
    LastSubmittedRenderPolygonCount = 0;
    CaptureDebugLogsRemaining = MelonDSAndroid::areRendererDebugToolsEnabled() ? 48u : 0u;
    CaptureReadbackPending = false;
    PendingCaptureReadbackContext = nullptr;
    RawReadbackWidth = 0;
    RawReadbackHeight = 0;
    RawReadbackRgba.clear();
    CaptureReadbackImageInitialized = false;
    resetCaptureLineState();
    clearLineCache();
    LastSubmittedRenderContext = nullptr;
    if (discardColorTarget)
        ColorImageInitialized = false;
}

VulkanRenderer3D::TextureSamplingPath VulkanRenderer3D::resolveTextureSamplingPath() const noexcept
{
    if (!VulkanContext::Get().SupportsDynamicTextureIndexing())
        return TextureSamplingPath::BaseSingleDescriptor;
    return VulkanContext::Get().SupportsNonUniformTextureIndexing()
        ? TextureSamplingPath::NonUniform
        : TextureSamplingPath::CompatDynamicUniform;
}

const char* VulkanRenderer3D::textureSamplingPathName(TextureSamplingPath path) noexcept
{
    switch (path)
    {
        case TextureSamplingPath::BaseSingleDescriptor:
            return "base-switch-descriptor";
        case TextureSamplingPath::CompatDynamicUniform:
            return "compat-dynamic-uniform";
        case TextureSamplingPath::NonUniform:
            return "nonuniform";
    }
    return "unknown";
}

const char* VulkanRenderer3D::backendModeName(BackendMode mode) noexcept
{
    switch (mode)
    {
        case BackendMode::GraphicsHardware:
            return "simple_graphics";
    }
    return "unknown";
}

const char* VulkanRenderer3D::rasterExecutionProfileName(RasterExecutionProfile profile) noexcept
{
    switch (profile)
    {
        case RasterExecutionProfile::AdrenoCpuDense:
            return "adreno_cpu_dense";
        case RasterExecutionProfile::AdrenoCpuSparse:
            return "adreno_cpu_sparse";
        case RasterExecutionProfile::MaliDenseScan:
            return "mali_dense_scan";
        case RasterExecutionProfile::MaliCpuDense:
            return "mali_cpu_dense";
        case RasterExecutionProfile::GeneralNonUniform:
            return "general_nonuniform";
        case RasterExecutionProfile::LegacyFallback:
            return "legacy_fallback";
        case RasterExecutionProfile::Count:
            break;
    }
    return "unknown";
}

const char* VulkanRenderer3D::rasterSceneModeName(RasterSceneMode mode) noexcept
{
    switch (mode)
    {
        case RasterSceneMode::DenseNoBoundary:
            return "dense_no_boundary";
        case RasterSceneMode::DenseBoundary:
            return "dense_boundary";
        case RasterSceneMode::SparseActive:
            return "sparse_active";
        case RasterSceneMode::Count:
            break;
    }
    return "unknown";
}

const char* VulkanRenderer3D::rasterTileLoopModeName(RasterTileLoopMode mode) noexcept
{
    switch (mode)
    {
        case RasterTileLoopMode::DenseGroupList:
            return "dense_group_list";
        case RasterTileLoopMode::SparseActive:
            return "sparse_active";
        case RasterTileLoopMode::LegacyWorklist:
            return "legacy_worklist";
        case RasterTileLoopMode::Count:
            break;
    }
    return "unknown";
}

const char* VulkanRenderer3D::capturePathModeName(CapturePathMode mode) noexcept
{
    switch (mode)
    {
        case CapturePathMode::Disabled:
            return "disabled";
        case CapturePathMode::CaptureLineExport:
            return "capture_line";
        case CapturePathMode::FallbackReadback:
            return "fallback_readback";
        case CapturePathMode::Count:
            break;
    }
    return "unknown";
}

VkDescriptorSet VulkanRenderer3D::getDescriptorSet(RenderContext* context, u32 singleTextureDescriptorIndex) const
{
    if (!usesSingleDescriptorTexturePath())
        return context != nullptr ? context->DescriptorSet : DescriptorSet;

    const u32 resolvedDescriptorIndex = std::min<u32>(singleTextureDescriptorIndex, MaxTextureDescriptors - 1u);
    return context != nullptr
        ? context->SingleTextureDescriptorSets[resolvedDescriptorIndex]
        : SingleTextureDescriptorSets[resolvedDescriptorIndex];
}

VulkanRenderer3D::DescriptorSetCache& VulkanRenderer3D::getDescriptorSetCache(RenderContext* context, u32 singleTextureDescriptorIndex)
{
    if (!usesSingleDescriptorTexturePath())
        return context != nullptr ? context->DescriptorCache : DescriptorCache;

    const u32 resolvedDescriptorIndex = std::min<u32>(singleTextureDescriptorIndex, MaxTextureDescriptors - 1u);
    return context != nullptr
        ? context->SingleTextureDescriptorCaches[resolvedDescriptorIndex]
        : SingleTextureDescriptorCaches[resolvedDescriptorIndex];
}

VulkanRenderer3D::GraphicsDescriptorSetCache& VulkanRenderer3D::getGraphicsDescriptorSetCache(RenderContext* context)
{
    return context != nullptr ? context->GraphicsDescriptorCache : GraphicsDescriptorCache;
}

void VulkanRenderer3D::invalidateDescriptorSetCache(RenderContext* context)
{
    if (!usesSingleDescriptorTexturePath())
    {
        if (context != nullptr)
            context->DescriptorCache.Ready = false;
        else
            DescriptorCache.Ready = false;
        return;
    }

    if (context != nullptr)
    {
        for (DescriptorSetCache& cache : context->SingleTextureDescriptorCaches)
            cache.Ready = false;
    }
    else
    {
        for (DescriptorSetCache& cache : SingleTextureDescriptorCaches)
            cache.Ready = false;
    }
}

void VulkanRenderer3D::invalidateAllDescriptorSetCaches()
{
    DescriptorCache.Ready = false;
    for (DescriptorSetCache& cache : SingleTextureDescriptorCaches)
        cache.Ready = false;

    for (RenderContext& renderContext : RenderContexts)
    {
        renderContext.DescriptorCache.Ready = false;
        for (DescriptorSetCache& cache : renderContext.SingleTextureDescriptorCaches)
            cache.Ready = false;
    }
}

void VulkanRenderer3D::invalidateGraphicsDescriptorSetCache(RenderContext* context)
{
    getGraphicsDescriptorSetCache(context).Ready = false;
}

void VulkanRenderer3D::invalidateAllGraphicsDescriptorSetCaches()
{
    GraphicsDescriptorCache.Ready = false;
    for (RenderContext& renderContext : RenderContexts)
        renderContext.GraphicsDescriptorCache.Ready = false;
}

void VulkanRenderer3D::updateDescriptorSet(RenderContext* context, u32 singleTextureDescriptorIndex)
{
    const VkDescriptorSet DescriptorSet = getDescriptorSet(context, singleTextureDescriptorIndex);
    const VkBuffer TriangleBuffer = context != nullptr ? context->TriangleBuffer : this->TriangleBuffer;
    const VkBuffer BinMaskBuffer = context != nullptr && context->BinMaskBuffer != VK_NULL_HANDLE
        ? context->BinMaskBuffer
        : this->BinMaskBuffer;
    const VkBuffer GroupListBuffer = context != nullptr && context->GroupListBuffer != VK_NULL_HANDLE
        ? context->GroupListBuffer
        : this->GroupListBuffer;
    const VkBuffer SpanSetupBuffer = context != nullptr && context->SpanSetupBuffer != VK_NULL_HANDLE
        ? context->SpanSetupBuffer
        : this->SpanSetupBuffer;
    const VkBuffer WorkOffsetBuffer = context != nullptr && context->WorkOffsetBuffer != VK_NULL_HANDLE
        ? context->WorkOffsetBuffer
        : this->WorkOffsetBuffer;
    const VkBuffer ToonBuffer = context != nullptr ? context->ToonBuffer : this->ToonBuffer;
    const VkBuffer CaptureLineBuffer = context != nullptr ? context->CaptureLineBuffer : this->CaptureLineBuffer;

    if (DescriptorSet == VK_NULL_HANDLE
        || ColorImageView == VK_NULL_HANDLE
        || TriangleBuffer == VK_NULL_HANDLE
        || ResultBuffer == VK_NULL_HANDLE
        || BinMaskBuffer == VK_NULL_HANDLE
        || GroupListBuffer == VK_NULL_HANDLE
        || SpanSetupBuffer == VK_NULL_HANDLE
        || WorkOffsetBuffer == VK_NULL_HANDLE
        || ToonBuffer == VK_NULL_HANDLE
        || CaptureLineBuffer == VK_NULL_HANDLE
        || FallbackTextureView == VK_NULL_HANDLE
        || FallbackTextureSampler == VK_NULL_HANDLE)
        return;

    DescriptorSetCache& descriptorCache = getDescriptorSetCache(context, singleTextureDescriptorIndex);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = ColorImageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo triangleInfo{};
    triangleInfo.buffer = TriangleBuffer;
    triangleInfo.offset = 0;
    triangleInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo resultInfo{};
    resultInfo.buffer = ResultBuffer;
    resultInfo.offset = 0;
    resultInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo binMaskInfo{};
    binMaskInfo.buffer = BinMaskBuffer;
    binMaskInfo.offset = 0;
    binMaskInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo groupListInfo{};
    groupListInfo.buffer = GroupListBuffer;
    groupListInfo.offset = 0;
    groupListInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo toonInfo{};
    toonInfo.buffer = ToonBuffer;
    toonInfo.offset = 0;
    toonInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo spanSetupInfo{};
    spanSetupInfo.buffer = SpanSetupBuffer;
    spanSetupInfo.offset = 0;
    spanSetupInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo workOffsetInfo{};
    workOffsetInfo.buffer = WorkOffsetBuffer;
    workOffsetInfo.offset = 0;
    workOffsetInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo captureLineInfo{};
    captureLineInfo.buffer = CaptureLineBuffer;
    captureLineInfo.offset = 0;
    captureLineInfo.range = VK_WHOLE_SIZE;

    std::array<VkDescriptorImageInfo, MaxTextureDescriptors> textureInfos{};
    VkDescriptorImageInfo fallbackInfo{};
    fallbackInfo.sampler = FallbackTextureSampler;
    fallbackInfo.imageView = FallbackTextureView;
    fallbackInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    textureInfos.fill(fallbackInfo);

    if (usesSingleDescriptorTexturePath())
    {
        const u32 resolvedDescriptorIndex = std::min<u32>(singleTextureDescriptorIndex, MaxTextureDescriptors - 1u);
        if (resolvedDescriptorIndex < ActiveTextureDescriptorCount && resolvedDescriptorIndex < MaxActiveTextureDescriptors)
            textureInfos[0] = ActiveTextureDescriptors[resolvedDescriptorIndex];
    }
    else
    {
        for (u32 i = 0; i < ActiveTextureDescriptorCount && i < MaxActiveTextureDescriptors; i++)
            textureInfos[i] = ActiveTextureDescriptors[i];
    }
    const u32 textureDescriptorCount = getTextureBindingDescriptorCount();

    std::array<VkWriteDescriptorSet, 10> writes{};
    u32 writeCount = 0;
    if (!descriptorCache.Ready || descriptorCache.ColorImageView != ColorImageView)
    {
        writes[writeCount++] = makeImageDescriptorWrite(DescriptorSet, 0, &imageInfo, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }

    if (!descriptorCache.Ready || descriptorCache.TriangleBuffer != TriangleBuffer)
    {
        writes[writeCount++] = makeBufferDescriptorWrite(DescriptorSet, 1, &triangleInfo);
    }

    bool texturesChanged = !descriptorCache.Ready;
    if (!texturesChanged)
    {
        for (u32 i = 0; i < textureDescriptorCount; i++)
        {
            if (!descriptorImageInfoEquals(descriptorCache.TextureInfos[i], textureInfos[i]))
            {
                texturesChanged = true;
                break;
            }
        }
    }
    if (texturesChanged)
    {
        writes[writeCount++] = makeImageDescriptorWrite(
            DescriptorSet,
            2,
            textureInfos.data(),
            textureDescriptorCount,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    }

    if (!descriptorCache.Ready || descriptorCache.ResultBuffer != ResultBuffer)
    {
        writes[writeCount++] = makeBufferDescriptorWrite(DescriptorSet, 3, &resultInfo);
    }
    if (!descriptorCache.Ready || descriptorCache.BinMaskBuffer != BinMaskBuffer)
    {
        writes[writeCount++] = makeBufferDescriptorWrite(DescriptorSet, 4, &binMaskInfo);
    }
    if (!descriptorCache.Ready || descriptorCache.GroupListBuffer != GroupListBuffer)
    {
        writes[writeCount++] = makeBufferDescriptorWrite(DescriptorSet, 5, &groupListInfo);
    }
    if (!descriptorCache.Ready || descriptorCache.ToonBuffer != ToonBuffer)
    {
        writes[writeCount++] = makeBufferDescriptorWrite(DescriptorSet, 6, &toonInfo);
    }
    if (!descriptorCache.Ready || descriptorCache.SpanSetupBuffer != SpanSetupBuffer)
    {
        writes[writeCount++] = makeBufferDescriptorWrite(DescriptorSet, 7, &spanSetupInfo);
    }
    if (!descriptorCache.Ready || descriptorCache.WorkOffsetBuffer != WorkOffsetBuffer)
    {
        writes[writeCount++] = makeBufferDescriptorWrite(DescriptorSet, 8, &workOffsetInfo);
    }
    if (!descriptorCache.Ready || descriptorCache.CaptureLineBuffer != CaptureLineBuffer)
    {
        writes[writeCount++] = makeBufferDescriptorWrite(DescriptorSet, 9, &captureLineInfo);
    }

    if (writeCount > 0)
        vkUpdateDescriptorSets(Device, writeCount, writes.data(), 0, nullptr);

    descriptorCache.Ready = true;
    descriptorCache.ColorImageView = ColorImageView;
    descriptorCache.TriangleBuffer = TriangleBuffer;
    descriptorCache.FallbackTextureView = FallbackTextureView;
    descriptorCache.FallbackTextureSampler = FallbackTextureSampler;
    descriptorCache.ResultBuffer = ResultBuffer;
    descriptorCache.BinMaskBuffer = BinMaskBuffer;
    descriptorCache.GroupListBuffer = GroupListBuffer;
    descriptorCache.ToonBuffer = ToonBuffer;
    descriptorCache.SpanSetupBuffer = SpanSetupBuffer;
    descriptorCache.WorkOffsetBuffer = WorkOffsetBuffer;
    descriptorCache.CaptureLineBuffer = CaptureLineBuffer;
    descriptorCache.TextureInfos = textureInfos;
}

bool VulkanRenderer3D::updateCaptureExportDescriptorSet(RenderContext* context)
{
    const VkDescriptorSet descriptorSet = getDescriptorSet(context, FallbackTextureDescriptorIndex);
    const VkBuffer captureLineBuffer = context != nullptr ? context->CaptureLineBuffer : this->CaptureLineBuffer;
    if (descriptorSet == VK_NULL_HANDLE || ColorImageView == VK_NULL_HANDLE || captureLineBuffer == VK_NULL_HANDLE)
        return false;

    DescriptorSetCache& descriptorCache = getDescriptorSetCache(context, FallbackTextureDescriptorIndex);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = ColorImageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo captureLineInfo{};
    captureLineInfo.buffer = captureLineBuffer;
    captureLineInfo.offset = 0;
    captureLineInfo.range = VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet, 2> writes{};
    u32 writeCount = 0;
    if (!descriptorCache.Ready || descriptorCache.ColorImageView != ColorImageView)
        writes[writeCount++] = makeImageDescriptorWrite(descriptorSet, 0, &imageInfo, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    if (!descriptorCache.Ready || descriptorCache.CaptureLineBuffer != captureLineBuffer)
        writes[writeCount++] = makeBufferDescriptorWrite(descriptorSet, 9, &captureLineInfo);

    if (writeCount > 0)
        vkUpdateDescriptorSets(Device, writeCount, writes.data(), 0, nullptr);

    descriptorCache.Ready = true;
    descriptorCache.ColorImageView = ColorImageView;
    descriptorCache.CaptureLineBuffer = captureLineBuffer;
    return true;
}

void VulkanRenderer3D::updateGraphicsDescriptorSet(RenderContext* context)
{
    const VkDescriptorSet descriptorSet = context != nullptr ? context->GraphicsDescriptorSet : GraphicsDescriptorSet;
    const VkBuffer triangleBuffer = context != nullptr ? context->TriangleBuffer : TriangleBuffer;
    const VkBuffer toonBuffer = context != nullptr ? context->ToonBuffer : ToonBuffer;
    const VkBuffer clearBuffer = context != nullptr ? context->ClearBuffer : ClearBuffer;

    if (descriptorSet == VK_NULL_HANDLE
        || triangleBuffer == VK_NULL_HANDLE
        || toonBuffer == VK_NULL_HANDLE
        || clearBuffer == VK_NULL_HANDLE
        || AttrImageView == VK_NULL_HANDLE
        || DepthImageView == VK_NULL_HANDLE
        || GraphicsAttachmentSampler == VK_NULL_HANDLE)
    {
        return;
    }

    GraphicsDescriptorSetCache& descriptorCache = getGraphicsDescriptorSetCache(context);

    VkDescriptorBufferInfo triangleInfo{};
    triangleInfo.buffer = triangleBuffer;
    triangleInfo.offset = 0;
    triangleInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo toonInfo{};
    toonInfo.buffer = toonBuffer;
    toonInfo.offset = 0;
    toonInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo clearInfo{};
    clearInfo.buffer = clearBuffer;
    clearInfo.offset = 0;
    clearInfo.range = VK_WHOLE_SIZE;

    std::array<VkDescriptorImageInfo, MaxTextureDescriptors> textureInfos{};
    VkDescriptorImageInfo fallbackInfo{};
    fallbackInfo.sampler = FallbackTextureSampler;
    fallbackInfo.imageView = FallbackTextureView;
    fallbackInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    textureInfos.fill(fallbackInfo);
    for (u32 i = 0; i < ActiveTextureDescriptorCount && i < MaxActiveTextureDescriptors; i++)
        textureInfos[i] = ActiveTextureDescriptors[i];

    VkDescriptorImageInfo attrInfo{};
    attrInfo.sampler = GraphicsAttachmentSampler;
    attrInfo.imageView = AttrImageView;
    attrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo depthInfo{};
    depthInfo.sampler = GraphicsAttachmentSampler;
    depthInfo.imageView = DepthImageView;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 6> writes{};
    u32 writeCount = 0;

    if (!descriptorCache.Ready || descriptorCache.TriangleBuffer != triangleBuffer)
        writes[writeCount++] = makeBufferDescriptorWrite(descriptorSet, 0, &triangleInfo);

    bool texturesChanged = !descriptorCache.Ready;
    if (!texturesChanged)
    {
        for (u32 i = 0; i < MaxTextureDescriptors; i++)
        {
            if (!descriptorImageInfoEquals(descriptorCache.TextureInfos[i], textureInfos[i]))
            {
                texturesChanged = true;
                break;
            }
        }
    }
    if (texturesChanged)
    {
        writes[writeCount++] = makeImageDescriptorWrite(
            descriptorSet,
            1,
            textureInfos.data(),
            getTextureBindingDescriptorCount(),
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    }

    if (!descriptorCache.Ready || descriptorCache.ToonBuffer != toonBuffer)
        writes[writeCount++] = makeBufferDescriptorWrite(descriptorSet, 2, &toonInfo);

    if (!descriptorCache.Ready || descriptorCache.AttrImageView != AttrImageView || descriptorCache.AttachmentSampler != GraphicsAttachmentSampler)
        writes[writeCount++] = makeImageDescriptorWrite(descriptorSet, 3, &attrInfo, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    if (!descriptorCache.Ready || descriptorCache.DepthImageView != DepthImageView || descriptorCache.AttachmentSampler != GraphicsAttachmentSampler)
        writes[writeCount++] = makeImageDescriptorWrite(descriptorSet, 4, &depthInfo, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    if (!descriptorCache.Ready || descriptorCache.ClearBuffer != clearBuffer)
        writes[writeCount++] = makeBufferDescriptorWrite(descriptorSet, 5, &clearInfo);

    if (writeCount > 0)
        vkUpdateDescriptorSets(Device, writeCount, writes.data(), 0, nullptr);

    descriptorCache.Ready = true;
    descriptorCache.TriangleBuffer = triangleBuffer;
    descriptorCache.ToonBuffer = toonBuffer;
    descriptorCache.ClearBuffer = clearBuffer;
    descriptorCache.AttrImageView = AttrImageView;
    descriptorCache.DepthImageView = DepthImageView;
    descriptorCache.AttachmentSampler = GraphicsAttachmentSampler;
    descriptorCache.TextureInfos = textureInfos;
}

u32 VulkanRenderer3D::findMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) const
{
    return VulkanContext::Get().FindMemoryType(typeBits, properties);
}

bool VulkanRenderer3D::dispatchRasterAndReadback(
    RenderContext* context,
    u32 rgbaColor,
    u32 clearDepth,
    u32 dispCnt,
    u32 alphaRef,
    u32 fogColor,
    u32 fogOffset,
    u32 fogShift,
    u32 clearAttr,
    const u8* fogDensityTable,
    const u16* edgeColorTable,
    const u16* toonTable,
    bool readbackToCpu,
    bool captureReadbackPath)
{
    if (ActiveBackendMode == BackendMode::GraphicsHardware)
    {
        return dispatchGraphicsRasterAndReadback(
            context,
            rgbaColor,
            clearDepth,
            dispCnt,
            alphaRef,
            fogColor,
            fogOffset,
            fogShift,
            clearAttr,
            fogDensityTable,
            edgeColorTable,
            toonTable,
            readbackToCpu,
            captureReadbackPath);
    }

    auto hasRequiredRasterPipelines = [&]() -> bool {
        constexpr u32 kFallbackShadeMode = RasterShadeModeCount - 1u;
        constexpr u32 kFallbackTextureMode = RasterTextureModeCount - 1u;
        constexpr u32 kFallbackTranslucencyMode = RasterTranslucencyModeCount - 1u;

        for (u32 sceneMode = 0; sceneMode < RasterSceneModeCount; sceneMode++)
        {
            for (u32 rasterWMode = 0; rasterWMode < RasterWModeCount; rasterWMode++)
            {
                const u32 fallbackIndex =
                    ((((sceneMode * RasterWModeCount) + rasterWMode) * RasterShadeModeCount + kFallbackShadeMode)
                        * RasterTextureModeCount + kFallbackTextureMode)
                    * RasterTranslucencyModeCount
                    + kFallbackTranslucencyMode;
                if (fallbackIndex >= RasterPipelineVariantCount || RasterPipelines[fallbackIndex] == VK_NULL_HANDLE)
                    return false;
            }
        }

        return true;
    };

    auto hasAllFinalPipelines = [&]() -> bool {
        for (const VkPipeline pipeline : FinalPipelines)
        {
            if (pipeline == VK_NULL_HANDLE)
                return false;
        }
        return true;
    };

    const bool useSynchronousContext = context == nullptr;
    const VkCommandBuffer CommandBuffer = context != nullptr ? context->CommandBuffer : this->CommandBuffer;
    const VkFence FrameFence = context != nullptr ? context->FrameFence : this->FrameFence;
    const VkBuffer TriangleBuffer = context != nullptr ? context->TriangleBuffer : this->TriangleBuffer;
    const VkDeviceMemory TriangleMemory = context != nullptr ? context->TriangleMemory : this->TriangleMemory;
    const VkDeviceSize TriangleBufferSize = context != nullptr ? context->TriangleBufferSize : this->TriangleBufferSize;
    const VkBuffer BinMaskBuffer = context != nullptr && context->BinMaskBuffer != VK_NULL_HANDLE
        ? context->BinMaskBuffer
        : this->BinMaskBuffer;
    const VkDeviceSize BinMaskBufferSize = context != nullptr && context->BinMaskBuffer != VK_NULL_HANDLE
        ? context->BinMaskBufferSize
        : this->BinMaskBufferSize;
    const VkBuffer GroupListBuffer = context != nullptr && context->GroupListBuffer != VK_NULL_HANDLE
        ? context->GroupListBuffer
        : this->GroupListBuffer;
    const VkDeviceSize GroupListBufferSize = context != nullptr && context->GroupListBuffer != VK_NULL_HANDLE
        ? context->GroupListBufferSize
        : this->GroupListBufferSize;
    const VkBuffer SpanSetupBuffer = context != nullptr && context->SpanSetupBuffer != VK_NULL_HANDLE
        ? context->SpanSetupBuffer
        : this->SpanSetupBuffer;
    const VkDeviceSize SpanSetupBufferSize = context != nullptr && context->SpanSetupBuffer != VK_NULL_HANDLE
        ? context->SpanSetupBufferSize
        : this->SpanSetupBufferSize;
    const VkBuffer WorkOffsetBuffer = context != nullptr && context->WorkOffsetBuffer != VK_NULL_HANDLE
        ? context->WorkOffsetBuffer
        : this->WorkOffsetBuffer;
    const VkDeviceSize WorkOffsetBufferSize = context != nullptr && context->WorkOffsetBuffer != VK_NULL_HANDLE
        ? context->WorkOffsetBufferSize
        : this->WorkOffsetBufferSize;
    const VkBuffer ToonBuffer = context != nullptr ? context->ToonBuffer : this->ToonBuffer;
    const VkDeviceMemory ToonMemory = context != nullptr ? context->ToonMemory : this->ToonMemory;
    const VkDeviceSize ToonBufferSize = context != nullptr ? context->ToonBufferSize : this->ToonBufferSize;
    const VkBuffer CaptureLineBuffer = context != nullptr ? context->CaptureLineBuffer : this->CaptureLineBuffer;
    void* captureLineMapped = context != nullptr ? context->CaptureLineMapped : CaptureLineMapped;
    const bool exportCaptureLine = captureReadbackPath;
    const bool useCpuDirectTiles = context != nullptr && useCpuTileBinning();
    const bool useLegacyRasterWorklist = ActiveRasterDispatchPath == RasterDispatchPath::LegacyWorklist;
    bool useCpuActiveTileDispatch = false;

    if (Device == VK_NULL_HANDLE
        || Queue == VK_NULL_HANDLE
        || InterpPipeline == VK_NULL_HANDLE
        || BinPipeline == VK_NULL_HANDLE
        || WorkOffsetsPipeline == VK_NULL_HANDLE
        || SortPipeline == VK_NULL_HANDLE
        || DepthBlendPipeline == VK_NULL_HANDLE
        || !hasRequiredRasterPipelines()
        || !hasAllFinalPipelines()
        || ColorImage == VK_NULL_HANDLE
        || ResultBuffer == VK_NULL_HANDLE
        || BinMaskBuffer == VK_NULL_HANDLE
        || GroupListBuffer == VK_NULL_HANDLE
        || SpanSetupBuffer == VK_NULL_HANDLE
        || WorkOffsetBuffer == VK_NULL_HANDLE
        || ToonBuffer == VK_NULL_HANDLE
        || TriangleBuffer == VK_NULL_HANDLE
        || TriangleMemory == VK_NULL_HANDLE)
        return false;

    if (exportCaptureLine && (CaptureLineBuffer == VK_NULL_HANDLE || captureLineMapped == nullptr))
        return false;

    constexpr u32 kCaptureReadbackWidth = 256u;
    constexpr u32 kCaptureReadbackHeight = 192u;
    const bool useCaptureDownscaleForReadback = readbackToCpu
        && captureReadbackPath
        && (ColorImageWidth != kCaptureReadbackWidth || ColorImageHeight != kCaptureReadbackHeight);
    const u32 readbackWidth = useCaptureDownscaleForReadback ? kCaptureReadbackWidth : ColorImageWidth;
    const u32 readbackHeight = useCaptureDownscaleForReadback ? kCaptureReadbackHeight : ColorImageHeight;

    if (readbackToCpu)
    {
        if (useCaptureDownscaleForReadback && !ensureCaptureReadbackImage())
            return false;

        const VkDeviceSize requiredReadbackSize = static_cast<VkDeviceSize>(readbackWidth) * static_cast<VkDeviceSize>(readbackHeight) * sizeof(u32);
        if (ReadbackBuffer == VK_NULL_HANDLE || ReadbackMemory == VK_NULL_HANDLE || ReadbackSize != requiredReadbackSize)
        {
            destroyReadbackBuffer();
            if (!createReadbackBuffer(readbackWidth, readbackHeight))
                return false;
        }
    }

    if (useSynchronousContext)
    {
        const u64 waitStartNs = PerfNowNs();
        const VkResult waitResult = vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX);
        if (waitResult != VK_SUCCESS)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: vkWaitForFences failed (%d)", static_cast<int>(waitResult));
            return false;
        }

        FenceWaitCpuWindow.Add(PerfNowNs() - waitStartNs);
        consumeGpuTiming(nullptr);
    }

    if (vkResetFences(Device, 1, &FrameFence) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: vkResetFences failed");
        return false;
    }

    if (vkResetCommandBuffer(CommandBuffer, 0) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: vkResetCommandBuffer failed");
        return false;
    }

    RasterPushConstants pushConstants{};
    pushConstants.width = ColorImageWidth;
    pushConstants.height = ColorImageHeight;
    pushConstants.clearColor = rgbaColor;
    pushConstants.clearDepth = clearDepth;
    pushConstants.triangleCount = static_cast<u32>(Triangles.size());
    pushConstants.dispCnt = dispCnt;
    pushConstants.alphaRef = alphaRef;
    pushConstants.fogColor = fogColor;
    pushConstants.fogOffset = fogOffset;
    pushConstants.fogShift = fogShift;
    pushConstants.clearAttr = clearAttr;

    for (u32 i = 0; i < 34; i++)
    {
        const u32 density = fogDensityTable != nullptr ? static_cast<u32>(fogDensityTable[i]) : 0u;
        const u32 packedWord = i / 4;
        const u32 packedShift = (i % 4) * 8;
        pushConstants.fogDensityPacked[packedWord] |= (density & 0xFFu) << packedShift;
    }

    for (u32 i = 0; i < 8; i++)
    {
        const u32 edgeColor = edgeColorTable != nullptr ? static_cast<u32>(edgeColorTable[i]) : 0u;
        u32 r = (edgeColor << 1u) & 0x3Eu;
        u32 g = (edgeColor >> 4u) & 0x3Eu;
        u32 b = (edgeColor >> 9u) & 0x3Eu;
        if (r) r++;
        if (g) g++;
        if (b) b++;
        pushConstants.edgeColorPacked[i] = (r & 0x3Fu) | ((g & 0x3Fu) << 8u) | ((b & 0x3Fu) << 16u);
    }
    constexpr u32 kVariantWildcard = 0xFFFFFFFFu;
    constexpr u32 kVariantFlagTextured = 1u << 0u;
    constexpr u32 kVariantFlagDecal = 1u << 1u;
    constexpr u32 kVariantFlagModulate = 1u << 2u;
    constexpr u32 kVariantFlagToon = 1u << 3u;
    constexpr u32 kVariantFlagHighlight = 1u << 4u;
    constexpr u32 kVariantFlagShadow = 1u << 5u;
    constexpr u32 kVariantFlagWBuffer = 1u << 6u;
    constexpr u32 kVariantFlagTranslucent = 1u << 7u;
    constexpr u32 kDebugFlagFinalActiveTileMask = 1u << 31u;
    constexpr u32 kVariantPipelineMask = (1u << 8u) - 1u;
    constexpr u32 kTriangleFlagWBuffer = 1u << 4u;
    constexpr u32 kTriangleFlagBoundaryEdge0 = 1u << 7u;
    constexpr u32 kTriangleFlagBoundaryEdge1 = 1u << 8u;
    constexpr u32 kTriangleFlagBoundaryEdge2 = 1u << 9u;
    constexpr u32 kTriangleBoundaryEdgeMask =
        kTriangleFlagBoundaryEdge0
        | kTriangleFlagBoundaryEdge1
        | kTriangleFlagBoundaryEdge2;
    constexpr u32 kRasterSceneModeDenseNoBoundary = 0u;
    constexpr u32 kRasterSceneModeDenseBoundary = 1u;
    constexpr u32 kRasterSceneModeSparseActive = 2u;
    constexpr u32 kRasterWModeZ = 0u;
    constexpr u32 kRasterWModeW = 1u;
    constexpr u32 kRasterWModeAny = 2u;
    constexpr u32 kRasterShadeModeModulate = 0u;
    constexpr u32 kRasterShadeModeDecal = 1u;
    constexpr u32 kRasterShadeModeToon = 2u;
    constexpr u32 kRasterShadeModeHighlight = 3u;
    constexpr u32 kRasterShadeModeShadow = 4u;
    constexpr u32 kRasterShadeModeAny = 5u;
    constexpr u32 kRasterTextureModeNoTexture = 0u;
    constexpr u32 kRasterTextureModeUseTexture = 1u;
    constexpr u32 kRasterTextureModeAny = 2u;
    constexpr u32 kRasterTranslucencyModeOpaque = 0u;
    constexpr u32 kRasterTranslucencyModeTranslucent = 1u;
    constexpr u32 kRasterTranslucencyModeAny = 2u;
    const bool frameWBufferMode = !Triangles.empty() && ((Triangles[0].flags & kTriangleFlagWBuffer) != 0u);
    for (TriangleGpu& triangle : Triangles)
    {
        if (frameWBufferMode)
        {
            triangle.flags |= kTriangleFlagWBuffer;
            triangle.variantKey |= kVariantFlagWBuffer;
        }
        else
        {
            triangle.flags &= ~kTriangleFlagWBuffer;
            triangle.variantKey &= ~kVariantFlagWBuffer;
        }
    }
    auto resolveRasterWMode = [frameWBufferMode](u32 variantKey) -> u32 {
        (void)variantKey;
        return frameWBufferMode ? kRasterWModeW : kRasterWModeZ;
    };
    auto resolveRasterShadeMode = [](u32 variantKey) -> u32 {
        if (variantKey == kVariantWildcard)
            return kRasterShadeModeAny;
        if ((variantKey & kVariantFlagShadow) != 0u)
            return kRasterShadeModeShadow;
        if ((variantKey & kVariantFlagHighlight) != 0u)
            return kRasterShadeModeHighlight;
        if ((variantKey & kVariantFlagToon) != 0u)
            return kRasterShadeModeToon;
        if ((variantKey & kVariantFlagDecal) != 0u)
            return kRasterShadeModeDecal;
        if ((variantKey & kVariantFlagModulate) != 0u)
            return kRasterShadeModeModulate;
        return kRasterShadeModeAny;
    };
    auto makeRasterPipelineIndex = [&](u32 rasterSceneMode,
                                       u32 rasterWMode,
                                       u32 rasterShadeMode,
                                       u32 rasterTextureMode,
                                       u32 rasterTranslucencyMode) -> u32 {
        return ((((rasterSceneMode * RasterWModeCount) + rasterWMode) * RasterShadeModeCount + rasterShadeMode) * RasterTextureModeCount + rasterTextureMode)
            * RasterTranslucencyModeCount
            + rasterTranslucencyMode;
    };
    pushConstants.variantKey = kVariantWildcard;
    pushConstants.passIndex = MelonDSAndroid::getVulkanDiagnosticFlags();
    pushConstants.passIndex &= ~kDebugFlagFinalActiveTileMask;
    pushConstants.triangleBase = 0;
    pushConstants.depthBlendMode = frameWBufferMode ? 1u : 0u;

    TriangleCountWindow.Add(static_cast<u64>(Triangles.size()));

    void* mappedTriangles = context != nullptr ? context->TriangleMapped : TriangleMapped;
    if (mappedTriangles == nullptr)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: triangle buffer is not mapped");
        return false;
    }

    if (!Triangles.empty())
        std::memcpy(mappedTriangles, Triangles.data(), Triangles.size() * sizeof(TriangleGpu));
    else
        std::memset(mappedTriangles, 0, sizeof(TriangleGpu));

    if (!updateToonBuffer(context, toonTable))
    {
        Log(LogLevel::Error, "VulkanRenderer3D: failed to update toon buffer");
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(CommandBuffer, &beginInfo) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: vkBeginCommandBuffer failed");
        return false;
    }

    VkQueryPool timestampQueryPool = context != nullptr ? context->TimestampQueryPool : TimestampQueryPool;
    if (timestampQueryPool != VK_NULL_HANDLE && ResetQueryPool != nullptr)
        ResetQueryPool(Device, timestampQueryPool, 0, TimestampQueryCount);
    if (timestampQueryPool != VK_NULL_HANDLE)
        vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampQueryPool, 0);

    VkImageMemoryBarrier toGeneralBarrier{};
    toGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneralBarrier.srcAccessMask = ColorImageLayout != VK_IMAGE_LAYOUT_UNDEFINED
        ? (VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT)
        : 0;
    toGeneralBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toGeneralBarrier.oldLayout = ColorImageLayout;
    toGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneralBarrier.image = ColorImage;
    toGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toGeneralBarrier.subresourceRange.baseMipLevel = 0;
    toGeneralBarrier.subresourceRange.levelCount = 1;
    toGeneralBarrier.subresourceRange.baseArrayLayer = 0;
    toGeneralBarrier.subresourceRange.layerCount = 1;

    const VkPipelineStageFlags toGeneralSrcStage = ColorImageLayout != VK_IMAGE_LAYOUT_UNDEFINED
        ? (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT)
        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    vkCmdPipelineBarrier(
        CommandBuffer,
        toGeneralSrcStage,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toGeneralBarrier
    );

    VkBufferMemoryBarrier triangleBufferBarrier{};
    triangleBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    triangleBufferBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    triangleBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    triangleBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    triangleBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    triangleBufferBarrier.buffer = TriangleBuffer;
    triangleBufferBarrier.offset = 0;
    triangleBufferBarrier.size = TriangleBufferSize;

    VkBufferMemoryBarrier toonBufferBarrier{};
    toonBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    toonBufferBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    toonBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toonBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toonBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toonBufferBarrier.buffer = ToonBuffer;
    toonBufferBarrier.offset = 0;
    toonBufferBarrier.size = ToonBufferSize;

    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        1,
        &triangleBufferBarrier,
        0,
        nullptr
    );

    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        1,
        &toonBufferBarrier,
        0,
        nullptr
    );

    VkBufferMemoryBarrier binMaskToWriteBarrier{};
    binMaskToWriteBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    binMaskToWriteBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    binMaskToWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    binMaskToWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    binMaskToWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    binMaskToWriteBarrier.buffer = BinMaskBuffer;
    binMaskToWriteBarrier.offset = 0;
    binMaskToWriteBarrier.size = BinMaskBufferSize;

    VkBufferMemoryBarrier groupListToWriteBarrier{};
    groupListToWriteBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    groupListToWriteBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    groupListToWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    groupListToWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    groupListToWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    groupListToWriteBarrier.buffer = GroupListBuffer;
    groupListToWriteBarrier.offset = 0;
    groupListToWriteBarrier.size = GroupListBufferSize;

    VkBufferMemoryBarrier spanSetupToWriteBarrier{};
    spanSetupToWriteBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    spanSetupToWriteBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    spanSetupToWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    spanSetupToWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    spanSetupToWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    spanSetupToWriteBarrier.buffer = SpanSetupBuffer;
    spanSetupToWriteBarrier.offset = 0;
    spanSetupToWriteBarrier.size = SpanSetupBufferSize;

    VkBufferMemoryBarrier spanSetupToReadBarrier{};
    spanSetupToReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    spanSetupToReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    spanSetupToReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    spanSetupToReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    spanSetupToReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    spanSetupToReadBarrier.buffer = SpanSetupBuffer;
    spanSetupToReadBarrier.offset = 0;
    spanSetupToReadBarrier.size = SpanSetupBufferSize;

    VkBufferMemoryBarrier spanSetupHostToReadBarrier{};
    spanSetupHostToReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    spanSetupHostToReadBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    spanSetupHostToReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    spanSetupHostToReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    spanSetupHostToReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    spanSetupHostToReadBarrier.buffer = SpanSetupBuffer;
    spanSetupHostToReadBarrier.offset = 0;
    spanSetupHostToReadBarrier.size = SpanSetupBufferSize;

    VkBufferMemoryBarrier resultToReadWriteBarrier{};
    resultToReadWriteBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    resultToReadWriteBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    resultToReadWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    resultToReadWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resultToReadWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resultToReadWriteBarrier.buffer = ResultBuffer;
    resultToReadWriteBarrier.offset = 0;
    resultToReadWriteBarrier.size = ResultBufferSize;

    VkBufferMemoryBarrier resultToWriteBarrier{};
    resultToWriteBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    resultToWriteBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    resultToWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    resultToWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resultToWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resultToWriteBarrier.buffer = ResultBuffer;
    resultToWriteBarrier.offset = 0;
    resultToWriteBarrier.size = ResultBufferSize;

    VkBufferMemoryBarrier resultToTransferWriteBarrier{};
    resultToTransferWriteBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    resultToTransferWriteBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    resultToTransferWriteBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    resultToTransferWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resultToTransferWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resultToTransferWriteBarrier.buffer = ResultBuffer;
    resultToTransferWriteBarrier.offset = 0;
    resultToTransferWriteBarrier.size = ResultBufferSize;

    if (useLegacyRasterWorklist)
    {
        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            1,
            &resultToTransferWriteBarrier,
            0,
            nullptr
        );

        const VkDeviceSize framebufferStrideBytes = static_cast<VkDeviceSize>(ColorImageWidth)
            * static_cast<VkDeviceSize>(ColorImageHeight)
            * sizeof(u32);
        const u32 clearColor6A5 = ((rgbaColor & 0xFFu) >> 2)
            | ((((rgbaColor >> 8u) & 0xFFu) >> 2) << 8u)
            | ((((rgbaColor >> 16u) & 0xFFu) >> 2) << 16u)
            | ((((rgbaColor >> 24u) & 0xFFu) >> 3) << 24u);
        vkCmdFillBuffer(CommandBuffer, ResultBuffer, 0u * framebufferStrideBytes, framebufferStrideBytes, clearColor6A5);
        vkCmdFillBuffer(CommandBuffer, ResultBuffer, 1u * framebufferStrideBytes, framebufferStrideBytes, clearColor6A5);
        vkCmdFillBuffer(CommandBuffer, ResultBuffer, 2u * framebufferStrideBytes, framebufferStrideBytes, clearDepth);
        vkCmdFillBuffer(CommandBuffer, ResultBuffer, 3u * framebufferStrideBytes, framebufferStrideBytes, clearDepth);
        vkCmdFillBuffer(CommandBuffer, ResultBuffer, 4u * framebufferStrideBytes, framebufferStrideBytes, clearAttr);
        vkCmdFillBuffer(CommandBuffer, ResultBuffer, 5u * framebufferStrideBytes, framebufferStrideBytes, clearAttr);
        vkCmdFillBuffer(CommandBuffer, ResultBuffer, 6u * framebufferStrideBytes, framebufferStrideBytes, 0u);
        vkCmdFillBuffer(CommandBuffer, ResultBuffer, 7u * framebufferStrideBytes, framebufferStrideBytes, 0u);

        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            nullptr,
            1,
            &resultToReadWriteBarrier,
            0,
            nullptr
        );
    }
    else
    {
        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            nullptr,
            1,
            &resultToWriteBarrier,
            0,
            nullptr
        );
    }

    constexpr u32 binTileSize = 8;
    const u32 binTilesX = (ColorImageWidth + (binTileSize - 1u)) / binTileSize;
    const u32 binTilesY = (ColorImageHeight + (binTileSize - 1u)) / binTileSize;
    const u32 tileCount = std::max<u32>(1u, binTilesX * binTilesY);

    VkBufferMemoryBarrier binMaskToReadBarrier{};
    binMaskToReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    binMaskToReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    binMaskToReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    binMaskToReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    binMaskToReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    binMaskToReadBarrier.buffer = BinMaskBuffer;
    binMaskToReadBarrier.offset = 0;
    binMaskToReadBarrier.size = BinMaskBufferSize;

    VkBufferMemoryBarrier binMaskHostToReadBarrier{};
    binMaskHostToReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    binMaskHostToReadBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    binMaskHostToReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    binMaskHostToReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    binMaskHostToReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    binMaskHostToReadBarrier.buffer = BinMaskBuffer;
    binMaskHostToReadBarrier.offset = 0;
    binMaskHostToReadBarrier.size = BinMaskBufferSize;

    VkBufferMemoryBarrier groupListToReadBarrier{};
    groupListToReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    groupListToReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    groupListToReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    groupListToReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    groupListToReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    groupListToReadBarrier.buffer = GroupListBuffer;
    groupListToReadBarrier.offset = 0;
    groupListToReadBarrier.size = GroupListBufferSize;

    VkBufferMemoryBarrier groupListHostToReadBarrier{};
    groupListHostToReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    groupListHostToReadBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    groupListHostToReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    groupListHostToReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    groupListHostToReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    groupListHostToReadBarrier.buffer = GroupListBuffer;
    groupListHostToReadBarrier.offset = 0;
    groupListHostToReadBarrier.size = GroupListBufferSize;

    VkBufferMemoryBarrier workOffsetToWriteBarrier{};
    workOffsetToWriteBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    workOffsetToWriteBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    workOffsetToWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    workOffsetToWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    workOffsetToWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    workOffsetToWriteBarrier.buffer = WorkOffsetBuffer;
    workOffsetToWriteBarrier.offset = 0;
    workOffsetToWriteBarrier.size = WorkOffsetBufferSize;

    VkBufferMemoryBarrier workOffsetToReadBarrier{};
    workOffsetToReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    workOffsetToReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    workOffsetToReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    workOffsetToReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    workOffsetToReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    workOffsetToReadBarrier.buffer = WorkOffsetBuffer;
    workOffsetToReadBarrier.offset = 0;
    workOffsetToReadBarrier.size = WorkOffsetBufferSize;

    VkBufferMemoryBarrier workOffsetHostToReadBarrier{};
    workOffsetHostToReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    workOffsetHostToReadBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    workOffsetHostToReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    workOffsetHostToReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    workOffsetHostToReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    workOffsetHostToReadBarrier.buffer = WorkOffsetBuffer;
    workOffsetHostToReadBarrier.offset = 0;
    workOffsetHostToReadBarrier.size = WorkOffsetBufferSize;

    constexpr VkDeviceSize kSortDispatchIndirectOffset = 0;
    constexpr VkDeviceSize kRasterDispatchIndirectOffset = sizeof(u32) * 3u;

    struct RasterPass
    {
        u32 triangleBase;
        u32 triangleCount;
        u32 textureDescriptorIndex;
    };

    std::vector<RasterPass> rasterPasses;
    const bool singleDescriptorTexturePath = usesSingleDescriptorTexturePath();
    if (singleDescriptorTexturePath)
    {
        auto resolveTriangleDescriptorIndex = [&](const TriangleGpu& triangle) -> u32 {
            if ((triangle.variantKey & kVariantFlagTextured) == 0u)
                return FallbackTextureDescriptorIndex;
            return std::min<u32>(triangle.texArrayIndex, MaxTextureDescriptors - 1u);
        };

        const u32 triangleCount = static_cast<u32>(Triangles.size());
        if (triangleCount == 0u)
        {
            rasterPasses.push_back({0u, 0u, FallbackTextureDescriptorIndex});
        }
        else
        {
            u32 runBase = 0u;
            u32 runDescriptorIndex = resolveTriangleDescriptorIndex(Triangles[0]);
            for (u32 triangleIndex = 1u; triangleIndex < triangleCount; triangleIndex++)
            {
                const u32 descriptorIndex = resolveTriangleDescriptorIndex(Triangles[triangleIndex]);
                if (descriptorIndex != runDescriptorIndex)
                {
                    rasterPasses.push_back({runBase, triangleIndex - runBase, runDescriptorIndex});
                    runBase = triangleIndex;
                    runDescriptorIndex = descriptorIndex;
                }
            }
            rasterPasses.push_back({runBase, triangleCount - runBase, runDescriptorIndex});
        }
    }
    else
    {
        rasterPasses.push_back({0u, static_cast<u32>(Triangles.size()), FallbackTextureDescriptorIndex});
    }

    if (rasterPasses.empty())
        rasterPasses.push_back({0u, 0u, FallbackTextureDescriptorIndex});

    PassCountWindow.Add(static_cast<u64>(rasterPasses.size()));

    const bool frameNeedsBoundaryAttrs = ((dispCnt & (1u << 4u)) != 0u) || ((dispCnt & (1u << 5u)) != 0u);
    auto resolveRasterModesForRange = [&](u32 triangleBase,
                                          u32 triangleCount,
                                          u32& outShadeMode,
                                          u32& outTextureMode,
                                          u32& outTranslucencyMode,
                                          bool& outNeedsDenseBoundaryMode) {
        outShadeMode = kRasterShadeModeAny;
        outTextureMode = kRasterTextureModeAny;
        outTranslucencyMode = kRasterTranslucencyModeAny;
        outNeedsDenseBoundaryMode = false;

        if (triangleCount == 0u)
            return;

        bool allTextured = true;
        bool allUntextured = true;
        bool allTranslucent = true;
        bool allOpaque = true;
        const u32 firstShadeMode = resolveRasterShadeMode(Triangles[triangleBase].variantKey);
        bool uniformShadeMode = firstShadeMode != kRasterShadeModeAny;

        for (u32 triangleIndex = triangleBase; triangleIndex < triangleBase + triangleCount; triangleIndex++)
        {
            const TriangleGpu& triangle = Triangles[triangleIndex];
            const bool isTextured = (triangle.variantKey & kVariantFlagTextured) != 0u;
            const bool isTranslucent = (triangle.variantKey & kVariantFlagTranslucent) != 0u;
            const bool hasBoundaryEdges = (triangle.flags & kTriangleBoundaryEdgeMask) != 0u;
            const bool isWireframe = ((triangle.polyAttr >> 16u) & 0x1Fu) == 0u;
            allTextured &= isTextured;
            allUntextured &= !isTextured;
            allTranslucent &= isTranslucent;
            allOpaque &= !isTranslucent;
            outNeedsDenseBoundaryMode |= hasBoundaryEdges && (frameNeedsBoundaryAttrs || isWireframe);

            if (uniformShadeMode && resolveRasterShadeMode(triangle.variantKey) != firstShadeMode)
                uniformShadeMode = false;
        }

        if (allTextured)
            outTextureMode = kRasterTextureModeUseTexture;
        else if (allUntextured)
            outTextureMode = kRasterTextureModeNoTexture;

        if (allTranslucent)
            outTranslucencyMode = kRasterTranslucencyModeTranslucent;
        else if (allOpaque)
            outTranslucencyMode = kRasterTranslucencyModeOpaque;

        if (uniformShadeMode)
            outShadeMode = firstShadeMode;
    };

    u64 cpuActiveTileCountAccum = 0;
    u64 cpuTileCountAccum = 0;
    u64 cpuActiveGroupCountAccum = 0;
    u64 cpuActiveDispatchAccum = 0;
    bool canUseFinalActiveTileDispatch = rasterPasses.size() == 1u;
    bool finalUsesCpuActiveTileDispatch = false;

    for (const RasterPass& rasterPass : rasterPasses)
    {
        const VkDescriptorSet passDescriptorSet = getDescriptorSet(context, rasterPass.textureDescriptorIndex);
        if (passDescriptorSet == VK_NULL_HANDLE)
            return false;

        updateDescriptorSet(context, rasterPass.textureDescriptorIndex);
        vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1, &passDescriptorSet, 0, nullptr);

        pushConstants.triangleBase = rasterPass.triangleBase;
        pushConstants.triangleCount = rasterPass.triangleCount;
        pushConstants.variantKey = kVariantWildcard;
        pushConstants.passIndex = MelonDSAndroid::getVulkanDiagnosticFlags() & ~kDebugFlagFinalActiveTileMask;

        const u32 binGroupCount = std::max<u32>(1u, (pushConstants.triangleCount + 31u) / 32u);
        const u32 interpGroupCount = std::max<u32>(1u, (pushConstants.triangleCount + 63u) / 64u);
        const u32 rasterWMode = resolveRasterWMode(kVariantWildcard);
        u32 rasterShadeMode = kRasterShadeModeAny;
        u32 rasterTextureMode = kRasterTextureModeAny;
        u32 rasterTranslucencyMode = kRasterTranslucencyModeAny;
        bool denseBoundaryMode = false;
        resolveRasterModesForRange(
            rasterPass.triangleBase,
            rasterPass.triangleCount,
            rasterShadeMode,
            rasterTextureMode,
            rasterTranslucencyMode,
            denseBoundaryMode
        );

        if (rasterTextureMode != kRasterTextureModeAny)
            RasterSpecializedTextureModeCount++;
        if (rasterTranslucencyMode != kRasterTranslucencyModeAny)
            RasterSpecializedTranslucencyModeCount++;
        if (rasterShadeMode != kRasterShadeModeAny)
            RasterSpecializedShadeModeCount++;
        if (rasterTextureMode != kRasterTextureModeAny
            && rasterTranslucencyMode != kRasterTranslucencyModeAny
            && rasterShadeMode != kRasterShadeModeAny)
        {
            RasterSpecializedAllModesCount++;
        }

        if (!useCpuDirectTiles)
        {
            const u64 interpCpuStartNs = PerfNowNs();
            vkCmdPipelineBarrier(
                CommandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0,
                nullptr,
                1,
                &spanSetupToWriteBarrier,
                0,
                nullptr
            );
            vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, InterpPipeline);
            vkCmdPushConstants(CommandBuffer, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
            vkCmdDispatch(CommandBuffer, interpGroupCount, 1, 1);
            if (timestampQueryPool != VK_NULL_HANDLE)
                vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 1);
            InterpCpuWindow.Add(PerfNowNs() - interpCpuStartNs);
        }
        else
        {
            InterpCpuWindow.Add(0);
            if (timestampQueryPool != VK_NULL_HANDLE)
                vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 1);
        }

        u32 cpuActiveTileCountSample = 0;
        u32 cpuTileCountSample = 0;
        u32 cpuActiveGroupCountSample = 0;
        u32 cpuActiveDispatchSample = 0;
        bool passUsesCpuActiveTileDispatch = false;

        const u64 binCpuStartNs = PerfNowNs();
        if (useCpuDirectTiles)
        {
            if (!prepareCpuTileBins(*context, pushConstants))
            {
                Log(LogLevel::Error, "VulkanRenderer3D: failed to prepare CPU tile bins");
                return false;
            }

            const u32* workOffsetValues = reinterpret_cast<const u32*>(context->WorkOffsetMapped);
            if (workOffsetValues != nullptr)
            {
                cpuActiveTileCountSample = workOffsetValues[kWorkActiveTileCount];
                cpuActiveGroupCountSample = workOffsetValues[kWorkActiveGroupCount];
            }
            cpuTileCountSample = tileCount;
            const bool hasSparseCoverage = static_cast<u64>(cpuActiveTileCountSample) * 100ull
                < static_cast<u64>(tileCount) * static_cast<u64>(kCpuActiveTileDispatchMaxCoveragePercent);
            const bool firstPassOfMultiDescriptorSequence =
                singleDescriptorTexturePath && rasterPasses.size() > 1u && rasterPass.triangleBase == 0u;
            passUsesCpuActiveTileDispatch =
                !firstPassOfMultiDescriptorSequence && (cpuActiveTileCountSample == 0u || hasSparseCoverage);
            cpuActiveDispatchSample = passUsesCpuActiveTileDispatch ? 100u : 0u;
            if (passUsesCpuActiveTileDispatch)
                pushConstants.passIndex |= kDebugFlagFinalActiveTileMask;

            BinCpuWindow.Add(PerfNowNs() - binCpuStartNs);
            WorkOffsetsCpuWindow.Add(0);
            SortCpuWindow.Add(0);
            CpuDirectTilesPathCount++;

            std::array<VkBufferMemoryBarrier, 3> rasterReadBarriers{};
            rasterReadBarriers[0] = spanSetupHostToReadBarrier;
            rasterReadBarriers[1] = binMaskHostToReadBarrier;
            rasterReadBarriers[2] = passUsesCpuActiveTileDispatch
                ? workOffsetHostToReadBarrier
                : groupListHostToReadBarrier;
            vkCmdPipelineBarrier(
                CommandBuffer,
                VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                0,
                0,
                nullptr,
                static_cast<u32>(rasterReadBarriers.size()),
                rasterReadBarriers.data(),
                0,
                nullptr
            );
            if (timestampQueryPool != VK_NULL_HANDLE)
            {
                vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 2);
                vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 3);
                vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 4);
            }
        }
        else
        {
            if (!useLegacyRasterWorklist)
            {
                vkCmdFillBuffer(
                    CommandBuffer,
                    GroupListBuffer,
                    0,
                    static_cast<VkDeviceSize>(tileCount) * sizeof(u32),
                    0
                );
            }
            std::array<VkBufferMemoryBarrier, 3> binWriteBarriers = {
                spanSetupToReadBarrier,
                binMaskToWriteBarrier,
                groupListToWriteBarrier,
            };
            const u32 binWriteBarrierCount = useLegacyRasterWorklist ? 2u : static_cast<u32>(binWriteBarriers.size());
            const VkPipelineStageFlags binWriteSrcStages = useLegacyRasterWorklist
                ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                : (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
            vkCmdPipelineBarrier(
                CommandBuffer,
                binWriteSrcStages,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0,
                nullptr,
                binWriteBarrierCount,
                binWriteBarriers.data(),
                0,
                nullptr
            );
            vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, BinPipeline);
            vkCmdPushConstants(CommandBuffer, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
            vkCmdDispatch(CommandBuffer, binGroupCount, binTilesX, binTilesY);
            if (timestampQueryPool != VK_NULL_HANDLE)
                vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 2);
            BinCpuWindow.Add(PerfNowNs() - binCpuStartNs);

            if (useLegacyRasterWorklist)
            {
                LegacyWorklistPathCount++;

                const u64 workOffsetsCpuStartNs = PerfNowNs();
                std::array<VkBufferMemoryBarrier, 2> workOffsetsBarriers = {
                    binMaskToReadBarrier,
                    workOffsetToWriteBarrier,
                };
                vkCmdPipelineBarrier(
                    CommandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0,
                    0,
                    nullptr,
                    static_cast<u32>(workOffsetsBarriers.size()),
                    workOffsetsBarriers.data(),
                    0,
                    nullptr
                );
                vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, WorkOffsetsPipeline);
                vkCmdPushConstants(CommandBuffer, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
                vkCmdDispatch(CommandBuffer, 1, 1, 1);
                if (timestampQueryPool != VK_NULL_HANDLE)
                    vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 3);
                WorkOffsetsCpuWindow.Add(PerfNowNs() - workOffsetsCpuStartNs);

                const u64 sortCpuStartNs = PerfNowNs();
                vkCmdPipelineBarrier(
                    CommandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                    0,
                    0,
                    nullptr,
                    1,
                    &workOffsetToReadBarrier,
                    0,
                    nullptr
                );
                vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, SortPipeline);
                vkCmdPushConstants(CommandBuffer, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
                vkCmdDispatchIndirect(CommandBuffer, WorkOffsetBuffer, kSortDispatchIndirectOffset);
                if (timestampQueryPool != VK_NULL_HANDLE)
                    vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 4);
                SortCpuWindow.Add(PerfNowNs() - sortCpuStartNs);

                std::array<VkBufferMemoryBarrier, 2> rasterReadBarriers = {
                    binMaskToReadBarrier,
                    workOffsetToReadBarrier,
                };
                vkCmdPipelineBarrier(
                    CommandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                    0,
                    0,
                    nullptr,
                    static_cast<u32>(rasterReadBarriers.size()),
                    rasterReadBarriers.data(),
                    0,
                    nullptr
                );
            }
            else
            {
                DirectTilesPathCount++;
                WorkOffsetsCpuWindow.Add(0);
                SortCpuWindow.Add(0);
                std::array<VkBufferMemoryBarrier, 2> rasterReadBarriers = {
                    binMaskToReadBarrier,
                    groupListToReadBarrier,
                };
                vkCmdPipelineBarrier(
                    CommandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0,
                    0,
                    nullptr,
                    static_cast<u32>(rasterReadBarriers.size()),
                    rasterReadBarriers.data(),
                    0,
                    nullptr
                );
                if (timestampQueryPool != VK_NULL_HANDLE)
                {
                    vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 3);
                    vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 4);
                }
            }
        }

        cpuActiveTileCountAccum += cpuActiveTileCountSample;
        cpuTileCountAccum += cpuTileCountSample;
        cpuActiveGroupCountAccum += cpuActiveGroupCountSample;
        cpuActiveDispatchAccum += cpuActiveDispatchSample;
        if (canUseFinalActiveTileDispatch)
            finalUsesCpuActiveTileDispatch = passUsesCpuActiveTileDispatch;

        const u32 rasterSceneMode =
            (useLegacyRasterWorklist || passUsesCpuActiveTileDispatch)
                ? kRasterSceneModeSparseActive
                : (denseBoundaryMode ? kRasterSceneModeDenseBoundary : kRasterSceneModeDenseNoBoundary);
        const u32 rasterPipelineIndex = makeRasterPipelineIndex(
            rasterSceneMode,
            rasterWMode,
            rasterShadeMode,
            rasterTextureMode,
            rasterTranslucencyMode
        );
        const u32 rasterFallbackPipelineIndex = makeRasterPipelineIndex(
            rasterSceneMode,
            rasterWMode,
            kRasterShadeModeAny,
            kRasterTextureModeAny,
            kRasterTranslucencyModeAny
        );
        if (rasterPipelineIndex >= RasterPipelineVariantCount || rasterFallbackPipelineIndex >= RasterPipelineVariantCount)
            return false;
        VkPipeline rasterPipeline = RasterPipelines[rasterPipelineIndex];
        if (rasterPipeline == VK_NULL_HANDLE)
            rasterPipeline = RasterPipelines[rasterFallbackPipelineIndex];
        if (rasterPipeline == VK_NULL_HANDLE)
            return false;

        if (useCpuDirectTiles)
        {
            ActiveRasterExecutionProfile = passUsesCpuActiveTileDispatch
                ? (VulkanContext::Get().GetDeviceProfile().IsArmMali
                    ? RasterExecutionProfile::MaliCpuDense
                    : RasterExecutionProfile::AdrenoCpuSparse)
                : (VulkanContext::Get().GetDeviceProfile().IsArmMali
                    ? RasterExecutionProfile::MaliCpuDense
                    : RasterExecutionProfile::AdrenoCpuDense);
        }
        ActiveRasterTileLoopMode = useLegacyRasterWorklist
            ? RasterTileLoopMode::LegacyWorklist
            : (passUsesCpuActiveTileDispatch ? RasterTileLoopMode::SparseActive : RasterTileLoopMode::DenseGroupList);
        RasterExecutionProfileCounts[static_cast<size_t>(ActiveRasterExecutionProfile)]++;
        RasterTileLoopModeCounts[static_cast<size_t>(ActiveRasterTileLoopMode)]++;

        const u64 rasterCpuStartNs = PerfNowNs();
        vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rasterPipeline);
        vkCmdPushConstants(CommandBuffer, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
        if (useLegacyRasterWorklist || passUsesCpuActiveTileDispatch)
            vkCmdDispatchIndirect(CommandBuffer, WorkOffsetBuffer, kRasterDispatchIndirectOffset);
        else
            vkCmdDispatch(CommandBuffer, binTilesX, binTilesY, 1);
        if (timestampQueryPool != VK_NULL_HANDLE)
            vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 5);
        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            nullptr,
            1,
            &resultToReadWriteBarrier,
            0,
            nullptr
        );
        RasterCpuWindow.Add(PerfNowNs() - rasterCpuStartNs);
    }

    const u64 rasterPassCount = std::max<u64>(1u, static_cast<u64>(rasterPasses.size()));
    CpuActiveTileCountWindow.Add(cpuActiveTileCountAccum / rasterPassCount);
    CpuTileCountWindow.Add(cpuTileCountAccum / rasterPassCount);
    CpuActiveGroupCountWindow.Add(cpuActiveGroupCountAccum / rasterPassCount);
    CpuActiveDispatchWindow.Add(cpuActiveDispatchAccum / rasterPassCount);

    useCpuActiveTileDispatch = canUseFinalActiveTileDispatch && finalUsesCpuActiveTileDispatch;
    pushConstants.variantKey = kVariantWildcard;
    pushConstants.triangleBase = 0;
    pushConstants.triangleCount = static_cast<u32>(Triangles.size());
    pushConstants.passIndex = MelonDSAndroid::getVulkanDiagnosticFlags() & ~kDebugFlagFinalActiveTileMask;
    pushConstants.depthBlendMode = frameWBufferMode ? 1u : 0u;

    DepthBlendCpuWindow.Add(0);

    VkBufferMemoryBarrier resultToReadBarrier{};
    resultToReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    resultToReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    resultToReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    resultToReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resultToReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resultToReadBarrier.buffer = ResultBuffer;
    resultToReadBarrier.offset = 0;
    resultToReadBarrier.size = ResultBufferSize;

    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        1,
        &resultToReadBarrier,
        0,
        nullptr
    );
    if (timestampQueryPool != VK_NULL_HANDLE)
        vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 6);

    u32 finalPipelineIndex = 0;
    if ((dispCnt & (1u << 5u)) != 0u) // edge marking
        finalPipelineIndex |= 0x1u;
    if ((dispCnt & (1u << 7u)) != 0u) // fog
        finalPipelineIndex |= 0x2u;
    if ((dispCnt & (1u << 4u)) != 0u) // anti-aliasing
        finalPipelineIndex |= 0x4u;

    VkPipeline finalPipeline = FinalPipelines[finalPipelineIndex];
    if (finalPipeline == VK_NULL_HANDLE)
        return false;
    if (exportCaptureLine && CaptureLineExportPipeline == VK_NULL_HANDLE)
        return false;

    if (useCpuActiveTileDispatch)
    {
        VkImageMemoryBarrier imageToTransferWriteBarrier{};
        imageToTransferWriteBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageToTransferWriteBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        imageToTransferWriteBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageToTransferWriteBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageToTransferWriteBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageToTransferWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageToTransferWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageToTransferWriteBarrier.image = ColorImage;
        imageToTransferWriteBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageToTransferWriteBarrier.subresourceRange.baseMipLevel = 0;
        imageToTransferWriteBarrier.subresourceRange.levelCount = 1;
        imageToTransferWriteBarrier.subresourceRange.baseArrayLayer = 0;
        imageToTransferWriteBarrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &imageToTransferWriteBarrier
        );

        const u32 clearColor6A5 = ((rgbaColor & 0xFFu) >> 2u)
            | ((((rgbaColor >> 8u) & 0xFFu) >> 2u) << 8u)
            | ((((rgbaColor >> 16u) & 0xFFu) >> 2u) << 16u)
            | ((((rgbaColor >> 24u) & 0xFFu) >> 3u) << 24u);
        VkClearColorValue clearColorValue{};
        clearColorValue.float32[0] = static_cast<float>(clearColor6A5 & 0x3Fu) / 63.0f;
        clearColorValue.float32[1] = static_cast<float>((clearColor6A5 >> 8u) & 0x3Fu) / 63.0f;
        clearColorValue.float32[2] = static_cast<float>((clearColor6A5 >> 16u) & 0x3Fu) / 63.0f;
        clearColorValue.float32[3] = static_cast<float>((clearColor6A5 >> 24u) & 0x1Fu) / 31.0f;
        VkImageSubresourceRange clearRange{};
        clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearRange.baseMipLevel = 0;
        clearRange.levelCount = 1;
        clearRange.baseArrayLayer = 0;
        clearRange.layerCount = 1;
        vkCmdClearColorImage(CommandBuffer, ColorImage, VK_IMAGE_LAYOUT_GENERAL, &clearColorValue, 1, &clearRange);

        VkImageMemoryBarrier imageToComputeWriteBarrier{};
        imageToComputeWriteBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageToComputeWriteBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageToComputeWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        imageToComputeWriteBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageToComputeWriteBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageToComputeWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageToComputeWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageToComputeWriteBarrier.image = ColorImage;
        imageToComputeWriteBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageToComputeWriteBarrier.subresourceRange.baseMipLevel = 0;
        imageToComputeWriteBarrier.subresourceRange.levelCount = 1;
        imageToComputeWriteBarrier.subresourceRange.baseArrayLayer = 0;
        imageToComputeWriteBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &imageToComputeWriteBarrier
        );

    }

    const u64 finalCpuStartNs = PerfNowNs();
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, finalPipeline);
    vkCmdPushConstants(CommandBuffer, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
    if (useCpuActiveTileDispatch)
        vkCmdDispatchIndirect(CommandBuffer, WorkOffsetBuffer, kRasterDispatchIndirectOffset);
    else
        vkCmdDispatch(CommandBuffer, binTilesX, binTilesY, 1);
    if (timestampQueryPool != VK_NULL_HANDLE)
        vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 7);
    FinalCpuWindow.Add(PerfNowNs() - finalCpuStartNs);

    if (exportCaptureLine)
    {
        const u64 captureLineExportCpuStartNs = PerfNowNs();
        VkImageMemoryBarrier colorToCaptureReadBarrier{};
        colorToCaptureReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorToCaptureReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        colorToCaptureReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        colorToCaptureReadBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        colorToCaptureReadBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        colorToCaptureReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorToCaptureReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorToCaptureReadBarrier.image = ColorImage;
        colorToCaptureReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorToCaptureReadBarrier.subresourceRange.baseMipLevel = 0;
        colorToCaptureReadBarrier.subresourceRange.levelCount = 1;
        colorToCaptureReadBarrier.subresourceRange.baseArrayLayer = 0;
        colorToCaptureReadBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &colorToCaptureReadBarrier
        );

        vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, CaptureLineExportPipeline);
        vkCmdPushConstants(CommandBuffer, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
        vkCmdDispatch(CommandBuffer, 32u, 24u, 1u);

        VkBufferMemoryBarrier captureToHostReadBarrier{};
        captureToHostReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        captureToHostReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        captureToHostReadBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        captureToHostReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        captureToHostReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        captureToHostReadBarrier.buffer = CaptureLineBuffer;
        captureToHostReadBarrier.offset = 0;
        captureToHostReadBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0,
            0,
            nullptr,
            1,
            &captureToHostReadBarrier,
            0,
            nullptr
        );
        CaptureLineExportCpuWindow.Add(PerfNowNs() - captureLineExportCpuStartNs);
        CaptureLineExportCount++;
    }

    if (timestampQueryPool != VK_NULL_HANDLE)
        vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampQueryPool, 8);

    if (readbackToCpu)
    {
        VkImageMemoryBarrier toTransferSrcBarrier{};
        toTransferSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        // Match the prepared snapshot path: capture readback must see the
        // fully resolved ColorImage, including transfer-based clears and any
        // earlier transfer usage on the same image.
        toTransferSrcBarrier.srcAccessMask =
            VK_ACCESS_SHADER_READ_BIT |
            VK_ACCESS_SHADER_WRITE_BIT |
            VK_ACCESS_TRANSFER_WRITE_BIT |
            VK_ACCESS_TRANSFER_READ_BIT;
        toTransferSrcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransferSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        toTransferSrcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransferSrcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferSrcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferSrcBarrier.image = ColorImage;
        toTransferSrcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransferSrcBarrier.subresourceRange.baseMipLevel = 0;
        toTransferSrcBarrier.subresourceRange.levelCount = 1;
        toTransferSrcBarrier.subresourceRange.baseArrayLayer = 0;
        toTransferSrcBarrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toTransferSrcBarrier
        );

        if (useCaptureDownscaleForReadback)
        {
            VkImageMemoryBarrier captureToTransferDstBarrier{};
            captureToTransferDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            captureToTransferDstBarrier.srcAccessMask = CaptureReadbackImageInitialized ? VK_ACCESS_TRANSFER_READ_BIT : 0u;
            captureToTransferDstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            captureToTransferDstBarrier.oldLayout = CaptureReadbackImageLayout;
            captureToTransferDstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            captureToTransferDstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            captureToTransferDstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            captureToTransferDstBarrier.image = CaptureReadbackImage;
            captureToTransferDstBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            captureToTransferDstBarrier.subresourceRange.baseMipLevel = 0;
            captureToTransferDstBarrier.subresourceRange.levelCount = 1;
            captureToTransferDstBarrier.subresourceRange.baseArrayLayer = 0;
            captureToTransferDstBarrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(
                CommandBuffer,
                CaptureReadbackImageInitialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &captureToTransferDstBarrier
            );

            VkImageBlit blitRegion{};
            blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blitRegion.srcSubresource.mipLevel = 0;
            blitRegion.srcSubresource.baseArrayLayer = 0;
            blitRegion.srcSubresource.layerCount = 1;
            blitRegion.srcOffsets[0] = {0, 0, 0};
            blitRegion.srcOffsets[1] = {static_cast<int32_t>(ColorImageWidth), static_cast<int32_t>(ColorImageHeight), 1};
            blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blitRegion.dstSubresource.mipLevel = 0;
            blitRegion.dstSubresource.baseArrayLayer = 0;
            blitRegion.dstSubresource.layerCount = 1;
            blitRegion.dstOffsets[0] = {0, 0, 0};
            blitRegion.dstOffsets[1] = {static_cast<int32_t>(readbackWidth), static_cast<int32_t>(readbackHeight), 1};
            vkCmdBlitImage(
                CommandBuffer,
                ColorImage,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                CaptureReadbackImage,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &blitRegion,
                VK_FILTER_NEAREST
            );

            VkImageMemoryBarrier captureToTransferSrcBarrier{};
            captureToTransferSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            captureToTransferSrcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            captureToTransferSrcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            captureToTransferSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            captureToTransferSrcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            captureToTransferSrcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            captureToTransferSrcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            captureToTransferSrcBarrier.image = CaptureReadbackImage;
            captureToTransferSrcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            captureToTransferSrcBarrier.subresourceRange.baseMipLevel = 0;
            captureToTransferSrcBarrier.subresourceRange.levelCount = 1;
            captureToTransferSrcBarrier.subresourceRange.baseArrayLayer = 0;
            captureToTransferSrcBarrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(
                CommandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &captureToTransferSrcBarrier
            );

            VkBufferImageCopy copyRegion{};
            copyRegion.bufferOffset = 0;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.mipLevel = 0;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageOffset = {0, 0, 0};
            copyRegion.imageExtent.width = readbackWidth;
            copyRegion.imageExtent.height = readbackHeight;
            copyRegion.imageExtent.depth = 1;

            vkCmdCopyImageToBuffer(
                CommandBuffer,
                CaptureReadbackImage,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                ReadbackBuffer,
                1,
                &copyRegion
            );
        }
        else
        {
            VkBufferImageCopy copyRegion{};
            copyRegion.bufferOffset = 0;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.mipLevel = 0;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageOffset = {0, 0, 0};
            copyRegion.imageExtent.width = ColorImageWidth;
            copyRegion.imageExtent.height = ColorImageHeight;
            copyRegion.imageExtent.depth = 1;

            vkCmdCopyImageToBuffer(
                CommandBuffer,
                ColorImage,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                ReadbackBuffer,
                1,
                &copyRegion
            );
        }

        VkBufferMemoryBarrier toHostBarrier{};
        toHostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        toHostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        toHostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHostBarrier.buffer = ReadbackBuffer;
        toHostBarrier.offset = 0;
        toHostBarrier.size = ReadbackSize;

        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0,
            0,
            nullptr,
            1,
            &toHostBarrier,
            0,
            nullptr
        );

        if (useCaptureDownscaleForReadback)
        {
            VkImageMemoryBarrier captureBackToGeneralBarrier{};
            captureBackToGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            captureBackToGeneralBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            captureBackToGeneralBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            captureBackToGeneralBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            captureBackToGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            captureBackToGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            captureBackToGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            captureBackToGeneralBarrier.image = CaptureReadbackImage;
            captureBackToGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            captureBackToGeneralBarrier.subresourceRange.baseMipLevel = 0;
            captureBackToGeneralBarrier.subresourceRange.levelCount = 1;
            captureBackToGeneralBarrier.subresourceRange.baseArrayLayer = 0;
            captureBackToGeneralBarrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(
                CommandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &captureBackToGeneralBarrier
            );
        }

        VkImageMemoryBarrier backToGeneralBarrier{};
        backToGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        backToGeneralBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        backToGeneralBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        backToGeneralBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        backToGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        backToGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        backToGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        backToGeneralBarrier.image = ColorImage;
        backToGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        backToGeneralBarrier.subresourceRange.baseMipLevel = 0;
        backToGeneralBarrier.subresourceRange.levelCount = 1;
        backToGeneralBarrier.subresourceRange.baseArrayLayer = 0;
        backToGeneralBarrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &backToGeneralBarrier
        );
    }

    if (vkEndCommandBuffer(CommandBuffer) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "VulkanRenderer3D: vkEndCommandBuffer failed");
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &CommandBuffer;
    u64 completionValue = 0;
    VkTimelineSemaphoreSubmitInfo completionInfo{};
    if (CompletionTimelineSemaphore != VK_NULL_HANDLE)
    {
        completionValue = ++NextCompletionTimelineValue;
        completionInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        completionInfo.signalSemaphoreValueCount = 1;
        completionInfo.pSignalSemaphoreValues = &completionValue;
        submitInfo.pNext = &completionInfo;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &CompletionTimelineSemaphore;
    }

    {
        std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
        const VkResult submitResult = vkQueueSubmit(Queue, 1, &submitInfo, FrameFence);
        if (submitResult != VK_SUCCESS)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: vkQueueSubmit failed (%d)", static_cast<int>(submitResult));
            return false;
        }
    }

    if (context != nullptr && Threaded)
    {
        LastSubmittedRenderContext = context;
        context->SubmittedFrameSerial = Structured2DPendingSerial != 0
            ? Structured2DPendingSerial
            : Structured2DFrameSerial;
        context->SubmittedGeneration = GPU3D.GetCurrentRendererGeneration();
        context->CompletionValue = completionValue;
    }
    LastCompletionTimelineValue = completionValue;

    if (timestampQueryPool != VK_NULL_HANDLE)
    {
        if (context != nullptr)
            context->TimestampPending = true;
        else
            TimestampPending = true;
    }

    const bool deferCaptureReadbackCompletion = readbackToCpu && captureReadbackPath && context != nullptr && Threaded;

    if ((readbackToCpu && !deferCaptureReadbackCompletion) || useSynchronousContext)
    {
        const u64 waitStartNs = PerfNowNs();
        if (vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: completion fence wait failed");
            return false;
        }

        FenceWaitCpuWindow.Add(PerfNowNs() - waitStartNs);
        consumeGpuTiming(context);
    }

    if (readbackToCpu && !deferCaptureReadbackCompletion)
    {
        if (ReadbackMapped == nullptr)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: readback buffer is not mapped");
            return false;
        }

        const size_t pixelCount = static_cast<size_t>(readbackWidth) * static_cast<size_t>(readbackHeight);
        if (RawReadbackRgba.size() != pixelCount)
            RawReadbackRgba.resize(pixelCount);
        std::memcpy(RawReadbackRgba.data(), ReadbackMapped, pixelCount * sizeof(u32));

        if (useCaptureDownscaleForReadback)
            CaptureReadbackImageInitialized = true;
    }
    else if (deferCaptureReadbackCompletion)
    {
        CaptureReadbackPending = true;
        PendingCaptureReadbackContext = context;
        if (useCaptureDownscaleForReadback)
            CaptureReadbackImageInitialized = true;
    }

    if (exportCaptureLine)
    {
        ReadyCaptureLineData = reinterpret_cast<const u32*>(captureLineMapped);
        if (useSynchronousContext)
        {
            CaptureLinePending = false;
            PendingCaptureLineContext = nullptr;
            PendingCaptureLineBufferSlot = -1;
            PendingCaptureLineScreenSwap = false;
            CaptureLineReady = ReadyCaptureLineData != nullptr;
            ReadyCaptureLineBufferSlot = CaptureLineReady ? static_cast<int>(ActiveCaptureLineBufferSlot) : -1;
            ReadyCaptureLineScreenSwap = CurrentRenderScreenSwap;
        }
        else
        {
            CaptureLinePending = true;
            PendingCaptureLineContext = context;
            PendingCaptureLineBufferSlot = -1;
            PendingCaptureLineScreenSwap = CurrentRenderScreenSwap;
            CaptureLineReady = false;
            ReadyCaptureLineBufferSlot = -1;
            ReadyCaptureLineScreenSwap = false;
        }
    }

    ColorImageInitialized = true;
    ColorImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    HasCpuFrame = readbackToCpu && !deferCaptureReadbackCompletion;
    return true;
}

bool VulkanRenderer3D::dispatchGraphicsRasterAndReadback(
    RenderContext* context,
    u32 rgbaColor,
    u32 clearDepth,
    u32 dispCnt,
    u32 alphaRef,
    u32 fogColor,
    u32 fogOffset,
    u32 fogShift,
    u32 clearAttr,
    const u8* fogDensityTable,
    const u16* edgeColorTable,
    const u16* toonTable,
    bool readbackToCpu,
    bool captureReadbackPath)
{
    constexpr u32 kTriangleFlagTextured = 1u << 1u;
    constexpr u32 kTriangleFlagDecal = 1u << 2u;
    constexpr u32 kTriangleFlagWBuffer = 1u << 4u;
    constexpr u32 kTriangleFlagLinear = 1u << 6u;
    constexpr u32 kTriangleFlagTextureOpaque = 1u << 14u;
    constexpr u32 kCaptureReadbackWidth = 256u;
    constexpr u32 kCaptureReadbackHeight = 192u;

    if (Device == VK_NULL_HANDLE
        || Queue == VK_NULL_HANDLE
        || !GraphicsReady
        || ColorImage == VK_NULL_HANDLE
        || ColorImageView == VK_NULL_HANDLE
        || AttrImage == VK_NULL_HANDLE
        || AttrImageView == VK_NULL_HANDLE
        || DepthImage == VK_NULL_HANDLE
        || DepthImageView == VK_NULL_HANDLE
        || DepthStencilImage == VK_NULL_HANDLE
        || DepthStencilImageView == VK_NULL_HANDLE
        || GraphicsRasterRenderPass == VK_NULL_HANDLE
        || GraphicsFinalRenderPass == VK_NULL_HANDLE
        || GraphicsRasterFramebuffer == VK_NULL_HANDLE
        || GraphicsFinalFramebuffer == VK_NULL_HANDLE
        || GraphicsPipelineLayout == VK_NULL_HANDLE)
    {
        return false;
    }

    const bool useSynchronousContext = context == nullptr;
    const VkCommandBuffer commandBuffer = context != nullptr ? context->CommandBuffer : CommandBuffer;
    const VkFence frameFence = context != nullptr ? context->FrameFence : FrameFence;
    const VkBuffer triangleBuffer = context != nullptr ? context->TriangleBuffer : TriangleBuffer;
    const VkBuffer graphicsVertexBuffer = context != nullptr ? context->GraphicsVertexBuffer : GraphicsVertexBuffer;
    const VkBuffer graphicsSceneVertexBuffer = context != nullptr
        ? context->GraphicsSceneVertexBuffer
        : GraphicsSceneVertexBuffer;
    const VkBuffer graphicsEdgeIndexBuffer = context != nullptr
        ? context->GraphicsEdgeIndexBuffer
        : GraphicsEdgeIndexBuffer;
    const VkBuffer toonBuffer = context != nullptr ? context->ToonBuffer : ToonBuffer;
    const VkBuffer clearBuffer = context != nullptr ? context->ClearBuffer : ClearBuffer;
    void* triangleMapped = context != nullptr ? context->TriangleMapped : TriangleMapped;
    void* graphicsVertexMapped = context != nullptr ? context->GraphicsVertexMapped : GraphicsVertexMapped;
    void* graphicsSceneVertexMapped = context != nullptr
        ? context->GraphicsSceneVertexMapped
        : GraphicsSceneVertexMapped;
    void* graphicsEdgeIndexMapped = context != nullptr
        ? context->GraphicsEdgeIndexMapped
        : GraphicsEdgeIndexMapped;
    const VkBuffer captureLineBuffer = context != nullptr ? context->CaptureLineBuffer : CaptureLineBuffer;
    void* captureLineMapped = context != nullptr ? context->CaptureLineMapped : CaptureLineMapped;
    VkQueryPool timestampQueryPool = context != nullptr ? context->TimestampQueryPool : TimestampQueryPool;
    bool& timestampPending = context != nullptr ? context->TimestampPending : TimestampPending;

    if (triangleBuffer == VK_NULL_HANDLE || triangleMapped == nullptr
        || graphicsVertexBuffer == VK_NULL_HANDLE || graphicsVertexMapped == nullptr)
        return false;
    if (graphicsSceneVertexBuffer == VK_NULL_HANDLE || graphicsSceneVertexMapped == nullptr
        || graphicsEdgeIndexBuffer == VK_NULL_HANDLE || graphicsEdgeIndexMapped == nullptr)
        return false;
    if (captureReadbackPath && (captureLineBuffer == VK_NULL_HANDLE || captureLineMapped == nullptr))
        return false;

    if (useSynchronousContext)
    {
        const u64 waitStartNs = PerfNowNs();
        const VkResult waitResult = vkWaitForFences(Device, 1, &frameFence, VK_TRUE, UINT64_MAX);
        if (waitResult != VK_SUCCESS)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: graphics vkWaitForFences failed (%d)", static_cast<int>(waitResult));
            return false;
        }

        FenceWaitCpuWindow.Add(PerfNowNs() - waitStartNs);
        consumeGpuTiming(nullptr);
    }

    if (!updateToonBuffer(context, toonTable))
        return false;

    if (!Triangles.empty())
        std::memcpy(triangleMapped, Triangles.data(), Triangles.size() * sizeof(TriangleGpu));
    if (!GraphicsVertices.empty())
        std::memcpy(graphicsVertexMapped, GraphicsVertices.data(), GraphicsVertices.size() * sizeof(GraphicsVertexGpu));
    if (!GraphicsSceneVertices.empty())
        std::memcpy(graphicsSceneVertexMapped, GraphicsSceneVertices.data(), GraphicsSceneVertices.size() * sizeof(GraphicsVertexGpu));
    if (!SharedGraphicsScene.EdgeIndices.empty())
        std::memcpy(graphicsEdgeIndexMapped, SharedGraphicsScene.EdgeIndices.data(), SharedGraphicsScene.EdgeIndices.size() * sizeof(u16));

    updateGraphicsDescriptorSet(context);
    const VkDescriptorSet descriptorSet = context != nullptr ? context->GraphicsDescriptorSet : GraphicsDescriptorSet;
    if (descriptorSet == VK_NULL_HANDLE)
        return false;

    const bool useCaptureDownscaleForReadback = readbackToCpu
        && captureReadbackPath
        && (ColorImageWidth != kCaptureReadbackWidth || ColorImageHeight != kCaptureReadbackHeight);
    const u32 readbackWidth = useCaptureDownscaleForReadback ? kCaptureReadbackWidth : ColorImageWidth;
    const u32 readbackHeight = useCaptureDownscaleForReadback ? kCaptureReadbackHeight : ColorImageHeight;

    if (readbackToCpu)
    {
        if (useCaptureDownscaleForReadback && !ensureCaptureReadbackImage())
            return false;
        const VkDeviceSize requiredReadbackSize = static_cast<VkDeviceSize>(readbackWidth) * static_cast<VkDeviceSize>(readbackHeight) * sizeof(u32);
        if (ReadbackBuffer == VK_NULL_HANDLE || ReadbackMemory == VK_NULL_HANDLE || ReadbackSize != requiredReadbackSize)
        {
            destroyReadbackBuffer();
            if (!createReadbackBuffer(readbackWidth, readbackHeight))
                return false;
        }
    }

    if (vkResetFences(Device, 1, &frameFence) != VK_SUCCESS)
        return false;
    if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS)
        return false;

    RasterPushConstants pushConstants{};
    pushConstants.width = ColorImageWidth;
    pushConstants.height = ColorImageHeight;
    pushConstants.clearColor = rgbaColor;
    pushConstants.clearDepth = clearDepth;
    pushConstants.triangleCount = static_cast<u32>(Triangles.size());
    pushConstants.dispCnt = dispCnt;
    pushConstants.alphaRef = alphaRef;
    pushConstants.fogColor = fogColor;
    pushConstants.fogOffset = fogOffset;
    pushConstants.fogShift = fogShift;
    pushConstants.clearAttr = clearAttr;
    for (u32 i = 0; i < 34; i++)
    {
        const u32 density = fogDensityTable != nullptr ? static_cast<u32>(fogDensityTable[i]) : 0u;
        pushConstants.fogDensityPacked[i / 4u] |= (density & 0xFFu) << ((i % 4u) * 8u);
    }
    for (u32 i = 0; i < 8; i++)
    {
        const u16 edgeColor = edgeColorTable != nullptr ? edgeColorTable[i] : 0u;
        u32 r = (edgeColor << 1u) & 0x3Eu;
        u32 g = (edgeColor >> 4u) & 0x3Eu;
        u32 b = (edgeColor >> 9u) & 0x3Eu;
        if (r) r++;
        if (g) g++;
        if (b) b++;
        pushConstants.edgeColorPacked[i] =
            ((r << 2u) | (r >> 4u)) |
            ((((g << 2u) | (g >> 4u)) & 0xFFu) << 8u) |
            ((((b << 2u) | (b >> 4u)) & 0xFFu) << 16u);
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        return false;

    if (timestampQueryPool != VK_NULL_HANDLE && ResetQueryPool != nullptr)
    {
        ResetQueryPool(Device, timestampQueryPool, 0, TimestampQueryCount);
        timestampPending = true;
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampQueryPool, 0);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampQueryPool, 1);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampQueryPool, 2);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampQueryPool, 3);
    }

    std::array<VkBufferMemoryBarrier, 6> graphicsReadBarriers{};
    u32 graphicsReadBarrierCount = 0;

    VkBufferMemoryBarrier triangleBufferBarrier{};
    triangleBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    triangleBufferBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    triangleBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    triangleBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    triangleBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    triangleBufferBarrier.buffer = triangleBuffer;
    triangleBufferBarrier.offset = 0;
    triangleBufferBarrier.size = VK_WHOLE_SIZE;
    graphicsReadBarriers[graphicsReadBarrierCount++] = triangleBufferBarrier;

    VkBufferMemoryBarrier graphicsVertexBufferBarrier{};
    graphicsVertexBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    graphicsVertexBufferBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    graphicsVertexBufferBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    graphicsVertexBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    graphicsVertexBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    graphicsVertexBufferBarrier.buffer = graphicsVertexBuffer;
    graphicsVertexBufferBarrier.offset = 0;
    graphicsVertexBufferBarrier.size = VK_WHOLE_SIZE;
    graphicsReadBarriers[graphicsReadBarrierCount++] = graphicsVertexBufferBarrier;

    VkBufferMemoryBarrier graphicsSceneVertexBufferBarrier{};
    graphicsSceneVertexBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    graphicsSceneVertexBufferBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    graphicsSceneVertexBufferBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    graphicsSceneVertexBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    graphicsSceneVertexBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    graphicsSceneVertexBufferBarrier.buffer = graphicsSceneVertexBuffer;
    graphicsSceneVertexBufferBarrier.offset = 0;
    graphicsSceneVertexBufferBarrier.size = VK_WHOLE_SIZE;
    graphicsReadBarriers[graphicsReadBarrierCount++] = graphicsSceneVertexBufferBarrier;

    VkBufferMemoryBarrier graphicsEdgeIndexBufferBarrier{};
    graphicsEdgeIndexBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    graphicsEdgeIndexBufferBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    graphicsEdgeIndexBufferBarrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
    graphicsEdgeIndexBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    graphicsEdgeIndexBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    graphicsEdgeIndexBufferBarrier.buffer = graphicsEdgeIndexBuffer;
    graphicsEdgeIndexBufferBarrier.offset = 0;
    graphicsEdgeIndexBufferBarrier.size = VK_WHOLE_SIZE;
    graphicsReadBarriers[graphicsReadBarrierCount++] = graphicsEdgeIndexBufferBarrier;

    if (toonBuffer != VK_NULL_HANDLE)
    {
        VkBufferMemoryBarrier toonBufferBarrier{};
        toonBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        toonBufferBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        toonBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toonBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toonBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toonBufferBarrier.buffer = toonBuffer;
        toonBufferBarrier.offset = 0;
        toonBufferBarrier.size = VK_WHOLE_SIZE;
        graphicsReadBarriers[graphicsReadBarrierCount++] = toonBufferBarrier;
    }

    if (clearBuffer != VK_NULL_HANDLE)
    {
        VkBufferMemoryBarrier clearBufferBarrier{};
        clearBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        clearBufferBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        clearBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        clearBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        clearBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        clearBufferBarrier.buffer = clearBuffer;
        clearBufferBarrier.offset = 0;
        clearBufferBarrier.size = VK_WHOLE_SIZE;
        graphicsReadBarriers[graphicsReadBarrierCount++] = clearBufferBarrier;
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        graphicsReadBarrierCount,
        graphicsReadBarriers.data(),
        0,
        nullptr
    );

    // graphics_hw reuses the same images across frames. Adreno is especially
    // sensitive to beginning a render pass with reused attachments still in the
    // previous frame's layout while the render pass declares UNDEFINED-like
    // semantics. Explicitly transition each attachment back into the layout
    // that the raster render pass expects before the clear/draw pass starts.
    std::array<VkImageMemoryBarrier, 4> rasterAttachmentBarriers{};
    for (VkImageMemoryBarrier& barrier : rasterAttachmentBarriers)
    {
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
    }

    rasterAttachmentBarriers[0].image = ColorImage;
    rasterAttachmentBarriers[0].srcAccessMask = ColorImageLayout != VK_IMAGE_LAYOUT_UNDEFINED
        ? (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT)
        : 0u;
    rasterAttachmentBarriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    rasterAttachmentBarriers[0].oldLayout = ColorImageLayout;
    rasterAttachmentBarriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    rasterAttachmentBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    rasterAttachmentBarriers[1].image = AttrImage;
    rasterAttachmentBarriers[1].srcAccessMask = AttrImageLayout != VK_IMAGE_LAYOUT_UNDEFINED
        ? (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT)
        : 0u;
    rasterAttachmentBarriers[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    rasterAttachmentBarriers[1].oldLayout = AttrImageLayout;
    rasterAttachmentBarriers[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    rasterAttachmentBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    rasterAttachmentBarriers[2].image = DepthImage;
    rasterAttachmentBarriers[2].srcAccessMask = DepthImageLayout != VK_IMAGE_LAYOUT_UNDEFINED
        ? (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT)
        : 0u;
    rasterAttachmentBarriers[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    rasterAttachmentBarriers[2].oldLayout = DepthImageLayout;
    rasterAttachmentBarriers[2].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    rasterAttachmentBarriers[2].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    rasterAttachmentBarriers[3].image = DepthStencilImage;
    rasterAttachmentBarriers[3].srcAccessMask = DepthStencilImageLayout != VK_IMAGE_LAYOUT_UNDEFINED
        ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
        : 0u;
    rasterAttachmentBarriers[3].dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    rasterAttachmentBarriers[3].oldLayout = DepthStencilImageLayout;
    rasterAttachmentBarriers[3].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    rasterAttachmentBarriers[3].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

    const VkPipelineStageFlags rasterAttachmentSrcStage = ColorImageLayout != VK_IMAGE_LAYOUT_UNDEFINED
        ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    vkCmdPipelineBarrier(
        commandBuffer,
        rasterAttachmentSrcStage,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        static_cast<u32>(rasterAttachmentBarriers.size()),
        rasterAttachmentBarriers.data()
    );

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(ColorImageWidth);
    viewport.height = static_cast<float>(ColorImageHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent.width = ColorImageWidth;
    scissor.extent.height = ColorImageHeight;

    auto unpackNormalizedByte = [](u32 value) -> float {
        return static_cast<float>(value & 0xFFu) * (1.0f / 255.0f);
    };
    const float clearDepthNormalized = static_cast<float>(clearDepth) * (1.0f / 16777215.0f);
    const float clearFog = ((clearAttr >> 15u) & 0x1u) != 0u ? 1.0f : 0.0f;
    const float clearPolyId = static_cast<float>((clearAttr >> 24u) & 0x3Fu) * (1.0f / 63.0f);

    std::array<VkClearValue, 4> clearValues{};
    clearValues[0].color.float32[0] = unpackNormalizedByte(rgbaColor);
    clearValues[0].color.float32[1] = unpackNormalizedByte(rgbaColor >> 8u);
    clearValues[0].color.float32[2] = unpackNormalizedByte(rgbaColor >> 16u);
    clearValues[0].color.float32[3] = unpackNormalizedByte(rgbaColor >> 24u);
    clearValues[1].color.float32[0] = clearPolyId;
    clearValues[1].color.float32[1] = 0.0f;
    clearValues[1].color.float32[2] = clearFog;
    clearValues[1].color.float32[3] = 1.0f;
    clearValues[2].color.float32[0] = clearDepthNormalized;
    clearValues[3].depthStencil.depth = clearDepthNormalized;
    clearValues[3].depthStencil.stencil = 0xFFu;

    VkRenderPassBeginInfo rasterBeginInfo{};
    rasterBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rasterBeginInfo.renderPass = GraphicsRasterRenderPass;
    rasterBeginInfo.framebuffer = GraphicsRasterFramebuffer;
    rasterBeginInfo.renderArea.extent.width = ColorImageWidth;
    rasterBeginInfo.renderArea.extent.height = ColorImageHeight;
    rasterBeginInfo.clearValueCount = static_cast<u32>(clearValues.size());
    rasterBeginInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &rasterBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const bool useBitmapClear = (dispCnt & (1u << 14u)) != 0u;
    if (useBitmapClear && GraphicsClearPipeline != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GraphicsClearPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GraphicsPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, GraphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
        vkCmdSetStencilCompareMask(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFFu);
        vkCmdSetStencilWriteMask(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFFu);
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFFu);
        vkCmdDraw(commandBuffer, 3u, 1u, 0u, 0u);
    }

    const VkDeviceSize graphicsVertexOffset = 0u;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &graphicsVertexBuffer, &graphicsVertexOffset);

    const u64 rasterCpuStartNs = PerfNowNs();
    const u64 graphicsMainCpuStartNs = PerfNowNs();
    u32 drawCount = 0;
    struct GraphicsPassDebugStats
    {
        u32 opaque = 0;
        u32 edge = 0;
        u32 bgZeroShadowMask = 0;
        u32 bgZeroNeedOpaque = 0;
        u32 bgZeroShadowBlend = 0;
        u32 bgZeroTranslucent = 0;
        u32 bgZeroShadowSkippedPolyId = 0;
        u32 mainShadowMask = 0;
        u32 mainNeedOpaque = 0;
        u32 mainShadowClear = 0;
        u32 mainShadowBlend = 0;
        u32 mainTranslucent = 0;
        u32 paletteUiOpaqueReplay = 0;
        u32 wBufferFragmentDepth = 0;
    } graphicsPassDebugStats{};

    VkPipeline boundGraphicsPipeline = VK_NULL_HANDLE;
    bool graphicsDescriptorSetBound = false;
    u32 boundStencilCompareMask = std::numeric_limits<u32>::max();
    u32 boundStencilWriteMask = std::numeric_limits<u32>::max();
    u32 boundStencilReference = std::numeric_limits<u32>::max();
    const auto bindGraphicsPipelineCached = [&](VkPipeline pipeline) {
        if (boundGraphicsPipeline == pipeline)
            return;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        boundGraphicsPipeline = pipeline;
    };
    const auto bindGraphicsDescriptorSetCached = [&]() {
        if (graphicsDescriptorSetBound)
            return;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GraphicsPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        graphicsDescriptorSetBound = true;
    };
    const auto setStencilStateCached = [&](u32 compareMask, u32 writeMask, u32 reference) {
        if (boundStencilCompareMask != compareMask)
        {
            vkCmdSetStencilCompareMask(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, compareMask);
            boundStencilCompareMask = compareMask;
        }
        if (boundStencilWriteMask != writeMask)
        {
            vkCmdSetStencilWriteMask(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, writeMask);
            boundStencilWriteMask = writeMask;
        }
        if (boundStencilReference != reference)
        {
            vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
            boundStencilReference = reference;
        }
    };

    const auto opaquePipelineIndexFor = [&](const GraphicsPolygonDraw& draw) -> u32 {
        const bool wBuffer = draw.triangleCount > 0u
            && draw.firstTriangle < Triangles.size()
            && ((Triangles[draw.firstTriangle].flags & kTriangleFlagWBuffer) != 0u);
        const u32 wMode = wBuffer ? 1u : 0u;
        const u32 depthCompareMode = (draw.polyAttr & (1u << 14u)) != 0u ? 1u : 0u;
        return (wMode * GraphicsDepthCompareModeCount) + depthCompareMode;
    };
    const auto requiresWBufferFragmentDepth = [&](const GraphicsPolygonDraw& draw) -> bool {
        if (draw.triangleCount == 0u || draw.firstTriangle >= Triangles.size())
            return false;

        const TriangleGpu& firstTriangle = Triangles[draw.firstTriangle];
        const u32 flags = firstTriangle.flags;
        if ((flags & kTriangleFlagWBuffer) == 0u
            || (flags & kTriangleFlagTextured) == 0u
            || (flags & kTriangleFlagDecal) != 0u
            || (flags & kTriangleFlagLinear) != 0u)
        {
            return false;
        }

        const u32 alpha5 = (draw.polyAttr >> 16u) & 0x1Fu;
        const u32 blendMode = (draw.polyAttr >> 4u) & 0x3u;
        const bool depthWriteEnabled = (draw.polyAttr & (1u << 11u)) != 0u;
        const bool repeatOrMirror =
            (firstTriangle.texParam & ((1u << 16u) | (1u << 17u) | (1u << 18u) | (1u << 19u))) != 0u;
        if (!depthWriteEnabled || alpha5 != 0x1Fu || blendMode != 0u || !repeatOrMirror)
            return false;

        if (draw.firstVertex >= GraphicsSceneVertices.size())
            return false;

        const u32 vertexEnd = std::min<u32>(
            static_cast<u32>(GraphicsSceneVertices.size()),
            draw.firstVertex + draw.vertexCount);
        if (vertexEnd <= draw.firstVertex + 1u)
            return false;

        const float firstReciprocalW = GraphicsSceneVertices[draw.firstVertex].reciprocalW;
        for (u32 vertexIndex = draw.firstVertex + 1u; vertexIndex < vertexEnd; vertexIndex++)
        {
            if (std::abs(GraphicsSceneVertices[vertexIndex].reciprocalW - firstReciprocalW) > 0.0000001f)
                return true;
        }
        return false;
    };
    const auto opaquePipelineFor = [&](const GraphicsPolygonDraw& draw, u32 pipelineIndex) -> VkPipeline {
        if (requiresWBufferFragmentDepth(draw))
        {
            graphicsPassDebugStats.wBufferFragmentDepth++;
            if (pipelineIndex < GraphicsOpaqueFragmentDepthPipelines.size()
                && GraphicsOpaqueFragmentDepthPipelines[pipelineIndex] != VK_NULL_HANDLE)
            {
                return GraphicsOpaqueFragmentDepthPipelines[pipelineIndex];
            }
        }
        return pipelineIndex < GraphicsOpaquePipelines.size()
            ? GraphicsOpaquePipelines[pipelineIndex]
            : VK_NULL_HANDLE;
    };
    const auto bgZeroTranslucentPipelineIndexFor = [&](const GraphicsPolygonDraw& draw, bool fogWrite) -> u32 {
        const bool wBuffer = draw.triangleCount > 0u
            && draw.firstTriangle < Triangles.size()
            && ((Triangles[draw.firstTriangle].flags & kTriangleFlagWBuffer) != 0u);
        const u32 wMode = wBuffer ? 1u : 0u;
        const u32 depthCompareMode = (draw.polyAttr & (1u << 14u)) != 0u ? 1u : 0u;
        const u32 depthWriteMode = (draw.polyAttr & (1u << 11u)) != 0u ? 1u : 0u;
        const u32 fogWriteMode = fogWrite ? 1u : 0u;
        return (((wMode * GraphicsDepthCompareModeCount) + depthCompareMode) * GraphicsDepthWriteModeCount + depthWriteMode)
            * GraphicsFogWriteModeCount
            + fogWriteMode;
    };
    const auto translucentPipelineIndexFor = [&](const GraphicsPolygonDraw& draw, bool fogWrite, bool alphaBlendEnabled) -> u32 {
        const bool wBuffer = draw.triangleCount > 0u
            && draw.firstTriangle < Triangles.size()
            && ((Triangles[draw.firstTriangle].flags & kTriangleFlagWBuffer) != 0u);
        const u32 wMode = wBuffer ? 1u : 0u;
        const u32 depthCompareMode = (draw.polyAttr & (1u << 14u)) != 0u ? 1u : 0u;
        const u32 depthWriteMode = (draw.polyAttr & (1u << 11u)) != 0u ? 1u : 0u;
        const u32 fogWriteMode = fogWrite ? 1u : 0u;
        const u32 alphaBlendMode = alphaBlendEnabled ? 1u : 0u;
        return ((((wMode * GraphicsDepthCompareModeCount) + depthCompareMode) * GraphicsDepthWriteModeCount + depthWriteMode)
            * GraphicsFogWriteModeCount
            + fogWriteMode)
            * GraphicsAlphaBlendModeCount
            + alphaBlendMode;
    };
    const auto fogWriteEnabledFor = [&](const GraphicsPolygonDraw& draw) -> bool {
        return ((dispCnt & (1u << 7u)) != 0u) && ((draw.polyAttr & (1u << 15u)) == 0u);
    };
    const auto fastOpaqueModulatePipelineFor = [&](const GraphicsPolygonDraw& draw, u32 pipelineIndex) -> VkPipeline {
        if ((dispCnt & (1u << 0u)) == 0u
            || draw.firstTriangle >= Triangles.size()
            || pipelineIndex >= GraphicsOpaqueFastModulatePipelines.size())
        {
            return VK_NULL_HANDLE;
        }

        const u32 flags = Triangles[draw.firstTriangle].flags;
        if (requiresWBufferFragmentDepth(draw))
            return VK_NULL_HANDLE;

        const u32 requiredFlags = kTriangleFlagWBuffer | kTriangleFlagTextured;
        const u32 disallowedFlags = kTriangleFlagDecal | kTriangleFlagLinear;
        if ((flags & requiredFlags) != requiredFlags || (flags & disallowedFlags) != 0u)
            return VK_NULL_HANDLE;

        const u32 blendMode = (draw.polyAttr >> 4u) & 0x3u;
        const bool fullAlpha =
            (flags & kTriangleFlagTextureOpaque) != 0u
            && ((draw.polyAttr >> 16u) & 0x1Fu) == 0x1Fu
            && alphaRef < 0x1Fu;
        if (blendMode == 2u && (dispCnt & (1u << 1u)) == 0u)
        {
            VkPipeline pipeline = GraphicsOpaqueFastModulateToonPipelines[pipelineIndex];
            if (pipeline != VK_NULL_HANDLE)
                return pipeline;
        }
        else if (blendMode != 2u)
        {
            if (fullAlpha)
            {
                VkPipeline pipeline = GraphicsOpaqueFastModulateOpaqueAlphaPlainPipelines[pipelineIndex];
                if (pipeline != VK_NULL_HANDLE)
                    return pipeline;
            }
            VkPipeline pipeline = GraphicsOpaqueFastModulatePlainPipelines[pipelineIndex];
            if (pipeline != VK_NULL_HANDLE)
                return pipeline;
        }

        return GraphicsOpaqueFastModulatePipelines[pipelineIndex];
    };
    const auto bindAndDrawGraphics = [&](const GraphicsPolygonDraw& draw,
                                         VkPipeline pipeline,
                                         u32 stencilCompareMask,
                                         u32 stencilWriteMask,
                                         u32 stencilReference) -> bool {
        if (pipeline == VK_NULL_HANDLE || draw.triangleCount == 0u || draw.firstTriangle >= Triangles.size())
            return false;
        if (vkCmdPushConstants == nullptr
            || vkCmdDraw == nullptr
            || vkCmdBindPipeline == nullptr
            || vkCmdBindDescriptorSets == nullptr
            || vkCmdSetStencilCompareMask == nullptr
            || vkCmdSetStencilWriteMask == nullptr
            || vkCmdSetStencilReference == nullptr)
        {
            if (MelonDSAndroid::areRendererDebugToolsEnabled())
            {
                if (GraphicsDrawDispatchMissingLogCooldown == 0u)
                {
                    Log(
                        LogLevel::Warn,
                        "VulkanGraphics[DispatchMissing]: draw=%u push=%u bindPipeline=%u bindDescriptors=%u stencilCompare=%u stencilWrite=%u stencilReference=%u",
                        vkCmdDraw != nullptr ? 1u : 0u,
                        vkCmdPushConstants != nullptr ? 1u : 0u,
                        vkCmdBindPipeline != nullptr ? 1u : 0u,
                        vkCmdBindDescriptorSets != nullptr ? 1u : 0u,
                        vkCmdSetStencilCompareMask != nullptr ? 1u : 0u,
                        vkCmdSetStencilWriteMask != nullptr ? 1u : 0u,
                        vkCmdSetStencilReference != nullptr ? 1u : 0u);
                    GraphicsDrawDispatchMissingLogCooldown = 180u;
                }
                else
                {
                    GraphicsDrawDispatchMissingLogCooldown--;
                }
            }
            return false;
        }

        bindGraphicsPipelineCached(pipeline);
        bindGraphicsDescriptorSetCached();
        const TriangleGpu& firstTriangle = Triangles[draw.firstTriangle];
        pushConstants.depthBlendMode = draw.polyAttr;
        pushConstants.variantKey = ((firstTriangle.texLayer & 0xFFFFu) << 16u) | (firstTriangle.texArrayIndex & 0xFFFFu);
        pushConstants.passIndex = ((firstTriangle.texHeight & 0xFFFFu) << 16u) | (firstTriangle.texWidth & 0xFFFFu);
        pushConstants.triangleBase = firstTriangle.texParam;
        pushConstants.triangleCount = draw.triangleCount;
        vkCmdPushConstants(commandBuffer, GraphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
        setStencilStateCached(stencilCompareMask, stencilWriteMask, stencilReference);
        vkCmdDraw(commandBuffer, draw.triangleCount * 3u, 1u, draw.firstTriangle * 3u, 0u);
        drawCount++;
        return true;
    };
    const auto drawNeedOpaquePass = [&](const GraphicsPolygonDraw& draw) {
        const u32 pipelineIndex = opaquePipelineIndexFor(draw);
        VkPipeline pipeline = fastOpaqueModulatePipelineFor(draw, pipelineIndex);
        if (pipeline == VK_NULL_HANDLE)
        {
            pipeline = opaquePipelineFor(draw, pipelineIndex);
        }
        bindAndDrawGraphics(draw, pipeline, 0xFFu, 0xFFu, (draw.polyAttr >> 24u) & 0x3Fu);
    };
    const auto bindAndDrawGraphicsEdges = [&](const GraphicsPolygonDraw& draw) -> bool {
        if (draw.edgeIndexCount == 0u)
            return false;

        const u32 wMode = (draw.flags & AcceleratedPolygonFlagWBuffer) != 0u ? 1u : 0u;
        const VkPipeline pipeline = wMode < GraphicsEdgeMarkPipelines.size()
            ? GraphicsEdgeMarkPipelines[wMode]
            : VK_NULL_HANDLE;
        if (pipeline == VK_NULL_HANDLE)
            return false;
        if (vkCmdDrawIndexed == nullptr
            || vkCmdPushConstants == nullptr
            || vkCmdBindPipeline == nullptr
            || vkCmdBindDescriptorSets == nullptr)
        {
            if (MelonDSAndroid::areRendererDebugToolsEnabled())
            {
                if (GraphicsDrawDispatchMissingLogCooldown == 0u)
                {
                    Log(
                        LogLevel::Warn,
                        "VulkanGraphics[EdgeDispatchMissing]: drawIndexed=%u push=%u bindPipeline=%u bindDescriptors=%u",
                        vkCmdDrawIndexed != nullptr ? 1u : 0u,
                        vkCmdPushConstants != nullptr ? 1u : 0u,
                        vkCmdBindPipeline != nullptr ? 1u : 0u,
                        vkCmdBindDescriptorSets != nullptr ? 1u : 0u);
                    GraphicsDrawDispatchMissingLogCooldown = 120u;
                }
                else
                {
                    GraphicsDrawDispatchMissingLogCooldown--;
                }
            }
            return false;
        }

        bindGraphicsPipelineCached(pipeline);
        bindGraphicsDescriptorSetCached();
        pushConstants.depthBlendMode = wMode;
        pushConstants.triangleBase = draw.firstTriangle;
        pushConstants.triangleCount = draw.triangleCount;
        u32 savedEdgeColorPacked[8]{};
        if (draw.edgeColorOverrideMask != 0u)
        {
            for (u32 i = 0; i < 8u; i++)
            {
                if ((draw.edgeColorOverrideMask & (1u << i)) == 0u)
                    continue;
                savedEdgeColorPacked[i] = pushConstants.edgeColorPacked[i];
                pushConstants.edgeColorPacked[i] = draw.edgeColorOverridePacked;
            }
        }
        vkCmdPushConstants(commandBuffer, GraphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
        if (draw.edgeColorOverrideMask != 0u)
        {
            for (u32 i = 0; i < 8u; i++)
            {
                if ((draw.edgeColorOverrideMask & (1u << i)) != 0u)
                    pushConstants.edgeColorPacked[i] = savedEdgeColorPacked[i];
            }
        }
        vkCmdDrawIndexed(commandBuffer, draw.edgeIndexCount, 1u, draw.firstEdgeIndex, 0, 0u);
        drawCount++;
        return true;
    };
    const auto clearShadowStencilBit = [&]() {
        if (GraphicsStencilBitClearPipeline == VK_NULL_HANDLE)
            return;

        bindGraphicsPipelineCached(GraphicsStencilBitClearPipeline);
        bindGraphicsDescriptorSetCached();
        vkCmdPushConstants(commandBuffer, GraphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
        setStencilStateCached(0x80u, 0x80u, 0x00u);
        vkCmdDraw(commandBuffer, 3u, 1u, 0u, 0u);
        drawCount++;
    };
    const bool clearPlaneAlphaZero = ((clearAttr >> 16u) & 0x1Fu) == 0u;
    const u32 clearPlanePolyId = (clearAttr >> 24u) & 0x3Fu;
    const bool alphaBlendEnabled = (dispCnt & (1u << 3u)) != 0u;
    const auto drawYBounds = [&](const GraphicsPolygonDraw& draw) -> std::pair<u32, u32> {
        if (draw.firstTriangle >= Triangles.size())
            return {0u, 0u};

        const u32 yBounds = Triangles[draw.firstTriangle].yBounds;
        return {yBounds & 0xFFFFu, (yBounds >> 16u) & 0xFFFFu};
    };
    const auto yBoundsOverlap = [&](const GraphicsPolygonDraw& a, const GraphicsPolygonDraw& b) -> bool {
        const auto [aTop, aBottom] = drawYBounds(a);
        const auto [bTop, bBottom] = drawYBounds(b);
        return aBottom > bTop && bBottom > aTop;
    };
    const auto drawTopDs = [&](const GraphicsPolygonDraw& draw) -> float {
        const auto [top, bottom] = drawYBounds(draw);
        (void)bottom;
        const float scale = std::max(1.0f, static_cast<float>(ScaleFactor));
        return static_cast<float>(top) / scale;
    };
    const auto drawXBounds = [&](const GraphicsPolygonDraw& draw) -> std::pair<float, float> {
        if (draw.firstTriangle >= Triangles.size() || draw.triangleCount == 0u)
            return {0.0f, 0.0f};

        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        const u32 endTriangle = std::min<u32>(draw.firstTriangle + draw.triangleCount, static_cast<u32>(Triangles.size()));
        for (u32 triangleIndex = draw.firstTriangle; triangleIndex < endTriangle; triangleIndex++)
        {
            const TriangleGpu& tri = Triangles[triangleIndex];
            minX = std::min(minX, std::min(tri.x0, std::min(tri.x1, tri.x2)));
            maxX = std::max(maxX, std::max(tri.x0, std::max(tri.x1, tri.x2)));
        }
        return {minX, maxX};
    };
    const auto xBoundsOverlap = [&](const GraphicsPolygonDraw& a, const GraphicsPolygonDraw& b) -> bool {
        const auto [aLeft, aRight] = drawXBounds(a);
        const auto [bLeft, bRight] = drawXBounds(b);
        return aRight > bLeft && bRight > aLeft;
    };
    const auto isClampPaletteUiTriangle = [&](const TriangleGpu& tri) -> bool {
        const u32 texParam = tri.texParam;
        const u32 textureFormat = (texParam >> 26u) & 0x7u;
        const bool color0Transparent = (texParam & (1u << 29u)) != 0u;
        const bool repeatS = (texParam & (1u << 16u)) != 0u;
        const bool repeatT = (texParam & (1u << 17u)) != 0u;
        const bool mirrorS = (texParam & (1u << 18u)) != 0u;
        const bool mirrorT = (texParam & (1u << 19u)) != 0u;
        return (tri.flags & kTriangleFlagTextured) != 0u
            && (textureFormat == 2u || textureFormat == 3u)
            && color0Transparent
            && !repeatS
            && !repeatT
            && !mirrorS
            && !mirrorT;
    };
    const auto isCompactPaletteUiReplayTriangle = [&](const TriangleGpu& tri) -> bool {
        if (!isClampPaletteUiTriangle(tri))
            return false;

        const u32 texturePage = tri.texParam & 0xFFFFu;
        return texturePage == 0x05C0u
            || texturePage == 0x85C0u;
    };
    const auto isFlatDsUiPlaneTriangle = [&](const TriangleGpu& tri) -> bool {
        constexpr float kUiPlaneW = 25600.0f;
        constexpr float kUiPlaneTolerance = 0.5f;
        return std::abs(tri.w0 - kUiPlaneW) <= kUiPlaneTolerance
            && std::abs(tri.w1 - kUiPlaneW) <= kUiPlaneTolerance
            && std::abs(tri.w2 - kUiPlaneW) <= kUiPlaneTolerance;
    };
    const auto isCompactTopStatusGlyphTriangle = [&](const TriangleGpu& tri) -> bool {
        const u32 texParam = tri.texParam;
        const u32 textureFormat = (texParam >> 26u) & 0x7u;
        const bool color0Transparent = (texParam & (1u << 29u)) != 0u;
        const bool repeatS = (texParam & (1u << 16u)) != 0u;
        const bool repeatT = (texParam & (1u << 17u)) != 0u;
        const bool mirrorS = (texParam & (1u << 18u)) != 0u;
        const bool mirrorT = (texParam & (1u << 19u)) != 0u;
        return (tri.flags & kTriangleFlagTextured) != 0u
            && textureFormat == 3u
            && color0Transparent
            && (texParam & 0xFFFFu) == 0x05C0u
            && !repeatS
            && !repeatT
            && !mirrorS
            && !mirrorT;
    };
    const auto isCompactTopStatusGlyphDraw = [&](const GraphicsPolygonDraw& draw) -> bool {
        if (draw.firstTriangle >= Triangles.size())
            return false;

        const u32 alpha5 = (draw.polyAttr >> 16u) & 0x1Fu;
        const u32 blendMode = (draw.polyAttr >> 4u) & 0x3u;
        if (alpha5 != 31u
            || blendMode != 0u
            || (draw.polyAttr & (1u << 11u)) == 0u
            || !isCompactTopStatusGlyphTriangle(Triangles[draw.firstTriangle]))
        {
            return false;
        }

        const float scale = std::max(1.0f, static_cast<float>(ScaleFactor));
        const auto [xMin, xMax] = drawXBounds(draw);
        const auto [yTop, yBottom] = drawYBounds(draw);
        const float xMinDs = xMin / scale;
        const float xMaxDs = xMax / scale;
        const float yTopDs = static_cast<float>(yTop) / scale;
        const float yBottomDs = static_cast<float>(yBottom) / scale;
        return xMinDs >= 38.0f
            && xMaxDs <= 46.0f
            && yTopDs >= 6.0f
            && yBottomDs <= 16.0f;
    };
    const auto isCompactTopStatusGlyphOverlay = [&](const GraphicsPolygonDraw& draw) -> bool {
        if (!clearPlaneAlphaZero
            || !alphaBlendEnabled
            || draw.firstTriangle >= Triangles.size()
            || (draw.flags & AcceleratedPolygonFlagTranslucent) == 0u)
        {
            return false;
        }

        const u32 alpha5 = (draw.polyAttr >> 16u) & 0x1Fu;
        const u32 blendMode = (draw.polyAttr >> 4u) & 0x3u;
        return alpha5 == 9u
            && blendMode == 0u
            && (draw.polyAttr & (1u << 11u)) != 0u
            && isCompactTopStatusGlyphTriangle(Triangles[draw.firstTriangle]);
    };
    const auto isTranslucentPaletteUiOverlay = [&](const GraphicsPolygonDraw& draw) -> bool {
        if (!clearPlaneAlphaZero
            || !alphaBlendEnabled
            || draw.firstTriangle >= Triangles.size()
            || (draw.flags & AcceleratedPolygonFlagTranslucent) == 0u)
        {
            return false;
        }

        const u32 alpha5 = (draw.polyAttr >> 16u) & 0x1Fu;
        const u32 blendMode = (draw.polyAttr >> 4u) & 0x3u;
        const u32 polyId = (draw.polyAttr >> 24u) & 0x3Fu;
        return alpha5 > 0u
            && alpha5 < 31u
            && blendMode == 0u
            && polyId >= 3u
            && (draw.polyAttr & (1u << 11u)) == 0u
            && isClampPaletteUiTriangle(Triangles[draw.firstTriangle]);
    };
    const auto isPaletteUiHelpPanelOverlay = [&](const GraphicsPolygonDraw& draw) -> bool {
        if (!isTranslucentPaletteUiOverlay(draw))
            return false;

        const u32 alpha5 = (draw.polyAttr >> 16u) & 0x1Fu;
        const u32 polyId = (draw.polyAttr >> 24u) & 0x3Fu;
        const u32 texParam = Triangles[draw.firstTriangle].texParam;
        return alpha5 == 24u
            && polyId == 11u
            && texParam == 0x6DC00200u;
    };
    const bool hasLowAlphaPaletteUiOverlay = [&]() -> bool {
        for (u32 alphaDrawIndex : GraphicsAlphaDrawIndices)
        {
            if (alphaDrawIndex >= GraphicsPolygons.size())
                continue;

            const GraphicsPolygonDraw& alphaDraw = GraphicsPolygons[alphaDrawIndex];
            if (!isTranslucentPaletteUiOverlay(alphaDraw))
                continue;

            const u32 alpha5 = (alphaDraw.polyAttr >> 16u) & 0x1Fu;
            if (alpha5 < 27u)
                return true;
        }
        return false;
    }();
    const auto shouldReplayOpaquePaletteUiDraw = [&](const GraphicsPolygonDraw& draw) -> bool {
        if (!clearPlaneAlphaZero
            || !alphaBlendEnabled
            || draw.firstTriangle >= Triangles.size()
            || (draw.flags & AcceleratedPolygonFlagTranslucent) != 0u)
        {
            return false;
        }

        const u32 alpha5 = (draw.polyAttr >> 16u) & 0x1Fu;
        const u32 blendMode = (draw.polyAttr >> 4u) & 0x3u;
        const u32 texParam = draw.firstTriangle < Triangles.size() ? Triangles[draw.firstTriangle].texParam : 0u;
        const bool compactStatusGlyph = isCompactTopStatusGlyphDraw(draw);
        const bool paletteUiReplay =
            alpha5 == 31u
            && blendMode == 0u
            && (draw.polyAttr & (1u << 11u)) == 0u
            && texParam != 0x68C01B10u
            && texParam != 0x6A5016D0u
            && isClampPaletteUiTriangle(Triangles[draw.firstTriangle])
            && isFlatDsUiPlaneTriangle(Triangles[draw.firstTriangle]);
        if (!compactStatusGlyph && !paletteUiReplay)
        {
            return false;
        }
        if (paletteUiReplay
            && !hasLowAlphaPaletteUiOverlay
            && ((draw.polyAttr >> 24u) & 0x3Fu) != 0u)
        {
            return false;
        }

        bool matchesPaletteUiOverlay = false;
        for (u32 alphaDrawIndex : GraphicsAlphaDrawIndices)
        {
            if (alphaDrawIndex >= GraphicsPolygons.size())
                continue;

            const GraphicsPolygonDraw& alphaDraw = GraphicsPolygons[alphaDrawIndex];
            if (compactStatusGlyph)
            {
                if (isCompactTopStatusGlyphOverlay(alphaDraw)
                    && yBoundsOverlap(draw, alphaDraw))
                {
                    return true;
                }
                continue;
            }

            if (!isTranslucentPaletteUiOverlay(alphaDraw)
                || !yBoundsOverlap(draw, alphaDraw))
            {
                continue;
            }

            if (isPaletteUiHelpPanelOverlay(alphaDraw)
                && xBoundsOverlap(draw, alphaDraw))
            {
                return false;
            }
            const u32 alpha5 = (alphaDraw.polyAttr >> 16u) & 0x1Fu;
            if (!hasLowAlphaPaletteUiOverlay && alpha5 >= 27u && drawTopDs(draw) >= 18.0f)
            {
                return false;
            }
            matchesPaletteUiOverlay = true;
        }
        return matchesPaletteUiOverlay;
    };

    u32 paletteUiOpaqueReplayFirstDraw = 0xFFFFFFFFu;
    u32 paletteUiOpaqueReplayFirstPolyId = 0xFFFFFFFFu;
    u32 paletteUiOpaqueReplayFirstTexParam = 0u;
    for (u32 drawIndex : GraphicsOpaqueDrawIndices)
    {
        if (drawIndex >= GraphicsPolygons.size())
            continue;

        const GraphicsPolygonDraw& draw = GraphicsPolygons[drawIndex];
        const u32 pipelineIndex = opaquePipelineIndexFor(draw);
        VkPipeline pipeline = fastOpaqueModulatePipelineFor(draw, pipelineIndex);
        if (pipeline == VK_NULL_HANDLE)
        {
            pipeline = opaquePipelineFor(draw, pipelineIndex);
        }
        if (bindAndDrawGraphics(draw, pipeline, 0xFFu, 0xFFu, (draw.polyAttr >> 24u) & 0x3Fu))
            graphicsPassDebugStats.opaque++;
    }
    if ((timestampQueryPool != VK_NULL_HANDLE) && timestampPending)
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, timestampQueryPool, 3);

    if ((dispCnt & (1u << 5u)) != 0u)
    {
        const VkDeviceSize graphicsSceneVertexOffset = 0u;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &graphicsSceneVertexBuffer, &graphicsSceneVertexOffset);
        vkCmdBindIndexBuffer(commandBuffer, graphicsEdgeIndexBuffer, 0u, VK_INDEX_TYPE_UINT16);

        for (const GraphicsPolygonDraw& draw : GraphicsPolygons)
        {
            if ((draw.flags & AcceleratedPolygonFlagShadowMask) != 0u)
                continue;

            if (bindAndDrawGraphicsEdges(draw))
                graphicsPassDebugStats.edge++;
        }

        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &graphicsVertexBuffer, &graphicsVertexOffset);
    }

    if ((timestampQueryPool != VK_NULL_HANDLE) && timestampPending)
    {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, timestampQueryPool, 4);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, timestampQueryPool, 5);
    }
    GraphicsMainCpuWindow.Add(PerfNowNs() - graphicsMainCpuStartNs);

    const u64 graphicsAlphaCpuStartNs = PerfNowNs();
    if (clearPlaneAlphaZero)
    {
        for (const GraphicsPolygonDraw& draw : GraphicsPolygons)
        {
            if (draw.triangleCount == 0u)
                continue;

            const bool isShadowMask = (draw.flags & AcceleratedPolygonFlagShadowMask) != 0u;
            const bool isTranslucent = (draw.flags & AcceleratedPolygonFlagTranslucent) != 0u;
            const bool isShadow = (draw.flags & AcceleratedPolygonFlagShadow) != 0u;
            const bool needOpaque = (draw.flags & AcceleratedPolygonFlagNeedOpaquePass) != 0u;
            const u32 polyId = (draw.polyAttr >> 24u) & 0x3Fu;

            if (isShadowMask)
            {
                const bool wBuffer = (Triangles[draw.firstTriangle].flags & kTriangleFlagWBuffer) != 0u;
                const u32 wMode = wBuffer ? 1u : 0u;
                const VkPipeline pipeline = wMode < GraphicsShadowMaskBgZeroPipelines.size()
                    ? GraphicsShadowMaskBgZeroPipelines[wMode]
                    : VK_NULL_HANDLE;
                if (bindAndDrawGraphics(draw, pipeline, 0xFFu, 0x01u, 0xFFu))
                    graphicsPassDebugStats.bgZeroShadowMask++;
                continue;
            }

            if (!isTranslucent)
                continue;

            if (needOpaque)
            {
                drawNeedOpaquePass(draw);
                graphicsPassDebugStats.bgZeroNeedOpaque++;
            }

            const bool fogWrite = fogWriteEnabledFor(draw);
            const u32 writeMask = static_cast<u32>(~(0x40u | polyId)) & 0xFFu;
            if (isShadow)
            {
                if (polyId != clearPlanePolyId)
                {
                    graphicsPassDebugStats.bgZeroShadowSkippedPolyId++;
                    continue;
                }

                const u32 pipelineIndex = translucentPipelineIndexFor(draw, fogWrite, alphaBlendEnabled);
                const VkPipeline pipeline = pipelineIndex < GraphicsShadowBlendBgZeroPipelines.size()
                    ? GraphicsShadowBlendBgZeroPipelines[pipelineIndex]
                    : VK_NULL_HANDLE;
                if (bindAndDrawGraphics(draw, pipeline, 0xFFu, writeMask, 0xFEu))
                    graphicsPassDebugStats.bgZeroShadowBlend++;
            }
            else
            {
                const u32 pipelineIndex = bgZeroTranslucentPipelineIndexFor(draw, fogWrite);
                const VkPipeline pipeline = pipelineIndex < GraphicsBgZeroTranslucentPipelines.size()
                    ? GraphicsBgZeroTranslucentPipelines[pipelineIndex]
                    : VK_NULL_HANDLE;
                if (bindAndDrawGraphics(draw, pipeline, 0xFEu, writeMask, 0xFFu))
                    graphicsPassDebugStats.bgZeroTranslucent++;
            }
        }
    }

    for (const GraphicsPolygonDraw& draw : GraphicsPolygons)
    {
        if (draw.triangleCount == 0u)
            continue;

        const bool isShadowMask = (draw.flags & AcceleratedPolygonFlagShadowMask) != 0u;
        const bool isTranslucent = (draw.flags & AcceleratedPolygonFlagTranslucent) != 0u;
        const bool isShadow = (draw.flags & AcceleratedPolygonFlagShadow) != 0u;
        const bool needOpaque = (draw.flags & AcceleratedPolygonFlagNeedOpaquePass) != 0u;
        const u32 polyId = (draw.polyAttr >> 24u) & 0x3Fu;

        if (isShadowMask)
        {
            clearShadowStencilBit();

            const bool wBuffer = (Triangles[draw.firstTriangle].flags & kTriangleFlagWBuffer) != 0u;
            const u32 wMode = wBuffer ? 1u : 0u;
            const VkPipeline pipeline = wMode < GraphicsShadowMaskPipelines.size()
                ? GraphicsShadowMaskPipelines[wMode]
                : VK_NULL_HANDLE;
            if (bindAndDrawGraphics(draw, pipeline, 0x80u, 0x80u, 0x80u))
                graphicsPassDebugStats.mainShadowMask++;
            continue;
        }

        if (!isTranslucent)
            continue;

        if (needOpaque)
        {
            drawNeedOpaquePass(draw);
            graphicsPassDebugStats.mainNeedOpaque++;
        }

        const bool fogWrite = fogWriteEnabledFor(draw);
        if (isShadow)
        {
            const u32 clearPipelineIndex = opaquePipelineIndexFor(draw);
            const VkPipeline clearPipeline = clearPipelineIndex < GraphicsShadowClearPipelines.size()
                ? GraphicsShadowClearPipelines[clearPipelineIndex]
                : VK_NULL_HANDLE;
            if (bindAndDrawGraphics(draw, clearPipeline, 0x3Fu, 0x80u, polyId))
                graphicsPassDebugStats.mainShadowClear++;

            const u32 blendPipelineIndex = translucentPipelineIndexFor(draw, fogWrite, alphaBlendEnabled);
            const VkPipeline blendPipeline = blendPipelineIndex < GraphicsShadowBlendPipelines.size()
                ? GraphicsShadowBlendPipelines[blendPipelineIndex]
                : VK_NULL_HANDLE;
            if (bindAndDrawGraphics(draw, blendPipeline, 0x80u, 0x7Fu, 0xC0u | polyId))
                graphicsPassDebugStats.mainShadowBlend++;
        }
        else
        {
            const u32 pipelineIndex = translucentPipelineIndexFor(draw, fogWrite, alphaBlendEnabled);
            const VkPipeline pipeline = pipelineIndex < GraphicsTranslucentPipelines.size()
                ? GraphicsTranslucentPipelines[pipelineIndex]
                : VK_NULL_HANDLE;
            if (bindAndDrawGraphics(draw, pipeline, 0x7Fu, 0x7Fu, 0x40u | polyId))
                graphicsPassDebugStats.mainTranslucent++;
        }
    }

    for (u32 drawIndex : GraphicsOpaqueDrawIndices)
    {
        if (drawIndex >= GraphicsPolygons.size())
            continue;

        const GraphicsPolygonDraw& draw = GraphicsPolygons[drawIndex];
        if (!shouldReplayOpaquePaletteUiDraw(draw))
            continue;

        GraphicsPolygonDraw replayDraw = draw;
        replayDraw.polyAttr |= 1u << 14u;
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (isCompactTopStatusGlyphDraw(draw))
        {
            const bool wBuffer = replayDraw.triangleCount > 0u
                && replayDraw.firstTriangle < Triangles.size()
                && ((Triangles[replayDraw.firstTriangle].flags & kTriangleFlagWBuffer) != 0u);
            const u32 wMode = wBuffer ? 1u : 0u;
            pipeline = wMode < GraphicsOpaqueUiOverlayPipelines.size()
                ? GraphicsOpaqueUiOverlayPipelines[wMode]
                : VK_NULL_HANDLE;
        }
        else
        {
            const u32 pipelineIndex = opaquePipelineIndexFor(replayDraw);
            pipeline = fastOpaqueModulatePipelineFor(replayDraw, pipelineIndex);
            if (pipeline == VK_NULL_HANDLE)
            {
                pipeline = opaquePipelineFor(replayDraw, pipelineIndex);
            }
        }
        if (bindAndDrawGraphics(replayDraw, pipeline, 0xFFu, 0xFFu, (replayDraw.polyAttr >> 24u) & 0x3Fu))
        {
            if (graphicsPassDebugStats.paletteUiOpaqueReplay == 0u)
            {
                paletteUiOpaqueReplayFirstDraw = drawIndex;
                paletteUiOpaqueReplayFirstPolyId = (replayDraw.polyAttr >> 24u) & 0x3Fu;
                paletteUiOpaqueReplayFirstTexParam = Triangles[replayDraw.firstTriangle].texParam;
            }
            graphicsPassDebugStats.paletteUiOpaqueReplay++;
        }
    }
    GraphicsAlphaCpuWindow.Add(PerfNowNs() - graphicsAlphaCpuStartNs);
    if ((timestampQueryPool != VK_NULL_HANDLE) && timestampPending)
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, timestampQueryPool, 6);

    if (MelonDSAndroid::areRendererDebugToolsEnabled())
    {
        const bool paletteUiOpaqueReplayActive = graphicsPassDebugStats.paletteUiOpaqueReplay > 0u;
        if (PaletteUiOpaqueReplayLogCooldown == 0u || paletteUiOpaqueReplayActive != PaletteUiOpaqueReplayLastActive)
        {
            Log(
                LogLevel::Warn,
                "VulkanGraphics[PaletteUiOpaqueReplay]: active=%u replayed=%u firstDraw=%u firstPolyId=%u firstTexParam=%08X clearAlphaZero=%u alphaBlend=%u",
                paletteUiOpaqueReplayActive ? 1u : 0u,
                graphicsPassDebugStats.paletteUiOpaqueReplay,
                paletteUiOpaqueReplayFirstDraw,
                paletteUiOpaqueReplayFirstPolyId,
                paletteUiOpaqueReplayFirstTexParam,
                clearPlaneAlphaZero ? 1u : 0u,
                alphaBlendEnabled ? 1u : 0u);
            PaletteUiOpaqueReplayLogCooldown = paletteUiOpaqueReplayActive ? 60u : 180u;
            PaletteUiOpaqueReplayLastActive = paletteUiOpaqueReplayActive;
        }
        else
        {
            PaletteUiOpaqueReplayLogCooldown--;
        }
    }

    if (MelonDSAndroid::areRendererDebugToolsEnabled())
    {
        u32 paletteUiGateCandidates = 0u;
        u32 paletteUiGateFirstDraw = 0xFFFFFFFFu;
        u32 paletteUiGateFirstPolyId = 0xFFFFFFFFu;
        u32 paletteUiGateFirstAlpha5 = 0xFFFFFFFFu;
        for (u32 drawIndex : GraphicsAlphaDrawIndices)
        {
            if (drawIndex >= GraphicsPolygons.size())
                continue;

            const GraphicsPolygonDraw& draw = GraphicsPolygons[drawIndex];
            if (draw.firstTriangle >= Triangles.size())
                continue;

            const TriangleGpu& tri = Triangles[draw.firstTriangle];
            const u32 texParam = tri.texParam;
            const u32 textureFormat = (texParam >> 26u) & 0x7u;
            const bool color0Transparent = (texParam & (1u << 29u)) != 0u;
            const bool repeatS = (texParam & (1u << 16u)) != 0u;
            const bool repeatT = (texParam & (1u << 17u)) != 0u;
            const bool mirrorS = (texParam & (1u << 18u)) != 0u;
            const bool mirrorT = (texParam & (1u << 19u)) != 0u;
            const u32 alpha5 = (draw.polyAttr >> 16u) & 0x1Fu;
            const u32 blendMode = (draw.polyAttr >> 4u) & 0x3u;
            const bool depthWriteDisabled = (draw.polyAttr & (1u << 11u)) == 0u;
            const bool matchesPaletteUiGate =
                (tri.flags & kTriangleFlagTextured) != 0u
                && (tri.flags & kTriangleFlagLinear) != 0u
                && textureFormat == 3u
                && color0Transparent
                && depthWriteDisabled
                && clearPlaneAlphaZero
                && alphaBlendEnabled
                && blendMode == 0u
                && alpha5 > 0u
                && alpha5 < 31u
                && !repeatS
                && !repeatT
                && !mirrorS
                && !mirrorT;
            if (!matchesPaletteUiGate)
                continue;

            if (paletteUiGateCandidates == 0u)
            {
                paletteUiGateFirstDraw = drawIndex;
                paletteUiGateFirstPolyId = (draw.polyAttr >> 24u) & 0x3Fu;
                paletteUiGateFirstAlpha5 = alpha5;
            }
            paletteUiGateCandidates++;
        }
        const bool paletteUiGateActive = paletteUiGateCandidates > 0u;
        if (PaletteUiGateLogCooldown == 0u || paletteUiGateActive != PaletteUiGateLastActive)
        {
            Log(
                LogLevel::Warn,
                "VulkanGraphics[PaletteUiGate]: candidates=%u firstDraw=%u firstPolyId=%u firstAlpha5=%u clearAlphaZero=%u alphaBlend=%u",
                paletteUiGateCandidates,
                paletteUiGateFirstDraw,
                paletteUiGateFirstPolyId,
                paletteUiGateFirstAlpha5,
                clearPlaneAlphaZero ? 1u : 0u,
                alphaBlendEnabled ? 1u : 0u);
            PaletteUiGateLogCooldown = paletteUiGateActive ? 60u : 180u;
            PaletteUiGateLastActive = paletteUiGateActive;
        }
        else
        {
            PaletteUiGateLogCooldown--;
        }
    }

    if (MelonDSAndroid::areRendererDebugToolsEnabled() && CaptureDebugLogsRemaining > 0u)
    {
        Log(
            LogLevel::Warn,
            "VulkanGraphics[Passes]: clearAlphaZero=%u clearPolyId=%u alphaBlend=%u opaque=%u edge=%u bgZeroShadowMask=%u bgZeroNeedOpaque=%u bgZeroShadowBlend=%u bgZeroTrans=%u bgZeroShadowSkipPolyId=%u mainShadowMask=%u mainNeedOpaque=%u mainShadowClear=%u mainShadowBlend=%u mainTrans=%u paletteUiOpaqueReplay=%u wBufferFragmentDepth=%u",
            clearPlaneAlphaZero ? 1u : 0u,
            clearPlanePolyId,
            alphaBlendEnabled ? 1u : 0u,
            graphicsPassDebugStats.opaque,
            graphicsPassDebugStats.edge,
            graphicsPassDebugStats.bgZeroShadowMask,
            graphicsPassDebugStats.bgZeroNeedOpaque,
            graphicsPassDebugStats.bgZeroShadowBlend,
            graphicsPassDebugStats.bgZeroTranslucent,
            graphicsPassDebugStats.bgZeroShadowSkippedPolyId,
            graphicsPassDebugStats.mainShadowMask,
            graphicsPassDebugStats.mainNeedOpaque,
            graphicsPassDebugStats.mainShadowClear,
            graphicsPassDebugStats.mainShadowBlend,
            graphicsPassDebugStats.mainTranslucent,
            graphicsPassDebugStats.paletteUiOpaqueReplay,
            graphicsPassDebugStats.wBufferFragmentDepth);
        CaptureDebugLogsRemaining--;
    }

    RasterCpuWindow.Add(PerfNowNs() - rasterCpuStartNs);
    TriangleCountWindow.Add(static_cast<u64>(Triangles.size()));
    PassCountWindow.Add(drawCount > 0u ? 1u : 0u);
    BinCpuWindow.Add(0);
    WorkOffsetsCpuWindow.Add(0);
    SortCpuWindow.Add(0);
    CpuActiveTileCountWindow.Add(0);
    CpuTileCountWindow.Add(0);
    CpuActiveGroupCountWindow.Add(0);
    CpuActiveDispatchWindow.Add(0);

    vkCmdEndRenderPass(commandBuffer);

    VkImageMemoryBarrier samplingBarriers[3]{};
    for (VkImageMemoryBarrier& barrier : samplingBarriers)
    {
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
    }
    samplingBarriers[0].image = ColorImage;
    samplingBarriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    samplingBarriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    samplingBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    samplingBarriers[1].image = AttrImage;
    samplingBarriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    samplingBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    samplingBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    samplingBarriers[2].image = DepthImage;
    samplingBarriers[2].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    samplingBarriers[2].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    samplingBarriers[2].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        3,
        samplingBarriers
    );

    const bool runEdgePass = (dispCnt & (1u << 5u)) != 0u;
    const bool runFogPass = (dispCnt & (1u << 7u)) != 0u;
    const u64 finalCpuStartNs = PerfNowNs();
    const u32 savedFinalVariantKey = pushConstants.variantKey;
    const u32 savedFinalTriangleBase = pushConstants.triangleBase;
    if (GraphicsHiddenAlphaZeroFinalEdgePolyIdOverride < 64u)
    {
        pushConstants.variantKey = 0x80000000u | GraphicsHiddenAlphaZeroFinalEdgePolyIdOverride;
        pushConstants.triangleBase = GraphicsHiddenAlphaZeroFinalEdgeColorOverride;
    }
    else
    {
        pushConstants.variantKey = 0u;
        pushConstants.triangleBase = 0u;
    }

    VkRenderPassBeginInfo finalBeginInfo{};
    finalBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    finalBeginInfo.renderPass = GraphicsFinalRenderPass;
    finalBeginInfo.framebuffer = GraphicsFinalFramebuffer;
    finalBeginInfo.renderArea.extent.width = ColorImageWidth;
    finalBeginInfo.renderArea.extent.height = ColorImageHeight;
    vkCmdBeginRenderPass(commandBuffer, &finalBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    if (runEdgePass && runFogPass && GraphicsFinalEdgeFogPipeline != VK_NULL_HANDLE)
    {
        bindGraphicsPipelineCached(GraphicsFinalEdgeFogPipeline);
        bindGraphicsDescriptorSetCached();
        vkCmdPushConstants(commandBuffer, GraphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
        vkCmdDraw(commandBuffer, 3u, 1u, 0u, 0u);
    }
    else if (runEdgePass && GraphicsFinalEdgePipeline != VK_NULL_HANDLE)
    {
        bindGraphicsPipelineCached(GraphicsFinalEdgePipeline);
        bindGraphicsDescriptorSetCached();
        vkCmdPushConstants(commandBuffer, GraphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
        const float edgeBlendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        vkCmdSetBlendConstants(commandBuffer, edgeBlendConstants);
        vkCmdDraw(commandBuffer, 3u, 1u, 0u, 0u);
    }

    if (!(runEdgePass && runFogPass && GraphicsFinalEdgeFogPipeline != VK_NULL_HANDLE)
        && runFogPass && GraphicsFinalFogPipeline != VK_NULL_HANDLE)
    {
        bindGraphicsPipelineCached(GraphicsFinalFogPipeline);
        bindGraphicsDescriptorSetCached();
        vkCmdPushConstants(commandBuffer, GraphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
        const float fogBlendConstants[4] = {
            static_cast<float>(fogColor & 0x1Fu) * (1.0f / 31.0f),
            static_cast<float>((fogColor >> 5u) & 0x1Fu) * (1.0f / 31.0f),
            static_cast<float>((fogColor >> 10u) & 0x1Fu) * (1.0f / 31.0f),
            static_cast<float>((fogColor >> 16u) & 0x1Fu) * (1.0f / 31.0f),
        };
        vkCmdSetBlendConstants(commandBuffer, fogBlendConstants);
        vkCmdDraw(commandBuffer, 3u, 1u, 0u, 0u);
    }

    pushConstants.variantKey = savedFinalVariantKey;
    pushConstants.triangleBase = savedFinalTriangleBase;
    vkCmdEndRenderPass(commandBuffer);
    FinalCpuWindow.Add(PerfNowNs() - finalCpuStartNs);

    if ((timestampQueryPool != VK_NULL_HANDLE) && timestampPending)
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, timestampQueryPool, 7);

    bool deferCaptureReadbackCompletion = false;
    if (captureReadbackPath || readbackToCpu)
    {
        VkImageMemoryBarrier colorToTransferSrcBarrier{};
        colorToTransferSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorToTransferSrcBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorToTransferSrcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        colorToTransferSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        colorToTransferSrcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        colorToTransferSrcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorToTransferSrcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorToTransferSrcBarrier.image = ColorImage;
        colorToTransferSrcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorToTransferSrcBarrier.subresourceRange.baseMipLevel = 0;
        colorToTransferSrcBarrier.subresourceRange.levelCount = 1;
        colorToTransferSrcBarrier.subresourceRange.baseArrayLayer = 0;
        colorToTransferSrcBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &colorToTransferSrcBarrier
        );

        if (captureReadbackPath)
        {
            if (CaptureLineExportPipeline == VK_NULL_HANDLE || !updateCaptureExportDescriptorSet(context))
                return false;

            VkImageMemoryBarrier colorToCaptureReadBarrier{};
            colorToCaptureReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            colorToCaptureReadBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            colorToCaptureReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            colorToCaptureReadBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            colorToCaptureReadBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            colorToCaptureReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            colorToCaptureReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            colorToCaptureReadBarrier.image = ColorImage;
            colorToCaptureReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            colorToCaptureReadBarrier.subresourceRange.baseMipLevel = 0;
            colorToCaptureReadBarrier.subresourceRange.levelCount = 1;
            colorToCaptureReadBarrier.subresourceRange.baseArrayLayer = 0;
            colorToCaptureReadBarrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &colorToCaptureReadBarrier
            );

            const VkDescriptorSet captureExportDescriptorSet = getDescriptorSet(context, FallbackTextureDescriptorIndex);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, CaptureLineExportPipeline);
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                PipelineLayout,
                0,
                1,
                &captureExportDescriptorSet,
                0,
                nullptr
            );
            vkCmdPushConstants(commandBuffer, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
            vkCmdDispatch(commandBuffer, 32u, 24u, 1u);

            VkBufferMemoryBarrier captureToHostBarrier{};
            captureToHostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            captureToHostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            captureToHostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            captureToHostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            captureToHostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            captureToHostBarrier.buffer = captureLineBuffer;
            captureToHostBarrier.offset = 0;
            captureToHostBarrier.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT,
                0,
                0,
                nullptr,
                1,
                &captureToHostBarrier,
                0,
                nullptr
            );
            CaptureLineExportCount++;
            CaptureLineExportCpuWindow.Add(0);
            deferCaptureReadbackCompletion = true;
        }

        if (readbackToCpu)
        {
            VkBufferImageCopy copyRegion{};
            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageExtent.width = readbackWidth;
            copyRegion.imageExtent.height = readbackHeight;
            copyRegion.imageExtent.depth = 1;

            if (useCaptureDownscaleForReadback)
            {
                VkImageMemoryBarrier captureToTransferDstBarrier{};
                captureToTransferDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                captureToTransferDstBarrier.srcAccessMask = CaptureReadbackImageInitialized ? VK_ACCESS_TRANSFER_READ_BIT : 0u;
                captureToTransferDstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                captureToTransferDstBarrier.oldLayout = CaptureReadbackImageLayout;
                captureToTransferDstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                captureToTransferDstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                captureToTransferDstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                captureToTransferDstBarrier.image = CaptureReadbackImage;
                captureToTransferDstBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                captureToTransferDstBarrier.subresourceRange.levelCount = 1;
                captureToTransferDstBarrier.subresourceRange.layerCount = 1;
                vkCmdPipelineBarrier(commandBuffer, CaptureReadbackImageInitialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &captureToTransferDstBarrier);

                VkImageBlit blitRegion{};
                blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blitRegion.srcSubresource.layerCount = 1;
                blitRegion.srcOffsets[1] = {static_cast<int32_t>(ColorImageWidth), static_cast<int32_t>(ColorImageHeight), 1};
                blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blitRegion.dstSubresource.layerCount = 1;
                blitRegion.dstOffsets[1] = {static_cast<int32_t>(readbackWidth), static_cast<int32_t>(readbackHeight), 1};
                vkCmdBlitImage(commandBuffer, ColorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, CaptureReadbackImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blitRegion, VK_FILTER_NEAREST);

                VkImageMemoryBarrier captureToTransferSrcBarrier{};
                captureToTransferSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                captureToTransferSrcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                captureToTransferSrcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                captureToTransferSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                captureToTransferSrcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                captureToTransferSrcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                captureToTransferSrcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                captureToTransferSrcBarrier.image = CaptureReadbackImage;
                captureToTransferSrcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                captureToTransferSrcBarrier.subresourceRange.levelCount = 1;
                captureToTransferSrcBarrier.subresourceRange.layerCount = 1;
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &captureToTransferSrcBarrier);
                vkCmdCopyImageToBuffer(commandBuffer, CaptureReadbackImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ReadbackBuffer, 1, &copyRegion);

                VkImageMemoryBarrier captureBackToGeneralBarrier{};
                captureBackToGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                captureBackToGeneralBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                captureBackToGeneralBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                captureBackToGeneralBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                captureBackToGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                captureBackToGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                captureBackToGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                captureBackToGeneralBarrier.image = CaptureReadbackImage;
                captureBackToGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                captureBackToGeneralBarrier.subresourceRange.levelCount = 1;
                captureBackToGeneralBarrier.subresourceRange.layerCount = 1;
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &captureBackToGeneralBarrier);
                CaptureReadbackImageInitialized = true;
                CaptureReadbackImageLayout = VK_IMAGE_LAYOUT_GENERAL;
            }
            else
            {
                vkCmdCopyImageToBuffer(commandBuffer, ColorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ReadbackBuffer, 1, &copyRegion);
            }

            VkBufferMemoryBarrier toHostBarrier{};
            toHostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            toHostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toHostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            toHostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toHostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toHostBarrier.buffer = ReadbackBuffer;
            toHostBarrier.offset = 0;
            toHostBarrier.size = ReadbackSize;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &toHostBarrier, 0, nullptr);
        }

        VkImageMemoryBarrier colorBackToGeneralBarrier{};
        colorBackToGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorBackToGeneralBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        colorBackToGeneralBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorBackToGeneralBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        colorBackToGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        colorBackToGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorBackToGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorBackToGeneralBarrier.image = ColorImage;
        colorBackToGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorBackToGeneralBarrier.subresourceRange.levelCount = 1;
        colorBackToGeneralBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &colorBackToGeneralBarrier);
    }

    if ((timestampQueryPool != VK_NULL_HANDLE) && timestampPending)
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampQueryPool, 8);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        return false;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    u64 completionValue = 0;
    VkTimelineSemaphoreSubmitInfo completionInfo{};
    if (CompletionTimelineSemaphore != VK_NULL_HANDLE)
    {
        completionValue = ++NextCompletionTimelineValue;
        completionInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        completionInfo.signalSemaphoreValueCount = 1;
        completionInfo.pSignalSemaphoreValues = &completionValue;
        submitInfo.pNext = &completionInfo;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &CompletionTimelineSemaphore;
    }

    {
        std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
        const VkResult submitResult = vkQueueSubmit(Queue, 1, &submitInfo, frameFence);
        if (submitResult != VK_SUCCESS)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: graphics vkQueueSubmit failed (%d)", static_cast<int>(submitResult));
            return false;
        }
    }

    if (context != nullptr && Threaded)
    {
        LastSubmittedRenderContext = context;
        context->SubmittedFrameSerial = Structured2DPendingSerial != 0
            ? Structured2DPendingSerial
            : Structured2DFrameSerial;
        context->SubmittedGeneration = GPU3D.GetCurrentRendererGeneration();
        context->CompletionValue = completionValue;
    }
    LastCompletionTimelineValue = completionValue;

    if (timestampQueryPool != VK_NULL_HANDLE)
    {
        if (context != nullptr)
            context->TimestampPending = true;
        else
            TimestampPending = true;
    }

    if (captureReadbackPath)
    {
        PendingCaptureLineContext = context;
        CaptureLinePending = true;
        CaptureLineDataIsRgba8 = false;
        PendingCaptureLineBufferSlot = -1;
        PendingCaptureLineScreenSwap = CurrentRenderScreenSwap;
        CaptureLineReady = false;
        ReadyCaptureLineBufferSlot = -1;
        ReadyCaptureLineData = nullptr;
        ReadyCaptureLineScreenSwap = false;
        ActiveCapturePathMode = CapturePathMode::CaptureLineExport;
        CapturePathModeCounts[static_cast<size_t>(CapturePathMode::CaptureLineExport)]++;
    }
    else
    {
        resetCaptureLineState();
    }

    if (readbackToCpu)
    {
        CaptureReadbackPending = true;
        PendingCaptureReadbackContext = context;
        RawReadbackWidth = readbackWidth;
        RawReadbackHeight = readbackHeight;
    }
    else
    {
        CaptureReadbackPending = false;
        PendingCaptureReadbackContext = nullptr;
    }

    ColorImageInitialized = true;
    ColorImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    AttrImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    DepthImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    DepthStencilImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (useCaptureDownscaleForReadback)
        CaptureReadbackImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    HasCpuFrame = readbackToCpu && !deferCaptureReadbackCompletion;
    return true;
}

bool VulkanRenderer3D::submitGraphicsCaptureExportForCurrentFrame()
{
    constexpr u32 kCaptureReadbackWidth = 256u;
    constexpr u32 kCaptureReadbackHeight = 192u;

    if (ActiveBackendMode != BackendMode::GraphicsHardware
        || !ensureInitialized()
        || Device == VK_NULL_HANDLE
        || Queue == VK_NULL_HANDLE
        || CommandBuffer == VK_NULL_HANDLE
        || FrameFence == VK_NULL_HANDLE
        || ColorImage == VK_NULL_HANDLE
        || !ColorImageInitialized)
    {
        return false;
    }

    if (!ensureCaptureLineBuffer(nullptr) || !ensureCaptureReadbackImage())
        return false;

    if (CaptureLineMapped == nullptr)
        return false;

    const VkResult fenceStatus = vkGetFenceStatus(Device, FrameFence);
    if (fenceStatus == VK_NOT_READY)
        return false;
    if (fenceStatus != VK_SUCCESS)
        return false;

    consumeGpuTiming(nullptr);

    if (vkResetFences(Device, 1, &FrameFence) != VK_SUCCESS)
        return false;
    if (vkResetCommandBuffer(CommandBuffer, 0) != VK_SUCCESS)
        return false;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(CommandBuffer, &beginInfo) != VK_SUCCESS)
        return false;

    VkImageMemoryBarrier colorToTransferSrcBarrier{};
    colorToTransferSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    colorToTransferSrcBarrier.srcAccessMask =
        VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_SHADER_WRITE_BIT |
        VK_ACCESS_TRANSFER_READ_BIT |
        VK_ACCESS_TRANSFER_WRITE_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    colorToTransferSrcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    colorToTransferSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    colorToTransferSrcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    colorToTransferSrcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorToTransferSrcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorToTransferSrcBarrier.image = ColorImage;
    colorToTransferSrcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorToTransferSrcBarrier.subresourceRange.levelCount = 1;
    colorToTransferSrcBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &colorToTransferSrcBarrier
    );

    VkImageMemoryBarrier captureToTransferDstBarrier{};
    captureToTransferDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    captureToTransferDstBarrier.srcAccessMask = CaptureReadbackImageInitialized ? VK_ACCESS_TRANSFER_READ_BIT : 0u;
    captureToTransferDstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    captureToTransferDstBarrier.oldLayout = CaptureReadbackImageLayout;
    captureToTransferDstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    captureToTransferDstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    captureToTransferDstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    captureToTransferDstBarrier.image = CaptureReadbackImage;
    captureToTransferDstBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    captureToTransferDstBarrier.subresourceRange.levelCount = 1;
    captureToTransferDstBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        CommandBuffer,
        CaptureReadbackImageInitialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &captureToTransferDstBarrier
    );

    VkImageBlit blitRegion{};
    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcOffsets[1] = {static_cast<int32_t>(ColorImageWidth), static_cast<int32_t>(ColorImageHeight), 1};
    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstOffsets[1] = {static_cast<int32_t>(kCaptureReadbackWidth), static_cast<int32_t>(kCaptureReadbackHeight), 1};
    vkCmdBlitImage(
        CommandBuffer,
        ColorImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        CaptureReadbackImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &blitRegion,
        VK_FILTER_NEAREST
    );

    VkImageMemoryBarrier captureToTransferSrcBarrier{};
    captureToTransferSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    captureToTransferSrcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    captureToTransferSrcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    captureToTransferSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    captureToTransferSrcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    captureToTransferSrcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    captureToTransferSrcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    captureToTransferSrcBarrier.image = CaptureReadbackImage;
    captureToTransferSrcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    captureToTransferSrcBarrier.subresourceRange.levelCount = 1;
    captureToTransferSrcBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &captureToTransferSrcBarrier
    );

    VkBufferImageCopy captureCopyRegion{};
    captureCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    captureCopyRegion.imageSubresource.layerCount = 1;
    captureCopyRegion.imageExtent.width = kCaptureReadbackWidth;
    captureCopyRegion.imageExtent.height = kCaptureReadbackHeight;
    captureCopyRegion.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(
        CommandBuffer,
        CaptureReadbackImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        CaptureLineBuffer,
        1,
        &captureCopyRegion
    );

    VkBufferMemoryBarrier captureToHostBarrier{};
    captureToHostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    captureToHostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    captureToHostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    captureToHostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    captureToHostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    captureToHostBarrier.buffer = CaptureLineBuffer;
    captureToHostBarrier.offset = 0;
    captureToHostBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        0,
        nullptr,
        1,
        &captureToHostBarrier,
        0,
        nullptr
    );

    VkImageMemoryBarrier captureBackToGeneralBarrier{};
    captureBackToGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    captureBackToGeneralBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    captureBackToGeneralBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    captureBackToGeneralBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    captureBackToGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    captureBackToGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    captureBackToGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    captureBackToGeneralBarrier.image = CaptureReadbackImage;
    captureBackToGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    captureBackToGeneralBarrier.subresourceRange.levelCount = 1;
    captureBackToGeneralBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &captureBackToGeneralBarrier
    );

    VkImageMemoryBarrier colorBackToGeneralBarrier{};
    colorBackToGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    colorBackToGeneralBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    colorBackToGeneralBarrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_SHADER_WRITE_BIT |
        VK_ACCESS_TRANSFER_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    colorBackToGeneralBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    colorBackToGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    colorBackToGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBackToGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBackToGeneralBarrier.image = ColorImage;
    colorBackToGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorBackToGeneralBarrier.subresourceRange.levelCount = 1;
    colorBackToGeneralBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &colorBackToGeneralBarrier
    );

    if (vkEndCommandBuffer(CommandBuffer) != VK_SUCCESS)
        return false;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &CommandBuffer;
    {
        std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
        const VkResult submitResult = vkQueueSubmit(Queue, 1, &submitInfo, FrameFence);
        if (submitResult != VK_SUCCESS)
            return false;
    }

    CaptureReadbackImageInitialized = true;
    CaptureReadbackImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    CaptureLineExportCount++;
    CaptureLineExportCpuWindow.Add(0);
    PendingCaptureLineContext = nullptr;
    CaptureLinePending = true;
    CaptureLineDataIsRgba8 = true;
    PendingCaptureLineBufferSlot = static_cast<int>(ActiveCaptureLineBufferSlot);
    PendingCaptureLineScreenSwap = CurrentRenderScreenSwap;
    CaptureLineReady = false;
    ReadyCaptureLineBufferSlot = -1;
    ReadyCaptureLineData = nullptr;
    ReadyCaptureLineScreenSwap = false;
    ActiveCapturePathMode = CapturePathMode::CaptureLineExport;
    CapturePathModeCounts[static_cast<size_t>(CapturePathMode::CaptureLineExport)]++;
    return true;
}

bool VulkanRenderer3D::readbackGraphicsAttrImageToCpu(std::vector<u32>& outAttrPixels)
{
    if (!ensureInitialized() || AttrImage == VK_NULL_HANDLE || ColorImageWidth == 0 || ColorImageHeight == 0)
        return false;
    if (!waitForReadbackSource())
        return false;

    const VkDeviceSize requiredReadbackSize = static_cast<VkDeviceSize>(ColorImageWidth) * static_cast<VkDeviceSize>(ColorImageHeight) * sizeof(u32);
    if (ReadbackBuffer == VK_NULL_HANDLE || ReadbackMemory == VK_NULL_HANDLE || ReadbackSize != requiredReadbackSize)
    {
        destroyReadbackBuffer();
        if (!createReadbackBuffer(ColorImageWidth, ColorImageHeight))
            return false;
    }

    if (vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return false;
    if (vkResetFences(Device, 1, &FrameFence) != VK_SUCCESS)
        return false;
    if (vkResetCommandBuffer(CommandBuffer, 0) != VK_SUCCESS)
        return false;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(CommandBuffer, &beginInfo) != VK_SUCCESS)
        return false;

    TransitionImage(
        CommandBuffer,
        AttrImage,
        AttrImageLayout,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT);

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent.width = ColorImageWidth;
    copyRegion.imageExtent.height = ColorImageHeight;
    copyRegion.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(CommandBuffer, AttrImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ReadbackBuffer, 1, &copyRegion);

    VkBufferMemoryBarrier toHostBarrier{};
    toHostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    toHostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toHostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    toHostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHostBarrier.buffer = ReadbackBuffer;
    toHostBarrier.size = ReadbackSize;
    vkCmdPipelineBarrier(CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &toHostBarrier, 0, nullptr);

    TransitionImage(
        CommandBuffer,
        AttrImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        AttrImageLayout,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT);

    if (vkEndCommandBuffer(CommandBuffer) != VK_SUCCESS)
        return false;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &CommandBuffer;
    {
        std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
        if (vkQueueSubmit(Queue, 1, &submitInfo, FrameFence) != VK_SUCCESS)
            return false;
    }
    if (vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return false;
    if (ReadbackMapped == nullptr)
        return false;

    const size_t pixelCount = static_cast<size_t>(ColorImageWidth) * static_cast<size_t>(ColorImageHeight);
    std::vector<u32> rawPixels(pixelCount);
    std::memcpy(rawPixels.data(), ReadbackMapped, pixelCount * sizeof(u32));
    outAttrPixels.resize(pixelCount);
    for (size_t i = 0; i < pixelCount; i++)
        outAttrPixels[i] = PackOpenGlAttrToLogical(rawPixels[i]);
    return true;
}

bool VulkanRenderer3D::readbackGraphicsDepthImageToCpu(std::vector<u32>& outDepthPixels)
{
    if (!ensureInitialized() || DepthImage == VK_NULL_HANDLE || ColorImageWidth == 0 || ColorImageHeight == 0)
        return false;
    if (!waitForReadbackSource())
        return false;

    const VkDeviceSize requiredReadbackSize = static_cast<VkDeviceSize>(ColorImageWidth) * static_cast<VkDeviceSize>(ColorImageHeight) * sizeof(float);
    if (ReadbackBuffer == VK_NULL_HANDLE || ReadbackMemory == VK_NULL_HANDLE || ReadbackSize != requiredReadbackSize)
    {
        destroyReadbackBuffer();
        if (!createReadbackBuffer(ColorImageWidth, ColorImageHeight))
            return false;
    }

    if (vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return false;
    if (vkResetFences(Device, 1, &FrameFence) != VK_SUCCESS)
        return false;
    if (vkResetCommandBuffer(CommandBuffer, 0) != VK_SUCCESS)
        return false;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(CommandBuffer, &beginInfo) != VK_SUCCESS)
        return false;

    TransitionImage(
        CommandBuffer,
        DepthImage,
        DepthImageLayout,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT);

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent.width = ColorImageWidth;
    copyRegion.imageExtent.height = ColorImageHeight;
    copyRegion.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(CommandBuffer, DepthImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ReadbackBuffer, 1, &copyRegion);

    VkBufferMemoryBarrier toHostBarrier{};
    toHostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    toHostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toHostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    toHostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHostBarrier.buffer = ReadbackBuffer;
    toHostBarrier.size = ReadbackSize;
    vkCmdPipelineBarrier(CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &toHostBarrier, 0, nullptr);

    TransitionImage(
        CommandBuffer,
        DepthImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        DepthImageLayout,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT);

    if (vkEndCommandBuffer(CommandBuffer) != VK_SUCCESS)
        return false;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &CommandBuffer;
    {
        std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
        if (vkQueueSubmit(Queue, 1, &submitInfo, FrameFence) != VK_SUCCESS)
            return false;
    }
    if (vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return false;
    if (ReadbackMapped == nullptr)
        return false;

    const size_t pixelCount = static_cast<size_t>(ColorImageWidth) * static_cast<size_t>(ColorImageHeight);
    outDepthPixels.resize(pixelCount);
    const auto* depthValues = reinterpret_cast<const float*>(ReadbackMapped);
    for (size_t i = 0; i < pixelCount; i++)
    {
        const float depth = std::clamp(depthValues[i], 0.0f, 1.0f);
        outDepthPixels[i] = static_cast<u32>(std::lround(depth * 16777215.0f));
    }
    return true;
}

bool VulkanRenderer3D::readbackColorTargetToCpu(bool capturePath)
{
    ReadbackColorRequestCount++;

    if (!ensureInitialized() || ColorImage == VK_NULL_HANDLE || ColorImageWidth == 0 || ColorImageHeight == 0)
        return false;

    if (capturePath && !waitForReadbackSource())
        return false;

    constexpr u32 kCaptureReadbackWidth = 256u;
    constexpr u32 kCaptureReadbackHeight = 192u;
    const bool canUseCaptureDownscale = capturePath
        && (ColorImageWidth != kCaptureReadbackWidth || ColorImageHeight != kCaptureReadbackHeight);
    const u32 readbackWidth = canUseCaptureDownscale ? kCaptureReadbackWidth : ColorImageWidth;
    const u32 readbackHeight = canUseCaptureDownscale ? kCaptureReadbackHeight : ColorImageHeight;
    const VkDeviceSize requiredReadbackSize = static_cast<VkDeviceSize>(readbackWidth) * static_cast<VkDeviceSize>(readbackHeight) * sizeof(u32);

    if (canUseCaptureDownscale && !ensureCaptureReadbackImage())
        return false;

    if (ReadbackBuffer == VK_NULL_HANDLE || ReadbackMemory == VK_NULL_HANDLE || ReadbackSize != requiredReadbackSize)
    {
        destroyReadbackBuffer();
        if (!createReadbackBuffer(readbackWidth, readbackHeight))
            return false;
    }

    if (vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return false;

    if (vkResetFences(Device, 1, &FrameFence) != VK_SUCCESS)
        return false;

    if (vkResetCommandBuffer(CommandBuffer, 0) != VK_SUCCESS)
        return false;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(CommandBuffer, &beginInfo) != VK_SUCCESS)
        return false;

    VkImageMemoryBarrier colorToTransferSrcBarrier{};
    colorToTransferSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    colorToTransferSrcBarrier.srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_SHADER_WRITE_BIT |
        VK_ACCESS_TRANSFER_WRITE_BIT |
        VK_ACCESS_TRANSFER_READ_BIT;
    colorToTransferSrcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    colorToTransferSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    colorToTransferSrcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    colorToTransferSrcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorToTransferSrcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorToTransferSrcBarrier.image = ColorImage;
    colorToTransferSrcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorToTransferSrcBarrier.subresourceRange.baseMipLevel = 0;
    colorToTransferSrcBarrier.subresourceRange.levelCount = 1;
    colorToTransferSrcBarrier.subresourceRange.baseArrayLayer = 0;
    colorToTransferSrcBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &colorToTransferSrcBarrier
    );

    if (canUseCaptureDownscale)
    {
        VkImageMemoryBarrier captureToTransferDstBarrier{};
        captureToTransferDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        captureToTransferDstBarrier.srcAccessMask = CaptureReadbackImageInitialized ? VK_ACCESS_TRANSFER_READ_BIT : 0u;
        captureToTransferDstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        captureToTransferDstBarrier.oldLayout = CaptureReadbackImageLayout;
        captureToTransferDstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        captureToTransferDstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        captureToTransferDstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        captureToTransferDstBarrier.image = CaptureReadbackImage;
        captureToTransferDstBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        captureToTransferDstBarrier.subresourceRange.baseMipLevel = 0;
        captureToTransferDstBarrier.subresourceRange.levelCount = 1;
        captureToTransferDstBarrier.subresourceRange.baseArrayLayer = 0;
        captureToTransferDstBarrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            CommandBuffer,
            CaptureReadbackImageInitialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &captureToTransferDstBarrier
        );

        VkImageBlit blitRegion{};
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.mipLevel = 0;
        blitRegion.srcSubresource.baseArrayLayer = 0;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {static_cast<int32_t>(ColorImageWidth), static_cast<int32_t>(ColorImageHeight), 1};
        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.mipLevel = 0;
        blitRegion.dstSubresource.baseArrayLayer = 0;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.dstOffsets[0] = {0, 0, 0};
        blitRegion.dstOffsets[1] = {static_cast<int32_t>(readbackWidth), static_cast<int32_t>(readbackHeight), 1};
        vkCmdBlitImage(
            CommandBuffer,
            ColorImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            CaptureReadbackImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blitRegion,
            VK_FILTER_NEAREST
        );

        VkImageMemoryBarrier captureToTransferSrcBarrier{};
        captureToTransferSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        captureToTransferSrcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        captureToTransferSrcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        captureToTransferSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        captureToTransferSrcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        captureToTransferSrcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        captureToTransferSrcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        captureToTransferSrcBarrier.image = CaptureReadbackImage;
        captureToTransferSrcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        captureToTransferSrcBarrier.subresourceRange.baseMipLevel = 0;
        captureToTransferSrcBarrier.subresourceRange.levelCount = 1;
        captureToTransferSrcBarrier.subresourceRange.baseArrayLayer = 0;
        captureToTransferSrcBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &captureToTransferSrcBarrier
        );

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent.width = readbackWidth;
        copyRegion.imageExtent.height = readbackHeight;
        copyRegion.imageExtent.depth = 1;
        vkCmdCopyImageToBuffer(
            CommandBuffer,
            CaptureReadbackImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            ReadbackBuffer,
            1,
            &copyRegion
        );
    }
    else
    {
        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent.width = ColorImageWidth;
        copyRegion.imageExtent.height = ColorImageHeight;
        copyRegion.imageExtent.depth = 1;
        vkCmdCopyImageToBuffer(
            CommandBuffer,
            ColorImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            ReadbackBuffer,
            1,
            &copyRegion
        );
    }

    VkBufferMemoryBarrier toHostBarrier{};
    toHostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    toHostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toHostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    toHostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHostBarrier.buffer = ReadbackBuffer;
    toHostBarrier.offset = 0;
    toHostBarrier.size = ReadbackSize;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        0,
        nullptr,
        1,
        &toHostBarrier,
        0,
        nullptr
    );

    if (canUseCaptureDownscale)
    {
        VkImageMemoryBarrier captureBackToGeneralBarrier{};
        captureBackToGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        captureBackToGeneralBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        captureBackToGeneralBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        captureBackToGeneralBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        captureBackToGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        captureBackToGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        captureBackToGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        captureBackToGeneralBarrier.image = CaptureReadbackImage;
        captureBackToGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        captureBackToGeneralBarrier.subresourceRange.baseMipLevel = 0;
        captureBackToGeneralBarrier.subresourceRange.levelCount = 1;
        captureBackToGeneralBarrier.subresourceRange.baseArrayLayer = 0;
        captureBackToGeneralBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &captureBackToGeneralBarrier
        );
    }

    VkImageMemoryBarrier colorBackToGeneralBarrier{};
    colorBackToGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    colorBackToGeneralBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    colorBackToGeneralBarrier.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_SHADER_WRITE_BIT |
        VK_ACCESS_TRANSFER_READ_BIT;
    colorBackToGeneralBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    colorBackToGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    colorBackToGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBackToGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBackToGeneralBarrier.image = ColorImage;
    colorBackToGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorBackToGeneralBarrier.subresourceRange.baseMipLevel = 0;
    colorBackToGeneralBarrier.subresourceRange.levelCount = 1;
    colorBackToGeneralBarrier.subresourceRange.baseArrayLayer = 0;
    colorBackToGeneralBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &colorBackToGeneralBarrier
    );

    if (vkEndCommandBuffer(CommandBuffer) != VK_SUCCESS)
        return false;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &CommandBuffer;
    {
        std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
        const VkResult submitResult = vkQueueSubmit(Queue, 1, &submitInfo, FrameFence);
        if (submitResult != VK_SUCCESS)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: readback vkQueueSubmit failed (%d)", static_cast<int>(submitResult));
            return false;
        }
    }

    if (vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return false;

    if (ReadbackMapped == nullptr)
        return false;

    const size_t pixelCount = static_cast<size_t>(readbackWidth) * static_cast<size_t>(readbackHeight);
    if (RawReadbackRgba.size() != pixelCount)
        RawReadbackRgba.resize(pixelCount);
    std::memcpy(RawReadbackRgba.data(), ReadbackMapped, pixelCount * sizeof(u32));

    HasCpuFrame = true;
    ColorImageInitialized = true;
    ColorImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    if (canUseCaptureDownscale)
    {
        CaptureReadbackImageInitialized = true;
        CaptureReadbackImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    if (capturePath)
    {
        ActiveCapturePathMode = CapturePathMode::FallbackReadback;
        CapturePathModeCounts[static_cast<size_t>(CapturePathMode::FallbackReadback)]++;
    }
    return true;
}

bool VulkanRenderer3D::readbackResultBufferToCpu()
{
    ReadbackResultRequestCount++;

    if (!ensureInitialized() || ResultBuffer == VK_NULL_HANDLE || ResultBufferSize == 0)
        return false;

    if (!waitForReadbackSource())
        return false;

    if (ResultReadbackBuffer == VK_NULL_HANDLE || ResultReadbackMemory == VK_NULL_HANDLE || ResultReadbackSize != ResultBufferSize)
    {
        destroyResultReadbackBuffer();
        if (!createResultReadbackBuffer())
            return false;
    }

    if (vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return false;

    if (vkResetFences(Device, 1, &FrameFence) != VK_SUCCESS)
        return false;

    if (vkResetCommandBuffer(CommandBuffer, 0) != VK_SUCCESS)
        return false;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(CommandBuffer, &beginInfo) != VK_SUCCESS)
        return false;

    VkBufferMemoryBarrier toTransferBarrier{};
    toTransferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    toTransferBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    toTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferBarrier.buffer = ResultBuffer;
    toTransferBarrier.offset = 0;
    toTransferBarrier.size = ResultBufferSize;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        1,
        &toTransferBarrier,
        0,
        nullptr
    );

    VkBufferCopy copyRegion{};
    copyRegion.size = ResultBufferSize;
    vkCmdCopyBuffer(CommandBuffer, ResultBuffer, ResultReadbackBuffer, 1, &copyRegion);

    VkBufferMemoryBarrier toHostBarrier{};
    toHostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    toHostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toHostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    toHostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHostBarrier.buffer = ResultReadbackBuffer;
    toHostBarrier.offset = 0;
    toHostBarrier.size = ResultReadbackSize;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        0,
        nullptr,
        1,
        &toHostBarrier,
        0,
        nullptr
    );

    VkBufferMemoryBarrier backToComputeBarrier{};
    backToComputeBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    backToComputeBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    backToComputeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    backToComputeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    backToComputeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    backToComputeBarrier.buffer = ResultBuffer;
    backToComputeBarrier.offset = 0;
    backToComputeBarrier.size = ResultBufferSize;
    vkCmdPipelineBarrier(
        CommandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        1,
        &backToComputeBarrier,
        0,
        nullptr
    );

    if (vkEndCommandBuffer(CommandBuffer) != VK_SUCCESS)
        return false;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &CommandBuffer;
    {
        std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
        const VkResult submitResult = vkQueueSubmit(Queue, 1, &submitInfo, FrameFence);
        if (submitResult != VK_SUCCESS)
        {
            Log(LogLevel::Error, "VulkanRenderer3D: result readback vkQueueSubmit failed (%d)", static_cast<int>(submitResult));
            return false;
        }
    }

    if (vkWaitForFences(Device, 1, &FrameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return false;

    if (ResultReadbackMapped == nullptr)
        return false;

    const size_t wordCount = static_cast<size_t>(ResultReadbackSize / sizeof(u32));
    if (RawResultReadback.size() != wordCount)
        RawResultReadback.resize(wordCount);
    std::memcpy(RawResultReadback.data(), ResultReadbackMapped, ResultReadbackSize);
    return true;
}

void VulkanRenderer3D::buildGraphicsTriangleList(melonDS::GPU& gpu)
{
    struct TextureFrameData
    {
        u32 DescriptorIndex = 0;
    };

    struct TextureLookupKey
    {
        TexcacheVulkanLoader::TextureHandle Handle = 0;

        bool operator==(const TextureLookupKey& other) const noexcept
        {
            return Handle == other.Handle;
        }
    };

    struct TextureLookupHasher
    {
        size_t operator()(const TextureLookupKey& key) const noexcept
        {
            return std::hash<u64>{}(key.Handle);
        }
    };

    const u32 scaleFactor = static_cast<u32>(std::max(1, ScaleFactor));
    const u32 targetHeight = 192u * scaleFactor;
    const float scale = static_cast<float>(scaleFactor);
    const float maxTargetX = 256.0f * scale;
    const float maxTargetY = 192.0f * scale;
    const bool textureMapsEnabled = (gpu.GPU3D.RenderDispCnt & (1u << 0u)) != 0u;
    const bool highlightEnabled = (gpu.GPU3D.RenderDispCnt & (1u << 1u)) != 0u;
    const bool disablePassiveRepeatCoverageExpand =
        (MelonDSAndroid::getVulkanDiagnosticFlags() & kVulkanDiagnosticDisablePassiveRepeatCoverageExpand) != 0u;
    const float coverageDepthBias = CoverageFixDepthBias * 16777215.0f;

    AcceleratedSceneBuildConfig sceneBuildConfig{};
    sceneBuildConfig.Scale = ScaleFactor;
    sceneBuildConfig.BetterPolygons = BetterPolygons;
    sceneBuildConfig.UseHiresCoordinates = true;
    sceneBuildConfig.MaxFixedX = static_cast<s32>((256u * scaleFactor * 16u) - 1u);
    sceneBuildConfig.MaxFixedY = static_cast<s32>((192u * scaleFactor * 16u) - 1u);
    sceneBuildConfig.CoverageFix.Enabled = CoverageFixEnabled;
    sceneBuildConfig.CoverageFix.UserPx = CoverageFixPx;
    sceneBuildConfig.CoverageFix.ApplyRepeat = CoverageFixApplyRepeat;
    sceneBuildConfig.CoverageFix.ApplyClamp = CoverageFixApplyClamp;
    sceneBuildConfig.CoverageFix.PassiveRepeatPx = PassiveCoverageFixRepeatPx;
    sceneBuildConfig.CoverageFix.DisablePassiveRepeat = disablePassiveRepeatCoverageExpand;
    sceneBuildConfig.CoverageFix.PaletteUiClampEnabled = false;
    sceneBuildConfig.CoverageFix.PaletteUiClampPx = 0.5f;

    const u64 sceneBuildCpuStartNs = PerfNowNs();
    BuildAcceleratedScene(gpu.GPU3D, sceneBuildConfig, SharedGraphicsScene);
    GraphicsSceneBuildCpuWindow.Add(PerfNowNs() - sceneBuildCpuStartNs);

    const size_t estimatedTriangleCount =
        SharedGraphicsScene.Triangles.size() + (SharedGraphicsScene.Draws.size() * 2u);
    Triangles.reserve(std::max(Triangles.capacity(), estimatedTriangleCount));
    GraphicsVertices.reserve(std::max(GraphicsVertices.capacity(), estimatedTriangleCount * 3u));
    GraphicsSceneVertices.resize(SharedGraphicsScene.Vertices.size());
    GraphicsPolygons.reserve(std::max(GraphicsPolygons.capacity(), SharedGraphicsScene.Draws.size()));
    GraphicsOpaqueDrawIndices.reserve(std::max(GraphicsOpaqueDrawIndices.capacity(), SharedGraphicsScene.Draws.size()));
    GraphicsNeedOpaqueDrawIndices.reserve(std::max(GraphicsNeedOpaqueDrawIndices.capacity(), SharedGraphicsScene.Draws.size()));
    GraphicsAlphaDrawIndices.reserve(std::max(GraphicsAlphaDrawIndices.capacity(), SharedGraphicsScene.Draws.size()));
    GraphicsShadowMaskDrawIndices.reserve(std::max(GraphicsShadowMaskDrawIndices.capacity(), SharedGraphicsScene.Draws.size()));
    GraphicsShadowDrawIndices.reserve(std::max(GraphicsShadowDrawIndices.capacity(), SharedGraphicsScene.Draws.size()));
    GraphicsHiddenAlphaZeroFinalEdgePolyIdOverride = 0xFFFFFFFFu;
    GraphicsHiddenAlphaZeroFinalEdgeColorOverride = 0u;

    std::unordered_map<TextureLookupKey, TextureFrameData, TextureLookupHasher> textureLookup{};
    textureLookup.reserve(SharedGraphicsScene.Draws.size());

    const auto to8From6 = [](u32 c6) -> u32 {
        c6 &= 0x3Fu;
        return (c6 << 2u) | (c6 >> 4u);
    };

    const auto to8From5 = [](u32 c5) -> u32 {
        c5 &= 0x1Fu;
        return (c5 << 3u) | (c5 >> 2u);
    };

    const auto packRgba8 = [](u32 r, u32 g, u32 b, u32 a) -> u32 {
        return (r & 0xFFu) | ((g & 0xFFu) << 8u) | ((b & 0xFFu) << 16u) | ((a & 0xFFu) << 24u);
    };

    struct TriangleVertexData
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
        float u = 0.0f;
        float v = 0.0f;
        u32 colorRgba8 = 0;
        u32 wRaw = 1u;
    };

    constexpr u32 kTriangleFlagTranslucent = 1u << 0u;
    constexpr u32 kTriangleFlagTextured = 1u << 1u;
    constexpr u32 kTriangleFlagDecal = 1u << 2u;
    constexpr u32 kTriangleFlagCoverageFix = 1u << 3u;
    constexpr u32 kTriangleFlagWBuffer = 1u << 4u;
    constexpr u32 kTriangleFlagShadowMask = 1u << 5u;
    constexpr u32 kTriangleFlagLinear = 1u << 6u;
    constexpr u32 kTriangleFlagBoundaryEdge0 = 1u << 7u;
    constexpr u32 kTriangleFlagBoundaryEdge1 = 1u << 8u;
    constexpr u32 kTriangleFlagBoundaryEdge2 = 1u << 9u;
    constexpr u32 kTriangleFlagFrontFacing = 1u << 10u;
    constexpr u32 kTriangleFlagTopLeftEdge0 = 1u << 11u;
    constexpr u32 kTriangleFlagTopLeftEdge1 = 1u << 12u;
    constexpr u32 kTriangleFlagTopLeftEdge2 = 1u << 13u;
    constexpr u32 kTriangleFlagTextureOpaque = 1u << 14u;
    constexpr u32 kVariantFlagTextured = 1u << 0u;
    constexpr u32 kVariantFlagDecal = 1u << 1u;
    constexpr u32 kVariantFlagModulate = 1u << 2u;
    constexpr u32 kVariantFlagToon = 1u << 3u;
    constexpr u32 kVariantFlagHighlight = 1u << 4u;
    constexpr u32 kVariantFlagShadowMask = 1u << 5u;
    constexpr u32 kVariantFlagWBuffer = 1u << 6u;
    constexpr u32 kVariantFlagTranslucent = 1u << 7u;
    constexpr u32 kVariantFlagCoverageFix = 1u << 8u;

    const auto packYBounds = [&](const float* yValues, size_t yValueCount) -> std::optional<u32> {
        u32 polygonYTop = targetHeight;
        u32 polygonYBot = 0u;
        bool hasPolygonYBounds = false;
        for (size_t yIndex = 0; yIndex < yValueCount; yIndex++)
        {
            const float clampedY = std::clamp(yValues[yIndex], 0.0f, static_cast<float>(targetHeight));
            const u32 yTopLine = static_cast<u32>(std::floor(clampedY));
            const u32 yBottomLine = std::min<u32>(targetHeight, static_cast<u32>(std::ceil(clampedY)));
            polygonYTop = std::min(polygonYTop, yTopLine);
            polygonYBot = std::max(polygonYBot, yBottomLine);
            hasPolygonYBounds = true;
        }

        if (!hasPolygonYBounds)
            return std::nullopt;

        if (polygonYBot <= polygonYTop)
            polygonYBot = std::min<u32>(targetHeight, polygonYTop + 1u);

        return (polygonYTop & 0xFFFFu) | ((polygonYBot & 0xFFFFu) << 16u);
    };

    const auto packSceneDrawYBounds = [&](const AcceleratedSceneDraw& sceneDraw) -> std::optional<u32> {
        u32 polygonYTop = targetHeight;
        u32 polygonYBot = 0u;
        bool hasPolygonYBounds = false;
        for (u32 vertexOffset = 0; vertexOffset < sceneDraw.VertexCount; vertexOffset++)
        {
            const u32 sceneVertexIndex = sceneDraw.FirstVertex + vertexOffset;
            if (sceneVertexIndex >= SharedGraphicsScene.Vertices.size())
                break;

            const float clampedY = std::clamp(SharedGraphicsScene.Vertices[sceneVertexIndex].Y, 0.0f, static_cast<float>(targetHeight));
            const u32 yTopLine = static_cast<u32>(std::floor(clampedY));
            const u32 yBottomLine = std::min<u32>(targetHeight, static_cast<u32>(std::ceil(clampedY)));
            polygonYTop = std::min(polygonYTop, yTopLine);
            polygonYBot = std::max(polygonYBot, yBottomLine);
            hasPolygonYBounds = true;
        }

        if (!hasPolygonYBounds)
            return std::nullopt;

        if (polygonYBot <= polygonYTop)
            polygonYBot = std::min<u32>(targetHeight, polygonYTop + 1u);

        return (polygonYTop & 0xFFFFu) | ((polygonYBot & 0xFFFFu) << 16u);
    };

    for (const AcceleratedSceneDraw& sceneDraw : SharedGraphicsScene.Draws)
    {
        const Polygon* polygon = sceneDraw.SourcePolygon;
        if (polygon == nullptr)
            continue;

        const size_t polygonTriangleBase = Triangles.size();
        const AcceleratedPolygonMeta& polygonMeta = sceneDraw.Meta;
        const u32 alpha5 = polygonMeta.Alpha5;
        const bool polygonUsesGlTranslucentPass =
            HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagTranslucent);
        const bool isTranslucent = polygonUsesGlTranslucentPass || (alpha5 != 0u && alpha5 < 0x1Fu);
        const u32 blendMode = (polygonMeta.PolyAttr >> 4u) & 0x3u;
        const float effectiveCoverageDepthBias =
            sceneDraw.CoverageFixState.ApplyUserFix ? coverageDepthBias : 0.0f;
        const bool polygonTexturedByRegs = textureMapsEnabled && (((polygon->TexParam >> 26u) & 0x7u) != 0u);

        if (!Renderer3DDebugShouldDrawPolygon(
                polygonMeta,
                sceneDraw.PrimitiveType == AcceleratedPrimitiveType::Lines,
                polygonTexturedByRegs,
                highlightEnabled))
        {
            continue;
        }

        const std::optional<u32> debugYBounds = packSceneDrawYBounds(sceneDraw);
        if (debugYBounds.has_value() && !Renderer3DDebugYBoundsEnabled(*debugYBounds, targetHeight))
            continue;

        bool polygonTextured = polygonTexturedByRegs;
        TexcacheVulkanLoader::TextureHandle textureHandle = 0;
        u32 textureLayer = 0;
        u32* helper = nullptr;
        u32 textureDescriptorIndex = FallbackTextureDescriptorIndex;
        bool textureFallbackUsed = false;
        bool textureLayerOpaque = false;
        u32 texWidth = 0u;
        u32 texHeight = 0u;
        if (polygonTextured)
        {
            Texcache.GetTexture(
                polygon->TexParam,
                polygon->TexPalette,
                textureHandle,
                textureLayer,
                helper
            );

            texWidth = TextureWidth(polygon->TexParam);
            texHeight = TextureHeight(polygon->TexParam);
            if (texWidth == 0u || texHeight == 0u)
            {
                polygonTextured = false;
            }
            else
            {
                const TextureLookupKey textureKey{textureHandle};
                auto textureIt = textureLookup.find(textureKey);
                if (textureIt == textureLookup.end())
                {
                    VkDescriptorImageInfo textureDescriptorInfo{};
                    if (Texcache.GetLoader().GetTextureDescriptor(textureHandle, &textureDescriptorInfo)
                        && ActiveTextureDescriptorCount < MaxActiveTextureDescriptors)
                    {
                        textureDescriptorIndex = ActiveTextureDescriptorCount;
                        ActiveTextureDescriptors[textureDescriptorIndex] = textureDescriptorInfo;
                        ActiveTextureDescriptorCount++;
                        textureLookup.emplace(textureKey, TextureFrameData{textureDescriptorIndex});
                    }
                    else
                    {
                        textureDescriptorIndex = FallbackTextureDescriptorIndex;
                        textureLayer = 0u;
                        texWidth = 1u;
                        texHeight = 1u;
                        textureFallbackUsed = true;
                        textureLayerOpaque = true;
                    }
                }
                else
                {
                    textureDescriptorIndex = textureIt->second.DescriptorIndex;
                }
                if (!textureFallbackUsed)
                    textureLayerOpaque = Texcache.GetLoader().IsTextureLayerOpaque(textureHandle, textureLayer);
            }
        }

        const bool hasTexture = polygonTextured && texWidth > 0u && texHeight > 0u;
        u32 sceneVertexFlags = 0u;
        if (hasTexture)
        {
            sceneVertexFlags |= kTriangleFlagTextured;
            if (textureLayerOpaque)
                sceneVertexFlags |= kTriangleFlagTextureOpaque;
            if ((blendMode & 0x1u) != 0u && !textureFallbackUsed)
                sceneVertexFlags |= kTriangleFlagDecal;
        }
        if (sceneDraw.CoverageFixState.Apply)
            sceneVertexFlags |= kTriangleFlagCoverageFix;
        if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagWBuffer))
            sceneVertexFlags |= kTriangleFlagWBuffer;
        if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadowMask))
            sceneVertexFlags |= kTriangleFlagShadowMask;
        if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagFacingView))
            sceneVertexFlags |= kTriangleFlagFrontFacing;

        const auto makeColor = [&](const AcceleratedSceneVertex& vertex) -> u32 {
            const u32 vr = static_cast<u32>(vertex.FinalColorR) >> 3u;
            const u32 vg = static_cast<u32>(vertex.FinalColorG) >> 3u;
            const u32 vb = static_cast<u32>(vertex.FinalColorB) >> 3u;
            return packRgba8(
                to8From6(vr),
                to8From6(vg),
                to8From6(vb),
                to8From5(std::min<u32>(31u, vertex.Alpha5)));
        };

        const auto makeSceneGraphicsVertex = [&](const AcceleratedSceneVertex& vertex) -> GraphicsVertexGpu {
            GraphicsVertexGpu graphicsVertex{};
            graphicsVertex.x = vertex.X;
            graphicsVertex.y = vertex.Y;
            graphicsVertex.z = static_cast<float>(vertex.Z);
            graphicsVertex.reciprocalW = 1.0f / static_cast<float>(std::max<u32>(1u, vertex.W));
            graphicsVertex.u = static_cast<float>(vertex.TexCoordS);
            graphicsVertex.v = static_cast<float>(vertex.TexCoordT);
            graphicsVertex.colorRgba8 = makeColor(vertex);
            graphicsVertex.flags = sceneVertexFlags;
            graphicsVertex.texLayer = textureLayer;
            graphicsVertex.texArrayIndex = hasTexture ? textureDescriptorIndex : 0u;
            graphicsVertex.texWidth = hasTexture ? texWidth : 0u;
            graphicsVertex.texHeight = hasTexture ? texHeight : 0u;
            graphicsVertex.texParam = hasTexture ? polygon->TexParam : 0u;
            graphicsVertex.polyAttr = polygonMeta.PolyAttr;
            return graphicsVertex;
        };

        for (u32 vertexOffset = 0; vertexOffset < sceneDraw.VertexCount; vertexOffset++)
        {
            const u32 sceneVertexIndex = sceneDraw.FirstVertex + vertexOffset;
            if (sceneVertexIndex >= SharedGraphicsScene.Vertices.size() || sceneVertexIndex >= GraphicsSceneVertices.size())
                break;
            GraphicsSceneVertices[sceneVertexIndex] = makeSceneGraphicsVertex(SharedGraphicsScene.Vertices[sceneVertexIndex]);
        }

        const auto makeTriangleVertex = [&](const AcceleratedSceneVertex& vertex,
                                            float x,
                                            float y,
                                            std::optional<u32> colorOverride = std::nullopt) -> TriangleVertexData {
            TriangleVertexData triangleVertex{};
            triangleVertex.x = x;
            triangleVertex.y = y;
            triangleVertex.z = static_cast<float>(vertex.Z);
            triangleVertex.wRaw = std::max<u32>(1u, vertex.W);
            triangleVertex.w = static_cast<float>(triangleVertex.wRaw);
            if (sceneDraw.CoverageFixState.Apply && effectiveCoverageDepthBias > 0.0f)
                triangleVertex.z = std::max(0.0f, triangleVertex.z - effectiveCoverageDepthBias);
            triangleVertex.u = static_cast<float>(vertex.TexCoordS);
            triangleVertex.v = static_cast<float>(vertex.TexCoordT);
            triangleVertex.colorRgba8 = colorOverride.value_or(makeColor(vertex));
            return triangleVertex;
        };

        const auto appendTriangle = [&](const TriangleVertexData& vertex0,
                                        const TriangleVertexData& vertex1,
                                        const TriangleVertexData& vertex2,
                                        u32 boundaryFlags,
                                        u32 packedYBounds) {
            TriangleGpu triangle{};
            triangle.x0 = vertex0.x;
            triangle.y0 = vertex0.y;
            triangle.z0 = vertex0.z;
            triangle.w0 = vertex0.w;
            triangle.x1 = vertex1.x;
            triangle.y1 = vertex1.y;
            triangle.z1 = vertex1.z;
            triangle.w1 = vertex1.w;
            triangle.x2 = vertex2.x;
            triangle.y2 = vertex2.y;
            triangle.z2 = vertex2.z;
            triangle.w2 = vertex2.w;
            triangle.u0 = vertex0.u;
            triangle.v0 = vertex0.v;
            triangle.u1 = vertex1.u;
            triangle.v1 = vertex1.v;
            triangle.u2 = vertex2.u;
            triangle.v2 = vertex2.v;
            triangle.yBounds = packedYBounds;
            triangle.texLayer = textureLayer;
            triangle.color0Rgba8 = vertex0.colorRgba8;
            triangle.color1Rgba8 = vertex1.colorRgba8;
            triangle.color2Rgba8 = vertex2.colorRgba8;

            const u32 a0 = (triangle.color0Rgba8 >> 24u) & 0xFFu;
            const u32 a1 = (triangle.color1Rgba8 >> 24u) & 0xFFu;
            const u32 a2 = (triangle.color2Rgba8 >> 24u) & 0xFFu;
            const bool alphaTranslucent = (a0 < 255u) || (a1 < 255u) || (a2 < 255u);

            triangle.flags = boundaryFlags;
            if (isTranslucent || alphaTranslucent)
                triangle.flags |= kTriangleFlagTranslucent;
            if (hasTexture)
            {
                triangle.flags |= kTriangleFlagTextured;
                if (textureLayerOpaque)
                    triangle.flags |= kTriangleFlagTextureOpaque;
                if ((blendMode & 0x1u) != 0u && !textureFallbackUsed)
                    triangle.flags |= kTriangleFlagDecal;
                triangle.texArrayIndex = textureDescriptorIndex;
                triangle.texWidth = texWidth;
                triangle.texHeight = texHeight;
                triangle.texParam = polygon->TexParam;
            }
            if (sceneDraw.CoverageFixState.Apply)
                triangle.flags |= kTriangleFlagCoverageFix;
            if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagWBuffer))
                triangle.flags |= kTriangleFlagWBuffer;
            if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadowMask))
                triangle.flags |= kTriangleFlagShadowMask;
            if (vertex0.wRaw == vertex1.wRaw && vertex1.wRaw == vertex2.wRaw && (vertex0.wRaw & 0x7Fu) == 0u)
                triangle.flags |= kTriangleFlagLinear;
            if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagFacingView))
                triangle.flags |= kTriangleFlagFrontFacing;

            const auto isTopLeftEdge = [](const TriangleVertexData& start, const TriangleVertexData& end) -> bool {
                const float deltaY = end.y - start.y;
                if (std::fabs(deltaY) < 0.000001f)
                    return (end.x - start.x) > 0.0f;
                return deltaY < 0.0f;
            };
            const float signedArea = (vertex2.x - vertex0.x) * (vertex1.y - vertex0.y)
                - (vertex2.y - vertex0.y) * (vertex1.x - vertex0.x);
            const bool positiveArea = signedArea > 0.0f;
            if ((triangle.flags & kTriangleFlagBoundaryEdge0) == 0u
                && (positiveArea ? isTopLeftEdge(vertex1, vertex2) : isTopLeftEdge(vertex2, vertex1)))
            {
                triangle.flags |= kTriangleFlagTopLeftEdge0;
            }
            if ((triangle.flags & kTriangleFlagBoundaryEdge1) == 0u
                && (positiveArea ? isTopLeftEdge(vertex2, vertex0) : isTopLeftEdge(vertex0, vertex2)))
            {
                triangle.flags |= kTriangleFlagTopLeftEdge1;
            }
            if ((triangle.flags & kTriangleFlagBoundaryEdge2) == 0u
                && (positiveArea ? isTopLeftEdge(vertex0, vertex1) : isTopLeftEdge(vertex1, vertex0)))
            {
                triangle.flags |= kTriangleFlagTopLeftEdge2;
            }

            triangle.polyAttr = polygonMeta.PolyAttr;
            triangle.variantKey = 0u;
            if (hasTexture)
                triangle.variantKey |= kVariantFlagTextured;
            if (blendMode == 2u)
            {
                triangle.variantKey |= highlightEnabled ? kVariantFlagHighlight : kVariantFlagToon;
            }
            else if (hasTexture && (blendMode & 0x1u) != 0u && !textureFallbackUsed)
            {
                triangle.variantKey |= kVariantFlagDecal;
            }
            else
            {
                triangle.variantKey |= kVariantFlagModulate;
            }
            if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadowMask))
                triangle.variantKey |= kVariantFlagShadowMask;
            if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagWBuffer))
                triangle.variantKey |= kVariantFlagWBuffer;
            if (isTranslucent || alphaTranslucent)
                triangle.variantKey |= kVariantFlagTranslucent;
            if (sceneDraw.CoverageFixState.Apply)
                triangle.variantKey |= kVariantFlagCoverageFix;

            Triangles.push_back(triangle);

            const auto reciprocalW = [](float w) {
                return 1.0f / std::max(w, 1.0f);
            };
            const auto appendGraphicsVertex = [&](const TriangleVertexData& vertexData) {
                GraphicsVertexGpu graphicsVertex{};
                graphicsVertex.x = vertexData.x;
                graphicsVertex.y = vertexData.y;
                graphicsVertex.z = vertexData.z;
                graphicsVertex.reciprocalW = reciprocalW(vertexData.w);
                graphicsVertex.u = vertexData.u;
                graphicsVertex.v = vertexData.v;
                graphicsVertex.colorRgba8 = vertexData.colorRgba8;
                graphicsVertex.flags = triangle.flags;
                graphicsVertex.texLayer = triangle.texLayer;
                graphicsVertex.texArrayIndex = triangle.texArrayIndex;
                graphicsVertex.texWidth = triangle.texWidth;
                graphicsVertex.texHeight = triangle.texHeight;
                graphicsVertex.texParam = triangle.texParam;
                graphicsVertex.polyAttr = triangle.polyAttr;
                GraphicsVertices.push_back(graphicsVertex);
            };

            appendGraphicsVertex(vertex0);
            appendGraphicsVertex(vertex1);
            appendGraphicsVertex(vertex2);
        };

        const auto appendLineSegment = [&](u16 vertexIndex0,
                                           u16 vertexIndex1,
                                           std::optional<u32> colorOverride = std::nullopt,
                                           float endpointExtend = 0.0f) {
            if (vertexIndex0 >= SharedGraphicsScene.Vertices.size() || vertexIndex1 >= SharedGraphicsScene.Vertices.size())
                return;

            const AcceleratedSceneVertex& lineVertex0 = SharedGraphicsScene.Vertices[vertexIndex0];
            const AcceleratedSceneVertex& lineVertex1 = SharedGraphicsScene.Vertices[vertexIndex1];
            const float lineX0 = lineVertex0.X;
            const float lineY0 = lineVertex0.Y;
            const float lineX1 = lineVertex1.X;
            const float lineY1 = lineVertex1.Y;

            const float deltaX = lineX1 - lineX0;
            const float deltaY = lineY1 - lineY0;
            const float lineLengthSquared = (deltaX * deltaX) + (deltaY * deltaY);
            if (lineLengthSquared <= 0.000001f)
                return;

            const float inverseLineLength = 1.0f / std::sqrt(lineLengthSquared);
            const float lineDirX = deltaX * inverseLineLength;
            const float lineDirY = deltaY * inverseLineLength;
            const float halfLineWidth = 0.5f;
            const float perpX = -deltaY * inverseLineLength * halfLineWidth;
            const float perpY = deltaX * inverseLineLength * halfLineWidth;
            const float startX = lineX0 - (lineDirX * endpointExtend);
            const float startY = lineY0 - (lineDirY * endpointExtend);
            const float endX = lineX1 + (lineDirX * endpointExtend);
            const float endY = lineY1 + (lineDirY * endpointExtend);

            const float quadPositionsX[4] = {
                startX + perpX,
                startX - perpX,
                endX - perpX,
                endX + perpX,
            };
            const float quadPositionsY[4] = {
                startY + perpY,
                startY - perpY,
                endY - perpY,
                endY + perpY,
            };

            const std::optional<u32> packedLineYBounds = packYBounds(quadPositionsY, 4u);
            if (!packedLineYBounds.has_value())
                return;

            appendTriangle(
                makeTriangleVertex(lineVertex0, quadPositionsX[0], quadPositionsY[0], colorOverride),
                makeTriangleVertex(lineVertex0, quadPositionsX[1], quadPositionsY[1], colorOverride),
                makeTriangleVertex(lineVertex1, quadPositionsX[2], quadPositionsY[2], colorOverride),
                kTriangleFlagBoundaryEdge0 | kTriangleFlagBoundaryEdge2,
                *packedLineYBounds);
            appendTriangle(
                makeTriangleVertex(lineVertex0, quadPositionsX[0], quadPositionsY[0], colorOverride),
                makeTriangleVertex(lineVertex1, quadPositionsX[2], quadPositionsY[2], colorOverride),
                makeTriangleVertex(lineVertex1, quadPositionsX[3], quadPositionsY[3], colorOverride),
                kTriangleFlagBoundaryEdge0 | kTriangleFlagBoundaryEdge1,
                *packedLineYBounds);
        };

        const auto enqueueGraphicsDraw = [&](size_t polygonTriangleCount,
                                             bool suppressEdgeMarkIndices = false,
                                             u32 edgeColorOverrideMask = 0u,
                                             u32 edgeColorOverridePacked = 0u) {
            if (polygonTriangleCount == 0u)
                return;

            GraphicsPolygonDraw draw{};
            draw.firstTriangle = static_cast<u32>(polygonTriangleBase);
            draw.triangleCount = static_cast<u32>(polygonTriangleCount);
            draw.polyAttr = polygonMeta.PolyAttr;
            draw.flags = polygonMeta.Flags;
            draw.firstVertex = sceneDraw.FirstVertex;
            draw.vertexCount = sceneDraw.VertexCount;
            draw.firstEdgeIndex = sceneDraw.FirstEdgeIndex;
            draw.edgeIndexCount = suppressEdgeMarkIndices ? 0u : sceneDraw.EdgeIndexCount;
            draw.edgeColorOverrideMask = edgeColorOverrideMask;
            draw.edgeColorOverridePacked = edgeColorOverridePacked;

            const u32 drawIndex = static_cast<u32>(GraphicsPolygons.size());
            GraphicsPolygons.push_back(draw);

            if (!polygonUsesGlTranslucentPass
                && !HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadowMask)
                && !HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadow))
            {
                GraphicsOpaqueDrawIndices.push_back(drawIndex);
            }

            if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagNeedOpaquePass))
                GraphicsNeedOpaqueDrawIndices.push_back(drawIndex);

            if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadowMask))
            {
                GraphicsShadowMaskDrawIndices.push_back(drawIndex);
            }
            else if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadow))
            {
                GraphicsShadowDrawIndices.push_back(drawIndex);
            }
            else if (polygonUsesGlTranslucentPass)
            {
                GraphicsAlphaDrawIndices.push_back(drawIndex);
            }
        };

        if (sceneDraw.PrimitiveType == AcceleratedPrimitiveType::Lines)
        {
            if (sceneDraw.IndexCount < 2u || (sceneDraw.FirstIndex + 1u) >= SharedGraphicsScene.Indices.size())
                continue;

            const u16 vertexIndex0 = SharedGraphicsScene.Indices[sceneDraw.FirstIndex];
            const u16 vertexIndex1 = SharedGraphicsScene.Indices[sceneDraw.FirstIndex + 1u];
            if (vertexIndex0 >= SharedGraphicsScene.Vertices.size() || vertexIndex1 >= SharedGraphicsScene.Vertices.size())
                continue;

            appendLineSegment(vertexIndex0, vertexIndex1);
            enqueueGraphicsDraw(Triangles.size() - polygonTriangleBase);
            continue;
        }

        if (alpha5 == 0u)
        {
            const bool hiddenLayerAlphaZero =
                !hasTexture
                && blendMode == 0u
                && polygonMeta.Flags == 0u;
            const std::optional<u32> wireframeLineColor =
                hiddenLayerAlphaZero ? std::optional<u32>(0xFFFFFFFFu) : std::nullopt;
            const float wireframeEndpointExtend = 0.0f;
            const u32 wireframeEdgeColorOverrideMask = hiddenLayerAlphaZero
                ? (1u << ((polygonMeta.PolyId >> 3u) & 0x7u))
                : 0u;
            const u32 wireframeEdgeColorOverridePacked = hiddenLayerAlphaZero ? 0x00FFFFFFu : 0u;
            if (hiddenLayerAlphaZero && polygonMeta.PolyId == 56u)
            {
                GraphicsHiddenAlphaZeroFinalEdgePolyIdOverride = 56u;
                GraphicsHiddenAlphaZeroFinalEdgeColorOverride = 0x00FFFFFFu;
            }

            u32 emittedBoundaryEdgeCount = 0u;
            for (u32 triangleIndex = sceneDraw.FirstTriangle;
                 triangleIndex < sceneDraw.FirstTriangle + sceneDraw.TriangleCount;
                 triangleIndex++)
            {
                if (triangleIndex >= SharedGraphicsScene.Triangles.size())
                    break;

                const AcceleratedSceneTriangle& sceneTriangle = SharedGraphicsScene.Triangles[triangleIndex];
                const u16 vertexIndex0 = sceneTriangle.Indices[0];
                const u16 vertexIndex1 = sceneTriangle.Indices[1];
                const u16 vertexIndex2 = sceneTriangle.Indices[2];
                if (vertexIndex0 >= SharedGraphicsScene.Vertices.size()
                    || vertexIndex1 >= SharedGraphicsScene.Vertices.size()
                    || vertexIndex2 >= SharedGraphicsScene.Vertices.size())
                {
                    continue;
                }

                if ((sceneTriangle.BoundaryFlags & AcceleratedTriangleBoundaryEdge0) != 0u)
                {
                    appendLineSegment(vertexIndex1, vertexIndex2, wireframeLineColor, wireframeEndpointExtend);
                    emittedBoundaryEdgeCount++;
                }
                if ((sceneTriangle.BoundaryFlags & AcceleratedTriangleBoundaryEdge1) != 0u)
                {
                    appendLineSegment(vertexIndex2, vertexIndex0, wireframeLineColor, wireframeEndpointExtend);
                    emittedBoundaryEdgeCount++;
                }
                if ((sceneTriangle.BoundaryFlags & AcceleratedTriangleBoundaryEdge2) != 0u)
                {
                    appendLineSegment(vertexIndex0, vertexIndex1, wireframeLineColor, wireframeEndpointExtend);
                    emittedBoundaryEdgeCount++;
                }
            }

            static u32 loggedWireframePolygonCount = 0u;
            if (loggedWireframePolygonCount < 24u && MelonDSAndroid::areRendererDebugToolsEnabled())
            {
                const u32 yBounds = debugYBounds.value_or(0u);
                Log(
                    LogLevel::Warn,
                    "VulkanGraphics[AlphaZeroWireframe]: sample=%u polyId=%u blend=%u flags=%#x hasTexture=%u texParam=%#x vertexCount=%u edgeIndexCount=%u originalTriCount=%u emittedBoundaryEdgeCount=%u hiddenLayerAlphaZero=%u lineColor=%#x edgeColorOverrideMask=%#x finalEdgePolyIdOverride=%u y=%u..%u",
                    loggedWireframePolygonCount,
                    polygonMeta.PolyId,
                    (polygonMeta.PolyAttr >> 4u) & 0x3u,
                    polygonMeta.Flags,
                    hasTexture ? 1u : 0u,
                    polygon->TexParam,
                    sceneDraw.VertexCount,
                    sceneDraw.EdgeIndexCount,
                    sceneDraw.TriangleCount,
                    emittedBoundaryEdgeCount,
                    hiddenLayerAlphaZero ? 1u : 0u,
                    wireframeLineColor.value_or(0u),
                    wireframeEdgeColorOverrideMask,
                    GraphicsHiddenAlphaZeroFinalEdgePolyIdOverride < 64u ? GraphicsHiddenAlphaZeroFinalEdgePolyIdOverride : 0xFFFFFFFFu,
                    yBounds & 0xFFFFu,
                    (yBounds >> 16u) & 0xFFFFu);
                loggedWireframePolygonCount++;
            }

            enqueueGraphicsDraw(
                Triangles.size() - polygonTriangleBase,
                false,
                wireframeEdgeColorOverrideMask,
                wireframeEdgeColorOverridePacked);
            continue;
        }

        for (u32 triangleIndex = sceneDraw.FirstTriangle;
             triangleIndex < sceneDraw.FirstTriangle + sceneDraw.TriangleCount;
             triangleIndex++)
        {
            if (triangleIndex >= SharedGraphicsScene.Triangles.size())
                break;

            const AcceleratedSceneTriangle& sceneTriangle = SharedGraphicsScene.Triangles[triangleIndex];
            const u16 vertexIndex0 = sceneTriangle.Indices[0];
            const u16 vertexIndex1 = sceneTriangle.Indices[1];
            const u16 vertexIndex2 = sceneTriangle.Indices[2];
            if (vertexIndex0 >= SharedGraphicsScene.Vertices.size()
                || vertexIndex1 >= SharedGraphicsScene.Vertices.size()
                || vertexIndex2 >= SharedGraphicsScene.Vertices.size())
            {
                continue;
            }

            u32 boundaryFlags = 0u;
            if ((sceneTriangle.BoundaryFlags & AcceleratedTriangleBoundaryEdge0) != 0u)
                boundaryFlags |= kTriangleFlagBoundaryEdge0;
            if ((sceneTriangle.BoundaryFlags & AcceleratedTriangleBoundaryEdge1) != 0u)
                boundaryFlags |= kTriangleFlagBoundaryEdge1;
            if ((sceneTriangle.BoundaryFlags & AcceleratedTriangleBoundaryEdge2) != 0u)
                boundaryFlags |= kTriangleFlagBoundaryEdge2;

            const AcceleratedSceneVertex& vertex0 = SharedGraphicsScene.Vertices[vertexIndex0];
            const AcceleratedSceneVertex& vertex1 = SharedGraphicsScene.Vertices[vertexIndex1];
            const AcceleratedSceneVertex& vertex2 = SharedGraphicsScene.Vertices[vertexIndex2];
            appendTriangle(
                makeTriangleVertex(vertex0, vertex0.X, vertex0.Y),
                makeTriangleVertex(vertex1, vertex1.X, vertex1.Y),
                makeTriangleVertex(vertex2, vertex2.X, vertex2.Y),
                boundaryFlags,
                sceneTriangle.PackedYBounds);
        }

        enqueueGraphicsDraw(Triangles.size() - polygonTriangleBase);
    }

    LastGraphicsOpaqueDrawCount = static_cast<u32>(GraphicsOpaqueDrawIndices.size());
    LastGraphicsNeedOpaqueDrawCount = static_cast<u32>(GraphicsNeedOpaqueDrawIndices.size());
    LastGraphicsAlphaDrawCount = static_cast<u32>(
        GraphicsAlphaDrawIndices.size() + GraphicsShadowMaskDrawIndices.size() + GraphicsShadowDrawIndices.size());
    LastGraphicsOpaqueWDrawCount = 0;
    LastGraphicsOpaqueZDrawCount = 0;
    LastGraphicsOpaqueTexturedDrawCount = 0;
    LastGraphicsOpaqueUntexturedDrawCount = 0;
    LastGraphicsOpaqueModulateDrawCount = 0;
    LastGraphicsOpaqueDecalDrawCount = 0;
    LastGraphicsOpaqueToonDrawCount = 0;
    LastGraphicsOpaqueHighlightDrawCount = 0;
    LastGraphicsOpaqueLinearDrawCount = 0;
    LastGraphicsOpaqueRepeatDrawCount = 0;
    LastGraphicsOpaqueMirrorDrawCount = 0;
    LastGraphicsOpaqueRepeatSDrawCount = 0;
    LastGraphicsOpaqueRepeatTDrawCount = 0;
    LastGraphicsOpaqueMirrorSDrawCount = 0;
    LastGraphicsOpaqueMirrorTDrawCount = 0;
    LastGraphicsOpaqueClampSDrawCount = 0;
    LastGraphicsOpaqueClampTDrawCount = 0;
    LastGraphicsOpaqueFullAlphaDrawCount = 0;
    LastGraphicsOpaqueHighresRepeatModelDrawCount = 0;
    for (u32 drawIndex : GraphicsOpaqueDrawIndices)
    {
        if (drawIndex >= GraphicsPolygons.size())
            continue;

        const GraphicsPolygonDraw& draw = GraphicsPolygons[drawIndex];
        const u32 firstTriangleFlags = draw.firstTriangle < Triangles.size() ? Triangles[draw.firstTriangle].flags : 0u;
        if ((draw.flags & AcceleratedPolygonFlagWBuffer) != 0u)
            LastGraphicsOpaqueWDrawCount++;
        else
            LastGraphicsOpaqueZDrawCount++;

        const bool textured = (firstTriangleFlags & kTriangleFlagTextured) != 0u;
        if (textured)
        {
            LastGraphicsOpaqueTexturedDrawCount++;
            if ((firstTriangleFlags & kTriangleFlagTextureOpaque) != 0u
                && ((draw.polyAttr >> 16u) & 0x1Fu) == 0x1Fu
                && gpu.GPU3D.RenderAlphaRef < 0x1Fu)
            {
                LastGraphicsOpaqueFullAlphaDrawCount++;
            }
            if ((firstTriangleFlags & kTriangleFlagDecal) != 0u)
                LastGraphicsOpaqueDecalDrawCount++;
            else
                LastGraphicsOpaqueModulateDrawCount++;

            const u32 texParam = Triangles[draw.firstTriangle].texParam;
            const u32 textureFormat = (texParam >> 26u) & 0x7u;
            const u32 alpha5 = (draw.polyAttr >> 16u) & 0x1Fu;
            const u32 blendMode = (draw.polyAttr >> 4u) & 0x3u;
            const bool color0Transparent = (texParam & (1u << 29u)) != 0u;
            const bool repeatS = (texParam & (1u << 16u)) != 0u;
            const bool repeatT = (texParam & (1u << 17u)) != 0u;
            const bool mirrorS = (texParam & (1u << 18u)) != 0u;
            const bool mirrorT = (texParam & (1u << 19u)) != 0u;
            if (repeatS || repeatT)
                LastGraphicsOpaqueRepeatDrawCount++;
            if (mirrorS || mirrorT)
                LastGraphicsOpaqueMirrorDrawCount++;
            if (repeatS)
                LastGraphicsOpaqueRepeatSDrawCount++;
            else
                LastGraphicsOpaqueClampSDrawCount++;
            if (repeatT)
                LastGraphicsOpaqueRepeatTDrawCount++;
            else
                LastGraphicsOpaqueClampTDrawCount++;
            if (mirrorS)
                LastGraphicsOpaqueMirrorSDrawCount++;
            if (mirrorT)
                LastGraphicsOpaqueMirrorTDrawCount++;
            if ((firstTriangleFlags & kTriangleFlagLinear) != 0u
                && (textureFormat == 4u || textureFormat == 5u)
                && !color0Transparent
                && alpha5 == 31u
                && blendMode == 0u
                && (repeatS || repeatT || mirrorS || mirrorT))
            {
                LastGraphicsOpaqueHighresRepeatModelDrawCount++;
            }
        }
        else
            LastGraphicsOpaqueUntexturedDrawCount++;

        const u32 blendMode = (draw.polyAttr >> 4u) & 0x3u;
        if (blendMode == 2u)
        {
            if (highlightEnabled)
                LastGraphicsOpaqueHighlightDrawCount++;
            else
                LastGraphicsOpaqueToonDrawCount++;
        }
        if ((firstTriangleFlags & kTriangleFlagLinear) != 0u)
            LastGraphicsOpaqueLinearDrawCount++;
    }

    if (MelonDSAndroid::areRendererDebugToolsEnabled() && CaptureDebugLogsRemaining > 0u)
    {
        const u32 firstTranslucentDraw = SharedGraphicsScene.FirstTranslucentDraw == std::numeric_limits<u32>::max()
            ? 0xFFFFFFFFu
            : SharedGraphicsScene.FirstTranslucentDraw;
        Log(
            LogLevel::Warn,
            "VulkanGraphics[Scene]: draws=%zu triangles=%zu vertices=%zu indices=%zu firstTranslucent=%u opaque=%u needOpaque=%u alpha=%zu shadowMask=%zu shadow=%zu",
            GraphicsPolygons.size(),
            Triangles.size(),
            SharedGraphicsScene.Vertices.size(),
            SharedGraphicsScene.Indices.size(),
            firstTranslucentDraw,
            LastGraphicsOpaqueDrawCount,
            LastGraphicsNeedOpaqueDrawCount,
            GraphicsAlphaDrawIndices.size(),
            GraphicsShadowMaskDrawIndices.size(),
            GraphicsShadowDrawIndices.size());
        CaptureDebugLogsRemaining--;

        const auto logGraphicsBucket = [&](const char* label, const std::vector<u32>& drawIndices) {
            if (CaptureDebugLogsRemaining == 0u)
                return;

            Log(LogLevel::Warn, "VulkanGraphics[%s]: count=%zu", label, drawIndices.size());
            CaptureDebugLogsRemaining--;

            const size_t maxSampleCount = std::strcmp(label, "AlphaBucket") == 0 ? 24u : 3u;
            const size_t sampleCount = std::min<size_t>(drawIndices.size(), maxSampleCount);
            for (size_t sampleIndex = 0; sampleIndex < sampleCount && CaptureDebugLogsRemaining > 0u; sampleIndex++)
            {
                const u32 drawIndex = drawIndices[sampleIndex];
                if (drawIndex >= GraphicsPolygons.size())
                    continue;

                const GraphicsPolygonDraw& draw = GraphicsPolygons[drawIndex];
                const u32 polyId = (draw.polyAttr >> 24u) & 0x3Fu;
                const u32 alpha5 = (draw.polyAttr >> 16u) & 0x1Fu;
                const u32 blendMode = (draw.polyAttr >> 4u) & 0x3u;
                const u32 yBounds = draw.firstTriangle < Triangles.size()
                    ? Triangles[draw.firstTriangle].yBounds
                    : 0u;
                Log(
                    LogLevel::Warn,
                    "VulkanGraphics[%s]: sample=%zu draw=%u triBase=%u triCount=%u polyId=%u alpha5=%u blend=%u flags=%#x depthEq=%u depthWrite=%u fogWrite=%u y=%u..%u",
                    label,
                    sampleIndex,
                    drawIndex,
                    draw.firstTriangle,
                    draw.triangleCount,
                    polyId,
                    alpha5,
                    blendMode,
                    draw.flags,
                    (draw.flags & AcceleratedPolygonFlagDepthEqual) != 0u ? 1u : 0u,
                    (draw.polyAttr & (1u << 11u)) != 0u ? 1u : 0u,
                    (draw.flags & AcceleratedPolygonFlagFogWrite) != 0u ? 1u : 0u,
                    yBounds & 0xFFFFu,
                    (yBounds >> 16u) & 0xFFFFu);
                CaptureDebugLogsRemaining--;

                if (draw.firstTriangle < Triangles.size() && CaptureDebugLogsRemaining > 0u)
                {
                    const TriangleGpu& tri = Triangles[draw.firstTriangle];
                    Log(
                        LogLevel::Warn,
                        "VulkanGraphics[%sDetail]: draw=%u triFlags=%#x texDesc=%u texLayer=%u texSize=%ux%u texParam=%#x color=%#x,%#x,%#x pos=(%.3f,%.3f)->(%.3f,%.3f)->(%.3f,%.3f) uv=(%.3f,%.3f)->(%.3f,%.3f)->(%.3f,%.3f) w=(%.3f,%.3f,%.3f) yBounds=%#x",
                        label,
                        drawIndex,
                        tri.flags,
                        tri.texArrayIndex,
                        tri.texLayer,
                        tri.texWidth,
                        tri.texHeight,
                        tri.texParam,
                        tri.color0Rgba8,
                        tri.color1Rgba8,
                        tri.color2Rgba8,
                        tri.x0, tri.y0,
                        tri.x1, tri.y1,
                        tri.x2, tri.y2,
                        tri.u0, tri.v0,
                        tri.u1, tri.v1,
                        tri.u2, tri.v2,
                        tri.w0, tri.w1, tri.w2,
                        tri.yBounds);
                    CaptureDebugLogsRemaining--;
                }
            }
        };

        logGraphicsBucket("OpaqueBucket", GraphicsOpaqueDrawIndices);
        logGraphicsBucket("NeedOpaqueBucket", GraphicsNeedOpaqueDrawIndices);
        logGraphicsBucket("AlphaBucket", GraphicsAlphaDrawIndices);
        logGraphicsBucket("ShadowMaskBucket", GraphicsShadowMaskDrawIndices);
        logGraphicsBucket("ShadowBucket", GraphicsShadowDrawIndices);
    }

    static bool loggedGraphicsTriangleSummary = false;
    if (!loggedGraphicsTriangleSummary && !Triangles.empty())
    {
        size_t viewportIntersectingCount = 0u;
        size_t nonDegenerateCount = 0u;
        for (const TriangleGpu& triangle : Triangles)
        {
            const float minX = std::min({triangle.x0, triangle.x1, triangle.x2});
            const float maxX = std::max({triangle.x0, triangle.x1, triangle.x2});
            const float minY = std::min({triangle.y0, triangle.y1, triangle.y2});
            const float maxY = std::max({triangle.y0, triangle.y1, triangle.y2});
            if (maxX > 0.0f && maxY > 0.0f && minX < maxTargetX && minY < maxTargetY)
                viewportIntersectingCount++;

            const float signedArea =
                ((triangle.x1 - triangle.x0) * (triangle.y2 - triangle.y0))
                - ((triangle.y1 - triangle.y0) * (triangle.x2 - triangle.x0));
            if (std::fabs(signedArea) > 0.001f)
                nonDegenerateCount++;
        }

        const TriangleGpu& triangle = Triangles.front();
        const float rawW0 = triangle.w0 > 0.000001f ? (1.0f / triangle.w0) : 0.0f;
        const float rawW1 = triangle.w1 > 0.000001f ? (1.0f / triangle.w1) : 0.0f;
        const float rawW2 = triangle.w2 > 0.000001f ? (1.0f / triangle.w2) : 0.0f;
        Log(
            LogLevel::Warn,
            "VulkanGraphics[Triangles]: scale=%d count=%zu viewportIntersect=%zu nonDegenerate=%zu textures=%u first tri pos=(%.3f,%.3f,%.3f,w=%.3f)->(%.3f,%.3f,%.3f,w=%.3f)->(%.3f,%.3f,%.3f,w=%.3f) flags=%#x texDesc=%u texLayer=%u texSize=%ux%u texParam=%#x polyAttr=%#x yBounds=%#x",
            ScaleFactor,
            Triangles.size(),
            viewportIntersectingCount,
            nonDegenerateCount,
            ActiveTextureDescriptorCount,
            triangle.x0, triangle.y0, triangle.z0, rawW0,
            triangle.x1, triangle.y1, triangle.z1, rawW1,
            triangle.x2, triangle.y2, triangle.z2, rawW2,
            triangle.flags,
            triangle.texArrayIndex,
            triangle.texLayer,
            triangle.texWidth,
            triangle.texHeight,
            triangle.texParam,
            triangle.polyAttr,
            triangle.yBounds);
        if (!GraphicsPolygons.empty())
        {
            const GraphicsPolygonDraw& draw = GraphicsPolygons.front();
            Log(
                LogLevel::Warn,
                "VulkanGraphics[Draws]: polygons=%zu opaque=%u needOpaque=%u alphaShadow=%u shadowMask=%zu shadow=%zu first firstTriangle=%u triangleCount=%u polyAttr=%#x flags=%#x dispCnt=%#x alphaRef=%u",
                GraphicsPolygons.size(),
                LastGraphicsOpaqueDrawCount,
                LastGraphicsNeedOpaqueDrawCount,
                LastGraphicsAlphaDrawCount,
                GraphicsShadowMaskDrawIndices.size(),
                GraphicsShadowDrawIndices.size(),
                draw.firstTriangle,
                draw.triangleCount,
                draw.polyAttr,
                draw.flags,
                gpu.GPU3D.RenderDispCnt,
                gpu.GPU3D.RenderAlphaRef);
        }
        loggedGraphicsTriangleSummary = true;
    }
}

void VulkanRenderer3D::buildTriangleList(melonDS::GPU& gpu)
{
    Triangles.clear();
    GraphicsVertices.clear();
    GraphicsSceneVertices.clear();
    GraphicsPolygons.clear();
    GraphicsOpaqueDrawIndices.clear();
    GraphicsNeedOpaqueDrawIndices.clear();
    GraphicsAlphaDrawIndices.clear();
    GraphicsShadowMaskDrawIndices.clear();
    GraphicsShadowDrawIndices.clear();
    ActiveTextureDescriptorCount = 0;
    ActiveTextureDescriptors.fill(VkDescriptorImageInfo{});
    Triangles.reserve(static_cast<size_t>(gpu.GPU3D.RenderNumPolygons) * 3u);
    GraphicsVertices.reserve(static_cast<size_t>(gpu.GPU3D.RenderNumPolygons) * 9u);
    GraphicsPolygons.reserve(static_cast<size_t>(gpu.GPU3D.RenderNumPolygons));
    GraphicsOpaqueDrawIndices.reserve(static_cast<size_t>(gpu.GPU3D.RenderNumPolygons));
    GraphicsNeedOpaqueDrawIndices.reserve(static_cast<size_t>(gpu.GPU3D.RenderNumPolygons));
    GraphicsAlphaDrawIndices.reserve(static_cast<size_t>(gpu.GPU3D.RenderNumPolygons));
    GraphicsShadowMaskDrawIndices.reserve(static_cast<size_t>(gpu.GPU3D.RenderNumPolygons));
    GraphicsShadowDrawIndices.reserve(static_cast<size_t>(gpu.GPU3D.RenderNumPolygons));

    if (ActiveBackendMode == BackendMode::GraphicsHardware)
    {
        buildGraphicsTriangleList(gpu);
        return;
    }

    struct TextureFrameData
    {
        u32 DescriptorIndex = 0;
    };

    struct TextureLookupKey
    {
        TexcacheVulkanLoader::TextureHandle Handle = 0;

        bool operator==(const TextureLookupKey& other) const noexcept
        {
            return Handle == other.Handle;
        }
    };

    struct TextureLookupHasher
    {
        size_t operator()(const TextureLookupKey& key) const noexcept
        {
            return std::hash<u64>{}(key.Handle);
        }
    };

    std::unordered_map<TextureLookupKey, TextureFrameData, TextureLookupHasher> textureLookup{};
    textureLookup.reserve(static_cast<size_t>(gpu.GPU3D.RenderNumPolygons));

    const u32 targetHeight = 192u * static_cast<u32>(std::max(1, ScaleFactor));
    const float scale = static_cast<float>(std::max(1, ScaleFactor));
    const float maxTargetX = 256.0f * scale;
    const float maxTargetY = 192.0f * scale;
    const s32 maxTargetFixedX = static_cast<s32>(256u * static_cast<u32>(std::max(1, ScaleFactor)) * 16u);
    const s32 maxTargetFixedY = static_cast<s32>(192u * static_cast<u32>(std::max(1, ScaleFactor)) * 16u);
    // Keep Vulkan geometry in subpixel space even at 1x. Integer-snapped FinalPosition
    // opens visible cracks on repeat-textured floors once the passive coverage expand is disabled.
    const bool useHiresCoordinates = true;
    const bool textureMapsEnabled = (gpu.GPU3D.RenderDispCnt & (1u << 0)) != 0;
    const bool disablePassiveRepeatCoverageExpand =
        (MelonDSAndroid::getVulkanDiagnosticFlags() & kVulkanDiagnosticDisablePassiveRepeatCoverageExpand) != 0u;
    const float coverageDepthBias = CoverageFixDepthBias * 16777215.0f;

    const auto resolveVertexX = [&](const Vertex* vertex) -> float {
        const s32 xFixed = ResolveAcceleratedVertexFixedX(*vertex, ScaleFactor, useHiresCoordinates);
        return std::clamp(static_cast<float>(xFixed) * (1.0f / 16.0f), 0.0f, maxTargetX);
    };

    const auto resolveVertexY = [&](const Vertex* vertex) -> float {
        const s32 yFixed = ResolveAcceleratedVertexFixedY(*vertex, ScaleFactor, useHiresCoordinates);
        return std::clamp(static_cast<float>(yFixed) * (1.0f / 16.0f), 0.0f, maxTargetY);
    };

    const auto to8From6 = [](u32 c6) -> u32 {
        c6 &= 0x3Fu;
        return (c6 << 2) | (c6 >> 4);
    };

    const auto to8From5 = [](u32 c5) -> u32 {
        c5 &= 0x1Fu;
        return (c5 << 3) | (c5 >> 2);
    };

    const auto packRgba8 = [](u32 r, u32 g, u32 b, u32 a) -> u32 {
        return (r & 0xFFu) | ((g & 0xFFu) << 8) | ((b & 0xFFu) << 16) | ((a & 0xFFu) << 24);
    };

    for (u32 i = 0; i < gpu.GPU3D.RenderNumPolygons; i++)
    {
        Polygon* polygon = gpu.GPU3D.RenderPolygonRAM[i];
        if (polygon == nullptr
            || polygon->Degenerate
            || (polygon->Type == 1 ? polygon->NumVertices < 2 : polygon->NumVertices < 3))
            continue;

        const size_t polygonTriangleBase = Triangles.size();
        const AcceleratedPolygonMeta polygonMeta = BuildAcceleratedPolygonMeta(*polygon);

        const u32 alpha5 = polygonMeta.Alpha5;
        const bool polygonUsesGlTranslucentPass =
            HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagTranslucent);
        const bool isTranslucent = polygonUsesGlTranslucentPass || (alpha5 != 0u && alpha5 < 0x1Fu);
        const u32 blendMode = (polygonMeta.PolyAttr >> 4) & 0x3u;
        const bool highlightEnabled = (gpu.GPU3D.RenderDispCnt & (1u << 1)) != 0;
        const bool polygonTexturedByRegs = textureMapsEnabled && (((polygon->TexParam >> 26) & 0x7u) != 0u);
        if (!Renderer3DDebugShouldDrawPolygon(
                polygonMeta,
                polygon->Type == 1,
                polygonTexturedByRegs,
                highlightEnabled))
        {
            continue;
        }

        const AcceleratedCoverageFixState coverageFixState = ResolveAcceleratedCoverageFix(
            *polygon,
            AcceleratedCoverageFixConfig{
                CoverageFixEnabled,
                CoverageFixPx,
                CoverageFixApplyRepeat,
                CoverageFixApplyClamp,
                PassiveCoverageFixRepeatPx,
                disablePassiveRepeatCoverageExpand,
            });
        const float effectiveCoverageFixPx = coverageFixState.EffectivePx;
        const float effectiveCoverageDepthBias =
            coverageFixState.ApplyUserFix ? coverageDepthBias : 0.0f;
        const bool applyCoverageFix = coverageFixState.Apply;

        std::array<float, 10> expandedVertexX{};
        std::array<float, 10> expandedVertexY{};
        if (applyCoverageFix)
        {
            std::array<u32, 10> expandedVertexFixedX{};
            std::array<u32, 10> expandedVertexFixedY{};
            ComputeAcceleratedCoverageExpandedVerticesFixed(
                *polygon,
                ScaleFactor,
                useHiresCoordinates,
                maxTargetFixedX,
                maxTargetFixedY,
                effectiveCoverageFixPx,
                expandedVertexFixedX,
                expandedVertexFixedY);
            for (u32 vertexIndex = 0; vertexIndex < polygon->NumVertices; vertexIndex++)
            {
                if (polygon->Vertices[vertexIndex] == nullptr)
                    continue;

                expandedVertexX[vertexIndex] = std::clamp(
                    static_cast<float>(expandedVertexFixedX[vertexIndex]) * (1.0f / 16.0f),
                    0.0f,
                    maxTargetX);
                expandedVertexY[vertexIndex] = std::clamp(
                    static_cast<float>(expandedVertexFixedY[vertexIndex]) * (1.0f / 16.0f),
                    0.0f,
                    maxTargetY);
            }
        }

        bool polygonTextured = polygonTexturedByRegs;
        TexcacheVulkanLoader::TextureHandle textureHandle = 0;
        u32 textureLayer = 0;
        u32* helper = nullptr;
        u32 textureDescriptorIndex = FallbackTextureDescriptorIndex;
        bool textureFallbackUsed = false;
        u32 texWidth = 0;
        u32 texHeight = 0;
        if (polygonTextured)
        {
            Texcache.GetTexture(
                polygon->TexParam,
                polygon->TexPalette,
                textureHandle,
                textureLayer,
                helper
            );

            texWidth = TextureWidth(polygon->TexParam);
            texHeight = TextureHeight(polygon->TexParam);
            if (texWidth == 0 || texHeight == 0)
            {
                polygonTextured = false;
            }
            else
            {
                const TextureLookupKey textureKey{
                    textureHandle,
                };
                auto textureIt = textureLookup.find(textureKey);
                if (textureIt == textureLookup.end())
                {
                    VkDescriptorImageInfo textureDescriptorInfo{};
                    if (Texcache.GetLoader().GetTextureDescriptor(textureHandle, &textureDescriptorInfo)
                        && ActiveTextureDescriptorCount < MaxActiveTextureDescriptors)
                    {
                        textureDescriptorIndex = ActiveTextureDescriptorCount;
                        ActiveTextureDescriptors[textureDescriptorIndex] = textureDescriptorInfo;
                        ActiveTextureDescriptorCount++;
                        textureLookup.emplace(
                            textureKey,
                            TextureFrameData{
                                textureDescriptorIndex,
                            });
                    }
                    else
                    {
                        textureDescriptorIndex = FallbackTextureDescriptorIndex;
                        textureLayer = 0;
                        texWidth = 1;
                        texHeight = 1;
                        textureFallbackUsed = true;
                    }
                }
                else
                {
                    textureDescriptorIndex = textureIt->second.DescriptorIndex;
                }
            }
        }

        const auto makeX = [&](const Vertex* vertex, u32 vertexIndex) -> float {
            if (applyCoverageFix && vertexIndex < polygon->NumVertices)
                return expandedVertexX[vertexIndex];

            return resolveVertexX(vertex);
        };

        const auto makeY = [&](const Vertex* vertex, u32 vertexIndex) -> float {
            if (applyCoverageFix && vertexIndex < polygon->NumVertices)
                return expandedVertexY[vertexIndex];

            return resolveVertexY(vertex);
        };

        u32 debugYTop = targetHeight;
        u32 debugYBottom = 0u;
        bool hasDebugYBounds = false;
        for (u32 vertexIndex = 0; vertexIndex < polygon->NumVertices; vertexIndex++)
        {
            const Vertex* vertex = polygon->Vertices[vertexIndex];
            if (vertex == nullptr)
                continue;

            const float y = makeY(vertex, vertexIndex);
            const float clampedY = std::clamp(y, 0.0f, static_cast<float>(targetHeight));
            const u32 yTopLine = static_cast<u32>(std::floor(clampedY));
            const u32 yBottomLine = std::min<u32>(targetHeight, static_cast<u32>(std::ceil(clampedY)));
            debugYTop = std::min(debugYTop, yTopLine);
            debugYBottom = std::max(debugYBottom, yBottomLine);
            hasDebugYBounds = true;
        }
        if (hasDebugYBounds)
        {
            if (debugYBottom <= debugYTop)
                debugYBottom = std::min<u32>(targetHeight, debugYTop + 1u);

            const u32 debugPackedYBounds = (debugYTop & 0xFFFFu) | ((debugYBottom & 0xFFFFu) << 16u);
            if (!Renderer3DDebugYBoundsEnabled(debugPackedYBounds, targetHeight))
                continue;
        }

        const auto makeColor = [&](const Vertex* vertex) -> u32 {
            // FinalColor is produced in the same expanded range the software/OpenGL paths
            // consume. Quantizing it as if it were already 0..255 makes Vulkan too bright.
            const u32 vr = static_cast<u32>(std::clamp(vertex->FinalColor[0], 0, 511)) >> 3;
            const u32 vg = static_cast<u32>(std::clamp(vertex->FinalColor[1], 0, 511)) >> 3;
            const u32 vb = static_cast<u32>(std::clamp(vertex->FinalColor[2], 0, 511)) >> 3;
            const u32 polyAlpha = alpha5;

            return packRgba8(
                to8From6(vr),
                to8From6(vg),
                to8From6(vb),
                to8From5(std::min<u32>(31, polyAlpha))
            );
        };

        struct TriangleVertexData
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 1.0f;
            float u = 0.0f;
            float v = 0.0f;
            u32 colorRgba8 = 0;
            u32 wRaw = 1;
        };

        const auto makeTriangleVertex = [&](const Vertex* vertex, u32 vertexIndex, float x, float y) -> TriangleVertexData {
            TriangleVertexData triangleVertex{};
            triangleVertex.x = x;
            triangleVertex.y = y;
            triangleVertex.z = static_cast<float>(polygon->FinalZ[vertexIndex]);
            triangleVertex.wRaw = static_cast<u32>(std::max<s32>(1, polygon->FinalW[vertexIndex]));
            triangleVertex.w = static_cast<float>(triangleVertex.wRaw);
            if (applyCoverageFix && effectiveCoverageDepthBias > 0.0f)
                triangleVertex.z = std::max(0.0f, triangleVertex.z - effectiveCoverageDepthBias);

            triangleVertex.u = static_cast<float>(vertex->TexCoords[0]);
            triangleVertex.v = static_cast<float>(vertex->TexCoords[1]);
            triangleVertex.colorRgba8 = makeColor(vertex);
            return triangleVertex;
        };

        const auto makeCenterTriangleVertex = [&]() -> std::optional<TriangleVertexData> {
            if (!BetterPolygons || polygon->NumVertices <= 3u)
                return std::nullopt;

            u32 centerXFixed = 0u;
            u32 centerYFixed = 0u;
            float centerZ = 0.0f;
            float centerReciprocalW = 0.0f;
            float centerR = 0.0f;
            float centerG = 0.0f;
            float centerB = 0.0f;
            float centerU = 0.0f;
            float centerV = 0.0f;
            u32 validVertexCount = 0;

            for (u32 vertexIndex = 0; vertexIndex < polygon->NumVertices; vertexIndex++)
            {
                const Vertex* vertex = polygon->Vertices[vertexIndex];
                if (vertex == nullptr)
                    return std::nullopt;

                centerXFixed += static_cast<u32>(std::max<s32>(0, vertex->HiresPosition[0]));
                centerYFixed += static_cast<u32>(std::max<s32>(0, vertex->HiresPosition[1]));

                const float fw = static_cast<float>(std::max<s32>(1, polygon->FinalW[vertexIndex]))
                    * static_cast<float>(polygon->NumVertices);
                centerReciprocalW += 1.0f / fw;

                if (polygon->WBuffer)
                    centerZ += static_cast<float>(polygon->FinalZ[vertexIndex]) / fw;
                else
                    centerZ += static_cast<float>(polygon->FinalZ[vertexIndex]);

                // Keep the same expanded FinalColor range the software/OpenGL paths use,
                // then quantize once when emitting the synthesized center vertex.
                centerR += static_cast<float>(std::clamp(vertex->FinalColor[0], 0, 511)) / fw;
                centerG += static_cast<float>(std::clamp(vertex->FinalColor[1], 0, 511)) / fw;
                centerB += static_cast<float>(std::clamp(vertex->FinalColor[2], 0, 511)) / fw;
                centerU += static_cast<float>(vertex->TexCoords[0]) / fw;
                centerV += static_cast<float>(vertex->TexCoords[1]) / fw;
                validVertexCount++;
            }

            if (validVertexCount == 0u || centerReciprocalW <= 0.0f)
                return std::nullopt;

            TriangleVertexData centerVertex{};
            centerXFixed /= validVertexCount;
            centerYFixed /= validVertexCount;
            centerVertex.x = std::clamp((static_cast<float>(centerXFixed) * scale) * (1.0f / 16.0f), 0.0f, maxTargetX);
            centerVertex.y = std::clamp((static_cast<float>(centerYFixed) * scale) * (1.0f / 16.0f), 0.0f, maxTargetY);
            centerVertex.w = 1.0f / centerReciprocalW;
            centerVertex.wRaw = std::max<u32>(1u, static_cast<u32>(centerVertex.w));

            if (polygon->WBuffer)
                centerVertex.z = centerZ * centerVertex.w;
            else
                centerVertex.z = centerZ / static_cast<float>(validVertexCount);

            if (applyCoverageFix && effectiveCoverageDepthBias > 0.0f)
                centerVertex.z = std::max(0.0f, centerVertex.z - effectiveCoverageDepthBias);

            centerR *= centerVertex.w;
            centerG *= centerVertex.w;
            centerB *= centerVertex.w;
            centerVertex.u = static_cast<float>(static_cast<s32>(centerU * centerVertex.w));
            centerVertex.v = static_cast<float>(static_cast<s32>(centerV * centerVertex.w));
            const u32 centerR6 = static_cast<u32>(std::clamp(static_cast<int>(centerR), 0, 511)) >> 3;
            const u32 centerG6 = static_cast<u32>(std::clamp(static_cast<int>(centerG), 0, 511)) >> 3;
            const u32 centerB6 = static_cast<u32>(std::clamp(static_cast<int>(centerB), 0, 511)) >> 3;
            centerVertex.colorRgba8 = packRgba8(
                to8From6(std::min<u32>(63u, centerR6)),
                to8From6(std::min<u32>(63u, centerG6)),
                to8From6(std::min<u32>(63u, centerB6)),
                to8From5(std::min<u32>(31u, alpha5)));
            return centerVertex;
        };

        constexpr u32 kTriangleFlagTranslucent = 1u << 0u;
        constexpr u32 kTriangleFlagTextured = 1u << 1u;
        constexpr u32 kTriangleFlagDecal = 1u << 2u;
        constexpr u32 kTriangleFlagCoverageFix = 1u << 3u;
        constexpr u32 kTriangleFlagWBuffer = 1u << 4u;
        constexpr u32 kTriangleFlagShadowMask = 1u << 5u;
        constexpr u32 kTriangleFlagLinear = 1u << 6u;
        constexpr u32 kTriangleFlagBoundaryEdge0 = 1u << 7u;
        constexpr u32 kTriangleFlagBoundaryEdge1 = 1u << 8u;
        constexpr u32 kTriangleFlagBoundaryEdge2 = 1u << 9u;
        constexpr u32 kTriangleFlagFrontFacing = 1u << 10u;
        constexpr u32 kTriangleFlagTopLeftEdge0 = 1u << 11u;
        constexpr u32 kTriangleFlagTopLeftEdge1 = 1u << 12u;
        constexpr u32 kTriangleFlagTopLeftEdge2 = 1u << 13u;
        constexpr u32 kVariantFlagTextured = 1u << 0u;
        constexpr u32 kVariantFlagDecal = 1u << 1u;
        constexpr u32 kVariantFlagModulate = 1u << 2u;
        constexpr u32 kVariantFlagToon = 1u << 3u;
        constexpr u32 kVariantFlagHighlight = 1u << 4u;
        constexpr u32 kVariantFlagShadowMask = 1u << 5u;
        constexpr u32 kVariantFlagWBuffer = 1u << 6u;
        constexpr u32 kVariantFlagTranslucent = 1u << 7u;
        constexpr u32 kVariantFlagCoverageFix = 1u << 8u;

        auto packYBounds = [&](const float* yValues, size_t yValueCount) -> std::optional<u32> {
            u32 polygonYTop = targetHeight;
            u32 polygonYBot = 0;
            bool hasPolygonYBounds = false;
            for (size_t yIndex = 0; yIndex < yValueCount; yIndex++)
            {
                const float clampedY = std::clamp(yValues[yIndex], 0.0f, static_cast<float>(targetHeight));
                const u32 yTopLine = static_cast<u32>(std::floor(clampedY));
                const u32 yBottomLine = std::min<u32>(targetHeight, static_cast<u32>(std::ceil(clampedY)));
                polygonYTop = std::min(polygonYTop, yTopLine);
                polygonYBot = std::max(polygonYBot, yBottomLine);
                hasPolygonYBounds = true;
            }

            if (!hasPolygonYBounds)
                return std::nullopt;

            if (polygonYBot <= polygonYTop)
                polygonYBot = std::min<u32>(targetHeight, polygonYTop + 1u);

            return (polygonYTop & 0xFFFFu) | ((polygonYBot & 0xFFFFu) << 16u);
        };

        auto packPolygonYBounds = [&]() -> std::optional<u32> {
            if (!useHiresCoordinates)
            {
                const s32 rawTop = std::clamp(polygon->YTop, 0, static_cast<s32>(targetHeight));
                const s32 rawBottom = std::clamp(polygon->YBottom, 0, static_cast<s32>(targetHeight));
                u32 polygonYTop = static_cast<u32>(rawTop);
                u32 polygonYBot = static_cast<u32>(rawBottom);
                if (polygonYBot <= polygonYTop)
                    polygonYBot = std::min<u32>(targetHeight, polygonYTop + 1u);
                return (polygonYTop & 0xFFFFu) | ((polygonYBot & 0xFFFFu) << 16u);
            }

            return std::nullopt;
        };

        const bool hasTexture = polygonTextured && texWidth > 0 && texHeight > 0;
        const auto appendTriangle = [&](
            const TriangleVertexData& vertex0,
            const TriangleVertexData& vertex1,
            const TriangleVertexData& vertex2,
            u32 boundaryFlags,
            u32 packedYBounds)
        {
            TriangleGpu triangle{};
            triangle.x0 = vertex0.x;
            triangle.y0 = vertex0.y;
            triangle.z0 = vertex0.z;
            triangle.w0 = vertex0.w;
            triangle.x1 = vertex1.x;
            triangle.y1 = vertex1.y;
            triangle.z1 = vertex1.z;
            triangle.w1 = vertex1.w;
            triangle.x2 = vertex2.x;
            triangle.y2 = vertex2.y;
            triangle.z2 = vertex2.z;
            triangle.w2 = vertex2.w;

            triangle.u0 = vertex0.u;
            triangle.v0 = vertex0.v;
            triangle.u1 = vertex1.u;
            triangle.v1 = vertex1.v;
            triangle.u2 = vertex2.u;
            triangle.v2 = vertex2.v;
            triangle.yBounds = packedYBounds;
            triangle.texLayer = textureLayer;

            triangle.color0Rgba8 = vertex0.colorRgba8;
            triangle.color1Rgba8 = vertex1.colorRgba8;
            triangle.color2Rgba8 = vertex2.colorRgba8;
            const u32 a0 = (triangle.color0Rgba8 >> 24) & 0xFFu;
            const u32 a1 = (triangle.color1Rgba8 >> 24) & 0xFFu;
            const u32 a2 = (triangle.color2Rgba8 >> 24) & 0xFFu;
            const bool alphaTranslucent = (a0 < 255u) || (a1 < 255u) || (a2 < 255u);

            triangle.flags = boundaryFlags;
            if (isTranslucent || alphaTranslucent)
                triangle.flags |= kTriangleFlagTranslucent;
            if (hasTexture)
            {
                triangle.flags |= kTriangleFlagTextured;
                if ((blendMode & 0x1u) != 0u && !textureFallbackUsed)
                    triangle.flags |= kTriangleFlagDecal;
                triangle.texArrayIndex = textureDescriptorIndex;
                triangle.texWidth = texWidth;
                triangle.texHeight = texHeight;
                triangle.texParam = polygon->TexParam;
            }
            if (applyCoverageFix)
                triangle.flags |= kTriangleFlagCoverageFix;
            if (polygon->WBuffer)
                triangle.flags |= kTriangleFlagWBuffer;
            if (polygon->IsShadowMask)
                triangle.flags |= kTriangleFlagShadowMask;
            if (vertex0.wRaw == vertex1.wRaw && vertex1.wRaw == vertex2.wRaw && (vertex0.wRaw & 0x7Fu) == 0u)
                triangle.flags |= kTriangleFlagLinear;
            if (polygon->FacingView)
                triangle.flags |= kTriangleFlagFrontFacing;

            auto isTopLeftEdge = [](const TriangleVertexData& start, const TriangleVertexData& end) -> bool {
                const float deltaY = end.y - start.y;
                if (std::fabs(deltaY) < 0.000001f)
                    return (end.x - start.x) > 0.0f;
                return deltaY < 0.0f;
            };
            const float signedArea = (vertex2.x - vertex0.x) * (vertex1.y - vertex0.y)
                - (vertex2.y - vertex0.y) * (vertex1.x - vertex0.x);
            const bool positiveArea = signedArea > 0.0f;
            if ((triangle.flags & kTriangleFlagBoundaryEdge0) == 0u
                && (positiveArea ? isTopLeftEdge(vertex1, vertex2) : isTopLeftEdge(vertex2, vertex1)))
            {
                triangle.flags |= kTriangleFlagTopLeftEdge0;
            }
            if ((triangle.flags & kTriangleFlagBoundaryEdge1) == 0u
                && (positiveArea ? isTopLeftEdge(vertex2, vertex0) : isTopLeftEdge(vertex0, vertex2)))
            {
                triangle.flags |= kTriangleFlagTopLeftEdge1;
            }
            if ((triangle.flags & kTriangleFlagBoundaryEdge2) == 0u
                && (positiveArea ? isTopLeftEdge(vertex0, vertex1) : isTopLeftEdge(vertex1, vertex0)))
            {
                triangle.flags |= kTriangleFlagTopLeftEdge2;
            }

            triangle.polyAttr = polygonMeta.PolyAttr;
            triangle.variantKey = 0;
            if (hasTexture)
                triangle.variantKey |= kVariantFlagTextured;

            if (blendMode == 2u)
            {
                triangle.variantKey |= highlightEnabled ? kVariantFlagHighlight : kVariantFlagToon;
            }
            else if (hasTexture && (blendMode & 0x1u) != 0u && !textureFallbackUsed)
            {
                triangle.variantKey |= kVariantFlagDecal;
            }
            else
            {
                triangle.variantKey |= kVariantFlagModulate;
            }
            if (polygon->IsShadowMask)
                triangle.variantKey |= kVariantFlagShadowMask;
            if (polygon->WBuffer)
                triangle.variantKey |= kVariantFlagWBuffer;
            if (isTranslucent || alphaTranslucent)
                triangle.variantKey |= kVariantFlagTranslucent;
            if (applyCoverageFix)
                triangle.variantKey |= kVariantFlagCoverageFix;

            Triangles.push_back(triangle);

            auto reciprocalW = [](float w) {
                return 1.0f / std::max(w, 1.0f);
            };

            const auto appendGraphicsVertex = [&](const TriangleVertexData& vertexData) {
                GraphicsVertexGpu graphicsVertex{};
                graphicsVertex.x = vertexData.x;
                graphicsVertex.y = vertexData.y;
                graphicsVertex.z = vertexData.z;
                graphicsVertex.reciprocalW = reciprocalW(vertexData.w);
                graphicsVertex.u = vertexData.u;
                graphicsVertex.v = vertexData.v;
                graphicsVertex.colorRgba8 = vertexData.colorRgba8;
                graphicsVertex.flags = triangle.flags;
                graphicsVertex.texLayer = triangle.texLayer;
                graphicsVertex.texArrayIndex = triangle.texArrayIndex;
                graphicsVertex.texWidth = triangle.texWidth;
                graphicsVertex.texHeight = triangle.texHeight;
                graphicsVertex.texParam = triangle.texParam;
                graphicsVertex.polyAttr = triangle.polyAttr;
                GraphicsVertices.push_back(graphicsVertex);
            };

            appendGraphicsVertex(vertex0);
            appendGraphicsVertex(vertex1);
            appendGraphicsVertex(vertex2);
        };

        const auto enqueueGraphicsDraw = [&](size_t polygonTriangleCount) {
            if (polygonTriangleCount == 0u)
                return;

            GraphicsPolygonDraw draw{};
            draw.firstTriangle = static_cast<u32>(polygonTriangleBase);
            draw.triangleCount = static_cast<u32>(polygonTriangleCount);
            draw.polyAttr = polygonMeta.PolyAttr;
            draw.flags = polygonMeta.Flags;

            const u32 drawIndex = static_cast<u32>(GraphicsPolygons.size());
            GraphicsPolygons.push_back(draw);

            if (!polygonUsesGlTranslucentPass
                && !HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadowMask)
                && !HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadow))
            {
                GraphicsOpaqueDrawIndices.push_back(drawIndex);
            }

            if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagNeedOpaquePass))
                GraphicsNeedOpaqueDrawIndices.push_back(drawIndex);

            if (polygonUsesGlTranslucentPass
                && !HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadowMask)
                && !HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagShadow))
            {
                GraphicsAlphaDrawIndices.push_back(drawIndex);
            }
        };

        if (polygon->Type == 1)
        {
            const AcceleratedLineEndpoints lineEndpoints = ResolveAcceleratedLineEndpoints(*polygon);
            if (lineEndpoints.Count < 2u)
                continue;

            const float lineX0 = makeX(lineEndpoints.Vertices[0], lineEndpoints.Indices[0]);
            const float lineY0 = makeY(lineEndpoints.Vertices[0], lineEndpoints.Indices[0]);
            const float lineX1 = makeX(lineEndpoints.Vertices[1], lineEndpoints.Indices[1]);
            const float lineY1 = makeY(lineEndpoints.Vertices[1], lineEndpoints.Indices[1]);

            const float deltaX = lineX1 - lineX0;
            const float deltaY = lineY1 - lineY0;
            const float lineLengthSquared = (deltaX * deltaX) + (deltaY * deltaY);
            if (lineLengthSquared <= 0.000001f)
                continue;

            const float inverseLineLength = 1.0f / std::sqrt(lineLengthSquared);
            const float halfLineWidth = 0.5f;
            const float perpX = -deltaY * inverseLineLength * halfLineWidth;
            const float perpY = deltaX * inverseLineLength * halfLineWidth;

            const float quadPositionsX[4] = {
                std::clamp(lineX0 + perpX, 0.0f, maxTargetX),
                std::clamp(lineX0 - perpX, 0.0f, maxTargetX),
                std::clamp(lineX1 - perpX, 0.0f, maxTargetX),
                std::clamp(lineX1 + perpX, 0.0f, maxTargetX),
            };
            const float quadPositionsY[4] = {
                std::clamp(lineY0 + perpY, 0.0f, maxTargetY),
                std::clamp(lineY0 - perpY, 0.0f, maxTargetY),
                std::clamp(lineY1 - perpY, 0.0f, maxTargetY),
                std::clamp(lineY1 + perpY, 0.0f, maxTargetY),
            };

            const std::optional<u32> packedLineYBounds = packYBounds(quadPositionsY, 4);
            if (!packedLineYBounds.has_value())
                continue;

            appendTriangle(
                makeTriangleVertex(lineEndpoints.Vertices[0], lineEndpoints.Indices[0], quadPositionsX[0], quadPositionsY[0]),
                makeTriangleVertex(lineEndpoints.Vertices[0], lineEndpoints.Indices[0], quadPositionsX[1], quadPositionsY[1]),
                makeTriangleVertex(lineEndpoints.Vertices[1], lineEndpoints.Indices[1], quadPositionsX[2], quadPositionsY[2]),
                kTriangleFlagBoundaryEdge0 | kTriangleFlagBoundaryEdge2,
                *packedLineYBounds);
            appendTriangle(
                makeTriangleVertex(lineEndpoints.Vertices[0], lineEndpoints.Indices[0], quadPositionsX[0], quadPositionsY[0]),
                makeTriangleVertex(lineEndpoints.Vertices[1], lineEndpoints.Indices[1], quadPositionsX[2], quadPositionsY[2]),
                makeTriangleVertex(lineEndpoints.Vertices[1], lineEndpoints.Indices[1], quadPositionsX[3], quadPositionsY[3]),
                kTriangleFlagBoundaryEdge0 | kTriangleFlagBoundaryEdge1,
                *packedLineYBounds);
            enqueueGraphicsDraw(Triangles.size() - polygonTriangleBase);
            continue;
        }

        const std::optional<u32> packedPolygonYBounds = packPolygonYBounds();
        u32 polygonYTop = targetHeight;
        u32 polygonYBot = 0;
        bool hasPolygonYBounds = false;
        if (packedPolygonYBounds.has_value())
        {
            polygonYTop = *packedPolygonYBounds & 0xFFFFu;
            polygonYBot = (*packedPolygonYBounds >> 16u) & 0xFFFFu;
            hasPolygonYBounds = true;
        }
        else
        {
            for (u32 vertexIndex = 0; vertexIndex < polygon->NumVertices; vertexIndex++)
            {
                const Vertex* vertex = polygon->Vertices[vertexIndex];
                if (vertex == nullptr)
                    continue;

                const float y = makeY(vertex, vertexIndex);
                const float clampedY = std::clamp(y, 0.0f, static_cast<float>(targetHeight));
                const u32 yTopLine = static_cast<u32>(std::floor(clampedY));
                const u32 yBottomLine = std::min<u32>(targetHeight, static_cast<u32>(std::ceil(clampedY)));
                polygonYTop = std::min(polygonYTop, yTopLine);
                polygonYBot = std::max(polygonYBot, yBottomLine);
                hasPolygonYBounds = true;
            }
            if (!hasPolygonYBounds)
                continue;
            if (polygonYBot <= polygonYTop)
                polygonYBot = std::min<u32>(targetHeight, polygonYTop + 1u);
        }
        const u32 packedYBounds = (polygonYTop & 0xFFFFu) | ((polygonYBot & 0xFFFFu) << 16u);

        const std::optional<TriangleVertexData> centerVertex = makeCenterTriangleVertex();
        if (centerVertex.has_value())
        {
            u32 firstOuterVertexIndex = polygon->NumVertices;
            u32 previousOuterVertexIndex = polygon->NumVertices;

            for (u32 vertexIndex = 0; vertexIndex < polygon->NumVertices; vertexIndex++)
            {
                Vertex* outerVertex = polygon->Vertices[vertexIndex];
                if (outerVertex == nullptr)
                {
                    firstOuterVertexIndex = polygon->NumVertices;
                    break;
                }

                if (firstOuterVertexIndex == polygon->NumVertices)
                {
                    firstOuterVertexIndex = vertexIndex;
                    previousOuterVertexIndex = vertexIndex;
                    continue;
                }

                Vertex* previousOuterVertex = polygon->Vertices[previousOuterVertexIndex];
                appendTriangle(
                    *centerVertex,
                    makeTriangleVertex(
                        previousOuterVertex,
                        previousOuterVertexIndex,
                        makeX(previousOuterVertex, previousOuterVertexIndex),
                        makeY(previousOuterVertex, previousOuterVertexIndex)),
                    makeTriangleVertex(
                        outerVertex,
                        vertexIndex,
                        makeX(outerVertex, vertexIndex),
                        makeY(outerVertex, vertexIndex)),
                    kTriangleFlagBoundaryEdge0,
                    packedYBounds);
                previousOuterVertexIndex = vertexIndex;
            }

            if (firstOuterVertexIndex < polygon->NumVertices
                && previousOuterVertexIndex < polygon->NumVertices
                && previousOuterVertexIndex != firstOuterVertexIndex)
            {
                Vertex* lastOuterVertex = polygon->Vertices[previousOuterVertexIndex];
                Vertex* firstOuterVertex = polygon->Vertices[firstOuterVertexIndex];
                appendTriangle(
                    *centerVertex,
                    makeTriangleVertex(
                        lastOuterVertex,
                        previousOuterVertexIndex,
                        makeX(lastOuterVertex, previousOuterVertexIndex),
                        makeY(lastOuterVertex, previousOuterVertexIndex)),
                    makeTriangleVertex(
                        firstOuterVertex,
                        firstOuterVertexIndex,
                        makeX(firstOuterVertex, firstOuterVertexIndex),
                        makeY(firstOuterVertex, firstOuterVertexIndex)),
                    kTriangleFlagBoundaryEdge0,
                    packedYBounds);
                enqueueGraphicsDraw(Triangles.size() - polygonTriangleBase);
                continue;
            }
        }

        for (u32 vertexIdx = 1; vertexIdx + 1 < polygon->NumVertices; vertexIdx++)
        {
            Vertex* v0 = polygon->Vertices[0];
            Vertex* v1 = polygon->Vertices[vertexIdx];
            Vertex* v2 = polygon->Vertices[vertexIdx + 1];
            if (v0 == nullptr || v1 == nullptr || v2 == nullptr)
                continue;

            u32 boundaryFlags = kTriangleFlagBoundaryEdge0;
            if (vertexIdx + 1 == polygon->NumVertices - 1)
                boundaryFlags |= kTriangleFlagBoundaryEdge1;
            if (vertexIdx == 1)
                boundaryFlags |= kTriangleFlagBoundaryEdge2;

            appendTriangle(
                makeTriangleVertex(v0, 0, makeX(v0, 0), makeY(v0, 0)),
                makeTriangleVertex(v1, vertexIdx, makeX(v1, vertexIdx), makeY(v1, vertexIdx)),
                makeTriangleVertex(v2, vertexIdx + 1, makeX(v2, vertexIdx + 1), makeY(v2, vertexIdx + 1)),
                boundaryFlags,
                packedYBounds);
        }

        enqueueGraphicsDraw(Triangles.size() - polygonTriangleBase);
    }

    static bool loggedGraphicsTriangleSummary = false;
    if (!loggedGraphicsTriangleSummary && !Triangles.empty())
    {
        size_t viewportIntersectingCount = 0;
        size_t nonDegenerateCount = 0;
        for (const TriangleGpu& triangle : Triangles)
        {
            const float minX = std::min({triangle.x0, triangle.x1, triangle.x2});
            const float maxX = std::max({triangle.x0, triangle.x1, triangle.x2});
            const float minY = std::min({triangle.y0, triangle.y1, triangle.y2});
            const float maxY = std::max({triangle.y0, triangle.y1, triangle.y2});
            if (maxX > 0.0f && maxY > 0.0f && minX < maxTargetX && minY < maxTargetY)
                viewportIntersectingCount++;

            const float signedArea =
                ((triangle.x1 - triangle.x0) * (triangle.y2 - triangle.y0))
                - ((triangle.y1 - triangle.y0) * (triangle.x2 - triangle.x0));
            if (std::fabs(signedArea) > 0.001f)
                nonDegenerateCount++;
        }

        const TriangleGpu& triangle = Triangles.front();
        const float rawW0 = triangle.w0 > 0.000001f ? (1.0f / triangle.w0) : 0.0f;
        const float rawW1 = triangle.w1 > 0.000001f ? (1.0f / triangle.w1) : 0.0f;
        const float rawW2 = triangle.w2 > 0.000001f ? (1.0f / triangle.w2) : 0.0f;
        Log(
            LogLevel::Warn,
            "VulkanGraphics[Triangles]: scale=%d count=%zu viewportIntersect=%zu nonDegenerate=%zu textures=%u first tri pos=(%.3f,%.3f,%.3f,w=%.3f)->(%.3f,%.3f,%.3f,w=%.3f)->(%.3f,%.3f,%.3f,w=%.3f) flags=%#x texDesc=%u texLayer=%u texSize=%ux%u texParam=%#x polyAttr=%#x yBounds=%#x",
            ScaleFactor,
            Triangles.size(),
            viewportIntersectingCount,
            nonDegenerateCount,
            ActiveTextureDescriptorCount,
            triangle.x0, triangle.y0, triangle.z0, rawW0,
            triangle.x1, triangle.y1, triangle.z1, rawW1,
            triangle.x2, triangle.y2, triangle.z2, rawW2,
            triangle.flags,
            triangle.texArrayIndex,
            triangle.texLayer,
            triangle.texWidth,
            triangle.texHeight,
            triangle.texParam,
            triangle.polyAttr,
            triangle.yBounds);
        if (!GraphicsPolygons.empty())
        {
            const GraphicsPolygonDraw& draw = GraphicsPolygons.front();
            Log(
                LogLevel::Warn,
                "VulkanGraphics[Draws]: polygons=%zu first firstTriangle=%u triangleCount=%u polyAttr=%#x flags=%#x dispCnt=%#x alphaRef=%u",
                GraphicsPolygons.size(),
                draw.firstTriangle,
                draw.triangleCount,
                draw.polyAttr,
                draw.flags,
                gpu.GPU3D.RenderDispCnt,
                gpu.GPU3D.RenderAlphaRef);
        }
        loggedGraphicsTriangleSummary = true;
    }
}

bool VulkanRenderer3D::copyReadyCaptureLineToLineCache()
{
    // compute paths write DS-packed 6A5 values directly, but graphics_hw
    // The ready capture source may belong to the threaded render context that
    // produced the frame. Using the global CaptureLineMapped pointer in
    // graphics_hw can read a different context's stale/zero buffer and causes
    // top/bottom flicker even though the exact export finished correctly.
    const u32* captureSource = ReadyCaptureLineData;

    if (captureSource == nullptr)
    {
        resetCaptureLineState();
        return false;
    }
    if (ActiveBackendMode == BackendMode::GraphicsHardware
        && HasCurrentCaptureScreenSwapHint
        && ReadyCaptureLineScreenSwap != CurrentCaptureScreenSwapHint)
    {
        CaptureLineReady = false;
        ReadyCaptureLineData = nullptr;
        ReadyCaptureLineBufferSlot = -1;
        ReadyCaptureLineScreenSwap = false;
        return false;
    }

    if (CaptureLineDataIsRgba8)
    {
        const size_t pixelCount = LineCache.size();
        for (size_t i = 0; i < pixelCount; i++)
        {
            const u32 sourcePixel = captureSource[i];
            const u32 r = sourcePixel & 0xFFu;
            const u32 g = (sourcePixel >> 8u) & 0xFFu;
            const u32 b = (sourcePixel >> 16u) & 0xFFu;
            const u32 a = (sourcePixel >> 24u) & 0xFFu;

            LineCache[i] =
                (r >> 2u)
                | ((g >> 2u) << 8u)
                | ((b >> 2u) << 16u)
                | ((a >> 3u) << 24u);
        }
    }
    else
    {
        std::memcpy(LineCache.data(), captureSource, LineCache.size() * sizeof(u32));
    }

    ExactCaptureLineCachePrepared = ActiveBackendMode == BackendMode::GraphicsHardware;
    ExactCaptureLineCacheFresh = ActiveBackendMode == BackendMode::GraphicsHardware;
    if (ActiveBackendMode == BackendMode::GraphicsHardware)
    {
        LastValidExactCaptureLineCache = LineCache;
        HasLastValidExactCapture = true;
        LastValidExactCaptureScreenSwap = ReadyCaptureLineScreenSwap;
    }
    CaptureLineReady = false;
    ReadyCaptureLineData = nullptr;
    ReadyCaptureLineBufferSlot = -1;
    CaptureLineDataIsRgba8 = false;

    ActiveCapturePathMode = CapturePathMode::CaptureLineExport;
    CapturePathModeCounts[static_cast<size_t>(CapturePathMode::CaptureLineExport)]++;
    clearRawReadbackState();
    return true;
}

bool VulkanRenderer3D::restoreLastValidExactCaptureToLineCache()
{
    if (!HasLastValidExactCapture)
        return false;
    if (HasCurrentCaptureScreenSwapHint
        && LastValidExactCaptureScreenSwap != CurrentCaptureScreenSwapHint)
    {
        return false;
    }

    LineCache = LastValidExactCaptureLineCache;
    ExactCaptureLineCachePrepared = true;
    ExactCaptureLineCacheFresh = false;
    return true;
}

void VulkanRenderer3D::convertReadbackToLineCache()
{
    if (!HasCpuFrame || RawReadbackWidth == 0 || RawReadbackHeight == 0 || RawReadbackRgba.empty())
    {
        clearLineCache();
        return;
    }

    const bool readbackIsNativeDs = RawReadbackWidth == 256u && RawReadbackHeight == 192u;

    if (readbackIsNativeDs)
    {
        const size_t pixelCount = 256u * 192u;
        for (size_t i = 0; i < pixelCount; i++)
        {
            const u32 sourcePixel = RawReadbackRgba[i];

            const u32 r = sourcePixel & 0xFF;
            const u32 g = (sourcePixel >> 8) & 0xFF;
            const u32 b = (sourcePixel >> 16) & 0xFF;
            const u32 a = (sourcePixel >> 24) & 0xFF;

            LineCache[i] =
                (r >> 2)
                | ((g >> 2) << 8)
                | ((b >> 2) << 16)
                | ((a >> 3) << 24);
        }

        ExactCaptureLineCachePrepared = false;
        return;
    }

    const u32 sampleScale = static_cast<u32>(std::max(1, ScaleFactor));
    const u32 sampleOffset = sampleScale > 1u ? (sampleScale / 2u) : 0u;
    for (u32 y = 0; y < 192; y++)
    {
        const u32 sourceY = std::min(RawReadbackHeight - 1, y * sampleScale + sampleOffset);
        for (u32 x = 0; x < 256; x++)
        {
            const u32 sourceX = std::min(RawReadbackWidth - 1, x * sampleScale + sampleOffset);
            const u32 sourcePixel = RawReadbackRgba[static_cast<size_t>(sourceY) * static_cast<size_t>(RawReadbackWidth) + sourceX];

            const u32 r = sourcePixel & 0xFF;
            const u32 g = (sourcePixel >> 8) & 0xFF;
            const u32 b = (sourcePixel >> 16) & 0xFF;
            const u32 a = (sourcePixel >> 24) & 0xFF;

            LineCache[static_cast<size_t>(y) * 256u + x] =
                (r >> 2)
                | ((g >> 2) << 8)
                | ((b >> 2) << 16)
                | ((a >> 3) << 24);
        }
    }
    ExactCaptureLineCachePrepared = false;
}

void VulkanRenderer3D::fillLineCacheWithCaptureFallbackColor()
{
    std::fill(LineCache.begin(), LineCache.end(), ExactCaptureFallbackPackedColor);
    ExactCaptureLineCachePrepared = true;
    ExactCaptureLineCacheFresh = false;
}

u32 VulkanRenderer3D::buildClearColorRgba8(const melonDS::GPU& gpu) const
{
    const u32 clearAttr1 = gpu.GPU3D.RenderClearAttr1;

    u32 r = (clearAttr1 << 1) & 0x3E;
    if (r)
        r++;

    u32 g = (clearAttr1 >> 4) & 0x3E;
    if (g)
        g++;

    u32 b = (clearAttr1 >> 9) & 0x3E;
    if (b)
        b++;

    const u32 a = (clearAttr1 >> 16) & 0x1F;

    const u32 r8 = (r << 2) | (r >> 4);
    const u32 g8 = (g << 2) | (g >> 4);
    const u32 b8 = (b << 2) | (b >> 4);
    const u32 a8 = (a << 3) | (a >> 2);

    return r8 | (g8 << 8) | (b8 << 16) | (a8 << 24);
}

void VulkanRenderer3D::clearLineCache()
{
    std::fill(LineCache.begin(), LineCache.end(), 0);
    ExactCaptureLineCachePrepared = false;
    ExactCaptureLineCacheFresh = false;
}

void VulkanRenderer3D::WarmTextureCache(melonDS::GPU& gpu)
{
    const bool enableTextureMaps = gpu.GPU3D.RenderDispCnt & (1 << 0);
    if (!enableTextureMaps)
        return;

    TexcacheVulkanLoader::TextureHandle textureHandle = 0;
    u32 textureLayer = 0;
    u32* helper = nullptr;

    for (u32 i = 0; i < gpu.GPU3D.RenderNumPolygons; i++)
    {
        Polygon* polygon = gpu.GPU3D.RenderPolygonRAM[i];
        if (polygon == nullptr)
            continue;

        if (((polygon->TexParam >> 26) & 0x7) == 0)
            continue;

        Texcache.GetTexture(
            polygon->TexParam,
            polygon->TexPalette,
            textureHandle,
            textureLayer,
            helper
        );
    }
}

}
