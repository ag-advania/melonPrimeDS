# melonPrimeDS Vulkan 次期作業 実施指示書
## Vendor-Neutral Present Pacing / Target-Time JIT 完成

- 対象リポジトリ: `ag-advania/melonPrimeDS`
- 対象ブランチ: `develop_remakeVulkan_ver2`
- 作業開始基準HEAD: `ee8ed13474cec528831708b136ec4a30286877be`
- 作成日: 2026-08-12
- 対象backend: **現行Vulkan clean-room backend**
- 主対象:
  - `src/VulkanPresentPacer.{h,cpp}`
  - `src/VulkanModernPresentCompat.h`
  - `src/VulkanDevice.{h,cpp}`
  - `src/VulkanLoader.{h,cpp}`
  - `src/frontend/qt_sdl/MelonPrimeVulkanPresenter.{h,cpp}`
  - `src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp`
  - `src/frontend/qt_sdl/EmuInstance.{h,cpp}`
  - `src/frontend/qt_sdl/EmuThread.cpp`
  - `tools/ci/audits/audit-low-latency-contract.py`
  - `docs/features/rendering/vulkan-backend.md`

---

# 1. この指示書の目的

前回監査で確認された残作業を実装し、現在の:

```text
present_id2
+
present_wait2
+
present_timing telemetry
+
adaptive bounded wait
+
experimental FIFO_LATEST_READY
```

を、

```text
dynamic timing-property tracking
+
time-domain tracking
+
presentation feedback
+
target-time scheduling
+
validated FIFO_LATEST_READY
```

まで完成させる。

最終目標は:

> **`JustInTime` / `JustInTimeFifoLatestReady` というpolicy名と、実際のruntime挙動を一致させること。**

現在の`JustInTime`は、名前に反して`VkPresentTimingInfoEXT::targetTime`を使用していない。

今回の作業ではこれを**真のtarget-time presentation scheduling**へ進める。

---

# 2. 作業開始前に必ず確認すること

作業開始時にremote HEADを再確認すること。

基準:

```text
ee8ed13474cec528831708b136ec4a30286877be
Fix Vulkan/DX12 issues
```

HEADが進んでいた場合:

1. 最新HEADを取得
2. `VulkanPresentPacer`周辺の差分を確認
3. この指示書と競合する変更が既に入っていないか確認
4. 既に実装済みの作業を二重実装しない

---

# 3. Clean-Room制約

現行Vulkan backendはclean-room rewriteである。

今回の作業で参照してよいもの:

```text
現行 develop_remakeVulkan_ver2 のVulkan clean-room実装
GPU3D_Compute
Software renderer
DX12 backendのfunctional integration shape
Khronos Vulkan Specification
Khronos Vulkan-Headers
Validation Layers
```

参照禁止:

```text
WatermelonDS
SapphireRhodonite/melonDS-android
SapphireRhodonite/melonDS-android-lib
旧Sapphire Vulkan backend
旧Vulkan implementationのcode/design/shader/comment/history
```

**旧Sapphire実装を見て実装方法を決めないこと。**

また、現行コメントに残っている:

```text
previous Vulkan backend
```

等の表現も、clean-room contractと紛らわしいものは別commitで整理する。

---

# 4. 現在すでに実装済みのもの

以下は**作り直さない**。

```text
VK_KHR_get_surface_capabilities2
VK_KHR_present_id2
VK_KHR_present_wait2
VK_KHR_calibrated_timestamps
VK_EXT_present_timing
VK_KHR_present_mode_fifo_latest_ready

VulkanPresentPacer
surface-scoped capability query
VkPresentId2KHR
bounded vkWaitForPresent2KHR
Present Timing results queue
timing queue drain
VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT retry
pacing authority
Reflex correlation
Anti-Lag 2 integration
swapchain recreation reset
TelemetryOnly default
```

また以下も維持する。

```text
NVIDIA Reflex
AMD Radeon Anti-Lag 2
host 60 FPS limiter
late input polling
F2 / settings probe fix
Windows Vulkan device lifetime fix
```

---

# 5. 現在の問題

現状:

```cpp
metadata.Timing.sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT;
metadata.Timing.presentStageQueries = PresentStageQueries;
```

は設定されているが、

```text
flags = 0
targetTime = 0
```

のまま。

つまり現在の:

```text
JustInTime
```

は実際には:

```text
previous presentへのbounded wait
```

であり、

```text
presentation engineへ将来のtarget present timeを指定
```

していない。

---

# 6. 今回の実装方針

一気にCPU frame limiterまで書き換えない。

順序:

```text
Phase 1
Timing Properties lifecycle

Phase 2
Time Domain lifecycle

Phase 3
Presentation feedback baseline

Phase 4
Target-Time scheduling

Phase 5
FIFO_LATEST_READY gate

Phase 6
Diagnostics / CI / docs

Phase 7
Runtime validation
```

