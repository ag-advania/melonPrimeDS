#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "MelonPrimeVulkanSurfacePresenter.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>

#include "MelonPrimeVulkanOutput.h"
#include "MelonPrimeVulkanSurface.h"
#include "MelonPrimeVulkanSurfacePresenterFragmentShaderData.h"
#include "MelonPrimeVulkanSurfacePresenterVertexShaderData.h"
#include "Platform.h"
#include "VulkanContext.h"
#include "VulkanDispatch.h"

namespace MelonPrime
{
namespace
{
const char* NativeWindowTypeName(VulkanNativeWindowType type)
{
    switch (type)
    {
    case VulkanNativeWindowType::Win32:
        return "Win32";
    case VulkanNativeWindowType::Xlib:
        return "Xlib";
    case VulkanNativeWindowType::Wayland:
        return "Wayland";
    default:
        return "Unknown";
    }
}

const char* SurfaceExtensionName(VulkanNativeWindowType type)
{
    switch (type)
    {
    case VulkanNativeWindowType::Win32:
        return "VK_KHR_win32_surface";
    case VulkanNativeWindowType::Xlib:
        return "VK_KHR_xlib_surface";
    case VulkanNativeWindowType::Wayland:
        return "VK_KHR_wayland_surface";
    default:
        return "unknown";
    }
}
} // namespace

MelonPrimeVulkanSurfacePresenter::~MelonPrimeVulkanSurfacePresenter()
{
    Shutdown();
}

void MelonPrimeVulkanSurfacePresenter::SetError(const std::string& value)
{
    lastError = value;
    melonDS::Platform::Log(melonDS::Platform::LogLevel::Error, "VulkanPresenter: %s", value.c_str());
}

bool MelonPrimeVulkanSurfacePresenter::Init(
    const VulkanNativeWindowInfo& nativeWindow,
    std::uint32_t width,
    std::uint32_t height,
    bool requestedVsync)
{
    Shutdown();
    requestedWidth = width;
    requestedHeight = height;
    vsync = requestedVsync;

    auto& context = melonDS::VulkanContext::Get();
    if (!context.Acquire())
    {
        SetError("shared Vulkan context acquisition failed");
        return false;
    }
    contextAcquired = true;
    instance = context.GetInstance();
    physicalDevice = context.GetPhysicalDevice();
    device = context.GetDevice();
    queue = context.GetQueue();
    queueFamilyIndex = context.GetQueueFamilyIndex();

    surface = CreateVulkanSurface(instance, nativeWindow, lastError);
    if (surface == VK_NULL_HANDLE)
    {
        SetError(lastError.empty() ? "surface creation failed" : lastError);
        Shutdown();
        return false;
    }

    VkBool32 presentSupported = VK_FALSE;
    if (vkGetPhysicalDeviceSurfaceSupportKHR(
            physicalDevice, queueFamilyIndex, surface, &presentSupported) != VK_SUCCESS
        || presentSupported != VK_TRUE)
    {
        SetError("selected Vulkan queue cannot present to this surface");
        Shutdown();
        return false;
    }
    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Info,
        "VulkanPresenter: surface=%s extension=%s presentSupport=1 queueFamily=%u",
        NativeWindowTypeName(nativeWindow.type),
        SurfaceExtensionName(nativeWindow.type),
        queueFamilyIndex
    );

    if (!CreateCommandResources() || !CreatePresentationResources() || !CreateSwapchain())
    {
        Shutdown();
        return false;
    }

    initialized = true;
    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Info,
        "VulkanPresenter: initialized extent=%ux%u format=%d vsync=%u",
        swapchainExtent.width,
        swapchainExtent.height,
        static_cast<int>(swapchainFormat),
        vsync ? 1u : 0u);
    return true;
}

void MelonPrimeVulkanSurfacePresenter::Shutdown()
{
    if (device != VK_NULL_HANDLE)
    {
        if (submitFence != VK_NULL_HANDLE)
            vkWaitForFences(device, 1, &submitFence, VK_TRUE, UINT64_MAX);
        DestroySwapchain();
        DestroyPresentationResources();
        DestroyCommandResources();
    }
    if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
    if (submittedFrame != nullptr)
        submittedFrame->presentTimelineValue = 0;
    submittedFrame = nullptr;

    if (contextAcquired)
        melonDS::VulkanContext::Get().Release();
    contextAcquired = false;
    instance = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
    queue = VK_NULL_HANDLE;
    initialized = false;
}

