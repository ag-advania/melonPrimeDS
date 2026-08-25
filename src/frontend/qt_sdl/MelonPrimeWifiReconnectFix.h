#ifndef MELON_PRIME_WIFI_RECONNECT_FIX_H
#define MELON_PRIME_WIFI_RECONNECT_FIX_H

#ifdef MELONPRIME_DS

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {

    struct RomAddresses;

    // Same-session WFC / Wiimmfi reconnect fix (Error 52200).
    //
    // Not an ARM9 code patch and deliberately not part of MelonPrimePatchRegistry: the
    // repair is a guest runtime-state reset that has to happen at one precise Wi-Fi
    // lifecycle point (successful virtual-AP association), which the registry's
    // apply/restore sites cannot express. All this module does is publish the address of
    // the guest CRT `errno` word for the detected ROM version to the Wifi component,
    // which owns the association hook. Independent of the Friend/Rival Wi-Fi bitset patch
    // in MelonPrimePatchFixWifi.cpp -- different bug, different ROM versions.
    //
    // Pass rom == nullptr for "no MPH ROM detected". Publishes 0 -- disabling the fix
    // entirely -- when there is no ROM or the user setting is off, so a non-MPH title can
    // never inherit a previous session's address and no guest memory is touched.
    //
    // Emulation thread only: it writes state the Wifi component reads while running.
    void WifiReconnectFix_Publish(
        melonDS::NDS* nds,
        Config::Table& cfg,
        const RomAddresses* rom);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_WIFI_RECONNECT_FIX_H
