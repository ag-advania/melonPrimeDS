# Portuguese Menu Audit Report — 改訂版

改訂日: 2026-07-12

## 元レポートの有効範囲

元レポートのcoverage、空文字、制御文字、重複表示ラベルの結果は、
当時の3翻訳ファイルに対する静的監査結果です。

重複表示ラベルは、直ちにバグとは限りません。

例:

```text
Fast forward / Fast-Forward
&Close / Close
```

異なるUI部品が同じ表示文字列を使うことは正常です。

## 重要な訂正

Portugueseでメニュー項目が消える問題は、
翻訳テーブルのcoverage不足が直接原因ではありませんでした。

Qt/macOSでは、`QAction::TextHeuristicRole`により、
翻訳後の文字列に`config`、`setup`、`prefer`、`option`等が含まれると、
ActionがmacOSのネイティブPreferences枠へ再配置され、
元のメニューから消えることがあります。

したがって、次は分離して扱います。

```text
翻訳coverage監査
QAction MenuRole監査
実行時メニュー表示監査
```

## 現在の静的判定

Portuguese翻訳は、少なくとも過去監査時点では次を満たしていました。

- missing entryなし
- empty stringなし
- whitespace-onlyなし
- C0 control characterなし
- top-menu/actionの静的blockerなし

この結果は、Portugueseの翻訳品質が完全に自然であることや、
実行時に全Actionが見えることを保証しません。

## 重複ラベルの扱い

次のような重複は、文脈確認なしで修正しません。

```text
Geral
Tamanho da tela
Configurações do MelonPrime
Avanço rápido
Fechar
```

キーが異なっても同じ意味なら、同じ訳で正常な場合があります。

## 現行の推奨ゲート

### Static

```text
coverage
empty
control characters
mnemonic
leading/trailing whitespace
placeholder
```

### QAction

```text
real Preferences action以外はNoRole
translated textによるTextHeuristicRole再分類を防止
```

### Runtime

```text
EnglishとPortugueseで各メニューのAction数を比較
Configメニュー
MelonPrimeメニュー
Preferences
Input
Video
Audio
Network
Cheat
ROM info
RAM search
Custom HUD editor
```

## 最終判断

```text
Portuguese static coverage: PASS
Duplicate visible labels: REVIEW ONLY
Runtime menu visibility: translation tableとは別問題
```
