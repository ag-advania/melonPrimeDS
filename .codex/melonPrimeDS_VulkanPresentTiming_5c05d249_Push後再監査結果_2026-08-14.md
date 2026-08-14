# melonPrimeDS Vulkan Present Timing

## 5c05d249 Push後 再監査結果

- 作成日: 2026-08-14
- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver3`
- 前回監査HEAD: `ed859a1bcc5e58fc7b3b2d55a14f1f0feedb047e`
- 今回HEAD: `5c05d249c27cc11d667349bfccae763a941d3ece`
- HEAD commit: `Add Vulkan present pacer dispatch coverage`
- follow-up検証対象HEAD: `18bad3ade1f0b3c22b1a2d6830e7d53e3f400ebe` (`fix`)
- 前回HEADから: `2 commits ahead / 0 behind`
- 前回P3 hardening: `API-level fake Vulkan dispatch integration`
- 前回P3 hardening判定: **CLOSED**
- 今回新規P2 source defect: **なし**
- 今回新規P3 source defect: **なし**
- 今回新規P4 audit/documentation finding: **3件（follow-upで解消）**
- Windows: `follow-up incremental build PASS / runtime NOT RUN`
- Linux clean build/test: repository evidence上 `PASS`
- Linux physical Vulkan runtime: `NOT RUN`
- macOS physical Vulkan runtime: repository evidence上 `PASS`（Intel + MoltenVK + GOOGLE timing path）
- Khronos validation layer: `BLOCKED / NOT RUN`
- GitHub-hosted CI: `NO STATUS / NO WORKFLOW RUN`
- 総合判定:

```text
PASS
WITH REMAINING PLATFORM / VALIDATION GAPS
```

---

# 1. 結論

今回のPushは、前回監査で最大の継続P3として残していた:

```text
API-level fake Vulkan dispatch integration
```

を実装している。

前回までのcoverageは:

```text
pure capability model
pure result classifier
pure generation helper
pure fallback helper
source contract audit
```

までであり、

```text
fake Vulkan API call
    ↓
production VulkanPresentPacer
    ↓
typed lifecycle result
    ↓
state mutation
    ↓
presenter action
```

を一続きに検査するintegration harnessが存在しなかった。

現HEADでは:

```cpp
struct VulkanPresentPacerDispatch
```

が追加され、productionが使用するpresent timing関係のVulkan function pointerをvalue-owned dispatchとして一度だけsnapshotする設計になった。

さらにtest-only seam:

```cpp
InitializeForTesting(...)
```

が:

```cpp
#if defined(MELONPRIME_VULKAN_PRESENT_PACER_TESTING)
```

配下に追加され、productionとtestの両方が:

```cpp
InitializeCommon(...)
```

を通る。

そして:

```text
tools/testing/vulkan-present-pacer-dispatch-tests.cpp
```

がproductionの:

```text
src/VulkanPresentPacer.cpp
```

そのものをcompileしてfake Vulkan dispatchを注入する。

したがって、前回残していた:

```text
static/pure model
    vs
production dispatch/state machine
```

の最大gapは実質的に埋まった。

**前回P3 hardeningはCLOSEDとしてよい。**

今回のsource変更を起点に:

- dispatch seam
- production initialization parity
- WaitForPresent2 routing
- EXT timing routing
- GOOGLE timing routing
- time-domain bounded retry
- initial timing queue allocation failure
- queue pressure
- generation invalidation
- same-frame recreation
- lifecycle failure latch
- CaptureState attribution
- CMake target wiring
- source contract audit

を再監査した。

**今回新規のP2/P3 source-level functional defectは確認できなかった。**

ただし、初回再監査時点では、機能コードではなく監査証跡・documentationについて以下3点を確認した。

```text
P4-1:
    audit READMEのcommit scope説明が実commitと一致しない

P4-2:
    vulkan-backend.mdのKnown limitationsが
    同commitのIntel/macOS/MoltenVK runtime evidenceと不整合

P4-3:
    audit READMEの「queue-pressure recovery」表現が
    fake-dispatch testの実coverageより強い
```

これらはP2/P3のruntime defectではなく、follow-upで修正・追加検証した。

---

# 2. Push確認

現在のbranch:

```text
develop_remakeVulkan_ver3
```

現在HEAD:

```text
5c05d249c27cc11d667349bfccae763a941d3ece
Add Vulkan present pacer dispatch coverage
```

前回監査HEAD:

```text
ed859a1bcc5e58fc7b3b2d55a14f1f0feedb047e
```

比較結果:

```text
status = ahead
ahead_by = 2
behind_by = 0
total_commits = 2
```

2 commits:

```text
4e7579359371aded40430b00fd95ad34de32e47f
    add md

5c05d249c27cc11d667349bfccae763a941d3ece
    Add Vulkan present pacer dispatch coverage
```

**判定: Push確認 PASS**

---

# 3. 前回HEADからの変更ファイル

GitHub compareで確認した主な変更:

```text
.codex/agents/luna-worker.toml
.codex/config.toml

.codex/
    melonPrimeDS_VulkanPresentTiming_335dc767_プッシュ後再監査_2026-08-14.md
        removed

    melonPrimeDS_VulkanPresentTiming_335dc767_プッシュ後再監査_実装結果_2026-08-14.md
        removed

    melonPrimeDS_VulkanPresentTiming_ed859a1_Push後再監査結果_2026-08-14.md
        added

docs/archive/audits/vulkan/2026-08-14-present-pacer-dispatch/
    README.md
    f2-runtime.log

docs/development/codex/
    luna-orchestrator-prompt.md

docs/features/rendering/
    vulkan-backend.md

src/
    VulkanPresentPacer.cpp
    VulkanPresentPacer.h

src/frontend/qt_sdl/
    CMakeLists.txt

tools/ci/audits/
    audit-low-latency-contract.py

tools/testing/
    vulkan-present-pacer-dispatch-tests.cpp
