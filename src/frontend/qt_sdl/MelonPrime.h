/*
    MelonPrimeDS Logic Separation (Optimized: Plan 1 Only)
    Plan 1: Input Snapshot (Batch Read) -> ENABLED
    Plan 3: Unsafe RAM Access -> DISABLED (Reverted to Safe Mode)
*/

#ifndef MELONPRIME_H
#define MELONPRIME_H

#include <QObject>
#include <QBitArray>
#include <QPoint>
#include <functional>
#include <vector>
#include <cstdint>

#include "types.h"
#include "Config.h"

// Define FORCE_INLINE here so it is available for inline methods below
#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  else
#    define FORCE_INLINE __attribute__((always_inline)) inline
#  endif
#endif

class EmuInstance;
namespace melonDS { class NDS; }

#ifdef _WIN32
class RawInputWinFilter;
#endif

// ============================================================
// Input Cache Bits (Plan 1)
// ============================================================
enum InputCacheBit : uint64_t {
    IB_JUMP = 1ULL << 0,
    IB_SHOOT = 1ULL << 1, // ShootScan | ScanShoot
    IB_ZOOM = 1ULL << 2,
    IB_MORPH = 1ULL << 3, // Pressed
    IB_MORPH_BOOST = 1ULL << 4, // Down
    IB_WEAPON_CHECK = 1ULL << 5, // Down

    IB_MOVE_F = 1ULL << 6,
    IB_MOVE_B = 1ULL << 7,
    IB_MOVE_L = 1ULL << 8,
    IB_MOVE_R = 1ULL << 9,

    IB_MENU = 1ULL << 10,
    IB_SCAN_VISOR = 1ULL << 11, // Pressed

    // UI Keys (Pressed)
    IB_UI_OK = 1ULL << 12,
    IB_UI_LEFT = 1ULL << 13,
    IB_UI_RIGHT = 1ULL << 14,
    IB_UI_YES = 1ULL << 15,
    IB_UI_NO = 1ULL << 16,

    // Weapon Keys (Pressed)
    IB_WEAPON_BEAM = 1ULL << 17,
    IB_WEAPON_MISSILE = 1ULL << 18,
    IB_WEAPON_1 = 1ULL << 19,
    IB_WEAPON_2 = 1ULL << 20,
    IB_WEAPON_3 = 1ULL << 21,
    IB_WEAPON_4 = 1ULL << 22,
    IB_WEAPON_5 = 1ULL << 23,
    IB_WEAPON_6 = 1ULL << 24,
    IB_WEAPON_SPECIAL = 1ULL << 25,
    IB_WEAPON_NEXT = 1ULL << 26,
    IB_WEAPON_PREV = 1ULL << 27,
};

struct FrameInputState {
    uint64_t down = 0;   // Down state cache
    uint64_t press = 0;  // Pressed (Edge) state cache
    int mouseX = 0;
    int mouseY = 0;
};

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

    // ============================================================
    // Optimized Input Mask API
    // ============================================================
    uint16_t GetInputMaskFast() const;

    // ============================================================
    // Public State Flags
    // ============================================================
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
    bool m_blockStylusAim = false;

    // --- Optimized Input Cache (Plan 1) ---
    FrameInputState m_input;
    void UpdateInputState();

    FORCE_INLINE bool IsDown(uint64_t bit) const { return m_input.down & bit; }
    FORCE_INLINE bool IsPressed(uint64_t bit) const { return m_input.press & bit; }

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

    void ProcessMoveInputFast();
    // Legacy: syncs to QBitArray
    void ProcessMoveInput(QBitArray& inputMask);

    void ProcessAimInputMouse(melonDS::u8* mainRAM);
    void ProcessAimInputStylus();

    bool ProcessWeaponSwitch(melonDS::u8* mainRAM);
    bool HandleMorphBallBoost(melonDS::u8* mainRAM);
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