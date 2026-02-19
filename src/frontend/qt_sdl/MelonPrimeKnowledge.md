# MelonPrime 最適化ナレッジ

## 対象モジュール

- Raw Input 層: `MelonPrimeRawInputState` / `MelonPrimeRawInputWinFilter`
- ゲームロジック層: `MelonPrime.h` / `MelonPrime.cpp` / `MelonPrimeInGame.cpp` / `MelonPrimeGameInput.cpp` / `MelonPrimeGameWeapon.cpp`
- ROM検出: `MelonPrimeGameRomDetect.cpp`

---

## スレッディングモデル

MelonPrime の Raw Input 層は2つのモードを持ち、いずれも **書き込みスレッドは常に1つ** である。

| モード | ライター | リーダー | 入力経路 |
|---|---|---|---|
| Joy2Key ON | Qt メインスレッド | Emu スレッド | `nativeEventFilter` → `processRawInput` |
| Joy2Key OFF | Emu スレッド | Emu スレッド | `Poll` → `processRawInputBatched` |

この Single-Writer 保証が、以下の最適化の正当性の根拠となる。

### ON / OFF モード構造差（入力 stuck 問題の背景）

| 項目 | ON (Joy2Key) | OFF (Joy2Key Off) |
|---|---|---|
| ライタースレッド | Qt メインスレッド | Emu スレッド |
| 入力経路 | `nativeEventFilter` → `processRawInput(HRAWINPUT)` | `Poll()` → `GetRawInputBuffer` (バッチ) |
| WM_INPUT 受信先 | Qt ウィンドウ (flags=0) | 隠しウィンドウ (`RIDEV_INPUTSINK`) |
| イベント読取 | `GetRawInputData` (1件ずつ) | `GetRawInputBuffer` (バッチ一括) |
| 読取タイミング | WM_INPUT 到着即時 | フレーム先頭の `Poll()` で一括 |
| フォーカス喪失時 | WM_INPUT 自体が来なくなる → 安全 | `RIDEV_INPUTSINK` で**受信し続ける** |
| DefWindowProc 耐性 | `nativeEventFilter` が先にデータ消費 → 安全 | `DefWindowProc` でデータ破棄リスクあり |

**ON では key stuck が発生しない理由:**
1. `nativeEventFilter` は `DispatchMessage` の**前**に `GetRawInputData` でデータを消費する。後で `DefWindowProc` が処理しても既にデータは読取済。
2. フォーカス喪失時、Qt ウィンドウには WM_INPUT が届かなくなるため stale データが残らない。

---

## 既知の不具合と修正

### [FIX-1] HiddenWndProc の WM_INPUT データ破棄（根本原因）

**事象:** Joy2Key OFF 時にキーやクリックが押しっぱなしになる。ON では発生しない。

**原因:**
Joy2Key OFF モードの隠しウィンドウの `WndProc` が `DefWindowProcW` に全メッセージを委譲していた。
Windows の仕様上、`DefWindowProc` は `WM_INPUT` を受けると内部で `DefRawInputProc` を呼び出し、
カーネルの raw input バッファからデータを**破棄**する。

通常は `Poll()` が `GetRawInputBuffer` で先に読み取り、`PeekMessage(PM_REMOVE)` で除去するため
`DefWindowProc` には到達しない。しかし `Poll()` 間に EmuThread 上の Win32 API が
暗黙のメッセージポンプを実行すると、隠しウィンドウ宛の WM_INPUT がディスパッチされる:

```
[メッセージポンプを実行しうる API]
  wglMakeCurrent / SwapBuffers（一部GPUドライバ）
  SDL 内部の COM 呼出し（DirectInput / XInput）
  SDL_Delay → Sleep 中の SentMessage 処理
```

→ `HiddenWndProc` → `DefWindowProcW` → `DefRawInputProc` → データ破棄
→ 次の `GetRawInputBuffer` で key-up が欠損 → stuck

**修正 (`MelonPrimeRawInputWinFilter.cpp`):**

