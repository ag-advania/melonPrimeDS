# melonPrimeDS `develop_metal` 完全Metal化 実装監査報告

**監査日:** 2026-07-17  
**対象リポジトリ:** https://github.com/ag-advania/melonPrimeDS  
**対象ブランチ:** `develop_metal`  
**監査HEAD:** `8077db617b0a0e097f621231217da97632069c40`  
**比較元:** `develop` (`db87eb30f6de6285828dadcb06f121033dc40d47`)  
**比較状態:** `develop_metal` は `develop` より6コミット先行、遅延0  
**監査対象計画:** `docs/plans/melonPrimeDS_develop_完全Metal化_詳細修正指示書.md`  
**監査方式:** GitHub上のHEADソースを対象とした静的コード監査  
**実行環境上の制約:** 本監査ではmacOS実機上のビルド、Metal Validation、GPU Frame Capture、実ROMプレイを再実行していない。リポジトリ内文書に記録された実測値・実ROM確認は「実装者による検証記録」として扱い、ソースコードから確認できる構造と分離して評価する。

---

# 1. 結論

## 1.1 総合判定

> `develop_metal` は、通常の非Display-Captureフレームについて、Metal 3D、Metal segmented 2D、Metal最終合成、Metal presenterを使うGPU常駐経路を既定で試行する状態まで進んでいる。  
> しかし、計画で定義した「完全Metal」は未達である。

現時点の正確な呼称は次である。

```text
通常フレーム:
Metal 3D / Metal Compute
→ Metal segmented 2D
→ Metal final texture array
→ CAMetalLayer presenter

Display Capture・不適格・初期化待ち・異常時:
Metal 3D readback または Metal raster reference
→ SoftRenderer 2D
→ CPU BGRA
→ Metal textureへ再アップロード
→ CAMetalLayer presenter

HUD / OSD:
QImage / QPainter
→ dirty-rect CPU upload
→ Metal UI blend
```

したがって、現在は以下の段階である。

- **Metal GPU常駐経路のproduction化:** 大きく前進
- **通常フレームのreadback削減:** 相当部分を達成
- **Display Captureを含む全フレームのGPU常駐化:** 未達
- **`SoftRenderer`依存撤廃:** 未達
- **Metal Computeの完全独立化:** 未達
- **HUD／OSDのMetal native化:** 未達
- **初回既定バックエンドのMetal化:** 未達

## 1.2 監査判定一覧

| 区分 | 判定 |
|---|---|
| Metal APIを実際に使用しているか | **合格** |
| Metal 3D rasterが実際に描画するか | **合格** |
| Metal Computeが実際にcompute kernelを実行するか | **合格** |
| Metal 2Dが実際にBG／OBJ／compositorを描画するか | **合格** |
| rendererとpresenterの`MTLDevice`が統一されたか | **合格** |
| Full-GPU 2Dが既定試行されるか | **合格** |
| 非capture通常フレームでCPU readbackを避けられるか | **概ね合格** |
| captureフレームもGPUだけで完結するか | **不合格** |
| `SoftRenderer`を継承せず独立しているか | **不合格** |
| output leaseがMetal texture-onlyを保証するか | **不合格** |
| Compute選択時に常にCompute textureが表示されるか | **不合格** |
| HUD／OSDがCPU画像合成を使わないか | **不合格** |
| MetalがmacOS初回既定か | **不合格** |
| Metal OFF／FORCE_DISABLEを含むCI確認 | **未確認** |

## 1.3 リリース可否

### 「Metal実験ブランチ」として

**条件付き可。**

- 通常フレームのFull-GPU経路を試験する目的には使用可能。
- CPUフォールバックを維持しているため、未対応シーンでも即座に全画面崩壊しにくい。
- 実装者の記録では実ROM確認と性能計測が行われている。

### 「完全Metal実装」として

**不可。**

理由:

1. capture開始フレームを`CaptureCnt bit31`でFull-GPU対象外にしている。
2. 対象外フレームでは`SoftRenderer::DrawScanline`、`DrawSprites`、`VBlank`へ戻る。
3. `MetalRenderer`自体が`SoftRenderer`を継承している。
4. `AcquireOutputLease()`と`GetOutput()`が`SoftRenderer::GetOutput()`へ戻れる。
5. presenterが`CpuBgra`を受理し、CPU画面を`replaceRegion`でアップロードできる。
6. HUD／OSDが毎フレーム`QImage`／`QPainter`を使用する。
7. Metal Computeが`RasterReference`を保持し、条件不成立時に描画を委譲する。

---

# 2. 差分概要

`develop`から`develop_metal`までの差分は、6コミット、21ファイルである。

主な変更領域:

- Metal output contract拡張
- renderer／presenter共有`MTLDevice`
- Full-GPU segmented 2Dの既定化
- capture適格性判定とsticky cooldown
- capture CPU fallback uploadの非同期化
- Metal Compute shader修復
- compute capture texture配線
- presenter output slot競合修正
- HUD／OSD dirty-rect upload
- Metal専用ビルドスクリプト
- 完全Metal化計画書と残作業文書

主要な実装ファイル:

```text
src/GPU.h
src/GPU_Metal.h
src/GPU_Metal.mm
src/GPU_MetalFullGpuMethods.inc
src/GPU_MetalCaptureMethods.inc
src/GPU_MetalStrictDiagnostics.h
src/GPU2D_Metal.mm
src/GPU2D_MetalFullGpuMethods.inc
src/GPU3D_Metal.mm
src/GPU3D_MetalCompute.mm
src/GPU3D_MetalComputeDepthBlendShaders.inc
src/MetalContext.h
src/MetalContext.mm
src/frontend/qt_sdl/MelonPrimeScreenMetal.mm
src/frontend/qt_sdl/CMakeLists.txt
```

