# Repository Tools

Reusable executable tooling lives outside `.claude` and is grouped by purpose:

- `build/windows/`, `build/macos/`, `build/linux/` — supported build wrappers
- `ci/audits/` — standing CI audits and localization checks
- `codegen/hud/` — HUD schema generator
- `perf/` — performance collection, comparison, and summarization
- `testing/hud-golden/` — developer HUD golden harness
- `maintenance/` — repository-layout and documentation-link gates
- `linux-vm/` — VirtualBox Ubuntu setup, build, and smoke-test helpers
- `archive/` — one-time historical utilities; never invoke these from CI

Human procedures are indexed in [`docs/development/`](../docs/development/README.md). Run tools from the repository root unless their usage text says otherwise.
