# MelonPrimeDS Vulkan Rendering Backend 完全実装フェーズ計画

**作成日:** 2026-07-11  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**文書ステータス:** 実装進行中の正式設計・実行計画
**最終目標:** `Vulkan` と `Vulkan Compute Shader` の両方を、既存の `OpenGL` と `OpenGL Compute Shader` に並ぶ正式な描画バックエンドとして完成させる

## 実装進捗

| Phase | Status | Commit | Build verified | Runtime verified | Hardware / Validation | Unverified / Known limitation | Rollback |
|---|---|---|---|---|---|---|---|
| 0 — baseline | Implemented; evidence gate pending | `docs(vulkan): capture Vulkan backend baseline and acceptance matrix` | Windows configure / compile / link passed | HUD harness repeatable; ROM capture pending | Win11 / RTX 5070 Ti; repo audits passed | No scoped ROM; existing HUD golden drift recorded in `tests/vulkan/reference/phase0-verification.md` | Revert the Phase 0 commit |
| 1 — build gate / shader toolchain | Done | `build(vulkan): add complete Vulkan build gate and shader toolchain` | Windows default OFF / Vulkan ON / force-disable passed | No Vulkan runtime path by design; default-OFF HUD harness unchanged | Win11 / RTX 5070 Ti; full SPIR-V check and repo audits passed | Linux, macOS, MoltenVK and CI execution unverified; see `tests/vulkan/reference/phase1-verification.md` | Revert the Phase 1 commit |
| 2 — backend policy / stable IDs | Done | `refactor(video): add stable renderer IDs and Vulkan backend policy` | Windows default OFF / Vulkan ON / force-disable passed | OFF config ID 5 and ON env raster/compute no-ROM smoke passed | Win11 / RTX 5070 Ti; repo audits passed | ROM renderer creation/log and non-Windows unverified; see `tests/vulkan/reference/phase2-verification.md` | Revert the Phase 2 commit |
| 3 — Qt instance / device probe | Done | `feat(vulkan): add Qt Vulkan instance host and device capability probe` | Windows Vulkan ON / force-disable / default OFF passed | RTX 5070 Ti surface/device probe and create/destroy 10/10 passed | Win11 / RTX 5070 Ti; raster+compute+timeline available | Validation layer absent; X11, Wayland, macOS and CI execution deferred; see `tests/vulkan/reference/phase3-verification.md` | Revert the Phase 3 commit |
| 4 — CPU BGRA presenter baseline | Implemented; ROM/long-run acceptance pending | `feat(vulkan): add CPU BGRA Vulkan presenter baseline` | Windows Vulkan ON / force-disable / default OFF passed | Vulkan no-ROM capture, resize, maximize, minimize/restore and two-window capture passed | Win11 / RTX 5070 Ti; generated presenter SPIR-V and repo audits passed | No scoped ROM; active OSD/HUD/radar/Fast Forward and 30-minute run unverified; Qt baseline is FIFO pending explicit VSync-OFF mode work; see `tests/vulkan/reference/phase4-verification.md` | Revert the Phase 4 commit |
| 5 — typed Vulkan output / shared lease | Done; progress record normalized by Phase 6 package | `9040e63728d8 (Phase 5 source state)` | Windows default OFF / Vulkan ON / force-disable passed in supplied log | lease harness passed; serial/generation and stale-generation rejection confirmed | Win11 / RTX 5070 Ti | Real Vulkan GPU-image producer and cross-queue slot reuse deferred; see `tests/vulkan/reference/phase5-verification.md` | Revert the Phase 5 source commit(s) |
| 6 — Vulkan renderer shell | Implemented; Windows build/runtime pending | `pending Phase 6 commit` | Windows Vulkan build pending | shell harness pending; ROM runtime pending | Windows package targets UCRT64 | Native Vulkan 3D intentionally absent; see `tests/vulkan/reference/phase6-verification.md` | Revert the Phase 6 commit |
| 7 — Vulkan Classic 3D correctness baseline | Phase 7.1 through Phase 7.10B textured polygon GPU draw implemented; Windows build/runtime pending | `pending Phase 7.10B commit` | Windows Vulkan build pending | opaque/translucent/shadow/toon/highlight and textured polygon harnesses pending | Windows UCRT64 package | ROM still uses Software; edge/fog/line/GetLine remain pending | Revert the Phase 7.10B commit |
| 8 — Vulkan texture cache/capture lifecycle | Implemented as a complete subsystem candidate: residency, all-format RGB6A5 decode, dirty-page hash, persistent upload ring, timeline/fence retirement, display capture, capture-backed texture reuse, CPU VRAM sync, savestate/reset/renderer-switch lifecycle and 1x through 16x memory-budgeted scale planning; Windows/ROM acceptance pending | `pending Phase 8 completion commit` | Windows Vulkan build pending | integrated Phase 8 completion and regression harnesses pending | Windows UCRT64 package | ROM rendering remains on the Software correctness baseline; native DS polygon/2D renderer integration is deliberately not claimed by Phase 8 | Revert the Phase 8 completion commit |
| 9 — Vulkan 2D/final composition/GPU resident output | Implemented as a complete subsystem candidate: Software 2D upload plus Vulkan final, native Vulkan 2D mirror/visible compute passes, BG/OBJ/window/blend/mosaic/brightness/3D-layer composition, per-scanline partial render, VRAM/FIFO display modes, ScreenSwap, two-layer GPU-resident output and frame-serial ownership validation; Windows/ROM acceptance pending | `pending Phase 9 completion commit` | Windows Vulkan build pending | integrated Phase 9 GPU/readback and Phase 8 regression harnesses pending | Windows UCRT64 package | Normal-frame CPU readback is absent inside the Phase 9 subsystem harness, but the ROM-visible Vulkan renderer path remains deliberately inactive until acceptance; zero-copy presenter is Phase 10 | Revert the Phase 9 completion commit |
| 10 — zero-copy presenter/output ring/multi-window | Implemented as a complete subsystem candidate: fixed three-slot output ring, direct resident-image sampling, producer timeline/fence synchronization, two simultaneous presenter leases, bounded frame-drop policy, generation isolation and scale/renderer/fullscreen lifecycle; Windows/ROM acceptance pending | `pending Phase 10 completion commit` | Windows Vulkan build pending | integrated Phase 10 GPU and Phase 9 regression harnesses pending | Windows UCRT64 package | The harness proves zero presenter CPU-copy on one Vulkan device; the ROM-visible renderer remains Software until hardware acceptance and Phase 11 compute work | Revert the Phase 10 completion commit |
| 11 — Vulkan Compute Shader renderer | Implemented as a complete subsystem candidate: all 33 stage equivalents, shared SPIR-V specialization pipelines, explicit storage/indirect/image barriers, device-limit-aware dispatch planning, integer texture/capture inputs, shadow/fog/edge/AA final variants, Hires Coordinates and 1x through 16x GPU output integrated with the Phase 10 output ring; Windows/ROM acceptance pending | `pending Phase 11 completion commit` | Windows Vulkan build pending | integrated Phase 11 GPU and Phase 8 through 10 regression harnesses pending | Windows UCRT64 package | The developer harness owns its visible compute output; the normal ROM-visible backend remains Software until pixel-parity acceptance on scoped ROMs and multiple vendors | Revert the Phase 11 completion commit |
| 12～16 | Not started | — | — | — | — | Phase順に実施 | Phase単位でrevert |

---

# 0. この計画の位置付け

この文書は、MelonPrimeDSへVulkan描画を追加するための、設計、実装、検証、UI公開、CI、配布までを含む完全なフェーズ計画である。

Vulkan実装の参照元は、次の3系統とする。

1. **OpenGL Classic**
   - 現在正常に動作している通常のGPUラスタライザ
   - DSのポリゴン描画、テクスチャ、深度、ステンシル、影、toon、highlight、fog、edge marking、display capture、最終合成の正解実装として扱う

2. **OpenGL Compute Shader**
   - 現在正常に動作しているcomputeベースのラスタライザ
   - span生成、binning、tile memory、variant別work list、間接dispatch、最終passの正解実装として扱う

3. **Metal実装とCLAUDE.md周辺**
   - CMake完全ゲート
   - rendererとpresenterの分離
   - feature probe
   - config正規化
   - requested rendererとactual rendererの区別
   - output lease
   - frame ownership
   - UIの動的追加
   - platform限定機能の扱い
   - fallbackログ
   - 段階的公開
   - upstream差分の最小化
   - 各Phaseの検証記録

Vulkan実装は、Metalコードをそのまま翻訳するものではない。  
描画の正しさはOpenGL Classic／OpenGL Compute Shaderから取り、統合・公開・安全性のルールはMetal計画から取る。

---

# 1. 最終完成状態

Vulkan対応完了時、MelonPrimeDSは次のrendererを持つ。

```text
Software
OpenGL
OpenGL Compute Shader
Metal
Metal Compute Shader
Vulkan
Vulkan Compute Shader
```

最終的なVulkan系の役割は次の通り。

| renderer | 役割 | 主な参照実装 |
|---|---|---|
| Vulkan | 通常のVulkanラスタライザ | `GPU3D_OpenGL.cpp` |
| Vulkan Compute Shader | Vulkan computeによるDSラスタライザ | `GPU3D_Compute.cpp` |
| Vulkan presenter | Qt windowへVulkan swapchainで表示 | `ScreenPanelGL`、`ScreenPanelMetal` |
| Vulkan 2D／final composition | DS 2D、display mode、brightness、capture、top／bottom合成 | `GPU_OpenGL.cpp`、`GPU2D_OpenGL.cpp` |
| Vulkan texture cache | DS texture VRAMのdecode、upload、dirty管理 | `GPU3D_TexcacheOpenGL.*` |
| Vulkan output handoff | rendererからpresenterへのGPU image引き渡し | Metalの`RendererOutput`／lease設計 |

## 1.1 Tier 1対象

正式サポート対象は次とする。

- Windows 10／11 x86_64
- Linux x86_64
  - X11
  - Wayland
- Vulkan 1.1以上
- Intel、AMD、NVIDIA

## 1.2 Tier 2対象

macOSはMoltenVK経由の任意対応とする。

- macOS上のVulkanはネイティブAPIではない
- 既存のMetal rendererを置き換えない
- MoltenVKが利用可能なbuildでのみVulkan項目を生成する
- macOSではMetal／Metal Computeを優先経路とする
- MoltenVK固有の制約を理由にWindows／Linux側の設計を劣化させない

## 1.3 非目標

初期実装では次を行わない。

- OpenGL rendererの削除
- OpenGL Compute Shaderの削除
- Metal rendererの削除
- Vulkan非対応環境での強制使用
- runtime GLSLコンパイラの必須化
- Vulkan 1.3専用機能の必須化
- descriptor indexingの必須化
- dynamic renderingの必須化
- synchronization2の必須化
- ray tracing関連機能
- VulkanとOpenGLでのリソース共有
- rendererの選択だけで既存プリセットの意味を変更すること
- parity前のVulkan既定ON化

---

# 2. 絶対に守る設計原則

# 2.1 完全なビルドゲート

Vulkan関連の全要素は、1つの派生条件で完全に除外できなければならない。

```cmake
MELONPRIME_ENABLE_VULKAN
MELONPRIME_FORCE_DISABLE_VULKAN
MELONPRIME_ENABLE_VULKAN_MOLTENVK
MELONPRIME_VULKAN_ACTIVE
```

`MELONPRIME_VULKAN_ACTIVE=OFF`のbuildでは、次が残ってはならない。

- Vulkan renderer enum
- Vulkan presenter enum
- Vulkan source
- Vulkan headers
- Vulkan Loader link
- volk
- Vulkan Memory Allocator
- shader生成物
- Vulkan UI
- Vulkan translation
- Vulkan feature probe
- Vulkan force環境変数
- Vulkanログ文字列
- Vulkan runtime dependency
- Vulkan config migrationの副作用

禁止例:

```cpp
#ifdef _WIN32
#include <vulkan/vulkan.h>
#endif
```

