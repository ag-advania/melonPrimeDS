# melonPrimeDS `develop` 完全Metal化 詳細修正指示書

**作成日:** 2026-07-16  
**対象リポジトリ:** https://github.com/ag-advania/melonPrimeDS  
**対象ブランチ:** `develop`  
**対象OS:** macOS  
**対象バックエンド:** `Metal` / `Metal Compute Shader`  
**目的:** 通常フレームのDS 2D、DS 3D、Display Capture、最終合成、画面表示、Custom HUD、OSDをMetal中心のGPU常駐経路へ統合し、Software renderer由来のCPUピクセル合成、通常フレームのGPU readback、暗黙のSoftware／Metal Rasterフォールバックを撤廃する。

---

# 0. 本書の結論

現在の`develop`ブランチにあるMetal実装は偽物ではない。Metal API、MSL、Metal render pipeline、Metal compute pipeline、`CAMetalLayer`を実際に使用している。

ただし、通常設定の`Metal`は次のハイブリッド構成である。

```text
Metal 3D
→ native 256x192へresolve
→ CPUへreadback
→ Software 2D compositor
→ CPU BGRA完成画面
→ Metal textureへ再アップロード
→ 高解像度Metal 3Dを部分差し替え
→ Metal presenter
```

また、`Metal Compute Shader`は本物のcompute kernelを実行するが、通常状態では`CpuReadbackRequired`によりcompute final textureが可視経路へ採用されず、`MetalRenderer3D RasterReference`へ戻る場合がある。

したがって「完全Metal化」は、単にMetalコードを増やす作業ではない。次の4点を同時に成立させる必要がある。

1. DS 2Dを常時Metalで描画する。
2. DS 3D textureをCPUへ戻さず、そのままMetal 2D compositorへ渡す。
3. Display CaptureをGPU常駐で循環させる。
4. presenterがrenderer所有のMetal textureだけを表示し、通常フレームでCPU BGRAへ戻らない。

最終完了条件は次である。

> Metal選択中の通常フレームでは、`SoftRenderer::DrawScanline`、`SoftRenderer::DrawSprites`、`SoftRenderer::VBlank`、`SoftRenderer::GetFramebuffers`、3D color targetの同期readback、CPU完成画面の`replaceRegion`、QImageによる全画面HUD合成を実行しない。

---

# 1. 完全Metalの定義

本書では「完全Metal」を2段階で定義する。

## 1.1 完了A: エミュレーターGPU経路の完全Metal化

次をすべてMetalで処理する。

- DS 3D rasterization
- DS 3D texture decode／texture cache
- depth／stencil／polygon attribute
- translucent polygon／shadow／fog／edge marking
- DS 2D BG
- DS 2D OBJ／sprite
- window／mosaic／priority／blend
- 3D layerと2D layerの合成
- master brightness
- screen swap
- display capture
- internal resolution scaling
- final two-screen texture array
- `CAMetalLayer`へのpresent

この段階では、デバッグ用スクリーンショット、savestate、CPUによるVRAM参照など、明示的にCPUデータが必要な操作で限定的readbackを許可する。

## 1.2 完了B: 通常フレームの厳密なend-to-end Metal化

完了Aに加え、次を通常フレームのCPU画像合成から外す。

- Custom HUD
- HUD editor overlay
- OSD
- splash画面
- UI icon／text overlay

文字glyph atlasの初回生成や設定変更時の更新はCPUで行ってよい。ただし毎フレームの全画面`QImage`生成、`QPainter`合成、全画面texture uploadは禁止する。

## 1.3 完全Metalに含めないもの

次はCPU処理のままでよい。

- ARM7／ARM9エミュレーション
- GPU register解析
- polygon list構築
- 2D scanline state snapshot
- command encoding
- ROM patch
- input
- audio
- Qt設定画面
- savestate serialization
- 必要時の限定readback

「完全Metal」はアプリ全体をGPUプログラムにするという意味ではなく、通常フレームの画像生成と表示をGPU常駐Metal経路で完結させるという意味である。

---

# 2. 現状監査

## 2.1 ビルドゲート

対象:

- `src/frontend/qt_sdl/CMakeLists.txt`

現状:

- macOSでは`MELONPRIME_ENABLE_METAL`が既定ON。
- `MELONPRIME_METAL_ACTIVE`内でMetal関連`.mm`を追加。
- `Metal.framework`と`QuartzCore.framework`をリンク。
- `GPU_Metal.mm`、`GPU2D_Metal.mm`、`GPU3D_Metal.mm`、`GPU3D_MetalCompute.mm`をビルド。
- `MelonPrimeScreenMetal.mm`をビルド。

判定:

- ビルド上は真正なMetal backend。
- 完全Metal化後も、この完全ビルドゲートを維持する。
- Metal専用型、Objective-C型、framework依存を非Appleビルドへ漏らさない。

## 2.2 renderer factory

対象:

- `src/frontend/qt_sdl/EmuThread.cpp`
- `src/frontend/qt_sdl/EmuInstance.h`

現状:

- `renderer3D_Metal`は`MetalRenderer(*nds, false)`を生成。
- `renderer3D_MetalCompute`は`MetalRenderer(*nds, true)`を生成。
- renderer選択はOpenGL contextの有無と分離済み。

判定:

- 選択経路自体は正常。
- 問題は生成後の`MetalRenderer`が`SoftRenderer`を継承し、通常フレームでSoftware経路を利用する点。

## 2.3 Metal renderer本体

対象:

- `src/GPU_Metal.h`
- `src/GPU_Metal.mm`
- `src/GPU_MetalFullGpuMethods.inc`
- `src/GPU_MetalCaptureMethods.inc`

現状:

- `MetalRenderer : public SoftRenderer`。
- `MetalRenderer2D`を2基保有。
- Full GPU経路は存在する。
- Full GPU要求は`MELONPRIME_METAL_FULL_GPU=1`に依存。
- Full GPU不成立時はSoftware 2D compositorへ戻る。
- CPU完成画面をMetal textureへアップロードし、高解像度3Dを合成する互換経路がある。

主な問題:

