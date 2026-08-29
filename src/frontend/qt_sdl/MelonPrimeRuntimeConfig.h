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

// AimSens/AimYScale derivation only (no AimAdjust). The raw values are
// retained so the EmuThread hotkey can update sensitivity without reading or
// writing Config::Table. AimAdjust remains structurally excluded.
struct AimSensitivitySnapshot {
    int aimSensitivity = 1;
    float aimYScale = 1.0f;
    float aimSensiFactor = 1.0f;
    float aimCombinedY = 1.0f;
};

// Complete aim snapshot used by the normal config reload boundary.
struct AimConfigSnapshot {
    int aimSensitivity = 1;
    float aimYScale = 1.0f;
    float aimSensiFactor = 0.f;
    float aimCombinedY = 0.f;
    float aimAdjust = 0.f;
};

// Values used by the focused out-of-game reconciliation path. Config::Table
// is read only at the cold load/apply boundary; guest RAM is still sampled at
// reconciliation time because the game may rewrite these fields itself.
struct MenuGameSettingsSnapshot {
    bool headphoneEnabled = false;
    uint16_t mphSensitivityValue = 2457;
    bool dataUnlockEnabled = false;
    bool useFirmwareName = false;
    bool hunterApply = false;
    uint8_t hunterBits = 0;
    bool licenseColorApply = false;
    uint8_t licenseColorBits = 0;
    bool sfxApply = false;
    uint8_t sfxSteps = 9;
    bool musicApply = false;
    uint8_t musicSteps = 9;
};

struct OutOfGamePatchSnapshot {
    bool fixWifiEnabled = true;
    bool useFirmwareLanguageEnabled = false;
    uint8_t firmwareLanguageBits = 0;
    bool expandStageMatrixEnabled = false;
    bool expandStageMatrixExtraEnabled = false;
};

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

    bool nativeZoomToggle = false;

    bool zoomAimScaleEnable = false;
    uint32_t zoomAimScaleQ14 = 1u << 14;

    // Mouse-mode Morph Ball swipe hierarchy. Parent defaults ON; custom raw
    // threshold defaults OFF, preserving the game's internal swipe amount.
    // MELONPRIME_MORPH_BOOST_MODE_CONTROLS_V14
    bool morphBoostSwipeEnabled = true;
    bool morphBoostCustomRawThreshold = false;
    int32_t morphBoostAssistThresholdSq = 0x1FA4;

#ifdef MELONPRIME_DS
    bool nativeWeaponSwitch = false;
    bool fixShadowFreeze = false;
    bool fixNoxusBladePersistence = false;
#endif

    int screenSyncMode = 0;

    MenuGameSettingsSnapshot menuGameSettings{};
    OutOfGamePatchSnapshot outOfGamePatches{};

    // MELONPRIME_PHASE5_CONFIG_USAGE_V1
    // Aim config now crosses the same Load/Apply snapshot boundary as the
    // remaining runtime settings. No second Config::Table read is needed.
    AimConfigSnapshot aimConfig{};
};

// Pure: reads `cfg`, clamps/derives values, applies developer-feature gating.
// Does not touch MelonPrimeCore state — see the Load/Apply boundary comment above.
RuntimeConfigSnapshot LoadRuntimeConfigSnapshot(Config::Table& cfg) noexcept;

// Pure: reads `cfg` only. Does not touch MelonPrimeCore state.
[[nodiscard]] AimSensitivitySnapshot LoadAimSensitivitySnapshot(Config::Table& cfg) noexcept;

// Pure: reads `cfg` only. Does not touch MelonPrimeCore state.
AimConfigSnapshot LoadAimConfigSnapshot(Config::Table& cfg) noexcept;

// Pure: reads menu/game settings from `cfg`, clamps/encodes them, and does not
// touch MelonPrimeCore state.
MenuGameSettingsSnapshot LoadMenuGameSettingsSnapshot(Config::Table& cfg) noexcept;

} // namespace MelonPrime
