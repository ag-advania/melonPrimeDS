# Phase 4 post-FinishFrame crash — root cause found (S81-1/S81-4/S81-5)

## Summary

The crash that blocked Phase 4 across S77 through S81 was a **compile-definition
mismatch producing a real ODR (One Definition Rule) violation on `class GPU`**,
not a logic bug in `NDS::RunFrame()`, `GPU::FinishFrame()`, or anywhere in the
frame-lifecycle code every prior session's tracing focused on.

`src/frontend/qt_sdl/CMakeLists.txt` builds
`SapphireGenerated/SapphireFrameLatchCore.cpp` and
`SapphireGenerated/SapphireFrameQueueCore.cpp` as two separate CMake `OBJECT`
libraries (`sapphire_frame_latch_core`, `sapphire_frame_queue_core`), whose
objects are then folded into the final `melonDS` executable via
`target_sources(melonDS PRIVATE $<TARGET_OBJECTS:...>)`. Both files
`#include "GPU.h"` transitively, and `GPU.h` `#include`s `<vulkan/vulkan.h>`
(and, through it, headers that depend on `VMA_STATIC_VULKAN_FUNCTIONS` /
`VMA_DYNAMIC_VULKAN_FUNCTIONS`) whenever `MELONPRIME_ENABLE_VULKAN` is
defined. Their `target_compile_definitions()` only ever set `MELONPRIME_DS`,
`MELONPRIME_ENABLE_VULKAN=1`, and `VK_NO_PROTOTYPES=1` — they were missing
`VK_USE_PLATFORM_WIN32_KHR=1` (Windows-only) and both VMA function macros,
all three of which `src/CMakeLists.txt`'s `core` target (the library that
actually compiles `GPU.cpp` and constructs every `GPU` instance) does define.

That mismatch means `class GPU` compiled to two different in-memory shapes
across the final binary: the shape `core` used when constructing the real
object, and the shape `SapphireFrameLatchCore.cpp` used when reading through
a `GPU&` reference. Reading a member through the wrong shape reads whatever
happens to sit at that offset in the *real* layout instead — in this case,
`melonDS::SapphireGPU2D::SoftRenderer::GetDebugCaptureStats()` (called via
`GPU::TryGetSapphireRenderer2D()` → `SapphireVulkan2DAccess.get()`) read
`this == 0xFFFFFFFFFFFFFFFF` and faulted immediately (`mov (%rcx),%rax`, the
very first instruction of the function).

## Why the fix is unambiguous even without pinning the exact byte offset

The read-through-wrong-layout mechanism doesn't require identifying which
specific member shifted. The correct fix is unconditional: every translation
unit that shares a header must be compiled with identical values for every
macro that header (or anything it includes) branches on. `src/CMakeLists.txt`
already establishes the ground truth for `core`:

```cmake
target_compile_definitions(core PUBLIC MELONPRIME_ENABLE_VULKAN=1)
target_compile_definitions(core PRIVATE VK_NO_PROTOTYPES=1)
if (WIN32)
    target_compile_definitions(core PRIVATE VK_USE_PLATFORM_WIN32_KHR=1)
endif()
target_compile_definitions(core PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
```

`sapphire_frame_latch_core` / `sapphire_frame_queue_core` (and the
`sapphire_frame_queue_differential_test` executable that also links
`sapphire_frame_queue_core`'s objects) now share a
`MELONPRIME_SAPPHIRE_GENERATED_CORE_VULKAN_DEFINES` CMake list with exactly
these values, so they can never drift from `core` again silently.

`_WIN32_WINNT` was deliberately **not** added to the shared list: `core`
defines it as `_WIN32_WINNT_WIN8`, while `melonDS` (frontend, via Qt's own
`WINVER=0x0A00`) already defines it as `0x0A00` — the two disagree today
without an observed layout-affecting symptom, which is evidence (not proof)
that this particular macro isn't what changes `class GPU`'s shape. Revisit
if a different corruption pattern ever surfaces.

## How this was found (S81-1/S81-4)

1. Discovered (separately, S81-1) that `MELONPRIME_DIAGNOSTIC_SYMBOLS=ON`
   never actually produced a symbolizable binary: `ENABLE_LTO_RELEASE`
   defaulted `ON`, and `CMAKE_EXE_LINKER_FLAGS_RELEASE` unconditionally
   appended `-s` (strip), discarding `-g`'s debug info regardless of the
   diagnostic flag. Fixed in `CMakeLists.txt`; added a dedicated
   `rebuild-mingw-x86_64` preset/build tree
   (`build-mingw-vulkan-sapphire-rebuild-exact.bat`) with both disabled.
2. Built that tree with `MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=OFF` (the exact
   variant has its own, unrelated, not-yet-fixed compile errors — see
   `EXACT_PIN_COMPILE_STATUS.md`) and reproduced the cold-start crash.
3. `addr2line -e melonPrimeDS.exe -f -C -i <imagebase + rva>...` (image base
   must be added to the crash report's RVA — `0x140000000` for this link —
   addr2line does not want a raw section-relative RVA) resolved the full
   call stack for the first time across the whole S77-S81 investigation:
   `EmuThread::run()` → `MelonPrimeVulkanFrontendSession::completeProducerFrame()`
   → `latchAndPrepareProducerFrameLocked()` →
   `SapphireVulkanFrameLatch::latchSoftPackedFrameSnapshot()` →
   `SapphireFrameLatchCore.cpp` → crash in
   `SoftRenderer::GetDebugCaptureStats()`.
4. `nm -C --defined-only --numeric-sort` located the crash IP exactly at the
   start of `melonDS::SapphireGPU2D::SoftRenderer::GetDebugCaptureStats() const`
   (offset 0 — confirmed by disassembling with `objdump -d`: the very first
   instruction, `mov (%rcx),%rax`, faults). `RCX` (the x64 `this` argument
   register) held `0xFFFFFFFFFFFFFFFF` in the crash report's register dump,
   even though the caller had already null-checked its pointer — proving the
   pointer wasn't null, it was corrupt.
5. Comparing the *actual* `ninja -t targets`/`build.ninja` `DEFINES =` lines
   for `SapphireFrameLatchCore.cpp.obj` against `core`'s (captured from an
   earlier failed exact-pin compile log) found the three missing macros.

## Verification status

Fix applied 2026-07-16; a full rebuild (219 stale targets after the define
change touched shared compile flags) and cold-start re-run are the next
steps to confirm the crash is actually gone, not just that the mismatch is
real (the mismatch itself is not in question — it's directly observable in
the two targets' own `target_compile_definitions()` calls).
