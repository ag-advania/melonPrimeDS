# MelonPrime 全面リファクタリング計画 V3（Phase 0–7）

**作成日:** 2026-07-02
**対象:** master（zip展開版で実測。実行は実gitリポジトリ + Windows MinGW環境で行うこと）
**ステータス:** 進行中（Phase 6 完了）
**前提:** [V1計画（2026-06-11完了）](completed/melonprime-full-refactor-plan.md) /
[V2計画（2026-06-11完了）](completed/melonprime-full-refactor-plan-v2.md) の後継。
V1/V2 が解消した負債（パッチレジストリ / StaticWordPatch / ROMアドレス X-macro / HUDプロパティスキーマ /
OSDカラースキーマ / InputConfig分割 / フックサイト共有テーブル / Localization TU化）は**対象外**。
本計画は **2026-07-02 に master を実測した証拠**に基づく。憶測項目はない。

---

## 0. ゴールと非ゴール

### ゴール

1. **V2 以降に再蓄積したドリフトの解消** — V2 完了時 1,040 だった `"Metroid.*"` リテラルが
   **1,165（+125）** に再増加している。新機能追加時にスキーマ規律が守られていない証拠であり、
   放置すれば V2 の成果は数リリースで無効化される
2. **再発の構造的防止** — V1/V2 に欠けていた「リファクタ成果を固定化する仕組み」を導入する。
   監査スクリプト（現在は手動 PowerShell 運用）を CI に組み込み、リテラル予算・スキーマ整合・
   `.inc` 所有規約の違反をビルドで機械検出する。**これが本計画の最重要成果物**
3. **HUD プレビューの三重実装の統一** — 同じ HUD 要素の QPainter 描画が
   ランタイム / ゲーム内編集モード / 設定ダイアログプレビューの3系統に独立実装されている。
   見た目のドリフト（プレビューと実描画の不一致）を構造的に防ぐ
4. **期限切れ legacy キーの決着** — 「次回リリース後に削除」と明記された migration コードの削除
5. **設定マイグレーションの一元化** — V1 Phase 4-4 の積み残し。migration コードが
   InputConfig 2箇所 / runtime 1箇所 / EmuInstance.cpp（アンカー・HudFontSize migration）に分散
6. 結果として **正味 -400〜-800 行** + ドリフト再増加ゼロの恒久化

### 非ゴール（やらないこと）

- ホットパスの再設計（V1 §2.1 の do-not-touch リストを全文継承）
- melonDS 本体（`src/` 直下コア）の構造変更
- 新機能の追加、HUD 描画結果・編集モード UX の変更
- `#ifdef MELONPRIME_DS` ガードの除去（upstream diff 衛生のため維持）
- 設定キーの**保存名・型・デフォルト値の変更**（TOML 互換。V1/V2 と同じ）
- `MelonPrimeHudPropSchema.inc` の canonical owner リテラル（575個）と
  `MelonPrimeDef.h::CfgKey`（56定数）の削減 — これらは「単一定義場所」そのものであり無駄ではない
- upstream 統合点の削減は Phase 6（任意）に隔離。マージ予定がなければ V1/V2 と同様スキップ

---

## 1. 無駄の棚卸し（2026-07-02 実測・証拠付き）

### 現状スナップショット

| 指標 | V2 完了時（2026-06-11） | 今回実測 | 差分 |
|---|---:|---:|---:|
| `MelonPrime*` ファイル数 | 114 | 123 | +9 |
| `MelonPrime*` LOC（.ui 除く） | 約25,000 | 30,877 | 約+5,900 |
| `"Metroid.*"` quoted リテラル | 1,040 | **1,165** | **+125** |
| HUD スキーマ行数 | 549 | 561 | +12 |
| `MelonPrimeInputConfig.cpp` | 995 | 1,120 | +125 |
| `MelonPrime.cpp` | 752 | 896 | +144 |
| unity `.inc` 断片 | — | 50 | — |

LOC 増加の大半は新機能（プレビューウィジェット群、スキーマ拡張等）による正当な成長だが、
その**追加のされ方**にドリフトが混じっている。以下が具体的な負債。

### V3-W1. リテラルの再蓄積 — スキーマ規律の未執行（最大の負債）

