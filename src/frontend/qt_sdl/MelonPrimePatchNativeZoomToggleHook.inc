#ifdef MELONPRIME_DS

    namespace {

        struct NativeZoomToggleSite
        {
            uint32_t LocalPlayerPtrGlobal;
            uint32_t ScopeZoomSetter;
            uint32_t WeaponActionUpdate;
        };

        static constexpr uint32_t kNativeZoomToggleTrampolineAddr = 0x02003F00u;
        static constexpr uint32_t kNativeZoomToggleScratchAddr = 0x02003F40u;

        static constexpr uint32_t kNativeZoomScratchPlayer = 0u;
        static constexpr uint32_t kNativeZoomScratchEnabled = 4u;
        static constexpr uint32_t kNativeZoomScratchSetter = 8u;
        static constexpr uint32_t kNativeZoomScratchReturnAddr = 12u;

        static constexpr uint32_t kPlayerScopeFlagsOffset = 0x850u;
        static constexpr uint32_t kPlayerCurrentWeaponPtrOffset = 0x858u;
        static constexpr uint32_t kWeaponFlagsOffset = 0x08u;
        static constexpr uint32_t kScopeZoomEnabledBit = 0x01u;
        static constexpr uint32_t kWeaponFlagZoomCapable = 0x00000800u;

        // ARM trampoline:
        //   save original regs
        //   call SetPlayerScopeZoom(player, enabled)
        //   restore original regs
        //   branch back to the original hook address. The pending call is
        //   already cleared, so the second hook hit falls through to the
        //   game's original instruction stream.
        static constexpr uint32_t kNativeZoomToggleTrampolineCode[] = {
            0xE92D5FFFu, // stmdb sp!, {r0-r12, lr}
            0xE59FC01Cu, // ldr   r12, [pc, #0x1C]
            0xE59C0000u, // ldr   r0,  [r12, #0x00]
            0xE5DC1004u, // ldrb  r1,  [r12, #0x04]
            0xE59C3008u, // ldr   r3,  [r12, #0x08]
            0xE12FFF33u, // blx   r3
            0xE8BD5FFFu, // ldmia sp!, {r0-r12, lr}
            0xE59FC004u, // ldr   r12, [pc, #0x04]
            0xE59C300Cu, // ldr   r3,  [r12, #0x0C]
            0xE12FFF13u, // bx    r3
            kNativeZoomToggleScratchAddr,
        };

        // ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3,
        //                  EU1_0=4, EU1_1=5, KR1_0=6
        static constexpr NativeZoomToggleSite kNativeZoomToggleSites[] = {
            { 0x020BE790u, 0x02015C98u, 0x02024174u },
            { 0x020BE750u, 0x02015C98u, 0x02024174u },
            { 0x020BCA70u, 0x02015CB8u, 0x02024198u },
            { 0x020BD2D0u, 0x02015CBCu, 0x02024198u },
            { 0x020BD2F0u, 0x02015CB0u, 0x02024190u },
            { 0x020BD370u, 0x02015CBCu, 0x02024198u },
            { 0x020B6240u, 0x0201CEBCu, 0x0200D07Cu },
        };

        [[nodiscard]] static FORCE_INLINE const NativeZoomToggleSite*
        NativeZoomToggle_GetSite(uint8_t romGroupIndex) noexcept
        {
            if (romGroupIndex >= sizeof(kNativeZoomToggleSites) / sizeof(kNativeZoomToggleSites[0]))
                return nullptr;
            return &kNativeZoomToggleSites[romGroupIndex];
        }

        [[nodiscard]] static FORCE_INLINE bool NativeZoomToggle_IsMainRamRange(
            uint32_t address,
            uint32_t size) noexcept
        {
            return size != 0
                && address >= 0x02000000u
                && size - 1u <= 0x023FFFFFu - address;
        }

        [[nodiscard]] static FORCE_INLINE bool NativeZoomToggle_Read8(
            melonDS::NDS* nds,
            uint32_t address,
            uint8_t& out) noexcept
        {
            if (!nds || !NativeZoomToggle_IsMainRamRange(address, sizeof(out)))
                return false;
            out = Read8(nds->MainRAM, address);
            return true;
        }

        [[nodiscard]] static FORCE_INLINE bool NativeZoomToggle_Read32(
            melonDS::NDS* nds,
            uint32_t address,
            uint32_t& out) noexcept
        {
            if (!nds || !NativeZoomToggle_IsMainRamRange(address, sizeof(out)))
                return false;
            out = Read32(nds->MainRAM, address);
            return true;
        }

        [[nodiscard]] static bool NativeZoomToggle_EnsureTrampoline(melonDS::NDS* nds)
        {
            if (!nds)
                return false;
            if (!NativeZoomToggle_IsMainRamRange(
                    kNativeZoomToggleTrampolineAddr,
                    static_cast<uint32_t>(sizeof(kNativeZoomToggleTrampolineCode))))
            {
                return false;
            }
            if (!NativeZoomToggle_IsMainRamRange(kNativeZoomToggleScratchAddr, 16u))
                return false;

            for (uint32_t i = 0; i < sizeof(kNativeZoomToggleTrampolineCode) / sizeof(kNativeZoomToggleTrampolineCode[0]); ++i) {
                const uint32_t addr = kNativeZoomToggleTrampolineAddr + i * sizeof(uint32_t);
                const uint32_t word = kNativeZoomToggleTrampolineCode[i];
                if (nds->ARM9Read32(addr) != word)
                    nds->ARM9Write32(addr, word);
            }
            return true;
        }

        static FORCE_INLINE void NativeZoomToggle_WriteScratch(
            melonDS::NDS* nds,
            uint32_t player,
            uint8_t enabled,
            uint32_t setter,
            uint32_t returnAddr)
        {
            Write32(nds->MainRAM, kNativeZoomToggleScratchAddr + kNativeZoomScratchPlayer, player);
            Write32(nds->MainRAM, kNativeZoomToggleScratchAddr + kNativeZoomScratchEnabled, enabled ? 1u : 0u);
            Write32(nds->MainRAM, kNativeZoomToggleScratchAddr + kNativeZoomScratchSetter, setter);
            Write32(nds->MainRAM, kNativeZoomToggleScratchAddr + kNativeZoomScratchReturnAddr, returnAddr);
        }

        [[nodiscard]] static FORCE_INLINE bool NativeZoomToggle_IsZoomEnabled(
            melonDS::NDS* nds,
            uint32_t player)
        {
            uint8_t scopeFlags = 0;
            return NativeZoomToggle_Read8(nds, player + kPlayerScopeFlagsOffset, scopeFlags)
                && (scopeFlags & kScopeZoomEnabledBit) != 0;
        }

        [[nodiscard]] static FORCE_INLINE bool NativeZoomToggle_IsCurrentWeaponZoomCapable(
            melonDS::NDS* nds,
            uint32_t player)
        {
            uint32_t weapon = 0;
            uint32_t flags = 0;
            return NativeZoomToggle_Read32(nds, player + kPlayerCurrentWeaponPtrOffset, weapon)
                && NativeZoomToggle_IsMainRamRange(weapon, 0x5Cu)
                && NativeZoomToggle_Read32(nds, weapon + kWeaponFlagsOffset, flags)
                && (flags & kWeaponFlagZoomCapable) != 0;
        }

        [[nodiscard]] static FORCE_INLINE uint32_t NativeZoomToggle_GetLocalPlayer(
            melonDS::NDS* nds,
            const NativeZoomToggleSite& site)
        {
            uint32_t player = 0;
            if (!NativeZoomToggle_Read32(nds, site.LocalPlayerPtrGlobal, player))
                return 0;
            return NativeZoomToggle_IsMainRamRange(player, 0x900u) ? player : 0;
        }

    } // anonymous namespace

    uint32_t MelonPrimeCore::NativeZoomToggleHook_GetAddresses(
        uint8_t romGroupIndex,
        uint32_t* out,
        uint32_t maxCount)
    {
        if (!out || maxCount == 0)
            return 0;

        const NativeZoomToggleSite* const site = NativeZoomToggle_GetSite(romGroupIndex);
        if (!site)
            return 0;

        out[0] = site->WeaponActionUpdate;
        return 1;
    }

    HOT_FUNCTION void MelonPrimeCore::UpdateNativeZoomToggleInput()
    {
        // ApplyZoomBindingInput already gates on m_enableNativeZoomToggle, and
        // ApplyConfigReload clears m_nativeZoomPending / m_nativeZoomTogglePrevDown
        // when the feature is toggled off, so the inner !enableNativeZoomToggle
        // branch was dead.
        const bool down = IsDown(IB_ZOOM);
        const bool pressedEdge = down && !m_nativeZoomTogglePrevDown;
        m_nativeZoomTogglePrevDown = down;

        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT)
            || !m_flags.test(StateFlags::BIT_LAST_FOCUSED)
            || (m_aimBlockBits & AIMBLK_NOT_IN_GAME))
        {
            return;
        }

        if (m_nativeZoomPending.Pending)
            return;

        // Hot-path skip: when zoom is known disabled and the player did not
        // just press the zoom edge, the auto-disable check has nothing to do.
        // The game cannot flip zoom from disabled→enabled on its own (only
        // the edge-triggered toggle below or our trampoline writes do that),
        // so skipping the IsZoomEnabled MainRAM read here is sound.
        if (!pressedEdge && !m_nativeZoomLastKnownEnabled)
            return;

        melonDS::NDS* const nds = emuInstance->getNDS();
        const NativeZoomToggleSite* const site =
            NativeZoomToggle_GetSite(m_currentRom.romGroupIndex);
        if (!nds || !site)
            return;

        const uint32_t player = NativeZoomToggle_GetLocalPlayer(nds, *site);
        if (player == 0)
            return;

        const bool currentEnabled = NativeZoomToggle_IsZoomEnabled(nds, player);
        m_nativeZoomLastKnownEnabled = currentEnabled;

        if (!pressedEdge) {
            if (currentEnabled && !NativeZoomToggle_IsCurrentWeaponZoomCapable(nds, player)) {
                m_nativeZoomPending.FunctionAddr = site->ScopeZoomSetter;
                m_nativeZoomPending.Player = player;
                m_nativeZoomPending.Enabled = 0;
                m_nativeZoomPending.Pending = true;
                m_nativeZoomLastKnownEnabled = false; // we are about to disable
            }
            return;
        }

        const bool desiredEnabled = !currentEnabled;
        if (desiredEnabled && !NativeZoomToggle_IsCurrentWeaponZoomCapable(nds, player))
            return;

        m_nativeZoomPending.FunctionAddr = site->ScopeZoomSetter;
        m_nativeZoomPending.Player = player;
        m_nativeZoomPending.Enabled = desiredEnabled ? 1u : 0u;
        m_nativeZoomPending.Pending = true;
        m_nativeZoomLastKnownEnabled = desiredEnabled;
    }

    bool MelonPrimeCore::NativeZoomToggleHook_DispatchCheckAndRedirect(
        melonDS::NDS* nds,
        uint32_t arm9ExecAddr,
        uint32_t regs[16],
        uint32_t& redirectExecAddr)
    {
        (void)regs;
        redirectExecAddr = 0;

        if (!nds || !m_enableNativeZoomToggle || !m_nativeZoomPending.Pending)
            return false;

        const NativeZoomToggleSite* const site =
            NativeZoomToggle_GetSite(m_currentRom.romGroupIndex);
        if (!site || arm9ExecAddr != site->WeaponActionUpdate)
            return false;

        if (m_nativeZoomPending.FunctionAddr != site->ScopeZoomSetter
            || !NativeZoomToggle_IsMainRamRange(m_nativeZoomPending.Player, 0x900u))
        {
            m_nativeZoomPending.Clear();
            return false;
        }

        if (!NativeZoomToggle_EnsureTrampoline(nds))
            return false;

        NativeZoomToggle_WriteScratch(
            nds,
            m_nativeZoomPending.Player,
            m_nativeZoomPending.Enabled,
            site->ScopeZoomSetter,
            site->WeaponActionUpdate);

        m_nativeZoomPending.Clear();
        redirectExecAddr = kNativeZoomToggleTrampolineAddr;
        return true;
    }

#else

    HOT_FUNCTION void MelonPrimeCore::UpdateNativeZoomToggleInput()
    {
    }

#endif // MELONPRIME_DS
