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

    // =========================================================================
    // Config reload helper â€” batches all config reads into one function
    // to reduce call overhead and improve branch prediction.
    // =========================================================================
    void MelonPrimeCore::ReloadConfigFlags()
    {
        m_flags.assign(StateFlags::BIT_JOY2KEY, localCfg.GetBool(CfgKey::Joy2Key));
        m_flags.assign(StateFlags::BIT_SNAP_TAP, localCfg.GetBool(CfgKey::SnapTap));
        m_flags.assign(StateFlags::BIT_STYLUS_MODE, localCfg.GetBool(CfgKey::StylusMode));
        isStylusMode = m_flags.test(StateFlags::BIT_STYLUS_MODE);
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

    // OPT-O/F: Recompute all fixed-point aim parameters.
    //
    // Called only on config change (cold path). Derives:
    //   - Q14 scale factors for X/Y axes
    //   - Q14 AimAdjust deadzone + snap-to-1 thresholds
    //   - Integer delta skip thresholds (OPT-F)
    //
    // When AimAdjust is disabled (≤ 0):
    //   adjThresh = 0, snapThresh = 0 → both Q14 comparisons always fail
    //   → hot path reduces to IMUL + SAR only (no AimAdjust branches).
    void MelonPrimeCore::RecalcAimFixedPoint()
    {
        // --- Q14 scale factors ---
        m_aimFixedScaleX = static_cast<int32_t>(m_aimSensiFactor * AIM_ONE_FP + 0.5f);
        m_aimFixedScaleY = static_cast<int32_t>(m_aimCombinedY * AIM_ONE_FP + 0.5f);

        // --- Q14 AimAdjust thresholds ---
        if (m_aimAdjust > 0.0f) {
            m_aimFixedAdjust = static_cast<int64_t>(m_aimAdjust * AIM_ONE_FP + 0.5f);
            m_aimFixedSnapThresh = AIM_ONE_FP;
        } else {
            m_aimFixedAdjust = 0;
            m_aimFixedSnapThresh = 0;
        }

        // --- Delta skip thresholds (OPT-F) ---
        //   When adjust ON:  skip when |delta * scale| < adjust → |delta| < adjust/scale
        //   When adjust OFF: skip when |delta * scale| < 1.0   → |delta| < 1.0/scale
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
        // OPT-N: packed reset to 0 (BIT_LAYOUT_PENDING was dead).
        //   m_isLayoutChangePending is set explicitly below.
        m_flags.packed = 0;
        m_isLayoutChangePending = true;
        // OPT-D: m_isInGame removed — BIT_IN_GAME is cleared by packed reset above
        m_appliedFlags = 0;
        m_isWeaponCheckActive = false;

        ReloadConfigFlags();
        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));
        InputReset();
    }

    void MelonPrimeCore::OnEmuStop()
    {
        m_flags.clear(StateFlags::BIT_IN_GAME);
        // OPT-D: m_isInGame removed — unified with BIT_IN_GAME
    }

    void MelonPrimeCore::OnEmuPause() {}

    void MelonPrimeCore::OnEmuUnpause()
    {
        ReloadConfigFlags();
        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));

        m_appliedFlags &= ~APPLIED_ALL_ONCE;
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

    // =========================================================================
    // RunFrameHook â€” THE hot path, called every frame (~60 Hz)
    //
    // Key optimizations vs. original:
    //   - Early return for re-entrant case is tighter (no redundant flag checks)
    //   - Player-position offset calculation uses multiply+add (no repeated add)
    //   - Pointer resolution block is sequential for prefetch friendliness
    //   - Cursor mode transition uses XOR change-detect
    // =========================================================================
    HOT_FUNCTION void MelonPrimeCore::RunFrameHook()
    {
        melonDS::u8* const mainRAM = emuInstance->getNDS()->MainRAM;

        // --- Re-entrant path (called from within FrameAdvance) ---
        // OPT: Fresh Poll + UpdateInputState on every re-entrant frame.
        //      Without this, weapon-switch / morph-toggle trigger 2-4 NDS frames
        //      that all use the SAME stale mouse delta from the parent frame's
        //      initial Poll(). With 8000Hz mouse, ~16 events arrive per sub-frame
        //      that would otherwise be ignored. Cost: ~1 Poll per sub-frame (trivial).
        if (UNLIKELY(m_isRunningHook)) {
#ifdef _WIN32
            // OPT-M: Single pointer load — subsequent uses via register.
            auto* const rawFilter = m_rawFilter.get();
            if (rawFilter) {
                rawFilter->Poll();
            }
#endif
            UpdateInputState();

            ProcessMoveInputFast();

            // OPT-E: Batched button mask update (mirrors main path).
            //   Old: 3 sequential InputSetBranchless calls → 3 RMW dependency chain.
            //   New: Single RMW with parallel bit extraction.
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

        // --- Synchronous RawInput polling (lowest latency) ---
#ifdef _WIN32
        // OPT-M: Single pointer load for the entire main path.
        auto* const rawFilter = m_rawFilter.get();
        if (rawFilter) {
            rawFilter->Poll();
        }
#endif

        UpdateInputState();
        InputReset();
        m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);

        HandleGlobalHotkeys();

        // --- ROM detection (cold path, runs once) ---
        if (UNLIKELY(!m_flags.test(StateFlags::BIT_ROM_DETECTED))) {
            DetectRomAndSetAddresses();
        }

        if (LIKELY(m_flags.test(StateFlags::BIT_ROM_DETECTED))) {
            // OPT-L: m_ptrs.inGame is in the hot cache line (HotPointers CL1+),
            //   avoiding a load from cold m_addrHot every frame.
            const bool isInGame = (*m_ptrs.inGame) == 0x0001;
            m_flags.assign(StateFlags::BIT_IN_GAME, isInGame);
            // OPT-D: m_isInGame removed — IsInGame() now reads BIT_IN_GAME directly

            // --- In-game init (runs once per game-join) ---
            if (isInGame && !m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
                m_flags.set(StateFlags::BIT_IN_GAME_INIT);
                m_playerPosition = Read8(mainRAM, m_currentRom.playerPos);

                // Compute all player-relative offsets in one block
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

                // Resolve all RAM pointers sequentially (prefetch-friendly)
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
            }

            // --- Per-frame focused logic ---
            if (isFocused) {
                if (LIKELY(isInGame)) {
                    // OPT-G: Clear not-in-game aim block on transition back to gameplay.
                    //   Only fires on the single frame of menu→game transition.
                    if (UNLIKELY(m_aimBlockBits & AIMBLK_NOT_IN_GAME)) {
                        SetAimBlockBranchless(AIMBLK_NOT_IN_GAME, false);
                    }
                    HandleInGameLogic();
                }
                else {
                    m_flags.clear(StateFlags::BIT_IN_ADVENTURE);
                    // OPT-G: Use unified aimBlockBits instead of standalone bool
                    SetAimBlockBranchless(AIMBLK_NOT_IN_GAME, true);
                    if (m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
                        m_flags.clear(StateFlags::BIT_IN_GAME_INIT);
                    }
                    ApplyGameSettingsOnce();
                }

                // Cursor mode transition â€” only fires on actual change
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
            // OPT-K: Skip store when focus state unchanged (99.99% of frames).
            //   During gameplay, isFocused is persistently true; this guard avoids
            //   a read-modify-write to m_flags.packed on every frame.
            if (UNLIKELY(m_flags.test(StateFlags::BIT_LAST_FOCUSED) != isFocused)) {
                m_flags.assign(StateFlags::BIT_LAST_FOCUSED, isFocused);
            }
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
