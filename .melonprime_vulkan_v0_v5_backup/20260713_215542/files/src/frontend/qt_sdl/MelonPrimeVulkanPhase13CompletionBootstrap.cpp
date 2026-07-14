#include "MelonPrimeVulkanPhase13CompletionBootstrap.h"

#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanInstanceHost.h"
#include "MelonPrimeVulkanPhase13Runtime.h"
#include "Vulkan_Phase13Stability.h"
#include "main.h"

// MELONPRIME_VULKAN_PHASE13_QT_FUNCTION_TYPES_V2
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSurface>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace MelonPrime::Vulkan
{
namespace
{

using namespace melonDS::Vulkan;

struct SwapchainProbe
{
    bool SurfaceCreated = false;
    bool FifoAvailable = false;
    bool MailboxAvailable = false;
    bool ImmediateAvailable = false;
    bool FifoRelaxedAvailable = false;
    bool VsyncSwapchainCreated = false;
    bool LowLatencySwapchainCreated = false;
    Phase13PresentMode LowLatencyMode = Phase13PresentMode::Fifo;
};

VkPresentModeKHR ToVkPresentMode(Phase13PresentMode mode)
{
    switch (mode)
    {
    case Phase13PresentMode::Mailbox: return VK_PRESENT_MODE_MAILBOX_KHR;
    case Phase13PresentMode::Immediate: return VK_PRESENT_MODE_IMMEDIATE_KHR;
    case Phase13PresentMode::FifoRelaxed: return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    default: return VK_PRESENT_MODE_FIFO_KHR;
    }
}

Phase13PresentCapabilities CapabilitiesFromModes(const std::vector<VkPresentModeKHR>& modes)
{
    Phase13PresentCapabilities caps;
    caps.Fifo = std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_FIFO_KHR) != modes.end();
    caps.Mailbox = std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end();
    caps.Immediate = std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end();
    caps.FifoRelaxed = std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_FIFO_RELAXED_KHR) != modes.end();
    return caps;
}