canonical owner 以外の `"Metroid.*"` リテラル分布（実測、上位）:

| ファイル | リテラル数 | 性質 |
|---|---:|---|
| [InputConfig/MelonPrimeInputConfigHudPreviews.inc](../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigHudPreviews.inc) | **144** | プレビューウィジェットの `paintEvent` が config を直読み。V2 の `MP_HUD_PROP_KEY_*` 規律に**未準拠** |
| [MelonPrimeHudConfigOnScreenDraw.inc](../../src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenDraw.inc) | 62 | 編集モード描画の直読み残り |
| [InputConfig/MelonPrimeInputConfigHudTables.inc](../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigHudTables.inc) | 53 | V2 Phase 3 で純移動されたテーブルの残存リテラル |
| [InputConfig/MelonPrimeInputConfigCustomHudBuild.inc](../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigCustomHudBuild.inc) | 48 | 同上（`setupCustomHudWidgets` 本体） |
| [InputConfig/MelonPrimeInputConfig.cpp](../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp) | 35 | 非バインディング特殊ケース |
| [InputConfig/MelonPrimeInputConfigConfig.cpp](../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigConfig.cpp) | 28 | save 側特殊ケース（大半は `CfgKey::*` 化可能） |
| [Window.cpp](../../src/frontend/qt_sdl/Window.cpp) | 24 | Metroid メニューのランタイムトグルが直リテラル |
| [EmuInstance.cpp](../../src/frontend/qt_sdl/EmuInstance.cpp) | 17 | アンカー migration / `HudFontSize` legacy migration が Visual キーを直読み |

規律は文書（repo-architecture.md）にはあるが、**機械執行されていない**。
監査スクリプト（`audit-hud-key-parity.ps1` 等）は手動実行前提で、CI（`.github/workflows/build-windows.yml`）
に組み込まれていないため、違反がマージを素通りする。+125 の再増加はその直接の帰結。

### V3-W2. HUD 要素描画の三重実装

同一 HUD 要素（クロスヘア / HP / 武器・弾薬 / ゲージ / アウトライン）の QPainter 描画が3系統:

| 系統 | ファイル | 行数 |
|---|---|---:|
| ランタイム実描画 | [MelonPrimeHudRenderDraw.inc](../../src/frontend/qt_sdl/MelonPrimeHudRenderDraw.inc) | 1,470 |
| ゲーム内編集モードプレビュー | [MelonPrimeHudConfigOnScreenDraw.inc](../../src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenDraw.inc) | 1,328 |
| 設定ダイアログプレビュー（5クラスの `paintEvent`） | [InputConfig/MelonPrimeInputConfigHudPreviews.inc](../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigHudPreviews.inc) | 約700 |

ランタイム側はキャッシュ・dirty-rect 前提の高度に最適化された実装（触らない）。問題は
ダイアログプレビュー側が **config 直読み + 独自描画** で、実描画とパラメータ解釈が
乖離しうること（例: アウトラインの太さ・opacity の解釈、ゲージのランプ色補間）。
プレビュー3系統の完全統合は UX 変更リスクが高いため、**「パラメータの読み方と要素ジオメトリ計算」
だけを共有し、描画呼び出しは各系統に残す**のが現実的な落とし所（Phase 3 で詳細化）。

### V3-W3. 期限切れ legacy キー

| 対象 | 証拠 |
|---|---|
| `Metroid.Aim.Enable.InstantAimFollow` migration | [InputConfig.cpp](../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp) / [InputConfigConfig.cpp](../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigConfig.cpp) / runtime / ARM9 hook / patch path に互換読みが残る。V2 Phase 1 の「次回リリース後」コメントは Phase 4 で台帳化し、未保存旧TOMLユーザー保護のため削除保留と判断 |
| `Metroid.Visual.HudFontSize` legacy migration | EmuInstance.cpp 内で `HasKey()` ガード付き読み取り。repo-architecture.md に「migration-only key」と記載。同時に決着させる |

### V3-W4. 設定マイグレーションの分散（V1 4-4 積み残しの続き）

