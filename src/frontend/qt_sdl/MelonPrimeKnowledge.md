# MelonPrime Raw Input 最適化ナレッジ

## 対象モジュール

`MelonPrimeRawInputState` / `MelonPrimeRawInputWinFilter`

---

## スレッディングモデル

MelonPrime の Raw Input 層は2つのモードを持ち、いずれも **書き込みスレッドは常に1つ** である。

| モード | ライター | リーダー | 入力経路 |
|---|---|---|---|
| Joy2Key ON | Qt メインスレッド | Emu スレッド | `nativeEventFilter` → `processRawInput` |
| Joy2Key OFF | Emu スレッド | Emu スレッド | `Poll` → `processRawInputBatched` |

この Single-Writer 保証が、以下の最適化の正当性の根拠となる。

---

## 適用した最適化

### 1. CAS ループ / locked RMW → relaxed load + release store

**原理:**
書き込みスレッドが1つしかない場合、`compare_exchange_weak` / `fetch_or` / `fetch_and` / `fetch_add` は不要。
これらは x86 で `LOCK CMPXCHG` / `LOCK OR` / `LOCK AND` / `LOCK XADD` を生成し、キャッシュラインの排他取得で ~20-40 cyc かかる。

Single-Writer なら `load(relaxed)` → 計算 → `store(release)` で十分。
x86 では通常の `MOV` + `MOV` になり ~1-5 cyc。

**適用箇所:**

| 箇所 | Before | After |
|---|---|---|
| `setVkBit` | `fetch_or` / `fetch_and` | load + store |
| `processRawInput` マウス座標 | `fetch_add` ×2 | load + store ×2 |
| `processRawInput` マウスボタン | CAS ループ | load + store |
| `processRawInputBatched` コミット（VK） | CAS ループ ×4 | load + store ×4 |
| `processRawInputBatched` コミット（ボタン） | CAS ループ | 直接 store |
| `processRawInputBatched` コミット（マウス座標） | `fetch_add` ×2 | load + store ×2 |

**削減見積もり:** ホットパス全体で LOCK プレフィクス命令 ~10個を排除、~200-400 cyc/frame 削減。

### 2. `pollHotkeys` フェンス集約

5回の `acquire` load → 5回の `relaxed` load + `atomic_thread_fence(acquire)` 1回。

x86 では acquire load は `MOV` なので実質同等だが、ARM/RISC-V でフェンス命令を4回削減。
コードの意図（「ここで全ての先行 store を観測する」）も明確になる。

---

## 適用しなかった / 取り消した変更

### ❌ `static thread_local` バッファ → スタックバッファ（リバート済み）

**当初の意図:** TLS ガードチェック (~5-10 cyc) の排除。

**実際の問題:**
- POD 配列の `static thread_local` は MSVC で `__readgsqword` 一発。ガードチェックは発生しない（非 POD やコンストラクタ付きの場合のみ発生）
- 16KB のスタック確保は MSVC で `__chkstk` を誘発（4KB ページ境界ごとにプローブ → 4回）
- `alignas(64)` + 16KB でレジスタ圧迫が発生し、内側のイベント処理ループの最適化が悪化
- 8000Hz マウスの場合、1フレーム ~133 イベントをループ処理するため影響が体感できるレベルに

**教訓:** `static thread_local` の POD 配列は十分高速。大きなバッファをスタックに持ってくるのは `__chkstk` とレジスタ圧迫の両面でマイナスになりうる。TLS のコストを過大評価しないこと。

### ❌ `Poll()` ドレインループの上限設定（不採用）

**当初の意図:** 無限ループによるフレーム遅延の防止。

**不採用理由:**
- 8000Hz マウスでは1フレームあたり ~133 メッセージが通常発生する
- 512 の上限では安全マージンが小さい
- 現時点で問題は発生していない
- `GetRawInputBuffer` がデータを消費済みなので、ドレインはメッセージの削除のみ（データコピーなし）で高速

---

## 環境メモ

- マウス: 8000Hz ポーリングレート
- 60fps 想定時: ~133 WM_INPUT メッセージ/フレーム（マウスのみ）
- コンパイラ: MSVC / MinGW（両対応）
- `NEXTRAWINPUTBLOCK` マクロ使用のため MinGW では `QWORD` typedef が必要
