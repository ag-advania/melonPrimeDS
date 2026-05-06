#ifdef MELONPRIME_DS

    namespace {

        struct WeaponSwitchMismatchSite
        {
            uint32_t EntryAddr;
            uint32_t ExpectedEntry;
            uint32_t CmpAddr;
            uint32_t ExpectedCmp;
            uint32_t BeqAddr;
            uint32_t SkipAddr;
            uint32_t PrepAddr;
            uint32_t CallAddr;
            uint32_t AfterCallAddr;
            uint32_t TryEquipAddr;
            uint32_t MainPlayerPtrGlobal;
        };

        // ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3,
        //                  EU1_0=4, EU1_1=5, KR1_0=6
        static constexpr WeaponSwitchMismatchSite kWeaponSwitchMismatchSites[] = {
            { 0x02026EDCu, 0xE08A0008u, 0x02026EE8u, 0xE1500001u, 0x02026EECu, 0x02026F00u, 0x02026EF0u, 0x02026EF8u, 0x02026F1Cu, 0x0200C5FCu, 0x020BE790u },
            { 0x02026EDCu, 0xE08A0008u, 0x02026EE8u, 0xE1500001u, 0x02026EECu, 0x02026F00u, 0x02026EF0u, 0x02026EF8u, 0x02026F1Cu, 0x0200C5FCu, 0x020BE750u },
            { 0x02026F00u, 0xE08A0008u, 0x02026F0Cu, 0xE1500001u, 0x02026F10u, 0x02026F24u, 0x02026F14u, 0x02026F1Cu, 0x02026F40u, 0x0200C5FCu, 0x020BCA70u },
            { 0x02026F00u, 0xE08A0008u, 0x02026F0Cu, 0xE1500001u, 0x02026F10u, 0x02026F24u, 0x02026F14u, 0x02026F1Cu, 0x02026F40u, 0x0200C5FCu, 0x020BD2D0u },
            { 0x02026EF8u, 0xE08A0008u, 0x02026F04u, 0xE1500001u, 0x02026F08u, 0x02026F1Cu, 0x02026F0Cu, 0x02026F14u, 0x02026F38u, 0x0200C600u, 0x020BD2F0u },
            { 0x02026F00u, 0xE08A0008u, 0x02026F0Cu, 0xE1500001u, 0x02026F10u, 0x02026F24u, 0x02026F14u, 0x02026F1Cu, 0x02026F40u, 0x0200C5FCu, 0x020BD370u },
            { 0x0200C55Cu, 0xE08A0009u, 0x0200C568u, 0xE1500001u, 0x0200C56Cu, 0x0200C580u, 0x0200C570u, 0x0200C578u, 0x0200C598u, 0x02025DBCu, 0x020B6240u },
        };

        [[nodiscard]] static FORCE_INLINE const WeaponSwitchMismatchSite*
        WeaponSwitchHook_GetSite(uint8_t romGroupIndex) noexcept
        {
            if (romGroupIndex >= sizeof(kWeaponSwitchMismatchSites) / sizeof(kWeaponSwitchMismatchSites[0]))
                return nullptr;
            return &kWeaponSwitchMismatchSites[romGroupIndex];
        }

        [[nodiscard]] static FORCE_INLINE bool WeaponSwitchHook_IsMainRamRange(
            uint32_t address,
            uint32_t size) noexcept
        {
            return size != 0
                && address >= 0x02000000u
                && size - 1u <= 0x023FFFFFu - address;
        }

    } // anonymous namespace

    uint32_t MelonPrimeCore::WeaponSwitchHook_GetAddresses(
        uint8_t romGroupIndex,
        uint32_t* out,
        uint32_t maxCount)
    {
        if (!out || maxCount == 0)
            return 0;

        const WeaponSwitchMismatchSite* const site = WeaponSwitchHook_GetSite(romGroupIndex);
        if (!site)
            return 0;

        out[0] = site->EntryAddr;
        return 1;
    }

    bool MelonPrimeCore::WeaponSwitchHook_IsRomSupported(uint8_t romGroupIndex)
    {
        return WeaponSwitchHook_GetSite(romGroupIndex) != nullptr;
    }

    bool MelonPrimeCore::WeaponSwitchHook_IsSiteValid(
        melonDS::NDS* nds,
        uint8_t romGroupIndex)
    {
        const WeaponSwitchMismatchSite* const site = WeaponSwitchHook_GetSite(romGroupIndex);
        return nds
            && site
            && nds->ARM9Read32(site->EntryAddr) == site->ExpectedEntry
            && nds->ARM9Read32(site->CmpAddr) == site->ExpectedCmp;
    }

    void MelonPrimeCore::QueueWeaponSwitchRequest(uint8_t weaponId) noexcept
    {
        if (weaponId >= 9) {
            m_weaponSwitchPending.Clear();
            return;
        }

        m_weaponSwitchPending.WeaponId = weaponId;
        m_weaponSwitchPending.RetryCount = 2;
        m_weaponSwitchPending.FallbackFrames = 3;
    }

    bool MelonPrimeCore::WeaponSwitchHook_DispatchCheckAndRedirect(
        melonDS::NDS* nds,
        uint32_t arm9ExecAddr,
        uint32_t regs[16],
        uint32_t& redirectExecAddr)
    {
        redirectExecAddr = 0;

        if (!nds || !regs)
            return false;
        if (!m_weaponSwitchPending.IsValid())
            return false;
        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
            m_weaponSwitchPending.Clear();
            return false;
        }

        const WeaponSwitchMismatchSite* const site =
            WeaponSwitchHook_GetSite(m_currentRom.romGroupIndex);
        if (!site || arm9ExecAddr != site->EntryAddr)
            return false;

        if (nds->ARM9Read32(site->EntryAddr) != site->ExpectedEntry
            || nds->ARM9Read32(site->CmpAddr) != site->ExpectedCmp)
        {
            m_weaponSwitchPending.Clear();
            return false;
        }

        const uint32_t player = regs[10];
        if (!WeaponSwitchHook_IsMainRamRange(player, 0x4CFu))
            return false;

        uint32_t localPlayer = 0;
        if (WeaponSwitchHook_IsMainRamRange(site->MainPlayerPtrGlobal, sizeof(localPlayer)))
            localPlayer = Read32(nds->MainRAM, site->MainPlayerPtrGlobal);

        if (localPlayer != 0) {
            if (player != localPlayer)
                return false;
        }
        else {
            const uint32_t expectedPlayer =
                m_currentRom.playerStructStart
                + static_cast<uint32_t>(m_playerPosition) * Consts::PLAYER_ADDR_INC;
            if (player != expectedPlayer)
                return false;
        }

        if (m_weaponSwitchPending.RetryCount == 0) {
            m_weaponSwitchPending.Clear();
            return false;
        }
        --m_weaponSwitchPending.RetryCount;

        const uint8_t weaponId = m_weaponSwitchPending.WeaponId;
        if (weaponId >= 9) {
            m_weaponSwitchPending.Clear();
            return false;
        }

        const uint8_t currentWeapon = Read8(nds->MainRAM, player + 0x4CEu);
        if (currentWeapon == weaponId) {
            m_weaponSwitchPending.Clear();
            return false;
        }

        // JIT hooks only reliably fire at block starts.  Hook the slot scan
        // entry and jump to the existing BL so LR still returns to the game's
        // original after-call path.
        regs[0] = player;
        regs[1] = static_cast<uint32_t>(weaponId);
        regs[2] = 0;
        redirectExecAddr = site->CallAddr;
        m_weaponSwitchPending.Clear();
        return true;
    }

#endif // MELONPRIME_DS
