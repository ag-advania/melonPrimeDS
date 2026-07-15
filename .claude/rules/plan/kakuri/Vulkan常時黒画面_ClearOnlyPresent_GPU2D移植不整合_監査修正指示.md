# Vulkan ROM起動後・常時黒画面
## v61 最新push監査／Sapphire準拠修正指示書

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査対象:** 前回監査基準 `e35c92408fb9d3b93af0cfe31a52e661d7d62a0a` 以降の最新push  
**確認build:** `melonPrimeDS v3.4.3 v61.exe`  
**添付ログ:** `error.txt`  
**参照frontend:** `SapphireRhodonite/melonDS-android` tag `0.7.0.rc4`  
**参照core:** `SapphireRhodonite/melonDS-android-lib` commit `d77944275fa61f9b79cfcead2c3e98993429a023`  
**症状:** VulkanでROMを開くとno-ROM splashは正常に消えるが、ゲーム画面は常時黒。しばらく動作後にhost側Segmentation Faultも発生する。

---

# 0. 結論

前回まで問題だった:

```text
FrameQueueがdrainされない
presenterが登録解除される
queued frameとlive renderer serialが一致せず拒否される
first presentが成立せずsplashが残る
```

という経路は、今回のログでは解消している。

今回のログでは:

```text
producer queue push                    103回
completeProducerTransaction result=1  103回
buildInputs=1                           95回
surfacePresent=1                        95回
commitPresentedFrame相当                95回
```

が成立している。

さらに:

```text
actual renderer:
Software → Vulkan
```

への遷移も成立している。

したがって今回の黒画面は:

```text
Frameがqueueへ来ていない
swapchainへpresentできていない
splash overlayが上に残っている
```

ことが原因ではない。

**黒いswapchain imageを正常presentしている**か、  
**ゲーム画面quadを1個も描かず、render passのblack clearだけを正常presentしている**かのどちらかである。

現行コードには、この2つを「成功」と誤判定できる独立した欠陥がある。

## P0-1: clear-only presentをゲームframe成功としてcommitできる

`ScreenPanelVulkan::configureSurface()`は:

```text
numScreens == 0
matrixが未初期化／退化
enabled game screen transformが0個
```

でも`VulkanSurfacePresenter::configureSurface()`成功扱いになり得る。

presenterはgame screen draw callが0個でもrender passをblackでclearし、
`vkQueuePresentKHR`が成功すれば`presentFrame()`をtrueで返せる。

すると:

```text
surfacePresent=1
commit
hasPresentedFrame=true
splash hide
画面はblack clearのみ
```

になる。

今回のログはこの経路と完全に両立する。

## P0-2: GPU2D Sapphire parityが実際には閉じていない

`docs/vulkan/SAPPHIRE_UPSTREAM.md`は:

```text
GPU2D::SoftRenderer capture metadata parity:
closed S59-6
```

としている。

しかし実コードはSapphire coreの`GPU2D::SoftRenderer`をそのままvendorしていない。

現在は:

```text
melonDS旧SoftRenderer
＋
旧SoftRenderer2D
＋
SapphireGPU2DStructuredVulkan.cpp
＋
SapphireGPU2DSoftAccess facade
```

というhybridである。

さらに`SapphireGPU2DStructuredVulkan.cpp`は、
Sapphire sourceの一部行をPythonで抽出して文字列置換したものになっている。

代表例:

```text
CurUnit->             → CaptureUnit().
CurUnit == nullptr    → false
_3DLine               → Capture3DLine
BGOBJLine             → CaptureBGOBJLine
```

これはSapphireと同じ実装ではない。

現在のVulkan raw framebufferは、
このhybrid structured planeをそのままpacked rowへ書く。

したがってstructured plane／control／line metaが空または不整合なら:

```text
有効なFrameResource
有効なVkImage
有効なswapchain present
```

であっても、表示内容は全面黒になる。

## P0-3: host Segmentation Faultが残っている

ログ終端ではframe 103までproducer成功後に:

```text
Segmentation fault
```

が発生する。

その前に:

```text
ARM9 data abort    4回
ARM9 prefetch abort 1回
```

も発生している。

