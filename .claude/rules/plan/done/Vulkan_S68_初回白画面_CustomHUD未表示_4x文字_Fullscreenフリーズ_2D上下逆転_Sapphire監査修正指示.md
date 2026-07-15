# Vulkan S68 監査・修正指示書
## 初回ROM白画面／CustomHUD未表示／4x文字潰れ／fullscreen黒画面・再切替フリーズ／2D異常・上下画面逆転

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `b8d39ab5a56e3479fccb9f26f5ac6c80aec0d6b8`  
**前回監査HEAD:** `aaed0869922961270c4c58e910ec07713ba36ca1`  
**差分:** 9 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 今回の結論

S67で次は改善されている。

```text
- Vulkan-capable buildでもnative Rend2D_A/Bを生成
- Sapphire GPU2D実行をactive Vulkan rendererへ限定
- GPU2D power stateをSapphire Unitへ同期
- VBlank／VBlankEnd forwardingをGPU側へ一本化
- renderer交換前にSapphire framebuffer bindingを無効化
- completed frame publicationをGPU::FinishFrameへ限定
```

しかし、現在もSapphire本家と異なる部分が残っている。

```text
1. Vulkan activation transactionが存在しない
2. native GPU2DとSapphire Unitを二重所有している
3. framebuffer slotを独自に「physical top／bottom」と再定義している
4. CustomHUD upload completionをpresent成功と結び付けている
5. Qt fullscreen transitionをsurface lifecycleへ直接反映している
6. desktop affine transformにpixel-center／integer snap契約がない
```

症状別の最有力原因は次の通り。

| 症状 | 最有力原因 | 重要度 |
|---|---|---:|
| 初回Vulkan ROM起動が白画面 | Renderer3D install後にSapphire Unit完全seed／binding activationを行わない | P0 |
| 他renderer経由ならVulkanが動く | backend切替後の成熟したnative stateを偶然利用している。activation edgeが未定義 | P0 |
| 2D異常 | native GPU2DとSapphire Unitのstate／internal phase不一致 | P0 |
| 上下画面逆転 | Sapphireのframebuffer slot semanticsへ独自physical-owner layerを重ねた二重remap | P0 |
| CustomHUDが出ない | upload slotのtimeline確定が`PresentedGameFrame`成功時だけ。失敗経路でslotが永久占有 | P0 |
| fullscreen数秒黒画面 | resize中にsurface identity確認、swapchain build、fence waitが直列化 | P0 |
| 黒画面中に再切替するとfreeze | fullscreen toggleのcoalescingなし＋recover pathの無期限wait＋pending resource破棄 | P0 |
| 4x文字潰れ | 2D source corruptionを先に除外すべき。残る場合はaffine transformのsubpixel sampling | P1 |

---

# 1. Sapphire parityの正確な判定

## 1.1 そのまま移植されている部分

現在の以下はSapphire 0.7.0.rc4と強く対応している。

```text
SapphireGPU2DCore/Unit.cpp
SapphireGPU2DCore/GPU2D_Soft.cpp
VulkanReference/VulkanOutput.cpp
VulkanReference/VulkanSurfacePresenter.cpp の主要algorithm
FrameQueue
VulkanCompositorShader
VulkanAccumulate3dShader
VulkanSurfacePresenter shader
```

特に`VulkanOutput`内の:

```text
class4判定
regular capture判定
structured handoff
temporal previous source
capture-backed 3D
```

はMelonPrime独自の車輪ではなく、Sapphire側にも存在する。

**これらを「複雑だから」という理由で削除しないこと。**

---

## 1.2 Sapphireと異なる部分

Sapphire本家のcoreは:

```text
GPU owns canonical GPU2D_A
GPU owns canonical GPU2D_B
GPU owns one GPU2D::SoftRenderer
```

であり、`Unit`が正規GPU2D stateである。

現在のMelonPrimeDSは:

```text
native melonDS::GPU2D_A/B
+
SapphireGpu2DState::UnitA/B
```

を同時に保持する。

したがって、現在は:

```text
Sapphire pixel algorithm parity
```

は高いが、

```text
Sapphire state ownership parity
Sapphire activation lifecycle parity
```

は未達である。

---

# 2. S67反映状況

## 2.1 native Software 2D復元

現在の`SoftRenderer`はVulkan buildでも:

