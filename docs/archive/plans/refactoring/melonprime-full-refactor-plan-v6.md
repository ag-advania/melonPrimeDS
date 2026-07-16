# MelonPrime 全面リファクタリング計画 V6（Phase 0–7）— 検証負債の完済と HUD CPU 削減

**作成日:** 2026-07-04
**対象ブランチ:** `highres_fonts_v3`（HEAD `efbe6b82` 時点で実測）
**ステータス:** Phase 0 macOS・Linux VM 基準値取得済み / Windows ソーク待ち（2026-07-04）。
[V7](../../../plans/refactoring/melonprime-full-refactor-plan-v7.md) Phase 4（2026-07-09）も同じ Windows 基準値ゲートに
到達し、計測なしでの実装を避けて正式にスキップ（「未計測のため見送り」）を選択した。両計画とも
この単一のゲート（Windows `MELONPRIME_PERF=1` ソーク、OSD ON 条件を含む）待ちで揃っている。
**前提:** [V1](melonprime-full-refactor-plan.md) /
[V2](melonprime-full-refactor-plan-v2.md) /
[V3](melonprime-full-refactor-plan-v3.md) /
[V4](melonprime-full-refactor-plan-v4.md) /
[V5](melonprime-full-refactor-plan-v5.md) の後継。
V1–V5 が解消した負債（パッチレジストリ / ROM X-macro / HUD スキーマ / リテラルラチェット /
プラットフォーム入力ファサード / HUD ジオメトリ共有 / 計測基盤 / 入力ホットパス残渣）は対象外。
本計画は **2026-07-04 に `highres_fonts_v3` を実測した証拠**に基づく。憶測項目はない。

---

## 0. 最重要の前提認識 — V6 の主敵は「未検証」である

V1–V4 で構造負債は解消され、ラチェット（リテラル予算 1 / scatter 予算 30 / inc 所有検査 /
スキーマ再生成検査）は**全て green を実測確認済み**（§1）。V5 で計測基盤
（`MelonPrimePerfProbe.h` + EmuThread 区間プローブ + `summarize-melonprime-perf.py`）も完成した。

しかし V5 は **「実装済み・検証 pending」のまま completed/ へ移動**されており、次が宙吊りである:

1. **3 プラットフォームのフレームタイム基準値** — macOS / Linux VM は 2026-07-04 ソーク取得済み（§8）。
   Windows は未取得
2. V5 Phase 3 のペーシング変更（非 Win coarse margin 1.0→0.5ms）が **ROM 実測なしで本番コードに入っている**
3. V5 Phase 4b（HUD element cache）と Phase 6 ストレッチ全候補が「計測待ち」で凍結
4. S18/S19/S20（mac/Linux エイム実機）、S21（フレームタイム比較）、S22 の Windows/Ubuntu 再確認が未実施
5. 手動スモークの未実施項目が V1 から 5 計画分積み残されている（S9/S10/S13/S14/S16/S17 系）

**V6 の第一目的はこの検証負債の完済**であり、第二目的が検証網を持った上での
HUD CPU 再描画削減（V5 W8 の実装）とコアの責務分離である。
計測なしに Phase 3 以降へ進むことは V5 不変条件 1 の継承として禁止する。

---

## 1. 無駄の棚卸し（2026-07-04 実測・証拠付き）

### 現状スナップショット（HEAD `efbe6b82`）

| 指標 | V5 完了時 | 今回実測 | 判定 |
|---|---:|---:|---|
| `MelonPrime*` LOC（.cpp/.h/.inc/.mm） | — | **33,090** | V4 最終 32,531 から +559（CrosshairFx / PerfProbe 等の正当な成長） |
| 非 canonical `"Metroid.*"` リテラル | 1 | **1**（`Config.cpp` 移行行のみ） | ✅ ラチェット維持 |
| `.inc` 所有検査 | green | **PASS（51 ファイル）** | ✅ |
| プラットフォーム散乱 | 30/30 | **24/30** | ✅ 予算に 6 の余剰 → ラチェット強化余地 |
| TODO/FIXME/HACK コメント | — | **0 件** | ✅ |
| `MELONPRIME_PERF` 基準値 | 未取得 | **macOS・Linux VM 取得済み** / Win 未取得 | ⚠️ V6-W1（Win pending） |

### V6-W1. 検証負債（最大の負債・§0 のとおり）

- `MELONPRIME_PERF` の消費箇所は `MelonPrimePerfProbe.h` + `EmuThread.cpp` の区間プローブ
  （FrameBegin / LimiterSleep / LimiterSpin / Input / RunFrame — 配線済みを実測確認）。
  **基盤は完成**。macOS / Linux VM で初回 ROM 10 分ソークを取得
  （`artifacts/perf-baseline/macos-perf-20260704-082454.log`、
  `linux-vm-perf-20260704-085743.log` — 後者は VM 共有フォルダ権限で tee 失敗、
  端末出力から復元）。
  Windows は未計測。
- V5 進捗表: **mac / Linux VM 基準値は V5 completed Phase 0 表へ書き戻し済み**。Win・S21/S22・S24・V5 最終確定は pending。

### V6-W2. HUD CPU 再描画（V5 W8 の継続・計測ゲート）

- dirty rect（OPT-DR1..DR3）と GL upload skip の後も、**CPU 側の clear + QPainter 再描画は毎
  プレゼンテーションフレーム残る**（repo-architecture.md OPT-DR3 節に明記済みの既知事実）。
