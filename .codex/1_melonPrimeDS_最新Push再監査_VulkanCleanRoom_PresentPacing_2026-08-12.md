# melonPrimeDS 最新Push再監査
## Vulkan Clean-Room / Vendor-Neutral Present Pacing 重点監査

- 対象リポジトリ: `ag-advania/melonPrimeDS`
- 対象ブランチ: `develop_remakeVulkan_ver2`
- 監査基準HEAD: `ee8ed13474cec528831708b136ec4a30286877be`
- 最新コミット: `Fix Vulkan/DX12 issues`
- 監査日: 2026-08-12
- 比較基準: 前回監査HEAD `4e3f18a175d5aaf1def545873d3e91d319b592b7`
- 重点: Vulkan clean-room rewrite、vendor-neutral present pacing、F2/設定UI/device lifetime、低遅延APIとFPS limiter

---

# 1. 結論

今回のPushで、前回「未実装」としていたvendor-neutral Vulkan present pacingの基盤は**実際に実装された**。

追加された主要機能:

```text
VK_KHR_get_surface_capabilities2
VK_KHR_present_id2
VK_KHR_present_wait2
VK_KHR_calibrated_timestamps
VK_EXT_present_timing
VK_KHR_present_mode_fifo_latest_ready

VulkanPresentPacer
Vulkan pacing authority
surface-scoped capability probe
bounded present wait
present timing telemetry
FIFO_LATEST_READY experimental policy
swapchain recreation state reset
```

また次のコミットで、F2後に設定UIを開いた際の異種DX12 probe、`VK_ERROR_DEVICE_LOST`、Software fallback、Vulkanグレーアウト、Vulkan/DX12切替時のdevice lifetime、低遅延API使用中のFPS limiterが修正されている。

ただし今回の再監査で最も重要な点は以下。

> **現在の `JustInTime` / `JustInTimeFifoLatestReady` policyは、名前どおりの「target-timeを使った真のJIT presentation」にはまだなっていない。**

現在の`VkPresentTimingInfoEXT`は実質:

```text
flags      = 0
targetTime = 0
```

であり、`VK_EXT_present_timing`は現状:

```text
presentation timing telemetry
+
previous-present bounded wait
```

として使われている。

したがって現状を正確に分類すると:

```text
present_id2                 IMPLEMENTED
present_wait2               IMPLEMENTED
present timing telemetry    IMPLEMENTED
adaptive bounded wait       IMPLEMENTED
true target-time JIT        NOT IMPLEMENTED
FIFO_LATEST_READY selection IMPLEMENTED / EXPERIMENTAL
```

---

# 2. Sapphire由来という表現の訂正

前回監査で使用した「Sapphire由来Vulkan rasterizer」という表現は現行branchに対しては不正確。

現行Vulkan backendはrepository自身のdocumentで明確に:

```text
rewritten from scratch
clean-room rewrite
```

として定義されている。

現在のVulkan rasterizerの基礎は:

```text
GPU3D_Compute.cpp
GPU3D_Compute_shaders.h
```

であり、OpenGL Compute版のcompute rasterizerをVulkan-nativeに移植したもの。

旧Watermelon / Sapphire系Android Vulkan backendは、現在の実装へ継続利用する対象ではなく、**置換された旧実装**。

clean-room contractでは参照元も明示的に制限されている。

許可:

```text
Software Renderer
GPU3D_Compute
non-Vulkan melonPrimeDS integration
DX12のfunctional Definition of Done / frontend integration shape
Khronos Vulkan Specification / Vulkan-Headers
```

禁止:

```text
WatermelonDS
SapphireRhodonite/melonDS-android
SapphireRhodonite/melonDS-android-lib
旧Vulkan backend本体のcode/design/shader/history/comment
```

今後は**「現行Vulkan clean-room backend」**と呼ぶのが正確。

---

# 3. 今回追加された2コミット

前回監査HEADから最新HEADまで2コミット。

## Commit 1

```text
6fa1c99ae7c7102bb60ec3c752d10b95482bd56b
Add vendor-neutral Vulkan present pacing
```

主要内容:

```text
VulkanPresentPacer
VulkanModernPresentCompat
modern WSI capability
present_id2
present_wait2
present_timing
calibrated timestamps plumbing
FIFO_LATEST_READY
developer pacing policy
low-latency CI extension
```

## Commit 2

```text
ee8ed13474cec528831708b136ec4a30286877be
Fix Vulkan/DX12 issues
```

