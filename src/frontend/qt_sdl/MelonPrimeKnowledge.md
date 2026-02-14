# MelonPrime 最適化ナレッジ

## 対象モジュール

- Raw Input 層: `MelonPrimeRawInputState` / `MelonPrimeRawInputWinFilter`
- ゲームロジック層: `MelonPrime.h` / `MelonPrimeInGame.cpp` / `MelonPrimeGameWeapon.cpp`

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

## ゲームロジック層の最適化

### 3. `setRawInputTarget` HWND ガード

`UpdateInputState` から毎フレーム呼ばれるが、HWND は通常変わらない。
先頭に `if (m_hwndQtTarget == hwnd) return;` を追加し、不要な書き込みと Joy2Key ON 時の Unregister/Register を排除。

### 4. `HandleAdventureMode` UI ボタン早期脱出

UI ボタン5個（OK, LEFT, RIGHT, YES, NO）の `IsPressed` を個別にチェックしていたのを、`IB_UI_ANY` 合成マスクで1回のビットテストに集約。99%以上のフレームでは UI ボタンは押されないため、5回の関数呼び出しを1回に削減。

### 5. WeaponData 重複 LUT 統合

`ID_TO_ORDER_INDEX[]`（C配列）と `ID_TO_ORDERED_IDX`（`std::array`）が完全に同一内容だった。`ID_TO_ORDERED_IDX` に統合し、参照箇所を修正。

### 6. 再入パスへの Fresh Poll 追加（エイムレイテンシ修正）

**発見した問題:**

`RunFrameHook` のフレームパイプラインをトレースした結果、**武器切替・モーフ中にエイムデータが 66ms stale になる**ことが判明。

```
通常フレーム（問題なし）:
  Poll()                    [T+0.0ms]  入力収穫
  UpdateInputState()        [T+0.01ms] デルタ確定
  HandleInGameLogic()
    ProcessAimInputMouse()  [T+0.03ms] aim書込
  NDS::RunFrame()           [T+0.05ms] ゲーム読み取り
  → Poll〜aim: ~30μs

武器切替時（問題あり - 修正前）:
  Poll()                    [T+0.0ms]  入力収穫
  UpdateInputState()        [T+0.01ms] デルタ確定
  HandleInGameLogic()
    FrameAdvanceTwice() ×2  [T+0.1ms]  NDS 4フレーム実行
      ↳ 再入 RunFrameHook() ×4
        Poll なし! UpdateInputState なし!
        ProcessAimInputMouse() ← 66ms前のデルタを使用
  → 4フレーム全てが stale aim
```

**修正:** 再入パスの先頭に `Poll()` + `UpdateInputState()` を追加。

```cpp
if (UNLIKELY(m_isRunningHook)) {
    // NEW: 再入フレームでも fresh input を取得
    if (m_rawFilter) m_rawFilter->Poll();
    UpdateInputState();
    // ... 以下既存コード
}
```

8000Hz マウスの場合、サブフレーム間（~4ms）に ~32 イベントが到着する。
修正前はこれが全て無視されていた。修正後は各サブフレームで最新のデルタを使用。

**コスト:** サブフレームあたり Poll() 1回（pending ~2-4 メッセージ、数μs）。

---

## フレームパイプライン分析

### レイテンシチェーン全体

```
マウス物理移動
  → USB ポール (125μs @ 8000Hz)
  → OS raw input バッファ
  → Poll() → GetRawInputBuffer (atomic 書込)
  → UpdateInputState() → fetchMouseDelta (atomic 読取)
  → ProcessAimInputMouse() → float計算 → game RAM 書込
  → NDS::RunFrame() → ゲームが aim 読取
  → GPU レンダリング
  → ディスプレイ表示
```

### 制御可能な区間