```

production Vulkan Present Timingの主要変更は:

```text
src/VulkanPresentPacer.cpp
src/VulkanPresentPacer.h
src/frontend/qt_sdl/CMakeLists.txt
tools/testing/vulkan-present-pacer-dispatch-tests.cpp
tools/ci/audits/audit-low-latency-contract.py
```

である。

---

# 4. 前回P3 hardeningの要求

前回監査では最大残件を:

```text
API-level fake Vulkan dispatch integration
```

としていた。

必要だったのはclassifier単体ではなく:

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

の検証である。

最低でも前回要求したresult classは:

```text
SURFACE_LOST
DEVICE_LOST
OUT_OF_DATE
SUBOPTIMAL
INCOMPLETE
NOT_READY
TIMEOUT
```

で、その後の:

```text
swapchain rebuild
renderer failure
retry pending
optional backend disable
timing metadata pause
capture attribution
generation state
```

まで確認する必要があった。

今回の実装はこの要求をほぼ直接満たしている。

**判定: 前回P3 hardening CLOSED**

---

# 5. VulkanPresentPacerDispatch設計監査

現HEADでは:

```cpp
struct VulkanPresentPacerDispatch
{
    PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR
        GetPhysicalDeviceSurfaceCapabilities2KHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR
        GetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkSetSwapchainPresentTimingQueueSizeEXT
        SetSwapchainPresentTimingQueueSizeEXT = nullptr;
    PFN_vkWaitForPresent2KHR WaitForPresent2KHR = nullptr;
    PFN_vkGetSwapchainTimingPropertiesEXT GetSwapchainTimingPropertiesEXT = nullptr;
    PFN_vkGetSwapchainTimeDomainPropertiesEXT
        GetSwapchainTimeDomainPropertiesEXT = nullptr;
    PFN_vkGetPastPresentationTimingEXT GetPastPresentationTimingEXT = nullptr;
    PFN_vkGetRefreshCycleDurationGOOGLE GetRefreshCycleDurationGOOGLE = nullptr;
    PFN_vkGetPastPresentationTimingGOOGLE GetPastPresentationTimingGOOGLE = nullptr;
};
```

が追加された。

対象はpacerが使用する9 PFNへ限定されている。

設計上:

```text
virtual dispatch:
    なし

std::function:
    なし

lock:
    なし

per-frame function-table lookup:
    なし

test-only branch in hot path:
    なし
```

で、production behaviorへtest architectureを持ち込む方式ではない。

`VulkanPresentPacerDispatch`はvalue-ownedであり、initialize時に一度copyされる。

これはfake seamとして十分に小さく、SRP/KISSの観点でも妥当。

**判定: PASS**

---

# 6. production initialization parity監査

production:

```cpp
bool VulkanPresentPacer::Initialize(
    const VulkanDevice& device,
    VkSurfaceKHR surface)
```

は:

```text
device.InstanceFns()
device.Fns()
device handle
physical device handle
enabled extensions
present timing feature bits
```

を:

```cpp
VulkanPresentPacerDispatch
VulkanPresentPacerInitInfo
```

へcopyした後:

```cpp
return InitializeCommon(dispatch, info, surface);
```

へ流す。

test:

```cpp
InitializeForTesting(...)
```

も:

```cpp
return InitializeCommon(dispatch, info, surface);
```

を使う。

したがって:

```text
production capability setup
test capability setup
```

が別実装へ分岐していない。

テスト専用初期化だけが:

```cpp
MELONPRIME_VULKAN_PRESENT_PACER_TESTING
```

でcompile-time gateされている。

**判定: PASS**

---

# 7. old Device pointer除去の回帰監査

前回のpacerは:

```cpp
const VulkanDevice* Device
```

を保持し、各API call時に:

```text
Device->Fns()
Device->InstanceFns()
Device->GetHandle()
Device->GetPhysicalDevice()
```

へ到達していた。

現HEADは:

```cpp
VulkanPresentPacerDispatch Dispatch{};
VkDevice DeviceHandle = VK_NULL_HANDLE;
VkPhysicalDevice PhysicalDeviceHandle = VK_NULL_HANDLE;
```

へ変更された。

これはfunction pointer/handleをinitialize時にsnapshotする変更であり、production pathのVulkan APIそのものは変えていない。

`Shutdown()`では:

```cpp
Dispatch = {};
DeviceHandle = VK_NULL_HANDLE;
PhysicalDeviceHandle = VK_NULL_HANDLE;
```

へ戻る。

つまり旧`VulkanDevice*` lifetimeへ依存した間接参照を減らしつつ、test injection seamを作っている。

今回確認した範囲で、old `Device->...`依存を残してdispatch seamを部分的に迂回する箇所は確認しなかった。

**判定: PASS**

---

# 8. CMake fake-dispatch target監査

現HEADのVulkan-active blockには:

```cmake
add_executable(melonprime_vulkan_present_pacer_dispatch_tests
    ${CMAKE_SOURCE_DIR}/tools/testing/vulkan-present-pacer-dispatch-tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanPresentPacer.cpp)
```

が追加された。

compile definitions:

```cmake
MELONPRIME_DS
MELONPRIME_ENABLE_VULKAN=1
MELONPRIME_VULKAN_PRESENT_PACER_TESTING=1
```

include:

```cmake
${CMAKE_CURRENT_SOURCE_DIR}/../..
${MELONPRIME_VULKAN_INCLUDE_DIR}
```

link:

```cmake
target_link_libraries(
    melonprime_vulkan_present_pacer_dispatch_tests
    PRIVATE core)
```

さらに:

```cmake
add_custom_target(
    melonprime_vulkan_present_pacer_dispatch_check ALL
    COMMAND $<TARGET_FILE:melonprime_vulkan_present_pacer_dispatch_tests>
    ...)