```cpp
LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(...) {
    if (msg == WM_INPUT) return 0;   // ← 追加
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
```

WM_INPUT のみ自前で消費（return 0）し、`DefRawInputProc` によるデータ破棄を遮断。
`Poll()` の `GetRawInputBuffer` が唯一の読取経路として機能し続ける。

`Poll()` 内の PeekMessage ドレインは引き続き必要（キュー溢れ防止）。ただし FIX-1 により、
仮にドレイン前にメッセージがディスパッチされても raw input データは安全。

### [FIX-2] UpdateInputState の !isFocused 時 stale 入力

**事象:** サブフレーム進行中にフォーカスが外れると、キーマスクが stale のまま NDS に渡る。

**原因:**
`UpdateInputState()` は `!isFocused` 時に即 `return` していた。
しかし `m_input.down` が前フレームの値を保持したまま残るため、
再入パス (`m_isRunningHook==true`) で `~m_input.down` が stale なキーマスクを生成 →
`SetKeyMask` に渡される。

**修正 (`MelonPrimeGameInput.cpp`):**

```cpp
if (!isFocused) {
    m_input.down = 0;
    m_input.press = 0;
    m_input.moveIndex = 0;
    m_input.mouseX = 0;
    m_input.mouseY = 0;
    m_input.wheelDelta = 0;
    return;
}
```

### [FIX-3] フォーカス遷移時の raw input リセット

**事象:** フォーカス喪失→復帰間に stale キーが残りうる。

**原因:**
`BIT_LAST_FOCUSED` 遷移検出時に `m_input` と raw input 状態をクリアしていなかったため、
unfocus → refocus 間に残った stale データが次フレームに影響する。

**修正 (`MelonPrime.cpp` - `RunFrameHook`):**

```cpp
if (UNLIKELY(m_flags.test(StateFlags::BIT_LAST_FOCUSED) != isFocused)) {
    m_flags.assign(StateFlags::BIT_LAST_FOCUSED, isFocused);
    if (!isFocused) {
        m_input.down = 0;
        m_input.press = 0;
        m_input.moveIndex = 0;
        if (m_rawFilter) {
            m_rawFilter->resetAllKeys();
            m_rawFilter->resetMouseButtons();
        }
    }
}
```

FIX-2 と FIX-3 は多重防御の関係:
- FIX-2: 再入パスの `UpdateInputState` 内で毎フレーム即座にクリア
- FIX-3: メインパスのフレーム末尾で遷移を検出し、raw input 層含め包括クリア

### リファクタリングで発見: OnEmuUnpause のリセットは既に十分

初回分析で「`OnEmuUnpause()` に `resetAllKeys + resetMouseButtons` が不足」と判断し追加したが、
リファクタリング時に `ApplyJoy2KeySupportAndQtFilter(enable, doReset=true)` が
OnEmuUnpause の先頭で呼ばれていることを確認。`doReset=true`（デフォルト）により
`resetAllKeys + resetMouseButtons + resetHotkeyEdges` は**既に実行済み**だった。

追加の `#ifdef _WIN32` ブロック内で必要なのは:
1. `BindMetroidHotkeysFromConfig()` — VK バインドの再読込
2. `resetHotkeyEdges()` — バインド変更後のエッジ状態再同期

冗長な `resetAllKeys + resetMouseButtons` は除去した。

---

## 適用した最適化一覧

### 総合サイクル見積もり (A-U)

