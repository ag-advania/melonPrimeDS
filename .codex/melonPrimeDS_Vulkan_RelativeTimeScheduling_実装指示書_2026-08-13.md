# melonPrimeDS Vulkan
# Relative-Time Scheduling 実装指示書
## `presentAtAbsoluteTimeSupported = false` 環境で Target-Time JIT を成立させる

- 対象リポジトリ: `ag-advania/melonPrimeDS`
- 対象ブランチ: `develop_remakeVulkan_ver2`
- 作成時HEAD: `3da6d5a7d1bf139c293b7be30320aadbd782bba7`
- 作成日: 2026-08-13
- 対象backend: **現行Vulkan clean-room backend**
- 優先度: **P1 / Phase 3 A/B blocker解除**
- **実施状況: 完了 (2026-08-13) — 本文は作成時点のまま、以下は追記**
- 主対象:
  - `src/VulkanPresentPacingPolicy.h`
  - `src/VulkanPresentTimingModel.h`
  - `src/VulkanPresentPacer.h`
  - `src/VulkanPresentPacer.cpp`
  - `src/VulkanDevice.cpp`
  - `tools/testing/vulkan-present-timing-tests.cpp`
  - `tools/ci/audits/audit-low-latency-contract.py`
  - `docs/features/rendering/vulkan-backend.md`
  - `docs/development/testing/vulkan-present-pacing-runbook.md`

---

# 0. 実施結果 — DONE (2026-08-13)

## §81 DoD — Static

```markdown
- [x] RelativeTimingDeviceをresolverへ接続
- [x] RelativeTimingSurfaceをresolverへ接続
- [x] TargetMode None/Absolute/Relative
- [x] Absolute優先 (SelectVulkanTargetSchedulingMode)
- [x] Relative fallback
- [x] relative duration計算をabsolute timestamp計算と分離
      (EvaluateAbsoluteTargetTime / EvaluateRelativeTargetDuration)
- [x] FRR refresh quantization (VulkanRelativeCadence, 端数アキュムレータ)
- [x] VRR path (refreshInterval == UINT64_MAX → max(frameInterval, refreshDuration))
- [x] unknown refresh path (refreshInterval == 0 → raw frame interval)
- [x] retry/commit/abandon transactional cadence
- [x] FIFO_LATEST_READY relative gate
- [x] vendor authority regressionなし
- [x] host limiter regressionなし
- [x] pure tests PASS (25群、うち relative 10群を新規追加)
- [x] CI audits PASS
- [x] release build PASS
- [x] debug build PASS
```

## §82 DoD — Runtime (RTX 5070 Ti / driver 610.74.0.0 / loader 1.4.357)

```markdown
- [x] Validation layer enabled
- [x] Core Validation ERROR 0
- [x] Sync Validation hazard 0 (policy 2 relative / 3 relative / Reflex on)
- [x] Policy 2 targetMode=relative
- [x] Policy 2 targetTime>0 after bootstrap
- [x] Policy 3 FIFO_LATEST_READY selected
- [x] Reflex On → generic target off (authority=NvidiaReflex, targetMode=none)
- [x] VSync Off → relative target off (IMMEDIATE, fallback=present mode is not FIFO)
- [x] no DEVICE_LOST
- [x] no Software fallback
- [x] no recreate storm
```

## §83 DoD — Phase 3 unblock

```text
policy=JustInTime authority=GenericPresentTiming state=TargetSchedulingActive
targetMode=relative absoluteSupported=no relativeSupported=yes fallback=none

policy=JustInTimeFifoLatestReady → selected-present-mode=FIFO_LATEST_READY
```

を実機で確認。**Phase 3 A/B = UNBLOCKED**。

## 実測されたcadence (§18-19の検証)

```text
jit=active lastTarget=14885600 frameIntervalNs=16666667
refreshIntervalNs=1859000 dynamics=FRR queueFull=0 fallback=none
```

`14,885,600 = 8 × 1,860,700` でrefresh intervalの整数倍。
16,666,667 / 1,860,700 = 8.957 なので端数 1,781,067ns/frame を
アキュムレータが配分し、約11フレームに1回だけ9 quantaになる。
固定round(8.957)=9 なら恒常的に3.8%遅く、切り捨て8なら10.7%速い。
指示書が禁じた固定roundを避けられていることの実測証拠。

## 副次的に解消した点

前回セッションで「§16の期待値 `present mode is not FIFO` が観測できない」と
記録していた件は解消。absolute非対応がもはや先行blockerでなくなったため、
VSync OFF時に本来の非FIFO理由が表に出るようになった。

## 証拠

```text
docs/archive/audits/vulkan/2026-08-13-relative-scheduling/
```

## 未実施 (§75「実装しただけでlatency improvedと書かない」を遵守)

```text
Phase 1 event matrix (fullscreen/resize/DPI/F2/renderer切替/速度モード)
Phase 3 A/B 全mode
click-to-photon
AMD / Intel / MoltenVK / Linux / macOS
```

