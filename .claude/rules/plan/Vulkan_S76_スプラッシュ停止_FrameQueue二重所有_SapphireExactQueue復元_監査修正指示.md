# Vulkan S76 監査・修正指示書
## Vulkan選択時スプラッシュ停止
## FrameQueue二重所有／初回Present Defer破壊
## Sapphire Exact Queue復元・Desktop Resource Lease分離

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `dca928511296a98ef60ca6334ef60548e254e653`  
**前回監査HEAD:** `d489c6678a4ea7b7539dc7da05fbdd21b197fc3e`  
**差分:** 16 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire frontend commit:** `2c10e59d7209d354e90d9ef4228330bac3f6e794`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 監査結論

S75では次が改善された。

```text
- SapphireFrameLatchCoreをOBJECTとして一度だけcompileし、
  同じobjectをproduction melonDSへlink
- Sapphire FrameQueue selection coreを固定Sapphireから生成
- DesktopFrameLifetimeTrackerを追加
- VulkanOutput::prepareFrameForPresentationの
  固定region比較を追加
- published 2D pointer／serial／generation検証を強化
- Linux Debug／ReleaseとWindows build jobを追加
```

しかし、現在報告されている:

```text
Vulkanを選ぶとスプラッシュスクリーンから進まない
```

症状と一致する**確定したFrameQueue所有権バグ**がある。

最重要結論:

```text
SapphireFrameQueueCoreそのもの:
    固定Sapphire由来

Desktop wrapper:
    Sapphire queue contractを破壊

結果:
    同じFrame slotが
    freeQueueとpresentQueueへ同時所属できる
```

したがって現在は:

```text
Sapphire FrameQueueを使っている
```

のではなく:

```text
Sapphire FrameQueueの前後で
Desktop trackerがqueue ownershipを再解釈している
```

状態である。

今回のスプラッシュ停止について最も確度が高い原因は:

```text
初回present失敗／defer
    ↓
Frame slotをfreeQueueへ返す
    ↓
同じFrame slotをpresentQueueへ再投入
    ↓
producer／presenter二重所有
    ↓
PresentedGameFrameが成立しない
    ↓
hasPresentedFrame()がfalseのまま
    ↓
スプラッシュoverlayが消えない
```

である。

新しいpixel、capture、class4 heuristicは追加しない。

---

# 1. S75で正しく改善された点

## 1.1 FrameLatch compiled object

現在は:

```cmake
add_library(sapphire_frame_latch_core OBJECT
    SapphireGenerated/SapphireFrameLatchCore.cpp)

target_sources(melonDS PRIVATE
    $<TARGET_OBJECTS:sapphire_frame_latch_core>)
```

となっている。

前回の:

```text
監査用OBJECTとproduction objectが別
```

という問題は解消された。

これは維持する。

---

## 1.2 FrameQueue selection coreのvendor化

`SapphireGenerated/SapphireFrameQueueCore.cpp`は、
固定SapphireのFrameQueue.cpp regionから生成される。

generatorの主要変換:

```text
FrameQueue::
    → SapphireFrameQueueCore::

frames
    → frames_
```

queue policy、ordering、previous reuse、defer、drop処理の
中核は固定Sapphire由来である。

この生成Core自体は維持してよい。

問題はCoreの外側にある
`DesktopFrameLifetimeTracker`との合成方法である。

---

## 1.3 FrameLatch algorithm本体

S75差分ではFrameLatch algorithm本体への新しい手編集は見られない。

したがって:

```text
Sapphire FrameLatch exact化
```

は維持されている。

今回のスプラッシュ停止をFrameLatchのpixel heuristicで
修正してはいけない。

---

# 2. P0 根本原因
# Present deferで同じframeを二重queueする

現在のwrapper:

```cpp
void FrameQueue::deferPresentedFrame(
    Frame* frame,
    const FrameQueuePolicy& policy)
{
    impl_->lifetime.onPresentationDeferred(
        frame, impl_->core);

    impl_->core.deferPresentedFrame(
        frame, policy);
}
```

`onPresentationDeferred()`:

```cpp
if (frame->presentationReferences != 0)
    frame->presentationReferences--;

retireFrameLocked(frame, core);
```

`retireFrameLocked()`:

```cpp
if (historyReferences == 0
    && presentationReferences == 0)
{
    transition frame → Free;
    core.freeQueue.push(frame);
}
```

その直後、固定Sapphire Coreの
`deferPresentedFrame()`は:

```cpp
presentQueue.push_back(pendingPresentFrame);
pendingPresentFrame = nullptr;
```

を実行する。

結果:

```text
同じFrame*
    ├─ freeQueue
    └─ presentQueue
```

へ同時に入る。

これはqueue ownership invariant違反である。

---

# 3. スプラッシュ停止までの実行経路

`ScreenPanelVulkan::presentOnGuiThread()`は:

```cpp
Frame* frame = session.acquirePresentFrame();

if (frame->surfaceGeneration
    != surfaceHost.generation())
{
    session.deferPresentedFrame(frame);
    return;
}

const auto result =
    session.presentAcquiredFrame(...);

if (result == PresentedGameFrame)
    session.commitPresentedFrame(frame);
else
    session.deferPresentedFrame(frame);
```

初回swapchainでは次が通常発生し得る。

```text
surface generation transition
out-of-date
suboptimal
swapchain rebuild
bounded timeout
first surface not ready
invalid composition input
```

これらは全てdefer経路へ入る。

一度でも初回frameがdeferされると、
現在の二重queueバグが成立する。

次のproducerは`freeQueue`からそのframeを取得できる一方、
presenterは`presentQueue`から同じframeを取得できる。

その後:

```text
Rendering
Ready
pendingPresent
previousFrame
freeQueue
```

の状態が同じpointer上で衝突する。

---

# 4. スプラッシュが消えない理由

`syncNoRomSplashOverlay()`は:

```cpp
const bool hasPresentedGameFrame =
    vulkanFrontendSession().hasPresentedFrame();

const bool showSplash =
    !emuThread
    || !emuThread->emuIsActive()
    || !hasPresentedGameFrame;
```

で判定する。

`hasPresentedFrame()`がtrueになるのは、
`commitPresentedFrame()`により
`lastPresentedSerial != 0`になった後。

queue ownershipが壊れて
`PresentedGameFrame`が成立しなければ:

```text
lastPresentedSerial = 0
```

のままなので、
スプラッシュoverlayが永久に表示される。

今回の症状と直接一致する。

---

# 5. P0
# Presentation state transitionが実装されていない

`onPresentationAcquired()`は現在:

```cpp
frame->presentationReferences++;
```

だけ。

必要な:

```text
Ready
    → AcquiredForPresentation

Previous
    → AcquiredForPresentation
```

遷移を行わない。

一方、`synchronizePresentationCompletion()`は:

```cpp
if (frame.queueState()
    == AcquiredForPresentation)
{
    continue;
}
```

という分岐を持つ。

しかし実際には
`AcquiredForPresentation`へ遷移するコードがない。

つまりstate machineは内部的に整合していない。

---

# 6. P0
# Commit時にlifetime trackerが呼ばれない

現在:

```cpp
void FrameQueue::commitPresentedFrame(...)
{
    impl_->core.commitPresentedFrame(...);
}
```

だけ。

`DesktopFrameLifetimeTracker`には
commit callbackが存在しない。

したがってcommit時に:

```text
AcquiredForPresentation
    → Previous

presentation timeline valueの確定
presentationReferencesのownership更新
```

が行われない。

Sapphire CoreはDesktopのstate／ref countを知らないため、
wrapper側で明確に完結させる必要がある。

現在はCoreとTrackerの責務が半分ずつになっている。

---

# 7. P0
# Resyncがlifetime trackerを完全に迂回する

現在:

```cpp
void FrameQueue::requestPresentationResync()
{
    impl_->core.requestPresentationResync();
}
```

固定Sapphire Coreのresyncは:

```text
presentQueue frameをfreeQueueへ移す
pendingPresentFrameをfreeQueueへ移す
previousFrameをfreeQueueへ移す
```

しかしDesktopではframeが:

```text
historyReferences > 0
presentationReferences > 0
GPU timeline未完了
```

の場合がある。

Trackerを通さずCoreへ直接resyncすると、
参照中frameをfreeQueueへ戻せる。

`requestPresentationResync()`は次で呼ばれる。

```text
completeBackendSwitch()
beginGeneration()
beginSurfaceGeneration()
```

つまりVulkan初期化・surface作成時に
この危険経路を通る。

`requestFastForwardPresentationTransition()`も同様に
Trackerを迂回している。

---

# 8. P0
# Coreが選択したframeを拒否した後のrollbackが不完全

`getRenderFrame()`:

```cpp
Frame* frame = core.getRenderFrame();

if (!lifetime.allowRenderAcquisition(frame))
{
    lifetime.undoRenderAcquisition(frame, core);
    return nullptr;
}
```

固定Sapphire Coreはすでに:

```text
freeQueue／presentQueueからframeを除去
frameIdを更新
drop statsを更新
```

している。

その後Trackerが拒否しても、
Coreが行った選択結果を完全には巻き戻せない。

`getPresentCandidate()`も:

```cpp
Frame* frame =
    core.getPresentCandidate();

if (!allowPresentationAcquisition(frame))
    return nullptr;
```

だけ。

この場合:

```text
core.pendingPresentFrame
```

は設定されたまま。

次回Coreは同じpending frameを返すが、
presentation referenceが残っているため
wrapperは再びnullを返し続ける可能性がある。

Sapphire selectionの後にDesktop側で候補を拒否する設計自体が危険。

---

# 9. P0
# Core mutexとTracker操作がatomicでない

`SapphireFrameQueueCore`は内部に:

```cpp
std::mutex frameLock;
```

を持つ。

Core methodはlockを取得する。

一方、Trackerはfriend accessで:

```text
freeQueue
presentQueue
pendingPresentFrame
previousFrame
stats
frames_
```

を直接変更するが、
同じ`frameLock`を取得しない。

wrapperは:

```text
Tracker処理
Core処理
```

を別transactionとして実行する。

FrontendSessionのstateMutexで多くの呼び出しは直列化されるが、
FrameQueue単体のthread safety contractは失われている。

また:

```text
test harness
shutdown
surface callback
future refactor
```

からの利用でdata raceが発生し得る。

queue ownership変更は
1つのmutex transaction内で行う必要がある。

---

# 10. P0
# Differential testが今回のバグを検査しない

Harnessの「defer」test:

```cpp
Frame* firstCandidate =
    wrapper.getPresentCandidate(...);

Frame* secondCandidate =
    wrapper.getPresentCandidate(...);

return secondCandidate == nullptr;
```

実際には:

```text
deferPresentedFrame()
```

を一度も呼んでいない。

さらにこのtestは:

```text
2回目のcandidateがnull
```

というSapphireとの差を正しい挙動として固定している。

固定Sapphire Coreは
`pendingPresentFrame`があれば同じpointerを返す。

したがってこのtestは:

```text
upstream-vs-Desktop differential parity
```

ではなく:

```text
Desktop divergence contract
```

になっている。

---

# 11. P0
# CIでDifferential Harnessを実行していない

Python testは:

```python
C:/msys64/mingw64/bin/cmake.exe
```

が存在しない場合:

```python
skipTest(...)
```

になる。

Ubuntu parity jobでは必ずskipされる。

Linux build jobは:

```text
sapphire_frame_queue_differential_test
```

をbuildするが、実行しない。

したがって今回のqueue ownershipバグは
CIで検出されない。

修正:

```yaml
- name: Run FrameQueue differential test
  run: ./build/.../sapphire_frame_queue_differential_test
```

をLinuxとWindowsの両方へ追加する。

---

# 12. P0
# S75 smoke testは実起動試験ではない

現在のcold-start smoke testは:

```text
main.cppにMELONPRIME_ENABLE_VULKANがある
main.cppにLogBuildIdentityがある
VulkanSurfacePresenter文字列がある
toggleFullscreen文字列がある
```

ことを検査するだけ。

実際には:

```text
process起動
Vulkan選択
surface作成
ROM起動
frame生成
PresentedGameFrame
splash hide
```

を行わない。

今回のスプラッシュ停止を検出できない。

test名を:

```text
source_contract
```

へ変更するか、
本当にprocessを起動する。

---

# 13. P0
# Golden fixtureはSapphire出力ではない

`generate_sapphire_latch_fixtures.py`はPythonで:

```text
synthetic plane bytearray
synthetic capture3d byte sequence
synthetic line mask
synthetic stats
```

を連結してbinaryを作る。

固定Sapphire Coreも、
Desktop generated Coreも実行しない。

metadataへSapphire commitを書いているだけで、
そのcommitから出力されたfixtureではない。

したがって:

```text
Android Sapphire output
    == Desktop output
```

は証明されない。

---

# 14. P0
# 120-frame hashはrenderer frame hashではない

各frame hashは:

```python
sha256(
    f"{golden_digest}:{frame_index}:static2d"
)
```

で生成される。

hash対象は:

```text
framebuffer
Snapshot
VulkanOutput
compositor output
```

