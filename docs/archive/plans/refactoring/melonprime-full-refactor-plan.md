# MelonPrime 全面リファクタリング計画（Phase 0–8）

**作成日:** 2026-06-10
**対象ブランチ:** `highres_fonts_v3`（フェーズごとに作業ブランチを切る）
**ステータス:** 完了（2026-06-11、Phase 0-8 の構造リファクタ完了。手動スモーク未完了項目は進捗表に残す）

このドキュメントは、長年の機能追加・最適化ラウンド（R1–R2, P-1〜P-47, OPT A–Z5）で蓄積した
**構造的な無駄**を、動作とパフォーマンスを保ったまま段階的に解消するための実行計画である。
パフォーマンス史は [melonprime-refactoring.md](../../../architecture/history/melonprime-refactoring.md) が正典。本計画は
それと矛盾しない「構造の負債」だけを扱う。

---

## 0. ゴールと非ゴール

### ゴール
1. **パッチ追加コストの激減** — 現在「新パッチ1個 = 約8ファイル・10数箇所の手作業」を「モジュール1ファイル + レジストリ1行」にする
2. **同一情報の単一定義化** — アドレステーブル・武器定義・ROMバージョン・設定キーの重複排除
3. **確認済みデッドコードの削除** — 空スタブ、呼び出しゼロの関数、消滅ファイルへの参照
4. **ドキュメントとコードの矛盾解消** — 古いコメント・古い .md の整理
5. 結果として **正味 -1,500〜-2,500 行**（MelonPrime系 約30,500行中）と、変更時の「触り忘れ事故」の構造的防止

### 非ゴール（やらないこと）
- ホットパスの再最適化・再設計（R1〜Round 9 で完了済み。挙動・コード生成を保つことが正義）
- melonDS 本体（`src/` 直下のコア）の構造変更
- 新機能の追加
- `#ifdef MELONPRIME_DS` ガードの除去（フラグは常時ONだが、upstream との diff 衛生のため**ガードは維持**する）
- Compute Renderer モザイクバグ調査（[別ログ](../../investigations/rendering/compute-renderer-mosaic-bug.md)で継続）

---

## 1. 無駄の棚卸し（調査済み・証拠付き）

### W1. パッチライフサイクルの「ショットガン手術」 — 最大の負債
[MelonPrime.cpp](../../../../src/frontend/qt_sdl/MelonPrime.cpp) 内に同型のパッチ呼び出しリストが分散:

| 呼び出し | 件数 | 散在箇所 |
|---|---|---|
| `*_ResetPatchState()` | **39回** | `OnEmuStart` / `ResetRuntimeStateForBoot` / `OnEmuStop` の3つのほぼ同一ブロック |
| `*_RestoreOnce()` | **16回** | `OnEmuStart` / `OnEmuStop` / `RunFrameHook` のゲーム離脱ブロック |
| `*_ApplyOnce()` | **16回** | `HandleGameJoinInit` / `ApplyConfigReload` / `RunFrameHook` の `!isInGame` ブロック |

[melonprime-patch-system.md](../../../architecture/gameplay/patch-system.md) 自身が「3ブロックのどれか1つでも追加し忘れると stale state が残る」と
警告している = **設計が人為ミスを前提にしている**。

### W2. パッチモジュールの定型コード重複
`PatchWord` 構造体・`IsValidRomGroup`・`CanWritePatch`・`WritePatch`・`s_applied`/`s_appliedRomGroupIndex`
の組が、少なくとも6モジュール（ShowHeadshotOnline / ShowEnemyHpMeterOnline / DisableDoubleDamageMultiplier /
NoPickingUpSpecificItems / InstantAimFollow / FixWifi）でほぼコピペ（grep で34箇所一致）。
1モジュールあたり約60–80行 × 6 ≒ **400–500行が純粋な重複**。
他の write-patch（AspectRatio / OsdColor / LowHpWarning / NoHud / ExpandStageMatrix / UseFirmwareLanguage /
NoDoubleTapJump）も微妙に違う独自実装を持つ。

