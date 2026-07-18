# Qt Menu Actions

## Scope

Use this when adding a menu item to the Qt frontend, especially a MelonPrime-specific toggle under the `Metroid` menu.

Primary files:

| File | Role |
|------|------|
| `src/frontend/qt_sdl/Window.cpp` | Creates menu actions, connects signals, initializes checked state, implements slots |
| `src/frontend/qt_sdl/Window.h` | Declares slot methods and stores `QAction*` members |
| `src/frontend/qt_sdl/Config.cpp` | Owns default values for config keys |
| `src/frontend/qt_sdl/InputConfig/*` | Optional settings-dialog mirror of the same config key |
| `src/frontend/qt_sdl/MelonPrimeLocalization.h` | localization API (`Tr`, `SetLocalizedActionText`, `LocalizeMenu`, ...) |
| `src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslationCatalog.cpp` | translation lookup implementation and catalog ownership |

## Localization

MelonPrime menu labels are localized through the `MelonPrime::UiText` API in `MelonPrimeLocalization.h`; the translation manifests and topic shards live in `MelonPrimeLocalization/inc/`.

- Add every user-facing `QMenu`/`QAction` label to the appropriate `MelonPrimeTranslations*.inc` topic shard included by `MelonPrimeTranslations.inc` when adding or renaming a menu item. See [Add a Menu Language](../localization/add-menu-language.md) for the full translation-content workflow and pack-verification rules.
- `MainWindow::localizeMenuText()` applies the saved `Metroid.UI.MenuLanguage` to the menubar.
- For labels that change after construction, use `MelonPrime::UiText::SetLocalizedActionText(action, englishSourceText)` instead of raw `setText()` so the English source text stays available when switching between Japanese and English.
- Dynamically rebuilt menus, such as `recentMenu`, should call `MelonPrime::UiText::LocalizeMenu(menu)` after repopulating actions.
- The menu language selector is always available (not gated to any OS locale) and currently lists 82 selectable languages plus "System default" — see `AllSelectableMenuLanguages()` in `MelonPrimeLanguageRegistry.cpp`.

### macOS: always set `QAction::NoRole` on new top-level menu actions

Every new `QAction` defaults to `QAction::TextHeuristicRole`. On macOS, Qt scans the action's
**current** text (i.e. whatever it is *after* localization runs) for substrings like
`"config"`/`"setup"`/`"prefer"`/`"option"` (case-insensitive) and silently reassigns matching
items to the single native Preferences menu slot, removing them from their real menu position.

This does not depend on the English source text at all — it depends on whatever the *active
menu language* translated it to. `"Video settings"` never matches in English, but its Portuguese
translation `"Configurações de vídeo"` does (it contains the substring `"config"`), so an item
that renders fine in English or Japanese can vanish the moment the user switches to Portuguese
(or any future language whose translation happens to contain one of those substrings). This bit
almost the entire Config menu and the first three MelonPrime menu items in Portuguese before
being fixed (2026-07) — see `git log --oneline -- src/frontend/qt_sdl/Window.cpp` around that
date for the fix.

**Add `action->setMenuRole(QAction::NoRole);` right after every `menu->addAction(...)` call for a
new Config-menu or MelonPrime-menu item**, unless the action is genuinely meant to route to a
native macOS role (the real "Preferences..." action already sets `PreferencesRole` explicitly;
"About..." intentionally relies on the heuristic matching its English text so macOS moves it into
the app menu — do not add `NoRole` to that one). When in doubt, add `NoRole` and verify with a
menu-language smoke test rather than leaving it to the heuristic.

## Pattern: Checkable MelonPrime Toggle

Use `Disable SF (Shadow Freeze)` as the reference implementation.

### 1. Add a slot in `Window.h`

Inside the `#ifdef MELONPRIME_DS` slots block:

```cpp
void onChangeMetroidFoo(bool checked);
```

### 2. Add an action member in `Window.h`

Inside the `#ifdef MELONPRIME_DS` action block:

```cpp
QAction* actMetroidFoo;
```

### 3. Create the action in `Window.cpp`

In the `Metroid` menu construction block:

```cpp
actMetroidFoo = menu->addAction("Menu Label");
actMetroidFoo->setCheckable(true);
connect(actMetroidFoo, &QAction::triggered, this, &MainWindow::onChangeMetroidFoo);
```

If the action mirrors a settings-dialog checkbox, use exactly the same config key in both places.

### 4. Initialize checked state

In the constructor section where other actions are initialized:

```cpp
actMetroidFoo->setChecked(localCfg.GetBool("Metroid.Some.Key"));
```

### 5. Implement the slot

Near the existing MelonPrime menu slots:

```cpp
void MainWindow::onChangeMetroidFoo(bool checked)
{
    localCfg.SetBool("Metroid.Some.Key", checked);
}
```

If the runtime patch has a dirty/config notification hook, call it here too. Example:

```cpp
MelonPrime::ShadowFreezeRuntimeHook_NotifyConfigChanged();
```

### 6. Resync after settings dialog closes

In `MainWindow::onInputConfigFinished()`, refresh the menu check state from config:

```cpp
actMetroidFoo->setChecked(localCfg.GetBool("Metroid.Some.Key"));
```

This keeps the menu correct when the same option is changed through the settings dialog.

## Persistence Notes

`localCfg.SetBool()` updates the instance config table immediately. If the same option is exposed in `MelonPrimeInputConfig`, normal dialog accept/save flow persists it through `saveConfig()`.

For a menu-only option, check the surrounding frontend convention before adding `Config::Save()`. Existing MelonPrime menu toggles currently mirror live config state and rely on the normal config lifecycle.

## Checklist

- [ ] `Window.h`: slot declared
- [ ] `Window.h`: `QAction*` member declared
- [ ] `Window.cpp`: action created under the intended menu
- [ ] `Window.cpp`: action is checkable
- [ ] `Window.cpp`: signal connected to slot
- [ ] `Window.cpp`: checked state initialized from config
- [ ] `Window.cpp`: slot writes the same config key used by settings UI/runtime
- [ ] `Window.cpp`: `onInputConfigFinished()` resyncs checked state when there is a settings-dialog mirror
- [ ] `Config.cpp`: bool default exists in `DefaultBools` if `GetBool()` is used
- [ ] Build after changing `Window.h`; automoc may rebuild many files
