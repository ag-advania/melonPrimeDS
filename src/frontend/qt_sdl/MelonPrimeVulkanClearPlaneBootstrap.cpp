#include "MelonPrimeVulkanClearPlaneBootstrap.h"

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

#include "GPU3D_Vulkan.h"
#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanInstanceHost.h"
#include "Platform.h"
#include "main.h"

namespace MelonPrime::Vulkan
{
namespace
{

constexpr std::uint32_t kRawAttr1 =
    5u | (12u << 5) | (27u << 10) | (1u << 15) | (17u << 16) | (42u << 24);
constexpr std::uint32_t kRawAttr2 = 0x4567u;

struct BufferResource
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
};

struct ImageResource
{
    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
};

struct ProbeResult
{
    bool Passed = false;
    bool ContractPassed = false;
    bool ClearSubmitted = false;
    bool ColorReadbackCompleted = false;
    bool AttributeReadbackCompleted = false;
    bool ColorSamplesMatched = false;
    bool AttributeSamplesMatched = false;
    bool DepthStencilAttachmentCreated = false;
    bool DepthStencilClearSubmitted = false;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
    std::array<std::uint8_t, 4> ExpectedColor{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> ExpectedAttributes{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> ColorFirst{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> ColorCenter{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> ColorLast{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> AttributeFirst{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> AttributeCenter{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> AttributeLast{{0, 0, 0, 0}};
};

std::uint8_t ToUnorm8(float value)
{
    const float scaled = std::clamp(value, 0.0f, 1.0f) * 255.0f;
    return static_cast<std::uint8_t>(std::lround(scaled));
}

std::array<std::uint8_t, 4> StorageBytes(
    const std::array<float, 4>& rgba,
    VkFormat format)
{
    const std::array<std::uint8_t, 4> logical{{
        ToUnorm8(rgba[0]),
        ToUnorm8(rgba[1]),
        ToUnorm8(rgba[2]),
        ToUnorm8(rgba[3])}};
    if (format == VK_FORMAT_B8G8R8A8_UNORM)
        return {{logical[2], logical[1], logical[0], logical[3]}};
    return logical;
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
    for (int scale : {1, 2, 4, 8, 16})
    {
        const auto contract = melonDS::Vulkan::BuildRasterTargetContract(
            scale, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_D24_UNORM_S8_UINT);
        if (!contract.Valid ||
            contract.Extent.width != static_cast<std::uint32_t>(256 * scale) ||
            contract.Extent.height != static_cast<std::uint32_t>(192 * scale) ||
            contract.AttributeFormat != VK_FORMAT_R8G8B8A8_UNORM)
        {
            return false;
        }
    }
    if (melonDS::Vulkan::BuildRasterTargetContract(
            0, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_D24_UNORM_S8_UINT).Valid ||
        melonDS::Vulkan::BuildRasterTargetContract(
            17, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_D24_UNORM_S8_UINT).Valid ||
        melonDS::Vulkan::BuildRasterTargetContract(
            1, VK_FORMAT_UNDEFINED, VK_FORMAT_D24_UNORM_S8_UINT).Valid ||
        melonDS::Vulkan::BuildRasterTargetContract(
            1, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_UNDEFINED).Valid)
    {
        return false;
    }

    const auto zero = melonDS::Vulkan::DecodeClearPlaneState(0, 0);
    if (zero.Depth24 != 0x1FFu || zero.Stencil != 0xFFu ||
        zero.Color[0] != 0.0f || zero.Color[3] != 0.0f ||
        zero.Attributes[0] != 0.0f || zero.Attributes[2] != 0.0f ||
        zero.Attributes[3] != 1.0f)
    {
        return false;
    }

    const std::uint32_t maxAttr1 =
        31u | (31u << 5) | (31u << 10) | (1u << 15) |
        (31u << 16) | (63u << 24);
    const auto maximum = melonDS::Vulkan::DecodeClearPlaneState(maxAttr1, 0x7FFFu);
    if (maximum.Depth24 != 0xFFFFFFu || maximum.OpaquePolyId != 63u ||
        !maximum.Fog || maximum.Color[0] != 1.0f || maximum.Color[3] != 1.0f ||
        maximum.Attributes[0] != 1.0f || maximum.Attributes[2] != 1.0f)
    {
        return false;
    }

    const auto mixed = melonDS::Vulkan::DecodeClearPlaneState(kRawAttr1, kRawAttr2);
    return mixed.Color5[0] == 5u && mixed.Color5[1] == 12u &&
        mixed.Color5[2] == 27u && mixed.Color5[3] == 17u &&
        mixed.OpaquePolyId == 42u && mixed.Fog &&
        mixed.Depth24 == ((kRawAttr2 & 0x7FFFu) * 0x200u) + 0x1FFu;
}

class ClearPlaneBootstrapProbe
{
public:
    ClearPlaneBootstrapProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        if (Context)
        {
            Device = Context->device();
            Functions = Context->functions();
        }
    }

    ~ClearPlaneBootstrapProbe()
    {
        Destroy();
    }

    ProbeResult Run()
    {
        ProbeResult result;
        result.ContractPassed = ContractChecks();
        State = melonDS::Vulkan::DecodeClearPlaneState(kRawAttr1, kRawAttr2);
        Contract = melonDS::Vulkan::BuildRasterTargetContract(
            1,
            Context->featureInfo().colorFormat,
            Context->featureInfo().depthStencilFormat);
        result.ExpectedColor = StorageBytes(State.Color, Contract.ColorFormat);
        result.ExpectedAttributes = StorageBytes(
            State.Attributes, Contract.AttributeFormat);

        if (!result.ContractPassed || !Contract.Valid)
        {
            result.FailureStage = "clear-plane contract validation";
            return result;
        }
        if (!CreateResources())
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.DepthStencilAttachmentCreated = DepthImage.Image != VK_NULL_HANDLE;
        if (!RecordAndSubmit())
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.ClearSubmitted = true;
        result.DepthStencilClearSubmitted = true;

        if (!Readback(ColorReadback, result.ColorFirst, result.ColorCenter, result.ColorLast))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.ColorReadbackCompleted = true;
        if (!Readback(
                AttributeReadback,
                result.AttributeFirst,
                result.AttributeCenter,
                result.AttributeLast))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.AttributeReadbackCompleted = true;

        result.ColorSamplesMatched =
            PixelMatches(result.ColorFirst, result.ExpectedColor) &&
            PixelMatches(result.ColorCenter, result.ExpectedColor) &&
            PixelMatches(result.ColorLast, result.ExpectedColor);
        result.AttributeSamplesMatched =
            PixelMatches(result.AttributeFirst, result.ExpectedAttributes) &&
            PixelMatches(result.AttributeCenter, result.ExpectedAttributes) &&
            PixelMatches(result.AttributeLast, result.ExpectedAttributes);
        result.Passed = result.ContractPassed && result.ClearSubmitted &&
            result.ColorReadbackCompleted && result.AttributeReadbackCompleted &&
            result.ColorSamplesMatched && result.AttributeSamplesMatched &&
            result.DepthStencilAttachmentCreated && result.DepthStencilClearSubmitted;
        if (!result.Passed)
            result.FailureStage = "clear-plane attachment readback comparison";
        return result;
    }

    const melonDS::Vulkan::ClearPlaneState& state() const
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

    bool FormatSupports(
        VkFormat format,
        VkFormatFeatureFlags required) const
    {
        VkFormatProperties properties{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceFormatProperties(
            Context->physicalDevice(), format, &properties);
        return (properties.optimalTilingFeatures & required) == required;
    }

    bool CreateBuffer(VkDeviceSize size, BufferResource& resource)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult result = Functions->vkCreateBuffer(Device, &info, nullptr, &resource.Buffer);
        if (result != VK_SUCCESS)
            return Fail("vkCreateBuffer", result);

        VkMemoryRequirements requirements{};
        Functions->vkGetBufferMemoryRequirements(Device, resource.Buffer, &requirements);
        const std::uint32_t memoryType = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            0);
        if (memoryType == std::numeric_limits<std::uint32_t>::max())
            return Fail("host-visible coherent memory type", VK_ERROR_FEATURE_NOT_PRESENT);

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
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspect,
        ImageResource& resource)
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {Contract.Extent.width, Contract.Extent.height, 1};
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

    bool CreateResources()
    {
        constexpr VkFormatFeatureFlags colorFeatures =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        if (!FormatSupports(Contract.ColorFormat, colorFeatures) ||
            !FormatSupports(Contract.AttributeFormat, colorFeatures))
        {
            return Fail("clear-plane color format features", VK_ERROR_FORMAT_NOT_SUPPORTED);
        }

        const VkDeviceSize frameBytes =
            static_cast<VkDeviceSize>(Contract.Extent.width) * Contract.Extent.height * 4u;
        if (!CreateBuffer(frameBytes, ColorReadback) ||
            !CreateBuffer(frameBytes, AttributeReadback))
        {
            return false;
        }
        if (!CreateImage(
                Contract.ColorFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                ColorImage) ||
            !CreateImage(
                Contract.AttributeFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                AttributeImage) ||
            !CreateImage(
                Contract.DepthStencilFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                melonDS::Vulkan::DepthStencilAspectMask(Contract.DepthStencilFormat),
                DepthImage))
        {
            return false;
        }

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
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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
        VkResult result = Functions->vkCreateRenderPass(
            Device, &renderPassInfo, nullptr, &RenderPass);
        if (result != VK_SUCCESS)
            return Fail("vkCreateRenderPass", result);

        const VkImageView framebufferAttachments[] = {
            ColorImage.View, AttributeImage.View, DepthImage.View};
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

    bool RecordAndSubmit()
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult result = Functions->vkBeginCommandBuffer(CommandBuffer, &begin);
        if (result != VK_SUCCESS)
            return Fail("vkBeginCommandBuffer", result);

        const auto clearValues = melonDS::Vulkan::BuildClearPlaneAttachmentValues(State);
        VkRenderPassBeginInfo renderPassBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderPassBegin.renderPass = RenderPass;
        renderPassBegin.framebuffer = Framebuffer;
        renderPassBegin.renderArea.extent = Contract.Extent;
        renderPassBegin.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
        renderPassBegin.pClearValues = clearValues.data();
        Functions->vkCmdBeginRenderPass(
            CommandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
        Functions->vkCmdEndRenderPass(CommandBuffer);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {Contract.Extent.width, Contract.Extent.height, 1};
        Functions->vkCmdCopyImageToBuffer(
            CommandBuffer,
            ColorImage.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            ColorReadback.Buffer,
            1,
            &copy);
        Functions->vkCmdCopyImageToBuffer(
            CommandBuffer,
            AttributeImage.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            AttributeReadback.Buffer,
            1,
            &copy);

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
        std::size_t pixelIndex)
    {
        const std::size_t offset = pixelIndex * 4u;
        return {{data[offset], data[offset + 1], data[offset + 2], data[offset + 3]}};
    }

    bool Readback(
        const BufferResource& resource,
        std::array<std::uint8_t, 4>& first,
        std::array<std::uint8_t, 4>& center,
        std::array<std::uint8_t, 4>& last)
    {
        const VkDeviceSize frameBytes =
            static_cast<VkDeviceSize>(Contract.Extent.width) * Contract.Extent.height * 4u;
        void* mapped = nullptr;
        const VkResult result = Functions->vkMapMemory(
            Device, resource.Memory, 0, frameBytes, 0, &mapped);
        if (result != VK_SUCCESS)
            return Fail("vkMapMemory(readback)", result);
        const auto* bytes = static_cast<const std::uint8_t*>(mapped);
        first = ReadPixel(bytes, 0);
        center = ReadPixel(
            bytes,
            static_cast<std::size_t>(Contract.Extent.height / 2) * Contract.Extent.width +
                (Contract.Extent.width / 2));
        last = ReadPixel(
            bytes,
            static_cast<std::size_t>(Contract.Extent.width) * Contract.Extent.height - 1);
        Functions->vkUnmapMemory(Device, resource.Memory);
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

    void Destroy()
    {
        if (!Functions || !Device)
            return;
        if (Fence)
            Functions->vkDestroyFence(Device, Fence, nullptr);
        if (CommandPool)
            Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
        if (Framebuffer)
            Functions->vkDestroyFramebuffer(Device, Framebuffer, nullptr);
        if (RenderPass)
            Functions->vkDestroyRenderPass(Device, RenderPass, nullptr);
        DestroyImage(DepthImage);
        DestroyImage(AttributeImage);
        DestroyImage(ColorImage);
        DestroyBuffer(AttributeReadback);
        DestroyBuffer(ColorReadback);
    }

    std::shared_ptr<DeviceContext> Context;
    QWindow* Window = nullptr;
    QVulkanDeviceFunctions* Functions = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    melonDS::Vulkan::RasterTargetContract Contract;
    melonDS::Vulkan::ClearPlaneState State;
    BufferResource ColorReadback;
    BufferResource AttributeReadback;
    ImageResource ColorImage;
    ImageResource AttributeImage;
    ImageResource DepthImage;
    VkRenderPass RenderPass = VK_NULL_HANDLE;
    VkFramebuffer Framebuffer = VK_NULL_HANDLE;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

QJsonArray PixelArray(const std::array<std::uint8_t, 4>& pixel)
{
    return QJsonArray{
        static_cast<int>(pixel[0]),
        static_cast<int>(pixel[1]),
        static_cast<int>(pixel[2]),
        static_cast<int>(pixel[3])};
}

QJsonArray FloatArray(const std::array<float, 4>& values)
{
    return QJsonArray{values[0], values[1], values[2], values[3]};
}

} // namespace

int RunClearPlaneBootstrapHarness(const QString& outputPath, int iterations)
{
    if (iterations <= 0)
        iterations = 1;

    FeatureInfo lastInfo;
    ProbeResult lastResult;
    melonDS::Vulkan::ClearPlaneState lastState;
    melonDS::Vulkan::RasterTargetContract lastContract;
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
                ClearPlaneBootstrapProbe probe(context, &window);
                lastResult = probe.Run();
                lastState = probe.state();
                lastContract = probe.contract();
            }
            context.reset();
            window.destroy();
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
            "[MelonPrime] Vulkan clear-plane bootstrap passed: iterations=%d size=%ux%u color=%d attr=%d depth=%d\n",
            completed,
            lastContract.Extent.width,
            lastContract.Extent.height,
            static_cast<int>(lastContract.ColorFormat),
            static_cast<int>(lastContract.AttributeFormat),
            static_cast<int>(lastContract.DepthStencilFormat));
    }
    else
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan clear-plane bootstrap failed: completed=%d/%d stage=%s VkResult=%d\n",
            completed,
            iterations,
            lastResult.FailureStage.empty() ? "unknown" : lastResult.FailureStage.c_str(),
            static_cast<int>(lastResult.FailureResult));
    }

    const QJsonObject object{
        {"schema_version", 1},
        {"passed", passed},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"contract_passed", lastResult.ContractPassed},
        {"raster_target_contract_version",
            static_cast<int>(melonDS::Vulkan::kRasterTargetContractVersion)},
        {"clear_plane_contract_version",
            static_cast<int>(melonDS::Vulkan::kClearPlaneContractVersion)},
        {"width", static_cast<qint64>(lastContract.Extent.width)},
        {"height", static_cast<qint64>(lastContract.Extent.height)},
        {"scale_factor", static_cast<qint64>(lastContract.ScaleFactor)},
        {"color_format", static_cast<int>(lastContract.ColorFormat)},
        {"attribute_format", static_cast<int>(lastContract.AttributeFormat)},
        {"depth_stencil_format", static_cast<int>(lastContract.DepthStencilFormat)},
        {"clear_submitted", lastResult.ClearSubmitted},
        {"color_readback_completed", lastResult.ColorReadbackCompleted},
        {"attribute_readback_completed", lastResult.AttributeReadbackCompleted},
        {"color_samples_matched", lastResult.ColorSamplesMatched},
        {"attribute_samples_matched", lastResult.AttributeSamplesMatched},
        {"depth_stencil_attachment_created", lastResult.DepthStencilAttachmentCreated},
        {"depth_stencil_clear_submitted", lastResult.DepthStencilClearSubmitted},
        {"depth_stencil_readback_verified", false},
        {"software_game_rendering_preserved", true},
        {"native_ds_polygon_raster_integrated", false},
        {"raw_clear_attr1", static_cast<qint64>(lastState.RawAttr1)},
        {"raw_clear_attr2", static_cast<qint64>(lastState.RawAttr2)},
        {"depth24", static_cast<qint64>(lastState.Depth24)},
        {"depth", lastState.Depth},
        {"stencil", static_cast<int>(lastState.Stencil)},
        {"opaque_polygon_id", static_cast<int>(lastState.OpaquePolyId)},
        {"fog", lastState.Fog},
        {"logical_color", FloatArray(lastState.Color)},
        {"logical_attributes", FloatArray(lastState.Attributes)},
        {"expected_color_storage", PixelArray(lastResult.ExpectedColor)},
        {"expected_attribute_storage", PixelArray(lastResult.ExpectedAttributes)},
        {"color_first", PixelArray(lastResult.ColorFirst)},
        {"color_center", PixelArray(lastResult.ColorCenter)},
        {"color_last", PixelArray(lastResult.ColorLast)},
        {"attribute_first", PixelArray(lastResult.AttributeFirst)},
        {"attribute_center", PixelArray(lastResult.AttributeCenter)},
        {"attribute_last", PixelArray(lastResult.AttributeLast)},
        {"failure_stage", QString::fromStdString(lastResult.FailureStage)},
        {"failure_vk_result", static_cast<int>(lastResult.FailureResult)},
    };
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 2;
    output.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    output.close();
    return passed ? 0 : 1;
}

} // namespace MelonPrime::Vulkan
