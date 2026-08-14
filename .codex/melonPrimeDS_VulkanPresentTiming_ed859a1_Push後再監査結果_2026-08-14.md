# melonPrimeDS Vulkan Present Timing

## ed859a1 Push後 再監査結果

- 作成日: 2026-08-14
- Repository: ag-advania/melonPrimeDS
- Branch: develop_remakeVulkan_ver3
- 前回監査HEAD: `335dc767743d6585cf5bb397b279d745ec4ed4d2`
- 今回HEAD: `ed859a1bcc5e58fc7b3b2d55a14f1f0feedb047e`
- HEAD commit: `Fix swapchain recreation fallback reason`
- 前回HEADから: `2 commits ahead / 0 behind`
- 総合判定: `PASS WITH EXISTING P3 TEST-HARNESS / PLATFORM VALIDATION GAPS`
- 前回P3 diagnostic finding: `CLOSED`
- 今回新規P2: なし
- 今回新規P3 source finding: なし

---

## 1. 結論

今回のPushは、前回監査で残していた次のP3 diagnostic findingを正しく修正している。

```text
same-frame swapchain recreation
    ↓
旧generationのframe decisionがinvalid
    ↓
CaptureState()
    ↓
FallbackReason = TelemetryOnlyPolicy

しかし実際のpolicyは
    JustInTime
    JustInTimeFifoLatestReady
    PresentWait
の可能性がある
```

現HEADでは、専用reason:

```cpp
FrameDecisionInvalidatedBySwapchainRecreation
```

が追加され、generation mismatch時のcaptureはこのreasonを使用するようになった。

また`ResetTimingLifecycle()`はpolicy-specificな`TelemetryOnlyPolicy`ではなく、neutralな`None`へ戻すよう修正された。

さらに:

- `JustInTime`
- `JustInTimeFifoLatestReady`
- `PresentWait`

の3 policyについてgeneration mismatch時に`TelemetryOnlyPolicy`を使わないpure testが追加され、contract auditにも固定された。

したがって前回のP3 diagnostic findingは`CLOSED`としてよい。

今回差分を起点に既存のswapchain generation guard、WaitForPresent2 result routing、EXT present timing、GOOGLE timing classifier、mixed EXT+GOOGLE、FIFO_LATEST_READY経路も再監査したが、今回の修正によるregressionは確認しなかった。

今回新規のP2/P3 source-level functional defectは確認できなかった。

ただし以下は継続残件である。

```text
P3:
    API-level fake Vulkan dispatch integration
    OPEN

Windows build:
    NOT RUN

Linux build:
    NOT RUN

physical Vulkan runtime:
    NOT RUN

validation layer / endurance:
    NOT RUN
```

---

## 2. Push確認

現在のbranch HEAD:

```text
develop_remakeVulkan_ver3
ed859a1bcc5e58fc7b3b2d55a14f1f0feedb047e
Fix swapchain recreation fallback reason
```

前回監査HEAD:

```text
335dc767743d6585cf5bb397b279d745ec4ed4d2
```

比較:

```text
ahead_by = 2
behind_by = 0
```

2 commits:

```text
b2ed8311d86593d0cce7014dd8e8edc6eb77f673
    add md

ed859a1bcc5e58fc7b3b2d55a14f1f0feedb047e
    Fix swapchain recreation fallback reason
```

主な変更対象:

```text
src/VulkanPresentPacer.cpp
src/VulkanPresentPacingPolicy.h
tools/testing/vulkan-present-timing-tests.cpp
tools/ci/audits/audit-low-latency-contract.py

.codex/
    melonPrimeDS_VulkanPresentTiming_335dc767_プッシュ後再監査_2026-08-14.md
    melonPrimeDS_VulkanPresentTiming_335dc767_プッシュ後再監査_実装結果_2026-08-14.md
```

---

## 3. 前回P3の修正監査

### 3.1 専用fallback reason

`VulkanPresentPacingPolicy.h`へ以下が追加されている。

