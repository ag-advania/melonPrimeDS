#ifndef MELON_PRIME_DEF_H
#define MELON_PRIME_DEF_H

#include <cstdint>

namespace MelonPrime {

    // Global state (set by EmuInstance)
    extern uint32_t globalChecksum;    // header + ARM9 + ARM7 CRC32 (variant label / fallback)
    extern uint32_t globalGameCode;    // NDS header gameCode @0x0C, packed via GameCodeAsU32()
    extern uint8_t  globalRomVersion;  // NDS header ROM revision @0x1E (0 = 1.0, 1 = 1.1)
    extern bool isRomDetected;

    // =========================================================================
    // Config key string constants — avoid repeated string construction per frame
    // =========================================================================
    namespace CfgKey {
        inline constexpr const char* Joy2Key        = "Metroid.Apply.joy2KeySupport";
        inline constexpr const char* SnapTap         = "Metroid.Operation.SnapTap";
        inline constexpr const char* StylusMode      = "Metroid.Enable.stylusMode";
        inline constexpr const char* AimSens         = "Metroid.Sensitivity.Aim";
        inline constexpr const char* AimYScale       = "Metroid.Sensitivity.AimYAxisScale";
        inline constexpr const char* AimAdjust       = "Metroid.Aim.Adjust";
        inline constexpr const char* ZoomAimScaleEnable = "Metroid.Aim.ZoomScale.Enable";
        inline constexpr const char* ZoomAimScalePct = "Metroid.Aim.ZoomScale.Percent";
        inline constexpr const char* DisableMphAimSmoothing = "Metroid.Aim.Disable.MphAimSmoothing";
        inline constexpr const char* AimAccumulator = "Metroid.Aim.Enable.Accumulator";
        inline constexpr const char* NativeAimHookMode  = "Metroid.Aim.NativeHookMode"; // 0=off 1=RegisterInject 2=FoldDerived
        inline constexpr const char* InstantAimFollow = "Metroid.Aim.Enable.InstantAimFollow";
        inline constexpr const char* LowLatencyAimMode = "Metroid.Aim.LowLatencyMode";
        inline constexpr const char* MoonLikeAimNormalStepQ12 = "Metroid.Aim.MoonLikeAimNormalStepQ12";
        inline constexpr const char* MoonLikeAimFastStepQ12 = "Metroid.Aim.MoonLikeAimFastStepQ12";
        inline constexpr const char* MoonLikeAimFastThresholdQ12 = "Metroid.Aim.MoonLikeAimFastThresholdQ12";
        inline constexpr const char* ImmediateInputEdgeOverlay = "Metroid.Input.Enable.ImmediateInputEdgeOverlay";
        inline constexpr const char* DirectAltFormTransform    = "Metroid.Input.Enable.DirectAltFormTransform";
        inline constexpr const char* WeaponSwitchMethod        = "Metroid.Input.WeaponSwitchMethod"; // 0=Legacy touch 1=New native
        inline constexpr const char* BipedFireMethod           = "Metroid.Input.BipedFireMethod"; // 0=Legacy input 1=New native edge
        inline constexpr const char* ZoomInputMethod           = "Metroid.Input.ZoomMethod"; // 0=Legacy fixed R 1=New preset binding 2=New native toggle
        inline constexpr const char* ScreenSyncMode = "Metroid.Screen.SyncMode";
        inline constexpr const char* MphSens         = "Metroid.Sensitivity.Mph";
        inline constexpr const char* Headphone       = "Metroid.Apply.Headphone";
        inline constexpr const char* SfxVolApply     = "Metroid.Apply.SfxVolume";
        inline constexpr const char* SfxVol          = "Metroid.Volume.SFX";
        inline constexpr const char* MusicVolApply   = "Metroid.Apply.MusicVolume";
        inline constexpr const char* MusicVol        = "Metroid.Volume.Music";
        inline constexpr const char* LicColorApply   = "Metroid.HunterLicense.Color.Apply";
        inline constexpr const char* LicColorSel     = "Metroid.HunterLicense.Color.Selected";
        inline constexpr const char* HunterApply     = "Metroid.HunterLicense.Hunter.Apply";
        inline constexpr const char* HunterSel       = "Metroid.HunterLicense.Hunter.Selected";
        inline constexpr const char* UseFwName       = "Metroid.Use.Firmware.Name";
        inline constexpr const char* DataUnlock      = "Metroid.Data.Unlock";
        inline constexpr const char* FixShadowFreeze = "Metroid.BugFix.FixShadowFreeze";

