# MelonPrime Refactoring Unified Document

**Integrated sources:** `MelonPrime_REFACTORING_1.md` through `MelonPrime_REFACTORING_5.md`, former `MelonPrime_PERF_ROUND6.md`, Round 7–8, and Custom HUD refactoring / optimization

**Document type:** Current unified edition / canonical reference copy

**Canonical filename:** `.claude/rules/melonprime-refactoring.md`

**Final audit:** Verified against the current code as of 2026-04-15 (`src/frontend/qt_sdl`)

**Former unified document names:** `MelonPrime_REFACTORING_UNIFIED.md`, `MelonPrime_REFACTORING_UNIFIED_1_TO_5.md`, `MelonPrime_REFACTORING_UNIFIED_1_TO_5_aliases_v2.md`, `MelonPrime_REFACTORING_UNIFIED_1_TO_5_aliases_v3_audited.md`

**Positioning:** This is the current edition reflecting the contents of 1–5, support for former names, source-audit results, Round 6–8, and the splitting / acceleration of the Custom HUD system. The former `src/frontend/qt_sdl/MelonPrime_PERF_ROUND6.md` has already been merged into this document and removed. The former `src/frontend/qt_sdl/MelonPrime_REFACTORING.md` has been moved to `.claude/rules/`. A major priority is preserving reverse lookupability from source comments and work notes.

**Integration goal:** To consolidate, without omission and with duplication cleaned up, the knowledge scattered across 1–5, the optimizations already applied, correctness fixes, refactorings, input-latency optimizations, and rejected / reverted items into a single unified document.
**Role of this document:**
- 1 is the foundation of “existing knowledge + FIX + OPT A–Z5”
- 2 is **Refactoring Round 1 (R1)**
- 3 is **Refactoring Round 2 (R2)**
- 4 is **Round 4: proposals and adoption decisions for P-1–P-10**
- 5 is **Round 5: input-latency / frame-pacing optimizations for P-11–P-27**
- 6 is **Round 6: syscall reduction / hot-path improvements for P-33–P-37**
- 7 is **Round 7: frame-loop optimizations for P-38–P-41**
- 8 is **Round 8: hotkey / aim / register optimizations for P-42–P-44**
- 9 is **Custom HUD Refactor: unity-fragment split of render / on-screen editor / Screen integration**
- 10 is **Custom HUD Performance: dirty rect, config cache, screen-fragment GL skip**
- Overlapping items are merged, while preserving historically important cases that were once adopted and later replaced or reverted

---

## 1. Source mapping table

| Integrated source | Positioning | Main contents |
|---|---|---|
| `MelonPrime_REFACTORING_1.md` | Foundational knowledge / initial optimization | Threading model, FIX-1–3, OPT A–Z5, pipeline analysis, rejected items |
| `MelonPrime_REFACTORING_2.md` | R1 | Maintainability improvements, deduplication, SmallVkList, static→member, constexpr tables, UTF-8 fixes |
| `MelonPrime_REFACTORING_3.md` | R2 | Button-priority bug fix, fetch_add, setHotkeyVks pointer+count, LUT conversion, hasMask removal, complete heap elimination |
| `MelonPrime_REFACTORING_4.md` | Round 4 | Detailed analysis of P-1–P-10, organization of applied / rejected / reverted items |
| `MelonPrime_REFACTORING_5.md` | Round 5 | Input-latency optimization for P-11–P-27, frame-pacing improvements, final latency-oriented organization |
| Former `MelonPrime_PERF_ROUND6.md` | Round 6 | P-33–P-37: PrePollRawInput removal, SDL skipping, validation of DeferredDrain lightweight plan and P-35 revert, hot-path branch improvements. Already merged into this document |
| (This document §16) | Round 7 | P-38–P-41: hotkey gate inside the frame loop, NeedsShaderCompile cache, division→multiplication, DSi sync removal |
| (This document §17) | Round 8 | P-42–P-44: hotkey scan, RunFrameHook, aim zero-delta skip |
| (This document §18) | Custom HUD Refactor | Split into `MelonPrimeHudRender*.inc`, `MelonPrimeHudConfigOnScreen*.inc`, and `MelonPrimeHudScreenCpp*.inc` |
| (This document §19) | Custom HUD Perf | OPT-DR1 / OPT-SC1: high-performance HUD dirty rect and screen overlay GL/software paths |

---

## 2. Normalization rules

### 2.1 Unification of round names

- `MelonPrime_REFACTORING_2.md` is treated as **R1**
- `MelonPrime_REFACTORING_3.md` is treated as **R2**
- `MelonPrime_REFACTORING_4.md` is treated as **Round 4**
- `MelonPrime_REFACTORING_5.md` is treated as **Round 5**
- Former `MelonPrime_PERF_ROUND6.md` has already been merged into §15 of this document as **Round 6**
- (This document §16) is treated as **Round 7**
- (This document §17) is treated as **Round 8**
- (This document §18) is treated as **Custom HUD Refactor**
- (This document §19) is treated as **Custom HUD Perf**

### 2.2 Unification of duplicated IDs

`MelonPrime_REFACTORING_5.md` contains duplicate descriptions under the same IDs, so this unified document normalizes them as follows.

| Original notation | Handling in this document |
|---|---|
| P-16 appears twice | Merged into **P-16: VSync restore fix after FastForward/SlowMo** |
| P-17 appears multiple times | Merged into **P-17: subpixel residual accumulation** |
| P-26 appears in two forms | Split into **P-26a: Auto Screen Layout bypass** and **P-26b: DeferredDrain throttle** |
| P-14 treated as already applied / replaced by P-19 and P-20 | **P-14 is treated as a historical stage**, ultimately absorbed into P-19 and P-20 |

### 2.3 Handling of history

- **Applied:** Treated as the current implementation endpoint
- **Deferred / Rejected:** Preserved as evaluation results
- **Reverted:** Preserved as something that was tried once and later rolled back
- **Historical stage:** Treated separately as a stepping stone toward later optimizations

### 2.4 Rules for preserving former names / aliases

This unified edition emphasizes not only the unified names but also the ability to trace original section names from the source documents. In particular, even when older names appear in source comments or work notes, they should still be reverse searchable, so they are preserved under the following rules.

- For items whose headings were unified, **former name / alias / original heading** is retained in the body
- For IDs that were duplicated in Round 5, both the **normalized name + former wording** are shown together
- When an old name appears in source comments, first consult this section and the **Former name / original heading** column of each status table

### 2.5 Quick reference: normalized names ↔ former names

| Normalized name | Former name / alias / original heading |
|---|---|
| P-14: PrePoll batched drain | PrePollRawInput, PrePoll batched drain (Stuck Keys fix) |
| P-15 + P-21: separation of joystick state updates | Late-Poll joystick, `inputRefreshJoystickState()`, `inputProcess` split |
| P-16: VSync restore fix | VSync setting restoration, VSync restore bug fix |
| P-17: subpixel residual accumulation | Subpixel accumulator, subpixel aim accumulation, subpixel residual accumulator |
| P-18: Dual-Path aim pipeline | Direct Path / Legacy Path, P-18c residual clamp |
| P-19: immediate `processRawInput` in HiddenWndProc | Root stuck-keys fix, HiddenWndProc processRawInput |
| P-20: removal of PrePollRawInput | P-20b `InputReset` bug fix, P-20c P-3 cache application omission fix |
| P-22: DeferredDrain split | `DeferredDrainInput()` call addition, drain split out of PollAndSnapshot |
| P-24: outer-loop unified early exit for hotkeys | Outer-loop hotkey unified early exit |
| P-26a: Auto Screen Layout bypass | Auto Screen Layout bypass |
| P-26b: DeferredDrain throttle | DeferredDrain throttle |

---

### 2.6 Policy for reflecting source audits

In this edition, not only the original documents but also the current code are audited as information sources. When discrepancies exist, they are handled with the following priority.

1. **Current code**
   Actual functions, call sites, and comments take top priority.
2. **Original Round 5 document**
   Treated as a primary source showing the intent at proposal time and adoption time.
3. **Existing UNIFIED / older versions of this unified document**
   Used as organized references, but corrected if they contradict the code.

In particular, the following items should be read by separating the **document-side endpoint** from the **current-code residue / later changes**.

| Item | Audit result |
|---|---|
| P-14 / `PrePollRawInput` | In Round 5 it was absorbed into P-19 / P-20. **Completed in Round 6 (P-33) by empty-inlining and removing the calls** |
| P-20 / removal of PrePollRawInput | **Completed in Round 6 (P-33)**. PrePollRawInput was effectively removed as designed |
| P-22 / DeferredDrain split | The split itself remains valid. Placement is after `drawScreen()`. **P-35 has been reverted — `drainPendingMessages` is retained as a shared-buffer safety net** |
| P-26b / DeferredDrain throttle | **Already withdrawn / not adopted in current code**. No 8-frame throttle counter exists, and the old throttle comment has been removed |
| P-32 | Outside the 1–5 range, but still needed as a **later change in the current code that explains DeferredDrain’s final placement** |

## 3. Architecture overview

## 3.1 Target modules

- Raw Input layer: `MelonPrimeRawInputState` / `MelonPrimeRawInputWinFilter`
- Game-logic layer: `MelonPrime.h` / `MelonPrime.cpp` / `MelonPrimeInGame.cpp` / `MelonPrimeGameInput.cpp` / `MelonPrimeGameWeapon.cpp`
- ROM-detection layer: `MelonPrimeGameRomDetect.cpp`

## 3.2 Threading model

MelonPrime’s Raw Input layer has two modes, and in both cases **there is always exactly one writer thread**.

| Mode | Writer | Reader | Input path |
|---|---|---|---|
| Joy2Key ON | Qt main thread | Emu thread | `nativeEventFilter` → `processRawInput` |
| Joy2Key OFF | Emu thread | Emu thread | `PollAndSnapshot()` / `LateLatchMouseDelta()` / `DeferredDrain()` → `processRawInputBatched` + hidden-window dispatch |

