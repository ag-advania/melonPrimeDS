# Wi-Fi Reconnect Fix (Error 52200)

Metroid Prime Hunters fails with **Error 52200** when it connects to Nintendo WFC / Wiimmfi a
second time in the same emulator session. The first connection succeeds; every later attempt
fails until the emulator is restarted.

Setting: **Bug Fixes -> Wi-Fi Reconnect Fix**, config key `Metroid.BugFix.WifiReconnect`,
default `true`.

## Root cause

MPH's DWC connection test parses numbers with its own `atoi()` wrapper, which is really
`strtol(s, NULL, 10)`, and then inspects the C runtime `errno` global — without clearing it
first.

`strtol()` correctly stores `ERANGE` (34 = `0x22`) when an input overflows 32 bits. `errno` is
an ordinary guest global, so that value survives for the rest of the boot:

```text
boot                      -> CRT errno = 0
1st WFC connection        -> a strtol overflow sets errno = 34, connection still succeeds
disconnect
2nd WFC connection        -> parse is fine, but the stale errno == ERANGE is read
                          -> DWC connection test treated as failed
                          -> Error 52200
```

This is not Wiimmfi refusing the reconnect: it is guest state left over from the previous
connection attempt.

## Fix

A successful virtual-AP association is the boundary of a new connection attempt, so that is
where the configured guest word is restored to its boot-fresh `0`.

```text
MelonPrimeGameRomDetect  -- resolves RomGroup
        |
        v
WifiReconnectFix_Publish -- RomAddresses::crtErrno, or 0 when off / non-MPH
        |
        v
Wifi::SetMphReconnectErrnoAddress
        |
WifiAP::HandleManagementFrame -- assoc request accepted, ClientStatus = 2
        |
        v
Wifi::OnClientAssociated -- ARM9Read32(addr); if nonzero, ARM9Write32(addr, 0)
```

Same principle as the `mstan/MetroidPrimeHuntersRecomp` v0.4.1 fix
(`network.wfc.clear_crt_errno_addr`), which validated it with repeated same-session reconnect
cycles.

## Guest CRT `errno` addresses

Confirmed by disassembling each version's `atoi` wrapper and its `ERANGE` store site.

| Version | `atoi` wrapper | `strtol` family | CRT `errno` |
| --- | ---: | ---: | ---: |
| JP1_0 | `0x020A8160` | `0x020A8174` | `0x0210432C` |
| JP1_1 | `0x020A811C` | `0x020A8130` | `0x021042EC` |
| US1_0 | `0x020A6438` | `0x020A644C` | `0x021021EC` |
| US1_1 | `0x020A6C8C` | `0x020A6CA0` | `0x02102CAC` |
| EU1_0 | `0x020A6CC4` | `0x020A6CD8` | `0x02102CCC` |
| EU1_1 | `0x020A6D38` | `0x020A6D4C` | `0x02102D4C` |
| KR1_0 | `0x020A0240` | `0x020A0178` | `0x020FBFC0` |

KR1.0 is the only version whose `strtol` body sits below its wrapper; the semantics are
identical.

The addresses live in the shared X-macro table
(`MelonPrimeGameRomAddrTable.h`, `MP_ROM_FIELDS_DS_WIFI` -> `RomAddresses::crtErrno`), so they
follow the existing `RomGroup` detection (checksum first, NDS header `gameCode` + revision as
the fallback for trimmed/modified dumps) instead of a second per-version switch.

## Files

| File | Role |
| --- | --- |
| `MelonPrimeGameRomAddrTable.h` | `crtErrno` row for all seven versions |
| `MelonPrimeWifiReconnectFix.{h,cpp}` | reads the setting, publishes the address to `Wifi` |
| `MelonPrimeGameRomDetect.cpp` | clears on entry, publishes once the `RomGroup` is known |
| `MelonPrimeLifecycle.cpp` | republishes from `ApplyConfigReload()` |
| `src/Wifi.{h,cpp}` | holds the address, `OnClientAssociated()` performs the clear |
| `src/WifiAP.cpp` | calls back the moment `ClientStatus = 2` |

## Design notes

- **Not in `MelonPrimePatchFixWifi.cpp`.** That patch is the Friend/Rival active-bitset fix: a
  51-word ARM9 instruction rewrite for JP1.0 / US1.0 / EU1.0 only, with its own signature check
  and apply/revert state. This one is a guest runtime-state reset for all seven versions, tied
  to a Wi-Fi lifecycle event. Separate bugs, separate settings.
- **Not in `MelonPrimePatchRegistry`.** The registry's apply/restore sites (game join, config
  reload, out-of-game frame, leave, stop) cannot express "at the instant an association
  succeeds", and there is nothing to revert.
- **Not serialized.** `Wifi::MphReconnectErrnoAddr` is host configuration derived from the
  loaded ROM, so it stays out of `Wifi::DoSavestate()`. The guest `errno` word itself is
  ordinary main RAM and is already covered by savestates; after a load, the next association
  clears it again.
- **Clears any nonzero value, not just 34.** Association marks the start of a new attempt, so
  reproducing boot state exactly is the point. Clearing only `34` would leave other stale
  values behind for no benefit.
- **Association only, never per frame.** `errno` is ordinary CRT state; zeroing it every frame
  would destroy legitimate error reporting. Clearing after the DWC parse would be too late.
- **`strtol` is left alone.** Storing `ERANGE` on overflow is correct CRT behavior; the bug is
  the missing clear before the check.
- **Error 52200 is never forced to success.** Genuine connection failures still surface.
- **No effect outside MPH.** A non-MPH ROM, an unrecognized `gameCode`, or the setting turned
  off all publish address `0`, and `Wifi::OnClientAssociated()` then touches no guest memory.
- **Runs on the emulation thread.** `WifiAP::HandleManagementFrame()` calls straight into
  `Wifi`, which reads/writes guest RAM inline; there is no dispatch to another thread.

## Verification

Runtime verification is the owner's; the acceptance criteria are:

- All seven versions connect to WFC / Wiimmfi on the first attempt.
- Three disconnect/reconnect cycles in one emulator session, no Error 52200.
- Only the detected version's address is read or written.
- Non-MPH ROM: address `0`, no guest memory write.
- A rejected association request produces no callback.
- Local wireless, save data, and friend/rival data are unaffected.

The strongest causal evidence is an A/B with the setting off (52200 reproduces) and on
(reconnect succeeds).

## Analysis source

`mphAnalysis/WiFi/Wiimmfi-Reconnect-52200/` — per-version `atoi` / `strtol` disassembly notes,
the CRT errno table, and the Recomp fix investigation.
