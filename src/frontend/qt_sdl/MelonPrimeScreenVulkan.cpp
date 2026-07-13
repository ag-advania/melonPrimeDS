#include "MelonPrimeScreenVulkan.h"
#include "MelonPrimeVulkanNativeRaster.h"

#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanWindow>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPainter>
#include <QResizeEvent>
#include <QThread>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

#include "MelonPrimeVulkanInstanceHost.h"
#include "MelonPrimeVulkanPhase13Runtime.h"
#include "EmuInstance.h"
#include "GPU_Vulkan.h"
#include "Window.h"
#include "Platform.h"
#include "main.h"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrime.h"
#include "MelonPrimeConstants.h"
#include "MelonPrimeHudRender.h"
#include "MelonPrimeHudConfigOnScreenEdit.h"
#include "MelonPrimePerfProbe.h"
#include "MelonPrimeHudPropSchema.inc"

// MELONPRIME_VULKAN_DIRECT_COMPOSITOR_P4_V1
// Phase 3 called these Screen.cpp-local static helpers from a different
// translation unit. Include the shared implementation here as well so MinGW
// sees their declarations and definitions in MelonPrimeScreenVulkan.cpp.
#include "MelonPrimeHudScreenCppHelpers.inc"
#endif
#include "../../../Vulkan_shaders/generated/VulkanShaders.h"

namespace
{

using melonDS::Platform::Log;
using melonDS::Platform::LogLevel;

constexpr VkFormat kUploadFormat = VK_FORMAT_B8G8R8A8_UNORM;

VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

// MELONPRIME_VULKAN_DIRECT_COMPOSITOR_P3_V1
struct alignas(16) VulkanDirectPushConstants
{
    std::array<float, 4> Transform0{};
    std::array<float, 4> Transform1{};
    std::array<float, 4> Geometry{};
    std::array<float, 4> Radar{};
    std::array<std::uint32_t, 4> Params{};
};
static_assert(sizeof(VulkanDirectPushConstants) == 80);

int RoundDirectExtent(int value)
{
    value = std::max(1, value);
    return (value + 63) & ~63;
}

void CopyImageRectTightly(
    std::uint8_t* destination,
    const QImage& image,
    const QRect& requestedRect)
{
    const QRect sourceRect = requestedRect.intersected(image.rect());
    if (!destination || image.isNull() || sourceRect.isEmpty())
        return;

    const std::size_t rowBytes =
        static_cast<std::size_t>(sourceRect.width()) * 4u;
    for (int y = 0; y < sourceRect.height(); ++y)
    {
        const std::uint8_t* source =
            image.constScanLine(sourceRect.y() + y) +
            static_cast<std::size_t>(sourceRect.x()) * 4u;
        std::memcpy(
            destination + static_cast<std::size_t>(y) * rowBytes,
            source,
            rowBytes);
    }
}

void CopyImageTightly(std::uint8_t* destination, const QImage& image)
{
    CopyImageRectTightly(destination, image, image.rect());
}

class PresenterWindow;

class PresenterRenderer final : public QVulkanWindowRenderer
{
public:
    PresenterRenderer(PresenterWindow* window, ScreenPanelVulkan* panel)
        : m_window(window), m_panel(panel)
    {
    }

    void initResources() override;
    void releaseResources() override;
    void startNextFrame() override;
    void logicalDeviceLost() override;
    void physicalDeviceLost() override;

private:
    bool createStaticResources();
    bool createDirectStaticResources();
    bool createFrameResources(const QSize& size);
    bool createDirectResources(
        const QSize& screenSize,
        const QSize& overlaySize);
    bool renderDirectFrame(const VulkanDirectFrameSnapshot& snapshot);
    void destroyFrameResources();
    void destroyDirectResources();
    void destroyStaticResources();
    bool createShaderModule(const std::uint32_t* code, std::size_t size, VkShaderModule& module);
    std::uint32_t findMemoryType(std::uint32_t bits, VkMemoryPropertyFlags required) const;
    void fail(const char* stage, VkResult result = VK_ERROR_UNKNOWN);
    void completeFrame();
    bool savePipelineCache();

    PresenterWindow* m_window = nullptr;
    ScreenPanelVulkan* m_panel = nullptr;
    QVulkanDeviceFunctions* m_df = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
    melonDS::Vulkan::Phase13PipelineCacheIdentity m_phase13PipelineCacheIdentity{};
    bool m_phase13PipelineCacheIdentityValid = false;
    bool m_phase13PipelineCacheWarm = false;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkSampler m_nearestSampler = VK_NULL_HANDLE;
    VkSampler m_linearSampler = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    std::uint8_t* m_stagingMap = nullptr;
    VkDeviceSize m_frameStride = 0;
    QSize m_frameSize;
    int m_frameCount = 0;
    std::array<VkImage, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_images{};
    std::array<VkDeviceMemory, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_imageMemory{};
    std::array<VkImageView, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_imageViews{};
    std::array<VkDescriptorSet, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_descriptorSets{};
    std::array<VkImageLayout, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_imageLayouts{};

    // MELONPRIME_VULKAN_DIRECT_COMPOSITOR_P3_V1
    VkDescriptorSetLayout m_directDescriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_directPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_directPipeline = VK_NULL_HANDLE;
    bool m_directStaticReady = false;

    VkBuffer m_directStagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_directStagingMemory = VK_NULL_HANDLE;
    std::uint8_t* m_directStagingMap = nullptr;
    VkDeviceSize m_directTopOffset = 0;
    VkDeviceSize m_directBottomOffset = 0;
    VkDeviceSize m_directOverlayOffset = 0;
    VkDeviceSize m_directFrameStride = 0;
    QSize m_directScreenSize;
    QSize m_directOverlaySize;
    int m_directFrameCount = 0;

    VkDescriptorPool m_directDescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directDescriptorSets{};
    std::array<VkImage, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directScreenImages{};
    std::array<VkDeviceMemory, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directScreenMemory{};
    std::array<VkImageView, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directScreenViews{};
    std::array<VkImageLayout, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directScreenLayouts{};
    std::array<VkImage, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directOverlayImages{};
    std::array<VkDeviceMemory, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directOverlayMemory{};
    std::array<VkImageView, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directOverlayViews{};
    std::array<VkImageLayout, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directOverlayLayouts{};

    // MELONPRIME_VULKAN_DIRECT_COMPOSITOR_P6_FRAME_IDENTITY_V1
    // Each QVulkanWindow frame slot owns a separate sampled image. Remember the
    // exact producer frame stored in that slot so expose/paint-driven duplicate
    // presents do not recopy and retransmit unchanged screen pixels.
    std::array<std::uint64_t, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directScreenFrameSerials{};
    std::array<std::uint64_t, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directScreenGenerations{};
    std::array<std::array<QSize, 2>, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directScreenExtents{};
    std::array<VkSampler, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directDescriptorScreenSamplers{};
    std::array<bool, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT>
        m_directDescriptorInitialized{};

    // MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1
    std::unique_ptr<MelonPrime::Vulkan::NativeRasterGpu>
        m_nativeRasterGpu;

    bool m_ready = false;
};

class PresenterWindow final : public QVulkanWindow
{
public:
    explicit PresenterWindow(ScreenPanelVulkan* panel) : m_panel(panel)
    {
        setFlags(QVulkanWindow::PersistentResources);
        setFlag(Qt::WindowDoesNotAcceptFocus, true);
        setPreferredColorFormats({
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM});
    }

    QVulkanWindowRenderer* createRenderer() override
    {
        return new PresenterRenderer(this, m_panel);
    }