default は §76 のとおり `TelemetryOnly` のまま変更していない。

---

# 1. 背景

NVIDIA実機Validationで以下を確認済み。

```text
GPU                   RTX 5070 Ti
NVIDIA driver         610.74.0.0
Vulkan loader         1.4.357

device:
  presentTiming                 yes
  presentAtAbsoluteTime         yes
  presentAtRelativeTime         yes
  FIFO_LATEST_READY             yes

surface:
  presentTimingSupported         yes
  presentAtAbsoluteTimeSupported false
  presentAtRelativeTimeSupported yes
```

現行schedulerはabsolute target-timeだけをbehavioral JITとして認めている。

そのため:

```text
Policy = JustInTime
↓
PresentTimingAbsoluteSurface = false
↓
targetScheduling = off
↓
fallback = absolute timing unsupported by surface
```

となる。

現在のNVIDIA実機では:

```text
A1 PresentWait
A2 JustInTime
A3 JustInTimeFifoLatestReady
```

のA2/A3がtarget-time schedulingを実際には使用できない。

したがってPhase 3 A/Bを行っても:

```text
PresentWait
vs
Target-Time JIT
```

を比較できない。

---

# 2. 今回の目的

`VK_EXT_present_timing`の:

```text
presentAtRelativeTime
VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT
```

を実装し、

```text
Absolute scheduling supported
    → Absoluteを使用

Absolute unsupported
Relative supported
    → Relativeへfallback

両方unsupported
    → PresentWait / GenericHostへfail-soft
```

とする。

最終的に上記NVIDIA実機で:

```text
Policy = JustInTime
authority = GenericPresentTiming
targetScheduling = active
targetMode = relative
targetTime != 0
```

を観測可能にし、Phase 3 A/Bのblockerを解除する。

---

# 3. 重要: relative-timeはabsolute-timeと意味が違う

relative-timeを:

```text
absolute target timestampの代替clock
```

として扱ってはいけない。

Khronosの定義ではrelative targetは:

> 前回presentationの `IMAGE_FIRST_PIXEL_VISIBLE` から、前の画像を最低何ns表示しておくか

という**duration**。

つまり:

```text
Absolute:
    targetTime = clock上の時刻

Relative:
    targetTime = 前画像のminimum visible duration
```

。

同じ`ComputeTargetTime()`をflagだけ変えて使ってはいけない。

---

# 4. Vulkan仕様上の必須条件

relative targetをnon-zeroで送る場合:

```text
VkPhysicalDevicePresentTimingFeaturesEXT::presentAtRelativeTime == VK_TRUE
VkPresentTimingSurfaceCapabilitiesEXT::presentAtRelativeTimeSupported == VK_TRUE
flags contains VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT
```

が必要。

またnon-zero targetはFIFO familyだけ。

```text
FIFO
FIFO_RELAXED
FIFO_LATEST_READY
```

。

IMMEDIATE / MAILBOXではtarget schedulingしない。

---

# 5. `timeDomainId` VUIDを絶対に再発させない

今回の実機Validationで既に:

```text
VUID-VkPresentTimingInfoEXT-timeDomainId-12400
```

を発見・修正済み。

重要な契約:

```text
VkPresentTimingInfoEXTをattachするなら
targetTime == 0でも
timeDomainIdはvkGetSwapchainTimeDomainPropertiesEXTが返したID
```

。

relative targetでもこの契約を崩さない。

したがって:

```text
TimeDomainsReady == false
```

なら、現行と同様にtiming metadata自体をattachしない。

relativeだから`timeDomainId=0`でよい、という実装は禁止。

---

# 6. 現行コードで既にできていること

`VulkanDevice.cpp`は`VkPhysicalDevicePresentTimingFeaturesEXT`から:

```text
PresentTiming
PresentAtAbsoluteTime
PresentAtRelativeTime
```

を保存済み。

`presentTimingFeatures`自体もsupported feature valuesを保持した状態で
`vkCreateDevice`のfeature chainへ入るため、driverがrelativeをadvertiseしている場合、
relative featureは既にdevice creation時にenableされる構造。

従って今回:

> 新しいextensionを追加する必要はない。

また`VulkanModernPresentCompat.h`には既に:

```text
VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT
presentAtRelativeTime
presentAtRelativeTimeSupported
```

が定義済み。

compat headerを不用意に変更しない。

---

# 7. 現行コードで足りない部分

現在`VulkanPresentPacer::Initialize()`は:

```text
AbsoluteTimingDevice
```

しかscheduler capabilityとして作っていない。

surfaceは既に:

```text
PresentTimingRelative
PresentTimingAbsoluteSurface
```

を保持している。

しかし`BuildCapabilities()`ではrelativeをresolverへ渡していない。

さらに`ClassifyVulkanTargetFallback()`は:

```text
AbsoluteTimingDevice
AbsoluteTimingSurface
```

を必須条件としている。

ここが主blocker。

---

# 8. 設計原則

booleanを増やすだけで:

```text
if absolute
else if relative
```

を`PreparePresent()`の各所へ散らさない。

pure policy layerで最初にmodeを1つ決める。

追加推奨:

```cpp
enum class VulkanTargetSchedulingMode : int
{
    None = 0,
    Absolute,
    Relative,
};
```

。

1 frameについて:

```text
Pacing Authority
Target Scheduling Mode
Bounded Wait
Fallback Reason
```

を一度だけresolveする。

---

# 9. Mode優先順位

必ず:

```text
Absolute
↓
Relative
↓
None
```

。

理由:

absolute pathは既にfeedback-based rebaseを持ち、

```text
TargetFrameInterval
presentation sequence
feedback stage timestamp
```

を直接用いてアプリ側cadenceを表現できる。

relativeはsurfaceがabsoluteを提供しない場合のfallback。

absolute対応環境までrelativeへ切り替えない。

---

# 10. `VulkanPacingCapabilities`変更

追加:

```cpp
bool RelativeTimingDevice = false;
bool RelativeTimingSurface = false;
```

。

既存:

```cpp
bool AbsoluteTimingDevice;
bool AbsoluteTimingSurface;
```

は維持。

名称の曖昧さを減らすならsurface member:

```text
PresentTimingRelative
```

を:

```text
PresentTimingRelativeSurface
```

へrenameしてもよい。

ただしrenameは機能commitと混ぜすぎない。

---

# 11. Device capability

`VulkanPresentPacer::Initialize()`:

現在:

```cpp
AbsoluteTimingDevice = PresentTimingDevice
    && TimeDomainQueryAvailable
    && device.GetPresentTimingFeatures().PresentAtAbsoluteTime;
```

。

追加:

```cpp
RelativeTimingDevice = PresentTimingDevice
    && TimeDomainQueryAvailable
    && device.GetPresentTimingFeatures().PresentAtRelativeTime;
```

。

`TimeDomainQueryAvailable`をrelativeにも残す理由:

```text
target durationそのものはclock-domainを必要としない
```

が、

```text
VkPresentTimingInfoEXT::timeDomainId
```

はmetadataをattachする以上、有効なIDでなければならないから。

---

# 12. Surface capability

現行:

```cpp
PresentTimingRelative =
    PresentTimingSurface
    && timing.presentAtRelativeTimeSupported == VK_TRUE;
```

をresolverへ接続する。

実機でここは:

```text
true
```

であることが確認済み。

---

# 13. `VulkanPacingDecision`拡張

現行:

```cpp
bool TargetTimeScheduling;
```

だけでは:

```text
absolute
relative
```

を区別できない。

推奨:

```cpp
VulkanTargetSchedulingMode TargetMode =
    VulkanTargetSchedulingMode::None;
```

を追加。

`TargetTimeScheduling`を残すなら:

```cpp
TargetTimeScheduling =
    TargetMode != VulkanTargetSchedulingMode::None;
```

というderived semanticsにする。

booleanとmodeが矛盾しない設計にすること。

---

# 14. Resolver

pure function内でmodeを決める。

概念:

```cpp
if (absolute device && absolute surface)
    mode = Absolute;
else if (relative device && relative surface)
    mode = Relative;
else
    mode = None;
```

ただしこれより前に:

```text
vendor authority
normalSpeed
TelemetryOnly
PresentWait-only
PresentId2
PresentTiming metadata
FIFO family
frame interval
timing lifecycle
time domain
target stage
```

のgateがある。

---

# 15. Fallback reason再設計

現行:

```text
AbsoluteTimingUnsupportedDevice
AbsoluteTimingUnsupportedSurface
```

をそのままtarget全体のblockerとして使うとrelative fallback後のlogが不正確になる。

最低限:

```text
absolute unsupported
+
relative available
```

は`Reason=None`でなければならない。

推奨diagnostic:

```cpp
NoTargetTimingModeDevice,
NoTargetTimingModeSurface,
```

あるいは:

```text
Absolute unsupported, using Relative
```

をfallback reasonではなくmode logとして表示。

重要:

> relativeに正常fallbackしたことを「fallback error」として扱わない。

---

# 16. Developer log

最低:

```text
targetMode=absolute
targetMode=relative
targetMode=none
```

を表示。

現在実機で必要な証拠:

```text
presentAtAbsoluteTimeSupported=no
presentAtRelativeTimeSupported=yes
targetMode=relative
targetScheduling=active
```

。

---

# 17. `ShouldUseFifoLatestReady()`修正

現行gateは:

```text
PresentTimingAbsoluteSurface
&& AbsoluteTimingDevice
```

を必須にしている。

relative実装後は:

```text
Absolute mode can become active
OR
Relative mode can become active
```

に変更。

つまり:

```text
Policy == JustInTimeFifoLatestReady
TargetSchedulingLifecycleFailed == false
PresentTimingSurface
PresentId2Surface
LatestReadyDevice
timing queue API available
(any supported target scheduling mode)
```

。

これにより現在のRTX 5070 Ti環境でもA3が到達可能になる。

---

# 18. Relative schedulerの計算方式

relative `targetTime`へ単純に:

```cpp
TargetFrameIntervalNs
```

を毎回入れるだけではFRRで不十分。

KhronosはFixed Refresh Rate時のIPDについて:

```text
refreshIntervalの整数倍
```

を推奨している。

例:

```text
60 FPS app
144 Hz display

app interval      ≈ 16.6667 ms
refresh interval  ≈ 6.9444 ms
ratio             = 2.4 refreshes/frame
```

。

毎回2 refresh:

```text
13.8889 ms
```

では速すぎる。

毎回3 refresh:

```text
20.8333 ms
```

では遅すぎる。

---

# 19. FRR/Dynamic Refreshの推奨方式

**error diffusion / fractional refresh accumulator**を使う。

144Hz / 60fpsなら:

```text
2, 2, 3, 2, 3 refreshes
```

のように配分し、

```text
5 frames
= 12 refresh cycles
≈ 83.333 ms
```

として平均16.6667msを維持。

単純round固定:

```text
round(2.4) = 2
```

は禁止。

---

# 20. Relative quantizerの状態

pure modelへ以下相当を追加。

```cpp
struct RelativeCadenceState
{
    u64 RefreshQuantumNs = 0;
    u64 FractionAccumulatorNs = 0;

    u64 PendingAccumulatorNs = 0;
    bool Pending = false;
};
```

。

ただし実装形は既存`VulkanPresentTimingModel`へ統合してよい。

重要なのは:

```text
PreparePresent
→ pending relative cadence state

present accepted
→ commit

present rejected
→ abandon
```

。

既存のpresentation sequenceと同じtransactional modelにする。

---

# 21. Retryでcadenceを二重消費しない

`VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT`では:

```text
same image
same present ID
same presentation sequence
```

でtiming metadataなしにretryする。

relative quantizerも:

```text
同じpending cadence step
```

を二重消費してはいけない。

retry accepted時:

```text
1回だけcommit
```

。

---

# 22. Relative duration計算

finite `RefreshIntervalNs`時:

```cpp
base = TargetFrameIntervalNs / RefreshIntervalNs;
remainder = TargetFrameIntervalNs % RefreshIntervalNs;

quanta = base;
pendingAccumulator = committedAccumulator + remainder;

if (pendingAccumulator >= RefreshIntervalNs)
{
    ++quanta;
    pendingAccumulator -= RefreshIntervalNs;
}
```

。

そして:

```cpp
quanta = max<u64>(1, quanta);
relativeDurationNs = quanta * RefreshIntervalNs;
```

。

overflow guard必須。

---

# 23. 表示refreshがemulatorより遅い場合

例:

```text
DS target = 60fps
display = 50Hz
```

では:

```text
TargetFrameInterval < RefreshInterval
base=0
```

になり得る。

この場合relative targetで0を送ってはいけない。

`targetTime=0`は:

```text
target schedulingなし
```

という特殊値だから。

最低:

```text
quanta = 1
```

。

つまりpresentation engineへ追加holdを要求せず:

```text
次に可能なrefresh
```

を使わせる。

このケースでは60fpsの全frameをphysical displayへ出すことは不可能なので、
relative schedulerが表示能力を超えてcadenceを作ろうとしない。

---

# 24. VRR

`RefreshIntervalNs == UINT64_MAX`:

```text
VRR
```

。

この場合fixed refresh quantumへ丸めない。

推奨:

```cpp
relativeDurationNs =
    max(TargetFrameIntervalNs, RefreshDurationNs);
```

。

`RefreshDurationNs`はVRR時のminimum refresh cycle duration。

flags:

```text
RELATIVE_TIME
```

を基本とする。

VRRでは固定gridを前提とするmanual quantizationをしない。

---

# 25. `RefreshIntervalNs == 0`

refresh granularity不明。

relative feature自体がsurface/deviceでsupportedなら:

```cpp
relativeDurationNs = TargetFrameIntervalNs;
```

を使用可能。

ただしdeveloper logに:

```text
relativeCadence=raw-frame-interval
refreshInterval=unknown
```

を出す。

これは推論でrefresh quantumを捏造するより安全。

---

# 26. `NEAREST_REFRESH_CYCLE`

finite refresh intervalで、quantized durationがrefresh quantumの整数倍なら:

```text
RELATIVE_TIME
|
NEAREST_REFRESH_CYCLE
```

を推奨。

目的:

```text
小さなprecision errorで1 refresh遅れる
```

のを避ける。

VRR / refresh interval unknownでは:

```text
RELATIVE_TIME
```

のみを基本とする。

