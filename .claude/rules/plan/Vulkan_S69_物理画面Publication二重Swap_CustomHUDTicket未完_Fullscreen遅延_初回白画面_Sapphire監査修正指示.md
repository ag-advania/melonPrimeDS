# Vulkan S69 監査・修正指示書
## 初回ROM白画面／2D上下画面混合／CustomHUD未表示／4x文字潰れ／fullscreen黒画面遅延

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `ba9c4f01dde5f8b916b04b9f3465d77771451e0a`  
**前回監査HEAD:** `b8d39ab5a56e3479fccb9f26f5ac6c80aec0d6b8`  
**差分:** 10 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 結論

S68-1～9の骨格は反映されている。

改善済み:

```text
- 明示的なSapphire Vulkan activation
- renderer generation gate
- framebuffer bindingとcompleted publicationの分離
- packed framebuffer全領域clear
- CustomHUD submission callbackの追加
- fullscreen desired-stateの導入
- resize serialの導入
- integer transformのpixel snap
```

しかし、今回の症状を直接説明する重大な実装ミスが残っている。

```text
P0-1:
completed 2D frame publicationで
physical top／bottomをScreenSwapにより再度入れ替えている

P0-2:
CustomHUD submissionがticket単位ではなく、
全recorded slot一括確定になっている

P0-3:
HUD upload slotの再利用判定がrecorded stateを無視している

P0-4:
HUD textureのGPU状態をsubmit前にCPU側で確定している

P0-5:
fullscreen transition state machineが
逆方向toggleを受けた場合に完了不能になる

P0-6:
同一extentのresizeでも毎回swapchainをdirtyにする

P0-7:
presentのたびにnative window identityを再検査し、
一時的なidentity不一致でsurface再生成へ進める

P1:
GPU2D pixel algorithmはSapphire由来だが、
GPU2D state ownershipはまだMelonPrime独自の二重state
```

特にP0-1は、ユーザー報告の:

```text
上画面へ下画面が合成されたように表示される
初回Vulkan起動が白画面
一部の4x表示で文字が潰れる
```

を同時に説明できる。

---

# 1. 最新pushの反映確認

今回追加された主要処理:

```text
GPU::ActivateSapphireVulkan2D()
GPU::DeactivateSapphireVulkan2D()
SoftRenderer::AssignSapphireFramebuffers()
SoftRenderer::PublishCompletedSapphireFrontBuffer()
Overlay submission notifier
fullscreenTransitionActive
desiredFullscreen
SurfaceState::resizeSerial
SurfaceState::buildingSerial
integerAxisAligned pixel snap
```

S68の意図自体は正しい。

ただし、static source testが意味的なscreen ownershipを検査せず、
文字列の存在だけを検査したため、
二重swapを正しい契約として固定してしまっている。

---

# 2. P0
# 2D上画面へ下画面が合成される直接原因

## 2.1 Sapphire framebuffer assignment

現在の`SoftRenderer::AssignSapphireFramebuffers()`:

```cpp
if (GPU.ScreenSwap)
{
    state->Renderer.SetFramebuffer(
        Framebuffer[BackBuffer][0].get(),
        Framebuffer[BackBuffer][1].get());
}
else
{
    state->Renderer.SetFramebuffer(
        Framebuffer[BackBuffer][1].get(),
        Framebuffer[BackBuffer][0].get());
}
```

これはSapphireの`AssignFramebuffers()`と同じ意味である。

この処理により:

```text
Framebuffer[][0] = physical top
Framebuffer[][1] = physical bottom
```

という完成frame slot契約が成立する。

`ScreenSwap`は:

```text
Unit Aをphysical topへ書くか
Unit Bをphysical topへ書くか
```

を決定するために、ここで一度だけ使う。

---

## 2.2 publicationで再swapしている

現在の`PublishSapphire2DFrame()`:

```cpp
const u32* physicalTop =
    GPU.ScreenSwap
        ? Framebuffer[frontBuffer][0].get()
        : Framebuffer[frontBuffer][1].get();

const u32* physicalBottom =
    GPU.ScreenSwap
        ? Framebuffer[frontBuffer][1].get()
        : Framebuffer[frontBuffer][0].get();
```

これは誤り。

`AssignSapphireFramebuffers()`ですでに
physical screen ownershipを解決している。

publicationで再び`ScreenSwap`を使用すると、
`ScreenSwap == false`のとき:

