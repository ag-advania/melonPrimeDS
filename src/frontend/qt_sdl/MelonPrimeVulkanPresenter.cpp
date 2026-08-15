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
#include <cstdint>
#include <cstring>

#include <QWidget>

#include "MelonPrimeVulkanPresentShaders/MelonPrimeVulkanPresentShaderBlobs.h"
#include "Platform.h"
#include "VulkanContext.h"
#include "VulkanFeatureProbe.h"
#include "VulkanPerf.h"

using namespace melonDS;

namespace MelonPrime
{

namespace
{

constexpr VkFormat kLayerFormat = VK_FORMAT_B8G8R8A8_UNORM;
constexpr u32 kDescriptorSetsPerFrame = 64;
constexpr std::size_t kPresenterSamplerCount = 2;
// Three renderer compositor slots, each exposing a top and bottom image view.
constexpr std::size_t kDirectViewCacheCount = 3 * 2;
constexpr std::size_t kDirectDescriptorSetCount = kDirectViewCacheCount * 2;
constexpr std::size_t kPersistentDescriptorSetCount =
    static_cast<std::size_t>(VulkanPresenter::Layer::Count) * kPresenterSamplerCount
    + kDirectDescriptorSetCount;
static_assert(kDirectViewCacheCount == 6);
static_assert(kDirectDescriptorSetCount == 12);
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
#ifdef VK_KHR_present_mode_fifo_latest_ready
    case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR:         return "FIFO_LATEST_READY";
#endif
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

void VulkanPresenter::Quiesce() noexcept
{
    if (Device.IsValid())
        Frames.WaitIdle();
}


const VulkanPresenter::DirectDescriptorCacheEntry*
VulkanPresenter::FindDirectDescriptor(
    VkImage image,
    VkImageView view,
    u64 resourceGeneration) const noexcept
{
    for (const DirectDescriptorCacheEntry& entry : DirectDescriptorCache)
    {
        if (entry.Valid && entry.Image == image && entry.View == view
            && entry.ResourceGeneration == resourceGeneration)
        {
            return &entry;
        }
    }
    return nullptr;
}


bool VulkanPresenter::UpdateDirectDescriptorSets(
    DirectDescriptorCacheEntry& entry,
    VkImage image,
    VkImageView view,
    u64 resourceGeneration)
{
    if (image == VK_NULL_HANDLE || view == VK_NULL_HANDLE
        || entry.Sets[0] == VK_NULL_HANDLE || entry.Sets[1] == VK_NULL_HANDLE)
    {
        return false;
    }

    VulkanPerf::ScopedCpuTimer descriptorTimer(VulkanPerf::CpuMetric::DescriptorUpdate);
    const auto descriptorStart = VulkanPerf::Clock::now();
    std::array<VkDescriptorImageInfo, kPresenterSamplerCount> imageInfos{};
    std::array<VkWriteDescriptorSet, kPresenterSamplerCount> writes{};
    for (std::size_t sampler = 0; sampler < kPresenterSamplerCount; ++sampler)
    {
        imageInfos[sampler].sampler = sampler == 0 ? SamplerNearest : SamplerLinear;
        imageInfos[sampler].imageView = view;
        imageInfos[sampler].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[sampler].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[sampler].dstSet = entry.Sets[sampler];
        writes[sampler].dstBinding = 0;
        writes[sampler].descriptorCount = 1;
        writes[sampler].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[sampler].pImageInfo = &imageInfos[sampler];
    }
    Device.Fns().UpdateDescriptorSets(
        Device.GetHandle(), static_cast<u32>(writes.size()), writes.data(), 0, nullptr);

    entry.Image = image;
    entry.View = view;
    entry.ResourceGeneration = resourceGeneration;
    entry.Valid = true;
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::DescriptorUpdateCount,
        static_cast<u64>(writes.size()));
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::PresenterDescriptorUpdateCount,
        static_cast<u64>(writes.size()));
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::DescriptorWriteCount,
        static_cast<u64>(writes.size()));
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::PresenterDescriptorCpuTimeNs,
        static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            VulkanPerf::Clock::now() - descriptorStart).count()));
    return true;
}


bool VulkanPresenter::EnsureDirectDescriptor(
    VkImage image,
    VkImageView view,
    u64 resourceGeneration)
{
    if (FindDirectDescriptor(image, view, resourceGeneration))
    {
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::PresenterDescriptorCacheHitCount);
        return true;
    }

    VulkanPerf::AddCounter(
        VulkanPerf::Counter::PresenterDescriptorCacheMissCount);
    DirectDescriptorCacheEntry* freeEntry = nullptr;
    for (DirectDescriptorCacheEntry& entry : DirectDescriptorCache)
    {
        if (!entry.Valid)
        {
            freeEntry = &entry;
            break;
        }
    }
    if (!freeEntry)
        return false;
    return UpdateDirectDescriptorSets(*freeEntry, image, view, resourceGeneration);
}


bool VulkanPresenter::PrepareDirectOutputDescriptors(
    const melonDS::VulkanPresentedFrame& frame)
{
    if (!Initialized || Failed || !Device.IsValid())
        return false;
    if (!frame.HasDirectSampledOutput() || frame.ResourceGeneration == 0)
        return true;

    if (CachedDirectResourceGeneration != frame.ResourceGeneration)
    {
        bool hadEntries = false;
        for (const DirectDescriptorCacheEntry& entry : DirectDescriptorCache)
            hadEntries |= entry.Valid;
        if (hadEntries)
            Frames.WaitIdle();
        for (DirectDescriptorCacheEntry& entry : DirectDescriptorCache)
        {
            entry.Image = VK_NULL_HANDLE;
            entry.View = VK_NULL_HANDLE;
            entry.ResourceGeneration = 0;
            entry.Valid = false;
        }
        CachedDirectResourceGeneration = frame.ResourceGeneration;
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::PresenterDescriptorCacheInvalidateCount);
    }

    // The compositor publishes one top and one bottom view per output slot.
    // If an unexpected seventh view appears, UploadLayerFromImage() uses the
    // per-frame transient pool instead of failing the frame.
    EnsureDirectDescriptor(
        frame.DirectImageTop, frame.DirectImageViewTop, frame.ResourceGeneration);
    EnsureDirectDescriptor(
        frame.DirectImageBottom, frame.DirectImageViewBottom, frame.ResourceGeneration);
    return true;
}


