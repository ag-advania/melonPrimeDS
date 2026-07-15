# Vulkan表示・HUD・Fullscreen・Pacing・画面所有権
## Sapphire準拠監査／修正指示書（S62）

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `03ee5800bd80f56ac521f33e8adee5716565b052`  
**監査HEADメッセージ:** `Document S61 Vulkan black-screen fix implementation progress.`  
**比較基準:** `e35c92408fb9d3b93af0cfe31a52e661d7d62a0a`以降  
**Sapphire frontend:** `SapphireRhodonite/melonDS-android` tag `0.7.0.rc4`  
**Sapphire core:** `SapphireRhodonite/melonDS-android-lib` commit `d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 監査結論

今回の報告:

```text
1. 試合中の3D映像はほぼ正常
2. CustomHUDが潰れている
3. 下画面レーダーを上画面へ表示する処理がおかしい
4. fullscreen切替時に数秒黒画面
5. frame limiter toggleが効かないように見える
6. ゲームメニューで上画面と下画面が逆
```

は、単一のVulkan 3D rasterizer不具合ではない。

現在の3D表示がほぼ正常であることから、主問題は以下の境界に移っている。

```text
Sapphire frame/presenter core
        ↓
MelonPrime desktop surface adapter
        ↓
MelonPrime CustomHUD／radar overlay
        ↓
Qt screen layout
        ↓
EmuThread pacing state
```

監査結果は次のとおり。

| 項目 | 判定 | 主因 |
|---|---|---|
| CustomHUD潰れ | **コード上確定** | 既存HUD rendererを使わず、固定0.5倍bitmap font＋1pixel＝1quadで再実装 |
| レーダー異常 | **コード上確定** | 既存radar rendererを使わず、presenter shaderへ簡略circle cropを独自追加 |
| Sapphire presenter parity | **未達** | upstream presenter／shaderへHUD・radar・affine desktop機能を混在 |
| fullscreen黒画面 | **コード上確定** | resizeではなくpresenter unregister・surface detach・`VkSurfaceKHR`破棄／再作成を起こし得る |
| limiter toggle | **一部仕様＋移植欠落** | AudioSync有効時は速度維持。加えてVulkanのSapphire fast-forward queue policyが未接続 |
| メニュー上下逆 | **最有力原因を特定、runtime traceで確定必要** | GPU2D framebuffer binding、hardware screen swap、Qt layout swap、presenter screen selectionの所有権が重複適用される可能性 |

優先順位:

```text
P0  CustomHUD／radarをSapphire presenterから分離し、既存CustomHud_Renderを再利用
P0  screen ownershipを1箇所だけで適用する契約へ統一
P1  fullscreen時は同一native surfaceのswapchain resizeに限定
P1  Sapphireの動的FrameQueuePolicyをそのまま移植し、runtime pacing stateを接続
P2  upstream parityの自動検査を追加
```

---

# 1. Sapphireと同じ実装になっているか

## 1.1 GPU2D core

前回までの部分抽出方式は廃止方向へ進み、現在は:

```text
src/SapphireGPU2DCore/GPU2D_Soft.cpp
src/SapphireGPU2DCore/GPU2D_Soft.h
src/SapphireGPU2DCore/SapphireGPU2DRenderer2D.h
src/SapphireGPU2DCore/UnitSync.cpp
```

を使用している。

これは前回より大幅に改善している。

ただし完全一致ではない。

MelonPrime側には依然として:

```text
GPU_Soft.cpp
UnitSync.cpp
SapphireGPU2DSoftAccess.cpp
GPU::FrontBuffer／Framebuffer公開
```

というouter adapterがある。

したがって:

```text
Sapphire GPU2D raster/capture algorithm
```

は移植されていても:

```text
framebuffer ownership
front/back buffer publication
unit A/B → physical top/bottom screen mapping
```

はMelonPrime独自である。

特にメニュー画面の上下逆はこのouter adapter境界を疑うべきである。

## 1.2 VulkanOutput／FrameQueue

現行構造はSapphireの:

```text
RunFrame
→ latch SoftPackedFrameSnapshot
→ prepareFrameForPresentation
→ FrameQueue push
→ presenter acquire
→ present
→ commit/defer
```

へ近づいている。

この方向は維持する。

## 1.3 VulkanSurfacePresenter

ここはSapphireと同じではない。

Sapphire upstreamのpresenterは基本的に:

```text
game frame
screen rectangles
background
post filter
swapchain
pacing
```

を担当する。

現行MelonPrime版では追加で:

```text
arbitrary affine screen transform
solid overlay quad
bitmap-font HUD
radar crop
radar frame
OSD
```

まで同じvertex buffer／shader／push constantsへ統合している。

代表的な差:

```text
upstream:
  kMaxSurfaceVertexCount = 30
  SurfaceVertex = x,y,u,v,alpha
  draw mode 0～6