主要内容:

```text
Vulkan設定UI中の不要なDX12 probe抑止
Vulkan shared device probe race抑止
Windows Vulkan device lifetime安定化
DX12/Vulkan切替時device扱い修正
Microsoft Basic Render Driver除外
DX12 Reflex + DXGI double wait修正
Vulkan host 60FPS limiter復元
Intel XeLL default Off維持
```

---

# 4. `VulkanPresentPacer` の設計

追加:

```text
src/VulkanPresentPacer.h
src/VulkanPresentPacer.cpp
```

policy:

```cpp
TelemetryOnly = 0
PresentWait = 1
JustInTime = 2
JustInTimeFifoLatestReady = 3
```

authority:

```cpp
GenericHost
NvidiaReflex
AmdAntiLag2
GenericPresentTiming
```

責務はPresenter/WSI側へ閉じており、GPU3D rasterizerへpresentation timing policyを侵入させていない。

**設計位置: PASS**

---

# 5. Default policy

Config default:

```text
3D.Vulkan.PresentPacingPolicy = 0
```

つまり`TelemetryOnly`がdefault。

defaultでは:

```text
vkWaitForPresent2KHRによるbehavioral waitなし
FIFO_LATEST_READYへの変更なし
host limiter維持
```

新機能のruntime検証が十分でなくてもrelease経路へ挙動変更を持ち込まない。

**評価: PASS**

---

# 6. Device capability

`VulkanDevice::Create()`ではgeneric present pathをoptionalとしてprobeする。

```text
VK_KHR_get_surface_capabilities2
VK_KHR_present_id2
VkPhysicalDevicePresentId2FeaturesKHR
VK_KHR_present_wait2
VkPhysicalDevicePresentWait2FeaturesKHR
VK_KHR_calibrated_timestamps
VK_EXT_present_timing
VkPhysicalDevicePresentTimingFeaturesEXT
VK_KHR_present_mode_fifo_latest_ready
VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR
```

extension nameだけで有効化せずfeature bitまで確認している。

依存関係も:

```text
Present ID2
    requires caps2

Present Wait2
    requires Present ID2

Present Timing
    requires caps2
    requires Present ID2
    requires calibrated timestamps
    requires presentTiming feature

FIFO Latest Ready
    requires Present Timing
    requires extension
    requires feature
```

の形。

**評価: PASS**

---

# 7. Fail-soft device creation

optional low-latency extensions込みの`vkCreateDevice`が失敗した場合:

```text
optional extensions除去
↓
vkCreateDevice再試行
```

となる。

modern present extensionがdriverで壊れていることだけを理由にVulkan renderer全体を使用不能にしない。

既存Reflex / Anti-Lag 2と同じfailure containment思想。

**評価: PASS**

---

# 8. Surface capability

`VulkanPresentPacer::QuerySurfaceCapabilities()`では実`VkSurfaceKHR`に対して:

```text
VkSurfaceCapabilitiesPresentId2KHR
VkSurfaceCapabilitiesPresentWait2KHR
VkPresentTimingSurfaceCapabilitiesEXT
```

を`VkSurfaceCapabilities2KHR`へchain。

device-level availabilityだけではなく:

```text
PresentId2Surface
PresentWait2Surface
PresentTimingSurface
```

を別に持つ。

失敗時はlegacy `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`へfallback。

**評価: PASS**

---

# 9. Swapchain flags

surface supportに応じて:

```text
VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR
VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR
VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT
```

を付加。

未対応featureはflagを付けない。

**評価: PASS**

---

# 10. Present ID2 / Reflex coexistence

`PreparePresent()`で`VkPresentId2KHR`を`VkPresentInfoKHR::pNext`へ接続。

logical IDは:

```text
Reflex active
    → Reflex Frame ID

Reflex inactive
    → Generic LastSubmittedId + 1
```

を使う。

既存Reflexは:

```text
VkLatencySubmissionPresentIdNV
VkPresentIdKHR
```

を維持し、新generic pathは`VkPresentId2KHR`を使用。

Reflex active時は同じlogical IDを共有するため、旧`VK_KHR_present_id`を機械的に置換せず共存できている。

**評価: PASS**

---

# 11. Present Wait2

behavioral policy有効時:

```text
LastPresentedId
↓
vkWaitForPresent2KHR
↓
late input
```

となる。

timeout上限:

```text
2,000,000 ns = 2 ms
```

