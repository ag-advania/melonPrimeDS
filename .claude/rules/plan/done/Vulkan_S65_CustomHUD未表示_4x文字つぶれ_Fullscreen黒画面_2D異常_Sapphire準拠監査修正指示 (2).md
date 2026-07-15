# Vulkan CustomHUD未表示・4x文字潰れ・Fullscreen黒画面・2D異常
## Sapphire準拠監査／修正指示書（S65）

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `f89820754f46d0672a6b95d2020a735c39ce3902`  
**前回監査HEAD:** `e9529a9644cdaf319c68847ea8b0dc1c680f029b`  
**前回監査後:** 8 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android` tag `0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib` commit `d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 対象症状

```text
1. 試合画面の3D映像は正常
2. CustomHUDが表示されない
3. 4xなど一部内部解像度でゲーム内文字が潰れる
4. fullscreen切替時に数秒黒画面
5. 2D画面の描画、透過、背景、layer合成がおかしい
```

ユーザー補足:

```text
4x文字潰れは今回のpushで発生した回帰ではなく、
以前から存在していた未報告の独立不具合。
```

したがって、4x文字潰れを今回追加されたS64変更の回帰とは扱わない。

---

# 1. 結論

| 項目 | 判定 | 根本原因／最有力原因 |
|---|---|---|
| CustomHUD未表示 | **同期不備をコード上で確認** | host staging write→transfer read barrierが不正。single staging bufferのin-flight上書きも可能 |
| 4x文字潰れ | **従来からのfilter責務混在** | desktopの`ScreenFilter`をSapphire compositor内部のpacked 2D補間にも直接適用 |
| fullscreen黒画面 | **構造上残存** | native identity変化時の全surface破棄、swapchain再作成時のwait、active resource先行撤去 |
| 2D異常 | **screen-owner binding不整合が最有力** | Sapphire rendererがphysical screen判定を行う時点で`GPU.Framebuffer` aliasが前frame／前状態の可能性 |
| Sapphire shader parity | **高い** | presenter shader／compositor shaderはexact upstream |
| Sapphire C++ parity | **部分的** | algorithmはvendor化したが、desktop adapter、screen ownership、surface lifecycleは独自 |
| GPU2D lifecycle parity | **未完了** | register forwardingは導入済みだが、現行melonDS stateとSapphire Unitの二重ownerが残る |

最優先修正:

```text
P0  HUDのhost→transfer barrierを正す
P0  HUD stagingをper-frame ring化
P0  Sapphire rendererを呼ぶ前にphysical screen aliasを確定
P0  bind用stateとcompleted-frame publicationを完全分離
P1  2D compositor内部filterと最終surface filterを分離
P1  fullscreenをactive／pending swapchain方式へ変更
P1  vendor生成物のCI検証を完成
```

---

# 2. 最新pushで改善した内容

## 2.1 ROM起動直後クラッシュ

前回指摘した:

```text
constructor中
→ current Renderer3D == nullptr
→ structured plane getter
→ null dereference
```

は修正されている。

現在は:

```text
GPU::SetRenderer:
    publication invalidate
    current renderer clear
    renderer生成
    framebuffer bindingのみ

GPU::FinishFrame:
    SwapBuffers
    RefreshSapphireVulkanBindings
    completed frame publication
```

へ分離されている。

この方向は正しい。

## 2.2 Sapphire register forwarding

現在はGPU2D register writeをSapphire Unitへforwardしている。

```text
Write8
Write16
Write32
Window Check
VBlank
VBlankEnd
```

が追加され、以前の「全register stateを毎scanline memcpy」から前進している。

`UnitSync`は現在:

```text
Enabled
MasterBrightness
Capture
Display FIFO
```

など外部stateの同期へ縮小している。

この方向も維持する。

## 2.3 Vendor parity checker

前回の到達不能分岐は修正されている。

現在は:

```text
exact_upstream
normalized_upstream
local_baseline
```

を明示的に処理し、workflowもSapphire frontend／coreをcheckoutして
`--verify-upstream`を実行している。

ただし後述のとおり、generator出力の検証がworkflowへ入っていない。

---

# 3. Sapphireと同じ実装か

## 3.1 exact upstream

現在、次はSapphireとexact一致として管理されている。

