// MelonPrimeDS - shared Metal device context (full-Metal-ification Phase M2).
// See MetalContext.h for rationale.

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "MetalContext.h"

#include <cstdio>
#include <mutex>

namespace melonDS
{

void* MelonPrimeSharedMetalDeviceHandle() noexcept
{
    static id<MTLDevice> device = nil;
    static std::once_flag once;
    std::call_once(once, []() {
        device = MTLCreateSystemDefaultDevice();
        if (device)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal shared context: device='%s' lowPower=%d "
                "removable=%d unifiedMemory=%d\n",
                device.name.UTF8String,
                device.isLowPower ? 1 : 0,
                device.isRemovable ? 1 : 0,
                device.hasUnifiedMemory ? 1 : 0);
        }
        else
        {
            std::fprintf(stderr,
                "[MelonPrime] metal shared context: MTLCreateSystemDefaultDevice() "
                "returned nil\n");
        }
    });
    return (__bridge void*)device;
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