```cpp
FrameDecisionInvalidatedBySwapchainRecreation,
```

位置は既存enum末尾:

```cpp
PresentWait2Unsupported,
TimingQueuePressure,
FrameDecisionInvalidatedBySwapchainRecreation,
```

となっている。

既存enum値の途中へ挿入していないため、既存capture CSV等で数値化されたfallback reasonのnumeric compatibilityを維持する設計になっている。

**判定: PASS**

---

### 3.2 pure fallback helper

新規helper:

```cpp
constexpr VulkanJitFallbackReason VulkanFrameDecisionFallbackReason(
    bool decisionCurrent,
    VulkanJitFallbackReason currentReason) noexcept
{
    return decisionCurrent
        ? currentReason
        : VulkanJitFallbackReason::FrameDecisionInvalidatedBySwapchainRecreation;
}
```

により:

```text
current generation
    -> 現在のreasonを維持

stale generation
    -> swapchain recreationによるdecision invalidationを明示
```

となった。

前回のように`TelemetryOnlyPolicy`をgeneration mismatch sentinelとして流用していない。

**判定: PASS**

---

## 4. ResetTimingLifecycle()監査

現HEADではreset時:

```cpp
LastDecision = VulkanPacingDecision{};
DecisionSwapchainGeneration = 0;
WaitAttemptSwapchainGeneration = 0;
TargetFrameIntervalNs = 0;
WaitAttemptedThisFrame = false;
FallbackReason = VulkanJitFallbackReason::None;
```

となっている。

これは正しい。

`ResetTimingLifecycle()`は内部stateをneutralへ戻す責務であり、policyがTelemetryOnlyであることを意味する場所ではない。

前回:

```cpp
FallbackReason = VulkanJitFallbackReason::TelemetryOnlyPolicy;
```

としていたsemantic misuseは除去された。

**判定: PASS**

---

## 5. CaptureState()監査

現HEAD:

```cpp
const bool decisionCurrent = VulkanFrameDecisionMatchesSwapchain(
    DecisionSwapchainGeneration, SwapchainGeneration);

const bool waitAttemptCurrent = VulkanFrameDecisionMatchesSwapchain(
    WaitAttemptSwapchainGeneration, SwapchainGeneration);

snapshot.BoundedPresentWait =
    decisionCurrent && LastDecision.BoundedPresentWait;

snapshot.BoundedWaitAttempted =
    waitAttemptCurrent && WaitAttemptedThisFrame;

snapshot.FallbackReason = static_cast<int>(
    VulkanFrameDecisionFallbackReason(
        decisionCurrent, FallbackReason));

snapshot.FrameIntervalNs =
    decisionCurrent ? TargetFrameIntervalNs : 0;
```

same-frame recreation:

```text
DecisionSwapchainGeneration = N
SwapchainGeneration = N+1
```

なら:

```text
decisionCurrent = false

BoundedPresentWait = false
BoundedWaitAttempted = false
FrameIntervalNs = 0
FallbackReason =
    FrameDecisionInvalidatedBySwapchainRecreation
```

となる。

したがって:

```text
Policy = JustInTime
FallbackReason = TelemetryOnlyPolicy
```

という前回の意味的矛盾は解消した。

**判定: PASS / 前回P3 CLOSED**

---

## 6. runtime diagnostic name

`VulkanJitFallbackReasonName()`にも:

```cpp
case VulkanJitFallbackReason::FrameDecisionInvalidatedBySwapchainRecreation:
    return "frame decision invalidated by swapchain recreation";
```

が追加されている。

capture数値だけでなくdeveloper logでも原因を判別可能。

**判定: PASS**

---

## 7. EvaluateTargetTiming() defensive guard

current sourceはgeneration mismatch時:

```cpp
if (!VulkanFrameDecisionMatchesSwapchain(
        DecisionSwapchainGeneration,
        SwapchainGeneration))
{
    FallbackReason =
        VulkanJitFallbackReason::FrameDecisionInvalidatedBySwapchainRecreation;
    return result;
}
```