ではない。

frame_indexが異なるため120個のhashが生成されるが、
これは実アニメーションでも静止sceneでもない。

period-2検査はrendererの点滅を一切観測しない。

---

# 15. P1
# Published/live完全一致が初回frameを永久rejectする可能性

`BuildDesktopSapphireFrameInput()`は次を全て必須にする。

```text
published frontBuffer
    == live frontBuffer

published screenSwap
    == live screenSwap

published emulatedFrameSerial
    == Vulkan3DFrameView.FrameSerial

published rendererGeneration
    == 3D generation

published packed pointer
    == live framebuffer pointer

published structured pointer
    == live Sapphire renderer pointer
```

FrameLatch固定Sapphire APIは:

```text
Frame*
frontBuffer
screenSwap
structured enabled
```

を受け取り、
Desktop独自publicationとの完全一致を要求しない。

Desktop publicationと3D viewを別々に取得する以上、
初回activation、surface generation変更、backend switchで
一時的に1frame差が生じ得る。

その場合:

```text
emulatedSerialMismatch
publishedLiveFrontBufferMismatch
publishedLiveScreenSwapMismatch
publishedLivePackedPointerMismatch
publishedLiveStructuredPointerMismatch
```

として毎frame破棄される可能性がある。

これは今回の停止の第二候補。

---

# 16. Atomic frame tupleへ統一

現在:

```text
2D publicationを読む
live GPU frontBufferを読む
live GPU screenSwapを読む
3D frame viewを読む
```

という複数sourceを後から照合している。

修正後:

```cpp
struct CompletedSapphireFrameTuple
{
    FrameSerial;
    RendererGeneration;
    FrontBuffer;
    ScreenSwap;

    Published2DView;
    Vulkan3DFrameView;
};
```

をGPU frame completion境界で一度だけpublishする。

Frontendはこのtupleのみを使用する。

禁止:

```text
tuple取得後にlive GPU stateを再読して
hard rejectする
```

generation違いはframe全体をrejectしてよい。

しかし同じframe内の2D／3Dは、
最初から同一transactionでpublishする。

---

# 17. 最優先の復旧方針

## 推奨
# DesktopFrameLifetimeTrackerをqueue ownershipから外す

固定Sapphire FrameQueueをそのまま使用する。

```text
SapphireFrameQueueCore
    queue ordering
    pending
    previous
    defer
    drop
    backlog

DesktopVulkanResourceLease
    VkImage lifetime
    timeline completion
    surface generation
    renderer generation
```

FrameQueueのpointer membershipを
Desktop lifetime管理で変更しない。

---

# 18. Desktop lifetimeの正しい置き場所

既にVulkanOutputには:

```cpp
waitBeforePackedBufferOverwrite(...)
```

がある。

Frame slot再利用時は:

```text
選ばれたslotのresource completionを待つ
またはresource generationを新規確保する
```

べき。

queueから別frameを選び直してはいけない。

Presenter側は:

```cpp
struct DesktopPresentationLease
{
    VkImage;
    VkImageView;
    VkSemaphore;
    timelineValue;
    rendererGeneration;
    surfaceGeneration;
};
```

をsubmission単位で保持する。

FrameQueue slotが再利用されても、
leaseがtimeline完了までresourceを保持する。

これにより:

```text
historyReferences
presentationReferences
HistoryReferenced queue state
```

をFrameQueueから削除できる。

---

# 19. Immediate hotfix

完全分離までの一時修正として、
最低限次を行う。

## 19.1 Acquire

```cpp
void onPresentationAcquired(
    Frame* frame,
    SapphireFrameQueueCore& core)
{
    if (frame == core.previousFrame)
        transition Previous
            → AcquiredForPresentation;
    else
        transition Ready
            → AcquiredForPresentation;

    frame->presentationReferences++;
}
```

---

## 19.2 Defer

`onPresentationDeferred()`で
`retireFrameLocked()`を呼ばない。

```cpp
decrement presentation ref;

transition:
    AcquiredForPresentation
        → Ready
```

その後Coreの`deferPresentedFrame()`が
frameをpresentQueueへ戻す。

freeQueueへ入れてはいけない。

---

## 19.3 Commit

追加:

```cpp
void onPresentationCommitted(
    Frame* frame,
    SapphireFrameQueueCore& core)
```

Core commitと同一transactionで:

```text
AcquiredForPresentation
    → Previous

presentationReferences:
    submission完了まで1

presentTimelineValue:
    actual submission timeline
```