void VulkanPresenter::InvalidateDirectDescriptorCache() noexcept
{
    for (DirectDescriptorCacheEntry& entry : DirectDescriptorCache)
    {
        entry.Image = VK_NULL_HANDLE;
        entry.View = VK_NULL_HANDLE;
        entry.ResourceGeneration = 0;
        entry.Valid = false;
    }
    CachedDirectResourceGeneration = 0;
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::PresenterDescriptorCacheInvalidateCount);
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
    const bool sharedDeviceExists = melonDS::VulkanDevice::HasSharedDevice(*Context);
    if (!sharedDeviceExists && !Context->SelectPhysicalDevice(Surface.Handle))
    {
        return Fail(Context->GetFailureReason().empty()
            ? std::string("no Vulkan device can present to this window")
            : Context->GetFailureReason());
    }

    // Both vendor low-latency extensions are asked for unconditionally rather
    // than from the current setting, because a device extension can only be
    // added at vkCreateDevice time. Requesting them is free and changes nothing
    // on its own: VK_NV_low_latency2 does nothing until vkSetLatencySleepModeNV
    // turns pacing on, and VK_AMD_anti_lag does nothing until
    // vkAntiLagUpdateAMD is called. That is precisely what lets the user toggle
    // either one at runtime without rebuilding the device or the swapchain. On
    // hardware that lacks one, VulkanDevice records the reason and creates the
    // identical device it would have created anyway.
    melonDS::VulkanLowLatencyRequest lowLatency;
    lowLatency.NvLowLatency2 = true;
    lowLatency.AmdAntiLag = true;
    lowLatency.GenericPresentTiming = true;

    if (!Device.Create(*Context, "Vulkan presenter", lowLatency))
        return Fail(Device.GetFailureReason());

    if (sharedDeviceExists && !Device.ResolvePresentSupport(Surface.Handle))
        return Fail(Device.GetFailureReason());

    // Neither Initialize() failing is an error: both report why through
    // GetUnavailableReason(), which LogLowLatencyState() prints.
    Reflex.Initialize(Device);
    AntiLag.Initialize(Device);
    PresentPacer.Initialize(Device, Surface.Handle);

    if (!Device.Fns().CreateSwapchainKHR || !Device.Fns().AcquireNextImageKHR
        || !Device.Fns().QueuePresentKHR)
    {
        return Fail("the Vulkan device does not expose VK_KHR_swapchain entry points");
    }

    if (Device.GetPresentQueue() == VK_NULL_HANDLE)
        return Fail("the Vulkan device exposes no queue that can present to this window");

    if (!Frames.Create(Device, Device.GetMainQueueFamily(), Vk::FramesInFlight))
        return Fail("the Vulkan presenter's frame ring could not be created");
    VulkanPerf::SetCounter(
        VulkanPerf::Counter::VulkanPresenterFramesInFlight,
        Frames.GetFramesInFlight());

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

    VkDescriptorPoolSize persistentSize{};
    persistentSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    persistentSize.descriptorCount = static_cast<u32>(kPersistentDescriptorSetCount);

    VkDescriptorPoolCreateInfo persistentPoolInfo{};
    persistentPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    persistentPoolInfo.maxSets = static_cast<u32>(kPersistentDescriptorSetCount);
    persistentPoolInfo.poolSizeCount = 1;
    persistentPoolInfo.pPoolSizes = &persistentSize;
    res = fns.CreateDescriptorPool(
        Device.GetHandle(), &persistentPoolInfo, nullptr, &PersistentDescriptorPool);
    if (res != VK_SUCCESS)
        return Fail("vkCreateDescriptorPool(persistent)", res);

    std::array<VkDescriptorSetLayout, kPersistentDescriptorSetCount> setLayouts{};
    std::array<VkDescriptorSet, kPersistentDescriptorSetCount> sets{};
    setLayouts.fill(SetLayout);
    VkDescriptorSetAllocateInfo persistentAllocInfo{};
    persistentAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    persistentAllocInfo.descriptorPool = PersistentDescriptorPool;
    persistentAllocInfo.descriptorSetCount = static_cast<u32>(sets.size());
    persistentAllocInfo.pSetLayouts = setLayouts.data();
    res = fns.AllocateDescriptorSets(
        Device.GetHandle(), &persistentAllocInfo, sets.data());
    if (res != VK_SUCCESS)
        return Fail("vkAllocateDescriptorSets(persistent)", res);
    for (std::size_t layer = 0; layer < static_cast<std::size_t>(Layer::Count); ++layer)
    {
        for (std::size_t sampler = 0; sampler < kPresenterSamplerCount; ++sampler)
            LayerDescriptorSets[layer][sampler] = sets[layer * kPresenterSamplerCount + sampler];
    }
    const std::size_t directSetBase =
        static_cast<std::size_t>(Layer::Count) * kPresenterSamplerCount;
    for (std::size_t viewIndex = 0; viewIndex < DirectDescriptorCache.size(); ++viewIndex)
    {
        DirectDescriptorCache[viewIndex].Sets[0] =
            sets[directSetBase + viewIndex * kPresenterSamplerCount];
        DirectDescriptorCache[viewIndex].Sets[1] =
            sets[directSetBase + viewIndex * kPresenterSamplerCount + 1];
    }
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::DescriptorCreateCount,
        static_cast<u64>(kPersistentDescriptorSetCount));
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::PresenterDescriptorPersistentCreateCount,
        static_cast<u64>(kPersistentDescriptorSetCount));

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
        if (ListContains(available, VK_PRESENT_MODE_FIFO_KHR))
        {
            out = VK_PRESENT_MODE_FIFO_KHR;
            reason = "VSync on: FIFO";
            return true;
        }

        // A surface without FIFO violates the specification. Keep the
        // renderer usable if it reports another mode, but make the anomaly
        // explicit instead of claiming VSync is active.
        if (!available.empty())
        {
            out = available.front();
            reason = "VSync on: mandatory FIFO was not reported; using first available mode";
            return true;
        }
        return false;
    }

    // IMMEDIATE is the only present mode whose semantics directly match
    // "VSync off": it does not wait for VBlank and may tear.
    if (ListContains(available, VK_PRESENT_MODE_IMMEDIATE_KHR))
    {
        out = VK_PRESENT_MODE_IMMEDIATE_KHR;
        reason = "VSync off: IMMEDIATE supported";
        return true;
    }

    // MAILBOX remains the low-latency fallback when IMMEDIATE is unavailable.
    // It replaces queued frames with the newest one, but is still VBlank-bound
    // and tear-free, so it must not be advertised as literal VSync-off.
    if (ListContains(available, VK_PRESENT_MODE_MAILBOX_KHR))
    {
        out = VK_PRESENT_MODE_MAILBOX_KHR;
        reason = "VSync off: IMMEDIATE unavailable; using MAILBOX tear-free fallback";
        return true;
    }

    if (ListContains(available, VK_PRESENT_MODE_FIFO_KHR))
    {
        out = VK_PRESENT_MODE_FIFO_KHR;
        reason = "VSync off requested but only FIFO-compatible path is available";
        return true;
    }

    if (!available.empty())
    {
        out = available.front();
        reason = "No preferred present mode available; using first reported mode";
        return true;
    }

    return false;
}


