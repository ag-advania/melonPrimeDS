#ifdef MELONPRIME_DS

    namespace {

        // ROM group order:
        // JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
        //
        // Hook at the action consumer entry.  Fires AFTER the input poll has written
        // fresh values into player+0x464, and BEFORE any action handler (morph check,
        // fire gate, jump gate, etc.) reads from it.
        static constexpr uint32_t kImmediateOverlayActionConsumerPc[] = {
            0x02024174u,  // JP1_0
            0x02024174u,  // JP1_1
            0x02024198u,  // US1_0
            0x02024198u,  // US1_1
            0x02024190u,  // EU1_0
            0x02024198u,  // EU1_1
            0x0200F6DCu,  // KR1_0
        };

        // ── AltForm VectorSteer hooks (spec108 == 0: Samus/Kanden/Noxus/Spire) ──
        //
        // The game reads raw aim delta from input+0x2C (vertical) and input+0x2A
        // (horizontal) via ldrsh, then scales by player+0x6F4/+0x6F8/+0x6FC/+0x700.
        //
        // Hooks fire PRE-execution.  To intercept after the ldrsh has loaded the
        // delta, we hook at PC+4 (the instruction immediately following each ldrsh).
        // At that point the destination register already holds the game-loaded value,
        // which we replace with our pending mouse delta.
        //
        // Injection order: V1 → V2 → H1 → H2.
        // V1/V2 inject m_nativeAimDeltaY (vertical), H1/H2 inject m_nativeAimDeltaX.
        // The delta is NOT consumed at V1/V2/H1 so all four reads reuse the same
        // pending value.  It is consumed (zeroed) after H2.
        //
        // VectorSteer uses its own player+0x6F4/+0x6FC scaling, so we inject the raw
        // int16 delta directly without pre-multiplying by player+0x3F8/+0x3FC.

        struct VSteerEntry {
            uint32_t pc;        // PC+4 after the ldrsh instruction
            uint8_t  deltaReg;  // register to override (holds game's ldrsh result)
            bool     isVertical;// true → inject m_nativeAimDeltaY, false → X
        };

        struct VSteerSet {
            uint8_t    playerReg;
            uint32_t   localPlayerPtrAddr;
            VSteerEntry v[4]; // V1, V2, H1, H2
        };

        // deltaReg pattern across all versions: V1=r1, V2=r7, H1=r7, H2=r6
        static constexpr VSteerSet kVSteerHooks[] = {
            // JP1_0
            { 5u, 0x020BE790u, {
                { 0x02021E08u, 1u, true  }, // V1
                { 0x02021E48u, 7u, true  }, // V2
                { 0x02021E80u, 7u, false }, // H1
                { 0x02021EB8u, 6u, false }, // H2
            }},
            // JP1_1
            { 5u, 0x020BE750u, {
                { 0x02021E08u, 1u, true  },
                { 0x02021E48u, 7u, true  },
                { 0x02021E80u, 7u, false },
                { 0x02021EB8u, 6u, false },
            }},
            // US1_0
            { 5u, 0x020BCA70u, {
                { 0x02021E2Cu, 1u, true  },
                { 0x02021E6Cu, 7u, true  },
                { 0x02021EA4u, 7u, false },
                { 0x02021EDCu, 6u, false },
            }},
            // US1_1
            { 5u, 0x020BD2D0u, {
                { 0x02021E2Cu, 1u, true  },
                { 0x02021E6Cu, 7u, true  },
                { 0x02021EA4u, 7u, false },
                { 0x02021EDCu, 6u, false },
            }},
            // EU1_0
            { 5u, 0x020BD2F0u, {
                { 0x02021E24u, 1u, true  },
                { 0x02021E64u, 7u, true  },
                { 0x02021E9Cu, 7u, false },
                { 0x02021ED4u, 6u, false },
            }},
            // EU1_1
            { 5u, 0x020BD370u, {
                { 0x02021E2Cu, 1u, true  },
                { 0x02021E6Cu, 7u, true  },
                { 0x02021EA4u, 7u, false },
                { 0x02021EDCu, 6u, false },
            }},
            // KR1_0  (playerReg = r7; deltaReg pattern same as JP/US/EU)
            { 7u, 0x020B6240u, {
                { 0x0200F8C0u, 1u, true  },
                { 0x0200F90Cu, 7u, true  },
                { 0x0200F948u, 7u, false },
                { 0x0200F980u, 6u, false },
            }},
        };

        static_assert(
            sizeof(kVSteerHooks) / sizeof(kVSteerHooks[0])
                == sizeof(kImmediateOverlayActionConsumerPc) / sizeof(kImmediateOverlayActionConsumerPc[0]),
            "kVSteerHooks ROM count mismatch");

        // ── AltForm StoredYaw / StoredPitch hooks (transition fallback) ───────
        //
        // These fire in the spec108 != 0 code path (Trace/Sylux/Weavel) during
        // biped↔altform transition, when NativeAimDeltaHook is suppressed by
        // IsPlayerTransforming().  They update player+0xE4/+0xE8 (stored aim) so
        // the snap-to-aim-position on landing is correct.
        //
        // Both hooks inject scaled delta into r1 before the bl:
        //   yawPc   → r1 = m_nativeAimDeltaX * sensX  (horizontal)
        //   pitchPc → r1 = m_nativeAimDeltaY * sensY  (vertical, then consume)
        //
        // If delta is already zero (NativeAimDeltaHook consumed it in steady
        // AltForm this frame) the hooks are no-ops.

        struct AltStoredSet {
            uint8_t  playerReg;
            uint32_t localPlayerPtrAddr;
            uint32_t yawPc;
            uint32_t pitchPc;
        };

        static constexpr AltStoredSet kAltStoredHooks[] = {
            { 5u, 0x020BE790u, 0x020221D0u, 0x020221DCu }, // JP1_0
            { 5u, 0x020BE750u, 0x020221D0u, 0x020221DCu }, // JP1_1
            { 5u, 0x020BCA70u, 0x020221F4u, 0x02022200u }, // US1_0
            { 5u, 0x020BD2D0u, 0x020221F4u, 0x02022200u }, // US1_1
            { 5u, 0x020BD2F0u, 0x020221ECu, 0x020221F8u }, // EU1_0
            { 5u, 0x020BD370u, 0x020221F4u, 0x02022200u }, // EU1_1
            { 7u, 0x020B6240u, 0x0200FC78u, 0x0200FC84u }, // KR1_0
        };

        static_assert(
            sizeof(kAltStoredHooks) / sizeof(kAltStoredHooks[0])
                == sizeof(kImmediateOverlayActionConsumerPc) / sizeof(kImmediateOverlayActionConsumerPc[0]),
            "kAltStoredHooks ROM count mismatch");

        // ── Shared helpers ───────────────────────────────────────────────────

        [[nodiscard]] static FORCE_INLINE bool ImmediateOverlay_IsMainRamRange(
            uint32_t address, uint32_t size) noexcept
        {
            return size != 0
                && address >= 0x02000000u
                && size - 1u <= 0x023FFFFFu - address;
        }

        [[nodiscard]] static FORCE_INLINE uint16_t ImmediateOverlay_ReadBindingLow(
            const uint8_t* mainRAM, uint32_t addr) noexcept
        {
            uint32_t v = 0;
            std::memcpy(&v, mainRAM + (addr & 0x3FFFFFu), sizeof(v));
            return static_cast<uint16_t>(v & 0xFFFFu);
        }

    } // anonymous namespace

    uint32_t MelonPrimeCore::ImmediateInputEdgeOverlay_GetAddresses(
        uint8_t romGroupIndex,
        uint32_t* out,
        uint32_t maxCount)
    {
        if (!out || maxCount == 0)
            return 0;

        constexpr uint32_t kRomCount =
            sizeof(kImmediateOverlayActionConsumerPc) / sizeof(kImmediateOverlayActionConsumerPc[0]);
        if (romGroupIndex >= kRomCount)
            return 0;

        uint32_t count = 0;
        const auto push = [&](uint32_t pc) {
            if (count < maxCount)
                out[count++] = pc;
        };

        // 1. Action consumer (Fire / Jump / Zoom / Movement)
        push(kImmediateOverlayActionConsumerPc[romGroupIndex]);

        // 2. AltForm VectorSteer: V1, V2, H1, H2
        // DISABLED: 02021E04 block updates movement/steering vectors (sp+0x4/0xC)
        // via player+0x6F4/+0x6F8/+0x6FC/+0x700, not the camera AIM chain
        // (02027798/02027E1C).  Injecting m_nativeAimDeltaX/Y here corrupts
        // collision/movement state and freezes the game regardless of clamp value.
        // spec108==0 hunters fall back to the ProcessAimInputMouse memory write
        // path (*m_ptrs.aimX/Y) which feeds input+0x2A/+0x2C naturally.
        // for (const auto& e : kVSteerHooks[romGroupIndex].v)
        //     push(e.pc);

        // 3. AltForm StoredYaw / StoredPitch (transition fallback)
        push(kAltStoredHooks[romGroupIndex].yawPc);
        push(kAltStoredHooks[romGroupIndex].pitchPc);

        return count;
    }

    void MelonPrimeCore::ImmediateInputEdgeOverlay_DispatchCheck(
        melonDS::NDS* nds,
        uint32_t arm9ExecAddr,
        uint32_t regs[16])
    {
        if (!nds)
            return;

        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT))
            return;
        if (!m_flags.test(StateFlags::BIT_LAST_FOCUSED))
            return;
        if (isStylusMode)
            return;
        if (m_aimBlockBits & AIMBLK_NOT_IN_GAME)
            return;

        const uint8_t romIdx = m_currentRom.romGroupIndex;
        constexpr uint8_t kRomCount =
            sizeof(kImmediateOverlayActionConsumerPc) / sizeof(kImmediateOverlayActionConsumerPc[0]);
        if (romIdx >= kRomCount)
            return;

        uint8_t* const mainRAM = nds->MainRAM;

        // ── Action Consumer (Fire / Jump / Zoom / Movement) ──────────────────
        if (arm9ExecAddr == kImmediateOverlayActionConsumerPc[romIdx]) {
            if (!m_enableImmediateInputEdgeOverlay)
                return;

            const uint32_t offP = static_cast<uint32_t>(m_playerPosition)
                                * static_cast<uint32_t>(Consts::PLAYER_ADDR_INC);
            const uint32_t playerBase = m_currentRom.playerStructStart + offP;

            if (!ImmediateOverlay_IsMainRamRange(playerBase, 0x470u))
                return;

            // Morph / AltForm transform is handled by TransformGateHook, not here.
            // TransformGateHook hooks at Gate A (02023B30 etc.) and redirects
            // execution to the transform call site when IB_MORPH is pressed.
            // No morph injection is needed in this overlay.

            // ── General action bindings (ImmediateInputEdgeOverlay) ──────────
            //   +0x368  Move left         +0x36C  Move right
            //   +0x370  Move forward      +0x374  Move backward
            //   +0x398  Fire              +0x39C  Jump (normal binding path)
            //   +0x3E0  Zoom
            const uint16_t movLMask = ImmediateOverlay_ReadBindingLow(mainRAM, playerBase + 0x368u);
            const uint16_t movRMask = ImmediateOverlay_ReadBindingLow(mainRAM, playerBase + 0x36Cu);
            const uint16_t movFMask = ImmediateOverlay_ReadBindingLow(mainRAM, playerBase + 0x370u);
            const uint16_t movBMask = ImmediateOverlay_ReadBindingLow(mainRAM, playerBase + 0x374u);
            const uint16_t fireMask = ImmediateOverlay_ReadBindingLow(mainRAM, playerBase + 0x398u);
            const uint16_t jumpMask = ImmediateOverlay_ReadBindingLow(mainRAM, playerBase + 0x39Cu);
            const uint16_t zoomMask = ImmediateOverlay_ReadBindingLow(mainRAM, playerBase + 0x3E0u);

            const uint16_t managedMask = static_cast<uint16_t>(
                movLMask | movRMask | movFMask | movBMask |
                fireMask | jumpMask | zoomMask);
            if (managedMask == 0)
                return;

            // Build emulator-side held state for all managed actions.
            const uint64_t down = m_input.down;
            uint16_t injectedHeld = 0;
            if (down & IB_MOVE_L) injectedHeld = static_cast<uint16_t>(injectedHeld | movLMask);
            if (down & IB_MOVE_R) injectedHeld = static_cast<uint16_t>(injectedHeld | movRMask);
            if (down & IB_MOVE_F) injectedHeld = static_cast<uint16_t>(injectedHeld | movFMask);
            if (down & IB_MOVE_B) injectedHeld = static_cast<uint16_t>(injectedHeld | movBMask);
            if (down & IB_SHOOT)  injectedHeld = static_cast<uint16_t>(injectedHeld | fireMask);
            if (down & IB_JUMP)   injectedHeld = static_cast<uint16_t>(injectedHeld | jumpMask);
            if (down & IB_ZOOM)   injectedHeld = static_cast<uint16_t>(injectedHeld | zoomMask);

            const uint16_t prevInjectedHeld = static_cast<uint16_t>(m_immediateOverlayPrevHeld & managedMask);
            const uint16_t injectedPressed  = static_cast<uint16_t>( injectedHeld & ~prevInjectedHeld);
            const uint16_t injectedReleased = static_cast<uint16_t>(~injectedHeld &  prevInjectedHeld);

            // Read game-polled input struct at player+0x464.
            const uint32_t inputBase = playerBase + 0x464u;
            uint16_t current  = 0;
            uint16_t pressed  = 0;
            uint16_t released = 0;
            std::memcpy(&current,  mainRAM + ((inputBase + 0x00u) & 0x3FFFFFu), sizeof(current));
            std::memcpy(&pressed,  mainRAM + ((inputBase + 0x04u) & 0x3FFFFFu), sizeof(pressed));
            std::memcpy(&released, mainRAM + ((inputBase + 0x08u) & 0x3FFFFFu), sizeof(released));

            // Replace only managed bits; leave all others from the game's poll.
            const uint16_t newCurrent  = static_cast<uint16_t>((current  & ~managedMask) | injectedHeld);
            const uint16_t newPressed  = static_cast<uint16_t>((pressed  & ~managedMask) | injectedPressed);
            const uint16_t newReleased = static_cast<uint16_t>((released & ~managedMask) | injectedReleased);

            std::memcpy(mainRAM + ((inputBase + 0x00u) & 0x3FFFFFu), &newCurrent,  sizeof(newCurrent));
            std::memcpy(mainRAM + ((inputBase + 0x04u) & 0x3FFFFFu), &newPressed,  sizeof(newPressed));
            std::memcpy(mainRAM + ((inputBase + 0x08u) & 0x3FFFFFu), &newReleased, sizeof(newReleased));

            m_immediateOverlayPrevHeld = injectedHeld;
            return;
        }

        // ── AltForm VectorSteer (spec108 == 0: Samus/Kanden/Noxus/Spire) ─────
        // Hook registration disabled above; this branch is never reached.
        // Left in place for future re-evaluation once the correct injection
        // point (camera AIM boundary rather than movement vector block) is found.
        if (false && m_enableNativeAimDeltaHook && m_disableMphAimSmoothing) {
            const auto& vs = kVSteerHooks[romIdx];
            for (int i = 0; i < 4; i++) {
                if (vs.v[i].pc != arm9ExecAddr)
                    continue;

                if (!IsPlayerAltForm())
                    return;
                if (!regs)
                    return;

                const uint32_t player = regs[vs.playerReg];
                uint32_t localPlayer = 0;
                if (!ImmediateOverlay_IsMainRamRange(vs.localPlayerPtrAddr, sizeof(localPlayer)))
                    return;
                std::memcpy(&localPlayer, mainRAM + (vs.localPlayerPtrAddr & 0x3FFFFFu), sizeof(localPlayer));
                if (player != localPlayer)
                    return;

                // m_nativeAimDeltaX/Y is the sensitivity-scaled AIM output
                // (resX >> AIM_DIRECT_BITS), typically ±4..±64 for normal mouse use.
                // The VectorSteer path applies its own ~×5 multiplier via r2≈0x5000
                // and then a basis component (~Q12 unit vector, ≈0x1000), so the
                // final stack vector component is roughly rawDelta × 5.
                // Injecting the full AIM delta causes the stack vector to overflow
                // and freeze the game.  Clamp to ±4 so the max component is ±20,
                // which is safe.  Raise this conservatively if steering feels weak.
                static constexpr int32_t kVSteerClamp = 4;
                const int32_t raw = vs.v[i].isVertical
                    ? static_cast<int32_t>(m_nativeAimDeltaY)
                    : static_cast<int32_t>(m_nativeAimDeltaX);
                const int32_t clamped = raw > kVSteerClamp ? kVSteerClamp
                                      : raw < -kVSteerClamp ? -kVSteerClamp
                                      : raw;
                regs[vs.v[i].deltaReg] = static_cast<uint32_t>(clamped);

                // Consume after H2 (index 3 = last in V1→V2→H1→H2 sequence).
                // V1/V2/H1 leave the delta intact so all four reads reuse it.
                if (i == 3) {
                    m_nativeAimDeltaX = 0;
                    m_nativeAimDeltaY = 0;
                }
                return;
            }
        }

        // ── AltForm StoredYaw / StoredPitch (transition fallback) ─────────────
        //
        // Fires in the spec108 != 0 path (Trace/Sylux/Weavel) while transforming.
        // NativeAimDeltaHook is suppressed during transformation, so pending delta
        // is available here.  If the delta is already zero (steady AltForm, where
        // NativeAimDeltaHook consumed it), both hooks are no-ops.
        if (m_enableNativeAimDeltaHook && m_disableMphAimSmoothing
            && (m_nativeAimDeltaX | m_nativeAimDeltaY) != 0)
        {
            const auto& as = kAltStoredHooks[romIdx];
            const bool isYaw   = (arm9ExecAddr == as.yawPc);
            const bool isPitch = (arm9ExecAddr == as.pitchPc);

            if ((isYaw || isPitch) && regs) {
                const uint32_t player = regs[as.playerReg];
                uint32_t localPlayer = 0;
                if (!ImmediateOverlay_IsMainRamRange(as.localPlayerPtrAddr, sizeof(localPlayer)))
                    return;
                std::memcpy(&localPlayer, mainRAM + (as.localPlayerPtrAddr & 0x3FFFFFu), sizeof(localPlayer));
                if (player != localPlayer)
                    return;
                if (!ImmediateOverlay_IsMainRamRange(player + 0x3F8u, 8u))
                    return;

                uint32_t sensXRaw = 0, sensYRaw = 0;
                std::memcpy(&sensXRaw, mainRAM + ((player + 0x3F8u) & 0x3FFFFFu), sizeof(sensXRaw));
                std::memcpy(&sensYRaw, mainRAM + ((player + 0x3FCu) & 0x3FFFFFu), sizeof(sensYRaw));

                if (isYaw) {
                    // Horizontal: inject horizontalScaled into r1, do not consume yet
                    const int64_t scaled =
                        static_cast<int64_t>(m_nativeAimDeltaX) * static_cast<int32_t>(sensXRaw);
                    regs[1] = static_cast<uint32_t>(scaled);
                } else {
                    // Vertical (pitch): inject verticalScaled into r1, then consume
                    const int64_t scaled =
                        static_cast<int64_t>(m_nativeAimDeltaY) * static_cast<int32_t>(sensYRaw);
                    regs[1] = static_cast<uint32_t>(scaled);
                    m_nativeAimDeltaX = 0;
                    m_nativeAimDeltaY = 0;
                }
                return;
            }
        }
    }

#endif // MELONPRIME_DS
