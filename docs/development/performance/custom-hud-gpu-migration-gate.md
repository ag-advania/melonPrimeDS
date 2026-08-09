# Custom HUD GPU migration measurement gate

This note records the measured decision for the Custom HUD CPU/GPU audit based
on commit `9b7db55b6763dc0de7f2a208a3874530dcbd0fbf`. It separates the cost of the
existing QPainter path before introducing a backend-neutral draw list or new
GPU HUD renderers.

## Instrumentation

Developer builds started with `MELONPRIME_PERF=1` emit one additional line per
one-second window:

```text
[MelonPrimePerf] hud_phase_us state_p50=... state_p99=... qpainter_p50=... qpainter_p99=... clear_p50=... clear_p99=... hash_p50=... hash_p99=... upload_prepare_p50=... upload_prepare_p99=... calls=... drawn=...
```

The phases are:

- `state`: game/config/cache/visibility work inside `CustomHud_Render()` before
  normal gameplay rasterization starts;
- `qpainter`: normal gameplay HUD rasterization;
- `clear`: creation or clearing of the retained CPU overlay;
- `hash`: OpenGL dirty-region equality hashing;
- `upload_prepare`: the CPU-visible dirty-region upload or staging operation.

Vulkan, DX12, Metal, OpenGL, and the QPainter fallback all use the same phase
names. A window with `drawn=0` is not an active-HUD sample. The summarizer
automatically excludes those windows from HUD phase and HUD render medians, so
ROM startup, HUD-hidden death screens, and post-match screens do not dilute the
result.

The existing `VulkanPerf` presenter timers and upload counters remain the
source for CPU submission/presentation context. This probe measures the CPU
side of the migration candidate. No GPU HUD renderer exists yet, so a dedicated
GPU HUD timestamp-query range is intentionally deferred with that renderer;
this document does not claim a GPU HUD execution-time result.

## Windows Vulkan measurement (2026-08-09)

Hardware and scenario:

- NVIDIA GeForce RTX 5070 Ti;
- Vulkan presenter, 60 Hz gameplay;
- Metroid Prime Hunters USA Rev 1;
- the F7 state was loaded before sampling and saved in a non-death gameplay
  state;
- only the first uninterrupted active-HUD interval was included;
- the first partial window containing state-load/font/asset warm-up was not
  used as a steady-state result.

The table contains the median of each one-second window's per-frame p50. The
last column is the sum of the five component medians, not a separately sampled
end-to-end percentile.

| Outer window | Active frames | state | QPainter | clear | hash | upload prepare | Component sum |
|---|---:|---:|---:|---:|---:|---:|---:|
| 784x448 | 424 | 3.5 us | 58.2 us | 6.8 us | 0.0 us | 7.5 us | 76.0 us |
| maximized 2580x1460 capture | 425 | 3.8 us | 169.0 us | 90.0 us | 0.0 us | 93.1 us | 355.9 us |

The corresponding median `CustomHud_Render` averages were 66.7 us and
180.3 us. At the maximized size, QPainter plus clear plus upload preparation
was about 0.352 ms/frame. The attempted 3840x2160 host resize was constrained
by the 2560x1440 desktop and produced a 2580x1460 outer window, so this run is
not presented as a 4K result.

Backend smoke coverage used the same F7 state and 784x448 outer window:

| Backend | Active frames | state | QPainter | clear | hash | upload prepare |
|---|---:|---:|---:|---:|---:|---:|
| DX12 | 413 | 3.7 us | 59.9 us | 7.7 us | 0.0 us | 20.0 us |
| OpenGL | 420 | 3.6 us | 55.9 us | 6.4 us | 0.0 us | 61.0 us |

The OpenGL dirty region in this scenario stayed below the large-region hash
threshold, so `hash=0` is expected rather than a missing probe. Metal uses the
same instrumentation points but was not built or run in this Windows session.

## Decision

The audit's priority guide raises GPU HUD work when QPainter/upload reaches
roughly 0.5-1.0 ms or exhibits a high-resolution spike. Neither tested size
crossed that gate. The maximized component sum was about 0.356 ms, while the
smaller window was about 0.076 ms.

Therefore the backend-neutral `HudFrameState`/`HudDrawList` migration and four
new GPU rasterizers are **deferred, not rejected**. Implementing them now would
replace a mature, pixel-verified renderer across Vulkan, DX12, Metal, OpenGL,
the editor, and software fallback for less than the measured trigger on this
system. The existing native GPU radar path and dirty-region uploads remain in
place.

Reopen the migration when a reproducible active-HUD run shows either:

- a steady component sum of at least 500 us/frame;
- a QPainter/upload p99 spike that materially affects frame pacing; or
- a real 3840x2160 result that crosses the same threshold.

At that point, retain the existing QPainter path as the software renderer and
golden-image oracle, and implement the audit's neutral draw-list boundary
before adding any backend-specific GPU HUD logic.

## Reproduction

Build with developer features, start the application with
`MELONPRIME_PERF=1`, load the target state, and keep the HUD visible for at
least several one-second windows. Then run:

```powershell
python tools/perf/summarize-melonprime-perf.py <perf-log>
```

The `HUD-active phase` section reports only windows with `drawn > 0`.