**Host FPS limiterのownership変更は今回の必須範囲に含めない。**

まず:

```text
DS emulation rate control
```

と:

```text
Vulkan presentation scheduling
```

を分離したまま完成させる。

---

# 7. Phase 1 — Timing Properties lifecycle

## 7.1 現在の問題

現在の:

```cpp
RefreshTimingProperties()
```

は主にswapchain作成直後に呼ばれる。

しかし`vkGetSwapchainTimingPropertiesEXT`はswapchain作成直後に:

```text
VK_NOT_READY
```

を返してよい。

さらにtiming propertiesはruntime中に変化する。

現在は:

```text
timingPropertiesCounter
```

を保存・追跡していない。

---

# 8. 追加state

`VulkanPresentPacer`へ最低限追加する。

例:

```cpp
u64 TimingPropertiesCounter = 0;
bool TimingPropertiesReady = false;
```

必要に応じて:

```cpp
bool TimingPropertiesRetryPending = false;
```

を持ってもよい。

---

# 9. `RefreshTimingProperties()` の仕様変更

現在の単純なvoid処理を、結果を区別できる形へ変更する。

例:

```cpp
enum class TimingRefreshResult
{
    Updated,
    NotReady,
    Unavailable,
    Failed,
};
```

または同等の明示的な表現。

要件:

```text
VK_SUCCESS
    → RefreshDurationNs / RefreshIntervalNs / counter更新
    → TimingPropertiesReady = true

VK_NOT_READY
    → ERROR扱いしない
    → timing schedulingを一時pending
    → 後で再試行

その他failure
    → target schedulingだけdisable
    → Vulkan renderer自体は継続
```

---

# 10. 初回`VK_NOT_READY`再試行

次のどちらか、または両方で再試行する。

```text
最初のsuccessful/suboptimal present後
```

および:

```text
ReportPastTiming()でtiming resultを受け取った時
```

条件:

```text
!TimingPropertiesReady
```

なら:

```text
RefreshTimingProperties()
```

を再実行。

毎frame無条件queryは禁止。

---

# 11. dynamic property change

`VkPastPresentationTimingPropertiesEXT`の:

```text
timingPropertiesCounter
```

を必ず確認。

現在値と違う場合:

```text
TimingPropertiesCounter更新
↓
RefreshTimingProperties()
↓
target model再計算
```

とする。

property changeの例:

```text
refresh rate change
fullscreen transition
power state
VRR/FRR state
driver/display mode
```

---

# 12. FRR / VRR state

`RefreshTimingProperties()`成功時に内部stateとして区別できるようにする。

```text
refreshInterval == 0
    → timing dynamics unknown

refreshInterval == UINT64_MAX
    → VRR

refreshInterval == refreshDuration
    → FRR

その他
    → dynamic/mixed timing
```

これはlog/diagnostic用でよい。

ここで勝手にVSync設定を書き換えない。

---

# 13. Phase 2 — Time Domain lifecycle

`VK_EXT_present_timing`でabsolute target timeを使うため、time domain IDを管理する。

現在Loaderには:

```text
vkGetSwapchainTimeDomainPropertiesEXT
vkGetCalibratedTimestampsKHR
```

があるが、target schedulerへ未接続。

---

# 14. 追加state

例:

```cpp
u64 TimeDomainsCounter = 0;
bool TimeDomainsReady = false;

VkTimeDomainKHR TargetTimeDomain = ...;
u64 TargetTimeDomainId = 0;

VkPresentStageFlagsEXT TargetPresentStage = 0;
```

surface capabilityとして現在保持している:

```text
presentAtRelativeTimeSupported
```

だけでなく:

```text
presentAtAbsoluteTimeSupported
```

も保持する。

---

# 15. `RefreshTimeDomains()`

新規helperを追加。

概念:

```cpp
bool RefreshTimeDomains();
```

実装はKhronos推奨どおりtwo-call query。

```text
1. count query
2. vector確保
3. domain + domain ID取得
```

これはswapchain作成時・counter変化時だけなので、一時的な`std::vector`使用は許容。

per-frame heap allocationは禁止。

---

# 16. Domain selection

初期実装ではabsolute target schedulingを優先する。

優先domain:

```text
1. VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT
2. VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT
```

`PRESENT_STAGE_LOCAL`を選ぶ場合は:

```text
targetTimeDomainPresentStage
```

へ単一のstage bitを設定する。

---

# 17. Target Present Stage選択

surfaceの:

```text
presentStageQueries
```

から、実際に利用可能なstageだけを選ぶ。

推奨優先順位:

```text
VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT
↓
VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT
↓
VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT
↓
VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT
```

理由:

実表示に近いstageを優先する。

ただし対応していないstageを要求しない。

---

# 18. `timeDomainsCounter`

