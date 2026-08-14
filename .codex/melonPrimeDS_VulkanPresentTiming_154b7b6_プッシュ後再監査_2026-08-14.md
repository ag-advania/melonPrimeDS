# melonPrimeDS Vulkan Present Timing
## `154b7b6` Push後 再監査・残件指示書

- 作成日: 2026-08-14
- 対象Repository: `ag-advania/melonPrimeDS`
- 対象Branch: `develop_remakeVulkan_ver3`
- 前回監査HEAD: `5de846c91e0f2334fdb58ea3bed545f885509de5`
- 今回監査HEAD: `154b7b6a573ff623167f13b4675dea9473a0ccd2`
- HEAD commit message: `Fix Vulkan present timing lifecycle result routing`
- 比較範囲: `5de846c91e0f2334fdb58ea3bed545f885509de5..154b7b6a573ff623167f13b4675dea9473a0ccd2`
- 差分: **2 commits ahead / 0 behind**
- 総合判定: **PASS WITH NEW P2 PRESENT_WAIT2 FINDINGS**

---

# 1. 結論

今回Pushされた修正について、前回指示した`VK_EXT_present_timing` / `VK_GOOGLE_display_timing`のfatal result routingは、**source上は正しく実装されている**。

特に以下は閉じた。

```text
前回 P2-1:
    mixed EXT telemetry + GOOGLE target selector
    → CLOSED

前回 P2-2:
    GOOGLE + FIFO_LATEST_READY
    → CLOSED

前回 NEW P2:
    vkGetPastPresentationTimingEXT fatal routing
    → CLOSED on source

前回 NEW P2:
    vkGetSwapchainTimingPropertiesEXT SURFACE_LOST routing
    → CLOSED on source

前回 NEW P2:
    vkGetSwapchainTimeDomainPropertiesEXT SURFACE_LOST routing
    → CLOSED on source

前回 NEW P2:
    GOOGLE eager refresh fatal result latching
    → CLOSED on source

前回 P3:
    time-domain VK_NOT_READY誤認
    → CLOSED on source
```

しかし再監査で、**同じpresent lifecycle内の`vkWaitForPresent2KHR()`に新しいP2残件を2件確認した**。

```text
NEW P2-5:
    vkWaitForPresent2KHR
    VK_ERROR_SURFACE_LOST_KHR
    → 現在はDisableWait() + Continueへ落ちる
    → OPEN

NEW P2-6:
    vkWaitForPresent2KHR
    VK_SUBOPTIMAL_KHR
    → Vulkan仕様上success code
    → 現在はDisableWait()へ落ちる
    → OPEN
```

したがって、今回のPushを

```text
present timing lifecycle result routing:
    FULLY CLOSED
```

とはまだ判定しない。

正確な判定は:

```text
PASS WITH NEW P2 PRESENT_WAIT2 FINDINGS
```

とする。

---

# 2. GitHub上のPush確認

今回のBranch HEADは:

```text
154b7b6a573ff623167f13b4675dea9473a0ccd2
Fix Vulkan present timing lifecycle result routing
```

前回監査HEAD:

```text
5de846c91e0f2334fdb58ea3bed545f885509de5
```

との比較:

```text
ahead_by: 2
behind_by: 0
```

変更対象:

```text
.codex/
    melonPrimeDS_VulkanPresentTiming_P2残件_致命的結果ルーティング修正指示書_2026-08-14.md
    melonPrimeDS_VulkanPresentTiming_P2残件_致命的結果ルーティング実装結果_2026-08-14.md

src/
    VulkanPresentPacer.cpp
    VulkanPresentPacer.h
    VulkanPresentPacingPolicy.h

tools/
    ci/audits/audit-low-latency-contract.py
    testing/vulkan-present-timing-tests.cpp
```

なお、旧監査MD:

```text
.codex/melonPrimeDS_Vulkan低遅延技術_353cd23c_コミット後再監査_2026-08-14.md
.codex/melonPrimeDS_Vulkan低遅延技術_develop_remakeVulkan_ver3_再監査結果_2026-08-14.md
```

は今回の比較範囲で削除されている。