1. 完全GPU経路が既定無効。
2. フレーム単位でSoftware経路へ戻れる。
3. `SoftRenderer`継承により依存が構造化されている。
4. Display Capture feedbackでFull GPUが拒否される。
5. CPU完成画面が正規出力として残っている。

## 2.4 Metal 3D raster

対象:

- `src/GPU3D_Metal.h`
- `src/GPU3D_Metal.mm`
- `src/GPU3D_TexcacheMetal.h`

現状:

- `MTLDevice`、command queue、render pipeline、depth／stencil、texture arrayを使用。
- DS polygonをMetal draw callへ変換。
- 高解像度ColorTargetを生成。
- native resolve textureを生成。
- 通常Software 2Dへ渡すため、native targetをCPU line bufferへreadback可能。
- `SoftRenderer3D Delegate`を診断／fallback用に保持。

主な問題:

1. `ReadbackNativeColorTargetToLineBuffer()`が通常経路に入る。
2. `GetLine()`契約がSoftware 2D compositorを前提としている。
3. `Delegate`がproduction経路へ入り得る。
4. 3D output contractがtexture中心ではなく、scanline互換を残している。

## 2.5 Metal Compute

対象:

- `src/GPU3D_MetalCompute.h`
- `src/GPU3D_MetalCompute.mm`

現状:

- span setup、binning、sorting、texture raster、depth blend、final passをMetal computeで実行。
- compute final textureを持つ。
- `RasterReference`として`MetalRenderer3D`を常時保有。
- `!CpuReadbackRequired`等の条件を満たした場合だけcompute final textureを可視化。
- 条件不成立時は`RasterReference.RenderFrame()`。
- `GetLine()`は`RasterReference.GetLine()`。

主な問題:

1. Compute選択でも通常フレームがMetal rasterになる場合がある。
2. Compute backendが独立したproduction output contractを持っていない。
3. full GPU 2D不成立がcompute可視化を阻害する。
4. UI名称と実際の可視経路が一致しない。

## 2.6 Metal 2D

対象:

- `src/GPU2D_Metal.h`
- `src/GPU2D_Metal.mm`
- `src/GPU2D_MetalFullGpuMethods.inc`

現状:

- BG、OBJ、window、mosaic、blend、3D layer、capture textureを扱うMSLが存在。
- scanline stateをsnapshotし、状態変化区間ごとにsegmented renderingする。
- BG texture、OBJ texture、depth texture、output textureをMetal上に保持。
- 実command buffer、render pass、draw callを発行。

判定:

- 完全Metal化の基礎は既にある。
- 新規rendererをゼロから作るのではなく、このsegmented 2D経路をproduction既定へ昇格させる。

## 2.7 presenter

対象:

- `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`

現状:

- `CAMetalLayer`へ真正なMetal描画を行う。
- `RendererOutputKind::MetalTexture`を直接表示できる。
- `RendererOutputKind::CpuBgra`も受け取り、CPU完成画面をMetal textureへアップロードできる。
- Custom HUD／OSDは`QImage`と`QPainter`で全画面overlayを作り、毎フレームtextureへアップロード。

主な問題:

1. presenterがCPU outputを正規入力として許可している。
2. Metal選択中でもCPU BGRAを黙って表示できる。
3. HUD／OSDが毎フレームCPU画像合成。
4. rendererとpresenterが別々に`MTLCreateSystemDefaultDevice()`を呼ぶ。

## 2.8 既定設定

対象:

- `src/frontend/qt_sdl/Config.cpp`
- `src/frontend/qt_sdl/VideoSettingsDialog.cpp`

現状:

- MelonPrimeDSの既定rendererはOpenGL。
- Metal Compute tooltipはMetal raster fallbackを明記。

判定:

- 完全Metal化が完了するまではOpenGL既定を維持する。
- 完了Aの受け入れ後、macOSのみMetalを既定へ変更する。
- Metal Computeを既定にするのは完了Bとは別の判断とする。

---

# 3. 設計原則

## 3.1 暗黙fallback禁止

Metal選択中に次へ黙って戻ってはならない。

- Software renderer
- Software 2D compositor
- CPU BGRA presenter input
- Metal ComputeからMetal Raster
- 前フレームの無期限再表示

productionでMetal処理が成立しない場合は次のいずれかにする。

1. renderer初期化時に選択を拒否し、理由をUIへ表示する。
2. 実行中GPU failureならエミュレーションを一時停止し、明示的エラーをOSD／ログへ出す。
3. ユーザーが設定画面で別rendererへ変更する。

デバッグビルドだけは明示的なfallback optionを許可する。

## 3.2 texture ownershipを最上位契約にする

Metal backendの正規出力は、CPU pointerではなく次を持つMetal texture leaseとする。

- `id<MTLTexture>`相当のopaque handle
- 2-layer texture array
- width／height
- scale
- frame serial
- producer generation
- completion synchronization
- release callback

`RendererOutputKind::CpuBgra`はSoftware backend専用とし、Metal backendから返してはならない。

## 3.3 正常フレームではreadbackしない

Metal backendでreadbackを許可するのは次だけとする。

- savestate前にCPU VRAM整合が必要なcapture block
- ARM CPUがcapture-backed VRAMを読むとき
- screenshot／video capture機能がCPU画像を要求するとき
- renderer切替
- デバッグ差分検証
- 明示的診断コマンド

通常の60FPS／90FPSフレームループでは禁止する。

## 3.4 2Dと3Dは同一frame serialを共有する

次の不一致を禁止する。

- 2D frame N + 3D frame N+1
- screen swap frame N + brightness frame N+1
- capture texture frame N-1 + VRAM state frame N
- presenterが解放済みslotを参照

frame contextに最低限次を保持する。

- frame serial
- scale
- Engine A／B screen assignment
- 192本のscreen swap state
- 192本のmaster brightness A／B
- 2D segment list
- 3D output texture
- capture generation
- output slot generation

## 3.5 productionと診断を分離する

現在の多数の環境変数を整理する。

production機能の有効化を環境変数へ依存させない。

残してよい環境変数:

- `MELONPRIME_METAL_PERF=1`
- `MELONPRIME_METAL_DIAG=1`
- `MELONPRIME_METAL_ASSERT_GPU_ONLY=1`
- `MELONPRIME_METAL_ALLOW_DEBUG_FALLBACK=1`

