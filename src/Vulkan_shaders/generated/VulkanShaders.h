#pragma once

#include "VulkanBuildProbe.spv.h"
#include "VulkanPresenterVertex.spv.h"
#include "VulkanPresenterFragment.spv.h"
#include "VulkanClearBitmapVertex.spv.h"
#include "VulkanClearBitmapFragment.spv.h"
#include "VulkanOpaqueVertex.spv.h"
#include "VulkanOpaqueVertexWBuffer.spv.h"
#include "VulkanOpaqueFragment.spv.h"
#include "VulkanOpaqueFragmentWBuffer.spv.h"
#include "VulkanOpaqueFragmentToonHighlight.spv.h"
#include "VulkanOpaqueFragmentToonHighlightWBuffer.spv.h"
#include "VulkanTranslucentVertex.spv.h"
#include "VulkanTranslucentVertexWBuffer.spv.h"
#include "VulkanTranslucentFragment.spv.h"
#include "VulkanTranslucentFragmentWBuffer.spv.h"
#include "VulkanTranslucentFragmentToonHighlight.spv.h"
#include "VulkanTranslucentFragmentToonHighlightWBuffer.spv.h"
#include "VulkanShadowMaskFragment.spv.h"
#include "VulkanShadowMaskFragmentWBuffer.spv.h"
#include "VulkanToonHighlightVertex.spv.h"
#include "VulkanToonHighlightFragment.spv.h"
#include "VulkanTextureSamplingCompute.spv.h"
#include "VulkanTexturedPolygonVertex.spv.h"
#include "VulkanTexturedPolygonVertexWBuffer.spv.h"
#include "VulkanTexturedPolygonFragmentOpaqueModulate.spv.h"
#include "VulkanTexturedPolygonFragmentOpaqueDecal.spv.h"
#include "VulkanTexturedPolygonFragmentTranslucentModulate.spv.h"
#include "VulkanTexturedPolygonFragmentTranslucentDecal.spv.h"
#include "VulkanTexturedPolygonFragmentOpaqueModulateWBuffer.spv.h"
#include "VulkanTexturedPolygonFragmentOpaqueDecalWBuffer.spv.h"
#include "VulkanTexturedPolygonFragmentTranslucentModulateWBuffer.spv.h"
#include "VulkanTexturedPolygonFragmentTranslucentDecalWBuffer.spv.h"
#include "VulkanTextureCacheCompute.spv.h"
#include "VulkanPhase8CaptureCompute.spv.h"
#include "VulkanPhase9TwoDCompute.spv.h"
#include "VulkanPhase9FinalCompute.spv.h"
#include "VulkanPhase10PresenterVertex.spv.h"
#include "VulkanPhase10PresenterFragment.spv.h"
#include "VulkanPhase11Compute.spv.h"
#include "VulkanPhase14NativeRasterVertex.spv.h"
#include "VulkanPhase14NativeRasterFragment.spv.h"
#include "VulkanPhase14StencilClearVertex.spv.h"
#include "VulkanPhase14StencilOnlyFragment.spv.h"
#include "VulkanPhase14ShadowMaskFragment.spv.h"
#include "VulkanPhase14FinalVertex.spv.h"
#include "VulkanPhase14FinalEdgeFragment.spv.h"
#include "VulkanPhase14FinalFogFragment.spv.h"
#include "VulkanPhase14FinalEdgeFogFragment.spv.h"

namespace melonDS::Vulkan::Shaders
{

inline constexpr std::size_t kShaderCount = 48;
inline constexpr char kManifestSha256[] = "7f84e119d60b333f2615aacc53a7d227d53287f74608653f7d11ffa2fbcb049e";
inline constexpr char kCompilerVersion[] = "Glslang Version: 11:16.3.0 | ESSL Version: OpenGL ES GLSL 3.20 glslang Khronos. 16.3.0 | GLSL Version: 4.60 glslang Khronos. 16.3.0 | SPIR-V Version 0x00010600, Revision 1 | GLSL.std.450 Version 100, Revision 1 | Khronos Tool ID 8 | SPIR-V Generator Version 11 | GL_KHR_vulkan_glsl version 100 | ARB_GL_gl_spirv version 100";

} // namespace melonDS::Vulkan::Shaders
