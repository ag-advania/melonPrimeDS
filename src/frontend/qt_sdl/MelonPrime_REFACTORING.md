# MelonPrime リファクタリング統合ドキュメント

**統合対象:** `MelonPrime_REFACTORING_1.md` ～ `MelonPrime_REFACTORING_5.md` + Round 6  
**文書種別:** 最終統合版 / 参照用正本  
**推奨ファイル名:** `MelonPrime_REFACTORING_UNIFIED_FINAL.md`  
**旧統合版名:** `MelonPrime_REFACTORING_UNIFIED.md`, `MelonPrime_REFACTORING_UNIFIED_1_TO_5.md`, `MelonPrime_REFACTORING_UNIFIED_1_TO_5_aliases_v2.md`, `MelonPrime_REFACTORING_UNIFIED_1_TO_5_aliases_v3_audited.md`  
**位置づけ:** 1～5 の内容、旧名対応、情報源監査をすべて反映した最終版。Round 6 を追記。ソースコメントや作業メモから逆引きできることを重視する。

**統合目的:** 1～5 に分散していた知見、適用済み最適化、正確性修正、リファクタリング、入力遅延最適化、却下・撤回事項を、**重複を整理したうえで漏れなく 1 本化**する。  
**この文書の立場:**  
- 1 は「既存知見 + FIX + OPT A～Z5」の土台  
- 2 は **Refactoring Round 1 (R1)**  
- 3 は **Refactoring Round 2 (R2)**  
- 4 は **Round 4: P-1～P-10 の提案・採否**  
- 5 は **Round 5: P-11～P-27 の入力遅延 / フレームペーシング最適化**  
- 6 は **Round 6: P-33～P-37 の syscall 削減 / ホットパス改善**  
- 7 は **Round 7: P-38～P-41 のフレームループ最適化**  
- 重複する項目は統合し、**履歴として重要な「一度採用されたが後で代替・撤回されたもの」も残す**

---

## 1. ソース対応表

| 統合元 | 位置づけ | 主な内容 |
|---|---|---|
| `MelonPrime_REFACTORING_1.md` | 基礎ナレッジ / 初期最適化 | スレッディングモデル、FIX-1～3、OPT A～Z5、パイプライン分析、却下項目 |
| `MelonPrime_REFACTORING_2.md` | R1 | 保守性改善、重複排除、SmallVkList、static→member、constexpr テーブル、UTF-8 修正 |
| `MelonPrime_REFACTORING_3.md` | R2 | ボタン優先度バグ修正、fetch_add、setHotkeyVks pointer+count、LUT 化、hasMask 廃止、ヒープ排除完全化 |
| `MelonPrime_REFACTORING_4.md` | Round 4 | P-1～P-10 の詳細分析、適用済み / 却下 / リバートの整理 |
| `MelonPrime_REFACTORING_5.md` | Round 5 | P-11～P-27 の入力遅延最適化、フレームペーシング改善、最終レイテンシ観点の整理 |
| `MelonPrime_PERF_ROUND6.md` | Round 6 | P-33～P-37: PrePollRawInput 除去、SDL スキップ、DeferredDrain 軽量化、ホットパス分岐改善 |
| (本文書 §16) | Round 7 | P-38～P-41: フレームループ内 hotkey ゲート、NeedsShaderCompile キャッシュ、除算→乗算、DSi sync 除去 |

---

## 2. 正規化ルール

### 2.1 ラウンド名の統一

- `MelonPrime_REFACTORING_2.md` を **R1**
- `MelonPrime_REFACTORING_3.md` を **R2**
- `MelonPrime_REFACTORING_4.md` を **Round 4**
- `MelonPrime_REFACTORING_5.md` を **Round 5**
- `MelonPrime_PERF_ROUND6.md` を **Round 6**
- (本文書 §16) を **Round 7**

として扱う。

### 2.2 重複 ID の統一

`MelonPrime_REFACTORING_5.md` には同一 ID の重複記述があるため、本統合文書では以下のように正規化する。

| 元の表記 | 本文書での扱い |
|---|---|
| P-16 が 2 回出現 | **P-16: FastForward/SlowMo 後の VSync 復帰修正** に統合 |
| P-17 が複数回出現 | **P-17: サブピクセル残差蓄積** に統合 |
| P-26 が 2 種類出現 | **P-26a: Auto Screen Layout バイパス** と **P-26b: DeferredDrain スロットル** に分離 |
| P-14 が適用済み扱い / P-19・P-20 で代替 | **P-14 は歴史的段階**, 最終的には P-19 と P-20 に吸収 |

### 2.3 履歴の扱い

- **適用済み**: 現実装の到達点として扱う
- **見送り / 却下**: 検討結果として保持する
- **リバート済み**: 一度試したが戻したものとして保持する
- **歴史的段階**: 後続最適化の踏み台になったものとして別扱いする

### 2.4 旧名 / 別名の保持ルール

この統合版では、**統一名だけでなく元ドキュメントの見出し名も追えること**を重視する。
特にソースコメントや作業メモで旧名が使われていても逆引きできるよう、以下の規則で併記する。

- 見出しを統一した項目は、本文中に **旧名 / 別名 / 元見出し** を残す
- Round 5 で重複していた ID は、**正規化名 + 元の呼び方** を併記する
- ソースコメントで旧名が出てきたときは、まずこの節と各ステータス表の **旧名 / 元見出し** 列を参照する

### 2.5 正規化名 ↔ 旧名 早見表

| 正規化名 | 旧名 / 別名 / 元見出し |
|---|---|
| P-14: PrePoll バッチドレイン | PrePollRawInput, PrePoll バッチドレイン (Stuck Keys 修正) |
| P-15 + P-21: ジョイスティック状態更新の分離 | Late-Poll ジョイスティック, `inputRefreshJoystickState()`, `inputProcess` 分離 |
| P-16: VSync 復帰修正 | VSync 設定復元, VSync 復帰バグ修正 |
| P-17: サブピクセル残差蓄積 | サブピクセルアキュムレータ, サブピクセルエイム蓄積, サブピクセル残差アキュムレータ |
| P-18: Dual-Path エイムパイプライン | Direct Path / Legacy Path, P-18c 残差クランプ |
| P-19: HiddenWndProc 即時 `processRawInput` | Stuck keys 根本修正, HiddenWndProc processRawInput |
| P-20: PrePollRawInput 除去 | P-20b `InputReset` バグ修正, P-20c P-3 キャッシュ適用漏れ修正 |
| P-22: DeferredDrain 分離 | `DeferredDrainInput()` 呼び出し追加, PollAndSnapshot から drain 分離 |
| P-24: 外部ループ hotkey 一括早期脱出 | 外部ループ ホットキー一括早期脱出 |
| P-26a: Auto Screen Layout バイパス | Auto Screen Layout バイパス |
| P-26b: DeferredDrain スロットル | DeferredDrain スロットル |

---


### 2.6 情報源監査の反映方針

この版では、**元ドキュメントだけでなく現コードも情報源として監査**し、食い違いがある箇所は次の優先順位で扱う。

1. **現コード**  
   実際に存在する関数、呼び出し位置、コメントを最優先とする。
2. **Round 5 原文**  
   提案時点・適用時点の意図を示す一次資料として扱う。
3. **既存 UNIFIED / 本統合版の旧版**  
   整理結果として参照するが、コードと矛盾した場合は修正対象とする。

特に以下は、**文書上の到達点** と **現コード残置 / 後続変更** を分けて読む。

| 項目 | 監査結果 |
|---|---|
| P-14 / `PrePollRawInput` | Round 5 では P-19 / P-20 に吸収。**Round 6 (P-33) で空インライン化・呼び出し除去により完了** |
| P-20 / PrePollRawInput 除去 | **Round 6 (P-33) により完了**。設計どおり PrePollRawInput は実質削除された |
| P-22 / DeferredDrain 分離 | 分離自体は有効。配置は `drawScreen()` 後。**P-35 はリバート済み — drainPendingMessages を維持 (shared-buffer セーフティネット)** |
| P-26b / DeferredDrain スロットル | **撤回済み / 現コード未採用**。ただし `RawInputWinFilter.cpp` のコメントに旧注釈が残っている |
| P-32 | 1～5 の範囲外だが、**DeferredDrain の最終配置を説明する現コード上の後続変更**として参照が必要 |


## 3. アーキテクチャ概要

## 3.1 対象モジュール