guest ARM9 exceptionとhost Segmentation Faultの因果関係は未確定だが、
少なくとも:

```text
黒画面だけ直せば完了
```

ではない。

ASan／UBSan／Vulkan validationでhost memory safetyを確定するまで、
このVulkan pathを完成扱いにしてはいけない。

---

# 1. ログから確定していること

## 1.1 Vulkan初期化は成功

```text
VulkanContext ready
Vulkan Renderer3D initialized
VulkanOutput: sync path initialized
```

present queueとgraphics queueも同一familyで成立している。

```text
graphicsFamily=0
presentFamily=0
separate=0
```

## 1.2 frontend session／presenter登録は成功

```text
register presenter=...
active=...
Vulkan frontend session attached to desktop surface generation 1
```

ROM起動後はoverlay updateも成功へ変わっている。

```text
overlay result=1
```

## 1.3 producerは継続して成功

最初のframe:

```text
[VulkanProducer]
valid=1
serial=1
generation=3
frontBuffer=0
screenSwap=1

queuePush frameId=1
completeProducerTransaction result=1
```

その後もframe 103まで成功している。

したがって:

```text
FrameQueue slotが枯渇している
producer transactionが失敗している
```

わけではない。

## 1.4 buildCompositionInputsは成功

```text
[VulkanPresent]
frameId=...
buildInputs=1
```

queued serialとlive serialが異なるframe 1再利用時も:

```text
queuedSerial=1
liveSerial=3
buildInputs=1
```

となっている。

前回のserial一致gate問題は修正されている。

## 1.5 surface presentとcommitは成功

```text
surfacePresent=1
present result=1
commit
```

が継続している。

よって今回の問題は:

```text
presenter呼出しまで届かない
```

ではなく:

```text
presenterが何を描いたか
compositor outputに何が入っているか
```

である。

---

# 2. Critical S61-1
## clear-only presentを成功扱いしない

対象:

```text
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h
```

---

## 2.1 現在のdesktop surface config

現在の`ScreenPanelVulkan::configureSurface()`は:

```cpp
VulkanSurfaceConfig config{};

config.screenTransformCount =
    std::clamp(numScreens, 0, capacity);

for (...)
{
    destination.enabled = true;
    destination.topScreen = screenKind[index] == 0;
    destination.matrix[...] =
        screenMatrix[index][...] * dpr;
}

presenter->configureSurface(surfaceId, config, {});
```

としている。

ここには次のvalidationがない。

```text
numScreens > 0
topまたはbottomのgame screenが1つ以上ある
matrix全要素がfinite
matrixが非退化
変換後width／heightが正
screen quadがsurface内またはsurface近傍にある
```

`ScreenPanel`のmember初期値は:

```cpp
int numScreens = 0;
```

である。

Vulkan panel生成直後に有効layoutが構築される前に
surface configureが走れば、screen transform 0個のconfigを渡せる。

---

## 2.2 presenterはdraw call 0でもpresentできる

`VulkanSurfacePresenter::updateVertexBuffer()`は:

```text
screenTransformCount > 0:
    enabled transformだけappend

screenTransformCount == 0:
    legacy top／bottom rectだけappend
```

する。

しかしdefault `VulkanSurfaceConfig`のlegacy rectはdisabledである。

つまり:

```text
screenTransformCount == 0
legacy rect enabled == false
```

ならgame screen draw callは0個になる。

その後も:

```text
render pass begin
black clear
draw call loopは0回
render pass end
queue submit
queue present
```

は成立できる。

`vkQueuePresentKHR`が成功すると、
現在はgame frame成功としてtrueを返す。

---

## 2.3 修正方針

### A. 初回surface configure前にlayoutを確定

`ScreenPanelVulkan::initVulkan()`で、
native surfaceをattachする前にGUI thread上で:

```cpp
setupScreenLayout();
```

を明示的に実行する。

ただし`setupScreenLayout()`は`layoutGeneration`をincrementするため、
毎present時に呼ばない。

推奨順序:

```cpp
bool ScreenPanelVulkan::initVulkan()
{
    assert(QThread::currentThread() == thread());

    presenter = std::make_unique<VulkanSurfacePresenter>();
    if (!presenter->init())
        return false;

    setupScreenLayout();

    if (!hasValidGameScreenLayout())
    {
        log...
        presenter->shutdown();
        presenter.reset();
        return false;
    }

    if (!ensureNativeSurface())
        ...
}
```

### B. layout validationを追加

```cpp
bool ScreenPanelVulkan::hasValidGameScreenLayout() const noexcept
{
    if (numScreens <= 0
        || numScreens > kMaxScreenTransforms)
    {
        return false;
    }

    bool hasTopOrBottom = false;

    for (int i = 0; i < numScreens; ++i)
    {
        const float* m = screenMatrix[i];

        for (int c = 0; c < 6; ++c)
        {
            if (!std::isfinite(m[c]))
                return false;
        }

        const float transformedWidth =
            std::hypot(m[0] * 256.0f, m[1] * 256.0f);

        const float transformedHeight =
            std::hypot(m[2] * 192.0f, m[3] * 192.0f);

        const float determinant =
            m[0] * m[3] - m[1] * m[2];

        if (transformedWidth <= 0.5f
            || transformedHeight <= 0.5f
            || std::fabs(determinant) <= 1.0e-8f)
        {
            return false;
        }

        if (screenKind[i] == 0 || screenKind[i] == 1)
            hasTopOrBottom = true;
    }

    return hasTopOrBottom;
}
```

### C. presenter coreでもreject

desktop adapterだけのvalidationに依存しない。

```cpp
static bool HasDrawableGameScreen(
    const VulkanSurfaceConfig& config) noexcept;
```

をpresenterへ追加する。

条件:

```text
enabledかつnon-degenerateなscreen transformが1個以上
または
enabled／positive-size／positive-alphaなlegacy screen rectが1個以上
```

これを満たさなければ:

```cpp
configureSurface(...)
{
    if (!HasDrawableGameScreen(config))
        return false;
}
```

とする。

### D. game draw call数を保持

`SurfaceState`へ:

```cpp
u32 cachedGameScreenDrawCallCount = 0;
u32 cachedOverlayDrawCallCount = 0;
```

を追加する。

`updateVertexBuffer()`で画面quadとoverlay quadを別集計する。

### E. clear-only presentは失敗扱い

`presentFrame()`で:

```cpp
if (surfaceState.cachedGameScreenDrawCallCount == 0)
{
    pacingStats.NoGameScreenDrawCalls++;
    return false;
}
```

とする。

overlayだけ描けてもgame frame成功にはしない。

---

# 3. Critical S61-2
## present結果をboolから意味付き結果へ変更する

現在:

```cpp
bool VulkanSurfacePresenter::presentFrame(...);
```

では:

```text
game imageを描いた
black clearだけpresentした
overlayだけ描いた
swapchain presentした
```

を区別できない。

## 推奨enum

```cpp
enum class VulkanPresentResult : u8
{
    PresentedGameFrame,
    NoDrawableGameScreen,
    InvalidFrameInputs,
    FrameWaitFailed,
    ComposeFailed,
    SwapchainUnavailable,
    AcquireFailed,
    RecordFailed,
    SubmitFailed,
    QueuePresentFailed,
};
```

`MelonPrimeVulkanFrontendSession::presentAcquiredFrame()`も同enumを返す。

`ScreenPanelVulkan::presentOnGuiThread()`は:

```cpp
const auto result = session.presentAcquiredFrame(...);

if (result == VulkanPresentResult::PresentedGameFrame)
{
    session.commitPresentedFrame(frame);
}
else
{
    session.deferPresentedFrame(frame);
}
```

とする。

これにより:

```text
swapchainへblack clearが出ただけ
```

でsplashを隠すことを防げる。

---

# 4. 必須trace S61-3
## screen geometryをログへ出す

現在のログには:

```text
surfacePresent=1
```

しかなく、game quadが存在したか判断できない。

surface configure時に必ず次を出す。

```text
[VulkanSurfaceConfig]
surfaceId
surfaceWidth
surfaceHeight
dpr
layoutGeneration
numScreens
screenTransformCount
validGameScreens
```

各transform:

```text
index
enabled
topScreen
matrix=[m0,m1,m2,m3,m4,m5]
mappedBounds=[left,top,right,bottom]
determinant
finite
nonDegenerate
```

present時:

```text
[VulkanSurfaceDraw]
frameId
gameDrawCalls
overlayDrawCalls
vertices
directPresent
compositePresent
swapchainImageIndex
```

期待:

```text
gameDrawCalls >= 1
```

これが0なら、今回の黒画面はgeometry pathで確定する。

---

# 5. Critical S61-4
## GPU2D Sapphire parity statusを「未完了」へ戻す

対象:

```text
docs/vulkan/SAPPHIRE_UPSTREAM.md
tools/extract_sapphire_gpu2d_structured.py
src/SapphireGPU2DStructuredVulkan.cpp
src/SapphireGPU2DSoftAccess.cpp
src/GPU_Soft.cpp
src/GPU_Soft.h
src/GPU2D_Soft.cpp
src/GPU2D_Soft.h
```

現在のparity tracker:

```text
GPU2D::SoftRenderer capture metadata
closed S59-6

Full vendor of GPU2D_Soft.cpp/.h
ported
```

は実態と一致しない。

## 実際の構造

現在:

```text
SoftRenderer
  ├─ SoftRenderer2D Renderer2D[2]
  ├─ StructuredPlane0／1／Control
  ├─ StructuredVulkan2DPlanes
  ├─ SapphireGPU2DStructuredVulkan.cppから抽出したhelper
  └─ SapphireGPU2DSoftAccess facade
```

Sapphire core:

```text
GPU2D::SoftRenderer
  ├─ CurUnit
  ├─ BGOBJLine
  ├─ _3DLine
  ├─ structured planes
  ├─ display capture state
  ├─ capture line mask
  └─ scanline／capture ownership
```

Sapphireでは同じrenderer object内で:

```text
scanline source
unit selection
BG／OBJ
3D line
capture source
structured metadata
VRAM capture
```

が更新される。

MelonPrime hybridではこれらが複数objectへ分散している。

---

# 6. 自動抽出scriptの問題

`tools/extract_sapphire_gpu2d_structured.py`は、
upstream sourceの特定line rangeを切り出し、文字列置換している。

例:

```python
text.replace("CurUnit->", "CaptureUnit().")
text.replace("CurUnit == nullptr", "false")
text.replace("_3DLine", "Capture3DLine")
text.replace("BGOBJLine", "CaptureBGOBJLine")
```

この方式ではC++の意味を保持できない。

## 問題点

### 6.1 dynamic unit ownershipを失う

Sapphireの`CurUnit`はscanline処理中のcurrent 2D unitを表す。

現行`CaptureUnit()`は固定的にGPU2D_Aを返す。

DS display capture自体は主にengine Aで行われるが、
それだけで次が同一になるとは限らない。

```text
structured source screen ownership
screen swap
unit-local blend registers
unit-local display mode
current line target
```

### 6.2 null状態を消している

```cpp
CurUnit == nullptr
```

を:

```cpp
false
```

へ置換すると、
upstreamのlifecycle／guard semanticsを消す。

### 6.3 member ownershipを分断

```text
_3DLine
BGOBJLine
structured capture state
```

が別object／一時pointerへ分散する。

### 6.4 upstream更新に弱い

line numberが変わっても、
scriptが別functionの一部を抽出してbuildできる可能性がある。

compile成功はsemantic parityを保証しない。

---

# 7. 修正 S61-5
## Sapphire GPU2D coreをdependency closureでvendorする

車輪の再発明を避けるため、
`GPU2D_Soft.cpp`の一部を再構成しない。

次をSapphire core pinからdependency closureとして移植する。

```text
src/GPU2D_Soft.h
src/GPU2D_Soft.cpp
```

そのcompileに必要な:

```text
GPU2D renderer interface
GPU renderer ownership
GPU2D Unit access
display capture interfaces
structured Vulkan debug hooks
```

も同じcommitから追う。

## 移植原則

許容差分:

```text
include path
namespace integration
MELONPRIME_DS build gate
debug configuration adapter
logging adapter
desktop build system
```

禁止差分:

```text
scanline ordering
CurUnit ownership
capture source selection
structured plane indexing
screen ownership
display capture mode判定
3D line取得timing
VRAM capture timing
```

## 推奨配置

Sapphire classをrenameして旧classへ接ぎ木するのではなく、
core内で本来の責務を持つrendererとして保持する。

例:

```text
melonDS::GPU2D::SoftRenderer
```

をそのまま使用する。

MelonPrime固有のouter rendererが必要なら、
外側のadapterがSapphire rendererを所有する。

```cpp
class MelonPrimeSoftRendererAdapter final : public Renderer
{
    melonDS::GPU2D::SoftRenderer renderer2D;
};
```

アルゴリズム本体は変更しない。

---

# 8. Critical S61-6
## packed framebufferの内容を構造検証する

現在のVulkan pathでは、
`WriteAcceleratedPackedRow()`が:

```text
StructuredPlane0
StructuredPlane1
StructuredControl
line metadata
```

を直接packed framebufferへ書く。

displayMode 1では通常のnative `DrawScanlineA/B`出力を
表示bufferへ書かず、structured packed rowが唯一の表示sourceになる。

したがってstructured stateが空なら、
queue／compositor／presenterが正常でも全面黒になる。

## 追加するstats

```cpp
struct VulkanPreparedContentStats
{
    u32 topPlane0NonZero = 0;
    u32 topPlane1NonZero = 0;
    u32 topControlNonZero = 0;
    u32 topLineMetaNonZero = 0;

    u32 bottomPlane0NonZero = 0;
    u32 bottomPlane1NonZero = 0;
    u32 bottomControlNonZero = 0;
    u32 bottomLineMetaNonZero = 0;

    u32 topStructured3dSlots = 0;
    u32 bottomStructured3dSlots = 0;

    u32 topDisplayModeCounts[4]{};
    u32 bottomDisplayModeCounts[4]{};

    bool renderer3dSnapshotValid = false;
};
```

producerのqueue push直前に集計する。

## 必須ログ

```text
[VulkanFrameContent]
frameId
frontBuffer
screenSwap
topP0
topP1
topCtl
topMeta
bottomP0
bottomP1
bottomCtl
bottomMeta
top3dSlots
bottom3dSlots
rendererSnapshot
```

## 注意

「全pixelが黒」という理由だけでframeをrejectしてはいけない。

ゲーム上、合法的なblack frameは存在する。

reject対象は構造的に不可能な状態に限定する。

例:

```text
display engine enabled
display modeがblank以外
192 line処理済み
しかしline metadataが全0
controlも全0
renderer snapshotも無効
```

---

# 9. 修正 S61-7
## structured lineをearly return前に初期化する

現在の`SoftRenderer2D::DrawScanline()`は:

```cpp
if (!GPU2D.Enabled)
{
    fill black;
    return;
}

if (forced blank)
{
    fill white;
    return;
}

Parent.BeginStructuredVulkan2DLine(...);
```

の順になっている。

そのためdisabled／forced blank lineでは:

```text
前frame／前lineのstructured stateが残る
または
packed rowのplaneとmetaが一致しない
```

可能性がある。

## 修正

structured modeの場合はfunction冒頭で:

```cpp
Parent.BeginStructuredVulkan2DLine(
    GPU2D.Num,
    line);
```

を実行する。

その後early return pathでも:

```text
structured planesを明示clear
native finalを正しいblank colorで埋める
line metadataをforced blank／disabledとして書く
```

こと。

例:

```cpp
void SoftRenderer2D::DrawScanline(u32 line, u32* dst)
{
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    const bool structured = Parent.UseStructuredVulkan2D();
    if (structured)
        Parent.BeginStructuredVulkan2DLine(GPU2D.Num, line);
#endif

    if (!GPU2D.Enabled)
    {
        ...
#if defined(...)
        if (structured)
            Parent.FinalizeStructuredBlankLine(...);
#endif
        return;
    }

    ...
}
```

ただし最終形はSapphire core full vendorに合わせ、
独自helperを増やし過ぎない。

---

# 10. 修正 S61-8
## raw packed bufferを独立fallbackと誤認しない

現在のframe latchは:

```text
GPU.Framebuffer[frontBuffer]
structured plane getters
capture source
```

