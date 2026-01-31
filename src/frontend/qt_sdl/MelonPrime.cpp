/*
    MelonPrimeDS Logic Implementation (Optimized: Plan 1 Only)
    Plan 1: Input Snapshot (Batch Read) -> ENABLED
    Plan 3: Unsafe RAM Access -> DISABLED (Safety First)
*/

#include "MelonPrime.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "NDS.h"
#include "GPU.h"
#include "main.h" 
#include "Screen.h"
#include "Platform.h"
#include "MelonPrimeDef.h" 
#include "MelonPrimeRomAddrTable.h"

#include <cmath>
#include <algorithm>
#include <QCoreApplication>
#include <QCursor>

#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeHotkeyVkBinding.h"
static RawInputWinFilter* g_rawFilter = nullptr;
#endif

// ============================================================
// Input Bit Definitions (NDS)
// ============================================================
enum InputBit : uint16_t {
    INPUT_A = 0, INPUT_B = 1, INPUT_SELECT = 2, INPUT_START = 3,
    INPUT_RIGHT = 4, INPUT_LEFT = 5, INPUT_UP = 6, INPUT_DOWN = 7,
    INPUT_R = 8, INPUT_L = 9, INPUT_X = 10, INPUT_Y = 11,
};

// ============================================================
// Fast Input Mask
// ============================================================
static uint16_t gInputMaskFast = 0xFFFF;

#define INPUT_PRESS(bit)   (gInputMaskFast &= ~(1u << (bit)))
#define INPUT_RELEASE(bit) (gInputMaskFast |= (1u << (bit)))
#define INPUT_SET(bit, released) do { \
    if (released) gInputMaskFast |= (1u << (bit)); \
    else gInputMaskFast &= ~(1u << (bit)); \
} while(0)
#define INPUT_RESET() (gInputMaskFast = 0xFFFF)

// ============================================================
// Hotkey Macros (Original - used inside UpdateInputState only)
// ============================================================
#if defined(_WIN32)
#define MP_JOY_DOWN(id)      (emuInstance->joyHotkeyMask.testBit((id)))
#define MP_JOY_PRESSED(id)   (emuInstance->joyHotkeyPress.testBit((id)))
#define MP_JOY_RELEASED(id)  (emuInstance->joyHotkeyRelease.testBit((id)))
#define MP_HK_DOWN_RAW(id)     ( (g_rawFilter && g_rawFilter->hotkeyDown((id)))     || MP_JOY_DOWN((id)) )
#define MP_HK_PRESSED_RAW(id)  ( (g_rawFilter && g_rawFilter->hotkeyPressed((id)))  || MP_JOY_PRESSED((id)) )
#else
#define MP_HK_DOWN_RAW(id)     ( emuInstance->hotkeyMask.testBit((id)) )
#define MP_HK_PRESSED_RAW(id)  ( emuInstance->hotkeyPress.testBit((id)) )
#endif

// Helpers for Adventure Mode touches
#define TOUCH_IF(PRESS_BIT, X, Y) \
    if (IsPressed(PRESS_BIT)) { \
        emuInstance->getNDS()->ReleaseScreen(); \
        FrameAdvanceTwice(); \
        emuInstance->getNDS()->TouchScreen((X), (Y)); \
        FrameAdvanceTwice(); \
    }

// ============================================================
// Aim Block System
// ============================================================
#ifndef AIMBLOCK_ATOMIC
#define AIMBLOCK_ATOMIC 0
#endif

#if AIMBLOCK_ATOMIC
#include <atomic>
using AimBitsType = std::atomic<uint32_t>;
#define AIMBITS_LOAD(x)    (x.load(std::memory_order_relaxed))
#define AIMBITS_OR(x, m)   (x.fetch_or((m), std::memory_order_relaxed))
#define AIMBITS_AND(x, m)  (x.fetch_and((m), std::memory_order_relaxed))
#else
using AimBitsType = uint32_t;
#define AIMBITS_LOAD(x)    (x)
#define AIMBITS_OR(x, m)   ((x) |= (m))
#define AIMBITS_AND(x, m)  ((x) &= (m))
#endif

static AimBitsType gAimBlockBits = 0;
static uint32_t isAimDisabled = 0;

enum : uint32_t {
    AIMBLK_CHECK_WEAPON = 1u << 0,
    AIMBLK_MORPHBALL_BOOST = 1u << 1,
    AIMBLK_CURSOR_MODE = 1u << 2,
};

static inline void syncIsAimDisabled() noexcept {
    isAimDisabled = AIMBITS_LOAD(gAimBlockBits);
}

static inline void setAimBlock(uint32_t bitMask, bool enable) noexcept {
    if (enable) AIMBITS_OR(gAimBlockBits, bitMask);
    else AIMBITS_AND(gAimBlockBits, ~bitMask);
    syncIsAimDisabled();
}

// ============================================================
// Aim Adjust & Sensitivity
// ============================================================
static float gAimAdjust = 0.5f;

static inline void applyAimAdjustSetting(Config::Table& cfg) noexcept {
    double v = cfg.GetDouble("Metroid.Aim.Adjust");
    if (std::isnan(v) || v < 0.0) v = 0.0;
    gAimAdjust = static_cast<float>(v);
}

static inline void applyAimAdjustFloat(float& dx, float& dy) noexcept {
    const float a = gAimAdjust;
    if (a <= 0.0f) return;
    float avx = std::fabs(dx);
    if (avx < a) dx = 0.0f; else if (avx < 1.0f) dx = (dx >= 0.0f ? 1.0f : -1.0f);
    float avy = std::fabs(dy);
    if (avy < a) dy = 0.0f; else if (avy < 1.0f) dy = (dy >= 0.0f ? 1.0f : -1.0f);
}
#define AIM_ADJUST(dx, dy) applyAimAdjustFloat((dx), (dy))

static float gAimSensiFactor = 0.01f;
static float gAimCombinedY = 0.013333333f;

static inline void recalcAimSensitivityCache(Config::Table& localCfg) {
    const int sens = localCfg.GetInt("Metroid.Sensitivity.Aim");
    const float aimYAxisScale = static_cast<float>(localCfg.GetDouble("Metroid.Sensitivity.AimYAxisScale"));
    gAimSensiFactor = sens * 0.01f;
    gAimCombinedY = gAimSensiFactor * aimYAxisScale;
}

static inline std::uint16_t sensiNumToSensiVal(double sensiNum) {
    constexpr std::uint32_t BASE_VAL = 0x0999;
    constexpr std::uint32_t STEP_VAL = 0x0199;
    double val = static_cast<double>(BASE_VAL) + (sensiNum - 1.0) * static_cast<double>(STEP_VAL);
    std::uint32_t result = static_cast<std::uint32_t>(std::llround(val));
    return static_cast<std::uint16_t>(result > 0xFFFF ? 0xFFFF : result);
}

