# MelonPrimeLocalization 多言語対応リファクタリング計画

作成日: 2026-07-04  
対象ブランチ: `highres_fonts_v3`  
対象領域: `src/frontend/qt_sdl/MelonPrimeLocalization.*`  
新規分割先候補: `src/frontend/qt_sdl/MelonPrimeLocalization/`

---

## 0. 目的

現在の多言語対応は、主要なクラッシュ・文字化け・保存値衝突の修正が入った状態であり、機能としてはかなり前進している。

一方で、現状は `MelonPrimeLocalization.cpp` に以下が密集している。

- 言語ID定義
- OSロケール検出
- 言語表示名
- fallback言語解決
- 翻訳テーブル
- objectNameベース翻訳
- `Tr()` のexact/prefix/suffix/dynamic処理
- widget tree localize
- menu/action localize
- dialog show-time localizer
- no-ROM splash localize
- no-ROM splash描画
- Arabic/Thai shaped text対応
- UTF-8 bounded copy

このままだと、対応言語追加・翻訳修正・UI追加のたびに巨大なC++ファイルを触ることになり、列ズレ・重複key・fallback漏れ・クラッシュ再発を起こしやすい。

本計画の目的は、既存挙動を維持したまま、翻訳基盤を以下の状態へ整理すること。

```txt
1. 言語メタデータを一箇所へ集約する
2. 翻訳データを実装ロジックから分離する
3. Tr() の責務を分ける
4. widget/menu/dialog/splash localizeを分離する
5. 翻訳漏れ・重複・保存値衝突を機械監査できるようにする
6. 将来的にJSON/TOML/Qt .ts/.qmへ移行できる構造にする
```

---

## 1. 現在の前提

現在のブランチでは、以下の修正は入っている前提とする。

```txt
- MenuLangId::First = 100
- -1 = System default
- legacy 0 = old native/system default
- legacy 1 = old English
- 新しい明示言語IDは100以上
- Arabic / Italian と legacy 0/1 の衝突は解消済み
- LanguageTagToMenuLang() は '-' を '_' に正規化済み
- ChineseTraditional は簡体字fallback扱いで、正式翻訳まで選択肢から除外済み
- no-ROM splash は UTF-8 bounded copy 対応済み
- Arabic / Thai は shaped text描画分岐あり
- ScreenPanel終了時クラッシュ対策として、destructorからunclip系処理を外し、beginClose側へ移動済み
```

これらは今後の不変条件として維持する。

---

## 2. 新ディレクトリ構成案

新規ディレクトリを作る。

```txt
src/frontend/qt_sdl/MelonPrimeLocalization/
```

最終的な構成案:

```txt
src/frontend/qt_sdl/
  MelonPrimeLocalization.h
  MelonPrimeLocalization.cpp

  MelonPrimeLocalization/
    MelonPrimeLanguageRegistry.h
    MelonPrimeLanguageRegistry.cpp

    MelonPrimeTranslationCatalog.h
    MelonPrimeTranslationCatalog.cpp

    MelonPrimeTranslationDynamic.h
    MelonPrimeTranslationDynamic.cpp

    MelonPrimeWidgetLocalizer.h
    MelonPrimeWidgetLocalizer.cpp

    MelonPrimeSplashLocalization.h
    MelonPrimeSplashLocalization.cpp

    MelonPrimeTranslations.inc
    MelonPrimeObjectTranslations.inc

    README.md
```

### 2.1 既存APIの扱い

既存呼び出し側の影響を小さくするため、外部APIは当面 `src/frontend/qt_sdl/MelonPrimeLocalization.h` に残す。

既存の呼び出し側:

```txt
Window.cpp
Screen.cpp
InputConfig/MelonPrimeInputConfig.cpp
InputConfig/MelonPrimeInputConfigCustomHudCode.inc
各種dialog open helper
```

これらは基本的に変更しない。

`MelonPrimeLocalization.cpp` は最終的に薄いfacadeにする。

```cpp
#include "MelonPrimeLocalization.h"
#include "MelonPrimeLocalization/MelonPrimeLanguageRegistry.h"
#include "MelonPrimeLocalization/MelonPrimeTranslationCatalog.h"
#include "MelonPrimeLocalization/MelonPrimeTranslationDynamic.h"
#include "MelonPrimeLocalization/MelonPrimeWidgetLocalizer.h"
#include "MelonPrimeLocalization/MelonPrimeSplashLocalization.h"
```