```text
published.top.packed    = physical bottom
published.bottom.packed = physical top
```

になる。

---

## 2.3 structured planeとの不一致

structured planeは:

```cpp
GetStructuredVulkan2DPlane(true, ...)
GetStructuredVulkan2DPlane(false, ...)
```

によりphysical top／bottomとして取得される。

`CurrentUnitTargetsTopScreen()`も、
rendererが現在書いているframebuffer pointerを:

```text
GPU.Framebuffer[][0] → top
GPU.Framebuffer[][1] → bottom
```

として判定している。

その結果、`ScreenSwap == false`では:

```text
published.top.packed
    = bottom screen packed data

published.top.structuredPlane*
    = top screen structured data
```

となる。

つまり一つのtop screenを合成する際に:

```text
下画面のpacked pixels
+
上画面のstructured metadata
+
上画面または3Dのlive source
```

を混ぜている。

これはユーザー報告の:

```text
上画面に下画面が合成されたようになる
```

と完全に一致する。

---

# 3. P0修正
# completed publicationからpointer swapを削除する

## 3.1 最小修正

```cpp
const u32* physicalTop =
    Framebuffer[frontBuffer][0].get();

const u32* physicalBottom =
    Framebuffer[frontBuffer][1].get();
```

publication内ではpointer選択に
`GPU.ScreenSwap`を使用しない。

---

## 3.2 `ScreenSwap`を残してよい情報

以下のmetadataには残してよい。

```cpp
published.hardwareScreenSwap =
    GPU.ScreenSwap;

published.top.engine =
    GPU.ScreenSwap ? 0u : 1u;

published.bottom.engine =
    GPU.ScreenSwap ? 1u : 0u;
```

理由:

```text
packed pointer:
    physical screen identity

engine:
    そのphysical screenを描いた2D engine identity

hardwareScreenSwap:
    capture／3D source timing metadata
```

であり、役割が異なる。

---

## 3.3 推奨API

pointerとstructured planeを別々に組み立てず、
同じ関数でphysical screen viewを生成する。

```cpp
struct SapphirePhysical2DScreenView
{
    const u32* packed = nullptr;
    const u32* plane0 = nullptr;
    const u32* plane1 = nullptr;
    const u32* control = nullptr;
    u32 engine = 0;
};

SapphirePhysical2DScreenView
SoftRenderer::BuildPhysicalScreenView(
    int frontBuffer,
    SapphirePhysicalScreen screen) const;
```

例:

```cpp
SapphirePhysical2DScreenView
SoftRenderer::BuildPhysicalScreenView(
    int frontBuffer,
    SapphirePhysicalScreen screen) const
{
    const bool top =
        screen == SapphirePhysicalScreen::Top;

    SapphirePhysical2DScreenView view{};
    view.packed =
        Framebuffer[frontBuffer]
            [top ? 0 : 1].get();

    view.plane0 =
        GetSapphire2DRenderer()
            .GetStructuredVulkan2DPlane(
                top, 0);

    view.plane1 =
        GetSapphire2DRenderer()
            .GetStructuredVulkan2DPlane(
                top, 1);

    view.control =
        GetSapphire2DRenderer()
            .GetStructuredVulkan2DPlane(
                top, 2);

    view.engine =
        top
            ? (GPU.ScreenSwap ? 0u : 1u)
            : (GPU.ScreenSwap ? 1u : 0u);

    return view;
}
```

これにより:

```text
packed physical identity
structured physical identity
engine identity
```

を一か所で確定できる。

---

# 4. 初回Vulkan ROM起動が白画面になる理由

## 4.1 最有力経路

ROM起動直後に`ScreenSwap == false`の場合、
現在のpublicationはtop screenとしてslot 1を公開する。

しかしslot 1はphysical bottomである。

ROM起動時に:

```text
表示layout = top only
physical bottom = blank／white
physical top = boot image
```

であれば、Vulkan presenterは白画面を正常frameとして表示する。

同時にstructured topはphysical top由来なので、
packed／structuredが一致しない。

---

## 4.2 他renderer経由で動く理由

他rendererでROMを起動し、
ゲーム内または試合画面まで進んでからVulkanへ切り替えると:

```text
PowerControl9 bit15
GPU.ScreenSwap
display capture state
```

が起動時とは異なる可能性がある。

`ScreenSwap == true`の状態では、
現在の誤ったpublication式でも:

```text
top = slot 0
bottom = slot 1
```

