# melonPrimeDS Vulkan Present Timing P2残件
## 致命的VkResultライフサイクル・ルーティング修正指示書

- 作成日: 2026-08-14
- 対象Repository: `ag-advania/melonPrimeDS`
- 対象Branch: `develop_remakeVulkan_ver3`
- 監査時Branch HEAD: `5de846c91e0f2334fdb58ea3bed545f885509de5`
- 実装Source基準Commit: `266aa7e4132e945d6e398d4d84197373e7ea4b3f`
- 主対象:
  - `src/VulkanPresentPacer.cpp`
  - `src/VulkanPresentPacer.h`
  - `src/VulkanPresentPacingPolicy.h`
  - `tools/testing/vulkan-present-timing-tests.cpp`
  - Vulkan present timing関連contract audit / fault-injection test
- 判定: **PASS WITH NEW P2 LIFECYCLE FINDINGS**

---

# 1. この指示書の目的

前回監査で残っていた以下の問題は、現HEADではsource/model上ほぼ閉じている。

1. **P2-1**
   - `VK_EXT_present_timing` telemetryと`VK_GOOGLE_display_timing` target schedulingが混在する構成で、lifecycle側とper-frame resolver側のbackend選択がずれる問題
   - `SelectVulkanPresentBackendForPolicy()`によるpolicy-aware backend selectionへ統一されている

2. **P2-2**
   - `VK_GOOGLE_display_timing + VK_PRESENT_MODE_FIFO_LATEST_READY_KHR`
   - source / pure model / contract上の組合せは成立する状態になっている

一方、再監査によって**present timing queryの失敗結果を、既存のtyped lifecycle resultへ最後まで伝播できていない新しいP2残件**が確認できた。

今回の目的は、optional timing機能の単なるdisableと、swapchain再作成・surface喪失・device喪失を必要とする**致命的またはライフサイクル上のVkResultを絶対に混同しないこと**である。

---

# 2. 現在の状態

## 2.1 Branch HEAD

監査時点:

```text
develop_remakeVulkan_ver3
HEAD = 5de846c91e0f2334fdb58ea3bed545f885509de5
parent = 266aa7e4132e945d6e398d4d84197373e7ea4b3f
```

`266aa7e4`から`5de846c9`までの差分は監査用Markdownのみであり、今回確認するVulkan source本体は`266aa7e4`時点と同一である。

したがって、本指示書では`5de846c9`を監査HEADとしつつ、実装評価は`266aa7e4`のsourceへ対して行う。

---

# 3. 結論

現状を一文でまとめると次の通り。

```text
P2-1 mixed EXT telemetry + GOOGLE target:
    CLOSED

P2-2 GOOGLE + FIFO_LATEST_READY source/model:
    CLOSED

NEW P2:
    OPEN
    present timing query lifecycleにおけるfatal / rebuild result routingが不完全

P3:
    OPEN
    vkGetSwapchainTimeDomainPropertiesEXTのVK_NOT_READY扱いが現行仕様と不一致
```

今回の新P2は、target schedulingアルゴリズムそのものの誤りではない。

問題は、**正しいVkResultが返ってきても、それをpresenter側の既存failure routingへ届ける前にoptional timing failureへ潰してしまうこと**である。

---

# 4. 既に閉じている事項

## 4.1 P2-1 policy-aware backend selection

現HEADでは`OnSwapchainCreated()`が次の選択を使用している。

```cpp
const VulkanPacingCapabilities lifecycleCaps = BuildCapabilities();
const VulkanPresentTimingBackend lifecycleBackend =
    SelectVulkanPresentBackendForPolicy(GetPolicy(), lifecycleCaps);
```

これにより、

```text
EXT telemetry available
+
EXT target path unavailable
+
GOOGLE target path available
```

というmixed構成でも、swapchain creation lifecycleとper-frame decisionが別々のbackendを勝手に選ぶ旧問題は閉じている。

