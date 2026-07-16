# macOS Window Rounded Corners — Investigation Log

**Status:** OPEN — no product fix implemented. Confirmed macOS system window clipping, not a MelonPrimeDS rendering bug.
**First investigated:** 2026-07-02
**Branch:** `highres_fonts_v3` (reported on macOS build)

## Symptoms reported

- **mac版で小画面の時に角が丸くなる** — when the main emulator window is small (e.g. 1× native screen sizing), the **four outer corners of the window** look rounded.
- The curved areas show **black** (same as the letterbox / unused panel background), not game content.
- Windows and Linux builds do **not** show this behavior.
- **Not** the Custom HUD weapon-inventory **Highlight Corner Radius** setting (`Metroid.Visual.HudWeaponInventoryHighlightCornerRadius`). That is an in-game HUD outline drawn by QPainter and is configurable in Metroid settings.

## Visual model

```
┌─────────────────────────────┐  ← macOS clips the whole window to a rounded rect
│ ╭─────────────────────────╮   │
│ │  OpenGL / ScreenPanel   │   │  ← drawable area is rectangular
│ │  (glClear black, 256×192│   │
│ │   screens laid out)     │   │
│ ╰─────────────────────────╯   │
└─────────────────────────────┘
     ↑ black curved wedges at corners when window is small
```

The emulator draws a **rectangular** framebuffer. macOS applies **window-level rounded clipping** on top. At small window sizes the corner radius is roughly **fixed in points** (~10–16 pt depending on macOS version / display), so the arcs occupy a **larger fraction** of the visible area and become more noticeable.

## Code paths (MelonPrimeDS)

| Area | File | Notes |
|---|---|---|
| Main window shell | [Window.cpp](../../../../src/frontend/qt_sdl/Window.cpp) | `QMainWindow`; mac-specific code is menu Preferences role and center-on-screen only. **No** corner / NSWindow customization. |
| GL screen panel | [Screen.cpp](../../../../src/frontend/qt_sdl/Screen.cpp) | `ScreenPanelGL::drawScreen()` clears with `glClearColor(0,0,0,1)` and `glViewport(0,0,w,h)` — full rectangular viewport. |
| macOS GL context | [context_agl.mm](../../../../src/frontend/duckstation/gl/context_agl.mm) | Binds `NSOpenGLContext` to Qt's `NSView`; no window shape changes. |
| App startup | [main.cpp](../../../../src/frontend/qt_sdl/main.cpp) | `Qt::AA_NativeWindows` / `AA_DontCreateNativeWidgetSiblings` only; no Cocoa window styling. |

**Conclusion:** Nothing in the MelonPrimeDS frontend intentionally rounds the main window corners. The effect is **NSWindow / compositor behavior** on modern macOS (Big Sur+; more pronounced on recent releases).

## Ruled out

| Hypothesis | Why rejected |
|---|---|
| Custom HUD `HighlightCornerRadius` | User confirmed outer **window** corners, not weapon-inventory highlight. |
| `drawRoundedRect` in HUD preview / edit mode | Only affects HUD overlay widgets, not the main window chrome. |
| OpenGL viewport miscalculation at small sizes | Viewport is full widget size; corruption would not match symmetric corner arcs. |
| Qt `QMacStyle` drawing rounded popup chrome | Applies to popups/menus, not the persistent main `QMainWindow` content clip. |

## Why it feels worse at “小画面” (small window)

1. **Fixed corner radius in points** — window content shrinks; corner arc radius does not scale down proportionally.
2. **1× screen sizing** — minimum practical window size leaves little margin around the dual-screen layout, so letterboxing + corner clip overlap at the edges.
3. **Menu bar height** — vertical space for the menubar reduces the central widget area further at small sizes (see also the Windows 11 menubar padding note in `Window.cpp` for a related “small window” UX issue on another OS).

## User workarounds (no code change)

| Workaround | Effect |
|---|---|
| Enlarge the window slightly | Reduces relative corner area. |
| Full screen | Corners off-screen or less distracting. |
| Higher macOS display resolution (More Space) | Corner radius stays in points but appears smaller relative to content. |

There is **no supported macOS user setting** to disable standard window corner rounding system-wide (as of macOS 15 / Tahoe; third-party hacks exist but are out of scope for melonDS).

## Fix options (not implemented)