This **Single-Writer guarantee** is the basis for the atomic optimizations.
More concretely, it is what allows the design to avoid CAS loops and locked RMW operations, and instead use `relaxed load + release store`.

## 3.3 Structural differences between Joy2Key ON / OFF

| Item | ON (Joy2Key) | OFF (Joy2Key Off) |
|---|---|---|
| Writer thread | Qt main thread | Emu thread |
| Input path | `nativeEventFilter` → `processRawInput(HRAWINPUT)` | `PollAndSnapshot()` / `LateLatchMouseDelta()` / `DeferredDrain()` → `GetRawInputBuffer` + hidden-window dispatch |
| WM_INPUT destination | Qt window | Hidden window (`RIDEV_INPUTSINK`) |
| Event retrieval | `GetRawInputData` one by one | Batched with `GetRawInputBuffer` |
| Read timing | Immediate on arrival | Snapshot at frame start, late latch before aim, drain after rendering |
| When focus is lost | WM_INPUT itself stops arriving | Reception continues via `RIDEV_INPUTSINK` |
| DefWindowProc resilience | Safe because data is consumed first | Risk of data consumption by `DefWindowProc` |

`Poll()` remains as a compatibility / legacy path, but the main path for current per-frame input is `PollAndSnapshot()` + `LateLatchMouseDelta()` + `DeferredDrain()`.

**Why stuck keys are less likely when ON**
1. `nativeEventFilter` performs `GetRawInputData` before `DispatchMessage`
2. When focus is lost, WM_INPUT no longer reaches the Qt side

---

## 4. Known bugs and fixes

## 4.1 FIX-1: WM_INPUT data loss in HiddenWndProc (root cause of stuck keys)

**Phenomenon**
When Joy2Key is OFF, keys or clicks can remain stuck as if held down.

**Cause**
If the hidden window’s `WndProc` passes WM_INPUT to `DefWindowProcW`, it internally performs `GetRawInputData`, consuming the raw-input buffer.
As a result, the key-up event that should have been retrieved later by `GetRawInputBuffer` is lost, causing a stuck key.

**Important behavior**
1. `PeekMessage(PM_REMOVE)` removes WM_INPUT from the queue
2. At that point it becomes invisible to `GetRawInputBuffer`
3. The `HRAWINPUT` in `lParam` can still be read via `GetRawInputData`
4. `DefWindowProcW(WM_INPUT)` consumes it internally

**Fix**
- As soon as `HiddenWndProc` receives WM_INPUT, call `processRawInput(reinterpret_cast<HRAWINPUT>(lParam))`
- Do not pass it to `DefWindowProcW`; instead `return 0`
- Prevent loss via a dual path with `Poll()` / `drainPendingMessages()`

**Key point**
A plain `return 0` is not enough; **it is necessary to explicitly perform the equivalent of `GetRawInputData` inside the WndProc and rescue the event there**.

## 4.2 FIX-2: stale input in `UpdateInputState()` while `!isFocused`

**Phenomenon**
During focus loss, stale `m_input.down` can remain, and the wrong key mask may be passed to the NDS on the reentry path.

**Fix**
When `!isFocused`, zero-clear the following and return:
- `m_input.down`
- `m_input.press`
- `m_input.moveIndex`
- `m_input.mouseX`
- `m_input.mouseY`
- `m_input.wheelDelta`

## 4.3 FIX-3: raw-input reset on focus transition

**Phenomenon**
Stale keys may remain between focus loss and focus regain.

**Fix**
When a change in `BIT_LAST_FOCUSED` is detected, run the following on the focus-lost side:
- Clear `m_input.down/press/moveIndex`
- `m_rawFilter->resetAllKeys()`
- `m_rawFilter->resetMouseButtons()`

**Relationship between FIX-2 and FIX-3**
- FIX-2: immediately blocks stale state on the hot path every frame
- FIX-3: performs a comprehensive clear at the raw-input layer as part of the focus-transition event

## 4.4 The reset in OnEmuUnpause is already sufficient

At first glance, it may seem desirable to add `resetAllKeys + resetMouseButtons` to `OnEmuUnpause()`, but in practice `ApplyJoy2KeySupportAndQtFilter(enable, doReset=true)` is already called at the start, and it has already performed:
- `resetAllKeys`
- `resetMouseButtons`
- `resetHotkeyEdges`

Therefore, any additional reset would be redundant. What is needed is only resynchronization of:
1. `BindMetroidHotkeysFromConfig()`
2. `resetHotkeyEdges()`

## 4.5 R2: `processRawInputBatched` button-priority bug fix (former name: inconsistency in mouse-button priority)

**Problem**
Single-event processing used `UP wins`, while batched processing used `DOWN wins`.

**Before**
```cpp
(finalBtnState & ~lut.upBits) | lut.downBits
```

**After**
```cpp
(finalBtnState | lut.downBits) & ~lut.upBits
```

When DOWN and UP are both set in the same combined message, the correct final state is “released”, so behavior was unified under `UP wins`.

## 4.6 P-1: memory-ordering inconsistency in `snapshotInputFrame` (same former name)

**Problem**
`m_accumMouseX/Y.load(relaxed)` was placed before the acquire fence inside `takeSnapshot()`.
On x86 TSO this is unlikely to become a practical issue, but on ARM / RISC-V, load-load reordering could break consistency between the VK snapshot and mouse delta.

**Fix policy**
- Read `m_accumMouseX/Y` after `takeSnapshot()`
or
- Introduce an integrated snapshot such as `takeFullSnapshot()`

In Round 4, **Option A: read the mouse after `takeSnapshot()`** was recommended as the minimal change.

## 4.7 P-20b: residual-destruction bug caused by `InputReset()` (former name: InputReset bug fix)

**Problem**
Because `InputReset()` executed `m_aimResidualX/Y = 0` every frame, the subpixel accumulation of P-17 and the Dual-Path behavior of P-18 were effectively disabled.

**Fix**
- Remove the residual reset from `InputReset()`
- Limit residual clears to explicit reset cases only, such as sensitivity changes, layout changes, or aim blocking

## 4.8 P-20c: fix for missing application of the P-3 cache (same former name)

Although P-3 introduced `m_cachedPanel`, `UpdateInputState()` was still traversing `emuInstance->getMainWindow()->panel`.
By replacing it with `m_cachedPanel ? m_cachedPanel->getDelta() : 0`, the intent of P-3 was correctly reflected in the hot path.

## 4.9 P-22 fix: missing `DeferredDrainInput()` call (former name: DeferredDrainInput call addition)

P-22 split `drainPendingMessages()` out of `PollAndSnapshot`, but `frameAdvanceOnce()` in `EmuThread.cpp` did not yet call `DeferredDrainInput()`.
After this was added, later changes moved its placement to after `drawScreen()`. The throttle proposal from P-26b was ultimately not adopted.

---

## 5. Foundational optimizations (OPT A–Z5)

## 5.1 Overall list

| OPT | Target | Reduction / effect | Summary |
|---|---|---|---|
| A | Pre-fetch of wheelDelta + weapon gate | ~18–28 cyc/frame | Fetch `wheelDelta` first and skip processing in 99%+ of frames with no weapon input |
| B | Boost bit guard | ~2–4 cyc/frame | Early bit check for Morph Ball boost conditions |
| C | Class layout optimization | ~0–10 cyc/frame | Better placement of hot members |
| D | `m_isInGame` → `BIT_IN_GAME` | ~1 cyc/frame | Merge bool into flags |
| E | Batching the reentry path | ~2 cyc/reentry | Organize duplicated reentry processing |
| F | Integer-threshold skip | ~15–25 cyc/frame | Exclude unnecessary aim computation at low sensitivity |
| G | `m_isAimDisabled` → `aimBlockBits` | ~1–2 cyc/frame | Bit-pack block management for multiple conditions |
| H | Prefetch of aim write targets | ~0–10 cyc/frame | Warm up the write targets for aimX / aimY |
| I | Remove per-frame `setRawInputTarget` | ~7–10 cyc/frame | Eliminate redundant HWND updates |
| J | NDS pointer cache | ~3–5 cyc/frame | Reduce repeated chasing of `emuInstance->getNDS()` etc. |
| K | `BIT_LAST_FOCUSED` change guard | ~1–2 cyc/frame | Reduce unnecessary focus-change detection |
| L | Promote inGame pointers to HotPointers | ~4–10 cyc/frame | Improve cache locality of frequently used addresses |
| M | Single-load of `m_rawFilter` | ~4–6 cyc/frame | Reduce rereads of the pointer within the same frame |
| N | Remove dead code for `BIT_LAYOUT_PENDING` | Quality improvement | Remove unnecessary code |
| O | Fixed-point aim pipeline | ~14 cyc/frame | Replace float-based path with Q14 fixed point |
| P | Fused AimAdjust | Integrated into O | Absorbed into O instead of remaining an independent item |
| Q | Remove redundant zero checks | ~1–2 cyc/frame | Organize duplicate zero tests |
| R | Remove redundant safety-net rereads | ~500–1000 cyc/frame | Organize redundant rereads after `PollAndSnapshot`, etc. However, `drainPendingMessages()` inside `DeferredDrain` remains due to the P-35 revert |
| S | Fuse `pollHotkeys + fetchMouseDelta` | ~8–15 cyc/frame | Retrieve hotkey + mouse delta in one snapshot |
| T | Remove `hasMouseDelta / hasButtonChanges` | ~0–133 cyc/frame | Reduce unnecessary flags |
| U | Cache `m_state` inside `Poll()` | ~2–3 cyc/frame | Reduce indirect references |
| W | Hoist `BIT_IN_GAME_INIT` block out | icache ~300–400 bytes | Move cold initialization out of the hot path |
| Z1 | Deferred acquisition of mainRAM | ~6–10 cyc/frame | Acquire only when needed |
| Z2 | Merge `ProcessMoveAndButtonsFast` | ~3–5 cyc/frame | Unify move / buttons into one path |
| Z3 | Merge `PollAndSnapshot` | ~8–12 cyc/frame | Fuse Poll + snapshot |
| Z4 | Inline `HandleGlobalHotkeys` | ~5 cyc/frame | Remove call / return |
| Z5 | Early prefetch of aimX/aimY | 0–40 cyc | Hide L2 misses probabilistically |

