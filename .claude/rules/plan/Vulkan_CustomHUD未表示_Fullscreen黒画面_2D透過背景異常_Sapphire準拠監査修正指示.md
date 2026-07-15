# Vulkan CustomHUD未表示・Fullscreen黒画面・2D透過／背景異常
## Sapphire準拠監査・修正指示書（S63）

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `ee2ce6ccef8f4c9ae439c4ed1fd8bca8b18df4c7`  
**前回監査HEAD:** `03ee5800bd80f56ac521f33e8adee5716565b052`  
**前回監査後の進行:** 9 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android` tag `0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib` commit `d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 監査対象の症状

```text
1. 試合画面の3D映像は問題ない
2. CustomHUDが表示されない
3. fullscreen切替時に数秒黒画面
4. 2Dの黒色部分が、本来不透明なのに透過する
5. 2D背景が画面全面へ出てくる場合がある
```

今回の症状はVulkan 3D rasterizer本体ではなく、次の境界に集中している。

```text
CustomHud_Render
    ↓
MelonPrimeVulkanOverlayRenderer
    ↓
Sapphire VulkanSurfacePresenter
    ↓
swapchain／surface lifecycle

Sapphire GPU2D renderer
    ↓
MelonPrime GPU_Soft adapter
    ↓
SapphireVulkanFrameLatch
    ↓
VulkanOutput compositor
```

---

# 1. 結論

| 項目 | 判定 | 根本原因 |
|---|---|---|
| CustomHUD未表示 | **コード上確定** | overlay upload失敗時に表示要求を消している。固定256×256 staging、dirty rect offset欠落、毎回`UNDEFINED`遷移、static callbackも問題 |
| fullscreen黒画面 | **コード上確定** | surface／swapchain更新時に無期限idle wait。新swapchain完成前に旧描画resourceをactive stateから外す |
| 2D黒透過 | **入力契約不一致が最有力** | raw packed top/bottomとstructured top/bottomを別経路で決定し、frame latchが同一画面としてマージしている |
| 背景全面表示 | **GPU2D state adapter不完全が最有力** | 現行melonDS GPU2D stateを古いSapphire Unitへ毎scanline部分コピー。window／layer／mosaic／affine lifecycleがdependency closureではない |
| Sapphire同一性 | **未達** | presenter shaderのみexact。主要C++とGPU2Dは`local_baseline`で、CIは上流一致を確認していない |
| 3D | **維持対象** | 現在ほぼ正常。3D rasterizerへ場当たり修正を追加しない |

修正優先順位:

```text
P0  CustomHUD upload contractを修正
P0  Sapphire GPU2D dependency closureとphysical screen publicationを再構築
P0  raw packedとstructured planeを同一publication objectからlatch
P1  fullscreen swapchain更新を非破壊・非blocking化
P1  vendor parity CIを本当のupstream比較へ変更
P2  2D pixel ownership／protected-blackの自動検証を追加
```

---

# 2. 前回修正で改善した点

前回監査後、次の方向は正しい。

## 2.1 CustomHUDの正規renderer再利用

現在のVulkan pathは、旧bitmap fontによるHUD再実装を廃止し、

```cpp
MelonPrimeHud_RenderTopOverlay(...)
    → MelonPrime::CustomHud_Render(...)
```

を使っている。

これは維持する。

対象:

```text
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/MelonPrimeHudScreenCppHelpers.inc
```

## 2.2 Presenter shader

以下はSapphire frontendのhashと一致している。

```text
VulkanSurfacePresenter.vert
VulkanSurfacePresenter.frag
```

HUD／radar固有draw modeも削除されている。

## 2.3 `oldSwapchain`

swapchain作成時に:

```cpp
swapchainInfo.oldSwapchain = oldSwapchain;
```

を渡すようになった。

ただしresource lifecycle全体は依然blockingであり、
これだけではfullscreen黒画面は解消しない。

## 2.4 runtime pacing

Sapphireのqueue policyに寄せたruntime pacing bridgeが追加された。

今回の主要問題ではないため、既存実装を維持する。

---

# 3. P0: CustomHUDが表示されない直接原因

対象:

```text
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.cpp
src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.h
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp
```

---

## 3.1 正規HUD画像は生成されている

GUI thread側で:

```cpp
MelonPrimeHud_PrepareTopOverlay(...)
MelonPrimeHud_RenderTopOverlay(...)
```

