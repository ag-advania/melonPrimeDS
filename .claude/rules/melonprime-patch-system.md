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
#include "Config.h"
#include "NDS.h"

namespace MelonPrime {

// Per-ROM base addresses. Use 0xFFFFFFFFu for ROM versions where the patch
// does not apply (already fixed upstream, or different code layout).
// ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
static constexpr uint32_t kBase[7] = {
    0x0206XXXX, // JP1_0
    0xFFFFFFFF, // JP1_1  (not applicable)
    0x0206XXXX, // US1_0
    0xFFFFFFFF, // US1_1  (not applicable)
    0x0206XXXX, // EU1_0
    0xFFFFFFFF, // EU1_1  (not applicable)
    0xFFFFFFFF, // KR1_0  (not applicable)
};

// Patch entries: byte offset from base, value to write (apply), original value (revert).
// If all ROM versions share the same instruction values (common), store offsets+values once.
// If values differ per ROM, use a per-ROM table instead.
struct PatchWord { uint32_t offset, applyVal, revertVal; };
static constexpr PatchWord kWords[] = {
    {0x000u, 0xXXXXXXXX, 0xXXXXXXXX},
    // ...
};

static bool s_applied = false;

void Foo_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex)
{
    if (s_applied) return;
    if (!cfg.GetBool("Metroid.BugFix.Foo")) return;
    if (romGroupIndex >= 7 || kBase[romGroupIndex] == 0xFFFFFFFFu) return;
    const uint32_t base = kBase[romGroupIndex];
    for (const auto& w : kWords)
        nds->ARM9Write32(base + w.offset, w.applyVal);
    s_applied = true;
}

void Foo_ResetPatchState()
{
    s_applied = false;
}

// Optional: mid-session restore (e.g. on game exit)
// void Foo_RestoreOnce(melonDS::NDS* nds, uint8_t romGroupIndex)
// {
//     if (!s_applied || kBase[romGroupIndex] == 0xFFFFFFFFu) return;
//     const uint32_t base = kBase[romGroupIndex];
//     for (const auto& w : kWords)
//         nds->ARM9Write32(base + w.offset, w.revertVal);
//     s_applied = false;
// }

} // namespace MelonPrime

#endif // MELONPRIME_DS
```

---

## Checklist for a new patch

### 1. Patch files

- [ ] Create `MelonPrimePatchFoo.h` (see header pattern above)
- [ ] Create `MelonPrimePatchFoo.cpp` (see implementation pattern above)
- [ ] Add `#include "MelonPrimePatchFoo.h"` to `MelonPrimePatch.h`
- [ ] Add `MelonPrimePatchFoo.cpp` to `CMakeLists.txt` inside the `MELONPRIME_DS` source list (around line 88–91)

### 2. Apply call site — `MelonPrime.cpp`

Choose the apply site based on what the patch targets:

#### A. Standard: `HandleGameJoinInit()` — once per in-game join (cold path)

Used when the patch writes ARM9 code that persists until the game resets.
Add after the existing `OsdColor_ApplyOnce` call:

```cpp
#ifdef MELONPRIME_DS
    InGameAspectRatio_ApplyOnce(emuInstance, localCfg, m_currentRom);
    OsdColor_ApplyOnce(emuInstance, localCfg, m_currentRom);
    Foo_ApplyOnce(emuInstance->getNDS(), localCfg, m_currentRom.romGroupIndex);
#endif
```

#### B. Per-frame in-game: `isInGame` block in `RunFrameHook`

Used when the patch needs re-evaluation every frame while the game is running
(e.g. OsdColor which varies with game state).

#### C. Per-frame outside game: `!isInGame` block in `RunFrameHook`

Used when the patch targets data that is loaded/unloaded by the game's menu system
and can be reset by screen transitions. The patch must include its own loaded-state
guard so it is a no-op when the target data is not present.

In the `!isInGame && focused` block (alongside `FixWifi_ApplyOnce` and
`UseFirmwareLanguage_ApplyOnce`):