| OPT | 対象 | 削減/frame | カテゴリ |
|-----|------|-----------|---------|
| A | wheelDelta 事前フェッチ + 武器ゲート | ~18-28 cyc | ゲームロジック |
| B | Boost ビットガード | ~2-4 cyc | ゲームロジック |
| C | クラスレイアウト最適化 | ~0-10 cyc | キャッシュ |
| D | m_isInGame → BIT_IN_GAME 統合 | ~1 cyc | フラグ統合 |
| E | 再入パス バッチ化 | ~2 cyc/再入 | ゲームロジック |
| F | 整数閾値スキップ | ~15-25 cyc (低感度) | エイム |
| G | m_isAimDisabled → aimBlockBits 統合 | ~1-2 cyc | フラグ統合 |
| H | エイム書込プリフェッチ | ~0-10 cyc | キャッシュ |
| I | setRawInputTarget 毎フレーム除去 | ~7-10 cyc | 入力 |
| J | NDS ポインタキャッシュ | ~3-5 cyc | ポインタ追跡 |
| K | BIT_LAST_FOCUSED 変更ガード | ~1-2 cyc | フラグ |
| L | inGame ポインタ HotPointers 昇格 | ~4-10 cyc | キャッシュ |
| M | m_rawFilter シングルロード | ~4-6 cyc | ポインタ追跡 |
| N | BIT_LAYOUT_PENDING デッドコード除去 | 0 (品質) | クリーンアップ |
| O | 固定小数点エイムパイプライン | ~14 cyc | エイム |
| P | 融合 AimAdjust (O に統合) | (O に含む) | エイム |
| Q | 冗長ゼロチェック除去 | ~1-2 cyc | エイム |
| R | Safety-net processRawInputBatched 除去 | ~500-1000 cyc | RawInput |
| S | pollHotkeys + fetchMouseDelta 融合 | ~8-15 cyc | RawInput |
| T | hasMouseDelta / hasButtonChanges 除去 | ~0-133 cyc | RawInput |
| U | Poll() 内 m_state キャッシュ | ~2-3 cyc | RawInput |
| W | BIT_IN_GAME_INIT ブロック外出し | icache ~300-400 byte 削減 | icache/レジスタ |

---

## Raw Input 層の最適化

### 既存（プロジェクト開始前に適用済み）

#### CAS ループ / locked RMW → relaxed load + release store

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

#### `pollHotkeys` フェンス集約

5回の `acquire` load → 5回の `relaxed` load + `atomic_thread_fence(acquire)` 1回。

x86 では acquire load は `MOV` なので実質同等だが、ARM/RISC-V でフェンス命令を4回削減。
コードの意図（「ここで全ての先行 store を観測する」）も明確になる。

### OPT-R: Safety-net processRawInputBatched 除去（~500-1000 cyc/frame）

`Poll()` の旧 3rd ステップ（PeekMessage ドレイン後の再読み取り）を除去。

**根拠:** ドレイン中（~13-40μs）に到着するイベントは次フレームの Poll() で回収される。
8000Hz マウスでドレイン中に到着するのは最大 ~0.3 イベント（133 中の 0.2%）。
体感影響ゼロ。GetRawInputBuffer のカーネル遷移1回分（syscall overhead）を節約。

### OPT-S: pollHotkeys + fetchMouseDelta 融合（~8-15 cyc/frame）

新メソッド `InputState::snapshotInputFrame()` が hotkey poll と mouse delta fetch を1回の呼び出しで実行。

```
旧: pollHotkeys  → 5 relaxed loads + 1 fence
    fetchMouseDelta → 2 acquire loads (各 relaxed + fence on ARM)
    = 7 atomic loads + 2-3 fences + 2 function calls

新: snapshotInputFrame → 7 relaxed loads + 1 fence
    = 7 atomic loads + 1 fence + 1 function call
```

ARM/RISC-V で fence 1-2回削減。x86 でも関数呼び出し1回 + `m_state` 間接参照1回を削減。

呼び出し元 `UpdateInputState()` で `rawFilter->snapshotInputFrame(hk, m_input.mouseX, m_input.mouseY)` に統合。

### OPT-T: hasMouseDelta / hasButtonChanges 除去（~0-133 cyc/frame）

`processRawInputBatched()` 内部ループの per-event `hasMouseDelta = true` ストアを除去。
133 マウスイベント/frame の場合、最初の1回以降は全て冗長な書き込みだった。

