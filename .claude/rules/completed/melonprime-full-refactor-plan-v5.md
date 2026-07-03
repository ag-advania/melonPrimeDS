# MelonPrime 全面リファクタリング計画 V5（Phase 0–7）— パフォーマンス主目的

**作成日:** 2026-07-04
**対象ブランチ:** `highres_fonts_v3`
**ステータス:** 実装・文書化済み / **検証 pending**（2026-07-04）— ROM 基準値・S21・CI green 待ち。`completed/` へ移動済みだが全 Phase 完了扱いではない。
**前提:** [V1](melonprime-full-refactor-plan.md)〜[V4](melonprime-full-refactor-plan-v4.md) の後継。
構造的負債（リテラル・パッチライフサイクル・HUDスキーマ・プラットフォーム散乱）は V1–V4 の
ラチェットで固定済み。**V5 はパフォーマンス（低遅延・低CPU・フレームタイム安定）を主目的**とする。

**最重要の前提認識:** Windows のホットパスは Rounds 1–9 / OPT A–Z5 / P-1〜P-48 /
Custom HUD Perf（[melonprime-refactoring.md](../melonprime-refactoring.md) が正典）で徹底最適化済みで、
V1 §2.1 の do-not-touch リストが生きている。**V5 の主戦場はそこではなく、
(a) 2026-07 に追加された macOS / Linux 経路（正しさ優先で書かれ、性能監査が一度もない）、
(b) 計測基盤の不在（歴代 Round は概算サイクルで進めており、フレームタイム分布を実測する仕組みが
リポジトリに存在しない — `grep MELONPRIME_PERF` = 0件で確認済み）** の2つである。

---

## 0. コードベース俯瞰 — MelonPrime 固有処理の散在マップ（2026-07-04 実測）

| ドメイン | 主なファイル | LOC | ホットパス関与 |
|---|---|---:|---|
| 入力（raw層） | `MelonPrimeRawInput{State,WinFilter,MacFilter,LinuxFilter}` + `PlatformInput.h` | 2,366 | ◎ 毎フレーム + 毎イベント |
| 入力（ゲーム層） | `MelonPrime.{cpp,h}` / `MelonPrimeInGame.cpp` / `MelonPrimeGameInput.cpp` / `MelonPrimeGame*` | 4,747 | ◎ `RunFrameHook`/`HandleInGameLogic` |
| HUD/描画 | `MelonPrimeHud*`（render/config/edit/screen断片） | 12,410 | ◎ 毎プレゼンテーションフレーム |
| ROMパッチ/フック | `MelonPrimePatch*` / `MelonPrimeArm9*` | 6,239 | ○ コールドパス中心 + JITトランポリン |
| 設定UI | `InputConfig/MelonPrime*` | 5,675 | × ダイアログ時のみ |
| フレーム更新 | `EmuThread.cpp`（+ `MelonPrimeEmuThread*.inc`） | — | ◎ フレームリミッタ/ペーシング |

毎フレーム経路の全体像（プラットフォーム別）は [melonprime-aim-input.md](../melonprime-aim-input.md) と
[melonprime-gameplay-runtime.md](../melonprime-gameplay-runtime.md) §1/§9 が正典。

---

## 1. ゴールと非ゴール

### ゴール
1. **毎フレーム実行される不要処理の排除** — 特に非Windowsの新経路に残る per-frame
   syscall / Qt呼び出し / atomic / 文字列比較（§2 に実測済みの初期リスト）
2. **フレームタイム計測基盤の導入** — p50/p99/最大、区間別タイマ。以後の全最適化を
   「概算」ではなく実測で判定できる状態にする（Round 10 以降の土台）
3. **非Windowsのフレームペーシング最適化** — P-11/P-12 相当の検討が mac/Linux で未実施
4. **キャッシュ/invalidation 規律の台帳化** — [melonprime-performance.md](../melonprime-performance.md)
   の原則に V4 以降の新規コードを照合し、invalidation owner を明文化
5. 結果: 非Windowsで **per-frame syscall −1以上・不要atomic除去・スピンCPU削減**、
   全プラットフォームで**フレームタイム分布の可視化と基準値の記録**

### 非ゴール（やらないこと）
- **V1 §2.1 do-not-touch リストの再最適化**（`ProcessAimInputMouse` の数式、
  `snapshotInputFrame`、`RunFrameHook` 分岐構造、`MoveLUT`、EmuThread フレームループの
  P-11..P-47 適用部、`MelonPrime.h` メンバ順、JITトランポリン）。触る場合は
  歴代どおり「単独コミット + 計測必須」だが、V5 では計画しない
- **計測なき最適化**。Phase 0 の基盤なしに Phase 3 以降の変更をコミットしない
- 機能追加・挙動変更（early-out追加等はすべて出力不変であること）
- melonDS コア（`src/` 直下）の変更（コンピュートレンダラ含む — 済みの Fix D/E を維持）

---

## 2. 無駄の棚卸し（初期実測 2026-07-04・証拠付き。Phase 1 で網羅化）

