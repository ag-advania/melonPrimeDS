// MelonPrimeDS - shared production Metal shader library (PR-14: MSL
// asset/metallib).
//
// Historically every Metal-owning translation unit embedded its own MSL as a
// C string literal and compiled it at runtime with
// -[MTLDevice newLibraryWithSource:options:error:]. The MSL now lives as
// physical .metal sources under src/shaders/metal/, compiled ahead-of-time
// into melonPrimeDS.metallib (see src/frontend/qt_sdl/CMakeLists.txt) and
// copied into the app bundle's Contents/Resources.
//
// This header exposes one process-wide loader for that bundled metallib so
// every migrated call site asks for the same id<MTLLibrary> instead of
// recompiling shader source (or a redundant metallib load) per renderer.
//
// Release contract (MELONPRIME_METAL_ALLOW_SOURCE_FALLBACK not defined and
// NDEBUG defined): production shader call sites must treat a nil return here
// as fatal-to-that-feature (fail closed) and must NOT fall back to
// -newLibraryWithSource: for production shaders. Debug builds (NDEBUG not
// defined) or an explicit MELONPRIME_METAL_ALLOW_SOURCE_FALLBACK build may
// still keep a source-compile fallback per call site for iteration without a
// full CMake re-run/app-bundle copy step; see MELONPRIME_METAL_ALLOW_SOURCE_FALLBACK
// usage at each migrated call site.
//
// See docs/plans/melonPrimeDS_develop_metal_完全Metal化_実行指示書_17b46586_2026-07-17.md
// §20 (PR-14).

#ifndef MELONPRIME_METAL_LIBRARY_H
#define MELONPRIME_METAL_LIBRARY_H

#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL) // scatter-budget-exempt: Metal build-gate, not input dispatch

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

// MELONPRIME_METAL_BUNDLED_METALLIB_V1
namespace MelonPrime::Metal {

#ifdef __OBJC__

// Returns the shared production shader library loaded from the app bundle's
// Contents/Resources/melonPrimeDS.metallib for `device`. The result is
// cached per device (the load itself only happens once per distinct
// id<MTLDevice> the process encounters). Returns nil if the bundled
// metallib is missing, unreadable, or incompatible with `device` --
// callers are responsible for the release/debug fallback policy documented
// above.
id<MTLLibrary> MelonPrimeMetalDefaultLibrary(id<MTLDevice> device);

#endif // __OBJC__

// True once at least one MelonPrimeMetalDefaultLibrary() call has been made
// (regardless of whether that load succeeded). Used by audits/diagnostics to
// confirm the bundled-metallib path is actually exercised at runtime rather
// than only existing as unreachable code.
bool MelonPrimeMetalDefaultLibraryLoadAttempted() noexcept;

// True once a MelonPrimeMetalDefaultLibrary() load has succeeded at least
// once for some device in this process.
bool MelonPrimeMetalDefaultLibraryLoadSucceeded() noexcept;

} // namespace MelonPrime::Metal

#endif // MELONPRIME_ENABLE_METAL (Apple-only gate above)
#endif // MELONPRIME_METAL_LIBRARY_H
