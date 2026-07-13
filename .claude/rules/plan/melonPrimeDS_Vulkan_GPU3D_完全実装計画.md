# melonPrimeDS Vulkan GPU 3D完全実装計画

## 1. 目的

この計画の目的は、melonPrimeDSのVulkan選択時に、Software Rendererが生成した3D画面をVulkan presenter側で拡大・再構築する現在の方式を廃止し、Nintendo DSの3DポリゴンをVulkanのgraphics pipelineまたはcompute pipelineで直接ラスタライズする構造へ置き換えることである。

基準実装は次の固定版とする。

- フロントエンド: `SapphireRhodonite/melonDS-android` タグ `0.7.0.rc4`
- コア: `SapphireRhodonite/melonDS-android-lib` コミット `d77944275fa61f9b79cfcead2c3e98993429a023`
- 移植先: `ag-advania/melonPrimeDS` ブランチ `highres_fonts_v3`

最終構造では、Vulkanを選択したフレームの3D描画を`SoftRenderer3D`へ委譲しない。CPU上のSoftware 3D framebuffer、Software 3D ownership mask、Software 3D line callback、presenter側の3D再ラスタライズ用CPU snapshotを通常経路から除外する。

2D描画は段階的にVulkanへ移すが、最初のGPU 3D接続時点ではSoftware 2Dを一時的に残してよい。ただし、3Dだけはコアの`Renderer3D`としてVulkan GPUが所有し、Software 3Dを正解画像として併走させない。

---

## 2. 現在の構造から置き換える対象

現在の`VulkanRenderer`は`SoftRenderer`を継承している。`DrawScanline()`、`GetOutput()`、framebuffer生成はSoftware経路に依存し、`NativeRasterSnapshotBuilder`がCPU側で次の処理を行っている。

- Software 3Dの256×192出力をコピー
- Software 2Dが直接3Dを選択した画素のownership maskをコピー
- texture VRAMとpalette VRAMをCPUへcoherent化
- DS textureをCPUでdecode
- ポリゴン、texture、clear bitmap、各種registerをimmutable snapshotへ格納
- Qt presenter側の`NativeRasterGpu`へsnapshotを渡して、表示用Vulkan imageを別途生成

この方式では、エミュレーションコアの3D rendererはSoftwareのままであり、Vulkanは表示直前の再構築器になっている。これを次の責務へ分解し直す。

```text
GPU3D command processing
    ↓
AcceleratedScene生成
    ↓
VulkanRenderer3D
    ├─ texture cache更新
    ├─ vertex/index upload
    ├─ clear plane / clear bitmap
    ├─ opaque / translucent / shadow raster
    ├─ edge / fog / antialiasing final pass
    └─ display capture用line export
    ↓
Vulkan 3D image
    ↓
VulkanRenderer 2D/final compositor
    ↓
GPU-resident two-screen output ring
    ↓
ScreenPanelVulkan presenter
    ↓
QVulkanWindow swapchain
```

---

## 3. 実装上の固定方針

### 3.1 3D rendererの所有位置

実体は`src/GPU3D_Vulkan.h`と`src/GPU3D_Vulkan.cpp`へ置き、次の型を実装する。

```cpp
class VulkanRenderer3D final : public Renderer3D
```

この型が`Renderer3D` lifecycleを直接実装する。

```cpp
Reset()
RenderFrame()
FinishRendering()
RestartFrame()
GetLine()
SetupAccelFrame()
PrepareCaptureFrame()
BeginCaptureFrame()
Blit()
Stop()
```

melonPrimeDS独自のphase contract群を実体rendererの代わりに使わない。contract helperは、実体rendererから利用される純粋な変換関数だけ残す。

### 3.2 外側のrenderer

`VulkanRenderer`は最終的に`SoftRenderer`継承をやめ、OpenGLの`GLRenderer`と同じく`Renderer`を直接継承する。

```cpp
class VulkanRenderer final : public Renderer
```

内部に次を所有する。

```text
Rend3D       = VulkanRenderer3D
Rend2D_A     = Vulkan用2D adapterまたはstructured Software 2D producer
Rend2D_B     = Vulkan用2D adapterまたはstructured Software 2D producer
VulkanOutput = final compositorとoutput ring
```

移行途中にSoftware 2Dを使う場合も、`SoftRenderer`全体を基底クラスにせず、2D producerとして必要な部分だけを独立させる。

### 3.3 GPU contextの共有

renderer、texture cache、final compositor、presenterが別々に`VkInstance`と`VkDevice`を生成しない。`VulkanContext`を唯一の所有者とし、reference-countedな共有contextにする。

```text
VulkanContext
    VkInstance
    VkPhysicalDevice
    VkDevice
    VkQueue
    QueueFamilyIndex
    QueueLock
    feature profile
    timeline semaphore functions
    descriptor indexing capabilities
    timestamp functions
```

Qtの`QVulkanWindow`がdeviceを所有する設計と、コアがdeviceを所有する設計を混在させない。melonPrimeDSでは、コアとpresenterで同じdeviceを使えるよう、Qt側へ既存instanceを渡すか、Qtのinstance/device生成をVulkanContextの初期化へ統合する。

### 3.4 通常フレームのCPU readback禁止

通常表示用フレームは次の形式だけで渡す。

```cpp
RendererOutputKind::VulkanImage
```

通常フレームで`RendererOutputKind::CpuBgra`へ戻さない。CPU readbackを残す用途は次だけに限定する。

- DS display captureがCPU/VRAM側の厳密なline dataを必要とする場合
- screenshot APIがCPU画像を要求する場合
- renderer切替時の一時的な最後のframe保持

### 3.5 frame ownership

すべてのVulkan出力へ次を付与する。

```text
FrameSerial
Generation
ProducerTimeline
ProducerValue
ImageLayout
QueueFamilyIndex
EngineALayer
```

presenterはlive GPU stateやlive VRAMを読まない。rendererがpublishしたimmutable outputだけをleaseして使用する。

