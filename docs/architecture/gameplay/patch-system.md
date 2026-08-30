# MelonPrime Patch System

This document describes how to implement a new runtime ARM9 patch in MelonPrimeDS.

There are **two distinct patch classes**:

1. **Static write-patches** (the main body of this doc) — `*_ApplyOnce` rewrites ARM9
   instructions/data once; the change persists until the game resets. Use for behavior that does
   not depend on live state at a specific code point.
2. **Runtime instruction hooks** (see the "ARM9 instruction-hook subsystem" section below) —
   trampoline into C++ *every time* the ARM9 executes a specific PC, with access to live
   registers/RAM and the option to redirect execution. Use when the effect depends on runtime
   state or must conditionally change control flow.

## Overview

Patches are standalone `.h` / `.cpp` pairs under `src/frontend/qt_sdl/MelonPrimePatch*.{h,cpp}`.
All headers are aggregated by `MelonPrimePatch.h` (included by `MelonPrime.cpp` and any other caller).

All patch code is wrapped in `#ifdef MELONPRIME_DS … #endif`.

When a patch needs code inside files that do **not** start with `MelonPrime`,
keep the added code behind `#ifdef MELONPRIME_DS … #endif`.  If the addition is
more than a tiny call site, put the MelonPrime-specific body in a
`MelonPrime*.inc` file and include that file from the guarded region.

---

## Patch registry (`MelonPrimePatchRegistry`)

`MelonPrimePatchRegistry.h/.cpp` centralizes the write-patch lifecycle. `MelonPrime.cpp` no
longer lists per-module calls; it builds a `PatchCtx` and calls the registry at each lifecycle
site. Modules keep their existing public functions untouched; the registry holds one
`kPatchRegistry[]` entry per module with thin adapters mapping `PatchCtx` onto each module's
signature.

```cpp
struct PatchCtx {            // unified context, built by the caller per call
    melonDS::NDS* nds;
    EmuInstance* emu;
    Config::Table& cfg;
    const RomAddresses& rom; // rom.romGroupIndex / rom.isInAdventure etc. for adapters
};
enum PatchApplySite : uint8_t {  // where Patches_Apply fires the entry
    PatchSite_GameJoin       = 1u << 0,  // HandleGameJoinInit (once per join)
    PatchSite_ConfigReload   = 1u << 1,  // ApplyConfigReload (ROM detected)
    PatchSite_OutOfGameFrame = 1u << 2,  // RunFrameHook !isInGame && focused (per frame, self-guarded)
};
enum PatchRestoreFlag : uint8_t {        // which restore API fires the entry
    RF_None = 0,
    RF_OnLeave = 1u << 0,                // Patches_RestoreOnLeave (isEndOfGame window)
    RF_OnStop  = 1u << 1,                // Patches_RestoreOnStop (OnEmuStart / OnEmuStop)
};

void Patches_Apply(uint8_t siteMask, const PatchCtx&);
void Patches_RestoreOnLeave(const PatchCtx&);
void Patches_RestoreOnStop(const PatchCtx&);
void Patches_ResetAll();   // state flags only, never touches emulated RAM
```

**Ordering guarantee:** `kPatchRegistry[]` iteration order defines apply and restore order. The
table is ordered so each site's apply order matches the pre-registry call lists exactly:
GameJoin = AspectRatio; BattleRuntime = OsdColor, LowHpWarning, FpsCameraLock,
ShowHeadshotOnline, ShowEnemyHpMeterOnline, DisableDoubleDamageMultiplier,
NoPickingUpSpecificItems; ConfigReload = FpsCameraLock..NoPickingUp (only while
`BIT_BATTLE_RUNTIME_MODE`); OutOfGameFrame = FixWifi, UseFirmwareLanguage, ExpandStageMatrix.

**Call sites in `MelonPrime.cpp`:** `HandleGameJoinInit` → `Patches_Apply(PatchSite_GameJoin)`
directly (InGameAspectRatio only); `HandleBattleRuntimeEnter` (first `mode==0x0E && flow==0`) →
`PatchLifecycle::ApplyOnBattleRuntimeEnter(...)`, which wraps
`Patches_Apply(PatchSite_BattleRuntime)` + `ARM9Hook_SetMatchHooksActive(true)` + the conditional
`WeaponSwitchHook_IsSiteValid` call (PatchLifecycleGateway Step 3 Site B); `ApplyConfigReload`
(when `BIT_BATTLE_RUNTIME_MODE`) → `PatchLifecycle::ReapplyForConfigReload(...)`, which wraps
`ARM9Hook_SetMatchHooksActive(true)` + `Patches_Apply(PatchSite_ConfigReload)` (Step 2);
`RunFrameHook` `!isInGame && focused` → `PatchLifecycle::ApplyOutOfGameFrame(...)`, wrapping
`Patches_Apply(PatchSite_OutOfGameFrame)` (Step 3 Site E); **match-end poll** (`flow!=0` after
latch) → `PatchLifecycle::RestoreOnMatchEnd(...)`, wrapping `Patches_RestoreOnLeave` +
`ARM9Hook_SetMatchHooksActive(false)` (Step 3 Site A; not on generic `!isInGame` — see
[battle-flow-state.md](battle-flow-state.md)); `OnEmuStart` /
`OnEmuStop` → `PatchLifecycle::ResetForEmuStart` / `RestoreForEmuStop`, wrapping
`Patches_RestoreOnStop` + `Patches_ResetAll` (Step 1); `ResetRuntimeStateForBoot` →
`PatchLifecycle::ResetForBoot`, wrapping `Patches_ResetAll` only (boot reset: state only, no
RAM restore).