正しい形:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
#include <vulkan/vulkan.h>
#endif
```

CMake側では、source、definition、library、tool、UIをすべて同じ`MELONPRIME_VULKAN_ACTIVE`内へ置く。

# 2.2 rendererとpresenterを分離する

Vulkan rendererとVulkan presenterは別責務とする。

```text
VulkanRenderer
    DSの2D／3D／capture／final imageを生成する

ScreenPanelVulkan
    VkSurfaceKHR／VkSwapchainKHRを所有し、windowへ表示する
```

Vulkan rendererが存在していても、presenterはCPU BGRAを表示できる。  
Vulkan presenterが存在していても、初期PhaseではSoftware rendererのCPU BGRAを表示できる。

この分離により、次を個別に検証できる。

- Vulkan device作成
- Qt surface作成
- swapchain表示
- CPU upload
- native 3D
- native 2D
- zero-copy handoff
- compute renderer

# 2.3 OpenGLを正解実装として扱う

Vulkanの描画結果は、推測や独自仕様ではなく、既存OpenGL実装と比較する。

通常ラスタライザの主参照:

```text
src/GPU_OpenGL.cpp
src/GPU2D_OpenGL.cpp
src/GPU3D_OpenGL.cpp
src/GPU3D_OpenGL.h
src/GPU3D_TexcacheOpenGL.cpp
src/OpenGL_shaders/
```

Compute rendererの主参照:

```text
src/GPU3D_Compute.cpp
src/GPU3D_Compute.h
src/GPU3D_Compute_shaders.h
```

特に次の挙動を完全に一致させる。

- polygon RAM順序
- Z-buffer／W-buffer
- less／less-equal
- depth write
- translucent same-polyID抑制
- shadow mask
- shadow polygon
- texture format
- texture repeat／mirror
- alpha test
- decal／modulate／toon／highlight
- clear plane
- clear bitmap
- fog
- edge marking
- antialiasing
- line polygon
- display capture
- RenderXPos
- ScreenSwap
- Master Brightness
- VRAM display mode
- main-memory FIFO display
- Better Polygons
- High Resolution Coordinates

# 2.4 requested／normalized／actualを分ける

Vulkan選択時は、最低でも次の3値を持つ。

```text
requested renderer
normalized renderer
actual renderer
```

例:

```text
requested = Vulkan Compute Shader
normalized = Vulkan
actual = Vulkan
reason = compute capability requirement not met
```

または:

```text
requested = Vulkan
normalized = Software
actual = Software
reason = no Vulkan physical device supports presentation
```

fallbackを無言で行ってはならない。

# 2.5 frame ownershipを固定する

完成画面Nへ、3D画面N+1を混ぜてはならない。

Vulkan output slotは次を保持する。

```text
frame serial
generation
VkImage
VkImageView
image layout
extent
scale
top／bottom layer mapping
ScreenSwap
MasterBrightnessA
MasterBrightnessB
producer timeline value
presenter reference count
```

consumerはlive renderer stateを再読込せず、producerが公開したsnapshotのみを使う。

# 2.6 GPU imageの寿命をleaseで管理する

rendererがslot Aへ次フレームを書き始める前に、全presenterがslot Aの読み込みを完了していなければならない。

必要な同期:

- renderer内部のframes-in-flight
- renderer queueからpresenter queueへのtimeline semaphore
- presenter acquire semaphore
- presenter render-complete semaphore
- swapchain image fence
- output slot lease
- scale変更時のgeneration
- renderer破棄時のdrain
- multi-window presenter参照数

# 2.7 upstream所有ファイルの変更を最小化する

新規実装は原則としてfork所有ファイルへ置く。

優先:

```text
MelonPrimeVulkan*.cpp
MelonPrimeVulkan*.h
GPU_Vulkan*.cpp
GPU2D_Vulkan*.cpp
GPU3D_Vulkan*.cpp
VulkanSupport*.cpp
```

既存melonDSファイルを変更する場合:

```cpp
#ifdef MELONPRIME_DS
// fork固有処理
#else
// upstreamと同一の処理
#endif
```

変更候補となるupstream所有ファイル:

```text
src/GPU.h
src/GPU.cpp
src/CMakeLists.txt
src/frontend/qt_sdl/EmuInstance.h
src/frontend/qt_sdl/EmuThread.cpp
src/frontend/qt_sdl/Screen.h
src/frontend/qt_sdl/Window.cpp
src/frontend/qt_sdl/VideoSettingsDialog.cpp
src/frontend/qt_sdl/Config.cpp
src/frontend/qt_sdl/CMakeLists.txt
```

これらへの変更は、専用ファイルで解決できない統合点だけに限定する。

---

# 3. 推奨ファイル構成

## 3.1 Core renderer

```text
src/
├── GPU_Vulkan.h
├── GPU_Vulkan.cpp
├── GPU_VulkanContext.h
├── GPU_VulkanContext.cpp
├── GPU_VulkanOutput.h
├── GPU2D_Vulkan.h
├── GPU2D_Vulkan.cpp
├── GPU3D_Vulkan.h
├── GPU3D_Vulkan.cpp
├── GPU3D_VulkanCompute.h
├── GPU3D_VulkanCompute.cpp
├── GPU3D_TexcacheVulkan.h
├── GPU3D_TexcacheVulkan.cpp
├── VulkanSupport.h
├── VulkanSupport.cpp
└── Vulkan_shaders/
    ├── common.glsl
    ├── present.vert
    ├── present.frag
    ├── final_pass.vert
    ├── final_pass.frag
    ├── capture.vert
    ├── capture.frag
    ├── capture_downscale.comp
    ├── 3d_clear.vert
    ├── 3d_clear.frag
    ├── 3d_clear_bitmap.frag
    ├── 3d_render.vert
    ├── 3d_render.frag
    ├── 3d_edge.frag
    ├── 3d_fog.frag
    ├── compute_common.glsl
    ├── compute_interp_spans.comp
    ├── compute_bin_combined.comp
    ├── compute_depth_blend.comp
    ├── compute_rasterise.comp
    ├── compute_sort_work.comp
    ├── compute_final_pass.comp
    └── generated/
        ├── VulkanShaders.h
        └── *.spv.h
```

## 3.2 Qt frontend

```text
src/frontend/qt_sdl/
├── MelonPrimeVulkanInstanceHost.h
├── MelonPrimeVulkanInstanceHost.cpp
├── MelonPrimeVulkanFeatureCheck.h
├── MelonPrimeVulkanFeatureCheck.cpp
├── MelonPrimeScreenVulkan.h
├── MelonPrimeScreenVulkan.cpp
└── MelonPrimeVulkanDiagnostics.h
```

## 3.3 Tooling

```text
tools/vulkan/
├── generate_spirv.py
├── check_spirv.py
├── build_vulkan_test.ps1
├── build_vulkan_test.command
├── run_vulkan_validation.ps1
├── run_vulkan_validation.command
└── compare_renderer_frames.py
```

## 3.4 文書

```text
.claude/rules/melonprime-vulkan-backend-plan.md
.claude/rules/completed/
CLAUDE.md
```

---

# 4. Vulkan全体アーキテクチャ

# 4.1 ownership

推奨ownershipは次の通り。

```text
MelonApplication
└── MelonPrimeVulkanInstanceHost
    └── QVulkanInstance

EmuInstance
└── VulkanDeviceContext
    ├── VkPhysicalDevice
    ├── VkDevice
    ├── graphics queue
    ├── compute queue
    ├── transfer queue
    ├── pipeline cache
    ├── VMA allocator
    └── shared timeline semaphores

NDS::GPU
└── VulkanRenderer
    ├── VulkanRenderer2D A
    ├── VulkanRenderer2D B
    ├── VulkanRenderer3D または VulkanComputeRenderer3D
    ├── capture resources
    ├── final composition resources
    └── output ring

MainWindow
└── ScreenPanelVulkan
    ├── VkSurfaceKHR
    ├── VkSwapchainKHR
    ├── swapchain image views
    ├── per-frame command buffers
    ├── acquire／render-complete semaphores
    └── per-frame fences
```

## 4.2 device共有

rendererとpresenterは同じ`VkDevice`を使う。

理由:

- GPU imageをコピーせずpresenterへ渡せる
- external memoryを不要にできる
- queue family ownership transferを明示できる
- device lossの単位が一致する
- pipeline cacheを共有できる

複数windowを持つ場合:

- `EmuInstance`単位で1 device
- 各windowが独立したsurface／swapchain
- 選択deviceが全surfaceでpresent可能か確認
- 対応できない追加surfaceが生じた場合は、そのwindowだけCPU presenterへ落とすのではなく、明確なエラーとしてVulkan presenter再構成を止める
- multi-windowの正式公開は対応確認後に行う

## 4.3 queue family

初期実装では、可能ならgraphics、compute、presentを同一queue familyにまとめる。

優先順位:

1. graphics + compute + present
2. graphics + present、別compute
3. graphics、別compute、別present

queueが分かれる場合:

- image ownership transferを必ず実装
- concurrent sharingで問題を隠さない
- transfer queueは性能改善Phaseまでは任意
- `vkQueueSubmit`／`vkQueuePresentKHR`への同一queue外部同期を守る

## 4.4 shader方式

runtime GLSLコンパイルは正式経路にしない。

正式方式:

```text
GLSL source
→ glslangValidator または glslc
→ SPIR-V
→ generated header
→ binaryへ埋め込み
```

必須条件:

- sourceとgenerated SPIR-Vの不一致をCIで検出
- shader名、variant、define、specialization constantをmanifest化
- build host toolとtarget runtime dependencyを分離
- runtimeにshader compiler DLL／shared objectを要求しない
- compile errorを開発時に検出
- shader sourceとC++ struct layoutの対応をstatic_assertで確認

## 4.5 baseline API

初期baselineはVulkan 1.1とする。

必須候補:

- swapchain
- timeline semaphore
  - core 1.2またはextension経由
  - 非対応device向けにbinary semaphore＋fence fallbackを設計可能にする
- shader draw parameters相当
- storage buffer
- storage image
- indirect dispatch
- depth／stencil attachment
- image format support
- sampler anisotropyは不要
- descriptor indexingは不要
- dynamic renderingは不要

Vulkan 1.3機能は、optional optimizationとしてのみ使用する。

## 4.6 resource allocator

Vulkan Memory Allocatorを使用する。

用途:

- device-local image
- host-visible staging buffer
- persistently mapped upload ring
- compute SSBO
- readback buffer
- texture cache image
- transient attachment

毎frameの`vkAllocateMemory`は禁止する。

## 4.7 image format

推奨形式:

| 用途 | 第一候補 | fallback |
|---|---|---|
| final color | `VK_FORMAT_B8G8R8A8_UNORM` | `VK_FORMAT_R8G8B8A8_UNORM` |
| native 3D color | `VK_FORMAT_B8G8R8A8_UNORM` | `VK_FORMAT_R8G8B8A8_UNORM` |
| attr | `VK_FORMAT_R8G8B8A8_UNORM` | 同形式のみ |
| depth／stencil | `VK_FORMAT_D32_SFLOAT_S8_UINT` | `VK_FORMAT_D24_UNORM_S8_UINT` |
| compute tile color | storage bufferまたは`R32_UINT`配列 | device featureに応じてbuffer |
| compute depth／attr | storage buffer | 同左 |
| DS texture cache | format別image arrayまたはatlas | RGBA8 decode |

format差でpixel parityが変わる場合、fallback形式を正式サポート扱いにしない。

---

# 5. renderer IDとconfig互換性

現在のrenderer enumはcompile条件により値が前後する可能性がある。  
Vulkan追加時にpersisted configの意味が変わらないよう、明示値へ変更する。

推奨値:

```cpp
enum
{
    renderer3D_Software          = 0,
    renderer3D_OpenGL            = 1,
    renderer3D_OpenGLCompute     = 2,
    renderer3D_Metal             = 3,
    renderer3D_MetalCompute      = 4,
    renderer3D_Vulkan            = 5,
    renderer3D_VulkanCompute     = 6,
    renderer3D_Max               = 7,
};
```

compileされていないrenderer IDをconfigから読んだ場合:

```text
値を別rendererとして解釈しない
未対応値として検出する
platform policyに従い正規化する
理由をログへ出す
```

推奨正規化:

| requested | build／platform | normalized |
|---|---|---|
| Vulkan | Vulkan無効build | Software |
| Vulkan Compute | Vulkan無効build | Software |
| Vulkan Compute | compute要件不足 | Vulkan |
| Vulkan | presentation要件不足 | Software |
| OpenGL Compute | macOS | OpenGL |
| Metal | 非macOS | Software |
| 不明値 | 全platform | Software |

旧buildとの往復でconfigを破壊しないよう、normalized値を即時保存するかは別判断にする。  
runtimeだけ正規化し、ユーザーが設定保存を実行した時点で保存する方が安全である。

---

# 6. Phase 0 ～ 現状固定・基準画像・受入条件

## 6.1 目的

Vulkan実装前の正解を固定する。

## 6.2 実施内容

OpenGL ClassicとOpenGL Compute Shaderについて、次を収集する。

- requested／actual rendererログ
- 1x、2x、4x、8xのframe capture
- GPU3D output
- final top／bottom output
- display capture使用場面
- shader variant一覧
- render pass順序
- texture format一覧
- depth／stencil state一覧
- blend state一覧
- polygon submission順
- draw call数
- compute dispatch数
- CPU frame time
- GPU frame time
- upload量
- readback量
- VRAM dirty更新量

## 6.3 基準シーン

最低限、Metroid Prime Huntersで次を記録する。

```text
起動ロゴ
タイトル
メニュー
ハンター選択
試合開始暗転
通常戦闘
透過エフェクト
Shadow Freeze
Noxus関連効果
マップ
ポーズ
スキャン
ズーム
Morph Ball
ダメージ時
低HP表示
試合終了
画面切替
```

DS GPU機能別:

```text
clear plane
clear bitmap
opaque polygon
translucent polygon
shadow mask
shadow polygon
toon
highlight
fog
edge marking
antialiasing
line polygon
VRAM display
main-memory FIFO display
display capture 128
display capture 256
ScreenSwap途中変更
Master Brightness
RenderXPos
```

## 6.4 作成物

```text
tests/vulkan/reference/
├── manifest.json
├── opengl_classic/
├── opengl_compute/
└── expected_logs/
```

著作権上ROM由来画像をrepositoryへ入れられない場合:

- ローカル専用manifest
- hash
- capture手順
- scene identifier
- pixel diff script
- 画像本体は`.gitignore`

## 6.5 Exit Criteria

- OpenGL ClassicとComputeの正解経路を説明できる
- Vulkan parity対象が列挙されている
- 検証シーンが再現可能
- 実装前baseline commitが作成されている
- runtime behavior変更なし

## 6.6 推奨commit

```text
docs(vulkan): capture Vulkan backend baseline and acceptance matrix
```

---

# 7. Phase 1 ～ CMake完全ゲート・依存関係・shader toolchain

## 7.1 目的

Vulkanコードを一切有効化せず、完全に切り離されたbuild foundationを作る。

## 7.2 CMake option

```cmake
option(MELONPRIME_ENABLE_VULKAN
    "Build MelonPrime Vulkan renderers and presenter"
    OFF)