---

## 4. 実装ステージ1: Vulkan基盤を実体化する

### 4.1 追加するファイル

reference実装を基準に、次をmelonPrimeDSへ導入する。

```text
src/VulkanDispatch.h
src/VulkanDispatch.cpp
src/VulkanContext.h
src/VulkanContext.cpp
src/VulkanPerfStats.h
```

### 4.2 `VulkanDispatch`

`VK_NO_PROTOTYPES`を前提とし、volkまたはreference側のdispatch wrapperのどちらか一方へ統一する。現在のbuild gateでvolkを使用しているため、melonPrimeDSではvolkを基盤にし、reference側のAPI surfaceだけ合わせる。

実装する責務:

- global function loading
- instance function loading
- device function loading
- Vulkan loader未存在時の明示的な失敗理由
- optional extension function pointerの保持
- device再生成時のdispatch再設定

### 4.3 `VulkanContext`

次のAPIを持つsingletonまたはprocess-shared objectとして実装する。

```cpp
bool Acquire();
void Release();
bool IsReady() const;
VkInstance GetInstance() const;
VkPhysicalDevice GetPhysicalDevice() const;
VkDevice GetDevice() const;
VkQueue GetQueue() const;
u32 GetQueueFamilyIndex() const;
std::mutex& GetQueueLock();
const VulkanDeviceProfile& GetDeviceProfile() const;
```

初期化処理へ次を含める。

- Vulkan 1.1以上を基本APIとする
- graphics queueを持つqueue family選択
- swapchain利用可能queue family選択
- timeline semaphore機能
- dynamic texture indexing機能
- non-uniform texture indexing機能
- host query reset機能
- pipeline cache UUID取得
- vendor ID、device ID、device名によるdevice profile生成
- Windows/Linux/macOS MoltenVKごとのinstance extension構成
- surface extensionをplatform別に組み立てる
- queueはrendererとpresenterの共有resourceとして管理する

### 4.4 device profile

少なくとも次のprofile flagを保持する。

```text
NVIDIA
AMD
Intel
Qualcomm / Adreno
ARM / Mali
PowerVR
MoltenVK
Software Vulkan device
```

profileによる分岐はshaderの正しさを変えず、descriptor indexing、timeline semaphore、subgroup、pipeline構成、memory placementの選択だけに使う。

### 4.5 build gate整理

`MELONPRIME_VULKAN_ACTIVE`内へ次をまとめる。

```text
Vulkan source
Vulkan shader generation
volk
VulkanMemoryAllocator
platform surface library
Qt Vulkan presenter source
MoltenVK frameworkまたはdynamic library
```

`MELONPRIME_FORCE_DISABLE_VULKAN=ON`では、上記を完全に除外する。

---

## 5. 実装ステージ2: `VulkanRenderer3D`をコアへ接続する

### 5.1 現在の`GPU3D_Vulkan.h`を再編する

現在のphase contract、packed upload helper、pipeline state helperを次の3層へ分割する。

```text
GPU3D_Vulkan.h/.cpp
    実体のVulkanRenderer3D

GPU3D_VulkanScene.h/.cpp
    AcceleratedSceneからGPU upload形式への変換

GPU3D_VulkanPipelineState.h/.cpp
    DS polygon stateからVulkan pipeline keyへの変換
```

### 5.2 `VulkanRenderer3D` public API

reference版と同等のAPIを持たせる。

```cpp
static std::unique_ptr<VulkanRenderer3D> New() noexcept;

void Reset(GPU& gpu) override;
void RenderFrame(GPU& gpu) override;
void RestartFrame(GPU& gpu) override;
u32* GetLine(int line) override;

void SetupAccelFrame() override;
void PrepareCaptureFrame() override;
void BeginCaptureFrame() override;
void SetCaptureScreenSwapHint(bool screenSwap) override;
void Blit(const GPU& gpu) override;
void Stop(const GPU& gpu) override;

void SetRenderSettings(
    bool threaded,
    bool betterPolygons,
    int scale,
    bool useSimplePipeline,
    bool coverageEnabled,
    float coveragePixels,
    float coverageDepthBias,
    bool coverageRepeat,
    bool coverageClamp,
    GPU& gpu) noexcept;
```

### 5.3 `VulkanRenderer`初期化

`GPU_Vulkan.cpp`の`Init()`で`SoftRenderer::Init()`を呼ばない。代わりに次を生成する。

```cpp
Rend3D = VulkanRenderer3D::New();
Rend2D_A = std::make_unique<VulkanRenderer2D>(...);
Rend2D_B = std::make_unique<VulkanRenderer2D>(...);
Output = std::make_unique<VulkanOutput>(...);
```

最初の移行状態でVulkan 2Dが未完成なら、`StructuredSoftRenderer2D`を導入する。この型は2D layer metadataを生成するだけで、3D rasterをSoftwareへ戻さない。

### 5.4 renderer lifecycle接続

`GPU::StartFrame()`、`StartHBlank()`、`FinishFrame()`、`Restart3DFrame()`から既存`Renderer` APIを通じてVulkanRenderer3Dへ到達させる。

次の状態遷移を実装する。

```text
GPU3D RenderFrame開始
    ↓
RenderContext取得
    ↓
AcceleratedScene生成
    ↓
texture cache更新
    ↓
command buffer記録
    ↓
GPU submit
    ↓
3D target publish
    ↓
2D/final compositorが同じframe serialで参照
```

### 5.5 Software 3D依存の除去

次をVulkan通常経路から削除する。

```text
VulkanRenderer::OnRendered3DLine
VulkanRenderer::OnComposed3DOwnershipLine
Native3DFrame
Native3DVisible
Native3DBgra
CopyNative3DForPresenter
CopyNative3DOwnershipForPresenter
NativeReferenceBgra
Software 3D ownership alpha bit
```

`SoftRenderer3D`はSoftware renderer選択時だけ生成される状態へ戻す。

---