コミットフェーズのガード:
- マウス座標: `if (localAccX)` / `if (localAccY)` — 直接値チェック
- ボタン: `if (finalBtnState != initialBtnState)` — 初期スナップショットとの比較

### OPT-U: Poll() 内 m_state キャッシュ（~2-3 cyc/frame）

`m_state.get()` を1回のローカル変数キャッシュで `unique_ptr` 間接参照を削減。

**注: PeekMessage ドレイン（~40000 cyc/frame @ 8000Hz）は OS API 制約上回避不可。**
WM_INPUT メッセージはキュー溢れ防止のため除去が必須であり、バッチ除去 API は Windows に存在しない。
ただし [FIX-1] により、ドレイン前にメッセージがディスパッチされても raw input データの安全性は保証される。

---

## ゲームロジック層の最適化

### OPT-A: wheelDelta 事前フェッチ + 武器ゲート（~18-28 cyc/frame）

`ProcessWeaponSwitch()` が毎フレーム `emuInstance→getMainWindow()→panel→getDelta()` のポインタチェーンを辿っていた。
`UpdateInputState()` で `m_input.wheelDelta` にプリフェッチし、`ProcessWeaponSwitch()` では `if (LIKELY(!m_input.wheelDelta && !IsAnyPressed(IB_WEAPON_ANY))) return false;` で 99%+ のフレームを即座にスキップ。

### OPT-B: Boost ビットガード（~2-4 cyc/frame）

`HandleMorphBallBoost()` で毎フレーム実行していた `SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, ...)` を、値が変化した時のみ実行するよう UNLIKELY ガード追加。

### OPT-C: クラスレイアウト最適化（~0-10 cyc/frame）

`MelonPrimeCore` のメンバ変数をキャッシュラインアクセス頻度で再配置:
- CL0: `m_input` (R/W every frame, 64B aligned)
- CL1+: `m_ptrs` (R every frame, 64B aligned)
- Hot scalars + `emuInstance` (R/W every frame)
- Warm: `fnAdvance`, `frameAdvanceFunc`, `rawFilter`
- Cold: `m_addrHot`, `m_currentRom` (init-only, end of object)

### OPT-D: m_isInGame → BIT_IN_GAME 統合（~1 cyc/frame）

独立 `bool m_isInGame` を `StateFlags::BIT_IN_GAME` に統合。冗長なストア削除。
`IsInGame()` は `m_flags.test(BIT_IN_GAME)` を直接返す。

### OPT-E: 再入パス バッチ化（~2 cyc/再入フレーム）

再入パスの3個の `InputSetBranchless()` 呼び出し（3 RMW 依存チェーン）を、
単一 RMW + 並列ビット抽出に統合。

### OPT-F: 整数閾値スキップ（~15-25 cyc @ 低感度）

`ProcessAimInputMouse()` でプリコンピュート済み整数閾値チェック。
`|deltaX| < threshold && |deltaY| < threshold` なら float パイプライン全体をスキップ。

閾値は `RecalcAimFixedPoint()` で感度/AimAdjust 変更時にプリコンピュート。
高感度では閾値→1（≡既存ゼロチェック）、低感度では閾値が大きくなり大半のフレームをスキップ。

### OPT-G: m_isAimDisabled → aimBlockBits 統合（~1-2 cyc/frame）

独立 `bool m_isAimDisabled` を `uint32_t m_aimBlockBits` のビットフィールドに統合。
`AIMBLK_CHECK_WEAPON`, `AIMBLK_MORPHBALL_BOOST`, `AIMBLK_CURSOR_MODE`, `AIMBLK_NOT_IN_GAME` の4ビット。
`if (m_aimBlockBits)` で全ブロック条件を1命令でテスト。

### OPT-H: エイム書込プリフェッチ（~0-10 cyc/frame）

`PREFETCH_WRITE(m_ptrs.aimX)` を float 計算の前に発行。
aimX/Y は NDS mainRAM 内（4MB）にあり、武器切替やシェーダコンパイル後に L2/L3 に落ちている可能性がある。
計算の ~15 cyc でプリフェッチが完了する。aimX と aimY は 8 バイト差で同一キャッシュライン。