void MelonPrimeVulkanSurfacePresenter::Resize(
    std::uint32_t width,
    std::uint32_t height,
    bool requestedVsync)
{
    if (requestedWidth == width && requestedHeight == height && vsync == requestedVsync)
        return;
    requestedWidth = width;
    requestedHeight = height;
    vsync = requestedVsync;
    swapchainDirty = true;
}

VkPresentModeKHR MelonPrimeVulkanSurfacePresenter::ChoosePresentMode(
    const std::vector<VkPresentModeKHR>& modes) const
{
    if (vsync)
        return VK_PRESENT_MODE_FIFO_KHR;
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
        return VK_PRESENT_MODE_MAILBOX_KHR;
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    return VK_PRESENT_MODE_FIFO_KHR;
}

bool MelonPrimeVulkanSurfacePresenter::CreateSwapchain()
{
    if (requestedWidth == 0 || requestedHeight == 0)
    {
        swapchainDirty = true;
        return true;
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities) != VK_SUCCESS)
    {
        SetError("surface capability query failed");
        return false;
    }

    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    if (formatCount == 0)
    {
        SetError("surface exposes no formats");
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(
            physicalDevice, surface, &formatCount, formats.data()) != VK_SUCCESS)
    {
        SetError("surface format query failed");
        return false;
    }

    VkSurfaceFormatKHR selected = formats.front();
    for (const auto& candidate : formats)
    {
        if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM
            || candidate.format == VK_FORMAT_R8G8B8A8_UNORM)
        {
            selected = candidate;
            break;
        }
    }

    std::uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    if (modeCount > 0)
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, modes.data());
    const VkPresentModeKHR presentMode = ChoosePresentMode(modes);

    VkExtent2D extent{};
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
        extent = capabilities.currentExtent;
    else
    {
        extent.width = std::clamp(
            requestedWidth,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width);
        extent.height = std::clamp(
            requestedHeight,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height);
    }

    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
    {
        SetError("swapchain images do not support color attachment usage");
        return false;
    }

    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0)
        imageCount = std::min(imageCount, capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = selected.format;
    createInfo.imageColorSpace = selected.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
        : capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = swapchain;

    VkSwapchainKHR nextSwapchain = VK_NULL_HANDLE;
    const VkResult createResult = vkCreateSwapchainKHR(device, &createInfo, nullptr, &nextSwapchain);
    if (createResult != VK_SUCCESS)
    {
        SetError("vkCreateSwapchainKHR failed with VkResult " + std::to_string(static_cast<int>(createResult)));
        return false;
    }

    const VkSwapchainKHR oldSwapchain = swapchain;
    DestroySwapchainGraphicsResources();
    swapchain = nextSwapchain;
    if (oldSwapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device, oldSwapchain, nullptr);

    std::uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &actualCount, nullptr);
    swapchainImages.resize(actualCount);
    if (actualCount == 0
        || vkGetSwapchainImagesKHR(device, swapchain, &actualCount, swapchainImages.data()) != VK_SUCCESS)
    {
        SetError("swapchain image query failed");
        return false;
    }

    swapchainFormat = selected.format;
    swapchainExtent = extent;
    if (!CreateSwapchainGraphicsResources())
    {
        SetError("swapchain graphics resource creation failed");
        return false;
    }
    swapchainDirty = false;
    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Info,
        "VulkanPresenter: swapchain images=%u extent=%ux%u presentMode=%d",
        actualCount,
        extent.width,
        extent.height,
        static_cast<int>(presentMode));
    return true;
}

