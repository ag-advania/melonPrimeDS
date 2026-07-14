# Vulkan 参照実装ポート 実行計画（V0–V5）

**作成日:** 2026-07-13  
**対象ブランチ:** `highres_fonts_v3`  
**上位計画:** [melonPrimeDS_Vulkan_GPU3D_完全実装計画.md](melonPrimeDS_Vulkan_GPU3D_完全実装計画.md)（本書はその実行手順）  
**症状:** Vulkan選択時に内部解像度設定が効かず、可視出力が常に1x相当。

## 0. 根本原因（2026-07-13 監査確定）

設定伝搬（UI→Config→EmuThread→`VulkanRenderer::SetRenderSettings`）は正常。問題はアーキテクチャ:

- 可視出力の基底は常に `SoftRenderer`（Software 2D/3D合成、256x192 CPU BGRA）。
- プレゼンター側 `NativeRasterGpu` がGPUでスケール3Dを再ラスタライズするが、採用は
  `phase10_presenter.frag` の画素単位ヒューリスティック（所有ビット + 不透明 + RGB一致等）に
  ゲートされ、実質ほぼ全画素がSoftware 1x基底のまま表示される。
- この「Software 3Dをownership referenceとして併走 + presenter再ラスタライズ + CPU snapshot」方式は
  上位計画 §25 の禁止方式であり、修正ではなく置換対象。

## 1. 参照実装（ローカルclone済み）

| リポジトリ | ローカルパス | 版 |
|---|---|---|
| SapphireRhodonite/melonDS-android-lib | `C:/Users/Admin/Documents/git/melonDS-android-lib` | `d7794427`（checkout済み） |
| SapphireRhodonite/melonDS-android | `C:/Users/Admin/Documents/git/melonDS-android` | tag `0.7.0.rc4` |

参照アーキテクチャ:

```text
core: VulkanRenderer3D : Renderer3D   (GPU3D_Vulkan.cpp 13,943行, graphics-hardware backend)
      + VulkanContext (instance/device/queue 共有, 800行)
      + VulkanDispatch (グローバルPFN loader ≒ volk相当)
      + GPU3D_TexcacheVulkan (GPU texture cache, 584行)
      + GPU3D_AcceleratedFrontend (scene build, 725行)
      + 事前生成SPIR-V (*ShaderData.h)
core 2D: GPU2D_Soft の Accelerated モード（stride 256*3+1 のパック平面:
      plane0/plane1/control + line meta。Sapphire拡張のstructured metadata付き）
frontend: VulkanOutput.cpp (GPUコンポジタ 5,622行, VulkanCompositorShader.comp 1,160行)
        + VulkanSurfacePresenter.cpp (swapchainプレゼンター 3,756行)
```

要点:

- 参照は `vkXxx` グローバル関数呼び出しスタイル。volk のグローバルPFNと互換 →
  **VulkanDispatch はポートせず volk を使う**（`Initialize/LoadInstance/LoadDevice` →
  `volkInitialize/volkLoadInstance/volkLoadDevice`）。
- 参照コアは VMA 不使用（生 `vkAllocateMemory`）。
- 参照の `BackendMode` は `GraphicsHardware` のみ（compute backendは rc4 に存在しない）。
- `GetLine()` は capture line export（GPU→CPU readback、native 256幅）で全ライン供給可能 →
  melonPrimeDS の Soft 2D 合成（`GPU_Soft.cpp` の `Rend3D->GetLine(line)`）にそのまま接続できる。
- 高解像度の可視化は Accel 2D パック平面 + `VulkanCompositorShader.comp` + presenter の組で成立する
  （置換画素ヒューリスティック不要）。

## 2. melonPrimeDS 側の統合点（監査済み）

- `Renderer` 基底（`GPU.h:1010`）: `Start3DRendering()/Finish3DRendering()/Restart3DRendering()` が
  `Rend3D` を呼ぶ。Metal と同型で、`VulkanRenderer`（SoftRenderer派生）の `Rend3D` スロットへ
  `VulkanRenderer3D` を差せる。
