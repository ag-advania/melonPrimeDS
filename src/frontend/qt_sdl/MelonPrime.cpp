/*
    MelonPrimeDS Logic Implementation (OPTIMIZED VERSION)
    Key Optimizations:
    1. Cache-line aligned hot data structures
    2. Branchless operations where possible
    3. Batched RAM reads with prefetching
    4. LUT-based movement processing
    5. Reduced function call overhead
    6. SIMD-friendly data layout
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
// Fast Input Mask (Global for fastest access)
// ============================================================
static uint16_t gInputMaskFast = 0xFFFF;

// Branchless input operations
#define INPUT_PRESS(bit)   (gInputMaskFast &= ~(1u << (bit)))
#define INPUT_RELEASE(bit) (gInputMaskFast |= (1u << (bit)))
#define INPUT_RESET()      (gInputMaskFast = 0xFFFF)

// Branchless conditional set
FORCE_INLINE void InputSetBranchless(uint16_t bit, bool released) {
    const uint16_t mask = 1u << bit;
    gInputMaskFast = (gInputMaskFast & ~mask) | (released ? mask : 0);
}

// ============================================================
// Hotkey Macros
// ============================================================
#if defined(_WIN32)
#define MP_JOY_DOWN(id)      (emuInstance->joyHotkeyMask.testBit((id)))
#define MP_JOY_PRESSED(id)   (emuInstance->joyHotkeyPress.testBit((id)))
#define MP_JOY_RELEASED(id)  (emuInstance->joyHotkeyRelease.testBit((id)))
#define MP_HK_DOWN_RAW(id)   ((g_rawFilter && g_rawFilter->hotkeyDown((id))) || MP_JOY_DOWN((id)))
#define MP_HK_PRESSED_RAW(id) ((g_rawFilter && g_rawFilter->hotkeyPressed((id))) || MP_JOY_PRESSED((id)))
#else
#define MP_HK_DOWN_RAW(id)    (emuInstance->hotkeyMask.testBit((id)))
#define MP_HK_PRESSED_RAW(id) (emuInstance->hotkeyPress.testBit((id)))
#endif

// ============================================================
// Aim Block System (Branchless)
// ============================================================
static uint32_t gAimBlockBits = 0;
static uint32_t isAimDisabled = 0;

enum : uint32_t {
    AIMBLK_CHECK_WEAPON = 1u << 0,
    AIMBLK_MORPHBALL_BOOST = 1u << 1,
    AIMBLK_CURSOR_MODE = 1u << 2,
};

FORCE_INLINE void setAimBlockBranchless(uint32_t bitMask, bool enable) noexcept {
    const uint32_t enableMask = enable ? bitMask : 0;
    const uint32_t clearMask = enable ? 0 : bitMask;
    gAimBlockBits = (gAimBlockBits | enableMask) & ~clearMask;
    isAimDisabled = gAimBlockBits;
}

// ============================================================
// Sensitivity Cache (Pre-computed)
// ============================================================
static float gAimSensiFactor = 0.01f;
static float gAimCombinedY = 0.013333333f;
static float gAimAdjust = 0.5f;

FORCE_INLINE void recalcAimSensitivityCache(Config::Table& localCfg) {
    const int sens = localCfg.GetInt("Metroid.Sensitivity.Aim");
    const float aimYAxisScale = static_cast<float>(localCfg.GetDouble("Metroid.Sensitivity.AimYAxisScale"));
    gAimSensiFactor = sens * 0.01f;
    gAimCombinedY = gAimSensiFactor * aimYAxisScale;
}

FORCE_INLINE void applyAimAdjustSetting(Config::Table& cfg) noexcept {
    double v = cfg.GetDouble("Metroid.Aim.Adjust");
    gAimAdjust = static_cast<float>(std::max(0.0, std::isnan(v) ? 0.0 : v));
}

// Branchless aim adjustment
FORCE_INLINE void applyAimAdjustBranchless(float& dx, float& dy) noexcept {
    const float a = gAimAdjust;
    if (UNLIKELY(a <= 0.0f)) return;

    const float avx = std::fabs(dx);
    const float avy = std::fabs(dy);

    // Branchless: (avx < a) ? 0 : ((avx < 1) ? sign(dx) : dx)
    const float signX = (dx >= 0.0f) ? 1.0f : -1.0f;
    const float signY = (dy >= 0.0f) ? 1.0f : -1.0f;

    dx = (avx < a) ? 0.0f : ((avx < 1.0f) ? signX : dx);
    dy = (avy < a) ? 0.0f : ((avy < 1.0f) ? signY : dy);
}

FORCE_INLINE std::uint16_t sensiNumToSensiVal(double sensiNum) {
    constexpr std::uint32_t BASE_VAL = 0x0999;
    constexpr std::uint32_t STEP_VAL = 0x0199;
    double val = static_cast<double>(BASE_VAL) + (sensiNum - 1.0) * static_cast<double>(STEP_VAL);
    return static_cast<std::uint16_t>(std::min(static_cast<uint32_t>(std::llround(val)), 0xFFFFu));
}

static const char* kWeaponNames[] = {
    "Power Beam", "Volt Driver", "Missile Launcher", "Battlehammer",
    "Imperialist", "Judicator", "Magmaul", "Shock Coil", "Omega Cannon"
};

// ============================================================
// Fast RAM Access (Optimized with Prefetch)
// ============================================================
namespace {
    constexpr uint32_t RAM_MASK = 0x3FFFFF;

    FORCE_INLINE uint8_t FastRead8(const melonDS::u8* ram, melonDS::u32 addr) {
        return ram[addr & RAM_MASK];
    }

    FORCE_INLINE uint16_t FastRead16(const melonDS::u8* ram, melonDS::u32 addr) {
        return *reinterpret_cast<const uint16_t*>(&ram[addr & RAM_MASK]);
    }

    FORCE_INLINE uint32_t FastRead32(const melonDS::u8* ram, melonDS::u32 addr) {
        return *reinterpret_cast<const uint32_t*>(&ram[addr & RAM_MASK]);
    }

    FORCE_INLINE void FastWrite8(melonDS::u8* ram, melonDS::u32 addr, uint8_t val) {
        ram[addr & RAM_MASK] = val;
    }

    FORCE_INLINE void FastWrite16(melonDS::u8* ram, melonDS::u32 addr, uint16_t val) {
        *reinterpret_cast<uint16_t*>(&ram[addr & RAM_MASK]) = val;
    }

    // Batched read for multiple addresses (cache-friendly)
    struct BatchedGameState {
        uint8_t isAltForm;
        uint8_t jumpFlag;
        uint8_t currentWeapon;
        uint8_t selectedWeapon;
        uint8_t boostGauge;
        uint8_t isBoosting;
        uint8_t isInVisorOrMap;
        uint8_t isPaused;
        uint16_t inGame;
        uint16_t havingWeapons;
        uint32_t weaponAmmo;
    };

    FORCE_INLINE BatchedGameState BatchReadGameState(
        const melonDS::u8* ram,
        const GameAddressesHot& addr)
    {
        // Prefetch next cache line
        PREFETCH_READ(&ram[(addr.weaponAmmo) & RAM_MASK]);

        BatchedGameState state;
        state.isAltForm = FastRead8(ram, addr.isAltForm);
        state.jumpFlag = FastRead8(ram, addr.jumpFlag);
        state.currentWeapon = FastRead8(ram, addr.currentWeapon);
        state.selectedWeapon = FastRead8(ram, addr.selectedWeapon);
        state.boostGauge = FastRead8(ram, addr.boostGauge);
        state.isBoosting = FastRead8(ram, addr.isBoosting);
        state.isInVisorOrMap = FastRead8(ram, addr.isInVisorOrMap);
        state.isPaused = FastRead8(ram, addr.isMapOrUserActionPaused);
        state.inGame = FastRead16(ram, addr.inGame);
        state.havingWeapons = FastRead16(ram, addr.havingWeapons);
        state.weaponAmmo = FastRead32(ram, addr.weaponAmmo);
        return state;
    }

    // Movement LUT: 4 bits -> D-pad pattern (UP/DOWN/LEFT/RIGHT in bits 4-7)
    alignas(64) static constexpr uint8_t MoveLUT[16] = {
        0xF0, 0xB0, 0x70, 0xF0,  // 0000, 0001(F), 0010(B), 0011(F+B)
        0xD0, 0x90, 0x50, 0xD0,  // 0100(L), 0101(F+L), 0110(B+L), 0111(F+B+L)
        0xE0, 0xA0, 0x60, 0xE0,  // 1000(R), 1001(F+R), 1010(B+R), 1011(F+B+R)
        0xF0, 0xB0, 0x70, 0xF0,  // 1100(L+R), ...
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
    memset(&m_addrHot, 0, sizeof(m_addrHot));
    memset(&m_addrCold, 0, sizeof(m_addrCold));
    m_flags.packed = 0;
}

MelonPrimeCore::~MelonPrimeCore() {}

// ============================================================
// Initialization
// ============================================================

void MelonPrimeCore::Initialize()
{
    m_flags.assign(StateFlags::BIT_JOY2KEY, localCfg.GetBool("Metroid.Apply.joy2KeySupport"));
    m_flags.assign(StateFlags::BIT_SNAP_TAP, localCfg.GetBool("Metroid.Operation.SnapTap"));
    m_flags.assign(StateFlags::BIT_STYLUS_MODE, localCfg.GetBool("Metroid.Enable.stylusMode"));
    isStylusMode = m_flags.test(StateFlags::BIT_STYLUS_MODE);

    recalcAimSensitivityCache(localCfg);
    applyAimAdjustSetting(localCfg);

#ifdef _WIN32
    SetupRawInput();
#endif
}

void MelonPrimeCore::SetFrameAdvanceFunc(std::function<void()> func)
{
    m_frameAdvanceFunc = std::move(func);
}

void MelonPrimeCore::SetupRawInput()
{
#ifdef _WIN32
    if (!g_rawFilter) {
        HWND hwnd = nullptr;
        if (auto* mw = emuInstance->getMainWindow()) {
            hwnd = reinterpret_cast<HWND>(mw->winId());
        }
        g_rawFilter = new RawInputWinFilter(m_flags.test(StateFlags::BIT_JOY2KEY), hwnd);
        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));
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

    m_flags.assign(StateFlags::BIT_JOY2KEY, enable);
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
// Input Cache Implementation (OPTIMIZED)
// ============================================================

HOT_FUNCTION void MelonPrimeCore::UpdateInputState()
{
    // Reset to zero (single write)
    m_input.down = 0;
    m_input.press = 0;

    // Build down state with bitwise OR (no branches in hot path)
    uint64_t down = 0;
    uint64_t press = 0;

    // Movement - batch together
    down |= MP_HK_DOWN_RAW(HK_MetroidMoveForward) ? IB_MOVE_F : 0;
    down |= MP_HK_DOWN_RAW(HK_MetroidMoveBack) ? IB_MOVE_B : 0;
    down |= MP_HK_DOWN_RAW(HK_MetroidMoveLeft) ? IB_MOVE_L : 0;
    down |= MP_HK_DOWN_RAW(HK_MetroidMoveRight) ? IB_MOVE_R : 0;

    // Actions
    down |= MP_HK_DOWN_RAW(HK_MetroidJump) ? IB_JUMP : 0;
    down |= MP_HK_DOWN_RAW(HK_MetroidZoom) ? IB_ZOOM : 0;
    down |= (MP_HK_DOWN_RAW(HK_MetroidShootScan) || MP_HK_DOWN_RAW(HK_MetroidScanShoot)) ? IB_SHOOT : 0;
    down |= MP_HK_DOWN_RAW(HK_MetroidWeaponCheck) ? IB_WEAPON_CHECK : 0;
    down |= MP_HK_DOWN_RAW(HK_MetroidHoldMorphBallBoost) ? IB_MORPH_BOOST : 0;
    down |= MP_HK_DOWN_RAW(HK_MetroidMenu) ? IB_MENU : 0;

    // Pressed (Edges)
    press |= MP_HK_PRESSED_RAW(HK_MetroidMorphBall) ? IB_MORPH : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidScanVisor) ? IB_SCAN_VISOR : 0;

    // UI
    press |= MP_HK_PRESSED_RAW(HK_MetroidUIOk) ? IB_UI_OK : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidUILeft) ? IB_UI_LEFT : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidUIRight) ? IB_UI_RIGHT : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidUIYes) ? IB_UI_YES : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidUINo) ? IB_UI_NO : 0;

    // Weapons - can be batched with single mask check later
    press |= MP_HK_PRESSED_RAW(HK_MetroidWeaponBeam) ? IB_WEAPON_BEAM : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidWeaponMissile) ? IB_WEAPON_MISSILE : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidWeapon1) ? IB_WEAPON_1 : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidWeapon2) ? IB_WEAPON_2 : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidWeapon3) ? IB_WEAPON_3 : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidWeapon4) ? IB_WEAPON_4 : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidWeapon5) ? IB_WEAPON_5 : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidWeapon6) ? IB_WEAPON_6 : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidWeaponSpecial) ? IB_WEAPON_SPECIAL : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidWeaponNext) ? IB_WEAPON_NEXT : 0;
    press |= MP_HK_PRESSED_RAW(HK_MetroidWeaponPrevious) ? IB_WEAPON_PREV : 0;

    // Store results
    m_input.down = down;
    m_input.press = press;

    // Pre-compute movement index for LUT
    m_input.moveIndex = static_cast<uint32_t>((down >> 6) & 0xF);

    // Mouse Delta
#if defined(_WIN32)
    if (g_rawFilter) {
        g_rawFilter->fetchMouseDelta(m_input.mouseX, m_input.mouseY);
    }
    else {
        m_input.mouseX = 0;
        m_input.mouseY = 0;
    }
#else
    const QPoint currentPos = QCursor::pos();
    m_input.mouseX = currentPos.x() - m_aimData.centerX;
    m_input.mouseY = currentPos.y() - m_aimData.centerY;
#endif
}

// ============================================================
// Lifecycle Callbacks
// ============================================================

void MelonPrimeCore::OnEmuStart()
{
    m_flags.packed = StateFlags::BIT_LAYOUT_PENDING; // Reset all, set layout pending
    m_isInGame = false;
    m_appliedFlags = 0;

    // ▼▼▼ 追加: 設定を再読み込みしてフラグと変数を同期させる ▼▼▼
    m_flags.assign(StateFlags::BIT_SNAP_TAP, localCfg.GetBool("Metroid.Operation.SnapTap"));
    m_flags.assign(StateFlags::BIT_STYLUS_MODE, localCfg.GetBool("Metroid.Enable.stylusMode"));
    m_flags.assign(StateFlags::BIT_JOY2KEY, localCfg.GetBool("Metroid.Apply.joy2KeySupport"));

    isStylusMode = m_flags.test(StateFlags::BIT_STYLUS_MODE);

    // Joy2Keyフィルタの適用もここで行うのが安全
    ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));
    // ▲▲▲ 追加ここまで ▲▲▲

    INPUT_RESET();
    UpdateRendererSettings();
}

void MelonPrimeCore::OnEmuStop()
{
    m_flags.clear(StateFlags::BIT_IN_GAME | StateFlags::BIT_WAS_IN_GAME_RENDERER);
    m_isInGame = false;
}

void MelonPrimeCore::OnEmuPause() {}

void MelonPrimeCore::OnEmuUnpause()
{
    m_flags.assign(StateFlags::BIT_SNAP_TAP, localCfg.GetBool("Metroid.Operation.SnapTap"));
    m_flags.assign(StateFlags::BIT_STYLUS_MODE, localCfg.GetBool("Metroid.Enable.stylusMode"));
    m_flags.assign(StateFlags::BIT_JOY2KEY, localCfg.GetBool("Metroid.Apply.joy2KeySupport"));
    isStylusMode = m_flags.test(StateFlags::BIT_STYLUS_MODE);

    ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));

    m_appliedFlags &= ~(APPLIED_UNLOCK | APPLIED_HEADPHONE | APPLIED_VOL_SFX | APPLIED_VOL_MUSIC);
    m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);

    recalcAimSensitivityCache(localCfg);
    applyAimAdjustSetting(localCfg);

#ifdef _WIN32
    if (g_rawFilter) {
        BindMetroidHotkeysFromConfig(g_rawFilter, emuInstance->getInstanceID());
        g_rawFilter->resetHotkeyEdges();
    }
#endif

    if (m_flags.test(StateFlags::BIT_IN_GAME)) {
        UpdateRendererSettings();
        ApplyMphSensitivity(emuInstance->getNDS(), localCfg, m_addrCold.sensitivity,
            m_addrCold.inGameSensi, m_flags.test(StateFlags::BIT_IN_GAME_INIT));
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
    return m_flags.test(StateFlags::BIT_ROM_DETECTED) && !m_flags.test(StateFlags::BIT_IN_GAME);
}

// ============================================================
// ROM Detection (Cold Path)
// ============================================================

COLD_FUNCTION void MelonPrimeCore::DetectRomAndSetAddresses()
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
        romInfo->group, m_addrCold.baseChosenHunter, m_addrHot.inGame, m_addrCold.playerPos,
        m_addrCold.baseIsAltForm, m_addrCold.baseWeaponChange, m_addrCold.baseSelectedWeapon,
        m_addrCold.baseAimX, m_addrCold.baseAimY, m_addrCold.isInAdventure, m_addrHot.isMapOrUserActionPaused,
        m_addrCold.unlockMapsHunters, m_addrCold.sensitivity, m_addrCold.mainHunter, m_addrCold.baseLoadedSpecialWeapon
    );

    // Compute derived addresses
    m_addrHot.isInVisorOrMap = m_addrCold.playerPos - 0xABB;
    m_addrCold.baseJumpFlag = m_addrCold.baseSelectedWeapon - 0xA;
    m_addrCold.volSfx8Bit = m_addrCold.unlockMapsHunters;
    m_addrCold.volMusic8Bit = m_addrCold.volSfx8Bit + 0x1;
    m_addrCold.operationAndSound = m_addrCold.unlockMapsHunters - 0x1;
    m_addrCold.unlockMapsHunters2 = m_addrCold.unlockMapsHunters + 0x3;
    m_addrCold.unlockMapsHunters3 = m_addrCold.unlockMapsHunters + 0x7;
    m_addrCold.unlockMapsHunters4 = m_addrCold.unlockMapsHunters + 0xB;
    m_addrCold.unlockMapsHunters5 = m_addrCold.unlockMapsHunters + 0xF;
    m_addrCold.dsNameFlagAndMicVolume = m_addrCold.unlockMapsHunters5 + 0x1;
    m_addrCold.baseInGameSensi = m_addrCold.sensitivity + 0xB4;
    m_addrCold.rankColor = m_addrCold.mainHunter + 0x3;

    m_flags.set(StateFlags::BIT_ROM_DETECTED);

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
    int sensitivityChange = 0;
    if (emuInstance->hotkeyReleased(HK_MetroidIngameSensiUp)) sensitivityChange = 1;
    else if (emuInstance->hotkeyReleased(HK_MetroidIngameSensiDown)) sensitivityChange = -1;

    if (UNLIKELY(sensitivityChange != 0)) {
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
// Main Frame Hook (OPTIMIZED)
// ============================================================

HOT_FUNCTION void MelonPrimeCore::RunFrameHook()
{
    static bool isRunningHook = false;

    melonDS::u8* mainRAM = emuInstance->getNDS()->MainRAM;

    if (UNLIKELY(isRunningHook)) {
        // Recursive call - minimal processing
        ProcessMoveInputFast();
        InputSetBranchless(INPUT_B, !IsDown(IB_JUMP));

        if (isStylusMode) {
            if (emuInstance->isTouching && !m_flags.test(StateFlags::BIT_BLOCK_STYLUS)) {
                emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
            }
        }
        else {
            ProcessAimInputMouse(mainRAM);
        }

        InputSetBranchless(INPUT_L, !IsDown(IB_SHOOT));
        InputSetBranchless(INPUT_R, !IsDown(IB_ZOOM));
        return;
    }

    isRunningHook = true;

    // Snapshot input ONCE per frame
    UpdateInputState();

    INPUT_RESET();
    m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);

    HandleGlobalHotkeys();

    if (isStylusMode) {
        isFocused = true;
    }

    const bool wasDetected = m_flags.test(StateFlags::BIT_ROM_DETECTED);
    if (UNLIKELY(!wasDetected)) {
        DetectRomAndSetAddresses();
    }
    if (UNLIKELY(!wasDetected && m_flags.test(StateFlags::BIT_ROM_DETECTED))) {
        emuInstance->getEmuThread()->updateVideoRenderer();
    }

    if (LIKELY(m_flags.test(StateFlags::BIT_ROM_DETECTED))) {
        const bool isInGame = FastRead16(mainRAM, m_addrHot.inGame) == 0x0001;
        m_flags.assign(StateFlags::BIT_IN_GAME, isInGame);
        m_isInGame = isInGame;

        const bool wasInGameRenderer = m_flags.test(StateFlags::BIT_WAS_IN_GAME_RENDERER);
        if (UNLIKELY(isInGame != wasInGameRenderer)) {
            m_flags.assign(StateFlags::BIT_WAS_IN_GAME_RENDERER, isInGame);
            emuInstance->getEmuThread()->updateVideoRenderer();
        }

        const bool isInAdventure = m_flags.test(StateFlags::BIT_IN_ADVENTURE);
        const bool isPaused = m_flags.test(StateFlags::BIT_PAUSED);
        const bool shouldBeCursorMode = !isInGame || (isInAdventure && isPaused);

        if (isInGame && !m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
            m_flags.set(StateFlags::BIT_IN_GAME_INIT);
            constexpr uint16_t kPlayerAddrInc = 0xF30;
            constexpr uint8_t kAimAddrInc = 0x48;

            m_playerPosition = FastRead8(mainRAM, m_addrCold.playerPos);

            // Compute player-specific addresses
            m_addrHot.isAltForm = calculatePlayerAddress(m_addrCold.baseIsAltForm, m_playerPosition, kPlayerAddrInc);
            m_addrHot.loadedSpecialWeapon = calculatePlayerAddress(m_addrCold.baseLoadedSpecialWeapon, m_playerPosition, kPlayerAddrInc);
            m_addrCold.chosenHunter = calculatePlayerAddress(m_addrCold.baseChosenHunter, m_playerPosition, 0x01);
            m_addrHot.weaponChange = calculatePlayerAddress(m_addrCold.baseWeaponChange, m_playerPosition, kPlayerAddrInc);
            m_addrHot.selectedWeapon = calculatePlayerAddress(m_addrCold.baseSelectedWeapon, m_playerPosition, kPlayerAddrInc);
            m_addrHot.currentWeapon = m_addrHot.selectedWeapon - 0x1;
            m_addrHot.havingWeapons = m_addrHot.selectedWeapon + 0x3;
            m_addrHot.weaponAmmo = m_addrHot.selectedWeapon - 0x383;
            m_addrHot.jumpFlag = calculatePlayerAddress(m_addrCold.baseJumpFlag, m_playerPosition, kPlayerAddrInc);

            const uint8_t hunterID = FastRead8(mainRAM, m_addrCold.chosenHunter);
            m_flags.assign(StateFlags::BIT_IS_SAMUS, hunterID == 0x00);
            m_flags.assign(StateFlags::BIT_IS_WEAVEL, hunterID == 0x06);

            m_addrCold.inGameSensi = calculatePlayerAddress(m_addrCold.baseInGameSensi, m_playerPosition, 0x04);
            m_addrHot.boostGauge = m_addrHot.loadedSpecialWeapon - 0x12;
            m_addrHot.isBoosting = m_addrHot.loadedSpecialWeapon - 0x10;
            m_addrHot.aimX = calculatePlayerAddress(m_addrCold.baseAimX, m_playerPosition, kAimAddrInc);
            m_addrHot.aimY = calculatePlayerAddress(m_addrCold.baseAimY, m_playerPosition, kAimAddrInc);

            m_flags.assign(StateFlags::BIT_IN_ADVENTURE, FastRead8(mainRAM, m_addrCold.isInAdventure) == 0x02);
        }

        if (isFocused) {
            if (LIKELY(isInGame)) {
                HandleInGameLogic(mainRAM);
            }
            else {
                m_flags.clear(StateFlags::BIT_IN_ADVENTURE);
                isAimDisabled = true;
                if (m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
                    m_flags.clear(StateFlags::BIT_IN_GAME_INIT);
                }
                ApplyGameSettingsOnce();
            }

            if (UNLIKELY(shouldBeCursorMode != isCursorMode)) {
                isCursorMode = shouldBeCursorMode;
                setAimBlockBranchless(AIMBLK_CURSOR_MODE, isCursorMode);
                if (!isStylusMode) ShowCursor(isCursorMode);
            }

            if (isCursorMode) {
                if (emuInstance->isTouching)
                    emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
                else
                    emuInstance->getNDS()->ReleaseScreen();
            }
            InputSetBranchless(INPUT_START, !IsDown(IB_MENU));
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
        m_flags.assign(StateFlags::BIT_LAST_FOCUSED, isFocused);
    }
    isRunningHook = false;
}

// ============================================================
// In-Game Logic (OPTIMIZED)
// ============================================================

HOT_FUNCTION void MelonPrimeCore::HandleInGameLogic(melonDS::u8* mainRAM)
{
    // Prefetch hot addresses for upcoming reads
    PREFETCH_READ(&mainRAM[m_addrHot.isAltForm & RAM_MASK]);

    // 1. Transform
    if (UNLIKELY(IsPressed(IB_MORPH))) {
        if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();
        emuInstance->getNDS()->TouchScreen(231, 167);
        FrameAdvanceTwice();
        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();
    }

    // 2. Weapon Switch (check with single mask)
    if (UNLIKELY(ProcessWeaponSwitch(mainRAM))) {
        if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
    }

    // 3. Weapon Check
    static bool isWeaponCheckActive = false;
    const bool weaponCheckDown = IsDown(IB_WEAPON_CHECK);

    if (weaponCheckDown) {
        if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
        if (!isWeaponCheckActive) {
            isWeaponCheckActive = true;
            setAimBlockBranchless(AIMBLK_CHECK_WEAPON, true);
            emuInstance->getNDS()->ReleaseScreen();
            FrameAdvanceTwice();
        }
        emuInstance->getNDS()->TouchScreen(236, 30);
    }
    else if (UNLIKELY(isWeaponCheckActive)) {
        isWeaponCheckActive = false;
        emuInstance->getNDS()->ReleaseScreen();
        setAimBlockBranchless(AIMBLK_CHECK_WEAPON, false);
        FrameAdvanceTwice();
    }

    // 4. Adventure Mode (rare)
    if (UNLIKELY(m_flags.test(StateFlags::BIT_IN_ADVENTURE))) {
        HandleAdventureMode(mainRAM);
    }

    // 5. Movement (optimized with pre-computed index)
    ProcessMoveInputFast();

    // 6. Basic inputs (branchless)
    InputSetBranchless(INPUT_B, !IsDown(IB_JUMP));
    InputSetBranchless(INPUT_L, !IsDown(IB_SHOOT));
    InputSetBranchless(INPUT_R, !IsDown(IB_ZOOM));

    // 7. Morph Ball Boost
    HandleMorphBallBoost(mainRAM);

    // 8. Aim
    if (isStylusMode) {
        if (!m_flags.test(StateFlags::BIT_BLOCK_STYLUS)) {
            ProcessAimInputStylus();
        }
    }
    else {
        ProcessAimInputMouse(mainRAM);
        if (!m_flags.test(StateFlags::BIT_LAST_FOCUSED) || !isAimDisabled) {
            emuInstance->getNDS()->TouchScreen(128, 88);
        }
    }
}

// ============================================================
// Movement Input (OPTIMIZED with Pre-computed Index)
// ============================================================

HOT_FUNCTION void MelonPrimeCore::ProcessMoveInputFast()
{
    static uint16_t snapState = 0;

    uint32_t curr = m_input.moveIndex;
    uint32_t finalInput;

    if (LIKELY(!m_flags.test(StateFlags::BIT_SNAP_TAP))) {
        finalInput = curr;
    }
    else {
        // SnapTap logic (branchless where possible)
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

    // Apply LUT (single memory access)
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
// Aim Input (OPTIMIZED)
// ============================================================

void MelonPrimeCore::ProcessAimInputStylus()
{
    if (LIKELY(emuInstance->isTouching)) {
        emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
    }
    else {
        emuInstance->getNDS()->ReleaseScreen();
    }
}

HOT_FUNCTION void MelonPrimeCore::ProcessAimInputMouse(melonDS::u8* mainRAM)
{
    if (isAimDisabled) return;

    if (UNLIKELY(m_isLayoutChangePending)) {
        m_isLayoutChangePending = false;
#ifdef _WIN32
        if (g_rawFilter) g_rawFilter->discardDeltas();
#endif
        return;
    }

    if (LIKELY(m_flags.test(StateFlags::BIT_LAST_FOCUSED))) {
        const int deltaX = m_input.mouseX;
        const int deltaY = m_input.mouseY;

        // Early exit with single OR check
        if ((deltaX | deltaY) == 0) return;

        float scaledX = deltaX * gAimSensiFactor;
        float scaledY = deltaY * gAimCombinedY;
        applyAimAdjustBranchless(scaledX, scaledY);

        // Combined zero check
        if (scaledX == 0.0f && scaledY == 0.0f) return;

        FastWrite16(mainRAM, m_addrHot.aimX, static_cast<int16_t>(scaledX));
        FastWrite16(mainRAM, m_addrHot.aimY, static_cast<int16_t>(scaledY));

#if !defined(_WIN32)
        QCursor::setPos(m_aimData.centerX, m_aimData.centerY);
#endif
        return;
    }

#if !defined(_WIN32)
    const QPoint center = GetAdjustedCenter();
    m_aimData.centerX = center.x();
    m_aimData.centerY = center.y();
    QCursor::setPos(center);
#endif
    m_isLayoutChangePending = false;
}

// ============================================================
// Morph Ball Boost (OPTIMIZED)
// ============================================================

HOT_FUNCTION bool MelonPrimeCore::HandleMorphBallBoost(melonDS::u8* mainRAM)
{
    if (!m_flags.test(StateFlags::BIT_IS_SAMUS)) return false;

    if (IsDown(IB_MORPH_BOOST)) {
        const bool isAltForm = FastRead8(mainRAM, m_addrHot.isAltForm) == 0x02;
        m_flags.assign(StateFlags::BIT_IS_ALT_FORM, isAltForm);

        if (isAltForm) {
            const uint8_t boostGaugeValue = FastRead8(mainRAM, m_addrHot.boostGauge);
            const bool isBoosting = FastRead8(mainRAM, m_addrHot.isBoosting) != 0x00;
            const bool isBoostGaugeEnough = boostGaugeValue > 0x0A;

            setAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, true);

            if (!IsDown(IB_WEAPON_CHECK)) {
                emuInstance->getNDS()->ReleaseScreen();
            }

            InputSetBranchless(INPUT_R, !isBoosting && isBoostGaugeEnough);

            if (isBoosting) {
                setAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, false);
            }
            return true;
        }
    }
    else {
        setAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, false);
    }
    return false;
}

// ============================================================
// Adventure Mode
// ============================================================

void MelonPrimeCore::HandleAdventureMode(melonDS::u8* mainRAM)
{
    const bool isPaused = FastRead8(mainRAM, m_addrHot.isMapOrUserActionPaused) == 0x1;
    m_flags.assign(StateFlags::BIT_PAUSED, isPaused);

    if (IsPressed(IB_SCAN_VISOR)) {
        if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();
        emuInstance->getNDS()->TouchScreen(128, 173);

        if (FastRead8(mainRAM, m_addrHot.isInVisorOrMap) == 0x1) {
            FrameAdvanceTwice();
        }
        else {
            for (int i = 0; i < 30; i++) {
                UpdateInputState();
                ProcessMoveInputFast();
                emuInstance->getNDS()->SetKeyMask(gInputMaskFast);
                FrameAdvanceOnce();
            }
        }
        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();
    }

    // UI touches with macro
#define TOUCH_IF_PRESSED(BIT, X, Y) \
        if (IsPressed(BIT)) { \
            emuInstance->getNDS()->ReleaseScreen(); \
            FrameAdvanceTwice(); \
            emuInstance->getNDS()->TouchScreen((X), (Y)); \
            FrameAdvanceTwice(); \
        }

    TOUCH_IF_PRESSED(IB_UI_OK, 128, 142)
        TOUCH_IF_PRESSED(IB_UI_LEFT, 71, 141)
        TOUCH_IF_PRESSED(IB_UI_RIGHT, 185, 141)
        TOUCH_IF_PRESSED(IB_UI_YES, 96, 142)
        TOUCH_IF_PRESSED(IB_UI_NO, 160, 142)

#undef TOUCH_IF_PRESSED
}

// ============================================================
// Weapon Switch (OPTIMIZED)
// ============================================================

HOT_FUNCTION bool MelonPrimeCore::ProcessWeaponSwitch(melonDS::u8* mainRAM)
{
    static constexpr uint8_t  WEAPON_ORDER[] = { 0, 2, 7, 6, 5, 4, 3, 1, 8 };
    static constexpr uint16_t WEAPON_MASKS[] = { 0x001, 0x004, 0x080, 0x040, 0x020, 0x010, 0x008, 0x002, 0x100 };
    static constexpr uint8_t  MIN_AMMO[] = { 0, 0x5, 0xA, 0x4, 0x14, 0x5, 0xA, 0xA, 0 };
    static constexpr uint8_t  WEAPON_INDEX_MAP[] = { 0, 7, 1, 6, 5, 4, 3, 2, 8 };
    static constexpr uint8_t  WEAPON_COUNT = 9;

    static constexpr uint64_t BIT_MAP[] = {
        IB_WEAPON_BEAM, IB_WEAPON_MISSILE, IB_WEAPON_1, IB_WEAPON_2,
        IB_WEAPON_3, IB_WEAPON_4, IB_WEAPON_5, IB_WEAPON_6, IB_WEAPON_SPECIAL
    };
    static constexpr uint8_t BIT_WEAPON_ID[] = {
        0, 2, 7, 6, 5, 4, 3, 1, 0xFF
    };

    // Quick check: any weapon key pressed?
    if (LIKELY(!IsAnyPressed(IB_WEAPON_ANY))) {
        // Check wheel/next/prev
        auto* panel = emuInstance->getMainWindow()->panel;
        if (!panel) return false;
        const int wheelDelta = panel->getDelta();
        const bool nextKey = IsPressed(IB_WEAPON_NEXT);
        const bool prevKey = IsPressed(IB_WEAPON_PREV);

        if (!wheelDelta && !nextKey && !prevKey) return false;

        if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

        const bool forward = (wheelDelta < 0) || nextKey;
        const uint8_t curID = FastRead8(mainRAM, m_addrHot.currentWeapon);
        const uint16_t having = FastRead16(mainRAM, m_addrHot.havingWeapons);
        const uint32_t ammoData = FastRead32(mainRAM, m_addrHot.weaponAmmo);
        const uint16_t weaponAmmo = static_cast<uint16_t>(ammoData & 0xFFFF);
        const uint16_t missileAmmo = static_cast<uint16_t>(ammoData >> 16);

        uint16_t available = 0;
        const bool isWeavel = m_flags.test(StateFlags::BIT_IS_WEAVEL);

        for (int i = 0; i < WEAPON_COUNT; ++i) {
            const uint8_t wid = WEAPON_ORDER[i];
            const bool owned = (wid == 0 || wid == 2) || ((having & WEAPON_MASKS[i]) != 0);
            if (!owned) continue;

            bool ok = true;
            if (wid == 2) ok = (missileAmmo >= 0xA);
            else if (wid != 0 && wid != 8) {
                uint8_t req = MIN_AMMO[wid];
                if (wid == 3 && isWeavel) req = 0x5;
                ok = (weaponAmmo >= req);
            }
            if (ok) available |= (1u << i);
        }

        if (!available) {
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
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

        m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
        return false;
    }

    // Direct weapon key pressed
    if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

    // Find which weapon key
    uint32_t hot = 0;
    for (size_t i = 0; i < 9; ++i) {
        if (IsPressed(BIT_MAP[i])) hot |= (1u << i);
    }

    const int firstSet = __builtin_ctz(hot);

    if (UNLIKELY(firstSet == 8)) {
        const uint8_t loaded = FastRead8(mainRAM, m_addrHot.loadedSpecialWeapon);
        if (loaded == 0xFF) {
            emuInstance->osdAddMessage(0, "Have not Special Weapon yet!");
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }
        SwitchWeapon(mainRAM, loaded);
        return true;
    }

    const uint8_t weaponID = BIT_WEAPON_ID[firstSet];
    const uint16_t having = FastRead16(mainRAM, m_addrHot.havingWeapons);
    const uint32_t ammoData = FastRead32(mainRAM, m_addrHot.weaponAmmo);
    const uint16_t weaponAmmo = static_cast<uint16_t>(ammoData & 0xFFFF);
    const uint16_t missileAmmo = static_cast<uint16_t>(ammoData >> 16);

    const bool owned = (weaponID == 0 || weaponID == 2) ||
        ((having & WEAPON_MASKS[WEAPON_INDEX_MAP[weaponID]]) != 0);
    if (!owned) {
        emuInstance->osdAddMessage(0, "Have not %s yet!", kWeaponNames[weaponID]);
        m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
        return false;
    }

    bool hasAmmo = true;
    if (weaponID == 2) {
        hasAmmo = (missileAmmo >= 0xA);
    }
    else if (weaponID != 0 && weaponID != 8) {
        uint8_t required = MIN_AMMO[weaponID];
        if (weaponID == 3 && m_flags.test(StateFlags::BIT_IS_WEAVEL)) required = 0x5;
        hasAmmo = (weaponAmmo >= required);
    }

    if (!hasAmmo) {
        emuInstance->osdAddMessage(0, "Not enough Ammo for %s!", kWeaponNames[weaponID]);
        m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
        return false;
    }

    SwitchWeapon(mainRAM, weaponID);
    return true;
}

// ============================================================
// Switch Weapon
// ============================================================

void MelonPrimeCore::SwitchWeapon(melonDS::u8* mainRAM, int weaponIndex)
{
    if (FastRead8(mainRAM, m_addrHot.selectedWeapon) == weaponIndex) return;

    if (m_flags.test(StateFlags::BIT_IN_ADVENTURE)) {
        if (m_flags.test(StateFlags::BIT_PAUSED)) {
            emuInstance->osdAddMessage(0, "You can't switch weapon now!");
            return;
        }
        if (FastRead8(mainRAM, m_addrHot.isInVisorOrMap) == 0x1) return;
    }

    uint8_t currentJumpFlags = FastRead8(mainRAM, m_addrHot.jumpFlag);
    bool isTransforming = currentJumpFlags & 0x10;
    uint8_t jumpFlag = currentJumpFlags & 0x0F;
    bool isRestoreNeeded = false;

    const bool isAltForm = FastRead8(mainRAM, m_addrHot.isAltForm) == 0x02;
    m_flags.assign(StateFlags::BIT_IS_ALT_FORM, isAltForm);

    if (!isTransforming && jumpFlag == 0 && !isAltForm) {
        FastWrite8(mainRAM, m_addrHot.jumpFlag, (currentJumpFlags & 0xF0) | 0x01);
        isRestoreNeeded = true;
    }

    FastWrite8(mainRAM, m_addrHot.weaponChange, (FastRead8(mainRAM, m_addrHot.weaponChange) & 0xF0) | 0x0B);
    FastWrite8(mainRAM, m_addrHot.selectedWeapon, weaponIndex);
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
        currentJumpFlags = FastRead8(mainRAM, m_addrHot.jumpFlag);
        FastWrite8(mainRAM, m_addrHot.jumpFlag, (currentJumpFlags & 0xF0) | jumpFlag);
    }
}

// ============================================================
// Apply Game Settings
// ============================================================

COLD_FUNCTION void MelonPrimeCore::ApplyGameSettingsOnce()
{
    InputSetBranchless(INPUT_L, !IsPressed(IB_UI_LEFT));
    InputSetBranchless(INPUT_R, !IsPressed(IB_UI_RIGHT));

    ApplyHeadphoneOnce(emuInstance->getNDS(), localCfg, m_addrCold.operationAndSound, m_appliedFlags, APPLIED_HEADPHONE);
    ApplyMphSensitivity(emuInstance->getNDS(), localCfg, m_addrCold.sensitivity, m_addrCold.inGameSensi, m_flags.test(StateFlags::BIT_IN_GAME_INIT));
    ApplyUnlockHuntersMaps(emuInstance->getNDS(), localCfg, m_appliedFlags, APPLIED_UNLOCK,
        m_addrCold.unlockMapsHunters, m_addrCold.unlockMapsHunters2, m_addrCold.unlockMapsHunters3,
        m_addrCold.unlockMapsHunters4, m_addrCold.unlockMapsHunters5);
    useDsName(emuInstance->getNDS(), localCfg, m_addrCold.dsNameFlagAndMicVolume);
    applySelectedHunterStrict(emuInstance->getNDS(), localCfg, m_addrCold.mainHunter);
    applyLicenseColorStrict(emuInstance->getNDS(), localCfg, m_addrCold.rankColor);
    ApplySfxVolumeOnce(emuInstance->getNDS(), localCfg, m_addrCold.volSfx8Bit, m_appliedFlags, APPLIED_VOL_SFX);
    ApplyMusicVolumeOnce(emuInstance->getNDS(), localCfg, m_addrCold.volMusic8Bit, m_appliedFlags, APPLIED_VOL_MUSIC);
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

// ============================================================
// Static Helpers (with packed flags)
// ============================================================

bool MelonPrimeCore::ApplyHeadphoneOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, uint8_t& flags, uint8_t bit)
{
    if (!nds || (flags & bit)) return false;
    if (!localCfg.GetBool("Metroid.Apply.Headphone")) return false;
    uint8_t oldVal = nds->ARM9Read8(addr);
    constexpr uint8_t kMask = 0x18;
    if ((oldVal & kMask) == kMask) { flags |= bit; return false; }
    nds->ARM9Write8(addr, oldVal | kMask);
    flags |= bit;
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
    sel = std::clamp(sel, 0, 6);
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

bool MelonPrimeCore::ApplySfxVolumeOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, uint8_t& flags, uint8_t bit)
{
    if (!nds || (flags & bit) || !localCfg.GetBool("Metroid.Apply.SfxVolume")) return false;
    uint8_t oldVal = nds->ARM9Read8(addr);
    uint8_t steps = static_cast<uint8_t>(std::clamp(localCfg.GetInt("Metroid.Volume.SFX"), 0, 9));
    uint8_t newVal = (oldVal & 0xC0) | ((steps & 0x0F) << 2) | 0x03;
    if (newVal == oldVal) { flags |= bit; return false; }
    nds->ARM9Write8(addr, newVal);
    flags |= bit;
    return true;
}

bool MelonPrimeCore::ApplyMusicVolumeOnce(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, uint8_t& flags, uint8_t bit)
{
    if (!nds || (flags & bit) || !localCfg.GetBool("Metroid.Apply.MusicVolume")) return false;
    uint8_t oldVal = nds->ARM9Read8(addr);
    uint8_t steps = static_cast<uint8_t>(std::clamp(localCfg.GetInt("Metroid.Volume.Music"), 0, 9));
    uint8_t newVal = (oldVal & ~0x3C) | ((steps & 0x0F) << 2);
    if (newVal == oldVal) { flags |= bit; return false; }
    nds->ARM9Write8(addr, newVal);
    flags |= bit;
    return true;
}

void MelonPrimeCore::ApplyMphSensitivity(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addrSensi, melonDS::u32 addrInGame, bool inGameInit)
{
    double mphSensi = localCfg.GetDouble("Metroid.Sensitivity.Mph");
    uint16_t sensiVal = sensiNumToSensiVal(mphSensi);
    nds->ARM9Write16(addrSensi, sensiVal);
    if (inGameInit) nds->ARM9Write16(addrInGame, sensiVal);
}

bool MelonPrimeCore::ApplyUnlockHuntersMaps(melonDS::NDS* nds, Config::Table& localCfg, uint8_t& flags, uint8_t bit,
    melonDS::u32 a1, melonDS::u32 a2, melonDS::u32 a3, melonDS::u32 a4, melonDS::u32 a5)
{
    if ((flags & bit) || !localCfg.GetBool("Metroid.Data.Unlock")) return false;
    nds->ARM9Write8(a1, nds->ARM9Read8(a1) | 0x03);
    nds->ARM9Write32(a2, 0x07FFFFFF);
    nds->ARM9Write8(a3, 0x7F);
    nds->ARM9Write32(a4, 0xFFFFFFFF);
    nds->ARM9Write8(a5, 0x7F);
    flags |= bit;
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