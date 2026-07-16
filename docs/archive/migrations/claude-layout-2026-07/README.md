# Compact Claude Layout Migration (2026-07)

This migration moved 154 inventoried files out of the former large `.claude` tree. Detailed architecture and procedures now live under `docs/`, reusable executables under `tools/`, and only six short standing rule files remain under `.claude/rules/`.

[`manifest.json`](manifest.json) is the machine-readable source-to-destination ledger. It records the supplied archive SHA-256 (`d16d55d052337fd5116b262baf67521a6d7c80e1053e7ec4bb1f1e3adc45ac81`), original byte/line counts, action, phase, destination, and distillation target for all 154 files.

`tools/maintenance/check-claude-layout.py` enforces the final file set, six-rule index and per-rule size caps, the 32 KiB Markdown budget, executable ban, manifest totals and destination existence, workflow path parity, archive-tool isolation, and absence of legacy `.claude` references.

## Verification

The migration was locally verified on Windows with:

- compact-layout audit: 154 manifest entries, six rule files, 11,530 Markdown bytes
- Markdown link audit: 407 local links resolved
- HUD schema regeneration: 578 rows, zero missing defaults, zero type drift
- all standing config/HUD/INC/literal/platform/color/SRP audits
- focused thread-boundary and instance-state audits
- strict localization and all-new-language coverage audits
- MinGW existing-tree build through `tools/build/windows/build-mingw-existing.bat`
- shell/Python syntax checks and `git diff --check`

Windows, Ubuntu x86_64/aarch64, and macOS x86_64/arm64 workflows include the new layout/link gates. Their platform results must be recorded from GitHub Actions after this working tree is committed and pushed; local Windows success is not represented as cross-platform CI success.