### OPT-I: setRawInputTarget 毎フレーム除去（~7-10 cyc/frame）

`UpdateInputState()` から `m_rawFilter->setRawInputTarget(m_cachedHwnd)` を除去。
HWND は `ApplyJoy2KeySupportAndQtFilter()`（start/unpause時）で既に同期されており、
毎フレームの関数呼び出し + HWND比較 + null チェック (~8 cyc) は完全に冗長。

### OPT-J: NDS ポインタキャッシュ（~3-5 cyc/frame）

`HandleInGameLogic()` 先頭で `auto* const nds = emuInstance->getNDS()` をキャッシュ。
関数内の複数の `emuInstance->getNDS()→TouchScreen()` 呼び出しでポインタチェーンを排除。
コンパイラは関数境界を越えた CSE ができないため手動キャッシュが必要。

### OPT-K: BIT_LAST_FOCUSED 変更ガード（~1-2 cyc/frame）

`m_flags.assign(BIT_LAST_FOCUSED, isFocused)` を `if (UNLIKELY(...test != isFocused))` でガード。
ゲームプレイ中は isFocused が常に true なので、99.99% のフレームで RMW をスキップ。

### OPT-L: inGame ポインタ HotPointers 昇格（~4-10 cyc/frame）

`m_addrHot.inGame`（コールドゾーン末尾）から `m_ptrs.inGame`（HotPointers CL1+ ホットゾーン）へ昇格。

毎フレームの `Read16(mainRAM, m_addrHot.inGame)` が:
- 旧: コールドゾーンのアドレスロード → mainRAM アクセス（2段間接）
- 新: ホットゾーンのキャッシュ済みポインタ → mainRAM 直接アクセス（1段間接）

`DetectRomAndSetAddresses()` で BIT_ROM_DETECTED 設定直後にポインタ解決。
他の `m_ptrs.*` はプレイヤーポジション依存で後から解決されるが、`inGame` はポジション非依存。

### OPT-M: m_rawFilter シングルロード（~4-6 cyc/frame）

`UpdateInputState()` と `RunFrameHook()` のメイン/再入パスで `m_rawFilter.get()` を
ローカル変数にキャッシュ。3回の `unique_ptr` 間接参照 + null チェック → 1回に削減。

### OPT-N: BIT_LAYOUT_PENDING デッドコード除去（品質改善）

`StateFlags::BIT_LAYOUT_PENDING` は定義されていたが `test()` されていなかった。
実際のレイアウト変更検出は独立 `bool m_isLayoutChangePending` が担当。
`OnEmuStart()` の `m_flags.packed = BIT_LAYOUT_PENDING` を `m_flags.packed = 0` + `m_isLayoutChangePending = true` に修正。

### OPT-O: 固定小数点エイムパイプライン（~14 cyc/frame）

`ProcessAimInputMouse()` の float パイプラインを Q14 固定小数点に完全置換。

```
旧: CVTSI2SS ×2 + MULSS ×2 + float AimAdjust + CVTTSS2SI ×2 (~29 cyc)
    4回の int↔float ドメインクロッシング

新: IMUL ×2 + integer AimAdjust (CMP) + SAR ×2 (~15 cyc)
    ドメインクロッシングゼロ
```

Q14 精度（小数部 14bit = 16384 分の 1 ≈ 0.006%）は AimAdjust のデッドゾーン/スナップ判定に十分。

プリコンピュート値（`RecalcAimFixedPoint()` で config 変更時に計算）:
- `m_aimFixedScaleX`: sensiFactor × 2^14
- `m_aimFixedScaleY`: combinedY × 2^14
- `m_aimFixedAdjust`: aimAdjust × 2^14（0 = disabled）
- `m_aimFixedSnapThresh`: AIM_ONE_FP（adjust ON 時）/ 0（OFF 時）

