# Vulkan ROM起動後スプラッシュ固定
## Sapphire 0.7.0.rc4 準拠監査・再移植指示書

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査時HEAD:** `e35c92408fb9d3b93af0cfe31a52e661d7d62a0a`  
**HEADメッセージ:** `Fixing vulkan`  
**参照frontend:** `SapphireRhodonite/melonDS-android` tag `0.7.0.rc4`  
**参照core:** `SapphireRhodonite/melonDS-android-lib` commit `d77944275fa61f9b79cfcead2c3e98993429a023`  
**症状:** VulkanでROMを開けるようになったが、ゲーム画面へ切り替わらず、no-ROM splashが表示されたままになる。

---

# 0. 結論

前回の即時Segmentation Faultを起こしていた次の2点は、最新pushで正しく修正されている。

```text
GPU_Soft.cpp:
Vulkan packed framebufferのscanline offset二重加算

GPU_Soft.cpp:
GPU.FrontBufferとphysical Framebuffer indexの不一致
```

したがって、今回のスプラッシュ固定は前回のheap corruptionとは別問題である。

今回の最重要監査結果は次のとおり。

```text
現行MelonPrimeDS:
queued Frameのserial／generation
==
GUI present時点のlive VulkanRenderer3D frame viewのserial／generation

を必須条件にしている。
```

しかしproducer threadとGUI threadは非同期である。

producerがserial NのFrameをqueueへ入れた後、
GUI callbackが実行されるまでにrendererがserial N+1以降へ進むと、
現行`VulkanOutput::buildCompositionInputs()`は必ずfalseを返す。

その結果:

```text
acquirePresentFrame()
↓
buildCompositionInputs() == false
↓
presentAcquiredFrame() == false
↓
deferPresentedFrame()
↓
commitPresentedFrame()が一度も呼ばれない
↓
lastPresentedSerial == 0のまま
↓
hasPresentedFrame() == false
↓
splashを表示し続ける
```

となる。

これは単なる推測ではなく、現行コードに存在する明示的な失敗条件である。

一方、Sapphire `0.7.0.rc4`の`buildCompositionInputs()`には、
queued Frameと現在のlive rendererのserial／generation一致条件は存在しない。

Sapphireは:

```text
Frameに紐づくFrameResource
↓
FrameResourceに保存済みのrenderer3dSnapshot
↓
そのsnapshotをcomposition sourceとして使用
```

する。

つまり、現行MelonPrimeDSへ追加された:

```text
Vulkan3DFrameViewをGUI present時に再取得する処理
serial／generationの厳密一致判定
```

がSapphireとの差分であり、スプラッシュ固定を直接成立させている。

## 修正方針

```text
Vulkan3DFrameViewをfrontend全体へ広げない
producer内でFrameResourceへsnapshot copyするためだけに使用する
GUI presenterはlive renderer frameを参照しない
SapphireのVulkanOutput APIと関数本体へ戻す
Qt固有処理はsurface host／GUI dispatch／overlay adapterだけに限定する
```

---

# 1. 最新pushで正しく直っている箇所

## 1.1 packed framebufferのrow pointer

最新コードでは`WriteAcceleratedPackedRow()`が、
呼出側から渡された対象行pointerへ直接書くようになっている。

正しい形:

```cpp
std::memcpy(
    dstRow,
    StructuredPlane0[engine],
    kStructuredScreenWidth * sizeof(u32));

std::memcpy(
    dstRow + kStructuredScreenWidth,
    StructuredPlane1[engine],
    kStructuredScreenWidth * sizeof(u32));

std::memcpy(
    dstRow + (kStructuredScreenWidth * 2u),
    StructuredControl[engine],
    kStructuredScreenWidth * sizeof(u32));

dstRow[kPackedStride - 1u] = meta;
```

`line * kPackedStride`の再加算は削除済み。

この修正は維持する。

## 1.2 FrontBuffer mapping

最新コードではphysical buffer indexを維持している。

```cpp
GPU.FrontBuffer = BackBuffer ^ 1;

GPU.Framebuffer[0][0] = Framebuffer[0][0];
GPU.Framebuffer[0][1] = Framebuffer[0][1];
GPU.Framebuffer[1][0] = Framebuffer[1][0];
GPU.Framebuffer[1][1] = Framebuffer[1][1];
```

Sapphire latchの:

```cpp
GPU.Framebuffer[GPU.FrontBuffer][screen]
```