static const char* kWeaponNames[] = {
    "Power Beam", "Volt Driver", "Missile Launcher", "Battlehammer",
    "Imperialist", "Judicator", "Magmaul", "Shock Coil", "Omega Cannon"
};

// ============================================================
// Fast RAM Access (SAFE VERSION - MASK RESTORED)
// ============================================================
namespace {
    FORCE_INLINE uint8_t FastRead8(const melonDS::u8* ram, melonDS::u32 addr) {
        return ram[addr & 0x3FFFFF];
    }
    FORCE_INLINE uint16_t FastRead16(const melonDS::u8* ram, melonDS::u32 addr) {
        return *reinterpret_cast<const uint16_t*>(&ram[addr & 0x3FFFFF]);
    }
    FORCE_INLINE uint32_t FastRead32(const melonDS::u8* ram, melonDS::u32 addr) {
        return *reinterpret_cast<const uint32_t*>(&ram[addr & 0x3FFFFF]);
    }
    FORCE_INLINE void FastWrite8(melonDS::u8* ram, melonDS::u32 addr, uint8_t val) {
        ram[addr & 0x3FFFFF] = val;
    }
    FORCE_INLINE void FastWrite16(melonDS::u8* ram, melonDS::u32 addr, uint16_t val) {
        *reinterpret_cast<uint16_t*>(&ram[addr & 0x3FFFFF]) = val;
    }

    alignas(64) static constexpr uint8_t MoveLUT[16] = {
        0xF0, 0xB0, 0x70, 0xF0, 0xD0, 0x90, 0x50, 0xD0,
        0xE0, 0xA0, 0x60, 0xE0, 0xF0, 0xB0, 0x70, 0xF0,
    };
}

using namespace melonDS;

// ============================================================
// Constructor / Destructor
// ============================================================

MelonPrimeCore::MelonPrimeCore(EmuInstance* instance)
    : emuInstance(instance),
    localCfg(instance->getLocalConfig()),
    globalCfg(instance->getGlobalConfig())
{
    memset(&addr, 0, sizeof(addr));
    m_blockStylusAim = false;
}

MelonPrimeCore::~MelonPrimeCore() {}

// ============================================================
// Initialization
// ============================================================

void MelonPrimeCore::Initialize()
{
    isJoy2KeySupport = localCfg.GetBool("Metroid.Apply.joy2KeySupport");
    isSnapTapMode = localCfg.GetBool("Metroid.Operation.SnapTap");
    isStylusMode = localCfg.GetBool("Metroid.Enable.stylusMode");

    recalcAimSensitivityCache(localCfg);
    applyAimAdjustSetting(localCfg);

#ifdef _WIN32
    SetupRawInput();
#endif
}

void MelonPrimeCore::SetFrameAdvanceFunc(std::function<void()> func)
{
    m_frameAdvanceFunc = func;
}

void MelonPrimeCore::SetupRawInput()
{
#ifdef _WIN32
    if (!g_rawFilter) {
        HWND hwnd = nullptr;
        if (auto* mw = emuInstance->getMainWindow()) {
            hwnd = reinterpret_cast<HWND>(mw->winId());
        }
        g_rawFilter = new RawInputWinFilter(isJoy2KeySupport, hwnd);
        ApplyJoy2KeySupportAndQtFilter(isJoy2KeySupport);
        BindMetroidHotkeysFromConfig(g_rawFilter, emuInstance->getInstanceID());
    }
#endif
}

void MelonPrimeCore::ApplyJoy2KeySupportAndQtFilter(bool enable, bool doReset)
{
#ifdef _WIN32
    if (!g_rawFilter) return;
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) return;

    HWND hwnd = nullptr;
    if (auto* mw = emuInstance->getMainWindow()) {
        hwnd = reinterpret_cast<HWND>(mw->winId());
    }
    g_rawFilter->setRawInputTarget(hwnd);

    isJoy2KeySupport = enable;
    static bool s_isInstalled = false;

    if (!enable) {
        if (s_isInstalled) {
            app->removeNativeEventFilter(g_rawFilter);
            s_isInstalled = false;
        }
        g_rawFilter->setJoy2KeySupport(false);
        if (doReset) {
            g_rawFilter->resetAllKeys();
            g_rawFilter->resetMouseButtons();
            g_rawFilter->resetHotkeyEdges();
        }
    }
    else {
        g_rawFilter->setJoy2KeySupport(true);
        if (!s_isInstalled) {
            app->installNativeEventFilter(g_rawFilter);
            s_isInstalled = true;
        }
        if (doReset) {
            g_rawFilter->resetAllKeys();
            g_rawFilter->resetMouseButtons();
            g_rawFilter->resetHotkeyEdges();
        }
    }
#endif
}

// ============================================================
// Input Cache Implementation (Plan 1)
// ============================================================

