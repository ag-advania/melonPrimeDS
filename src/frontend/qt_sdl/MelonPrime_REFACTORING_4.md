# MelonPrime パフォーマンス最適化 Round 4

## 概要

Round 1-3 で既に高度に最適化されたコードベース (OPT A-Z5, R1-R2) に対し、残存する **メモリオーダリングの正確性問題・ホットパス微細最適化・コールドパス改善** を提案。

既存の最適化水準が非常に高いため、ホットパスの改善幅は **~5-20 cyc/frame** 程度。
ただし **P-1 は正確性バグ**であり、ARM/RISC-V では観測可能な不整合を引き起こす可能性がある。

---

## P-1: [BUG] `snapshotInputFrame` メモリオーダリング不整合 (正確性修正)

**ファイル:** `MelonPrimeRawInputState.cpp` L324-343

**問題:** マウスアキュムレータの `load(relaxed)` が `takeSnapshot()` 内の `acquire` フェンス **より前** にシーケンスされている。

```cpp
// 現行コード:
void InputState::snapshotInputFrame(...) noexcept
{
    // これらの relaxed load は acquire フェンスの「前」に配置されている
    const int64_t curX = m_accumMouseX.load(std::memory_order_relaxed);  // ← フェンス前
    const int64_t curY = m_accumMouseY.load(std::memory_order_relaxed);  // ← フェンス前
    const auto snap = takeSnapshot();  // ← acquire フェンスはここの中
    // ...
}
```

コメントでは "The acquire fence inside takeSnapshot() covers these loads as well, since they are sequenced before it returns" とあるが、これは **不正確**。

C++ メモリモデルにおいて、`atomic_thread_fence(acquire)` はフェンス **以降** の非アトミック読み書きに対して、フェンス前の `relaxed` load が観測した store と同じスレッドの先行 store を可視にする。しかし、フェンス **より前** にシーケンスされた relaxed load 自体にオーダリング保証を遡及適用することはない。

**x86 影響:** x86 の TSO モデルでは relaxed load = 通常 `MOV` であり、load-load リオーダリングが発生しないため **x86 では実質問題なし**。

**ARM/RISC-V 影響:** weak memory model では load-load リオーダリングが発生しうる。マウスデルタの load が VK スナップショットの load より後に実行される可能性があり、不整合なスナップショットとなる。

**修正案:**

```cpp
void InputState::snapshotInputFrame(FrameHotkeyState& outHk,
    int& outMouseX, int& outMouseY) noexcept
{
    // Option A: takeSnapshot() の後にマウスを読む (シンプル)
    const auto snap = takeSnapshot();  // acquire フェンス内蔵
    // フェンス後の load は全て保護される
    const int64_t curX = m_accumMouseX.load(std::memory_order_relaxed);
    const int64_t curY = m_accumMouseY.load(std::memory_order_relaxed);

    outMouseX = static_cast<int>(curX - m_lastReadMouseX);
    outMouseY = static_cast<int>(curY - m_lastReadMouseY);
    m_lastReadMouseX = curX;
    m_lastReadMouseY = curY;

    const uint64_t newDown = scanBoundHotkeys(snap);
    outHk.down = newDown;
    outHk.pressed = newDown & ~m_hkPrev;
    m_hkPrev = newDown;
}
```

**Option B (よりタイト):** マウス load を `takeSnapshot()` 内に統合:

```cpp
struct InputSnapshot {
    uint64_t vk[4];
    uint8_t  mouse;
    int64_t  accumX;
    int64_t  accumY;
};

[[nodiscard]] FORCE_INLINE InputSnapshot takeFullSnapshot() const noexcept {
    InputSnapshot snap;
    for (int i = 0; i < 4; ++i)
        snap.vk[i] = m_vkDown[i].load(std::memory_order_relaxed);
    snap.mouse = m_mouseButtons.load(std::memory_order_relaxed);
    snap.accumX = m_accumMouseX.load(std::memory_order_relaxed);
    snap.accumY = m_accumMouseY.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    return snap;
}
```

Option A を推奨 (最小変更、x86 で同一 codegen)。