V1 Phase 4-4 は migration 3箇所にバナーを付けるに留めた（層跨ぎ移動は評価タイミングを変えるため不可、
という判断は正しい）。しかしその後 **EmuInstance.cpp にアンカー系 migration（Visual キー 15個の直読み）**
が存在することが今回の実測で判明。少なくとも「migration の在処一覧」が文書化されておらず、
削除判断（W3）のたびに全域 grep が必要になっている。

### V3-W5. 監査ツールの CI 未統合・環境依存

- `audit-config-defaults.ps1` / `audit-hud-key-parity.ps1` / `check-inc-ownership.ps1` は
  PowerShell 専用・手動実行。`generate-hud-prop-schema.py` は Python
- `.github/workflows/build-windows.yml` は存在する（MSYS2/MinGW ビルド）が、監査ジョブがない
- スキーマ `.inc` が再生成で byte-identical かの検証も手動

### V3-W6. ファイルの再肥大（小粒）

- `MelonPrime.cpp` 752→896、`MelonPrimeInputConfig.cpp` 995→1,120（V2 目標 1,200 以下はまだ維持）
- [MelonPrimeHudRenderConfig.inc](../../src/frontend/qt_sdl/MelonPrimeHudRenderConfig.inc) 1,090 /
  [MelonPrimeHudRenderRuntime.inc](../../src/frontend/qt_sdl/MelonPrimeHudRenderRuntime.inc) 1,022 —
  分割は不要だが、セクション見出しの整備で可読性を維持する程度
- 緊急性は低い。Phase 5 の任意項目

### V3-W7. ドキュメントの経年整理

- `.claude/rules/` は V1/V2 の completed 分離により概ね健全。ただし
  `melonprime-refactoring.md`（統合履歴、非常に長大）に V3 結果節を追記する運用は継続
- `.claude/proposals/crosshair-smooth.md` の生死判定（実装済み/破棄済みかの明記）

---

## 2. 不変条件 — 絶対に壊さないもの

**V1 §2（do-not-touch リスト・規律・共通DoD）と V2 §2 を全文継承**。V3 固有の追加:

1. **プレビュー統一（Phase 3）はランタイム描画側を1行も変えない方向で行う**。共有化は
   「プレビューがランタイムのヘルパー/スキーマを参照する」一方向のみ。ランタイムの
   キャッシュ機構・dirty-rect・draw funnel（OPT-DR1/DR2/DR3）は不変
2. リテラル置換（Phase 2）は**保存名の変更ゼロ**。`MP_HUD_PROP_KEY_*` / `CfgKey::*` への
   参照差し替えのみ。置換前後で TOML キー集合の byte-exact 一致を監査スクリプトで証明
3. CI に追加する監査は**既存スクリプトの実行**を基本とし、監査ロジックの新規実装で
   既存スクリプトと二重管理にしない
4. ビルドは `.\.claude\skills\build-mingw.bat`（`--jobs 1` 厳守）。CMake 変更のないフェーズは
   `-existing` 可。macOS/zip 環境ではビルド検証不可能なので、**実装は必ず Windows 実機リポジトリで行う**
5. `git reset --hard` 等の破壊的操作禁止。フェーズ = ブランチ、ロールバック = revert

---

## 3. スモークチェックリスト

**V1 §3 の S1–S12 + V2 §3 の S13–S15 をそのまま使う**。V3 で追加:

| # | 項目 | 確認内容 |
|---|---|---|
| S16 | プレビュー一致 | 設定ダイアログの各プレビューと、同設定でのゲーム内実描画/編集モード表示が目視で一致（Phase 3 の前後で差が出ないこと） |
| S17 | 旧TOML読み込み | legacy キー削除後、削除前のユーザー TOML を読み込んで挙動が変わらない（migration 済みユーザー / 未 migration ユーザーの2系統） |

**Phase 2 は S10 / S13 / S14 必須。Phase 3 は S9 / S10 / S16 必須。Phase 4 は S17 必須。**

---

## 4. フェーズ計画

### Phase 0: ベースラインと監査 CI 化（1日 / リスクなし / **恒久化の本丸**）

V1/V2 との最大の違い。「計測してから始める」に加えて「**終わった後に戻れなくする**」を最初に作る。

