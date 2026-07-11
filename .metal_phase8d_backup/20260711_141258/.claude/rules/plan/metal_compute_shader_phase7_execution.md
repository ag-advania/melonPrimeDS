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

<!-- MELONPRIME_METAL_COMPUTE_PHASE7E_UI -->
## 2026-07-10 — Phase 7E / renderer UI

- Video Settings exposes separate **Metal** and **Metal Compute Shader** renderer IDs.
- Metal Compute selection no longer depends on `MELONPRIME_METAL_COMPUTE_FOUNDATION`.
- Phase 7E consumes Phase 7D Color/Depth/Attr work-item tiles in a non-visible Metal DepthBlend pass.
- Current DepthBlend scope: clear state, opaque depth selection, equal-depth handling, basic translucent blending, result Color/Depth/Attr buffers and asynchronous diagnostics.
- Visible output intentionally remains `MetalRasterReference` until texture variants and FinalPass reach parity.
- Known pre-match dark-transition composition issue remains deferred until the compute final-output cutover.

<!-- MELONPRIME_METAL_COMPUTE_PHASE7G_HIRES -->
## 2026-07-10 — Phase 7G high-resolution visible-output authority

- Metal Compute internal-scale changes are applied live; the renderer is no longer reconstructed through a temporary default 1x state.
- `3D.GL.ScaleFactor` is authoritative for the Metal raster target and final two-layer output texture.
- The visible-output path rejects stale target dimensions and self-heals after a target resize.
- High-resolution 3D selection uses frame-latched polygon activity as a fallback when live Engine-A DISPCNT has advanced before composition.
- Scaled Metal final textures use linear downsampling so 2x/3x/4x supersampling is visible even when ordinary 1x screen filtering is disabled.
- The compute renderer still uses the validated Metal raster result as its visible source while textured compute Rasterise remains under development.

<!-- MELONPRIME_METAL_COMPUTE_PHASE7I_MSL_INIT_FIX -->
## 2026-07-10 — Phase 7I MSL初期化修正

- `BinPolygon`のMSLアドレス空間を`device const&`へ修正。
- compute foundation失敗時はMetal raster可視出力のみで継続。
- degraded modeではcompute readinessをfalseに固定。
- renderer初期化失敗時のSoftware fallbackへエラーログを追加。

<!-- MELONPRIME_METAL_COMPUTE_PHASE7J_RENDER_OPTIONS -->
## 2026-07-10 — Phase 7J Metal描画オプション

- Metal rasterに既存実装済みのBetterPolygons center-fan分割をVideo Settingsから有効化。
- Metal rasterへ高解像度座標モードを実装。
- scale > 1かつ設定ONの場合、`Vertex::HiresPosition`をtarget-pixel座標へ変換し、shaderのscreenSizeも同じ座標系へ変更。
- Metal Computeではhidden compute mirrorとvisible `RasterReference`へ同じ高解像度座標設定を適用。
- 1x時は高解像度座標設定による描画変更なし。
- OpenGL ComputeではBetterPolygonsを引き続き無効化。

<!-- MELONPRIME_METAL_PHASE7L_FRAME_HANDOFF_FADE -->
## 2026-07-11 — Phase 7L Metal frame handoff / fade fix

- 最終2画面textureへpresenter leaseを追加し、別queueの画面描画完了までring slotを再利用しない。
- メニュー／マップなど前後フレーム差が大きい場面で上下画面が混ざるcross-queue read/writeを修正。
- scale変更時は新規leaseを停止し、producer/presenter完了後にring textureを再確保。
- Engine A/BのMaster Brightnessをnative/high-resolution 3Dへ適用してから高解像度3Dを差し替える。
- 試合開始時の暗転で3Dだけ明るく残る／背景復元が破綻する問題を修正。
- MetalとMetal Compute Shaderの共通visible-output経路へ適用。

<!-- MELONPRIME_METAL_PHASE7M_VISIBILITY_MASK -->
## 2026-07-11 — Phase 7M per-pixel 3D visibility mask

- 1xが正常で2x以上だけ壊れることからhigh-resolution 3D replacementを根本原因として確定。
- SoftRenderer2Dの最終layer selectionから各画面・各ピクセルの安全な3D差し替えモードを生成。
- mode 0: 2D BG／OBJ／window／capture／effectが3Dを覆うためCPU pixelを維持。
- mode 1: 3Dが直接最前面なのでhigh-resolution sampleへ直接置換。
- mode 2: native 3Dが第2layerへalpha blendされるためbackground復元後に再合成。
- maskはScreenSwapとframebuffer double bufferingへ追従。
- Master Brightnessが有効なscanlineはmaskを0にして暗転をCPU compositeのまま表示。
- Metal／Metal Compute Shader共通のvisible-output shaderがmaskを参照。

<!-- MELONPRIME_METAL_PHASE7N_FRAME_SNAPSHOT -->
## 2026-07-11 — Phase 7N current-frame 3D snapshot

- 効果がなかったPhase 7M visibility maskを完全撤回。
- `GPU_Soft.h/.cpp`と`GPU2D_Soft.cpp`をmelonDS本来の実装へ復元。
- Metal専用コード以外へ追加した変更をゼロに戻す。
- DSはVCount 215で次フレーム3Dを描画し始めるため、FinishFrame時のlive Metal targetはCPU framebufferより1フレーム先だった。
- VCount 192の`Finish3DRendering()`でcurrent-frame high/native 3Dを専用textureへblit snapshot。
- FinishFrameではlive targetではなくsnapshotと完成済みCPU framebufferを合成。
- ScreenSwap、high-resolution gate、rendered scale、Master Brightnessもsnapshot時点で固定。
- Metal／Metal Compute Shader共通のMetal専用経路のみを変更。

