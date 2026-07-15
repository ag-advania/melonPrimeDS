# Vulkan 初回ROM白画面・ゲームHUD欠落・CustomHUD未表示・4x文字潰れ・Fullscreen黒画面・2D異常
## Sapphire state ownership／lifecycle準拠監査・修正指示書（S66）

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `16426bf6ca840d6b53be373c0db9eb873e866589`  
**前回監査HEAD:** `f89820754f46d0672a6b95d2020a735c39ce3902`  
**前回監査後:** 9 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android` tag `0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib` commit `d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 症状

```text
1. 試合画面の3D映像は正常
2. 3D映像の上にゲーム本来のHUD／2D layerが表示されない
3. CustomHUDも表示されない
4. 4xなど一部内部解像度でゲーム内文字が潰れる
5. fullscreen切替時に数秒黒画面になる
6. 2D画面の透過、背景、window、layer合成がおかしい
7. Vulkanを選択した状態でROMを開くと白画面が続く
8. 他rendererでROMを開いた後にVulkanへ切り替えると3Dは表示される
```

ユーザー補足:

```text
4x文字潰れは今回のpushによる回帰ではなく、以前から存在していた。
```

---

# 1. 監査結論

## 1.1 最重要の根本原因

現在のVulkan切替は、3D rendererだけでなく外側の`SoftRenderer`も毎回作り直す。

```text
CreateRendererForSelection(Vulkan)
    ↓
new SoftRenderer
    ↓
new Sapphire Unit A
new Sapphire Unit B
new Sapphire GPU2D SoftRenderer
    ↓
GPU::SetRenderer()
    ↓
SoftRenderer::Reset()
    ↓
Sapphire Unit A/B Reset
    ↓
Vulkan Renderer3D install
```

しかし現在の`UnitSync`がnative GPU2Dから復元するのは次だけである。

```text
Enabled
MasterBrightness
CaptureCnt
CaptureLatch
Display FIFO
```

復元されないstate:

```text
DispCnt
BGCnt[4]
BG X/Y scroll
affine matrix
BGXRef／BGYRef
BGXRefInternal／BGYRefInternal
window coordinates
window control
window active state
mosaic size
mosaic Y counters
BlendCnt
BlendAlpha
EVA／EVB／EVY
OBJ mosaic internal counters
```

このため外側`SoftRenderer`の再作成後、Sapphire Unitはnative GPU2Dと不一致になる。

直接説明できる症状:

```text
ゲーム本来のHUDが消える
2D BG／OBJが消える
window maskが壊れる
黒が誤透過する
背景が全面表示される
blendが壊れる
4x文字の一部が欠ける
```

---

## 1.2 初回Vulkan白画面も同じ原因で成立する

Direct Boot／ROM bootの順序:

```text
new NDS
GPU／初期SoftRenderer生成
NDS Reset
SetupDirectBoot
GPU2D state作成
nds->Start
    ↓
最初のemulation loop
videoSettingsDirty
    ↓
CreateRendererForSelection(Vulkan)
    ↓
別のSoftRendererを新規作成
    ↓
Sapphire Unit A/BをReset
    ↓
既存native GPU2D stateを完全seedしない
```

他rendererではnative GPU2D stateをそのまま使用するため白画面にならない。

VulkanはResetされたSapphire Unitを描画sourceとして使うため、ROM起動時から白画面／空の2D frameになり得る。

これは:

```text
他rendererで起動
→ ROMが進行
→ Vulkanへ切替
→ 3Dは表示される
→ 既存2D／HUDは欠落
```

という現在の回避手順とも整合する。

---

## 1.3 S65の実装状況

前回指示した以下は実装済み。

```text
S65-1 HostWriteToTransferRead
S65-2 HUD staging ring
S65-3 2D描画前binding
S65-4 completed-frame publication
S65-5 compositor／surface filter分離
S65-6 active／pending swapchain
S65-7 generated vendor CI
```

ただし、実装が成立する前提にまだ問題がある。

```text
S65-2:
    slot完了判定がGPU completionではない

S65-3／4:
    framebufferは依然engine-owned bufferを最終screenSwapで再解釈

S65-5:
    2D state自体が壊れている場合は文字潰れを直せない

S65-6:
    pending作成前にsurface idle wait
    native identity変化時は依然surface全破棄

S65-7:
    key lifecycle adapterはmanifest対象外
```