option(MELONPRIME_FORCE_DISABLE_VULKAN
    "Completely exclude all MelonPrime Vulkan code and dependencies"
    OFF)

option(MELONPRIME_ENABLE_VULKAN_MOLTENVK
    "Allow the Vulkan backend through MoltenVK on macOS"
    OFF)

set(MELONPRIME_VULKAN_ACTIVE OFF)

if (MELONPRIME_FORCE_DISABLE_VULKAN)
    set(MELONPRIME_VULKAN_ACTIVE OFF)
elseif (MELONPRIME_ENABLE_VULKAN)
    if (APPLE AND NOT MELONPRIME_ENABLE_VULKAN_MOLTENVK)
        message(FATAL_ERROR
            "Vulkan on macOS requires MELONPRIME_ENABLE_VULKAN_MOLTENVK=ON")
    endif()
    set(MELONPRIME_VULKAN_ACTIVE ON)
endif()
```

## 7.3 dependencies

推奨:

```text
Vulkan-Headers
Vulkan-Loader
volk
Vulkan Memory Allocator
glslangValidator または glslc（host tool）
```

Windows static buildでは:

- Vulkan loaderのstatic／dynamic方針を明示
- system Vulkan Loaderへのruntime依存を受け入れる場合、installer／READMEへ記載
- `vulkan-1.dll`をアプリへ同梱しない
- volkでfunction pointerをload
- MinGW UCRT64とvcpkg tripletでlink確認

Linuxでは:

- Vulkan loaderを通常のsystem dependencyとして扱う
- X11／Wayland surface extensionはQtのinstance拡張と一致させる
- build時にWayland development packageがない場合、X11 Vulkanまで失敗させない

macOSでは:

- MoltenVKを明示opt-in
- portability enumeration extension
- portability subset
- framework／dylibの配布形態を分離
- Metal native backendを既定優先とする

## 7.4 shader generation

`tools/vulkan/generate_spirv.py`を追加する。

機能:

- shader manifest読込
- variant展開
- compiler version記録
- SPIR-V生成
- generated C++ header生成
- hash埋め込み
- sourceとgeneratedの差分検出
- `--check` mode
- Windows／Linux／macOS host対応
- pathに空白があっても動作
- 失敗shader名とcompiler stderrを表示

## 7.5 empty shell

このPhaseでは、Vulkan runtimeを呼ばない空のsourceだけを追加する。

```text
VulkanSupport.h/.cpp
MelonPrimeVulkanFeatureCheck.h/.cpp
```

## 7.6 検証

```text
default OFF build
force disable build
enable Vulkan build
Windows MinGW build
Linux X11 build
Linux Wayland build
macOS default Metal build
macOS MoltenVK OFF build
shader generation
shader --check
binary strings
dependency inspection
```

force disabled binaryで次が存在しないことを確認する。

```text
Vulkan
vkCreateInstance
volkInitialize
VMA
MELONPRIME_FORCE_VULKAN
ScreenPanelVulkan
```

## 7.7 Exit Criteria

- Vulkan OFFで以前と同一のruntime
- Vulkan OFFでVulkan dependencyなし
- Vulkan ONでempty sourceがbuild
- shader generationが再現可能
- CIでgenerated file driftを検出
- UI変更なし

## 7.8 推奨commit

```text
build(vulkan): add complete Vulkan build gate and shader toolchain
```

---

# 8. Phase 2 ～ backend policy・安定enum・config正規化

## 8.1 目的

Vulkan rendererを追加しても、OpenGL context判定やwindow生成を誤らないpolicy層を作る。

## 8.2 `MelonPrimeVideoBackend`拡張

```cpp
enum class PresentationBackend
{
    NativeQt,
    OpenGL,
#if defined(MELONPRIME_ENABLE_METAL)
    Metal,
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    Vulkan,
#endif
};
```

追加API:

```cpp
bool RendererRequiresVulkanContext(int renderer);
bool IsVulkanPresentation(PresentationBackend backend);
bool RendererIsAvailableInBuild(int renderer);
int NormalizeRendererForPlatform(int requested);
```

## 8.3 bootstrap環境変数

UI公開前の開発専用:

```text
MELONPRIME_FORCE_VULKAN_PRESENTER=1
MELONPRIME_FORCE_VULKAN_RENDERER=1
MELONPRIME_FORCE_VULKAN_COMPUTE_RENDERER=1
```

正式UI公開後に削除する。

## 8.4 renderer factory

`EmuThread::updateRenderer()`へVulkanを追加する前に、factoryを専用関数へ寄せる。

推奨:

```cpp
std::unique_ptr<melonDS::Renderer> CreateRendererForSelection(
    melonDS::NDS& nds,
    int normalizedRenderer,
    BackendCreationReport& report);
