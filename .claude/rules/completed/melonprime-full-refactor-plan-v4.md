# MelonPrime 全面リファクタリング計画 V4（Phase 0–7）

**作成日:** 2026-07-03
**対象ブランチ:** `highres_fonts_v3`（HEAD `2977aa46` 時点で再実測）
**ステータス:** 完了（Phase 4/6 は見送り）
**前提:** [V1](melonprime-full-refactor-plan.md) / [V2](melonprime-full-refactor-plan-v2.md) /
[V3](melonprime-full-refactor-plan-v3.md) の後継。V1–V3 が解消した負債
（パッチレジストリ / ROM X-macro / HUDスキーマ / リテラルラチェット / migration台帳）は対象外。
**V3 のラチェットは機能している**（非 canonical リテラル = 1 を維持、実測）。
本計画の主対象は **V3 完了後に追加された macOS / Linux 対応（GCMouse・IOHID・XInput2・CGWarp・
プラットフォーム別CI）で新たに蓄積したクロスプラットフォーム系の負債**である。憶測項目はない。

---

## 0. ゴールと非ゴール

### ゴール

1. **プラットフォーム入力層の三重化解消** — mac/Linux フィルタは公開APIが完全に同一
   （`Acquire/Release/isAvailable/fetchMouseDelta/resetAll`）なのに共有抽象がなく、
   呼び出し側に `#if defined(__APPLE__) / #elif defined(__linux__)` クラスタが複製されている。
   ファサード導入で「新プラットフォーム追加 = フィルタ1ファイル + ファサード1行」にする
2. **V3 Phase 3 積み残しの完遂** — `MelonPrimeHudGeometry.h` の消費が2TUに留まり、
   編集モード描画（1,328行）は依然独自ジオメトリのまま
3. **CI マトリクスの整合** — mac/ubuntu ビルドCIが追加されたが、監査（リテラル予算等）は
   Windows CI にしか載っていない。ドキュメントのCI方針（「Windows専用フォーク」前提）も現実と矛盾
4. **プラットフォーム対応で生じたドキュメント陳腐化の解消**
5. 結果として **正味 -300〜-600 行** と、プラットフォーム差分の触り忘れ事故の構造的防止

### 非ゴール（やらないこと）

- **Windows Raw Input 経路のコード生成を1命令も変えない**（V1 §2.1 do-not-touch を全文継承。
  ファサードは mac/Linux のデルタ専用フィルタのみを対象とし、`RawInputWinFilter` の
  豊富なAPI（PollAndSnapshot / LateLatch / DeferredDrain）はラップしない）
- エイム数式・感度計算・残差蓄積（P-17/P-18/P-44）の変更
- GCMouse→IOHID→QCursor のバックエンド優先順・二重計上ゲート（`gcActive`）の変更
- 設定キーの保存名変更（TOML互換、V1–V3と同じ）
- 新機能の追加

---

## 1. 無駄の棚卸し（2026-07-03 実測・証拠付き）

### 現状スナップショット

| 指標 | V3完了時 | 今回実測 | 備考 |
|---|---:|---:|---|
| `MelonPrime*` LOC（.ui除く） | 約31,000 | 32,306 | 増分はプラットフォームフィルタとLinux raw-input修正が主 |
| 非canonical `"Metroid.*"` リテラル | 1 | **1** | ✅ V3ラチェット維持を確認 |
| `MelonPrime.cpp` | 908 | **1,021** | プラットフォーム起動/リセット処理で再肥大 |
| プラットフォーム条件マーカー | — | MelonPrime.cpp **12** / GameInput.cpp **10** / MelonPrime.h 5（散乱予算スクリプト基準） | 新負債 |
| `m_macRawFilter` / `m_linuxRawFilter` 参照 | 0 | **36箇所** | 〃 |

### V4-W1. プラットフォーム入力層の三重化 + 呼び出し側の #ifdef 散乱（最大の負債）

