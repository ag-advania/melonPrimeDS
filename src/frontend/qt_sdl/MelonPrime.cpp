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
#include "MelonPrimeGameRomAddrTable.h"

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

    MelonPrimeCore::MelonPrimeCore(EmuInstance* instance)
        : emuInstance(instance)
        , localCfg(instance->getLocalConfig())
        , globalCfg(instance->getGlobalConfig())
    {
        m_flags.packed = 0;
    }

    MelonPrimeCore::~MelonPrimeCore() = default;

    void MelonPrimeCore::ReloadConfigFlags()
    {
        m_flags.assign(StateFlags::BIT_JOY2KEY, localCfg.GetBool(CfgKey::Joy2Key));
        m_flags.assign(StateFlags::BIT_SNAP_TAP, localCfg.GetBool(CfgKey::SnapTap));
        m_flags.assign(StateFlags::BIT_STYLUS_MODE, localCfg.GetBool(CfgKey::StylusMode));
        isStylusMode = m_flags.test(StateFlags::BIT_STYLUS_MODE);

        m_disableMphAimSmoothing = localCfg.GetBool(CfgKey::DisableMphAimSmoothing);
    }

    void MelonPrimeCore::Initialize()
    {
        ReloadConfigFlags();
        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);

#ifdef _WIN32
        SetupRawInput();
#endif
    }

    void MelonPrimeCore::SetFrameAdvanceFunc(std::function<void()> func)
    {
        m_frameAdvanceFunc = std::move(func);
        m_fnAdvance = m_frameAdvanceFunc
            ? &MelonPrimeCore::FrameAdvanceCustom
            : &MelonPrimeCore::FrameAdvanceDefault;
    }

    void MelonPrimeCore::SetupRawInput()
    {
#ifdef _WIN32
        if (m_rawFilter) return;

        if (auto* mw = emuInstance->getMainWindow()) {
            m_cachedHwnd = reinterpret_cast<HWND>(mw->winId());
        }
        m_rawFilter.reset(
            RawInputWinFilter::Acquire(m_flags.test(StateFlags::BIT_JOY2KEY), m_cachedHwnd));

        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));
        BindMetroidHotkeysFromConfig(m_rawFilter.get(), emuInstance->getInstanceID());
