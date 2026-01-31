/*
    MelonPrimeDS Logic Separation (OPTIMIZED VERSION)
    Optimizations:
    - SIMD-friendly input batching
    - Cache-line aligned hot data
    - Branch prediction hints
    - Reduced virtual function overhead
    - Batched RAM access patterns
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

// ============================================================
// Compiler Hints & Intrinsics
// ============================================================
#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  elif defined(__GNUC__) || defined(__clang__)
#    define FORCE_INLINE __attribute__((always_inline)) inline
#  else
#    define FORCE_INLINE inline
#  endif
#endif

#ifndef NOINLINE
#  if defined(_MSC_VER)
#    define NOINLINE __declspec(noinline)
#  elif defined(__GNUC__) || defined(__clang__)
#    define NOINLINE __attribute__((noinline))
#  else
#    define NOINLINE
#  endif
#endif

#ifndef HOT_FUNCTION
#  if defined(__GNUC__) || defined(__clang__)
#    define HOT_FUNCTION __attribute__((hot))
#  else
#    define HOT_FUNCTION
#  endif
#endif

#ifndef COLD_FUNCTION
#  if defined(__GNUC__) || defined(__clang__)
#    define COLD_FUNCTION __attribute__((cold))
#  else
#    define COLD_FUNCTION
#  endif
#endif

#ifndef LIKELY
#  if defined(__GNUC__) || defined(__clang__)
#    define LIKELY(x)   __builtin_expect(!!(x), 1)
#    define UNLIKELY(x) __builtin_expect(!!(x), 0)
#  else
#    define LIKELY(x)   (x)
#    define UNLIKELY(x) (x)
#  endif
#endif

#ifndef PREFETCH
#  if defined(__GNUC__) || defined(__clang__)
#    define PREFETCH_READ(addr)  __builtin_prefetch((addr), 0, 3)
#    define PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)
#  elif defined(_MSC_VER)
#    include <intrin.h>
#    define PREFETCH_READ(addr)  _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#    define PREFETCH_WRITE(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#  else
#    define PREFETCH_READ(addr)
#    define PREFETCH_WRITE(addr)
#  endif
#endif

class EmuInstance;
namespace melonDS { class NDS; }

#ifdef _WIN32
class RawInputWinFilter;
#endif

// ============================================================
// Input Cache Bits (Optimized: Single 64-bit comparison)
// ============================================================
enum InputCacheBit : uint64_t {
    IB_JUMP = 1ULL << 0,
    IB_SHOOT = 1ULL << 1,
    IB_ZOOM = 1ULL << 2,
    IB_MORPH = 1ULL << 3,
    IB_MORPH_BOOST = 1ULL << 4,
    IB_WEAPON_CHECK = 1ULL << 5,

    IB_MOVE_F = 1ULL << 6,
    IB_MOVE_B = 1ULL << 7,
    IB_MOVE_L = 1ULL << 8,
    IB_MOVE_R = 1ULL << 9,

    IB_MENU = 1ULL << 10,
    IB_SCAN_VISOR = 1ULL << 11,

    IB_UI_OK = 1ULL << 12,
    IB_UI_LEFT = 1ULL << 13,
    IB_UI_RIGHT = 1ULL << 14,
    IB_UI_YES = 1ULL << 15,
    IB_UI_NO = 1ULL << 16,

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

    // Movement mask for fast extraction
    IB_MOVE_MASK = IB_MOVE_F | IB_MOVE_B | IB_MOVE_L | IB_MOVE_R,

    // Weapon mask for single check
    IB_WEAPON_ANY = IB_WEAPON_BEAM | IB_WEAPON_MISSILE | IB_WEAPON_1 | IB_WEAPON_2 |
    IB_WEAPON_3 | IB_WEAPON_4 | IB_WEAPON_5 | IB_WEAPON_6 | IB_WEAPON_SPECIAL,
};

// ============================================================
// Cache-Line Aligned Input State (64 bytes)
// ============================================================
struct alignas(64) FrameInputState {
    uint64_t down;      // Down state cache
    uint64_t press;     // Pressed (Edge) state cache
    int32_t mouseX;
    int32_t mouseY;
    uint32_t moveIndex; // Pre-computed movement LUT index
    uint32_t _pad[3];   // Padding to 64 bytes
};
static_assert(sizeof(FrameInputState) == 64, "FrameInputState must be 64 bytes");

// ============================================================
// Cache-Line Aligned Game Addresses (Hot Path)
// ============================================================
struct alignas(64) GameAddressesHot {
    // Most frequently accessed (during gameplay)
    melonDS::u32 isAltForm;
    melonDS::u32 jumpFlag;
    melonDS::u32 weaponChange;
    melonDS::u32 selectedWeapon;
    melonDS::u32 currentWeapon;
    melonDS::u32 aimX;
    melonDS::u32 aimY;
    melonDS::u32 havingWeapons;
    melonDS::u32 weaponAmmo;
    melonDS::u32 boostGauge;
    melonDS::u32 isBoosting;
    melonDS::u32 loadedSpecialWeapon;
    melonDS::u32 isInVisorOrMap;
    melonDS::u32 isMapOrUserActionPaused;
    melonDS::u32 inGame;
    melonDS::u32 _pad;
};
static_assert(sizeof(GameAddressesHot) == 64, "GameAddressesHot must be 64 bytes");

struct GameAddressesCold {
    // Less frequently accessed (initialization, settings)
    melonDS::u32 chosenHunter;
    melonDS::u32 inGameSensi;
    melonDS::u32 playerPos;
    melonDS::u32 isInAdventure;
    melonDS::u32 operationAndSound;
    melonDS::u32 unlockMapsHunters;
    melonDS::u32 unlockMapsHunters2;
    melonDS::u32 unlockMapsHunters3;
    melonDS::u32 unlockMapsHunters4;
    melonDS::u32 unlockMapsHunters5;
    melonDS::u32 volSfx8Bit;
    melonDS::u32 volMusic8Bit;
    melonDS::u32 sensitivity;
    melonDS::u32 dsNameFlagAndMicVolume;
    melonDS::u32 mainHunter;
    melonDS::u32 rankColor;

    // Base addresses for player offset calculation
    melonDS::u32 baseIsAltForm;
    melonDS::u32 baseLoadedSpecialWeapon;
    melonDS::u32 baseWeaponChange;
    melonDS::u32 baseSelectedWeapon;
    melonDS::u32 baseChosenHunter;
    melonDS::u32 baseJumpFlag;
    melonDS::u32 baseAimX;
    melonDS::u32 baseAimY;
    melonDS::u32 baseInGameSensi;
};

class MelonPrimeCore
{
public:
    explicit MelonPrimeCore(EmuInstance* instance);
    ~MelonPrimeCore();

    void Initialize();
    HOT_FUNCTION void RunFrameHook();
    void OnEmuStart();
    void OnEmuStop();
    void OnEmuPause();
    void OnEmuUnpause();
    void OnReset();

    void SetFrameAdvanceFunc(std::function<void()> func);
    void UpdateRendererSettings();

    FORCE_INLINE bool IsInGame() const { return m_isInGame; }
    bool ShouldForceSoftwareRenderer() const;

    uint16_t GetInputMaskFast() const;

    // Public state (cache-friendly layout)
    bool isCursorMode = true;
    bool isFocused = false;
    bool isClipWanted = false;
    bool isStylusMode = false;

    void NotifyLayoutChange() { m_isLayoutChangePending = true; }

private:
    // ============================================================
    // Hot Data (accessed every frame) - First cache line
    // ============================================================
    alignas(64) FrameInputState m_input{};

    // ============================================================
    // Hot Addresses - Second cache line
    // ============================================================
    GameAddressesHot m_addrHot{};

    // ============================================================
    // Frequently accessed state flags (packed)
    // ============================================================
    struct alignas(8) StateFlags {
        uint32_t packed;

        // Bit positions
        static constexpr uint32_t BIT_ROM_DETECTED = 1u << 0;
        static constexpr uint32_t BIT_IN_GAME = 1u << 1;
        static constexpr uint32_t BIT_IN_GAME_INIT = 1u << 2;
        static constexpr uint32_t BIT_PAUSED = 1u << 3;
        static constexpr uint32_t BIT_IN_ADVENTURE = 1u << 4;
        static constexpr uint32_t BIT_WAS_IN_GAME_RENDERER = 1u << 5;
        static constexpr uint32_t BIT_IS_SAMUS = 1u << 6;
        static constexpr uint32_t BIT_IS_WEAVEL = 1u << 7;
        static constexpr uint32_t BIT_IS_ALT_FORM = 1u << 8;
        static constexpr uint32_t BIT_SNAP_TAP = 1u << 9;
        static constexpr uint32_t BIT_JOY2KEY = 1u << 10;
        static constexpr uint32_t BIT_STYLUS_MODE = 1u << 11;
        static constexpr uint32_t BIT_LAYOUT_PENDING = 1u << 12;
        static constexpr uint32_t BIT_LAST_FOCUSED = 1u << 13;
        static constexpr uint32_t BIT_BLOCK_STYLUS = 1u << 14;

        FORCE_INLINE void set(uint32_t bit) { packed |= bit; }
        FORCE_INLINE void clear(uint32_t bit) { packed &= ~bit; }
        FORCE_INLINE void assign(uint32_t bit, bool val) {
            packed = (packed & ~bit) | (val ? bit : 0);
        }
        FORCE_INLINE bool test(uint32_t bit) const { return (packed & bit) != 0; }
    } m_flags{};

    // Expose some flags for external access
    bool m_isInGame = false;
    bool m_isLayoutChangePending = true;

    // ============================================================
    // Cold Data (accessed less frequently)
    // ============================================================
    EmuInstance* emuInstance;
    Config::Table& localCfg;
    Config::Table& globalCfg;
    std::function<void()> m_frameAdvanceFunc;

    GameAddressesCold m_addrCold{};

    melonDS::u8 m_playerPosition = 0;

    struct AimData {
        int centerX = 0;
        int centerY = 0;
    } m_aimData;

    // Applied flags (packed into single byte)
    uint8_t m_appliedFlags = 0;
    static constexpr uint8_t APPLIED_HEADPHONE = 1u << 0;
    static constexpr uint8_t APPLIED_UNLOCK = 1u << 1;
    static constexpr uint8_t APPLIED_VOL_SFX = 1u << 2;
    static constexpr uint8_t APPLIED_VOL_MUSIC = 1u << 3;

    // ============================================================
    // Optimized Input Methods
    // ============================================================
    HOT_FUNCTION void UpdateInputState();

    FORCE_INLINE bool IsDown(uint64_t bit) const {
        return (m_input.down & bit) != 0;
    }
    FORCE_INLINE bool IsPressed(uint64_t bit) const {
        return (m_input.press & bit) != 0;
    }
    FORCE_INLINE bool IsAnyDown(uint64_t mask) const {
        return (m_input.down & mask) != 0;
    }
    FORCE_INLINE bool IsAnyPressed(uint64_t mask) const {
        return (m_input.press & mask) != 0;
    }

    // ============================================================
    // Core Logic Methods (Hot Path)
    // ============================================================
    HOT_FUNCTION void HandleInGameLogic(melonDS::u8* mainRAM);
    HOT_FUNCTION void ProcessMoveInputFast();
    HOT_FUNCTION void ProcessAimInputMouse(melonDS::u8* mainRAM);
    HOT_FUNCTION bool ProcessWeaponSwitch(melonDS::u8* mainRAM);
    HOT_FUNCTION bool HandleMorphBallBoost(melonDS::u8* mainRAM);

    // ============================================================
    // Cold Path Methods
    // ============================================================
    COLD_FUNCTION void DetectRomAndSetAddresses();
    COLD_FUNCTION void ApplyGameSettingsOnce();
    void HandleGlobalHotkeys();
    void HandleAdventureMode(melonDS::u8* mainRAM);
    void ProcessAimInputStylus();
    void ProcessMoveInput(QBitArray& inputMask);
    void SwitchWeapon(melonDS::u8* mainRAM, int weaponIndex);
    void ShowCursor(bool show);
    void FrameAdvanceTwice();
    void FrameAdvanceOnce();
    QPoint GetAdjustedCenter();

    // Static helpers
    static bool ApplyHeadphoneOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, uint8_t& flags, uint8_t bit);
    static bool ApplySfxVolumeOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, uint8_t& flags, uint8_t bit);
    static bool ApplyMusicVolumeOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, uint8_t& flags, uint8_t bit);
    static bool applyLicenseColorStrict(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr);
    static bool applySelectedHunterStrict(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr);
    static bool useDsName(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr);
    static void ApplyMphSensitivity(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addrSensi, melonDS::u32 addrInGame, bool inGameInit);
    static bool ApplyUnlockHuntersMaps(melonDS::NDS* nds, Config::Table& localCfg, uint8_t& flags, uint8_t bit,
        melonDS::u32 a1, melonDS::u32 a2, melonDS::u32 a3, melonDS::u32 a4, melonDS::u32 a5);
    static melonDS::u32 calculatePlayerAddress(melonDS::u32 base, melonDS::u8 pos, int32_t inc);

    void SetupRawInput();
    void ApplyJoy2KeySupportAndQtFilter(bool enable, bool doReset = true);
};

#endif // MELONPRIME_H