---

## 3. ファイルごとの責務

### 3.1 `MelonPrimeLocalization.h`

公開APIのみ残す。

残すもの:

```cpp
namespace MelonPrime::UiText
{
    enum class MenuLangId : int;

    int NormalizeMenuLanguageConfig(int storedValue);
    bool IsEnglishMenuLanguage(MenuLangId lang);

    MenuLangId DetectSystemMenuLanguage();
    bool IsMenuTranslationActive();
    MenuLangId ActiveMenuLanguage();

    bool IsJapaneseSystemLocale();
    bool IsJapaneseLocale();

    int MenuLanguageSelection();
    void SetMenuLanguageSelection(int configValue);

    QString MenuLanguageDisplayName(MenuLangId lang);
    QString MenuLanguageNativeLabel();
    QList<MenuLangId> AllSelectableMenuLanguages();

    QString TranslateExact(const QString& text);
    QString TranslateByObjectName(const QWidget* widget, const QString& text);
    QString Tr(const QString& text);
    QString TrWidgetText(const QWidget* widget, const QString& text);
    QString Tr(const char* text);
    QString Tr(const char* text, int size);
    QStringList TrList(const QStringList& items);

    void LocalizeWidgetTextProperties(QWidget* widget);
    void LocalizeWidgetTree(QWidget* root);
    void LocalizeMelonDsDialog(QWidget* dialog);
    void InstallMelonDsDialogShowLocalizer(QWidget* dialog);

    void LocalizeActionTextProperties(QAction* action);
    void SetLocalizedActionText(QAction* action, const QString& sourceText);
    void LocalizeAction(QAction* action);
    void LocalizeMenu(QMenu* menu);
    void LocalizeMenuBar(QMenuBar* menuBar);

    void ApplyNoRomSplashLocalization(char line0[256], char line1[256]);
    bool TryRenderNoRomSplashOsdItem(...);
    bool UsesLocalizedSplashLayout();
}
```

`enum class MenuLangId` は互換性のため当面ここに置いてよい。  
ただし言語メタデータ実体は `MelonPrimeLanguageRegistry.*` へ移す。

---

### 3.2 `MelonPrimeLanguageRegistry.h/.cpp`

言語そのものの情報を集約する。

担当:

```txt
- MenuLangId のmetadata
- stable code
- display name
- selectable
- fallback base
- RTL判定
- shaped text判定
- splash font group
- OS locale / env / AppleLanguages検出
- NormalizeMenuLanguageConfig
- ActiveMenuLanguage
```

導入する構造体案:

```cpp
namespace MelonPrime::UiText
{
    enum class TextDirection
    {
        LeftToRight,
        RightToLeft,
    };

    enum class SplashFontGroup
    {
        Latin,
        Japanese,
        ChineseSimplified,
        ChineseTraditional,
        Korean,
        Arabic,
        Thai,
    };

    struct LanguageInfo
    {
        MenuLangId id;
        const char* stableCode;
        const char* displayName;
        MenuLangId translationBase;
        bool selectable;
        TextDirection direction;
        bool requiresShapedSplash;
        SplashFontGroup splashFontGroup;
    };

    const LanguageInfo* FindLanguageInfo(MenuLangId id);
    const LanguageInfo& LanguageInfoOrEnglish(MenuLangId id);
    MenuLangId ResolveTranslationLanguage(MenuLangId lang);
    bool IsRightToLeftLanguage(MenuLangId lang);
    bool RequiresShapedSplashText(MenuLangId lang);
    SplashFontGroup SplashFontGroupForLanguage(MenuLangId lang);
}
```

`LanguageInfo` 例:

```cpp
static constexpr LanguageInfo kLanguageInfos[] = {
    {
        MenuLangId::Arabic,
        "ar",
        "العربية",
        MenuLangId::Arabic,
        true,
        TextDirection::RightToLeft,
        true,
        SplashFontGroup::Arabic,
    },
    {
        MenuLangId::ChineseTraditional,
        "zh-Hant",
        "中文（繁體，简体fallback）",
        MenuLangId::ChineseSimplified,
        false,
        TextDirection::LeftToRight,
        false,
        SplashFontGroup::ChineseTraditional,
    },
};
```

