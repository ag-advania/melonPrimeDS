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
#include <array>
#include <string_view>
#include <QCoreApplication>
#include <QCursor>
#include <QBitArray>

#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeInputState.h"
#include "MelonPrimeHotkeyVkBinding.h"

namespace MelonPrime {
    void FilterDeleter::operator()(RawInputWinFilter* ptr) {
        if (ptr) RawInputWinFilter::Release();
    }
}
#endif

namespace MelonPrime {

    namespace Consts {
        constexpr int32_t PLAYER_ADDR_INC = 0xF30;
        constexpr uint8_t AIM_ADDR_INC = 0x48;
        constexpr uint32_t VISOR_OFFSET = 0xABB;
        constexpr uint32_t RAM_MASK = 0x3FFFFF;

        namespace UI {
            constexpr QPoint SCAN_VISOR_BUTTON{ 128, 173 };
            constexpr QPoint OK{ 128, 142 };
            constexpr QPoint LEFT{ 71, 141 };
            constexpr QPoint RIGHT{ 185, 141 };
            constexpr QPoint YES{ 96, 142 };
            constexpr QPoint NO{ 160, 142 };
            constexpr QPoint CENTER_RESET{ 128, 88 };
            constexpr QPoint WEAPON_CHECK_START{ 236, 30 };
            constexpr QPoint MORPH_START{ 231, 167 };
        }
    }

    enum InputBit : uint16_t {
        INPUT_A = 0, INPUT_B = 1, INPUT_SELECT = 2, INPUT_START = 3,
        INPUT_RIGHT = 4, INPUT_LEFT = 5, INPUT_UP = 6, INPUT_DOWN = 7,
        INPUT_R = 8, INPUT_L = 9, INPUT_X = 10, INPUT_Y = 11,
    };

    static constexpr std::array<std::string_view, 9> kWeaponNames = {
        "Power Beam", "Volt Driver", "Missile Launcher", "Battlehammer",
        "Imperialist", "Judicator", "Magmaul", "Shock Coil", "Omega Cannon"
    };

    namespace WeaponData {
        struct Info {
            uint8_t id;
            uint16_t mask;
            uint8_t minAmmo;
        };
        constexpr std::array<Info, 9> ORDERED_WEAPONS = { {
            {0, 0x001, 0},    // Power Beam
            {2, 0x004, 0x5},  // Missile
            {7, 0x080, 0xA},  // Volt Driver
            {6, 0x040, 0x4},  // Battlehammer
            {5, 0x020, 0x14}, // Imperialist
            {4, 0x010, 0x5},  // Judicator
            {3, 0x008, 0xA},  // Magmaul
            {1, 0x002, 0xA},  // Shock Coil
            {8, 0x100, 0}     // Omega Cannon
        } };
        constexpr uint64_t BIT_MAP[] = {
            IB_WEAPON_BEAM, IB_WEAPON_MISSILE, IB_WEAPON_1, IB_WEAPON_2,
            IB_WEAPON_3, IB_WEAPON_4, IB_WEAPON_5, IB_WEAPON_6, IB_WEAPON_SPECIAL
        };
        constexpr uint8_t BIT_WEAPON_ID[] = {
            0, 2, 7, 6, 5, 4, 3, 1, 0xFF
        };
        constexpr uint8_t ID_TO_INDEX[] = { 0, 7, 1, 6, 5, 4, 3, 2, 8 };
    }

    alignas(64) static constexpr std::array<uint8_t, 16> MoveLUT = {
        0xF0, 0xB0, 0x70, 0xF0, 0xD0, 0x90, 0x50, 0xD0,
        0xE0, 0xA0, 0x60, 0xE0, 0xF0, 0xB0, 0x70, 0xF0,
    };

    FORCE_INLINE std::uint16_t SensiNumToSensiVal(double sensiNum) {
        constexpr std::uint32_t BASE_VAL = 0x0999;
        constexpr std::uint32_t STEP_VAL = 0x0199;
        double val = static_cast<double>(BASE_VAL) + (sensiNum - 1.0) * static_cast<double>(STEP_VAL);
        return static_cast<std::uint16_t>(std::min(static_cast<uint32_t>(std::llround(val)), 0xFFFFu));
    }

    FORCE_INLINE uint8_t Read8(const melonDS::u8* ram, melonDS::u32 addr) {
        return ram[addr & Consts::RAM_MASK];
    }
    FORCE_INLINE uint16_t Read16(const melonDS::u8* ram, melonDS::u32 addr) {
        return *reinterpret_cast<const uint16_t*>(&ram[addr & Consts::RAM_MASK]);
    }
    FORCE_INLINE uint32_t Read32(const melonDS::u8* ram, melonDS::u32 addr) {
        return *reinterpret_cast<const uint32_t*>(&ram[addr & Consts::RAM_MASK]);
    }
    FORCE_INLINE void Write8(melonDS::u8* ram, melonDS::u32 addr, uint8_t val) {
        ram[addr & Consts::RAM_MASK] = val;
    }
    FORCE_INLINE void Write16(melonDS::u8* ram, melonDS::u32 addr, uint16_t val) {
        *reinterpret_cast<uint16_t*>(&ram[addr & Consts::RAM_MASK]) = val;
    }

    FORCE_INLINE bool MelonPrimeCore::IsJoyDown(int id) const {
#ifdef _WIN32
        return emuInstance->joyHotkeyMask.testBit(id);
#else
        return false;
#endif
    }

    FORCE_INLINE bool MelonPrimeCore::IsJoyPressed(int id) const {
#ifdef _WIN32
        return emuInstance->joyHotkeyPress.testBit(id);
#else
        return false;
#endif
    }