となり、偶然正しく見える。

したがって:

```text
初回Vulkanだけ白い
他renderer経由なら見える
```

という非対称な症状が発生できる。

---

## 4.3 修正順

初回白画面へ新しいfallbackやwaitを追加する前に、
必ずphysical publicationを修正する。

禁止:

```text
初回frameを数十frame捨てる
white pixel率でframeを拒否する
Software framebufferへfallbackする
splashを再表示して隠す
```

---

# 5. P0
# S68 static testが誤った契約を固定している

現在のtest:

```python
self.assertIn(
    "GPU.ScreenSwap",
    publish.group(0))
```

はpublication内に`ScreenSwap`があることを要求する。

これは今回の二重swapを
「Sapphire parity」として通してしまう。

削除する。

---

## 5.1 正しいtest

### pointer identity

```cpp
CHECK(
    published.top.packed
    == renderer.FramebufferForPhysicalTop(
        published.frontBuffer));

CHECK(
    published.bottom.packed
    == renderer.FramebufferForPhysicalBottom(
        published.frontBuffer));
```

### packed／structured identity

各physical screenへ異なるsentinelを入れる。

```text
top packed       = red sentinel
top structured   = top marker

bottom packed    = blue sentinel
bottom structured= bottom marker
```

`ScreenSwap` false／trueの両方で:

```text
top outputにblue sentinelが混ざらない
bottom outputにred sentinelが混ざらない
```

ことを検査する。

---

# 6. P0
# CustomHUDが表示されない原因

S68ではsubmission notifierが追加されたが、
計画したticket ownershipにはなっていない。

---

## 6.1 recorded slotを再利用可能扱いしている

現在:

```cpp
const bool slotAvailable =
    slot.completionTimelineValue == 0
    || completed;
```

しかし:

```text
recorded = true
completionTimelineValue = 0
```

は:

```text
command bufferへ記録済み
まだqueue submit結果が未確定
```

という状態である。

このslotを再利用すると、
GPUが読む前にmapped staging bufferを上書きできる。

修正:

```cpp
slotAvailable =
    slot.state == UploadState::Free
    || (slot.state == UploadState::Submitted
        && isCompleted(slot));
```

---

## 6.2 callbackにupload tokenがない

現在のcallback:

```cpp
void (*)(bool submitted,
         u64 timelineValue,
         void* userData);
```

では:

```text
どのupload
どのslot
どのsurface command buffer
```

のsubmit結果か識別できない。

---

## 6.3 全recorded slotを一括確定する

現在の`notifySurfaceSubmission()`は
全slotをloopし:

```text
recorded slotすべて
```

へ同じtimeline valueを設定する。

これはticket ownershipではない。

複数surface、
record失敗、
frame defer、
resize中command rebuildで誤関連付けされる。

---

## 6.4 submit前にGPU stateを確定している

`recordPendingTransfer()`内で:

```cpp
textureInitialized = true;
textureLayout =
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
hasValidUploadedOverlay = true;
```

を設定している。

これはcommand recording完了時点であり、
queue submit成功前である。

submitが失敗した場合、
実際のVkImageは:

```text
UNDEFINED
TRANSFER_DST
以前のlayout
```

のいずれかである可能性がある。

しかし次frameはCPU stateを信じて:

```text
oldLayout =
SHADER_READ_ONLY_OPTIMAL
```

としてbarrierを記録する。

これによりHUD textureが
永続的に不正layoutへ入り得る。

---

## 6.5 failure callbackがtexture stateをrollbackしない

submission failure時にclearされるのは:

```text
slot.recorded
uploadToken
completionTimelineValue
```

だけ。

以下は戻されない。

```text
textureInitialized
textureLayout
hasValidUploadedOverlay
```

そのためinvalid textureを
valid overlayとしてsamplingし続ける。

---

## 6.6 timeline非対応時がunsafe

submit成功でもtimeline valueが0の場合、
現在はslotをcancel扱いにする。

GPU commandは実際には実行中でも、
staging slotが即再利用される。

timeline semaphore非対応時は:

```text
surface in-flight fence
またはsubmission serial
```

をcompletion primitiveとして使う必要がある。

---

## 6.7 teardown時にsubmission notifierを解除していない

`ScreenPanelVulkan` destructorでは:

```text
overlay recorder解除
overlay transfer recorder解除
```

は行うが、

```text
overlay submission notifier解除
```

を行っていない。