- Raw Input 層: `MelonPrimeRawInputState` / `MelonPrimeRawInputWinFilter`
- ゲームロジック層: `MelonPrime.h` / `MelonPrime.cpp` / `MelonPrimeInGame.cpp` / `MelonPrimeGameInput.cpp` / `MelonPrimeGameWeapon.cpp`
- ROM 検出層: `MelonPrimeGameRomDetect.cpp`

## 3.2 スレッディングモデル

MelonPrime の Raw Input 層は 2 モードを持ち、どちらも **書き込みスレッドは常に 1 つ** である。

| モード | ライター | リーダー | 入力経路 |
|---|---|---|---|
| Joy2Key ON | Qt メインスレッド | Emu スレッド | `nativeEventFilter` → `processRawInput` |
| Joy2Key OFF | Emu スレッド | Emu スレッド | `Poll` → `processRawInputBatched` |

この **Single-Writer 保証** が、atomic 最適化の根拠である。  
具体的には、CAS ループや locked RMW を避け、`relaxed load + release store` を使う判断の土台になっている。

## 3.3 Joy2Key ON / OFF の構造差

| 項目 | ON (Joy2Key) | OFF (Joy2Key Off) |
|---|---|---|
| ライタースレッド | Qt メインスレッド | Emu スレッド |
| 入力経路 | `nativeEventFilter` → `processRawInput(HRAWINPUT)` | `Poll()` → `GetRawInputBuffer` |
| WM_INPUT 受信先 | Qt ウィンドウ | 隠しウィンドウ (`RIDEV_INPUTSINK`) |
| イベント取得 | `GetRawInputData` 1 件ずつ | `GetRawInputBuffer` でバッチ |
| 読み取りタイミング | 到着即時 | フレーム先頭付近で一括 |
| フォーカス喪失時 | WM_INPUT 自体が来なくなる | `RIDEV_INPUTSINK` により継続受信 |
| DefWindowProc 耐性 | 先にデータ消費されるので安全 | `DefWindowProc` によるデータ消費リスク |

**ON で stuck が起きにくい理由**
1. `nativeEventFilter` が `DispatchMessage` より前に `GetRawInputData` する  
2. フォーカス喪失時に Qt 側へ WM_INPUT が届かなくなる

---

## 4. 既知の不具合と修正

## 4.1 FIX-1: HiddenWndProc の WM_INPUT データ消失 (stuck keys 根本原因)

**事象**  
Joy2Key OFF 時にキーやクリックが押しっぱなしになる。

**原因**  
隠しウィンドウの `WndProc` が `DefWindowProcW` に WM_INPUT を渡すと、内部で `GetRawInputData` が走り、raw input バッファが消費される。  
その後 `GetRawInputBuffer` で拾うべき key-up が失われ、stuck になる。

**重要な仕様**
1. `PeekMessage(PM_REMOVE)` は WM_INPUT をキューから外す  
2. この時点で `GetRawInputBuffer` からは不可視になる  
3. `lParam` の `HRAWINPUT` は `GetRawInputData` でまだ読める  
4. `DefWindowProcW(WM_INPUT)` は内部でそれを消費する

**修正**
- `HiddenWndProc` で `WM_INPUT` を受けた瞬間に `processRawInput(reinterpret_cast<HRAWINPUT>(lParam))`
- `DefWindowProcW` に渡さず `return 0`
- `Poll()` / `drainPendingMessages()` との二重経路でロストを防止

**要点**  
`return 0` だけでは足りず、**WndProc 内で明示的に `GetRawInputData` 相当を実行して救出する必要がある**。

## 4.2 FIX-2: `UpdateInputState()` の !isFocused 時 stale 入力

**事象**  
フォーカス喪失中に stale な `m_input.down` が残り、再入パスで誤ったキーマスクが NDS に渡る。

**修正**
`!isFocused` 時に以下をゼロクリアして return:
- `m_input.down`
- `m_input.press`
- `m_input.moveIndex`
- `m_input.mouseX`
- `m_input.mouseY`
- `m_input.wheelDelta`

## 4.3 FIX-3: フォーカス遷移時の raw input リセット

**事象**  
focus 喪失 → 復帰間に stale キーが残る可能性がある。

**修正**
`BIT_LAST_FOCUSED` の変化を検出したら、フォーカス喪失側で
- `m_input.down/press/moveIndex` のクリア
- `m_rawFilter->resetAllKeys()`
- `m_rawFilter->resetMouseButtons()`

を実行する。

**FIX-2 と FIX-3 の関係**
- FIX-2: 毎フレームのホットパス側で stale を即時遮断
- FIX-3: フォーカス遷移イベントとして raw input 層まで含めて包括クリア

## 4.4 OnEmuUnpause のリセットは既に十分

一見すると `OnEmuUnpause()` に `resetAllKeys + resetMouseButtons` を追加したくなるが、  
実際には `ApplyJoy2KeySupportAndQtFilter(enable, doReset=true)` が先頭で呼ばれており、既に
- `resetAllKeys`
- `resetMouseButtons`
- `resetHotkeyEdges`

が実行済み。  
したがって追加リセットは冗長であり、必要なのは
1. `BindMetroidHotkeysFromConfig()`
2. `resetHotkeyEdges()`

の再同期のみ。

## 4.5 R2: `processRawInputBatched` ボタン優先度バグ修正 (旧名: マウスボタン優先度の不整合)

**問題**  
単一イベント処理は `UP wins`、バッチ処理は `DOWN wins` になっていた。

**修正前**
```cpp
(finalBtnState & ~lut.upBits) | lut.downBits
```

**修正後**
```cpp
(finalBtnState | lut.downBits) & ~lut.upBits
```

同一メッセージに DOWN/UP が同時に立つ結合入力では、最終状態は「離された」が正しいため、`UP wins` に統一する。

## 4.6 P-1: `snapshotInputFrame` のメモリオーダリング不整合 (旧名同じ)

**問題**  
`m_accumMouseX/Y.load(relaxed)` が `takeSnapshot()` 内の acquire fence より前に配置されていた。  
x86 TSO では実質問題化しにくいが、ARM / RISC-V では load-load reorder により VK スナップショットとマウスデルタの整合が崩れる可能性がある。

**修正方針**
- `takeSnapshot()` の後で `m_accumMouseX/Y` を読む  
または
- `takeFullSnapshot()` 的な統合スナップショットを導入する

Round 4 では **Option A: takeSnapshot 後にマウスを読む** の最小変更が推奨された。

## 4.7 P-20b: `InputReset()` による残差破壊バグ (旧名: InputReset バグ修正)

**問題**  
`InputReset()` が毎フレーム `m_aimResidualX/Y = 0` を実行していたため、P-17 のサブピクセル蓄積と P-18 の Dual-Path が事実上無効化されていた。

**修正**
- `InputReset()` から残差リセットを除去
- 残差クリアは、感度変更、レイアウト変更、エイムブロックなどの明示的リセット時のみに限定

## 4.8 P-20c: P-3 キャッシュ適用漏れ修正 (旧名同じ)

P-3 で `m_cachedPanel` を導入したのに、`UpdateInputState()` がまだ `emuInstance->getMainWindow()->panel` を辿っていた。  
これを `m_cachedPanel ? m_cachedPanel->getDelta() : 0` に置き換えることで、P-3 の意図をホットパスへ正しく反映した。

## 4.9 P-22 修正: `DeferredDrainInput()` 呼び出し漏れ (旧名: DeferredDrainInput 呼び出し追加)

P-22 で `PollAndSnapshot` から `drainPendingMessages()` を分離したが、`EmuThread.cpp` の `frameAdvanceOnce()` に `DeferredDrainInput()` 呼び出しが入っていなかった。  
これを RunFrame 直後に追加し、後続の P-26b でさらにスロットル対象にした。

---

## 5. 基礎最適化 (OPT A～Z5)

## 5.1 総合一覧

