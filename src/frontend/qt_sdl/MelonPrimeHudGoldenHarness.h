#ifndef MELON_PRIME_HUD_GOLDEN_HARNESS_H
#define MELON_PRIME_HUD_GOLDEN_HARNESS_H

#ifdef MELONPRIME_CUSTOM_HUD
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES

// =========================================================================
//  Developer-only Custom HUD golden hash harness.
//
//  Behind the same developer gate as the implementation fragment, so a
//  release build cannot reach the declaration either.
// =========================================================================

class QString;

namespace MelonPrime {

    // Developer-only CLI hook: render deterministic HUD cases and write hashes.
    int CustomHud_RunGoldenHarness(const QString& outputPath);

} // namespace MelonPrime

#endif // MELONPRIME_ENABLE_DEVELOPER_FEATURES
#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_GOLDEN_HARNESS_H