- 対象コードの規模（実測）: `HudRenderDraw.inc` 1,438 行 + `HudRenderCrosshairFx.inc` 462 行
  （Draw.inc:800 から include されるネスト unity 断片）+ `HudRenderMain.inc` 348 行。
- 高解像度フォント + ズームスコープ + 大レーダーの組合せで QPainter が支配的になる仮説は
  V5 でも立てられたが、**ROM 計測がないため実装着手できなかった**。V6 では Phase 0 → 2 → 3 の
  順で「計測 → 検証網 → 実装」と直列に解く。

### V6-W3. HUD リファクタの回帰検証が全て手動目視

- S1–S22 の 22 項目が**全て手動**。HUD 系（S9/S13/S16）は「目視一致」が判定基準で、
  V3 Phase 3 / V4 Phase 3 / V5 Phase 4 のいずれも「pixel-hash 比較を必須化」と書きながら
  **ハーネスが存在しない**（リポジトリに該当ツールなし、実測確認）。
- 一方、実現可能性の根拠は既にコード内にある: 編集モードの `DrawEditHudPreview()`
  （`MelonPrimeHudConfigOnScreenDraw.inc:298-380`）は **合成状態 + 制御された RAM ポインタで
  runtime の `DrawCrosshair/DrawHP/DrawWeaponAmmo/DrawWeaponInventory` を直接駆動**している。
  つまり Draw 層は ROM なしで決定的にレンダリング可能。これをオフラインの golden-hash
  ハーネスに固定すれば、V6-W2 の element cache を安全に実装できる。

### V6-W4. `MelonPrimeCore` の責務集中（ロードマップ P0 の 2 計画連続先送り）

- `MelonPrime.cpp` 976 行 / `MelonPrime.h` 714 行。`RunFrameHook()` は **243 行**
  （`MelonPrime.cpp:537-779`、実測）で、再入・設定 reload・ROM 検出・join/battle-enter/
  match-end・HUD pre-frame・damage notify・cursor mode・focus 遷移・weapon switch pending を
  1 関数で司る。
- [Post-V4 ロードマップ](melonprime-highres-fonts-v3-refactor-roadmap.md) Phase 1 と
  V5 Phase 6 ストレッチで 2 回「候補」に挙がり、2 回とも見送り。V6 で条件付き（§4 Phase 4 の
  不変条件厳守・純移動のみ）で決着させる。
- 分離候補のコールド関数は `ApplyConfigReload`（:440）/ `HandleGameJoinInit`（:787）/
  `HandleBattleRuntimeEnter`（:883）+ `OnEmuStart/OnEmuStop/ResetRuntimeStateForBoot` 群
  （いずれも `COLD_FUNCTION` 指定済みを実測確認）。

### V6-W5. per-frame 再評価の残り（V5 Phase 6 から繰越・計測ゲート）

V5 が「計測なしに完了扱いしない」として閉じずに残した候補（全て V5 §Phase 6 表から繰越）:

- `Patches_Apply(PatchSite_OutOfGameFrame)` の毎フレーム registry ループ（site 別 constexpr view 化）
- battle 中の per-frame `OsdColor_ApplyOnce()` の実コスト計測と edge/epoch 化の安全性判定
  （pattern B: ゲームが RAM を上書きするため、**安全と証明できない場合は現状維持 + 理由コメント固定**）
- RAM read 予算監査（`ZoomStatus` / `DamageNotifyPurpleTick` の Weavel proxy 分岐前倒し）
- `getScreenWidgetRect()` のレイアウト時キャッシュ、`resizeEvent`/`setupScreenLayout` の clip 更新重複
- Linux フィルタ poll ウェイクアップ調律、mac GCMouse ハンドラ専用キュー化

### V6-W6. ドキュメント・監査の小規模ドリフト（実測）

| 対象 | 証拠 |
|---|---|
| `MelonPrimeHudRenderCrosshairFx.inc`（462 行）が正典の所有表に無い | [MelonPrime refactoring history](../../../architecture/history/melonprime-refactoring.md) §12/§18.2 の HUD 断片表に行が無い。include 親は `HudRenderDraw.inc:800`（`.inc` が `.inc` を include するネスト断片の初出だが、所有検査は PASS） |
| scatter 予算の余剰 | 実測 24/30。V3 リテラル予算と同じラチェット思想なら **24 に固定**すべき（増加余地 6 を放置しない） |
| V5 completed 文書の進捗表 | pending 6 項目が残ったまま completed/ に居る。Phase 0 完了時に確定値で書き戻す必要 |
| upstream マーカー微増 | EmuThread.cpp 48→**53**（V5 プローブ挿入分）/ Screen.cpp 29→**32** / Screen.h 15→**16**。V6 では非対象（upstream マージ予定なし）だが数値は記録する |
| `kTranslations` 707 行（V3 時 675） | 自然増。重複 5 件（V3 Phase 5 調査済み・文脈違いで統合不可）— 対応不要と再確認のみ |

### V6-W7. 監査済み・白（触らないことを明記）

- 非 canonical リテラル 1 / inc 所有 / HUD スキーマ / config デフォルト監査 — **全 green 実測**
- パッチ per-ROM ローカルテーブル 13 モジュール — V2 Phase 4 で shared `LIST_*` に載せる基準
  （mphCodex で同一関数と証明できたもののみ）を確立済み。残りは**意図的に module-local**