を呼んでおり、`QImage::Format_ARGB32_Premultiplied`の
透明HUD imageを生成している。

したがって、HUD内容をもう一度別rendererで作り直してはいけない。

問題はその後のVulkan upload／compositeである。

---

## 3.2 固定staging容量が小さすぎる

現在:

```cpp
stagingCapacity = 256u * 256u * 4u;
```

で固定されている。

容量は:

```text
262,144 bytes
```

しかない。

upload側は:

```cpp
uploadBytes = rect.width() * rect.height() * 4;

if (uploadBytes > stagingCapacity)
    return false;
```

である。

例:

```text
1280×720 full dirty = 3,686,400 bytes
1920×1080 full dirty = 8,294,400 bytes
2560×1440 full dirty = 14,745,600 bytes
```

したがって初回表示、fullscreen、HUD構成変更などでdirty rectが広いと、
ほぼ確実にupload失敗する。

さらにcallerは:

```cpp
if (overlayRenderer.uploadRegion(...))
    setCompositeRect(...);
else
    clearCompositeRequest();
```

としているため、upload失敗した瞬間にHUD draw自体を無効化する。

これがCustomHUD未表示の直接原因である。

---

## 3.3 dirty rectのcopy destinationが常に左上

現在の`VkBufferImageCopy`は:

```cpp
region.imageExtent = {
    rect.width(),
    rect.height(),
    1
};
```

しか設定していない。

必要な:

```cpp
region.imageOffset = {
    rect.x(),
    rect.y(),
    0
};
```

がない。

そのため部分更新データをtextureの`(0, 0)`へ書き込む。

結果:

```text
HUDが本来の場所へ出ない
以前のHUD位置が更新されない
左上へ断片が現れる
dirty領域とtexture領域が一致しない
```

となる。

---

## 3.4 毎回`oldLayout = UNDEFINED`

現在のupload barrier:

```cpp
oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
```

を毎回使用している。

`UNDEFINED`からの遷移は旧texture内容を保持しない。

dirty rect方式では:

```text
前frameのHUD textureを保持
前dirtyをtransparentで消す
新dirtyを上書き
```

する必要がある。

毎回`UNDEFINED`を使うと、更新していない領域の内容は未定義になる。

正しい遷移:

```text
初回:
UNDEFINED
→ TRANSFER_DST_OPTIMAL
→ SHADER_READ_ONLY_OPTIMAL

2回目以降:
SHADER_READ_ONLY_OPTIMAL
→ TRANSFER_DST_OPTIMAL
→ SHADER_READ_ONLY_OPTIMAL
```

textureごとに現在layoutを追跡すること。

---

## 3.5 static global callback

現在:

```cpp
VulkanSurfacePresenter::desktopOverlayRecorder
VulkanSurfacePresenter::desktopOverlayUserData
```

が`static`である。

つまりcallbackはpresenter instanceごとではなく、process global。

さらに`ScreenPanelVulkan` destructorは:

```cpp
presenter->SetDesktopOverlayRecorder(nullptr, nullptr);
```

を呼ぶ。

そのため:

```text
旧panel破棄
backend transition
複数window
一時presenter
```

のいずれかで、別の生存中presenterのHUD callbackまで消える。

これはCustomHUDが突然完全に出なくなる直接原因になり得る。

修正:

```cpp
class VulkanSurfacePresenter
{
    VulkanDesktopOverlayRecorderFn desktopOverlayRecorder = nullptr;
    void* desktopOverlayUserData = nullptr;
};
```

またはsurface stateごとに保持する。

global staticは禁止。

---

## 3.6 GUI threadで無期限wait

HUD uploadは毎回:

```cpp
vkQueueSubmit(...)
vkWaitForFences(..., UINT64_MAX)
```

を実行する。

これはGUI threadを停止する。

fullscreen時には:

```text
swapchain idle wait
surface configure wait
HUD transfer fence wait
```

が重なり得る。

さらにshared graphics queueへsubmitしているが、
`VulkanContext::Get().GetQueueLock()`による外部同期も見当たらない。

Vulkan queue operationは外部同期が必要である。

---

# 4. CustomHUD修正指示

## 4.1 最小修正

まず表示を復旧する最小修正:

```cpp
bool ensureStagingCapacity(VkDeviceSize required)
{
    if (required <= stagingCapacity)
        return true;

    waitForLastOverlayTransfer();
    destroyStagingBuffer();

    stagingCapacity = RoundUpToPowerOfTwo(required);
    return createMappedStagingBuffer(stagingCapacity);
}
```

