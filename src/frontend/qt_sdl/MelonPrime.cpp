/*
    MelonPrimeDS Logic Implementation (Full Version: Performance Optimized)
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
// Static filter instance
static RawInputWinFilter* g_rawFilter = nullptr;
#endif

// --- Macros & Constants ---

// Input Indices
#define INPUT_A 0
#define INPUT_B 1
#define INPUT_SELECT 2
#define INPUT_START 3
#define INPUT_RIGHT 4
#define INPUT_LEFT 5
#define INPUT_UP 6
#define INPUT_DOWN 7
#define INPUT_R 8
#define INPUT_L 9
#define INPUT_X 10
#define INPUT_Y 11

// Input Mask Helper
#define GET_INPUT_MASK(inputMask) ( \
    (static_cast<uint32_t>((inputMask).testBit(0))  << 0)  | \
    (static_cast<uint32_t>((inputMask).testBit(1))  << 1)  | \
    (static_cast<uint32_t>((inputMask).testBit(2))  << 2)  | \
    (static_cast<uint32_t>((inputMask).testBit(3))  << 3)  | \
    (static_cast<uint32_t>((inputMask).testBit(4))  << 4)  | \
    (static_cast<uint32_t>((inputMask).testBit(5))  << 5)  | \
    (static_cast<uint32_t>((inputMask).testBit(6))  << 6)  | \
    (static_cast<uint32_t>((inputMask).testBit(7))  << 7)  | \
    (static_cast<uint32_t>((inputMask).testBit(8))  << 8)  | \
    (static_cast<uint32_t>((inputMask).testBit(9))  << 9)  | \
    (static_cast<uint32_t>((inputMask).testBit(10)) << 10) | \
    (static_cast<uint32_t>((inputMask).testBit(11)) << 11)   \
)

// Hotkey Macros
#if defined(_WIN32)
#define MP_JOY_DOWN(id)      (emuInstance->joyHotkeyMask.testBit((id)))
#define MP_JOY_PRESSED(id)   (emuInstance->joyHotkeyPress.testBit((id)))
#define MP_JOY_RELEASED(id)  (emuInstance->joyHotkeyRelease.testBit((id)))
#define MP_HK_DOWN(id)     ( (g_rawFilter && g_rawFilter->hotkeyDown((id)))     || MP_JOY_DOWN((id)) )
#define MP_HK_PRESSED(id)  ( (g_rawFilter && g_rawFilter->hotkeyPressed((id)))  || MP_JOY_PRESSED((id)) )
#define MP_HK_RELEASED(id) ( (g_rawFilter && g_rawFilter->hotkeyReleased((id))) || MP_JOY_RELEASED((id)) )
#else
#define MP_HK_DOWN(id)     ( emuInstance->hotkeyMask.testBit((id)) )
#define MP_HK_PRESSED(id)  ( emuInstance->hotkeyPress.testBit((id)) )
#define MP_HK_RELEASED(id) ( emuInstance->hotkeyRelease.testBit((id)) )
#endif

#define TOUCH_IF(PRESS, X, Y) \
    if (MP_HK_PRESSED(PRESS)) { \
        emuInstance->getNDS()->ReleaseScreen(); \
        FrameAdvanceTwice(); \
        emuInstance->getNDS()->TouchScreen((X), (Y)); \
        FrameAdvanceTwice(); \
    }

// Aim Block Logic
#ifndef AIMBLOCK_ATOMIC
#define AIMBLOCK_ATOMIC 0
#endif

#if AIMBLOCK_ATOMIC
#include <atomic>
using AimBitsType = std::atomic<uint32_t>;
#define AIMBITS_LOAD(x)    (x.load(std::memory_order_relaxed))
#define AIMBITS_STORE(x,v) (x.store((v), std::memory_order_relaxed))
#define AIMBITS_OR(x, m)   (x.fetch_or((m), std::memory_order_relaxed))
#define AIMBITS_AND(x, m)  (x.fetch_and((m), std::memory_order_relaxed))
#else
using AimBitsType = uint32_t;
#define AIMBITS_LOAD(x)    (x)
#define AIMBITS_STORE(x,v) ((x) = (v))
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
    isAimDisabled = AIMBLOCK_ATOMIC ? AIMBITS_LOAD(gAimBlockBits) : gAimBlockBits;
}

static inline void setAimBlock(uint32_t bitMask, bool enable) noexcept {
    if (enable) AIMBITS_OR(gAimBlockBits, bitMask);
    else AIMBITS_AND(gAimBlockBits, ~bitMask);
    syncIsAimDisabled();
}

// Aim Adjust Logic
static float gAimAdjust = 0.5f;

inline void applyAimAdjustSetting(Config::Table& cfg) noexcept {
    double v = cfg.GetDouble("Metroid.Aim.Adjust");
    if (std::isnan(v) || v < 0.0) v = 0.0;
    gAimAdjust = static_cast<float>(v);
}

inline void applyAimAdjustFloat(float& dx, float& dy) noexcept {
    const float a = gAimAdjust;
    if (a <= 0.0f) return;
    auto adj1 = [a](float& v) {
        float av = std::fabs(v);
        if (av < a) { v = 0.0f; }
        else if (av < 1.0f) { v = (v >= 0.0f ? 1.0f : -1.0f); }
        };
    adj1(dx);
    adj1(dy);
}
#define AIM_ADJUST(dx, dy) do { applyAimAdjustFloat((dx), (dy)); } while (0)

// Sensitivity Cache
static float gAimSensiFactor = 0.01f;
static float gAimCombinedY = 0.013333333f;

static inline void recalcAimSensitivityCache(Config::Table& localCfg) {
    const int sens = localCfg.GetInt("Metroid.Sensitivity.Aim");
    const float aimYAxisScale = static_cast<float>(localCfg.GetDouble("Metroid.Sensitivity.AimYAxisScale"));
    gAimSensiFactor = sens * 0.01f;
    gAimCombinedY = gAimSensiFactor * aimYAxisScale;
}

// Math Helpers
static inline double sensiValToSensiNum(std::uint32_t sensiVal) {
    constexpr std::uint32_t BASE_VAL = 0x0999;
    constexpr std::uint32_t STEP_VAL = 0x0199;
    const std::int64_t diff = static_cast<std::int64_t>(sensiVal) - static_cast<std::int64_t>(BASE_VAL);
    return static_cast<double>(diff) / static_cast<double>(STEP_VAL) + 1.0;
}

static inline std::uint16_t sensiNumToSensiVal(double sensiNum) {
    constexpr std::uint32_t BASE_VAL = 0x0999;
    constexpr std::uint32_t STEP_VAL = 0x0199;
    double steps = sensiNum - 1.0;
    double val = static_cast<double>(BASE_VAL) + steps * static_cast<double>(STEP_VAL);
    std::uint32_t result = static_cast<std::uint32_t>(std::llround(val));
    if (result > 0xFFFF) result = 0xFFFF;
    return static_cast<std::uint16_t>(result);
}

// Weapon Names for OSD
static const char* kWeaponNames[] = {
    "Power Beam",
    "Volt Driver",
    "Missile Launcher",
    "Battlehammer",
    "Imperialist",
    "Judicator",
    "Magmaul",
    "Shock Coil",
    "Omega Cannon"
};

// --- Fast RAM Access Helpers (Optimization) ---
// Direct MainRAM access avoids virtual function calls and bus arbitration overhead.
namespace {
    inline uint8_t FastRead8(const melonDS::u8* ram, melonDS::u32 addr) {
        return ram[addr & 0x3FFFFF];
    }
    inline uint16_t FastRead16(const melonDS::u8* ram, melonDS::u32 addr) {
        return *reinterpret_cast<const uint16_t*>(&ram[addr & 0x3FFFFF]);
    }
    inline uint32_t FastRead32(const melonDS::u8* ram, melonDS::u32 addr) {
        return *reinterpret_cast<const uint32_t*>(&ram[addr & 0x3FFFFF]);
    }
    inline void FastWrite8(melonDS::u8* ram, melonDS::u32 addr, uint8_t val) {
        ram[addr & 0x3FFFFF] = val;
    }
    inline void FastWrite16(melonDS::u8* ram, melonDS::u32 addr, uint16_t val) {
        *reinterpret_cast<uint16_t*>(&ram[addr & 0x3FFFFF]) = val;
    }
}

// --- Implementation ---

using namespace melonDS;

MelonPrimeCore::MelonPrimeCore(EmuInstance* instance)
    : emuInstance(instance),
    localCfg(instance->getLocalConfig()),
    globalCfg(instance->getGlobalConfig())
{
    memset(&addr, 0, sizeof(addr));
}

MelonPrimeCore::~MelonPrimeCore()
{
}

void MelonPrimeCore::Initialize()
{
    isJoy2KeySupport = localCfg.GetBool("Metroid.Apply.joy2KeySupport");
    isSnapTapMode = localCfg.GetBool("Metroid.Operation.SnapTap");
    isStylusMode = localCfg.GetBool("Metroid.Enable.stylusMode");

    // Initial sensitivity cache
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
        // QMainWindowのHWNDを取得
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

    const bool cur = g_rawFilter->getJoy2KeySupport();
    const bool wantInstalled = enable;

    if (cur == enable && s_isInstalled == wantInstalled) return;

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

void MelonPrimeCore::OnEmuStart()
{
    isInGame = false;
    isRomDetected = false;
    isInGameAndHasInitialized = false;
    wasInGameForRenderer = false;

    isHeadphoneApplied = false;
    isVolumeSfxApplied = false;
    isVolumeMusicApplied = false;
    isUnlockMapsHuntersApplied = false;

    UpdateRendererSettings();
}

void MelonPrimeCore::OnEmuStop()
{
    isInGame = false;
    wasInGameForRenderer = false;
}

void MelonPrimeCore::OnEmuPause()
{
    // 必要ならここにポーズ時の処理
}

void MelonPrimeCore::OnEmuUnpause()
{
    isSnapTapMode = localCfg.GetBool("Metroid.Operation.SnapTap");
    isStylusMode = localCfg.GetBool("Metroid.Enable.stylusMode");
    isJoy2KeySupport = localCfg.GetBool("Metroid.Apply.joy2KeySupport");

    ApplyJoy2KeySupportAndQtFilter(isJoy2KeySupport);

    // リセットフラグ関係
    isUnlockMapsHuntersApplied = false;
    isHeadphoneApplied = false;
    isVolumeSfxApplied = false;
    isVolumeMusicApplied = false;

    recalcAimSensitivityCache(localCfg);
    applyAimAdjustSetting(localCfg);

#ifdef _WIN32
    if (g_rawFilter) {
        BindMetroidHotkeysFromConfig(g_rawFilter, emuInstance->getInstanceID());
        g_rawFilter->resetHotkeyEdges();
    }
#endif

    // ゲーム内であれば設定を再適用
    if (isInGame) {
        UpdateRendererSettings();
        ApplyMphSensitivity(emuInstance->getNDS(), localCfg, addr.sensitivity, addr.inGameSensi, isInGameAndHasInitialized);
    }
}

void MelonPrimeCore::OnReset()
{
    OnEmuStart();
}

void MelonPrimeCore::UpdateRendererSettings()
{
    bool vsyncFlag = globalCfg.GetBool("Screen.VSync");
    emuInstance->setVSyncGL(vsyncFlag);
}

// Logic to force software renderer when not in-game (fixes menu glitches)
bool MelonPrimeCore::ShouldForceSoftwareRenderer() const
{
    if (!isRomDetected) return false;
    return !isInGame;
}

void MelonPrimeCore::DetectRomAndSetAddresses()
{
    // 1. 構造体に name を追加
    struct RomInfo {
        uint32_t checksum;
        const char* name;
        RomGroup group;
    };

    // 2. テーブルに名前文字列を追加
    const RomInfo ROM_INFO_TABLE[] = {
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
        romInfo->group,
        addr.baseChosenHunter,
        addr.inGame,
        addr.playerPos,
        addr.baseIsAltForm,
        addr.baseWeaponChange,
        addr.baseSelectedWeapon,
        addr.baseAimX,
        addr.baseAimY,
        addr.isInAdventure,
        addr.isMapOrUserActionPaused,
        addr.unlockMapsHunters,
        addr.sensitivity,
        addr.mainHunter,
        addr.baseLoadedSpecialWeapon
    );

    // Derived addresses
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

    // 3. OSDメッセージに名前を含めるように変更
    char message[256];
    // 安全のため snprintf を推奨しますが、元の sprintf でも動作します
    sprintf(message, "MPH Rom Detected: %s", romInfo->name);
    emuInstance->osdAddMessage(0, message);

    // 初期化
    recalcAimSensitivityCache(localCfg);
    applyAimAdjustSetting(localCfg);
}

void MelonPrimeCore::HandleGlobalHotkeys()
{
    // Aim Sensitivity Adjustment
    int sensitivityChange = 0;
    if (emuInstance->hotkeyReleased(HK_MetroidIngameSensiUp)) sensitivityChange = 1;
    if (emuInstance->hotkeyReleased(HK_MetroidIngameSensiDown)) sensitivityChange = -1;

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

void MelonPrimeCore::RunFrameHook()
{
    // --- RECURSION GUARD ---
    static bool isRunningHook = false;

    // Check if we are being called recursively (e.g. from Adventure Mode loop)
    if (isRunningHook) {
        // Even during recursion, we MUST process basic inputs.
        ProcessMoveInput(emuInstance->inputMask);
        emuInstance->inputMask.setBit(INPUT_B, !MP_HK_DOWN(HK_MetroidJump));

        bool isShoot = MP_HK_DOWN(HK_MetroidShootScan) || MP_HK_DOWN(HK_MetroidScanShoot);
        emuInstance->inputMask.setBit(INPUT_L, !isShoot);

        bool isZoom = MP_HK_DOWN(HK_MetroidZoom);
        emuInstance->inputMask.setBit(INPUT_R, !isZoom);

        // 再帰中も、特殊操作中（m_blockStylusAim == true）ならエイムをスキップする
        if (!m_blockStylusAim) {
            ProcessAimInput();
        }

        return;
    }

    isRunningHook = true;
    // -----------------------

    // Global Hotkeys
    HandleGlobalHotkeys();

    if (isStylusMode) {
        // isStylus who touch touch screen to aim, operate.
        isFocused = true;
    }

    // Capture previous detection state to trigger renderer update on first detect
    bool wasDetected = isRomDetected;

    if (!isRomDetected) {
        DetectRomAndSetAddresses();
    }

    // Trigger update if just detected
    if (!wasDetected && isRomDetected) {
        emuInstance->getEmuThread()->updateVideoRenderer();
    }

    if (Q_LIKELY(isRomDetected)) {
        // Optimized: Cache MainRAM pointer for this frame
        melonDS::NDS* nds = emuInstance->getNDS();
        melonDS::u8* mainRAM = nds->MainRAM;

        // Optimized: Direct read for commonly accessed variables
        isInGame = FastRead16(mainRAM, addr.inGame) == 0x0001;

        // --- Renderer Switch Logic ---
        if (isInGame != wasInGameForRenderer) {
            wasInGameForRenderer = isInGame;
            emuInstance->getEmuThread()->updateVideoRenderer();
        }
        // -----------------------------

        bool shouldBeCursorMode = !isInGame || (isInAdventure && isPaused);

        if (isInGame && !isInGameAndHasInitialized) {
            isInGameAndHasInitialized = true;

            const uint16_t incrementOfPlayerAddress = 0xF30;
            const uint8_t incrementOfAimAddr = 0x48;

            // Optimization here is less critical since it runs once per game load,
            // but we can still use FastRead where easy.
            playerPosition = FastRead8(mainRAM, addr.playerPos);

            addr.isAltForm = calculatePlayerAddress(addr.baseIsAltForm, playerPosition, incrementOfPlayerAddress);
            addr.loadedSpecialWeapon = calculatePlayerAddress(addr.baseLoadedSpecialWeapon, playerPosition, incrementOfPlayerAddress);
            addr.chosenHunter = calculatePlayerAddress(addr.baseChosenHunter, playerPosition, 0x01);
            addr.weaponChange = calculatePlayerAddress(addr.baseWeaponChange, playerPosition, incrementOfPlayerAddress);
            addr.selectedWeapon = calculatePlayerAddress(addr.baseSelectedWeapon, playerPosition, incrementOfPlayerAddress);
            addr.currentWeapon = addr.selectedWeapon - 0x1;
            addr.havingWeapons = addr.selectedWeapon + 0x3;
            addr.weaponAmmo = addr.selectedWeapon - 0x383;
            addr.jumpFlag = calculatePlayerAddress(addr.baseJumpFlag, playerPosition, incrementOfPlayerAddress);

            addr.chosenHunter = calculatePlayerAddress(addr.baseChosenHunter, playerPosition, 0x01);
            uint8_t hunterID = FastRead8(mainRAM, addr.chosenHunter);
            isSamus = (hunterID == 0x00);
            isWeavel = (hunterID == 0x06);

            addr.inGameSensi = calculatePlayerAddress(addr.baseInGameSensi, playerPosition, 0x04);
            addr.boostGauge = addr.loadedSpecialWeapon - 0x12;
            addr.isBoosting = addr.loadedSpecialWeapon - 0x10;

            addr.aimX = calculatePlayerAddress(addr.baseAimX, playerPosition, incrementOfAimAddr);
            addr.aimY = calculatePlayerAddress(addr.baseAimY, playerPosition, incrementOfAimAddr);

            isInAdventure = FastRead8(mainRAM, addr.isInAdventure) == 0x02;
        }

        if (isFocused) {
            if (Q_LIKELY(isInGame)) {
                HandleInGameLogic();
            }
            else {
                // Not In Game
                isInAdventure = false;
                isAimDisabled = true;

                if (isInGameAndHasInitialized) {
                    isInGameAndHasInitialized = false;
                }

                ApplyGameSettingsOnce();
            }

            // Cursor Mode
            if (shouldBeCursorMode != isCursorMode) {
                isCursorMode = shouldBeCursorMode;
                setAimBlock(AIMBLK_CURSOR_MODE, isCursorMode);
                if (!isStylusMode) {
                    ShowCursor(isCursorMode);
                }
            }

            if (isCursorMode) {
                if (emuInstance->isTouching)
                    nds->TouchScreen(emuInstance->touchX, emuInstance->touchY);
                else
                    nds->ReleaseScreen();
            }

            emuInstance->inputMask.setBit(INPUT_START, !MP_HK_DOWN(HK_MetroidMenu));

        }
        else {
            // Not Focused
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

void MelonPrimeCore::HandleInGameLogic()
{
    // 1. 特別なタッチ操作の判定
    m_blockStylusAim = false;

    // トランスフォーム
    if (MP_HK_PRESSED(HK_MetroidMorphBall)) {
        if (isStylusMode) m_blockStylusAim = true; // このフレームのスタイラス処理をブロック
        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();
        emuInstance->getNDS()->TouchScreen(231, 167);
        FrameAdvanceTwice();
        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();
    }

    // 武器切り替え
    if (ProcessWeaponSwitch()) {
        if (isStylusMode) m_blockStylusAim = true;
    }

    // 武器チェック（押しっぱなしの間、他のタッチをブロック）
    static bool isWeaponCheckActive = false;
    if (emuInstance->hotkeyDown(HK_MetroidWeaponCheck)) {
        if (isStylusMode) m_blockStylusAim = true; // 押している間はずっと true
        if (!isWeaponCheckActive) {
            isWeaponCheckActive = true;
            setAimBlock(AIMBLK_CHECK_WEAPON, true);
            emuInstance->getNDS()->ReleaseScreen();
            FrameAdvanceTwice();
        }
        emuInstance->getNDS()->TouchScreen(236, 30);
        // ここでは FrameAdvance を呼ばず、通常のループに任せることで
        // ProcessAimInput の上書きを m_blockStylusAim で防ぐ
    }
    else if (isWeaponCheckActive) {
        isWeaponCheckActive = false;
        emuInstance->getNDS()->ReleaseScreen();
        setAimBlock(AIMBLK_CHECK_WEAPON, false);
        FrameAdvanceTwice();
    }

    if (Q_UNLIKELY(isInAdventure)) {
        HandleAdventureMode();
        // Adventureモード内のUI操作もタッチを伴うなら考慮が必要ですが、
        // 現状は HandleAdventureMode 内の TOUCH_IF で完結しています
    }

    // 2. 基本入力
    ProcessMoveInput(emuInstance->inputMask);
    emuInstance->inputMask.setBit(INPUT_B, !MP_HK_DOWN(HK_MetroidJump));
    emuInstance->inputMask.setBit(INPUT_L, !(MP_HK_DOWN(HK_MetroidShootScan) || MP_HK_DOWN(HK_MetroidScanShoot)));
    emuInstance->inputMask.setBit(INPUT_R, !MP_HK_DOWN(HK_MetroidZoom));

    HandleMorphBallBoost();

    // 3. エイム・タッチ入力の最終決定
    // 特殊操作（変身、武器選択、武器チェック）をしていない時だけ、
    // マウスエイムやユーザーの直接タッチを処理する
    if (!m_blockStylusAim) {
        ProcessAimInput();

        if (!isStylusMode) {
            if (!wasLastFrameFocused || !isAimDisabled) {
                emuInstance->getNDS()->TouchScreen(128, 88);
            }
        }
    }
}

void MelonPrimeCore::ProcessMoveInput(QBitArray& mask)
{
    alignas(64) static constexpr uint32_t MaskLUT[16] = {
        0x0F0F0F0F, 0x0F0F0F0E, 0x0F0F0E0F, 0x0F0F0F0F,
        0x0F0E0F0F, 0x0F0E0F0E, 0x0F0E0E0F, 0x0F0E0F0F,
        0x0E0F0F0F, 0x0E0F0F0E, 0x0E0F0E0F, 0x0E0F0F0F,
        0x0F0F0F0F, 0x0F0F0F0E, 0x0F0F0E0F, 0x0F0F0F0F
    };

    static uint16_t snapState = 0;

    const uint32_t f = MP_HK_DOWN(HK_MetroidMoveForward);
    const uint32_t b = MP_HK_DOWN(HK_MetroidMoveBack);
    const uint32_t l = MP_HK_DOWN(HK_MetroidMoveLeft);
    const uint32_t r = MP_HK_DOWN(HK_MetroidMoveRight);
    const uint32_t curr = (f) | (b << 1) | (l << 2) | (r << 3);

    if (Q_LIKELY(!isSnapTapMode)) {
        const uint32_t mb = MaskLUT[curr];
        (mb & 0x00000001) ? mask.setBit(INPUT_UP) : mask.clearBit(INPUT_UP);
        ((mb >> 8) & 0x01) ? mask.setBit(INPUT_DOWN) : mask.clearBit(INPUT_DOWN);
        ((mb >> 16) & 0x01) ? mask.setBit(INPUT_LEFT) : mask.clearBit(INPUT_LEFT);
        ((mb >> 24) & 0x01) ? mask.setBit(INPUT_RIGHT) : mask.clearBit(INPUT_RIGHT);
        return;
    }

    // SnapTap
    const uint32_t last = snapState & 0xFFu;
    const uint32_t priority = snapState >> 8;
    const uint32_t newPress = curr & ~last;
    const uint32_t h3 = (curr & 0x3u);
    const uint32_t hConflict = (h3 ^ 0x3u) ? 0u : 0x3u;
    const uint32_t v12 = (curr & 0xCu);
    const uint32_t vConflict = (v12 ^ 0xCu) ? 0u : 0xCu;
    const uint32_t conflict = vConflict | hConflict;
    const uint32_t updateMask = -((newPress & conflict) != 0u);
    const uint32_t newPriority = (priority & ~(conflict & updateMask)) | (newPress & conflict & updateMask);
    const uint32_t activePriority = newPriority & curr;

    snapState = static_cast<uint16_t>((curr & 0xFFu) | ((activePriority & 0xFFu) << 8));
    const uint32_t final = (curr & ~conflict) | (activePriority & conflict);
    const uint32_t mb = MaskLUT[final];

    (mb & 0x00000001) ? mask.setBit(INPUT_UP) : mask.clearBit(INPUT_UP);
    ((mb >> 8) & 0x01) ? mask.setBit(INPUT_DOWN) : mask.clearBit(INPUT_DOWN);
    ((mb >> 16) & 0x01) ? mask.setBit(INPUT_LEFT) : mask.clearBit(INPUT_LEFT);
    ((mb >> 24) & 0x01) ? mask.setBit(INPUT_RIGHT) : mask.clearBit(INPUT_RIGHT);
}

void MelonPrimeCore::ProcessAimInput()
{
    if (isStylusMode) {
        if (Q_LIKELY(emuInstance->isTouching)) {
            emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
        }
        else {
            emuInstance->getNDS()->ReleaseScreen();
        }
        return;
    }

    if (isAimDisabled) return;

    if (isLayoutChangePending) {
        isLayoutChangePending = false;
        if (g_rawFilter) g_rawFilter->discardDeltas();
        return;
    }

    if (Q_LIKELY(wasLastFrameFocused)) {
        int deltaX = 0, deltaY = 0;
#if defined(_WIN32)
        if (g_rawFilter) g_rawFilter->fetchMouseDelta(deltaX, deltaY);
#else
        const QPoint currentPos = QCursor::pos();
        deltaX = currentPos.x() - aimData.centerX;
        deltaY = currentPos.y() - aimData.centerY;
#endif

        // Optimized: Skip calculation and write if no movement
        if ((deltaX | deltaY) == 0) return;

        float scaledX = deltaX * gAimSensiFactor;
        float scaledY = deltaY * gAimCombinedY;
        AIM_ADJUST(scaledX, scaledY);

        // Optimized: Skip write if adjustment results in zero (deadzone etc)
        if (scaledX == 0.0f && scaledY == 0.0f) return;

        // Optimized: Direct write
        melonDS::u8* mainRAM = emuInstance->getNDS()->MainRAM;
        FastWrite16(mainRAM, addr.aimX, static_cast<int16_t>(scaledX));
        FastWrite16(mainRAM, addr.aimY, static_cast<int16_t>(scaledY));

#if !defined(_WIN32)
        // Optimization: Reduce SetPos calls? 
        // For now, keep it to ensure centering, but user can add threshold here.
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

void MelonPrimeCore::HandleMorphBallBoost()
{
    if (isSamus) {
        if (MP_HK_DOWN(HK_MetroidHoldMorphBallBoost)) {
            // Optimization: Get pointer once
            melonDS::u8* mainRAM = emuInstance->getNDS()->MainRAM;

            isAltForm = FastRead8(mainRAM, addr.isAltForm) == 0x02;

            if (isAltForm) {
                uint8_t boostGaugeValue = FastRead8(mainRAM, addr.boostGauge);
                bool isBoosting = FastRead8(mainRAM, addr.isBoosting) != 0x00;
                bool isBoostGaugeEnough = boostGaugeValue > 0x0A;

                setAimBlock(AIMBLK_MORPHBALL_BOOST, true);

                if (!emuInstance->hotkeyDown(HK_MetroidWeaponCheck)) {
                    emuInstance->getNDS()->ReleaseScreen();
                }

                // Restore Original Logic: Auto-release R when gauge is full
                // 1 (True) = Released -> Boosts
                // 0 (False) = Pressed -> Charges
                emuInstance->inputMask.setBit(INPUT_R, (!isBoosting && isBoostGaugeEnough));

                if (isBoosting) {
                    setAimBlock(AIMBLK_MORPHBALL_BOOST, false);
                    if (isStylusMode) {
                        if (emuInstance->isTouching) {
                            emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
                        }
                    }
                }
            }
        }
        else {
            setAimBlock(AIMBLK_MORPHBALL_BOOST, false);
        }
    }
}

void MelonPrimeCore::HandleAdventureMode()
{
    melonDS::u8* mainRAM = emuInstance->getNDS()->MainRAM;
    isPaused = FastRead8(mainRAM, addr.isMapOrUserActionPaused) == 0x1;

    if (MP_HK_PRESSED(HK_MetroidScanVisor)) {
        // スタイラスの干渉をブロック開始
        if (isStylusMode) m_blockStylusAim = true;

        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();
        emuInstance->getNDS()->TouchScreen(128, 173); // バイザーアイコンをタッチ

        if (FastRead8(mainRAM, addr.isInVisorOrMap) == 0x1) {
            FrameAdvanceTwice();
        }
        else {
            // バイザー起動待ちループ
            for (int i = 0; i < 30; i++) {
                // ここで ProcessAimInput が呼ばれますが、
                // m_blockStylusAim が true なので、再帰ガード内で
                // スタイラスの「離す」処理がスキップされます
                if (!isStylusMode) {
                    ProcessAimInput();
                }
                ProcessMoveInput(emuInstance->inputMask);
                emuInstance->getNDS()->SetKeyMask(GET_INPUT_MASK(emuInstance->inputMask));
                FrameAdvanceOnce();
            }
        }
        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();

        // 操作完了
        m_blockStylusAim = false;
    }

    // UI操作系
    TOUCH_IF(HK_MetroidUIOk, 128, 142)
        TOUCH_IF(HK_MetroidUILeft, 71, 141)
        TOUCH_IF(HK_MetroidUIRight, 185, 141)
        TOUCH_IF(HK_MetroidUIYes, 96, 142)
        TOUCH_IF(HK_MetroidUINo, 160, 142)
}

bool MelonPrimeCore::ProcessWeaponSwitch()
{
    static constexpr uint8_t  WEAPON_ORDER[] = { 0, 2, 7, 6, 5, 4, 3, 1, 8 };
    static constexpr uint16_t WEAPON_MASKS[] = { 0x001, 0x004, 0x080, 0x040, 0x020, 0x010, 0x008, 0x002, 0x100 };
    static constexpr uint8_t  MIN_AMMO[] = { 0, 0x5, 0xA, 0x4, 0x14, 0x5, 0xA, 0xA, 0 };
    static constexpr uint8_t  WEAPON_INDEX_MAP[] = { 0, 7, 1, 6, 5, 4, 3, 2, 8 };
    static constexpr uint8_t  WEAPON_COUNT = 9;

    static constexpr struct {
        int hotkey;
        uint8_t weapon;
    } HOTKEY_MAP[] = {
        { HK_MetroidWeaponBeam,    0 },
        { HK_MetroidWeaponMissile, 2 },
        { HK_MetroidWeapon1,       7 },
        { HK_MetroidWeapon2,       6 },
        { HK_MetroidWeapon3,       5 },
        { HK_MetroidWeapon4,       4 },
        { HK_MetroidWeapon5,       3 },
        { HK_MetroidWeapon6,       1 },
        { HK_MetroidWeaponSpecial, 0xFF }
    };

    uint32_t hot = 0;
    for (size_t i = 0; i < 9; ++i)
        if (MP_HK_PRESSED(HOTKEY_MAP[i].hotkey)) hot |= (1u << i);

    // Optimization: Get MainRAM once
    melonDS::u8* mainRAM = emuInstance->getNDS()->MainRAM;

    if (hot) {
        const int firstSet = __builtin_ctz(hot);
        if (Q_UNLIKELY(firstSet == 8)) {
            const uint8_t loaded = FastRead8(mainRAM, addr.loadedSpecialWeapon);
            if (loaded == 0xFF) {
                emuInstance->osdAddMessage(0, "Have not Special Weapon yet!");
                return false;
            }
            SwitchWeapon(loaded);
            return true;
        }

        const uint8_t weaponID = HOTKEY_MAP[firstSet].weapon;
        const uint16_t having = FastRead16(mainRAM, addr.havingWeapons);
        const uint32_t ammoData = FastRead32(mainRAM, addr.weaponAmmo);
        const uint16_t missileAmmo = (uint16_t)(ammoData >> 16);
        const uint16_t weaponAmmo = (uint16_t)(ammoData & 0xFFFF);

        bool owned = (weaponID == 0 || weaponID == 2) ? true : ((having & WEAPON_MASKS[WEAPON_INDEX_MAP[weaponID]]) != 0);
        if (!owned) {
            emuInstance->osdAddMessage(0, "Have not %s yet!", kWeaponNames[weaponID]);
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
            return false;
        }
        SwitchWeapon(weaponID);
        return true;
    }

    // Wheel / Next / Prev
    auto* panel = emuInstance->getMainWindow()->panel;
    if (!panel) return false;
    const int wheelDelta = panel->getDelta();
    const bool nextKey = MP_HK_PRESSED(HK_MetroidWeaponNext);
    const bool prevKey = MP_HK_PRESSED(HK_MetroidWeaponPrevious);
    if (!wheelDelta && !nextKey && !prevKey) return false;

    const bool forward = (wheelDelta < 0) || nextKey;
    const uint8_t curID = FastRead8(mainRAM, addr.currentWeapon);

    const uint16_t having = FastRead16(mainRAM, addr.havingWeapons);
    const uint32_t ammoData = FastRead32(mainRAM, addr.weaponAmmo);
    const uint16_t missileAmmo = (uint16_t)(ammoData >> 16);
    const uint16_t weaponAmmo = (uint16_t)(ammoData & 0xFFFF);

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
    if (!available) return false;

    uint8_t idx = WEAPON_INDEX_MAP[curID];
    for (int n = 0; n < WEAPON_COUNT; ++n) {
        idx = forward ? (uint8_t)((idx + 1) % WEAPON_COUNT)
            : (uint8_t)((idx + WEAPON_COUNT - 1) % WEAPON_COUNT);
        if (available & (1u << idx)) {
            SwitchWeapon(WEAPON_ORDER[idx]);
            return true;
        }
    }
    return false;
}

void MelonPrimeCore::SwitchWeapon(int weaponIndex)
{
    melonDS::u8* mainRAM = emuInstance->getNDS()->MainRAM;
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
    else {
        if (emuInstance->isTouching) {
            emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
        }
    }
    FrameAdvanceTwice();

    if (isRestoreNeeded) {
        currentJumpFlags = FastRead8(mainRAM, addr.jumpFlag);
        FastWrite8(mainRAM, addr.jumpFlag, (currentJumpFlags & 0xF0) | jumpFlag);
    }
}

void MelonPrimeCore::ApplyGameSettingsOnce()
{
    // L / R for Hunter License
    emuInstance->inputMask.setBit(INPUT_L, !MP_HK_PRESSED(HK_MetroidUILeft));
    emuInstance->inputMask.setBit(INPUT_R, !MP_HK_PRESSED(HK_MetroidUIRight));

    // Headphone
    ApplyHeadphoneOnce(emuInstance->getNDS(), localCfg, addr.operationAndSound, isHeadphoneApplied);

    // MPH Sensitivity
    ApplyMphSensitivity(emuInstance->getNDS(), localCfg, addr.sensitivity, addr.inGameSensi, isInGameAndHasInitialized);

    // Unlock
    ApplyUnlockHuntersMaps(emuInstance->getNDS(), localCfg, isUnlockMapsHuntersApplied,
        addr.unlockMapsHunters, addr.unlockMapsHunters2, addr.unlockMapsHunters3,
        addr.unlockMapsHunters4, addr.unlockMapsHunters5);

    // DS Name
    useDsName(emuInstance->getNDS(), localCfg, addr.dsNameFlagAndMicVolume);

    // License
    applySelectedHunterStrict(emuInstance->getNDS(), localCfg, addr.mainHunter);
    applyLicenseColorStrict(emuInstance->getNDS(), localCfg, addr.rankColor);

    // Volumes
    ApplySfxVolumeOnce(emuInstance->getNDS(), localCfg, addr.volSfx8Bit, isVolumeSfxApplied);
    ApplyMusicVolumeOnce(emuInstance->getNDS(), localCfg, addr.volMusic8Bit, isVolumeMusicApplied);
}

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
    if (m_frameAdvanceFunc) {
        m_frameAdvanceFunc();
    }
    else {
        emuInstance->inputProcess();
        if (emuInstance->usesOpenGL()) emuInstance->makeCurrentGL();

        if (emuInstance->getNDS()->GPU.GetRenderer3D().NeedsShaderCompile()) {
            int currentShader, shadersCount;
            emuInstance->getNDS()->GPU.GetRenderer3D().ShaderCompileStep(currentShader, shadersCount);
        }
        else {
            emuInstance->getNDS()->RunFrame();
        }

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

// --- Static Helpers (Ported) ---
// Note: These rarely called helpers use standard read/write for safety and simplicity,
// or can be optimized if strictly needed. Since they run once per setting change, they are fine.

bool MelonPrimeCore::ApplyHeadphoneOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, bool& applied)
{
    if (!nds || applied) return false;
    if (!localCfg.GetBool("Metroid.Apply.Headphone")) return false;

    std::uint8_t oldVal = nds->ARM9Read8(addr);
    constexpr std::uint8_t kAudioFieldMask = 0x18;
    if ((oldVal & kAudioFieldMask) == kAudioFieldMask) {
        applied = true;
        return false;
    }
    std::uint8_t newVal = static_cast<std::uint8_t>(oldVal | kAudioFieldMask);
    if (newVal == oldVal) {
        applied = true;
        return false;
    }
    nds->ARM9Write8(addr, newVal);
    applied = true;
    return true;
}

bool MelonPrimeCore::applyLicenseColorStrict(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr)
{
    if (!nds) return false;
    if (!localCfg.GetBool("Metroid.HunterLicense.Color.Apply")) return false;

    int sel = localCfg.GetInt("Metroid.HunterLicense.Color.Selected");
    if (sel < 0 || sel > 2) return false;

    constexpr std::uint8_t kColorBitsLUT[3] = { 0x00, 0x40, 0x80 };
    constexpr std::uint8_t KEEP_MASK = 0x3F;
    const std::uint8_t desiredColorBits = kColorBitsLUT[sel];
    const std::uint8_t oldVal = nds->ARM9Read8(addr);
    const std::uint8_t newVal = static_cast<std::uint8_t>((oldVal & KEEP_MASK) | desiredColorBits);

    if (newVal == oldVal) return false;
    nds->ARM9Write8(addr, newVal);
    return true;
}

bool MelonPrimeCore::applySelectedHunterStrict(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr)
{
    if (!nds) return false;
    if (!localCfg.GetBool("Metroid.HunterLicense.Hunter.Apply")) return false;

    constexpr std::uint8_t HUNTER_MASK = 0x78;
    constexpr std::uint8_t kHunterBitsLUT[7] = { 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30 };
    int sel = localCfg.GetInt("Metroid.HunterLicense.Hunter.Selected");
    if (sel < 0) sel = 0;
    if (sel > 6) sel = 6;

    const std::uint8_t desiredHunterBits = static_cast<std::uint8_t>(kHunterBitsLUT[sel] & HUNTER_MASK);
    const std::uint8_t oldVal = nds->ARM9Read8(addr);
    const std::uint8_t newVal = static_cast<std::uint8_t>((oldVal & ~HUNTER_MASK) | desiredHunterBits);

    if (newVal == oldVal) return false;
    nds->ARM9Write8(addr, newVal);
    return true;
}

bool MelonPrimeCore::useDsName(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr)
{
    if (!nds) return false;
    if (!localCfg.GetBool("Metroid.Use.Firmware.Name")) return false;

    std::uint8_t oldVal = nds->ARM9Read8(addr);
    constexpr std::uint8_t kFlagMask = 0x01;
    std::uint8_t newVal = static_cast<std::uint8_t>(oldVal & ~kFlagMask);

    if (newVal == oldVal) return false;
    nds->ARM9Write8(addr, newVal);
    return true;
}

bool MelonPrimeCore::ApplySfxVolumeOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, bool& applied)
{
    if (!nds || applied) return false;
    if (!localCfg.GetBool("Metroid.Apply.SfxVolume")) return false;

    std::uint8_t oldVal = nds->ARM9Read8(addr);
    int cfgSteps = localCfg.GetInt("Metroid.Volume.SFX");
    std::uint8_t steps = static_cast<std::uint8_t>(std::clamp(cfgSteps, 0, 9));
    std::uint8_t newVal = static_cast<std::uint8_t>((oldVal & 0xC0) | ((steps & 0x0F) << 2) | 0x03);

    if (newVal == oldVal) {
        applied = true;
        return false;
    }
    nds->ARM9Write8(addr, newVal);
    applied = true;
    return true;
}

bool MelonPrimeCore::ApplyMusicVolumeOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, bool& applied)
{
    if (!nds || applied) return false;
    if (!localCfg.GetBool("Metroid.Apply.MusicVolume")) return false;

    std::uint8_t oldVal = nds->ARM9Read8(addr);
    int cfgSteps = localCfg.GetInt("Metroid.Volume.Music");
    std::uint8_t steps = static_cast<std::uint8_t>(std::clamp(cfgSteps, 0, 9));
    std::uint8_t newField = static_cast<std::uint8_t>((steps & 0x0F) << 2);
    std::uint8_t newVal = static_cast<std::uint8_t>((oldVal & static_cast<std::uint8_t>(~0x3C)) | newField);

    if (newVal == oldVal) {
        applied = true;
        return false;
    }
    nds->ARM9Write8(addr, newVal);
    applied = true;
    return true;
}

void MelonPrimeCore::ApplyMphSensitivity(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addrSensi, melonDS::u32 addrInGame, bool inGameInit)
{
    double mphSensitivity = localCfg.GetDouble("Metroid.Sensitivity.Mph");
    std::uint32_t sensiVal = sensiNumToSensiVal(mphSensitivity);
    nds->ARM9Write16(addrSensi, static_cast<std::uint16_t>(sensiVal));

    if (inGameInit) {
        nds->ARM9Write16(addrInGame, static_cast<std::uint16_t>(sensiVal));
    }
}

bool MelonPrimeCore::ApplyUnlockHuntersMaps(melonDS::NDS* nds, Config::Table& localCfg, bool& applied, melonDS::u32 a1, melonDS::u32 a2, melonDS::u32 a3, melonDS::u32 a4, melonDS::u32 a5)
{
    if (applied) return false;
    if (!localCfg.GetBool("Metroid.Data.Unlock")) return false;

    std::uint8_t cur = nds->ARM9Read8(a1);
    std::uint8_t newVal = static_cast<std::uint8_t>(cur | 0x03);
    nds->ARM9Write8(a1, newVal);
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