| OPT | 対象 | 削減 / 効果 | 要旨 |
|---|---|---|---|
| A | wheelDelta 事前フェッチ + 武器ゲート | ~18～28 cyc/frame | wheelDelta を先に取得し、武器入力がない 99%+ フレームで処理をスキップ |
| B | Boost ビットガード | ~2～4 cyc/frame | モーフボール boost 条件の早期ビット判定 |
| C | クラスレイアウト最適化 | ~0～10 cyc/frame | ホットメンバーの配置改善 |
| D | `m_isInGame` → `BIT_IN_GAME` | ~1 cyc/frame | bool からフラグ統合へ |
| E | 再入パス バッチ化 | ~2 cyc/再入 | 再入時の重複処理整理 |
| F | 整数閾値スキップ | ~15～25 cyc/frame | 低感度時の無駄な aim 演算を除外 |
| G | `m_isAimDisabled` → `aimBlockBits` | ~1～2 cyc/frame | 複数条件のブロック管理をビット化 |
| H | エイム書込プリフェッチ | ~0～10 cyc/frame | aimX / aimY 書き込み先のウォームアップ |
| I | `setRawInputTarget` 毎フレーム除去 | ~7～10 cyc/frame | HWND 更新の冗長呼び出しを除去 |
| J | NDS ポインタキャッシュ | ~3～5 cyc/frame | `emuInstance->getNDS()` 等の追跡を削減 |
| K | `BIT_LAST_FOCUSED` 変更ガード | ~1～2 cyc/frame | フォーカス変化検出の無駄を削減 |
| L | `inGame` ポインタ HotPointers 昇格 | ~4～10 cyc/frame | 頻用アドレスのキャッシュ局所性改善 |
| M | `m_rawFilter` シングルロード | ~4～6 cyc/frame | 同一フレーム内のポインタ再読込を減らす |
| N | `BIT_LAYOUT_PENDING` デッドコード除去 | 品質改善 | 不要コード除去 |
| O | 固定小数点エイムパイプライン | ~14 cyc/frame | float 系を Q14 固定小数点へ置換 |
| P | 融合 AimAdjust | O に統合 | 独立項目ではなく O に吸収 |
| Q | 冗長ゼロチェック除去 | ~1～2 cyc/frame | 二重のゼロ判定整理 |
| R | Safety-net `processRawInputBatched` 除去 | ~500～1000 cyc/frame | 冗長な再読込除去 |
| S | `pollHotkeys + fetchMouseDelta` 融合 | ~8～15 cyc/frame | 1 回の snapshot で hotkey + mouse delta を取得 |
| T | `hasMouseDelta / hasButtonChanges` 除去 | ~0～133 cyc/frame | 不要フラグの削減 |
| U | `Poll()` 内 `m_state` キャッシュ | ~2～3 cyc/frame | 間接参照削減 |
| W | `BIT_IN_GAME_INIT` ブロック外出し | icache ~300～400 byte | ホットパスから cold 初期化を退避 |
| Z1 | mainRAM 遅延取得 | ~6～10 cyc/frame | 必要時のみ取得 |
| Z2 | `ProcessMoveAndButtonsFast` 統合 | ~3～5 cyc/frame | move / buttons を 1 本化 |
| Z3 | `PollAndSnapshot` 統合 | ~8～12 cyc/frame | Poll + snapshot を融合 |
| Z4 | `HandleGlobalHotkeys` インライン | ~5 cyc/frame | call / ret 除去 |
| Z5 | aimX/aimY 早期プリフェッチ | 0～40 cyc | L2 miss を確率的に隠蔽 |

## 5.2 Raw Input 層の主要最適化

### CAS ループ / locked RMW 排除

Single-Writer を前提として、
- `fetch_or / fetch_and / compare_exchange`
- `lock xadd`

などの locked 命令を避け、`relaxed load + release store` に置換する方向で最適化された。

主な適用箇所:
- `setVkBit`
- `processRawInput` のマウス座標
- `processRawInput` のマウスボタン
- `processRawInputBatched` の VK / mouse / delta commit

### `pollHotkeys` フェンス集約

複数の acquire load を
- relaxed load 群
- `atomic_thread_fence(acquire)` 1 回

へ集約し、特に ARM / RISC-V 側のオーダリングコストを削減。

### OPT-R: Safety-net `processRawInputBatched` 除去

ドレイン後の再読み取りは理論上の安心感はあるが、コストに対して実利が小さいため除去。

### OPT-S: `snapshotInputFrame()` による hotkey + mouse delta 融合

`pollHotkeys()` と `fetchMouseDelta()` を 1 API にまとめ、load / fence / call 回数を削減。

### OPT-T / U

- `hasMouseDelta / hasButtonChanges` のような補助フラグを整理
- `Poll()` 内で `m_state` を一度だけ読む

## 5.3 ゲームロジック層の主要最適化

- `wheelDelta` を先に取得し、武器入力経路をゲート
- `aimBlockBits` によるブロック状態の統合
- Q14 固定小数点化
- `HotPointers` による頻用 RAM アドレスの集約
- `ProcessMoveAndButtonsFast` で store / load サイクルを整理
- `HandleGlobalHotkeys` インライン化
- `mainRAM` 遅延取得

---

## 6. R1: 保守性・コード品質リファクタリング

| 項目 | 変更 | 効果 |
|---|---|---|
| R1-1 | `MelonPrimeCompilerHints.h` 新設 | `FORCE_INLINE` / `LIKELY` / `PREFETCH_*` / `HOT/COLD_FUNCTION` / `NOINLINE` の一元化、ODR リスク排除 |
| R1-2 | `takeSnapshot()` + `scanBoundHotkeys()` 抽出 | `pollHotkeys` / `snapshotInputFrame` / `resetHotkeyEdges` / `hotkeyDown` の重複除去 |
| R1-3 | `drainPendingMessages()` 抽出 | `Poll()` と `PollAndSnapshot()` のドレインロジック重複排除 |
| R1-4 | `SmallVkList` 導入 | hotkey バインド経路のヒープ確保削減 |
| R1-5 | `BindMetroidHotkeysFromConfig()` の Config 一括取得 | 28 回のテーブル取得を 1 回に縮小 |
| R1-6 | `static bool s_isInstalled` → `m_isNativeFilterInstalled` | マルチインスタンス安全性とテスタビリティ向上 |
| R1-7 | `TOUCH_IF_PRESSED` マクロ → constexpr テーブル | 型安全・デバッグ容易性向上 |
| R1-8 | UTF-8 文字化けコメント修正 | `â†'`, `Ã—` などの残骸除去 |

**総評**  
R1 はホットパスの性能を変えずに、将来の改修と検証をしやすくしたラウンドである。

---

## 7. R2: 正確性・ヒープ排除完全化

| 項目 | 変更 | 効果 |
|---|---|---|
| R2-1 | `processRawInputBatched` ボタン優先度統一 | 単一イベント処理とバッチ処理の意味論を一致 |
| R2-2 | `processRawInput` の `fetch_add` 化 | コード量削減、意味的正確性向上 |
| R2-3 | `setHotkeyVks(int, const UINT*, size_t)` 追加 | `std::vector<UINT>` ブリッジの完全撤廃 |
| R2-4 | `MapQtKeyIntToVks()` + `SmallVkList` 直結 | hotkey バインド経路のヒープ確保 0 |
| R2-5 | マウスボタン LUT 化 | switch 連鎖をテーブル参照へ |
| R2-6 | `hasMask[64]` 廃止 | 64B 削減、`m_boundHotkeys` に統合 |
| R2-7 | `MelonPrimeGameWeapon.cpp` の NDS キャッシュ | 一貫性向上、コールドパス改善 |
| R2-8 | 残存 UTF-8 文字化け修正 | コメント品質の統一 |

**総評**  
R2 は「ホットパス微改善」よりも、**正確性バグ 1 件の修正** と **ヒープ排除の完了** が本質である。

---

## 8. Round 4: P-1～P-10 の統合結果 (旧名も併記)

## 8.1 ステータス一覧