---

# 3. フェーズ別監査

## 3.1 M0: 診断・カウンター

**判定: 条件付き完了**

実装済み:

- `MELONPRIME_METAL_PERF`
- visible source集計
- CPU readback frame／byte集計
- software delegate集計
- presenter fallback集計
- `MELONPRIME_METAL_ASSERT_GPU_ONLY`
- ログモードと`abort`モードの分離

評価:

- Full-GPU化の観測性は改善している。
- fallbackが無言で発生する状態から、原因を追跡できる状態へ進んでいる。

残る問題:

1. `MELONPRIME_METAL_ASSERT_GPU_ONLY`は既定無効であり、production contractを強制しない。
2. 既知のcapture fallbackやmid-frame invalidationはstrict violation対象外である。
3. `GPU_Metal.mm`等のfunction-static集計オブジェクトは非atomicであり、複数インスタンスが別render threadで同時に動く場合、診断有効時にデータ競合する可能性がある。
4. カウンターはインスタンス別ではなくプロセス全体で混在する。

必要修正:

- 集計を`MetalRenderer`／presenter instance stateへ移動する。
- 複数インスタンス識別子をログへ付与する。
- CI／テスト用ビルドではstrict modeを自動有効化できるCMake optionを追加する。
- 「既知fallback」と「予期しないcontract violation」を別カウンターにする。

---

## 3.2 M1: output contractとlease

**判定: 部分完了**

実装済み:

- `RendererOutput`へ次のmetadataを追加。
  - `FrameSerial`
  - `Width`
  - `Height`
  - `ArrayLength`
  - `Scale`
  - `Generation`
- `RendererOutputLease`でpresenter完了まで出力slotを保持。
- published slotを新規compose先として再利用しない。
- presenter command buffer完了時にleaseを解放。

良い点:

- renderer queueとpresenter queueが別でも、presenterがsampling中のtextureをrendererが上書きしない。
- published slot raceへの対策は妥当。
- completion handlerでleaseを解放する設計も妥当。

未達点:

1. presenterは追加されたmetadataを実質的な契約検証に使用していない。
2. presenterは実際の`MTLTexture`を問い合わせてtype／arrayLengthをログ確認するだけで、異常なtextureでも描画を継続する。
3. `ArrayLength < 2`を検出しても`ERROR`ログだけであり、描画拒否しない。
4. `Width`、`Height`、`Scale`、`Generation`と実textureの一致を検証していない。
5. `AcquireOutputLease()`が失敗すると`SoftRenderer::GetOutput()`を返す。
6. `GetOutput()`も`SoftRenderer::GetOutput()`へ戻る。
7. lease contextを毎フレーム`new`しており、presenter hot pathに小さなheap allocationが残る。

必要修正:

```cpp
bool ValidateMetalRendererOutput(
    const RendererOutput& output,
    id<MTLTexture> texture,
    id<MTLDevice> expectedDevice);
```

検証項目:

- `Kind == MetalTexture`
- `texture != nil`
- `texture.device == expectedDevice`
- `texture.textureType == MTLTextureType2DArray`
- `texture.arrayLength == 2`
- `output.ArrayLength == 2`
- `output.Width == texture.width`
- `output.Height == texture.height`
- `output.Scale >= 1`
- `output.Width == 256 * output.Scale`
- `output.Height == 192 * output.Scale`
- `output.Generation != 0`
- serial／generationの単調性

Metal選択時に検証失敗した場合は、通常モードでは直前の有効なMetal textureを維持し、strictモードでは明示的に失敗させる。CPU BGRAへ暗黙復帰してはならない。

---

## 3.3 M2: 共有`MTLDevice`

**判定: 完了**

実装済み:

- `MetalContext.h/.mm`を追加。
- `MTLCreateSystemDefaultDevice()`を`std::call_once`で一度だけ実行。
- Metal 3D rendererが共有deviceを使用。
- Metal presenterも同じ共有deviceを使用。
- dual-GPU Macでrendererとpresenterが異なるdeviceを選ぶ問題を構造的に防止。

注意点:

- 共有されているのは`MTLDevice`であり、`MTLCommandQueue`ではない。
- renderer側とpresenter側は別queueを作成している。
- これは正常な設計だが、一部コメントに「shared renderer/presenter queue」と読める記述があり、実装と一致しない。

修正文言:

```text
誤:
CaptureState->Queue is the shared renderer/presenter queue

正:
CaptureState->Queue is the renderer-side shared queue used by 3D, 2D,
capture and final composition. The presenter owns a separate queue and
synchronizes through RendererOutputLease.
```

将来の実装者が「queueが同一だから順序保証される」と誤認しないよう修正すること。

---

## 3.4 M3: Full-GPU 2Dの既定化

**判定: 完了。ただし「常時Full-GPU」ではない**

実装済み:

```cpp
const char* env = std::getenv("MELONPRIME_METAL_FULL_GPU");
FullGpuState->Requested = !env || env[0] != '0';
```

