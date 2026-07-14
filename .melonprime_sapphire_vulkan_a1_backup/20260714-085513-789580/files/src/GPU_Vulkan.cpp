#include "GPU_Vulkan.h"
#include "NDS.h"
#include "Platform.h"
namespace melonDS {
VulkanRendererShellContract DescribeVulkanRendererShell(bool computeSelected) noexcept { VulkanRendererShellContract c{}; c.ComputeSelected=computeSelected; c.ModeName=computeSelected?"Vulkan Compute Shader (graphics compatibility)":"Vulkan"; return c; }
VulkanRenderer::VulkanRenderer(NDS& nds,bool compute) noexcept:SoftRenderer(nds),ComputeRequested(compute){}
VulkanRenderer::~VulkanRenderer()=default;
bool VulkanRenderer::Init(){if(Initialized)return true;if(!SoftRenderer::Init())return false;auto vk=VulkanRenderer3D::New(GPU.GPU3D);if(!vk||!vk->Init()){Platform::Log(Platform::LogLevel::Error,"[MelonPrime] VulkanRenderer3D initialization failed; actual=software fallback\n");return true;}Vulkan3D=vk.get();Rend3D=std::move(vk);Initialized=true;SetRenderSettings(LastSettings);Platform::Log(Platform::LogLevel::Info,"[MelonPrime] Vulkan reference renderer active: requested=%s actual=graphics-hardware\n",ComputeRequested?"compute":"graphics");return true;}
void VulkanRenderer::Reset(){SoftRenderer::Reset();}
void VulkanRenderer::Stop(){if(Vulkan3D)Vulkan3D->StopRenderer();Vulkan3D=nullptr;Initialized=false;SoftRenderer::Stop();}
void VulkanRenderer::PreSavestate(){}
void VulkanRenderer::PostSavestate(){}
void VulkanRenderer::SetRenderSettings(RendererSettings& s){LastSettings=s;if(Vulkan3D)Vulkan3D->SetRenderSettings(s.Threaded,s.BetterPolygons,s.ScaleFactor,false,false,0.0f,0.0f,true,false,false,GPU);}
void VulkanRenderer::SwapBuffers(){SoftRenderer::SwapBuffers();}
}