MelonPrime:
  kMaxSurfaceVertexCount = 65536
  SurfaceVertexにRGBA追加
  draw mode 7 = solid overlay
  draw mode 8 = radar overlay
  radar用push constants追加
```

したがって現在のpresenterは:

```text
Sapphire core + desktop adapter
```

ではなく:

```text
Sapphire coreを直接拡張したMelonPrime独自renderer
```

になっている。

この混在がCustomHUD／radarの再発を招いている。

---

# 2. P0: CustomHUDが潰れる根本原因

対象:

```text
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/MelonPrimeHudRender.cpp
src/frontend/qt_sdl/MelonPrimeHudRenderMain.inc
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.frag
```

## 2.1 既存の正規HUD rendererを使っていない

通常のMelonPrime CustomHUDには既に:

```text
CustomHud_Render()
QPainter
QFont
高解像度font選択
HUD scale
anchor
alignment
crosshair
HP
score
rank
time
ammo
inventory
radar
SVG frame
```

を扱う実装がある。

しかしVulkan pathはこれを呼んでいない。

代わりに`captureHudSnapshotOnEmuThread()`でHUD要素の一部を再構成し:

```cpp
addText(..., 0.5f, ...);
```

のようにtext scaleを固定している。

その後GUI thread側で:

```text
extern unsigned short font[]
12px bitmap glyph
1bit pixel
1pixelごとにsolid quadを1枚
```

へ展開している。

これは既存CustomHUDと同じ見た目にはならない。

## 2.2 潰れが発生する直接要因

### A. scale固定

HUD設定のfont size／scaleを使わず、text commandが原則`0.5f`固定。

高DPI／fullscreen／stretched top screenで細線化または潰れが発生する。

### B. bitmap fontへ退化

通常HUDはQt font rasterizationを使うが、Vulkan pathは古いOSD用bitmap fontを使う。

以下が失われる。

```text
font family
weight
hinting
anti-alias
language glyph
高解像度font
文字幅
baseline
kerning
```

### C. glyph pixelごとのquad

文字1pixelにつき6 vertexを追加する。

HUD全体では非常に多数のquadになる。

```cpp
constexpr size_t kMaxSolidQuads = 10'000;
```

へ到達すると、後続のpixelが静かに捨てられる。

これは文字やゲージの一部欠け、潰れ、断片化を説明する。

### D. UI renderingとpresenter coreが結合

HUD変更のたびにSapphire presenterのvertex bufferとshader契約へ影響する。

upstream parityが保てない。

---

# 3. CustomHUD修正方針
## 既存`CustomHud_Render`をそのまま再利用する

CustomHUDのvisual algorithmをVulkan用に再実装しない。

## 3.1 新規desktop-only layer

```text
MelonPrimeVulkanOverlayRenderer.h
MelonPrimeVulkanOverlayRenderer.cpp
```

を作る。

責務:

```text
既存CustomHud_Renderが生成したtransparent RGBA imageを受け取る
persistent VkImageへdirty rect upload
Sapphire game frame draw後にalpha composite
```

Sapphire presenter本体へHUD固有draw modeを追加しない。

## 3.2 HUD生成

第一選択:

```cpp
QImage hudImage(
    outputWidth,
    outputHeight,
    QImage::Format_ARGB32_Premultiplied);

hudImage.fill(Qt::transparent);

QPainter painter(&hudImage);
CustomHud_Render(
    painter,
    ...既存引数...);
