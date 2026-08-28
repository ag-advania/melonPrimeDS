#ifdef MELONPRIME_DS

    // =========================================================================
    // Native Biped Fire — post-poll Fire input overlay
    //
    // The previous design redirected the player-update BL to a MainRAM
    // trampoline, direct-called PlayerFireUpdate(player), and forced r0=1 at
    // the inner "movs rN,r0" that consumes the Pressed-edge selector
    // (0x40000 -> input+0x04). That converted host-side *held* into game-side
    // *Pressed* on every frame and ran the fire update outside the biped action
    // state machine, bypassing the caller-owned Shooting state, repeat-fire
    // gate, charge accumulation/release, animation selection and return-value
    // propagation.
    //
    // The current design does not call PlayerFireUpdate and does not touch any
    // register. It registers the same post-poll action-consumer PC as the
    // generic ImmediateInputEdgeOverlay and overlays a consistent Fire
    // current/pressed/released triple into player+0x464 after the game's input
    // poll and before any action handler reads it. Everything downstream —
    // cooldown, repeat, charge, ammo, projectile, HUD, SFX, animation — stays
    // on the ROM's own path.
    //
    // Ownership: the Fire bit is written by exactly one place, the single
    // read/modify/write in ImmediateInputEdgeOverlay_DispatchCheck. This file
    // owns only the host-side Fire edge latch that feeds it.
    // =========================================================================

    // Opt-in instrumentation, off by default: define
    // MELONPRIME_NATIVE_BIPED_FIRE_DIAG in a developer build to register the
    // fire-edge diagnostic PC and get one OSD line per trigger pull. Leaving it
    // on would both spam the OSD and cost an extra JIT trampoline site.
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_NATIVE_BIPED_FIRE_DIAG)
#define MELONPRIME_NATIVE_BIPED_FIRE_DIAG_ENABLED 1
#endif

    namespace {

        // Same post-poll action consumer the generic overlay hooks. When both
        // features are on, AddDispatchAddress() merges the two dispatch masks
        // onto this single PC; when only Native Biped Fire is on, this
        // registration is what keeps the overlay handler running.
        static constexpr const auto& kNativeBipedFireActionConsumerPc =
            LIST_HookActionConsumerPc;
        static_assert(
            kNativeBipedFireActionConsumerPc.size() == static_cast<uint32_t>(RomGroup::COUNT),
            "Native biped-fire action-consumer table must cover every ROM group");

        // Developer-only diagnostic PC: the inner fire-edge site the stability
        // instructions reserve as a diagnostic reference (JP1_0 02027234 etc.).
        // Execution reaches it immediately after PlayerFireUpdate's call to the
        // input helper, so regs[0] still holds the helper's Pressed result for
        // the Fire binding. Registering it answers, in one run, whether
        // PlayerFireUpdate is reached at all and whether the ROM saw the bit the
        // overlay wrote. Never registered in release builds.
        static constexpr uint32_t kNativeBipedFireEdgeDiagPc[] = {
            0x02027234u, // JP1_0
            0x02027234u, // JP1_1
            0x02027258u, // US1_0
            0x02027258u, // US1_1
            0x02027250u, // EU1_0
            0x02027258u, // EU1_1
            0x0200BD78u, // KR1_0
        };
        static_assert(
            sizeof(kNativeBipedFireEdgeDiagPc) / sizeof(kNativeBipedFireEdgeDiagPc[0])
                == static_cast<uint32_t>(RomGroup::COUNT),
            "Native biped-fire diagnostic PC table must cover every ROM group");

    } // anonymous namespace

    uint32_t MelonPrimeCore::NativeBipedFireHook_GetAddresses(
        uint8_t romGroupIndex,
        uint32_t* out,
        uint32_t maxCount)
    {
        if (!out || maxCount == 0)
            return 0;

        constexpr uint32_t kRomCount =
            static_cast<uint32_t>(kNativeBipedFireActionConsumerPc.size());
        if (romGroupIndex >= kRomCount)
            return 0;

        uint32_t count = 0;
        out[count++] = kNativeBipedFireActionConsumerPc[romGroupIndex];
#if defined(MELONPRIME_NATIVE_BIPED_FIRE_DIAG_ENABLED)
        if (count < maxCount)
            out[count++] = kNativeBipedFireEdgeDiagPc[romGroupIndex];
#endif
        return count;
    }

    // Host-side Fire edge latch — resolved once per frame.
    //
    // The ROM action consumer (LIST_HookActionConsumerPc) is the entry of the
    // per-player input consumer: MphRead's PlayerEntity.Process() calls
    // ProcessInput() for every player entity, so the hook is entered up to four
    // times per frame and only one of those entries belongs to the local
    // player. Resolving the edge inside the hook therefore destroys it — the
    // first entry writes Pressed, the next entry recomputes held-vs-held and
    // writes 0 back over it before the local player's own entry reads the
    // field. Current survives (it is the same value every entry), which is why
    // the symptom was "IsDown works, nothing ever fires".
    //
    // So the edge is resolved here, once, on the frame path, and the overlay
    // applies the resulting triple idempotently at every hook entry.
    //
    // The latch is invalidated (re-baselined, not zeroed) by every lifecycle
    // transition that can hide host input or change what it refers to — game
    // join/leave, focus loss/regain, feature toggle, savestate load,
    // boot/stop/ROM change, AIMBLK_NOT_IN_GAME transitions, a change of the
    // ROM's local Player*, and biped<->alt-form transitions. Re-entry while Shoot is already held then restores Current
    // without manufacturing a stale Pressed; a real release->press after that
    // produces the next edge.
    HOT_FUNCTION void MelonPrimeCore::UpdateNativeBipedFireInput(
        bool localPlayerChanged)
    {
        // ApplyPostPollOverlayInput already gates on m_enableNativeBipedFire, so
        // by contract this function is never entered with the feature disabled.
        NativeBipedFireHook_DiagnosticReport();

        m_nativeBipedFireFramePressed = false;
        m_nativeBipedFireFrameReleased = false;

        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT)
            || !m_flags.test(StateFlags::BIT_LAST_FOCUSED)
            || (m_aimBlockBits & AIMBLK_NOT_IN_GAME))
        {
            m_nativeBipedFireFrameHeld = false;
            m_nativeBipedFireLatchValid = false;
            return;
        }

        const bool held = (m_input.down & IB_SHOOT) != 0;
        const bool altForm = IsPlayerAltForm();

        const bool resync = !m_nativeBipedFireLatchValid
                         || localPlayerChanged
                         || altForm != m_nativeBipedFirePrevAltForm;

        const bool wasHeld = m_nativeBipedFirePrevHeld;
        m_nativeBipedFirePrevHeld = held;
        m_nativeBipedFirePrevAltForm = altForm;
        m_nativeBipedFireLatchValid = true;
        m_nativeBipedFireFrameHeld = held;

        if (LIKELY(!resync)) {
            m_nativeBipedFireFramePressed = held && !wasHeld;
            m_nativeBipedFireFrameReleased = !held && wasHeld;
        }