bool VulkanPresenter::RecreateSwapchain(u32 requestedWidth, u32 requestedHeight)
{
    const Vk::InstanceDispatch& instanceFns = Device.InstanceFns();
    const Vk::DeviceDispatch& fns = Device.Fns();

    VkSurfaceCapabilitiesKHR caps{};
    if (!PresentPacer.QuerySurfaceCapabilities(caps))
        return Fail("the Vulkan surface capability query failed");
    VkResult res = VK_SUCCESS;

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
    if (vsyncRequested && PresentPacer.ShouldUseFifoLatestReady()
        && ListContains(presentModes, VK_PRESENT_MODE_FIFO_LATEST_READY_KHR))
    {
        presentMode = VK_PRESENT_MODE_FIFO_LATEST_READY_KHR;
        presentReason = "VSync on: verified present-timing path selected FIFO_LATEST_READY";
    }
    else if (!ChoosePresentMode(presentModes, presentMode, presentReason))
    {
        return Fail("the surface reported no Vulkan present modes");
    }

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

    // Reflex is scoped to a swapchain, so the old one is surrendered before it
    // is retired below. This also drops any in-flight Reflex frame, which is
    // correct: its presentID belongs to a swapchain that is going away.
    Reflex.SetSwapchain(VK_NULL_HANDLE);
    PresentPacer.OnSwapchainDestroyed();

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.flags = PresentPacer.GetSwapchainCreateFlags();
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

    // VK_NV_low_latency2 requires the swapchain itself to opt in: without
    // VkSwapchainLatencyCreateInfoNV::latencyModeEnable, vkSetLatencySleepModeNV
    // and the markers have nothing to attach to. It is set whenever the
    // extension exists rather than only when Reflex is currently switched on,
    // so toggling the setting at runtime does not force a swapchain rebuild.
    // The struct must outlive the vkCreateSwapchainKHR call, hence this scope.
    VkSwapchainLatencyCreateInfoNV latencyInfo{};
    if (Reflex.WantsSwapchainLatencyMode())
    {
        latencyInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV;
        latencyInfo.latencyModeEnable = VK_TRUE;
        latencyInfo.pNext = info.pNext;
        info.pNext = &latencyInfo;
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

    // Sleep mode is swapchain state and does not survive recreation, so this
    // re-arms pacing on the new one at whatever mode the user currently has
    // selected.
    Reflex.SetSwapchain(Swapchain);
    // imageCount is what was requested; the driver may give more. It is only
    // used to size the optional timing-results queue, where erring low simply
    // means the pacer grows the queue once after the first full event.
    PresentPacer.OnSwapchainCreated(Swapchain, presentMode, imageCount);

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
    VulkanPerf::SetCounter(
        VulkanPerf::Counter::VulkanSwapchainImageCount, realImageCount);
    VulkanPerf::SetCounter(
        VulkanPerf::Counter::VulkanPresentMode,
        static_cast<u64>(PresentMode));

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
    if (!EnsureLayerImage(Layer::Hud, Layers[static_cast<std::size_t>(Layer::Hud)],
                          SwapchainExtent.width, SwapchainExtent.height,
                          LayerDebugName(Layer::Hud)))
        return false;
    if (!EnsureLayerImage(Layer::Osd, Layers[static_cast<std::size_t>(Layer::Osd)],
                          SwapchainExtent.width, SwapchainExtent.height,
                          LayerDebugName(Layer::Osd)))
        return false;

    VSyncApplied = vsyncRequested;
    PresentMode = presentMode;

    std::string availableModeNames;
    for (VkPresentModeKHR mode : presentModes)
    {
        if (!availableModeNames.empty())
            availableModeNames += ',';
        const char* modeName = PresentModeName(mode);
        availableModeNames += modeName;
        if (std::strcmp(modeName, "UNKNOWN") == 0)
        {
            availableModeNames += '(';
            availableModeNames += std::to_string(static_cast<int>(mode));
            availableModeNames += ')';
        }
    }

    Platform::Log(
        Platform::LogLevel::Info,
        "[Vulkan] presentation: requested-vsync=%s available-present-modes=%s "
        "selected-present-mode=%s swapchain-images=%u extent=%ux%u format=%d "
        "window-mode=%s reason=%s; VRR actual state is driver/display controlled\n",
        vsyncRequested ? "on" : "off",
        availableModeNames.empty() ? "none" : availableModeNames.c_str(),
        PresentModeName(PresentMode),
        realImageCount,
        SwapchainExtent.width,
        SwapchainExtent.height,
        static_cast<int>(SurfaceFormat.format),
        WindowFullscreen.load(std::memory_order_acquire) ? "fullscreen" : "windowed",
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
    Layer layer, LayerTexture& texture, u32 width, u32 height, const char* debugName)
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
    return UpdateLayerDescriptorSets(layer);
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


bool VulkanPresenter::UpdateLayerDescriptorSets(Layer layer)
{
    VulkanPerf::ScopedCpuTimer descriptorTimer(VulkanPerf::CpuMetric::DescriptorUpdate);
    const Vk::DeviceDispatch& fns = Device.Fns();
    const LayerTexture& texture = Layers[static_cast<std::size_t>(layer)];
    if (!texture.Image.IsValid())
        return false;

    std::array<VkDescriptorImageInfo, kPresenterSamplerCount> imageInfos{};
    std::array<VkWriteDescriptorSet, kPresenterSamplerCount> writes{};
    for (std::size_t sampler = 0; sampler < kPresenterSamplerCount; ++sampler)
    {
        imageInfos[sampler].sampler = sampler == 0 ? SamplerNearest : SamplerLinear;
        imageInfos[sampler].imageView = texture.Image.GetView();
        imageInfos[sampler].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[sampler].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[sampler].dstSet = LayerDescriptorSets[static_cast<std::size_t>(layer)][sampler];
        writes[sampler].dstBinding = 0;
        writes[sampler].descriptorCount = 1;
        writes[sampler].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[sampler].pImageInfo = &imageInfos[sampler];
        if (writes[sampler].dstSet == VK_NULL_HANDLE)
            return false;
    }

    fns.UpdateDescriptorSets(
        Device.GetHandle(), static_cast<u32>(writes.size()), writes.data(), 0, nullptr);
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::DescriptorUpdateCount,
        static_cast<u64>(writes.size()));
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::PresenterDescriptorUpdateCount,
        static_cast<u64>(writes.size()));
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::DescriptorWriteCount,
        static_cast<u64>(writes.size()));
    return true;
}