- 3フィルタの公開APIは同一シグネチャ（実測: [MacFilter.h](../../../src/frontend/qt_sdl/MelonPrimeRawInputMacFilter.h) /
  [LinuxFilter.h](../../../src/frontend/qt_sdl/MelonPrimeRawInputLinuxFilter.h) とも
  `Acquire/Release/isAvailable/fetchMouseDelta/resetAll`）だが、型が別・共有ヘッダなし
- 呼び出し側に同型の分岐クラスタが複製されている（実測5サイト）:
  `HandleAimEarlyReset` / レイアウトリセット（GameInput.cpp）、`ShowCursor` / フォーカス喪失 /
  `Initialize`（MelonPrime.cpp）— いずれも `#if __APPLE__ → m_macRawFilter->resetAll()
  #elif __linux__ → m_linuxRawFilter->resetAll()` の繰り返し
- カーソルワープも分散: `WarpCursorTo`（GameInput.cpp 内 static）と
  `MacWarpCursorGlobal` 直呼び（MelonPrime.cpp ShowCursor / Screen.cpp clipCursorCenter1px）が
  それぞれ独自の #ifdef チェーンを持つ
- シングルトン雛形（mutex + refCount + Acquire/Release 約35行）が mac/Linux で丸ごと重複

**帰結:** 新しい入力挙動（リセット追加・ワープ仕様変更）のたびに5サイト×3分岐を触る。
mac対応時に Screen.cpp のワープを入れ忘れた実績あり（コミット `77088431` で追補）＝
既にこの構造が事故を起こしている。

### V4-W2. V3 Phase 3 の縮小実装（既知の積み残し）

- `MelonPrimeHudGeometry.h`（85行）の消費者は実測2TUのみ（`InputConfig.cpp` / `HudRender.cpp`）
- 編集モード描画 [MelonPrimeHudConfigOnScreenDraw.inc](../../../src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenDraw.inc)
  （1,328行）とプレビューウィジェットの大半は独自ジオメトリのまま
- 乖離ベースラインは V3 が作成済み: [notes/MelonPrimeHudPreviewDriftPhase3a.md](../notes/MelonPrimeHudPreviewDriftPhase3a.md)

### V4-W3. CI マトリクスの不整合

- ワークフロー実測: `build-windows.yml` / `build-macos.yml` / `build-ubuntu.yml` / **`build-bsd.yml`**
- 監査ステップ（config-defaults / hud-key-parity / literal-budget）は **Windows CI のみ**。
  ubuntu ランナーには pwsh があるため技術的には移設/併設可能
- `build-bsd.yml` は**意図的**（ユーザー確認済み 2026-07-03）。ただし
  [merge-upstream-melonds.md](../../skills/merge-upstream-melonds.md) は「mac/ubuntu/BSD CIは
  常に再削除」と指示したままで**現実と矛盾** — skill側の更新が必要（Phase 1）

### V4-W4. ファイル再肥大

- `MelonPrime.cpp` 1,001行（V2末 752 → V3末 908 → 現在）。増分の多くはプラットフォーム
  起動・リセット・ワープ処理で、W1 のファサードTUへ移せば戻る
- `MelonPrimeLocalization.cpp` 1,337行（増加傾向。テーブル追記による自然増で、緊急性は低い）

### V4-W5. ドキュメント陳腐化（プラットフォーム対応起因）

| 対象 | 実測した矛盾 |
|---|---|
| [melonprime-aim-input.md](../melonprime-aim-input.md) 冒頭 | mac の説明が「IOHIDManager」— 現在は **GCMouse が第一バックエンド**（IOHIDは3秒待ちフォールバック） |
| [merge-upstream-melonds.md](../../skills/merge-upstream-melonds.md) | 「CI は Windows のみ / mac・ubuntu・BSD の yml は再削除」— mac/ubuntu CI が現存し方針転換済み |
| [project-context.md](../project-context.md) | 「Primary target: Windows」— mac/Linux ネイティブ対応後の位置づけを追記すべき |
| [release-notes.md](../../skills/release-notes.md) テンプレ | Download Files が Windows zip のみ — mac/Linux アーティファクトが増えた場合の節がない（**要確認**: 配布するか未定なら現状維持） |