`ReportPastTiming()`で:

```text
properties.timeDomainsCounter
```

が保存値と違った場合:

```text
RefreshTimeDomains()
↓
existing timing baseline invalidate
```

する。

古いdomain IDを次のpresentへ使用しない。

---

# 19. Swapchain recreation時

以下を必ずreset。

```text
TimingPropertiesCounter
TimingPropertiesReady
TimeDomainsCounter
TimeDomainsReady
TargetTimeDomainId
TargetPresentStage
JIT baseline
JIT sequence mapping
last target time
```

古いswapchainのtiming stateを新しいswapchainへ持ち越さない。

---

# 20. Phase 3 — Timing feedbackをschedulerへ接続

現在`ReportPastTiming()`は主にlog用。

これをscheduler feedbackとして使用する。

---

# 21. Feedback state

最低限:

```cpp
u64 BaselineLogicalPresentId = 0;
u64 BaselineSequence = 0;
u64 BaselineStageTimeNs = 0;
u64 BaselineTimeDomainId = 0;
VkTimeDomainKHR BaselineTimeDomain = ...;
bool BaselineValid = false;
```

を持つ。

---

# 22. Logical IDとpresentation sequenceを分離

重要。

現在logical IDは:

```text
Reflex active
    → Reflex frame ID

Reflex inactive
    → generic ID
```

である。

Reflex IDはemulation frame単位で進むため、presentされなかったframeがあるとgapが発生し得る。

target schedulingで:

```text
logical ID差
```

をそのまま:

```text
present count差
```

として扱わない。

---

# 23. Present sequence

`VulkanPresentPacer`内部に:

```cpp
u64 PresentSequence = 0;
```

を追加。

**新しいpresent requestを準備した時だけ** increment。

retry:

```text
VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT
↓
same image / same ID retry
```

ではincrementしない。

---

# 24. ID → sequence mapping

timing feedbackはlogical present IDで返る。

そのため小さな固定ringを追加する。

例:

```cpp
struct PresentSequenceRecord
{
    u64 LogicalId = 0;
    u64 Sequence = 0;
};

std::array<PresentSequenceRecord, 32> PresentSequenceHistory{};
```

目的:

```text
timing feedback presentId
↓
local presentation sequence
```

へ変換。

毎frame heap allocation禁止。

---

# 25. Feedback採用条件

`VkPastPresentationTimingEXT`を受け取った時:

```text
reportComplete
```

または必要stageのtimeが取得済みであり、

```text
presentId != 0
stage time != 0
time domain valid
```

の場合だけbaseline更新。

対象stageが結果にない場合:

```text
baseline更新しない
```

0 timestampを有効値として扱わない。

---

# 26. Feedback domain mismatch

driverはrequested domainと異なるdomainで結果を返す場合がある。

その場合:

```text
現在選択中domainと一致
    → baseline採用

不一致
    → baseline invalidate
    → RefreshTimeDomains()
    → schedulingは一時telemetry-only
```

安全側へ倒す。

---

# 27. Phase 4 — Target-Time JIT

ここで初めて:

```text
targetTime != 0
```

を有効にする。

---

# 28. Desired frame intervalをPresenterへ渡す

重要。

monitor refresh rateからDSのframe intervalを推測してはいけない。

melonPrimeDSの実際のframe intervalはEmuThread側に既に存在する:

```text
storedFrametimeStep
```

これを使う。

---

# 29. frontend signature拡張

現行:

```text
beginVulkanLowLatencyFrame(
    reflexMode,
    antiLag2Enabled,
    normalSpeed)
```

へframe intervalを追加する。

例:

```cpp
beginVulkanLowLatencyFrame(
    int reflexMode,
    bool antiLag2Enabled,
    bool normalSpeed,
    u64 targetFrameIntervalNs);
```

経路:

```text
EmuThread
↓
EmuInstance
↓
MainWindow / ScreenPanelVulkan
↓
VulkanPresenter
↓
VulkanPresentPacer
```

---

# 30. interval計算

EmuThreadで:

```cpp
const u64 targetFrameIntervalNs =
    static_cast<u64>(std::llround(storedFrametimeStep * 1'000'000'000.0));
```

同等の安全な計算を行う。

使用条件:

```text
limitFPS
&& !fastforward
&& !slowmo
```

それ以外は:

```text
targetFrameIntervalNs = 0
```

としてgeneric target schedulingを無効にする。

---

# 31. 60 FPSをhard-codeしない

禁止:

```cpp
constexpr u64 kFrameNs = 16'666'667;
```

`TargetFPS`は設定可能であり、`storedFrametimeStep`が既にruntime source of truth。

---

# 32. Absolute target calculation

Khronosのfeedback-based方式に合わせる。

概念:

```text
baseline stage time
+
(current presentation sequence - baseline sequence)
  * target frame interval
=
target presentation time
```