- `NoPickingUpSpecificItems` / `FixWifi` の独自ステートマシン — V1/V2 で意図的と文書化済み
- static mutable state の instance 所有化 — ロードマップ Phase 9 で**不採用確定**（マルチインスタンスは別プロセス）
- `.inc` → `.cpp` 変換 — **不採用確定**（unity 断片規約と衝突）

---

## 2. 不変条件 — 絶対に壊さないもの

**V1 §2.1 do-not-touch リスト / V2–V4 §2 / V5 §3 を全文継承**。V6 固有の追加:

1. **検証ファースト**: Phase 3（HUD element cache）と Phase 5（per-frame 縮減）は、
   Phase 0 の ROM 実測数値と（Phase 3 は）Phase 2 の golden-hash green が揃うまで着手禁止。
   数値が「効果なし」を示したら実装せず Rejected 表に記録して閉じる（歴代の流儀）。
2. **HUD 出力の 1bit 不変**: element cache / layer 分離は `CustomHud_Render` の出力画素を
   1bit も変えない。変える最適化はスコープ外として分離。判定は Phase 2 ハーネスの
   pixel-hash 一致で機械化する。
3. **Phase 4 の純移動規律**: コールドライフサイクルの TU 分離は関数本体の**物理移動のみ**。
   `MelonPrime.h` のメンバ宣言順・`RunFrameHook` の分岐構造・呼び出し順・`COLD_FUNCTION` /
   `HOT_FUNCTION` 指定は 1 文字も変えない。移動後に Windows 側のコード生成が変わらないことを
   関数単位 diff で確認（インライン境界に影響しないことが条件）。
4. Phase 2 ハーネスは `MELONPRIME_ENABLE_DEVELOPER_FEATURES` ゲート内。リリースバイナリに
   シンボル・文字列を 1 つも残さない（S22 と同じ検査を適用）。
5. ゲーム ROM・スクリーンショット等の資産をリポジトリにコミットしない。golden は
   **ハッシュ値（テキスト）のみ**をチェックインする。
6. 検証 3 系統: Windows CI / mac ローカル / Linux VM（[../../../development/build/linux-vm.md](../../../development/build/linux-vm.md)、
   マウス統合オフ必須）。ビルドは各 OS の正規手順（[../../../development/build/overview.md](../../../development/build/overview.md)）。
7. `git reset --hard` 等の破壊的操作禁止。フェーズ = ブランチ、ロールバック = revert。
   1 コミット ≦ 約 400 行 diff 目安。

---

## 3. スモークチェックリスト

**V1 S1–S12 / V2 S13–S15 / V3 S16–S17 / V4 S18–S20 / V5 S21–S22 を全て継承**。V6 追加:

| # | 項目 | 確認内容 |
|---|---|---|
| S23 | HUD golden hash | Phase 2 ハーネスのハッシュスイートが全ケース一致（フォント 3 モード × 主要 HUD 要素 ON/OFF × 2 スケール以上）。Phase 3 の各コミットで必須 |
| S24 | 基準値の再現性 | `MELONPRIME_PERF=1` の同条件 2 回計測で p50/p99 の差が ±10% 以内（計測自体のノイズ確認。Phase 0 で 1 回だけ実施） |

**Phase 3 は S9 / S13 / S16 / S21 / S23 必須。Phase 4 は S2 / S6 / S7 / S8 必須。**

---

## 4. フェーズ計画

### Phase 0: V5 検証負債の完済（1–2 日 + ユーザー実機時間 / リスクなし / **絶対の先頭**）

| 項目 | 内容 |
|---|---|
| 0-1 | **計測手順書の整備**: 3 プラットフォーム別に「dev ビルド → `MELONPRIME_PERF=1` 起動 → in-game 10 分ソーク（対戦 or bot 相手）→ `summarize-melonprime-perf.py` 集計 → 記録」の 1 ページ手順を `docs/development/performance/baseline-procedure.md` としてチェックイン。AI が準備できる部分（ビルド、ログ解析、表の更新）と、ユーザー実機が必要な部分（ROM 起動・プレイ）を明確に分離する |
| 0-2 | **基準値取得**: macOS（ローカル）→ Windows（実機）→ Linux VM の順で p50/p95/p99/max + 区間別（input/RunFrame/draw/limiter-sleep/limiter-spin）+ カウンタ（入力ソース比率 / warp 回数 / `Patches_Apply(OutOfGameFrame)` 回数 / `OsdColor` 実 write 数 / HUD dirty 面積 / GL upload バイト / DR3 skip 回数 / `CustomHud_Render` 所要）を取得。S24 で再現性を 1 回確認 |
| 0-3 | **V5 Phase 3 の事後検証**: 非 Win margin 0.5ms の妥当性を 0-2 のヒストグラムで判定（フレームタイム分散が Windows 同等以下か）。悪化していれば margin を戻す（1 行 revert） |
| 0-4 | **S21/S22 の消し込み**: Windows/Ubuntu CI のリリース構成で perf プローブ痕跡ゼロを再確認（mac は V5 で確認済み）。S18/S19/S20 の実機スモークをユーザーと実施し結果を記録 |
| 0-5 | **V5 completed 文書の確定**: 取得した基準値を V5 の Phase 0 表に書き戻し、進捗表の pending を「完了 / 計測により不採用」の確定状態へ更新 |