void MelonPrimeCore::UpdateInputState()
{
    // Reset cache
    m_input.down = 0;
    m_input.press = 0;
    m_input.mouseX = 0;
    m_input.mouseY = 0;

    // --- Batch read inputs into cache ---
    // Using macros once here reduces atomic overhead for the rest of the frame

    // Movement
    if (MP_HK_DOWN_RAW(HK_MetroidMoveForward)) m_input.down |= IB_MOVE_F;
    if (MP_HK_DOWN_RAW(HK_MetroidMoveBack))    m_input.down |= IB_MOVE_B;
    if (MP_HK_DOWN_RAW(HK_MetroidMoveLeft))    m_input.down |= IB_MOVE_L;
    if (MP_HK_DOWN_RAW(HK_MetroidMoveRight))   m_input.down |= IB_MOVE_R;

    // Actions
    if (MP_HK_DOWN_RAW(HK_MetroidJump))        m_input.down |= IB_JUMP;
    if (MP_HK_DOWN_RAW(HK_MetroidZoom))        m_input.down |= IB_ZOOM;
    if (MP_HK_DOWN_RAW(HK_MetroidShootScan) || MP_HK_DOWN_RAW(HK_MetroidScanShoot)) m_input.down |= IB_SHOOT;
    if (MP_HK_DOWN_RAW(HK_MetroidWeaponCheck)) m_input.down |= IB_WEAPON_CHECK;
    if (MP_HK_DOWN_RAW(HK_MetroidHoldMorphBallBoost)) m_input.down |= IB_MORPH_BOOST;
    if (MP_HK_DOWN_RAW(HK_MetroidMenu))        m_input.down |= IB_MENU;

    // Pressed (Edges)
    if (MP_HK_PRESSED_RAW(HK_MetroidMorphBall)) m_input.press |= IB_MORPH;
    if (MP_HK_PRESSED_RAW(HK_MetroidScanVisor)) m_input.press |= IB_SCAN_VISOR;

    // UI
    if (MP_HK_PRESSED_RAW(HK_MetroidUIOk))    m_input.press |= IB_UI_OK;
    if (MP_HK_PRESSED_RAW(HK_MetroidUILeft))  m_input.press |= IB_UI_LEFT;
    if (MP_HK_PRESSED_RAW(HK_MetroidUIRight)) m_input.press |= IB_UI_RIGHT;
    if (MP_HK_PRESSED_RAW(HK_MetroidUIYes))   m_input.press |= IB_UI_YES;
    if (MP_HK_PRESSED_RAW(HK_MetroidUINo))    m_input.press |= IB_UI_NO;

    // Weapons
    if (MP_HK_PRESSED_RAW(HK_MetroidWeaponBeam))    m_input.press |= IB_WEAPON_BEAM;
    if (MP_HK_PRESSED_RAW(HK_MetroidWeaponMissile)) m_input.press |= IB_WEAPON_MISSILE;
    if (MP_HK_PRESSED_RAW(HK_MetroidWeapon1))       m_input.press |= IB_WEAPON_1;
    if (MP_HK_PRESSED_RAW(HK_MetroidWeapon2))       m_input.press |= IB_WEAPON_2;
    if (MP_HK_PRESSED_RAW(HK_MetroidWeapon3))       m_input.press |= IB_WEAPON_3;
    if (MP_HK_PRESSED_RAW(HK_MetroidWeapon4))       m_input.press |= IB_WEAPON_4;
    if (MP_HK_PRESSED_RAW(HK_MetroidWeapon5))       m_input.press |= IB_WEAPON_5;
    if (MP_HK_PRESSED_RAW(HK_MetroidWeapon6))       m_input.press |= IB_WEAPON_6;
    if (MP_HK_PRESSED_RAW(HK_MetroidWeaponSpecial)) m_input.press |= IB_WEAPON_SPECIAL;
    if (MP_HK_PRESSED_RAW(HK_MetroidWeaponNext))    m_input.press |= IB_WEAPON_NEXT;
    if (MP_HK_PRESSED_RAW(HK_MetroidWeaponPrevious)) m_input.press |= IB_WEAPON_PREV;

    // Mouse Delta
#if defined(_WIN32)
    if (g_rawFilter) g_rawFilter->fetchMouseDelta(m_input.mouseX, m_input.mouseY);
#else
    const QPoint currentPos = QCursor::pos();
    m_input.mouseX = currentPos.x() - aimData.centerX;
    m_input.mouseY = currentPos.y() - aimData.centerY;
#endif
}

// ============================================================
// Lifecycle Callbacks
// ============================================================

void MelonPrimeCore::OnEmuStart()
{
    isInGame = false;
    isRomDetected = false;
    isInGameAndHasInitialized = false;
    wasInGameForRenderer = false;
    m_blockStylusAim = false;

    isHeadphoneApplied = false;
    isVolumeSfxApplied = false;
    isVolumeMusicApplied = false;
    isUnlockMapsHuntersApplied = false;

    INPUT_RESET();
    UpdateRendererSettings();
}

void MelonPrimeCore::OnEmuStop()
{
    isInGame = false;
    wasInGameForRenderer = false;
}

void MelonPrimeCore::OnEmuPause() {}

void MelonPrimeCore::OnEmuUnpause()
{
    isSnapTapMode = localCfg.GetBool("Metroid.Operation.SnapTap");
    isStylusMode = localCfg.GetBool("Metroid.Enable.stylusMode");
    isJoy2KeySupport = localCfg.GetBool("Metroid.Apply.joy2KeySupport");

    ApplyJoy2KeySupportAndQtFilter(isJoy2KeySupport);

    isUnlockMapsHuntersApplied = false;
    isHeadphoneApplied = false;
    isVolumeSfxApplied = false;
    isVolumeMusicApplied = false;
    m_blockStylusAim = false;

    recalcAimSensitivityCache(localCfg);
    applyAimAdjustSetting(localCfg);

#ifdef _WIN32
    if (g_rawFilter) {
        BindMetroidHotkeysFromConfig(g_rawFilter, emuInstance->getInstanceID());
        g_rawFilter->resetHotkeyEdges();
    }
#endif

    if (isInGame) {
        UpdateRendererSettings();
        ApplyMphSensitivity(emuInstance->getNDS(), localCfg, addr.sensitivity, addr.inGameSensi, isInGameAndHasInitialized);
    }
}

void MelonPrimeCore::OnReset() { OnEmuStart(); }

void MelonPrimeCore::UpdateRendererSettings()
{
    bool vsyncFlag = globalCfg.GetBool("Screen.VSync");
    emuInstance->setVSyncGL(vsyncFlag);
}

bool MelonPrimeCore::ShouldForceSoftwareRenderer() const
{
    if (!isRomDetected) return false;
    return !isInGame;
}

// ============================================================
// ROM Detection
// ============================================================

