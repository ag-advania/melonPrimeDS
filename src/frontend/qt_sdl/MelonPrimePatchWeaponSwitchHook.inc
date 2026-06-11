#ifdef MELONPRIME_DS

    namespace {

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_WEAPON_SWITCH_DEBUG_LOG)
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

        static constexpr uint32_t kWeaponSwitchTrampolineAddr = 0x02003EA0u;
        static constexpr uint32_t kWeaponSwitchScratchAddr = 0x02003EE0u;

        static constexpr uint32_t kWeaponSwitchScratchPlayer = 0u;
        static constexpr uint32_t kWeaponSwitchScratchWeaponId = 4u;
        static constexpr uint32_t kWeaponSwitchScratchTryEquip = 8u;
        static constexpr uint32_t kWeaponSwitchScratchOriginalCallee = 12u;
        static constexpr uint32_t kWeaponSwitchScratchAfter = 16u;

        // ARM trampoline:
        //   save r0-r12/lr
        //   call TryEquipWeapon(player, pendingWeaponId, 0)
        //   restore original hook-site regs
        //   tail-call the original per-frame callee with lr=ActiveAfterAddr
        //
        // This keeps the original BL's work in the same frame instead of
        // replacing it with the weapon switch native call.
        static constexpr uint32_t kWeaponSwitchTrampolineCode[] = {
            0xE92D5FFFu, // stmdb sp!, {r0-r12, lr}
            0xE59FC024u, // ldr   r12, [pc, #0x24] ; scratch base literal
            0xE59C0000u, // ldr   r0,  [r12, #0x00]
            0xE5DC1004u, // ldrb  r1,  [r12, #0x04]
            0xE3A02000u, // mov   r2,  #0
            0xE59C3008u, // ldr   r3,  [r12, #0x08]
            0xE12FFF33u, // blx   r3
            0xE8BD5FFFu, // ldmia sp!, {r0-r12, lr}
            0xE59FC008u, // ldr   r12, [pc, #0x08] ; scratch base literal
            0xE59CE010u, // ldr   lr,  [r12, #0x10]
            0xE59C300Cu, // ldr   r3,  [r12, #0x0C]
            0xE12FFF13u, // bx    r3
            kWeaponSwitchScratchAddr,
        };

        // ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3,
        //                  EU1_0=4, EU1_1=5, KR1_0=6
#define MP_WEAPON_SWITCH_SITE(i, originalTarget, tryEquipAddr, expectedTryEquipEntry) \
        { LIST_HookPlayerUpdateActiveCallAddr[i], LIST_HookPlayerUpdateActiveCallExpected[i], \
          LIST_HookPlayerUpdateActiveAfter[i], originalTarget, tryEquipAddr, \
          expectedTryEquipEntry, LIST_HookLocalPlayerPtrGlobal[i] }
        static constexpr WeaponSwitchActiveCallSite kWeaponSwitchActiveCallSites[] = {
            MP_WEAPON_SWITCH_SITE(0, 0x02026BFCu, 0x0200C5FCu, 0xE92D47F0u),
            MP_WEAPON_SWITCH_SITE(1, 0x02026BFCu, 0x0200C5FCu, 0xE92D47F0u),
            MP_WEAPON_SWITCH_SITE(2, 0x02026C20u, 0x0200C5FCu, 0xE92D47F0u),
            MP_WEAPON_SWITCH_SITE(3, 0x02026C20u, 0x0200C5FCu, 0xE92D47F0u),
            MP_WEAPON_SWITCH_SITE(4, 0x02026C18u, 0x0200C600u, 0xE92D47F0u),
            MP_WEAPON_SWITCH_SITE(5, 0x02026C20u, 0x0200C5FCu, 0xE92D47F0u),
            MP_WEAPON_SWITCH_SITE(6, 0x0200C29Cu, 0x02025DBCu, 0xE92D47F0u),
        };