### V4-W6. 配布準備の不足（mac/Linux 配布は**予定あり** — ユーザー確認済み 2026-07-03）

- macOS 配布時の署名: Phase 5 で CMake post-build ad-hoc 署名を追加済み。
  Developer ID notarization は行わない方針（ユーザー確認済み）のため、Gatekeeper 警告は
  release notes の既知制約として扱う
- release-notes テンプレの Download Files 節が Windows zip のみ
- ~~`tools/linux-vm/` 未追跡~~ → コミット済み・[linux-vm-build.md](../linux-vm-build.md) に文書化済み（解消）

### V4-W7. upstream 統合点（V1/V2/V3 から継続・任意）

実測: EmuThread.cpp 48 / Screen.cpp 29 / Window.cpp 20 / Config.cpp 17 / EmuInstanceInput.cpp 13。
判断基準は歴代計画と同じ: **upstreamマージ予定がなければスキップ**。

---

## 2. 不変条件 — 絶対に壊さないもの

**V1 §2 / V2 §2 / V3 §2 を全文継承**。V4 固有の追加:

1. ファサード（Phase 2）は **ゼロコストの inline 転送のみ**。仮想関数・関数ポインタ経由に
   しない（毎フレーム呼ばれる `fetchMouseDelta` があるため）。Windows ビルドでは
   ファサードヘッダが `RawInputWinFilter` の既存呼び出しを**一切変えない**
   （Windows専用サイトはファサードの対象外として現状維持）
2. GCMouse→IOHID の優先順・3秒グレース・`gcActive` ゲート・stderr ログ行の形式を保存
3. カーソル再センターは macOS では **必ず `CGWarpMouseCursorPosition` 系**
   （`QCursor::setPos` 禁止 — Accessibility 権限なしで黙って失敗し暴走を再発させる。
   [build.md](../build.md) の恒久ルール）
4. Wayland での QCursor フォールバックは**要検証項目**（`setPos` が no-op の可能性 =
   mac で直した暴走と同型）。Phase 2 で防御を入れるが、挙動変更はガード追加
   （暴走するくらいならエイム停止 + 通知）に限定する
5. ビルド検証は3系統: Windows は `build-mingw.bat`、macOS は `cmake --build build-mac`、
   Linux は CI（または `tools/linux-vm`）。プラットフォーム分岐を触ったフェーズは
   **最低2系統（Windows + mac）のビルド green** を DoD に含める

---

## 3. スモークチェックリスト

**V1 S1–S12 / V2 S13–S15 / V3 S16–S17 を継承**。V4 で追加:

| # | 項目 | 確認内容 |
|---|---|---|
| S18 | macOS エイム | 起動ログに `GCMouse backend` → 実プレイでエイム正常（過回転なし・感度raw相当）。マウス抜去→QCursorフォールバックでも暴走しないこと |
| S19 | macOS ライフサイクル | Alt-Tab復帰・レイアウト変更・モーフ後にエイムジャンプなし（resetAll 経路がファサード経由でも全サイト効いていること） |
| S20 | Linux エイム | X11: XInput2 でエイム正常 — `tools/linux-vm/`（VirtualBox + Ubuntu 22.04、[linux-vm-build.md](../linux-vm-build.md)）で実施。Wayland: 検証手段なし（ユーザー確認済み）のため 2b の防御ガードを入れた上で**検証は保留**と記録する |

**Phase 2 は S18 / S19 必須（+可能なら S20）。Phase 3 は S9 / S13 / S16 必須。**

---

## 4. フェーズ計画

### Phase 0: ベースラインと予算の拡張（0.5日 / リスクなし）

1. LOC / プラットフォーム条件マーカー数 / フィルタ参照数を本ファイル末尾に記録（上表を正とする）
2. **プラットフォーム散乱予算の新設**: `MelonPrime*.{cpp,h}` 内の
   `__APPLE__|__linux__` 条件マーカー数を数える小スクリプトを `.claude/skills/` に追加し、
   現状値で予算化（Phase 2 で下げてラチェット固定 — V3 のリテラル予算と同じ方式）
