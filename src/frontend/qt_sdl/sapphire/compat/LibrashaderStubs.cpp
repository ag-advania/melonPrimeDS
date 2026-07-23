// MELONPRIME-PC-ADAPT: desktop stub for the librashader C API; see librashader.h for rationale. All entry
// points return an error (or null/false) so callers treat filter-chain construction as failed and fall back
// to unfiltered presentation (see VulkanRetroArchFilterChain::createChain()'s error handling and
// VulkanSurfacePresenter.cpp's "preset failed to compile/load ... presenting unfiltered" fallback). Nothing
// here is reachable unless VulkanFilterMode::RetroArch is selected with a non-empty preset path, which no
// desktop MelonPrime config currently exposes.

#include "librashader.h"

#include <cstring>

// librashader.h only forward-declares `struct libra_error_impl` (kept opaque for real API consumers); this
// TU is the one place that needs a complete type so it can hand out a non-null sentinel instance below.
struct libra_error_impl
{
};

namespace
{
libra_error_t makeStubError()
{
    // A librashader real implementation would return a heap-allocated opaque error handle here; the desktop
    // stub library is never actually linked against real librashader logic, so a fixed non-null sentinel is
    // sufficient to signal failure to every `error != nullptr` check in VulkanRetroArchFilterChain.cpp.
    static libra_error_impl sentinel{};
    return &sentinel;
}
}

libra_error_t libra_vk_filter_chain_free(libra_vk_filter_chain_t* chain)
{
    if (chain != nullptr)
        *chain = nullptr;
    return nullptr;
}

libra_error_t libra_preset_ctx_create(libra_preset_ctx_t* context)
{
    if (context != nullptr)
        *context = nullptr;
    return makeStubError();
}

libra_error_t libra_preset_ctx_free(libra_preset_ctx_t* context)
{
    if (context != nullptr)
        *context = nullptr;
    return nullptr;
}

int32_t libra_preset_ctx_set_runtime(libra_preset_ctx_t* /*context*/, libra_preset_ctx_runtime_t /*runtime*/)
{
    return -1;
}

int32_t libra_preset_ctx_set_core_name(libra_preset_ctx_t* /*context*/, const char* /*name*/)
{
    return -1;
}

int32_t libra_preset_ctx_set_core_aspect_orientation(libra_preset_ctx_t* /*context*/, libra_preset_ctx_orientation_t /*orientation*/)
{
    return -1;
}

int32_t libra_preset_ctx_set_view_aspect_orientation(libra_preset_ctx_t* /*context*/, libra_preset_ctx_orientation_t /*orientation*/)
{
    return -1;
}

int32_t libra_preset_ctx_set_allow_rotation(libra_preset_ctx_t* /*context*/, bool /*allow*/)
{
    return -1;
}

libra_error_t libra_preset_create_with_options(
    const char* /*presetPath*/,
    libra_preset_ctx_t* /*context*/,
    const libra_preset_opt_t* /*options*/,
    libra_shader_preset_t* out)
{
    if (out != nullptr)
        *out = nullptr;
    return makeStubError();
}

libra_error_t libra_preset_free(libra_shader_preset_t* preset)
{
    if (preset != nullptr)
        *preset = nullptr;
    return nullptr;
}

libra_error_t libra_vk_filter_chain_create(
    libra_shader_preset_t* /*preset*/,
    libra_device_vk_t /*device*/,
    const filter_chain_vk_opt_t* /*options*/,
    libra_vk_filter_chain_t* out)
{
    if (out != nullptr)
        *out = nullptr;
    return makeStubError();
}

libra_error_t libra_vk_filter_chain_set_param(libra_vk_filter_chain_t* /*chain*/, const char* /*name*/, float /*value*/)
{
    return makeStubError();
}

libra_error_t libra_vk_filter_chain_frame(
    libra_vk_filter_chain_t* /*chain*/,
    VkCommandBuffer /*commandBuffer*/,
    size_t /*frameCountForVsync*/,
    libra_image_vk_t /*source*/,
    libra_image_vk_t /*output*/,
    const libra_viewport_t* /*viewport*/,
    const float* /*mvp*/,
    const frame_vk_opt_t* /*options*/)
{
    return makeStubError();
}

int32_t libra_error_write(libra_error_t error, char** out)
{
    if (out == nullptr)
        return -1;
    if (error == nullptr)
    {
        *out = nullptr;
        return -1;
    }

    static const char kMessage[] = "librashader is not available in this build";
    char* copy = new char[sizeof(kMessage)];
    std::memcpy(copy, kMessage, sizeof(kMessage));
    *out = copy;
    return 0;
}

int32_t libra_error_free_string(char** message)
{
    if (message == nullptr || *message == nullptr)
        return -1;
    delete[] *message;
    *message = nullptr;
    return 0;
}

int32_t libra_error_free(libra_error_t* error)
{
    if (error != nullptr)
        *error = nullptr;
    return 0;
}