**カテゴリ:** 正確性修正 (クロスプラットフォーム)
**x86 影響:** ゼロ (TSO により既に正しく動作)
**ARM 影響:** 不整合スナップショット防止

---

## P-2: `HandleInGameLogic` / `ProcessAimInputMouse` 二重プリフェッチ除去 (~2-4 cyc/frame)

**ファイル:** `MelonPrimeInGame.cpp` L18-24, `MelonPrimeGameInput.cpp` L215-216

**問題:** `HandleInGameLogic()` の冒頭で `aimX` / `aimY` を `PREFETCH_WRITE` した後、`ProcessAimInputMouse()` 内で再度 `PREFETCH_WRITE` している。

```cpp
// HandleInGameLogic (L21-23):
if (LIKELY(!isStylusMode)) {
    PREFETCH_WRITE(m_ptrs.aimX);   // 1回目
    PREFETCH_WRITE(m_ptrs.aimY);   // 1回目
}

// ProcessAimInputMouse (L215-216):
PREFETCH_WRITE(m_ptrs.aimX);       // 2回目 (既にL1に存在)
PREFETCH_WRITE(m_ptrs.aimY);       // 2回目 (既にL1に存在)
```

`HandleInGameLogic` → `ProcessMoveAndButtonsFast` → `HandleMorphBallBoost` → `ProcessAimInputMouse` の間に ~50-100 命令あり、最初のプリフェッチで十分なリードタイムが確保されている。2回目のプリフェッチは既に L1 に存在するキャッシュラインに対する NOP 命令だが、CPU のプリフェッチキューを無駄に消費する。

**修正案:** `ProcessAimInputMouse` 内のプリフェッチを除去:

```cpp
HOT_FUNCTION void MelonPrimeCore::ProcessAimInputMouse()
{
    if (m_aimBlockBits) return;

    if (UNLIKELY(m_isLayoutChangePending)) {
        m_isLayoutChangePending = false;
#ifdef _WIN32
        if (m_rawFilter) m_rawFilter->discardDeltas();
#endif
        return;
    }

    if (LIKELY(m_flags.test(StateFlags::BIT_LAST_FOCUSED))) {
        const int32_t deltaX = m_input.mouseX;
        const int32_t deltaY = m_input.mouseY;

        // PREFETCH_WRITE(m_ptrs.aimX);  -- 除去: HandleInGameLogic で既にプリフェッチ済み
        // PREFETCH_WRITE(m_ptrs.aimY);  -- 除去: HandleInGameLogic で既にプリフェッチ済み
        // ...
```

**ただし:** 再入パス (RunFrameHook re-entrant) では `HandleInGameLogic` を経由しないため、再入パスでのプリフェッチが失われる。再入パスは武器切替/モーフ時のみ発生し、その際はユーザーが既にマウスを動かしている = `aimX/aimY` は直近の書き込みで L1 に残存する可能性が高い。

**リスク:** 再入パスで L2 ミス (~10 cyc) が発生する可能性。ただし再入パス自体が `FrameAdvanceTwice` (~数万 cyc) を含むため、影響は無視可能。

**効果:** ホットパスで prefetch 命令 2 個 (各 ~1-2 cyc) を削減。

---

## P-3: `UpdateInputState` panel ポインタチェーン最適化 (~3-5 cyc/frame)

**ファイル:** `MelonPrimeGameInput.cpp` L121-124

**問題:** 毎フレーム `emuInstance->getMainWindow()->panel->getDelta()` のポインタチェーンを辿る。

```cpp
{
    auto* panel = emuInstance->getMainWindow()->panel;
    m_input.wheelDelta = panel ? panel->getDelta() : 0;
}
```

`getMainWindow()` の返り値は実行中に変化しないため、`MelonPrimeCore` にキャッシュ可能。

**修正案:** `m_cachedPanel` メンバーを追加:

```cpp
// MelonPrime.h に追加:
class ScreenPanel;  // forward decl (Screen.h の include を避ける)
ScreenPanel* m_cachedPanel = nullptr;

// OnEmuStart / HandleGameJoinInit で更新:
m_cachedPanel = emuInstance->getMainWindow()->panel;

// UpdateInputState で使用:
m_input.wheelDelta = m_cachedPanel ? m_cachedPanel->getDelta() : 0;
```

