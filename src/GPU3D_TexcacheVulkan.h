#ifndef GPU3D_TEXCACHEVULKAN
#define GPU3D_TEXCACHEVULKAN

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

#include "GPU3D_Texcache.h"

namespace melonDS
{

class TexcacheVulkanLoader
{
public:
    using TextureHandle = u64;

    TexcacheVulkanLoader();
    ~TexcacheVulkanLoader();

    TextureHandle GenerateTexture(u32 width, u32 height, u32 layers);
    void UploadTexture(TextureHandle handle, u32 width, u32 height, u32 layer, void* data);
    void DeleteTexture(TextureHandle handle);
    bool GetTextureDescriptor(TextureHandle handle, VkDescriptorImageInfo* outImageInfo) const;
    bool IsTextureLayerOpaque(TextureHandle handle, u32 layer) const;

private:
    struct TextureArray
    {
        u32 Width = 0;
        u32 Height = 0;
        u32 Layers = 0;

        VkImage Image = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkImageView ArrayView = VK_NULL_HANDLE;
        VkSampler Sampler = VK_NULL_HANDLE;

        VkBuffer StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
        VkDeviceSize StagingSize = 0;
        std::vector<u8> LayerOpaque;
    };

    struct SharedState
    {
        // One upload submission in flight. Layer uploads no longer block the
        // emulation thread on a fence; a slot is only waited on when it has to
        // be reused, and all slots are drained before any image is destroyed.
        struct UploadSlot
        {
            VkBuffer StagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
            VkDeviceSize StagingSize = 0;
            VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
            VkFence Fence = VK_NULL_HANDLE;
            bool InFlight = false;
        };

        static constexpr size_t UploadSlotCount = 8;

        TextureHandle NextHandle = 1;
        std::unordered_map<TextureHandle, TextureArray> TextureArrays;
        std::array<UploadSlot, UploadSlotCount> UploadSlots{};
        size_t NextUploadSlot = 0;

        bool ContextAcquired = false;
        VkDevice Device = VK_NULL_HANDLE;
        VkQueue Queue = VK_NULL_HANDLE;
        u32 QueueFamilyIndex = 0;
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        VkFence UploadFence = VK_NULL_HANDLE;
    };

    bool EnsureVulkanState();
    void CleanupVulkanState();
    void DestroyTextureArray(TextureArray& textureArray);
    void WaitForPendingUploads();

private:
    std::shared_ptr<SharedState> State;
};

using TexcacheVulkan = Texcache<TexcacheVulkanLoader, TexcacheVulkanLoader::TextureHandle>;

}

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN

#endif