これはcode correctness blockerではないが、後述のP4 documentation traceabilityとして記録する。

---

# 3. 前回指示書に対する実装監査

# 3.1 `ReportPastTiming()` typed result化

前回:

```cpp
void VulkanPresentPacer::ReportPastTiming()
```

だったものが、現在は:

```cpp
VulkanPacerBeginResult VulkanPresentPacer::ReportPastTiming()
```

へ変更されている。

`BeginFrame()`も:

```cpp
const VulkanPacerBeginResult extTiming = ReportPastTiming();
if (extTiming != VulkanPacerBeginResult::Continue)
    return extTiming;
```

としており、戻り値を捨てていない。

判定:

```text
PASS
```

---

# 3.2 `vkGetPastPresentationTimingEXT()` fatal routing

現在の処理は概ね:

```text
VK_SUCCESS
VK_INCOMPLETE
    → timing report処理継続

VK_ERROR_OUT_OF_DATE_KHR
    → SwapchainOutOfDate

VK_ERROR_DEVICE_LOST
    → DeviceLost

VK_ERROR_SURFACE_LOST_KHR
    → SurfaceLost

その他
    → optional EXT timing disable
    → TimingQueryFailed
    → Continue
```

となっている。

fatal lifecycle resultを`TimingQueryFailed`へ潰す旧問題は解消された。

判定:

```text
PASS
```

---

# 3.3 `RefreshTimingProperties()` typed result化

現在:

```cpp
VulkanPacerBeginResult VulkanPresentPacer::RefreshTimingProperties()
```

となっている。

`VK_NOT_READY`は:

```cpp
TimingPropertiesRetryPending = true;
return VulkanPacerBeginResult::Continue;
```

として、仕様どおりbootstrap/pendingとして扱われる。

それ以外のfailureについては`ClassifyPresentLifecycleResult()`を通し、`VK_ERROR_SURFACE_LOST_KHR`は`SurfaceLost`へ伝播する。

判定:

```text
PASS
```

---

# 3.4 `RefreshTimeDomains()` typed result化

現在:

```cpp
VulkanPacerBeginResult VulkanPresentPacer::RefreshTimeDomains()
```

となった。

count query:

```text
VK_SUCCESS
    → enumeration継続

VK_ERROR_SURFACE_LOST_KHR
    → SurfaceLost
```

array query:

```text
VK_SUCCESS
    → complete

VK_INCOMPLETE
    → bounded retry

VK_ERROR_SURFACE_LOST_KHR
    → SurfaceLost
```

となっている。

判定:

```text
PASS
```

---

# 3.5 time-domain `VK_NOT_READY`誤認の解消

前回コードでは:

```text
vkGetSwapchainTimingPropertiesEXT
vkGetSwapchainTimeDomainPropertiesEXT
```

の両方について`VK_NOT_READY`をbootstrap pendingとして扱っていた。

今回:

```text
TimingProperties:
    VK_NOT_READYをpendingとして維持

TimeDomain:
    VK_NOT_READY専用branchを削除
    VK_SUCCESS / VK_INCOMPLETE contractへ修正
```

となっている。

変数名も:

```text
TimeDomainsRetryPending
```

から:

```text
TimeDomainsEnumerationRetryPending
```

へ変更され、意味が`VK_INCOMPLETE` enumeration retryに限定された。

判定:

```text
PASS
```

---

# 3.6 required time domain contract

現在の`RefreshTimeDomains()`は:

```cpp
VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT
```

の存在を明示的に確認する。

そのうえでtarget用domainを:

```text
SWAPCHAIN_LOCAL
    ↓ fallback
PRESENT_STAGE_LOCAL
```

の順で選ぶ。

required domain自体が欠落している場合は:

```text
TargetSchedulingLifecycleFailed = true
```

としてbootstrap pendingへ誤分類しない。

判定:

```text
PASS
```

---

# 3.7 GOOGLE eager refresh fatal result

前回は:

```cpp
const VulkanPacerBeginResult google = RefreshGoogleTiming();
if (google != Continue)
    log only;
```

となり、`DeviceLost` / `SurfaceLost`をその場で失っていた。

