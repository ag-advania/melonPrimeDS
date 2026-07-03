#include "MelonPrimeInternal.h"
#include "EmuInstance.h"
#include "MelonPrimeDef.h"
#include "NDS.h"

#include <cstdint>

namespace MelonPrime {

    COLD_FUNCTION void MelonPrimeCore::ReloadDamageNotifyPurpleConfig()
    {
        // Disabled by default; cached so the per-frame tick reads only one bool,
        // never the config table.
        const bool dnpRequested =
            localCfg.GetBool(MelonPrime::CfgKey::DamageNotifyPurple);
        const bool ddMultDisabled =
            localCfg.GetBool(MelonPrime::CfgKey::DisableDoubleDamageMultiplier);
        const bool dnp = dnpRequested && ddMultDisabled;

        if (m_damageNotifyPurpleEnabled && !dnp)
            m_damageNotifyPurpleState = {};
        m_damageNotifyPurpleEnabled = dnp;
    }

    // =========================================================================
    // Damage Notify Purple — Weavel-aware effective HP watcher v4
    //
    // Spec: 29-Damage-Notify-Purple-NonHook-AI-Implementation-Instructions-v4
    //
    // When the local player's effective HP drops, write 10 to the local Double
    // Damage timer (CPlayer +0x4B0). This produces a short purple flash so
    // opponents can see "this player just took damage". Pair with Disable
    // Double Damage Multiplier so the flash never becomes a real 2x boost.
    //
    // Observed HP metric:
    // - non-Weavel             : observed = mainHp
    // - Weavel proxy inactive  : observed = mainHp
    // - Weavel proxy active    : observed = mainHp + proxyHp
    //
    // proxyActive attach/detach edges are baseline-only frames so metric
    // switches never look like damage. Threshold = 5; real MPH damage is >=10.
    // Caller gates this on isInGame, so ROM/pointer setup is valid.
    // =========================================================================
    HOT_FUNCTION void MelonPrimeCore::DamageNotifyPurpleTick()
    {
        if (!m_damageNotifyPurpleEnabled) {
            if (UNLIKELY(m_damageNotifyPurpleState.initialized))
                m_damageNotifyPurpleState = {};
            return;
        }

        if (UNLIKELY(!m_ptrs.health || !m_ptrs.doubleDamageTimer)) {
            m_damageNotifyPurpleState = {};
            return;
        }

        constexpr uint32_t PROXY_ACTIVE_BIT = 0x20u;        // moreFlags bit5
        constexpr uint32_t PROXY_HP_OFF     = 0xD0u;        // proxy entity +0xD0
        constexpr uint32_t ARM9_RAM_BASE    = 0x02000000u;
        constexpr uint32_t ARM9_RAM_END     = 0x023FFFFEu;  // last addr that fits a u16
        constexpr uint16_t PROXY_HP_MAX     = 100;
        constexpr uint16_t NOTIFY_DURATION  = 10;
        constexpr uint16_t DELTA_THRESHOLD  = 5;

        DamageNotifyPurpleState& s = m_damageNotifyPurpleState;
        const bool isWeavel = m_flags.test(StateFlags::BIT_IS_WEAVEL);

        const uint16_t mainHp = *m_ptrs.health;
        if (LIKELY(!isWeavel)) {
            if (!s.initialized) {
                s.previousObservedHp = mainHp;
                s.previousMainHp = mainHp;
                s.previousProxyHp = 0;
                s.previousProxyActive = false;
                s.initialized = true;
                return;
            }

            const uint16_t prev = s.previousObservedHp;
            s.previousObservedHp = mainHp;
            s.previousMainHp = mainHp;

            if (prev > mainHp) {
                const uint16_t delta = static_cast<uint16_t>(prev - mainHp);
                if (delta > DELTA_THRESHOLD)
                    *m_ptrs.doubleDamageTimer = NOTIFY_DURATION;
            }
            return;
        }

        uint16_t observedHp = mainHp;
        uint16_t proxyHp = 0;
        bool proxyActive = false;
        bool skipCompare = false;

        if (m_ptrs.moreFlags && m_ptrs.weavelProxyPtr) {
            proxyActive = (*m_ptrs.moreFlags & PROXY_ACTIVE_BIT) != 0;

            if (proxyActive) {
                const uint32_t proxyHpAddr = *m_ptrs.weavelProxyPtr + PROXY_HP_OFF;
                const bool proxyAddrValid =
                    proxyHpAddr >= ARM9_RAM_BASE && proxyHpAddr <= ARM9_RAM_END;

                if (!proxyAddrValid) {
                    skipCompare = true;
                }
                else {
                    melonDS::u8* const mainRAM = emuInstance->getNDS()->MainRAM;
                    if (UNLIKELY(mainRAM == nullptr)) {
                        skipCompare = true;
                    }
                    else {
                        const uint16_t proxyHpRaw = Read16(mainRAM, proxyHpAddr);
                        if (proxyHpRaw > PROXY_HP_MAX) {
                            // Junk read; baseline only so the next valid frame
                            // does not look like a sudden total drop.
                            skipCompare = true;
                        }
                        else {
                            proxyHp = proxyHpRaw;
                            const uint32_t total =
                                static_cast<uint32_t>(mainHp) +
                                static_cast<uint32_t>(proxyHp);
                            observedHp = (total > 0xFFFFu)
                                ? 0xFFFFu : static_cast<uint16_t>(total);
                        }
                    }
                }
            }
        }

        const bool wasProxyActive = s.previousProxyActive;
        const bool proxyJustAttached = !wasProxyActive && proxyActive;
        const bool proxyJustDetached = wasProxyActive && !proxyActive;

        auto updateBaselines = [&]() {
            s.previousObservedHp = observedHp;
            s.previousMainHp = mainHp;
            s.previousProxyHp = proxyHp;
            s.previousProxyActive = proxyActive;
        };

        if (!s.initialized) {
            s.initialized = true;
            updateBaselines();
            return;
        }

        if (skipCompare || proxyJustAttached || proxyJustDetached) {
            updateBaselines();
            return;
        }

        const uint16_t prev = s.previousObservedHp;
        updateBaselines();

        if (prev > observedHp) {
            const uint16_t delta = static_cast<uint16_t>(prev - observedHp);
            if (delta > DELTA_THRESHOLD)
                *m_ptrs.doubleDamageTimer = NOTIFY_DURATION;
        }
    }

} // namespace MelonPrime