VkDescriptorSet VulkanPresenter::AcquireDescriptorSet(Layer layer, bool linearFilter) const noexcept
{
    const LayerTexture& texture = Layers[static_cast<std::size_t>(layer)];
    if (texture.UsesDirect)
        return texture.DirectDescriptorSets[linearFilter ? 1 : 0];
    return LayerDescriptorSets[static_cast<std::size_t>(layer)][linearFilter ? 1 : 0];
}


// ---------------------------------------------------------------------------
// Frame path
// ---------------------------------------------------------------------------

bool VulkanPresenter::BeginFrame(u32 requestedWidth, u32 requestedHeight)
{
    VulkanPerf::ScopedCpuTimer beginTimer(VulkanPerf::CpuMetric::PresentBeginTotal);
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

    // These are presenter gauges, not per-report counters. Refresh them on
    // every frame as well as at creation so the one-Hz perf line remains
    // meaningful after VulkanPerf resets its windowed counters.
    VulkanPerf::SetCounter(
        VulkanPerf::Counter::VulkanSwapchainImageCount,
        static_cast<u64>(SwapchainImages.size()));
    VulkanPerf::SetCounter(
        VulkanPerf::Counter::VulkanPresenterFramesInFlight,
        Frames.GetFramesInFlight());
    VulkanPerf::SetCounter(
        VulkanPerf::Counter::VulkanPresentMode,
        static_cast<u64>(PresentMode));

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

    const bool presenterFencePending = Frames.NextFrameSlotHasPendingSubmission();
    const bool perfEnabled = VulkanPerf::IsEnabled();
    const VulkanPerf::Clock::time_point presenterFenceWaitStart = perfEnabled
        ? VulkanPerf::Clock::now()
        : VulkanPerf::Clock::time_point{};
    Vk::FrameContext* frame = Frames.BeginFrame();
    if (perfEnabled && presenterFencePending)
    {
        const u64 waitNs = static_cast<u64>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(
                VulkanPerf::Clock::now() - presenterFenceWaitStart).count());
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::VulkanPresenterFrameFenceWaitCount);
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::VulkanPresenterFrameFenceWaitNs, waitNs);
    }
    if (!frame)
        return Fail("the Vulkan presenter's frame ring could not begin a frame");

    const u32 frameIndex = Frames.GetFrameIndex();
    CurrentCommandBuffer = frame->CommandBuffer;

    // Both are safe to recycle now: BeginFrame() waited on this slot's fence,
    // so nothing the previous use of this slot recorded is still executing.
    Staging[frameIndex].Reset();
    Device.Fns().ResetDescriptorPool(Device.GetHandle(), DescriptorPools[frameIndex], 0);
    for (LayerTexture& texture : Layers)
    {
        texture.DirectImage = VK_NULL_HANDLE;
        texture.DirectView = VK_NULL_HANDLE;
        texture.DirectResourceGeneration = 0;
        texture.DirectDescriptorSets.fill(VK_NULL_HANDLE);
        texture.UsesDirect = false;
    }

    VkResult res = VK_SUCCESS;
    const bool acquirePerfEnabled = VulkanPerf::IsEnabled();
    const VulkanPerf::Clock::time_point acquireWaitStart = acquirePerfEnabled
        ? VulkanPerf::Clock::now()
        : VulkanPerf::Clock::time_point{};
    {
        VulkanPerf::ScopedCpuTimer acquireTimer(VulkanPerf::CpuMetric::PresentAcquire);
        res = Device.Fns().AcquireNextImageKHR(
            Device.GetHandle(),
            Swapchain,
            UINT64_MAX,
            frame->ImageAvailable,
            VK_NULL_HANDLE,
            &CurrentImageIndex);
    }
    if (acquirePerfEnabled)
    {
        const u64 waitNs = static_cast<u64>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(
                VulkanPerf::Clock::now() - acquireWaitStart).count());
        VulkanPerf::AddCounter(VulkanPerf::Counter::VulkanAcquireWaitCount);
        VulkanPerf::AddCounter(VulkanPerf::Counter::VulkanAcquireWaitNs, waitNs);
    }

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
            VkResult waitRes = VK_SUCCESS;
            {
                VulkanPerf::ScopedCpuTimer imageWaitTimer(
                    VulkanPerf::CpuMetric::PresentImageFence);
                waitRes = Device.Fns().WaitForFences(
                    Device.GetHandle(), 1, &imageFence, VK_TRUE, UINT64_MAX);
            }
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
    return UploadLayerRegion(layer, pixels, width, height, rowBytes, 0, 0, width, height);
}


bool VulkanPresenter::UploadLayerRegion(
    Layer layer, const void* pixels, u32 width, u32 height, std::size_t rowBytes,
    u32 x, u32 y, u32 regionWidth, u32 regionHeight)
{
    if (!FrameOpen || CompositionOpen || !pixels || width == 0 || height == 0
        || regionWidth == 0 || regionHeight == 0 || x >= width || y >= height)
        return false;

    LayerTexture& texture = Layers[static_cast<std::size_t>(layer)];

    // The surface-sized layers were allocated at swapchain creation; only the
    // screen layers follow the renderer's internal resolution and may need a
    // new image here.
    if (layer == Layer::ScreenTop || layer == Layer::ScreenBottom)
    {
        if (!EnsureLayerImage(layer, texture, width, height, LayerDebugName(layer)))
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
    if (x >= texture.Width || y >= texture.Height)
        return false;
    const u32 copyWidth = std::min({regionWidth, width - x, texture.Width - x});
    const u32 copyHeight = std::min({regionHeight, height - y, texture.Height - y});

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
    const auto* src = static_cast<const u8*>(pixels)
        + static_cast<std::size_t>(y) * rowBytes + static_cast<std::size_t>(x) * 4u;
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
    copy.imageOffset = {
        static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), 0};
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
    if (layer == Layer::Hud)
        VulkanPerf::AddCounter(VulkanPerf::Counter::HudUploadBytes, bytes);
    return true;
}

