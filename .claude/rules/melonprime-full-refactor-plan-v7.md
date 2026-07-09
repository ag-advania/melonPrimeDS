# MelonPrime 全面リファクタリング計画 V7（Phase 0–6）— SRP完成・残債の完済・検証は最後

**作成日:** 2026-07-09
**対象ブランチ:** `highres_fonts_v3`（HEAD `c01de360` 時点で実測）
**ステータス:** 進行中（Phase 0 完了、2026-07-09）
**前提:** [V1](completed/melonprime-full-refactor-plan.md) / [V2](completed/melonprime-full-refactor-plan-v2.md) /
[V3](completed/melonprime-full-refactor-plan-v3.md) / [V4](completed/melonprime-full-refactor-plan-v4.md) /
[V5](completed/melonprime-full-refactor-plan-v5.md) / [V6](melonprime-full-refactor-plan-v6.md) /
**SRP refactor v3**（2026-07-08〜09、PR #520 + 後続 Batch/Phase 群 — 監査済み 2026-07-09、下記 §1）の後継。

**運用方針（ユーザー指示 2026-07-09）:** 手動スモークテストと GitHub Actions CI の全マトリクス確認は
**最終 Phase（Phase 6）に一括で後回し**とする。各 Phase の DoD は「AI による差分監査 + ローカル監査
スクリプト green + ローカル macOS ビルド green」で閉じる。ROM 実機・Windows/Linux 実機・CI は
Phase 6 でまとめて消化する。

---

## 0. 最重要の前提認識 — 構造負債はほぼ完済済み。V7 は「仕上げ」である

V1–V6 + SRP v3 で以下は**解消済み・監査 green を実測確認済み**（2026-07-09）:

- パッチレジストリ + `PatchLifecycle` ゲートウェイ（Sites A/B/D/E + emu start/stop/boot/config-reload/ROM-detect）
- ROM アドレス X-macro / HUD プロパティスキーマ / OSD カラースキーマ / リテラル予算 1
- `RuntimeConfigSnapshot` / `AimConfigSnapshot` / `AimSensitivitySnapshot` の Load/Apply 境界
- `InputProjection`（ProjectDownState / ProjectPressMask / MoveLUT のヘッダ化、ビットパッキング不変）
- `ScreenCursorPolicy`（クリップ/ワープ/解放の単一所有）・`ColorDialogPrefs`・HUD Editor `FormBuilder`
- プラットフォーム入力ファサード / HUD ジオメトリ共有 / 計測基盤（PerfProbe）/ HUD golden ハーネス
- ローカライゼーション TU 分割 + key/value 化 + 監査（`MelonPrimeTranslations.inc` の
  行フォーマット問題も 2026-07-08 の再フォーマットで解消済み — 1 行 1 エントリ、監査 green）

**したがって V7 で「大規模な構造手術」は不要**。残っているのは:

1. **SRP の最後の穴** — HUD エディタ側パネルの `populate*()` 手書き列挙（V2 Phase 2d で
   Option A として意図的に見送った箇所。FormBuilder 導入で前提が変わった）
2. **神クラス表面の残り** — `RunFrameHook()` 243 行 / `MelonPrime.h` 725 行（ただし
   do-not-touch 制約下なので「命名と節化」まで。分割はしない）
3. **計測ゲートで凍結中のパフォーマンス残債**（V6-W5 — Windows 基準値待ち）
4. **小粒のデッドコード・API 疣**（SRP v3 監査で検出した非ブロッカー 3 件を含む）
5. **ドキュメント・スクリプトの散乱**（SRP v3 の設計/進捗ドキュメント 6 本が
   `.claude/features/` に平置き、i18n 一発物監査スクリプト約 20 本が `.claude/skills/` に堆積）
6. **検証負債**（V6 Phase 0 の Windows 基準値 / S24、SRP v3 の実戦スモーク未実施分）→ Phase 6 へ集約

計測なしの最適化はしない（V5/V6 不変条件の継承）。V6 Phase 3（HUD element cache)は
mac 実測で「draw 非支配」が出ており **Rejected 候補のまま**。Windows 基準値が覆さない限り実装しない。

---

## 1. 直近リファクタ（SRP v3、2026-07-08〜09）の監査結果 — 承認済み