```

report:

```cpp
struct BackendCreationReport
{
    int requested;
    int normalized;
    int actual;
    std::string failedStage;
    std::string fallbackReason;
};
```

upstream-owned`EmuThread.cpp`へ大きなswitchを増やさず、fork所有unitへ置く。

## 8.5 config migration

- enum値を明示値へ固定
- 既存0～4の意味を維持
- 5／6をVulkan用に予約
- out-of-rangeはSoftware
- Vulkan無効buildで5／6を別rendererとして解釈しない
- Cancel時は元値を安全に復元
- radio buttonがないIDでnullptr dereferenceしない

## 8.6 ログ

```text
[MelonPrime] video backend:
requested=VulkanCompute(6)
normalized=Vulkan(5)
actual=Vulkan(5)
presenter=Vulkan
reason=compute subgroup requirement missing
```

## 8.7 Exit Criteria

- UIなしでenv bootstrap選択可能
- Vulkan無効buildで5／6がSoftwareへ安全に正規化
- OpenGL contextをVulkanに対して作らない
- Metal contextをVulkanに対して作らない
- invalid configでcrashしない
- requested／normalized／actualが必ずログ化

## 8.8 推奨commit

```text
refactor(video): add stable renderer IDs and Vulkan backend policy
```

---

# 9. Phase 3 ～ QVulkanInstance・feature probe・device context

## 9.1 目的

rendererをまだ実装せず、Vulkan instance、surface、physical device、logical deviceを正しく生成する。

## 9.2 QVulkanInstance host

`MelonPrimeVulkanInstanceHost`を追加する。

責務:

- process内の`QVulkanInstance`生成
- Qtが要求するplatform surface extensionの取得
- validation layerの開発build有効化
- debug messenger
- portability enumeration
- instance function loader
- `QWindow::setVulkanInstance`
- instance lifetime管理
- instance破棄順序管理

原則:

- `QWidget::winId()`を直接VkSurfaceへ変換しない
- Qtの`surfaceForWindow(QWindow*)`を使用する
- platformごとの`vkCreateWin32SurfaceKHR`／Xlib／Wayland分岐をfrontendへ散らさない
- Qt surface lifecycle eventを監視する

## 9.3 feature probe

`MelonPrimeVulkanFeatureCheck`は、次を返す。

```cpp
struct VulkanFeatureInfo
{
    bool instanceAvailable;
    bool presentationAvailable;
    bool rasterRendererAvailable;
    bool computeRendererAvailable;
    bool timelineSemaphoreAvailable;
    bool validationAvailable;
    uint32_t apiVersion;
    uint32_t vendorId;
    uint32_t deviceId;
    std::string deviceName;
    std::string driverName;
    std::string unavailableReason;
};
```

## 9.4 raster必須機能

- graphics queue
- presentation support
- swapchain extension
- sampled image
- color attachment
- depth／stencil
- storage buffer
- uniform buffer
- independent blendまたは必要な代替
- fragment shader integer operation
- format feature確認
- minimum descriptor limits
- minimum buffer range
- minimum push constant size
- framebuffer extent
- max image dimensions

## 9.5 compute必須機能

OpenGL Computeの仕様をVulkanへ移すため、追加で確認する。

- compute queue
- storage buffer
- storage imageまたはbuffer-only代替
- indirect dispatch
- workgroup count
- workgroup size
- shared memory size
- shader int64が本当に必要かを事前監査
- subgroupを必須にしない
- descriptor binding数
- SSBO総量
- 最大allocation
- atomics要件

NVIDIA向け既存work group dimension問題を再発させないため、device limitを実測してdispatch分割を決める。

## 9.6 device selection score

候補deviceをscoreする。

```text
必須機能を満たさない → reject
surface present不可 → reject
discrete GPU → 加点
integrated GPU → 加点
compute専用queue → 小加点
timeline semaphore → 加点
preferred depth format → 加点
present mode MAILBOX → 加点
driver denylist → 減点またはreject
```

ユーザーが複数GPUを持つ場合に備え、将来のdevice selector用にstable identifierを保持する。

## 9.7 validation

開発build:

```text
VK_LAYER_KHRONOS_validation
GPU-assisted validationは任意
best practices
synchronization validation
debug utils object name
```

release build:

- validation layer必須にしない
- debug labelはbuild optionで残せる
- validationが存在しなくても起動する

## 9.8 Exit Criteria

- Windows、X11、Waylandでinstanceとsurface作成
- selected deviceとqueue familyをログ化
- unsupported理由が1行で説明可能
- create／destroyを10回繰り返してleakなし
- validation errorなし
- rendererはまだSoftware
- UIはまだ非公開

## 9.9 推奨commit

```text
feat(vulkan): add Qt Vulkan instance host and device capability probe
```

---

# 10. Phase 4 ～ Vulkan presenter CPU BGRA baseline

## 10.1 目的

Software rendererのCPU BGRAを、Vulkan swapchainで正しく表示する。

## 10.2 `ScreenPanelVulkan`

実装責務:

- surface取得
- swapchain作成
- swapchain image view
- render pass
- framebuffer
- descriptor set
- screen sampler
- CPU BGRA upload texture
- staging buffer ring
- command pool
- command buffer
- frames-in-flight
- acquire semaphore
- render-complete semaphore
- fence
- resize／minimize／restore
- fullscreen
- HiDPI
- screen move
- surface recreation
- device lost
- presentation error

## 10.3 thread model

GUI thread:

- widget／QWindow生成
- QVulkanInstance設定
- surface lifecycle通知
- drawable size更新要求
- window destroy通知

emu thread／draw thread:

- CPU framebuffer取得
- upload
- command encode
- queue submit
- queue present
- swapchain再作成の実行

GUI objectをemu threadから直接操作しない。  
GUI threadがatomic generation／pending resizeを更新し、draw threadが安全な境界で再構成する。

## 10.4 swapchain

format選択:

1. `B8G8R8A8_UNORM + SRGB_NONLINEAR`
2. `R8G8B8A8_UNORM + SRGB_NONLINEAR`
3. 明示fallback

present mode:

| 状態 | 優先 |
|---|---|
| VSync ON | FIFO |
| VSync OFF | MAILBOX |
| MAILBOXなし | IMMEDIATE |
| IMMEDIATEなし | FIFO_RELAXED |
| すべて不可 | FIFO |

VSync OFFでも、emulator側Frame Limitが別に残ることを前提とする。  
presenterが独立して60Hz制限を作っていないか確認する。

## 10.5 CPU upload

- persistently mapped staging ring
- row pitchを正しく扱う
- BGRA／RGBA swizzle
- top／bottomをtexture arrayまたは2 imageへ格納
- dirty rect upload
- Custom HUDの大領域が同一ならhashでupload skip
- splash／OSD／HUDの更新を含む

初期Phaseで最適化を優先しないが、毎frameのallocationは禁止する。

## 10.6 screen layout

`ScreenPanelGL`と同じlayout matrixを使う。

- vertical
- horizontal
- hybrid
- top only
- bottom only
- screen swap
- gap
- rotation
- integer scaling
- aspect ratio
- in-game top only
- fullscreen
- filter nearest／linear

Vulkan側で別のlayout計算を持たない。  
既存`ScreenLayout`結果をuniformへ変換する。

## 10.7 OSD／Custom HUD／radar／splash

最初から正式対象に含める。

- OSD texture cache
- text bitmap upload
- Custom HUD overlay
- dirty rect
- unchanged content hash
- bottom radar overlay
- no-ROM splash
- localization反映
- edit mode

「ゲーム画面だけ表示できる」をPhase完了としない。

## 10.8 swapchain semaphoreルール

swapchain imageごとのfence trackingを持つ。

```text
frame slot semaphore
swapchain image in-flight fence
acquire result
submit result
present result
```

`vkAcquireNextImageKHR`で使ったsemaphoreを、画像のpresent完了前に再利用しない。

## 10.9 Exit Criteria

- Software renderer + Vulkan presenterで全画面表示
- no-ROM splash
- OSD
- Custom HUD
- radar
- resize
- fullscreen
- minimize／restore
- screen move
- HiDPI
- multi-window初期smoke
- VSync ON／OFF
- Fast Forward
- validation errorなし
- 30分連続実行でswapchain hangなし

## 10.10 推奨commit

```text
feat(vulkan): add CPU BGRA Vulkan presenter baseline
```

---

# 11. Phase 5 ～ RendererOutput Vulkan型・output lease一般化

## 11.1 目的

rendererとpresenter間のVulkan GPU image handoffを型安全にする。

## 11.2 output kind

追加:

```cpp
enum class RendererOutputKind
{
    CpuBgra,
    OpenGLTextureArray,
#if defined(MELONPRIME_ENABLE_METAL)
    MetalTexture,
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    VulkanImage,
#endif
    None,
};
```

## 11.3 opaque descriptor

`VkImage`を直接`void*`へcastしない。

```cpp
struct VulkanRendererOutput
{
    VkImage image;
    VkImageView view;
    VkFormat format;
    VkExtent2D extent;
    VkImageLayout layout;
    uint32_t layerCount;
    uint32_t engineALayer;
    uint64_t frameSerial;
    uint64_t generation;
    VkSemaphore producerTimeline;
    uint64_t producerValue;
    void* leaseContext;
};
```

`RendererOutput.Top`には、このdescriptorへのpointerを入れる。  
descriptor自身はslotと同じ寿命を持つ。

## 11.4 lease一般化

現在Metal限定の`RendererOutputLease`を、native GPU backend共通へ一般化する。

例:

```cpp
#if defined(MELONPRIME_ENABLE_METAL) || defined(MELONPRIME_ENABLE_VULKAN)
#define MELONPRIME_NATIVE_GPU_OUTPUT_LEASE 1
#endif
```

ただしmacro追加より、常時存在する軽量RAII型にしてbackend-specific fieldだけ条件付きにする方が、将来の分岐を減らせる。  
upstream差分とbinary footprintを比較して決定する。

## 11.5 lease protocol

1. rendererがslotを完成
2. producer timelineをsignal
3. slotをPublishedへ遷移
4. presenterがAcquireOutputLease
5. presenter refs++
6. presenter submitがproducer timelineをwait
7. presenter command completionでrefs--
8. refs==0かつrenderer in-flight完了なら再利用可能

## 11.6 generation

次でgenerationを増やす。

- scale変更
- renderer切替
- device recreation
- savestate load
- reset
- swapchainとは独立したoffscreen image再作成
- format変更

旧generationのoutputはpresenterが捨てる。

## 11.7 regression

- CPU Native presenter
- OpenGL presenter
- Metal presenter
- Vulkan presenter
- Software renderer
- OpenGL renderer
- Metal renderer

すべての既存output kindを誤解釈しないこと。

## 11.8 Exit Criteria

- VulkanImage descriptorが型安全
- output slot reuse raceなし
- Metal lease regressionなし
- CPU／GL output regressionなし
- frame serialがproducerとpresenterで一致
- generation変更時にstale imageを表示しない

## 11.9 推奨commit

```text
refactor(renderer): add Vulkan output descriptors and shared output leases
```

---

# 12. Phase 6 ～ Vulkan renderer shell・Software correctness baseline

## 12.1 目的

`renderer3D_Vulkan`を実際に生成できるようにしつつ、描画の正しさはSoftwareを維持する。

## 12.2 `VulkanRenderer`

初期形:

```cpp
class VulkanRenderer : public SoftRenderer
{
public:
    explicit VulkanRenderer(NDS& nds, bool compute);
    bool Init() override;
    void SetRenderSettings(RendererSettings& settings) override;
    RendererOutput GetOutput() override;
    RendererOutputLease AcquireOutputLease() override;
};
```

このPhaseでは:

- Software 2D
- Software 3D
- Software capture
- CPU BGRA final
- Vulkan presenter

を使用する。

## 12.3 目的

この段階で確認するのは、renderer ID、lifecycle、config、presenter selection、fallbackである。  
native Vulkan 3Dを実装済みとは表現しない。

## 12.4 lifecycle

確認:

- Init
- Reset
- Stop
- PreSavestate
- PostSavestate
- SetRenderSettings
- VBlank
- VBlankEnd
- renderer switch
- ROM switch
- instance destroy
- window destroy
- multi-instance
- device lost

## 12.5 fallback

VulkanRenderer::Init失敗時:

```text
failed stage
VkResult
device
requested backend
actual fallback
```

fallback先:

1. OpenGL Classicへ自動fallbackするか
2. Softwareへ直接fallbackするか

推奨はSoftware。  
理由は、Vulkan presenter選択時にOpenGL contextがないためである。

## 12.6 Exit Criteria

- Vulkan renderer IDでROM起動
- Softwareとpixel一致
- Vulkan presenter表示
- renderer切替でcrashなし
- fallback理由あり
- no silent fallback
- native Vulkan 3D未実装とログ／文書で明記

## 12.7 推奨commit

```text
feat(vulkan): add safe Vulkan renderer shell on software rendering
```

---

# 13. Phase 7 ～ Vulkan Classic 3D correctness baseline

## 13.1 目的

`GLRenderer3D`の機能をVulkan raster pipelineへ完全移植する。

このPhaseでは2Dとfinal compositeをSoftware経路に残し、Vulkan 3Dを`Renderer3D::GetLine()`へreadbackして統合する。  
これにより3DだけをOpenGL Classicと比較できる。

## 13.2 実装順序

### Phase 7.1 native target

作成:

- color target
- depth／stencil target
- attr target
- native resolve target
- readback buffer
- command pool
- per-frame command buffer
- fence／timeline
- render pass
- framebuffer

scale:

```text
width = 256 * ScaleFactor
height = 192 * ScaleFactor
```

### Phase 7.2 clear plane

OpenGLのclear shaderと完全一致させる。

- RenderClearAttr1
- RenderClearAttr2
- clear color
- clear alpha
- clear polyID
- fog flag
- clear depth
- Z／W semantics
- color／attr／depth／stencil

### Phase 7.3 clear bitmap

- VRAM texture slot 2／3
- 256x256 repeat
- clear bitmap offset
- color
- polyID
- fog flag
- depth
- dirty tracking

### Phase 7.4 vertex upload

Vulkan用packed vertex layoutを定義する。

対象:

- FinalPosition
- HiresPosition
- FinalZ
- FinalW
- FinalColor
- TexCoords
- polygon attr
- texture param
- palette
- W-buffer flag
- facing
- line polygon
- texture layer

C++とshader layoutをstatic_assertする。

### Phase 7.5 polygon order

`RenderPolygonRAM`順序を保持する。

許可されるbatch:

- 隣接polygonのみ
- 同じpipeline key
- 同じtexture
- 同じsampler
- 同じZ／W
- 同じrender mode
- orderを変えない

frame全体のunordered groupingは禁止する。

### Phase 7.6 opaque

- Z-buffer
- W-buffer
- less
- less-equal
- depth write
- stencil polyID replace
- alpha test
- textureなし
- decal
- modulate
- toon
- highlight

### Phase 7.7 translucent

- blend equation
- same translucent polyID抑制
- stencil `0x40 | polyID`
- depth write条件
- fog attr color mask相当
- alpha test
- texture modes
- polygon order

VulkanにはOpenGLのattachment color mask相当がpipeline stateとして存在するため、fog参加／非参加ごとのpipeline variantを作る。

### Phase 7.8 shadow

2段階で移植する。

1. shadow mask polygon
   - stencil bit 7
   - depth fail operation
   - lower polyID保持

2. visible shadow polygon
   - same lower polyID除外
   - bit 7 remaining部分だけ描画
   - blended color
   - stencil更新

### Phase 7.9 toon／highlight

- 32-entry toon table
- blendmode==2
- toon mode
- highlight mode
- textureあり／なし
- vertex color変換
- alpha維持

### Phase 7.10 edge marking

- attr polyID
- edge flag
- neighbor depth
- neighbor polyID
- RenderEdgeTable
- antialiasing時half alpha
- viewport edge

### Phase 7.11 fog

- RenderFogOffset
- RenderFogShift
- RenderFogDensityTable
- RenderFogColor
- color fog
- alpha-only fog
- translucent fog participation

### Phase 7.12 line polygon

- `Polygon::Type == 1`
- duplicate vertex除外
- first two valid vertices
- Vulkan line rasterization差を検証
- wideLinesを要求しない
- DS期待値と一致しないdriverではtriangle化fallbackを持つ

### Phase 7.13 Better Polygons

OpenGL Classicと同じcenter vertex triangulationを移植する。

- 4頂点以上
- center color
- center texture coordinate
- center depth
- vertex count
- line polygon非対象
- setting OFF時は従来fan

### Phase 7.14 GetLine readback

- render完了をtimelineで待つ
- native 256x192 resolve
- BGRA8からDS 6-bit color＋alphaへ変換
- RenderXPos
- line pointer
- frame serial
- CPU readbackはこのPhaseの正解確認用

## 13.3 pipeline key

例:

```cpp
struct VulkanRasterPipelineKey
{
    bool wBuffer;
    RenderMode mode;
    TextureMode textureMode;
    bool toon;
    bool highlight;
    bool depthEqual;
    bool depthWrite;
    bool fogAttrWrite;
    bool shadowStage;
    VkFormat colorFormat;
    VkFormat depthStencilFormat;
};
```

pipeline cacheは固定arrayまたはunordered mapで保持し、毎frame生成しない。

## 13.4 shader variant爆発対策

- sourceは共通化
- specialization constantを使用
- pipeline state差が必要なものだけ別pipeline
- branchが安いものはuniform
- shader moduleは再利用
- pipeline作成はInit／settings変更時のみ

## 13.5 pixel diff

比較:

```text
Vulkan 3D native resolve
OpenGL Classic color buffer
Software final GetLine
```

判定:

- integer／fixed-point部分は原則pixel exact
- format conversionで1 LSB以内の差は個別記録
- 大規模差をtoleranceで隠さない
- alpha、depth、attrを別々に比較
- 最初に異なるpassをログ化

## 13.6 Exit Criteria

- 全3D機能がOpenGL Classicと一致
- 1x／2x／4x／8x
- Better Polygons ON／OFF
- fog／edge／shadow
- clear bitmap
- line polygon
- translucent
- validation errorなし
- 30分プレイでdevice lossなし
- CPU readback経路で正しい
- 2D／finalはまだSoftwareでもよい

## 13.7 推奨commit分割

```text
feat(vulkan): add Vulkan 3D targets and clear passes
feat(vulkan): port opaque polygon rendering
feat(vulkan): port translucent and shadow stencil paths
feat(vulkan): port toon highlight fog and edge passes
feat(vulkan): add lines BetterPolygons and GetLine integration
```

---

# 14. Phase 8 ～ Vulkan texture cache・display capture・savestate・scale

## 14.1 目的

OpenGL Classicのtexture／capture lifecycleをVulkanへ完全移植する。

## 14.2 `TexcacheVulkan`

責務:

- DS texture key
- texture format decode
- palette decode
- VRAM dirty判定
- image allocation
- staging upload
- sampler selection
- repeat／mirror／clamp
- capture-backed texture
- invalidation
- reset
- savestate
- renderer switch
- memory budget

## 14.3 sampler table

OpenGL Computeと同様に、S／T各3モードの9samplerを事前作成する。

```text
clamp
repeat
mirror
```

filterはnearest。

## 14.4 texture arrayか個別imageか

初期方針:

- 個別image + descriptor set cache
- descriptor indexingを必須にしない
- texture layerを無理に巨大arrayへ詰めない
- capture textureは専用image

性能Phaseでdescriptor indexing対応deviceのみbindlessを検討する。

## 14.5 upload ring

- persistently mapped staging buffer
- frameごとのsuballocation
- non-coherent atom sizeに従ってflush
- alignment
- wrap時のfence wait
- upload byte数を計測
- dirtyでないtextureはuploadしない

## 14.6 display capture

完全対応:

- source A 2D／3D
- source B VRAM／FIFO
- blend factors
- 128／256 width
- destination VRAM bank／offset
- partial line capture
- capture outputを後続textureとして使用
- CPUがVRAMを読む場合のsync
- GPU capture imageからCPU VRAMへのreadback
- renderer switch時のflush
- savestate前のflush

## 14.7 savestate

`PreSavestate`:

- in-flight render完了
- GPU-only captureをCPU VRAMへ同期
- stale mapping解消

`PostSavestate`:

- texture cache reset
- capture state reset
- output generation++
- scale resource再構成
- first frameを必ずfull upload

## 14.8 scale live変更

1xから16xまでを既存UIと一致させる。

- GPU memory不足時の明確な失敗
- 旧resourceをpresenter lease中に破棄しない
- new generation
- pipelineは必要なものだけ再利用
- compute tile設定は別Phase

## 14.9 Exit Criteria

- 全texture format
- repeat／mirror
- clear bitmap
- display capture
- captureをtextureとして再利用
- savestate save／load
- reset
- renderer switch
- scale live変更
- validation errorなし
- leakなし
- CPU readback同期が正しい

## 14.10 推奨commit

```text
feat(vulkan): add Vulkan texture cache and display capture synchronization
```

---

# 15. Phase 9 ～ Vulkan 2D・final composition・GPU resident output

## 15.1 目的

OpenGL parent rendererの2D、display mode、capture、final passをVulkanへ移植し、通常frameのCPU readbackを廃止する。

## 15.2 Vulkan 2D

`GPU2D_OpenGL.cpp`を参照し、次を移植する。

- BG
- OBJ
- window
- blend
- mosaic
- brightness
- 3D layer
- per-scanline state
- layer output
- screen A／B
- partial render
- capture source

最初から全機能を一括でnative化せず、次の順にする。

1. Software 2D upload + Vulkan final
2. Vulkan 2D mirror
3. Vulkan 2D visible
4. Software 2D comparison mode削除

## 15.3 final pass

OpenGLの`FinalPassConfig`と同じ情報をVulkan uniformへ渡す。

```text
uScreenSwap[192]
uScaleFactor
uAuxLayer
uDispModeA
uDispModeB
uBrightModeA
uBrightModeB
uBrightFactorA
uBrightFactorB
uAuxColorFactor
```

対応:

- top／bottom
- ScreenSwap per line
- 2D output
- 3D output
- VRAM display mode
- FIFO display mode
- Master Brightness
- screen disable
- scale
- capture image

## 15.4 partial render

OpenGLの`NeedPartialRender`／`LastLine`相当を維持する。

状態変化:

- DispCnt
- MasterBrightness
- CaptureCnt
- ScreenSwap
- display mode
- capture source

状態が変わるlineまで前区間をflushする。

## 15.5 GPU resident output

最終出力:

```text
2-layer VkImage
layer 0 = top
layer 1 = bottom
```

または、Engine A layer mappingをdescriptorに保持する。

条件:

- presenterが直接sample可能
- final imageはtransfer sourceにもできる
- debug readback可能
- output ring slotごとに独立
- renderer queueからpresenter queueへtimeline同期

## 15.6 ownership

CPU画面と3D targetの世代を混ぜない。

snapshot:

```text
frame serial
3D target serial
2D final serial
capture serial
ScreenSwap
MasterBrightness
scale
engine layer
```

不一致時は公開せず、前のcompleted frameを維持するかCPU fallbackへ明示的に切り替える。

## 15.7 comparison mode

開発用:

```text
MELONPRIME_VULKAN_2D_SOURCE=software
MELONPRIME_VULKAN_2D_SOURCE=vulkan
MELONPRIME_VULKAN_2D_DIFF=1
MELONPRIME_VULKAN_FINAL_DIFF=1
```

正式公開前にenv依存を削除するかdeveloper build限定にする。

## 15.8 Exit Criteria

- 通常frameでCPU readbackなし
- OpenGL Classic final outputとpixel一致
- HUD／reticle／window／blendを高解像度3Dが上書きしない
- per-line ScreenSwap
- Master Brightness
- VRAM／FIFO display
- display capture
- menu／暗転／画面切替
- frame serial一致
- validation errorなし

## 15.9 推奨commit分割

```text
feat(vulkan): add Vulkan final composition over software 2D
feat(vulkan): port DS 2D rendering and display modes
feat(vulkan): publish GPU-resident top and bottom output
```

---

# 16. Phase 10 ～ zero-copy presenter・ring・multi-window

## 16.1 目的

Vulkan rendererのfinal imageを、CPU copyなしでVulkan presenterへ表示する。

## 16.2 output ring

最低3slot。

```cpp
struct VulkanOutputSlot
{
    VulkanRendererOutput output;
    bool rendererInFlight;
    uint32_t presenterRefs;
    uint64_t frameSerial;
    uint64_t generation;
    uint64_t producerValue;
};
```

slot選択:

```text
rendererInFlight == false
presenterRefs == 0
generation == current
```

空きがない場合:

- 無制限allocationしない
- 最古のproducer完了slotを待つ
- presenterがhangしている場合はdiagnostic timeout
- frame drop方針を明示する

## 16.3 layout

公開時layoutを固定する。

推奨:

```text
VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
```

renderer次frameでattachmentへ戻す時:

```text
SHADER_READ_ONLY_OPTIMAL
→ COLOR_ATTACHMENT_OPTIMAL
```

queue familyが異なる場合はownership transferを含める。

## 16.4 presenter

VulkanImage時:

- CPU uploadを行わない
- descriptorのproducer timelineをwait
- final imageをsample
- layout transform
- filter
- OSD
- Custom HUD
- radar
- splash
- swapchainへrender
- presenter completionでlease release

CpuBgra時:

- Phase 4のupload経路を維持

## 16.5 multi-window

各windowが同じoutput slotのleaseを取得できる。

```text
Window A refs++
Window B refs++
A complete refs--
B complete refs--
refs==0で再利用可能
```

window閉鎖時:

- pending command completion
- refs release
- swapchain destroy
- surface destroy
- renderer deviceを先にdestroyしない

## 16.6 frame pacing

- emulator frame production
- renderer GPU completion
- presenter acquire
- present
- display sync

を別々に計測する。

presenterがVSync OFF時にもemulation速度を固定しないこと。  
Fast Forward中はpresenterが古いframeを適切にdropできる設計を検討する。

## 16.7 Exit Criteria

- steady state CPU copy 0
- output slot race 0
- N／N+1混合 0
- multi-window
- window close中raceなし
- scale変更
- renderer switch
- fullscreen
- VSync
- Fast Forward
- 60分連続実行
- validation／sync validation errorなし

## 16.8 推奨commit

```text
feat(vulkan): add zero-copy renderer-to-presenter output leases
```

---

# 17. Phase 11 ～ Vulkan Compute Shader renderer完全移植

## 17.1 目的

OpenGL Compute Shader rendererの全処理をVulkan computeへ移植する。

最終UI名:

```text
Vulkan Compute Shader
```

## 17.2 直接翻訳しないもの

OpenGL固有:

- GL buffer binding point
- GL texture buffer
- runtime GLSL string連結
- GL indirect dispatch buffer layout
- implicit memory ordering
- GL framebuffer object
- GL sampler object binding
- GL state transition

Vulkanでは、同じアルゴリズムを次へ明示変換する。

- descriptor set
- storage buffer
- storage image
- pipeline barrier
- indirect dispatch buffer
- specialization constant
- pipeline layout
- command buffer
- timeline semaphore

## 17.3 compute stage一覧

OpenGLの33 compile stepを基準に、Vulkanでは次を持つ。

```text
InterpSpans Z
InterpSpans W
BinCombined
DepthBlend Z
DepthBlend W
Rasterise NoTexture Z
Rasterise NoTexture W
Rasterise NoTexture Toon Z
Rasterise NoTexture Toon W
Rasterise NoTexture Highlight Z
Rasterise NoTexture Highlight W
Rasterise Texture Decal Z
Rasterise Texture Decal W
Rasterise Texture Modulate Z
Rasterise Texture Modulate W
Rasterise Texture Toon Z
Rasterise Texture Toon W
Rasterise Texture Highlight Z
Rasterise Texture Highlight W
Rasterise ShadowMask Z
Rasterise ShadowMask W
ClearCoarseBinMask
ClearIndirectWorkCount
CalculateWorkOffsets
SortWork
FinalPass
FinalPass Edge
FinalPass Fog
FinalPass Edge+Fog
FinalPass AA
FinalPass AA+Edge
FinalPass AA+Fog
FinalPass AA+Edge+Fog
```

Vulkanではshader moduleを33個必ず作る必要はない。  
source／SPIR-V moduleを共有し、specialization constantとpipeline variantを使う。

## 17.4 buffer構造

移植対象:

```text
SpanSetupY
SpanSetupX
SetupIndices
RenderPolygon
BinResultHeader
YSpanIndices
YSpanSetups
XSpanSetups
BinResult
WorkDesc
TileMemory Color
TileMemory Depth
TileMemory Attr
FinalTileMemory
MetaUniform
```

必須:

- C++ `sizeof`
- C++ `alignof`
- field offset
- GLSL std430 offset
- SPIR-V reflection結果

をCIで比較する。

`bool`をshader共有structへ入れない。  
pointerを入れない。  
platform依存paddingを許さない。

## 17.5 specialization constants

候補:

```text
ScreenWidth
ScreenHeight
MaxWorkTiles
TileSize
CoarseTileCountY
CoarseTileArea
ClearCoarseBinMaskLocalSize
ZBuffer／WBuffer
texture mode
toon／highlight
final pass flags
```

scale変更ごとに全shaderをruntime compileしない。  
必要なpipelineをcacheする。

## 17.6 dispatch limit

既存NVIDIA対策を維持し、次をdevice limitから計算する。

```text
maxComputeWorkGroupCount[0]
maxComputeWorkGroupCount[1]
maxComputeWorkGroupCount[2]
maxComputeWorkGroupInvocations
maxComputeSharedMemorySize
```

real work countを保持し、Y／Zへ安全に分割する。  
「Vulkanなら制限が消える」と仮定しない。

## 17.7 barriers

各stage間に必要なdependencyを明示する。

例:

```text
CPU staging write
→ transfer copy
→ compute shader read

