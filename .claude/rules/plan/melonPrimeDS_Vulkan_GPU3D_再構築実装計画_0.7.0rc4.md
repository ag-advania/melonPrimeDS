# melonPrimeDS Vulkan GPU 3D再構築実装計画
## SapphireRhodonite/melonDS-android 0.7.0.rc4準拠

**作成日:** 2026-07-14  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**参照フロントエンド:** `SapphireRhodonite/melonDS-android` タグ `0.7.0.rc4`  https://github.com/SapphireRhodonite/melonDS-android/releases/tag/0.7.0.rc4 
**参照コア:** `SapphireRhodonite/melonDS-android-lib` コミット `d77944275fa61f9b79cfcead2c3e98993429a023`  
**対象状態:** 現在のVulkanレンダリング経路は破損しているものとして扱う  
**既存計画:** 既存計画書の進行度、完了マーク、phase番号、contract versionを実装済み判定に使わない  

---

# 1. 目的

melonPrimeDSでVulkanを選択した場合に、Nintendo DSの3DポリゴンをVulkan GPUで直接ラスタライズし、そのGPU上の3D画像を2D合成、画面配置、最終表示までGPU常駐で処理する。

最終的な通常フレーム経路は次とする。

```text
GPU3D polygon state
    ↓
GPU3D_AcceleratedFrontend
    ↓
VulkanRenderer3D
    ├─ clear plane
    ├─ clear bitmap
    ├─ texture cache
    ├─ opaque polygons
    ├─ translucent polygons
    ├─ shadow mask / reject / blend
    ├─ toon / highlight
    ├─ depth / stencil
    ├─ edge marking
    ├─ fog
    └─ antialiasing
    ↓
GPU-resident high-resolution 3D image
    ↓
VulkanOutput
    ├─ structured 2D metadata
    ├─ VRAM display
    ├─ FIFO display
    ├─ display capture history
    ├─ screen swap
    └─ master brightness
    ↓
GPU-resident two-screen output
    ↓
FrameQueue
    ↓
VulkanSurfacePresenter
    ↓
Qt native window swapchain
```

Vulkan選択時の3D正解源をSoftware Rendererにしない。

通常表示フレームで次を行わない。

```text
Software 3D framebuffer生成
Software 3D結果のCPUコピー
Software 3D ownership mask生成
CPUによる高解像度再構築
QImageへの3D画像変換
CPU BGRA画面をVulkanへ再アップロード
Vulkan imageの通常フレームreadback
Native Qt painterによる最終表示
```

---

# 2. 現行実装を前提にした再構築方針

現在の`highres_fonts_v3`には、Sapphire由来の`VulkanRenderer3D`本体、structured 2D metadata、`VulkanOutput`、`VulkanSurfacePresenter`、`FrameQueue`が部分的に存在する。

ただし、実際の実行経路は一体化されていない。

現在の構造は概念的に次の状態である。

```text
CreateRendererForSelection(Vulkan)
    ↓
SoftRendererを生成
    ↓
GPU3D::CurrentRendererへVulkanRenderer3Dを別所有
    ↓
Renderer::GetRenderer3D()だけVulkanへ差し替え
    ↓
SoftRendererがVulkanRenderer3D::GetLine()を読む
    ↓
SoftRendererがCPU framebufferを完成
    ↓
ScreenPanelVulkanはNative CPU presenterへ委譲
```

同時に、次の未接続要素が存在する。

```text
VulkanRenderer3D内のSapphireCompositionImage
VulkanRenderer3D内のstructured 2D GPU buffer
Vulkan compositor shader module
VulkanReference/VulkanOutput
VulkanReference/VulkanSurfacePresenter
VulkanReference/FrameQueue
```

この構造を延命しない。

次の原則で整理する。

1. 外側の`VulkanRenderer`を正式な`Renderer`として復活させる。
2. `VulkanRenderer`が`VulkanRenderer3D`、2D metadata producer、`VulkanOutput`を所有する。
3. `GPU3D::CurrentRenderer`によるrenderer横取りを廃止する。
4. `Renderer::GetRenderer3D()`を通常の`Rend3D`所有へ戻す。
5. final compositorを`VulkanRenderer3D`内部に置かない。
6. final compositorは参照実装と同じく`VulkanOutput`へ集約する。
7. `ScreenPanelVulkan`をCPU presenterではなく`VulkanSurfacePresenter`へ接続する。
8. Vulkan Computeという別名で未実装backendを表示しない。
9. phase contractやboolは実装の代用にしない。
10. 参照実装のコードを先に固定し、desktop差分だけをadapterとして追加する。

---

# 3. 最終クラス所有関係

最終所有関係を次へ統一する。

```text
GPU
└─ std::unique_ptr<Renderer> Rend
   └─ VulkanRenderer
      ├─ std::unique_ptr<Renderer2D> Rend2D_A
      ├─ std::unique_ptr<Renderer2D> Rend2D_B
      ├─ std::unique_ptr<Renderer3D> Rend3D
      │  └─ VulkanRenderer3D
      ├─ std::unique_ptr<VulkanOutput> Output
      ├─ FrameQueue
      └─ immutable frame state
```

GUI側は次を所有する。

```text
ScreenPanelVulkan
├─ native surface host
├─ VulkanSurfacePresenter
├─ surface ID
├─ current layout state
├─ current background state
└─ current HUD/OSD state
```

共有GPU objectは次へ集約する。

```text
VulkanContext
├─ VkInstance
├─ VkPhysicalDevice
├─ VkDevice
├─ VkQueue
├─ queue family index
├─ queue mutex
├─ enabled extensions
├─ enabled features
├─ device profile
├─ timeline semaphore functions
├─ descriptor indexing functions
└─ timestamp functions
```

---

# 4. 実装境界

## 4.1 melonDS共通層

次の変更はbackend非依存の最小限に限定する。

```text
src/GPU.h
src/GPU.cpp
src/GPU3D.h
src/GPU3D.cpp
src/GPU_Soft.h
src/GPU_Soft.cpp
src/GPU2D_Soft.h
src/GPU2D_Soft.cpp
src/frontend/qt_sdl/EmuThread.cpp
```

共通層へ追加するのは次だけとする。

- renderer output種別
- renderer output lease
- structured 2D metadataの抽象interface
- Vulkan renderer生成に必要なbackend-neutral lifecycle hook
- display captureとaccelerated rendererの共通hook
- renderer切替時の安全な所有権移動

Vulkan API型を共通層へ露出する場合は、既存の`MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN`ガード内へ限定する。

## 4.2 MelonPrime Vulkan専用層

次へVulkan固有実装を置く。

```text
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
src/GPU2D_Vulkan.h
src/GPU2D_Vulkan.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU3D_TexcacheVulkan.h
src/GPU3D_TexcacheVulkan.cpp
src/VulkanContext.h
src/VulkanContext.cpp
src/VulkanDesktopSurface.h
src/VulkanDesktopSurface.cpp
src/VulkanPerfStats.h
src/frontend/qt_sdl/MelonPrimeScreenVulkan.h
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHost.h
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHost.cpp
src/frontend/qt_sdl/VulkanReference/*
```