と整合する。

この修正も維持する。

---

# 2. スプラッシュが消える条件

対象:

```text
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
ScreenPanelVulkan::syncNoRomSplashOverlay()
```

現行判定:

```cpp
const bool hasPresentedGameFrame =
    emuInstance->vulkanFrontendSession().hasPresentedFrame();

const bool showSplash =
    !emuThread
    || !emuThread->emuIsActive()
    || !hasPresentedGameFrame;
```

これは基本的に正しい。

ROMがactiveでも、swapchainへゲームframeを1枚も正常presentできていない間は
splashを残すべきである。

`hasPresentedFrame()`は:

```cpp
return initialized
    && !producerSuspended
    && activePresenter != nullptr
    && lastPresentedSerial != 0;
```

である。

`lastPresentedSerial`を設定するのは:

```cpp
commitPresentedFrame()
```

のみ。

したがって現在の症状は:

```text
ゲームframeのcommitが一度も成立していない
```

ことを意味する。

## 禁止する誤修正

次のようにsplash条件だけを緩めてはいけない。

```cpp
showSplash = !emuThread || !emuThread->emuIsActive();
```

または:

```cpp
showSplash = !session.hasCompositedFrame();
```

この変更はゲームframeをpresentできていない問題を隠し、
黒画面／白画面／未初期化swapchainを露出させるだけである。

---

# 3. Critical: GUI present時のlive frame一致要求

対象:

```text
src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp
MelonPrimeVulkanFrontendSession::presentAcquiredFrame()
```

現行処理:

```cpp
VulkanRenderer3D* renderer3D = ...;

const Vulkan3DFrameView frameView =
    renderer3D->GetVulkan3DFrameView();

VulkanCompositionInputs inputs{};

if (!output.buildCompositionInputs(
        frame,
        frameView,
        scale,
        VulkanFilterMode::Nearest,
        false,
        false,
        false,
        inputs))
{
    return false;
}
```

ここで取得している`frameView`は:

```text
queueから取得したFrameをproducerが作成した時点のview
```

ではなく:

```text
GUI callbackが実行された瞬間のlive renderer view
```

である。

producerとGUIは別threadであり、同期していない。

例:

```text
EmuThread:
Frame Aをserial 100でprepare
Frame Aをqueueへpush
次のRunFrameを開始
renderer serial 101へ進む

GUI thread:
Frame Aをacquire
live frameView serial 101を取得
```

この状態で現行`buildCompositionInputs()`を呼ぶ。

---

# 4. Critical: 現行buildCompositionInputsの失敗条件

対象:

```text
src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp
VulkanOutput::buildCompositionInputs()
```

現行コードは入口で:

```cpp
if (!initialized
    || frame == nullptr
    || scale < 1
    || frame->frameSerial != frameView.FrameSerial
    || frame->rendererGeneration != frameView.Generation)
{
    return false;
}
```

さらにFrameResourceについて:

```cpp
if (!hasRenderer3dSnapshot
    || resource.renderer3dFrameSerial != frameView.FrameSerial
    || resource.renderer3dGeneration != frameView.Generation)
{
    return false;
}
```

としている。

queued Frame Aが正しく次を保持していても:

```text
frame->frameSerial              = 100
resource.renderer3dFrameSerial  = 100
resource.renderer3dSnapshot     = Frame Aの正しいsnapshot
```

GUI時点のlive viewが101なら:

```text
100 != 101
```

でfalseになる。

FrameResourceにはすでにFrame A専用のimmutable snapshotがあるため、
live viewとの一致確認は不要であり、むしろ非同期presentationを破壊する。

---

# 5. Sapphire 0.7.0.rc4との正確な差分

Sapphireのsignature:

```cpp
bool buildCompositionInputs(
    const Frame* frame,
    const melonDS::VulkanRenderer3D& renderer3D,
    int scale,
    VulkanFilterMode filtering,
    bool needsReadback,
    bool multiSurface,
    bool validationMode,
    VulkanCompositionInputs& outInputs) const;
```

Sapphireの入口:

```cpp
if (!initialized || frame == nullptr || scale < 1)
    return false;

auto iterator = resources.find(const_cast<Frame*>(frame));
if (iterator == resources.end())
    return false;

const FrameResource& resource = iterator->second;
if (!resource.hasPreparedInputs)
    return false;
```

Sapphireは次を優先する。