```cpp
#ifdef MELONPRIME_DS
    {
        melonDS::NDS* const nds = emuInstance->getNDS();
        FixWifi_ApplyOnce(nds, localCfg, m_currentRom.romGroupIndex);
        UseFirmwareLanguage_ApplyOnce(nds, localCfg, m_currentRom.romGroupIndex, m_currentRom.isInAdventure);
        Foo_ApplyIfLoaded(nds, localCfg, m_currentRom.romGroupIndex);  // ← pattern C
    }
#endif
```

For pattern C, `ResetPatchState()` is a no-op (the guard re-detects each call; there is no
persistent `s_applied` flag). The function signature is `Foo_ApplyIfLoaded` rather than
`Foo_ApplyOnce` to signal this distinction.

### 3. Reset call sites — `MelonPrime.cpp`

**Three** `#ifdef MELONPRIME_DS` reset blocks exist. Add `Foo_ResetPatchState();` to **all three**:

1. Inside `OnEmuStart` / soft-reset path (search for `ReloadConfigFlags()` just below the block)
2. Inside `ResetRuntimeStateForBoot()` (search for the second reset block before `InputReset()`)
3. Inside `OnEmuStop()`

```cpp
#ifdef MELONPRIME_DS
    InGameAspectRatio_ResetPatchState();
    OsdColor_ResetPatchState();
    FixWifi_ResetPatchState();
    Foo_ResetPatchState();   // ← add here in all three blocks
#endif
```

Missing any one of the three blocks causes stale patch state to survive across stop/reset cycles.

### 4. Settings UI — `MelonPrimeInputConfig.ui`

Add a `QCheckBox` (and optional `QLabel` description) inside an existing or new collapsible section.
For bug fixes, the **BUG FIXES** section (`btnToggleBugFix` / `sectionBugFix`) already exists
immediately above the GAMEPLAY TOGGLES section in the Settings tab.

Visible MelonPrime settings text must also be registered for English/Japanese localization in
`src/frontend/qt_sdl/MelonPrimeLocalization.h`:

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

Then add the checkbox label and description text to `MelonPrimeLocalization.h`. For the description
above, prefer the `lblMetroidFooDesc` object-name mapping.

### 5. Settings wiring — `MelonPrimeInputConfig.cpp`

In `setupSensitivityAndToggles(instcfg)`, add:

```cpp
// Bug fixes
ui->cbMetroidFoo->setChecked(instcfg.GetBool("Metroid.BugFix.Foo"));
```

The BUG FIXES section toggle is already registered in `setupCollapsibleSections`.
No extra `setupToggle` call is needed for new checkboxes inside the existing section.

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
ui->cbMetroidFoo->setChecked(instcfg.GetBool("Metroid.BugFix.Foo"));
ui->cbMetroidFooChild->setChecked(instcfg.GetBool("Metroid.BugFix.FooChild"));
ui->cbMetroidFooChild->setEnabled(ui->cbMetroidFoo->isChecked());
```

Qt's `QMetaObject::connectSlotsByName` (called inside `ui->setupUi(this)`) automatically connects `on_cbMetroidFoo_stateChanged` to the widget's `stateChanged` signal. No manual `connect()` call is needed or wanted.

### 6. Save — `MelonPrimeInputConfigConfig.cpp`

In `saveConfig()`, inside the "Bug fixes" block:

```cpp
instcfg.SetBool("Metroid.BugFix.Foo", ui->cbMetroidFoo->checkState() == Qt::Checked);
```

### 7. Config defaults — `Config.cpp`

In `DefaultBools`, add inside the `#ifdef MELONPRIME_DS` block
(near other `Metroid.BugFix.*` or `Metroid.Visual.*` entries):

```cpp
{"Instance*.Metroid.BugFix.Foo", true},   // or false if opt-in
```

If the config key uses `GetInt` or `GetDouble`, add it to `DefaultInts` / `DefaultDoubles` instead
(see repo-architecture.md "Default value type classification" for the type-list rule).

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

