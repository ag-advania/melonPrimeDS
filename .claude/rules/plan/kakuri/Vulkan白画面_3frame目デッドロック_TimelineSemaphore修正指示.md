# Vulkan ROM起動後白画面停止
## 3frame目デッドロック／Timeline Semaphore submit不整合 修正指示書

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査時HEAD:** `b3182a2a44523c5d6e9c01c11dfc545b63535ca0`  
**確認build:** `melonPrimeDS v3.4.3 v54.exe`  
**参照フロントエンド:** `SapphireRhodonite/melonDS-android`  
**参照タグ:** `0.7.0.rc4`

---

# 0. 結論

今回の白画面は、Vulkan compositorが白色pixelを生成し続けていることを示すログではない。

v54では次が成功している。

```text
frame 1:
acquire
present result=1
commit

frame 2:
acquire
present result=1
commit
```

しかし3回目は:

```text
GUI thread:
overlay result=1
↓
acquire frameのログが出ない

EmuThread:
session initialize begin
↓
initialize completeが出ない
```

で停止している。

これは次の待ち合わせと一致する。

```text
GUI thread
  acquirePresentFrame()
  └─ stateMutex取得
     └─ synchronizeFrameReferencesLocked()
        └─ FrameQueue frameLock取得
           └─ waitForFrameConsumption(UINT64_MAX)
              └─ timeline semaphoreを永久待機

EmuThread
  submitVulkanFrontendFrame()
  └─ initialize()
     └─ stateMutex待機
```

さらに、`VulkanSurfacePresenter::submitSurfaceCommands()`の
`VkTimelineSemaphoreSubmitInfo`がVulkan仕様上不正な配列長になっている。

```text
signalSemaphoreCount      = 2
signalSemaphoreValueCount = 1
```

signal対象:

```text
index 0 = renderFinishedSemaphore  // binary
index 1 = timelineSemaphore        // timeline
```

value配列:

```text
1要素だけ
```

このため、唯一の`signalValue`はindex 0のbinary semaphoreへ対応し、
index 1のtimeline semaphore用valueが存在しない。

v54ログでは:

```text
timeline=1
```

なので、この不正経路が実際に有効になっている。

`vkQueueSubmit()`と`vkQueuePresentKHR()`が成功を返しても、
`frame->presentTimelineValue`に設定した値までtimeline semaphoreが進まず、
次の`waitForFrameConsumption(UINT64_MAX)`で永久停止できる。

---

# 1. v54ログから確定したこと

## 1.1 前回のFrameQueue枯渇は解消済み

前buildでは:

```text
submitCompletedFrame result=1 × 9
その後 result=0 が継続
```

だった。

v54では:

```text
submitCompletedFrame result=1 × 2
present result=1 × 2
commit × 2
```

となっている。

したがって:

```text
presenter登録消失
overlay failureによるqueue drain停止
9slot全消費
```

は修正されている。

## 1.2 actual renderer判定も改善済み

v54ではfirst present前:

```text
actual=Software
```

first／second present成功後:

```text
video actual renderer changed: Vulkan
```

となる。

`first present成功後にVulkan actual認定`という方針は維持する。

## 1.3 停止は3frame目

ログ終端:

```text
[VulkanPresenterTrace] tick ...
[VulkanPresenterTrace] overlay result=1

[VulkanSubmitTrace] frame gate backend=Vulkan actual=Vulkan(5)
[VulkanSubmitTrace] gate accepted
[VulkanSubmitTrace] entry ...
[VulkanSubmitTrace] before HasCurrentRenderer generation=3
[VulkanSubmitTrace] HasCurrentRenderer=1
[VulkanSubmitTrace] current Renderer3D=...
[VulkanSubmitTrace] Vulkan dynamic_cast=...
[VulkanSubmitTrace] session initialize begin
```

欠けているログ:

```text
[VulkanPresenterTrace] acquire frame=...
[VulkanSubmitTrace] session initialize complete
```

よって、GUI threadが`acquirePresentFrame()`内部で`stateMutex`を保持し、
EmuThreadが同じmutexを待っている可能性が極めて高い。

---

# 2. 最新pushで追加された直接的な停止要因

