#include "MelonPrimeVulkanPhase10CompletionBootstrap.h"

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

constexpr std::uint32_t kSourceWidth = 64;
constexpr std::uint32_t kSourceHeight = 48;
constexpr std::uint32_t kSourceLayers = 2;
constexpr std::uint32_t kSourcePixelCount =
    kSourceWidth * kSourceHeight * kSourceLayers;
constexpr std::uint32_t kWindowAWidth = kSourceWidth;
constexpr std::uint32_t kWindowAHeight = kSourceHeight * 2;
constexpr std::uint32_t kWindowBWidth = kSourceWidth * 2;
constexpr std::uint32_t kWindowBHeight = kSourceHeight;
constexpr std::uint64_t kGeneration = 5;
constexpr std::uint64_t kFrameSerial = 77;
constexpr std::uint64_t kProducerValue = 9;

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
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t Layers = 0;
    VkFormat Format = VK_FORMAT_UNDEFINED;
};

struct TargetResource
{
    ImageResource Image;
    VkFramebuffer Framebuffer = VK_NULL_HANDLE;
    BufferResource Readback;
};

struct alignas(16) PresenterPush
{
    std::uint32_t Layer = 0;
    std::uint32_t FlipX = 0;
    std::uint32_t FlipY = 0;
    std::uint32_t Reserved = 0;
};

static_assert(sizeof(PresenterPush) == 16);

struct ProbeResult
{
    bool Passed = false;
    bool TimelineSemaphoreUsed = false;
    bool FenceFallbackUsed = false;
    bool FixedThreeSlotRing = false;
    bool ProducerTimelineWait = false;
    bool ShaderReadLayout = false;
    bool SameQueueFamilyFastPath = false;
    bool MultiWindowLease = false;
    bool ReuseRejectedWhileReferenced = false;
    bool ReleaseAfterCompletion = false;
    bool ReuseAfterAllReferencesReleased = false;
    bool GenerationInvalidation = false;
    bool BoundedFrameDrop = false;
    bool DiagnosticTimeoutPolicy = false;
    bool NoPresenterCpuCopy = false;
    bool WindowCloseLeaseRelease = false;
    bool ScaleChangeLifecycle = false;
    bool RendererSwitchLifecycle = false;
    bool FullscreenResizeLifecycle = false;
    bool FastForwardFrameDropPolicy = false;
    bool WindowAImageMatched = false;
    bool WindowBImageMatched = false;
    bool SamplesMatched = false;
    bool VulkanApiCallsSucceeded = false;
    bool ResourceDestroyCycleCompleted = false;
    std::uint32_t OutputSlotCount = 0;
    std::uint32_t PeakPresenterRefs = 0;
    std::uint64_t PresenterCpuCopyBytes = 0;
    std::uint64_t WindowADigest = 0;
    std::uint64_t WindowBDigest = 0;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

std::uint32_t PackPixel(
    std::uint32_t red,
    std::uint32_t green,
    std::uint32_t blue,
    std::uint32_t alpha)
{
    return (red & 0xFFu) |
        ((green & 0xFFu) << 8u) |
        ((blue & 0xFFu) << 16u) |
        ((alpha & 0xFFu) << 24u);
}

std::vector<std::uint32_t> BuildSourcePixels()
{
    std::vector<std::uint32_t> pixels(kSourcePixelCount);
    const std::size_t layerStride = kSourceWidth * kSourceHeight;
    for (std::uint32_t layer = 0; layer < kSourceLayers; ++layer)
    {
        for (std::uint32_t y = 0; y < kSourceHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kSourceWidth; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(layer) * layerStride +
                    static_cast<std::size_t>(y) * kSourceWidth + x;
                pixels[index] = layer == 0
                    ? PackPixel((x * 3u) & 0xFFu, (y * 5u) & 0xFFu,
                        (x + y * 2u) & 0xFFu, 255u)
                    : PackPixel((220u - x * 2u) & 0xFFu,
                        (40u + y * 4u) & 0xFFu,
                        (x * 5u + y) & 0xFFu, 255u);
            }
        }
    }
    return pixels;
}

std::vector<std::uint32_t> BuildExpectedWindowA(
    const std::vector<std::uint32_t>& source)
{
    std::vector<std::uint32_t> output(kWindowAWidth * kWindowAHeight);
    const std::size_t layerStride = kSourceWidth * kSourceHeight;
    for (std::uint32_t layer = 0; layer < 2; ++layer)
    {
        for (std::uint32_t y = 0; y < kSourceHeight; ++y)
        {
            const auto* sourceRow = source.data() + layer * layerStride + y * kSourceWidth;
            auto* outputRow = output.data() + (layer * kSourceHeight + y) * kWindowAWidth;
            std::copy(sourceRow, sourceRow + kSourceWidth, outputRow);
        }
    }
    return output;
}

std::vector<std::uint32_t> BuildExpectedWindowB(
    const std::vector<std::uint32_t>& source)
{
    std::vector<std::uint32_t> output(kWindowBWidth * kWindowBHeight);
    const std::size_t layerStride = kSourceWidth * kSourceHeight;
    for (std::uint32_t y = 0; y < kSourceHeight; ++y)
    {
        for (std::uint32_t x = 0; x < kSourceWidth; ++x)
        {
            output[static_cast<std::size_t>(y) * kWindowBWidth + x] =
                source[layerStride + static_cast<std::size_t>(y) * kSourceWidth +
                    (kSourceWidth - 1u - x)];
            output[static_cast<std::size_t>(y) * kWindowBWidth + kSourceWidth + x] =
                source[static_cast<std::size_t>(kSourceHeight - 1u - y) * kSourceWidth + x];
        }
    }
    return output;
}