ClearIndirectWorkCount
→ BinCombined

BinCombined
→ CalculateWorkOffsets

CalculateWorkOffsets
→ SortWork

SortWork
→ Rasterise

Rasterise
→ FinalPass

FinalPass
→ final composition sample
```

`vkCmdPipelineBarrier`またはoptional `vkCmdPipelineBarrier2`を使用する。  
validationが通るだけでなく、access maskが実データflowと一致すること。

## 17.8 texture

- Vulkan Texcache
- 9 sampler
- capture texture
- clear bitmap
- texture mode
- palette
- integer fetch
- exact fixed-point conversion

## 17.9 High Resolution Coordinates

OpenGL Computeの設定を完全移植する。

- FinalPosition
- HiresPosition
- scale
- clipping
- span setup
- tile coordinates
- screen edge
- line polygon
- clear offset

## 17.10 visible output

初期:

```text
compute result
→ Vulkan final composition
→ Vulkan output ring
→ Vulkan presenter
```

raster referenceを可視出力に使い続けてはならない。  
比較用mirrorとして保持する場合、developer-onlyとする。

## 17.11 parity

比較対象:

- OpenGL Compute
- Vulkan Compute
- OpenGL Classic
- Vulkan Raster

Compute固有:

- 1x
- 2x
- 4x
- 8x
- 16x
- HiresCoordinates OFF／ON
- max polygon
- max span
- high resolution particle effect
- screen edge effect
- shadow
- fog
- edge
- AA
- clear bitmap
- capture texture
- large work count

## 17.12 Exit Criteria

- 33 stage相当の全機能
- visible outputがVulkan Compute自身
- OpenGL Computeとpixel parity
- NVIDIA、AMD、Intel
- work group overflowなし
- mosaic／画面端破損なし
- HiresCoordinates
- 16xでmemory errorを明確に処理
- validation errorなし
- 60分連続実行
- renderer switch
- savestate
- scale live変更

## 17.13 推奨commit分割

```text
feat(vulkan-compute): port span setup and binning
feat(vulkan-compute): port raster variants and texture sampling
feat(vulkan-compute): port depth shadow fog edge and AA final passes
feat(vulkan-compute): expose native compute output with high-resolution coordinates
```

---

# 18. Phase 12 ～ UI・設定・プリセット・多言語

## 18.1 目的

Vulkan RasterとVulkan Computeを、対応環境でのみ安全に公開する。

## 18.2 Video Settings

`VideoSettingsDialog.cpp`へ動的に追加する。

表示名:

```text
Vulkan
Vulkan Compute Shader
```

生成条件:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
```

