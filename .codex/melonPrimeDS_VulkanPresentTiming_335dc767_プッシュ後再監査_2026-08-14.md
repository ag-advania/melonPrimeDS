# melonPrimeDS Vulkan Present Timing
## `335dc767` Push後 再監査・残件指示書

- 作成日: 2026-08-14
- 対象Repository: `ag-advania/melonPrimeDS`
- 対象Branch: `develop_remakeVulkan_ver3`
- 前回監査HEAD: `54809ed1cf6838c83d9285ee01f50e1d414527f5`
- 今回監査HEAD: `335dc767743d6585cf5bb397b279d745ec4ed4d2`
- HEAD commit: `Fix Vulkan swapchain generation pacing state`
- HEAD parent: `a1952db81964352dbd9d518e9e1b61ef1df404ef`
- 前回監査HEADから: **2 commits ahead / 0 behind**
- 総合判定: **PASS WITH P3 DIAGNOSTIC FINDING**
- 新規P2: **なし**
- 新規P3:
  - generation mismatch captureが`TelemetryOnlyPolicy`を代替fallback reasonとして記録するため、JIT/PresentWait policyの診断意味と一致しない
- 継続P3:
  - production pacerへのAPI-level fake Vulkan dispatch integration testは未実装
- Platform/runtime gates:
  - macOS: repository内実装結果ではPASS
  - Windows: NOT RUN
  - Linux: NOT RUN
  - physical runtime / validation-layer endurance: NOT RUN
  - GitHub-hosted status/workflow: 監査時点で確認できず

---

# 1. 結論

今回Pushされた修正は、前回の最重要残件であった:

```text
OLD swapchain:
    BeginFrame()
        -> decision generation N

same emulation frame:
    swapchain recreation
        -> generation N+1

NEW swapchain:
    PreparePresent()

旧generation NのLastDecisionを
新generation N+1へ使ってしまう
```

という**P2-7 swapchain-generation lifecycle gapを正しく閉じている**。

現HEADでは:

```text
BeginFrame()
    -> DecisionSwapchainGeneration = SwapchainGeneration

wait attempt
    -> WaitAttemptSwapchainGeneration = SwapchainGeneration

swapchain recreation
    -> ResetTimingLifecycle()
        -> LastDecision reset
        -> decision generation = 0
        -> wait generation = 0
        -> TargetFrameIntervalNs = 0
        -> WaitAttemptedThisFrame = false

PreparePresent()
    -> decision generation == current generation ?
        YES: normal pacing decision
        NO:  backend=None / safe untimed path

CaptureState()
    -> old-generation bounded wait permissionを記録しない
    -> old-generation wait attemptを記録しない
    -> old-generation frame intervalを記録しない
```

となっている。

さらに前回P3だった:

```text
vkGetRefreshCycleDurationGOOGLE
vkGetPastPresentationTimingGOOGLE
```

の異なるreturn-code contractも、**別々のclassifierへ分離済み**。

従って前回の:

```text
P2-7:
    OPEN
```

は:

```text
P2-7:
    CLOSED on source/model/contract
```

へ変更する。

ただし再監査で**新しいP3 diagnostic finding**を1件確認した。

generation mismatch時のcaptureは:

```cpp
snapshot.FallbackReason = static_cast<int>(decisionCurrent
        ? FallbackReason
        : VulkanJitFallbackReason::TelemetryOnlyPolicy);
```

となっている。

`TelemetryOnlyPolicy`は本来:

```text
policy自体がTelemetryOnlyなのでtargetを要求しない
```

という意味のfallback reasonである。

しかしsame-frame swapchain recreationは:

```text
policy = JustInTime
```

または:

```text
policy = PresentWait
```

でも発生し得る。

そのため新generationの最初のsafe untimed presentについて:

```text
Policy:
    JustInTime

FallbackReason:
    TelemetryOnlyPolicy
```

という**意味的に矛盾したcapture row**を作れる。

これは現在のpacing behaviorを壊すP2ではない。

generation guardそのものは機能しており、old targetがnew swapchainへ漏れることはない。

したがって:

```text
P3 diagnostic / measurement semantics
```

と判定する。

---

# 2. Push確認

現在のbranch HEAD:

```text
develop_remakeVulkan_ver3
335dc767743d6585cf5bb397b279d745ec4ed4d2
Fix Vulkan swapchain generation pacing state
```

