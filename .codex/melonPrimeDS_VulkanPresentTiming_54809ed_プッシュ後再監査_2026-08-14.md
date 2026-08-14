# melonPrimeDS Vulkan Present Timing
## `54809ed` Push後 再監査・残件指示書

- 作成日: 2026-08-14
- 対象Repository: `ag-advania/melonPrimeDS`
- 対象Branch: `develop_remakeVulkan_ver3`
- 前回監査HEAD: `154b7b6a573ff623167f13b4675dea9473a0ccd2`
- 今回監査HEAD: `54809ed1cf6838c83d9285ee01f50e1d414527f5`
- HEAD commit: `Route Vulkan present wait lifecycle results`
- HEAD parent: `5e8c17fb1d2be6c0261f71ed2edcbfa76a330d9c`
- 前回監査HEADからの差分: **2 commits ahead / 0 behind**
- 今回実装commit単体: **1 commit / 523 changes / +457 / -66**
- 総合判定: **PASS WITH NEW P2 SWAPCHAIN-GENERATION FINDING**

---

# 1. 結論

今回Pushされた修正について、前回監査で残した`vkWaitForPresent2KHR()`の2件は、**source / pure model / contract audit上は閉じている**。

```text
前回 NEW P2-5
vkWaitForPresent2KHR
VK_ERROR_SURFACE_LOST_KHR
    -> SurfaceLost
    -> FailRenderer

判定:
    CLOSED
```

```text
前回 NEW P2-6
vkWaitForPresent2KHR
VK_SUBOPTIMAL_KHR
    -> SwapchainSuboptimal
    -> RebuildSwapchain

判定:
    CLOSED
```

また、EXT / GOOGLE timing queryについてAPI固有classifierが追加され、前回までのresult contract整理もかなり強化された。

ただし、今回`VK_SUBOPTIMAL_KHR -> RebuildSwapchain`を正しく実装したことで、**pre-inputで決定した旧swapchainの`LastDecision`が、同じエミュレーションframe中に再作成された新swapchainへ持ち越されるlifecycle gap**が明確になった。

現在の流れは次の通り。

```text
OLD swapchain
    |
    | PresentPacer.BeginFrame()
    |   -> ResolveDecision()
    |   -> LastDecision = OLD swapchain用decision
    |   -> vkWaitForPresent2KHR()
    |       -> VK_SUBOPTIMAL_KHR
    |
    v
VulkanPresenter::BeginLowLatencyFrame()
    -> SwapchainDirty = true
    -> frameは継続
    |
    v
input
RunFrame
drawScreen
    |
    v
VulkanPresenter::BeginFrame()
    -> SwapchainDirtyを検知
    -> RecreateSwapchain()
        -> OnSwapchainDestroyed()
        -> ResetTimingLifecycle()
        -> OnSwapchainCreated()
    |
    | しかし LastDecision はresetされない
    | WaitAttemptedThisFrame も旧swapchainの値を保持
    | TargetFrameIntervalNs も旧frameの値を保持
    |
    v
NEW swapchain
    |
    v
VulkanPresenter::EndFrame()
    -> PresentPacer.PreparePresent()
    -> backend = LastDecision.TimingBackend
    -> GOOGLEならLastDecision.TargetTimeSchedulingを使用
```

つまり、

```text
decision generation != present generation
```

になり得る。

これはVulkan APIのreturn-code mapping自体の誤りではなく、**swapchain generationとper-frame pacing decisionのlifetimeが一致していない**ことが根本原因である。

したがって今回のcanonical verdictは:

```text
P2-1 mixed EXT telemetry + GOOGLE target:
    CLOSED

P2-2 GOOGLE + FIFO_LATEST_READY:
    CLOSED on source/model

EXT/GOOGLE fatal query routing:
    CLOSED on source/model/contract

WaitForPresent2 SURFACE_LOST:
    CLOSED on source/model/contract

WaitForPresent2 SUBOPTIMAL:
    CLOSED on source/model/contract

NEW P2-7:
    per-frame LastDecision survives same-frame swapchain recreation
    OPEN

P3:
    API-level injected Vulkan query path
    OPEN

P3:
    GOOGLE refresh-cycle classifier is not truly API-specific
    OPEN

Windows/Linux/physical validation:
    OPEN / NOT RUN

OVERALL:
    PASS WITH NEW P2 SWAPCHAIN-GENERATION FINDING
```

---

# 2. Push確認

現在のbranch HEAD:

```text
develop_remakeVulkan_ver3
54809ed1cf6838c83d9285ee01f50e1d414527f5
Route Vulkan present wait lifecycle results
```

前回監査HEAD:

```text
154b7b6a573ff623167f13b4675dea9473a0ccd2
```

比較:

```text
ahead_by: 2
behind_by: 0
```

前回監査HEADから今回HEADまでの主要変更:

```text
.codex/
    melonPrimeDS_VulkanPresentTiming_154b7b6_プッシュ後再監査_2026-08-14.md
        added

    melonPrimeDS_VulkanPresentTiming_154b7b6_プッシュ後再監査_実装結果_2026-08-14.md
        added

    旧P2指示書 / 実装結果
        removed

src/
    VulkanPresentPacer.cpp
        modified

    VulkanPresentPacer.h
        modified

    VulkanPresentPacingPolicy.h
        modified

tools/
    ci/audits/audit-low-latency-contract.py
        modified

    testing/vulkan-present-timing-tests.cpp
        modified
```

実装commit単体:

```text
parent:
    5e8c17fb1d2be6c0261f71ed2edcbfa76a330d9c

head:
    54809ed1cf6838c83d9285ee01f50e1d414527f5

files:
    6

changes:
    523

additions:
    457

deletions:
    66
```

