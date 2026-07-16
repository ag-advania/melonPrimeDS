# MelonPrimeDS i18n Release Quality Final — 改訂版

改訂日: 2026-07-12

## 訂正

旧レポートの`AUTOMATED_RELEASE_QUALITY_GATE_PASSED`は、
その時点のリリース候補と、その時点の静的ゲートに対する結果です。

これは次を保証しません。

- 現在の`highres_fonts_v3`に明白な誤訳がない
- すべての文がネイティブ品質である
- RTL表示に問題がない
- 複雑文字系のshapingやクリッピングに問題がない
- 全ダイアログが実行時に正しく表示される
- 後から追加された翻訳に回帰がない

## 改訂後の表現

旧:

```text
AUTOMATED_RELEASE_QUALITY_GATE_PASSED
```

改訂:

```text
HISTORICAL_STATIC_GATE_PASSED
CURRENT_NATIVE_QUALITY_REVIEW_REQUIRED
```

## 現行ブランチの状態

### PASS

- 翻訳基盤の構造
- language registry
- region fallback
- ChineseTraditional選択
- ChineseTraditional動的fallback
- s2twpによる台湾語彙変換
- 台湾UI用語の決定的な補正辞書

### WARN

- 一部言語のオンラインHP説明
- 一部言語のHeadshot通知
- 一部ダイアログタイトル
- 一部ON/OFF状態表示
- Odiaの一部用語
- 実画面での折り返し、RTL、shaping、文字欠け

## Release Qualityの定義

今後は静的ゲート合格とRelease Qualityを分離します。

### Static Gate

```text
coverage
duplicate key
duplicate MenuLangId
empty translation
placeholder
HTML tag balance
UTF-8
control characters
```

### Native Quality

```text
意味が正しい
自然な語順
格・数・性が正しい
UI文脈に合う
技術語の扱いが一貫している
```

### Runtime UI

```text
RTL
shaping
font fallback
line wrapping
button clipping
menu visibility
dialog layout
```

## 改訂後の最終判定

```text
Static structure: PASS
Current native quality: WARN
Release declaration: 保留
```

「release quality」と宣言するには、
現行ソースに対する再監査と実画面確認が必要です。