parent:

```text
a1952db81964352dbd9d518e9e1b61ef1df404ef
```

前回監査HEAD:

```text
54809ed1cf6838c83d9285ee01f50e1d414527f5
```

比較:

```text
ahead_by:
    2

behind_by:
    0
```

前回監査HEADから今回HEADまでの主変更:

```text
.codex/
    melonPrimeDS_VulkanPresentTiming_54809ed_プッシュ後再監査_2026-08-14.md
        added

    melonPrimeDS_VulkanPresentTiming_54809ed_プッシュ後再監査_実装結果_2026-08-14.md
        added

    154b7b6時点の旧監査MD / 実装結果MD
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

---

# 3. 前回P2-7
## swapchain generation guard

# 3.1 pure generation helper

`VulkanPresentPacingPolicy.h`に:

```cpp
constexpr bool VulkanFrameDecisionMatchesSwapchain(
    u64 decisionSwapchainGeneration,
    u64 currentSwapchainGeneration) noexcept
{
    return decisionSwapchainGeneration != 0
        && decisionSwapchainGeneration == currentSwapchainGeneration;
}
```

が追加されている。

このhelperによって:

```text
same generation:
    valid

different generation:
    invalid

generation stamp = 0:
    invalid
```

がpure contractとして表現された。

## 判定

```text
PASS
```

---

# 3.2 generation stamps

`VulkanPresentPacer`へ:

```cpp
u64 DecisionSwapchainGeneration = 0;
u64 WaitAttemptSwapchainGeneration = 0;
```

が追加されている。

役割:

```text
DecisionSwapchainGeneration
    -> LastDecisionがどのswapchain generationでresolveされたか

WaitAttemptSwapchainGeneration
    -> vkWaitForPresent2KHRをどのgenerationで実際にattemptしたか
```

permissionとactual attemptを別generation stampへ分けている点も妥当。

## 判定

```text
PASS
```

---

# 3.3 `BeginFrame()`でdecision generationをstamp

current `BeginFrame()`はresolver後:

```cpp
LastDecision = decision;
DecisionSwapchainGeneration = SwapchainGeneration;
```

とする。

従って:

```text
LastDecision
```

単体ではなく:

```text
LastDecision
+
DecisionSwapchainGeneration
```

で一つのframe decisionになる。

## 判定

```text
PASS
```

---

# 3.4 wait attempt generation

bounded waitを実際に呼ぶframeでは:

```cpp
WaitAttemptedThisFrame = true;
WaitAttemptSwapchainGeneration = SwapchainGeneration;
```

となる。

従ってcapture側は:

```text
policy上wait可能だった
```

と:

```text
このgenerationで本当にwaitした
```

を混同しない。

## 判定

```text
PASS
```

---

# 4. lifecycle reset監査

`ResetTimingLifecycle()`は今回、timing modelだけではなくper-frame behavioral stateもinvalidateする。

現在:

```cpp
LastDecision = VulkanPacingDecision{};
DecisionSwapchainGeneration = 0;
WaitAttemptSwapchainGeneration = 0;
TargetFrameIntervalNs = 0;
WaitAttemptedThisFrame = false;
FallbackReason = VulkanJitFallbackReason::TelemetryOnlyPolicy;
```

を実行する。

さらに既存どおり:

```text
TimingModel
GoogleTimingModel
timing properties
time domain
timing queue
feedback
relative cadence
pending lifecycle result
target scheduling active
```

もresetする。

前回問題:

```text
ResetTimingLifecycle()
    -> timing stateだけreset
    -> old LastDecisionだけ残存
```

は解消した。

## 判定

```text
PASS
```

`FallbackReason`だけは後述のP3診断残件がある。

---

# 5. `PreparePresent()` generation guard

現在:

```cpp
const bool decisionCurrent = VulkanFrameDecisionMatchesSwapchain(
    DecisionSwapchainGeneration, SwapchainGeneration);

const VulkanPresentTimingBackend backend = decisionCurrent
    ? LastDecision.TimingBackend
    : VulkanPresentTimingBackend::None;
```

となっている。

これによりsame-frame recreation後:

```text
DecisionSwapchainGeneration:
    0

SwapchainGeneration:
    N+1

decisionCurrent:
    false

backend:
    None