1. LOC / リテラル数 / exe サイズ / スキーマ行数のスナップショットを本ファイル末尾に記録
2. `.github/workflows/build-windows.yml` に監査ステップを追加（またはビルド後ステップ化）:
   - `audit-config-defaults.ps1`（既存・終了コード対応済み）
   - `audit-hud-key-parity.ps1`
   - `check-inc-ownership.ps1`
   - **リテラル予算チェック（新設・小スクリプト）**: canonical owner
     （`MelonPrimeHudPropSchema.inc` / `MelonPrimeOsdColorSchema.inc` / `MelonPrimeDef.h`）
     **以外**の `"Metroid.*"` リテラル総数が予算値を超えたら fail。初期予算 = Phase 0 実測値。
     Phase 2 の各コミットで予算値を下げていき、最終値で固定する（ラチェット方式）
   - スキーマ再生成チェック: `generate-hud-prop-schema.py` を流して生成 `.inc` が
     byte-identical であること
3. §3 スモークのベースライン実施（壊れていた項目の濡れ衣防止。V1/V2 と同じ理由）

**成果物:** 計測値の追記 + CI 監査ジョブ。プロダクトコード変更なし。
**DoD:** CI が現状コードで green（予算値 = 現状値なので必ず通る）。

---

### Phase 1: 死活整理・stale の掃除（0.5日 / リスク極小）

| # | 作業 |
|---|---|
| 1-1 | `.claude/proposals/crosshair-smooth.md` の生死判定（実装済みなら completed へ、破棄なら理由を明記して削除） |
| 1-2 | V2 進捗表の「ビルド未検証」項目の消し込み: V2 Phase 2e/3/4/5/6/8 は shell runner 問題でビルド未検証のまま完了扱いになっている。フル configure ビルド1回で一括検証し、V2 completed 文書に検証済みの追記 |
| 1-3 | 呼び出しゼロ公開 API の再スイープ（V2 1-4 と同じ機械確認のみ。判断が要るものは触らない） |
| 1-4 | stale コメント修正（スイープ中に見つかる範囲。`git diff -w` 小） |

**DoD:** フル configure ビルド + S10。
**期待削減:** 行数より「V2 の完了宣言を検証で裏打ちする」ことが目的。

---

### Phase 2: リテラル再蓄積の解消 + ラチェット固定（2–3日 / リスク低中 / **本丸A**）

V2 Phase 2e の続きを、今度は**逃げ場なし（CI 予算）付き**で完遂する。

#### 2a. 機械抽出
- 非 canonical リテラル 約530個（1,165 − schema 575 − Def.h 56）の全箇所を抽出し、
  「`MP_HUD_PROP_KEY_*` で置換可能 / `CfgKey::*` で置換可能 / CfgKey 新設が必要 /
  意図的に残す（理由必須）」の4分類表を作る

#### 2b. ファイル単位の置換（それぞれ独立コミット、コミットごとに予算値を下げる）
1. `InputConfigHudPreviews.inc`（144）— 全て Visual キーなので `MP_HUD_PROP_KEY_*` 化。
   プレビュー描画ロジック自体は触らない（Phase 3 と分離）
2. `MelonPrimeHudConfigOnScreenDraw.inc`（62）— 同上
3. `InputConfigHudTables.inc`（53）+ `InputConfigCustomHudBuild.inc`（48）— V2 Phase 3 の
   純移動時に取り残された分。schema props include の参照へ
4. `InputConfig.cpp`（35）+ `InputConfigConfig.cpp`（28）— 大半は既存 `CfgKey::*` の参照漏れ。
   `Metroid.UI.Section*` 系は CfgKey に定数追加（保存名不変）
5. `Window.cpp`（24）+ `EmuInstance.cpp`（17）+ その他 — 非 MelonPrime ファイルなので
   `#ifdef MELONPRIME_DS` ガード内での参照差し替えのみ。EmuInstance.cpp の migration 直読みは
   Phase 4 で扱うため、ここではキー定数化だけ行う

#### 2c. 予算の最終固定
- 目標: 非 canonical リテラル **530 → 50 以下**（意図的残置リストを本ファイルに記録）
- CI 予算値を最終値で固定。以後、新キーは schema / CfgKey 経由でしか追加できない

**DoD:** フル configure ビルド + S10 / S13 / S14 + 全監査 green + TOML キー集合 byte-exact 一致。
**期待削減:** 行数は微減だが、**W1 の再発経路を CI で恒久遮断**するのが価値。