**効果:** ポインタチェーン (`emuInstance` → `MainWindow` → `panel`) 3段階の間接参照 → 1段階に削減。

**注意:** `MainWindow` / `panel` がエミュ実行中に再生成される場合、キャッシュの更新タイミングに注意。`OnEmuStart` + `NotifyLayoutChange` で更新すれば十分カバーできる。

---

## P-4: 再入パス軽量化 (~10-20 cyc/再入フレーム)

**ファイル:** `MelonPrime.cpp` L255-268

**問題:** 再入パス (武器切替/モーフ中の `FrameAdvanceOnce` → `RunFrameHook` 再呼び出し) で `UpdateInputState` のフルパスを実行:

```cpp
if (UNLIKELY(m_isRunningHook)) {
    UpdateInputState();           // ← フル PollAndSnapshot + hotkey scan
    ProcessMoveAndButtonsFast();
    // ...
}
```

再入時に実行される `UpdateInputState` の内容:
1. `PollAndSnapshot` (processRawInputBatched + drainPendingMessages + snapshotInputFrame)
2. フォーカスチェック
3. 29 hotkey の down/press スキャン
4. moveIndex 計算
5. wheelDelta 取得

このうち再入時に必要なのは **1 (入力ドレイン + デルタ取得)** と **3 の移動キー部分のみ**。Hotkey のpress検出 (weapon switch, morph) は不要 (既に親フレームで処理済み)。

**修正案:** 再入専用の軽量更新を導入:

```cpp
// MelonPrime.h に追加:
HOT_FUNCTION void UpdateInputStateReentrant();

// MelonPrimeGameInput.cpp:
HOT_FUNCTION void MelonPrimeCore::UpdateInputStateReentrant()
{
#ifdef _WIN32
    auto* const rawFilter = m_rawFilter.get();
    if (rawFilter) {
        FrameHotkeyState hk{};
        rawFilter->PollAndSnapshot(hk, m_input.mouseX, m_input.mouseY);

        // 再入時は移動キーの down 状態のみ更新
        uint64_t down = m_input.down;  // 前フレームの状態を保持
        const auto hkDown = [&](int id) -> bool {
            return hk.isDown(id) || ((emuInstance->joyHotkeyMask >> id) & 1);
        };

        // 移動キーのみ更新 (4ビット)
        down &= ~static_cast<uint64_t>(IB_MOVE_MASK);
        if (hkDown(HK_MetroidMoveForward)) down |= IB_MOVE_F;
        if (hkDown(HK_MetroidMoveBack))    down |= IB_MOVE_B;
        if (hkDown(HK_MetroidMoveLeft))    down |= IB_MOVE_L;
        if (hkDown(HK_MetroidMoveRight))   down |= IB_MOVE_R;

        // Shoot/Jump は再入でも反映が必要
        if (hkDown(HK_MetroidJump))      down |= IB_JUMP;
        else                              down &= ~IB_JUMP;
        if (hkDown(HK_MetroidShootScan)) down |= IB_SHOOT;
        else                              down &= ~IB_SHOOT;

        m_input.down = down;
        m_input.press = 0;  // 再入時は press イベントなし
        m_input.moveIndex = static_cast<uint32_t>((down >> 6) & 0xF);
    }
#endif
}
```

**効果:**
- Hotkey スキャン: 29回 → 6回 (移動4 + Jump + Shoot)
- press 検出: 18個のテンプレート展開 → 0
- wheelDelta 取得: 除去 (再入時は不要)

**リスク:** 再入時に SCAN_VISOR や UI ボタンの press を検出しなくなる。ただし再入は FrameAdvanceTwice の内部で発生するため、ユーザーが1フレーム中に武器切替 + バイザーを同時に押すことは実質不可能。

---

## P-5: MSVC COLD_FUNCTION 改善 (icache ~200-400 byte 削減)

**ファイル:** `MelonPrimeCompilerHints.h` L40-46

**問題:** MSVC では `COLD_FUNCTION` が空マクロ。GCC/Clang では `.text.unlikely` セクションに配置されるが、MSVC ではホットパスと同一セクションに配置される。

