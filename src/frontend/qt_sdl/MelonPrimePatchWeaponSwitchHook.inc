#ifdef MELONPRIME_DS

    namespace {

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
#define MP_WEAPON_SWITCH_LOG(...) \
        melonDS::Platform::Log(melonDS::Platform::LogLevel::Info, __VA_ARGS__)
#else
#define MP_WEAPON_SWITCH_LOG(...) do {} while (0)
#endif

        struct WeaponSwitchActiveCallSite
        {
            uint32_t ActiveCallAddr;
            uint32_t ExpectedActiveCall;
            uint32_t ActiveAfterAddr;
            uint32_t OriginalTarget;
            uint32_t TryEquipAddr;
            uint32_t ExpectedTryEquipEntry;
            uint32_t MainPlayerPtrGlobal;
        };

        // ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3,
        //                  EU1_0=4, EU1_1=5, KR1_0=6
        static constexpr WeaponSwitchActiveCallSite kWeaponSwitchActiveCallSites[] = {
            { 0x020263DCu, 0xEB000206u, 0x020263E0u, 0x02026BFCu, 0x0200C5FCu, 0xE92D47F0u, 0x020BE790u },
            { 0x020263DCu, 0xEB000206u, 0x020263E0u, 0x02026BFCu, 0x0200C5FCu, 0xE92D47F0u, 0x020BE750u },
            { 0x02026400u, 0xEB000206u, 0x02026404u, 0x02026C20u, 0x0200C5FCu, 0xE92D47F0u, 0x020BCA70u },
            { 0x02026400u, 0xEB000206u, 0x02026404u, 0x02026C20u, 0x0200C5FCu, 0xE92D47F0u, 0x020BD2D0u },
            { 0x020263F8u, 0xEB000206u, 0x020263FCu, 0x02026C18u, 0x0200C600u, 0xE92D47F0u, 0x020BD2F0u },
            { 0x02026400u, 0xEB000206u, 0x02026404u, 0x02026C20u, 0x0200C5FCu, 0xE92D47F0u, 0x020BD370u },
            { 0x0200CF1Cu, 0xEBFFFCDEu, 0x0200CF20u, 0x0200C29Cu, 0x02025DBCu, 0xE92D47F0u, 0x020B6240u },
        };

        [[nodiscard]] static FORCE_INLINE const WeaponSwitchActiveCallSite*
        WeaponSwitchHook_GetSite(uint8_t romGroupIndex) noexcept
        {
            if (romGroupIndex >= sizeof(kWeaponSwitchActiveCallSites) / sizeof(kWeaponSwitchActiveCallSites[0]))
                return nullptr;
            return &kWeaponSwitchActiveCallSites[romGroupIndex];
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

        const WeaponSwitchActiveCallSite* const site = WeaponSwitchHook_GetSite(romGroupIndex);
        if (!site)
            return 0;

        out[0] = site->ActiveCallAddr;
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
        const WeaponSwitchActiveCallSite* const site = WeaponSwitchHook_GetSite(romGroupIndex);
        return nds
            && site
            && nds->ARM9Read32(site->ActiveCallAddr) == site->ExpectedActiveCall
            && nds->ARM9Read32(site->TryEquipAddr) == site->ExpectedTryEquipEntry;
    }

    void MelonPrimeCore::QueueWeaponSwitchRequest(uint8_t weaponId) noexcept
    {
        if (weaponId >= 9) {
            m_weaponSwitchPending.Clear();
            return;
        }

        m_weaponSwitchPending.WeaponId = weaponId;
        m_weaponSwitchPending.RetryCount = 4;
        m_weaponSwitchPending.FallbackFrames = 12;

        const uint8_t currentWeapon = m_ptrs.currentWeapon ? *m_ptrs.currentWeapon : 0xFFu;
        const uint8_t selectedWeapon = m_ptrs.selectedWeapon ? *m_ptrs.selectedWeapon : 0xFFu;
        MP_WEAPON_SWITCH_LOG(
            "WeaponSwitch Queue: rom=%u weapon=%u current=%u selected=%u\n",
            m_currentRom.romGroupIndex,
            weaponId,
            currentWeapon,
            selectedWeapon);
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
            MP_WEAPON_SWITCH_LOG(
                "WeaponSwitch GateFail: inGameInit=0 pending=%u exec=%08X\n",
                m_weaponSwitchPending.WeaponId,
                arm9ExecAddr);
            m_weaponSwitchPending.Clear();
            return false;
        }

        const WeaponSwitchActiveCallSite* const site =
            WeaponSwitchHook_GetSite(m_currentRom.romGroupIndex);
        if (!site || arm9ExecAddr != site->ActiveCallAddr)
            return false;

        MP_WEAPON_SWITCH_LOG(
            "WeaponSwitch HookHit: exec=%08X pending=%u r0=%08X r1=%08X r5=%08X lr=%08X\n",
            arm9ExecAddr,
            m_weaponSwitchPending.WeaponId,
            regs[0],
            regs[1],
            regs[5],
            regs[14]);

        const uint32_t activeCallWord = nds->ARM9Read32(site->ActiveCallAddr);
        const uint32_t tryEquipEntry = nds->ARM9Read32(site->TryEquipAddr);
        if (activeCallWord != site->ExpectedActiveCall
            || tryEquipEntry != site->ExpectedTryEquipEntry)
        {
            MP_WEAPON_SWITCH_LOG(
                "WeaponSwitch SiteMismatch: exec=%08X active=%08X expectedActive=%08X try=%08X expectedTry=%08X\n",
                arm9ExecAddr,
                activeCallWord,
                site->ExpectedActiveCall,
                tryEquipEntry,
                site->ExpectedTryEquipEntry);
            m_weaponSwitchPending.Clear();
            return false;
        }

        uint32_t player = regs[0];
        if (!WeaponSwitchHook_IsMainRamRange(player, 0x900u)
            && WeaponSwitchHook_IsMainRamRange(regs[5], 0x900u))
        {
            player = regs[5];
        }

        if (!WeaponSwitchHook_IsMainRamRange(player, 0x900u)) {
            MP_WEAPON_SWITCH_LOG(
                "WeaponSwitch PlayerRangeFail: exec=%08X player=%08X r0=%08X r5=%08X pending=%u\n",
                arm9ExecAddr,
                player,
                regs[0],
                regs[5],
                m_weaponSwitchPending.WeaponId);
            return false;
        }

        uint32_t localPlayer = 0;
        if (WeaponSwitchHook_IsMainRamRange(site->MainPlayerPtrGlobal, sizeof(localPlayer)))
            localPlayer = Read32(nds->MainRAM, site->MainPlayerPtrGlobal);

        const uint32_t expectedPlayer =
            m_currentRom.playerStructStart
            + static_cast<uint32_t>(m_playerPosition) * Consts::PLAYER_ADDR_INC;
        if (localPlayer != 0) {
            if (player != localPlayer) {
                MP_WEAPON_SWITCH_LOG(
                    "WeaponSwitch LocalMismatch: exec=%08X player=%08X local=%08X expected=%08X pos=%u\n",
                    arm9ExecAddr,
                    player,
                    localPlayer,
                    expectedPlayer,
                    m_playerPosition);
                return false;
            }
        }
        else if (player != expectedPlayer) {
            MP_WEAPON_SWITCH_LOG(
                "WeaponSwitch LocalMismatch: exec=%08X player=%08X local=00000000 expected=%08X pos=%u\n",
                arm9ExecAddr,
                player,
                expectedPlayer,
                m_playerPosition);
            return false;
        }

        if (m_weaponSwitchPending.RetryCount == 0) {
            MP_WEAPON_SWITCH_LOG(
                "WeaponSwitch RetryExhausted: exec=%08X pending=%u\n",
                arm9ExecAddr,
                m_weaponSwitchPending.WeaponId);
            m_weaponSwitchPending.Clear();
            return false;
        }
        --m_weaponSwitchPending.RetryCount;

        const uint8_t weaponId = m_weaponSwitchPending.WeaponId;
        if (weaponId >= 9) {
            MP_WEAPON_SWITCH_LOG(
                "WeaponSwitch InvalidWeapon: exec=%08X weapon=%u\n",
                arm9ExecAddr,
                weaponId);
            m_weaponSwitchPending.Clear();
            return false;
        }

        const uint8_t currentWeapon = Read8(nds->MainRAM, player + 0x4CEu);
        if (currentWeapon == weaponId) {
            MP_WEAPON_SWITCH_LOG(
                "WeaponSwitch AlreadyCurrent: exec=%08X weapon=%u\n",
                arm9ExecAddr,
                weaponId);
            m_weaponSwitchPending.Clear();
            return false;
        }

        regs[0] = player;
        regs[1] = static_cast<uint32_t>(weaponId);
        regs[2] = 0;
        regs[14] = site->ActiveAfterAddr;
        redirectExecAddr = site->TryEquipAddr;

        MP_WEAPON_SWITCH_LOG(
            "WeaponSwitch NativeInvoke: exec=%08X player=%08X weapon=%u tryEquip=%08X lr=%08X original=%08X\n",
            arm9ExecAddr,
            player,
            weaponId,
            site->TryEquipAddr,
            site->ActiveAfterAddr,
            site->OriginalTarget);

        m_weaponSwitchPending.Clear();
        return true;
    }

#undef MP_WEAPON_SWITCH_LOG

#endif // MELONPRIME_DS