upload前:

```cpp
const VkDeviceSize required =
    VkDeviceSize(rect.width())
    * VkDeviceSize(rect.height())
    * 4;

if (!ensureStagingCapacity(required))
    return false;
```

copy:

```cpp
VkBufferImageCopy region{};
region.bufferOffset = 0;
region.bufferRowLength = 0;
region.bufferImageHeight = 0;
region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
region.imageSubresource.layerCount = 1;
region.imageOffset = {rect.x(), rect.y(), 0};
region.imageExtent = {
    static_cast<u32>(rect.width()),
    static_cast<u32>(rect.height()),
    1
};
```

layout追跡:

```cpp
VkImageLayout textureLayout = VK_IMAGE_LAYOUT_UNDEFINED;
bool textureInitialized = false;
```

barrier:

```cpp
const VkImageLayout oldLayout =
    textureInitialized
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
```

upload成功後:

```cpp
textureInitialized = true;
textureLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
```

---

## 4.2 upload失敗時に最後のHUDを消さない

現在の:

```cpp
else
    overlayRenderer.clearCompositeRequest();
```

をやめる。

原則:

```text
新upload成功:
  新textureを表示

新upload失敗:
  最後に成功したtextureを表示継続
  error counter／rate-limited log
```

HUD無効化またはROM終了時だけ:

```cpp
clearCompositeRequest();
```

を行う。

必要な状態:

```cpp
bool hasValidUploadedOverlay = false;
u64 lastUploadedHudGeneration = 0;
```

---

## 4.3 推奨最終形: presenter submitへ統合

毎HUD frameごとに独立queue submit＋waitを行わない。

推奨:

```text
GUI thread:
  QImage生成
  mapped staging ringへmemcpy
  upload requestをpublish

presenter command recording:
  transfer barrier
  vkCmdCopyBufferToImage
  shader-read barrier
  game frame draw
  HUD draw
  single queue submit
```

これにより:

```text
別queue submit不要
無期限fence wait不要
queue lock競合不要
fullscreen時の追加stall減少
```

となる。

Sapphire presenter coreへHUD algorithmを追加してはいけない。

追加するのは汎用desktop hookのみ。

例:

```cpp
struct VulkanDesktopOverlayHooks
{
    bool (*recordPreRenderPass)(
        VkCommandBuffer,
        const VulkanDesktopOverlayTarget&,
        void*);

    bool (*recordInsideRenderPass)(
        VkCommandBuffer,
        const VulkanDesktopOverlayTarget&,
        void*);

    void* userData;
};
```

hookはpresenter instance memberとする。

---

## 4.4 full image uploadを先に採用してもよい

dirty uploadを正しく実装するまで、
まずfull HUD image uploadへ戻す方が安全。

```text
正しいfull upload
→表示確認
→dirty rect optimization
```

の順とする。

誤ったdirty uploadを維持するより、
full image uploadの方が正確である。

ただし毎frame同期waitは入れず、ring／timeline化する。

---

# 5. P1: fullscreen切替時の数秒黒画面

対象:

```text
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHost.cpp
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp
```

---

## 5.1 native identityが同じ場合

現在は次が追加されている。

```cpp
if (surfaceId != 0 && surfaceHost.matchesNativeIdentity(*this))
{
    surfaceHost.rebindWidget(*this);
    return true;
}
```

これは正しい。

同じHWND／X11 Window／Wayland surfaceなら:

```text
VkSurfaceKHR維持
presenter registration維持
surface generation維持
FrameQueue維持
```

とする。

---

## 5.2 native identity変更時は依然破壊的

identity不一致時:

```text
session.unregisterPresenter
presenter.detachSurface
surfaceHost.destroy
new VkSurfaceKHR
attachSurface
beginSurfaceGeneration
configureSurface
registerPresenter
```

を行う。

fullscreen transition中にQtが一時的にnative identityを変更すると、
この完全切替へ入る。

旧surfaceを先に破棄しているため、
新surfaceが最初にpresentするまで黒画面になる。

---

## 5.3 `configureSurface()`のidle wait

`configureSurface()`はconfigが変わると:

```cpp
waitForSurfaceIdle(surfaceState)
```

を実行する。

fullscreenでは:

```text
サイズ変更
DPR変更
screen transform変更
```

が連続する。

各変更でidle waitへ入る可能性がある。

transform／filter変更だけでsurface全体をidleにする必要はない。

