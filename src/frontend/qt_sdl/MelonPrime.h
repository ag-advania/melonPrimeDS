/*
    MelonPrimeDS Logic Separation (Fixed Header)
*/

#ifndef MELONPRIME_H
#define MELONPRIME_H

#include <QObject>
#include <QBitArray>
#include <QPoint>
#include <functional>
#include <vector>

#include "types.h"
#include "Config.h"

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

    void Initialize();
    void RunFrameHook();
    void OnEmuStart();
    void OnEmuStop();
    void OnEmuPause();
    void OnEmuUnpause();
    void OnReset();

    void SetFrameAdvanceFunc(std::function<void()> func);
    void UpdateRendererSettings();
    bool IsInGame() const { return isInGame; }
    bool ShouldForceSoftwareRenderer() const;

    // Public State Flags
    bool isCursorMode = true;
    bool isFocused = false;
    bool isClipWanted = false;
    bool isStylusMode = false;

    void NotifyLayoutChange() { isLayoutChangePending = true; }

private:
    EmuInstance* emuInstance;
    Config::Table& localCfg;
    Config::Table& globalCfg;

    std::function<void()> m_frameAdvanceFunc;

    // 【修正】メンバ変数として復活（再帰ガード共有用）
    bool m_blockStylusAim = false;

    // --- State Flags ---
    bool isRomDetected = false;
    bool isInGame = false;
    bool isInGameAndHasInitialized = false;
    bool isPaused = false;
    bool isInAdventure = false;
    bool wasInGameForRenderer = false;

    // --- Input / Control Flags ---
    bool isJoy2KeySupport = false;
    bool isSnapTapMode = false;
    bool wasLastFrameFocused = false;
    bool isLayoutChangePending = true;

    struct AimData {
        int centerX = 0;
        int centerY = 0;
    } aimData;

    bool isHeadphoneApplied = false;
    bool isUnlockMapsHuntersApplied = false;
    bool isVolumeSfxApplied = false;
    bool isVolumeMusicApplied = false;

    // --- Memory Addresses ---
    struct GameAddresses {
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

        melonDS::u32 baseIsAltForm;
        melonDS::u32 baseLoadedSpecialWeapon;
        melonDS::u32 baseWeaponChange;
        melonDS::u32 baseSelectedWeapon;
        melonDS::u32 baseChosenHunter;
        melonDS::u32 baseJumpFlag;
        melonDS::u32 baseAimX;
        melonDS::u32 baseAimY;
        melonDS::u32 baseInGameSensi;

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

    melonDS::u8 playerPosition = 0;
    bool isSamus = false;
    bool isWeavel = false;
    bool isAltForm = false;

    // --- Internal Logic Methods ---
    void DetectRomAndSetAddresses();
    void ApplyGameSettingsOnce();
    void HandleGlobalHotkeys();

    void HandleInGameLogic(melonDS::u8* mainRAM);
    void ProcessMoveInput(QBitArray& inputMask);

    void ProcessAimInputMouse(melonDS::u8* mainRAM);
    void ProcessAimInputStylus();

    // 戻り値変更なし、内部でメンバ変数を操作
    bool ProcessWeaponSwitch(melonDS::u8* mainRAM);
    bool HandleMorphBallBoost(melonDS::u8* mainRAM);

    // 引数変更: bool& blockAimOut を削除
    void HandleAdventureMode(melonDS::u8* mainRAM);

    void SwitchWeapon(melonDS::u8* mainRAM, int weaponIndex);

    void ShowCursor(bool show);
    void FrameAdvanceTwice();
    void FrameAdvanceOnce();
    QPoint GetAdjustedCenter();

    static bool ApplyHeadphoneOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, bool& applied);
    static bool ApplySfxVolumeOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, bool& applied);
    static bool ApplyMusicVolumeOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, bool& applied);
    static bool applyLicenseColorStrict(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr);
    static bool applySelectedHunterStrict(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr);
    static bool useDsName(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr);
    static void ApplyMphSensitivity(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addrSensi, melonDS::u32 addrInGame, bool inGameInit);
    static bool ApplyUnlockHuntersMaps(melonDS::NDS* nds, Config::Table& localCfg, bool& applied, melonDS::u32 a1, melonDS::u32 a2, melonDS::u32 a3, melonDS::u32 a4, melonDS::u32 a5);
    static melonDS::u32 calculatePlayerAddress(melonDS::u32 base, melonDS::u8 pos, int32_t inc);

    void SetupRawInput();
    void ApplyJoy2KeySupportAndQtFilter(bool enable, bool doReset = true);
};

#endif // MELONPRIME_H