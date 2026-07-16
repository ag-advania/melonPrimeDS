# MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN compile status (S81-1)

## Finding

`MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=ON` (compiles
`src/SapphireVendor/upstream/melonDS-android-lib/src/GPU2D_Soft.cpp` — the
byte-pinned upstream copy — instead of the normalized
`src/SapphireGPU2DCore/GPU2D_Soft.cpp`) has never actually compiled as part of
the full `core` library. It defaulted `OFF` even on the "pure Sapphire core"
rebuild branch (S81 audit finding), and turning it `ON` for a real build
(discovered 2026-07-16 while building the new `rebuild-mingw-x86_64` preset,
see S81-1) hits two, then a third, real compile error — not flag-wiring gaps.

## Fixed in this pass

1. **Include-guard collision.** `src/GPU2D.h` (repo's own, adapted header) and
   `src/SapphireVendor/upstream/melonDS-android-lib/src/GPU2D.h` (the exact
   vendor copy) both used the literal guard `GPU2D_H`. Whichever a
   translation unit included first silently suppressed the other via the
   preprocessor. Because repo's `GPU2D.h` declares `Renderer2D` directly under
   `namespace melonDS` while the vendor copy nests it under
   `namespace melonDS::GPU2D`, losing repo's copy to the collision broke
   unqualified `Renderer2D` lookup in `src/GPU.h` (`error: 'Renderer2D' was
   not declared in this scope` at `GPU.h:1161-1162`). Fixed by renaming the
   vendor copy's guard to `MELONPRIME_SAPPHIRE_VENDOR_UPSTREAM_GPU2D_H` — a
   pure preprocessor-token rename, zero semantic change, and safe because
   this file is not tracked by `tools/sapphire_vendor_manifest.json` (no SHA
   pin on it).
2. **Missing `Renderer3D::Accelerated`.** Upstream's `Renderer3D` stores a
   `const bool Accelerated` (constructor-injected). This repo's `Renderer3D`
   never had that field; it derives the same concept from the virtual
   `UsesStructured2DMetadata()` override, exposed via
   `GPU3D::IsRendererAccelerated()`. Adding a `bool` constructor parameter to
   every `Renderer3D` subclass (SoftRenderer, GLRenderer3D, MetalRenderer3D,
   the Vulkan renderers, ...) would be a large, risky, unrelated-looking
   change, so instead added a lazily-evaluated `AcceleratedProxy` member
   (`operator bool() const { return Owner->UsesStructured2DMetadata(); }`)
   under the existing `Renderer3D` `MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN`
   guard, so `renderer3d.Accelerated` reads exactly as upstream's vendor
   source expects, evaluated post-construction (virtual dispatch during the
   base constructor would only ever see the base's own trivial definition,
   hence the proxy instead of a plain bool field computed eagerly).

Both fixes verified individually via
`c++.exe -fsyntax-only` against `GPU2D_Soft.cpp` (through an MSYS2 login
shell — `c++.exe` invoked directly from a non-MSYS2 Git-Bash shell fails to
resolve its own runtime DLLs and exits 1 with no diagnostic output at all,
which looks identical to "compiled clean" until you check the exit code; do
not mistake silent no-output/exit-1 for success).

## Still blocking: `melonDS::GPU2D` is a class here, a namespace upstream

After both fixes above, the next (and structurally deeper) error is:

```
src/GPU2D.h:29:7: error: 'struct melonDS::GPU2D' redeclared as different kind of entity
   29 | class GPU2D
      |       ^~~~~
src/SapphireVendor/upstream/melonDS-android-lib/src/GPU2D.h:44:11: note: previous declaration 'namespace melonDS::GPU2D { }'
   44 | namespace GPU2D
      |           ^~~~~
```

This repo's architecture flattened upstream's `namespace melonDS::GPU2D {
class Unit; class Renderer2D; ... }` into a single `class GPU2D` declared
directly under `namespace melonDS` (see `src/GPU2D.h:29`). The vendor's exact
copy still uses the original nested-namespace shape. `melonDS::GPU2D` cannot
simultaneously be a class and a namespace in the same translation unit — this
is not a naming accident like the guard collision, it is two genuinely
incompatible declarations of the same qualified name. `GPU.h` unconditionally
`#include`s both (repo's adapted `GPU2D.h` directly, and — via the exact
vendor's own `GPU2D_Soft.h` → vendor `GPU2D.h` chain — the vendor's nested-
namespace shape), so as long as the exact-pinned `GPU2D_Soft.cpp` shares
`GPU.h` with the rest of the adapted codebase, this cannot be resolved by a
token rename the way the guard collision was.

### What a real fix looks like (not attempted here — separate, larger task)

The "exact pin" A/B design needs the vendored comparison copy to build against
its **own** self-contained object model, not literally share `GPU.h` with the
production/adapted renderer stack. Two directions, in order of increasing
invasiveness:

1. **Namespace-isolate the vendor tree.** Wrap the vendored
   `GPU2D.h`/`GPU2D_Soft.{h,cpp}` (and anything else under
   `src/SapphireVendor/upstream/...`) in a dedicated namespace (e.g.
   `melonDS::SapphireVendorExact`) so `GPU2D` unambiguously means "the
   upstream namespace" there and never collides with repo's `class GPU2D`.
   Same spirit as the guard rename (pure structural rename, no logic change),
   but touches every unqualified name the vendor file relies on resolving via
   ADL/namespace lookup — needs care, and should be scripted/checked against
   the vendor manifest process rather than hand-edited, since it's a
   real content change this time (not just a guard token).
2. **Give the exact-pinned target its own bridge/adapter**, compiled as an
   isolated component (matching the existing
   `sapphire_frame_queue_differential_test` executable pattern already used
   elsewhere in this repo for A/B testing) rather than linked into the full
   `core` library via `GPU.h`. This avoids the shared-header conflict
   entirely at the cost of needing an adapter layer to feed it the same input
   data the differential test harnesses already construct.

Until one of these lands, `MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=ON` should be
treated as **not buildable as part of the full `core`/`melonDS` targets**.
Rebuild-branch diagnostic builds (crash symbolization, cold-start testing)
should keep using `MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=OFF` (the normalized
`SapphireGPU2DCore/GPU2D_Soft.cpp`, which is what every historical crash
report — S77 through S81 — has actually exercised) until this is resolved.