## 4.3 参照実装コードとdesktop adapterの分離

参照実装由来コードへQt固有処理を直接混ぜない。

```text
VulkanReference/
    参照実装に近いrenderer output、frame queue、presenter logic

MelonPrimeVulkanSurfaceHost.*
    HWND、XCB/Xlib、Wayland、MoltenVK surfaceの取得

MelonPrimeScreenVulkan.*
    Qt event、window lifecycle、layout、HUD、OSD
```

---

# 5. フェーズR0: 現在の偽装された完成状態を解除する

## 5.1 shell contractを能力判定に使わない

次を実装判定から外す。

```text
VulkanRendererShellContract
NativeVulkan3DImplemented
SapphireRenderer3DOwnership
SapphireFrameLifecycle
SapphireStructured2DMetadata
SapphirePacked2DGpuUpload
SapphireGpuCompositionResources
SapphireGpuCompositionCommandContext
ContractVersion
```

これらのboolを参照するfactory分岐を削除する。

実際の能力は、初期化済みobjectから得る。

```cpp
struct VulkanRuntimeCapabilities
{
    bool Renderer3DReady = false;
    bool Structured2DReady = false;
    bool FinalCompositorReady = false;
    bool PresenterReady = false;
    bool TimelineSemaphoreReady = false;
    bool DescriptorIndexingReady = false;
    bool ComputeRasterReady = false;
};
```

## 5.2 A1～A7コメントを進行管理に使わない

次の形式のコメントを実行条件にしない。

```text
MELONPRIME_SAPPHIRE_VULKAN_*_A1
MELONPRIME_SAPPHIRE_VULKAN_*_A2
MELONPRIME_SAPPHIRE_VULKAN_*_A3
MELONPRIME_SAPPHIRE_VULKAN_*_A4
MELONPRIME_SAPPHIRE_VULKAN_*_A5
MELONPRIME_SAPPHIRE_VULKAN_*_A6
MELONPRIME_SAPPHIRE_VULKAN_*_A7
```

参照実装へ一致する実体コードへ置き換えた後に削除する。

## 5.3 Vulkan選択中のactual renderer報告を正す

現在のfactoryはVulkan選択時にも`SoftRenderer`を返しているため、`report.actual`をVulkanのままにしない。

再構築中は次のどちらかに限定する。

```text
完全なVulkanRenderer生成成功
    actual = Vulkan

生成失敗
    actual = Software
```

`SoftRenderer + VulkanRenderer3D override`をVulkanとして報告する状態を廃止する。

---

# 6. フェーズR1: 0.7.0.rc4基準ソースを固定する

## 6.1 固定する参照点

コアは必ず次のcommitを使う。

```text
d77944275fa61f9b79cfcead2c3e98993429a023
```

frontendは必ず次のtagを使う。

```text
0.7.0.rc4
```

移植元をmaster、nightly、後続releaseへ自動追従させない。

## 6.2 コア側の再取り込み対象

次を参照commitとファイル単位で再照合し、MelonPrime固有追加を分離する。

```text
GPU3D_AcceleratedFrontend.h
GPU3D_AcceleratedFrontend.cpp
GPU3D_Vulkan.h
GPU3D_Vulkan.cpp
GPU3D_TexcacheVulkan.h
GPU3D_TexcacheVulkan.cpp
VulkanContext.h
VulkanContext.cpp
VulkanPerfStats.h
```

shader sourceとgenerated headerも同じcommitへ揃える。

```text
GPU3D_Vulkan_InterpSpansShader.comp
GPU3D_Vulkan_BinCombinedShader.comp
GPU3D_Vulkan_CalculateWorkOffsetsShader.comp
GPU3D_Vulkan_SortWorkShader.comp
GPU3D_Vulkan_TriRasterShader.comp
GPU3D_Vulkan_TriRasterBaseShader.comp
GPU3D_Vulkan_TriRasterCompatShader.comp
GPU3D_Vulkan_DepthBlendShader.comp
GPU3D_Vulkan_FinalPassShader.comp
GPU3D_Vulkan_CaptureLineExportShader.comp
GPU3D_Vulkan_GraphicsRasterShader.vert
GPU3D_Vulkan_GraphicsRasterShader.frag
GPU3D_Vulkan_GraphicsNoColorShader.frag
GPU3D_Vulkan_GraphicsClearShader.frag
GPU3D_Vulkan_GraphicsFinalShader.vert
GPU3D_Vulkan_GraphicsEdgeShader.frag
GPU3D_Vulkan_GraphicsEdgeFogShader.frag
GPU3D_Vulkan_GraphicsFogShader.frag
```

## 6.3 frontend側の再取り込み対象

```text
VulkanOutput.h
VulkanOutput.cpp
VulkanCompositorShader.comp
VulkanAccumulate3dShader.comp
FrameQueue.h
FrameQueue.cpp
VulkanSurfacePresenter.h
VulkanSurfacePresenter.cpp
VulkanSurfacePresenter.vert
VulkanSurfacePresenter.frag
VulkanFilterMode.h
```

Android固有部分は削除するのではなく、platform adapterへ置換する。

## 6.4 参照コードへのMelonPrime固有差分の置き方

差分は次の3種類に分ける。

```text
Core compatibility adaptation
Desktop platform adaptation
MelonPrime UI/layout adaptation
```

各差分を別関数、別型、別ファイルへ置き、参照関数の内部へ広範囲に混在させない。

---

# 7. フェーズR2: renderer所有権を正常化する

## 7.1 `GPU3D::CurrentRenderer`を削除する

次を削除する。

```cpp
std::unique_ptr<Renderer3D> GPU3D::CurrentRenderer;
GPU3D::SetCurrentRenderer();
GPU3D::GetCurrentRendererOverride();
```

`GPU3D`はrendererを所有しない。

## 7.2 `Renderer::GetRenderer3D()`を通常所有へ戻す

最終形を次へ戻す。

```cpp
Renderer3D& GetRenderer3D() noexcept
{
    return *Rend3D;
}

const Renderer3D& GetRenderer3D() const noexcept
{
    return *Rend3D;
}
```

Vulkanだけ別経路でrendererを横取りしない。

## 7.3 `GPU::SetRenderer()`の順序を整理する

最終順序を次へ統一する。

```text
旧rendererのcapture同期
旧rendererのStop
旧renderer破棄
新rendererをRendへmove
新renderer Init
新renderer Reset
VRAM cache state reset
palette/OAM dirty state reset
```

`GPU3D::SetCurrentRenderer(nullptr)`を削除する。

## 7.4 renderer切替objectを一度だけ生成する

次の二段生成を廃止する。

```text
CreateRendererForSelection()
CreateRenderer3DOverrideForSelection()
```

factoryは`Renderer`を一つだけ返す。

---