最新commitで次が変更されている。

```cpp
activePresenter->waitForFrameConsumption(frame, 0);
```

から:

```cpp
activePresenter->waitForFrameConsumption(frame, UINT64_MAX);
```

へ変更された。

対象:

```cpp
void MelonPrimeVulkanFrontendSession::synchronizeFrameReferencesLocked()
{
    frameQueue.synchronizePresentationCompletion([&](Frame* frame) {
        return activePresenter == nullptr
            || activePresenter->waitForFrameConsumption(
                frame,
                UINT64_MAX);
    });
    ...
}
```

この関数は:

```text
stateMutexを保持した状態
＋
FrameQueue::frameLockを保持した状態
```

でcallbackを実行する。

callback内で:

```cpp
vkWaitSemaphores(..., UINT64_MAX)
```

を呼ぶ。

つまり、通常frame loopの中に:

```text
二重mutex保持中の無期限GPU wait
```

が入っている。

これはSapphire型の低遅延producer／presenter contractへ反する。

---

# 3. Timeline semaphore submitの仕様違反

現在:

```cpp
std::array<VkSemaphore, 2> signalSemaphores = {
    surfaceState.renderFinishedSemaphore,
    timelineSemaphore,
};

VkTimelineSemaphoreSubmitInfo timelineSubmitInfo{};
u64 signalValue = 0;

if (useTimelineSemaphores
    && timelineSemaphore != VK_NULL_HANDLE)
{
    signalValue = ++timelineValue;

    timelineSubmitInfo.sType =
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;

    timelineSubmitInfo.signalSemaphoreValueCount = 1;
    timelineSubmitInfo.pSignalSemaphoreValues =
        &signalValue;

    submitInfo.pNext = &timelineSubmitInfo;
    submitInfo.signalSemaphoreCount = 2;
    submitInfo.pSignalSemaphores =
        signalSemaphores.data();
}
```

Vulkanのvalid usage:

```text
pSignalSemaphoresにtimeline semaphoreが1個でも含まれる場合、
VkTimelineSemaphoreSubmitInfo::signalSemaphoreValueCountは
VkSubmitInfo::signalSemaphoreCountと一致しなければならない。
```

今回:

```text
2 != 1
```

で不正。

さらにvalueは`pSignalSemaphores`とindex対応する。

現在の単一valueは:

```text
index 0のrenderFinishedSemaphoreへ対応
```

する。

binary semaphore用valueはimplementationが無視する。

timeline semaphoreはindex 1だが、対応valueがない。

---

# 4. 最優先修正 V55-1
## signal value配列をsemaphore配列と一致させる

修正例:

```cpp
std::array<VkSemaphore, 2> signalSemaphores = {
    surfaceState.renderFinishedSemaphore,
    timelineSemaphore,
};

std::array<u64, 1> waitValues = {
    0u, // imageAvailableSemaphoreはbinaryなので無視される
};

std::array<u64, 2> signalValues = {
    0u,          // renderFinishedSemaphoreはbinary
    signalValue, // timelineSemaphore
};

VkTimelineSemaphoreSubmitInfo timelineSubmitInfo{};
u64 signalValue = 0;

if (useTimelineSemaphores
    && timelineSemaphore != VK_NULL_HANDLE)
{
    signalValue = ++timelineValue;

    signalValues[0] = 0u;
    signalValues[1] = signalValue;

    timelineSubmitInfo.sType =
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;

    timelineSubmitInfo.waitSemaphoreValueCount =
        submitInfo.waitSemaphoreCount;
    timelineSubmitInfo.pWaitSemaphoreValues =
        waitValues.data();

    timelineSubmitInfo.signalSemaphoreValueCount =
        submitInfo.signalSemaphoreCount;
    timelineSubmitInfo.pSignalSemaphoreValues =
        signalValues.data();

    submitInfo.pNext = &timelineSubmitInfo;
    submitInfo.signalSemaphoreCount =
        static_cast<u32>(signalSemaphores.size());
    submitInfo.pSignalSemaphores =
        signalSemaphores.data();
}
```

ただし、上記例では`submitInfo.signalSemaphoreCount`を
`timelineSubmitInfo.signalSemaphoreValueCount`設定前に確定させる方が安全。

