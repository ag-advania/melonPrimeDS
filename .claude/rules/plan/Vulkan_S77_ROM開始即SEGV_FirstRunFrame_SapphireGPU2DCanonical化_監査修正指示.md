# Vulkan S77 監査・修正指示書
## ROM Open直後 First RunFrame Segmentation Fault
## Sapphire GPU2D Canonical Ownership化
## FrameQueue Adapter契約修正
## Android・Desktop差分をWSI／Resource Lifetimeだけに限定

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `399a5014d1c0a50b4ee79de3a019d79cc8cca925`  
**クラッシュログbuild:** `f2842ddcb41c9c881a55caef37dfec7bea738dd9`  
**直前監査HEAD:** `dca928511296a98ef60ca6334ef60548e254e653`  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire frontend commit:** `2c10e59d7209d354e90d9ef4228330bac3f6e794`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 結論

今回のクラッシュログでは、Vulkan初期化、surface登録、Renderer3D生成、Sapphire Vulkan 2D activation、Vulkan frontend session初期化まで完了している。

最後のログ:

```text
[RomBootTrace] first RunFrame begin
Segmentation fault
```

次のログは出ていない。

```text
first RunFrame complete
completeProducerTransaction
VulkanProducer queuePush
FrameLatch latch
VulkanOutput prepare
surface present
```

したがってクラッシュ区間は確定している。

```text
beginVulkanProducerFrame()完了
    ↓
NDS::RunFrame()開始
    ↓
最初のGPU／CPU event処理中
    ↓
SEGV
```

これは次の層より前である。

```text
- CompletedSapphireFrameTuple構築
- DesktopSapphireFrameInput検証
- Sapphire FrameLatch完成snapshot
- VulkanOutput composition preparation
- FrameQueue present candidate
- swapchain present
- splash hide
```

よって、今回の直接調査対象は:

```text
1. beginVulkanProducerFrameで行ったslot／resource準備による事前破壊
2. first scanlineで実行されるSapphire GPU2D dual-state path
3. VulkanRenderer3Dのfirst RenderFrame
```

である。

最も大きなSapphireとの差はGPU2D ownership。

固定Sapphireは:

```text
GPU
 ├─ GPU2D::Unit A
 ├─ GPU2D::Unit B
 └─ GPU2D::Renderer2D
```

をcanonical stateとして直接所有する。

現在のMelonPrimeDSは:

```text
GPU
 ├─ native melonDS GPU2D_A
 ├─ native melonDS GPU2D_B
 ├─ native outer SoftRenderer2D A/B
 └─ SapphireGpu2DState
      ├─ mirrored UnitA
      ├─ mirrored UnitB
      └─ Sapphire SoftRenderer
```

という二重stateである。

さらに:

```text
native register write
    → Sapphire Unitへforward

activation
    → native stateの一部をSapphire Unitへseed

各scanline
    → external stateだけ再sync

VBlank／Window
    → 別adapterで両方へforward
```

している。

これはSapphire exactではない。

今回の修正は、新しい同期heuristicを追加するのではなく:

```text
Sapphire GPU2D Unit A/Bをcanonical stateへする
```

こと。

FrameLatch、FrameQueue、VulkanOutputだけを上流化しても、
GPU2D coreが二重stateのままではSapphire exactにならない。

---

# 1. クラッシュログから確定できる時系列

## 1.1 正常完了している処理

```text
VulkanContext ready
Presenter pipeline cache load
surface生成
presenter登録
surface generation attach
ROM load
NDS Reset
cart install
NDS Start
MelonPrime OnEmuStart
Vulkan Renderer3D初期化
outer renderer install
Renderer3D install
Sapphire Vulkan activation
VulkanOutput init
frontend session init
renderer transaction complete
```

## 1.2 クラッシュ位置

EmuThreadの順序は次。