- 環境変数未指定でFull-GPUを要求。
- `MELONPRIME_METAL_FULL_GPU=0`を緊急kill switchとして維持。
- mixed 3D＋2D overlayを許可。
- segmented snapshotが揃った場合にMetal 2Dへ進む。
- mid-frame invalidation後に60フレームcooldownを入れ、毎フレーム失敗する持続フリーズを抑制。

重要な解釈:

> 「Full-GPUが既定」とは、各フレームで適格性判定を行い、適格ならFull-GPUを使うという意味である。  
> capture、不適格、初期化失敗、snapshot不足ではCPU経路へ戻る。

したがってM3単体は完了しているが、完全Metal化の完了を意味しない。

---

## 3.5 M4: per-scanline Display Capture

**判定: 未実装。完全Metal化の最大ブロッカー**

現在の適格性判定:

```cpp
if (GPU.CaptureCnt & (1u << 31))
    return false;
```

このため、次フレームでDisplay Captureが開始される場合、Full-GPU経路へ入らない。

結果:

```text
Start3DRendering
→ FrameActive=false
→ CpuReadbackRequired=true
→ Metal 3D native targetをCPU line bufferへreadback
→ SoftRenderer::DrawScanline
→ SoftRenderer::DrawSprites
→ SoftRenderer::VBlank
→ CPU completed captureをMetal capture textureへupload
```

一方、GPU capture shader自体は存在する。

- line configを192本記録
- source A／source Bを選択
- EVA／EVB blend
- capture128／capture256 texture arrayへwrite
- capture-backed 3D textureをComputeへ配線

しかし、production適格性判定がcapture開始フレームを除外するため、現在のGPU capture実装は「全captureフレームの正式経路」にはなっていない。

必要修正:

1. 2D segmentをcapture境界で分割する。
2. 各segmentについて次の順序を同一renderer queueでencodeする。

```text
BG/OBJ segment
→ 3D layerとの2D compositor
→ capture対象line rangeをcapture textureへwrite
→ 次segmentが更新済みcapture textureをsample
```

3. capture source Bが同一フレームのcapture destinationを読む場合、snapshotではなくsegment完了後のtextureを参照させる。
4. capture write後に必要なresource usage／barrierを明示する。
5. `CaptureCnt bit31`による全面除外を、対応可能なcapture mode別の判定へ縮小する。
6. unsupported modeは明示的な理由コードを記録する。
7. CPU正解画像とのchecksum／pixel diffを用意する。
8. 128x128、256x64、256x128、256x192の全sizeを検証する。
9. source A=2D、source A=3D、source B=VRAM、source B=FIFO、blendを個別検証する。
10. capture destination wrapとstart offset 0～3を検証する。

M4完了条件:

- capture active frameでも`FrameActive=true`。
- `CpuReadbackRequired=false`。
- `SoftRenderer::DrawScanline`が0回。
- `SoftRenderer::DrawSprites`が0回。
- `SoftRenderer::VBlank`が0回。
- `ReadbackNativeColorTargetToLineBuffer()`が0回。
- capture textureの通常フレームreadbackが0回。
- CPU参照が必要なVRAM access時だけon-demand readbackを許可。

---

## 3.6 M5: 通常フレームreadback 0化

**判定: 部分完了**

達成:

- Full-GPU適格フレームでは`CpuReadbackRequired=false`。
- Metal rasterはその場合`ReadbackNativeColorTargetToLineBuffer()`を実行しない。
- Computeは`!CpuReadbackRequired`をvisible cutover条件として使用。
- capture textureのCPU参照はon-demand syncへ限定する構造がある。

未達:

- capture frameでは`CpuReadbackRequired=true`。
- CPU 2D compositorへ3D lineを渡すため、3D readbackが必要。
- presenterのHUD用にCPU framebufferを問い合わせる。
- HUD表示時にbottom framebufferを`QImage`へコピーする。
- CPU fallback outputでは2画面を`replaceRegion`で再アップロードする。

追加の性能上の注意:

`MetalRenderer3D::RenderFrame()`のidentical-frame reuseは`State->NativeLineReady`を条件に含む。一方、GPU-only frameでreadbackを省略すると`NativeLineReady=false`へ設定されるため、Full-GPU経路では同一3Dフレーム再利用が効かず、毎回Metal 3Dを再encode／renderする可能性がある。

改善案:

- CPU line bufferのready状態とGPU color targetのready状態を分離する。

```cpp
bool NativeLineBufferReady;
bool ColorTargetReady;
uint64_t ColorTargetInputSignature;
```

- GPU-only identical frameではColorTargetを再利用する。
- CPU fallbackへ切り替わった瞬間だけnative resolveをreadbackする。

---

## 3.7 M6: `SoftRenderer`継承撤廃

**判定: 未着手**

現在:

```cpp
class MetalRenderer : public SoftRenderer
```

残る直接依存:

- `SoftRenderer::DrawScanline`
- `SoftRenderer::DrawSprites`
- `SoftRenderer::VBlankEnd`
- `SoftRenderer::VBlank`
- `SoftRenderer::GetFramebuffers`
- `SoftRenderer::GetOutput`
- `SoftRenderer3D Delegate`
- `GetLine()`互換契約

`MetalRenderer`の`Init()`も、Metal output／capture初期化失敗時にCPU compositorを維持する方針である。

撤廃順序:

1. M4完了。
2. capture frameの3D readback撤廃。
3. CPU compositor fallbackを診断専用adapterへ隔離。
4. `MetalRenderer : public Renderer`へ変更。
5. Metal 2D A／Bを正式renderer componentとして所有。
6. `GetFramebuffers`を通常Metal出力契約から除外。
7. `GetLine()`をproduction pathから除外。
8. Software比較は独立したdebug mirrorへ移動。
9. `AcquireOutputLease()`失敗時は直前Metal frame維持または`None`を返す。
10. Metal選択中の`CpuBgra`を型レベルで禁止する。

推奨型:

```cpp
class MetalRenderer final : public Renderer
{
public:
    RendererOutputLease AcquireOutputLease() override;

private:
    std::unique_ptr<MetalRenderer2D> Renderer2DA;
    std::unique_ptr<MetalRenderer2D> Renderer2DB;
    std::unique_ptr<Renderer3D> Renderer3DBackend;
    std::unique_ptr<MetalCaptureState> Capture;
    std::shared_ptr<MetalOutputState> Output;
};
```

---

## 3.8 M7: Metal Compute独立化

**判定: 部分完了**

達成:

- compute visibleは既定有効。
- compute shader compile failureを修正。
- span setup、binning、sorting、texture raster、depth blend、final passを実行。
- final texture submit成功時はcompute textureを可視ソースにする。
- capture texture arraysをComputeへ配線。
- `CUTOVER active`ログを持つ。

未達:

- `MetalComputeRenderer3D`が`MetalRenderer3D RasterReference`を常時保持。
- scale resize failure時にRasterReferenceへ戻る。
- stage未ready時にRasterReferenceへ戻る。
- `CpuReadbackRequired=true`時にRasterReferenceへ戻る。
- frame slot／tile memory不足時にRasterReferenceへ戻る。
- GPU command failure後はvisible cutoverを停止しRasterReferenceへ戻る。
- `GetLine()`は常にRasterReferenceへ委譲。
- `GetNativeResolveTexture()`もRasterReferenceを返す。
- capture frameはM4未完了のためCompute production出力にならない。

UI不整合:

`VideoSettingsDialog.cpp`の説明は現在も次の趣旨である。

```text
Metal Computeは実験的で、検証済みMetal rasterが可視fallbackとして残る。
```

実装はcompute finalを既定visibleに変更しているため、説明が古い。

更新案:

```text
Metal Compute Shader
Experimental native Metal compute renderer.
The compute final texture is used directly when all production stages are
available. Unsupported or failed frames temporarily use the Metal raster
compatibility path.
```

M7完了条件:

- production classから`RasterReference` fieldを除去。
- fallbackはrenderer切替単位に限定し、同一renderer内の暗黙fallbackを廃止。
- compute native resolveをcompute finalから生成。
- Compute選択中の全対応フレームで`LastFrameComputeVisible=true`。
- captureを含む。
- `GetLine()` production call countが0。
- UIとログが実経路に一致。

---

## 3.9 M8: presenter Metal texture-only化

**判定: 部分完了**

達成:

- `CAMetalLayer`へ実際にpresent。
- renderer所有の2-layer Metal texture arrayをsample。
- renderer output leaseをpresenter command completionまで保持。
- shared `MTLDevice`を使用。
- published slot再利用を防止。
- presenter側のin-flight semaphoreで`nextDrawable()`による長時間blockを抑制。

未達:

1. 起動grace中は`CpuBgra`を受理。
2. Metal texture取得失敗時にCPU BGRA fallbackを受理。
3. software fallback用`screenTex`を常時確保。
4. CPU fallbackでは2画面を`replaceRegion`でupload。
5. Metal texture取得時でもHUD用に`GPU.GetFramebuffers()`を呼ぶ。
6. output metadata検証が不十分。
7. invalid array textureを検出しても描画を拒否しない。

完全Metal向け修正:

```text
Metal renderer selected
  + valid Metal lease
    → present texture
  + no new Metal lease, previous valid lease texture exists
    → retain/present previous texture
  + no valid Metal texture at startup
    → black clear + explicit initialization OSD
  + strict mode
    → fail immediately
```

CPU fallback textureはdeveloper comparison buildだけで生成する。

```cpp
#if defined(MELONPRIME_METAL_ENABLE_CPU_PRESENT_FALLBACK)
    // debug only
#endif
```

---

## 3.10 M9: HUD／OSDのMetal化

**判定: 部分完了**

達成:

- UI overlayの全画面毎フレームclearをdirty rect clearへ変更。
- Metal UI textureの全画面毎フレームuploadをdirty rect uploadへ変更。
- overlay textureをMetalでalpha blend。
- 実装記録上、presenter時間は改善。

未達:

- `QImage uiOverlay`
- `QPainter overlayPainter`
- CPU font rasterization
- CPU OSD bitmap
- CPU splash bitmap
- CPU Custom HUD drawing
- HUD表示中のbottom framebuffer copy
- CPU radar composition
- UI textureへのCPU upload

HUD表示時:

```cpp
std::memcpy(
    m->bottomImage.bits(),
    bottomCpuBufForFrame,
    256 * 192 * 4);
```

これは1フレーム196,608 byteのCPU copyである。

また、Metal final textureを取得済みでも次を実行する。

```cpp
nds->GPU.GetFramebuffers(&topCpuBufForFrame, &bottomCpuBufForFrame)
```

`GPU::GetFramebuffers()`はrendererの`GetFramebuffers()`をそのまま呼ぶため、Metal presenterがSoftRenderer framebuffer contractへ依存し続ける。

完全Metal化方針:

1. HUD command listをCPUで構築する。
2. rectangle、line、circle、sprite、text glyphをMetal draw commandへ変換。
3. glyph atlasは初回／font変更時だけCPU生成。
4. OSD itemはglyph／icon atlas参照へ変換。
5. radarはMetal final texture layer 1を直接sample。
6. circle mask shaderでレーダー形状をclip。
7. HUD editor handleもMetal primitiveで描画。
8. `QImage uiOverlay`を通常プレイから除去。
9. `bottomImage`と毎フレームmemcpyを除去。
10. splashだけは別の低頻度upload経路として残してもよい。

---

## 3.11 M10: macOS初回既定Metal

**判定: 未着手**

現在のMelonPrimeDS初回既定:

```cpp
{"3D.Renderer", renderer3D_OpenGL}
{"Screen.UseGL", true}
```

Metalを選ばない限り、新規ユーザーはOpenGLで起動する。

変更はM4～M8完了後に限定する。

```cpp
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    {"3D.Renderer", renderer3D_Metal},
#else
    {"3D.Renderer", renderer3D_OpenGL},
#endif
```

`Screen.UseGL`もmacOS Metal buildではfalseにする。

注意:

- 既存設定を上書きしない。
- Metal feature probe失敗時だけOpenGLまたはSoftwareへフォールバック。
- renderer enum値がbuild optionによって変わり得るため、Metal OFF buildの設定互換を検証する。

---

## 3.12 M11: shader資産分離

**判定: 未着手**

現在もMSL sourceがObjective-C++文字列として埋め込まれている。

未実装:

- `.metal`分離
- build時`metallib`生成
- function constant整理
- pipeline cache
- offline compile failure検出
- shader ABI versioning

これは画質parityとM4～M9完了後に行うのが妥当。

---

# 4. 重大度別の監査指摘

## 4.1 HIGH: 完全Metal化を阻止する項目

### H-01 Display CaptureフレームがCPU／Software経路へ戻る

**場所:**

- `src/GPU_MetalFullGpuMethods.inc`
- `src/GPU_Metal.mm`
- `src/GPU3D_Metal.mm`

**根拠:**

- `CaptureCnt bit31`でFull-GPU不適格。
- `FrameActive=false`なら`CpuReadbackRequired=true`。
- `DrawScanline`／`DrawSprites`／`VBlank`がSoftRendererへ戻る。

**影響:**

- capture使用シーンで通常フレームreadbackが発生。
- Metal Compute選択でもRasterReferenceへ戻る。
- 完全Metalの完了Aを満たさない。

**修正:** M4を実装する。

---

### H-02 `MetalRenderer`が`SoftRenderer`に構造依存

**場所:**

- `src/GPU_Metal.h`
- `src/GPU_Metal.mm`

**根拠:**

```cpp
class MetalRenderer : public SoftRenderer
```

**影響:**

- CPU framebuffer、GetLine、Software 2D、CpuBgra fallbackがproduction contractに残る。
- Metal経路の不成立が暗黙のSoftware復帰になる。
- 完全Metalになったかを型構造から保証できない。

**修正:** M4／M5後にM6を実施する。

---

### H-03 HUD／OSDがCPU画像合成

**場所:**

- `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`

**根拠:**

- `QImage`
- `QPainter`
- CPU bitmap OSD
- bottom framebuffer memcpy
- `replaceRegion`

**影響:**

- 完了B未達。
- Metal final textureが存在してもCPU framebuffer dependencyが残る。
- Retina／大型windowでCPU帯域を消費。

**修正:** Metal UI rendererとnative radarを実装する。

---

### H-04 output lease失敗時にCPU BGRAへ戻る

**場所:**

- `src/GPU_Metal.mm`
- `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`

**根拠:**

```cpp
return RendererOutputLease(SoftRenderer::GetOutput(), nullptr, nullptr);
```

**影響:**

- Metal選択中でもCpuBgraが型として正規出力になり得る。
- startup grace後もstrict modeが無効ならCPU fallbackを表示できる。
- 完全Metal texture-only contractにならない。

**修正:** 直前のMetal texture維持、`None`、明示エラーのいずれかへ変更する。

---

## 4.2 MEDIUM: 正しさ・堅牢性上の項目

### M-01 非同期capture upload失敗時もmetadataをvalidにする

**場所:**

- `src/GPU_MetalCaptureMethods.inc`
- `UploadCpuCompletedCaptures()`

**現在の流れ:**

1. CPU VRAMからupload bufferを作る。
2. blit commandをcommit。
3. completion前に`Meta.Valid=true`、`PendingCpuUpload=false`へ更新。
4. command error時はログだけ出す。

**問題:**

GPU commandが失敗した場合、capture textureは更新されていないのにmetadataはvalidのままになる。その後、2D／3Dが古いcapture textureを正しいものとしてsampleする可能性がある。

**修正案:**

- upload対象layerとgenerationをcompletion handlerへ渡す。
- success時だけ`Valid=true`、`PendingCpuUpload=false`。
- error時は`Valid=false`、`PendingCpuUpload=true`。
- CaptureState lifetimeを`shared_ptr`またはgeneration tokenで保護。
- 複数uploadが同じlayerへ並ぶ場合、serialが最新のものだけmetadataを更新する。

---

### M-02 中断フレームがline 0だけ記録した場合のsnapshot ring疑義

**場所:**

- `src/GPU2D_MetalFullGpuMethods.inc`
- `BeginSegmentSnapshotFrameIfNeeded()`

現在のリセット条件:

```cpp
if (State->LayerSnapshotLastLine > 0 ||
    State->SpriteSnapshotLastLine > 0)
```

通常フレームはline 191まで進むため問題ない。しかし、frame restart／abort／savestate切替等でline 0だけ記録して中断した場合、次のline 0で前slotを解放・resetしない可能性がある。

**想定影響:**

- 古いreservationの再利用
- `SegmentedCaptureAttempted`が残る
- snapshot validityの持ち越し
- ring slot leakまたはstale frame

**修正案:**

```cpp
if (State->SegmentedCaptureAttempted ||
    State->SegmentedCaptureSlot >= 0 ||
    State->LayerSnapshotLastLine >= 0 ||
    State->SpriteSnapshotLastLine >= 0)
{
    ResetSegmentedSnapshotFrame();
}
```

ただし同一フレーム内で`DrawSprites(0)`と`DrawScanline(0)`が別順で呼ばれるため、単純な`>=0`化だけでは2回目のline 0呼出を新フレームと誤認する。frame serial／VCount epochを明示的に渡して判定するのが安全。

必要テスト:

- line 0直後のRestartFrame
- savestate load
- renderer switch
- pause／frame step
- GPU abort frame
- dual-instance

---

### M-03 output metadataがconsumerで強制されない

**場所:**

- `src/GPU.h`
- `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`

**問題:**

M1でmetadataを追加したが、presenterはmetadataを信頼境界として使っていない。2-layerでないtextureを検出してもログ後に描画を継続する。

**影響:**

- shaderが存在しないsliceを読む可能性。
- scale mismatchやstale generationを見逃す。
- output contractの追加が診断情報に留まる。

**修正:** M1節のvalidationを実装し、異常textureを拒否する。

---

### M-04 mid-frame invalidation時に1フレーム`RetainPrevious`

**場所:**

- `src/GPU_MetalFullGpuMethods.inc`
- `src/GPU_Metal.mm`

**現在の設計:**

- Full-GPU開始後に不適格化すると、そのフレームのSoftware scanlineは生成済みでない。
- VBlankでGPU frameをpublishできず、前フレームを維持する。
- sticky flagと60-frame cooldownで連続発生を抑制。

**評価:**

持続フリーズ修正としては妥当な暫定策だが、正しい当該フレームを表示していないため、完全なframe correctnessではない。

**修正:**

- M4のsegment/capture因果を完成させ、mid-frame invalidation要因をなくす。
- それでも発生可能なresource shortageは、frame開始前に予約する。
- 2D upload ring、output ring、compute frame slotを`Start3DRendering()`前に確保し、途中失敗を防ぐ。

---

### M-05 Compute backendが同一フレームでRasterへ暗黙復帰

**場所:**

- `src/GPU3D_MetalCompute.h`
- `src/GPU3D_MetalCompute.mm`

**影響:**

- UIでComputeを選んでもフレーム単位でMetal rasterになる。
- performance特性がシーンごとに変わる。
- Compute固有の不具合がfallbackで隠れる。
- parity完了を判断しづらい。

**修正:**

- debug fallbackとproduction outputを分離。
- production Computeではunsupported reasonを返し、renderer切替を上位層で一度だけ行う。
- 同一フレーム内のRasterReference fallbackを廃止。
- M4完了後に`RasterReference` fieldを削除。

---

## 4.3 LOW: 性能・保守性・表示上の項目

### L-01 CPU capture uploadで`MTLBuffer`を都度生成

`CpuUploadScratch`により`std::vector`の再確保は減っているが、upload lambda内で次を毎回実行する。

```objc
[CaptureState->Device newBufferWithBytes:...]
```

したがって「フレーム毎ヒープ確保除去」はMetal buffer allocationまで含めると未完了である。

**修正:**

- persistent staging buffer ringを用意。
- bank／size別に最大必要量を確保。
- command completionでslotを解放。
- `newBufferWithBytes`を通常fallback frameから除去。

---

### L-02 UI説明が実装と不一致

`VideoSettingsDialog.cpp`では、Compute rasterが可視sourceとして残る説明のままである。

現在の実装はcompute finalを既定visibleにしているため、文言を更新する。

---

### L-03 strict／perf集計の複数インスタンス対応

function-static non-atomic accumulatorは、複数インスタンス時に値が混ざる。診断有効時だけでもC++ data raceを避ける。

---

### L-04 shared queueに関するコメントが不正確

renderer componentsはrenderer queueを共有するが、presenterは別queueである。leaseがqueue間同期契約であることをコメントへ統一する。

---

### L-05 Metalが初回既定ではない

M10未実装。完全Metal化の技術的安定性を受け入れ試験で確認した後に変更する。

---

### L-06 stale source commentsと余分なnested scope

以下のような古い説明や整形崩れが残る。

- Compute headerが「visible output remains RasterReference」と記載。
- Metal raster側コメントが毎フレームreadback前提。
- Compute encoding部に`{ {`の重複scopeがある。

動作上直ちに問題ではないが、監査性を下げるため整理する。

---

# 5. 実装上の良い点

今回の差分で評価できる点:

1. Full-GPUを単純に強制せず、frame eligibilityを設けて正しさを優先している。
2. capture判定で`GPU.CaptureEnable`の時相問題を認識し、`CaptureCnt bit31`へ修正した。
3. mid-frame failureをsticky＋cooldownでbounded failureへ変えた。
4. output slotのpublished／in-flight／presenter refsを分離した。
5. presenter completionまでleaseを保持する。
6. rendererとpresenterのdevice identityを共有contextで保証した。
7. renderer-sideの3D、2D、capture、final compositionを同じqueueへ寄せた。
8. 2D snapshotをpersistent upload ringへ移した。
9. 2D segmentをscanline state hashでまとめ、不要なstate切替を減らしている。
10. Metal Computeのfinal textureを実際の可視sourceへ昇格させた。
11. Computeのcapture texture配線を追加した。
12. HUD／OSD uploadをdirty rect化した。
13. CPU VRAM access時のcapture readbackをon-demandに限定する方針は妥当。
14. macOS Metal専用build treeを分離した。

---

# 6. ビルド・CI監査

## 6.1 Metal ON build

専用スクリプトは次を明示する。

```text
-DMELONPRIME_ENABLE_METAL=ON
-DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON
-DUSE_QT6=ON
Release
Ninja
```

評価:

- canonical `build-mac`のCMake cacheにMetal OFFが残っていても、別treeで確実にMetal sourceをbuildできる。
- 開発用として妥当。

改善:

- `brew --prefix qt`と`brew --prefix libarchive`の存在確認を追加。
- Intel／Apple Siliconの両方でprefixを記録。
- configure outputにMetal source list／framework検出を表示。
- `ctest`または起動smoke testまで行う別scriptを追加。

## 6.2 Metal OFF build

HEAD時点のcombined statusに成功checkを確認できず、残作業文書にも`GPU.h`変更後のMetal OFF／FORCE_DISABLE再検証が未実施と記録されている。

必須matrix:

| OS | Metal option | 期待 |
|---|---:|---|
| macOS | ON | Metal raster／Compute／presenterをbuild |
| macOS | OFF | Metal source／framework／enum依存なし |
| macOS | FORCE_DISABLE=ON | Metal source／framework完全除外 |
| Windows | OFF | 既存Software／OpenGL正常 |
| Linux | OFF | 既存Software／OpenGL／Wayland正常 |

追加確認:

- `RendererOutput` aggregate initializationが全buildで成立。
- `renderer3D_Max`とconfig rangeがMetal ON/OFFで安全。
- Metal enum値を保存した設定をMetal OFF buildで読み込んだ場合のnormalize。
- `MELONPRIME_ENABLE_METAL`が`core PUBLIC`から意図せず非Apple consumerへ漏れないこと。
- `MetalContext.mm`がARCでcompileされること。
- Metal／QuartzCore linkが最終targetへ確実に入ること。

---

# 7. 必須受け入れ試験

## 7.1 静的contract試験

Metal選択中、通常プレイ6000フレームで以下を要求する。

```text
softwareDelegate=0/600
cpuReadbackFrames=0/600
cpuReadbackBytes=0
cpuComposite=0/600
retainPrevious=0/600
presenter softwareFallback=0
STRICT GPU-ONLY VIOLATION=0
```

M4完了後はcapture使用シーンでも同じ条件を要求する。

## 7.2 Renderer matrix

- Metal raster
- Metal Compute
- 1x
- 2x
- 3x
- 4x
- 8x
- 16x
- Better Polygons ON／OFF
- High Resolution Coordinates ON／OFF
- VSync ON／OFF
- frame limiter ON／OFF
- fullscreen／window
- Retina／non-Retina
- single screen／dual screen
- screen swap
- horizontal／vertical／hybrid layout

## 7.3 DS 2D parity

- text BG
- affine BG
- extended affine BG
- bitmap BG
- OBJ normal
- OBJ affine
- OBJ double-size
- OBJ mosaic
- OBJ window
- BG window
- color effect window mask
- alpha blend
- brighten／darken
- master brightness
- mid-frame register changes
- per-line mosaic
- priority ties

## 7.4 DS 3D parity

- opaque
- translucent
- shadow mask／shadow polygon
- alpha test
- texture format全種
- palette
- toon
- highlight
- fog
- edge marking
- W-buffer
- Z-buffer
- texture repeat／mirror／clamp
- clear image
- viewport
- RenderXPos
- identical frame
- AbortFrame

## 7.5 Display Capture

- source A 2D
- source A 3D
- source B VRAM
- source B FIFO
- A+B blend
- EVA／EVB 0～16
- 128x128
- 256x64
- 256x128
- 256x192
- destination bank A～D
- destination offset 0～3
- same-frame read-after-write
- capture textureをBGから読む
- capture textureをOBJから読む
- capture textureを3D polygonから読む
- CPUがcapture VRAMを読む
- CPUがcapture VRAMへ書く
- savestate直前／直後
- renderer switch直前／直後

## 7.6 lifecycle

- ROM boot
- reset
- ROM close
- renderer切替
- scale変更
- fullscreen切替
- display移動
- sleep／wake
- savestate save／load
- pause／resume
- frame step
- fast forward
- multi-instance
- multiplayer
- app shutdown

## 7.7 Metal Validation

- API Validation
- Shader Validation
- Thread Sanitizerを使えるCPU側build
- Address Sanitizer
- GPU Frame Capture
- command buffer error handler
- resource lifetime
- cross-queue texture lifetime
- output ring reuse
- snapshot ring reuse
- capture state reconfigure
- scale change中のgeneration invalidation

---

# 8. 推奨修正順序

## PR 1: 監査で見つかった堅牢性修正

対象:

- capture upload completionでmetadata更新
- upload失敗時のretry
- output metadata validation
- invalid texture array描画拒否
- snapshot frame epoch導入
- stale UI／comment修正
- diagnostic accumulator instance化
- persistent capture upload buffer ring

完了条件:

- 既存GPU／CPU fallbackの挙動を変えず、failure handlingだけ強化。
- macOS Metal ON/OFF build。
- Windows／Linux build。

## PR 2: M4 per-segment capture

対象:

- capture boundary segment
- same-frame read-after-write
- capture shader range dispatch
- 2D/capture interleave
- GPU parity diff

完了条件:

- capture active frameでもFull-GPU。
- CPU compositor 0。
- readback 0。
- pixel parity合格。

## PR 3: M5／M6

対象:

- CPU 3D line buffer production撤廃
- `SoftRenderer`継承撤廃
- `CpuBgra` fallback撤廃
- `GetLine()` production撤廃
- debug mirror分離

完了条件:

- Metal renderer型が`Renderer`直接派生。
- Metal選択中にSoftware renderer symbolへ到達しない。

## PR 4: M7

対象:

- Computeから`RasterReference`撤廃
- compute native resolve
- Compute capture parity
- renderer-level fallback policy

完了条件:

- Compute選択時、対応フレームの可視3Dが100% Compute。
- fallbackは初期化時のrenderer切替のみ。

## PR 5: M8／M9

対象:

- presenter Metal texture-only
- native radar
- HUD command list
- glyph atlas
- OSD Metal描画
- `QImage uiOverlay` production撤廃

完了条件:

- presenterが`GPU.GetFramebuffers()`を呼ばない。
- HUD表示中もCPU framebuffer copy 0。
- CPU UI texture uploadはfont／asset変更時だけ。

## PR 6: M10／M11

対象:

- macOS初回既定Metal
- shader asset分離
- metallib
- CI matrix
- release acceptance

---

# 9. 修正禁止事項

1. capture不具合を隠すためにcapture自体を無効化しない。
2. Full-GPU失敗時に無言でSoftwareへ戻さない。
3. Metal Compute選択中にRasterReference fallbackを「Compute成功」と記録しない。
4. `waitUntilCompleted`を通常フレームへ追加しない。
5. presenter queueとrenderer queueが同一だと仮定しない。
6. published textureをpresenter lease取得前に再利用しない。
7. `MTLTexture`をraw pointerだけで世代管理しない。
8. Metal選択中のCPU BGRAをproductionの正常系として残さない。
9. HUDをMetal化したという理由でfont rasterizationまで毎フレームGPU生成しない。
10. Metal専用変更を非Apple buildへ漏らさない。
11. melonPrime以外の既存コードを変更する場合、必要な`MELONPRIME_DS`／Metal build guardを外さない。
12. Software renderer／OpenGL rendererの既存経路を破壊しない。

---

# 10. 最終完了チェックリスト

## 完了A: エミュレーターGPU経路

- [ ] capture active frameもFull-GPU
- [ ] `SoftRenderer::DrawScanline` 0
- [ ] `SoftRenderer::DrawSprites` 0
- [ ] `SoftRenderer::VBlank` 0
- [ ] `SoftRenderer::GetFramebuffers` 0
- [ ] 3D normal-frame readback 0
- [ ] CPU BGRA screen upload 0
- [ ] Metal rasterのSoftware delegate production撤廃
- [ ] ComputeのRasterReference production撤廃
- [ ] presenter Metal texture-only
- [ ] output metadata強制検証
- [ ] strict violation 0
- [ ] retainPrevious 0
- [ ] capture parity合格
- [ ] savestate parity合格
- [ ] multi-instance合格
- [ ] Metal OFF build合格
- [ ] Windows／Linux回帰なし

## 完了B: HUD／OSD

- [ ] presenterから`GPU.GetFramebuffers()`を削除
- [ ] `bottomImage` memcpy削除
- [ ] CPU radar合成削除
- [ ] `QImage uiOverlay`通常フレーム生成削除
- [ ] `QPainter`通常フレーム合成削除
- [ ] HUD primitive Metal化
- [ ] glyph atlas
- [ ] OSD Metal化
- [ ] splash低頻度asset uploadへ分離
- [ ] HUD editor parity
- [ ] Retina性能合格

---

# 11. 最終評価

`develop_metal`は、以前の「Metal 3DをCPUへ戻してSoftware 2Dで合成するハイブリッド」から、**非capture通常フレームではMetal 3D＋Metal 2D＋Metal final textureを直接表示する経路**へ実質的に進化している。

特に次は有意な前進である。

- Full-GPU既定化
- segmented 2D production経路
- shared `MTLDevice`
- output lease競合修正
- Compute visible cutover修復
- capture texture配線
- presenter dirty rect化

一方、計画で定義された完全Metalの中核条件はまだ残っている。

```text
Display Capture
SoftRenderer inheritance
CPU BGRA fallback
Compute RasterReference
CPU HUD / OSD
```

よって監査結論は次である。

> **M0～M3を中心とする第一段階の実装としては妥当であり、通常シーンのMetal GPU常駐化は成立している。**  
> **ただし、Display Captureを含む全フレームのGPU常駐化、SoftRenderer撤廃、Compute独立化、HUD／OSD Metal化が未完了であるため、「完全Metal化完了」とは判定できない。**

次の最優先作業は、M4のper-scanline／per-segment Display Captureである。M4を完了させない限り、M5、M6、M7の最終撤廃作業へ安全に進めない。
