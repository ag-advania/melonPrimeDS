# MelonPrime Metal — フリッカー / 黒3D 完全修正 + 正式Metal実装 フェーズ計画

**作成日:** 2026-07-10 JST
**対象ブランチ:** `highres_fonts_v3`（HEAD `9c5b178a` 時点でコード監査済み）
**対象ファイル:** `src/GPU_Metal.mm` / `src/GPU3D_Metal.mm` / `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`
**先行文書:** [melonprime-metal-backend-plan.md](../melonprime-metal-backend-plan.md)（Phase 8/9 進行中）、
[plan/done/metal_flicker_black3d_srp_best_practices_instructions.md](done/metal_flicker_black3d_srp_best_practices_instructions.md)
（Phase A–D = frame ownership / 診断基盤は完了済み。本計画はその続き — 診断で炙り出された実バグの修正と、
残っていた §13 の 8–10（ルーティング再設計・ブロッキング待ち除去・バッファリング）を正式なフェーズに落とす）

**症状（ユーザー報告 2026-07-10）:**

```text
1. 画面が点滅する（フリッカー）
2. 3D映像がレンダリングされず、3D部分が真っ暗
```

両方ともコード監査で根本原因を特定済み。特に (2) は決定的なバグを発見した（§1-A）。

---

## 0. 監査の前提 — 何が既に正しいか

2026-07-10 の全ファイル監査で、以下は**問題なし**と確認した（再調査不要）:

- フレーム所有権: 最終合成は `MetalRenderer::VBlank()`（emu スレッド、VCount 192）で行われ、
  `GetOutput()` は完成フレームの getter のみ。front/back フリップは合成成功時に 1 回。SRP 文書 Phase A の要求どおり。
- スレッドモデル: compose（VBlank）→ presenter `drawScreen()`（RunFrame 後）は同一 emu スレッドで逐次実行。
  `CAMetalLayer` プロパティ書き込みは GUI スレッド（`updateDrawableSizeGuiThread`）に限定されている。
- レイヤ割当: `EngineAOutputLayer = ScreenSwap ? 0 : 1` は `GPU_Soft.cpp:99-108`
  （ScreenSwap=true → Engine A = Framebuffer[..][0] = top）と一致。フレーム単位である点を除き正しい。
- presenter のスクリーン変換: `mp_screen_vs` は `kScreenVS`（main_shaders.h）と同一の affine→NDC 数式。
  `kScreenVertices` は GL 側と byte 一致、`screenKind`/`screenMatrix` 消費も GL と同型。
- 3D フレームタイミング: `Start3DRendering()`（VCount 215）→ 次フレームの scanline 消費 → VBlank(192) compose、
  の順序で、compose が読む `ColorTarget` は当該フレームの 3D で正しい。
- テクスチャリング: `Texcache<>` 共有テンプレート経由のデコード、TexRepeat 3x3 サンプラ行列、
  modulate/decal 分岐 — スタンドアロンハーネスで手計算一致を検証済み（backend-plan §3j/3k/3n）。

---

## 1. 監査結果 — 根本原因（確度順）

### A. 【黒3D・確定バグ】クリアパスがデプスバッファ全面を 0.0 で上書きしている

`GPU3D_Metal.mm` の `ClearNativeTarget()`:

1. レンダーパスは `clearDepth = 1.0`（far）で正しくクリアする（:1047）。
2. **直後に** `ClearPipeline` + `ClearDepthStencil` でフルスクリーン三角形を描く。
   - クリア頂点シェーダ `mp3d_clear_vs` は `position = float4(pos, 0.0, 1.0)` — **z = 0.0**（:386）。
   - `ClearDepthStencil` は **`MTLCompareFunctionAlways` + `depthWriteEnabled = YES`**（:887-890）。
3. → フルスクリーン三角形がデプスバッファ**全面を 0.0（最近値）で上書き**する。

その後の `RenderNativeOpaquePolygons()` は `MTLCompareFunctionLess`（:963）なので、
フラグメントのデプスが **0.0 未満でなければ全部棄却** — DS のデプスは常に ≥ 0 なので**全ポリゴンが落ちる**。
ドローコールは実行される（`draws > 0`）が 1 ピクセルも書かれず、`ColorTarget` はクリア黒のまま =
**「3D部分が真っ暗」の直接原因**。SRP 文書 §B1 が予言していた
「draws > 0 かつ nonzeroPixels == 0 → native pass 内のデプス棄却」そのもの。

スタンドアロンハーネス（backend-plan §3j.1）で検出できなかった理由: ハーネスは
`LoadActionClear` のみでクリアし、この**クリア三角形を経由していなかった**。

GL 参照実装: `GPU3D_OpenGL.cpp` の `ClearShaderPlain` は `uDepth` uniform
（`RenderClearAttr2` 由来の rear-plane デプス、通常は far）を書く。z=0.0 固定が誤り。

### B. 【フリッカー】3系統の候補（B1 が最有力、B2 は併発しうる）

**B1. 最終合成のフレーム単位ルーティングヒューリスティックによる交番（コンテンツレベル）**

`GPU_Metal.mm` の `AnalyzeFinalRouting()` + compose ループ（:652-722）は、フレームごとに
`DispCnt A` のビット（dispMode==1 && bit3 && bit8）を見て上画面レイヤの**ソースを丸ごと切り替える**:

| その瞬間の DispCnt / 状態 | 上画面（Engine A layer）に出るもの |
|---|---|
| `EngineAUses3D == true` | native 3D target **のみ**（A のバグで現状は真っ黒。修正後も 2D HUD なしの opaque-only） |
| `EngineA3DBitsSet && dispMode != 1` | **マゼンタ塗り**（"unsupported route" — 通常ビルドの通常経路に診断色が居る） |
| native target 不在 / デバイス不一致 | **赤 / 青塗り**（同上） |
| それ以外 | CPU 合成フレーム（完全な絵） |

ゲームが DispCnt を切り替える度（キャプチャの double-buffer 表示 = dispMode 1↔2 交番、フェード、
メニュー遷移、BG0 enable のトグル）に、上画面が「完全な絵 ↔ 真っ黒 ↔ マゼンタ」を**フレーム単位で交番**する。
これが点滅の最有力機序。決定ログ: routing ログはシグネチャ変化時のみ出力なので、
**点滅中に `metal final route:` 行が毎フレーム連打されるならこれで確定**（Phase 0-6 の確認手順）。

なお、そもそも whole-layer 置換は原理的に正しい絵を作れない（§C）ため、この機序は「調整」ではなく
**経路ごと削除**で潰す（Phase 0-3 / Phase 2）。

**B2. Qt ↔ CAMetalLayer のレイヤ所有権競合（ウィンドウレベル）**

`MelonPrimeScreenMetal.mm` `initMetal()`:

- Qt 管理の NSView に対して `view.layer = metalLayer` で**バッキングレイヤを直接差し替え**ている（:332）。
- `QEvent::WinIdChange` ハンドラが**存在しない**（設計書 §10.2 の必須要件が未実装 —
  fullscreen 遷移 / スクリーン移動 / native handle 再生成で Qt が自前レイヤを再作成すると Metal レイヤが外れる）。
- `WA_PaintOnScreen` は Qt ドキュメント上 X11 専用で、macOS では Qt のレイヤ管理と干渉しうる。

AppKit/Qt が自前レイヤを再アタッチ・再描画すると、Metal の絵と Qt の空背景が交互に出る =
ウィンドウ全体の点滅になる。B1 修正後もまだ点滅が残る場合の本命（Phase 1 で切り分け・修正）。

**B3. ソース無しフレームで黒クリアの drawable を present している**

`drawScreen()` はソースが得られないフレーム（`GetOutput()` が `Kind=None` を返す起動直後、
fallback 拒否時など）でも `LoadActionClear`（黒）のパスを実行して present する（:523-527, :810-811）。
→ 断続的に真っ黒フレームが挿入される。present をスキップすべき。

### C. 【構造的欠陥】whole-layer 置換は原理的に正しい出力を作れない

現行アーキテクチャ: `MetalRenderer` は `SoftRenderer` を継承し、CPU の scanline 合成
（`GPU2D_Soft` が `Rend3D->GetLine()` を毎ラインで消費して 2D レイヤと融合、`GPU_Soft.cpp:95-148`）が
「完全な絵」を作る。一方 Metal の最終合成は「上画面全体を raw native 3D target で置き換える」——これは:

- 2D レイヤ（HUD・スプライト・ウィンドウ・ブレンド）を全部捨てる
- per-scanline の 3D↔2D アルファブレンド、display capture、master brightness を全部捨てる
- クリアプレーン（alpha=0 で 2D が透ける）を表現できない

melonDS 現行版（このツリー）には旧 GLCompositor 的な「2D だけの中間出力」は存在しない。
`SoftRenderer::Framebuffer` は**最終合成済み**出力。正しい統合点は次の 2 つだけ:

1. **GetLine 統合（ハイブリッド）**: `MetalRenderer3D::GetLine()` が native Metal 3D の scanline を返し、
   既存の soft 2D 合成にそのまま流す。合成の正しさ（アルファ・キャプチャ・輝度・スワップ）を
   CPU 側の実績コードが全部担保する。native 解像度限定。→ **Phase 2**
2. **GLRenderer 完全ミラー**: `GLRenderer2D`（2D の GPU 化、1,913 行）+ `RenderScreen()` 最終パス
   （per-line `uScreenSwap[192]`、dispMode 0–3、brightness、aux/FIFO、capture）の Metal 移植。
   hires の本命。→ **Phase 4**

`AnalyzeFinalRouting` / `MetalFinalState` の compose 経路は、この 2 つのどちらでもない
第 3 の発明であり、**修正ではなく削除**の対象。

### D. 【同期・性能負債】（正しさ確定後に Phase 5 で処理）

- `waitUntilCompleted` が毎フレーム最低 3 回（ClearNativeTarget / RenderNativeOpaquePolygons / compose）。
  `MELONPRIME_METAL_DIAG=1` だとさらに readback ×3。GPU/CPU 直列化でフレームペーシングを悪化させる。
- 毎フレーム `newBufferWithBytes`（vertex 1 本 + グループ数ぶんの index buffer）。
- presenter: `uiOverlay` QImage を毎フレーム fill + **全面** `replaceRegion` アップロード
  （Retina で数 MB/frame）。GL パスの OPT-DR1..DR3（dirty-rect）思想が未適用。
- 2D/3D フレームスキュー: compose は `GetFramebuffers()`（front = **前フレーム**、swap は VCount 262 の
  `FinishFrame` なので VBlank 時点では未スワップ）を使うため、2D=フレーム N-1 / 3D=フレーム N の混成。
- compose と presenter が**別々の `MTLCommandQueue`**（renderer queue / presenter queue）。
  ハザードトラッキング（default tracked）で正しさは保たれるが、設計として文書化されていない。

