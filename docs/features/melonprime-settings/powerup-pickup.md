# Power-up pickup patches

## Purpose

The Power-up Pickup controls remove selected pickup types from the guest pickup
flow. They are not inventory unlocks and they do not grant the item directly.
The patch changes the pickup branch so the selected item is not accepted by
the ordinary pickup handler.

| Control | Key | Default |
| --- | --- | --- |
| Power-Ups: Pick Up With No Effect | Metroid.GameFeature.PowerUpPickupNoEffectPowerUps | false |
| Double Damage pickup | Metroid.GameFeature.PowerUpPickupNoEffectDoubleDamage | false |
| Cloak pickup | Metroid.GameFeature.PowerUpPickupNoEffectCloak | false |
| Deathalt pickup | Metroid.GameFeature.PowerUpPickupNoEffectDeathalt | false |

The child controls are subordinate to the parent. When the parent is checked,
all three child values are forced on and the individual controls are disabled.
When the parent is unchecked, the child controls are enabled and can be
selected independently. A stale child checkbox must not cause a child patch to
appear while the parent is checked with a contradictory state.

## Item types

| Item | Item type |
| --- | ---: |
| Double damage | 3 |
| Cloak | 17 |
| Deathalt | 20 |

The implementation uses the item type in the pickup dispatch calculation. It
does not identify an item by a translated display string.

The common JP/US/EU branch values are:

| Item | Apply | Restore | Legacy skip accepted by guard |
| --- | ---: | ---: | ---: |
| Double damage | 0xEA0001BC | 0xEA000139 | 0xEA0001C1 |
| Cloak | 0xEA0001AE | 0xEA00013C | 0xEA0001B3 |
| Deathalt | 0xEA0001AB | 0xEA00014A | 0xEA0001B0 |

KR has a distinct table:

| Item | Apply | Restore | Legacy skip accepted by guard |
| --- | ---: | ---: | ---: |
| Double damage | 0xEA0001C2 | 0xEA000140 | 0xEA0001C7 |
| Cloak | 0xEA0001B4 | 0xEA000142 | 0xEA0001B9 |
| Deathalt | 0xEA0001B1 | 0xEA00014F | 0xEA0001B6 |

## ROM table

Each ROM has a switch compare/add instruction pair and three entry locations.
The entries correspond to the item types above.

| ROM | Switch compare | Switch add | Double damage entry | Cloak entry | Deathalt entry |
| --- | ---: | ---: | ---: | ---: | ---: |
| JP1.0 / JP1.1 | 0x02019CC8 | 0x02019CCC | 0x02019CE0 | 0x02019D18 | 0x02019D24 |
| US1.0 / US1.1 | 0x02019CEC | 0x02019CF0 | 0x02019D04 | 0x02019D3C | 0x02019D48 |
| EU1.0 | 0x02019CE4 | 0x02019CE8 | 0x02019CFC | 0x02019D34 | 0x02019D40 |
| EU1.1 | 0x02019CEC | 0x02019CF0 | 0x02019D04 | 0x02019D3C | 0x02019D48 |
| KR1.0 | 0x02018C20 | 0x02018C24 | 0x02018C38 | 0x02018C70 | 0x02018C7C |

The entry address is derived from the validated pickup layout:

~~~text
entry = add_instruction + 8 + (item_type * 4)
~~~

The implementation validates this layout instead of trusting the derived
address alone. The compare/add words and each target entry must be the
expected original or already-applied value before the patch writes.

## Apply and restore contract

For each selected item:

1. validate the ROM-specific switch sequence and entry layout;
2. validate the current entry word;
3. write the branch replacement for the selected item;
4. leave unselected entries unchanged; and
5. restore only the entry words owned by this feature. The switch compare and
   add words are validation anchors; this feature does not replace them.

An invalid or mixed layout fails closed. This is particularly important when
switch entries are shared by multiple child options. A successful write
indicates a guarded instruction transition, not that the item can never be
received through an unrelated game event.

The registry handles match/config reload and leave/stop restoration. Test
restoration explicitly instead of relying only on process restart.

## Interaction with unlock and damage settings

Power-up pickup suppression is independent of
Metroid.Data.Unlock. Unlock makes game data available; it should not be
interpreted as a pickup filter. Double damage pickup suppression is also
independent of Disable Double Damage Multiplier, which changes a damage
calculation path. A test must state which of these is enabled.

## Verification checklist

- Verify the checked parent forces all children on and disables their
  individual controls.
- Verify the unchecked parent enables independent child selection.
- Test each item type independently.
- Confirm an unselected item remains on the ordinary pickup path.
- Confirm guard failure produces no partial writes.
- Verify match leave/stop restore and a second match.
- Test in conjunction with Data Unlock and damage multiplier controls while
  keeping their results separate.
- Record ROM revision and exact item type when reporting a result.

## Evidence and related material

Current source:

- MelonPrimePatchNoPickingUpSpecificItems.cpp
- MelonPrimeInputConfig.cpp
- MelonPrimePatchRegistry.cpp

The longer patch design and validation notes are maintained in
docs/architecture/gameplay/patches/no-picking-up-specific-items.md.

Supporting reverse-engineering material:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\NoPickUp\AllItemInfo\current\Item-Pickup-Switch-Investigation-AllVersions.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\NoPickUp\PickUpButNoEffect\current\Powerup-Pickup-Consume-NoEffect-AI-Instructions-AllVersions.md
