#ifdef MELONPRIME_DS

    namespace {

        // Hook at the "cmp r0,#0" instruction that immediately follows the
        // call to the input binding helper (02028EE8 etc.).
        //
        // Flow inside each transform gate:
        //   gate_cmp_addr - 0x0C:  ldr r1,[playerReg,#0x3A0] ; morph binding (normally 0)
        //   gate_cmp_addr - 0x08:  set r0 to input state
        //   gate_cmp_addr - 0x04:  bl  input_helper       ; returns 0 (binding=0)
        //   gate_cmp_addr + 0x00:  cmp r0,#0             ; ← HOOK HERE
        //   gate_cmp_addr + 0x04:  bne  <transform call>  ; fires if r0 != 0
        //
        // When an emulator-side transform request is pending for the local
        // player, redirect to the same native call path the bne would have
        // taken. This avoids relying on player+0x3A0, which is normally zero,
        // and lets TransformRequest run every original state guard and
        // hunter-specific setup.

        struct TransformGateEntry
        {
            uint32_t CmpAddr;           // hook address (cmp r0,#0 instruction)
            uint32_t TransformCallAddr; // native "mov r0,player; mov r1,#0; bl TransformRequest"
            uint32_t LocalPlayerPtrAddr; // pointer-to-pointer to local player
            uint8_t  PlayerReg;         // register holding player ptr at this point
        };

        // ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3,
        //                  EU1_0=4, EU1_1=5, KR1_0=6
        // CmpAddr = GateEntryAddr + 0x0C; TransformCallAddr = GateEntryAddr + 0x44.
        static constexpr TransformGateEntry kTransformGateEntries[][2] = {
            {   // JP1_0
                { 0x02023B3Cu, 0x02023B74u, LIST_HookLocalPlayerPtrGlobal[0], 5 },   // Gate A
                { 0x02025F94u, 0x02025FCCu, LIST_HookLocalPlayerPtrGlobal[0], 10 },  // Gate B
            },
            {   // JP1_1
                { 0x02023B3Cu, 0x02023B74u, LIST_HookLocalPlayerPtrGlobal[1], 5 },
                { 0x02025F94u, 0x02025FCCu, LIST_HookLocalPlayerPtrGlobal[1], 10 },
            },
            {   // US1_0
                { 0x02023B60u, 0x02023B98u, LIST_HookLocalPlayerPtrGlobal[2], 5 },
                { 0x02025FB8u, 0x02025FF0u, LIST_HookLocalPlayerPtrGlobal[2], 10 },
            },
            {   // US1_1
                { 0x02023B60u, 0x02023B98u, LIST_HookLocalPlayerPtrGlobal[3], 5 },
                { 0x02025FB8u, 0x02025FF0u, LIST_HookLocalPlayerPtrGlobal[3], 10 },
            },
            {   // EU1_0
                { 0x02023B58u, 0x02023B90u, LIST_HookLocalPlayerPtrGlobal[4], 5 },
                { 0x02025FB0u, 0x02025FE8u, LIST_HookLocalPlayerPtrGlobal[4], 10 },
            },
            {   // EU1_1
                { 0x02023B60u, 0x02023B98u, LIST_HookLocalPlayerPtrGlobal[5], 5 },
                { 0x02025FB8u, 0x02025FF0u, LIST_HookLocalPlayerPtrGlobal[5], 10 },
            },
            {   // KR1_0
                { 0x02011598u, 0x020115D0u, LIST_HookLocalPlayerPtrGlobal[6], 7 },
                { 0x0200EE54u, 0x0200EE8Cu, LIST_HookLocalPlayerPtrGlobal[6], 10 },
            },
        };
        static_assert(
            sizeof(kTransformGateEntries) / sizeof(kTransformGateEntries[0])
                == static_cast<uint32_t>(RomGroup::COUNT),
            "Transform gate hook table must cover every ROM group");
        static constexpr bool TransformGate_SitesInMainRam() noexcept
        {
            for (const auto& group : kTransformGateEntries) {
                for (const auto& entry : group) {
                    if (!RomAddrDetail::InMainRam(entry.CmpAddr)
                        || !RomAddrDetail::InMainRam(entry.TransformCallAddr)
                        || !RomAddrDetail::InMainRam(entry.LocalPlayerPtrAddr))
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        static_assert(TransformGate_SitesInMainRam(),
                      "Transform gate hook table contains a non-main-RAM address");

        [[nodiscard]] static FORCE_INLINE bool TransformGate_IsMainRamRange(
            uint32_t address,
            uint32_t size) noexcept
        {
            return size != 0
                && address >= 0x02000000u
                && size - 1u <= 0x023FFFFFu - address;
        }

        [[nodiscard]] static FORCE_INLINE bool TransformGate_ReadMainRam32(
            melonDS::NDS* nds, uint32_t address, uint32_t& out) noexcept
        {
            if (!nds || !TransformGate_IsMainRamRange(address, sizeof(out)))
                return false;
            std::memcpy(&out, nds->MainRAM + (address & 0x3FFFFFu), sizeof(out));
            return true;
        }

        [[nodiscard]] static FORCE_INLINE const TransformGateEntry* TransformGate_Find(
            uint8_t romGroupIndex,
            uint32_t arm9ExecAddr) noexcept
        {
            if (romGroupIndex >= sizeof(kTransformGateEntries) / sizeof(kTransformGateEntries[0]))
                return nullptr;

            for (const auto& entry : kTransformGateEntries[romGroupIndex]) {
                if (entry.CmpAddr == arm9ExecAddr)
                    return &entry;
            }
            return nullptr;
        }

    } // anonymous namespace

    uint32_t MelonPrimeCore::TransformGateHook_GetAddresses(
        uint8_t romGroupIndex,
        uint32_t* out,
        uint32_t maxCount)
    {
        if (!out || maxCount == 0)
            return 0;
        if (romGroupIndex >= sizeof(kTransformGateEntries) / sizeof(kTransformGateEntries[0]))
            return 0;

        uint32_t count = 0;
        for (const auto& entry : kTransformGateEntries[romGroupIndex]) {
            if (entry.CmpAddr == 0)
                continue;
            if (count >= maxCount)
                break;
            out[count++] = entry.CmpAddr;
        }
        return count;
    }

    bool MelonPrimeCore::TransformGateHook_DispatchCheckAndRedirect(
        melonDS::NDS* nds,
        uint32_t arm9ExecAddr,
        uint32_t regs[16],
        uint32_t& redirectExecAddr)
    {
        redirectExecAddr = 0;

        // ARM9Hook_Install gates registration on m_enableDirectAltFormTransform,
        // so the runtime check here is dead.
        if (!nds || !regs)
            return false;
        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT))
            return false;
        if (!m_flags.test(StateFlags::BIT_LAST_FOCUSED))
            return false;
        if (m_aimBlockBits & AIMBLK_NOT_IN_GAME)
            return false;

        const TransformGateEntry* const entry =
            TransformGate_Find(m_currentRom.romGroupIndex, arm9ExecAddr);
        if (!entry)
            return false;

        // Only inject for the local player.
        const uint32_t playerInRegs = regs[entry->PlayerReg];
        if (!TransformGate_IsMainRamRange(playerInRegs, 0x4CCu))
            return false;

        uint32_t localPlayer = 0;
        if (!TransformGate_ReadMainRam32(nds, entry->LocalPlayerPtrAddr, localPlayer))
            return false;
        if (playerInRegs != localPlayer)
            return false;

        // The request is held for a few frames by HandleRareMorph so a host
        // edge is not lost when the game skips this gate on the exact press
        // frame. Consume it here so Gate A and Gate B cannot both dispatch it.
        if (m_directTransformPendingFrames == 0 && !(m_input.press & IB_MORPH))
            return false;
        m_directTransformPendingFrames = 0;
        m_input.press &= ~IB_MORPH;

        redirectExecAddr = entry->TransformCallAddr;
        return true;
    }

#endif // MELONPRIME_DS