```cpp
bool vulkanProducerBegun =
    beginVulkanProducerFrame();

log("first RunFrame begin");

nlines = nds->RunFrame();

if (vulkanProducerBegun)
    completeVulkanProducerFrame();
```

ログでは`first RunFrame begin`の直後に落ちる。

よって:

```text
completeVulkanProducerFrame()
```

は実行されていない。

Sapphire FrameLatchの主要処理:

```text
GetVulkan3DFrameView
BuildCompletedSapphireFrameTuple
latchSoftPackedFrameSnapshot
prepareFrameForPresentation
```

は全てRunFrameの後。

今回のSEGV原因をFrameLatch completion logic、
strict serial check、
VulkanOutput compositorへ置いてはいけない。

---

# 2. ログbuildと最新HEADの差

クラッシュログ:

```text
commit=f2842ddcb41c
```

最新remote HEAD:

```text
399a5014d1c0a50b4ee79de3a019d79cc8cca925
```

差は1 commit。

そのcommitは主に:

```text
FrameQueue friend declaration
membership helperのclass member化
plan更新
```

であり、次を変更していない。

```text
EmuThread first RunFrame順序
Sapphire GPU2D activation
SoftRenderer::DrawScanline
UnitSync
VulkanRenderer3D first frame
producer resource準備
```

したがって最新HEADでの再実行は必要だが、
コード監査上はSEGVが解消された根拠はない。

---

# 3. first RunFrame前に実行済みのVulkan処理

`beginVulkanProducerFrame()`はRunFrame前に次を行う。

```text
temporal history gate更新
FrameQueue render slot取得
frame resource dimension validation
VulkanOutput frame resource確保
条件によりpre-run 3D snapshot
pendingProducerFrame設定
```

実行していないもの:

```text
published 2D取得
3D completed view取得
FrameLatch
composition input build
queue push
present
```

よってfirst RunFrameのSEGVを切り分けるには、
producer beginの成功／失敗を明示ログする必要がある。

現ログは:

```text
first RunFrame begin
```

だけであり、

```text
vulkanProducerBegun = true / false
ensureFrameResources result
acquired Frame pointer
```

が不明。

---

# 4. P0
# FrameQueueにはまだ確定した二重enqueueがある

S76でpresentation deferの二重所有は修正された。

しかしrender slotの返却経路に同種の問題が残る。

## 4.1 recycleRenderFrame

wrapper:

```cpp
lifetime.onRecycleRender(frame, core);
core.recycleRenderFrame(frame);
```

tracker側`onRecycleRender()`:

```cpp
retireFrameLocked(frame, core);
```

`retireFrameLocked()`は参照が無ければ:

```cpp
state = Free;
core.freeQueue.push(frame);
```

を行う。

その直後、Sapphire coreの`recycleRenderFrame()`も:

```cpp
freeQueue.push(frame);
```

する。

結果:

```text
同じFrame*がfreeQueueへ2回入る
```

---

## 4.2 discardRenderedFrame

wrapper:

```cpp
lifetime.onDiscardRendered(frame, core);
core.discardRenderedFrame(frame);
```

trackerの`onDiscardRendered()`は
`onRecycleRender()`を呼ぶため、既にfreeQueueへ入れる。

その後coreも:

```cpp
freeQueue.push(frame);
```

する。

これも二重enqueue。

---

## 4.3 first RunFrameとの関係

`beginProducerFrame()`で:

```text
ensureFrameResources失敗
```

するとRunFrame前に`discardRenderedFrame()`が呼ばれる。

EmuThreadはproducer beginがfalseでも
`NDS::RunFrame()`自体は実行する。

したがってクラッシュログの直前に:

```text
freeQueue duplicate
queue state mismatch
```

が作られた可能性を除外できない。

ただしqueue duplicateだけで
NDS内部が即SEGVするとはまだ確定できない。

必須ログ:

```text
[VulkanProducerBegin]
result
frame pointer
frameId
ensureFrameResources
queue invariant
```

---