bool VulkanPresenter::UploadLayerFromBuffer(
    Layer layer, const melonDS::VulkanPresentedFrame& frame, VkDeviceSize sourceOffset)
{
    if (!FrameOpen || CompositionOpen || frame.Buffer == VK_NULL_HANDLE
        || frame.Width == 0 || frame.Height == 0
        || (layer != Layer::ScreenTop && layer != Layer::ScreenBottom))
    {
        return false;
    }

    LayerTexture& texture = Layers[static_cast<std::size_t>(layer)];
    texture.DirectImage = VK_NULL_HANDLE;
    if (!EnsureLayerImage(layer, texture, frame.Width, frame.Height, LayerDebugName(layer)))
        return false;
    texture.DirectView = VK_NULL_HANDLE;
    texture.DirectResourceGeneration = 0;
    texture.DirectDescriptorSets.fill(VK_NULL_HANDLE);
    texture.UsesDirect = false;

    VkBufferMemoryBarrier sourceBarrier{};
    sourceBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    sourceBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    sourceBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sourceBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sourceBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sourceBarrier.buffer = frame.Buffer;
    sourceBarrier.offset = sourceOffset;
    sourceBarrier.size = static_cast<VkDeviceSize>(frame.Width) * frame.Height * sizeof(u32);
    Device.Fns().CmdPipelineBarrier(
        CurrentCommandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 1, &sourceBarrier, 0, nullptr);

    const VkImageLayout currentLayout = texture.Image.GetLayout();
    texture.Image.RecordLayoutTransition(
        CurrentCommandBuffer,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        currentLayout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        currentLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT);

    VkBufferImageCopy copy{};
    copy.bufferOffset = sourceOffset;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {frame.Width, frame.Height, 1};
    Device.Fns().CmdCopyBufferToImage(
        CurrentCommandBuffer,
        frame.Buffer,
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
    VulkanPerf::AddCounter(VulkanPerf::Counter::PresentedScreenCopyBytes,
        static_cast<u64>(frame.Width) * frame.Height * sizeof(u32));
    return true;
}

bool VulkanPresenter::UploadLayerFromImage(
    Layer layer, const melonDS::VulkanPresentedFrame& frame)
{
    if (!FrameOpen || CompositionOpen || !frame.HasDirectSampledOutput()
        || frame.Width == 0 || frame.Height == 0
        || (layer != Layer::ScreenTop && layer != Layer::ScreenBottom))
    {
        return false;
    }

    const VkImageView view = layer == Layer::ScreenTop
        ? frame.DirectImageViewTop : frame.DirectImageViewBottom;
    const VkImage image = layer == Layer::ScreenTop
        ? frame.DirectImageTop : frame.DirectImageBottom;
    if (view == VK_NULL_HANDLE)
        return false;

    LayerTexture& texture = Layers[static_cast<std::size_t>(layer)];
    std::array<VkDescriptorSet, kPresenterSamplerCount> descriptorSets{};
    const DirectDescriptorCacheEntry* cached = FindDirectDescriptor(
        image, view, frame.ResourceGeneration);
    if (cached)
    {
        descriptorSets = cached->Sets;
    }
    else
    {
        // The cache is deliberately non-fatal. This is the old safe path for
        // missing metadata, cache overflow, and unexpected renderer output.
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::PresenterDescriptorFallbackCount);
        const u32 frameIndex = Frames.GetFrameIndex();
        const auto descriptorStart = VulkanPerf::Clock::now();
        std::array<VkDescriptorSetLayout, kPresenterSamplerCount> layouts{};
        layouts.fill(SetLayout);
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = DescriptorPools[frameIndex];
        allocateInfo.descriptorSetCount = static_cast<u32>(descriptorSets.size());
        allocateInfo.pSetLayouts = layouts.data();
        const VkResult allocateResult = Device.Fns().AllocateDescriptorSets(
            Device.GetHandle(), &allocateInfo, descriptorSets.data());
        if (allocateResult != VK_SUCCESS)
            return Fail("vkAllocateDescriptorSets(direct presenter layer)", allocateResult);

        std::array<VkDescriptorImageInfo, kPresenterSamplerCount> imageInfos{};
        std::array<VkWriteDescriptorSet, kPresenterSamplerCount> writes{};
        for (std::size_t sampler = 0; sampler < kPresenterSamplerCount; ++sampler)
        {
            imageInfos[sampler].sampler = sampler == 0 ? SamplerNearest : SamplerLinear;
            imageInfos[sampler].imageView = view;
            imageInfos[sampler].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            writes[sampler].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[sampler].dstSet = descriptorSets[sampler];
            writes[sampler].dstBinding = 0;
            writes[sampler].descriptorCount = 1;
            writes[sampler].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[sampler].pImageInfo = &imageInfos[sampler];
        }
        Device.Fns().UpdateDescriptorSets(
            Device.GetHandle(), static_cast<u32>(writes.size()), writes.data(), 0, nullptr);

        VulkanPerf::AddCounter(
            VulkanPerf::Counter::DescriptorCreateCount,
            static_cast<u64>(descriptorSets.size()));
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::PresenterSrvCreateCount,
            static_cast<u64>(descriptorSets.size()));
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::DescriptorUpdateCount,
            static_cast<u64>(writes.size()));
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::PresenterDescriptorUpdateCount,
            static_cast<u64>(writes.size()));
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::DescriptorWriteCount,
            static_cast<u64>(writes.size()));
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::PresenterDescriptorCpuTimeNs,
            static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                VulkanPerf::Clock::now() - descriptorStart).count()));
    }

    texture.Width = frame.Width;
    texture.Height = frame.Height;
    texture.DirectImage = image;
    texture.DirectView = view;
    texture.DirectResourceGeneration = frame.ResourceGeneration;
    texture.DirectDescriptorSets = descriptorSets;
    texture.UsesDirect = true;
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
    if ((!texture.UsesDirect && !texture.Image.IsValid()) || !texture.HasContent)
        return;

    const VkDescriptorSet set = AcquireDescriptorSet(layer, linearFilter);
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