```cpp
#ifndef COLD_FUNCTION
#  if defined(__GNUC__) || defined(__clang__)
#    define COLD_FUNCTION __attribute__((cold))
#  else
#    define COLD_FUNCTION       // ← MSVC: 何もしない
#  endif
#endif
```

**修正案:** MSVC で `__declspec(noinline)` を付与し、少なくともインライン展開を抑制:

```cpp
#ifndef COLD_FUNCTION
#  if defined(__GNUC__) || defined(__clang__)
#    define COLD_FUNCTION __attribute__((cold))
#  elif defined(_MSC_VER)
#    define COLD_FUNCTION __declspec(noinline)
#  else
#    define COLD_FUNCTION
#  endif
#endif
```

**効果:** MSVC でもコールドパス関数 (`HandleRareMorph`, `HandleRareWeaponSwitch`, `HandleGameJoinInit` 等) がホット関数にインライン化されることを防止。OPT-W で実施した `.text.unlikely` 分離の効果を MSVC でも部分的に得られる。

**注意:** MSVC の PGO (Profile-Guided Optimization) を使用している場合、PGO が自動的にコールドブロックを分離するため、この変更の効果は PGO 使用時は限定的。

---

## P-6: `processRawInputBatched` イベントループ型分岐最適化 (~5-10 cyc/frame)

**ファイル:** `MelonPrimeRawInputState.cpp` L154-212

**問題:** 内側ループで毎イベント `dwType` の比較が2回:

```cpp
for (UINT i = 0; i < count; ++i) {
    if (raw->header.dwType == RIM_TYPEMOUSE) {        // 分岐1
        // マウス処理
    }
    else if (raw->header.dwType == RIM_TYPEKEYBOARD) { // 分岐2
        // キーボード処理
    }
    raw = NEXTRAWINPUTBLOCK(raw);
}
```

8000Hz マウスで ~133 イベント/フレームの大半がマウスイベント。キーボードイベントは 0-2 個/フレーム。分岐予測は RIM_TYPEMOUSE 側を学習するため、マウスイベントは well-predicted。

**修正案:** `LIKELY` ヒントの追加:

```cpp
if (LIKELY(raw->header.dwType == RIM_TYPEMOUSE)) {
    // マウス処理
}
else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
    // キーボード処理
}
```

**効果:** GCC/Clang でマウスパスのコードをフォールスルー側に配置。x86 分岐予測器は既にパターンを学習しているため効果は微小 (~1-2 cyc/frame) だが、コードの意図文書化としても有用。

---

## P-7: `HandleGlobalHotkeys` 条件最適化 (~1-2 cyc/frame)

**ファイル:** `MelonPrime.cpp` (HandleGlobalHotkeys の実装)

**問題:** `HandleGlobalHotkeys` は `FORCE_INLINE` だが、毎フレーム呼ばれる。内部でどのホットキーもpress/release されていないフレーム (99%+) でも全チェックが走る。

**修正案:** 早期脱出ゲートを追加 (hk.pressed 全体が 0 なら即座にリターン):

```cpp
FORCE_INLINE void MelonPrimeCore::HandleGlobalHotkeys()
{
    // 99%+ のフレームでグローバルホットキーは押されない
    // press ビット全体をチェックして早期脱出
    if (LIKELY(!m_input.press)) return;  // ← 追加

    // 既存のホットキー処理...
}
```

**注意:** `HandleGlobalHotkeys` の内容を確認する必要がある。`press` だけでなく `down` に依存する処理がある場合はこの最適化は不可。現在のコードでは `HandleGlobalHotkeys` の実装が `MelonPrime.cpp` L202-250 (truncated) にあるため、内容次第。

---

## P-8: `FrameInputState` パディング活用 — `_pad` を廃止して有用データ配置

**ファイル:** `MelonPrime.h` L68-77

**現行:**
```cpp
struct alignas(64) FrameInputState {
    uint64_t down;
    uint64_t press;
    int32_t  mouseX;
    int32_t  mouseY;
    int32_t  wheelDelta;
    uint32_t moveIndex;
    uint32_t _pad[2];  // 64B alignment padding
};
```