void MelonPrimeVulkanSurfacePresenter::DestroySwapchain()
{
    DestroySwapchainGraphicsResources();
    swapchainImages.clear();
    if (swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
    swapchainFormat = VK_FORMAT_UNDEFINED;
    swapchainExtent = {};
}

bool MelonPrimeVulkanSurfacePresenter::CreateSwapchainGraphicsResources()
{
    swapchainImageViews.resize(swapchainImages.size(), VK_NULL_HANDLE);
    swapchainFramebuffers.resize(swapchainImages.size(), VK_NULL_HANDLE);
    for (std::size_t index = 0; index < swapchainImages.size(); ++index)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages[index];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &swapchainImageViews[index]) != VK_SUCCESS)
            return false;
    }

    VkAttachmentDescription attachment{};
    attachment.format = swapchainFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference attachmentReference{};
    attachmentReference.attachment = 0;
    attachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &attachmentReference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
        return false;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertexShaderModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragmentShaderModule;
    shaderStages[1].pName = "main";

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(SurfaceVertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 3> attributes{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(SurfaceVertex, x))};
    attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(SurfaceVertex, u))};
    attributes[2] = {2, 0, VK_FORMAT_R32_SFLOAT, static_cast<std::uint32_t>(offsetof(SurfaceVertex, alpha))};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDescription;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    if (vkCreateGraphicsPipelines(
            device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS)
        return false;

    for (std::size_t index = 0; index < swapchainImageViews.size(); ++index)
    {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &swapchainImageViews[index];
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[index]) != VK_SUCCESS)
            return false;
    }
    return true;
}

void MelonPrimeVulkanSurfacePresenter::DestroySwapchainGraphicsResources()
{
    for (VkFramebuffer framebuffer : swapchainFramebuffers)
    {
        if (framebuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    swapchainFramebuffers.clear();
    if (graphicsPipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, graphicsPipeline, nullptr);
    if (renderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(device, renderPass, nullptr);
    graphicsPipeline = VK_NULL_HANDLE;
    renderPass = VK_NULL_HANDLE;
    for (VkImageView view : swapchainImageViews)
    {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device, view, nullptr);
    }
    swapchainImageViews.clear();
}

bool MelonPrimeVulkanSurfacePresenter::CreateCommandResources()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
    {
        SetError("present command pool creation failed");
        return false;
    }

    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = commandPool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &allocation, &commandBuffer) != VK_SUCCESS)
    {
        SetError("present command buffer allocation failed");
        return false;
    }

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailable) != VK_SUCCESS
        || vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinished) != VK_SUCCESS)
    {
        SetError("present semaphore creation failed");
        return false;
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device, &fenceInfo, nullptr, &submitFence) != VK_SUCCESS)
    {
        SetError("present fence creation failed");
        return false;
    }
    return true;
}