### OPT-P: 融合 AimAdjust（OPT-O に統合）

AimAdjust の3分岐（デッドゾーン / snap-to-±1 / パススルー）を Q14 空間で直接実行。
`m_aimFixedAdjust` と `m_aimFixedSnapThresh` が config 変更時にプリコンピュート済み。
AimAdjust 無効時は両閾値 = 0 で比較が自動的にフォールスルー（専用分岐不要）。

ホットケース（速いエイム移動）: |scaled| >> snap threshold → 2 predicted-false 分岐 + SAR。
旧 float ternary と同じ分岐パターンだが、CVTSI2SS/MULSS/CVTTSS2SI のオーバーヘッドがない。

### OPT-Q: 冗長ゼロチェック除去（~1-2 cyc/frame）

`(deltaX | deltaY) == 0` チェックは OPT-F の閾値チェック（閾値 ≥ 1）に完全包含。1分岐削減。

### OPT-W: BIT_IN_GAME_INIT ブロック外出し（icache ~300-400 byte 削減）

`RunFrameHook` (HOT_FUNCTION) 内にインラインされていたゲーム参加時の初期化ブロック
（~50行: アドレス計算 + ポインタ解決 + 設定適用）を `HandleGameJoinInit` (COLD_FUNCTION) に抽出。

**問題:**
```
RunFrameHook (HOT_FUNCTION) の機械語サイズ:
  ホットパス:  ~200-300 byte (毎フレーム)
  initブロック: ~300-400 byte (ゲーム参加時のみ = 数十秒に1回)
  合計:         ~500-700 byte → L1i 32KB の ~1.5-2.2%
```

initブロックは数十秒に1回しか実行されないにもかかわらず:
1. RunFrameHook 全体の icache フットプリントを膨張させる
2. コンパイラがホットパスのレジスタ割当に initブロックのローカル変数（offP, offA 等）を考慮する
3. 武器切替時の FrameAdvanceTwice → 再帰 RunFrameHook 呼出しで icache miss 確率が上がる

**修正:**
```cpp
// 旧: RunFrameHook 内にインライン
if (isInGame && !m_flags.test(BIT_IN_GAME_INIT)) {
    m_flags.set(BIT_IN_GAME_INIT);
    // ~50行のアドレス計算 + ポインタ解決 + 設定適用
}

// 新: COLD_FUNCTION に抽出
if (isInGame && !m_flags.test(BIT_IN_GAME_INIT)) {
    HandleGameJoinInit(mainRAM);  // COLD_FUNCTION → .text.unlikely
}
```

COLD_FUNCTION の効果:
1. `.text.unlikely` セクションに配置 → ホットパスの icache 占有を削減
2. コンパイラが RunFrameHook のレジスタ割当を initブロック無しで最適化
3. NOINLINE が暗黙適用 → コンパイラが再インライン化しない

### 既存（プロジェクト開始前）: `setRawInputTarget` HWND ガード

`setRawInputTarget()` 先頭に `if (m_hwndQtTarget == hwnd) return;` を追加。
OPT-I で UpdateInputState からの呼び出し自体を除去したが、ガードは他の呼び出し元のために維持。

### 既存: `HandleAdventureMode` UI ボタン早期脱出

UI ボタン5個の `IsPressed` を `IB_UI_ANY` 合成マスクで1回のビットテストに集約。

### 既存: WeaponData 重複 LUT 統合

`ID_TO_ORDER_INDEX[]`（C配列）と `ID_TO_ORDERED_IDX`（`std::array`）を統合。

### 既存: 再入パスへの Fresh Poll 追加（エイムレイテンシ修正）

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

8000Hz マウスの場合、サブフレーム間（~4ms）に ~32 イベントが到着する。
修正前はこれが全て無視されていた。修正後は各サブフレームで最新のデルタを使用。

---

## フレームパイプライン分析

### レイテンシチェーン全体