同一present IDの二重waitを防ぐguardあり。

`VK_ERROR_OUT_OF_DATE_KHR`ではswapchain rebuild、その他runtime errorではgeneric waitだけをdisableして`GenericHost`へfallback。

**評価: PASS / EXPERIMENTAL**

---

# 12. Input ordering

最新EmuThread:

```text
Host Hybrid FPS Limiter
↓
beginVulkanLowLatencyFrame()
    ↓
    Generic PresentWait2
    ↓
    Reflex sleep
    ↓
    Anti-Lag INPUT
↓
Reflex INPUT_SAMPLE
↓
inputRefreshJoystickState()
↓
RunFrameHook()
↓
SetKeyMask()
↓
Simulation
```

generic waitを使う場合でも:

> wait → fresh input

を維持。

late-input architectureを壊していない。

**評価: PASS**

---

# 13. Pacing authority

`SelectAuthority()`は:

```text
Reflex Active
    → NvidiaReflex

Anti-Lag 2 Active
    → AmdAntiLag2

normal speed
+ behavioral generic policy
+ PresentWait2 available
    → GenericPresentTiming

otherwise
    → GenericHost
```

となる。

generic WSI waitがReflex/Anti-Lagへ重ねられない点は良い。

---

# 14. Host limiterは別に残る

最新fixでVulkanの`ShouldBypassHostLimiter()`経路は削除された。

normal speed + LimitFPS ONではexperimental generic policyでも:

```text
Host Hybrid Limiter
↓
Generic PresentWait2
↓
fresh input
```

となり得る。

これは最新commitの「低遅延API使用中もホスト側60 FPS制限を維持」の意図どおり。

良い点:

```text
PresentWait2を60FPS capと誤認
↓
emulatorが無制限に進む
```

問題を防ぐ。

ただしコードコメントの「exactly one pacing authority」は、wait ownership全体を厳密には表さない。

より正確には:

```text
WSI低遅延authorityは1つ
emulation-rate limiterは別責務
```

と整理するべき。

---

# 15. 最大の監査結果: `JustInTime`はまだ真のJITではない

現在`PreparePresent()`は`VkPresentTimingInfoEXT`へtiming query情報を入れるが、target scheduling fieldsはゼロのまま。

コードコメントも:

```text
telemetry-only metadata:
no absolute/relative target is requested
```

と明示している。

Khronos Vulkan仕様では`VkPresentTimingInfoEXT::targetTime`が**非zero**の場合に、presentation engineが指定時刻へpresentを合わせようとする。

よって現在の`JustInTime`は実際には:

> **refresh durationを使ってPresentWait2のtimeoutを短くするadaptive bounded wait**

である。

---

# 16. 現在のpolicyの実態

## TelemetryOnly

```text
present timing telemetry
host limiter
behavioral waitなし
```

## PresentWait

```text
host limiter
+
previous presentを最大2ms待つ
```

## JustInTime

現状:

```text
host limiter
+
previous presentを
min(2ms, max(0.25ms, refreshDuration/4))
だけ待つ
```

**target-time JITではない。**

## JustInTimeFifoLatestReady

現状:

```text
上記adaptive wait
+
FIFO_LATEST_READY
```

であり、target present timeは指定しない。

---

# 17. 対応方針

2案ある。

## 案A: 名前を実態に合わせる

```text
TelemetryOnly
PresentWait
AdaptivePresentWait
AdaptivePresentWaitFifoLatestReady
```

## 案B: 名前どおり真のJITを実装する

こちらを推奨。

`VK_EXT_present_timing`本来のtarget schedulingを利用し:

```text
targetTime != 0
```

を指定する。

---

# 18. `RefreshTimingProperties()` lifecycle不足

現在:

```text
OnSwapchainCreated()
↓
RefreshTimingProperties()
```

を1回実行する。

しかしVulkan仕様上`vkGetSwapchainTimingPropertiesEXT`はswapchain作成直後に`VK_NOT_READY`を返してよい。platformによっては最低1回presentした後までrefresh timingが分からない。

その場合現コードでは:

```text
RefreshDurationNs = 0
RefreshIntervalNs = 0
```

のまま残る可能性がある。

---

# 19. Timing propertiesは動的に変化する

Vulkan仕様ではswapchain timing propertiesはruntime中に変化可能。

`vkGetPastPresentationTimingEXT`が返す:

```text
timingPropertiesCounter
```

