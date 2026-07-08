#pragma once

#include <cstdint>

namespace Config {
class Table;
}

namespace MelonPrime {

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

    int screenSyncMode = 0;
};

RuntimeConfigSnapshot LoadRuntimeConfigSnapshot(Config::Table& cfg) noexcept;

} // namespace MelonPrime