## 5.2 Main optimizations in the Raw Input layer

### Removal of CAS loops / locked RMW

With Single-Writer as the premise:
- `fetch_or / fetch_and / compare_exchange`
- `lock xadd`

and similar locked instructions were avoided, and the implementation was optimized toward `relaxed load + release store`.

Main application points:
- `setVkBit`
- Mouse coordinates in `processRawInput`
- Mouse buttons in `processRawInput`
- VK / mouse / delta commit phase in `processRawInputBatched`

### Fence aggregation in `pollHotkeys`

Multiple acquire loads were aggregated into:
- a group of relaxed loads
- one `atomic_thread_fence(acquire)`

thereby reducing ordering cost, especially on ARM / RISC-V.

### OPT-R: removal of redundant safety-net rereads

Rereads with duplicate meaning within the same frame, such as after `PollAndSnapshot`, were cleaned up.
However, in the current code’s `DeferredDrain()`, `processRawInputBatched()` inside `drainPendingMessages()` is essential as a shared-buffer safety net.
An attempt was made in P-35 to remove this from `DeferredDrain`, but it was reverted because stuck keys reappeared.

### OPT-S: fusion of hotkey + mouse delta via `snapshotInputFrame()`

`pollHotkeys()` and `fetchMouseDelta()` were combined into one API, reducing the number of loads, fences, and calls.

### OPT-T / U

- Auxiliary flags such as `hasMouseDelta / hasButtonChanges` were eliminated
- `m_state` is read only once inside `Poll()`

## 5.3 Main optimizations in the game-logic layer

- Fetch `wheelDelta` first and gate the weapon-input path
- Unify block state via `aimBlockBits`
- Convert to Q14 fixed point
- Aggregate frequently used RAM addresses via `HotPointers`
- Organize store / load cycles in `ProcessMoveAndButtonsFast`
- Inline `HandleGlobalHotkeys`
- Defer acquisition of `mainRAM`

---

## 6. R1: maintainability / code-quality refactoring

| Item | Change | Effect |
|---|---|---|
| R1-1 | Add `MelonPrimeCompilerHints.h` | Centralize `FORCE_INLINE` / `LIKELY` / `PREFETCH_*` / `HOT/COLD_FUNCTION` / `NOINLINE`, remove ODR risk |
| R1-2 | Extract `takeSnapshot()` + `scanBoundHotkeys()` | Remove duplication across `pollHotkeys` / `snapshotInputFrame` / `resetHotkeyEdges` / `hotkeyDown` |
| R1-3 | Extract `drainPendingMessages()` | Remove duplicated drain logic in `Poll()` and `PollAndSnapshot()` |
| R1-4 | Introduce `SmallVkList` | Reduce heap allocations in the hotkey-binding path |
| R1-5 | Batch config acquisition in `BindMetroidHotkeysFromConfig()` | Reduce 28 table fetches to 1 |
| R1-6 | `static bool s_isInstalled` → `m_isNativeFilterInstalled` | Improve multi-instance safety and testability |
| R1-7 | `TOUCH_IF_PRESSED` macro → constexpr table | Improve type safety and debuggability |
| R1-8 | Fix garbled UTF-8 comments | Remove remnants such as `â†'`, `Ã—`, etc. |

**Overall assessment**
R1 is the round that made future modifications and verification easier without changing hot-path performance.

---

## 7. R2: correctness / complete heap elimination

| Item | Change | Effect |
|---|---|---|
| R2-1 | Unify button priority in `processRawInputBatched` | Match the semantics of single-event and batched processing |
| R2-2 | Convert `processRawInput` to `fetch_add` | Reduce code size and improve semantic correctness |
| R2-3 | Add `setHotkeyVks(int, const UINT*, size_t)` | Completely eliminate the `std::vector<UINT>` bridge |
| R2-4 | Connect `MapQtKeyIntToVks()` directly to `SmallVkList` | Zero heap allocations in the hotkey-binding path |
| R2-5 | Convert mouse buttons to LUTs | Replace switch chains with table lookup |
| R2-6 | Remove `hasMask[64]` | Reduce 64B and merge into `m_boundHotkeys` |
| R2-7 | NDS cache in `MelonPrimeGameWeapon.cpp` | Improve consistency and the cold path |
| R2-8 | Fix remaining garbled UTF-8 | Unify comment quality |

**Overall assessment**
The essence of R2 is not minor hot-path improvement, but rather **one correctness bug fix** and **completion of heap elimination**.

---

## 8. Round 4: integrated results for P-1–P-10 (with former names)

## 8.1 Status list

| ID | Category | Status | Conclusion | Former name / original heading |
|---|---|---|---|---|
| P-1 | Correctness | ✅ Applied | Fix memory ordering in `snapshotInputFrame` | `snapshotInputFrame` memory-ordering inconsistency |
| P-2 | Performance | ❌ Reverted | Removing double prefetch hurt performance | Removal of double prefetch in `HandleInGameLogic` / `ProcessAimInputMouse` |
| P-3 | Performance | ✅ Applied | Reduce panel pointer chain via `m_cachedPanel` | `UpdateInputState` panel pointer-chain optimization |
| P-4 | Performance | ❌ Deferred | Double management of the reentry path is too complex | Reentry-path lightweighting |
| P-5 | Code quality | ✅ Applied | Suppress inlining of cold code on MSVC as well | MSVC `COLD_FUNCTION` improvement |
| P-6 | Performance | ✅ Applied | Improve type-branch hints in `processRawInputBatched` | Event-loop type-branch optimization in `processRawInputBatched` |
| P-7 | Performance | ❌ Unnecessary | Early exit already exists | `HandleGlobalHotkeys` condition optimization |
| P-8 | Performance | ❌ Deferred | Repacking `FrameInputState` is too risky | Use of `FrameInputState` padding |
| P-9 | Performance / cleanup | ✅ Applied | Consolidate reset APIs and reorganize the focus-transition side | Merge of `UNLIKELY` focus-transition branches in `RunFrameHook` |
| P-10 | Performance | ❌ Rejected | Removing `hasKeyChanges` is counterproductive | Removal of the `hasKeyChanges` branch for keyboard deltas in `processRawInputBatched` |

## 8.2 Key points of the applied items

### P-1 (former name: `snapshotInputFrame` memory-ordering inconsistency)
- On x86, actual impact is hard to observe
- The main goal is preventing inconsistency on ARM / RISC-V
- A minimal-change fix is recommended

### P-3 (former name: `UpdateInputState` panel pointer-chain optimization)
- Shortens the three-step pointer chase of `emuInstance->getMainWindow()->panel->getDelta()` to `m_cachedPanel->getDelta()`
- Updating it in `OnEmuStart()` and `NotifyLayoutChange()` is sufficient

### P-5 (former name: MSVC `COLD_FUNCTION` improvement)
- Improve `COLD_FUNCTION` so that it pushes code toward the cold side on MSVC as well as GCC / Clang

### P-6 (former name: event-loop type-branch optimization in `processRawInputBatched`)
- Provide branch hints in `processRawInputBatched()` that match the actual input-type ratios

### P-9 (former name: merge of `UNLIKELY` focus-transition branches in `RunFrameHook`)
Round 4 uses the wording “merge of `UNLIKELY` focus-transition branches in RunFrameHook”, but once integrated, the essence is the following.

- Organizing reset-related APIs
- Fence unification through `resetAll()`
- Moving focus-transition handling onto the cold path
- Improving consistency with existing FIX items

## 8.3 Key points of non-adopted / rejected items

### P-2: removal of double prefetch was reverted (former name: removal of double prefetch in `HandleInGameLogic` / `ProcessAimInputMouse`)
Even if it appears redundant, the near-distance prefetch on the `ProcessAimInputMouse()` side acts as an “insurance policy”, and deleting it was counterproductive considering the MainRAM working set.

### P-4: reentry-path lightweighting deferred (same former name)
Reentry is rare, and `FrameAdvanceTwice()` is dominant. The bug-injection risk from a double implementation is larger than a 10–20 cyc reduction.

### P-7: unnecessary (former name: `HandleGlobalHotkeys` condition optimization)
`HandleGlobalHotkeys` already has an early exit, so there is almost no further optimization headroom.

### P-8: deferred (former name: use of `FrameInputState` padding)
Although reusing `_pad` in `FrameInputState` is theoretically possible, the API impact and readability loss are too large.

### P-10: rejected (former name: removal of the `hasKeyChanges` branch for keyboard deltas in `processRawInputBatched`)
Since most frames do not have keyboard input, keeping the `hasKeyChanges` guard is the more rational choice.

### Other rejected items
- Manual unrolling of `processRawInputBatched`
- Additional prefetching of `mainRAM`
- Faster `Config::Table` lookup
- Re-optimization of `UnrollCheckDown/Press` in `UpdateInputState`

---

## 9. Round 5: integrated results for P-11–P-27 (with former names)

## 9.1 Central theme of Round 5

The primary purpose of Round 5 is not CPU-cycle reduction itself, but the following:

1. **Increase the freshness of input acquisition**
2. **Reduce frame-timing jitter**
3. **Suppress interference between Raw Input and the SDL message pump**
4. **Eliminate truncation loss in aim output**
5. **Reduce the steady-state cost of syscalls / mutexes / virtual dispatch**

## 9.2 Status list (normalized edition)

