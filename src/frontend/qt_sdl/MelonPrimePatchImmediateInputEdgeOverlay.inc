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

        // ── Shared helpers ───────────────────────────────────────────────────

        [[nodiscard]] static FORCE_INLINE bool ImmediateOverlay_IsMainRamRange(
            uint32_t address, uint32_t size) noexcept
        {
            return size != 0
                && address >= 0x02000000u
                && size - 1u <= 0x023FFFFFu - address;
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

        // Action consumer only (Fire / Jump / Zoom / Movement).
        // AltForm AIM hooks (VSteerInput, AltStored) live in NativeAimDeltaHook.inc.
        push(kImmediateOverlayActionConsumerPc[romGroupIndex]);

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
            // Cached at game join; these control-preset fields do not change
            // during a match, and this hook runs on a hot action-consumer path.
            //   +0x368  Move left         +0x36C  Move right
            //   +0x370  Move forward      +0x374  Move backward
            //   +0x398  Fire              +0x39C  Jump (normal binding path)
            //   +0x3E0  Zoom
            const uint16_t movLMask = m_bindingMoveL;
            const uint16_t movRMask = m_bindingMoveR;
            const uint16_t movFMask = m_bindingMoveF;
            const uint16_t movBMask = m_bindingMoveB;
            const uint16_t fireMask = m_enableNativeBipedFire
                ? 0u
                : m_bindingFire;
            const uint16_t jumpMask = m_bindingJump;
            const uint16_t zoomMask = m_enableNativeZoomToggle
                ? 0u
                : (m_enableNewZoomInputMethod
                    ? m_bindingZoom
                    : static_cast<uint16_t>(1u << INPUT_R));

            const uint16_t managedMask = static_cast<uint16_t>(
                movLMask | movRMask | movFMask | movBMask |
                fireMask | jumpMask | zoomMask);
            if (managedMask == 0)
                return;

            // Some systems synthesize hardware input before the game's poll
            // (for example morph-ball boost writes INPUT_R). If such a bit also
            // belongs to the overlay's managed set, leave the game-polled
            // current/pressed/released fields intact for that bit.
            const uint16_t preserveMask =
                static_cast<uint16_t>(m_immediateOverlayPreserveMask & managedMask);
            const uint16_t overlayMask =
                static_cast<uint16_t>(managedMask & ~preserveMask);

            // Build emulator-side held state for all overlay-managed actions.
            const uint64_t down = m_input.down;
            uint16_t injectedHeld = 0;
            if (down & IB_MOVE_L) injectedHeld = static_cast<uint16_t>(injectedHeld | movLMask);
            if (down & IB_MOVE_R) injectedHeld = static_cast<uint16_t>(injectedHeld | movRMask);
            if (down & IB_MOVE_F) injectedHeld = static_cast<uint16_t>(injectedHeld | movFMask);
            if (down & IB_MOVE_B) injectedHeld = static_cast<uint16_t>(injectedHeld | movBMask);
            if (down & IB_SHOOT)  injectedHeld = static_cast<uint16_t>(injectedHeld | fireMask);
            if (down & IB_JUMP)   injectedHeld = static_cast<uint16_t>(injectedHeld | jumpMask);
            if (down & IB_ZOOM)   injectedHeld = static_cast<uint16_t>(injectedHeld | zoomMask);
            injectedHeld = static_cast<uint16_t>(injectedHeld & overlayMask);

            const uint16_t prevInjectedHeld = static_cast<uint16_t>(m_immediateOverlayPrevHeld & overlayMask);
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

            // Replace only overlay-owned bits; preserve game-polled bits that
            // another MelonPrime path synthesized through the hardware mask.
            const uint16_t newCurrent  = static_cast<uint16_t>((current  & ~overlayMask) | injectedHeld);
            const uint16_t newPressed  = static_cast<uint16_t>((pressed  & ~overlayMask) | injectedPressed);
            const uint16_t newReleased = static_cast<uint16_t>((released & ~overlayMask) | injectedReleased);

            std::memcpy(mainRAM + ((inputBase + 0x00u) & 0x3FFFFFu), &newCurrent,  sizeof(newCurrent));
            std::memcpy(mainRAM + ((inputBase + 0x04u) & 0x3FFFFFu), &newPressed,  sizeof(newPressed));
            std::memcpy(mainRAM + ((inputBase + 0x08u) & 0x3FFFFFu), &newReleased, sizeof(newReleased));

            m_immediateOverlayPrevHeld =
                static_cast<uint16_t>(injectedHeld | (current & preserveMask));
            return;
        }
    }

#endif // MELONPRIME_DS