**成果物:** 3 プラットフォーム基準値表（本ファイル末尾 §8）+ V5 文書確定。コード変更は 0-3 の revert 可能性のみ。
**DoD:** §8 の表が全行埋まる。以後の Phase 3/5 のゲートが開く。

---

### Phase 1: 監査・ドキュメント衛生（0.5 日 / リスク極小）

| 項目 | 内容 |
|---|---|
| 1-1 | `melonprime-refactoring.md` §12/§18.2 の HUD 断片表に `MelonPrimeHudRenderCrosshairFx.inc`（親: `HudRenderDraw.inc`）を追記。「`.inc` が `.inc` を include するネスト断片」の規約（親は 1 ファイル、所有検査対象）を repo-architecture.md に 1 段落明文化 |
| 1-2 | scatter 予算を 30 → **24** にラチェット（実測値固定）。Windows/Ubuntu CI の予算値も同時更新 |
| 1-3 | upstream マーカー数の現在値（EmuThread 53 / Screen 32 / Window 20 / Config 17 / EmuInstance.h 16 / Screen.h 16 / EmuInstanceInput 13）を non-melonprime-upstream-diff.md に記録（削減はしない — マージ予定なし） |
| 1-4 | `kTranslations` 重複 5 件の「統合不可（文脈違い）」判定を再確認し、V3 Phase 5 ノートに現在行数（707）を追記 |

**DoD:** 全監査 green（literal 1 / inc PASS / scatter 24/24）+ リンクチェック。ビルド不要（ドキュメント + スクリプト定数のみ）。

---

### Phase 2: HUD ゴールデン描画ハーネス（1.5–2.5 日 / リスク低中 / **本丸 A の土台**）

**狙い:** ROM 不要・決定的な HUD 描画テストを作り、S9/S13/S16 の「目視一致」を機械判定に
格上げする。Phase 3 の element cache はこれが green であることを着手条件とする。

#### 2a. 実現可能性スパイク（0.5 日、コード変更なしで判定）
- `DrawEditHudPreview()` が既に「合成状態 + 制御 RAM」で runtime Draw 群を駆動している経路を
  読み解き、**Draw 層ハーネス**（`CachedHudConfig` + 合成 RAM バッファ + `QImage`/`QPainter` で
  `Draw*` を直接呼ぶ）に必要な依存を列挙する。`CustomHud_Render` 全体（`EmuInstance*` 依存）を
  駆動する案は、依存が重ければ**採らない**（Draw 層で 2 の目的は達成できる）
- フォント決定性の確認: MPH バンドルフォント（HudFontMode=0）はプラットフォーム非依存の
  はず — 3 OS でハッシュが一致するかをスパイクで確認。**不一致なら golden はプラットフォーム
  別に持つ**（判定を諦めない）

#### 2b. ハーネス実装
- 新規 `MelonPrimeHudGoldenHarness.cpp`（developer-only TU、`MELONPRIME_ENABLE_DEVELOPER_FEATURES`
  ゲート、CMake 追加可）。隠し CLI フラグ（例: `--melonprime-hud-golden <out.txt>`）または
  dev ビルド専用の環境変数トリガで、定義済みケース群をレンダリングし FNV-1a ハッシュ
  （OPT-DR3 の `MelonPrimeHud_HashImageRegion` を流用）をテキスト出力して終了する
- ケース行列（初期）: フォント 3 モード × {HP+ゲージ, 武器+アイコン+弾薬, クロスヘア(通常/ズーム遷移中/スコープ), マッチステータス, ランク/タイム, ボム, レーダー枠} × スケール {100%, 200%} × outline ON/OFF
- golden ファイル（ハッシュのみ）を `src/frontend/qt_sdl/tests/`（または `tools/` 配下の
  データとして）チェックインし、`tools/run-hud-golden.{sh,ps1}` で「実行 → diff」を 1 コマンド化

#### 2c. CI 併設
- Windows/Ubuntu CI の監査ジョブにハーネス実行を追加（dev ビルドが必要なため、ビルド後ステップ）。
  実行時間が問題になる場合はケースを縮小して nightly でなく PR 毎に耐える規模に調整

**DoD:** 3 OS でハーネス green + S22 相当（リリース構成に痕跡ゼロ）+ 既存挙動不変
（ハーネスは追加専用で、既存レンダラを 1 行も変えないこと）。
**期待効果:** 以後の HUD 変更全てに回帰網。S9/S13/S16 の一部が自動化される。

---

### Phase 3: HUD element-level render cache / static-dynamic layer 分離（2–4 日 / リスク中 / **本丸 A・二重ゲート**）

**着手条件（両方必須）:**
1. Phase 0 計測で `CustomHud_Render`（draw 区間）が支配的コストであることが数値で確認できる
2. Phase 2 ハーネスが green

数値が (1) を否定したら、**実装せず**「計測済み・効果なし」として §6 に記録して閉じる。

| 項目 | 内容 |
|---|---|
| 3-1 | 要素ごとの render signature 導入（値+色+フォント+outline+scale+アンカー解決結果）。署名一致なら QPainter 再描画をスキップし、前回の描画済み層を再利用 |
| 3-2 | static 層（枠 / ラベル / 固定アイコン / レーダー枠）と dynamic 層（数値 / crosshair / radar crop / timer）の分離。crosshair は既存の game-frame キー機構（OPT-ZOOM1）を尊重して別扱い |
| 3-3 | dirty rect / `s_drawnDirtyPx` / OPT-DR3 hash skip との整合: キャッシュヒット時も dirty 計算が正しい合成矩形を返すことをハーネスで検証 |
| 3-4 | edit mode は対象外（毎フレーム全要素 bounds が必要）。invalidation owner を [../../../architecture/performance.md](../../../architecture/performance.md) の台帳に**同コミットで**追記 |

