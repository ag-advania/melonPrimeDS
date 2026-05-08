#ifdef MELONPRIME_DS

    // ── PostFold Write Hook ──────────────────────────────────────────────────
    //
    // Alternative to the NativeAimDeltaHookRegisterInjection version.
    //
    // Hooks at the single point right AFTER the TouchInputProcessor writes the
    // derived Y value (strh at 0202A02C / 0202A050 etc.).  At that PC, r7/r6
    // still holds the input struct base pointer, and input+0x2A/+0x2C have just
    // been set by the game.  We overwrite them with m_nativeAimDeltaX/Y.
    //
    // This single hook covers ALL aim consumers:
    //   - Biped Normal aim (reads input+0x2A/+0x2C at consumer PC)
    //   - AltForm spec108=1 (Trace/Sylux/Weavel — AltInput chain)
    //   - AltForm spec108=0 (Samus/Kanden/Noxus/Spire — VectorSteer)
    //
    // Tradeoff vs register injection:
    //   + Covers VectorSteer without register-level breakage
    //   - Fires one stage earlier in the pipeline; slightly more latency than
    //     the register injection approach which fires at the aim call-site.

    namespace {

        struct AimFoldDerivedHookEntry {
            uint32_t postFoldPc;   // branch instruction immediately after Y strh
            uint8_t  inputReg;     // register holding inputBase at postFoldPc
            uint32_t inputBase1P;  // 1P inputBase address
        };

        // inputBase stride per player position: 0x48
        static constexpr AimFoldDerivedHookEntry kAimFoldDerivedHookEntries[] = {
            { 0x0202A030u, 7u, 0x020E03A8u }, // JP1_0
            { 0x0202A030u, 7u, 0x020E0368u }, // JP1_1
            { 0x0202A054u, 7u, 0x020DE4E8u }, // US1_0
            { 0x0202A054u, 7u, 0x020DED68u }, // US1_1
            { 0x0202A04Cu, 7u, 0x020DED88u }, // EU1_0
            { 0x0202A054u, 7u, 0x020DEE08u }, // EU1_1
            { 0x02028070u, 6u, 0x020D7BD0u }, // KR1_0
        };

    } // anonymous namespace

    uint32_t MelonPrimeCore::NativeAimDeltaHookPostFoldWrite_GetAddresses(
        uint8_t romGroupIndex,
        uint32_t* out,
        uint32_t maxCount)
    {
        if (!out || maxCount == 0)
            return 0;
        if (romGroupIndex >= sizeof(kAimFoldDerivedHookEntries) / sizeof(kAimFoldDerivedHookEntries[0]))
            return 0;
        out[0] = kAimFoldDerivedHookEntries[romGroupIndex].postFoldPc;
        return 1;
    }

    void MelonPrimeCore::NativeAimDeltaHookPostFoldWrite_DispatchCheck(
        melonDS::NDS* nds,
        uint32_t arm9ExecAddr,
        uint32_t regs[16])
    {
        // ARM9Hook_Install gates registration on m_enableNativeAimDeltaHook
        // (nativeAimHookMode == 2) AND m_disableMphAimSmoothing, so both
        // runtime checks are dead. Stylus mode is dynamic and ARM9Hook_Install
        // does not gate on it, so the isStylusMode check is kept. Single
        // address registered per ROM, so the postFoldPc equality check is
        // dead too.
        (void)arm9ExecAddr;

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
        const auto& h = kAimFoldDerivedHookEntries[romIdx];

        const uint32_t inputBase = regs[h.inputReg];
        const uint32_t expectedInputBase = h.inputBase1P
            + static_cast<uint32_t>(m_playerPosition) * 0x48u;
        if (inputBase != expectedInputBase)
            return;
        // expectedInputBase is computed from a constant table entry +
        // (uint8_t player position * 0x48), so the maximum value comfortably
        // fits inside main RAM. No need to re-bounds-check inputBase here —
        // the equality with expectedInputBase already guarantees it.

        const int16_t x = m_nativeAimDeltaX;
        const int16_t y = m_nativeAimDeltaY;
        std::memcpy(nds->MainRAM + ((inputBase + 0x2Au) & 0x3FFFFFu), &x, sizeof(x));
        std::memcpy(nds->MainRAM + ((inputBase + 0x2Cu) & 0x3FFFFFu), &y, sizeof(y));
    }

#endif // MELONPRIME_DS