        // Phase 5b — keys driven by the non-HUD settings binding table.
        // Storage names are byte-identical to the original literals (TOML compat).
        inline constexpr const char* WifiBitset                       = "Metroid.BugFix.WifiBitset";
        inline constexpr const char* FixNoxusBladePersistence         = "Metroid.BugFix.FixNoxusBladePersistence";
        inline constexpr const char* UseFirmwareLanguage             = "Metroid.BugFix.UseFirmwareLanguage";
        inline constexpr const char* ShowHeadshotOnline             = "Metroid.GameFeature.ShowHeadshotOnline";
        inline constexpr const char* ShowEnemyHpMeterOnline         = "Metroid.GameFeature.ShowEnemyHpMeterOnline";
        inline constexpr const char* ExpandStageMatrix               = "Metroid.GameFeature.ExpandStageMatrix";
        inline constexpr const char* ExpandStageMatrixExtra         = "Metroid.GameFeature.ExpandStageMatrixExtra";
        inline constexpr const char* DisableDoubleDamageMultiplier   = "Metroid.GameFeature.DisableDoubleDamageMultiplier";
        inline constexpr const char* DamageNotifyPurple             = "Metroid.GameFeature.DamageNotifyPurple";
        inline constexpr const char* PowerUpPickupNoEffectPowerUps   = "Metroid.GameFeature.PowerUpPickupNoEffectPowerUps";
        inline constexpr const char* PowerUpPickupNoEffectDoubleDamage = "Metroid.GameFeature.PowerUpPickupNoEffectDoubleDamage";
        inline constexpr const char* PowerUpPickupNoEffectCloak       = "Metroid.GameFeature.PowerUpPickupNoEffectCloak";
        inline constexpr const char* PowerUpPickupNoEffectDeathalt   = "Metroid.GameFeature.PowerUpPickupNoEffectDeathalt";
        inline constexpr const char* InGameAspectRatio               = "Metroid.Visual.InGameAspectRatio";
        inline constexpr const char* InGameAspectRatioMode           = "Metroid.Visual.InGameAspectRatioMode";
        inline constexpr const char* LowHpWarningMode                 = "Metroid.LowHpWarning.Mode";
        inline constexpr const char* LowHpWarningFixed               = "Metroid.LowHpWarning.Fixed";
        inline constexpr const char* LowHpWarningLow                 = "Metroid.LowHpWarning.Low";
        inline constexpr const char* LowHpWarningMedium             = "Metroid.LowHpWarning.Medium";
        inline constexpr const char* LowHpWarningHigh               = "Metroid.LowHpWarning.High";
        inline constexpr const char* LowHpWarningAutoBase           = "Metroid.LowHpWarning.AutoBase";
    }

    namespace WeaponSwitchMethod {
        inline constexpr int LegacyTouch = 0;
        inline constexpr int NewNative = 1;
    }

    namespace BipedFireMethod {
        inline constexpr int LegacyInput = 0;
        inline constexpr int NewNativeEdge = 1;
    }

    namespace ZoomInputMethod {
        inline constexpr int LegacyFixedR = 0;
        inline constexpr int NewPresetBinding = 1;
        inline constexpr int NewNativeToggle = 2;
    }

    namespace LowLatencyAimMode {
        inline constexpr int Off = 0;
        inline constexpr int ImmediateSync = 1;
        inline constexpr int MoonLikeAim = 2;
        // Developer-only; public builds normalize this stored value to ImmediateSync.
        inline constexpr int InstantAimFollow = 3;
    }


    namespace WeaponId {
        inline constexpr uint8_t PowerBeam   = 0;
        inline constexpr uint8_t VoltDriver  = 1;
        inline constexpr uint8_t Missile     = 2;
        inline constexpr uint8_t Battlehammer= 3;
        inline constexpr uint8_t Imperialist = 4;
        inline constexpr uint8_t Judicator   = 5;
        inline constexpr uint8_t Magmaul     = 6;
        inline constexpr uint8_t ShockCoil   = 7;
        inline constexpr uint8_t OmegaCannon = 8;
        inline constexpr uint8_t None        = 0xFF; // sentinel: no weapon
    }

    namespace WeaponMask {
        inline constexpr uint16_t PowerBeam    = static_cast<uint16_t>(1u << WeaponId::PowerBeam);
        inline constexpr uint16_t VoltDriver   = static_cast<uint16_t>(1u << WeaponId::VoltDriver);
        inline constexpr uint16_t Missile      = static_cast<uint16_t>(1u << WeaponId::Missile);
        inline constexpr uint16_t Battlehammer = static_cast<uint16_t>(1u << WeaponId::Battlehammer);
        inline constexpr uint16_t Imperialist  = static_cast<uint16_t>(1u << WeaponId::Imperialist);
        inline constexpr uint16_t Judicator    = static_cast<uint16_t>(1u << WeaponId::Judicator);
        inline constexpr uint16_t Magmaul      = static_cast<uint16_t>(1u << WeaponId::Magmaul);
        inline constexpr uint16_t ShockCoil    = static_cast<uint16_t>(1u << WeaponId::ShockCoil);
        inline constexpr uint16_t OmegaCannon  = static_cast<uint16_t>(1u << WeaponId::OmegaCannon);
    }

    // ROM checksum literals previously lived here in `namespace RomVersions`.
    // They are now inlined directly into CHECKSUM_TABLE in
    // MelonPrimeGameRomDetect.cpp (single source of truth).

    // NDS header gameCode (offset 0x0C) packed by NDSHeader::GameCodeAsU32(),
    // i.e. GameCode[0] | [1]<<8 | [2]<<16 | [3]<<24. MPH region codes are "AMHx".
    // ROM detection is hybrid (see MelonPrimeGameRomDetect.cpp): the checksum
    // above is the PRIMARY authoritative selector; gameCode + header revision
    // (@0x1E) is only the FALLBACK used when the checksum is unrecognized
    // (trimmed / modified / brand-new dump).
    namespace MphGameCode {
        constexpr uint32_t US = 0x45484D41; // "AMHE"
        constexpr uint32_t EU = 0x50484D41; // "AMHP"
        constexpr uint32_t JP = 0x4A484D41; // "AMHJ"
        constexpr uint32_t KR = 0x4B484D41; // "AMHK"
    }

} // namespace MelonPrime

#endif // MELON_PRIME_DEF_H
