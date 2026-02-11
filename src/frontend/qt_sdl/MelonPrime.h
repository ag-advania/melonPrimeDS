#ifndef MELONPRIME_H
#define MELONPRIME_H

#include <functional>
#include <vector>
#include <cstdint>
#include <array>
#include <string_view>
#include <memory>
#include <cmath>
#include <cstring>

class QPoint;

#include "types.h"
#include "Config.h"
#include "MelonPrimeGameSettings.h"
#include "MelonPrimeGameRomAddrTable.h"

// =========================================================================
// Compiler hint macros (deduplicated, defined once)
// =========================================================================
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

#ifndef PREFETCH_READ
#  if defined(__GNUC__) || defined(__clang__)
#    define PREFETCH_READ(addr)  __builtin_prefetch((addr), 0, 3)
#    define PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)
#  elif defined(_MSC_VER)
#    include <intrin.h>
#    define PREFETCH_READ(addr)  _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#    define PREFETCH_WRITE(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#  else
#    define PREFETCH_READ(addr)  ((void)0)
#    define PREFETCH_WRITE(addr) ((void)0)
#  endif
#endif

class EmuInstance;
namespace melonDS { class NDS; }

namespace MelonPrime {

#ifdef _WIN32
    class RawInputWinFilter;
#endif

    // =========================================================================
    // Input cache bits — one-hot encoded for branchless mask operations
    // =========================================================================
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

        IB_MOVE_MASK = IB_MOVE_F | IB_MOVE_B | IB_MOVE_L | IB_MOVE_R,
        IB_WEAPON_ANY = IB_WEAPON_BEAM | IB_WEAPON_MISSILE |
        IB_WEAPON_1 | IB_WEAPON_2 | IB_WEAPON_3 |
        IB_WEAPON_4 | IB_WEAPON_5 | IB_WEAPON_6 |
        IB_WEAPON_SPECIAL,
        IB_UI_ANY = IB_UI_OK | IB_UI_LEFT | IB_UI_RIGHT | IB_UI_YES | IB_UI_NO,
    };

    // =========================================================================
    // Per-frame input snapshot — one cache line, tight packing
    // =========================================================================
    struct alignas(64) FrameInputState {
        uint64_t down;
        uint64_t press;
        int32_t  mouseX;
        int32_t  mouseY;
        uint32_t moveIndex;
        uint32_t _pad[3];
    };
    static_assert(sizeof(FrameInputState) == 64);

    // =========================================================================
    // Hot game addresses — addresses recomputed on player-position change
    //   Reduced from 128 → 76 bytes (no wasteful 60-byte padding).
    //   Fits in 2 cache lines.  Fields ordered by access frequency.
    // =========================================================================
    struct alignas(64) GameAddressesHot {
        // ---- Most-accessed (every frame in-game) ----
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

        // ---- Accessed on init / hunter change ----
        melonDS::u32 chosenHunter;
        melonDS::u32 inGameSensi;
    };

    // =========================================================================
    // Hot pointers — pre-resolved RAM pointers, avoid per-access masking
    // =========================================================================
    struct alignas(64) HotPointers {
        // 【最適化】アクセス頻度が高い順に再配置 (Cache Locality)
        // [Tier 1: Every Frame (Always)] Aiming & Basic Combat
        uint16_t* aimX;             // 8 bytes
        uint16_t* aimY;             // 8 bytes
        uint8_t* isAltForm;        // 8 bytes
        uint8_t* jumpFlag;         // 8 bytes
        uint32_t* weaponAmmo;       // 8 bytes (Used in loop)

        // [Tier 2: Every Frame (Often)] Weapon State
        uint16_t* havingWeapons;    // 8 bytes
        uint8_t* currentWeapon;    // 8 bytes
        uint8_t* weaponChange;     // 8 bytes

        // -- Cache Line Boundary likely here (64 bytes) --

        // [Tier 3: Conditional / Rare]
        uint8_t* selectedWeapon;
        uint8_t* loadedSpecialWeapon;
        uint8_t* boostGauge;       // Only if Morph
        uint8_t* isBoosting;       // Only if Morph
        uint8_t* isInVisorOrMap;   // Adventure only
        uint8_t* isMapOrUserActionPaused; // Adventure only
    };

    // =========================================================================
    // Aim block reason bits
    // =========================================================================
    enum AimBlockBit : uint32_t {
        AIMBLK_CHECK_WEAPON = 1u << 0,
        AIMBLK_MORPHBALL_BOOST = 1u << 1,
        AIMBLK_CURSOR_MODE = 1u << 2,
    };

    // =========================================================================
    // Applied-once setting flags
    // =========================================================================
    enum AppliedFlag : uint8_t {
        APPLIED_HEADPHONE = 1u << 0,
        APPLIED_UNLOCK = 1u << 1,
        APPLIED_VOL_SFX = 1u << 2,
        APPLIED_VOL_MUSIC = 1u << 3,
        APPLIED_ALL_ONCE = APPLIED_HEADPHONE | APPLIED_UNLOCK | APPLIED_VOL_SFX | APPLIED_VOL_MUSIC,
    };