2026-07-09 に全差分（`6caaf971^..c01de360`、50 ファイル +8,716/-5,664）を監査した。結論: **全て承認**。

検証した項目:

| 領域 | 判定 | 根拠 |
|---|---|---|
| `PatchLifecycle` 全 8 API | ✅ 挙動不変 | Sites A/B/D/E・start/stop/boot/reload/ROM-detect の呼び出し順・restore/reset 順が旧コードおよび patch-system.md / BattleFlowState.md の不変条件と一致。boot=state-only 維持 |
| `RuntimeConfigSnapshot` / `AimConfigSnapshot` | ✅ 挙動不変 | 全キー・clamp・developer ゲート・無効化副作用（pending clear / residual reset）が旧 `ReloadConfigFlags` と一致。`ApplyAimAdjustSetting` 削除は正当（`ReloadAimConfigFromTable` に吸収、`RecalcAimFixedPoint` の冗長 2 回呼びが 1 回に — 最終状態同一） |
| `InputProjection.h` | ✅ verbatim 移動 | ビットパッキング・`FORCE_INLINE`・`alignas(64)`・static_assert 全保存。do-not-touch リスト非侵害 |
| `ScreenCursorPolicy` | ✅ 挙動保存 | Win ClipCursor / mac GCMouse ゲート / Linux `resetAimMouseDelta` 全分岐一致。`isFocused` 読みが暗黙 seq_cst → 明示 acquire（GUI コールドパス、問題なし）。BSD 分岐のみ「raw チェック省略で無条件ワープ」だが BSD に raw filter は存在せず等価 |
| `FormBuilder` / `WidgetFactoryContext` | ✅ ライフタイム健全 | 全メンバが panel 寿命超の参照（`cfg()` → `EmuInstance::localCfg` 安定参照を確認）。`populating` は生参照でシグナル時に評価 |
| `ColorDialogPrefs` | ✅ | コールドパス自己完結。`QColorDialog` 直呼び禁止は新監査スクリプトで機械化済み |
| Screen.h アクセサ追加 | ✅ | `#ifdef MELONPRIME_DS` ガード内。friend 結合の縮小は妥当 |
| CMake / inc 所有 / リテラル / scatter / color-dialog / srp-performance 監査 | ✅ 全 PASS | ローカル実行 2026-07-09 |
| ローカライゼーション再フォーマット（`6caaf971`） | ✅ | dialogs `#include` 行残存、監査 green（既存 zh-Hant 用語 WARN 2 件のみ）、1 行 1 エントリ化で diff 可能性回復 |
| macOS ビルド | ✅ | HEAD で `ninja: no work to do`（クリーン tree でビルド済み確認） |

非ブロッカーの検出事項（→ Phase 1 で処理）:

- **A1**: `PatchLifecycle::RestoreForEmuStop` の `romDetected` 引数が未使用（将来ガード予約のコメント付き）。
  使う予定が確定するまで引数ごと削除するか、予約コメントの期限を明記する
- **A2**: `PlatformInput_WarpCursor` が Windows でもコンパイルされるようになった（`QCursor::setPos`
  フォールバック）。現在 Windows から到達する呼び出しは存在しない（`ContainAimCursorIfNeeded` 系は
  全て `__APPLE__` ゲート）が、将来 Windows 側から呼ぶと ClipCursor 規律を静かに迂回する。
  関数コメントに「Windows では ClipCursor が正、ここを呼ぶな」を明記する
- **A3**: SRP v3 完了サマリに残る実戦スモーク未実施項目（Sites A/B の live match 遷移、Windows
  `ClipCursor` / Linux `resetAimMouseDelta` の `ReleaseForClose` 分岐）→ Phase 6 に転記

---

## 2. 無駄の棚卸し（2026-07-09 実測・証拠付き）

### 現状スナップショット（HEAD `c01de360`）

| 指標 | 値 | 備考 |
|---|---:|---|
| `MelonPrime*` LOC（.cpp/.h/.inc/.mm、ローカライゼーション込み） | 116,623 | うちローカライゼーションデータ約 83k |
| 同（ローカライゼーション除くコード） | 約 33,000 | V6 時 33,090 とほぼ同水準（SRP v3 は分割中心で正味微減） |
| `MelonPrime.cpp` / `MelonPrime.h` | 706 / 725 | `RunFrameHook()` は 243 行のまま |
| `MelonPrimeHudConfigOnScreenEdit.cpp` | 697 | FormBuilder 抽出後も `populate*()` 手書き列挙が残存 |
| `InputConfig/MelonPrimeInputConfig.cpp` | 1,192 | V2 目標 1,200 未満だが上限张り付き |
| 監査 | 全 green | literal 1/1・scatter 22/22・inc 55 PASS・color-dialog PASS・srp-performance PASS |