この修正は残すこと。

今回の修正で、再び

```text
PresentTimingSurface == true
    → 常にEXTを優先
```

のような単純判定へ戻してはならない。

---

## 4.2 P2-2 GOOGLE + FIFO_LATEST_READY

`ShouldUseFifoLatestReady()`はGOOGLE backendをEXTとは独立して評価している。

GOOGLEに対して不必要に以下を要求しない設計になっている。

```text
PresentId2
EXT time-domain query
EXT result queue
EXT absolute/relative feature
```

このsource/model上の修正も維持すること。

今回のライフサイクル修正は、このbackend capability matrixを変更する作業ではない。

---

# 5. NEW P2-1: `vkGetPastPresentationTimingEXT()`のfatal resultが潰れている

## 5.1 現行コード

現在の`ReportPastTiming()`は戻り値が`void`である。

概略:

```cpp
void VulkanPresentPacer::ReportPastTiming()
{
    ...

    const VkResult result = Device->Fns().GetPastPresentationTimingEXT(
        Device->GetHandle(), &info, &properties);

    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        TimingMetadataEnabled = false;
        TimingResultsQueryEnabled = false;
        TimingQueueRecoveryPending = false;
        TargetSchedulingActive.store(false, std::memory_order_release);
        FallbackReason = VulkanJitFallbackReason::TimingQueryFailed;
        ...
        return;
    }

    ...
}
```

`BeginFrame()`側も単に:

```cpp
ReportPastTiming();
```

としている。

## 5.2 問題

現行Khronos仕様で`vkGetPastPresentationTimingEXT()`は以下を返し得る。

### Success

```text
VK_SUCCESS
VK_INCOMPLETE
```

### Failure

```text
VK_ERROR_DEVICE_LOST
VK_ERROR_OUT_OF_DATE_KHR
VK_ERROR_SURFACE_LOST_KHR
VK_ERROR_UNKNOWN
VK_ERROR_VALIDATION_FAILED
```

ところが現コードでは、このうち以下3種類まで全部:

```text
VK_ERROR_DEVICE_LOST
VK_ERROR_OUT_OF_DATE_KHR
VK_ERROR_SURFACE_LOST_KHR
```

を:

```text
TimingQueryFailed
→ optional timing disable
→ rendererは継続
```

へ潰している。

これは誤りである。

既に`VulkanPacerBeginResult`には:

```cpp
enum class VulkanPacerBeginResult : int
{
    Continue = 0,
    SwapchainOutOfDate,
    DeviceLost,
    SurfaceLost,
};
```

が存在しているため、この3種類は既存routingへ接続すべきである。

## 5.3 必須mapping

```text
vkGetPastPresentationTimingEXT result
    VK_SUCCESS
        → Continue

    VK_INCOMPLETE
        → Continue
        → 取得できたreportを処理

    VK_ERROR_OUT_OF_DATE_KHR
        → VulkanPacerBeginResult::SwapchainOutOfDate

    VK_ERROR_DEVICE_LOST
        → VulkanPacerBeginResult::DeviceLost

    VK_ERROR_SURFACE_LOST_KHR
        → VulkanPacerBeginResult::SurfaceLost

    その他のoptional timing failure
        → timing metadata / result queryをdisable
        → VulkanJitFallbackReason::TimingQueryFailed
        → Continue
```

## 5.4 修正方針

最低限、`ReportPastTiming()`をtyped result化すること。

例:

```cpp
[[nodiscard]] VulkanPacerBeginResult ReportPastTiming();
```

`BeginFrame()`では必ず結果を確認する。

```cpp
const VulkanPacerBeginResult extTiming = ReportPastTiming();
if (extTiming != VulkanPacerBeginResult::Continue)
    return extTiming;
```

**禁止:**

```cpp
if (!ReportPastTiming())
{
    DisableTiming...
}
```

のように再びboolへ潰すこと。

今回の問題は、まさにfailure classの情報損失である。