# 5. P0
# Desktop lifetime trackerがSapphire queue内部を直接変更する

`DesktopFrameLifetimeTracker`はfriend accessで次を直接変更する。

```text
freeQueue
presentQueue
pendingPresentFrame
previousFrame
frames_
stats
```

さらに:

```text
FrameQueueState
historyReferences
presentationReferences
rendererGeneration
surfaceGeneration
```

をSapphire Frameに追加している。

固定Sapphire queueのselection後に:

```text
Desktop trackerがstateを再解釈
queueへ追加／削除
candidateを拒否
generation mismatchをdrop
```

する構造。

これはAndroid／Desktopのplatform差ではない。

queue ordering、
previous reuse、
drop policy、
slot ownershipを変更するalgorithm差である。

---

# 6. FrameQueueの即時修正

## 6.1 queue membershipを変更するownerを1つにする

productionでは:

```text
SapphireFrameQueueCoreだけが
freeQueue／presentQueue／pending／previousを変更
```

する。

Desktop trackerはqueueへpush／eraseしない。

---

## 6.2 recycle／discard

最小hotfix:

```cpp
void FrameQueue::recycleRenderFrame(Frame* frame)
{
    lifetime.beforeCoreRecycle(frame);
    core.recycleRenderFrame(frame);
    lifetime.afterCoreRecycle(frame);
}
```

Trackerはstate metadataだけ変更し、
freeQueueへpushしない。

同様にdiscard。

推奨最終形:

```text
FrameQueueからDesktop FrameQueueStateを削除
```

し、Vk resource lifetimeはFrame外のleaseへ移す。

---

# 7. P0
# first scanlineのSapphire GPU2Dはdual-state adapter

Vulkan 2D active時のouter SoftRenderer:

```cpp
SyncSapphireUnitsFromGPU2D();
AssignSapphireFramebuffers();

state.Renderer.DrawScanline(
    line, &state.UnitA);

state.Renderer.DrawScanline(
    line, &state.UnitB);
```

ここで使うUnit A/Bは、
NDS coreがcanonicalに更新するnative GPU2D_A/Bではない。

別object。

---

## 7.1 activation時seed

```cpp
SeedCompleteUnitFromNative(
    Sapphire UnitA,
    native GPU2D_A);

SeedCompleteUnitFromNative(
    Sapphire UnitB,
    native GPU2D_B);
```

コピーするもの:

```text
Enabled
DispCnt
BGCnt
BG position
affine refs
rotation matrix
window coordinates
mosaic size／一部counter
blend
brightness
capture／FIFO
```

コピーしない、または意味が異なるもの:

```text
DispCntLatch[3]
LayerEnable
OBJEnable
ForcedBlank
BGXRefReload
BGYRefReload
BGMosaicLatch
OBJMosaicLatch
native BGMosaicLine semantics
native OBJMosaicLine semantics
遅延register application state
```

---

## 7.2 scanline sync

各scanline前の`SyncUnitFromGPU2D()`は:

```cpp
SyncExternalGpuState(...)
```

だけ。

更新:

```text
Enabled
MasterBrightness
CaptureCnt
CaptureLatch
DispFIFO
FIFO pointers
```

更新しない:

```text
BG／OBJ registers
window state
affine internal state
mosaic state
delayed layer state
```

register write forwardingで差分を埋めようとしているが、
nativeとSapphireでregister lifecycleが同じではない。

---

## 7.3 Sapphire本家

固定Sapphireは:

```cpp
GPU2D::Unit GPU2D_A;
GPU2D::Unit GPU2D_B;
std::unique_ptr<GPU2D::Renderer2D>
    GPU2D_Renderer;
```

をGPUが直接所有する。

NDS register read/write、
VBlank、
window、
mosaic、
capture、
renderer drawが
同じUnit objectを使用する。

mirrorもUnitSyncもない。

---

# 8. first scanlineの高危険箇所

Sapphire SoftRendererの開始:

```cpp
CurUnit = unit;

int stride =
    GPU.GPU3D.IsRendererAccelerated()
        ? 769
        : 256;

u32* dst =
    &Framebuffer[CurUnit->Num]
        [stride * line];
```

続いて:

```text
CurrentUnitTargetsTopScreen
ClearStructuredVulkan2DLine
VRAM dirty state derive
MakeVRAMFlat coherent
DrawScanline_BGOBJ
mosaic update
capture
```

ここで次のどれかが不正ならfirst RunFrameで即SEGVする。

```text
Framebuffer[0／1]
CurUnit
CurUnit->Num
VRAM mapping／dirty adapter
native-to-Sapphire Unit state
structured plane state
renderer generation lifetime
```

現ログの症状と一致する最初の場所。

ただしstack traceがないため、
どの行かは未確定。

---

# 9. 必須 first-scanline trace

最初の1 frame／line 0だけ、次を出す。

```text
[FirstVulkanFrame]
producerBegin enter
producerBegin result
frame pointer
frameId
ensureResources result

[FirstGpuFrame]
GPU::StartFrame enter
VulkanFrameSerial
renderer generation

[FirstGpu2D]
DrawScanline enter
VCount
line argument
Sapphire active
active generation
GPU3D generation
UnitA pointer
UnitB pointer

[FirstGpu2D]
after SyncUnits
UnitA Num
UnitB Num
UnitA DispCnt
UnitB DispCnt

[FirstGpu2D]
after Bind
Framebuffer0
Framebuffer1
BackBuffer
stride

[FirstGpu2D]
before UnitA
after UnitA
before UnitB
after UnitB
```

flushを毎stageで行う。

これにより最初に欠けるログが
直接クラッシュstageになる。

productionではbudget終了後に出さない。

---

# 10. 必須Windows crash stack

現在の`Segmentation fault`だけでは
fault addressもcall stackも無い。

MinGW Debug buildで:

```text
-g3
-O0
-fno-omit-frame-pointer
```

を使用する。

最低限取得:

```text
exception address
thread ID
registers
call stack
source file／line
faulting pointer
```

候補手段:

```text
gdb --args melonPrimeDS.exe
run
bt full
thread apply all bt full
```

またはWindows DbgHelp minidump。

minidumpへBuildIdentityを含める。

---

# 11. 必須ASan／UBSan

LinuxまたはClang64のVulkan debug buildで:

```text
-fsanitize=address,undefined
-fno-omit-frame-pointer
```

を有効化。

初回ROM openだけを実行。

確認対象:

```text
heap buffer overflow
use-after-free
null dereference
misaligned access
invalid shift
object lifetime
queue duplicateによるresource reuse
```

Sapphire GPU2D arrayは大きいため、
stackではなくobject／heap lifetimeも確認する。

---

# 12. 原因を一回で分離するA/B gate

debug build限定。

## Test A
## Producer beginを止める

```text
Vulkan 3D ON
Sapphire 2D ON
beginVulkanProducerFrame OFF
NDS RunFrame ON
```

結果:

```text
SEGV継続
    → FrameQueue／VulkanOutput resource準備ではない
    → Sapphire GPU2D／Vulkan3D first frame

SEGV消失
    → producer beginのqueue／resource準備
```

---

## Test B
## Sapphire 2Dだけ止める

```text
Vulkan Renderer3D ON
Vulkan presenter ON
Sapphire 2D activation OFF
native Software 2D ON
```

結果:

```text
SEGV消失
    → dual-state Sapphire GPU2D path

SEGV継続
    → VulkanRenderer3D first RenderFrame
       またはproducer begin
```

---

## Test C
## 両方を独立評価

```text
producer ON / Sapphire2D OFF
producer OFF / Sapphire2D ON
producer OFF / Sapphire2D OFF
```

4象限で原因を分離する。

debug gateをproduction fallbackとして残さない。

---