void MelonPrimeCore::DetectRomAndSetAddresses()
{
    struct RomInfo {
        uint32_t checksum;
        const char* name;
        RomGroup group;
    };

    static const RomInfo ROM_INFO_TABLE[] = {
        {RomVersions::US1_1,           "US1.1",           GROUP_US1_1},
        {RomVersions::US1_1_ENCRYPTED, "US1.1 ENCRYPTED", GROUP_US1_1},
        {RomVersions::US1_0,           "US1.0",           GROUP_US1_0},
        {RomVersions::US1_0_ENCRYPTED, "US1.0 ENCRYPTED", GROUP_US1_0},
        {RomVersions::EU1_1,           "EU1.1",           GROUP_EU1_1},
        {RomVersions::EU1_1_ENCRYPTED, "EU1.1 ENCRYPTED", GROUP_EU1_1},
        {RomVersions::EU1_1_BALANCED,  "EU1.1 BALANCED",  GROUP_EU1_1},
        {RomVersions::EU1_1_RUSSIANED, "EU1.1 RUSSIANED", GROUP_EU1_1},
        {RomVersions::EU1_0,           "EU1.0",           GROUP_EU1_0},
        {RomVersions::EU1_0_ENCRYPTED, "EU1.0 ENCRYPTED", GROUP_EU1_0},
        {RomVersions::JP1_0,           "JP1.0",           GROUP_JP1_0},
        {RomVersions::JP1_0_ENCRYPTED, "JP1.0 ENCRYPTED", GROUP_JP1_0},
        {RomVersions::JP1_1,           "JP1.1",           GROUP_JP1_1},
        {RomVersions::JP1_1_ENCRYPTED, "JP1.1 ENCRYPTED", GROUP_JP1_1},
        {RomVersions::KR1_0,           "KR1.0",           GROUP_KR1_0},
        {RomVersions::KR1_0_ENCRYPTED, "KR1.0 ENCRYPTED", GROUP_KR1_0},
    };

    const RomInfo* romInfo = nullptr;
    for (const auto& info : ROM_INFO_TABLE) {
        if (globalChecksum == info.checksum) {
            romInfo = &info;
            break;
        }
    }

    if (!romInfo) return;

    detectRomAndSetAddresses_fast(
        romInfo->group, addr.baseChosenHunter, addr.inGame, addr.playerPos,
        addr.baseIsAltForm, addr.baseWeaponChange, addr.baseSelectedWeapon,
        addr.baseAimX, addr.baseAimY, addr.isInAdventure, addr.isMapOrUserActionPaused,
        addr.unlockMapsHunters, addr.sensitivity, addr.mainHunter, addr.baseLoadedSpecialWeapon
    );

    addr.isInVisorOrMap = addr.playerPos - 0xABB;
    addr.baseJumpFlag = addr.baseSelectedWeapon - 0xA;
    addr.volSfx8Bit = addr.unlockMapsHunters;
    addr.volMusic8Bit = addr.volSfx8Bit + 0x1;
    addr.operationAndSound = addr.unlockMapsHunters - 0x1;
    addr.unlockMapsHunters2 = addr.unlockMapsHunters + 0x3;
    addr.unlockMapsHunters3 = addr.unlockMapsHunters + 0x7;
    addr.unlockMapsHunters4 = addr.unlockMapsHunters + 0xB;
    addr.unlockMapsHunters5 = addr.unlockMapsHunters + 0xF;
    addr.dsNameFlagAndMicVolume = addr.unlockMapsHunters5 + 0x1;
    addr.baseInGameSensi = addr.sensitivity + 0xB4;
    addr.rankColor = addr.mainHunter + 0x3;

    isRomDetected = true;
    char message[256];
    snprintf(message, sizeof(message), "MPH Rom Detected: %s", romInfo->name);
    emuInstance->osdAddMessage(0, message);

    recalcAimSensitivityCache(localCfg);
    applyAimAdjustSetting(localCfg);
}

// ============================================================
// Global Hotkeys
// ============================================================

void MelonPrimeCore::HandleGlobalHotkeys()
{
    // Global hotkeys are less frequent, so we can use direct access or check released logic here.
    // For consistency with optimizing the game loop, we can leave these as direct checks
    // since they aren't part of the "UpdateInputState" tight cache.
    int sensitivityChange = 0;
    if (emuInstance->hotkeyReleased(HK_MetroidIngameSensiUp)) sensitivityChange = 1;
    else if (emuInstance->hotkeyReleased(HK_MetroidIngameSensiDown)) sensitivityChange = -1;

    if (sensitivityChange != 0) {
        int currentSensitivity = localCfg.GetInt("Metroid.Sensitivity.Aim");
        int newSensitivity = currentSensitivity + sensitivityChange;

        if (newSensitivity < 1) {
            emuInstance->osdAddMessage(0, "AimSensi cannot be decreased below 1");
        }
        else if (newSensitivity != currentSensitivity) {
            localCfg.SetInt("Metroid.Sensitivity.Aim", newSensitivity);
            Config::Save();
            recalcAimSensitivityCache(localCfg);
            emuInstance->osdAddMessage(0, "AimSensi Updated: %d->%d", currentSensitivity, newSensitivity);
        }
    }
}

// ============================================================
// Main Frame Hook (Optimized)
// ============================================================

void MelonPrimeCore::RunFrameHook()
{
    static bool isRunningHook = false;

    // Use safe RAM access again
    melonDS::u8* mainRAM = emuInstance->getNDS()->MainRAM;

    if (Q_UNLIKELY(isRunningHook)) {
        // Recursive call handling
        // IMPORTANT: We do NOT call UpdateInputState() here to avoid thrashing inputs in recursive steps
        ProcessMoveInputFast();
        INPUT_SET(INPUT_B, !IsDown(IB_JUMP));

        if (isStylusMode) {
            if (emuInstance->isTouching && !m_blockStylusAim) {
                emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
            }
        }
        else {
            ProcessAimInputMouse(mainRAM);
        }

        bool isShoot = IsDown(IB_SHOOT);
        INPUT_SET(INPUT_L, !isShoot);
        INPUT_SET(INPUT_R, !IsDown(IB_ZOOM));
        return;
    }

    isRunningHook = true;

    // --- Plan 1: Snapshot Input ---
    // Only pay the Atomic cost ONCE per frame
    UpdateInputState();

    // Reset at frame start
    INPUT_RESET();
    m_blockStylusAim = false;

    HandleGlobalHotkeys();

    if (isStylusMode) {
        isFocused = true;
    }

    bool wasDetected = isRomDetected;
    if (!isRomDetected) DetectRomAndSetAddresses();
    if (!wasDetected && isRomDetected) emuInstance->getEmuThread()->updateVideoRenderer();

    if (Q_LIKELY(isRomDetected)) {
        isInGame = FastRead16(mainRAM, addr.inGame) == 0x0001;

        if (isInGame != wasInGameForRenderer) {
            wasInGameForRenderer = isInGame;
            emuInstance->getEmuThread()->updateVideoRenderer();
        }

        bool shouldBeCursorMode = !isInGame || (isInAdventure && isPaused);

        if (isInGame && !isInGameAndHasInitialized) {
            isInGameAndHasInitialized = true;
            constexpr uint16_t kPlayerAddrInc = 0xF30;
            constexpr uint8_t kAimAddrInc = 0x48;

            playerPosition = FastRead8(mainRAM, addr.playerPos);
            addr.isAltForm = calculatePlayerAddress(addr.baseIsAltForm, playerPosition, kPlayerAddrInc);
            addr.loadedSpecialWeapon = calculatePlayerAddress(addr.baseLoadedSpecialWeapon, playerPosition, kPlayerAddrInc);
            addr.chosenHunter = calculatePlayerAddress(addr.baseChosenHunter, playerPosition, 0x01);
            addr.weaponChange = calculatePlayerAddress(addr.baseWeaponChange, playerPosition, kPlayerAddrInc);
            addr.selectedWeapon = calculatePlayerAddress(addr.baseSelectedWeapon, playerPosition, kPlayerAddrInc);
            addr.currentWeapon = addr.selectedWeapon - 0x1;
            addr.havingWeapons = addr.selectedWeapon + 0x3;
            addr.weaponAmmo = addr.selectedWeapon - 0x383;
            addr.jumpFlag = calculatePlayerAddress(addr.baseJumpFlag, playerPosition, kPlayerAddrInc);

            uint8_t hunterID = FastRead8(mainRAM, addr.chosenHunter);
            isSamus = (hunterID == 0x00);
            isWeavel = (hunterID == 0x06);

            addr.inGameSensi = calculatePlayerAddress(addr.baseInGameSensi, playerPosition, 0x04);
            addr.boostGauge = addr.loadedSpecialWeapon - 0x12;
            addr.isBoosting = addr.loadedSpecialWeapon - 0x10;
            addr.aimX = calculatePlayerAddress(addr.baseAimX, playerPosition, kAimAddrInc);
            addr.aimY = calculatePlayerAddress(addr.baseAimY, playerPosition, kAimAddrInc);

            isInAdventure = FastRead8(mainRAM, addr.isInAdventure) == 0x02;
        }

        if (isFocused) {
            if (Q_LIKELY(isInGame)) {
                HandleInGameLogic(mainRAM);
            }
            else {
                isInAdventure = false;
                isAimDisabled = true;
                if (isInGameAndHasInitialized) isInGameAndHasInitialized = false;
                ApplyGameSettingsOnce();
            }

            if (shouldBeCursorMode != isCursorMode) {
                isCursorMode = shouldBeCursorMode;
                setAimBlock(AIMBLK_CURSOR_MODE, isCursorMode);
                if (!isStylusMode) ShowCursor(isCursorMode);
            }

            if (isCursorMode) {
                if (emuInstance->isTouching)
                    emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
                else
                    emuInstance->getNDS()->ReleaseScreen();
            }
            INPUT_SET(INPUT_START, !IsDown(IB_MENU));
        }
        else {
#ifdef _WIN32
            if (g_rawFilter) {
                g_rawFilter->discardDeltas();
                g_rawFilter->resetMouseButtons();
                g_rawFilter->resetAllKeys();
                g_rawFilter->resetHotkeyEdges();
            }
#endif
        }
        wasLastFrameFocused = isFocused;
    }
    isRunningHook = false;
}