```text
VulkanSurfacePresenter.vert
VulkanSurfacePresenter.frag
VulkanCompositorShader.comp
VulkanAccumulate3dShader.comp
```

4x文字潰れを修正するために、これらshaderへ直接独自条件を追加してはいけない。

## 3.2 normalized upstream

次はdesktop adaptation込みのnormalized parity。

```text
FrameQueue.h／cpp
VulkanOutput.h／cpp
VulkanSurfacePresenter.h／cpp
GPU2D_Soft.h／cpp
```

ただし実際のruntime pathには、上記以外の独自層が存在する。

```text
MelonPrimeScreenVulkan
MelonPrimeVulkanOverlayRenderer
MelonPrimeVulkanSurfaceHost
MelonPrimeVulkanFrontendSession
GPU_Soft
SapphirePublished2DFrame
MelonPrimeSapphireGpu2DAdapter
UnitSync
```

今回の症状は主にこの独自層で発生している。

## 3.3 最終判定

```text
Sapphire shader／GPU2D algorithm:
    同一性は高い

framebuffer ownership／filter policy／HUD／fullscreen:
    Sapphireと同じではない
```

「Sapphire sourceを持ってきた」ことと、
「Sapphireのライフサイクル契約を再現した」ことは別である。

---

# 4. P0: CustomHUDが表示されない

対象:

```text
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.cpp
src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.h
src/VulkanR24Sync.h
```

---

# 5. HUD内容の生成自体は正しい

Vulkan pathは現在:

```text
MelonPrimeHud_PrepareTopOverlay
MelonPrimeHud_RenderTopOverlay
CustomHud_Render
```

を使用している。

つまり:

```text
HUD配置
HUD font
HP
ammo
score
rank
time
crosshair
```

などをVulkan専用に再実装しているわけではない。

この設計は維持する。

また`ScreenPanelVulkan`がbase constructorへ
`allocateCpuOverlayStorage=false`を渡しているため、
base constructor時には`Overlay[0]`が未確保だが、
`MelonPrimeHud_PrepareTopOverlay()`が初回にsurfaceサイズで遅延確保する。

したがって:

```text
Overlayがconstructorで未確保
```

だけをHUD未表示の根本原因として修正してはいけない。

---

# 6. Critical HUD-1
## host write→transfer read barrierが不正

現在のoverlay transfer:

```cpp
VulkanR24Barrier::HostWriteToShaderRead(
    commandBuffer,
    stagingBuffer,
    uploadBytes,
    VK_PIPELINE_STAGE_TRANSFER_BIT);
```

しかし`HostWriteToShaderRead()`の実装は:

```text
destination stageにshader stageが含まれる:
    VK_ACCESS_SHADER_READ_BIT

destination stageにVERTEX_INPUTが含まれる:
    VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT

destination stageがTRANSFERだけ:
    dstAccessMask = 0
```

になる。

直後に実行する処理は:

```cpp
vkCmdCopyBufferToImage(...)
```

であるため、必要なdestination accessは:

```cpp
VK_ACCESS_TRANSFER_READ_BIT
```

である。

現在のbarrierでは、mapped staging memoryへ行ったhost writeを
transfer operationへ正しくavailable／visibleにしていない。

結果:

```text
透明な旧データをcopy
stale dataをcopy
driver依存でたまたま表示
HUD textureが全面透明
```

になり得る。

これはCustomHUD未表示を説明できる明確な同期不備である。

---

# 7. 修正 HUD-1
## 専用barrierを追加する

`VulkanR24Sync.h`:

```cpp
inline void HostWriteToTransferRead(
    VkCommandBuffer commandBuffer,
    VkBuffer buffer,
    VkDeviceSize size)
{
    if (commandBuffer == VK_NULL_HANDLE
        || buffer == VK_NULL_HANDLE
        || size == 0)
    {
        return;
    }

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = size;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        1, &barrier,
        0, nullptr);
}
```

overlay:

```cpp
VulkanR24Barrier::HostWriteToTransferRead(
    commandBuffer,
    stagingBuffer,
    uploadBytes);
```

既存`HostWriteToShaderRead()`へTRANSFERの例外を追加するより、
目的別helperを分ける方が安全。

---

# 8. Critical HUD-2
## single staging bufferをGPU使用中に上書きできる

現在の順序:

```text
GUI presentOnGuiThread
    ↓
uploadRegion()
    ↓
single mapped staging bufferへCPU memcpy
    ↓
acquirePresentFrame()
    ↓
presenter内部で前frame fence／timelineを処理
    ↓
record／submit
```

問題は、CPUがstaging bufferへ書く時点で、
前frameのGPU transferが同じstaging bufferを使用中か確認していないこと。

前frameがまだ:

```text
vkCmdCopyBufferToImage(stagingBuffer → HUD texture)
```

を実行中なら、次frameのCPU memcpyと競合する。

症状:

```text
HUDが出ない
HUDが断続的に出る
HUDが一部欠ける
HUDが透明になる
fullscreen時に消える
```

---

# 9. 修正 HUD-2
## per-frame staging ring

推奨:

```cpp
struct OverlayUploadSlot
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize capacity = 0;

    VkSemaphore completionTimeline = VK_NULL_HANDLE;
    u64 completionValue = 0;
    bool pending = false;
};

std::array<OverlayUploadSlot, 3> uploadSlots;
u32 nextUploadSlot = 0;
```

slot選択:

```text
timeline完了済みslot
または
未使用slot
```

だけへCPU writeする。

正常presentation pathで無期限waitはしない。

利用可能slotがない場合:

```text
そのframeのHUD uploadをskip
最後に成功したHUD textureを表示継続
```

とする。

---

# 10. HUD upload failure時の契約

現在の`clearCompositeRequest()`は:

```cpp
compositeRequested = false;
hasValidUploadedOverlay = false;
```

を同時に行う。

次を分離する。

```cpp
void hideOverlay() noexcept
{
    compositeRequested = false;
}

void invalidateOverlayTexture() noexcept
{
    compositeRequested = false;
    hasValidUploadedOverlay = false;
}
```

用途:

```text
HUD設定OFF:
    hideOverlay

ROM停止:
    hideOverlay

texture破棄／device loss:
    invalidateOverlayTexture

一時upload失敗:
    何もしない
    最後のvalid textureを表示
```

---

# 11. HUD diagnostics

最低限:

```text
[VulkanHUD]
canRender
hudEnabled
editMode
imageWidth／imageHeight
dirtyRect
uploadBytes
stagingSlot
stagingCapacity
textureInitialized
uploadRecorded
transferBarrier
hasValidUploadedOverlay
compositeRequested
drawRecorded
```

HUD textureのalpha統計:

```text
nonZeroAlphaPixels
maxAlpha
nonZeroRgbPixels
```

判定:

```text
CPU QImage alpha > 0
staging data alpha > 0
HUD draw commandあり
画面に出ない

ならVulkan transfer／descriptor／blend側
```

---

# 12. 4xゲーム内文字潰れ
## 今回のpush回帰ではない

この問題は以前から存在する独立問題として扱う。

対象:

```text
Vulkan compositorのpacked 2D sampling
desktop ScreenFilterの適用範囲
最終surface sampling
```

---

# 13. 現在のfilter責務混在

Qt側:

```cpp
config.filtering =
    filter
        ? VulkanFilterMode::Linear
        : VulkanFilterMode::Nearest;
```

presenterは同じ値を:

```cpp
pushConstants.filtering =
    static_cast<u32>(surfaceState.config.filtering);
```

としてSapphire shaderへ渡す。

Sapphire compositor shaderは`Linear`の場合、
2D packed layer自体をDS native pixel間でbilinear補間する。

概略:

```glsl
Rgba6 nearest = readPacked(...);

if (filtering != Linear || scale <= 1)
    return nearest;

c00 = packed(x0, y0);
c10 = packed(x1, y0);
c01 = packed(x0, y1);
c11 = packed(x1, y1);

return bilinear(c00, c10, c01, c11);
```

細いDS bitmap fontでは:

```text
文字stroke
＋
透明／背景pixel
```

が補間され、線が薄くなる、欠ける、潰れる可能性がある。

---

# 14. Sapphire shaderは変更しない

現在のcompositor shaderはexact Sapphire source。

そのため修正場所はshader algorithmではなく、
desktop側がどのfilterをどのstageへ渡すかである。

現在は:

```text
ScreenFilter
    ↓
packed 2D内部補間
    ↓
3D sampling
    ↓
final atlas sampling
```

