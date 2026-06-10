#ifndef MELON_PRIME_PATCH_COMMON_H
#define MELON_PRIME_PATCH_COMMON_H

#ifdef MELONPRIME_DS

#include "NDS.h"

#include <cstdint>

namespace MelonPrime {

    // Static write-patch word. Addresses are absolute ARM9 addresses.
    struct PatchWord {
        uint32_t address;
        uint32_t applyVal;
        uint32_t revertVal;
    };

    // One ROM group's static write-patch view. count == 0 means unsupported.
    struct RomPatchSpan {
        const PatchWord* words;
        uint32_t count;
    };

    // Shared state machine for per-process singleton static write-patches.
    // This helper is intentionally limited to all-or-nothing word patches;
    // instruction hooks, pattern-C loaded-state patches, and partial masks stay
    // in their modules.
    class StaticWordPatch {
    public:
        constexpr explicit StaticWordPatch(const RomPatchSpan (&perRomSpans)[7]) noexcept
            : m_perRomSpans(perRomSpans)
        {
        }

        void ApplyOnce(melonDS::NDS* nds, uint8_t romGroupIndex)
        {
            if (!IsSupportedRomGroup(romGroupIndex))
                return;
            if (m_applied && m_appliedRomGroupIndex == romGroupIndex)
                return;
            if (m_applied)
                RestoreOnce(nds, m_appliedRomGroupIndex);
            if (!CanWritePatch(nds, romGroupIndex, true))
                return;

            WritePatch(nds, romGroupIndex, true);
            m_applied = true;
            m_appliedRomGroupIndex = romGroupIndex;
        }

        void RestoreOnce(melonDS::NDS* nds, uint8_t romGroupIndex)
        {
            if (!m_applied || !nds)
                return;
            if (IsSupportedRomGroup(m_appliedRomGroupIndex))
                romGroupIndex = m_appliedRomGroupIndex;
            if (!IsSupportedRomGroup(romGroupIndex))
                return;
            if (!CanWritePatch(nds, romGroupIndex, false))
                return;

            WritePatch(nds, romGroupIndex, false);
            ResetState();
        }

        void ResetState()
        {
            m_applied = false;
            m_appliedRomGroupIndex = 0xFFu;
        }

    private:
        [[nodiscard]] bool IsSupportedRomGroup(uint8_t romGroupIndex) const noexcept
        {
            return romGroupIndex < 7 && m_perRomSpans[romGroupIndex].count != 0;
        }

        [[nodiscard]] bool CanWritePatch(
            melonDS::NDS* nds,
            uint8_t romGroupIndex,
            bool apply) const
        {
            if (!nds || !IsSupportedRomGroup(romGroupIndex))
                return false;

            const RomPatchSpan& span = m_perRomSpans[romGroupIndex];
            for (uint32_t i = 0; i < span.count; ++i)
            {
                const PatchWord& word = span.words[i];
                const uint32_t current = nds->ARM9Read32(word.address);
                const uint32_t expected = apply ? word.revertVal : word.applyVal;
                const uint32_t already = apply ? word.applyVal : word.revertVal;
                if (current != expected && current != already)
                    return false;
            }
            return true;
        }

        void WritePatch(melonDS::NDS* nds, uint8_t romGroupIndex, bool apply) const
        {
            const RomPatchSpan& span = m_perRomSpans[romGroupIndex];
            for (uint32_t i = 0; i < span.count; ++i)
            {
                const PatchWord& word = span.words[i];
                nds->ARM9Write32(word.address, apply ? word.applyVal : word.revertVal);
            }
        }

        const RomPatchSpan (&m_perRomSpans)[7];
        bool m_applied = false;
        uint8_t m_appliedRomGroupIndex = 0xFFu;
    };

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_COMMON_H
