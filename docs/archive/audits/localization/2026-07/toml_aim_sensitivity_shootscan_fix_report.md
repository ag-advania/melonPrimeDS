# TOML / Aim / Sensitivity / Shoot / Scan Quality Fix Report — 改訂版

改訂日: 2026-07-12
元対象: `melonprime-i18n-release-candidate.zip`
現行参照: `highres_fonts_v3`

## このレポートの位置づけ

元レポートに記録された次の値は、当時のリリース候補に対する履歴値です。

```text
patched_rows: 598
main: 563
object: 35
dialogs: 0
```

これらを現行ブランチの未修正数または修正済み数として扱ってはいけません。

## 履歴として維持する内容

当時のパスでは、次の直接的な英語残存を広範囲に修正しました。

- Shoot/Scan
- Scan/Shoot
- AimSensitivity Up/Down
- Scan Visor
- SENSITIVITY
- CROSSHAIR
- TOML周辺の壊れた混在文

元レポートが示した「当時の候補ZIPで598件を変更した」という事実は維持します。

## 現行ソースへの適用可否

元CSVや元パッチを、現行ソースへそのまま適用することは禁止します。

理由:

1. 翻訳行の内容がすでに変化している
2. 言語数とregion fallback構成が変化している
3. ChineseTraditionalとChineseHongKongの処理が追加・改善されている
4. 同じキーでも、現在の翻訳の方が元CSVのafter値より自然な場合がある
5. 機械的な全文置換により、既存の良い現地語を英語へ戻す危険がある

## 現行での再監査ルール

次を検索対象にします。

```text
Shoot/Scan
Scan/Shoot
AimSensitivity
Scan Visor
SENSITIVITY
CROSSHAIR
Custom HUD
TOML
```

ただし、検出しただけでは修正しません。

分類:

```text
明白な一般UI英語残存
技術用語として許容
ゲーム固有名詞
開発者向け内部名
文脈確認が必要
```

## TOMLの扱い

`TOML`自体は技術規格名であるため翻訳しません。

修正対象になるのは次です。

```text
TOMLを含む文の語順が壊れている
TOML tableの意味が現地語で伝わらない
英語と現地語が助詞なしで連結されている
```

## Aim / Sensitivityの扱い

`AimSensitivity`が設定キーまたは内部識別子として表示される場合と、
ユーザー向け操作名として表示される場合を区別します。

```text
設定キー: 維持可
ユーザー向け操作名: 自然な現地語へ翻訳
```

## 現行で別途確認された課題

元レポートの対象語以外に、現在は次を優先します。

- オンラインHP説明
- オンラインHeadshot通知
- Video settingsタイトル
- ON/OFF状態表示
- Odiaの用語誤記

## 最終判断

```text
元レポートの修正件数: 履歴として有効
元パッチの現行ブランチへの再適用: 禁止
現行修正: Git blobを固定して個別に再監査
```