    MelonPrimeCore::MelonPrimeCore(EmuInstance* instance)
        : emuInstance(instance),
        localCfg(instance->getLocalConfig()),
        globalCfg(instance->getGlobalConfig())
    {
        m_flags.packed = 0;
    }

    MelonPrimeCore::~MelonPrimeCore()
    {
    }

    void MelonPrimeCore::Initialize()
    {
        m_flags.assign(StateFlags::BIT_JOY2KEY, localCfg.GetBool("Metroid.Apply.joy2KeySupport"));
        m_flags.assign(StateFlags::BIT_SNAP_TAP, localCfg.GetBool("Metroid.Operation.SnapTap"));
        m_flags.assign(StateFlags::BIT_STYLUS_MODE, localCfg.GetBool("Metroid.Enable.stylusMode"));
        isStylusMode = m_flags.test(StateFlags::BIT_STYLUS_MODE);

        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);

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
        if (!m_rawFilter) {
            HWND hwnd = nullptr;
            if (auto* mw = emuInstance->getMainWindow()) {
                hwnd = reinterpret_cast<HWND>(mw->winId());
            }
            m_rawFilter.reset(RawInputWinFilter::Acquire(m_flags.test(StateFlags::BIT_JOY2KEY), hwnd));
            ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));
            melonDS::BindMetroidHotkeysFromConfig(m_rawFilter.get(), emuInstance->getInstanceID());
        }
#endif
    }

    void MelonPrimeCore::ApplyJoy2KeySupportAndQtFilter(bool enable, bool doReset)
    {
#ifdef _WIN32
        if (!m_rawFilter) return;
        QCoreApplication* app = QCoreApplication::instance();
        if (!app) return;

        HWND hwnd = nullptr;
        if (auto* mw = emuInstance->getMainWindow()) {
            hwnd = reinterpret_cast<HWND>(mw->winId());
        }

        m_rawFilter->setRawInputTarget(hwnd);
        m_flags.assign(StateFlags::BIT_JOY2KEY, enable);

        static bool s_isInstalled = false;

        // モード変更通知 (これでターゲットが切り替わる: QtWindow <-> HiddenWindow)
        m_rawFilter->setJoy2KeySupport(enable);

        if (!enable) {
            // Joy2Key OFF: 隠しウィンドウモード
            // Qtフィルタは不要なので外す（隠しウィンドウ宛なのでQtには来ないが念のため）
            if (s_isInstalled) {
                app->removeNativeEventFilter(m_rawFilter.get());
                s_isInstalled = false;
            }
        }
        else {
            // Joy2Key ON: Qtフィルタモード
            if (!s_isInstalled) {
                app->installNativeEventFilter(m_rawFilter.get());
                s_isInstalled = true;
            }
        }

        if (doReset) {
            m_rawFilter->resetAllKeys();
            m_rawFilter->resetMouseButtons();
            m_rawFilter->resetHotkeyEdges();
        }
#endif
    }

    void MelonPrimeCore::RecalcAimSensitivityCache(Config::Table& cfg) {
        const int sens = cfg.GetInt("Metroid.Sensitivity.Aim");
        const float aimYAxisScale = static_cast<float>(cfg.GetDouble("Metroid.Sensitivity.AimYAxisScale"));
        m_aimSensiFactor = sens * 0.01f;
        m_aimCombinedY = m_aimSensiFactor * aimYAxisScale;
    }

    void MelonPrimeCore::ApplyAimAdjustSetting(Config::Table& cfg) {
        double v = cfg.GetDouble("Metroid.Aim.Adjust");
        m_aimAdjust = static_cast<float>(std::max(0.0, std::isnan(v) ? 0.0 : v));
    }

    HOT_FUNCTION void MelonPrimeCore::UpdateInputState()
    {
        m_input.down = 0;
        m_input.press = 0;
        m_input.mouseX = 0;
        m_input.mouseY = 0;
        m_input.moveIndex = 0;

#ifdef _WIN32
        if (!isFocused) return;
        if (m_rawFilter) {
            HWND myHwnd = (HWND)emuInstance->getMainWindow()->winId();
            m_rawFilter->setRawInputTarget(myHwnd);

            // Joy2Key OFFなら、メインスレッドPollingを実行！
            // ここでNtUserGetRawInputBufferが炸裂し、隠しウィンドウ宛の入力を一括取得します
            if (!m_flags.test(StateFlags::BIT_JOY2KEY)) {
                m_rawFilter->poll();
            }
        }
#endif

        uint64_t down = 0;
        uint64_t press = 0;

        // =================================================================
        // [最適化] pollHotkeys: スナップショット1回 + バインド済みのみ走査
        // =================================================================
#ifdef _WIN32
        FrameHotkeyState hk{};
        if (m_rawFilter) {
            m_rawFilter->pollHotkeys(hk);
        }

        auto hkDown = [&](int id) -> bool {
            return hk.isDown(id) || IsJoyDown(id);
            };
        auto hkPressed = [&](int id) -> bool {
            return hk.isPressed(id) || IsJoyPressed(id);
            };
#else
        auto hkDown = [&](int id) -> bool {
            return emuInstance->hotkeyMask.testBit(id);
            };
        auto hkPressed = [&](int id) -> bool {
            return emuInstance->hotkeyPress.testBit(id);
            };
#endif

        down |= hkDown(HK_MetroidMoveForward) ? IB_MOVE_F : 0;
        down |= hkDown(HK_MetroidMoveBack) ? IB_MOVE_B : 0;
        down |= hkDown(HK_MetroidMoveLeft) ? IB_MOVE_L : 0;
        down |= hkDown(HK_MetroidMoveRight) ? IB_MOVE_R : 0;
        down |= hkDown(HK_MetroidJump) ? IB_JUMP : 0;
        down |= hkDown(HK_MetroidZoom) ? IB_ZOOM : 0;
        down |= (hkDown(HK_MetroidShootScan) || hkDown(HK_MetroidScanShoot)) ? IB_SHOOT : 0;
        down |= hkDown(HK_MetroidWeaponCheck) ? IB_WEAPON_CHECK : 0;
        down |= hkDown(HK_MetroidHoldMorphBallBoost) ? IB_MORPH_BOOST : 0;
        down |= hkDown(HK_MetroidMenu) ? IB_MENU : 0;

        press |= hkPressed(HK_MetroidMorphBall) ? IB_MORPH : 0;
        press |= hkPressed(HK_MetroidScanVisor) ? IB_SCAN_VISOR : 0;
        press |= hkPressed(HK_MetroidUIOk) ? IB_UI_OK : 0;
        press |= hkPressed(HK_MetroidUILeft) ? IB_UI_LEFT : 0;
        press |= hkPressed(HK_MetroidUIRight) ? IB_UI_RIGHT : 0;
        press |= hkPressed(HK_MetroidUIYes) ? IB_UI_YES : 0;
        press |= hkPressed(HK_MetroidUINo) ? IB_UI_NO : 0;
        press |= hkPressed(HK_MetroidWeaponBeam) ? IB_WEAPON_BEAM : 0;
        press |= hkPressed(HK_MetroidWeaponMissile) ? IB_WEAPON_MISSILE : 0;
        press |= hkPressed(HK_MetroidWeapon1) ? IB_WEAPON_1 : 0;
        press |= hkPressed(HK_MetroidWeapon2) ? IB_WEAPON_2 : 0;
        press |= hkPressed(HK_MetroidWeapon3) ? IB_WEAPON_3 : 0;
        press |= hkPressed(HK_MetroidWeapon4) ? IB_WEAPON_4 : 0;
        press |= hkPressed(HK_MetroidWeapon5) ? IB_WEAPON_5 : 0;
        press |= hkPressed(HK_MetroidWeapon6) ? IB_WEAPON_6 : 0;
        press |= hkPressed(HK_MetroidWeaponSpecial) ? IB_WEAPON_SPECIAL : 0;
        press |= hkPressed(HK_MetroidWeaponNext) ? IB_WEAPON_NEXT : 0;
        press |= hkPressed(HK_MetroidWeaponPrevious) ? IB_WEAPON_PREV : 0;

        m_input.down = down;
        m_input.press = press;
        m_input.moveIndex = static_cast<uint32_t>((down >> 6) & 0xF);

#if defined(_WIN32)
        if (m_rawFilter) {
            m_rawFilter->fetchMouseDelta(m_input.mouseX, m_input.mouseY);
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

    void MelonPrimeCore::OnEmuStart()
    {
        m_flags.packed = StateFlags::BIT_LAYOUT_PENDING;
        m_isInGame = false;
        m_appliedFlags = 0;
        m_flags.assign(StateFlags::BIT_SNAP_TAP, localCfg.GetBool("Metroid.Operation.SnapTap"));
        m_flags.assign(StateFlags::BIT_STYLUS_MODE, localCfg.GetBool("Metroid.Enable.stylusMode"));
        m_flags.assign(StateFlags::BIT_JOY2KEY, localCfg.GetBool("Metroid.Apply.joy2KeySupport"));
        isStylusMode = m_flags.test(StateFlags::BIT_STYLUS_MODE);
        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));
        m_isWeaponCheckActive = false;
        InputReset();
    }

    void MelonPrimeCore::OnEmuStop()
    {
        m_flags.clear(StateFlags::BIT_IN_GAME);
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

        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);

