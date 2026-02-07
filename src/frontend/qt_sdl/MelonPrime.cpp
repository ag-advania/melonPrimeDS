#include "MelonPrimeInternal.h"
#include "MelonPrimeGameSettings.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "NDS.h"
#include "GPU.h"
#include "main.h"
#include "Screen.h"
#include "Platform.h"
#include "MelonPrimeDef.h"

#include <cmath>
#include <algorithm>
#include <QCoreApplication>
#include <QCursor>

#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawHotkeyVkBinding.h"

namespace MelonPrime {
    void FilterDeleter::operator()(RawInputWinFilter* ptr) {
        if (ptr) RawInputWinFilter::Release();
    }
}
#endif

namespace MelonPrime {

    // --- FIXED: Removed FORCE_INLINE ---
    bool MelonPrimeCore::IsJoyDown(int id) const {
#ifdef _WIN32
        return emuInstance->joyHotkeyMask.testBit(id);
#else
        return false;
#endif
    }

    bool MelonPrimeCore::IsJoyPressed(int id) const {
#ifdef _WIN32
        return emuInstance->joyHotkeyPress.testBit(id);
#else
        return false;
#endif
    }
    // -----------------------------------

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
            BindMetroidHotkeysFromConfig(m_rawFilter.get(), emuInstance->getInstanceID());
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

        m_rawFilter->setJoy2KeySupport(enable);

        if (!enable) {
            if (s_isInstalled) {
                app->removeNativeEventFilter(m_rawFilter.get());
                s_isInstalled = false;
            }
        }
        else {
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
            BindMetroidHotkeysFromConfig(m_rawFilter.get(), emuInstance->getInstanceID());
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
                m_addrHot.isAltForm = MelonPrimeGameSettings::CalculatePlayerAddress(m_addrCold.baseIsAltForm, m_playerPosition, PLAYER_ADDR_INC);
                m_addrHot.loadedSpecialWeapon = MelonPrimeGameSettings::CalculatePlayerAddress(m_addrCold.baseLoadedSpecialWeapon, m_playerPosition, PLAYER_ADDR_INC);
                m_addrHot.weaponChange = MelonPrimeGameSettings::CalculatePlayerAddress(m_addrCold.baseWeaponChange, m_playerPosition, PLAYER_ADDR_INC);
                m_addrHot.selectedWeapon = MelonPrimeGameSettings::CalculatePlayerAddress(m_addrCold.baseSelectedWeapon, m_playerPosition, PLAYER_ADDR_INC);
                m_addrHot.jumpFlag = MelonPrimeGameSettings::CalculatePlayerAddress(m_addrCold.baseJumpFlag, m_playerPosition, PLAYER_ADDR_INC);
                m_addrHot.aimX = MelonPrimeGameSettings::CalculatePlayerAddress(m_addrCold.baseAimX, m_playerPosition, AIM_ADDR_INC);
                m_addrHot.aimY = MelonPrimeGameSettings::CalculatePlayerAddress(m_addrCold.baseAimY, m_playerPosition, AIM_ADDR_INC);

                m_addrHot.currentWeapon = m_addrHot.selectedWeapon - 0x1;
                m_addrHot.havingWeapons = m_addrHot.selectedWeapon + 0x3;
                m_addrHot.weaponAmmo = m_addrHot.selectedWeapon - 0x383;
                m_addrHot.boostGauge = m_addrHot.loadedSpecialWeapon - 0x12;
                m_addrHot.isBoosting = m_addrHot.loadedSpecialWeapon - 0x10;

                m_addrCold.chosenHunter = MelonPrimeGameSettings::CalculatePlayerAddress(m_addrCold.baseChosenHunter, m_playerPosition, 0x01);
                m_addrCold.inGameSensi = MelonPrimeGameSettings::CalculatePlayerAddress(m_addrCold.baseInGameSensi, m_playerPosition, 0x04);

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

                MelonPrimeGameSettings::ApplyMphSensitivity(emuInstance->getNDS(), localCfg, m_addrCold.sensitivity, m_addrCold.inGameSensi, true);
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

} // namespace MelonPrime

