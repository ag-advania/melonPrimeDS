#ifndef GPU3D_TEXCACHEVULKAN
#define GPU3D_TEXCACHEVULKAN

#include <memory>
#include <unordered_map>
#include <vector>
#include <volk.h>

#include "GPU3D_Texcache.h"
#include "VulkanR24Sync.h"

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
    void SetRetirementPoint(VkSemaphore timelineSemaphore, u64 timelineValue);
    void DrainRetiredTextures();

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
        TextureHandle NextHandle = 1;
        std::unordered_map<TextureHandle, TextureArray> TextureArrays;
        VkSemaphore RetirementTimeline = VK_NULL_HANDLE;
        u64 RetirementValue = 0;
        VulkanRetireQueue RetiredTextures;

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

private:
    std::shared_ptr<SharedState> State;
};

using TexcacheVulkan = Texcache<TexcacheVulkanLoader, TexcacheVulkanLoader::TextureHandle>;

}

#endif
