#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "MelonPrimeVulkanSurface.h"

namespace MelonPrime
{

class MelonPrimeVulkanOutput;
struct VulkanFrame;
struct VulkanCompositionInputs;

struct VulkanPresentRegion
{
    bool enabled = false;
    bool bottomScreen = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::array<float, 8> corners{};
    bool hasTransformedCorners = false;
};

struct VulkanRadarFrame
{
    bool enabled = false;
    float x = 0.0f;
    float y = 0.0f;
    float size = 0.0f;
    float opacity = 0.0f;
    std::uint32_t sourceCenterY = 0;
    std::uint32_t sourceRadius = 0;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return enabled && size > 0.0f && opacity > 0.0f && sourceRadius > 0;
    }
};

struct VulkanOverlayFrame
{
    const void* pixels = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t rowBytes = 0;
    VulkanRadarFrame radar{};

    [[nodiscard]] bool IsValid() const noexcept
    {
        return pixels != nullptr && width > 0 && height > 0
            && rowBytes >= static_cast<std::size_t>(width) * sizeof(std::uint32_t);
    }
};

class MelonPrimeVulkanSurfacePresenter
{
public:
    MelonPrimeVulkanSurfacePresenter() = default;
    ~MelonPrimeVulkanSurfacePresenter();

    MelonPrimeVulkanSurfacePresenter(const MelonPrimeVulkanSurfacePresenter&) = delete;
    MelonPrimeVulkanSurfacePresenter& operator=(const MelonPrimeVulkanSurfacePresenter&) = delete;

    bool Init(
        const VulkanNativeWindowInfo& nativeWindow,
        std::uint32_t width,
        std::uint32_t height,
        bool vsync);
    void Shutdown();
    void Resize(std::uint32_t width, std::uint32_t height, bool vsync);
    bool Present(
        VulkanFrame* frame,
        MelonPrimeVulkanOutput& output,
        const VulkanCompositionInputs& inputs,
        std::uint32_t sourceWidth,
        std::uint32_t sourceHeight,
        const std::vector<VulkanPresentRegion>& regions,
        const VulkanOverlayFrame* overlay);

    void BeginNvidiaReflexFrame(int mode);
    void MarkNvidiaReflexInputSample();
    void MarkNvidiaReflexRenderSubmitStart();
    void MarkNvidiaReflexRenderSubmitEnd();
    void FinishNvidiaReflexFrame();

    [[nodiscard]] bool IsInitialized() const noexcept { return initialized; }
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError; }

private:
    bool CreateSwapchain();
    void DestroySwapchain();
    bool CreateCommandResources();
    void DestroyCommandResources();
    bool CreatePresentationResources();
    void DestroyPresentationResources();
    bool CreateSwapchainGraphicsResources();
    void DestroySwapchainGraphicsResources();
    bool UpdatePresentationDescriptors(
        VkImageView frameImageView,
        const VulkanCompositionInputs& inputs,
        VkBuffer overlayBuffer,
        VkDeviceSize overlayBufferSize);
    bool UpdatePresentationVertices(
        const std::vector<VulkanPresentRegion>& regions,
        const VulkanRadarFrame* radar,
        bool includeOverlay);
    bool UpdateOverlayBuffer(const VulkanOverlayFrame* overlay);
    void DestroyOverlayBuffer();
    std::uint32_t FindMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags properties) const;
    bool RecoverSwapchain(const char* stage);
    VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes) const;
    void SetError(const std::string& value);
    bool ApplyNvidiaReflexMode();
    void SendNvidiaReflexMarker(VkLatencyMarkerNV marker);
    void DisableNvidiaReflex(const char* operation, VkResult result);

private:
    bool initialized = false;
    bool contextAcquired = false;
    bool swapchainDirty = false;
    bool vsync = true;
    std::uint32_t requestedWidth = 0;
    std::uint32_t requestedHeight = 0;
    std::string lastError;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamilyIndex = 0;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;

    struct SurfaceVertex
    {
        float x;
        float y;
        float u;
        float v;
        float alpha;
    };

    struct PresenterPushConstants
    {
        std::uint32_t drawMode;
        std::uint32_t scale;
        std::uint32_t rendererWidth;
        std::uint32_t rendererHeight;
        std::uint32_t packedStride;
        std::uint32_t screenSwap;
        std::uint32_t filtering;
        std::uint32_t previousTopSourceValid;
        std::uint32_t previousBottomSourceValid;
        std::uint32_t captureSourceValid;
        std::uint32_t captureSourceScreenSwapValid;
        std::uint32_t captureSourceScreenSwap;
        std::uint32_t liveSourceScreenSwap;
        std::uint32_t class4VramStructuredPair;
        std::uint32_t class4NoAboveVramStructuredPair;
        std::uint32_t class4PreservePackedVramValid;
        std::uint32_t class4PreservePackedVramScreenSwap;
        std::uint32_t topStructuredHandoffNoCurrent3d;
        std::uint32_t bottomStructuredHandoffNoCurrent3d;
        std::uint32_t topStructuredHandoffSuppress3d;
        std::uint32_t bottomStructuredHandoffSuppress3d;
        float viewportWidth;
        float viewportHeight;
        std::uint32_t overlayWidth;
        std::uint32_t overlayHeight;
        float radarOpacity;
        std::uint32_t radarSourceCenterY;
        std::uint32_t radarSourceRadius;
        std::uint32_t radarReserved;
    };

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule vertexShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragmentShaderModule = VK_NULL_HANDLE;
    VkSampler nearestSampler = VK_NULL_HANDLE;
    VkSampler linearSampler = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    void* mappedVertexMemory = nullptr;
    std::size_t vertexCapacity = 0;
    std::uint32_t presentVertexCount = 0;
    std::uint32_t presentScreenVertexCount = 0;
    std::uint32_t radarFirstVertex = 0;
    std::uint32_t radarVertexCount = 0;
    std::uint32_t overlayFirstVertex = 0;
    VkBuffer overlayBuffer = VK_NULL_HANDLE;
    VkDeviceMemory overlayMemory = VK_NULL_HANDLE;
    void* mappedOverlayMemory = nullptr;
    VkDeviceSize overlayCapacity = 0;
    VkDeviceSize overlayDataSize = 0;
    std::uint32_t overlayWidth = 0;
    std::uint32_t overlayHeight = 0;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence submitFence = VK_NULL_HANDLE;

    PFN_vkSetLatencySleepModeNV setLatencySleepModeNV = nullptr;
    PFN_vkLatencySleepNV latencySleepNV = nullptr;
    PFN_vkSetLatencyMarkerNV setLatencyMarkerNV = nullptr;
    VkSemaphore reflexSleepSemaphore = VK_NULL_HANDLE;
    std::uint64_t reflexSleepValue = 0;
    std::uint64_t reflexFrameId = 0;
    int reflexMode = 1;
    bool reflexRuntimeAvailable = false;
    bool reflexModeApplied = false;
    bool reflexFrameOpen = false;
    bool reflexSimulationOpen = false;
    bool reflexRenderSubmitOpen = false;
    bool reflexPresentOpen = false;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
