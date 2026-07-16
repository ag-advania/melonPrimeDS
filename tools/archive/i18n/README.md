# Archived i18n one-shot audit scripts

Moved here in V7 Phase 1 (2026-07-09) from `tools/`.

These scripts were written as investigation/verification tooling for specific,
one-time i18n quality-fix passes described in
[melonprime-i18n-50lang-quality-audit.md](../../../docs/archive/audits/localization/2026-07/melonprime-i18n-50lang-quality-audit.md).
They are kept as a record of the methodology used at the time, not as
standing CI gates.

Most of them (`audit-melonprime-i18n-phase5a.py` through `...-phase10.py`)
reference external review-queue data (`i18n_quality_phase5a/` ...
`i18n_quality_phase10/`, several tens of MB of CSV/JSON) that was
intentionally **not committed** to this repo (same policy as ROMs or
screenshots — see the notes file above). Running them here will print
`[FAIL] missing ...` for those paths; that is expected, not a regression.

`audit-melonprime-i18n-qualityfix-pass1.py` through `...-pass5.py` measured
before/after leak-rate deltas for a specific 14-language quality-fix pass.
They are self-contained (no missing external data) but were single-use
verification for that one pass, superseded going forward by
`tools/ci/audits/localization/audit-melonprime-localization.py`.

`audit-melonprime-i18n-qualityfix-pass6.py` was removed because it was
byte-for-byte identical to pass 5. Git history remains the recovery source
for the duplicate filename.

`audit-melonprime-i18n-he-coverage.py` was an early per-language coverage
checker; it is self-contained and currently green, but its coverage check is
now subsumed by `tools/ci/audits/localization/audit-melonprime-all-new-language-coverage.py`
(kept in place, still a standing gate) and
`tools/ci/audits/localization/audit-melonprime-localization.py`.

## What is still a live gate

Kept in `tools/ci/audits/localization/` (not archived):

- `audit-melonprime-localization.py` — the strict, always-run i18n gate.
- `audit-melonprime-all-new-language-coverage.py` — coverage check driven by
  the committed `tools/melonprime_all_new_language_metadata.json`, currently
  green and self-contained.

If you need to reproduce or extend the methodology in one of the archived
scripts, read it for the approach, but write a new self-contained script
against `audit-melonprime-localization.py`'s conventions rather than trying
to re-run these against data that no longer exists in the repo.