## 6. 実装ステージ3: scene frontendとGPU upload ABIを統一する

### 6.1 `GPU3D_AcceleratedFrontend`を共通入口にする

既に移植されている`GPU3D_AcceleratedFrontend`を、Vulkan graphics pathとVulkan compute pathの唯一のscene builderにする。

`AcceleratedScene`へ次を格納する。

- fixed-point X/Y
- high-resolution X/Y
- Z
- W
- vertex color
- alpha
- texture coordinate
- polygon attribute
- polygon ID
- W-buffer flag
- depth-equal flag
- depth-write flag
- fog-write flag
- translucent flag
- shadow mask flag
- shadow flag
- NeedOpaque flag
- line primitive情報
- boundary edge index
- source polygon order

### 6.2 source order保持

ポリゴンは`RenderPolygonRAM`順を保持する。pipeline keyが同じでもframe全体を再ソートしない。

batch化は隣接範囲だけを結合する。

```text
A / B / A
```

は3 batchのままとする。これによりDSの半透明、shadow、poly ID依存順序を維持する。

### 6.3 GPU vertex ABI

reference版の`GraphicsVertexGpu`相当を採用する。

```cpp
struct VulkanGraphicsVertex
{
    float x;
    float y;
    float z;
    float reciprocalW;
    float u;
    float v;
    u32 colorRgba8;
    u32 flags;
    u32 textureIndex;
    u32 textureWidth;
    u32 textureHeight;
    u32 texParam;
    u32 polyAttr;
};
```

現在の28-byte packed vertex contractは、bootstrap互換層として残さず、実描画用ABIへ一本化する。必要なfull precision Z/Wを別bufferへ分けるより、graphics pathで直接消費できるvertex ABIへ統合する。

### 6.4 GPU polygon/draw ABI

```cpp
struct VulkanGraphicsDraw
{
    u32 firstVertex;
    u32 vertexCount;
    u32 firstIndex;
    u32 indexCount;
    u32 firstEdgeIndex;
    u32 edgeIndexCount;
    u32 polygonFlags;
    u32 polyAttr;
    u32 textureDescriptorIndex;
    u32 pipelineVariant;
};
```

CPU側は各drawのpipeline variantとtexture descriptor indexだけを決定し、shader内部で巨大なstate switchを行わない。

### 6.5 line polygon

DS line polygonは、reference版と同じaccelerated frontendでtriangle quadへ展開する。

- source line endpointsを解決
- scaleに応じた1-pixel相当幅を付与
- clipping範囲をscale後targetへ合わせる
- boundary edge metadataを維持
- alpha-zero polygonのedge用途も同じ展開規則へ統合

### 6.6 Better Polygonsとhigh-resolution coordinates

`RendererSettings`の次をscene build configへ直接渡す。

```text
ScaleFactor
BetterPolygons
HiresCoordinates
```

scale変更はpresenterだけに適用せず、vertex position、render target size、line width、coverage expansion、clear bitmap offset、fog/edge final passの全てへ反映する。

---

## 7. 実装ステージ4: render targetとframe contextを構築する

### 7.1 render target構成

scaleごとに次のimageを生成する。

```text
ColorImage
    RGBA8またはRGBA8_UINT
    color attachment
    storage
    sampled
    transfer source

AttributeImage
    polygon ID
    fog flag
    coverage/edge属性
    color attachmentまたはstorage
    sampled

DepthStencilImage
    depth attachment
    stencil attachment

FinalColorImage
    final edge/fog適用後
    sampled
    storageまたはcolor attachment
    transfer source
```

widthとheightは次で決定する。

```text
width  = 256 × ScaleFactor
height = 192 × ScaleFactor
```

### 7.2 format選択

deviceごとにdepth/stencil formatを次の優先順位で選ぶ。

```text
VK_FORMAT_D32_SFLOAT_S8_UINT
VK_FORMAT_D24_UNORM_S8_UINT
VK_FORMAT_D16_UNORM_S8_UINT
```

attribute imageはDS属性をlosslessに扱えるinteger formatを優先する。

### 7.3 `RenderContext` ring

最低3、reference版に寄せる場合は6個のrender contextを持つ。

各contextが所有するもの:

```text
VkCommandPool
VkCommandBuffer
VkFence
VkDescriptorSet
vertex buffer
index buffer
edge index buffer
draw metadata buffer
toon table buffer
clear state buffer
capture line buffer
timestamp query pool
```

context再利用時は、そのcontextのfenceだけを待つ。通常フレームで`vkDeviceWaitIdle()`を呼ばない。

### 7.4 dynamic buffer growth

vertex、index、draw、capture bufferはcapacityを保持し、必要量が増えたときだけ再作成する。

```text
newCapacity = max(required, oldCapacity × 2)
```

host-visible persistent mappingを使い、毎フレームmap/unmapしない。

### 7.5 image layout規約

renderer内部のlayout遷移を固定する。

```text
UNDEFINED
→ COLOR_ATTACHMENT_OPTIMAL / GENERAL
→ SHADER_READ_ONLY_OPTIMAL
```

compute pathのstorage imageは`GENERAL`を使い、graphics final passまたはpresenter sampling前に明示barrierを入れる。

---

## 8. 実装ステージ5: clear planeとclear bitmapをGPU化する

### 8.1 clear plane

`RenderClearAttr1`と`RenderClearAttr2`をdecodeし、次をVulkan attachment clearへ変換する。

- RGB5
- alpha5
- depth24
- opaque polygon ID
- fog flag
- stencil初期値

clear planeが通常色の場合、`vkCmdClearAttachments`またはrender pass load clearを使う。

### 8.2 clear bitmap

`RenderDispCnt`のclear bitmap bitが有効な場合、VRAM texture領域からclear colorとclear depth/fogを取得し、GPU imageへuploadする。

実装順:

1. dirty VRAM rangeだけstaging bufferへcopy
2. clear bitmap color imageへtransfer
3. clear bitmap depth/fog imageへtransfer
4. fullscreen triangleまたはcompute passでscaled targetへ展開
5. X/Y offsetをpush constantで適用
6. color、depth、attribute、stencilを同じpassで初期化

CPUで256×256全画素を毎frame decodeして`std::vector`へ格納する現在のsnapshot方式を廃止する。

### 8.3 clear bitmap cache

VRAM dirty generationとoffset/registerをkeyにし、内容不変時は再uploadしない。

---

## 9. 実装ステージ6: Vulkan texture cacheをGPU常駐化する

### 9.1 texture cache API

reference版の`TexcacheVulkanLoader`相当を実装する。

```cpp
TextureHandle GenerateTexture(u32 width, u32 height, u32 layers);
void UploadTexture(TextureHandle handle, u32 width, u32 height, u32 layer, const void* data);
void DeleteTexture(TextureHandle handle);
bool GetTextureDescriptor(TextureHandle handle, VkDescriptorImageInfo* out) const;
bool IsTextureLayerOpaque(TextureHandle handle, u32 layer) const;
```

### 9.2 array texture

同サイズ・同formatのDS textureをarray layerへまとめる。

各array resource:

```text
VkImage
VkDeviceMemoryまたはVMA allocation
VkImageView array view
clamp sampler
repeat sampler
mirror sampler
staging buffer
layer opacity table
```

### 9.3 VRAM dirty連携

`GPU::VRAMDirty_Texture`と`GPU::VRAMDirty_TexPal`から変更rangeを取得し、該当textureだけinvalidateする。

現在の`NativeRasterSnapshotBuilder`が行うframe単位のCPU hash map、CPU decode vector、shared_ptr pixel保持を通常経路から削除する。

### 9.4 decode方式

最初は既存`GPU3D_Texcache`のCPU decode結果をstaging uploadしてよい。ただし、decode結果はrenderer-owned texture cacheへ直接入り、presenter snapshotへ複製しない。

その後、compute decodeへ置き換えられる構造にする。

```text
VRAM texture bytes
VRAM palette bytes
    ↓
GPU decode compute
    ↓
RGBA8_UINT texture array layer
```

### 9.5 sampler table

S/T軸の組み合わせを固定sampler tableへする。

```text
Clamp / Clamp
Clamp / Repeat
Clamp / Mirror
Repeat / Clamp
Repeat / Repeat
Repeat / Mirror
Mirror / Clamp
Mirror / Repeat
Mirror / Mirror
```

polygonごとにsampler objectを生成しない。

### 9.6 descriptor indexing

利用可能deviceではdescriptor arrayとnon-uniform indexingを使う。非対応deviceでは次の互換pathを持つ。

```text
1 texture descriptor set per draw group
```

互換pathでも同一のrender resultを生成し、CPU rasterへfallbackしない。

---

## 10. 実装ステージ7: graphics hardware raster pathを完成させる

### 10.1 基本render pass構成

```text
Pass 1: clear plane / clear bitmap
Pass 2: opaque polygons
Pass 3: NeedOpaque部分
Pass 4: translucent polygons
Pass 5: shadow mask
Pass 6: shadow self-reject
Pass 7: shadow blend
Pass 8: edge marking / fog / antialiasing final
```

DSのsource orderが必要な箇所では、batch順を維持したままpassを分割する。

### 10.2 opaque pipeline

pipeline variant keyへ次を含める。

```text
W-buffer / Z-buffer
LESS / EQUAL depth compare
texture enabled
modulate / decal / toon / highlight
fog attribute write
alpha-zero behavior
line / triangle
color attachment format
attribute attachment format
depth/stencil format
```

opaque fragmentの処理:

- vertex color補間
- perspective-correct texture coordinate
- DS texture sampling
- modulate/decal/toon/highlight
- alpha test
- color write
- polygon ID write
- fog flag write
- depth write
- stencil lower 6～7 bit更新

### 10.3 NeedOpaque pass

半透明polygonのうち、alpha=31になるtexture fragmentをopaqueとして先に処理するDS規則を分離する。

- opaque alpha fragmentはdepth writeあり
- translucent alpha fragmentは後段へ送る
- texture opacity tableを使って不要なNeedOpaque passを省く

### 10.4 translucent pipeline

次を実装する。

- source alpha / one-minus-source-alpha blend
- alpha channelはmax相当
- depth write flag
- depth equal flag
-同一translucent polygon IDの二重blend抑制
-異なるpolygon IDの重ね合わせ許可
- fog attribute write
- W-bufferとZ-bufferの両方

### 10.5 shadow pipeline

shadowを3段へ分ける。

```text
Shadow Mask
    depth fail時にstencil bit 7を設定

Shadow Reject
    自分自身のpolygon IDと一致する画素のbit 7を消す

Shadow Blend
    bit 7が残る画素だけ色をblend
```

stencil lower bitsのpolygon IDを保持し、shadow bitだけを独立して更新する。

### 10.6 toon/highlight

32-entry toon tableをuniform/storage bufferとして保持する。

- toon modeはvertex intensityからtable indexを決定
- highlight modeはtexture/vertex結果へhighlight色を加算
- textured/untexturedの両方
- opaque/translucentの両方
- W-buffer/Z-bufferの両方

### 10.7 texture combiner

DSのtexture modeを独立fragment variantへする。

```text
None
Modulate
Decal
Toon
Highlight
```

巨大なif-chainを1本のshaderへ詰めず、pipeline specializationまたはprecompiled variantへ分ける。

### 10.8 depth

Z-bufferとW-bufferを別variantにする。

- ZはDSの24-bit depth規則へ合わせる
- Wはreciprocal WとDS比較規則へ合わせる
- depth equal toleranceをscaleやfloating-point誤差へ無条件に広げない
- clear depthとpolygon depthのencodingを同一関数へ集約する

### 10.9 coverage expansion

repeat textureのtriangle間crackを防ぐpassive expansionをaccelerated frontendへ適用する。