**DoD:** S23（golden 全一致 = 出力 1bit 不変の機械証明）+ S9/S13/S16 目視 + Phase 0 比の
draw 区間 before/after を数値で記録（`MELONPRIME_PERF=1`）+ resize / config 変更 / TOML import /
editor 終了後の stale なし。
**期待効果:** 高解像度フォント・大型スコープ時の CPU 大幅減（Phase 0 実測で定量化して確定）。

---

### Phase 4: MelonPrimeCore コールドライフサイクル分離（1–1.5 日 / リスク低中 / **本丸 B**）

**狙い:** V6-W4。`MelonPrime.cpp`（976 行）からコールドライフサイクル群を純移動で分離し、
`RunFrameHook` 周辺のホットコードと構造的に隔離する。計測不要（出力・挙動・ホットパス不変が
条件のため）だが、Phase 0 の後に置くのは基準値との比較で「巻き込まれ」を検出できるようにするため。

| 項目 | 内容 |
|---|---|
| 4-1 | 新規 `MelonPrimeLifecycle.cpp`（通常 TU、CMake 追加）へ純移動: `OnEmuStart` / `OnEmuStop` / `OnEmuPause` / `OnEmuUnpause` / `ResetRuntimeStateForBoot` / `ApplyConfigReload` / `ReloadConfigFlags` / `Initialize` / デストラクタ。**`HandleGameJoinInit` / `HandleBattleRuntimeEnter` は移動対象から除外**（`RunFrameHook` と同一 TU にあることでの LTO/インライン境界を変えないため。移動する場合は単独コミット + Windows 逆アセンブリ比較を必須とする） |
| 4-2 | `RunFrameHook()`（243 行）本体は**分割しない**。セクションバナー（V3 Phase 5 で付与済み）の粒度確認のみ |
| 4-3 | `MelonPrime.h` は宣言の移動ゼロ（メンバ順不変の原則）。include 追加のみ許可 |
| 4-4 | 移動後の検証: Windows CI + mac ビルド + `RunFrameHook` を含む TU のコード生成が不変であること（`nm` シンボル比較 or 逆アセンブリの該当関数比較） |

**DoD:** フル configure ビルド 3 系統 + S2 / S6 / S7 / S8 + コード生成比較。
**期待削減:** `MelonPrime.cpp` 976 → 600 行前後。ホット TU の再コンパイル範囲縮小。

---

### Phase 5: per-frame 再評価の縮減（1–2 日 / リスク中 / **計測ゲート**）

V6-W5 の候補を **Phase 0 の実測カウンタで選別**してから実装する。効果が数値で出ないものは
Rejected として §6 に記録（実装しない）。

| 候補 | ゲートとなる計測値 | 実装内容（採用時） |
|---|---|---|
| OutOfGameFrame site view | `Patches_Apply(OutOfGameFrame)` 呼び出し数 × 実コスト | site 別 constexpr view でループを短縮。self-guard 契約は不変 |
| `OsdColor_ApplyOnce` edge 化 | per-frame 実 write 数（0 write 率） | pattern B の「ゲームが上書きする」性質上、**安全と証明できた場合のみ** epoch/edge 化。不能なら理由コメント固定で現状維持 |
| RAM read 予算 | ZoomStatus / DamageNotify の read 数 | Weavel proxy read の分岐前倒し等。[zoom-status-performance.md](../../../features/zoom-status-performance.md) の読み戻し禁止規律と突合 |
| `getScreenWidgetRect` キャッシュ | 呼び出し頻度（レイアウト外での呼び出しがあるか） | レイアウト時キャッシュ化 + clip 更新重複の整理 |
| Linux poll / GCMouse queue | 配送ジッタ（Phase 0 の input 区間分散） | 有意差がある場合のみ、プラットフォーム別・単独コミット |

**DoD:** 採用項目ごとに before/after 数値をコミットメッセージに記載 + 該当スモーク
（S6/S7 はパッチ系、S18–S20 は入力系）+ invalidation 台帳更新。

---

### Phase 6: 小粒 declutter（0.5 日 / リスク極小 / 任意）

- Phase 0–5 の作業中に見つかった stale コメント・微細ドリフトの掃除（`git diff -w` 小）
- 呼び出しゼロ公開 API の機械的再スイープ（V2 1-4 / V3 1-3 と同じ基準: 機械確認できるもののみ）
- `MelonPrimePerfProbe.h`（455 行）のカウンタに Phase 3/5 で不要になったものがあれば整理

**DoD:** ビルド + 監査 green。

---

### Phase 7: ドキュメント整備・最終計測・ラチェット固定（0.5 日 / リスクなし）