### DoD

```txt
- MenuLanguageDisplayName() は LanguageInfo 由来
- AllSelectableMenuLanguages() は LanguageInfo.selectable 由来
- ResolveTranslationLanguage() は LanguageInfo.translationBase 由来
- Arabic RTL判定は LanguageInfo 由来
- Thai shaped text判定は LanguageInfo 由来
- no-ROM splash font groupは LanguageInfo 由来
```

---

### 3.3 `MelonPrimeTranslationCatalog.h/.cpp`

exact translation と objectName translation のcatalogを担当する。

担当:

```txt
- kTranslations lookup
- kObjectTextTranslations lookup
- TranslateExact()
- TranslateByObjectName()
- duplicate key監査
- empty/null翻訳監査
- fallback処理
```

当面は既存の構造体をそのまま移してよい。

初期移動:

```txt
MelonPrimeLocalization.cpp
  Translation
  ObjectTextTranslation
  TranslationFieldForLang()
  ObjectTranslationFieldForLang()
  kTranslations
  kObjectTextTranslations
  TranslateExact()
  TranslateByObjectName()
```

を以下へ移す。

```txt
MelonPrimeLocalization/MelonPrimeTranslationCatalog.cpp
MelonPrimeLocalization/MelonPrimeTranslations.inc
MelonPrimeLocalization/MelonPrimeObjectTranslations.inc
```

### Phase A: まずは移動だけ

既存構造を変えずに、ファイルだけ分ける。

```cpp
struct Translation
{
    const char* en;
    const char* ja;
    ...
};
```

データは `.inc` に置く。

```cpp
// MelonPrimeTranslations.inc
static constexpr Translation kTranslations[] = {
    ...
};
```

### Phase B: 後でkey/value型へ移行

列順依存をなくすため、後続で以下へ変える。

```cpp
struct TranslationValue
{
    MenuLangId lang;
    const char* text;
};

struct TranslationEntry
{
    const char* key;
    std::initializer_list<TranslationValue> values;
};
```

例:

```cpp
{
    "Save",
    {
        {MenuLangId::Japanese, "保存"},
        {MenuLangId::German, "Speichern"},
        {MenuLangId::Spanish, "Guardar"},
    }
}
```

### DoD

```txt
- TranslateExact() の出力が移動前と一致
- TranslateByObjectName() の出力が移動前と一致
- kTranslations本体が MelonPrimeLocalization.cpp から消える
- 重複key検出の足場がある
```

---

### 3.4 `MelonPrimeTranslationDynamic.h/.cpp`

`Tr()` 内に散らばっている動的文字列処理を分離する。

対象:

```txt
- prefix/suffix付き文字列
- コロン付き文字列
- "Configuring settings for instance %1"
- "Configuring mappings for instance %1"
- "Configuring paths for instance %1"
- "Setting battery levels for instance %1"
- DS slot / GBA slot
- Top / Bottom
- Direct mode
- camera/device系
- Failed to load Custom HUD code: %1
```

関数案:

```cpp
namespace MelonPrime::UiText
{
    std::optional<QString> TranslateDecoratedText(const QString& text);
    std::optional<QString> TranslateDynamicText(const QString& text);
}
```

`Tr()` はこうする。

```cpp
QString Tr(const QString& text)
{
    if (!IsMenuTranslationActive() || text.isEmpty())
        return text;

    const QString exact = TranslateExact(text);
    if (exact != text)
        return exact;

    if (auto decorated = TranslateDecoratedText(text))
        return *decorated;

    if (auto dynamic = TranslateDynamicText(text))
        return *dynamic;

    return text;
}
```

### DoD

```txt
- Tr() 本体が短くなる
- dynamic特殊処理が MelonPrimeTranslationDynamic.cpp に集約
- 既存の翻訳結果は変えない
```

---

### 3.5 `MelonPrimeWidgetLocalizer.h/.cpp`

Widget / Action / Menu / Dialog localizeを担当する。

