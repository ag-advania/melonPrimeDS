# MelonPrime 全面リファクタリング計画 V2（Phase 0–8）

**作成日:** 2026-06-11
**対象ブランチ:** `highres_fonts_v3`（フェーズごとに作業ブランチを切る）
**ステータス:** 未着手
**前提:** [V1計画（Phase 0–8、2026-06-11完了）](completed/melonprime-full-refactor-plan.md) の後継。
V1 が解消した負債（パッチレジストリ / StaticWordPatch / ROMアドレス X-macro / 非HUD設定バインディングテーブル /
Localization TU化 / EmuThread断片化 / デッドコード第1弾）は対象外。
本計画は **V1完了後のコードを 2026-06-11 に実測した証拠**に基づく。憶測項目はない。

---

## 0. ゴールと非ゴール

### ゴール
1. **HUDプロパティ追加コストの激減** — 現在「新プロパティ1個 = 5〜6箇所の手書きミラー」を
   「スキーマ1行 + 消費側1箇所」にする（V1がパッチで達成したことのHUD版）
2. **設定キー文字列リテラルの撲滅** — 現在 `"Metroid.*"` リテラルは**計1,801箇所**。
   typo・面間ドリフト（ダイアログにあるが編集パネルに無い等）を構造的に防止する
3. **巨大ファイルの分割** — `MelonPrimeInputConfig.cpp`（2,665行）の責務分離
4. **フックサイトテーブルの整備** — モジュール毎に散在する per-ROM PC テーブルの共有値一本化 + 範囲検証 + mphCodex 監査
5. **V1積み残しの決着** — transient reset 非対称、legacy設定キー、OSDカラー多重定義、孤児ファイル
6. 結果として **正味 -800〜-1,500 行** と、HUD設定変更時の「触り忘れ事故」の構造的防止

### 非ゴール（やらないこと）
- ホットパスの再設計（V1 §2.1 の do-not-touch リストをそのまま継承）
- melonDS 本体（`src/` 直下コア）の構造変更
- 新機能の追加、HUD描画ロジック・編集モードUXの変更
- `#ifdef MELONPRIME_DS` ガードの除去（upstream diff 衛生のため維持）
- **ランタイム `Load*Config` のデータ駆動化はしない**（構造体への代入・`ApplyAnchor` 等の後処理は
  手書きのまま。キー参照の定数化＝コンパイル時検証までに留める。理由: 描画前ロードは形が複雑で、
  データ駆動化はバグ混入リスクに対しリターンが薄い）
- 設定キーの**保存名変更**（TOML互換性。V1と同じ）

---

## 1. 無駄の棚卸し（2026-06-11 実測・証拠付き）

### V2-W1. HUDプロパティの多面ミラー — 最大の負債

`"Metroid.*"` 設定キー文字列リテラルの分布（実測、上位）:

| ファイル | リテラル数 |
|---|---:|
| [InputConfig/MelonPrimeInputConfig.cpp](../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp) | 757 |
| [MelonPrimeHudRenderConfig.inc](../../src/frontend/qt_sdl/MelonPrimeHudRenderConfig.inc) | 255 |
| [MelonPrimeHudConfigOnScreenEdit.cpp](../../src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenEdit.cpp) | 229 |
| [MelonPrimeHudConfigOnScreenDefs.inc](../../src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenDefs.inc) | 221 |
| [MelonPrimeHudConfigOnScreenDraw.inc](../../src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenDraw.inc) | 62 |
| MelonPrimePatchOsdColor.cpp | 55 |
| MelonPrimeHudConfigOnScreenSnapshot.inc | 41 |
| **全体合計** | **1,801** |

Config.cpp の `Metroid.*` デフォルトキーは **661個（うち `Metroid.Visual.*` が549個）**。
1プロパティ（例: `Metroid.Visual.HudHpX`）の定義/参照面:

1. `Config.cpp` デフォルト値（3つの型別リスト）
2. 設定ダイアログ: `kSec*` の `HudWidgetProp` テーブル
   （[MelonPrimeInputConfig.cpp:943-1711](../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp#L943)、約770行）
3. ゲーム内編集モード（オンスクリーン側）: `kProps*` の `HudEditPropDesc` テーブル
   （[MelonPrimeHudConfigOnScreenDefs.inc:63-244](../../src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenDefs.inc#L63)）
4. ゲーム内編集モード（Qtサイドパネル側）: `populateHP()` 等の手書き行構築
   （[MelonPrimeHudConfigOnScreenEdit.cpp:506-](../../src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenEdit.cpp#L506)、
   `addOffsetRows("Metroid.Visual.HudHpX", ...)` のようにリテラル再記述）
5. ランタイムロード: `Load*Config`
   （[MelonPrimeHudRenderConfig.inc:396-652](../../src/frontend/qt_sdl/MelonPrimeHudRenderConfig.inc#L396)）
6. （スナップショットは既に kProps*/kEditElems を反復していて概ね駆動済み。ただし
   [Snapshot.inc:65-67](../../src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenSnapshot.inc#L65) の
   `CrosshairColorR/G/B` 等のリテラル直書き残りあり）

つまり **HUDプロパティ1個の追加 = 最大6箇所の手作業**。V1 Phase 5b は非HUD43キーのみをテーブル化し、
「HUD系はダイアログのみ `HudWidgetProp` で駆動済み」だった — ダイアログ以外の面が取り残されている。
面間ドリフト（編集パネルに出ないプロパティ等）が**既に存在する可能性**があり、Phase 2a の機械抽出で炙り出す。

### V2-W2. `MelonPrimeInputConfig.cpp` の肥大（2,665行・4責務同居）

| 区画 | 行範囲（実測） | 内容 |
|---|---|---|
| ダイアログ本体 | 82-942 | ctor / バインディング / 感度・トグル(177行) / 入力メソッド(205行) / 折りたたみ / auto-slot |
| HUD静的テーブル群 | 943-1711 | `kSec*` / `kSubs*` / `kHudMainSections`（約770行のデータ） |
| HUDウィジェット構築 | 2145-2898 | `setupCustomHudWidgets` **単体で約755行** |
| 末尾 | 2898-2900 | `#include "MelonPrimeInputConfigCustomHudCode.inc"`（分割の先例あり） |

### V2-W3. フックサイト per-ROM テーブルのモジュール散在

各フック `.inc` / `.cpp` が独自の struct + 7行テーブル + lookup を持つ（実測9系統）:
`kImmediateOverlayActionConsumerPc` / `kNativeZoomToggleSites` / `kLowLatencyAimHookSites` /
`kWeaponSwitchActiveCallSites` / `kNativeBipedFireSites` / `kAltFormVSteerInputHooks` /
`kAltStoredHooks` / ShadowFreeze (.cpp内) / FixNoxusBlade (.cpp内)。

具体的な重複の証拠: action-consumer PC `0x02024174/0x02024198/0x02024190`（JP/US/EU）が
[MelonPrimePatchImmediateInputEdgeOverlay.inc:12-18](../../src/frontend/qt_sdl/MelonPrimePatchImmediateInputEdgeOverlay.inc#L12) と
[MelonPrimePatchNativeZoomToggleHook.inc:50-56](../../src/frontend/qt_sdl/MelonPrimePatchNativeZoomToggleHook.inc#L50) に
独立定義。**ただし KR1_0 は `0x0200F6DC` vs `0x0200D07C` と食い違う** — 意図的（別関数）か転記ミスかの
監査が必要（Phase 4）。また、これらモジュール内テーブルは Phase 3a で X-macro テーブルに入った
main-RAM 範囲 `static_assert` 検証の**対象外**のまま。

### V2-W4. 孤児ファイル・legacy キー（V1積み残し）

| 対象 | 証拠 |
|---|---|
| `MelonPrimePatch.cpp` | 中身1行（`// Replaced by ...` コメントのみ）。CMakeLists 未登録の孤児ファイル |
| `Metroid.Aim.Enable.NativeDeltaHook` | V1 Phase 4-4 で「ライブ read 無し（デフォルト定義 + CfgKey 定数のみ）」と確定済み。削除未実施 |
| `Metroid.Aim.Enable.InstantAimFollow` | 互換キー。migration は3層に分散（バナー付与済み）。「次回リリース後に削除検討」のまま |

### V2-W5. OSDカラーのミニドメイン多重定義

メッセージ別 OSD カラー（約11メッセージ × R/G/B 3キー）が
`kSecOsd*` テーブル（[MelonPrimeInputConfig.cpp:1589-1676](../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp#L1589)）、
`MelonPrimePatchOsdColor.cpp`（リテラル55個）、Config.cpp デフォルトの3面に手書きミラー。
メッセージ1種の追加 = 3箇所更新。

### V2-W6. transient reset の非対称（V1 Phase 4 の「Phase8候補」未決着）

- `OnEmuStart` に `TR_BipedFire` 無し
- `OnEmuStop` に `TR_AimResiduals` / `TR_OverlayHeld` 無し

V1 は「歴史的差分を保存」して統合を見送り、Phase 8（ドキュメントのみ）でも未決着。
意図的として恒久文書化するか、統一するかを決める。

### V2-W7. upstream 統合点の残り（V1 Phase 7 の続き・任意）

`#ifdef MELONPRIME*` ブロック実測: EmuThread.cpp **27** / Screen.cpp **23** / Window.cpp **14** /
Config.cpp **14** / EmuInstanceInput.cpp **10** / EmuInstance.h **9** / Screen.h **9**。
V1 Phase 7 は EmuThread の自己完結分のみ切り出し済み。マージ予定がなければ今回もスキップ可。

### V2-W8. その他（小粒・確認済み）

- `FixWifi` の「先頭1ワード canary = 51ワード適用済みとみなす」検証は V1 から suspicious 指定のまま
  （`NoPickingUpSpecificItems` のマスク選択型ステートマシンは**意図的**と文書化済み — 触らない）
- HUD系キーは `MelonPrimeDef.h::CfgKey` に**未登録**（CfgKey は非HUD約55定数のみ）。
  リテラル直書き禁止ルール（V1 Phase 5b）が HUD ドメインには未適用

---

## 2. 不変条件 — 絶対に壊さないもの

**V1 §2（do-not-touch リスト・規律・共通DoD）を全文継承**した上で、V2 固有の追加:

1. **設定の保存名・型・デフォルト値は1ビットも変えない**。スキーマ化は「定義場所の統一」であって
   「値の変更」ではない。各コミットでキー集合・デフォルト値の前後一致を機械検証する（Phase 0 の監査スクリプト）
2. スキーマ行は**手打ちしない**。既存コードからの機械抽出（Python等）で生成し、抽出時点で
   面間矛盾を検出・記録する（V1 Phase 3a の X-macro 変換と同じ手法）
3. ダイアログのウィジェット**生成順・objectName・シグナル接続順を保存**する
   （V1 Phase 5b の教訓: セグメント順序が観測可能な副作用を持つ）
4. 編集モードのスナップショット/リストア/リセットのキー集合を前後比較する
5. フックテーブルの統合は「mphCodex の根拠付きで同一関数と確認できた値」のみ。
   KR1_0 の食い違いはデフォルト**分離維持**（[mphcodex-reference.md](mphcodex-reference.md) のバージョン規律に従い、
   出典コメントに ROM バージョンを併記）
6. ビルドは `.\.claude\skills\build-mingw.bat`（CMake変更なしフェーズは `-existing` 可）、`--jobs 1` 厳守
7. `git reset --hard` 等の破壊的操作禁止。フェーズ = ブランチ、ロールバック = revert

---

## 3. スモークチェックリスト

**V1 §3 の S1–S12 をそのまま使う**（[completed/melonprime-full-refactor-plan.md](completed/melonprime-full-refactor-plan.md#3-スモークチェックリスト手動検証プロトコル) 参照）。
V2 で追加する項目:

| # | 項目 | 確認内容 |
|---|---|---|
| S13 | HUD編集モード往復 | 編集モードで各要素のプロパティ変更 → Save / Cancel(スナップショット復元) / Reset-to-default の3経路 → 設定ダイアログ側と値一致 |
| S14 | TOML往復 | 設定保存 → TOML のキー集合・値が変換前ビルドと完全一致（diff ゼロ。監査スクリプトで機械実行） |
| S15 | dev-OSD フック数 | 開発ビルドで `ARM9 hooks: registered (N PCs)` の N がフェーズ前後で不変 |

**Phase 2 は S9 / S10 / S13 / S14 必須**。Phase 4 は S2 / S3 / S15 必須。

---

## 4. フェーズ計画

### Phase 0: ベースラインと監査ツール（0.5日 / リスクなし）

1. V1 と同一手法（`wc -l`）で LOC スナップショットを再計測し本ファイル末尾に記録
   （参考: 2026-06-11 の `Get-Content` 計測では MelonPrime* 全体 114ファイル・約23,600行。手法差があるため Phase 0 で正式値を取る）
2. クリーンビルド成功確認、`melonDS.exe` サイズ記録
3. **キー監査スクリプトの新設** `.claude/skills/audit-hud-key-parity.ps1`:
   - Config.cpp の `Metroid.*` デフォルト（キー/型/値）をダンプ
   - ダイアログ `kSec*`・編集 `kProps*`/`kEditElems`・サイドパネル `populate*`・`Load*Config` それぞれの
     参照キー集合を抽出し、**面間の差集合を出力**（= 既存ドリフトの検出。Phase 2 の変換正当性証明にも使う）
   - 実ユーザー設定 TOML の保存前後 diff 機能
4. §3 スモークのベースライン実施（壊れていた項目の濡れ衣防止。V1 と同じ理由）

**成果物:** 計測値の追記 + 監査スクリプト。コード変更なし。

---

### Phase 1: 死活整理・小粒の決着（0.5–1日 / リスク極小）

| # | 作業 | 対象 |
|---|---|---|
| 1-1 | `MelonPrimePatch.cpp`（1行の孤児ファイル）を削除。直前に全文 grep で参照ゼロを最終確認 | 同ファイル |
| 1-2 | `Metroid.Aim.Enable.NativeDeltaHook` の Config.cpp デフォルト行と `CfgKey::NativeAimDeltaHook` 定数を削除（V1 でライブ read 無しと確定済み。読み手が現れたらビルドが落ちるので安全） | Config.cpp / MelonPrimeDef.h |
| 1-3 | `Metroid.Aim.Enable.InstantAimFollow` migration は**温存**し、3箇所のバナーコメントに「削除予定: 次回リリース後」と期日を明記（削除はしない） | InputConfig 2箇所 / runtime 1箇所 |
| 1-4 | 公開APIの呼び出しゼロ再スイープ（機械確認できるもののみ）。`drainMessagesOnly` は使用中と確認済み — 触らない | 各所 |
| 1-5 | スイープ中に見つかる stale コメントの修正（`git diff -w` が小さく収まる範囲） | 各所 |

**DoD:** フル configure ビルド + S10。
**期待削減:** 約 -20〜-60行（このフェーズは行数より「嘘の除去」が目的）。

---

### Phase 2: HUDプロパティ・スキーマ統一（3–5日 / リスク中 / **本丸**）

**狙い:** `Metroid.Visual.*` 549キーのドメインに単一スキーマを導入し、
デフォルト / ダイアログ表 / 編集記述子 / サイドパネル行 / ランタイム参照キーをそこから導出する。

#### 2a. 機械抽出とスキーマ生成（コード変更なしで完結）
- Python ワンオフスクリプトで既存5面（Config.cpp / kSec* / kProps* / populate* / Load*Config）から
  キー・型・デフォルト・範囲・ラベル・所属セクション・編集行種別を抽出
- **面間矛盾レポート**を出す（範囲の不一致、ダイアログのみ存在、編集パネルのみ存在、型不一致）。
  矛盾は「現状維持フラグ」としてスキーマに記録し、**黙って正規化しない**
- 生成物: `MelonPrimeHudPropSchema.inc`（X-macro、1行=1プロパティ）

```cpp
//  X(keyLiteral, CfgType, default, min, max, "Label", DialogSec, EditElem, EditRowKind, flags)
    X("Metroid.Visual.HudHpX", Int, -71, -256, 256, "Offset X", SEC_HP, ELEM_HP, ROW_OFFSET_X, 0)
```

- 同時に `CfgKey::Visual::*`（または同等の定数群）をスキーマから生成し、HUDキーにも
  「リテラル直書き禁止」を適用可能にする

#### 2b. Config.cpp デフォルトのスキーマ駆動化
- `DefaultInts` / `DefaultBools` / `DefaultDoubles` の Visual 区画をスキーマ X-macro 展開に置換
- **型別リストの仕分けはスキーマの CfgType 列から自動で正しくなる**
  （repo-architecture.md「Default value type classification — CRITICAL」の事故を構造的に防止）
- 監査: 変換前後の `{キー → 型, 値}` 完全一致をスクリプトで証明してからコミット

#### 2c. ダイアログテーブルのスキーマ駆動化
- `kSec*` の `HudWidgetProp` 行をスキーマから生成（セクション割り・行順は現状コード順を保存）
- ラベル文字列は英語ソースのまま（Localization 機構は無変更）
- **要素単位（HP→HPゲージ→…）の小コミット**。各コミットで監査スクリプト + ダイアログの
  objectName ツリー diff

#### 2d. 編集モード両系のスキーマ駆動化
- オンスクリーン側 `kProps*` / `kEditElems` をスキーマ参照に置換
- Qtサイドパネル側 `populate*()` の手書き列挙をスキーマ反復 + 既存 `addXxx` ヘルパー呼び出しに置換
  （ヘルパー自体・行の見た目・順序は不変）
- [Snapshot.inc:65-67](../../src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenSnapshot.inc#L65) 等の
  リテラル残りも記述子反復に畳む

#### 2e. ランタイム参照の定数化（データ駆動化はしない）
- `Load*Config` / Draw系 / `OsdColor` 以外の HUD リテラルを `CfgKey::Visual::*` 参照へ機械置換
- 構造体・代入・`ApplyAnchor`・キャッシュ機構は**1行も変えない**

**DoD:** フル configure ビルド + **S9 / S10 / S13 / S14 / S6** + 監査スクリプト green（面間差集合が
Phase 2a 時点のレポートと同一 = 新たなドリフトを作っていない）。
**期待削減:** 約 -600〜-1,100行。HUDプロパティ追加の触り箇所 **5〜6 → 2**（スキーマ1行 + 消費側の使用箇所）。
**期待副産物:** 2a の矛盾レポートが既存の面間ドリフトバグを浮かび上がらせる。

---

### Phase 3: `MelonPrimeInputConfig.cpp` の分割（0.5–1日 / リスク低）

Phase 2 でテーブルがスキーマ駆動になった後の物理分割（純移動のみ、ロジック不変）:

| # | 作業 |
|---|---|
| 3-1 | HUDテーブル/スキーマ消費部を `InputConfig/MelonPrimeInputConfigHudTables.inc` へ移動 |
| 3-2 | `setupCustomHudWidgets`（約755行）を `InputConfig/MelonPrimeInputConfigCustomHudBuild.inc` へ移動（既存の `MelonPrimeInputConfigCustomHudCode.inc` 末尾 include と同じ先例） |
| 3-3 | `check-inc-ownership.ps1` に新 `.inc` の1親検査を追加。CMakeLists には**追加しない** |
| 3-4 | 本体 .cpp の目標: **1,200行以下** |

**DoD:** ビルド（CMake不変なので `-existing` 可）+ S10 + inc-ownership スクリプト green。

---

### Phase 4: フックサイトテーブルの整備と監査（1–1.5日 / リスク中）

| # | 作業 |
|---|---|
| 4-1 | 9系統のモジュール内 per-ROM テーブルを棚卸しし、**複数モジュールで同一の論理サイト**（action-consumer PC 系）を `MelonPrimeGameRomAddrTable.h` の X-macro 行（または共有定数）として一本化。各モジュールは参照に変更 |
| 4-2 | **KR1_0 食い違い監査**: `0x0200F6DC` vs `0x0200D07C` を mphCodex ダンプ（`mnt/data/mphDump/KR1_0.txt`）で照合。別関数なら「別サイトである」根拠コメントを両モジュールに残す。転記ミスなら修正（この場合のみ挙動変更 = バグ修正としてコミット分離） |
| 4-3 | 全モジュールテーブルに main-RAM 範囲の `static_assert` を付与（Phase 3a と同等の typo 検出網に入れる） |
| 4-4 | 共有 lookup ヘルパー（`romGroupIndex` 添字 + 範囲検査）の導入は**同型のものだけ**。トランポリンコード配列・ディスパッチ優先順は不変 |
| 4-5 | 出典コメントを mphcodex-reference.md の規律（ROMバージョン併記）で統一 |

**DoD:** フル configure ビルド + S2 / S3 / S15（フック登録数不変）+ 武器切替・ズーム・モーフの実機確認。
**備考:** ネイティブ武器切替のフリーズ疑い（既知・別件）はこの監査の通り道にある。**修正はスコープ外**だが、
4-2/4-3 で見つかった事実は `project_native_weapon_switch_freeze` 調査の材料として記録する。

---

### Phase 5: OSDカラー・ドメインの一本化（0.5–1日 / リスク低中）

- メッセージ一覧（id / 表示ラベル / R/G/B キー3個 / パッチ側アドレス参照）を X-macro 1行に集約
- 導出先: ダイアログ `kSecOsd*` 行 / `MelonPrimePatchOsdColor.cpp` のキー参照 / Config.cpp デフォルト
- `OsdColor_InvalidatePatch` / per-frame 再適用（pattern B）の**呼び出し構造は不変**
- Phase 2 のスキーマ機構に自然に乗るなら統合してよいが、パッチモジュールが絡むためコミットは分離

**DoD:** ビルド + S6（OSDカラー ON/OFF/色変更 → ゲーム内反映 → 復元）+ S14。

---

### Phase 6: ライフサイクル残課題の決着（0.5日 / リスク低）

| # | 作業 |
|---|---|
| 6-1 | transient reset 非対称（V2-W6）の決着。提案デフォルト: **挙動不変のまま恒久文書化**（各サイトに「意図的に BipedFire を含めない: 理由」コメント + 「Phase8候補」表記の除去）。統一する場合は S7/S8 の前後比較を必須とし単独コミット |
| 6-2 | `FixWifi` canary の扱い決定。提案デフォルト: 現挙動維持 + 「先頭ワード=全51ワードの代表」前提を関数コメントに明文化。全ワード検証へ強化する場合は挙動変更として分離コミット + S6 |
| 6-3 | V1 進捗表に残る「スモーク待ち」項目の消し込み（実施 or 不要判断を記録） |

**DoD:** ビルド + S7 / S8。

---

### Phase 7: upstream 統合点の継続削減（1–2日 / **任意・ストレッチ**）

V1 Phase 7 と同じ判断基準: **upstream マージ予定がなければスキップ**。
やる場合は Screen.cpp（23ブロック、ただし大半は既に `.inc` 化済みの呼び出し点）と Window.cpp（14）の
連続・自己完結ブロックのみ。EmuThread の残り27のうちフレームループ内タイミング系はインライン維持（V1 と同じ）。

**DoD:** フル configure ビルド + S2 + S9。

---

### Phase 8: ドキュメント整備・最終計測（0.5日 / リスクなし）

1. [melonprime-refactoring.md](melonprime-refactoring.md) に「Structural Refactor V2 2026-06」節を追記
2. [repo-architecture.md](repo-architecture.md) に HUDスキーマの所有点（スキーマ→5面導出の図）を追加。
   「新しいHUDプロパティの追加手順」を3ステップで記載
3. [melonprime-patch-system.md](melonprime-patch-system.md) のフックサイトテーブル節を Phase 4 後の形に更新
4. CLAUDE.md / rules README 更新、本ファイルを `completed/` へ移動しステータス「完了」
5. Phase 0 比の LOC / exe サイズ / リテラル数（目標: 1,801 → 300以下）を記録

---

## 5. リスクと対策

| リスク | 該当 | 対策 |
|---|---|---|
| スキーマ抽出の取り違え（549キー分） | 2a | 手打ち禁止・機械抽出。面間矛盾は「現状維持フラグ」で保存し黙って直さない。前後一致を監査スクリプトで証明してからコミット |
| ダイアログ生成順・auto-slot 副作用の変化 | 2c | 生成順を現行コード順に固定。要素単位コミット + objectName ツリー diff + S10 |
| 編集モードのスナップショット/復元の取り漏れ | 2d | スナップショットキー集合の前後 diff + S13（Save/Cancel/Reset 3経路） |
| 型別デフォルトリストの仕分けミス（GetBool が DefaultInts を見ない問題） | 2b | スキーマの型列から機械導出 + audit-config-defaults.ps1 必須実行 |
| フックテーブルの誤統合（KR系など別関数を同一視） | 4 | mphCodex ダンプ照合で同一と証明できた値のみ統合。食い違いはデフォルト分離維持 + 出典コメント |
| OsdColor は emulated RAM を書く | 5 | キー参照の置換のみ。apply/restore/invalidate の構造不変 + S6 |
| ビルドメモリ枯渇・タイムアウト | 全 | `--jobs 1` 厳守。タイムアウト後は build.md の復旧手順 |
| 巨大 diff によるレビュー不能化 | 2, 3 | 1コミット ≦ 約400行 diff 目安（V1 と同じ）。生成テーブルの置換は要素単位 |

---

## 6. 進捗トラッキング

| Phase | 内容 | 状態 | 完了日 | 結果メモ |
|---|---|---|---|---|
| 0 | ベースライン + キー監査ツール | 未着手 | — | |
| 1 | 死活整理（孤児ファイル / legacyキー / スイープ） | 未着手 | — | |
| 2 | HUDプロパティ・スキーマ統一（本丸） | 未着手 | — | |
| 3 | InputConfig.cpp 分割 | 未着手 | — | |
| 4 | フックサイトテーブル整備 + KR監査 | 未着手 | — | |
| 5 | OSDカラー一本化 | 未着手 | — | |
| 6 | ライフサイクル残課題決着 | 未着手 | — | |
| 7 | upstream統合点（任意） | 未着手 | — | |
| 8 | ドキュメント + 最終計測 | 未着手 | — | |

### Phase 0 計測値（実施時に記入）
- LOC（wc -l）: —
- melonDS.exe サイズ: —
- `"Metroid.*"` リテラル数: —（2026-06-11 参考実測: 1,801）
- スモーク基準: —

### 参考実測（2026-06-11、計画作成時。手法: PowerShell `Get-Content | Measure-Object -Line`）
- MelonPrime* 全体: 114ファイル / 約23,600行
- 上位: InputConfig.cpp 2,665 / HudConfigOnScreenDraw.inc 1,213 / InputConfig.ui 1,209 /
  Localization.cpp 1,094 / HudRenderConfig.inc 817 / MelonPrime.cpp 752 / HudConfigOnScreenEdit.cpp 736
- Config.cpp の Metroid.* デフォルト: 661キー（Visual 549）
- `#ifdef MELONPRIME*`: EmuThread 27 / Screen 23 / Window 14 / Config 14

---

## 7. 推奨着手順序

```
0 → 1 → 2 (本丸) → 3 → 5 → 4 → 6 → (7) → 8
```

- 0/1 は即効。2 が最優先の価値（HUDプロパティは今後も増え続けるため複利が効く — V1 のパッチレジストリと同じ論理）
- 3 は 2 の直後が最効率（スキーマ化済みテーブルを動かすだけ）
- 5 は 2 のスキーマ機構を流用できるため 2/3 の後に
- 4 は完全独立。mphCodex を開く集中作業なので「別の日の頭」で
- 6 は半日の決着作業。7 は upstream マージ計画がない限り省略

総見積り: **必須フェーズ（0–6, 8）で約7–10日相当**。各フェーズは独立コミット列なので中断・再開自由。