| ID | 種別 | 状態 | 結論 | 旧名 / 元見出し |
|---|---|---|---|---|
| P-1 | 正確性 | ✅ 適用 | `snapshotInputFrame` のメモリオーダリング修正 | `snapshotInputFrame` メモリオーダリング不整合 |
| P-2 | パフォーマンス | ❌ リバート | 二重プリフェッチ除去は性能悪化 | `HandleInGameLogic` / `ProcessAimInputMouse` 二重プリフェッチ除去 |
| P-3 | パフォーマンス | ✅ 適用 | `m_cachedPanel` による panel ポインタチェーン削減 | `UpdateInputState` panel ポインタチェーン最適化 |
| P-4 | パフォーマンス | ❌ 見送り | 再入パス二重管理の複雑化が大きい | 再入パス軽量化 |
| P-5 | コード品質 | ✅ 適用 | MSVC でも cold code の inlining を抑制 | MSVC `COLD_FUNCTION` 改善 |
| P-6 | パフォーマンス | ✅ 適用 | `processRawInputBatched` の型分岐ヒント改善 | `processRawInputBatched` イベントループ型分岐最適化 |
| P-7 | パフォーマンス | ❌ 不要 | 既に早期脱出が存在 | `HandleGlobalHotkeys` 条件最適化 |
| P-8 | パフォーマンス | ❌ 見送り | `FrameInputState` 再配置はリスクが高い | `FrameInputState` パディング活用 |
| P-9 | パフォーマンス / クリーンアップ | ✅ 適用 | reset 系の統合とフォーカス遷移側の整理 | `RunFrameHook` フォーカス遷移の `UNLIKELY` 分岐マージ |
| P-10 | パフォーマンス | ❌ 却下 | `hasKeyChanges` 除去は逆効果 | `processRawInputBatched` キーボードデルタの `hasKeyChanges` 分岐除去 |

## 8.2 適用済み項目の要点

### P-1 (旧名: `snapshotInputFrame` メモリオーダリング不整合)
- x86 では実害が見えにくい
- ARM / RISC-V での不整合防止が主眼
- 最小変更での修正が推奨

### P-3 (旧名: `UpdateInputState` panel ポインタチェーン最適化)
- `emuInstance->getMainWindow()->panel->getDelta()` の 3 段ポインタ追跡を、
  `m_cachedPanel->getDelta()` に短縮
- `OnEmuStart()` と `NotifyLayoutChange()` で更新すれば十分

### P-5 (旧名: MSVC `COLD_FUNCTION` 改善)
- `COLD_FUNCTION` を GCC / Clang だけでなく MSVC でも cold 側へ寄せる方向へ改善

### P-6 (旧名: `processRawInputBatched` イベントループ型分岐最適化)
- `processRawInputBatched()` 内のイベント型分岐に対して、実際の入力比率に合った分岐ヒントを与える

### P-9 (旧名: `RunFrameHook` フォーカス遷移の `UNLIKELY` 分岐マージ)
Round 4 文書では「RunFrameHook フォーカス遷移の `UNLIKELY` 分岐マージ」という表現があるが、統合すると本質は以下である。

- reset 系 API の整理
- `resetAll()` による fence 統合
- フォーカス遷移処理の冷経路化
- 既存 FIX との整合性改善

## 8.3 非採用 / 却下の要点

### P-2: 二重プリフェッチ除去はリバート (旧名: `HandleInGameLogic` / `ProcessAimInputMouse` 二重プリフェッチ除去)
見た目は冗長でも、`ProcessAimInputMouse()` 側の近距離プリフェッチは「保険」として有効であり、MainRAM のワーキングセットを考えると削除は逆効果だった。

### P-4: 再入パス軽量化は見送り (旧名同じ)
再入は稀で、`FrameAdvanceTwice()` が支配的。10～20 cyc の削減より、二重実装によるバグ混入リスクの方が大きい。

### P-7: 不要 (旧名: `HandleGlobalHotkeys` 条件最適化)
既に `HandleGlobalHotkeys` には早期脱出があり、追加の最適化余地はほぼない。

### P-8: 見送り (旧名: `FrameInputState` パディング活用)
`FrameInputState` の `_pad` 再利用は理論上可能でも、API 影響と可読性低下が大きい。

### P-10: 却下 (旧名: `processRawInputBatched` キーボードデルタの `hasKeyChanges` 分岐除去)
キーボード入力がないフレームが大半のため、`hasKeyChanges` ガードの方が合理的。

### その他の却下
- `processRawInputBatched` 手動アンロール
- `mainRAM` 追加プリフェッチ
- `Config::Table` ルックアップ高速化
- `UpdateInputState` の UnrollCheckDown/Press 再最適化

---

## 9. Round 5: P-11～P-27 の統合結果 (旧名も併記)

## 9.1 Round 5 の中心テーマ

Round 5 は CPU サイクル削減そのものよりも、以下を主目的とする。

1. **入力取得の鮮度を上げる**
2. **フレームタイミングのブレを減らす**
3. **Raw Input と SDL メッセージポンプの干渉を抑える**
4. **エイム出力の切り捨て損失をなくす**
5. **syscall / mutex / 仮想ディスパッチの定常コストを削る**

## 9.2 ステータス一覧 (正規化版)

| ID | 種別 | 状態 | 統合後の扱い | 旧名 / 元見出し |
|---|---|---|---|---|
| P-11 | タイマー解像度 | ✅ | `NtSetTimerResolution` 導入 | Windows タイマー解像度 (`NtSetTimerResolution`) |
| P-12 | フレームリミッタ | ✅ | Hybrid Sleep + Spin | 精密ハイブリッド Sleep+Spin フレームリミッタ |
| P-13 | フレーム順序 | ✅ | Late-Poll アーキテクチャ | Late-Poll フレームアーキテクチャ |
| P-14 | 歴史的段階 | ⚠→✅ P-33 で空インライン化 | PrePoll バッチドレイン。Round 5 では P-19 / P-20 に吸収された流れ。Round 6 (P-33) で `PrePollRawInput()` を空インライン化し完了 | PrePoll バッチドレイン (Stuck Keys 修正), PrePollRawInput |
| P-15 | ジョイスティック鮮度 | ✅ | `inputRefreshJoystickState()` 導入 | Late-Poll ジョイスティック |
| P-16 | VSync 復帰修正 | ✅ | 重複記述を統合 | VSync 設定復元, VSync 復帰バグ修正 |
| P-17 | サブピクセル残差蓄積 | ✅ | 重複記述を統合 | サブピクセルアキュムレータ, サブピクセルエイム蓄積, サブピクセル残差アキュムレータ |
| P-18 | Dual-Path エイム | ✅ | Direct path / Legacy path の二系統 | Dual-Path エイムパイプライン, P-18c 残差クランプ |
| P-19 | Stuck keys 根本修正 | ✅ | HiddenWndProc で即時 `processRawInput` | Stuck keys 根本修正 — HiddenWndProc processRawInput |
| P-20 | PrePollRawInput 除去 | ⚠→✅ P-33 で完了 | Round 5 の設計どおり P-19 により不要化。Round 6 (P-33) で呼び出し除去 + 空インライン化を実施 | PrePollRawInput 除去 |
| P-20b | 残差破壊バグ修正 | ✅ | `InputReset()` から残差リセット除去 | InputReset バグ修正 |
| P-20c | P-3 適用漏れ修正 | ✅ | `m_cachedPanel` のホットパス反映 | P-3 キャッシュ適用漏れ修正 |
| P-21 | `inputProcess` 分離 | ✅ | edge 検出と状態再ポーリングの分離 | `inputProcess` 分離 |
| P-22 | DeferredDrain 分離 | ✅ | PollAndSnapshot から drain を切り離し。P-35 はリバート済み、drainPendingMessages を維持 | DeferredDrainInput 呼び出し追加 |
| P-23 | ジョイスティック不在高速パス | ✅ | joystick 未接続時の SDL overhead 削減 | ジョイスティック不在時高速パス |
| P-24 | 外部ループ hotkey 早期脱出 | ✅ | `hotkeyPress==0` で 7 チェック回避 | 外部ループ ホットキー一括早期脱出 |
| P-25 | セーブフラッシュ間引き | ✅ | 30 フレームごとに flush check | セーブフラッシュ間引き |
| P-26a | Auto Screen Layout バイパス | ✅ | MelonPrime では不要な自動レイアウト処理を除外 | Auto Screen Layout バイパス |
| P-26b | DeferredDrain スロットル | ❌ 撤回 / コメント残骸あり | 8 フレームごと drain という案は文書上の一時案。現コードは毎フレーム `DeferredDrain()` で、旧コメントだけが残っている | DeferredDrain スロットル |
| P-27 | 整数スピン比較 | ✅ | float 乗算を除去 | 整数スピン比較 |

## 9.3 P-11: Windows タイマー解像度 (旧名: `NtSetTimerResolution`)

- `NtSetTimerResolution(5000)` を `WinInternal` に統合
- 0.5ms 解像度を狙う
- 失敗時は `timeBeginPeriod(1)` へフォールバック

**効果**
- `SDL_Delay(1)` が最大 15.6ms に張り付く問題を大幅に緩和
- P-12 のスピンマージンを 1.5ms → 1.0ms に縮小可能
- CPU スピン浪費を約 33% 改善