---

## 5.4 `ensureSwapchain()`のblocking wait

swapchain dirty時:

```cpp
waitForSurfaceIdle(surfaceState)
```

をtimeout指定なしで呼ぶ。

その後、新swapchain作成前に:

```text
surfaceState.framebuffersをmove
surfaceState.swapchainImageViewsをmove
pipelineをactive stateから外す
renderPassをactive stateから外す
swapchain handleをNULL
extentをclear
descriptor cacheをclear
draw call cacheをclear
```

している。

`oldSwapchain` handleを`vkCreateSwapchainKHR`へ渡していても、
旧swapchainのrender resourceは既にactive stateから外れている。

そのため非破壊切替ではない。

---

## 5.5 present pathの追加wait

通常present pathは:

```text
ensureSwapchain()
waitForSurfaceIdle()
descriptor update
record
submit
```

である。

swapchain recreate frameでは:

```text
ensureSwapchain内のwait
presentFrame内のwait
```

が重なる可能性がある。

これにHUD uploadの無期限fence waitが加わる。

---

# 6. fullscreen修正指示

## 6.1 active／pending swapchainを分ける

```cpp
struct SwapchainBundle
{
    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;
    VkRenderPass renderPass;
    VkPipeline pipeline;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkFramebuffer> framebuffers;
};

struct SurfaceState
{
    SwapchainBundle active;
    std::optional<SwapchainBundle> pending;
};
```

recreate:

```text
1. activeを維持
2. pendingを作成
3. pending作成成功
4. 次presentからpendingをactiveへswap
5. 旧activeをtimeline retire
```

新swapchain作成失敗時は旧activeを維持する。

---

## 6.2 無期限wait禁止

GUI／normal present pathでは:

```text
vkDeviceWaitIdle
vkQueueWaitIdle
vkWaitForFences(UINT64_MAX)
```

を禁止する。

使うもの:

```text
timeline semaphore completed value
bounded wait
retired resource queue
```

teardown時のみ無期限wait可。

---

## 6.3 config updateをlock-free publication化

screen transform／filter設定:

```cpp
std::atomic<std::shared_ptr<const VulkanSurfaceConfigSnapshot>>
```

またはmutex保護されたpending configへ書く。

presenterは次command recording時に読み取り、
vertex bufferを更新する。

config変更だけでsurface idleを待たない。

---

## 6.4 native surface replacement

本当にnative handleが変わる場合:

```text
old SurfaceHostを維持
new SurfaceHostを作成
new presenter surfaceをattach
new swapchain作成
new surfaceへ最初のpresent成功
activeSurfaceを切替
旧surfaceをretire
```

現在のように旧surfaceを先にdestroyしない。

---

## 6.5 Qt event debounce

毎present時の一時identity変化だけで切替えない。

対象event:

```text
QEvent::WindowStateChange
QEvent::Resize
QEvent::DevicePixelRatioChange
QPlatformSurfaceEvent::SurfaceCreated
QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed
```

`WindowStateChange`後、0ms～1 event-loop遅延してidentityとgeometryを再確認する。

同じidentityならswapchain resizeだけ。

---

# 7. P0: 2D黒透過／背景全面表示

この問題へ:

```text
黒ならalpha=1
背景なら最背面
```

のようなshader例外を追加してはいけない。

Sapphire structured 2Dは既に:

```text
3D slot
2D above
2D only
protected black
no 3D coverage
composition mode
```

をcontrol planeに保持している。

症状はcontrol algorithm不足より、
異なるscreen／frame／stateを同一pixelとして合成している可能性が高い。

---

# 8. raw packedとstructured planeの契約

frame latchは同時に次を読む。

```cpp
topPackedRaw =
    GPU.Framebuffer[frontBuffer][0];

bottomPackedRaw =
    GPU.Framebuffer[frontBuffer][1];

structuredTop =
    GetStructuredVulkan2DPlane(true, ...);

structuredBottom =
    GetStructuredVulkan2DPlane(false, ...);
```

その後、line単位で:

```text
topPackedRaw + structuredTop
bottomPackedRaw + structuredBottom
```

をmergeする。

したがって絶対条件:

```text
GPU.Framebuffer[front][0]とstructuredTopは同じphysical LCD
GPU.Framebuffer[front][1]とstructuredBottomは同じphysical LCD
同じemulated frame
同じscreen swap state
同じGPU register snapshot
```

である。

---

