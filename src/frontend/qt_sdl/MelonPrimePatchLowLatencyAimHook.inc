#ifdef MELONPRIME_DS

    namespace {

        struct LowLatencyAimHookSites
        {
            uint32_t ExitA;
            uint32_t ExitB;
        };

        struct LowLatencyAimVec3
        {
            int32_t x;
            int32_t y;
            int32_t z;
        };

        static constexpr LowLatencyAimHookSites kLowLatencyAimHookSites[] = {
            { 0x020282C8u, 0x02028544u }, // JP1_0
            { 0x020282C8u, 0x02028544u }, // JP1_1
            { 0x020282ECu, 0x02028568u }, // US1_0
            { 0x020282ECu, 0x02028568u }, // US1_1
            { 0x020282E4u, 0x02028560u }, // EU1_0
            { 0x020282ECu, 0x02028568u }, // EU1_1
            { 0x0200B454u, 0x0200B6B0u }, // KR1_0
        };

        static constexpr int32_t kLowLatencyAimQ12One = 0x1000;
        static constexpr int32_t kLowLatencyAimVecComponentLimit = 0x20000;
        static constexpr uint32_t kLowLatencyAimPlayerMinSize = 0xB4u;

        [[nodiscard]] static FORCE_INLINE const LowLatencyAimHookSites*
        LowLatencyAim_GetSites(uint8_t romGroupIndex) noexcept
        {
            if (romGroupIndex >= sizeof(kLowLatencyAimHookSites) / sizeof(kLowLatencyAimHookSites[0]))
                return nullptr;
            return &kLowLatencyAimHookSites[romGroupIndex];
        }

        [[nodiscard]] static FORCE_INLINE bool LowLatencyAim_IsHookPc(
            const LowLatencyAimHookSites& sites,
            uint32_t arm9ExecAddr) noexcept
        {
            return arm9ExecAddr == sites.ExitA || arm9ExecAddr == sites.ExitB;
        }

        [[nodiscard]] static FORCE_INLINE bool LowLatencyAim_IsMainRamRange(
            uint32_t address,
            uint32_t size) noexcept
        {
            return size != 0
                && address >= 0x02000000u
                && size - 1u <= 0x023FFFFFu - address;
        }

        [[nodiscard]] static FORCE_INLINE int64_t LowLatencyAim_AbsI32ToI64(int32_t value) noexcept
        {
            return value < 0 ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
        }

        [[nodiscard]] static FORCE_INLINE bool LowLatencyAim_IsReasonableVec(
            LowLatencyAimVec3 v) noexcept
        {
            return LowLatencyAim_AbsI32ToI64(v.x) <= kLowLatencyAimVecComponentLimit
                && LowLatencyAim_AbsI32ToI64(v.y) <= kLowLatencyAimVecComponentLimit
                && LowLatencyAim_AbsI32ToI64(v.z) <= kLowLatencyAimVecComponentLimit;
        }

        [[nodiscard]] static FORCE_INLINE int32_t LowLatencyAim_RoundShiftQ12(
            int64_t value) noexcept
        {
            if (value >= 0)
                return static_cast<int32_t>((value + 0x800) >> 12);
            return -static_cast<int32_t>(((-value) + 0x800) >> 12);
        }

        [[nodiscard]] static FORCE_INLINE int64_t LowLatencyAim_Dot(
            LowLatencyAimVec3 a,
            LowLatencyAimVec3 b) noexcept
        {
            return static_cast<int64_t>(a.x) * b.x
                + static_cast<int64_t>(a.y) * b.y
                + static_cast<int64_t>(a.z) * b.z;
        }

        [[nodiscard]] static FORCE_INLINE uint64_t LowLatencyAim_LengthSq(
            LowLatencyAimVec3 v) noexcept
        {
            return static_cast<uint64_t>(static_cast<int64_t>(v.x) * v.x)
                + static_cast<uint64_t>(static_cast<int64_t>(v.y) * v.y)
                + static_cast<uint64_t>(static_cast<int64_t>(v.z) * v.z);
        }

        [[nodiscard]] static uint32_t LowLatencyAim_SqrtU64(uint64_t value) noexcept
        {
            uint64_t result = 0;
            uint64_t bit = 1ull << 62;

            while (bit > value)
                bit >>= 2;

            while (bit != 0)
            {
                if (value >= result + bit)
                {
                    value -= result + bit;
                    result = (result >> 1) + bit;
                }
                else
                {
                    result >>= 1;
                }
                bit >>= 2;
            }

            return static_cast<uint32_t>(result);
        }

        [[nodiscard]] static FORCE_INLINE LowLatencyAimVec3 LowLatencyAim_Add(
            LowLatencyAimVec3 a,
            LowLatencyAimVec3 b) noexcept
        {
            return { a.x + b.x, a.y + b.y, a.z + b.z };
        }

        [[nodiscard]] static FORCE_INLINE LowLatencyAimVec3 LowLatencyAim_Sub(
            LowLatencyAimVec3 a,
            LowLatencyAimVec3 b) noexcept
        {
            return { a.x - b.x, a.y - b.y, a.z - b.z };
        }

        [[nodiscard]] static FORCE_INLINE LowLatencyAimVec3 LowLatencyAim_ScaleQ12(
            LowLatencyAimVec3 v,
            int32_t scaleQ12) noexcept
        {
            return {
                LowLatencyAim_RoundShiftQ12(static_cast<int64_t>(v.x) * scaleQ12),
                LowLatencyAim_RoundShiftQ12(static_cast<int64_t>(v.y) * scaleQ12),
                LowLatencyAim_RoundShiftQ12(static_cast<int64_t>(v.z) * scaleQ12),
            };
        }

        [[nodiscard]] static FORCE_INLINE LowLatencyAimVec3 LowLatencyAim_CrossQ12(
            LowLatencyAimVec3 a,
            LowLatencyAimVec3 b) noexcept
        {
            return {
                LowLatencyAim_RoundShiftQ12(static_cast<int64_t>(a.y) * b.z - static_cast<int64_t>(a.z) * b.y),
                LowLatencyAim_RoundShiftQ12(static_cast<int64_t>(a.z) * b.x - static_cast<int64_t>(a.x) * b.z),
                LowLatencyAim_RoundShiftQ12(static_cast<int64_t>(a.x) * b.y - static_cast<int64_t>(a.y) * b.x),
            };
        }

        [[nodiscard]] static bool LowLatencyAim_TryNormalize(
            LowLatencyAimVec3 v,
            LowLatencyAimVec3& out) noexcept
        {
            if (!LowLatencyAim_IsReasonableVec(v))
                return false;

            const uint64_t lenSq = LowLatencyAim_LengthSq(v);
            if (lenSq == 0)
                return false;

            const uint32_t len = LowLatencyAim_SqrtU64(lenSq);
            if (len == 0)
                return false;

            const int32_t scaleQ12 = static_cast<int32_t>(
                (static_cast<uint64_t>(kLowLatencyAimQ12One) << 12) / len);
            if (scaleQ12 == 0)
                return false;

            out = LowLatencyAim_ScaleQ12(v, scaleQ12);
            return true;
        }

        [[nodiscard]] static LowLatencyAimVec3 LowLatencyAim_ApplyMoonLikeChase(
            LowLatencyAimVec3 current,
            LowLatencyAimVec3 target,
            int32_t normalStepQ12,
            int32_t fastStepQ12,
            int32_t fastThresholdQ12) noexcept
        {
            LowLatencyAimVec3 currentNorm {};
            LowLatencyAimVec3 targetNorm {};

            if (!LowLatencyAim_TryNormalize(target, targetNorm))
                return current;
            if (!LowLatencyAim_TryNormalize(current, currentNorm))
                return targetNorm;

            const LowLatencyAimVec3 diff = LowLatencyAim_Sub(targetNorm, currentNorm);
            const uint64_t diffLenSq = LowLatencyAim_LengthSq(diff);
            const int32_t selectedStep = diffLenSq > static_cast<uint64_t>(fastThresholdQ12) * fastThresholdQ12
                ? fastStepQ12
                : normalStepQ12;

            if (diffLenSq <= static_cast<uint64_t>(selectedStep) * selectedStep)
                return targetNorm;

            const uint32_t diffLen = LowLatencyAim_SqrtU64(diffLenSq);
            if (diffLen == 0)
                return targetNorm;

            const int32_t scaleQ12 = static_cast<int32_t>(
                (static_cast<uint64_t>(selectedStep) << 12) / diffLen);
            LowLatencyAimVec3 next = LowLatencyAim_Add(
                currentNorm,
                LowLatencyAim_ScaleQ12(diff, scaleQ12));

            LowLatencyAimVec3 nextNorm {};
            return LowLatencyAim_TryNormalize(next, nextNorm) ? nextNorm : targetNorm;
        }

        [[nodiscard]] static LowLatencyAimVec3 LowLatencyAim_BuildFallbackSide(
            LowLatencyAimVec3 current) noexcept
        {
            static constexpr LowLatencyAimVec3 kWorldUp { 0, kLowLatencyAimQ12One, 0 };
            static constexpr LowLatencyAimVec3 kWorldForward { 0, 0, kLowLatencyAimQ12One };
            static constexpr LowLatencyAimVec3 kWorldRight { kLowLatencyAimQ12One, 0, 0 };

            LowLatencyAimVec3 side = LowLatencyAim_CrossQ12(kWorldUp, current);
            LowLatencyAimVec3 sideNorm {};
            if (LowLatencyAim_TryNormalize(side, sideNorm))
                return sideNorm;

            side = LowLatencyAim_CrossQ12(kWorldForward, current);
            return LowLatencyAim_TryNormalize(side, sideNorm) ? sideNorm : kWorldRight;
        }

        static void LowLatencyAim_RebuildBasisPreservingSide(
            LowLatencyAimVec3 current,
            LowLatencyAimVec3& side,
            LowLatencyAimVec3& up) noexcept
        {
            LowLatencyAimVec3 currentNorm {};
            if (!LowLatencyAim_TryNormalize(current, currentNorm))
                return;

            LowLatencyAimVec3 sideNorm {};
            if (LowLatencyAim_TryNormalize(side, sideNorm))
            {
                const int32_t sideDotCurrentQ12 =
                    LowLatencyAim_RoundShiftQ12(LowLatencyAim_Dot(sideNorm, currentNorm));
                const LowLatencyAimVec3 projected = LowLatencyAim_Sub(
                    sideNorm,
                    LowLatencyAim_ScaleQ12(currentNorm, sideDotCurrentQ12));
                if (!LowLatencyAim_TryNormalize(projected, side))
                    side = LowLatencyAim_BuildFallbackSide(currentNorm);
            }
            else
            {
                side = LowLatencyAim_BuildFallbackSide(currentNorm);
            }

            const LowLatencyAimVec3 upRaw = LowLatencyAim_CrossQ12(currentNorm, side);
            if (!LowLatencyAim_TryNormalize(upRaw, up))
            {
                side = LowLatencyAim_BuildFallbackSide(currentNorm);
                const LowLatencyAimVec3 retryUp = LowLatencyAim_CrossQ12(currentNorm, side);
                LowLatencyAim_TryNormalize(retryUp, up);
            }
        }

        [[nodiscard]] static FORCE_INLINE LowLatencyAimVec3 LowLatencyAim_ReadVec3(
            const uint8_t* ram,
            uint32_t address) noexcept
        {
            return {
                static_cast<int32_t>(Read32(ram, address)),
                static_cast<int32_t>(Read32(ram, address + 4u)),
                static_cast<int32_t>(Read32(ram, address + 8u)),
            };
        }

        static FORCE_INLINE void LowLatencyAim_WriteVec3(
            uint8_t* ram,
            uint32_t address,
            LowLatencyAimVec3 v) noexcept
        {
            Write32(ram, address, static_cast<uint32_t>(v.x));
            Write32(ram, address + 4u, static_cast<uint32_t>(v.y));
            Write32(ram, address + 8u, static_cast<uint32_t>(v.z));
        }

    } // anonymous namespace

    uint32_t MelonPrimeCore::LowLatencyAimHook_GetAddresses(
        uint8_t romGroupIndex,
        uint32_t* out,
        uint32_t maxCount)
    {
        if (!out || maxCount < 2)
            return 0;

        const LowLatencyAimHookSites* const sites = LowLatencyAim_GetSites(romGroupIndex);
        if (!sites)
            return 0;

        out[0] = sites->ExitA;
        out[1] = sites->ExitB;
        return 2;
    }

    void MelonPrimeCore::LowLatencyAimHook_DispatchCheck(
        melonDS::NDS* nds,
        uint32_t arm9ExecAddr,
        uint32_t regs[16])
    {
        if (!nds || !regs || !nds->MainRAM)
            return;
        if (m_lowLatencyAimMode == LowLatencyAimMode::Off || isStylusMode)
            return;
        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT))
            return;
        if (!m_flags.test(StateFlags::BIT_LAST_FOCUSED))
            return;

        const LowLatencyAimHookSites* const sites =
            LowLatencyAim_GetSites(m_currentRom.romGroupIndex);
        if (!sites || !LowLatencyAim_IsHookPc(*sites, arm9ExecAddr))
            return;

        const uint32_t player = regs[4];
        const uint32_t expectedPlayer = m_currentRom.playerStructStart
            + static_cast<uint32_t>(m_playerPosition) * Consts::PLAYER_ADDR_INC;
        if (player != expectedPlayer)
            return;
        if (!LowLatencyAim_IsMainRamRange(player, kLowLatencyAimPlayerMinSize))
            return;

        uint8_t* const ram = nds->MainRAM;
        const LowLatencyAimVec3 current = LowLatencyAim_ReadVec3(ram, player + 0x4Cu);
        const LowLatencyAimVec3 target = LowLatencyAim_ReadVec3(ram, player + 0x9Cu);

        LowLatencyAimVec3 next {};
        if (m_lowLatencyAimMode == LowLatencyAimMode::ImmediateSync)
        {
            if (!LowLatencyAim_TryNormalize(target, next))
                return;
        }
        else if (m_lowLatencyAimMode == LowLatencyAimMode::MoonLikeAim)
        {
            next = LowLatencyAim_ApplyMoonLikeChase(
                current,
                target,
                m_moonLikeAimNormalStepQ12,
                m_moonLikeAimFastStepQ12,
                m_moonLikeAimFastThresholdQ12);
            if (!LowLatencyAim_TryNormalize(next, next))
                return;
        }
        else
        {
            return;
        }

        LowLatencyAim_WriteVec3(ram, player + 0x4Cu, next);

        LowLatencyAimVec3 side = LowLatencyAim_ReadVec3(ram, player + 0x58u);
        LowLatencyAimVec3 up = LowLatencyAim_ReadVec3(ram, player + 0x64u);
        LowLatencyAim_RebuildBasisPreservingSide(next, side, up);
        LowLatencyAim_WriteVec3(ram, player + 0x58u, side);
        LowLatencyAim_WriteVec3(ram, player + 0x64u, up);
    }

#endif // MELONPRIME_DS