```

ただし既存`CustomHud_Render`がwidget-space／screen transformを内部計算する場合は、
そのまま正しいsurface sizeと既存screen layoutを渡す。

最小の変更で共有できない場合は、次の2層へ分ける。

```text
CustomHud_BuildFrameModel(...)
CustomHud_PaintFrameModel(QPainter&, ...)
```

通常OpenGL pathとVulkan overlay pathの両方が同じpaint functionを使う。

禁止:

```text
Vulkan専用HudTextCommandの手作業追加
HPだけ別実装
ammoだけ別実装
font幅を独自計算
bitmap fontへの変換
```

## 3.3 upload

毎frame新規image／memory allocationしない。

保持:

```cpp
struct VulkanHudTexture
{
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    void* mapped;
    u32 width;
    u32 height;
    u64 generation;
    QRect dirtyRect;
};
```

更新:

```text
HUD generation不変 → uploadなし
サイズ変更 → texture recreate
dirty rectあり → rectだけcopy
```

これはCPU framebufferによるゲーム表示fallbackではない。

UIをCPUでrasterizeしGPU textureへuploadする通常のoverlay方式であり、
ゲーム画面のCPU readback／resamplingは禁止したまま維持できる。

## 3.4 composite

推奨順序:

```text
Sapphire presenter: game frame
MelonPrime overlay pass: CustomHUD texture
MelonPrime overlay pass: OSD
present
```

同一render passのsecond subpassでもよいが、
upstream presenter sourceを変更しないwrapper設計を優先する。

---

# 4. P0: レーダー表示異常

## 4.1 現在のVulkan radarは正規実装ではない

正規CustomHUD radarは:

```text
bottom screen source
hunter別center
source radius
circular clip
frame image／SVG
outline
opacity
destination anchor
```

を既存rendererで描く。

Vulkan pathではこれを簡略化し:

```text
circle length
0.92固定border
uTextureからbottom atlasをsample
frame color
```

だけにしている。

shaderでは:

```glsl
uvFromScreenLocal(sourceLocal, false)
```

を使い、常にbottom atlas regionとしてsampleする。

問題:

```text
frame-local screen ownershipを明示していない
hardware screen swapを考慮しない
capture／temporal sourceを区別しない
既存SVG frameを使わない
既存outlineを使わない
destination top screenとsource bottom screenの概念を分離していない
```

## 4.2 修正

CustomHUD texture方式へ統一し、radarも`CustomHud_Render`へ戻す。

これにより:

```text
source crop
hunter center
frame
outline
opacity
anchor
```

は既存実装をそのまま使える。

presenter shaderの:

```text
kDrawModeRadarOverlay
radarSourceCenterX/Y
radarSourceRadius
radarFrameColor
```

は削除する。

## 4.3 重要な所有権

レーダーの意味は:

```text
描画先: 上画面のHUD座標
画像source: DS下画面
```

である。

この2つを同じ`topScreen` boolで表現してはいけない。

必要な型:

```cpp
enum class HudDestinationSurface
{
    PhysicalTop,
    PhysicalBottom,
    WindowSpace
};

enum class HudImageSource
{
    PhysicalTopFrame,
    PhysicalBottomFrame
};
```

ただし既存`CustomHud_Render`を完全に再利用できるなら、
新しい抽象化を増やさず既存transform／source buffer契約を使う。

---

# 5. P0: Sapphire presenterを元へ戻す

## 5.1 そのまま持ってくるコード

tag `0.7.0.rc4`から以下を再vendorする。

```text
VulkanSurfacePresenter.h
VulkanSurfacePresenter.cpp
VulkanSurfacePresenter.vert
VulkanSurfacePresenter.frag
VulkanSurfacePresenter_*ShaderData.h
```

以下の範囲はalgorithm bodyを変更しない。

```text
frame wait
composition input
descriptor update
swapchain acquire
render command recording
submission
present
pacing stats
deadline handling
screen texture sample
post filter
```

## 5.2 MelonPrime独自差分を外へ移す

外部wrapper:

```cpp
class MelonPrimeDesktopVulkanPresenter
{
    VulkanSurfacePresenter sapphirePresenter;
    MelonPrimeVulkanOverlayRenderer overlayRenderer;
    MelonPrimeVulkanSurfaceHost surfaceHost;
};
```

処理:

```text
1. Sapphire presenterへframe inputを渡す
2. Sapphireがgame frameをrecord
3. desktop overlayをrecord
4. queue submit／present
```

Sapphire presenterにoverlay callback hookを1箇所だけ追加する必要がある場合も、
汎用callbackに限定する。

例:

```cpp
using AppendDesktopOverlayCommands =
    bool (*)(VkCommandBuffer, const OverlayTarget&, void*);