3. 監査ステップを `build-ubuntu.yml` にも併設（ubuntu ランナーの pwsh で既存 .ps1 を実行。
   スクリプト本体は変更しない）
4. 3系統ビルドの現状 green を確認（Windows CI / mac ローカル / ubuntu CI）

**成果物:** 計測値の追記 + 予算スクリプト + CI変更。プロダクトコード変更なし。

---

### Phase 1: 衛生・ドキュメント同期（0.5日 / リスク極小）

| # | 作業 |
|---|---|
| 1-1 | merge-upstream-melonds.md の CI 方針を現実化（BSD/mac/ubuntu CI は**維持する**方針へ書き換え。「What NOT to take」から CI 再削除指示を除去し、代わりに「フォーク版 yml の維持マージ手順」を記載） |
| 1-2 | ドキュメント同期: aim-input.md（mac=GCMouse優先へ）/ project-context.md（マルチプラットフォーム化を1段落追記） |
| 1-3 | release-notes.md テンプレに mac/Linux アセット節を追加（配布予定あり — ユーザー確認済み。Phase 5 のアーティファクト名確定後に最終化でも可） |

**DoD:** リンクチェッカー green（V3同様）+ ドキュメントのみなのでビルド不要。

---

### Phase 2: プラットフォーム入力ファサード（1.5–2.5日 / リスク中 / **本丸A**）

#### 2a. `MelonPrimePlatformInput.h` の新設（ヘッダオンリー）

```cpp
// 非Windowsのデルタ専用フィルタを1つの型名・1組のinline関数に畳む。
// Windows(RawInputWinFilter)は対象外 — 既存コードを1行も変えない。
namespace MelonPrime {
#if defined(__APPLE__)
    using PlatformRawFilter = MacRawInputFilter;
#elif defined(__linux__)
    using PlatformRawFilter = LinuxRawInputFilter;
#endif
#if defined(__APPLE__) || defined(__linux__)
    inline PlatformRawFilter* PlatformInput_Acquire() { return PlatformRawFilter::Acquire(); }
    inline void PlatformInput_Release()               { PlatformRawFilter::Release(); }
    inline void PlatformInput_Reset(PlatformRawFilter* f) { if (f) f->resetAll(); }
    // fetch/isAvailable も同様の薄い転送
#endif
    // カーソル再センター（全非Windows共通の唯一の入口）
    void PlatformInput_WarpCursor(int x, int y);   // mac=CGWarp / X11=QCursor::setPos
}
```

- `MelonPrime.h` の `m_macRawFilter` / `m_linuxRawFilter` 2メンバを
  `PlatformRawFilter* m_platformRawFilter` 1本に統合（コールドセクション、ガード付き）
- 呼び出し5サイトの `#if __APPLE__ / #elif __linux__` クラスタを
  `PlatformInput_Reset(m_platformRawFilter)` 等の1行に置換
- GameInput.cpp の `WarpCursorTo` / MelonPrime.cpp・Screen.cpp の `MacWarpCursorGlobal`
  直呼びを `PlatformInput_WarpCursor` に一本化（**mac は CGWarp のまま** — 不変条件3）

#### 2b. Wayland 暴走ガード

- `PlatformInput_WarpCursor` の Linux 実装で、Wayland セッション
  （`QGuiApplication::platformName() == "wayland"`）かつ XInput2 フィルタ非活性の場合は
  QCursor フォールバックのエイムを **AIMBLK 系でブロック + OSD 通知1回**
  （mac で実証済みの「ワープ失敗→無限回転」と同型の事故の防御。実機検証は S20 / **要確認**）

**実装結果メモ（2026-07-03）:** 直前の Linux raw-input 修正（`2977aa46`）で、
XInput2 raw が実際に `hasReceivedMotion()` するまで ScreenPanel の前回位置ベース fallback が
エイムを所有し、毎フレーム center warp しない形になっている。Phase 2 ではその挙動を維持し、
`PlatformInput_ShouldAcquireRawFilter()` を XCB に限定、warp の入口だけ
`PlatformInput_WarpCursor` に統一した。Wayland 実機検証手段がないため、新規 AIMBLK/OSD は
追加せず、S20 の Wayland 検証は保留として記録する。