| ID | Category | Status | Handling after integration | Former name / original heading |
|---|---|---|---|---|
| P-11 | Timer resolution | ✅ | Introduce `NtSetTimerResolution` | Windows timer resolution (`NtSetTimerResolution`) |
| P-12 | Frame limiter | ✅ | Hybrid Sleep + Spin | Precise hybrid Sleep+Spin frame limiter |
| P-13 | Frame order | ✅ | Late-Poll architecture | Late-Poll frame architecture |
| P-14 | Historical stage | ⚠→✅ Empty-inlined in P-33 | PrePoll batched drain. In Round 5, this flow was absorbed into P-19 / P-20. Completed in Round 6 (P-33) by making `PrePollRawInput()` empty-inline | PrePoll batched drain (Stuck Keys fix), PrePollRawInput |
| P-15 | Joystick freshness | ✅ | Introduce `inputRefreshJoystickState()` | Late-Poll joystick |
| P-16 | VSync restore fix | ✅ | Duplicate descriptions unified | VSync setting restoration, VSync restore bug fix |
| P-17 | Subpixel residual accumulation | ✅ | Duplicate descriptions unified | Subpixel accumulator, subpixel aim accumulation, subpixel residual accumulator |
| P-18 | Dual-Path aim | ✅ | Two paths: Direct path / Legacy path | Dual-Path aim pipeline, P-18c residual clamp |
| P-19 | Root stuck-keys fix | ✅ | Immediate `processRawInput` in HiddenWndProc | Root stuck-keys fix — HiddenWndProc processRawInput |
| P-20 | Removal of PrePollRawInput | ⚠→✅ Completed in P-33 | As designed in Round 5, it became unnecessary because of P-19. Round 6 (P-33) removed the calls and replaced it with an empty-inline stub | Removal of PrePollRawInput |
| P-20b | Residual-destruction bug fix | ✅ | Remove residual reset from `InputReset()` | InputReset bug fix |
| P-20c | Fix for missing application of P-3 | ✅ | Reflect `m_cachedPanel` on the hot path | P-3 cache application omission fix |
| P-21 | Split `inputProcess` | ✅ | Separate edge detection from state repolling | `inputProcess` split |
| P-22 | DeferredDrain split | ✅ | Split drain out of PollAndSnapshot. P-35 has been reverted, and `drainPendingMessages` is retained | DeferredDrainInput call addition |
| P-23 | Fast path for no joystick | ✅ | Reduce SDL overhead when no joystick is connected | Fast path when joystick is absent |
| P-24 | Early exit for outer-loop hotkeys | ✅ | Avoid 7 checks when `hotkeyPress==0` | Outer-loop hotkey unified early exit |
| P-25 | Save-flush throttling | ✅ | Perform flush check every 30 frames | Save-flush throttling |
| P-26a | Auto Screen Layout bypass | ✅ | Exclude unnecessary automatic layout processing in MelonPrime | Auto Screen Layout bypass |
| P-26b | DeferredDrain throttle | ❌ Withdrawn | The “drain once every 8 frames” plan was only a temporary document-side proposal. Current code runs `DeferredDrain()` every frame, and has neither the 8-frame counter nor the old throttle comment | DeferredDrain throttle |
| P-27 | Integer spin comparison | ✅ | Remove float multiplication | Integer spin comparison |

## 9.3 P-11: Windows timer resolution (former name: `NtSetTimerResolution`)

- Integrate `NtSetTimerResolution(5000)` into `WinInternal`
- Target 0.5ms resolution
- Fall back to `timeBeginPeriod(1)` on failure

**Effect**
- Greatly alleviates the problem where `SDL_Delay(1)` can stick at up to 15.6ms
- Allows the spin margin in P-12 to be reduced from 1.5ms → 1.0ms
- Improves wasted CPU spin time by about 33%

## 9.4 P-12: precise hybrid Sleep + Spin (former name: precise hybrid Sleep+Spin frame limiter)

Old:
- `SDL_Delay(round(ms))`
- Frame time fluctuated into the 15–32ms range

New:
1. Perform the coarse wait with `SDL_Delay`
2. Fill the remaining time with a QPC-based spin

**Effect**
- Improves jitter from roughly ±15ms down to roughly ±0.03ms
- However, CPU usage increases due to up to ~1ms of spin waiting

## 9.5 P-13: Late-Poll frame architecture (same former name)

Old:
```text
Poll → RunFrame → Render → Sleep
```

New:
```text
Sleep → Poll → RunFrame → Render
```

This makes it possible for input that arrives during Sleep to be reflected **in the immediately following RunFrame, rather than in the next frame**.

## 9.6 P-14: PrePoll batched drain / PrePollRawInput (historical stage; current code has only an empty inline)

In P-14, the strategy was to rescue WM_INPUT with `GetRawInputBuffer` before SDL’s `PeekMessage` could dispatch it. Places labeled **PrePollRawInput** in old comments correspond to this item.

```text
PrePollRawInput
→ inputProcess
→ Sleep
→ PollAndSnapshot
```

However, this method has the following limitations:
- A race between `PrePollRawInput` and `inputProcess`
- Poor interaction with the increased number of SDL joystick updates introduced by P-15
- Increased syscall count

**Document-side endpoint in Round 5**
- P-19 makes the HiddenWndProc side run `processRawInput` immediately
- P-20 removes PrePollRawInput itself

**However, according to the current-code audit (Round 6 update)**
- In Round 6 (P-33), `PrePollRawInput()` was converted into an empty inline
- The calls from `EmuThread.cpp` were also removed
- Therefore, **P-14 became fully historical due to P-33**

## 9.7 P-15 + P-21: separation of joystick state updates (former names: Late-Poll joystick / `inputRefreshJoystickState()` / split of `inputProcess`)

Problem:
- `inputProcess()` performs both edge detection and state updates at the same time
- Calling `inputProcess()` again after Sleep breaks `lastHotkeyMask` and can cause double firing

Solution:
- On the main-loop side, execute `inputProcess()` only once to finalize edge detection
- After Sleep, use `inputRefreshJoystickState()` to update **only the state**

In the original Round 5 document, this was split across **P-15: Late-Poll joystick** and **P-21: `inputProcess` split**.

```cpp
void EmuInstance::inputRefreshJoystickState()
{
    SDL_JoystickUpdate();
    inputMask = keyInputMask & joyInputMask;
    hotkeyMask = keyHotkeyMask | joyHotkeyMask;
    // lastHotkeyMask / hotkeyPress / hotkeyRelease are not updated
}
```

## 9.8 P-16: VSync restore fix (former names: VSync setting restoration / VSync restore bug fix)

If VSync is unconditionally restored to `true` when entering or leaving FastForward / SlowMo, user settings are broken. Places in source comments labeled **VSync setting restoration** or **VSync restore bug fix** refer to this item.

After the fix, the following are handled correctly:
- Current `Screen.VSync` setting
- `Screen.VSyncInterval`
- Entry / exit of FastForward / SlowMo

The behavior was changed from **“always restore to true on exit”** to **“restore to the original setting”**.

## 9.9 P-17: subpixel residual accumulation (former names: subpixel accumulator / subpixel aim accumulation / subpixel residual accumulator)

At low sensitivity, `delta * scale` was often quantized to zero, causing fine mouse movement to disappear. Whether comments call it **subpixel accumulator**, **subpixel aim accumulation**, or **subpixel residual accumulator**, they all refer to the same family of improvements.

Fix:
- Store residuals in Q14 fixed point
- Subtract the emitted integer portion with `residual -= out << 14`
- Carry the fractional part over to the next frame

**Effect**
- Improves aim stepping at low speed / low sensitivity
- Eliminates truncation loss
- Residuals are explicitly reset only on sensitivity changes, layout changes, aim blocking, and similar events

## 9.10 P-18: Dual-Path aim pipeline (same former name)

### Direct Path
- Used when the ASM patch is enabled
- `residual >> 12` provides 4× resolution
- Avoids dead zones and outputs immediately every frame

### Legacy Path
- Conventional compatibility path
- Passes through `apply_aim()` and preserves dead zones and snapping

### Residual clamp
A clamp was also introduced as P-18c to prevent residual runaway.

## 9.11 P-19: immediate `processRawInput` in HiddenWndProc (former names: root stuck-keys fix / HiddenWndProc processRawInput)

Instead of P-14’s idea of “rescue it from the buffer before SDL dispatches it”, the design moved to **reading out the `HRAWINPUT` in `lParam` at the instant it is dispatched**.

The former heading **Root stuck-keys fix — HiddenWndProc processRawInput** corresponds to this section.

This makes the following true:
- Safe no matter when SDL calls `PeekMessage`
- Captured before DefWindowProc can consume it
- Eliminates the root cause of stuck keys

## 9.12 P-20: removal of PrePollRawInput (related former names: P-20b `InputReset` bug fix / P-20c P-3 cache application omission fix)

Because P-19 removed the SDL dispatch threat, **from the design perspective of the Round 5 document**, PrePollRawInput became unnecessary.

**Design-side effects**
- Fewer `GetRawInputBuffer` calls
- Fewer PeekMessage / drain-related syscalls
- A simpler input path

**Audit note (Round 6 update)**
- In Round 6 (P-33), the implementation of `PrePollRawInput()` was removed, and its calls were also removed
- An empty inline remains in the header for source compatibility
- **The design endpoint of P-20 was completed by P-33**

## 9.13 P-22: DeferredDrain split (former name: `DeferredDrainInput()` call addition)

`drainPendingMessages()` was split out of `PollAndSnapshot()`, and places in old comments labeled **DeferredDrainInput call addition** are also included here. It separates:
- The critical path of input reflection
- The non-critical path of message cleanup

**Important audit note**
- In the original Round 5 text, `DeferredDrainInput()` was placed **immediately after RunFrame**
- However, in the current code it was moved by later changes to **after `drawScreen()`**
- Therefore, the essence of P-22 is the **split itself**, and the description of its final placement should be understood as having been overwritten by later changes outside the 1–5 range

## 9.14 P-23: fast path when no joystick is connected (same former name)

Even for KB+M players, every frame was executing:
- `SDL_LockMutex`
- `SDL_JoystickUpdate`
- `SDL_UnlockMutex`