### V7-W1. HUD エディタ側パネル `populate*()` の手書き列挙（SRP の最後の穴・本丸）

- `MelonPrimeHudConfigOnScreenEdit.cpp`（697 行）の `populate*()` 群は、要素ごとに
  `AddBoolRadioRow / AddSpinBoxRow / AddColorPickerRow ...` を**手書きで列挙**している。
- V2 Phase 2d はこれを「Option A: 見送り」とした — 当時はウィジェット生成コードが bespoke で、
  テーブル駆動化の土台がなかった。**SRP v3 の FormBuilder で前提が変わった**: 全行が既に
  「factory 関数 1 呼び出し = 1 行」に正規化されており、`(rowKind, label, key, min, max, items...)`
  のデータ行に機械変換できる形になっている。
- スキーマ（`MelonPrimeHudPropSchema.inc`）には edit-surface メタデータが既にあり
  （`kProps*` / `HudEditPropDesc` はスキーマ生成）、側パネルだけが手書きのまま。
- **注意（V2 の判断を尊重する範囲）**: 側パネルの行順・ラベルは `kProps*` と意図的に食い違う
  （bespoke UX）。テーブル駆動化は「側パネル専用のテーブル」を作る形にし、`kProps*` への統一
  （V2 Option C = 行順が変わる UX 変更）は**やらない**。

### V7-W2. `RunFrameHook()` 243 行と `MelonPrime.h` の混在宣言

- SRP v3 の Deferred リストに「RunFrameHook large split」「MelonPrimeCore hot state struct
  extraction」が明記されている。**V7 でも分割はしない**（分岐構造・メンバ宣言順は do-not-touch）。
- やる価値があるのは可読性の仕上げのみ:
  - `RunFrameHook` 内のコールドブロック（join 判定、match-end poll、focus 遷移）は既に
    セクションコメントで区切られている。**同一 TU 内・`COLD_FUNCTION` 済みヘルパへの純移動**は
    join/battle-enter で実績があるが、残りのブロックは分岐フラグと密結合なので、
    コード生成比較を DoD にできる場合のみ個別判断（デフォルトは現状維持）
  - `MelonPrime.h` はセクションバナーの整備（宣言の並べ替えなし）まで

### V7-W3. 計測ゲート付きパフォーマンス残債（V6-W5 繰越し）

全て **Windows `MELONPRIME_PERF=1` 基準値が前提**（mac/Linux VM は取得済み、V6 §8）:

| 候補 | ゲート計測値 | mac 実測の示唆 |
|---|---|---|
| `Patches_Apply(OutOfGameFrame)` site 別 constexpr view | 呼び出し数 × 実コスト | 166.9 回/分 — レジストリ全周回でも軽量。効果は薄い見込み |
| `OsdColor_ApplyOnce` の edge 化 | per-frame 実 write 数 | mac 0.1/分だが **OSD OFF ソークの疑い** — OSD ON 再計測が先 |
| `getScreenWidgetRect()` レイアウト時キャッシュ | 呼び出し頻度 | 未計測 |
| RAM read 予算（ZoomStatus / DamageNotify） | read 数 | 未計測 |
| HUD element cache（V6 Phase 3） | draw 区間支配度 | mac で **非支配（Rejected 候補）** — Windows が覆さない限り不実装 |

### V7-W4. ドキュメント・スクリプトの散乱

- `.claude/features/` に SRP v3 の設計/進捗/監査ドキュメントが 6 本平置き
  （`melonprime-srp-refactor-v3-progress.md` / `melonprime-srp-v3-completion-summary.md` /
  `melonprime_patch_lifecycle_gateway_step3_plan.md` / `..._site_d_plan.md` /
  `melonprime_aim_config_reload_paths_audit.md` / `..._outcome_c_design_note.md`）。
  完了済みの経緯ドキュメントは `completed/` 系へ、恒久ルール（Load/Apply 境界 =
  `melonprime-srp-performance-contract.md`）は rules へ昇格すべき
