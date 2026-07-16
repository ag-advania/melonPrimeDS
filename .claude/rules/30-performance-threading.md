# Performance, State, and Threading

## Hot paths

- Treat per-frame gameplay, aim, render, audio, and emulation-thread code as hot paths. Avoid per-frame allocation, repeated config lookup, repeated asset decoding, avoidable locking, logging, and redundant RAM reads.
- Cache immutable or slowly changing configuration and rendered assets. Define explicit invalidation when config, hunter, language, renderer, DPI, font, or instance state changes.
- Preserve the hot/cold split in gameplay runtime; rare actions belong in cold helpers.
- Performance changes require measurement using the tools under `tools/perf/`; do not infer speedups from code shape alone.

## State ownership

- Mutable runtime state must have an explicit owner and lifetime. Prefer per-instance state over file-static mutable state.
- Keep emulator-core state on the emulation thread and QWidget/QObject mutations on the GUI thread. Cross boundaries through the existing command/snapshot mechanisms.
- Do not read live GUI objects from render/emulation hot paths. Publish immutable snapshots or cached POD state.
- Reset transient input, patch, HUD, and platform backend state on ROM/session/instance lifecycle transitions. Do not allow one instance to affect another.

## SRP contract

- Separate configuration acquisition, state transition, patch application, and rendering. A refactor must preserve ordering and observable behavior unless behavior change is the task.
- New property surfaces must remain in parity across schema/defaults, settings dialog, edit mode, side panel, runtime load, and serialization as applicable.
- Respect existing ratchets for instance state, config literals, thread boundaries, include ownership, and platform scatter.

Details: [`docs/architecture/performance.md`](../../docs/architecture/performance.md), [`docs/architecture/srp-performance-contract.md`](../../docs/architecture/srp-performance-contract.md), and [`docs/architecture/gameplay/runtime.md`](../../docs/architecture/gameplay/runtime.md).
