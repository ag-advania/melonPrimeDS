#ifdef MELONPRIME_DS

    namespace {

        struct NativeBipedFireSite
        {
            uint32_t DirectCallHookAddr;
            uint32_t ExpectedDirectCall;
            uint32_t FireUpdateAddr;
            uint32_t InnerEdgeHookAddr;
            uint32_t ExpectedInnerEdgeMovs;
            uint32_t LocalPlayerPtrGlobal;
        };

        enum class NativeBipedFireHookKind : uint8_t
        {
            None,
            DirectCall,
            InnerEdge,
        };

        static constexpr uint32_t kNativeBipedFireTrampolineAddr = 0x02003F60u;
        static constexpr uint32_t kNativeBipedFireScratchAddr = 0x02003F90u;

        static constexpr uint32_t kNativeBipedFireScratchPlayer = 0u;
        static constexpr uint32_t kNativeBipedFireScratchFireUpdate = 4u;
        static constexpr uint32_t kNativeBipedFireScratchReturnAddr = 8u;

        // ARM trampoline:
        //   save original regs
        //   call PlayerFireUpdate(player)
        //   restore original regs
        //   branch back to the direct hook address. The second hook hit clears
        //   m_nativeBipedFireDirectActive and lets the original BL execute.
        static constexpr uint32_t kNativeBipedFireTrampolineCode[] = {
            0xE92D5FFFu, // stmdb sp!, {r0-r12, lr}
            0xE59FC018u, // ldr   r12, [pc, #0x18]
            0xE59C0000u, // ldr   r0,  [r12, #0x00]
            0xE59C3004u, // ldr   r3,  [r12, #0x04]
            0xE12FFF33u, // blx   r3
            0xE8BD5FFFu, // ldmia sp!, {r0-r12, lr}
            0xE59FC004u, // ldr   r12, [pc, #0x04]
            0xE59C3008u, // ldr   r3,  [r12, #0x08]
            0xE12FFF13u, // bx    r3
            kNativeBipedFireScratchAddr,
        };

        // ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3,
        //                  EU1_0=4, EU1_1=5, KR1_0=6
        //
        // DirectCallHookAddr matches the reliable player-update BL used by
        // WeaponSwitchHook.  The inner edge hook only forces r0=1 during the
        // trampoline's direct PlayerFireUpdate call.
        static constexpr NativeBipedFireSite kNativeBipedFireSites[] = {
            { 0x020263DCu, 0xEB000206u, 0x020271E0u, 0x02027234u, 0xE1B06000u, 0x020BE790u },
            { 0x020263DCu, 0xEB000206u, 0x020271E0u, 0x02027234u, 0xE1B06000u, 0x020BE750u },
            { 0x02026400u, 0xEB000206u, 0x02027204u, 0x02027258u, 0xE1B06000u, 0x020BCA70u },
            { 0x02026400u, 0xEB000206u, 0x02027204u, 0x02027258u, 0xE1B06000u, 0x020BD2D0u },
            { 0x020263F8u, 0xEB000206u, 0x020271FCu, 0x02027250u, 0xE1B06000u, 0x020BD2F0u },
            { 0x02026400u, 0xEB000206u, 0x02027204u, 0x02027258u, 0xE1B06000u, 0x020BD370u },
            { 0x0200CF1Cu, 0xEBFFFCDEu, 0x0200BD28u, 0x0200BD78u, 0xE1B04000u, 0x020B6240u },
        };

        [[nodiscard]] static FORCE_INLINE const NativeBipedFireSite*
        NativeBipedFire_GetSite(uint8_t romGroupIndex) noexcept
        {
            if (romGroupIndex >= sizeof(kNativeBipedFireSites) / sizeof(kNativeBipedFireSites[0]))
                return nullptr;
            return &kNativeBipedFireSites[romGroupIndex];
        }

        [[nodiscard]] static FORCE_INLINE bool NativeBipedFire_IsMainRamRange(
            uint32_t address,
            uint32_t size) noexcept
        {
            return size != 0
                && address >= 0x02000000u
                && size - 1u <= 0x023FFFFFu - address;
        }

        [[nodiscard]] static FORCE_INLINE bool NativeBipedFire_Read32(
            melonDS::NDS* nds,
            uint32_t address,
            uint32_t& out) noexcept
        {
            if (!nds || !NativeBipedFire_IsMainRamRange(address, sizeof(out)))
                return false;
            out = nds->ARM9Read32(address);
            return true;
        }

        [[nodiscard]] static bool NativeBipedFire_EnsureTrampoline(melonDS::NDS* nds)
        {
            if (!nds)
                return false;
            if (!NativeBipedFire_IsMainRamRange(
                    kNativeBipedFireTrampolineAddr,
                    static_cast<uint32_t>(sizeof(kNativeBipedFireTrampolineCode))))
            {
                return false;
            }
            if (!NativeBipedFire_IsMainRamRange(kNativeBipedFireScratchAddr, 12u))
                return false;

            for (uint32_t i = 0; i < sizeof(kNativeBipedFireTrampolineCode) / sizeof(kNativeBipedFireTrampolineCode[0]); ++i) {
                const uint32_t addr = kNativeBipedFireTrampolineAddr + i * sizeof(uint32_t);
                const uint32_t word = kNativeBipedFireTrampolineCode[i];
                if (nds->ARM9Read32(addr) != word)
                    nds->ARM9Write32(addr, word);
            }
            return true;
        }

        static FORCE_INLINE void NativeBipedFire_WriteScratch(
            melonDS::NDS* nds,
            const NativeBipedFireSite& site,
            uint32_t player)
        {
            Write32(nds->MainRAM, kNativeBipedFireScratchAddr + kNativeBipedFireScratchPlayer, player);
            Write32(nds->MainRAM, kNativeBipedFireScratchAddr + kNativeBipedFireScratchFireUpdate, site.FireUpdateAddr);
            Write32(nds->MainRAM, kNativeBipedFireScratchAddr + kNativeBipedFireScratchReturnAddr, site.DirectCallHookAddr);
        }

        [[nodiscard]] static FORCE_INLINE NativeBipedFireHookKind
        NativeBipedFire_GetHookKind(
            const NativeBipedFireSite& site,
            uint32_t arm9ExecAddr,
            uint32_t& expectedWord) noexcept
        {
            if (arm9ExecAddr == site.DirectCallHookAddr) {
                expectedWord = site.ExpectedDirectCall;
                return NativeBipedFireHookKind::DirectCall;
            }
            if (arm9ExecAddr == site.InnerEdgeHookAddr) {
                expectedWord = site.ExpectedInnerEdgeMovs;
                return NativeBipedFireHookKind::InnerEdge;
            }

            expectedWord = 0;
            return NativeBipedFireHookKind::None;
        }

    } // anonymous namespace

    uint32_t MelonPrimeCore::NativeBipedFireHook_GetAddresses(
        uint8_t romGroupIndex,
        uint32_t* out,
        uint32_t maxCount)
    {
        if (!out || maxCount == 0)
            return 0;

        const NativeBipedFireSite* const site = NativeBipedFire_GetSite(romGroupIndex);
        if (!site)
            return 0;

        uint32_t count = 0;
        if (count < maxCount) out[count++] = site->DirectCallHookAddr;
        if (count < maxCount) out[count++] = site->InnerEdgeHookAddr;
        return count;
    }

    HOT_FUNCTION void MelonPrimeCore::UpdateNativeBipedFireInput()
    {
        // ApplyBipedFireInput already gates on m_enableNativeBipedFire, so by
        // contract this function is never entered with the feature disabled.
        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT)
            || !m_flags.test(StateFlags::BIT_LAST_FOCUSED)
            || (m_aimBlockBits & AIMBLK_NOT_IN_GAME))
        {
            m_nativeBipedFirePending = false;
            m_nativeBipedFireDirectActive = false;
            return;
        }

        const bool shootDown = (m_input.down & IB_SHOOT) != 0;
        if (!shootDown || IsPlayerAltForm()) {
            m_nativeBipedFirePending = false;
            m_nativeBipedFireDirectActive = false;
            return;
        }

        m_nativeBipedFirePending = true;
    }

    bool MelonPrimeCore::NativeBipedFireHook_DispatchCheckAndRedirect(
        melonDS::NDS* nds,
        uint32_t arm9ExecAddr,
        uint32_t regs[16],
        uint32_t& redirectExecAddr)
    {
        redirectExecAddr = 0;

        // ARM9Hook_Install gates registration on m_enableNativeBipedFire AND
        // a non-null GetSite for the current ROM, so both checks here are
        // dead in practice.
        if (!nds || !regs)
            return false;

        const NativeBipedFireSite* const site =
            NativeBipedFire_GetSite(m_currentRom.romGroupIndex);
        if (!site)
            return false;

        uint32_t expectedWord = 0;
        const NativeBipedFireHookKind hookKind =
            NativeBipedFire_GetHookKind(*site, arm9ExecAddr, expectedWord);
        if (hookKind == NativeBipedFireHookKind::None)
            return false;

        if (nds->ARM9Read32(arm9ExecAddr) != expectedWord) {
            m_nativeBipedFirePending = false;
            m_nativeBipedFireDirectActive = false;
            return false;
        }

        if (hookKind == NativeBipedFireHookKind::InnerEdge) {
            if (m_nativeBipedFireDirectActive)
                regs[0] = 1u;
            return false;
        }

        if (m_nativeBipedFireDirectActive) {
            m_nativeBipedFireDirectActive = false;
            return false;
        }

        if (!m_nativeBipedFirePending)
            return false;

        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
            m_nativeBipedFirePending = false;
            return false;
        }

        if (IsPlayerAltForm()) {
            m_nativeBipedFirePending = false;
            return false;
        }

        uint32_t player = regs[0];
        if (!NativeBipedFire_IsMainRamRange(player, 0x900u)
            && NativeBipedFire_IsMainRamRange(regs[5], 0x900u))
        {
            player = regs[5];
        }
        if (!NativeBipedFire_IsMainRamRange(player, 0x900u))
            return false;

        uint32_t localPlayer = 0;
        if (!NativeBipedFire_Read32(nds, site->LocalPlayerPtrGlobal, localPlayer))
            return false;

        const uint32_t expectedPlayer =
            m_currentRom.playerStructStart
            + static_cast<uint32_t>(m_playerPosition) * Consts::PLAYER_ADDR_INC;
        if (localPlayer != 0) {
            if (player != localPlayer)
                return false;
        }
        else if (player != expectedPlayer)
            return false;

        if (!NativeBipedFire_EnsureTrampoline(nds))
            return false;

        NativeBipedFire_WriteScratch(nds, *site, player);
        m_nativeBipedFireDirectActive = true;
        redirectExecAddr = kNativeBipedFireTrampolineAddr;
        return true;
    }

#else

    HOT_FUNCTION void MelonPrimeCore::UpdateNativeBipedFireInput()
    {
    }

#endif // MELONPRIME_DS