---

### Phase 3: HUD プレビュー描画の共有化（2–4日 / リスク中 / **本丸B**）

三重実装を「共有コア + 3つの薄い呼び出し側」に再構成する。**完全統合はしない**
（ランタイムはキャッシュ/dirty-rect 前提、プレビューは即時再描画前提で、実行モデルが違う）。

#### 3a. 乖離の実測（コード変更なし）
- 3系統それぞれの「要素ジオメトリ計算」（クロスヘアのアーム座標、ゲージの塗り分け、
  アウトラインの太さ/opacity 解釈、アンカー適用）を突き合わせ、**既存の見た目乖離レポート**を作る。
  乖離は Phase 2a と同じ方針で「現状維持フラグ」として記録し、黙って直さない

#### 3b. ジオメトリ/色計算コアの抽出
- 新規 `MelonPrimeHudGeometry.h`（ヘッダオンリー、状態なし・純関数）:
  クロスヘア形状、ゲージ矩形とランプ色、アウトラインパラメータ、9点アンカー解決
- **ランタイム側**: `HudRenderDraw.inc` / `HudRenderConfig.inc` の該当計算を純関数呼び出しに
  差し替え。キャッシュに載せる値が bit 単位で不変であることを確認してからコミット
  （計算式の移動のみ、演算順も保存）
- **プレビュー側**: `InputConfigHudPreviews.inc` の5クラスと `HudConfigOnScreenDraw.inc` の
  プレビュー描画を同じ純関数の消費者に変更

#### 3c. プレビューの config 読みの統一
- プレビューウィジェットの config 直読み（`GetInt`/`GetDouble` 散発呼び)を、ランタイムの
  `CachedHudConfig` ローダーを流用した「プレビュー用スナップショット構造体」1回読みに変更。
  読み取りキー集合はランタイムと同一になるため、以後キー追加時の取り漏れが構造的に消える

**DoD:** フル configure ビルド + S9 / S10 / S16 + 3a の乖離レポートと変換後の乖離が同一
（= 新たな見た目変化を作っていない）。
**期待削減:** 約 -300〜-600 行 + プレビュー乖離バグの構造的防止。

---

### Phase 4: マイグレーションの一元管理と legacy キー決着（1日 / リスク低中）

| # | 作業 |
|---|---|
| 4-1 | **migration 台帳の作成**: 全 migration コード（InstantAimFollow 互換読み、EmuInstance.cpp のアンカー migration、`HudFontSize`、ZoomMethod/WeaponSwitchMethod/BipedFireMethod の enum 変換）を1つの表に棚卸しし、本ファイルまたは notes/ に記録。層跨ぎの物理移動はしない（V1 4-4 の判断を維持） |
| 4-2 | **InstantAimFollow migration の削除**: 「次回リリース後に削除」の期日確認 → 経過していれば3箇所を削除。`Metroid.Aim.Enable.InstantAimFollow` の Config デフォルト・CfgKey 定数も同時削除（V2 1-2 の NativeDeltaHook と同じ手順）。**S17（旧TOML 2系統）必須** |
| 4-3 | `HudFontSize` legacy migration の削除判断: 導入時期が古く migration 済みユーザーが大半なら削除、判断材料が無ければ「削除予定日」を明記して温存 |
| 4-4 | アンカー migration（EmuInstance.cpp）: 完了済みユーザーには no-op であることを確認し、ガード条件を1関数に整理（挙動不変） |

**DoD:** フル configure ビルド + S10 / S17（削除前 TOML の読み込み確認は必須）。
**期待削減:** 約 -80〜-150 行 + 「消してよいか毎回悩む」状態の解消。

---

### Phase 5: ファイル整理・小粒の decluttering（0.5–1日 / リスク低 / 任意）

| # | 作業 |
|---|---|
| 5-1 | `MelonPrime.cpp`（896行）: 追加分のセクション見出し整備。1,000 行超えが見えたら `HandleGameJoinInit` / lifecycle 群の `.inc` 化を検討（今回は見送り可） |
| 5-2 | `MelonPrimeInputConfig.cpp`（1,120行）: V2 目標 1,200 以下は維持中。増加分の性質を確認し、テーブル化漏れがあれば binding table へ |
| 5-3 | `HudRenderConfig.inc` / `HudRenderRuntime.inc`: セクションバナーの整備のみ（分割はしない — unity 断片の増殖は所有検査コスト増） |
| 5-4 | `MelonPrimeLocalization.cpp`（1,245行）: 英日テーブルの整列・重複エントリ検査スクリプト追加（任意。テーブル形式の変更はしない） |

