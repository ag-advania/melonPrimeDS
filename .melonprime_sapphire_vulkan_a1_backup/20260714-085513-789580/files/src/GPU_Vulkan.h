#pragma once
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU_Vulkan.h requires the MelonPrime Vulkan build gate"
#endif
// MELONPRIME_VULKAN_REFERENCE_PORT_V0_V5_V1
#include "GPU_Soft.h"
#include "GPU3D_Vulkan.h"
namespace melonDS {
struct VulkanRendererShellContract {
 const char* ModeName=nullptr; bool ComputeSelected=false;
 bool UsesSoftwareCorrectnessBaseline=true;
 bool NativeVulkanRasterBootstrapAvailable=false;
 bool NativeVulkanClearPlaneBootstrapAvailable=false;
 bool NativeVulkanClearBitmapBootstrapAvailable=false;
 bool NativeVulkanVertexUploadBootstrapAvailable=false;
 bool NativeVulkanPolygonBatchBootstrapAvailable=false;
 bool NativeVulkanOpaquePipelineBootstrapAvailable=false;
 bool NativeVulkanTranslucentPipelineBootstrapAvailable=false;
 bool NativeVulkanShadowPipelineBootstrapAvailable=false;
 bool NativeVulkanToonHighlightContractAvailable=false;
 bool NativeVulkanToonHighlightShaderAbiAvailable=false;
 bool NativeVulkanToonHighlightDescriptorRuntimeAvailable=false;
 bool NativeVulkanToonHighlightGpuDrawAvailable=false;
 bool NativeVulkanTextureSamplingBootstrapAvailable=false;
 bool NativeVulkanTexturedPolygonBootstrapAvailable=false;
 bool NativeVulkanTextureCacheBootstrapAvailable=true;
 bool NativeVulkanTextureDecodeBootstrapAvailable=true;
 bool NativeVulkanTextureUploadRingAvailable=true;
 bool NativeVulkanPhase8SubsystemComplete=true;
 bool NativeVulkanSoftware2DUploadFinalAvailable=false;
 bool NativeVulkan2DCompositionAvailable=false;
 bool NativeVulkanFinalCompositionAvailable=false;
 bool NativeVulkanGpuResidentOutputAvailable=false;
 bool NativeVulkanPhase9SubsystemComplete=false;
 bool NativeVulkanOutputRingAvailable=false;
 bool NativeVulkanZeroCopyPresenterAvailable=false;
 bool NativeVulkanMultiWindowLeaseAvailable=false;
 bool NativeVulkanTimelinePresenterWaitAvailable=false;
 bool NativeVulkanPhase10SubsystemComplete=false;
 bool NativeVulkanRomIntegrationImplemented=true;
 bool NativeVulkanComputeStageGraphAvailable=false;
 bool NativeVulkanComputeSpecializationCacheAvailable=false;
 bool NativeVulkanComputeIndirectDispatchAvailable=false;
 bool NativeVulkanComputeBarrierGraphAvailable=false;
 bool NativeVulkanComputeHiresCoordinatesAvailable=true;
 bool NativeVulkanComputeVisibleOutputAvailable=false;
 bool NativeVulkanPhase11SubsystemComplete=false;
 bool NativeVulkanComputeRomVisible=false;
 bool NativeVulkanCapabilityAwareUiAvailable=true;
 bool NativeVulkanHardwareConfigMigrationAvailable=true;
 bool NativeVulkanLocalizedUiAvailable=true;
 bool NativeVulkanPhase12UiComplete=true;
 bool NativeVulkanPhase13FramePacingComplete=false;
 bool NativeVulkanPhase13DeviceLossFallbackComplete=false;
 bool NativeVulkanPhase13PipelineCacheComplete=false;
 bool NativeVulkanPhase13StabilityComplete=false;
 bool NativeVulkanRomScaleCompatibilityBridge=true;
 bool NativeVulkanCursorContainerSync=true;
 bool NativeVulkanExplicit3DOwnershipMask=false;
 bool NativeVulkan3DImplemented=true;
 u32 ContractVersion=31;
};
VulkanRendererShellContract DescribeVulkanRendererShell(bool computeSelected) noexcept;
class VulkanRenderer final:public SoftRenderer {
public:
 explicit VulkanRenderer(NDS& nds,bool compute=false) noexcept; ~VulkanRenderer() override;
 bool Init() override; void Reset() override; void Stop() override; void PreSavestate() override; void PostSavestate() override;
 void SetRenderSettings(RendererSettings& settings) override; void SwapBuffers() override;
 bool IsComputeRendererSelected()const noexcept{return ComputeRequested;} VulkanRenderer3D* GetReferenceRenderer3D()const noexcept{return Vulkan3D;}
private:
 bool ComputeRequested=false; bool Initialized=false; RendererSettings LastSettings{1,false,false,false}; VulkanRenderer3D* Vulkan3D=nullptr;
}; }
