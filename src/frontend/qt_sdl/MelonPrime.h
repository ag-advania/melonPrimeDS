/*
    MelonPrimeDS Logic Separation (Full Version: Added Sensitivity Hotkeys)
*/

#ifndef MELONPRIME_H
#define MELONPRIME_H

#include <QObject>
#include <QBitArray>
#include <QPoint>
#include <functional> // for std::function
#include <vector>

#include "types.h"
#include "Config.h"

// Forward declarations
class EmuInstance;
namespace melonDS { class NDS; }

#ifdef _WIN32
class RawInputWinFilter;
#endif

class MelonPrimeCore
{
public:
    explicit MelonPrimeCore(EmuInstance* instance);
    ~MelonPrimeCore();

    // Main Hooks called from EmuThread
    void Initialize();
    void RunFrameHook(); // Called every frame inside the emulation loop
    void OnEmuStart();
    void OnEmuStop();
    void OnEmuPause();
    void OnEmuUnpause();
    void OnReset();

    // Callback setter for FPS-limited frame advance
    void SetFrameAdvanceFunc(std::function<void()> func);

    // Config/State updates
    void UpdateRendererSettings();
    bool IsInGame() const { return isInGame; }

    // Check if we should force software renderer (Menu fix)
    bool ShouldForceSoftwareRenderer() const;

    // --- Public State Flags (Accessed by Screen.cpp) ---
    bool isCursorMode = true;
    bool isFocused = false;
    bool isClipWanted = false; // Added for ScreenPanel clip logic

    void NotifyLayoutChange() { isLayoutChangePending = true; }

    bool isStylusMode = false;


private:
    EmuInstance* emuInstance;
    Config::Table& localCfg;
    Config::Table& globalCfg;

    // Callback function to call EmuThread's frame advance (with FPS limit)
    std::function<void()> m_frameAdvanceFunc;

    bool m_blockStylusAim = false; // ÉNÉâÉXÉÅÉìÉoÇ∆ÇµÇƒí«â¡

    // --- State Flags ---
    bool isRomDetected = false;
    bool isInGame = false;
    bool isInGameAndHasInitialized = false;
    bool isPaused = false; // Adventure Mode pause state
    bool isInAdventure = false;

    // Renderer state tracking
    bool wasInGameForRenderer = false;

    // --- Input / Control Flags ---
    bool isJoy2KeySupport = false;
    bool isSnapTapMode = false;
    bool wasLastFrameFocused = false;
    bool isLayoutChangePending = true;

    // --- Aim / Mouse State ---
    struct AimData {
        int centerX = 0;
        int centerY = 0;
    } aimData;

    // --- One-time Application Flags ---
    bool isHeadphoneApplied = false;
    bool isUnlockMapsHuntersApplied = false;
    bool isVolumeSfxApplied = false;
    bool isVolumeMusicApplied = false;

    // --- Memory Addresses (Cached) ---
    struct GameAddresses {
        // Dynamic addresses calculated from PlayerPos
        melonDS::u32 isAltForm;
        melonDS::u32 loadedSpecialWeapon;
        melonDS::u32 chosenHunter;
        melonDS::u32 weaponChange;
        melonDS::u32 selectedWeapon;
        melonDS::u32 jumpFlag;
        melonDS::u32 havingWeapons;
        melonDS::u32 currentWeapon;
        melonDS::u32 boostGauge;
        melonDS::u32 isBoosting;
        melonDS::u32 weaponAmmo;
        melonDS::u32 aimX;
        melonDS::u32 aimY;
        melonDS::u32 inGameSensi;
        melonDS::u32 isInVisorOrMap;

        // Base addresses (set during detection)
        melonDS::u32 baseIsAltForm;
        melonDS::u32 baseLoadedSpecialWeapon;
        melonDS::u32 baseWeaponChange;
        melonDS::u32 baseSelectedWeapon;
        melonDS::u32 baseChosenHunter;
        melonDS::u32 baseJumpFlag;
        melonDS::u32 baseAimX;
        melonDS::u32 baseAimY;
        melonDS::u32 baseInGameSensi;

        // Static addresses
        melonDS::u32 inGame;
        melonDS::u32 playerPos;
        melonDS::u32 isInAdventure;
        melonDS::u32 isMapOrUserActionPaused;
        melonDS::u32 operationAndSound;
        melonDS::u32 unlockMapsHunters;
        melonDS::u32 volSfx8Bit;
        melonDS::u32 volMusic8Bit;
        melonDS::u32 unlockMapsHunters2;
        melonDS::u32 unlockMapsHunters3;
        melonDS::u32 unlockMapsHunters4;
        melonDS::u32 unlockMapsHunters5;
        melonDS::u32 sensitivity;
        melonDS::u32 dsNameFlagAndMicVolume;
        melonDS::u32 mainHunter;
        melonDS::u32 rankColor;
    } addr;

    // --- Game Context ---
    melonDS::u8 playerPosition = 0;
    bool isSamus = false;
    bool isWeavel = false;
    bool isAltForm = false;

    // --- Internal Logic Methods ---
    void DetectRomAndSetAddresses();
    void ApplyGameSettingsOnce();

    // [NEW] Global hotkeys (Sensitivity, Layout Reset)
    void HandleGlobalHotkeys();

    void HandleInGameLogic();
    void ProcessMoveInput(QBitArray& inputMask);
    void ProcessAimInput();
    bool ProcessWeaponSwitch();
    void HandleMorphBallBoost();
    void HandleAdventureMode();

    void SwitchWeapon(int weaponIndex);

    // Helpers
    void ShowCursor(bool show);
    void FrameAdvanceTwice();
    void FrameAdvanceOnce(); // Uses the callback
    QPoint GetAdjustedCenter();

    // Private Static Helpers (ported from original EmuThread.cpp)
    static bool ApplyHeadphoneOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, bool& applied);
    static bool ApplySfxVolumeOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, bool& applied);
    static bool ApplyMusicVolumeOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, bool& applied);
    static bool applyLicenseColorStrict(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr);
    static bool applySelectedHunterStrict(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr);
    static bool useDsName(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr);
    static void ApplyMphSensitivity(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addrSensi, melonDS::u32 addrInGame, bool inGameInit);
    static bool ApplyUnlockHuntersMaps(melonDS::NDS* nds, Config::Table& localCfg, bool& applied, melonDS::u32 a1, melonDS::u32 a2, melonDS::u32 a3, melonDS::u32 a4, melonDS::u32 a5);
    static melonDS::u32 calculatePlayerAddress(melonDS::u32 base, melonDS::u8 pos, int32_t inc);

    // Windows Specific
    void SetupRawInput();
    void ApplyJoy2KeySupportAndQtFilter(bool enable, bool doReset = true);
};

#endif // MELONPRIME_H