を設定する。

---

## 19.4 Resync

禁止:

```cpp
core.requestPresentationResync();
```

だけを直接呼ぶこと。

Tracker-aware resyncを追加する。

```text
queue membershipを解除
timeline未完了resourceはretired leaseへ移す
Core queueをSapphire semanticsでreset
```

同じframeをfreeQueueへ複数回入れない。

---

# 20. Queue invariant checker

Debug／test buildへ追加する。

各Frame*についてmembershipを数える。

```text
freeQueue
presentQueue
pendingPresentFrame
previousFrame
rendering owner
history/resource lease
```

必須:

```text
queue membership <= 1
```

例:

```cpp
assert(
    freeCount
    + presentCount
    + pendingCount
    + previousCount
    <= 1);
```

追加検査:

```text
freeQueueに重複pointerなし
presentQueueに重複pointerなし
pendingPresentFrameはpresentQueueに存在しない
previousFrameはfreeQueueに存在しない
Rendering frameはどのqueueにも存在しない
```

---

# 21. First-present failure injection test

今回のバグを直接再現する。

sequence:

```text
1. getRenderFrame
2. validate
3. pushRenderedFrame
4. getPresentCandidate
5. simulate SurfaceNotReady
6. deferPresentedFrame
7. getRenderFrame
8. pushRenderedFrame
9. getPresentCandidate
10. commitPresentedFrame
```

各stepでinvariantを検査する。

failure resultを全て試す。

```text
InvalidFrameInputs
Timeout
OutOfDate
Suboptimal
SurfaceNotReady
SurfaceGenerationMismatch
SwapchainRecreated
```

完了条件:

```text
同一frame pointerの二重membership = 0
最終PresentedGameFrame成立
```

---

# 22. Real Vulkan cold-start test

実processを起動する。

条件:

```text
3D.Renderer = Vulkan
Screen backend = Vulkan
cold configuration
no previous OpenGL／Software activation
```

test:

```text
application start
surface attach
ROM boot
first completed 2D publication
first completed 3D view
first queued frame
first PresentedGameFrame
splash overlay hide
```

timeout例:

```text
10 seconds
または
first 10 emulated frames
```

保存artifact:

```text
stdout/stderr log
frame serial sequence
generation sequence
queue membership dump
first 10 Snapshot hashes
first 10 final frame hashes
screenshot
```

---

# 23. Logging追加

起動時だけbudget付きで記録する。

```text
[VulkanColdStart]
activeGeneration
surfaceGeneration
published2D serial
3D serial
frontBuffer
screenSwap
buildResult rejectReason
queue frameId
queue membership
present result
defer reason
commit
splash visible
```

`BuildDesktopSapphireFrameInput()`のreject countを
reason別に集計する。

```text
invalidPublishedFrame
emulatedSerialMismatch
publishedGenerationMismatch
publishedLiveFrontBufferMismatch
publishedLiveScreenSwapMismatch
publishedLivePackedPointerMismatch
publishedLiveStructuredPointerMismatch
```

同じreasonが3frame連続したら
1回だけError logを出す。

---

# 24. Sapphire exactの最終構造

```text
Pinned Sapphire
    ├─ SapphireFrameLatchCore
    ├─ SapphireFrameQueueCore
    ├─ VulkanOutput composition decisions
    └─ shaders

Desktop platform layer
    ├─ Qt VkSurfaceKHR
    ├─ volk
    ├─ Vk queue family barriers
    ├─ timeline resource lease
    ├─ renderer／surface generation validation
    ├─ fullscreen lifecycle
    └─ CustomHUD final pass
```

Desktop platform layerは:

```text
queue ordering
previous frame selection
defer queue position
screen owner
capture owner
class4
temporal threshold
```

を変更してはいけない。

---

# 25. VulkanOutput parity残課題

現在のvendor manifestが厳密比較するのは:

```text
prepareFrameForPresentation
```

の1 regionだけ。

次はまだwhole-body exact比較ではない。

```text
dispatchCompositor
buildCompositionInputs
updatePreparedCapture3dSource
accumulator update
previous composed replay
temporal reference release
```

platform barrier置換だけをhook化し、
composition decision regionを追加比較する。

---

# 26. Fixture修正

## 26.1 Golden

PythonでSnapshotらしいbytesを作るのをやめる。

同じsynthetic GPU inputを:

```text
A. pinned Sapphire compiled core
B. Desktop generated core
```

へ入力する。