```
マウス物理移動
  → USB ポール (125μs @ 8000Hz)
  → OS raw input バッファ
  → Poll() → GetRawInputBuffer (atomic 書込)
  → UpdateInputState() → snapshotInputFrame (atomic 読取)
  → ProcessAimInputMouse() → Q14固定小数点計算 → game RAM 書込
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

### Raw Input ホットパスコスト (Joy2Key OFF, 8000Hz, 60fps)

```
GetRawInputBuffer (main):         ~1000-3000 cyc  (syscall)
Event processing loop (133×):     ~931 cyc  (133 × ~7 cyc)
PeekMessage drain (133×):         ~39900 cyc (133 × ~300 cyc via NtPeek)  ← 回避不可
Commit phase:                     ~20-50 cyc
snapshotInputFrame:               ~250-400 cyc (29 hotkeys × scan+test + mouse delta)
TOTAL:                            ~42000-44000 cyc/frame
```

PeekMessage ドレインが ~90% を占めるが、WM_INPUT のキュー溢れ防止のため除去不可。
バッチ除去 API は Windows に存在しない。
[FIX-1] によりドレイン前のデータ安全性は保証されるが、キュー溢れ防止は別の問題。

### マルチスレッド化の評価

**ゲームロジックの並列化: 不可**

- `HandleInGameLogic` 内の全操作（morph, weapon, boost, aim）が NDS mainRAM に読み書き
- NDS::RunFrame() はスレッドセーフではない
- 操作間に依存関係がある

**バックグラウンド入力収穫スレッド: 検討の余地あり**

```
[入力スレッド]                    [Emu スレッド]
  hidden window 所有                atomic 読取のみ
  ↓                                ↓
  MsgWait(QS_RAWINPUT)            snapshotInputFrame()
  → 起床 (~125μs毎)              → 常に最新データ
  processRawInputBatched()
  → atomic 書込
  PeekMessage ドレイン
  → スリープ