### V5-W1. mac: エイム毎フレームの CGWarp syscall（最有力・実測済み）
- [MelonPrimeGameInput.cpp:585](../../../src/frontend/qt_sdl/MelonPrimeGameInput.cpp) —
  mac は `warpCursorAfterAim = true` のままで、**エイム出力のある毎フレーム
  `CGWarpMouseCursorPosition`（syscall 相当）を発行**している
- GCMouse/IOHID デルタはワープ不変（デバイスレベル）なので、Linux で実証済みの
  「毎フレームワープ廃止 + パネル閾値ワープで格納」方式（V4/aim-input §9）を
  mac にも適用可能。P-33/P-47 と同クラスの per-frame syscall 削減
- 注意: mac の QCursor フォールバック（権限なし時）だけは再センターが必要 —
  Linux と同じく「rawアクティブ時のみワープ省略」のゲートにする

### V5-W2. Linux: rawモード時の毎フレーム `resetAimMouseDelta()`（実測済み）
- [MelonPrimeGameInput.cpp:227](../../../src/frontend/qt_sdl/MelonPrimeGameInput.cpp) —
  raw アクティブの毎フレーム、パネル蓄積の破棄に **atomic release store ×2 +
  baseline flag store** を無条件実行
- パネル側は raw アクティブ中は蓄積しない（V4 で mouseMoveEvent 側をゲート済み）ので、
  毎フレームの破棄は冗長。**モード遷移エッジ（panel→raw 切替時）に1回**で足りる

### V5-W3. `platformName()` の都度 QString 比較（実測: PlatformInput.h 2箇所）
- [MelonPrimePlatformInput.h:31,83](../../../src/frontend/qt_sdl/MelonPrimePlatformInput.h) —
  Linux のワープ/取得判定が呼び出しごとに `QGuiApplication::platformName() ==
  QStringLiteral("xcb")`（QString 生成+比較）。ワープは閾値化済みでコールド寄りだが、
  プラットフォーム名はプロセス起動後不変 — **static bool に1回で確定**できる

### V5-W4. 計測基盤の不在（実測: 該当シンボル 0件）
- フレームタイムのヒストグラム/percentile/区間タイマが存在しない。歴代 Round の
  効果推定はすべて概算サイクル。ペーシング比較（Phase 3）や回帰検知が現状不可能

### V5-W5. 非Windowsフレームペーシングの未調律（コード根拠）
- [EmuThread.cpp](../../../src/frontend/qt_sdl/EmuThread.cpp) のハイブリッドリミッタは
  coarse sleep のマージン **1.0ms が Windows の NtSetTimerResolution(0.5ms) 前提**
  （P-11/P-12 のコメントに明記）。mac/Linux の nanosleep 系は粒度が桁で細かい可能性が
  高く、マージン縮小 = スピン時間（busy CPU）削減の余地。**要計測**（Phase 0 依存）
- mac: GCMouse ハンドラはデフォルトでメインキューに配送 — 専用 dispatch queue 化で
  配送ジッタを下げられる可能性（**要計測**、挙動同一が条件）

### V5-W7. per-frame パッチ/状態再評価（[別ロードマップ](../notes/melonprime-highres-fonts-v3-refactor-roadmap.md)より採用・要計測）
- out-of-game + focused 中、`Patches_Apply(PatchSite_OutOfGameFrame)` が毎フレーム
  registry ループ + self-guard 関数呼び出しを実行（FixWifi / UseFirmwareLanguage /
  ExpandStageMatrix）。[gameplay-runtime.md](../melonprime-gameplay-runtime.md) §7 は
  「cheap cold-path check」として**意図的設計**と明記 — 置換是非は計測で判断
- battle runtime 中の per-frame `OsdColor_ApplyOnce()`（pattern B: ゲームがRAMを
  上書きするため意図的な毎フレーム再評価）。内部の実コスト（read/branch/write）を計測し、
  edge/epoch 置換が**安全な場合のみ**変更。不能なら理由をコメント固定して現状維持

### V5-W8. HUD の CPU 再描画残存（同ロードマップ 3.4 と一致・要計測）
- dirty rect / GL upload skip（OPT-DR1..DR3）後も **CPU 側の clear + QPainter 再描画は
  残る**（[repo-architecture.md](../repo-architecture.md) OPT-DR3 節に明記済みの既知事実）。
  高解像度フォント + ズームスコープ + 大レーダーの組合せでは QPainter が支配的になる
  可能性 — 次の段階は **element-level render cache / static layer 分離**（Phase 4 の
  ストレッチ。計測で QPainter 支配が確認された場合のみ）

### V5-W6. 規律コンプライアンスは現状良好（監査で確認済みの非・問題）
- ホットパス gameplay ファイルの per-frame `Config::Table` 参照: **0件**（実測）
- HUD の epoch/dirty-rect/game-frame キャッシュ群（OPT-DR1..DR3/SC1/ZOOM1/HRT1）は
  プラットフォーム非依存実装で mac/Linux にもそのまま効いている
- → Phase 1 は「壊れている前提」ではなく、**新規コード（V4 ファサード、HudGeometry
  消費者、mac/Linux フィルタ）が既存規律に完全準拠しているかの網羅確認**として行う