- `.claude/skills/` に i18n 一発物監査スクリプト約 20 本（`audit-melonprime-i18n-phase*.py`、
  `...-qualityfix-pass*.py`）— 参照データが未コミットで実行すると FAIL 表示になるもの。
  手法記録としての価値はあるのでアーカイブディレクトリへ移動
- `melonprime-refactoring.md`（正典）に SRP v3 / PatchLifecycle の節が未追記
- V6 進捗表が「Windows 基準値待ち」で open のまま（V7 Phase 6 完了時に閉じる）

### V7-W5. 小粒のデッドコード・API 疣（§1 の監査検出分）

- A1: `RestoreForEmuStop(romDetected)` 未使用引数
- A2: `PlatformInput_WarpCursor` の Windows フォールバック無警告
- `ScreenCursorPolicy.cpp` の `#include <atomic>` は使用確認（`isFocused.load` 用 ✓）— 対応不要
- 呼び出しゼロ公開 API の定期スイープ（V2/V3/V6 と同じ機械基準）を 1 回

### V7-W6. 監査済み・白（触らないことを明記）

- HUD render unity `.inc` 群（Draw 1,438 / Config 1,082 / Runtime 1,022）— 意図的な unity 断片。
  V6 で element cache が Rejected 候補になった以上、性能動機も消えている
- `NoPickingUpSpecificItems` / `FixWifi` の独自ステートマシン（意図的、文書化済み）
- upstream 統合点（EmuThread 53 / Screen 32 マーカー）— マージ予定なし、恒久スキップ継続
- Raw Input 層 / EmuThread フレームループ / エイム数式（V1 §2.1 全文継承）
- PatchLifecycleGateway **Site C（GameJoin）** — SRP v3 で「explicit non-goal」と決定済み。
  `HandleGameJoinInit` 内の直接 `Patches_Apply(PatchSite_GameJoin, ...)` は正当な現状
- Aim reload 統一 outcome B — outcome C（`AimSensitivitySnapshot`）で決着済み
  （`melonprime_aim_config_reload_paths_audit.md`）
- ローカライゼーション**翻訳内容**の品質改善（Zulu/Slovak 等 25-31% 残） — コードリファクタでは
  ないため本計画外（既存の i18n ノートで追跡継続）

---

## 3. 不変条件 — 絶対に壊さないもの

**V1 §2.1 do-not-touch リスト / V2–V4 §2 / V5 §3 / V6 §2 を全文継承**。V7 固有:

1. **スモーク後回しの代償規律**: 各 Phase の DoD は「差分の AI 監査（挙動同値性の明示確認）+
   ローカル監査スクリプト全 green + macOS ビルド green」。**ゲームプレイに触れる差分**
   （パッチ・フック・入力・HUD ランタイム）は、挙動同値を**コード上で証明できる純移動/参照差し替えのみ**許可。
   証明できない変更は Phase 6 のスモークまで待つか、やらない
2. Phase 2（側パネルテーブル駆動化）は**行順・ラベル・widget 型・初期値・シグナル配線の完全保存**。
   変換前後で「生成される行の (label, widgetType, key, range) 列」をスクリプトで抽出し byte 一致を証明
3. Phase 4 は Windows 基準値なしに着手しない（V6 不変条件の継承）。数値が効果なしなら
   Rejected 表に記録して閉じる
4. `RunFrameHook` の分岐構造・`MelonPrime.h` メンバ宣言順・Site C 直呼びは変えない
5. 破壊的 git 操作禁止。1 コミット ≦ 約 400 行 diff 目安。フェーズ = ブランチ、ロールバック = revert

---

## 4. フェーズ計画

### Phase 0: ベースライン固定（0.5 日 / リスクなし）

1. LOC / 監査値 / ビルド確認のスナップショットを本ファイル §7 に記録（§2 の表を正とする）
2. 全ローカル監査スクリプトの green を再確認（2026-07-09 実施済み — 再掲でよい）
3. **（並行・ユーザー依存）** Windows `MELonPRIME_PERF=1` 基準値ソークの依頼を出す
   （Phase 4 のゲート。取得完了まで Phase 4 は凍結、他 Phase は進行可）

