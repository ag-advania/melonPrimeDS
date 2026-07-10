#ifdef MELONPRIME_DS

    namespace {

        struct NativeAimDeltaHookInfo {
            uint32_t hookPc;
            uint8_t playerReg;
            uint8_t horizontalReg;
            uint8_t verticalReg;
            uint32_t localPlayerPtrAddr;
            bool isAltFormPath;
        };

        // ROM group order:
        // JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
        static constexpr NativeAimDeltaHookInfo kNativeAimDeltaHooks[][2] = {
            {
                { 0x02024318u, 10, 4, 6, LIST_HookLocalPlayerPtrGlobal[0], false },
                { 0x020220F8u,  5, 6, 7, LIST_HookLocalPlayerPtrGlobal[0], true  },
            },
            {
                { 0x02024318u, 10, 4, 6, LIST_HookLocalPlayerPtrGlobal[1], false },
                { 0x020220F8u,  5, 6, 7, LIST_HookLocalPlayerPtrGlobal[1], true  },
            },
            {
                { 0x0202433Cu, 10, 4, 6, LIST_HookLocalPlayerPtrGlobal[2], false },
                { 0x0202211Cu,  5, 6, 7, LIST_HookLocalPlayerPtrGlobal[2], true  },
            },
            {
                { 0x0202433Cu, 10, 4, 6, LIST_HookLocalPlayerPtrGlobal[3], false },
                { 0x0202211Cu,  5, 6, 7, LIST_HookLocalPlayerPtrGlobal[3], true  },
            },
            {
                { 0x02024334u, 10, 4, 6, LIST_HookLocalPlayerPtrGlobal[4], false },
                { 0x02022114u,  5, 6, 7, LIST_HookLocalPlayerPtrGlobal[4], true  },
            },
            {
                { 0x0202433Cu, 10, 4, 6, LIST_HookLocalPlayerPtrGlobal[5], false },
                { 0x0202211Cu,  5, 6, 7, LIST_HookLocalPlayerPtrGlobal[5], true  },
            },
            {
                { 0x0200D208u, 10, 5, 6, LIST_HookLocalPlayerPtrGlobal[6], false },
                { 0x0200FBA0u,  7, 9, 8, LIST_HookLocalPlayerPtrGlobal[6], true  },
            },
        };

        // ── AltForm VectorSteer input write (spec108 == 0: Samus/Kanden/Noxus/Spire) ──
        //
        // Fires at the VectorSteer block entry, immediately after the spec108==0
        // branch falls through.  Writes scaled m_nativeAimDeltaX/Y to
        // input+0x2A/+0x2C so the subsequent ldrsh instructions read our delta
        // without any register injection into the movement solver.
        // The write path stays in game input memory; gain/clamp controls aim strength.

        struct AltFormVSteerInputHook {
            uint32_t pc;               // first instr after spec108==0 bne (entry confirmed)
            uint8_t  playerReg;        // register holding player ptr at this PC
            uint32_t localPlayerPtrAddr;
        };

        static constexpr AltFormVSteerInputHook kAltFormVSteerInputHooks[] = {
            { 0x02021DACu, 5u, LIST_HookLocalPlayerPtrGlobal[0] }, // JP1_0
            { 0x02021DACu, 5u, LIST_HookLocalPlayerPtrGlobal[1] }, // JP1_1
            { 0x02021DD0u, 5u, LIST_HookLocalPlayerPtrGlobal[2] }, // US1_0
            { 0x02021DD0u, 5u, LIST_HookLocalPlayerPtrGlobal[3] }, // US1_1
            { 0x02021DC8u, 5u, LIST_HookLocalPlayerPtrGlobal[4] }, // EU1_0
            { 0x02021DD0u, 5u, LIST_HookLocalPlayerPtrGlobal[5] }, // EU1_1
            { 0x0200F884u, 7u, LIST_HookLocalPlayerPtrGlobal[6] }, // KR1_0
        };

        // ── AltForm StoredYaw / StoredPitch (transition fallback) ────────────
        //
        // Fires in the spec108 != 0 path (Trace/Sylux/Weavel) during
        // biped↔altform transformation when NativeAimDeltaHook is suppressed by
        // IsPlayerTransforming().  Injects scaled delta into r1 before the bl
        // to update player+0xE4/+0xE8 (stored aim).

        struct AltStoredHookSet {
            uint8_t  playerReg;
            uint32_t localPlayerPtrAddr;
            uint32_t yawPc;
            uint32_t pitchPc;
        };

        static constexpr AltStoredHookSet kAltStoredHooks[] = {
            { 5u, LIST_HookLocalPlayerPtrGlobal[0], 0x020221D0u, 0x020221DCu }, // JP1_0
            { 5u, LIST_HookLocalPlayerPtrGlobal[1], 0x020221D0u, 0x020221DCu }, // JP1_1
            { 5u, LIST_HookLocalPlayerPtrGlobal[2], 0x020221F4u, 0x02022200u }, // US1_0
            { 5u, LIST_HookLocalPlayerPtrGlobal[3], 0x020221F4u, 0x02022200u }, // US1_1
            { 5u, LIST_HookLocalPlayerPtrGlobal[4], 0x020221ECu, 0x020221F8u }, // EU1_0
            { 5u, LIST_HookLocalPlayerPtrGlobal[5], 0x020221F4u, 0x02022200u }, // EU1_1
            { 7u, LIST_HookLocalPlayerPtrGlobal[6], 0x0200FC78u, 0x0200FC84u }, // KR1_0
        };

        static_assert(
            sizeof(kAltFormVSteerInputHooks) / sizeof(kAltFormVSteerInputHooks[0])
                == sizeof(kNativeAimDeltaHooks) / sizeof(kNativeAimDeltaHooks[0]),
            "kAltFormVSteerInputHooks ROM count mismatch");
        static_assert(
            sizeof(kAltStoredHooks) / sizeof(kAltStoredHooks[0])
                == sizeof(kNativeAimDeltaHooks) / sizeof(kNativeAimDeltaHooks[0]),
            "kAltStoredHooks ROM count mismatch");
        static_assert(
            sizeof(kNativeAimDeltaHooks) / sizeof(kNativeAimDeltaHooks[0])
                == static_cast<uint32_t>(RomGroup::COUNT),
            "Native aim-delta hook table must cover every ROM group");
        static constexpr bool NativeAimDeltaHook_SitesInMainRam() noexcept
        {
            for (const auto& group : kNativeAimDeltaHooks) {
                for (const auto& info : group) {
                    if (!RomAddrDetail::InMainRam(info.hookPc)
                        || !RomAddrDetail::InMainRam(info.localPlayerPtrAddr))
                    {
                        return false;
                    }
                }
            }
            for (const auto& hook : kAltFormVSteerInputHooks) {
                if (!RomAddrDetail::InMainRam(hook.pc)
                    || !RomAddrDetail::InMainRam(hook.localPlayerPtrAddr))
                {
                    return false;
                }
            }
            for (const auto& hook : kAltStoredHooks) {
                if (!RomAddrDetail::InMainRam(hook.localPlayerPtrAddr)
                    || !RomAddrDetail::InMainRam(hook.yawPc)
                    || !RomAddrDetail::InMainRam(hook.pitchPc))
                {
                    return false;
                }
            }
            return true;
        }
        static_assert(NativeAimDeltaHook_SitesInMainRam(),
                      "Native aim-delta hook table contains a non-main-RAM address");

        [[nodiscard]] static FORCE_INLINE bool NativeAimDeltaHook_IsMainRamRange(
            uint32_t address,
            uint32_t size) noexcept
        {
            return size != 0
                && address >= 0x02000000u
                && size - 1u <= 0x023FFFFFu - address;
        }

        [[nodiscard]] static FORCE_INLINE bool NativeAimDeltaHook_ReadMainRam32(
            melonDS::NDS* nds,
            uint32_t address,
            uint32_t& out) noexcept
        {
            if (!nds || !NativeAimDeltaHook_IsMainRamRange(address, sizeof(out)))
                return false;
            std::memcpy(&out, nds->MainRAM + (address & 0x3FFFFFu), sizeof(out));
            return true;
        }

        [[nodiscard]] static FORCE_INLINE int16_t NativeAimDeltaHook_ClampToS16(
            int32_t value) noexcept
        {
            if (value > 32767)
                return 32767;
            if (value < -32768)
                return -32768;
            return static_cast<int16_t>(value);
        }

        [[nodiscard]] static FORCE_INLINE int16_t NativeAimDeltaHook_ClampScaledS16(
            int32_t value,
            int32_t gain,
            int32_t limit) noexcept
        {
            int64_t scaled = static_cast<int64_t>(value) * static_cast<int64_t>(gain);

            if (scaled > limit)
                scaled = limit;
            else if (scaled < -limit)
                scaled = -limit;

            return static_cast<int16_t>(scaled);
        }

        [[nodiscard]] static FORCE_INLINE const NativeAimDeltaHookInfo* NativeAimDeltaHook_Find(
            uint8_t romGroupIndex,
            uint32_t arm9ExecAddr) noexcept
        {
            if (romGroupIndex >= sizeof(kNativeAimDeltaHooks) / sizeof(kNativeAimDeltaHooks[0]))
                return nullptr;

            for (const auto& info : kNativeAimDeltaHooks[romGroupIndex]) {
                if (info.hookPc == arm9ExecAddr)
                    return &info;
            }
            return nullptr;
        }

    } // anonymous namespace

    uint32_t MelonPrimeCore::NativeAimDeltaHookRegisterInjection_GetAddresses(
        uint8_t romGroupIndex,
        uint32_t* out,
        uint32_t maxCount)
    {
        if (!out || maxCount == 0)
            return 0;
        if (romGroupIndex >= sizeof(kNativeAimDeltaHooks) / sizeof(kNativeAimDeltaHooks[0]))
            return 0;

        uint32_t count = 0;
        const auto push = [&](uint32_t pc) {
            if (count < maxCount) out[count++] = pc;
        };

        for (const auto& info : kNativeAimDeltaHooks[romGroupIndex])
            push(info.hookPc);

        // AltForm spec108==0 input write (Samus/Kanden/Noxus/Spire)
        push(kAltFormVSteerInputHooks[romGroupIndex].pc);

        // AltForm StoredYaw / StoredPitch (transition fallback)
        push(kAltStoredHooks[romGroupIndex].yawPc);
        push(kAltStoredHooks[romGroupIndex].pitchPc);

        return count;
    }

    void MelonPrimeCore::NativeAimDeltaHookRegisterInjection_DispatchCheck(
        melonDS::NDS* nds,
        uint32_t arm9ExecAddr,
        uint32_t regs[16])
    {
        // ARM9Hook_Install gates registration on m_enableNativeAimDeltaHook
        // (nativeAimHookMode == 1) AND m_disableMphAimSmoothing AND a valid
        // romGroupIndex, so those runtime checks are dead. Stylus mode is
        // dynamic but ARM9Hook_Install does not gate on it, so isStylusMode
        // is still checked. localPlayerPtrAddr values come from constant tables
        // (always inside main RAM), so their IsMainRamRange checks are dead.
        if (!nds || !regs)
            return;
        if (isStylusMode)
            return;
        if ((m_aimBlockBits | static_cast<uint32_t>(m_isLayoutChangePending)) != 0)
            return;
        if (!m_flags.test(StateFlags::BIT_LAST_FOCUSED))
            return;

        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT))
            return;

        const uint8_t romIdx = m_currentRom.romGroupIndex;
        uint8_t* const mainRAM = nds->MainRAM;

        // Dispatch by frequency. Register injection (Normal / AltInput
        // spec108=1) fires every aim update during normal first-person play
        // and is by far the most common path; check it first via the small
        // 2-entry table so the hot case takes the LIKELY branch.
        const NativeAimDeltaHookInfo* const info =
            NativeAimDeltaHook_Find(romIdx, arm9ExecAddr);
        if (LIKELY(info != nullptr)) {
            if (IsPlayerTransforming())
                return;
            if (IsPlayerAltForm() != info->isAltFormPath)
                return;

            const uint32_t player = regs[info->playerReg];
            uint32_t localPlayer = 0;
            std::memcpy(&localPlayer,
                        mainRAM + (info->localPlayerPtrAddr & 0x3FFFFFu),
                        sizeof(localPlayer));
            if (player != localPlayer)
                return;
            if (!NativeAimDeltaHook_IsMainRamRange(player, 0x400u))
                return;

            int32_t outX = m_nativeAimDeltaX;
            int32_t outY = m_nativeAimDeltaY;
            m_nativeAimDeltaX = 0;
            m_nativeAimDeltaY = 0;

#ifdef _WIN32
            int lateX = 0;
            int lateY = 0;
            if (m_rawFilter)
                m_rawFilter->LateLatchMouseDelta(lateX, lateY);

            if ((lateX | lateY) != 0)
            {
                if (UNLIKELY(m_enableZoomAimScale))
                    UpdateZoomAimEffectiveScale();

                const bool hasLateX = lateX != 0;
                const bool hasLateY = lateY != 0;
                int64_t resX = m_aimResidualX + static_cast<int64_t>(lateX) * m_aimEffectiveFixedScaleX;
                int64_t resY = m_aimResidualY + static_cast<int64_t>(lateY) * m_aimEffectiveFixedScaleY;
                resX = ClampAimResidual(resX, AIM_MAX_RESIDUAL);
                resY = ClampAimResidual(resY, AIM_MAX_RESIDUAL);

                // Match the main accumulator path: truncate signed fixed-point
                // toward zero and only emit on an axis that received fresh input.
                const int16_t lateOutX = hasLateX
                    ? static_cast<int16_t>(AimFixedToIntTowardZero(resX, AIM_DIRECT_BITS))
                    : 0;
                const int16_t lateOutY = hasLateY
                    ? static_cast<int16_t>(AimFixedToIntTowardZero(resY, AIM_DIRECT_BITS))
                    : 0;

                if ((lateOutX | lateOutY) != 0)
                {
                    if (hasLateX)
                        resX -= static_cast<int64_t>(lateOutX) << AIM_DIRECT_BITS;
                    if (hasLateY)
                        resY -= static_cast<int64_t>(lateOutY) << AIM_DIRECT_BITS;

                    outX += lateOutX;
                    outY += lateOutY;

                }

                // The OFF setting must never carry a fractional remainder, even
                // when the late delta was too small to produce an integer output.
                if (!m_enableAimAccumulator)
                {
                    resX = 0;
                    resY = 0;
                }
                m_aimResidualX = resX;
                m_aimResidualY = resY;
            }
#endif

            if ((outX | outY) == 0)
                return;

            const int16_t finalX = NativeAimDeltaHook_ClampToS16(outX);
            const int16_t finalY = NativeAimDeltaHook_ClampToS16(outY);

            // player+0x3F8/+0x3FC are inside the player+0x400 range we just
            // verified, so skip the redundant per-read bounds check.
            uint32_t sensXRaw = 0;
            uint32_t sensYRaw = 0;
            std::memcpy(&sensXRaw, mainRAM + ((player + 0x3F8u) & 0x3FFFFFu), sizeof(sensXRaw));
            std::memcpy(&sensYRaw, mainRAM + ((player + 0x3FCu) & 0x3FFFFFu), sizeof(sensYRaw));

            const int32_t sensX = static_cast<int32_t>(sensXRaw);
            const int32_t sensY = static_cast<int32_t>(sensYRaw);
            const int64_t horizontalScaled = static_cast<int64_t>(finalX) * static_cast<int64_t>(sensX);
            const int64_t verticalScaled   = static_cast<int64_t>(finalY) * static_cast<int64_t>(sensY);

            regs[info->horizontalReg] = static_cast<uint32_t>(horizontalScaled);
            regs[info->verticalReg]   = static_cast<uint32_t>(verticalScaled);
            return;
        }

        // Rare paths (VSteer + AltStored) only do work with a non-zero delta;
        // gate both at once before any address compare or RAM read.
        if ((m_nativeAimDeltaX | m_nativeAimDeltaY) == 0)
            return;

        // ── AltForm VectorSteer input write (spec108==0: Samus/Kanden/Noxus/Spire) ──
        {
            const auto& vi = kAltFormVSteerInputHooks[romIdx];
            if (arm9ExecAddr == vi.pc) {
                uint32_t localPlayer = 0;
                std::memcpy(&localPlayer,
                            mainRAM + (vi.localPlayerPtrAddr & 0x3FFFFFu),
                            sizeof(localPlayer));
                const uint32_t player = regs[vi.playerReg];
                if (player != localPlayer)
                    return;

                const uint32_t addrH = player + 0x464u + 0x2Au;
                const uint32_t addrV = player + 0x464u + 0x2Cu;
                if (!NativeAimDeltaHook_IsMainRamRange(addrH, 2u)
                    || !NativeAimDeltaHook_IsMainRamRange(addrV, 2u))
                    return;

                static constexpr int32_t kVSteerGain  = 8;
                static constexpr int32_t kVSteerClamp = 128;

                const int16_t dx = NativeAimDeltaHook_ClampScaledS16(
                    m_nativeAimDeltaX, kVSteerGain, kVSteerClamp);
                const int16_t dy = NativeAimDeltaHook_ClampScaledS16(
                    m_nativeAimDeltaY, kVSteerGain, kVSteerClamp);
                std::memcpy(mainRAM + (addrH & 0x3FFFFFu), &dx, sizeof(dx));
                std::memcpy(mainRAM + (addrV & 0x3FFFFFu), &dy, sizeof(dy));
                m_nativeAimDeltaX = 0;
                m_nativeAimDeltaY = 0;
                return;
            }
        }

        // ── AltForm StoredYaw / StoredPitch (transition fallback) ────────────
        // Fires during biped↔altform transformation (IsPlayerTransforming==true),
        // when the register injection path is suppressed. No IsPlayerTransforming
        // guard here so this block is reachable in that state.
        {
            const auto& as = kAltStoredHooks[romIdx];
            const bool isYaw   = (arm9ExecAddr == as.yawPc);
            const bool isPitch = (arm9ExecAddr == as.pitchPc);
            if (!(isYaw || isPitch))
                return;

            uint32_t localPlayer = 0;
            std::memcpy(&localPlayer,
                        mainRAM + (as.localPlayerPtrAddr & 0x3FFFFFu),
                        sizeof(localPlayer));
            const uint32_t player = regs[as.playerReg];
            if (player != localPlayer)
                return;
            if (!NativeAimDeltaHook_IsMainRamRange(player + 0x3F8u, 8u))
                return;

            uint32_t sensXRaw = 0, sensYRaw = 0;
            std::memcpy(&sensXRaw, mainRAM + ((player + 0x3F8u) & 0x3FFFFFu), sizeof(sensXRaw));
            std::memcpy(&sensYRaw, mainRAM + ((player + 0x3FCu) & 0x3FFFFFu), sizeof(sensYRaw));

            if (isYaw) {
                regs[1] = static_cast<uint32_t>(
                    static_cast<int64_t>(m_nativeAimDeltaX) * static_cast<int32_t>(sensXRaw));
            } else {
                regs[1] = static_cast<uint32_t>(
                    static_cast<int64_t>(m_nativeAimDeltaY) * static_cast<int32_t>(sensYRaw));
                m_nativeAimDeltaX = 0;
                m_nativeAimDeltaY = 0;
            }
        }
    }

#endif // MELONPRIME_DS
