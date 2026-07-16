# Metal Compute Shader 内部解像度が効かない問題 — 根本原因と修正指示

日付: 2026-07-10 JST
対象ブランチ: `highres_fonts_v3`
調査時 HEAD: `4c278247`（Phase 7H：内部解像度修正）
調査環境: 実機 Intel Mac（Intel Iris Plus Graphics 655 / Metal 3 / macOS 15.7.7）+ 実 MPH ROM（JP）

## 1. 結論（最重要）

**Metal Compute Shader 選択時、内部解像度変更のコードは 1 行も実行されていなかった。**
compute 用 MSL シェーダライブラリが実機の Metal コンパイラで**コンパイル失敗**し、
その結果 `MetalRenderer::Init()` 全体が失敗、`GPU::SetRenderer()` が**サイレントに
plain `SoftRenderer`（常時 256x192）へフォールバック**していたため。

つまり「Metal Compute Shader」を選んでいたつもりでも、実際に動いていたのは
ただのソフトウェアレンダラである。Phase 7G（表示修正）/ 7H（内部解像度修正）が
何度直しても効かなかったのは、**修正対象のコード（scale sync / visible output /
compose）が一度も呼ばれない状態だった**からで、修正内容自体の問題ではない。

修正は **1 トークン**。本調査セッションで実機 + 実 ROM で end-to-end 検証済み（§4）。

## 2. 根本原因の連鎖（実ログ証拠付き）

### 2.1 MSL コンパイルエラー

`src/GPU3D_MetalCompute.mm` の `kMetalComputeSource` 内、`BinPolygon` ヘルパの
第 1 引数がデフォルト（thread）アドレス空間の参照で宣言されているのに、
呼び出し側は `device` バッファの要素を直接渡している:

```cpp
// 定義（GPU3D_MetalCompute.mm:593 付近、Phase 7B で導入）
static inline bool BinPolygon(
    thread const RenderPolygon& polygon,   // ← thread 空間参照
    int2 topLeft,
    int2 botRight,
    device const SpanSetupX* xSpans)

// 呼び出し（同ファイル :655 / :678、mp_compute_bin_combined カーネル内）
device const RenderPolygon* polygons [[buffer(1)]];
...
BinPolygon(polygons[polygonIdx], ...)      // ← device 空間の lvalue を渡す
```

MSL はアドレス空間の異なる参照束縛を許さないため、実機ランタイムコンパイラ
（`newLibraryWithSource:`）はこう失敗する:

```text
[MelonPrime] metal compute: MSL compile failed: program_source:424:13: error: no matching function for call to 'BinPolygon'
            BinPolygon(polygons[polygonIdx], coarseTopLeft, coarseBotRight, xSpans))
program_source:362:20: note: candidate function not viable: cannot bind reference in address space 'device' to object in default address space in 1st argument
（:447 の 2 箇所目も同一エラー）
```

**このシグネチャと呼び出しは Phase 7B（`b09bec88`）から一度も変わっていない**
（`git log -L` で確認済み）。バックエンド計画の進捗表が Phase 7A/7B を
「実機ビルド待ち」としていたとおり、**この MSL は実機で一度もコンパイルに
成功したことがない**。計画文書に書かれた self-test PASS ログは期待フォーマット
であり、実測記録ではなかった。

### 2.2 失敗の伝搬とサイレントフォールバック

1. `MetalComputeRenderer3D::CreateComputeFoundation()`（GPU3D_MetalCompute.mm:1445 付近）
   が `newLibraryWithSource:` 失敗で `false` を返す。
2. `MetalComputeRenderer3D::Init()`（:1377）が `false` を返す
   （この時点で `RasterReference.Init()` は**成功済み**なのに全体を失敗にする）。
3. `MetalRenderer::Init()`（GPU_Metal.mm）の `if (!Rend3D->Init()) return false;` で失敗。
4. `GPU::SetRenderer()`（src/GPU.cpp:315 付近）の `if (!good)` 分岐が
   **`SoftRenderer` を無言でインストール**する。該当箇所のコメントは
   `// TODO: report error to platform` のまま。