# 8. フェーズR3: 正式な`VulkanRenderer`を実装する

## 8.1 class定義

`GPU_Vulkan.h`へ次を実装する。

```cpp
class VulkanRenderer final : public Renderer
{
public:
    explicit VulkanRenderer(
        NDS& nds,
        VulkanRendererMode mode) noexcept;
    ~VulkanRenderer() override;

    bool Init() override;
    void Reset() override;
    void Stop() override;

    void PreSavestate() override;
    void PostSavestate() override;

    void SetRenderSettings(RendererSettings& settings) override;

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;

    void VBlank() override;
    void VBlankEnd() override;

    void AllocCapture(u32 bank, u32 start, u32 len) override;
    void SyncVRAMCapture(
        u32 bank,
        u32 start,
        u32 len,
        bool complete) override;

    bool GetFramebuffers(void** top, void** bottom) override;
    RendererOutput GetOutput() override;
    RendererOutputLease AcquireOutputLease() override;

private:
    bool InitializeRenderer3D();
    bool Initialize2DProducer();
    bool InitializeOutput();
    void BeginFrame();
    void EndFrame();
    void InvalidateOutputGeneration();
};
```

## 8.2 constructorで所有するobject

```cpp
Rend3D = VulkanRenderer3D::New();
Rend2D_A = std::make_unique<StructuredSoftRenderer2D>(...);
Rend2D_B = std::make_unique<StructuredSoftRenderer2D>(...);
Output = std::make_unique<MelonDSAndroid::VulkanOutput>();
FrameQueue = std::make_unique<MelonDSAndroid::FrameQueue>();
```

`SoftRenderer`を継承しない。

## 8.3 `GetFramebuffers()`

Vulkan通常経路ではRAM framebufferを公開しない。

```cpp
bool VulkanRenderer::GetFramebuffers(void** top, void** bottom)
{
    *top = nullptr;
    *bottom = nullptr;
    return false;
}
```

## 8.4 `GetOutput()`

最終GPU image descriptorを返す。

```cpp
RendererOutput VulkanRenderer::GetOutput()
{
    return RendererOutput::VulkanImage(&PublishedOutput);
}
```

## 8.5 mode

```cpp
enum class VulkanRendererMode
{
    Graphics
};
```

0.7.0.rc4へ合わせる最初の正式backendはGraphicsのみとする。

既存の`renderer3D_VulkanCompute`は独立backendとして扱わない。

次のいずれかへ変更する。

```text
UIから一時的に非表示
Vulkan Graphicsへ正規化
設定migrationでVulkanへ置換
```

未実装Compute選択をGraphicsへ見せかけたまま残さない。

---

# 9. フェーズR4: `VulkanContext`をdesktop用に完成させる

## 9.1 instance extension

platformごとに必要なinstance extensionを構築する。

### Windows

```text
VK_KHR_surface
VK_KHR_win32_surface
```

### Linux X11

```text
VK_KHR_surface
VK_KHR_xlib_surface
```

または

```text
VK_KHR_surface
VK_KHR_xcb_surface
```

### Linux Wayland

```text
VK_KHR_surface
VK_KHR_wayland_surface
```

### macOS MoltenVK

```text
VK_KHR_surface
VK_EXT_metal_surface
VK_KHR_portability_enumeration
```

instance create flagsへ次を設定する。

```text
VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
```

## 9.2 device extension

必須:

```text
VK_KHR_swapchain
```

利用可能時:

```text
VK_KHR_timeline_semaphore
VK_EXT_descriptor_indexing
VK_EXT_host_query_reset
VK_KHR_portability_subset
```

## 9.3 physical device選択

単純なdiscrete優先だけにしない。

候補ごとに次を確認する。

```text
graphics queue
surface present対応
swapchain extension
必要format
必要depth/stencil format
storage image対応
sampled integer image対応
max image dimension
max descriptor count
timeline semaphore
descriptor indexing
```

surface作成前のrenderer初期化では、graphics capabilityを選び、presenter attach時にpresent capabilityを確認する。

graphics queueとpresent queueが異なる場合を扱う。

```cpp
u32 GraphicsQueueFamilyIndex;
u32 PresentQueueFamilyIndex;
VkQueue GraphicsQueue;
VkQueue PresentQueue;
```

同一familyならsingle queue fast pathを使う。

## 9.4 feature chain

feature queryとdevice create chainを明確に分離する。

```cpp
VkPhysicalDeviceFeatures2 queriedFeatures;
VkPhysicalDeviceTimelineSemaphoreFeatures queriedTimeline;
VkPhysicalDeviceDescriptorIndexingFeatures queriedIndexing;
```

有効化するfeatureだけを別のenabled構造へコピーする。

self-assignment形式を廃止する。

## 9.5 context lifetime

`Acquire()`と`Release()`は次を保証する。

```text
最初のAcquireだけinstance/device生成
失敗時にReferenceCountを0へ戻す
複数renderer/presenterが共有
最後のReleaseだけdevice破棄
presenter leaseが残る間は破棄しない
```

## 9.6 queue access

すべてのsubmitとpresentを共通queue lockで保護する。

```cpp
std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
```

長時間のCPU処理中にqueue lockを保持しない。

---

# 10. フェーズR5: shader生成経路を一つにする

## 10.1 sourceを正とする

`.comp`、`.vert`、`.frag`を正とし、generated headerを自動生成する。

手編集したSPIR-V配列を正にしない。

## 10.2 generator

```text
tools/vulkan/generate_sapphire_spirv.py
```

へ次を実装する。

```text
glslangValidatorの解決
source一覧読込
define variant生成
SPIR-V生成
C++ header生成
source hash記録
ABI manifest生成
```

## 10.3 core shader一覧

参照commitのshaderをそのまま生成対象にする。

graphics shaderとcompute shaderを同じmanifestへ置く。

## 10.4 compositor shader

現在のinline shader dataとA6/A7専用moduleを廃止し、参照frontendと同じgenerated headerへ統一する。

```text
VulkanCompositorShaderData.h
VulkanAccumulate3dShaderData.h
VulkanSurfacePresenterVertexShaderData.h
VulkanSurfacePresenterFragmentShaderData.h
```

## 10.5 ABI定義

descriptor bindingとpush constantをshader側、core側、frontend側で重複定義しない。

共通headerへ集約する。

```cpp
struct VulkanCompositorAbi
{
    static constexpr u32 OutputImage = 0;
    static constexpr u32 Current3DImage = 1;
    static constexpr u32 TopPackedBuffer = 2;
    static constexpr u32 BottomPackedBuffer = 3;
    static constexpr u32 PreviousTop3DImage = 4;
    static constexpr u32 Capture3DBuffer = 5;
    static constexpr u32 PreviousBottom3DImage = 6;
};
```

---

# 11. フェーズR6: `VulkanRenderer3D`を参照実装へ戻す

## 11.1 constructor API

現在のMelonPrime版で追加された`GPU3D&` constructor依存を整理する。

参照実装のAPIへ近づける。

