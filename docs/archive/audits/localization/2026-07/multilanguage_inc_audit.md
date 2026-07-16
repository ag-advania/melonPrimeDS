# multilanguage_inc.zip audit — 改訂版

改訂日: 2026-07-12
元監査対象: `multilanguage_inc.zip`
現行確認対象: `ag-advania/melonPrimeDS` / `highres_fonts_v3`

## 結論

元の監査結果は、**当時の`multilanguage_inc.zip`に対する履歴資料としては有効**です。
ただし、現在の`highres_fonts_v3`の翻訳状態を表す監査ではありません。

したがって、元の結論:

```text
NG / そのまま採用非推奨
```

は、当時のZIPに対してのみ維持します。

現行ブランチに対する結論:

```text
構造: PASS
言語レジストリ: PASS
翻訳品質: WARN / 継続修正が必要
```

## 元監査で正しかった履歴事項

当時のZIPでは、追加言語の多くが次の状態でした。

- 一般UIが英語keyのコピー
- melonDSダイアログが英語keyのコピー
- Object説明文が日本語コピー
- 一部Object行が欠落
- ZIP直下に3つの`.inc`があり、リポジトリ内の配置構造を持たない

これらは、当時のZIPを不採用と判断する十分な理由でした。

## 現在は無効になった指摘

### `EnglishUK` / `EnglishGB`

現行言語レジストリでは次を使用しています。

```cpp
MenuLangId::EnglishGB
stable code: en-GB
display name: English (UK)
fallback: English
```

`EnglishUK`を採用すべきかという懸念は解消済みです。

### `SpanishLatAm`

現行レジストリには次が存在します。

```cpp
MenuLangId::SpanishLatAm
stable code: es-419
display name: Español (Latinoamérica)
fallback: Spanish
```

「SpanishLatAmが欠落している可能性」は現行状態には該当しません。

### Portugueseの地域分離

現行レジストリでは次の構成です。

```text
Portuguese        -> pt / Português (Portugal)
PortugueseBrazil  -> pt-BR / Português (Brasil)
```

`PortuguesePortugal`という別enumを新設する必要はありません。

### ChineseTraditional

現行レジストリでは`ChineseTraditional`は選択可能です。

```text
stable code: zh-Hant
display name: 中文（繁體）
fallback: ChineseSimplified
selectable: true
```

また`ChineseHongKong`は`ChineseTraditional`へfallbackします。

## 現行ブランチで確認された未解消問題

元監査の「翻訳が完全ではない」という大枠は、品質面では引き続き当てはまります。
ただし、問題の内容は変わっています。

現在の代表例:

- オンラインHP説明に機械翻訳由来の語順崩れや`update`混在がある
- Headshot通知説明に格・数・性の不一致がある
- 一部ダイアログタイトルの語順が不自然
- 一部ON/OFF表示が状態ラベルとして不適切
- Odiaのステージ説明に用語誤記がある

## 監査ルールの改訂

現在は、次の単純判定を禁止します。

```text
translation == English key
```

理由:

- 武器名
- ハンター名
- ステージ名
- ゲームモード名
- HUD / OSD / HP / FPS
- ARM9 / ROM / TOML / OpenGL
- 開発者向け内部識別子

は、英語維持が適切な場合があります。

今後は、各項目を次へ分類します。

```text
TRANSLATE_UI
KEEP_TECHNICAL
KEEP_GAME_NAME
KEEP_WEAPON_NAME
KEEP_MAP_NAME
KEEP_MODE_NAME
KEEP_ABBREVIATION
KEEP_DEVELOPER
NEEDS_CONTEXT
```

## 現行監査の基準ファイル

```text
MelonPrimeTranslations.inc
89d99dfb0b268e6d470bafe41cc9fa80a81b1eff

MelonPrimeObjectTranslations.inc
ed0ebe46f34c9c320836ab1e22882b87624a1e98

MelonPrimeLocalizationMelondsDialogs.inc
bf982d3d4333c9acfc8dec3a14ace98f139d8e49
```

## 最終判断

```text
元ZIPに対する不採用判断: 維持
現行ブランチへの流用: 禁止
現在の品質課題: 別途、現行ソースを基準に修正
```
