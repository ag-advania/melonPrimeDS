#ifdef MELONPRIME_DS

#include "MelonPrimeWifiReconnectFix.h"

#include "Config.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "NDS.h"

namespace MelonPrime {

void WifiReconnectFix_Publish(
    melonDS::NDS* nds,
    Config::Table& cfg,
    const RomAddresses* rom)
{
    if (!nds)
        return;

    // No ROM detected or the option is off -> 0, which makes Wifi::OnClientAssociated()
    // a no-op and leaves guest memory untouched.
    const uint32_t addr =
        (rom && cfg.GetBool(CfgKey::WifiReconnect)) ? rom->crtErrno : 0u;

    nds->Wifi.SetMphReconnectErrnoAddress(addr);
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