```

によりVulkan build時に実行対象へ入る。

`core`はrepository上:

```cmake
add_library(core STATIC ...)
```

である。

fake targetは`VulkanPresentPacer.cpp`を直接compileするため、production pacer objectを試験している。

static archive側の同objectは、直接compileされたsymbolで解決済みなら通常extract対象にならないため、この構成による明白なduplicate-symbol defectは確認しなかった。

**判定: PASS**

---

# 9. Linux Vulkan include portability修正

pure target:

```text
melonprime_vulkan_present_timing_tests
```

は`VulkanPresentPacer.h`をincludeする。

現HEADではinclude pathへ:

```cmake
"${MELONPRIME_VULKAN_INCLUDE_DIR}"
```

が明示追加された。

repository audit READMEによれば、最初のLinux buildでは:

```text
vulkan/vulkan.h: No such file or directory
```

が発生し、このinclude propagation不足が修正されたとしている。

現sourceでも修正そのものを確認できた。

**判定: PASS**

---

# 10. fake dispatch test architecture監査

新規:

```text
tools/testing/vulkan-present-pacer-dispatch-tests.cpp
```

は:

```cpp
struct FakeVulkan
```

を持ち、各APIごとに:

```text
scripted VkResult queue
call counter
fake output
```

を提供する。

production pacerは:

```cpp
InitializeForTesting(fake.Dispatch(), info, surface)
```

で初期化される。

したがってtestはpure classifierを直接呼ぶだけではなく:

```text
fake PFN
    ↓
production VulkanPresentPacer method
    ↓
production state
```

を通る。

**判定: PASS**

---

# 11. WaitForPresent2 fake-dispatch coverage

`TestWaitResults()`は:

```text
VK_SUCCESS
VK_TIMEOUT
VK_SUBOPTIMAL_KHR
VK_ERROR_OUT_OF_DATE_KHR
VK_ERROR_DEVICE_LOST
VK_ERROR_SURFACE_LOST_KHR
VK_ERROR_UNKNOWN
```

をproduction:

```cpp
BeginFrame()
```

へ注入する。

期待route:

```text
VK_SUCCESS
    -> Continue

VK_TIMEOUT
    -> Continue
    -> WaitTimeouts +1
    -> BoundedWaitAttempted = true

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

VK_ERROR_UNKNOWN
    -> DisableWait
    -> Continue
    -> subsequent WaitForPresent2 call停止
```

を確認している。

特に:

```cpp
VulkanPacerActionFor(result)
```

までtestしているためclassifierだけで終わっていない。

**判定: PASS**

---

# 12. EXT past timing fake-dispatch coverage

`TestExtPastResults()`は:

```text
VK_SUCCESS
VK_INCOMPLETE
VK_ERROR_OUT_OF_DATE_KHR
VK_ERROR_DEVICE_LOST
VK_ERROR_SURFACE_LOST_KHR
VK_ERROR_UNKNOWN
```

をproduction:

```text
ReportPastTiming()
```

経路へ注入する。

期待:

```text
SUCCESS / INCOMPLETE
    -> Continue

OUT_OF_DATE
    -> SwapchainOutOfDate
    -> rebuild

DEVICE_LOST
    -> DeviceLost
    -> renderer failure

SURFACE_LOST
    -> SurfaceLost
    -> renderer failure

unknown
    -> optional EXT timing disable
    -> subsequent query停止
```

を検証。

**判定: PASS**

---

# 13. EXT timing properties fake-dispatch coverage

`TestTimingProperties()`は:

```text
VK_SUCCESS
VK_NOT_READY
VK_ERROR_SURFACE_LOST_KHR
VK_ERROR_UNKNOWN
```

を試験する。

`VK_NOT_READY`:

```text
initial RefreshTimingProperties()
    -> retry pending

accepted present
    ↓
next BeginFrame()
    ↓
retry
    ↓
SUCCESS
```

まで検証している。

`SURFACE_LOST`は:

```text
SurfaceLost
    -> FailRenderer
```

へ到達。

unknownはrenderer failureではなく:

```text
target scheduling lifecycle retirement
```

へ落ちる。

**判定: PASS**

---

# 14. EXT time-domain fake-dispatch coverage

`TestTimeDomainsSuccessAndRetry()`は:

```text
count SUCCESS
array SUCCESS

count VK_INCOMPLETE
    -> bounded retry

array VK_INCOMPLETE
    -> bounded retry
```

を検査する。

さらに:

```cpp
TestTimeDomainIncompleteExhaustion()
```

では:

```text
count VK_INCOMPLETE x3
    ↓
bounded attempt exhaustion
    ↓
retry pending
```

を確認。

accepted presentが存在しない間は追加queryを行わず:

```text
accepted present
    ↓
next BeginFrame
    ↓
4th count SUCCESS
    ↓
array SUCCESS
```

へ復帰することまで試験している。

これは前回要求した:

```text
retry pending
```

をproduction state machineで確認する重要なcoverage。

**判定: PASS**

---

# 15. time-domain lifecycle failure

`TestTimeDomainFailures()`では:

```text
count:
    VK_ERROR_SURFACE_LOST_KHR

array:
    VK_ERROR_SURFACE_LOST_KHR
```

を注入。

production `OnSwapchainCreated()`中のeager queryで発生するため:

```text
LatchPendingBeginResult()
    ↓
next BeginFrame()
    ↓
SurfaceLost
```

となる。

presenter action:

```text
FailRenderer
```

も確認されている。

また:

```text
PRESENT_STAGE_LOCAL domain missing
```

の場合:

```text
target scheduling unavailable
```

へ安全にdowngradeする。

**判定: PASS**

---

# 16. GOOGLE refresh-cycle fake-dispatch coverage

`TestGoogleRefresh()`:

```text
VK_SUCCESS
VK_ERROR_DEVICE_LOST
VK_ERROR_SURFACE_LOST_KHR
VK_ERROR_UNKNOWN
```

を試験。

期待:

```text
SUCCESS
    -> Continue

DEVICE_LOST
    -> DeviceLost
    -> FailRenderer