### E. 【細部・雑多】（各フェーズのついでに処理）

- `kScreenVertices` のコメント（:38-41）「texcoord.z ではなくバインドするテクスチャで選ぶ」は stale
  （現物は texture2d_array + texcoord.z 選択）。
- `MetalRenderer::Init()` 内 `EnsureFinalOutput()` と `EnsureFinalOutputForDevice()` の device 再構築時、
  `HasCompletedFrame` をリセットしない経路がある（device 差し替え時 :398-413 は Scale=0 にするが
  完成フラグは触らない — 旧テクスチャ nil 化後に GetOutput が nil texture を返す窓は
  `FinalOutputTex[FrontBuffer]` null チェックで防がれているので実害なしだが、一貫性のため揃える）。
- presenter の CPU fallback 経路は `m->screenTex[0]` を present 実行中に `replaceRegion` で
  上書きしうる（in-flight hazard）。fallback は dev 専用なので優先度低。

---

## 2. 不変条件（全フェーズ共通）

[melonprime-metal-backend-plan.md](../melonprime-metal-backend-plan.md) §0–1 と
[SRP 文書](done/metal_flicker_black3d_srp_best_practices_instructions.md) §2 を全文継承。特に:

1. `MELONPRIME_FORCE_DISABLE_METAL=ON` で Metal の痕跡ゼロ（ソース・シンボル・文字列・UI）。
   デフォルトビルド（`build-mac`）は毎フェーズで strings/nm 検査。
2. `High2` は OpenGL Compute のまま。Metal へ流用・リダイレクトしない。
3. `GetOutput()` は getter のみ（コマンドバッファ生成・アップロード・wait・flip 禁止）— 既達、維持。
4. 通常モードで**サイレント software fallback しない**。ただし「診断色を通常ユーザーに見せる」も禁止
   （本計画で修正）。fallback は `MELONPRIME_METAL_ALLOW_SOFTWARE_FALLBACK=1` 明示時のみ。
5. Windows/Linux ビルド不変。既存監査（literal 1 / scatter 22 / inc-ownership / srp-performance）green。
6. 各フェーズ = 独立コミット列。`git reset --hard` 禁止。1 コミット ≦ 約 400 行 diff 目安。
7. 検証は毎フェーズ: `build-mac-metal-test` + `build-mac` 両方ビルド green、
   実 ROM スモーク（`tools/macos/run_metal_test.command <rom>`）、デフォルトバイナリ文字列検査。
8. Apple Silicon は本セッション環境からは検証不可 — 公開ゲートとして Phase 6 に残す（Intel 検証のみで
   「クロスプラットフォーム検証済み」を名乗らない）。

---

## 3. フェーズ計画

```text
Phase 0 (止血: 黒3D修正 + フリッカー遮断)     0.5–1日   ← 最優先・ここだけでユーザー症状は消える
Phase 1 (レイヤ所有権の切り分けと恒久化)       0.5–1日
Phase 2 (GetLine統合 = 正しい2D/3D合成)        2–4日    ← 本丸A
Phase 3 (native 3D 機能パリティ)               1–2週    ← 本丸B・サブフェーズ分割
Phase 4 (hires: GLRendererミラー)              2–4週    ← Phase 3 完了が前提
Phase 5 (同期・性能)                           2–4日
Phase 6 (検証・公開ゲート)                     継続
```

---

### Phase 0: 止血 — 黒3Dの修正とフリッカー機序の遮断（0.5–1日 / リスク小）

ユーザー可視の 2 症状を最小差分で消す。アーキテクチャは変えない。

| # | 作業 | 対象 |
|---|---|---|
| 0-1 | **クリアパスのデプス修正（§A）**: `mp3d_clear_vs` の出力 z を rear-plane（far）にする。最小修正は 2 案 — (a) クリア三角形の z を `1.0` に変更（`[[position]]` の z=1.0 は far、CompareAlways+write で depth=1.0 が入る）、または (b) **クリア三角形の描画自体をやめ**、`LoadActionClear`（color=黒 / attr=0 / depth=1.0 / stencil=0xFF）だけにする。現状のクリア色は定数なので (b) が最も安全で速い（コマンドバッファ 1 本削減にもなる）。`RenderClearAttr1/2` からの正式な rear-plane（色/alpha/polyID/fog/デプス/クリアビットマップ）は Phase 3-1 で実装するので、ここではクリアパイプライン自体は温存してよい | `GPU3D_Metal.mm` `ClearNativeTarget()` / `kMetal3DShaderSource` |
| 0-2 | **診断色の隔離（§B1）**: マゼンタ/赤/青のクリアは `MELONPRIME_METAL_DIAG=1` のときだけ。通常モードでは該当レイヤを CPU 合成フレームにフォールバックし、レート制限付きで理由をログ | `GPU_Metal.mm` compose ループ |
| 0-3 | **ルーティング交番の構造的遮断（§B1）**: 通常モードの可視ソースを**両レイヤとも CPU 合成フレーム**（完全な絵）に固定する。native 3D を可視レイヤに載せる経路は `MELONPRIME_METAL_NATIVE_3D_VISIBLE=1`（developer 向け bring-up フラグ）に隔離。`AnalyzeFinalRouting` は診断ログ用としてのみ残す。→ DispCnt 変化による交番が起き得なくなる。**注:** これは Phase 2 までの暫定状態（3D は soft 合成で見える）。native パスは毎フレーム実行を継続し、diag readback で修正効果を計測できる状態を保つ | `GPU_Metal.mm` |
| 0-4 | **ソース無し present の禁止（§B3）**: `drawScreen()` でエミュ実行中に描くものが無いフレームは `nextDrawable` を取得せず return（no-ROM スプラッシュ・OSD のみのフレームは従来どおり描く） | `MelonPrimeScreenMetal.mm` |
| 0-5 | stale コメント修正（§E: `kScreenVertices` コメント等）。`git diff -w` 小 | 同上 |
| 0-6 | **決定ログの取得**: 修正前後で `MELONPRIME_METAL_DIAG=1` の ROM ログを保存。確認点 — (i) 修正後に `metal 3d diag: nonzero=` が **> 0** になること（0-1 の効果証明）、(ii) 点滅していた場面で `metal final route:` が毎フレーム連打されていたか（B1 確定の事後証拠）、(iii) 修正後に点滅が残るか（残るなら Phase 1 の B2 へ） | ログのみ |

