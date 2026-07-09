# Completed Feature Notes

Finished feature design/progress documents for the SRP v3 refactor live here so
the active feature notes list stays easy to scan. The refactor itself is
summarized in [melonprime-refactoring.md](../../rules/melonprime-refactoring.md)
section "Structural Refactor V7 / SRP v3 Integration 2026-07", and the
binding, still-enforced contract these documents implemented lives at
[melonprime-srp-performance-contract.md](../../rules/melonprime-srp-performance-contract.md).

- [SRP v3 Completion Summary](melonprime-srp-v3-completion-summary.md) — the
  "read this first" overview of what was split, what was deliberately kept
  inline, and what manual smoke coverage remains open
- [SRP v3 Refactor Progress](melonprime-srp-refactor-v3-progress.md) — the
  full blow-by-blow history, commit hashes, and per-phase CI run IDs
- [PatchLifecycleGateway Step 3 Plan](melonprime_patch_lifecycle_gateway_step3_plan.md) —
  design for Sites A/B/D/E; still referenced by
  [melonprime-patch-system.md](../../rules/melonprime-patch-system.md) for why
  Site D's cleanup stays inline
- [PatchLifecycleGateway Site D Plan](melonprime_patch_lifecycle_gateway_site_d_plan.md)
- [Aim Config Reload Paths Audit](melonprime_aim_config_reload_paths_audit.md) —
  found and closed the dead `ApplyAimAdjustSetting` function
- [Aim Reload Outcome C Design Note](melonprime_aim_reload_outcome_c_design_note.md) —
  the `AimSensitivitySnapshot` design that shipped instead of "outcome B"