---

# 2. Sapphireと同じ実装か

## 2.1 同一性が高い部分

現在のmanifestでexact upstream:

```text
VulkanSurfacePresenter.vert
VulkanSurfacePresenter.frag
VulkanCompositorShader.comp
VulkanAccumulate3dShader.comp
```

normalized upstream:

```text
FrameQueue
VulkanOutput
VulkanSurfacePresenter
GPU2D_Soft
```

workflowも現在は:

```text
Sapphire frontend checkout
Sapphire core checkout
upstream snapshot verify
generated source verify
normalized／exact parity verify
```

を実行している。

この改善は維持する。

---

## 2.2 Sapphireと同じではない部分

今回の症状に直結する以下は、Sapphire algorithm parityだけでは保証されない。

```text
GPU_Soft.cpp
GPU.cppのrenderer交換順
MelonPrimeRendererFactory
MelonPrimeSapphireGpu2DAdapter
UnitSync
SapphirePublished2DFrame
SapphireVulkanFrameLatch
MelonPrimeVulkanFrontendSession
MelonPrimeVulkanOverlayRenderer
MelonPrimeScreenVulkan
Qt native surface lifecycle
```

特にmanifestは:

```text
GPU2D_Soft.h／cpp
```

を検証するが、Sapphire Unitの所有期間とrenderer切替契約を検証しない。

したがって現在のCI passは:

```text
Sapphire pixel algorithmの主要部分が同じ
```

ことは示すが、

```text
Sapphireと同じstate lifetimeで動いている
```

ことは示さない。

---

# 3. P0
# Sapphire Unitをrenderer-ownedからGPU-ownedへ移す

## 3.1 現在の誤った所有関係

```text
SoftRenderer
 ├─ SapphireUnitA
 ├─ SapphireUnitB
 └─ Sapphire2DRenderer
```

外側renderer交換で、GPU2D stateまで破棄される。

---

## 3.2 推奨所有関係

```text
GPU
 └─ SapphireGpu2DState
     ├─ Sapphire Unit A
     ├─ Sapphire Unit B
     └─ Sapphire GPU2D SoftRenderer

SoftRenderer
 └─ SapphireGpu2DStateへのreference／view
```

GPU2D register stateはGPU lifecycleに属する。

3D backend交換に属さない。

これは新しいGPU2D algorithmを発明するものではない。SapphireのUnitとSoftRendererをそのままpersistent ownerへ置くだけである。

---

## 3.3 構造例

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

class SapphireGpu2DState
{
public:
    explicit SapphireGpu2DState(melonDS::GPU& gpu)
        : UnitA(0, gpu)
        , UnitB(1, gpu)
        , Renderer(gpu)
    {
    }

    void Reset()
    {
        UnitA.Reset();
        UnitB.Reset();
        Renderer.ClearStructuredVulkan2DState();
    }

    SapphireGPU2DCore::GPU2D::Unit UnitA;
    SapphireGPU2DCore::GPU2D::Unit UnitB;
    SapphireGPU2DCore::GPU2D::SoftRenderer Renderer;
};

#endif
```

`GPU`:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
std::unique_ptr<SapphireGpu2DState> Sapphire2D;
#endif
```

生成:

```text
GPU constructor中
native GPU2D A/Bと同じlifetime
renderer選択より前
```

Reset:

```text
GPU::Reset()
    native GPU2D Reset
    SapphireGpu2DState Reset
```

破棄:

```text
GPU destructor
```

---

# 4. register forwardingをcurrent outer rendererから切り離す

## 4.1 現在

```cpp
if (auto* renderer =
        dynamic_cast<SoftRenderer*>(&gpu.GetRenderer()))
{
    renderer->ForwardSapphireGpu2DRegisterWrite...();
}
```

OpenGL／Metalなど別outer renderer中はforwardされない。

その状態からVulkanへ切り替えても、過去のregister write履歴をSapphire Unitは持っていない。

---

## 4.2 修正