In every case, `RunFrameHook`'s own frame-state flag writes
(`BIT_END_OF_GAME_PATCH_RESTORED`, `BIT_BATTLE_RUNTIME_MODE`) stay in `MelonPrime.cpp` next to
the `PatchLifecycle` call — the gateway functions own patch/ARM9-hook lifecycle only, never
frame-state flags. `RunFrameHook`'s **Site D** (the generic `!isInGame` leave-in-game cleanup,
which calls `ARM9Hook_SetMatchHooksActive(false)` without `Patches_RestoreOnLeave`) is still a
direct inline call, not a `PatchLifecycle` wrapper — see
[../../archive/features/srp-v3/melonprime_patch_lifecycle_gateway_step3_plan.md](../../archive/features/srp-v3/melonprime_patch_lifecycle_gateway_step3_plan.md)
for why that one is deliberately left as-is for now.

**Statics:** module `s_applied` state is per-process; melonDS multi-instance runs as separate
processes, so the per-process singleton assumption holds.

**Intentionally outside the registry:**

- the per-frame `OsdColor_ApplyOnce` re-apply in `RunFrameHook`'s `isInGame` branch (pattern B —
  game-state-dependent re-evaluation; the registry covers only its game-join apply + leave restore)
- ARM9 instruction hooks — `ARM9Hook_Install/Uninstall` is its own registry; dispatch entries
  and the selected ROM group are owned by `MelonPrimeArm9HookState` per Core
- Custom HUD patch state (`CustomHud_*`) — HUD-owned lifecycle
- `NoDoubleTapJump` — transient, wraps weapon-switch frames in `MelonPrimeGameWeapon.cpp`
- `NoHud` — driven by the Custom HUD render path
- `OsdColor_InvalidatePatch` / `ExpandStageMatrix_InvalidatePatch` on settings save
  (`MelonPrimeInputConfigConfig.cpp`)

---

## File structure

### Header pattern (`MelonPrimePatchFoo.h`)

```cpp
#ifndef MELON_PRIME_PATCH_FOO_H
#define MELON_PRIME_PATCH_FOO_H

#ifdef MELONPRIME_DS

#include <cstdint>

// Forward-declare only what the API uses
namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {

    void Foo_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex);
    void Foo_ResetPatchState();
    // Add Foo_RestoreOnce(melonDS::NDS*, const RomAddresses&) if mid-session restore is needed

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_FOO_H
```

### Implementation pattern (`MelonPrimePatchFoo.cpp`)

```cpp
#ifdef MELONPRIME_DS

#include "MelonPrimePatchFoo.h"
#include "MelonPrimePatchCommon.h"
#include "Config.h"

namespace MelonPrime {
namespace {

static constexpr const char* kCfgFoo = "Metroid.BugFix.Foo";

// ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
static constexpr PatchWord kPatchWords[7][2] = {
    { // JP1_0
        { 0x0206XXXXu, 0xE1A00000u, 0xXXXXXXXXu },
        { 0x0206XXXXu, 0xE1A00000u, 0xXXXXXXXXu },
    },
    { // JP1_1
        { 0x0206XXXXu, 0xE1A00000u, 0xXXXXXXXXu },
        { 0x0206XXXXu, 0xE1A00000u, 0xXXXXXXXXu },
    },
    // ...
};

// count == 0 means this ROM group is unsupported.
static constexpr RomPatchSpan kPatchSpans[7] = {
    { &kPatchWords[0][0], 2 },
    { &kPatchWords[1][0], 2 },
    { nullptr, 0 }, // US1_0 unsupported
    { nullptr, 0 }, // US1_1 unsupported
    { nullptr, 0 }, // EU1_0 unsupported
    { nullptr, 0 }, // EU1_1 unsupported
    { nullptr, 0 }, // KR1_0 unsupported
};

static StaticWordPatch s_patch(kPatchSpans);

} // namespace

void Foo_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex)
{
    if (!cfg.GetBool(kCfgFoo))
    {
        s_patch.RestoreOnce(nds, romGroupIndex);
        return;
    }

    s_patch.ApplyOnce(nds, romGroupIndex);
}

void Foo_RestoreOnce(melonDS::NDS* nds, uint8_t romGroupIndex)
{
    s_patch.RestoreOnce(nds, romGroupIndex);
}

void Foo_ResetPatchState()
{
    s_patch.ResetState();
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
```