推奨順序:

```cpp
submitInfo.signalSemaphoreCount =
    static_cast<u32>(signalSemaphores.size());
submitInfo.pSignalSemaphores =
    signalSemaphores.data();

timelineSubmitInfo.signalSemaphoreValueCount =
    submitInfo.signalSemaphoreCount;
timelineSubmitInfo.pSignalSemaphoreValues =
    signalValues.data();
```

完成形:

```cpp
VkTimelineSemaphoreSubmitInfo timelineSubmitInfo{};
u64 signalValue = 0;
std::array<u64, 1> waitValues{0u};
std::array<u64, 2> signalValues{0u, 0u};

if (useTimelineSemaphores
    && timelineSemaphore != VK_NULL_HANDLE)
{
    signalValue = ++timelineValue;

    submitInfo.signalSemaphoreCount =
        static_cast<u32>(signalSemaphores.size());
    submitInfo.pSignalSemaphores =
        signalSemaphores.data();

    signalValues[1] = signalValue;

    timelineSubmitInfo.sType =
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineSubmitInfo.waitSemaphoreValueCount =
        submitInfo.waitSemaphoreCount;
    timelineSubmitInfo.pWaitSemaphoreValues =
        waitValues.data();
    timelineSubmitInfo.signalSemaphoreValueCount =
        submitInfo.signalSemaphoreCount;
    timelineSubmitInfo.pSignalSemaphoreValues =
        signalValues.data();

    submitInfo.pNext = &timelineSubmitInfo;
}
```

---

# 5. Sapphire参照についての注意

Sapphire `0.7.0.rc4`の
`VulkanSurfacePresenter::submitSurfaceCommands()`にも、
同じlegacy patternが残っている。

```cpp
signalSemaphoreValueCount = 1;
signalSemaphoreCount = 2;
```

したがって、この箇所だけはSapphireをそのままコピーしてはいけない。

採用するもの:

```text
SapphireのFrameQueue ownership
producer／presenter分離
present成功後commit
失敗時defer
renderer transactionとsurface lifetimeの分離
```

修正するもの:

```text
VkTimelineSemaphoreSubmitInfoの配列長
desktop GUI thread上の無期限wait
mutex保持中のVulkan wait
```

今回のWindows／NVIDIA環境は:

```text
timeline=1
```

であり、Sapphire Android側で潜在していた同期不整合が
desktop移植で顕在化した形と考える。

---

# 6. 最優先修正 V55-2
## reference同期をnon-blockingへ戻す

即時修正:

```cpp
void MelonPrimeVulkanFrontendSession::
synchronizeFrameReferencesLocked()
{
    frameQueue.synchronizePresentationCompletion(
        [&](Frame* frame) {
            return activePresenter == nullptr
                || activePresenter
                    ->waitForFrameConsumption(
                        frame,
                        0);
        });

    frameQueue.synchronizeHistoryReferences(
        [&](const Frame* frame) {
            return output
                .isFrameReferencedAsPendingPreviousSource(
                    frame);
        });
}
```

`timeout=0`で未完了なら:

```text
presentationReferencesを維持
frameを再利用しない
次のpollで再確認
```

とする。

禁止:

```cpp
waitForFrameConsumption(
    frame,
    UINT64_MAX);
```

を:

```text
stateMutex保持中
FrameQueue::frameLock保持中
GUI thread
```

で呼ばないこと。

---

# 7. 恒久修正 V55-3
## callback内GPU waitを廃止する

現在:

```text
FrameQueue lock
→ callback
→ vkWaitSemaphores
```

となっている。

恒久的にはcallback方式をやめる。

## 7.1 Presenterへcounter query追加

```cpp
bool VulkanSurfacePresenter::
getCompletedTimelineValue(
    u64& completedValue) const
{
    completedValue = 0;

    if (!initialized
        || !useTimelineSemaphores
        || timelineSemaphore == VK_NULL_HANDLE)
    {
        return false;
    }

    return vkGetSemaphoreCounterValue(
        device,
        timelineSemaphore,
        &completedValue) == VK_SUCCESS;
}
```

## 7.2 FrameQueueへvalue型同期追加