**提案:** `_pad[2]` (8 bytes) を活用し、頻繁にアクセスされる値を同一キャッシュラインに配置:

```cpp
struct alignas(64) FrameInputState {
    uint64_t down;
    uint64_t press;
    int32_t  mouseX;
    int32_t  mouseY;
    int32_t  wheelDelta;
    uint32_t moveIndex;
    // _pad を廃止し、ホットデータを詰め込む
    uint16_t inputMaskFast;    // m_inputMaskFast を CL0 に移動
    uint16_t snapState;        // m_snapState を CL0 に移動
    uint32_t aimBlockBits;     // m_aimBlockBits を CL0 に移動
};
```

**効果:** `m_inputMaskFast`, `m_snapState`, `m_aimBlockBits` は毎フレーム R/W されるホットデータ。`m_input` と同一キャッシュラインに配置することで、`ProcessMoveAndButtonsFast()` と `ProcessAimInputMouse()` が `m_input` + これらのフィールドを単一キャッシュラインフェッチで読み書きできる。

**注意:** `m_inputMaskFast` のアクセスパターンが `m_input` の後に連続するか確認が必要。`ProcessMoveAndButtonsFast()` では `m_input.moveIndex` を読んだ直後に `m_inputMaskFast` を読むため、同一 CL に配置する効果は高い。

**リスク:** メンバー変数のレイアウト変更は広範囲に影響。`GetInputMaskFast()` の外部公開 API にも影響しうる。

---

## P-9: `RunFrameHook` フォーカス遷移の `UNLIKELY` 分岐マージ

**ファイル:** `MelonPrime.cpp` L329-342

**問題:** フォーカス遷移チェックがホットパスの終盤にあるが、`if (!isFocused)` ブロック内で `m_rawFilter->resetAllKeys()` + `m_rawFilter->resetMouseButtons()` を呼ぶ。これらは `RawInputWinFilter` の委譲関数であり、呼び出し先で `m_state->resetAllKeys()` → 4 atomic store + fence が走る。

**修正案:** フォーカスアウト時のリセットを 1 回のバッチ呼び出しに統合:

```cpp
// InputState に追加:
void InputState::resetAll() noexcept {
    for (auto& vk : m_vkDown) vk.store(0, std::memory_order_relaxed);
    m_mouseButtons.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    m_hkPrev = 0;
    // resetMouseButtons の内容も統合済み (m_mouseButtons.store は上で実行)
}

// RawInputWinFilter に委譲:
void RawInputWinFilter::resetAll() { m_state->resetAll(); }
```

**効果:**
- `resetAllKeys` + `resetMouseButtons` = fence 2回 → `resetAll` = fence 1回
- 関数呼び出し 2回 → 1回
- ただしフォーカス遷移は稀 (数十秒に1回) のため実測影響はゼロに近い

---

## P-10: `processRawInputBatched` キーボードデルタの `hasKeyChanges` 分岐除去

**ファイル:** `MelonPrimeRawInputState.cpp` L228-237

**現行:**
```cpp
if (hasKeyChanges) {
    for (int i = 0; i < 4; ++i) {
        if (localKeyDeltaDown[i] | localKeyDeltaUp[i]) {
            // atomic store
        }
    }
}
```

**修正案:** `hasKeyChanges` フラグを除去し、内側の `if (localKeyDeltaDown[i] | localKeyDeltaUp[i])` に全てを委ねる:

```cpp
// hasKeyChanges 削除
for (int i = 0; i < 4; ++i) {
    if (localKeyDeltaDown[i] | localKeyDeltaUp[i]) {
        const uint64_t cur = m_vkDown[i].load(std::memory_order_relaxed);
        m_vkDown[i].store(
            (cur | localKeyDeltaDown[i]) & ~localKeyDeltaUp[i],
            std::memory_order_relaxed);
    }
}
```

**効果:** `bool hasKeyChanges` の分岐を除去 (~1 cyc/frame)。`localKeyDeltaDown[i]` は全て 0 初期化されているため、内側の条件で同等の短絡評価が行われる。

**トレードオフ:** キーボードイベントが 0 個のフレーム (99%+) では、現行コードは `hasKeyChanges` = false で 4 回のループ自体をスキップ。修正後は 4 回のゼロチェックが走る (各 ~1 cyc)。

