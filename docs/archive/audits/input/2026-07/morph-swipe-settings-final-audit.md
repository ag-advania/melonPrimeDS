# Morph swipe, wheel input, and Sensitivity reset — final audit (2026-07)

<!-- MELONPRIME_FINAL_DOC_AUDIT_V18 -->

## Scope

This audit covers the Morph Ball mouse-swipe series through V17, the mouse-wheel weapon-cycle Disable setting, Sensitivity UI placement, compiled-default reset behavior, the compact CLAUDE rule layout, and the long-form documents affected by those changes.

## Findings corrected

1. `docs/development/ui/settings-and-edit-mode.md` still stated that a settings-dialog default reset was not implemented. V17 implements a Sensitivity-section reset, so the document was stale.
2. `docs/architecture/repository.md` described three typed default lists and only current-value `GetXxx()` accessors. The code has `DefaultStrings` and V17 adds guarded `GetDefaultXxx()` accessors.
3. The required-movement help text said that `90` matched the game's default. Since custom mode measures MelonPrime current-frame raw mouse counts, `90` is the MelonPrime setting default, not MPH's native internal swipe threshold.
4. The Morph feature document did not yet include V16's dynamic placement or V17's reset/commit boundary.

## CLAUDE rule compliance

- `CLAUDE.md` remains a short index to the six standing rules; no new long-form material is added under `.claude`.
- Common `Config.h/.cpp` additions remain inside `MELONPRIME_DS` guards.
- Runtime behavior remains snapshot/cached; the documentation correction adds no per-frame config lookup, allocation, or GUI access.
- UI reset writes widgets only and commits through the existing Save/OK path.
- Detailed evidence is stored under `docs/archive/`, and reusable checks stay under `tools/`.
- Release notes are generated outside the repository, following `docs/development/release/release-notes.md`.

## Static validation

The final check entry point runs:

- V18 exact-state and stale-wording checks
- `tools/maintenance/check-claude-layout.py`
- `tools/maintenance/check-doc-links.py`
- `tools/ci/audits/audit-config-defaults.ps1`
- Morph Ball and mouse-wheel localization audits
- `git diff --check`

## Validation boundary

Package fixture tests cover apply, reapply, rollback, hash-guarded revert, CRLF preservation, and stale-document detection. Windows compilation, Qt visual inspection, and gameplay/runtime testing were not performed by this audit and must not be inferred from the static result.

Localization coverage remains a structural claim. English source wording and Japanese wording are reviewed in this change; other languages retain their existing translations and have not received native-speaker review.