# 9. 今回変更された`GPU_Soft.cpp`

前回:

```cpp
if (GPU.ScreenSwap)
    SetFramebuffer(unitA, unitB);
else
    SetFramebuffer(unitB, unitA);

GPU.Framebuffer[...] = raw unit buffers;
```

現在:

```cpp
SetFramebuffer(unitA, unitB);

if (GPU.ScreenSwap)
{
    physicalTop = unitA;
    physicalBottom = unitB;
}
else
{
    physicalTop = unitB;
    physicalBottom = unitA;
}
```

方向として:

```text
Sapphire renderer内部はunit A/B identity
publication時だけphysical top/bottomへ変換
```

は合理的である。

ただし現在はpublication objectがなく、
次が別々の状態から画面所有権を推測している。

```text
raw packed:
  GPU.Framebuffer[][] pointer mapping

structured plane:
  CurrentUnitTargetsTopScreen()
  → framebuffer pointer比較
  → PowerControl9 fallback

3D:
  RenderScreenSwapAt3D

latch:
  frontBuffer
  screenSwap bool
```

一時的なscreen swap、frame boundary、reset、capture transitionで
1つでも更新順がずれると、rawとstructuredが別画面になる。

---

# 10. `CurrentUnitTargetsTopScreen()`の問題

現在:

```cpp
const u32* currentFramebuffer = Framebuffer[CurUnit->Num];

for (buffer)
{
    if (currentFramebuffer == GPU.Framebuffer[buffer][0])
        return true;

    if (currentFramebuffer == GPU.Framebuffer[buffer][1])
        return false;
}

fallback:
PowerControl9 bit15
```

これはscreen identityをpointer aliasから逆算している。

問題:

```text
GPU.Framebuffer mappingが毎scanline後に更新
DrawScanline中は前回publicationを参照
screen swap transition frameで旧mappingを見る可能性
front／back両bufferを探索するためalias契約が暗黙
pointer publication順序へ依存
```

Sapphire core algorithmが正しくても、
desktop adapterのpointer graphが違えば結果が変わる。

---

# 11. GPU2D UnitSyncはdependency closureではない

現行melonDSの`GPU2D`は:

```text
DispCntLatch[3]
LayerEnable
OBJEnable
ForcedBlank
BGXRefReload／BGYRefReload
BGMosaicLatch
OBJMosaicLatch
BGMosaicLine
OBJMosaicLine
```

などのstateを持つ。

vendored Sapphire `Unit`は別のlifecycleを持つ。

```text
VBlank
CheckWindows
UpdateMosaicCounters
OBJMosaicYCount
OBJMosaicYMax
DispFIFO state
CaptureLatch
```

現在の`UnitSync.cpp`は一部だけを毎scanlineコピーする。

不足／不整合候補:

```text
DispCnt delayed latchの完全な再現ではない
affine reference reload lifecycleを移植していない
mosaic latch／line stateを移植していない
OBJMosaicYCount／OBJMosaicYMaxを設定していない
Sapphire Unit自身のcounter更新とmelonDS側counter更新が二重
window active lifecycleを値コピーだけで再現
VBlank state transitionをdependency closureとして共有していない
```

これにより:

```text
window maskが効かずBG全面表示
layer enable timingずれ
affine backgroundの範囲異常
mosaic counter異常
forced blank timingずれ
```

が起こり得る。

---

# 12. 黒が透過する仕組み

Sapphire structured controlでは不透明黒を:

```text
pixel RGB = 0
pixel value自体は0ではない
control alphaにProtectedBlack flag 0x20
```

として区別する。

frame latch／compositorは:

```text
0                  = payloadなし
0x20000000         = 3D placeholder
RGB 0 + valid data = 不透明黒候補
control 0x20       = protected black
```

を使う。

raw／structuredのscreen ownerまたはframe ownerがずれると:

```text
黒pixelはscreen A
protected-black controlはscreen B
```

という組合せが発生する。

その場合、正しい黒が:

```text
payloadなし
3D slot
backdrop
```

として処理され、透過して見える。

したがって最初に直すべきものはpixel判定ではなく、
raw＋controlの同一owner保証である。

---

# 13. 背景が全面へ出る仕組み

全面背景は主に次のどちらか。

## A. window／layer state不一致

```text
Win0／Win1 mask
OBJ window
LayerEnable delay
DispCnt latch
BG priority
```

がSapphire Unitへ正しく渡らず、
本来window外で隠れるBGが全面描画される。