**結論:** キーボードイベントがないフレームで ~3 cyc 増加、キーボードイベントがあるフレームで ~1 cyc 削減。**現行のほうが良い。適用しない。**

---

## 適用結果サマリー

| ID | 種別 | 影響 | 状態 |
|----|------|------|------|
| **P-1** | **BUG (正確性)** | ARM/RISC-V で不整合スナップショット | **✅ 適用済み** |
| ~~P-2~~ | パフォーマンス | ~2-4 cyc/frame | **❌ リバート (性能劣化)** |
| P-3 | パフォーマンス | ~3-5 cyc/frame | **✅ 適用済み** |
| P-4 | パフォーマンス | ~10-20 cyc/再入フレーム | ❌ 見送り (コード複雑化 vs 効果) |
| P-5 | コード品質 | MSVC icache ~200-400 byte | **✅ 適用済み** |
| P-6 | パフォーマンス | ~1-2 cyc/frame | **✅ 適用済み** |
| P-7 | パフォーマンス | ~1-2 cyc/frame | ❌ 不要 (既に早期脱出あり) |
| P-8 | パフォーマンス | ~0-5 cyc/frame | ❌ 見送り (レイアウト変更リスク) |
| P-9 | パフォーマンス | ~0 cyc (コールドパス) | **✅ 適用済み** |
| ~~P-10~~ | ~~パフォーマンス~~ | ~~却下~~ | ❌ **適用しない** |

---

## 適用しなかった最適化 (検討・却下)

### ❌ P-2: `ProcessAimInputMouse` 二重プリフェッチ除去 (リバート済み)

**当初の意図:** `HandleInGameLogic()` L21-23 で `PREFETCH_WRITE(m_ptrs.aimX/aimY)` を発行した後、`ProcessAimInputMouse()` L215-216 で再度同じプリフェッチが走る。~50-100 命令のリードタイムで十分カバーできると判断し、2回目を除去。

**実際の問題:** 2つのプリフェッチは **異なる目的** を持っていた:

```
HandleInGameLogic のプリフェッチ:
  → m_ptrs 構造体のキャッシュライン (ポインタ値自体) をウォームアップ
  → m_ptrs は alignas(64) で CL1 に配置、毎フレーム読まれる
  → ここでは「m_ptrs.aimX というポインタ値」が L1 に載る

ProcessAimInputMouse のプリフェッチ:
  → *m_ptrs.aimX が指す MainRAM 内の実データをウォームアップ
  → MainRAM は 4MB の配列、L1 に常駐しない
  → ここでは「MainRAM[addr] という実エイムデータ」が L1 に載る
```

`PREFETCH_WRITE(m_ptrs.aimX)` は C++ の評価規則により `m_ptrs.aimX` を **まずデリファレンスしてからそのアドレスをプリフェッチ** する。つまり:
1. HandleInGameLogic: `m_ptrs.aimX` (ポインタ) → そのポインタが指す MainRAM アドレスをプリフェッチ
2. ProcessAimInputMouse: 同上だが、間に ~50-100 命令 + `ProcessMoveAndButtonsFast` + `HandleMorphBallBoost` が入るため、最初のプリフェッチで取得した CL が L1 から追い出される可能性がある

4MB MainRAM 内の離散アドレスへのアクセスは L3 ミス (~30-40 cyc) を引き起こしうる。HandleInGameLogic のプリフェッチから実際の `*m_ptrs.aimX = ...` 書き込みまでの距離が大きすぎ、間に挟まる ProcessMoveAndButtonsFast の MoveLUT アクセスや m_inputMaskFast 書き込みが L1 キャッシュラインを競合追い出しする可能性がある。

**教訓: 「同じ式に見えるプリフェッチ ≠ 冗長」**
`PREFETCH_WRITE(ptr)` が ptr のデリファレンス先をプリフェッチする場合、2回のプリフェッチは：
- 1回目: L3→L1 のフェッチを開始
- 2回目: L1 に残っていればNOP、追い出されていれば再フェッチ
後者の「保険」としての役割は、間に他のメモリアクセスが多い場合に重要。特に 4MB MainRAM のような L1 に収まらないワーキングセットでは、近距離のプリフェッチ (書き込みの直前) が不可欠。