// ============================================================
// In-Game Logic
// ============================================================

void MelonPrimeCore::HandleInGameLogic(melonDS::u8* mainRAM)
{
    // 1. Transform
    if (IsPressed(IB_MORPH)) {
        if (isStylusMode) m_blockStylusAim = true;
        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();
        emuInstance->getNDS()->TouchScreen(231, 167);
        FrameAdvanceTwice();
        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();
    }

    // 2. Weapon Switch
    if (ProcessWeaponSwitch(mainRAM)) {
        if (isStylusMode) m_blockStylusAim = true;
    }

    // 3. Weapon Check
    static bool isWeaponCheckActive = false;
    if (IsDown(IB_WEAPON_CHECK)) {
        if (isStylusMode) m_blockStylusAim = true;
        if (!isWeaponCheckActive) {
            isWeaponCheckActive = true;
            setAimBlock(AIMBLK_CHECK_WEAPON, true);
            emuInstance->getNDS()->ReleaseScreen();
            FrameAdvanceTwice();
        }
        emuInstance->getNDS()->TouchScreen(236, 30);
    }
    else if (isWeaponCheckActive) {
        isWeaponCheckActive = false;
        emuInstance->getNDS()->ReleaseScreen();
        setAimBlock(AIMBLK_CHECK_WEAPON, false);
        FrameAdvanceTwice();
    }

    // 4. Adventure Mode
    if (Q_UNLIKELY(isInAdventure)) {
        HandleAdventureMode(mainRAM);
    }

    // 5. Movement (optimized)
    ProcessMoveInputFast();

    // 6. Basic inputs
    INPUT_SET(INPUT_B, !IsDown(IB_JUMP));
    bool isShoot = IsDown(IB_SHOOT);
    INPUT_SET(INPUT_L, !isShoot);
    INPUT_SET(INPUT_R, !IsDown(IB_ZOOM));

    // 7. Morph Ball Boost
    HandleMorphBallBoost(mainRAM);

    // 8. Aim
    if (isStylusMode) {
        if (!m_blockStylusAim) {
            ProcessAimInputStylus();
        }
    }
    else {
        ProcessAimInputMouse(mainRAM);
        if (!wasLastFrameFocused || !isAimDisabled) {
            emuInstance->getNDS()->TouchScreen(128, 88);
        }
    }
}

// ============================================================
// Movement Input (Optimized with LUT + Cached Input)
// ============================================================

void MelonPrimeCore::ProcessMoveInputFast()
{
    static uint16_t snapState = 0;

    const uint32_t f = IsDown(IB_MOVE_F) ? 1 : 0;
    const uint32_t b = IsDown(IB_MOVE_B) ? 1 : 0;
    const uint32_t l = IsDown(IB_MOVE_L) ? 1 : 0;
    const uint32_t r = IsDown(IB_MOVE_R) ? 1 : 0;
    const uint32_t curr = f | (b << 1) | (l << 2) | (r << 3);

    uint32_t finalInput;

    if (Q_LIKELY(!isSnapTapMode)) {
        finalInput = curr;
    }
    else {
        // SnapTap logic
        const uint32_t last = snapState & 0xFFu;
        const uint32_t priority = snapState >> 8;
        const uint32_t newPress = curr & ~last;
        const uint32_t hConflict = ((curr & 0x3u) == 0x3u) ? 0x3u : 0u;
        const uint32_t vConflict = ((curr & 0xCu) == 0xCu) ? 0xCu : 0u;
        const uint32_t conflict = vConflict | hConflict;
        const uint32_t updateMask = (newPress & conflict) ? ~0u : 0u;
        const uint32_t newPriority = (priority & ~(conflict & updateMask)) | (newPress & conflict & updateMask);
        const uint32_t activePriority = newPriority & curr;

        snapState = static_cast<uint16_t>((curr & 0xFFu) | ((activePriority & 0xFFu) << 8));
        finalInput = (curr & ~conflict) | (activePriority & conflict);
    }

    // Apply LUT
    const uint8_t lutResult = MoveLUT[finalInput & 0xF];
    gInputMaskFast = (gInputMaskFast & 0xFF0Fu) | (static_cast<uint16_t>(lutResult) & 0x00F0u);
}

// Legacy function
void MelonPrimeCore::ProcessMoveInput(QBitArray& mask)
{
    ProcessMoveInputFast();
    mask.setBit(INPUT_UP, (gInputMaskFast >> INPUT_UP) & 1);
    mask.setBit(INPUT_DOWN, (gInputMaskFast >> INPUT_DOWN) & 1);
    mask.setBit(INPUT_LEFT, (gInputMaskFast >> INPUT_LEFT) & 1);
    mask.setBit(INPUT_RIGHT, (gInputMaskFast >> INPUT_RIGHT) & 1);
}

// ============================================================
// Aim Input
// ============================================================