削除または互換読み込みだけにする候補:

- `MELONPRIME_METAL_FULL_GPU`
- `MELONPRIME_METAL_GETLINE_SOURCE`
- `MELONPRIME_METAL_COMPUTE_VISIBLE`
- `MELONPRIME_METAL_COMPUTE_DISABLE_VISIBLE`
- `MELONPRIME_METAL_ALLOW_SOFTWARE_FALLBACK`
- `MELONPRIME_METAL_HIRES_REPLACEMENT`

---

# 4. 目標アーキテクチャ

```text
GPU register／VRAM state
        │
        ├─ Metal 3D Raster または Metal 3D Compute
        │      └─ GPU resident 3D color/depth/attr texture
        │
        ├─ Metal Display Capture
        │      └─ GPU resident capture block texture generations
        │
        └─ Metal 2D Segmented Renderer A/B
               ├─ BG
               ├─ OBJ
               ├─ Window
               ├─ Mosaic
               ├─ Blend
               ├─ 3D layer
               └─ Capture-backed layers
                         │
                         ▼
                Engine A/B output textures
                         │
                         ▼
                Metal final two-screen composer
                         │
                         ▼
                triple-buffered texture2d_array
                         │
                         ▼
                Metal HUD／OSD compositor
                         │
                         ▼
                    CAMetalLayer
```

通常フレームではCPU BGRA完成画面を生成しない。

---

# 5. 実装フェーズ

# Phase M0: 基準固定と観測機構

## 目的

完全Metal化前の画質、frame ordering、CPU readback、fallback回数を数値化する。

## 対象

- `src/GPU_Metal.mm`
- `src/GPU3D_Metal.mm`
- `src/GPU3D_MetalCompute.mm`
- `src/GPU2D_Metal.mm`
- `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`
- `src/frontend/qt_sdl/MelonPrimeInstanceDiagnostics.cpp`

## 修正内容

1. Metal frame統計構造体を追加する。
2. 次をフレーム単位と600フレーム集計で記録する。
   - Metal 3D draw数
   - Metal compute dispatch数
   - 2D segment数
   - capture pass数
   - CPU readback bytes
   - CPU composite frame数
   - CPU BGRA presenter frame数
   - Metal raster fallback数
   - Software fallback数
   - retained previous frame数
   - command buffer failure数
3. `MELONPRIME_METAL_ASSERT_GPU_ONLY=1`を追加する。
4. strict assertion時に次を検出したら即座に明示エラーにする。
   - Metal選択中の`RendererOutputKind::CpuBgra`
   - `SoftRenderer::DrawScanline`到達
   - 通常フレームの3D readback
   - CPU composite upload
   - Compute選択中の`RasterReference.RenderFrame()`
5. 画質比較用に、Software／OpenGL／Metalのframe checksum取得手段を追加する。ただし通常ビルドでは無効にする。

## 完了条件

- どの経路が実際に可視化されたかログだけで判定できる。
- fallbackを「推測」ではなくカウンターで確認できる。
- strict assertionなしでは現状挙動を変えない。

---

# Phase M1: Metal output contractの固定

## 目的

Metal backendの正規出力をMetal texture leaseへ一本化する。

## 対象

- `src/GPU.h`
- `src/GPU.cpp`
- `src/GPU_Metal.h`
- `src/GPU_Metal.mm`
- `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`

## 修正内容

### 1. `RendererOutput`を拡張

Metal texture outputへ次のmetadataを追加する。

- width
- height
- array length
- scale
- pixel format identifier
- frame serial
- generation
- completion serialまたはshared event value

`void* Top`だけへ意味を詰め込まない。Metal専用metadataは`MELONPRIME_ENABLE_METAL`内へ置く。

### 2. leaseを唯一の取得APIにする

Metal presenterは`GetRendererOutput()`を使わず、必ず`AcquireRendererOutputLease()`を使う。

lease条件:

- presenterがcommand bufferをcommitするまでslotを再利用しない。
- presenter command completionでreleaseする。
- renderer破棄時はin-flight leaseが0になるまで安全に待つ。
- stale generationは表示しない。

### 3. Metal backendからCPU outputを返さない

`MetalRenderer::AcquireOutputLease()`と`GetOutput()`の末尾にあるSoftware output fallbackをproductionから削除する。

初期化未完了時の処理:

- `None`を返す。
- 画面を黒でclearする。
- 1回だけ明示エラーを表示する。
- Software outputを返さない。

### 4. presenterの入力を厳格化

Metal renderer選択中:

- `MetalTexture`以外を受け取ったら描画しない。
- `CpuBgra`を自動アップロードしない。
- ログへrenderer名、output kind、frame serial、failure reasonを出す。

Software renderer + Metal presenterという診断構成を残す場合は、明示的なdeveloper option下だけにする。

## 完了条件

- Metal renderer選択中のvisible sourceは常に`MetalTexture`または`None`。
- CPU BGRAはMetal rendererの正常出力として存在しない。
- output slot lifetimeにraceがない。

---

# Phase M2: 共有Metal contextと同期モデル

## 目的

renderer、2D、3D、capture、presenterが同一deviceと明示的同期契約を共有する。

## 新規候補ファイル

- `src/MetalContext.h`
- `src/MetalContext.mm`
- `src/MetalFrameContext.h`

ファイル名はプロジェクト規則に合わせて変更してよい。

## 修正対象

- `src/frontend/qt_sdl/EmuInstance.cpp`
- `src/frontend/qt_sdl/EmuThread.cpp`
- `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`
- `src/GPU_Metal.h`
- `src/GPU_Metal.mm`
- `src/GPU3D_Metal.mm`
- `src/GPU3D_MetalCompute.mm`
- `src/GPU2D_Metal.mm`

## 修正内容

1. `MTLDevice`を1回だけ作成する共有contextを導入する。
2. 次のqueueをcontextから作る。
   - renderer queue
   - presenter queue
   - readback／utility queue、必要な場合のみ
3. rendererとpresenterが別deviceになる可能性を構造上排除する。
4. producer／presenter同期は次のどちらかへ統一する。
   - 同一queueによるsubmission ordering
   - shared event valueによるcross-queue同期
