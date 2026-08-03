# Metal Compute 残作業の実装記録

**対象ブランチ:** `develop_metal_renew`
**前提資料:** [`compute-shader-phase7-execution.md`](compute-shader-phase7-execution.md)

Metal Compute の残作業一覧（P0 1〜10 と P1 14）に対する実装内容と、実際に検証した
範囲・未検証の範囲をまとめる。

## 事前に判明した阻害要因

`GPU3D_MetalComputeDepthBlendShaders.inc` の translucent 経路に構文エラー
（`if (secondAccepted)` の本体欠落）が残っており、`kMetalComputeCompleteDepthBlendSource`
の MSL コンパイルが必ず失敗していた。その結果 `CreateComputeFoundation()` が
false を返し、Metal Compute は常に `MetalRenderer3D` へ縮退していた。
これを修正しない限り以降の項目は動作確認できないため、最初に修正した。

## 実装内容

| 項目 | 対応 |
|---|---|
| 1. X-Span 属性補間 | `mp_compute_interp_spans_geometry` を OpenGL `InterpSpans` と同じ構造へ全面書き換え。Z/W・頂点色・UV を Y 方向に補間し、左右 edge 交差時の入れ替え、x-major/y-major、dummy span、水平 polygon を含む |
| 2. Coverage と edge 情報 | `EdgeParams_XMajor` / `EdgeParams_YMajor`、`CovLInitial` / `CovRInitial`、`InsideStart` / `InsideEnd`、`FillInside` / `FillLeft` / `FillRight` を移植。raster kernel は固定 31 ではなく DS の 0〜31 coverage を生成する |
| 3. 固定小数点補間 | `float` ベースの perspective factor を廃止し、`CalcYFactorX` / `CalcYFactorY` / `InterpolateAttrPersp` / `InterpolateAttrLinear` / `InterpolateZZBuffer` / `InterpolateZWBuffer` を移植。W-buffer / Z-buffer は `wBuffer` フラグで実行時分岐 |
| 4. Tile work capacity | `MaxWorkTiles` を tile memory 予算から導出し、`TileWorkCapacity` と常に一致させた。加えて CPU 側で polygon の bounding box から work 数の上界を求め、上界が capacity を超えるフレームは compute を丸ごと諦めて raster へ fallback する。**一部 work だけを捨てることはしない** |
| 5. Frame slot 分離 | Color/Depth/Attr tile memory、2 層 buffer、final color、native resolve を `FrameSlot` ごとに保持。グローバルな `TileMemoryInFlight` を削除し、GPU が 1 フレーム遅れても raster へ落ちない |
| 6. Compute native resolve | `mp_compute_native_resolve` を追加。compute の最終色から 256x192 の texture と shared buffer を生成する。scale 1 では恒等コピー |
| 7. Compute `GetLine()` | final pass が RGB6/A5 を buffer へ出力し、native resolve が 256x192 へ落とす。`GetLine()` はフレームあたり 1 回だけ完了待ちして shared buffer をコピーする。`RenderXPos` スクロールも raster 経路と同じ扱い |
| 8. 通常構成での可視化 | 可視条件から `CpuReadbackRequired` を除去。`MELONPRIME_METAL_FULL_GPU=1` は不要になり、`CpuReadbackRequired` は「compute の出力方法」だけを決める |
| 9. Display capture 接続 | `MetalRenderer::PublishCaptureTexturesTo3D()` を追加し、`ConfigureMetalCaptureState()` の後（成功・失敗・無効いずれも）に呼ぶ。失敗時は dummy texture へ戻る |
| 10. Variant 数超過 | variant 255 への畳み込みをやめ、256 を超えたフレームは compute を諦めて raster へ fallback する |
| 14. RasterReference 依存 | `GetLine()` と `GetNativeResolveTexture()` が compute 所有になった。`RasterReference` は緊急 fallback 専用として残す |

## 付随して必要だった修正