SURFACE_LOST
    -> SurfaceLost
    -> FailRenderer

unknown
    -> GOOGLE runtime disable
    -> subsequent query停止
```

GOOGLE refresh-cycleはpast-timingとは別classifier/別contractのまま維持されている。

**判定: PASS**

---

# 17. GOOGLE past timing fake-dispatch coverage

`TestGooglePast()`:

```text
VK_SUCCESS
VK_INCOMPLETE
VK_ERROR_OUT_OF_DATE_KHR
VK_ERROR_DEVICE_LOST
VK_ERROR_SURFACE_LOST_KHR
VK_ERROR_UNKNOWN
```

をproductionへ注入。

期待:

```text
SUCCESS / INCOMPLETE
    -> Continue

OUT_OF_DATE
    -> swapchain rebuild

DEVICE_LOST
    -> renderer failure

SURFACE_LOST
    -> renderer failure

unknown
    -> GOOGLE optional backend disable
```

を検証。

前回CLOSEDした:

```text
GOOGLE refresh-cycle classifier
GOOGLE past-timing classifier
```

の分離も維持されている。

**判定: PASS**

---

# 18. initial timing queue allocation failure

新規:

```cpp
TestTimingQueueAllocationFailure()
```

は:

```cpp
fake.QueueSizeResult = VK_ERROR_OUT_OF_HOST_MEMORY;
```

を:

```text
vkSetSwapchainPresentTimingQueueSizeEXT
```

相当のfake callへ返す。

確認事項:

```text
queue-size setter call = 1

BeginFrame:
    Continue

ShouldUseFifoLatestReady:
    false

presenter action:
    Continue

PreparePresent:
    TimingAttached = false
    TimingBackend = None
```

これは重要。

initial queue allocation失敗は:

```text
renderer fatal
```

ではなく:

```text
optional target-time pacing unavailable
```

として処理されるべきであり、現testはそのcontractをproduction code経由で固定している。

**判定: PASS**

---

# 19. queue-full / pressure coverage

`TestQueuePressureAndRetry()`前半は:

```text
VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT
```

相当のretry pathを通し:

```text
timing metadataのみ除去
present ID維持
sequence ownership維持
wait semaphore再待機禁止
TimingQueueFullCount +1
```

のproduction behaviorを検査する。

さらにfinite queueを16 timed presentsで埋め、17番目では:

```text
TimingAttached = false
TimingQueueSize = 16
```

となり、queue pressure時にpresent前からmetadataをpauseすることを確認する。

**queue pressure detection / pause / queue-full retryはPASS。**

ただし後述P4-3の通り、このtest自体には:

```text
drain complete report
    ↓
queue growth
    ↓
TimingQueueRecoveries +1
    ↓
metadata re-enable
```

までを明示的にassertする箇所は確認できなかった。

---

# 20. same-frame recreation generation capture

`TestSameFrameRecreationCapture()`:

```text
old swapchain
    BeginFrame()
    decision stamped

same frame
    OnSwapchainCreated(new)
    generation increment
    lifecycle reset

PreparePresent()
CaptureState()
```

をproduction pacerで実行する。

期待:

```text
FallbackReason =
    FrameDecisionInvalidatedBySwapchainRecreation

TargetTimeScheduling =
    false
```

を直接assertする。

これは前回CLOSEDしたdiagnostic修正をpure helperだけでなくproduction pacerで固定したもの。

**判定: PASS**

---

# 21. same-frame recreation + lifecycle failure

今回特に強いtest:

```cpp
TestSameFrameRecreationWithLifecycleFailure()
```

を確認した。

流れ:

```text
old generation
    BeginFrame()
    live decision

same-frame recreation
    ↓
new swapchain eager timing-properties query
    ↓
VK_ERROR_SURFACE_LOST_KHR
    ↓
LatchPendingBeginResult(SurfaceLost)
```

同時に旧decisionはgeneration mismatchでinvalidとなる。

new generationの最初の`PreparePresent()`:

```text
TimingAttached = false
GoogleTimingAttached = false
TimingBackend = None
FallbackReason =
    FrameDecisionInvalidatedBySwapchainRecreation
```

次の:

```text
BeginFrame()
```

ではlatched lifecycle result:

```text
SurfaceLost
```

が返り:

```text
FailRenderer = true
RebuildSwapchain = false
```

を確認する。

つまり:

```text
generation invalidation
```

と:

```text
new-generation eager lifecycle failure
```

を混同せず両方保持する。

前回の最大static-vs-production gapに対して非常に有効なtest。

**判定: PASS**

---

# 22. generation契約の回帰監査

現HEADでも:

```text
DecisionSwapchainGeneration
WaitAttemptSwapchainGeneration
```

は別stateのまま。

`BeginFrame()`:

```cpp
LastDecision = decision;
DecisionSwapchainGeneration = SwapchainGeneration;
```

actual wait:

```cpp
WaitAttemptedThisFrame = true;
WaitAttemptSwapchainGeneration = SwapchainGeneration;
```

reset:

```text
DecisionSwapchainGeneration = 0
WaitAttemptSwapchainGeneration = 0
WaitAttemptedThisFrame = false
```

`PreparePresent()`:

```text
stale decision
    -> TimingBackend = None
```

`CaptureState()`:

```text
stale decision
    -> BoundedPresentWait = false
    -> FrameIntervalNs = 0
    -> explicit recreation fallback