## B. 異なるstructured lineのmerge

frame latchには多くの:

```text
copyStructuredLine
mergeStructuredDisplayLine
temporal carry
capture repair
handoff promotion
```

がある。

入力screen ownershipが違うと、
別画面のBG planeをtop／bottom lineへコピーする。

Sapphireのrepair heuristicへさらに例外を追加してはいけない。

入力contractを直してから、同じSapphire algorithmを使用する。

---

# 14. 2D修正方針
## `SapphirePublished2DFrame`を導入する

pointerから後で所有権を推測しない。

frame boundaryで1回だけpublication objectを作る。

```cpp
enum class PhysicalScreen : u8
{
    Top,
    Bottom
};

struct SapphirePublished2DScreen
{
    const u32* packed = nullptr;
    const u32* structuredPlane0 = nullptr;
    const u32* structuredPlane1 = nullptr;
    const u32* structuredControl = nullptr;

    u32 engine = 0;
    PhysicalScreen physicalScreen = PhysicalScreen::Top;
};

struct SapphirePublished2DFrame
{
    SapphirePublished2DScreen top;
    SapphirePublished2DScreen bottom;

    int frontBuffer = 0;
    bool hardwareScreenSwap = false;
    bool renderScreenSwapAt3D = false;

    u64 emulatedFrameSerial = 0;
    u64 publicationGeneration = 0;
};
```

作成タイミング:

```text
VBlank／completed frame boundary
raw packed描画完了後
structured plane描画完了後
front/back確定後
```

latchは:

```cpp
latchSoftPackedFrameSnapshot(
    frame,
    const SapphirePublished2DFrame& published,
    ...);
```

のみを受け取る。

禁止:

```text
latch中にGPU.Framebuffer pointerを再解釈
structured planeを別accessorから後取得
PowerControl9をlatch側で再判定
```

---

# 15. Sapphire coreをそのまま持ってくる範囲

## 15.1 現在の方式の問題

現在は:

```text
現行melonDS GPU2D
    ↓ 毎scanline copy
Sapphire Unit
    ↓
Sapphire GPU2D renderer
```

である。

これは完全なdependency closureではなく、
version bridgeの再発明になっている。

## 15.2 第一選択

Sapphire coreから以下を一体でvendorする。

```text
GPU2D.h
GPU2D.cpp
GPU2D_Soft.h
GPU2D_Soft.cpp
GPU2D renderer interface
GPU2D Unit state lifecycle
VBlank／window／mosaic／capture state transition
```

namespaceを:

```text
melonDS::SapphireGPU2DCore
```

へ変えるだけに留める。

algorithm bodyを変えない。

## 15.3 register write routing

毎scanline全stateをcopyするのではなく、
register write／event発生時にSapphire Unitも更新する。

例:

```text
GPU2D register write
  → current melonDS GPU2D state update
  → Sapphire Unit same write

VBlank
  → Sapphire Unit::VBlank

VBlankEnd
  → Sapphire renderer::VBlankEnd
```

これによりSapphire自身のlatch／counter lifecycleを使用できる。

## 15.4 現行coreと共存が難しい場合

単一の明示的version adapterを作る。

```text
SapphireGPU2DVersionAdapter.h/.cpp
```

条件:

```text
全field mappingを表形式で管理
未対応fieldはcompile errorまたは明示static_assert
毎scanlineの推測ではなくframe event単位
golden state testを追加
algorithm fileへadapter条件を入れない
```

---

# 16. そのままvendorするfrontend範囲

以下はSapphire tag `0.7.0.rc4`から再vendorする。

```text
FrameQueue.h
FrameQueue.cpp
VulkanOutput.h
VulkanOutput.cpp
VulkanCompositorShader.comp
VulkanAccumulate3dShader.comp
VulkanSurfacePresenter.h
VulkanSurfacePresenter.cpp
VulkanSurfacePresenter.vert
VulkanSurfacePresenter.frag
```

desktop差分:

```text
Qt surface host
screen affine transform
CustomHUD overlay hook
OSD／splash
runtime pacing bridge
```

は別fileへ置く。

Sapphire C++本体を`local_baseline`として自由変更しない。

---

# 17. 現在のvendor parity CIは不十分

manifestでは主要fileが:

```json
"parity_mode": "local_baseline"
```

である。

対象:

```text
FrameQueue
VulkanOutput
VulkanSurfacePresenter cpp/h
GPU2D_Soft cpp/h
```