としている。

一方、通常のsame-frame recreationは`PreparePresent()`で先に:

```text
decisionCurrent = false
backend = None
```

へ落ちる。

したがって安全性は:

```text
Primary:
    PreparePresent generation guard

Secondary:
    EvaluateTargetTiming defensive generation guard
```

の二重構造であり、fallback修正のために既存behavioral guardを弱めていない。

**判定: PASS**

---

## 8. swapchain generation regression監査

今回も以下を維持している。

`BeginFrame()`

```cpp
LastDecision = decision;
DecisionSwapchainGeneration = SwapchainGeneration;
```

bounded wait actual attempt

```cpp
WaitAttemptedThisFrame = true;
WaitAttemptSwapchainGeneration = SwapchainGeneration;
```

reset

```text
LastDecision = {}
DecisionSwapchainGeneration = 0
WaitAttemptSwapchainGeneration = 0
WaitAttemptedThisFrame = false
TargetFrameIntervalNs = 0
```

`PreparePresent()`

```cpp
const bool decisionCurrent =
    VulkanFrameDecisionMatchesSwapchain(
        DecisionSwapchainGeneration,
        SwapchainGeneration);

const VulkanPresentTimingBackend backend =
    decisionCurrent
        ? LastDecision.TimingBackend
        : VulkanPresentTimingBackend::None;
```

これにより旧generationのEXT/GOOGLE timing backendやtarget permissionはnew swapchainへ漏れない。

**判定: PASS / P2-7 remains CLOSED**

---

## 9. test監査

新規:

```cpp
TestSwapchainRecreationFallbackReason()
```

を確認した。

coverage:

```text
current decision + None
    -> None

current decision + PresentWaitPolicyNoTarget
    -> PresentWaitPolicyNoTarget
```

さらに:

```text
JustInTime
JustInTimeFifoLatestReady
PresentWait
```

について:

```text
old generation = 41
current generation = 42
decisionCurrent = false

Expected:
    FrameDecisionInvalidatedBySwapchainRecreation

Forbidden:
    TelemetryOnlyPolicy
```

を固定している。

`main()`からも実行される。

これは前回DoDを満たしている。

**判定: PASS**

---

## 10. contract audit監査

`audit-low-latency-contract.py`にも以下が追加されている。

```text
VulkanFrameDecisionFallbackReasonの存在

enum ordering:
    TimingQueuePressure
    FrameDecisionInvalidatedBySwapchainRecreation

runtime nameの存在

ResetTimingLifecycle:
    FallbackReason = None

ResetTimingLifecycle:
    TelemetryOnlyPolicyを使用しない

EvaluateTargetTiming:
    explicit recreation reason

CaptureState:
    VulkanFrameDecisionFallbackReason(...)

TestSwapchainRecreationFallbackReasonの存在
```

今後同じsemantic regressionが入ればsource contract auditで検出できる。

**判定: PASS**

---

## 11. WaitForPresent2 regression監査

既存result contractは維持されている。

```text
VK_SUCCESS
    -> Continue

VK_TIMEOUT
    -> Continue

VK_SUBOPTIMAL_KHR
    -> SwapchainSuboptimal
    -> swapchain rebuild

VK_ERROR_OUT_OF_DATE_KHR
    -> SwapchainOutOfDate
    -> swapchain rebuild

VK_ERROR_DEVICE_LOST
    -> DeviceLost
    -> renderer failure

VK_ERROR_SURFACE_LOST_KHR
    -> SurfaceLost
    -> renderer failure

other
    -> DisableOptional
```

今回差分でこのroutingを壊す変更は確認しなかった。

**判定: PASS**

---

## 12. EXT present timing regression監査

今回差分によるregressionは確認しなかった。