```cpp
static std::unique_ptr<VulkanRenderer3D> New() noexcept;
VulkanRenderer3D() noexcept;
```

melonPrimeDS側の`Renderer3D`基底が`GPU3D&`を要求する場合は、factory adapterで渡す。

参照本体の内部へMelonPrime固有所有権処理を入れない。

## 11.2 lifecycle

次を参照順序のまま接続する。

```text
Reset
VCount144
RenderFrame
RestartFrame
SetupAccelFrame
PrepareCaptureFrame
BeginCaptureFrame
SetCaptureScreenSwapHint
Blit
Stop
```

現在のinline adapterで`RenderFrame()`冒頭に`BeginCaptureFrame()`を強制する構造を見直す。

capture lifecycleはGPUのscanline/frame lifecycle側から参照実装と同じ順序で呼ぶ。

## 11.3 backend mode

0.7.0.rc4準拠では次を正式backendにする。

```cpp
BackendMode::GraphicsHardware
```

Compute pipelineはgraphics backend内部の補助実装として扱い、UI上の別renderer identityにしない。

## 11.4 final composition責務を削除する

`VulkanRenderer3D`から次を削除する。

```text
SapphireVulkanCompositionInput
SapphireVulkanCompositionResources
SapphireVulkanCompositionCommandContext
SapphireVulkanCompositionShaderModule
SapphireCompositionImage
SapphireCompositionDescriptorSetLayout
SapphireCompositionDescriptorPool
SapphireCompositionDescriptorSet
SapphireCompositionPipelineLayout
SapphireCompositionCommandPool
SapphireCompositionCommandBuffer
SapphireCompositionFence
ensureSapphireComposition*
destroySapphireComposition*
```

3D rendererが所有するのは3D targetとcapture exportまでとする。

## 11.5 structured 2D責務を移動する

`VulkanRenderer3D`から次を`VulkanRenderer`または`VulkanOutput`へ移す。

```text
Structured2DPlane0
Structured2DPlane1
Structured2DControl
Structured2DNativeFinal
Structured2DLineMeta
Structured2DLineState
Structured2DPacked
Structured2DGpuBuffer
Structured2DGpuMemory
BeginStructured2DFrame
SubmitStructured2DLine
EndStructured2DFrame
```

`VulkanRenderer3D`はstructured 2Dを所有しない。

---

# 12. フェーズR7: 3D render targetを完成させる

## 12.1 target size

```text
Width  = 256 × ScaleFactor
Height = 192 × ScaleFactor
```

scale変更時は3D targetを再生成する。

presenter側だけで拡大しない。

## 12.2 image構成

```text
ColorImage
AttrImage
DepthImage
DepthStencilImage
CaptureReadbackImage
```

用途を次へ揃える。

### ColorImage

```text
COLOR_ATTACHMENT
STORAGE
SAMPLED
TRANSFER_SRC
TRANSFER_DST
```

### AttrImage

```text
COLOR_ATTACHMENT
STORAGE
SAMPLED
TRANSFER_SRC
```

### DepthImage

```text
STORAGE
SAMPLED
TRANSFER_SRC
```

### DepthStencilImage

```text
DEPTH_STENCIL_ATTACHMENT
TRANSFER_SRC
```

## 12.3 format

colorとattributeは参照実装のformatを維持する。

depth/stencilはdevice対応から選択する。

```text
D32_SFLOAT_S8_UINT
D24_UNORM_S8_UINT
D16_UNORM_S8_UINT
```

## 12.4 image ownership

3D targetをpresenterへ直接貸さない。

`VulkanOutput`が3D targetを入力として参照し、別のframe output imageへ合成する。

---

# 13. フェーズR8: render context ringを完成させる

## 13.1 context数

参照実装と同じ6 contextを基本とする。

```cpp
static constexpr size_t AsyncRenderContextCount = 6;
```

## 13.2 contextごとのresource

```text
command pool
command buffer
frame fence
descriptor set
graphics descriptor set
single texture descriptor sets
triangle buffer
graphics vertex buffer
bin mask buffer
group list buffer
span setup buffer
work offset buffer
toon buffer
clear buffer
capture line buffer
timestamp query pool
descriptor cache
```

## 13.3 再利用

次のcontextだけ待つ。

```text
次に再利用するcontext
capture readback source context
texture cache mutationと競合するcontext
```

通常フレームで`vkDeviceWaitIdle()`を呼ばない。

## 13.4 buffer growth

必要capacityを超えた場合だけ再確保する。

```cpp
newCapacity = max(requiredCapacity, oldCapacity * 2);
```

persistent mapped memoryを利用する。

---

# 14. フェーズR9: accelerated sceneを正式入力にする

## 14.1 共通scene builder

`GPU3D_AcceleratedFrontend`をgraphics pathの唯一のgeometry sourceにする。

別のMelonPrime packed vertex builderを併走させない。

## 14.2 scene内容

```text
Vertices
Indices
EdgeIndices
Triangles
Draws
FirstTranslucentDraw
```

各drawへ次を持たせる。

```text
source polygon
render key
flags
poly attr
poly ID
alpha
primitive type
coverage state
first vertex
vertex count
first index
index count
first edge index
edge index count
first triangle
triangle count
```

## 14.3 source order

`RenderPolygonRAM`順を保持する。

同一pipeline stateでも非隣接polygonをまとめない。

```text
A B A
```

は3 draw rangeのままとする。

## 14.4 line polygon

参照実装のline解決とquad化を使用する。

MelonPrime独自の別line expansionを重ねない。

## 14.5 high-resolution coordinates

```text
ScaleFactor
BetterPolygons
HiresCoordinates
coverage fix
```

をscene build configへ一括で渡す。

`HiresCoordinates`を捨てない。

現在の`SetRenderSettings()`内の`(void)hiresCoordinates`を廃止する。

---

# 15. フェーズR10: Vulkan texture cacheを参照実装へ揃える

## 15.1 loader

```cpp
class TexcacheVulkanLoader
{
public:
    TextureHandle GenerateTexture(
        u32 width,
        u32 height,
        u32 layers);

    void UploadTexture(
        TextureHandle handle,
        u32 width,
        u32 height,
        u32 layer,
        void* data);

    void DeleteTexture(TextureHandle handle);

    bool GetTextureDescriptor(
        TextureHandle handle,
        VkDescriptorImageInfo* out) const;

    bool IsTextureLayerOpaque(
        TextureHandle handle,
        u32 layer) const;
};
```

## 15.2 cache更新

```text
VRAMDirty_Texture
VRAMDirty_TexPal
VRAMMap_Texture
VRAMMap_TexPal
```

を使い、変更textureだけを更新する。

## 15.3 descriptor path

device能力に応じて次を選ぶ。

```text
NonUniform
CompatDynamicUniform
BaseSingleDescriptor
```

いずれもGPU rasterを使う。

descriptor indexing非対応をSoftware fallback理由にしない。

## 15.4 sampler