**DoD:** ビルド + S10。

---

### Phase 6: upstream 統合点の継続削減（1–2日 / **任意・ストレッチ**）

V1/V2 Phase 7 と同一の判断基準: **upstream マージ予定がなければスキップ**。
実測: EmuThread.cpp 48 / Screen.cpp 29 / Window.cpp 19 / Config.cpp 17 / EmuInstance.h 16 /
Screen.h 15（`MELONPRIME` 出現数）。やる場合は Screen.cpp / Window.cpp の連続・自己完結
ブロックのみ、`.inc` 切り出しの include 位置完全同一（コード生成不変）で。

**DoD:** フル configure ビルド + S2 + S9。

---

### Phase 7: ドキュメント整備・最終計測（0.5日 / リスクなし）

1. [melonprime-refactoring.md](melonprime-refactoring.md) に「Structural Refactor V3 2026-07」節を追記
2. [repo-architecture.md](repo-architecture.md) に HudGeometry 共有コアの所有点
   （コア → ランタイム/編集/ダイアログの3消費者の図）と「新 HUD 要素のプレビュー追加手順」を追記
3. CI 監査（Phase 0）の運用ルールを [build.md](build.md) に追記
   （予算値の変更は削減方向のみ許可、増加はレビュー必須、など）
4. CLAUDE.md / rules README 更新、本ファイルを `completed/` へ移動しステータス「完了」
5. 最終計測: リテラル数（目標: 非 canonical 50以下）/ LOC / exe サイズ / CI green

---

## 5. リスクと対策

| リスク | 該当 | 対策 |
|---|---|---|
| リテラル置換の取り違え（530箇所） | 2 | 手打ち禁止・機械抽出 + 4分類表。置換はマクロ参照化のみで保存名不変。TOML キー集合 byte-exact 監査を各コミットで実行 |
| ジオメトリ共有化でランタイム描画が変わる | 3b | ランタイム側は「計算式の移動」のみ、演算順保存。キャッシュ値の bit 一致を一時 assert で証明してからコミット。乖離は 3a で先に記録し黙って直さない |
| プレビューの見た目が変わり UX 苦情 | 3c | S16 目視 + 要素単位の小コミット。乖離修正は「バグ修正」として分離コミット |
| legacy キー削除で旧 TOML ユーザーが壊れる | 4-2 | S17 で migration 済み/未 migration の2系統 TOML を実際に読ませて確認。疑義があれば削除を次リリースへ延期（温存はコスト小） |
| CI 予算がノイズ化して形骸化する | 0, 2c | 予算はラチェット（下げるのは自由、上げるのはレビュー必須）。canonical owner の定義を予算スクリプト内にコメントで明記 |
| ビルドメモリ枯渇・タイムアウト | 全 | `--jobs 1` 厳守。タイムアウト後は build.md の復旧手順 |
| 巨大 diff によるレビュー不能化 | 2, 3 | 1コミット ≦ 約400行 diff 目安（V1/V2 と同じ）。ファイル単位・要素単位の小コミット |
| zip 環境で計画しただけになる | 全 | 実行は Windows 実機の git リポジトリで。Phase 0 の計測は実機で取り直す（zip 実測値は参考値として本ファイルに残す） |

---

## 6. 進捗トラッキング