When no joystick is connected, a new connection is checked only once every 60 frames, and everything else is skipped completely. In the current code, both `inputProcess()` and `inputRefreshJoystickState()` each have a throttled check, avoiding SDL overhead in both the outer loop and the frame hot path during normal frames.

## 9.15 P-24: outer-loop unified early exit for hotkeys (former name: outer-loop hotkey unified early exit)

Calling `hotkeyPressed()` for seven hotkeys every frame is wasteful.
Since `hotkeyPress == 0` in almost every frame, the logic is aggregated as follows:

```cpp
if (UNLIKELY(emuInstance->hotkeyPress)) {
    // 7 checks
}
```

## 9.16 P-25: save-flush throttling (same former name)

Three `CheckFlush()` calls per frame are excessive.
They are throttled to once every 30 frames (about 0.5 seconds).

## 9.17 P-26a: Auto Screen Layout bypass (same former name)

MelonPrime manages screen layout independently, so melonDS-side auto screen sizing is unnecessary.
Therefore, per-frame `PowerControl9` reads and array updates are excluded under compile-time conditions.

## 9.18 P-26b: DeferredDrain throttle (same former name; treated as withdrawn by audit)

Running `DeferredDrain()` every frame accumulates PeekMessage syscalls in 8kHz mouse environments.
A temporary proposal therefore suggested draining only once every 8 frames.

**However, the audit result is as follows**
- The current code’s `DeferredDrain()` runs every frame and has no 8-frame counter
- The old comments of the `With P-26 throttle (every 8 frames)` type have already been removed
- Therefore, this item is treated as **not adopted / withdrawn in the current implementation**

**Estimated values at the time of the proposal**
- 8kHz / 60fps ≒ 133 WM_INPUT / frame
- Even after accumulating for 8 frames, that is only about ~1064 messages
- Still comfortably below the Windows queue limit of 10,000

## 9.19 P-27: integer spin comparison (same former name)

Old:
```cpp
while (SDL_GetPerformanceCounter() * perfCountsSec < targetTime) { ... }
```

New:
- Convert `targetTime` to ticks in advance
- Use integer comparison only inside the loop

This removes the overhead of float multiplication during the spin wait.

---

## 10. Frame pipeline (starting from Round 5 / supplemented by current-code audit)

When the **document-side endpoint** integrated up through Round 5 is corrected using current-code auditing from Round 6 onward, it can be organized as follows.

```text
Document-side endpoint in Round 5:
  Main loop:
    inputProcess()                   ← edge detection is performed only once here
    hotkeyPressed(HK_Reset), etc.    ← unified gate by P-24

    frameAdvanceOnce() {
      Sleep / HybridLimiter          ← P-11, P-12
      inputRefreshJoystickState()    ← P-15, P-21

      RunFrameHook() {
        PollAndSnapshot()            ← final input acquisition after Z3 + P-19/P-20
        UpdateInputState()
        HandleInGameLogic()
          ProcessMoveAndButtonsFast()← Z2
          ProcessAimInputMouse()     ← OPT-O + P-17 + P-18
      }

      SetKeyMask()
      makeCurrentGL()
      RunFrame()
      drawScreen()

      DeferredDrainInput()           ← P-22 (every frame; P-26b throttle not adopted)
    }
```

### Notes based on the current-code audit (after Round 6)
- Due to P-33, `PrePollRawInput()` has become an empty inline. This achieves the design endpoint of P-20
- `DeferredDrainInput()` is placed **after `drawScreen()`** due to a later change (P-35 was reverted, and `drainPendingMessages` is used)
- The 8-frame throttle of P-26b does not exist in the current code, and the old throttle comment has also been removed

### Historical supplement
- At the P-14 stage, `PrePollRawInput()` was still used
- P-19 made the HiddenWndProc side run `processRawInput` immediately
- In the Round 5 document, P-20 made PrePollRawInput unnecessary
- **Round 6 (P-33) removed the calls and converted it to an empty inline, completing P-20**

Therefore, the **document-side design endpoint** and the **current implementation state** became consistent thanks to P-33.

---

## 11. List of non-adopted, reverted, and rejected items

## 11.1 Rejections / reversions originating from 1

| Item | Conclusion | Reason |
|---|---|---|
| `static thread_local` buffer → stack | Reverted | `__chkstk`, register pressure, and the 16KB stack buffer were disadvantageous |
| Upper bound on the drain loop in `Poll()` | Not adopted | Even at 8000Hz, the normal ~133 messages / frame are fully processable |
| `ApplyAimAdjustBranchless` | Reverted | Added ALU work is counterproductive in hot cases where branch prediction hits |
| Extra reset in OnEmuUnpause | Removed | `ApplyJoy2KeySupportAndQtFilter(..., doReset=true)` already does it |
| Redesign of the HandleGlobalHotkeys gate | Rejected | Too little benefit |
| `m_rawFilter` → BIT flags | Rejected | The pointer is still required anyway |
| Redesign of UnrollCheckDown/Press | Rejected | It is already template-expanded |
| Redesign of ProcessMoveInputFast SnapTap | Rejected | Already sufficiently optimized |
| Add early exit to HandleMorphBallBoost | Historically rejected | The initial proposal was rejected for small effect, but a different form of branch unification was later adopted in Round 6 as P-36 |
| `m_isWeaponCheckActive` → BIT | Rejected | No benefit on a rare path |
| Batch `InputSetBranchless(INPUT_START)` | Rejected | Too much complexity for too little gain |
| Cache `FrameAdvanceDefault usesOpenGL()` | Rejected | Cold path only |
| Skip wheelDelta on reentry frames | Rejected | Reentry is rare |
| `isStylusMode` bool → BIT | Rejected | Externally referenced, with effectively no speed difference |
| `m_isLayoutChangePending` → BIT | Rejected | Effectively no difference between bool and BIT |
| BSF improvement for `pollHotkeys` | Rejected | Already reasonable |
| Split `dwType` in `processRawInputBatched` | Rejected | Hurts cache locality |
| mainRAM prefetch | Rejected | Uncertain effect |
| Panel pointer cache unnecessary | Rejected in the original document | Later adopted in Round 4 / 5 via P-3 and P-20c |

## 11.2 Rejections / reversions from Round 4

| Item | Conclusion | Reason |
|---|---|---|
| P-2 | Reverted | Lost near-distance prefetch and became counterproductive |
| P-4 | Deferred | Double management of the reentry path is dangerous |
| P-7 | Unnecessary | Early exit already exists |
| P-8 | Deferred | Redesigning `FrameInputState` is too risky |
| P-10 | Rejected | Better to keep the `hasKeyChanges` guard |
| Loop unrolling | Rejected | Poor fit with the variable-length `NEXTRAWINPUTBLOCK` |
| Faster Config::Table lookup | Rejected | Cold path only |
| Additional mainRAM prefetch | Rejected | Uncertain effect |
| Re-optimization of UnrollCheckDown/Press | Rejected | Improvement would be too small |

## 11.3 Review notes from Round 5 (not active work items)

The following are additional review notes from the time of Round 5, and are not active work items. They should not be claimed as unapplied optimizations in the current code.

| Item | Contents |
|---|---|
| Frame-time visualization | In addition to FPS, an ms display would make the effect of P-12 easier to validate |
| Further cleanup of SDL joystick Late-Poll | Possible design room to move `inputProcess()` entirely after Sleep |
| `glFlush()` / GPU pipeline delay | Tradeoff analysis for flush before SwapBuffers, assuming `glFinish()` is avoided |

## 11.4 Reverts from Round 6

| Item | Conclusion | Reason |
|---|---|---|
| P-35 | Reverted | Removing `processRawInputBatched` from DeferredDrain caused stuck keys to reappear. Due to the shared-buffer semantics described in FIX-1, `GetRawInputBuffer` must run before PeekMessage. `drainMessagesOnly` cannot be used in `DeferredDrain` |

---

## 11.5 Readings fixed by the source audit

When reading this unified edition, treat the following four points as fixed rules.

1. **P-14 is historically important, but was converted into an empty inline in Round 6 (P-33)**
   In current code, `PrePollRawInput()` does nothing. P-19 covers everything.
2. **P-20 was completed by Round 6 (P-33)**
   Calls to `PrePollRawInput` have been removed, and the implementation is an empty inline.
3. **The essence of P-22 is the drain split, and its placement has been overwritten by later changes**
   The final current-code position is after `drawScreen()`. Because P-35 was reverted, it uses `drainPendingMessages`.
4. **The throttle from P-26b is not adopted in the current implementation**
   There is neither an 8-frame counter nor the old throttle comment.

## 12. Integrated list of modified files