```

となる。

従って新swapchain first presentへ:

```text
old EXT target metadata
old GOOGLE target metadata
old timing backend selection
```

は伝播しない。

## 判定

```text
PASS
```

---

# 6. GOOGLE target generation guard

GOOGLE pathでも:

```cpp
const bool requestTarget =
    decisionCurrent && LastDecision.TargetTimeScheduling;
```

となっている。

これは前回特に重要だった。

GOOGLE modelはfresh swapchainでもabsolute targetをbootstrapできるため、generation guardがなければ:

```text
old generation:
    TargetTimeScheduling = true

new generation:
    GoogleTimingModel.Reset()

PreparePresent:
    old trueを使って
    new GOOGLE desiredPresentTimeを生成
```

が可能だった。

現在は:

```text
decisionCurrent == false
    -> requestTarget = false
```

なので閉じている。

## 判定

```text
PASS
```

---

# 7. EXT path generation guard

EXT pathは:

```text
backend=None
```

によりgeneration mismatch時にtiming metadata自体へ入らない。

したがって以前の:

```text
resetされたmodelのbootstrap guardが
たまたまtarget=0へ落としてくれる
```

という偶然の安全性に依存しない。

現在は明示的に:

```text
old decision
    -> invalid
```

としている。

## 判定

```text
PASS
```

---

# 8. safe first present

generation mismatch時でもpresentそのものを捨てるのではなく:

```text
timing backend:
    None

target:
    None

present:
    safe untimed / ID-only
```

へ落とす設計になっている。

これはdraw中に:

```text
PresentPacer.BeginFrame()
```

を再実行しないため、pre-input pacing orderingも壊さない。

次のエミュレーションframeで:

```text
BeginFrame()
    -> new generation capabilities
    -> fresh decision
```

へ戻る。

前回指示した責務分離:

```text
recreation:
    decision invalidate

same frame first present:
    safe untimed

next frame:
    fresh resolve
```

が実装された。

## 判定

```text
PASS
```

---

# 9. Capture generation監査

current `CaptureState()`:

```cpp
const bool decisionCurrent = VulkanFrameDecisionMatchesSwapchain(
    DecisionSwapchainGeneration, SwapchainGeneration);

const bool waitAttemptCurrent = VulkanFrameDecisionMatchesSwapchain(
    WaitAttemptSwapchainGeneration, SwapchainGeneration);

snapshot.BoundedPresentWait =
    decisionCurrent && LastDecision.BoundedPresentWait;

snapshot.BoundedWaitAttempted =
    waitAttemptCurrent && WaitAttemptedThisFrame;

snapshot.FrameIntervalNs =
    decisionCurrent ? TargetFrameIntervalNs : 0;
```

となる。

従ってsame-frame recreate後のcapture rowは:

```text
SwapchainGeneration:
    NEW

BoundedPresentWait:
    false

BoundedWaitAttempted:
    false

FrameIntervalNs:
    0
```

となり、旧generationのwait attributionを新generationへ混ぜない。

これは前回のA/B計測上のP2も閉じている。

## 判定

```text
PASS
```

---

# 10. generation pure test

新規:

```cpp
TestSwapchainRecreationInvalidatesFrameDecision()
```

が追加されている。

確認している項目:

```text
decision generation == current generation
    -> true

decision generation != current generation
    -> false

decision generation == 0
    -> false

old decision generation 41
new swapchain generation 42
    -> stale target schedulingは適用されない

old decision generation 41
new swapchain generation 42
    -> stale backendはNone

old wait generation 41
new swapchain generation 42
    -> old wait attemptはnew generationへ帰属しない

reset decision generation 0
    -> valid decisionとして扱わない
```

`main()`からも呼ばれている。

## 判定

```text
PASS
```

---

# 11. contract audit

`audit-low-latency-contract.py`も今回のgeneration contractを検査する。

確認対象:

```text
VulkanFrameDecisionMatchesSwapchain

DecisionSwapchainGeneration
WaitAttemptSwapchainGeneration

ResetTimingLifecycle:
    LastDecision = VulkanPacingDecision{}
    DecisionSwapchainGeneration = 0
    WaitAttemptedThisFrame = false
    WaitAttemptSwapchainGeneration = 0
    TargetFrameIntervalNs = 0

PreparePresent:
    VulkanFrameDecisionMatchesSwapchain
    decisionCurrent
    decisionCurrent ? LastDecision.TimingBackend : None