### Option A — Do nothing (current)

- **Pros:** Zero risk; matches other Qt/macOS apps; upstream-friendly.
- **Cons:** Small-window UX remains visually imperfect on macOS.

### Option B — macOS “square corners” via `NSWindow` style mask (WezTerm-style)

Reference: [wezterm `MACOS_FORCE_SQUARE_CORNERS`](https://github.com/wez/wezterm/issues/2182) uses a style mask **without** `NSTitledWindowMask`, combining `NSFullSizeContentViewWindowMask` with closable / miniaturizable / resizable flags, and draws its own title bar.

- **Pros:** Can achieve visually square corners with OpenGL / Metal content.
- **Cons for MelonPrimeDS:**
  - Qt owns `NSWindow` lifecycle and style mask; post-hoc mutation can fight `QCocoaWindow` on flag changes, resize, and fullscreen transitions.
  - `QMainWindow` + native `QMenuBar` expects a titled window; borderless / full-size-content modes need careful re-validation of menu integration, traffic-light buttons, and multi-window (`MP new instance`).
  - High maintenance across macOS major versions.

### Option C — `contentView.layer.cornerRadius = 0`

Setting the layer corner radius on the content view does **not** remove the **system window** rounded mask on standard titled windows. At best it affects subview clipping; reports are inconsistent and it can break OpenGL layer backing.

**Not recommended** without a dedicated macOS test matrix.

### Option D — Optional setting + dedicated `.mm` helper (future)

If implemented later:

1. Add config key e.g. `Window.MacSquareCorners` (default `false`).
2. Add `MacWindowAppearance.mm` with a single entry point:
   `void ApplyMacMainWindowAppearance(WId winId, bool squareCorners);`
3. Call from `MainWindow` after `show()` and on `QEvent::WinIdChange` if needed.
4. Gate behind `#ifdef __APPLE__`; link Cocoa in existing Apple block in [CMakeLists.txt](../../../../src/frontend/qt_sdl/CMakeLists.txt).
5. Document as **experimental** — Mission Control / tabbing / fullscreen round-trip must be smoke-tested.

**DoD if pursued:** S16-style visual check at 1× sizing on macOS 13+; no regression to GL context creation (`ScreenPanelGL::createContext`); second-instance window still works.

## Related but distinct: HUD “Corner Radius”

Weapon inventory highlight uses DS-space corner radius:

- Schema: [MelonPrimeHudPropSchema.inc](../../../../src/frontend/qt_sdl/MelonPrimeHudPropSchema.inc) — `HudWeaponInventoryHighlightCornerRadius` (0–16).
- Draw: [MelonPrimeHudRenderDraw.inc](../../../../src/frontend/qt_sdl/MelonPrimeHudRenderDraw.inc) — `p->drawRoundedRect(...)`.

Setting this to **0** removes **in-game HUD** rounded highlights only; it does **not** affect macOS window corners.

## References

- [WezTerm: disable rounded corners macOS (#2182)](https://github.com/wez/wezterm/issues/2182) — `MACOS_FORCE_SQUARE_CORNERS` design discussion.
- [Qt `qnswindow.mm` — background / frameless behavior](https://github.com/qt/qtbase/blob/dev/src/plugins/platforms/cocoa/qnswindow.mm) — Qt defers to system background for framed windows.
- [Stack Overflow: NSWindow default corner radius](https://stackoverflow.com/questions/79186429/how-can-i-get-the-default-standard-corner-radius-of-an-nswindow) — radius is system-defined, not a fixed constant exposed to apps.
- [OpenGL + Qt widget on macOS (layer backing)](https://stackoverflow.com/questions/71726409/opengl-cannot-draw-in-qtwidget-at-macos) — caution for NSView layer changes near GL.

## Progress tracking

| Date | Action | Result |
|---|---|---|
| 2026-07-02 | User report: rounded corners when window is small on macOS | Confirmed window-level clipping, not HUD setting |
| 2026-07-02 | Code review: `Window.cpp`, `Screen.cpp`, `context_agl.mm`, `main.cpp` | No existing mitigation; rectangular GL clear confirmed |
| 2026-07-02 | Investigation doc created | This file |

**Next step (if product owner requests a fix):** spike Option D on a real Mac with 1× window size; compare WezTerm style mask vs. accept-as-is.