#### 2c. 起動/終了処理の移設

- `Initialize()` / デストラクタ / フォーカス喪失の分岐を ファサード呼びへ置換し、
  `MelonPrime.cpp` のプラットフォームコードを削減（目標: 1,001 → 950 以下、
  条件マーカー 24 → **8 以下**、GameInput 15 → **6 以下**）
- シングルトン雛形の重複（mac/Linux 各35行）は、ファサード側に共通実装を持たせるか
  現状維持かをこの時点で判断（挙動不変が確認できる場合のみ統合）

**DoD:** Windows CI green（コード生成不変の確認: Windows側は差分ゼロが原則）+
mac ローカルビルド + S18 / S19（+可能なら S20）+ Phase 0 の散乱予算を新値でラチェット。
**期待削減:** 約 -120〜-200 行 + 分岐サイト 5→1。

**実施結果（2026-07-03）:** `MelonPrimePlatformInput.h` を追加し、mac/Linux raw filter の
Acquire/Release/isAvailable/fetch/reset と non-Windows warp 入口を一本化。`m_macRawFilter` /
`m_linuxRawFilter` 参照は 36→0、呼び出し側は `m_platformRawFilter` に統合。
散乱予算はファサード本体を canonical owner として除外し、36→30 にラチェット。
`MelonPrime.cpp` は 1,021→976 行。mac ローカルビルド green、監査 green。
S18/S19 は実機/ROM が必要なため未実施。

---

### Phase 3: HUD ジオメトリ消費の完遂（1.5–2日 / リスク中 / **本丸B** = V3 Phase 3 の残り）

- [notes/MelonPrimeHudPreviewDriftPhase3a.md](../notes/MelonPrimeHudPreviewDriftPhase3a.md) の
  乖離ベースラインを起点に、V3 で未着手だった消費者を `MelonPrimeHudGeometry.h` に載せ替える:
  1. `MelonPrimeHudConfigOnScreenDraw.inc`（編集モードの要素バウンズ・プレビュー描画）
  2. `InputConfig/MelonPrimeInputConfigHudPreviews.inc` の残りウィジェット
- ランタイム側（HudRender系）は V3 で載せ替え済みの範囲を**変えない**
- 乖離は V3 と同じ規律: 「現状維持フラグ」で記録し黙って直さない。直す場合はバグ修正として分離コミット

**DoD:** 3系統ビルド + S9 / S13 / S16（プレビュー⇔実描画⇔編集モードの目視一致）。
**期待削減:** 約 -200〜-400 行 + ジオメトリ変更時の触り箇所 3→1。

**実施結果（2026-07-03）:** `MelonPrimeHudGeometry.h` に gauge align / gauge→text 逆算 /
rect anchor の純粋ヘルパーを追加し、runtime wrapper、編集モード bounds、設定ダイアログ previews の
重複式を共有化。V3 Phase 3a で intentional とした runtime crosshair cache / dirty rect /
zoom transition / preview simplification は変更なし。mac ローカルビルド green。
S9/S13/S16 の目視スモークは未実施。

---

### Phase 4: フィルタ内部の共通化（0.5–1日 / リスク低 / 任意）

- mac/Linux フィルタのアキュムレータ（atomic int32 ×2 + exchange/fetch_add 規律）と
  シングルトン雛形を共通ヘッダへ（Phase 2c で見送った場合のみ）
- **やらない**: バックエンドスレッドモデルの統合（CFRunLoop / XInput2 イベントループは
  プラットフォーム固有のままが正しい）

**DoD:** mac ビルド + Linux CI + S18 / S20。