- repeat texture時のみ既定の小さなexpansion
- clamp textureへの適用は設定で分離
- line quadと通常triangleを別扱い
- screen端でclamp
- high-resolution coordinateとBetter Polygons後の座標へ適用

---

## 11. 実装ステージ8: final passをGPU化する

### 11.1 edge marking

attribute imageのpolygon IDと隣接pixelを参照し、edge tableから色を取得する。

- edge marking enable bit
- polygon IDの上位bitからedge color table index
- hidden/alpha-zero polygonのedge規則
- screen border処理
- scale後pixelに対するDS 1-pixel edge幅の扱い

### 11.2 fog

次をbufferまたはpush constantで渡す。

```text
FogColor
FogOffset
FogShift
FogDensityTable[34]
```

attribute imageのfog flagが立つpixelだけに適用する。

### 11.3 antialiasing

coverageまたはedge coverageを保持し、DS antialiasing enable時だけfinal passで背景とのblendへ使う。

### 11.4 final target

final pass結果を`FinalColorImage`へ書く。3D imageはこの時点で次の状態にする。

```text
scale済み
edge適用済み
fog適用済み
antialiasing適用済み
presenterまたは2D compositorからsample可能
```

---

## 12. 実装ステージ9: display captureをVulkan 3Dへ接続する

### 12.1 capture lifecycle

`Renderer3D`の次を実体化する。

```text
PrepareCaptureFrame
BeginCaptureFrame
GetLine
Blit
```

### 12.2 capture line export

reference版の`GPU3D_Vulkan_CaptureLineExportShader.comp`相当を導入する。

- scaled 3D targetからDS native 256×192 lineへ変換
- RGB6A5形式へquantize
- capture source screen swap hintをsnapshot
- double-buffered capture line bufferへ出力
- producer frame serialとcapture serialを関連付ける

### 12.3 readback範囲

全画面readbackを常用せず、captureが必要なlineまたはframeだけをhost-visible bufferへcopyする。

### 12.4 capture texture連携

display captureがVRAMへ書かれ、そのVRAMが後続frameのtextureや2D layerとして使われる場合、capture completionとVRAM dirty generationを結び付ける。

```text
Vulkan 3D output
    ↓
Capture GPU buffer/image
    ↓
必要rangeだけVRAM同期
    ↓
texture cache / 2D producer invalidate
```

---

## 13. 実装ステージ10: Software 2Dとの構造化合成を導入する

### 13.1 目的

Vulkan 3DをSoftware 2Dの最終BGRA画像へ単純に貼り戻す方式では、window、blend、brightness、mosaic、capture-backed displayの意味を失う。reference frontendと同じく、Software 2Dから最終色ではなく構造化metadataをVulkan compositorへ渡す。

### 13.2 2D snapshot

各screenについて次をimmutable frame snapshotへ格納する。

```text
Plane0 pixels
Plane1 pixels
control words
line metadata
display mode
window state
blend mode
EVA / EVB
brightness mode / factor
mosaic state
3D slot position
screen swap
capture source metadata
```

### 13.3 `StructuredSoftRenderer2D`

Software 2Dのlayer evaluationを再利用しつつ、最終BGRAだけでなく次を出力するadapterを作る。

```cpp
struct VulkanPacked2DScreen
{
    std::array<u32, 256 * 192> Plane0;
    std::array<u32, 256 * 192> Plane1;
    std::array<u32, 256 * 192> Control;
    std::array<u32, 192> LineMeta;
};
```

MelonPrime専用変更は`MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN`内へ隔離する。

### 13.4 GPU compositor

`VulkanCompositorShader.comp`相当を移植し、次をGPU上で合成する。

- current high-resolution 3D image
- previous 3D source
- top/bottom packed 2D buffers
- display capture 3D source
- screen swap
- per-line state
- VRAM display
- FIFO display
- screen disable
- master brightness

### 13.5 high-resolution 3Dの選択

packed controlが3D layerを直接選択するpixelだけ、scaled Vulkan 3Dを使う。

2D blend/window/effectを受けるpixelはcontrol metadataに従ってcompositorが処理し、Software ownership maskの単純な0/1判定へ戻さない。

### 13.6 temporal source

captureや特殊display modeでprevious frame sourceが必要な場合、top/bottomごとにprevious Vulkan image leaseを保持する。

live frameとprevious frameを`FrameSerial`で区別し、presenterがlive GPU registerを参照しない。

---

## 14. 実装ステージ11: GPU-resident output ringをrendererへ統合する

### 14.1 output image

final compositorは2-screen array imageを生成する。

```text
width       = 256 × ScaleFactor
height      = 192 × ScaleFactor
layerCount  = 2
EngineALayer = 0または1
layout      = SHADER_READ_ONLY_OPTIMAL
```

### 14.2 output ring

既存`GPU_VulkanOutputRing`のcontract-only実装を実resource所有型へ置き換える。

各slotが所有するもの:

```text
VkImage
VMA allocation
VkImageView
producer timeline value
frame serial
generation
renderer in-flight flag
presenter reference count
```

### 14.3 producer lifecycle

```text
BeginProduce
    空きslot取得

Record/Submit
    3D + 2D + final composition

Publish
    descriptorを公開

MarkProducerComplete
    timeline value更新
```

presenter参照中slotへ次frameを書かない。空きslotがなければ最古未提示frameを置き換えるpolicyをrenderer側で持つが、present中slotは再利用しない。

### 14.4 `RendererOutput`

`VulkanRenderer::GetOutput()`はCPU pointerを返さず、次を返す。

```cpp
RendererOutput::VulkanImage(&descriptor)
```

`AcquireOutputLease()`はoutput ringのleaseを`RendererOutputLease`へ接続する。

### 14.5 generation invalidation

次でgenerationを増やす。

- renderer切替
- scale変更
- device再生成
- savestate復帰
- ROM close/reopen
- output format変更
- screen count/layout contract変更

旧generationのslotをpresenterへ新規貸出ししない。