1. [MelonPrime refactoring history](../../../architecture/history/melonprime-refactoring.md) に「Structural Refactor V6 / Round 11 — Measured」節を追記（Phase 3/5 の実測値表）
2. [../../../architecture/performance.md](../../../architecture/performance.md) の per-frame syscall 予算表と invalidation 台帳を V6 後の姿に更新。**フレームタイム基準値表（3 プラットフォーム）を正典に固定** — 以後の性能 PR は基準値との比較添付が必須というラチェット
3. Phase 2 ハーネスの運用ルール（golden の更新は「意図的な見た目変更」コミットでのみ許可、更新時は before/after スクリーンショット添付）を build.md に追記
4. CLAUDE.md / rules README 更新、本ファイルを `completed/` へ移動
5. 最終計測: LOC / 監査値 / exe・バイナリサイズ / 3 系統 CI green

---

## 5. リスクと対策

| リスク | 該当 | 対策 |
|---|---|---|
| ROM ソークがユーザー時間に依存し Phase 0 が停滞する | 0 | 0-1 の手順書で AI 準備とユーザー実行を分離。mac ローカルだけでも先行取得し、Windows/Linux は「取得待ち」を明示したまま Phase 1/2/4（計測非依存フェーズ）を先行させる |
| フォントラスタライズの OS 差で golden が一致しない | 2 | 2a スパイクで先に判定。不一致ならプラットフォーム別 golden（3 ファイル）で運用。それでも不安定なら「同一プラットフォーム内の before/after 比較」に限定して回帰検知の価値は維持 |
| element cache の signature 漏れで HUD stale | 3 | S23（golden）+ resize/config/TOML/editor の 4 系統 invalidation テストをケース行列に含める。invalidation owner の台帳追記を DoD 化 |
| キャッシュ導入で dirty rect が過小になり描き残し | 3 | 3-3 で「キャッシュヒット時の合成矩形」をハーネス検証。dirty 系は OPT-DR1..DR3 の既存規律に従い owner を明記 |
| ライフサイクル TU 分離でインライン境界が変わり性能退行 | 4 | 移動対象をコールド関数（`COLD_FUNCTION` 済み）に限定し、join/battle-enter は除外。コード生成比較を DoD に含め、差が出たら revert |
| `OsdColor` edge 化が pattern B の再書き込みを取りこぼす | 5 | 実 write カウンタで「ゲームが上書きする頻度」を先に計測。ゼロ保証できなければ現状維持を正式決定として記録 |
| ハーネス/計測コードのリリース混入 | 2, 0 | S22 の strings/nm 検査を Windows/Ubuntu にも適用（0-4）。ハーネスは developer-only TU |
| 「最適化したい病」で do-not-touch へ踏み込む | 全 | V1 §2.1 リスト該当は本計画で扱わない（変更前チェックを Phase ごとの DoD に含める） |

---

## 6. 進捗トラッキング

| Phase | 内容 | 状態 | 完了日 | 結果メモ |
|---|---|---|---|---|
| 0 | V5 検証負債の完済（基準値・S21/S22・V5 確定） | 一部完了 / Windows・S24 待ち | 2026-07-04 | 0-1 完了。**0-2 macOS/Linux VM 完了**（ログ名・p50・draw・入力 backend 記録済み）。**0-5 部分完了**: mac/Linux を V5 completed Phase 0 表へ書き戻し。**S24 未確認**（mac 1 本のみ — 同条件 2 回目 pending）。**OSD 設定 OFF の可能性** — `OsdColor` 実 write は Phase 5 参考値。Windows baseline / 0-3/0-4 / V5 確定は pending |
| 1 | 監査・ドキュメント衛生（CrosshairFx 表記・scatter 24 ラチェット） | 完了 | 2026-07-04 | CrosshairFx 所有表/ネスト `.inc` 規約追記、scatter 24/24 に固定、Windows/Ubuntu CI 更新、upstream marker snapshot と kTranslations 707行/重複5件を記録 |
| 2 | HUD ゴールデン描画ハーネス（本丸 A 土台） | 完了 | 2026-07-04 | Developer-only `--melonprime-hud-golden` 追加、macOS dev build で golden一致、release CI に harness痕跡ゼロ検査を追加 |
| 3 | HUD element cache / layer 分離（本丸 A・二重ゲート） | 未着手 / **Rejected 候補** | — | mac: draw p50=0.732ms / `CustomHud_Render` p50=219.6µs → run 非支配、**不採用方向**。Linux VM draw は llvmpipe 参考のみ（ゲート非使用）。**Windows baseline 取得後に §6 Rejected へ移動して確定** — 取得前に Rejected 確定しない |
| 4 | コールドライフサイクル TU 分離（本丸 B） | 実装完了 / 実機スモーク待ち | 2026-07-04 | `MelonPrimeLifecycle.cpp` 追加。`OnEmuStart` / stop / pause / unpause / boot reset / config reload / config flags / initialize / destructor を純移動し、`HandleGameJoinInit` / `HandleBattleRuntimeEnter` / `RunFrameHook` は `MelonPrime.cpp` に保持。`MelonPrime.cpp` 976→683行。macOS dev/release build、scatter 22/22、inc ownership、literal budget、diff check、nm 所在確認は通過。S2/S6/S7/S8 は ROM 実機操作待ち。**スモークブロッカー（2026-07-04）:** macOS InputConfigDialog Cancel で `restoreVisualSnapshot()` が stale `ui->cbMetroidEnableCustomHud` 等を触り `EXC_BAD_ACCESS (SIGBUS)` — `m_hudWidgets` を `QPointer` 化、`visualSnapshotTargetsAlive()` で破棄済み no-op。**修正済み・Cancel 再スモーク待ち**（設定保存仕様は不変） |
| 5 | per-frame 再評価の縮減（計測ゲート） | 未着手 | — | mac カウンタで候補選別可能だが **Windows baseline 待ちが安全**。`OsdColor_ApplyOnce` 実 write（mac 0.1/分）は **OSD OFF ソークの可能性** — edge 化判断には OSD ON 追加ソークが必要 |
| 6 | 小粒 declutter（任意） | 完了 | 2026-07-04 | `MelonPrime.cpp` の未使用 `Platform.h` include を機械的に除去。macOS dev build、scatter 22/22、inc ownership、literal budget、diff check 通過 |
| 7 | ドキュメント + ラチェット固定 | 固定可能分完了 / 実測待ち | 2026-07-04 | scatter 22/22 にラチェット、Windows/Ubuntu CI 更新、HUD golden 運用ルールを build.md に追記、performance/refactoring/repo docs に V6 現況と未完了ゲートを記録。mac / Linux VM 基準値 §8 反映済み。**V5 completed Phase 0 表へ mac/Linux 書き戻し済み**。Windows 表・S24・Phase 0/S21/V5 最終確定は Windows baseline 後 |

