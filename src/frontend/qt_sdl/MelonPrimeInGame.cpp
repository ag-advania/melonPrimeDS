#include "MelonPrimeInternal.h"
#include "MelonPrimeGameSettings.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeGameRomAddrTable.h"

namespace MelonPrime {

    // =========================================================================
    // HandleInGameLogic — per-frame in-game update
    //
    // Ordering is deliberate:
    //   1. Morph ball toggle (edge-triggered, rare)
    //   2. Weapon switch (edge-triggered, rare)
    //   3. Weapon check hold (continuous)
    //   4. Adventure mode (conditional)
    //   5. Movement (every frame)
    //   6. Button mapping (every frame)
    //   7. Morph ball boost (conditional)
    //   8. Aim input (every frame)
    // =========================================================================
    HOT_FUNCTION void MelonPrimeCore::HandleInGameLogic()
    {
        PREFETCH_READ(m_ptrs.isAltForm);

        // --- Morph ball toggle (edge-triggered, cold path) ---
        // 【最適化】モーフボールは頻度が低いため、UNLIKELYヒントで分岐予測を最適化
        if (UNLIKELY(IsPressed(IB_MORPH))) {
            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
            auto* nds = emuInstance->getNDS();
            nds->ReleaseScreen();
            FrameAdvanceTwice();
            using namespace Consts::UI;
            nds->TouchScreen(MORPH_START.x(), MORPH_START.y());
            FrameAdvanceTwice();
            nds->ReleaseScreen();
            FrameAdvanceTwice();
        }

        // --- Weapon switch (edge-triggered) ---
        // Mouse Wheelによる変更も含まれるため、キー入力有無に関わらず呼び出す必要があるが、
        // 関数内での早期リターンとインライン展開に期待する。
        if (UNLIKELY(ProcessWeaponSwitch())) {
            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
        }

        // --- Adventure mode (conditional) ---
        // 【最適化】Adventureモードかつ関連キーが押されている場合のみ重い処理を呼び出す。
        if (UNLIKELY(m_flags.test(StateFlags::BIT_IN_ADVENTURE))) {
            // ポーズ状態の更新は毎フレーム必要だが、メモリ読み取りだけなのでインライン化
            const bool isPaused = (*m_ptrs.isMapOrUserActionPaused) == 0x1;
            m_flags.assign(StateFlags::BIT_PAUSED, isPaused);

            // スキャンバイザーまたはUIボタンが押されている場合のみ関数呼び出し
            if (IsAnyPressed(IB_SCAN_VISOR | IB_UI_ANY)) {
                HandleAdventureMode();
            }
        }

        // --- Weapon check hold ---
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

        // --- Movement + buttons (every frame) ---
        ProcessMoveInputFast();

        InputSetBranchless(INPUT_B, !IsDown(IB_JUMP));
        InputSetBranchless(INPUT_L, !IsDown(IB_SHOOT));
        InputSetBranchless(INPUT_R, !IsDown(IB_ZOOM));

        // --- Morph ball boost ---
        HandleMorphBallBoost();

        // --- Aim input ---
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

    // =========================================================================
    // HandleAdventureMode — adventure-specific logic (scan visor, UI buttons)
    // =========================================================================
    void MelonPrimeCore::HandleAdventureMode()
    {
        // Note: isMapOrUserActionPaused の更新は呼び出し元で行っているため削除

        auto* nds = emuInstance->getNDS();

        if (IsPressed(IB_SCAN_VISOR)) {
            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

            nds->ReleaseScreen();
            FrameAdvanceTwice();
            using namespace Consts::UI;
            nds->TouchScreen(SCAN_VISOR_BUTTON.x(), SCAN_VISOR_BUTTON.y());

            if ((*m_ptrs.isInVisorOrMap) == 0x1) {
                FrameAdvanceTwice();
            }
            else {
                // Hold movement during visor activation animation
                for (int i = 0; i < 30; ++i) {
                    UpdateInputState();
                    ProcessMoveInputFast();
                    nds->SetKeyMask(m_inputMaskFast);
                    FrameAdvanceOnce();
                }
            }
            nds->ReleaseScreen();
            FrameAdvanceTwice();
        }

        // UI touch buttons — combined mask early-out
        // 呼び出し元でチェック済みだが、念のためIB_UI_ANYチェックは残すか、
        // 個別のIsPressedだけにする。ここは安全策で個別のまま（Anyチェックは呼び出し元で担保）
        if (IsAnyPressed(IB_UI_ANY)) {
#define TOUCH_IF_PRESSED(BIT, POINT) \
            if (IsPressed(BIT)) { \
                nds->ReleaseScreen(); \
                FrameAdvanceTwice(); \
                nds->TouchScreen(POINT.x(), POINT.y()); \
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
    }

    // =========================================================================
    // HandleMorphBallBoost — Samus-only morph ball boost logic
    // =========================================================================
    HOT_FUNCTION bool MelonPrimeCore::HandleMorphBallBoost()
    {
        if (!m_flags.test(StateFlags::BIT_IS_SAMUS)) return false;

        if (IsDown(IB_MORPH_BOOST)) {
            const bool isAltForm = (*m_ptrs.isAltForm) == 0x02;
            m_flags.assign(StateFlags::BIT_IS_ALT_FORM, isAltForm);

            if (isAltForm) {
                const uint8_t boostGauge = *m_ptrs.boostGauge;
                const bool isBoosting = (*m_ptrs.isBoosting) != 0x00;
                const bool gaugeEnough = boostGauge > 0x0A;

                SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, true);

                if (!IsDown(IB_WEAPON_CHECK)) {
                    emuInstance->getNDS()->ReleaseScreen();
                }

                // R button: trigger boost only when gauge sufficient and not already boosting
                InputSetBranchless(INPUT_R, !isBoosting && gaugeEnough);

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

    // =========================================================================
    // ApplyGameSettingsOnce — applies one-time game settings when out of game
    // =========================================================================
    COLD_FUNCTION void MelonPrimeCore::ApplyGameSettingsOnce()
    {
        InputSetBranchless(INPUT_L, !IsPressed(IB_UI_LEFT));
        InputSetBranchless(INPUT_R, !IsPressed(IB_UI_RIGHT));

        auto* nds = emuInstance->getNDS();

        if (!(m_appliedFlags & APPLIED_HEADPHONE)) {
            MelonPrimeGameSettings::ApplyHeadphoneOnce(
                nds, localCfg, m_currentRom.operationAndSound, m_appliedFlags, APPLIED_HEADPHONE);
        }

        MelonPrimeGameSettings::ApplyMphSensitivity(
            nds, localCfg, m_currentRom.sensitivity,
            m_currentRom.baseInGameSensi, m_flags.test(StateFlags::BIT_IN_GAME_INIT));

        if (!(m_appliedFlags & APPLIED_UNLOCK)) {
            MelonPrimeGameSettings::ApplyUnlockHuntersMaps(
                nds, localCfg, m_appliedFlags, APPLIED_UNLOCK,
                m_currentRom.unlockMapsHunters, m_currentRom.unlockMapsHunters2,
                m_currentRom.unlockMapsHunters3, m_currentRom.unlockMapsHunters4,
                m_currentRom.unlockMapsHunters5);
        }

        MelonPrimeGameSettings::UseDsName(nds, localCfg, m_currentRom.dsNameFlagAndMicVolume);
        MelonPrimeGameSettings::ApplySelectedHunterStrict(nds, localCfg, m_currentRom.mainHunter);
        MelonPrimeGameSettings::ApplyLicenseColorStrict(nds, localCfg, m_currentRom.rankColor);

        if (!(m_appliedFlags & APPLIED_VOL_SFX)) {
            MelonPrimeGameSettings::ApplySfxVolumeOnce(
                nds, localCfg, m_currentRom.volSfx8Bit, m_appliedFlags, APPLIED_VOL_SFX);
        }
        if (!(m_appliedFlags & APPLIED_VOL_MUSIC)) {
            MelonPrimeGameSettings::ApplyMusicVolumeOnce(
                nds, localCfg, m_currentRom.volMusic8Bit, m_appliedFlags, APPLIED_VOL_MUSIC);
        }
    }

} // namespace MelonPrime