さらにfeature probeでenable状態を決める。

```text
Vulkan instance不可 → 両方disabled
presentation不可 → 両方disabled
raster要件不足 → Vulkan disabled
compute要件不足 → Vulkan Computeだけdisabled
```

tooltipには具体理由を表示する。

悪い例:

```text
Unsupported
```

良い例:

```text
Vulkan Compute Shader is unavailable because the selected GPU exposes
only 32,768 storage-buffer bytes per stage; this backend requires 65,536 bytes.
```

## 18.3 option enable条件

| option | Software | OpenGL | OpenGL Compute | Metal | Metal Compute | Vulkan | Vulkan Compute |
|---|---:|---:|---:|---:|---:|---:|---:|
| Internal Resolution | いいえ | はい | はい | はい | はい | はい | はい |
| Better Polygons | いいえ | はい | いいえ | はい | はい※ | はい | いいえ※ |
| High Resolution Coordinates | いいえ | いいえ | はい | はい※ | はい | 実装時のみ | はい |
| Software Threaded | はい | いいえ | いいえ | いいえ | いいえ | いいえ | いいえ |
| OpenGL Display | Software時のみ | N/A | N/A | N/A | N/A | N/A | N/A |
| VSync | presenter能力に基づく | 同左 | 同左 | 同左 | 同左 | 同左 | 同左 |

`※`は実装済みであることをconsumerまで確認してからenableする。

## 18.4 config key

既存の共通hardware renderer設定を可能な限り再利用する。

```text
3D.GL.ScaleFactor
3D.GL.BetterPolygons
3D.GL.HiresCoordinates
```

ただし名前がOpenGL固有で誤解を生むため、長期的には次へmigrationする。

```text
3D.Hardware.ScaleFactor
3D.Hardware.BetterPolygons
3D.Hardware.HiresCoordinates
```

migration手順:

1. 新keyが存在すれば新key
2. 旧GL keyが存在すれば移行
3. default
4. 保存時は新key
5. 旧buildとの往復互換が必要なら一定期間dual write
6. migration完了後に旧key削除

このmigrationはVulkan renderer実装commitと混ぜない。

## 18.5 MelonPrime VIDEO QUALITY preset

既存:

```text
Low
High
High2
```

既存の意味は変更しない。

```text
Low   = Software
High  = OpenGL Classic
High2 = OpenGL Compute Shader
```

Vulkan用は新規に追加する。

候補:

```text
High Vulkan
High Vulkan Compute
```

またはbackend selectorを別にし、quality presetとrenderer選択を分離する。

推奨は後者。

```text
Rendering Backend:
Auto
OpenGL
OpenGL Compute Shader
Metal
Metal Compute Shader
Vulkan
Vulkan Compute Shader

Video Quality:
Low
High
Very High
```

ただし既存UI改修が大きいため、初回公開ではVulkan専用2ボタン追加でもよい。

Vulkan Raster preset:

```text
renderer = Vulkan
scale = 4x
Better Polygons = ON
High Resolution Coordinates = OFF
VSync = OFF
```

Vulkan Compute preset:

```text
renderer = Vulkan Compute Shader
scale = 4x
Better Polygons = OFF
High Resolution Coordinates = ON
VSync = OFF
```

すべての関連値を明示的に書き、以前の設定を残さない。

## 18.6 platform表示

Windows／Linux:

- Vulkan build有効なら生成
- feature probeでenable

macOS:

- MoltenVK build有効時のみ生成
- Metal項目の下へ配置
- tooltipへMoltenVKであることを表示
- Metalを推奨backendとして維持
- OpenGL Compute macOS制限を変更しない

## 18.7 localization

全対応言語へ追加する。

対象文字列:

```text
Vulkan
Vulkan Compute Shader
Native Vulkan raster renderer.
Vulkan compute-shader renderer.
Vulkan is unavailable on this system.
The current GPU does not support the required Vulkan features.
Internal 3D render scale used by OpenGL, Metal, and Vulkan hardware renderers.
```

確認:

- dynamic language switch
- tooltip再設定
- localization後のenable理由保持
- RTL
- button clipping
- technical backend名を不自然に翻訳しない
- menu説明中の実メニュー名一致

## 18.8 Exit Criteria

- 対応deviceだけ操作可能
- 非対応理由が具体的
- Cancelで元renderer復元
- unknown renderer IDでcrashなし
- 全言語
- presetが全関連値を設定
- macOS制限を維持
- Vulkan無効buildにUI文字列なし

## 18.9 推奨commit

```text
feat(ui): expose Vulkan renderers with capability-aware localization
```

---

# 19. Phase 13 ～ VSync・frame pacing・device loss・pipeline cache

## 19.1 目的

「描画できる」から「日常利用で安定している」状態へ進める。

## 19.2 VSync

確認対象:

```text
Screen.VSync
Screen.VSyncInterval
Frame Limit
Fast Forward Hold
Fast Forward Toggle
Slow Motion Hold
Slow Motion Toggle
audio sync
swapchain present mode
present acquire blocking
```

規則:

```text
通常:
    configured VSyncに従う

Fast Forward／Slow Motion:
    presenter側同期が速度変更を妨げないようpresent modeまたはframe dropを調整

速度変更終了:
    configured VSyncへ復帰
```

swapchain present mode変更にはswapchain再作成が必要なため、毎key eventで即再作成しない。  
高速化中は既存MAILBOX／IMMEDIATE swapchainを使うか、present frame dropを選ぶ。

## 19.3 frame drop

Fast Forwardでemulationがpresentより速い場合:

- latest completed outputのみ表示
- 古いunpresented frameをskip
- leaseを正しくrelease
- rendererをswapchain acquireでblockしない
- audio／input timingを変えない

## 19.4 device loss

`VK_ERROR_DEVICE_LOST`時:

1. 失敗stageログ
2. queue submit／present／fenceのどこかを記録
3. GPU crash dumpが利用可能なら保存
4. Vulkan rendererを停止
5. Vulkan resourceを再利用しない
6. Softwareへfallback
7. window presenterをNativeQtまたはCPU Vulkan presenterへ再構成
8. OSDで一度だけ通知
9. config値を勝手に上書きしない

device recreationは別Phaseのoptional機能とし、初回は安全fallbackを優先する。

## 19.5 pipeline cache

保存情報:

```text
vendorID
deviceID
driverVersion
pipelineCacheUUID
shader manifest hash
backend version
```

一致しないcacheは破棄する。

場所:

```text
cache/vulkan/<device-id>/<hash>.bin
```

書き込み:

- app終了時
- pipeline生成完了時のdebounce
- crash中に破損させないatomic rename

## 19.6 stutter

- UI公開前に必要pipelineをprewarm
- compile progress UIを既存shader compile flowへ統合
- pipeline creation feedbackがあればoptional使用
- gameplay中の初回pipeline生成を記録
- variant missing時はcrashではなく明確なerror

## 19.7 Exit Criteria

- VSync ON／OFF
- Frame Limit ON／OFF
- Fast Forward／Slow Motion
- audio sync
- present mode切替
- frame drop
- pipeline cache cold／warm
- device lost injection
- fallback
- no deadlock
- no stale output

## 19.8 推奨commit

```text
fix(vulkan): integrate presentation pacing device-loss fallback and pipeline cache
```

---

# 20. Phase 14 ～ 性能最適化

## 20.1 目的

正しさを維持したままOpenGLと同等以上の性能を目指す。

## 20.2 計測項目

```text
CPU emulation frame time
renderer CPU encode time
GPU 3D time
GPU 2D time
capture time
final composition time
presenter GPU time
queue wait
fence wait
swapchain acquire wait
texture upload bytes
HUD upload bytes
readback bytes
descriptor update count
pipeline bind count
draw call count
dispatch count
barrier count
allocation count
VRAM use
staging ring use
pipeline cache hit
output slot stall
```

## 20.3 禁止

正しさ確認前に次を行わない。

- barrier削除
- fence削除
- queue wait削除
- output slot無制限再利用
- polygon order変更
- draw batchの全frame regroup
- CPU state snapshot省略
- texture dirty判定省略
- capture sync省略

## 20.4 optimization候補

### CPU

- pipeline key事前計算
- descriptor set cache
- adjacent batch
- command buffer再利用
- secondary command bufferは計測後
- dynamic allocation排除
- per-frame config lookup排除
- per-frame QString排除
- per-frame log排除
- packed upload buffer
- dirty textureだけdecode

### GPU

