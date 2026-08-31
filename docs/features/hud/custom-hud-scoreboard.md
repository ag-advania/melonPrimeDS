# Custom HUD scoreboard

## Scope

The Custom HUD scoreboard is a host-rendered composite built from the running
match state. It is not the ROM's native START/TAB scoreboard and does not patch
the guest scoreboard renderer. This page owns behaviour that cannot be inferred
from the generated property list: roster sampling, result order, team rows,
hunter icon variants, retained-plan invalidation, and runtime boundaries.

For every user-editable key, default, type, and surface, use the
[generated HUD property schema](../../generated/hud/MelonPrimeHudPropSchemaPhase2a.md).
For preview/import/edit workflow, use the
[Custom HUD settings guide](custom-hud-settings.md).

## Visibility and setting boundary

`Metroid.Visual.HudScoreboardShow` controls whether the composite is eligible
to draw. The scoreboard remains subject to the normal Custom HUD enable state,
gameplay-state timing, match availability, and its own visibility snapshot.
It can be configured as an always-on gameplay element; that does not mean
“force the native scoreboard open”.

Position, panel dimensions, row sizing, colors, typography, outline, icon
sizes, and displayed cells are the `Metroid.Visual.HudScoreboard*` family.
Those properties are schema-owned. The dialog preview is representative only;
the runtime uses live player, team, mode, rank, score, and result-order data.

## Runtime data model

`SampleScoreboardSnapshot()` separates physical player slots from display
order:

```text
playersBySlot[0..3]  physical roster records and live values
resultSlots[0..3]    slot IDs in the order the game currently displays them
```

Each player snapshot includes whether the slot is active, hunter ID, team,
standing, team standing, score/metric data, rank/stars, and decoded name.
`resultSlots` may change as standings change. It never renumbers the physical
slot or changes which duplicate-hunter color belongs to that player.

Match-static data is cached after match join and keyed by NDS/RAM/ROM identity
plus match serial. Live metrics and `resultSlots` are refreshed per emulated
frame. Match leave, stop, identity change, or a new match serial invalidates
the old snapshot.

## Hunter icon and color selection

There are 28 scoreboard portraits: seven hunters times four in-game player
color variants.

| Hunter ID | Hunter | Resource variants |
| ---: | --- | --- |
| 0 | Samus | p1, p2, p3, p4 |
| 1 | Kanden | p1, p2, p3, p4 |
| 2 | Trace | p1, p2, p3, p4 |
| 3 | Sylux | p1, p2, p3, p4 |
| 4 | Noxus | p1, p2, p3, p4 |
| 5 | Spire | p1, p2, p3, p4 |
| 6 | Weavel | p1, p2, p3, p4 |

Assets live under `res/assets/scoreboard/hunter_icons/` and are registered as
`:/mph-scoreboard-hunter-<hunter>-p<variant>` in `res/melon.qrc`.
`MelonPrimeHudRenderAssets.inc` scales them to the configured hunter-icon
height and stores them in `scoreboardHunterIcons[7][4]`.

`ResolveScoreboardHunterVariant(snapshot, slot)` follows physical active-slot
order:

```text
variant = number of lower active slots using the same hunter
variant = min(variant, 3)
```

Thus the first active Samus slot gets p1, the next active Samus slot gets p2,
and so on. Different hunters each begin at p1. Invalid/inactive slot or hunter
data safely resolves to variant zero before the later validity check decides
whether an image can be used.

### Why result order must not select the variant

Suppose physical slots 0 and 3 both use Trace. Slot 0 is p1 and slot 3 is p2.
If slot 3 moves above slot 0 in `resultSlots`, their rows swap, but their icon
colors do not. Counting duplicates in display order would make colors change
whenever standings change, contradicting the game's player-position ownership.

Team grouping also does not change the rule. Team rows and result order decide
where a player is drawn; `playersBySlot` decides who the player is and which
duplicate-hunter variant belongs to that slot.