5. frame slotは最低3個持つ。
6. 各slotへ次を持たせる。
   - final texture array
   - frame serial
   - generation
   - producer completion value
   - presenter reference count
   - in-flight state
7. `waitUntilCompleted`を通常フレームから排除する。
8. shutdown、fullscreen、window handle再生成、renderer切替時のdrain処理を定義する。

## 注意

- Objective-C型を通常C++ headerへ直接公開しない。
- opaque handleまたはPImplを使う。
- non-Appleビルドでheaderが解釈できることを確認する。

## 完了条件

- rendererとpresenterのdevice mismatch分岐が通常到達不能。
- 通常フレームでCPU waitなし。
- slot再利用とpresenter samplingが競合しない。

---

# Phase M3: Metal 2D segmented rendererのproduction昇格

## 目的

Software 2D compositorを通常フレームから外す。

## 対象

- `src/GPU2D_Metal.h`
- `src/GPU2D_Metal.mm`
- `src/GPU2D_MetalFullGpuMethods.inc`
- `src/GPU_Metal.mm`
- `src/GPU_MetalFullGpuMethods.inc`

## 修正内容

### 1. 環境変数ゲートを撤廃

`MELONPRIME_METAL_FULL_GPU=1`をproduction要件から削除する。

Metal renderer選択時は原則として毎フレームFull GPU 2Dを要求する。

### 2. Full GPU適格判定をcapability判定へ変更

現状の「問題があるフレームはSoftwareへ戻す」設計をやめる。

次を初期化時に判定する。

- 2D pipelines作成成功
- scanline snapshot buffers作成成功
- capture textures作成成功
- output textures作成成功
- 3D textureとのformat／device互換

初期化で成立しない場合はMetal renderer選択自体を拒否する。

フレーム中の状態差はMetal path内で処理し、Softwareへ戻す理由にしない。

### 3. scanline segmented pathを常時使用

各scanlineについて次をsnapshotする。

- DispCnt
- BGCnt
- affine parameters
- window state
- BlendCnt／EVA／EVB／EVY
- mosaic state
- LayerEnable
- master brightness
- screen swap
- OAM／OBJ state generation
- capture mapping generation

隣接scanlineで状態が同じ場合はsegmentとしてまとめる。

### 4. BG実装を完成させる

最低限次をSoftware/OpenGLと一致させる。

- text BG 16色
- text BG 256色
- affine BG
- extended affine
- bitmap 256色
- direct color bitmap
- extended palette
- wrap／clamp
- horizontal／vertical flip
- scroll
- per-scanline affine
- BG priority
- transparent color

### 5. OBJ実装を完成させる

- regular OBJ
- affine OBJ
- double-size affine
- bitmap OBJ
- OBJ window
- semi-transparent OBJ
- mosaic
- priority
- extended palette
- 1D／2D mapping
- capture-backed texture
- per-scanline OAM state

### 6. compositorを完成させる

- BG0～BG3
- OBJ
- 3D layer
- window 0／1／OBJ window／outside
- first／second target
- alpha blend
- brightness up／down
- forced blank
- display modes
- master brightness
- screen swap

### 7. `SoftRenderer`呼び出しを停止

Full GPU frameで次を呼ばない。

- `SoftRenderer::DrawScanline`
- `SoftRenderer::DrawSprites`
- `SoftRenderer::VBlank`
- `SoftRenderer::VBlankEnd`

最終的にはMetal選択中の全正常フレームで呼ばない。

## 完了条件

- MPHの試合中HUD、メニュー、マップ、ポーズ、暗転、画面切替でSoftware 2Dと視覚一致。
- mixed 3D + 2D overlayがMetal 2Dで成立。
- 2D frame rejection数が0。
- CPU composite frame数が0。

---

# Phase M4: Display Captureの完全GPU常駐化

## 目的

Display Capture feedbackを理由にFull GPU経路を拒否しない。

## 対象

- `src/GPU.h`
- `src/GPU.cpp`
- `src/GPU_MetalCaptureMethods.inc`
- `src/GPU_Metal.mm`
- `src/GPU2D_Metal.mm`
- `src/GPU3D_Metal.mm`
- `src/GPU3D_MetalCompute.mm`
- `src/GPU3D_TexcacheMetal.h`

## 設計

capture可能なVRAM A～Dの各blockについて、次の状態を持つ。

- CPU VRAM generation
- GPU capture generation
- CPU dirty
- GPU dirty
- last writer
- pending command serial
- texture slice／offset

## 修正内容

1. display captureの出力をMetal textureへ直接書く。
2. 128幅／256幅captureを正しいformatとstrideで保持する。
3. 2D BG／OBJ／3D texture cacheがcapture-backed VRAMを参照する場合、CPU VRAMではなくGPU capture textureを直接sampleする。
4. capture後の同一フレーム参照をcommand orderingで保証する。
5. CPUがcapture blockを読む場合だけ対象blockをreadbackする。
6. CPUがcapture blockへ書いた場合は、そのblockだけMetal textureへ再uploadする。
7. savestate時はdirtyなcapture blockだけ同期する。
8. renderer切替時はGPU dirty blockをCPUへ同期してから破棄する。
9. `BlockedByCaptureFeedback`を通常制御から削除する。
10. capture unsupportedによる`RetainPrevious`を撤廃する。

## 検証項目

- capture source A/B/3D/VRAM
- 128幅／256幅
- source offset
- destination offset
- blend係数
- repeated capture
- capture textureを同一フレームでBGへ使用
- capture textureを3D textureとして使用
- savestate直後とload後

## 完了条件

- capture有効フレームでもFull GPUが継続する。
- captureを理由とするCPU fallbackが0。
- CPU readbackは実CPU read要求時だけ。

---

# Phase M5: Metal Raster 3DのGPU-only化

## 目的

安定版`Metal`rendererを最初の完全Metal production backendとして完成させる。

## 対象

- `src/GPU3D_Metal.h`
- `src/GPU3D_Metal.mm`
- `src/GPU_Metal.h`
- `src/GPU_Metal.mm`

## 修正内容