### macOS Phase 0 ソーク（2026-07-04 08:24 JST）

| 項目 | 値 |
|---|---|
| ログ | `artifacts/perf-baseline/macos-perf-20260704-082454.log` |
| 集計 | `artifacts/perf-baseline/macos-perf-20260704-082454.summary.txt` |
| 入力 | GCMouse（`[MelonPrime] mac input: GCMouse backend`） |
| OSD 設定 | **OFF の可能性あり** — 下記 `OsdColor` 実 write は Phase 5 edge 化判断に使わない |
| フレーム数 | shutdown histogram **8192** frames（1 Hz window ×640） |
| 区間 p50 (ms) | sleep=5.785 / spin=0.366 / input=0.005 / **run=9.809** / **draw=0.732** |
| Phase 3 所見 | draw + CustomHud（~0.22ms）≪ run — element cache は **効果なし前提で Rejected 候補**（Win でも確認後確定） |

### Linux VM Phase 0 ソーク（2026-07-04 08:57 JST）

| 項目 | 値 |
|---|---|
| ログ | `artifacts/perf-baseline/linux-vm-perf-20260704-085743.log`（端末出力から復元 — 共有フォルダ `artifacts/` への tee が Permission denied） |
| 集計 | `artifacts/perf-baseline/linux-vm-perf-20260704-085743.summary.txt` |
| 入力 | XInput2 RawMotion（`[MelonPrime] linux input: XInput2 RawMotion active`） |
| OSD 設定 | **OFF の可能性あり**（mac と同条件想定） — `OsdColor` 実 write は参考値のみ |
| GL | Mesa **llvmpipe**（ソフトウェアレンダラ — VM 固有、ネイティブ Linux GPU とは不可比） |
| FPS 目安 | **かなり低い** — p50=49.2ms ≒ **~20 fps 相当**（mac p50=16.7ms ≒ ~60 fps と比較）。VirtualBox ゲスト + CPU ソフト GL のため **VM 上実行の影響が支配的**。ネイティブ Linux 実機の性能指標ではない |
| フレーム数 | shutdown histogram **6497** frames（1 Hz window ×317） |
| 区間 p50 (ms) | sleep=0.000 / spin=0.000 / input=0.010 / **run=34.024** / **draw=16.929** |
| Phase 3 所見 | draw が mac の ~23 倍 — **llvmpipe 起因の VM アーティファクト**。element cache 判断は mac 基準値を正とし、Linux VM は参考値のみ |
| スクリプト修正 | `collect-perf-baseline.sh`: `artifacts/` 非書き込み時は `~/.local/share/melonprime-perf-baseline` → `/tmp/...` へ fallback |

### Phase 0 残タスク（2026-07-04 時点）

| 項目 | 状態 |
|---|---|
| macOS baseline | ✅ 取得済み（1 本目） |
| Linux VM baseline | ✅ 取得済み（llvmpipe VM 参考値。Phase 3/5 性能ゲート非使用） |
| Windows baseline | ❌ 未取得 — **Phase 0 閉鎖・S21・V5 最終確定の前提** |
| S24 再現性 | ❌ 未確認 — mac 同条件 **2 回目** が必要（`compare-perf-repro.py` で p50/p99 ±10%） |
| OSD 条件 | ⚠️ mac/Linux ソークは **OSD OFF の可能性**。frame time / draw / CustomHud / input は有効。`OsdColor` edge 化判断には **OSD ON 追加ソーク**（または Windows と同 OSD 設定で比較）が必要 |
| V5 completed 書き戻し | ✅ mac/Linux を Phase 0 表へ反映。Windows は pending のまま |
| Phase 3 Rejected 確定 | ⏳ Windows baseline 後（現状は Rejected **候補**のみ） |

### 計測により不採用（Rejected — Phase 3/5 で数値が効果なしを示した場合にここへ記録）

**Phase 3 HUD element cache — Rejected 候補（Windows baseline 取得後に確定）**

- macOS: draw p50=0.732ms、`CustomHud_Render` p50=219.6µs ≪ run p50=9.809ms → HUD CPU cache は **効果なし前提**
- Linux VM: draw 高値は llvmpipe VM アーティファクト — **判断材料にしない**
- Windows: 未取得 — mac だけでは最終 Rejected に移さない（Windows で draw/CustomHud が支配的なら再検討余地あり）

---

## 7. 推奨着手順序

