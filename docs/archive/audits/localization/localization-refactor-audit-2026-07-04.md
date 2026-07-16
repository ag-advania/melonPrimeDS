# MelonPrimeLocalization リファクタリング監査レポート

作成日: 2026-07-04  
対象ブランチ: `highres_fonts_v3`  
対象領域: `src/frontend/qt_sdl/MelonPrimeLocalization.*` / `src/frontend/qt_sdl/MelonPrimeLocalization/`  
目的: 分割後の多言語対応実装監査、および中国語繁体字対応に向けた追加計画

---

## 1. 総評

今回のリファクタリングは、**かなり良い方向に進んでいる**。

特に以下は完了扱いでよい。

```txt
- MelonPrimeLocalization/ ディレクトリ分割
- CMakeへの分割cpp登録
- MelonPrimeLocalization.cpp のfacade化
- LanguageInfo導入
- MenuLangId::First=100 による旧0/1設定値衝突回避
- LanguageInfo由来の displayName / selectable / fallback / RTL / splash font group
- TranslationCatalog のQHash化
- TranslationValue key/value形式への移行
- Tr() の責務分離
- Widget/Menu/Dialog localizer分離
- No-ROM splash localizer分離
- UTF-8 bounded copy
- Arabic / Thai shaped splash描画分岐
- 監査スクリプト追加
```

現在の主な残課題は、**中国語繁体字を正式対応させるためのfallback設計**と、**監査スクリプトの固定列数前提**。

特に重要なのは次。

```txt
今の TranslationFieldForLang() は ResolveTranslationLanguage(lang) 後の baseLang だけを見ている。
そのため、ChineseTraditional の translationBase が ChineseSimplified のままだと、
仮に {MenuLangId::ChineseTraditional, "..."} を追加しても拾われない。
```

繁体字を段階的に対応したいなら、**actual lang → fallback/base lang → English** の順で探索する必要がある。

---

## 2. 監査結果一覧

| 項目 | 判定 | 内容 |
|---|---:|---|
| ディレクトリ分割 | PASS | `MelonPrimeLocalization/` 配下に責務別cpp/hが分割済み |
| CMake登録 | PASS | 5つの分割cppが `SOURCES_QT_SDL` に登録済み |
| facade化 | PASS | `MelonPrimeLocalization.cpp` は include と空namespaceのみ |
| LanguageInfo | PASS | `id/stableCode/displayName/translationBase/selectable/direction/requiresShapedSplash/splashFontGroup` が導入済み |
| 保存値互換 | PASS | `MenuLangId::First=100`, `-1=System default`, legacy `0/1` migration 維持 |
| QHash catalog | PASS | exact/object translation lookupがQHash化済み |
| key/value翻訳 | PASS | `TranslationValue { MenuLangId, text }` 形式へ移行済み |
| Tr責務分離 | PASS | exact/decorated/dynamic のdispatcher化済み |
| Widget localizer | PASS | widget/menu/action/dialog系が分離済み |
| Splash localizer | PASS | UTF-8境界維持、font group、Arabic/Thai shaped描画あり |
| 監査スクリプト | PASS/WARN | 追加済み。ただし繁体字追加には修正必須 |
| 中国語繁体字 | NOT READY | registryには存在するが `selectable=false` かつ `ChineseSimplified fallback` |
| 繁体字の段階追加 | BLOCKER | 現状の lookup は actual lang を見ないため、個別繁体字訳を追加しても拾えない |
| Dynamic翻訳coverage | WARN | instance/camera/native系は旧主要言語中心。追加言語は英語fallbackが残る |
| release時catalog重複検出 | WARN | `Q_ASSERT_X` はreleaseでは無効。監査スクリプト実行をCI化したい |

---

## 3. 良い点

### 3.1 物理分割は成功

`CMakeLists.txt` には以下が登録されている。