void SetDesktopOverlayCallback(
    AppendDesktopOverlayCommands callback,
    void* context);
```

ただし可能ならwrapper側でcommand bufferを組み、
upstream sourceを完全無改変にする。

## 5.3 禁止

```text
Sapphire shaderへMelonPrime draw mode追加
Sapphire SurfaceVertexへHUD color追加
kMaxSurfaceVertexCountをHUD都合で65536へ増加
CustomHUDのpixelをSapphire screen vertex bufferへ混ぜる
radar samplingをSapphire frame shaderへ混ぜる
```

---

# 6. P1: fullscreen切替時の数秒黒画面

対象:

```text
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHost.cpp
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp
src/frontend/qt_sdl/Window.cpp
```

## 6.1 現在の経路

`presentOnGuiThread()`は毎回:

```cpp
ensureNativeSurface()
```

を呼ぶ。

`matchesWidget()`がfalseなら:

```text
presenter unregister
surface detach
VkSurfaceKHR destroy
new native identity query
new VkSurfaceKHR create
present queue resolve
surface attach
surface generation increment
swapchain configure
presenter re-register
```

を行う。

fullscreen時、Qt／Win32はwindow state・child native handle・DPR・geometryを
複数eventに分けて更新し得る。

その途中の一時identity変化を拾うと、
単なるresizeにもかかわらず完全surface再生成になる。

## 6.2 swapchain再生成も破壊的

現在の`ensureSwapchain()`は:

```text
surface idle wait
old swapchain destroy
new swapchain create
oldSwapchain = VK_NULL_HANDLE
```

である。

新swapchain準備前に旧swapchainを失うため、
切替中に表示可能な最後のframeがなくなる。

## 6.3 修正するstate machine

```cpp
enum class DesktopSurfaceLifecycle
{
    Stable,
    ResizePending,
    NativeSurfaceReplacementPending,
    ReplacementSurfaceReady,
    RetiringOldSurface
};
```

### resize／fullscreenでnative identityが同じ

```text
presenter登録維持
VkSurfaceKHR維持
surface generation維持
FrameQueue維持
resizeSurfaceだけ呼ぶ
swapchainだけ再生成
```

### native identityが本当に変わる

```text
1. 新native identityがevent-loop 1～2 turn安定するまで待つ
2. 新VkSurfaceKHRを作る
3. present queue support確認
4. 新presenter surface stateをattach
5. 新swapchainを作る
6. 最初のpresent成功
7. active surface generationを切替
8. 旧surface／swapchainをretire
```

旧surfaceを先に破壊しない。

## 6.4 Qt event駆動

毎present時のidentity pollingで破壊しない。

扱うevent:

```text
QEvent::Resize
QEvent::WindowStateChange
QEvent::ScreenChangeInternal
QEvent::DevicePixelRatioChange
QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed
QPlatformSurfaceEvent::SurfaceCreated
```

`WindowStateChange`では0msまたは1 event-loop遅延でgeometryを確定し、
同一HWNDならresize扱い。

## 6.5 `oldSwapchain`を使う

```cpp
VkSwapchainKHR oldSwapchain = surfaceState.swapchain;

VkSwapchainCreateInfoKHR info{};
info.oldSwapchain = oldSwapchain;

VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
vkCreateSwapchainKHR(..., &newSwapchain);

surfaceState.swapchain = newSwapchain;
retire(oldSwapchain, completionTimelineValue);
```

旧image view／framebufferも同じcompletion pointでretireする。

## 6.6 黒画面防止

新surfaceの最初のgame frame presentまでは:

```text
hasPresentedFrameをresetしない
no-ROM splashを出さない
renderer generationをresetしない
producerをsuspendしない
```

platform上で旧surfaceを維持できない場合のみ、
小さいtransition overlayを表示する。

---

# 7. P1: frame limiter toggle

対象:

```text
src/frontend/qt_sdl/Window.cpp
src/frontend/qt_sdl/EmuThread.cpp
src/VulkanDesktopCompat.cpp
src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp
```

## 7.1 UI toggle自体

`actLimitFramerate`は:

```cpp
emuInstance->doLimitFPS = checked;
```

を更新している。

ここは接続されている。

## 7.2 AudioSync仕様

EmuThreadは:

```cpp
if (!doLimitFPS && !doAudioSync)
    curFPS = 1000.0;