**DoD:**
- 実 ROM（MPH）でフリッカー消滅（メニュー/ロード/インゲームの目視）。
- `MELONPRIME_METAL_DIAG=1` で `native3D.nonzero > 0`（ゲームプレイ中）— 黒3Dバグの修正証明。
  ※この時点で通常画面に映る 3D は soft のもの（0-3 の暫定）。native の絵は
  `MELONPRIME_METAL_NATIVE_3D_VISIBLE=1` で確認（opaque-only・HUD なしは既知の制限として扱う）。
- `build-mac-metal-test` / `build-mac` 両方 green、デフォルトバイナリ文字列検査クリア、既存監査 green。

---

### Phase 1: レイヤ所有権の切り分けと恒久化（0.5–1日 / リスク小中）

Phase 0 後もフリッカーが残る場合の本命（§B2）。残らなくても 1-2/1-3 は恒久化のため実施する。

| # | 作業 |
|---|---|
| 1-1 | **切り分け**: `MELONPRIME_METAL_DIAG_FINAL_LAYERS=1`（チェッカー固定表示）で点滅するか確認。チェッカーでも点滅する → コンテンツ無関係 = レイヤ/present 問題（B2/B3）で確定。安定 → コンテンツ経路（B1）は Phase 0 で解消済みと確定 |
| 1-2 | **`QEvent::WinIdChange` ハンドラ追加**（設計書 §10.2 の未実装要件）: native handle 再生成時に `view.wantsLayer`/`view.layer` を再適用。fullscreen 遷移・スクリーン移動・`QEvent::ScreenChangeInternal` 相当での `updateDrawableSizeGuiThread()` 再実行 |
| 1-3 | **レイヤ差し替え方式の見直し**（1-1 でレイヤ問題と確定した場合のみ）: 第 1 候補 = `view.layer` を置き換えず **sublayer 方式**（`view.wantsLayer=YES` のまま `[view.layer addSublayer:metalLayer]` + frame/`autoresizingMask` 追従 + `zPosition` 前面）。第 2 候補 = layer-hosting child NSView（設計書 §10.4 のフォールバック — 採る場合はマウス aim / cursor lock / Escape / フォーカス / タッチの全入力スモーク必須）。`WA_PaintOnScreen` は macOS で実効性がないため、挙動確認のうえ削除を検討（ScreenPanelGL との差分は要記録） |
| 1-4 | **スモーク**: resize 連打 / fullscreen 往復 / 別スクリーンへ移動 / Mission Control / 最小化復帰 / レイアウト切替（Natural/Vertical/Hybrid）で点滅・黒窓・レイヤ剥がれゼロ |

**DoD:** 上記全シナリオで点滅ゼロ。入力（aim/クリック/Escape）退行なし。

---

### Phase 2: GetLine 統合 — native Metal 3D を正しい合成で画面に出す（2–4日 / 本丸A）

**目的:** §C の結論に従い、「whole-layer 置換」を廃止して、native Metal 3D の出力を
**既存 soft 2D scanline 合成**（実績コード）に供給する。これで:

- 3D の上に 2D HUD/スプライトが正しく載る
- クリアプレーン透過・アルファ・キャプチャ・輝度・per-line swap が全部正しくなる
- DispCnt 変化はすべて soft 2D 側が吸収 → ルーティングヒューリスティック不要 = フリッカー機序が消滅
- soft 3D ラスタライズ（soft レンダラ最大の CPU コスト）が GPU にオフロードされる

**制限（明示）:** この段階の可視解像度は native 256×192（hires は Phase 4）。