```
0 (検証負債・絶対の先頭) → 1 → 2 (土台) → 4 (計測非依存・0 の実機待ち中に可) → 3 (本丸A) → 5 → (6) → 7
```

- **0 が絶対の先頭**。ただし 0-2 の Windows/Linux 実機ソークはユーザー時間に依存するため、
  mac 基準値の取得後は 1 / 2 / 4（計測非依存フェーズ）を並行して進めてよい
- 3 は「0 の draw 区間実測」と「2 の green」の両方が揃うまで着手禁止
- 5 は 0 のカウンタ実測が揃い次第、候補ごとに独立判断
- upstream 統合点の削減はマージ予定が生じるまで恒久スキップ（V1–V5 と同じ判断）

総見積り: **必須フェーズ（0–4, 7）で約 7–10 日相当 + ユーザー実機ソーク時間**。
各フェーズは独立コミット列なので中断・再開自由。

---

## 8. Phase 0 基準値

developer ビルドで `MELONPRIME_PERF=1` を付けて起動し、in-game 10 分ソーク後に
`python3 tools/perf/summarize-melonprime-perf.py perf.log` で集計する。詳細手順は
[perf-baseline-procedure.md](../../../development/performance/baseline-procedure.md) に固定。

**計測条件:** macOS / Linux VM の初回ソークは **OSD 設定 OFF の可能性**あり。
3 プラットフォーム横比較（S21）は **同一 OSD 設定**で取得すること。
OSD ON 時の `OsdColor` 負荷を見る場合は **別 baseline** として明記する。
**クラッシュ終了したソークログは正式 baseline に使わない**（histogram が途中打切り・終了処理未完了のため）。
正常終了（Quit / ダイアログ OK で閉じた計測）のみ §8 表へ反映する。

| プラットフォーム | p50 | p95 | p99 | max | draw 区間 p50 | 備考 |
|---|---:|---:|---:|---:|---:|---|
| macOS | 16.689 | 22.829 | 24.929 | 173.172 | 0.732 | 2026-07-04 soak、GCMouse、8192 frames。**OSD OFF の可能性** — frame/draw/CustomHud/input は有効。区間 p50 (ms): run 9.809 / sleep 5.785 |
| Windows | 未取得 | 未取得 | 未取得 | 未取得 | 未取得 | mac/Linux と比較するなら **同一 OSD 設定**で取得 |
| Linux VM | 49.216 | 103.792 | 141.452 | 290.728 | 16.929 | **VM 低 FPS 参考値**（p50 ≒ 20 fps、llvmpipe）。Phase 3/5 性能ゲート非使用。OSD OFF の可能性。入力 path（XInput2 RawMotion）確認としては有効 |

カウンタ基準値（同ソークから）:

| カウンタ | macOS | Windows | Linux VM | 備考 |
|---|---:|---:|---:|---|
| `Patches_Apply(OutOfGameFrame)` / 分 | 166.9 | — | 137.6 | Linux VM 列は **VM 低 FPS 環境の参考値**（Phase 3/5 ゲート判定には使わない） |
| `OsdColor_ApplyOnce` 実 write / 分 | 0.1 | — | 0.4 | **OSD OFF ソークの可能性 — Phase 5 edge 化判断に使わない**（参考値のみ。apply: mac 3398.3/分、Linux VM 1026.6/分）。OSD ON 追加ソークが必要 |
| HUD dirty 面積 平均 px/frame | 80580.9 | — | 75831.6 | |
| GL upload バイト / frame | 161184.4 | — | 151663.1 | Linux VM: llvmpipe + VM 低 FPS — 参考値のみ |
| DR3 hash skip / frame | 0.0 | — | 0.0 | |
| `CustomHud_Render` p50/p99 µs | 219.6 / 381.2 | — | 338.6 / 3064.5 | mac: draw 非支配。Linux VM: VM 低 FPS で p99 スパイク大 |

### Phase 0 スナップショット（計画作成時 2026-07-04・HEAD `efbe6b82`）

- `MelonPrime*` LOC（.cpp/.h/.inc/.mm）: **33,090**
- 主要ファイル: HudPropSchema.inc 2,333 / HudRenderDraw.inc 1,438 / Localization.cpp 1,337 /
  HudConfigOnScreenDraw.inc 1,320 / InputConfig.cpp 1,160 / HudRenderConfig.inc 1,082 /
  HudRenderRuntime.inc 1,022 / MelonPrime.cpp 976 / HudConfigOnScreenEdit.cpp 924 /
  CustomHudBuild.inc 810 / OnScreenInput.inc 757 / RawInputState.cpp 730 / MelonPrime.h 714 /
  GameInput.cpp 677 / HudPreviews.inc 649 / CrosshairFx.inc 462 / PerfProbe.h 455
- `RunFrameHook()`: 243 行（MelonPrime.cpp:537-779）
- 監査: 非 canonical リテラル **1** / inc 所有 **PASS（51）** / scatter **24/30** / TODO 系 **0**
- `.inc` 断片 51 / パッチ関連ファイル 42（うち .cpp 16）/ per-ROM ローカルテーブル保持 13 モジュール
- upstream `MELONPRIME` マーカー: EmuThread 53 / Screen 32 / Window 20 / Config 17 /
  EmuInstance.h 16 / Screen.h 16 / EmuInstanceInput 13 / EmuInstance.cpp 5
- `kTranslations` 707 行
