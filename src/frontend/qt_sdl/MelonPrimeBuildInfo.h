#ifndef MELON_PRIME_BUILD_INFO_H
#define MELON_PRIME_BUILD_INFO_H

#ifdef MELONPRIME_DS

#include "version.h"

// MelonPrimeDS build/app identity shown in the window title and console banner.
//
// The build timestamp is reformatted from the compiler's __DATE__ / __TIME__
// into an internationally unambiguous "YYYY-MM-DD HH:MM:SS" form (the C locale
// "Jun 14 2026" month-name format is confusing for non-English readers).
//
// Everything here is computed at compile time (constexpr over the __DATE__ /
// __TIME__ string literals), so it costs nothing at runtime. It also lives in
// this small header instead of the CMake-generated version.h, so it never
// forces a rebuild of every version.h consumer the way a configure-time
// string(TIMESTAMP) would.

// Build-machine UTC offset (e.g. "GMT+9"), supplied by CMake as a compile
// definition. Fallback keeps the header usable if the define is ever missing.
#ifndef MELONPRIMEDS_BUILD_TZ
#define MELONPRIMEDS_BUILD_TZ "GMT?"
#endif

// Git provenance (HEAD SHA/branch/dirty state) captured at CMake configure
// time. Used to stamp diagnostic logs (e.g. vulkan2d.log) so a captured trace
// can be verified as coming from the binary under test, not a stale build.
#ifndef MELONPRIMEDS_GIT_SHA
#define MELONPRIMEDS_GIT_SHA "unknown"
#endif
#ifndef MELONPRIMEDS_GIT_BRANCH
#define MELONPRIMEDS_GIT_BRANCH "unknown"
#endif
#ifndef MELONPRIMEDS_GIT_DIRTY
#define MELONPRIMEDS_GIT_DIRTY "1"
#endif

// App title pieces (plain string literals, usable in literal concatenation).
// The full title is assembled as:
//
//   MelonPrimeDS (build <YYYY-MM-DD HH:MM:SS> GMT+9) (melonDS <ver>)
//
// The version number belongs to the upstream melonDS base, so it is shown as a
// trailing "(melonDS <ver>)" rather than right after "MelonPrimeDS", where it
// would look like a MelonPrimeDS version. The build timestamp (kBuildStamp)
// goes between PREFIX and SUFFIX, inserted by each call site (usually via %s);
// the timezone offset is part of SUFFIX so it appears right after the time.
#define MELONPRIMEDS_TITLE_PREFIX "MelonPrimeDS (build "
#define MELONPRIMEDS_TITLE_SUFFIX " " MELONPRIMEDS_BUILD_TZ ") (melonDS " MELONDS_VERSION ")"

namespace MelonPrime {
namespace detail {

    // __DATE__ is "Mmm dd yyyy" (day is space-padded for single digits).
    constexpr int BuildMonthNumber()
    {
        const char* d = __DATE__;
        return (d[0] == 'J' && d[1] == 'a')                  ? 1   // Jan
             : (d[0] == 'F')                                 ? 2   // Feb
             : (d[0] == 'M' && d[2] == 'r')                  ? 3   // Mar
             : (d[0] == 'A' && d[1] == 'p')                  ? 4   // Apr
             : (d[0] == 'M' && d[2] == 'y')                  ? 5   // May
             : (d[0] == 'J' && d[2] == 'n')                  ? 6   // Jun
             : (d[0] == 'J' && d[2] == 'l')                  ? 7   // Jul
             : (d[0] == 'A' && d[1] == 'u')                  ? 8   // Aug
             : (d[0] == 'S')                                 ? 9   // Sep
             : (d[0] == 'O')                                 ? 10  // Oct
             : (d[0] == 'N')                                 ? 11  // Nov
             :                                                 12; // Dec
    }

    struct BuildStamp
    {
        // "YYYY-MM-DD HH:MM:SS" = 19 chars + NUL.
        char text[20];
    };

    constexpr BuildStamp MakeBuildStamp()
    {
        const char* d = __DATE__; // "Mmm dd yyyy"
        const char* t = __TIME__; // "HH:MM:SS"
        const int month = BuildMonthNumber();

        BuildStamp b{};
        b.text[0]  = d[7];                          // year
        b.text[1]  = d[8];
        b.text[2]  = d[9];
        b.text[3]  = d[10];
        b.text[4]  = '-';
        b.text[5]  = static_cast<char>('0' + month / 10);
        b.text[6]  = static_cast<char>('0' + month % 10);
        b.text[7]  = '-';
        b.text[8]  = (d[4] == ' ') ? '0' : d[4];    // day (un-pad leading space)
        b.text[9]  = d[5];
        b.text[10] = ' ';
        b.text[11] = t[0];                          // time HH:MM:SS
        b.text[12] = t[1];
        b.text[13] = ':';
        b.text[14] = t[3];
        b.text[15] = t[4];
        b.text[16] = ':';
        b.text[17] = t[6];
        b.text[18] = t[7];
        b.text[19] = '\0';
        return b;
    }

    inline constexpr BuildStamp kBuildStamp = MakeBuildStamp();

} // namespace detail

// "YYYY-MM-DD HH:MM:SS" build timestamp (compile-time, zero runtime cost).
inline constexpr const char* kBuildStamp = detail::kBuildStamp.text;

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_BUILD_INFO_H