CaptureState:
    generation matching
    WaitAttemptSwapchainGeneration
    decisionCurrent ? TargetFrameIntervalNs : 0

TestSwapchainRecreationInvalidatesFrameDecision
```

static contractとしても前回P2-7のregressionを検出可能になった。

## 判定

```text
PASS
```

---

# 12. GOOGLE API contract分離
## 前回P3 CLOSED

前回は:

```cpp
ClassifyVulkanGoogleTimingResult()
```

を:

```text
vkGetRefreshCycleDurationGOOGLE
vkGetPastPresentationTimingGOOGLE
```

の両方で共有していた。

しかしKhronos current refpageではreturn-code contractが違う。

---

# 12.1 `vkGetRefreshCycleDurationGOOGLE`

current Khronos:

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

現在のsourceは専用:

```cpp
ClassifyVulkanGoogleRefreshCycleResult()
```

へ分離した。

概略:

```text
VK_SUCCESS
    -> Continue

VK_ERROR_DEVICE_LOST
    -> DeviceLost

VK_ERROR_SURFACE_LOST_KHR
    -> SurfaceLost

other
    -> DisableOptional
```

従ってrefresh-cycle queryに:

```text
VK_INCOMPLETE
VK_ERROR_OUT_OF_DATE_KHR
```

を正規success/lifecycle resultとして認めない。

## 判定

```text
PASS
CLOSED
```

---

# 12.2 `vkGetPastPresentationTimingGOOGLE`

current Khronos:

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

現在のsourceは専用:

```cpp
ClassifyVulkanGooglePastTimingResult()
```

へ分離した。

概略:

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

## 判定

```text
PASS
CLOSED
```

---

# 12.3 GOOGLE contract pure tests

`TestPresentTimingQueryContractClassification()`も更新され:

```text
refresh-cycle:
    INCOMPLETEをsuccess扱いしない
    OUT_OF_DATEをrefresh-cycle lifecycle result扱いしない

past-timing:
    INCOMPLETEをContinue
    OUT_OF_DATEをSwapchainOutOfDate
```

という差を固定している。

## 判定

```text
PASS
```

---

# 13. WaitForPresent2 regression監査

前回までに修正された:

```text
VK_SUBOPTIMAL_KHR
    -> SwapchainSuboptimal
    -> RebuildSwapchain

VK_ERROR_OUT_OF_DATE_KHR
    -> SwapchainOutOfDate
    -> RebuildSwapchain

VK_ERROR_DEVICE_LOST
    -> DeviceLost
    -> FailRenderer

VK_ERROR_SURFACE_LOST_KHR
    -> SurfaceLost
    -> FailRenderer

VK_TIMEOUT
    -> WaitTimeouts++
    -> Continue

other optional failure
    -> DisableWait
    -> Continue
```

は今回も維持されている。

Khronos `vkWaitForPresent2KHR` current refpageも:

```text
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

である。

## 判定

```text
PASS
previous P2 remains CLOSED
```

---

# 14. EXT timing query regression監査

今回のgeneration修正によって、既存のEXT result routingを壊すregressionは確認しなかった。

維持されている。

```text
GetPastPresentationTimingEXT:
    SUCCESS / INCOMPLETE
        -> Continue

    OUT_OF_DATE
        -> SwapchainOutOfDate

    DEVICE_LOST
        -> DeviceLost

    SURFACE_LOST
        -> SurfaceLost

    other
        -> DisableOptional
```

```text
GetSwapchainTimingPropertiesEXT:
    SUCCESS
        -> Continue

    NOT_READY
        -> RetryAfterPresent

    SURFACE_LOST
        -> SurfaceLost

    other
        -> DisableTargetLifecycle
```

```text
GetSwapchainTimeDomainPropertiesEXT:
    SUCCESS
        -> Continue

    INCOMPLETE
        -> bounded enumeration retry

    SURFACE_LOST
        -> SurfaceLost

    other
        -> DisableTargetLifecycle
```

time-domainで`VK_NOT_READY`をpendingとして扱う旧誤りも再導入されていない。

## 判定

```text
PASS
```

---

# 15. mixed EXT + GOOGLE regression

既存のpolicy-aware backend selection:

```text
SelectVulkanPresentTimingBackend()
SelectVulkanPresentTargetBackend()
SelectVulkanPresentBackendForPolicy()
VulkanShouldPollGoogleForFrame()
```