#endif
    }

    void MelonPrimeCore::ApplyJoy2KeySupportAndQtFilter(bool enable, bool doReset)
    {
#ifdef _WIN32
        if (!m_rawFilter) return;
        QCoreApplication* app = QCoreApplication::instance();
        if (!app) return;

        if (auto* mw = emuInstance->getMainWindow()) {
            m_cachedHwnd = reinterpret_cast<HWND>(mw->winId());
        }

        m_rawFilter->setRawInputTarget(static_cast<HWND>(m_cachedHwnd));
        m_flags.assign(StateFlags::BIT_JOY2KEY, enable);

        static bool s_isInstalled = false;
        m_rawFilter->setJoy2KeySupport(enable);

        if (enable != s_isInstalled) {
            if (enable) {
                app->installNativeEventFilter(m_rawFilter.get());
            }
            else {
                app->removeNativeEventFilter(m_rawFilter.get());
            }
            s_isInstalled = enable;
        }

        if (doReset) {
            m_rawFilter->resetAllKeys();
            m_rawFilter->resetMouseButtons();
            m_rawFilter->resetHotkeyEdges();
        }
#endif
    }

    void MelonPrimeCore::RecalcAimSensitivityCache(Config::Table& cfg) {
        const float sens = static_cast<float>(cfg.GetInt(CfgKey::AimSens));
        const float yScale = static_cast<float>(cfg.GetDouble(CfgKey::AimYScale));
        m_aimSensiFactor = sens * 0.01f;
        m_aimCombinedY = m_aimSensiFactor * yScale;
        RecalcAimFixedPoint();
    }

    void MelonPrimeCore::ApplyAimAdjustSetting(Config::Table& cfg) {
        const double v = cfg.GetDouble(CfgKey::AimAdjust);
        m_aimAdjust = static_cast<float>(std::max(0.0, std::isnan(v) ? 0.0 : v));
        RecalcAimFixedPoint();
    }

    void MelonPrimeCore::RecalcAimFixedPoint()
    {
        m_aimFixedScaleX = static_cast<int32_t>(m_aimSensiFactor * AIM_ONE_FP + 0.5f);
        m_aimFixedScaleY = static_cast<int32_t>(m_aimCombinedY * AIM_ONE_FP + 0.5f);

        if (m_aimAdjust > 0.0f) {
            m_aimFixedAdjust = static_cast<int64_t>(m_aimAdjust * AIM_ONE_FP + 0.5f);
            m_aimFixedSnapThresh = AIM_ONE_FP;
        }
        else {
            m_aimFixedAdjust = 0;
            m_aimFixedSnapThresh = 0;
        }

        const float effMin = (m_aimAdjust > 0.0f) ? m_aimAdjust : 1.0f;
        m_aimMinDeltaX = (m_aimSensiFactor > 0.0f)
            ? static_cast<int32_t>(std::ceil(effMin / m_aimSensiFactor))
            : 1;
        m_aimMinDeltaY = (m_aimCombinedY > 0.0f)
            ? static_cast<int32_t>(std::ceil(effMin / m_aimCombinedY))
            : 1;
        if (m_aimMinDeltaX < 1) m_aimMinDeltaX = 1;
        if (m_aimMinDeltaY < 1) m_aimMinDeltaY = 1;
    }

    void MelonPrimeCore::OnEmuStart()
    {
        m_flags.packed = 0;
        m_isLayoutChangePending = true;
        m_appliedFlags = 0;
        m_isWeaponCheckActive = false;

        ReloadConfigFlags();
        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));
        InputReset();
    }

    void MelonPrimeCore::OnEmuStop()
    {
        m_flags.clear(StateFlags::BIT_IN_GAME);
    }

    void MelonPrimeCore::OnEmuPause() {}

    void MelonPrimeCore::OnEmuUnpause()
    {
        // ApplyJoy2KeySupportAndQtFilter は doReset=true (デフォルト) で呼ばれるため、
        // 内部で resetAllKeys + resetMouseButtons + resetHotkeyEdges が実行される。
        // ポーズ中に失われた key-up イベントの stale ビットはここでクリアされる。
        ReloadConfigFlags();
        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));

        m_appliedFlags &= ~APPLIED_ALL_ONCE;
        m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);

        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);