```cpp
Rend2D_A = std::make_unique<SoftRenderer2D>(...);
Rend2D_B = std::make_unique<SoftRenderer2D>(...);
Rend3D   = std::make_unique<SoftRenderer3D>(...);
```

を生成する。

`DrawScanline()`／`DrawSprites()`も:

```text
active Sapphire Vulkan
    → Sapphire GPU2D renderer

それ以外
    → native melonDS SoftRenderer2D
```

へ分岐する。

この方向は正しい。

---

## 2.2 power sync

`GPU::SetPowerCnt()`から:

```text
native GPU2D_A/B SetEnabled
Sapphire UnitA/B SetEnabled
```

が同時に呼ばれる。

前回の「disabled中にregister writeを捨てる」問題は改善している。

---

## 2.3 VBlank owner

現在は`GPU`が:

```text
VBlank
VBlankEnd
Window Check
```

をSapphire adapterへ配信し、
`SoftRenderer::VBlank()`／`VBlankEnd()`は空になっている。

二重配信は解消されている。

---

# 3. P0
# 初回Vulkan ROM白画面

## 3.1 activation transactionがない

現在のrenderer transaction:

```cpp
nds->GPU.SetRenderer3D(
    std::move(result.Renderer3D));

session.initialize(*nds);
session.beginGeneration(
    nds->GPU.GPU3D
        .GetCurrentRendererGeneration());
```

この間に行われていない処理:

```text
Sapphire UnitA/B完全seed
Sapphire internal phase整合
structured state clear
Sapphire framebuffer binding
first-frame publication reset
frame-boundary activation
```

`SetRenderer3D()`直後から:

```cpp
SapphireGpu2DState::IsActiveForRendering()
```

がtrueになるため、次のscanlineからSapphire GPU2Dが動く。

つまり:

```text
stateを完成させる前にactiveにしている
```

構造である。

---

## 3.2 complete seedはsavestate loadにしかない

現在の:

```cpp
SeedCompleteUnitFromNative(
    UnitA, GPU2D_A, GPU);

SeedCompleteUnitFromNative(
    UnitB, GPU2D_B, GPU);
```

はsavestate load後には実行される。

しかし:

```text
initial Vulkan install
Software→Vulkan
OpenGL→Vulkan
renderer generation change
Vulkan device recovery
```

では呼ばれない。

これはactivation lifecycleとして不完全である。

---

## 3.3 `RefreshSapphireVulkanBindings()`がpublicationまで行う

現在のbinding refreshは:

```cpp
softRenderer
    ->PublishCompletedSapphireFrontBuffer();
```

を呼ぶ。

関数名はbinding refreshだが、実際には:

```text
FrontBuffer変更
packed pointer公開
structured pointer公開
frame valid化
```

まで行う。

未完成frame、Reset直後のzero buffer、
古いrenderer generationを
completed frameとして公開する危険がある。

---

## 3.4 first-frame前の正しい状態

Vulkan activation直後は:

```text
Published2DFrame.valid = false
```

でなければならない。

最初の完全な:

```cpp
GPU::FinishFrame()
```

後だけ:

```text
Published2DFrame.valid = true
frameSerial > 0
rendererGeneration == active generation
frontBuffer == completed buffer
```

とする。

---

# 4. P0修正
# 明示的なSapphire Vulkan activationを追加する

## 4.1 API

`GPU`へ次を追加する。

```cpp
bool ActivateSapphireVulkan2D(
    u64 rendererGeneration) noexcept;

void DeactivateSapphireVulkan2D() noexcept;
```

または`SapphireGpu2DState`へ:

```cpp
bool ActivateFromNativeState(
    GPU& gpu,
    u64 rendererGeneration) noexcept;

void Deactivate(GPU& gpu) noexcept;
```

---

## 4.2 activation順序

必ずemulation pause中、
frame boundary、
renderer transaction lock内で行う。

```text
1. producer suspend
2. previous frame references retire
3. SetRenderer3D(Vulkan)
4. InvalidateSapphirePublication()
5. ClearStructuredVulkan2DState()
6. SeedCompleteUnitFromNative(UnitA)
7. SeedCompleteUnitFromNative(UnitB)
8. bind current backbuffer using exact Sapphire semantics
9. clear temporal history
10. set active Sapphire generation
11. session.initialize()
12. session.beginGeneration()
13. producer resume
```

実装例:

```cpp
nds->GPU.SetRenderer3D(
    std::move(result.Renderer3D));

if (activateVulkanFrontend)
{
    const u64 generation =
        nds->GPU.GPU3D
            .GetCurrentRendererGeneration();

    if (!nds->GPU
            .ActivateSapphireVulkan2D(
                generation))
    {
        // Vulkan fallback
    }

    if (!session.initialize(*nds))
    {
        // Vulkan fallback
    }

    session.beginGeneration(generation);
}
```

---

## 4.3 activation generationを保持する

```cpp
u64 ActiveSapphireRendererGeneration = 0;
bool SapphireRenderingActive = false;
```

`IsActiveForRendering()`は:

```cpp
return SapphireRenderingActive
    && GPU.GPU3D.HasCurrentRenderer()
    && ActiveSapphireRendererGeneration
        == GPU.GPU3D
            .GetCurrentRendererGeneration()
    && GPU.GPU3D
        .GetCurrentRenderer()
        .UsesStructured2DMetadata();
```

とする。

単にcurrent rendererのclassだけを見て、
未seed状態をactive扱いしない。

---

# 5. P0
# complete seedの不完全さ

現在の`SeedCompleteUnitFromNative()`には
field conversionが含まれる。

特に:

```cpp
unit.OBJMosaicYCount =
    static_cast<u8>(
        gpu2d.OBJMosaicLine);
```

は意味が一致していない可能性が高い。

```text
native OBJMosaicLine
```

はline stateであり、

```text
Sapphire OBJMosaicYCount
```

はmosaic counterである。

単純castはSapphire lifecycleの直接移植ではない。

---

## 5.1 必須対応

次をfield-by-fieldで監査する。

```text
DispCnt
BGCnt
BGXPos／BGYPos
BGXRef／BGYRef
BGXRefInternal／BGYRefInternal
BGXRefReload／BGYRefReload
BGRotA～D
Win0Coords／Win1Coords
WinCnt
Win0Active／Win1Active
BGMosaicSize
OBJMosaicSize
BGMosaicY
BGMosaicYMax
OBJMosaicY
OBJMosaicYMax
OBJMosaicYCount
BlendCnt
BlendAlpha
EVA／EVB／EVY
CaptureCnt
CaptureLatch
FIFO state
MasterBrightness
```

対応しないinternal fieldは推測で代入しない。

選択肢:

```text
A. activationをVBlank end直後へ限定し、
   internal phaseをSapphire Reset→register seedから再構築

B. native GPU2Dへ必要なexact getterを追加し、
   semantic mappingをtest化

C. 長期的にはSapphire Unitをcanonical GPU2D stateにする
```

最もSapphireに近いのはCである。

---

# 6. P0
# 上下画面逆転・2D physical ownership

## 6.1 現在の独自layer

現在はouter `SoftRenderer`のbufferを:

```cpp
physicalTop =
    Framebuffer[BackBuffer][0];

physicalBottom =
    Framebuffer[BackBuffer][1];
```

と命名し、

```cpp
if (GPU.ScreenSwap)
    SetFramebuffer(
        physicalTop,
        physicalBottom);
else
    SetFramebuffer(
        physicalBottom,
        physicalTop);
```

している。

その後publicationでは再び:

```text
top.engine =
    ScreenSwap ? A : B

bottom.engine =
    ScreenSwap ? B : A
```

を計算する。

さらにframe pipelineへ:

```text
preparedFrameScreenSwap
renderScreenSwapAt3D
hardwareScreenSwap
```

を渡している。

同じscreen ownershipが複数層で再解釈されている。

---

## 6.2 Sapphire本家

Sapphire本家は:

```cpp
if (PowerControl9 & (1<<15))
{
    GPU2D_Renderer.SetFramebuffer(
        Framebuffer[back][0],
        Framebuffer[back][1]);
}
else
{
    GPU2D_Renderer.SetFramebuffer(
        Framebuffer[back][1],
        Framebuffer[back][0]);
}
```

という`AssignFramebuffers()`を正規処理とする。

`Framebuffer[][0/1]`へ
後から独自physical meaningを付けない。

---

## 6.3 修正

`BindSapphirePhysicalTargets()`を廃止または改名し、
Sapphireと同じ:

```cpp
AssignSapphireFramebuffers()
```

を実装する。