S/T軸のclamp、repeat、mirror組み合わせを共有sampler tableへする。

drawごとにsamplerを生成しない。

## 15.5 fallback texture

白1×1 textureをrenderer初期化時に一度作る。

texture decode失敗polygonを削除せず、untexturedまたはfallback textureとして描画する。

---

# 16. フェーズR11: clear planeとclear bitmapを実装する

## 16.1 clear plane

`RenderClearAttr1`と`RenderClearAttr2`から次を生成する。

```text
RGB
alpha
depth
opaque polygon ID
fog flag
stencil
```

render passのclear valueへ反映する。

## 16.2 clear bitmap

`RenderDispCnt`のclear bitmap bitに従う。

```text
VRAM texture region
    ↓
color bitmap
depth/fog bitmap
    ↓
scaled clear pass
```

offsetは`RenderXPos`とclear attributeから参照実装どおりに反映する。

## 16.3 CPU snapshotを作らない

次を行わない。

```text
毎フレーム256×256をstd::vectorへdecode
presenter用snapshotへclear bitmapを複製
QImageへ変換
```

texture cacheまたはdedicated staging resourceからrendererのGPU imageへ直接送る。

---

# 17. フェーズR12: opaque polygon pathを完成させる

## 17.1 pipeline variant

最低限次をvariant keyへ含める。

```text
Z-buffer / W-buffer
depth less / depth equal
texture有無
modulate / decal / toon / highlight
fog write
line / triangle
fragment depth path
descriptor indexing path
```

## 17.2 vertex input

参照実装の`GraphicsVertexGpu` ABIを使う。

```text
position
depth
reciprocal W
texture coordinate
vertex color
polygon flags
texture descriptor index
texture size
texture param
polygon attr
```

## 17.3 opaque fragment処理

```text
vertex color補間
texture sampling
texture combiner
alpha test
depth test
depth write
color write
attribute write
polygon ID write
fog flag write
stencil write
```

## 17.4 NeedOpaque

半透明polygon内の完全不透明fragmentを先行opaque passへ送る。

texture arrayのopaque情報を利用し、不要なpassを省く。

---

# 18. フェーズR13: translucent polygon pathを完成させる

## 18.1 blend

```text
src = SRC_ALPHA
dst = ONE_MINUS_SRC_ALPHA
color op = ADD
alpha op = MAX
```

## 18.2 depth

polygon attributeに従い次を分ける。

```text
depth less
depth equal
depth write enabled
depth write disabled
Z-buffer
W-buffer
```

## 18.3 translucent polygon ID

同じtranslucent polygon IDによる重複blendをstencilで防ぐ。

異なるIDのpolygonは重ね合わせを許可する。

## 18.4 background alpha zero

背景alpha zero時の専用pipelineを参照実装どおりに分離する。

一般translucent pipelineへ無理に統合しない。

---

# 19. フェーズR14: shadow pathを完成させる

shadowを次の段階へ分ける。

```text
Shadow Mask
Shadow Self Reject
Shadow Blend
```

## 19.1 Shadow Mask

depth fail時にstencil bit 7を設定する。

lower polygon ID bitsを保持する。

## 19.2 Shadow Self Reject

shadow polygon自身のpoly IDと一致するpixelのshadow bitを消す。

## 19.3 Shadow Blend

shadow bitが残るpixelだけblendする。

blend後にlower stencil bitsを参照実装どおり更新する。

## 19.4 stencil clear

必要なpass境界でshadow bitだけをclearするpipelineを使う。

depth/stencil image全体を不要にclearしない。

---

# 20. フェーズR15: toon、highlight、edge、fog、AAを完成させる

## 20.1 toon table

32 entryをGPU bufferへuploadする。

opaque、translucent、textured、untexturedで共通利用する。

## 20.2 highlight

highlight modeを専用shader variantへする。

toonと同じbranchへ無理に統合しない。

## 20.3 edge marking

attribute imageのpolygon IDと隣接pixelを比較する。

edge tableを参照してfinal colorへ適用する。

## 20.4 fog

```text
RenderFogColor
RenderFogOffset
RenderFogShift
RenderFogDensityTable
```

をfinal passへ渡す。

fog write flagがあるpixelだけ処理する。

## 20.5 antialiasing

coverage情報をfinal passへ渡す。

edge、fogとの組み合わせをpipeline variantまたはspecializationへする。

---

# 21. フェーズR16: display captureを接続する

## 21.1 capture source

3D capture sourceはVulkan 3D targetと同じframe identityを使う。

## 21.2 capture line export

参照shaderを使用する。

```text
scaled 3D target
    ↓
native 256 pixel line
    ↓
DS color format
    ↓
capture line buffer
```

## 21.3 double buffer

capture line bufferは2 slot以上を持つ。

```text
active
pending
ready
```

を分離する。

## 21.4 `GetLine()`

通常2D合成のために毎line readbackしない。

`GetLine()`はcaptureまたはCPU同期が必要な場面だけexact line cacheを返す。

## 21.5 capture VRAM同期

GPU capture結果をVRAMへ反映する必要がある場合だけ、必要範囲をreadbackする。

normal presentationをcapture readbackへ依存させない。

---

# 22. フェーズR17: structured 2D producerを独立させる

## 22.1 `SoftRenderer`から分離する

現在の`SoftRenderer`内に追加された次を専用producerへ移す。

```text
StructuredPlane0
StructuredPlane1
StructuredControl
StructuredNativeFinal
StructuredFrameSerial
```

## 22.2 class

```cpp
class VulkanStructured2DProducer
{
public:
    void BeginFrame(
        u64 frameSerial,
        bool screenSwap);

    void SubmitEngineLine(
        u32 engine,
        u32 line,
        const u32* plane0,
        const u32* plane1,
        const u32* control,
        const u32* nativeFinal,
        u32 dispCnt,
        u16 masterBrightness,
        bool enabled,
        bool forcedBlank,
        bool screensEnabled);

    void EndFrame();
    const VulkanStructured2DFrame& PublishedFrame() const;
};
```

## 22.3 2D renderer連携

`SoftRenderer2D`のlayer evaluationは再利用してよい。

ただし外側`SoftRenderer`のCPU framebuffer完成処理を必要条件にしない。

Engine A、Engine Bごとに次を直接producerへ渡す。

```text
plane 0
plane 1
control
line metadata
display mode
master brightness
```

## 22.4 native final

VRAM display、FIFO display、forced blankなどstructured layer pairで表現できないmodeだけnative finalを使用する。

regular displayの通常pixelをnative finalへ丸ごと依存させない。

## 22.5 frame identity

structured 2D frameへ次を持たせる。

```text
FrameSerial
FrontBuffer
ScreenSwap
Generation
Complete
```

---

# 23. フェーズR18: `VulkanOutput`へfinal compositionを集約する

## 23.1 `VulkanRenderer3D`内の重複compositorを削除する

A3～A7で追加されたcomposition resourceを`VulkanOutput`へ統合する。