- **command buffer の寿命（ARC 非対応 TU）**。`src/GPU3D_MetalCompute.mm` は `core`
  ターゲットに属し、`-fobjc-arc` が付かない（ARC が有効なのは
  `src/frontend/qt_sdl/CMakeLists.txt` の一部ファイルだけ）。
  `-[MTLCommandQueue commandBuffer]` は autoreleased なので、`FrameSlot::LastCommand`
  へそのまま保持すると `RenderFrame()` の `@autoreleasepool` が抜けた時点で解放され、
  `GetLine()` 側の `waitUntilCompleted` が解放済みオブジェクトを触る。
  compute が実際に可視になって初めて毎フレーム踏むようになったため、
  `RetainFrameCommand()` / `ReleaseFrameCommand()` で明示的に retain する。
- 同じ理由で、`newBufferWithLength:` / `newTextureWithDescriptor:` の戻り値（+1）を
  scale 変更時に解放していなかった。`ReleaseFrameSlotResources()` を追加。
  4x では 1 slot あたり数百 MiB になるため無視できない。
- capture texture は `MetalRenderer` 側が所有し scale 変更で作り直すので、
  compute 側の参照は retain する。

- **VRAM coherency の所有権**（新規）。compute が可視のフレームでは `RasterReference.RenderFrame()`
  が走らないため、`Texcache::Update()` 経由の `MakeVRAMFlat_TextureCoherent()` /
  `MakeVRAMFlat_TexPalCoherent()` も走らない。compute は flat VRAM を直接読むので、
  この更新を `MetalComputeRenderer3D::RenderFrame()` が自分で行う。
  `DeriveState()` は dirty bit を破壊的に消費するため、消費者はフレームごとに 1 つだけ。
  同フレーム内で raster へ fallback した場合は `MetalRenderer3D::InvalidateTexcache()` で
  raster 側の texture cache を捨て、coherent な flat VRAM から再デコードさせる。
- **identical-frame 高速経路**が texture 変化を見ていなかったため、`RenderFrameIdentical`
  だけでなく VRAM 変化なしも条件に加えた。
- **raster kernel の並列度**。1 work item を 1 thread が舐めていたのを、OpenGL と同じ
  `TileSize x TileSize` threadgroup（1 pixel = 1 thread）へ変更した。
- **stale fine mask**。`numSetupIndices == 0` のとき binning が走らないので、
  depth blend が前フレームの mask を読まないよう `polygonGroups` を 0 にする。
- **wireframe の alpha 順序**。OpenGL は texture blend の後に `polyalpha == 0 -> a = 31`
  を適用する。事前に 31 へ置換すると modulate の alpha が変わるため順序を合わせた。

## ファイル構成

- `src/GPU3D_MetalComputeSpanMath.inc`（新規）— DS 固定小数点 span 演算と
  `SpanSetupY` / `SpanSetupX` の宣言。span/bin ライブラリと textured raster
  ライブラリの両方へ、ライブラリ生成時に前置される。
  MSL は `ulong` を持つので、GLSL 側の 64bit エミュレーション（`Div64_32_32` など）は
  直接の 64bit 演算として表現している。

## 検証状況

**検証済み**

- macOS `build-mac-metal`（`MELONPRIME_ENABLE_METAL=ON`）でのフルビルド成功。
- 4 つの MSL ライブラリすべてが `newLibraryWithSource:` で実際にコンパイルされること
  （スクラッチのランタイム checker で確認）。修正前は depth-blend が必ず失敗していた。
- `check-inc-ownership.ps1`、`audit-melonprime-srp-performance.ps1`、
  `audit-platform-scatter-budget.ps1 -Budget 22`、
  `audit-melonprime-thread-boundary.ps1 -Strict`、`git diff --check` が PASS。

**未検証**

- 実 ROM での描画・体感。起動時 self-test も実機実行していない。
- Windows / Linux CI。本変更は Metal 専用ファイルのみだが、別クレームである。
- 性能。raster kernel の並列度変更は構造上の改善だが、`tools/perf/` での測定は未実施。

## 意図的に対象外とした項目

一覧の P1 11〜13（テクスチャ形式・depth/blend/shadow・edge/fog/AA の self-test 拡張）、
P2 15（upload/dispatch 最適化の残り）、P2 16（pixel-diff 回帰テスト基盤）は、
いずれも検証基盤側の作業のため今回のスコープから外している。
ただし 15 のうち「tile 単位の threadgroup 並列化」だけは、実プレイ時の
フレームレートに直結するため先行して実装した。
