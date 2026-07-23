#pragma once
// MELONPRIME-PC-ADAPT: desktop stub for the librashader C API (https://github.com/SnowflakePowered/librashader).
// The real generated header is not vendored in this tree; this declares exactly the subset of types/functions
// that VulkanRetroArchFilterChain.h/.cpp reference, with signatures inferred from their call sites. No-op
// definitions live in LibrashaderStubs.cpp. The RetroArch filter path (and therefore every symbol here) is only
// reachable when VulkanFilterMode::RetroArch is selected and a non-empty preset path is configured; see
// VulkanRetroArchFilterChain::configure()'s empty-preset-path short-circuit and
// VulkanSurfacePresenter.cpp's `config.filtering == VulkanFilterMode::RetroArch` gate.

#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handles -------------------------------------------------------------
typedef struct libra_error_impl* libra_error_t;
typedef struct libra_vk_filter_chain_impl* libra_vk_filter_chain_t;
typedef struct libra_preset_ctx_impl* libra_preset_ctx_t;
typedef struct libra_shader_preset_impl* libra_shader_preset_t;

// Enums ------------------------------------------------------------------------
typedef enum libra_preset_ctx_runtime_t
{
    LIBRA_PRESET_CTX_RUNTIME_VULKAN = 0,
} libra_preset_ctx_runtime_t;

typedef enum libra_preset_ctx_orientation_t
{
    LIBRA_PRESET_CTX_ORIENTATION_HORIZONTAL = 0,
    LIBRA_PRESET_CTX_ORIENTATION_VERTICAL = 1,
} libra_preset_ctx_orientation_t;

#define LIBRASHADER_CURRENT_ABI 0u

// Structs ------------------------------------------------------------------------
typedef struct libra_device_vk_t
{
    VkPhysicalDevice physical_device;
    VkInstance instance;
    VkDevice device;
    VkQueue queue;
    PFN_vkGetInstanceProcAddr entry;
} libra_device_vk_t;

typedef struct libra_preset_opt_t
{
    uint32_t version;
    bool original_aspect_uniforms;
    bool frametime_uniforms;
} libra_preset_opt_t;

typedef struct filter_chain_vk_opt_t
{
    uint32_t version;
    uint32_t frames_in_flight;
    bool force_no_mipmaps;
    bool use_dynamic_rendering;
    bool disable_cache;
} filter_chain_vk_opt_t;

typedef struct libra_image_vk_t
{
    VkImage handle;
    VkFormat format;
    uint32_t width;
    uint32_t height;
} libra_image_vk_t;

typedef struct libra_viewport_t
{
    float x;
    float y;
    float width;
    float height;
} libra_viewport_t;

typedef struct frame_vk_opt_t
{
    uint32_t version;
    bool clear_history;
    int32_t frame_direction;
    uint32_t rotation;
    uint32_t total_subframes;
    uint32_t current_subframe;
    float aspect_ratio;
    float frames_per_second;
    float frametime_delta;
} frame_vk_opt_t;

// Functions ------------------------------------------------------------------------
libra_error_t libra_vk_filter_chain_free(libra_vk_filter_chain_t* chain);

libra_error_t libra_preset_ctx_create(libra_preset_ctx_t* context);
libra_error_t libra_preset_ctx_free(libra_preset_ctx_t* context);
int32_t libra_preset_ctx_set_runtime(libra_preset_ctx_t* context, libra_preset_ctx_runtime_t runtime);
int32_t libra_preset_ctx_set_core_name(libra_preset_ctx_t* context, const char* name);
int32_t libra_preset_ctx_set_core_aspect_orientation(libra_preset_ctx_t* context, libra_preset_ctx_orientation_t orientation);
int32_t libra_preset_ctx_set_view_aspect_orientation(libra_preset_ctx_t* context, libra_preset_ctx_orientation_t orientation);
int32_t libra_preset_ctx_set_allow_rotation(libra_preset_ctx_t* context, bool allow);

libra_error_t libra_preset_create_with_options(
    const char* presetPath,
    libra_preset_ctx_t* context,
    const libra_preset_opt_t* options,
    libra_shader_preset_t* out);
libra_error_t libra_preset_free(libra_shader_preset_t* preset);

libra_error_t libra_vk_filter_chain_create(
    libra_shader_preset_t* preset,
    libra_device_vk_t device,
    const filter_chain_vk_opt_t* options,
    libra_vk_filter_chain_t* out);
libra_error_t libra_vk_filter_chain_set_param(libra_vk_filter_chain_t* chain, const char* name, float value);
libra_error_t libra_vk_filter_chain_frame(
    libra_vk_filter_chain_t* chain,
    VkCommandBuffer commandBuffer,
    size_t frameCountForVsync,
    libra_image_vk_t source,
    libra_image_vk_t output,
    const libra_viewport_t* viewport,
    const float* mvp,
    const frame_vk_opt_t* options);

int32_t libra_error_write(libra_error_t error, char** out);
int32_t libra_error_free_string(char** message);
int32_t libra_error_free(libra_error_t* error);

#ifdef __cplusplus
}
#endif
