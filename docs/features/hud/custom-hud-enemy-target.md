# Custom HUD Enemy Target

Shows the opponent the local player last damaged: nickname, hunter portrait, HP, and the
mode-appropriate score. Native MPH draws the same information on the bottom screen; this is the
Custom HUD equivalent on the top-screen overlay.

Settings: **HUD ENEMY TARGET** section in the Custom HUD dialog, and element **Enemy Target** in
the in-game layout editor. Keys are `Metroid.Visual.HudEnemyTarget*`; `HudEnemyTargetShow`
defaults to `true`.

The dialog section opens with a note row pointing at **Show Enemy HP Meter Online**
(`Metroid.GameFeature.ShowEnemyHpMeterOnline`, under GAME FEATURE IMPROVEMENTS), which online
play additionally needs — see [Nintendo WFC](#nintendo-wfc). It is a wrapped `HWType::Label`
row, not a tooltip, so it is readable without hovering. Nothing is disabled when that fix is
off: the dependency is online-only and the panel works in Local Wireless regardless.

Native and Custom HUD render independently, and both are allowed to be visible at once — this
feature does not suppress the native Enemy Target HUD.

## Where the state comes from

The ROM already maintains this. Custom HUD only reads it.

```text
local player damages an opponent
  -> Player_TakeDamage confirms attacker == local player
  -> HUD event 0x3D with the victim slot
  -> (vanilla returns early here on Nintendo WFC)
  -> targetSlot = victimSlot, targetTimer = 60
  -> a separate ROM routine decrements the timer, and sets targetSlot = -1 at zero
```

So the whole feature is two `s16` words. At ~30 gameplay ticks/sec the panel lives about two
seconds, and a further hit on the same opponent re-arms the timer to 60.

| Version | targetTimer | targetSlot |
| --- | ---: | ---: |
| JP1_0 | `0x020E0574` | `0x020E0584` |
| JP1_1 | `0x020E0534` | `0x020E0544` |
| US1_0 | `0x020DE6B8` | `0x020DE6B4` |
| US1_1 | `0x020DEF38` | `0x020DEF34` |
| EU1_0 | `0x020DEF58` | `0x020DEF54` |
| EU1_1 | `0x020DEFD8` | `0x020DEFD4` |
| KR1_0 | `0x020D7D2E` | `0x020D7D30` |

US1_0 is the one version where the two words appear in the opposite order; KR1_0 sits at
`0x020D7D00 + 0x2E/+0x30`. Both are in the shared X-macro table as
`RomAddresses::enemyTargetTimer` / `enemyTargetSlot`, so they follow the existing `RomGroup`
detection rather than a second per-version switch.

## Everything else is read live

Once the slot is known, the panel reads current values every frame it is up, so damage taken
during those two seconds is reflected immediately. Nothing is snapshotted at hit time.

| Field | Source |
| --- | --- |
| current HP | `player + 0x0DA` (u16) |
| max HP | `player + 0x0DC` (u16) |
| TeamIndex | `player + 0x4AD` (u8) |
| Hunter ID | `runtime + 0x03C + slot` |
| nickname | `nameBase + slot * 0x15` |
| points / time / lives | match runtime, per mode (below) |

`player = playerStructStart + slot * 0xF30`. Nickname, hunter, and team come from the existing
per-match scoreboard roster cache, so the only per-frame RAM reads this element adds are the
target pair, the HP pair, and the mode value/goal.

## Mode-specific score

Resolved from the match runtime's own `GameMode` at `runtime + 0x000`, not from the native
formatter's internal selector.

| Mode | Shown |
| --- | --- |
| Survival, Survival Teams | lives left: `max(PointGoal - TeamDeaths[teamIndex], 0)` |
| Defender, Defender Teams, Prime Hunter | `PlayerTime[slot] / TimeGoal`, formatted as time |
| Battle, Capture, Bounty, Nodes (and Teams variants) | points `/ PointGoal` |

Team modes show the team aggregate (`TeamPoints[teamIndex]`) rather than the target's personal
points. A goal of zero renders as the bare value instead of `x / 0`.

## Display options

`HudEnemyTargetHpMode` selects how HP is drawn:

| Mode | Meaning |
| --- | --- |
| 0 Segmented | native two-bar Energy Tank meter (tank = 100), matching MphRead `DrawOpponent()` |
| 1 Single Bar | one bar across the whole of `maxHp` |
| 2 Number Only | numeric `hp/maxHp`, no bar |
| 3 Off | no HP row |

`HudEnemyTargetHpNumberShow` adds the numeric readout alongside a bar. Name, hunter portrait,
and score each have their own toggle, and `HudEnemyTargetTeamColor` tints the nickname with the
target's team color in Team modes.

## Nintendo WFC

Vanilla suppresses the *state generation* online: the event `0x3D` handler returns before
writing `targetSlot`/`targetTimer` when session-state bit 0 is set. That is why the native
Enemy Target HUD never appears in online matches.

Custom HUD does not second-guess that gate. Enable the existing **Friend/Rival Wi-Fi Active
Bitset Fix**-adjacent patch `Metroid.GameFeature.ShowEnemyHpMeterOnline`, which NOPs the WFC
early return, and the ROM then produces the target state online — at which point both the
native HUD and this element show it.

| Version | session state bits |
| --- | ---: |
| JP1_0 | `0x020ECC30` |
| JP1_1 | `0x020ECBF0` |
| US1_0 | `0x020EAAF0` |
| US1_1 | `0x020EB5B0` |
| EU1_0 | `0x020EB5D0` |
| EU1_1 | `0x020EB650` |
| KR1_0 | `0x020E4380` |

That word is a bitset of several states; never write 0/1 over the whole word.

## Files

| File | Role |
| --- | --- |
| `MelonPrimeGameRomAddrTable.h` | `enemyTargetTimer` / `enemyTargetSlot` rows |
| `MelonPrimeHudRenderRuntime.inc` | `EnemyTargetSnapshot`, snapshot reader, frame-cache slot |
| `MelonPrimeHudRenderConfig.inc` | `EnemyTargetHudConfig`, `LoadEnemyTargetConfig()` |
| `MelonPrimeHudRenderAssets.inc` | `EnsureEnemyTargetAssetsLoaded()` (own portrait raster set) |
| `MelonPrimeHudRenderDraw.inc` | layout, draw, preview snapshot |
| `MelonPrimeHudRenderMain.inc` | render call, edit-element count |
| `MelonPrimeHudConfigOnScreenDefs.inc` / `Draw.inc` | edit descriptors, element 15 bounds/preview |
| `MelonPrimeHudEditorSidePanelRows.inc` | side-panel rows |
| `InputConfig/MelonPrimeInputConfigHudPreviews.inc` | settings-dialog preview |

## Render plan

The panel is only up for ~2s after a hit, but that window lands mid-firefight, where a per-frame
hitch is least affordable. `EnemyTargetRenderPlan` caches everything that survives a frame and
rebuilds only when something that changes the drawn result changes.

Cached: the placed layout, the formatted HP/score strings, each row's measured advance, and the
`QPainterPath` glyph outlines. Rebuild key: config epoch, font generation, base font px,
`hudScale`, `stretchX` (a resize re-runs `RecomputeAnchorPositions()` without bumping the epoch),
plus the content — slot, hunter, team, mode, name, HP, and the mode value/goal.

`EnemyTargetSnapshot::timer` is deliberately **not** in the key: it ticks every frame and changes
nothing that is drawn, so keying on it would defeat the cache entirely. Within the window the
slot, name, hunter, and goal are fixed and HP only moves when damage lands, so the overwhelming
majority of frames reuse the plan whole and issue nothing but draw calls.

Measured on the developer machine (MinGW Release, median of 5 x 20000 iterations, isolated
benchmark of exactly the work the plan removes — three `QPainterPath::addText()` calls, six
`QFontMetrics::horizontalAdvance()` calls, and the two string formats):

| Path | Per frame |
| --- | ---: |
| rebuilt every frame (pre-plan) | 37.879 us |
| plan cache hit | 0.005 us |

`QPainterPath::addText()` dominates that figure; it is glyph-outline extraction, and outlines are
on by default. End-to-end in-game frame-time impact is not measured here.

## Rules this implementation follows

- The target is never inferred from HP deltas or the crosshair.
- The timer is never decremented Custom-HUD-side; the ROM owns it.
- No hit-time snapshot of HP/score — everything but the slot is read live.
- RAM is read once per emulated frame into the snapshot; repeated paints reuse the frame cache,
  including the "no target" result.
- The mode value comes from the runtime `GameMode`, not the native formatter's selector.
- Adventure is excluded, matching the scoreboard.

## Verification

Runtime verification is the owner's. Acceptance criteria:

- Local Wireless: hit an opponent, panel appears for ~2s.
- WFC with `ShowEnemyHpMeterOnline` off: no state generated (vanilla behavior).
- WFC with it on: native and Custom HUD both show the panel.
- Repeated hits on the same target refresh the timer; hitting a different one switches slot.
- HP updates live while the panel is up.
- All four player slots, TEAM 1 and TEAM 2.
- Battle, Battle Teams, Survival, Survival Teams, Capture, Bounty, Bounty Teams, Nodes, Nodes
  Teams, Defender, Defender Teams, Prime Hunter.
- Pause / START scoreboard, savestate load, renderer switch, Custom HUD off.
- All seven ROM versions.

## Analysis source

`mphAnalysis/Battle/Enemy-Target-HUD/` — the event/render/timer/reset function notes, the
all-version address map, and the implementation spec.
