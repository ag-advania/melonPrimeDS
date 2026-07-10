// MelonPrimeDS - Metal backend feature probe (experimental, Metal-plan Phase 2)
//
// Compiled only when MELONPRIME_METAL_ACTIVE selected MELONPRIME_ENABLE_METAL
// (see MELONPRIME_ENABLE_METAL / MELONPRIME_FORCE_DISABLE_METAL in
// CMakeLists.txt and .claude/rules/melonprime-metal-backend-plan.md). Probes
// whether this process can construct a usable MTLDevice, command queue, and
// a minimal render pipeline targeting BGRA8Unorm, without requiring Metal 4,
// Apple-GPU-family-only features, MetalFX, argument buffer tier 2, or
// unified memory -- Intel Macs must pass this baseline too.

#ifndef MELONPRIME_METAL_FEATURE_CHECK_H
#define MELONPRIME_METAL_FEATURE_CHECK_H

#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL) // scatter-budget-exempt: Metal build-gate, not input dispatch

#include <cstdint>
#include <string>

namespace MelonPrime::Metal {

struct FeatureInfo
{
    bool hasDevice = false;
    bool supportsRequiredBaseline = false;
    // Phase 8 texturing (GPU3D_Metal.mm TexcacheMetalLoader / the opaque
    // textured pass): MTLTextureType2DArray + MTLPixelFormatRGBA8Uint
    // allocation, upload, and texture2d_array<uint> sampling through a
    // nearest sampler, verified end-to-end (not just pipeline creation).
    // Folded into supportsRequiredBaseline once this is also required.
    bool supportsTextureArraySampling = false;
    bool isLowPower = false;
    bool isRemovable = false;
    bool hasUnifiedMemory = false;
    uint64_t recommendedMaxWorkingSetSize = 0;
    std::string deviceName;
    std::string unavailableReason;
};

// Runs MTLCreateSystemDefaultDevice() plus a minimal command-queue/pipeline
// smoke test exactly once per process and caches the result (std::call_once).
// The probe touches no window/AppKit state; it is safe to call from any
// thread, though the first call is expected to happen on the GUI thread
// alongside other startup feature checks.
const FeatureInfo& CachedFeatureInfo();

// True once CachedFeatureInfo().hasDevice is true (a device exists, even if
// it fails the baseline pipeline check).
bool IsRuntimeAvailable();

// True once CachedFeatureInfo().supportsRequiredBaseline is true. This is
// the gate every later phase (presenter, renderer, preset UI) must check
// before creating any Metal object.
bool SupportsRequiredBaseline();

// Logs one human-readable line (stderr, matching the existing
// "[MelonPrime] mac input: ..." convention in MelonPrimeRawInputMacFilter.mm)
// describing the probe result. Safe to call repeatedly; only the first call
// actually logs.
void LogFeatureInfoOnce();

} // namespace MelonPrime::Metal

#endif // MELONPRIME_ENABLE_METAL (Apple-only gate above)
#endif // MELONPRIME_METAL_FEATURE_CHECK_H