---

# 6. NEW P2-2: `vkGetSwapchainTimingPropertiesEXT()`の`SURFACE_LOST`がgeneric Failedへ潰れている

## 6.1 現行コード

`RefreshTimingProperties()`は:

```cpp
VulkanTimingRefreshResult VulkanPresentPacer::RefreshTimingProperties()
```

であり、

```text
Updated
NotReady
Unavailable
Failed
```

だけを返す。

現在:

```cpp
if (result == VK_NOT_READY)
{
    TimingPropertiesRetryPending = true;
    return VulkanTimingRefreshResult::NotReady;
}

if (result != VK_SUCCESS)
{
    TimingPropertiesRetryPending = false;
    TimingPropertiesReady = false;
    TargetSchedulingLifecycleFailed = true;
    ...
    return VulkanTimingRefreshResult::Failed;
}
```

となっている。

## 6.2 Khronos仕様

`vkGetSwapchainTimingPropertiesEXT()`の現行return codeは:

### Success

```text
VK_SUCCESS
VK_NOT_READY
```

### Failure

```text
VK_ERROR_OUT_OF_DEVICE_MEMORY
VK_ERROR_OUT_OF_HOST_MEMORY
VK_ERROR_SURFACE_LOST_KHR
VK_ERROR_UNKNOWN
VK_ERROR_VALIDATION_FAILED
```

したがって、**`VK_NOT_READY`をpendingとして扱う現在の実装は正しい**。

しかし:

```text
VK_ERROR_SURFACE_LOST_KHR
```

まで単なる:

```text
VulkanTimingRefreshResult::Failed
→ TargetSchedulingLifecycleFailed
→ host pacing fallback
```

へ落としてはいけない。

surfaceそのものが失われているため、既存の:

```text
VulkanPacerBeginResult::SurfaceLost
```

へ届ける必要がある。

## 6.3 注意

このAPIの現行仕様へ存在しないresultを想像で追加routingしないこと。

少なくとも現行refpageでは:

```text
VK_ERROR_DEVICE_LOST
VK_ERROR_OUT_OF_DATE_KHR
```

は`vkGetSwapchainTimingPropertiesEXT()`の列挙failure codeではない。

各APIのreturn-code contractを個別に守ること。

---

# 7. NEW P2-3: `vkGetSwapchainTimeDomainPropertiesEXT()`の`SURFACE_LOST`が消える

## 7.1 現行コード

`RefreshTimeDomains()`は`bool`を返す。

count query:

```cpp
VkResult result = Device->Fns().GetSwapchainTimeDomainPropertiesEXT(...);

if (result == VK_NOT_READY)
{
    TimeDomainsRetryPending = true;
    return false;
}

if (result != VK_SUCCESS || properties.timeDomainCount == 0)
{
    ...
    return false;
}
```

array query:

```cpp
result = Device->Fns().GetSwapchainTimeDomainPropertiesEXT(...);

if (result == VK_SUCCESS)
{
    ...
    break;
}

if (result != VK_INCOMPLETE)
{
    ...
    return false;
}
```

どちらのqueryでも`VK_ERROR_SURFACE_LOST_KHR`は単なる`false`へ潰れる。

## 7.2 Khronos仕様

現行`vkGetSwapchainTimeDomainPropertiesEXT()`のreturn codeは:

### Success

```text
VK_SUCCESS
VK_INCOMPLETE
```

### Failure

```text
VK_ERROR_OUT_OF_DEVICE_MEMORY
VK_ERROR_OUT_OF_HOST_MEMORY
VK_ERROR_SURFACE_LOST_KHR
VK_ERROR_UNKNOWN
VK_ERROR_VALIDATION_FAILED
```

従って:

```text
VK_ERROR_SURFACE_LOST_KHR
    → VulkanPacerBeginResult::SurfaceLost
```

へroutingすること。

count queryとarray queryの**両方**で必要である。

---