| # | 作業 |
|---|---|
| 2a | **ライン形式の確定**: `SoftRenderer3D::GetLine()` が返す u32 ピクセル形式（色 6bit 系 + **alpha = bits 24+**、`(c >> 24) == 0` で透明スキップ — `GPU2D_Soft.cpp:371-383 DrawBG_3D` が消費、最終合成 `DrawScanlineA` のブレンドが参照するフラグ含む）を `GPU3D_Soft.cpp` の実装から正確に写し取り、native パスの出力仕様として文書化する。**alpha を捨てない**ことが要点（現状 color(0) は `float4(col.rgb, 1.0)` で alpha 破棄 → 形式変更が必要。RGBA8Unorm ターゲットに alpha/フラグを載せるか、attr ターゲット併用かはこの調査で決める） |
| 2b | **クリアプレーン alpha**: クリア値の alpha は `RenderClearAttr1` の alpha（0 なら 2D が透ける）。Phase 0-1 の暫定クリア（黒/alpha=1 相当）を、少なくとも alpha について正しくする（色/polyID/fog の完全版は Phase 3-1） |
| 2c | **readback 経路**: `RenderFrame()`（VCount 215）完了後に `ColorTarget`（1x）を shared `MTLBuffer` へ blit（256×192×4 = 196KB）。`GetLine(line)` はそのバッファの行ポインタを返す。scale>1 の間は 3D を 1x で描く（または box downsample blit）— hires は Phase 4 で正式対応。readback 完了待ちは「最初の GetLine 呼び出し時に 1 回」へ遅延させ、フレームループへの直列化を最小化 |
| 2d | **ソース切替と A/B 検証**: `GetLine` のソースを soft ⟷ native で切り替えるスイッチ（設定 or env）を用意。`MELONPRIME_METAL_DIAG` に「同一フレームを両方で実行して per-pixel diff を出す」比較モードを追加 — **以後の Phase 3 パリティ作業の計測器になる**（soft 3D が正解データ）。既知の未実装（半透明・シャドウ・toon・fog・edge）由来の差分はカウント分類して許容 |
| 2e | **final composer の削除**: `MetalRenderer::VBlank()` の compose / `AnalyzeFinalRouting` / `MetalFinalState` / `RendererOutput::MetalTexture` 経路を撤去し、`GetOutput()` は `SoftRenderer::GetOutput()`（CpuBgra、GetLine 統合済みの完全合成フレーム）へ。presenter は CpuBgra 1 本化（`screenTex` アップロード経路 — 既存）。`RendererOutputKind::MetalTexture` は enum ごと温存し Phase 4 で復帰 |
| 2f | Delegate（SoftRenderer3D）は 2d の切替用に残すが、native ソース時は `RenderFrame` を呼ばない（CPU 二重ラスタライズの排除）。savestate（`PreSavestate`/`PostSavestate`）・`RestartFrame`/AbortFrame・`RenderFrameIdentical` スキップの整合をこのとき確認 |

**DoD:**
- native ソースで MPH の 3D が **HUD 込みで**正しく表示される（opaque 範囲で soft と視覚一致）。
- A/B diff モードで差分が「未実装機能に分類される画素」のみ。
- フリッカーゼロ（ルーティング経路が存在しないことによる構造的保証）。
- `MELONPRIME_METAL_PERF=1` で soft 比のフレームタイム記録（GetLine 統合の副作用計測）。

---

### Phase 3: native 3D 機能パリティ — GLRenderer3D 移植の完遂（1–2週 / 本丸B）

Phase 2 の A/B diff ハーネスを計測器として、`GPU3D_OpenGL.cpp`（1,491 行）の残機能を移植する。
サブフェーズごとに独立コミット + diff 縮小を数値で記録。**順序は依存関係順**:

| # | 内容 | 参照（GL 実装） |
|---|---|---|
| 3-1 | **クリアプレーン正式対応**: `RenderClearAttr1/2` から色/alpha/polyID/fog フラグ/デプスを uniform で供給、クリアビットマップ（`ClearShaderBitmap` 相当）対応。Phase 0-1 で消したクリア三角形をここで正式復活 | `ClearShaderPlain/Bitmap`, :984 付近 |
| 3-2 | **提出順の保持**: 現在の unordered `groups` マップを、`RenderPolygonRAM` の順序（opaque 先行 / translucent 後行のソート済み順）を保つ**隣接バッチ化**（GL の `RenderPolygonBatch` と同じ RenderKey 連続 run 方式）へ変更。半透明の正しさの前提 |
| 3-3 | **半透明ポリゴン**: アルファブレンド、polyID による同一 ID 上書き禁止ルール、depth write 制御（translucent は書かないのが基本 + bit11）、`depth-equal`（attr bit14）比較モード対応（パイプライン variant 追加） |
| 3-4 | **シャドウマスク / シャドウ**: stencil 2 パス（GL 実装の stencil 使用規則を踏襲。`DepthStencilTarget` の stencil 面は確保済み） |
| 3-5 | **toon / highlight**: toon テーブル（32 色）+ `RenderDispCnt` を per-frame uniform 化し blendmode==2 を正実装 |
| 3-6 | **fog / edge marking**: attr ターゲット（fog フラグ・polyID・edge 情報）を GL と同レイアウトにし、フルスクリーン後段パスで fog 色ブレンド + edge marking（`RenderFogColor/Offset/Table`, edge color テーブル） |
| 3-7 | **line polygons / VRAM-display-capture-as-texture / BetterPolygons** の順に残件消化（BetterPolygons は最後、任意） |
| 3-8 | 各サブフェーズで A/B diff 縮小を記録し、完了時に代表シーン（起動→メニュー→アドベンチャー→マルチ対戦）の**視覚一致**を確認。`melonprime-metal-backend-plan.md` の Phase 8 表を更新 |

**DoD:** A/B diff が全代表シーンでゼロ近傍（DS ハード固有の丸め差など、文書化した許容差のみ）。
これが達成された時点で「Metal レンダラは soft と同じ絵を GPU で作る」状態になる。

---

### Phase 4: hires 化 — GLRenderer 完全ミラー（2–4週 / Phase 3 完了が前提）

native 解像度の呪縛（GetLine = 256 幅）を外し、内部解像度 2x/4x を実現する本命。
`metal_real_renderer_compute_reference_instructions.md` §2 の構造に正式準拠する。

