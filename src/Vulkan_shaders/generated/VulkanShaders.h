#pragma once

#include "VulkanBuildProbe.spv.h"
#include "VulkanPresenterVertex.spv.h"
#include "VulkanPresenterFragment.spv.h"

namespace melonDS::Vulkan::Shaders
{

inline constexpr std::size_t kShaderCount = 3;
inline constexpr char kManifestSha256[] = "b15b0faa45ce22d1415baa3bff865667303a6a959ab734563a72cfbb16bb7ae4";
inline constexpr char kCompilerVersion[] = "Glslang Version: 11:16.3.0 | ESSL Version: OpenGL ES GLSL 3.20 glslang Khronos. 16.3.0 | GLSL Version: 4.60 glslang Khronos. 16.3.0 | SPIR-V Version 0x00010600, Revision 1 | GLSL.std.450 Version 100, Revision 1 | Khronos Tool ID 8 | SPIR-V Generator Version 11 | GL_KHR_vulkan_glsl version 100 | ARB_GL_gl_spirv version 100";

} // namespace melonDS::Vulkan::Shaders