```cpp
void FrameQueue::
synchronizePresentationCompletion(
    u64 completedTimelineValue)
{
    std::unique_lock lock(frameLock);

    for (Frame& frame : frames)
    {
        if (frame.presentationReferences == 0)
            continue;

        if (frame.presentTimelineValue == 0
            || frame.presentTimelineValue
                <= completedTimelineValue)
        {
            frame.presentTimelineValue = 0;
            frame.presentationReferences = 0;

            if (frame.state
                    == FrameQueueState::HistoryReferenced
                && frame.historyReferences == 0)
            {
                transitionFrameLocked(
                    &frame,
                    FrameQueueState::HistoryReferenced,
                    FrameQueueState::Free);
            }
        }
    }

    rebuildFreeQueueLocked();
}
```

## 7.3 Session側

```cpp
u64 completedTimelineValue = 0;

if (activePresenter == nullptr
    || !activePresenter->getCompletedTimelineValue(
        completedTimelineValue))
{
    completedTimelineValue = 0;
}

frameQueue.synchronizePresentationCompletion(
    completedTimelineValue);
```

利点:

```text
GPU waitなし
callbackなし
FrameQueue lock中の外部callなし
全frameを1回のcounter queryで処理
```

---

# 8. 最優先修正 V55-4
## per-frame initializeを廃止する

現在`EmuInstance::submitVulkanFrontendFrame()`は毎frame:

```cpp
vulkanFrontendSessionOwner->initialize(*nds);
vulkanFrontendSessionOwner->beginGeneration(
    rendererGeneration);
```

を呼ぶ。

しかしsession初期化とgeneration設定は既に
renderer transaction内で行われている。

毎frameの`initialize()`は:

```text
stateMutexを毎frame取得
output init stateへ触れる
停止位置をsession initialize beginに見せる
producer／presenter競合を増やす
```

だけである。

削除対象:

```cpp
VulkanSubmitTrace(
    "[VulkanSubmitTrace] session initialize begin\n");

if (!vulkanFrontendSessionOwner->initialize(*nds))
{
    ...
}

VulkanSubmitTrace(
    "[VulkanSubmitTrace] session initialize complete\n");

vulkanFrontendSessionOwner->beginGeneration(
    rendererGeneration);
```

代替:

```cpp
if (!vulkanFrontendSessionOwner->isInitialized())
{
    VulkanSubmitTrace(
        "[VulkanSubmitTrace] skip: session not initialized\n");
    return;
}
```

さらに良い形:

```cpp
if (!vulkanFrontendSessionOwner
        ->isReadyForGeneration(
            rendererGeneration))
{
    ...
}
```

`beginGeneration()`は以下だけで行う。

```text
Renderer3D install成功時
backend switch commit時
renderer generation変更時
```

通常frame hot pathから外す。

---

# 9. stateMutexの責務を縮小する

現在:

```cpp
bool presentAcquiredFrame(...)
{
    std::scoped_lock lock(stateMutex);
    ...
    return presenter.presentFrame(
        frame,
        output,
        iterator->second,
        timeoutNs);
}
```

`presenter.presentFrame()`は内部で:

```text
output.waitForFrame
surface in-flight fence wait
vkAcquireNextImageKHR
command record
vkQueueSubmit
vkQueuePresentKHR
```

を行う。

これを`stateMutex`保持中に実行している。

今後、swapchainやdriverが遅延すると:

```text
producer
backend switch
shutdown
renderer generation更新
```

がすべて停止する。

## 推奨構造

追加:

```cpp
std::mutex presentationCallMutex;
```

lock order:

```text
presentationCallMutex
→ stateMutex
```

`presentAcquiredFrame()`:

```cpp
bool MelonPrimeVulkanFrontendSession::
presentAcquiredFrame(...)
{
    std::scoped_lock presentationLock(
        presentationCallMutex);

    VulkanCompositionInputs inputs{};

    {
        std::scoped_lock stateLock(
            stateMutex);

        const auto iterator =
            frameInputs.find(frame);

        if (!initialized
            || producerSuspended
            || activePresenter != &presenter
            || frame == nullptr
            || iterator == frameInputs.end())
        {
            return false;
        }

        inputs = iterator->second;
    }

    return presenter.presentFrame(
        frame,
        output,
        inputs,
        timeoutNs);
}
```