1. `CpuReadbackRequired`の既定をfalseにする。
2. Metal 2Dが有効な正常フレームでは`ReadbackNativeColorTargetToLineBuffer()`を呼ばない。
3. 2D compositorへ`ColorTarget`またはfinal 3D textureを直接渡す。
4. native resolve textureは次の用途に限定する。
   - capture仕様上native resolutionが必要
   - screenshot／diagnostic
   - CPU VRAM同期
5. `GetLine()`をMetal production経路から使用しない。
6. strict modeで`GetLine()`が呼ばれたらassertする。
7. `SoftRenderer3D Delegate`を次のいずれかへ変更する。
   - developer diagnosticsビルドだけにcompile
   - lazy生成し、明示的diff modeだけで使用
8. 通常MetalでSoftware delegate frame数を0にする。
9. identical-frame reuseはGPU textureとframe metadataを保持する方式で継続する。
10. high-resolution targetと2D output scaleのauthorityを1か所へ統一する。

## 画質一致必須項目

- opaque polygon
- translucent polygon
- alpha test
- texture formats
- texture repeat／mirror／clamp
- toon／highlight shading
- Z-buffer／W-buffer
- shadow polygon
- polygon ID
- edge marking
- fog color／alpha
- clear color／clear image
- antialiasing関連挙動
- Better Polygons
- high-resolution coordinates

## 完了条件

- `renderer3D_Metal`で通常フレームreadback 0 bytes。
- `GetLine()`呼び出し0。
- Software delegate 0 frames。
- 1x～16xでtarget sizeとvisible sizeが一致。
- Metal 2Dが3D textureを直接sampleする。

---

# Phase M6: `MetalRenderer`の`SoftRenderer`継承撤廃

## 目的

完全Metalを構造上保証し、Software経路への偶発回帰を防ぐ。

## 対象

- `src/GPU_Metal.h`
- `src/GPU_Metal.mm`
- `src/GPU_Soft.h`
- 必要に応じて`src/GPU.h`／renderer基底

## 修正内容

1. `MetalRenderer : public SoftRenderer`をやめる。
2. `MetalRenderer : public Renderer`へ変更する。
3. Metal 2D／3Dに必要な共通状態だけをrenderer基底またはMetal専用classへ移す。
4. `SoftRenderer`内部bufferへ依存している箇所を全列挙し、Metal texture／Metal stateへ置換する。
5. `GetFramebuffers()`はMetal rendererでfalseを返す。
6. `RendererOutput`はMetal textureのみ。
7. savestate hook、capture allocation、VRAM syncのvirtual contractをMetal側で独立実装する。
8. Software fallbackを必要とする診断は別objectとして明示生成する。

## 注意

このフェーズはM3～M5の後に行う。先に継承だけ外すと、既存互換経路を大量に壊し、原因切り分けが困難になる。

## 完了条件

- Metal renderer class hierarchyに`SoftRenderer`が存在しない。
- Metal production binaryでSoftware compositor symbolへの通常呼び出しがない。
- strict GPU-only testが常時通る。

---

# Phase M7: Metal Computeの独立production化

## 目的

`Metal Compute Shader`選択時の可視3Dを常にcompute final textureにする。

## 対象

- `src/GPU3D_MetalCompute.h`
- `src/GPU3D_MetalCompute.mm`
- `src/GPU_Metal.mm`
- `src/frontend/qt_sdl/VideoSettingsDialog.cpp`

## 修正内容

1. M3～M6の完全Metal 2D output contractをComputeにも適用する。
2. `CpuReadbackRequired`をCompute可視化条件から除外できる構造にする。
3. Compute final textureをMetal 2Dへ直接渡す。
4. `GetLine()`を正常経路から廃止する。
5. `RasterReference`をproduction fallbackとして使用しない。
6. `RasterReference`を残す場合は診断比較専用とし、developer buildまたは明示optionでのみ生成する。
7. `SubmitRealFrameSpanBin()`失敗時にそのフレームだけRasterへ戻らない。
8. frame slot不足は古い完成textureを安全に保持し、次フレームで再試行する。ただし恒常的不足は明示エラーとする。
9. GPU command failure時はCompute rendererを停止し、ユーザーへrenderer変更を促す明示エラーを表示する。
10. UI tooltipから「Metal raster remains fallback」を削除するのは、全受け入れ試験完了後に行う。

## Compute parity項目

- polygon setup
- span interpolation
- tile binning
- texture variants
- capture-backed texture
- depth test
- W-buffer
- alpha test
- translucent ordering
- destination alpha
- shadow
- polygon ID
- edge marking
- fog
- final resolve
- internal resolution
- identical-frame reuse

## 完了条件

- Compute選択中、最初の有効フレームから`ComputeFinalTexture`が可視source。
- `RasterReference.RenderFrame()` 0回。
- `GetLine()` 0回。
- compute fallback counter 0。
- UI名称と実際の経路が一致。

---

# Phase M8: presenterのMetal texture-only化

## 目的

Metal rendererの最終textureをCPUを介さず`CAMetalLayer`へ表示する。

## 対象

- `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`
- `src/frontend/qt_sdl/MelonPrimeScreenMetal.h`
- `src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp`

## 修正内容

1. Metal renderer選択中の`CpuBgra`処理を削除する。
2. `screenTex[0]`へのCPU top／bottom uploadをSoftware renderer診断時だけに限定する。
3. Metal final texture arrayを直接sampleする。
4. renderer output metadataでwidth、height、scale、arrayLengthを検証する。
5. presenter独自の`MTLDevice`作成を共有contextへ置換する。
6. producer completionを待たず、GPU eventで依存を設定する。
7. presenter command completionでoutput leaseを解放する。
8. `nextDrawable()`が取れない場合、renderer slotを即座に解放する。
9. fullscreen／Retina scale変更時に`CAMetalLayer`だけを更新し、renderer textureを不必要に再生成しない。
10. VSync、Fast Forward、Slow Motionの既存制御を維持する。

## 完了条件

- Metal rendererでCPU texture upload 0 bytes／frame。
- visible sourceログが常に`MetalFinalTexture`。
- device mismatch 0。
- presenter backlogでエミュレーションthreadが停止しない。

---

# Phase M9: Custom HUD／OSDのMetal化

## 目的

毎フレームの`QImage`／`QPainter`全画面overlayを撤廃する。

## 対象