| Phase | 内容 | 状態 | 完了日 | 結果メモ |
|---|---|---|---|---|
| 0 | ベースライン + 監査 CI 化 | 完了 | 2026-07-02 | 実リポジトリで再計測。Windows workflow に監査ステップを追加し、非 canonical `"Metroid.*"` リテラル予算 519 を初期値として固定。schema generator の現行 crosshair count / side-panel outline 検出漏れを補正し、再生成出力を更新。`ci/phase0-refactor-audits` の `cdcb5fde` で Windows/macOS/Ubuntu/BSD 全 workflow 成功、Windows の audit/schema/build/upload 各 step 成功。 |
| 1 | 死活整理・V2 検証消し込み | 完了 | 2026-07-02 | `crosshair-smooth.md` は現行コード未実装（`CrosshairSmooth` / `chSmooth` は提案内のみ）と確認し、破棄ではなく保留提案として明記。V2 completed の Phase 2/3/4/5/6/8 は、V2当時の個別コミット再ビルドではなく、V2成果を含む現行treeが Windows CI run 28588126394 / 28587404877 の full configure/build + audit/schema checks とローカル macOS build を通過済みとして消し込み。公開 API 再スイープ: `RawInputWinFilter::Poll` 定義/呼び出しゼロ、`resetAllKeys`/`resetMouseButtons` は `InputState` 内部実装のみ、`drainMessagesOnly` は `drainPendingMessages()` 内で使用中のため維持。手動 S10 は V1/V2 と同じく継続回帰項目。 |
| 2 | リテラル解消 + ラチェット固定（本丸A） | 完了 | 2026-07-02 | 非 canonical `"Metroid.*"` 519件を機械分類（HUD schema 365 / 既存 CfgKey 62 / 新規 UI CfgKey 86 / `MenuLanguage` 6）。HUD visual は `MP_HUD_PROP_KEY_*`、非HUD・UIセクションは `CfgKey::*` に置換し、`Metroid.UI.*` 定数を `MelonPrimeDef.h` へ追加。`generate-hud-prop-schema.py` は dialog macro-ref scan に対応。非 canonical リテラルは 1件のみ（`Config.cpp` の固定長 legacy migration row `Metroid.Sensitivity.Aim`）として意図的残置し、CI literal budget を 519→1 にラチェット。ローカル macOS build 成功、schema regeneration は dialog 562 / edit 230 / side 370 / runtime 505 を維持。手動 S10/S13/S14 は継続回帰項目。 |
| 3 | HUD プレビュー共有化（本丸B） | 完了（3cは見送り判断） | 2026-07-02 | `MelonPrimeHudPreviewDriftPhase3a.md` を作成し、runtime / in-game edit / dialog preview の乖離を棚卸し。`MelonPrimeHudGeometry.h` を新設し、アンカー解決・text align・gauge relative position/alignmentをQt非依存の純関数として共有。runtime `ApplyAnchor` / `CalcAlignedTextX` / `CalcGaugePos` と dialog preview `dsPos` / `alignedX` / `drawGaugeDS` は同じ helper を使用し、dialog側の整数丸めは維持。runtime `CachedHudConfig` の公開化は dirty-rect/cache/font/crosshair事前計算に絡むため見送り、dialog previewの簡略描画はPhase3aの現状維持対象として温存。ローカル macOS build 成功。手動 S9/S10/S16 は継続回帰項目。 |
| 4 | migration 一元化 + legacy 決着 | 完了 | 2026-07-02 | `MelonPrimeMigrationLedgerPhase4.md` を作成し、`InstantAimFollow` 互換読み、pre-anchor HUD migration、`HudFontSize` migration、LowLatency/WeaponSwitch/BipedFire/Zoom/DirectTransform の enum/gate 変換を棚卸し。最新ローカルタグは `Release3.4.1`（2026-06-27）だが、未保存の旧TOMLユーザーが post-V3 build へ直接移行するケースを壊さないため `InstantAimFollow` 削除は保留し、「post-V3 release で1回保存機会を与えた後に再判定」と決定。`EmuInstance.cpp` のアンカー migration 判定を `ShouldMigrateLegacyHudAnchors()` に整理（挙動不変）。ローカル macOS build 成功。S17 は削除なしのためコードパスレビュー扱い、将来の削除コミットで migrated/unmigrated TOML 実機確認必須。 |
| 5 | ファイル整理（任意） | 完了 | 2026-07-02 | `MelonPrimeDeclutterPhase5.md` を作成し、肥大ファイルを再計測。`MelonPrime.cpp` は984行で1,000行のsoft threshold手前のため `.inc` 化せず、config/platform setup・lifecycle reset・per-frame hook の節見出しだけ追加。`InputConfig.cpp` は1,160行でV2目標1,200行以内、残る非binding config操作は migration / developer-only / enum transform / invalidate-couple / renderer preset / preview/schema dynamic path と確認し温存。`HudRenderConfig.inc` / `HudRenderRuntime.inc` は既存バナーで所有境界が十分なため分割なし。Localization duplicate scan は `kTranslations` 675行中5件（短語の文脈違い）で、機械統合は見送り。 |
| 6 | upstream 統合点（任意） | 完了（スキップ判断） | 2026-07-02 | `MelonPrimeUpstreamIntegrationPhase6.md` を作成し、upstream-owned frontend ファイルの `MELONPRIME` 出現数を再計測（EmuThread.cpp 48 / Screen.cpp 29 / Window.cpp 20 / Config.cpp 17 / EmuInstance.h 16 / Screen.h 15）。現時点ではupstream merge targetがなく、残るブロックは機能的hook siteであり、追加 `.inc` 化は所有/順序コストの方が大きいと判断してコード抽出はスキップ。次回upstream mergeで繰り返しconflict hotspotになった連続自己完結ブロックのみ再検討する。 |
| 7 | ドキュメント + 最終計測 | 未着手 | — | — |