else
    curFPS = targetFPS;
```

としている。

したがって:

```text
LimitFPS OFF
AudioSync ON
```

では、速度はtarget FPS相当に保たれる。

これは現行melonDS系の仕様であり、
Vulkanだけの故障ではない。

UI上は混乱しやすいため、OSDを:

```text
Frame limiter disabled
Audio sync is still limiting speed
```

のように分けることを推奨する。

## 7.3 Vulkan固有の移植欠落

`VulkanDesktopCompat.cpp`の:

```cpp
bool isFastForwardActive()
{
    return false;
}
```

が常時false。

さらにfrontend sessionのqueue policyが固定:

```cpp
MaxBacklogDepth = 2;
AllowStealPending = false;
AllowPreviousFrameReuse = true;
AllowDropForDeadline = false;
```

である。

このため:

```text
LimitFPS OFF
AudioSync OFF
```

でも、producerがpresenter／FrameQueueの逆圧で止まり、
期待するunlimited速度にならない可能性が高い。

## 7.4 Sapphireコードをそのまま移植

Sapphire `MelonInstance.cpp`から以下をalgorithm bodyそのままで移植する。

```text
makeVulkanRealtimeFrameQueuePolicy()
makeVulkanLateRealtimeFrameQueuePolicy()
makeVulkanFastForwardFrameQueuePolicy()
constrainGraphicsHardwareFrameQueuePolicy()
makeFrameQueuePolicy()
```

MelonPrime session側で:

```cpp
FrameQueuePolicy queuePolicy(
    int renderScale,
    bool presentationLate,
    bool temporalHistoryRequired);
```

として呼ぶ。

## 7.5 runtime state bridge

```cpp
struct VulkanRuntimePacingState
{
    std::atomic_bool fastForward{false};
    std::atomic_bool unlimited{false};
    std::atomic_bool presentationLate{false};
    std::atomic_u64 generation{0};
};
```

EmuThreadで毎frameまたは状態変更時にpublish:

```cpp
state.fastForward.store(fastforward);
state.unlimited.store(
    !emuInstance->doLimitFPS
    && !emuInstance->doAudioSync);
state.generation.fetch_add(1);
```

compat function:

```cpp
bool isFastForwardActive()
{
    return RuntimePacingState().fastForward.load(...)
        || RuntimePacingState().unlimited.load(...);
}
```

Sapphire API名を維持して変更量を減らす。

## 7.6 limiter phase reset

toggle時に:

```text
frameLimitError
lastTime
storedFrametimeStep
isFirstLimiterFrame
```

をresetする。

EmuThreadへmessageを追加するか、
pacing generation変化をmain loopでconsumeする。

固定phaseを持ち越すと、
toggle直後に一時停止／burstが発生する。

## 7.7 VSync／present mode

limiter OFFで速度が上がらない場合でも、
いきなりpresent modeを強制変更しない。

順序:

```text
1. AudioSync状態確認
2. EmuThread limiter状態確認
3. Sapphire fast-forward queue policy適用
4. queue backlog／steal stats確認
5. 最後にpresent mode検討
```

---

# 8. P0: ゲームメニューで上画面と下画面が逆

この項目はruntime logがないため、現時点では最有力原因と確認手順を示す。

## 8.1 変更してはいけない箇所

Sapphireも現行MelonPrimeもframe latchへ:

```cpp
preparedFrameScreenSwap =
    nds->GPU.GPU3D.RenderScreenSwapAt3D;
```

を渡している。

このboolを単純反転してはいけない。

```cpp
preparedFrameScreenSwap = !...
```

は試合中3Dを壊す可能性が高い。

## 8.2 現在存在する3種類のswap

### A. hardware physical screen routing

```cpp
GPU.ScreenSwap
```

engine A／Bをphysical top／bottom displayへ割り当てる。

### B. 3D source ownership

```cpp
GPU3D.RenderScreenSwapAt3D
```

live 3D imageがどちらのphysical screenへ属するかを表す。

### C. user layout swap

```cpp
ScreenSwap config
layout.Setup(..., screenSwap)
screenKind[]
```

window内でtop／bottom表示位置をユーザー設定により入れ替える。

この3つは別物である。

## 8.3 現行outer adapter

Vulkan gate時の`GPU_Soft.cpp`は:

```cpp
if (GPU.ScreenSwap)
    Sapphire2DRenderer->SetFramebuffer(
        Framebuffer[BackBuffer][0],
        Framebuffer[BackBuffer][1]);