```cpp
void SoftRenderer::
AssignSapphireFramebuffers() noexcept
{
    auto* state =
        GPU.TryGetSapphireGpu2DState();

    if (!state)
        return;

    if (GPU.ScreenSwap)
    {
        state->Renderer.SetFramebuffer(
            Framebuffer[BackBuffer][0],
            Framebuffer[BackBuffer][1]);
    }
    else
    {
        state->Renderer.SetFramebuffer(
            Framebuffer[BackBuffer][1],
            Framebuffer[BackBuffer][0]);
    }
}
```

publicationでは、
buffer slotへ新しいphysical意味を付与しない。

---

## 6.4 source-of-truthを1個にする

次のどちらか一方に統一する。

### 推奨

```text
completed frameが
physical top pointer
physical bottom pointer
を確定して公開
```

その後のrenderer／presenterは
`ScreenSwap`で再swapしない。

### またはSapphire exact

```text
Framebuffer slot + ScreenSwap
```

をSapphireと同じ場所で一度だけ解決する。

**両方を同時に使わない。**

---

# 7. P0
# packed framebuffer clear不足

Vulkan-capable buildの`SoftRenderer` framebufferは:

```text
stride = 256 * 3 + 1
height = 192
```

で確保される。

`Reset()`は全領域をclearするが、
`Stop()`は現在:

```cpp
256 * 192 * sizeof(u32)
```

しかclearしない。

したがって:

```text
plane1
control plane
line metadata
```

が旧frameのまま残る。

backend切替後の:

```text
2D異常
古いHUD／BG metadata
黒透過
上下画面異常
4xだけ見える文字欠け
```

を悪化させる。

修正:

```cpp
#if Vulkan-capable
const size_t len =
    kPackedFramebufferPixels
    * sizeof(u32);
#else
const size_t len =
    256 * 192 * sizeof(u32);
#endif
```

Reset／Stop／fallback／device recoveryで同じhelperを使う。

---

# 8. P0
# CustomHUD未表示

## 8.1 現在のupload lifecycle

GUI側:

```text
renderCustomHud
uploadRegion
setCompositeRect
presentFrame
PresentedGameFrameなら
markLastUploadSubmitted(
    frame->presentTimelineValue)
```

overlay側:

```text
stage slot
recorded = true
pendingSubmittedUploadSlot = slot
pendingUpload = false
```

slotのcompletion timelineは、
GUI側がpresent成功後に後付けする。

---

## 8.2 permanent slot leak

次のケース:

```text
transfer command record成功
graphics queue submit成功
vkQueuePresentKHR失敗
```

または:

```text
transfer record成功
後続recordSurfaceCommands失敗
submitされない
```

では:

```text
slot.recorded = true
completionTimelineValue = 0
```

のまま残り得る。

`PresentedGameFrame`にならないため:

```cpp
markLastUploadSubmitted()
```

が呼ばれない。

3-slotを使い切ると、
以後CustomHUD uploadが永久に失敗する。

---

## 8.3 修正方針

present結果ではなく、
**queue submission結果**をownership boundaryにする。

presenterへdesktop overlay submission callbackを追加する。

```cpp
using OverlaySubmissionFn =
    void (*)(
        u64 uploadToken,
        bool submitted,
        u64 timelineValue,
        void* userData);
```

record時:

```text
slot tokenをcommand bufferへ関連付け
```

submit成功直後:

```text
submitted = true
timelineValue = actual submit value
```

record／submit失敗時:

```text
submitted = false
slotをimmediate reusableへ戻す
```

---

## 8.4 global last-slotを廃止する

現在のような:

```text
pendingSubmittedUploadSlot
markLastUploadSubmitted
```

というglobal mutable stateをやめる。

各uploadを:

```cpp
struct OverlayUploadTicket
{
    u64 token;
    size_t slotIndex;
    bool recorded;
    bool submitted;
    u64 completionTimelineValue;
};
```

で管理する。

---

## 8.5 CustomHUDはSapphire coreへ入れない

SapphireにはMelonPrime CustomHUDは存在しない。

したがって直接移植対象ではないが、
次の境界を守れば車輪の再発明にはならない。

```text
Sapphire:
    game frame production
    2D/3D composition
    surface presenter

MelonPrime Qt adapter:
    final post-game overlay pass
```

CustomHUDを:

```text
VulkanOutput
GPU2D structured metadata
Sapphire compositor
```

へ混ぜない。

---

# 9. P0
# fullscreen黒画面・再切替freeze

## 9.1 toggle coalescingがない

`MainWindow::toggleFullscreen()`は:

```cpp
showFullScreen();
```

または:

```cpp
showNormal();
```

を即時実行する。

transition中かどうかのstate、
debounce、
desired-state coalescingがない。

黒画面中に再度toggleすると、
最初のQt window-state transitionと
逆方向transitionが重なる。

---

## 9.2 毎presentでnative identityを検査

`ScreenPanelVulkan::presentOnGuiThread()`は
毎回`ensureNativeSurface()`へ入る。

fullscreen transition中はQt／Win32／X11／Wayland側の
native identityが一時的に変化・未確定になる場合がある。

identity mismatchになると:

```text
presenter unregister
detachSurface
surfaceHost destroy
VkSurfaceKHR recreate
presenter attach
surface generation更新
```

へ進む。

fullscreenを単なるresizeではなく、
surface teardownとして扱ってしまう。

---

## 9.3 detach／recovery wait

現在:

```text
detachSurface:
    最大250ms surface fence wait
    その後resource destroy

recoverSwapchain:
    waitForSurfaceIdle(surfaceState)
    timeout指定なし
    destroySwapchain
```

である。

2回目fullscreen toggleが
recovery pathと重なると、
GUI threadで無期限waitへ入り得る。

---

## 9.4 pending swapchain処理

`ensureSwapchain()`開始時:

```cpp
if (surfaceState.pending)
    destroySwapchainBundle(
        *surfaceState.pending);
```

と同期破棄する。

resize eventが連続した場合:

```text
pending build
↓
次resize
↓
pendingを即destroy
↓
再build
```

を繰り返す。

old activeを表示し続ける設計が
途中で崩れやすい。

---

# 10. P0修正
# fullscreen state machine

## 10.1 MainWindow側

```cpp
enum class FullscreenTransition
{
    StableWindowed,
    EnterPending,
    StableFullscreen,
    ExitPending,
};
```

保持値:

```text
current stable state
desired state
transition serial
```

toggle時:

```text
desired stateだけ更新
既存transition中なら新しいshow*を即呼ばない
```

Qt `WindowStateChange`完了後に、
desired stateが異なる場合だけ次transitionを開始する。

---

## 10.2 Vulkan widget側

通常fullscreenは:

```text
same QWidget
same native window
resize only
```

として扱う。

`ensureNativeSurface()`を毎presentから外す。

呼ぶ場所:

```text
show event
QPlatformSurfaceEvent::SurfaceCreated
QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed
明示backend activation
```

呼ばない場所:

```text
every present
normal WindowStateChange
normal Resize
```

---

## 10.3 swapchain request coalescing

`SurfaceState`へ:

```cpp
VkExtent2D desiredExtent;
u64 resizeSerial;
u64 buildingSerial;
```

を追加する。

resize event:

```text
desiredExtent更新
resizeSerial++
swapchainDirty=true
```

build中に新しいresizeが来た場合:

```text
現在のbuild完了
古いserialならpromotionせずretire
最新desiredExtentで次build
```

とする。

---

## 10.4 old activeを表示し続ける

新swapchainが完成するまで:

```text
active swapchainを維持
```

する。

pending作成失敗時:

```text
activeがvalidならpresent継続
```

黒clear画面へ切り替えない。

---

## 10.5 無期限wait禁止

runtime recoveryで:

```cpp
waitForSurfaceIdle(surfaceState);
```

を使わない。

```text
timeline retire
bounded fence poll
dirty marking
next frame retry
```

へ置換する。

`vkQueueWaitIdle()`を許可するのは:

```text
final application shutdown
device loss terminal recovery
```

だけ。

---

# 11. P1
# 4x文字潰れ

## 11.1 現在のfilter分離は方向として正しい

current presenterは:

```text
scale > 1
Linear filter
packed 2Dあり
```

の場合、direct presentを避ける。

その後:

```text
Sapphire composition = Nearest
final surface sampling = configured filter
```

に分離している。

この設計は維持する。

---

## 11.2 先に2D sourceを修正する

現状では:

```text
Sapphire Unit state不一致
screen ownership二重解釈
packed tail未clear
```

が残る。

これらは4xで拡大されると、
文字欠け・潰れとして目立つ。

したがって修正順:

```text
1. activation seed
2. screen ownership
3. packed clear
4. 4x filter
```

とする。

---

## 11.3 A/B test

必須matrix:

| Scale | Filter | Layout | 判定 |
|---:|---|---|---|
| 3x | Nearest | integer | baseline |
| 4x | Nearest | integer | source correctness |
| 4x | Linear | integer | final filter |
| 4x | Nearest | non-integer window | transform alignment |
| 4x | Linear | non-integer window | subpixel worst case |

判定:

```text
4x Nearestでも潰れる
    → packed／structured source問題

4x Nearestは正常、Linearだけ潰れる
    → final sampler／pixel-center問題

integer layoutは正常、任意window sizeだけ潰れる
    → affine transform subpixel問題
```

---

## 11.4 desktop affine transformのpixel snap

現在のtransform pathは:

```cpp
px = m0*x + m2*y + m4;
py = m1*x + m3*y + m5;
```

をそのままNDCへ変換し、
UVを0～1へ割り当てる。

integer scaling時も:

```text
translation
screen edge
texel center
```

をdevice pixelへsnapしない。

0°／180°かつinteger scalingでは:

```text
transformed edgeを整数pixelへround
source texel centerとdestination pixel centerを一致
```

させる。

回転・非integer transformでは
現状のfloat pathを維持する。

---

# 12. Sapphireからそのまま持ってくるもの

## 12.1 原則変更禁止

```text
SapphireGPU2DCore/Unit.cpp
SapphireGPU2DCore/GPU2D_Soft.cpp
VulkanReference/VulkanOutput.cpp
VulkanReference/FrameQueue.cpp
VulkanCompositorShader
VulkanAccumulate3dShader
VulkanSurfacePresenter shader
```

desktop差分以外で改変しない。

---

## 12.2 Sapphire presenterから維持するalgorithm

```text
FrameQueue ownership
FrameResource ownership
packed buffer upload
renderer 3D snapshot
composeAndSubmitFrame
direct/fallback present判定
surface draw order
temporal source handling
```

---

## 12.3 MelonPrime側に限定する差分

```text
VkSurfaceKHR owner adapter
Qt affine transforms
QWidget lifecycle
DPR変換
CustomHUD final overlay
fullscreen transition coalescing
device-loss callback
volk dispatch
```

---

# 13. 長期的な再発防止

## 13.1 canonical GPU2D state

理想:

```text
Sapphire UnitA/Bをcanonical stateにする
native rendererはそのstateを参照する
```

最低限:

```text
backend activation時にcomplete seed
activation後はevent orderを一致
```

を保証する。

---

## 13.2 upstream snapshotをvendor directoryへ固定

```text
vendor/sapphire/0.7.0.rc4/
```

または現行のSapphire source directoryに:

```text
UPSTREAM_COMMIT
UPSTREAM_SHA256
LOCAL_PATCHES.md
```

を置く。

---

## 13.3 allowed-difference marker

例:

```cpp
// MELONPRIME_DESKTOP_ADAPTER_BEGIN
// ...
// MELONPRIME_DESKTOP_ADAPTER_END
```

この範囲外はupstream normalized diffが0であることをCIで確認する。

---

# 14. 必須runtime test

## 14.1 initial Vulkan boot

```text
アプリ起動
Vulkan選択済み
ROM open
```

完了条件:

```text
first non-white frame <= 3 emulated frames
activation seed count = 1
publication before FinishFrame = 0
frame serial 0 publication = 0
```

---

## 14.2 backend matrix

```text
Software boot
OpenGL boot
OpenGL Compute boot
Vulkan boot
Software→Vulkan
OpenGL→Vulkan
Vulkan→Software
```

各20回。

---

## 14.3 screen ownership

次をROM menu／match画面で確認する。

```text
PowerControl bit15 = 0
PowerControl bit15 = 1
RenderScreenSwapAt3D = 0
RenderScreenSwapAt3D = 1
UI ScreenSwap = 0
UI ScreenSwap = 1
```

assert:

```text
physical top pointer
physical bottom pointer
engine A/B
layout top/bottom
```

をframeごとに記録する。

---

## 14.4 CustomHUD failure injection

次を各10回発生させる。

```text
record failure
submit failure
present out-of-date
present suboptimal
acquire timeout
fullscreen resize
```

完了条件:

```text
upload slots in use <= ring size
completion value 0のrecorded slot = 0
HUD再表示 <= 1 successful submit
```

---

## 14.5 fullscreen torture

```text
windowed→fullscreen→windowed
```

を0.2秒間隔で20回入力する。

内部ではcoalesceされるため:

```text
同時transition = 1
surface detach = 0
VkSurface recreation = 0
unbounded wait = 0
```

