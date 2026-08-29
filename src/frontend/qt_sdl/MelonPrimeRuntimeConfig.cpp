#include "MelonPrimeRuntimeConfig.h"

#include "Config.h"
#include "MelonPrimeDef.h"

#include <algorithm>
#include <cmath>

namespace MelonPrime {

namespace {

[[nodiscard]] int NormalizeScreenSyncMode(int mode) noexcept
{
#ifdef _WIN32
    return (mode >= 0 && mode <= 2) ? mode : 0;
#else
    return (mode == 1) ? 1 : 0;
#endif
}

constexpr int64_t kAimOneFp = 1LL << 14;
constexpr int64_t kMorphBoostMaxRequiredMovement = 46339;

[[nodiscard]] int32_t CalculateMorphBoostSwipeThresholdSq(int requiredMovement) noexcept
{
    // MELONPRIME_MORPH_BOOST_MODE_CONTROLS_V14
    // Disablement belongs to the parent boolean. The user-facing distance is
    // always 1..46339 and is squared once for the custom raw hot path.
    const int64_t movement = std::clamp<int64_t>(
        requiredMovement, 1, kMorphBoostMaxRequiredMovement);
    return static_cast<int32_t>(movement * movement);
}

[[nodiscard]] uint16_t EncodeMphSensitivity(double sensitivity) noexcept
{
    constexpr double kBaseValue = 2457.0; // 0x0999
    constexpr double kStepValue = 409.0;  // 0x0199
    const double value = kBaseValue + (sensitivity - 1.0) * kStepValue;
    return static_cast<uint16_t>(std::min(
        static_cast<uint32_t>(value + 0.5), 0xFFFFu));
}

} // namespace

RuntimeConfigSnapshot LoadRuntimeConfigSnapshot(Config::Table& cfg) noexcept
{
    RuntimeConfigSnapshot s{};
    s.aimConfig = LoadAimConfigSnapshot(cfg);

    s.joy2Key = cfg.GetBool(CfgKey::Joy2Key);
    s.snapTap = cfg.GetBool(CfgKey::SnapTap);
    s.stylusMode = cfg.GetBool(CfgKey::StylusMode);

    s.disableMphAimSmoothing = cfg.GetBool(CfgKey::DisableMphAimSmoothing);
    s.aimAccumulator = cfg.GetBool(CfgKey::AimAccumulator);

    s.nativeAimHookMode = static_cast<int8_t>(cfg.GetInt(CfgKey::NativeAimHookMode));
#ifndef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    s.nativeAimHookMode = 0;
#endif
    s.enableNativeAimDeltaHook = (s.nativeAimHookMode != 0);

    int lowLatencyAimMode =
        std::clamp(cfg.GetInt(CfgKey::LowLatencyAimMode),
                   LowLatencyAimMode::Off,
                   LowLatencyAimMode::InstantAimFollow);
    // InstantAimFollow is the legacy value for the independent FPS camera
    // lock. Keep it distinct from the timing modes; its standalone patch is
    // developer-gated in MelonPrimePatchFpsCameraLock.cpp.
    if (!s.disableMphAimSmoothing
        && lowLatencyAimMode != LowLatencyAimMode::InstantAimFollow)
        lowLatencyAimMode = LowLatencyAimMode::Off;
    s.lowLatencyAimMode = static_cast<int8_t>(lowLatencyAimMode);

    s.moonLikeAimNormalStepQ12 = std::clamp(
        cfg.GetInt(CfgKey::MoonLikeAimNormalStepQ12), 1, 8192);
    s.moonLikeAimFastStepQ12 = std::clamp(
        cfg.GetInt(CfgKey::MoonLikeAimFastStepQ12), 1, 8192);
    s.moonLikeAimFastThresholdQ12 = std::clamp(
        cfg.GetInt(CfgKey::MoonLikeAimFastThresholdQ12), 1, 8192);

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    s.immediateInputEdgeOverlay = cfg.GetBool(CfgKey::ImmediateInputEdgeOverlay);
#else
    s.immediateInputEdgeOverlay = false;
#endif

    s.directAltFormTransform = cfg.GetBool(CfgKey::DirectAltFormTransform);

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    s.nativeBipedFire =
        cfg.GetInt(CfgKey::BipedFireMethod) != BipedFireMethod::LegacyInput;
#else
    s.nativeBipedFire = false;
#endif

    const int zoomInputMethod = cfg.GetInt(CfgKey::ZoomInputMethod);
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    s.nativeZoomToggle =
        zoomInputMethod == ZoomInputMethod::NewNativeToggle;
#else
    s.nativeZoomToggle = false;
#endif

    const int zoomAimScalePct = std::clamp(cfg.GetInt(CfgKey::ZoomAimScalePct), 10, 300);
    s.zoomAimScaleQ14 = static_cast<uint32_t>(
        (static_cast<int64_t>(zoomAimScalePct) * kAimOneFp + 50) / 100);
    s.zoomAimScaleEnable =
        cfg.GetBool(CfgKey::ZoomAimScaleEnable)
        && s.zoomAimScaleQ14 != static_cast<uint32_t>(kAimOneFp);

    const int morphBoostRequiredMovement = cfg.GetInt(CfgKey::MorphBoostSwipeDistance);
    // Legacy V12/V13 migration: before the explicit parent existed, distance 0
    // meant disabled. Preserve that state only when the new key is absent.
    s.morphBoostSwipeEnabled = cfg.HasKey(CfgKey::MorphBoostSwipeEnabled)
        ? cfg.GetBool(CfgKey::MorphBoostSwipeEnabled)
        : morphBoostRequiredMovement != 0;
    s.morphBoostCustomRawThreshold =
        cfg.GetBool(CfgKey::MorphBoostCustomRawThreshold);
    s.morphBoostAssistThresholdSq = CalculateMorphBoostSwipeThresholdSq(
        morphBoostRequiredMovement <= 0 ? 90 : morphBoostRequiredMovement); // MELONPRIME_MORPH_BOOST_MODE_CONTROLS_V14

#ifdef MELONPRIME_DS
    s.outOfGamePatches.fixWifiEnabled = cfg.GetBool(CfgKey::WifiBitset);
    s.outOfGamePatches.useFirmwareLanguageEnabled =
        cfg.GetBool(CfgKey::UseFirmwareLanguage);
    s.outOfGamePatches.expandStageMatrixEnabled =
        cfg.GetBool(CfgKey::ExpandStageMatrix);
    s.outOfGamePatches.expandStageMatrixExtraEnabled =
        cfg.GetBool(CfgKey::ExpandStageMatrixExtra);
    s.nativeWeaponSwitch =
        cfg.GetInt(CfgKey::WeaponSwitchMethod) != WeaponSwitchMethod::LegacyTouch;
    s.fixShadowFreeze = cfg.GetBool(CfgKey::FixShadowFreeze);
    s.fixNoxusBladePersistence =
        cfg.GetBool(CfgKey::FixNoxusBladePersistence);
#endif

    s.screenSyncMode = NormalizeScreenSyncMode(cfg.GetInt(CfgKey::ScreenSyncMode));
    s.menuGameSettings = LoadMenuGameSettingsSnapshot(cfg);

    return s;
}

AimSensitivitySnapshot LoadAimSensitivitySnapshot(Config::Table& cfg) noexcept
{
    AimSensitivitySnapshot s{};
    s.aimSensitivity = cfg.GetInt(CfgKey::AimSens);
    s.aimYScale = static_cast<float>(cfg.GetDouble(CfgKey::AimYScale));
    s.aimSensiFactor = static_cast<float>(s.aimSensitivity) * 0.01f;
    s.aimCombinedY = s.aimSensiFactor * s.aimYScale;
    return s;
}

AimConfigSnapshot LoadAimConfigSnapshot(Config::Table& cfg) noexcept
{
    AimConfigSnapshot s{};
    const AimSensitivitySnapshot sens = LoadAimSensitivitySnapshot(cfg);
    s.aimSensitivity = sens.aimSensitivity;
    s.aimYScale = sens.aimYScale;
    s.aimSensiFactor = sens.aimSensiFactor;
    s.aimCombinedY = sens.aimCombinedY;
    const double v = cfg.GetDouble(CfgKey::AimAdjust);
    s.aimAdjust = static_cast<float>(std::max(0.0, std::isnan(v) ? 0.0 : v));
    return s;
}

MenuGameSettingsSnapshot LoadMenuGameSettingsSnapshot(Config::Table& cfg) noexcept
{
    MenuGameSettingsSnapshot s{};
    s.headphoneEnabled = cfg.GetBool(CfgKey::Headphone);
    s.mphSensitivityValue = EncodeMphSensitivity(
        cfg.GetDouble(CfgKey::MphSens));
    s.dataUnlockEnabled = cfg.GetBool(CfgKey::DataUnlock);
    s.useFirmwareName = cfg.GetBool(CfgKey::UseFwName);

    s.hunterApply = cfg.GetBool(CfgKey::HunterApply);
    const int hunter = std::clamp(cfg.GetInt(CfgKey::HunterSel), 0, 6);
    s.hunterBits = static_cast<uint8_t>(hunter * 0x08);

    const int color = cfg.GetInt(CfgKey::LicColorSel);
    s.licenseColorApply = cfg.GetBool(CfgKey::LicColorApply)
        && color >= 0 && color <= 2;
    s.licenseColorBits = static_cast<uint8_t>(color * 0x40);

    s.sfxApply = cfg.GetBool(CfgKey::SfxVolApply);
    s.sfxSteps = static_cast<uint8_t>(
        std::clamp(cfg.GetInt(CfgKey::SfxVol), 0, 9));
    s.musicApply = cfg.GetBool(CfgKey::MusicVolApply);
    s.musicSteps = static_cast<uint8_t>(
        std::clamp(cfg.GetInt(CfgKey::MusicVol), 0, 9));
    return s;
}

} // namespace MelonPrime