### ❌ P-4: 再入パス軽量化 (見送り)

再入パスは武器切替/モーフ時のみ発生 (数秒に1回, 各4-8フレーム)。FrameAdvanceTwice (~数万 cyc) が支配的であり、~10-20 cyc/再入フレームの削減は体感不可。入力ロジックの二重管理によるバグ混入リスクが効果を上回る。

### ❌ P-7: HandleGlobalHotkeys 条件最適化 (不要)

既存コードに `if (LIKELY(!up && !down)) return;` (L234) の早期脱出が実装済み。追加の最適化は不要。

### ❌ P-8: FrameInputState パディング活用 (見送り)

`m_inputMaskFast`, `m_snapState`, `m_aimBlockBits` を `FrameInputState` に移動する案。効果は ~0-5 cyc/frame だが、構造体のセマンティクス変更、`GetInputMaskFast()` 等の外部 API 影響、`static_assert(sizeof == 64)` の破壊、アクセスパターンの検証コストが改善幅を上回る。

### ❌ P-10: hasKeyChanges 分岐除去 (却下)

上記分析の通り、キーボードイベントがないフレーム (99%+) で 4 回のゼロチェックが追加される。既存の `hasKeyChanges` ガードのほうが効率的。

### ❌ processRawInputBatched ループアンロール

133 イベント/frame のイベントループを手動アンロールする案。ただし `NEXTRAWINPUTBLOCK` の可変長ステップにより、ループ本体のアンロールは困難。コンパイラの自動アンロールに委ねるのが最適。

### ❌ mainRAM プリフェッチ (RunFrameHook 冒頭)

`HandleGameJoinInit` で一度解決された `m_ptrs.*` は MainRAM 内の固定オフセットを指す。MainRAM 自体は 4MB の巨大配列のため L1 に収まらないが、`m_ptrs.*` が指す個々のアドレスは相互に近接 (同一プレイヤー構造体内) するため、最初の `*m_ptrs.inGame` アクセスでキャッシュラインが取得され、後続の `*m_ptrs.isAltForm` 等は同一またはアジャセント CL からの読み出しとなる。プリフェッチの追加は不要。

### ❌ Config::Table ルックアップの高速化

`CfgKey::*` は `const char*` であり、`Config::Table::GetBool/GetInt` は文字列比較を行う可能性がある。ハッシュ or enum ベースのルックアップに変更する案だが、Config アクセスは `OnEmuStart` / `OnEmuUnpause` / `RecalcAimSensitivityCache` 等のコールドパスでのみ発生するため、フレーム単位のパフォーマンスに影響なし。

### ❌ `UpdateInputState` の UnrollCheckDown/Press テンプレート最適化

現行の fold-expression テンプレート展開は既に最適。11個の down チェック + 18個の press チェック = 29 bit テスト。各 ~1 cyc で合計 ~29 cyc。BSF ベースのスパーススキャンに変更しても、大半のフレームで 0-2 個しか hit しないため、BSF のセットアップコスト (~3-5 cyc) を考慮すると改善幅は微小。

---

## 変更ファイル一覧

| ファイル | 適用 |
|---------|------|
| `MelonPrimeRawInputState.cpp` | P-1, P-6, P-9 |
| `MelonPrimeRawInputState.h` | P-9 |
| `MelonPrimeGameInput.cpp` | P-3 |
| `MelonPrime.h` | P-3 |
| `MelonPrime.cpp` | P-3, P-9 |
| `MelonPrimeCompilerHints.h` | P-5 |
| `MelonPrimeRawInputWinFilter.h` | P-9 |
| `MelonPrimeRawInputWinFilter.cpp` | P-9 |

---

## 環境メモ (Round 1 から継承)

- マウス: 8000Hz ポーリングレート
- 60fps 想定時: ~133 WM_INPUT メッセージ/フレーム (マウスのみ)
- コンパイラ: MSVC / MinGW (両対応)
- x86-64 TSO: relaxed load/store = 通常 MOV (P-1 の x86 影響がゼロな理由)