---

# 3. 前回P2-5監査
## WaitForPresent2 `VK_ERROR_SURFACE_LOST_KHR`

現在は`VulkanPresentPacer.h`に:

```cpp
enum class VulkanPresentWait2ResultAction : int
{
    Continue = 0,
    Timeout,
    SwapchainSuboptimal,
    SwapchainOutOfDate,
    DeviceLost,
    SurfaceLost,
    DisableWait,
};
```

が追加されている。

classifier:

```cpp
constexpr VulkanPresentWait2ResultAction ClassifyVulkanPresentWait2Result(
    VkResult result) noexcept
{
    switch (result)
    {
    case VK_SUCCESS:
        return VulkanPresentWait2ResultAction::Continue;

    case VK_TIMEOUT:
        return VulkanPresentWait2ResultAction::Timeout;

    case VK_SUBOPTIMAL_KHR:
        return VulkanPresentWait2ResultAction::SwapchainSuboptimal;

    case VK_ERROR_OUT_OF_DATE_KHR:
        return VulkanPresentWait2ResultAction::SwapchainOutOfDate;

    case VK_ERROR_DEVICE_LOST:
        return VulkanPresentWait2ResultAction::DeviceLost;

    case VK_ERROR_SURFACE_LOST_KHR:
        return VulkanPresentWait2ResultAction::SurfaceLost;

    default:
        return VulkanPresentWait2ResultAction::DisableWait;
    }
}
```

`BeginFrame()`側も:

```cpp
case VulkanPresentWait2ResultAction::SurfaceLost:
    TargetSchedulingActive.store(false, std::memory_order_release);
    LogPresentLifecycleRoute(
        "WaitForPresent2KHR", result, VulkanPacerBeginResult::SurfaceLost);
    return VulkanPacerBeginResult::SurfaceLost;
```

となっている。

従って旧:

```text
SURFACE_LOST
    -> DisableWait()
    -> Continue
```

は消えた。

## 判定

```text
PASS
CLOSED
```

---

# 4. 前回P2-6監査
## WaitForPresent2 `VK_SUBOPTIMAL_KHR`

Vulkan仕様で`vkWaitForPresent2KHR()`のsuccess codeは:

```text
VK_SUCCESS
VK_TIMEOUT
VK_SUBOPTIMAL_KHR
```

である。

今回:

```cpp
case VK_SUBOPTIMAL_KHR:
    return VulkanPresentWait2ResultAction::SwapchainSuboptimal;
```

となった。

`VulkanPacerBeginResult`にも既存値の番号を壊さない形で:

```cpp
enum class VulkanPacerBeginResult : int
{
    Continue = 0,
    SwapchainOutOfDate,
    DeviceLost,
    SurfaceLost,
    SwapchainSuboptimal,
};
```

が追加されている。

action:

```cpp
case VulkanPacerBeginResult::SwapchainSuboptimal:
    return {true, false};
```

なので:

```text
VK_SUBOPTIMAL_KHR
    -> SwapchainSuboptimal
    -> RebuildSwapchain = true
    -> FailRenderer = false
```

となる。

`DisableWait()`へfallthroughしない。

## 判定

```text
PASS
CLOSED
```

---

# 5. WaitForPresent2 return contract監査

現行Khronos refpage:

```text
vkWaitForPresent2KHR

Success:
    VK_SUBOPTIMAL_KHR
    VK_SUCCESS
    VK_TIMEOUT

Failure:
    VK_ERROR_DEVICE_LOST
    VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT
    VK_ERROR_OUT_OF_DATE_KHR
    VK_ERROR_OUT_OF_DEVICE_MEMORY
    VK_ERROR_OUT_OF_HOST_MEMORY
    VK_ERROR_SURFACE_LOST_KHR
    VK_ERROR_UNKNOWN
    VK_ERROR_VALIDATION_FAILED
```

現在のproduction classifier:

```text
SUCCESS
    -> Continue

TIMEOUT
    -> Timeout
    -> WaitTimeouts++
    -> Continue

SUBOPTIMAL
    -> SwapchainSuboptimal
    -> RebuildSwapchain

OUT_OF_DATE
    -> SwapchainOutOfDate
    -> RebuildSwapchain

DEVICE_LOST
    -> DeviceLost
    -> FailRenderer

SURFACE_LOST
    -> SurfaceLost
    -> FailRenderer

other
    -> DisableWait
    -> Continue
```

前回問題だった重大classは正しく分離された。

`VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT`はdefault `DisableWait`になる。

current branchをcode searchした範囲では`VK_EXT_full_screen_exclusive`を実利用する経路は見つからないため、現時点ではP2 blockerとはしない。

ただし将来fullscreen-exclusiveを有効化する場合は専用lifecycle routeが必要。

---

# 6. 新規API-specific classifier監査

今回追加された:

```text
ClassifyVulkanPastTimingResult()
ClassifyVulkanTimingPropertiesResult()
ClassifyVulkanTimeDomainResult()
ClassifyVulkanGoogleTimingResult()
```

を確認した。

---

# 6.1 `vkGetPastPresentationTimingEXT`

現在:

```text
VK_SUCCESS
VK_INCOMPLETE
    -> Continue

VK_ERROR_OUT_OF_DATE_KHR
    -> SwapchainOutOfDate

VK_ERROR_DEVICE_LOST
    -> DeviceLost

VK_ERROR_SURFACE_LOST_KHR
    -> SurfaceLost

other
    -> DisableOptional
```

Khronos return contractとの主要部分は一致している。

## 判定

```text
PASS
```

---

