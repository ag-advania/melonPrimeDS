// MelonPrimeDS - shared Metal device context (full-Metal-ification Phase M2).
//
// Both the Metal 3D raster renderer (root device creator) and the Metal
// presenter (MelonPrimeScreenMetal.mm) used to call MTLCreateSystemDefaultDevice()
// independently. On a dual-GPU Mac (integrated + discrete, automatic
// graphics switching) those two calls are not guaranteed to return the same
// device, which is exactly the mismatch MelonPrimeScreenMetal.mm's presenter
// already has a guard for. This header exposes one process-wide, lazily
// created device so every Metal-owning translation unit asks for the same
// GPU instead of separately discovering one.
//
// See docs/plans/melonPrimeDS_develop_完全Metal化_詳細修正指示書.md Phase M2.

#ifndef MELONPRIME_METAL_CONTEXT_H
#define MELONPRIME_METAL_CONTEXT_H

#if defined(MELONPRIME_ENABLE_METAL)

namespace melonDS
{

// Returns an id<MTLDevice> (as an opaque, already-retained-for-ARC bridge
// pointer) shared by every Metal renderer/presenter in this process. Safe to
// call from any thread; the device is created on first call and cached for
// the process lifetime.
void* MelonPrimeSharedMetalDeviceHandle() noexcept;

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // MELONPRIME_METAL_CONTEXT_H