とする。

---

## 14.6 4x text

```text
Nearest
Linear
integer scaling on/off
DPR 100%／125%／150%／200%
```

でscreenshot comparisonを行う。

---

# 15. 推奨commit分割

## S68-1

```text
Add explicit Sapphire Vulkan activation transaction
```

```text
complete seed
generation gate
frame-boundary activation
```

---

## S68-2

```text
Separate Sapphire framebuffer binding from completed-frame publication
```

```text
RefreshSapphireVulkanBindingsからpublish削除
Bind helperとPublish helperを分離
```

---

## S68-3

```text
Restore exact Sapphire framebuffer slot semantics
```

```text
独自physicalTop／Bottom再定義を削除
AssignFramebuffersを直接移植
screen swap ownerを一か所へ統一
```

---

## S68-4

```text
Clear the complete packed framebuffer lifecycle
```

```text
Reset／Stop／fallback／recoveryを共通helper化
```

---

## S68-5

```text
Bind CustomHUD uploads to queue submission tickets
```

```text
upload token
submit callback
failure cancellation
timeline retirement
```

---

## S68-6

```text
Coalesce Qt fullscreen transitions
```

```text
desired fullscreen state
transition serial
second toggle coalescing
```

---

## S68-7

```text
Keep Vulkan surfaces stable across fullscreen resize
```

```text
normal fullscreenでsurface recreate禁止
pending swapchain serial
old active継続
unbounded recovery wait削除
```

---

## S68-8

```text
Snap integer Vulkan screen transforms to device pixels
```

4x text問題がsource修正後も残る場合に実施。

---

## S68-9

```text
Add executable Sapphire lifecycle regression tests
```

static source文字列testではなく、
実frame／timeline／surfaceを検査する。

---

# 16. 修正順

```text
1. S68-1 activation
2. S68-2 binding/publication分離
3. S68-3 framebuffer semantics
4. S68-4 packed clear
5. 初回Vulkan／2D／上下画面再検証
6. S68-5 CustomHUD
7. S68-6 fullscreen coalescing
8. S68-7 stable surface
9. fullscreen／HUD同時test
10. 4x A/B test
11. 必要な場合のみS68-8
12. S68-9
```

---

# 17. 禁止事項

```text
白画面をsplash／overlayの表示条件で隠す
frameSerial 0をpresented扱いする
SetRenderer直後にcompleted frameをpublishする
毎scanline complete seedする
Sapphire VulkanOutput heuristicsを独自判断で削除する
screen swapをGPU2D／publication／presenterの複数層で行う
CustomHUD slotをpresent成功時だけreleaseする
fullscreen transition中にVkSurfaceを毎回作り直す
recoverSwapchainで無期限waitする
pending swapchainをtimeline無視で即destroyする
4x文字問題をSapphire compositor shaderのROM固有hackで直す
```

---

# 18. 最終判断

今回の症状は、
Sapphire rasterizerそのものの失敗ではない。

```text
3D映像が正常
```

であることからも、
Vulkan 3D rendererの主要portは機能している。

残件は主に:

```text
Sapphire GPU2D stateをactiveにする順序
Sapphire framebuffer slotのdesktop解釈
Qt overlay upload ownership
Qt fullscreen surface lifecycle
desktop transform alignment
```

に集中している。

最も車輪の再発明を避ける修正は:

```text
Sapphire core／VulkanOutput／shaderは維持
Sapphire AssignFramebuffersとownership契約を直接使用
MelonPrime独自処理をQt adapter境界だけへ縮小
```

することである。

---

# 40. 実装進捗（S68）

| Phase | Commit | Status | Notes |
|---|---|---|---|
| S68-1 | `47a370f5d` | done | Sapphire Vulkan activation transaction |
| S68-2 | `69cc3b37d` | done | Binding/publication separation |
| S68-3 | `0fbc89a86` | done | Sapphire framebuffer slot semantics |
| S68-4 | `88c4b6967` | done | Packed framebuffer clear lifecycle |
| S68-5 | `8d6539ba9` | done | CustomHUD upload submission tickets |
| S68-6 | `18a0594f6` | done | Fullscreen transition coalescing |
| S68-7 | `a8c9a4861` | done | Stable surface across fullscreen resize |
| S68-8 | `d027d14ec` | done | Integer transform pixel snap |
| S68-9 | `39d4c5811` | done | S68 lifecycle regression tests |