現在は:

```cpp
if (google != VulkanPacerBeginResult::Continue)
    LatchPendingBeginResult(google);
```

となった。

`BeginFrame()`最上流で:

```cpp
if (PendingBeginResult != VulkanPacerBeginResult::Continue)
{
    const VulkanPacerBeginResult pending = PendingBeginResult;
    PendingBeginResult = VulkanPacerBeginResult::Continue;
    return pending;
}
```

として既存presenter routingへ送られる。

判定:

```text
PASS
```

---

# 3.8 first-fatal preservation

新規:

```cpp
constexpr VulkanPacerBeginResult VulkanLatchBeginResult(
    VulkanPacerBeginResult current,
    VulkanPacerBeginResult observed) noexcept
{
    return current == VulkanPacerBeginResult::Continue ? observed : current;
}
```

により、新swapchain lifecycle中に複数queryが失敗しても、最初に観測したfatal classを後続のoptional queryが上書きしない。

判定:

```text
PASS
```

---

# 3.9 presenterまでのend-to-end routing

`MelonPrimeVulkanPresenter.cpp`では:

```cpp
const VulkanPacerBeginResult pacerResult = PresentPacer.BeginFrame(...);
const VulkanPacerBeginAction pacerAction =
    VulkanPacerActionFor(pacerResult);
```

を行い、

```text
SwapchainOutOfDate
    → SwapchainDirty = true

DeviceLost
    → FailRenderer

SurfaceLost
    → FailRenderer
```

へ実際に接続されている。

単にpacer内部でenumを返して終わっているわけではない。

判定:

```text
PASS
```

---

# 4. P2-1 regression audit

mixed構成:

```text
EXT telemetry available
EXT target path unavailable
GOOGLE target path available
```

について、現在も:

```text
SelectVulkanPresentTargetBackend()
SelectVulkanPresentBackendForPolicy()
VulkanShouldPollGoogleForFrame()
```

を使用したpolicy-aware selectionが維持されている。

`OnSwapchainCreated()`側も:

```cpp
SelectVulkanPresentBackendForPolicy(GetPolicy(), lifecycleCaps)
```

を使用している。

今回のfatal routing修正によってselector driftは再導入されていない。

判定:

```text
P2-1:
    CLOSED
```

---

# 5. P2-2 regression audit

`GOOGLE + FIFO_LATEST_READY`についても:

```text
GOOGLE target scheduling
    ↓
VulkanTargetCanUseFifoLatestReady()
    ↓
FIFO_LATEST_READY eligible
```

のmodelが維持されている。

GOOGLE pathに対して不必要に:

```text
PresentId2Surface
EXT time domain
EXT timing queue
EXT absolute/relative mode
```

を要求するregressionも確認していない。

判定:

```text
P2-2:
    CLOSED on source/model
```

physical runtimeは別項で扱う。

---

# 6. NEW P2-5
## `vkWaitForPresent2KHR()`の`VK_ERROR_SURFACE_LOST_KHR`がtyped routingされていない

ここが今回の最重要新規残件。

現在の`BeginFrame()`内のwait2処理は:

```cpp
const VkResult result = Device->Fns().WaitForPresent2KHR(...);

if (result == VK_SUCCESS)
    return Continue;

if (result == VK_TIMEOUT)
{
    ++WaitTimeouts;
    return Continue;
}

if (result == VK_ERROR_OUT_OF_DATE_KHR)
    return SwapchainOutOfDate;

if (result == VK_ERROR_DEVICE_LOST)
    return DeviceLost;

DisableWait(...);
return Continue;
```

となっている。

Khronosの`vkWaitForPresent2KHR`現行return codeには:

```text
VK_ERROR_SURFACE_LOST_KHR
```

が含まれる。

しかし現コードでは:

```text
VK_ERROR_SURFACE_LOST_KHR
    ↓
DisableWait()
    ↓
Continue
```

となる。

これは今回修正したEXT/GOOGLE queryと同型の問題である。

surface喪失は:

```text
optional present-wait機能だけが壊れた
```

わけではない。

既存の:

```text
VulkanPacerBeginResult::SurfaceLost
```

