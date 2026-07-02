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
| `src/frontend/qt_sdl/MelonPrimeLocalization.cpp` | translation tables (`kTranslations`, `kObjectTextTranslations`) |

## Localization

MelonPrime menu labels are localized through the `MelonPrime::UiText` API in `MelonPrimeLocalization.h`; the translation tables live in `MelonPrimeLocalization.cpp`.

- Add every user-facing `QMenu`/`QAction` label to `kTranslations` (in `MelonPrimeLocalization.cpp`) when adding or renaming a menu item.
- `MainWindow::localizeMenuText()` applies the saved `Metroid.UI.MenuLanguage` to the menubar.
- For labels that change after construction, use `MelonPrime::UiText::SetLocalizedActionText(action, englishSourceText)` instead of raw `setText()` so the English source text stays available when switching between Japanese and English.
- Dynamically rebuilt menus, such as `recentMenu`, should call `MelonPrime::UiText::LocalizeMenu(menu)` after repopulating actions.
- The menu language selector appears only on Japanese OS locales; non-Japanese OS locales stay English-only.

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