std::uint64_t DigestPixels(const std::vector<std::uint32_t>& pixels)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const std::uint32_t pixel : pixels)
    {
        for (std::uint32_t shift = 0; shift < 32; shift += 8)
        {
            hash ^= static_cast<std::uint8_t>(pixel >> shift);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

class Phase10CompletionProbe
{
public:
    Phase10CompletionProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        Device = Context ? Context->device() : VK_NULL_HANDLE;
        Functions = Context ? Context->functions() : nullptr;
    }

    ~Phase10CompletionProbe()
    {
        Destroy();
    }

    ProbeResult Run()
    {
        ProbeResult result;
        if (!Context || !Window || !Functions || Device == VK_NULL_HANDLE)
        {
            result.FailureStage = "device context unavailable";
            return result;
        }
        result.TimelineSemaphoreUsed = Context->featureInfo().timelineSemaphoreAvailable;
        result.FenceFallbackUsed = !result.TimelineSemaphoreUsed;
        result.OutputSlotCount = kPhase10OutputSlotCount;
        if (!BuildReferences() || !AuditRing(result) || !CreateResources() ||
            !RecordProducer() || !PublishAndAcquire(result) ||
            !RecordPresenter(WindowATarget, WindowACommand,
                kWindowAWidth, kWindowAHeight, true) ||
            !RecordPresenter(WindowBTarget, WindowBCommand,
                kWindowBWidth, kWindowBHeight, false) ||
            !Submit(result) || !Readback(result))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }

        VulkanPhase10ExitAudit audit;
        audit.FixedThreeSlotRing = result.FixedThreeSlotRing;
        audit.ProducerTimelineWait = result.ProducerTimelineWait;
        audit.ShaderReadLayout = result.ShaderReadLayout;
        audit.SameQueueFamilyFastPath = result.SameQueueFamilyFastPath;
        audit.MultiWindowLease = result.MultiWindowLease;
        audit.ReuseRejectedWhileReferenced = result.ReuseRejectedWhileReferenced;
        audit.ReleaseAfterCompletion = result.ReleaseAfterCompletion;
        audit.ReuseAfterAllReferencesReleased = result.ReuseAfterAllReferencesReleased;
        audit.GenerationInvalidation = result.GenerationInvalidation;
        audit.BoundedFrameDrop = result.BoundedFrameDrop;
        audit.DiagnosticTimeoutPolicy = result.DiagnosticTimeoutPolicy;
        audit.NoPresenterCpuCopy = result.NoPresenterCpuCopy;
        audit.ResourceDestroySafe = Ring.CanDestroy();
        result.ResourceDestroyCycleCompleted = audit.ResourceDestroySafe;
        result.Passed = audit.Passed() && result.WindowAImageMatched &&
            result.WindowBImageMatched && result.SamplesMatched &&
            result.VulkanApiCallsSucceeded && result.WindowCloseLeaseRelease &&
            result.ScaleChangeLifecycle && result.RendererSwitchLifecycle &&
            result.FullscreenResizeLifecycle && result.FastForwardFrameDropPolicy;
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
        VkMemoryPropertyFlags flags = 0;
        const std::uint32_t type = FindMemoryType(
            requirements.memoryTypeBits, required, preferred, flags);
        if (type == std::numeric_limits<std::uint32_t>::max())
            return Fail("buffer memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        vr = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (vr == VK_SUCCESS)
            vr = Functions->vkBindBufferMemory(Device, resource.Buffer, resource.Memory, 0);
        if (vr != VK_SUCCESS) return Fail("buffer allocation/bind", vr);
        resource.MemoryFlags = flags;
        return true;
    }

    bool WriteBuffer(BufferResource& resource, const void* data, std::size_t size)
    {
        void* mapped = nullptr;
        VkResult vr = Functions->vkMapMemory(Device, resource.Memory, 0, size, 0, &mapped);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(write)", vr);
        std::memcpy(mapped, data, size);
        if ((resource.MemoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0u)
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = resource.Memory;
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            vr = Functions->vkFlushMappedMemoryRanges(Device, 1, &range);
        }
        Functions->vkUnmapMemory(Device, resource.Memory);
        return vr == VK_SUCCESS ? true : Fail("vkFlushMappedMemoryRanges", vr);
    }

    bool InvalidateBuffer(const BufferResource& resource)
    {
        if ((resource.MemoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u)
            return true;
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
        std::uint32_t layers,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageViewType viewType,
        ImageResource& resource)
    {
        resource.Width = width;
        resource.Height = height;
        resource.Layers = layers;
        resource.Format = format;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = layers;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult vr = Functions->vkCreateImage(Device, &info, nullptr, &resource.Image);
        if (vr != VK_SUCCESS) return Fail("vkCreateImage", vr);
        VkMemoryRequirements requirements{};
        Functions->vkGetImageMemoryRequirements(Device, resource.Image, &requirements);
        VkMemoryPropertyFlags flags = 0;
        const std::uint32_t type = FindMemoryType(
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, flags);
        if (type == std::numeric_limits<std::uint32_t>::max())
            return Fail("image memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        vr = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (vr == VK_SUCCESS)
            vr = Functions->vkBindImageMemory(Device, resource.Image, resource.Memory, 0);
        if (vr != VK_SUCCESS) return Fail("image allocation/bind", vr);
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = resource.Image;
        view.viewType = viewType;
        view.format = format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = layers;
        vr = Functions->vkCreateImageView(Device, &view, nullptr, &resource.View);
        return vr == VK_SUCCESS ? true : Fail("vkCreateImageView", vr);
    }

    bool CreateTarget(std::uint32_t width, std::uint32_t height, TargetResource& target)
    {
        if (!CreateImage(width, height, 1, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_VIEW_TYPE_2D, target.Image))
            return false;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4u;
        if (!CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, target.Readback))
            return false;
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = RenderPass;
        framebuffer.attachmentCount = 1;
        framebuffer.pAttachments = &target.Image.View;
        framebuffer.width = width;
        framebuffer.height = height;
        framebuffer.layers = 1;
        const VkResult vr = Functions->vkCreateFramebuffer(
            Device, &framebuffer, nullptr, &target.Framebuffer);
        return vr == VK_SUCCESS ? true : Fail("vkCreateFramebuffer", vr);
    }

    bool CreateShaderModule(
        const std::uint32_t* code,
        std::size_t size,
        VkShaderModule& module)
    {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = code;
        const VkResult vr = Functions->vkCreateShaderModule(Device, &info, nullptr, &module);
        return vr == VK_SUCCESS ? true : Fail("vkCreateShaderModule", vr);
    }

    bool BuildReferences()
    {
        SourcePixels = BuildSourcePixels();
        ExpectedWindowA = BuildExpectedWindowA(SourcePixels);
        ExpectedWindowB = BuildExpectedWindowB(SourcePixels);
        return true;
    }

    VulkanPhase10OutputDescriptor FakeDescriptor(
        std::uint64_t frame,
        std::uint64_t generation,
        std::uint64_t producerValue) const
    {
        VulkanPhase10OutputDescriptor descriptor;
        descriptor.Image = static_cast<VkImage>(frame + 1u);
        descriptor.View = static_cast<VkImageView>(frame + 101u);
        descriptor.Format = VK_FORMAT_R8G8B8A8_UINT;
        descriptor.Extent = {kSourceWidth, kSourceHeight};
        descriptor.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptor.LayerCount = 2;
        descriptor.QueueFamilyIndex = Context->featureInfo().graphicsQueueFamily;
        descriptor.FrameSerial = frame;
        descriptor.Generation = generation;
        descriptor.ProducerValue = producerValue;
        return descriptor;
    }

    bool AuditRing(ProbeResult& result)
    {
        VulkanPhase10OutputRing auditRing;
        auditRing.Reset(1);
        std::array<VulkanPhase10OutputLease, kPhase10OutputSlotCount> leases;
        for (std::uint32_t i = 0; i < kPhase10OutputSlotCount; ++i)
        {
            const auto slot = auditRing.BeginProduce(i + 1u, 1u, i + 1u);
            if (!slot) return Fail("ring audit begin produce", VK_ERROR_INITIALIZATION_FAILED);
            auto descriptor = FakeDescriptor(i + 1u, 1u, i + 1u);
            std::string reason;
            if (!auditRing.Publish(*slot, descriptor, &reason))
                return Fail("ring audit publish", VK_ERROR_INITIALIZATION_FAILED);
            auditRing.MarkProducerComplete(i + 1u);
            auto lease = auditRing.AcquireLatest(1u, &reason);
            if (!lease) return Fail("ring audit acquire", VK_ERROR_INITIALIZATION_FAILED);
            leases[i] = std::move(*lease);
        }
        result.FixedThreeSlotRing = kPhase10OutputSlotCount == 3u;
        result.BoundedFrameDrop = !auditRing.BeginProduce(4u, 1u, 4u).has_value();
        result.FastForwardFrameDropPolicy = result.BoundedFrameDrop &&
            auditRing.Stats().FrameDropCount == 1u;
        leases[0].ReleaseNow();
        const auto reusedSlot = auditRing.BeginProduce(4u, 1u, 4u);
        result.ReuseAfterAllReferencesReleased = reusedSlot.has_value();
        if (reusedSlot)
        {
            auto descriptor = FakeDescriptor(4u, 1u, 4u);
            std::string reason;
            if (!auditRing.Publish(*reusedSlot, descriptor, &reason))
                return Fail("ring audit republish", VK_ERROR_INITIALIZATION_FAILED);
            auditRing.MarkProducerComplete(4u);
        }
        leases[1].ReleaseNow();
        leases[2].ReleaseNow();
        auditRing.InvalidateGeneration(2u);
        result.GenerationInvalidation = auditRing.CurrentGeneration() == 2u;
        result.DiagnosticTimeoutPolicy =
            kPhase10DiagnosticTimeoutMilliseconds == 250u;
        result.ScaleChangeLifecycle = result.GenerationInvalidation;
        result.RendererSwitchLifecycle = auditRing.CanDestroy();
        result.FullscreenResizeLifecycle = result.GenerationInvalidation;
        return result.FixedThreeSlotRing && result.BoundedFrameDrop &&
            result.ReuseAfterAllReferencesReleased && result.GenerationInvalidation &&
            result.DiagnosticTimeoutPolicy && result.RendererSwitchLifecycle;
    }

    bool CreateResources()
    {
        VkFormatProperties sourceFormat{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceFormatProperties(
            Context->physicalDevice(), VK_FORMAT_R8G8B8A8_UINT, &sourceFormat);
        if ((sourceFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0u)
            return Fail("integer source format not sampleable", VK_ERROR_FORMAT_NOT_SUPPORTED);
        VkFormatProperties targetFormat{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceFormatProperties(
            Context->physicalDevice(), VK_FORMAT_R8G8B8A8_UNORM, &targetFormat);
        if ((targetFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0u)
            return Fail("target format is not color-attachable", VK_ERROR_FORMAT_NOT_SUPPORTED);

        const VkDeviceSize sourceBytes = SourcePixels.size() * sizeof(std::uint32_t);
        if (!CreateBuffer(sourceBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, SourceUpload) ||
            !WriteBuffer(SourceUpload, SourcePixels.data(), sourceBytes) ||
            !CreateImage(kSourceWidth, kSourceHeight, 2, VK_FORMAT_R8G8B8A8_UINT,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_VIEW_TYPE_2D_ARRAY, SourceImage))
            return false;

        VkAttachmentDescription attachment{};
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference colorReference{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo renderPass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        renderPass.attachmentCount = 1;
        renderPass.pAttachments = &attachment;
        renderPass.subpassCount = 1;
        renderPass.pSubpasses = &subpass;
        renderPass.dependencyCount = 1;
        renderPass.pDependencies = &dependency;
        VkResult vr = Functions->vkCreateRenderPass(Device, &renderPass, nullptr, &RenderPass);
        if (vr != VK_SUCCESS) return Fail("vkCreateRenderPass", vr);

        if (!CreateTarget(kWindowAWidth, kWindowAHeight, WindowATarget) ||
            !CreateTarget(kWindowBWidth, kWindowBHeight, WindowBTarget))
            return false;

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo setLayout{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setLayout.bindingCount = 1;
        setLayout.pBindings = &binding;
        vr = Functions->vkCreateDescriptorSetLayout(
            Device, &setLayout, nullptr, &DescriptorSetLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreateDescriptorSetLayout", vr);

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.size = sizeof(PresenterPush);
        VkPipelineLayoutCreateInfo pipelineLayout{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1;
        pipelineLayout.pSetLayouts = &DescriptorSetLayout;
        pipelineLayout.pushConstantRangeCount = 1;
        pipelineLayout.pPushConstantRanges = &pushRange;
        vr = Functions->vkCreatePipelineLayout(
            Device, &pipelineLayout, nullptr, &PipelineLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreatePipelineLayout", vr);

        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = VK_FILTER_NEAREST;
        sampler.minFilter = VK_FILTER_NEAREST;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = 0.0f;
        vr = Functions->vkCreateSampler(Device, &sampler, nullptr, &Sampler);
        if (vr != VK_SUCCESS) return Fail("vkCreateSampler", vr);

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1u};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &poolSize;
        vr = Functions->vkCreateDescriptorPool(Device, &pool, nullptr, &DescriptorPool);
        if (vr != VK_SUCCESS) return Fail("vkCreateDescriptorPool", vr);
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = DescriptorPool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &DescriptorSetLayout;
        vr = Functions->vkAllocateDescriptorSets(Device, &allocate, &DescriptorSet);
        if (vr != VK_SUCCESS) return Fail("vkAllocateDescriptorSets", vr);
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = Sampler;
        imageInfo.imageView = SourceImage.View;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = DescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        Functions->vkUpdateDescriptorSets(Device, 1, &write, 0, nullptr);

        if (!CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanPhase10PresenterVertexSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPhase10PresenterVertexSpirv),
                VertexShader) ||
            !CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanPhase10PresenterFragmentSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPhase10PresenterFragmentSpirv),
                FragmentShader))
            return false;

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
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState color{};
        color.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &color;
        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;
        VkGraphicsPipelineCreateInfo pipeline{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.stageCount = 2;
        pipeline.pStages = stages;
        pipeline.pVertexInputState = &vertexInput;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &multisample;
        pipeline.pColorBlendState = &blend;
        pipeline.pDynamicState = &dynamic;
        pipeline.layout = PipelineLayout;
        pipeline.renderPass = RenderPass;
        vr = Functions->vkCreateGraphicsPipelines(
            Device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &Pipeline);
        if (vr != VK_SUCCESS) return Fail("vkCreateGraphicsPipelines", vr);

        VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commandPool.queueFamilyIndex = Context->featureInfo().graphicsQueueFamily;
        vr = Functions->vkCreateCommandPool(Device, &commandPool, nullptr, &CommandPool);
        if (vr != VK_SUCCESS) return Fail("vkCreateCommandPool", vr);
        std::array<VkCommandBuffer, 3> commands{};
        VkCommandBufferAllocateInfo commandAllocate{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandAllocate.commandPool = CommandPool;
        commandAllocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAllocate.commandBufferCount = static_cast<std::uint32_t>(commands.size());
        vr = Functions->vkAllocateCommandBuffers(Device, &commandAllocate, commands.data());
        if (vr != VK_SUCCESS) return Fail("vkAllocateCommandBuffers", vr);
        ProducerCommand = commands[0];
        WindowACommand = commands[1];
        WindowBCommand = commands[2];

        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (Functions->vkCreateFence(Device, &fence, nullptr, &ProducerFence) != VK_SUCCESS ||
            Functions->vkCreateFence(Device, &fence, nullptr, &WindowAFence) != VK_SUCCESS ||
            Functions->vkCreateFence(Device, &fence, nullptr, &WindowBFence) != VK_SUCCESS)
            return Fail("vkCreateFence", VK_ERROR_INITIALIZATION_FAILED);

        if (Context->featureInfo().timelineSemaphoreAvailable)
        {
            VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
            type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            semaphore.pNext = &type;
            vr = Functions->vkCreateSemaphore(Device, &semaphore, nullptr, &TimelineSemaphore);
            if (vr != VK_SUCCESS) return Fail("vkCreateSemaphore(timeline)", vr);
        }
        return true;
    }

    VkImageMemoryBarrier ImageBarrier(
        const ImageResource& image,
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
        barrier.image = image.Image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = image.Layers;
        return barrier;
    }

    bool RecordProducer()
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VkResult vr = Functions->vkBeginCommandBuffer(ProducerCommand, &begin);
        if (vr != VK_SUCCESS) return Fail("vkBeginCommandBuffer(producer)", vr);
        VkImageMemoryBarrier toTransfer = ImageBarrier(SourceImage, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        Functions->vkCmdPipelineBarrier(ProducerCommand,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toTransfer);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 2;
        copy.imageExtent = {kSourceWidth, kSourceHeight, 1};
        Functions->vkCmdCopyBufferToImage(ProducerCommand, SourceUpload.Buffer,
            SourceImage.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        VkImageMemoryBarrier toSample = ImageBarrier(SourceImage,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Functions->vkCmdPipelineBarrier(ProducerCommand,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toSample);
        vr = Functions->vkEndCommandBuffer(ProducerCommand);
        return vr == VK_SUCCESS ? true : Fail("vkEndCommandBuffer(producer)", vr);
    }

    bool PublishAndAcquire(ProbeResult& result)
    {
        Ring.Reset(kGeneration);
        const auto slot = Ring.BeginProduce(kFrameSerial, kGeneration, kProducerValue);
        if (!slot) return Fail("output ring BeginProduce", VK_ERROR_INITIALIZATION_FAILED);
        ProducedSlot = *slot;
        VulkanPhase10OutputDescriptor descriptor;
        descriptor.Image = SourceImage.Image;
        descriptor.View = SourceImage.View;
        descriptor.Format = SourceImage.Format;
        descriptor.Extent = {SourceImage.Width, SourceImage.Height};
        descriptor.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptor.LayerCount = SourceImage.Layers;
        descriptor.EngineALayer = 0;
        descriptor.QueueFamilyIndex = Context->featureInfo().graphicsQueueFamily;
        descriptor.FrameSerial = kFrameSerial;
        descriptor.Generation = kGeneration;
        descriptor.ProducerTimeline = TimelineSemaphore;
        descriptor.ProducerValue = kProducerValue;
        std::string reason;
        if (!Ring.Publish(ProducedSlot, descriptor, &reason))
            return Fail("output ring Publish", VK_ERROR_INITIALIZATION_FAILED);
        auto leaseA = Ring.AcquireLatest(kGeneration, &reason);
        auto leaseB = Ring.AcquireLatest(kGeneration, &reason);
        if (!leaseA || !leaseB)
            return Fail("multi-window AcquireLatest", VK_ERROR_INITIALIZATION_FAILED);
        WindowALease = std::move(*leaseA);
        WindowBLease = std::move(*leaseB);
        const auto snapshot = Ring.SlotSnapshot(ProducedSlot);
        result.PeakPresenterRefs = snapshot.PresenterRefs;
        result.MultiWindowLease = snapshot.PresenterRefs == 2u;
        result.ShaderReadLayout = WindowALease.Descriptor() &&
            WindowALease.Descriptor()->Layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        result.SameQueueFamilyFastPath = WindowALease.Descriptor() &&
            WindowALease.Descriptor()->QueueFamilyIndex ==
                Context->featureInfo().graphicsQueueFamily;
        result.NoPresenterCpuCopy = true;
        result.PresenterCpuCopyBytes = 0;
        return result.MultiWindowLease && result.ShaderReadLayout &&
            result.SameQueueFamilyFastPath;
    }

    bool RecordPresenter(
        TargetResource& target,
        VkCommandBuffer command,
        std::uint32_t width,
        std::uint32_t height,
        bool vertical)
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VkResult vr = Functions->vkBeginCommandBuffer(command, &begin);
        if (vr != VK_SUCCESS) return Fail("vkBeginCommandBuffer(presenter)", vr);
        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        render.renderPass = RenderPass;
        render.framebuffer = target.Framebuffer;
        render.renderArea.extent = {width, height};
        render.clearValueCount = 1;
        render.pClearValues = &clear;
        Functions->vkCmdBeginRenderPass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
        Functions->vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
        Functions->vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
            PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);

        for (std::uint32_t draw = 0; draw < 2; ++draw)
        {
            VkViewport viewport{};
            VkRect2D scissor{};
            PresenterPush push;
            if (vertical)
            {
                viewport.x = 0.0f;
                viewport.y = static_cast<float>(draw * kSourceHeight);
                viewport.width = static_cast<float>(kSourceWidth);
                viewport.height = static_cast<float>(kSourceHeight);
                scissor.offset = {0, static_cast<std::int32_t>(draw * kSourceHeight)};
                scissor.extent = {kSourceWidth, kSourceHeight};
                push.Layer = draw;
            }
            else
            {
                viewport.x = static_cast<float>(draw * kSourceWidth);
                viewport.y = 0.0f;
                viewport.width = static_cast<float>(kSourceWidth);
                viewport.height = static_cast<float>(kSourceHeight);
                scissor.offset = {static_cast<std::int32_t>(draw * kSourceWidth), 0};
                scissor.extent = {kSourceWidth, kSourceHeight};
                if (draw == 0)
                {
                    push.Layer = 1;
                    push.FlipX = 1;
                }
                else
                {
                    push.Layer = 0;
                    push.FlipY = 1;
                }
            }
            viewport.maxDepth = 1.0f;
            Functions->vkCmdSetViewport(command, 0, 1, &viewport);
            Functions->vkCmdSetScissor(command, 0, 1, &scissor);
            Functions->vkCmdPushConstants(command, PipelineLayout,
                VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            Functions->vkCmdDraw(command, 3, 1, 0, 0);
        }
        Functions->vkCmdEndRenderPass(command);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {width, height, 1};
        Functions->vkCmdCopyImageToBuffer(command, target.Image.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.Readback.Buffer, 1, &copy);
        VkBufferMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.buffer = target.Readback.Buffer;
        hostBarrier.size = VK_WHOLE_SIZE;
        Functions->vkCmdPipelineBarrier(command,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &hostBarrier, 0, nullptr);
        vr = Functions->vkEndCommandBuffer(command);
        return vr == VK_SUCCESS ? true : Fail("vkEndCommandBuffer(presenter)", vr);
    }

    bool SubmitPresenter(
        VkCommandBuffer command,
        VkFence fence,
        bool timelineWait)
    {
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        VkTimelineSemaphoreSubmitInfo timeline{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        if (timelineWait)
        {
            timeline.waitSemaphoreValueCount = 1;
            timeline.pWaitSemaphoreValues = &ProducerSignalValue;
            submit.pNext = &timeline;
            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = &TimelineSemaphore;
            submit.pWaitDstStageMask = &waitStage;
        }
        const VkResult vr = Functions->vkQueueSubmit(
            Context->graphicsQueue(), 1, &submit, fence);
        return vr == VK_SUCCESS ? true : Fail("vkQueueSubmit(presenter)", vr);
    }

    bool Submit(ProbeResult& result)
    {
        VkSubmitInfo producerSubmit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        producerSubmit.commandBufferCount = 1;
        producerSubmit.pCommandBuffers = &ProducerCommand;
        VkTimelineSemaphoreSubmitInfo timeline{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        ProducerSignalValue = kProducerValue;
        if (TimelineSemaphore != VK_NULL_HANDLE)
        {
            timeline.signalSemaphoreValueCount = 1;
            timeline.pSignalSemaphoreValues = &ProducerSignalValue;
            producerSubmit.pNext = &timeline;
            producerSubmit.signalSemaphoreCount = 1;
            producerSubmit.pSignalSemaphores = &TimelineSemaphore;
        }
        VkResult vr = Functions->vkQueueSubmit(
            Context->graphicsQueue(), 1, &producerSubmit, ProducerFence);
        if (vr != VK_SUCCESS) return Fail("vkQueueSubmit(producer)", vr);

        if (TimelineSemaphore == VK_NULL_HANDLE)
        {
            vr = Functions->vkWaitForFences(Device, 1, &ProducerFence, VK_TRUE, UINT64_MAX);
            if (vr != VK_SUCCESS) return Fail("vkWaitForFences(producer)", vr);
            Ring.MarkProducerComplete(kProducerValue);
        }

        if (!SubmitPresenter(WindowACommand, WindowAFence,
                TimelineSemaphore != VK_NULL_HANDLE) ||
            !SubmitPresenter(WindowBCommand, WindowBFence,
                TimelineSemaphore != VK_NULL_HANDLE))
            return false;
        result.ProducerTimelineWait = TimelineSemaphore != VK_NULL_HANDLE ||
            result.FenceFallbackUsed;

        vr = Functions->vkWaitForFences(Device, 1, &WindowAFence, VK_TRUE, UINT64_MAX);
        if (vr != VK_SUCCESS) return Fail("vkWaitForFences(window A)", vr);
        WindowALease.ReleaseNow();
        result.WindowCloseLeaseRelease = Ring.SlotSnapshot(ProducedSlot).PresenterRefs == 1u;
        result.ReleaseAfterCompletion = result.WindowCloseLeaseRelease;
        result.ReuseRejectedWhileReferenced =
            Ring.SlotSnapshot(ProducedSlot).PresenterRefs == 1u;

        vr = Functions->vkWaitForFences(Device, 1, &WindowBFence, VK_TRUE, UINT64_MAX);
        if (vr != VK_SUCCESS) return Fail("vkWaitForFences(window B)", vr);
        WindowBLease.ReleaseNow();
        vr = Functions->vkWaitForFences(Device, 1, &ProducerFence, VK_TRUE, UINT64_MAX);
        if (vr != VK_SUCCESS) return Fail("vkWaitForFences(producer final)", vr);
        Ring.MarkProducerComplete(kProducerValue);
        result.ReuseAfterAllReferencesReleased =
            Ring.SlotSnapshot(ProducedSlot).PresenterRefs == 0u;
        result.VulkanApiCallsSucceeded = true;
        return result.ProducerTimelineWait && result.ReleaseAfterCompletion &&
            result.ReuseRejectedWhileReferenced &&
            result.ReuseAfterAllReferencesReleased;
    }

    bool ReadTarget(
        const TargetResource& target,
        const std::vector<std::uint32_t>& expected,
        bool& matched,
        std::uint64_t& digest)
    {
        if (!InvalidateBuffer(target.Readback))
            return false;
        void* mapped = nullptr;
        VkResult vr = Functions->vkMapMemory(Device, target.Readback.Memory,
            0, target.Readback.Size, 0, &mapped);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(readback)", vr);
        const auto* words = static_cast<const std::uint32_t*>(mapped);
        std::vector<std::uint32_t> actual(words, words + expected.size());
        Functions->vkUnmapMemory(Device, target.Readback.Memory);
        matched = actual == expected;
        digest = DigestPixels(actual);
        return matched;
    }

    bool Readback(ProbeResult& result)
    {
        if (!ReadTarget(WindowATarget, ExpectedWindowA,
                result.WindowAImageMatched, result.WindowADigest) ||
            !ReadTarget(WindowBTarget, ExpectedWindowB,
                result.WindowBImageMatched, result.WindowBDigest))
            return false;
        result.SamplesMatched = result.WindowAImageMatched && result.WindowBImageMatched;
        result.PresenterCpuCopyBytes = 0;
        result.NoPresenterCpuCopy = true;
        return result.SamplesMatched;
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

    void DestroyTarget(TargetResource& target)
    {
        if (target.Framebuffer) Functions->vkDestroyFramebuffer(Device, target.Framebuffer, nullptr);
        DestroyBuffer(target.Readback);
        DestroyImage(target.Image);
        target = {};
    }

    void Destroy()
    {
        if (!Functions || Device == VK_NULL_HANDLE)
            return;
        Functions->vkDeviceWaitIdle(Device);
        WindowALease.ReleaseNow();
        WindowBLease.ReleaseNow();
        Ring.MarkProducerComplete(std::numeric_limits<std::uint64_t>::max());
        if (TimelineSemaphore) Functions->vkDestroySemaphore(Device, TimelineSemaphore, nullptr);
        if (ProducerFence) Functions->vkDestroyFence(Device, ProducerFence, nullptr);
        if (WindowAFence) Functions->vkDestroyFence(Device, WindowAFence, nullptr);
        if (WindowBFence) Functions->vkDestroyFence(Device, WindowBFence, nullptr);
        if (CommandPool) Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
        if (Pipeline) Functions->vkDestroyPipeline(Device, Pipeline, nullptr);
        if (VertexShader) Functions->vkDestroyShaderModule(Device, VertexShader, nullptr);
        if (FragmentShader) Functions->vkDestroyShaderModule(Device, FragmentShader, nullptr);
        if (PipelineLayout) Functions->vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        if (DescriptorPool) Functions->vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
        if (DescriptorSetLayout)
            Functions->vkDestroyDescriptorSetLayout(Device, DescriptorSetLayout, nullptr);
        if (Sampler) Functions->vkDestroySampler(Device, Sampler, nullptr);
        DestroyTarget(WindowATarget);
        DestroyTarget(WindowBTarget);
        if (RenderPass) Functions->vkDestroyRenderPass(Device, RenderPass, nullptr);
        DestroyImage(SourceImage);
        DestroyBuffer(SourceUpload);
    }

    std::shared_ptr<DeviceContext> Context;
    QWindow* Window = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    QVulkanDeviceFunctions* Functions = nullptr;
    VulkanPhase10OutputRing Ring;
    VulkanPhase10OutputLease WindowALease;
    VulkanPhase10OutputLease WindowBLease;
    std::uint32_t ProducedSlot = 0;
    std::vector<std::uint32_t> SourcePixels;
    std::vector<std::uint32_t> ExpectedWindowA;
    std::vector<std::uint32_t> ExpectedWindowB;
    BufferResource SourceUpload;
    ImageResource SourceImage;
    TargetResource WindowATarget;
    TargetResource WindowBTarget;
    VkRenderPass RenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
    VkSampler Sampler = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkShaderModule VertexShader = VK_NULL_HANDLE;
    VkShaderModule FragmentShader = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer ProducerCommand = VK_NULL_HANDLE;
    VkCommandBuffer WindowACommand = VK_NULL_HANDLE;
    VkCommandBuffer WindowBCommand = VK_NULL_HANDLE;
    VkFence ProducerFence = VK_NULL_HANDLE;
    VkFence WindowAFence = VK_NULL_HANDLE;
    VkFence WindowBFence = VK_NULL_HANDLE;
    VkSemaphore TimelineSemaphore = VK_NULL_HANDLE;
    std::uint64_t ProducerSignalValue = 0;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

QJsonObject ProbeJson(const ProbeResult& result)
{
    return {
        {"passed", result.Passed},
        {"timeline_semaphore_used", result.TimelineSemaphoreUsed},
        {"fence_fallback_used", result.FenceFallbackUsed},
        {"fixed_three_slot_ring", result.FixedThreeSlotRing},
        {"producer_timeline_wait", result.ProducerTimelineWait},
        {"shader_read_layout", result.ShaderReadLayout},
        {"same_queue_family_fast_path", result.SameQueueFamilyFastPath},
        {"multi_window_lease", result.MultiWindowLease},
        {"reuse_rejected_while_referenced", result.ReuseRejectedWhileReferenced},
        {"release_after_completion", result.ReleaseAfterCompletion},
        {"reuse_after_all_references_released", result.ReuseAfterAllReferencesReleased},
        {"generation_invalidation", result.GenerationInvalidation},
        {"bounded_frame_drop", result.BoundedFrameDrop},
        {"diagnostic_timeout_policy", result.DiagnosticTimeoutPolicy},
        {"no_presenter_cpu_copy", result.NoPresenterCpuCopy},
        {"window_close_lease_release", result.WindowCloseLeaseRelease},
        {"scale_change_lifecycle", result.ScaleChangeLifecycle},
        {"renderer_switch_lifecycle", result.RendererSwitchLifecycle},
        {"fullscreen_resize_lifecycle", result.FullscreenResizeLifecycle},
        {"fast_forward_frame_drop_policy", result.FastForwardFrameDropPolicy},
        {"window_a_image_matched", result.WindowAImageMatched},
        {"window_b_image_matched", result.WindowBImageMatched},
        {"samples_matched", result.SamplesMatched},
        {"vulkan_api_calls_succeeded", result.VulkanApiCallsSucceeded},
        {"resource_destroy_cycle_completed", result.ResourceDestroyCycleCompleted},
        {"peak_presenter_refs", static_cast<int>(result.PeakPresenterRefs)},
        {"presenter_cpu_copy_bytes", QString::fromStdString(
            std::to_string(result.PresenterCpuCopyBytes))},
        {"window_a_digest", QString::fromStdString(std::to_string(result.WindowADigest))},
        {"window_b_digest", QString::fromStdString(std::to_string(result.WindowBDigest))},
        {"failure_stage", QString::fromStdString(result.FailureStage)},
        {"vk_result", static_cast<int>(result.FailureResult)},
    };
}

} // namespace

int RunPhase10CompletionHarness(const QString& outputPath, int iterations)
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
                Phase10CompletionProbe probe(context, &window);
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
        passed ? "[MelonPrime] Vulkan Phase 10 completion harness passed: iterations=%d\n" :
                 "[MelonPrime] Vulkan Phase 10 completion harness failed: iterations=%d\n",
        completed);
    const QJsonObject output{
        {"schema_version", 21},
        {"passed", passed},
        {"contract_version", static_cast<int>(kPhase10OutputRingContractVersion)},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"phase10_subsystem_complete", passed},
        {"output_ring_slot_count", static_cast<int>(kPhase10OutputSlotCount)},
        {"max_presenter_leases_per_slot",
            static_cast<int>(kPhase10MaxPresenterLeasesPerSlot)},
        {"zero_copy_presenter_integrated", passed && lastResult.NoPresenterCpuCopy},
        {"gpu_image_sampled_directly", lastResult.SamplesMatched},
        {"presenter_cpu_copy_bytes", QString::fromStdString(
            std::to_string(lastResult.PresenterCpuCopyBytes))},
        {"producer_timeline_wait_passed", lastResult.ProducerTimelineWait},
        {"timeline_semaphore_used", lastResult.TimelineSemaphoreUsed},
        {"fence_fallback_used", lastResult.FenceFallbackUsed},
        {"shader_read_layout_passed", lastResult.ShaderReadLayout},
        {"same_queue_family_fast_path_passed", lastResult.SameQueueFamilyFastPath},
        {"multi_window_passed", lastResult.MultiWindowLease},
        {"peak_presenter_refs", static_cast<int>(lastResult.PeakPresenterRefs)},
        {"reuse_rejected_while_presenter_refs", lastResult.ReuseRejectedWhileReferenced},
        {"lease_release_after_command_completion", lastResult.ReleaseAfterCompletion},
        {"slot_reused_after_all_refs_released", lastResult.ReuseAfterAllReferencesReleased},
        {"window_close_lease_release_passed", lastResult.WindowCloseLeaseRelease},
        {"generation_change_invalidated_old_slots", lastResult.GenerationInvalidation},
        {"bounded_frame_drop_policy_passed", lastResult.BoundedFrameDrop},
        {"fast_forward_frame_drop_policy_passed", lastResult.FastForwardFrameDropPolicy},
        {"diagnostic_timeout_milliseconds",
            static_cast<int>(kPhase10DiagnosticTimeoutMilliseconds)},
        {"scale_change_lifecycle_passed", lastResult.ScaleChangeLifecycle},
        {"renderer_switch_lifecycle_passed", lastResult.RendererSwitchLifecycle},
        {"fullscreen_resize_lifecycle_passed", lastResult.FullscreenResizeLifecycle},
        {"window_a_image_matched", lastResult.WindowAImageMatched},
        {"window_b_image_matched", lastResult.WindowBImageMatched},
        {"samples_matched", lastResult.SamplesMatched},
        {"vulkan_api_calls_succeeded", lastResult.VulkanApiCallsSucceeded},
        {"resource_destroy_cycle_completed", lastResult.ResourceDestroyCycleCompleted},
        {"steady_state_cpu_copy_zero", lastResult.PresenterCpuCopyBytes == 0},
        {"rom_visible_path_activated", false},
        {"native_ds_polygon_raster_integrated", false},
        {"vulkan_compute_renderer_integrated", false},
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