| File | Main integrated contents |
|---|---|
| `MelonPrimeCompilerHints.h` | R1: common macro integration, P-5 |
| `MelonPrime.h` | OPT C/D/G/L/O/W, P-3, P-17, P-18, P-33 (empty-inline PrePollRawInput) |
| `MelonPrime.cpp` | OPT D/E/K/L/O/W/Z1/Z2/Z3/Z4, FIX-3, P-20b, P-20c, P-22 (`DeferredDrainInput()` implementation), P-33 (remove PrePoll implementation), P-43 (focused local cache) |
| `MelonPrimeGameInput.cpp` | OPT F/O/Q/Z2/Z3, FIX-2, P-3, P-17, P-18, P-44 (zero-delta skip) |
| `MelonPrimeInGame.cpp` | OPT A/B/G/H/J/Z2/Z5, R1 constexpr tables, P-36 (HandleMorphBallBoost branch unification) |
| `MelonPrimeGameWeapon.cpp` | OPT A, R2 NDS cache |
| `MelonPrimeGameRomDetect.cpp` | OPT L |
| `MelonPrimeRawInputState.h` | R1 snapshot helpers, R2 organization of `setHotkeyVks` / `hasMask`, P-9, P-42 (`m_hkFastWord` + fast path for `scanBoundHotkeys`) |
| `MelonPrimeRawInputState.cpp` | OPT S/T, R2 button precedence / fetch_add / LUT, P-1, P-6, P-37 (commit-phase branch merge), P-42 (`setHotkeyVks` fast-word calculation) |
| `MelonPrimeRawInputWinFilter.h` | R1 drain helper, R2 overload, P-22, P-35 revert (`drainMessagesOnly` declaration remains) |
| `MelonPrimeRawInputWinFilter.cpp` | FIX-1, OPT R/U, R1/R2, P-19, P-22, P-26b already withdrawn, P-35 reverted (`DeferredDrain` restored to `drainPendingMessages`) |
| `MelonPrimeRawHotkeyVkBinding.h` | R1 `SmallVkList` |
| `MelonPrimeRawHotkeyVkBinding.cpp` | Complete heap elimination in R1/R2 |
| `MelonPrimeRawWinInternal.h/.cpp` | P-11 `NtSetTimerResolution` |
| `EmuThread.cpp` | P-11, P-12, P-13, P-15, P-16, P-22, P-24, P-25, P-26a, P-27, later placement change corresponding to P-32, P-33 (remove PrePollRawInput calls ×2), P-38 (inner hotkey unified gate), P-39 (NeedsShaderCompile cache), P-40 (division→multiplication), P-41 (remove DSi volume sync) |
| `EmuInstance.h` | P-15 / P-21 |
| `EmuInstanceInput.cpp` | P-15, P-21, P-23, P-34 (skip SDL in `inputProcess` when joystick is absent) |
| `MelonPrimeHudRender.cpp` | Custom HUD runtime unity entry point. Bundles `MelonPrimeHudRender*.inc` and edit-mode includes |
| `MelonPrimeHudRenderAssets.inc` | HUD asset/icon/radar-frame/text/outline caches, image/text helpers, dirty-rect support |
| `MelonPrimeHudRenderConfig.inc` | Cached HUD config, anchor recomputation, auto-scale setup |
| `MelonPrimeHudRenderRuntime.inc` | Battle/match state, runtime helpers, hide rules, NoHUD patch, static dirty rect |
| `MelonPrimeHudRenderDraw.inc` | Drawing of HUD elements such as HP/weapon/ammo/radar/crosshair |
| `MelonPrimeHudRenderMain.inc` | `CustomHud_Render()`, edit-mode forward state, radar-frame drawing |
| `MelonPrimeHudConfigOnScreen.cpp` | In-game HUD editor unity entry point. Holds shared edit-mode state and include ordering |
| `MelonPrimeHudConfigOnScreenDefs.inc` | Edit-mode definition tables / property descriptors / element table |
| `MelonPrimeHudConfigOnScreenSnapshot.inc` | Edit-mode snapshot / restore / reset |
| `MelonPrimeHudConfigOnScreenDraw.inc` | Edit-mode bounds / overlay drawing / previews |
| `MelonPrimeHudConfigOnScreenInput.inc` | Edit-mode public API and mouse/wheel input handling |
| `Screen.cpp` | Owner of Custom HUD screen-integration includes. Includes `MelonPrimeHudScreenCpp*.inc` at each call site |
| `Screen.h` | Custom HUD screen-side caches: HUD enable epoch, radar epoch, top matrix, radar anchor DS coords |
| `MelonPrimeHudScreenCppHelpers.inc` | Common helpers for screen fragments: edit-panel placement, epoch/config refresh, overlay clear/render, patch restore |
| `MelonPrimeHudScreenCppOverlayOfSoftware.inc` | Software-paint HUD overlay path. Dirty-rect composite |
| `MelonPrimeHudScreenCppOverlayOfGl.inc` | GL HUD overlay upload/composite and GL-native bottom radar overlay. Dirty upload / GL-state skip |
| `MelonPrimeHudScreenCppGlInit.inc` / `MelonPrimeHudScreenCppGlDeinit.inc` | Custom HUD GL resource init/deinit |
| `MelonPrimeHudScreenCppInit.inc` / `Layout.inc` / `Mouse*.inc` / `EditPanel*.inc` | Screen-panel setup, layout cache, edit-mode input forwarding, floating-panel placement |

---

## 13. Environment notes

- Mouse: 8000Hz polling rate
- At 60fps: about 133 WM_INPUT / frame
- Compiler: supports both MSVC and MinGW
- Because `NEXTRAWINPUTBLOCK` is used, MinGW requires a `QWORD` typedef
- Under x86-64 TSO, relaxed load/store normally compiles to `MOV`
- Q14 fixed point uses `int64_t` multiplication
- Because 4MB MainRAM is involved, prefetch and locality evaluations must be made against the real working set, not under the assumption that everything resides in L1

---

## 14. Summary

This document shall serve as the **canonical document going forward**, while older versions are treated as historical references.

When everything up to the current edition is integrated, the evolution of MelonPrime can be organized as follows.

1. In **1**, Raw Input optimizations based on the Single-Writer premise, FIX-1–3, and OPT A–Z5 were established
2. In **2 (R1)**, maintainability and deduplication advanced
3. In **3 (R2)**, correctness bug fixes and heap elimination were completed
4. In **4**, the adoption of fine-grained optimizations was reviewed in detail, and P-1 / P-3 / P-5 / P-6 / P-9 were adopted
5. In **5**, the **pipeline redesign proposal and its application history** were organized, including input latency, frame order, VSync restoration, subpixel accumulation, the root stuck-keys fix, and syscall reduction
6. In **6**, P-20 (removal of PrePollRawInput) was completed, SDL skipping in `inputProcess` when absent, validation of the DeferredDrain lightweight plan and the P-35 revert, and hot-path branch improvements were applied
7. In **7**, steady-state costs inside the frame loop (virtual dispatch, floating-point division, dead code, hotkey branching) were reduced
8. In **8**, hotkey-scan instruction-count reduction, register optimization in RunFrameHook, and zero-delta skipping in the aim pipeline were applied
9. In **Custom HUD Refactor**, the runtime HUD, on-screen editor, and Screen integration were organized into unity fragments
10. In **Custom HUD Perf**, dirty rect, config cache, screen matrix cache, and GL zero-work skip were applied

In other words, this integration result is not just a collection of fragments intended to “make things faster”, but the **evolution history itself of MelonPrime’s input foundation, frame foundation, and Custom HUD foundation**, built up step by step in terms of:

- **Correctness of Raw Input**
- **Hot-path optimization of game logic**
- **Renewal of input-acquisition timing**
- **Improved precision of aim output**
- **Maintainability and future ease of modification**
- **Minimization of steady-state syscall cost**
- **Organization of Custom HUD rendering cost and Screen integration**

---

## 15. Round 6: integrated results for P-33–P-37

## 15.1 Central theme of Round 6

Round 6 primarily aims to close the gap uncovered by the Round 5 audit between the **document-side endpoint** and the **current code state**, while also further reducing the **steady-state syscall cost** of the input pipeline.

1. **Completion of P-20** — effective removal of PrePollRawInput
2. **Reduction of SDL overhead for KB+M players**
3. **Validation and revert of the DeferredDrain lightweight plan**
4. **Hot-path branch improvements**

## 15.2 Status list

| ID | Category | Status | Handling after integration | Target files |
|---|---|---|---|---|
| P-33 | Syscall reduction | ✅ | Removal of PrePollRawInput (completion of P-20) | `EmuThread.cpp`, `MelonPrime.cpp`, `MelonPrime.h` |
| P-34 | Syscall reduction | ✅ | Skip SDL in `inputProcess` when joystick is absent | `EmuInstanceInput.cpp` |
| P-35 | Syscall reduction | ❌ Reverted | Removal of `GetRawInputBuffer` inside DeferredDrain → stuck keys reappeared | `MelonPrimeRawInputWinFilter.cpp`, `MelonPrimeRawInputWinFilter.h` |
| P-36 | Hot-path improvement | ✅ | Branch unification for HandleMorphBallBoost | `MelonPrimeInGame.cpp` |
| P-37 | Hot-path improvement | ✅ | Commit-phase branch merge in `processRawInputBatched` | `MelonPrimeRawInputState.cpp` |

## 15.3 P-33: removal of PrePollRawInput (completion of P-20)

The Round 5 document (P-20) defined that “PrePollRawInput becomes unnecessary because of P-19”, but by the time of the Round 6 audit, both the implementation and the calls still remained.

**Changes**
- Remove the two `melonPrime->PrePollRawInput()` calls from `EmuThread.cpp`
- Remove the `PrePollRawInput()` implementation from `MelonPrime.cpp`
- Replace it with an empty inline in `MelonPrime.h` (for source compatibility)

**Effect**
- `GetRawInputBuffer` syscalls originating from `PrePollRawInput`: 2/frame → 0/frame
- `PeekMessage` loops originating from `PrePollRawInput`: 2/frame → 0/frame (the post-render `DeferredDrain` remains)
- Estimated reduction: ~500–2000 cyc/frame

**Basis for safety**
Thanks to P-19, `HiddenWndProc` immediately executes `processRawInput(HRAWINPUT)` when WM_INPUT arrives. Even if SDL dispatches WM_INPUT via `PeekMessage`, the data is immediately captured by `processRawInput`. `processRawInputBatched()` inside `PollAndSnapshot` and `DeferredDrain` collect the rest, forming triple protection.

## 15.4 P-34: skip SDL in `inputProcess` when joystick is absent

P-23 implemented a fast path for the no-joystick case only in `inputRefreshJoystickState()`, but `inputProcess()` itself was still unconditionally executing `SDL_LockMutex` + `SDL_JoystickUpdate` + `SDL_UnlockMutex` every frame.

**Changes**
- When `joystick == nullptr`, `inputProcess()` skips SDL calls completely in 59 out of 60 frames
- Detect new connections via a throttled check once every 60 frames
- `inputRefreshJoystickState()` also has a no-joystick fast path, avoiding SDL mutex/update in the hot path inside `frameAdvanceOnce`
- In the current code, `inputProcess()` and `inputRefreshJoystickState()` each have their own 60-frame throttled hot-plug check

**Effect**
- For KB+M players: avoid the SDL mutex syscall + `SDL_JoystickUpdate` in both `inputProcess()` and the frame hot path during normal frames
- Estimated reduction: ~300–600 cyc/frame