- `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`
- `src/frontend/qt_sdl/MelonPrimeHudRender.cpp`
- `src/frontend/qt_sdl/MelonPrimeHudRender.h`
- OSD描画関連
- font解決関連

## 新規候補

- `MelonPrimeMetalUiRenderer.h`
- `MelonPrimeMetalUiRenderer.mm`
- `MelonPrimeMetalGlyphAtlas.h`
- `MelonPrimeMetalGlyphAtlas.mm`

## 設計

### 1. 描画command方式へ変更

既存HUDロジックは描画結果ではなく、次のcommand listを生成する。

- text
- rectangle
- line
- image
- clipped region
- opacity
- transform

Qt／OpenGL／Metalが同じcommand listを各backendで描画できる形が望ましい。

### 2. glyph atlas

- CoreText等でglyph bitmapをatlasへ追加する。
- glyph追加は文字列変更時だけ。
- 毎フレーム全画面bitmapを作らない。
- 日本語、韓国語、中国語、アラビア語等の既存言語を壊さない。
- atlasは複数page対応にする。
- font変更、pixel size変更、DPI変更でgenerationを更新する。

### 3. Metal UI pipeline

- premultiplied alpha
- text atlas sampling
- solid color primitive
- image primitive
- scissor
- logical coordinateからdrawable coordinateへの変換
- Retina scale
- screen layout matrix

### 4. HUD editor

- selection rectangle
- handles
- guide
- label
- mouse hit testはCPU
- 描画はMetal

### 5. OSD

- OSD itemをQImageではなくtext／icon commandとして保持する。
- duration、fade、stackingをMetal UI rendererへ渡す。

### 6. splash

- static imageを一度だけtextureへuploadする。
- textはglyph atlasで描画する。

## 完了条件

- 通常フレームの全画面`QImage`生成0。
- HUD／OSDの全画面upload 0 bytes。
- glyph追加時以外のCPU pixel generation 0。
- Custom HUD、編集モード、OSD、splashが既存表示と一致。

---

# Phase M10: 既定化、UI整理、設定migration

## 目的

完全MetalをmacOSのproduction既定へ昇格する。

## 対象

- `src/frontend/qt_sdl/Config.cpp`
- `src/frontend/qt_sdl/VideoSettingsDialog.cpp`
- `src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp`
- localization catalog
- README／macOS build docs

## 修正内容

1. macOS + Metal buildのみ、初回configの既定rendererを`renderer3D_Metal`へする。
2. `Screen.UseGL`既定をmacOS Metal時はfalseにする。
3. 既存ユーザーのrenderer設定は勝手に変更しない。
4. 古いMetal環境変数をconfig migration時に無視する。
5. UI表記を更新する。
   - `Metal`: Production native Metal renderer
   - `Metal Compute Shader`: Production native Metal compute renderer、parity完了後
6. feature probe失敗時はボタンをdisableし、理由をtooltipへ表示する。
7. renderer変更時に必要なbackend再生成を正しく行う。
8. OpenGL、Softwareは手動選択肢として残す。
9. macOS以外の既定値を変更しない。

## 推奨順序

- 最初は`Metal`だけをproduction表記へ変更。
- ComputeはM7の全試験完了までExperimental表記を維持。
- Computeを既定rendererにはしない。性能と互換性の実測後に別判断する。

## 完了条件

- 新規macOS configでMetalが選択される。
- 既存configは尊重される。
- OpenGL contextがMetal時に作られない。
- UI説明と実装が一致する。

---

# Phase M11: shader資産とビルドの整理

## 目的

巨大な埋め込みMSL文字列を保守可能な構成へ移す。

## 推奨構成

```text
src/metal/
  Metal3DClear.metal
  Metal3DRaster.metal
  Metal3DFinal.metal
  Metal3DCompute.metal
  Metal2DLayer.metal
  Metal2DObject.metal
  Metal2DComposite.metal
  MetalCapture.metal
  MetalPresent.metal
  MetalUi.metal
```

## 修正内容

1. MSLを役割別ファイルへ分離する。
2. CMakeでmacOS時だけshaderをビルドする。
3. Releaseでは事前compile済みlibraryをbundleへ含める。
4. Debugではruntime source compileを選択可能にする。
5. pipeline名とentry pointを一元管理する。
6. shader struct layoutへ`static_assert`を付ける。
7. shader library versionをログへ出す。
8. shader cache破損時の明示エラーを実装する。

## 注意

このフェーズは画質parity後に行う。shader移動と描画ロジック修正を同じcommitに混ぜない。

---

# 6. ファイル別修正指示

| ファイル | 必須修正 |
|---|---|
| `src/GPU.h` | Metal output metadata、lease contract、capability、capture ownershipを追加 |
| `src/GPU.cpp` | Metal output取得、capture block同期、renderer切替時drainを修正 |
| `src/GPU_Metal.h` | `SoftRenderer`継承を段階的に撤廃、Metal frame stateを所有 |
| `src/GPU_Metal.mm` | CPU composite path削除、Metal final textureを常時publish |
| `src/GPU_MetalFullGpuMethods.inc` | env gate削除、production既定、frame rejection撤廃 |
| `src/GPU_MetalCaptureMethods.inc` | GPU capture generation、限定readback、same-frame feedback対応 |
| `src/GPU2D_Metal.h` | production capability、segment contract、capture texture contract |
| `src/GPU2D_Metal.mm` | BG／OBJ／scanline snapshotのparity完成 |
| `src/GPU2D_MetalFullGpuMethods.inc` | segmented 2Dを常時submit、全compositor機能完成 |
| `src/GPU3D_Metal.h` | scanline output依存を削除、texture output中心へ変更 |
| `src/GPU3D_Metal.mm` | 通常readback削除、Delegateを診断限定、3D texture直接連携 |
| `src/GPU3D_MetalCompute.h` | RasterReferenceをproduction契約から外す |
| `src/GPU3D_MetalCompute.mm` | Compute final texture常時可視化、fallback禁止 |
| `src/GPU3D_TexcacheMetal.h` | capture-backed texture、generation同期、GPU常駐更新 |
| `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm` | Metal texture-only表示、共有device、Metal UI renderer |
| `src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp` | strict renderer選択、暗黙fallback禁止 |
| `src/frontend/qt_sdl/EmuThread.cpp` | Metal context受け渡し、renderer failureの明示処理 |
| `src/frontend/qt_sdl/EmuInstance.*` | Metal shared context所有、window lifecycle同期 |
| `src/frontend/qt_sdl/VideoSettingsDialog.cpp` | 実際のbackend状態に合わせた説明／enable条件 |
| `src/frontend/qt_sdl/Config.cpp` | 完了後にmacOS初回既定をMetalへ変更 |
| `src/frontend/qt_sdl/CMakeLists.txt` | 新規Metal context／UI／shader資産を完全ゲート内へ追加 |