compositor shader module、descriptor、pipeline、command bufferを一か所だけ所有する。

## 23.2 input

```cpp
struct VulkanCompositionInputs
{
    VkImage sourceImage;
    VkImageView sourceImageView;

    VkImage previousTopSourceImage;
    VkImageView previousTopSourceImageView;

    VkImage previousBottomSourceImage;
    VkImageView previousBottomSourceImageView;

    VkBuffer topPackedBuffer;
    VkBuffer bottomPackedBuffer;
    VkBuffer capture3dBuffer;

    u32 scale;
    u32 rendererWidth;
    u32 rendererHeight;
    u32 packedStride;
    u32 screenSwap;

    bool previousTopSourceValid;
    bool previousBottomSourceValid;
    bool capture3dSourceValid;
    bool liveSourceScreenSwap;
};
```

参照実装の入力fieldを維持する。

## 23.3 output image

frame slotごとに2-screen output imageを持つ。

```text
width = 256 × scale
height = 192 × scale × 2
```

またはarray layer 2とする。

参照shaderとpresenterの期待する形式へ統一する。

## 23.4 descriptor

bindingを参照shader ABIへ完全一致させる。

現在の単一structured buffer bindingを、top/bottom packed bufferへ分離する。

## 23.5 composition dispatch

次を一つのcommand bufferへ記録する。

```text
packed 2D upload barrier
3D image barrier
history image barrier
capture buffer barrier
compositor bind
descriptor bind
push constants
vkCmdDispatch
output image barrier
timeline signal
```

## 23.6 previous source

topとbottomのprevious 3D sourceを別々に保持する。

screen swap、capture、class4表示で必要な履歴をframe identity付きで管理する。

---

# 24. フェーズR19: `FrameQueue`を正式な出力所有者にする

## 24.1 frame構造

desktop Vulkan用に次を持たせる。

```cpp
struct Frame
{
    FrameBackend backend;
    u32 width;
    u32 height;
    u64 frameId;

    VkImage image;
    VkImageView imageView;
    VkDeviceMemory memory;

    VkFence renderFence;
    VkSemaphore renderTimeline;
    u64 renderTimelineValue;

    VkFence presentFence;
    VkSemaphore presentTimeline;
    u64 presentTimelineValue;
};
```

## 24.2 queue size

参照実装の9 frame policyを基準にする。

fast forward時のbacklogとnormal frameのlatency policyを分離する。

## 24.3 state

```text
free
rendering
ready to present
pending present
previous frame
retired
```

を明確にする。

## 24.4 frame reuse

presenterが参照中のframeをrendererへ戻さない。

previous sourceとして保持中のframeを上書きしない。

## 24.5 generation

renderer再作成、scale変更、ROM切替、savestate復帰でgenerationを更新する。

旧generation frameを新しいpresenterへ渡さない。

---

# 25. フェーズR20: desktop surface adapterを実装する

## 25.1 共通interface

```cpp
class VulkanDesktopSurface
{
public:
    virtual ~VulkanDesktopSurface() = default;

    virtual VkSurfaceKHR CreateSurface(
        VkInstance instance) = 0;

    virtual QSize PixelSize() const = 0;
    virtual void* NativeHandle() const = 0;
};
```

## 25.2 Windows

Qt windowの`winId()`から`HWND`を取得する。

```cpp
VkWin32SurfaceCreateInfoKHR
```

を使う。

`HINSTANCE`は`GetModuleHandleW(nullptr)`から取得する。

## 25.3 X11

Qt native interfaceからDisplayとWindowを取得する。

```cpp
VkXlibSurfaceCreateInfoKHR
```

またはXCB variantを使用する。

XlibとXCBを混在させない。

## 25.4 Wayland

Qt native interfaceから`wl_display`と`wl_surface`を取得する。

```cpp
VkWaylandSurfaceCreateInfoKHR
```

を使う。

surface再作成時に古い`wl_surface`を保持しない。

## 25.5 macOS

`CAMetalLayer`を持つnative viewからMetal surfaceを生成する。

```cpp
VkMetalSurfaceCreateInfoEXT
```

MoltenVK build gate内へ限定する。

---

# 26. フェーズR21: `VulkanSurfacePresenter`をQtへ接続する

## 26.1 `ScreenPanelVulkan`のstubを削除する

次を廃止する。

```cpp
void ScreenPanelVulkan::drawScreen()
{
    ScreenPanelNative::drawScreen();
}
```

## 26.2 `ScreenPanelVulkan`所有物

```cpp
std::unique_ptr<VulkanDesktopSurface> SurfaceHost;
std::unique_ptr<VulkanSurfacePresenter> Presenter;
std::unique_ptr<VulkanOutputBridge> OutputBridge;
int SurfaceId;
```

## 26.3 init

```text
native child window作成
VulkanContext Acquire
surface作成
presenter init
attachSurface
layout config生成
background config生成
```

## 26.4 draw

```text
rendererのlatest Frame取得
composition inputs取得
surface config更新
presentFrame
present完了後frameをqueueへ返却
```

CPU framebufferを取得しない。

## 26.5 resize

resize eventでは次だけを行う。

```text
pixel size更新
swapchain dirty
surface config更新
```

3D rendererとtexture cacheを再作成しない。

## 26.6 layout

既存`ScreenLayout`から次を`VulkanSurfaceConfig`へ変換する。

```text
top rect
bottom rect
hybrid top rect
hybrid bottom rect
alpha
draw order
background mode
filter mode
```

## 26.7 HUD、OSD、radar

最終outputをCPUへ戻さない。

次のどちらかでGPU合成する。

```text
presenter内overlay pass
compositor outputへの追加layer
```

radarはbottom screen textureを直接sampleする。

---

# 27. フェーズR22: renderer factoryとEmuThreadを一本化する

## 27.1 factory

```cpp
case renderer3D_Vulkan:
    return std::make_unique<VulkanRenderer>(
        nds,
        VulkanRendererMode::Graphics);
```

`SoftRenderer`を返さない。

## 27.2 override factoryを削除する

```text
CreateRenderer3DOverrideForSelection
```

を削除する。

## 27.3 EmuThread

`updateRenderer()`は次だけを行う。

```text
requested renderer正規化
presentation backend決定
新Renderer生成
GPU::SetRenderer
RendererSettings適用
screen panel backend切替通知
```

GPU3D overrideの作成、設定、破棄を行わない。

## 27.4 presentation backend

```text
Software → NativeQt
OpenGL → OpenGL
Metal → Metal
Vulkan → Vulkan
```

Vulkan rendererなのにNativeQt CPU presenterを選ばない。

## 27.5 Vulkan Compute設定migration

既存設定値がVulkan Computeの場合はVulkan Graphicsへ正規化する。

設定保存時も正式なVulkan IDへ書き戻す。

---

# 28. フェーズR23: frame lifecycleを参照実装へ揃える

## 28.1 frame開始

```text
Renderer::Start3DRendering
VulkanRenderer3D::RenderFrame
```