Historical note: older modules may still hand-roll `s_applied` / `CanWritePatch` when they have
patch-specific mechanics; new all-or-nothing static word-patches should use `StaticWordPatch`.

---

## Checklist for a new patch

### 1. Patch files

- [ ] Create `MelonPrimePatchFoo.h` (see header pattern above)
- [ ] Create `MelonPrimePatchFoo.cpp` (see implementation pattern above)
- [ ] Add `#include "MelonPrimePatchFoo.h"` to `MelonPrimePatch.h`
- [ ] Add `MelonPrimePatchFoo.cpp` to `CMakeLists.txt` inside the `MELONPRIME_DS` source list (around line 88–91)

### 2. Registry entry — `MelonPrimePatchRegistry.cpp`

Add **one entry** to `kPatchRegistry` (a thin `Apply_Foo` / `Restore_Foo` adapter pair plus the
table row — see the "Patch registry" section below), choosing:

- **Apply site mask** (`PatchApplySite`):
  - `PatchSite_GameJoin` — standard: applied once per in-game join from `HandleGameJoinInit()`
    (cold path). Use when the patch writes ARM9 code that persists until the game resets.
  - `PatchSite_ConfigReload` — also re-applied from `ApplyConfigReload()` (only when a ROM is
    detected) so a live settings toggle takes effect without rejoining. Usually combined with
    `PatchSite_GameJoin` and `RF_OnLeave | RF_OnStop`.
  - `PatchSite_OutOfGameFrame` — applied per frame in `RunFrameHook`'s `!isInGame && focused`
    block (pattern C). The module must include its own loaded-state guard so it is a no-op when
    the target data is not present; name the function `Foo_ApplyIfLoaded` rather than
    `Foo_ApplyOnce` to signal this distinction (`ResetPatchState()` is then typically a no-op).
- **Restore flags** (`PatchRestoreFlag`): `RF_OnLeave` (restored on match end — latch
  `mode==0x0E && flow==0`, then first `flow!=0` while `BIT_IN_GAME_INIT &&
  !BIT_END_OF_GAME_PATCH_RESTORED`), `RF_OnStop` (restored in `OnEmuStart` / `OnEmuStop`), or
  `RF_None`.
- **resetState**: point it at `Foo_ResetPatchState` (wire it even for pattern C no-ops).

Table order = apply/restore order. Insert the entry at the position matching its site grouping
(GameJoin entries first, then the GameJoin+ConfigReload subset, then OutOfGameFrame entries).