5. 以後、Video Settings で内部解像度を何度変えても
   `MetalRenderer::SetRenderSettings` は存在しないオブジェクトの上の話になり、
   画面は常に native 256x192 の CPU 合成。

### 2.3 修正前の実ログ（HEAD `4c278247`、ScaleFactor=4 設定で起動）

```text
[MelonPrime] metal compute: selected from Video Settings
[MelonPrime] metal renderer: initializing native Metal 3D GetLine integration path
[MelonPrime] metal renderer3D: internal target scale=1 size=256x192 resolve=256x192
[MelonPrime] metal compute: MSL compile failed: ... 'BinPolygon' ...
[MelonPrime] metal presenter: visible source=MetalGetLineCpuComposite softwareFallback=0
```

- `metal compute scale sync` / `metal visible output: configured` /
  `first compose` / `forcing target scale` が**一切出ない** = スケール系コード未実行。
- presenter は CPU BGRA（256x192）のみを表示。

## 3. 必須修正（Fix 1 — 1 トークン）

`src/GPU3D_MetalCompute.mm` の `BinPolygon` 宣言（:593-597）:

```diff
 static inline bool BinPolygon(
-    thread const RenderPolygon& polygon,
+    device const RenderPolygon& polygon,
     int2 topLeft,
     int2 botRight,
     device const SpanSetupX* xSpans)
```

- `BinPolygon` 内はフィールド read のみなので `device const&` で正しい
  （コピー版 `const RenderPolygon poly = polygons[i];` を呼び出し側に置く案より
  変更が小さく、余計な threadgroup/thread コピーも発生しない）。
- 呼び出し 2 箇所（:655 / :678）は無変更でそのまま束縛できる。
- ライブラリ全体のコンパイルはこの 1 箇所で通る。**他に潜在エラーはない**
  （修正後の実機ランタイムコンパイルで全カーネル・全パイプライン生成 +
  全 self-test PASS を確認済み — §4）。

## 4. 修正後の実機検証結果（本調査セッションで実施済み）

ビルド: `cmake --build build-mac-metal-test --parallel 4`（クリーン、新規警告なし）
実行: `build-mac-metal-test/melonPrimeDS.app/Contents/MacOS/melonPrimeDS <MPH JP ROM>`
（config: `3D.Renderer = 4`（MetalCompute）, `3D.GL.ScaleFactor = 4`）

```text
[MelonPrime] metal compute span/bin: configured scale=1 ... maxWorkTiles=12288 ... slots=3
[MelonPrime] metal compute foundation: self-test PASS device=Intel(R) Iris(TM) Plus Graphics 655 threadWidth=32 maxThreads=1024 fixDChunk=32768 fixEMaxWorkTiles=8
[MelonPrime] metal compute span/bin: self-test PASS rectangle=16,8..48,24 workTiles=8 sorted=8
[MelonPrime] metal compute tile memory: self-test PASS work=1 covered=64 ...
[MelonPrime] metal compute texture variants: Phase 7F ready scale=1 target=256x192; visible output remains Metal raster reference
[MelonPrime] metal renderer3D: forcing target scale=4 expected=1024x768 actual=256x192
[MelonPrime] metal renderer3D: internal target scale=4 size=1024x768 resolve=256x192
[MelonPrime] metal compute scale sync: applied forced scale=4 target=1024x768 compute=1024x768
[MelonPrime] metal visible output: configured scale=4 textureArray=1024x768x2
[MelonPrime] metal presenter: source texture type=3 layers=2 size=1024x768 ... visibleSource=MetalFinalTexture scale=4 softwareFallback=0
```

- **compute foundation self-test が実機で初めて PASS**。
- Phase 7H の scale sync / forced resize / visible output 再構成が設計どおり動作。
- presenter が 1024x768x2 の `MetalFinalTexture` を受信（= 内部解像度が可視化）。
- 実行中の Video Settings からのライブ変更 4x → 6x → 1x も全て即時適用を確認:

```text
[MelonPrime] metal compute scale sync: applied forced scale=6 target=1536x1152 compute=1536x1152
[MelonPrime] metal visible output: configured scale=6 textureArray=1536x1152x2
[MelonPrime] metal visible output: first compose renderer=MetalCompute scale=6 renderedScale=6 size=1536x1152 engineALayer=0 high3D=1
```

- ゲーム内 3D フレームで `high3D=1`（7H のフレームラッチゲートも正常）。
- compute mirror も毎フレーム動作（tile memory / depth blend / texture variants
  の診断が frame=1,2,3... と流れ、`hiresCoords=1`）。
- ログ全体で `failed / error / busy / mismatch / refusing` は **0 件**。

補足: `first compose` の 1 回目が `high3D=0` になるのは正常（ブート直後は 3D
ポリゴンなし / スケール切替直後の 1 フレームは `renderedScale != OutputState->Scale`
ゲートで意図的に native 合成へ落とす設計）。

## 5. 推奨修正（Fix 2 — graceful degradation、強く推奨）

今回の障害モードの本質的な問題は「**非可視のミラーである compute foundation の
失敗が、可視出力を担う raster reference まで道連れにして、無言で SoftRenderer に
落ちる**」こと。アーキテクチャ上、compute は raster を可視ソースとする鏡像
（backend-plan / flicker-plan の恒久ルール）なので、foundation 失敗時は
raster のみで続行するのが正しい。

`MetalComputeRenderer3D::Init()`（GPU3D_MetalCompute.mm:1377）を変更:

- `RasterReference.Init()` 失敗 → 従来どおり `false`（本当に何も出せない）。
- `CreateComputeFoundation()` / `ConfigureSpanBinResources()` / 各 self-test の
  失敗 → **`true` を返して raster-only モードで続行**。
  `State->Ready = false; State->SpanBinReady = false;` のままにし、
  1 回だけ loud なログを出す:

```text
[MelonPrime] metal compute: foundation unavailable; continuing with Metal raster visible source only
```

- `RenderFrame()` は既に `State->SpanBinReady` ゲート付きで
  `RasterReference.RenderFrame()` を先に実行する構造（7H）なので、
  raster-only 続行に追加変更は不要。
- `SetScaleFactor()` も `ForceScaleFactor()` を State チェックより先に呼ぶ構造
  （7H）なので raster 側のリサイズはそのまま効く。

これで将来別の MSL/デバイス互換性問題が出ても「Metal Compute を選んだら
なぜかソフトレンダラだった」という今回の症状は構造的に再発しない。

## 6. 推奨修正（Fix 3 — フォールバックの可視化、小）

`GPU::SetRenderer()`（src/GPU.cpp）の `// TODO: report error to platform` 分岐に
最低限 stderr ログを追加する（upstream 所有ファイルなので
`#ifdef MELONPRIME_DS` ガード + 最小差分で）:

```cpp
else
{
#ifdef MELONPRIME_DS
    Platform::Log(Platform::LogLevel::Error,
        "Renderer init failed; falling back to software renderer\n");
#endif
    // TODO: report error to platform
}
```

developer ビルドなら OSD 通知も検討可（必須ではない）。サイレントフォール
バックが今回 3 コミット分（7G/7H 含む）の的外れ修正を生んだ直接の温床。

## 7. 実施手順とDoD

1. Fix 1 を適用（1 トークン）。
2. Fix 2 / Fix 3 を適用（推奨。分離コミット可）。
3. `cmake --build build-mac-metal-test --parallel 4` — クリーン。
4. `cmake --build build-mac --parallel 4` — デフォルト（Metal 無効）ビルドに影響なし
   （`GPU3D_MetalCompute.mm` は `MELONPRIME_METAL_ACTIVE` 時のみコンパイル）。
