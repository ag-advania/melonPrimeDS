# Aim Config Reload — Outcome C Design Note

Status: **implemented** (commit `5a49864c`, "Introduce AimSensitivitySnapshot
helper"). Outcome C has been implemented as `AimSensitivitySnapshot`,
matching the shape below exactly. Gameplay smoke (sensitivity hotkey
up/down in-game, and a Settings-dialog `AimAdjust` change followed by a
sensitivity hotkey press) remains deferred to the final release-readiness
smoke pass — it was not run as part of this implementation.

## 1. The duplication this would remove

`LoadAimConfigSnapshot` (`MelonPrimeRuntimeConfig.cpp:107-117`):

```cpp
AimConfigSnapshot LoadAimConfigSnapshot(Config::Table& cfg) noexcept
{
    AimConfigSnapshot s{};
    const float sens = static_cast<float>(cfg.GetInt(CfgKey::AimSens));
    const float yScale = static_cast<float>(cfg.GetDouble(CfgKey::AimYScale));
    s.aimSensiFactor = sens * 0.01f;
    s.aimCombinedY = s.aimSensiFactor * yScale;
    const double v = cfg.GetDouble(CfgKey::AimAdjust);
    s.aimAdjust = static_cast<float>(std::max(0.0, std::isnan(v) ? 0.0 : v));
    return s;
}
```

`MelonPrimeCore::RecalcAimSensitivityCache` (`MelonPrime.cpp:172-178`):

```cpp
void MelonPrimeCore::RecalcAimSensitivityCache(Config::Table& cfg) {
    const float sens = static_cast<float>(cfg.GetInt(CfgKey::AimSens));
    const float yScale = static_cast<float>(cfg.GetDouble(CfgKey::AimYScale));
    m_aimSensiFactor = sens * 0.01f;
    m_aimCombinedY = m_aimSensiFactor * yScale;
    RecalcAimFixedPoint();
}
```

The `sens * 0.01f` / `sensiFactor * yScale` computation is byte-identical
in both places. Outcome C factors it into one shared helper while
preserving the existing behavior split (hotkey path never touches
`AimAdjust`).

## 2. Proposed shape

New `AimSensitivitySnapshot` (narrower than `AimConfigSnapshot` — no
`aimAdjust` field, so it is architecturally incapable of reading/writing
`AimAdjust`/`m_aimAdjust`):

```cpp
// MelonPrimeRuntimeConfig.h
struct AimSensitivitySnapshot {
    float aimSensiFactor = 1.0f;
    float aimCombinedY = 1.0f;
};

[[nodiscard]] AimSensitivitySnapshot LoadAimSensitivitySnapshot(Config::Table& cfg) noexcept;
```

```cpp
// MelonPrimeRuntimeConfig.cpp
AimSensitivitySnapshot LoadAimSensitivitySnapshot(Config::Table& cfg) noexcept
{
    AimSensitivitySnapshot s{};
    const float sens = static_cast<float>(cfg.GetInt(CfgKey::AimSens));
    const float yScale = static_cast<float>(cfg.GetDouble(CfgKey::AimYScale));
    s.aimSensiFactor = sens * 0.01f;
    s.aimCombinedY = s.aimSensiFactor * yScale;
    return s;
}

AimConfigSnapshot LoadAimConfigSnapshot(Config::Table& cfg) noexcept
{
    AimConfigSnapshot s{};
    const AimSensitivitySnapshot sens = LoadAimSensitivitySnapshot(cfg);
    s.aimSensiFactor = sens.aimSensiFactor;
    s.aimCombinedY = sens.aimCombinedY;
    const double v = cfg.GetDouble(CfgKey::AimAdjust);
    s.aimAdjust = static_cast<float>(std::max(0.0, std::isnan(v) ? 0.0 : v));
    return s;
}
```

```cpp
// MelonPrime.cpp
void MelonPrimeCore::RecalcAimSensitivityCache(Config::Table& cfg) {
    const AimSensitivitySnapshot s = LoadAimSensitivitySnapshot(cfg);
    m_aimSensiFactor = s.aimSensiFactor;
    m_aimCombinedY = s.aimCombinedY;
    RecalcAimFixedPoint();
}
```

`ApplyAimConfigSnapshot` (`MelonPrime.cpp:156-162`) is untouched — it still
assigns all three fields from `AimConfigSnapshot` and calls
`RecalcAimFixedPoint()` exactly as today; only where `AimConfigSnapshot`'s
sensitivity fields come from changes (now via the shared helper instead of
being computed twice).

## 3. Answering the three required questions

**Does this change any call-site ordering?** No. All four external call
sites are untouched:

```text
MelonPrimeLifecycle.cpp:115   Initialize()          → ReloadAimConfigFromTable(localCfg)
MelonPrimeLifecycle.cpp:218   ApplyConfigReload()    → ReloadAimConfigFromTable(localCfg)
MelonPrimeGameRomDetect.cpp:155 DetectRomAndSetAddresses() → ReloadAimConfigFromTable(localCfg)
MelonPrime.cpp (HandleGlobalHotkeys) → RecalcAimSensitivityCache(localCfg)
```

None of these four call sites change what they call, in what order, or
relative to anything else in their surrounding function. Only the
*internals* of `LoadAimConfigSnapshot` and `RecalcAimSensitivityCache`
change to share a helper.

**Does `RecalcAimFixedPoint()` call count stay the same?** Yes — exactly
one call inside `ApplyAimConfigSnapshot` (unchanged) and exactly one call
inside `RecalcAimSensitivityCache` (unchanged, still the last statement).
Total call count and call sites are identical to today.

**Does `AimAdjust` remain skipped on the hotkey path?** Yes — and outcome C
makes this a **compile-time guarantee** rather than "current code happens
not to touch it": `AimSensitivitySnapshot` has no `aimAdjust` member, so
`RecalcAimSensitivityCache` cannot read or write `AimAdjust`/`m_aimAdjust`
even by accident in a future edit. This is strictly stronger than the
current state, where the omission is enforced only by the author
remembering not to add it.

## 4. Verdict

All three answers were favorable: outcome C removes a literal duplicate
computation, changes zero call-site behavior, and turns an implicit
invariant (hotkey path skips `AimAdjust`) into a structural one.

**Implemented.** Build and `audit-melonprime-srp-performance.ps1` both
passed for the implementation commit. The gameplay smoke pass described
above (sensitivity hotkey up/down in-game, and a Settings-dialog
`AimAdjust` change followed immediately by a sensitivity hotkey press, to
confirm the OSD-visible sensitivity value and actual aim feel are
unchanged) is intentionally deferred to final release-readiness smoke,
consistent with how Sites A/B and the ScreenCursorPolicy extraction were
each build+audit verified before their own later gameplay smoke passes.