### Phase 0 計測値（実リポジトリ実測 2026-07-02）

- `MelonPrime*` ファイル: 126 / 31,618 行（.ui 除く）
- `"Metroid.*"` quoted リテラル: 1,165（Phase 2 後: 718）
  （Phase 2 後 canonical: HudPropSchema.inc 575 + OsdColorSchema.inc 15 + Def.h 127 = 717。
  Phase 2 前の非 canonical 519。Phase 2 後の非 canonical 1:
  `Config.cpp` の固定長 legacy migration row `Metroid.Sensitivity.Aim`。Phase 2 前の非 canonical 上位: HudPreviews 194 / OnScreenDraw 62 /
  HudTables 53 / CustomHudBuild 48 / InputConfig.cpp 35 / ConfigConfig 28 /
  Window.cpp 24 / EmuInstance.cpp 17 / OnScreenInput 14 / HudScreenCppHelpers 7）
- HUD schema generator: rows 575 / dialog 562 / edit 230 / side 370 / runtime 505 /
  missing defaults 0 / type drift 0 / range drift 5
- 主要ファイル: HudPropSchema.inc 2,333 / HudRenderDraw.inc 1,470 / HudConfigOnScreenDraw.inc 1,328 /
  Localization.cpp 1,337 / InputConfig.cpp 1,157 / HudRenderConfig.inc 1,090 / HudRenderRuntime.inc 1,022 /
  MelonPrime.cpp 984
- `MELONPRIME` 出現数（upstream ファイル）: EmuThread 51 / Screen 29 / Window 21 / Config 17 /
  EmuInstance.h 16 / Screen.h 15
- unity `.inc`: 50
- ローカル macOS バイナリサイズ: 7,064,496 bytes
  (`build-mac/melonPrimeDS.app/Contents/MacOS/melonPrimeDS`)
- Windows exe サイズ: 50,150,912 bytes
  (`melonPrimeDS-windows-x86_64` artifact from `ci/phase0-refactor-audits` run 28587404877)
- CI 検証: `cdcb5fde` on `ci/phase0-refactor-audits`
  - Windows: audit config/defaults + HUD parity + `.inc` ownership + literal budget + schema regeneration + build + artifact upload 成功
  - macOS / Ubuntu / BSD: 成功

---

## 7. 推奨着手順序

```
0 (CI恒久化) → 1 → 2 (本丸A) → 3 (本丸B) → 4 → (5) → (6) → 7
```

- **0 を最優先**。V1/V2 が実証したとおり、規律は文書だけでは守られない（+125 の再増加が証拠）。
  監査を CI に固定してから中身に着手することで、以後の全フェーズの成果が保全される
- 2 は 0 のラチェットと一体。3 は 2 の後（プレビューのキー参照が定数化済みの方が安全）
- 4 は完全独立。リリースサイクルの都合に合わせて前後させてよい
- 5/6 は任意。6 は upstream マージ計画がない限り省略（V1/V2 と同じ判断）

総見積り: **必須フェーズ（0–4, 7）で約7–10日相当**。各フェーズは独立コミット列なので中断・再開自由。