## 15.5 P-35: removal of `GetRawInputBuffer` inside DeferredDrain (reverted)

`drainPendingMessages()` had a two-stage structure: `processRawInputBatched()` (`GetRawInputBuffer`) + a PeekMessage loop. Since P-19 had seemingly made `GetRawInputBuffer` redundant in calls from `DeferredDrain`, its removal was attempted.

**Attempted change (already reverted)**
- Introduce `drainMessagesOnly()` (PeekMessage loop only)
- Change `DeferredDrain()` to use `drainMessagesOnly()`

**Reason for revert: stuck keys reappeared**

The cause was the **shared-buffer semantics** documented in FIX-1. `GetRawInputBuffer` and `GetRawInputData` share an internal buffer:

1. `GetRawInputBuffer` in `PollAndSnapshot` consumes the buffer
2. `PeekMessage(PM_REMOVE)` in `DeferredDrain` dispatches WM_INPUT
3. `HiddenWndProc` → `processRawInput` → attempts `GetRawInputData(HRAWINPUT)`, but the buffer has already been consumed and the call fails
4. The key-up event is lost → stuck key

In the original `drainPendingMessages`, `processRawInputBatched` ran before `PeekMessage`, acquiring new data first via `GetRawInputBuffer`, so it was safe even if the later `GetRawInputData` failed.

**Lesson learned**
- The `processRawInputBatched` inside `drainPendingMessages` looks redundant, but is required as a safety net for the shared buffer
- The “belt-and-suspenders” intent behind P-14 existed specifically to prevent this problem
- `drainMessagesOnly` remains as a later-stage helper after `drainPendingMessages`, but is not used in `DeferredDrain`

## 15.6 P-36: branch unification in HandleMorphBallBoost

The original code had a three-stage structure: `BIT_IS_SAMUS` test → `IsDown(IB_MORPH_BOOST)` test → else branch. In the vast majority of frames (not Samus or boost not pressed), execution reaches the else-side aimBlock cleanup.

**Changes**
- Merge the fast path into one branch with `LIKELY(!BIT_IS_SAMUS || !IsDown(IB_MORPH_BOOST))`
- Move the aimBlock cleanup into the fast path
- Improve readability by unnesting the boost-execution logic (rare path)

**Effect**
- Estimated reduction: ~5–15 cyc/frame (mainly via improved branch prediction)

## 15.7 P-37: commit-phase branch merge in `processRawInputBatched`

The two independent zero-check branches for X and Y in mouse-delta commit were merged into one branch using `localAccX | localAccY`.

**Changes**
- Combine both axes in the outer zero check
- Preserve the inner per-axis store skips (to avoid unnecessary stores when only one axis moves)

**Effect**
- On frames with no mouse movement: 2 branches → 1 branch
- Estimated reduction: ~3–8 cyc/frame

## 15.8 Total estimated effect of Round 6

| Environment | Reduction (cyc/frame) | Main factors |
|---|---|---|
| 8kHz mouse + KB+M | ~800–2600 | P-33 + P-34 |
| Normal mouse + KB+M | ~300–1000 | Syscall reduction has relatively larger impact |
| Using joystick | ~500–2000 | No effect from P-34; others remain similar |

**Note:** P-35 is not included in the total because it was reverted.

## 15.9 Frame pipeline after applying Round 6

```text
Endpoint after applying Round 6:
  Main loop:
    inputProcess()                   ← P-34: skip SDL when no joystick
    hotkeyPressed(HK_Reset), etc.    ← unified gate by P-24

    frameAdvanceOnce() {
      Sleep / HybridLimiter          ← P-11, P-12
      // P-33: PrePollRawInput removed
      inputRefreshJoystickState()    ← P-15, P-21

      RunFrameHook() {
        PollAndSnapshot()            ← final input acquisition after Z3 + P-19
        UpdateInputState()
        HandleInGameLogic()
          HandleMorphBallBoost()     ← P-36: branch merge
          ProcessMoveAndButtonsFast()← Z2
          ProcessAimInputMouse()     ← OPT-O + P-17 + P-18
      }

      SetKeyMask()
      makeCurrentGL()
      RunFrame()
      drawScreen()

      DeferredDrainInput()           ← P-22 (`drainPendingMessages`, P-35 reverted)
    }
```

---

## 16. Round 7: frame-loop optimization for P-38–P-41

## 16.1 Central theme of Round 7

Whereas Round 6 focused on reducing syscalls in the input pipeline, Round 7 reduces the **steady-state cost of the frame loop itself**. All targets are inside the `frameAdvanceOnce` lambda in `EmuThread.cpp`.

## 16.2 Status list

| ID | Category | Status | Contents | Estimated effect |
|---|---|---|---|---|
| P-38 | Branch reduction | ✅ | Inner-loop unified hotkey gate (extension of P-24) | ~5–10 cyc/frame |
| P-39 | Virtual-dispatch reduction | ✅ | NeedsShaderCompile cache | ~15–25 cyc/frame |
| P-40 | Floating-point arithmetic improvement | ✅ | Convert division→multiplication in targetTick calculation | ~15–30 cyc/frame |
| P-41 | Dead-code removal | ✅ | Skip DSi volume sync (NDS only) | ~10–20 cyc/frame |

## 16.3 P-38: inner-loop unified hotkey gate

The same pattern used in P-24, which unified the seven `hotkeyPressed()` checks in the main loop behind `hotkeyPress == 0`, is also applied to the three checks inside `frameAdvanceOnce` (FastForwardToggle, SlowMoToggle, AudioMuteToggle).

Since `hotkeyPress == 0` in 99.9%+ of frames, three bit tests + branches are skipped.

## 16.4 P-39: reduction of virtual dispatch in NeedsShaderCompile

`GPU.GetRenderer().NeedsShaderCompile()` costs about ~15–25 cyc due to a vtable lookup + indirect call, but after shader compilation finishes, it returns `false` 100% of the time.

A `shadersReady` flag is introduced so that after compilation completes, the virtual dispatch itself is skipped:

```cpp
bool needsCompile = UNLIKELY(!shadersReady)
    && emuInstance->nds->GPU.GetRenderer().NeedsShaderCompile();
```

Because of short-circuit evaluation, the right-hand side is not evaluated when `shadersReady == true`.

> **⚠️ Important: reset is mandatory when `videoSettingsDirty` occurs**
> `shadersReady` must be reset to `false` when the renderer is switched.
> When `updateRenderer()` creates a new 3D renderer, `NeedsShaderCompile()` becomes `true` again,
> but if `shadersReady` remains `true`, short-circuiting skips the check itself,
> **preventing the new renderer’s shaders from compiling and breaking the display**.
> Reset location: inside the `videoSettingsDirty` block, immediately after `updateRenderer()`.

## 16.5 P-40: convert division→multiplication in targetTick calculation

In the integer-comparison spin loop introduced by P-27, tick conversion used `targetTime / perfCountsSec`.
Since `perfCountsSec = 1.0 / frequency`, that expression is equivalent to `targetTime * frequency`.

```cpp
// Before: DIVSD (~20–35 cyc)
const Uint64 targetTick = static_cast<Uint64>(targetTime / perfCountsSec);

// After: MULSD (~3–5 cyc)
const Uint64 targetTick = static_cast<Uint64>(targetTime * perfCountsFreq);
```

`perfCountsFreq` is computed only once before the loop and referenced via lambda capture.

## 16.6 P-41: skip DSi volume sync

Because MelonPrime is NDS-only (`ConsoleType == 0`), the DSi-specific volume-sync code is unreachable. It is excluded at compile time with `#ifndef MELONPRIME_DS`, removing the following from every frame:

- The bool test of `audioDSiVolumeSync`
- Member access to `nds->ConsoleType`
- Possible `DSi*` cast + I2C pointer chase

## 16.7 Total estimated effect of Round 7

| Environment | Reduction (cyc/frame) | Notes |
|---|---|---|
| Common to all environments | ~45–85 | P-38 + P-39 + P-40 + P-41 |

Compared with the syscall reductions of Round 6 (P-33/P-34), this is an order of magnitude smaller, but it is a steady-state improvement that benefits all environments equally.

## 16.8 Integrated frame pipeline after Round 6 + 7

```text
Endpoint after applying Round 7:
  Main loop:
    inputProcess()                   ← P-34: skip SDL when no joystick
    if (hotkeyPress)                 ← P-24: unified gate (7 checks)
    { ... }

    frameAdvanceOnce() {
      Sleep / HybridLimiter          ← P-11, P-12, P-40 (mul)
      inputRefreshJoystickState()    ← P-15, P-21

      NeedsShaderCompile             ← P-39: skip vtable via shadersReady
      RunFrameHook() {
        PollAndSnapshot()            ← Z3 + P-19
        UpdateInputState()
        HandleInGameLogic()
          HandleMorphBallBoost()     ← P-36: branch merge
          ProcessMoveAndButtonsFast()
          ProcessAimInputMouse()     ← OPT-O + P-17 + P-18
      }

      SetKeyMask()
      makeCurrentGL()
      RunFrame()
      drawScreen()

      DeferredDrainInput()           ← P-22

      if (hotkeyPress)               ← P-38: unified gate (3 checks)
      { FF / SlowMo / Mute }
                                     ← P-41: remove DSi volume sync
    }
```

---

## 17. Round 8: hotkey / aim / register optimizations for P-42–P-44

## 17.1 Central theme of Round 8

Round 8 reduces the **instruction count and unnecessary memory accesses on hot paths that execute every frame**.

## 17.2 Status list

| ID | Category | Status | Contents | Estimated effect |
|---|---|---|---|---|
| P-42 | Instruction-count reduction | ✅ | `testHotkeyMask` single-word fast path | ~60–140 cyc/frame |
| P-43 | Register optimization | ✅ | Local caching of `isFocused` in `RunFrameHook` | ~8–12 cyc/frame |
| P-44 | Instruction-count reduction | ✅ | Zero-delta skip in `ProcessAimInputMouse` | ~8–12 cyc/frame (at low-rate mouse / while stationary) |

