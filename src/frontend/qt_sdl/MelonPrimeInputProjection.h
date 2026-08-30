#pragma once

#include "MelonPrimeCompilerHints.h"
#include "MelonPrimeDef.h"

#include <array>
#include <cstdint>

// HK_* hotkey indices (EmuInstance.h) must be visible before this include.

namespace MelonPrime::InputProjection {

// Out-of-game menu movement, as a DS button mask to PRESS per 4-bit move index
// (Forward/Back/Left/Right, opposite pairs cancel).
//
// Menus are not control-preset bound: the Adventure planet/region map and the
// Hunter License pages navigate on the D-pad whatever the player picked for
// in-game movement. Only the in-game path uses the preset table that
// MelonPrimeCore::PresetButtonBindings::Build() derives per game join.
alignas(64) inline constexpr std::array<uint16_t, 16> MenuMoveMask = {
    0x0000, 0x0040, 0x0080, 0x0000,
    0x0020, 0x0060, 0x00A0, 0x0020,
    0x0010, 0x0050, 0x0090, 0x0010,
    0x0000, 0x0040, 0x0080, 0x0000,
};

static_assert(HK_MetroidMoveBack        == HK_MetroidMoveForward + 1, "Move group must stay contiguous");
static_assert(HK_MetroidMoveLeft        == HK_MetroidMoveForward + 2, "Move group must stay contiguous");
static_assert(HK_MetroidMoveRight       == HK_MetroidMoveForward + 3, "Move group must stay contiguous");
static_assert(HK_MetroidUILeft          == HK_MetroidScanVisor + 1,   "UI group layout changed");
static_assert(HK_MetroidUIRight         == HK_MetroidScanVisor + 2,   "UI group layout changed");
static_assert(HK_MetroidUIOk            == HK_MetroidScanVisor + 3,   "UI group layout changed");
static_assert(HK_MetroidUIYes           == HK_MetroidScanVisor + 4,   "UI group layout changed");
static_assert(HK_MetroidUINo            == HK_MetroidScanVisor + 5,   "UI group layout changed");
static_assert(HK_MetroidWeaponMissile   == HK_MetroidWeaponBeam + 1,  "Weapon group layout changed");
static_assert(HK_MetroidWeaponSpecial   == HK_MetroidWeaponBeam + 2,  "Weapon group layout changed");
static_assert(HK_MetroidWeaponNext      == HK_MetroidWeaponBeam + 3,  "Weapon group layout changed");
static_assert(HK_MetroidWeaponPrevious  == HK_MetroidWeaponBeam + 4,  "Weapon group layout changed");
static_assert(HK_MetroidWeapon1         == HK_MetroidWeaponBeam + 5,  "Weapon group layout changed");
static_assert(HK_MetroidWeapon6         == HK_MetroidWeaponBeam + 10, "Weapon group layout changed");

struct ProjectedDownState {
    uint64_t mask;
    uint32_t moveIndex;
};

[[nodiscard]] FORCE_INLINE ProjectedDownState ProjectDownState(uint64_t hotMask) noexcept
{
    const uint32_t moveBits =
        static_cast<uint32_t>((hotMask >> HK_MetroidMoveForward) & 0xFULL);

    uint64_t down = static_cast<uint64_t>(moveBits) << 6;

    down |= ((hotMask >> HK_MetroidJump)               & 1ULL) << 0;
    down |= ((((hotMask >> HK_MetroidShootScan) |
               (hotMask >> HK_MetroidScanShoot))       & 1ULL) << 1);
    down |= ((hotMask >> HK_MetroidZoom)               & 1ULL) << 2;
    down |= ((hotMask >> HK_MetroidHoldMorphBallBoost) & 1ULL) << 4;
    down |= ((hotMask >> HK_MetroidWeaponCheck)        & 1ULL) << 5;
    down |= ((hotMask >> HK_MetroidMenu)               & 1ULL) << 10;

    return { down, moveBits };
}

[[nodiscard]] FORCE_INLINE uint64_t ProjectPressMask(uint64_t hotMask) noexcept
{
    const uint64_t uiBits = (hotMask >> HK_MetroidUILeft) & 0x1FULL;
    const uint64_t weaponBits = (hotMask >> HK_MetroidWeaponBeam) & 0x7FFULL;

    uint64_t press = 0;
    press |= ((hotMask >> HK_MetroidMorphBall) & 1ULL) << 3;
    press |= ((hotMask >> HK_MetroidWeaponCheck) & 1ULL) << 5;
    press |= ((hotMask >> HK_MetroidScanVisor) & 1ULL) << 11;

    // UI order in hotkeys: Left Right Ok Yes No
    // UI order in IB bits : Ok(12) Left(13) Right(14) Yes(15) No(16)
    press |= (uiBits & 0x3ULL) << 13;         // Left / Right
    press |= ((uiBits >> 2) & 0x1ULL) << 12;  // Ok
    press |= ((uiBits >> 3) & 0x3ULL) << 15;  // Yes / No

    // Weapon order in hotkeys:
    //   Beam Missile Special Next Prev 1 2 3 4 5 6
    // Weapon order in IB bits:
    //   17   18      25      26   27  19..24
    press |= (weaponBits & 0x3ULL) << 17;          // Beam / Missile
    press |= ((weaponBits >> 5) & 0x3FULL) << 19;  // Weapon1..6
    press |= ((weaponBits >> 2) & 0x1ULL) << 25;   // Special
    // Primary Next/Prev from the contiguous weapon group, OR secondary aliases.
    press |= ((weaponBits >> 3) & 0x3ULL) << 26;   // Next / Prev (primary)
    press |= ((hotMask >> HK_MetroidWeaponNextSecondary) & 1ULL) << 26;
    press |= ((hotMask >> HK_MetroidWeaponPreviousSecondary) & 1ULL) << 27;
    return press;
}

} // namespace MelonPrime::InputProjection