void VulkanPresenter::DrawRadar(
    const Quad& quad, float opacity, u32 sourceCenterY, u32 sourceRadius)
{
    if (!CompositionOpen || Failed || sourceRadius == 0 || opacity <= 0.0f)
        return;

    const LayerTexture& texture = Layers[static_cast<std::size_t>(Layer::ScreenBottom)];
    if ((!texture.UsesDirect && !texture.Image.IsValid()) || !texture.HasContent)
        return;

    const VkDescriptorSet set = AcquireDescriptorSet(Layer::ScreenBottom, true);
    if (set == VK_NULL_HANDLE)
        return;

    Quad radar = quad;
    radar.Tint[0] = opacity;
    radar.Tint[1] = static_cast<float>(sourceCenterY);
    radar.Tint[2] = static_cast<float>(sourceRadius);
    radar.Tint[3] = -1.0f;

    const Vk::DeviceDispatch& fns = Device.Fns();
    fns.CmdBindPipeline(CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineBlended);
    fns.CmdBindDescriptorSets(
        CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout,
        0, 1, &set, 0, nullptr);
    fns.CmdPushConstants(
        CurrentCommandBuffer, PipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(Quad), &radar);
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

    // One decision for the whole frame. Reading it once means the submit and
    // the present can never disagree about whether they are tagged, which would
    // leave the driver correlating a submission against a present id it never
    // saw.
    const bool tagLatency = Reflex.WantsFrameIdChaining();
    const melonDS::u64 latencyFrameId = Reflex.GetFrameId();

    // VkLatencySubmissionPresentIdNV is what ties this submission to the
    // present below. It is chained onto the VkSubmitInfo itself, so it has to
    // travel through FrameRing::SubmitFrame rather than being applied here.
    VkLatencySubmissionPresentIdNV submitPresentId{};
    const void* submitPNext = nullptr;
    if (tagLatency)
    {
        submitPresentId.sType = VK_STRUCTURE_TYPE_LATENCY_SUBMISSION_PRESENT_ID_NV;
        submitPresentId.presentID = latencyFrameId;
        submitPNext = &submitPresentId;
    }

    // RENDERSUBMIT_START sits immediately before the real vkQueueSubmit --
    // inside FrameRing::SubmitFrame, one call below -- and RENDERSUBMIT_END
    // immediately after it. Nothing else is between the two markers.
    if (tagLatency)
        Reflex.MarkRenderSubmitStart();
    // Unconditional, unlike the Reflex marker above: the A/B capture has to
    // measure the policies Reflex is not tagging frames for.
    LatencyCapture.MarkRenderSubmitStart();

    bool submitted = false;
    {
        VulkanPerf::ScopedCpuTimer submitTimer(VulkanPerf::CpuMetric::QueueSubmit);
        submitted = Frames.SubmitFrame(
            Device.GetMainQueue(),
            frame ? frame->ImageAvailable : VK_NULL_HANDLE,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            signalSemaphore,
            submitPNext);
    }

    if (tagLatency)
        Reflex.MarkRenderSubmitEnd();
    LatencyCapture.MarkRenderSubmitEnd();

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

    // VK_KHR_present_id, which VulkanDevice enables as a hard dependency of
    // Reflex. This is the other half of the correlation: the same id the
    // markers carry and the submission was tagged with. One entry per
    // swapchain, and there is exactly one swapchain here.
    melonDS::VulkanPresentPacer::PresentMetadata genericPresentMetadata{};
    const melonDS::u64 logicalPresentId = PresentPacer.PreparePresent(
        present, tagLatency ? latencyFrameId : 0, genericPresentMetadata);

    VkPresentIdKHR presentId{};
    if (tagLatency)
    {
        presentId.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
        presentId.swapchainCount = 1;
        presentId.pPresentIds = logicalPresentId != 0
            ? &genericPresentMetadata.LogicalId
            : &latencyFrameId;
        presentId.pNext = present.pNext;
        present.pNext = &presentId;
    }

    // PRESENT_START / PRESENT_END bracket the real vkQueuePresentKHR and
    // nothing else. VK_NV_low_latency2 specifies PRESENT_START "just before
    // vkQueuePresentKHR" and PRESENT_END "when vkQueuePresentKHR returns".
    //
    // Both markers therefore sit INSIDE the queue lock: taking the mutex before
    // the span keeps queue contention out of it, and closing the span before
    // the unlock keeps the release out too. With the markers outside, a
    // contended queue would charge its wait to every Reflex latency report and
    // to the host input-to-present figure the A/B compares -- time that is not
    // the presentation call at all. Nor may any pacer or capture bookkeeping
    // sit between the call returning and the end markers.
    //
    // unique_lock rather than lock_guard so the release is an explicit
    // statement: the marker/lock boundary is then a source ordering a static
    // audit can check, not something implied by a closing brace.
    VkResult res = VK_SUCCESS;
    {
        std::unique_lock<std::mutex> queueLock(Device.GetQueueMutex());

        // Anti-Lag's PRESENT stage is specified to be issued immediately before
        // vkQueuePresentKHR, with the frame index its INPUT partner used. Keep
        // the update under the same queue lock so contention cannot separate
        // the vendor marker from the queue operation. The index is the Reflex
        // frame id when Reflex is running and the presenter's own absolute
        // frame counter otherwise -- see BeginLowLatencyFrame.
        AntiLag.EndFrame(LowLatencyFrameIndex);

        if (tagLatency)
            Reflex.MarkPresentStart();
        LatencyCapture.MarkPresentStart();

        res = fns.QueuePresentKHR(Device.GetPresentQueue(), &present);
        // On the queue-full retry path the span covers both calls and ends when
        // the second returns; one presentation gets one marker pair.
        if (PresentPacer.PrepareRetryWithoutTiming(res, present, genericPresentMetadata))
            res = fns.QueuePresentKHR(Device.GetPresentQueue(), &present);

        LatencyCapture.MarkPresentEnd();
        if (tagLatency)
            Reflex.MarkPresentEnd();

        queueLock.unlock();
    }

    PresentPacer.NotifyPresentResult(res, genericPresentMetadata);

    // Only a present the engine accepted is a measurable frame. Recording the
    // rejected ones would mix "displayed at time T" with "never displayed".
    // This runs after NotifyPresentResult so the row's presentation sequence is
    // the one this present just committed.
    if (LatencyCapture.IsEnabled() && (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR))
    {
        LatencyCapture.SetReflexMode(static_cast<int>(Reflex.GetMode()));
        LatencyCapture.Commit(
            genericPresentMetadata.LogicalId,
            PresentPacer.CaptureState(genericPresentMetadata));
    }

    // Not a marker: this is vkLatencySleepNV's "once between presents"
    // bookkeeping, so it deliberately sits outside the PRESENT span.
    // VK_SUBOPTIMAL_KHR did present; VK_ERROR_OUT_OF_DATE_KHR did not, but it
    // retires the swapchain anyway and the rebuild resets the state.
    if (tagLatency && (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR))
        Reflex.NotifyPresented();

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        // Neither is an error: the frame reached the presentation engine (or
        // was correctly rejected because the surface changed size), and the
        // swapchain is rebuilt before the next one.
        SwapchainDirty.store(true, std::memory_order_release);
        VulkanPerf::AddCounter(VulkanPerf::Counter::Frames);
        VulkanPerf::MaybeReport();
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
    VulkanPerf::AddCounter(VulkanPerf::Counter::Frames);
    VulkanPerf::MaybeReport();
    return true;
}


// ---------------------------------------------------------------------------
// Vendor low-latency frame path
//
// The emulation thread calls these around its own frame, through
// ScreenPanelVulkan. Everything here is a no-op when neither extension is
// available, which is the normal case on non-NVIDIA / non-AMD hardware.
// ---------------------------------------------------------------------------

void VulkanPresenter::SetLowLatencyPreferences(int reflexMode, bool antiLag2Enabled)
{
    Reflex.SetMode(melonDS::VulkanNvidiaReflexModeFromConfig(reflexMode));
    AntiLag.SetEnabled(antiLag2Enabled);
}