void MelonPrimeCore::ProcessAimInputStylus()
{
    if (Q_LIKELY(emuInstance->isTouching)) {
        emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
    }
    else {
        emuInstance->getNDS()->ReleaseScreen();
    }
}

void MelonPrimeCore::ProcessAimInputMouse(melonDS::u8* mainRAM)
{
    if (isAimDisabled) return;

    if (isLayoutChangePending) {
        isLayoutChangePending = false;
#ifdef _WIN32
        if (g_rawFilter) g_rawFilter->discardDeltas();
#endif
        return;
    }

    if (Q_LIKELY(wasLastFrameFocused)) {
        // Use cached delta (Plan 1)
        int deltaX = m_input.mouseX;
        int deltaY = m_input.mouseY;

        if ((deltaX | deltaY) == 0) return;
        float scaledX = deltaX * gAimSensiFactor;
        float scaledY = deltaY * gAimCombinedY;
        AIM_ADJUST(scaledX, scaledY);
        if (scaledX == 0.0f && scaledY == 0.0f) return;

        FastWrite16(mainRAM, addr.aimX, static_cast<int16_t>(scaledX));
        FastWrite16(mainRAM, addr.aimY, static_cast<int16_t>(scaledY));

#if !defined(_WIN32)
        QCursor::setPos(aimData.centerX, aimData.centerY);
#endif
        return;
    }

#if !defined(_WIN32)
    const QPoint center = GetAdjustedCenter();
    aimData.centerX = center.x();
    aimData.centerY = center.y();
    QCursor::setPos(center);
#endif
    isLayoutChangePending = false;
}

// ============================================================
// Morph Ball Boost
// ============================================================

bool MelonPrimeCore::HandleMorphBallBoost(melonDS::u8* mainRAM)
{
    if (!isSamus) return false;

    if (IsDown(IB_MORPH_BOOST)) {
        isAltForm = FastRead8(mainRAM, addr.isAltForm) == 0x02;

        if (isAltForm) {
            uint8_t boostGaugeValue = FastRead8(mainRAM, addr.boostGauge);
            bool isBoosting = FastRead8(mainRAM, addr.isBoosting) != 0x00;
            bool isBoostGaugeEnough = boostGaugeValue > 0x0A;

            setAimBlock(AIMBLK_MORPHBALL_BOOST, true);

            if (!IsDown(IB_WEAPON_CHECK)) {
                emuInstance->getNDS()->ReleaseScreen();
            }

            INPUT_SET(INPUT_R, (!isBoosting && isBoostGaugeEnough));

            if (isBoosting) {
                setAimBlock(AIMBLK_MORPHBALL_BOOST, false);
            }
            return true;
        }
    }
    else {
        setAimBlock(AIMBLK_MORPHBALL_BOOST, false);
    }
    return false;
}

// ============================================================
// Adventure Mode
// ============================================================

void MelonPrimeCore::HandleAdventureMode(melonDS::u8* mainRAM)
{
    isPaused = FastRead8(mainRAM, addr.isMapOrUserActionPaused) == 0x1;

    if (IsPressed(IB_SCAN_VISOR)) {
        if (isStylusMode) m_blockStylusAim = true;

        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();
        emuInstance->getNDS()->TouchScreen(128, 173);

        if (FastRead8(mainRAM, addr.isInVisorOrMap) == 0x1) {
            FrameAdvanceTwice();
        }
        else {
            // Important: FrameAdvance loop requires updated input!
            for (int i = 0; i < 30; i++) {
                // Must update snapshot for each frame iteration since time is advancing
                UpdateInputState();
                ProcessMoveInputFast();
                emuInstance->getNDS()->SetKeyMask(gInputMaskFast);
                FrameAdvanceOnce();
            }
        }
        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();
    }

    TOUCH_IF(IB_UI_OK, 128, 142)
        TOUCH_IF(IB_UI_LEFT, 71, 141)
        TOUCH_IF(IB_UI_RIGHT, 185, 141)
        TOUCH_IF(IB_UI_YES, 96, 142)
        TOUCH_IF(IB_UI_NO, 160, 142)
}

// ============================================================
// Weapon Switch
// ============================================================