を一つのenumで制御している。

分離する。

```cpp
struct VulkanPresentationFiltering
{
    VulkanFilterMode compositor2D =
        VulkanFilterMode::Nearest;

    VulkanFilterMode finalSurface =
        VulkanFilterMode::Nearest;
};
```

---

# 15. 修正 4X-1
## packed 2DはNearestを標準にする

ゲーム内文字、UI、sprite、tile BGを含むDS 2Dは、
内部解像度が整数倍率ならNearestでpixel boundaryを維持する。

```cpp
inputs.filtering = VulkanFilterMode::Nearest;
```

最終window scalingに対してのみ:

```cpp
surfaceConfig.filtering =
    userScreenFilter
        ? VulkanFilterMode::Linear
        : VulkanFilterMode::Nearest;
```

を使用する。

---

# 16. direct present時の注意

direct present pathでは、同一presenter shader内で:

```text
packed 2D再構成
3D sample
surface出力
```

を行うため、内部filterと最終filterを完全分離できない。

車輪の再発明を避ける推奨方法:

```text
Linear final filterが必要
かつ
structured packed 2Dを含む
かつ
scale > 1

→ direct presentを選ばない
→ exact Sapphire compositorでNearestの高解像度atlasを生成
→ Sapphire presenterのsamplerでatlasをLinear表示
```

つまり:

```cpp
const bool requiresSeparated2dFiltering =
    inputs.scale > 1
    && surfaceState.config.filtering == VulkanFilterMode::Linear
    && inputs.topPackedBuffer != VK_NULL_HANDLE
    && inputs.bottomPackedBuffer != VK_NULL_HANDLE;

directPresentRequested &=
    !requiresSeparated2dFiltering;
```

compositor入力:

```cpp
compositionInputs.filtering =
    VulkanFilterMode::Nearest;
```

presenter sampler:

```cpp
surfaceState.config.filtering =
    user requested filter;
```

---

# 17. 4x検証

最低限:

```text
1x Nearest
2x Nearest
3x Nearest
4x Nearest
4x Linear
```

同じゲーム内文字を比較。

判定:

```text
4x Nearestでも潰れる:
    filterだけの問題ではない
    structured plane／control／screen ownerを調査

4x Nearestは正常、Linearだけ潰れる:
    filter責務混在で確定
```

比較対象:

```text
Software
OpenGL Classic
OpenGL Compute
Vulkan
Sapphire Android
```

---

# 18. P0: 2D rendering異常

対象:

```text
src/GPU_Soft.cpp
src/GPU2D.cpp
src/MelonPrimeSapphireGpu2DAdapter.cpp
src/SapphireGPU2DCore/UnitSync.cpp
src/SapphireGPU2DCore/GPU2D_Soft.cpp
src/SapphirePublished2DFrame.h
src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp
```

---

# 19. Sapphire rendererのphysical screen判定

Sapphire `GPU2D::SoftRenderer`は、
現在描画中Unitの出力先を次で判定する。

```cpp
const u32* currentFramebuffer =
    Framebuffer[CurUnit->Num];

for (int buffer = 0; buffer < 2; ++buffer)
{
    if (currentFramebuffer == GPU.Framebuffer[buffer][0])
        return true;

    if (currentFramebuffer == GPU.Framebuffer[buffer][1])
        return false;
}
```

つまりSapphire rendererを呼ぶ前に:

```text
Renderer2D::Framebuffer[unit]
GPU.Framebuffer[buffer][physical screen]
```

のalias関係が現在の`GPU.ScreenSwap`と一致していなければならない。

---

# 20. 現在の呼出し順

現在の`SoftRenderer::DrawScanline()`:

```text
1. SyncSapphireUnitsFromGPU2D
2. Sapphire2DRenderer->SetFramebuffer(unitA, unitB)
3. Sapphire Aを描画
4. Sapphire Bを描画
5. SyncSapphireFramebufferBindings
```

physical screen aliasを更新するのが、
Sapphire rendererの描画後である。

したがってSapphireの`CurrentUnitTargetsTopScreen()`は描画中に:

```text
前scanline
前frame
reset直後
screen-swap変更前
```

の`GPU.Framebuffer` mappingを見る可能性がある。

結果:

```text
raw packedはphysical top
structured controlはphysical bottom

または

raw packedはphysical bottom
structured planeはphysical top
```

