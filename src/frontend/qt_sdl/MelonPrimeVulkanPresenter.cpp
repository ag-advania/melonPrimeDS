/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#include "MelonPrimeVulkanPresenter.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <cstring>

#include <QWidget>

#include "MelonPrimeVulkanPresentShaders/MelonPrimeVulkanPresentShaderBlobs.h"
#include "Platform.h"
#include "VulkanContext.h"
#include "VulkanFeatureProbe.h"

using namespace melonDS;

namespace MelonPrime
{

namespace
{

constexpr VkFormat kLayerFormat = VK_FORMAT_B8G8R8A8_UNORM;
constexpr u32 kDescriptorSetsPerFrame = 64;
constexpr VkDeviceSize kMinStagingBytes = 4u * 1024u * 1024u;

const char* PresentModeName(VkPresentModeKHR mode) noexcept
{
    switch (mode)
    {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:                 return "IMMEDIATE";
    case VK_PRESENT_MODE_MAILBOX_KHR:                   return "MAILBOX";
    case VK_PRESENT_MODE_FIFO_KHR:                      return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:              return "FIFO_RELAXED";
    case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:     return "SHARED_DEMAND_REFRESH";
    case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR: return "SHARED_CONTINUOUS_REFRESH";
    default:                                            return "UNKNOWN";
    }
}

bool ListContains(const std::vector<VkPresentModeKHR>& modes, VkPresentModeKHR mode) noexcept
{
    return std::find(modes.begin(), modes.end(), mode) != modes.end();
}

const char* LayerDebugName(VulkanPresenter::Layer layer) noexcept
{
    switch (layer)
    {
    case VulkanPresenter::Layer::ScreenTop:    return "MelonPrime.Present.ScreenTop";
    case VulkanPresenter::Layer::ScreenBottom: return "MelonPrime.Present.ScreenBottom";
    case VulkanPresenter::Layer::Hud:          return "MelonPrime.Present.Hud";
    case VulkanPresenter::Layer::Osd:          return "MelonPrime.Present.Osd";
    default:                                   return "MelonPrime.Present.Layer";
    }
}

} // namespace


VulkanPresenter::~VulkanPresenter()
{
    Shutdown();
}


bool VulkanPresenter::Fail(const char* operation, VkResult result)
{
    Error = std::string(operation) + " failed: " + Vk::FormatResult(result);
    Failed = true;
    Platform::Log(Platform::LogLevel::Error, "[Vulkan] presenter: %s\n", Error.c_str());
    return false;
}


bool VulkanPresenter::Fail(std::string reason)
{
    Error = std::move(reason);
    Failed = true;
    Platform::Log(Platform::LogLevel::Error, "[Vulkan] presenter: %s\n", Error.c_str());
    return false;
}


void VulkanPresenter::SetVSync(bool enabled) noexcept
{
    if (VSyncRequested.exchange(enabled, std::memory_order_acq_rel) != enabled)
    {
        // The present mode is immutable once a swapchain exists, so a VSync
        // toggle is a swapchain recreation like any other.
        SwapchainDirty.store(true, std::memory_order_release);
    }
}


// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

bool VulkanPresenter::Init(QWidget* surfaceWidget)
{
    if (Initialized)
        return true;

    Failed = false;
    Error.clear();
    SurfaceWidget = surfaceWidget;

    if (!surfaceWidget)
        return Fail("internal error: the Vulkan presenter was given no surface widget");

    if (!AcquireContext())
        return false;

    if (!CreateSurface(surfaceWidget))
    {
        Shutdown();
        return false;
    }

    if (!CreateDeviceObjects())
    {
        Shutdown();
        return false;
    }

    const QSize size = surfaceWidget->size();
    const qreal dpr = surfaceWidget->devicePixelRatioF();
    const u32 width = static_cast<u32>(std::max(1, qRound(size.width() * dpr)));
    const u32 height = static_cast<u32>(std::max(1, qRound(size.height() * dpr)));

    if (!RecreateSwapchain(width, height))
    {
        // A window that is minimized at creation time is not a failure: the
        // swapchain is built at the first frame that has a non-zero extent.
        if (Failed)
        {
            Shutdown();
            return false;
        }
    }

    Initialized = true;
    return true;
}


bool VulkanPresenter::AcquireContext()
{
    Context = &VulkanContext::Get();

    // Presentation is mandatory here, unlike the settings-dialog probe: without
    // the surface instance extensions there is nothing to create a swapchain
    // from.
    if (!Context->Acquire(true))
    {
        Context = nullptr;
        return Fail(
            VulkanContext::Get().GetFailureReason().empty()
                ? std::string("the Vulkan instance could not be created for presentation")
                : VulkanContext::Get().GetFailureReason());
    }

    ContextAcquired = true;
    return true;
}


bool VulkanPresenter::CreateSurface(QWidget* widget)
{
    Surface = VulkanSurface::Create(
        Context->GetInstance(),
        Context->GetLibrary().Global().GetInstanceProcAddr,
        widget);

    if (!Surface.IsValid())
    {
        return Fail(Surface.Failure.empty()
            ? std::string("the platform Vulkan surface could not be created")
            : Surface.Failure);
    }
    return true;
}


bool VulkanPresenter::CreateDeviceObjects()
{
    // Re-selects the physical device now that a real surface exists. The
    // headless probe could not evaluate present support at all, so this is the
    // first point where the present queue family is known. Re-selecting is
    // cheap: no logical device has been created from the previous selection.
    if (!Context->SelectPhysicalDevice(Surface.Handle))
    {
        return Fail(Context->GetFailureReason().empty()
            ? std::string("no Vulkan device can present to this window")
            : Context->GetFailureReason());
    }

    if (!Device.Create(*Context, "Vulkan presenter"))
        return Fail(Device.GetFailureReason());

    if (!Device.Fns().CreateSwapchainKHR || !Device.Fns().AcquireNextImageKHR
        || !Device.Fns().QueuePresentKHR)
    {
        return Fail("the Vulkan device does not expose VK_KHR_swapchain entry points");
    }

    if (Device.GetPresentQueue() == VK_NULL_HANDLE)
        return Fail("the Vulkan device exposes no queue that can present to this window");

    if (!Frames.Create(Device, Device.GetMainQueueFamily(), Vk::FramesInFlight))
        return Fail("the Vulkan presenter's frame ring could not be created");

    if (!CreateSamplers())
        return false;
    if (!CreateDescriptorObjects())
        return false;
    if (!EnsureStaging(kMinStagingBytes))
        return false;

    return true;
}


bool VulkanPresenter::CreateSamplers()
{
    const Vk::DeviceDispatch& fns = Device.Fns();

    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // CLAMP_TO_EDGE on both axes: every layer is drawn with a UV rect that
    // stops at the texture edge, and a repeating or bordered sampler would wrap
    // the opposite screen edge into the letterbox seam under linear filtering.
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.maxLod = 0.0f;
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

    info.magFilter = VK_FILTER_NEAREST;
    info.minFilter = VK_FILTER_NEAREST;
    VkResult res = fns.CreateSampler(Device.GetHandle(), &info, nullptr, &SamplerNearest);
    if (res != VK_SUCCESS)
        return Fail("vkCreateSampler(nearest)", res);

    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    res = fns.CreateSampler(Device.GetHandle(), &info, nullptr, &SamplerLinear);
    if (res != VK_SUCCESS)
        return Fail("vkCreateSampler(linear)", res);

    Device.SetDebugName(VK_OBJECT_TYPE_SAMPLER, SamplerNearest, "MelonPrime.Present.SamplerNearest");
    Device.SetDebugName(VK_OBJECT_TYPE_SAMPLER, SamplerLinear, "MelonPrime.Present.SamplerLinear");
    return true;
}


bool VulkanPresenter::CreateDescriptorObjects()
{
    const Vk::DeviceDispatch& fns = Device.Fns();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    VkResult res = fns.CreateDescriptorSetLayout(Device.GetHandle(), &layoutInfo, nullptr, &SetLayout);
    if (res != VK_SUCCESS)
        return Fail("vkCreateDescriptorSetLayout", res);

    VkDescriptorPoolSize size{};
    size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    size.descriptorCount = kDescriptorSetsPerFrame;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kDescriptorSetsPerFrame;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &size;
    // No FREE_DESCRIPTOR_SET_BIT: whole pools are recycled with
    // vkResetDescriptorPool once per frame, which is both cheaper than freeing
    // sets individually and impossible to get wrong.

    for (VkDescriptorPool& pool : DescriptorPools)
    {
        res = fns.CreateDescriptorPool(Device.GetHandle(), &poolInfo, nullptr, &pool);
        if (res != VK_SUCCESS)
            return Fail("vkCreateDescriptorPool", res);
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = sizeof(Quad);
    static_assert(sizeof(Quad) == 64, "the Present push-constant block is four vec4s");

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &SetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push;

    res = fns.CreatePipelineLayout(Device.GetHandle(), &pipelineLayoutInfo, nullptr, &PipelineLayout);
    if (res != VK_SUCCESS)
        return Fail("vkCreatePipelineLayout", res);

    return true;
}


bool VulkanPresenter::CreateRenderPass()
{
    const Vk::DeviceDispatch& fns = Device.Fns();

    VkAttachmentDescription color{};
    color.format = SurfaceFormat.format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    // CLEAR rather than DONT_CARE: the clear is what draws the letterbox and
    // pillarbox bars, and it is also what guarantees no stale frame shows
    // through when the screen layout leaves part of the window uncovered.
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // UNDEFINED is correct precisely because loadOp is CLEAR: the previous
    // contents of the acquired image are not needed, so the driver may discard
    // them instead of preserving them across the layout transition.
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // The acquire semaphore is waited at COLOR_ATTACHMENT_OUTPUT, so the
    // implicit UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL transition performed by
    // the render pass must not run before that wait. This external dependency
    // is what ties the two together; without it the transition is unordered
    // with respect to the presentation engine's last read of the image.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    const VkResult res = fns.CreateRenderPass(Device.GetHandle(), &info, nullptr, &RenderPass);
    if (res != VK_SUCCESS)
        return Fail("vkCreateRenderPass", res);

    Device.SetDebugName(VK_OBJECT_TYPE_RENDER_PASS, RenderPass, "MelonPrime.Present.RenderPass");
    return true;
}


bool VulkanPresenter::CreatePipelines()
{
    const Vk::DeviceDispatch& fns = Device.Fns();

    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = sizeof(VulkanPresentShaders::PresentVertexSpirv);
    moduleInfo.pCode = VulkanPresentShaders::PresentVertexSpirv;
    VkResult res = fns.CreateShaderModule(Device.GetHandle(), &moduleInfo, nullptr, &vertexModule);
    if (res != VK_SUCCESS)
        return Fail("vkCreateShaderModule(Present.vert)", res);

    moduleInfo.codeSize = sizeof(VulkanPresentShaders::PresentFragmentSpirv);
    moduleInfo.pCode = VulkanPresentShaders::PresentFragmentSpirv;
    res = fns.CreateShaderModule(Device.GetHandle(), &moduleInfo, nullptr, &fragmentModule);
    if (res != VK_SUCCESS)
    {
        fns.DestroyShaderModule(Device.GetHandle(), vertexModule, nullptr);
        return Fail("vkCreateShaderModule(Present.frag)", res);
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    // No vertex buffers at all: the quad corners come from gl_VertexIndex.
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    // Culling is off because a rotated or mirrored screen layout flips the
    // quad's winding, and the layout matrix is data rather than something this
    // pipeline can predict.
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendState{};
    blendState.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendState;

    // Viewport and scissor are dynamic so a swapchain resize does not have to
    // rebuild the pipelines; only the framebuffers depend on the extent.
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = PipelineLayout;
    info.renderPass = RenderPass;
    info.subpass = 0;

    bool ok = true;
    res = fns.CreateGraphicsPipelines(
        Device.GetHandle(), VK_NULL_HANDLE, 1, &info, nullptr, &PipelineOpaque);
    if (res != VK_SUCCESS)
        ok = Fail("vkCreateGraphicsPipelines(opaque)", res);

    if (ok)
    {
        // Premultiplied alpha, matching QImage::Format_ARGB32_Premultiplied and
        // ScreenPanelGL's glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
        blendState.blendEnable = VK_TRUE;
        blendState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendState.colorBlendOp = VK_BLEND_OP_ADD;
        blendState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendState.alphaBlendOp = VK_BLEND_OP_ADD;

        res = fns.CreateGraphicsPipelines(
            Device.GetHandle(), VK_NULL_HANDLE, 1, &info, nullptr, &PipelineBlended);
        if (res != VK_SUCCESS)
            ok = Fail("vkCreateGraphicsPipelines(blended)", res);
    }

    // Shader modules are only needed while the pipelines are being created.
    fns.DestroyShaderModule(Device.GetHandle(), fragmentModule, nullptr);
    fns.DestroyShaderModule(Device.GetHandle(), vertexModule, nullptr);

    if (ok)
    {
        Device.SetDebugName(VK_OBJECT_TYPE_PIPELINE, PipelineOpaque, "MelonPrime.Present.Opaque");
        Device.SetDebugName(VK_OBJECT_TYPE_PIPELINE, PipelineBlended, "MelonPrime.Present.Blended");
    }
    return ok;
}


// ---------------------------------------------------------------------------
// Swapchain
// ---------------------------------------------------------------------------

bool VulkanPresenter::ChooseSurfaceFormat(VkSurfaceFormatKHR& out, std::string& reason) const
{
    const Vk::InstanceDispatch& fns = Device.InstanceFns();

    u32 count = 0;
    VkResult res = fns.GetPhysicalDeviceSurfaceFormatsKHR(
        Device.GetPhysicalDevice(), Surface.Handle, &count, nullptr);
    if (res != VK_SUCCESS || count == 0)
    {
        reason = "the surface reports no supported formats";
        return false;
    }

    std::vector<VkSurfaceFormatKHR> formats(count);
    res = fns.GetPhysicalDeviceSurfaceFormatsKHR(
        Device.GetPhysicalDevice(), Surface.Handle, &count, formats.data());
    if (res != VK_SUCCESS && res != VK_INCOMPLETE)
    {
        reason = "vkGetPhysicalDeviceSurfaceFormatsKHR failed: " + Vk::FormatResult(res);
        return false;
    }
    formats.resize(count);

    // Never assume: the format list is queried and matched, with two explicit
    // preferences and a documented fallback.
    //
    // BGRA/RGBA *UNORM* rather than *SRGB* on purpose. The composed frame is
    // already in the DS's display space, exactly as the software, OpenGL and
    // DX12 panels present it; an sRGB swapchain would have the driver apply an
    // extra encode on write and the picture would come out visibly brighter
    // than every other renderer in the same build.
    for (const VkSurfaceFormatKHR& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM
            && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            out = format;
            return true;
        }
    }
    for (const VkSurfaceFormatKHR& format : formats)
    {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM
            && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            out = format;
            return true;
        }
    }

    out = formats.front();
    Platform::Log(
        Platform::LogLevel::Warn,
        "[Vulkan] presenter: no 8-bit UNORM surface format offered; using format=%d colorSpace=%d\n",
        static_cast<int>(out.format),
        static_cast<int>(out.colorSpace));
    return true;
}


bool VulkanPresenter::ChoosePresentMode(
    const std::vector<VkPresentModeKHR>& available,
    VkPresentModeKHR& out,
    std::string& reason) const
{
    const bool vsync = VSyncRequested.load(std::memory_order_acquire);

    if (vsync)
    {
        // FIFO is the only mode the specification requires every surface to
        // support, and it is exactly "VSync on": one present per refresh, no
        // tearing.
        out = VK_PRESENT_MODE_FIFO_KHR;
        reason = "FIFO is the specification-guaranteed VSync mode";
        if (!ListContains(available, VK_PRESENT_MODE_FIFO_KHR))
        {
            // A surface without FIFO violates the specification. Rather than
            // trusting it, fall back to whatever it does report and say so.
            out = available.empty() ? VK_PRESENT_MODE_FIFO_KHR : available.front();
            reason = "the surface does not report the mandatory FIFO mode; using its first reported mode";
        }
        return true;
    }

    if (ListContains(available, VK_PRESENT_MODE_MAILBOX_KHR))
    {
        out = VK_PRESENT_MODE_MAILBOX_KHR;
        reason = "VSync off, MAILBOX supported (no tearing, no frame-rate cap)";
        return true;
    }
    if (ListContains(available, VK_PRESENT_MODE_IMMEDIATE_KHR))
    {
        out = VK_PRESENT_MODE_IMMEDIATE_KHR;
        reason = "VSync off, MAILBOX unsupported, IMMEDIATE supported";
        return true;
    }

    out = VK_PRESENT_MODE_FIFO_KHR;
    reason = "VSync off requested but the surface supports neither MAILBOX nor IMMEDIATE; "
             "presenting with FIFO, so VSync remains effectively on";
    return true;
}


bool VulkanPresenter::RecreateSwapchain(u32 requestedWidth, u32 requestedHeight)
{
    const Vk::InstanceDispatch& instanceFns = Device.InstanceFns();
    const Vk::DeviceDispatch& fns = Device.Fns();

    VkSurfaceCapabilitiesKHR caps{};
    VkResult res = instanceFns.GetPhysicalDeviceSurfaceCapabilitiesKHR(
        Device.GetPhysicalDevice(), Surface.Handle, &caps);
    if (res != VK_SUCCESS)
        return Fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", res);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu || extent.height == 0xFFFFFFFFu)
    {
        // The surface lets the application choose (Wayland). The widget's own
        // physical pixel size is the answer, clamped to what the surface allows.
        extent.width = std::clamp(requestedWidth, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(requestedHeight, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    if (extent.width == 0 || extent.height == 0)
    {
        // Minimized. A zero-extent swapchain is invalid, so no swapchain is
        // created and the frame is skipped; the dirty flag is put back so the
        // next frame retries once the window is restored.
        SwapchainDirty.store(true, std::memory_order_release);
        return false;
    }

    std::vector<VkPresentModeKHR> presentModes;
    {
        u32 count = 0;
        res = instanceFns.GetPhysicalDeviceSurfacePresentModesKHR(
            Device.GetPhysicalDevice(), Surface.Handle, &count, nullptr);
        if (res != VK_SUCCESS)
            return Fail("vkGetPhysicalDeviceSurfacePresentModesKHR", res);
        presentModes.resize(count);
        if (count > 0)
        {
            res = instanceFns.GetPhysicalDeviceSurfacePresentModesKHR(
                Device.GetPhysicalDevice(), Surface.Handle, &count, presentModes.data());
            if (res != VK_SUCCESS && res != VK_INCOMPLETE)
                return Fail("vkGetPhysicalDeviceSurfacePresentModesKHR", res);
            presentModes.resize(count);
        }
    }

    const bool vsyncRequested = VSyncRequested.load(std::memory_order_acquire);
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    std::string presentReason;
    ChoosePresentMode(presentModes, presentMode, presentReason);

    VkSurfaceFormatKHR format{};
    {
        std::string reason;
        if (!ChooseSurfaceFormat(format, reason))
            return Fail(std::move(reason));
    }

    // minImageCount + 1 so the application always owns one image while the
    // presentation engine owns another; without the extra image every acquire
    // blocks until the previous present has completed.
    u32 imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0)
        imageCount = std::min(imageCount, caps.maxImageCount);

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (const VkCompositeAlphaFlagBitsKHR candidate : {
             VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
             VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
             VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
             VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
    {
        if (caps.supportedCompositeAlpha & candidate)
        {
            compositeAlpha = candidate;
            break;
        }
    }

    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
        return Fail("the surface does not allow swapchain images to be used as colour attachments");

    // Every in-flight frame is drained before the old swapchain's views,
    // framebuffers and semaphores are destroyed. This is the one place in the
    // presenter that is allowed to wait for the device, and it is reached only
    // on a resize, a DPI change, a fullscreen transition or a VSync toggle --
    // never per frame. Resize events are coalesced into the SwapchainDirty flag
    // precisely so a resize drag lands here once per frame boundary and not
    // once per event.
    Frames.WaitIdle();
    DestroySwapchainObjects(true);

    VkSwapchainKHR oldSwapchain = Swapchain;

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = Surface.Handle;
    info.minImageCount = imageCount;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = compositeAlpha;
    info.presentMode = presentMode;
    // Clipped: pixels hidden by another window need not be rendered coherently.
    // Nothing here ever reads the presented image back, which is the only case
    // where clipping would be observable.
    info.clipped = VK_TRUE;
    info.oldSwapchain = oldSwapchain;

    const u32 mainFamily = Device.GetMainQueueFamily();
    const u32 presentFamily = Device.GetPresentQueueFamily();
    const u32 families[2] = {mainFamily, presentFamily};
    if (Device.RequiresPresentOwnershipTransfer())
    {
        // CONCURRENT rather than explicit release/acquire barrier pairs. The
        // presenter submits one command buffer per frame and the cost of
        // concurrent access on a two-family split is far below the cost of
        // getting an ownership transfer subtly wrong on the one path that has
        // no way to be tested on the common (universal-family) hardware.
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = families;
    }
    else
    {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    res = fns.CreateSwapchainKHR(Device.GetHandle(), &info, nullptr, &newSwapchain);

    // The old swapchain is retired by the call above whether it succeeded or
    // not, and the device is idle, so destroying it here is safe and required.
    if (oldSwapchain != VK_NULL_HANDLE)
        fns.DestroySwapchainKHR(Device.GetHandle(), oldSwapchain, nullptr);
    Swapchain = VK_NULL_HANDLE;

    if (res != VK_SUCCESS)
        return Fail("vkCreateSwapchainKHR", res);

    Swapchain = newSwapchain;
    SwapchainExtent = extent;

    const bool formatChanged = (SurfaceFormat.format != format.format);
    SurfaceFormat = format;

    if (RenderPass == VK_NULL_HANDLE || formatChanged)
    {
        if (RenderPass != VK_NULL_HANDLE)
        {
            // Both pipelines are compiled against the render pass, so a format
            // change invalidates all three together.
            fns.DestroyPipeline(Device.GetHandle(), PipelineBlended, nullptr);
            fns.DestroyPipeline(Device.GetHandle(), PipelineOpaque, nullptr);
            fns.DestroyRenderPass(Device.GetHandle(), RenderPass, nullptr);
            PipelineBlended = VK_NULL_HANDLE;
            PipelineOpaque = VK_NULL_HANDLE;
            RenderPass = VK_NULL_HANDLE;
        }
        if (!CreateRenderPass())
            return false;
        if (!CreatePipelines())
            return false;
    }

    u32 realImageCount = 0;
    res = fns.GetSwapchainImagesKHR(Device.GetHandle(), Swapchain, &realImageCount, nullptr);
    if (res != VK_SUCCESS || realImageCount == 0)
        return Fail("vkGetSwapchainImagesKHR", res == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : res);

    SwapchainImages.resize(realImageCount);
    res = fns.GetSwapchainImagesKHR(
        Device.GetHandle(), Swapchain, &realImageCount, SwapchainImages.data());
    if (res != VK_SUCCESS && res != VK_INCOMPLETE)
        return Fail("vkGetSwapchainImagesKHR", res);
    SwapchainImages.resize(realImageCount);

    SwapchainImageViews.assign(realImageCount, VK_NULL_HANDLE);
    SwapchainFramebuffers.assign(realImageCount, VK_NULL_HANDLE);
    RenderFinished.assign(realImageCount, VK_NULL_HANDLE);
    ImagesInFlight.assign(realImageCount, VK_NULL_HANDLE);

    for (u32 i = 0; i < realImageCount; ++i)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = SwapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = SurfaceFormat.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        res = fns.CreateImageView(Device.GetHandle(), &viewInfo, nullptr, &SwapchainImageViews[i]);
        if (res != VK_SUCCESS)
            return Fail("vkCreateImageView(swapchain)", res);

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = RenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &SwapchainImageViews[i];
        fbInfo.width = SwapchainExtent.width;
        fbInfo.height = SwapchainExtent.height;
        fbInfo.layers = 1;

        res = fns.CreateFramebuffer(Device.GetHandle(), &fbInfo, nullptr, &SwapchainFramebuffers[i]);
        if (res != VK_SUCCESS)
            return Fail("vkCreateFramebuffer", res);

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        res = fns.CreateSemaphore(Device.GetHandle(), &semInfo, nullptr, &RenderFinished[i]);
        if (res != VK_SUCCESS)
            return Fail("vkCreateSemaphore(renderFinished)", res);
    }

    // Layer textures that follow the surface size are rebuilt here, while the
    // device is still idle from the WaitIdle() above. Doing it anywhere else
    // would mean reallocating an image another in-flight frame might sample.
    if (!EnsureLayerImage(Layers[static_cast<std::size_t>(Layer::Hud)],
                          SwapchainExtent.width, SwapchainExtent.height,
                          LayerDebugName(Layer::Hud)))
        return false;
    if (!EnsureLayerImage(Layers[static_cast<std::size_t>(Layer::Osd)],
                          SwapchainExtent.width, SwapchainExtent.height,
                          LayerDebugName(Layer::Osd)))
        return false;

    VSyncApplied = vsyncRequested;
    PresentMode = presentMode;

    Platform::Log(
        Platform::LogLevel::Info,
        "[Vulkan] swapchain created extent=%ux%u images=%u format=%d "
        "Requested VSync=%s / Requested Present Mode=%s / Actual Present Mode=%s / Reason=%s\n",
        SwapchainExtent.width,
        SwapchainExtent.height,
        realImageCount,
        static_cast<int>(SurfaceFormat.format),
        vsyncRequested ? "on" : "off",
        vsyncRequested ? "FIFO" : "MAILBOX>IMMEDIATE",
        PresentModeName(PresentMode),
        presentReason.c_str());

    return true;
}


void VulkanPresenter::DestroySwapchainObjects(bool immediate)
{
    if (!Device.IsValid())
        return;

    // `immediate` is always true today: every caller has just drained the
    // device. The parameter exists so the contract is stated at the call site
    // rather than assumed here.
    if (!immediate)
        Frames.WaitIdle();

    const Vk::DeviceDispatch& fns = Device.Fns();
    VkDevice device = Device.GetHandle();

    for (VkSemaphore semaphore : RenderFinished)
    {
        if (semaphore != VK_NULL_HANDLE)
            fns.DestroySemaphore(device, semaphore, nullptr);
    }
    RenderFinished.clear();

    for (VkFramebuffer framebuffer : SwapchainFramebuffers)
    {
        if (framebuffer != VK_NULL_HANDLE)
            fns.DestroyFramebuffer(device, framebuffer, nullptr);
    }
    SwapchainFramebuffers.clear();

    for (VkImageView view : SwapchainImageViews)
    {
        if (view != VK_NULL_HANDLE)
            fns.DestroyImageView(device, view, nullptr);
    }
    SwapchainImageViews.clear();

    // The VkImages themselves belong to the swapchain and must not be
    // destroyed; only the views the presenter created are its own.
    SwapchainImages.clear();
    ImagesInFlight.clear();
}


// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

bool VulkanPresenter::EnsureLayerImage(
    LayerTexture& texture, u32 width, u32 height, const char* debugName)
{
    width = std::max(1u, width);
    height = std::max(1u, height);

    if (texture.Image.IsValid() && texture.Width == width && texture.Height == height)
        return true;

    if (texture.Image.IsValid())
    {
        // Reallocation only happens when the window size or the internal
        // resolution changed, never per frame, so draining is both correct and
        // affordable here. Vk::Image owns its memory and view privately and has
        // no handle-release API, so it cannot be routed through the deferred
        // destruction queue.
        Frames.WaitIdle();
        texture.Image.Destroy();
    }

    Vk::Image::CreateInfo info{};
    info.Format = kLayerFormat;
    info.Width = width;
    info.Height = height;
    info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.ViewType = VK_IMAGE_VIEW_TYPE_2D;
    info.DebugName = debugName;

    if (!texture.Image.Create(Device, info))
        return Fail(std::string("could not allocate the presentation image ") + debugName);

    texture.Width = width;
    texture.Height = height;
    texture.HasContent = false;
    return true;
}


bool VulkanPresenter::EnsureStaging(VkDeviceSize bytes)
{
    bytes = std::max(bytes, kMinStagingBytes);
    if (StagingCapacity >= bytes)
        return true;

    // Growth only: the ring is sized for the largest frame seen so far, so a
    // resolution change pays one reallocation and nothing after that.
    Frames.WaitIdle();

    for (std::size_t i = 0; i < Staging.size(); ++i)
    {
        Staging[i].Destroy();
        if (!Staging[i].Create(Device, bytes, "MelonPrime.Present.Staging"))
            return Fail("the Vulkan presenter's upload buffer could not be allocated");
    }

    StagingCapacity = bytes;
    return true;
}


VkDescriptorSet VulkanPresenter::AcquireDescriptorSet(VkImageView view, VkSampler sampler)
{
    const Vk::DeviceDispatch& fns = Device.Fns();
    VkDescriptorPool pool = DescriptorPools[Frames.GetFrameIndex()];

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &SetLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult res = fns.AllocateDescriptorSets(Device.GetHandle(), &allocInfo, &set);
    if (res != VK_SUCCESS)
    {
        Fail("vkAllocateDescriptorSets", res);
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    fns.UpdateDescriptorSets(Device.GetHandle(), 1, &write, 0, nullptr);
    return set;
}


// ---------------------------------------------------------------------------
// Frame path
// ---------------------------------------------------------------------------

bool VulkanPresenter::BeginFrame(u32 requestedWidth, u32 requestedHeight)
{
    if (!Initialized || Failed || !Device.IsValid())
        return false;
    if (FrameOpen)
        return Fail("BeginFrame() called while a frame was already open");

    const bool dirty = SwapchainDirty.exchange(false, std::memory_order_acq_rel);
    if (dirty || Swapchain == VK_NULL_HANDLE
        || VSyncApplied != VSyncRequested.load(std::memory_order_acquire))
    {
        if (!RecreateSwapchain(requestedWidth, requestedHeight))
            return false;
    }
    if (Swapchain == VK_NULL_HANDLE)
        return false;

    // Upload-ring growth requested by a previous frame. Applied here, before
    // any command buffer is open, because it destroys and reallocates the
    // buffers a recording frame would already reference.
    if (PendingStagingRequest > StagingCapacity)
    {
        const VkDeviceSize request = PendingStagingRequest;
        PendingStagingRequest = 0;
        if (!EnsureStaging(request))
            return false;
    }

    Vk::FrameContext* frame = Frames.BeginFrame();
    if (!frame)
        return Fail("the Vulkan presenter's frame ring could not begin a frame");

    const u32 frameIndex = Frames.GetFrameIndex();
    CurrentCommandBuffer = frame->CommandBuffer;

    // Both are safe to recycle now: BeginFrame() waited on this slot's fence,
    // so nothing the previous use of this slot recorded is still executing.
    Staging[frameIndex].Reset();
    Device.Fns().ResetDescriptorPool(Device.GetHandle(), DescriptorPools[frameIndex], 0);

    const VkResult res = Device.Fns().AcquireNextImageKHR(
        Device.GetHandle(),
        Swapchain,
        UINT64_MAX,
        frame->ImageAvailable,
        VK_NULL_HANDLE,
        &CurrentImageIndex);

    if (res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // No image was acquired and ImageAvailable was NOT signalled, so the
        // open command buffer is closed with an empty, dependency-free
        // submission. That keeps the ring's fence and frame numbering
        // consistent instead of leaving a slot recording forever.
        SwapchainDirty.store(true, std::memory_order_release);
        Frames.SubmitFrame(Device.GetMainQueue());
        CurrentCommandBuffer = VK_NULL_HANDLE;
        return false;
    }
    if (res == VK_SUBOPTIMAL_KHR)
    {
        // The image IS acquired and the semaphore IS signalled: this frame is
        // presented normally and the swapchain is rebuilt for the next one.
        SwapchainDirty.store(true, std::memory_order_release);
    }
    else if (res != VK_SUCCESS)
    {
        Frames.SubmitFrame(Device.GetMainQueue());
        CurrentCommandBuffer = VK_NULL_HANDLE;
        return Fail("vkAcquireNextImageKHR", res);
    }

    // A swapchain image may be handed out again while an earlier frame that
    // targeted it is still executing (more images than frames in flight). The
    // frame fence covers the slot, not the image, so the image gets its own
    // wait. The current slot's fence is skipped: BeginFrame() has just reset
    // it, and waiting on an unsignalled fence nothing will signal would hang.
    if (CurrentImageIndex < ImagesInFlight.size())
    {
        VkFence imageFence = ImagesInFlight[CurrentImageIndex];
        if (imageFence != VK_NULL_HANDLE && imageFence != frame->InFlightFence)
        {
            const VkResult waitRes = Device.Fns().WaitForFences(
                Device.GetHandle(), 1, &imageFence, VK_TRUE, UINT64_MAX);
            if (waitRes != VK_SUCCESS)
            {
                Frames.SubmitFrame(Device.GetMainQueue());
                CurrentCommandBuffer = VK_NULL_HANDLE;
                return Fail("vkWaitForFences(swapchain image)", waitRes);
            }
        }
        ImagesInFlight[CurrentImageIndex] = frame->InFlightFence;
    }

    FrameOpen = true;
    CompositionOpen = false;
    return true;
}


bool VulkanPresenter::UploadLayer(
    Layer layer, const void* pixels, u32 width, u32 height, std::size_t rowBytes)
{
    if (!FrameOpen || CompositionOpen || !pixels || width == 0 || height == 0)
        return false;

    LayerTexture& texture = Layers[static_cast<std::size_t>(layer)];

    // The surface-sized layers were allocated at swapchain creation; only the
    // screen layers follow the renderer's internal resolution and may need a
    // new image here.
    if (layer == Layer::ScreenTop || layer == Layer::ScreenBottom)
    {
        if (!EnsureLayerImage(texture, width, height, LayerDebugName(layer)))
            return false;
    }
    else if (!texture.Image.IsValid())
    {
        return false;
    }

    // A surface-sized layer may legitimately be handed content smaller than its
    // image (the OSD strip is only as tall as its messages). Larger is clamped:
    // copying past the image would be a validation error, and the caller's UV
    // rect is derived from the same clamped size.
    const u32 copyWidth = std::min(width, texture.Width);
    const u32 copyHeight = std::min(height, texture.Height);

    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(copyWidth) * copyHeight * 4u;

    Vk::StagingRing& staging = Staging[Frames.GetFrameIndex()];

    // bufferOffset must be a multiple of 4 (the texel size) and the driver's
    // optimalBufferCopyOffsetAlignment is respected on top of that so the copy
    // takes the fast path rather than a driver-side fixup.
    const VkDeviceSize alignment = std::max<VkDeviceSize>(
        4u, Device.GetLimits().optimalBufferCopyOffsetAlignment);

    VkDeviceSize offset = 0;
    void* mapped = staging.Allocate(bytes, alignment, offset);
    if (!mapped)
    {
        // The ring cannot be grown here: copies recorded earlier in this frame
        // already reference its buffer, so destroying it now would leave the
        // open command buffer pointing at freed memory. The request is recorded
        // instead and applied at the next frame boundary, where nothing is
        // recording. Doubling the shortfall means one frame is skipped, not a
        // sequence of them.
        PendingStagingRequest = std::max(PendingStagingRequest, (staging.GetUsed() + bytes) * 2u);
        Platform::Log(
            Platform::LogLevel::Warn,
            "[Vulkan] presenter: upload ring is %llu bytes, growing to %llu at the next frame\n",
            static_cast<unsigned long long>(staging.GetCapacity()),
            static_cast<unsigned long long>(PendingStagingRequest));
        return false;
    }

    // Packed tightly regardless of the source stride, so bufferRowLength can
    // stay 0 and no row-pitch alignment rule applies to the copy.
    const auto* src = static_cast<const u8*>(pixels);
    auto* dst = static_cast<u8*>(mapped);
    const std::size_t dstRow = static_cast<std::size_t>(copyWidth) * 4u;
    for (u32 y = 0; y < copyHeight; ++y)
        std::memcpy(dst + y * dstRow, src + y * rowBytes, dstRow);

    if (!staging.FlushWritten())
        return Fail("the Vulkan presenter could not flush its upload buffer");

    const VkImageLayout currentLayout = texture.Image.GetLayout();
    texture.Image.RecordLayoutTransition(
        CurrentCommandBuffer,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        currentLayout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        currentLayout == VK_IMAGE_LAYOUT_UNDEFINED
            ? 0
            : VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT);

    VkBufferImageCopy copy{};
    copy.bufferOffset = offset;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {copyWidth, copyHeight, 1};

    Device.Fns().CmdCopyBufferToImage(
        CurrentCommandBuffer,
        staging.GetHandle(),
        texture.Image.GetHandle(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copy);

    texture.Image.RecordLayoutTransition(
        CurrentCommandBuffer,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);

    texture.HasContent = true;
    return true;
}


void VulkanPresenter::BeginComposition()
{
    if (!FrameOpen || CompositionOpen)
        return;

    const Vk::DeviceDispatch& fns = Device.Fns();

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = RenderPass;
    info.framebuffer = SwapchainFramebuffers[CurrentImageIndex];
    info.renderArea.extent = SwapchainExtent;
    info.clearValueCount = 1;
    info.pClearValues = &clear;

    fns.CmdBeginRenderPass(CurrentCommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(SwapchainExtent.width);
    viewport.height = static_cast<float>(SwapchainExtent.height);
    viewport.maxDepth = 1.0f;
    fns.CmdSetViewport(CurrentCommandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = SwapchainExtent;
    fns.CmdSetScissor(CurrentCommandBuffer, 0, 1, &scissor);

    CompositionOpen = true;
}


void VulkanPresenter::DrawLayer(Layer layer, const Quad& quad, Blend blend, bool linearFilter)
{
    if (!CompositionOpen || Failed)
        return;

    const LayerTexture& texture = Layers[static_cast<std::size_t>(layer)];
    if (!texture.Image.IsValid() || !texture.HasContent)
        return;

    const VkSampler sampler = linearFilter ? SamplerLinear : SamplerNearest;
    const VkDescriptorSet set = AcquireDescriptorSet(texture.Image.GetView(), sampler);
    if (set == VK_NULL_HANDLE)
        return;

    const Vk::DeviceDispatch& fns = Device.Fns();
    fns.CmdBindPipeline(
        CurrentCommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        blend == Blend::Opaque ? PipelineOpaque : PipelineBlended);
    fns.CmdBindDescriptorSets(
        CurrentCommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        PipelineLayout,
        0, 1, &set,
        0, nullptr);
    fns.CmdPushConstants(
        CurrentCommandBuffer,
        PipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(Quad),
        &quad);
    fns.CmdDraw(CurrentCommandBuffer, 4, 1, 0, 0);
}


bool VulkanPresenter::EndFrame()
{
    if (!FrameOpen)
        return false;

    const Vk::DeviceDispatch& fns = Device.Fns();

    if (CompositionOpen)
    {
        fns.CmdEndRenderPass(CurrentCommandBuffer);
        CompositionOpen = false;
    }

    Vk::FrameContext* frame = Frames.GetCurrentFrame();
    const VkSemaphore signalSemaphore =
        CurrentImageIndex < RenderFinished.size()
            ? RenderFinished[CurrentImageIndex]
            : VK_NULL_HANDLE;

    const bool submitted = Frames.SubmitFrame(
        Device.GetMainQueue(),
        frame ? frame->ImageAvailable : VK_NULL_HANDLE,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        signalSemaphore);

    FrameOpen = false;
    CurrentCommandBuffer = VK_NULL_HANDLE;

    if (!submitted)
        return Fail("the Vulkan presenter could not submit its frame");

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    if (signalSemaphore != VK_NULL_HANDLE)
    {
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &signalSemaphore;
    }
    present.swapchainCount = 1;
    present.pSwapchains = &Swapchain;
    present.pImageIndices = &CurrentImageIndex;

    const VkResult res = fns.QueuePresentKHR(Device.GetPresentQueue(), &present);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        // Neither is an error: the frame reached the presentation engine (or
        // was correctly rejected because the surface changed size), and the
        // swapchain is rebuilt before the next one.
        SwapchainDirty.store(true, std::memory_order_release);
        return true;
    }
    if (res != VK_SUCCESS)
        return Fail("vkQueuePresentKHR", res);

    if (!FirstPresentLogged)
    {
        FirstPresentLogged = true;
        Platform::Log(
            Platform::LogLevel::Info,
            "[Vulkan] first frame presented extent=%ux%u presentMode=%s\n",
            SwapchainExtent.width,
            SwapchainExtent.height,
            PresentModeName(PresentMode));
    }
    return true;
}


// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

void VulkanPresenter::Shutdown() noexcept
{
    if (Device.IsValid())
    {
        // Teardown is the second sanctioned use of a full device drain: every
        // object below may still be referenced by work in flight, and there is
        // no cheaper way to be sure it is not.
        Frames.WaitIdle();

        const Vk::DeviceDispatch& fns = Device.Fns();
        VkDevice device = Device.GetHandle();

        DestroySwapchainObjects(true);

        if (Swapchain != VK_NULL_HANDLE)
        {
            fns.DestroySwapchainKHR(device, Swapchain, nullptr);
            Swapchain = VK_NULL_HANDLE;
        }

        for (LayerTexture& texture : Layers)
        {
            texture.Image.Destroy();
            texture.Width = 0;
            texture.Height = 0;
            texture.HasContent = false;
        }

        for (Vk::StagingRing& ring : Staging)
            ring.Destroy();
        StagingCapacity = 0;

        for (VkDescriptorPool& pool : DescriptorPools)
        {
            if (pool != VK_NULL_HANDLE)
            {
                fns.DestroyDescriptorPool(device, pool, nullptr);
                pool = VK_NULL_HANDLE;
            }
        }

        if (PipelineBlended != VK_NULL_HANDLE)
        {
            fns.DestroyPipeline(device, PipelineBlended, nullptr);
            PipelineBlended = VK_NULL_HANDLE;
        }
        if (PipelineOpaque != VK_NULL_HANDLE)
        {
            fns.DestroyPipeline(device, PipelineOpaque, nullptr);
            PipelineOpaque = VK_NULL_HANDLE;
        }
        if (PipelineLayout != VK_NULL_HANDLE)
        {
            fns.DestroyPipelineLayout(device, PipelineLayout, nullptr);
            PipelineLayout = VK_NULL_HANDLE;
        }
        if (SetLayout != VK_NULL_HANDLE)
        {
            fns.DestroyDescriptorSetLayout(device, SetLayout, nullptr);
            SetLayout = VK_NULL_HANDLE;
        }
        if (RenderPass != VK_NULL_HANDLE)
        {
            fns.DestroyRenderPass(device, RenderPass, nullptr);
            RenderPass = VK_NULL_HANDLE;
        }
        if (SamplerLinear != VK_NULL_HANDLE)
        {
            fns.DestroySampler(device, SamplerLinear, nullptr);
            SamplerLinear = VK_NULL_HANDLE;
        }
        if (SamplerNearest != VK_NULL_HANDLE)
        {
            fns.DestroySampler(device, SamplerNearest, nullptr);
            SamplerNearest = VK_NULL_HANDLE;
        }

        Frames.Destroy();
        Device.Destroy();
    }

    // The surface outlives the device and must be destroyed before the
    // instance reference is dropped.
    if (Context && Surface.IsValid())
    {
        VulkanSurface::Destroy(
            Context->GetInstance(),
            Context->GetLibrary().Global().GetInstanceProcAddr,
            Surface);
    }
    Surface = VulkanSurface::Surface{};

    if (ContextAcquired && Context)
    {
        Context->Release();
        ContextAcquired = false;
    }
    Context = nullptr;

    SurfaceWidget = nullptr;
    SwapchainExtent = VkExtent2D{0, 0};
    FrameOpen = false;
    CompositionOpen = false;
    Initialized = false;
    FirstPresentLogged = false;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