例:

```cpp
targetTime =
    BaselineStageTimeNs
    + (currentSequence - BaselineSequence)
      * TargetFrameIntervalNs;
```

overflow guardを入れる。

---

# 33. Target flag

absolute target scheduling時:

```text
VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT
```

を使用。

理由:

60 FPSと144 Hz等、app frame rateとdisplay refreshが整数倍でない場合でも、target時刻に最も近いrefresh cycleをdriverに選ばせる。

FRR display向けに固定:

```text
2 refresh
3 refresh
2 refresh
...
```

のようなcadenceをアプリ側で手書きしない。

---

# 34. `VkPresentTimingInfoEXT`

JIT active時:

```cpp
metadata.Timing.flags =
    VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT;

metadata.Timing.targetTime = calculatedTargetTime;
metadata.Timing.timeDomainId = TargetTimeDomainId;
metadata.Timing.targetTimeDomainPresentStage =
    TargetTimeDomain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT
        ? TargetPresentStage
        : 0;

metadata.Timing.presentStageQueries = PresentStageQueries;
```

概念上この状態にする。

---

# 35. Absolute scheduling有効条件

次をすべて満たす場合だけ:

```text
policy = JustInTime
or JustInTimeFifoLatestReady

normal speed
TargetFrameIntervalNs > 0

PresentTimingSurface
PresentId2Surface

presentAtAbsoluteTime device feature enabled
presentAtAbsoluteTimeSupported surface capability

TimingPropertiesReady
TimeDomainsReady

TargetTimeDomainId valid
TargetPresentStage valid

BaselineValid
```

満たさない場合:

```text
targetTime = 0
```

へfallback。

**renderer failureにしない。**

---

# 36. bootstrap phase

swapchain作成直後はbaselineがない。

その間:

```text
targetTime = 0
```

で普通にpresentし、timing feedbackを収集。

有効なbaselineを得た次のframeからtarget scheduling開始。

これを明示的なstateにする。

例:

```text
TelemetryBootstrap
TargetSchedulingActive
```

---

# 37. stale target guard

計算したtarget timeが、最新feedbackから見て明らかに古い場合:

```text
targetTime = 0
baseline invalidate
```

または最新feedbackでrebase。

古いtarget timeをdriverへ送り続けない。

---

# 38. scheduling rebase

Khronos仕様の考え方に合わせて、absolute target計算は定期的に最新feedbackからrebaseする。

つまり:

```text
最初のbaselineを永遠に使わない
```

新しいcomplete timing feedbackが来たら:

```text
BaselineStageTimeNs
BaselineSequence
```

を更新。

clock drift / rounding errorを蓄積させない。

---

# 39. calibrated timestamps

今回の必須実装では:

```text
vkGetCalibratedTimestampsKHR
```

をCPU clockとのJIT wake calculationに使う必要はない。

present feedback自体をabsolute scheduling baselineに使うため。

したがって:

> **Calibrated timestamp plumbingは残すが、無理に今回CPU-start schedulerまで実装しない。**

CPU startをtarget-timeから逆算する最適化は、runtime telemetry確認後の別Phaseとする。

---

# 40. PresentWait2との関係

`JustInTime`でも現在のbounded:

```text
vkWaitForPresent2KHR
```

は維持してよい。

順序:

```text
Host FPS limiter
↓
PresentWait2
↓
Reflex sleep if active
↓
fresh input
↓
simulation
↓
render
↓
target-time vkQueuePresentKHR
```

ただしReflex / Anti-Lag 2 active時はgeneric wait authorityを取らない既存ルールを維持する。

---

# 41. Host limiter

今回:

```text
Vulkan generic pacing active
→ host limiter bypass
```

へ戻してはいけない。

理由:

target-time presentationは:

```text
emulation clock
```

そのものではない。

Host limiterは引き続き:

```text
TargetFPS / emulation speed
```

を保持する。

---

# 42. Phase 5 — FIFO_LATEST_READY

現在は:

```text
policy = JustInTimeFifoLatestReady
+
PresentTimingRuntimeEnabled
```

程度で選択可能。

これを強化。

---

# 43. 新しいgate

`FIFO_LATEST_READY`は最低限:

```text
VSync ON
policy = JustInTimeFifoLatestReady

PresentTimingSurface
PresentId2Surface
LatestReadyDevice

absolute target scheduling capability
TimingPropertiesReady
TimeDomainsReady
TargetScheduling infrastructure ready
```

を満たす時だけ選択。

ただしswapchain作成時点ではbaseline timing feedbackはまだ存在しないため、

```text
BaselineValid
```

自体をswapchain mode選択条件にはしない。

必要なのは:

> **target schedulingを将来activeにできるcapability/lifecycleが成立していること。**

---

# 44. runtime scheduler failure