は維持されている。

generation mismatchでは:

```text
backend=None
```

へ安全に落とすだけで、次frameではnew generation capabilitiesから通常resolverが再実行される。

従って:

```text
EXT telemetry available
EXT target unavailable
GOOGLE target available
```

のmixed matrixへ旧selector driftを再導入していない。

## 判定

```text
P2-1:
    CLOSED
```

---

# 16. GOOGLE + FIFO_LATEST_READY regression

`VulkanTargetCanUseFifoLatestReady()`を含む既存capability pathも維持される。

今回のgeneration invalidationは:

```text
same-frame new swapchain first presentをuntimedにする
```

だけであり、次frameのbackend selection / latest-ready eligibilityを変更しない。

## 判定

```text
P2-2:
    CLOSED on source/model
```

---

# 17. NEW P3
## generation mismatch captureに`TelemetryOnlyPolicy`を使っている

ここが今回の新規残件。

current `CaptureState()`:

```cpp
snapshot.FallbackReason = static_cast<int>(decisionCurrent
        ? FallbackReason
        : VulkanJitFallbackReason::TelemetryOnlyPolicy);
```

となっている。

また`ResetTimingLifecycle()`自身も:

```cpp
FallbackReason = VulkanJitFallbackReason::TelemetryOnlyPolicy;
```

へresetする。

---

# 17.1 `TelemetryOnlyPolicy`の定義意味

`VulkanJitFallbackReason`のコメント:

```text
Why a target presentation time was not requested.
```

enum:

```cpp
None = 0,
TelemetryOnlyPolicy,
PresentWaitPolicyNoTarget,
VendorLatencyApiOwnsPacing,
...
```

つまり:

```text
TelemetryOnlyPolicy
```

は:

```text
現在のpolicyがTelemetryOnlyだから
target presentation timeを要求しなかった
```

という意味である。

これは単なる:

```text
targetなし
```

のgeneric placeholderではない。

---

# 17.2 generation mismatchはpolicy変更ではない

same-frame recreationは:

```text
Policy:
    JustInTime
```

でも:

```text
Policy:
    JustInTimeFifoLatestReady
```

でも:

```text
Policy:
    PresentWait
```

でも起こり得る。

そのframeでdecisionがinvalidateされた理由は:

```text
TelemetryOnly policyだから
```

ではなく:

```text
swapchain generationが変わり、
pre-inputのold-generation decisionを
new-generation presentへ使えないから
```

である。

従って:

```text
Policy = JustInTime

FallbackReason = TelemetryOnlyPolicy
```

というrowは意味的に誤る。

---

# 17.3 behaviorへの影響

重要:

この問題によって:

```text
old targetがnew swapchainへ漏れる
```

ことはない。

generation guard:

```text
backend=None
requestTarget=false
```

は正しく働いている。

従って:

```text
render/pacing correctness:
    PASS
```

のまま。

影響は:

```text
developer log
A/B capture CSV
fallback reason analysis
future automated diagnosis
```

である。

そのため:

```text
P3 diagnostic / measurement semantics
```

とする。

---

# 17.4 推奨修正

`VulkanJitFallbackReason`末尾へ新しいreasonを追加する。

既存numeric CSV compatibilityを維持するため、**末尾へ追加**すること。

例:

```cpp
FrameDecisionInvalidatedBySwapchainRecreation,
```

または:

```cpp
SwapchainRecreatedAwaitingFrameDecision,
```

推奨名:

```cpp
FrameDecisionInvalidatedBySwapchainRecreation
```

理由:

```text
何が起きたか:
    frame decision invalidated

なぜ:
    swapchain recreation
```

が一意。

---

# 17.5 reset側

```cpp
FallbackReason =
    VulkanJitFallbackReason::FrameDecisionInvalidatedBySwapchainRecreation;
```

ただし`ResetTimingLifecycle()`は:

```text
Initialize
Shutdown
swapchain destroy
swapchain recreate
```

等から使われる可能性があるため、より厳密にはreasonをreset functionへ固定埋め込みするより:

```text
OnSwapchainDestroyed / recreation path
```

で明示する設計の方がよい。

例えば:

```cpp
void ResetTimingLifecycle(
    VulkanJitFallbackReason invalidationReason =
        VulkanJitFallbackReason::None);
```

のような拡張も可能だが、過剰なら:

```text
ResetTimingLifecycle:
    None

OnSwapchainDestroyed:
    FrameDecisionInvalidatedBySwapchainRecreation
```

でもよい。

重要なのは:

```text
TelemetryOnlyPolicy
```

をpolicyと無関係なgeneration mismatch sentinelとして使わないこと。

---

# 17.6 CaptureState側

現在:

```cpp
decisionCurrent
    ? FallbackReason
    : TelemetryOnlyPolicy
```

を:

```cpp
decisionCurrent
    ? FallbackReason
    : VulkanJitFallbackReason::FrameDecisionInvalidatedBySwapchainRecreation
```

へする。

あるいはreset済み`FallbackReason`自体を使用する。

推奨は:

```cpp
snapshot.FallbackReason =
    static_cast<int>(decisionCurrent
        ? FallbackReason
        : VulkanJitFallbackReason::FrameDecisionInvalidatedBySwapchainRecreation);
```

とし、capture側でもgeneration mismatchを自己完結して表現する。

---

# 18. NEW P3 test

追加する。

例:

```cpp
void TestSwapchainRecreationFallbackReason()
```

必須ケース:

```text
Policy = JustInTime
Decision generation = 41
Current generation = 42

Expected:
    target = false
    backend = None
    fallback = FrameDecisionInvalidatedBySwapchainRecreation

Forbidden:
    TelemetryOnlyPolicy
```

さらに:

```text
Policy = JustInTimeFifoLatestReady
Policy = PresentWait
```

でもsame explicit lifecycle reasonになること。

---

# 19. contract audit追加

`audit-low-latency-contract.py`へ:

```text
1.
FrameDecisionInvalidatedBySwapchainRecreationがenum末尾に存在

2.
decisionCurrent == false時に
TelemetryOnlyPolicyをcapture sentinelとして使わない

3.
CaptureState generation mismatchが
explicit recreation reasonを記録

4.
TestSwapchainRecreationFallbackReasonが存在
```

を追加。

静的に:

```text
: VulkanJitFallbackReason::TelemetryOnlyPolicy
```

がgeneration mismatch ternaryに残っていたらFAILにしてよい。

---

# 20. API-level fake Vulkan dispatch
## 継続P3

repository内実装結果MD自身も:

```text
API-level fake Vulkan dispatch:
    NOT RUN
```

としている。

現在のtestsは:

```text
pure capability model
pure lifecycle classifier
pure generation helper
source contract audit
```

が中心。

まだproduction functionへfake dispatchを注入し:

```text
GetPastPresentationTimingEXT
GetSwapchainTimingPropertiesEXT
GetSwapchainTimeDomainPropertiesEXT
GetRefreshCycleDurationGOOGLE
GetPastPresentationTimingGOOGLE
WaitForPresent2KHR
```

から任意`VkResult`を返して:

```text
return value
state mutation
retry flags
runtime disable
pending result
generation reset
```

を直接testするintegration harnessはない。

今回のsource correctnessを否定するものではない。

ただし:

```text
result classifier:
    correct

production side effects:
    source audit中心
```

なのでfuture hardeningとして残す。

## 判定

```text
P3
OPEN
```

---

# 21. 実装結果MDとの照合

repositoryの:

```text
.codex/
melonPrimeDS_VulkanPresentTiming_54809ed_プッシュ後再監査_実装結果_2026-08-14.md
```

は今回:

```text
PASS (source/model/contract/macOS build)
```

としている。

記載gate:

```text
macOS Vulkan developer features ON:
    PASS

macOS Vulkan developer features OFF:
    PASS

current Vulkan present timing model:
    PASS

developer features OFF model:
    PASS

low-latency contract audit:
    PASS

aggregate Vulkan latency tests:
    PASS

Software parity audit:
    PASS

git diff --check:
    PASS

Linux Vulkan:
    NOT RUN

Windows Vulkan:
    NOT RUN

API-level fake Vulkan dispatch:
    NOT RUN

validation layer / physical runtime:
    NOT RUN
```

GitHub source差分と記述内容の主要部分は整合している。

ただしこの再監査では:

```text
generation mismatch fallback reason
```

の意味的不整合を新規P3として追加する。

---

# 22. GitHub-hosted CI

監査時点のHEAD:

```text
335dc767743d6585cf5bb397b279d745ec4ed4d2
```