---

# 27. Relative first present

Vulkan仕様上、swapchainで一度もpresentされていない場合:

```text
relative targetTimeはignored
```

。

実装では曖昧なactive表示を避けるため、推奨:

```text
first accepted present
    → targetTime = 0

second present onward
    → relative targetTime != 0
```

。

これによりlog:

```text
bootstrap
↓
relative active
```

が明確になる。

---

# 28. Relative bootstrap reason

既存:

```text
BootstrapWaitingForFeedback
```

はabsolute専用の意味。

relativeはfeedback baselineを必要としない。

追加推奨:

```text
BootstrapWaitingForFirstPresent
```

。

absolute:

```text
feedback待ち
```

。

relative:

```text
previous present成立待ち
```

。

を区別。

---

# 29. Feedback baseline

relative modeでも:

```text
vkGetPastPresentationTimingEXT
```

は維持。

理由:

```text
timing properties counter
time domain counter
queue lifecycle
A/B telemetry
actual present-stage timing
```

が必要。

ただしrelative schedulingのtarget duration計算自体を:

```text
absolute BaselineStageTimeNs
```

へ依存させない。

---

# 30. Domain mismatch

relative duration自体はclock domainに依存しないが、

```text
timeDomainId
timing feedback
```

は依存する。

現行:

```text
feedback domain mismatch
→ target scheduling off
→ RefreshTimeDomains()
```

の安全側挙動を維持してよい。

「relativeだからdomain mismatchを無視する」は禁止。

VUID再発防止を優先。

---

# 31. Timing properties change

`timingPropertiesCounter`変化で:

```text
RefreshDurationNs
RefreshIntervalNs
```

が変わる。

relative quantizerは古いrefresh quantumを持ち越さない。

変更検出時:

```text
Relative cadence accumulator reset
```

。

absolute baselineは現在の既存方針に従う。

---

# 32. Mode transition

以下でrelative accumulatorをreset:

```text
swapchain recreation
Absolute → Relative
Relative → Absolute
Generic → Reflex
Reflex → Generic Relative
Fast Forward → Normal
Slow Motion → Normal
TargetFPS change
RefreshInterval change
```

。

古いfraction phaseを別cadenceへ持ち込まない。

---

# 33. TargetFPS change

relative durationのsource of truthは:

```text
TargetFrameIntervalNs
```

。

display refreshからemulator FPSを逆算しない。

TargetFPSが変更されたら:

```text
relative fractional accumulator reset
```

。

---

# 34. `EvaluateTargetTime()`をそのまま拡張しない

現在の:

```cpp
u64 EvaluateTargetTime(u64 sequence)
```

はabsolute timestampを返す関数。

relative durationまで同じ名前・戻り値へ押し込むと意味が崩れる。

推奨:

```cpp
struct VulkanTargetTimingRequest
{
    VulkanTargetSchedulingMode Mode = None;
    u64 ValueNs = 0;
};
```

あるいは:

```cpp
EvaluateAbsoluteTargetTime()
EvaluateRelativeTargetDuration()
```

へ分離。

---

# 35. Present metadata

debug/capture用に:

```text
TargetMode
TargetValueNs
```

を保持。

既存:

```text
TargetTimeNs
```

はabsolute/relativeで意味が変わるため、可能なら:

```text
TargetValueNs
```

へrename。

ただしCSV互換性を壊す場合:

```text
target_time_ns
target_mode
```

として、relative時は「duration」であることをrunbookに明記する。

---

# 36. `PreparePresent()` Absolute

absolute branchは現在の動作を維持:

```text
flags =
    NEAREST_REFRESH_CYCLE

targetTime =
    absolute target timestamp

timeDomainId =
    TargetTimeDomainId
```

。

feedback baselineとpresentation sequenceを変更しない。

---

# 37. `PreparePresent()` Relative

relative branch:

```cpp
metadata.Timing.flags =
    VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT;
```

finite refresh quantized case:

```cpp
metadata.Timing.flags |=
    VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT;
```

。

```cpp
metadata.Timing.targetTime =
    relativeDurationNs;
```

。

絶対timestampを入れない。

---

# 38. `targetTimeDomainPresentStage`

`timeDomainId`が:

```text
VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT
```

なら、targetTime non-zero時:

```text
targetTimeDomainPresentStage
```

へsingle valid stageを入れる現行処理を維持。

relative flagが付いていても、このfieldを雑に0へしない。

---

# 39. TelemetryOnly

Policy 0:

```text
TargetMode = None
targetTime = 0
valid timeDomainId
presentStageQueries != 0
```

。

今回修正済みVUIDを守る。

relative実装のためにTelemetryOnlyのbehaviorを変えない。

---

# 40. PresentWait

Policy 1:

```text
BoundedPresentWait = true if available
TargetMode = None
targetTime = 0
```

。

relativeを勝手に有効化しない。

---

# 41. JustInTime

