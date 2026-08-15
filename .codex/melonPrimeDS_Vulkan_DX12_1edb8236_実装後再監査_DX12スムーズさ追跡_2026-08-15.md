# melonPrimeDS Vulkan / DX12 最新コミット実装後再監査
## Vulkan残存pacing監査・DX12スムーズさ低下追跡

- 作成日: 2026-08-15
- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver3`
- 前回監査HEAD: `94d5e41c80260d1510b89ac528cdfc9d94cbde0b`
- 今回HEAD: `1edb8236d264c0fce5f169a7a9cf7533c14c4989`
- HEAD commit: `[PERF] complete Vulkan DX12 residual pacing audit`
- 前回HEADから: **1 commit ahead / 0 behind**
- 主監査対象:
  - Vulkan latest-submission one-frame budget
  - `VK_KHR_present_wait2` / `VK_KHR_present_wait` / fence fallback ladder
  - bounded `AcquireNextImageKHR` A/B
  - presenter-local frames-in-flight A/B
  - 2-image swapchain A/B
  - transient descriptor pool lazy reset
  - Vulkan / DX12 GPU timestamp telemetry
  - NVIDIA Reflex / AMD Anti-Lag 2 pacing authority非回帰
  - ユーザー報告「DX12のスムーズさが少し失われた気がする」の原因追跡

---

# 1. 総合判定

```text
PASS WITH ONE NEW VULKAN P2 ROBUSTNESS FINDING
AND CONDITIONAL DX12 MEASUREMENT-OVERHEAD FINDING