によって変更を検知できる。

現在コードは`VkPastPresentationTimingPropertiesEXT`を取得しているが、`timingPropertiesCounter`を利用していない。

### 推奨修正

```cpp
u64 TimingPropertiesCounter = 0;
```

を保持し:

```text
new counter != stored counter
↓
RefreshTimingProperties()
```

する。

また`RefreshDurationNs == 0`なら、successful present後またはtiming result取得後に再試行する。

---

# 20. Time Domain plumbingはあるが未使用

Loaderには:

```text
vkGetSwapchainTimeDomainPropertiesEXT
vkGetCalibratedTimestampsKHR
```

が追加されている。

しかし`VulkanPresentPacer`ではtarget-time計算へ未接続。

`PresentTimingRelative`もcapabilityとして保存するだけ。

つまり:

> 真のJITに必要なAPI plumbingは途中まであるが、schedulerへ接続されていない。

---

# 21. 真のJITに必要な実装

最低限:

```text
present timing feedback
refresh timing
time domain
desired target presentation time
estimated CPU/GPU lead
safety margin
```

を持つ。

最初はsurfaceが`presentAtRelativeTimeSupported`を持つ場合に:

```text
VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT
```

を使う方式が比較的単純。

その後absolute schedulingを行うなら:

```text
vkGetSwapchainTimeDomainPropertiesEXT
↓
time domain選択
↓
timeDomainId保存
↓
必要ならvkGetCalibratedTimestampsKHR
↓
targetTime算出
↓
VkPresentTimingInfoEXT
```

へ進む。

`timeDomainsCounter`も追跡する。

---

# 22. FIFO_LATEST_READY

現行コードは:

```text
VSync ON
+
policy = JustInTimeFifoLatestReady
+
PresentTimingRuntimeEnabled
+
PresentId2Surface
+
LatestReadyDevice
```

で`VK_PRESENT_MODE_FIFO_LATEST_READY_KHR`を選ぶ。

extensionの機能としては正しい。

ただしKhronosはこのmodeをtime-based present APIとの組み合わせに有用と説明している。現状は`targetTime=0`なので、名前どおりのJIT schedulingとの組み合わせはまだ完成していない。

### 推奨

最終的にFIFO_LATEST_READYは:

```text
actual target-time scheduler ready
+
timing properties valid
+
time-domain policy valid
```

を条件にする。

---

# 23. Present Timing results queue

swapchain作成後:

```text
vkSetSwapchainPresentTimingQueueSizeEXT(..., 16)
```

を実行し、毎frame:

```text
vkGetPastPresentationTimingEXT
```

でqueueをdrain。

これは正しい。

`VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT`時にはtiming metadataを外してpresentを再試行する。Khronos proposalでもqueue-full時の選択肢として、results drain、queue拡大、timing dataなしでpresent再試行が示されている。

**評価: PASS**

---

# 24. F2 / Video Settings問題

最新commitではfeature probeがbackend依存になった。

Vulkan選択中はVulkan probe、DX12 probeは:

```cpp
if (dx12Renderer)
{
    DX12FeatureCheck::Probe();
}
```

に限定。

つまり:

```text
Vulkan実行中
↓
F2
↓
設定UI
↓
不要なDX12 device作成/probe
```

を避ける。

**修正意図とコードが一致。**

---

# 25. Vulkan probe retry

`VulkanFeatureCheck::ResetProbeForRetry()`はlive shared Vulkan deviceが存在する場合はprobeをやり直さない。

動作中のlogical device自体がstatic physical-device probeより強い成功証拠であり、renderer transition付近で余計なprobeをdriverへ入れない。

**評価: PASS**

---

# 26. Windows Vulkan device lifetime

Windowsでは`ProcessLifetimeDevice()`にshared referenceを保持し、backend switch中に最後のfrontend Vulkan viewが消えても`vkDestroyDevice`を即実行しない。

目的はVulkan→other backend transition中にdriver / injected graphics layer callbackが残る可能性への防御。

今回の`VK_ERROR_DEVICE_LOST`対策と整合する。

---

# 27. Clean-room文書上の小さな懸念

`VulkanDevice.cpp`のコメントには現在:

```text
Match the proven lifetime rule used by the previous Vulkan backend
```

という表現がある。

これは現行rasterizerが旧backend由来という意味ではない。

ただしclean-room contractでは旧Vulkan backendのdesign/history/commentsまでreference禁止としているため、文書上の整合性を厳密にするならこの表現は避けた方がよい。