swapchain作成後にtarget schedulerだけ失敗した場合:

```text
FIFO_LATEST_READY swapchainを即破棄
```

する必要はない。

まず:

```text
targetTime = 0
```

で安全に継続。

次のswapchain recreation時にpolicy/capabilityに応じてFIFOへ戻すか判断。

runtime failureでrecreate stormを起こさない。

---

# 45. VSync OFF

target present time semanticsはFIFO present modes向け。

したがって:

```text
VSync OFF
IMMEDIATE / MAILBOX
```

ではtarget schedulingをactiveにしない。

Telemetryは続けてもよい。

---

# 46. Phase 6 — Diagnostics

developer logへ追加。

状態変化時のみ出す。

例:

```text
[Vulkan] present JIT:
policy=JustInTime
timingReady=yes
timeDomainsReady=yes
absoluteSupported=yes
targetStage=IMAGE_FIRST_PIXEL_VISIBLE
timeDomain=SWAPCHAIN_LOCAL
domainId=...
frameIntervalNs=16666667
baselineId=...
baselineSequence=...
baselineTime=...
targetTime=...
authority=GenericPresentTiming
```

毎frame大量logは禁止。

---

# 47. periodic summary

既存600frame summaryへ追加:

```text
target scheduling active
targetTime
feedback presentId
feedback stage time
timingPropertiesCounter
timeDomainsCounter
refreshDuration
refreshInterval
VRR/FRR
PresentWait timeout count
JIT fallback reason
```

---

# 48. fallback reason

最低限分類:

```text
telemetry-only policy
vendor latency API owns pacing
not normal speed
present_id2 unsupported
present_wait2 unsupported
present timing unsupported
absolute timing unsupported
surface absolute timing unsupported
timing properties not ready
time domains not ready
no valid target stage
bootstrap waiting for feedback
domain changed
timing query failed
```

---

# 49. Fail-soft原則

以下のどれが失敗しても:

```text
present timing
time-domain query
target scheduler
FIFO_LATEST_READY
```

Vulkan renderer自体をSoftwareへfallbackさせない。

fallback:

```text
target scheduling
↓
PresentWait
↓
TelemetryOnly
↓
GenericHost
```

へ段階的に落とす。

---

# 50. `VK_ERROR_DEVICE_LOST`

generic pacingの新コードで:

```text
device lost
```

を握り潰さない。

`VK_ERROR_DEVICE_LOST`は既存Vulkan runtime failure pathへ渡す。

「optional featureだから無視」で継続しない。

optional feature固有のfailureとdevice-level fatal failureを区別する。

---

# 51. F2 / settings regression禁止

今回の変更で:

```text
F2
↓
Video Settings
↓
別backend probe
```

を復活させない。

Vulkan選択中はVulkan feature stateのみ。

DX12 probeはDX12選択時のみ。

現在のfixを維持する。

---

# 52. Windows device lifetime regression禁止

`ProcessLifetimeDevice()`周辺は、今回のpresent timing作業と直接関係しない。

不要に変更しない。

clean-roomコメントの文言整理だけを行う場合は、runtime変更とは別commit。

---

# 53. Phase 6 — Pure timing-model test

target calculationはVulkan runtimeなしでもtestできるようにする。

推奨:

```text
VulkanPresentTimingModel
```

の純粋C++計算部分を小さく分離する。

新規候補:

```text
src/VulkanPresentTimingModel.h
```

または`VulkanPresentPacer`内部の独立struct。

巨大なmanager classにはしない。

---

# 54. Pure modelに入れるもの

入れる:

```text
frame interval
present sequence
baseline sequence
baseline stage time
target calculation
overflow guard
rebase
stale baseline判定
counter changeによるreset
```

入れない:

```text
VkDevice
VkSwapchainKHR
Vulkan dispatch
queue submit
Qt
renderer lifecycle
```

---

# 55. unit test

候補:

```text
tools/testing/vulkan-present-timing-tests.cpp
```

最低テスト:

```text
60 FPS target interval
120 Hz / 144 Hzに関係なくabsolute target intervalが60 FPS周期になる

sequence gap
logical present ID gap
retry present
baseline rebase
counter reset
overflow
zero interval
bootstrap
stale feedback
```

特に:

```text
logical ID gap ≠ presentation sequence gap
```

をテストする。

---

# 56. 144 Hzテスト

例:

```text
frame interval = 16,666,667 ns
baseline = 1,000,000,000 ns
```

ならsequence:

```text
+1 → 1,016,666,667
+2 → 1,033,333,334
+3 → 1,050,000,001
```

のようにapp側targetは60 FPS周期を維持する。

144Hz向けrefresh cycle選択は:

```text
NEAREST_REFRESH_CYCLE
```

をdriverへ任せる。

---

# 57. CI static audit

既存:

```text
tools/ci/audits/audit-low-latency-contract.py
```

を拡張。

必須contract:

```text
TelemetryOnly default = 0

JustInTime pathに
targetTime non-zero生成処理が存在

timingPropertiesCounterを比較

timeDomainsCounterを比較

VK_NOT_READYをfatal扱いしない

swapchain destructionでtiming model reset

generic wait → input ordering維持

Reflex active時generic waitを重ねない

Anti-Lag active時generic waitを重ねない

VSync OFFでtarget schedulingを有効にしない

FIFO_LATEST_READYはJIT capability gate下のみ

host limiterをVulkan generic pathで無条件bypassしない
```

---

# 58. Validation Layer

developer buildでKhronos validationを有効にできる場合:

```text
JustInTime
JustInTimeFifoLatestReady
```

それぞれを最低数分実行し:

```text
VUID
pNext lifetime
timeDomainId
targetTimeDomainPresentStage
swapchain flag
present mode
present ID
```

関連validation errorが0であること。

---

# 59. Modern header compat

minimum supported Vulkan SDKが新structを持たない場合のみ:

```text
VulkanModernPresentCompat.h
```

へKhronos Vulkan-Headersと**完全一致する定義**を追加。

独自ABIを作らない。

追加が必要になり得る候補:

```text
VkSwapchainCalibratedTimestampInfoEXT
VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT
VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT
関連structure type
```

ただし現在のbuild headerに既にあるものを二重定義しない。

`#ifndef extension/macro` gateを維持。

---

# 60. CMake

新test source / helper sourceを追加した場合:

```text
MELONPRIME_ENABLE_VULKAN
```

gate内だけに登録。

Vulkan OFF buildを壊さない。

既存clean-room build matrix:

```text
Vulkan ON
Vulkan OFF
Windows + DX12 coexist
```

を維持。

---

# 61. 共通コード変更