---

## 15. 実装ステージ12: Qt presenterをrenderer-owned imageへ切り替える

### 15.1 `ScreenPanelVulkan`の入力変更

現在の`VulkanDirectFrameSnapshot`から次を削除する。

```text
QImage Screens[2]
NativeRasterFrame
NativeRasterViews
NativeRasterGpu
NativeReference
Coverage mask
```

代わりに`RendererOutputLease`だけを保持する。

```cpp
struct VulkanPresentFrame
{
    RendererOutputLease Lease;
    ScreenLayoutSnapshot Layout;
    HudSnapshot Hud;
    u64 FrameSerial;
    u64 Generation;
};
```

### 15.2 QVulkanWindow renderer

QVulkanWindowのframe callbackで次を行う。

1. latest Vulkan output lease取得
2. producer timeline value待機をqueue submitへ組み込む
3. output image viewをdescriptorへ設定
4. layout matrixに従ってtop/bottomをdraw
5. HUD/OSD/radar overlayを同じcommand bufferへdraw
6. swapchain imageへrender
7. present completion後にleaseをrelease

### 15.3 swapchain

surface stateごとに次を持つ。

```text
VkSurfaceKHR
VkSwapchainKHR
swapchain image views
framebuffers
render pass
present pipeline
image-available semaphore
render-finished semaphore
in-flight fence
```

window resize、fullscreen、DPI変更ではswapchainだけを再生成し、コアの3D rendererを再生成しない。

### 15.4 layout

既存`ScreenLayout`のmatrixをvertex shaderへ渡す。

- top only
- bottom only
- vertical
- horizontal
- hybrid
- screen swap
- rotation
- integer scaling
- gap
- custom HUD radar destination

### 15.5 filtering

nearest/linearはpresenter samplerで選ぶ。内部解像度そのものはrenderer scaleで決まり、presenter filterによる拡大を内部解像度として扱わない。

### 15.6 HUDとradar

HUDとradarはVulkan overlay textureまたはvertex geometryとして合成する。

下画面radarを上画面へ移す処理は、CPUで完成画面を切り貼りせず、bottom output layerをsource textureとしてradar destinationへsampleする。

---

## 16. 実装ステージ13: renderer factoryと設定経路を実体能力へ合わせる

### 16.1 factory

`MelonPrimeRendererFactory.cpp`でVulkanを選択した場合、shell contractではなく実体rendererを生成する。

```cpp
case renderer3D_Vulkan:
    return std::make_unique<VulkanRenderer>(nds, VulkanMode::Graphics);

case renderer3D_VulkanCompute:
    return std::make_unique<VulkanRenderer>(nds, VulkanMode::Compute);
```

### 16.2 actual backend

`BackendCreationReport.actual`は、`VulkanRenderer::Init()`が成功し、`VulkanRenderer3D`とoutput compositorの両方が生成された時だけVulkanにする。

初期化失敗時はfactoryまたはrenderer切替controllerがSoftware/OpenGLへ明示的に切り替える。Vulkan objectの内部でSoftware 3Dを動かし続けるfallbackは実装しない。

### 16.3 settings

次を`VulkanRenderer3D::SetRenderSettings()`へ接続する。

```text
Internal resolution
Better Polygons
High Resolution Coordinates
Threaded rendering
Coverage expansion
Texture filtering policy
Graphics / Compute mode
```

scaleは1、2、3、4、5、6、8、16のようなUI値をそのまま受けられる設計にし、shaderやbuffer allocation側で任意の1～16を扱う。

### 16.4 renderer切替

切替順序を固定する。

```text
新renderer生成
新renderer Init
設定適用
旧present lease回収
旧renderer Stop
GPU::SetRenderer
output generation更新
presenter descriptor cache破棄
```

---

## 17. 実装ステージ14: Vulkan Computeを実体化する

### 17.1 graphics pathを主系統にする

最初に`renderer3D_Vulkan`をgraphics hardware pathとして完成させる。`renderer3D_VulkanCompute`は、graphics pathと同じtexture cache、scene frontend、output ring、2D compositorを共有する。

### 17.2 compute stage graph

reference shader構成に合わせ、次の実体stageを実装する。

```text
InterpSpans Z/W
BinCombined
CalculateWorkOffsets
SortWork
TriRaster variants
DepthBlend Z/W
FinalPass variants
CaptureLineExport
```

現在の`GPU3D_VulkanCompute`にある33-stage enum、barrier graph、dispatch planを、契約評価関数ではなく実際の`VkPipeline`、descriptor、dispatchへ接続する。

### 17.3 compute resource

```text
triangle buffer
span setup buffer
bin mask buffer
group list buffer
work offset buffer
indirect dispatch buffer
result buffer
color storage image
attribute storage image
depth storage image
capture line buffer
```

### 17.4 indirect dispatch

binning結果からwork countを作り、`vkCmdDispatchIndirect`へ接続する。device limitを超える場合は複数chunkへ分割する。

### 17.5 barrier graph

stage間の依存を明示する。

```text
shader storage write
→ shader storage read

shader storage write
→ indirect command read

storage image write
→ sampled image read
```

CPU側のstage simulatorや`EvaluateVulkanComputeStageWord()`を可視出力経路に使わない。

### 17.6 mode selection

```text
Vulkan
    graphics hardware raster

Vulkan Compute Shader
    compute raster
```

どちらもGPU 3D rendererであり、片方だけSoftware 3D snapshotを使う状態を許さない。

---

## 18. 実装ステージ15: shader資産をreference構成へ整理する

### 18.1 core 3D shader

次を基準名として整理する。

```text
GPU3D_Vulkan_GraphicsRasterShader.vert
GPU3D_Vulkan_GraphicsRasterShader.frag
GPU3D_Vulkan_GraphicsNoColorShader.frag
GPU3D_Vulkan_GraphicsClearShader.frag
GPU3D_Vulkan_GraphicsFinalShader.vert
GPU3D_Vulkan_GraphicsEdgeShader.frag
GPU3D_Vulkan_GraphicsEdgeFogShader.frag
GPU3D_Vulkan_GraphicsFogShader.frag

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
```