推奨例:

```text
On Windows, backend switching can leave driver or injected-layer callbacks
alive beyond the last frontend Vulkan view. Retain one process-lifetime
device reference so vkDestroyDevice is not executed during that transition.
```

**優先度: P3 / documentation-cleanliness**

---

# 28. NVIDIA Reflex / AMD Anti-Lag 2

既存Vulkan Reflex:

```text
VK_NV_low_latency2
timeline semaphore
vkLatencySleepNV
INPUT_SAMPLE
SIMULATION_START/END
RENDERSUBMIT_START/END
PRESENT_START/END
VkLatencySubmissionPresentIdNV
VkPresentIdKHR
```

は維持。

AMD Anti-Lag 2も:

```text
VK_AMD_anti_lag
INPUT
PRESENT
same frame index
```

を維持。

新generic pacingはそれらがactiveな時にbehavioral wait authorityを取らない。

**静的回帰なし。**

---

# 29. Host 60 FPS limiter復元の評価

今回の修正でVulkanはhost limiterを維持する。

現時点では妥当。

generic present pacerはまだ:

```text
DSのTargetFPSを完全に所有していない
targetTimeを設定していない
```

ため、PresentWait2へFPS capまで任せるとemulation speedをpresentation engineへ依存させる危険がある。

> **真のtarget-time JIT + cap ownershipが完成するまではHost limiterを残す**

のが安全。

ただしexperimental policyではHost limiter + PresentWait2の二段waitになるため、input latency / P95 frame time / timeout countをA/B測定する。

---

# 30. 現状の機能matrix

| 機能 | 状態 | コメント |
|---|---|---|
| Vulkan clean-room rewrite | **CONFIRMED** | 旧Sapphire backend置換済み |
| NVIDIA Reflex | **IMPLEMENTED** | vendor path維持 |
| AMD Anti-Lag 2 | **IMPLEMENTED** | vendor path維持 |
| `VK_KHR_present_id2` | **IMPLEMENTED** | device + surface + present |
| `VK_KHR_present_wait2` | **IMPLEMENTED** | bounded wait |
| `VK_EXT_present_timing` telemetry | **IMPLEMENTED** | queue + feedback |
| Timing results queue drain | **IMPLEMENTED** | 毎frame |
| Queue-full fallback | **IMPLEMENTED** | timing metadataを外してretry |
| `VK_KHR_present_mode_fifo_latest_ready` | **IMPLEMENTED** | experimental |
| Pacing authority | **IMPLEMENTED** | vendor > generic |
| true target-time JIT | **NOT IMPLEMENTED** | `targetTime=0` |
| time-domain selection | **NOT IMPLEMENTED** | dispatchのみ |
| calibrated timestamp use | **NOT IMPLEMENTED** | dispatchのみ |
| timing property counter tracking | **NOT IMPLEMENTED** | counter未使用 |
| initial `VK_NOT_READY` retry | **NOT IMPLEMENTED** | 要追加 |
| default behavioral generic pacing | **OFF** | TelemetryOnly |
| Host 60 FPS limiter | **ACTIVE** | 最新commitで復元 |
| Intel Vulkan実機検証 | **未確認** | Intel Arc環境なし |
| Linux generic WSI runtime | **未確認** | 要実機 |
| MoltenVK generic WSI runtime | **未確認** | 要実機 |

---

# 31. 優先順位

## P0 — timing properties lifecycle修正

実装:

```text
TimingPropertiesCounter保存
timeDomainsCounter保存
VK_NOT_READY後再試行
counter変化時re-query
```

## P0 — `JustInTime`を本物にするかrename

推奨は本物のtarget schedulingへ進めること。

```text
targetTime != 0
```

を使う。

## P1 — time domain

```text
vkGetSwapchainTimeDomainPropertiesEXT
relative target scheduling
absolute/calibrated timestampは次段階
```

## P1 — FIFO Latest Ready gate強化

actual target-time schedulerが成立した場合だけbehavioral JIT policyとして選択。

## P1 — CI追加

```text
JustInTime → targetTime nonzero path必須
timingPropertiesCounter tracking必須
timeDomainsCounter tracking必須
initial VK_NOT_READY retry必須
default TelemetryOnly → waitなし保証
default TelemetryOnly → FIFO_LATEST_READYなし保証
swapchain recreation → timing/time-domain model reset保証
```

---