についてGitHub connectorで確認した範囲:

```text
combined commit statuses:
    none

commit-associated workflow runs:
    none
```

従ってrepository内実装結果MDのmacOS PASSは:

```text
repository-authored local build evidence
```

として扱う。

```text
GitHub-hosted CI PASS
```

とは扱わない。

---

# 23. platform validation

現状:

```text
macOS:
    repository-authored local build PASS

Windows:
    NOT RUN

Linux:
    NOT RUN

physical Vulkan runtime:
    NOT RUN

validation layer:
    NOT RUN

SUBOPTIMAL-induced recreation runtime:
    NOT RUN

resize/minimize/restore endurance:
    NOT RUN
```

source/model/contractのCLOSED判定と、physical runtime gateは分けて扱う。

---

# 24. 前回DoDとの照合

## P2-7 lifecycle

- [x] `DecisionSwapchainGeneration`
- [x] `WaitAttemptSwapchainGeneration`
- [x] `BeginFrame()` decision stamp
- [x] wait attempt stamp
- [x] lifecycle resetでdecision invalidate
- [x] lifecycle resetでwait attribution invalidate
- [x] lifecycle resetでframe interval invalidate
- [x] `PreparePresent()` generation guard
- [x] GOOGLE target generation guard
- [x] EXT backend generation guard
- [x] `CaptureState()` bounded wait generation guard
- [x] `CaptureState()` actual wait generation guard
- [x] `CaptureState()` frame interval generation guard
- [x] generation pure test
- [x] contract audit

## GOOGLE classifier P3

- [x] refresh-cycle専用classifier
- [x] past-timing専用classifier
- [x] refresh-cycle `VK_INCOMPLETE`をsuccessにしない
- [x] refresh-cycle `OUT_OF_DATE`をlisted lifecycle result扱いしない
- [x] past-timing `VK_INCOMPLETE`
- [x] past-timing `OUT_OF_DATE`
- [x] pure contract test
- [x] production source usage

## test/runtime

- [x] pure source/model/contract coverage
- [ ] API-level fake Vulkan dispatch
- [ ] Windows build
- [ ] Linux build
- [ ] physical runtime
- [ ] validation-layer endurance

---

# 25. 今回新P3 Definition of Done

## fallback semantics

- [ ] generation mismatch専用fallback reasonを追加
- [ ] enum末尾へ追加して既存numeric値を維持
- [ ] JIT policyで`TelemetryOnlyPolicy`を記録しない
- [ ] PresentWait policyでも`TelemetryOnlyPolicy`を誤用しない
- [ ] same-frame recreation rowが明示的なlifecycle reasonを持つ

## tests

- [ ] `TestSwapchainRecreationFallbackReason`
- [ ] JustInTime
- [ ] JustInTimeFifoLatestReady
- [ ] PresentWait
- [ ] generation mismatch
- [ ] `TelemetryOnlyPolicy`禁止
- [ ] explicit lifecycle reason確認

## audit

- [ ] generation mismatch ternaryのTelemetryOnlyPolicy sentinelを禁止
- [ ] explicit reason存在を確認
- [ ] enum numeric stabilityを確認

## existing regression

- [ ] `TestSwapchainRecreationInvalidatesFrameDecision` PASS
- [ ] `TestPresentWait2ResultClassification` PASS
- [ ] `TestPresentTimingQueryContractClassification` PASS
- [ ] P2-1 backend matrix PASS
- [ ] P2-2 FIFO_LATEST_READY matrix PASS
- [ ] low-latency contract audit PASS
- [ ] aggregate Vulkan latency tests PASS
- [ ] `git diff --check` PASS

---

# 26. 推奨修正順

```text
Step 1
    VulkanJitFallbackReason末尾へ
    FrameDecisionInvalidatedBySwapchainRecreation追加

Step 2
    CaptureStateのgeneration mismatch fallbackを差し替え

Step 3
    lifecycle reset時のFallbackReason設定も意味を統一

Step 4
    pure fallback test追加

Step 5
    contract audit追加

Step 6
    current Vulkan timing tests全実行

Step 7
    macOS build再確認

Step 8
    Windows / Linux build gate

Step 9
    validation-layer resize/minimize/restore/SUBOPTIMAL matrix

Step 10
    再監査
```

---

# 27. 禁止事項

今回の次修正で以下を戻さないこと。