**DoD:** 計測値の追記のみ。コード変更なし。

---

### Phase 1: 小粒デッドコード・API 疣の掃除（0.5 日 / リスク極小）

| # | 作業 | 対象 |
|---|---|---|
| 1-1 | A1: `RestoreForEmuStop` の `romDetected` 引数を削除（呼び出し側 1 箇所も同時修正）。将来ガードが必要になったらその時に追加する — YAGNI | `MelonPrimePatchLifecycle.h/.cpp`, `MelonPrimeLifecycle.cpp` |
| 1-2 | A2: `PlatformInput_WarpCursor` に「Windows では ClipCursor が正。Windows から呼ぶ場合はレビュー必須（QCursor::setPos は clip 規律を迂回する）」コメントを付与 | `MelonPrimePlatformInput.h` |
| 1-3 | 呼び出しゼロ公開 API の機械スイープ（grep で定義/呼び出しゼロを確認できるもののみ削除。判断が要るものは触らない） | 各所 |
| 1-4 | i18n 一発物監査スクリプト（phase5a〜10 / qualityfix-pass1〜6 / he-coverage 等、参照データ未コミットのもの）を `.claude/skills/archive/i18n/` へ移動し、README に「手法記録・参照データ非コミットのため直接実行不可」と明記。恒久ゲート（`audit-melonprime-localization.py`）は現位置維持 | `.claude/skills/` |
| 1-5 | スイープ中に見つかる stale コメント修正（`git diff -w` 小） | 各所 |

**DoD:** 監査 green + macOS ビルド green + 差分監査。
**期待削減:** コード -20〜-50 行 + skills 整理。

---

### Phase 2: HUD エディタ側パネルのテーブル駆動化（1.5–2.5 日 / リスク低中 / **本丸**）

**狙い:** V7-W1。`populate*()` の手書き factory 呼び出し列挙を、側パネル専用の行テーブル
（データ）+ 汎用イテレータ（1 関数）に置換する。HUD プロパティ追加時の「側パネルだけ手で足す」
最後のミラーを消す。

#### 2a. 機械抽出（コード変更なし）
- 現 `populate*()` の全 factory 呼び出しを `(element, rowKind, label, keys..., range/items)` の
  表に機械抽出（Python ワンオフ）。bespoke な行（要素専用の特殊 UI、複合行）を「テーブル外残置」
  として分類。**行順・ラベルは現状を 1 文字も変えない**

#### 2b. 行テーブル + イテレータ実装
- `MelonPrimeHudEditorSidePanelRows.inc`（または同等）に行テーブルを定義。キーは既存の
  `MP_HUD_PROP_KEY_*` マクロ参照（リテラル直書き禁止 — literal 予算 1 を維持）
- `populate*()` 本体を「テーブル該当区間のイテレート + テーブル外 bespoke 行の直呼び」に縮約
- FormBuilder の factory シグネチャは不変。`WidgetFactoryContext` のライフタイム契約
  （全メンバ参照・panel 寿命超）を厳守

#### 2c. 同値性証明
- 2a の抽出スクリプトを変換後コードにも適用し、生成行列の byte 一致を確認してからコミット
- スキーマ generator との整合（`generate-hud-prop-schema.py` 再生成 diff ゼロ）

**DoD:** 監査 green + スキーマ再生成 diff ゼロ + 行列 byte 一致 + macOS ビルド green。
目視スモーク（編集モード開閉・各行の操作）は Phase 6 に送るが、**行列一致証明を代替検証とする**。
**期待削減:** `OnScreenEdit.cpp` 697 → 350–450 行。HUD プロパティ追加の触り箇所が
「スキーマ 1 行（+側パネル表 1 行）」に確定。

---

### Phase 3: 可読性の仕上げ — 節化・命名・コメント正典化（0.5–1 日 / リスク極小）

**分割はしない**（V7-W2 の判断）。純粋に読み手のためだけの作業:

| # | 作業 |
|---|---|
| 3-1 | `MelonPrime.h` のセクションバナー整備（hot/cold/lifecycle/config の区画を明示。宣言の移動・並べ替えは**ゼロ**） |
| 3-2 | `RunFrameHook` 内コールドブロックのコメントを PatchLifecycle Site 名（A/B/D/E）と相互参照させる最終パス（SRP v3 で大半済み — 抜け確認のみ） |
| 3-3 | `melonprime-srp-performance-contract.md`（Load/Apply 境界）を `.claude/features/` から `.claude/rules/` へ昇格し、rules README / CLAUDE.md に載せる（恒久ルール化） |
| 3-4 | `ScreenCursorPolicy` / `FormBuilder` / `PatchLifecycle` / `RuntimeConfig` / `InputProjection` / `ColorDialogPrefs` の所有点を `repo-architecture.md` に 1 節で追記（SRP v3 の成果を正典に反映） |

**DoD:** リンクチェック + `git diff -w` がコメント/ドキュメントのみであること。

---

### Phase 4: 計測ゲート付きパフォーマンス残債（1–2 日 / リスク中 / **Windows 基準値ゲート**）

**着手条件:** Phase 0-3 で依頼した Windows `MELONPRIME_PERF=1` ソーク（+ OSD ON 条件）が届いていること。
届かない場合はこの Phase を**スキップし「未計測のため見送り」と記録**（凍結のまま V8 へ）。

V7-W3 の候補を計測値で選別し、採用項目のみ実装。各項目は独立コミット + before/after 数値を
コミットメッセージに記録。`OsdColor` edge 化は pattern B（ゲームが RAM を上書き）のため、
実 write ゼロを保証できない場合は「現状維持 + 理由コメント固定」で正式決着とする。

**DoD:** 採用項目ごとに数値記録 + invalidation 台帳（melonprime-performance.md）更新。
不採用項目は §6 Rejected 表へ。

---

### Phase 5: ドキュメント統合・計画のクローズ処理（0.5 日 / リスクなし）

1. `melonprime-refactoring.md` に「Structural Refactor V7 / SRP v3 統合」節を追記
   （SRP v3 の監査結果 §1 と V7 の実測値表を正典に固定）
2. SRP v3 の完了済み経緯ドキュメント（progress / completion-summary / step3 plan / site-d plan /
   aim audit / outcome-c note）を `.claude/features/completed/`（新設）または
   `.claude/rules/completed/` に移動し、features README を更新
3. V6 進捗表の消し込み（Phase 6 の実測が済んだ項目を確定状態へ。Windows 基準値が未取得のまま
   なら「V7 Phase 4 スキップ判断と共に open のまま」と明記）
4. CLAUDE.md / rules README 更新

---

### Phase 6: スモーク + CI の一括消化（最後 / ユーザー実機依存）

**ユーザー指示によりここへ集約**。V1–V6 の継続回帰項目 + SRP v3 未実施分 + V7 追加分:

| # | 項目 | 由来 |
|---|---|---|
| 6-1 | ゲームプレイ一括スモーク: S2（参加/エイム/移動/射撃）/ S3（武器切替）/ S4（モーフ/ブースト）/ S6–S7（パッチ ON/OFF・ライフサイクル）/ S8（フォーカス） | V1 + SRP v3 A3（Sites A/B の live match 遷移を含む — 試合参加→終了→リマッチで hook register/unregister の OSD を確認） |
| 6-2 | HUD 編集モード全操作（Phase 2 の側パネル行を全要素で開閉・操作・Save/Cancel/Reset） | S9/S13 + V7 Phase 2 |
| 6-3 | Windows 実機: `ClipCursor` 系（`ReleaseForClose` / `ConfineToBottomScreen` / center-1px）、Linux 実機/VM: `resetAimMouseDelta` 分岐 | SRP v3 A3 |
| 6-4 | 設定ダイアログ往復（S10/S14: 保存→再起動→値保持、TOML キー集合一致） | V2 |
| 6-5 | CI 全マトリクス（Windows/macOS/Ubuntu/BSD）green 確認 — V7 の全コミットを含む状態で 1 回 | 全 Phase |
| 6-6 | （届いていれば）S21/S24: Windows 基準値の反映と再現性確認、V5/V6 進捗表の最終確定 | V5/V6 |

**DoD:** 上記の実施記録を本ファイル §7 に記入。V6 計画を `completed/` へ移動できる状態にする。

---

## 5. リスクと対策