P1 correctness findings: 0
P2 source findings: 1
P3 measurement / validation findings: 2
Previous Vulkan latest-slot P2: CLOSED
DX12 transition P3: remains CLOSED
NVIDIA Reflex regression: NOT FOUND STATICALLY
GitHub commit status / workflow evidence: NONE
```

今回のpushでは、前回監査で最重要だった

```text
fallbackが next reusable slot を待ち、latest submitted frameを待っていない
```

という問題は正しく修正されている。

`FrameRing`が`LastSubmittedIndex`を保持し、strict fallbackは

```cpp
Frames.LatestSubmittedFrameHasPendingSubmission()
Frames.WaitForLatestSubmittedFrame(frameBudgetNs)
```

を使用するため、2-slot presenterでも「2 frame前の再利用slot」ではなく、直前にsubmitした最新presenter workを待つ。

この点は**CLOSED**と判定する。

一方、新しいbounded fence fallbackでは、frame budget内に最新submissionがretireしなかっただけで、

```cpp
Failed = true;
```

としてVulkan presenter自体を恒久failureへ落としている。

これは、

```text
latency budget miss
```

と、

```text
device / surface / synchronization failure
```

を同一扱いしており、低遅延制御として過剰である。

特に同じprevious-present waitを行う`present_wait2` / legacy `present_wait` pathでは`VK_TIMEOUT`を非fatalとしてcountした後に継続しているため、fence fallbackだけfailure semanticsが不整合になっている。

したがって今回の新規主要findingは、

> **VK-P2-001: strict latest-submission fence timeoutをrenderer failureにしてはいけない**

である。

---

# 2. 最新コミット確認

今回HEAD:

```text
1edb8236d264c0fce5f169a7a9cf7533c14c4989
[PERF] complete Vulkan DX12 residual pacing audit
```

parent:

```text
94d5e41c80260d1510b89ac528cdfc9d94cbde0b
```

したがって前回監査から**1 commit**である。

主な変更ファイルは以下。

```text
src/DX12Context.cpp
src/DX12Context.h
src/DX12GpuTimestamp.h
src/DX12Perf.h
src/GPU3D_DX12.cpp
src/GPU3D_Vulkan.cpp
src/GPU_DX12.cpp
src/GpuStageMetrics.h
src/VulkanDevice.cpp
src/VulkanGpuTimestamp.h
src/VulkanLoader.cpp
src/VulkanLoader.h
src/VulkanModernPresentCompat.h
src/VulkanPerf.h
src/VulkanPresentPacer.cpp
src/VulkanPresentPacer.h
src/VulkanPresentPacingPolicy.h
src/VulkanPresenterFrameBudget.h
src/VulkanSync.cpp
src/VulkanSync.h
src/frontend/qt_sdl/MelonPrimeDX12SurfacePresenter.cpp
src/frontend/qt_sdl/MelonPrimeVulkanPresenter.cpp
src/frontend/qt_sdl/MelonPrimeVulkanPresenter.h
tools/ci/audits/audit-low-latency-contract.py
tools/testing/vulkan-present-pacer-dispatch-tests.cpp
tools/testing/vulkan-present-timing-tests.cpp
```

またcommit内の監査追記には、local build / device-free unit / fake dispatch / static auditのPASS記録がある。

ただしGitHub connectorで今回HEADを確認した範囲では、

```text
combined statuses: none
workflow runs associated with commit: none
```

である。

よって本書では、commit内に記録されたlocal validationは「実施記録」として参照するが、GitHub Actionsによる独立CI PASSとは扱わない。

---

# 3. 前回P2: latest submitted frame wait

## 判定

**CLOSED / PASS STATIC**

前回コードは、

```cpp
WaitForNextFrameSlot()
```

を使用しており、2-slot ringでは最新frameではなく次に再利用する古いslotを待っていた。

今回、`FrameRing`へ、

```cpp
u32 LastSubmittedIndex;
bool HasSubmittedFrame;
```

が追加された。

`SubmitFrame()`成功時に、

```cpp
LastSubmittedIndex = CurrentIndex;
HasSubmittedFrame = true;
```

を保存する。

strict waitは、

```cpp
FrameContext& frame = Frames[LastSubmittedIndex];
```

を対象にする。

これにより、

```text
F1 -> slot 0
F2 -> slot 1
F3 pre-input -> slot 1 / F2をwait
```

となる。

前回の、

```text
F3 pre-input -> slot 0 / F1をwait
```

という誤りは解消した。

またunit testにも、

```text
latest submitted frame is the target
next reusable slot is NOT used as one-frame-budget definition
```

という回帰防止assertが追加されている。

この修正方針は正しい。

---

# 4. `VK_KHR_present_wait2` → legacy `VK_KHR_present_wait` → fence fallback

## 判定

**PASS STATIC**

今回、従来の`VK_KHR_present_wait2`だけでなく、legacyの

```text
VK_KHR_present_id
VK_KHR_present_wait
vkWaitForPresentKHR
```

もdevice feature / extension / dispatch chainへ追加されている。

pacerは、

```text
1. present_wait2 が利用可能 -> vkWaitForPresent2KHR
2. present_wait2不可、legacy wait可 -> vkWaitForPresentKHR
3. generic present wait不可 -> latest-submission fence fallback
4. それも使わないpolicy / authority -> waitなし
```

というladderになった。

またpresent_wait2がruntime failureでdisableされた場合も、legacy present-id correlationを維持できるように`VkPresentIdKHR`をpresent chainへ保持している。

これは前回指示した「capability downgradeで急に2-deepへ戻らない」設計として妥当。

---

# 5. Reflex / Anti-Lag 2二重pacing監査

## 判定

**PASS STATIC / REGRESSION NOT FOUND**

`PresentPacer.BeginFrame()`には、

```text
Reflex active
Anti-Lag active
normal speed
frame interval
```

が渡される。

vendor low-latency APIがauthorityを持つ場合、generic previous-present waitを単純に重ねない設計は維持されている。

今回追加された、

```text
MELONPRIME_VULKAN_PRESENTER_PREINPUT_WAIT=1
```

はdeveloper A/B専用であり、Reflex authorityそのものを奪わず、Reflex sleep前にlatest presenter fenceを追加で試験するためのexplicit experimentになっている。

通常設定へ強制適用されるものではない。

したがって今回commitによるReflex marker / sleep state machineの破壊は静的には見つからない。

---

# 6. 新規P2: fence budget timeoutをrenderer failureにしている

## ID

`VK-P2-001`

## Severity

**P2 / High confidence / robustness + latency path**

## 現在の実装

`BeginLowLatencyFrame()`では、strict fence fallbackまたはReflex pre-input A/B時に、

```cpp
const u64 frameBudgetNs = VulkanPresenterOneFrameBudgetTimeoutNs(
    targetFrameIntervalNs);