```cpp
const bool hasRenderer3dSnapshot =
    resource.hasRenderer3dSnapshot
    && resource.renderer3dSnapshot != VK_NULL_HANDLE
    && resource.renderer3dSnapshotView != VK_NULL_HANDLE;

if (hasRenderer3dSnapshot)
{
    outInputs.sourceImage =
        resource.renderer3dSnapshot;
    outInputs.sourceImageView =
        resource.renderer3dSnapshotView;
    outInputs.rendererWidth =
        resource.snapshotWidth;
    outInputs.rendererHeight =
        resource.snapshotHeight;
}
else
{
    // snapshotがない場合だけlive color targetへfallback
    outInputs.sourceImage =
        renderer3D.GetColorTargetImage();
    ...
}
```

重要なのは:

```text
Sapphireには
frame serial == current live renderer serial
という条件がない。
```

FrameResourceにsnapshotがあれば、そのFrameResourceだけでcomposition inputsを作る。

## 判定

| 領域 | 現行MelonPrime | Sapphire 0.7.0.rc4 | 判定 |
|---|---|---|---|
| FrameをRunFrame前に取得 | 実装済み | 実装 | 一致に近い |
| frame resource準備 | 実装済み | 実装 | 一致に近い |
| RunFrame後のfront buffer latch | 実装済み | 実装 | 一致に近い |
| valid frameのみqueueへpush | 実装済み | 実装 | 一致に近い |
| FrameResource内3D snapshot | 実装済み | 実装 | 一致 |
| GUI時にlive frame view再取得 | あり | serial比較なし | 不一致 |
| queued／live serial一致必須 | あり | なし | 重大な不一致 |
| queued／live generation一致必須 | あり | なし | 重大な不一致 |
| snapshot優先composition | 条件付きだがlive一致必須 | snapshot優先 | 不一致 |
| no-ROM splash | Qt overlay | Android UI | 意図的差分 |
| native surface | Qt／Win32／X11等 | ANativeWindow | 意図的差分 |
| HUD overlay | desktop追加 | 別UI層 | 意図的差分 |

結論:

```text
producer orderingは以前よりSapphireへ近づいたが、
presentation ownershipはSapphireと同じではない。
```

---

# 6. 最優先修正 S59-1
## SapphireのVulkanOutput public APIへ戻す

現行のpublic APIから`Vulkan3DFrameView`引数を外す。

## 現行

```cpp
bool captureRenderer3dSnapshot(
    Frame* frame,
    const Vulkan3DFrameView& frameView,
    bool snapshotScreenSwap);

bool prepareFrameForPresentation(
    Frame* frame,
    const GPU& gpu,
    int frontBuffer,
    bool frameScreenSwap,
    SoftPackedFrameSnapshot& snapshot,
    VulkanRenderer3D& renderer3D,
    const Vulkan3DFrameView& frameView);

bool buildCompositionInputs(
    const Frame* frame,
    const Vulkan3DFrameView& frameView,
    int scale,
    ...);
```

## Sapphire準拠

```cpp
bool captureRenderer3dSnapshot(
    Frame* frame,
    const VulkanRenderer3D& renderer3D,
    bool snapshotScreenSwap);

bool prepareFrameForPresentation(
    Frame* frame,
    const GPU& gpu,
    int frontBuffer,
    bool frameScreenSwap,
    SoftPackedFrameSnapshot& snapshot,
    VulkanRenderer3D& renderer3D);

bool buildCompositionInputs(
    const Frame* frame,
    const VulkanRenderer3D& renderer3D,
    int scale,
    VulkanFilterMode filtering,
    bool needsReadback,
    bool multiSurface,
    bool validationMode,
    VulkanCompositionInputs& outInputs) const;
```

## 目的

`Vulkan3DFrameView`を:

```text
frontend session
GUI presenter
surface layer
```

へ漏らさない。

必要なsnapshot取得は`VulkanOutput`内部に閉じ込める。

---

# 7. 最優先修正 S59-2
## SapphireのbuildCompositionInputs本体を戻す

`VulkanOutput::buildCompositionInputs()`は、
Sapphire `0.7.0.rc4`の同関数を原則そのまま持ってくる。

削除する条件:

```cpp
frame->frameSerial != frameView.FrameSerial

frame->rendererGeneration != frameView.Generation

resource.renderer3dFrameSerial != frameView.FrameSerial

resource.renderer3dGeneration != frameView.Generation
```