---

# 7. 禁止する実装

次の実装は採用しない。

1. 問題フレームだけSoftwareへ戻す。
2. Compute失敗時に毎フレームMetal Rasterを呼ぶ。
3. capture有効時に前フレームを表示し続ける。
4. CPU BGRAをMetal textureへアップロードして「完全Metal」と判定する。
5. Full GPUを環境変数がある場合だけ有効にする。
6. 1xだけGPU、2x以上だけCPU等、scaleで責務を分ける。
7. HUD／OSDのQImage全画面uploadを残したまま厳密な完全Metalと表記する。
8. `waitUntilCompleted`を通常フレームへ追加する。
9. device mismatchをCPU fallbackで隠す。
10. Metal failureをログなしでSoftwareへ正規化する。
11. `#ifdef MELONPRIME_DS`なしでMetal専用変更をmelonDS共通経路へ漏らす。
12. 2D、capture、compute、presenterを1つの巨大commitで同時変更する。

---

# 8. 受け入れ試験

## 8.1 静的検査

Metal production経路について次を確認する。

- `MetalRenderer`が`SoftRenderer`を継承していない。
- Metal normal frameから`SoftRenderer::DrawScanline`を呼ばない。
- Metal normal frameから`SoftRenderer::DrawSprites`を呼ばない。
- Metal normal frameから`SoftRenderer::VBlank`を呼ばない。
- Metal normal frameから`SoftRenderer::GetFramebuffers`を呼ばない。
- Metal presenterが`CpuBgra`を受理しない。
- Compute normal frameから`RasterReference.RenderFrame`を呼ばない。
- `waitUntilCompleted`はdiagnostic／explicit readbackだけ。
- Metal source、definition、frameworkが`MELONPRIME_METAL_ACTIVE`内にある。

## 8.2 起動ログ

Metal Raster選択時に最低限次を出す。

```text
backend=MetalRaster
fullGpu2D=1
captureGpuResident=1
cpuComposite=0
cpuReadback=0
visibleSource=MetalFinalTexture
strictGpuOnly=1
```

Metal Compute選択時:

```text
backend=MetalCompute
computeVisible=1
rasterReference=0
fullGpu2D=1
cpuComposite=0
cpuReadback=0
visibleSource=MetalComputeFinalTexture
```

600フレーム集計で次が0であること。

- Software fallback
- CPU composite
- CPU presenter upload
- raster fallback、Compute選択時
- normal-frame readback
- stale texture presentation
- device mismatch
- command failure

## 8.3 renderer matrix

| renderer | 期待結果 |
|---|---|
| Software | 従来通りCPU BGRA |
| OpenGL | 従来通りOpenGL texture |
| OpenGL Compute | 従来通り、macOS制限を維持 |
| Metal | 完全GPU常駐Metal Raster |
| Metal Compute | 完全GPU常駐Metal Compute |

## 8.4 scale matrix

次をすべて確認する。

- 1x
- 2x
- 3x
- 4x
- 6x
- 8x
- 16x、GPU memory上限により拒否する場合は明示エラー

各scaleで確認:

- target size
- final texture size
- presenter sampling
- HUD座標
- screen layout
- filter
- capture
- screenshot

## 8.5 MPHゲーム状態

最低限次を通す。

1. ROM boot
2. title
3. profile／license
4. main menu
5. Adventure ship
6. stage load
7. normal gameplay
8. mixed 3D + 2D HUD
9. weapon HUD
10. zoom
11. scan visor
12. morph ball
13. map
14. pause
15. death／respawn
16. cutscene
17. screen fade
18. master brightness
19. multiplayer lobby
20. online match
21. screen swap
22. fullscreen切替
23. Retina／non-Retina移動
24. savestate／loadstate
25. renderer再初期化

## 8.6 DS GPU機能

- BG mode 0～6
- text／affine／bitmap
- OBJ全形式
- window
- mosaic
- alpha blend
- brightness
- master brightness
- forced blank
- 3D layer priority
- display capture
- capture feedback
- clear image
- fog
- edge marking
- shadow
- translucent polygon

## 8.7 frame ordering

各完成textureにframe serialを埋め、次を確認する。

- 2D serial = 3D serial
- capture serial <= consumer serial、仕様上必要なordering
- final serial単調増加
- presenterが未完成serialを表示しない
- fullscreen再生成後に古いgenerationを表示しない

## 8.8 性能

比較対象:

- OpenGL 4x
- 現行Metal 4x
- 完全Metal Raster 4x
- 完全Metal Compute 4x

計測:

- emulation frame time
- renderer GPU time
- CPU renderer time
- readback wait
- upload bytes
- presenter time
- CPU usage
- GPU usage
- frame pacing
- audio underrun
- memory

必須結果:

- normal-frame readback wait = 0
- CPU composite time = 0
- CPU full-frame upload bytes = 0
- frame pacingが現行より悪化しない
- audio underrunが増えない

---

# 9. 自動テストとCI

## 9.1 macOS build jobs

最低限次を追加する。

1. Metal ON Release
2. Metal ON Debug
3. Metal OFF
4. Metal FORCE_DISABLE ON
5. Apple Silicon
6. Intel Mac、CI環境が利用可能な場合

## 9.2 compile matrix

- `MELONPRIME_ENABLE_METAL=ON`
- `MELONPRIME_ENABLE_METAL=OFF`
- `MELONPRIME_FORCE_DISABLE_METAL=ON`
- `MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON`

## 9.3 headless／test harness

Metal textureへ既知パターンを描画し、限定readbackで検証するtestを用意する。