へ送る必要がある。

## 必須修正

最低限:

```cpp
if (result == VK_ERROR_SURFACE_LOST_KHR)
{
    TargetSchedulingActive.store(false, std::memory_order_release);
    LogPresentLifecycleRoute(
        "WaitForPresent2KHR", result,
        VulkanPacerBeginResult::SurfaceLost);
    return VulkanPacerBeginResult::SurfaceLost;
}
```

ただし後述の共通classifierを使う方を推奨する。

判定:

```text
P2
OPEN
```

---

# 7. NEW P2-6
## `vkWaitForPresent2KHR()`の`VK_SUBOPTIMAL_KHR`をfailure扱いしている

`vkWaitForPresent2KHR()`のsuccess codeには:

```text
VK_SUCCESS
VK_TIMEOUT
VK_SUBOPTIMAL_KHR
```

が含まれる。

現在のコードは:

```text
VK_SUCCESS
    → Continue

VK_TIMEOUT
    → Continue

VK_SUBOPTIMAL_KHR
    → 明示branchなし
    → DisableWait()
    → Continue
```

となる。

つまり、swapchainがsuboptimalになっただけで:

```text
generic present wait runtime disabled
```

へ落ちる。

これは誤りである。

`VK_SUBOPTIMAL_KHR`は:

```text
WaitForPresent2KHR自体のoptional-feature failure
```

ではなく、swapchain/surface状態を表す成功結果である。

少なくとも:

```text
DisableWait()してはならない
```

。

## 推奨設計

より正確には`VulkanPacerBeginResult`へ:

```cpp
SwapchainSuboptimal
```

を追加する。

例:

```cpp
enum class VulkanPacerBeginResult : int
{
    Continue = 0,
    SwapchainOutOfDate,
    SwapchainSuboptimal,
    DeviceLost,
    SurfaceLost,
};
```

action:

```cpp
case VulkanPacerBeginResult::SwapchainSuboptimal:
    return {true, false};
```

これにより:

```text
VK_SUBOPTIMAL_KHR
    → SwapchainSuboptimal
    → RebuildSwapchain
```

とできる。

既存enum追加を避ける最小変更なら:

```text
VK_SUBOPTIMAL_KHR
    → Continue
    → DisableWaitしない
```

でも現在より正しい。

ただし、その場合wait2が既に観測したsuboptimalをswapchain recreationへ直接反映できない。

このため本指示書では:

```text
SwapchainSuboptimal typed result追加
```

を第一候補とする。

判定:

```text
P2
OPEN
```

---

# 8. P3
## `VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT`

Khronosの`vkWaitForPresent2KHR`は:

```text
VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT
```

も返し得る。

現branchには専用routingがないため、現在は:

```text
DisableWait()
Continue
```

へ落ちる。

ただし現在のmelonPrimeDS source監査では:

```text
VK_EXT_full_screen_exclusive
VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT
```

を実際に利用している形跡を確認していない。

したがって、現構成で直ちに再現するP2 blockerとはせず:

```text
P3 future-proofing
```

とする。

将来`VK_EXT_full_screen_exclusive`を有効化する場合は、単純なoptional wait disableではなく、fullscreen exclusive reacquire / swapchain lifecycleへ明示routingすること。

---

# 9. `vkWaitForPresent2KHR()` result routingを共通化する

今回新たにwait2で同型問題が見つかったため、APIごとのif-chainを増築し続けるより、wait2専用のpure action classifierを置く方が安全。

例:

```cpp
enum class VulkanPresentWaitResultAction : int
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

pure helper:

```cpp
constexpr VulkanPresentWaitResultAction ClassifyVulkanPresentWaitResult(
    VkResult result) noexcept
{
    switch (result)
    {
    case VK_SUCCESS:
        return VulkanPresentWaitResultAction::Continue;
    case VK_TIMEOUT:
        return VulkanPresentWaitResultAction::Timeout;
    case VK_SUBOPTIMAL_KHR:
        return VulkanPresentWaitResultAction::SwapchainSuboptimal;
    case VK_ERROR_OUT_OF_DATE_KHR:
        return VulkanPresentWaitResultAction::SwapchainOutOfDate;
    case VK_ERROR_DEVICE_LOST:
        return VulkanPresentWaitResultAction::DeviceLost;
    case VK_ERROR_SURFACE_LOST_KHR:
        return VulkanPresentWaitResultAction::SurfaceLost;
    default:
        return VulkanPresentWaitResultAction::DisableWait;
    }
}
```

そのうえで`BeginFrame()`側はactionを実行する。

重要なのは:

```text
VK_SUBOPTIMAL_KHR
VK_ERROR_SURFACE_LOST_KHR
```

をgeneric `DisableWait` fallbackから確実に除外すること。

---

# 10. generic lifecycle classifierの再利用

既存:

```cpp
VulkanPresentPacer::ClassifyPresentLifecycleResult()
```

は:

```text
OUT_OF_DATE
DEVICE_LOST
SURFACE_LOST
```

を正しく分類している。

wait2では:

```cpp
if (result == VK_SUCCESS) ...
if (result == VK_TIMEOUT) ...
if (result == VK_SUBOPTIMAL_KHR) ...

const VulkanPacerBeginResult lifecycle =
    ClassifyPresentLifecycleResult(result);

if (lifecycle != VulkanPacerBeginResult::Continue)
    return lifecycle;

DisableWait(...);
```

の構造でもよい。

ただし`VK_SUBOPTIMAL_KHR`はgeneric lifecycle classifierでは`Continue`になるため、必ずその前にAPI固有のsuccess/statusとして処理すること。

これは前回指示書の:

```text
各APIが実際に返すsuccess/failure contractをcaller側で保持する
```

という原則とも一致する。

---

# 11. NEW test gap
## 現在の「fault injection」はgeneric classifier testであり、API pathそのものではない

今回追加された:

```cpp
TestPresentTimingLifecycleResultClassification()
```

は有効である。

確認している内容:

```text
VK_SUCCESS
VK_INCOMPLETE
VK_NOT_READY
VK_ERROR_OUT_OF_DATE_KHR
VK_ERROR_DEVICE_LOST
VK_ERROR_SURFACE_LOST_KHR
VK_ERROR_UNKNOWN
first-fatal latch
```

しかし、このtestが直接呼んでいるのは:

```text
ClassifyPresentLifecycleResult()
VulkanLatchBeginResult()
```

であり、

```text
ReportPastTiming()
RefreshTimingProperties()
RefreshTimeDomains()
RefreshGoogleTiming()
ReportGooglePastTiming()
```

へmock Vulkan dispatchを差し込んで実際に状態遷移を検証するfault-injection testではない。

したがって、前回DoDの:

```text
EXT fault-injection tests PASS
```

を厳密に満たしたとはまだ判定しない。

現在の状態:

```text
generic lifecycle classifier pure test:
    PASS

source contract audit:
    PASS

API-level injected query path:
    NOT IMPLEMENTED / NOT VERIFIED
```

---

# 12. 推奨するAPI-specific pure tests

実Vulkan deviceなしでも、generic classifierだけではなくAPI contractをpure helper化すればかなり強くできる。

例:

```text
ClassifyPastTimingResult()
ClassifyTimingPropertiesResult()
ClassifyTimeDomainResult()
ClassifyPresentWait2Result()
```

これにより、APIごとに異なるsuccess setをtestできる。

## `GetPastPresentationTimingEXT`

```text
SUCCESS
    → Continue

INCOMPLETE
    → Continue

OUT_OF_DATE
    → SwapchainOutOfDate

DEVICE_LOST
    → DeviceLost

SURFACE_LOST
    → SurfaceLost

UNKNOWN
    → DisableOptionalTiming
```

## `GetSwapchainTimingPropertiesEXT`

```text
SUCCESS
    → Updated

NOT_READY
    → RetryAfterPresent

SURFACE_LOST
    → SurfaceLost

UNKNOWN
    → DisableTargetLifecycle
```

## `GetSwapchainTimeDomainPropertiesEXT`

```text
SUCCESS
    → Enumerate

INCOMPLETE
    → RetryEnumeration