FrameResourceのserial／generation field自体は、
diagnosticとproducer-side assert用に残してよい。

ただしGUI presentationのacceptance gateには使わない。

## 修正後の要点

```cpp
bool VulkanOutput::buildCompositionInputs(
    const Frame* frame,
    const VulkanRenderer3D& renderer3D,
    int scale,
    VulkanFilterMode filtering,
    bool needsReadback,
    bool multiSurface,
    bool validationMode,
    VulkanCompositionInputs& outInputs) const
{
    if (!initialized || frame == nullptr || scale < 1)
        return false;

    auto iterator =
        resources.find(const_cast<Frame*>(frame));

    if (iterator == resources.end())
        return false;

    const FrameResource& resource =
        iterator->second;

    if (!resource.hasPreparedInputs)
        return false;

    const bool hasRenderer3dSnapshot =
        resource.hasRenderer3dSnapshot
        && resource.renderer3dSnapshot != VK_NULL_HANDLE
        && resource.renderer3dSnapshotView != VK_NULL_HANDLE;

    if (!hasRenderer3dSnapshot
        && !renderer3D.HasColorTarget())
    {
        return false;
    }

    if (hasRenderer3dSnapshot)
    {
        outInputs.sourceImage =
            resource.renderer3dSnapshot;
        outInputs.sourceImageView =
            resource.renderer3dSnapshotView;
        outInputs.rendererWidth =
            resource.snapshotWidth;
        outInputs.rendererHeight =
            resource.snapshotHeight;
    }
    else
    {
        outInputs.sourceImage =
            renderer3D.GetColorTargetImage();
        outInputs.sourceImageView =
            renderer3D.GetColorTargetImageView();
        outInputs.rendererWidth =
            renderer3D.GetColorTargetWidth();
        outInputs.rendererHeight =
            renderer3D.GetColorTargetHeight();
    }

    // 以下はSapphire本体を維持
    ...
}
```

---

# 8. 最優先修正 S59-3
## presenterでlive frame viewを取得しない

現行:

```cpp
const Vulkan3DFrameView frameView =
    renderer3D->GetVulkan3DFrameView();

if (!output.buildCompositionInputs(
        frame,
        frameView,
        scale,
        ...,
        inputs))
{
    return false;
}
```

修正:

```cpp
if (!output.buildCompositionInputs(
        frame,
        *renderer3D,
        scale,
        VulkanFilterMode::Nearest,
        false,
        false,
        false,
        inputs))
{
    return false;
}
```

ここで`renderer3D`はSapphire互換fallback APIのために渡すだけ。

正常なqueued frameでは:

```text
resource.hasRenderer3dSnapshot == true
```

であるため、GUI側でlive color targetを使ってはいけない。

## 推奨assert

producerがqueueへpushする直前にのみ:

```cpp
assert(frame->frameSerial != 0);
assert(frame->rendererGeneration != 0);
assert(output.preparedSnapshotMatchesFrame(frame));
```

を確認する。

GUI側ではlive rendererとの一致を確認しない。

---

# 9. 最優先修正 S59-4
## Sapphire互換getterをVulkanRenderer3Dへ追加

現行rendererは`GetVulkan3DFrameView()`中心のAPIへ独自変更されている。

Sapphireの`VulkanOutput.cpp`を直接持ってくるには、
次の互換APIを復元する。

```cpp
[[nodiscard]]
bool HasColorTarget() const noexcept;

[[nodiscard]]
bool IsColorTargetInitialized() const noexcept;

[[nodiscard]]
VkImage GetColorTargetImage() const noexcept;

[[nodiscard]]
VkImageView GetColorTargetImageView() const noexcept;

[[nodiscard]]
u32 GetColorTargetWidth() const noexcept;

[[nodiscard]]
u32 GetColorTargetHeight() const noexcept;
```

これらはSapphire frontendを改造せずに使うためのcompatibility layerとする。

## 注意

各getterを毎回`GetVulkan3DFrameView()`で別々に再取得すると、
同じ呼出中に状態が変化する可能性がある。

可能ならrenderer内部fieldを直接返すSapphire互換実装にする。

少なくとも:

```text
producer thread上
renderer state lock下
```

でのみfallback sourceを読む。

---

# 10. Producer側のsnapshot取得

SapphireではproducerがFrameをRunFrame前に確保し、
RunFrame後にFrameResourceへ必要な内容を固定する。

