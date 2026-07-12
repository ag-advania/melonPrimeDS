# Phase 8 Vulkan texture-cache/capture lifecycle completion verification

## Scope

This phase closes the Phase 8 subsystem contract on top of Phase 8.1 through 8.3.
It adds one integrated developer harness for:

- texture-key, all-format decode, dirty-page detection and upload-ring acquisition
- timeline-semaphore retirement with fence/serial fallback
- DS display capture source A/source B/blend behavior
- partial-line and destination-wrap capture
- capture-backed texture reuse
- CPU LCDC VRAM synchronization and dirty-page marking
- savestate, reset and renderer-switch lifecycle boundaries
- live 1x through 16x scale planning, memory-budget failure and presenter-lease deferral
- deterministic resource destruction across three create/run/destroy iterations

## Required result

`phase8-completion.json` must report `schema_version=19`, three completed iterations,
`phase8_subsystem_complete=true`, and every Phase 8 exit-audit field as true.
Either timeline semaphore or the fence/serial fallback must be active.

## Deliberate boundary

This completes the Phase 8 texture/cache/capture lifecycle subsystem and its Vulkan GPU harness.
It does not switch ROM rendering from the Software correctness baseline and does not claim the
later native DS polygon/2D renderer integration.
