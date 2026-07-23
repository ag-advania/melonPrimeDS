#ifndef VULKANRETROARCHFILTERCHAIN_H
#define VULKANRETROARCHFILTERCHAIN_H

#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

#include "types.h"

#define LIBRA_RUNTIME_VULKAN 1
#include "librashader.h"

namespace MelonDSAndroid
{

class VulkanRetroArchFilterChain
{
public:
    VulkanRetroArchFilterChain() = default;
    ~VulkanRetroArchFilterChain();

    VulkanRetroArchFilterChain(const VulkanRetroArchFilterChain&) = delete;
    VulkanRetroArchFilterChain& operator=(const VulkanRetroArchFilterChain&) = delete;
    VulkanRetroArchFilterChain(VulkanRetroArchFilterChain&& other) noexcept;
    VulkanRetroArchFilterChain& operator=(VulkanRetroArchFilterChain&& other) noexcept;

    void shutdown();
    bool configure(
        const std::string& presetPath,
        melonDS::u32 sourceWidth,
        melonDS::u32 sourceHeight,
        melonDS::u32 outputWidth,
        melonDS::u32 outputHeight,
        const std::vector<std::pair<std::string, float>>& parameterOverrides);
    bool recordFrame(
        VkCommandBuffer commandBuffer,
        VkImage sourceImage,
        VkImage outputImage,
        melonDS::u64 frameCount,
        bool clearHistory,
        melonDS::u32 frametimeDeltaMs);

    const std::string& getPresetPath() const { return currentPresetPath; }
    melonDS::u32 getSourceWidth() const { return currentSourceWidth; }
    melonDS::u32 getSourceHeight() const { return currentSourceHeight; }
    melonDS::u32 getOutputWidth() const { return currentOutputWidth; }
    melonDS::u32 getOutputHeight() const { return currentOutputHeight; }
    const std::vector<std::pair<std::string, float>>& getParameterOverrides() const { return currentParameterOverrides; }

private:
    bool createChain(
        const std::string& presetPath,
        melonDS::u32 sourceWidth,
        melonDS::u32 sourceHeight,
        melonDS::u32 outputWidth,
        melonDS::u32 outputHeight,
        const std::vector<std::pair<std::string, float>>& parameterOverrides);
    static void logError(const char* context, libra_error_t error);

private:
    libra_vk_filter_chain_t chain = nullptr;
    std::string currentPresetPath;
    melonDS::u32 currentSourceWidth = 0;
    melonDS::u32 currentSourceHeight = 0;
    melonDS::u32 currentOutputWidth = 0;
    melonDS::u32 currentOutputHeight = 0;
    std::vector<std::pair<std::string, float>> currentParameterOverrides;
};

}

#endif