#ifdef _WIN32
    struct FilterDeleter {
        void operator()(RawInputWinFilter* ptr);
    };
#endif

    // =========================================================================
    // Core class
    // =========================================================================
    class MelonPrimeCore
    {
    public:
        explicit MelonPrimeCore(EmuInstance* instance);
        ~MelonPrimeCore();

        MelonPrimeCore(const MelonPrimeCore&) = delete;
        MelonPrimeCore& operator=(const MelonPrimeCore&) = delete;

        void Initialize();
        HOT_FUNCTION void RunFrameHook();
        void OnEmuStart();
        void OnEmuStop();
        void OnEmuPause();
        void OnEmuUnpause();
        void OnReset();

        void SetFrameAdvanceFunc(std::function<void()> func);

        [[nodiscard]] FORCE_INLINE bool IsInGame() const { return m_isInGame; }
        [[nodiscard]] bool ShouldForceSoftwareRenderer() const;
        [[nodiscard]] uint16_t GetInputMaskFast() const { return m_inputMaskFast; }

        bool isCursorMode = true;
        bool isFocused = false;
        bool isClipWanted = false;
        bool isStylusMode = false;

        void NotifyLayoutChange() { m_isLayoutChangePending = true; }

    private:
        // =================================================================
        // Data layout — ordered by access frequency & cache line grouping
        // =================================================================

        // [Cache Line 0: Per-frame input]
        alignas(64) FrameInputState m_input{};

        // [Cache Lines 1-2: Hot addresses + pointers — accessed every in-game frame]
        GameAddressesHot m_addrHot{};
        HotPointers      m_ptrs{};

        // [Cache Line 3: Per-frame scalars — fits in one line]
        uint16_t m_inputMaskFast = 0xFFFF;
        uint16_t m_snapState = 0;
        uint32_t m_aimBlockBits = 0;
        float    m_aimSensiFactor = 0.01f;
        float    m_aimCombinedY = 0.013333333f;
        float    m_aimAdjust = 0.5f;
        bool     m_isAimDisabled = false;
        bool     m_isRunningHook = false;
        bool     m_isWeaponCheckActive = false;
        bool     m_isInGame = false;
        bool     m_isLayoutChangePending = true;

        // [State flags — packed bitfield for single-compare branching]
        struct alignas(4) StateFlags {
            uint32_t packed = 0;

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
            FORCE_INLINE void assign(uint32_t bit, bool val) { packed = (packed & ~bit) | (val ? bit : 0u); }
            [[nodiscard]] FORCE_INLINE bool test(uint32_t bit) const { return (packed & bit) != 0; }
        } m_flags{};

        // [Cold / infrequently accessed]
        RomAddresses m_currentRom{};
        uint8_t      m_appliedFlags = 0;
        melonDS::u8  m_playerPosition = 0;

        EmuInstance* emuInstance;
        Config::Table& localCfg;
        Config::Table& globalCfg;

        // Frame-advance dispatch — member-function pointer avoids
        // std::function's type-erased indirect call on every FrameAdvanceOnce().
        std::function<void()> m_frameAdvanceFunc;
        using AdvanceMethod = void (MelonPrimeCore::*)();
        AdvanceMethod m_fnAdvance = &MelonPrimeCore::FrameAdvanceDefault;

#ifdef _WIN32
        std::unique_ptr<RawInputWinFilter, FilterDeleter> m_rawFilter;
        void* m_cachedHwnd = nullptr;
#endif

        struct AimData {
            int centerX = 0;
            int centerY = 0;
        } m_aimData;

        // =================================================================
        // Inline helpers — keep in header for inlining
        // =================================================================

        FORCE_INLINE void InputReset() { m_inputMaskFast = 0xFFFF; }

        FORCE_INLINE void InputSetBranchless(uint16_t bit, bool released) {
            const uint16_t mask = 1u << bit;
            m_inputMaskFast = (m_inputMaskFast & ~mask) | (static_cast<uint16_t>(released) * mask);
        }

        FORCE_INLINE void SetAimBlockBranchless(uint32_t bitMask, bool enable) noexcept {
            // Branchless set/clear: avoids branch on enable for hot-path usage
            m_aimBlockBits = (m_aimBlockBits & ~bitMask) | (enable ? bitMask : 0u);
            m_isAimDisabled = (m_aimBlockBits != 0);
        }

        template <typename T>
        [[nodiscard]] FORCE_INLINE T* GetRamPointer(melonDS::u8* ram, melonDS::u32 addr) {
            return reinterpret_cast<T*>(&ram[addr & 0x3FFFFF]);
        }

        FORCE_INLINE void ApplyAimAdjustBranchless(float& dx, float& dy) noexcept {
            const float a = m_aimAdjust;
            if (UNLIKELY(a <= 0.0f)) return;

            const float absX = std::abs(dx);
            dx = (absX < a) ? 0.0f : (absX < 1.0f) ? std::copysign(1.0f, dx) : dx;

            const float absY = std::abs(dy);
            dy = (absY < a) ? 0.0f : (absY < 1.0f) ? std::copysign(1.0f, dy) : dy;
        }

        [[nodiscard]] FORCE_INLINE bool IsDown(uint64_t bit) const { return (m_input.down & bit) != 0; }
        [[nodiscard]] FORCE_INLINE bool IsPressed(uint64_t bit) const { return (m_input.press & bit) != 0; }
        [[nodiscard]] FORCE_INLINE bool IsAnyPressed(uint64_t mask) const { return (m_input.press & mask) != 0; }

        // =================================================================
        // Method declarations
        // =================================================================
        HOT_FUNCTION void UpdateInputState();
        HOT_FUNCTION void HandleInGameLogic();
        HOT_FUNCTION void ProcessMoveInputFast();
        HOT_FUNCTION void ProcessAimInputMouse();
        HOT_FUNCTION bool ProcessWeaponSwitch();
        HOT_FUNCTION bool HandleMorphBallBoost();

        COLD_FUNCTION void DetectRomAndSetAddresses();
        COLD_FUNCTION void ApplyGameSettingsOnce();
        void RecalcAimSensitivityCache(Config::Table& cfg);
        void ApplyAimAdjustSetting(Config::Table& cfg);
        void HandleGlobalHotkeys();
        void HandleAdventureMode();
        void ProcessAimInputStylus();
        void SwitchWeapon(int weaponIndex);
        void ShowCursor(bool show);
        void FrameAdvanceTwice();
        FORCE_INLINE void FrameAdvanceOnce() { (this->*m_fnAdvance)(); }
        void FrameAdvanceDefault();
        void FrameAdvanceCustom();
        QPoint GetAdjustedCenter();

        void SetupRawInput();
        void ApplyJoy2KeySupportAndQtFilter(bool enable, bool doReset = true);

        // Helper: bulk config re-read (called on unpause/start)
        void ReloadConfigFlags();
    };

} // namespace MelonPrime

#endif // MELONPRIME_H