# Aim Config Reload Paths — Audit (doc only)

Status: **audit only, no behavior change**. This document exists to make the
tradeoff visible before anyone decides to unify these paths (Batch 4 of the
accelerated follow-up plan). Do not implement outcome B or C below without a
dedicated review — this area affects aim feel.

## 1. The three paths

| Path | Function | Reads | Resets residuals? | Trigger |
|---|---|---|---|---|
| Snapshot reload | `MelonPrimeCore::ReloadAimConfigFromTable(cfg)` → `ApplyAimConfigSnapshot(LoadAimConfigSnapshot(cfg))` | `AimSens`, `AimYScale`, `AimAdjust` | Yes, via `RecalcAimFixedPoint()` | `Initialize()`, `ApplyConfigReload()`, ROM detect (`DetectRomAndSetAddresses()`) |
| Sensitivity hotkey | `MelonPrimeCore::RecalcAimSensitivityCache(cfg)` | `AimSens`, `AimYScale` (**not** `AimAdjust`) | Yes, via `RecalcAimFixedPoint()` | `HandleGlobalHotkeys()` — `HK_MetroidIngameSensiUp` / `HK_MetroidIngameSensiDown` |
| Dead code | `MelonPrimeCore::ApplyAimAdjustSetting(cfg)` | `AimAdjust` | Yes, via `RecalcAimFixedPoint()` | **none** — zero call sites found (`grep -rn ApplyAimAdjustSetting src/` matches only its own declaration + definition) |

All three ultimately call the same `RecalcAimFixedPoint()`, which rebuilds
`m_aimFixedScaleX/Y`, `m_aimFixedAdjust`, `m_aimFixedSnapThresh`, and — as a
documented side effect (P-17) — zeroes `m_aimResidualX/Y` and
`m_nativeAimDeltaX/Y` because they were accumulated under the previous scale.

## 2. Exact call sites (verified 2026-07-08, HEAD after Batch 1/2/3)

```text
MelonPrimeLifecycle.cpp:115   Initialize()          → ReloadAimConfigFromTable(localCfg)
MelonPrimeLifecycle.cpp:218   ApplyConfigReload()    → ReloadAimConfigFromTable(localCfg)
MelonPrimeGameRomDetect.cpp:155 DetectRomAndSetAddresses() → ReloadAimConfigFromTable(localCfg)
MelonPrime.cpp:271            HandleGlobalHotkeys()  → RecalcAimSensitivityCache(localCfg)
```

`ApplyAimAdjustSetting` (`MelonPrime.cpp:180`, declared `MelonPrime.h:696`)
has no call sites anywhere in `src/`.

## 3. Behavior-sensitive points

- **`AimAdjust` reload gap on the hotkey path.** If a user changes `AimAdjust`
  via the Settings dialog spinbox (`ui->metroidAimAdjustSpinBox`,
  `InputConfig/MelonPrimeInputConfig.cpp:305`) and *then* presses the
  sensitivity hotkey before the dialog's own config-reload path runs, the
  hotkey path (`RecalcAimSensitivityCache`) does not re-read `AimAdjust` —
  it only touches `m_aimSensiFactor`/`m_aimCombinedY`. In practice this is
  usually moot because closing the Settings dialog with changes triggers
  `NotifyConfigChanged()` → `m_configReloadPending` → `ApplyConfigReload()`
  → `ReloadAimConfigFromTable()` on the very next `RunFrameHook`, which
  *does* pick up `AimAdjust`. The gap only matters if the hotkey fires in
  the same frame window as an unprocessed config-reload-pending flag, which
  is a narrow race, not a steady-state behavior difference.
- **`RecalcAimFixedPoint()` always resets residuals.** All three paths share
  this side effect. A user holding a slow, sub-pixel mouse movement across a
  sensitivity-hotkey press will see the fractional carry drop to zero at
  that instant — this is existing, intentional behavior (see P-17 in
  `melonprime-refactoring.md`), not something introduced by the path split.
- **`Config::Save()` ordering on the hotkey path.** `HandleGlobalHotkeys()`
  writes `AimSens` to `localCfg`, calls `Config::Save()`, *then* calls
  `RecalcAimSensitivityCache(localCfg)` — i.e. it re-reads from the same
  `Config::Table` it just wrote to (not from disk), so `Config::Save()`
  ordering relative to the recalc has no observable effect on the value
  read. It is ordered before the recalc call only because the OSD message
  and save should reflect the applied value together.
- **ROM-detect reload has no dependency on ROM data.** `ReloadAimConfigFromTable`
  at ROM-detect time only re-reads `AimSens`/`AimYScale`/`AimAdjust` from the
  config table — none of the three paths read anything ROM-address-derived.
  This call exists for defensive freshness (ensure the aim cache reflects
  current config right as a ROM becomes active), not because ROM detection
  changes the values.

## 4. Why the split exists (not a regression)

Per the note already in `MelonPrimeRuntimeConfig.h`: `AimConfigSnapshot`
predates the wider `RuntimeConfigSnapshot` (introduced in the SRP v3
immediate plan; the aim-only snapshot was Phase 10's narrower scope). The
sensitivity hotkey path (`RecalcAimSensitivityCache`) was never migrated to
use `AimConfigSnapshot` — it is older code that reads `AimSens`/`AimYScale`
directly and intentionally skips `AimAdjust` (a hotkey nudges *sensitivity*,
not the adjust curve).

## 5. Decision outcomes

**A. Keep current split, document it (this document).** Zero implementation
risk. The behavior gap in §3 is narrow and has not been reported as an
issue. This is the outcome of this audit — no code change accompanies it.

**B. Convert the hotkey path to use `AimConfigSnapshot`.** Would close the
`AimAdjust` gap in §3, but changes what the hotkey path reads (currently:
`AimSens`, `AimYScale` only) to also read `AimAdjust` on every sensitivity
keypress. Needs an explicit decision on whether that is desired — the
current code's omission of `AimAdjust` reads as intentional ("a sensitivity
hotkey should only touch sensitivity"), not an oversight, so this outcome
should not be assumed correct by default.

**C. Introduce a lighter `AimSensitivitySnapshot` (no `AimAdjust`) and route
both `RecalcAimSensitivityCache` and the `AimSens`/`AimYScale` portion of
`ReloadAimConfigFromTable` through it.** Would remove the duplicated
`sens * 0.01f` / `m_aimSensiFactor * yScale` math (currently written twice:
`MelonPrime.cpp:158-159` inside `ApplyAimConfigSnapshot` and again at
`MelonPrime.cpp:175-176` inside `RecalcAimSensitivityCache`), while
preserving the existing behavior split (hotkey never touches `AimAdjust`).
This is the option that removes duplication without changing observable
behavior — but it still touches hot-launch-adjacent code (`RecalcAimFixedPoint`
call site count would not change, only how the two callers get their
sensitivity values), so it needs its own build+audit+smoke pass, not a
doc-only change.

**No outcome is selected by this document.** B and C are not implemented
here.

## 6. Secondary finding: dead code

`MelonPrimeCore::ApplyAimAdjustSetting(Config::Table&)` (`MelonPrime.h:696`,
`MelonPrime.cpp:180-184`) has zero call sites. Its purpose — applying
`AimAdjust` on its own — is now fully covered by `ReloadAimConfigFromTable`
(which applies `AimSens` + `AimYScale` + `AimAdjust` together via
`AimConfigSnapshot`). This looks like a superseded helper from before the
snapshot split landed. Not removed in this document (doc-only scope); flagged
as a small, low-risk future cleanup candidate — a plain dead-function removal,
verified by the same `grep -rn` search used here, with no behavior change.