checkerは`local_baseline`の場合:

```text
manifestに記録したlocal hash
現在のlocal hash
```

しか比較しない。

workflowも:

```bash
python3 tools/check_sapphire_vendor_parity.py
```

だけで、Sapphire cloneも`--verify-upstream`もない。

したがって現在のCIが保証するものは:

```text
現在の独自実装から勝手に変わっていない
```

だけであり:

```text
Sapphireと同じ
```

ではない。

---

# 18. parity CI修正

workflow:

```yaml
- name: Checkout Sapphire Android
  uses: actions/checkout@v4
  with:
    repository: SapphireRhodonite/melonDS-android
    ref: 0.7.0.rc4
    path: sapphire-android

- name: Checkout Sapphire core
  uses: actions/checkout@v4
  with:
    repository: SapphireRhodonite/melonDS-android-lib
    ref: d77944275fa61f9b79cfcead2c3e98993429a023
    path: sapphire-android-lib

- name: Verify Sapphire parity
  env:
    SAPPHIRE_ANDROID_ROOT: ${{ github.workspace }}/sapphire-android
    SAPPHIRE_ANDROID_LIB_ROOT: ${{ github.workspace }}/sapphire-android-lib
  run: python3 tools/check_sapphire_vendor_parity.py --verify-upstream
```

manifest:

```text
algorithm file:
  exact_upstream

include／namespace／build guardだけ違う:
  normalized_upstream

desktop-only file:
  desktop_adapter_exempt

local_baseline:
  原則禁止
```

`normalized_upstream`で除去してよいもの:

```text
Source comment
include path prefix
namespace wrapper
MELONPRIME build guard
volk／dispatch include adaptation
```

除去してはいけないもの:

```text
if条件
threshold
pixel algorithm
capture repair
frame queue policy
resource lifecycle
wait logic
```

---

# 19. 必須diagnostic

## 19.1 HUD

rate-limited log:

```text
HUD image size
dirty rect
upload bytes
staging capacity
texture generation
old/new image layout
upload result
compositeRequested
callback presenter address
callback userData
```

assert:

```cpp
assert(rect.x() >= 0);
assert(rect.y() >= 0);
assert(rect.right() < textureWidth);
assert(rect.bottom() < textureHeight);
```

## 19.2 2D ownership

frameごとに最初の120 frame:

```text
frameSerial
frontBuffer
GPU.ScreenSwap
PowerControl9 bit15
RenderScreenSwapAt3D

unitA packed pointer
unitB packed pointer
published top packed owner
published bottom packed owner

structured top owner
structured bottom owner

top raw checksum
top structured control checksum
bottom raw checksum
bottom structured control checksum
```

assert:

```cpp
published.top.engine == structuredTopEngine;
published.bottom.engine == structuredBottomEngine;
published.emulatedFrameSerial == structuredPublicationSerial;
```

## 19.3 protected black

各screen:

```text
opaque black pixel count
protected black control count
opaque black without protected flag count
protected flag with zero payload count
```

異常条件:

```text
opaque black without protected flagが急増
protected flag countがscreen swap時に反対画面へ移る
```

## 19.4 background／window

各scanline sample:

```text
DispCnt raw
EffectiveDispCnt
LayerEnable
OBJEnable
ForcedBlank
Win0Active
Win1Active
WinCnt
WindowMask checksum
BG mode
BG priority
```

Software／OpenGL／Vulkanで比較する。

---

# 20. Commit分割

## Commit 1

```text
Fix Vulkan HUD upload capacity and dirty offsets
```

変更:

```text
dynamic staging
imageOffset
layout tracking
last valid overlay保持
upload failure log
```

## Commit 2

```text
Make desktop overlay callback presenter-local
```

変更:

```text
static callback削除
instance／surface member化
destructor isolation
```

## Commit 3

```text
Integrate HUD upload into presenter submission
```

変更:

```text
queue submit統合
無期限wait削除
timeline retire
```

## Commit 4

```text
Add immutable Sapphire 2D frame publication
```

変更:

```text
SapphirePublished2DFrame
frame-boundary publication
latch API変更
ownership assertions
```

## Commit 5

```text
Vendor Sapphire GPU2D dependency closure
```

変更:

```text
Unit lifecycle
register write routing
window／mosaic／capture event
UnitSync部分copy廃止
```

## Commit 6

```text
Restore exact Sapphire Vulkan compositor sources
```