    bool event(QEvent* event) override
    {
        switch (event->type())
        {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
        case QEvent::Enter:
        case QEvent::Leave:
            return QApplication::sendEvent(m_panel, event);
        default:
            return QVulkanWindow::event(event);
        }
    }

private:
    ScreenPanelVulkan* m_panel;
};

void PresenterRenderer::fail(const char* stage, VkResult result)
{
    m_ready = false;
    Log(LogLevel::Error,
        "[MelonPrime] Vulkan presenter failure: stage=%s VkResult=%d\n",
        stage, static_cast<int>(result));
    if (result == VK_ERROR_DEVICE_LOST && m_panel)
        m_panel->phase13NotifyDeviceLoss(stage, static_cast<int>(result));
}

std::uint32_t PresenterRenderer::findMemoryType(
    std::uint32_t bits, VkMemoryPropertyFlags required) const
{
    VkPhysicalDeviceMemoryProperties properties{};
    m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(
        m_window->physicalDevice(), &properties);
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
    {
        if ((bits & (1u << index)) &&
            (properties.memoryTypes[index].propertyFlags & required) == required)
        {
            return index;
        }
    }
    return std::numeric_limits<std::uint32_t>::max();
}

bool PresenterRenderer::createShaderModule(
    const std::uint32_t* code, std::size_t size, VkShaderModule& module)
{
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size;
    info.pCode = code;
    const VkResult result = m_df->vkCreateShaderModule(m_device, &info, nullptr, &module);
    if (result != VK_SUCCESS)
        fail("vkCreateShaderModule", result);
    return result == VK_SUCCESS;
}

bool PresenterRenderer::createStaticResources()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo descriptorInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorInfo.bindingCount = 1;
    descriptorInfo.pBindings = &binding;
    VkResult result = m_df->vkCreateDescriptorSetLayout(
        m_device, &descriptorInfo, nullptr, &m_descriptorLayout);
    if (result != VK_SUCCESS)
    {
        fail("vkCreateDescriptorSetLayout", result);
        return false;
    }

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorLayout;
    result = m_df->vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout);
    if (result != VK_SUCCESS)
    {
        fail("vkCreatePipelineLayout", result);
        return false;
    }

    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    const auto* phase13Properties = m_window->physicalDeviceProperties();
    MelonPrime::Vulkan::Phase13PipelineCacheLoad phase13Cache;
    if (phase13Properties)
    {
        m_phase13PipelineCacheIdentity =
            MelonPrime::Vulkan::BuildPhase13PipelineCacheIdentity(*phase13Properties);
        m_phase13PipelineCacheIdentityValid = true;
        phase13Cache = MelonPrime::Vulkan::LoadPhase13PipelineCache(
            m_phase13PipelineCacheIdentity);
        if (phase13Cache.Warm)
        {
            cacheInfo.initialDataSize = static_cast<std::size_t>(phase13Cache.Payload.size());
            cacheInfo.pInitialData = phase13Cache.Payload.constData();
            m_phase13PipelineCacheWarm = true;
        }
    }
    result = m_df->vkCreatePipelineCache(m_device, &cacheInfo, nullptr, &m_pipelineCache);
    if (result != VK_SUCCESS && phase13Cache.Warm)
    {
        Log(LogLevel::Warn,
            "[MelonPrime] Vulkan pipeline cache rejected by driver; retrying cold.\n");
        cacheInfo.initialDataSize = 0;
        cacheInfo.pInitialData = nullptr;
        m_phase13PipelineCacheWarm = false;
        result = m_df->vkCreatePipelineCache(m_device, &cacheInfo, nullptr, &m_pipelineCache);
    }
    if (result != VK_SUCCESS)
    {
        fail("vkCreatePipelineCache", result);
        return false;
    }

    auto createSampler = [this](VkFilter filter, VkSampler& sampler) {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = filter;
        info.minFilter = filter;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod = 0.0f;
        return m_df->vkCreateSampler(m_device, &info, nullptr, &sampler);
    };
    result = createSampler(VK_FILTER_NEAREST, m_nearestSampler);
    if (result == VK_SUCCESS)
        result = createSampler(VK_FILTER_LINEAR, m_linearSampler);
    if (result != VK_SUCCESS)
    {
        fail("vkCreateSampler", result);
        return false;
    }

    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    if (!createShaderModule(
            melonDS::Vulkan::Shaders::kVulkanPresenterVertexSpirv,
            sizeof(melonDS::Vulkan::Shaders::kVulkanPresenterVertexSpirv), vertex) ||
        !createShaderModule(
            melonDS::Vulkan::Shaders::kVulkanPresenterFragmentSpirv,
            sizeof(melonDS::Vulkan::Shaders::kVulkanPresenterFragmentSpirv), fragment))
    {
        if (vertex)
            m_df->vkDestroyShaderModule(m_device, vertex, nullptr);
        if (fragment)
            m_df->vkDestroyShaderModule(m_device, fragment, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
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
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewport;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depth;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_window->defaultRenderPass();
    result = m_df->vkCreateGraphicsPipelines(
        m_device, m_pipelineCache, 1, &pipelineInfo, nullptr, &m_pipeline);
    m_df->vkDestroyShaderModule(m_device, vertex, nullptr);
    m_df->vkDestroyShaderModule(m_device, fragment, nullptr);
    if (result != VK_SUCCESS)
    {
        fail("vkCreateGraphicsPipelines", result);
        return false;
    }
    m_directStaticReady = createDirectStaticResources();
    if (!m_directStaticReady)
    {
        Log(
            LogLevel::Warn,
            "[MelonPrime] Vulkan direct compositor disabled; "
            "QPainter fallback remains available.\n");
    }
    savePipelineCache();
    return true;
}

bool PresenterRenderer::createDirectStaticResources()
{
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1] = bindings[0];
    bindings[1].binding = 1;
    bindings[2] = bindings[0];
    bindings[2].binding = 2;
    bindings[3] = bindings[0];
    bindings[3].binding = 3;
    bindings[4] = bindings[0];
    bindings[4].binding = 4;
    bindings[5] = bindings[0];
    bindings[5].binding = 5;

    VkDescriptorSetLayoutCreateInfo descriptorInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorInfo.bindingCount =
        static_cast<std::uint32_t>(bindings.size());
    descriptorInfo.pBindings = bindings.data();

    VkResult result = m_df->vkCreateDescriptorSetLayout(
        m_device,
        &descriptorInfo,
        nullptr,
        &m_directDescriptorLayout);
    if (result != VK_SUCCESS)
    {
        Log(
            LogLevel::Warn,
            "[MelonPrime] Vulkan direct compositor unavailable: "
            "vkCreateDescriptorSetLayout=%d\n",
            static_cast<int>(result));
        return false;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(VulkanDirectPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_directDescriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    result = m_df->vkCreatePipelineLayout(
        m_device,
        &layoutInfo,
        nullptr,
        &m_directPipelineLayout);
    if (result != VK_SUCCESS)
    {
        Log(
            LogLevel::Warn,
            "[MelonPrime] Vulkan direct compositor unavailable: "
            "vkCreatePipelineLayout=%d\n",
            static_cast<int>(result));
        return false;
    }

    const auto createDirectShader =
        [this](const std::uint32_t* code, std::size_t size, VkShaderModule& module)
        {
            VkShaderModuleCreateInfo info{
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = size;
            info.pCode = code;
            return m_df->vkCreateShaderModule(
                m_device, &info, nullptr, &module);
        };

    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    result = createDirectShader(
        melonDS::Vulkan::Shaders::kVulkanPhase10PresenterVertexSpirv,
        sizeof(melonDS::Vulkan::Shaders::kVulkanPhase10PresenterVertexSpirv),
        vertex);
    if (result == VK_SUCCESS)
    {
        result = createDirectShader(
            melonDS::Vulkan::Shaders::kVulkanPhase10PresenterFragmentSpirv,
            sizeof(melonDS::Vulkan::Shaders::kVulkanPhase10PresenterFragmentSpirv),
            fragment);
    }
    if (result != VK_SUCCESS)
    {
        if (vertex)
            m_df->vkDestroyShaderModule(m_device, vertex, nullptr);
        if (fragment)
            m_df->vkDestroyShaderModule(m_device, fragment, nullptr);
        Log(
            LogLevel::Warn,
            "[MelonPrime] Vulkan direct compositor unavailable: "
            "shader module=%d\n",
            static_cast<int>(result));
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
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
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewport;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depth;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = m_directPipelineLayout;
    pipelineInfo.renderPass = m_window->defaultRenderPass();

    result = m_df->vkCreateGraphicsPipelines(
        m_device,
        m_pipelineCache,
        1,
        &pipelineInfo,
        nullptr,
        &m_directPipeline);
    m_df->vkDestroyShaderModule(m_device, vertex, nullptr);
    m_df->vkDestroyShaderModule(m_device, fragment, nullptr);

    if (result != VK_SUCCESS)
    {
        Log(
            LogLevel::Warn,
            "[MelonPrime] Vulkan direct compositor unavailable: "
            "vkCreateGraphicsPipelines=%d\n",
            static_cast<int>(result));
        return false;
    }

    return true;
}

bool PresenterRenderer::createDirectResources(
    const QSize& screenSize,
    const QSize& overlaySize)
{
    if (screenSize.isEmpty() || overlaySize.isEmpty())
        return false;

    destroyDirectResources();

    m_directScreenSize = screenSize;
    m_directOverlaySize = overlaySize;
    m_directFrameCount = m_window->concurrentFrameCount();

    const VkDeviceSize alignment = std::max<VkDeviceSize>(
        4,
        m_window->physicalDeviceProperties()
            ->limits.optimalBufferCopyOffsetAlignment);
    const VkDeviceSize screenBytes =
        static_cast<VkDeviceSize>(screenSize.width()) *
        screenSize.height() * 4u;
    const VkDeviceSize overlayBytes =
        static_cast<VkDeviceSize>(overlaySize.width()) *
        overlaySize.height() * 4u;

    m_directTopOffset = 0;
    m_directBottomOffset = AlignUp(screenBytes, alignment);
    m_directOverlayOffset = AlignUp(
        m_directBottomOffset + screenBytes,
        alignment);
    m_directFrameStride = AlignUp(
        m_directOverlayOffset + overlayBytes,
        alignment);

    VkBufferCreateInfo bufferInfo{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size =
        m_directFrameStride *
        static_cast<VkDeviceSize>(m_directFrameCount);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VkResult result = m_df->vkCreateBuffer(
        m_device,
        &bufferInfo,
        nullptr,
        &m_directStagingBuffer);
    if (result != VK_SUCCESS)
        goto direct_resource_failure;

    {
        VkMemoryRequirements requirements{};
        m_df->vkGetBufferMemoryRequirements(
            m_device,
            m_directStagingBuffer,
            &requirements);
        const std::uint32_t memoryType = findMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memoryType == std::numeric_limits<std::uint32_t>::max())
        {
            result = VK_ERROR_MEMORY_MAP_FAILED;
            goto direct_resource_failure;
        }

        VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        result = m_df->vkAllocateMemory(
            m_device,
            &allocation,
            nullptr,
            &m_directStagingMemory);
        if (result == VK_SUCCESS)
        {
            result = m_df->vkBindBufferMemory(
                m_device,
                m_directStagingBuffer,
                m_directStagingMemory,
                0);
        }
        if (result == VK_SUCCESS)
        {
            result = m_df->vkMapMemory(
                m_device,
                m_directStagingMemory,
                0,
                bufferInfo.size,
                0,
                reinterpret_cast<void**>(&m_directStagingMap));
        }
        if (result != VK_SUCCESS)
            goto direct_resource_failure;
    }

    for (int index = 0; index < m_directFrameCount; ++index)
    {
        const auto createImage =
            [this](
                const QSize& size,
                std::uint32_t layers,
                VkImageViewType viewType,
                VkImage& image,
                VkDeviceMemory& memory,
                VkImageView& view) -> VkResult
            {
                VkImageCreateInfo imageInfo{
                    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
                imageInfo.format = kUploadFormat;
                imageInfo.extent = {
                    static_cast<std::uint32_t>(size.width()),
                    static_cast<std::uint32_t>(size.height()),
                    1};
                imageInfo.mipLevels = 1;
                imageInfo.arrayLayers = layers;
                imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.usage =
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT;
                imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                VkResult localResult = m_df->vkCreateImage(
                    m_device,
                    &imageInfo,
                    nullptr,
                    &image);
                if (localResult != VK_SUCCESS)
                    return localResult;

                VkMemoryRequirements requirements{};
                m_df->vkGetImageMemoryRequirements(
                    m_device,
                    image,
                    &requirements);
                const std::uint32_t memoryType = findMemoryType(
                    requirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                if (memoryType ==
                    std::numeric_limits<std::uint32_t>::max())
                {
                    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
                }

                VkMemoryAllocateInfo allocation{
                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocation.allocationSize = requirements.size;
                allocation.memoryTypeIndex = memoryType;
                localResult = m_df->vkAllocateMemory(
                    m_device,
                    &allocation,
                    nullptr,
                    &memory);
                if (localResult == VK_SUCCESS)
                {
                    localResult = m_df->vkBindImageMemory(
                        m_device,
                        image,
                        memory,
                        0);
                }
                if (localResult != VK_SUCCESS)
                    return localResult;

                VkImageViewCreateInfo viewInfo{
                    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                viewInfo.image = image;
                viewInfo.viewType = viewType;
                viewInfo.format = kUploadFormat;
                viewInfo.subresourceRange.aspectMask =
                    VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.layerCount = layers;
                return m_df->vkCreateImageView(
                    m_device,
                    &viewInfo,
                    nullptr,
                    &view);
            };

        result = createImage(
            screenSize,
            2,
            VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            m_directScreenImages[index],
            m_directScreenMemory[index],
            m_directScreenViews[index]);
        if (result != VK_SUCCESS)
            goto direct_resource_failure;

        result = createImage(
            overlaySize,
            1,
            VK_IMAGE_VIEW_TYPE_2D,
            m_directOverlayImages[index],
            m_directOverlayMemory[index],
            m_directOverlayViews[index]);
        if (result != VK_SUCCESS)
            goto direct_resource_failure;
    }

    {
        VkDescriptorPoolSize poolSize{
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            static_cast<std::uint32_t>(m_directFrameCount * 6)};
        VkDescriptorPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets =
            static_cast<std::uint32_t>(m_directFrameCount);
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        result = m_df->vkCreateDescriptorPool(
            m_device,
            &poolInfo,
            nullptr,
            &m_directDescriptorPool);
        if (result != VK_SUCCESS)
            goto direct_resource_failure;

        std::array<
            VkDescriptorSetLayout,
            QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT> layouts{};
        layouts.fill(m_directDescriptorLayout);

        VkDescriptorSetAllocateInfo setInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool = m_directDescriptorPool;
        setInfo.descriptorSetCount =
            static_cast<std::uint32_t>(m_directFrameCount);
        setInfo.pSetLayouts = layouts.data();
        result = m_df->vkAllocateDescriptorSets(
            m_device,
            &setInfo,
            m_directDescriptorSets.data());
        if (result != VK_SUCCESS)
            goto direct_resource_failure;
    }

    m_directScreenLayouts.fill(VK_IMAGE_LAYOUT_UNDEFINED);
    m_directOverlayLayouts.fill(VK_IMAGE_LAYOUT_UNDEFINED);
    m_directScreenFrameSerials.fill(0);
    m_directScreenGenerations.fill(0);
    for (auto& extents : m_directScreenExtents)
        extents = std::array<QSize, 2>{};
    m_directDescriptorScreenSamplers.fill(VK_NULL_HANDLE);
    m_directDescriptorInitialized.fill(false);
    return true;

direct_resource_failure:
    Log(
        LogLevel::Warn,
        "[MelonPrime] Vulkan direct compositor resources unavailable: "
        "VkResult=%d\n",
        static_cast<int>(result));
    if (result == VK_ERROR_DEVICE_LOST && m_panel)
    {
        m_panel->phase13NotifyDeviceLoss(
            "direct compositor resources",
            static_cast<int>(result));
    }
    destroyDirectResources();
    return false;
}

void PresenterRenderer::destroyDirectResources()
{
    if (!m_df || !m_device)
        return;

    if (m_directStagingMap)
    {
        m_df->vkUnmapMemory(
            m_device,
            m_directStagingMemory);
        m_directStagingMap = nullptr;
    }

    if (m_directDescriptorPool)
    {
        m_df->vkDestroyDescriptorPool(
            m_device,
            m_directDescriptorPool,
            nullptr);
    }
    m_directDescriptorPool = VK_NULL_HANDLE;

    for (int index = 0;
         index < QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT;
         ++index)
    {
        if (m_directScreenViews[index])
        {
            m_df->vkDestroyImageView(
                m_device,
                m_directScreenViews[index],
                nullptr);
        }
        if (m_directScreenImages[index])
        {
            m_df->vkDestroyImage(
                m_device,
                m_directScreenImages[index],
                nullptr);
        }
        if (m_directScreenMemory[index])
        {
            m_df->vkFreeMemory(
                m_device,
                m_directScreenMemory[index],
                nullptr);
        }

        if (m_directOverlayViews[index])
        {
            m_df->vkDestroyImageView(
                m_device,
                m_directOverlayViews[index],
                nullptr);
        }
        if (m_directOverlayImages[index])
        {
            m_df->vkDestroyImage(
                m_device,
                m_directOverlayImages[index],
                nullptr);
        }
        if (m_directOverlayMemory[index])
        {
            m_df->vkFreeMemory(
                m_device,
                m_directOverlayMemory[index],
                nullptr);
        }

        m_directDescriptorSets[index] = VK_NULL_HANDLE;
        m_directScreenImages[index] = VK_NULL_HANDLE;
        m_directScreenMemory[index] = VK_NULL_HANDLE;
        m_directScreenViews[index] = VK_NULL_HANDLE;
        m_directScreenLayouts[index] = VK_IMAGE_LAYOUT_UNDEFINED;
        m_directOverlayImages[index] = VK_NULL_HANDLE;
        m_directOverlayMemory[index] = VK_NULL_HANDLE;
        m_directOverlayViews[index] = VK_NULL_HANDLE;
        m_directOverlayLayouts[index] = VK_IMAGE_LAYOUT_UNDEFINED;
        m_directScreenFrameSerials[index] = 0;
        m_directScreenGenerations[index] = 0;
        m_directScreenExtents[index] = std::array<QSize, 2>{};
        m_directDescriptorScreenSamplers[index] = VK_NULL_HANDLE;
        m_directDescriptorInitialized[index] = false;
    }

    if (m_directStagingBuffer)
    {
        m_df->vkDestroyBuffer(
            m_device,
            m_directStagingBuffer,
            nullptr);
    }
    if (m_directStagingMemory)
    {
        m_df->vkFreeMemory(
            m_device,
            m_directStagingMemory,
            nullptr);
    }

    m_directStagingBuffer = VK_NULL_HANDLE;
    m_directStagingMemory = VK_NULL_HANDLE;
    m_directTopOffset = 0;
    m_directBottomOffset = 0;
    m_directOverlayOffset = 0;
    m_directFrameStride = 0;
    m_directScreenSize = {};
    m_directOverlaySize = {};
    m_directFrameCount = 0;
}

bool PresenterRenderer::renderDirectFrame(
    const VulkanDirectFrameSnapshot& snapshot)
{
    if (!m_directStaticReady ||
        snapshot.Screens[0].isNull() ||
        snapshot.Screens[1].isNull() ||
        snapshot.LogicalSize.isEmpty() ||
        snapshot.NumScreens <= 0)
    {
        return false;
    }

    const QSize requiredScreen(
        std::max(
            snapshot.Screens[0].width(),
            snapshot.Screens[1].width()),
        std::max(
            snapshot.Screens[0].height(),
            snapshot.Screens[1].height()));
    const std::array<QSize, 2> screenExtents{{
        snapshot.Screens[0].size(),
        snapshot.Screens[1].size()}};
    const bool screenIdentityValid =
        snapshot.FrameSerial != 0 && snapshot.Generation != 0;
    const QSize requiredOverlay(
        std::max(1, snapshot.HudSource.width()),
        std::max(1, snapshot.HudSource.height()));

    const bool needsResources =
        !m_directStagingMap ||
        m_directFrameCount != m_window->concurrentFrameCount() ||
        requiredScreen.width() > m_directScreenSize.width() ||
        requiredScreen.height() > m_directScreenSize.height() ||
        requiredOverlay.width() > m_directOverlaySize.width() ||
        requiredOverlay.height() > m_directOverlaySize.height();

    if (needsResources)
    {
        const QSize newScreen(
            RoundDirectExtent(std::max(
                requiredScreen.width(),
                m_directScreenSize.width())),
            RoundDirectExtent(std::max(
                requiredScreen.height(),
                m_directScreenSize.height())));
        const QSize newOverlay(
            RoundDirectExtent(std::max(
                requiredOverlay.width(),
                m_directOverlaySize.width())),
            RoundDirectExtent(std::max(
                requiredOverlay.height(),
                m_directOverlaySize.height())));

        m_df->vkDeviceWaitIdle(m_device);
        if (!createDirectResources(newScreen, newOverlay))
            return false;
    }

    const int slot = m_window->currentFrame();
    const VkDeviceSize frameBase =
        m_directFrameStride *
        static_cast<VkDeviceSize>(slot);
    std::uint8_t* const staging =
        m_directStagingMap + frameBase;

    const bool screenUploadRequired =
        !screenIdentityValid ||
        m_directScreenLayouts[slot] == VK_IMAGE_LAYOUT_UNDEFINED ||
        m_directScreenFrameSerials[slot] != snapshot.FrameSerial ||
        m_directScreenGenerations[slot] != snapshot.Generation ||
        m_directScreenExtents[slot] != screenExtents;
    if (screenUploadRequired)
    {
        CopyImageTightly(
            staging + m_directTopOffset,
            snapshot.Screens[0]);
        CopyImageTightly(
            staging + m_directBottomOffset,
            snapshot.Screens[1]);
    }

    const bool hasHudOverlay =
        !snapshot.HudOverlay.isNull() &&
        !snapshot.HudSource.isEmpty();
    const bool overlayUploadRequired =
        hasHudOverlay ||
        m_directOverlayLayouts[slot] == VK_IMAGE_LAYOUT_UNDEFINED;
    QSize overlayCopySize(1, 1);
    if (overlayUploadRequired)
    {
        std::memset(
            staging + m_directOverlayOffset,
            0,
            4);
        if (hasHudOverlay)
        {
            const QRect clippedSource =
                snapshot.HudSource.intersected(
                    snapshot.HudOverlay.rect());
            if (!clippedSource.isEmpty())
            {
                overlayCopySize = clippedSource.size();
                CopyImageRectTightly(
                    staging + m_directOverlayOffset,
                    snapshot.HudOverlay,
                    clippedSource);
            }
        }
    }

    VkCommandBuffer command =
        m_window->currentCommandBuffer();

    MelonPrime::Vulkan::NativeRasterViews nativeViews;
    if (!m_nativeRasterGpu)
    {
        m_nativeRasterGpu =
            std::make_unique<MelonPrime::Vulkan::NativeRasterGpu>();
    }
    if (!snapshot.NativeRaster ||
        !m_nativeRasterGpu->Render(
            m_window,
            m_df,
            command,
            slot,
            *snapshot.NativeRaster,
            nativeViews))
    {
        nativeViews = {};
    }

    if (screenUploadRequired)
    {
    VkImageMemoryBarrier screenToTransfer{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    screenToTransfer.srcAccessMask =
        m_directScreenLayouts[slot] ==
            VK_IMAGE_LAYOUT_UNDEFINED
        ? 0
        : VK_ACCESS_SHADER_READ_BIT;
    screenToTransfer.dstAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;
    screenToTransfer.oldLayout =
        m_directScreenLayouts[slot];
    screenToTransfer.newLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    screenToTransfer.srcQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    screenToTransfer.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    screenToTransfer.image =
        m_directScreenImages[slot];
    screenToTransfer.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    screenToTransfer.subresourceRange.levelCount = 1;
    screenToTransfer.subresourceRange.layerCount = 2;

    m_df->vkCmdPipelineBarrier(
        command,
        m_directScreenLayouts[slot] ==
            VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &screenToTransfer);

    std::array<VkBufferImageCopy, 2> screenCopies{};
    for (std::uint32_t layer = 0; layer < 2; ++layer)
    {
        const QImage& image = snapshot.Screens[layer];
        auto& copy = screenCopies[layer];
        copy.bufferOffset =
            frameBase +
            (layer == 0
                ? m_directTopOffset
                : m_directBottomOffset);
        copy.imageSubresource.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.baseArrayLayer = layer;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {
            static_cast<std::uint32_t>(image.width()),
            static_cast<std::uint32_t>(image.height()),
            1};
    }

    m_df->vkCmdCopyBufferToImage(
        command,
        m_directStagingBuffer,
        m_directScreenImages[slot],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<std::uint32_t>(screenCopies.size()),
        screenCopies.data());

    VkImageMemoryBarrier screenToSample{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    screenToSample.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;
    screenToSample.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;
    screenToSample.oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    screenToSample.newLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    screenToSample.srcQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    screenToSample.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    screenToSample.image =
        m_directScreenImages[slot];
    screenToSample.subresourceRange =
        screenToTransfer.subresourceRange;

    m_df->vkCmdPipelineBarrier(
        command,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &screenToSample);
        m_directScreenLayouts[slot] =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (screenIdentityValid)
        {
            m_directScreenFrameSerials[slot] = snapshot.FrameSerial;
            m_directScreenGenerations[slot] = snapshot.Generation;
            m_directScreenExtents[slot] = screenExtents;
        }
        else
        {
            m_directScreenFrameSerials[slot] = 0;
            m_directScreenGenerations[slot] = 0;
            m_directScreenExtents[slot] = screenExtents;
        }
    }

    if (overlayUploadRequired)
    {
    VkImageMemoryBarrier overlayToTransfer{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    overlayToTransfer.srcAccessMask =
        m_directOverlayLayouts[slot] ==
            VK_IMAGE_LAYOUT_UNDEFINED
        ? 0
        : VK_ACCESS_SHADER_READ_BIT;
    overlayToTransfer.dstAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;
    overlayToTransfer.oldLayout =
        m_directOverlayLayouts[slot];
    overlayToTransfer.newLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    overlayToTransfer.srcQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    overlayToTransfer.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    overlayToTransfer.image =
        m_directOverlayImages[slot];
    overlayToTransfer.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    overlayToTransfer.subresourceRange.levelCount = 1;
    overlayToTransfer.subresourceRange.layerCount = 1;

    m_df->vkCmdPipelineBarrier(
        command,
        m_directOverlayLayouts[slot] ==
            VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &overlayToTransfer);

    VkBufferImageCopy overlayCopy{};
    overlayCopy.bufferOffset =
        frameBase + m_directOverlayOffset;
    overlayCopy.imageSubresource.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    overlayCopy.imageSubresource.layerCount = 1;
    overlayCopy.imageExtent = {
        static_cast<std::uint32_t>(
            overlayCopySize.width()),
        static_cast<std::uint32_t>(
            overlayCopySize.height()),
        1};

    m_df->vkCmdCopyBufferToImage(
        command,
        m_directStagingBuffer,
        m_directOverlayImages[slot],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &overlayCopy);

    VkImageMemoryBarrier overlayToSample{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    overlayToSample.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;
    overlayToSample.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;
    overlayToSample.oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    overlayToSample.newLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    overlayToSample.srcQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    overlayToSample.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    overlayToSample.image =
        m_directOverlayImages[slot];
    overlayToSample.subresourceRange =
        overlayToTransfer.subresourceRange;

    m_df->vkCmdPipelineBarrier(
        command,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &overlayToSample);
        m_directOverlayLayouts[slot] =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    const VkSampler desiredScreenSampler =
        m_panel->linearFilteringForVulkan()
        ? m_linearSampler
        : m_nearestSampler;
    VkDescriptorImageInfo screenInfo{};
    screenInfo.sampler = desiredScreenSampler;
    screenInfo.imageView = m_directScreenViews[slot];
    screenInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo overlayInfo{};
    overlayInfo.sampler = m_linearSampler;
    overlayInfo.imageView = m_directOverlayViews[slot];
    overlayInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo radarInfo = screenInfo;
    radarInfo.sampler = m_linearSampler;

    VkDescriptorImageInfo nativeHighInfo{};
    nativeHighInfo.sampler = nativeViews.Valid
        ? nativeViews.Sampler : m_nearestSampler;
    nativeHighInfo.imageView = nativeViews.Valid
        ? nativeViews.HighResolution : m_directOverlayViews[slot];
    nativeHighInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo nativeReferenceInfo{};
    nativeReferenceInfo.sampler = nativeViews.Valid
        ? nativeViews.Sampler : m_nearestSampler;
    nativeReferenceInfo.imageView = nativeViews.Valid
        ? nativeViews.NativeReference : m_directOverlayViews[slot];
    nativeReferenceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo nativeCoverageInfo{};
    nativeCoverageInfo.sampler = nativeViews.Valid
        ? nativeViews.Sampler : m_nearestSampler;
    nativeCoverageInfo.imageView = nativeViews.Valid
        ? nativeViews.Coverage : m_directOverlayViews[slot];
    nativeCoverageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 6> writes{};
    const VkDescriptorImageInfo* infos[] = {
        &screenInfo,
        &overlayInfo,
        &radarInfo,
        &nativeHighInfo,
        &nativeReferenceInfo,
        &nativeCoverageInfo};
    for (std::uint32_t binding = 0; binding < writes.size(); ++binding)
    {
        writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[binding].dstSet = m_directDescriptorSets[slot];
        writes[binding].dstBinding = binding;
        writes[binding].descriptorCount = 1;
        writes[binding].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[binding].pImageInfo = infos[binding];
    }
    m_df->vkUpdateDescriptorSets(
        m_device,
        static_cast<std::uint32_t>(writes.size()),
        writes.data(),
        0,
        nullptr);
    m_directDescriptorScreenSamplers[slot] = desiredScreenSampler;
    m_directDescriptorInitialized[slot] = true;

    const QSize swapSize =
        m_window->swapChainImageSize();
    VkClearValue clearValues[2]{};
    clearValues[0].color =
        {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPass{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPass.renderPass =
        m_window->defaultRenderPass();
    renderPass.framebuffer =
        m_window->currentFramebuffer();
    renderPass.renderArea.extent = {
        static_cast<std::uint32_t>(swapSize.width()),
        static_cast<std::uint32_t>(swapSize.height())};
    renderPass.clearValueCount = 2;
    renderPass.pClearValues = clearValues;

    m_df->vkCmdBeginRenderPass(
        command,
        &renderPass,
        VK_SUBPASS_CONTENTS_INLINE);
    m_df->vkCmdBindPipeline(
        command,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_directPipeline);
    m_df->vkCmdBindDescriptorSets(
        command,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_directPipelineLayout,
        0,
        1,
        &m_directDescriptorSets[slot],
        0,
        nullptr);

    VkViewport viewport{};
    viewport.width =
        static_cast<float>(swapSize.width());
    viewport.height =
        static_cast<float>(swapSize.height());
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = renderPass.renderArea.extent;
    m_df->vkCmdSetViewport(
        command, 0, 1, &viewport);
    m_df->vkCmdSetScissor(
        command, 0, 1, &scissor);

    const float panelWidth =
        static_cast<float>(
            snapshot.LogicalSize.width());
    const float panelHeight =
        static_cast<float>(
            snapshot.LogicalSize.height());

    const auto drawQuad =
        [&](const std::array<float, 6>& matrix,
            float localWidth,
            float localHeight,
            float sourceScaleX,
            float sourceScaleY,
            std::uint32_t mode,
            std::uint32_t layer,
            const std::array<float, 4>& radar)
        {
            VulkanDirectPushConstants push{};
            push.Transform0 = {
                matrix[0],
                matrix[1],
                matrix[2],
                matrix[3]};
            push.Transform1 = {
                matrix[4],
                matrix[5],
                panelWidth,
                panelHeight};
            push.Geometry = {
                localWidth,
                localHeight,
                sourceScaleX,
                sourceScaleY};
            push.Radar = radar;
            push.Params = {
                mode,
                layer,
                nativeViews.Valid
                    ? snapshot.NativeRaster->EngineAScreen + 1u
                    : 0u,
                nativeViews.Valid
                    ? static_cast<std::uint32_t>(
                        snapshot.NativeRaster->MasterBrightnessA)
                    : 0u};

            m_df->vkCmdPushConstants(
                command,
                m_directPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT |
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(push),
                &push);
            m_df->vkCmdDraw(
                command, 6, 1, 0, 0);
        };

    for (int index = 0;
         index < snapshot.NumScreens;
         ++index)
    {
        const int kind =
            snapshot.ScreenKinds[index];
        if (kind < 0 || kind > 1)
            continue;
        const QImage& image =
            snapshot.Screens[kind];
        drawQuad(
            snapshot.ScreenMatrices[index],
            256.0f,
            192.0f,
            static_cast<float>(image.width()) /
                m_directScreenSize.width(),
            static_cast<float>(image.height()) /
                m_directScreenSize.height(),
            0,
            static_cast<std::uint32_t>(kind),
            {});
    }

    if (!snapshot.HudOverlay.isNull() &&
        !snapshot.HudSource.isEmpty() &&
        !snapshot.HudDestination.isEmpty())
    {
        const std::array<float, 6> matrix{{
            1.0f,
            0.0f,
            0.0f,
            1.0f,
            static_cast<float>(
                snapshot.HudDestination.x()),
            static_cast<float>(
                snapshot.HudDestination.y())}};
        drawQuad(
            matrix,
            static_cast<float>(
                snapshot.HudDestination.width()),
            static_cast<float>(
                snapshot.HudDestination.height()),
            static_cast<float>(
                snapshot.HudSource.width()) /
                m_directOverlaySize.width(),
            static_cast<float>(
                snapshot.HudSource.height()) /
                m_directOverlaySize.height(),
            1,
            0,
            {});
    }

    if (snapshot.RadarVisible &&
        !snapshot.RadarDestination.isEmpty())
    {
        const std::array<float, 6> matrix{{
            static_cast<float>(
                snapshot.RadarDestination.width()),
            0.0f,
            0.0f,
            static_cast<float>(
                snapshot.RadarDestination.height()),
            static_cast<float>(
                snapshot.RadarDestination.x()),
            static_cast<float>(
                snapshot.RadarDestination.y())}};
        const std::array<float, 4> radar{{
            static_cast<float>(
                snapshot.RadarSourceCenter.x()),
            static_cast<float>(
                snapshot.RadarSourceCenter.y()),
            snapshot.RadarSourceRadius,
            snapshot.RadarOpacity}};
        const QImage& bottom =
            snapshot.Screens[1];
        drawQuad(
            matrix,
            1.0f,
            1.0f,
            static_cast<float>(bottom.width()) /
                m_directScreenSize.width(),
            static_cast<float>(bottom.height()) /
                m_directScreenSize.height(),
            2,
            1,
            radar);
    }

    m_df->vkCmdEndRenderPass(command);
    return true;
}


bool PresenterRenderer::createFrameResources(const QSize& size)
{
    if (size.isEmpty())
        return false;
    destroyFrameResources();
    m_frameSize = size;
    m_frameCount = m_window->concurrentFrameCount();
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(size.width()) * size.height() * 4u;
    const VkDeviceSize alignment = std::max<VkDeviceSize>(
        4, m_window->physicalDeviceProperties()->limits.optimalBufferCopyOffsetAlignment);
    m_frameStride = AlignUp(bytes, alignment);

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = m_frameStride * static_cast<VkDeviceSize>(m_frameCount);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkResult result = m_df->vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_stagingBuffer);
    if (result != VK_SUCCESS)
    {
        fail("vkCreateBuffer(staging)", result);
        return false;
    }
    VkMemoryRequirements bufferRequirements{};
    m_df->vkGetBufferMemoryRequirements(m_device, m_stagingBuffer, &bufferRequirements);
    const std::uint32_t hostType = findMemoryType(
        bufferRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (hostType == std::numeric_limits<std::uint32_t>::max())
    {
        fail("host-coherent staging memory unavailable");
        return false;
    }
    VkMemoryAllocateInfo bufferAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    bufferAlloc.allocationSize = bufferRequirements.size;
    bufferAlloc.memoryTypeIndex = hostType;
    result = m_df->vkAllocateMemory(m_device, &bufferAlloc, nullptr, &m_stagingMemory);
    if (result == VK_SUCCESS)
        result = m_df->vkBindBufferMemory(m_device, m_stagingBuffer, m_stagingMemory, 0);
    if (result == VK_SUCCESS)
    {
        result = m_df->vkMapMemory(m_device, m_stagingMemory, 0, bufferInfo.size, 0,
            reinterpret_cast<void**>(&m_stagingMap));
    }
    if (result != VK_SUCCESS)
    {
        fail("staging memory allocation/map", result);
        return false;
    }

    for (int index = 0; index < m_frameCount; ++index)
    {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = kUploadFormat;
        imageInfo.extent = {
            static_cast<std::uint32_t>(size.width()),
            static_cast<std::uint32_t>(size.height()), 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        result = m_df->vkCreateImage(m_device, &imageInfo, nullptr, &m_images[index]);
        if (result != VK_SUCCESS)
        {
            fail("vkCreateImage(frame)", result);
            return false;
        }
        VkMemoryRequirements imageRequirements{};
        m_df->vkGetImageMemoryRequirements(m_device, m_images[index], &imageRequirements);
        const std::uint32_t deviceType = findMemoryType(
            imageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (deviceType == std::numeric_limits<std::uint32_t>::max())
        {
            fail("device-local frame memory unavailable");
            return false;
        }
        VkMemoryAllocateInfo imageAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        imageAlloc.allocationSize = imageRequirements.size;
        imageAlloc.memoryTypeIndex = deviceType;
        result = m_df->vkAllocateMemory(
            m_device, &imageAlloc, nullptr, &m_imageMemory[index]);
        if (result == VK_SUCCESS)
            result = m_df->vkBindImageMemory(
                m_device, m_images[index], m_imageMemory[index], 0);
        if (result != VK_SUCCESS)
        {
            fail("frame image memory allocation/bind", result);
            return false;
        }
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = m_images[index];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = kUploadFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        result = m_df->vkCreateImageView(
            m_device, &viewInfo, nullptr, &m_imageViews[index]);
        if (result != VK_SUCCESS)
        {
            fail("vkCreateImageView(frame)", result);
            return false;
        }
        m_imageLayouts[index] = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    VkDescriptorPoolSize poolSize{
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        static_cast<std::uint32_t>(m_frameCount)};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = static_cast<std::uint32_t>(m_frameCount);
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    result = m_df->vkCreateDescriptorPool(
        m_device, &poolInfo, nullptr, &m_descriptorPool);
    if (result != VK_SUCCESS)
    {
        fail("vkCreateDescriptorPool(frame)", result);
        return false;
    }
    std::array<VkDescriptorSetLayout, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT> layouts{};
    layouts.fill(m_descriptorLayout);
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = m_descriptorPool;
    setInfo.descriptorSetCount = static_cast<std::uint32_t>(m_frameCount);
    setInfo.pSetLayouts = layouts.data();
    result = m_df->vkAllocateDescriptorSets(m_device, &setInfo, m_descriptorSets.data());
    if (result != VK_SUCCESS)
    {
        fail("vkAllocateDescriptorSets(frame)", result);
        return false;
    }
    return true;
}

void PresenterRenderer::destroyFrameResources()
{
    if (!m_df || !m_device)
        return;
    if (m_stagingMap)
    {
        m_df->vkUnmapMemory(m_device, m_stagingMemory);
        m_stagingMap = nullptr;
    }
    if (m_descriptorPool)
        m_df->vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    m_descriptorPool = VK_NULL_HANDLE;
    for (int index = 0; index < QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT; ++index)
    {
        if (m_imageViews[index])
            m_df->vkDestroyImageView(m_device, m_imageViews[index], nullptr);
        if (m_images[index])
            m_df->vkDestroyImage(m_device, m_images[index], nullptr);
        if (m_imageMemory[index])
            m_df->vkFreeMemory(m_device, m_imageMemory[index], nullptr);
        m_imageViews[index] = VK_NULL_HANDLE;
        m_images[index] = VK_NULL_HANDLE;
        m_imageMemory[index] = VK_NULL_HANDLE;
        m_descriptorSets[index] = VK_NULL_HANDLE;
        m_imageLayouts[index] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    if (m_stagingBuffer)
        m_df->vkDestroyBuffer(m_device, m_stagingBuffer, nullptr);
    if (m_stagingMemory)
        m_df->vkFreeMemory(m_device, m_stagingMemory, nullptr);
    m_stagingBuffer = VK_NULL_HANDLE;
    m_stagingMemory = VK_NULL_HANDLE;
    m_frameSize = {};
    m_frameCount = 0;
    m_frameStride = 0;
}

void PresenterRenderer::destroyStaticResources()
{
    if (!m_df || !m_device)
        return;

    if (m_directPipeline)
        m_df->vkDestroyPipeline(
            m_device, m_directPipeline, nullptr);
    if (m_directPipelineLayout)
        m_df->vkDestroyPipelineLayout(
            m_device, m_directPipelineLayout, nullptr);
    if (m_directDescriptorLayout)
        m_df->vkDestroyDescriptorSetLayout(
            m_device, m_directDescriptorLayout, nullptr);
    m_directPipeline = VK_NULL_HANDLE;
    m_directPipelineLayout = VK_NULL_HANDLE;
    m_directDescriptorLayout = VK_NULL_HANDLE;
    m_directStaticReady = false;

    if (m_pipeline)
        m_df->vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineCache)
    {
        savePipelineCache();
        m_df->vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);
    }
    if (m_pipelineLayout)
        m_df->vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_nearestSampler)
        m_df->vkDestroySampler(m_device, m_nearestSampler, nullptr);
    if (m_linearSampler)
        m_df->vkDestroySampler(m_device, m_linearSampler, nullptr);
    if (m_descriptorLayout)
        m_df->vkDestroyDescriptorSetLayout(m_device, m_descriptorLayout, nullptr);
    m_pipeline = VK_NULL_HANDLE;
    m_pipelineCache = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_nearestSampler = VK_NULL_HANDLE;
    m_linearSampler = VK_NULL_HANDLE;
    m_descriptorLayout = VK_NULL_HANDLE;
}

bool PresenterRenderer::savePipelineCache()
{
    if (!m_df || !m_device || !m_pipelineCache || !m_phase13PipelineCacheIdentityValid)
        return false;
    std::size_t size = 0;
    VkResult result = m_df->vkGetPipelineCacheData(m_device, m_pipelineCache, &size, nullptr);
    if (result != VK_SUCCESS || size == 0)
        return false;
    std::vector<std::uint8_t> data(size);
    result = m_df->vkGetPipelineCacheData(m_device, m_pipelineCache, &size, data.data());
    if (result != VK_SUCCESS)
        return false;
    data.resize(size);
    QString error;
    const bool saved = MelonPrime::Vulkan::SavePhase13PipelineCacheAtomic(
        m_phase13PipelineCacheIdentity, data.data(), data.size(), &error);
    if (!saved)
    {
        Log(LogLevel::Warn,
            "[MelonPrime] Vulkan pipeline cache save failed: %s\n",
            error.toUtf8().constData());
    }
    return saved;
}

void PresenterRenderer::completeFrame()
{
    if (m_panel)
        m_panel->phase13PresentCompleted();
    m_window->frameReady();
}

void PresenterRenderer::initResources()
{
    m_device = m_window->device();
    m_df = m_window->vulkanInstance()->deviceFunctions(m_device);
    m_ready = createStaticResources();
    if (m_ready)
    {
        const auto* properties = m_window->physicalDeviceProperties();
        Log(LogLevel::Info,
            "[MelonPrime] Vulkan presenter initialized: device=%s frames=%d swapchainFormat=%d\n",
            properties ? properties->deviceName : "unknown",
            m_window->concurrentFrameCount(), static_cast<int>(m_window->colorFormat()));
    }
}

void PresenterRenderer::releaseResources()
{
    if (m_df && m_device)
        m_df->vkDeviceWaitIdle(m_device);
    if (m_nativeRasterGpu)
    {
        m_nativeRasterGpu->Release();
        m_nativeRasterGpu.reset();
    }
    destroyDirectResources();
    destroyFrameResources();
    destroyStaticResources();
    m_ready = false;
}

void PresenterRenderer::logicalDeviceLost()
{
    Log(LogLevel::Error, "[MelonPrime] Vulkan presenter logical device lost\n");
    m_ready = false;
    if (m_panel)
        m_panel->phase13NotifyDeviceLoss("logical device", static_cast<int>(VK_ERROR_DEVICE_LOST));
}

void PresenterRenderer::physicalDeviceLost()
{
    Log(LogLevel::Error, "[MelonPrime] Vulkan presenter physical device lost\n");
    m_ready = false;
    if (m_panel)
        m_panel->phase13NotifyDeviceLoss("physical device", static_cast<int>(VK_ERROR_DEVICE_LOST));
}

void PresenterRenderer::startNextFrame()
{
    if (!m_ready)
    {
        completeFrame();
        return;
    }

    VulkanDirectFrameSnapshot directSnapshot;
    if (m_panel->prepareDirectFrameForVulkan(directSnapshot) &&
        renderDirectFrame(directSnapshot))
    {
        completeFrame();
        return;
    }

    const QImage& frame = m_panel->composeFrameForVulkan();
    if (frame.isNull())
    {
        completeFrame();
        return;
    }
    if (frame.size() != m_frameSize)
    {
        m_df->vkDeviceWaitIdle(m_device);
        if (!createFrameResources(frame.size()))
        {
            completeFrame();
            return;
        }
    }

    // MELONPRIME_VULKAN_ANDROIDSTYLE_PRESENTER_UPLOAD_V2
    // Emulator frames normally change every refresh. A full hash followed by a
    // second full dirty-rectangle scan is more expensive than one cache-friendly
    // memcpy and one full transfer.
    const int slot = m_window->currentFrame();
    const VkDeviceSize offset =
        m_frameStride * static_cast<VkDeviceSize>(slot);
    const std::size_t byteCount =
        static_cast<std::size_t>(frame.width()) * frame.height() * 4u;
    std::memcpy(m_stagingMap + offset, frame.constBits(), byteCount);

    VkCommandBuffer command = m_window->currentCommandBuffer();

    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.srcAccessMask =
        m_imageLayouts[slot] == VK_IMAGE_LAYOUT_UNDEFINED
        ? 0
        : VK_ACCESS_SHADER_READ_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = m_imageLayouts[slot];
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = m_images[slot];
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    m_df->vkCmdPipelineBarrier(
        command,
        m_imageLayouts[slot] == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toTransfer);

    VkBufferImageCopy copy{};
    copy.bufferOffset = offset;
    copy.bufferRowLength = static_cast<std::uint32_t>(frame.width());
    copy.bufferImageHeight = static_cast<std::uint32_t>(frame.height());
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {
        static_cast<std::uint32_t>(frame.width()),
        static_cast<std::uint32_t>(frame.height()),
        1};
    m_df->vkCmdCopyBufferToImage(
        command,
        m_stagingBuffer,
        m_images[slot],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copy);

    VkImageMemoryBarrier toSample{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toSample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSample.image = m_images[slot];
    toSample.subresourceRange = toTransfer.subresourceRange;
    m_df->vkCmdPipelineBarrier(
        command,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toSample);
    m_imageLayouts[slot] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = m_panel->linearFilteringForVulkan()
        ? m_linearSampler : m_nearestSampler;
    imageInfo.imageView = m_imageViews[slot];
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_descriptorSets[slot];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    m_df->vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    const QSize swapSize = m_window->swapChainImageSize();
    VkClearValue clearValues[2]{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo renderPass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPass.renderPass = m_window->defaultRenderPass();
    renderPass.framebuffer = m_window->currentFramebuffer();
    renderPass.renderArea.extent = {
        static_cast<std::uint32_t>(swapSize.width()),
        static_cast<std::uint32_t>(swapSize.height())};
    renderPass.clearValueCount = 2;
    renderPass.pClearValues = clearValues;
    m_df->vkCmdBeginRenderPass(command, &renderPass, VK_SUBPASS_CONTENTS_INLINE);
    m_df->vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    m_df->vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout, 0, 1, &m_descriptorSets[slot], 0, nullptr);
    VkViewport viewport{};
    viewport.width = static_cast<float>(swapSize.width());
    viewport.height = static_cast<float>(swapSize.height());
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = renderPass.renderArea.extent;
    m_df->vkCmdSetViewport(command, 0, 1, &viewport);
    m_df->vkCmdSetScissor(command, 0, 1, &scissor);
    m_df->vkCmdDraw(command, 3, 1, 0, 0);
    m_df->vkCmdEndRenderPass(command);
    completeFrame();
}

} // namespace

ScreenPanelVulkan::ScreenPanelVulkan(QWidget* parent)
    : ScreenPanelNative(parent),
      m_phase13Runtime(std::make_unique<MelonPrime::Vulkan::Phase13RuntimeState>())
{
    // MELONPRIME_VULKAN_PHASE13_SCREEN_RUNTIME_V1
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);
}

ScreenPanelVulkan::~ScreenPanelVulkan()
{
    if (m_vulkanWindow)
        m_vulkanWindow->setVisible(false);
    delete m_windowContainer;
    m_windowContainer = nullptr;
    m_vulkanWindow = nullptr;
}

bool ScreenPanelVulkan::initVulkan()
{
    auto& host = static_cast<MelonApplication*>(qApp)->vulkanInstanceHost();
    if (!host.ensureCreated())
    {
        Log(LogLevel::Error, "[MelonPrime] Vulkan presenter unavailable: %s\n",
            host.unavailableReason().c_str());
        return false;
    }

    auto* window = new PresenterWindow(this);
    window->setVulkanInstance(&host.instance());
    const auto devices = window->availablePhysicalDevices();
    if (devices.isEmpty())
    {
        Log(LogLevel::Error,
            "[MelonPrime] Vulkan presenter unavailable: no physical device\n");
        delete window;
        return false;
    }
    int selected = 0;
    for (int index = 0; index < devices.size(); ++index)
    {
        if (devices[index].deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            selected = index;
            break;
        }
    }
    window->setPhysicalDeviceIndex(selected);
    m_vulkanWindow = window;
    m_windowContainer = QWidget::createWindowContainer(window, this);
    m_windowContainer->setFocusPolicy(Qt::NoFocus);
    m_windowContainer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_windowContainer->setGeometry(rect());
    m_windowContainer->show();
    syncVulkanCursor();
    window->requestUpdate();
    return true;
}

bool ScreenPanelVulkan::captureVulkanFrame(const QString& outputPath)
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (!m_vulkanWindow || !m_vulkanWindow->isValid())
        return false;
    const QImage captured = m_vulkanWindow->grab();
    return !captured.isNull() && captured.save(outputPath, "PNG");
#else
    (void)outputPath;
    return false;
#endif
}

void ScreenPanelVulkan::drawScreen()
{
    ScreenPanelNative::drawScreen();
    syncVulkanCursor();
    if (!m_vulkanWindow || !m_phase13Runtime || m_phase13Runtime->DeviceLost())
        return;

    const std::uint64_t serial = m_phase13CompletedSerial.fetch_add(1) + 1;
    melonDS::Vulkan::Phase13PacingInput input;
    input.Config.VSync = emuInstance->getGlobalConfig().GetBool("Screen.VSync");
    input.Config.VSyncInterval = static_cast<std::uint32_t>(std::max(1,
        emuInstance->getGlobalConfig().GetInt("Screen.VSyncInterval")));
    input.Config.FrameLimit = emuInstance->doLimitFPS;
    input.Config.AudioSync = emuInstance->doAudioSync;
    if (emuInstance->curFPS > emuInstance->targetFPS * 1.01)
        input.Config.SpeedMode = melonDS::Vulkan::Phase13SpeedMode::FastForward;
    else if (emuInstance->curFPS < emuInstance->targetFPS * 0.99)
        input.Config.SpeedMode = melonDS::Vulkan::Phase13SpeedMode::SlowMotion;
    input.Capabilities.Fifo = true;
    input.Capabilities.QtFifoOnlyPresenter = true;
    input.CompletedOutputSerial = serial;
    input.LastPresentedSerial = m_phase13QueuedSerial.load();
    input.PresentRequestPending = m_phase13PresentPending.load();

    const auto decision = m_phase13Runtime->Evaluate(input);
    if (!decision.RequestPresent)
        return;
    bool expected = false;
    if (!m_phase13PresentPending.compare_exchange_strong(expected, true))
        return;

    // Build the native Vulkan handoff here, on EmuThread, immediately after
    // the emulated frame is complete. The previous presenter-side capture
    // raced RenderPolygonRAM and VRAM writes when the FPS limiter was off.
    VulkanDirectFrameSnapshot published;
    auto* nds = emuInstance ? emuInstance->getNDS() : nullptr;
    if (!nds)
    {
        m_phase13PresentPending.store(false);
        return;
    }
    melonDS::RendererOutputLease outputLease =
        nds->GPU.AcquireRendererOutputLease();
    const melonDS::RendererOutput& output = outputLease.Output;
    if (output.Kind != melonDS::RendererOutputKind::CpuBgra ||
        !output.Top || !output.Bottom)
    {
        m_phase13PresentPending.store(false);
        return;
    }
    published.FrameSerial = output.FrameSerial;
    published.Generation = output.Generation;
    constexpr std::size_t nativeBytes =
        256u * 192u * sizeof(melonDS::u32);
    const void* nativeSources[2] = {output.Top, output.Bottom};
    for (int index = 0; index < 2; ++index)
    {
        QImage& image = m_directNativeScreens[index];
        if (image.size() != QSize(256, 192) ||
            image.format() != QImage::Format_RGB32)
        {
            image = QImage(256, 192, QImage::Format_RGB32);
        }
        if (image.isNull())
        {
            m_phase13PresentPending.store(false);
            return;
        }
        std::memcpy(image.bits(), nativeSources[index], nativeBytes);
        published.Screens[index] = image;
    }

    const auto* vulkanRenderer =
        dynamic_cast<const melonDS::VulkanRenderer*>(
            &nds->GPU.GetRenderer());
    if (vulkanRenderer)
        published.NativeRaster = vulkanRenderer->AcquireNativeRasterFrame();
    {
        QMutexLocker frameLocker(&m_directFrameMutex);
        m_pendingDirectFrame = std::move(published);
    }

    m_phase13QueuedSerial.store(serial);
    QMetaObject::invokeMethod(this, [this] {
        if (m_vulkanWindow)
            m_vulkanWindow->requestUpdate();
        else
            m_phase13PresentPending.store(false);
    }, Qt::QueuedConnection);
}

void ScreenPanelVulkan::phase13PresentCompleted()
{
    const std::uint64_t serial = m_phase13QueuedSerial.load();
    if (m_phase13Runtime)
        m_phase13Runtime->OnPresented(serial);
    m_phase13PresentPending.store(false);
}

void ScreenPanelVulkan::phase13NotifyDeviceLoss(const char* stage, int result)
{
    if (!m_phase13Runtime)
        return;
    const auto decision = m_phase13Runtime->RecordDeviceLoss(
        MelonPrime::Vulkan::Phase13StageFromName(stage), result, stage ? stage : "unknown");
    m_phase13PresentPending.store(false);
    if (!decision.FirstNotification)
        return;
    const QString stageText = QString::fromUtf8(stage ? stage : "unknown");
    QMetaObject::invokeMethod(this, [this, stageText, result] {
        if (mainWindow)
            mainWindow->handleVulkanDeviceLoss(stageText, result);
    }, Qt::QueuedConnection);
}


// MELONPRIME_VULKAN_CURSOR_CHILD_SYNC_V2
void ScreenPanelVulkan::syncVulkanCursor()
{
    // ScreenPanelNative::drawScreen() is driven by EmuThread. QVulkanWindow and
    // its QWidget container must only be touched from this panel's GUI thread.
    if (QThread::currentThread() != thread())
    {
        bool expected = false;
        if (m_vulkanCursorSyncQueued.compare_exchange_strong(expected, true))
        {
            const bool queued = QMetaObject::invokeMethod(
                this,
                [this]()
                {
                    m_vulkanCursorSyncQueued.store(false);
                    syncVulkanCursor();
                },
                Qt::QueuedConnection);
            if (!queued)
                m_vulkanCursorSyncQueued.store(false);
        }
        return;
    }

    const QCursor desired = cursor();

    // Compare the actual child objects rather than a remembered parent shape.
    // A native child/window recreation can reset its cursor while the parent's
    // cursor shape remains unchanged.
    if (m_windowContainer &&
        m_windowContainer->cursor().shape() != desired.shape())
    {
        m_windowContainer->setCursor(desired);
    }
    if (m_vulkanWindow &&
        m_vulkanWindow->cursor().shape() != desired.shape())
    {
        m_vulkanWindow->setCursor(desired);
    }
}

bool ScreenPanelVulkan::event(QEvent* event)
{
    const bool handled = ScreenPanelNative::event(event);
    if (event && event->type() == QEvent::CursorChange)
        syncVulkanCursor();
    return handled;
}

namespace
{

struct VulkanCompatibilityImageHold
{
    std::shared_ptr<const melonDS::VulkanCompatibilityFrame> Frame;
};

void ReleaseVulkanCompatibilityImage(void* opaque)
{
    delete static_cast<VulkanCompatibilityImageHold*>(opaque);
}

QImage WrapVulkanCompatibilityImage(
    const std::shared_ptr<const melonDS::VulkanCompatibilityFrame>& frame,
    const std::vector<melonDS::u32>& pixels,
    int width,
    int height)
{
    if (!frame || pixels.empty() || width <= 0 || height <= 0 ||
        pixels.size() != static_cast<std::size_t>(width) * height)
    {
        return {};
    }

    auto* hold = new (std::nothrow) VulkanCompatibilityImageHold{frame};
    if (!hold)
        return {};

    return QImage(
        reinterpret_cast<const uchar*>(pixels.data()),
        width,
        height,
        width * static_cast<int>(sizeof(melonDS::u32)),
        QImage::Format_RGB32,
        ReleaseVulkanCompatibilityImage,
        hold);
}

} // namespace

bool ScreenPanelVulkan::copyHighResolutionScreens(
    QImage& top,
    QImage& bottom) const
{
    if (!emuInstance || !emuInstance->getNDS())
        return false;

    const auto* renderer = dynamic_cast<const melonDS::VulkanRenderer*>(
        &emuInstance->getNDS()->GPU.GetRenderer());
    if (!renderer)
        return false;

    const auto frame = renderer->AcquireCompatibilityFrame();
    if (!frame)
        return false;

    top = WrapVulkanCompatibilityImage(
        frame, frame->Top, frame->TopWidth, frame->TopHeight);
    bottom = WrapVulkanCompatibilityImage(
        frame, frame->Bottom, frame->BottomWidth, frame->BottomHeight);
    return !top.isNull() && !bottom.isNull();
}


bool ScreenPanelVulkan::prepareDirectFrameForVulkan(
    VulkanDirectFrameSnapshot& snapshot)
{
    snapshot = VulkanDirectFrameSnapshot{};

    if (!emuInstance || !emuInstance->getNDS())
        return false;

    auto* emuThread = emuInstance->getEmuThread();
    if (!emuThread || !emuThread->emuIsActive())
        return false;

    {
        QMutexLocker frameLocker(&m_directFrameMutex);
        if (m_pendingDirectFrame.Screens[0].isNull() ||
            m_pendingDirectFrame.Screens[1].isNull())
        {
            return false;
        }
        snapshot = std::move(m_pendingDirectFrame);
        m_pendingDirectFrame = VulkanDirectFrameSnapshot{};
    }

    // Keep gameplay OSD messages on the direct path. Renderer changes post an
    // OSD notification for several seconds; falling back here made Vulkan look
    // native-resolution until that notification expired.
    osdUpdate();

    snapshot.LogicalSize =
        size().expandedTo(QSize(1, 1));
    snapshot.NumScreens =
        std::clamp(numScreens, 0, kMaxScreenTransforms);
    for (int index = 0;
         index < snapshot.NumScreens;
         ++index)
    {
        std::copy_n(
            screenMatrix[index],
            6,
            snapshot.ScreenMatrices[index].begin());
        snapshot.ScreenKinds[index] =
            screenKind[index];
    }

#ifdef MELONPRIME_CUSTOM_HUD
    auto* mp = emuThread->GetMelonPrimeCore();
    const bool editMode =
        mp &&
        MelonPrime::CustomHud_IsEditMode(
            mp->HudConfigState());
    if (MelonPrimeHud_CanRenderForCore(mp, editMode))
    {
        auto& instcfg =
            emuInstance->getLocalConfig();
        MelonPrimeHud_RefreshHudEnabledIfNeeded(
            mp->HudConfigState(),
            instcfg,
            m_hudCfgEpoch,
            m_hudEnabled);
        MelonPrimeHud_RefreshOverlayFontIfNeeded(
            mp->HudConfigState(),
            instcfg,
            m_hudFontEpoch,
            overlayFont);
        MelonPrimeHud_RefreshRadarConfigIfNeeded(
            mp->HudConfigState(),
            instcfg,
            m_radarCfgEpoch,
            m_radarEnable,
            m_radarAnchor,
            m_radarDstX,
            m_radarDstY,
            m_radarDstSize,
            m_radarOpacity,
            m_radarSrcRadius,
            m_radarAnchorDsX,
            m_radarAnchorDsY);

        const bool hudVisible =
            MelonPrimeHud_IsHudVisibleOrRestorePatch(
                emuInstance,
                instcfg,
                mp,
                m_hudEnabled,
                editMode);
        if (hudVisible)
        {
            const int overlayWidth =
                std::max(1, width());
            const int overlayHeight =
                std::max(1, height());
            MelonPrimeHud_PrepareTopOverlay(
                Overlay[0],
                overlayWidth,
                overlayHeight,
                m_hudPrevDirty);
            const QRect currentDirty =
                MelonPrimeHud_RenderTopOverlay(
                    emuInstance,
                    instcfg,
                    mp,
                    Overlay[0],
                    overlayFont,
                    m_topStretchX,
                    m_hudScale,
                    m_hudOriginX,
                    m_hudOriginY);
            m_hudPrevDirty = currentDirty;
            if (!currentDirty.isEmpty())
            {
                // Keep the implicitly-shared full overlay alive for this
                // presenter call. renderDirectFrame copies only HudSource into
                // staging, avoiding QImage::copy() allocation and pixel copy.
                const QRect clippedDirty =
                    currentDirty.intersected(Overlay[0].rect());
                if (!clippedDirty.isEmpty())
                {
                    snapshot.HudOverlay = Overlay[0];
                    snapshot.HudSource = clippedDirty;
                    snapshot.HudDestination = clippedDirty;
                }
            }
        }

        if (m_radarEnable &&
            m_hudTopMatrixValid &&
            MelonPrime::CustomHud_ShouldDrawRadarOverlay(
                emuInstance,
                mp->GetCurrentRom(),
                mp->GetPlayerPosition()))
        {
            const float* topMatrix =
                m_hudTopMatrix;
            const float worldAnchorX =
                topMatrix[0] * m_radarAnchorDsX +
                topMatrix[1] * m_radarAnchorDsY +
                topMatrix[4];
            const float worldAnchorY =
                topMatrix[2] * m_radarAnchorDsX +
                topMatrix[3] * m_radarAnchorDsY +
                topMatrix[5];
            const int baseX =
                static_cast<int>(m_hudOriginX);
            const int baseY =
                static_cast<int>(m_hudOriginY);
            const int destinationX =
                baseX +
                static_cast<int>(
                    (worldAnchorX - m_hudOriginX) +
                    m_radarDstX * m_hudScale);
            const int destinationY =
                baseY +
                static_cast<int>(
                    (worldAnchorY - m_hudOriginY) +
                    m_radarDstY * m_hudScale);
            const int destinationSize =
                std::max(
                    1,
                    static_cast<int>(
                        m_radarDstSize * m_hudScale));
            const std::uint8_t hunterId =
                mp->GetHunterID();

            snapshot.RadarVisible = true;
            snapshot.RadarDestination =
                QRectF(
                    destinationX,
                    destinationY,
                    destinationSize,
                    destinationSize);
            snapshot.RadarSourceCenter =
                QPointF(
                    MelonPrime::kBtmOverlaySrcCenterX /
                        256.0,
                    MelonPrime::kBtmOverlaySrcCenterY[
                        hunterId] /
                        192.0);
            snapshot.RadarSourceRadius =
                m_radarSrcRadius / 256.0f;
            snapshot.RadarOpacity =
                std::clamp(
                    m_radarOpacity,
                    0.0f,
                    1.0f);
        }
    }
#endif

    // The direct compositor has one general overlay upload. When an OSD is
    // active, combine it with the optional Custom HUD into an owned image so
    // both remain visible without abandoning high-resolution Vulkan output.
    {
        QMutexLocker osdLocker(&osdMutex);
        const bool hasActiveOsd = osdEnabled && !osdItems.empty();
        if (hasActiveOsd)
        {
            const QSize overlaySize = size().expandedTo(QSize(1, 1));
            if (m_directOsdComposite.size() != overlaySize ||
                m_directOsdComposite.format() !=
                    QImage::Format_ARGB32_Premultiplied)
            {
                m_directOsdComposite = QImage(
                    overlaySize, QImage::Format_ARGB32_Premultiplied);
            }
            if (!m_directOsdComposite.isNull())
            {
                m_directOsdComposite.fill(Qt::transparent);
                QPainter overlayPainter(&m_directOsdComposite);
                if (!snapshot.HudOverlay.isNull() &&
                    !snapshot.HudSource.isEmpty() &&
                    !snapshot.HudDestination.isEmpty())
                {
                    overlayPainter.drawImage(
                        snapshot.HudDestination,
                        snapshot.HudOverlay,
                        snapshot.HudSource);
                }

                constexpr int osdMargin = 6;
                int y = osdMargin;
                QRect dirty;
                for (const auto& item : osdItems)
                {
                    const QRect destination(
                        osdMargin, y, item.bitmap.width(), item.bitmap.height());
                    overlayPainter.drawImage(destination.topLeft(), item.bitmap);
                    dirty = dirty.united(destination);
                    y += item.bitmap.height();
                }
                if (!snapshot.HudDestination.isEmpty())
                    dirty = dirty.united(snapshot.HudDestination);
                dirty = dirty.intersected(m_directOsdComposite.rect());
                if (!dirty.isEmpty())
                {
                    snapshot.HudOverlay = m_directOsdComposite;
                    snapshot.HudSource = dirty;
                    snapshot.HudDestination = dirty;
                }
            }
        }
    }

    return true;
}


const QImage& ScreenPanelVulkan::composeFrameForVulkan()
{
    const QSize wanted = size().expandedTo(QSize(1, 1));
    if (m_compositeFrame.size() != wanted ||
        m_compositeFrame.format() != QImage::Format_ARGB32_Premultiplied)
    {
        m_compositeFrame = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
    }
    QPainter painter(&m_compositeFrame);
    paintFrame(painter, m_compositeFrame.rect());
    return m_compositeFrame;
}

void ScreenPanelVulkan::paintEvent(QPaintEvent* event)
{
    (void)event;
    syncVulkanCursor();
    if (m_vulkanWindow)
        m_vulkanWindow->requestUpdate();
}

void ScreenPanelVulkan::resizeEvent(QResizeEvent* event)
{
    ScreenPanelNative::resizeEvent(event);
    if (m_windowContainer)
        m_windowContainer->setGeometry(rect());
    syncVulkanCursor();
}