Stays outside the registry: per-frame **in-game** re-evaluation (pattern B — e.g. the
`OsdColor_ApplyOnce` call in `RunFrameHook`'s `isInGame` branch) remains a direct call at that
site; transient patches (NoDoubleTapJump) and HUD-owned patches (NoHud) keep their own call sites.

### 3. Lifecycle — automatic via the registry

No `MelonPrime.cpp` edits are needed for restore/reset. The lifecycle blocks (`OnEmuStart`,
`ResetRuntimeStateForBoot`, `OnEmuStop`) and the match-end `Patches_RestoreOnLeave` call wire
restore-on-stop/leave; `Patches_ResetAll` on boot/stop is automatic from the registry entry in
step 2.
(`ResetRuntimeStateForBoot` intentionally resets state **without** restoring RAM: emulated memory
is being re-initialized.)

### 4. Settings UI — `MelonPrimeInputConfig.ui`

Add a `QCheckBox` (and optional `QLabel` description) inside an existing or new collapsible section.
For bug fixes, the **BUG FIXES** section (`btnToggleBugFix` / `sectionBugFix`) already exists
immediately above the GAMEPLAY TOGGLES section in the Settings tab.

Visible MelonPrime settings text must also be registered for English/Japanese localization in
`src/frontend/qt_sdl/MelonPrimeLocalization.cpp` (the public API stays in
`MelonPrimeLocalization.h`):

- Short labels, checkbox text, combo items, button text, tooltips, and anonymous descriptions go in
  `MelonPrime::UiText::kTranslations` as English source text -> Japanese display text.
- Long description labels should have a stable object name such as `lblMetroidFooDesc`, then use
  `MelonPrime::UiText::kObjectTextTranslations` to map that object name to the Japanese text. This
  avoids fragile exact matching for long or HTML-rich descriptions.
- Dynamic labels created in C++ should either set a stable object name before
  `MelonPrime::UiText::LocalizeWidgetTree(this)` runs, or assign text through
  `MelonPrime::UiText::Tr(...)`.
- Config keys, TOML keys, object names, and storage enum values stay English. Localization is only
  for visible UI text.

Add to `sectionBugFix`'s `QVBoxLayout`:

```xml
<item>
    <widget class="QCheckBox" name="cbMetroidFoo">
        <property name="text"><string>Short label for the patch</string></property>
    </widget>
</item>
<item>
    <widget class="QLabel" name="lblMetroidFooDesc">
        <property name="text"><string>Multi-line description of what the patch does.</string></property>
        <property name="wordWrap"><bool>true</bool></property>
    </widget>
</item>
```

Then add the checkbox label and description text to `MelonPrimeLocalization.cpp`. For the
description above, prefer the `lblMetroidFooDesc` object-name mapping.

### 5. Settings wiring — `CfgKey` + `MelonPrimeInputConfig.cpp`

Add a config-key constant to `MelonPrimeDef.h` under `namespace MelonPrime::CfgKey`:

```cpp
inline constexpr const char* Foo = "Metroid.BugFix.Foo";
```

For a straightforward mirrored setting (checkbox, combo index, int spin box, or double spin box),
add one row to `MelonPrimeInputConfig::buildSettingBindings()` in the matching segment:

```cpp
{ C::Foo, K::CheckBool, ui->cbMetroidFoo },
```

`loadBindingsRange()` and `saveBindings()` then handle the load/save path. Keep the row in the
same segment where the old manual load would have lived; some segment boundaries preserve
observable slot side effects and parent/child enable ordering.

The BUG FIXES section toggle is already registered in `setupCollapsibleSections`. No extra
`setupToggle` call is needed for new checkboxes inside the existing section.

#### Checkbox dependencies (parent/child enable control)

If a checkbox must control the enabled state of another checkbox, **do not use a lambda `connect` inside `setupSensitivityAndToggles`**. This causes crashes because signal/event ordering during dialog initialization is unpredictable.

Instead, use the Qt auto-slot pattern:

**`MelonPrimeInputConfig.h`** — add to the `private slots:` block alongside other `on_cbXxx_stateChanged` declarations:

```cpp
void on_cbMetroidFoo_stateChanged(int state);
```

**`MelonPrimeInputConfig.cpp`** — add the implementation near other `on_cbXxx_stateChanged` definitions (around line 760):

```cpp
void MelonPrimeInputConfig::on_cbMetroidFoo_stateChanged(int state)
{
    const bool checked = (state == Qt::Checked);
    ui->cbMetroidFooChild->setEnabled(checked);
    if (!checked)
        ui->cbMetroidFooChild->setChecked(false);
}
```

**`setupSensitivityAndToggles`** — set the initial enabled state after setting the check states:

```cpp
// The checked states should normally come from buildSettingBindings() + loadBindingsRange().
ui->cbMetroidFooChild->setEnabled(ui->cbMetroidFoo->isChecked());
```

Qt's `QMetaObject::connectSlotsByName` (called inside `ui->setupUi(this)`) automatically connects `on_cbMetroidFoo_stateChanged` to the widget's `stateChanged` signal. No manual `connect()` call is needed or wanted.

### 6. Save — `MelonPrimeInputConfigConfig.cpp`

For generic bindings from step 5, no manual save line is needed; `saveBindings()` writes the key.
Only add direct `instcfg.Set*()` code for special cases that are intentionally outside the binding
table (legacy migrations, old/new invalidation coupling, dynamic combo data, or developer-only
guarded settings). If a special case is needed, keep it near the related block and use `CfgKey::*`
where available:

```cpp
instcfg.SetBool(MelonPrime::CfgKey::Foo, ui->cbMetroidFoo->checkState() == Qt::Checked);
```

### 7. Config defaults — `Config.cpp`

In `DefaultBools`, add inside the `#ifdef MELONPRIME_DS` block
(near other `Metroid.BugFix.*` or `Metroid.Visual.*` entries):

```cpp
{"Instance*.Metroid.BugFix.Foo", true},   // or false if opt-in
```

If the config key uses `GetInt` or `GetDouble`, add it to `DefaultInts` / `DefaultDoubles` instead
(see repo-architecture.md "Default value type classification" for the type-list rule).

Run the checked-in default coverage audit after adding settings:

```powershell
.\tools\ci\audits\audit-config-defaults.ps1
```

---

## ROM group index reference

| Index | ROM | Notes |
|-------|-----|-------|
| 0 | JP1_0 | First JP release — often has bugs fixed in JP1_1 |
| 1 | JP1_1 | Revision |
| 2 | US1_0 | First US release |
| 3 | US1_1 | Revision |
| 4 | EU1_0 | First EU release |
| 5 | EU1_1 | Revision |
| 6 | KR1_0 | Korean release |

`m_currentRom.romGroupIndex` (type `uint8_t`) holds the index for the running ROM.
Use `0xFFFFFFFFu` as the sentinel "not applicable" base address.

---

## Existing patches — reference implementations

These are all **static write-patches**. Instruction-hook patches (ShadowFreeze, FixNoxusBlade,
WeaponSwitch, TransformGate, NativeAimDelta, etc.) are documented in the
"ARM9 instruction-hook subsystem" section below instead.

| Patch | Files | Apply site (registry mask) | Notes |
|-------|-------|-----------|-------|
| In-game aspect ratio | `MelonPrimePatchAspectRatio.*` | Registry: `GameJoin` | Uses `MainRAM` read to detect/guard; registry resets state on stop/reset |
| OSD color | `MelonPrimePatchOsdColor.*` | Registry: `BattleRuntime` (`RF_OnLeave`) + per-frame `isInGame` re-apply when `BIT_BATTLE_RUNTIME_MODE` | `OsdColor_InvalidatePatch` on settings save |
| No-HUD | `MelonPrimePatchNoHud.*` | Outside registry: called from Custom HUD render path | Per-frame apply/restore depending on HUD state |
| No double-tap jump | `MelonPrimePatchNoDoubleTapJump.*` | Outside registry: `MelonPrimeGameWeapon.cpp` (transient, wraps `FrameAdvanceTwice`) | Not persistent; applied/restored around weapon-switch frames only |
| Wi-Fi bitset fix | `MelonPrimePatchFixWifi.*` | Registry: `OutOfGameFrame` | JP1_0 / US1_0 / EU1_0 only; 51-word patch |
| Stage matrix expansion | `MelonPrimePatchExpandStageMatrix.*` | Registry: `OutOfGameFrame` (pattern C) | Writes RAM data bytes, not ARM code; self-guarded via strict 3-point loaded-state check; `ResetPatchState` is a no-op (still wired in the registry); base (5 cells) + extra (9 cells) split across two config keys |
| Low HP warning | `MelonPrimePatchLowHpWarning.*` | Registry: `BattleRuntime` | Registry: `RF_OnLeave \| RF_OnStop` |
| Use firmware language | `MelonPrimePatchUseFirmwareLanguage.*` | Registry: `OutOfGameFrame` | Adventure-aware; applied in menus; adapter passes `rom.isInAdventure` as 4th arg |
| FPS Camera Lock | `MelonPrimePatchFpsCameraLock.*` | Registry: `BattleRuntime \| ConfigReload` (`RF_OnLeave \| RF_OnStop`) | Public independent camera-behavior patch; distinct from the `LowLatencyMode` ImmediateSync/MoonLike instruction hook. |
| Show headshot online | `MelonPrimePatchShowHeadshotOnline.*` | Registry: `BattleRuntime \| ConfigReload` (`RF_OnLeave \| RF_OnStop`) | |
| Show enemy HP meter online | `MelonPrimePatchShowEnemyHpMeterOnline.*` | Registry: `BattleRuntime \| ConfigReload` (`RF_OnLeave \| RF_OnStop`) | |
| Disable double-damage multiplier | `MelonPrimePatchDisableDoubleDamageMultiplier.*` | Registry: `BattleRuntime \| ConfigReload` (`RF_OnLeave \| RF_OnStop`) | Pairs with Damage-Notify-Purple |
| No picking up specific items | `MelonPrimePatchNoPickingUpSpecificItems.*` | Registry: `BattleRuntime \| ConfigReload` (`RF_OnLeave \| RF_OnStop`) | |
| Touch-screen aim only (battle) | `MelonPrimePatchTouchScreenAimOnly.*` | Registry: `BattleRuntime \| ConfigReload` (`RF_OnLeave \| RF_OnStop`) | Neutralizes the three in-match bottom-screen HUD hit-tests (Morph Ball, weapon quick slots, weapon menu) with `mov r0,#0` so the whole touch screen stays aim input; double-tap jump and the touch boost gesture are left alone |

---

## ARM9 instruction-hook subsystem

*Runtime trampoline hooks.*

The patterns above cover **static write-patches**: `*_ApplyOnce` rewrites ARM9 instructions/data
once and they persist until the game resets. A second, distinct class exists — **runtime
instruction hooks** that trampoline into C++ *every time* the ARM9 executes a specific PC. Use a
hook (not a write-patch) when the behavior depends on live state (registers / RAM / config) at the
moment the game reaches a code point, or when you need to conditionally redirect execution.

### Core mechanism

- `src/frontend/qt_sdl/MelonPrimeArm9InstructionHook.inc` is a multi-section fragment included into
  core melonDS files (`NDS.h`, `NDS.cpp`, `ARM.cpp`, `ARMJIT_x64/ARMJIT_Compiler.*`) behind
  `#ifdef MELONPRIME_DS`. It adds one hook slot to `NDS`:
  `SetARM9InstructionHook(fn, userdata, addresses[], count)` / `ClearARM9InstructionHook()`.
  `ARM9InstructionHookMaxAddresses = 32`.
- Hook fn signature:
  `bool(NDS*, void* userdata, u32 arm9ExecAddr, u32 regs[16], u32& redirectExecAddr)`.
  Return `true` + set `redirectExecAddr` → CPU `JumpTo(redirectExecAddr)` (redirect). Return
  `false` → original instruction runs (side-effect-only hook).
- **JIT path (default):** the compiler emits the trampoline call **only at addresses that matched at
  compile time** (`ARM9InstructionHookMatches(addr)` in the compile loop), and
  `SetARM9InstructionHook` resets the JIT block cache when the installed address list changes.
  `InstalledDispatcherMatches` compares the dispatcher, userdata, and address list for developer
  diagnostics, but `ARM9Hook_Install` still reattaches the hook set and writes back every hook PC
  when the enabled count is nonzero. This is required after match-end `ClearARM9InstructionHook`
  so rematched JIT blocks pick up the trampoline. Non-hooked instructions cost **zero**; each
  hooked PC pays a `RegCache.Flush` + call when executed.
- **Interpreter path:** every instruction runs `ARM9InstructionHookAddressMatches`, a hash-mask
  early-out (`1u << ((addr>>2)&31)`) followed by a short linear scan.

### Central dispatcher — `MelonPrimeArm9Hook.cpp`

Owns the single hook slot and fans out to all registered MelonPrime hooks.

- `ARM9Hook_SetMatchHooksActive(nds, romGroupIndex, core, active, osdEmu)` — installs or
  clears **match-scoped** hooks (`ARM9HookScope_InMatch`). Today every registered hook is
  match-scoped. `true` from `HandleBattleRuntimeEnter`; `false` on match-end and `!isInGame`.
  `ApplyConfigReload` when `BIT_BATTLE_RUNTIME_MODE`. ROM detect calls `false`. The activation
  booleans come from the core's already-applied `Arm9HookActivationPlan`; this edge does not read
  or reinterpret `Config::Table` keys. Future out-of-match hooks can use a new scope bit.
- `ARM9Hook_Install(nds, romGroupIndex, core, plan, activeScope, osdEmu)` — builds the enabled hook
  PC list from the supplied `Arm9HookActivationPlan` and scope, then **always** calls
  `SetARM9InstructionHook` when `count > 0` and write-backs every hook PC
  (needed so JIT blocks pick up trampolines after match-end `ClearARM9InstructionHook`; skipping
  `SetARM9InstructionHook` when the address list matches left rematch hooks dead). Match-end latch
  requires `mode==0x0E && flow==0` before polling `flow!=0` so stale post-match `flow` does not
  unregister on the same frame as join (see
  [battle-flow-state.md](battle-flow-state.md)).
- `ARM9Hook_Uninstall(nds, core, osdEmu)` — wired into **all three** reset blocks before the
  registry restore/reset. It clears the Core-owned dispatch entries and the NDS hook slot, so
  there is no process-global ARM9 reset step. Developer OSD posts `ARM9 hooks: unregistered`
  when the instance still had a registered hook set or the NDS hook slot was active — including
  after `emuInstance->reset()` clears the NDS slot before `OnEmuStart` runs.
- Developer builds (`MELONPRIME_ENABLE_DEVELOPER_FEATURES`): match hook install/clear posts
  `osdAddMessage` — `ARM9 hooks: registered (N PCs)` / `ARM9 hooks: unregistered` (only on
  actual hook attach/detach, not on redundant `SetMatchHooksActive(false)` no-ops).
- Same builds: patch registry posts `Patches: applied (GameJoin|ConfigReload, N entries)` and
  `Patches: restored (match end|emu stop, N entries)`. `OutOfGameFrame` apply is silent (per-frame).
- `DispatcherCallback` looks up the address's mask (`FindDispatchMask`, 1-entry cache) and calls
  each set hook's handler in a fixed priority order; side-effect hooks run unconditionally, redirect
  hooks return early once one redirects.
- **Registration is config-gated**: only hooks that can run for the current config are registered,
  because every registered PC becomes a JIT trampoline call site. A live settings toggle should call
  `NotifyConfigChanged()` / `ApplyConfigReload()` so the hook set is rebuilt, rather than keeping a
  disabled hook registered and branching out inside the handler. Developer-only hooks are forced off
  in non-developer builds.

### Per-hook module contract

Each instruction-hook module provides:

- `uint32_t Foo_GetAddresses(uint8_t romGroupIndex, uint32_t* out, uint32_t maxCount)` — fills the
  ROM-specific hook PCs, returns the count.
- a handler: side-effect `Foo_DispatchCheck(nds, romGroupIndex, arm9ExecAddr, regs)` **or**
  redirecting `bool Foo_DispatchCheckAndRedirect(nds, romGroupIndex, arm9ExecAddr, regs,
  u32& redirectExecAddr)`.
- The dispatcher only invokes a handler at that hook's own registered PCs, so re-deriving / re-matching
  the PC inside the handler is redundant — a single-site side-effect handler can ignore
  `arm9ExecAddr` (e.g. `(void)arm9ExecAddr;`); multi-site handlers still use it to select behavior.
- Standalone modules such as `MelonPrimePatchFixNoxusBladePersistence` and
  `MelonPrimePatchShadowFreezeRuntimeHook` are stateless. The dispatcher supplies the
  per-Core `romGroupIndex` and its mask supplies the feature gate; modules must not add a
  process-global config/ROM cache or a `SetState`/`ClearState`/`ResetPatchState` API.

### Shared hook-site tables

Phase 4 of Structural Refactor V2 moved hook PCs that are shared across multiple instruction-hook
modules into `MelonPrimeGameRomAddrTable.h`. Use these generated `LIST_*` arrays before adding a
module-local per-ROM table:

| Shared list | Meaning | Current consumers |
|---|---|---|
| `LIST_HookLocalPlayerPtrGlobal` | per-ROM global pointer-to-local-player address | NativeAimDelta, TransformGate, NativeZoomToggle, WeaponSwitch, DirectInvocation |
| `LIST_HookActionConsumerPc` | post-poll player action consumer PC | ImmediateInputEdgeOverlay, NativeBipedFire, NativeZoomToggle for JP/US/EU rows |
| `LIST_HookPlayerUpdateActiveCallAddr` | reliable player-update active call hook PC | WeaponSwitch, DirectInvocation |
| `LIST_HookPlayerUpdateActiveCallExpected` | original BL word expected at the active call hook | WeaponSwitch, DirectInvocation |
| `LIST_HookPlayerUpdateActiveAfter` | return PC immediately after the active call hook | WeaponSwitch, DirectInvocation |

Do not merge tables only because the numeric addresses are near each other. KR1_0 is the standing
example: `LIST_HookActionConsumerPc[KR1_0]` is `0x0200F6DC` (post-poll action consumer), while
NativeZoomToggle's KR weapon-update hook remains module-local at `0x0200D07C` because it is a
different function. Keep this distinction unless a new mphCodex audit proves otherwise.

Hook tables should have two compile-time checks where practical:

- ROM count equals `RomGroup::COUNT`.
- ADDR fields are in main RAM via `RomAddrDetail::InMainRam()`; DATA fields such as instruction
  encodings are not range-checked.

### Registered hooks (dispatch priority order)

| Hook | File | Kind | Gating |
|------|------|------|--------|
| NativeAimDelta (RegisterInjection / PostFoldWrite) | `MelonPrimePatchNativeAimDeltaHook*Version.inc` | register side-effect | developer-only; `NativeHookMode`, direct-aim path |
| LowLatencyAim | `MelonPrimePatchLowLatencyAimHook.inc` | RAM side-effect | `LowLatencyMode` ImmediateSync/MoonLike; requires `DisableMphAimSmoothing`, non-stylus |
| NativeZoomToggle | `MelonPrimePatchNativeZoomToggleHook.inc` | redirect | developer-only |
| ImmediateInputEdgeOverlay (+ NativeBipedFire) | `MelonPrimePatchImmediateInputEdgeOverlay.inc`, `MelonPrimePatchNativeBipedFireHook.inc` | RAM side-effect | developer-only |
| FixNoxusBladePersistence | `MelonPrimePatchFixNoxusBladePersistence.cpp` | RAM side-effect | `Metroid.BugFix.FixNoxusBladePersistence` |
| TransformGate | `MelonPrimePatchImmediateTransformGateHook.inc` | redirect | `AltFormTransformMethod == NewNativeGate` |
| WeaponSwitch | `MelonPrimePatchWeaponSwitchHook.inc` | redirect | `WeaponSwitchMethod == NewNative` |
| DirectInvocation | `MelonPrimePatchDirectInvocationHook.inc` | redirect | any of `AltFormTransformMethod` / `WeaponSwitchMethod` / `ZoomMethod` set to its DirectInvocation value |
| ShadowFreezeRuntimeHook | `MelonPrimePatchShadowFreezeRuntimeHook.cpp` | redirect | `Metroid.BugFix.FixShadowFreeze` |

The `*.inc` handlers are unity-included into `MelonPrimeGameInput.cpp` (see its `#include` block);
the two `.cpp` modules (`FixNoxusBladePersistence`, `ShadowFreezeRuntimeHook`) are standalone
translation units listed in `CMakeLists.txt`.

### Guest trampolines and the shared ROM code cave

A redirect hook that has to *call* a ROM routine needs real ARM code in guest memory. That code
lives in the zero-filled cave at **`0x02003E9C..0x02003FBB`** — 72 words, the only extent verified
zero across all seven ROM dumps. Trampoline RAM is outside the patch registry.

This cave is shared and now essentially full. **This table is canonical**; feature docs link here
rather than restating addresses.

| Range | Owner |
| --- | --- |
| `0x02003EA0..0x02003ED3` | WeaponSwitch (Method 1) trampoline |
| `0x02003EE0..0x02003EF3` | WeaponSwitch (Method 1) scratch |
| `0x02003F00..0x02003F2B` | NativeZoomToggle trampoline |
| `0x02003F40..0x02003F4F` | NativeZoomToggle scratch |
| `0x02003F50..0x02003FBB` | DirectInvocation (Method 2 / 3) trampoline |

Remaining free space is fragments only: `0x02003E9C` (1 word), `0x02003ED4..0x02003EDF` (3),
`0x02003EF4..0x02003EFF` (3), `0x02003F2C..0x02003F3F` (5). There is no contiguous room left for
another trampoline of any size.

Three rules apply to every module that writes into it:

1. **Assert the extent at compile time.** `MelonPrimePatchDirectInvocationHook.inc` is the worked
   example: it `static_assert`s its own range against the other modules' own constants (visible
   because the fragments share a unity parent) and against the cave end, so moving or growing any
   of them fails the build instead of silently corrupting guest code. A collision here passes every
   test and only shows up as a hang on hardware.