変更:

```text
VulkanOutput exact／normalized parity
compositor shader exact
desktop adapter分離
```

## Commit 7

```text
Make fullscreen swapchain replacement non-blocking
```

変更:

```text
active／pending bundle
old resource timeline retire
config publication
same-surface resize
```

## Commit 8

```text
Enforce true Sapphire upstream parity in CI
```

変更:

```text
reference checkout
--verify-upstream
local_baseline削減
normalized diff
```

---

# 21. 検証マトリクス

## CustomHUD

```text
windowed
fullscreen
DPI 100％／125％／150％／200％
HUD ON／OFF
edit mode
font変更
HUD scale変更
HP／ammo／score／rank／time
radar
backend switch
window再生成
```

完了条件:

```text
初回frameから表示
fullscreen後も消えない
旧panel破棄で別presenterのHUDが消えない
dirty updateで正しい座標
upload失敗時も最後のvalid HUDを維持
```

## fullscreen

```text
windowed → fullscreen
fullscreen → windowed
Alt+Enter連打
別monitor
DPI違いmonitor
maximize
restore
```

計測:

```text
WindowStateChange
final geometry
swapchain pending create
active swap
first present
```

完了条件:

```text
数秒黒画面0
normal GUI pathの無期限wait 0
同一native identityでsurface generation不変
旧frameを新swapchain完成まで表示可能
```

## 2D

```text
boot menu
title menu
pause menu
match
match transition
display capture
VRAM display
window mask多用画面
黒背景
fade
brightness
screen swap transition
```

比較:

```text
Software
OpenGL Classic
Vulkan
Sapphire Android
```

完了条件:

```text
不透明黒が透過しない
BGがwindow外へ全面表示されない
screen swap前後でraw／structured owner一致
protected black countがreferenceと一致
```

---

# 22. 禁止事項

```text
黒pixelを無条件opaque化
RGB==0だけでalpha判定
背景全面表示をshader clipで隠す
ROM／画面ごとの例外
screenSwap boolを無条件反転
frame latch heuristicをさらに追加
stagingCapacityだけ巨大固定値へ変更
upload失敗時にHUDをclear
GUI threadでvkWaitForFences(UINT64_MAX)
fullscreen時にpresenter unregister
fullscreen resizeでVkSurfaceKHR破棄
Sapphire algorithm fileをlocal_baselineのまま変更
「parity OK」を上流未比較で表示
```

---

# 23. 最終指示

試合中3Dが正常であるため、Vulkan 3D renderer本体は維持する。

今回の修正軸は次の3点。

```text
1. CustomHUD
   正規CustomHud_Renderは既に使えている。
   upload／lifecycleだけを修正する。

2. 2D
   pixel heuristicを修正する前に、
   raw packedとstructured planeを同一frame publicationへ統合する。
   Sapphire GPU2D Unit lifecycleをdependency closureとして移植する。

3. fullscreen
   oldSwapchain parameterだけで完了とせず、
   active resourceを維持したままpending swapchainを構築する。
```

車輪の再発明を避けるため:

```text
Sapphire algorithmはexact／normalized vendor
MelonPrime差分はdesktop adapter
両者の境界はtyped publication objectとgeneric hook
```

へ固定すること。

この責務分離が完了するまで、
2D compositor shaderへ個別の黒透過／背景例外を追加してはいけない。

---

# 24. 実施進捗 (S63)

| Phase | Commit | Hash | Status |
|---|---|---|---|
| S63-1 | Fix Vulkan HUD upload capacity and dirty offsets | `1156a4f50` | done |
| S63-2 | Make desktop overlay callback presenter-local | `4adb6b9ca` | done |
| S63-3 | Integrate HUD upload into presenter submission | `13a80759c` | done |
| S63-4 | Add immutable Sapphire 2D frame publication | `6f04262ec` | done |
| S63-5 | Vendor Sapphire GPU2D dependency closure | `110b17d6a` | done |
| S63-6 | Restore exact Sapphire Vulkan compositor sources | `006af75ae` | done |
| S63-7 | Make fullscreen swapchain replacement non-blocking | `11bd5b6d0` | done |
| S63-8 | Enforce true Sapphire upstream parity in CI | `aaff82243` | done |

**Branch:** `highres_fonts_v3`  
**最新push:** S63-8 `aaff82243`（全フェーズ完遂）  
**検証:** Windows MinGW release build 成功。ROM手動検証（§21）は未実施。

