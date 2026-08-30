# Shadow Freeze

## Setting contract

| Item | Value |
| --- | --- |
| UI section | Bug Fixes |
| Control | Fix Shadow Freeze |
| Key | Metroid.BugFix.FixShadowFreeze |
| Default | false |
| Source | MelonPrimePatchShadowFreezeRuntimeHook.cpp |
| Patch type | Runtime execution hook; no code-cave or guest word write |

The fix changes the final Ice Wave hit/miss decision for bodies and half
turrets. The original lateral-distance range check remains in the guest. Once
that check reaches its conditional branch, the runtime hook recomputes the
angle test from the full three-dimensional target vector and redirects
execution to the existing hit or miss continuation.

## Runtime decision

At a decision site the original flow is conceptually:

~~~text
cmp  dot, threshold
ble  miss
fall through to hit
~~~

The hook reads the beam direction and position, resolves the target position,
and computes a fixed-point dot product. The result is compared strictly as
`fullDotQ12 > thresholdQ12`: a greater value redirects to the original hit
continuation; otherwise it redirects to the original miss continuation.

The relevant guest structures are:

| Data | Offset | Meaning |
| --- | ---: | --- |
| Body target | CPlayer + 0x1C | Target body position, three signed 32-bit components |
| Half-turret pointer | CPlayer + 0xF24 | Pointer used to resolve the turret target |
| Half-turret target | half-turret + 0x30 | Half-turret position, three signed 32-bit components |
| Beam direction | beam + 0x70 | Normalized Q12 direction vector |
| Beam position | beam + 0xA0 | Beam origin, three signed 32-bit components |

The implementation treats the position components as fx32/Q12 and the beam
direction as normalized Q12. It performs the multiply in a wider integer type,
checks address ranges and arithmetic overflow, and declines to redirect when
the guest pointers or vector calculation are invalid. A failed safety check
falls back to the original instruction execution path.

## ROM-specific hook table

The shared ARM9 dispatcher identifies the current execution PC and selects the
table row for the detected ROM group. The register columns are part of the
contract: JP/US/EU and KR keep the player, beam, and threshold in different
registers.

| ROM | Target | Decision PC | Hit continuation | Miss continuation | Player reg | Beam reg | Threshold reg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| JP1.0 | Body | 0x0203E5FC | 0x0203E600 | 0x0203E62C | r6 | r7 | r1 |
| JP1.0 | Half-turret | 0x0203E734 | 0x0203E738 | 0x0203E764 | r6 | r7 | r1 |
| JP1.1 | Body | 0x0203E5FC | 0x0203E600 | 0x0203E62C | r6 | r7 | r1 |
| JP1.1 | Half-turret | 0x0203E734 | 0x0203E738 | 0x0203E764 | r6 | r7 | r1 |
| US1.0 | Body | 0x0203E4B4 | 0x0203E4B8 | 0x0203E4E4 | r6 | r7 | r1 |
| US1.0 | Half-turret | 0x0203E5EC | 0x0203E5F0 | 0x0203E61C | r6 | r7 | r1 |
| US1.1 | Body | 0x0203E3E4 | 0x0203E3E8 | 0x0203E414 | r6 | r7 | r1 |
| US1.1 | Half-turret | 0x0203E51C | 0x0203E520 | 0x0203E54C | r6 | r7 | r1 |
| EU1.0 | Body | 0x0203E3DC | 0x0203E3E0 | 0x0203E40C | r6 | r7 | r1 |
| EU1.0 | Half-turret | 0x0203E514 | 0x0203E518 | 0x0203E544 | r6 | r7 | r1 |
| EU1.1 | Body | 0x0203E3E4 | 0x0203E3E8 | 0x0203E414 | r6 | r7 | r1 |
| EU1.1 | Half-turret | 0x0203E51C | 0x0203E520 | 0x0203E54C | r6 | r7 | r1 |
| KR1.0 | Body | 0x0203D950 | 0x0203D954 | 0x0203D980 | r5 | r6 | r10 |
| KR1.0 | Half-turret | 0x0203DA78 | 0x0203DA7C | 0x0203DAA8 | r5 | r6 | r10 |

JP1.0 and JP1.1 currently share the same hook PCs, as do the corresponding
US/EU rows where shown. This is still represented as a separate ROM row so
that ROM coverage is explicit and future revision differences cannot be
silently inherited.

## Dispatcher and lifecycle

This feature is registered in the shared `MelonPrimeArm9Hook` dispatcher. When
the match-scoped hook plan has `shadowFreeze` enabled, the module publishes the
two decision PCs for the current ROM group. The module itself is stateless; it
receives the ROM group and register snapshot from the dispatcher and does not
re-read configuration on the hot path.

Because this is a redirect-only runtime hook:

- enabling or changing the setting rebuilds the ARM9 hook plan through the
  normal configuration-change path;
- disabling it removes the dispatcher mask and returns execution to the guest
  code;
- leave, stop, ROM change, and reset remove the match-scoped registration; and
- there is no static ARM9 word to restore and no RAM patch state to serialize.

The source's default is off. The UI warning that the behavior is unstable is
especially important for KR1.0 and should remain visible when the option is
enabled.

## Verification checklist

- Confirm all fourteen decision rows (two targets for each of seven ROM groups)
  are in ARM9 MainRAM and have the expected hit/miss continuations.
- Verify the dispatcher registers exactly the two decision PCs for the active
  ROM and no addresses from another ROM group.
- Test body and half-turret targets with lateral range passing and full-3D
  angle both above and at/below the threshold.
- Test invalid player, half-turret, and beam pointers; the hook must decline to
  redirect and must not read outside MainRAM.
- Test extreme vector values and zero-length vectors; overflow or invalid
  geometry must fall back safely.
- Toggle the option across config reload, leave, stop, ROM change, and rejoin.
- Compare every result against the vanilla path with the option disabled.

Static hook registration and unit-level arithmetic checks do not establish
physical runtime acceptance. The remaining acceptance evidence must include
the supported ROM, game mode, target type, renderer, and actual Ice Wave hit or
miss result.

## References

Current source:

- src/frontend/qt_sdl/MelonPrimePatchShadowFreezeRuntimeHook.cpp
- src/frontend/qt_sdl/MelonPrimePatchShadowFreezeRuntimeHook.h
- src/frontend/qt_sdl/MelonPrimeArm9Hook.cpp
- src/frontend/qt_sdl/MelonPrimeDef.h

The older architecture note is retained as design/reissue history and may
describe a fallback word-patch proposal; this page is the current source
contract:

- [Shadow Freeze runtime-hook architecture note](../../architecture/gameplay/patches/shadow-freeze-runtime-hook.md)

Supporting reverse-engineering material is maintained in mphCodex rather than
copied into this repository:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\maintenance\bugFix\ShadowFreeze\current\summary\Shadow-Freeze-RuntimeHook-NoCave-AllVersions.md
