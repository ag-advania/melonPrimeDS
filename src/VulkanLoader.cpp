/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "VulkanLoader.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    #include <glob.h>
#else
    #include <dlfcn.h>
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    #include <dlfcn.h>
#endif

namespace melonDS::Vk
{

namespace
{

// Candidate runtime names, most specific first.
//
// The versioned soname is tried before the unversioned one on purpose: on
// Linux, libvulkan.so is part of the *development* package and is frequently
// absent on a user's machine, while libvulkan.so.1 ships with the runtime. On
// macOS packaged apps try the bundled MoltenVK runtime first so Vulkan does
// not depend on Homebrew, the Vulkan SDK, or the Finder launch environment.
std::vector<std::string> GetLibraryCandidates()
{
    std::vector<std::string> names;
#if defined(_WIN32)
    names.emplace_back("vulkan-1.dll");
#elif defined(__APPLE__)
    names = {
        "@executable_path/../Frameworks/libMoltenVK.dylib",
        "libvulkan.1.dylib",
        "libvulkan.dylib",
        "libMoltenVK.dylib",
        "@rpath/libvulkan.1.dylib",
    };
#else
    names = { "libvulkan.so.1", "libvulkan.so" };
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    // BSD package managers may install a fully versioned shared object such
    // as OpenBSD's libvulkan.so.1.3 without providing either libvulkan.so.1
    // or libvulkan.so. The production loader must test those exact installed
    // candidates rather than relying on a file-exists probe to imply that a
    // bare soname will resolve through the system's runtime search path.
    constexpr const char* PackageLibRoots[] = {
        "/usr/local/lib",
        "/usr/pkg/lib",
        "/usr/X11R6/lib",
        "/usr/X11R7/lib",
    };
    for (const char* root : PackageLibRoots)
    {
        const std::string pattern = std::string(root) + "/libvulkan.so.*";
        glob_t matches{};
        if (::glob(pattern.c_str(), GLOB_NOSORT, nullptr, &matches) == 0)
        {
            for (size_t i = 0; i < matches.gl_pathc; i++)
            {
                const std::string candidate = matches.gl_pathv[i];
                if (std::find(names.begin(), names.end(), candidate) == names.end())
                    names.push_back(candidate);
            }
        }
        ::globfree(&matches);
    }
#endif
#endif
    return names;
}

void* OpenLibrary(const char* name) noexcept
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(::LoadLibraryA(name));
#else
    (void)::dlerror();
    // RTLD_LOCAL keeps the driver's symbols out of the global namespace so a
    // driver that exports e.g. its own "malloc" cannot interpose on ours.
    return ::dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

std::string GetLibraryOpenError() noexcept
{
#if defined(_WIN32)
    const DWORD error = ::GetLastError();
    return error ? (std::string("Win32 error ") + std::to_string(error)) : "unknown LoadLibrary failure";
#else
    const char* const error = ::dlerror();
    return error ? std::string(error) : "unknown dlopen failure";
#endif
}

void CloseLibrary(void* handle) noexcept
{
    if (!handle)
        return;
#if defined(_WIN32)
    ::FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

PFN_vkVoidFunction ResolveSymbol(void* handle, const char* name) noexcept
{
#if defined(_WIN32)
    // The cast through a plain function pointer is required because
    // GetProcAddress returns FARPROC; going via void* is ill-formed on MSVC's
    // headers and warns on MinGW.
    return reinterpret_cast<PFN_vkVoidFunction>(
        ::GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return reinterpret_cast<PFN_vkVoidFunction>(::dlsym(handle, name));
#endif
}

bool ReportMissing(const char* level, const char* name, std::string& outFailureReason)
{
    outFailureReason = std::string("Vulkan ") + level + " entry point " + name
        + " is missing from the runtime";
    Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", outFailureReason.c_str());
    return false;
}

} // namespace


bool ExtensionEnabled(const std::vector<const char*>& enabled, const char* name) noexcept
{
    if (!name)
        return false;

    for (const char* entry : enabled)
    {
        if (entry && std::strcmp(entry, name) == 0)
            return true;
    }
    return false;
}


Library::~Library()
{
    Close();
}

bool Library::Open()
{
    if (Handle)
        return true;

    FailureReason.clear();

    const std::vector<std::string> candidates = GetLibraryCandidates();
    std::vector<std::string> openErrors;

    for (const std::string& candidate : candidates)
    {
        Handle = OpenLibrary(candidate.c_str());
        if (Handle)
        {
            LibraryName = candidate;
        }
        else
        {
            openErrors.emplace_back(candidate + ": " + GetLibraryOpenError());
        }
        if (Handle)
            break;
    }

    if (!Handle)
    {
        FailureReason = "No Vulkan runtime could be loaded (tried ";
        for (size_t i = 0; i < candidates.size(); i++)
        {
            if (i)
                FailureReason += ", ";
            FailureReason += candidates[i];
        }
        FailureReason += ")";
        Platform::Log(Platform::LogLevel::Info, "[Vulkan] %s\n", FailureReason.c_str());
        for (const std::string& openError : openErrors)
        {
            Platform::Log(Platform::LogLevel::Info,
                "[Vulkan] loader candidate failed %s\n",
                openError.c_str());
        }
        return false;
    }

    // vkGetInstanceProcAddr is the only symbol the spec guarantees is exported
    // from the loader library itself. Everything else -- including
    // vkCreateInstance -- is resolved through it with a null instance.
    GlobalFns.GetInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(ResolveSymbol(Handle, "vkGetInstanceProcAddr"));

    if (!GlobalFns.GetInstanceProcAddr)
    {
        FailureReason = LibraryName + " does not export vkGetInstanceProcAddr; it is not a Vulkan loader";
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
        Close();
        return false;
    }

    const auto getGlobal = [this](const char* name) -> PFN_vkVoidFunction {
        return GlobalFns.GetInstanceProcAddr(VK_NULL_HANDLE, name);
    };

    GlobalFns.CreateInstance =
        reinterpret_cast<PFN_vkCreateInstance>(getGlobal("vkCreateInstance"));
    GlobalFns.EnumerateInstanceExtensionProperties =
        reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(getGlobal("vkEnumerateInstanceExtensionProperties"));
    GlobalFns.EnumerateInstanceLayerProperties =
        reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(getGlobal("vkEnumerateInstanceLayerProperties"));

    // Absence is meaningful, not fatal: per the spec a loader that does not
    // expose vkEnumerateInstanceVersion supports Vulkan 1.0 only.
    GlobalFns.EnumerateInstanceVersion =
        reinterpret_cast<PFN_vkEnumerateInstanceVersion>(getGlobal("vkEnumerateInstanceVersion"));

    if (!GlobalFns.CreateInstance
        || !GlobalFns.EnumerateInstanceExtensionProperties
        || !GlobalFns.EnumerateInstanceLayerProperties)
    {
        FailureReason = LibraryName + " is missing a mandatory global entry point "
            "(vkCreateInstance / vkEnumerateInstanceExtensionProperties / vkEnumerateInstanceLayerProperties)";
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
        Close();
        return false;
    }

    InstanceVersion = VK_API_VERSION_1_0;
    if (GlobalFns.EnumerateInstanceVersion)
    {
        u32 version = 0;
        const VkResult res = GlobalFns.EnumerateInstanceVersion(&version);
        if (res == VK_SUCCESS)
        {
            InstanceVersion = version;
        }
        else
        {
            // Not fatal on its own -- the probe still gets to report the 1.0
            // baseline failure with a precise reason -- but worth logging
            // because a failing enumeration usually means a broken layer.
            Platform::Log(Platform::LogLevel::Warn,
                "[Vulkan] vkEnumerateInstanceVersion failed: %s; assuming 1.0\n",
                FormatResult(res).c_str());
        }
    }

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] loaded %s, instance API version %s\n",
        LibraryName.c_str(), FormatApiVersion(InstanceVersion).c_str());