# 6.2 `vkGetSwapchainTimingPropertiesEXT`

現在:

```text
VK_SUCCESS
    -> Continue

VK_NOT_READY
    -> RetryAfterPresent

VK_ERROR_SURFACE_LOST_KHR
    -> SurfaceLost

other
    -> DisableTargetLifecycle
```

Khronos refpageでは:

```text
Success:
    VK_NOT_READY
    VK_SUCCESS

Failure:
    VK_ERROR_OUT_OF_DEVICE_MEMORY
    VK_ERROR_OUT_OF_HOST_MEMORY
    VK_ERROR_SURFACE_LOST_KHR
    VK_ERROR_UNKNOWN
    VK_ERROR_VALIDATION_FAILED
```

である。

前回問題だった`VK_NOT_READY` pendingと`SURFACE_LOST` typed routingは維持されている。

## 判定

```text
PASS
```

---

# 6.3 `vkGetSwapchainTimeDomainPropertiesEXT`

現在:

```text
VK_SUCCESS
    -> Continue

VK_INCOMPLETE
    -> RetryEnumeration

VK_ERROR_SURFACE_LOST_KHR
    -> SurfaceLost

other
    -> DisableTargetLifecycle
```

Khronos refpage:

```text
Success:
    VK_INCOMPLETE
    VK_SUCCESS

Failure:
    VK_ERROR_OUT_OF_DEVICE_MEMORY
    VK_ERROR_OUT_OF_HOST_MEMORY
    VK_ERROR_SURFACE_LOST_KHR
    VK_ERROR_UNKNOWN
    VK_ERROR_VALIDATION_FAILED
```

と一致する。

count query / array queryの両方が同じclassifierを通り、`VK_INCOMPLETE`は最大3回のbounded enumeration retryへ入る。

## 判定

```text
PASS
```

---

# 7. required time-domain contract回帰

現コードは引き続き:

```cpp
VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT
```

が列挙されたことをrequired contractとして確認する。

そのうえで:

```text
SWAPCHAIN_LOCAL
    ↓
PRESENT_STAGE_LOCAL
```

の順でtarget domainを選択している。

required domainがない場合は:

```text
TimeDomainsReady = false
TargetSchedulingLifecycleFailed = true
```

となる。

`VK_NOT_READY`をtime-domain bootstrap pendingへ戻すregressionは確認していない。

## 判定

```text
PASS
```

---

# 8. Wait2 pure test監査

新規:

```cpp
void TestPresentWait2ResultClassification()
```

が追加されている。

確認項目:

```text
VK_SUCCESS
VK_TIMEOUT
VK_SUBOPTIMAL_KHR
VK_ERROR_OUT_OF_DATE_KHR
VK_ERROR_DEVICE_LOST
VK_ERROR_SURFACE_LOST_KHR
VK_ERROR_UNKNOWN
VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT
```

`main()`からも呼ばれる。

さらに:

```text
TestBeginResultRouting()
```

へ`SwapchainSuboptimal -> RebuildSwapchain`が追加されている。

## 判定

```text
PASS
```

---

# 9. timing query pure contract test監査

新規:

```cpp
void TestPresentTimingQueryContractClassification()
```

が追加されている。

generic lifecycle classifierだけではなく、API固有success / pending setをpure testで確認する方向へ改善された。

`main()`からの呼出しも存在する。

## 判定

```text
PASS
```

ただし後述の通り、これは**Vulkan dispatchを実際にfault injectionするintegration testではない**。

---

# 10. contract audit監査

`audit-low-latency-contract.py`には今回のWait2契約チェックが追加されている。

最低限、source上:

```text
SwapchainSuboptimal
VulkanPresentWait2ResultAction
ClassifyVulkanPresentWait2Result
VK_SUBOPTIMAL_KHR
VK_ERROR_SURFACE_LOST_KHR
```

の存在を確認し、

`BeginFrame()`内で:

```text
classifier
    -> SwapchainSuboptimal
    -> SurfaceLost
    -> DisableWait
```

の順序を確認する。

さらに:

```text
TestPresentWait2ResultClassification
VulkanPacerBeginResult::SwapchainSuboptimal
```

のtest存在もaudit対象になった。

## 判定

```text
PASS
```

---

# 11. NEW P2-7
## 同一frame中のswapchain recreation後に旧`LastDecision`が残る

今回の再監査で最も重要な残件。

---

# 11.1 `LastDecision`の設計意図

`VulkanPresentPacer.h`には:

```cpp
// Resolved once per frame in BeginFrame() and reused by PreparePresent(),
// so the wait decision and the scheduling decision can never disagree.
VulkanPacingDecision LastDecision{};
```

と明記されている。

これは正しい設計思想である。

同一swapchain generation内なら:

```text
BeginFrameのwait decision
PreparePresentのtarget decision
```

が同じresolver結果を使うので、frame内のdecision driftを防げる。

問題は:

```text
BeginFrameとPreparePresentの間でswapchain generationが変わる
```

ケースである。

---

# 11.2 `BeginFrame()`は旧swapchainでdecisionを確定する

`PresentPacer.BeginFrame()`は:

```text
BuildCapabilities()
ResolveDecision()
LastDecision = decision
```

を行った後、`vkWaitForPresent2KHR()`へ進む。

この時点の:

```text
Swapchain
PresentMode
TimingMetadataEnabled
TimeDomainsReady
GoogleRefreshDurationReady
FifoFamilyPresentMode
```

等は**旧swapchain**の状態である。

---

# 11.3 SUBOPTIMALでdirtyになるがframeは中断しない

今回追加された経路:

```text
vkWaitForPresent2KHR
    -> VK_SUBOPTIMAL_KHR
    -> VulkanPacerBeginResult::SwapchainSuboptimal
```

はpresenterで:

```cpp
if (pacerAction.RebuildSwapchain)
    SwapchainDirty.store(true, std::memory_order_release);
```

となる。

しかし、その後:

```text
Reflex.BeginFrame()
AntiLag.BeginFrame()
input
RunFrame
drawScreen
```

へframeは継続する。

つまり:

```text
pacer result = rebuild requested
```

は:

```text
このframeを中断する
```

という意味ではない。

---

# 11.4 draw側で同じframe中にswapchainが再作成される

EmuThreadの実行順は:

```text
beginVulkanLowLatencyFrame()
    ↓
input sample
    ↓
RunFrameHook
    ↓
RunFrame
    ↓
drawScreen()
```

である。

draw側の`VulkanPresenter::BeginFrame()`は:

```cpp
const bool dirty = SwapchainDirty.exchange(false, std::memory_order_acq_rel);
if (dirty || Swapchain == VK_NULL_HANDLE
    || VSyncApplied != VSyncRequested.load(std::memory_order_acquire))
{
    if (!RecreateSwapchain(requestedWidth, requestedHeight))
        return false;
}
```

となっている。

従ってWait2 SUBOPTIMALは:

```text
pre-input:
    old swapchainでdecision

same emulation frameのdraw:
    new swapchainへrecreate
```

を発生させる。

---

# 11.5 recreationはtiming modelをresetする

`RecreateSwapchain()`は:

```text
Frames.WaitIdle()
DestroySwapchainObjects()
PresentPacer.OnSwapchainDestroyed()
CreateSwapchainKHR()
PresentPacer.OnSwapchainCreated()
```

を通る。

`OnSwapchainDestroyed()`:

```cpp
Swapchain = VK_NULL_HANDLE;
ResetTimingLifecycle();
LastSubmittedId = 0;
LastPresentedId = 0;
LastWaitedId = 0;
Authority.store(GenericHost);
```

`OnSwapchainCreated()`:

```cpp
++SwapchainGeneration;
ResetTimingLifecycle();
Swapchain = swapchain;
...
```

となる。

---

# 11.6 しかし`ResetTimingLifecycle()`は`LastDecision`を消さない

現在の`ResetTimingLifecycle()`がresetするもの:

```text
TimingPropertiesCounter
TimingPropertiesReady
TimingPropertiesRetryPending
RefreshDynamics
TimeDomainsCounter
TimeDomainsReady
TimeDomainsEnumerationRetryPending
TargetTimeDomain
TargetTimeDomainId
TimingQueueAllocated
TimingQueueSize
TimingQueueRecoveries
TimingQueuePressureActive
TimingQueueRecoveryPending
OutstandingTimedPresents
RefreshDurationNs
RefreshIntervalNs
LastTargetValueNs
LastAppliedTargetMode
LastTargetMode
LastRelativeRequest
LastFeedbackId
LastFeedbackStageTimeNs
TimingModel
GoogleTimingModel
GoogleRefreshDurationReady
PendingBeginResult
RelativeCadence
TargetSchedulingActive
```

しかし以下はresetされない。

```text
LastDecision
WaitAttemptedThisFrame
TargetFrameIntervalNs
FallbackReason
```

特に:

```text
LastDecision
WaitAttemptedThisFrame
TargetFrameIntervalNs
```

は、直前の旧swapchain `BeginFrame()`で作られた**per-frame state**である。

---

# 11.7 新swapchainの`PreparePresent()`が旧`LastDecision`を読む

現在の`PreparePresent()`は:

```cpp
const VulkanPresentTimingBackend backend = LastDecision.TimingBackend;
```

から始まる。

EXT pathでは:

```cpp
const TargetTimingRequest target =
    EvaluateTargetTiming(metadata.Sequence);
```

となり、`EvaluateTargetTiming()`は`LastDecision.TargetMode`を使う。

GOOGLE pathではより直接的に:

```cpp
const bool requestTarget = LastDecision.TargetTimeScheduling;

const VulkanGooglePresentRequest request = GoogleTimingModel.Prepare(
    nowNs,
    TargetFrameIntervalNs,
    requestTarget);
```

となる。

つまり新swapchainの最初のpresentが:

```text
old generationでresolvedした
    TimingBackend
    TargetTimeScheduling
    TargetMode
    FrameInterval
```

を参照し得る。

---

# 11.8 EXT pathへの影響

EXT absolute pathはtiming modelがresetされているため、baselineがなく:

```text
target = 0
```

へ落ちやすい。

relative pathも:

```text
LastPresentedId == 0
```

なのでfirst-present bootstrapへ落ちる。

従ってEXTでは多くの場合:

```text
stale decision
    -> target自体はbootstrap guardで抑止
```

される。

これは偶然安全側へ落ちているだけであり、generation contractが正しいことを意味しない。

またbackend選択自体はold decisionなので、new swapchain capabilitiesに対して再resolveされたものではない。

---

# 11.9 GOOGLE pathへの影響

GOOGLE pathはabsolute targetを自身のmodelでbootstrapできる。

そのため:

```cpp
requestTarget = LastDecision.TargetTimeScheduling;
```

が旧generationで`true`のままなら、新swapchainのfirst presentで:

```text
desiredPresentTime != 0
```

を生成可能。

このdecisionはnew swapchainに対して`ResolveDecision()`を実行して得たものではない。

つまり:

```text
new swapchain first present
    -> old swapchain decisionをbehavioral inputとして使用
```

する。

この状態は明示的に禁止すべきである。

---

# 11.10 A/B captureにもgeneration混入が起きる

