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

Formal M0/M1/M2 is **COMPLETE for the fixed F2 scene**. The run used the same
AMHJ Japanese real ROM and its matching F2 slot 2 savestate, three randomized
runs per mode, 600 warm-up frames and at least 10,000 measured frames per run.
The earlier AMHP-ROM/Japanese-state trial is rejected as an invalid pair; it
was the cause of the apparently frozen in-match clock. With the matching pair,
the camera/HUD/time advanced and no ARM9 abort, `DEVICE_LOST`, `SIGABRT`, or
VUID was observed. The diagnostic state-load hook now runs after `NDS::Start()`
and renderer selection.

| Mode | Runs | Frame P50 median | Frame P95 median | Frame P99 median | Pipeline P50 median | Pipeline P95 median |
|---|---:|---:|---:|---:|---:|---:|
| M0 TelemetryOnly | 3 | 16.671 ms | 20.719 ms | 23.036 ms | 16.611 ms | 18.703 ms |
| M1 PresentWait | 3 | 16.678 ms | 20.125 ms | 22.442 ms | 15.851 ms | 18.425 ms |
| M2 JustInTime | 3 | 16.688 ms | 20.506 ms | 23.326 ms | 15.543 ms | 18.474 ms |

M0 remains the shipping default. M2's median pipeline P50 was about 1.068 ms
better than M0, but median frame P99 was about 0.290 ms worse and median FPS
was lower (59.257 versus 59.708). This is **NO MATERIAL DIFFERENCE** under the
strict gate, not a correctness failure. M1/M2 bounded-wait timeout rates were
approximately 54.4–58.5%, which is not appropriate for this low-spec machine.
No 4x or 16x configuration was used in the formal runs.

The complete run manifest, binary/ROM/state hashes and limitations are in
[`formal-f2-macbook.md`](formal-f2-macbook.md).

Short functional captures also showed frequent two-millisecond bounded-wait
timeouts on this MoltenVK setup. That is recorded as a performance concern for
M1/M2 comparison, not hidden as a GOOGLE success. The shipping default remains
TelemetryOnly until controlled Formal evidence justifies a separate change.