Policy 2:

```text
Absolute supported
    → Absolute

else Relative supported
    → Relative

else
    → no target
```

。

bounded waitは従来どおり独立。

---

# 42. JustInTimeFifoLatestReady

Policy 3:

```text
Absolute OR Relative target scheduler can become active
+
FIFO_LATEST_READY supported
```

ならswapchain mode候補。

現在のRTX 5070 Ti実機で:

```text
Relative
+
FIFO_LATEST_READY
```

へ到達できることが今回の主要DoD。

---

# 43. NVIDIA Reflex

Reflex active時:

```text
TargetMode = None
BoundedPresentWait = false
Authority = NvidiaReflex
```

を維持。

relative targetをReflexの下へ重ねない。

---

# 44. AMD Anti-Lag 2

同様:

```text
TargetMode = None
BoundedPresentWait = false
Authority = AmdAntiLag2
```

。

今回NVIDIA向けblocker解除でもvendor-neutral resolverを壊さない。

---

# 45. Host FPS limiter

絶対に削除・bypassしない。

役割:

```text
Host limiter
    emulator rate

PresentWait2
    previous-present bounded wait

Absolute/Relative Present Timing
    display scheduling
```

。

relative schedulingを60fps limiterとして扱わない。

---

# 46. Queue-full recovery

既存:

```text
timing metadata pause
queue grow
bounded recovery
retry same present without timing
```

を維持。

relative mode追加で別queueを作らない。

---

# 47. OutstandingTimedPresents

実機Validation後に追加された:

```text
OutstandingTimedPresents
```

を維持。

relative modeでもtiming metadata付きaccepted presentは同じ1 outstanding resultとして扱う。

---

# 48. Pure tests — capability matrix

追加必須:

## Absolute available

```text
Absolute device=yes
Absolute surface=yes
Relative=yes
→ TargetMode=Absolute
```

## Absolute surface unavailable / Relative available

```text
Absolute device=yes
Absolute surface=no
Relative device=yes
Relative surface=yes
→ TargetMode=Relative
→ target scheduling=true
```

**今回のRTX 5070 Ti実機を再現する最重要test。**

---

# 49. Pure tests — relative unsupported

```text
Absolute surface=no
Relative surface=no
→ TargetMode=None
```

。

PresentWait2があれば:

```text
BoundedPresentWait=true
```

は維持。

---

# 50. Pure tests — vendor authority

```text
Relative capable
+
Reflex active
→ NvidiaReflex
→ TargetMode=None

Relative capable
+
AntiLag active
→ AmdAntiLag2
→ TargetMode=None
```

。

---

# 51. Pure tests — VSync OFF

relative capableでも:

```text
FifoPresentMode=false
→ TargetMode=None
```

。

---

# 52. Pure tests — quantizer 60fps / 144Hz

入力を整数nsで生成。

期待:

```text
refresh-quanta sequenceの長期平均 ≈ 2.4
全durationがrefreshIntervalの整数倍
duration != 0
```

。

patternそのものを固定しすぎない。

検証すべき不変条件:

```text
N framesのtotal duration error
< 1 refresh quantum
```

。

---

# 53. Pure tests — 60 / 120Hz

```text
Target = 60fps
Refresh = 120Hz
→ 2 refresh cycles per presented frame
```

。

---

# 54. Pure tests — 60 / 60Hz

```text
→ 1 refresh cycle
```

。

---

# 55. Pure tests — display slower than target

```text
Target = 60fps
Refresh = 50Hz
→ duration >= 1 refresh interval
→ never returns 0
```

。

---

# 56. Pure tests — VRR

```text
RefreshInterval = UINT64_MAX
RefreshDuration < TargetFrameInterval
→ raw TargetFrameInterval

RefreshDuration > TargetFrameInterval
→ RefreshDuration
```

。

---

# 57. Pure tests — unknown refresh interval

```text
RefreshInterval = 0
→ TargetFrameIntervalNs
```

。

---

# 58. Pure tests — retry

```text
prepare relative duration
present rejected
abandon
next prepare
→ cadence step reused
```

。

---

# 59. Pure tests — accepted retry without timing

```text
queue full
↓
same present retry without timing
↓
accepted
```

。

cadence stateは1回だけcommit。

---

# 60. Pure tests — refresh change

```text
144Hz
↓
timingPropertiesCounter change
↓
120Hz
```

。

relative fractional accumulatorがresetされること。

---

# 61. Pure tests — TargetFPS change

```text
60fps
↓
120fps
```

。

old cadence fractionを持ち越さない。

---

# 62. CI contract

`audit-low-latency-contract.py`へ最低:

```text
relative feature device capability
relative surface capability
absolute preferred
absolute unavailable → relative selected
relative flag attached only in Relative mode
Reflex/Anti-Lag suppress relative
FIFO_LATEST_READY accepts relative-capable scheduler
timeDomainId remains valid-gated
TelemetryOnly targetTime remains zero
```