```cpp
void ForwardRegisterWrite16(
    GPU& gpu, u32 engine, u32 addr, u16 value)
{
    auto* state = gpu.TryGetSapphireGpu2DState();
    if (state == nullptr)
        return;

    auto& unit =
        engine == 0
            ? state->UnitA
            : state->UnitB;

    unit.Write16(addr, value);
}
```

同様に:

```text
Write8
Write16
Write32
Window Check
VBlank
VBlankEnd
Display FIFO
Capture lifecycle
```

をpersistent stateへ送る。

outer rendererの種類に依存させない。

---

# 5. Vulkan選択時にouter SoftRendererを不要に作り直さない

## 5.1 現在

`CreateRendererForSelection(Vulkan)`:

```cpp
result.OuterRenderer =
    std::make_unique<SoftRenderer>(nds);

result.Renderer3D =
    CreateSapphireVulkanRenderer3D(...);
```

`applyRendererCreation()`:

```cpp
nds->SetRenderer(
    std::move(result.OuterRenderer));

nds->GPU.SetRenderer3D(
    std::move(result.Renderer3D));
```

必ずouterを置換する。

---

## 5.2 最小修正

`RendererCreationResult`:

```cpp
enum class OuterRendererAction
{
    KeepCurrent,
    Replace,
};

OuterRendererAction OuterAction =
    OuterRendererAction::Replace;
```

Vulkan:

```cpp
if (dynamic_cast<melonDS::SoftRenderer*>(
        &nds.GPU.GetRenderer()) != nullptr)
{
    result.OuterAction =
        OuterRendererAction::KeepCurrent;
}
else
{
    result.OuterRenderer =
        std::make_unique<SoftRenderer>(nds);

    result.OuterAction =
        OuterRendererAction::Replace;
}
```

commit:

```cpp
if (result.OuterAction
    == OuterRendererAction::Replace)
{
    nds->SetRenderer(
        std::move(result.OuterRenderer));
}

nds->GPU.SetRenderer3D(
    std::move(result.Renderer3D));
```

これにより初回NDS constructorで既に存在するSoftRendererを再利用できる。

ただし最終形は前章のpersistent SapphireGpu2DStateである。

---

# 6. one-time full seedを主設計にしない

native GPU2DからSapphire Unitへ全fieldを1回コピーすれば、表面的には復旧できる。

しかし次の内部stateは単純なregister valueだけでは完全再構築できない。

```text
BGXRefInternal
BGYRefInternal
mosaic Y phase
OBJ mosaic counters
window active phase
capture latch
FIFO read position
midscanline write history
```

したがって:

```text
毎切替時に手書きfield copy
```

をproduction設計にしない。

推奨:

```text
GPU lifecycle開始時からSapphire Unitをpersistentに追従
```

一時的なmigration helperを作る場合も:

```text
paused
renderLock保持
frame boundary
1回だけ
debug state digestで一致検証
```

に限定する。

---

# 7. P0
# framebufferをengine-ownedではなくphysical-screen-ownedにする

## 7.1 現在

```text
Framebuffer[buffer][0] = Unit A output
Framebuffer[buffer][1] = Unit B output
```

描画時:

```cpp
Sapphire2DRenderer->SetFramebuffer(
    unitABuffer,
    unitBBuffer);
```

publication時:

```text
現在のGPU.ScreenSwapで
buffer全体をtop／bottomへ再解釈
```

問題:

```text
screen swapがframe中に変化
publication時のlive ScreenSwapが描画時と異なる
menu／transitionでownerが変化
```

すると、completed bufferを単一のfinal swap bitで正しくphysical screenへ戻せない。

---

## 7.2 推奨

```text
Framebuffer[buffer][0] = physical top
Framebuffer[buffer][1] = physical bottom
```

描画前:

```cpp
void SoftRenderer::BindSapphirePhysicalTargets()
{
    u32* const physicalTop =
        Framebuffer[BackBuffer][0];

    u32* const physicalBottom =
        Framebuffer[BackBuffer][1];

    GPU.Framebuffer[BackBuffer][0] =
        physicalTop;

    GPU.Framebuffer[BackBuffer][1] =
        physicalBottom;

    if (GPU.ScreenSwap)
    {
        Sapphire2DRenderer->SetFramebuffer(
            physicalTop,
            physicalBottom);
    }
    else
    {
        Sapphire2DRenderer->SetFramebuffer(
            physicalBottom,
            physicalTop);
    }
}
```