# 32. Intel Arcがない場合

XeLLはIntel Arc runtime依存のため実機なしでは最終runtime validation不可。

一方、vendor-neutral Vulkan WSI実装はIntel専用ではないため、NVIDIA等でもAPI path自体を検証可能。

ただし:

```text
Intel Arc + Vulkanで遅延改善した
```

とはIntel実機入手まで表現しない。

Intel XeLLは今回もdefault Offを維持。

```text
implementation        完了
static/Fake tests     完了
negative path         完了
hardware runbook      完了
Intel Arc runtime     未検証
latency improvement   未検証
```

---

# 33. 推奨コミット分割

```text
1. Vulkan: refresh present timing properties after first present and counter changes
2. Vulkan: track present timing time-domain changes
3. Vulkan: implement target-time JIT presentation
4. Vulkan: gate FIFO_LATEST_READY on active target-time scheduler
5. Vulkan: clarify emulator-rate limiter and WSI pacing ownership
6. CI: enforce Vulkan target-time pacing contract
7. Docs: remove ambiguous previous-backend clean-room wording
```

---

# 34. 今回Pushの最終評価

## Clean-room Vulkan

**PASS**

現行VulkanはSapphire由来rasterizerではない。旧third-party Vulkan backendを置換したclean-room implementation。

## Vendor-neutral capability plumbing

**PASS**

前回未実装だったmodern present extensionsは実装済み。

## Present Wait2

**PASS / EXPERIMENTAL**

bounded・surface-scoped・fail-soft。default Off。

## Present Timing telemetry

**PASS**

queue、drain、feedback、queue-full処理まで実装。

## JustInTime

**PARTIAL**

現在はadaptive previous-present wait。target-time JITではない。

## FIFO Latest Ready

**IMPLEMENTED / EXPERIMENTAL**

true target-time schedulerとの接続は未完成。

## F2 / settings device-loss fix

**静的には妥当**

不要なcross-backend DX12 probeを抑止し、live shared Vulkan device中のprobe retryも防ぐ。

## Host FPS limiter

**今回の復元は妥当**

真のJIT/cap ownershipがない段階で、generic present waitへ60 FPS制御まで任せない。

---

# 35. 最終結論

今回のPushによってVulkan低遅延architectureは:

```text
NVIDIA
    → Reflex

AMD
    → Anti-Lag 2

その他 / vendor feature Off
    → Generic Present ID2
    → Present Timing telemetry
    → optional Present Wait2
    → optional FIFO Latest Ready

fallback
    → Host pacing
```

まで進んだ。

前回監査時点の最大の未実装領域だった:

```text
present_id2
present_wait2
present_timing
FIFO_LATEST_READY
```

は**もう未実装ではない**。

次の焦点はextensionを追加することではなく:

> **収集できるpresentation timingを実際にtarget-time schedulingへ利用し、`JustInTime`を本物のJITに完成させること。**

その際も:

```text
DS 60 FPS emulation-rate control
```

と:

```text
Vulkan presentation scheduling
```

を混同しないことが重要。

---

# 36. 一次資料

## melonPrimeDS

Repository:
https://github.com/ag-advania/melonPrimeDS

Branch:
https://github.com/ag-advania/melonPrimeDS/tree/develop_remakeVulkan_ver2

Latest audited commit:
https://github.com/ag-advania/melonPrimeDS/commit/ee8ed13474cec528831708b136ec4a30286877be

Vendor-neutral pacing commit:
https://github.com/ag-advania/melonPrimeDS/commit/6fa1c99ae7c7102bb60ec3c752d10b95482bd56b

Clean-room contract:
https://github.com/ag-advania/melonPrimeDS/blob/develop_remakeVulkan_ver2/docs/plans/rendering/vulkan/clean-room-rewrite-contract.md

Vulkan backend documentation:
https://github.com/ag-advania/melonPrimeDS/blob/develop_remakeVulkan_ver2/docs/features/rendering/vulkan-backend.md

## Khronos Vulkan

VK_EXT_present_timing:
https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_present_timing.html

VkPresentTimingInfoEXT:
https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentTimingInfoEXT.html

vkGetSwapchainTimingPropertiesEXT:
https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimingPropertiesEXT.html

VK_KHR_present_wait2:
https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_wait2.html

VK_KHR_present_id2:
https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_id2.html

VK_KHR_present_mode_fifo_latest_ready:
https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_mode_fifo_latest_ready.html