```text
vkGetPastPresentationTimingEXT

SUCCESS / INCOMPLETE
    -> Continue

OUT_OF_DATE
    -> SwapchainOutOfDate

DEVICE_LOST
    -> DeviceLost

SURFACE_LOST
    -> SurfaceLost
```

```text
vkGetSwapchainTimingPropertiesEXT

SUCCESS
    -> Continue

NOT_READY
    -> RetryAfterPresent

SURFACE_LOST
    -> SurfaceLost
```

```text
vkGetSwapchainTimeDomainPropertiesEXT

SUCCESS
    -> Continue

INCOMPLETE
    -> bounded enumeration retry

SURFACE_LOST
    -> SurfaceLost
```

time-domain enumerationで`VK_NOT_READY`をpending扱いする旧誤りも再導入されていない。

**判定: PASS**

---

## 13. GOOGLE exact API contract regression

前回分離したclassifierも維持されている。

```text
ClassifyVulkanGoogleRefreshCycleResult()
```

と:

```text
ClassifyVulkanGooglePastTimingResult()
```

を別contractとして扱う。

refresh-cycle:

```text
VK_SUCCESS -> Continue
DEVICE_LOST -> DeviceLost
SURFACE_LOST -> SurfaceLost
other -> DisableOptional
```

past-timing:

```text
VK_SUCCESS / VK_INCOMPLETE -> Continue
OUT_OF_DATE -> SwapchainOutOfDate
DEVICE_LOST -> DeviceLost
SURFACE_LOST -> SurfaceLost
other -> DisableOptional
```

今回差分で共有classifierへ戻すregressionはない。

**判定: PASS**

---

## 14. mixed EXT + GOOGLE / FIFO_LATEST_READY

既存のpolicy-aware backend selectionは維持されている。

same-frame recreation時のみ:

```text
decisionCurrent = false
backend = None
```

へ安全に落とし、次frameでnew generation capabilityから再resolveする。

`JustInTimeFifoLatestReady`も今回の新fallback test matrixに含まれる。

判定:

```text
P2-1 mixed EXT + GOOGLE:
    remains CLOSED

P2-2 GOOGLE + FIFO_LATEST_READY:
    remains CLOSED
```

---

## 15. production flow照合

production orderingは概ね:

```text
Emulation thread:
    BeginLowLatencyFrame()
        -> PresentPacer.BeginFrame()
        -> current generationへdecision stamp

Draw/presenter:
    BeginFrame()
        -> SwapchainDirtyならRecreateSwapchain()
        -> OnSwapchainCreated()
        -> generation increment
        -> timing lifecycle reset

same frame:
    EndFrame()
        -> Present
        -> CaptureState(metadata)
```

となる。

したがって今回の:

```text
PreparePresent generation guard
CaptureState fallback helper
```

はpure testだけの修正ではなく、実際のsame-frame recreation production pathへ適用される。

**判定: PASS**

---

## 16. repository内実装結果MDとの照合

現在の:

```text
.codex/
melonPrimeDS_VulkanPresentTiming_335dc767_プッシュ後再監査_実装結果_2026-08-14.md
```

では以下を報告している。

```text
macOS Vulkan developer features ON:
    PASS

macOS Vulkan developer features OFF:
    PASS

Vulkan present timing model test:
    PASS

developer features OFF model test:
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

今回のsource監査では、この結果MDの主要主張と実装差分は整合していた。

ただしPASSのうちlocal execution結果については、今回GitHub connectorから再実行したものではなく、repository内実装結果MDに記録されたevidenceとして扱う。

---

## 17. GitHub-hosted CI

今回HEAD:

```text
ed859a1bcc5e58fc7b3b2d55a14f1f0feedb047e
```

について監査時点で:

```text
combined commit statuses:
    []

commit-associated workflow runs:
    []