## 9.4 P-12: 精密ハイブリッド Sleep + Spin (旧名: 精密ハイブリッド Sleep+Spin フレームリミッタ)

旧:
- `SDL_Delay(round(ms))`
- フレーム時間が 15～32ms へブレる

新:
1. 粗い待機を `SDL_Delay`
2. 残りを QPC ベースのスピンで詰める

**効果**
- jitter を ±15ms 級から ±0.03ms 級へ改善
- ただし最大 ~1ms のスピン待ちによる CPU 使用は増える

## 9.5 P-13: Late-Poll フレームアーキテクチャ (旧名同じ)

旧:
```text
Poll → RunFrame → Render → Sleep
```

新:
```text
Sleep → Poll → RunFrame → Render
```

これにより、Sleep 中に到着した入力を **次フレームではなく直後の RunFrame に反映** できる。

## 9.6 P-14: PrePoll バッチドレイン / PrePollRawInput (歴史的段階, ただし現コード残置)

P-14 では、SDL の `PeekMessage` が WM_INPUT を dispatch する前に `GetRawInputBuffer` で救出する作戦が取られた。旧コメントでは **PrePollRawInput** と書かれている箇所がこれに対応する。

```text
PrePollRawInput
→ inputProcess
→ Sleep
→ PollAndSnapshot
```

しかしこの方式には
- `PrePollRawInput` と `inputProcess` の間のレース
- P-15 による SDL ジョイスティック更新回数増加との相性
- syscall の増大

という限界がある。

**Round 5 文書上の到達点**
- P-19 で HiddenWndProc 側が即時 `processRawInput`
- P-20 で PrePollRawInput 自体を削除

**ただし現コード監査結果 (Round 6 更新)**
- Round 6 (P-33) により `PrePollRawInput()` は空インライン化された
- `EmuThread.cpp` からの呼び出しも除去済み
- したがって、**P-14 は P-33 により完全に歴史的段階となった**

## 9.7 P-15 + P-21: ジョイスティック状態更新の分離 (旧名: Late-Poll ジョイスティック / `inputRefreshJoystickState()` / `inputProcess` 分離)

問題:
- `inputProcess()` は edge 検出と状態更新を同時に行う
- Sleep 後にもう一度 `inputProcess()` を呼ぶと、`lastHotkeyMask` が壊れて二重発火の温床になる

解決:
- メインループ側は `inputProcess()` を 1 回だけ実行し edge を確定
- Sleep 後は `inputRefreshJoystickState()` で **状態だけ** 更新する

この節は、Round 5 の元文書では **P-15: Late-Poll ジョイスティック** と **P-21: `inputProcess` 分離** に分かれていた。

```cpp
void EmuInstance::inputRefreshJoystickState()
{
    SDL_JoystickUpdate();
    inputMask = keyInputMask & joyInputMask;
    hotkeyMask = keyHotkeyMask | joyHotkeyMask;
    // lastHotkeyMask / hotkeyPress / hotkeyRelease は更新しない
}
```

## 9.8 P-16: VSync 復帰修正 (旧名: VSync 設定復元 / VSync 復帰バグ修正)

FastForward / SlowMo の出入りで VSync を無条件に `true` に戻すと、ユーザー設定を破壊する。ソースコメントで **VSync 設定復元** または **VSync 復帰バグ修正** と書かれている箇所はこの項目を指す。

修正後は
- 現在の `Screen.VSync` 設定
- `Screen.VSyncInterval`
- FastForward / SlowMo の出入り

を正しく考慮し、**「解除したら必ず true」ではなく「元設定へ戻す」** 方式へ修正した。

## 9.9 P-17: サブピクセル残差蓄積 (旧名: サブピクセルアキュムレータ / サブピクセルエイム蓄積 / サブピクセル残差アキュムレータ)

低感度時には、`delta * scale` が整数化でゼロになりやすく、細かいマウス移動が消えていた。コメント上は **サブピクセルアキュムレータ**、**サブピクセルエイム蓄積**、**サブピクセル残差アキュムレータ** のいずれで書かれていても同じ系列の改善を指す。

修正:
- Q14 固定小数点で残差を保持
- 出力した整数分だけ `residual -= out << 14`
- 小数部を次フレームへ持ち越す

**効果**
- 低速・低感度の aim ステッピングを改善
- 切り捨て損失ゼロ
- 残差は、感度変更、レイアウト変更、エイムブロックなどでのみ明示的にリセット

## 9.10 P-18: Dual-Path エイムパイプライン (旧名同じ)

### Direct Path
- ASM パッチが有効なとき
- `residual >> 12` で 4 倍分解能
- デッドゾーンを避け、毎フレーム即出力

### Legacy Path
- 従来互換パス
- `apply_aim()` を通し、デッドゾーンやスナップを保持

### 残差クランプ
P-18c として残差の暴走を防ぐクランプも導入。

## 9.11 P-19: HiddenWndProc 即時 `processRawInput` (旧名: Stuck keys 根本修正 / HiddenWndProc processRawInput)

P-14 の「SDL に dispatch される前に buffer から救う」発想ではなく、  
**dispatch された瞬間に `lParam` の `HRAWINPUT` を読み切る** 方式へ移行。

旧見出しの **Stuck keys 根本修正 — HiddenWndProc processRawInput** はこの節に対応する。

これにより、
- SDL がいつ `PeekMessage` しても安全
- DefWindowProc に食われる前に確保
- stuck keys の根本を断つ

## 9.12 P-20: PrePollRawInput 除去 (関連旧名: P-20b `InputReset` バグ修正 / P-20c P-3 キャッシュ適用漏れ修正)

P-19 により SDL dispatch が脅威でなくなったため、**Round 5 文書の設計上は** PrePollRawInput は不要になった。

**設計上の効果**
- `GetRawInputBuffer` 呼び出し回数削減
- PeekMessage / drain 系 syscall 削減
- 入力経路の簡素化

**監査注記 (Round 6 更新)**
- Round 6 (P-33) により `PrePollRawInput()` の実装は削除、呼び出しも除去済み
- ヘッダーでは空インラインとして残留（ソース互換性維持）
- **P-20 の設計到達点は P-33 により完了**

## 9.13 P-22: DeferredDrain 分離 (旧名: `DeferredDrainInput()` 呼び出し追加)

`PollAndSnapshot()` から `drainPendingMessages()` を切り離し、
旧コメントで **DeferredDrainInput 呼び出し追加** と書かれている箇所もここに含める。
- 入力反映のクリティカルパス
- メッセージ掃除のノンクリティカルパス

を分離した。

**重要な監査注記**
- Round 5 原文では `DeferredDrainInput()` は **RunFrame 直後** に置かれていた
- しかし現コードでは、後続変更により **`drawScreen()` の後** に配置されている
- したがって、P-22 の本質は **「分離」** であり、**最終配置説明は 1～5 外の後続変更で上書き済み** と整理する

## 9.14 P-23: ジョイスティック不在時高速パス (旧名同じ)

KB+M プレイヤーでも毎フレーム
- `SDL_LockMutex`
- `SDL_JoystickUpdate`
- `SDL_UnlockMutex`

が走っていた。

joystick 未接続時は、60 フレームに 1 回だけ新規接続確認を行い、残りは完全スキップする。

## 9.15 P-24: 外部ループ hotkey 一括早期脱出 (旧名: 外部ループ ホットキー一括早期脱出)

毎フレーム 7 個の `hotkeyPressed()` を呼ぶのは無駄。  
ほぼ全フレームで `hotkeyPress == 0` なので、

```cpp
if (UNLIKELY(emuInstance->hotkeyPress)) {
    // 7 個のチェック
}
```

に集約する。

## 9.16 P-25: セーブフラッシュ間引き (旧名同じ)

毎フレーム 3 回の `CheckFlush()` は過剰。  
30 フレームごと (約 0.5 秒) に 1 回へ間引く。

## 9.17 P-26a: Auto Screen Layout バイパス (旧名同じ)

MelonPrime は画面レイアウトを独自管理しており、melonDS 側の auto screen sizing は不要。  
したがって毎フレームの `PowerControl9` 読み取りや配列更新をコンパイル条件で除外する。

## 9.18 P-26b: DeferredDrain スロットル (旧名同じ, 監査上は撤回扱い)