bool MelonPrimeCore::ProcessWeaponSwitch(melonDS::u8* mainRAM)
{
    static constexpr uint8_t  WEAPON_ORDER[] = { 0, 2, 7, 6, 5, 4, 3, 1, 8 };
    static constexpr uint16_t WEAPON_MASKS[] = { 0x001, 0x004, 0x080, 0x040, 0x020, 0x010, 0x008, 0x002, 0x100 };
    static constexpr uint8_t  MIN_AMMO[] = { 0, 0x5, 0xA, 0x4, 0x14, 0x5, 0xA, 0xA, 0 };
    static constexpr uint8_t  WEAPON_INDEX_MAP[] = { 0, 7, 1, 6, 5, 4, 3, 2, 8 };
    static constexpr uint8_t  WEAPON_COUNT = 9;

    // Map bits to weapons: order corresponds to BIT_WEAPON_BEAM...
    static constexpr uint64_t BIT_MAP[] = {
        IB_WEAPON_BEAM, IB_WEAPON_MISSILE, IB_WEAPON_1, IB_WEAPON_2,
        IB_WEAPON_3, IB_WEAPON_4, IB_WEAPON_5, IB_WEAPON_6, IB_WEAPON_SPECIAL
    };
    static constexpr uint8_t BIT_WEAPON_ID[] = {
        0, 2, 7, 6, 5, 4, 3, 1, 0xFF
    };

    uint32_t hot = 0;
    for (size_t i = 0; i < 9; ++i) {
        if (IsPressed(BIT_MAP[i])) hot |= (1u << i);
    }

    if (hot) {
        if (isStylusMode) m_blockStylusAim = true;

        const int firstSet = __builtin_ctz(hot);
        if (Q_UNLIKELY(firstSet == 8)) {
            const uint8_t loaded = FastRead8(mainRAM, addr.loadedSpecialWeapon);
            if (loaded == 0xFF) {
                emuInstance->osdAddMessage(0, "Have not Special Weapon yet!");
                m_blockStylusAim = false;
                return false;
            }
            SwitchWeapon(mainRAM, loaded);
            return true;
        }

        const uint8_t weaponID = BIT_WEAPON_ID[firstSet];
        const uint16_t having = FastRead16(mainRAM, addr.havingWeapons);
        const uint32_t ammoData = FastRead32(mainRAM, addr.weaponAmmo);
        const uint16_t weaponAmmo = static_cast<uint16_t>(ammoData & 0xFFFF);
        const uint16_t missileAmmo = static_cast<uint16_t>(ammoData >> 16);

        bool owned = (weaponID == 0 || weaponID == 2) ? true : ((having & WEAPON_MASKS[WEAPON_INDEX_MAP[weaponID]]) != 0);
        if (!owned) {
            emuInstance->osdAddMessage(0, "Have not %s yet!", kWeaponNames[weaponID]);
            m_blockStylusAim = false;
            return false;
        }

        bool hasAmmo = true;
        if (weaponID == 2) hasAmmo = (missileAmmo >= 0xA);
        else if (weaponID != 0 && weaponID != 2 && weaponID != 8) {
            uint8_t required = MIN_AMMO[weaponID];
            if (weaponID == 3 && isWeavel) required = 0x5;
            hasAmmo = (weaponAmmo >= required);
        }

        if (!hasAmmo) {
            emuInstance->osdAddMessage(0, "Not enough Ammo for %s!", kWeaponNames[weaponID]);
            m_blockStylusAim = false;
            return false;
        }
        SwitchWeapon(mainRAM, weaponID);
        return true;
    }

    auto* panel = emuInstance->getMainWindow()->panel;
    if (!panel) return false;
    const int wheelDelta = panel->getDelta();
    const bool nextKey = IsPressed(IB_WEAPON_NEXT);
    const bool prevKey = IsPressed(IB_WEAPON_PREV);
    if (!wheelDelta && !nextKey && !prevKey) return false;

    if (isStylusMode) m_blockStylusAim = true;

    const bool forward = (wheelDelta < 0) || nextKey;
    const uint8_t curID = FastRead8(mainRAM, addr.currentWeapon);
    const uint16_t having = FastRead16(mainRAM, addr.havingWeapons);
    const uint32_t ammoData = FastRead32(mainRAM, addr.weaponAmmo);
    const uint16_t weaponAmmo = static_cast<uint16_t>(ammoData & 0xFFFF);
    const uint16_t missileAmmo = static_cast<uint16_t>(ammoData >> 16);

    uint16_t available = 0;
    for (int i = 0; i < WEAPON_COUNT; ++i) {
        const uint8_t wid = WEAPON_ORDER[i];
        bool owned_i = (wid == 0 || wid == 2) ? true : ((having & WEAPON_MASKS[i]) != 0);
        if (!owned_i) continue;
        bool ok = true;
        if (wid == 2) ok = (missileAmmo >= 0xA);
        else if (wid != 0 && wid != 2 && wid != 8) {
            uint8_t req = MIN_AMMO[wid];
            if (wid == 3 && isWeavel) req = 0x5;
            ok = (weaponAmmo >= req);
        }
        if (ok) available |= (1u << i);
    }
    if (!available) {
        m_blockStylusAim = false;
        return false;
    }

    uint8_t idx = WEAPON_INDEX_MAP[curID];
    for (int n = 0; n < WEAPON_COUNT; ++n) {
        idx = forward ? static_cast<uint8_t>((idx + 1) % WEAPON_COUNT)
            : static_cast<uint8_t>((idx + WEAPON_COUNT - 1) % WEAPON_COUNT);
        if (available & (1u << idx)) {
            SwitchWeapon(mainRAM, WEAPON_ORDER[idx]);
            return true;
        }
    }

    m_blockStylusAim = false;
    return false;
}

// ============================================================
// Switch Weapon
// ============================================================

void MelonPrimeCore::SwitchWeapon(melonDS::u8* mainRAM, int weaponIndex)
{
    if (FastRead8(mainRAM, addr.selectedWeapon) == weaponIndex) return;

    if (isInAdventure) {
        if (isPaused) {
            emuInstance->osdAddMessage(0, "You can't switch weapon now!");
            return;
        }
        if (FastRead8(mainRAM, addr.isInVisorOrMap) == 0x1) return;
    }

    uint8_t currentJumpFlags = FastRead8(mainRAM, addr.jumpFlag);
    bool isTransforming = currentJumpFlags & 0x10;
    uint8_t jumpFlag = currentJumpFlags & 0x0F;
    bool isRestoreNeeded = false;
    isAltForm = FastRead8(mainRAM, addr.isAltForm) == 0x02;

    if (!isTransforming && jumpFlag == 0 && !isAltForm) {
        FastWrite8(mainRAM, addr.jumpFlag, (currentJumpFlags & 0xF0) | 0x01);
        isRestoreNeeded = true;
    }

    FastWrite8(mainRAM, addr.weaponChange, (FastRead8(mainRAM, addr.weaponChange) & 0xF0) | 0x0B);
    FastWrite8(mainRAM, addr.selectedWeapon, weaponIndex);
    emuInstance->getNDS()->ReleaseScreen();
    FrameAdvanceTwice();

    if (!isStylusMode) {
        emuInstance->getNDS()->TouchScreen(128, 88);
    }
    else if (emuInstance->isTouching) {
        emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
    }
    FrameAdvanceTwice();

    if (isRestoreNeeded) {
        currentJumpFlags = FastRead8(mainRAM, addr.jumpFlag);
        FastWrite8(mainRAM, addr.jumpFlag, (currentJumpFlags & 0xF0) | jumpFlag);
    }
}

// ============================================================
// Apply Game Settings
// ============================================================

void MelonPrimeCore::ApplyGameSettingsOnce()
{
    INPUT_SET(INPUT_L, !IsPressed(IB_UI_LEFT));
    INPUT_SET(INPUT_R, !IsPressed(IB_UI_RIGHT));

    ApplyHeadphoneOnce(emuInstance->getNDS(), localCfg, addr.operationAndSound, isHeadphoneApplied);
    ApplyMphSensitivity(emuInstance->getNDS(), localCfg, addr.sensitivity, addr.inGameSensi, isInGameAndHasInitialized);
    ApplyUnlockHuntersMaps(emuInstance->getNDS(), localCfg, isUnlockMapsHuntersApplied,
        addr.unlockMapsHunters, addr.unlockMapsHunters2, addr.unlockMapsHunters3,
        addr.unlockMapsHunters4, addr.unlockMapsHunters5);
    useDsName(emuInstance->getNDS(), localCfg, addr.dsNameFlagAndMicVolume);
    applySelectedHunterStrict(emuInstance->getNDS(), localCfg, addr.mainHunter);
    applyLicenseColorStrict(emuInstance->getNDS(), localCfg, addr.rankColor);
    ApplySfxVolumeOnce(emuInstance->getNDS(), localCfg, addr.volSfx8Bit, isVolumeSfxApplied);
    ApplyMusicVolumeOnce(emuInstance->getNDS(), localCfg, addr.volMusic8Bit, isVolumeMusicApplied);
}

// ============================================================
// Helper Functions
// ============================================================