### W3. 確認済みデッドコード
| 対象 | 証拠 |
|---|---|
| `MelonPrimeCore::PrePollRawInput()` 空インライン | [MelonPrime.h:285](../../../../src/frontend/qt_sdl/MelonPrime.h#L285)。呼び出しゼロ（grep確認済み）。コメント自身が「remove in future cleanup」 |
| `RawInputWinFilter::Poll()` | [MelonPrimeRawInputWinFilter.cpp:96](../../../../src/frontend/qt_sdl/MelonPrimeRawInputWinFilter.cpp#L96)。`->Poll(` / `.Poll(` の呼び出しゼロ（要最終確認のうえ削除） |
| `resetAllKeys()` / `resetMouseButtons()` 公開API | P-9 で `resetAll()` に置換済み。MelonPrime側から個別呼び出しは現存せずコメント内のみ（InputState 内部実装としては `resetAll` から使用継続） |
| CMakeLists の消滅ファイル参照 | `#MelonPrimeXInputFilter.cpp` 等4ファイル×3ブロック（[CMakeLists.txt:67-70, 156-159, 168-171](../../../../src/frontend/qt_sdl/CMakeLists.txt#L67)）。ファイル自体が存在しない |
| レガシー設定キー | `Metroid.Aim.Enable.NativeDeltaHook`（"legacy bool, kept for migration"）、`Metroid.Aim.Enable.InstantAimFollow`（互換キー）。マイグレーション経路が本流コードに混在 |

### W4. 同一ドメイン情報の多重定義
| 情報 | 定義1 | 定義2 | 問題 |
|---|---|---|---|
| 武器ID/マスク | [MelonPrimeDef.h](../../../../src/frontend/qt_sdl/MelonPrimeDef.h) の `WeaponId` / `WeaponMask` | [MelonPrimeGameWeapon.cpp](../../../../src/frontend/qt_sdl/MelonPrimeGameWeapon.cpp) の `WeaponData::ID` / `Info.mask` | 同じ9武器の enum とビットマスクが2系統 |
| ROMチェックサム | `MelonPrimeDef.h` の `RomVersions` | [MelonPrimeGameRomDetect.cpp](../../../../src/frontend/qt_sdl/MelonPrimeGameRomDetect.cpp) の `CHECKSUM_TABLE` | 定数→テーブルの2段。テーブル直書きで1段にできる |
| アドレステーブル | [MelonPrimeGameRomAddrTable.h](../../../../src/frontend/qt_sdl/MelonPrimeGameRomAddrTable.h) | — | 1アドレス追加 = `LIST_*` + 構造体フィールド + `CreateRomAddress` の**位置依存3点更新**。#ifdef で分断されており、順序ズレが silent bug になる |

### W5. ドキュメントとコードの矛盾（stale comments / stale docs）
- [MelonPrimeDef.h:122-125](../../../../src/frontend/qt_sdl/MelonPrimeDef.h#L122) は「gameCode が primary selector、checksum は表示ラベルのみ」と書くが、
  実装（RomDetect.cpp）と [melonprime-gameplay-runtime.md](../../../architecture/gameplay/runtime.md) は**checksum が primary**。コメントが旧仕様のまま
- [MelonPrimeZoomInputMethods.md](../../../features/input/zoom-input-methods.md) の config 値表（`0=New preset binding / 1=Legacy fixed R`）が
  `MelonPrimeDef.h` の `ZoomInputMethod`（`0=LegacyFixedR / 1=NewPresetBinding / 2=NewNativeToggle`）と矛盾
- `src/frontend/qt_sdl/` 直下に調査メモ .md が6本（ZoomInputMethods / PatchImmediateInputEdgeOverlay /
  PatchInstantAimFollow / PatchNoPickingUpSpecificItems / PatchShadowFreezeRuntimeHook /
  PatchNativeAimDeltaHookRegisterInjectionVersion）— ソースツリーと文書ツリーの混在

### W6. コア状態リセットの分散
`m_aimResidualX/Y` / `m_nativeAimDeltaX/Y` / `m_immediateOverlayPrevHeld` / `m_directTransformPendingFrames` /
`m_nativeBipedFire*` / `m_weaponSwitchPending` のリセットが `OnEmuStart` / `ResetRuntimeStateForBoot` /
`OnEmuStop` / フォーカス喪失 / ゲーム離脱 / `HandleGameJoinInit` の**6箇所に微妙に異なる部分集合**で散在。

### W7. UI/設定層の鏡写しコード
- [InputConfig/MelonPrimeInputConfig.cpp](../../../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp)（2,850行）の load 側と
  [MelonPrimeInputConfigConfig.cpp](../../../../src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigConfig.cpp)（274行）の save 側が
  キー文字列リテラルで手動ミラー（HUD系は `HudWidgetProp` テーブルでデータ駆動化済み — **非HUD系だけ取り残されている**）
- [MelonPrimeLocalization.h](../../../../src/frontend/qt_sdl/MelonPrimeLocalization.h)（1,212行の翻訳テーブル）が4TUにインクルードされ、
  コンパイル時間と .obj サイズを無駄に消費

### W8. その他
- [MelonPrimeGameInput.cpp](../../../../src/frontend/qt_sdl/MelonPrimeGameInput.cpp) が8本のフック `.inc`（計約2,100行）のユニティ親で、
  「入力処理」の名前と実態が乖離
- `MelonPrimeHudConfigOnScreen.cpp`（unity-include される .cpp）と `MelonPrimeHudConfigOnScreenEdit.cpp`（独立TUのQtウィジェット）の
  命名衝突は Phase 6 で `MelonPrimeHudConfigOnScreenUnity.inc` への改名により解消。`.cpp` を `#include` する慣習も禁止化済み
- [EmuThread.cpp](../../../../src/frontend/qt_sdl/EmuThread.cpp) に `#ifdef MELONPRIME_DS` ブロックが31個 — upstream マージの摩擦源
- CMakeLists の Win32 ソースリストが3箇所に重複

---

## 2. 不変条件 — 絶対に壊さないもの

### 2.1 ホットパス do-not-touch リスト
以下はリファクタ対象外。触る場合は単独コミット + 計測（最低限ディスアセンブリ比較）必須:

- `ProcessAimInputMouse` / `ApplyAim` / `ClampAimResidual`（[MelonPrimeGameInput.cpp](../../../../src/frontend/qt_sdl/MelonPrimeGameInput.cpp)）
- `snapshotInputFrame` / `processRawInputBatched` / `clearStuckMouseButtons`（[MelonPrimeRawInputState.cpp](../../../../src/frontend/qt_sdl/MelonPrimeRawInputState.cpp)）— click/hold ドロップ修正の塊（[melonprime-click-handling.md](../../investigations/input/click-handling.md)）
- `RunFrameHook` の分岐構造・`HandleInGameLogic` の処理順
- `ProjectDownState` / `ProjectPressMask` / `MoveLUT` のビットパッキング
- EmuThread のフレームループ（P-11/12/27/40/45/46/47 適用部）
- `MelonPrimeArm9InstructionHook.inc` の JIT トランポリン機構
- **`MelonPrime.h` のメンバ宣言順**（cache-line レイアウトは意図的。メンバの追加・削除はコールドセクション末尾のみ可）

### 2.2 規律
- 非 `MelonPrime*` ファイルへの追加は必ず `#ifdef MELONPRIME_DS` ガード（既存ルール厳守）
- Raw Input 層の Single-Writer アトミック規律（relaxed load + release store）を変えない
- `.inc` は新規TUにしない（CMakeLists に追加しない）。各 `.inc` の親は1ファイルのみ
- 設定キーの**保存名は変えない**（ユーザーの TOML 互換性）。キー定数の置き場所だけ統一する
- `git reset --hard` 等の破壊的操作禁止。フェーズ = ブランチ、ロールバック = revert

### 2.3 各フェーズ共通の完了条件（DoD）
1. `.\tools\build\windows\build-mingw.bat` 成功（CMake変更があるフェーズは configure 込み、なければ `build-mingw-existing.bat` 可）
2. §3 のスモークチェック該当項目をパス
3. 影響する `docs/architecture/`・`docs/plans/` と短いstanding ruleを同コミットで更新（嘘ドキュメントを残さない）
4. このファイルの進捗表を更新
5. コミットは論理単位ごと（1コミット ≦ 約400行 diff 目安）

---

## 3. スモークチェックリスト（手動検証プロトコル）

フェーズの影響範囲に応じて選択実行。**Phase 2 と 3 は全項目必須**。

| # | 項目 | 確認内容 |
|---|---|---|
| S1 | ROM検出 | 起動→ROMロード→OSD「MPH Rom Detected: <ver>」。可能なら US1.1 + もう1リージョン |
| S2 | ゲーム参加 | マルチ対戦開始→マウスエイム・移動・ジャンプ・射撃 |
| S3 | 武器切替 | ホイール / 直接ホットキー / Next/Prev、Omega制限メッセージ |
| S4 | モーフ/ブースト | モーフボール変形、Samusブースト（エイムブロック解除含む） |
| S5 | Adventure | スキャンバイザー、ポーズ→カーソルモード遷移、UI OK/YES/NO |
| S6 | パッチON/OFF | Headshot表示・敵HPメーター・DD倍率無効・アイテム不取得を設定切替→ゲーム内反映→OFF→復元 |
| S7 | ライフサイクル | ポーズ/再開、リセット、停止→再起動でパッチ状態が漏れない（S6再確認） |
| S8 | フォーカス | Alt-Tab→復帰でスタックキーなし、エイム残差ジャンプなし |
| S9 | Custom HUD | HUD表示、編集モード（ドラッグ・プロパティパネル）、レーダーオーバーレイ |
| S10 | 設定ダイアログ | 全セクション開閉、保存→再起動→値保持（config監査スクリプトも実行） |
| S11 | 感度ホットキー | IngameSensiUp/Down で OSD 更新 |
| S12 | スタイラスモード | タッチ操作とモーフ干渉なし |

設定キー検証は [repo-architecture.md](../../../architecture/repository.md) の **GetXxx Default Coverage Audit** PowerShell スクリプトを流用する。

---

## 4. フェーズ計画

### Phase 0: ベースラインとセーフティネット（0.5日 / リスクなし）
**目的:** 以後の全フェーズの比較基準を作る。

1. クリーンビルドを通し、`melonDS.exe` のサイズ・ビルド時間を記録
2. §3 スモーク全項目を**現状のコードで**一度実施し、結果をこのファイル末尾に記録（「壊れていた項目」をリファクタのせいにしない/されないため）
3. `wc -l` ベースの LOC スナップショット（MelonPrime* / InputConfig/MelonPrime* / *.inc）
4. `.inc` 所有関係の検査スクリプトを `tools/` に追加
   （各 `.inc` が想定親1ファイルからのみ include されることを grep で確認する PowerShell。Phase 6 でも使う）

**成果物:** 計測値をこのファイルに追記。コード変更なし。

---

### Phase 1: 確認済みデッドコードの除去（0.5–1日 / リスク極小）
**原則: 「呼び出しゼロを機械的に確認できるもの」だけ**。判断が要るものは後続フェーズへ。

| # | 作業 | 対象 |
|---|---|---|
| 1-1 | `PrePollRawInput()` 空インライン削除 + 参照コメント整理 | MelonPrime.h:282-285、EmuThread.cpp の言及コメント2箇所は「P-33で除去済み」の1行に圧縮 |
| 1-2 | `RawInputWinFilter::Poll()` 削除（最終 grep 確認後） | MelonPrimeRawInputWinFilter.h/.cpp |
| 1-3 | `resetAllKeys`/`resetMouseButtons` の **RawInputWinFilter 公開ラッパー**削除（`resetAll` のみ残す。InputState 内部実装は維持） | 同上 |
| 1-4 | CMakeLists: 存在しない4ファイルのコメント参照を3ブロックすべて削除 | CMakeLists.txt |
| 1-5 | src直下の調査 .md 6本を `docs/archive/`（新設）へ移動し、`ZoomInputMethods.md` の矛盾した config 値表を実装に合わせて修正 | §W5 |
| 1-6 | stale コメント修正: MelonPrimeDef.h:122-125 のROM検出説明を「checksum primary / header fallback」に書き換え | MelonPrimeDef.h |
| 1-7 | 末尾空行・文字化け残骸の掃除（HudRender.cpp 末尾等。`git diff -w` が空になる変更のみ） | 各所 |

**注意:** 1-2/1-3 はヘッダ公開APIの削除なので、ビルドは**フル configure** で確認。
**DoD:** ビルド + S8（入力リセット系を触るため）+ S10。
**期待削減:** 約 -150〜-250行。

---

### Phase 2: パッチサブシステム統一（2–4日 / リスク中 / **本丸**）

#### 2a. 共通スキャフォールディングの抽出
新規 `MelonPrimePatchCommon.h`（+必要なら .cpp）に以下を一本化:

```cpp
namespace MelonPrime {
    struct PatchWord { uint32_t address, applyVal, revertVal; };

    // 7 ROMグループ × 任意ワード数の静的 write-patch を一般化。
    // 既存6モジュールの s_applied / s_appliedRomGroupIndex / CanWritePatch /
    // WritePatch / Apply-Restore-Reset の状態機械をそのまま移植する
    // （挙動変更なし。CanWritePatch の「期待値 or 適用済み値」検証も維持）。
    class StaticWordPatch {
    public:
        constexpr StaticWordPatch(const PatchWord* perRomWords, /* ROMごと words/counts */ ...);
        void ApplyOnce(melonDS::NDS*, uint8_t romGroupIndex);
        void RestoreOnce(melonDS::NDS*, uint8_t romGroupIndex);
        void ResetState();
    private:
        bool m_applied = false;
        uint8_t m_appliedRomGroupIndex = 0xFF;
        ...
    };
}
```

- まず **ShowHeadshotOnline / ShowEnemyHpMeterOnline / DisableDoubleDamageMultiplier** の同型3モジュールを変換（最小差分で等価性を確認しやすい）
- 次に NoPickingUpSpecificItems / InstantAimFollow / FixWifi（複数ワード・条件付き）を変換
- 変形パターン（OsdColor の per-frame 再評価、ExpandStageMatrix の ApplyIfLoaded、UseFirmwareLanguage の追加引数）は**無理に共通化しない**。既存シグネチャのまま 2b のレジストリにだけ載せる

#### 2b. パッチレジストリの導入
新規 `MelonPrimePatchRegistry.h/.cpp`:

```cpp
struct PatchCtx {            // 全 apply シグネチャの統一コンテキスト
    melonDS::NDS* nds;
    EmuInstance* emu;
    Config::Table& cfg;
    const RomAddresses& rom;
};
enum PatchSite : uint8_t {   // どのライフサイクル点で動くか
    Site_GameJoin = 1, Site_ConfigReload = 2, Site_OutOfGameFrame = 4,
};
struct PatchEntry {
    const char* name;
    uint8_t applySites;                    // PatchSite ビットマスク
    void (*apply)(const PatchCtx&);        // 各モジュールへの thin アダプタ
    void (*restore)(const PatchCtx&);      // null = restore なし
    void (*resetState)();                  // null = 状態なし（pattern C）
};
inline constexpr PatchEntry kPatchRegistry[] = { /* 全 write-patch を列挙 */ };
```

そして MelonPrime.cpp の呼び出しリスト群を置換:
- `OnEmuStart` / `ResetRuntimeStateForBoot` / `OnEmuStop` の3ブロック → `Patches_RestoreAll(ctx)` + `Patches_ResetAll()` の各1行（Restore の有無の差分は現挙動を表に落としてから移植。**現3ブロックは微妙に内容が違う**: ResetRuntimeStateForBoot は RestoreOnce を呼ばない。レジストリ側に `restoreOnBoot` 相当の区別を持たせず、呼び出し側で「Restore してから Reset」「Reset のみ」を選ぶAPIにする）
- `HandleGameJoinInit` の8連 ApplyOnce → `Patches_Apply(Site_GameJoin, ctx)`
- `ApplyConfigReload` の5連 → `Patches_Apply(Site_ConfigReload, ctx)`
- `RunFrameHook` の `!isInGame` ブロック3連 → `Patches_Apply(Site_OutOfGameFrame, ctx)`
- ゲーム離脱ブロックの5連 RestoreOnce → `Patches_RestoreOnLeave(ctx)`

**やらないこと:** ARM9 instruction-hook 群（ShadowFreeze / NoxusBlade / WeaponSwitch 等）は
`ARM9Hook_Install` が既にレジストリとして機能しているので**対象外**。フックモジュールの
`*_ResetPatchState` だけレジストリの resetState 経由に載せる。

**静的状態について:** 各モジュールの `s_applied` はプロセスグローバルだが、melonDS の
マルチインスタンスは別プロセスなので実害なし。**Phase 2 では statics を維持**し、
PatchCommon.h に「per-process singleton 前提」と明記するに留める。

#### 2c. ドキュメント更新
[melonprime-patch-system.md](../../../architecture/gameplay/patch-system.md) のチェックリストを新手順
（モジュール作成 → レジストリ1行 → UI/Config の3ステップ）に書き換え。旧3ブロック警告を削除。

**DoD:** フル configure ビルド + スモーク**全項目**（特に S6/S7 を ON→OFF→ON、停止→再開をまたいで）。
**期待削減:** 約 -700〜-1,100行。新パッチ追加時の触り箇所 約8 → 3（モジュール / レジストリ / UI+Config）。

---

### Phase 3: ドメインテーブルの単一情報源化（1–2日 / リスク中）

#### 3a. ROMアドレステーブルの X-macro 化
[MelonPrimeGameRomAddrTable.h](../../../../src/frontend/qt_sdl/MelonPrimeGameRomAddrTable.h) を
「1アドレス = 1行」の単一定義に変換:

```cpp
//                 name                JP1_0       JP1_1       US1_0       US1_1       EU1_0       EU1_1       KR1_0
#define MP_ROM_ADDR_FIELDS_CORE(X) \
    X(playerStructStart, 0x020DC5D4, 0x020DC594, 0x020DA714, 0x020DAF94, 0x020DAFB4, 0x020DB034, 0x020D3DE0) \
    ...
```

- マクロ展開で `LIST_*` テーブル・`RomAddresses` 構造体・`CreateRomAddress` を**全自動生成** → 位置依存3点更新を1点に
- `#ifdef MELONPRIME_CUSTOM_HUD` / `MELONPRIME_DS` ブロックはマクロを分割（`_CORE` / `_HUD` / `_DS`）して維持
- **constexpr 検証を追加:** 全アドレスが `0x02000000–0x023FFFFF`（main RAM）または既知のコード領域に収まることを `static_assert` で網羅（テーブル typo の構造的検出）
- 変換は機械的だが幅が広いので、**変換前後で `ROM_INSTANCES` の全バイト一致を static_assert か一時テストコードで証明**してからコミット

#### 3b. 武器定義の統一
- `WeaponData::ID` を廃止し `MelonPrimeDef.h` の `WeaponId` に一本化（`WeaponMask` も同様）
- `ORDERED_WEAPONS` の `mask` は `WeaponMask::*` から導出
- minAmmo の歴史的注意（過去のリファクタで壊した実績あり、[GameWeapon.cpp:47-51](../../../../src/frontend/qt_sdl/MelonPrimeGameWeapon.cpp#L47)）が
  あるため、**値の表自体は1文字も変えない**。enum の参照先だけ差し替え

#### 3c. ROMバージョン定数の整理
- `RomVersions::*` 定数を `CHECKSUM_TABLE` 直書きに畳み込み（使用箇所が CHECKSUM_TABLE のみであることを確認済み。他で参照が出たら中止）
- mphCodex 由来のアドレス・チェックサムを触る場合は [mphcodex-reference.md](../../../reverse-engineering/mphcodex-workflow.md) の
  バージョン規律（ROMバージョン併記）に従って出典をコメントに残す

**DoD:** フル ビルド + S1（**複数リージョンROMで必須**）+ S2/S3/S6。
**期待削減:** 約 -150〜-250行 + 将来のアドレス追加が1行化。

---

### Phase 4: コアライフサイクル整理（1日 / リスク低中）

| # | 作業 |
|---|---|
| 4-1 | `ResetTransientInputState()`（仮称）を導入し、§W6 の6箇所に散在するリセット集合を共通化。**各箇所の現リセット内容を表に起こし、差分（意図的なものか漏れか）をコミットメッセージに記録**してから統合。意図的な差分は引数フラグで表現 |
| 4-2 | `MelonPrime.h` の整理: 9フック分の `*_GetAddresses` / `*_DispatchCheck*` 宣言（約80行）をセクションコメントで構造化（並べ替えのみ、シグネチャ不変）。`WeaponSwitchPendingRequest` 等の内部構造体定義はそのまま（メンバ順序固定の原則） |
| 4-3 | `FilterDeleter`（ptr を無視して static `Release()` を呼ぶ特異なカスタムデリータ）に意図コメントを付与、または小さな RAII ハンドル型に置換（任意） |
| 4-4 | レガシー設定キーのマイグレーションを `MigrateLegacyConfigKeys()` 1関数に隔離（NativeDeltaHook bool → NativeHookMode、Enable.InstantAimFollow → LowLatencyMode=3）。**削除はしない**（次回リリース後に削除検討、と関数コメントに明記） |

**DoD:** ビルド + S7/S8（リセット系）+ S2。

---### Phase 5: UI/設定層のデータ駆動化（2–3日 / リスク低・作業量大）

#### 5a. Localization の TU 化
`MelonPrimeLocalization.h` の翻訳テーブル本体を新規 `MelonPrimeLocalization.cpp` に移し、
ヘッダは宣言と `Tr()` 系インターフェースのみに（4TU × 1,212行のパース削減）。
CMakeLists にソース追加（`.inc` ではないので追加してよい）。

#### 5b. 非HUD設定のバインディングテーブル化
HUD系で実績のある `HudWidgetProp` パターンを非HUD設定に展開:

```cpp
struct SettingBinding {
    const char* key;          // CfgKey::* を参照（リテラル直書き禁止）
    WidgetKind kind;          // CheckBox / Combo / SpinBox / DoubleSpin
    QWidget* MelonPrimeInputConfig::* widget;  // または objectName 文字列
};
```

- `setupSensitivityAndToggles`（218行）の load と `saveConfig` の save を同一テーブルから駆動し、**鏡写しドリフトを構造的に根絶**
- 特殊ロジック（developer-only ガード、依存 enable/disable、Qt auto-slot）は既存パターンを変えずテーブル外に残す（[melonprime-developer-only-section.md](../../../development/developer-features.md) の必須パターン遵守）
- 設定キー文字列リテラルの直書きを `MelonPrimeDef.h` の `CfgKey` 参照に統一（保存名は不変）

#### 5c. 監査の自動化
repo-architecture.md の Default Coverage Audit スクリプトを `tools/ci/audits/audit-config-defaults.ps1` として
チェックイン（現在はドキュメント内コピペ運用）。Phase 5 の DoD で実行。

**DoD:** ビルド + S10（保存→再起動→全値保持を重点的に）+ S6 + 監査スクリプト green。
**期待削減:** 約 -300〜-500行 + 翻訳/設定追加時の触り箇所半減。

---

### Phase 6: HUD/ユニティ断片の整合（0.5–1日 / リスク低）

| # | 作業 |
|---|---|
| 6-1 | Phase 0 の `.inc` 所有検査スクリプトを CI 的に整備（全 `.inc` / unity `.cpp` の親が規約どおり1つであることを機械検証） |
| 6-2 | `MelonPrimeHudConfigOnScreen.cpp` → `MelonPrimeHudConfigOnScreenUnity.inc` へ改名（`#include "*.cpp"` の解消）。[build.md](../../../development/build/overview.md) / [repo-architecture.md](../../../architecture/repository.md) / [melonprime-refactoring.md](../../../architecture/history/melonprime-refactoring.md) §18 の該当記述を同時更新 |
| 6-3 | `MelonPrimeGameInput.cpp` のフック `.inc` 8連 include に「ここはフック実装の unity 親」セクションコメントを付け、ファイル先頭に所有 `.inc` 一覧を明記（移動はしない — 移動は ApplyAim 等への依存があるため不可と明記） |

**DoD:** フル configure ビルド + S9（HUD/編集モード全般）。

---

### Phase 7: 統合点の集約（1–2日 / リスク中 / **任意・ストレッチ**）
upstream (melonDS) 追従コストの削減が目的。**機能的価値はないので、マージ予定がなければスキップ可。**

- EmuThread.cpp の31個の `#ifdef MELONPRIME_DS` ブロックのうち、**連続・自己完結なもの**を
  `MelonPrimeEmuThread*.inc` に切り出し（Screen.cpp 方式の踏襲）。include 位置は完全同一 = コード生成不変
- フレームループ内のタイミング系（P-11/12/27/40/45）は**インライン維持**（切り出すと読みにくくなるだけ）
- CMakeLists の Win32 ソースリスト3重複を変数化で1本に

**DoD:** フル configure ビルド + S2 + フレームペーシング体感確認（60fps維持）。

---

### Phase 8: ドキュメント最終整備（0.5日 / リスクなし）
1. [melonprime-refactoring.md](../../../architecture/history/melonprime-refactoring.md) に「Structural Refactor 2026-06」として本計画の結果サマリを1節追記（正典の歴史を途切れさせない）
2. [melonprime-patch-system.md](../../../architecture/gameplay/patch-system.md) / [repo-architecture.md](../../../architecture/repository.md) /
   [melonprime-gameplay-runtime.md](../../../architecture/gameplay/runtime.md) の最終整合パス
3. CLAUDE.md インデックス更新、本ファイルのステータスを「完了」に
4. Phase 0 比の LOC / ビルド時間 / exe サイズを記録

---

## 5. リスクと対策

| リスク | 該当フェーズ | 対策 |
|---|---|---|
| パッチ restore/reset の意味差を均してしまい stale state 再発 | 2b | 統合前に現3ブロックの**差分表**を作りコミットメッセージに残す。S6/S7 を ON/OFF×停止/再開の組合せで実施 |
| アドレステーブル変換での1バイト typo | 3a | 変換前後の `ROM_INSTANCES` 全フィールド一致を static_assert で証明してから旧定義を削除 |
| minAmmo 等「過去に一度壊した」値の再破壊 | 3b | 値の表は不変、参照だけ差し替え。S3 で全武器の切替可否を確認 |
| ホットパスへの巻き込まれ（inline 境界の変化等） | 2, 4 | レジストリはコールドパス（join/reload/stop）のみに導入。RunFrameHook 内のループ化は per-frame サイトだけ影響を見る |
| UI バインディング化での load/save 片落ち | 5b | 監査スクリプト + S10。テーブル移行はセクション単位の小コミット |
| ビルドメモリ枯渇・タイムアウト | 全フェーズ | `--jobs 1` 厳守。タイムアウト後は build.md の復旧手順 |

---

## 6. 進捗トラッキング

| Phase | 内容 | 状態 | 完了日 | 結果メモ |
|---|---|---|---|---|
| 0 | ベースライン計測 | 部分完了 | 2026-06-10 | LOC計測+incスクリプト完了 / ビルド計測・スモーク基準はオーケストレーター/ユーザー実施待ち |
| 1 | デッドコード除去 | ビルド検証済み（スモーク S8/S10 待ち） | 2026-06-10 | PrePollRawInput / RawInputWinFilter::Poll / 同resetAllKeys・resetMouseButtonsラッパー削除、CMakeLists死参照12行削除、調査md6本を notes/ へ移動+Zoom値表修正、RomDetectコメント修正、HudRender末尾空行除去。スキップ項目なし（InputState::resetAllKeys/resetMouseButtons は方針どおり維持・現在呼び出しゼロと判明）。フル configure ビルド成功（23/23、エラー/警告なし） |
| 2 | パッチサブシステム統一 | 完了 | 2026-06-11 | 2a: `MelonPrimePatchCommon.h` 新設。StaticWordPatch 化: ShowHeadshotOnline / ShowEnemyHpMeterOnline / DisableDoubleDamageMultiplier / InstantAimFollow。未変換: NoPickingUpSpecificItems（マスク選択・部分適用・legacySkipVal 遷移あり）/ FixWifi（canary式・状態なし・restoreなし・base+offset式）。2b: `MelonPrimePatchRegistry.h/.cpp` 新設（11モジュールを site マスク + restore フラグで駆動）。MelonPrime.cpp の Apply×16 / Restore×16 / Reset×33 をレジストリ4API呼びに置換（ARM9Hook / CustomHud / per-frame OsdColor は手動のまま）。挙動差はゲーム離脱時の OsdColor 復元順（最後→6件中先頭。書込アドレス互いに素で安全）と同呼び出しの `#ifdef MELONPRIME_DS` ガード内移動のみ。patch-system.md 更新済み。ビルド+動作確認済み（2026-06-11、ユーザー実施）。レビューで FixWifi の `PatchWord` ODR 違反（共通ヘッダと同名・異フィールドの namespace スコープ定義、-Wodr 検出）を無名名前空間化で修正。2a 詳細: [phase-2a 実装結果](melonprime-full-refactor-plan-phase-2a-implementation-results.md) |
| 3 | テーブル単一情報源化 | 完了（ビルド検証済み） | 2026-06-11 | 3b: `WeaponData::enum ID` を削除し `WeaponId::*`（Def.h）に一本化。`None=0xFF` を `WeaponId` に追加（additive）。`ORDERED_WEAPONS` の mask リテラルを `WeaponMask::*` 化（値は HEAD と一致、minAmmo・順序・コメント不変）。参照は `using namespace WeaponId;`（WeaponData 内）で解決。`WeaponData::` 外部参照は `kOmegaCannonOwnedBitMask` のみで影響なし。3c: `RomVersions::` 17定数（RomDetect.cpp のみで使用）を `CHECKSUM_TABLE` に16進直書きで畳み込み、Def.h の `namespace RomVersions` 削除（label↔値の17行全一致確認済み、行順・コメント保持）。3a: `MelonPrimeGameRomAddrTable.h` を X-macro 単一定義化（行形式 `X(KIND, fieldName, ListName, 7値)`、KIND=ADDR/DATA）。全81リスト（CORE 35 / HUD 14 / DS scale 3 / DS battle 3 / DS OSD 18 list-only / AIM 8）を機械変換（Python で旧ヘッダから抽出・組立、手打ちなし、HEAD と全値一致をスクリプト確認）。`LIST_*`・`RomAddresses`・`CreateRomAddress` を同一名・同一順・同一 #ifdef 配置で全自動生成。ADDR 行は main RAM 範囲 static_assert（恒久）、`romGroupIndex==i` も恒久 assert。`Phase3aVerify` 名前空間の一時検証ブロック（`OLD_LIST_*` 原本コピー + 新旧全値一致 static_assert）はビルド成功後に除去すること（マーカー「Phase 3a temporary verification block」〜「end Phase 3a temporary verification block」）。消費側（OsdColor.cpp の LIST_ 直接参照含む）変更ゼロ。g++ -fsyntax-only -Wall -Wextra -Wpedantic で4フラグ構成すべて警告ゼロ確認済み |
| 4 | ライフサイクル整理 | 完了（ビルド検証済み、スモーク S7/S8/S2 推奨） | 2026-06-11 | 4-1: `ResetTransientInputState(uint8_t parts)` + `TransientReset` ビットマスク enum を `MelonPrime.h` に新設（メソッド/enum のみ・データメンバ順不変）。6サイトの transient-input クラスタをヘルパー呼びに置換し各サイトのサブセットを完全維持。サイト別フラグ: OnEmuStart=`AimResiduals\|OverlayHeld\|DirectTransform`（**BipedFire 無し**=Phase8候補。weaponSwitchPending は ARM9Hook_Uninstall 前で既存維持）/ ResetRuntimeStateForBoot=`+BipedFire` / OnEmuStop=`DirectTransform\|BipedFire`（**AimResiduals/OverlayHeld 無し**=Phase8候補）/ RunFrameHook focus-loss=`DirectTransform\|BipedFire`（m_input.* zero と resetAll は verbatim）/ game-leave=`OverlayHeld\|DirectTransform\|BipedFire` / HandleGameJoinInit=`OverlayHeld\|DirectTransform\|BipedFire\|WeaponSwitchPending`（順序制約無しのため weaponSwitch を fold）。`m_aimBlockBits`/`m_input.*`/`resetAll()`/`m_damageNotifyPurpleState={}`/flag 操作は verbatim 据置。4-2: 9フック宣言にドメイン別バナーコメント（Aim/Input-overlay/Fire&zoom/Transform&weapon。並び・シグネチャ不変）。4-3: FilterDeleter に refcounted-singleton release 意図コメント（コード不変）。4-4: 移行ロジックは層ごと1箇所（UI load/UI save/runtime patch）で層跨ぎ移動は評価タイミングを変えるため不可 → 3箇所に `// Legacy key migration` バナー付与。`Metroid.Aim.Enable.NativeDeltaHook` はライブ read 無し（Config.cpp デフォルト + CfgKey 定数のみ）で移行ロジック不在 → ラベル対象なし・未削除で温存。NO build / NO commit。 |
| 5 | UI/設定データ駆動化 | 完了（ビルド検証済み・config監査PASS、スモーク S10/S6 待ち） | 2026-06-11 | 5a: `MelonPrimeLocalization.h`（1212行）から翻訳テーブル本体（`kTranslations[]`/`kObjectTextTranslations[]`、`Translation`/`ObjectTextTranslation` 構造体定義込み）と全関数定義を新規 `MelonPrimeLocalization.cpp`（1170行）へ移動。ヘッダは69行に縮小。CMakeLists.txt の `SOURCES_QT_SDL` に `MelonPrimeLocalization.cpp` を追加。消費側4ファイル無変更。5b: 非HUD設定の load（`setupSensitivityAndToggles`）/ save（`saveConfig`）を単一バインディングテーブル `m_settingBindings`（`SettingBinding{key, SettingKind, QWidget*}`、43エントリ）で駆動。`SettingKind` = CheckBool/ComboIndexInt/SpinInt/DoubleSpinDouble。`buildSettingBindings`（setupUi後に runtime 構築、コード順）/ `loadBindingsRange(begin,end)`（順序保存のためセグメント適用 [0,20)[20,21)[21,24)[24,29)[29,31)[31,35)[35,43) 連続・隙間なし）/ `saveBindings`（全件、キー互いに素で順序無関係）。変換43キー、保存キー集合 HEAD と byte-exact 一致（71キー、欠落/重複/二重書込なし、ツール検証済み）。テーブル外に温存: dev-only（NativeHookMode/ImmediateInputEdgeOverlay/NativeAimPostFold/BipedFire/Zoom2）、migration（InstantAimFollow↔LowLatencyAimMode）、transform（WeaponSwitch/BipedFire/Zoom の clamp+enum）、invalidate-couple（Clip/TopScreen の old!=new）、非鏡写し save（DirectAltFormTransform）、LowLatencyAimMode combo（動的生成・currentData）。MelonPrimeDef.h CfgKey に 22定数追加（値はリテラルと byte-identical）。dev-only パターン・parent/child enable wiring・Qt auto-slot は無変更。5c: 監査スクリプトを `tools/ci/audits/audit-config-defaults.ps1` として新規チェックイン。実行結果 PASS（8セクション空、終了コード0）。NO build / NO commit。 |
| 6 | HUD断片整合 | 完了（ビルド検証済み、S9手動スモーク待ち） | 2026-06-11 | `MelonPrimeHudConfigOnScreen.cpp` を `MelonPrimeHudConfigOnScreenUnity.inc` に改名し、`MelonPrimeHudRender.cpp` の `#include "*.cpp"` を解消。`check-inc-ownership.ps1` を Phase 6 仕様に強化（unity `.inc` 1親検査、`MelonPrimeArm9InstructionHook.inc` の固定親セット検査、included `.cpp` 禁止、CMakeListed `.inc` 禁止）。`MelonPrimeGameInput.cpp` に所有フック `.inc` 一覧と unity 親コメントを追加。build.md / repo-architecture.md / melonprime-refactoring.md / 関連 skill メモ更新。 |
| 7 | 統合点集約（任意） | 完了（ビルド検証済み、S2/フレームペーシング体感待ち） | 2026-06-11 | `MelonPrimeEmuThread*.inc` を追加し、EmuThread.cpp の自己完結した MelonPrime 統合点（include / constructor / run setup / frame state declarations / message queue atomics / renderer VSync preservation）を unity 断片化。P-11/12/27/40/45 のフレームリミッタ本体、RunFrameHook 周辺、内外 hotkey 分岐、DSi/auto-screen-layout bypass は可読性・ホットパス維持のためインライン据置。CMakeLists の非Windows raw-input 除外リストを `MELONPRIME_WINDOWS_ONLY_SOURCES` へ変数化。フル configure ビルド成功。 |
| 8 | ドキュメント整備 | 完了（ビルド検証済み、ドキュメント整合パス完了） | 2026-06-11 | `melonprime-refactoring.md` に Structural Refactor 2026-06 の結果サマリを追記。`melonprime-patch-system.md` は Phase 5 後の `CfgKey` + binding-table 手順と `MelonPrimeLocalization.cpp` 所有に更新。`repo-architecture.md` に構造リファクタ後の所有点を追加。`melonprime-gameplay-runtime.md` は registry 後の RunFrameHook / AspectRatio 説明に更新。CLAUDE.md / rules README に notes index を追加。最終メトリクス記録: `qt_sdl/MelonPrime*` all 20,828 lines（Phase0比 -784）、InputConfig no-ui 4,022 lines（+26）、UI 1,239 lines（±0）、合算 26,089 lines（-758）、`melonDS.exe` 48,606,208 bytes（-3,072）。`build-mingw.bat --tail 80` は up-to-date tree で 4.7秒・成功。 |

### Phase 0 計測値（実施時に記入）
- ビルド時間: フル configure + 増分23タスクで約7分（AI実行、--parallel 1。クリーンフルビルドは未計測）
- melonDS.exe サイズ: 48,609,280 bytes（2026-06-10、Phase 1 適用後、developer features ON）
- LOC（wc -l、2026-06-10、Phase 1 編集前）: qt_sdl/MelonPrime* = 21,612（うち調査 .md 6本 = 1,108、コードのみ = 20,504）/ InputConfig/MelonPrime*（.ui 除く）= 3,996 / MelonPrimeInputConfig.ui = 1,239
- スモーク結果: —（オーケストレーター/ユーザーが記録）

### Phase 8 最終スナップショット（2026-06-11）
- ビルド時間: `.\tools\build\windows\build-mingw.bat --tail 80` が 4.7 秒で成功（configure + vcpkg確認 + no-op Ninja。クリーンビルド比較ではない）
- melonDS.exe サイズ: 48,606,208 bytes（`build/release-mingw-x86_64/melonDS.exe`、developer features ON、Phase 0比 -3,072 bytes）
- LOC: qt_sdl/MelonPrime* all = 20,828（Phase0比 -784）/ qt_sdl/MelonPrime* code = 20,828（+324）/ InputConfig/MelonPrime*（.ui 除く）= 4,022（+26）/ MelonPrimeInputConfig.ui = 1,239（±0）/ 合算 = 26,089（-758）
- スモーク結果: Phaseごとの手動スモーク未完了項目は進捗表に残存。Phase 8 はドキュメントのみ変更。

---

## 7. 推奨着手順序

```
0 → 1 → 2 (本丸) → 3 → 4 → 5 → 6 → (7) → 8
```

- 0/1 は半日ずつで即効性があり、以後の diff ノイズを減らす
- **2 が最優先の価値**（パッチ追加は今後も続くため、早いほど複利が効く）
- 3 は 2 と独立しているが、2 の後の方が PatchCtx に RomAddresses が乗っていて自然
- 5 は他と完全独立。疲れたときの「箸休めフェーズ」として差し込み可
- 7 は upstream マージ計画がない限り省略してよい

総見積り: **必須フェーズ（0–6, 8）で約8–12日相当**。各フェーズは独立コミット列なので中断・再開自由。