```

となる。

今回dispatch seam導入によってこの契約は弱められていない。

**判定: PASS**

---

# 23. ResetTimingLifecycle()回帰監査

現HEADも:

```cpp
FallbackReason = VulkanJitFallbackReason::None;
```

を維持している。

前回問題だった:

```cpp
FallbackReason = VulkanJitFallbackReason::TelemetryOnlyPolicy;
```

への逆戻りはない。

同時に:

```text
LastDecision = {}
DecisionSwapchainGeneration = 0
TargetFrameIntervalNs = 0
WaitAttemptedThisFrame = false
WaitAttemptSwapchainGeneration = 0
PendingBeginResult = Continue
```

をresetする。

**判定: PASS**

---

# 24. fallback enum numeric stability

`VulkanPresentPacingPolicy.h`は今回のsource差分対象ではなく、前回HEADのcontractを維持している。

末尾:

```text
PresentWait2Unsupported
TimingQueuePressure
FrameDecisionInvalidatedBySwapchainRecreation
```

のnumeric orderingを今回のdispatch実装は変更していない。

**判定: PASS**

---

# 25. contract audit強化

`tools/ci/audits/audit-low-latency-contract.py`は今回:

```text
VulkanPresentPacerDispatch
9 PFNs
VulkanPresentPacerInitInfo
InitializeCommon
InitializeForTesting
Dispatch.* production calls
fake dispatch CMake target
direct VulkanPresentPacer.cpp compilation
test compile define
core link
```

をsource contractとして要求する。

さらにfake test内の:

```text
TestWaitResults
TestExtPastResults
TestTimingProperties
TestTimeDomainsSuccessAndRetry
TestTimeDomainIncompleteExhaustion
TestGoogleRefresh
TestGooglePast
TestQueuePressureAndRetry
TestTimingQueueAllocationFailure
TestSameFrameRecreationCapture
TestSameFrameRecreationWithLifecycleFailure
```

および主要`VkResult` / action / capture tokenの存在を固定している。

将来fake harnessそのものが削除・縮退するregressionを検出できる。

**判定: PASS**

---

# 26. repository内test/build evidence

repositoryへ追加された:

```text
docs/archive/audits/vulkan/
2026-08-14-present-pacer-dispatch/README.md
```

は以下を報告している。

```text
melonprime_vulkan_present_pacer_dispatch_tests:
    macOS PASS
    Linux PASS

melonprime_vulkan_present_timing_tests:
    PASS

audit-low-latency-contract.py:
    PASS

audit-raster-software-parity.py:
    PASS

check-vulkan-shaders.py:
    PASS

macOS developer Vulkan/Metal build:
    PASS

macOS release-features OFF:
    PASS (219/219)

Linux clean VM build:
    PASS (313/313)

Windows:
    NOT RUN

Khronos validation layer:
    BLOCKED / NOT RUN
```

今回GitHub connectorからこれらlocal commandを再実行したわけではない。

したがって本監査では:

```text
repository-authored execution evidence
```

として扱う。

ただしsource/CMake/test内容との整合は確認した。

---

# 27. macOS physical Vulkan runtime evidence

同commitには:

```text
docs/archive/audits/vulkan/
2026-08-14-present-pacer-dispatch/f2-runtime.log
```

が含まれる。

log上:

```text
Vulkan runtime:
    @executable_path/../Frameworks/libMoltenVK.dylib

instance API:
    1.4.357

GPU:
    Intel(R) Iris(TM) Plus Graphics 655

requested renderer:
    Vulkan

actual renderer:
    Vulkan
```

を確認できる。

device extensionsには:

```text
VK_KHR_swapchain
VK_KHR_portability_subset
VK_KHR_present_id2
VK_KHR_present_wait2
VK_GOOGLE_display_timing
```

が含まれる。

present capability:

```text
present-timing EXT:
    no

GOOGLE display timing:
    yes
```

実行中は:

```text
policy=JustInTime
authority=GenericPresentTiming
timingBackend=google_display_timing
targetMode=absolute
boundedWait=on
fallback=none
```

が記録されている。

さらに複数回のswapchain recreationログも存在する。

したがって前回:

```text
physical Vulkan runtime:
    NOT RUN
```

だった状態から、

```text
macOS Intel + MoltenVK + GOOGLE display timing path
```

についてはrepository evidence上runtime validationが追加された。

ただし:

```text
VK_EXT_present_timing physical runtime
AMD
NVIDIAの今回dispatch変更後のphysical runtime
Linux physical Vulkan runtime
Windows Vulkan runtime
```

まで一般化してはいけない。

**判定: macOS/GOOGLE path PARTIALLY CLOSED**

---

# 28. Linux validation

repository audit READMEは:

```text
VirtualBox Ubuntu clean build:
    313/313

production VulkanPresentPacer.cpp:
    compiled

pure timing test:
    PASS

fake dispatch test:
    PASS

final melonPrimeDS link:
    PASS
```

を報告している。

前回:

```text
Linux build:
    NOT RUN
```

だったため、repository evidence上Linux compile/test gapは進展している。

ただしREADME自身が:

```text
No physical Linux Vulkan/F2 run was claimed
```

としている。

よって:

```text
Linux build/test:
    PASS evidence

Linux physical Vulkan runtime:
    OPEN
```

と分離する。

---

# 29. Windows validation

初回再監査時のrepository evidence:

```text
Windows:
    NOT RUN
```

今回HEADについてGitHub-hosted workflowも存在しない。

follow-upでは現Windowsホストの既存Vulkan ON / DX12 ON Releaseツリーを
`build-mingw-existing.bat --jobs 1`でincremental buildし、production fake
dispatch target・pure timing test・XeLL testがPASSした。

したがって現時点のWindowsについて:

```text
compile:
    PASS (incremental existing-tree build)

runtime:
    NOT VERIFIED
```

**判定: build PARTIALLY CLOSED / runtime OPEN**

---

# 30. Khronos validation layer

repository audit READMEは:

```text
Khronos validation layer:
    BLOCKED / NOT RUN
```

としている。

physical macOS runはbundled direct MoltenVK loaderを使用しているため、このrunから:

```text
validation-layer clean
```

とは判定できない。

**判定: OPEN**

---

# 31. GitHub-hosted CI

current HEAD:

```text
5c05d249c27cc11d667349bfccae763a941d3ece
```

についてGitHub API上:

```text
combined status:
    statuses = []
    total_count = 0