### 18.2 compositor/presenter shader

```text
VulkanCompositorShader.comp
VulkanAccumulate3dShader.comp
VulkanSurfacePresenter.vert
VulkanSurfacePresenter.frag
```

### 18.3 variant生成

shader sourceの`#define`組み合わせをmanifestへ列挙し、SPIR-V headerを決定的に生成する。

variant軸:

```text
Z/W
opaque/translucent
texture none/modulate/decal/toon/highlight
shadow mask/shadow blend
fog
edge
antialiasing
fragment depth write方式
descriptor indexing方式
```

### 18.4 shader ABI

各shaderのdescriptor set、binding、push constant size、specialization constantをC++ headerへ集約する。

```cpp
namespace VulkanShaderAbi
{
    constexpr u32 SceneSet = 0;
    constexpr u32 TextureSet = 1;
    constexpr u32 FrameSet = 2;
}
```

phase番号をshader名、binding名、C++型名へ残さない。

---

## 19. 実装ステージ16: pipeline cacheとresource lifetimeを整理する

### 19.1 pipeline cache

cache keyへ次を含める。

```text
vendor ID
device ID
driver version
pipeline cache UUID
shader manifest version
renderer ABI version
graphics/compute mode
descriptor indexing mode
```

### 19.2 pipeline生成

すべてを起動時に一括生成せず、基本pipelineを先に生成し、variantは最初の使用時に生成してcacheする。

### 19.3 resource destroy順

```text
presenter lease停止
output ring retire
queue submission完了
pipeline破棄
descriptor pool破棄
framebuffer/render pass破棄
image view破棄
image/allocation破棄
command pool破棄
device release
```

### 19.4 device loss

device loss時はVulkan resourceを全破棄し、backend controllerへ失敗を返す。Vulkan object内部でSoftware 3Dへ切り替えない。

---

## 20. 実装ステージ17: 旧compatibility経路を削除する

GPU 3DとGPU-resident compositorの接続後、次を削除する。

### 20.1 core側

```text
VulkanRenderer : SoftRenderer
VulkanCompatibilityFrame
CompatibilityFrames
RebuildHighResolutionOutput
ResolveCompatibilityScale
FastNearestUpscale
NativeRasterSnapshotBuilder
NativeRasterFrame
NativeRasterTexture
NativeReferenceBgra
Native3DFrame
Native3DVisible
OnRendered3DLine
OnComposed3DOwnershipLine
CopyNative3DForPresenter
CopyNative3DOwnershipForPresenter
```

### 20.2 frontend側

```text
MelonPrimeVulkanNativeRaster.h/.cpp
NativeRasterGpu
NativeRasterViews
VulkanDirectFrameSnapshot::Screens
VulkanDirectFrameSnapshot::NativeRaster
QImageをVulkan 3D sourceとしてuploadする経路
CPU ownership maskを使うhybrid merge
```

### 20.3 phase/bootstrap群

実体rendererから呼ばれないcontract-only classを削除する。

```text
MelonPrimeVulkanClearBitmapBootstrap.*
MelonPrimeVulkanVertexUploadBootstrap.*
MelonPrimeVulkanPolygonBatchBootstrap.*
MelonPrimeVulkanOpaqueBootstrap.*
MelonPrimeVulkanTranslucentBootstrap.*
MelonPrimeVulkanShadowBootstrap.*
MelonPrimeVulkanToonHighlightBootstrap.*
MelonPrimeVulkanTextureSamplingBootstrap.*
MelonPrimeVulkanTexturedPolygonBootstrap.*
MelonPrimeVulkanTextureCacheBootstrap.*
MelonPrimeVulkanTextureDecodeBootstrap.*
MelonPrimeVulkanTextureUploadRingBootstrap.*
MelonPrimeVulkanPhase8CompletionBootstrap.*
MelonPrimeVulkanPhase9CompletionBootstrap.*
MelonPrimeVulkanPhase10CompletionBootstrap.*
MelonPrimeVulkanPhase11CompletionBootstrap.*
MelonPrimeVulkanPhase12CompletionBootstrap.*
MelonPrimeVulkanPhase13CompletionBootstrap.*
MelonPrimeVulkanClearPlaneBootstrap.*
MelonPrimeVulkanRasterBootstrap.*
```

再利用できる処理は`GPU3D_Vulkan*`、`GPU2D_Vulkan*`、`GPU_VulkanOutput*`の実体classへ移してから削除する。

### 20.4 shell contract

`VulkanRendererShellContract`を削除する。UI能力は実体の`VulkanCapabilities`から取得する。

```cpp
struct VulkanCapabilities
{
    bool GraphicsRaster;
    bool ComputeRaster;
    bool TimelineSemaphore;
    bool DescriptorIndexing;
    bool NonUniformIndexing;
    bool MoltenVK;
    int MaxScale;
};
```

---

## 21. CMake再編

### 21.1 core source

`MELONPRIME_VULKAN_ACTIVE`で次を追加する。

```text
VulkanDispatch.cpp
VulkanContext.cpp
GPU3D_AcceleratedFrontend.cpp
GPU3D_Vulkan.cpp
GPU3D_VulkanScene.cpp
GPU3D_VulkanPipelineState.cpp
GPU3D_TexcacheVulkan.cpp
GPU2D_Vulkan.cpp
GPU_Vulkan.cpp
GPU_VulkanOutput.cpp
GPU_VulkanOutputRing.cpp
```

### 21.2 frontend source

```text
MelonPrimeVulkanInstanceHost.cpp
MelonPrimeVulkanFeatureCheck.cpp
MelonPrimeScreenVulkan.cpp
MelonPrimeVulkanSurfacePresenter.cpp
```

bootstrap sourceはsource listから除去する。

### 21.3 dependencies