overlay shutdown後にpresenterからcallbackされると、
解放済みoverlay objectへアクセスできる。

---

# 7. P0修正
# CustomHUDをexact upload ticket方式にする

## 7.1 state machine

```cpp
enum class OverlayUploadState : u8
{
    Free,
    Staged,
    Recorded,
    Submitted,
};
```

slot:

```cpp
struct OverlayUploadSlot
{
    VkBuffer buffer;
    VkDeviceMemory memory;
    void* mapped;
    VkDeviceSize capacity;

    OverlayUploadState state =
        OverlayUploadState::Free;

    u64 uploadToken = 0;
    u64 submitTimelineValue = 0;
    u64 submitSerial = 0;
};
```

---

## 7.2 record result

```cpp
struct OverlayTransferRecord
{
    bool recorded = false;
    u64 uploadToken = 0;
    u32 slotIndex = UINT32_MAX;
};
```

```cpp
OverlayTransferRecord
recordTransfer(
    VkCommandBuffer commandBuffer);
```

presenterのsurface command contextへ保存する。

```cpp
struct SurfaceCommandOverlayState
{
    bool transferRecorded = false;
    u64 uploadToken = 0;
};
```

---

## 7.3 submission callback

```cpp
using VulkanDesktopOverlaySubmissionFn =
    void (*)(
        u64 uploadToken,
        bool submitted,
        u64 timelineValue,
        u64 submissionSerial,
        void* userData);
```

submit成功時は、
そのcommand bufferへ記録されたtokenだけ通知する。

```cpp
if (overlayState.transferRecorded)
{
    notifier(
        overlayState.uploadToken,
        true,
        timelineValue,
        submissionSerial,
        userData);
}
```

record／submit失敗:

```cpp
notifier(
    overlayState.uploadToken,
    false,
    0,
    0,
    userData);
```

---

## 7.4 texture stateをtransactionalにする

CPU側に:

```cpp
struct OverlayTextureState
{
    VkImageLayout committedLayout;
    bool committedInitialized;
    bool committedValid;
};
```

を持つ。

command recording中はlocal state:

```cpp
OverlayTextureTransition transition;
```

を使用する。

submit成功後だけ:

```text
committedLayout更新
committedInitialized=true
committedValid=true
```

とする。

submit失敗時はcommitted stateを変更しない。

---

## 7.5 timeline非対応fallback

presenter側で:

```cpp
u64 submittedSerial;
u64 completedSerial;
```

をsurfaceごとに管理する。

in-flight fence wait成功時:

```cpp
completedSerial =
    lastSubmittedSerial;
```

とし、overlayへcompletion通知する。

slotは:

```text
timeline対応:
    timeline valueで解放

timeline非対応:
    completed submission serialで解放
```

する。

---

## 7.6 texture resource retirement

fullscreen／DPR変更でHUD textureサイズが変わる場合、
旧textureを即destroyしない。

```text
image
imageView
memory
descriptor generation
last submit value
```

を一つのresource bundleとしてretire queueへ入れる。

---

## 7.7 destructor順序

```cpp
presenter
    ->SetDesktopOverlaySubmissionNotifier(
        nullptr, nullptr);

presenter
    ->SetDesktopOverlayRecorder(
        nullptr, nullptr);

presenter
    ->SetDesktopOverlayTransferRecorder(
        nullptr, nullptr);
```

をoverlay shutdownより先に行う。

---

# 8. P0
# fullscreen transition state machineの停止バグ

現在:

```cpp
void syncFullscreenTransitionState()
{
    const bool nowFullscreen =
        isFullScreen();

    if (nowFullscreen
        == desiredFullscreen)
    {
        fullscreenTransitionActive =
            false;
    }
    else if (!fullscreenTransitionActive)
    {
        startFullscreenTransition();
    }
}
```

---

## 8.1 逆方向toggle時

例:

```text
windowed
→ fullscreen要求
→ transitionActive=true
→ 完了前にwindowed要求
→ desiredFullscreen=false
→ 最初のWindowStateChange到着
→ nowFullscreen=true
→ desiredFullscreen=false
→ transitionActive=true
→ 何もしない
```

その後:

```text
transitionActive=trueのまま
次transitionが開始されない
```

状態になる。

---

## 8.2 修正

WindowStateChangeは
「現在のOS transitionが一度完了した」
という通知として扱う。