#if defined(MELONPRIME_NATIVE_BIPED_FIRE_DIAG_ENABLED)
        // Report on the next frame, once the hooks for this one have run.
        if (m_nativeBipedFireFramePressed)
            m_nbfDiagArmed = true;
#endif
    }

    // Applied by ImmediateInputEdgeOverlay_DispatchCheck at every hook entry.
    // Pure projection of the frame-resolved state onto the binding mask: no
    // latch mutation, so repeated entries within one frame are idempotent.
    uint16_t MelonPrimeCore::NativeBipedFire_ResolveOverlayEdges(
        uint16_t fireMask,
        uint16_t& pressedOut,
        uint16_t& releasedOut) const noexcept
    {
        pressedOut  = m_nativeBipedFireFramePressed  ? fireMask : 0u;
        releasedOut = m_nativeBipedFireFrameReleased ? fireMask : 0u;
        return m_nativeBipedFireFrameHeld ? fireMask : 0u;
    }

    // ── Developer diagnostics ────────────────────────────────────────────
    //
    // One OSD line per trigger pull, reported on the frame after the hooks ran:
    //
    //   NBF ovl=<overlay entries> fire=<fire-edge PC entries> mask=<fire bits>
    //       live=<player+0x398 re-read> pre=<+0x04 before write>
    //       post=<+0x04 after write> seen=<+0x04 at the fire-edge PC>
    //       hit=<helper result>
    //
    // Reading it:
    //   ovl=0                  the overlay hook never ran (registration/gating)
    //   mask=0000, live!=0     the join-time Fire binding cache is stale/empty
    //   mask=0000, live=0000   playerBase is wrong, or the preset is unread
    //   post without the mask  our write did not land
    //   fire=0                 PlayerFireUpdate is never reached this frame
    //   seen=0000 with post!=0 something cleared +0x04 between the two points
    //   hit=0 with seen&mask   binding/selector mismatch inside the helper
    void MelonPrimeCore::NativeBipedFireHook_DiagnosticRecordOverlay(
        const uint8_t* mainRAM,
        uint32_t playerBase,
        uint16_t fireOverlayMask,
        uint16_t prePressed,
        uint16_t postPressed) noexcept
    {
#if defined(MELONPRIME_NATIVE_BIPED_FIRE_DIAG_ENABLED)
        if (!m_enableNativeBipedFire)
            return;
        if (m_nbfDiagOverlayHits < 0xFFFFu)
            ++m_nbfDiagOverlayHits;
        if (m_nbfDiagOverlayHits == 1) {
            m_nbfDiagFireMask = fireOverlayMask;
            m_nbfDiagPrePressed = prePressed;
            m_nbfDiagPostPressed = postPressed;
            // Live re-read of the Fire ButtonControl the ROM itself loads at
            // PlayerFireUpdate (player+0x398, low half = ButtonFlags). Tells a
            // stale/empty join-time binding cache apart from a lost write.
            m_nbfDiagLiveBinding = 0;
            const uint32_t bindingAddr = playerBase + 0x398u;
            if (mainRAM && bindingAddr >= 0x02000000u && bindingAddr <= 0x023FFFFEu) {
                uint16_t live = 0;
                std::memcpy(&live, mainRAM + (bindingAddr & 0x3FFFFFu), sizeof(live));
                m_nbfDiagLiveBinding = live;
            }
        }
#else
        (void)mainRAM;
        (void)playerBase;
        (void)fireOverlayMask;
        (void)prePressed;
        (void)postPressed;
#endif
    }

    bool MelonPrimeCore::NativeBipedFireHook_DiagnosticCheck(
        melonDS::NDS* nds,
        uint32_t arm9ExecAddr,
        uint32_t regs[16])
    {
#if defined(MELONPRIME_NATIVE_BIPED_FIRE_DIAG_ENABLED)
        const uint8_t romGroupIndex = m_currentRom.romGroupIndex;
        if (romGroupIndex >= static_cast<uint8_t>(RomGroup::COUNT))
            return false;
        if (arm9ExecAddr != kNativeBipedFireEdgeDiagPc[romGroupIndex])
            return false;

        // This PC belongs to the diagnostic only; the caller must not also run
        // the overlay for it.
        if (m_nbfDiagFireUpdateHits < 0xFFFFu)
            ++m_nbfDiagFireUpdateHits;
        if (regs[0] != 0)
            m_nbfDiagHelperResult = 1;

        if (nds) {
            const uint32_t offP = static_cast<uint32_t>(m_playerPosition)
                                * static_cast<uint32_t>(Consts::PLAYER_ADDR_INC);
            const uint32_t pressedAddr =
                m_currentRom.playerStructStart + offP + 0x464u + 0x04u;
            if (pressedAddr >= 0x02000000u && pressedAddr <= 0x023FFFFEu) {
                uint16_t seen = 0;
                std::memcpy(&seen, nds->MainRAM + (pressedAddr & 0x3FFFFFu), sizeof(seen));
                m_nbfDiagSeenPressed = seen;
            }
        }
        return true;
#else
        (void)nds;
        (void)arm9ExecAddr;
        (void)regs;
        return false;
#endif
    }

    void MelonPrimeCore::NativeBipedFireHook_DiagnosticReport()
    {
#if defined(MELONPRIME_NATIVE_BIPED_FIRE_DIAG_ENABLED)
        if (m_nbfDiagArmed) {
            m_nbfDiagArmed = false;
            emuInstance->osdAddMessage(
                0,
                "NBF ovl=%u fire=%u mask=%04X live=%04X pre=%04X post=%04X seen=%04X hit=%u",
                static_cast<unsigned>(m_nbfDiagOverlayHits),
                static_cast<unsigned>(m_nbfDiagFireUpdateHits),
                static_cast<unsigned>(m_nbfDiagFireMask),
                static_cast<unsigned>(m_nbfDiagLiveBinding),
                static_cast<unsigned>(m_nbfDiagPrePressed),
                static_cast<unsigned>(m_nbfDiagPostPressed),
                static_cast<unsigned>(m_nbfDiagSeenPressed),
                static_cast<unsigned>(m_nbfDiagHelperResult));
        }

        m_nbfDiagOverlayHits = 0;
        m_nbfDiagFireUpdateHits = 0;
        m_nbfDiagFireMask = 0;
        m_nbfDiagLiveBinding = 0;
        m_nbfDiagPrePressed = 0;
        m_nbfDiagPostPressed = 0;
        m_nbfDiagSeenPressed = 0;
        m_nbfDiagHelperResult = 0;
#endif
    }

#else

    HOT_FUNCTION void MelonPrimeCore::UpdateNativeBipedFireInput(bool)
    {
    }

    void MelonPrimeCore::NativeBipedFireHook_DiagnosticRecordOverlay(
        const uint8_t*, uint32_t, uint16_t, uint16_t, uint16_t) noexcept
    {
    }

    bool MelonPrimeCore::NativeBipedFireHook_DiagnosticCheck(
        melonDS::NDS*, uint32_t, uint32_t[16])
    {
        return false;
    }

    void MelonPrimeCore::NativeBipedFireHook_DiagnosticReport()
    {
    }

#endif // MELONPRIME_DS