void MelonPrimeVulkanSurfacePresenter::DestroyCommandResources()
{
    if (submitFence != VK_NULL_HANDLE)
        vkDestroyFence(device, submitFence, nullptr);
    if (renderFinished != VK_NULL_HANDLE)
        vkDestroySemaphore(device, renderFinished, nullptr);
    if (imageAvailable != VK_NULL_HANDLE)
        vkDestroySemaphore(device, imageAvailable, nullptr);
    if (commandBuffer != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE)
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    if (commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(device, commandPool, nullptr);
    submitFence = VK_NULL_HANDLE;
    renderFinished = VK_NULL_HANDLE;
    imageAvailable = VK_NULL_HANDLE;
    commandBuffer = VK_NULL_HANDLE;
    commandPool = VK_NULL_HANDLE;
}

std::uint32_t MelonPrimeVulkanSurfacePresenter::FindMemoryType(
    std::uint32_t typeBits,
    VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
    {
        if ((typeBits & (1u << index)) != 0
            && (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties)
            return index;
    }
    return UINT32_MAX;
}

bool MelonPrimeVulkanSurfacePresenter::CreatePresentationResources()
{
    std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[4] = {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[5] = {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[6] = {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[7] = {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
        return false;

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3};
    poolSizes[2] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocationInfo.descriptorPool = descriptorPool;
    allocationInfo.descriptorSetCount = 1;
    allocationInfo.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &allocationInfo, &descriptorSet) != VK_SUCCESS)
        return false;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.size = sizeof(PresenterPushConstants);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        return false;

    const auto createShaderModule = [this](
        const unsigned char* bytes,
        std::size_t length,
        VkShaderModule& module) {
        std::vector<std::uint32_t> words((length + 3u) / 4u);
        std::memcpy(words.data(), bytes, length);
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = length;
        createInfo.pCode = words.data();
        return vkCreateShaderModule(device, &createInfo, nullptr, &module) == VK_SUCCESS;
    };
    if (!createShaderModule(
            melonDS_android_vulkan_surface_presenter_vert_spv,
            melonDS_android_vulkan_surface_presenter_vert_spv_len,
            vertexShaderModule)
        || !createShaderModule(
            melonDS_android_vulkan_surface_presenter_frag_spv,
            melonDS_android_vulkan_surface_presenter_frag_spv_len,
            fragmentShaderModule))
        return false;

    const auto createSampler = [this](VkFilter filter, VkSampler& sampler) {
        VkSamplerCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        createInfo.magFilter = filter;
        createInfo.minFilter = filter;
        createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.maxLod = 1.0f;
        return vkCreateSampler(device, &createInfo, nullptr, &sampler) == VK_SUCCESS;
    };
    if (!createSampler(VK_FILTER_NEAREST, nearestSampler)
        || !createSampler(VK_FILTER_LINEAR, linearSampler))
        return false;

    vertexCapacity = 128;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = vertexCapacity * sizeof(SurfaceVertex);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &vertexBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, vertexBuffer, &requirements);
    VkMemoryAllocateInfo memoryInfo{};
    memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryInfo.allocationSize = requirements.size;
    memoryInfo.memoryTypeIndex = FindMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memoryInfo.memoryTypeIndex == UINT32_MAX
        || vkAllocateMemory(device, &memoryInfo, nullptr, &vertexMemory) != VK_SUCCESS
        || vkBindBufferMemory(device, vertexBuffer, vertexMemory, 0) != VK_SUCCESS
        || vkMapMemory(device, vertexMemory, 0, bufferInfo.size, 0, &mappedVertexMemory) != VK_SUCCESS)
        return false;
    return true;
}

void MelonPrimeVulkanSurfacePresenter::DestroyPresentationResources()
{
    DestroyOverlayBuffer();
    if (mappedVertexMemory != nullptr)
        vkUnmapMemory(device, vertexMemory);
    mappedVertexMemory = nullptr;
    if (vertexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, vertexBuffer, nullptr);
    if (vertexMemory != VK_NULL_HANDLE)
        vkFreeMemory(device, vertexMemory, nullptr);
    if (nearestSampler != VK_NULL_HANDLE)
        vkDestroySampler(device, nearestSampler, nullptr);
    if (linearSampler != VK_NULL_HANDLE)
        vkDestroySampler(device, linearSampler, nullptr);
    if (vertexShaderModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, vertexShaderModule, nullptr);
    if (fragmentShaderModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, fragmentShaderModule, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (descriptorSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    vertexBuffer = VK_NULL_HANDLE;
    vertexMemory = VK_NULL_HANDLE;
    nearestSampler = VK_NULL_HANDLE;
    linearSampler = VK_NULL_HANDLE;
    vertexShaderModule = VK_NULL_HANDLE;
    fragmentShaderModule = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    descriptorSet = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    descriptorSetLayout = VK_NULL_HANDLE;
    vertexCapacity = 0;
    presentVertexCount = 0;
    presentScreenVertexCount = 0;
    overlayFirstVertex = 0;
}

bool MelonPrimeVulkanSurfacePresenter::UpdatePresentationDescriptors(
    VkImageView frameImageView,
    const VulkanCompositionInputs& inputs,
    VkBuffer overlayDescriptorBuffer,
    VkDeviceSize overlayBufferSize)
{
    if (frameImageView == VK_NULL_HANDLE
        || inputs.sourceImageView == VK_NULL_HANDLE
        || inputs.topPackedBuffer == VK_NULL_HANDLE
        || inputs.bottomPackedBuffer == VK_NULL_HANDLE
        || inputs.capture3dBuffer == VK_NULL_HANDLE)
        return false;

    const VkImageView previousTop = inputs.previousTopSourceImageView != VK_NULL_HANDLE
        ? inputs.previousTopSourceImageView
        : inputs.sourceImageView;
    const VkImageView previousBottom = inputs.previousBottomSourceImageView != VK_NULL_HANDLE
        ? inputs.previousBottomSourceImageView
        : inputs.sourceImageView;
    const VkSampler sampler = inputs.filtering == VulkanFilterMode::Nearest
        ? nearestSampler
        : linearSampler;
    std::array<VkDescriptorImageInfo, 4> imageInfos{};
    imageInfos[0] = {sampler, frameImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[1] = {VK_NULL_HANDLE, inputs.sourceImageView, VK_IMAGE_LAYOUT_GENERAL};
    imageInfos[2] = {VK_NULL_HANDLE, previousTop, VK_IMAGE_LAYOUT_GENERAL};
    imageInfos[3] = {VK_NULL_HANDLE, previousBottom, VK_IMAGE_LAYOUT_GENERAL};
    std::array<VkDescriptorBufferInfo, 4> bufferInfos{};
    bufferInfos[0] = {inputs.topPackedBuffer, 0, inputs.packedBufferSize};
    bufferInfos[1] = {inputs.bottomPackedBuffer, 0, inputs.packedBufferSize};
    bufferInfos[2] = {inputs.capture3dBuffer, 0, inputs.capture3dBufferSize};
    bufferInfos[3] = {overlayDescriptorBuffer, 0, overlayBufferSize};
    std::array<VkWriteDescriptorSet, 8> writes{};
    const auto setImageWrite = [&](std::size_t index, std::uint32_t binding, VkDescriptorType type, VkDescriptorImageInfo* info) {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = descriptorSet;
        writes[index].dstBinding = binding;
        writes[index].descriptorCount = 1;
        writes[index].descriptorType = type;
        writes[index].pImageInfo = info;
    };
    const auto setBufferWrite = [&](std::size_t index, std::uint32_t binding, VkDescriptorBufferInfo* info) {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = descriptorSet;
        writes[index].dstBinding = binding;
        writes[index].descriptorCount = 1;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[index].pBufferInfo = info;
    };
    setImageWrite(0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[0]);
    setImageWrite(1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imageInfos[1]);
    setBufferWrite(2, 2, &bufferInfos[0]);
    setBufferWrite(3, 3, &bufferInfos[1]);
    setImageWrite(4, 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imageInfos[2]);
    setBufferWrite(5, 5, &bufferInfos[2]);
    setImageWrite(6, 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imageInfos[3]);
    setBufferWrite(7, 7, &bufferInfos[3]);
    vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return true;
}

bool MelonPrimeVulkanSurfacePresenter::UpdatePresentationVertices(
    const std::vector<VulkanPresentRegion>& regions,
    bool includeOverlay)
{
    std::vector<SurfaceVertex> vertices;
    vertices.reserve((regions.size() + (includeOverlay ? 1u : 0u)) * 6u);
    const auto ndcX = [this](float x) {
        return (x * 2.0f / static_cast<float>(swapchainExtent.width)) - 1.0f;
    };
    const auto ndcY = [this](float y) {
        return (y * 2.0f / static_cast<float>(swapchainExtent.height)) - 1.0f;
    };
    for (const VulkanPresentRegion& region : regions)
    {
        if (!region.enabled || region.width <= 0 || region.height <= 0)
            continue;
        std::array<float, 8> corners = region.corners;
        if (!region.hasTransformedCorners)
        {
            corners = {
                static_cast<float>(region.x), static_cast<float>(region.y),
                static_cast<float>(region.x + region.width), static_cast<float>(region.y),
                static_cast<float>(region.x), static_cast<float>(region.y + region.height),
                static_cast<float>(region.x + region.width), static_cast<float>(region.y + region.height),
            };
        }
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
            corners[corner * 2u] = ndcX(corners[corner * 2u]);
            corners[(corner * 2u) + 1u] = ndcY(corners[(corner * 2u) + 1u]);
        }
        const float uvTop = region.bottomScreen ? (194.0f / 386.0f) : 0.0f;
        const float uvBottom = region.bottomScreen ? 1.0f : (192.0f / 386.0f);
        const SurfaceVertex topLeft{corners[0], corners[1], 0.0f, uvTop, 1.0f};
        const SurfaceVertex topRight{corners[2], corners[3], 1.0f, uvTop, 1.0f};
        const SurfaceVertex bottomLeft{corners[4], corners[5], 0.0f, uvBottom, 1.0f};
        const SurfaceVertex bottomRight{corners[6], corners[7], 1.0f, uvBottom, 1.0f};
        vertices.insert(vertices.end(), {
            topLeft, bottomLeft, bottomRight,
            topLeft, bottomRight, topRight,
        });
    }
    presentScreenVertexCount = static_cast<std::uint32_t>(vertices.size());
    overlayFirstVertex = presentScreenVertexCount;
    if (includeOverlay)
    {
        const SurfaceVertex topLeft{-1.0f, -1.0f, 0.0f, 0.0f, 1.0f};
        const SurfaceVertex topRight{1.0f, -1.0f, 1.0f, 0.0f, 1.0f};
        const SurfaceVertex bottomLeft{-1.0f, 1.0f, 0.0f, 1.0f, 1.0f};
        const SurfaceVertex bottomRight{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        vertices.insert(vertices.end(), {
            topLeft, bottomLeft, bottomRight,
            topLeft, bottomRight, topRight,
        });
    }
    if (vertices.size() > vertexCapacity || mappedVertexMemory == nullptr)
        return false;
    if (!vertices.empty())
        std::memcpy(mappedVertexMemory, vertices.data(), vertices.size() * sizeof(SurfaceVertex));
    presentVertexCount = static_cast<std::uint32_t>(vertices.size());
    return presentScreenVertexCount > 0;
}

void MelonPrimeVulkanSurfacePresenter::DestroyOverlayBuffer()
{
    if (mappedOverlayMemory != nullptr)
        vkUnmapMemory(device, overlayMemory);
    if (overlayBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, overlayBuffer, nullptr);
    if (overlayMemory != VK_NULL_HANDLE)
        vkFreeMemory(device, overlayMemory, nullptr);
    overlayBuffer = VK_NULL_HANDLE;
    overlayMemory = VK_NULL_HANDLE;
    mappedOverlayMemory = nullptr;
    overlayCapacity = 0;
    overlayDataSize = 0;
    overlayWidth = 0;
    overlayHeight = 0;
}

bool MelonPrimeVulkanSurfacePresenter::UpdateOverlayBuffer(const VulkanOverlayFrame* overlay)
{
    overlayDataSize = 0;
    overlayWidth = 0;
    overlayHeight = 0;
    if (overlay == nullptr || !overlay->IsValid())
        return true;

    const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(overlay->width)
        * static_cast<VkDeviceSize>(overlay->height) * sizeof(std::uint32_t);
    if (overlayBuffer == VK_NULL_HANDLE || overlayCapacity < requiredSize)
    {
        DestroyOverlayBuffer();
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = requiredSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &overlayBuffer) != VK_SUCCESS)
            return false;

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, overlayBuffer, &requirements);
        VkMemoryAllocateInfo memoryInfo{};
        memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryInfo.allocationSize = requirements.size;
        memoryInfo.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memoryInfo.memoryTypeIndex == UINT32_MAX
            || vkAllocateMemory(device, &memoryInfo, nullptr, &overlayMemory) != VK_SUCCESS
            || vkBindBufferMemory(device, overlayBuffer, overlayMemory, 0) != VK_SUCCESS
            || vkMapMemory(device, overlayMemory, 0, requiredSize, 0, &mappedOverlayMemory) != VK_SUCCESS)
        {
            DestroyOverlayBuffer();
            return false;
        }
        overlayCapacity = requiredSize;
    }

    const std::size_t destinationStride = static_cast<std::size_t>(overlay->width) * sizeof(std::uint32_t);
    auto* destination = static_cast<std::uint8_t*>(mappedOverlayMemory);
    const auto* source = static_cast<const std::uint8_t*>(overlay->pixels);
    for (std::uint32_t y = 0; y < overlay->height; ++y)
    {
        std::memcpy(
            destination + static_cast<std::size_t>(y) * destinationStride,
            source + static_cast<std::size_t>(y) * overlay->rowBytes,
            destinationStride);
    }
    overlayDataSize = requiredSize;
    overlayWidth = overlay->width;
    overlayHeight = overlay->height;
    return true;
}