`EmuThread.cpp`等の共有melonDSファイルを変更する場合:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
```

で必要範囲を限定。

Vulkan timing値をSoftware/OpenGL pathへ漏らさない。

---

# 62. Performance

禁止:

```text
毎frame vector allocation
毎frame device/surface capability query
毎frame time-domain enumerate
毎frame大量log
毎frame Config lookup追加
毎frame vkDeviceWaitIdle
```

許容:

```text
swapchain creation
counter change
runtime failure
```

時の一時allocation/query。

---

# 63. Queue synchronization

`vkGetPastPresentationTimingEXT`等のhost accessにはswapchain external synchronization要件がある。

現在Presenterのqueue/present pathとのthread ownershipを確認し、

```text
同じswapchainへ別threadから同時host operation
```

が発生しないことを保証。

必要なら既存Presenter ownership/queue mutexを利用。

新しい粗いglobal lockは追加しない。

---

# 64. Present timing queue

既存:

```text
queue size = 16
毎frame drain
queue full → timing metadataを外してretry
```

を維持。

target schedulingを追加しても:

```text
VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT
```

時は**同じimage / same logical ID / same presentation sequence**でretry。

新しいsequenceとして数えない。

---

# 65. Present result

次だけをaccepted presentとして扱う。

```text
VK_SUCCESS
VK_SUBOPTIMAL_KHR
```

`VK_ERROR_OUT_OF_DATE_KHR`:

```text
swapchain reset
timing baseline reset
sequence history reset
```

device lost:

```text
existing fatal runtime path
```

---

# 66. Fast Forward / Slow Motion

現行方針維持。

```text
fastforward
slowmo
LimitFPS OFF
```

では:

```text
Generic PresentWait2 behavioral pacing OFF
target-time scheduling OFF
FIFO Latest Ready JIT policy OFF
```

Telemetryは可能なら維持してよい。

---

# 67. NVIDIA Reflex

変更禁止事項:

```text
vkLatencySleepNV ordering
INPUT_SAMPLE ordering
Simulation markers
RenderSubmit markers
Present markers
VkLatencySubmissionPresentIdNV
VkPresentIdKHR
```

generic timing追加でmarker位置をずらさない。

---

# 68. AMD Anti-Lag 2

維持:

```text
INPUT immediately before input
PRESENT immediately before vkQueuePresentKHR
same frame index
```

generic JITをAnti-Lag active時にbehavioral wait authorityとして重ねない。

---

# 69. UI / Config

既存:

```text
3D.Vulkan.PresentPacingPolicy
```

を使用。

新しいuser-facing release settingは増やさなくてよい。

当面developer A/B用途を維持。

default:

```text
TelemetryOnly
```

を変更しない。

実機検証完了前に:

```text
JustInTime
```

をdefaultにしない。

---

# 70. Policy semantics完成後

最終policy:

## TelemetryOnly

```text
Present Timing telemetryのみ
host limiter使用
targetTime = 0
PresentWait2 behavioral waitなし
```

## PresentWait

```text
host limiter使用
bounded PresentWait2
targetTime = 0
```

## JustInTime

```text
host limiter使用
bounded PresentWait2
absolute target-time scheduling
FIFO
```

## JustInTimeFifoLatestReady

```text
host limiter使用
bounded PresentWait2
absolute target-time scheduling
FIFO_LATEST_READY
```

これで名前と実装が一致する。

---

# 71. Runtime test matrix

## GPU

最低:

```text
NVIDIA GPU
```

可能なら:

```text
AMD
Intel
```

Intel Arc実機がない場合:

```text
Intel path = 未検証
```

と明記。

---

# 72. Vendor API比較

NVIDIA環境では:

```text
A. Reflex Off / TelemetryOnly
B. Reflex Off / PresentWait
C. Reflex Off / JustInTime
D. Reflex Off / JustInTimeFifoLatestReady
E. Reflex On
```

を比較。

Generic pathの比較時はReflexをOffにする。

---

# 73. Display

最低:

```text
60 Hz
120/144 Hz
```

可能なら:

```text
VRR ON
VRR OFF
```

を比較。

特に60FPS emulator + 144Hz displayで:

```text
60 FPS emulation rateが崩れない
present cadenceが不安定化しない
```

ことを確認。

---

# 74. VSync

```text
VSync ON
VSync OFF
```

VSync OFFでは:

```text
target-time scheduling inactive
```

をlogで確認。

---

# 75. Lifecycle

```text
起動
ROM load
F2 Video Settings
設定Cancel
設定Apply
Vulkan → Software
Software → Vulkan
Vulkan → DX12
DX12 → Vulkan
fullscreen
windowed
resize
DPI change
minimize
restore
savestate
ROM close
ROM reopen
```

F2後:

```text
VK_ERROR_DEVICE_LOSTなし
Software化なし
Vulkan optionグレーアウトなし
```

を確認。

---

# 76. Frame-rate modes

```text
LimitFPS ON
LimitFPS OFF
Fast Forward Hold
Fast Forward Toggle
Slow Motion
```

normal speed以外でgeneric target schedulerが勝手にrateを制限しないこと。

---

# 77. Measurement

developer telemetryで最低:

```text
emulation FPS
frame time P50/P95
PresentWait timeout count
target scheduling active ratio
timing feedback availability
queue-full count
swapchain recreate count
```

を確認。

可能なら外部latency測定も行う。

---

# 78. Success Criteria

実装完了条件:

```markdown
- [ ] `timingPropertiesCounter`追跡
- [ ] `timeDomainsCounter`追跡
- [ ] `VK_NOT_READY`再試行
- [ ] dynamic timing property refresh
- [ ] dynamic time-domain refresh
- [ ] valid target stage選択
- [ ] logical IDとpresentation sequenceを分離
- [ ] timing feedback baseline
- [ ] `targetTime != 0` JIT path
- [ ] `NEAREST_REFRESH_CYCLE`
- [ ] host limiter維持
- [ ] Reflex回帰なし
- [ ] Anti-Lag 2回帰なし
- [ ] FIFO_LATEST_READY gate強化
- [ ] swapchain reset完全
- [ ] queue-full retryでsequence重複なし
- [ ] Fast Forward/Slow MotionでJIT inactive
- [ ] CI contract追加
- [ ] Vulkan ON build
- [ ] Vulkan OFF build
- [ ] Windows DX12 coexist build
- [ ] Validation errorなし
- [ ] F2 regressionなし
```

---

# 79. 未検証をPASSにしない

実行していない場合:

```text
Windows build          NOT RUN
Linux build            NOT RUN
NVIDIA runtime         NOT RUN
AMD runtime            NOT RUN
Intel runtime          NOT RUN
MoltenVK runtime       NOT RUN
Validation Layer       NOT RUN
Latency measurement    NOT RUN
```

と正直に記録する。

---

# 80. 推奨コミット分割

## Commit 1

```text
Vulkan: track dynamic presentation timing properties
```

内容:

```text
TimingPropertiesCounter
VK_NOT_READY retry
property refresh
FRR/VRR diagnostics
```

---

## Commit 2

```text
Vulkan: track swapchain presentation time domains
```

内容:

```text
time-domain enumeration
TimeDomainsCounter
target stage selection
domain lifecycle
```

---

## Commit 3

```text
Vulkan: add presentation sequence feedback model
```

内容:

```text
presentation sequence
ID→sequence ring
feedback baseline
rebase
pure calculation tests
```

---

## Commit 4

```text
Vulkan: implement target-time JIT presentation
```

内容:

```text
target frame interval plumbing
absolute target calculation
NEAREST_REFRESH_CYCLE
bootstrap/fallback
```

---

## Commit 5

```text
Vulkan: gate FIFO_LATEST_READY on target-time scheduling
```

---

## Commit 6

```text
CI: enforce Vulkan target-time pacing contract
```

---

## Commit 7

```text
Docs: document Vulkan target-time pacing and clean-room status
```

このcommitでのみclean-room wording整理も行う。

---

# 81. やってはいけないこと

```text
Sapphire Vulkanを参考にする
旧Vulkan codeをcopyする
host limiterを再度無条件bypassする
60 FPSをhard-codeする
display refresh = emulator FPSと仮定する
144Hzで固定2/3 cadenceを手書きする
targetTimeをclock feedbackなしで延々積算する
古いtimeDomainIdをswapchain recreation後に使う
VK_NOT_READYをfatal errorにする
optional present timing failureでVulkan全体をSoftware化する
Reflex marker位置を変更する
Anti-Lag marker位置を変更する
毎framedevice capability queryする
毎frameheap allocationする
実機未確認をPASSと書く
```

---

# 82. コメント方針

timing codeは将来見ても理由が分かるコメントを書く。

特に:

```text
なぜhost limiterを残すか
なぜabsolute targetをfeedbackからrebaseするか
なぜlogical IDとpresent sequenceを分けるか
なぜVK_NOT_READYが正常状態か
なぜFIFO_LATEST_READYをcapabilityだけで即使わないか
なぜReflex/Anti-Lagとgeneric waitを重ねないか
```

を説明する。

「仕様がそうだから」だけで終わらず、melonPrimeDS上の目的も書く。

---

# 83. 最終的な期待経路

normal speed / generic JIT:

```text
Host FPS limiter
    │
    │ DS emulation rateを維持
    ↓