`DeferredDrain()` を毎フレーム行うと、8kHz マウス環境では PeekMessage syscall が積み上がる。  
そこで 8 フレームごとに drain する、という案が一時的に提案された。

**ただし監査結果**
- 現コードの `DeferredDrain()` は毎フレーム実行される構造で、8 フレーム周期カウンタは存在しない
- `RawInputWinFilter.cpp` にある `With P-26 throttle (every 8 frames)` コメントは旧注釈の残骸
- したがって本項目は **現行実装では未採用 / 撤回扱い** とする

**提案当時の想定値**
- 8kHz / 60fps ≒ 133 WM_INPUT / frame
- 8 フレーム蓄積でも ~1064 メッセージ
- Windows キュー上限 10,000 未満に十分収まる

## 9.19 P-27: 整数スピン比較 (旧名同じ)

旧:
```cpp
while (SDL_GetPerformanceCounter() * perfCountsSec < targetTime) { ... }
```

新:
- `targetTime` をあらかじめ tick に変換
- ループ内は整数比較のみ

これにより、スピン中の float 乗算オーバーヘッドを削る。

---

## 10. 最終フレームパイプライン (1～5 統合後 / 旧名対応版)

Round 5 までを統合した**到達点**は次の形で整理できる。

```text
Round 5 文書上の到達点:
  メインループ:
    inputProcess()                   ← edge 検出はここで 1 回だけ
    hotkeyPressed(HK_Reset) など     ← P-24 で一括ゲート

    frameAdvanceOnce() {
      Sleep / HybridLimiter          ← P-11, P-12
      inputRefreshJoystickState()    ← P-15, P-21

      RunFrameHook() {
        PollAndSnapshot()            ← Z3 + P-19/P-20 後の最終入力取得
        UpdateInputState()
        HandleInGameLogic()
          ProcessMoveAndButtonsFast()← Z2
          ProcessAimInputMouse()     ← OPT-O + P-17 + P-18
      }

      SetKeyMask()
      makeCurrentGL()
      RunFrame()
      drawScreen()

      DeferredDrainInput()           ← P-22
        └ スロットル案             ← P-26b (最終的には未採用)
    }
```

### 現コード監査ベースの注記 (Round 6 適用後)
- P-33 により `PrePollRawInput()` は空インライン化。P-20 の設計到達点を達成
- `DeferredDrainInput()` は後続変更により **`drawScreen()` 後** に置かれている (P-35 はリバート済み、drainPendingMessages を使用)
- P-26b の 8 フレームスロットルは現コードには入っていない

### 歴史的補足
- P-14 の段階では `PrePollRawInput()` を使っていた
- P-19 により HiddenWndProc 側で即時 `processRawInput`
- Round 5 文書では P-20 により PrePollRawInput は不要化
- **Round 6 (P-33) で呼び出し除去・空インライン化を実施し、P-20 完了**

したがって、**文書上の設計到達点** と **現コードの実装状態** は P-33 により一致した。

---

## 11. 非採用・撤回・却下一覧

## 11.1 1 からの却下 / リバート

| 項目 | 結論 | 理由 |
|---|---|---|
| `static thread_local` バッファ → スタック | リバート | `__chkstk`、レジスタ圧迫、16KB stack buffer が不利 |
| `Poll()` ドレインループ上限 | 不採用 | 8000Hz 環境でも通常 ~133 メッセージ / frame で十分処理可能 |
| `ApplyAimAdjustBranchless` | リバート | 分岐予測が当たるホットケースでは ALU 追加が逆効果 |
| OnEmuUnpause への extra reset | 除去 | 既に `ApplyJoy2KeySupportAndQtFilter(..., doReset=true)` が実施済み |
| HandleGlobalHotkeys ゲート再設計 | 却下 | 効果が小さい |
| `m_rawFilter` → BIT フラグ | 却下 | ポインタ自体は結局必要 |
| UnrollCheckDown/Press 再設計 | 却下 | 既にテンプレート展開済み |
| ProcessMoveInputFast SnapTap 再設計 | 却下 | 既に十分最適 |
| HandleMorphBallBoost 早期脱出追加 | 却下 | 予測安定で効果小 |
| `m_isWeaponCheckActive` → BIT | 却下 | 稀経路で効果なし |
| `InputSetBranchless(INPUT_START)` バッチ化 | 却下 | 複雑化の割に効果が小さい |
| `FrameAdvanceDefault usesOpenGL()` キャッシュ | 却下 | コールドパスのみ |
| wheelDelta 再入フレームスキップ | 却下 | 再入が稀 |
| `isStylusMode` bool → BIT | 却下 | 外部参照あり、速度差実質なし |
| `m_isLayoutChangePending` → BIT | 却下 | bool と BIT の差が実質ない |
| `pollHotkeys` BSF 改善 | 却下 | 既に妥当 |
| `processRawInputBatched` の dwType 分離 | 却下 | キャッシュ局所性悪化 |
| mainRAM プリフェッチ | 却下 | 効果不確定 |
| panel ポインタキャッシュは不要 | 元文書では却下 | ただし Round 4 / 5 で P-3 と P-20c により採用へ転換 |

## 11.2 Round 4 からの却下 / リバート

| 項目 | 結論 | 理由 |
|---|---|---|
| P-2 | リバート | 近距離プリフェッチを失い逆効果 |
| P-4 | 見送り | 再入パス二重管理が危険 |
| P-7 | 不要 | 既に早期脱出あり |
| P-8 | 見送り | `FrameInputState` の再設計リスクが高い |
| P-10 | 却下 | `hasKeyChanges` ガード維持の方が良い |
| ループアンロール | 却下 | `NEXTRAWINPUTBLOCK` の可変長と相性が悪い |
| Config::Table 高速化 | 却下 | コールドパスのみ |
| mainRAM 追加プリフェッチ | 却下 | 効果不確定 |
| UnrollCheckDown/Press 再最適化 | 却下 | 改善幅微小 |

## 11.3 Round 5 の今後の検討事項

| 項目 | 内容 |
|---|---|
| フレームタイム可視化 | FPS だけでなく ms 表示があると P-12 の効果検証がしやすい |
| SDL ジョイスティック Late-Poll のさらなる整理 | `inputProcess()` を完全に Sleep 後へ寄せる設計余地 |
| `glFlush()` / GPU パイプライン遅延 | `glFinish()` は避ける前提で、SwapBuffers 前 flush のトレードオフ検討 |

## 11.4 Round 6 からのリバート

| 項目 | 結論 | 理由 |
|---|---|---|
| P-35 | リバート | DeferredDrain から `processRawInputBatched` を除去したところ stuck keys 再発。FIX-1 の shared-buffer semantics により、GetRawInputBuffer が PeekMessage の前に走る必要がある。`drainMessagesOnly` は `DeferredDrain` では使用不可 |

---


## 11.5 情報源監査で確定した読み替え

この統合版を読むときは、次の 4 点を固定ルールとして扱う。

1. **P-14 は歴史的に重要だが、Round 6 (P-33) により空インライン化された**  
   現コードでは `PrePollRawInput()` は何もしない。P-19 が全てカバーする。
2. **P-20 は Round 6 (P-33) により完了した**  
   `PrePollRawInput` の呼び出しは除去済み、実装は空インライン。
3. **P-22 の本質は drain 分離であり、配置は後続変更で上書きされている**  
   現コードでの最終位置は `drawScreen()` 後。P-35 はリバート済みのため `drainPendingMessages` を使用。
4. **P-26b のスロットルは現行実装では採用されていない**  
   残っているのはコメント上の痕跡だけ。


## 12. 変更ファイル統合一覧