#ifdef _WIN32
        if (m_rawFilter) {
            // ホットキーの VK バインドを設定から再読込。
            // バインド変更後のエッジ状態を再同期するため resetHotkeyEdges が必要。
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
        const bool up = emuInstance->hotkeyReleased(HK_MetroidIngameSensiUp);
        const bool down = emuInstance->hotkeyReleased(HK_MetroidIngameSensiDown);
        if (LIKELY(!up && !down)) return;

        const int change = up ? 1 : -1;
        const int cur = localCfg.GetInt(CfgKey::AimSens);
        const int next = cur + change;

        if (next < 1) {
            emuInstance->osdAddMessage(0, "AimSensi cannot be decreased below 1");
        }
        else if (next != cur) {
            localCfg.SetInt(CfgKey::AimSens, next);
            Config::Save();
            RecalcAimSensitivityCache(localCfg);
            emuInstance->osdAddMessage(0, "AimSensi Updated: %d->%d", cur, next);
        }
    }

    HOT_FUNCTION void MelonPrimeCore::RunFrameHook()
    {
        melonDS::u8* const mainRAM = emuInstance->getNDS()->MainRAM;

        if (UNLIKELY(m_isRunningHook)) {
#ifdef _WIN32
            auto* const rawFilter = m_rawFilter.get();
            if (rawFilter) {
                rawFilter->Poll();
            }
#endif
            UpdateInputState();

            ProcessMoveInputFast();

            {
                constexpr uint16_t kModBits = (1u << INPUT_B) | (1u << INPUT_L) | (1u << INPUT_R);
                const uint64_t nd = ~m_input.down;
                const uint16_t bBit = static_cast<uint16_t>(((nd >> 0) & 1u) << INPUT_B);
                const uint16_t lBit = static_cast<uint16_t>(((nd >> 1) & 1u) << INPUT_L);
                const uint16_t rBit = static_cast<uint16_t>(((nd >> 2) & 1u) << INPUT_R);
                m_inputMaskFast = (m_inputMaskFast & ~kModBits) | bBit | lBit | rBit;
            }

            if (isStylusMode) {
                if (emuInstance->isTouching && !m_flags.test(StateFlags::BIT_BLOCK_STYLUS)) {
                    emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
                }
            }
            else {
                ProcessAimInputMouse();
            }
            return;
        }

        m_isRunningHook = true;

#ifdef _WIN32
        auto* const rawFilter = m_rawFilter.get();
        if (rawFilter) {
            rawFilter->Poll();
        }
#endif

        UpdateInputState();
        InputReset();
        m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);

        HandleGlobalHotkeys();

        if (UNLIKELY(!m_flags.test(StateFlags::BIT_ROM_DETECTED))) {
            DetectRomAndSetAddresses();
        }

        if (LIKELY(m_flags.test(StateFlags::BIT_ROM_DETECTED))) {
            const bool isInGame = (*m_ptrs.inGame) == 0x0001;
            m_flags.assign(StateFlags::BIT_IN_GAME, isInGame);

            if (isInGame && !m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
                HandleGameJoinInit(mainRAM);
            }

            if (isFocused) {
                if (LIKELY(isInGame)) {
                    if (UNLIKELY(m_aimBlockBits & AIMBLK_NOT_IN_GAME)) {
                        SetAimBlockBranchless(AIMBLK_NOT_IN_GAME, false);
                    }
                    HandleInGameLogic();
                }
                else {
                    m_flags.clear(StateFlags::BIT_IN_ADVENTURE);
                    SetAimBlockBranchless(AIMBLK_NOT_IN_GAME, true);
                    if (m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
                        m_flags.clear(StateFlags::BIT_IN_GAME_INIT);
                    }
                    ApplyGameSettingsOnce();
                }

                const bool isAdventure = m_flags.test(StateFlags::BIT_IN_ADVENTURE);
                const bool isPaused = m_flags.test(StateFlags::BIT_PAUSED);
                const bool shouldBeCursorMode = !isInGame || (isAdventure && isPaused);

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

            // [FIX-3] フォーカス遷移時に入力状態と raw input 層を安全にリセット。
            // m_input のクリアは [FIX-2] (UpdateInputState) と多重防御の関係にある:
            //   FIX-2: 再入パスの UpdateInputState 内で毎フレーム即座にクリア
            //   FIX-3: メインパスのフレーム末尾で遷移を検出し、raw input 層含め包括クリア
            // → 関連: [FIX-1] HiddenWndProc の WM_INPUT 遮断（根本原因の修正）
            if (UNLIKELY(m_flags.test(StateFlags::BIT_LAST_FOCUSED) != isFocused)) {
                m_flags.assign(StateFlags::BIT_LAST_FOCUSED, isFocused);
                if (!isFocused) {
                    m_input.down = 0;
                    m_input.press = 0;
                    m_input.moveIndex = 0;
#ifdef _WIN32
                    if (m_rawFilter) {
                        m_rawFilter->resetAllKeys();
                        m_rawFilter->resetMouseButtons();
                    }
#endif
                }
            }
        }
        m_isRunningHook = false;
    }

    // =========================================================================
    // OPT-W: HandleGameJoinInit — outlined from RunFrameHook
    //
    // This block executes once per game-join (every ~tens of seconds).
    // Inlining ~50 lines of address calculation + pointer resolution into
    // the HOT_FUNCTION RunFrameHook inflated its icache footprint by
    // ~300-400 bytes, degrading register allocation for the hot path.
    //
    // COLD_FUNCTION ensures the compiler:
    //   1. Places this code in a separate text section (.text.unlikely)
    //   2. Doesn't pollute RunFrameHook's register allocator with cold locals
    //   3. Doesn't inline it back (NOINLINE implied by cold attribute)
    // =========================================================================
    COLD_FUNCTION void MelonPrimeCore::HandleGameJoinInit(melonDS::u8* mainRAM)
    {
        m_flags.set(StateFlags::BIT_IN_GAME_INIT);
        m_playerPosition = Read8(mainRAM, m_currentRom.playerPos);

        const uint32_t offP = static_cast<uint32_t>(m_playerPosition) * Consts::PLAYER_ADDR_INC;
        const uint32_t offA = static_cast<uint32_t>(m_playerPosition) * Consts::AIM_ADDR_INC;

        m_addrHot.isAltForm = m_currentRom.baseIsAltForm + offP;
        m_addrHot.loadedSpecialWeapon = m_currentRom.baseLoadedSpecialWeapon + offP;
        m_addrHot.weaponChange = m_currentRom.baseWeaponChange + offP;
        m_addrHot.selectedWeapon = m_currentRom.baseSelectedWeapon + offP;
        m_addrHot.jumpFlag = m_currentRom.baseJumpFlag + offP;
        m_addrHot.currentWeapon = m_currentRom.baseCurrentWeapon + offP;
        m_addrHot.havingWeapons = m_currentRom.baseHavingWeapons + offP;
        m_addrHot.weaponAmmo = m_currentRom.baseWeaponAmmo + offP;
        m_addrHot.boostGauge = m_currentRom.boostGauge + offP;
        m_addrHot.isBoosting = m_currentRom.isBoosting + offP;
        m_addrHot.isInVisorOrMap = m_currentRom.isInVisorOrMap + offP;

        m_addrHot.aimX = m_currentRom.baseAimX + offA;
        m_addrHot.aimY = m_currentRom.baseAimY + offA;

        m_addrHot.chosenHunter = m_currentRom.baseChosenHunter + m_playerPosition * 0x01u;
        m_addrHot.inGameSensi = m_currentRom.baseInGameSensi + m_playerPosition * 0x04u;

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

        const uint8_t hunterID = Read8(mainRAM, m_addrHot.chosenHunter);
        m_flags.assign(StateFlags::BIT_IS_SAMUS, hunterID == 0x00);
        m_flags.assign(StateFlags::BIT_IS_WEAVEL, hunterID == 0x06);
        m_flags.assign(StateFlags::BIT_IN_ADVENTURE, Read8(mainRAM, m_currentRom.isInAdventure) == 0x02);

        MelonPrimeGameSettings::ApplyMphSensitivity(
            emuInstance->getNDS(), localCfg, m_currentRom.sensitivity, m_addrHot.inGameSensi, true);

        MelonPrimeGameSettings::ApplyAimSmoothingPatch(
            emuInstance->getNDS(), m_currentRom, m_disableMphAimSmoothing);
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

    void MelonPrimeCore::FrameAdvanceCustom() { m_frameAdvanceFunc(); }

    void MelonPrimeCore::FrameAdvanceDefault()
    {
        emuInstance->inputProcess();
        if (emuInstance->usesOpenGL()) emuInstance->makeCurrentGL();

        auto& renderer = emuInstance->getNDS()->GPU.GetRenderer();
        if (renderer.NeedsShaderCompile()) {
            int cur, total;
            renderer.ShaderCompileStep(cur, total);
        }
        else {
            emuInstance->getNDS()->RunFrame();
        }

        if (emuInstance->usesOpenGL()) emuInstance->drawScreen();
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
        const QRect r = panel->geometry();
        return panel->mapToGlobal(QPoint(r.width() / 2, r.height() / 2));
    }

} // namespace MelonPrime