2. **Never hand a request to a trampoline through a PC-relative load.** The JIT constant-folds
   `ldr rX,[pc,#imm]` at compile time (`Compiler::Comp_MemAccess` → `Comp_MemLoadLiteral`, which
   emits `MOV Imm32`), and a host write straight into `NDS::MainRAM` does not invalidate the
   compiled block, so the trampoline replays whatever the first dispatch happened to store.
   Prefer writing the request into `regs[]` before returning `true` — `r0`-`r3` and `r12` are
   caller-saved across a `bl`, so a replaced call site can carry them for free. If it must go
   through memory, PC-load only the scratch **address** (a genuine constant, safe to fold) and read
   the data through that register, which is what the two older trampolines do.
3. **Author the code on the battle-runtime boundary, not on first use.**
   `HandleBattleRuntimeEnter` → `ApplyOnBattleRuntimeEnter` calls each enabled module's
   `*_IsSiteValid()` so the cave is written once, cold, per match. Writing guest code from inside
   the hook callback also invalidates JIT blocks mid-execution.

### Match lifecycle checklist for a new hook

Every registered hook is match-scoped. A new one must follow all of it, not just the parts the
dispatcher enforces:

- Install/clear happens through `ARM9Hook_SetMatchHooksActive`, driven by the battle-runtime latch.
  That latch is two-stage and the stages need not fall on the same frame: `BIT_BATTLE_RUNTIME_SEEN`
  sticks once `mode == MODE_BATTLE_RUNTIME && flow == FLOW_ACTIVE_MATCH` has been observed, and
  `BIT_BATTLE_RUNTIME_MODE` completes on the first frame after that where the local player's HP is
  not 0. Everything hung off the latch therefore lands on a spawned player rather than during the
  spawn window. An unresolved HP pointer counts as in play, so the latch cannot stall.