```txt
MelonPrimeLocalization.cpp
MelonPrimeLocalization/MelonPrimeLanguageRegistry.cpp
MelonPrimeLocalization/MelonPrimeTranslationCatalog.cpp
MelonPrimeLocalization/MelonPrimeTranslationDynamic.cpp
MelonPrimeLocalization/MelonPrimeWidgetLocalizer.cpp
MelonPrimeLocalization/MelonPrimeSplashLocalization.cpp
```

構成としてはかなり良い。

推奨ディレクトリ構成はほぼ達成済み。

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
```

### 3.2 `MelonPrimeLocalization.cpp` がfacade化されている

現在の `MelonPrimeLocalization.cpp` は、分割ファイルをincludeするだけの薄い入口になっている。

これは理想に近い。

### 3.3 `LanguageInfo` 導入は良い

`MelonPrimeLanguageRegistry.h` に以下が入っている。

```cpp
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
```

これにより、以下が一元管理しやすくなった。

```txt
- 表示名
- fallback先
- 選択可能かどうか
- RTLかどうか
- shaped splashが必要か
- splash font group
```

### 3.4 保存値互換は維持できている

`MenuLangId::First = 100` により、旧設定値 `0/1` と新しい言語IDが衝突しない。

維持すべき仕様:

```txt
- -1 = System default
- 0 = legacy native/system default
- 1 = legacy English
- 100以上 = explicit MenuLangId
```

これは今後も絶対に維持すること。

### 3.5 `TranslationCatalog` のQHash化は良い

`TranslationCatalog` が以下を持っている。

```cpp
QHash<QString, const Translation*> m_exact;
QHash<QString, const ObjectTextTranslation*> m_object;
```

これにより、毎回配列を線形探索する状態から脱却できている。

### 3.6 翻訳データのkey/value化は良い

現在は以下のような形式になっている。

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

これは以前の列順依存形式より安全。

```txt
旧: {"Save", "保存", "Speichern", "Guardar", ...}
新: {"Save", {{MenuLangId::Japanese, "保存"}, ...}}
```

列ズレ事故はかなり起きにくくなった。

---

## 4. 重要な問題

## 4.1 中国語繁体字はまだ正式対応ではない

現在の `LanguageInfo` は以下の状態。

```cpp
{MenuLangId::ChineseTraditional,
 "zh-Hant",
 "中文（繁體，简体fallback）",
 MenuLangId::ChineseSimplified,
 false,
 TextDirection::LeftToRight,
 false,
 SplashFontGroup::ChineseTraditional},
```

つまり、

```txt
- registry上には存在する
- selectable=false
- translationBase=ChineseSimplified
- 表示名も「简体fallback」
```

この状態では、繁体字は正式対応ではない。

現在の扱いは、

```txt
zh-Hantを検出しても翻訳実体はzh-Hansへfallback
UI選択肢には出さない
```

という妥協状態。

これは現段階では正しいが、「中国語繁体字も対応させたい」なら次のフェーズで変更が必要。

---

## 4.2 今のlookupでは繁体字個別訳が拾われない

現在の `TranslationFieldForLang()` は概ね以下の構造。

```cpp
const MenuLangId baseLang = ResolveTranslationLanguage(lang);
if (baseLang == MenuLangId::English)
    return entry.en;

for (const TranslationValue& value : entry.values)
{
    if (value.lang == baseLang)
        return value.text;
}
return entry.en;
```

この設計だと、`ChineseTraditional` の `translationBase` が `ChineseSimplified` の場合、実際に探索するのは `ChineseSimplified` だけになる。

つまり、後から以下を追加しても無視される。

```cpp
{MenuLangId::ChineseTraditional, "繁體訳"}
```

なぜなら `ResolveTranslationLanguage(MenuLangId::ChineseTraditional)` が `ChineseSimplified` を返し、`ChineseTraditional` 自体を探索しないため。

### 必須修正

`actual lang -> base/fallback lang -> English` の順で探索する。

```cpp
const char* FindTranslationValue(const Translation& entry, MenuLangId wanted)
{
    for (const TranslationValue& value : entry.values)
    {
        if (value.lang == wanted)
            return value.text;
    }
    return nullptr;
}