shutdown／unregister／surface detachも
`presentationCallMutex`を先に取得する。

これにより:

```text
stateMutexをGPU wait中に保持しない
```

構造へ変更する。

注意:

```text
output.shutdown()
presenter destruction
```

とpresent callがraceしないように、
lifecycle操作も同じ`presentationCallMutex`で直列化すること。

---

# 10. GUI threadでUINT64_MAXを常用しない

現在:

```cpp
constexpr u64 kPresentTimeoutNs =
    UINT64_MAX;
```

となっている。

コメントではSapphire準拠としているが、
Sapphireは常に無期限waitするわけではない。

Sapphireは:

```text
deadline
frame ready状態
fast-forward
render scale
temporal pressure
device profile
```

を見てtimeoutを決定する。

通常GUI pathでの無条件`UINT64_MAX`は避ける。

## V55での安全な暫定値

```cpp
constexpr u64 kPresentTimeoutNs =
    50'000'000ull;
```

50msを超えた場合:

```text
present=false
defer
failure count++
resync
```

とする。

## 恒久形

```cpp
u64 computePresenterTimeoutNs(
    bool frameReady,
    bool fastForward,
    int scale,
    bool temporalHistoryRequired);
```

通常:

```text
0～16.67ms
```

一時的な高解像度pressure:

```text
最大50～100ms
```

device teardown／明示的shutdownのみ:

```text
UINT64_MAX
```

を許可する。

---

# 11. timeline診断ログ

V55では最初の120frameのみ次を出す。

## submit前

```cpp
[VulkanTimeline]
submit waitCount=1
signalCount=2
waitValues=[0]
signalValues=[0,1]
timelineHandle=...
```

frameごと:

```text
signalValues=[0,1]
signalValues=[0,2]
signalValues=[0,3]
```

と増えること。

## submit後

```cpp
u64 completedValue = 0;
vkGetSemaphoreCounterValue(
    device,
    timelineSemaphore,
    &completedValue);

[VulkanTimeline]
submitted=2
completed=1
framePresent=2
```

## poll時

```text
[VulkanTimeline]
poll frameId=2
required=2
completed=2
released=1
```

## 異常時

```text
[VulkanTimeline]
STALL frameId=...
required=...
completed=...
lastSubmitted=...
```

---

# 12. Validation Layer

developer buildで:

```text
VK_LAYER_KHRONOS_validation
```

を有効化する。

修正前に期待されるVUID:

```text
VUID-VkSubmitInfo-pNext-03241
```

内容:

```text
VkTimelineSemaphoreSubmitInfoを含み、
pSignalSemaphoresにtimeline semaphoreがある場合、
signalSemaphoreValueCountはsignalSemaphoreCountと一致必須。
```

修正後:

```text
03241が出ない
timeline wait timeoutが出ない
```

ことを確認する。

release buildへvalidationを常時入れない。

---

# 13. 白画面の意味

v54は2frameだけpresentして停止している。

NDS起動直後の最初のframeが:

```text
白
黒
fade中
```

であることは通常あり得る。

したがって現時点の白画面は:

```text
白い最初期frameを正常present
↓
3frame目でdeadlock
↓
最後のswapchain imageが白のまま残る
```

と説明できる。

次はまだ調査対象にしない。

```text
SoftPacked pixel conversion
screenSwap
drawMode
descriptor contents
shader output color
```

まず300frame以上継続presentできる状態へ直す。

同期修正後も全frameが白い場合に限り、
composition contentsを別調査する。

---

# 14. 修正対象ファイル

## 必須

```text
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h
src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp
src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.h
src/frontend/qt_sdl/EmuInstance.cpp
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
```

## 恒久counter同期を入れる場合

```text
src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp
src/frontend/qt_sdl/VulkanReference/FrameQueue.h
```

---

# 15. 実装順序

## Phase V55-1

```text
VkTimelineSemaphoreSubmitInfoのcount/value対応を修正
```

## Phase V55-2