SURFACE_LOST
    → SurfaceLost

NOT_READY
    → generic failure
    → spec-expected pendingにはしない
```

## `WaitForPresent2KHR`

```text
SUCCESS
    → Continue

TIMEOUT
    → Continue + timeout counter

SUBOPTIMAL
    → RebuildSwapchain
    → DisableWaitしない

OUT_OF_DATE
    → RebuildSwapchain

DEVICE_LOST
    → FailRenderer

SURFACE_LOST
    → FailRenderer

UNKNOWN
    → Disable optional wait
```

これなら「generic classifierが正しい」だけでなく:

```text
API-specific success/failure contract
```

そのものを実行可能な形で固定できる。

---

# 13. contract audit追加要件

`audit-low-latency-contract.py`には既に今回のEXT/GOOGLE typed routing checksが追加されている。

次はwait2も固定する。

最低限:

```text
1.
WaitForPresent2KHR blockにVK_SUBOPTIMAL_KHR処理がある

2.
VK_SUBOPTIMAL_KHRがDisableWait()へfallthroughしない

3.
WaitForPresent2KHR blockにVK_ERROR_SURFACE_LOST_KHR typed routingがある

4.
SurfaceLostがDisableWait()へfallthroughしない

5.
VK_TIMEOUTはWaitTimeouts increment後Continue

6.
VK_ERROR_DEVICE_LOSTはDeviceLost

7.
VK_ERROR_OUT_OF_DATE_KHRはswapchain rebuild action

8.
VulkanPacerActionFor()でSuboptimalを追加する場合、
RebuildSwapchain=true / FailRenderer=false

9.
TestPresentWait2ResultClassification()が存在する
```

---

# 14. 現在のtest coverage再評価

current sourceには以下が残っている。

```text
TestTimingBackendSelection
TestFifoLatestReadyBackendCompatibility
TestPolicyAwareGooglePolling
TestGoogleTimingTransactions
TestBeginResultRouting
TestPresentTimingLifecycleResultClassification
```

従って:

```text
P2-1 mixed EXT + GOOGLE:
    pure model coverageあり

P2-2 GOOGLE + FIFO_LATEST_READY:
    pure model coverageあり

GOOGLE query action:
    pure model coverageあり

typed presenter action:
    pure model coverageあり

generic VkResult lifecycle classification:
    pure model coverageあり
```

一方:

```text
WaitForPresent2KHR SUBOPTIMAL:
    coverage不足

WaitForPresent2KHR SURFACE_LOST:
    coverage不足

API-level injected EXT/GOOGLE query:
    coverage不足
```

である。

---

# 15. build / runtime evidence再評価

今回Pushには実装結果MDが含まれており、実装者側の記録では:

```text
macOS Vulkan build / developer features ON:
    PASS

macOS Vulkan build / developer features OFF:
    PASS

Vulkan present timing model test:
    PASS

low-latency contract audit:
    PASS

aggregate Vulkan latency tests:
    PASS

Software parity audit:
    PASS

git diff --check:
    PASS

Linux Vulkan build:
    NOT RUN

Windows Vulkan build:
    NOT RUN

physical runtime / validation-layer endurance:
    NOT RUN
```

となっている。

この記録内容とsource変更の整合は確認できた。

ただしGitHub上の今回HEAD:

```text
154b7b6a573ff623167f13b4675dea9473a0ccd2
```

には、監査時点でGitHub-hosted status check / workflow runが付いていない。

従って証拠レベルを分ける。

```text
source audit:
    independently PASS except new Wait2 findings

repository-authored local macOS build evidence:
    PASS reported

GitHub-hosted CI:
    NO STATUS / NO WORKFLOW RUN FOUND

Windows build:
    NOT RUN

Linux build:
    NOT RUN

physical runtime:
    NOT RUN

validation-layer endurance:
    NOT RUN