| # | 内容 |
|---|---|
| 4a | **MetalRenderer2D**: `GPU2D_OpenGL.cpp`（1,913 行）の移植。サブフェーズ: BG text → affine → extended/large → OBJ/スプライト → window → ブレンド/特殊効果。2D 出力は `OutputTex2D[2]` 相当の Metal テクスチャ |
| 4b | **最終パス**: `GLRenderer::RenderScreen()` の移植 — per-line `uScreenSwap[192]`、dispMode 0–3（0=白 / 1=BG·OBJ / 2=VRAM display / 3=FIFO）、master brightness A/B、aux 入力。出力は 2-layer texture array（`FPOutputTex` 相当）。Phase A で作った compose 基盤（double buffer / frameSerial / presenter の texture array 経路）を**ここで正式再利用** |
| 4c | **capture**: `DoCapture` / `DownscaleCapture` / `SyncVRAMCapture` / `AllocCapture` の移植（capture は 2D/3D 両方の入力になるため 4a/4b と同時期） |
| 4d | **出力切替**: `RendererOutput::MetalTexture`（hires 2-layer）復帰、presenter の texture array 経路復活、`GetLine` readback は不要化（capture が GPU 化されるため）。`3D.GL.ScaleFactor` が可視結果に反映されることをテスター UI（既存の internal resolution combo）で確認 |
| 4e | 段階導入: 4a 完了まで「3D=Metal hires + 2D=CPU→アップスケール合成」のような中間ハイブリッドは**作らない**（第 3 の発明を繰り返さない）。Phase 2/3 の姿のまま 4a–4c を裏で建て、`RenderScreen` 一式が揃った時点で一括切替（env ゲート → 既定化） |

**DoD:** 1x/2x/4x で視覚一致 + シャープネス変化、代表シーンで soft/GL 比の絵一致、
capture を使うゲーム動作（MPH のスキャンバイザー等）確認。

---

### Phase 5: 同期・性能（2–4日 / 正しさ確定後）

SRP 文書 §13 の 10（「correctness の後にのみ」）を正式化。`MELONPRIME_METAL_PERF=1` の
before/after 添付を各コミット必須とする。

| # | 内容 |
|---|---|
| 5-1 | **`waitUntilCompleted` 撲滅**: フレーム内の clear + opaque（+ Phase 4 後は 2D/最終パス）を **1 コマンドバッファ**に統合。CPU 可視性が要る箇所（Phase 2 の readback）は blit + `addCompletedHandler` / 次フレーム消費に置換。in-flight ring（2–3 フレーム、`dispatch_semaphore`）導入。diag readback（`MELONPRIME_METAL_DIAG`）だけは blocking 継続で可 |
| 5-2 | **バッファリング**: 毎フレーム `newBufferWithBytes` を廃し、vertex/index の ring buffer（フレームあたり 1 度の書き込み + オフセット消費）へ。index buffer のグループ毎生成を単一バッファ + オフセット描画へ |
| 5-3 | **presenter**: `uiOverlay` の毎フレーム全面 fill/全面アップロードを dirty-rect 化（GL パス OPT-DR1..DR3 と同じ思想。`CustomHud_Render` は既に dirty QRect を返す）。OSD/スプラッシュの変化検出。CPU fallback テクスチャの in-flight hazard（§E）もここで double-buffer 化 |
| 5-4 | **キュー統合の明文化**: renderer と presenter の queue 関係（同一 device / hazard tracking 前提）をコメント + 本計画に固定。必要なら shared context（設計書 §5.2 `MelonPrimeMetalContext`）導入 |
| 5-5 | 計測: OpenGL(High) プリセットとの比較値を `MELONPRIME_METAL_PERF` で記録し、backend-plan に転記。リリース構成に perf/diag 痕跡ゼロ（strings/nm、S22 相当） |

**DoD:** 通常経路に blocking wait ゼロ（diag 除く）、per-frame アロケーションゼロ（リサイズ時除く）、
perf 数値の記録、10 分ソークでフレームタイム分散悪化なし。

---

### Phase 6: 検証・公開ゲート（継続）

| # | 内容 |
|---|---|
| 6-1 | Intel 実機フルスモーク: resize/fullscreen/レイアウト全種/screen swap/HUD+編集モード/OSD/スプラッシュ/2 窓/ROM 再起動/savestate 往復 |
| 6-2 | **Apple Silicon 検証**（オープンゲート — これが通るまで experimental ラベルと `High2` 無効を維持し、既定プリセット化しない） |
| 6-3 | Xcode Metal API Validation / Instruments Metal System Trace をワンパス（validation エラーゼロ、コマンドバッファ/autorelease リークゼロ） |
| 6-4 | kill-switch 検査（`MELONPRIME_FORCE_DISABLE_METAL=ON` ビルドで痕跡ゼロ）、既存監査一式、`melonprime-metal-backend-plan.md` §3/§6 進捗表の更新、本計画を `plan/done/` へ移動 |

---

## 4. 実行順序と検証コマンド

```text
0 (止血・最優先) → 1 (レイヤ恒久化) → 2 (本丸A: 正しい合成) → 3 (本丸B: パリティ)
→ 4 (hires) → 5 (性能) → 6 (ゲート)
```

- Phase 0 だけでユーザー報告の 2 症状は解消する見込み（0-6 のログで機序確定まで行う）。
- Phase 2 完了までは「Metal = presenter + 3D ラスタライズ（診断可視）」、Phase 2 完了で
  「Metal 3D が正規合成で可視」、Phase 4 完了で「hires の正式 Metal レンダラ」。
- Phase 3 と Phase 1 は独立。Phase 5 は Phase 2 以降いつでも部分先行可（ただし wait 除去は
  正しさ確定後の原則を守る）。