```

commit-associated Actions:

```text
workflow_runs = []
total_count = 0
```

を確認した。

従って:

```text
GitHub-hosted CI PASS
```

とは判定しない。

repository README記載のmacOS/Linux PASSはlocal/VM execution evidenceとして別扱いとする。

---

# 32. P4-1 audit READMEのcommit scope不整合

新規audit READMEには:

```text
The unrelated pre-existing worktree entries
.codex/config.toml,
.codex/agents/,
docs/development/codex/
were preserved and not included in this audit change set.
```

と書かれている。

しかしGitHub上のcurrent commit:

```text
5c05d249
Add Vulkan present pacer dispatch coverage
```

自体には:

```text
.codex/agents/luna-worker.toml
.codex/config.toml
docs/development/codex/luna-orchestrator-prompt.md
```

が含まれている。

つまり:

```text
working-tree上でVulkan実装対象外だった
```

という意味なら理解可能だが、

```text
not included in this audit change set
```

というcommit後の証跡表現としては実際のcommit scopeと一致しない。

またREADMEの:

```text
Actual changed files
```

一覧からも上記ファイルは除外されている。

これはVulkan runtime defectではない。

follow-up実施:

```text
docs/archive/audits/vulkan/2026-08-14-present-pacer-dispatch/README.md を更新
 - 5c05d249の実際のcommit scopeを明記
 - .codex/ と docs/development/codex/ はVulkan実装範囲外だが
   co-commitされたことを明記
```

分類:

```text
P4 audit provenance / documentation
CLOSED
```

---

# 33. P4-2 vulkan-backend.mdのKnown limitationsが古い

現HEADの:

```text
docs/features/rendering/vulkan-backend.md
```

にはKnown limitationsとして:

```text
AMD Anti-Lag 2 has never been exercised on AMD hardware,
and no non-NVIDIA GPU, Linux system or macOS system has run this backend.
```

さらに:

```text
Frame-level timing/wait behaviour and
Intel/AMD/Linux/MoltenVK paths remain unverified
```

という説明が残る。

しかし同じHEADへ追加された:

```text
f2-runtime.log
```

では明確に:

```text
Intel Iris Plus Graphics 655
macOS
MoltenVK
actual renderer=Vulkan
GOOGLE display timing JIT
```

のruntime evidenceが存在する。

audit READMEも:

```text
Intel macOS developer run
30分超
resize/minimize/restore
```

を報告している。

従って少なくとも:

```text
no non-NVIDIA GPU ... has run this backend
no macOS system has run this backend
Intel/MoltenVK paths remain unverified
```

は現HEADの証跡と矛盾する。

ただし:

```text
AMD hardware:
    未検証

Linux physical runtime:
    未検証

EXT present-timing physical path:
    今回macOSでは未検証
```

は残る。

follow-up実施:

```text
docs/features/rendering/vulkan-backend.md の Known limitations を更新
 - Intel/macOS/MoltenVK + GOOGLE display timingの一台証跡を明記
 - AMD、物理Linux/Windows、物理EXT、validation layerは未検証として維持
 - one-machine evidenceをcross-GPU parityと混同しない表現に変更
```

分類:

```text
P4 documentation drift
CLOSED
```

---

# 34. P4-3 queue-pressure recovery claimの強さ

audit READMEはfake coverageとして:

```text
queue-size allocation failure, queue-pressure detection/retry/pause,
and completed-report drain -> queue growth -> metadata re-enable recovery
```

を報告している。follow-upで:

```cpp
FakeVulkan::PastReports
TestQueuePressureAndRetry()
```

を拡張し、production `VulkanPresentPacer::ReportPastTiming()` に完了レポートを注入して:

```text
completed reportをfakeで返す
    ↓
OutstandingTimedPresents減少
    ↓
queue size growth
    ↓
TimingQueueRecoveries increment
    ↓
TimingMetadataEnabled再開
```

までを直接assertした。復帰後のqueue size `16 -> 32`、
`TimingQueueRecoveries == 1`、および次のpresentへのtiming metadata再付与を固定した。

分類:

```text
P4 test-evidence wording / hardening
CLOSED
```

---

# 35. legacy surface capabilities fallback

dispatch seamには:

```text
GetPhysicalDeviceSurfaceCapabilities2KHR
GetPhysicalDeviceSurfaceCapabilitiesKHR
```

の両方が含まれる。

follow-upで`TestSurfaceCapabilitiesFallback()`を追加し:

```text
caps2 failure
    ↓
legacy GetPhysicalDeviceSurfaceCapabilitiesKHR
    ↓
capabilities success / modern swapchain flags disabled
```

をproduction pacer経由で明示assertした。

分類:

```text
P4 optional fallback hardening
CLOSED
```

---

# 36. hot-path semantics監査

今回dispatch seamのproduction impactは:

```text
Device->Fns().X(...)
```

から:

```text
Dispatch.X(...)
```

への変更が中心。

dispatchはinitialize時にcopyされる。

確認した範囲では:

```text
per-frame heap allocation:
    追加なし

std::function:
    追加なし

virtual interface:
    追加なし

mutex:
    追加なし

test bool branch:
    追加なし
```

fake integrationのために低遅延hot pathへ重いabstractionを導入していない。

**判定: PASS**

---

# 37. presenter routing回帰

今回fake testは:

```cpp
VulkanPacerActionFor(result)
```

まで確認する。

既存contract:

```text
SwapchainOutOfDate
SwapchainSuboptimal
    -> RebuildSwapchain

DeviceLost
SurfaceLost
    -> FailRenderer

Continue
    -> neither