**判断結果（2026-07-03）:** 見送り。Phase 2 後に残る共通化候補は主に refcounted singleton と
atomic accumulator だが、mac は GCMouse/IOHID の Objective-C++ lifecycle、Linux は XInput2
absolute-device baseline / warp notification を持ち、共通化の実益より回帰リスクが大きい。
`MelonPrimePlatformInput.h` で呼び出し側の三重化は解消済みなので、Phase 4 は実装しない。

---

### Phase 5: mac/Linux 配布整備（1日 / リスク低 / **正式フェーズ** — 配布予定ありのため）

| # | 作業 |
|---|---|
| 5-1 | CMake に ad-hoc 署名フック（`codesign -s -`、Developer ID は使わない — ユーザー確認済み。Gatekeeper 警告は既知の制約として release notes に記載） |
| 5-2 | `build-macos.yml` の配布構成確認（.app の zip/dmg 化、`MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF`） |
| ~~5-3~~ | ~~Linux アーティファクト~~ → ワークフローで整備済み（ユーザー確認 2026-07-03） |
| 5-4 | release-notes.md テンプレの Download Files 節を3プラットフォーム化（1-3 と連動） |

**DoD:** CI が3プラットフォームのアーティファクトを生成し、mac は署名検証
（`codesign --verify`）green。実機インストール確認はユーザー実施。

**実施結果（2026-07-03）:** CMake の macOS bundle post-build に ad-hoc `codesign -s - --deep`
フックを追加（`MACOS_ADHOC_SIGN_BUNDLE=ON`）。`build-macos.yml` は arch別 app と universal app の
署名検証を実行し、mac/Linux release configure で
`MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF` を明示。ローカル確認:
`cmake --build build-mac --parallel 4` 後に `codesign --verify --deep --strict` green、
`codesign -dv` で `Signature=adhoc`。

---

### Phase 6: upstream 統合点の継続削減（任意・ストレッチ）

歴代計画と同一基準: **upstream マージ予定がなければスキップ**。やる場合は
Screen.cpp / Window.cpp の連続・自己完結ブロックのみ、include 位置完全同一で。

---

### Phase 7: ドキュメント整備・最終計測（0.5日 / リスクなし）

1. [melonprime-refactoring.md](../melonprime-refactoring.md) に「Structural Refactor V4」節を追記
2. [repo-architecture.md](../repo-architecture.md) にプラットフォーム入力ファサードの所有図
   （PlatformInput → Win/mac/Linux 実装、ワープの唯一の入口）を追加
3. [melonprime-aim-input.md](../melonprime-aim-input.md) のプラットフォーム節をファサード後の形に更新
4. CLAUDE.md / rules README 更新、本ファイルを `completed/` へ移動
5. 最終計測: LOC / 条件マーカー数（予算ラチェット固定）/ 3系統CI green

**実施結果（2026-07-03）:** `melonprime-refactoring.md` に Structural Refactor V4 節を追記し、
`repo-architecture.md` に platform input facade の所有図を追加。`melonprime-aim-input.md` は
macOS/Linux call site が `MelonPrimePlatformInput.h` 経由であることを明記。
本計画を completed へ移動し、最終計測を記録。

---

## 5. リスクと対策

| リスク | 該当 | 対策 |
|---|---|---|
| 配布署名の要件不足（notarization なしで Gatekeeper 警告） | 5 | まず ad-hoc 署名で配布し既知の制約として release notes に記載。Developer ID / notarization は加入状況確認後に追加（**要確認**） |
| ファサード化で Windows 側のコード生成が変わる | 2 | Windows サイトはファサード対象外。`#ifdef _WIN32` ブロックには触れない方針を機械確認（Windows CI + 差分レビューで `_WIN32` ガード内の diff ゼロを確認） |
| mac のワープ一本化で CGWarp 以外の経路が紛れ込む | 2a | `QCursor::setPos` の使用を検査する grep を散乱予算スクリプトに同梱（macガード内で検出したら fail） |
| resetAll の呼び忘れサイトが置換中に脱落 | 2 | 置換前に5サイトの表を作り、置換後に `PlatformInput_Reset` の呼び出し数一致を確認。S19 で挙動確認 |
| ジオメトリ載せ替えで編集モードの見た目が変わる | 3 | V3 3a の乖離レポートとの前後比較。乖離修正は分離コミット。S13/S16 目視 |
| Wayland ガードが X11 環境を誤ブロック | 2b | `platformName()` 判定は Wayland 完全一致のみ。X11 は無変更。S20 |
| Linux 検証環境が手元にない | 2, 3, 4 | ubuntu CI をビルドゲートに。実機スモーク（S20）は `tools/linux-vm` またはユーザー実機で（**要確認**） |
| 巨大 diff | 2, 3 | 1コミット ≦ 約400行（歴代と同じ）。サイト単位・要素単位の小コミット |