```cpp
void MainWindow::
syncFullscreenTransitionState()
{
    fullscreenTransitionActive = false;

    const bool nowFullscreen =
        isFullScreen();

    if (nowFullscreen
        != desiredFullscreen)
    {
        QTimer::singleShot(
            0,
            this,
            [this]()
            {
                if (!fullscreenTransitionActive
                    && isFullScreen()
                        != desiredFullscreen)
                {
                    startFullscreenTransition();
                }
            });
    }
}
```

---

## 8.3 初期desired state

constructorの固定falseではなく、
window restore後に:

```cpp
desiredFullscreen =
    isFullScreen();
```

で初期化する。

---

# 9. P0
# fullscreen時の重複resizeと同期swapchain再構築

## 9.1 resizeが三重に発生する

現在:

```text
ScreenPanelVulkan::changeEvent
ScreenPanelVulkan::resizeEvent
presentOnGuiThread→configureSurface
```

から`resizeSurface()`が呼ばれ得る。

---

## 9.2 同一extentでもdirtyにする

現在の`resizeSurface()`:

```cpp
requestedWidth = width;
requestedHeight = height;
desiredExtent = {width, height};
++resizeSerial;
swapchainDirty = true;
```

同じwidth／heightでも:

```text
resizeSerial増加
swapchainDirty=true
```

になる。

fullscreen中の複数Qt eventにより、
同一extentのswapchainを複数回作成できる。

---

## 9.3 idempotent化

```cpp
if (surfaceState.requestedWidth == width
    && surfaceState.requestedHeight
        == height)
{
    return true;
}
```

を最初に入れる。

---

## 9.4 GUI event coalescing

`changeEvent`と`resizeEvent`から
直接`resizeSurface()`を呼ばない。

```cpp
void scheduleSurfaceResize();
```

へ統一する。

```text
QTimer single-shot
最新pixel extentだけ保持
1 event loopにつき1回
```

とする。

---

## 9.5 present-time identity検査を外す

現在のpresent path:

```cpp
if (surfaceId == 0
    || !surfaceHost.matchesWidget(*this))
{
    ensureNativeSurface();
}
```

`matchesWidget()`はnative identityを取得する。

fullscreen transition中の一時的identity変化を
surface destruction理由にしてはいけない。

surface lifecycleは:

```text
QPlatformSurfaceEvent::SurfaceCreated
QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed
backend activation
widget destruction
```

でのみ処理する。

normal presentでは:

```cpp
if (surfaceId == 0)
    return;
```

だけにする。

---

# 10. fullscreen数秒黒画面の残存原因

## 10.1 swapchain buildがGUI thread同期

fullscreen extent変更後、
次presentの`ensureSwapchain()`内で:

```text
surface capabilities取得
formats取得
present modes取得
swapchain作成
image views作成
render pass作成
graphics pipeline作成
framebuffers作成
```

をすべてGUI threadで同期実行する。

driver pipeline compileが遅い環境では、
数秒black windowになり得る。

---

## 10.2 pipeline／render pass再利用

extentだけ変更され、
surface formatが同じ場合:

```text
graphics pipeline
pipeline layout
shader modules
compatible render pass
```

を再作成する必要はない。

Sapphire presenter algorithmを維持しつつ、
desktop adapterで:

```cpp
struct SurfaceFormatRenderResources
{
    VkFormat format;
    VkRenderPass renderPass;
    VkPipeline pipeline;
};
```

をformat単位でcacheする。

swapchain resize時に新規作成するのは:

```text
VkSwapchainKHR
swapchain image views
framebuffers
```

へ限定する。

---

## 10.3 old activeの扱い

new pending swapchain完成までは、
old activeを破棄しない。

new build成功後にのみ:

```text
active ← pending
old active → timeline retire
```

する。

---

## 10.4 recover wait

現在のrecoverは最大250ms waitしてから
swapchainを破棄する。

結果を無視して破棄へ進むのではなく:

```text
timeout:
    active bundleをretire queueへ保持
    dirtyだけ設定
    次GUI tickでreplacement build

success:
    normal retire
```

とする。

---

## 10.5 recursive rebuild禁止

現在のstale serial処理:

```cpp
return ensureSwapchain(
    surfaceState);
```

はrecursiveである。

resize event storm時の
再帰的なdriver resource buildを避ける。

```text
for attempt = 0～1
```

のbounded loopへする。

---

# 11. 4xでゲーム内文字が潰れる問題