else
    Sapphire2DRenderer->SetFramebuffer(
        Framebuffer[BackBuffer][1],
        Framebuffer[BackBuffer][0]);
```

とし、その後:

```cpp
GPU.FrontBuffer = BackBuffer ^ 1;
GPU.Framebuffer[...] = Framebuffer[...];
```

を毎scanline後に同期している。

ここで既にunit A/Bからphysical top/bottomへのswapを適用している。

その後のsnapshot／compositor／presenterがさらに:

```text
frame->screenSwap
screenSwap push constant
topScreen／bottomScreen shader分岐
screenKind
```

でscreen identityを解釈すると、二重適用になる。

3D試合画面は`RenderScreenSwapAt3D`で正しく見え、
2D主体のメニューだけ逆になる症状と整合する。

## 8.4 正規contract

推奨contract:

```text
packedTopPlane*    = physical top LCD
packedBottomPlane* = physical bottom LCD

screen transform topScreen=true
    → packedTopを描く

screen transform topScreen=false
    → packedBottomを描く

frame->screenSwap
    → live 3D／capture source ownership判定だけに使用

Qt ScreenSwap config
    → destination transformの位置だけ入れ替える
```

つまり:

```text
physical screen bufferは一度だけ決定
layout swapは表示位置だけ変更
3D screenSwapは3D source選択だけ変更
```

## 8.5 実装案

Sapphire rendererへ渡すframebufferを常にunit identityで固定し:

```cpp
Sapphire2DRenderer->SetFramebuffer(
    unitAFramebuffer,
    unitBFramebuffer);
```

frame完成時に1回だけ:

```cpp
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

をpublication adapterで適用する。

またはSapphire upstream outer GPU ownershipが同じ処理を持つなら、
そのdependency closureをそのまま移植する。

重要なのは:

```text
scanline renderer内でswap
snapshot側でもswap
presenter側でもswap
```

をしないこと。

## 8.6 `FrontBuffer` publication

`SyncSapphireFramebufferBindings()`を毎scanlineで呼び:

```cpp
GPU.FrontBuffer = BackBuffer ^ 1;
```

を毎回再設定する必要性を再監査する。

推奨:

```text
scanline中:
  back bufferへ書く

VBlank／frame complete:
  front/backを1回swap
  GPU.FrontBufferをpublish
  immutable snapshotをlatch
```

Sapphireのouter frame lifecycleをそのまま持ってこられるなら、
その実装を優先する。

## 8.7 必須trace

```cpp
struct ScreenOwnershipTrace
{
    u64 frameSerial;
    int frontBuffer;
    int backBuffer;

    bool gpuScreenSwap;
    bool renderScreenSwapAt3D;
    bool userLayoutSwap;

    int unitADestination;
    int unitBDestination;

    int packedTopEngine;
    int packedBottomEngine;

    bool frameScreenSwap;
    bool presenterTopTransformSamplesTop;
    bool presenterBottomTransformSamplesBottom;
};
```

VBlankごとに最初の120frameだけ出力。

期待:

```text
packedTopEngine／packedBottomEngineがphysical LCDと一致
userLayoutSwapはdestination matrixだけ変える
frameScreenSwapはpacked配列を交換しない
```

## 8.8 diagnostic signature

debug build限定で:

```text
unit A 左上 4x4 = red
unit B 左上 4x4 = blue
```

をpacked snapshot直前に挿入する。

確認:

```text
試合
pause menu
main menu
screen transition
user ScreenSwap ON/OFF
```

shipping buildには入れない。

---

# 9. Sapphireからそのまま持ってくる範囲

## 9.1 完全vendor推奨

```text
FrameQueue.h
FrameQueue.cpp

VulkanOutput.h
VulkanOutput.cpp

VulkanSurfacePresenter.h
VulkanSurfacePresenter.cpp
VulkanSurfacePresenter.vert
VulkanSurfacePresenter.frag
generated presenter shader headers

GPU2D_Soft.h
GPU2D_Soft.cpp
GPU2D Unit／Renderer2D interfaces

Sapphire frame queue policy functions
Sapphire frame latch／prepare／push ordering
```

## 9.2 dependency closureで持ってくる

個別function copyではなく、compile dependencyを追って持ってくる。

