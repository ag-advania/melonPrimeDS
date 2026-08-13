# Physical Intel MacBook Vulkan gate — 2026-08-14

## Verdict

The current-SHA physical Intel macOS smoke gate is **PARTIAL**. The bundled
Vulkan runtime, Metal surface, real Intel GPU selection, presenter, and a real
ROM frame were reached with both the local MoltenVK 1.4.2 build and the pinned
MoltenVK 1.4.0 bundle. The fixed in-match F2 scene is now covered by the formal
M0/M1/M2 matrix, but the full physical-gate PASS is not claimed because the
single continuous 30-minute session, match-end/recap, reset-after-load and
window/lifecycle matrix were not run. Validation is **BLOCKED** because this
app directly opens the bundled MoltenVK library; installing the Khronos
validation layer and setting `VK_LAYER_PATH` did not make that layer visible to
the app.

The additional Japanese ROM trial reproduced a white Vulkan window and an
ARM9 data-abort finding in the unguarded build. The failure was isolated to
`FixWifi`: the detected JP1.0 group selected a 51-word patch even though the
ROM's target words did not match either the apply or restore signatures. The
guarded build now rejects that layout without writing ARM9 memory; with
`WifiBitset = true`, Vulkan reached its first frame and produced no ARM9 data
abort during the 30-second smoke. The same ROM also reached visible output
with the software renderer.

## Fixed capture

| Item | Value |
|---|---|
| source SHA | `8f4352921102ba163796b8aaba503454d639fb96` + post-start F2 diagnostic hook (this change) |
| branch | `develop_remakeVulkan_ver3` |
| host | MacBookPro15,2, macOS 15.7.7 (24G720), x86_64 |
| CPU / memory | Intel Core i5-8259U, 4 cores, 16 GB |
| GPU | Intel Iris Plus Graphics 655, Metal 3, internal Retina 2560×1600 |
| build | Release guard smoke plus final Developer-features-ON OSD build, Ninja, Metal + Vulkan, jobs 2 |
| current local runtime | MoltenVK 1.4.2, bundled in the release app |
| shipping-pin runtime | MoltenVK 1.4.0, bundled and ad-hoc signed for this smoke |
| primary ROM evidence | AMHP Rev.01, SHA-256 `4c0510ae0389f793bf95bd095d8ecd29868cd85f3b15f8f72999685e813790c9` |
| additional ROM finding | AMHJ Rev.00, SHA-256 `8116cff4964daa430c4c4039170ecd063348fc6f768636b9bc3a19a951306e02` |
| power | AC power during the formal F2 matrix; final inventory was battery power, discharging |

No serial number, ROM path, ROM binary, save-state binary, or screenshot is
part of the repository evidence. PNGs and `*.sav`/`*.nds` are ignored by the
repository rules. Raw runtime logs are also intentionally ignored because the
application writes absolute local paths into them; the checked-in evidence is
the redacted excerpt set below.

## Result table

| Gate | Result | Runtime / GPU | Notes |
|---|---|---|---|
| Build x86_64 | PASS | release + developer / Intel host | baseline 211/211; final developer build 206/206; CMake Vulkan and Metal gates enabled |
| Bundle/sign | PASS | local MoltenVK 1.4.2 | bundled dylib; x86_64 executable/dylib; deep strict signature verified |
| no-ROM startup | PASS | local 1.4.2 | instance, surface, device, swapchain, IMMEDIATE present, presenter ready |
| Vulkan presenter ready | PASS | Intel Iris Plus 655 via MoltenVK | `VK_EXT_metal_surface`; actual renderer Vulkan |
| ROM launch | PASS | AMHP / local 1.4.2 and pinned 1.4.0 | first frame presented; Vulkan renderer reached |
| visual Vulkan output | PASS | AMHP / Intel Iris Plus 655 | valid intro/game imagery observed in local ignored screenshots; parity not claimed |
| additional Japanese ROM | PASS (guarded) | AMHJ / local 1.4.2 | first frame presented with `WifiBitset=true`; no ARM9 data abort in 30 seconds |
| FixWifi signature/lifecycle guard | PASS | developer + release builds | full 51-word check only once per ROM state; reset/re-detect clears cache; mismatch skips all writes |
| FixWifi developer OSD | PASS (build) | developer features ON | emits `FixWifi Applied`, `FixWifi Rejected`, or `FixWifi Unsupported` once per state transition |
| match gameplay | PASS (fixed F2) | AMHJ / Intel Iris Plus 655 | formal 3× M0/M1/M2, 600 warm-up + >10k measured frames/run; see `formal-f2-macbook.md` |
| match-end recap | NOT RUN | — | no controlled match lifecycle |
| resize/fullscreen/minimize | NOT RUN | — | required matrix not completed |
| renderer switch | PASS (stress) | AMHJ / Intel Iris Plus 655 | 30/30 production transitions: Vulkan↔Software, Vulkan↔Metal, and Vulkan↔Metal Compute, five round trips each; visual handover not claimed |
| savestate/reset | PARTIAL | AMHJ / Intel Iris Plus 655 | matching F2 savestate load verified; reset-after-load not run |
| 30-minute stability | NOT RUN | AC available for follow-up | launch produced no runtime evidence; no 30-minute session was completed |
| pinned MoltenVK 1.4.0 | PASS (smoke) | Intel Iris Plus 655 | no-ROM and AMHP ROM launch/presenter checks passed; full matrix remains open |
| validation layer | BLOCKED | Debug / MoltenVK direct load | layer package installed, but app reported it was not installed/enabled |
| current GenericPresentTiming | PASS (baseline) | current source | no OFF A/B: required baseline presenter path reached; optional timing caps were absent |

## Runtime evidence

The sanitized, repository-safe evidence is:

- [`environment.txt`](environment.txt)
- [`platform-availability.txt`](platform-availability.txt)
- [`build-checks.txt`](build-checks.txt)
- [`runtime-excerpts.txt`](runtime-excerpts.txt)
- [`renderer-switch-stress.txt`](renderer-switch-stress.txt)
- [`fixwifi-verification.txt`](fixwifi-verification.txt)
- [`validation-excerpts.txt`](validation-excerpts.txt)
- [`MoltenVK-LICENSE.txt`](pinned-1.4.0/MoltenVK-LICENSE.txt)

The raw `.log` files remain on the test machine for forensic follow-up but
are ignored by `.gitignore` because they contain absolute ROM and home paths.
The generated PNGs and save files remain local and ignored as well.

## Generic present timing

The current source setting was left unchanged (`GenericPresentTiming = true`).
On the physical Intel/MoltenVK surface, `VK_KHR_present_id2` and
`VK_KHR_present_wait2` were exposed, while `VK_EXT_present_timing`, absolute
timing, relative timing, and `FIFO_LATEST_READY` were not. The runtime selected
the documented fail-soft `TelemetryOnly`/`GenericHost` path. Because the
baseline reached presenter readiness without a Vulkan failure, the conditional
GenericPresentTiming OFF A/B was not run.

## Follow-up required for a full PASS

Complete the same-ROM controlled match lifecycle, at least five minutes of
gameplay after the formal scene, visual handover, match end/recap, resize /
fullscreen / minimize, reset-after-load, and one continuous 30-minute AC
session. If the macOS direct-loader design is changed to make validation layers
observable, rerun the Debug validation matrix and record the enabled banner
plus VUID/DEVICE_LOST results.

The detailed fixed-scene result is [`formal-f2-macbook.md`](formal-f2-macbook.md).
