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

} // namespace

RuntimeConfigSnapshot LoadRuntimeConfigSnapshot(Config::Table& cfg) noexcept
{
    RuntimeConfigSnapshot s{};

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
    if (lowLatencyAimMode == LowLatencyAimMode::Off
        && cfg.GetBool(CfgKey::InstantAimFollow))
        lowLatencyAimMode = LowLatencyAimMode::ImmediateSync;
#ifndef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    if (lowLatencyAimMode == LowLatencyAimMode::InstantAimFollow)
        lowLatencyAimMode = LowLatencyAimMode::ImmediateSync;
#endif
    if (!s.disableMphAimSmoothing)
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
    s.newZoomInputMethod =
        zoomInputMethod == ZoomInputMethod::NewPresetBinding;
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

#ifdef MELONPRIME_DS
    s.nativeWeaponSwitch =
        cfg.GetInt(CfgKey::WeaponSwitchMethod) != WeaponSwitchMethod::LegacyTouch;
#endif

    s.screenSyncMode = NormalizeScreenSyncMode(cfg.GetInt(CfgKey::ScreenSyncMode));

    return s;
}

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

} // namespace MelonPrime