## Retained render plan

The scoreboard uses a retained `ScoreboardRenderPlan` rather than formatting,
measuring, and laying out every string on every host frame. Its structural key
contains:

- match serial, game mode, active-player count, team flags, team mode, and
  scoreboard visibility;
- current `resultSlots`;
- each physical slot's team index, hunter ID, and active flag.

Hunter ID and active state are structural because changing either can change
the icon and the duplicate-hunter variant of later slots. A planned player row
stores both `hunterId` and the resolved `hunterVariant`; drawing then indexes
`scoreboardHunterIcons[hunterId][hunterVariant]` without recomputing ownership.

Dynamic score/time/standing cells use semantic value keys and can update in
place when the formatted value still fits. Name decoding and other match-static
reads remain outside the high-refresh draw loop. Font/metrics and outline paths
have separate caches so config, scale, or text changes invalidate only the
state that depends on them.

## Settings dialog and in-game editor

The Custom HUD tab's `ScoreboardPreviewWidget` uses a synthetic four-player
snapshot and the same broad layout rules. It confirms colors, sizing, spacing,
font, and panel presentation but cannot prove runtime address sampling or live
result ordering.

In-game edit mode exposes the scoreboard as element index 14, default anchor
3 (middle-left). Moving/resizing/editing writes the same schema-owned keys as
the settings dialog. The preview/editor do not create an independent scoreboard
configuration namespace.

## Source ownership

| Source | Responsibility |
| --- | --- |
| `MelonPrimeHudRuntimeSample.inc` | match cache, slot records, live metrics, `resultSlots` |
| `MelonPrimeHudRenderDraw.inc` | variant resolver, row construction, structural key, retained plan, draw |
| `MelonPrimeHudRenderPlan.inc` | structure/render-plan types |
| `MelonPrimeHudRenderAssets.inc` | 7x4 hunter image loading/scaling |
| `MelonPrimeHudRenderConfig.inc` | per-instance image/cache ownership |
| `MelonPrimeHudPropSchema.inc` | user-editable scoreboard property contract |
| `InputConfig/MelonPrimeInputConfigHudPreviews.inc` | dialog preview |
| `res/melon.qrc` | resource aliases |

## Reverse-engineering references without duplication

The current host rendering contract above is owned by melonPrimeDS source.
Guest scoreboard geometry and source-field investigations remain in mphCodex:

- `C:\Users\Admin\Documents\git\mphCodex\mphAnalysis\Battle\Scoreboard\Custom-HUD\0203BD14-scoreboard-hunter-icon-bootstrap-JP1_0.md`
- `C:\Users\Admin\Documents\git\mphCodex\mphAnalysis\Battle\Scoreboard\Match-Scoreboard-Native-Geometry-AllVersions-JP1_0.md`
- `C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\Match\current\Battle-Runtime-Result-Struct.md`

These links provide evidence and native-game context rather than a second copy
of this implementation document. If paths move in mphCodex, locate the same
filename; do not silently paste an old investigation into this repository.

## Verification checklist

- One instance of each hunter uses p1.
- Two to four active physical slots with the same hunter receive p1 through p4
  in ascending slot order.
- Reorder `resultSlots` by changing standings; row order changes while icon
  variants remain attached to physical slots.
- Mix duplicate hunters across teams and verify team grouping does not reassign
  variants.
- Mark a lower duplicate slot inactive and verify later variants compact based
  on active slots only.
- Exercise hunter ID/active/roster changes and confirm the retained structure
  rebuilds instead of drawing a stale portrait.
- Test solo/team modes, ties, result transitions, match leave/rejoin, and a new
  match without restarting the emulator.
- Compare dialog preview, in-game edit mode, and runtime at multiple HUD scales.
- Confirm missing/invalid images fail safely and never index outside `[7][4]`.
- Treat static source review and preview checks separately from an actual
  multiplayer runtime test.