- **Gate the request-producing side on `BIT_BATTLE_RUNTIME_MODE` too**, not just
  `BIT_IN_GAME_INIT`. A request queued during the join/countdown otherwise survives on its TTL and
  fires on the first battle-runtime frame, before the player state it acts on is settled.
- Clear pending host-side requests on the battle-runtime edge and in the lifecycle resets
  (`ResetTransientInputState`, emu start/stop/boot, savestate reconcile).
- **Every native dispatcher has to clear the spawn barrier**, because the match latch is a
  match boundary and respawn is a player boundary. Spawn restores HP early and the same player
  runtime update falls through to the input hooks, so a native call can land inside the update that
  spawned the player. `MelonPrimeCore::IsFirstPostSpawnInput()` (defined in
  `MelonPrimeGameInput.cpp`) identifies that one hook via `player+0xE1 == configured - 1`; each
  dispatcher drops its pending request and returns false there, letting the site's original
  instruction run. Drop, never defer — replaying a pressed edge from before the respawn is the
  behaviour being prevented.
- **And the request-producing side hands the whole `player+0xE1 != 0` window to the non-native
  path.** The dispatcher barrier only sees requests that actually reach a hook; this covers the
  rest of the invulnerability countdown. The two are complementary, not alternatives.

---

## BUG FIXES section (existing UI section)

A collapsible **BUG FIXES** section is already present in the Settings tab of the MelonPrimeInputConfig dialog, located immediately above **GAMEPLAY TOGGLES**.

- Toggle button: `btnToggleBugFix`
- Section widget: `sectionBugFix` (layout: `QVBoxLayout` named `vboxBugFix`)
- Persisted key: `Metroid.UI.SectionBugFix` (default: `true`, i.e. expanded)

To add a new bug-fix checkbox, append items to `sectionBugFix`'s layout in the `.ui` file
and follow steps 5–7 above. No new `setupToggle` call is required.