const char* TranslationFieldForLang(const Translation& entry, MenuLangId lang)
{
    if (lang == MenuLangId::English)
        return entry.en;

    if (const char* exact = FindTranslationValue(entry, lang))
        return exact;

    const MenuLangId baseLang = ResolveTranslationLanguage(lang);
    if (baseLang != lang && baseLang != MenuLangId::English)
    {
        if (const char* base = FindTranslationValue(entry, baseLang))
            return base;
    }

    return entry.en;
}
```

Object translation側も同様。

これにより、次が可能になる。

```txt
ChineseTraditional個別訳があればそれを使う
なければChineseSimplifiedへfallback
それもなければEnglish
```

この構造は、将来的に以下にも使える。

```txt
SpanishLatAm -> Spanish fallback
FrenchCanada -> French fallback
PortugueseBrazil -> Portuguese fallback
EnglishGB/US -> English fallback
```

地域差分を部分的に足せるようになる。

---

## 4.3 監査スクリプトが固定列数前提のまま

`audit-melonprime-localization.py` は追加済みで良いが、現在は以下のような固定値を持っている。

```python
EXPECTED_TRANSLATION_FIELDS = 26
EXPECTED_OBJECT_FIELDS = 26
```

さらに `parse_rows()` は各rowのC++文字列数が完全一致することを要求している。

これは key/value 形式と相性が悪い。

現状たまたま全行が同じ数の翻訳を持っているため通るが、繁体字を段階的に追加するとこうなる。

```txt
row A: source + 25言語 = 26 strings
row B: source + 25言語 + ChineseTraditional = 27 strings
```

すると監査スクリプトが落ちる。

### 必須修正

固定文字列数ではなく、`MenuLangId::X` をparseする方式へ変える。

監査すべきもの:

```txt
- source keyが空ではない
- 各TranslationValueのMenuLangIdが既知
- 同じrow内で同じMenuLangIdが重複しない
- exact keyが重複しない
- objectNameが重複しない
- selectable言語のcoverageを集計する
- required representative keyは指定言語で非空
```

繁体字対応後は、以下を追加。

```txt
- ChineseTraditionalがselectable
- ChineseTraditional coverageを出す
- ChineseTraditional未翻訳はChineseSimplified fallbackありとして扱う
```

---

## 4.4 Dynamic翻訳はまだ主要言語中心

`MelonPrimeTranslationDynamic.cpp` では、instance dialog/camera/native系などの特殊動的文言がまだ以下中心。

```txt
ja/de/es/fr/it/nl/pt/ru/zh-Hans/ko
```

追加言語は英語fallbackが残る。

これは即ブロッカーではないが、「全言語対応」と言う場合はcoverageとして明記するべき。

優先度は中。

---

## 4.5 `ReportCatalogIssue()` はreleaseでは弱い

`ReportCatalogIssue()` は以下。

```cpp
qWarning() << ...
Q_ASSERT_X(false, context, issue);
```

`Q_ASSERT_X` はrelease buildでは通常無効。

そのため、catalog重複・空key・異常はreleaseではwarning止まりになる可能性が高い。

対策:

```txt
- 監査スクリプトをCIで必ず実行する
- developer buildではassert
- release buildでは起動継続でOK
```

この方針でよい。

---

## 5. 中国語繁体字対応計画

## Phase ZH-0: 方針決定

おすすめは、**繁体字を段階的対応**にすること。

```txt
1. ChineseTraditionalを選択可能にする
2. 個別繁体字訳がある場合はそれを使う
3. 未翻訳keyはChineseSimplifiedへfallback
4. coverage reportで繁体字翻訳率を出す
5. coverageが十分になったら表示名からfallback注記を外す
```

この方針なら、すべての翻訳を一気に繁体字化しなくてもリリースできる。

---

## Phase ZH-1: lookupを actual -> fallback -> English に変更

最優先。

対象:

```txt
MelonPrimeTranslationCatalog.cpp
```

修正対象:

```txt
TranslationFieldForLang()
ObjectTranslationFieldForLang()
```

修正後の探索順:

```txt
1. ActiveMenuLanguageそのもの
2. ResolveTranslationLanguage(lang) のfallback/base
3. English
```

この変更を入れないと、繁体字個別訳を追加しても使われない。

---

## Phase ZH-2: LanguageInfoを変更

現在:

```cpp
{MenuLangId::ChineseTraditional,
 "zh-Hant",
 "中文（繁體，简体fallback）",
 MenuLangId::ChineseSimplified,
 false,
 TextDirection::LeftToRight,
 false,
 SplashFontGroup::ChineseTraditional},