# 8. P3: Time-domain queryで`VK_NOT_READY`を期待するコメントは削除・修正する

## 8.1 現行の不一致

`OnSwapchainCreated()`には:

```cpp
// Both queries are allowed to answer VK_NOT_READY here...
RefreshTimingProperties();
RefreshTimeDomains();
```

という意味のコメントがある。

さらに`RefreshTimeDomains()`にも:

```cpp
if (result == VK_NOT_READY)
{
    TimeDomainsRetryPending = true;
    return false;
}
```

がある。

これは`vkGetSwapchainTimingPropertiesEXT()`については正しいが、

```text
vkGetSwapchainTimeDomainPropertiesEXT()
```

については現行仕様と一致しない。

## 8.2 正しい整理

```text
vkGetSwapchainTimingPropertiesEXT
    VK_NOT_READY:
        valid / expected
        first present後にretry可能

vkGetSwapchainTimeDomainPropertiesEXT
    success:
        VK_SUCCESS / VK_INCOMPLETE

    VK_NOT_READY:
        現行仕様上のsuccess codeではない
        「spec-expected pending」として扱わない
```

従って以下を行う。

1. `OnSwapchainCreated()`の「Both queries」コメントを分離する
2. `RefreshTimeDomains()`の`VK_NOT_READY` special-caseを削除する
3. `TimeDomainsRetryPending`を`VK_NOT_READY`前提のstateとして使わない
4. time-domain list changeは`timeDomainsCounter`による再enumerationへ限定する
5. `VK_INCOMPLETE`は既存のbounded enumeration retryで処理する

## 8.3 required time domainのcontract

`VK_EXT_present_timing` proposalでは:

```text
VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT
```

はrequired supportである。

従ってtime-domain enumerationが成功したのに:

```text
timeDomainCount == 0
```

または必要なdomain contractが成立しない場合は、

```text
「まだreadyではない」
```

と誤魔化さず、

```text
extension lifecycle / driver contract failure
```

として明示ログを残すこと。

現在の実装がtarget用に:

```text
SWAPCHAIN_LOCAL
→ PRESENT_STAGE_LOCAL
```

の順で選ぶ設計自体は維持してよい。

ただし「present timing supportedなのにrequired domainが1つも広告されない」状態は正常bootstrap扱いしない。

---

# 9. NEW P2-4: GOOGLE eager refreshのtyped fatalが`OnSwapchainCreated()`で途切れる

## 9.1 現行コード

`RefreshGoogleTiming()`自身は正しくtyped resultを返している。

```cpp
if (result == VK_ERROR_DEVICE_LOST)
    return VulkanPacerBeginResult::DeviceLost;

if (result == VK_ERROR_SURFACE_LOST_KHR)
    return VulkanPacerBeginResult::SurfaceLost;
```

これは正しい。

しかし`OnSwapchainCreated()`では:

```cpp
const VulkanPacerBeginResult google = RefreshGoogleTiming();
if (google != VulkanPacerBeginResult::Continue)
{
    Platform::Log(... "initialization deferred ...");
}
```

としており、結果をlogだけで吸収している。

`OnSwapchainCreated()`自体が`void`なので、ここでtyped failure chainが切れている。

## 9.2 なぜ問題か

`VK_ERROR_DEVICE_LOST`や`VK_ERROR_SURFACE_LOST_KHR`は:

```text
GOOGLE backendだけ使えなくなった
```

というoptional feature failureではない。

既存のVulkan runtime failureへ伝えるべき状態である。

次frameの再queryで同じエラーが再現することへ依存してはいけない。

特にpolicy/backend selectionが次frameまでに変われば、GOOGLE pollingそのものが行われず、swapchain creation時に既に観測したfatal resultが失われる可能性がある。

## 9.3 推奨修正

最小侵襲なら、pacer内へpending typed lifecycle resultを持つ。

例:

```cpp
VulkanPacerBeginResult PendingBeginResult =
    VulkanPacerBeginResult::Continue;
```

swapchain creation時:

```cpp
const VulkanPacerBeginResult google = RefreshGoogleTiming();
if (google != VulkanPacerBeginResult::Continue)
    LatchPendingBeginResult(google);
```

`BeginFrame()`の最上流:

```cpp
if (PendingBeginResult != VulkanPacerBeginResult::Continue)
{
    const VulkanPacerBeginResult result = PendingBeginResult;
    PendingBeginResult = VulkanPacerBeginResult::Continue;
    return result;
}
```

これにより:

```text
OnSwapchainCreated
→ fatal query result
→ pending typed result
→ BeginFrame
→ VulkanPacerActionFor()
→ existing presenter/runtime failure path
```

が途切れなくなる。

別案として`OnSwapchainCreated()`そのものをtyped returnに変更し、swapchain creation callerへ即時返却してもよい。

ただし、どちらを採用する場合も**logだけで終了する実装は禁止**する。

---

# 10. typed routingを一箇所へ集約する

今回の修正では、同じ3種類のresult判定を各関数へコピーし続けないこと。

最低限、内部helperを用意する。

概念:

```cpp
VulkanPacerBeginResult ClassifyPresentLifecycleResult(VkResult result) noexcept
{
    switch (result)
    {
    case VK_ERROR_OUT_OF_DATE_KHR:
        return VulkanPacerBeginResult::SwapchainOutOfDate;
    case VK_ERROR_DEVICE_LOST:
        return VulkanPacerBeginResult::DeviceLost;
    case VK_ERROR_SURFACE_LOST_KHR:
        return VulkanPacerBeginResult::SurfaceLost;
    default:
        return VulkanPacerBeginResult::Continue;
    }
}
```

ただし重要なのは、**各Vulkan APIが実際に返し得るresult setをcaller側で保持すること**である。

例えば:

```text
vkGetPastPresentationTimingEXT:
    DeviceLost / OutOfDate / SurfaceLostをrouting

vkGetSwapchainTimingPropertiesEXT:
    SurfaceLostをrouting
    VK_NOT_READYは正常pending

vkGetSwapchainTimeDomainPropertiesEXT:
    SurfaceLostをrouting
    VK_INCOMPLETEはenumeration継続
    VK_NOT_READYを正常pendingにしない

vkGetRefreshCycleDurationGOOGLE:
    DeviceLost / SurfaceLostをrouting
```

「共通helperがあるから、全APIで全resultを期待する」という設計にはしない。

---

# 11. `VulkanPacerBeginResult` / `VulkanPacerActionFor()`は再利用する

既存コードには既に:

```text
SwapchainOutOfDate
DeviceLost
SurfaceLost
```

と、そのaction mappingが存在している。

今回、新しい第二のfailure enumをpresenterまで並走させる必要はない。

目標は:

```text
query固有state
    ↓
VulkanPacerBeginResult
    ↓
VulkanPacerActionFor()
    ↓
既存presenter routing
```

へ収束させること。

### routing contract

```text
SwapchainOutOfDate
    → RebuildSwapchain = true
    → FailRenderer = false

DeviceLost
    → RebuildSwapchain = false
    → FailRenderer = true

SurfaceLost
    → RebuildSwapchain = false
    → FailRenderer = true
```

`DeviceLost`をswapchain recreate loopへ入れてはならない。

`SurfaceLost`をoptional timing disableだけで継続してはならない。

---

# 12. `ReportPastTiming()`の副作用順序

fatal resultを受けた時、先にoptional timing stateだけを書き換えてから`Continue`するのではなく、typed failureを優先する。

推奨:

```cpp
const VkResult result = ...;

if (result == VK_ERROR_DEVICE_LOST)
    return VulkanPacerBeginResult::DeviceLost;

if (result == VK_ERROR_OUT_OF_DATE_KHR)
    return VulkanPacerBeginResult::SwapchainOutOfDate;

if (result == VK_ERROR_SURFACE_LOST_KHR)
    return VulkanPacerBeginResult::SurfaceLost;

if (result != VK_SUCCESS && result != VK_INCOMPLETE)
{
    DisableOptionalExtTiming(...);
    return VulkanPacerBeginResult::Continue;
}
```

こうすることで:

```text
fatal WSI/device state
```

と:

```text
optional timing telemetryだけのfailure
```

を意味的に分離できる。

---

# 13. swapchain lifecycle reset

pending typed result方式を採る場合、reset位置を明示する。

## 必須

新しいdevice/surface lifetime開始時:

```text
Initialize
Shutdown
```

ではpending resultをクリアする。

新しいswapchain generation作成時は、**旧swapchainの未処理resultを誤って新swapchainへ持ち越さない**。

ただし:

```text
OnSwapchainCreated()内で今回の新swapchainに対して発生したfatal result
```

は、その関数末尾のresetで消してはならない。

順序:

```text
1. old lifecycleをreset
2. new swapchainを設定
3. lifecycle query
4. fatal resultならlatch
5. 次のBeginFrameでconsume
```

とする。

---

# 14. log要件

failure classをログでも失わないこと。

推奨例:

```text
[Vulkan] present_timing query=GetPastPresentationTimingEXT
result=VK_ERROR_DEVICE_LOST
route=DeviceLost
action=FailRenderer
```

```text
[Vulkan] present_timing query=GetPastPresentationTimingEXT
result=VK_ERROR_OUT_OF_DATE_KHR
route=SwapchainOutOfDate
action=RebuildSwapchain
```

```text
[Vulkan] present_timing query=GetSwapchainTimeDomainPropertiesEXT
result=VK_ERROR_SURFACE_LOST_KHR
route=SurfaceLost
action=FailRenderer
```

```text
[Vulkan] present_timing query=GetSwapchainTimingPropertiesEXT
result=VK_NOT_READY
route=RetryAfterPresent
```

P3修正後は次のような誤ログ・誤コメントを残さない。

```text
time-domain VK_NOT_READY = spec expected
```

---

# 15. テスト必須項目

# 15.1 `vkGetPastPresentationTimingEXT`

fault injectionまたはmock dispatchで最低限:

```text
VK_SUCCESS
    → Continue

VK_INCOMPLETE
    → Continue
    → returned reportsを処理

VK_ERROR_OUT_OF_DATE_KHR
    → SwapchainOutOfDate
    → RebuildSwapchain

VK_ERROR_DEVICE_LOST
    → DeviceLost
    → FailRenderer

VK_ERROR_SURFACE_LOST_KHR
    → SurfaceLost
    → FailRenderer

VK_ERROR_UNKNOWN
    → optional timing disable
    → Continue
```

## 15.2 `vkGetSwapchainTimingPropertiesEXT`

```text
VK_SUCCESS
    → TimingPropertiesReady

VK_NOT_READY
    → TimingPropertiesRetryPending
    → fatal扱いしない

VK_ERROR_SURFACE_LOST_KHR
    → SurfaceLost
    → host pacing fallbackだけで済ませない
```

## 15.3 `vkGetSwapchainTimeDomainPropertiesEXT`

```text
count query VK_SUCCESS
    → enumeration継続

array query VK_SUCCESS
    → domains確定

array query VK_INCOMPLETE
    → bounded retry

count/array query VK_ERROR_SURFACE_LOST_KHR
    → SurfaceLost

source contract:
    VK_NOT_READYをspec-expected branchとして残さない
```

## 15.4 GOOGLE eager refresh

```text
OnSwapchainCreated
→ RefreshGoogleTiming
→ VK_ERROR_DEVICE_LOST
→ pending/returnでtyped resultを保持
→ next routing pointでDeviceLost
→ FailRenderer
```