    return true;
}

void Library::Close()
{
    if (!Handle)
        return;

    CloseLibrary(Handle);
    Handle = nullptr;
    GlobalFns = GlobalDispatch{};
    InstanceVersion = 0;
    LibraryName.clear();
}


bool LoadInstanceDispatch(
    const GlobalDispatch& global,
    VkInstance instance,
    const std::vector<const char*>& enabledExtensions,
    InstanceDispatch& out,
    std::string& outFailureReason)
{
    out = InstanceDispatch{};
    outFailureReason.clear();

    if (!global.GetInstanceProcAddr || instance == VK_NULL_HANDLE)
    {
        outFailureReason = "LoadInstanceDispatch called without a valid instance";
        return false;
    }

    const auto get = [&](const char* name) -> PFN_vkVoidFunction {
        return global.GetInstanceProcAddr(instance, name);
    };

    // A core entry point that fails to resolve means the loader/ICD is broken;
    // there is no useful degraded mode, so bail with the exact name.
    #define MELONPRIME_VK_LOAD_INSTANCE_CORE(member, name) \
        out.member = reinterpret_cast<PFN_##name>(get(#name)); \
        if (!out.member) return ReportMissing("instance", #name, outFailureReason)

    MELONPRIME_VK_LOAD_INSTANCE_CORE(DestroyInstance,                       vkDestroyInstance);
    MELONPRIME_VK_LOAD_INSTANCE_CORE(EnumeratePhysicalDevices,              vkEnumeratePhysicalDevices);
    MELONPRIME_VK_LOAD_INSTANCE_CORE(GetPhysicalDeviceProperties,           vkGetPhysicalDeviceProperties);
    MELONPRIME_VK_LOAD_INSTANCE_CORE(GetPhysicalDeviceFeatures,             vkGetPhysicalDeviceFeatures);
    MELONPRIME_VK_LOAD_INSTANCE_CORE(GetPhysicalDeviceMemoryProperties,     vkGetPhysicalDeviceMemoryProperties);
    MELONPRIME_VK_LOAD_INSTANCE_CORE(GetPhysicalDeviceQueueFamilyProperties, vkGetPhysicalDeviceQueueFamilyProperties);
    MELONPRIME_VK_LOAD_INSTANCE_CORE(GetPhysicalDeviceFormatProperties,     vkGetPhysicalDeviceFormatProperties);
    MELONPRIME_VK_LOAD_INSTANCE_CORE(GetPhysicalDeviceImageFormatProperties, vkGetPhysicalDeviceImageFormatProperties);
    MELONPRIME_VK_LOAD_INSTANCE_CORE(EnumerateDeviceExtensionProperties,    vkEnumerateDeviceExtensionProperties);
    MELONPRIME_VK_LOAD_INSTANCE_CORE(CreateDevice,                          vkCreateDevice);
    MELONPRIME_VK_LOAD_INSTANCE_CORE(GetDeviceProcAddr,                     vkGetDeviceProcAddr);

    // Core in Vulkan 1.1. The instance is only ever created with
    // apiVersion >= 1.1 (see VulkanContext), so the un-suffixed names are the
    // correct ones to ask for; the KHR aliases are never used.
    MELONPRIME_VK_LOAD_INSTANCE_CORE(GetPhysicalDeviceProperties2,          vkGetPhysicalDeviceProperties2);
    MELONPRIME_VK_LOAD_INSTANCE_CORE(GetPhysicalDeviceFeatures2,            vkGetPhysicalDeviceFeatures2);
    MELONPRIME_VK_LOAD_INSTANCE_CORE(GetPhysicalDeviceMemoryProperties2,    vkGetPhysicalDeviceMemoryProperties2);

    #undef MELONPRIME_VK_LOAD_INSTANCE_CORE

    // Extension entry points: resolved only when the extension was actually
    // enabled at vkCreateInstance time. If it was enabled and the pointer is
    // still null the runtime contradicted itself, which is a hard failure.
    #define MELONPRIME_VK_LOAD_INSTANCE_EXT(member, name) \
        out.member = reinterpret_cast<PFN_##name>(get(#name)); \
        if (!out.member) return ReportMissing("instance", #name, outFailureReason)

    if (ExtensionEnabled(enabledExtensions, VK_KHR_SURFACE_EXTENSION_NAME))
    {
        MELONPRIME_VK_LOAD_INSTANCE_EXT(DestroySurfaceKHR,                      vkDestroySurfaceKHR);
        MELONPRIME_VK_LOAD_INSTANCE_EXT(GetPhysicalDeviceSurfaceSupportKHR,     vkGetPhysicalDeviceSurfaceSupportKHR);
        MELONPRIME_VK_LOAD_INSTANCE_EXT(GetPhysicalDeviceSurfaceCapabilitiesKHR, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
        MELONPRIME_VK_LOAD_INSTANCE_EXT(GetPhysicalDeviceSurfaceFormatsKHR,     vkGetPhysicalDeviceSurfaceFormatsKHR);
        MELONPRIME_VK_LOAD_INSTANCE_EXT(GetPhysicalDeviceSurfacePresentModesKHR, vkGetPhysicalDeviceSurfacePresentModesKHR);
    }

    if (ExtensionEnabled(enabledExtensions, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME))
    {
        // Optional WSI pacing must remain fail-soft even if a broken loader
        // advertises the extension but omits its command. The presenter sees
        // the null dispatch and falls back to the legacy surface query.
        out.GetPhysicalDeviceSurfaceCapabilities2KHR =
            reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>(
                get("vkGetPhysicalDeviceSurfaceCapabilities2KHR"));
    }

    // Braces are mandatory around every one of these: the macro expands to two
    // statements, so an unbraced `if` would silently run the null check
    // unconditionally.
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    if (ExtensionEnabled(enabledExtensions, VK_KHR_WIN32_SURFACE_EXTENSION_NAME))
    {
        MELONPRIME_VK_LOAD_INSTANCE_EXT(CreateWin32SurfaceKHR, vkCreateWin32SurfaceKHR);
    }
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (ExtensionEnabled(enabledExtensions, VK_KHR_XLIB_SURFACE_EXTENSION_NAME))
    {
        MELONPRIME_VK_LOAD_INSTANCE_EXT(CreateXlibSurfaceKHR, vkCreateXlibSurfaceKHR);
    }
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
    if (ExtensionEnabled(enabledExtensions, VK_KHR_XCB_SURFACE_EXTENSION_NAME))
    {
        MELONPRIME_VK_LOAD_INSTANCE_EXT(CreateXcbSurfaceKHR, vkCreateXcbSurfaceKHR);
    }
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (ExtensionEnabled(enabledExtensions, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME))
    {
        MELONPRIME_VK_LOAD_INSTANCE_EXT(CreateWaylandSurfaceKHR, vkCreateWaylandSurfaceKHR);
    }
#endif
#if defined(VK_USE_PLATFORM_METAL_EXT)
    if (ExtensionEnabled(enabledExtensions, VK_EXT_METAL_SURFACE_EXTENSION_NAME))
    {
        MELONPRIME_VK_LOAD_INSTANCE_EXT(CreateMetalSurfaceEXT, vkCreateMetalSurfaceEXT);
    }
#endif

    if (ExtensionEnabled(enabledExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        MELONPRIME_VK_LOAD_INSTANCE_EXT(CreateDebugUtilsMessengerEXT,  vkCreateDebugUtilsMessengerEXT);
        MELONPRIME_VK_LOAD_INSTANCE_EXT(DestroyDebugUtilsMessengerEXT, vkDestroyDebugUtilsMessengerEXT);
    }

    #undef MELONPRIME_VK_LOAD_INSTANCE_EXT

    return true;
}


bool LoadDeviceDispatch(
    const InstanceDispatch& instanceFns,
    VkDevice device,
    const std::vector<const char*>& enabledExtensions,
    DeviceDispatch& out,
    std::string& outFailureReason)
{
    out = DeviceDispatch{};
    outFailureReason.clear();

    if (!instanceFns.GetDeviceProcAddr || device == VK_NULL_HANDLE)
    {
        outFailureReason = "LoadDeviceDispatch called without a valid device";
        return false;
    }

    const auto get = [&](const char* name) -> PFN_vkVoidFunction {
        return instanceFns.GetDeviceProcAddr(device, name);
    };

    #define MELONPRIME_VK_LOAD_DEVICE(member, name) \
        out.member = reinterpret_cast<PFN_##name>(get(#name)); \
        if (!out.member) return ReportMissing("device", #name, outFailureReason)

    MELONPRIME_VK_LOAD_DEVICE(DestroyDevice,                    vkDestroyDevice);
    MELONPRIME_VK_LOAD_DEVICE(GetDeviceQueue,                   vkGetDeviceQueue);
    MELONPRIME_VK_LOAD_DEVICE(DeviceWaitIdle,                   vkDeviceWaitIdle);
    MELONPRIME_VK_LOAD_DEVICE(QueueSubmit,                      vkQueueSubmit);
    MELONPRIME_VK_LOAD_DEVICE(QueueWaitIdle,                    vkQueueWaitIdle);

    MELONPRIME_VK_LOAD_DEVICE(AllocateMemory,                   vkAllocateMemory);
    MELONPRIME_VK_LOAD_DEVICE(FreeMemory,                       vkFreeMemory);
    MELONPRIME_VK_LOAD_DEVICE(MapMemory,                        vkMapMemory);
    MELONPRIME_VK_LOAD_DEVICE(UnmapMemory,                      vkUnmapMemory);
    MELONPRIME_VK_LOAD_DEVICE(FlushMappedMemoryRanges,          vkFlushMappedMemoryRanges);
    MELONPRIME_VK_LOAD_DEVICE(InvalidateMappedMemoryRanges,     vkInvalidateMappedMemoryRanges);

    MELONPRIME_VK_LOAD_DEVICE(CreateBuffer,                     vkCreateBuffer);
    MELONPRIME_VK_LOAD_DEVICE(DestroyBuffer,                    vkDestroyBuffer);
    MELONPRIME_VK_LOAD_DEVICE(GetBufferMemoryRequirements,      vkGetBufferMemoryRequirements);
    MELONPRIME_VK_LOAD_DEVICE(BindBufferMemory,                 vkBindBufferMemory);
    MELONPRIME_VK_LOAD_DEVICE(CreateBufferView,                 vkCreateBufferView);
    MELONPRIME_VK_LOAD_DEVICE(DestroyBufferView,                vkDestroyBufferView);

    MELONPRIME_VK_LOAD_DEVICE(CreateImage,                      vkCreateImage);
    MELONPRIME_VK_LOAD_DEVICE(DestroyImage,                     vkDestroyImage);
    MELONPRIME_VK_LOAD_DEVICE(GetImageMemoryRequirements,       vkGetImageMemoryRequirements);
    MELONPRIME_VK_LOAD_DEVICE(BindImageMemory,                  vkBindImageMemory);
    MELONPRIME_VK_LOAD_DEVICE(GetImageSubresourceLayout,        vkGetImageSubresourceLayout);
    MELONPRIME_VK_LOAD_DEVICE(CreateImageView,                  vkCreateImageView);
    MELONPRIME_VK_LOAD_DEVICE(DestroyImageView,                 vkDestroyImageView);
    MELONPRIME_VK_LOAD_DEVICE(CreateSampler,                    vkCreateSampler);
    MELONPRIME_VK_LOAD_DEVICE(DestroySampler,                   vkDestroySampler);

    MELONPRIME_VK_LOAD_DEVICE(CreateDescriptorSetLayout,        vkCreateDescriptorSetLayout);
    MELONPRIME_VK_LOAD_DEVICE(DestroyDescriptorSetLayout,       vkDestroyDescriptorSetLayout);
    MELONPRIME_VK_LOAD_DEVICE(CreateDescriptorPool,             vkCreateDescriptorPool);
    MELONPRIME_VK_LOAD_DEVICE(DestroyDescriptorPool,            vkDestroyDescriptorPool);
    MELONPRIME_VK_LOAD_DEVICE(ResetDescriptorPool,              vkResetDescriptorPool);
    MELONPRIME_VK_LOAD_DEVICE(AllocateDescriptorSets,           vkAllocateDescriptorSets);
    MELONPRIME_VK_LOAD_DEVICE(FreeDescriptorSets,               vkFreeDescriptorSets);
    MELONPRIME_VK_LOAD_DEVICE(UpdateDescriptorSets,             vkUpdateDescriptorSets);

    MELONPRIME_VK_LOAD_DEVICE(CreatePipelineLayout,             vkCreatePipelineLayout);
    MELONPRIME_VK_LOAD_DEVICE(DestroyPipelineLayout,            vkDestroyPipelineLayout);
    MELONPRIME_VK_LOAD_DEVICE(CreateShaderModule,               vkCreateShaderModule);
    MELONPRIME_VK_LOAD_DEVICE(DestroyShaderModule,              vkDestroyShaderModule);
    MELONPRIME_VK_LOAD_DEVICE(CreatePipelineCache,              vkCreatePipelineCache);
    MELONPRIME_VK_LOAD_DEVICE(DestroyPipelineCache,             vkDestroyPipelineCache);
    MELONPRIME_VK_LOAD_DEVICE(GetPipelineCacheData,             vkGetPipelineCacheData);
    MELONPRIME_VK_LOAD_DEVICE(CreateComputePipelines,           vkCreateComputePipelines);
    MELONPRIME_VK_LOAD_DEVICE(CreateGraphicsPipelines,          vkCreateGraphicsPipelines);
    MELONPRIME_VK_LOAD_DEVICE(DestroyPipeline,                  vkDestroyPipeline);

    MELONPRIME_VK_LOAD_DEVICE(CreateRenderPass,                 vkCreateRenderPass);
    MELONPRIME_VK_LOAD_DEVICE(DestroyRenderPass,                vkDestroyRenderPass);
    MELONPRIME_VK_LOAD_DEVICE(CreateFramebuffer,                vkCreateFramebuffer);
    MELONPRIME_VK_LOAD_DEVICE(DestroyFramebuffer,               vkDestroyFramebuffer);

    MELONPRIME_VK_LOAD_DEVICE(CreateCommandPool,                vkCreateCommandPool);
    MELONPRIME_VK_LOAD_DEVICE(DestroyCommandPool,               vkDestroyCommandPool);
    MELONPRIME_VK_LOAD_DEVICE(ResetCommandPool,                 vkResetCommandPool);
    MELONPRIME_VK_LOAD_DEVICE(AllocateCommandBuffers,           vkAllocateCommandBuffers);
    MELONPRIME_VK_LOAD_DEVICE(FreeCommandBuffers,               vkFreeCommandBuffers);
    MELONPRIME_VK_LOAD_DEVICE(BeginCommandBuffer,               vkBeginCommandBuffer);
    MELONPRIME_VK_LOAD_DEVICE(EndCommandBuffer,                 vkEndCommandBuffer);
    MELONPRIME_VK_LOAD_DEVICE(ResetCommandBuffer,               vkResetCommandBuffer);

    MELONPRIME_VK_LOAD_DEVICE(CreateFence,                      vkCreateFence);
    MELONPRIME_VK_LOAD_DEVICE(DestroyFence,                     vkDestroyFence);
    MELONPRIME_VK_LOAD_DEVICE(ResetFences,                      vkResetFences);
    MELONPRIME_VK_LOAD_DEVICE(GetFenceStatus,                   vkGetFenceStatus);
    MELONPRIME_VK_LOAD_DEVICE(WaitForFences,                    vkWaitForFences);
    MELONPRIME_VK_LOAD_DEVICE(CreateSemaphore,                  vkCreateSemaphore);
    MELONPRIME_VK_LOAD_DEVICE(DestroySemaphore,                 vkDestroySemaphore);

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    MELONPRIME_VK_LOAD_DEVICE(CreateQueryPool,                  vkCreateQueryPool);
    MELONPRIME_VK_LOAD_DEVICE(DestroyQueryPool,                 vkDestroyQueryPool);
    MELONPRIME_VK_LOAD_DEVICE(GetQueryPoolResults,              vkGetQueryPoolResults);
#endif

    MELONPRIME_VK_LOAD_DEVICE(CmdBindPipeline,                  vkCmdBindPipeline);
    MELONPRIME_VK_LOAD_DEVICE(CmdBindDescriptorSets,            vkCmdBindDescriptorSets);
    MELONPRIME_VK_LOAD_DEVICE(CmdPushConstants,                 vkCmdPushConstants);
    MELONPRIME_VK_LOAD_DEVICE(CmdDispatch,                      vkCmdDispatch);
    MELONPRIME_VK_LOAD_DEVICE(CmdDispatchIndirect,              vkCmdDispatchIndirect);
    MELONPRIME_VK_LOAD_DEVICE(CmdPipelineBarrier,               vkCmdPipelineBarrier);
    MELONPRIME_VK_LOAD_DEVICE(CmdCopyBuffer,                    vkCmdCopyBuffer);
    MELONPRIME_VK_LOAD_DEVICE(CmdCopyBufferToImage,             vkCmdCopyBufferToImage);
    MELONPRIME_VK_LOAD_DEVICE(CmdCopyImageToBuffer,             vkCmdCopyImageToBuffer);
    MELONPRIME_VK_LOAD_DEVICE(CmdCopyImage,                     vkCmdCopyImage);
    MELONPRIME_VK_LOAD_DEVICE(CmdBlitImage,                     vkCmdBlitImage);
    MELONPRIME_VK_LOAD_DEVICE(CmdFillBuffer,                    vkCmdFillBuffer);
    MELONPRIME_VK_LOAD_DEVICE(CmdUpdateBuffer,                  vkCmdUpdateBuffer);
    MELONPRIME_VK_LOAD_DEVICE(CmdClearColorImage,               vkCmdClearColorImage);
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    MELONPRIME_VK_LOAD_DEVICE(CmdResetQueryPool,                vkCmdResetQueryPool);
    MELONPRIME_VK_LOAD_DEVICE(CmdWriteTimestamp,                vkCmdWriteTimestamp);
#endif
    MELONPRIME_VK_LOAD_DEVICE(CmdBeginRenderPass,               vkCmdBeginRenderPass);
    MELONPRIME_VK_LOAD_DEVICE(CmdEndRenderPass,                 vkCmdEndRenderPass);
    MELONPRIME_VK_LOAD_DEVICE(CmdSetViewport,                   vkCmdSetViewport);
    MELONPRIME_VK_LOAD_DEVICE(CmdSetScissor,                    vkCmdSetScissor);
    MELONPRIME_VK_LOAD_DEVICE(CmdBindVertexBuffers,             vkCmdBindVertexBuffers);
    MELONPRIME_VK_LOAD_DEVICE(CmdBindIndexBuffer,               vkCmdBindIndexBuffer);
    MELONPRIME_VK_LOAD_DEVICE(CmdDraw,                          vkCmdDraw);
    MELONPRIME_VK_LOAD_DEVICE(CmdDrawIndexed,                   vkCmdDrawIndexed);

    if (ExtensionEnabled(enabledExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
    {
        MELONPRIME_VK_LOAD_DEVICE(CreateSwapchainKHR,           vkCreateSwapchainKHR);
        MELONPRIME_VK_LOAD_DEVICE(DestroySwapchainKHR,          vkDestroySwapchainKHR);
        MELONPRIME_VK_LOAD_DEVICE(GetSwapchainImagesKHR,        vkGetSwapchainImagesKHR);
        MELONPRIME_VK_LOAD_DEVICE(AcquireNextImageKHR,          vkAcquireNextImageKHR);
        MELONPRIME_VK_LOAD_DEVICE(QueuePresentKHR,              vkQueuePresentKHR);
    }

    if (ExtensionEnabled(enabledExtensions, VK_KHR_PRESENT_WAIT_EXTENSION_NAME))
    {
        out.WaitForPresentKHR = reinterpret_cast<PFN_vkWaitForPresentKHR>(
            get("vkWaitForPresentKHR"));
    }

    if (ExtensionEnabled(enabledExtensions, VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME))
    {
        out.WaitForPresent2KHR = reinterpret_cast<PFN_vkWaitForPresent2KHR>(
            get("vkWaitForPresent2KHR"));
    }

    if (ExtensionEnabled(enabledExtensions, VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME))
    {
        out.GetCalibratedTimestampsKHR =
            reinterpret_cast<PFN_vkGetCalibratedTimestampsKHR>(
                get("vkGetCalibratedTimestampsKHR"));
    }

    if (ExtensionEnabled(enabledExtensions, VK_EXT_PRESENT_TIMING_EXTENSION_NAME))
    {
        // These are deliberately best-effort. An incomplete optional dispatch
        // disables only the affected telemetry/pacing path in
        // VulkanPresentPacer::Initialize, never the renderer.
        out.SetSwapchainPresentTimingQueueSizeEXT =
            reinterpret_cast<PFN_vkSetSwapchainPresentTimingQueueSizeEXT>(
                get("vkSetSwapchainPresentTimingQueueSizeEXT"));
        out.GetSwapchainTimingPropertiesEXT =
            reinterpret_cast<PFN_vkGetSwapchainTimingPropertiesEXT>(
                get("vkGetSwapchainTimingPropertiesEXT"));
        out.GetSwapchainTimeDomainPropertiesEXT =
            reinterpret_cast<PFN_vkGetSwapchainTimeDomainPropertiesEXT>(
                get("vkGetSwapchainTimeDomainPropertiesEXT"));
        out.GetPastPresentationTimingEXT =
            reinterpret_cast<PFN_vkGetPastPresentationTimingEXT>(
                get("vkGetPastPresentationTimingEXT"));
    }

    if (ExtensionEnabled(enabledExtensions, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME))
    {
        // Optional and fail-soft, like VK_EXT_present_timing above. MoltenVK
        // versions have exposed this extension for years, but a mismatched
        // runtime must disable only Google pacing rather than the renderer.
        out.GetPastPresentationTimingGOOGLE =
            reinterpret_cast<PFN_vkGetPastPresentationTimingGOOGLE>(
                get("vkGetPastPresentationTimingGOOGLE"));
        out.GetRefreshCycleDurationGOOGLE =
            reinterpret_cast<PFN_vkGetRefreshCycleDurationGOOGLE>(
                get("vkGetRefreshCycleDurationGOOGLE"));
    }

    // --- optional low-latency extensions ------------------------------------
    //
    // Same contract as every other extension block: the entry points are looked
    // up only when the extension was actually enabled at vkCreateDevice, and a
    // null pointer for an enabled extension is a hard failure because it means
    // the driver contradicted its own extension list. A device that simply does
    // not have these never reaches here -- VulkanDevice does not put them in
    // EnabledExtensions -- and that path is a supported configuration, not an
    // error.
    if (ExtensionEnabled(enabledExtensions, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))
    {
        MELONPRIME_VK_LOAD_DEVICE(WaitSemaphoresKHR,            vkWaitSemaphoresKHR);
        MELONPRIME_VK_LOAD_DEVICE(GetSemaphoreCounterValueKHR,  vkGetSemaphoreCounterValueKHR);
    }

    if (ExtensionEnabled(enabledExtensions, VK_NV_LOW_LATENCY_2_EXTENSION_NAME))
    {
        MELONPRIME_VK_LOAD_DEVICE(SetLatencySleepModeNV,        vkSetLatencySleepModeNV);
        MELONPRIME_VK_LOAD_DEVICE(LatencySleepNV,               vkLatencySleepNV);
        MELONPRIME_VK_LOAD_DEVICE(SetLatencyMarkerNV,           vkSetLatencyMarkerNV);
        MELONPRIME_VK_LOAD_DEVICE(GetLatencyTimingsNV,          vkGetLatencyTimingsNV);
        MELONPRIME_VK_LOAD_DEVICE(QueueNotifyOutOfBandNV,       vkQueueNotifyOutOfBandNV);
    }

    if (ExtensionEnabled(enabledExtensions, VK_AMD_ANTI_LAG_EXTENSION_NAME))
    {
        MELONPRIME_VK_LOAD_DEVICE(AntiLagUpdateAMD,             vkAntiLagUpdateAMD);
    }

    #undef MELONPRIME_VK_LOAD_DEVICE

    // VK_EXT_debug_utils is an *instance* extension whose device-level
    // commands are resolved from the device. It is optional everywhere, so a
    // null pointer here is a supported configuration rather than an error --
    // Vk::SetDebugName() and the label helpers all no-op on null.
    if (instanceFns.CreateDebugUtilsMessengerEXT)
    {
        out.SetDebugUtilsObjectNameEXT =
            reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(get("vkSetDebugUtilsObjectNameEXT"));
        out.CmdBeginDebugUtilsLabelEXT =
            reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(get("vkCmdBeginDebugUtilsLabelEXT"));
        out.CmdEndDebugUtilsLabelEXT =
            reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(get("vkCmdEndDebugUtilsLabelEXT"));
        out.QueueBeginDebugUtilsLabelEXT =
            reinterpret_cast<PFN_vkQueueBeginDebugUtilsLabelEXT>(get("vkQueueBeginDebugUtilsLabelEXT"));
        out.QueueEndDebugUtilsLabelEXT =
            reinterpret_cast<PFN_vkQueueEndDebugUtilsLabelEXT>(get("vkQueueEndDebugUtilsLabelEXT"));
    }

    return true;
}

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