を追加。

---

# 63. Static compile

最低:

```text
Release build
Debug Validation build
capture OFF
capture ON
```

。

`VulkanPresentPacer.cpp`:

```text
-Wall
-Wextra
```

clean。

---

# 64. Validation Layer

relative実装後、**A/Bより先に再実施**。

理由:

今回すでにstatic auditを通過していたコードから:

```text
VUID-VkPresentTimingInfoEXT-timeDomainId-12400
```

が実機で見つかった。

relative branchは新しいVulkan valid-usage条件を踏むため、static testだけで完了扱いしない。

---

# 65. Core Validation matrix

RTX 5070 Tiで最低:

```text
Policy 0 Reflex Off
Policy 1 Reflex Off
Policy 2 Reflex Off
Policy 3 Reflex Off
Policy 2 Reflex On
Policy 3 Reflex On+Boost
VSync Off
```

。

期待:

```text
ERROR 0
VUID 0
```

。

---

# 66. 実機で最重要の観測

Policy 2:

```text
surface absolute = no
surface relative = yes

targetMode=relative
targetScheduling=active
targetTime != 0
```

。

これが出なければPhase 3 blockerは解除できていない。

---

# 67. First-present bootstrap実機確認

swapchain creation直後:

```text
targetMode=relative
targetTime=0
fallback/bootstrap=waiting for first present
```

。

その後:

```text
targetTime>0
targetScheduling=active
```

へ遷移。

---

# 68. Policy 3実機確認

対応環境:

```text
Policy 3
targetMode=relative
selected-present-mode=FIFO_LATEST_READY
```

。

これが今回のA3 unblock条件。

---

# 69. VSync OFF control

```text
selected-present-mode=IMMEDIATE or MAILBOX
targetScheduling=off
```

。

fallback reasonの文字列そのものより:

```text
non-FIFOでrelative targetが送られていない
```

ことを検証。

---

# 70. Synchronization Validation

Core Validation PASS後:

```text
validate_sync=true
```

で:

```text
Policy 2 Relative
Policy 3 Relative/FIFO_LATEST_READY
Reflex On
```

を実施。

blocking hazard 0。

---

# 71. A/B開始条件

以下すべて:

```text
[x] relative scheduler static tests
[x] validation ERROR 0
[x] sync hazard 0
[x] Policy 2 relative active
[x] Policy 3 latest-ready active if supported
[x] Reflex authority still suppresses generic
[x] no DEVICE_LOST
[x] no software fallback
[x] no recreate storm
```

。

---

# 72. A/B matrix

blocker解除後:

```text
A0 TelemetryOnly
A1 PresentWait
A2 Relative JIT
A3 Relative JIT + FIFO_LATEST_READY
B1 NVIDIA Reflex On
B2 NVIDIA Reflex On+Boost
```

。

各mode最低3 runs。

---

# 73. A/B logでtarget modeを保存

CSVへ:

```text
target_mode
```

追加推奨。

値:

```text
none
absolute
relative
```

。

これがないと後から:

```text
A2 runだったが実際はtarget off
```

を見抜けない。

---

# 74. Relative target CSV semantics

既存:

```text
target_time_ns
```

を維持する場合、relative modeでは:

```text
absolute timestampではなくduration
```

。

runbookへ必ず明記。

より明確にするなら:

```text
target_value_ns
target_mode
```

へ変更。

---

# 75. A/B acceptance

Relative JITが「実装済み」と言える条件:

```text
runtime active
Validation clean
```

まで。

Relative JITが「低遅延化に有効」と言える条件:

```text
A/Bで再現性ある改善
```

まで必要。

実装しただけで:

```text
latency improved
```

と書かない。

---

# 76. 既存default

変更禁止:

```text
VulkanPresentPacingPolicy = TelemetryOnly
```

。

NVIDIA 1環境でrelativeが動いたことだけを理由にdefault JITへ変更しない。

---

# 77. Clean-room

現行Vulkanはclean-room backend。

今回参照可:

```text
現行melonPrimeDSコード
Software Renderer
GPU3D_Compute
Khronos Vulkan Specification
Khronos Vulkan-Headers
Validation Layers
```

。

旧Watermelon / Sapphire Vulkan実装を参照しない。

---

# 78. 変更してはいけないもの

今回のscopeでは原則変更しない:

```text
GPU3D_Vulkan rasterizer
2D compositor
texture cache
Reflex marker ordering
Anti-Lag marker ordering
host FPS limiter
Windows retained device lifetime
F2 DX12 probe fix
Software/OpenGL backend
Metal
DX12
Intel XeLL
```

。

---

# 79. コメント

relative schedulingコードには必ず理由をコメント。

最低:

```text
relative target is a duration, not an absolute timestamp
FRR durations are quantized to refreshInterval
fractional accumulator preserves long-term emulator cadence
timeDomainId is still mandatory even for telemetry/relative metadata
first relative target is skipped because no previous presentation exists
```