## 17.3 P-42: `testHotkeyMask` single-word fast path

`scanBoundHotkeys` scans about ~28 bound hotkeys every frame and calls `testHotkeyMask` for each one. The original implementation ANDed + ORed all four vkMask words (4 AND + 3 OR + compare).

However, most hotkeys are bound to **a single VK code**, so only one of the four words is non-zero.

**Changes**
- Calculate `m_hkFastWord[id]` at bind time in `setHotkeyVks`:
  - 0–3: used word index (single-word, no mouse)
  - 4: mouse only
  - 5: multi-word or mixed (falls back to the full check)
- In `scanBoundHotkeys`, consult `m_hkFastWord`; if it is a single-word case, do only 1 AND + compare

**Effect**
- 28 hotkeys × (4 AND + 3 OR → 1 AND) = ~168 instructions removed
- Branch prediction: identical hotkey configuration every frame → perfectly predictable
- Estimated reduction: ~60–140 cyc/frame

## 17.4 P-43: local caching of `isFocused` in `RunFrameHook`

`isFocused` is a public member variable of `MelonPrimeCore`, set by the GUI thread. After member-function calls such as `UpdateInputState()`, `HandleGlobalHotkeys()`, and `HandleInGameLogic()` inside `RunFrameHook`, the compiler cannot prove that `this->isFocused` is unchanged, so it is forced to reread it from memory.

By caching it locally with `const bool focused = isFocused`, all subsequent references can be replaced with `focused`. This is safe because the emu thread only reads it.

**Effect**
- Avoids 2–3 L1 memory rereads
- Estimated reduction: ~8–12 cyc/frame

## 17.5 P-44: zero-delta skip in `ProcessAimInputMouse`

On an 8kHz mouse, delta is almost always non-zero, but on standard mice (125–1000Hz) or while stationary, zero-delta frames occur. When delta is zero, it skips IMUL × 2 + clamp × 2, and if residuals are also zero, it returns immediately.

**Changes**
- Non-zero check via `deltaX | deltaY` (the LIKELY path remains unchanged)
- Early return when both delta and residual are zero (skips the entire direct/legacy path)

**Effect**
- While stationary: skip 2 IMUL (~6 cyc) + 2 clamp (~4 cyc) + the entire direct/legacy path
- Estimated reduction: ~8–12 cyc/frame (only while the mouse is stationary; rare at 8kHz)

## 17.6 Cumulative estimated effect of Round 6 + 7 + 8

| Environment | Round 6 | Round 7 | Round 8 | Total |
|---|---|---|---|---|
| 8kHz mouse + KB+M | ~800–2600 | ~45–85 | ~68–152 | **~913–2837** |
| Normal mouse + KB+M | ~300–1000 | ~45–85 | ~76–164 | **~421–1249** |
| Using joystick | ~500–2000 | ~45–85 | ~68–152 | **~613–2237** |

---

## 18. Integrated result of the Custom HUD refactor

## 18.1 Central theme

Originally, large amounts of processing were concentrated in `MelonPrimeHudRender.cpp` / `MelonPrimeHudConfigOnScreen.cpp` / `Screen.cpp`. In the current implementation, responsibilities are split into unity include fragments, improving readability without increasing the number of build units.

Important rules:
- `MelonPrimeHudRender*.inc` must only be included from `MelonPrimeHudRender.cpp`
- `MelonPrimeHudConfigOnScreen*.inc` must only be included from `MelonPrimeHudConfigOnScreen.cpp`
- `MelonPrimeHudScreenCpp*.inc` must only be included from `Screen.cpp`
- These `.inc` files are not standalone translation units, so they must not be added to `CMakeLists.txt`

## 18.2 Runtime HUD split

`MelonPrimeHudRender.cpp` is the unity entry point for the runtime HUD, and currently includes the following fragments in order.

| File | Responsibility |
|---|---|
| `MelonPrimeHudRenderAssets.inc` | icon/radar/text/outline cache, image helper, dirty-rect support |
| `MelonPrimeHudRenderConfig.inc` | `CachedHudConfig`, config load, anchor recomputation, auto-scale |
| `MelonPrimeHudRenderRuntime.inc` | runtime state, battle state, hide rules, NoHUD patch, dirty-rect computation |
| `MelonPrimeHudRenderDraw.inc` | HUD element drawing |
| `MelonPrimeHudRenderMain.inc` | `CustomHud_Render()`, edit-mode forward state, radar-frame drawing |

## 18.3 On-screen HUD Editor split

`MelonPrimeHudConfigOnScreen.cpp` is the unity entry point for the in-game editor, and holds shared edit-mode state and the include order.

| File | Responsibility |
|---|---|
| `MelonPrimeHudConfigOnScreenDefs.inc` | edit-element table, property definitions, sample text |
| `MelonPrimeHudConfigOnScreenSnapshot.inc` | snapshot / restore / reset-to-default |
| `MelonPrimeHudConfigOnScreenDraw.inc` | hit bounds, selection box, property panel, preview drawing |
| `MelonPrimeHudConfigOnScreenInput.inc` | edit-mode API, mouse press/move/release, wheel handling |

## 18.4 Split of Custom HUD integration in Screen.cpp

The Custom HUD integration inside `Screen.cpp` is split into `MelonPrimeHudScreenCpp*.inc`.

| File | Responsibility |
|---|---|
| `MelonPrimeHudScreenCppHelpers.inc` | screen-side common helpers |
| `MelonPrimeHudScreenCppInit.inc` | overlay buffers, HUD font, edit side panel, selection callback |
| `MelonPrimeHudScreenCppLayout.inc` | update of HUD scale/origin/top-matrix cache |
| `MelonPrimeHudScreenCppMouseWheel.inc` | edit-mode mouse-wheel interception |
| `MelonPrimeHudScreenCppMousePress.inc` | edit-mode mouse-press interception |
| `MelonPrimeHudScreenCppMouseRelease.inc` | edit-mode mouse-release interception |
| `MelonPrimeHudScreenCppMouseMove.inc` | edit-mode mouse-move/drag interception |
| `MelonPrimeHudScreenCppEditPanelResize.inc` | edit side-panel placement on resize |
| `MelonPrimeHudScreenCppEditPanelMove.inc` | edit side-panel placement on move |
| `MelonPrimeHudScreenCppOverlayOfSoftware.inc` | software `QPainter` overlay path |
| `MelonPrimeHudScreenCppGlInit.inc` | HUD/radar GL resource init |
| `MelonPrimeHudScreenCppGlDeinit.inc` | HUD/radar GL resource cleanup |
| `MelonPrimeHudScreenCppOverlayOfGl.inc` | GL overlay upload/composite, GL-native radar overlay |

---

## 19. Integrated result of Custom HUD performance work

## 19.1 OPT-DR1: Dirty-rect overlay optimization

`CustomHud_Render()` returns the drawn pixel-space dirty rect as a `QRect`. The software / GL paths in `Screen.cpp` clear / composite / upload only the union of the previous frame’s dirty rect and the current frame’s dirty rect.

Main effects:
- Avoid steady-state full-window `QImage::fill()`
- In the GL path, limit `glTexSubImage2D` to the dirty rect only
- Keep the upload/composite region small on frames where only the crosshair moves
- Outside first-person view, skip weapon/ammo/crosshair-related RAM reads and dirty rect work

## 19.2 Cache / skip on the HUD-render runtime side

The current runtime caches the following.

| Target | Contents |
|---|---|
| `CachedHudConfig` | HUD config and anchor-recomputation results |
| `BattleMatchState` | Decode battle settings when joining a match |
| Text bitmap / measurement cache | Reuse pixmap/measurement results for repeated text drawing |
| Weapon/bomb icon cache | Reuse SVG/icon rasterization and tinting |
| Radar-frame cache | Regenerate SVG frame, tint, and outline only when size/color changes |
| Static dirty rect | Recompute dirty rects for fixed-position HUD elements only when config/transform changes |
| Crosshair dirty rect | Union of previous/current crosshair bounding boxes |

## 19.3 OPT-SC1: Screen-fragment hot-path optimization

On the Screen-integration side, per-frame costs outside the HUD overlay itself are reduced.

| Optimization | Contents |
|---|---|
| HUD enable cache | Use `m_hudCfgEpoch` + `m_hudEnabled` so `GetBool()` for `Metroid.Visual.CustomHUD` runs only when the epoch updates |
| Separate radar config cache | Manage GL radar config independently from the HUD enable cache via `m_radarCfgEpoch` |
| Top-matrix cache | Update `m_hudTopMatrix` / `m_hudTopMatrixValid` in `setupScreenLayout()`, removing `screenKind` scans from the GL radar path |
| Radar-anchor precompute | Compute `m_radarAnchorDsX/Y` during config refresh, removing per-frame `%` / `/` |
| Empty-dirty skip | In the GL path, if both previous/current dirty rects are empty, skip texture bind/upload, OSD shader setup, blend, and draw entirely |
| Conditional GL restore | Restore screen shader / buffer / VAO / texture only when the HUD/radar actually changed GL state |
| Software reset skip | In the software path, skip `painter.resetTransform()` when the composite rect is empty |

## 19.4 Current caution points

- `MelonPrimeHudScreenCppOverlayOfGl.inc` handles both the HUD overlay and the GL-native radar overlay, so if additional GL state changes are introduced, always verify `hudGlStateChanged` and the restore targets.
- `m_hudCfgEpoch` and `m_radarCfgEpoch` are intentionally separated so that the software path and the GL radar path do not invalidate each other’s cache refreshes.
- `m_hudTopMatrix` depends on the result of `setupScreenLayout()`. If a change bypasses layout updates, watch out for missed updates to this cache.
- `.inc` fragments are not compiled on their own. They depend on the scope of the including file, local variables, and `#ifdef MELONPRIME_CUSTOM_HUD`.