```text
DecisionSwapchainGeneration
WaitAttemptSwapchainGeneration
```

```text
VulkanFrameDecisionMatchesSwapchain()
```

```text
generation mismatch:
    backend=None
```

```text
GOOGLE:
    requestTarget = decisionCurrent && LastDecision.TargetTimeScheduling
```

```text
CaptureState:
    old wait attemptをnew generationへ帰属させない
```

```text
ClassifyVulkanGoogleRefreshCycleResult()
```

```text
ClassifyVulkanGooglePastTimingResult()
```

```text
ClassifyVulkanPresentWait2Result()
```

```text
SUBOPTIMAL:
    RebuildSwapchain
```

```text
SURFACE_LOST:
    FailRenderer
```

今回の修正対象は:

```text
behavioral generation guard
```

ではなく:

```text
generation mismatch frameを
診断上何と呼ぶか
```

だけである。

---

# 28. 最終監査判定

今回Pushにより、前回P2-7は正しく閉じている。

```text
same-frame swapchain recreation stale decision:
    CLOSED

old generation GOOGLE target leakage:
    CLOSED

old generation EXT backend leakage:
    CLOSED

old generation wait attribution:
    CLOSED

old generation frame interval attribution:
    CLOSED

GOOGLE exact command classifiers:
    CLOSED

Wait2 lifecycle:
    CLOSED

EXT timing lifecycle:
    CLOSED
```

今回確認した新規残件:

```text
P3:
    generation mismatch capture fallback reason
    = TelemetryOnlyPolicy
    -> semantic mismatch
    OPEN
```

継続残件:

```text
P3:
    API-level fake Vulkan dispatch integration
    OPEN

Windows:
    NOT RUN

Linux:
    NOT RUN

physical runtime / validation layer:
    NOT RUN
```

従って総合判定:

```text
PASS WITH P3 DIAGNOSTIC FINDING
NO NEW P2 FOUND
```

とする。

---

# 29. Khronos仕様参照

- `vkWaitForPresent2KHR`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForPresent2KHR.html

- `vkGetPastPresentationTimingGOOGLE`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingGOOGLE.html

- `vkGetRefreshCycleDurationGOOGLE`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetRefreshCycleDurationGOOGLE.html

- `VkResult`
  - https://docs.vulkan.org/refpages/latest/refpages/source/VkResult.html

- `vkGetPastPresentationTimingEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingEXT.html

- `vkGetSwapchainTimingPropertiesEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimingPropertiesEXT.html

- `vkGetSwapchainTimeDomainPropertiesEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimeDomainPropertiesEXT.html

---

# 30. GitHub参照

監査HEAD:

```text
https://github.com/ag-advania/melonPrimeDS/tree/335dc767743d6585cf5bb397b279d745ec4ed4d2
```

主要source:

```text
https://github.com/ag-advania/melonPrimeDS/blob/335dc767743d6585cf5bb397b279d745ec4ed4d2/src/VulkanPresentPacer.cpp

https://github.com/ag-advania/melonPrimeDS/blob/335dc767743d6585cf5bb397b279d745ec4ed4d2/src/VulkanPresentPacer.h

https://github.com/ag-advania/melonPrimeDS/blob/335dc767743d6585cf5bb397b279d745ec4ed4d2/src/VulkanPresentPacingPolicy.h

https://github.com/ag-advania/melonPrimeDS/blob/335dc767743d6585cf5bb397b279d745ec4ed4d2/tools/testing/vulkan-present-timing-tests.cpp

https://github.com/ag-advania/melonPrimeDS/blob/335dc767743d6585cf5bb397b279d745ec4ed4d2/tools/ci/audits/audit-low-latency-contract.py
```

---

# 31. 次回再監査の最優先確認

```text
Policy = JustInTime
OLD generation decision = N

same frame:
    swapchain recreation

NEW generation = N+1
decisionCurrent = false

必須capture:
    TargetTimeScheduling = false
    TimingBackend = None
    BoundedPresentWait = false
    BoundedWaitAttempted = false
    FrameIntervalNs = 0
    FallbackReason =
        FrameDecisionInvalidatedBySwapchainRecreation

禁止:
    FallbackReason = TelemetryOnlyPolicy
```

この診断意味まで閉じれば、現時点で静的に確認できるswapchain-generation pacing stateの残件はP2/P3ともかなり整理された状態になる。
