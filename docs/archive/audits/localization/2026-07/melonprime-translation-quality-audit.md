# MelonPrimeLocalization 翻訳品質監査レポート — 改訂版

改訂日: 2026-07-12
対象ブランチ: `highres_fonts_v3`

## 総評

```text
構造: PASS
言語レジストリ: PASS
ChineseTraditional対応: PASS
動的fallback: PASS
台湾UI自動変換基盤: PASS
翻訳自然さ: WARN
```

過去の監査でHighとした繁体字関連の2件は、現行ブランチでは解消済みです。

## 解消済み1: 動的翻訳のfallback

過去の問題:

```cpp
switch (ActiveMenuLanguage())
```

を直接使うことで、`ChineseTraditional`が`ChineseSimplified`へfallbackせず、
動的文言だけ英語になる可能性がありました。

現行実装では、動的翻訳の主要分岐が次を使用しています。

```cpp
switch (ResolveTranslationLanguage(ActiveMenuLanguage()))
```

さらにChineseTraditional専用文言が必要な箇所は、
fallback分岐より前に明示的に処理しています。

確認済みの系統:

- Configuring settings for instance N
- Configuring mappings for instance N
- Configuring paths for instance N
- Setting battery levels for instance N
- `(none)`
- Direct mode
- native suffix
- inner camera / outer camera

判定:

```text
RESOLVED
```

## 解消済み2: OpenCCと台湾UI用語

過去の問題:

```python
OpenCC("s2t")
```

では、文字を繁体字にするだけで、台湾UI向け語彙へ十分変換できませんでした。

現行ツール:

```python
OpenCC("s2twp")
```

を使用しています。

さらに、決定的な後処理辞書があります。

```text
以太網 -> 乙太網路
攝像頭 -> 相機
默認 -> 預設
設置 -> 設定
文件 -> 檔案
加載 -> 載入
連接 -> 連線
網絡 -> 網路
鼠標 -> 滑鼠
屏幕 -> 螢幕
圖標 -> 圖示
布局 -> 佈局
質量 -> 畫質
爲 -> 為
```

判定:

```text
RESOLVED
```

## 現在も有効な品質課題

繁体字基盤とは別に、全言語の自然さには未解消問題があります。

### オンラインHP説明

一部言語では次の問題を確認しました。

- `update`を現地語文中へ直接挿入
- 同じ意味の動詞や助動詞が重複
- 主語・述語の一致不良
- 格・数・性の誤り
- 「誰を撃ったかの大まかな目安」という意味が崩れている

### オンラインHeadshot通知

一部言語では次の問題があります。

- headshot相当語の重複
- 通知・試合・オンラインの格変化不良
- 単数と複数の不一致
- 語尾が機械的に連結されている

### melonDSダイアログタイトル

現行ソースで確認した例:

```text
Filipino: Video mga setting
Swahili: Video mipangilio
Croatian: postavke videa
Icelandic: myndstillingar
```

自然な候補:

```text
Filipino: Mga setting ng video
Swahili: Mipangilio ya video
Croatian: Postavke videa
Icelandic: Myndstillingar
```

### ON/OFF

Filipino、Telugu、Malayalamの一部訳は、
状態表示ではなく別の意味または不自然な表記になっています。

### Odia

ステージ拡張説明で、重力を表す語に誤記が2箇所あります。

## ステージ名表記ルール

ステージ名は次の形式を維持します。

```text
現地語のステージ名 (正式英語名)
```

括弧内の英語は意図的な補助表記であり、
英語残存として検出しません。

例:

```text
ଇନ୍ଧନ ଭଣ୍ଡାର (Fuel Stack)
```

一方、説明本文の一般語を英語へ戻すことは禁止します。

例:

```text
Defender ring
jumper
item
transform corridor
```

これらが既に自然な現地語になっている場合、英語へ置換しません。

## 現行基準SHA

```text
MelonPrimeObjectTranslations.inc
ed0ebe46f34c9c320836ab1e22882b87624a1e98

MelonPrimeTranslationDynamic.cpp
92e52819bfea20f9156e4f88a6c01b7564148f7e

complete_zh_hant_from_zh_hans.py
02bcc76a3f293be731ffda60039be76be5f9b531
```

## 最終判断

過去レポートの「繁体字High問題」はクローズします。

```text
ZH dynamic fallback: CLOSED
ZH OpenCC/Taiwan terminology: CLOSED
General native-quality review: OPEN
```
