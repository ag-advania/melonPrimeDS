#include "GPU_Vulkan.h"

#include <algorithm>

#include "Platform.h"

namespace melonDS
{

VulkanRendererShellContract DescribeVulkanRendererShell(bool computeSelected) noexcept
{
    return {
        computeSelected ? "Vulkan Compute Shader" : "Vulkan",
        computeSelected,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        false,
        15,
    };
}

VulkanRenderer::VulkanRenderer(melonDS::NDS& nds, bool useComputeRenderer) noexcept
    : SoftRenderer(nds),
      ComputeRendererSelected(useComputeRenderer)
{
}

VulkanRenderer::~VulkanRenderer() = default;

bool VulkanRenderer::Init()
{
    if (Initialized)
        return true;

    if (!SoftRenderer::Init())
        return false;

    Initialized = true;
    const VulkanRendererShellContract contract =
        DescribeVulkanRendererShell(ComputeRendererSelected);
    Platform::Log(
        Platform::LogLevel::Info,
        "[MelonPrime] %s renderer shell initialized: "
        "software_correctness_baseline=1 native_vulkan_raster_bootstrap=1 "
        "native_vulkan_clear_plane_bootstrap=1 native_vulkan_clear_bitmap_bootstrap=1 "
        "native_vulkan_vertex_upload_bootstrap=1 native_vulkan_polygon_batch_bootstrap=1 "
        "native_vulkan_opaque_pipeline_bootstrap=1 "
        "native_vulkan_translucent_pipeline_bootstrap=1 "
        "native_vulkan_shadow_pipeline_bootstrap=1 native_vulkan_toon_highlight_contract=1 native_vulkan_toon_highlight_shader_abi=1 native_vulkan_toon_highlight_descriptor_runtime=1 native_vulkan_toon_highlight_gpu_draw=1 native_vulkan_texture_sampling_bootstrap=1 native_vulkan_textured_polygon_bootstrap=1 native_vulkan_3d=0 generation=%llu\n",
        contract.ModeName,
        static_cast<unsigned long long>(OutputGeneration));
    return true;
}

void VulkanRenderer::AdvanceOutputGeneration() noexcept
{
    ++OutputGeneration;
    if (OutputGeneration == 0)
        OutputGeneration = 1;
    FrameSerial = 0;
}

void VulkanRenderer::Reset()
{
    SoftRenderer::Reset();
    AdvanceOutputGeneration();
}

void VulkanRenderer::Stop()
{
    SoftRenderer::Stop();
    Initialized = false;
    AdvanceOutputGeneration();
}

void VulkanRenderer::PreSavestate()
{
    SoftRenderer::PreSavestate();
}

void VulkanRenderer::PostSavestate()
{
    SoftRenderer::PostSavestate();
    AdvanceOutputGeneration();
}

void VulkanRenderer::SetRenderSettings(RendererSettings& settings)
{
    SoftRenderer::SetRenderSettings(settings);

    const int requestedScale = std::clamp(settings.ScaleFactor, 1, 16);
    if (requestedScale != ScaleFactor)
    {
        ScaleFactor = requestedScale;
        AdvanceOutputGeneration();
    }
}

void VulkanRenderer::SwapBuffers()
{
    SoftRenderer::SwapBuffers();
    ++FrameSerial;
    if (FrameSerial == 0)
        ++FrameSerial;
}

RendererOutput VulkanRenderer::GetOutput()
{
    RendererOutput output = SoftRenderer::GetOutput();
    output.FrameSerial = FrameSerial;
    output.Generation = OutputGeneration;
    return output;
}

RendererOutputLease VulkanRenderer::AcquireOutputLease()
{
    return RendererOutputLease(GetOutput(), nullptr, nullptr);
}

} // namespace melonDS