| リスク | 該当 | 対策 |
|---|---|---|
| スモーク後回しで挙動退行が Phase 6 まで潜伏する | 全 | 不変条件 1: ゲームプレイ接触差分は「コード上で同値性を証明できる純移動のみ」。証明不能な変更は入れない。Phase 2 は行列 byte 一致で機械証明 |
| 側パネルテーブル化で行順/ラベル/型がズレる | 2 | 2a/2c の抽出スクリプト往復で byte 一致。bespoke 行はテーブル外残置（無理に載せない） |
| テーブル化がリテラル予算を破る | 2 | キーは `MP_HUD_PROP_KEY_*` 参照のみ。literal 予算 1 の CI ラチェットが機械検出 |
| `WidgetFactoryContext` に値メンバを足してしまう | 2 | ヘッダの LIFETIME RULE コメント遵守。レビュー観点に明記 |
| Windows 基準値が届かず Phase 4 が宙吊り | 4 | スキップ判断を正式アウトカムとして記録（V5/V6 の「計測なしに完了扱いしない」文化の継承） |
| ドキュメント移動でリンク切れ | 3, 5 | リンクチェッカー + features/rules README の同コミット更新 |
| 「最適化したい病」で do-not-touch へ踏み込む | 全 | 変更前チェック: V1 §2.1 リスト該当は本計画で扱わない。SRP v3 Deferred リスト（RunFrameHook split / hot state struct / Screen mouse router / PlatformInput redesign / Site C）は V7 でも非対象 |

---

## 6. 進捗トラッキング

| Phase | 内容 | 状態 | 完了日 | 結果メモ |
|---|---|---|---|---|
| 0 | ベースライン固定 + Windows ソーク依頼 | 完了（コード変更分は 0） | 2026-07-09 | HEAD `83b82235` で再計測（計画作成時 `c01de360` から2コミット進行：md追加 + macOS High2プリセット無効化）。全監査ローカル実行: config-defaults PASS / hud-key-parity PASS（missing defaults 0）/ inc-ownership PASS（55）/ literal 1/1 PASS / color-dialog PASS / srp-performance PASS（Screen.cpp raw-aim manual-review 2件は既知・対応不要）/ localization audit PASS（zh-Hant 用語 WARN 2 のみ、既知）/ HUD schema regenerate diff ゼロ。**scatter budget が 22→26 に regression**（`InputConfig.cpp` に `#ifdef __APPLE__` 4箇所、macOS compute-renderer 制限コミット由来、非 input-dispatch）→ Phase 1 で是正。macOS ビルド green（re-link のみ、ソース差分なし）。Windows `MELONPRIME_PERF=1` ソークは本セッションでは取得不可（Windows実機なし）— Phase 4 は計画どおり凍結のまま進行し、未取得ならスキップ判断を正式記録する |
| 1 | 小粒掃除（A1/A2 / API スイープ / skills 整理 / scatter budget是正） | 未着手 | — | |
| 2 | HUD エディタ側パネルのテーブル駆動化（本丸） | 未着手 | — | |
| 3 | 節化・命名・ドキュメント昇格 | 未着手 | — | |
| 4 | 計測ゲート付き perf 残債（Windows 基準値待ち） | 凍結 | — | Windows 実機・`MELONPRIME_PERF=1` ソークがこのAIセッションから取得不可。計画の明示的フォールバック（§4「届かない場合はこの Phase をスキップし『未計測のため見送り』と記録」）の対象 |
| 5 | ドキュメント統合・クローズ処理 | 未着手 | — | |
| 6 | スモーク + CI 一括消化（最後） | 未着手 | — | ROM実機ゲームプレイ・Windows/Linux実機・GitHub Actions実行はこのAIセッションから実施不可。AI側で完結する検証のみ実施予定 |

### 計測により不採用（Rejected）

（Phase 4 実施時に記入。現時点の Rejected 候補: HUD element cache — mac 実測で draw 非支配、
V6 §6 参照。Windows 基準値が覆さない限りここへ確定記入する）

---

## 7. Phase 0 スナップショット（計画作成時 2026-07-09・HEAD `c01de360`）