`CaptureState()`は:

```cpp
snapshot.SwapchainGeneration = SwapchainGeneration;
snapshot.BoundedPresentWait = LastDecision.BoundedPresentWait;
snapshot.BoundedWaitAttempted = WaitAttemptedThisFrame;
snapshot.FallbackReason = static_cast<int>(FallbackReason);
snapshot.FrameIntervalNs = TargetFrameIntervalNs;
```

となっている。

したがってsame-frame recreation後は:

```text
SwapchainGeneration:
    NEW

BoundedPresentWait:
    OLD generation decision

BoundedWaitAttempted:
    OLD generationで実行したwait

FrameIntervalNs:
    OLD generation BeginFrameで設定
```

という行を生成し得る。

これはA/B計測のgeneration境界を汚す。

特に:

```text
bounded_wait_attempted_ratio
```

は「このswapchain generationでwaitを実際に試したか」を意味するべきだが、現在は前generationでのwait attemptを新generation rowへ持ち越せる。

これは単なるログ表示の問題ではなく、**計測の因果関係がgenerationを跨ぐ問題**である。

---

# 11.11 根本原因

根本原因は:

```text
per-frame decision lifetime
```

と:

```text
swapchain lifetime
```

が別々に管理されているのに、

```text
decisionがどのswapchain generationで作られたか
```

を保持していないこと。

現在の暗黙前提:

```text
BeginFrame()
    ↓
PreparePresent()

この間にswapchainは変わらない
```

が、実際のpresenter lifecycleでは成立しない。

今回のSUBOPTIMAL routeによって:

```text
BeginFrame()
    ↓
SwapchainDirty
    ↓
RecreateSwapchain()
    ↓
PreparePresent()
```

が明示的に成立するようになった。

---

# 12. NEW P2-7 推奨修正

## 方針A: generation stamp方式
### 推奨

`LastDecision`がどのgenerationに属するか明示する。

例:

```cpp
u64 DecisionSwapchainGeneration = 0;
u64 WaitAttemptSwapchainGeneration = 0;
```

`BeginFrame()`でdecision確定時:

```cpp
LastDecision = decision;
DecisionSwapchainGeneration = SwapchainGeneration;
```

実際にwaitを呼ぶ直前または直後:

```cpp
WaitAttemptedThisFrame = true;
WaitAttemptSwapchainGeneration = SwapchainGeneration;
```

`PreparePresent()`:

```cpp
const bool decisionCurrent =
    DecisionSwapchainGeneration == SwapchainGeneration;

const VulkanPresentTimingBackend backend =
    decisionCurrent
        ? LastDecision.TimingBackend
        : VulkanPresentTimingBackend::None;
```

behavioral targetについてはgeneration mismatchなら必ず無効。

```text
TargetTimeScheduling = false
TargetMode = None
TimingBackend = None
```

とする。

`CaptureState()`:

```cpp
snapshot.BoundedPresentWait =
    DecisionSwapchainGeneration == SwapchainGeneration
        ? LastDecision.BoundedPresentWait
        : false;

snapshot.BoundedWaitAttempted =
    WaitAttemptSwapchainGeneration == SwapchainGeneration
        ? WaitAttemptedThisFrame
        : false;
```

これなら:

```text
old decision
    -> new swapchain
```

への漏出を型ではなく状態contractとして防げる。

---

# 13. NEW P2-7 最小修正案

generation fieldを増やしたくない場合、`ResetTimingLifecycle()`でper-frame stateも失効させる。

最低限:

```cpp
LastDecision = VulkanPacingDecision{};
WaitAttemptedThisFrame = false;
TargetFrameIntervalNs = 0;
```

必要に応じて:

```cpp
FallbackReason = VulkanJitFallbackReason::TelemetryOnlyPolicy;
```

またはgeneration reset専用reasonを設定する。

これによりsame-frame recreation後のfirst presentは:

```text
backend = None
target = None
bounded wait attribution = false
```

となる。

1 presentだけgeneric timing metadataを付けない可能性があるが、

```text
retired swapchainのdecisionでbehaviorを決める
```

より安全。

次のエミュレーションframeで通常の`BeginFrame()`がnew swapchainのcapabilitiesから再resolveする。

---

# 14. 禁止する修正

## 14.1 draw中に`PresentPacer.BeginFrame()`をもう一度呼ばない

次のような修正は禁止。

```text
RecreateSwapchain()
    -> PresentPacer.BeginFrame()を再実行
```

理由:

```text
BeginFrame()
```

には単なるpure resolverだけでなく:

```text
past timing query
timing property retry
time-domain retry
bounded present wait
```

が含まれる。

さらにVulkan low-latency Beginは:

```text
host limiter
    ↓
pacing sleep/wait
    ↓
input sample
```

のためpre-inputへ置かれている。

draw中に再実行すると:

```text
input後にlate wait
```

が入り、低遅延orderingを壊す。

また同じframeでwait / query lifecycleを二重実行する危険がある。

---

# 15. 推奨する責務分離

理想:

```text
BeginFrame()
    -> current generationのbehavior decisionを確定
    -> optional wait実行

swapchain recreation
    -> current frame decisionをinvalidate

PreparePresent()
    -> decision generation == current generation ?
        YES:
            normal timing metadata / target
        NO:
            untimed safe present
```

次frame:

```text
BeginFrame()
    -> new generationで再resolve
```

この方が:

```text
input timing
swapchain lifecycle
target scheduling
```

の責務を混ぜない。

---

# 16. NEW P2-7 必須test

少なくともpure state helperを切り出し、次をtestする。

例:

```cpp
constexpr bool VulkanFrameDecisionMatchesSwapchain(
    u64 decisionGeneration,
    u64 swapchainGeneration) noexcept
{
    return decisionGeneration != 0
        && decisionGeneration == swapchainGeneration;
}
```

test:

```text
decision generation = 10
swapchain generation = 10
    -> true

decision generation = 10
swapchain generation = 11
    -> false

decision generation = 0
swapchain generation = 11
    -> false
```

さらに:

```text
generation mismatch
    -> target scheduling false
    -> backend None
    -> bounded wait attribution false
    -> bounded wait attempted attribution false
```

をpure testで固定する。

---

# 17. NEW P2-7 contract audit追加

`audit-low-latency-contract.py`へ最低限:

```text
1.
ResetTimingLifecycle()がLastDecisionをinvalidateする
または
DecisionSwapchainGenerationが存在する

2.
PreparePresent()がgeneration mismatchを拒否する

3.
CaptureState()がold-generation wait attemptをnew generationへ記録しない

4.
TargetFrameIntervalNsがgenerationを跨いでbehavioral targetへ使われない

5.
TestSwapchainRecreationInvalidatesFrameDecision()が存在する
```

を追加する。

特に静的auditで:

```text
PreparePresent():
    backend = LastDecision.TimingBackend
```

だけが無条件に残っていたらFAILさせる。

---

# 18. P3
## API-level fault injectionはまだ未実装

今回:

```text
TestPresentWait2ResultClassification()
TestPresentTimingQueryContractClassification()
```

が増えたことは良い。

ただし、これらが直接testしているのは:

```text
pure classifier
```

である。

まだ:

```text
fake Vulkan dispatch
    -> GetPastPresentationTimingEXT = SURFACE_LOST
    -> ReportPastTiming()
    -> state flags + typed result検証
```

のようなproduction function単位のfault injectionではない。

同様に:

```text
RefreshTimingProperties()
RefreshTimeDomains()
RefreshGoogleTiming()
ReportGooglePastTiming()
WaitForPresent2KHR path
```

へmocked VkResultを返して:

```text
TimingMetadataEnabled
TimingResultsQueryEnabled
TimingPropertiesRetryPending
TimeDomainsEnumerationRetryPending
TargetSchedulingLifecycleFailed
PendingBeginResult
WaitRuntimeEnabled
```

等のside effectまで確認してはいない。

## 判定

```text
pure contract:
    PASS

API-level injected production path:
    OPEN
```

これは現在のsource修正を否定するものではない。

ただし前回DoDにあった:

```text
API-level fault injection
```

を厳密にはまだ閉じていない。

---

# 19. P3
## GOOGLE classifierは2つのAPIで共有されている

名前:

```cpp
ClassifyVulkanGoogleTimingResult()
```

は現在:

```text
vkGetRefreshCycleDurationGOOGLE()
vkGetPastPresentationTimingGOOGLE()
```

の両方に使われている。

しかしKhronos return contractは同一ではない。

## `vkGetPastPresentationTimingGOOGLE`

```text
Success:
    VK_INCOMPLETE
    VK_SUCCESS

Failure:
    VK_ERROR_DEVICE_LOST
    VK_ERROR_OUT_OF_DATE_KHR
    VK_ERROR_OUT_OF_HOST_MEMORY
    VK_ERROR_SURFACE_LOST_KHR
    VK_ERROR_UNKNOWN
    VK_ERROR_VALIDATION_FAILED
```

## `vkGetRefreshCycleDurationGOOGLE`

```text
Success:
    VK_SUCCESS

Failure:
    VK_ERROR_DEVICE_LOST
    VK_ERROR_OUT_OF_HOST_MEMORY
    VK_ERROR_SURFACE_LOST_KHR
    VK_ERROR_UNKNOWN
    VK_ERROR_VALIDATION_FAILED
```

つまりrefresh-cycle queryにはrefpage上:

```text
VK_INCOMPLETE
VK_ERROR_OUT_OF_DATE_KHR
```

は列挙されていない。

現在classifierは:

```text
VK_INCOMPLETE
    -> Continue

VK_ERROR_OUT_OF_DATE_KHR
    -> SwapchainOutOfDate
```

を両APIへ共通で定義している。

現`RefreshGoogleTiming()`は:

```cpp
if (result == VK_SUCCESS && duration.refreshDuration != 0)
```

を先に要求するため、`VK_INCOMPLETE`がclassifierで`Continue`になっても最終的にはbackend disableへ落ちる。

従って現時点でconforming driver上の重大runtime bugとは判定しない。

ただし:

```text
API-specific classifier
```

と呼ぶ設計としては不正確。

## 推奨

分ける。

```text
ClassifyVulkanGoogleRefreshCycleResult()
ClassifyVulkanGooglePastTimingResult()
```

`GoogleRefreshCycle`:

```text
VK_SUCCESS
    -> Continue

VK_ERROR_DEVICE_LOST
    -> DeviceLost

VK_ERROR_SURFACE_LOST_KHR
    -> SurfaceLost

other listed optional failures
    -> DisableOptional
```

`GooglePastTiming`:

```text
VK_SUCCESS
VK_INCOMPLETE
    -> Continue

OUT_OF_DATE
DEVICE_LOST
SURFACE_LOST
    -> typed lifecycle

other
    -> DisableOptional
```

前回の方針:

```text
各APIが実際に持つreturn-code contractを個別に守る
```

を完全にするなら、この分離が望ましい。

## 判定

```text
P3
OPEN
non-blocking for current conforming runtime
```

---

# 20. implementation result MDとの照合

repository内の実装結果MDでは:

```text
PASS (source/model/contract/macOS build)
```

と記録されている。