---

## 3. 不変条件

**V1 §2 / V2 §2 / V3 §2 / V4 §2 を全文継承**。V5 固有:

1. **計測ファースト**: Phase 3 以降の各変更は、Phase 0 の計測で before/after を取り、
   数値をコミットメッセージに残す（歴代 Round の「推定値表」文化を実測に格上げ）
2. 計測コードは `MELONPRIME_ENABLE_DEVELOPER_FEATURES` + 実行時
   `MELONPRIME_PERF=1` の二重ゲート。**リリースビルドのホットパスに1命令も残さない**
   （コンパイル時除去を確認）
3. 出力不変: early-out/キャッシュ追加はエミュレーション出力・エイム出力・HUD描画結果を
   1bitも変えない。変える最適化は本計画のスコープ外として分離
4. mac の CGWarp 廃止（W1）は「raw アクティブ時のみ」。QCursor フォールバックの
   再センター（Accessibility 事故の再発防止ルール、[build.md](../build.md)）は不変
5. 検証は3系統: Windows CI / mac ローカル / Linux VM（S20 手順は
   [linux-vm-build.md](../linux-vm-build.md) — マウス統合オフ必須）

---

## 4. フェーズ計画

### Phase 0: フレームタイム計測基盤（1–1.5日 / リスクなし / **全フェーズの前提**）

| 項目 | 内容 |
|---|---|
| 対象ファイル | `EmuThread.cpp`（`MelonPrimeEmuThread*.inc` 断片として追加）、新規 `MelonPrimePerfProbe.h`、`.claude/skills/`（集計スクリプト） |
| 目的 | フレームタイム p50/p95/p99/max と区間タイマ（input / RunFrame / draw / limiter-sleep / limiter-spin）を取得可能にする |
| 作業 | (1) `MelonPrimePerfProbe.h`: QPC/`SDL_GetPerformanceCounter` ベースの軽量区間タイマ + リングバッファ。二重ゲート（コンパイル時+環境変数）。(2) frameAdvanceOnce の既存境界（P-13 の Sleep→Poll→RunFrame→draw→DeferredDrain）にプローブ挿入 — **既存コードの並びは変えない**。(3) イベント/状態カウンタも併設（別ロードマップ案を採用）: 入力ソース比率（raw/panel/QCursor）、warp 回数、`Patches_Apply(OutOfGameFrame)` 呼び出し数、`OsdColor_ApplyOnce` の実 write 数、HUD dirty rect 面積、GL upload バイト数、OPT-DR3 hash skip 回数、`CustomHud_Render` 所要時間。(4) 1Hz で stderr に集計行、終了時にヒストグラム出力。(5) 3プラットフォームでベースライン取得し本ファイル末尾に記録 |
| 期待効果 | 以後の全最適化が実測判定に。回帰検知が可能に |
| リスク | 極小。プローブ自体のオーバーヘッド（ゲート内でも ~2 QPC/区間）は PERF=1 時のみ |
| 検証 | リリース構成で `nm`/逆アセンブルによりプローブ痕跡ゼロを確認。PERF=1 で 60fps 維持 |

### Phase 1: ホットパス網羅監査（1日 / リスクなし / 変更なし）

| 項目 | 内容 |
|---|---|
| 対象ファイル | 監査のみ: `MelonPrimeGameInput.cpp` / `MelonPrimeInGame.cpp` / `MelonPrime.cpp` / `PlatformInput.h` / 3フィルタ / `Screen.cpp` 非Win断片 / `MelonPrimeHudScreenCpp*.inc` / `EmuThread.cpp` |
| 目的 | §2 の初期リストを網羅化。「毎フレーム/毎イベントに実行されるもの」を全列挙し、syscall / Qt呼び出し / atomic / ヒープ / 文字列 / 除算 の観点で分類した**証拠表**を作る |
| 作業 | チェックリスト駆動 grep + 読解: (a) per-frame syscall（warp/GetAsyncKeyState相当/GetRawInputBuffer相当の非Win版）、(b) per-event の Qt オブジェクト生成、(c) atomic RMW vs load-first（P-48a 規律）、(d) epoch/dirty ゲートの漏れ（[melonprime-performance.md](../melonprime-performance.md) の Invalidations 原則と突合）、(e) mac GL / software 両描画パスの per-frame 差 |
| 期待効果 | Phase 2–4 の作業項目が確定。非対象（規律準拠済み）も「監査済み・白」として記録され再調査コストが消える |
| リスク | なし（コード変更なし） |
| 検証 | 証拠表を本ファイルに追記。各項目に file:line を付す |

### Phase 2: プラットフォーム入力ホットパスの残渣除去（1–1.5日 / リスク低中 / **本丸A**）