```

段階対応案:

```cpp
{MenuLangId::ChineseTraditional,
 "zh-Hant",
 "中文（繁體）",
 MenuLangId::ChineseSimplified,
 true,
 TextDirection::LeftToRight,
 false,
 SplashFontGroup::ChineseTraditional},
```

ポイント:

```txt
- selectable=true
- displayNameから「简体fallback」を外すか、初期段階なら「繁體 / 简体fallback」と明記
- translationBaseは当面ChineseSimplifiedのままでOK
```

`translationBase` は「完全に同一扱い」ではなく、「未翻訳時のfallback先」として使う。

---

## Phase ZH-3: 監査スクリプト更新

対象:

```txt
tools/ci/audits/localization/audit-melonprime-localization.py
```

変更:

```txt
- EXPECTED_TRANSLATION_FIELDS / EXPECTED_OBJECT_FIELDS を廃止
- C++文字列数ベースの行検査をやめる
- TranslationValueのMenuLangIdをparseする
- row内のMenuLangId重複を検出
- selectable言語coverageを集計
- ChineseTraditionalをselectableとして期待
- zh-Hant coverageを表示
- zh-Hant未翻訳はzh-Hans fallback扱いでWARNにする
```

出力例:

```txt
[PASS] ChineseTraditional selectable
[INFO] zh-Hant exact coverage: 35/420, fallback to zh-Hans: 385
[INFO] zh-Hant object coverage: 12/180, fallback to zh-Hans: 168
```

---

## Phase ZH-4: 翻訳追加

まずは短い共通UIから追加。

優先順位:

```txt
1. Common controls
   Save / Cancel / OK / ON / OFF / Reset / Generate / Copy Output

2. Menu labels
   File / Config / Input Config / View / Help

3. no-ROM splash
   File->Open ROM...
   to get started

4. Input Config major headings
   INPUT SETTINGS
   SENSITIVITY
   BUG FIXES
   GAME FEATURE IMPROVEMENTS
   VIDEO QUALITY
   VOLUME

5. Custom HUD
   主要ボタン・ラベルのみ

6. 長文objectName翻訳
   description系は後回し
```

繁体字訳は `MenuLangId::ChineseTraditional` として追加する。

```cpp
{
    "Save",
    {
        {MenuLangId::ChineseSimplified, "保存"},
        {MenuLangId::ChineseTraditional, "儲存"},
    }
}
```

---

## Phase ZH-5: スモーク

必須確認:

```txt
1. System default が zh-Hant-TW / zh-TW / zh-HK を拾う
2. Language comboに 中文（繁體） が出る
3. 中文（繁體）を選択してOK保存
4. 再起動後も繁体字のまま
5. 未翻訳keyは簡体字fallbackまたはEnglish fallbackで落ちない
6. no-ROM splashが繁体字フォントで表示される
7. Input Config開閉/Cancel/OKでクラッシュしない
8. 終了時クラッシュしない
```

---

## 6. 推奨修正プロンプト

Cursor / Claudeに投げるならこれ。

```txt
多言語リファクタリング後の監査結果です。
中国語繁体字対応のため、以下を修正してください。