記録内容:

```text
macOS Vulkan full build, developer features ON
    PASS

macOS Vulkan full build, developer features OFF
    PASS

Vulkan present timing model tests
    PASS

low-latency contract audit
    PASS

aggregate Vulkan latency tests
    PASS

Software parity audit
    PASS

git diff --check
    PASS

Linux Vulkan build
    NOT RUN

Windows Vulkan build
    NOT RUN

validation layer / physical runtime endurance
    NOT RUN
```

source変更と実装結果記述の主要部分は整合している。

ただし今回再監査で見つけた:

```text
same-frame swapchain recreation後のLastDecision generation mismatch
```

は実装結果MDには記載されていない。

従って今回の再監査では:

```text
PASS
```

単独ではなく:

```text
PASS WITH NEW P2 SWAPCHAIN-GENERATION FINDING
```

へ更新する。

---

# 21. GitHub-hosted CI evidence

今回HEADについて監査時点で:

```text
combined commit statuses:
    none

commit-associated workflow runs:
    none
```

だった。

従って:

```text
macOS build PASS
```

はrepository内実装結果MDに記録されたlocal evidenceとして扱う。

GitHub-hosted CI PASSとは扱わない。

---

# 22. platform validation

今回も:

```text
Windows Vulkan build
    NOT RUN

Linux Vulkan build
    NOT RUN

physical GPU runtime
    NOT RUN

validation layer endurance
    NOT RUN
```

である。

Windowsについては、プロジェクトのWindows検証原則上:

```text
実機で未検証の項目を成功扱いしない
```

ため、source監査がPASSでも:

```text
Windows runtime PASS
```

とはしない。

同様にLinuxも未実施。

---

# 23. regression matrix

今回修正後に残すべきmatrix。

| 項目 | Source | Pure test | Contract audit | Runtime |
|---|---:|---:|---:|---:|
| Wait2 SUCCESS | PASS | PASS | PASS | NOT RUN |
| Wait2 TIMEOUT | PASS | PASS | PASS | NOT RUN |
| Wait2 SUBOPTIMAL | PASS | PASS | PASS | NOT RUN |
| Wait2 OUT_OF_DATE | PASS | PASS | PASS | NOT RUN |
| Wait2 DEVICE_LOST | PASS | PASS | PASS | NOT RUN |
| Wait2 SURFACE_LOST | PASS | PASS | PASS | NOT RUN |
| EXT past timing fatal routing | PASS | PASS classifier | PASS | NOT RUN |
| EXT timing properties NOT_READY | PASS | PASS classifier | PASS | NOT RUN |
| EXT time-domain INCOMPLETE | PASS | PASS classifier | PASS | NOT RUN |
| GOOGLE past timing | PASS | PASS classifier | PASS | NOT RUN |
| same-frame recreation decision invalidation | **FAIL / OPEN** | NONE | NONE | NOT RUN |
| API-level injected dispatch | OPEN | NONE | N/A | NOT RUN |
| Windows build | N/A | N/A | N/A | NOT RUN |
| Linux build | N/A | N/A | N/A | NOT RUN |

---

# 24. NEW P2-7 修正後Definition of Done

## lifecycle

- [ ] `LastDecision`がswapchain generationを跨がない
- [ ] `WaitAttemptedThisFrame`がswapchain generationを跨がない
- [ ] `TargetFrameIntervalNs`がold-generation targetへ再利用されない
- [ ] same-frame recreation後のfirst presentはold target permissionを使用しない
- [ ] GOOGLE `desiredPresentTime`をold decisionから生成しない
- [ ] EXT `TimingBackend`をold decisionから選ばない
- [ ] new generation capture rowへold wait attemptを記録しない

## source design

次のどちらかを満たすこと。

### A. generation stamp

- [ ] `DecisionSwapchainGeneration`
- [ ] `WaitAttemptSwapchainGeneration`
- [ ] `PreparePresent()` generation guard
- [ ] `CaptureState()` generation guard

または:

### B. lifecycle reset

- [ ] `ResetTimingLifecycle()`で`LastDecision` invalidation
- [ ] `WaitAttemptedThisFrame = false`
- [ ] `TargetFrameIntervalNs = 0`
- [ ] first new-generation presentがsafe untimed pathへ落ちる

## tests

- [ ] `TestSwapchainRecreationInvalidatesFrameDecision`
- [ ] old generation decision cannot schedule new generation
- [ ] old wait attempt cannot be attributed to new generation
- [ ] GOOGLE target false on generation mismatch
- [ ] EXT backend none on generation mismatch
- [ ] existing Wait2 tests remain PASS
- [ ] existing P2-1 mixed backend tests remain PASS
- [ ] existing P2-2 FIFO_LATEST_READY tests remain PASS

## audit

- [ ] unconditional stale `LastDecision.TimingBackend` useを検出
- [ ] generation guardまたはresetを検出
- [ ] capture generation attributionを検出

## build

- [ ] macOS developer features ON
- [ ] macOS developer features OFF
- [ ] Windows UCRT64 Vulkan build
- [ ] Linux Vulkan build
- [ ] `git diff --check`
- [ ] low-latency contract audit
- [ ] Vulkan present timing model tests
- [ ] aggregate Vulkan latency tests

## runtime

- [ ] resize
- [ ] minimize
- [ ] restore
- [ ] fullscreen toggle
- [ ] VSync toggle
- [ ] SUBOPTIMAL誘発可能なsurface change
- [ ] swapchain generation増加直後のcapture確認
- [ ] validation layerで新規errorなし

---

# 25. 推奨修正順

