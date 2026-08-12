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

#ifndef VULKAN_COMMON_H
#define VULKAN_COMMON_H

// MelonPrime Vulkan backend -- shared low-level plumbing.
//
// Like the DX12 equivalent this is a hard build gate: without MELONPRIME_DS +
// MELONPRIME_ENABLE_VULKAN the translation unit collapses to nothing, so no
// Vulkan type or symbol can leak into the Software / OpenGL / DX12 / Metal
// paths.
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

// Everything is resolved through vkGetInstanceProcAddr / vkGetDeviceProcAddr at
// runtime, so the static prototypes must never be declared: a stray strong
// reference to e.g. vkCreateInstance would force a link against the loader
// import library and make melonPrimeDS refuse to start on a machine with no
// Vulkan runtime. CMake defines this on the target; the local fallback keeps
// the header self-contained for syntax checks and tooling.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    // vulkan.h pulls in windows.h for this platform block. It is required for
    // VkWin32SurfaceCreateInfoKHR, which the presenter needs; declaring it here
    // keeps every Vulkan translation unit consistent instead of depending on
    // per-file define ordering.
    #ifndef VK_USE_PLATFORM_WIN32_KHR
    #define VK_USE_PLATFORM_WIN32_KHR
    #endif
#endif

// The Linux (Xlib/XCB/Wayland) and macOS (Metal) platform blocks are NOT forced
// on here: they drag in X11/xcb/wayland/Objective-C headers that core has no
// business including. The frontend surface adapters define the one they need
// before including this header, and VulkanLoader resolves the matching
// vkCreate*SurfaceKHR entry point only when the corresponding macro is visible.

#include <vulkan/vulkan.h>

#include "VulkanModernPresentCompat.h"

#include "Platform.h"
#include "types.h"

namespace melonDS::Vk
{

// Human-readable VkResult. Covers every result the backend can observe; unknown
// codes fall through to a caller-owned buffer-free "VkResult(<n>)" formatting in
// FormatResult() so a driver-specific extension result is still reported.
const char* ResultToString(VkResult result) noexcept;

// "VK_ERROR_DEVICE_LOST (-4)" style string, safe for any VkResult value.
std::string FormatResult(VkResult result);

// Logs `operation`, the VkResult and the call site at Error level. Always
// returns false so call sites can `return Vk::Fail(...)`.
bool Fail(const char* operation, VkResult result, const char* file, int line);

// Returns true when `result` is VK_SUCCESS. Otherwise logs and returns false.
//
// VK_INCOMPLETE and VK_SUBOPTIMAL_KHR are deliberately treated as failures
// here: they are meaningful only to the two call sites that enumerate arrays or
// present, and those handle the code explicitly rather than going through this
// helper. Silently accepting them everywhere else would hide truncated
// enumerations.
inline bool Check(VkResult result, const char* operation, const char* file, int line)
{
    if (result == VK_SUCCESS) [[likely]]
        return true;
    return Fail(operation, result, file, line);
}

// Usage: if (!MELONPRIME_VK_CHECK("vkCreateBuffer", dispatch.CreateBuffer(...))) return false;
#define MELONPRIME_VK_CHECK(operation, expr) \
    ::melonDS::Vk::Check((expr), (operation), __FILE__, __LINE__)

// Vulkan object handles come in two shapes: dispatchable handles are pointers,
// non-dispatchable handles are uint64_t on 64-bit and (with the default
// VK_DEFINE_NON_DISPATCHABLE_HANDLE) also uint64_t on 32-bit. A single
// reinterpret_cast would be ill-formed for one of the two, so the conversion is
// selected at compile time.
template <typename HandleT>
[[nodiscard]] inline u64 ToObjectHandle(HandleT handle) noexcept
{
    if constexpr (std::is_pointer_v<HandleT>)
        return reinterpret_cast<u64>(handle);
    else
        return static_cast<u64>(handle);
}

// Inverse of ToObjectHandle(), used by the deferred destruction queue: it
// stores every pending object as a u64 and has to hand the original handle type
// back to the matching vkDestroy* call. The same two-shape problem applies, so
// the conversion is again selected at compile time.
template <typename HandleT>
[[nodiscard]] inline HandleT FromObjectHandle(u64 handle) noexcept
{
    if constexpr (std::is_pointer_v<HandleT>)
        return reinterpret_cast<HandleT>(static_cast<std::uintptr_t>(handle));
    else
        return static_cast<HandleT>(handle);
}

// Attaches a debug name to a Vulkan object.
//
// `setName` is the resolved vkSetDebugUtilsObjectNameEXT pointer, which is null
// whenever VK_EXT_debug_utils was not enabled -- the normal case in a release
// build. The null check is the entire opt-out mechanism: no name string is
// built, no call is made, and the caller does not have to branch.
inline void SetDebugName(
    PFN_vkSetDebugUtilsObjectNameEXT setName,
    VkDevice device,
    VkObjectType objectType,
    u64 objectHandle,
    const char* name) noexcept
{
    if (!setName || device == VK_NULL_HANDLE || !name || objectHandle == 0)
        return;

    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = objectType;
    info.objectHandle = objectHandle;
    info.pObjectName = name;
    setName(device, &info);
}

// Convenience overload that derives the handle representation from the type.
template <typename HandleT>
inline void SetDebugName(
    PFN_vkSetDebugUtilsObjectNameEXT setName,
    VkDevice device,
    VkObjectType objectType,
    HandleT handle,
    const char* name) noexcept
{
    SetDebugName(setName, device, objectType, ToObjectHandle(handle), name);
}

// "525.116.04" / "31.0.24033.1003" style decoding. Vendors pack the driver
// version into the 32-bit field differently, and only NVIDIA and Intel-on-
// Windows deviate from the Vulkan VK_API_VERSION_* layout, so the vendor ID is
// required to print anything meaningful.
std::string FormatDriverVersion(u32 vendorId, u32 driverVersion);

// "1.3.280" from a packed VK_MAKE_API_VERSION value.
std::string FormatApiVersion(u32 apiVersion);

// PCI vendor ID to a short name, for the startup log. Unknown IDs come back as
// a hex string rather than "Unknown" so an unfamiliar device is still
// identifiable in a user's log.
std::string FormatVendor(u32 vendorId);

// Rounds `value` up to the next multiple of `alignment`. VkDeviceSize
// throughout: at 16x internal resolution the rasterizer's tile buffers are
// close to 1 GiB, so every size computation must stay 64-bit even on a 32-bit
// build.
[[nodiscard]] constexpr VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment) noexcept
{
    if (alignment <= 1)
        return value;
    return ((value + alignment - 1) / alignment) * alignment;
}

[[nodiscard]] constexpr VkDeviceSize AlignDown(VkDeviceSize value, VkDeviceSize alignment) noexcept
{
    if (alignment <= 1)
        return value;
    return (value / alignment) * alignment;
}

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_COMMON_H