| 項目 | 内容 |
|---|---|
| 対象ファイル | `MelonPrimeGameInput.cpp` / `MelonPrimePlatformInput.h` / `MelonPrimeRawInputMacFilter.mm` / `Screen.cpp`(mac断片) |
| 目的 | W1/W2/W3 の除去 + Phase 1 で追加確定した項目 |
| 作業 | (1) **W1**: mac の `warpCursorAfterAim` を「rawアクティブ時 false」に（Linux と同型）。パネル側に mac 用の閾値格納ワープを追加（`clipCursorCenter1px` 相当の既存 CGWarp を流用、>96px 時のみ）。QCursor フォールバック時は現行どおり毎フレーム再センター維持。(2) **W2**: `resetAimMouseDelta()` を rawモード遷移エッジのみに（`m_platformRawAimWasActive` 1バイトで判定）。(3) **W3**: `PlatformInput_IsXcb()` を static 初期化1回に。(4) **入力ソースの構造化**（別ロードマップ案を採用）: フレーム冒頭でソースを1回決定する enum（WinRaw/MacRaw/LinuxRaw/PanelDelta/QCursorFallback）を導入し、所有権をコメントでなく構造で保証。`QCursor::pos()` フォールバックの使用条件を「パネルデルタが存在しない環境のみ」に限定。GameInput 内 `WarpCursorTo` static ラッパーは `PlatformInput_WarpCursor` へ統合。(5) Phase 1 検出分 |
| 期待効果 | mac: per-frame syscall −1（60/s の CGWarp 全廃 @raw時）。Linux: per-frame atomic store −3。歴代基準で P-33/P-48 クラス |
| リスク | mac のカーソル格納が閾値式になることで、ウィンドウ外クリック事故の余地（Linux で運用実績あり。格納閾値は Linux と同じ 96px）。フォールバック経路の退行（→ ゲートを raw 限定にすることで構造的に回避） |
| 検証 | S18/S19（macエイム/ライフサイクル）+ S20（Linux VM）+ `MELONPRIME_INPUT_DEBUG=1` でモード遷移ログ確認 + Phase 0 計測で input 区間の before/after |

### Phase 3: 非Windowsフレームペーシング調律（1–2日 / リスク中 / **本丸B・計測ゲート**）

| 項目 | 内容 |
|---|---|
| 対象ファイル | `EmuThread.cpp`（リミッタの非Win分岐のみ）、必要なら `MelonPrimeRawWinInternal` 相当の mac/linux 版（新規、任意） |
| 目的 | W5。スピン時間（busy CPU）の削減とフレームタイム分散の縮小 |
| 作業 | (1) Phase 0 でプラットフォーム別に sleep 粒度・オーバーシュートを実測。(2) 非Win の coarse マージン 1.0ms を実測値ベースで縮小（`#ifdef` でプラットフォーム別定数、Windows 値は不変）。(3) 効果が出る場合のみ: mac `mach_wait_until` / Linux `clock_nanosleep(TIMER_ABSTIME)` による絶対期限スリープを検討（別コミット）。(4) mac GCMouse ハンドラの専用キュー化は計測で配送ジッタが確認できた場合のみ |
| 期待効果 | スピン削減 = CPU/電力低下。p99 フレームタイムの安定化 |
| リスク | ペーシング退行（音ズレ/テアリング体感）。**Windows 分岐は1文字も触らない**ことで既存環境を隔離 |
| 検証 | Phase 0 ヒストグラムの before/after 必須（p50/p99/max + スピン時間区間）。60fps 長時間（10分）で分散悪化なし |

### Phase 4: HUD/描画 per-frame 残渣 + CPU再描画削減（1–3日 / リスク低→中 / 計測ゲート）

| 項目 | 内容 |
|---|---|
| 対象ファイル | 4a: Phase 1 の証拠表で確定（候補: `MelonPrimeHudScreenCpp*` の mac GL パス、software パスの mac 特有コスト）。4b（ストレッチ）: `MelonPrimeHudRenderDraw.inc` / `Main.inc` / `Assets.inc` |
| 目的 | 4a: 既存の epoch/dirty 網から漏れた per-frame 処理の除去（**新規検出分のみ**。OPT-DR/SC/HRT 済み領域の再設計はしない）。4b: **W8** — dirty rect の次段階として、CPU 側 QPainter 再描画の要素単位スキップ（element-level render cache / static-dynamic layer 分離） |
| 作業 | 4a: Phase 1 検出分に限定して、既存パターン（epoch ゲート / 静的キャッシュ / dirty rect）を適用。owner を明記。4b（**Phase 0 計測で QPainter コストが支配的と確認された場合のみ着手**）: 要素ごとの render signature（値+色+フォント+outline+scale）を導入し、署名一致なら再描画スキップ。static 層（枠/ラベル/固定アイコン）と dynamic 層（数値/crosshair/radar crop/timer）を分離。crosshair は既存の game-frame キー機構（OPT-ZOOM1）を尊重し別扱い。edit mode は対象外 |
| 期待効果 | 4a: 検出内容次第。4b: 高解像度フォント/大型スコープ時の CPU 大幅減（計測で確定） |
| リスク | signature 漏れによる HUD stale（resize / config変更 / TOML import / editor 後）。→ 検証に **pixel-hash 比較**（新旧レンダラの出力一致を機械確認 — 別ロードマップ案を採用）を必須化 |
| 検証 | S9 / S13 / S16 + **新旧出力の pixel-hash 一致**（フォント3モード × HUD全要素ON/OFF × resize）+ Phase 0 の draw 区間 before/after |