```text
Step 1
    frame decisionとswapchain generationのcontractを決める

Step 2
    generation stamp方式またはResetTimingLifecycle invalidationを実装

Step 3
    PreparePresent()でold-generation decisionを拒否

Step 4
    CaptureState()でold-generation wait attributionを拒否

Step 5
    pure generation test追加

Step 6
    contract audit追加

Step 7
    GOOGLE classifierをrefresh-cycle / past-timingへ分離

Step 8
    existing model / audit test全実行

Step 9
    macOS / Windows / Linux build

Step 10
    validation-layer resize/minimize/restore matrix

Step 11
    再監査
```

---

# 26. 変更してはいけない既存修正

今回の次修正で以下を戻さないこと。

```text
ClassifyVulkanPresentWait2Result()
```

```text
VK_SUBOPTIMAL_KHR
    -> SwapchainSuboptimal
```

```text
VK_ERROR_SURFACE_LOST_KHR
    -> SurfaceLost
```

```text
VulkanPacerActionFor()
    Suboptimal/OutOfDate -> Rebuild
    DeviceLost/SurfaceLost -> FailRenderer
```

```text
ClassifyVulkanPastTimingResult()
```

```text
ClassifyVulkanTimingPropertiesResult()
```

```text
ClassifyVulkanTimeDomainResult()
```

```text
TimingProperties:
    VK_NOT_READY pending
```

```text
TimeDomain:
    VK_SUCCESS / VK_INCOMPLETE
```

```text
required VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT
```

```text
mixed EXT telemetry + GOOGLE target backend selection
```

```text
GOOGLE + FIFO_LATEST_READY capability path
```

今回の新P2は、これらを置き換える修正ではない。

対象は:

```text
swapchain recreationでframe decisionをinvalidateする
```

こと。

---

# 27. 最終判定

今回Pushは前回指摘への修正としては良好。

特に:

```text
VK_SUBOPTIMAL_KHR
```

を成功statusとして:

```text
RebuildSwapchain
```

へ分け、

```text
VK_ERROR_SURFACE_LOST_KHR
```

を:

```text
FailRenderer
```

へ分けた点は正しい。

また、result contractをpure classifierとして固定し、static contract auditまで追加した方向も正しい。

しかし、`SwapchainSuboptimal -> SwapchainDirty`によって、

```text
old swapchainでBeginFrame
new swapchainでPreparePresent
```

が同一エミュレーションframe内に成立する。

現在:

```text
LastDecision
WaitAttemptedThisFrame
TargetFrameIntervalNs
```

はそのgeneration切替を認識しない。

そのため最終判定:

```text
Wait2 result routing:
    CLOSED

present timing query routing:
    CLOSED

per-frame decision vs swapchain generation:
    OPEN (P2)

API-level fault injection:
    OPEN (P3)

GOOGLE exact per-command classifier:
    OPEN (P3)

Windows/Linux/physical validation:
    OPEN

OVERALL:
    PASS WITH NEW P2 SWAPCHAIN-GENERATION FINDING
```

とする。

---

# 28. 仕様参照

Khronos Vulkan Documentation:

- `vkWaitForPresent2KHR`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForPresent2KHR.html

- `VkResult`
  - https://docs.vulkan.org/refpages/latest/refpages/source/VkResult.html

- `vkGetPastPresentationTimingEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingEXT.html

- `vkGetSwapchainTimingPropertiesEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimingPropertiesEXT.html

- `vkGetSwapchainTimeDomainPropertiesEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimeDomainPropertiesEXT.html

- `vkGetPastPresentationTimingGOOGLE`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingGOOGLE.html

- `vkGetRefreshCycleDurationGOOGLE`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetRefreshCycleDurationGOOGLE.html

- `VK_EXT_present_timing`
  - https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html

---

# 29. GitHub参照

監査HEAD:

```text
https://github.com/ag-advania/melonPrimeDS/tree/54809ed1cf6838c83d9285ee01f50e1d414527f5
```

主要source:

```text
https://github.com/ag-advania/melonPrimeDS/blob/54809ed1cf6838c83d9285ee01f50e1d414527f5/src/VulkanPresentPacer.cpp

https://github.com/ag-advania/melonPrimeDS/blob/54809ed1cf6838c83d9285ee01f50e1d414527f5/src/VulkanPresentPacer.h

https://github.com/ag-advania/melonPrimeDS/blob/54809ed1cf6838c83d9285ee01f50e1d414527f5/src/VulkanPresentPacingPolicy.h

https://github.com/ag-advania/melonPrimeDS/blob/54809ed1cf6838c83d9285ee01f50e1d414527f5/src/frontend/qt_sdl/MelonPrimeVulkanPresenter.cpp

https://github.com/ag-advania/melonPrimeDS/blob/54809ed1cf6838c83d9285ee01f50e1d414527f5/src/frontend/qt_sdl/EmuThread.cpp

https://github.com/ag-advania/melonPrimeDS/blob/54809ed1cf6838c83d9285ee01f50e1d414527f5/tools/testing/vulkan-present-timing-tests.cpp

https://github.com/ag-advania/melonPrimeDS/blob/54809ed1cf6838c83d9285ee01f50e1d414527f5/tools/ci/audits/audit-low-latency-contract.py
```

---

# 30. 次回再監査で確認する一点

最優先はこれ。

```text
OLD swapchain:
    PresentPacer.BeginFrame()
        -> decision generation N

same frame:
    swapchain recreation
        -> generation N+1

NEW swapchain:
    PreparePresent()

必須:
    generation Nのdecisionを絶対に使わない
```

これがsource / test / captureの3層で保証されれば、今回新たに見つかったP2 lifecycle gapはCLOSEDにできる。