となる。

これは次の症状を説明できる。

```text
不透明黒のProtectedBlack flagが反対画面へ入る
本来透明でない黒が透過
別画面のBG planeが全面表示
window maskとpixel dataの画面ownerが不一致
メニューと試合で2Dの見え方が変わる
```

---

# 21. 修正 2D-1
## bindingとpublicationを完全分離する

現在の`SyncSapphireFramebufferBindings()`は:

```text
GPU.FrontBuffer更新
physical top／bottom alias更新
```

を同時に行う。

さらに各scanline後にも呼ばれる。

次の2関数へ分ける。

```cpp
void SoftRenderer::BindSapphireBackBufferScreenAliases() noexcept;
void SoftRenderer::PublishCompletedSapphireFrontBuffer() noexcept;
```

---

## 21.1 描画前binding

```cpp
void SoftRenderer::BindSapphireBackBufferScreenAliases() noexcept
{
    u32* const unitA = Framebuffer[BackBuffer][0];
    u32* const unitB = Framebuffer[BackBuffer][1];

    if (GPU.ScreenSwap)
    {
        GPU.Framebuffer[BackBuffer][0] = unitA;
        GPU.Framebuffer[BackBuffer][1] = unitB;
    }
    else
    {
        GPU.Framebuffer[BackBuffer][0] = unitB;
        GPU.Framebuffer[BackBuffer][1] = unitA;
    }

    Sapphire2DRenderer->SetFramebuffer(unitA, unitB);
}
```

`GPU.FrontBuffer`は変更しない。

---

## 21.2 DrawScanline

```cpp
void SoftRenderer::DrawScanline(u32 line)
{
    SyncSapphireUnitsFromGPU2D();

    BindSapphireBackBufferScreenAliases();

    line = GPU.VCount;
    if (line < 192)
    {
        Sapphire2DRenderer->DrawScanline(
            line, &SapphireUnitA);

        Sapphire2DRenderer->DrawScanline(
            line, &SapphireUnitB);
    }
}
```

描画後の`SyncSapphireFramebufferBindings()`は削除する。

---

## 21.3 DrawSprites

sprite rendererも同じphysical alias契約を使用するため、
描画前にbindingする。

```cpp
void SoftRenderer::DrawSprites(u32 line)
{
    SyncSapphireUnitsFromGPU2D();
    BindSapphireBackBufferScreenAliases();

    Sapphire2DRenderer->DrawSprites(
        line, &SapphireUnitA);

    Sapphire2DRenderer->DrawSprites(
        line, &SapphireUnitB);
}
```

---

## 21.4 completed frame publication

`SwapBuffers()`後だけ:

```cpp
void SoftRenderer::PublishCompletedSapphireFrontBuffer() noexcept
{
    GPU.FrontBuffer = BackBuffer ^ 1;

    for (int buffer = 0; buffer < 2; ++buffer)
    {
        u32* const unitA = Framebuffer[buffer][0];
        u32* const unitB = Framebuffer[buffer][1];

        if (GPU.ScreenSwap)
        {
            GPU.Framebuffer[buffer][0] = unitA;
            GPU.Framebuffer[buffer][1] = unitB;
        }
        else
        {
            GPU.Framebuffer[buffer][0] = unitB;
            GPU.Framebuffer[buffer][1] = unitA;
        }
    }

    (void)PublishSapphire2DFrame();
}
```

---

# 22. 修正 2D-2
## physical ownerをpointer推測から検証する

Sapphire algorithm本体は変更しない。

debug buildで、Sapphireが判定したscreen ownerと
MelonPrime adapterの期待値を比較するhookを追加する。

```cpp
struct Sapphire2DScreenOwnerTrace
{
    u64 frameSerial;
    u32 line;
    u32 unit;

    bool gpuScreenSwap;
    bool expectedTop;
    bool sapphireTop;

    const u32* unitFramebuffer;
    const u32* gpuPhysicalTop;
    const u32* gpuPhysicalBottom;
};
```

期待:

```cpp
expectedTop =
    unit == 0
        ? GPU.ScreenSwap
        : !GPU.ScreenSwap;
```

不一致時:

```text
frame
line
unit
BackBuffer
FrontBuffer
ScreenSwap
pointer
```