これでUnit A/Bは各scanlineのcurrent `ScreenSwap`に従い、正しいphysical screen bufferへ書く。

---

## 7.3 completed publication

```cpp
void SoftRenderer::PublishCompletedSapphireFrontBuffer()
{
    GPU.FrontBuffer = BackBuffer ^ 1;

    GPU.Framebuffer[0][0] =
        Framebuffer[0][0];
    GPU.Framebuffer[0][1] =
        Framebuffer[0][1];

    GPU.Framebuffer[1][0] =
        Framebuffer[1][0];
    GPU.Framebuffer[1][1] =
        Framebuffer[1][1];

    PublishSapphire2DFrame();
}
```

publication時にcurrent `GPU.ScreenSwap`でwhole bufferを再配置しない。

---

## 7.4 structured planeとの一致

Sapphireの`CurrentUnitTargetsTopScreen()`は:

```text
current Unit framebuffer pointer
vs
GPU.Framebuffer[buffer][physical top/bottom]
```

を比較する。

physical targetを描画前に確定すれば:

```text
raw packed physical owner
structured plane physical owner
control physical owner
```

が同じpointer contractから決定される。

---

# 8. `Published2DFrame.engine`を画面全体の真実として使わない

frame中にscreen ownershipが変わる場合:

```text
top engine = A
```

または:

```text
top engine = B
```

という1個の値では表現できない。

`Published2DFrame`のproduction contractは:

```text
top.packed = physical top
bottom.packed = physical bottom
structured top = physical top
structured bottom = physical bottom
```

にする。

`engine`はdiagnosticに限定するか削除する。

3D ownerは別に:

```text
GPU3D.RenderScreenSwapAt3D
```

を使用する。

---

# 9. P0
# CustomHUD staging ringはまだframe-safeではない

## 9.1 現在

transfer記録時:

```text
slot.inFlight = true
```

present resultが`PresentedGameFrame`になると:

```cpp
overlayRenderer.releaseUploadSlots();
```

が全slotを即座にfree扱いにする。

しかし`PresentedGameFrame`が意味するのは:

```text
command submit／queue present accepted
```

であり:

```text
GPUがstaging bufferを読み終えた
```

ではない。

presenterはその後に完了するtimeline valueを`frame->presentTimelineValue`へ格納している。

---

## 9.2 発生し得ること

```text
frame N:
    GPUがslot 0からtextureへcopy中

CPU frame N+1:
    PresentedGameFrameを見てslot 0をfree扱い
    slot 0へ次HUDを書込む

GPU frame N:
    書換え中のslot 0を読む
```

結果:

```text
CustomHUDが透明
一部欠ける
表示が不安定
fullscreen後に消える
```

---

## 9.3 修正

slot:

```cpp
struct OverlayUploadSlot
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize capacity = 0;

    bool recorded = false;
    u64 completionTimelineValue = 0;
};
```

submit後:

```cpp
overlayRenderer.markUploadSubmitted(
    recordedSlot,
    frame->presentTimelineValue);
```

次frame開始時:

```cpp
u64 completed = 0;

if (presenter->getCompletedTimelineValue(completed))
{
    overlayRenderer.releaseCompletedUploadSlots(
        completed);
}
```

解放条件:

```cpp
slot.completionTimelineValue != 0
&& slot.completionTimelineValue <= completed
```

全slotを一括解放しない。

---

# 10. HUD pipeline lifetime

`ensurePipeline()`はrender passが変わると:

```text
旧pipelineを即destroy
新pipelineを作成
```

する。

fullscreenで旧swapchainのcommandがin-flightの場合、旧HUD pipelineを即破棄してはいけない。

推奨:

```text
overlay pipelineをSwapchainBundle単位で所有
```

または:

```text
旧pipelineをpresenter timelineでretire
```

する。

`vkDeviceWaitIdle()`を通常fullscreen切替へ入れない。

---

# 11. CustomHUDはgame frame presentationに依存する

現在:

```text
HUD QImage作成
HUD upload staging
FrameQueueからgame frame取得
frame == nullptrならreturn
```

game frameが取得できないと:

```text
HUD transfer commandなし
HUD draw commandなし
```