移動対象:

```txt
- SourcePropertyText
- SourcePropertyTextList
- SourceObjectPropertyText
- TrWidgetText
- LocalizeWidgetTextProperties
- LocalizeWidgetTree
- LocalizeMelonDsDialog
- InstallMelonDsDialogShowLocalizer
- LocalizeActionTextProperties
- SetLocalizedActionText
- LocalizeAction
- LocalizeMenu
- LocalizeMenuBar
- melonDS dialog dynamic label wiring
- LAN dialog special label wiring
```

注意:

- `QObject` / `QWidget` の寿命に注意する
- dialog show-time localizerは `QPointer<QWidget>` を使うのが安全
- 翻訳元テキストはpropertyへ保存し、二重翻訳を避ける

### DoD

```txt
- Menu / QAction / QWidget のlocalize処理が専用ファイルへ移動
- Dialog show localizer がQPointer安全
- Input Config の言語切替で既存表示が壊れない
```

---

### 3.6 `MelonPrimeSplashLocalization.h/.cpp`

No-ROM splash専用。

移動対象:

```txt
- ApplyNoRomSplashLocalization
- UTF-8 bounded copy helper
- UsesLocalizedSplashLayout
- TryRenderNoRomSplashOsdItem
- SplashOsdRainbowColor
- NoRomSplashUiFont
- Arabic/Thai shaped text分岐
```

このファイルは `Screen.cpp` から呼ばれるので、依存を軽く保つ。

### DoD

```txt
- Screen.cpp側APIは変えない
- char[256] へのUTF-8境界維持を継続
- Arabic/Thai shaped描画を維持
- 日本語/韓国語/中国語/ロシア語で文字化けしない
```

---

## 4. CMake更新

新規 `.cpp` を追加するので、`src/frontend/qt_sdl/CMakeLists.txt` または該当source listへ追加する。

追加候補:

```txt
MelonPrimeLocalization/MelonPrimeLanguageRegistry.cpp
MelonPrimeLocalization/MelonPrimeTranslationCatalog.cpp
MelonPrimeLocalization/MelonPrimeTranslationDynamic.cpp
MelonPrimeLocalization/MelonPrimeWidgetLocalizer.cpp
MelonPrimeLocalization/MelonPrimeSplashLocalization.cpp
```

`.inc` はCMakeに追加しない。

```txt
MelonPrimeLocalization/MelonPrimeTranslations.inc
MelonPrimeLocalization/MelonPrimeObjectTranslations.inc
```

は `#include` で取り込むだけ。

---

## 5. 実装フェーズ

## 5.1 進捗ログ

| Phase | 状態 | 日付 | 検証 | メモ |
|---|---|---:|---|---|
| Phase 0 | 完了 | 2026-07-04 | `python3 .claude/skills/audit-melonprime-localization.py`; `cmake --build build-mac --parallel 4` | 監査スクリプト追加。既存の完全重複 exact keys を削除し、代表キー `OK` / `Input Config` を追加。 |
| Phase 1 | 完了 | 2026-07-04 | `python3 .claude/skills/audit-melonprime-localization.py`; `cmake --build build-mac --parallel 4`; no-ROM起動→quit | `MelonPrimeLocalization/` へ LanguageRegistry / TranslationCatalog / Dynamic / WidgetLocalizer / Splash を物理分割。外部APIと翻訳データ形式は維持。 |
| Phase 2 | 未着手 | — | — | LanguageInfo metadata |
| Phase 3 | 未着手 | — | — | Tr responsibility split |
| Phase 4 | 未着手 | — | — | catalog Map化 |
| Phase 5 | 未着手 | — | — | key/value翻訳形式 |
| Phase 6 | 未着手 | — | — | coverage report |
| Phase 7 | 未着手 | — | — | external translation file evaluation |

## Phase 0: 監査テスト/スクリプト追加

### 目的

リファクタリング前に現在の安全条件を固定する。

### 作業

`.claude/skills/audit-melonprime-localization.py` などを追加する。

検査内容:

```txt
1. MenuLangId::First が100以上
2. kMenuLanguageSystemDefault == -1
3. legacy 0/1 migration が維持されている
4. AllSelectableMenuLanguages に ChineseTraditional が含まれない
5. Arabic / Italian が selectable で保存値100以上
6. kTranslations の英語key重複なし
7. kObjectTextTranslations の objectName重複なし
8. 空文字/null翻訳を検出
9. 各selectable言語で代表キーがfallback込みで非空
10. 翻訳列数ズレを検出
```

代表キー:

```txt
Save
Cancel
OK
ON
OFF
File
Config
Input Config
File->Open ROM...
to get started
```

### DoD

```txt
python3 .claude/skills/audit-melonprime-localization.py が通る
```

---

## Phase 1: ファイル分割のみ

### 目的

挙動を変えずに物理分割する。

### 作業

```txt
1. MelonPrimeLocalization/ ディレクトリ作成
2. LanguageRegistry系を移動
3. TranslationCatalog系を移動
4. Dynamic translationを移動
5. Widget localizerを移動
6. Splash localizationを移動
7. MelonPrimeLocalization.cpp をfacade化
8. CMake更新
```

### 禁止

```txt
- 翻訳データ形式を変えない
- MenuLangId値を変えない
- Tr()の結果を変えない
- widget localizeのproperty名を変えない
```

### DoD

```txt
macOS build green
Windows build green
Linux build green
audit script green
日本語メニュー文字化けなし
```

---

## Phase 2: LanguageInfo導入

### 目的

言語メタデータの重複をなくす。

### 作業

`MelonPrimeLanguageRegistry` に `LanguageInfo` を導入する。

置き換える処理:

```txt
MenuLanguageDisplayName()
AllSelectableMenuLanguages()
ResolveTranslationLanguage()
IsEnglishMenuLanguage()
RequiresShapedSplashText()
SplashFontGroupForLanguage()
IsRightToLeftLanguage()
```

### DoD

```txt
言語を1つ追加する時、LanguageInfoに1行追加すれば最低限のmetadataが揃う
ChineseTraditionalの選択不可/fallback理由がmetadataで表現される
Arabic RTL判定がmetadataで表現される
```

---

## Phase 3: Tr()責務分離

### 目的

`Tr()` を巨大な条件分岐から薄いdispatcherにする。

### 作業

```txt
TrExact
TrDecorated
TrDynamic
TrObjectName
Tr
```

に分離。

### DoD

```txt
Tr() が短い
prefix/suffix/colon処理が専用関数
instance/slot/dialog系文言が専用関数
翻訳結果はPhase 2から不変
```

---

## Phase 4: 翻訳catalog Map化

### 目的

線形検索・重複key・空翻訳を改善する。

### 作業

```cpp
class TranslationCatalog
{
public:
    static const TranslationCatalog& Instance();

    QString translateExact(const QString& source, MenuLangId lang) const;
    QString translateObjectText(const QString& objectName, MenuLangId lang) const;

private:
    QHash<QString, TranslationRecord> m_exact;
    QHash<QString, ObjectTextRecord> m_object;
};
```

初回構築時に検査:

```txt
- duplicate exact key
- duplicate objectName
- null key
- empty English key
```

developer buildではwarningまたはassertを出す。

### DoD

```txt
TranslateExact() から kTranslations for-loop が消える
TranslateByObjectName() から kObjectTextTranslations for-loop が消える
重複keyを検出できる
```

---

## Phase 5: 翻訳データ形式をkey/value型へ移行

### 目的

列順依存をなくす。

### 現状の問題

```cpp
{"Save", "保存", "Speichern", "Guardar", ...}
```

これは列ズレに弱い。

### 移行後

```cpp
{
    "Save",
    {
        {MenuLangId::Japanese, "保存"},
        {MenuLangId::German, "Speichern"},
        {MenuLangId::Spanish, "Guardar"},
    }
}
```

### 注意

差分が巨大になるため、以下の順でやる。

```txt
1. 小さいサンプル範囲だけkey/value化
2. 旧形式と新形式を同時に読めるadapterを作る
3. 代表キーで一致確認
4. 全体移行
5. 旧形式削除
```

### DoD

```txt
列順依存が消える
新言語追加時に全翻訳行へ空列を足す必要がない
coverage scriptで未翻訳数が出る
```

---

## Phase 6: Coverage report