- 2D BG sample
- OBJ sample
- window sample
- blend sample
- capture sample
- 3D opaque／translucent
- compute tile sample
- final two-screen composer

限定readbackはtest内だけで許可する。

## 9.4 regression gate

PRをmerge不可にする条件:

- strict GPU-only test失敗
- Software fallback count > 0
- CPU composite count > 0
- Compute raster fallback count > 0
- frame checksum差分が許容外
- Metal OFF build失敗
- Windows／Linux build失敗

---

# 10. 実装commit分割

推奨commit順:

1. `metal: add strict path diagnostics and counters`
2. `metal: formalize texture output lease metadata`
3. `metal: share device and queue synchronization context`
4. `metal2d: promote segmented renderer to production path`
5. `metal: keep display capture GPU resident`
6. `metal3d: remove normal-frame raster readback`
7. `metal: remove SoftRenderer inheritance from MetalRenderer`
8. `metalcompute: make compute final texture authoritative`
9. `metal-presenter: reject CPU output for Metal renderers`
10. `metal-ui: render HUD and OSD through Metal pipelines`
11. `metal: migrate macOS defaults after parity validation`
12. `metal: split and precompile production MSL assets`

各commitは次を満たす。

- 単独でbuild可能
- Metal OFF build可能
- Windows／Linuxへ影響しない
- revert可能
- 変更理由と受け入れ結果をcommit messageへ記載

---

# 11. 実装順序の依存関係

```text
M0 観測
 └─ M1 output contract
     └─ M2 shared context
         ├─ M3 Metal 2D production
         │   └─ M4 GPU capture
         │       └─ M5 Metal Raster GPU-only
         │           └─ M6 SoftRenderer継承撤廃
         │               └─ M7 Metal Compute独立化
         │                   └─ M8 presenter strict化
         │                       └─ M9 HUD／OSD Metal化
         │                           └─ M10 既定化
         └─ M11 shader整理は画質parity後
```

M3とM4を飛ばしてM7へ進まない。Computeを完全可視化するには、CPU `GetLine()`を要求しないMetal 2Dが先に必要である。

---

# 12. 完了判定チェックリスト

## GPU core

- [ ] Metal RasterがSoftware 2Dを使用しない
- [ ] Metal Rasterが通常readbackしない
- [ ] Metal ComputeがRasterReferenceを使用しない
- [ ] Metal Computeが通常readbackしない
- [ ] Metal 2DがBG／OBJ／window／blendを処理する
- [ ] mixed 3D + 2DがMetal内で完結する
- [ ] Display CaptureがGPU常駐する
- [ ] capture feedbackでもGPU経路を継続する
- [ ] master brightnessがMetalで正しい
- [ ] screen swapがscanline単位で正しい

## output／presenter

- [ ] Metal outputは常に2-layer Metal texture array
- [ ] CPU BGRA fallbackがない
- [ ] output leaseがGPU completionまで有効
- [ ] stale generationを表示しない
- [ ] `CAMetalLayer`へ直接presentする
- [ ] normal-frame CPU uploadがない
- [ ] fullscreen／Retina変更でraceがない

## UI overlay

- [ ] Custom HUDがMetal描画
- [ ] HUD editorがMetal描画
- [ ] OSDがMetal描画
- [ ] splashがMetal描画
- [ ] 毎フレーム全画面QImageを作らない
- [ ] 毎フレーム全画面overlayをuploadしない

## build／platform

- [ ] Metal ON build成功
- [ ] Metal OFF build成功
- [ ] FORCE_DISABLE build成功
- [ ] Windows build成功
- [ ] Linux build成功
- [ ] macOS feature probeが正しい
- [ ] unsupported Macで明示的に選択不可

## runtime

- [ ] strict GPU-only 10分連続実行
- [ ] fallback counter全て0
- [ ] readback counter通常0
- [ ] command failure 0
- [ ] capture tests通過
- [ ] savestate tests通過
- [ ] multiplayer tests通過
- [ ] 1x～8x tests通過
- [ ] audio underrun増加なし
- [ ] frame pacing悪化なし

---

# 13. 最終的に削除できる互換経路

全試験完了後、次を段階的に削除する。

- `MetalRenderer`からの`SoftRenderer::GetOutput()` fallback
- CPU composite用`CpuComposite` texture slot
- high-resolution 3D ownership replacement shader
- `SoftRenderer3D Delegate`のproduction生成
- Computeのproduction `RasterReference`
- `MELONPRIME_METAL_FULL_GPU`ゲート
- `MELONPRIME_METAL_GETLINE_SOURCE`
- `MELONPRIME_METAL_HIRES_REPLACEMENT`
- Metal presenterのCPU top／bottom upload
- Metal presenterの全画面`QImage uiOverlay`
- capture feedbackによる`RetainPrevious`
- 「safe visible fallback」を前提にしたUI説明

削除は一括で行わない。各互換経路の利用カウンターが複数テストで0であることを確認してから削除する。

---

# 14. 最終状態

完全後の`Metal`は次の意味になる。

> DS 3DをMetal render pipelineで描画し、DS 2D、Display Capture、画面合成、HUD、OSD、presentationまでMetal textureをCPUへ戻さず処理するproduction renderer。

完全後の`Metal Compute Shader`は次の意味になる。

> DS 3DをMetal compute pipelineで描画し、そのcompute final textureをMetal 2D compositorへ直接渡し、Metal Rasterへ戻らない独立production renderer。

この状態になって初めて、UI上の`Metal`と`Metal Compute Shader`が、実際の可視出力経路と完全に一致する。

---

# 15. 実装開始時の最初の作業

最初のPRでは機能変更を広げず、次だけを実施する。

1. M0のfallback／readback／CPU composite countersを追加する。
2. `MELONPRIME_METAL_ASSERT_GPU_ONLY=1`を追加する。
3. Metal Raster、Metal Compute、Full GPU環境変数あり／なしでログを採取する。
4. MPHのmenu、gameplay、map、pause、capture発生箇所のbaselineを保存する。
5. どの条件でFull GPUが拒否されるかを一覧化する。
6. その結果を基にM3とM4の作業単位を確定する。

最初から`SoftRenderer`継承を削除しない。現在の依存箇所をカウンターとcall traceで確定してから、M3～M6の順に切り離す。