実際のC++ outputをbinary化する。

比較:

```text
全Snapshot field
全plane
全line meta
capture source
capture mask
stats
valid
frontBuffer
screenSwap
```

---

## 26.2 120-frame

各frameで実際にhashする。

```text
Sapphire latch output
Desktop latch output
VulkanOutput prepared input
final top RGBA
final bottom RGBA
```

Pythonの:

```text
golden_digest:index:static2d
```

文字列hashは禁止。

---

# 27. CI修正

Linux:

```yaml
build production melonDS
run sapphire_frame_queue_differential_test
run Vulkan cold-start headless/integration test
```

Windows:

```yaml
build production melonPrimeDS.exe
run queue differential test
run Vulkan cold-start integration test
```

CI成功証跡:

```text
commit SHA
workflow URL
job URL
artifact SHA-256
```

をplan progressへ記録する。

現在の監査HEADについて
combined status／PR workflow run成功証跡は確認できない。

---

# 28. 推奨commit分割

## S76-1

```text
Fix presentation defer ownership so frames cannot enter free and present queues simultaneously
```

## S76-2

```text
Add presentation acquired and committed state transitions
```

## S76-3

```text
Route presentation resync and fast-forward transitions through lifetime-safe cleanup
```

## S76-4

```text
Add FrameQueue membership invariant assertions
```

## S76-5

```text
Add first-present failure injection coverage
```

## S76-6

```text
Move Vulkan submission lifetime out of FrameQueue into DesktopPresentationLease
```

## S76-7

```text
Remove HistoryReferenced and presentation reference semantics from Sapphire queue selection
```

## S76-8

```text
Publish one atomic completed Sapphire 2D/3D frame tuple
```

## S76-9

```text
Replace source-contract cold-start test with an executable Vulkan cold-start test
```

## S76-10

```text
Replace synthetic golden bytes with compiled Sapphire-versus-Desktop output fixtures
```

## S76-11

```text
Replace synthetic 120-frame hashes with real compositor output hashes
```

## S76-12

```text
Run queue differential and cold-start tests on Linux and Windows CI
```

## S76-13

```text
Expand VulkanOutput exact regions beyond prepareFrameForPresentation
```

---

# 29. 実装順

```text
1. S76-1
2. S76-2
3. S76-3
4. Vulkan cold startを手動確認
5. S76-4
6. S76-5
7. S76-6
8. S76-7
9. S76-8
10. S76-9
11. S76-10
12. S76-11
13. S76-12
14. S76-13
```

スプラッシュ停止復旧前に
Golden／flicker test整備を先行しない。

---

# 30. 完了条件

```text
1. Vulkan cold startでスプラッシュが消える
2. 他renderer経由なしでfirst PresentedGameFrame成立
3. 初回presentを強制deferしても復旧
4. freeQueue／presentQueue二重所属 = 0
5. queue内重複pointer = 0
6. pending／previous／free ownership衝突 = 0
7. FrameQueue selection sequenceが固定Sapphireと一致
8. Desktop lifetimeはqueue orderingを変更しない
9. source-contractではなく実process cold-start test
10. binary fixtureがcompiled Sapphire output由来
11. 120-frame hashが実compositor output由来
12. Linux／Windowsでtest binaryを実行
13. FrameLatch algorithm差 = 0
14. VulkanOutput composition decision差 = 0
15. Android／Desktop差がWSI／loader／resource lifetime／HUDだけ
```

---

# 31. 最終判断

S75では:

```text
Sapphire FrameQueue selection coreを生成する
```

ところまでは正しい。

しかし:

```text
Coreが選択
    ↓
Desktop trackerが後から拒否／retire
    ↓
Coreがdefer／resync
```

という二重ownership layerを作ったことで、
Sapphireのqueue contractが破壊された。

今回のスプラッシュ停止は、
Sapphire algorithm不足ではない。

```text
Desktop adapterが
Sapphire FrameQueueの所有権を壊している
```

ことが原因である可能性が最も高い。

最短かつ車輪の再発明を避ける修正は:

```text
Sapphire FrameQueueをそのまま使う
Desktop Vulkan lifetimeをFrameQueue外のresource leaseで管理する
```

こと。

FrameLatch、FrameQueue、VulkanOutputへ
新しい描画heuristicを追加してはいけない。

---

# 32. 進捗

| Phase | Commit | Status | Notes |
|-------|--------|--------|-------|
| S76-1 | pending | in progress | Fix presentation defer ownership |
