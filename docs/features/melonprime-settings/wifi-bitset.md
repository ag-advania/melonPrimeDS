# Wi-Fi active friend/rival bitset fix

## Setting contract

| Item | Value |
| --- | --- |
| Key | Metroid.BugFix.WifiBitset |
| Default | true |
| Source | MelonPrimePatchFixWifi.cpp |
| Patch type | 51 guarded ARM9 word replacements |
| Applicable ROMs | JP1.0, US1.0, EU1.0 |

The affected guest code treats a set of active friend/rival slots as if it
were a 64-bit bitset but uses the wrong word/byte path for slot indices 32–59.
The replacement makes the set, test, and rival-slot paths byte-indexed in the
same shape already present in JP1.1, US1.1, EU1.1, and KR1.0.

This is a local guest-side data/logic correction. It does not change the WFC
server, matchmaking, or the number of network slots.

## ROM bases and unsupported groups

| ROM | Base address | Source behavior |
| --- | --- | --- |
| JP1.0 | 0x020662EC | Apply 51-word patch |
| JP1.1 | 0xFFFFFFFF | Not applicable; corrected in ROM |
| US1.0 | 0x020646BC | Apply 51-word patch |
| US1.1 | 0xFFFFFFFF | Not applicable; corrected in ROM |
| EU1.0 | 0x02064F38 | Apply 51-word patch |
| EU1.1 | 0xFFFFFFFF | Not applicable; corrected in ROM |
| KR1.0 | 0xFFFFFFFF | Not applicable; corrected in ROM |

Every listed address is base plus an offset. All three applicable ROMs share
the same apply/revert words; only the base changes.

## Complete patch table