5. 実 ROM 起動（MetalCompute + scale 4x 設定）で以下を確認:
   - `MSL compile failed` が**出ない**
   - `metal compute foundation: self-test PASS`
   - `metal compute scale sync: applied forced scale=4 target=1024x768`
   - `metal visible output: configured scale=4 textureArray=1024x768x2`
   - `metal presenter: ... visibleSource=MetalFinalTexture scale=4`
   - 画面の 3D が目視でシャープになる（1x と比較）
6. 実行中に Video Settings で 1x/2x/4x を切替 → 各切替で
   `scale sync: applied forced scale=N` + `visible output: configured scale=N` が出て
   表示が追従する。
7. Fix 2 の検証: 一時的に MSL を故意に壊して（例: 適当な構文エラー挿入）起動し、
   raster-only 続行ログが出て**ゲームが Metal raster の可視出力のまま動く**こと、
   その状態でも内部解像度変更が効くことを確認。検証後に戻す。
8. 既存監査（inc-ownership / literal / scatter）は非対象ファイルのため影響なし。
   デフォルトバイナリ strings 検査（Metal 文字列ゼロ）は従来どおり。

## 8. やってはいけないこと

- compose / scale sync / visible output 層をこれ以上いじらないこと。
  7G/7H の実装は検証の結果**正しく動いている**。効かなかった原因は本 MD の
  コンパイル失敗のみ。
- `BinPolygon` を呼び出し側での thread コピー（`RenderPolygon p = polygons[i];`）に
  する案は不採用（コピーコスト増・変更量増）。`device const&` 化が正。
- compute foundation 失敗時に `SoftRenderer` へ落とす現挙動を「安全側」として
  温存しないこと（可視品質・解像度・すべての Metal 系修正が無効化される）。

## 9. 関連する注意（バグではないが記録）

- 高スケール時の compute mirror メモリ: 4x で tileMemory 144 MiB、6x で
  192 MiB（× 3 slots）。Intel Iris 655（recommendedMaxWorkingSetSize ≈ 1.5 GB）
  ではまだ許容範囲だが、compute の可視化カットオーバー時に予算を再確認する。
  6x では既に `fullCoverage=0`（Fix E クランプ）で縮退運転になる。
- スケール切替直後の 1 フレームは `high3D=0`（renderedScale ラッチとの一致
  ゲート）。設計どおりであり修正不要。
- 進捗文書（`metal_compute_shader_phase7_execution.md` / flicker-plan Phase 7 行 /
  backend-plan）の「7A/7B 実機ビルド待ち」を、本修正コミットで「実機 self-test
  PASS 済み」に更新すること。

<!-- MELONPRIME_METAL_COMPUTE_PHASE7I_MSL_INIT_FIX -->
## 10. 実装ステータス

Phase 7IでFix 1〜3を実装する。

- `BinPolygon`を`device const RenderPolygon&`へ修正。
- compute初期化失敗時はMetal raster可視出力のみで続行。
- degraded modeではcompute readinessをfalseに維持。
- `GPU::SetRenderer()`のSoftware fallbackをエラーログへ記録。
- Phase 7G/7Hのcompose・scale・visible-output層は変更しない。

<!-- MELONPRIME_METAL_COMPUTE_PHASE7J_RENDER_OPTIONS -->
## 2026-07-10 — Phase 7J Metal描画オプション

- Metal rasterに既存実装済みのBetterPolygons center-fan分割をVideo Settingsから有効化。
- Metal rasterへ高解像度座標モードを実装。
- scale > 1かつ設定ONの場合、`Vertex::HiresPosition`をtarget-pixel座標へ変換し、shaderのscreenSizeも同じ座標系へ変更。
- Metal Computeではhidden compute mirrorとvisible `RasterReference`へ同じ高解像度座標設定を適用。
- 1x時は高解像度座標設定による描画変更なし。
- OpenGL ComputeではBetterPolygonsを引き続き無効化。
