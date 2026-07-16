# Code Boundaries

## Ownership

- Keep MelonPrime behavior in MelonPrime-owned files, wrappers, registries, or clearly bounded `MELONPRIME_DS` sections. Avoid unguarded behavior changes in upstream-owned melonDS code.
- Prefer hook points in the frontend to broad edits of upstream core files. If an upstream-owned file must change, keep the diff minimal, guarded, and documented.
- Do not patch upstream Qt `.cpp` files merely to translate melonDS UI. Use the MelonPrime localization integration in `Window.cpp` and the localization module.
- Preserve config/TOML key compatibility. Canonical key ownership is in `MelonPrimeDef.h`, `MelonPrimeHudPropSchema.inc`, and `MelonPrimeOsdColorSchema.inc`; avoid new string-literal mirrors.

## Unity include and generated-file boundaries

- `.inc` fragments are owned by their including `.cpp` or parent `.inc`. Never add unity fragments directly to CMake source lists.
- `MelonPrimeHudRender*.inc`, `MelonPrimeHudConfigOnScreen*.inc`, `MelonPrimeHudScreenCpp*.inc`, and `MelonPrimeEmuThread*.inc` follow this ownership rule.
- Edit generator inputs, then regenerate outputs with `tools/codegen/hud/generate-hud-prop-schema.py`. Commit source and generated changes together.

## Patch lifecycle

- Route game patches through `MelonPrimePatchRegistry` and the documented lifecycle; do not add scattered direct writes or duplicate per-ROM hook tables.
- Use shared strict-aliasing-safe RAM helpers and validated ROM/version tables. Preserve apply/restore/reset symmetry and instance isolation.
- Keep immediate input edges, per-frame runtime state, and persistent config state distinct.

Detailed boundaries: [`docs/architecture/repository.md`](../../docs/architecture/repository.md), [`docs/architecture/gameplay/patch-system.md`](../../docs/architecture/gameplay/patch-system.md). Historical upstream audit evidence: [`docs/archive/audits/upstream/`](../../docs/archive/audits/upstream/).
