# Custom HUD Scoreboard — Native Layout

The Custom HUD scoreboard can lay itself out two ways.

- **Native Layout** (`Metroid.Visual.HudScoreboardNativeLayout`, default `true`) reproduces the
  ROM's own START scoreboard geometry: the same anchors, the same row spacings, and the same
  height-then-centre placement the game uses on the bottom screen.
- **Auto layout** (the toggle off) keeps the Custom HUD's own table, which sizes its columns
  from the actual fonts and assets and is placed by the anchor, offsets and gap settings.

Settings: **HUD SCOREBOARD** section in the Custom HUD dialog, element **Scoreboard** in the
in-game layout editor, and the editor side panel. The toggle is available on all three, and the
settings-dialog preview reflects whichever layout is selected.

## The ROM geometry

All seven ROM versions use the same layout constants and the same relative placement; only the
data addresses differ. There is therefore no per-version geometry table — see
`MelonPrimeHudScoreboardNativeGeometry.h`, whose reference is
`mphAnalysis/Battle/Scoreboard/Match-Scoreboard-Native-Geometry-AllVersions-JP1_0.md`
(JP1_0 renderer `0202A99C`, player row `0202BC38`, height helper `0202BD30`).

Horizontal anchors, in DS pixels on a 256×192 screen:

| Element | X | Y | ROM anchor | Custom HUD |
| --- | ---: | ---: | --- | --- |
| Hunter portrait | `20` | `rowY - 13` | top-left | centred on (`36`, `rowY`) |
| Stars | `60` (second piece `92`) | `rowY` | top-left | centred on (`92`, `rowY`) |
| Nickname | `92` | `rowY - 9` | centre / line top | centred on (`92`, `rowY - 9`) |
| Team label | `42` | `rowY` | centre / line top | centred on (`42`, `rowY`) |
| Column 1 value | `160` | `rowY` | centre / line top | centred on (`160`, `rowY`) |
| Column 2 value | `215` | `rowY` | centre / line top | centred on (`215`, `rowY`) |

The two statistics columns are fixed for every game mode: Points, Time, Octoliths, Kills and
Deaths all land on the same two centres.

Vertically the board is measured before it is placed. Four spacings drive it — start `13`, team
gap `4`, team line `18`, player line `28` — and the whole height is then centred on **Y = 104**,
eight pixels below the physical screen centre:

```text
posY = 104 - (height >> 1)
```

The shift is the ROM's, not a divide: for an odd height, `104 - (H >> 1)` sits one pixel below
`104 - H / 2.0f`. `LayoutWalker` owns both the measuring pass and the placing pass, so a height
is literally the distance one walk covers and the two cannot drift apart.

Two consequences worth stating, because both invite a wrong shortcut:

- Inactive players contribute nothing to the height. Count drawn rows, not slots.
- The second and later Team headers pull back four pixels before they are placed, so a Team
  header's Y depends on how many players the preceding team block held. It cannot be a constant.

## What the port changes

The ROM draws text from a centre with no box, using a DS font whose glyph widths a Qt font does
not reproduce. Each native text anchor therefore gets a fixed cell (`kValueCellHalfW`,
`kNameCellHalfW`, `kTeamCellHalfW`). Fixed rather than content-measured on purpose: a value that
gains a digit keeps its cell, so the per-frame refresh stays on its cheap path instead of forcing
a full plan rebuild.

### Every cell centres on its anchor

Both axes, for text and images alike. Horizontally that puts centre-aligned text on the ROM's own
X. Vertically it is a port-side choice, and it is the one place the board leaves the ROM: the ROM
treats a text Y as the top of its own nine-pixel glyph line, so its text sits half a line lower
than this.

The reason to take that half a line is that nothing on a Custom HUD row is the size the ROM's own
artwork is. The portrait and the stars are sized by their own settings and the text by **Text
Scale %**, so anchoring an edge — any edge — lines a row up only when those sizes happen to match
the ROM's. Sharing a centre makes a row read as a row at any size, and raising Text Scale % grows
the text about its anchor rather than off one side of it.

The nickname keeps its own anchor nine pixels above the row, so it still clears the stars.

### Assets centre on their cells

The ROM anchors the portrait and the stars by their top-left corner, because it knows exactly how
big its own artwork is. Custom HUD does not: both assets are sized by their own settings, so
pinning a corner leaves a smaller asset visibly high and to the left of the row it belongs to.
Each therefore centres on the cell its ROM anchor implies:

- **Stars.** The ROM draws two pieces, at `60` and `92`, each one 32-pixel HUD object wide, so the
  pair spans 64 pixels and its midpoint is `92` — the nickname's own centre. One combined asset
  centres there and is capped at the pair's width, which keeps it clear of column 1.
- **Portrait.** The ROM hangs it thirteen pixels above the row, so the band is thirteen either
  side, and the board's HUD objects are 32 wide; the cell is that 32x26 box and its centre is
  (`36`, `rowY`). The ROM's true artwork size comes from asset metadata this port does not read,
  so the cell is the port's reading of the anchor, not a claim about the ROM. An asset the size of
  the cell lands exactly on the ROM's corner.

The auto layout centres the same two assets in their own cells for the same reason — the portrait
in the portrait column, the stars in whatever the rank line leaves them — so the two layouts agree
about everything except where the cells are.