を1回だけ出力する。

---

# 23. 修正 2D-3
## register state ownerを一本化する

現在は:

```text
現行melonDS GPU2D
Sapphire Unit
```

の両方がregister stateを持つ。

register write forwardは前進だが、
次の二重更新が残る。

```text
GPU2D::Write*
    → Sapphire Unit::Write*
    → 現行GPU2D state更新

DrawScanline
    → SyncUnitFromGPU2D
```

`SyncUnitFromGPU2D`は現在かなり縮小されているため、
以前より安全だが、最終形では次を明確にする。

Sapphire Unitがowner:

```text
DispCnt
BGCnt
BG position
affine refs
window
mosaic
blend
```

adapter同期対象:

```text
Enabled
MasterBrightness
CaptureCnt／CaptureLatch
DispFIFO
MelonPrime外部state
```

register fieldをscanlineごとに再上書きしない。

---

# 24. register forwardingの順序

現行`GPU2D::Write*()`はSapphireへのforwardを
native GPU2D state更新より先に行う。

```cpp
ForwardRegisterWrite(...);

switch (...)
{
    native state update;
}
```

この順序では:

```text
native側mask
Enabledによるwrite ignore
invalid bit clear
```

がSapphire側と完全に同じである必要がある。

Sapphire Unit sourceが同じmask semanticsを持つなら問題ないが、
version差がある場合はdivergenceする。

推奨test:

```text
各GPU2D registerへ:
    8bit write
    16bit write
    32bit write
    disabled engine中write
    midscanline write

native state
vs
Sapphire Unit state
```

を比較する。

差がある場合、手書き補正を追加するのではなく、
Sapphireのregister interfaceをstate ownerにする。

---

# 25. 2D frame publicationの検証

`SapphirePublished2DFrame`はpixel copyではなくpointer view。

したがってconsumer側は:

```text
frame serial
publication generation
renderer generation
valid
```

を必ず確認し、次frameのback buffer書込み前にsnapshot copyを終える。

assert:

```cpp
assert(published.valid);
assert(published.frameSerial == expectedFrameSerial);
assert(published.rendererGeneration == expectedRendererGeneration);

assert(published.top.packed != nullptr);
assert(published.bottom.packed != nullptr);

assert(published.top.structuredControl != nullptr);
assert(published.bottom.structuredControl != nullptr);
```

rawとstructured owner:

```cpp
assert(published.top.engine
    == expectedTopEngine);

assert(published.bottom.engine
    == expectedBottomEngine);
```

---

# 26. 2D diagnostics

最初の120frameだけ:

```text
[Sapphire2DOwner]
frameSerial
line
unit
ScreenSwap
BackBuffer
FrontBuffer
expectedTop
sapphireTop
unitFramebuffer
physicalTopPointer
physicalBottomPointer
```

frame publication:

```text
[Sapphire2DPublish]
frameSerial
publicationGeneration
frontBuffer
topEngine
bottomEngine
topPackedChecksum
bottomPackedChecksum
topControlChecksum
bottomControlChecksum
protectedBlackTop
protectedBlackBottom
```

2D異常画面で:

```text
Software
Vulkan
```

を比較する。

---

# 27. P1: fullscreen切替時の黒画面

現在、同一native identityなら:

```text
surface維持
resizeSurface
swapchainDirty
```

となる。

これは正しい。

しかし一時的にidentityが変わると:

```text
session.unregisterPresenter
presenter.detachSurface
surfaceHost.destroy
新VkSurfaceKHR
attachSurface
beginSurfaceGeneration
registerPresenter
```

を実行する。

fullscreen時のQt window transition中にこの経路へ入れば、
旧表示を即座に失う。

---

# 28. swapchain recreationの残存問題

現在:

```text
旧swapchainあり
→ 最大50ms wait
→ vkCreateSwapchainKHR(oldSwapchain)
```

までは改善している。

ただし旧resourceを:

```text
RetireReady
```

でretireしている。

`RetireReady`はtimeline／fenceを持たないため、
次回collectで即破棄対象になる。

また:

```text
framebuffers
imageViews
pipeline
renderPass
```

をactive stateから先にmoveし、
新resource構築前に旧active stateを空にしている。

これは真正なactive／pending切替ではない。

---

# 29. 修正 FULL-1
## SwapchainBundle

