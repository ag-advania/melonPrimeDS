// MelonPrimeDS - experimental Metal renderer shell (Metal-plan Phase 7)

#if defined(MELONPRIME_ENABLE_METAL)

#include "GPU_Metal.h"

#include <cstdio>

#include "NDS.h"

namespace melonDS
{

MetalRenderer::MetalRenderer(melonDS::NDS& nds) noexcept
    : SoftRenderer(nds)
{
}

bool MetalRenderer::Init()
{
    std::fprintf(stderr,
        "[MelonPrime] metal renderer: using Software 2D/3D output with Metal presentation; native Metal GPU3D port pending\n");
    return SoftRenderer::Init();
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