を読む。

しかしVulkan accelerated pathのraw framebuffer自体が:

```text
StructuredPlane0／1／Control
```

から作られている。

したがって:

```text
raw packed buffer
vs
structured plane
```

を比較しても、独立した二系統のvalidationにはならない。

同じ壊れたsourceから作られた二つのcopyである可能性がある。

## 診断buildのみのgolden comparison

一時的なdiagnostic modeで:

```text
同じscanlineを通常Software final outputにも生成
structured compositorを1xで生成
pixel単位比較
```

する。

ただしこれはテスト専用。

shipping pathへ:

```text
CPU resampling
Software framebuffer presentation fallback
CPU readback
```

を追加しない。

---

# 11. Compositor出力の切り分け

黒画面が:

```text
geometry 0
packed content 0
renderer3d snapshot 0
compositor shader出力 0
presenter sampling不良
```

のどこかを1回の実行で特定できるようにする。

## Stage A: geometry marker

game screen draw callがある時だけ、
debug buildで画面quadの隅へ1pixel相当の固定色markerを描く。

markerも見えない:

```text
surface geometry／presenter shader／descriptor
```

側。

markerだけ見える:

```text
compositor output content
```

側。

## Stage B: compositor output marker

`VulkanOutput::dispatchCompositor()`のdebug flagで、
output imageを固定magentaへclearしてpresentする。

magentaが見える:

```text
presenter／surfaceは正常
input／compositorが問題
```

magentaも見えない:

```text
surface geometry／presenter sampling
```

側。

## Stage C: direct renderer snapshot

debug専用でcompositorを通さず、
FrameResourceの`renderer3dSnapshot`をpresenterへ直接表示する。

これで:

```text
Vulkan 3D renderer
vs
2D structured compositor
```

を分離する。

これらはdiagnostic switchであり、
shipping fallbackにしない。

---

# 12. ARM9 abortの扱い

ログではframe 13 commit後に:

```text
ARM9: data abort (02007008)
ARM9: data abort (0200710C)
ARM9: data abort (02007114)
ARM9: data abort (02007118)
ARM9: prefetch abort (9A3B0204)
```

が出ている。

## 現時点で断定してはいけないこと

```text
ARM9 abortがVulkan黒画面の原因
FrameQueueがARM9を壊した
Vulkan shaderがguest PCを直接壊した
```

いずれも未証明。

## 必須比較

同じ:

```text
ROM
save
config
boot method
MelonPrime patch
JIT設定
```

で次を実行する。

```text
Software
OpenGL Classic
OpenGL Compute
Vulkan
```

記録:

```text
abort発生frame
abort address
PC
LR
SP
current ROM patch state
renderer
```

Vulkanだけで発生するなら、
host memory corruptionまたはdisplay-capture／VRAM side effectを優先調査する。

全rendererで発生するなら、
ROM patch／save／JITを別件として切り分ける。

---

# 13. Host Segmentation Faultの修正

frame 103後のSegmentation FaultはP0。

## 13.1 ASan／UBSan

Windows clang-clまたはMinGW ASan buildを用意する。

最低限:

```text
-fsanitize=address,undefined
-fno-omit-frame-pointer
-g
```

## 13.2 guard対象

```text
Framebuffer[2][2]
StructuredPlane0
StructuredPlane1
StructuredControl
StructuredNativeFinal
StructuredVulkan2DPlanes
StructuredVulkan2DCaptureSourceLine
StructuredVulkan2DCapturePlanes
StructuredVulkan2DCaptureLineValid
LastDebugCapture3dSource
CaptureLineUses3d
FrameQueue Frame array
VulkanOutput resource map
presenter mapped vertex buffer
```

## 13.3 index assert

```cpp
assert(engine < 2);
assert(line < 192);
assert(x < 256);
assert(plane < 3);
assert(vramBank < 4);
assert(captureAddress < 256 * 192);
assert(frame != nullptr);
assert(resources.contains(frame));
```

## 13.4 vertex buffer bounds

`SurfaceVertex`はoverlay追加で増える。

次をsubmit前に検証:

```text
vertices.size() <= kMaxSurfaceVertexCount
vertices bytes <= surfaceState.vertexBufferSize
drawCall.firstVertex + drawCall.vertexCount <= vertices.size()
```

## 13.5 host crashとguest abortを別trace化

host crash直前に最後に成功した:

```text
producer frameId
present frameId
Frame pointer
FrameResource pointer
frontBuffer
renderer generation
surface generation
```

をring logへ保存する。

---

# 14. Sapphireからそのまま持ってくる範囲

## 14.1 原則そのまま

frontend:

```text
FrameQueue.h／cpp
VulkanOutput.h／cpp
VulkanSurfacePresenter core
latchSoftPackedFrameSnapshot closure
```

core:

```text
GPU2D_Soft.h／cpp
structured capture helper全体
VulkanRenderer3D capture interface
```

## 14.2 desktop固有adapter

独自実装を許容するのは:

```text
VkSurfaceKHR生成
Qt QWidget lifecycle
Win32／X11／Wayland surface
GUI thread dispatch
screen layout → presenter transform変換
HUD／OSD overlay
```

のみ。

## 14.3 禁止

```text
Sapphire functionのline range抽出
blind text replacement
CurUnit固定化
capture ownershipの外出し
upstream functionを複数classへ分割
```

---

# 15. Parity tracker修正

`docs/vulkan/SAPPHIRE_UPSTREAM.md`を次のように変更する。

## 現在

```text
GPU2D parity: closed
```

## 修正

```text
GPU2D parity: OPEN / HIGH RISK

Current implementation:
hybrid SoftRenderer + SoftRenderer2D + generated structured adapter

Required:
vendor Sapphire GPU2D::SoftRenderer dependency closure
```

さらにfileごとに:

```text
upstream SHA-256
local SHA-256
allowed diff block
semantic owner
```

を記録する。

---

# 16. Commit分割

## Commit 1

```text
Reject Vulkan clear-only presentation
```

変更:

```text
game screen draw call count
zero-draw reject
meaning付きpresent result
```

## Commit 2

```text
Validate Vulkan screen transforms before surface configure
```

変更:

```text
initial setupScreenLayout
finite／non-degenerate validation
geometry trace
```

## Commit 3

```text
Add Vulkan prepared-content structural diagnostics
```

変更:

```text
packed plane／control／meta stats
renderer snapshot stats
```

## Commit 4

```text
Reopen Sapphire GPU2D parity gap
```

変更:

```text
SAPPHIRE_UPSTREAM.md訂正
extract scriptをdeprecated化
```

## Commit 5

```text
Vendor Sapphire GPU2D SoftRenderer dependency closure
```

変更:

```text
Sapphire coreからfull vendor
hybrid adapter縮小
```

## Commit 6

```text
Initialize structured blank lines before early returns
```

full vendorで同等処理が入るなら独立commit不要。

## Commit 7

```text
Harden Vulkan structured buffers under sanitizers
```

変更:

```text
bounds assert
ASan／UBSan target
vertex buffer validation
```

---

# 17. 検証手順

## T1: geometry trace

ROM起動直後:

```text
numScreens >= 1
validGameScreens >= 1
gameDrawCalls >= 1
```

を確認。

## T2: forced compositor magenta

debug flagでcompositor outputをmagenta化。

期待:

```text
画面にmagentaが表示
```

表示されなければsurface／geometry／presenter側。

## T3: direct 3D snapshot

FrameResourceの3D snapshotを直接表示。

期待:

```text
3D contentが見える
```

見えればGPU2D structured compositor側。

## T4: content stats

boot後100frameの:

```text
top／bottom plane
control
line meta
3D slot
```

が更新されること。

## T5: no clear-only commit

screen transformをdebugで0にする。

期待:

```text
present result=NoDrawableGameScreen
commitされない
splashまたはerror overlayを維持
```

## T6: Sapphire golden compare

Sapphire Android同一ROMの最初の120frameについて:

```text
display mode counts
comp mode counts
structured slot counts
capture line mask
screen swap
front buffer
```

を比較する。

## T7: backend comparison

```text
Software
OpenGL Classic
OpenGL Compute
Vulkan
```

でROM boot／ARM9 abort有無を比較。

## T8: sanitizer long run