| Patch | Files | Apply site | Notes |
|-------|-------|-----------|-------|
| In-game aspect ratio | `MelonPrimePatchAspectRatio.*` | `HandleGameJoinInit` | Uses `MainRAM` read to detect/guard; `InGameAspectRatio_ResetPatchState` on stop/reset |
| OSD color | `MelonPrimePatchOsdColor.*` | `HandleGameJoinInit` + per-frame `isInGame` block | `OsdColor_RestoreOnce` on game exit; `OsdColor_InvalidatePatch` on settings save |
| No-HUD | `MelonPrimePatchNoHud.*` | Called from Custom HUD render path | Per-frame apply/restore depending on HUD state |
| No double-tap jump | `MelonPrimePatchNoDoubleTapJump.*` | `MelonPrimeGameWeapon.cpp` (transient, wraps `FrameAdvanceTwice`) | Not persistent; applied/restored around weapon-switch frames only |
| Wi-Fi bitset fix | `MelonPrimePatchFixWifi.*` | `HandleGameJoinInit` | JP1_0 / US1_0 / EU1_0 only; 51-word patch; `FixWifi_ResetPatchState` on stop/reset |
| Stage matrix expansion | `MelonPrimePatchExpandStageMatrix.*` | `!isInGame` per-frame block (pattern C) | Writes RAM data bytes, not ARM code; self-guarded via strict 3-point loaded-state check; `ResetPatchState` is a no-op; base (5 cells) + extra (9 cells) split across two config keys |
| Low HP warning | `MelonPrimePatchLowHpWarning.*` | `HandleGameJoinInit` | `LowHpWarning_ResetPatchState` on stop/reset |
| Use firmware language | `MelonPrimePatchUseFirmwareLanguage.*` | `!isInGame && focused` block | Adventure-aware; applied in menus |
| Instant aim follow | `MelonPrimePatchInstantAimFollow.*` | `ApplyConfigReload` + game-join | `_RestoreOnce` on game leave; `_ResetPatchState` on stop/reset. (Distinct from the `LowLatencyMode` ImmediateSync/MoonLike instruction hook.) |
| Show headshot online | `MelonPrimePatchShowHeadshotOnline.*` | `ApplyConfigReload` + game-join | `_RestoreOnce` on game leave |
| Show enemy HP meter online | `MelonPrimePatchShowEnemyHpMeterOnline.*` | `ApplyConfigReload` + game-join | `_RestoreOnce` on game leave |
| Disable double-damage multiplier | `MelonPrimePatchDisableDoubleDamageMultiplier.*` | `ApplyConfigReload` + game-join | `_RestoreOnce` on game leave; pairs with Damage-Notify-Purple |
| No picking up specific items | `MelonPrimePatchNoPickingUpSpecificItems.*` | `ApplyConfigReload` + game-join | `_RestoreOnce` on game leave |

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
  `SetARM9InstructionHook` resets the JIT block cache when the address list changes. Non-hooked
  instructions cost **zero**; each hooked PC pays a `RegCache.Flush` + call when executed.
- **Interpreter path:** every instruction runs `ARM9InstructionHookAddressMatches`, a hash-mask
  early-out (`1u << ((addr>>2)&31)`) followed by a short linear scan.

### Central dispatcher — `MelonPrimeArm9Hook.cpp`

Owns the single hook slot and fans out to all registered MelonPrime hooks.

- `ARM9Hook_Install(nds, cfg, romGroupIndex, core)` — called after ROM detection
  (`DetectRomAndSetAddresses`) and again on every `ApplyConfigReload`, so a settings change
  re-registers the active hook set. For each **enabled** hook it calls the module's
  `Foo_GetAddresses(romGroupIndex, out, max)` and registers those PCs with a per-hook dispatch-mask
  bit (`AddDispatchAddress` ORs masks when two hooks share a PC), then calls `SetARM9InstructionHook`
  once with the union of addresses.
- `ARM9Hook_Uninstall(nds)` and `ARM9Hook_ResetPatchState()` — wired into **all three** reset
  blocks (the same blocks as the write-patch `*_ResetPatchState` calls; see §3).