現行のorderingは概ね正しい。

```text
beginProducerFrame()
    Frame取得
    ensureFrameResources()
    必要ならpre-run 3D snapshot

NDS::RunFrame()

completeProducerFrame()
    completed front buffer取得
    soft packed latch
    renderer3D snapshot copy
    prepareFrameForPresentation()
    queue push
```

維持すべき点:

```text
FrameのownershipをRunFrame前に確定
FrameResourceをFrame pointerに紐づける
RunFrame完了時点の3D sourceをsnapshotへcopy
snapshot完了後だけqueueへpush
```

変更すべき点:

```text
completeProducerFrame()で得たframe viewを
FrameResourceへcopyした後、
そのviewをGUI側まで持ち回らない。
```

---

# 11. Sapphireからそのまま持ってくる範囲

車輪の再発明を避けるため、
以下を「アルゴリズム再記述」ではなく「upstream source import」として扱う。

## 11.1 そのまま移植する優先度が高いもの

### frontend

```text
SapphireRhodonite/melonDS-android
tag 0.7.0.rc4

app/src/main/cpp/renderer/FrameQueue.h
app/src/main/cpp/renderer/FrameQueue.cpp

app/src/main/cpp/renderer/VulkanOutput.h
app/src/main/cpp/renderer/VulkanOutput.cpp

app/src/main/cpp/renderer/VulkanSurfacePresenter.h
app/src/main/cpp/renderer/VulkanSurfacePresenter.cpp

app/src/main/cpp/MelonInstance.cpp
内の:
    runFrame() Vulkan branch
    latchSoftPackedFrameSnapshot()
    temporal history helper群
```

### core

```text
SapphireRhodonite/melonDS-android-lib
commit d77944275fa61f9b79cfcead2c3e98993429a023

src/GPU2D_Soft.h
src/GPU2D_Soft.cpp
```

さらにcompile dependencyとして必要な:

```text
GPU2D.h／GPU2D.cpp
GPU.h／GPU.cpp
GPU3D renderer interface
Vulkan renderer accessors
```

の差分をdependency closureとして移植する。

## 11.2 直接コピー後に許容する変更

許容するのは次だけ。

```text
include path
namespace
volk／Vulkan loader
generated shader symbol名
logging adapter
build macro
MELONPRIME_DS gate
device-loss callback
```

## 11.3 desktop adapterとして別実装にする範囲

次はAndroidから直接コピーできない。

```text
ANativeWindow
vkCreateAndroidSurfaceKHR
JNI callback
Android lifecycle
EGL／Java surface ownership
```

これらは:

```text
MelonPrimeVulkanSurfaceHost
ScreenPanelVulkan
```

に限定する。

VulkanOutputのframe ownershipやcomposition判定へ
Qt固有条件を入れない。

---

# 12. 現行GPU2D facadeはSapphire coreと同一ではない

現行:

```text
SapphireGPU2DSoftAccess
    ↓
既存melonDS SoftRendererをOwnerとして呼ぶfacade
```

Sapphire core:

```text
GPU2D::SoftRenderer
    ↓
BG／OBJ合成
display capture
structured plane
capture history
capture line mask
3D source classification
を同じrenderer内で生成
```

Sapphire coreの`GPU2D::SoftRenderer`には:

```text
LastDebugCapture3dSource
CaptureLineUses3d
StructuredVulkan2DPlanes
StructuredVulkan2DCaptureSourceLine
StructuredVulkan2DCapturePlanes
StructuredVulkan2DCaptureLineValid
```

が直接存在する。

現行facadeはAPI表面を近づけているが、
内部ownershipと更新timingは別物である。

## 判定

```text
API名がSapphireと同じ
==
実装がSapphireと同じ
```

ではない。

短期的には今回のsplash固定をfrontend ownership修正で直す。

中期的には:

```text
Sapphire coreのGPU2D::SoftRenderer dependency closure
```

を移植し、
facadeによる再現実装を縮小または削除する。

---

# 13. 冗長なcomplete-frame structured snapshot ring

現行`GPU_Soft`には独自に:

```text
SapphireStructured2DFrameSnapshot
StructuredFrames[2]
StructuredWriteFrame
StructuredPublishedFrame
BeginStructured2DFrame()
SubmitStructured2DLine()
EndStructured2DFrame()
CopyStructured2DFrameSnapshot()
```

がある。

一方、現在のSapphire latch経路は:

```text
GPU.Framebuffer[frontBuffer]
＋
GetStructuredVulkan2DPlane()
＋
GetDebugCapture3dSource()
＋
GetDebugCaptureLineUses3dMask()
```

を直接読む。

このcomplete-frame ringがmain presentation pathで使われていないなら、
同一情報の二重ownershipとなる。

## 指示

repository全体でreferenceを確認する。

使われていなければ削除する。

```text
CopyStructured2DFrameSnapshot
SapphireStructured2DFrameSnapshot
StructuredFrames
StructuredLineReceived
```

を維持し続けない。

Sapphireにない中間representationを追加すると、
次の不一致を生みやすい。

```text
serial timing
screen swap timing
capture timing
front buffer timing
complete判定
巨大copy
```

---

# 14. FrameQueue失敗処理

現行ではpresentation失敗時に:

```cpp
session.deferPresentedFrame(frame);
```

する。

FrameQueueはrealtime modeでfailed candidateをReadyへ戻し、
次回より新しいframeを選べるようにしている。

この設計自体は合理的。

ただし現行のserial mismatchは:

```text
一時的なswapchain timeout
```

ではなく:

```text
構造的なpermanent validation failure
```

である。

frameを変えてもGUIが常にproducerより遅れていれば、
新しいframeでも同じ失敗を繰り返す。

## 修正後の診断拡張

最低限、次のfailure reasonを分離する。

```text
noPresenter
missingRenderer
missingFrameResource
missingPreparedInputs
missingSnapshot
queuedResourceMismatch
surfaceGenerationMismatch
frameWaitTimeout
composeFailure
swapchainAcquireFailure
swapchainPresentFailure
```

boolだけでは原因を特定できない。

ただしSapphire parityを壊す大規模な独自state machineは追加しない。

desktop adapter側だけでenum化する。

---

# 15. 必須trace

## Producer

queueへpushする直前:

```text
[VulkanProducer]
frameId
frameSerial
rendererGeneration
resourceSnapshotSerial
resourceSnapshotGeneration
frontBuffer
screenSwap
hasPreparedInputs
hasRenderer3dSnapshot
queuePush
```

## Presenter

```text
[VulkanPresent]
frameId
queuedSerial
queuedGeneration
resourceSerial
resourceGeneration
liveSerial
liveGeneration
resourceFound
prepared
buildInputs
compose
surfacePresent
commit
```

live serial／generationは診断表示だけにする。

acceptance条件へ使わない。

## 期待される確認

修正前:

```text
queuedSerial=100
resourceSerial=100
liveSerial=101
buildInputs=0
commit=0
```

修正後:

```text
queuedSerial=100
resourceSerial=100
liveSerial=101
buildInputs=1
surfacePresent=1
commit=1
```

---

# 16. Splash判定の維持

`syncNoRomSplashOverlay()`は次を維持する。

```text
emu inactive
または
first successful present前

ならsplash表示
```

splashを隠すeventは:

```text
commitPresentedFrame()
```

に一本化する。

ただしbackend generationが変わった時は:

```text
lastPresentedSerial = 0
lastPresentedFrameId = 0
```

へresetし、
新generationのfirst presentまで再表示する。

これはsurface recreationとrenderer switchの安全性に必要。

---

# 17. Commit分割

## Commit 1

```text
Restore Sapphire VulkanOutput ownership API
```

変更:

```text
VulkanOutput public APIをSapphire signatureへ戻す
GUI側Vulkan3DFrameView引数を削除
```

## Commit 2

```text
Build Vulkan composition from queued FrameResource snapshot
```

変更:

```text
buildCompositionInputsをSapphire本体へ戻す
live serial／generation一致gate削除
```

## Commit 3

```text
Stop querying live Vulkan frame view during GUI presentation
```

変更:

```text
presentAcquiredFrameからGetVulkan3DFrameView()削除
```

## Commit 4

```text
Add Sapphire-compatible Vulkan color target accessors
```

変更:

```text
VulkanRenderer3D互換getter
```

## Commit 5

```text
Remove redundant structured frame snapshot bridge
```

変更:

```text
未使用ringとcopy API削除
```

## Commit 6

```text
Vendor Sapphire GPU2D structured-capture dependency closure
```

変更:

```text
GPU2D_Softをcore commitから移植
facadeの再実装範囲を縮小
```

Commit 1～3を最優先とする。

---

# 18. 検証手順

