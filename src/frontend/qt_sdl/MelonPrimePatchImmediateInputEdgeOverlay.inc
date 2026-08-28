#ifdef MELONPRIME_DS

    namespace {

        // Hook at the action consumer entry.  Fires AFTER the input poll has written
        // fresh values into player+0x464, and BEFORE any action handler (morph check,
        // fire gate, jump gate, etc.) reads from it.
        static constexpr const auto& kImmediateOverlayActionConsumerPc =
            LIST_HookActionConsumerPc;
        static_assert(
            kImmediateOverlayActionConsumerPc.size() == static_cast<uint32_t>(RomGroup::COUNT),
            "Immediate overlay action-consumer table must cover every ROM group");

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
        // ARM9Hook_Install registers this handler when the generic overlay OR
        // Native Biped Fire is enabled and the rom is supported (both use the
        // same ActionConsumer PC, merged by AddDispatchAddress), and the
        // dispatcher routes here only at that registered PC. So the runtime
        // checks for romIdx range and arm9ExecAddr equality are dead — guarded
        // at install time. The two feature flags are still read below because
        // each owns a different subset of the managed bits.
        (void)arm9ExecAddr;

        if (!nds
            || !m_flags.test(StateFlags::BIT_IN_GAME_INIT)
            || !m_flags.test(StateFlags::BIT_LAST_FOCUSED)
            || isStylusMode
            || (m_aimBlockBits & AIMBLK_NOT_IN_GAME))
        {
            // The Fire latch is owned by UpdateNativeBipedFireInput() on the
            // frame path, which applies the same gates; nothing to do here.
            return;
        }

        uint8_t* const mainRAM = nds->MainRAM;

        // ── Action Consumer (Fire / Jump / Zoom / Movement) ──────────────────
        {
            const uint32_t offP = static_cast<uint32_t>(m_playerPosition)
                                * static_cast<uint32_t>(Consts::PLAYER_ADDR_INC);
            const uint32_t playerBase = m_currentRom.playerStructStart + offP;

            if (!ImmediateOverlay_IsMainRamRange(playerBase, 0x470u))
                return;

            // No local-player gate here on purpose.
            //
            // This PC is the entry of the ROM's per-player input consumer, so
            // it is reached once per player entity per frame and only one of
            // those entries is ours. An earlier revision skipped the other
            // entries by comparing regs[0] against *LIST_HookLocalPlayerPtrGlobal
            // (the comparison NativeAimDeltaHook makes deeper inside this same
            // function, where the player is known to live in r10). That gate
            // made Jump and Zoom intermittent, so the assumption that r0 holds
            // Player* at the *entry* instruction does not hold here.
            //
            // Writing the local player's struct on every entry is safe instead:
            // both edge sets are resolved once per frame on the frame path, so
            // every entry writes the same values and the local player's own
            // entry always sees them. Do not reintroduce a register-based gate
            // without runtime evidence for the register at this exact PC.

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
            //
            // Fire ownership: exactly one of the two features owns the Fire
            // bit. Native Biped Fire owns it when enabled (its edges come from
            // the dedicated latch below); otherwise the generic overlay owns
            // it with the shared prev-held state. Both write through the single
            // read/modify/write at the end of this function.
            const bool genericOverlay = m_enableImmediateInputEdgeOverlay;
            const bool nativeFire = m_enableNativeBipedFire;

            const uint16_t movLMask = genericOverlay ? m_bindingMoveL : 0u;
            const uint16_t movRMask = genericOverlay ? m_bindingMoveR : 0u;
            const uint16_t movFMask = genericOverlay ? m_bindingMoveF : 0u;
            const uint16_t movBMask = genericOverlay ? m_bindingMoveB : 0u;
            const uint16_t fireMask = (genericOverlay && !nativeFire)
                ? m_bindingFire
                : 0u;
            const uint16_t jumpMask = genericOverlay ? m_bindingJump : 0u;
            const uint16_t zoomMask = (!genericOverlay || m_enableNativeZoomToggle)
                ? 0u
                : (m_enableNewZoomInputMethod
                    ? m_bindingZoom
                    : static_cast<uint16_t>(1u << INPUT_R));
            const uint16_t nativeFireMask = nativeFire ? m_bindingFire : 0u;

            const uint16_t genericMask = static_cast<uint16_t>(
                movLMask | movRMask | movFMask | movBMask |
                fireMask | jumpMask | zoomMask);
            const uint16_t managedMask =
                static_cast<uint16_t>(genericMask | nativeFireMask);
            if (managedMask == 0)
                return;

            // Some systems synthesize hardware input before the game's poll
            // (for example morph-ball boost writes INPUT_R). If such a bit also
            // belongs to the overlay's managed set, leave the game-polled
            // current/pressed/released fields intact for that bit.
            const uint16_t preserveMask =
                static_cast<uint16_t>(m_immediateOverlayPreserveMask & managedMask);
            const uint16_t genericOverlayMask =
                static_cast<uint16_t>(genericMask & ~preserveMask);
            const uint16_t fireOverlayMask =
                static_cast<uint16_t>(nativeFireMask & ~preserveMask);

            // Expand the frame-resolved actions onto this frame's binding
            // masks. Both edge sets were resolved once on the frame path
            // (UpdateImmediateInputEdgeOverlayInput / UpdateNativeBipedFireInput)
            // because this hook is the entry of the ROM's per-player input
            // consumer: it runs once per player per frame, so recomputing an
            // edge here lets an earlier player's entry write Pressed and the
            // next entry erase it before the local player's entry reads it.
            const auto expandActions = [&](uint8_t actions) -> uint16_t {
                uint16_t bits = 0;
                if (actions & OVA_MOVE_L) bits = static_cast<uint16_t>(bits | movLMask);
                if (actions & OVA_MOVE_R) bits = static_cast<uint16_t>(bits | movRMask);
                if (actions & OVA_MOVE_F) bits = static_cast<uint16_t>(bits | movFMask);
                if (actions & OVA_MOVE_B) bits = static_cast<uint16_t>(bits | movBMask);
                if (actions & OVA_FIRE)   bits = static_cast<uint16_t>(bits | fireMask);
                if (actions & OVA_JUMP)   bits = static_cast<uint16_t>(bits | jumpMask);
                if (actions & OVA_ZOOM)   bits = static_cast<uint16_t>(bits | zoomMask);
                return static_cast<uint16_t>(bits & genericOverlayMask);
            };

            uint16_t injectedHeld     = expandActions(m_immediateOverlayFrameHeld);
            uint16_t injectedPressed  = expandActions(m_immediateOverlayFramePressed);
            uint16_t injectedReleased = expandActions(m_immediateOverlayFrameReleased);

            // Native Biped Fire owns the Fire bit through its own per-frame
            // latch, so a held button never re-arms the game's Pressed selector
            // (input+0x04) frame after frame.
            if (fireOverlayMask != 0) {
                uint16_t firePressed = 0;
                uint16_t fireReleased = 0;
                const uint16_t fireHeld = NativeBipedFire_ResolveOverlayEdges(
                    fireOverlayMask, firePressed, fireReleased);
                injectedHeld = static_cast<uint16_t>(injectedHeld | fireHeld);
                injectedPressed = static_cast<uint16_t>(injectedPressed | firePressed);
                injectedReleased = static_cast<uint16_t>(injectedReleased | fireReleased);
            }

            // Read game-polled input struct at player+0x464.
            const uint32_t inputBase = playerBase + 0x464u;
            uint16_t current  = 0;
            uint16_t pressed  = 0;
            uint16_t released = 0;
            std::memcpy(&current,  mainRAM + ((inputBase + 0x00u) & 0x3FFFFFu), sizeof(current));
            std::memcpy(&pressed,  mainRAM + ((inputBase + 0x04u) & 0x3FFFFFu), sizeof(pressed));
            std::memcpy(&released, mainRAM + ((inputBase + 0x08u) & 0x3FFFFFu), sizeof(released));

            // Additive, never destructive.
            //
            // This overlay makes an action land in the same frame; it is not the
            // only thing driving these bits. For Jump, Zoom and Movement the
            // legacy DS KEYINPUT path is still active, so the game's own poll
            // already produced correct current/pressed/released for them, and
            // the ROM's binding is not even the same bit MelonPrime presses --
            // Touch R binds Jump to 0x0C03 (A/B/X/Y) while the legacy path
            // presses B alone.
            //
            // Replacing the bits (the previous `(field & ~overlayMask) | injected`)
            // therefore cleared the game's own correct edge on every frame this
            // latch happened to report no edge: right after a re-baseline (game
            // join, focus blip, AIMBLK_NOT_IN_GAME transition, local-player
            // change) and whenever a re-entrant FrameAdvanceOnce (weapon switch,
            // morph) had already consumed the edge. That is the "jump/zoom often
            // does not respond" failure: the overlay could only ever subtract
            // reliability from the legacy path.
            //
            // OR-ing can only ever make an action fire earlier, never swallow
            // one. Native Biped Fire is unaffected by the change: it suppresses
            // INPUT_L, so the polled bits it ORs into are already zero.
            const uint16_t newCurrent  = static_cast<uint16_t>(current  | injectedHeld);
            const uint16_t newPressed  = static_cast<uint16_t>(pressed  | injectedPressed);
            const uint16_t newReleased = static_cast<uint16_t>(released | injectedReleased);

            std::memcpy(mainRAM + ((inputBase + 0x00u) & 0x3FFFFFu), &newCurrent,  sizeof(newCurrent));
            std::memcpy(mainRAM + ((inputBase + 0x04u) & 0x3FFFFFu), &newPressed,  sizeof(newPressed));
            std::memcpy(mainRAM + ((inputBase + 0x08u) & 0x3FFFFFu), &newReleased, sizeof(newReleased));

            NativeBipedFireHook_DiagnosticRecordOverlay(
                mainRAM, playerBase, fireOverlayMask, pressed, newPressed);
            return;
        }
    }

#endif // MELONPRIME_DS