# 13. P0
# Sapphire exact化の本命
# GPU2D Unitをcanonicalにする

## 13.1 vendor対象

固定Sapphireからdependency closureごと使用する。

```text
src/GPU2D.h
src/GPU2D.cpp
src/GPU2D_Soft.h
src/GPU2D_Soft.cpp
GPU.cppの:
    constructor
    Reset
    StartFrame
    StartScanline
    StartHBlank
    VBlank
    VBlankEnd
    framebuffer assignment
    renderer2D ownership
```

単一関数のcopyでは不十分。

GPU2D lifecycle全体をvendorする。

---

## 13.2 最終GPU構造

```cpp
GPU2D::Unit GPU2D_A;
GPU2D::Unit GPU2D_B;

std::unique_ptr<GPU2D::Renderer2D>
    GPU2D_Renderer;
```

renderer切替:

```cpp
GPU.SetRenderer2D(
    std::make_unique<
        GPU2D::SoftRenderer>(GPU));
```

Vulkanだから別Unitを作らない。

---

## 13.3 native renderer compatibility

Software／OpenGLのDesktop rendererが
現在のnative GPU2D typeへ依存する場合は、
Unit stateをmirrorしない。

次のどちらか。

### 推奨

Sapphire `GPU2D::Unit`を
MelonPrime build全体のcanonical GPU2D typeにする。

Software／OpenGL rendererを
そのUnit APIへ合わせる。

### 非推奨

native stateをcanonicalのまま
Sapphireへmirrorし続ける。

これは今回までの不具合を再発させる。

---

# 14. 削除対象

canonical migration完了後に削除。

```text
src/MelonPrimeSapphireGpu2DState.*
src/MelonPrimeSapphireGpu2DAdapter.*
src/SapphireGPU2DCore/UnitSync.*
GPU::ActivateSapphireVulkan2D
GPU::DeactivateSapphireVulkan2D
GPU::RefreshSapphireVulkanBindings
SyncSapphireUnitsFromGPU2D
ForwardSapphireGpu2DRegisterWrite*
ForwardSapphireGpu2DWindowCheck
ForwardVBlank
ForwardVBlankEnd
```

置換:

```text
GPU::SetRenderer2D
GPU::GetRenderer2D
canonical Unit lifecycle
```

---

# 15. framebuffer ownershipもSapphireへ合わせる

固定SapphireはGPUがframebufferを所有し、
Reset／AssignFramebuffersでRenderer2Dへbindingする。

現在はouter SoftRendererがbufferを所有し、
GPUへraw pointerをpublicationする。

Sapphire exact化後:

```text
GPU owns framebuffer
Renderer2D borrows backbuffer
Frame publication borrows completed frontbuffer
```

へ統一する。

Desktop adapterは:

```text
physical top／bottom pointerをFrontendへ公開
```

するだけ。

engine mappingやbuffer ownershipを再解釈しない。

---

# 16. safety invariant

canonical移行前にも追加する。

```cpp
u32* Framebuffer[2]{};
```

Draw前:

```cpp
if (Framebuffer[0] == nullptr
    || Framebuffer[1] == nullptr)
{
    Log fatal binding error;
    return false / abort debug;
}
```

activation条件:

```text
Renderer2D bound
Unit A/B valid
renderer generation一致
outer buffer size >= 769*192
```

binding成功後にだけactive flagを立てる。

現在は:

```text
AssignSapphireFramebuffers();
Sapphire2D->Activate();
```

だが、Assignが成功したかを返さない。

修正:

```cpp
if (!BindSapphireFramebuffers())
    return false;

Sapphire2D->Activate(...);
```

---

# 17. FrameQueue exact化の最終形

Sapphire Coreはそのまま使用。

Desktop側:

```text
queue selectionへ介入しない
queue containerを直接変更しない
```

Vk resource lifetimeは:

```text
DesktopVulkanResourceLease
```

だけが管理。