void MelonPrimeCore::ShowCursor(bool show)
{
    auto* panel = emuInstance->getMainWindow()->panel;
    if (!panel) return;
    QMetaObject::invokeMethod(panel, [panel, show]() {
        panel->setCursor(show ? Qt::ArrowCursor : Qt::BlankCursor);
        if (show) panel->unclip();
        else panel->clipCursorCenter1px();
        }, Qt::ConnectionType::QueuedConnection);
}

void MelonPrimeCore::FrameAdvanceOnce()
{
    if (m_frameAdvanceFunc) m_frameAdvanceFunc();
    else {
        emuInstance->inputProcess();
        if (emuInstance->usesOpenGL()) emuInstance->makeCurrentGL();
        if (emuInstance->getNDS()->GPU.GetRenderer3D().NeedsShaderCompile()) {
            int currentShader, shadersCount;
            emuInstance->getNDS()->GPU.GetRenderer3D().ShaderCompileStep(currentShader, shadersCount);
        }
        else emuInstance->getNDS()->RunFrame();
        if (emuInstance->usesOpenGL()) emuInstance->drawScreenGL();
    }
}

void MelonPrimeCore::FrameAdvanceTwice()
{
    FrameAdvanceOnce();
    FrameAdvanceOnce();
}

QPoint MelonPrimeCore::GetAdjustedCenter()
{
    auto* panel = emuInstance->getMainWindow()->panel;
    if (!panel) return QPoint(0, 0);
    QRect r = panel->geometry();
    return panel->mapToGlobal(QPoint(r.width() / 2, r.height() / 2));
}

// ============================================================
// Static Helpers
// ============================================================

bool MelonPrimeCore::ApplyHeadphoneOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, bool& applied)
{
    if (!nds || applied) return false;
    if (!localCfg.GetBool("Metroid.Apply.Headphone")) return false;
    uint8_t oldVal = nds->ARM9Read8(addr);
    constexpr uint8_t kMask = 0x18;
    if ((oldVal & kMask) == kMask) { applied = true; return false; }
    nds->ARM9Write8(addr, oldVal | kMask);
    applied = true;
    return true;
}

bool MelonPrimeCore::applyLicenseColorStrict(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr)
{
    if (!nds || !localCfg.GetBool("Metroid.HunterLicense.Color.Apply")) return false;
    int sel = localCfg.GetInt("Metroid.HunterLicense.Color.Selected");
    if (sel < 0 || sel > 2) return false;
    constexpr uint8_t kColorBits[3] = { 0x00, 0x40, 0x80 };
    uint8_t oldVal = nds->ARM9Read8(addr);
    uint8_t newVal = (oldVal & 0x3F) | kColorBits[sel];
    if (newVal == oldVal) return false;
    nds->ARM9Write8(addr, newVal);
    return true;
}

bool MelonPrimeCore::applySelectedHunterStrict(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr)
{
    if (!nds || !localCfg.GetBool("Metroid.HunterLicense.Hunter.Apply")) return false;
    constexpr uint8_t kHunterBits[7] = { 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30 };
    int sel = localCfg.GetInt("Metroid.HunterLicense.Hunter.Selected");
    if (sel < 0) sel = 0; if (sel > 6) sel = 6;
    uint8_t oldVal = nds->ARM9Read8(addr);
    uint8_t newVal = (oldVal & ~0x78) | (kHunterBits[sel] & 0x78);
    if (newVal == oldVal) return false;
    nds->ARM9Write8(addr, newVal);
    return true;
}

bool MelonPrimeCore::useDsName(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr)
{
    if (!nds || !localCfg.GetBool("Metroid.Use.Firmware.Name")) return false;
    uint8_t oldVal = nds->ARM9Read8(addr);
    uint8_t newVal = oldVal & ~0x01;
    if (newVal == oldVal) return false;
    nds->ARM9Write8(addr, newVal);
    return true;
}

bool MelonPrimeCore::ApplySfxVolumeOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, bool& applied)
{
    if (!nds || applied || !localCfg.GetBool("Metroid.Apply.SfxVolume")) return false;
    uint8_t oldVal = nds->ARM9Read8(addr);
    uint8_t steps = static_cast<uint8_t>(std::clamp(localCfg.GetInt("Metroid.Volume.SFX"), 0, 9));
    uint8_t newVal = (oldVal & 0xC0) | ((steps & 0x0F) << 2) | 0x03;
    if (newVal == oldVal) { applied = true; return false; }
    nds->ARM9Write8(addr, newVal);
    applied = true;
    return true;
}

bool MelonPrimeCore::ApplyMusicVolumeOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, bool& applied)
{
    if (!nds || applied || !localCfg.GetBool("Metroid.Apply.MusicVolume")) return false;
    uint8_t oldVal = nds->ARM9Read8(addr);
    uint8_t steps = static_cast<uint8_t>(std::clamp(localCfg.GetInt("Metroid.Volume.Music"), 0, 9));
    uint8_t newVal = (oldVal & ~0x3C) | ((steps & 0x0F) << 2);
    if (newVal == oldVal) { applied = true; return false; }
    nds->ARM9Write8(addr, newVal);
    applied = true;
    return true;
}

void MelonPrimeCore::ApplyMphSensitivity(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addrSensi, melonDS::u32 addrInGame, bool inGameInit)
{
    double mphSensi = localCfg.GetDouble("Metroid.Sensitivity.Mph");
    uint16_t sensiVal = sensiNumToSensiVal(mphSensi);
    nds->ARM9Write16(addrSensi, sensiVal);
    if (inGameInit) nds->ARM9Write16(addrInGame, sensiVal);
}

bool MelonPrimeCore::ApplyUnlockHuntersMaps(melonDS::NDS* nds, Config::Table& localCfg, bool& applied,
    melonDS::u32 a1, melonDS::u32 a2, melonDS::u32 a3, melonDS::u32 a4, melonDS::u32 a5)
{
    if (applied || !localCfg.GetBool("Metroid.Data.Unlock")) return false;
    nds->ARM9Write8(a1, nds->ARM9Read8(a1) | 0x03);
    nds->ARM9Write32(a2, 0x07FFFFFF);
    nds->ARM9Write8(a3, 0x7F);
    nds->ARM9Write32(a4, 0xFFFFFFFF);
    nds->ARM9Write8(a5, 0x7F);
    applied = true;
    return true;
}

melonDS::u32 MelonPrimeCore::calculatePlayerAddress(melonDS::u32 base, melonDS::u8 pos, int32_t inc)
{
    if (pos == 0) return base;
    int64_t result = static_cast<int64_t>(base) + (static_cast<int64_t>(pos) * inc);
    if (result < 0 || result > UINT32_MAX) return base;
    return static_cast<melonDS::u32>(result);
}

// ============================================================
// Public API
// ============================================================

uint16_t MelonPrimeCore::GetInputMaskFast() const
{
    return gInputMaskFast;
}