## 28.2 VCount 144

参照実装のearly submit/capture処理を呼ぶ。

```text
Renderer::VCount1443D
VulkanRenderer3D::VCount144
```

## 28.3 scanline 0～191

```text
2D metadata生成
capture必要時だけ3D line取得
```

通常2D合成のために`GetLine()`を毎line呼ばない。

## 28.4 frame完了

```text
VulkanRenderer3D::FinishRendering
structured 2D frame publish
VulkanOutput::prepareFrameForPresentation
VulkanOutput::composeAndSubmitFrame
FrameQueue::pushRenderedFrame
```

## 28.5 VBlank

```text
frame identity確定
history更新
presenterへframe ready通知
```

---

# 29. フェーズR24: 同期とresource lifetimeを整理する

## 29.1 fence

render context再利用に使う。

## 29.2 timeline semaphore

次のproducer valueを持つ。

```text
3D complete
composition complete
present complete
```

## 29.3 barrier

最低限次を明示する。

```text
host write → vertex shader read
host write → compute shader read
color attachment write → sampled image read
storage image write → sampled image read
shader write → indirect command read
transfer write → shader read
composition write → presenter fragment read
```

## 29.4 queue family ownership

graphics queueとpresent queueが異なる場合はownership transferを行う。

同じqueue familyでは`VK_QUEUE_FAMILY_IGNORED`を使う。

## 29.5 destroy順

```text
new frame publish停止
presenter frame取得停止
present中frame完了
FrameQueue clear
VulkanOutput shutdown
VulkanRenderer3D Stop
texture cache破棄
surface presenter shutdown
surface破棄
VulkanContext Release
```

---

# 30. フェーズR25: pipeline cacheを整理する

## 30.1 key

```text
vendor ID
device ID
driver version
pipeline cache UUID
reference port version
shader manifest hash
descriptor indexing mode
```

## 30.2 owner

3D pipeline cacheは`VulkanRenderer3D`が所有する。

compositor/presenter pipeline cacheはfrontend側が所有する。

同じcache fileへ異なるpipeline ABIを書かない。

## 30.3 lazy creation

基本pipelineを初期化時に作る。

大量variantは最初の利用時に作る。

---

# 31. フェーズR26: current A1～A7重複実装を削除する

## 31.1 `GPU_Vulkan.*`

削除:

```text
VulkanRendererShellContract
DescribeVulkanRendererShell
CreateSapphireVulkanRenderer3D
```

置換:

```text
VulkanRenderer
VulkanRuntimeCapabilities
```

## 31.2 `GPU3D.*`

削除:

```text
CurrentRenderer
SetCurrentRenderer
GetCurrentRendererOverride
```

## 31.3 `Renderer`

削除:

```text
GPU3D override lookup
```

## 31.4 `SoftRenderer`

削除:

```text
Vulkan structured metadata frame owner
Vulkan BeginStructured2DFrame呼出し
Vulkan SubmitStructured2DLine呼出し
Vulkan EndStructured2DFrame呼出し
```

structured 2D producerへ移動する。

## 31.5 `GPU3D_Vulkan.*`

削除:

```text
Sapphire composition structs
Sapphire composition image
Sapphire composition descriptor
Sapphire composition command context
Sapphire composition shader module
structured 2D frame storage
structured 2D GPU upload storage
```

## 31.6 `MelonPrimeScreenVulkan.*`

stub実装を削除する。

Native CPU presenterへの委譲を削除する。

## 31.7 backup directory

build、source discovery、code searchへ影響するbackup source treeをrepository外へ移す。

少なくとも次を通常source treeとして扱わない。

```text
.melonprime_sapphire_vulkan_*_backup
.melonprime_vulkan_v0_v5_backup
```

CMake globを使用しない。

---

# 32. CMake実装

## 32.1 core target

`MELONPRIME_VULKAN_ACTIVE`内へ次を追加する。

```text
GPU_Vulkan.cpp
GPU2D_Vulkan.cpp
GPU3D_AcceleratedFrontend.cpp
GPU3D_Vulkan.cpp
GPU3D_TexcacheVulkan.cpp
VulkanContext.cpp
VulkanDesktopCompat.cpp
generated 3D shader headers
```

## 32.2 frontend target

```text
MelonPrimeScreenVulkan.cpp
MelonPrimeVulkanSurfaceHost.cpp
VulkanReference/VulkanOutput.cpp
VulkanReference/FrameQueue.cpp
VulkanReference/VulkanSurfacePresenter.cpp
generated compositor/presenter shader headers
```

## 32.3 platform definitions

### Windows

```text
VK_USE_PLATFORM_WIN32_KHR
```

### Linux X11

```text
VK_USE_PLATFORM_XLIB_KHR
```

またはXCB。

### Linux Wayland

```text
VK_USE_PLATFORM_WAYLAND_KHR
```

### macOS

```text
VK_USE_PLATFORM_METAL_EXT
```

## 32.4 shader generation dependency

coreとfrontendが同じgenerated shader targetへ依存する。

生成前headerをsource treeへ手動コピーしない。

## 32.5 build gate

```text
MELONPRIME_ENABLE_VULKAN
MELONPRIME_FORCE_DISABLE_VULKAN
MELONPRIME_ENABLE_VULKAN_MOLTENVK
```

の3段階を維持する。

force disable時はsource、definition、dependency、platform linkを全て除外する。

---

# 33. ファイル別変更一覧

| ファイル | 実装内容 |
|---|---|
| `src/GPU.h` | Vulkan output descriptor、lease、正式renderer output種別 |
| `src/GPU.cpp` | renderer切替順序、override処理削除 |
| `src/GPU3D.h` | CurrentRenderer削除、capture/accelerated hook整理 |
| `src/GPU3D.cpp` | override lifecycle削除 |
| `src/GPU_Soft.h` | Vulkan structured frame storage削除 |
| `src/GPU_Soft.cpp` | Vulkan metadata送信処理削除 |
| `src/GPU2D_Soft.h` | structured output hookのbackend-neutral化 |
| `src/GPU2D_Soft.cpp` | plane/control metadata出力 |
| `src/GPU_Vulkan.h` | 正式`VulkanRenderer` |
| `src/GPU_Vulkan.cpp` | outer renderer lifecycle、output publish |
| `src/GPU2D_Vulkan.h` | structured 2D producer |
| `src/GPU2D_Vulkan.cpp` | 2D metadata frame構築 |
| `src/GPU3D_Vulkan.h` | rc4準拠3D renderer、composition責務削除 |
| `src/GPU3D_Vulkan.cpp` | GPU 3D raster、capture export |
| `src/GPU3D_TexcacheVulkan.*` | GPU texture cache |
| `src/VulkanContext.*` | desktop instance/device/queue共有 |
| `src/VulkanDesktopSurface.*` | platform surface抽象 |
| `src/frontend/qt_sdl/VulkanReference/VulkanOutput.*` | final GPU composition |
| `src/frontend/qt_sdl/VulkanReference/FrameQueue.*` | frame ownership |
| `src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.*` | swapchain presentation |
| `src/frontend/qt_sdl/MelonPrimeScreenVulkan.*` | Qt接続、layout、HUD |
| `src/frontend/qt_sdl/MelonPrimeRendererFactory.cpp` | 正式VulkanRenderer生成 |
| `src/frontend/qt_sdl/EmuThread.cpp` | override作成削除、backend切替一本化 |
| `src/CMakeLists.txt` | core Vulkan source、shader generation |
| `src/frontend/qt_sdl/CMakeLists.txt` | presenter、surface adapter、platform definition |