特に:

```text
GPU2D Unit state
framebuffer ownership
VBlank lifecycle
front/back buffer publication
screen swap mapping
```

は一体で扱う。

## 9.3 desktop固有として残す

```text
Qt screen layout → presenter transform
Win32／X11／Wayland VkSurfaceKHR
QPlatformSurfaceEvent bridge
fullscreen lifecycle
CustomHUD QImage texture upload
OSD／splash
EmuThread pacing atomic bridge
```

desktop-only blockには:

```cpp
// MELONPRIME_DESKTOP_ADAPTER_BEGIN
...
// MELONPRIME_DESKTOP_ADAPTER_END
```

を付ける。

---

# 10. Upstream parity自動検査

## 10.1 manifest

各vendor fileについて:

```text
upstream repository
upstream commit
upstream path
upstream SHA-256
local path
local SHA-256
allowed transform
```

を記録する。

## 10.2 CI

```text
tools/check_sapphire_vendor_parity.py
```

を追加。

判定:

```text
exact vendor file:
  hash一致必須

include pathだけ違うfile:
  normalized hash一致

desktop adapter:
  parity対象外だが明示list必須
```

## 10.3 presenter shader

HUD／radarを分離後:

```text
VulkanSurfacePresenter.frag
```

はupstream normalized hash一致を完了条件にする。

---

# 11. Commit分割

## Commit 1

```text
Restore Sapphire Vulkan presenter shader contract
```

変更:

```text
draw mode 7／8削除
radar push constants削除
SurfaceVertexをupstreamへ戻す
HUD vertex混在削除
```

## Commit 2

```text
Add MelonPrime Vulkan overlay renderer
```

変更:

```text
persistent HUD texture
dirty upload
alpha composite pass
OSD adapter
```

## Commit 3

```text
Reuse canonical CustomHud_Render for Vulkan
```

変更:

```text
Vulkan専用HUD再構成削除
固定0.5 scale削除
bitmap glyph quad削除
radarを既存rendererへ戻す
```

## Commit 4

```text
Make fullscreen Vulkan surface transitions non-destructive
```

変更:

```text
event-driven lifecycle
same-surface resize
staged replacement
oldSwapchain handoff
retire queue
```

## Commit 5

```text
Port Sapphire Vulkan frame queue policies
```

変更:

```text
realtime／late／fast-forward policy
runtime pacing bridge
stub isFastForwardActive削除
```

## Commit 6

```text
Reset frame limiter phase on runtime toggle
```

変更:

```text
limiter generation
phase reset
AudioSync OSD clarification
```

## Commit 7

```text
Define Vulkan physical screen ownership contract
```

変更:

```text
unit A/B publication
physical top/bottom mapping
frame screenSwap semantics
VBlank publication
```

## Commit 8

```text
Add Sapphire vendor parity CI
```

---

# 12. 検証マトリクス

## 12.1 CustomHUD

```text
windowed 1x
windowed high DPI
fullscreen
top stretched 16:9
top stretched 21:9
integer scaling
all HUD font choices
all HUD scales
Japanese／English OSD
```

比較:

```text
OpenGL Classic screenshot
Vulkan screenshot
```

pixel-perfectが不可能なfont backend差を除き、
layout／size／baseline／contentを一致させる。

## 12.2 radar

```text
all hunters
all source radius
all destination sizes
all anchors
top screen stretch
screen rotation
screen swap config
menu → match transition
```

確認:

```text
sourceは常に正しいbottom screen
destinationはtop HUD transform
SVG／frame／outline一致
```

## 12.3 fullscreen

計測:

```text
toggle request
WindowStateChange
final geometry
swapchain create begin/end
first acquire
first submit
first present
```

完了基準:

```text
黒画面 1 frame以下を目標
少なくとも数秒停止は0
renderer／FrameQueue再初期化なし
```

## 12.4 limiter

ケース:

```text
LimitFPS ON  / AudioSync ON
LimitFPS OFF / AudioSync ON
LimitFPS ON  / AudioSync OFF
LimitFPS OFF / AudioSync OFF
FastForward hold
FastForward toggle
SlowMo
```

記録:

```text
emu FPS
present FPS
queue backlog
stolen frames
dropped frames
producer wait
present mode
```

期待:

```text
両方OFF → producer unlimited policy
FastForward → Sapphire fast-forward policy
AudioSync ON →速度制限が残ることを明示
```