| ファイル | 主な統合内容 |
|---|---|
| `MelonPrimeCompilerHints.h` | R1: 共通マクロ統合、P-5 |
| `MelonPrime.h` | OPT C/D/G/L/O/W、P-3、P-17、P-18、P-33 (PrePollRawInput 空インライン化) |
| `MelonPrime.cpp` | OPT D/E/K/L/O/W/Z1/Z2/Z3/Z4、FIX-3、P-14、P-20b、P-20c、P-22（現コード残置あり）、P-33 (実装除去) |
| `MelonPrimeGameInput.cpp` | OPT F/O/Q/Z2/Z3、FIX-2、P-3、P-17、P-18 |
| `MelonPrimeInGame.cpp` | OPT A/B/G/H/J/Z2/Z5、R1 constexpr テーブル、P-36 (HandleMorphBallBoost 分岐統合) |
| `MelonPrimeGameWeapon.cpp` | OPT A、R2 NDS キャッシュ |
| `MelonPrimeGameRomDetect.cpp` | OPT L |
| `MelonPrimeRawInputState.h` | R1 snapshot helpers、R2 `setHotkeyVks` / `hasMask` 整理、P-9 |
| `MelonPrimeRawInputState.cpp` | OPT S/T、R2 button precedence / fetch_add / LUT、P-1、P-6、P-37 (コミットフェーズ分岐結合) |
| `MelonPrimeRawInputWinFilter.h` | R1 drain helper、R2 overload、P-22、P-35 リバート (`drainMessagesOnly` 宣言は残留) |
| `MelonPrimeRawInputWinFilter.cpp` | FIX-1、OPT R/U、R1/R2、P-19、P-22、P-26b 旧コメント残骸、P-35 リバート (`DeferredDrain` は `drainPendingMessages` に復元) |
| `MelonPrimeRawHotkeyVkBinding.h` | R1 `SmallVkList` |
| `MelonPrimeRawHotkeyVkBinding.cpp` | R1/R2 ヒープ排除完全化 |
| `MelonPrimeRawWinInternal.h/.cpp` | P-11 `NtSetTimerResolution` |
| `EmuThread.cpp` | P-11、P-12、P-13、P-14、P-15、P-16、P-22、P-24、P-25、P-26a、P-27、P-32 相当の後続配置変更、P-33 (PrePollRawInput 呼び出し除去 ×2)、P-38 (内側 hotkey 一括ゲート)、P-39 (NeedsShaderCompile キャッシュ)、P-40 (除算→乗算)、P-41 (DSi volume sync 除去) |
| `EmuInstance.h` | P-15 / P-21 |
| `EmuInstanceInput.cpp` | P-15、P-21、P-23、P-34 (`inputProcess` ジョイスティック不在時 SDL スキップ) |

---

## 13. 環境メモ

- マウス: 8000Hz ポーリングレート
- 60fps 想定時: 約 133 WM_INPUT / frame
- コンパイラ: MSVC / MinGW 両対応
- `NEXTRAWINPUTBLOCK` を使う都合で MinGW では `QWORD` typedef が必要
- x86-64 TSO では relaxed load/store は通常 `MOV`
- Q14 固定小数点では `int64_t` 乗算を使用
- 4MB MainRAM を扱うため、プリフェッチや近接アクセスの評価は「L1 常駐」前提ではなく、実ワーキングセットを前提に考える必要がある

---

## 14. まとめ

この文書を **今後の正本** とし、旧版は履歴参照用として扱う。


1～6 を統合すると、MelonPrime の流れは次のように整理できる。

1. **1** で、Single-Writer 前提の Raw Input 最適化、FIX-1～3、OPT A～Z5 が固まった  
2. **2 (R1)** で、保守性と重複排除が進んだ  
3. **3 (R2)** で、正確性バグ修正とヒープ排除が完了した  
4. **4** で、微細最適化の採否が精査され、P-1 / P-3 / P-5 / P-6 / P-9 が採用された  
5. **5** で、入力遅延、フレーム順序、VSync 復帰、サブピクセル蓄積、stuck keys 根本修正、syscall 削減までを含む**パイプライン再設計案とその適用履歴**が整理された  
6. **6** で、P-20 (PrePollRawInput 除去) が完了し、inputProcess の SDL 不在時スキップ、DeferredDrain 軽量化、ホットパス分岐改善が適用された  
7. **7** で、フレームループ内の定常コスト（仮想ディスパッチ、浮動小数点除算、デッドコード、hotkey 分岐）が削減された

つまりこの 1～6 の統合結果は、単なる「速くするための断片集」ではなく、

- **Raw Input の正確性**
- **ゲームロジックのホットパス最適化**
- **入力取得タイミングの刷新**
- **エイム出力の精度向上**
- **保守性と将来の改修容易性**
- **syscall の定常コスト最小化**

を段階的に積み上げた、**MelonPrime の入力・フレーム基盤の進化履歴そのもの**である。


---

## 15. Round 6: P-33～P-37 の統合結果

## 15.1 Round 6 の中心テーマ

Round 6 は Round 5 の監査で判明した **「文書上の到達点」と「現コード状態」のギャップ** を解消し、加えて入力パイプラインの **定常 syscall コストをさらに削減** することを主目的とする。

1. **P-20 の完了** — PrePollRawInput の実質的除去
2. **KB+M プレイヤー向け SDL overhead 削減**
3. **DeferredDrain の軽量化**
4. **ホットパスの分岐改善**

## 15.2 ステータス一覧

| ID | 種別 | 状態 | 統合後の扱い | 対象ファイル |
|---|---|---|---|---|
| P-33 | syscall 削減 | ✅ | PrePollRawInput 除去 (P-20 完了) | `EmuThread.cpp`, `MelonPrime.cpp`, `MelonPrime.h` |
| P-34 | syscall 削減 | ✅ | inputProcess ジョイスティック不在時 SDL スキップ | `EmuInstanceInput.cpp` |
| P-35 | syscall 削減 | ❌ リバート | DeferredDrain 内 GetRawInputBuffer 除去 → stuck keys 再発 | `MelonPrimeRawInputWinFilter.cpp`, `MelonPrimeRawInputWinFilter.h` |
| P-36 | ホットパス改善 | ✅ | HandleMorphBallBoost 分岐統合 | `MelonPrimeInGame.cpp` |
| P-37 | ホットパス改善 | ✅ | processRawInputBatched コミットフェーズ分岐結合 | `MelonPrimeRawInputState.cpp` |

## 15.3 P-33: PrePollRawInput 除去 (P-20 完了)

Round 5 文書 (P-20) で「P-19 により PrePollRawInput は不要化」と定義されていたが、現コードでは実装・呼び出しが残置されていた。

**変更内容**
- `EmuThread.cpp` の 2 箇所の `melonPrime->PrePollRawInput()` 呼び出しを除去
- `MelonPrime.cpp` の `PrePollRawInput()` 実装を削除
- `MelonPrime.h` で空インラインに置換（ソース互換性維持）

**効果**
- `GetRawInputBuffer` syscall: 4 回/frame → 2 回/frame（`PollAndSnapshot` 内のみ）
- `PeekMessage` ループ: 2 回/frame → 0 回（`DeferredDrain` のみ残留）
- 推定削減: ~500–2000 cyc/frame

**安全性の根拠**
P-19 により `HiddenWndProc` が WM_INPUT 到着時に即座に `processRawInput(HRAWINPUT)` を実行する。SDL の `PeekMessage` で WM_INPUT が dispatch されても、データは `processRawInput` で即時キャプチャされる。`PollAndSnapshot` 内の `processRawInputBatched()` と `DeferredDrain` が残りを回収し、三重の防御が成立。

## 15.4 P-34: inputProcess ジョイスティック不在時 SDL スキップ

P-23 は `inputRefreshJoystickState()` のみに no-joystick 高速パスを実装していたが、`inputProcess()` 自体は毎フレーム `SDL_LockMutex` + `SDL_JoystickUpdate` + `SDL_UnlockMutex` を無条件実行していた。

**変更内容**
- `joystick == nullptr` 時は 59/60 フレームで SDL 呼び出しを完全スキップ
- 60 フレームに 1 回のみ throttled check で新規接続を検出

**効果**
- KB+M プレイヤー: 2 mutex syscall + SDL_JoystickUpdate を 59/60 フレームでスキップ
- 推定削減: ~300–600 cyc/frame

## 15.5 P-35: DeferredDrain 内 GetRawInputBuffer 除去 (リバート)

`drainPendingMessages()` は `processRawInputBatched()` (GetRawInputBuffer) + PeekMessage ループの 2 段構成だったが、`DeferredDrain` からの呼び出しでは P-19 により GetRawInputBuffer は空を返すと想定して除去を試みた。

**変更内容**
- `drainMessagesOnly()` を新設（PeekMessage ループのみ）
- `DeferredDrain()` が `drainMessagesOnly()` を使用するよう変更

**リバート理由: stuck keys 再発**

FIX-1 で文書化された **shared-buffer semantics** が原因。GetRawInputBuffer と GetRawInputData は内部バッファを共有しており:

1. `PollAndSnapshot` の `GetRawInputBuffer` がバッファを消費
2. `DeferredDrain` の `PeekMessage(PM_REMOVE)` が WM_INPUT を dispatch
3. `HiddenWndProc` → `processRawInput` → `GetRawInputData(HRAWINPUT)` を試みるが、バッファは既に消費済みで失敗
4. key-up イベントがロスト → stuck key

元の `drainPendingMessages` では `processRawInputBatched` が PeekMessage の前に走り、GetRawInputBuffer で新しいデータを先に確保していたため、後続の GetRawInputData が失敗しても安全だった。

**教訓**
- `drainPendingMessages` 内の `processRawInputBatched` は冗長に見えるが、shared-buffer に対するセーフティネットとして必須
- P-14 の「belt-and-suspenders」の設計意図はこの問題を防ぐためだった
- `drainMessagesOnly` は `drainPendingMessages` の後段として残すが、`DeferredDrain` では使わない

## 15.6 P-36: HandleMorphBallBoost 分岐統合

元コードでは `BIT_IS_SAMUS` テスト → `IsDown(IB_MORPH_BOOST)` テスト → else ブランチの 3 段構造だった。大多数のフレーム（非サムス or ブースト未押下）は else の aimBlock クリーンアップに到達する。

**変更内容**
- `LIKELY(!BIT_IS_SAMUS || !IsDown(IB_MORPH_BOOST))` で fast path を 1 分岐に統合
- aimBlock クリーンアップを fast path 内に移動
- ブースト実行ロジック（rare path）のネスト解除で可読性向上

**効果**
- 推定削減: ~5–15 cyc/frame（分岐予測改善が主）

## 15.7 P-37: processRawInputBatched コミットフェーズ分岐結合

マウスデルタのコミットで X/Y を独立にゼロチェックしていた 2 分岐を、`localAccX | localAccY` の 1 分岐に統合。

**変更内容**
- 外側のゼロチェックで両軸を結合
- 内側で個別の store スキップは維持（片軸のみ移動時の不要 store 回避）

**効果**
- マウス静止フレーム: 2 分岐 → 1 分岐
- 推定削減: ~3–8 cyc/frame

## 15.8 Round 6 合計推定効果

| 環境 | 削減 (cyc/frame) | 主要要因 |
|---|---|---|
| 8kHz マウス + KB+M | ~800–2600 | P-33 + P-34 |
| 通常マウス + KB+M | ~300–1000 | syscall 削減効果が相対的に大 |
| ジョイスティック使用 | ~500–2000 | P-34 の効果なし、他は同等 |

**注記:** P-35 はリバート済みのため合計に含まない。

## 15.9 Round 6 適用後のフレームパイプライン

```text
Round 6 適用後の到達点:
  メインループ:
    inputProcess()                   ← P-34: no-joystick 時 SDL スキップ
    hotkeyPressed(HK_Reset) など     ← P-24 で一括ゲート

    frameAdvanceOnce() {
      Sleep / HybridLimiter          ← P-11, P-12
      // P-33: PrePollRawInput 除去
      inputRefreshJoystickState()    ← P-15, P-21

      RunFrameHook() {
        PollAndSnapshot()            ← Z3 + P-19 後の最終入力取得
        UpdateInputState()
        HandleInGameLogic()
          HandleMorphBallBoost()     ← P-36: 分岐統合
          ProcessMoveAndButtonsFast()← Z2
          ProcessAimInputMouse()     ← OPT-O + P-17 + P-18
      }

      SetKeyMask()
      makeCurrentGL()
      RunFrame()
      drawScreen()

      DeferredDrainInput()           ← P-22 (drainPendingMessages, P-35 リバート済み)
    }
```

---

## 16. Round 7: P-38～P-41 のフレームループ最適化

## 16.1 Round 7 の中心テーマ

Round 6 が入力パイプラインの syscall 削減に焦点を当てたのに対し、Round 7 は **フレームループ自体の定常コスト** を削減する。対象はすべて `EmuThread.cpp` の `frameAdvanceOnce` ラムダ内である。

## 16.2 ステータス一覧

| ID | 種別 | 状態 | 内容 | 推定効果 |
|---|---|---|---|---|
| P-38 | 分岐削減 | ✅ | 内側ループ hotkey 一括ゲート (P-24 の拡張) | ~5–10 cyc/frame |
| P-39 | 仮想ディスパッチ削減 | ✅ | NeedsShaderCompile キャッシュ | ~15–25 cyc/frame |
| P-40 | 浮動小数点演算改善 | ✅ | targetTick 算出の除算→乗算変換 | ~15–30 cyc/frame |
| P-41 | デッドコード除去 | ✅ | DSi ボリューム同期スキップ (NDS 専用) | ~10–20 cyc/frame |

## 16.3 P-38: 内側ループ hotkey 一括ゲート

P-24 がメインループの 7 個の `hotkeyPressed()` チェックを `hotkeyPress == 0` で一括ゲートしたのと同じパターンを、`frameAdvanceOnce` 内の 3 個のチェック (FastForwardToggle, SlowMoToggle, AudioMuteToggle) にも適用。

99.9%+ のフレームで `hotkeyPress == 0` なので、3 回の bit test + 分岐がスキップされる。

## 16.4 P-39: NeedsShaderCompile 仮想ディスパッチ削減

`GPU.GetRenderer().NeedsShaderCompile()` は vtable lookup + indirect call (~15–25 cyc) だが、シェーダーコンパイル完了後は 100% `false` を返す。

`shadersReady` フラグを導入し、コンパイル完了後は仮想ディスパッチ自体をスキップ:

```cpp
bool needsCompile = UNLIKELY(!shadersReady)
    && emuInstance->nds->GPU.GetRenderer().NeedsShaderCompile();
```

短絡評価により、`shadersReady == true` ならば右辺は評価されない。

## 16.5 P-40: targetTick 算出の除算→乗算変換

P-27 で導入されたスピンループの整数比較では `targetTime / perfCountsSec` で tick に変換していた。
`perfCountsSec = 1.0 / frequency` なので、この式は `targetTime * frequency` と等価。

```cpp
// 変更前: DIVSD (~20-35 cyc)
const Uint64 targetTick = static_cast<Uint64>(targetTime / perfCountsSec);

// 変更後: MULSD (~3-5 cyc)
const Uint64 targetTick = static_cast<Uint64>(targetTime * perfCountsFreq);
```

`perfCountsFreq` はループ前に一度だけ計算し、ラムダキャプチャで参照。

## 16.6 P-41: DSi ボリューム同期スキップ

MelonPrime は NDS (ConsoleType == 0) 専用のため、DSi 固有のボリューム同期コードは到達不能。コンパイル時に `#ifndef MELONPRIME_DS` で除外し:

- `audioDSiVolumeSync` の bool テスト
- `nds->ConsoleType` のメンバアクセス
- `DSi*` キャスト + I2C ポインタ追跡の可能性

を毎フレーム除去。

## 16.7 Round 7 合計推定効果

| 環境 | 削減 (cyc/frame) | 備考 |
|---|---|---|
| 全環境共通 | ~45–85 | P-38 + P-39 + P-40 + P-41 |

Round 6 (P-33/P-34) の syscall 削減と比べると 1 桁小さいが、全環境で均等に効く定常改善。

## 16.8 Round 6 + 7 統合後のフレームパイプライン

```text
Round 7 適用後の到達点:
  メインループ:
    inputProcess()                   ← P-34: no-joystick 時 SDL スキップ
    if (hotkeyPress)                 ← P-24: 一括ゲート (7 チェック)
    { ... }

    frameAdvanceOnce() {
      Sleep / HybridLimiter          ← P-11, P-12, P-40 (mul)
      inputRefreshJoystickState()    ← P-15, P-21

      NeedsShaderCompile             ← P-39: shadersReady で vtable スキップ
      RunFrameHook() {
        PollAndSnapshot()            ← Z3 + P-19
        UpdateInputState()
        HandleInGameLogic()
          HandleMorphBallBoost()     ← P-36: 分岐統合
          ProcessMoveAndButtonsFast()
          ProcessAimInputMouse()     ← OPT-O + P-17 + P-18
      }

      SetKeyMask()
      makeCurrentGL()
      RunFrame()
      drawScreen()

      DeferredDrainInput()           ← P-22

      if (hotkeyPress)               ← P-38: 一括ゲート (3 チェック)
      { FF / SlowMo / Mute }
                                     ← P-41: DSi volume sync 除去
    }
```