const FrameWaitResult waitResult =
    Frames.WaitForLatestSubmittedFrame(frameBudgetNs);
```

を行う。

`FrameWaitResult::Timeout`になると、

```cpp
VulkanPresenterBudgetMissCount++
VulkanPresenterLatestSubmissionWaitTimeoutCount++
Failed = true;
Error = "Vulkan presenter frame budget wait timed out";
return;
```

となる。

## 問題

`Timeout`は、

```text
GPU device lost
surface lost
fence API error
```

ではない。

単に、

```text
最新presenter submissionが設定したlow-latency budget内にretireしなかった
```

という意味である。

60Hz付近ではhelperが最大16msへclampするため、一時的に16msを少し超えただけでもhard failureへ落ち得る。

さらに`present_wait2` / legacy `present_wait`側は`VK_TIMEOUT`について、

```cpp
VulkanPreviousPresentWaitTimeoutCount++;
return VulkanPacerBeginResult::Continue;
```

としている。

つまり、同じ「previous workがbudget内に終わらなかった」という現象に対して、

```text
present_wait path -> nonfatal
fence fallback    -> fatal
```

となっている。

これはlow-latency fallback semanticsとして不整合。

## 推奨修正

`FrameWaitResult::Error`だけをhard failureにする。

`FrameWaitResult::Timeout`は、

```text
budget missとしてtelemetryへ記録
Vulkan presenterはaliveのまま維持
そのlogical frameのpresentationをskipして前frameを表示維持
次frameで再試行
```

とするのが安全。

例:

```cpp
if (waitResult == FrameWaitResult::Timeout)
{
    VulkanPerf::AddCounter(VulkanPerf::Counter::VulkanPresenterBudgetMissCount);
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::VulkanPresenterLatestSubmissionWaitTimeoutCount);
    SkipNextPresentationForLatencyBudget = true;
    return;
}