#undef MP_WEAPON_SWITCH_SITE
        static_assert(
            sizeof(kWeaponSwitchActiveCallSites) / sizeof(kWeaponSwitchActiveCallSites[0])
                == static_cast<uint32_t>(RomGroup::COUNT),
            "Weapon switch active-call table must cover every ROM group");
        static constexpr bool WeaponSwitchHook_SitesInMainRam() noexcept
        {
            for (const auto& site : kWeaponSwitchActiveCallSites) {
                if (!RomAddrDetail::InMainRam(site.ActiveCallAddr)
                    || !RomAddrDetail::InMainRam(site.ActiveAfterAddr)
                    || !RomAddrDetail::InMainRam(site.OriginalTarget)
                    || !RomAddrDetail::InMainRam(site.TryEquipAddr)
                    || !RomAddrDetail::InMainRam(site.MainPlayerPtrGlobal))
                {
                    return false;
                }
            }
            return true;
        }
        static_assert(WeaponSwitchHook_SitesInMainRam(),
                      "Weapon switch hook-site table contains a non-main-RAM address");

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

        [[nodiscard]] static bool WeaponSwitchHook_EnsureTrampoline(melonDS::NDS* nds)
        {
            if (!nds)
                return false;
            if (!WeaponSwitchHook_IsMainRamRange(
                    kWeaponSwitchTrampolineAddr,
                    static_cast<uint32_t>(sizeof(kWeaponSwitchTrampolineCode))))
            {
                return false;
            }
            if (!WeaponSwitchHook_IsMainRamRange(kWeaponSwitchScratchAddr, 20u))
                return false;

            for (uint32_t i = 0; i < sizeof(kWeaponSwitchTrampolineCode) / sizeof(kWeaponSwitchTrampolineCode[0]); ++i) {
                const uint32_t addr = kWeaponSwitchTrampolineAddr + i * sizeof(uint32_t);
                const uint32_t word = kWeaponSwitchTrampolineCode[i];
                if (nds->ARM9Read32(addr) != word)
                    nds->ARM9Write32(addr, word);
            }
            return true;
        }

        static FORCE_INLINE void WeaponSwitchHook_WriteScratch(
            melonDS::NDS* nds,
            const WeaponSwitchActiveCallSite& site,
            uint32_t player,
            uint8_t weaponId)
        {
            Write32(nds->MainRAM, kWeaponSwitchScratchAddr + kWeaponSwitchScratchPlayer, player);
            Write32(nds->MainRAM, kWeaponSwitchScratchAddr + kWeaponSwitchScratchWeaponId, weaponId);
            Write32(nds->MainRAM, kWeaponSwitchScratchAddr + kWeaponSwitchScratchTryEquip, site.TryEquipAddr);
            Write32(nds->MainRAM, kWeaponSwitchScratchAddr + kWeaponSwitchScratchOriginalCallee, site.OriginalTarget);
            Write32(nds->MainRAM, kWeaponSwitchScratchAddr + kWeaponSwitchScratchAfter, site.ActiveAfterAddr);
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
            && nds->ARM9Read32(site->TryEquipAddr) == site->ExpectedTryEquipEntry
            && WeaponSwitchHook_EnsureTrampoline(nds);
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

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_WEAPON_SWITCH_DEBUG_LOG)
        const uint8_t currentWeapon = m_ptrs.currentWeapon ? *m_ptrs.currentWeapon : 0xFFu;
        const uint8_t selectedWeapon = m_ptrs.selectedWeapon ? *m_ptrs.selectedWeapon : 0xFFu;
        MP_WEAPON_SWITCH_LOG(
            "WeaponSwitch Queue: rom=%u weapon=%u current=%u selected=%u\n",
            m_currentRom.romGroupIndex,
            weaponId,
            currentWeapon,
            selectedWeapon);
#endif
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

        if (!WeaponSwitchHook_EnsureTrampoline(nds)) {
            MP_WEAPON_SWITCH_LOG(
                "WeaponSwitch TrampolineFail: exec=%08X tramp=%08X scratch=%08X\n",
                arm9ExecAddr,
                kWeaponSwitchTrampolineAddr,
                kWeaponSwitchScratchAddr);
            return false;
        }

        WeaponSwitchHook_WriteScratch(nds, *site, player, weaponId);
        redirectExecAddr = kWeaponSwitchTrampolineAddr;

        MP_WEAPON_SWITCH_LOG(
            "WeaponSwitch NativeTrampoline: exec=%08X player=%08X weapon=%u tramp=%08X tryEquip=%08X after=%08X original=%08X\n",
            arm9ExecAddr,
            player,
            weaponId,
            kWeaponSwitchTrampolineAddr,
            site->TryEquipAddr,
            site->ActiveAfterAddr,
            site->OriginalTarget);

        m_weaponSwitchPending.Clear();
        return true;
    }

#undef MP_WEAPON_SWITCH_LOG

#endif // MELONPRIME_DS