Frame slotがSapphire Core上で再利用されても、
旧VkImage／semaphore／timelineはlease objectが保持する。

Frame structから最終的に削除候補:

```text
FrameQueueState
historyReferences
presentationReferences
```

これらはSapphire queue semanticsではない。

---

# 18. P0
# current cold-start testは実起動testではない

`test_sapphire_vulkan_cold_start_s76.py`は:

```text
logging文字列の存在
tuple builder名の存在
binary --help
```

だけを検査する。

実行しない。

```text
Vulkan renderer選択
ROM open
NDS RunFrame
first scanline
first queue push
first present
splash hide
```

今回のSEGVを検出できない。

---

# 19. 実cold-start test

ライセンス上利用可能なhomebrew／synthetic ROMを使用。

実行条件:

```text
fresh config
3D.Renderer=Vulkan
Screen presenter=Vulkan
previous renderer activationなし
```

必須checkpoint:

```text
renderer activation
producer begin
first RunFrame begin
first RunFrame complete
producer complete
FrameLatch valid
queuePush
PresentedGameFrame
splash hidden
```

timeoutだけでなくprocess exit codeを確認。

```text
SEGV／access violation = fail
```

artifact:

```text
stdout
stderr
minidump
first-frame trace
queue invariant dump
first screenshot
```

---

# 20. Golden／flicker fixture残課題

現在のGolden binaryはPythonがsynthetic byte列を作ったもの。

固定Sapphire coreとDesktop coreを
実際には実行していない。

120-frame hashも:

```text
sha256(
  golden_digest
  + frame_index
  + "static2d")
```

であり、renderer outputではない。

Sapphire exactの証拠に使えない。

修正:

```text
pinned Sapphire compiled core
Desktop compiled core
```

へ同一inputを渡し、
実outputを比較する。

---

# 21. CI状態

最新監査HEADについて、
GitHub connector上で:

```text
combined statusなし
workflow runなし
```

だった。

workflow sourceにjobが存在することと、
そのcommitで成功したことは別。

planへ完了を記録する条件:

```text
commit SHA
workflow run URL
job URL
artifact SHA-256
```

を保存すること。

---

# 22. 推奨commit分割

## S77-1

```text
Log the producer-begin result and first GPU scanline stages
```

## S77-2

```text
Add Windows first-frame minidump and symbolized stack capture
```

## S77-3

```text
Fix duplicate freeQueue insertion in recycle and discard paths
```

## S77-4

```text
Make SapphireFrameQueueCore the sole owner of queue membership
```

## S77-5

```text
Move Desktop Vulkan lifetime tracking completely outside queue containers
```

## S77-6

```text
Add debug-only producer/Sapphire2D four-way isolation gates
```

## S77-7

```text
Verify framebuffer binding before enabling Sapphire rendering
```

## S77-8

```text
Vendor pinned Sapphire GPU2D Unit and Renderer2D dependency closure
```

## S77-9

```text
Make Sapphire GPU2D Unit A/B canonical in MelonPrime Vulkan builds
```

## S77-10

```text
Remove native-to-Sapphire UnitSync and register forwarding
```

## S77-11

```text
Restore pinned Sapphire GPU framebuffer assignment lifecycle
```

## S77-12

```text
Adapt Software and OpenGL 2D renderers to the canonical Unit API
```

## S77-13

```text
Add a real executable Vulkan ROM cold-start integration test
```

## S77-14

```text
Run cold-start under ASan/UBSan and Windows minidump CI
```

## S77-15

```text
Replace synthetic golden and flicker hashes with compiled core outputs
```

---

# 23. 実装順

```text
1. S77-1
2. S77-2
3. 最新HEADでstack取得
4. S77-3
5. S77-4
6. S77-6のA/B test
7. direct crash sideを確定
8. S77-7
9. S77-8
10. S77-9
11. S77-10
12. S77-11
13. cold start確認
14. S77-5
15. S77-12
16. S77-13
17. S77-14
18. S77-15
```

