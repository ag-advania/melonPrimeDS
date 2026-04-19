# クロスヘアスムージング 設計案

**ステータス: 見送り（未実装）**
このゲームは30fpsで動作するため、高リフレッシュレート環境（60hz〜540hz）でクロスヘアの移動がカクカクになる問題を解決するための設計案。現時点では実装しないが、将来の参考のためにここに残す。

---

## 問題の構造

NDS ゲームロジックは 30fps で `crosshairPosX`/`crosshairPosY`（DS座標系 0〜255、絶対位置、uint8\_t）を更新する。MelonPrimeDS の描画ループは NDS フレームレート（約60fps）で `DrawCrosshair` を呼ぶが、ゲームが更新しないフレームでは前フレームと同じ座標が読める。結果として、60hz 表示でも2フレームに1回しかクロスヘアが動かない。

```
NDS frame 1: ゲーム更新 → crosshairPosX=130 → 130 で描画
NDS frame 2: ゲーム更新なし → crosshairPosX=130(変わらず) → 130 で描画  ← カクつき
NDS frame 3: ゲーム更新 → crosshairPosX=134 → 134 で描画
NDS frame 4: ゲーム更新なし → crosshairPosX=134(変わらず) → 134 で描画  ← カクつき
```

---

## なぜ `aimX`/`aimY` を直接使えないか

`*m_ptrs.aimX` / `*m_ptrs.aimY` は **速度値（デルタ、int16\_t 相当）** であり、絶対座標ではない。ゲームがこれを読み取って `crosshairPosX += aimX * 内部スケール` という処理をする。座標系が異なるため、直接クロスヘア描画位置として流用できない。

```
我々が書く : aimX = +5   (「5単位右に動かせ」という指示)
ゲームが処理: crosshairPosX += aimX * scale  (ゲーム内部スケール、未知)
DrawCrosshair: crosshairPosX を読む (絶対座標)
```

---

## 方針A: NDS フレームレートレベルのスムージング（実装コスト小）

### 概要

`DrawCrosshair` 内部に位置履歴（static 局所変数）を持ち、ゲームが更新しなかったフレームは直前2フレームの確定位置から線形外挿する。

### アルゴリズム

```
確定位置 A (2回前): s_smPrev
確定位置 B (1回前): s_smCurr
ゲーム更新間隔:      s_smPeriod (NDS フレーム数、通常 2)
経過フレーム数:      s_smAge

ゲームが更新した場合:
  s_smPrevX = s_smCurrX
  s_smCurrX = rawX
  s_smPeriod = max(1, s_smAge)
  s_smAge = 0
  → 実際の rawX で描画

ゲームが更新しなかった場合:
  s_smAge++
  t = s_smAge / s_smPeriod   (通常 0.5)
  predX = s_smCurrX + t * (s_smCurrX - s_smPrevX)
  → 予測値 predX で描画（0〜255 にクランプ）
```

### 具体例（等速移動）

| NDS frame | ゲーム更新 | crosshairPosX (RAM) | 描画値 | 状態 |
|---|---|---|---|---|
| 1 | ✓ (+2) | 130 | 130 | 確定 |
| 2 | ✗ | 130 | **131** | 外挿 (t=0.5, vel=+2) |
| 3 | ✓ (+2) | 134 | 134 | 確定 |
| 4 | ✗ | 134 | **136** | 外挿 (t=0.5, vel=+4) |

等速移動ではほぼ完璧に補正できる。急停止・反転時は最大1フレーム分のズレが発生するが、次のゲーム更新フレームで正確な位置に戻る。

### 実装箇所

- **変更ファイル**: `MelonPrimeHudRenderDraw.inc` の `DrawCrosshair` のみ
- **入力パイプライン変更**: 一切なし
- **追加遅延**: なし（外挿のため）
- **追加状態**: static 局所変数 6個（`s_smPrevX/Y`, `s_smCurrX/Y`, `s_smPeriod`, `s_smAge`）
- **OPT-DR1（ダーティレクト）**: `pxCx`/`pxCy` 基準のため変更不要
- **リセット**: 大きなジャンプ（`|rawX - s_smCurrX| + |rawY - s_smCurrY| > 閾値`）を検出して自動リセット

### 設定キー（案）

```
Metroid.Visual.CrosshairSmooth  (bool, default true)
```

`CachedHudConfig` / `CrosshairHudConfig` に `bool chSmooth` を追加し、`DrawCrosshair` で参照。

### 限界

- 改善範囲は **NDS フレームレート（60fps）まで**
- 120hz/240hz/540hz 表示では OS/GPU による静止フレームの繰り返しが残る（60fps → 30fps 比は改善）
- 急停止・方向転換時に1フレームだけ予測誤差が発生する可能性がある

---

## 方針B: 表示リフレッシュレート完全追従（実装コスト大）

クロスヘアだけを NDS フレームループから切り離し、ディスプレイリフレッシュレートで独立して描画する。

### 概要

1. NDS フレームループとは別にクロスヘア専用レンダリングスレッド（または Qt タイマー）を用意
2. マウスデルタを毎フレーム（120hz/240hz/540hz 相当）蓄積
3. 蓄積デルタ × キャリブレーションスケールで絶対位置を予測し、OS ネイティブなオーバーレイとして描画

### 必要な追加要素

- **キャリブレーション**: `aimX` のデルタと `crosshairPosX` の変化量の比率を自動測定して `scale` を推定
- **スレッド分離**: クロスヘアバッファを NDS レンダーテクスチャから独立させる
- **タイミング同期**: 表示リフレッシュタイミングとのフェーズ合わせ

### 備考

アーキテクチャの大幅変更が必要であり、既存の OPT-DR1 ダーティレクトや GL オーバーレイパスとの整合性を再設計する必要がある。ハイパフォーマンス要件との両立が難しいため、方針A での改善が確認できた後の将来的な発展として検討する。

---

## 関連ファイル

| ファイル | 関係 |
|---|---|
| `MelonPrimeHudRenderDraw.inc` | `DrawCrosshair` — 方針A の変更箇所 |
| `MelonPrimeHudRenderConfig.inc` | `CrosshairHudConfig` — `chSmooth` フラグの追加先（案） |
| `MelonPrimeGameInput.cpp` | `ProcessAimInputMouse` — `aimX`/`aimY` の書き込み元（方針Aでは変更不要） |
| `Config.cpp` | `DefaultBools` に `Metroid.Visual.CrosshairSmooth` を追加（案） |