### 目的

「対応言語」と「翻訳完了率」を分ける。

### 出力例

```txt
Exact translation keys: 420
Object translation keys: 180

ja: exact 420/420, object 180/180
de: exact 410/420, object 175/180
ar: exact 350/420, object 120/180
th: exact 348/420, object 118/180

Dynamic text:
  instance labels: ja/de/es/fr/it/nl/pt/ru/zh/ko only
  slot labels: ja/de/es/fr/it/nl/pt/ru/zh/ko only
  LAN warnings: ja/de/es/fr/it/nl/pt/ru/zh/ko only
```

### DoD

```txt
PRごとに翻訳漏れを確認できる
対応言語一覧と実装coverageがズレない
fallback言語が明示される
```

---

## Phase 7: 外部翻訳ファイル化の検討

すぐにはやらない。  
C++分割と監査が安定してから検討する。

候補:

```txt
A. JSON/TOML/YAMLをビルド時にC++ inc生成
B. Qt Linguist .ts/.qm
C. 現行C++ inc維持
```

現時点では A が最も現実的。

理由:

```txt
- melonDS upstream UIを直接tr()化していない
- objectNameベース翻訳がある
- 動的生成widgetが多い
- 既存のTr() hook方式を維持したい
```

---

## 6. 手動スモーク

各Phase後に最低限これを確認する。

### A. 起動/終了

```txt
1. macOSで起動
2. no-ROM splash表示
3. 何もせず終了
4. 終了時クラッシュなし
```

### B. 言語切替

以下を順に選択。

```txt
System default
English
Japanese
Arabic
Thai
Korean
Russian
Portuguese Brazil
French Canada
Spanish LatAm
```

確認:

```txt
- メニュー文字化けなし
- Input Config文字化けなし
- no-ROM splash文字化けなし
- Arabic/Thaiでsplash描画が崩壊しない
```

### C. 保存/再起動

```txt
1. Arabicを選択
2. OKで保存
3. 再起動
4. Arabicのまま
5. Italianを選択
6. OKで保存
7. 再起動
8. Italianのまま
```

旧0/1衝突の再発検査。

### D. Input Config

```txt
1. Input Configを開く
2. 言語変更
3. Cancel
4. 再度開く
5. OK
6. 終了
```

確認:

```txt
- Cancelでクラッシュしない
- OKで保存される
- 終了時クラッシュしない
```

### E. ROMあり

```txt
1. ROM起動
2. GCMouse aim使用
3. Stop
4. 終了
```

確認:

```txt
- ScreenPanel destructor crashなし
- cursor capture解除が残る
```

---

## 7. 自動監査コマンド案

```bash
python3 .claude/skills/audit-melonprime-localization.py
```

将来の出力例:

```txt
[PASS] MenuLangId::First >= 100
[PASS] System default = -1
[PASS] Legacy 0/1 migration
[PASS] Arabic selectable persisted value = 100
[PASS] Italian selectable persisted value = 101
[PASS] ChineseTraditional not selectable
[PASS] Exact keys duplicate check
[PASS] Object keys duplicate check
[WARN] Arabic object coverage 72%
[WARN] Thai object coverage 70%
[PASS] UTF-8 source scan
```

---

## 8. 変更禁止領域

このリファクタリングでは以下を触らない。

```txt
- RunFrameHook
- MelonPrimeCore input hot path
- WM_INPUT / RawInput
- GCMouse aim delta path
- perf probe
- ROM patch address tables
- Custom HUD rendering algorithm
- Stage Select
- MorphBall Boost logic
```

翻訳基盤の整理だけに集中する。

---

## 9. 推奨実施順

```txt
1. Phase 0: audit script
2. Phase 1: physical file split
3. Phase 2: LanguageInfo metadata
4. Phase 3: Tr() responsibility split
5. Phase 4: catalog Map化
6. Phase 5: key/value翻訳形式
7. Phase 6: coverage report
8. Phase 7: 外部翻訳ファイル化検討
```

一番安全なのは、**先にファイル分割だけを行い、挙動を変えないこと**。  
翻訳データ形式の変更は差分が大きくなるため、分割・監査・metadata集約が済んでから行う。

---

