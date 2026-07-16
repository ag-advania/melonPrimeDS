# V7 Phase 2 verification record

`verify-hud-side-panel-rows-migration.py` is a one-time verification script
used during V7 Phase 2 (2026-07-09) to prove that converting the HUD editor
side panel's hand-written `populate*()` factory-call sequences
(`MelonPrimeHudConfigOnScreenEdit.cpp`) into data-table rows
(`MelonPrimeHudEditorSidePanelRows.inc`) did not change the sequence of
widget-factory calls (name, label, keys, ranges, combo items) for any of the
14 side-panel elements.

It compares the pre-refactor file (captured as `/tmp/old_edit.cpp` via
`git show <pre-refactor-commit>:...`) against the post-refactor row tables +
residual code, reconstructing an equivalent call sequence from each and
diffing them. At the time it was run, all 14 elements matched exactly
(178 total factory calls, zero divergence).

This is not a standing CI gate — it depends on a specific pre-refactor
snapshot that no longer exists in the working tree, and the migration it
verifies is done. Kept as a record of the verification method, in case a
similar factory-call-sequence table migration is done elsewhere later.