if (waitResult == FrameWaitResult::Error)
{
    Failed = true;
    Error = "Vulkan presenter latest-submission fence wait failed";
    return;
}
```

実際のflag名は任意。

重要なのは、

```text
budget miss != renderer failure
```

を明確にすること。

## 禁止

```text
timeoutを1秒へ戻す
vkDeviceWaitIdleへfallback
vkQueueWaitIdleへfallback
Reflex sleepの後へwaitを移す
renderer/compositor global frames-in-flightを変更する
```

---

# 7. bounded `AcquireNextImageKHR` A/B

## 判定

**PASS STATIC / EXPERIMENTAL**

新しい環境変数、

```text
MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS=<nanoseconds>
```

が未設定なら従来どおり、

```cpp
UINT64_MAX
```

を返すため、default挙動は変わらない。

A/Bでtimeoutまたは`VK_NOT_READY`になった場合は、

```text
ImageAvailable semaphoreをwaitへ流用しない
empty dependency-free submissionでframe ringをretire
presentをskip
次frameで再試行
```

となっている。

これはsemaphore stateを壊さず、nonblocking acquire実験を行う設計として妥当。

ただし**defaultは依然としてunbounded acquire**である。

したがって、今回のcommitを入れただけで通常プレイのpost-input acquire stallが消えるわけではない。

実機A/Bで効果が確認できた後にdefault化を判断すべき。

---

# 8. presenter-local frames-in-flight=1 A/B

## 判定

**PASS STATIC / EXPERIMENTAL**

環境変数、

```text
MELONPRIME_VULKAN_PRESENTER_FRAMES_IN_FLIGHT=1
```

が指定された場合のみ、presenterの`FrameRing`を1 slotへする。

未指定時は、

```cpp
Vk::FramesInFlight
```

すなわち従来の2 slotを使用する。

この変更は、

```text
renderer raster ring
capture ring
structured compositor ring
```

には波及しない。

前回指示した「global frames-in-flightを雑に1へ変更しない」という条件を守っている。

一方、これもdefaultでは無効なので、ユーザーが通常起動だけで比較している場合、Vulkan presenter physical depthはまだ従来値である。

---

# 9. 2-image swapchain A/B

## 判定

**PASS STATIC / EXPERIMENTAL**

```text
MELONPRIME_VULKAN_SWAPCHAIN_IMAGE_COUNT=2
```

によるA/Bが追加されている。

これもdeveloper experimentであり、surface capabilityを無視して強制するものではなく、利用可能範囲でのA/B用として扱うべき。

defaultでは従来のswapchain image selectionを維持する。

したがって、今回のpushは

```text
VulkanをDX12と同じphysical depthへ即座に変更したcommit
```

ではなく、

```text
その差を安全に実測するためのA/B seamを完成させたcommit
```

と理解するのが正しい。

---

# 10. transient descriptor pool lazy reset

## 判定

**PASS STATIC**

以前はpresenter frame boundaryでtransient descriptor poolを定常的にresetしていた。

今回、

```cpp
if (TransientDescriptorPoolUsed[frameIndex])
{
    vkResetDescriptorPool(...);
    TransientDescriptorPoolUsed[frameIndex] = false;
}
```

となり、実際にtransient allocationを使用したslotだけresetする。

persistent direct compositor descriptor cacheとは分離されているため、resource lifetime設計も壊していない。

低リスクな改善として妥当。

---

# 11. GPU timestamp profiler

## 判定

**IMPLEMENTED / MEASUREMENT PATH ONLY**

VulkanとDX12の双方に、共通の5 stageが追加された。

```text
Raster
CaptureSidecar
StructuredCompositor
PresenterRenderPass
TotalQueueSpan
```

これにより、従来のCPU-side wait / record時間だけでなく、VulkanとDX12のGPU stage時間を同じ名前で比較できるようになった。

これは「VulkanがまだDX12より重く感じる」の原因を、

```text
presentation depth / wait由来
```

と、

```text
純粋なGPU throughput由来
```

へ分離するために有用。

---

# 12. DX12のスムーズさが少し失われたという報告

## 結論

現HEADのsource差分を見る限り、**DX12の通常pacing pathそのものを悪化させる変更は確認できない。**

現在もDX12 presenterは、

```cpp
Swapchain->SetMaximumFrameLatency(1);
FrameLatencyWaitable = Swapchain->GetFrameLatencyWaitableObject();
```

を維持している。

Reflex mode / pacing authorityについても今回のDX12変更はtelemetry counter追加が中心で、sleep/present sequencingを書き換えていない。

したがって、`MELONPRIME_PERF`を付けていない通常起動で本当に差が再現するなら、現時点ではstatic sourceだけから根本原因を確定できない。

ただし、**`MELONPRIME_PERF=1`を有効にしている場合は別**である。

---

# 13. DX12-P3-001: `MELONPRIME_PERF=1`時のGPU timestamp observer overhead

## Severity

**P3 / conditional / measurement perturbation**

## 13.1 default OFF

`DX12Perf::IsEnabled()`は、

```cpp
const char* value = std::getenv("MELONPRIME_PERF");
return value && value[0] == '1' && value[1] == '\0';
```

である。

`DX12CommandContext::Init()`も、

```cpp
if (DX12Perf::IsEnabled())
{
    CreateQueryHeap(...);
    GetTimestampFrequency(...);
    CreateCommittedResource(READBACK ...);
}
```

となる。

したがって環境変数未設定の通常起動では、timestamp query heap / readback resource自体を作らない。

`WriteTimestamp()`も、

```cpp
if (!TimestampQueriesEnabled)
    return;