```

だった。

したがって:

```text
GitHub-hosted CI PASS
```

とは判定しない。

repository内のmacOS PASS等はrepository-authored local verification evidenceとして扱う。

---

## 18. 継続P3

### API-level fake Vulkan dispatch integration

これは今回も未実装。

現在のcoverage:

```text
pure capability model
pure result classifier
pure generation helper
pure fallback helper
source contract audit
local build/test evidence
```

は強い。

ただしproduction pacerへfake dispatchを注入し:

```text
vkWaitForPresent2KHR
vkGetPastPresentationTimingEXT
vkGetSwapchainTimingPropertiesEXT
vkGetSwapchainTimeDomainPropertiesEXT
vkGetRefreshCycleDurationGOOGLE
vkGetPastPresentationTimingGOOGLE
```

から任意`VkResult`を返し、その後の:

```text
typed lifecycle return
retry pending
runtime disable
swapchain rebuild
renderer failure
generation state
capture attribution
```

まで直接検査するintegration harnessはない。

**判定: P3 hardening / OPEN**

---

## 19. platform / physical validation

現状:

```text
macOS:
    repository内結果ではPASS

Windows:
    NOT RUN

Linux:
    NOT RUN

physical Vulkan runtime:
    NOT RUN

validation layer:
    NOT RUN

resize/minimize/restore:
    NOT RUN

SUBOPTIMAL誘発recreation:
    NOT RUN

long-running present timing:
    NOT RUN
```

よって今回のPASSは:

```text
source/model/contract
+
repository-reported macOS build
```

の範囲。

all-platform / physical-runtimeまで完全CLOSEDとはしない。

---

## 20. Optional P4 defensive semantics observation

新helper:

```cpp
VulkanFrameDecisionFallbackReason(
    bool decisionCurrent,
    VulkanJitFallbackReason currentReason)
```

は`decisionCurrent == false`なら常に:

```text
FrameDecisionInvalidatedBySwapchainRecreation
```

を返す。

一方`VulkanFrameDecisionMatchesSwapchain()`は:

```text
generation mismatch
```

だけでなく:

```text
DecisionSwapchainGeneration == 0
```

でもfalseになる。

理論上は「まだdecisionがstampされていない」状態までrecreation reasonと表現する余地がある。

ただし今回確認した通常production flowでは、normal frameでは`BeginFrame()`がstampし、問題になるfalseはsame-frame recreationによるgeneration mismatchとして発生する。具体的なproduction mislabel pathは確認できなかった。

従ってこれは:

```text
P2ではない
P3でもない
P4 defensive semantics observation
```

に留める。

現修正を差し戻す理由にはしない。

---

## 21. 前回DoD照合

### fallback semantics

- [x] generation mismatch専用fallback reason追加
- [x] enum末尾へ追加
- [x] 既存numeric値維持
- [x] JITでTelemetryOnlyPolicyを記録しない
- [x] PresentWaitでもTelemetryOnlyPolicyを記録しない
- [x] same-frame recreationへ明示的reason
- [x] reset stateをNoneへ変更

### tests

- [x] `TestSwapchainRecreationFallbackReason`
- [x] `JustInTime`
- [x] `JustInTimeFifoLatestReady`
- [x] `PresentWait`
- [x] generation mismatch
- [x] `TelemetryOnlyPolicy`禁止
- [x] explicit lifecycle reason確認

### audit

- [x] enum ordering
- [x] runtime name
- [x] neutral reset
- [x] TelemetryOnly misuse禁止
- [x] `CaptureState` helper
- [x] test存在

### regression

- [x] swapchain generation guard
- [x] wait attribution guard
- [x] frame interval guard
- [x] Wait2 classifier
- [x] EXT classifiers
- [x] GOOGLE exact API classifiers
- [x] mixed backend model
- [x] FIFO_LATEST_READY model

---

## 22. 現在の残件一覧

```text
P2:
    新規なし

P3 source:
    新規なし

P3 hardening:
    API-level fake Vulkan dispatch integration
    OPEN

P4 optional:
    zero/unset generation時のfallback name genericity

Platform:
    Windows NOT RUN
    Linux NOT RUN