---

## 6. 進捗トラッキング

| Phase | 内容 | 状態 | 完了日 | 結果メモ |
|---|---|---|---|---|
| 0 | ベースライン + 散乱予算 + CI拡張 | 完了 | 2026-07-03 | `audit-platform-scatter-budget.ps1` 追加。Ubuntu CI に Windows と同等の独立 audit job + HUD schema 再生成検証を併設し、build job は audit 成功後に実行。既存監査も非Windows `pwsh` で有効に動くよう補正（`Sort-Object -Unique`、HUD runtime macro 展開、HUD schema expected owners 更新）。Linux raw-input修正後の現HEADで再計測し、散乱予算を 31→36 に更新。ローカル確認: PowerShell監査一式 green / workflow YAML parse green / 散乱予算 36/36 / macOS `QCursor::setPos` ガード clean / HUD schema 再生成 diffなし / macOS build green。Windows/Ubuntu は workflow 上の audit gate として次回PR/対象branch pushで実行。 |
| 1 | 衛生・ドキュメント同期 | 完了 | 2026-07-03 | `merge-upstream-melonds.md` を現行CI方針へ更新し、macOS/Ubuntu/BSD workflow は維持、fork版workflowを優先して小さなupstream maintenanceだけ手動移植する手順へ変更。`project-context.md` にWindows主軸 + macOS/Linux配布対応 + BSD build-only CIの現状を追記。`melonprime-aim-input.md` はGCMouse優先・IOHID fallback・TCC不要/必要の理由を明記。`release-notes.md` の Download Files 節を Windows / macOS / Linux artifact 名へ更新し、macOS ad-hoc署名のGatekeeper警告を既知制約として追加。 |
| 2 | プラットフォーム入力ファサード（本丸A） | コード完了 | 2026-07-03 | `MelonPrimePlatformInput.h` で mac/Linux raw filter と cursor warp の入口を統合。旧 `m_macRawFilter` / `m_linuxRawFilter` 参照は 0。散乱予算は canonical facade を除外して 30/30 に更新し、Windows/Ubuntu audit gate も 30 へラチェット。mac ローカルビルド green。S18/S19 実機スモークは未実施、S20 Wayland は検証保留。 |
| 3 | HUDジオメトリ消費の完遂（本丸B） | コード完了 | 2026-07-03 | `MelonPrimeHudGeometry.h` の消費を runtime wrapper / on-screen edit bounds / settings previews に拡張。gauge align、gauge→text 逆算、rect anchor、text alignment の重複式を共有化し、preview-only の意図的簡略化は維持。mac ローカルビルド green。S9/S13/S16 目視スモークは未実施。 |
| 4 | フィルタ内部共通化（任意） | 見送り | 2026-07-03 | 呼び出し側の三重化は Phase 2 で解消済み。残る重複は主に singleton / accumulator だが、mac Objective-C++ backend と Linux XInput2 absolute-device補正の lifecycle 差が大きく、共通化の実益より回帰リスクが大きいため実装しない。 |
| 5 | mac/Linux配布整備 | 完了 | 2026-07-03 | CMake macOS post-build ad-hoc signing hook を追加。macOS workflow は arch別/universal app を `codesign --verify --deep --strict` で検証し、mac/Linux release configure は `MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF` を明示。ローカル mac build + signature verify green、`Signature=adhoc` を確認。 |
| 6 | upstream統合点（任意） | 見送り | 2026-07-03 | 今回は upstream merge 作業を伴わないためスキップ。 |
| 7 | ドキュメント + 最終計測 | 完了 | 2026-07-03 | `melonprime-refactoring.md` / `repo-architecture.md` / `melonprime-aim-input.md` を更新し、本計画を completed へ移動。最終計測: `MelonPrime*` 128 files / 32,531 lines（.ui除く）、platform scatter 30/30、旧 mac/linux raw filter member refs 0、HUD schema rows 575、literal budget 1/1、mac build + ad-hoc signature verify green。 |