- `MelonPrime*` LOC: 116,623（ローカライゼーション込み）/ コード約 33,000
- 主要ファイル: HudPropSchema.inc 2,333（生成）/ HudRenderDraw.inc 1,438 /
  HudConfigOnScreenDraw.inc 1,320 / InputConfig.cpp 1,192 / HudRenderConfig.inc 1,082 /
  HudRenderRuntime.inc 1,022 / MelonPrime.h 725 / MelonPrime.cpp 706 /
  HudConfigOnScreenEdit.cpp 697 / RawInputState.cpp 730 / GameInput.cpp 610 /
  PatchLifecycle.cpp 160 / RuntimeConfig.cpp 128 / ScreenCursorPolicy.cpp 240 /
  HudEditorFormBuilder.cpp 394 / ColorDialogPrefs.cpp 184 / Lifecycle.cpp 266
- `RunFrameHook()`: 243 行
- 監査（2026-07-09 ローカル実行）: config-defaults PASS / hud-key-parity PASS / inc-ownership
  PASS（55）/ literal 1/1 PASS / scatter 22/22 PASS / color-dialog PASS / srp-performance PASS /
  localization audit PASS（zh-Hant 用語 WARN 2 のみ）
- macOS ビルド: HEAD で up-to-date（`ninja: no work to do`）

### Phase 0 再計測（実施時 2026-07-09・HEAD `83b82235`）

計画作成後に2コミット進行（`323debf7` add md、`83b82235` macOS High2プリセット無効化）。

- `MelonPrime*` LOC（.cpp/.h/.inc/.mm 合算）: 116,654（+31、ドキュメント追加分）
- 主要ファイル: `MelonPrime.cpp` 706 / `MelonPrime.h` 725 / `HudConfigOnScreenEdit.cpp` 697 /
  `InputConfig/MelonPrimeInputConfig.cpp` 1,223（+31、macOS High2プリセット無効化コミット由来）
- `RunFrameHook()`: 243 行（不変）
- 監査結果:
  - config-defaults PASS（cross-list mismatch なし）
  - hud-key-parity `-Strict` PASS（missing defaults 0、全4面）
  - inc-ownership PASS（55 ファイル）
  - literal budget 1/1 PASS
  - **platform scatter budget: 26/22 FAIL**（regression。`InputConfig.cpp` に `#ifdef __APPLE__`
    4箇所が `83b82235`（macOS compute-renderer High2プリセット無効化）で追加された。
    入力ディスパッチではなく UI/レンダラー設定ゲートのため、監査スクリプトの想定スコープ外。
    Phase 1 で行単位の除外マーカーを追加して是正）
  - color-dialog-prefs PASS
  - srp-performance PASS（Screen.cpp の raw-aim 手動レビュー要求2件は既知・対応不要）
  - localization audit PASS（zh-Hant 用語 WARN 2 のみ、既知・対応不要）
  - HUD schema regenerate: 出力 diff ゼロ（byte-identical）
- upstream `MELONPRIME` マーカー参考値（削減対象外・記録のみ）: EmuThread.cpp 53 /
  Screen.cpp 38（V6時32から増加）/ Window.cpp 33（V6時20から増加、`add md` コミット由来の可能性）/
  Config.cpp 17 / EmuInstance.h 16 / Screen.h 16 / EmuInstanceInput.cpp 13 / EmuInstance.cpp 5
- macOS ビルド: green（`cmake --build build-mac --parallel 4`、re-link のみ、ソース差分なし）
- Windows `MELONPRIME_PERF=1` 基準値: **このAIセッションからは取得不可**（Windows実機なし）。
  Phase 4 は計画の明示的フォールバックに従い凍結のまま進行し、スキップ判断を Phase 4 実施時に記録する

---

## 8. 推奨着手順序

```
0 → 1 → 2 (本丸) → 3 → 5 → 6 / （4 は Windows 基準値到着時に随時挿入）
```

- 0/1 は半日。**2 が唯一の実装的本丸**（HUD プロパティは今後も増えるため複利が効く — V1 パッチ
  レジストリ、V2 HUD スキーマと同じ論理の最終ピース）
- 3/5 はドキュメントのみで独立。4 は完全に計測待ちの寄生 Phase
- 6 は全コード Phase の後に 1 回だけ。ユーザー実機時間に依存するため事前にチェックリストを渡す

総見積り: **コード Phase（0–3, 5）で約 3.5–5 日相当 + Phase 6 のユーザー実機時間**。
V1–V6 と比べ小さいのは意図的 — 構造負債は既に完済しており、V7 は仕上げと検証の完済が主目的。