Runtime:
    physical validation NOT RUN
    validation layer NOT RUN
```

---

## 23. 残件を完全に閉じる場合の実装順

```text
1. Vulkan function dispatchをinjectable化
2. WaitForPresent2 fake result test
3. EXT past-timing fake result test
4. EXT timing-properties fake result test
5. EXT time-domain count/array fake result test
6. GOOGLE refresh-cycle fake result test
7. GOOGLE past-timing fake result test
8. same-frame recreation + lifecycle result combined test
9. Windows Vulkan build
10. Linux Vulkan build
11. validation-layer runtime
12. resize/minimize/restore/SUBOPTIMAL endurance
13. 最終再監査
```

---

## 24. 禁止事項

次のhardeningで今回CLOSEDした契約を戻さないこと。

```text
FrameDecisionInvalidatedBySwapchainRecreation
```

はenum numeric stabilityのため既存値の途中へ移動しない。

```text
ResetTimingLifecycle()
    FallbackReason = None
```

を`TelemetryOnlyPolicy`へ戻さない。

```text
CaptureState()
    VulkanFrameDecisionFallbackReason(...)
```

を外さない。

```text
PreparePresent()
    stale generation -> backend=None
```

を外さない。

```text
DecisionSwapchainGeneration
WaitAttemptSwapchainGeneration
```

を一つに統合しない。decision permissionとactual wait attemptは別概念。

GOOGLE refresh-cycle / past-timing classifierを再共有しない。

Wait2の:

```text
SUBOPTIMAL -> rebuild
SURFACE_LOST -> renderer failure
```

をoptional-disableへ戻さない。

---

## 25. 最終監査判定

```text
前回P3:
    TelemetryOnlyPolicy semantic misuse
    CLOSED

explicit recreation fallback:
    PASS

enum numeric stability:
    PASS

CaptureState production use:
    PASS

swapchain generation lifecycle:
    PASS

Wait2:
    PASS

EXT timing:
    PASS

GOOGLE classifiers:
    PASS

mixed EXT + GOOGLE:
    PASS

FIFO_LATEST_READY:
    PASS
```

今回新規:

```text
P2 source defect:
    NONE FOUND

P3 source defect:
    NONE FOUND
```

継続:

```text
P3 test-harness:
    API-level fake Vulkan dispatch
    OPEN

Windows:
    NOT RUN

Linux:
    NOT RUN

physical runtime / validation layer:
    NOT RUN
```

最終判定:

```text
PASS
WITH EXISTING P3 TEST-HARNESS / PLATFORM VALIDATION GAPS
```

前回のsource-level diagnostic残件は`CLOSED`としてよい。

---

## 26. GitHub参照

Branch:

```text
https://github.com/ag-advania/melonPrimeDS/tree/develop_remakeVulkan_ver3
```

HEAD:

```text
https://github.com/ag-advania/melonPrimeDS/tree/ed859a1bcc5e58fc7b3b2d55a14f1f0feedb047e
```

主要source:

```text
src/VulkanPresentPacer.cpp
src/VulkanPresentPacingPolicy.h
tools/testing/vulkan-present-timing-tests.cpp
tools/ci/audits/audit-low-latency-contract.py
```

---

## 27. 次回再監査の最優先点

API-level fake dispatchを実装した場合は、classifier単体ではなく:

```text
fake Vulkan call
    ↓
production pacer
    ↓
typed result
    ↓
state mutation
    ↓
presenter action
```

まで一続きで確認すること。

最低でも:

```text
SURFACE_LOST
DEVICE_LOST
OUT_OF_DATE
SUBOPTIMAL
INCOMPLETE
NOT_READY
TIMEOUT
```

をAPIごとの仕様に従って注入し:

```text
swapchain rebuild
renderer failure
retry pending
optional backend disable
timing metadata pause
capture attribution
```

まで検証する。

ここまで通れば、現在残る最大のstatic-vs-production test gapを`CLOSED`へできる。