。

「何をしているか」だけでなく「なぜ」を残す。

---

# 80. 推奨commit分割

```text
1. Vulkan: model absolute and relative target scheduling modes
2. Vulkan: add refresh-quantized relative present cadence
3. Vulkan: enable relative target-time presentation fallback
4. Vulkan: allow FIFO_LATEST_READY with relative scheduling
5. Tests: cover Vulkan relative present timing
6. CI: enforce relative present pacing contract
7. Docs: document absolute/relative Vulkan target scheduling
```

。

---

# 81. DoD — Static

```markdown
- [ ] RelativeTimingDeviceをresolverへ接続
- [ ] RelativeTimingSurfaceをresolverへ接続
- [ ] TargetMode None/Absolute/Relative
- [ ] Absolute優先
- [ ] Relative fallback
- [ ] relative duration計算をabsolute timestamp計算と分離
- [ ] FRR refresh quantization
- [ ] VRR path
- [ ] unknown refresh path
- [ ] retry/commit/abandon transactional cadence
- [ ] FIFO_LATEST_READY relative gate
- [ ] vendor authority regressionなし
- [ ] host limiter regressionなし
- [ ] pure tests PASS
- [ ] CI audits PASS
- [ ] release build PASS
- [ ] debug build PASS
```

---

# 82. DoD — Runtime

```markdown
- [ ] Validation layer enabled
- [ ] Core Validation ERROR 0
- [ ] Sync Validation hazard 0
- [ ] Policy 2 targetMode=relative
- [ ] Policy 2 targetTime>0 after bootstrap
- [ ] Policy 3 FIFO_LATEST_READY if supported
- [ ] Reflex On → generic target off
- [ ] Reflex Boost → generic target off
- [ ] VSync Off → relative target off
- [ ] no DEVICE_LOST
- [ ] no Software fallback
- [ ] no recreate storm
```

---

# 83. DoD — Phase 3 unblock

現在のRTX 5070 Ti環境で:

```text
presentAtAbsoluteTimeSupported = false
presentAtRelativeTimeSupported = true

Policy 2:
    targetMode = relative
    targetScheduling = active

Policy 3:
    targetMode = relative
    FIFO_LATEST_READY selected
    ※ present mode extension/surface条件を満たす場合
```

を実機で確認。

ここまで到達して初めて:

```text
Phase 3 A/B = UNBLOCKED
```

とする。

---

# 84. 実装完了後の次工程

順番:

```text
Relative scheduling implementation
↓
pure tests / CI
↓
Core Validation Layer
↓
Synchronization Validation
↓
残りのPhase 1 event matrix
↓
NVIDIA functional confirmation
↓
A/B
↓
click-to-photon if hardware available
```

。

---

# 85. 一次資料

## melonPrimeDS

Branch:

https://github.com/ag-advania/melonPrimeDS/tree/develop_remakeVulkan_ver2

作成時HEAD:

https://github.com/ag-advania/melonPrimeDS/commit/3da6d5a7d1bf139c293b7be30320aadbd782bba7

現行結果報告:

```text
.codex/melonPrimeDS_Vulkan_ValidationLayer_NVIDIA実機AB_実施結果_2026-08-13.md
```

主要code:

```text
src/VulkanPresentPacingPolicy.h
src/VulkanPresentTimingModel.h
src/VulkanPresentPacer.h
src/VulkanPresentPacer.cpp
src/VulkanDevice.cpp
src/VulkanModernPresentCompat.h
tools/testing/vulkan-present-timing-tests.cpp
tools/ci/audits/audit-low-latency-contract.py
```

## Khronos

VK_EXT_present_timing:

https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html

VkPresentTimingInfoEXT:

https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentTimingInfoEXT.html

VkPresentTimingInfoFlagBitsEXT:

https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentTimingInfoFlagBitsEXT.html

VkPhysicalDevicePresentTimingFeaturesEXT:

https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDevicePresentTimingFeaturesEXT.html

vkGetPastPresentationTimingEXT:

https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingEXT.html

---

# 86. 最終指示

今回の根本要件は:

> **「absoluteが使えないからJITを諦める」のではなく、Vulkanが明示的に提供しているrelative-time schedulingへ安全にfallbackする。**

ただしrelativeはabsolute timestampではない。

```text
Absolute = presentation clock上のtarget時刻
Relative = previous imageのminimum visible duration
```

という意味の違いをarchitectureに反映すること。

またFRRではdurationをrefresh quantumへ適切に配分し、60fps on 144Hzのような非整数比を固定roundで壊さない。

実装後も:

```text
TelemetryOnly default
Reflex / Anti-Lag authority
host FPS limiter
Validation-first
fail-soft
clean-room
```

を維持する。

Phase 3 A/Bは、実機logで`targetMode=relative`かつ`targetScheduling=active`が確認できてから開始すること。