| 区間 | 現状 | 最適化後 |
|---|---|---|
| Poll〜aim書込（通常） | ~30μs | 変更不要 |
| Poll〜aim書込（武器切替） | ~66ms (stale) | ~4ms/サブフレーム |
| aim書込〜ゲーム読取 | ~0（直前に書込） | 変更不要 |

### マルチスレッド化の評価

**ゲームロジックの並列化: 不可**

- `HandleInGameLogic` 内の全操作（morph, weapon, boost, aim）が NDS mainRAM に読み書きする
- NDS::RunFrame() はスレッドセーフではない
- 操作間に依存関係がある（武器切替: 状態読取 → RAM書込 → RunFrame → 状態読取）
- 並列化しても書込先が同一バッファなので効果がない

**バックグラウンド入力収穫スレッド: 検討の余地あり**

現状（Joy2Key OFF）: Emu スレッドが `Poll()` で同期的に収穫。Poll 間（~16ms）は OS バッファに滞留。

提案アーキテクチャ:
```
[入力スレッド]                    [Emu スレッド]
  hidden window 所有                atomic 読取のみ
  ↓                                ↓
  MsgWait(QS_RAWINPUT)            pollHotkeys()
  → 起床 (~125μs毎)              fetchMouseDelta()
  processRawInputBatched()        → 常に最新データ
  → atomic 書込
  PeekMessage ドレイン
  → スリープ
```

メリット:
- 入力アトミクスが常に最新（~125μs 以内）
- Emu スレッドが重い処理中でも入力が滞留しない
- Poll() 呼び出し不要（Emu スレッドは atomic 読取のみ）

デメリット:
- hidden window の所有スレッド変更が必要
- スレッドライフサイクル管理の複雑化
- **通常フレームでの改善は ~30μs → ~0μs で体感できない可能性が高い**
- Joy2Key ON モードには不要（Qt event filter が既にメインスレッドで動作）

**結論:** 再入 Poll 修正で最大の実用的改善は達成済み。バックグラウンドスレッドは、シェーダコンパイル中のカクつき等が問題になった場合に再検討。

---

## 適用しなかった / 取り消した変更（ゲームロジック層）

### ❌ `ApplyAimAdjustBranchless` max+copysign 変換（リバート済み）

**当初の意図:** 軸あたり chained ternary（2分岐）→ `std::max(MAXSS)` + `std::copysign(ANDPS/ORPS)` で1分岐に削減。

**実際の問題:**

元のコード:
```cpp
dx = (absX < a) ? 0.0f : (absX < 1.0f) ? std::copysign(1.0f, dx) : dx;
```

エイムを速く振る（ホットケース）時、`abs(delta) >> 1.0` なので:
- `absX < a` → false（予測的中）
- `absX < 1.0` → false（予測的中）
- **dx をそのまま返す。計算ゼロ。**

変更後のコード:
```cpp
return std::copysign(std::max(av, 1.0f), val);
```

同じホットケースで:
- `av < a` → false（予測的中）
- `std::max(av, 1.0f)` → MAXSS 常に実行（結果は av のまま）
- `std::copysign(...)` → ANDPS+ORPS 常に実行（結果は val のまま）
- **同じ値を返すのに、毎回2命令余計に走る。**

フレームあたり2回（X,Y）×毎フレーム×8000Hz マウスの高頻度デルタで体感に出た。

**教訓: 「分岐数を減らす ≠ 常に速い」**

分岐予測が安定的に的中するホットパスでは、分岐コスト ≈ 0。
分岐を減らすために追加した ALU 命令は、ホットケースで**無駄な計算**になる。
最適化の判断は「最頻ケースで何が実行されるか」を基準にすべき。

---

## 環境メモ

- マウス: 8000Hz ポーリングレート
- 60fps 想定時: ~133 WM_INPUT メッセージ/フレーム（マウスのみ）
- コンパイラ: MSVC / MinGW（両対応）
- `NEXTRAWINPUTBLOCK` マクロ使用のため MinGW では `QWORD` typedef が必要