- transient attachment
- memory aliasing
- dedicated transfer queue
- async compute
- compute／graphics overlap
- timeline semaphore
- attachment storeOp削減
- render pass merge
- subpass
- input attachment
- descriptor indexing optional
- dynamic rendering optional
- synchronization2 optional
- shader specialization
- subgroup optional
- pipeline library optional

### Presenter

- zero-copy
- latest-frame selection
- unchanged HUD upload skip
- partial upload
- MAILBOX
- swapchain image count調整
- acquire timeout方針
- present queue分離

## 20.5 vendor別確認

NVIDIA:

- work group count
- indirect dispatch
- validationとdriver behavior差
- descriptor update
- present mode

AMD:

- storage image format
- barriers
- pipeline cache
- depth／stencil

Intel:

- integrated memory
- host-visible device-local memory
- transfer overhead
- descriptor limits
- tile memory pressure

MoltenVK:

- portability subset
- storage image
- depth／stencil
- push constant
- argument buffer変換
- pipeline compile stutter

## 20.6 性能目標

最低目標:

- 1xでOpenGL Classicより著しく遅くない
- 4xでVulkan RasterがOpenGL Classic同等以上
- Vulkan ComputeがOpenGL Computeと同等以上
- steady-state allocation 0
- steady-state shader／pipeline compile 0
- steady-state CPU readback 0
- output slot stallが通常0
- Fast Forward速度をpresenterが制限しない
- frame pacingの99th percentile悪化なし

## 20.7 Exit Criteria

- benchmark report
- vendor別結果
- optimization前後比較
- pixel hash不変
- validation errorなし
- regression matrix pass
- 使用していないoptional pathを削除

## 20.8 推奨commit

```text
perf(vulkan): optimize command submission uploads and renderer frame pacing
```

---

# 21. Phase 15 ～ CI・配布・validation・MoltenVK

## 21.1 CI matrix

### Windows

```text
Windows 2025
MSYS2 UCRT64
MinGW
vcpkg static release
Vulkan OFF
Vulkan ON
force disable
shader regeneration check
```

### Linux

```text
Ubuntu
X11
Wayland build
Vulkan OFF
Vulkan ON
Mesa lavapipe smoke
validation layer
shader regeneration check
```

lavapipeはcompile／API smoke用であり、性能合格判定には使わない。

### macOS

```text
Metal default
Vulkan OFF
MoltenVK opt-in build
Intel可能ならbuild
Apple Silicon build
```

## 21.2 CI test

- configure
- compile
- link
- generated shader check
- binary string check
- dependency check
- feature probe unit test
- renderer enum test
- config normalization test
- output lease test
- swapchain semaphore state test
- synthetic image test
- headless offscreen render test
- pixel diff
- validation log zero

## 21.3 runtime hardware matrix

最低:

| OS | vendor | Vulkan Raster | Vulkan Compute |
|---|---|---:|---:|
| Windows | NVIDIA | 必須 | 必須 |
| Windows | AMD | 必須 | 必須 |
| Windows | Intel | 必須 | 必須 |
| Linux X11 | Mesa AMD | 必須 | 必須 |
| Linux X11 | Mesa Intel | 必須 | 必須 |
| Linux Wayland | Mesa | 必須 | 必須 |
| macOS MoltenVK | Apple Silicon | Tier 2 | Tier 2 |
| macOS MoltenVK | Intel | 可能なら | 可能なら |

## 21.4 validation tools

- Vulkan Validation Layers
- synchronization validation
- RenderDoc
- apitraceはVulkanでは補助
- Nsight Graphics
- Radeon GPU Profiler
- Intel Graphics Performance Analyzers
- Xcode GPU CaptureはMoltenVK時の補助
- AddressSanitizer
- UndefinedBehaviorSanitizer
- ThreadSanitizerはCPU lifetime部分

## 21.5 package

Windows:

- Vulkan loaderはOS／driver提供を使用
- 必要なruntime dependencyを明記
- shaderはbinary埋め込み
- validation layer非同梱

Linux:

- Vulkan loader dependency
- AppImage／portable時のdriver ICDを同梱しない
- hostのICDを使用
- X11／Wayland plugin確認

macOS:

- MoltenVK library／framework
- license
- code signing
- bundle path
- rpath
- portability enumeration
- Metal backendとの選択

## 21.6 default ON移行

初回release:

```text
MELONPRIME_ENABLE_VULKAN=OFF
UIはVulkan buildでのみ表示
```

次release候補:

```text
Windows／Linuxでbuild default ON
renderer defaultは既存のまま
```

さらに後:

```text
Auto backendでVulkanを候補にする
```

次を満たすまで既定rendererにしない。

- 3 vendor
- Windows／Linux
- pixel parity
- device lost
- multi-window
- savestate
- capture
- VSync／Fast Forward
- 2 release cycle
- blocking issue 0

## 21.7 Exit Criteria

- 全CI pass
- 全hardware matrix pass
- validation error 0
- packaging確認
- license確認
- release note
- fallback確認
- Metal／OpenGL regressionなし

## 21.8 推奨commit

```text
ci(vulkan): validate package and test Vulkan backends across platforms
```

---

# 22. Phase 16 ～ 文書統合・force path削除・正式完了

## 22.1 目的

開発用bootstrapを整理し、CLAUDE.mdと実装状態を一致させる。

## 22.2 更新対象

```text
CLAUDE.md
.claude/rules/melonprime-vulkan-backend-plan.md
.claude/rules/repo-architecture.md
.claude/rules/non-melonprime-upstream-diff.md
.claude/rules/build.md
.claude/rules/project-context.md
.claude/skills/build-windows-mingw.md
README.md
release notes
```

## 22.3 削除対象

正式UIが完成したら、原則削除する。

```text
MELONPRIME_FORCE_VULKAN_PRESENTER
MELONPRIME_FORCE_VULKAN_RENDERER
MELONPRIME_FORCE_VULKAN_COMPUTE_RENDERER
temporary raster mirror
temporary software visible source
temporary CPU mandatory readback
temporary diagnostic UI
obsolete markers
```

developer-only診断として残す場合:

- `MELONPRIME_ENABLE_DEVELOPER_FEATURES`
- 文書化
- release build非表示
-通常ユーザーconfigへ保存しない

## 22.4 完了記録

各Phaseへ記録する。

```text
Status
Commit
Build verified
Runtime verified
Hardware
Validation
Unverified
Known limitation
Rollback
```

「build成功」と「runtime成功」を分ける。

## 22.5 Exit Criteria

- 文書とsourceが一致
- force path不要
- temporary fallbackなし
- dead codeなし
- obsolete markerなし
- source inventory更新
- upstream diff更新
- Definition of Done完全達成

## 22.6 推奨commit

```text
docs(vulkan): finalize Vulkan backend implementation and maintenance contract
```

---

# 23. UI設計詳細

# 23.1 renderer配置

Video Settingsの推奨順:

```text
Software
OpenGL
OpenGL Compute Shader
Vulkan
Vulkan Compute Shader
Metal
Metal Compute Shader
```

macOSでは:

```text
Software
OpenGL
OpenGL Compute Shader（disabled）
Metal
Metal Compute Shader
Vulkan（MoltenVK buildのみ）
Vulkan Compute Shader（MoltenVK buildのみ）
```

# 23.2 tooltip

Vulkan:

```text
Vulkanを使用するネイティブGPUラスタライザです。
内部解像度、ポリゴン分割の改善、display capture、
fog、edge marking、shadowをサポートします。
```

Vulkan Compute Shader:

```text
Vulkan compute shaderを使用してDSの3Dラスタライズを行います。
内部解像度と高解像度座標をサポートします。
```

feature不足:

```text
このGPUではVulkan Compute Shaderを使用できません。
不足機能: maxComputeWorkGroupCountY、storage buffer range
```

# 23.3 Auto backend

将来追加する場合の優先例:

Windows／Linux:

```text
Vulkan Compute
Vulkan Raster
OpenGL Compute
OpenGL Classic
Software
```

macOS:

```text
Metal Compute
Metal Raster
Vulkan Compute through MoltenVK
Vulkan Raster through MoltenVK
OpenGL Classic
Software
```

ただしAutoは、rendererごとの完全parity後に別Phaseとして追加する。

# 23.4 設定変更時のwindow再構成

renderer変更でpresentation backendが変わる場合:

```text
pause emulation
drain old renderer
release old output leases
destroy old panel resources
GUI threadでpanel再生成
create new presenter
create renderer
restore settings
resume
```

失敗時:

```text
Software + NativeQt
```

へ安全に戻す。  
old rendererを破棄した後にfallback失敗してwindowが消える状態を作らない。

---

# 24. shaderとC++ data layout契約

## 24.1 原則

全共有structに次を行う。

```cpp
static_assert(sizeof(Struct) == expected);
static_assert(offsetof(Struct, field) == expected);
```

SPIR-V reflectionから自動生成したlayout manifestと比較する。

## 24.2 禁止型

shader共有structでは禁止:

```text
bool
size_t
pointer
enum class without fixed underlying type
long
platform-dependent alignment
std::array nested layoutを未確認で使用
```

推奨:

```text
uint32_t
int32_t
float
alignas(16)
fixed-size raw array
```

## 24.3 endianness

対象platformはlittle endianを前提にできるが、packed DS texture decodeのbit layoutは明示する。  
host structのreinterpret castだけに依存しない。

## 24.4 specialization constant ID

IDをmanifestで固定する。

```text
0 ScreenWidth
1 ScreenHeight
2 ScaleFactor
3 TileSize
4 MaxWorkTiles
5 UseWBuffer
6 TextureMode
7 ToonMode
8 FinalPassFlags
```

ID変更はshader cache versionを上げる。

---

# 25. synchronization契約

## 25.1 resource state table

各resourceについて文書化する。

| resource | producer | consumer | write stage | read stage | lifetime |
|---|---|---|---|---|---|
| vertex upload | CPU／transfer | graphics vertex | host／transfer | vertex input | frame |
| texture upload | transfer | fragment／compute | transfer | shader read | cache |
| 3D color | graphics／compute | final composition | color／compute | fragment／compute | slot |
| attr | graphics／compute | edge／fog | color／compute | fragment／compute | frame |
| depth | graphics／compute | edge／fog | depth／compute | fragment／compute | frame |
| final output | renderer | presenter | color／compute | fragment | slot |
| capture image | renderer | texture cache／CPU VRAM | color／compute | shader／transfer | capture generation |
| swapchain image | presenter | presentation engine | color attachment | present | swapchain |

## 25.2 CPU synchronization

- queue submit ownerを決める
- output slot mutex
- presenter refs
- renderer destroy condition variable
- GUI lifecycle atomic flags
- device context shared ownership
- callback中にdestroyしない
- completion callbackが必要なlockを保持したままwaitしない

## 25.3 GPU synchronization

- host write flush
- transfer write to shader read
- graphics color write to shader read
- compute write to indirect read
- compute write to compute read
- final write to presenter read
- presenter write to present
- queue family transfer
- timeline value monotonic

## 25.4 validationだけに依存しない

validationが報告しなくても、次をframe captureで確認する。

- stale texture
- previous frame
- partially cleared image
- scale change corruption
- first frame black
- menu transition flicker
- capture one-frame delay
- HUD overwrite
- swapchain image reuse

---

# 26. fallback規則

# 26.1 renderer fallback

```text
Vulkan Compute init failure
→ Vulkan Raster
→ Software
```

```text
Vulkan Raster init failure
→ Software
```

OpenGLへfallbackしない理由:

- Vulkan presenter用windowにはGL contextがない
- rendererだけOpenGLへ変えてもpresentation pathと一致しない
- window再構成が必要
- silent cross-backend switchは原因を隠す

明示的にwindowを再構成するfallback機構が完成した後だけ、OpenGL fallbackを検討する。

# 26.2 presenter fallback

```text
Vulkan presenter init failure
→ ScreenPanelNative + Software
```

または、rendererがVulkan outputしか持たない場合:

1. rendererを停止
2. Softwareへ切替
3. NativeQt panelへ切替

# 26.3 feature downgrade

Vulkan Compute要件不足時:

- Vulkan Rasterが利用可能ならradio buttonはRasterのみenable
- configからComputeが要求されたらRasterへnormalized
- tooltip／logへ理由
- config値は自動保存しない