#ifdef _WIN32
        if (m_rawFilter) {
            melonDS::BindMetroidHotkeysFromConfig(m_rawFilter.get(), emuInstance->getInstanceID());
            m_rawFilter->resetHotkeyEdges();
        }
#endif

        if (m_flags.test(StateFlags::BIT_IN_GAME)) {
            m_flags.clear(StateFlags::BIT_IN_GAME_INIT);
        }
    }

    void MelonPrimeCore::OnReset() { OnEmuStart(); }

    bool MelonPrimeCore::ShouldForceSoftwareRenderer() const
    {
        return m_flags.test(StateFlags::BIT_ROM_DETECTED) && !m_flags.test(StateFlags::BIT_IN_GAME);
    }

    COLD_FUNCTION void MelonPrimeCore::DetectRomAndSetAddresses()
    {
        struct RomInfo {
            uint32_t checksum;
            const char* name;
            RomGroup group;
        };

        static const std::array<RomInfo, 16> ROM_INFO_TABLE = { {
            {RomVersions::US1_1,           "US1.1",           RomGroup::US1_1},
            {RomVersions::US1_1_ENCRYPTED, "US1.1 ENCRYPTED", RomGroup::US1_1},
            {RomVersions::US1_0,           "US1.0",           RomGroup::US1_0},
            {RomVersions::US1_0_ENCRYPTED, "US1.0 ENCRYPTED", RomGroup::US1_0},
            {RomVersions::EU1_1,           "EU1.1",           RomGroup::EU1_1},
            {RomVersions::EU1_1_ENCRYPTED, "EU1.1 ENCRYPTED", RomGroup::EU1_1},
            {RomVersions::EU1_1_BALANCED,  "EU1.1 BALANCED",  RomGroup::EU1_1},
            {RomVersions::EU1_1_RUSSIANED, "EU1.1 RUSSIANED", RomGroup::EU1_1},
            {RomVersions::EU1_0,           "EU1.0",           RomGroup::EU1_0},
            {RomVersions::EU1_0_ENCRYPTED, "EU1.0 ENCRYPTED", RomGroup::EU1_0},
            {RomVersions::JP1_0,           "JP1.0",           RomGroup::JP1_0},
            {RomVersions::JP1_0_ENCRYPTED, "JP1.0 ENCRYPTED", RomGroup::JP1_0},
            {RomVersions::JP1_1,           "JP1.1",           RomGroup::JP1_1},
            {RomVersions::JP1_1_ENCRYPTED, "JP1.1 ENCRYPTED", RomGroup::JP1_1},
            {RomVersions::KR1_0,           "KR1.0",           RomGroup::KR1_0},
            {RomVersions::KR1_0_ENCRYPTED, "KR1.0 ENCRYPTED", RomGroup::KR1_0},
        } };

        const RomInfo* romInfo = nullptr;
        for (const auto& info : ROM_INFO_TABLE) {
            if (globalChecksum == info.checksum) {
                romInfo = &info;
                break;
            }
        }

        if (!romInfo) return;

        const auto& addr = getRomAddrs(romInfo->group);

        m_addrCold.baseChosenHunter = addr.addrBaseChosenHunter;
        m_addrHot.inGame = addr.addrInGame;
        m_addrCold.playerPos = addr.addrPlayerPos;
        m_addrCold.baseIsAltForm = addr.addrBaseIsAltForm;
        m_addrCold.baseWeaponChange = addr.addrBaseWeaponChange;
        m_addrCold.baseSelectedWeapon = addr.addrBaseSelectedWeapon;
        m_addrCold.baseAimX = addr.addrBaseAimX;
        m_addrCold.baseAimY = addr.addrBaseAimY;
        m_addrCold.isInAdventure = addr.addrIsInAdventure;
        m_addrHot.isMapOrUserActionPaused = addr.addrIsMapOrUserActionPaused;
        m_addrCold.unlockMapsHunters = addr.addrUnlockMapsHunters;
        m_addrCold.sensitivity = addr.addrSensitivity;
        m_addrCold.mainHunter = addr.addrMainHunter;
        m_addrCold.baseLoadedSpecialWeapon = addr.addrBaseLoadedSpecialWeapon;

        m_addrHot.isInVisorOrMap = m_addrCold.playerPos - Consts::VISOR_OFFSET;
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

        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);
    }

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
                RecalcAimSensitivityCache(localCfg);
                emuInstance->osdAddMessage(0, "AimSensi Updated: %d->%d", currentSensitivity, newSensitivity);
            }
        }
    }

    HOT_FUNCTION void MelonPrimeCore::RunFrameHook()
    {
        melonDS::u8* mainRAM = emuInstance->getNDS()->MainRAM;

        if (UNLIKELY(m_isRunningHook)) {
            ProcessMoveInputFast();
            InputSetBranchless(INPUT_B, !IsDown(IB_JUMP));

            if (isStylusMode) {
                if (emuInstance->isTouching && !m_flags.test(StateFlags::BIT_BLOCK_STYLUS)) {
                    emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
                }
            }
            else {
                ProcessAimInputMouse();
            }

            InputSetBranchless(INPUT_L, !IsDown(IB_SHOOT));
            InputSetBranchless(INPUT_R, !IsDown(IB_ZOOM));
            return;
        }

        m_isRunningHook = true;

        UpdateInputState();
        InputReset();
        m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);

        HandleGlobalHotkeys();

        const bool wasDetected = m_flags.test(StateFlags::BIT_ROM_DETECTED);
        if (UNLIKELY(!wasDetected)) {
            DetectRomAndSetAddresses();
        }

        if (LIKELY(m_flags.test(StateFlags::BIT_ROM_DETECTED))) {
            const bool isInGame = Read16(mainRAM, m_addrHot.inGame) == 0x0001;
            m_flags.assign(StateFlags::BIT_IN_GAME, isInGame);
            m_isInGame = isInGame;

            const bool isInAdventure = m_flags.test(StateFlags::BIT_IN_ADVENTURE);
            const bool isPaused = m_flags.test(StateFlags::BIT_PAUSED);
            const bool shouldBeCursorMode = !isInGame || (isInAdventure && isPaused);

            if (isInGame && !m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
                m_flags.set(StateFlags::BIT_IN_GAME_INIT);
                m_playerPosition = Read8(mainRAM, m_addrCold.playerPos);

                using namespace Consts;
                m_addrHot.isAltForm = CalculatePlayerAddress(m_addrCold.baseIsAltForm, m_playerPosition, PLAYER_ADDR_INC);
                m_addrHot.loadedSpecialWeapon = CalculatePlayerAddress(m_addrCold.baseLoadedSpecialWeapon, m_playerPosition, PLAYER_ADDR_INC);
                m_addrHot.weaponChange = CalculatePlayerAddress(m_addrCold.baseWeaponChange, m_playerPosition, PLAYER_ADDR_INC);
                m_addrHot.selectedWeapon = CalculatePlayerAddress(m_addrCold.baseSelectedWeapon, m_playerPosition, PLAYER_ADDR_INC);
                m_addrHot.jumpFlag = CalculatePlayerAddress(m_addrCold.baseJumpFlag, m_playerPosition, PLAYER_ADDR_INC);
                m_addrHot.aimX = CalculatePlayerAddress(m_addrCold.baseAimX, m_playerPosition, AIM_ADDR_INC);
                m_addrHot.aimY = CalculatePlayerAddress(m_addrCold.baseAimY, m_playerPosition, AIM_ADDR_INC);

                m_addrHot.currentWeapon = m_addrHot.selectedWeapon - 0x1;
                m_addrHot.havingWeapons = m_addrHot.selectedWeapon + 0x3;
                m_addrHot.weaponAmmo = m_addrHot.selectedWeapon - 0x383;
                m_addrHot.boostGauge = m_addrHot.loadedSpecialWeapon - 0x12;
                m_addrHot.isBoosting = m_addrHot.loadedSpecialWeapon - 0x10;

                m_addrCold.chosenHunter = CalculatePlayerAddress(m_addrCold.baseChosenHunter, m_playerPosition, 0x01);
                m_addrCold.inGameSensi = CalculatePlayerAddress(m_addrCold.baseInGameSensi, m_playerPosition, 0x04);

                m_ptrs.isAltForm = GetRamPointer<uint8_t>(mainRAM, m_addrHot.isAltForm);
                m_ptrs.jumpFlag = GetRamPointer<uint8_t>(mainRAM, m_addrHot.jumpFlag);
                m_ptrs.weaponChange = GetRamPointer<uint8_t>(mainRAM, m_addrHot.weaponChange);
                m_ptrs.selectedWeapon = GetRamPointer<uint8_t>(mainRAM, m_addrHot.selectedWeapon);
                m_ptrs.currentWeapon = GetRamPointer<uint8_t>(mainRAM, m_addrHot.currentWeapon);
                m_ptrs.havingWeapons = GetRamPointer<uint16_t>(mainRAM, m_addrHot.havingWeapons);
                m_ptrs.weaponAmmo = GetRamPointer<uint32_t>(mainRAM, m_addrHot.weaponAmmo);
                m_ptrs.boostGauge = GetRamPointer<uint8_t>(mainRAM, m_addrHot.boostGauge);
                m_ptrs.isBoosting = GetRamPointer<uint8_t>(mainRAM, m_addrHot.isBoosting);
                m_ptrs.loadedSpecialWeapon = GetRamPointer<uint8_t>(mainRAM, m_addrHot.loadedSpecialWeapon);
                m_ptrs.aimX = GetRamPointer<uint16_t>(mainRAM, m_addrHot.aimX);
                m_ptrs.aimY = GetRamPointer<uint16_t>(mainRAM, m_addrHot.aimY);
                m_ptrs.isInVisorOrMap = GetRamPointer<uint8_t>(mainRAM, m_addrHot.isInVisorOrMap);
                m_ptrs.isMapOrUserActionPaused = GetRamPointer<uint8_t>(mainRAM, m_addrHot.isMapOrUserActionPaused);

                const uint8_t hunterID = Read8(mainRAM, m_addrCold.chosenHunter);
                m_flags.assign(StateFlags::BIT_IS_SAMUS, hunterID == 0x00);
                m_flags.assign(StateFlags::BIT_IS_WEAVEL, hunterID == 0x06);
                m_flags.assign(StateFlags::BIT_IN_ADVENTURE, Read8(mainRAM, m_addrCold.isInAdventure) == 0x02);

                ApplyMphSensitivity(emuInstance->getNDS(), localCfg, m_addrCold.sensitivity, m_addrCold.inGameSensi, true);
            }

            if (isFocused) {
                if (LIKELY(isInGame)) {
                    HandleInGameLogic();
                }
                else {
                    m_flags.clear(StateFlags::BIT_IN_ADVENTURE);
                    m_isAimDisabled = true;
                    if (m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
                        m_flags.clear(StateFlags::BIT_IN_GAME_INIT);
                    }
                    ApplyGameSettingsOnce();
                }

                if (UNLIKELY(shouldBeCursorMode != isCursorMode)) {
                    isCursorMode = shouldBeCursorMode;
                    SetAimBlockBranchless(AIMBLK_CURSOR_MODE, isCursorMode);
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
            m_flags.assign(StateFlags::BIT_LAST_FOCUSED, isFocused);
        }
        m_isRunningHook = false;
    }

    HOT_FUNCTION void MelonPrimeCore::HandleInGameLogic()
    {
        PREFETCH_READ(m_ptrs.isAltForm);

        if (UNLIKELY(IsPressed(IB_MORPH))) {
            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
            emuInstance->getNDS()->ReleaseScreen();
            FrameAdvanceTwice();
            using namespace Consts::UI;
            emuInstance->getNDS()->TouchScreen(MORPH_START.x(), MORPH_START.y());
            FrameAdvanceTwice();
            emuInstance->getNDS()->ReleaseScreen();
            FrameAdvanceTwice();
        }

        if (UNLIKELY(ProcessWeaponSwitch())) {
            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
        }

        const bool weaponCheckDown = IsDown(IB_WEAPON_CHECK);

        if (weaponCheckDown) {
            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
            if (!m_isWeaponCheckActive) {
                m_isWeaponCheckActive = true;
                SetAimBlockBranchless(AIMBLK_CHECK_WEAPON, true);
                emuInstance->getNDS()->ReleaseScreen();
                FrameAdvanceTwice();
            }
            using namespace Consts::UI;
            emuInstance->getNDS()->TouchScreen(WEAPON_CHECK_START.x(), WEAPON_CHECK_START.y());
        }
        else if (UNLIKELY(m_isWeaponCheckActive)) {
            m_isWeaponCheckActive = false;
            emuInstance->getNDS()->ReleaseScreen();
            SetAimBlockBranchless(AIMBLK_CHECK_WEAPON, false);
            FrameAdvanceTwice();
        }

        if (UNLIKELY(m_flags.test(StateFlags::BIT_IN_ADVENTURE))) {
            HandleAdventureMode();
        }

        ProcessMoveInputFast();

        InputSetBranchless(INPUT_B, !IsDown(IB_JUMP));
        InputSetBranchless(INPUT_L, !IsDown(IB_SHOOT));
        InputSetBranchless(INPUT_R, !IsDown(IB_ZOOM));

        HandleMorphBallBoost();

        if (isStylusMode) {
            if (!m_flags.test(StateFlags::BIT_BLOCK_STYLUS)) {
                ProcessAimInputStylus();
            }
        }
        else {
            ProcessAimInputMouse();
            if (!m_flags.test(StateFlags::BIT_LAST_FOCUSED) || !m_isAimDisabled) {
                using namespace Consts::UI;
                emuInstance->getNDS()->TouchScreen(CENTER_RESET.x(), CENTER_RESET.y());
            }
        }
    }

    HOT_FUNCTION void MelonPrimeCore::ProcessMoveInputFast()
    {
        uint32_t curr = m_input.moveIndex;
        uint32_t finalInput;

        if (LIKELY(!m_flags.test(StateFlags::BIT_SNAP_TAP))) {
            finalInput = curr;
        }
        else {
            const uint32_t last = m_snapState & 0xFFu;
            const uint32_t priority = m_snapState >> 8;
            const uint32_t newPress = curr & ~last;
            const uint32_t hConflict = ((curr & 0x3u) == 0x3u) ? 0x3u : 0u;
            const uint32_t vConflict = ((curr & 0xCu) == 0xCu) ? 0xCu : 0u;
            const uint32_t conflict = vConflict | hConflict;
            const uint32_t updateMask = (newPress & conflict) ? ~0u : 0u;
            const uint32_t newPriority = (priority & ~(conflict & updateMask)) | (newPress & conflict & updateMask);
            const uint32_t activePriority = newPriority & curr;
            m_snapState = static_cast<uint16_t>((curr & 0xFFu) | ((activePriority & 0xFFu) << 8));
            finalInput = (curr & ~conflict) | (activePriority & conflict);
        }

        const uint8_t lutResult = MoveLUT[finalInput & 0xF];
        m_inputMaskFast = (m_inputMaskFast & 0xFF0Fu) | (static_cast<uint16_t>(lutResult) & 0x00F0u);
    }

    void MelonPrimeCore::ProcessMoveInput(QBitArray& mask)
    {
        ProcessMoveInputFast();
        mask.setBit(INPUT_UP, (m_inputMaskFast >> INPUT_UP) & 1);
        mask.setBit(INPUT_DOWN, (m_inputMaskFast >> INPUT_DOWN) & 1);
        mask.setBit(INPUT_LEFT, (m_inputMaskFast >> INPUT_LEFT) & 1);
        mask.setBit(INPUT_RIGHT, (m_inputMaskFast >> INPUT_RIGHT) & 1);
    }

    void MelonPrimeCore::ProcessAimInputStylus()
    {
        if (LIKELY(emuInstance->isTouching)) {
            emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
        }
        else {
            emuInstance->getNDS()->ReleaseScreen();
        }
    }

    HOT_FUNCTION void MelonPrimeCore::ProcessAimInputMouse()
    {
        if (m_isAimDisabled) return;

        if (UNLIKELY(m_isLayoutChangePending)) {
            m_isLayoutChangePending = false;
#ifdef _WIN32
            if (m_rawFilter) m_rawFilter->discardDeltas();
#endif
            return;
        }

        if (LIKELY(m_flags.test(StateFlags::BIT_LAST_FOCUSED))) {
            const int deltaX = m_input.mouseX;
            const int deltaY = m_input.mouseY;

            if (deltaX == 0 && deltaY == 0) return;

            float rawScaledX = (deltaX * m_aimSensiFactor);
            float rawScaledY = (deltaY * m_aimCombinedY);

            float adjX = rawScaledX;
            float adjY = rawScaledY;
            ApplyAimAdjustBranchless(adjX, adjY);

            int16_t outX = static_cast<int16_t>(adjX);
            int16_t outY = static_cast<int16_t>(adjY);

            if (outX == 0 && outY == 0) return;

            *m_ptrs.aimX = static_cast<uint16_t>(outX);
            *m_ptrs.aimY = static_cast<uint16_t>(outY);

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

    HOT_FUNCTION bool MelonPrimeCore::HandleMorphBallBoost()
    {
        if (!m_flags.test(StateFlags::BIT_IS_SAMUS)) return false;

        if (IsDown(IB_MORPH_BOOST)) {
            const bool isAltForm = (*m_ptrs.isAltForm) == 0x02;
            m_flags.assign(StateFlags::BIT_IS_ALT_FORM, isAltForm);

            if (isAltForm) {
                const uint8_t boostGaugeValue = *m_ptrs.boostGauge;
                const bool isBoosting = (*m_ptrs.isBoosting) != 0x00;
                const bool isBoostGaugeEnough = boostGaugeValue > 0x0A;

                SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, true);

                if (!IsDown(IB_WEAPON_CHECK)) {
                    emuInstance->getNDS()->ReleaseScreen();
                }

                InputSetBranchless(INPUT_R, !isBoosting && isBoostGaugeEnough);

                if (isBoosting) {
                    SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, false);
                }
                return true;
            }
        }
        else {
            SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, false);
        }
        return false;
    }

    void MelonPrimeCore::HandleAdventureMode()
    {
        const bool isPaused = (*m_ptrs.isMapOrUserActionPaused) == 0x1;
        m_flags.assign(StateFlags::BIT_PAUSED, isPaused);

        if (IsPressed(IB_SCAN_VISOR)) {
            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

            emuInstance->getNDS()->ReleaseScreen();
            FrameAdvanceTwice();
            using namespace Consts::UI;
            emuInstance->getNDS()->TouchScreen(SCAN_VISOR_BUTTON.x(), SCAN_VISOR_BUTTON.y());

            if ((*m_ptrs.isInVisorOrMap) == 0x1) {
                FrameAdvanceTwice();
            }
            else {
                for (int i = 0; i < 30; i++) {
                    UpdateInputState();
                    ProcessMoveInputFast();
                    emuInstance->getNDS()->SetKeyMask(m_inputMaskFast);
                    FrameAdvanceOnce();
                }
            }
            emuInstance->getNDS()->ReleaseScreen();
            FrameAdvanceTwice();
        }

#define TOUCH_IF_PRESSED(BIT, POINT) \
        if (IsPressed(BIT)) { \
            emuInstance->getNDS()->ReleaseScreen(); \
            FrameAdvanceTwice(); \
            emuInstance->getNDS()->TouchScreen(POINT.x(), POINT.y()); \
            FrameAdvanceTwice(); \
        }

        using namespace Consts::UI;
        TOUCH_IF_PRESSED(IB_UI_OK, OK)
            TOUCH_IF_PRESSED(IB_UI_LEFT, LEFT)
            TOUCH_IF_PRESSED(IB_UI_RIGHT, RIGHT)
            TOUCH_IF_PRESSED(IB_UI_YES, YES)
            TOUCH_IF_PRESSED(IB_UI_NO, NO)
#undef TOUCH_IF_PRESSED
    }

    HOT_FUNCTION bool MelonPrimeCore::ProcessWeaponSwitch()
    {
        using namespace WeaponData;

        if (LIKELY(!IsAnyPressed(IB_WEAPON_ANY))) {
            auto* panel = emuInstance->getMainWindow()->panel;
            if (!panel) return false;
            const int wheelDelta = panel->getDelta();
            const bool nextKey = IsPressed(IB_WEAPON_NEXT);
            const bool prevKey = IsPressed(IB_WEAPON_PREV);

            if (!wheelDelta && !nextKey && !prevKey) return false;

            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

            const bool forward = (wheelDelta < 0) || nextKey;
            const uint8_t curID = *m_ptrs.currentWeapon;
            const uint16_t having = *m_ptrs.havingWeapons;
            const uint32_t ammoData = *m_ptrs.weaponAmmo;
            const uint16_t weaponAmmo = static_cast<uint16_t>(ammoData & 0xFFFF);
            const uint16_t missileAmmo = static_cast<uint16_t>(ammoData >> 16);
            const bool isWeavel = m_flags.test(StateFlags::BIT_IS_WEAVEL);

            auto isWeaponAvailable = [&](const Info& info) -> bool {
                if (info.id != 0 && info.id != 2 && !(having & info.mask)) return false;
                if (info.id == 2) return missileAmmo >= 0xA;
                if (info.id != 0 && info.id != 8) {
                    uint8_t req = info.minAmmo;
                    if (info.id == 3 && isWeavel) req = 0x5;
                    return weaponAmmo >= req;
                }
                return true;
                };

            uint16_t available = 0;
            for (size_t i = 0; i < ORDERED_WEAPONS.size(); ++i) {
                if (isWeaponAvailable(ORDERED_WEAPONS[i])) available |= (1u << i);
            }

            if (!available) {
                m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
                return false;
            }

            uint8_t idx = ID_TO_INDEX[curID % 9];
            const size_t count = ORDERED_WEAPONS.size();
            for (size_t n = 0; n < count; ++n) {
                idx = forward ? static_cast<uint8_t>((idx + 1) % count) : static_cast<uint8_t>((idx + count - 1) % count);
                if (available & (1u << idx)) {
                    SwitchWeapon(ORDERED_WEAPONS[idx].id);
                    return true;
                }
            }

            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

        uint32_t hot = 0;
        for (size_t i = 0; i < 9; ++i) {
            if (IsPressed(BIT_MAP[i])) hot |= (1u << i);
        }

#if defined(_MSC_VER) && !defined(__clang__)
        unsigned long firstSetUL;
        _BitScanForward(&firstSetUL, hot);
        const int firstSet = static_cast<int>(firstSetUL);
#else
        const int firstSet = __builtin_ctz(hot);
#endif

        if (UNLIKELY(firstSet == 8)) {
            const uint8_t loaded = *m_ptrs.loadedSpecialWeapon;
            if (loaded == 0xFF) {
                emuInstance->osdAddMessage(0, "Have not Special Weapon yet!");
                m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
                return false;
            }
            SwitchWeapon(loaded);
            return true;
        }

        const uint8_t weaponID = BIT_WEAPON_ID[firstSet];
        const uint16_t having = *m_ptrs.havingWeapons;
        const uint32_t ammoData = *m_ptrs.weaponAmmo;
        const uint16_t weaponAmmo = static_cast<uint16_t>(ammoData & 0xFFFF);
        const uint16_t missileAmmo = static_cast<uint16_t>(ammoData >> 16);

        const Info* info = nullptr;
        for (const auto& w : ORDERED_WEAPONS) {
            if (w.id == weaponID) { info = &w; break; }
        }

        if (!info) {
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        const bool owned = (weaponID == 0 || weaponID == 2) || ((having & info->mask) != 0);

        if (!owned) {
            emuInstance->osdAddMessage(0, "Have not %s yet!", kWeaponNames[weaponID].data());
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        bool hasAmmo = true;
        if (weaponID == 2) {
            hasAmmo = (missileAmmo >= 0xA);
        }
        else if (weaponID != 0 && weaponID != 8) {
            uint8_t required = info->minAmmo;
            if (weaponID == 3 && m_flags.test(StateFlags::BIT_IS_WEAVEL)) required = 0x5;
            hasAmmo = (weaponAmmo >= required);
        }

        if (!hasAmmo) {
            emuInstance->osdAddMessage(0, "Not enough Ammo for %s!", kWeaponNames[weaponID].data());
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        SwitchWeapon(weaponID);
        return true;
    }

    void MelonPrimeCore::SwitchWeapon(int weaponIndex)
    {
        if ((*m_ptrs.selectedWeapon) == weaponIndex) return;

        if (m_flags.test(StateFlags::BIT_IN_ADVENTURE)) {
            if (m_flags.test(StateFlags::BIT_PAUSED)) {
                emuInstance->osdAddMessage(0, "You can't switch weapon now!");
                return;
            }
            if ((*m_ptrs.isInVisorOrMap) == 0x1) return;
        }

        uint8_t currentJumpFlags = *m_ptrs.jumpFlag;
        bool isTransforming = currentJumpFlags & 0x10;
        uint8_t jumpFlag = currentJumpFlags & 0x0F;
        bool isRestoreNeeded = false;

        const bool isAltForm = (*m_ptrs.isAltForm) == 0x02;
        m_flags.assign(StateFlags::BIT_IS_ALT_FORM, isAltForm);

        if (!isTransforming && jumpFlag == 0 && !isAltForm) {
            *m_ptrs.jumpFlag = (currentJumpFlags & 0xF0) | 0x01;
            isRestoreNeeded = true;
        }

        *m_ptrs.weaponChange = ((*m_ptrs.weaponChange) & 0xF0) | 0x0B;
        *m_ptrs.selectedWeapon = weaponIndex;

        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();

        if (!isStylusMode) {
            using namespace Consts::UI;
            emuInstance->getNDS()->TouchScreen(CENTER_RESET.x(), CENTER_RESET.y());
        }
        else if (emuInstance->isTouching) {
            emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
        }
        FrameAdvanceTwice();

        if (isRestoreNeeded) {
            currentJumpFlags = *m_ptrs.jumpFlag;
            *m_ptrs.jumpFlag = (currentJumpFlags & 0xF0) | jumpFlag;
        }
    }

    COLD_FUNCTION void MelonPrimeCore::ApplyGameSettingsOnce()
    {
        InputSetBranchless(INPUT_L, !IsPressed(IB_UI_LEFT));
        InputSetBranchless(INPUT_R, !IsPressed(IB_UI_RIGHT));

        if (!(m_appliedFlags & APPLIED_HEADPHONE)) {
            ApplyHeadphoneOnce(emuInstance->getNDS(), localCfg, m_addrCold.operationAndSound, m_appliedFlags, APPLIED_HEADPHONE);
        }

        ApplyMphSensitivity(emuInstance->getNDS(), localCfg, m_addrCold.sensitivity, m_addrCold.inGameSensi, m_flags.test(StateFlags::BIT_IN_GAME_INIT));

        if (!(m_appliedFlags & APPLIED_UNLOCK)) {
            ApplyUnlockHuntersMaps(emuInstance->getNDS(), localCfg, m_appliedFlags, APPLIED_UNLOCK,
                m_addrCold.unlockMapsHunters, m_addrCold.unlockMapsHunters2, m_addrCold.unlockMapsHunters3,
                m_addrCold.unlockMapsHunters4, m_addrCold.unlockMapsHunters5);
        }

        UseDsName(emuInstance->getNDS(), localCfg, m_addrCold.dsNameFlagAndMicVolume);
        ApplySelectedHunterStrict(emuInstance->getNDS(), localCfg, m_addrCold.mainHunter);
        ApplyLicenseColorStrict(emuInstance->getNDS(), localCfg, m_addrCold.rankColor);

        if (!(m_appliedFlags & APPLIED_VOL_SFX)) {
            ApplySfxVolumeOnce(emuInstance->getNDS(), localCfg, m_addrCold.volSfx8Bit, m_appliedFlags, APPLIED_VOL_SFX);
        }
        if (!(m_appliedFlags & APPLIED_VOL_MUSIC)) {
            ApplyMusicVolumeOnce(emuInstance->getNDS(), localCfg, m_addrCold.volMusic8Bit, m_appliedFlags, APPLIED_VOL_MUSIC);
        }
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
            if (emuInstance->getNDS()->GPU.GetRenderer().NeedsShaderCompile()) {
                int currentShader, shadersCount;
                emuInstance->getNDS()->GPU.GetRenderer().ShaderCompileStep(currentShader, shadersCount);
            }
            else {
                emuInstance->getNDS()->RunFrame();
            }
            if (emuInstance->usesOpenGL()) emuInstance->drawScreen();
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

    bool MelonPrimeCore::ApplyLicenseColorStrict(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr)
    {
        if (!nds || !localCfg.GetBool("Metroid.HunterLicense.Color.Apply")) return false;
        int sel = localCfg.GetInt("Metroid.HunterLicense.Color.Selected");
        if (sel < 0 || sel > 2) return false;
        constexpr std::array<uint8_t, 3> kColorBits = { 0x00, 0x40, 0x80 };
        uint8_t oldVal = nds->ARM9Read8(addr);
        uint8_t newVal = (oldVal & 0x3F) | kColorBits[sel];
        if (newVal == oldVal) return false;
        nds->ARM9Write8(addr, newVal);
        return true;
    }

    bool MelonPrimeCore::ApplySelectedHunterStrict(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr)
    {
        if (!nds || !localCfg.GetBool("Metroid.HunterLicense.Hunter.Apply")) return false;
        constexpr std::array<uint8_t, 7> kHunterBits = { 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30 };
        int sel = localCfg.GetInt("Metroid.HunterLicense.Hunter.Selected");
        sel = std::clamp(sel, 0, 6);
        uint8_t oldVal = nds->ARM9Read8(addr);
        uint8_t newVal = (oldVal & ~0x78) | (kHunterBits[sel] & 0x78);
        if (newVal == oldVal) return false;
        nds->ARM9Write8(addr, newVal);
        return true;
    }

    bool MelonPrimeCore::UseDsName(melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr)
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
        uint16_t sensiVal = SensiNumToSensiVal(mphSensi);
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

    melonDS::u32 MelonPrimeCore::CalculatePlayerAddress(melonDS::u32 base, melonDS::u8 pos, int32_t inc)
    {
        if (pos == 0) return base;
        int64_t result = static_cast<int64_t>(base) + (static_cast<int64_t>(pos) * inc);
        if (result < 0 || result > UINT32_MAX) return base;
        return static_cast<melonDS::u32>(result);
    }

} // namespace MelonPrime
