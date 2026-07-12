# MelonPrimeDS ローカライズ監査MD 改訂版インデックス

改訂日: 2026-07-12
対象リポジトリ: `ag-advania/melonPrimeDS`
対象ブランチ: `highres_fonts_v3`

## 目的

過去に作成した監査MDには、当時の中間ZIPや古いリリース候補を対象にした結果が含まれています。
それらを現行ブランチの状態として読むと、すでに解消済みの問題を未修正と誤認したり、
反対に現在残っている問題を見落としたりする可能性があります。

本パックでは、過去の監査結果を削除せず、次の区分へ整理しました。

- **Historical finding**: 当時の入力ファイルに対しては正しかった記録
- **Resolved in current branch**: 現行`highres_fonts_v3`では解消済み
- **Still applicable**: 現行ブランチでも確認できる問題
- **Not revalidated**: 現行ソースに対する再検証が済んでいないため断定しない項目

## 現行基準

各ファイルのGit blob SHAは`CURRENT_STATE_BASELINE.json`に記録しています。

主要な3翻訳ファイル:

```text
MelonPrimeTranslations.inc
89d99dfb0b268e6d470bafe41cc9fa80a81b1eff

MelonPrimeObjectTranslations.inc
ed0ebe46f34c9c320836ab1e22882b87624a1e98

MelonPrimeLocalizationMelondsDialogs.inc
bf982d3d4333c9acfc8dec3a14ace98f139d8e49
```

## 改訂対象

- `multilanguage_inc_audit.md`
- `melonprime-translation-quality-audit.md`
- `toml_aim_sensitivity_shootscan_fix_report.md`
- `release_quality_final_report.md`
- `portuguese_menu_audit_report.md`

## 現在の優先課題

現行ソースで確認できた主な未解消項目:

1. 一部言語のオンラインHP説明に、`update`の英語混在、重複助動詞、格変化の崩れがある。
2. 一部言語のオンラインHeadshot通知説明に、重複語、格・数・性の不一致がある。
3. `Video settings - melonDS`の一部言語に明白な語順または文頭小文字がある。
4. Filipino、Telugu、Malayalamの一部ON/OFF訳がUI状態表示として不適切。
5. Odiaのステージ拡張説明に、重力を意味する語の誤記が2箇所ある。

## 解消済みとして扱う項目

- ChineseTraditionalの動的文言が英語へ戻る問題
- 動的翻訳で`ActiveMenuLanguage()`を直接switchしてfallbackを無視する問題
- OpenCCが単純な`s2t`のみを使用していた問題
- 台湾UI語彙補正辞書が存在しなかった問題
- `EnglishUK`と`EnglishGB`の命名懸念
- `SpanishLatAm`が存在しないという懸念
- `PortuguesePortugal`を別enumとして持つという懸念

## 注意

過去レポートに記載されたパッチ件数やcoverage件数は、
そのレポートが対象としたZIPまたはリリース候補に対する履歴値です。
現行ブランチの現在値として再利用しません。