毎フェーズの検証コマンド:

```sh
# ビルド（Metal テスト構成 + デフォルト構成の両方）
tools/macos/build_metal_test.command
cmake --build build-mac --parallel 4

# 実行（診断つき）
MELONPRIME_METAL_DIAG=1 tools/macos/run_metal_test.command /path/to/mph.nds 2>&1 | tee /tmp/metal-diag.log

# 診断モード（切り分け用・排他使用）
MELONPRIME_METAL_DIAG_FINAL_LAYERS=1 ...   # 合成/レイヤ経路のチェッカー表示
MELONPRIME_METAL_DIAG_SOLID_NATIVE3D=1 ... # native 3D 経路の単色表示
MELONPRIME_METAL_NATIVE_3D_VISIBLE=1 ...   # (Phase 0-3 で新設) native 3D を可視レイヤへ

# デフォルトバイナリ痕跡検査
strings build-mac/melonPrimeDS.app/Contents/MacOS/melonPrimeDS | \
  grep -E "metal presenter|metal probe|metal renderer|MELONPRIME_FORCE_METAL|MELONPRIME_METAL_" ; echo "expect: no output"
```

決定ログの読み方（Phase 0-6 / 1-1）:

| 観測 | 判定 |
|---|---|
| `metal final route:` が点滅中に毎フレーム連打 | フリッカー = B1（ルーティング交番）で確定 |
| 修正後 `metal 3d diag: nonzero=0` のまま（draws>0） | 0-1 以外のデプス/座標バグが残存 → solid-diag で切り分け |
| チェッカーモードでも点滅 | フリッカー = B2（レイヤ所有権）→ Phase 1-3 実施 |
| `usedNative3D=1` かつ `layerN.nonzero=0` | compose/合成側の欠陥（Phase 2 以降の経路で再発した場合の検知） |

---

## 5. リスクと対策

| リスク | 該当 | 対策 |
|---|---|---|
| 0-1 修正後も native が黒（別のデプス/座標バグ併存） | 0 | solid-diag → 頂点計算ダンプ（既存ハーネス手法）→ Z/W 両 variant を個別検証。診断基盤は整備済みなので切り分けは速い |
| sublayer / child NSView 化で入力（aim/クリック/Escape）退行 | 1 | 変更した場合は melonprime-aim-input.md §10 の macOS 入力スモークを必須化。まず WinIdChange ハンドラのみで様子見（最小差分優先） |
| GetLine 形式の取り違え（alpha/フラグ欠落で 2D ブレンド破壊 | 2 | 2a で soft 実装から形式を写経し、A/B diff ハーネスで soft ⟷ native を同フレーム比較。形式が合わなければ diff が全面に出るので即検知 |
| readback 直列化でフレームタイム悪化 | 2 | 遅延 wait（初回 GetLine 時）+ PERF 計測。悪化が大きければ「1 フレーム遅延消費」（3D が 1F 遅れる）を明示オプションで用意 |
| 半透明/ステンシルの順序バグ（3-2 の並べ替え起因） | 3 | GL の RenderPolygonBatch を run 単位で忠実に写す。A/B diff をサブフェーズ毎に取る |
| GLRenderer2D 移植の規模超過 | 4 | サブフェーズ（BG 種別ごと）で分割コミット。Phase 2/3 の姿が正しく動き続けるため、4 は中断・再開自由 |
| wait 除去でレース再発（過去の flicker の再来） | 5 | フレーム所有権の不変条件（compose=フレーム時刻 / present=getter 消費）を保ったまま同期プリミティブのみ置換。ring 導入時は frameSerial 検証ログを一時再有効化 |
| Apple Silicon 未検証のまま既定化してしまう | 6 | backend-plan の既存ゲートを維持: 公開プリセット化は AS 検証後。experimental ラベル継続 |

---

## 6. 進捗トラッキング