### Phase 5: キャッシュ/invalidation 台帳（0.5日 / リスクなし）

| 項目 | 内容 |
|---|---|
| 対象ファイル | [melonprime-performance.md](../melonprime-performance.md)（追記）、コードは変更なし |
| 目的 | 「どのキャッシュを誰が無効化するか」を1つの表に固定し、今後の機能追加時の invalidation 漏れを構造的に防ぐ |
| 作業 | 全キャッシュ（HUD config epoch群 / 3フィルタの蓄積・基準 / ZoomStatus / BattleMatchState / aim 残差 / P-3 panel / HudGeometry 消費側）について「キャッシュ名 / 保持場所 / 無効化トリガ / owner関数」の台帳を作成。V4 新規分（`absBaseInvalid`、`NotifyCursorWarp`、`gcActive`）を必ず含める |
| 期待効果 | stale バグの予防。レビュー基準の明文化 |
| リスク | なし |
| 検証 | 台帳の各行に file:line。リンクチェッカー green |

### Phase 6: 任意・計測ゲートのストレッチ

いずれも **Phase 0 計測で有意差/実害が出たものだけ**。出なければ「計測済み・効果なし」と
記録して閉じる（歴代の Rejected 表と同じ流儀）。

| 候補 | 出典/根拠 |
|---|---|
| W7 の実装: OutOfGameFrame の site 別 constexpr view / 呼び出し頻度の状態遷移化、`OsdColor_ApplyOnce` の edge 化 | 別ロードマップ Phase 2（安全条件は §2 W7 のとおり） |
| RAM read 予算監査: `ZoomStatus` / `DamageNotifyPurpleTick`（Weavel proxy read の分岐前倒し）/ `HotPointers` 未集約アクセスの洗い出し | 別ロードマップ Phase 4。[zoom-status-performance.md](../../features/zoom-status-performance.md) の「読み戻し禁止」規律と突合 |
| `MelonPrime.cpp` のコールドライフサイクル分離（OnEmuStart/Stop/ConfigReload 群を cold TU へ） | 別ロードマップ Phase 1。**MelonPrime.h のメンバ配置と RunFrameHook 分岐構造は不変**が絶対条件 |
| `getScreenWidgetRect()` のレイアウト時キャッシュ化、`resizeEvent`/`setupScreenLayout` の clip 更新重複整理 | 別ロードマップ Phase 8 の採用可能部分（`.inc`→`.cpp` 化は**不採用** — unity 断片規約と衝突） |
| Linux フィルタ poll ウェイクアップ調律、mac IOHID グレース定数、GCMouse ハンドラ専用キュー化 | V5 固有 |

### Phase 7: ドキュメント + 基準値固定（0.5日 / リスクなし）

1. [melonprime-refactoring.md](../melonprime-refactoring.md) に「Round 10 (V5) — Measured」節を追記
   （変更ごとの実測値表。以後この文書の効果欄は実測値で書く、という運用転換を明記）
2. [melonprime-performance.md](../melonprime-performance.md) に Phase 5 台帳と
   「per-frame syscall 予算表（プラットフォーム別・現在値）」を追加 —
   増やす変更はレビュー必須というラチェット
3. 3プラットフォームのフレームタイム基準値（Phase 0/3 の最終値）を本ファイルに記録し
   `completed/` へ移動
4. CLAUDE.md / rules README 更新

---

## 5. リスクと対策

| リスク | 該当 | 対策 |
|---|---|---|
| プローブ挿入が P-13 のフレーム順序を乱す | 0 | 既存境界に読み取り専用で挿入。順序・呼び出しは不変。リリースでコンパイル時除去を機械確認 |
| mac ワープ廃止でカーソルが画面外へ | 2 | Linux で運用実績のある閾値格納方式を同値移植。フォールバック時は従来動作。S18 で Alt-Tab/復帰含め確認 |
| 遷移エッジ化した reset の漏れ（stale 蓄積） | 2 | 遷移判定は1バイトの前回値比較のみ。`MELONPRIME_INPUT_DEBUG` に遷移ログを既設 — panel→raw 切替時の破棄を目視確認 |
| ペーシング変更の環境依存退行 | 3 | プラットフォーム別 `#ifdef` 定数で Windows 完全隔離。計測必須 + 10分ソーク |
| 計測基盤自体の観測者効果 | 0,3 | PERF=0 で完全無効。PERF=1 同士で before/after を比較（絶対値でなく差分で判断） |
| 「最適化したい病」で do-not-touch へ踏み込む | 全 | 変更前チェック: 対象が V1 §2.1 リストに載っていれば PR 分離 + 計測添付が必須（本計画では扱わない） |

---

## 6. スモークチェックリスト

V1 S1–S12 / V2 S13–S15 / V3 S16–S17 / V4 S18–S20 を継承。V5 追加:

| # | 項目 | 確認内容 |
|---|---|---|
| S21 | フレームタイム比較 | Phase 0 基盤で before/after の p50/p99/max を取得し、悪化なしを数値で確認（Phase 2/3/4 の DoD） |
| S22 | リリース痕跡ゼロ | リリース構成バイナリに perf プローブのシンボル/文字列が残らない |