- `Renderer3D` インターフェース（`GPU3D.h:321`）は最小型
  （Init/Reset/RenderFrame/FinishRendering/RestartFrame/GetLine）。参照が要求する追加フック
  （`VCount144/SetupAccelFrame/PrepareCaptureFrame/BeginCaptureFrame/SetCaptureScreenSwapHint/Blit/Stop/
  UsesStructured2DMetadata`）は `#ifdef MELONPRIME_DS` ガード付き仮想関数として追加する
  （デフォルト実装no-op、非MelonPrimeビルド挙動不変）。
- 参照メソッドは `GPU&` 引数、melonPrimeDS はctor保持参照。ポート時に melonPrimeDS 流へ機械変換。
- CMake: `MELONPRIME_VULKAN_ACTIVE` ゲート、volk / vulkan-headers / VMA / glslangValidator 配線済み。
  ローカルビルドツリーは `MELONPRIME_ENABLE_VULKAN=ON`。
- `Vertex::HiresPosition` は本ツリーに存在（RomScaleBridgeが使用中）→ 参照の頂点要件を満たす。

## 3. フェーズ

### V0 — 旧ハイブリッド経路の停止と撤去（クリーンスレート）

上位計画 §20 を先行実行する。削除対象（消費者ゼロを確認してから削除）:

- core: `NativeRasterSnapshotBuilder` 一式（`GPU_Vulkan.cpp` 後半 + `GPU_Vulkan.h` の
  `NativeRasterFrame/NativeRasterTexture`）、`Native3DFrame/Native3DVisible/Native3DBgra`、
  `OnRendered3DLine/OnComposed3DOwnershipLine` オーバーライド、`CompatibilityFrame` 機構、
  `Vulkan_RomScaleBridge.*`、旧 `GPU3D_Vulkan.{h,cpp}`（contract）、`GPU3D_TexcacheVulkan.{h,cpp}`（旧CPU decode版）、
  `GPU3D_VulkanCompute.{h,cpp}`、`GPU_VulkanOutputRing.{h,cpp}`（実体無しcontract）
- `GPU_Soft.{h,cpp}` / `GPU2D_Soft.cpp` の ownership-line フック（5b25bf5b追加分）を撤去し
  upstream形状へ戻す
- frontend: `MelonPrimeVulkanNativeRaster.{h,cpp}`、全 `MelonPrimeVulkan*Bootstrap.*`、
  presenter の native raster バインディング（binding 3-5）とハイブリッド分岐
  （`phase10_presenter.frag` を CPU 2画面 + HUD + radar のみに縮退、SPIR-V再生成）
- 残すもの: `VulkanRenderer` の identity/lifecycle、`MelonPrimeScreenVulkan`（QVulkanWindow CPUパス）、
  `MelonPrimeVulkanFeatureCheck/InstanceHost`、`Vulkan_Phase12UiContract/Phase13*`（UI/policy消費があるもの）、
  renderer ID / Config / UI
- DoD: Windowsビルド green、Vulkan選択でROMがSoftware相当表示（従来と同じ見た目）、監査green

### V1 — VulkanContext 導入（volkベース）

- 参照 `VulkanContext.{h,cpp}` を `src/` へポート。Android固有（AHB、custom driver、
  `vulkan_android.h`）を除去し、volk 初期化に置換。desktop device profile
  （NVIDIA/AMD/Intel判定）へ差し替え。timeline semaphore / dynamic indexing 検出は維持。
- CMake `MELONPRIME_VULKAN_ACTIVE` 内に追加。
- DoD: ビルドgreen。`VulkanRenderer::Init()` から `Acquire/Release` して動作ログ確認。

### V2 — コア3D移植（本丸A: GPU3Dラスタライザ）

- ポート: `GPU3D_AcceleratedFrontend.{h,cpp}`、`GPU3D_TexcacheVulkan.{h,cpp}`（参照版）、
  `GPU3D_Vulkan.{h,cpp}`（13.9k行）、全 `GPU3D_Vulkan_*ShaderData.h`（事前生成SPIR-V、逐語コピー）+
  対応 GLSL ソース（記録用）。
- 適応規則:
  - `VulkanDispatch` → volk（`#include <volk.h>`、Initialize系のみ置換）
  - `Renderer3D(bool accelerated)` 系 → melonPrimeDS `Renderer3D(GPU3D&)` 形状。`GPU&` 引数
    メソッドは保持参照使用へ変換
  - `GPU3D.h` へガード付きフック追加（§2）。`GPU2D_Soft`/`GPU.cpp` の capture 経路が
    `PrepareCaptureFrame/BeginCaptureFrame` を必要とする箇所は参照 `GPU.cpp`/`GPU2D_Soft.cpp` の
    呼び出し位置を移植（ガード付き）
  - Android専用コード（AHB import等）は削除