```

なのでno-op。

このため**通常起動で今回のtimestamp profilerがDX12を目立って重くする構造ではない。**

## 13.2 `MELONPRIME_PERF=1`では実際にGPU workが増える

perf有効時には、

```text
EndQuery(timestamp)
ResolveQueryData
readback Map
readback Unmap
```

が追加される。

さらに`Submit()`は、書き込まれたquery indexごとに、

```cpp
ResolveQueryData(..., queryIndex, 1, ...)
```

を発行している。

`RecordDX12GpuMetric()`はprevious submissionの結果を読むたびに`Map/Unmap`を行う。

Presenterでは1 frameのBeginFrame時に少なくとも、

```text
PresenterRenderPass
TotalQueueSpan
```

の2 spanを読み、renderer/compositor側にも同種のreadがある。

これはdeveloper telemetryとしては許容可能だが、

```text
「計測ON時の体感」を通常performanceとして比較してはいけない
```

というobserver effectを持つ。

ユーザーがperformance監査のために`MELONPRIME_PERF=1`を残したままDX12をプレイしている場合、今回の「少しスムーズさが失われた」という報告の**第一候補**になる。

## 13.3 重要: process再起動が必要

`IsEnabled()`はfunction-local `static const bool`で結果をcacheする。

そのため同じprocess内で環境変数を変更しても確実なA/Bにならない。

比較は必ず別processで行う。

Windows PowerShell例:

```powershell
Remove-Item Env:MELONPRIME_PERF -ErrorAction SilentlyContinue
# その後、新しくmelonPrimeDSを起動
```

perf ON再現用:

```powershell
$env:MELONPRIME_PERF = '1'
# 新しくmelonPrimeDSを起動
```

---

# 14. DX12 timestamp instrumentationの改善案

telemetry ONでもpacing perturbationをさらに小さくするなら次を推奨する。

## 14.1 query resolveをまとめる

現在はwritten queryごとに、

```cpp
ResolveQueryData(... NumQueries=1 ...)
```

を発行している。

可能なら、使用queryの最小index～最大indexを1つのcontiguous rangeとして、

```cpp
ResolveQueryData(... first, count ...)
```

へまとめる。

## 14.2 readbackをper-metric Map/Unmapしない

現在の`ReadTimestampSpanNanoseconds()`はspanを読むたびにMap/Unmapする。

1 submission分の10 query値を一度だけreadbackし、CPU arrayへcopyした後、複数metricをそのsnapshotから計算する方が軽い。

例:

```text
BeginFrame after previous fence retirement
    -> ReadAllResolvedTimestampsOnce()
    -> derive Raster / Sidecar / Compositor / Presenter / Total locally
```

## 14.3 profilerはshipping default OFFを維持

これは必須。

GPU timestamp profilerを常時ONへ変更してはいけない。

---

# 15. DX12-P3-002: timestamp frequencyの長期cache

## Severity

**P3 / measurement accuracy**

`DX12CommandContext::Init()`では、

```cpp
queue->GetTimestampFrequency(&frequency)
TimestampFrequency = frequency;
```

を一度だけ行い、その値を以後のtimestamp換算に使用する。

MicrosoftのD3D12 Timing documentationは、dynamic clock scaling下ではtimestamp frequencyが変化するhardwareがあり、timestamp resolveに近い時点でfrequencyを再queryするよう注意している。

したがって長時間runやGPU clock stateが変化する比較では、絶対nanoseconds値に誤差が入る可能性がある。

これはrenderer性能そのものを悪化させるfindingではないが、Vulkan/DX12比較telemetryの信頼性に関わる。

## 推奨

毎frame再queryまでは不要でも、少なくとも、

```text
perf report window開始時
一定秒数ごと
GPU clock / mode transition後
```

などで`GetTimestampFrequency()`を更新する。

---

# 16. 今回のVulkan改善はdefault performanceを直接変更しているか

## 結論

**大部分はNO。**

今回追加された重要なA/Bはdeveloper opt-inである。

```text
MELONPRIME_VULKAN_PRESENTER_FRAMES_IN_FLIGHT=1
MELONPRIME_VULKAN_PRESENTER_PREINPUT_WAIT=1
MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS=<nanoseconds>
MELONPRIME_VULKAN_SWAPCHAIN_IMAGE_COUNT=2
```

未設定時は、

```text
presenter depth = 従来値
Acquire timeout = UINT64_MAX
2-image experiment = OFF
Reflex pre-input extra fence A/B = OFF
```

である。

つまり今回のpushは、前回の原因仮説を**安全に実測・A/Bできる状態へした**ことが主な成果。

Vulkanが通常起動でDX12に体感上追いついたことをsourceだけから期待する段階ではまだない。

---

# 17. 次に行うべきA/B

## A. DX12 smoothness regression確認

最優先。

同一PC、同一ROM、同一scene、同一settings、同一build typeで、

```text
A1: 94d5e41c / MELONPRIME_PERF unset
A2: 1edb8236 / MELONPRIME_PERF unset
A3: 1edb8236 / MELONPRIME_PERF=1
```

を比較する。

期待:

```text
A1 ≒ A2, A3だけ悪化
    -> timestamp profiler observer overheadが原因

A1よりA2が悪化、perf unsetでも再現
    -> telemetry以外の差を追加追跡