VulkanPresentPacer::BeginFrame
    │
    ├─ timing feedback drain
    ├─ property/domain refresh when needed
    └─ bounded PresentWait2
    ↓
fresh input
    ↓
simulation
    ↓
Vulkan render / composition
    ↓
PreparePresent
    │
    ├─ Present ID2
    ├─ timing query request
    └─ targetTime != 0
    ↓
vkQueuePresentKHR
    ↓
presentation engine schedules near target refresh
    ↓
timing feedback
    ↓
next target rebase
```

NVIDIA Reflex active:

```text
Host FPS limiter
↓
generic behavioural wait disabled
↓
Reflex sleep
↓
late input
↓
render
↓
Reflex-correlated Present ID
↓
present
```

AMD Anti-Lag 2 active:

```text
Host FPS limiter
↓
generic behavioural wait disabled
↓
Anti-Lag INPUT
↓
input
↓
render
↓
Anti-Lag PRESENT
↓
present
```

---

# 84. 完了時に提出するもの

実装者は完了時に以下を残す。

```text
1. 変更概要
2. commit一覧
3. 変更ファイル一覧
4. build結果
5. CI結果
6. Validation Layer結果
7. runtime test matrix
8. developer timing log抜粋
9. 未検証項目
10. known limitation
```

Intel Arcがない場合は:

```text
Intel Vulkan runtime: NOT TESTED
Intel XeLL runtime: NOT TESTED
```

を明記。

---

# 85. 一次資料

## melonPrimeDS

Repository:

https://github.com/ag-advania/melonPrimeDS

Branch:

https://github.com/ag-advania/melonPrimeDS/tree/develop_remakeVulkan_ver2

基準commit:

https://github.com/ag-advania/melonPrimeDS/commit/ee8ed13474cec528831708b136ec4a30286877be

Clean-room contract:

https://github.com/ag-advania/melonPrimeDS/blob/develop_remakeVulkan_ver2/docs/plans/rendering/vulkan/clean-room-rewrite-contract.md

Vulkan backend documentation:

https://github.com/ag-advania/melonPrimeDS/blob/develop_remakeVulkan_ver2/docs/features/rendering/vulkan-backend.md

## Khronos Vulkan

VK_EXT_present_timing:

https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html

VkPresentTimingInfoEXT:

https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentTimingInfoEXT.html

VK_KHR_present_wait2:

https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_wait2.html

VK_KHR_present_id2:

https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_id2.html

VK_KHR_present_mode_fifo_latest_ready:

https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_mode_fifo_latest_ready.html

---

# 86. 最終指示

実装優先順位は:

```text
Correctness
↓
Fail-soft
↓
Late-input ordering
↓
Target-time correctness
↓
Frame pacing
↓
Latency
↓
Optimization
```

とする。

特に今回は、

> **「低遅延だから待機を減らす」ではなく、emulation rateを維持したうえで、正しいpresent timing feedbackを用いて表示時刻を制御する**

ことを目的とする。

`JustInTime`という名称だけを完成させるのではなく、

```text
timing lifecycle
time domain
feedback
target time
swapchain lifecycle
fallback
CI
runtime validation
```

まで一体として完成させること。