## 12.5 screen ownership

```text
boot menu
game main menu
match
pause
match end
screen transition
ScreenSwap config ON/OFF
natural／vertical／horizontal／hybrid
```

比較:

```text
Software
OpenGL Classic
Vulkan
Sapphire Android
```

## 12.6 regression

```text
display capture
screen swap during transition
temporal 3D history
custom top-only mode
touch coordinates
cursor confinement
screenshot
savestate
ROM reset
backend switch
```

---

# 13. 完了条件

```text
CustomHUDが既存CustomHud_Renderと同じrendererを使用
固定0.5 text scaleなし
bitmap glyph 1pixel＝1quadなし
10,000 quad capによる欠けなし

radarが既存CustomHUD radar rendererを使用
radar専用Sapphire presenter draw modeなし
source bottom／destination topを明確に分離

VulkanSurfacePresenter core／shaderがSapphire normalized parity
desktop overlayが別layer

fullscreenで同一native surfaceを破棄しない
new swapchain作成前にold swapchainを破棄しない
surface generationを不要に更新しない
数秒黒画面なし

isFastForwardActiveがruntime stateを返す
Sapphire realtime／late／fast-forward policyを使用
LimitFPSとAudioSyncの挙動を区別
toggle時にlimiter phase reset

packedTop／packedBottomがphysical screen contractに一致
hardware swapは1回だけ適用
user ScreenSwapはdestination layoutだけ変更
menu／matchで上下が一貫

Sapphire vendor parity CI成功
Software／OpenGL回帰なし
```

---

# 14. 禁止事項

```text
潰れたHUDのbitmap font sizeだけ調整
kMaxSolidQuadsだけ増加
radar shaderへ個別条件追加
menu時だけtop/bottomを反転
preparedFrameScreenSwapを無条件反転
screenKindをROM状態で書き換える
fullscreen時にrenderer全再生成
fullscreen時にFrameQueue clear
swapchain resizeでVkSurfaceKHRまで破棄
frame limiter修正をpresent mode変更だけで済ませる
Sapphire policyを参考に独自policyを再設計
Sapphire presenter shaderへさらにMelonPrime機能を追加
```

---

# 15. 最終指示

今回、試合中3Dがほぼ正常になったため、
Vulkan 3D rasterizerへ追加の場当たり修正を入れる段階ではない。

修正対象は:

```text
presentation ownership
desktop surface lifecycle
HUD overlay separation
runtime pacing bridge
physical screen contract
```

である。

最も重要な設計判断は次の2点。

```text
1. Sapphire presenterをゲーム画面提示へ戻し、
   MelonPrime HUD／radar／OSDを別overlay rendererへ分離する。

2. Sapphire GPU2D coreだけでなく、
   framebuffer ownership／front buffer publicationまで
   dependency closureとして移植し、
   screen swapを1箇所でのみ適用する。
```

これにより、CustomHUD、radar、fullscreen、limiter、menu screen swapを
個別の例外処理で直すのではなく、
Sapphireの責務境界とMelonPrime desktop adapterの責務境界を再確立できる。

---

# 21. 実施進捗 (S62)

| Phase | Commit | Hash | Status |
|---|---|---|---|
| S62-1 | Restore Sapphire Vulkan presenter shader contract | `431a964d1` | done |
| S62-2 | Add MelonPrime Vulkan overlay renderer | `fe701180d` | done |
| S62-3 | Reuse canonical CustomHud_Render for Vulkan | `716d2f5b9` | done |
| S62-4 | Make fullscreen Vulkan surface transitions non-destructive | `999e359fd` | done |
| S62-5 | Port Sapphire Vulkan frame queue policies | `d0cd46b00` | done |
| S62-6 | Reset frame limiter phase on runtime toggle | `b051b7dac` | done |
| S62-7 | Define Vulkan physical screen ownership contract | `86ebf4940` | done |
| S62-8 | Add Sapphire vendor parity CI | pending | in progress |

**Branch:** `highres_fonts_v3`  
**最新push:** S62-7 `86ebf4940`（S62-8 作業中）

## 検証メモ

- Windows MinGW release build: S62-5〜7 変更後ビルド成功（2026-07-15）
- ROM手動検証（§12）: 未実施 — CustomHUD／radar／fullscreen／limiter／screen swap