---

## 7. 進捗トラッキング

| Phase | 内容 | 状態 | 完了日 | 結果メモ |
|---|---|---|---|---|
| 0 | 計測基盤 + 3プラットフォーム基準値 | **pending**（基盤完了） | — | `MelonPrimePerfProbe.h` + EmuThread 区間プローブ + カウンタ群 + `summarize-melonprime-perf.py`。mac dev ビルド green。**3 platform baseline: 未取得** |
| 1 | ホットパス網羅監査（証拠表） | 完了 | 2026-07-04 | §9 証拠表 45 行。RED×3（W1–W3）、YELLOW×17、WHITE×25。Phase 2 優先: P1-001→002→003 |
| 2 | 入力ホットパス残渣除去（本丸A） | 完了 | 2026-07-04 | W1 mac raw時warp廃止+閾値格納 / W2 panel→rawエッジreset / W3 IsXcb static / AimInputSource enum / P-48a load-first。completion audit: scatter 36→24（PlatformInput facade へ集約） |
| 3 | ペーシング調律（本丸B・計測ゲート） | **pending**（実装済み） | — | 非Win coarse margin 1.0→0.5ms（Windows 1.0ms 不変）。**ROM perf validation: 未取得** |
| 4 | HUD/描画残渣（計測ゲート） | **pending**（監査済み） | — | 4a: Phase 1 新規REDなし（既存 OPT-DR/SC/HRT 網内）。4b: ROM計測未実施のため element-cache 不着手 |
| 5 | invalidation 台帳 | 完了 | 2026-07-04 | `melonprime-performance.md` §Invalidation Ledger + §Syscall Budget |
| 6 | ストレッチ（計測ゲート） | **pending** | — | 全候補「ROM計測未実施・効果未確認」— 計測なしに完了扱いしない |
| 7 | 文書化 + 基準値固定 | **pending** | — | Round 10 追記、`completed/` 移動、README/CLAUDE 更新済み。**CI green / S21 / S22 検証 pending** |

### 初期実測値（2026-07-04、計画作成時）

- mac: ~~`warpCursorAfterAim = true`~~ **Phase 2 修正済** — raw active 時 warp なし
- Linux raw時: ~~毎フレーム `resetAimMouseDelta()`~~ **Phase 2 修正済** — panel→raw エッジのみ
- `platformName()` QString比較: ~~PlatformInput.h 2箇所~~ **Phase 2 修正済** — `PlatformInput_IsXcb()` static
- ホットパス gameplay ファイルの per-frame Config 参照: 0件（規律維持を確認）
- 計測基盤: **`MelonPrimePerfProbe.h`**（`MELONPRIME_ENABLE_DEVELOPER_FEATURES` + `MELONPRIME_PERF=1`）

### S22 リリース痕跡ゼロ（2026-07-04 completion audit）

Release 構成（`MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF`, macOS Release）:

```text
strings build-release-audit/melonPrimeDS.app/Contents/MacOS/melonPrimeDS \
  | rg 'MelonPrimePerf|MELONPRIME_PERF|\[MelonPrimePerf\]'  → (none)
nm … | rg MelonPrimePerf  → (none)
```

Windows/Ubuntu CI でも同手順で再確認 pending。

### Phase 0 基準値（計測手順 — ROM 実行後に数値を追記）

developer ビルドで `MELONPRIME_PERF=1 ./melonPrimeDS 2>&1 | tee perf.log`。終了時に stderr へ histogram。
集計: `python3 .claude/skills/summarize-melonprime-perf.py perf.log`

| プラットフォーム | p50 | p95 | p99 | max | 備考 |
|---|---:|---:|---:|---:|---|
| macOS | 未取得 | 未取得 | 未取得 | 未取得 | dev build 導入済み。要 ROM ソーク |
| Linux VM | 未取得 | 未取得 | 未取得 | 未取得 | 未計測 |
| Windows | 未取得 | 未取得 | 未取得 | 未取得 | 未計測 |

- ドメインLOC: raw入力 2,366 / HUD 12,410 / パッチ・フック 6,239 / ゲーム層 4,747 / 設定UI 5,675

---

## 9. Phase 1 証拠表（2026-07-04 監査 / `highres_fonts_v3` @ 9ff53250）

監査対象: `MelonPrimeGameInput.cpp` / `MelonPrimeInGame.cpp` / `MelonPrime.cpp` /
`MelonPrimePlatformInput.h` / RawInput 3フィルタ / `Screen.cpp`（非Win断片）/
`MelonPrimeHudScreenCpp*.inc` / `EmuThread.cpp`。**コード変更なし。**

**Severity:** RED = Phase 2 確定 / YELLOW = 計測ゲート / WHITE = 監査済み・規律準拠

**§2 クロスチェック:** W1–W3 RED 確認 / W4 解消（Phase 0）/ W5–W8 YELLOW or WHITE

### mac GL vs software HUD（毎フレーム差分）