```cpp
struct SwapchainBundle
{
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<bool> imageInitialized;

    u64 lastSubmissionTimelineValue = 0;
};

struct SurfaceState
{
    SwapchainBundle active;
    std::optional<SwapchainBundle> pending;
};
```

処理:

```text
1. activeを維持
2. pendingへnew swapchainとresourceを構築
3. pending全resource成功
4. pendingをactiveへswap
5. old activeをlastSubmissionTimelineValueでretire
```

途中失敗:

```text
pendingだけ破棄
active表示を継続
```

---

# 30. 修正 FULL-2
## RetireReadyを使わない

旧swapchain／framebuffer／image view／pipeline／render passは:

```cpp
retiredResources.Retire(
    timelineSemaphore,
    oldBundle.lastSubmissionTimelineValue,
    VK_NULL_HANDLE,
    destroyOldBundle);
```

とする。

timelineが使えない場合は、
old bundle専用fenceでretireする。

---

# 31. 修正 FULL-3
## fullscreen event debounce

扱うevent:

```text
QEvent::WindowStateChange
QEvent::Resize
QEvent::DevicePixelRatioChange
QPlatformSurfaceEvent::SurfaceCreated
QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed
```

`WindowStateChange`後:

```text
0ms～1 event-loop turn待つ
native identity再取得
geometry再取得
```

同一identityならsurface再生成禁止。

```text
resizeSurfaceのみ
```

とする。

---

# 32. 修正 FULL-4
## normal pathの無期限wait禁止

現在、high-resolution fallback presentでは:

```cpp
inputs.scale > 1
    ? UINT64_MAX
    : timeoutNs;
```

としてcompositor完了を無期限待ちする経路がある。

fullscreen transition時のdriver stallと組み合わさると、
数秒停止を増幅できる。

正常presentation pathでは:

```text
16～50ms bounded wait
timeout時は前frame再利用
```

とする。

無期限waitを許可するのは:

```text
shutdown
device teardown
explicit blocking screenshot
```

だけ。

---

# 33. Vendor parityの残り

workflowは現在:

```text
Sapphire checkout
unit test
upstream snapshot verify
normalized parity check
```

を実行する。

改善している。

しかし:

```bash
python3 tools/vendor_sapphire.py --verify-generated
```

を実行していない。

repositoryには:

```text
committed upstream snapshot
generator
committed adapted local file
```

の3つがある。

最終的に確認すべきもの:

```text
pinned upstream
→ deterministic generator
→ committed local source
```

である。

workflowへ追加:

```yaml
- name: Verify generated Sapphire sources
  run: |
    python3 tools/vendor_sapphire.py --verify-generated
```

---

# 34. generatorの改善

現在のgeneratorは文字列置換を全fileへ共通適用する。

```text
namespace置換
include置換
Vulkan dispatch置換
```

をfile種別ごとのtransformへ分ける。

```python
TRANSFORMS = {
    "gpu2d_core": transform_gpu2d_core,
    "vulkan_frontend": transform_vulkan_frontend,
    "shader_exact": transform_exact,
}
```

manifest:

```json
"transform_id": "gpu2d_core"
```

許可transformだけを決定的に実行する。

---

# 35. 推奨commit分割

## S65-1

```text
Fix Vulkan HUD host-to-transfer synchronization
```

変更:

```text
HostWriteToTransferRead
正しいdstAccessMask
HUD transfer diagnostic
```

## S65-2

```text
Make Vulkan HUD uploads frame-safe
```

変更:

```text
3-slot staging ring
timeline ownership
last valid overlay保持
hide／invalidate分離
```

## S65-3

```text
Bind Sapphire physical screen aliases before 2D rendering
```

変更:

```text
BindSapphireBackBufferScreenAliases
DrawScanline前
DrawSprites前
```

## S65-4

```text
Separate Sapphire 2D binding from completed-frame publication
```

変更:

```text
GPU.FrontBuffer更新をFinishFrameへ限定
scanline後sync削除
publication assert
```

## S65-5

```text
Separate Vulkan compositor filtering from surface filtering
```

変更:

```text
packed 2D Nearest
final surface user filter
Linear時のfallback composition
```

## S65-6

```text
Make Vulkan fullscreen swapchain transitions non-destructive
```

変更:

```text
active／pending SwapchainBundle
timeline retire
bounded wait
Qt debounce
```

## S65-7

```text
Verify generated Sapphire sources in CI
```

変更:

```text
--verify-generated
file-specific deterministic transforms
```

---

# 36. 検証マトリクス

## 36.1 CustomHUD

```text
windowed
fullscreen
DPI 100％
DPI 125％
DPI 150％
DPI 200％
HUD ON／OFF
edit mode
font変更
scale変更
crosshair
HP
ammo
score
rank
time
radar
```

完了条件:

```text
CPU HUD imageにalphaあり
transfer barrier正しい
GPU textureにalphaあり
overlay drawあり
毎frame安定表示
fullscreen後も表示
```

## 36.2 4x文字

```text
1x Nearest
2x Nearest
3x Nearest
4x Nearest
4x Linear
```

比較:

```text
Software
OpenGL
Vulkan
Sapphire
```

完了条件:

```text
4x NearestでDS bitmap fontのpixel shape維持
Linear設定でも内部2D pixel shapeを壊さない
```

## 36.3 fullscreen

```text
windowed→fullscreen
fullscreen→windowed
Alt+Enter連打
maximize
restore
別monitor
DPI違いmonitor
```

計測:

```text
native identity
surface generation
swapchain active generation
pending creation時間
first present時間
wait時間
```

完了条件:

```text
数秒黒画面0
通常resizeでsurface再作成0
無期限wait 0
旧frameをnew swapchain完成まで維持
```

## 36.4 2D

```text
boot menu
title menu
pause menu
match
match transition
fade
window mask
transparent OBJ
bitmap OBJ
affine BG
mosaic
display capture
VRAM display
screen swap
```

完了条件:

```text
raw packed ownerとstructured owner一致
ProtectedBlack flagが正しい画面へ付く
背景がwindow外へ出ない
不透明黒が透過しない
```

---

# 37. 禁止事項

```text
HUDをbitmap fontで再実装
HUDをSapphire presenter shaderへ再統合
staging bufferを巨大固定値へ変更するだけ
HUD upload失敗時にtextureを無効化
RGB==0を無条件opaque化
黒透過をcompositor shaderの例外で隠す
4xだけglyph補正
ROM別text repair
Sapphire exact shaderを直接変更
screenSwap boolを無条件反転
DrawScanline後だけphysical aliasを更新
fullscreen時にpresenter登録解除
同一native surfaceでVkSurfaceKHR破棄
旧swapchain resourceをRetireReadyで即破棄
normal presentでUINT64_MAX wait
manifest hash更新だけでCIを通す
```

---

# 38. 完了条件

```text
CustomHUDが全解像度で表示
HUD transferにVK_ACCESS_TRANSFER_READ_BIT
staging bufferのin-flight上書き0

4x Nearestでゲーム内文字が潰れない
packed 2D filterとfinal surface filterが分離
Sapphire shader exact parity維持

Sapphire renderer呼出し前にphysical screen alias確定
DrawScanline／DrawSpritesでowner不一致0
raw／structured／controlのframe・screen一致

fullscreenのsurface全破棄0
active swapchainをpending完成まで維持
数秒黒画面0

vendor generator出力をCIで検証
Software／OpenGL回帰0
試合中3Dの現状品質維持
```

---

# 39. 最終判断

現在の試合中3D映像が正常であるため、
Vulkan 3D rendererへ追加修正を入れる必要はない。

今回の不具合は:

```text
CustomHUD:
    desktop overlay同期

4x文字:
    desktop filter policy

fullscreen:
    desktop surface lifecycle

2D:
    Sapphire rendererを呼ぶ前のphysical screen contract
```

に分かれる。

Sapphireからそのまま持ってくる対象:

```text
GPU2D algorithm
compositor shader
presenter shader
FrameQueue
VulkanOutput
presenter core
```

MelonPrimeで実装する対象:

```text
Qt surface lifecycle
HUD texture upload
filter policy adapter
physical screen alias publication
```

この境界を維持し、
Sapphire algorithmへ個別症状の例外を追加しないこと。

最優先は:

```text
1. HUD barrier＋staging lifetime
2. 2D physical aliasを描画前に確定
3. 4x filter責務分離
4. active／pending fullscreen swapchain
```

の順である。