```

---

# 16. 前回DoDとの照合

## source / lifecycle

- [x] `ReportPastTiming()` typed result
- [x] `GetPastPresentationTimingEXT DeviceLost` typed
- [x] `GetPastPresentationTimingEXT OutOfDate` typed
- [x] `GetPastPresentationTimingEXT SurfaceLost` typed
- [x] optional timing failureのみtiming disable
- [x] `RefreshTimingProperties SurfaceLost` typed
- [x] `RefreshTimeDomains count SurfaceLost` typed
- [x] `RefreshTimeDomains array SurfaceLost` typed
- [x] GOOGLE eager DeviceLostを保持
- [x] GOOGLE eager SurfaceLostを保持
- [x] presenterの`VulkanPacerActionFor()`へ到達

## P3 spec alignment

- [x] TimingProperties `VK_NOT_READY`維持
- [x] TimeDomain `VK_NOT_READY` pending branch削除
- [x] TimeDomain `VK_SUCCESS / VK_INCOMPLETE`
- [x] required `PRESENT_STAGE_LOCAL`
- [x] 「Both queries may return VK_NOT_READY」コメント修正

## regression

- [x] mixed EXT+GOOGLE selector source/model維持
- [x] GOOGLE+FIFO_LATEST_READY source/model維持
- [x] generic lifecycle classifier test追加
- [x] contract audit追加
- [ ] API-level EXT fault injection
- [ ] Wait2 SUBOPTIMAL test
- [ ] Wait2 SURFACE_LOST test
- [ ] Windows Vulkan build
- [ ] Linux Vulkan build
- [x] macOS Vulkan build: repository実装結果ではPASS
- [ ] physical runtime
- [ ] validation-layer endurance

従って前回DoDは:

```text
source implementation:
    MOSTLY CLOSED

all-platform / runtime DoD:
    OPEN
```

である。

---

# 17. NEW P2修正後のDefinition of Done

## `vkWaitForPresent2KHR`

- [ ] `VK_SUCCESS`でContinue
- [ ] `VK_TIMEOUT`でtimeout count + Continue
- [ ] `VK_SUBOPTIMAL_KHR`で`DisableWait()`しない
- [ ] `VK_SUBOPTIMAL_KHR`をswapchain rebuildへ明示routing
- [ ] `VK_ERROR_OUT_OF_DATE_KHR`でswapchain rebuild
- [ ] `VK_ERROR_DEVICE_LOST`でFailRenderer
- [ ] `VK_ERROR_SURFACE_LOST_KHR`でFailRenderer
- [ ] unknown optional wait failureだけ`DisableWait()`
- [ ] lifecycle result logへquery/result/route/actionを記録

## tests

- [ ] `TestPresentWait2ResultClassification()`追加
- [ ] SUCCESS
- [ ] TIMEOUT
- [ ] SUBOPTIMAL
- [ ] OUT_OF_DATE
- [ ] DEVICE_LOST
- [ ] SURFACE_LOST
- [ ] UNKNOWN
- [ ] contract auditがSUBOPTIMAL fallthroughを検出可能
- [ ] contract auditがSURFACE_LOST fallthroughを検出可能
- [ ] 既存P2-1 test PASS
- [ ] 既存P2-2 test PASS
- [ ] 既存lifecycle classification test PASS

## build / runtime

- [ ] macOS Vulkan build PASS
- [ ] Windows Vulkan build PASS
- [ ] Linux Vulkan build PASS
- [ ] `git diff --check` PASS
- [ ] low-latency contract audit PASS
- [ ] Vulkan present timing model tests PASS
- [ ] validation layerで新規errorなし
- [ ] swapchain resize / minimize / restoreでwait2 routing regressionなし

---

# 18. 推奨修正順

```text
Step 1
    WaitForPresent2KHR result-set用pure classifierを追加

Step 2
    VK_SUBOPTIMAL_KHRを明示処理
    DisableWaitへ落とさない

Step 3
    VK_ERROR_SURFACE_LOST_KHRをSurfaceLostへtyped routing

Step 4
    lifecycle logをwait2にも統一適用

Step 5
    Wait2 result classification pure tests追加

Step 6
    contract audit追加

Step 7
    existing present timing tests全実行

Step 8
    macOS / Windows / Linux build gate

Step 9
    validation-layer event matrix

Step 10
    再監査