```text
ASan
UBSan
Vulkan validation
10分
resize
fullscreen
backend switch
ROM stop／restart
```

## T9: queue reuse

GUI threadを100ms遅延。

期待:

```text
queuedSerial != liveSerialでもpresent
古いFrameResourceを破壊しない
host crashなし
```

---

# 18. 完了条件

```text
game screen draw callが最低1個ある時だけpresent成功
clear-only swapchain presentでcommitしない
VulkanSurfaceConfigのmatrixがfinite／non-degenerate
compositor magenta diagnosticが表示できる
direct renderer snapshot diagnosticが表示できる
prepared content statsが非空
Sapphire GPU2D parityを実態どおり管理
line-range抽出／blind replacementを廃止
Sapphire GPU2D::SoftRenderer dependency closureをvendor
ARM9 abortのrenderer依存性を切り分け
ASan／UBSanでhost Segmentation Faultなし
1000frame以上連続present
Software／OpenGL回帰なし
```

---

# 19. 禁止事項

```text
黒画面だからsplashを再表示するだけ
surfacePresent=1をそのままgame successとみなす
FrameQueue slot数を増やす
present timeoutを伸ばす
CPU framebuffer fallbackをshipping pathへ追加
CPU readbackを通常表示に使う
compositorを無効化してSoftware画面を拡大
structured planeが空でもcommit
ARM9 abortを無視
host Segmentation Faultを別件として後回し
GPU2D parityをclosedのまま維持
```

---

# 20. 最終判断

今回のpushで:

```text
splash固定
queue starvation
serial mismatch
presenter lifecycle
```

は前進している。

しかし現在のsuccess条件は:

```text
swapchain presentが成功した
```

だけであり:

```text
ゲーム画面quadを描いた
有効なcompositor contentを描いた
```

ことを保証していない。

さらにGPU2D structured pathは、
Sapphire coreのfull vendorではなく、
旧melonDS rendererへ抽出コードを接ぎ木したhybridである。

よって修正の優先順位は:

```text
1. clear-only presentを成功扱いしない
2. screen geometryをhard validate
3. packed／compositor contentを構造計測
4. Sapphire GPU2D coreをdependency closureでvendor
5. ASanで残存Segmentation Faultを除去
```

とする。

queue／splashへ再び手を入れるのではなく、
**「描画対象が存在するか」「FrameResourceに有効内容があるか」**
をsuccess contractへ追加し、
SapphireのGPU2D ownershipをそのまま戻すことが必要である。

---

# 21. 実施進捗 (S61)


| Phase | Commit | Hash | Status |
|---|---|---|---|
| S61-1 | Reject Vulkan clear-only presentation | `4acb3042c` | done |
| S61-2 | Validate Vulkan screen transforms | `0ef3366f8` | done |
| S61-3 | Prepared-content structural diagnostics | `5d77bd108` | done |
| S61-4 | Reopen Sapphire GPU2D parity gap (docs) | `5d7c8d6fe` | done |
| S61-5 | Vendor Sapphire GPU2D SoftRenderer dependency closure | `97c07a86f` | done |
| S61-7 | Structured blank line early-init | `060ecab6e` | done |
| S61-8 | Sanitizer / vertex bounds hardening | `28d91a126` | done |

実装フェーズ（§16 Commit 1–7）は `origin/highres_fonts_v3` へ push 済み。ビルド `build-mingw-vulkan-existing.bat` は全コミット後に成功確認済み。

## 手動検証待ち（§17–18）

| 項目 | 状態 |
|---|---|
| T1 geometry trace (`gameDrawCalls >= 1`) | ROM 起動で要確認 |
| T2 compositor magenta diagnostic | `validationMode` 配線のみ、magenta clear 未実装 |
| T3 direct 3D snapshot diagnostic | 未実装 |
| T4–T6 prepared content / Sapphire parity diff | ログ `[VulkanFrameContent]` で要確認 |
| T7 backend comparison (ARM9 abort) | 未実施 |
| T8 ASan/UBSan long run | `cmake/Sanitizers.cmake` あり、Vulkan 専用ビルド手順未追加 |
| T9 queue reuse delay | 未実施 |