になる。

したがって初回白画面／producer discardが直る前は、CustomHUDだけを単独修正しても表示されない。

修正順:

```text
1. Sapphire Unit state
2. first valid game frame
3. HUD timeline lifetime
4. HUD表示確認
```

---

# 12. 4x文字潰れ

## 12.1 S65-5は実装済み

現在:

```text
Linear surface
scale > 1
packed 2Dあり
    ↓
direct presentを無効
    ↓
Sapphire compositorはNearest
    ↓
final surface samplerだけLinear
```

方向は正しい。

Sapphire exact shaderを変更しない。

---

## 12.2 現在も潰れる場合の優先順位

第一候補:

```text
Sapphire Unit state欠落
```

文字を構成するBG／OBJ／window／blend state自体が壊れている。

第二候補:

```text
physical screen owner mismatch
```

文字pixelとcontrol metadataが別screenへ入る。

第三候補:

```text
filter分離pathへ入っていない
```

診断ログ:

```text
scale
surfaceFilter
requiresSeparated2dFiltering
directPresent
composeFilter
presenterSampler
drawMode
```

期待:

```text
4x＋Linear:
directPresent=0
composeFilter=Nearest
presenterSampler=Linear
```

---

## 12.3 必須比較

```text
4x Nearest
4x Linear
```

判定:

```text
Nearestでも潰れる:
    2D state／owner問題

Linearだけ潰れる:
    filter path／UV／sampler問題
```

Sapphire compositor shaderへ4x専用補正を追加しない。

---

# 13. P1
# fullscreen active／pending実装はまだblocking

## 13.1 pending作成前のwait

現在:

```cpp
if (active.swapchain != VK_NULL_HANDLE)
{
    waitForSurfaceIdle(
        surfaceState,
        50ms);
}
```

その後にpending swapchainを構築する。

activeを維持しているが、pending作成前にGUI threadが待つため、真のnon-destructive transitionではない。

---

## 13.2 native identity変化時の全破棄

`ensureNativeSurface()`:

```text
unregisterPresenter
detachSurface
surfaceHost.destroy
new VkSurfaceKHR
attachSurface
registerPresenter
```

`detachSurface()`はdefaultで無期限`waitForSurfaceIdle()`を行う。

`destroySwapchain()`はさらに:

```text
vkQueueWaitIdle(presentQueue)
```

を行う。

fullscreen transition中にこのpathへ入ると、数秒の黒画面を直接発生させられる。

---

## 13.3 Qt event contract

次のeventではsurfaceを破棄しない。

```text
WindowStateChange
Resize
DevicePixelRatioChange
ScreenChangeInternal
```

処理:

```text
resizeSurface
configureSurface
swapchainDirty
```

だけ。

surface detachを許可するのは:

```text
QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed
widget destruction
explicit backend switch
device loss
```

に限定する。

---

## 13.4 pending promotion

推奨:

```text
1. active表示を継続
2. waitなしでpending resource構築
3. active in-flight fence／timeline完了確認
4. pendingへpromotion
5. old activeをtimeline retire
```

command buffer／fenceを共有するため待ちが必要なら:

```text
pending resource作成は先に実行
command buffer reuse直前だけbounded wait
```

にする。

---

## 13.5 teardown wait

正常fullscreen pathから以下を排除する。

```text
waitForSurfaceIdle(UINT64_MAX)
vkQueueWaitIdle
vkDeviceWaitIdle
```

これらを許可するのは:

```text
shutdown
backend teardown
device loss recovery
SurfaceAboutToBeDestroyed
```

のみ。

---

# 14. first-frame diagnostics

初回Vulkan白画面を確定するため、最初の10frameだけ次を記録する。

```text
[VulkanBootstrap]
nativeGpu2dA.DispCnt
sapphireUnitA.DispCnt
nativeGpu2dB.DispCnt
sapphireUnitB.DispCnt

native BGCnt checksum
Sapphire BGCnt checksum

native window checksum
Sapphire window checksum

native blend checksum
Sapphire blend checksum

rendererGeneration
VulkanFrameSerial
Published2DFrame.valid
Published2DFrame.frameSerial
frontBuffer
screenSwap
```

outer交換前後:

```text
[RendererTransaction]
oldOuterType
newOuterType
outerReplaced
sapphireStateAddressBefore
sapphireStateAddressAfter
```

期待:

```text
Vulkan初回起動:
outerReplaced=0
Sapphire state address不変
native／Sapphire digest一致
```

---

# 15. 2D owner diagnostics

各frameの代表line:

```text
0
32
64
96
128
160
191
```

について:

```text
frameSerial
line
ScreenSwap
Unit A target physical screen
Unit B target physical screen
top packed pointer
bottom packed pointer
top structured pointer
bottom structured pointer
top control checksum
bottom control checksum
```

不透明黒:

```text
ProtectedBlack top count
ProtectedBlack bottom count
```

3D上HUD:

```text
StructuredAbove top count
StructuredAbove bottom count
```

ゲームHUDが消えるframeで:

```text
StructuredAboveが0へ落ちる
または
反対screenへ移る
```

かを確認する。

---

# 16. parity coverage拡張

manifestへ追加を検討する。

```text
SapphireGPU2DRenderer2D.h
Sapphire Unit implementation source
SapphireVulkanFrameLatchでvendorしたupstream section
```

adapter contract test対象:

```text
GPU_Soft
GPU::SetRenderer
MelonPrimeRendererFactory
MelonPrimeSapphireGpu2DAdapter
UnitSync
SapphirePublished2DFrame
```

これらは完全upstream一致にできないため、hash baselineだけではなくbehavior testを置く。

---

## 16.1 lifecycle parity test

```text
NDS Reset
Direct Boot setup
Vulkan install
OpenGL→Vulkan
Vulkan→Software→Vulkan
savestate load
reset
```

各phaseで:

```text
Sapphire Unit digest
native GPU2D digest
```

が一致すること。

---

## 16.2 physical screen parity test

frame途中でscreen swapを変更するsynthetic testを用意。

期待:

```text
変更前line:
Unit A→旧physical screen

変更後line:
Unit A→新physical screen

completed physical top/bottom:
line単位で正しい
```

final `ScreenSwap`でwhole bufferを再解釈する実装はfailにする。

---

# 17. 推奨commit分割

## S66-1

```text
Preserve the outer SoftRenderer across Vulkan 3D installation
```

変更:

```text
OuterRendererAction
既存SoftRenderer再利用
initial Direct Boot state保持
```

## S66-2

```text
Make Sapphire GPU2D state persistent across renderer switches
```

変更:

```text
SapphireGpu2DStateをGPU ownerへ移動
Unit A/B persistent
GPU2D SoftRenderer persistent
```

## S66-3

```text
Forward Sapphire GPU2D events independently of the active outer renderer
```

変更:

```text
register write
window
VBlank
VBlankEnd
FIFO
capture
```

## S66-4

```text
Store Vulkan packed 2D buffers by physical screen
```

変更:

```text
physical top／bottom framebuffer
Unit A/B target pointerを描画前にswap
publication remap削除
```

## S66-5

```text
Tie Vulkan HUD staging slots to presenter timeline completion
```

変更:

```text
slot timeline
completed value collect
全slot即解放廃止
```

## S66-6

```text
Retire Vulkan HUD pipelines with their swapchain generation
```

変更:

```text
render-pass generation ownership
timeline retire
```

## S66-7

```text
Remove blocking surface teardown from fullscreen transitions
```

変更:

```text
WindowStateChangeはresizeのみ
QPlatformSurfaceEvent contract
pending先行構築
normal path無期限wait廃止
```

## S66-8

```text
Add Sapphire lifecycle and physical-owner parity tests
```

変更:

```text
Direct Boot
backend switch
midframe screen swap
state digest
```

---

# 18. 修正順

```text
1. S66-1
   初回Vulkan時のouter再作成を止める

2. S66-2～3
   Sapphire Unitをpersistent化
   backend切替中もregister history保持

3. S66-4
   physical screen framebufferへ変更

4. 初回ROM白画面とゲーム本来HUDを再検証

5. S66-5～6
   CustomHUD GPU completion lifetime修正

6. 4x Nearest／Linearを再検証

7. S66-7
   fullscreen無停止化

8. S66-8
   parity contractをCI固定
```

この順序を崩さない。

