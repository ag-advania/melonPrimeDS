#pragma once

#include <cstdint>

namespace Config {
class Table;
}

namespace MelonPrime {

// Load/Apply boundary (see melonprime-srp-performance-contract.md):
//   Load*ConfigSnapshot(Config::Table&)   — pure read + clamp + developer-gate.
//     Reads only the passed-in Config::Table. Must never read or write
//     MelonPrimeCore member state, so it stays safe to call from cold paths
//     (Initialize, ApplyConfigReload, ROM detect) in any order.
//   MelonPrimeCore::Apply*ConfigSnapshot(const *ConfigSnapshot&) — the only
//     place that writes the loaded values into MelonPrimeCore. May have
//     runtime side effects (residual resets, fixed-point recalculation,
//     clearing pending hook/toggle state when a feature is disabled) — see
//     the per-function comments in MelonPrimeLifecycle.cpp / MelonPrime.cpp.
// Keep this split when adding new runtime config fields: clamp/derive in the
// snapshot struct's Load function, apply side effects in the Apply function.

struct RuntimeConfigSnapshot {
    bool joy2Key = false;
    bool snapTap = false;
    bool stylusMode = false;

    bool disableMphAimSmoothing = false;
    bool aimAccumulator = false;

    int8_t nativeAimHookMode = 0;
    bool enableNativeAimDeltaHook = false;

    int8_t lowLatencyAimMode = 0;
    int32_t moonLikeAimNormalStepQ12 = 0x0165;
    int32_t moonLikeAimFastStepQ12 = 0x058F;
    int32_t moonLikeAimFastThresholdQ12 = 0x042E;

    bool immediateInputEdgeOverlay = false;
    bool directAltFormTransform = false;
    bool nativeBipedFire = false;

    bool newZoomInputMethod = false;
    bool nativeZoomToggle = false;

    bool zoomAimScaleEnable = false;
    uint32_t zoomAimScaleQ14 = 1u << 14;

#ifdef MELONPRIME_DS
    bool nativeWeaponSwitch = false;
#endif

    int     screenSyncMode = 0;
};

// Aim sensitivity/adjust only. Loaded/applied via
// MelonPrimeCore::ReloadAimConfigFromTable(cfg), called from Initialize(),
// ApplyConfigReload(), and ROM detect. Separate from RuntimeConfigSnapshot
// because it predates the wider snapshot (Phase 10 of the SRP refactor
// scoped only the aim-sensitivity fields out of the ad-hoc reload code that
// existed at the time).
//
// Note: the in-game sensitivity hotkey path
// (MelonPrimeCore::RecalcAimSensitivityCache) does *not* go through this
// snapshot — it re-reads AimSens/AimYScale directly and skips AimAdjust.
// That is pre-existing, out of scope for this comment-only pass; do not
// "fix" it here.
struct AimConfigSnapshot {
    float aimSensiFactor = 0.f;
    float aimCombinedY = 0.f;
    float aimAdjust = 0.f;
};

// Pure: reads `cfg`, clamps/derives values, applies developer-feature gating.
// Does not touch MelonPrimeCore state — see the Load/Apply boundary comment above.
RuntimeConfigSnapshot LoadRuntimeConfigSnapshot(Config::Table& cfg) noexcept;

// Pure: reads `cfg` only. Does not touch MelonPrimeCore state.
AimConfigSnapshot LoadAimConfigSnapshot(Config::Table& cfg) noexcept;

} // namespace MelonPrime
