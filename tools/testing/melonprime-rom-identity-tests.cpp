/*
    Regression model for the per-EmuInstance ROM identity publication.

    The production owner is EmuInstance, while this test exercises the same
    small publication helpers used by its load/eject message handlers. Two
    independent identities must keep their classification inputs and
    generations isolated when either instance changes ROM state.
*/

#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "MelonPrimeDef.h"

namespace {

using Identity = MelonPrime::MelonPrimeRomIdentity;

bool SameIdentity(const Identity& lhs, const Identity& rhs)
{
    return lhs.checksum == rhs.checksum
        && lhs.gameCode == rhs.gameCode
        && lhs.romVersion == rhs.romVersion
        && lhs.generation == rhs.generation;
}

} // namespace

int main()
{
    static_assert(std::is_trivially_copyable_v<Identity>);
    static_assert(std::is_standard_layout_v<Identity>);

    Identity instanceA{};
    Identity instanceB{};

    MelonPrime::PublishRomIdentity(instanceA, 0x218DA42Cu, 0x454D4841u, 0u);
    MelonPrime::PublishRomIdentity(instanceB, 0xD75F539Du, 0x4A484541u, 1u);
    const Identity instanceABefore = instanceA;

    MelonPrime::ClearRomIdentity(instanceB);
    if (!SameIdentity(instanceA, instanceABefore)
        || instanceB.generation != 2u
        || instanceB.checksum != 0u
        || instanceB.gameCode != 0u
        || instanceB.romVersion != 0u) {
        std::fprintf(stderr,
            "FAIL: B load/eject changed A or did not clear B identity\n");
        return 1;
    }

    MelonPrime::PublishRomIdentity(instanceB, 0x910018A5u, 0x50484541u, 1u);
    if (!SameIdentity(instanceA, instanceABefore)
        || instanceB.generation != 3u
        || instanceB.checksum != 0x910018A5u
        || instanceB.gameCode != 0x50484541u
        || instanceB.romVersion != 1u) {
        std::fprintf(stderr,
            "FAIL: B replacement ROM changed A or published bad B state\n");
        return 1;
    }

    MelonPrime::ClearRomIdentity(instanceA);
    if (instanceB.generation != 3u
        || instanceB.checksum != 0x910018A5u
        || instanceA.generation != 2u) {
        std::fprintf(stderr,
            "FAIL: A eject changed B or advanced the wrong generation\n");
        return 1;
    }

    std::puts("melonprime-rom-identity-tests: PASS");
    return 0;
}