```text
OnSwapchainCreated
→ RefreshGoogleTiming
→ VK_ERROR_SURFACE_LOST_KHR
→ pending/returnでtyped resultを保持
→ SurfaceLost
→ FailRenderer
```

重要:

```text
logだけ出してContinue
```

ではテストPASSにしない。

---

# 16. contract audit追加

pure model testだけでは、今回の「戻り値を呼び出し元が捨てる」問題を完全には防げない。

source contract auditも追加する。

最低限、以下を検査すること。

```text
1. ReportPastTiming()がvoidではない
2. BeginFrame()がReportPastTiming()のtyped resultを確認する
3. GetPastPresentationTimingEXTのDeviceLost / OutOfDate / SurfaceLostが
   TimingQueryFailedへ一括collapseされない
4. RefreshGoogleTiming()のOnSwapchainCreated結果がlogだけで捨てられない
5. RefreshTimeDomains()に「VK_NOT_READYはspec expected」というcontractが残らない
6. GetSwapchainTimingPropertiesEXTのSurfaceLostがgeneric Failedだけに落ちない
7. GetSwapchainTimeDomainPropertiesEXTのSurfaceLostがbool falseだけに落ちない
8. VulkanPacerActionFor()の既存typed routingを維持する
```

---

# 17. 変更対象

主変更候補:

```text
src/VulkanPresentPacer.cpp
src/VulkanPresentPacer.h
src/VulkanPresentPacingPolicy.h
tools/testing/vulkan-present-timing-tests.cpp
関連contract audit
```

`VulkanPresentPacingPolicy.h`は、既存enum/actionで足りるなら変更を最小化すること。

新enumを増やすより、既存:

```text
VulkanPacerBeginResult
VulkanPacerActionFor
```

へ収束させることを優先する。

---

# 18. 変更しないもの

今回のP2 lifecycle fixを理由に、以下を変更しない。

```text
Software renderer
OpenGL renderer
OpenGL Compute renderer
DX12 renderer
Metal renderer
ROM patch
MPH game logic
Custom HUD
input
audio
general NDS timing
```

Vulkan present pacing内で閉じること。

共有コードへ変更が必要になった場合は、MelonPrime専用変更を無条件で漏らさず、既存の:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
```

境界を維持する。

---

# 19. 回帰禁止事項

今回の修正で以下を壊さない。

```text
P2-1:
    mixed EXT telemetry + GOOGLE target selection

P2-2:
    GOOGLE + FIFO_LATEST_READY eligibility

GOOGLE:
    GetPastPresentationTimingGOOGLEの
    DeviceLost / OutOfDate / SurfaceLost typed routing

EXT:
    TimingProperties VK_NOT_READY retry

EXT:
    VK_INCOMPLETE time-domain enumeration retry

EXT:
    timing queue pressure recovery

PresentWait2:
    timeoutはnon-fatal

PresentWait2:
    DeviceLostはswapchain rebuildではなくrenderer failure

Vendor latency:
    NVIDIA Reflexがauthorityを持つ時にgeneric target schedulingを競合させない

Vendor latency:
    AMD Anti-Lag 2がauthorityを持つ時にgeneric target schedulingを競合させない
```

---

# 20. 推奨実装順

```text
Step 1
    present timing VkResultのlifecycle classification helperを追加

Step 2
    ReportPastTiming()をtyped result化

Step 3
    BeginFrame()でEXT report resultを伝播

Step 4
    RefreshTimingProperties()のSurfaceLostをtyped routingへ接続

Step 5
    RefreshTimeDomains()のSurfaceLostをtyped routingへ接続

Step 6
    RefreshTimeDomains()からVK_NOT_READY spec-expected扱いを削除

Step 7
    OnSwapchainCreated()のGoogle eager fatalをpending/returnで保持

Step 8
    source contract audit追加

Step 9
    fault-injection / pure tests追加

Step 10
    Windows / Linux / macOS Vulkan buildを確認

