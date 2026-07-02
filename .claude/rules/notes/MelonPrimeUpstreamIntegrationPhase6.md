# MelonPrime Upstream Integration Phase 6

Date: 2026-07-02
Plan: V3 Phase 6

Phase 6 is optional. It should only extract or reshuffle upstream-owned hook
sites when there is an active upstream merge plan and a clear conflict-reduction
target.

## Current Counts

`MELONPRIME` occurrence counts in upstream-owned frontend files:

| File | Count |
|---|---:|
| `src/frontend/qt_sdl/EmuThread.cpp` | 48 |
| `src/frontend/qt_sdl/Screen.cpp` | 29 |
| `src/frontend/qt_sdl/Window.cpp` | 20 |
| `src/frontend/qt_sdl/Config.cpp` | 17 |
| `src/frontend/qt_sdl/EmuInstance.h` | 16 |
| `src/frontend/qt_sdl/Screen.h` | 15 |

## Decision

Skip code extraction in Phase 6.

Reasons:

- There is no current upstream merge target in this V3 refactor pass.
- The remaining blocks are functional hook sites, not obvious dead clutter.
- Extracting `Screen.cpp` or `Window.cpp` blocks into more unity fragments would
  trade merge conflicts for additional ownership/ordering checks.
- Phase 0 already protects `.inc` ownership, schema parity, and literal budget;
  adding more fragments without a merge driver would add process cost.

Revisit this note during the next upstream merge. If a file becomes a repeated
conflict hotspot, extract only a contiguous self-contained `#ifdef
MELONPRIME_DS` block and keep the include position byte-for-byte equivalent.