## T1: first present

VulkanでROMを起動する。

期待:

```text
producer queue push
present acquire
build inputs success
surface present success
commit
splash hide
```

2～3frame以内にcommitすること。

## T2: intentional GUI delay

GUI threadへdebugで100ms delayを入れる。

期待:

```text
live serialがqueued serialより進んでも
queued FrameResource snapshotでpresent成功
```

これが今回の本質的な回帰テスト。

## T3: serial trace

期待:

```text
queuedSerial != liveSerial
```

でも:

```text
buildInputs == 1
```

になること。

## T4: surface resize

ROM起動中に連続resizeする。

期待:

```text
surface generation mismatch frameは破棄／延期
新surface generationでfirst present
splash解除
```

## T5: backend switch

```text
Software
→ Vulkan
→ Software
→ Vulkan
```

を20回。

期待:

```text
stale resourceなし
stale presenterなし
旧generation commitなし
```

## T6: ROM lifecycle

```text
ROM start
ROM stop
別ROM start
reset
```

を繰り返す。

## T7: long run

```text
10分
1x／2x／4x
fullscreen
VSync on／off
fast-forward
```

## T8: memory validation

```text
AddressSanitizer
UndefinedBehaviorSanitizer
Vulkan validation layer
```

前回修正済みのpacked framebuffer overflowが再発していないことも確認する。

---

# 19. 同一性を維持する仕組み

今後また独自実装化しないよう、
repositoryへ次を追加する。

```text
docs/vulkan/SAPPHIRE_UPSTREAM.md
```

内容:

```text
frontend repo
frontend tag
core repo
core commit
各import file
各fileのSHA-256
許容差分
desktop adapter差分
```

例:

```text
VulkanOutput.cpp:
upstream body保持
許容差分:
    include path
    volk
    generated shader names
    logging
禁止差分:
    frame ownership
    temporal state
    composition input validation
```

## 自動diff

CIまたはlocal scriptで:

```text
Sapphire pristine copy
vs
MelonPrime imported core
```

を比較する。

platform adapter marker外の差分が増えたらfailureにする。

例marker:

```cpp
// MELONPRIME_DESKTOP_ADAPTER_BEGIN
...
// MELONPRIME_DESKTOP_ADAPTER_END
```

これにより、どこがSapphireそのままで、
どこがdesktop固有か明確になる。

---

# 20. 禁止事項

今回行ってはいけない修正:

```text
splashをemuActiveだけで隠す
hasCompositedFrameでsplashを隠す
lastSubmittedSerialをpresent成功扱いにする
serial mismatchを無視してlive imageをそのまま使う
GUI threadでlive ColorImageを直接sampleする
CPU framebuffer fallbackを常時追加する
FrameQueue slot数を増やして症状を遅らせる
present timeoutを無限にする
Sapphire latchを再度簡略化する
独自snapshot型をさらに増やす
```

正しい修正は:

```text
queued Frameに紐づいたprepared resourceだけをpresentする
```

ことである。

---

# 21. 完了条件

```text
VulkanでROMが起動する
first present後にsplashが消える
queued serialとlive serialが異なってもpresent可能
GUI presenterがGetVulkan3DFrameView()を呼ばない
buildCompositionInputsにlive serial一致条件がない
FrameResource snapshotがcomposition source
Sapphire buildCompositionInputs本体と意味的に一致
Qt差分がsurface／dispatch adapterへ限定
Software／OpenGLに回帰なし
resize／backend switch／ROM restartでgeneration安全
```

---

# 22. 最終判断

最新pushは前回のmemory corruptionを正しく解消している。

しかしVulkan presentation pathはまだSapphireと同じではない。

特に:

```text
GUI present時にlive Vulkan3DFrameViewを取得
queued Frameとのserial／generation一致を要求
```

する独自変更が、Sapphireのper-frame resource ownershipを壊している。

今回の症状は:

```text
ROMが動いていない
```

というより:

```text
prepared frameをpresent acceptance gateで永久に拒否し、
commitが一度も成立しない
```

経路で説明できる。

最優先でSapphire `0.7.0.rc4`の:

```text
VulkanOutput.h
VulkanOutput.cpp
buildCompositionInputs()
captureRenderer3dSnapshot()
prepareFrameForPresentation()
```

をsignatureごと戻し、
desktop固有差分を外側のadapterへ追い出すこと。

これが最も車輪の再発明を避けられる修正である。