---

# 19. 禁止事項

```text
ゲームHUDのpixelを別途CPUで合成
missing HUDを画像overlayで再実装
ROM別HUD例外
黒pixelを無条件opaque化
compositor shaderへ4x専用補正
Sapphire exact shaderの直接編集
毎scanline全GPU2D state memcpyへ戻す
renderer切替ごとにSapphire Unit Reset
current ScreenSwapでcompleted buffer全体を再解釈
PresentedGameFrame直後に全HUD slot解放
fullscreen WindowStateChangeでsurface detach
normal pathでvkQueueWaitIdle
normal pathでvkDeviceWaitIdle
manifest hash更新だけでparity pass
```

---

# 20. 必須テスト

## 20.1 初回起動

```text
Vulkan選択状態でアプリ起動
ROM Direct Boot
firmware boot
別ROM
reset
20回連続起動
```

完了条件:

```text
白画面0
他renderer経由不要
first valid frame 1～3frame以内
```

---

## 20.2 3D上のゲーム本来HUD

```text
match
pause
weapon menu
damage
ammo更新
score更新
zoom
radar
```

完了条件:

```text
3Dの上にOBJ／BG HUD表示
window／blend正常
opaque black正常
```

---

## 20.3 backend switch

```text
Software→Vulkan
OpenGL→Vulkan
Vulkan→Software→Vulkan
Vulkan→OpenGL→Vulkan
各20回
```

完了条件:

```text
Sapphire Unit address／state継続
2D消失0
白画面0
```

---

## 20.4 CustomHUD

```text
HUD ON／OFF
edit mode
font変更
HP
ammo
score
rank
timer
radar
fullscreen
DPI 100％～200％
```

完了条件:

```text
HUD表示
slot overwrite 0
timeline completion前reuse 0
fullscreen後も継続
```

---

## 20.5 4x文字

```text
1x Nearest
2x Nearest
3x Nearest
4x Nearest
4x Linear
```

完了条件:

```text
4x Nearestでpixel shape維持
Linearでもcompositor内部はNearest
```

---

## 20.6 fullscreen

```text
windowed→fullscreen
fullscreen→windowed
Alt+Enter連打
maximize／restore
別monitor
異なるDPI monitor
```

完了条件:

```text
数秒黒画面0
surface detach 0
normal path queueWaitIdle 0
旧frameをpending完成まで維持
```

---

# 21. 最終判断

試合中3Dが正常であるため、Vulkan 3D rasterizer本体を変更する必要はない。

現在の問題は次の4層に分かれる。

```text
初回白画面／ゲームHUD欠落／2D異常:
    Sapphire GPU2D state ownership

CustomHUD:
    upload slot／pipeline GPU lifetime

4x文字:
    まずGPU2D stateを修正
    その後filter pathを検証

fullscreen:
    Qt surface lifecycleとblocking teardown
```

最も重要なのは:

```text
Sapphire Unitを3D renderer交換で破棄しない
```

ことである。

Sapphireからそのまま利用するもの:

```text
Unit
GPU2D SoftRenderer
compositor shader
presenter shader
FrameQueue
VulkanOutput
```

MelonPrime独自adapterに残すもの:

```text
Qt surface
CustomHUD upload
renderer selection transaction
physical framebuffer publication
```

Sapphire algorithmへ症状別の例外を追加せず、Sapphireと同じstate lifetimeを再現すること。

---

# 40. 実装進捗（S66）

| Phase | Commit | Status | Notes |
|---|---|---|---|
| S66-1 | `5c38bc8a3` | done | OuterRendererAction KeepCurrent on Vulkan install |
| S66-2 | `3030de3d8` | done | SapphireGpu2DState GPU owner |
| S66-3 | `092e2db32` | done | GPU2D adapter event forwarding |
| S66-4 | `eccd496d0` | done | Physical top/bottom framebuffer + publication |
| S66-5 | `cccab318a` | done | HUD staging timeline completion |
| S66-6 | `cccab318a` | done | HUD pipeline timeline retire (same commit as S66-5) |
| S66-7 | `685f290c7` | done | Non-blocking pending swapchain + bounded detach |
| S66-8 | `6b4fe9b31` | done | lifecycle parity CI tests + plan §40 |