1. TranslationFieldForLang / ObjectTranslationFieldForLang の探索順を変更してください。
   現在は ResolveTranslationLanguage(lang) 後のbaseLangだけを見ています。
   そのため ChineseTraditional の translationBase が ChineseSimplified の場合、
   {MenuLangId::ChineseTraditional, "..."} を追加しても使われません。

   修正後:
   - まず実際の lang を探す
   - 見つからなければ ResolveTranslationLanguage(lang) のfallback/baseを探す
   - 見つからなければEnglishへfallback

2. LanguageInfoのChineseTraditionalを段階対応に変更してください。
   - selectable=true
   - displayNameは "中文（繁體）" または初期段階なら "中文（繁體 / 简体fallback）"
   - translationBaseは当面 ChineseSimplified のままでOK
   - SplashFontGroup::ChineseTraditional は維持

3. audit-melonprime-localization.py を修正してください。
   現在 EXPECTED_TRANSLATION_FIELDS=26 / EXPECTED_OBJECT_FIELDS=26 の固定列数前提です。
   key/value形式と繁体字の段階追加に合わないため、
   TranslationValueのMenuLangIdをparseしてcoverageを出す方式にしてください。

   必須:
   - row内MenuLangId重複検出
   - exact key重複検出
   - objectName重複検出
   - selectable言語coverage表示
   - ChineseTraditional selectableを検査
   - zh-Hant未翻訳はzh-Hans fallbackとしてcoverageに出す

4. 繁体字翻訳は短い共通UIから追加してください。
   優先:
   - Save / Cancel / OK / ON / OFF / Reset
   - File / Config / Input Config
   - File->Open ROM...
   - to get started
   - INPUT SETTINGS / SENSITIVITY / BUG FIXES など主要heading

5. 既存仕様は維持してください。
   - MenuLangId::First=100
   - -1=System default
   - legacy 0/1 migration
   - Arabic/Italian保存値
   - Arabic/Thai shaped splash
   - UTF-8 bounded copy
   - ScreenPanel終了時クラッシュ対策
   - RunFrameHook / input hot path / perf probeには触らない

DoD:
- audit script green
- build green
- 中文（繁體）が選択肢に出る
- zh-Hant-TW / zh-TW / zh-HK 系localeを拾う
- 中文（繁體）を選択・保存・再起動して維持される
- 繁体字個別訳があるkeyは繁体字表示
- 未翻訳keyは簡体字fallback
- no-ROM splash文字化けなし
- Input Config開閉/Cancel/OKでクラッシュなし
- 終了時クラッシュなし
```

---

## 7. 最終判定

現状のリファクタリングは、**構造分割としては合格**。

```txt
判定: PASS with follow-up
```

ただし、中国語繁体字を正式対応する前に、以下2点は必須。

```txt
1. Translation lookupを actual -> fallback -> English にする
2. audit scriptを固定列数前提からMenuLangId parse方式にする
```

この2点を直せば、繁体字はかなり安全に段階追加できる。

---

## 8. 対応完了ログ

対応日: 2026-07-04

実施内容:

```txt
- Translation lookupを actual lang -> translationBase fallback -> English に変更
- Object translation lookupも同じfallback順に変更
- ChineseTraditionalをselectable=trueへ変更
- displayNameを「中文（繁體）」へ変更
- translationBase=ChineseSimplifiedは未翻訳fallbackとして維持
- audit scriptを固定列数検査からMenuLangId付きTranslationValue parse方式へ変更
- row内MenuLangId重複、未知MenuLangId、空翻訳、key重複を検査
- zh-Hant direct coverage と zh-Hans fallback coverage を出力
- 短い共通UI/menu/no-ROM/主要headingにChineseTraditional訳を追加
```

検証:

```txt
python3 tools/ci/audits/localization/audit-melonprime-localization.py
cmake --build build-mac --parallel 4
no-ROM起動 -> quit smoke
```

監査結果:

```txt
zh-Hant exact direct 23/966, zh-Hans fallback 943
zh-Hant object direct 0/30, zh-Hans fallback 30
```