void VulkanPresenter::BeginLowLatencyFrame(
    int reflexMode, bool antiLag2Enabled, bool normalSpeed, u64 targetFrameIntervalNs)
{
    if (!Initialized || Failed || !Device.IsValid())
        return;

    // Re-applied every frame from the live config value. Both setters are
    // no-ops when the value has not changed, so this costs a comparison in the
    // steady state while still making a settings-dialog change take effect on
    // the very next frame -- without recreating the device or the swapchain,
    // neither of which a mid-session setting change should force.
    SetLowLatencyPreferences(reflexMode, antiLag2Enabled);

    // Exactly one optional late-wait authority is selected. The generic wait is
    // never layered over Reflex or Anti-Lag, and it runs here before every
    // fresh input read. The host frame limiter is a separate responsibility and
    // still owns the emulation rate: presentation scheduling decides when a
    // finished frame is shown, not how fast the DS runs.
    const melonDS::VulkanPacerBeginResult pacerResult = PresentPacer.BeginFrame(
        Reflex.IsActive(), AntiLag.IsActive(), normalSpeed, targetFrameIntervalNs);
    const melonDS::VulkanPacerBeginAction pacerAction =
        melonDS::VulkanPacerActionFor(pacerResult);
    if (pacerAction.RebuildSwapchain)
        SwapchainDirty.store(true, std::memory_order_release);
    if (pacerAction.FailRenderer)
    {
        // Device and surface loss are not stale-swapchain events. Route each
        // through the renderer failure path with the original class intact.
        const VkResult result = pacerResult == melonDS::VulkanPacerBeginResult::DeviceLost
            ? VK_ERROR_DEVICE_LOST
            : VK_ERROR_SURFACE_LOST_KHR;
        Fail("Vulkan present timing query", result);
        return;
    }

    // The strict presenter prototype adds a bounded fence back-pressure point
    // before the vendor sleep and before late input only when the
    // present_wait2 path is unavailable. It waits only the next presenter
    // slot, never the whole queue/device; present_wait2 owns the equivalent
    // display-aware wait when available, and vendor pacing remains exclusive.
    if (PresentPacer.UsesPresenterOneFrameBudget()
        && PresentPacer.GetAuthority() == melonDS::VulkanPacingAuthority::GenericHost
        && normalSpeed
        && Frames.NextFrameSlotHasPendingSubmission())
    {
        const bool perfEnabled = VulkanPerf::IsEnabled();
        const VulkanPerf::Clock::time_point waitStart = perfEnabled
            ? VulkanPerf::Clock::now()
            : VulkanPerf::Clock::time_point{};
        if (!Frames.WaitForNextFrameSlot())
        {
            Failed = true;
            Error = "Vulkan strict presenter frame budget wait failed";
            Platform::Log(Platform::LogLevel::Error, "[Vulkan] presenter: %s\n",
                Error.c_str());
            return;
        }
        if (perfEnabled)
        {
            const u64 waitNs = static_cast<u64>(std::chrono::duration_cast<
                std::chrono::nanoseconds>(
                    VulkanPerf::Clock::now() - waitStart).count());
            VulkanPerf::AddCounter(
                VulkanPerf::Counter::VulkanPresenterFrameFenceWaitCount);
            VulkanPerf::AddCounter(
                VulkanPerf::Counter::VulkanPresenterFrameFenceWaitNs, waitNs);
        }
    }

    // vkLatencySleepNV lives in here, and it must run before any input is read
    // -- that delay is the entire mechanism. SIMULATION_START is deliberately
    // NOT emitted here: it belongs after the sleep and after input sampling,
    // which is MarkLowLatencySimulationStart().
    Reflex.BeginFrame();

    // Keep the two features on the same frame numbering when both are live.
    // Reflex bumps its id inside BeginFrame() above; when it is not running,
    // its id stays put and Anti-Lag needs a counter of its own.
    if (Reflex.WantsFrameIdChaining())
        LowLatencyFrameIndex = Reflex.GetFrameId();
    else
        ++LowLatencyFrameIndex;

    // Anti-Lag's INPUT stage, specified to be issued immediately before the
    // application reads input -- the same point the Reflex sleep just returned
    // from. Its PRESENT partner is issued in EndFrame(), just before
    // vkQueuePresentKHR, with this same index.
    AntiLag.BeginFrame(LowLatencyFrameIndex);

    LogLowLatencyStateIfChanged();

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    ReportLatencyTimings();
#endif
}


#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
void VulkanPresenter::ReportLatencyTimings()
{
    if (!Reflex.IsFramePathAvailable())
        return;

    // Every 600th frame, so a ten-minute session produces a readable handful of
    // lines rather than a wall of them. Developer builds only: this exists to
    // prove the markers are landing, which is not something a shipping user
    // needs in their log.
    if (++LatencyTimingCountdown < 600)
        return;
    LatencyTimingCountdown = 0;

    VkLatencyTimingsFrameReportNV reports[8]{};
    const melonDS::u32 count = Reflex.QueryTimings(reports, 8);
    if (count == 0)
    {
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] Reflex timings: driver has no completed frame reports yet\n");
        return;
    }

    // The newest complete report. A non-zero presentID that matches the ids the
    // markers carried, together with non-zero sim/submit/present timestamps, is
    // the end-to-end evidence that vkSetLatencyMarkerNV, the tagged
    // vkQueueSubmit and the tagged vkQueuePresentKHR were all correlated by the
    // driver into one frame.
    const VkLatencyTimingsFrameReportNV& r = reports[count - 1];
    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] Reflex timings: reports=%u presentID=%llu sim=%llu..%llu "
        "renderSubmit=%llu..%llu present=%llu..%llu gpuRender=%llu..%llu inputSample=%llu\n",
        static_cast<unsigned>(count),
        static_cast<unsigned long long>(r.presentID),
        static_cast<unsigned long long>(r.simStartTimeUs),
        static_cast<unsigned long long>(r.simEndTimeUs),
        static_cast<unsigned long long>(r.renderSubmitStartTimeUs),
        static_cast<unsigned long long>(r.renderSubmitEndTimeUs),
        static_cast<unsigned long long>(r.presentStartTimeUs),
        static_cast<unsigned long long>(r.presentEndTimeUs),
        static_cast<unsigned long long>(r.gpuRenderStartTimeUs),
        static_cast<unsigned long long>(r.gpuRenderEndTimeUs),
        static_cast<unsigned long long>(r.inputSampleTimeUs));
}
#endif


void VulkanPresenter::MarkLowLatencyInputSample()
{
    Reflex.MarkInputSample();
    // The A/B capture takes host timestamps at the same points as the Reflex
    // markers, so every policy -- including the ones Reflex is not running for
    // -- produces the same measurement fields. Compiled out entirely unless the
    // capture build flag is on.
    LatencyCapture.MarkInputSample();
}