## 11.1 先にphysical publicationを修正する

現在はpacked topとstructured topが
異なるphysical screen由来になり得る。

DSの2D textは:

```text
packed plane
structured control
brightness／blend metadata
```

の境界が細かい。

この不一致を4xへ拡大すると:

```text
glyph edge欠け
横線潰れ
別screen pixel混入
blend判定不一致
```

として目立つ。

4x filterを変更する前に、
P0 physical publicationを直す。

---

## 11.2 現在のinteger snap

現在のpixel snapは:

```text
axis aligned
scale X/Yが整数
```

の場合にdestination positionをroundする。

これは有効だが、
source内容の誤りは直さない。

---

## 11.3 修正後のA/B test

| Internal scale | Surface filter | ScreenSwap | 判定 |
|---:|---|---:|---|
| 1x | Nearest | 0 | reference |
| 4x | Nearest | 0 | packed correctness |
| 4x | Linear | 0 | filter correctness |
| 4x | Nearest | 1 | swap parity |
| 4x | Linear | 1 | swap＋filter |
| 4x | Nearest | 0→1 | transition |
| 4x | Nearest | 1→0 | transition |

---

## 11.4 physical fix後もLinearだけ潰れる場合

Sapphire compositorはpacked 2Dを
native pixel identityから再構築できる。

次の順で処理する。

```text
2D packed reconstruction:
    nearest／integer

3D source:
    configured filter

final arbitrary window scaling:
    user filter
```

2D glyphを3Dと同じlinear tapへ
早期に混ぜない。

ROM固有の文字補正shaderは追加しない。

---

# 12. Sapphireと同じ実装か

## 判定

```text
Vulkan 3D renderer:
    高い準拠

VulkanOutput／FrameQueue:
    高い準拠

GPU2D pixel algorithm:
    Sapphire vendor code

GPU2D register state ownership:
    非準拠

GPU2D lifecycle ownership:
    非準拠

physical frame publication:
    MelonPrime独自処理に誤り

CustomHUD:
    MelonPrime desktop拡張

fullscreen／surface:
    MelonPrime Qt adapter
```

完全にSapphireと同じではない。

parity tracker自身も:

```text
OPEN / integration in progress
Remaining risk: UnitSync mirror
```

としている。

---

# 13. GPU2D state ownershipの未解決部分

現在:

```text
native melonDS::GPU2D_A/B
+
Sapphire UnitA/B
```

を同時に保持する。

activation時に:

```cpp
SeedCompleteUnitFromNative()
```

を行い、
frame中はregister write forwardingと
`SyncExternalGpuState()`で同期する。

---

## 13.1 per-scanline sync不足

`SyncExternalGpuState()`が同期するのは主に:

```text
Enabled
MasterBrightness
CaptureCnt
CaptureLatch
FIFO
```

である。

同期しないstate:

```text
DispCnt delayed latch
LayerEnable
OBJEnable
ForcedBlank
BGXRefReload
BGYRefReload
mosaic latch
mosaic line counter
window phase
```

native GPU2DとSapphire Unitでは
内部state modelが異なる。

---

## 13.2 complete seedもsemantic conversion

例:

```cpp
unit.OBJMosaicYCount =
    static_cast<u8>(
        gpu2d.OBJMosaicLine);
```

これは同一field copyではない。

```text
native line counter
→ Sapphire internal count
```

という意味変換である。

新しい個別heuristicを追加し続けると、
Sapphireから離れる。

---

# 14. 車輪の再発明を避ける移植方針

## 14.1 S69では新しいUnitSync hackを追加しない

今回の直接不具合:

```text
physical publication
HUD ownership
fullscreen lifecycle
```

を先に直す。

---

## 14.2 S70でcanonical GPU2D ownershipを移植する

別branchでSapphire coreの
GPU2D dependency closureをvendorする。

対象:

```text
GPU2D Unit declaration／implementation
GPU2D register read／write lifecycle
VBlank／VBlankEnd
window update
mosaic counters
capture ownership
GPU2D SoftRenderer
framebuffer assignment
```

可能な限り同一commitから
ファイル単位で持ってくる。

---

## 14.3 推奨最終構造

```text
Sapphire UnitA/B
    = canonical GPU2D state

Sapphire SoftRenderer
    = Vulkan 2D renderer

native Software／OpenGL renderer
    = canonical Unit stateのread-only viewを使用
```

禁止:

```text
native state
→毎frame copy→
Sapphire state
```

canonical stateは一つにする。

---

## 14.4 直接vendor対象

可能な限りそのまま持ってくる。

```text
GPU2D.h
GPU2D.cpp
GPU2D_Soft.h
GPU2D_Soft.cpp
Renderer2D interface
GPU側のGPU2D event ordering
AssignFramebuffers
FinishFrame publication boundary
```

許容差分:

```text
namespace
include path
build gate
logging adapter
Vulkan desktop host access
MelonPrime debug hooks
```

禁止差分:

```text
register timing
mosaic counter timing
window timing
capture timing
screen ownership
framebuffer slot meaning
VBlank ordering
```

---

# 15. CustomHUDはSapphireへ混ぜない

CustomHUDはSapphireに存在しない。

正しい境界:

```text
Sapphire:
    game 2D
    game 3D
    final DS composition
    surface game draw

MelonPrime desktop adapter:
    CustomHUD texture upload
    final premultiplied-alpha overlay draw
```

現在のpremultiplied blend:

```text
src = ONE
dst = ONE_MINUS_SRC_ALPHA
```

は正しい。

変更不要。

問題はtexture／upload ownershipである。

---

# 16. 必須runtime diagnostics

## 16.1 physical publication

```text
[SapphirePhysicalPublish]
frameSerial
frontBuffer
screenSwap
topPackedSlot
bottomPackedSlot
topStructuredScreen
bottomStructuredScreen
topEngine
bottomEngine
```

完了条件:

```text
topPackedSlot = 0
bottomPackedSlot = 1
```

がScreenSwapに依存しない。

---

## 16.2 content hashes

各frame:

```text
top packed hash
top plane0 hash
top plane1 hash
top control hash

bottom packed hash
bottom plane0 hash
bottom plane1 hash
bottom control hash
```

top／bottomのpointer identityと
physical screen markerを記録する。

---

## 16.3 HUD ticket

```text
[VulkanHUDTicket]
token
slot
state
surfaceId
recorded
submitted
timeline
submitSerial
completed
```

禁止状態:

```text
RecordedなのにslotAvailable
Submittedなのにcompletion primitiveなし
failure後もcommitted texture state変更
同じtokenを複数submitへ関連付け
```

---

## 16.4 fullscreen

```text
[VulkanFullscreen]
desired
actual
transitionActive
eventSerial
resizeRequested
resizeSkippedSameExtent
swapchainBuildSerial
activeExtent
pendingExtent
```

---

# 17. 必須test

## 17.1 physical screen sentinel test

```text
ScreenSwap=false
ScreenSwap=true
```

両方で:

```text
slot0=top sentinel
slot1=bottom sentinel
published top=slot0
published bottom=slot1
```

---

## 17.2 packed／structured pairing test

```text
top packed + top structured
bottom packed + bottom structured
```

以外の組合せをassert failureにする。

---

## 17.3 initial Vulkan boot

```text
application start
Vulkan selected
ROM open
```

20回。

完了条件:

```text
first valid publication <= 3 frames
top packed slot=0
white-only unintended frame=0
renderer switch不要
```

---

## 17.4 HUD ticket failure injection

```text
no present frame
record failure
submit failure
acquire out-of-date
swapchain rebuild
timeline unsupported
```

完了条件:

```text
recorded slotの早期再利用=0
wrong token completion=0
HUD復帰 <= 次success submit
```

---

## 17.5 fullscreen

0.1秒間隔で:

```text
windowed
fullscreen
windowed
fullscreen
```

20回。

完了条件:

```text
transitionActive永久残留=0
同一extent rebuild=0
surface recreation=0
GUI wait 100ms超=0
black interval 1 refresh超=0
```

---

## 17.6 4x text golden image

同一frameを:

```text
Software
OpenGL
Vulkan 1x
Vulkan 4x Nearest
Vulkan 4x Linear
```

で比較する。

---

# 18. 推奨commit分割

## S69-1

```text
Fix physical top-bottom publication after Sapphire framebuffer assignment
```

内容:

```text
publication pointerからScreenSwap削除
top=slot0
bottom=slot1
```

---

## S69-2

```text
Publish packed and structured data through one physical screen view
```

内容:

```text
BuildPhysicalScreenView
pointer／planes／engineの一括確定
```

---

## S69-3

```text
Replace static ScreenSwap publication test with runtime sentinel tests
```

---

## S69-4