```

は維持。

特にsame-frame recreation + surface lostで:

```text
FailRenderer = true
RebuildSwapchain = false
```

を固定した点は有効。

**判定: PASS**

---

# 38. 前回CLOSED項目の再監査

前回CLOSED:

```text
same-frame generation diagnostic
```

今回:

```text
PASS
production fake test追加
```

前回CLOSED:

```text
ResetTimingLifecycle neutral None
```

今回:

```text
PASS
```

前回CLOSED:

```text
Wait2 typed routing
```

今回:

```text
PASS
production fake injection追加
```

前回CLOSED:

```text
EXT exact result classifiers
```

今回:

```text
PASS
production fake injection追加
```

前回CLOSED:

```text
GOOGLE refresh/past classifier separation
```

今回:

```text
PASS
production fake injection追加
```

前回CLOSED:

```text
mixed EXT + GOOGLE
FIFO_LATEST_READY
```

今回dispatch変更でpure policyを変更しておらず、regressionを確認しなかった。

---

# 39. 今回のP2/P3判定

```text
P2 source-level functional defect:
    NONE FOUND

P3 source-level functional defect:
    NONE FOUND

Previous P3 hardening:
    API-level fake Vulkan dispatch integration
    CLOSED
```

初回再監査のP4 findingsはfollow-upで:

```text
P4-1:
    audit README commit-scope provenance drift
    CLOSED

P4-2:
    vulkan-backend.md runtime-status documentation drift
    CLOSED

P4-3:
    queue-pressure recovery evidence wording
    CLOSED

P4 optional:
    caps2 -> legacy fallback fake test
    CLOSED
```

とした。

---

# 40. platform validation matrix

| 項目 | 判定 | 根拠 |
|---|---|---|
| pure timing model | PASS evidence | repository audit README |
| production fake dispatch macOS | PASS evidence | repository audit README |
| production fake dispatch Linux | PASS evidence | repository audit README |
| macOS Vulkan build | PASS evidence | repository audit README |
| Linux clean build | PASS evidence | repository audit README |
| macOS physical Vulkan runtime | PASS evidence | `f2-runtime.log` |
| macOS GOOGLE timing runtime | PASS evidence | `google_display_timing` log |
| macOS EXT present timing runtime | NOT VERIFIED | runtime device reports EXT timing=no |
| Linux physical Vulkan runtime | NOT RUN | README明記 |
| Windows build | PASS (follow-up incremental) | `build-mingw-existing.bat`, Vulkan ON / DX12 ON Release tree |
| Windows runtime | NOT RUN | README明記 |
| Khronos validation layer | BLOCKED / NOT RUN | README明記 |
| AMD physical Vulkan | NOT VERIFIED | evidenceなし |
| current HEAD GitHub-hosted CI | NO RUN | GitHub API |
| long-term cross-GPU parity | NOT VERIFIED | evidence範囲外 |

---

# 41. 前回DoDとの照合

## API-level fake dispatch

- [x] injectable Vulkan timing dispatch
- [x] production/test共通initialization
- [x] test-only seamをcompile-time gate
- [x] production pacer cppを直接test targetへcompile
- [x] WaitForPresent2 fake result
- [x] EXT past timing fake result
- [x] EXT timing properties fake result
- [x] EXT time-domain count/array fake result
- [x] GOOGLE refresh fake result
- [x] GOOGLE past fake result
- [x] typed lifecycle result
- [x] presenter action mapping
- [x] runtime optional disable
- [x] bounded retry pending
- [x] generation invalidation
- [x] CaptureState attribution
- [x] same-frame recreation
- [x] recreation + lifecycle failure
- [x] queue initial allocation failure
- [x] queue pressure / pause
- [x] queue drain -> growth -> metadata recoveryをfake testで明示assert
- [x] caps2 failure -> legacy fallbackをfake testで明示assert

前回P3をCLOSEするのに必要な主要部分と、初回再監査で残したP4
hardening項目を満たしている。

---

# 42. 今後の残件一覧

```text
P2:
    none

P3 source:
    none

Previous P3 fake-dispatch:
    CLOSED

P4 audit/documentation/test evidence:
    CLOSED

Windows:
    incremental build PASS
    runtime NOT RUN

Linux:
    build/test evidence PASS
    physical runtime NOT RUN

macOS:
    Intel + MoltenVK + GOOGLE path runtime evidence PASS
    EXT present timing physical path NOT VERIFIED

Validation:
    Khronos validation layer NOT RUN
```

---

# 43. 残る検証ギャップ

```text
1. Windows Vulkan runtime

2. Linux physical Vulkan runtime

3. validation layer execution

4. EXT present timing対応GPUでphysical lifecycle test

5. cross-GPU endurance / parity
```

---

# 44. 今回CLOSEDしたものを戻さないための禁止事項

次の実装契約を維持すること。

```text
VulkanPresentPacerDispatch
```

を巨大なgeneric Vulkan abstractionへ拡大しない。

hot pathへ:

```text
std::function
virtual dispatch
mutex
heap allocation
test runtime branch
```

を持ち込まない。

productionとtestのinitializationは:

```cpp
InitializeCommon(...)
```

で共有し続ける。

```text
DecisionSwapchainGeneration
WaitAttemptSwapchainGeneration
```

を統合しない。

```text
FrameDecisionInvalidatedBySwapchainRecreation
```

を`TelemetryOnlyPolicy`へ戻さない。

```text
ResetTimingLifecycle()
    FallbackReason = None
```

を維持する。

Wait2:

```text
SUBOPTIMAL
    -> rebuild

OUT_OF_DATE
    -> rebuild

DEVICE_LOST
    -> renderer failure

SURFACE_LOST
    -> renderer failure

TIMEOUT
    -> Continue
```

を維持する。

GOOGLE:

```text
refresh-cycle
past-timing
```

classifierを再共有しない。

initial timing queue allocation failureを:

```text
renderer fatal
```

へ昇格させない。

same-frame recreationでold generationの:

```text
TimingBackend
TargetTimeScheduling
BoundedPresentWait
FrameIntervalNs
```

をnew generationへ漏らさない。

---

# 45. 最終監査判定

今回の実装HEAD:

```text
5c05d249c27cc11d667349bfccae763a941d3ece
Add Vulkan present pacer dispatch coverage
```

follow-up検証対象HEAD:

```text
18bad3ade1f0b3c22b1a2d6830e7d53e3f400ebe
fix
```

について:

```text
Push:
    PASS