```

DX12の今回差分は限定的なので、この3-way比較でかなり強く切り分けられる。

## B. Vulkan physical depth A/B

同じcurrent HEADで、

```text
B0: default
B1: PRESENTER_FRAMES_IN_FLIGHT=1
B2: PREINPUT_WAIT=1 + Reflex On
B3: SWAPCHAIN_IMAGE_COUNT=2
B4: bounded acquire
```

を一度に全部有効化せず、**1変数ずつ**比較する。

全部同時にONにすると、どのmechanismが効いたか分からない。

## C. Vulkan GPU throughput comparison

`MELONPRIME_PERF=1`はmeasurement overheadを含むため、絶対frame feelではなくstage ratioを見る。

```text
RasterGpuTimeNs
CaptureSidecarGpuTimeNs
StructuredCompositorGpuTimeNs
PresenterRenderPassGpuTimeNs
TotalQueueGpuSpanNs
```

を同一scene・同一scaleでDX12/Vulkan比較する。

特に、

```text
Vulkan Raster > DX12 Raster
```

ならrenderer本体差。

```text
Rasterは近いが Vulkan TotalQueueSpan / presenter waitだけ大きい
```

ならpresentation / synchronization差。

---

# 18. 推奨修正優先順位

## Priority 1

**VK-P2-001を修正する。**

fence timeoutをhard renderer failureにせず、budget miss / present skipとして扱う。

## Priority 2

**DX12 A1/A2/A3の3-way比較。**

ユーザーが感じたスムーズさ低下が`MELONPRIME_PERF=1`由来か即座に切り分ける。

## Priority 3

Vulkan presenter depth=1をReflex OnでA/B。

## Priority 4

Vulkan bounded acquireを単独A/B。

## Priority 5

Vulkan 2-image swapchainを単独A/B。

## Priority 6

GPU timestamp telemetry自体をlow-overhead化。

```text
batched ResolveQueryData
single readback Map per completed submission
periodic timestamp frequency refresh
```

---

# 19. 受入条件

## VK-P2-001修正後

必須:

```text
latest fence completes within budget -> normal present
latest fence timeout -> budget miss count++, renderer remains alive
latest fence API error -> renderer failure
present_wait2 timeout -> renderer remains alive
legacy present_wait timeout -> renderer remains alive
```

さらに、

```text
resize
minimize/restore
fullscreen toggle
renderer switch
Reflex Off
Reflex On
Reflex On+Boost
```

でVulkan rendererがbudget missだけを理由にfallbackしないこと。

## DX12

必須:

```text
MELONPRIME_PERF unsetで94d5e41cと1edb8236のpacing差がないこと
SetMaximumFrameLatency(1)維持
Reflex mode維持
present waitable object維持
```

telemetry ON時は、

```text
OFFより多少遅くなることは許容
ただしframe pacingを著しく乱さない
report自体がbenchmarkを支配しない
```

こと。

---

# 20. finding summary

| ID | Severity | Finding | 判定 |
|---|---|---|---|
| PREV-VK-001 | P2 | fallbackがnext reusable slotを待っていた | **CLOSED** |
| VK-WAIT-002 | - | present_wait2 previous-present wait | **PASS STATIC** |
| VK-WAIT-003 | - | legacy present_wait fallback | **PASS STATIC** |
| VK-REFLEX-001 | - | vendor pacing authority / double-wait防止 | **PASS STATIC** |
| VK-P2-001 | **P2** | latest fenceのbudget timeoutを`Failed=true`へしている | **OPEN / MUST FIX** |
| VK-AB-001 | P3 | presenter-local depth=1 | **IMPLEMENTED / RUNTIME A/B OPEN** |
| VK-AB-002 | P3 | bounded acquire | **IMPLEMENTED / RUNTIME A/B OPEN** |
| VK-AB-003 | P3 | 2-image swapchain | **IMPLEMENTED / RUNTIME A/B OPEN** |
| VK-PERF-006 | P3 | transient descriptor reset churn | **CLOSED STATIC** |
| DX12-SMOOTH-001 | P3 | perf telemetry ON時のtimestamp observer overhead | **CONDITIONAL / HIGH PLAUSIBILITY IF PERF=1** |
| MEASURE-DX12-001 | P3 | timestamp frequencyをInit時に長期cache | **OPEN / MEASUREMENT ACCURACY** |
| CI-001 | validation | current HEADのGitHub status/workflow metadataなし | **OPEN EVIDENCE GAP** |

---

# 21. 最終結論

今回のpushは、前回の中心問題だった、

```text
2-slot Vulkan presenterでlatest frameではなく古いreusable slotを待つ
```

という設計ミスを正しく解消した。

また、

```text
present_wait2
legacy present_wait
latest-submission fence
bounded acquire
presenter-local depth=1
2-image swapchain
GPU stage timestamp
```

まで揃い、VulkanとDX12の差をかなり精密に分解できる状態になった。

一方で、strict fence timeoutをhard failureにしている点は修正が必要。

これはlow-latency budget missをdevice failureと混同するため、A/B機能をdefault化する前に必ず直すべきである。

また、ユーザーが追加報告した、

> DX12のスムーズさが少し失われた気がする

については、今回のsource差分から、DX12の通常pacing / Reflex / `SetMaximumFrameLatency(1)`が壊れた証拠は見つからなかった。

ただし`MELONPRIME_PERF=1`時には、今回新設したGPU timestamp profilerが、

```text
EndQuery
ResolveQueryData
Map/Unmap
```

を毎frame追加する。

したがって、performance調査用環境変数を有効にしたままプレイしていた場合は、これが**最有力候補**である。

最初に、

```text
94d5e41c / perf OFF
1edb8236 / perf OFF
1edb8236 / perf ON
```

の3-way比較を行うべき。

これでDX12側の体感変化が計測instrumentationによるものか、通常pathの実regressionかを明確に分離できる。

---

# 22. 監査ソース

主に以下のcurrent HEAD sourceを確認した。

```text
src/VulkanSync.cpp
src/VulkanSync.h
src/VulkanPresentPacer.cpp
src/VulkanPresentPacingPolicy.h
src/VulkanPresenterFrameBudget.h
src/VulkanDevice.cpp
src/VulkanLoader.cpp
src/frontend/qt_sdl/MelonPrimeVulkanPresenter.cpp
src/frontend/qt_sdl/MelonPrimeVulkanPresenter.h
src/DX12Context.cpp
src/DX12Context.h
src/DX12GpuTimestamp.h
src/DX12Perf.h
src/GPU3D_DX12.cpp
src/GPU3D_Vulkan.cpp
src/GPU_DX12.cpp
src/GpuStageMetrics.h
src/frontend/qt_sdl/MelonPrimeDX12SurfacePresenter.cpp
tools/testing/vulkan-present-pacer-dispatch-tests.cpp
tools/testing/vulkan-present-timing-tests.cpp
```

D3D12 timestamp measurement semanticsについてはMicrosoft Direct3D 12 documentationも参照した。

---

# 23. 監査書に基づく実装完遂結果

## 23.1 `VK-P2-001` latest-fence timeout

strict presenter fallbackの`WaitForLatestSubmittedFrame()`について、timeoutとAPI errorを分離した。

```text
FrameWaitResult::Ready   -> 既存どおり通常のpresent pathへ継続
FrameWaitResult::Timeout -> budget missを計上し、次の1回だけBeginFrameをskipして次フレームで再試行
FrameWaitResult::Error   -> Failed=true、renderer failureとして既存のエラー経路へ移行
```

timeoutでは以下を行うが、`Failed`と`Error`は変更しない。

```text
VulkanPresenterBudgetMissCount++
VulkanPresenterLatestSubmissionWaitTimeoutCount++
VulkanPresentSkippedForLatencyBudgetCount++
SkipNextPresentationForLatencyBudget = true
```

skip flagは`VulkanPresenter::BeginFrame()`で1回だけ消費する。swapchain再作成が必要な場合は再作成を先に行い、presenterの生存状態を維持したまま次フレームへ戻る。timeout待ちの位置は従来どおりReflex sleep、Anti-Lag INPUT、入力サンプルより前であり、1秒timeout、`vkDeviceWaitIdle`、`vkQueueWaitIdle`、global `Vk::FramesInFlight`変更は導入していない。

`present_wait2` timeoutとlegacy `present_wait` timeoutの既存non-fatal処理も変更していない。

## 23.2 DX12 timestamp telemetry overhead

`DX12CommandContext::Submit()`を以下の形へ変更した。

```text
written queryの最小index〜最大indexを1つの連続範囲としてResolveQueryData() 1回で解決
```

未使用queryを含む連続範囲はreadback上 harmless であり、metric endpointごとのResolve commandを発行しない。

完了submissionのreadbackは`ReadTimestampSnapshot()`で一度だけMap/Unmapし、`ReadTimestampSpanNanoseconds()`の後続metricはキャッシュ済みのローカル配列を参照する。これにより、Presenter、Raster、Structured compositor、Capture sidecarの複数metricが同一submissionを読む場合もmetricごとのMap/Unmapを発生させない。

timestamp frequencyは`DX12CommandContext::ResetList()`のフレーム境界で、timestamp telemetryが有効な場合だけ1秒間隔で`GetTimestampFrequency()`を再取得する。通常の`MELONPRIME_PERF` unset pathではquery heap/readback自体が有効化されないため、通常pacingにこの計測処理を追加していない。

`SetMaximumFrameLatency(1)`、present waitable object、Reflex modeの実装は変更していない。

## 23.3 実行した検証

```text
python tools/ci/audits/audit-low-latency-contract.py
  -> Low-latency contract audit PASS

cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\debug-mingw-vulkan-validation2 --jobs 1 --tail 80
  -> Build succeeded
  -> Vulkan present timing model tests PASS
  -> Vulkan present pacer fake-dispatch tests passed
  -> Vulkan renderer fallback tests PASS
  -> CaptureSidecar dependency vectors PASS
  -> Intel XeLL fake API state-machine tests PASS

git diff --check
  -> PASS
```

このDebug構成は`MELONPRIME_ENABLE_DX12=1`、`MELONPRIME_ENABLE_VULKAN=1`でコンパイルした。

## 23.4 実機・CIの残存証拠

以下はこの作業環境で物理GPUを用いた再生試験まで実施していないため、合格とは記録しない。

```text
VK-AB-001 presenter-local depth=1 runtime A/B       OPEN / NOT RUN
VK-AB-002 bounded acquire runtime A/B               OPEN / NOT RUN
VK-AB-003 2-image swapchain runtime A/B             OPEN / NOT RUN
DX12 94d5e41c OFF vs 1edb8236 OFF vs 1edb8236 ON   OPEN / NOT RUN
resize/minimize/fullscreen/renderer-switch matrix   OPEN / NOT RUN
current HEAD GitHub CI/workflow evidence            OPEN / NOT AVAILABLE
```

したがって、今回の完遂判定はsource/static/build/unitの範囲での実装完了であり、physical runtime A/Bやcurrent-head CIの代替証拠ではない。

## 23.5 更新後finding summary

| ID | 更新後判定 | 根拠 |
|---|---|---|
| VK-P2-001 | **CLOSED STATIC / BUILD** | timeoutは非致命的budget miss、Errorだけがrenderer failure |
| DX12-SMOOTH-001 | **CONDITIONAL / RUNTIME A/B OPEN** | default OFF、ON時のobserver overheadをResolve/Map集約で低減 |
| MEASURE-DX12-001 | **CLOSED STATIC / RUNTIME CLOCK-DRIFT OPEN** | frequencyを1秒間隔で再取得する構造を実装 |
| VK-AB-001 | **IMPLEMENTED / RUNTIME A/B OPEN** | presenter-local depth=1の物理比較は未実施 |
| VK-AB-002 | **IMPLEMENTED / RUNTIME A/B OPEN** | bounded acquireの物理比較は未実施 |
| VK-AB-003 | **IMPLEMENTED / RUNTIME A/B OPEN** | 2-image swapchainの物理比較は未実施 |
| VK-PERF-006 | **CLOSED STATIC** | 既存のtransient descriptor lazy resetを維持 |
| CI-001 | **OPEN EVIDENCE GAP** | current HEADのCI metadataは未提供 |

以上により、監査書で指定された未完遂のsource対応は完了した。実機A/B、物理GPUのresize等のmatrix、GitHub CIの証跡は、実行環境または外部証拠が得られた時点で別途更新する。
