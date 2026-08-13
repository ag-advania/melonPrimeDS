# Intel MacBook VK_GOOGLE_display_timing implementation evidence

Date: 2026-08-14 (JST)  
Implementation commit: `d8acd67bb`  
Machine: MacBookPro15,2, Intel Core i5, Intel Iris Plus Graphics 655, 16 GB  
Display: built-in Retina display  
Power: AC power during the five-minute pinned-runtime smoke  
Internal resolution: 1x (no high-load scale was used)

## Capability gate

The standalone `vulkan-google-display-timing-probe` loaded each dylib directly,
created an instance and presentation-capable logical device, and observed:

| Runtime | Device extension | `vkGetPastPresentationTimingGOOGLE` | `vkGetRefreshCycleDurationGOOGLE` |
|---|---:|---:|---:|
| Homebrew MoltenVK 1.4.2 | yes | resolved | resolved |
| pinned MoltenVK 1.4.0 | yes | resolved | resolved |

This selected the instruction's implementation branch. The renderer requests
the extension optionally and retains `VK_EXT_present_timing` priority whenever
the EXT backend is usable.

## Physical 1.4.2 functional matrix

One Release A/B build was used with developer features off and latency capture
on. The same Japanese ROM, FIFO/VSync-on presentation, built-in display and 1x
internal resolution were retained except for the explicit VSync-off control.

| Mode | Rows | Observed contract | Result |
|---|---:|---|---|
| TelemetryOnly | 1,456 | backend GOOGLE, targets 0%, feedback 1,452 rows | PASS |
| PresentWait | 1,598 | backend GOOGLE, targets 0%, wait allowed | PASS (functional) |
| JustInTime | 1,283 | backend GOOGLE, absolute targets 100%, feedback 1,282 rows, targets monotonic | PASS |
| JustInTime, VSync off | 833 | IMMEDIATE, targets 0%, fallback non-FIFO | PASS |

The first JIT diagnostic exposed an Apple clock-domain bug before adoption:
libc++ `steady_clock` included system sleep while MoltenVK's reported
presentation clock used `mach_absolute_time`. The target and feedback epochs
were therefore different. The implementation now converts
`mach_absolute_time` to nanoseconds on Apple; the repeated functional run
confirmed target and feedback on the same timeline.

No GOOGLE result was copied into the EXT-only feedback-stage column. GOOGLE's
desired, actual, earliest and margin values have separate capture columns, and
the aggregator validates the backend/mode relationship and monotonic targets.

## Pinned MoltenVK 1.4.0 five-minute smoke

The pinned dylib was installed into the same signed Release A/B bundle for this
run only; its log and CSV were kept separate from 1.4.2. The Homebrew 1.4.2
dylib was restored and the bundle re-signed after the run.

| Check | Observation |
|---|---:|
| Duration | 5 minutes |
| Captured frames | 17,296 |
| GOOGLE backend rows | 17,296 (100%) |
| Absolute target rows | 17,296 (100%) |
| Feedback rows | 17,295 |
| Non-monotonic targets | 0 |
| Swapchain generations in measured run | 1 |
| GOOGLE/EXT timing queue-full errors | 0 |
| `DEVICE_LOST`, `SIGABRT`, VUID | 0 |

MoltenVK 1.4.0 emitted its known primitive-restart feature warning. It did not
disable the renderer or the GOOGLE timing backend.

## Validation performed

- `vulkan-present-timing-tests`: PASS
- aggregate Vulkan latency tests: PASS
- low-latency contract audit: PASS
- `git diff --check`: PASS
- official `tools/build/macos/build_macos_metal_n_vulkan.command --jobs 4`: PASS
- MoltenVK bundled into the app and ad-hoc bundle signature verified: PASS

## Formal A/B status

Formal M0/M1/M2 is **NOT RUN**. The runbook requires a human-controlled fixed
in-match scene (same savestate, room, camera and HUD), three randomized runs per
mode, 600 warm-up frames and at least 10,000 measured frames per run. This
checkout has a ROM save but no reproducible in-match savestate or automation to
establish that scene. Boot/menu captures would not satisfy that contract and
are not reported as Formal evidence.

Short functional captures also showed frequent two-millisecond bounded-wait
timeouts on this MoltenVK setup. That is recorded as a performance concern for
M1/M2 comparison, not hidden as a GOOGLE success. The shipping default remains
TelemetryOnly until controlled Formal evidence justifies a separate change.