# 26.4 fallbackログ

必須:

```text
requested
normalized
actual
failed function
VkResult
device
driver
fallback target
whether config was changed
```

---

# 27. テストマトリクス

## 27.1 renderer

```text
Software
OpenGL
OpenGL Compute Shader
Metal
Metal Compute Shader
Vulkan
Vulkan Compute Shader
```

## 27.2 scale

```text
1x
2x
3x
4x
6x
8x
12x
16x
```

## 27.3 setting

```text
Better Polygons OFF／ON
High Resolution Coordinates OFF／ON
Threaded OFF／ON
VSync OFF／ON
Frame Limit OFF／ON
filter nearest／linear
```

## 27.4 state

```text
no-ROM splash
ROM boot
firmware
title
menu
gameplay
pause
map
scan
zoom
Morph Ball
transition
dark screen
match end
reset
savestate
load state
ROM switch
renderer switch
scale change
fullscreen
minimize
restore
window move
DPI change
multi-window
```

## 27.5 GPU機能

```text
opaque
translucent
shadow
toon
highlight
fog
edge
AA
clear plane
clear bitmap
line
texture formats
repeat
mirror
palette
capture 128
capture 256
VRAM display
FIFO display
Master Brightness
ScreenSwap
RenderXPos
```

## 27.6 timing

```text
normal speed
Fast Forward Hold
Fast Forward Toggle
Slow Motion Hold
Slow Motion Toggle
audio sync
VSync
Frame Limit Toggle
```

## 27.7 window

```text
X11
Wayland
Win32
MoltenVK
HiDPI
1 monitor
multi monitor
different refresh rates
fullscreen
borderless
resize drag
minimized
occluded
```

---

# 28. ログ設計

## 28.1 startup

```text
Vulkan loader version
requested API version
created API version
instance extensions
validation enabled
physical devices
selected device
vendor ID
device ID
driver
queue families
surface support
swapchain format
present mode
timeline support
raster support
compute support
```

## 28.2 renderer

```text
requested renderer
normalized renderer
actual renderer
scale
Better Polygons
High Resolution Coordinates
texture cache generation
pipeline cache hit
shader manifest hash
output slot count
```

## 28.3 frame diagnostic

通常は状態変更時のみ。

```text
frame serial
output serial
slot
generation
producer timeline
presenter wait value
presented swapchain image
dropped frames
CPU upload bytes
readback bytes
```

## 28.4 error

```text
VkResult
function
resource
frame serial
queue
device lost
swapchain out of date
surface lost
fallback
```

## 28.5 spam防止

- 毎frameログ禁止
- 初回
- state change
- error
- developer mode
- 600frameごとのsummary
- rate limit

---

# 29. コードレビューチェック

## 29.1 correctness

```markdown
- [ ] OpenGL参照と描画順が一致
- [ ] Z／Wの両方
- [ ] translucent stencil
- [ ] shadow
- [ ] fog
- [ ] edge
- [ ] capture
- [ ] ScreenSwap
- [ ] Master Brightness
- [ ] frame serial一致
- [ ] stale generationなし
```

## 29.2 architecture

```markdown
- [ ] rendererとpresenterが分離
- [ ] same VkDevice
- [ ] output descriptor型安全
- [ ] lease
- [ ] device context owner明確
- [ ] GUI thread境界
- [ ] queue owner明確
- [ ] upstream変更最小
```

## 29.3 build gate

```markdown
- [ ] Vulkan OFF
- [ ] force disable
- [ ] source除外
- [ ] definition除外
- [ ] dependency除外
- [ ] UI除外
- [ ] translation除外
- [ ] binary string除外
```

## 29.4 performance

```markdown
- [ ] 毎frame allocationなし
- [ ] 毎frame pipeline作成なし
- [ ] 毎frame descriptor pool作成なし
- [ ] 毎frame config lookup増加なし
- [ ] steady-state readbackなし
- [ ] dirty textureのみupload
- [ ] HUD同一upload skip
- [ ] output slot stall計測
```

## 29.5 compatibility

```markdown
- [ ] Windows NVIDIA
- [ ] Windows AMD
- [ ] Windows Intel
- [ ] Linux X11
- [ ] Linux Wayland
- [ ] MoltenVK optional
- [ ] Software regressionなし
- [ ] OpenGL regressionなし
- [ ] Metal regressionなし
- [ ] multi-instance
- [ ] multi-window
```

---

# 30. 各Phase共通の実装ルール

各Phaseは、必ず次を含む。

```text
目的
変更ファイル
変更しないファイル
前提Phase
build gate
runtime path
fallback
ログ
検証
未検証
rollback
commit
CLAUDE plan status更新
```

## 30.1 1 Phase 1責務

次を同じcommitへ混ぜない。

- CMake基盤
- renderer algorithm
- UI
- localization
- performance optimization
- unrelated refactor
- upstream merge

## 30.2 marker

例:

```cpp
// MELONPRIME_VULKAN_OUTPUT_LEASE_V1
// MELONPRIME_VULKAN_FRAME_SNAPSHOT_V1
// MELONPRIME_VULKAN_COMPUTE_BINNING_V1
```

markerは適用済み判定、rollback、auditに使う。  
大量に散らさず、責務の入口へ置く。

## 30.3 失敗した実装

実機で効果がない、または症状差を説明できない実装は残さない。

1. 失敗Phaseを特定
2. rollback
3. obsolete marker削除
4. shared codeからfield／method削除
5. 新仮説で再実装
6. old symbolがないことをaudit

## 30.4 build成功の表現

区別する。

```text
source作成済み
syntax確認済み
configure済み
compile済み
link済み
起動済み
ROM runtime確認済み
pixel parity確認済み
hardware matrix確認済み
```

---

# 31. 将来のワンコマンド実装パッケージ規則

チャット経由でPhase patchを配布する場合:

```text
melonPrimeDS_Vulkan_PhaseXX_Name/
├── apply_phase_xx.py
├── apply_and_build_phase_xx.command
├── apply_and_build_phase_xx.ps1
├── check_phase_xx.command
├── check_phase_xx.ps1
├── run_phase_xx.command
├── run_phase_xx.ps1
├── README_実行手順.md
└── SHA256SUMS.txt
```

必須:

- preflight
- exact anchor
- backup
- diff
- marker
- idempotence
- old version upgrade
- exception rollback
- `git diff --check`
- build
- runtime command
- SHA-256

Vulkan SDKの手動installだけを前提にしない。  
vcpkg／system package／CIの依存解決を文書化する。

---

# 32. Definition of Done

## 32.1 Build

```markdown
- [ ] Vulkan完全kill switch
- [ ] force-disabled binaryにVulkan symbolなし
- [ ] Windows build
- [ ] Linux X11 build
- [ ] Linux Wayland build
- [ ] macOS Metal build regressionなし
- [ ] MoltenVK optional build
- [ ] generated SPIR-V一致
```

## 32.2 Renderer Raster

```markdown
- [ ] clear plane
- [ ] clear bitmap
- [ ] opaque
- [ ] translucent
- [ ] Z-buffer
- [ ] W-buffer
- [ ] shadow mask
- [ ] shadow
- [ ] texture
- [ ] toon
- [ ] highlight
- [ ] fog
- [ ] edge
- [ ] AA
- [ ] line
- [ ] Better Polygons
- [ ] RenderXPos
```

## 32.3 Renderer Compute

```markdown
- [ ] span setup
- [ ] binning
- [ ] work offsets
- [ ] sort
- [ ] indirect dispatch
- [ ] all raster variants
- [ ] tile color
- [ ] tile depth
- [ ] tile attr
- [ ] final pass 8 variants
- [ ] High Resolution Coordinates
- [ ] NVIDIA work count split
- [ ] visible compute output
```

## 32.4 2D／capture／final

```markdown
- [ ] 2D A
- [ ] 2D B
- [ ] 3D layer
- [ ] VRAM display
- [ ] FIFO display
- [ ] display capture 128
- [ ] display capture 256
- [ ] capture texture reuse
- [ ] ScreenSwap per line
- [ ] Master Brightness
- [ ] top／bottom output
```

## 32.5 Presenter

```markdown
- [ ] CPU BGRA
- [ ] Vulkan zero-copy
- [ ] OSD
- [ ] Custom HUD
- [ ] radar
- [ ] splash
- [ ] layout
- [ ] filter
- [ ] resize
- [ ] fullscreen
- [ ] minimize
- [ ] HiDPI
- [ ] multi-window
- [ ] output lease
```

## 32.6 Lifecycle

```markdown
- [ ] reset
- [ ] savestate
- [ ] load state
- [ ] ROM switch
- [ ] renderer switch
- [ ] scale change
- [ ] window close
- [ ] app exit
- [ ] device lost
- [ ] surface lost
- [ ] swapchain out of date
```

## 32.7 Timing

```markdown
- [ ] VSync ON
- [ ] VSync OFF
- [ ] Frame Limit
- [ ] Fast Forward Hold
- [ ] Fast Forward Toggle
- [ ] Slow Motion
- [ ] audio sync
- [ ] presenter独自60Hz制限なし
```

## 32.8 Quality

```markdown
- [ ] OpenGL Classic pixel parity
- [ ] OpenGL Compute pixel parity
- [ ] validation error 0
- [ ] synchronization validation error 0
- [ ] steady-state allocation 0
- [ ] steady-state readback 0
- [ ] no silent fallback
- [ ] requested／actual log
```

## 32.9 UI

```markdown
- [ ] feature probe
- [ ] unsupported理由
- [ ]全言語
- [ ] Cancel復元
- [ ] preset全値設定
- [ ] build gateで非表示
- [ ] macOS MoltenVK区別
```

## 32.10 Documentation

```markdown
- [ ] CLAUDE.md index
- [ ] Vulkan plan
- [ ] repo architecture
- [ ] upstream diff
- [ ] build docs
- [ ] CI docs
- [ ] release notes
- [ ] known limitations
```

---

# 33. 実行順序の要約

```text
Phase 0  正解baseline
Phase 1  build gateとshader toolchain
Phase 2  renderer IDとbackend policy
Phase 3  instance／surface／device probe
Phase 4  CPU BGRA Vulkan presenter
Phase 5  Vulkan output descriptorとlease
Phase 6  Software correctness renderer shell
Phase 7  Vulkan Classic 3D
Phase 8  texture cache／capture／savestate
Phase 9  Vulkan 2D／final composition
Phase 10 zero-copy presenter／multi-window
Phase 11 Vulkan Compute Shader
Phase 12 UI／preset／localization
Phase 13 VSync／frame pacing／device loss／cache
Phase 14 performance
Phase 15 CI／package／MoltenVK
Phase 16 docs／cleanup／formal completion
```

依存関係:

```text
0
└── 1
    └── 2
        └── 3
            ├── 4
            │   └── 5
            │       └── 6
            │           └── 7
            │               └── 8
            │                   └── 9
            │                       └── 10
            │                           └── 11
            └───────────────────────────────┘
                                            └── 12
                                                └── 13
                                                    └── 14
                                                        └── 15
                                                            └── 16
```

UI公開はPhase 12まで行わない。  
Vulkan Compute Shaderを「完成」とするのはPhase 11のvisible output、pixel parity、vendor matrix完了後である。

---

# 34. 最重要原則

```text
Vulkanを追加するだけではなく、Vulkan RasterとVulkan Compute Shaderを完成させる。

描画の正解はOpenGL ClassicとOpenGL Compute Shaderから取る。

統合の安全性はMetal計画のbuild gate、feature probe、output lease、
frame ownership、requested／actual backend、UI公開条件から取る。

rendererとpresenterを分離する。

GPU imageの所有権とフレーム世代を曖昧にしない。

設定値が変わったことではなく、最終画面へ正しいframeが届いたことを確認する。

Vulkan非対応環境では無言で壊さず、具体的な理由を表示する。

Vulkan無効buildにはVulkanのsource、UI、dependency、文字列を残さない。

正しさを証明する前に同期や描画順を最適化しない。

効果がなかった実装は残さない。

全Phaseを完了し、OpenGL／Metal／Softwareへの回帰がない状態を
Vulkan supportのDefinition of Doneとする。
```