- `DispatcherCallback` looks up the address's mask (`FindDispatchMask`, 1-entry cache) and calls
  each set hook's handler in a fixed priority order; side-effect hooks run unconditionally, redirect
  hooks return early once one redirects.
- **Registration is config-gated**: only hooks that can run for the current config are registered,
  because every registered PC becomes a JIT trampoline call site. Developer-only hooks are forced
  off in non-developer builds.

### Per-hook module contract

Each instruction-hook module provides:

- `uint32_t Foo_GetAddresses(uint8_t romGroupIndex, uint32_t* out, uint32_t maxCount)` — fills the
  ROM-specific hook PCs, returns the count.
- a handler: side-effect `Foo_DispatchCheck(nds, arm9ExecAddr, regs)` **or** redirecting
  `bool Foo_DispatchCheckAndRedirect(nds, arm9ExecAddr, regs, u32& redirectExecAddr)`.
- The dispatcher only invokes a handler at that hook's own registered PCs, so re-deriving / re-matching
  the PC inside the handler is redundant — a single-site side-effect handler can ignore
  `arm9ExecAddr` (e.g. `(void)arm9ExecAddr;`); multi-site handlers still use it to select behavior.
- Modules with their own config/ROM cache add `Foo_SetState` / `Foo_ClearState` /
  `Foo_ResetPatchState` (e.g. `MelonPrimePatchFixNoxusBladePersistence`,
  `MelonPrimePatchShadowFreezeRuntimeHook`), driven by `ARM9Hook_Install/Uninstall/ResetPatchState`.

### Registered hooks (dispatch priority order)

| Hook | File | Kind | Gating |
|------|------|------|--------|
| NativeAimDelta (RegisterInjection / PostFoldWrite) | `MelonPrimePatchNativeAimDeltaHook*Version.inc` | register side-effect | developer-only; `NativeHookMode`, direct-aim path |
| LowLatencyAim | `MelonPrimePatchLowLatencyAimHook.inc` | RAM side-effect | `LowLatencyMode` ImmediateSync/MoonLike; requires `DisableMphAimSmoothing`, non-stylus |
| NativeZoomToggle | `MelonPrimePatchNativeZoomToggleHook.inc` | redirect | developer-only |
| NativeBipedFire | `MelonPrimePatchNativeBipedFireHook.inc` | redirect | developer-only |
| ImmediateInputEdgeOverlay | `MelonPrimePatchImmediateInputEdgeOverlay.inc` | side-effect | developer-only |
| FixNoxusBladePersistence | `MelonPrimePatchFixNoxusBladePersistence.cpp` | RAM side-effect | `Metroid.BugFix.FixNoxusBladePersistence` |
| TransformGate | `MelonPrimePatchImmediateTransformGateHook.inc` | redirect | `DirectAltFormTransform` |
| WeaponSwitch | `MelonPrimePatchWeaponSwitchHook.inc` | redirect | `WeaponSwitchMethod != LegacyTouch` |
| ShadowFreezeRuntimeHook | `MelonPrimePatchShadowFreezeRuntimeHook.cpp` | redirect | **always registered**; the handler checks a cached config bit so the quick-menu toggle works live without a JIT block-cache reset |

The `*.inc` handlers are unity-included into `MelonPrimeGameInput.cpp` (see its `#include` block);
the two `.cpp` modules (`FixNoxusBladePersistence`, `ShadowFreezeRuntimeHook`) are standalone
translation units listed in `CMakeLists.txt`.

---

## BUG FIXES section (existing UI section)

A collapsible **BUG FIXES** section is already present in the Settings tab of the MelonPrimeInputConfig dialog, located immediately above **GAMEPLAY TOGGLES**.

- Toggle button: `btnToggleBugFix`
- Section widget: `sectionBugFix` (layout: `QVBoxLayout` named `vboxBugFix`)
- Persisted key: `Metroid.UI.SectionBugFix` (default: `true`, i.e. expanded)

To add a new bug-fix checkbox, append items to `sectionBugFix`'s layout in the `.ui` file
and follow steps 5–7 above. No new `setupToggle` call is required.
