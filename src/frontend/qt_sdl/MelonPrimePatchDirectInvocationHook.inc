#ifdef MELONPRIME_DS

    namespace {

        // ---------------------------------------------------------------
        // "New Method 2" for Alt-Form Transform and Weapon Change, and
        // "New Method 3" for Zoom.
        //
        // Implements the DirectInvocation specification from mphCodex
        // (Direct-Invocation-NoTouch-NoAim-Complete-AllVersions.md and
        // Direct-Invocation-Execution-Hook-Trampoline-AllVersions.md):
        //
        //   host pressed edge -> pending request
        //     -> PlayerProcessInput's ProcessTouchInput call site
        //     -> original ProcessTouchInput still runs first
        //     -> local Player guard
        //     -> ROM native Transform / TryEquipWeapon / SetPlayerScopeZoom
        //     -> back into the original flow
        //
        // What this path deliberately does NOT do (spec section 9):
        //   - no synthetic touch, no touch hit test
        //   - no NoAimInput (player+0x4C4 bit 0x01000000) set or clear
        //   - no radial weapon menu
        //   - no direct CurrentWeapon / form-bit / player+0x850 writes
        //   - no separate SFX or HUD dispatch alongside the zoom setter: the
        //     ROM setter raises both itself (Direct-Zoom-Toggle spec section 2)
        //
        // Divergence from the spec's quick-slot wrapper: the same-weapon
        // "HUD action 0x3C only" branch is not reproduced in the guest.
        // MelonPrimeCore::SwitchWeapon already returns before the request is
        // queued when the requested weapon is the one already held, so the
        // guest never receives a same-weapon request through this path.
        //
        // The hook site is the same one the Method-1 weapon hook uses, but
        // this trampoline runs the original callee first and only then makes
        // the native call, instead of calling first and tail-calling the
        // original.
        // ---------------------------------------------------------------

        struct DirectInvocationSite
        {
            uint32_t ActiveCallAddr;      // BL ProcessTouchInput (hook PC)
            uint32_t ExpectedActiveCall;  // encoded BL word at that PC
            uint32_t ActiveAfterAddr;     // instruction after the BL
            uint32_t OriginalCallee;      // ProcessTouchInput
            uint32_t TransformAddr;       // Player_RequestFormSwitch(player, force)
            uint32_t TryEquipAddr;        // TryEquipWeapon(player, weaponId, flags)
            uint32_t HudDispatchAddr;     // HudActionDispatch(actionId, arg)
            uint32_t ScopeZoomAddr;       // SetPlayerScopeZoom(player, enabled)
            uint32_t MainPlayerPtrGlobal; // pointer-to-pointer to local player
        };

        // Command values, handed to the trampoline in r2.
        static constexpr uint32_t kDirectInvocationCommandTransform = 1u;
        static constexpr uint32_t kDirectInvocationCommandWeapon = 2u;
        static constexpr uint32_t kDirectInvocationCommandZoom = 3u;

        // Player / WeaponData fields the zoom gates read
        // (Direct-Zoom-Toggle-NoTouch-NoAim-AllVersions.md sections 1 and 5).
        static constexpr uint32_t kPlayerFlags1Offset = 0x4C4u;
        static constexpr uint32_t kPlayerScopeStateOffset = 0x850u;
        static constexpr uint32_t kPlayerCurrentWeaponPtr = 0x858u;
        static constexpr uint32_t kWeaponDataFlagsOffset = 0x08u;
        static constexpr uint32_t kPlayerFlags1AltForm = 0x00000200u;
        static constexpr uint32_t kPlayerFlags1Morphing = 0x00000800u;
        static constexpr uint32_t kPlayerFlags1Unmorphing = 0x00001000u;
        static constexpr uint32_t kWeaponDataZoomCapable = 0x00000800u;
        static constexpr uint8_t  kPlayerScopeZoomBit = 0x01u;

        // ---------------------------------------------------------------
        // ROM code cave 02003E9C..02003FBB is 72 zero words in all seven
        // dumps. It is shared, and the existing reservations are:
        //
        //   02003EA0..02003ED3  Method-1 weapon switch trampoline
        //   02003EE0..02003EF3  Method-1 weapon switch scratch
        //   02003F00..02003F2B  native zoom toggle trampoline
        //   02003F40..02003F4F  native zoom toggle scratch
        //
        // This module takes the tail at 02003F50 and needs no data block: the
        // request is handed over in r0-r2, which the dispatcher writes into the
        // guest register file just before redirecting.
        //
        // Passing it in registers is not a micro-optimisation, it is required.
        // The JIT constant-folds every PC-relative load with an immediate
        // offset at compile time (Compiler::Comp_MemAccess -> Comp_MemLoadLiteral
        // emits MOV Imm32), and a raw host write into NDS::MainRAM does not
        // invalidate that. A trampoline that read its request through
        // "ldr rX,[pc,#imm]" would therefore keep replaying whatever the first
        // dispatch happened to store. Loading a constant scratch *address* that
        // way is fine -- that is what the two modules above do -- but the
        // request data itself must never arrive through a folded literal.
        // ---------------------------------------------------------------
        static constexpr uint32_t kDirectInvocationCaveEnd = 0x02003FBBu;
        static constexpr uint32_t kDirectInvocationTrampolineAddr = 0x02003F50u;

        // ARMv5TE trampoline. Word index is shown so the hand-computed branch
        // offsets stay checkable. The six BL / B words are ROM-specific and are
        // encoded at install time.
        //
        // On entry the dispatcher has already put the request in registers:
        //   r0 = Player* (host-validated local player; also the argument the
        //        replaced "bl ProcessTouchInput" expects)
        //   r1 = argument  (weapon id, or desired scope state)
        //   r2 = command   (1 transform, 2 weapon, 3 zoom)
        // r0-r3 and r12 are caller-saved across the call this replaces, so only
        // r4-r6 and lr need preserving.
        //
        //   00  stmdb sp!, {r4, r5, r6, lr}
        //   01  mov   r4,  r0            ; player
        //   02  mov   r5,  r1            ; argument
        //   03  mov   r6,  r2            ; command
        //   04  bl    ProcessTouchInput  ; the instruction we replaced
        //   05  mov   r0,  r4
        //   06  cmp   r6,  #1
        //   07  beq   16                 ; transform
        //   08  mov   r1,  r5
        //   09  cmp   r6,  #3
        //   10  beq   14                 ; zoom
        //   11  mov   r2,  #0            ; flags = 0 (normal quick slot)
        //   12  bl    TryEquipWeapon
        //   13  b     25
        //   14  bl    SetPlayerScopeZoom ; r1 = desired state, gated by host
        //   15  b     25
        //   16  mov   r1,  #0            ; force = 0 (normal request)
        //   17  bl    Transform
        //   18  cmp   r0,  #0
        //   19  ldrne r1,  [r4, #0x4C4]  ; Flags1, only if accepted
        //   20  tstne r1,  #0x800        ; Morphing
        //   21  beq   25                 ; rejected, or Unmorph: no 0x16
        //   22  mov   r0,  #0x16
        //   23  mov   r1,  #0
        //   24  bl    HudDispatch
        //   25  ldmia sp!, {r4, r5, r6, lr}
        //   26  b     After
        static constexpr uint32_t kDirectInvocationCodeWordCount = 27u;
        static constexpr uint32_t kDirectInvocationCodeSize =
            kDirectInvocationCodeWordCount * sizeof(uint32_t);

        // Word indices of the six ROM-specific branches.
        static constexpr uint32_t kDirectInvocationIdxCallOriginal = 4u;
        static constexpr uint32_t kDirectInvocationIdxCallTryEquip = 12u;
        static constexpr uint32_t kDirectInvocationIdxCallScopeZoom = 14u;
        static constexpr uint32_t kDirectInvocationIdxCallTransform = 17u;
        static constexpr uint32_t kDirectInvocationIdxCallHud = 24u;
        static constexpr uint32_t kDirectInvocationIdxBranchAfter = 26u;

        static constexpr uint32_t kDirectInvocationTrampolineTemplate[
            kDirectInvocationCodeWordCount] = {
            0xE92D4070u, // 00 stmdb sp!, {r4, r5, r6, lr}
            0xE1A04000u, // 01 mov   r4,  r0
            0xE1A05001u, // 02 mov   r5,  r1
            0xE1A06002u, // 03 mov   r6,  r2
            0u,          // 04 bl    ProcessTouchInput   (encoded per ROM)
            0xE1A00004u, // 05 mov   r0,  r4
            0xE3560001u, // 06 cmp   r6,  #1
            0x0A000007u, // 07 beq   +7  -> 16
            0xE1A01005u, // 08 mov   r1,  r5
            0xE3560003u, // 09 cmp   r6,  #3
            0x0A000002u, // 10 beq   +2  -> 14
            0xE3A02000u, // 11 mov   r2,  #0
            0u,          // 12 bl    TryEquipWeapon      (encoded per ROM)
            0xEA00000Au, // 13 b     +10 -> 25
            0u,          // 14 bl    SetPlayerScopeZoom  (encoded per ROM)
            0xEA000008u, // 15 b     +8  -> 25
            0xE3A01000u, // 16 mov   r1,  #0
            0u,          // 17 bl    Transform           (encoded per ROM)
            0xE3500000u, // 18 cmp   r0,  #0
            0x159414C4u, // 19 ldrne r1,  [r4, #0x4C4]
            0x13110B02u, // 20 tstne r1,  #0x800
            0x0A000002u, // 21 beq   +2  -> 25
            0xE3A00016u, // 22 mov   r0,  #0x16
            0xE3A01000u, // 23 mov   r1,  #0
            0u,          // 24 bl    HudDispatch         (encoded per ROM)
            0xE8BD4070u, // 25 ldmia sp!, {r4, r5, r6, lr}
            0u,          // 26 b     After               (encoded per ROM)
        };

        [[nodiscard]] static constexpr bool DirectInvocation_Disjoint(
            uint32_t aStart, uint32_t aSize,
            uint32_t bStart, uint32_t bSize) noexcept
        {
            return aStart + aSize <= bStart || bStart + bSize <= aStart;
        }

        // Cave reservations owned by the other two modules in this unity
        // parent. Their constants are in scope because this fragment is
        // included after both.
        static_assert(
            DirectInvocation_Disjoint(
                kDirectInvocationTrampolineAddr, kDirectInvocationCodeSize,
                kWeaponSwitchTrampolineAddr,
                static_cast<uint32_t>(sizeof(kWeaponSwitchTrampolineCode)))
            && DirectInvocation_Disjoint(
                kDirectInvocationTrampolineAddr, kDirectInvocationCodeSize,
                kWeaponSwitchScratchAddr, 20u)
            && DirectInvocation_Disjoint(
                kDirectInvocationTrampolineAddr, kDirectInvocationCodeSize,
                kNativeZoomToggleTrampolineAddr,
                static_cast<uint32_t>(sizeof(kNativeZoomToggleTrampolineCode)))
            && DirectInvocation_Disjoint(
                kDirectInvocationTrampolineAddr, kDirectInvocationCodeSize,
                kNativeZoomToggleScratchAddr, 16u),
            "DirectInvocation trampoline overlaps another ROM code-cave reservation");
        static_assert(
            kDirectInvocationTrampolineAddr + kDirectInvocationCodeSize - 1u
                <= kDirectInvocationCaveEnd,
            "DirectInvocation trampoline leaves the verified ROM code cave");

        // ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3,
        //                  EU1_0=4, EU1_1=5, KR1_0=6
        // Transform / TryEquipWeapon / HUD dispatch addresses are the
        // all-versions function map in the DirectInvocation spec (section 5).
#define MP_DIRECT_INVOCATION_SITE(i, originalCallee, transform, tryEquip, hud, scopeZoom) \
        { LIST_HookPlayerUpdateActiveCallAddr[i], LIST_HookPlayerUpdateActiveCallExpected[i], \
          LIST_HookPlayerUpdateActiveAfter[i], originalCallee, transform, tryEquip, hud, \
          scopeZoom, LIST_HookLocalPlayerPtrGlobal[i] }
        static constexpr DirectInvocationSite kDirectInvocationSites[] = {
            MP_DIRECT_INVOCATION_SITE(0, 0x02026BFCu, 0x02016338u, 0x0200C5FCu, 0x0202D06Cu, 0x02015C98u),
            MP_DIRECT_INVOCATION_SITE(1, 0x02026BFCu, 0x02016338u, 0x0200C5FCu, 0x0202D06Cu, 0x02015C98u),
            MP_DIRECT_INVOCATION_SITE(2, 0x02026C20u, 0x02016358u, 0x0200C5FCu, 0x0202D090u, 0x02015CB8u),
            MP_DIRECT_INVOCATION_SITE(3, 0x02026C20u, 0x0201635Cu, 0x0200C5FCu, 0x0202D090u, 0x02015CBCu),
            MP_DIRECT_INVOCATION_SITE(4, 0x02026C18u, 0x02016350u, 0x0200C600u, 0x0202D088u, 0x02015CB0u),
            MP_DIRECT_INVOCATION_SITE(5, 0x02026C20u, 0x0201635Cu, 0x0200C5FCu, 0x0202D090u, 0x02015CBCu),
            MP_DIRECT_INVOCATION_SITE(6, 0x0200C29Cu, 0x0201C408u, 0x02025DBCu, 0x02035EACu, 0x0201CEBCu),
        };
#undef MP_DIRECT_INVOCATION_SITE
        static_assert(
            sizeof(kDirectInvocationSites) / sizeof(kDirectInvocationSites[0])
                == static_cast<uint32_t>(RomGroup::COUNT),
            "DirectInvocation site table must cover every ROM group");
        static constexpr bool DirectInvocation_SitesInMainRam() noexcept
        {
            for (const auto& site : kDirectInvocationSites) {
                if (!RomAddrDetail::InMainRam(site.ActiveCallAddr)
                    || !RomAddrDetail::InMainRam(site.ActiveAfterAddr)
                    || !RomAddrDetail::InMainRam(site.OriginalCallee)
                    || !RomAddrDetail::InMainRam(site.TransformAddr)
                    || !RomAddrDetail::InMainRam(site.TryEquipAddr)
                    || !RomAddrDetail::InMainRam(site.HudDispatchAddr)
                    || !RomAddrDetail::InMainRam(site.ScopeZoomAddr)
                    || !RomAddrDetail::InMainRam(site.MainPlayerPtrGlobal))
                {
                    return false;
                }
            }
            return true;
        }
        static_assert(DirectInvocation_SitesInMainRam(),
                      "DirectInvocation site table contains a non-main-RAM address");

        [[nodiscard]] static FORCE_INLINE const DirectInvocationSite*
        DirectInvocation_GetSite(uint8_t romGroupIndex) noexcept
        {
            if (romGroupIndex >= sizeof(kDirectInvocationSites) / sizeof(kDirectInvocationSites[0]))
                return nullptr;
            return &kDirectInvocationSites[romGroupIndex];
        }

        [[nodiscard]] static FORCE_INLINE bool DirectInvocation_IsMainRamRange(
            uint32_t address,
            uint32_t size) noexcept
        {
            return size != 0
                && address >= 0x02000000u
                && size - 1u <= 0x023FFFFFu - address;
        }

        // ARM A1 branch encoding. Every target here is inside main RAM and
        // the trampoline is too, so the +-32MB range is never in question.
        [[nodiscard]] static constexpr uint32_t DirectInvocation_EncodeBranch(
            uint32_t base, uint32_t wordIndex, uint32_t target, bool link) noexcept
        {
            const uint32_t at = base + wordIndex * sizeof(uint32_t);
            const uint32_t imm24 =
                ((target - (at + 8u)) >> 2) & 0x00FFFFFFu;
            return (link ? 0xEB000000u : 0xEA000000u) | imm24;
        }

        static void DirectInvocation_BuildCode(
            const DirectInvocationSite& site,
            uint32_t (&out)[kDirectInvocationCodeWordCount]) noexcept
        {
            for (uint32_t i = 0; i < kDirectInvocationCodeWordCount; ++i)
                out[i] = kDirectInvocationTrampolineTemplate[i];

            constexpr uint32_t base = kDirectInvocationTrampolineAddr;
            out[kDirectInvocationIdxCallOriginal] = DirectInvocation_EncodeBranch(
                base, kDirectInvocationIdxCallOriginal, site.OriginalCallee, true);
            out[kDirectInvocationIdxCallTransform] = DirectInvocation_EncodeBranch(
                base, kDirectInvocationIdxCallTransform, site.TransformAddr, true);
            out[kDirectInvocationIdxCallHud] = DirectInvocation_EncodeBranch(
                base, kDirectInvocationIdxCallHud, site.HudDispatchAddr, true);
            out[kDirectInvocationIdxCallScopeZoom] = DirectInvocation_EncodeBranch(
                base, kDirectInvocationIdxCallScopeZoom, site.ScopeZoomAddr, true);
            out[kDirectInvocationIdxCallTryEquip] = DirectInvocation_EncodeBranch(
                base, kDirectInvocationIdxCallTryEquip, site.TryEquipAddr, true);
            out[kDirectInvocationIdxBranchAfter] = DirectInvocation_EncodeBranch(
                base, kDirectInvocationIdxBranchAfter, site.ActiveAfterAddr, false);
        }

        // ARM9Write32 is used for the code words so the JIT drops any block
        // that already covers the cave.
        [[nodiscard]] static bool DirectInvocation_EnsureTrampoline(
            melonDS::NDS* nds,
            const DirectInvocationSite& site)
        {
            if (!nds)
                return false;
            if (!DirectInvocation_IsMainRamRange(
                    kDirectInvocationTrampolineAddr, kDirectInvocationCodeSize))
            {
                return false;
            }
            uint32_t code[kDirectInvocationCodeWordCount];
            DirectInvocation_BuildCode(site, code);

            for (uint32_t i = 0; i < kDirectInvocationCodeWordCount; ++i) {
                const uint32_t addr = kDirectInvocationTrampolineAddr + i * sizeof(uint32_t);
                if (nds->ARM9Read32(addr) != code[i])
                    nds->ARM9Write32(addr, code[i]);
            }
            return true;
        }

        [[nodiscard]] static FORCE_INLINE uint32_t DirectInvocation_Read32(
            melonDS::NDS* nds, uint32_t address) noexcept
        {
            return DirectInvocation_IsMainRamRange(address, sizeof(uint32_t))
                ? Read32(nds->MainRAM, address)
                : 0u;
        }

        // Direct-Zoom-Toggle-NoTouch-NoAim-AllVersions.md section 5. The touch
        // shortcut's own gates, restated against the currently equipped weapon
        // because a standalone request has no tapped quick slot to read.
        //
        // Returns false when the request must be dropped; otherwise writes the
        // state to pass to SetPlayerScopeZoom.
        [[nodiscard]] static bool DirectInvocation_ResolveZoomState(
            melonDS::NDS* nds,
            uint32_t player,
            bool forceOff,
            uint8_t& desiredOut)
        {
            const bool current =
                (Read8(nds->MainRAM, player + kPlayerScopeStateOffset)
                 & kPlayerScopeZoomBit) != 0;

            // Cleanup path (spec section 6): turning zoom off skips the weapon
            // capability gate, because the reason to turn it off is usually
            // that the gate stopped holding.
            if (forceOff) {
                if (!current)
                    return false;
                desiredOut = 0;
                return true;
            }

            const uint32_t flags1 =
                DirectInvocation_Read32(nds, player + kPlayerFlags1Offset);
            if (flags1 & (kPlayerFlags1AltForm
                          | kPlayerFlags1Morphing
                          | kPlayerFlags1Unmorphing))
            {
                return false;
            }

            const uint32_t weapon =
                DirectInvocation_Read32(nds, player + kPlayerCurrentWeaponPtr);
            if (!DirectInvocation_IsMainRamRange(weapon, 0x5Cu))
                return false;
            const uint32_t weaponFlags =
                DirectInvocation_Read32(nds, weapon + kWeaponDataFlagsOffset);
            if ((weaponFlags & kWeaponDataZoomCapable) == 0)
                return false;

            desiredOut = current ? 0u : 1u;
            return true;
        }

    } // anonymous namespace

    uint32_t MelonPrimeCore::DirectInvocationHook_GetAddresses(
        uint8_t romGroupIndex,
        uint32_t* out,
        uint32_t maxCount)
    {
        if (!out || maxCount == 0)
            return 0;

        const DirectInvocationSite* const site = DirectInvocation_GetSite(romGroupIndex);
        if (!site)
            return 0;

        out[0] = site->ActiveCallAddr;
        return 1;
    }

    bool MelonPrimeCore::DirectInvocationHook_IsRomSupported(uint8_t romGroupIndex)
    {
        return DirectInvocation_GetSite(romGroupIndex) != nullptr;
    }

    bool MelonPrimeCore::DirectInvocationHook_IsSiteValid(
        melonDS::NDS* nds,
        uint8_t romGroupIndex)
    {
        const DirectInvocationSite* const site = DirectInvocation_GetSite(romGroupIndex);
        return nds
            && site
            && nds->ARM9Read32(site->ActiveCallAddr) == site->ExpectedActiveCall
            && DirectInvocation_EnsureTrampoline(nds, *site);
    }

    void MelonPrimeCore::QueueDirectInvocationTransform() noexcept
    {
        // Spec section 7: one request per pressed edge with a short TTL, never
        // a per-frame request while the key is held. The call site is only
        // reached when the player's +0x84E gate is clear, so the request is
        // held for a few frames rather than dropped on the press frame.
        m_directInvocationPending.Transform = true;
        m_directInvocationPending.TransformFrames = 10;
    }

    void MelonPrimeCore::QueueDirectInvocationWeapon(uint8_t weaponId) noexcept
    {
        if (weaponId >= 9) {
            m_directInvocationPending.ClearWeapon();
            return;
        }
        m_directInvocationPending.WeaponId = weaponId;
        m_directInvocationPending.WeaponFrames = 12;
    }

    void MelonPrimeCore::QueueDirectInvocationZoom(bool forceOff) noexcept
    {
        // Spec section 9: one toggle per pressed edge. Holding the key must not
        // re-request every frame, which is why the caller drives this from an
        // edge and not from the held state.
        m_directInvocationPending.Zoom = true;
        m_directInvocationPending.ZoomForceOff = forceOff;
        m_directInvocationPending.ZoomFrames = 8;
    }

    // Zoom pressed-edge driver. Mirrors the native toggle's frame logic but
    // queues into the DirectInvocation mailbox, so the setter runs from the
    // guest player-input safe point instead of the weapon action update.
    HOT_FUNCTION void MelonPrimeCore::UpdateDirectInvocationZoomInput()
    {
        const bool down = IsDown(IB_ZOOM);
        const bool pressedEdge = down && !m_nativeZoomTogglePrevDown;
        m_nativeZoomTogglePrevDown = down;

        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT)
            || !m_flags.test(StateFlags::BIT_BATTLE_RUNTIME_MODE)
            || !m_flags.test(StateFlags::BIT_LAST_FOCUSED)
            || (m_aimBlockBits & AIMBLK_NOT_IN_GAME)
            || !IsLocalPlayerAlive())
        {
            return;
        }

        if (m_directInvocationPending.Zoom)
            return;

        // Same hot-path skip as the native toggle: with zoom known off and no
        // press this frame, the cleanup check has nothing to do, and the game
        // never turns zoom on by itself.
        if (!pressedEdge && !m_nativeZoomLastKnownEnabled)
            return;

        melonDS::NDS* const nds = emuInstance->getNDS();
        const DirectInvocationSite* const site =
            DirectInvocation_GetSite(m_currentRom.romGroupIndex);
        if (!nds || !site)
            return;

        const uint32_t player =
            DirectInvocation_Read32(nds, site->MainPlayerPtrGlobal);
        if (!DirectInvocation_IsMainRamRange(player, Consts::PLAYER_STRUCT_SIZE))
            return;

        const bool currentEnabled =
            (Read8(nds->MainRAM, player + kPlayerScopeStateOffset)
             & kPlayerScopeZoomBit) != 0;
        m_nativeZoomLastKnownEnabled = currentEnabled;

        if (!pressedEdge) {
            // Cleanup only: the scope is on but the equipped weapon can no
            // longer zoom (spec section 6). The hook re-reads the state at the
            // safe point, so this is a request, not a decision.
            if (currentEnabled) {
                const uint32_t weapon =
                    DirectInvocation_Read32(nds, player + kPlayerCurrentWeaponPtr);
                const bool capable =
                    DirectInvocation_IsMainRamRange(weapon, 0x5Cu)
                    && (DirectInvocation_Read32(nds, weapon + kWeaponDataFlagsOffset)
                        & kWeaponDataZoomCapable) != 0;
                if (!capable) {
                    QueueDirectInvocationZoom(true);
                    m_nativeZoomLastKnownEnabled = false;
                }
            }
            return;
        }

        QueueDirectInvocationZoom(false);
        m_nativeZoomLastKnownEnabled = !currentEnabled;
    }

    bool MelonPrimeCore::DirectInvocationHook_DispatchCheckAndRedirect(
        melonDS::NDS* nds,
        uint32_t arm9ExecAddr,
        uint32_t regs[16],
        uint32_t& redirectExecAddr)
    {
        redirectExecAddr = 0;

        if (!nds || !regs)
            return false;
        if (!m_directInvocationPending.IsValid())
            return false;
        // Match-scoped like every other native path: the hooks are installed on
        // the battle-runtime latch, so a request must never be serviced outside
        // it either. Without this a request queued during the join/countdown
        // could fire on the very first battle-runtime frame.
        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT)
            || !m_flags.test(StateFlags::BIT_BATTLE_RUNTIME_MODE)
            || !IsLocalPlayerAlive())
        {
            m_directInvocationPending.Clear();
            return false;
        }

        const DirectInvocationSite* const site =
            DirectInvocation_GetSite(m_currentRom.romGroupIndex);
        if (!site || arm9ExecAddr != site->ActiveCallAddr)
            return false;

        if (nds->ARM9Read32(site->ActiveCallAddr) != site->ExpectedActiveCall) {
            m_directInvocationPending.Clear();
            return false;
        }

        // r0 holds Player* at the call site; r5 keeps it across the callee on
        // every dump, so it is the fallback when r0 is not a plausible player.
        // The trampoline uses the value resolved here, not r0, so a shuffled
        // register cannot send a native call to the wrong struct.
        uint32_t player = regs[0];
        if (!DirectInvocation_IsMainRamRange(player, Consts::PLAYER_STRUCT_SIZE)
            && DirectInvocation_IsMainRamRange(regs[5], Consts::PLAYER_STRUCT_SIZE))
        {
            player = regs[5];
        }
        if (!DirectInvocation_IsMainRamRange(player, Consts::PLAYER_STRUCT_SIZE))
            return false;

        // Spec section 3: a remote human player runs this call site too, so the
        // +0x84E gate alone must never decide whether host input applies.
        const uint32_t localPlayer = Read32(nds->MainRAM, site->MainPlayerPtrGlobal);
        if (localPlayer == 0 || player != localPlayer)
            return false;

        if (!DirectInvocation_EnsureTrampoline(nds, *site))
            return false;

        // Fixed priority when more than one is pending: transform, weapon,
        // zoom. The ones not taken keep their TTL and are dispatched on a later
        // frame that reaches this site.
        uint32_t command;
        uint8_t arg = 0;
        if (m_directInvocationPending.Transform) {
            command = kDirectInvocationCommandTransform;
            m_directInvocationPending.ClearTransform();
        }
        else if (m_directInvocationPending.WeaponValid()) {
            command = kDirectInvocationCommandWeapon;
            arg = m_directInvocationPending.WeaponId;
            m_directInvocationPending.ClearWeapon();
        }
        else {
            const bool forceOff = m_directInvocationPending.ZoomForceOff;
            m_directInvocationPending.ClearZoom();
            // Gates are evaluated here rather than at queue time: this is the
            // guest safe point, so the player state read is the state the
            // setter will actually act on.
            if (!DirectInvocation_ResolveZoomState(nds, player, forceOff, arg))
                return false;
            command = kDirectInvocationCommandZoom;
        }

        // Hand the request over in the guest register file. r0 doubles as the
        // Player* argument the replaced "bl ProcessTouchInput" expects, and
        // r0-r2 are caller-saved across that call, so the parent function
        // cannot observe the difference.
        //
        // This must not go through guest memory: the JIT folds PC-relative
        // loads into compile-time constants, and a host write into MainRAM
        // would not invalidate them, so the trampoline would replay the first
        // request forever. See the cave comment at the top of this file.
        regs[0] = player;
        regs[1] = arg;
        regs[2] = command;

        redirectExecAddr = kDirectInvocationTrampolineAddr;
        return true;
    }

#else

    HOT_FUNCTION void MelonPrimeCore::UpdateDirectInvocationZoomInput()
    {
    }

#endif // MELONPRIME_DS