```

**結論:** 再入 Poll 修正で最大の実用的改善は達成済み。バックグラウンドスレッドは、シェーダコンパイル中のカクつき等が問題になった場合に再検討。

---

## 適用しなかった / 取り消した変更

### ❌ `static thread_local` バッファ → スタックバッファ（リバート済み）

**当初の意図:** TLS ガードチェック (~5-10 cyc) の排除。

**実際の問題:**
- POD 配列の `static thread_local` は MSVC で `__readgsqword` 一発。ガードチェックは発生しない
- 16KB のスタック確保は MSVC で `__chkstk` を誘発（4KB ページ境界ごとにプローブ → 4回）
- `alignas(64)` + 16KB でレジスタ圧迫が発生し、内側のイベント処理ループの最適化が悪化

**教訓:** `static thread_local` の POD 配列は十分高速。大きなバッファをスタックに持ってくるのは `__chkstk` とレジスタ圧迫でマイナス。

### ❌ `Poll()` ドレインループの上限設定（不採用）

8000Hz で ~133 メッセージが通常発生。ドレインはデータコピーなし（削除のみ）で高速。上限不要。

### ❌ `ApplyAimAdjustBranchless` max+copysign 変換（リバート済み）

**当初の意図:** 軸あたり chained ternary（2分岐）→ `std::max(MAXSS)` + `std::copysign(ANDPS/ORPS)` で1分岐に削減。

**実際の問題:** ホットケース（速いエイム、`abs(delta) >> 1.0`）で:
- 元コードの2分岐は両方 predicted-false で計算ゼロ（dx をそのまま返す）
- 変更後は MAXSS + ANDPS+ORPS が毎回実行（同じ値を返すのに2命令余計）

**教訓: 「分岐数を減らす ≠ 常に速い」**
分岐予測が安定的に的中するホットパスでは、分岐コスト ≈ 0。
追加ALU命令はホットケースで無駄な計算になる。
※ なお OPT-O で float パイプライン自体が固定小数点に置換されたため、この問題は根本的に解消。

### ❌ OnEmuUnpause への resetAllKeys/resetMouseButtons 追加（冗長、除去済み）

**当初の意図:** ポーズ中に失われた key-up の stale ビットをクリア。

**実際の問題:** `ApplyJoy2KeySupportAndQtFilter(enable, doReset=true)` が OnEmuUnpause の
先頭で呼ばれており、`doReset=true`（デフォルト）により `resetAllKeys + resetMouseButtons +
resetHotkeyEdges` は**既に実行済み**。追加の呼び出しは完全に冗長だった。

**教訓:** 修正を追加する前に、既存のコールチェーンを完全にトレースすべき。
デフォルト引数 (`doReset=true`) が暗黙の動作を隠蔽していた。

### ❌ その他の検討・却下項目

| 項目 | 却下理由 |
|------|---------|
| HandleGlobalHotkeys ゲート | hotkeyReleased ~2 cyc、ゲートのメリット < 複雑化コスト |
| m_rawFilter → BIT フラグ | OPT-M のシングルロードで十分。フラグでもメソッド呼び出しにはポインタ必要 |
| UnrollCheckDown/Press 改善 | 29 bit テスト、各 ~1 cyc。既にテンプレート展開済み |
| ProcessMoveInputFast snap-tap | ブランチレス衝突検出完了。LUT alignas(64) 最適 |
| HandleMorphBallBoost 早期脱出 | 3テスト全て well-predicted、~3-4 cyc。最適 |
| m_isWeaponCheckActive → BIT | IB_WEAPON_CHECK down 時のみチェック（稀）。効果なし |
| InputSetBranchless(INPUT_START) バッチ化 | in-game/non-in-game 分岐が必要。~2 cyc で複雑化不相応 |
| FrameAdvanceDefault usesOpenGL() キャッシュ | コールドパスのみ（武器切替/モーフ） |
| wheelDelta 再入フレームスキップ | 再入は稀（~2-6/event）。~32 cyc/event で効果なし |
| isStylusMode public bool vs BIT | 外部コードが参照。bool テスト = BIT テスト |
| m_isLayoutChangePending → BIT | ProcessAimInputMouse 早期チェック。bool ≡ BIT コスト |
| pollHotkeys BitScanForward 改善 | 29 hotkeys × BSF + testHotkeyMask。既に最適 |
| processRawInputBatched dwType 分岐分離 | マウスイベント優位。分離はキャッシュ局所性悪化 |
| mainRAM プリフェッチ | 効果不確定（0-10 cyc）。再入チェックからの距離で十分な可能性 |
| panel ポインタキャッシュ | getDelta() は消費操作、毎フレーム呼び出し必須 |

---

## 環境メモ

- マウス: 8000Hz ポーリングレート
- 60fps 想定時: ~133 WM_INPUT メッセージ/フレーム（マウスのみ）
- コンパイラ: MSVC / MinGW（両対応）
- `NEXTRAWINPUTBLOCK` マクロ使用のため MinGW では `QWORD` typedef が必要
- Q14 固定小数点: `int64_t` 乗算使用。x86-64 では IMUL r64 と IMUL r32 は同一レイテンシ (~3 cyc)

---

## 変更ファイル一覧

| ファイル | 適用 OPT / FIX |
|---------|---------|
| MelonPrime.h | A, C, D, F, G, L, M, N, O, P, W |
| MelonPrime.cpp | D, E, F, K, L, M, N, O, W, FIX-3 |
| MelonPrimeInGame.cpp | A, B, G, H, J |
| MelonPrimeGameInput.cpp | A, F, G, I, M, O, P, Q, S, FIX-2 |
| MelonPrimeGameWeapon.cpp | A |
| MelonPrimeGameRomDetect.cpp | L |
| MelonPrimeRawInputState.h | S |
| MelonPrimeRawInputState.cpp | S, T |
| MelonPrimeRawInputWinFilter.h | S |
| MelonPrimeRawInputWinFilter.cpp | R, U, S, FIX-1 |