previous P3 fake-dispatch gap:
    CLOSED

production dispatch seam:
    PASS

production/test initialization parity:
    PASS

WaitForPresent2 fake integration:
    PASS

EXT past-timing fake integration:
    PASS

EXT timing-properties fake integration:
    PASS

EXT time-domain fake integration:
    PASS

GOOGLE refresh fake integration:
    PASS

GOOGLE past fake integration:
    PASS

initial queue allocation failure:
    PASS

queue pressure detection / retry / pause / recovery:
    PASS

same-frame recreation capture:
    PASS

same-frame recreation + lifecycle failure:
    PASS

generation contract:
    PASS

ResetTimingLifecycle neutral fallback:
    PASS

presenter typed action:
    PASS

CMake fake test wiring:
    PASS

contract audit:
    PASS
```

新規source defect:

```text
P2:
    NONE FOUND

P3:
    NONE FOUND
```

follow-upで解消した非functional findings:

```text
P4-1:
    audit README commit-scope provenance drift
    CLOSED

P4-2:
    vulkan-backend.md stale runtime verification statement
    CLOSED

P4-3:
    queue-pressure recovery evidence wording
    CLOSED

P4 optional:
    caps2 -> legacy fallback fake test
    CLOSED
```

platform:

```text
macOS Intel/MoltenVK/GOOGLE runtime:
    repository evidence PASS

Linux clean build/test:
    repository evidence PASS

Linux physical Vulkan:
    NOT RUN

Windows:
    incremental build PASS
    runtime NOT RUN

Khronos validation:
    NOT RUN

GitHub-hosted CI:
    NO RUN
```

最終判定:

```text
PASS
WITH REMAINING PLATFORM / VALIDATION GAPS
```

**前回最大残件だったAPI-level fake Vulkan dispatch integrationと、初回再監査のP4 evidence/documentation残件はCLOSED。**

---

# 46. GitHub参照

Branch:

```text
https://github.com/ag-advania/melonPrimeDS/tree/develop_remakeVulkan_ver3
```

Implementation commit:

```text
https://github.com/ag-advania/melonPrimeDS/tree/5c05d249c27cc11d667349bfccae763a941d3ece
```

Commit:

```text
https://github.com/ag-advania/melonPrimeDS/commit/5c05d249c27cc11d667349bfccae763a941d3ece
```

主要source:

```text
src/VulkanPresentPacer.cpp
src/VulkanPresentPacer.h
src/VulkanPresentPacingPolicy.h

src/frontend/qt_sdl/CMakeLists.txt

tools/testing/vulkan-present-pacer-dispatch-tests.cpp
tools/testing/vulkan-present-timing-tests.cpp

tools/ci/audits/audit-low-latency-contract.py

docs/archive/audits/vulkan/2026-08-14-present-pacer-dispatch/README.md
docs/archive/audits/vulkan/2026-08-14-present-pacer-dispatch/f2-runtime.log
docs/features/rendering/vulkan-backend.md
```

---

# 47. 次回再監査の最優先点

次回PushではP4ではなく、以下のplatform/runtime validationを見る。

```text
1. Windows Vulkan runtime

2. Linux physical Vulkan runtime

3. Khronos validation layer execution

4. EXT present timing対応GPUでphysical lifecycle test

5. cross-GPU endurance / parity
```

source/P4の残件はなく、残るのは上記のplatform/runtime validationである。

---

# 48. MacBook側追加実施（2026-08-14）

Windows側の作業範囲には手を付けず、このIntel macOSホストで実行可能な
validation-layer / F2 runtimeだけを追加確認した。対象ソースHEADは
`fd8e5c69e`（`Close Vulkan present-pacer audit follow-up`）。

## 実施内容

```text
build:
  tools/build/macos/build_macos_metal_n_vulkan.command
    --jobs 4 --debug --no-bundle
    --build-dir build-mac-vulkan-validation

runtime:
  Vulkan loader: Homebrew vulkan-loader (libvulkan.1.dylib)
  ICD: Homebrew MoltenVK 1.4.2
  layer: VK_LAYER_KHRONOS_validation
  ROM: /Users/admin/Downloads/_Documents/Metroid Prime - Hunters (Japan).nds
  state: /Users/admin/Downloads/_Documents/Metroid Prime - Hunters (Japan).ml2
  state-unpause: MELONPRIME_TEST_SAVESTATE_UNPAUSE=1
  renderer: Vulkan
  internal resolution: 1x
  duration: 60 seconds (harness timeout; returncode -9 is intentional kill)
```

F2 stateは `loaded=1`、実rendererは `Vulkan`、GPUは Intel Iris Plus
Graphics 655、present modeは FIFO、低遅延経路は
`google_display_timing` の JustInTime であることを確認した。validation
layerは `instance created ... 1 layers` と
`[Vulkan] validation layer enabled` で有効化され、VUIDおよびvalidation
ERRORは0件だった。validation channelにはMoltenVKの既知警告
`mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT`（primitive restart無効化非対応）が
2件だけ出力された。

## MacBookで完了できない項目

このGPU/MoltenVK構成は `present-timing=no` を報告するため、
`VK_EXT_present_timing` の物理lifecycleはMacBook上では引き続き
`NOT VERIFIED`。対応GPUでのEXT実機検証、物理Linux Vulkan、Windows
Vulkan runtime、cross-GPU endurance/parityはWindows/Linux側の環境で行う。

判定（MacBook側）:

```text
PASS  Debug build + Khronos validation-layer F2 runtime (60 s)
PASS  current-HEAD pure/fake present-pacer tests (Developer/Release)
PASS  low-latency, raster-parity, shader audits
NOT VERIFIED  physical VK_EXT_present_timing (device reports no)
OUT OF SCOPE  Windows/Linux physical runtime and cross-GPU parity
```