```

---

# 19. P4 documentation traceability

今回比較では旧監査MD2本が削除され、新しい指示書 / 実装結果MDへ置き換えられている。

code behaviorには影響しない。

ただし監査履歴としては:

```text
old finding
    ↓
fix
    ↓
re-audit
```

の履歴がGit上ではcommit historyを掘らないと読めなくなる。

今後は削除より:

```text
status: superseded
superseded-by: <new document>
```

を先頭へ追記するか:

```text
.codex/archive/
```

へ移動する方が追跡しやすい。

判定:

```text
P4
non-blocking
```

---

# 20. `ApplyTimingQueueSize()`の`VK_NOT_READY`について

現行`ApplyTimingQueueSize()`は:

```text
result != VK_SUCCESS
    → false
```

としているため、`vkSetSwapchainPresentTimingQueueSizeEXT()`の`VK_NOT_READY`もgeneric failure扱いになる。

仕様上`VK_NOT_READY`は:

```text
requested queue size < outstanding result count
```

の場合に返るsuccess/status codeである。

ただし現在のcall patternは:

```text
初回allocation:
    outstanding = 0

recovery:
    queue sizeを増加方向にのみ変更
```

となっているため、現行invariant下では通常`VK_NOT_READY`へ入らない。

従って現時点ではP2 functional bugとはしない。

将来queue shrinkingを実装する場合に備えて:

```text
P4 hardening
```

としてAPI contract testを追加しておく余地はある。

---

# 21. 最終監査判定

今回Pushで前回指示した中心問題はかなり正確に修正されている。

特に:

```text
VkResult
    ↓
typed lifecycle result
    ↓
VulkanPacerActionFor()
    ↓
real presenter action
```

というchainがEXT/GOOGLE queryについて成立した点は確認できた。

しかし`vkWaitForPresent2KHR()`だけが同じpresent lifecycleの中で旧式の個別if-chainを残しており:

```text
SURFACE_LOST
    → DisableWait + Continue

SUBOPTIMAL
    → DisableWait + Continue
```

という不整合が残った。

したがってcanonical verdictは:

```text
P2-1 mixed EXT telemetry + GOOGLE target:
    CLOSED

P2-2 GOOGLE + FIFO_LATEST_READY:
    CLOSED on source/model

Previous EXT/GOOGLE fatal query routing:
    CLOSED on source

Previous time-domain VK_NOT_READY issue:
    CLOSED on source

NEW P2-5 WaitForPresent2KHR SURFACE_LOST:
    OPEN

NEW P2-6 WaitForPresent2KHR SUBOPTIMAL:
    OPEN

API-level fault injection:
    OPEN

Windows build:
    NOT RUN

Linux build:
    NOT RUN

physical runtime / validation endurance:
    NOT RUN

OVERALL:
    PASS WITH NEW P2 PRESENT_WAIT2 FINDINGS
```

---

# 22. 仕様参照

Khronos Vulkan Documentation:

- `vkGetPastPresentationTimingEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingEXT.html
- `vkGetSwapchainTimingPropertiesEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimingPropertiesEXT.html
- `vkGetSwapchainTimeDomainPropertiesEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimeDomainPropertiesEXT.html
- `vkWaitForPresent2KHR`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForPresent2KHR.html
- `vkSetSwapchainPresentTimingQueueSizeEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkSetSwapchainPresentTimingQueueSizeEXT.html
- `VK_EXT_present_timing`
  - https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html

---

# 23. 最終作業指示

次の修正ではEXT/GOOGLE側の完成したroutingを崩さず、対象を`vkWaitForPresent2KHR()`へ限定する。

必須修正は:

```text
1.
VK_ERROR_SURFACE_LOST_KHR
    → SurfaceLost
    → FailRenderer

2.
VK_SUBOPTIMAL_KHR
    → success/statusとして処理
    → DisableWaitへ落とさない
    → preferably typed swapchain rebuild

3.
Wait2 API固有result classifier test追加

4.
contract audit追加

5.
all-platform build / validation evidenceを閉じる
```

この5点が閉じた時点で、present timing / present waitを含むgeneric Vulkan WSI pacing lifecycle全体を再度CLOSED判定する。
