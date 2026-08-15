/*
    Production Vulkan loader open check.

    This executable deliberately calls melonPrimeDS's VulkanLoader::Library
    rather than duplicating its candidate names or using a weaker file-exists
    probe. It therefore verifies the real runtime dlopen()/LoadLibrary() path
    and the mandatory global dispatch symbols.
*/

#include <cstdio>

#include "Platform.h"
#include "VulkanLoader.h"

namespace melonDS::Platform
{
// The standalone test links the core library but not the Qt frontend's
// Platform.cpp. Loader diagnostics are not part of this check's result.
void Log(LogLevel, const char*, ...)
{
}
} // namespace melonDS::Platform

int main()
{
    melonDS::Vk::Library library;
    if (!library.Open())
    {
        std::fprintf(stderr, "FAIL: production Vulkan loader open: %s\n",
            library.GetFailureReason().c_str());
        return 1;
    }

    if (!library.IsOpen()
        || library.GetLibraryName().empty()
        || !library.Global().GetInstanceProcAddr)
    {
        std::fprintf(stderr,
            "FAIL: production Vulkan loader opened without a valid global dispatch\n");
        return 1;
    }

    std::printf("PASS: production Vulkan loader opened %s\n",
        library.GetLibraryName().c_str());
    return 0;
}