void VulkanPresenter::MarkLowLatencySimulationStart()
{
    Reflex.MarkSimulationStart();
    LatencyCapture.MarkSimulationStart();
}


void VulkanPresenter::MarkLowLatencySimulationEnd()
{
    Reflex.MarkSimulationEnd();
    LatencyCapture.MarkSimulationEnd();
}


void VulkanPresenter::FinishLowLatencyFrame()
{
    // Closes the Reflex frame. Anti-Lag's PRESENT update is not issued here:
    // it has to sit immediately before vkQueuePresentKHR, so EndFrame() owns
    // it, and a frame that never presented correctly gets neither.
    Reflex.FinishFrame();
}


void VulkanPresenter::LogLowLatencyState(const char* context)
{
    const melonDS::VulkanLowLatencyStatus& reflexStatus = Device.GetNvLowLatency2Status();
    const melonDS::VulkanLowLatencyStatus& antiLagStatus = Device.GetAmdAntiLagStatus();

    // "device-extension-enabled" is what vkCreateDevice accepted; "actual" is what the frame
    // path is really doing right now. They differ whenever the extension is
    // present but the user has the feature switched off, or when a runtime
    // failure disabled it -- and that gap is the whole reason both columns
    // exist. A reason from the running module wins over the device's, because
    // it is the more specific one.
    const std::string& reflexReason = Reflex.IsAvailable()
        ? (Reflex.IsActive()
            ? std::string("latency markers active; no frame-rate cap requested")
            : std::string("latency markers active; low-latency pacing switched off by NvidiaReflexMode"))
        : (Reflex.GetUnavailableReason().empty() ? reflexStatus.Reason : Reflex.GetUnavailableReason());

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] %s NVIDIA Reflex (VK_NV_low_latency2): requested=%s supported=%s "
        "device-extension-enabled=%s actual=%s reason=%s\n",
        context,
        melonDS::VulkanNvidiaReflexModeName(Reflex.GetMode()),
        reflexStatus.Supported ? "yes" : "no",
        reflexStatus.Enabled ? "yes" : "no",
        Reflex.IsActive() ? "active" : "inactive",
        reflexReason.empty() ? "not evaluated" : reflexReason.c_str());

    const std::string& antiLagReason = AntiLag.IsAvailable()
        ? (AntiLag.IsActive()
            ? std::string("latency markers active; no frame-rate cap requested")
            : std::string("supported, switched off by AmdAntiLag2Enabled"))
        : (AntiLag.GetUnavailableReason().empty() ? antiLagStatus.Reason : AntiLag.GetUnavailableReason());

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] %s AMD Radeon Anti-Lag 2 (VK_AMD_anti_lag): requested=%s supported=%s "
        "device-extension-enabled=%s actual=%s reason=%s\n",
        context,
        AntiLag.IsEnabledRequested() ? "on" : "off",
        antiLagStatus.Supported ? "yes" : "no",
        antiLagStatus.Enabled ? "yes" : "no",
        AntiLag.IsActive() ? "active" : "inactive",
        antiLagReason.empty() ? "not evaluated" : antiLagReason.c_str());

    PresentPacer.LogState(context);
}


void VulkanPresenter::LogLowLatencyStateIfChanged()
{
    const int mode = static_cast<int>(Reflex.GetMode());
    const bool reflexActive = Reflex.IsActive();
    const bool antiLagActive = AntiLag.IsActive();

    if (LowLatencyStateLogged
        && mode == LoggedReflexMode
        && reflexActive == LoggedReflexActive
        && antiLagActive == LoggedAntiLagActive)
    {
        return;
    }

    LoggedReflexMode = mode;
    LoggedReflexActive = reflexActive;
    LoggedAntiLagActive = antiLagActive;

    const bool first = !LowLatencyStateLogged;
    LowLatencyStateLogged = true;
    LogLowLatencyState(first ? "low-latency:" : "low-latency changed:");
}


// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

void VulkanPresenter::Shutdown() noexcept
{
    if (Device.IsValid())
    {
        // Vendor pacing state is driver-owned and scoped to this live
        // device/swapchain. Turn it off before the final device drain so the
        // drain also retires the state transition itself. Doing this after
        // DeviceWaitIdle and then immediately destroying the Reflex semaphore
        // and swapchain left NVIDIA's driver holding active low-latency state
        // when switching away from Vulkan during gameplay.
        Reflex.FinishFrame();
        Reflex.SetMode(VulkanNvidiaReflexMode::Off);
        AntiLag.SetEnabled(false);
        AntiLag.EndFrame(LowLatencyFrameIndex);
        AntiLag.BeginFrame(LowLatencyFrameIndex);

        // Teardown is the second sanctioned use of a full device drain: every
        // object below may still be referenced by work in flight, and there is
        // no cheaper way to be sure it is not.
        Frames.WaitIdle();

        // Both vendor helpers are torn down while the device and the swapchain
        // are still alive, because both have to talk to the driver on the way
        // out (turn pacing off, destroy the Reflex timeline semaphore). Doing
        // it after the device is gone would be a use-after-free; doing it
        // before the drain above would destroy a semaphore the driver could
        // still be signalling.
        Reflex.Shutdown();
        AntiLag.Shutdown();
        PresentPacer.Shutdown();
        LowLatencyFrameIndex = 0;
        LowLatencyStateLogged = false;

        const Vk::DeviceDispatch& fns = Device.Fns();
        VkDevice device = Device.GetHandle();

        DestroySwapchainObjects(true);

        if (Swapchain != VK_NULL_HANDLE)
        {
            fns.DestroySwapchainKHR(device, Swapchain, nullptr);
            Swapchain = VK_NULL_HANDLE;
        }

        // Persistent layer sets own only descriptor state, not the image views.
        // Drop the pool before destroying those views so no stale binding can
        // survive the resource lifetime boundary.
        if (PersistentDescriptorPool != VK_NULL_HANDLE)
        {
            fns.DestroyDescriptorPool(device, PersistentDescriptorPool, nullptr);
            PersistentDescriptorPool = VK_NULL_HANDLE;
        }
        for (auto& layerSets : LayerDescriptorSets)
            layerSets.fill(VK_NULL_HANDLE);
        for (DirectDescriptorCacheEntry& entry : DirectDescriptorCache)
            entry = {};
        CachedDirectResourceGeneration = 0;

        for (LayerTexture& texture : Layers)
        {
            texture.Image.Destroy();
            texture.Width = 0;
            texture.Height = 0;
            texture.HasContent = false;
            texture.DirectImage = VK_NULL_HANDLE;
            texture.DirectView = VK_NULL_HANDLE;
            texture.DirectResourceGeneration = 0;
            texture.DirectDescriptorSets.fill(VK_NULL_HANDLE);
            texture.UsesDirect = false;
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