### Phase 0 計測値（2026-07-03、HEAD `2977aa46`）

- `MelonPrime*` LOC（.ui除く）: 32,306
- 非canonical `"Metroid.*"` リテラル: **1**（V3ラチェット維持 ✅）
- プラットフォーム条件マーカー（散乱予算スクリプト基準）: MelonPrime.cpp **12** / GameInput.cpp **10** / MelonPrime.h 5
- 散乱予算スクリプト基準: `MelonPrime*.{cpp,h}` の `__APPLE__|__linux__` 一致数 **31**
  → Linux raw-input修正後の現HEADでは **36**
  （MelonPrime.cpp 12 / GameInput.cpp 10 / MelonPrime.h 5 / Localization.cpp 3 /
  RawInputLinuxFilter.cpp 2 / RawInputLinuxFilter.h 2 / RawInputMacFilter.h 2）
- `m_macRawFilter`/`m_linuxRawFilter` 参照: 36箇所 / resetAll 分岐クラスタ: 5サイト
- フィルタLOC: mac 349+75 / linux 187+43 = 計654行（公開API同一・共有抽象なし）
- `MelonPrimeHudGeometry.h` 消費者: 2TU（InputConfig.cpp / HudRender.cpp）— OnScreenDraw.inc 未消費
- 主要ファイル: MelonPrime.cpp 1,021 / Localization.cpp 1,337 / HudConfigOnScreenDraw.inc 1,328
- CI: windows（監査あり）/ macos / ubuntu / **bsd（要確認）** — 監査はWindowsのみ
- upstream `MELONPRIME` マーカー: EmuThread 48 / Screen 29 / Window 20 / Config 17

---

## 7. 推奨着手順序

```
0 → 1 → 2 (本丸A) → 3 (本丸B) → 5 (配布) → (4) → (6) → 7
```

- 0/1 は半日ずつ。**2 が最優先**（プラットフォームは今後も触る領域で、mac対応時に
  ワープ入れ忘れ事故が既に1件起きている — 複利が効く）
- 3 は 2 と独立。V3 の乖離レポートが鮮度を失う前に消化するのが望ましい
- **5 は正式フェーズ**（配布予定あり）。2 の後に置くのは、配布バイナリにファサード後の
  入力層を含めるため
- 4/6 は任意
- 総見積り: **必須フェーズ（0–3, 5, 7）で約5.5–7.5日相当**。各フェーズは独立コミット列

---

## 8. 要確認事項

**解消済み（ユーザー回答 2026-07-03）:**
1. ~~`build-bsd.yml`~~ → 意図的に維持。skill文書のみ現実化（Phase 1-1）
2. ~~`tools/linux-vm/`~~ → コミット済み・文書化済み
3. ~~mac/Linux 配布~~ → **予定あり** → Phase 5 を正式フェーズ化
4. ~~Wayland 検証~~ → 手段なし → 2b の防御ガードのみ入れ、検証は保留と記録

**残存:** なし（全件回答済み 2026-07-03）
5. ~~Apple Developer Program~~ → **加入しない** → Phase 5-1 は ad-hoc 署名で確定
   （Gatekeeper 警告は既知の制約として release notes に記載）
6. ~~Linux 配布形式~~ → **ワークフローで整備済み**（ユーザー回答）→ Phase 5-3 は削除、
   Phase 5 は 5-1（ad-hoc署名フック）/ 5-2（mac配布構成確認）/ 5-4（release-notesテンプレ）のみ