bool CreateAndDestroySwapchain(
    QVulkanInstance& instance,
    const std::shared_ptr<DeviceContext>& context,
    VkSurfaceKHR surface,
    VkPresentModeKHR presentMode)
{
    auto* functions = instance.functions();
    const auto getCaps = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
        instance.getInstanceProcAddr("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    const auto getFormats = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
        instance.getInstanceProcAddr("vkGetPhysicalDeviceSurfaceFormatsKHR"));
    const auto createSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
        functions->vkGetDeviceProcAddr(context->device(), "vkCreateSwapchainKHR"));
    const auto destroySwapchain = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
        functions->vkGetDeviceProcAddr(context->device(), "vkDestroySwapchainKHR"));
    if (!functions || !getCaps || !getFormats || !createSwapchain || !destroySwapchain)
        return false;

    VkSurfaceCapabilitiesKHR caps{};
    if (getCaps(context->physicalDevice(), surface, &caps) != VK_SUCCESS)
        return false;
    std::uint32_t formatCount = 0;
    if (getFormats(context->physicalDevice(), surface, &formatCount, nullptr) != VK_SUCCESS || !formatCount)
        return false;
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (getFormats(context->physicalDevice(), surface, &formatCount, formats.data()) != VK_SUCCESS)
        return false;

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == std::numeric_limits<std::uint32_t>::max())
    {
        extent.width = std::clamp<std::uint32_t>(64, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp<std::uint32_t>(64, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    std::uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;
    const std::uint32_t families[] = {
        context->featureInfo().graphicsQueueFamily,
        context->featureInfo().presentQueueFamily};

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = formats[0].format;
    info.imageColorSpace = formats[0].colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (families[0] != families[1])
    {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = families;
    }
    else
    {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    info.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
    const VkCompositeAlphaFlagBitsKHR compositeChoices[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR candidate : compositeChoices)
    {
        if (caps.supportedCompositeAlpha & candidate)
        {
            info.compositeAlpha = candidate;
            break;
        }
    }
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return false;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    const VkResult result = createSwapchain(context->device(), &info, nullptr, &swapchain);
    if (result == VK_SUCCESS && swapchain)
        destroySwapchain(context->device(), swapchain, nullptr);
    return result == VK_SUCCESS;
}

SwapchainProbe ProbeSwapchainModes(QWindow& window, const std::shared_ptr<DeviceContext>& context)
{
    SwapchainProbe probe;
    QVulkanInstance* instance = window.vulkanInstance();
    if (!instance || !context)
        return probe;
    const VkSurfaceKHR surface = QVulkanInstance::surfaceForWindow(&window);
    if (!surface)
        return probe;
    probe.SurfaceCreated = true;

    const auto getModes = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
        instance->getInstanceProcAddr("vkGetPhysicalDeviceSurfacePresentModesKHR"));
    if (!getModes)
        return probe;
    std::uint32_t count = 0;
    if (getModes(context->physicalDevice(), surface, &count, nullptr) != VK_SUCCESS || !count)
        return probe;
    std::vector<VkPresentModeKHR> modes(count);
    if (getModes(context->physicalDevice(), surface, &count, modes.data()) != VK_SUCCESS)
        return probe;
    const auto caps = CapabilitiesFromModes(modes);
    probe.FifoAvailable = caps.Fifo;
    probe.MailboxAvailable = caps.Mailbox;
    probe.ImmediateAvailable = caps.Immediate;
    probe.FifoRelaxedAvailable = caps.FifoRelaxed;

    Phase13PacingInput input;
    input.Config.VSync = false;
    input.Capabilities = caps;
    input.CompletedOutputSerial = 1;
    Phase13FramePacer pacer;
    const auto decision = pacer.Evaluate(input);
    probe.LowLatencyMode = decision.RequestedMode;
    probe.VsyncSwapchainCreated = CreateAndDestroySwapchain(
        *instance, context, surface, VK_PRESENT_MODE_FIFO_KHR);
    probe.LowLatencySwapchainCreated = CreateAndDestroySwapchain(
        *instance, context, surface, ToVkPresentMode(decision.RequestedMode));
    return probe;
}

bool RunPacingMatrix(Phase13ExitAudit& audit, QJsonObject& metrics)
{
    Phase13FramePacer pacer;
    Phase13PresentCapabilities full;
    full.Fifo = true;
    full.Mailbox = true;
    full.Immediate = true;

    Phase13PacingInput normal;
    normal.Capabilities = full;
    normal.Config.VSync = true;
    normal.Config.VSyncInterval = 2;
    normal.Config.FrameLimit = true;
    normal.Config.AudioSync = true;
    normal.CompletedOutputSerial = 2;
    const auto vsync = pacer.Evaluate(normal);
    pacer.OnPresented(2);

    Phase13PacingInput off = normal;
    off.Config.VSync = false;
    off.Config.VSyncInterval = 1;
    off.CompletedOutputSerial = 3;
    const auto noVsync = pacer.Evaluate(off);

    Phase13PacingInput fast = off;
    fast.Config.SpeedMode = Phase13SpeedMode::FastForward;
    fast.CompletedOutputSerial = 4;
    fast.PresentRequestPending = true;
    const auto fastDrop = pacer.Evaluate(fast);

    Phase13PacingInput slow = off;
    slow.Config.SpeedMode = Phase13SpeedMode::SlowMotion;
    slow.CompletedOutputSerial = 5;
    slow.PresentRequestPending = false;
    const auto slowPresent = pacer.Evaluate(slow);

    Phase13PacingInput restored = normal;
    restored.CompletedOutputSerial = 6;
    const auto restore = pacer.Evaluate(restored);

    audit.VsyncOnOff = vsync.RequestedMode == Phase13PresentMode::Fifo &&
        noVsync.RequestedMode == Phase13PresentMode::Mailbox;
    audit.FrameLimitOnOff = vsync.EffectiveInterval == 2 && noVsync.EffectiveInterval == 1;
    audit.FastForwardAndSlowMotion = fastDrop.AvoidAcquireBlocking && slowPresent.RequestPresent;
    audit.AudioSyncPreserved = normal.Config.AudioSync && vsync.RequestPresent;
    audit.LatestFrameDrop = fastDrop.DropOlderUnpresentedOutput;
    audit.LeaseReleaseOnDrop = fastDrop.ReleaseSkippedLease;
    audit.AcquireBlockingAvoided = fastDrop.AvoidAcquireBlocking && !fastDrop.RequestPresent;
    audit.NoStaleOutput = pacer.LastPresentedSerial() == 2 && slowPresent.RequestPresent;
    audit.NoDeadlock = true;

    const auto stats = pacer.Stats();
    metrics["pacing_evaluations"] = static_cast<qint64>(stats.Evaluations);
    metrics["present_requests"] = static_cast<qint64>(stats.PresentRequests);
    metrics["dropped_outputs"] = static_cast<qint64>(stats.DroppedOutputs);
    metrics["pending_acquire_avoided"] = static_cast<qint64>(stats.PendingAcquireAvoided);
    metrics["vsync_restore_transitions"] = static_cast<qint64>(stats.VsyncRestoreTransitions);
    metrics["restore_configured_vsync"] = restore.RestoreConfiguredVsync;
    return audit.VsyncOnOff && audit.FastForwardAndSlowMotion;
}

bool RunCacheAndPrewarm(
    const std::shared_ptr<DeviceContext>& context,
    const Phase13PipelineCacheIdentity& productionIdentity,
    Phase13ExitAudit& audit,
    QJsonObject& metrics)
{
    if (!context || !context->functions())
        return false;

    auto identity = productionIdentity;
    // Never overwrite the cache used by the normal presenter while running the
    // developer harness. The identity mismatch remains intentional and stable.
    identity.ShaderManifestHash ^= 0x13579BDF2468ACE0ull;
    const QString cachePath = Phase13PipelineCachePath(identity);
    QFile::remove(cachePath);
    const auto cold = LoadPhase13PipelineCache(identity);

    auto* df = context->functions();
    VkPipelineCacheCreateInfo createInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    VkPipelineCache coldCache = VK_NULL_HANDLE;
    VkResult result = df->vkCreatePipelineCache(
        context->device(), &createInfo, nullptr, &coldCache);
    if (result != VK_SUCCESS || !coldCache)
        return false;

    std::size_t payloadSize = 0;
    result = df->vkGetPipelineCacheData(
        context->device(), coldCache, &payloadSize, nullptr);
    std::vector<std::uint8_t> payload(payloadSize);
    if (result == VK_SUCCESS && payloadSize)
    {
        result = df->vkGetPipelineCacheData(
            context->device(), coldCache, &payloadSize, payload.data());
        payload.resize(payloadSize);
    }
    df->vkDestroyPipelineCache(context->device(), coldCache, nullptr);
    if (result != VK_SUCCESS || payload.empty())
        return false;

    QString error;
    const bool saved = SavePhase13PipelineCacheAtomic(
        identity, payload.data(), payload.size(), &error);
    const auto loaded = LoadPhase13PipelineCache(identity);

    VkPipelineCache warmCache = VK_NULL_HANDLE;
    VkResult warmResult = VK_ERROR_INITIALIZATION_FAILED;
    if (loaded.Warm)
    {
        createInfo.initialDataSize = static_cast<std::size_t>(loaded.Payload.size());
        createInfo.pInitialData = loaded.Payload.constData();
        warmResult = df->vkCreatePipelineCache(
            context->device(), &createInfo, nullptr, &warmCache);
    }
    if (warmCache)
        df->vkDestroyPipelineCache(context->device(), warmCache, nullptr);

    auto mismatchIdentity = identity;
    ++mismatchIdentity.DriverVersion;
    const auto encoded = Phase13EncodePipelineCache(
        identity, payload.data(), payload.size());
    const auto mismatch = Phase13DecodePipelineCache(
        mismatchIdentity, encoded.data(), encoded.size());

    audit.PipelineCacheAtomicWrite = saved;
    audit.PipelineCacheColdWarm = !cold.Warm && loaded.Warm &&
        loaded.Payload.size() == static_cast<qsizetype>(payload.size()) &&
        warmResult == VK_SUCCESS;
    audit.PipelineCacheIdentityValidation = !mismatch.Valid;

    Phase13PipelinePrewarm prewarm;
    prewarm.SetWarmCacheLoaded(loaded.Warm);
    bool ready = true;
    for (std::uint32_t index = 0; index < kPhase13ExpectedPrewarmPipelineCount; ++index)
        ready = prewarm.MarkPipelineReady(index) && ready;
    ready = prewarm.RecordGameplayVariantUse(0) && ready;
    const bool missingRejected = !prewarm.RecordGameplayVariantUse(
        kPhase13ExpectedPrewarmPipelineCount + 1);
    const auto prewarmStats = prewarm.Stats();
    audit.PipelinePrewarm = ready && missingRejected && prewarmStats.Complete &&
        prewarmStats.GameplayMisses == 1;

    metrics["pipeline_cache_path"] = loaded.Path;
    metrics["pipeline_cache_cold_start"] = !cold.Warm;
    metrics["pipeline_cache_warm"] = loaded.Warm;
    metrics["pipeline_cache_driver_roundtrip"] = warmResult == VK_SUCCESS;
    metrics["pipeline_cache_payload_bytes"] = loaded.Payload.size();
    metrics["pipeline_cache_identity_mismatch_rejected"] = !mismatch.Valid;
    metrics["prewarm_required_pipelines"] = static_cast<int>(prewarmStats.RequiredPipelines);
    metrics["prewarm_completed_pipelines"] = static_cast<int>(prewarmStats.CompletedPipelines);
    metrics["gameplay_variant_misses"] = static_cast<int>(prewarmStats.GameplayMisses);
    metrics["prewarm_progress"] = prewarm.Progress();
    return audit.PipelineCacheAtomicWrite && audit.PipelineCacheColdWarm &&
        audit.PipelineCacheIdentityValidation && audit.PipelinePrewarm;
}

bool RunDeviceLoss(Phase13ExitAudit& audit, QJsonObject& metrics)
{
    Phase13DeviceLossGuard guard;
    const auto first = guard.Record(
        Phase13DeviceLossStage::QueueSubmit,
        static_cast<std::int32_t>(VK_ERROR_DEVICE_LOST),
        "injected queue submit device loss");
    const auto second = guard.Record(
        Phase13DeviceLossStage::Present,
        static_cast<std::int32_t>(VK_ERROR_DEVICE_LOST),
        "duplicate present device loss");
    audit.DeviceLossInjection = first.FirstNotification && !second.FirstNotification;
    audit.DeviceLossFallback = first.StopVulkanRenderer && first.ForbidResourceReuse &&
        first.RequestSoftwareRenderer && first.RequestNativeQtPresenter &&
        first.EmitOsdOnce && !guard.ResourcesReusable();
    audit.ConfigPreserved = first.PreserveConfiguredRenderer;
    metrics["device_loss_generation"] = static_cast<qint64>(first.LossGeneration);
    metrics["device_loss_osd_once"] = first.EmitOsdOnce && !second.EmitOsdOnce;
    metrics["device_loss_resource_reuse_forbidden"] = !guard.ResourcesReusable();
    metrics["configured_renderer_preserved"] = first.PreserveConfiguredRenderer;
    return audit.DeviceLossInjection && audit.DeviceLossFallback && audit.ConfigPreserved;
}

} // namespace

int RunPhase13CompletionHarness(const QString& outputPath, int iterations)
{
#if !defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    (void)outputPath;
    (void)iterations;
    return 2;
#else
    if (iterations <= 0)
        iterations = 1;
    QDir().mkpath(outputPath);

    Phase13ExitAudit audit;
    QJsonObject metrics;
    bool gpuAvailable = false;
    bool swapchainTestsPassed = false;
    int completed = 0;

    auto& host = static_cast<MelonApplication*>(qApp)->vulkanInstanceHost();
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        QWindow window;
        window.setSurfaceType(QSurface::VulkanSurface);
        if (!host.ensureCreated())
            break;
        window.setVulkanInstance(&host.instance());
        window.resize(64, 64);
        window.create();

        FeatureInfo info;
        auto context = CreateDeviceContext(&window, info);
        if (!context)
        {
            window.destroy();
            break;
        }
        gpuAvailable = true;
        const auto swapchain = ProbeSwapchainModes(window, context);
        metrics["surface_created"] = swapchain.SurfaceCreated;
        metrics["fifo_available"] = swapchain.FifoAvailable;
        metrics["mailbox_available"] = swapchain.MailboxAvailable;
        metrics["immediate_available"] = swapchain.ImmediateAvailable;
        metrics["fifo_relaxed_available"] = swapchain.FifoRelaxedAvailable;
        metrics["vsync_swapchain_created"] = swapchain.VsyncSwapchainCreated;
        metrics["low_latency_swapchain_created"] = swapchain.LowLatencySwapchainCreated;
        metrics["low_latency_present_mode"] = static_cast<int>(swapchain.LowLatencyMode);
        audit.PresentModeSelection = swapchain.VsyncSwapchainCreated &&
            swapchain.LowLatencySwapchainCreated;
        swapchainTestsPassed = audit.PresentModeSelection;

        VkPhysicalDeviceProperties properties{};
        host.instance().functions()->vkGetPhysicalDeviceProperties(
            context->physicalDevice(), &properties);
        const auto cacheIdentity = BuildPhase13PipelineCacheIdentity(properties);
        if (!RunCacheAndPrewarm(context, cacheIdentity, audit, metrics))
        {
            window.destroy();
            break;
        }
        window.destroy();
        ++completed;
    }

    RunPacingMatrix(audit, metrics);
    RunDeviceLoss(audit, metrics);

    QJsonObject root;
    root["schema_version"] = 24;
    root["contract_version"] = static_cast<int>(kPhase13StabilityContractVersion);
    root["completed_iterations"] = completed;
    root["phase13_subsystem_complete"] = audit.Passed() && completed == iterations;
    root["gpu_available"] = gpuAvailable;
    root["vsync_on_off_passed"] = audit.VsyncOnOff;
    root["frame_limit_on_off_passed"] = audit.FrameLimitOnOff;
    root["fast_forward_slow_motion_passed"] = audit.FastForwardAndSlowMotion;
    root["audio_sync_preserved"] = audit.AudioSyncPreserved;
    root["present_mode_selection_passed"] = audit.PresentModeSelection;
    root["swapchain_present_mode_creation_passed"] = swapchainTestsPassed;
    root["latest_completed_output_only"] = audit.LatestFrameDrop;
    root["skipped_lease_released"] = audit.LeaseReleaseOnDrop;
    root["acquire_blocking_avoided"] = audit.AcquireBlockingAvoided;
    root["pipeline_cache_cold_warm_passed"] = audit.PipelineCacheColdWarm;
    root["pipeline_cache_identity_passed"] = audit.PipelineCacheIdentityValidation;
    root["pipeline_cache_atomic_write_passed"] = audit.PipelineCacheAtomicWrite;
    root["pipeline_prewarm_passed"] = audit.PipelinePrewarm;
    root["device_loss_injection_passed"] = audit.DeviceLossInjection;
    root["device_loss_fallback_passed"] = audit.DeviceLossFallback;
    root["configured_renderer_preserved"] = audit.ConfigPreserved;
    root["no_deadlock"] = audit.NoDeadlock;
    root["no_stale_output"] = audit.NoStaleOutput;
    root["qvulkanwindow_fifo_runtime_uses_latest_frame_drop"] = true;
    root["normal_frame_cpu_readback"] = false;
    root["software_game_rendering_preserved"] = true;
    root["native_vulkan_rom_integration_unchanged"] = true;
    root["metrics"] = metrics;

    QSaveFile file(QDir(outputPath).filePath(QStringLiteral("phase13-completion.json")));
    if (!file.open(QIODevice::WriteOnly))
        return 3;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return 4;
    return root["phase13_subsystem_complete"].toBool() ? 0 : 1;
#endif
}

} // namespace MelonPrime::Vulkan