bool MelonPrimeVulkanSurfacePresenter::RecoverSwapchain(const char* stage)
{
    swapchainDirty = true;
    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Warn,
        "VulkanPresenter: swapchain recovery requested stage=%s",
        stage);
    return false;
}

bool MelonPrimeVulkanSurfacePresenter::Present(
    VulkanFrame* frame,
    MelonPrimeVulkanOutput& output,
    const VulkanCompositionInputs& inputs,
    std::uint32_t sourceWidth,
    std::uint32_t sourceHeight,
    const std::vector<VulkanPresentRegion>& regions,
    const VulkanOverlayFrame* overlay)
{
    if (!initialized || frame == nullptr || requestedWidth == 0 || requestedHeight == 0)
        return false;
    if (!output.waitForFrame(frame, UINT64_MAX))
    {
        SetError("compositor frame wait failed");
        return false;
    }

    if (swapchainDirty)
    {
        if (vkWaitForFences(device, 1, &submitFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS
            || !CreateSwapchain())
            return false;
    }
    if (swapchain == VK_NULL_HANDLE)
        return false;

    if (vkWaitForFences(device, 1, &submitFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return false;
    if (submittedFrame != nullptr)
        submittedFrame->presentTimelineValue = 0;
    submittedFrame = nullptr;
    if (!UpdateOverlayBuffer(overlay))
        return false;

    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        device,
        swapchain,
        UINT64_MAX,
        imageAvailable,
        VK_NULL_HANDLE,
        &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
        return RecoverSwapchain("acquire/out-of-date");
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        return RecoverSwapchain("acquire/failure");

    const VkImage sourceImage = output.getFrameImage(frame);
    const VkImageView sourceImageView = output.getFrameImageView(frame);
    const VkBuffer overlayDescriptorBuffer = overlayDataSize > 0
        ? overlayBuffer
        : inputs.capture3dBuffer;
    const VkDeviceSize overlayDescriptorSize = overlayDataSize > 0
        ? overlayDataSize
        : inputs.capture3dBufferSize;
    if (sourceImage == VK_NULL_HANDLE || sourceImageView == VK_NULL_HANDLE
        || !UpdatePresentationDescriptors(
            sourceImageView, inputs, overlayDescriptorBuffer, overlayDescriptorSize)
        || !UpdatePresentationVertices(regions, overlayDataSize > 0))
        return false;

    vkResetCommandBuffer(commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        return false;


    VkImageMemoryBarrier sourceToSample{};
    sourceToSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sourceToSample.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    sourceToSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceToSample.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    sourceToSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sourceToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sourceToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sourceToSample.image = sourceImage;
    sourceToSample.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    sourceToSample.subresourceRange.levelCount = 1;
    sourceToSample.subresourceRange.layerCount = 1;
    VkBufferMemoryBarrier vertexBarrier{};
    vertexBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    vertexBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    vertexBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vertexBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vertexBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vertexBarrier.buffer = vertexBuffer;
    vertexBarrier.size = VK_WHOLE_SIZE;
    std::array<VkBufferMemoryBarrier, 2> bufferBarriers{};
    bufferBarriers[0] = vertexBarrier;
    std::uint32_t bufferBarrierCount = 1;
    if (overlayDataSize > 0)
    {
        VkBufferMemoryBarrier& overlayBarrier = bufferBarriers[bufferBarrierCount++];
        overlayBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        overlayBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        overlayBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        overlayBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        overlayBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        overlayBarrier.buffer = overlayBuffer;
        overlayBarrier.size = overlayDataSize;
    }
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0,
        0,
        nullptr,
        bufferBarrierCount,
        bufferBarriers.data(),
        1,
        &sourceToSample);

    VkClearValue clearValue{};
    clearValue.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.extent = swapchainExtent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = swapchainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    const VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr);
    PresenterPushConstants pushConstants{};
    pushConstants.drawMode = 1; // pinned BGR compositor atlas -> swapchain RGB
    pushConstants.scale = inputs.scale;
    pushConstants.rendererWidth = inputs.rendererWidth;
    pushConstants.rendererHeight = inputs.rendererHeight;
    pushConstants.packedStride = inputs.packedStride;
    pushConstants.filtering = static_cast<std::uint32_t>(inputs.filtering);
    pushConstants.previousTopSourceValid = inputs.previousTopSourceValid ? 1u : 0u;
    pushConstants.previousBottomSourceValid = inputs.previousBottomSourceValid ? 1u : 0u;
    pushConstants.captureSourceValid = inputs.capture3dSourceValid ? 1u : 0u;
    pushConstants.capture3dOwnerValid = inputs.capture3dOwnerValid ? 1u : 0u;
    pushConstants.capture3dOwnerIsTop = inputs.capture3dOwnerIsTop ? 1u : 0u;
    pushConstants.renderer3dOwnerValid = inputs.renderer3dOwnerValid ? 1u : 0u;
    pushConstants.renderer3dOwnerIsTop = inputs.renderer3dOwnerIsTop ? 1u : 0u;
    pushConstants.class4VramStructuredPair = inputs.class4VramStructuredPair ? 1u : 0u;
    pushConstants.class4NoAboveVramStructuredPair = inputs.class4NoAboveVramStructuredPair ? 1u : 0u;
    pushConstants.class4PreservePackedVramValid = inputs.class4PreservePackedVramValid ? 1u : 0u;
    pushConstants.class4PreservePackedVramScreenSwap = inputs.class4PreservePackedVramScreenSwap ? 1u : 0u;
    pushConstants.topStructuredHandoffNoCurrent3d = inputs.topStructuredHandoffNoCurrent3d ? 1u : 0u;
    pushConstants.bottomStructuredHandoffNoCurrent3d = inputs.bottomStructuredHandoffNoCurrent3d ? 1u : 0u;
    pushConstants.topStructuredHandoffSuppress3d = inputs.topStructuredHandoffSuppress3d ? 1u : 0u;
    pushConstants.bottomStructuredHandoffSuppress3d = inputs.bottomStructuredHandoffSuppress3d ? 1u : 0u;
    pushConstants.viewportWidth = static_cast<float>(swapchainExtent.width);
    pushConstants.viewportHeight = static_cast<float>(swapchainExtent.height);
    pushConstants.overlayWidth = overlayWidth;
    pushConstants.overlayHeight = overlayHeight;
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(pushConstants),
        &pushConstants);
    vkCmdDraw(commandBuffer, presentScreenVertexCount, 1, 0, 0);
    if (overlayDataSize > 0)
    {
        pushConstants.drawMode = 7;
        vkCmdPushConstants(
            commandBuffer,
            pipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(pushConstants),
            &pushConstants);
        vkCmdDraw(commandBuffer, 6, 1, overlayFirstVertex, 0);
    }
    vkCmdEndRenderPass(commandBuffer);

    sourceToSample.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceToSample.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    sourceToSample.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sourceToSample.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &sourceToSample);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        return false;

    if (vkResetFences(device, 1, &submitFence) != VK_SUCCESS)
        return false;
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished;
    {
        std::scoped_lock queueLock(melonDS::VulkanContext::Get().GetQueueLock());
        if (vkQueueSubmit(queue, 1, &submitInfo, submitFence) != VK_SUCCESS)
            return false;
    }
    submittedFrame = frame;
    frame->presentTimelineValue = 1;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    VkResult presentResult;
    {
        std::scoped_lock queueLock(melonDS::VulkanContext::Get().GetQueueLock());
        presentResult = vkQueuePresentKHR(queue, &presentInfo);
    }
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        return RecoverSwapchain("present/recreate");
    if (presentResult != VK_SUCCESS)
        return RecoverSwapchain("present/failure");
    return true;
}

bool MelonPrimeVulkanSurfacePresenter::WaitForFrameConsumption(
    VulkanFrame* frame,
    std::uint64_t timeoutNs)
{
    if (!initialized || frame == nullptr || frame != submittedFrame)
        return true;
    if (device == VK_NULL_HANDLE || submitFence == VK_NULL_HANDLE)
        return false;

    const VkResult waitResult = vkWaitForFences(device, 1, &submitFence, VK_TRUE, timeoutNs);
    if (waitResult != VK_SUCCESS)
        return false;

    frame->presentTimelineValue = 0;
    submittedFrame = nullptr;
    return true;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