```text
volk
VulkanMemoryAllocator
Qt6::Gui
Qt6::Widgets
platform surface library
MoltenVK option
```

### 21.4 shader generation

現在のphase別manifestをreference構成へ再編する。

```text
src/Vulkan_shaders/3d/
src/Vulkan_shaders/2d/
src/Vulkan_shaders/presenter/
src/Vulkan_shaders/generated/
```

shader headerは`core`とfrontendの必要targetへだけ含める。

---

## 22. ファイル別実装マップ

| ファイル | 実装内容 |
|---|---|
| `src/VulkanContext.*` | instance、device、queue、feature、device profileの共有所有 |
| `src/VulkanDispatch.*` | Vulkan function dispatch |
| `src/GPU3D_Vulkan.*` | 実体`VulkanRenderer3D`、render context、render target、command submission |
| `src/GPU3D_VulkanScene.*` | `AcceleratedScene`からGPU bufferへの変換 |
| `src/GPU3D_VulkanPipelineState.*` | DS stateからpipeline variantへの変換 |
| `src/GPU3D_TexcacheVulkan.*` | GPU texture array、sampler、descriptor、VRAM dirty連携 |
| `src/GPU2D_Vulkan.*` | structured 2D metadataとGPU compositor入力 |
| `src/GPU_Vulkan.*` | outer renderer、frame lifecycle、settings、capture、final output |
| `src/GPU_VulkanOutput.*` | 2D/3D final composition |
| `src/GPU_VulkanOutputRing.*` | GPU image ring、timeline、lease、generation |
| `src/GPU.h/.cpp` | Vulkan output descriptorとlease接続 |
| `src/GPU3D.h/.cpp` | 必要最小限のRenderer3D共通hook |
| `src/frontend/qt_sdl/MelonPrimeRendererFactory.cpp` | 実体Vulkan renderer生成 |
| `src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp` | requested/actual backend管理 |
| `src/frontend/qt_sdl/MelonPrimeScreenVulkan.*` | renderer-owned imageのQt表示 |
| `src/frontend/qt_sdl/MelonPrimeVulkanInstanceHost.*` | QtとVulkanContextのinstance共有 |
| `src/frontend/qt_sdl/VideoSettingsDialog.cpp` | Vulkan能力に基づく設定反映 |
| `src/CMakeLists.txt` | core Vulkan sourceとshader生成 |
| `src/frontend/qt_sdl/CMakeLists.txt` | presenter sourceとplatform link |

---

## 23. 実装依存順

次の順序を崩さない。

```text
1. VulkanContext / dispatch
2. VulkanRenderer3D lifecycle
3. AcceleratedScene upload
4. render target / render context
5. clear plane / clear bitmap
6. texture cache
7. opaque raster
8. translucent raster
9. shadow raster
10. toon/highlight
11. edge/fog/AA final pass
12. display capture
13. structured 2D handoff
14. final compositor
15. output ring / lease
16. Qt presenter
17. settings / factory
18. compute raster
19. compatibility経路削除
20. bootstrap/phase contract削除
```

Qt presenter側の再ラスタライズを先に拡張しない。コア`VulkanRenderer3D`がGPU 3D imageを生成する構造を先に完成させ、そのimageを2D compositorとpresenterへ順番に接続する。

---

## 24. 最終的な通常フレーム経路

```text
GPU3D::RenderPolygonRAM
    ↓ BuildAcceleratedScene
VulkanRenderer3D
    ↓ GPU texture cache
    ↓ graphics/compute raster
    ↓ edge/fog/AA
VkImage 3DColor
    ↓
VulkanOutput
    + structured 2D buffers
    + capture history
    + screen swap / brightness
    ↓ GPU compute composition
VkImageArray FinalScreens[2]
    ↓ output ring publish
RendererOutput::VulkanImage
    ↓ RendererOutputLease
ScreenPanelVulkan
    ↓ presenter shader
QVulkanWindow swapchain
```

この経路では、Software 3D framebufferを作成しない。Vulkan 3D imageをCPU BGRAへ戻してから表示しない。内部解像度はVulkanRenderer3Dのrender target寸法へ直接反映される。3D polygon、texture、depth、stencil、shadow、toon、edge、fogはGPU commandとして処理される。

---

## 25. 実装時に禁止する代替方式

- Software 3Dを動かし、その完成画像だけVulkan textureへuploadする方式
- Software 3Dをownership referenceとして常時併走させる方式
- CPUでscale倍の3D画面をnearest/bilinear拡大する方式
- presenterがlive `GPU3D`、live VRAM、live registerを読む方式
- Qt `QImage`をVulkan rendererの正式な3D outputとする方式
- Vulkan選択中に内部でSoftware rendererへ黙って切り替える方式
- `NativeVulkan3DImplemented=false`のままUI上だけVulkanを有効化する方式
- phase contractのboolを実装完了の代わりに使う方式
- normal frameごとの`vkDeviceWaitIdle()`
- presenter参照中image slotの再利用
- scale設定をpresenter拡大だけで処理する方式
- graphics VulkanとVulkan Computeの片方だけSoftware 3Dへ残す方式

---

## 26. 最終コード状態

最終状態では次が成立する構造にする。

```text
Software renderer選択
    SoftRenderer + SoftRenderer3D

OpenGL選択
    GLRenderer + GLRenderer3D

OpenGL Compute選択
    GLRenderer + ComputeRenderer3D

Vulkan選択
    VulkanRenderer + VulkanRenderer3D graphics path

Vulkan Compute Shader選択
    VulkanRenderer + VulkanRenderer3D compute path

Metal選択
    MetalRenderer + Metal renderer path
```

Vulkanの外側rendererは`Renderer`を直接継承し、3D rendererは`Renderer3D`を直接継承する。Vulkan選択時に`SoftRenderer3D`を生成しない。GPU-resident final outputを`RendererOutputLease`でQt presenterへ渡し、present completionまでresource lifetimeを保持する。
