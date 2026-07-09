// MelonPrimeDS - experimental Metal renderer shell (Metal-plan Phase 7)

#if defined(MELONPRIME_ENABLE_METAL)

#include "GPU_Metal.h"

#include <cstdio>

#include "NDS.h"

namespace melonDS
{

MetalRenderer::MetalRenderer(melonDS::NDS& nds) noexcept
    : Renderer(nds.GPU)
{
}

bool MetalRenderer::Init()
{
    std::fprintf(stderr,
        "[MelonPrime] metal renderer: shell is present but 2D/3D rendering is not implemented yet; falling back to Software\n");
    return false;
}

bool MetalRenderer::GetFramebuffers(void** top, void** bottom)
{
    if (top)
        *top = nullptr;
    if (bottom)
        *bottom = nullptr;
    return false;
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
