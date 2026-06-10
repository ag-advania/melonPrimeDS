# No Picking Up Specific Items - 実装メモ

## 目的

`Power-Ups: Pick Up With No Effect` 系の設定で、特定のパワーアップを
「拾った扱いで消すが、効果は発動しない」ようにする。

対象は次の3つだけ。

| Item type | Item |
| ---: | --- |
| `3` | Double Damage |
| `17` | Cloak |
| `20` | Deathalt |

Imperialist (`8`) と Shock Coil (`11`) は対象外。

## 仕組み

MPH の pickup 処理は `item+0x4C` の item type を `r0` に読み、switch table で
各アイテムの pickup handler へ分岐する。

```text
ldrsh r0, [item, #0x4C]
cmp   r0, #0x15
addls pc, pc, r0, lsl #2
```

このパッチは switch table のうち item type `3` / `17` / `20` の entry だけを
`pickedUp=1` の consume/delete exit へ飛ぶ branch に置き換える。

hot path では実行時 hook を使わない。設定変更や ROM 起動時だけ ARM9 RAM の
該当 word を検証して書き換える。

## Entry address

`addls pc, pc, r0, lsl #2` は ARM pipeline の PC bias があるので、
switch table entry は次で計算する。

```text
entry = addlsAddress + 8 + itemType * 4
```

絶対番地を直接列挙するだけだと、ROM layout の違いを読み違えやすい。
特に US/EU1.1 では:

| Item type | Entry |
| ---: | ---: |
| `8` Imperialist | `0x02019D18` |
| `11` Shock Coil | `0x02019D24` |
| `17` Cloak | `0x02019D3C` |
| `20` Deathalt | `0x02019D48` |

JP の Cloak/Deathalt entry と同じ見た目の番地を US/EU1.1 に流用すると、
Imperialist/Shock Coil 側へ刺さる危険がある。

## ROM groups

ROM group order:

```text
JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
```

| ROM | cmp | addls | DD | Cloak | Deathalt |
| --- | ---: | ---: | ---: | ---: | ---: |
| JP1.0 | `0x02019CC8` | `0x02019CCC` | `0x02019CE0` | `0x02019D18` | `0x02019D24` |
| JP1.1 | `0x02019CC8` | `0x02019CCC` | `0x02019CE0` | `0x02019D18` | `0x02019D24` |
| US1.0 | `0x02019CEC` | `0x02019CF0` | `0x02019D04` | `0x02019D3C` | `0x02019D48` |
| US1.1 | `0x02019CEC` | `0x02019CF0` | `0x02019D04` | `0x02019D3C` | `0x02019D48` |
| EU1.0 | `0x02019CE4` | `0x02019CE8` | `0x02019CFC` | `0x02019D34` | `0x02019D40` |
| EU1.1 | `0x02019CEC` | `0x02019CF0` | `0x02019D04` | `0x02019D3C` | `0x02019D48` |
| KR1.0 | `0x02018C20` | `0x02018C24` | `0x02018C38` | `0x02018C70` | `0x02018C7C` |

実装側では `static_assert` で全 ROM group の計算結果を固定している。

## Patch words

JP/US/EU:

| Item | apply | restore | legacy skip |
| --- | ---: | ---: | ---: |
| Double Damage | `0xEA0001BC` | `0xEA000139` | `0xEA0001C1` |
| Cloak | `0xEA0001AE` | `0xEA00013C` | `0xEA0001B3` |
| Deathalt | `0xEA0001AB` | `0xEA00014A` | `0xEA0001B0` |

KR:

| Item | apply | restore | legacy skip |
| --- | ---: | ---: | ---: |
| Double Damage | `0xEA0001C2` | `0xEA000140` | `0xEA0001C7` |
| Cloak | `0xEA0001B4` | `0xEA000142` | `0xEA0001B9` |
| Deathalt | `0xEA0001B1` | `0xEA00014F` | `0xEA0001B6` |

`legacy skip` は以前の skip-to-next 実装から復元/移行するためだけに許可する。
任意の ARM branch を「たぶん既存パッチ」として受け入れない。

## Safety checks

適用前に必ず次を確認する。

```text
cmpAddress   word == 0xE3500015  // cmp r0,#0x15
addLsAddress word == 0x908FF100  // addls pc,pc,r0,lsl #2
```

各 entry の現在値は、次のいずれかの場合だけ書き換える。

```text
restore value
apply value
legacy skip value
```

これにより、ROM hack や別パッチで switch table が変わっている場合は触らない。

## Config keys

```text
Metroid.GameFeature.PowerUpPickupNoEffectPowerUps
Metroid.GameFeature.PowerUpPickupNoEffectDoubleDamage
Metroid.GameFeature.PowerUpPickupNoEffectCloak
Metroid.GameFeature.PowerUpPickupNoEffectDeathalt
```

`PowerUpPickupNoEffectPowerUps` が true の場合は3種すべてを有効化する。
個別キーはそれぞれの item type だけを有効化する。

## Performance

このパッチは static word patch 方式。

```text
runtime hook なし
JIT trampoline 追加なし
pickup 処理中の C++ callback なし
```

負荷がかかる処理は `ApplyOnce` / `RestoreOnce` のタイミングだけで、通常の
pickup 実行中には追加コストがない。