| 観点 | GL (`MelonPrimeHudScreenCppOverlayOfGl.inc`) | Software (`MelonPrimeHudScreenCppOverlayOfSoftware.inc`) |
|---|---|---|
| CPU render | 共有: `QPainter` + `CustomHud_Render` | 同左 |
| Dirty clear | 共有: `MelonPrimeHud_PrepareTopOverlay` memset (OPT-HUD-1) | 同左 |
| Composite | `glTexSubImage2D` / full `glTexImage2D` + OSD shader `glDrawArrays`（dirty 時） | `painter.drawImage(compositeRect)` |
| Upload skip | OPT-DR3 FNV hash（≥256×256  unchanged） | N/A |
| 追加描画 | GL-native radar overlay（有効時） | なし |

両パスとも **CPU 側 QPainter 再描画は残る**（W8 / P1-012）。

### 証拠表

| ID | Sev | Cat | File:Line | One-line evidence | Phase action |
|---|---|---|---|---|---|
| P1-001 | RED | syscall | `MelonPrimeGameInput.cpp:607,674,709,731,752` | mac `warpCursorAfterAim=true`; エイム delta 毎フレーム `WarpCursorTo` → CGWarp (**W1**) | Phase 2: raw-active 時 false + 閾値格納 |
| P1-002 | RED | atomic | `MelonPrimeGameInput.cpp:232` | Linux raw 毎フレーム `resetAimMouseDelta()`（panel 未使用時も）(**W2**) | Phase 2: panel→raw 遷移エッジのみ |
| P1-003 | RED | syscall/Qt | `MelonPrimePlatformInput.h:88` | `PlatformInput_WarpCursor` 呼び出し毎 `platformName()==xcb` (**W3**) | Phase 2: static `PlatformInput_IsXcb()` |
| P1-004 | YELLOW | Qt | `MelonPrimeGameInput.cpp:267-269` | QCursor fallback: raw 不可時 `QCursor::pos()` / frame | Phase 2: fallback 条件を構造化 |
| P1-005 | YELLOW | atomic | `MelonPrimeRawInputMacFilter.mm:310-311` | mac snapshot: `accX/Y.exchange(0)` (RMW) | Phase 2: Win 同型 load+store (P-48a) |
| P1-006 | YELLOW | atomic | `MelonPrimeRawInputLinuxFilter.cpp:333-334` | Linux snapshot: `exchange(0)` (RMW) | Phase 2: 同左 |
| P1-007 | YELLOW | atomic | `Screen.h:109-110` | panel `getAimMouseDelta`: `exchange(0)` | Phase 2: load+store 検討 |
| P1-008 | YELLOW | atomic | `Screen.cpp:747-748` | panel `mouseMoveEvent`: `fetch_add` per event | 計測（イベント率） |
| P1-009 | YELLOW | syscall | `MelonPrimeRawInputMacFilter.mm:36-37` | `MacWarpCursorGlobal` → CGWarp | P1-001 解消で間接削減 |
| P1-010 | YELLOW | syscall | `MelonPrimeRawInputLinuxFilter.cpp:46-47` | 閾値 containment: `XWarpPointer`+`XFlush` | 計測（低頻度想定） |
| P1-011 | YELLOW | Qt | `MelonPrime.cpp:918-940` | `ShowCursor`: `QMetaObject::invokeMethod` on edge | コールド; 必要時計測 |
| P1-012 | YELLOW | HUD/Qt | `MelonPrimeHudScreenCppHelpers.inc:215-226` | 毎フレーム `QPainter` + `CustomHud_Render` (**W8**) | Phase 4: draw 区間計測 |
| P1-013 | YELLOW | HUD/GL | `MelonPrimeHudScreenCppOverlayOfGl.inc:44-133` | DR3 skip 後も GL composite 毎フレーム | Phase 4: 計測 |
| P1-014 | YELLOW | HUD/GL | `MelonPrimeHudScreenCppOverlayOfGl.inc:140-216` | GL-native radar pass（有効時） | Phase 4: 計測 |
| P1-015 | YELLOW | HUD/Qt | `MelonPrimeHudScreenCppOverlayOfSoftware.inc:32-44` | software `drawImage` composite | Phase 4: 計測 |
| P1-016 | YELLOW | pacing | `EmuThread.cpp:215-218` | coarse margin `- 1.0` ms 全プラットフォーム固定 (**W5**) | Phase 3: 実測ベース縮小 |
| P1-017 | YELLOW | pacing | `EmuThread.cpp:237-245` | spin loop 毎 limited frame | Phase 3: 計測後調律 |
| P1-018 | YELLOW | RAM | `MelonPrimeGameInput.cpp:624-628` | zoom-aim 有効時 `ZoomStatus::ReadScopeState` | Phase 6 ストレッチ |
| P1-019 | YELLOW | string | `MelonPrimePlatformInput.h:33` | `ShouldAcquireRawFilter` 内 `platformName()` | Phase 2: static cache (W3) |
| P1-020 | YELLOW | string | `Screen.cpp:1827-1848` | `getWindowInfo` cold `platformName()` | 低優先 |
| P1-021 | WHITE | intentional | `MelonPrimeGameInput.cpp:605,643-647` | Linux `warpCursorAfterAim=false`; P-44 zero-delta skip | 規律準拠 |
| P1-022 | WHITE | intentional | `Screen.cpp:717-734` | Linux 閾値 warp >96px のみ | 規律準拠; mac Phase 2 移植対象 |
| P1-023 | WHITE | intentional | `MelonPrime.cpp:632-633` | battle `OsdColor_ApplyOnce` 毎フレーム (**W7**) | 計測; edge 化は Phase 6 |
| P1-024 | WHITE | intentional | `MelonPrime.cpp:687-690` | OutOfGame `Patches_Apply` 毎フレーム (**W7**) | 計測; Phase 6 stretch |
| P1-025 | WHITE | intentional | `MelonPrimeInGame.cpp:118-122` | 毎フレーム center `TouchScreen` | do-not-touch |
| P1-026 | WHITE | atomic | `MelonPrime.cpp:574,729-730` | `isFocused.load(acquire)` 1回/frame | 規律準拠 |
| P1-027 | WHITE | atomic | `MelonPrime.cpp:562` | `m_configReloadPending.exchange` cold only | 規律準拠 |
| P1-028 | WHITE | W6 | `MelonPrimeGameInput.cpp` | per-frame `Config::Table` 参照 **0件** | 監査済み |
| P1-029 | WHITE | W6 | `MelonPrimeInGame.cpp` | per-frame `Config::Table` 参照 **0件** | 監査済み |
| P1-030 | WHITE | W6 | `MelonPrimeHudScreenCppHelpers.inc:117-124` | epoch gate before config/font/radar refresh | OPT-DR 規律 |
| P1-031 | WHITE | W6 | `MelonPrimeHudScreenCppHelpers.inc:66-79` | dirty clear = scanline memset (OPT-HUD-1) | 規律準拠 |
| P1-032 | WHITE | W6 | `MelonPrimeHudScreenCppOverlayOfGl.inc:76-83` | OPT-DR3 hash skip | 規律準拠 |
| P1-033 | WHITE | W6 | `MelonPrimeHudScreenCppLayout.inc:5-17` | layout 時キャッシュ; paint は member 読み | 規律準拠 |
| P1-034 | WHITE | W6 | `MelonPrimeHudScreenCppHelpers.inc:172-173` | `% 3`/`/ 3` は epoch-gated refresh 内のみ | 規律準拠 |
| P1-035 | WHITE | W4 | `MelonPrimePerfProbe.h` + `EmuThread.cpp` | Phase 0 区間プローブ; 二重ゲート | 基準値取得待ち |
| P1-036 | WHITE | W4 | `MelonPrimeGameInput.cpp:271-286` | `CountInputSource` perf counter | 規律準拠 |
| P1-037 | WHITE | compare | `MelonPrimeRawInputState.cpp:283-289` | Win delta: load-first (P-48a) | mac/Linux 収束目標 |
| P1-038 | WHITE | compare | `MelonPrimeGameInput.cpp:599-605` | Win/Lin 無 warp; mac のみ outlier | Phase 2 対象 |
| P1-039 | WHITE | intentional | `MelonPrimeInGame.cpp:273-288` | morph boost: shift/compare, 除算なし | 規律準拠 |
| P1-040 | WHITE | intentional | `MelonPrimeGameInput.cpp:341,662-667` | MoveLUT `& 0xF`; aim `>>` shifts | 規律準拠 |
| P1-041 | WHITE | intentional | `Screen.cpp:1935-1963` | `updateClipIfNeeded` Windows-only | mac/Linux は warp-on-clip |
| P1-042 | WHITE | intentional | `MelonPrimeRawInputMacFilter.mm:76-77` | writer `fetch_add` (GCMouse/IOHID) | single-writer OK |
| P1-043 | WHITE | intentional | `MelonPrimeRawInputLinuxFilter.cpp:264-285` | filter thread `poll(100ms)` off emu path | Phase 6 optional |
| P1-044 | WHITE | heap | hot-path TUs | per-frame heap alloc なし（監査範囲） | 規律準拠 |
| P1-045 | WHITE | edit-cold | `MelonPrimeHudScreenCppMouse*.inc` | edit mode only `getLocalConfig()` | gameplay 外 |

### Phase 2 優先キュー（RED 由来）

1. **P1-001** — mac raw-active 時 warp 廃止（最大 per-frame syscall 削減）
2. **P1-002** — Linux `resetAimMouseDelta` 遷移エッジ化
3. **P1-003 / P1-019** — `platformName()` static 1回確定
4. **P1-005 / P1-006 / P1-007** — exchange → load+store（Win 同型）

---

## 8. 推奨着手順序

```
0 (計測) → 1 (監査) → 2 (本丸A) → 3 (本丸B) → 4 → 5 → (6) → 7
```

- **0 が絶対の先頭**。これなしに 3/4/6 は着手禁止（不変条件1）
- 2 は計測前でも正当化できる唯一の実装フェーズ（syscall/atomic の存在自体が証拠のため）
  だが、効果の記録には 0 を使う
- 5 は独立・随時可能。総見積り: **必須（0–5, 7）で約5.5–8日相当**