| Offset | Apply value | Revert value | Region / operation |
| ---: | ---: | ---: | --- |
| 0x000 | 0xE2052007 | 0xE3A01001 | set/clear path |
| 0x004 | 0xE3A01001 | 0xE1E01511 | set/clear path |
| 0x008 | 0xE7D031A5 | 0xE5903000 | set/clear path |
| 0x00C | 0xE1E02211 | 0xE5902004 | set/clear path |
| 0x010 | 0xE0033002 | 0xE0033001 | set/clear path |
| 0x014 | 0xE7C031A5 | 0xE0021FC1 | set/clear path |
| 0x018 | 0xE1A00000 | 0xE5803000 | set/clear path |
| 0x01C | 0xE1A00000 | 0xE5801004 | set/clear path |
| 0x028 | 0xE2052007 | 0xE3A01001 | second set path |
| 0x02C | 0xE3A01001 | 0xE5904000 | second set path |
| 0x030 | 0xE7D031A5 | 0xE1A02511 | second set path |
| 0x034 | 0xE1833211 | 0xE5903004 | second set path |
| 0x038 | 0xE7C031A5 | 0xE1844511 | second set path |
| 0x03C | 0xE1A00000 | 0xE1831FC2 | second set path |
| 0x040 | 0xE1A00000 | 0xE5804000 | second set path |
| 0x044 | 0xE1A00000 | 0xE5801004 | second set path |
| 0x0AC | 0xE7D021A5 | 0xE3A01001 | test/read path |
| 0x0B0 | 0xE2051007 | 0xE5903000 | test/read path |
| 0x0B4 | 0xE3A00001 | 0xE1A02511 | test/read path |
| 0x0B8 | 0xE1A01110 | 0xE5900004 | test/read path |
| 0x0BC | 0xE0121001 | 0xE0033511 | test/read path |
| 0x0C0 | 0x03A00000 | 0xE0001FC2 | test/read path |
| 0x0C4 | 0x13A00001 | 0xE3A00000 | test/read path |
| 0x0C8 | 0xE3500000 | 0xE1510000 | test/read path |
| 0x0CC | 0xE1A00000 | 0x01530000 | test/read path |
| 0x73C | 0xE7D021A9 | 0xE1A01914 | friend-slot test A |
| 0x740 | 0xE2091007 | 0xE5902000 | friend-slot test A |
| 0x744 | 0xE3A00001 | 0xE5900004 | friend-slot test A |
| 0x748 | 0xE1A01110 | 0xE0022914 | friend-slot test A |
| 0x74C | 0xE0121001 | 0xE0001FC1 | friend-slot test A |
| 0x750 | 0x03A00000 | 0xE3A00000 | friend-slot test A |
| 0x754 | 0x13A00001 | 0xE1510000 | friend-slot test A |
| 0x758 | 0xE3500000 | 0x01520000 | friend-slot test A |
| 0x7D4 | 0xE7D021A9 | 0xE1A01914 | friend-slot test B |
| 0x7D8 | 0xE2091007 | 0xE5902000 | friend-slot test B |
| 0x7DC | 0xE3A00001 | 0xE5900004 | friend-slot test B |
| 0x7E0 | 0xE1A01110 | 0xE0022914 | friend-slot test B |
| 0x7E4 | 0xE0121001 | 0xE0001FC1 | friend-slot test B |
| 0x7E8 | 0x03A00000 | 0xE3A00000 | friend-slot test B |
| 0x7EC | 0x13A00001 | 0xE1510000 | friend-slot test B |
| 0x7F0 | 0xE3500000 | 0x01520000 | friend-slot test B |
| 0x8CC | 0xE7D021AA | 0xE5901000 | rival-slot test |
| 0x8D0 | 0xE20A1007 | 0xE59D0008 | rival-slot test |
| 0x8D4 | 0xE3A00001 | 0xE1A02A10 | rival-slot test |
| 0x8D8 | 0xE1A01110 | 0xE0010A10 | rival-slot test |
| 0x8DC | 0xE0121001 | 0xE59F1088 | rival-slot test |
| 0x8E0 | 0x03A00000 | 0xE5913004 | rival-slot test |
| 0x8E4 | 0x13A00001 | 0xE3A01000 | rival-slot test |
| 0x8E8 | 0xE3500000 | 0xE0032FC2 | rival-slot test |
| 0x8EC | 0xE1A00000 | 0xE1520001 | rival-slot test |
| 0x8F0 | 0xE1A00000 | 0x01500001 | rival-slot test |

There are 51 entries across six regions. The final address is base plus
0x8F0, not a contiguous 0x8F4-byte replacement.

## Apply guard and lifecycle

The patch first checks every target word. Each current word must equal either
its apply value or its revert value. If any word is foreign, the entire
operation is rejected and no write is performed. If all words are already
apply values, the state becomes Applied without writing. Otherwise all 51
apply values are written.

This prevents a detected ROM group from being treated as a sufficient
signature for a different dump/layout. The current module has no RAM restore
callback in the registry because the applicable ROM's corrected code is
intended to remain in the guest for the session and emulator teardown reloads
the image. The host patch state is reset on ROM detection/emu reset.

## Verification checklist

- Count 51 entries and verify all offsets are four-byte aligned.
- Validate every apply/revert value against the current source.
- Test one foreign word in every region and confirm zero writes.
- Test all-revert, all-applied, and mixed apply/revert states.
- Confirm JP1.1, US1.1, EU1.1, and KR1.0 report not applicable.
- Exercise friend slots 0, 31, 32, 59, and 60 and compare visibility.
- Repeat a second WFC session; this feature is independent of the errno
  reconnect feature.

## References

- src/frontend/qt_sdl/MelonPrimePatchFixWifi.cpp
- src/frontend/qt_sdl/MelonPrimePatchRegistry.cpp
- C:/Users/Admin/Documents/git/mphCodex/mnt/data/analysis/mphAnalysis/topics/network/wifi/current/summary/Friend_Rival_WiFi_JP10_US10_EU10_BitsetFix_PatchProposal_v2.md