| Phase | 内容 | 状態 | 日付 | メモ |
|---|---|---|---|---|
| 0 | 止血（黒3D デプス修正 / 診断色隔離 / ルーティング遮断 / present スキップ） | 完了（機械検証済み / ROMスモーク未実施） | 2026-07-10 | `ClearNativeTarget()` は render-pass clear のみに変更し、depth=1.0 を保持。通常表示は CPU 合成済み2層へ固定、native 3D 可視化は `MELONPRIME_METAL_NATIVE_3D_VISIBLE=1` に隔離。source 無し active frame は drawable 取得前に present をスキップ。`build_metal_test.command` / `cmake --build build-mac --parallel 4` / default strings 検査 / config・inc・literal・scatter 監査 PASS。ワークスペース内に MPH ROM が無く、`run_metal_test.command <rom>` と `MELONPRIME_METAL_DIAG=1` 実ROMログは未実施。 |
| 1 | レイヤ所有権（WinIdChange / sublayer 検討 / スモーク） | 完了（機械検証済み / 手動スモーク未実施） | 2026-07-10 | `ScreenPanelMetal::event()` で `WinIdChange` / `Show` / `WindowStateChange` を捕捉し、現在の Qt 管理 `NSView` へ既存 `CAMetalLayer` を再接続。`setupScreenLayout()` でも再適用し、drawable size を更新。直接 `view.layer` 方式を維持し、入力リスクの高い sublayer / child NSView 化は未採用。`build_metal_test.command` / `cmake --build build-mac --parallel 4` / default strings 検査 / config・inc・literal・scatter 監査 PASS。`MELONPRIME_METAL_DIAG_FINAL_LAYERS=1`、resize/fullscreen/別スクリーン/入力の手動スモークは ROM/GUI 操作が必要なため未実施。 |
| 2 | GetLine 統合（正しい 2D/3D 合成、composer 削除、A/B ハーネス） | 完了（機械検証済み / ROM視覚検証未実施） | 2026-07-10 | Metal 3D target を native 256x192 に固定し、BGRA8 readback を soft 3D の 6-bit RGB + alpha ライン形式へ変換して `MetalRenderer3D::GetLine()` から返す経路へ切替。通常時は software 3D delegate を走らせず、`MELONPRIME_METAL_GETLINE_SOURCE=soft` または `MELONPRIME_METAL_GETLINE_DIFF=1` の時だけ delegate を使用。`MetalRenderer::VBlank()` は final composer を呼ばず、`GetOutput()` は `SoftRenderer::GetOutput()` の CPU BGRA（Metal 3D を soft 2D 合成済み）を返す。presenter は Metal renderer の CPU BGRA を `MetalGetLineCpuComposite` として通常ソース扱い。`build_metal_test.command` / `cmake --build build-mac --parallel 4` / default strings 検査 / config・inc・literal・scatter 監査 PASS。A/B diff 実ROMログ、HUD込み視覚一致、perf 実測は ROM 不在のため未実施。 |
| 3 | native 3D パリティ（clear plane / 半透明 / shadow / toon / fog / edge） | 進行中（3-1/3-3/3-4/3-5/3-6/3-7 実装項目は機械検証済み / ROM視覚検証未実施） | 2026-07-10 | 3-1 部分: plain clear plane の color/alpha/depth と attr target polyID/fog flag を `RenderClearAttr1/2` 由来に合わせ、`RenderDispCnt` bit14 の clear bitmap も VRAM texture slot 2/3 から Metal texture へ upload して fullscreen clear pass で color/attr/depth を書くようにした。3-2: opaque pass の `unordered_map` 全体 coalesce を廃止し、GL の `RenderPolygonBatch` と同じ隣接 key run（WBuffer / texture / TexRepeat）で提出順を保持。3-3 部分: fragment alpha を保持し、半透明 non-shadow polygon を描画対象に含め、blended pipeline と depth less/less-equal + write/no-write state variants を追加。opaque は stencil に polyID を書き、translucent は `0x40|polyID` reference の not-equal/replace で同一 translucent ID の再描画抑止に寄せた。translucent attr target は通常時に既存 attr を保持し、fog 有効かつ polygon attr bit15 が未設定の時だけ blue channel write mask の pipeline で fog flag を clear するようにして GL の `glColorMaski(1, false, false, transfog, false)` に寄せた。3-4 部分: shadow mask polygon を color write 無しの Z/W stencil-only pipeline で通し、mask 提出前に stencil bit7 だけを clear、depth-fail 時に bit7 を set する foundation を追加した。actual shadow polygon は GL と同じく単独 group で texture/alpha discard 付きの stencil-only prepass を行い、bit7 が残った領域だけ blended visible draw で描画して `0x40|polyID` を stencil 下位に書く二段 pass を追加した。clear-alpha-zero 背景向けの特殊 shadow 経路と ROM A/B visual parity は未完。3-5 部分: `RenderDispCnt` と toon table を Metal 3D shader uniform に追加し、GL の `3DRenderFS.glsl` と同じ `blendmode==2` toon/highlight color substitution を実装。3-6 fog+edge 部分: depth/attr target を読む fullscreen fog pass と edge pass を追加。3-7 line 部分: `Polygon::Type==1` を GL と同じく最初の重複しない2頂点で `MTLPrimitiveTypeLine` 描画するようにした。VRAM display-capture texture は SoftRenderer-derived Metal 経路では `DoCapture()` が emulated VRAM に書いた内容を `Texcache<>` が通常 direct-color texture として読むため、capture-backed texture を明示検出して `captureTextured` 診断に出すようにした。BetterPolygons は renderer setting を `MetalRenderer3D` へ渡し、4頂点以上では GL と同じ center vertex + center-fan splitting を使うようにした。`build_metal_test.command` / `cmake --build build-mac --parallel 4` / default strings 検査 / config・inc・literal・scatter 監査 / `git diff --check` PASS。ROM A/B diff、clear-alpha-zero shadow 特殊経路、`captureTextured>0` 実ログは ROM 不在のため未実施。 |
| 4 | hires（MetalRenderer2D + RenderScreen + capture + MetalTexture 出力復帰） | 進行中（4a scaffold / 機械検証済み） | 2026-07-10 | `GPU2D_Metal.{h,mm}` を追加し、Metal 有効ビルドだけで Engine A/B 用の非可視 `MetalRenderer2D` ミラーを初期化する足場を作成。現時点では `OutputTex2D` 相当の BGRA8 render-target/ shader-read texture を scale/device に追従して確保するのみで、BG text/affine/OBJ/window/blend/final `RenderScreen` は未実装。Phase 4e の制約どおり `Rend2D_A/B` は soft のまま維持し、可視出力は Phase 2/3 の CPU 合成 `GetOutput()` を継続。`build_metal_test.command` / `cmake --build build-mac --parallel 4` / default strings 検査 / config・inc・literal・scatter 監査 / `git diff --check` PASS。 |
| 5 | 同期・性能（wait 除去 / ring / presenter dirty-rect） | 未着手 | — | |
| 6 | 検証・公開ゲート（Apple Silicon / validation / kill-switch / 文書） | 未着手 | — | |