- 統合: `VulkanRenderer::Init()` で `VulkanContext::Acquire()` 成功時に `Rend3D` を
  `VulkanRenderer3D` に差し替え（失敗時は SoftRenderer3D 継続 + 明示ログ）。
  `SetRenderSettings` が scale/BetterPolygons を `VulkanRenderer3D::SetRenderSettings` へ転送。
  Soft 2D は `GetLine()`（capture line export 由来の native ライン）で合成。
- DoD: ビルドgreen。ROM起動でGPUラスタ動作（ログ + 目視で3D正常）。可視解像度はまだ1x
  （GetLine経由のため）。クラッシュ/バリデーションエラーなし。

### V3 — Accel 2D（structured packed planes）

- 参照 `GPU2D_Soft.cpp` の Accelerated 経路（`DrawPixel_Accel`、stride 256*3+1、meta flags、
  structured handoff）を melonPrimeDS `GPU2D_Soft.cpp` / `GPU_Soft.cpp` / `GPU.cpp`
  （framebufferサイズ）へ移植。`Renderer3D::Accelerated` 相当の切替は
  `UsesStructured2DMetadata()` ベースでガード付き導入。
- Vulkan renderer選択時のみ有効。Software/GL/Metal 選択時は完全に従来経路。
- DoD: ビルドgreen + Vulkan時にパック平面が生成される（V4までは可視変化なし。
  互換のためV4完成までフラグOFFのままでも可）。

### V4 — GPUコンポジタ + プレゼンター（本丸B: ここで高解像度が可視化）

- 参照 frontend から `VulkanOutput.{h,cpp}`（コンポジタ）、`VulkanCompositorShader.comp`、
  `VulkanAccumulate3dShader.comp`、`VulkanSurfacePresenter.{h,cpp,vert,frag}` を
  `src/frontend/qt_sdl/` へポート（ANativeWindow → Qt `QWindow` +
  `QVulkanInstance::surfaceForWindow`、instance は VulkanContext の VkInstance をラップ）。
- `MelonPrimeScreenVulkan` を QVulkanWindow ベースから VulkanContext 共有デバイス +
  自前swapchainベースへ置換。HUD/OSD/radar オーバーレイは既存機能を新presenterへ移植。
- 出力: 2画面GPU-residentテクスチャ（scale×256 × scale×192）、frame serial/generation lease。
- DoD: 内部解像度 1x/2x/4x/8x で**可視出力が実際に変化**。resize/fullscreen/最小化復帰、
  HUD/radar/OSD、Fast Forward、30分ソークでハングなし。

### V5 — 仕上げ

- Vulkan Compute Shader ID の扱い正規化（当面 graphics backend へマップ + ログ、UI文言調整）。
- coverage fix 等の設定配線、`3D.Hardware.*` キーとの整合。
- 上位計画 §20 残件の削除確認、ドキュメント更新（本書 + 上位計画進捗表 + repo-architecture）。
- 監査一式 + Windowsビルド + 手動スモーク記録。

## 4. 不変条件

- 上位計画 §2（ビルドゲート、renderer/presenter分離、requested/normalized/actual、frame ownership、
  upstream最小差分）を全面継承。
- 非 `MelonPrime*` ファイル変更は `#ifdef MELONPRIME_DS`（+ Vulkanは
  `MELONPRIME_ENABLE_VULKAN`）ガード必須。
- `git reset --hard` 禁止。フェーズ = 独立コミット列。
- ビルドは `.\.claude\skills\build-mingw.bat`（`--jobs 1`）。CMake変更なしの再ビルドは
  `build-mingw-existing.bat` 可。

## 5. 進捗

| Phase | 状態 | 日付 | メモ |
|---|---|---|---|
| V0 | 未着手 | — | |
| V1 | 未着手 | — | |
| V2 | 未着手 | — | |
| V3 | 未着手 | — | |
| V4 | 未着手 | — | |
| V5 | 未着手 | — | |