---

# 34. 実装順序

実装順序は次で固定する。

```text
R0  偽装contractとactual backend報告を解除
R1  0.7.0.rc4固定ソースを再同期
R2  GPU3D CurrentRenderer overrideを削除
R3  正式なVulkanRendererを実装
R4  VulkanContextをdesktop対応
R5  shader生成を一本化
R6  VulkanRenderer3Dからcomposition責務を除去
R7  render targetを完成
R8  render context ringを完成
R9  accelerated sceneを正式入力化
R10 texture cacheを完成
R11 clear plane / clear bitmap
R12 opaque path
R13 translucent path
R14 shadow path
R15 toon / highlight / edge / fog / AA
R16 display capture
R17 structured 2D producer分離
R18 VulkanOutput final composition
R19 FrameQueue正式接続
R20 desktop surface adapter
R21 VulkanSurfacePresenterとQt接続
R22 factoryとEmuThread一本化
R23 frame lifecycle統合
R24 synchronizationとlifetime
R25 pipeline cache
R26 A1～A7重複実装とstub削除
```

次の順序へ逆転しない。

```text
presenterを先に装飾
GPU3D overrideを残したままfinal compositor追加
SoftRenderer framebufferを正式outputとして維持
A3～A7 compositorとVulkanOutputを併走
```

---

# 35. 各段階の実装完了状態

## R0～R3完了時

```text
Vulkan選択でVulkanRendererが生成される
SoftRendererは生成されない
GPU3D overrideは存在しない
VulkanRendererがVulkanRenderer3Dを所有する
```

## R4～R10完了時

```text
desktop Vulkan deviceが共有される
3D polygonがGPU targetへ描画される
textureがGPU cacheからsampleされる
scaleが3D target寸法へ反映される
```

## R11～R16完了時

```text
clear、opaque、translucent、shadow、toon、edge、fog、captureがGPU経路へ入る
通常表示がSoftware 3Dへ依存しない
```

## R17～R21完了時

```text
2D metadataがVulkanOutputへ渡る
final two-screen imageがGPU上で生成される
Qt presenterがswapchainへ直接表示する
Native CPU presenterを通らない
```

## R22～R26完了時

```text
renderer切替が一つのobject lifecycleになる
旧A1～A7重複resourceが消える
stub、shell、override、CPU safety presentationが消える
0.7.0.rc4型のVulkan経路だけが残る
```

---

# 36. 最終コード構造

```text
Software
    SoftRenderer
    ├─ SoftRenderer2D
    └─ SoftRenderer3D

OpenGL
    GLRenderer
    ├─ GLRenderer2D
    └─ GLRenderer3D / ComputeRenderer3D

Metal
    MetalRenderer
    ├─ Metal 2D
    └─ Metal 3D

Vulkan
    VulkanRenderer
    ├─ VulkanStructured2DProducer
    ├─ VulkanRenderer3D
    ├─ VulkanOutput
    └─ FrameQueue
```

presenter:

```text
NativeQt presenter
    Software output用

OpenGL presenter
    OpenGL output用

Metal presenter
    Metal output用

VulkanSurfacePresenter
    Vulkan output用
```

---

# 37. 最終通常フレーム

```text
GPU3D::RenderPolygonRAM
    ↓
BuildAcceleratedScene
    ↓
VulkanRenderer3D::RenderFrame
    ↓
VkImage ColorImage
    ↓
VulkanStructured2DProducer
    ↓
VulkanOutput::prepareFrameForPresentation
    ↓
VulkanOutput::composeAndSubmitFrame
    ↓
FrameQueue::pushRenderedFrame
    ↓
ScreenPanelVulkan
    ↓
VulkanSurfacePresenter::presentFrame
    ↓
vkQueuePresentKHR
```

この経路にSoftware 3D framebuffer、CPU ownership mask、QImage 3D source、Native painter fallbackを含めない。

---

# 38. 禁止する実装

- Vulkan選択時に`SoftRenderer`をouter rendererとして返す
- `GPU3D::CurrentRenderer`でVulkanを横取りする
- `Renderer::GetRenderer3D()`でglobal overrideを探索する
- Software 3DとVulkan 3Dを通常フレームで併走させる
- `GetLine()`を通常2D合成のため毎scanline呼ぶ
- Vulkan 3DをCPU BGRAへ戻して表示する
- CPU BGRAをVulkan textureへ再uploadする
- `ScreenPanelVulkan::drawScreen()`からNative presenterを呼ぶ
- GPU3D renderer内部へfinal 2D compositorを持たせる
- A3～A7 compositorと`VulkanOutput`を二重所有する
- Compute未実装なのにComputeとして表示する
- `NativeVulkan3DImplemented=true`だけで完成扱いする
- pipeline creation前のshader moduleだけで完成扱いする
- descriptor作成だけでfinal composition完成扱いする
- normal frameで`vkDeviceWaitIdle()`を呼ぶ
- present中frameをrendererが再利用する
- live GPU registerをGUI threadから読む
- live VRAMをpresenterから読む
- scaleをpresenter拡大だけで処理する
- platform surface処理を参照renderer本体へ混ぜる
- backup source treeをbuild対象へ含める

---

# 39. 最終到達点

Vulkan選択時に次が成立する。

```text
3D polygon rasterization        = Vulkan GPU
texture sampling                = Vulkan GPU
depth test                      = Vulkan GPU
stencil                         = Vulkan GPU
translucency                    = Vulkan GPU
shadow                          = Vulkan GPU
toon / highlight                = Vulkan GPU
edge marking                    = Vulkan GPU
fog                             = Vulkan GPU
internal resolution scaling     = Vulkan 3D render target
2D / 3D final composition       = Vulkan GPU
screen swap                     = Vulkan compositor
final two-screen output         = Vulkan image
window presentation             = Vulkan swapchain
normal frame CPU readback       = なし
Software 3D dependency          = なし
Native CPU presenter dependency = なし
```

0.7.0.rc4へ近づける際の中心は、既に存在する個別部品を追加し続けることではない。

中心作業は次である。

```text
参照実装の所有関係
参照実装のframe lifecycle
参照実装のVulkanOutput
参照実装のFrameQueue
参照実装のSurfacePresenter
```

を一つの実行経路として接続し直し、現在の`SoftRenderer + GPU3D override + compositor duplicate + CPU presenter stub`を完全に置き換えることである。