Step 11
    validation layer有効buildで通常present pathの回帰を確認
```

---

# 21. Definition of Done

## P2 lifecycle routing

- [ ] `ReportPastTiming()`がtyped resultを返す
- [ ] `VK_ERROR_DEVICE_LOST`が`DeviceLost`へ届く
- [ ] `VK_ERROR_OUT_OF_DATE_KHR`が`SwapchainOutOfDate`へ届く
- [ ] `VK_ERROR_SURFACE_LOST_KHR`が`SurfaceLost`へ届く
- [ ] 上記3種を`TimingQueryFailed`へcollapseしない
- [ ] optional timing-only failureだけがtiming disableへ落ちる
- [ ] `RefreshTimingProperties()`のSurfaceLostをtyped routingする
- [ ] `RefreshTimeDomains()`のcount query SurfaceLostをtyped routingする
- [ ] `RefreshTimeDomains()`のarray query SurfaceLostをtyped routingする
- [ ] GOOGLE eager refreshのDeviceLostをlogだけで捨てない
- [ ] GOOGLE eager refreshのSurfaceLostをlogだけで捨てない
- [ ] `VulkanPacerActionFor()`へ最終的に接続される

## P3 spec alignment

- [ ] Timing propertiesの`VK_NOT_READY` handlingは維持
- [ ] Time-domain queryの`VK_NOT_READY` spec-expected handlingを削除
- [ ] `VK_SUCCESS / VK_INCOMPLETE`をtime-domain success contractとする
- [ ] required `VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT`のcontractを監査
- [ ] 誤った「Both queries may return VK_NOT_READY」コメントを修正

## Regression

- [ ] P2-1 mixed EXT + GOOGLE selector test PASS
- [ ] P2-2 GOOGLE + FIFO_LATEST_READY model test PASS
- [ ] GOOGLE feedback typed error tests PASS
- [ ] EXT fault-injection tests PASS
- [ ] contract audit PASS
- [ ] `git diff --check` PASS
- [ ] Windows Vulkan build PASS
- [ ] Linux Vulkan build PASS
- [ ] macOS Vulkan build PASS、対象環境でVulkan buildを持つ場合
- [ ] validation layerで新規errorなし

---

# 22. 完了判定

この修正後の期待状態:

```text
P2-1 mixed EXT telemetry + GOOGLE target:
    CLOSED

P2-2 GOOGLE + FIFO_LATEST_READY:
    CLOSED

P2 present timing fatal-result lifecycle routing:
    CLOSED

P3 time-domain result-code contract:
    CLOSED
```

最終的に次の原則が成立していること。

```text
optional timing機能が失敗しただけならoptional timingを落として継続する。

swapchainがout-of-dateならswapchainを再作成する。

deviceがlostならrenderer failureへ送る。

surfaceがlostならsurface/runtime failureへ送る。

この4種類を同じboolやTimingQueryFailedへ潰さない。
```

---

# 23. 仕様根拠

Khronos Vulkan Documentationの現行refpageを基準とする。

- `vkGetPastPresentationTimingEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingEXT.html
- `vkGetSwapchainTimingPropertiesEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimingPropertiesEXT.html
- `vkGetSwapchainTimeDomainPropertiesEXT`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimeDomainPropertiesEXT.html
- `vkGetRefreshCycleDurationGOOGLE`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetRefreshCycleDurationGOOGLE.html
- `VK_EXT_present_timing` proposal
  - https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html

---

# 24. 最終指示

今回の作業では、低遅延アルゴリズムを再設計しないこと。

直す対象は明確に:

```text
VkResult
→ query固有classification
→ VulkanPacerBeginResult
→ VulkanPacerActionFor
→ presenter/runtime action
```

の**error-routing chain**である。

前回P2-1/P2-2で完成したbackend selector・capability modelは維持し、その上で、EXT/GOOGLEどちらのtiming backendでもlifecycle error classが失われない状態へ仕上げること。