```text
Make Vulkan HUD upload slots stateful and non-reusable while recorded
```

---

## S69-5

```text
Bind Vulkan HUD submission completion to exact upload tokens
```

---

## S69-6

```text
Commit Vulkan HUD texture layout only after successful submission
```

---

## S69-7

```text
Retire Vulkan HUD textures and clear all presenter callbacks safely
```

---

## S69-8

```text
Fix reverse fullscreen transition coalescing
```

---

## S69-9

```text
Coalesce and deduplicate Vulkan surface resize requests
```

---

## S69-10

```text
Remove native surface identity checks from normal present
```

---

## S69-11

```text
Reuse Vulkan surface format pipelines across extent-only swapchain rebuilds
```

---

## S69-12

```text
Add initial Vulkan boot and 4x text golden tests
```

---

# 19. 実装順

```text
1. S69-1
2. S69-2
3. S69-3
4. 初回boot／2D／4x再確認
5. S69-4
6. S69-5
7. S69-6
8. S69-7
9. CustomHUD再確認
10. S69-8
11. S69-9
12. S69-10
13. S69-11
14. fullscreen再確認
15. S69-12
16. S70 canonical GPU2D parity branch開始
```

---

# 20. 完了条件

```text
physical top packedは常にslot0
physical bottom packedは常にslot1
packedとstructuredのphysical identity一致
初回Vulkan ROM bootで白画面なし
他renderer経由不要
上画面へ下画面混入なし
CustomHUDがVulkanで表示
HUD upload slot早期再利用なし
4x Nearest文字がSoftware referenceと一致
fullscreen黒画面が1 refresh以内
同一extent swapchain rebuildなし
reverse fullscreen toggleでstate停止なし
surface recreationはsurface lifecycle event時のみ
```

---

# 21. 禁止事項

```text
publication pointerをScreenSwapで再swapする
static testでGPU.ScreenSwap文字列だけを要求する
初回白画面をframe delayで隠す
top／bottomの色内容でphysical identityを推測する
CustomHUD submit結果を全recorded slotへ適用する
recorded staging slotをcompletion=0だけで再利用する
submit前にtexture layoutをcommitted扱いする
timeline非対応時に即slot解放する
fullscreen resizeごとにsurfaceを再生成する
同一extentでswapchainをdirtyにする
recursive ensureSwapchainを無制限に使う
UnitSync field conversionをさらに増やす
ROM固有4x文字補正shaderを追加する
```

---

# 22. 最終判断

S68で導入したactivation transaction自体は有効である。

しかし現在の最重要問題はactivation不足ではなく:

```text
Sapphire framebuffer assignment後の
completed publication二重swap
```

である。

この二重swapにより:

```text
physical bottom packed
+
physical top structured metadata
```

がtop screenとして合成されている。

まずこれを修正することで:

```text
2D異常
上画面への下画面混入
初回白画面
4x文字異常の一部
```

が同時に解消する可能性が高い。

CustomHUDとfullscreenは別のdesktop adapter ownership問題であり、
Sapphire compositorへ新しいhackを入れて直してはいけない。

車輪の再発明を最小化する正しい方針は:

```text
Sapphire screen ownershipをそのまま使う
Sapphire core algorithmを変更しない
MelonPrime差分をHUD／Qt surface adapterへ限定する
長期的にはSapphire Unitをcanonical GPU2D stateにする
```

ことである。

---

# 40. 進捗

| Phase | Commit | Status | Notes |
|---|---|---|---|
| S69-1 | `a0c061ec5` | done | Physical publication pointer fix |
| S69-2 | `42ec4b882` | done | BuildPhysicalScreenView |
| S69-3 | `a93f824d5` | done | Physical screen contract tests |
| S69-4 | `20e6188a0` | done | HUD slot state machine |
| S69-5 | `20e6188a0` | done | Exact upload token submission |
| S69-6 | `20e6188a0` | done | Texture commit after submit |
| S69-7 | `20e6188a0` | done | HUD texture retirement + notifier teardown |
| S69-8 | `c6e71dfb7` | done | Fullscreen transition coalescing |
| S69-9 | `8c961d55d` | done | Coalesced surface resize scheduling |
| S69-10 | `20e6188a0` | done | Present path skips identity recheck |
| S69-11 | `20e6188a0` | done | Format-scoped pipeline cache |
| S69-12 | `fbc75ae02` | done | S69 lifecycle tests + CI |