<!-- MELONPRIME_METAL_PHASE7O_PRESENT_RATE_CONTROL -->
## 2026-07-11 — Phase 7O Metal presentation rate control

- Fast Forward Toggle／HoldとFrame Limit Toggleの入力・`curFPS`／`doLimitFPS`更新自体は正常。
- Metal presenterの`CAMetalLayer`が既定のdisplay-syncを維持し、Screen.VSync OFFでも別の約60Hz上限を作っていた。
- Metal layer生成時は`displaySyncEnabled=NO`から開始。
- 通常時は`Screen.VSync`設定へ追従。
- Fast Forward Toggle／HoldまたはSlow Motion中はdisplay syncを強制OFFし、終了時に設定値へ復帰。
- layer propertyはGUI threadへqueued invocationして更新。
- 変更対象はmacOS Metal専用の`MelonPrimeScreenMetal.mm`だけ。melonDS共有rendererは変更しない。

<!-- MELONPRIME_METAL_PHASE7P_HIGH_PERFORMANCE -->
## 2026-07-11 — Phase 7P Metal high-performance path

- OpenGL ClassicとMetalの無制限FPS差を、GPU待機、readback、CPU変換、buffer allocation、非可視compute mirrorへ分解。
- OpenGLと同じ`RenderFrameIdentical` fast pathをMetalへ追加。
- unchanged frameではMetal描画、native resolve、GPU→CPU readback、CPU色変換をすべて省略。
- native BGRA8→RGB6A5変換を256-entry LUTへ変更し、1フレーム約20万回の整数除算を除去。
- vertex/index用`MTLBuffer`を毎フレーム生成せず、容量拡張式のpersistent shared bufferへ変更。
- 隣接polygonの同一TexParam/TexPaletteではTexcache lookupを再利用。
- Metal Computeのcompute foundationは現在非可視mirrorであるため、productionでは既定OFF。
- `MELONPRIME_METAL_COMPUTE_MIRROR=1`で従来のdeveloper compute mirrorを再有効化可能。
- 変更は`GPU3D_Metal.mm`と`GPU3D_MetalCompute.mm`だけ。melonDS共有SoftRendererは変更しない。

<!-- MELONPRIME_METAL_PHASE8A_GPU_RESIDENT_2D -->
## 2026-07-11 — Phase 8A GPU-resident 2D / no-readback normal path

- 既存`GPU2D_Metal` scaffoldへOBJ raster、window、mosaic、priority、blend、3D BG0合成を行うMSL pipelineを追加。
- Metal 2DはMetal 3Dと同じ`MTLCommandQueue`を使用し、BG prerenderの`waitUntilCompleted`を撤去。
- `MELONPRIME_METAL_FULL_GPU=1`時、display mode A/B=1かつdisplay capture未使用のframeをGPU内で完結。
- 対象frameでは`MetalRenderer3D::ReadbackNativeColorTargetToLineBuffer()`を実行しない。
- scanlineごとのScreenSwapとMasterBrightnessをGPU final passへ渡す。
- capture-backed BG/OBJ、VRAM display、FIFO display、途中で条件が変化したframeは安全側へrejectし、次frameから従来CPU compositorへ戻す。
- Metal Compute選択時も`RasterReference`のreadbackを同じ契約で停止できる。
- Metal Computeの独自可視texture完成はPhase 7D/7Eのtexture variants、shadow、DepthBlend、FinalPass完了後に別途cutoverする。

<!-- MELONPRIME_METAL_PHASE8B_GPU_DISPLAY_CAPTURE -->
## 2026-07-11 — Phase 8B GPU display capture / on-demand readback

- Metal GPU内にCapture128（16 layer）とCapture256（4 layer）を追加。
- Source AのEngine A 2D／3D、Source BのLCDC VRAM／Display FIFOをMSL computeでcapture。
- EVA／EVB blend、128x128、256x64／128／192、destination offsetとwrapを実装。
- capture-backed bitmap BG／bitmap OBJをMetal 2D compositorから直接sample。
- capture sourceとdestinationが同一の場合はsnapshot textureを使用し、旧VRAM内容を読むDS動作を維持。
- 通常capture frameではGPU→CPU readbackを行わない。
- CPUがcapture VRAMを読む、renderer変更、scale変更など実際にCPU coherenceが必要な場合だけ該当captureをRGBA5551へ同期readback。
- 同一frameでcapture destinationをBG／OBJへfeedbackするscanline依存ケースは旧CPU経路へ安全にfallback。
- `MetalComputeRenderer3D::RenderFrame()`は`ForceScaleFactor()`失敗を処理し、`[[nodiscard]]` warningを解消。

<!-- MELONPRIME_METAL_PHASE8C_COMPUTE_TEXTURED_RASTER -->
## 2026-07-11 — Phase 8C Metal Compute textured tile raster

- Metal Compute tile rasterを全DS texture format（1〜7）へ拡張。
- A3I5、2/4/8bpp palette、4x4 compressed、A5I3、direct RGB555をraw VRAMからMSLでdecode。
- Clamp／Repeat／Mirror、Modulate／Decal／Toon／Highlightを実装。
- Phase 8BのCapture128／Capture256 textureをCompute texture sourceへ接続。
- texture VRAM、texture palette、toon tableはframe slotごとのbufferへsnapshotし、in-flight frameとの競合を防止。
- ComputeとMetal raster／capture／2Dを同一MTLCommandQueueへ統合。
- このPhaseではvisible sourceをMetal RasterReferenceのまま維持し、Compute結果を非可視検証する。
- Shadow mask／shadow polygon、完全DepthBlend、FinalPass、visible cutoverは次Phase。