Sapphire canonical移行前に
新しい2D補正algorithmを追加しない。

---

# 24. 禁止事項

```text
- SEGVをcatchしてSoftwareへsilent fallback
- 最初の数frameだけSapphireを無効化して製品化
- first frameをsleepで遅延
- null pointerを握り潰して継続
- UnitSyncへさらにfield copyを追加
- register write forwardingの例外を増やす
- FrameQueue CoreとTrackerの両方がfreeQueueへpush
- queue candidateをDesktop側で別frameへすり替える
- FrameLatch／VulkanOutputにSEGV対策を入れる
- source文字列testをcold-start testと呼ぶ
- synthetic文字列hashをframe hashと呼ぶ
- stack traceなしでfault lineを断定する
- Android／Desktop差としてGPU2D semanticsを変更する
```

---

# 25. 完了条件

```text
1. Vulkan選択状態でROM openしてSEGVしない
2. first RunFrame completeへ到達
3. first producer completionへ到達
4. first PresentedGameFrameへ到達
5. splashが消える
6. queue duplicate pointer = 0
7. free／present／pending／previous二重membership = 0
8. framebuffer binding null = 0
9. native GPU2DとSapphire Unitの二重state = 0
10. UnitSync／register forwarding = 0
11. GPU2D lifecycleがpinned Sapphireと一致
12. FrameLatch algorithmがpinned Sapphireと一致
13. FrameQueue selectionがpinned Sapphireと一致
14. VulkanOutput composition decisionがpinned Sapphireと一致
15. Windows／Linux実process cold-start test成功
16. ASan／UBSan error = 0
17. Android／Desktop差がWSI／loader／resource lifetime／HUDだけ
```

---

# 26. 最終判断

S76でpresentation deferの明白な二重所有は修正された。

しかし今回のSEGVにより、
より根本のSapphire parity不足が明確になった。

```text
FrameLatch:
    Sapphire generated core

FrameQueue selection:
    Sapphire generated core

GPU2D lifecycle:
    native state + Sapphire mirror
    custom UnitSync
    custom register forwarding
```

つまりpipelineの入口だけは
まだSapphireそのものではない。

今回のログが落ちる場所も、
その入口であるfirst `NDS::RunFrame()`内。

修正の中心は:

```text
Sapphire GPU2Dをcanonical stateとして
そのまま使用する
```

こと。

AndroidかDesktopかの差は、
GPU2D register／scanline／capture semanticsではない。

差として許容するのは:

```text
surface
Vulkan loader
queue-family
timeline resource lifetime
window lifecycle
CustomHUD final pass
```

だけ。

これ以上mirror同期を増やすより、
Sapphire GPU2D dependency closureをそのまま採用する方が、
修正量・回帰リスク・将来保守の全てで小さい。

---

# 27. 進捗記録

| Phase | Commit | Status | Summary |
|-------|--------|--------|---------|
| S77-1 | `cc6d45de1` | done | `[FirstVulkanFrame]`/`[VulkanProducerBegin]`/`[FirstGpuFrame]`/`[FirstGpu2D]` trace budget |
| S77-2 | `42e806b21` | done | DbgHelp minidump + `.crash.txt` stack + `build-mingw-vulkan-debug.bat` |
| S77-3 | `7692c364a` | done | Tracker recycle/discard no longer double-enqueue freeQueue |
| S77-4 | `12961dbd6` | done | Core owns freeQueue rebuild/sanitize/reset; pushRendered mismatch guard |
| S77-5 | (pending) | pending | Move Vulkan lifetime fully outside queue containers |
| S77-6 | `1fab5d513` | done | Debug env gates for producer begin / Sapphire 2D |
| S77-7 | `40a51b45f` | done | `AssignSapphireFramebuffers()` binding validation |
| S77-8 | (pending) | in progress | Vendor pinned GPU2D.h/cpp upstream snapshots in manifest |