## 10. Cursor / Claude 投げ込み用プロンプト

```txt
現在の highres_fonts_v3 ブランチで、MelonPrimeLocalization の多言語対応を段階的にリファクタリングしてください。

新規ディレクトリ:
src/frontend/qt_sdl/MelonPrimeLocalization/

目的:
MelonPrimeLocalization.cpp に密集している言語検出・翻訳catalog・dynamic翻訳・widget/menu/dialog localize・no-ROM splash処理を分割し、監査可能で壊れにくい構造にしてください。

重要な前提:
- MenuLangId::First=100 を維持
- -1 = System default を維持
- legacy 0/1 migration を維持
- ChineseTraditional は正式翻訳列を持つまで選択肢に出さない
- Arabic/Thai shaped splash描画を維持
- no-ROM splash UTF-8 bounded copyを維持
- ScreenPanel終了時クラッシュ対策を壊さない
- 外部API MelonPrime::UiText::* は基本維持
- RunFrameHook / input hot path / perf probe には触らない

Phase 0:
.claude/skills/audit-melonprime-localization.py を追加し、MenuLangId保存値、legacy migration、ChineseTraditional非選択、翻訳key重複、空翻訳、代表キーfallbackを監査してください。

Phase 1:
挙動を変えずにファイル分割してください。
候補:
- MelonPrimeLocalization/MelonPrimeLanguageRegistry.h/.cpp
- MelonPrimeLocalization/MelonPrimeTranslationCatalog.h/.cpp
- MelonPrimeLocalization/MelonPrimeTranslationDynamic.h/.cpp
- MelonPrimeLocalization/MelonPrimeWidgetLocalizer.h/.cpp
- MelonPrimeLocalization/MelonPrimeSplashLocalization.h/.cpp
- MelonPrimeLocalization/MelonPrimeTranslations.inc
- MelonPrimeLocalization/MelonPrimeObjectTranslations.inc

Phase 2:
LanguageInfoテーブルを導入し、displayName/selectable/fallback/RTL/shapedSplash/splashFontGroupを一元管理してください。

Phase 3:
Tr()を TrExact / TrDecorated / TrDynamic / TrObjectName / Tr dispatcher に分離してください。

Phase 4:
TranslationCatalogをQHash/Map化し、重複key検出を可能にしてください。

Phase 5:
翻訳データを列順依存からkey/value依存へ段階移行してください。
未翻訳言語は英語fallbackでOKですが、coverageに出してください。

DoD:
- macOS/Windows/Linux build green
- audit script green
- 起動直後クラッシュなし
- no-ROM splash文字化けなし
- Input Config開閉/Cancel/OKでクラッシュなし
- Arabic/Italianが選択・保存・再起動後も維持される
- ChineseTraditionalが選択肢に出ない
- 言語切替後にmenu/Input Config/no-ROM splashが更新される
- 終了時クラッシュなし
```

---

## 11. 最終状態イメージ

最終的に `MelonPrimeLocalization.cpp` は巨大な翻訳ファイルではなく、以下のような薄い入口になる。

```cpp
#include "MelonPrimeLocalization.h"

#include "MelonPrimeLocalization/MelonPrimeLanguageRegistry.h"
#include "MelonPrimeLocalization/MelonPrimeTranslationCatalog.h"
#include "MelonPrimeLocalization/MelonPrimeTranslationDynamic.h"
#include "MelonPrimeLocalization/MelonPrimeWidgetLocalizer.h"
#include "MelonPrimeLocalization/MelonPrimeSplashLocalization.h"

namespace MelonPrime::UiText
{
    QString Tr(const QString& text)
    {
        if (!IsMenuTranslationActive() || text.isEmpty())
            return text;

        if (auto exact = TryTranslateExact(text))
            return *exact;

        if (auto decorated = TryTranslateDecorated(text))
            return *decorated;

        if (auto dynamic = TryTranslateDynamic(text))
            return *dynamic;

        return text;
    }
}
```

翻訳データは `MelonPrimeTranslations.inc` と `MelonPrimeObjectTranslations.inc` に隔離する。

これにより、翻訳追加・表示名追加・fallback変更・splash描画修正が、それぞれ別の責務として安全に変更できるようになる。