```text
synchronizeFrameReferencesLocked()のwaitをtimeout=0へ戻す
```

## Phase V55-3

```text
GUI normal pathのUINT64_MAXをbounded timeoutへ変更
```

## Phase V55-4

```text
per-frame initialize／beginGenerationを削除
```

## Phase V55-5

```text
stateMutex保持中のpresentFrameを廃止
presentationCallMutex導入
```

## Phase V55-6

```text
vkGetSemaphoreCounterValueによる一括non-blocking release
callback型synchronizePresentationCompletionを廃止
```

---

# 16. commit分割

## Commit 1

```text
Fix Vulkan presenter timeline semaphore submit values
```

内容:

```text
signalSemaphoreValueCountを2へ
signalValues=[0, signalValue]
必要ならwaitValues=[0]
validation VUID修正
```

## Commit 2

```text
Remove blocking Vulkan waits from FrameQueue synchronization
```

内容:

```text
UINT64_MAXを0へ
mutex保持中のblocking wait禁止
timeline poll log
```

## Commit 3

```text
Remove per-frame Vulkan session initialization
```

内容:

```text
initialize／beginGenerationをrenderer transactionへ限定
frame hot path簡略化
```

## Commit 4

```text
Decouple Vulkan presentation calls from session state locking
```

内容:

```text
presentationCallMutex
stateMutex短時間化
shutdown／unregister直列化
```

## Commit 5

```text
Release Vulkan frame references by completed timeline value
```

内容:

```text
vkGetSemaphoreCounterValue
FrameQueue value型release
callback廃止
```

---

# 17. テスト

## T1: timeline count

起動後300frame。

期待:

```text
signal=1,2,3...
completedが追従
requiredを最終的に超える／一致する
```

禁止:

```text
required=1 completed=0で固定
```

## T2: presenter継続

期待:

```text
acquire id=1
present id=1 result=1
commit id=1

acquire id=2
present id=2 result=1
commit id=2

...

acquire id=300
present id=300 result=1
commit id=300
```

## T3: producer継続

期待:

```text
submitCompletedFrame result=1
```

が継続する。

禁止:

```text
session initialize begin
```

でログ停止。

## T4: GUI応答

実行中に:

```text
window移動
resize
menu操作
pause
fullscreen
```

が可能。

GUI threadの無期限waitがないこと。

## T5: renderer switch

```text
Software
→ Vulkan
→ Software
→ Vulkan
```

10回。

期待:

```text
timeline counter再初期化
stale frame release
presenter pointer維持
deadlockなし
```

## T6: timeline無効経路

debug optionでtimelineを無効化。

期待:

```text
binary／fence fallbackで継続動作
```

timeline有効時だけ止まる場合は、
今回の根本原因を強く裏付ける。

## T7: validation

期待:

```text
VUID-VkSubmitInfo-pNext-03241なし
semaphore lifetime errorなし
command buffer pending errorなし
```

---

# 18. 完了条件

```text
3frame目で停止しない
session initialize beginで固まらない
GUI threadが応答する
present／commitが300frame以上継続する
timeline completed valueが増加する
FrameQueueが継続循環する
ROM起動時の白画面からゲーム画面へ進む
actual Vulkan認定はfirst present成功後
Software／Vulkan切替でdeadlockしない
validation error 03241が出ない
```

---

# 19. 最終判断

今回の最有力根本原因は二つの組合せ。

## 原因1

```text
VkTimelineSemaphoreSubmitInfoのsignal value数が不正
```

```text
signalSemaphoreCount=2
signalSemaphoreValueCount=1
```

timeline semaphore用valueが正しく対応していない。

## 原因2

```text
不正にsignalされた可能性のあるtimeline値を
二重mutex保持中にUINT64_MAXで待つ
```

その結果:

```text
GUI threadがacquirePresentFrame()で停止
EmuThreadがinitialize()のstateMutex待ちで停止
最後にpresentした白い初期frameが画面へ残る
```

Sapphireから維持すべきなのは:

```text
FrameQueue ownership
producer／presenter分離
present成功後commit
失敗時defer
```

であり、Sapphire `0.7.0.rc4`に残る
legacy timeline submitのcount不整合までコピーしてはいけない。
