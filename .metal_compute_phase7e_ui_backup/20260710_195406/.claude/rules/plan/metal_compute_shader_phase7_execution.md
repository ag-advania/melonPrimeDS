# Metal Compute Shader Phase 7 実行計画

## 目的

macOS の Metal バックエンドへ、OpenGL `ComputeRenderer3D` と同じタイルベース compute rasterizer を実装する。
既存の `MetalRenderer3D` は比較基準および旧GPU向けラスタ版として維持する。

## 今回の実装範囲: Phase 7A/7B Foundation

1. `TexcacheMetalLoader` を `GPU3D_TexcacheMetal.h` へ抽出し、ラスタ版とcompute版で共有可能にする。
2. `MetalComputeRenderer3D` を正式な `Renderer3D` 派生クラスとして追加する。
3. 以下のGL compute orchestration kernelをMSLへ移植する。
   - `ClearIndirectWorkCount`
   - `ClearCoarseBinMask`
   - `CalcOffsets`
   - `SortWork`
4. GLのグローバルバリアを、Metalではcompute encoder境界として表現する。
5. フォーク固有の修正を最初から組み込む。
   - Fix D: real count保持 + 32,768単位のY/Z dispatch分割情報
   - Fix E: `MaxWorkTiles`を超えるwork descriptorの読み書き禁止
6. 起動時synthetic self-testでbuffer layout、atomic offset、variant sort、Fix D/Eを検証する。
7. `MELONPRIME_METAL_COMPUTE_FOUNDATION=1`の場合のみ新クラスを選択する。
   可視出力は次段階まで既存`MetalRenderer3D`を比較基準として使用する。

## GLSLからMSLへの固定変換規約

| GLSL | Metal |
|---|---|
| SSBO | `device` buffer |
| UBO | `constant` buffer |
| `atomicAdd` | `atomic_fetch_add_explicit` |
| work-group barrier | `threadgroup_barrier` |
| `glMemoryBarrier` | compute encoderを分割して順序と可視性を確定 |
| indirect dispatch | `dispatchThreadgroupsWithIndirectBuffer`。移植前段階では同じ3-word layoutを生成してreadback検証 |
| image load/store | `texture2d` / `texture2d_array`の`read`、`write`、`read_write` |

## 完成までの実装順

### 7A: Foundation

今回実装。buffer contract、atomic orchestration、共有Texcache、renderer slotを確定する。

### 7B: Span interpolation

- `SpanSetupY` / `SpanSetupX` / `SetupIndices`をGL版とbyte一致させる。
- `InterpSpans` Z/W両variantをMSLへ移植する。
- CPUで作ったknown spanを投入し、GL期待値または手計算値とreadback比較する。

### 7C: Polygon binning

- `BinCombined`を移植する。
- coarse mask、fine mask、work offset、unsorted descriptorを検証する。
- Fix Eの超過ケースをsynthetic testで強制発生させ、OOBゼロを確認する。

### 7D: Rasterise variants

- NoTexture Z/W
- Texture Modulate / Decal
- Toon / Highlight
- ShadowMask
- texture sampler 3x3 address mode
- capture 128/256 texture

variant単位でMetal raster版とのpixel diffを縮小する。

### 7E: DepthBlend / FinalPass

- color/depth/attr tile統合
- translucent polygon ID規則
- clear bitmap
- anti-aliasing
- edge marking
- fog
- final RGBA8 texture出力

### 7F: 正式切替

- `MELONPRIME_METAL_COMPUTE=1`でcompute出力を可視化する。
- Metal raster版との同一フレームA/B diffを追加する。
- `RendererSettings`へ正式なraster/compute切替を追加する。
- `HiRes Coordinates`をMetal compute選択時にも有効化する。

### 7G: 公開ゲート

- Intel MacとApple Siliconで検証する。
- Metal API Validationエラーゼロ。
- MPHのMagmaul爆発、死亡エフェクト、ghost通過など既知mosaic再現箇所で破損ゼロ。
- `MELONPRIME_FORCE_DISABLE_METAL=ON`で全痕跡ゼロ。

## Foundation検証ログ

成功時:

```text
[MelonPrime] metal compute foundation: selected developer foundation mode
[MelonPrime] metal compute foundation: self-test PASS device=... threadWidth=... maxThreads=... fixDChunk=32768 fixEMaxWorkTiles=8
```

この段階ではゲーム画面は既存Metal raster版と同じである。compute self-test失敗時は初期化を失敗させ、黙って別経路へ切り替えない。

<!-- MELONPRIME_METAL_COMPUTE_SPANBIN_V2 -->
## Phase 7B: 実フレーム InterpSpans / BinCombined

- `GPU3D::RenderPolygonRAM`からGL Computeと同型の`SpanSetupY`、`SetupIndices`、`RenderPolygon`を毎フレーム構築。
- MSL `mp_compute_interp_spans_geometry`でX spanの幾何範囲を生成。
- MSL `mp_compute_bin_combined`でcoarse/fine tile binning、work descriptor生成、variant work count集計を実施。
- Phase 7Aの`CalcOffsets` / `SortWork`も実フレーム経路へ接続し、sorted work listまでGPU上で完走。
- 3-slotの非同期buffer ringを使用し、通常フレームに`waitUntilCompleted`を追加しない。
- Fix Dのreal work count/dispatch分割情報とFix Eの`MaxWorkTiles`クランプを維持。
- `RendererSettings::HiresCoordinates`をcompute span構築へ伝播。
- 可視出力はpixel parity完了まで`MetalRenderer3D`を継続し、途中実装による画面破損を防止。

成功ログ:

```text
[MelonPrime] metal compute span/bin: self-test PASS ...
[MelonPrime] metal compute span/bin: frame=... polygons=... xSpans=... variants=... sortedWorkTiles=...
```
