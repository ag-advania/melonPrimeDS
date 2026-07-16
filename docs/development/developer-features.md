# MelonPrime Developer Only Section

## 概要

Settings タブの末尾にある折りたたみ式セクション。開発中・未公開の実験的機能を格納する。

- トグルボタン: `btnToggleDeveloperOnly`
- セクション Widget: `sectionDeveloperOnly` (layout: `QVBoxLayout` named `vboxDeveloperOnly`)
- 開閉状態保存キー: `Metroid.UI.SectionDeveloperOnly`

---

## ビルドフラグ

```cmake
# CMakeLists.txt (src/frontend/qt_sdl/)
option(MELONPRIME_ENABLE_DEVELOPER_FEATURES "..." OFF)
# → target_compile_definitions(melonDS PRIVATE MELONPRIME_ENABLE_DEVELOPER_FEATURES)
```

`MelonPrimeInputConfig.cpp` 冒頭で compile-time 定数として展開される:

```cpp
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    constexpr bool kDeveloperOnlyFeaturesEnabled = true;
#else
    constexpr bool kDeveloperOnlyFeaturesEnabled = false;
#endif
```

`kDeveloperOnlyFeaturesEnabled = false` のとき、`setupCollapsibleSections` 内で:

```cpp
ui->btnToggleDeveloperOnly->setVisible(false);
ui->sectionDeveloperOnly->setVisible(false);
```

セクション全体が非表示になる。子 Widget は親と共に非表示になるため、個別に hide する必要はない。

---

## セクション内のウィジェット

### 静的ウィジェット（`.ui` ファイルで定義）

`.ui` ファイルの `vboxDeveloperOnly` に直接記述する。非有効ビルド向けに `enabled="false"` を設定しておく:

```xml
<widget class="QCheckBox" name="cbMetroidFoo">
    <property name="enabled"><bool>false</bool></property>
    <property name="text"><string>Foo feature</string></property>
</widget>
```

`MelonPrimeInputConfig.cpp` の `setupSensitivityAndToggles` で load 時にも `kDeveloperOnlyFeaturesEnabled` で制御:

```cpp
ui->cbMetroidFoo->setChecked(kDeveloperOnlyFeaturesEnabled && instcfg.GetBool("Metroid.Developer.Foo"));
```

現在の静的ウィジェット:
- `cbMetroidEnableNativeAimRegisterInjection` / `lblMetroidNativeAimRegisterInjectionDesc`
- `cbMetroidEnableImmediateInputEdgeOverlay` / `lblMetroidImmediateInputEdgeOverlayDesc`
- `cbMetroidFixNoxusBladePersistence` (BUG FIXES セクションから `setParent` で移動)

### 動的ウィジェット（`setupInputMethodSection` で追加）

`setupInputMethodSection` 内で `addDeveloperWidget` / `addDeveloperSpacing` ヘルパーを使って `vboxDeveloperOnly` に挿入する。挿入位置は `cbMetroidEnableNativeAimRegisterInjection` の直前:

```cpp
auto* developerLayout = ui->vboxDeveloperOnly;
int developerInsertIndex = developerLayout->indexOf(ui->cbMetroidEnableNativeAimRegisterInjection);
// ...
auto addDeveloperSpacing = [&]() { developerLayout->insertSpacing(developerInsertIndex++, 6); };
auto addDeveloperWidget  = [&](QWidget* w) { developerLayout->insertWidget(developerInsertIndex++, w); };
```

現在の動的ウィジェット (挿入順):
1. `cbMetroidEnableNativeAimPostFoldWrite` / `lblMetroidNativeAimHookModeDesc` (`.ui` から `setParent` で移動)
2. `m_cbMetroidUseNewBipedFireMethod` / `fireDesc`
3. `m_cbMetroidUseNewZoomMethod` / `m_cbMetroidUseNewZoomMethod2` / `zoomDesc` / `zoom2Desc`

---

## 新しい Developer Only ウィジェットを追加するパターン

### パターン A: 静的（`.ui` ファイルに定義する場合）

1. `.ui` の `vboxDeveloperOnly` レイアウトにウィジェットを追加。`enabled="false"` を設定。
2. `setupSensitivityAndToggles` に load 処理:
   ```cpp
   ui->cbMetroidFoo->setChecked(kDeveloperOnlyFeaturesEnabled && instcfg.GetBool("..."));
   ```
3. `saveConfig` に save 処理:
   ```cpp
   instcfg.SetBool("...", kDeveloperOnlyFeaturesEnabled && ui->cbMetroidFoo->isChecked());
   ```
4. `Config.cpp` の `DefaultBools` / `DefaultInts` にデフォルト値を追加。

### パターン B: 動的（`setupInputMethodSection` で追加する場合）

`m_sectionInputMethod` への追加が必要な機能と連動する場合に使う。

```cpp
auto* cb = new QCheckBox("Foo feature", ui->sectionDeveloperOnly);
cb->setChecked(kDeveloperOnlyFeaturesEnabled && instcfg.GetBool("..."));
cb->setEnabled(kDeveloperOnlyFeaturesEnabled);
addDeveloperSpacing();
addDeveloperWidget(cb);
```

---

## チェックボックス有効化パターン（必須）

Developer Only ウィジェットは以下の2点を **必ず** 守る:

```cpp
// setChecked: 非有効ビルドでは常に false
widget->setChecked(kDeveloperOnlyFeaturesEnabled && <条件>);

// setEnabled: 非有効ビルドでは操作不可
widget->setEnabled(kDeveloperOnlyFeaturesEnabled);
```

`setEnabled` を省略すると、非有効ビルドで UI 上は操作できないが、`setupSensitivityAndToggles` 内でセクションが非表示になる前に `setChecked` が呼ばれるタイミング次第でチェック状態が残る可能性がある。

---

## セクション表示に依存しない save/load の注意

`sectionDeveloperOnly` が非表示でも、`saveConfig` は常に呼ばれる。
save 側でも `kDeveloperOnlyFeaturesEnabled` を見て誤書き込みを防ぐ:

```cpp
// 安全パターン
instcfg.SetBool("Metroid.Developer.Foo",
    kDeveloperOnlyFeaturesEnabled && ui->cbMetroidFoo->isChecked());
```

または、ウィジェットの `isChecked()` が `false` を返すことに依存しても良い（`setChecked(kDeveloperOnlyFeaturesEnabled && ...)` で保証されるため）。既存の実装は後者を採用しているものが多い。

---

## Developer Only セクションに移動すべきケース

- 実装が完成しているが、広く配布する前にさらなる検証が必要な機能
- 特定の ROM バージョンや環境でのみ動作を確認した機能
- 通常のユーザーが誤って使うと問題が起きる可能性のある機能

`MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON` でビルドした場合のみ表示され、OFFビルド（配布版）では完全に非表示になる。

---

## CMakeLists.txt でのビルド指定（build.md 参照）

開発ビルドでは `-DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON` を渡す（バッチファイルがデフォルトで付与）。
配布ビルドでは明示的に `OFF` を指定する。