Its text is centred too: the name, the two statistics values and all three column headers sit in
the middle of their own columns, as they do natively. The rank line keeps its left edge, because
the stars centre in what it leaves them and the two would otherwise land on each other.

Vertically the statistics keep the whole row and centre on it, which puts them between the two
lines of the name cell — that cell's own middle.

Because the background box is the ROM's own height and the header line now centres on its top
edge, the box grows upwards by half a text line so the header is not left hanging out of it. The
bottom needs no such allowance: the last player row keeps most of its 28 pixels below whatever it
draws.

Two elements the ROM has are absent:

- **No Player column header.** Natively there are only the two statistics headers.
- **No rank text.** The hunter stars *are* the ROM's rank indicator, and they occupy the position
  the Custom HUD's rank line would use. The **Rank** setting (default off) applies to the auto
  layout only.

One element the ROM has is not reachable: the **GAME OVER** row. The Custom HUD board is the live
in-match scoreboard and never samples the Ending state, so its height term is always absent. The
geometry module still implements it, and the tests still cover it, so wiring an ending flag later
is a caller change rather than a layout change.

## Which settings apply

| Setting | Native layout | Auto layout |
| --- | --- | --- |
| Show, colours, opacity, background opacity, outline | applies | applies |
| Hunter Icon Height, Stars Height | applies | applies |
| Text Scale % | applies | applies |
| Scoreboard Scale % | applies — scales the geometry about the board centre | applies |
| Offset X / Offset Y | applies — moves the whole board | applies |
| Anchor | applies — **Centre** means the ROM's own placement | applies |
| Column Spacing, Right Padding, Row Spacing | **ignored** — the ROM owns its gaps | applies |
| Rank | **ignored** — see above | applies |

The dialog says the same thing in place: the four auto-only settings are grouped at the end of
the **HUD SCOREBOARD** section behind an `AUTO LAYOUT ONLY` note row, and the in-game editor and
its side panel list them last for the same reason.

### Anchor

The ROM board is already centred — on Y = 104, and on its own columns rather than the screen's
middle — so **Centre** (`kRomAnchor`, anchor value 4) is the one anchor that has to mean *native
placement*, or the default could not be pixel-exact. The other eight anchors pull the board box
onto that screen point exactly like any other HUD element, so the native board can still be put
in a corner; only the board's internal geometry is fixed, never where the board sits.

`HudScoreboardAnchor` therefore defaults to `4` (was `3`, left-middle) and `HudScoreboardY` to
`0` (was `-20`). A saved configuration that still carries the old values places the board where
those values say — reset the element to get the ROM's own position back.

Scale grows the board about the screen centre line (X = 128) and the board's own centre
(Y = 104), so a scaled board stays centred where the unscaled one was. Asset and font sizes
already follow Scale % through their own paths, so the geometry transform carries positions and
cell widths only.

## Where it lives

| Concern | Owner |
| --- | --- |
| ROM constants, walk, height, centring | `MelonPrimeHudScoreboardNativeGeometry.h` |
| Cell widths, text cells, user transform, anchor, board box | same header, "Custom HUD adaptation" section |
| Row content (text, colour, values) | `MakeScoreboardRowContent()` in `MelonPrimeHudRenderDraw.inc` |
| Native placement | `PlaceScoreboardPlanNative()` |
| Auto placement | `PlaceScoreboardPlanAuto()` |
| Auto stars placement, shared with the per-frame refresh | `ScoreboardAutoStarsRect()` |
| Edit-mode selection box | `ComputeEditBounds()` in `MelonPrimeHudConfigOnScreenDraw.inc` |
| Settings-dialog preview | `ScoreboardPreviewWidget::paintNativeScoreboard()` |

The geometry header is free of Qt and melonDS so the layout can be exercised directly. The
runtime board, the in-game edit mode and the settings preview are three translation units, and
they share the header rather than each carrying their own copy of the constants.

Both layouts resolve a row's content identically and differ only in placement, which is why
content resolution is factored out of both placement passes.

## Performance

Placement runs in the plan build, which is cached and only re-runs on a structural change, a
config change, or a font/scale change — the toggle is covered by the config epoch, so switching
layouts invalidates the plan without a key of its own. The per-frame path is unchanged: it
refreshes cell values in place, and the native layout's fixed cell widths make the reject-and-
rebuild case rarer than the auto layout's content-sized columns.

## Tests

`tools/testing/hud-scoreboard-native-geometry-tests.cpp`, built and run by the
`melonprime_hud_scoreboard_native_geometry_check` target on every build. It checks the reference
document's worked examples row by row — 4-player FFA, 2-player FFA, 2v2, 1v3, and both ending
boards — plus the integer centring (every odd height must differ from float centring), the
independence of height from row order, the panel box and user transform, that every text cell is
centred on its ROM anchor on both axes at any line height, offset or scale, that a whole player
row shares one centre, that the nickname still clears the stars, that the portrait and the stars
centre on their cells at any asset size (a cell-sized asset landing exactly on the ROM's corner),
and that each anchor places the board box without disturbing its internal geometry.

The header additionally pins the height and start-Y examples as `static_assert`s, so a change to
a spacing constant fails the compile rather than the test run.
