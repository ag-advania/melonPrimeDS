// MelonPrimeDS - Metal renderer host interface (PR-7 SoftRenderer inheritance flip)
//
// MetalRenderer3D / MetalComputeRenderer3D used to take a SoftRenderer&
// "parent" reference purely so MetalRenderer (which used to inherit
// SoftRenderer) could be passed through unchanged. MetalRenderer no longer
// inherits SoftRenderer (see GPU_Metal.h); MetalRendererHost is the narrow
// interface it implements instead so the 3D constructors keep a stable
// parent-reference parameter without depending on SoftRenderer at all.

#ifndef GPU_METAL_HOST_H
#define GPU_METAL_HOST_H

#if defined(MELONPRIME_ENABLE_METAL)

namespace melonDS
{

class MetalRendererHost
{
public:
    virtual ~MetalRendererHost() = default;
};

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU_METAL_HOST_H
