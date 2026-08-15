# melonPrimeDS Vulkan / DX12 最新実装後再監査
## 残存パフォーマンス差・入力遅延差の原因分析と追加修正指示

- 作成日: 2026-08-15
- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver3`
- 前回監査HEAD: `f2ec777658b26881828776439323e3996809ce35`
- 今回HEAD: `94d5e41c80260d1510b89ac528cdfc9d94cbde0b`
- HEAD commit: `[PERF] complete Vulkan pacing and DX12 descriptor transition fixes`
- 前回HEADから: **1 commit ahead / 0 behind**
- 監査対象:
  - PERF-006 DX12 renderer transition quiesce / descriptor cache invalidate
  - Vulkan strict presenter budget
  - `VK_KHR_present_wait2` / fence fallback
  - NVIDIA Reflex二重待ち防止
  - Vulkan presentation telemetry
  - VulkanとDX12の残存performance / latency構造差
- 総合判定: **PASS WITH REMAINING VULKAN P2 LATENCY / PERFORMANCE FINDINGS**
- P1 correctness finding: **0**
- P2 performance / latency finding: **3**
- P3 optimization / validation finding: **3**

---

# 1. 結論

今回のpushは、前回の2件の指示書に対して大部分を正しく実装している。

特に、

1. DX12 renderer切替前のqueue quiesce
2. DX12 direct descriptor cache invalidate
3. Vulkan `PresenterOneFrameBudget` policy
4. `VK_KHR_present_wait2`によるprevious-present wait
5. present_wait2非対応時のfence fallback
6. Reflex / Anti-Lag active時のgeneric wait二重化防止
7. presenter fence / acquire / previous-present wait telemetry

はソース上確認できた。

また、前回のDX12 PERF-006 renderer-instance boundary findingは、今回の修正によって**CLOSED**と判定する。

一方、ユーザーが現在も

> DX12の方がVulkanよりperformanceが良く感じる

と感じることについては、コード上でも依然として説明可能な差が残っている。

最も重要なのは次の3点である。

### 残存主因A

**今回追加した`PresenterOneFrameBudget`はdefaultでは有効にならない。**

現在のdefaultは、

```cpp
NvidiaReflexMode = 1
VulkanPresentPacingPolicy = 0
```

であり、Vulkan policy `0` は `TelemetryOnly` である。

つまり通常設定のままでは、今回追加したstrict presenter budgetはVulkanのbehaviorを変更しない。

さらにNVIDIA Reflexが実際にactiveな環境では、policy resolverがReflexを唯一のpacing authorityとして選択し、generic `present_wait2` / fence budgetを意図的に無効化する。

したがって、**NVIDIA環境でReflex Onの通常プレイを比較している場合、今回のstrict budget実装後もVulkanのqueue-depth modelが大きく変わらないのは現在の設計上当然の結果である。**

### 残存主因B

**present_wait2非対応時のfence fallbackは、名前とは異なり実際にはone-frame budgetになっていない。**

現在のVulkan presenterは2 frame ringである。

```cpp
Vk::FramesInFlight = 2
```

一方、fallbackは、

```cpp
Frames.NextFrameSlotHasPendingSubmission()
Frames.WaitForNextFrameSlot()
```

を使用する。

`WaitForNextFrameSlot()`が待つのは、**直前にsubmitしたframeではなく、次にcommand resourcesを再利用するslot**である。

2-slot ringでは、これは通常2 frame前のslotになる。

例:

```text
Frame 1 -> slot 0 submit
Frame 2 -> slot 1 submit
Frame 3 pre-input:
    WaitForNextFrameSlot() -> slot 0 / Frame 1をwait
    slot 1 / Frame 2はまだpendingでもよい
Frame 3 -> slot 0へsubmit可能
```

この結果、

```text
Frame 2 pending
Frame 3 pending
```

という2-deep状態は引き続き成立する。

つまり現在のfallbackは、

> draw時に発生していた既存2-frame ringのwaitをlate input前へ移動した

という効果はあるが、

> presenter queue depthを2から1へ減らした

わけではない。

これは今回の監査で新たに確定した**P2 finding**である。

### 残存主因C

DX12 presenterは依然としてVulkanより物理的に浅い。

DX12:

```text
2 back buffers
SetMaximumFrameLatency(1)
1 DX12CommandContext for presenter
```

Vulkan:

```text
presenter FrameRing = 2
swapchain = minImageCount + 1
typical FIFO環境では3 imagesになり得る
AcquireNextImageKHR(UINT64_MAX)はdraw側で実行
```

この差は今回のcommit後も残っている。

したがって、現在のコードからは、

> **DX12の方が入力追従・frame pacing・体感performanceが良いという報告は、依然としてpresentation pipelineの深さの差と整合する**

と判定する。

---

# 2. 今回pushの確認結果

## 2.1 commit

今回HEAD:

```text
94d5e41c80260d1510b89ac528cdfc9d94cbde0b
[PERF] complete Vulkan pacing and DX12 descriptor transition fixes
```

parent:

```text
f2ec777658b26881828776439323e3996809ce35
```

したがって前回監査からの変更は1 commitである。

---

# 3. ユーザー報告のvalidation結果

ユーザーから次がPASSと報告されている。

- build
- all tests
- static audit
- USA Rev.1 runtime smoke
- resize matrix
- minimize matrix
- fullscreen matrix

この結果は今回の監査記録上、**実施済みPASS報告**として扱う。

ただし、GitHub connectorから現在HEADを確認した範囲では、

```text
combined statuses: none
workflow runs: none
```

であった。

したがって、GitHub Actions / commit statusによる独立したCI PASS確認はできていない。

これはユーザー報告を否定するものではなく、単にGitHub上に確認可能なstatus metadataが存在しないという意味である。

---

# 4. 前回指示書1: DX12 PERF-006 transition fix

## 判定

**CLOSED / PASS STATIC**

## 4.1 今回追加された設計

`DX12SurfacePresenter`に、

```cpp
void Quiesce() noexcept;
```

が追加されている。

renderer transition boundaryでは、

```text
presenter.Quiesce()
    -> queue-wide completion
presenter.InvalidateDirectDescriptorCache()
frameLease.ReleaseNow()
```

の順序になった。

これは前回指示した、

1. old renderer / presenter GPU workを完了
2. descriptor identityをinvalidate
3. output leaseをrelease

というlifetime orderingと一致する。

## 4.2 `Quiesce()`の強度

DX12 presenterの`Quiesce()`は`Commands.WaitQueueIdle()`へ到達する。

`WaitQueueIdle()`はshared Direct Queueへ新しいfence signalを入れて待つため、presenter自身の直前command-listだけでなく、そのqueue上でそれ以前に発行されたworkをretireできる。

renderer切替というcold pathでのみ実行されるため、steady-state performanceへの影響もない。

## 4.3 結論

前回の、

> renderer instanceが再生成され`ResourceGeneration`が1から再開しても、old cache identityが残る可能性

は今回の明示invalidateによって解消された。

**DX12 PERF-006 transition findingはCLOSED。**

---

# 5. 前回指示書2: Vulkan strict presenter budget

## 判定

**PARTIAL PASS**

`present_wait2` pathは設計意図を満たす。

一方、fence fallbackは**one-frame queue-depth制約としては未達**である。

---

# 6. `VK_KHR_present_wait2` path

## 判定

**PASS STATIC**

policy 4:

```cpp
PresenterOneFrameBudget = 4
```

が追加された。

`present_wait2`が利用可能な場合は、前回accepted present IDに対して、late input前にbounded waitする。

strict policyでは、wait timeoutがemulator frame intervalを元に決まり、

```text
minimum: 250 us
maximum: 16 ms
```

へclampされる。

これは前回指示した、

> previous accepted presentをlate inputより前にframe-budget内で待つ

という目的に合っている。

また、

```text
previous_present_wait_count
previous_present_wait_ns
previous_present_wait_timeout_count
```

が追加され、runtimeで実際に待っているか確認できるようになった点も良い。

---

# 7. Reflex二重待ち防止

## 判定

**PASS STATIC**

policy resolverは、Reflex active時に、

```text
Authority = NvidiaReflex
BoundedPresentWait = false
TargetTimeScheduling = false
```

とする。

Anti-Lag 2 active時も同様にvendor authorityがgeneric pacingより優先される。

したがって、

```text
present_wait2
+ Reflex sleep
```

を同一frameへ単純に二重適用する実装にはなっていない。

この点は正しい。

---

# 8. ただしReflex ONでは今回のstrict budgetは効かない

ここは今回の体感差を理解する上で非常に重要である。

現在default configは、

```cpp
NvidiaReflexMode = 1;
VulkanPresentPacingPolicy = 0;
```

である。

つまり、supported NVIDIA GPUでは通常、

```text
Reflex = On
Vulkan policy = TelemetryOnly
```

になる。

また仮にpolicy 4を手動で指定しても、Reflexがactiveならvendor authorityが勝つため、

```text
present_wait2 wait = OFF
fence fallback = OFF
Reflex sleep = ON
```

となる。

これは「二重pacingを避ける」という意味では正しい。

しかし、前回指示書でも述べた通り、

> ReflexがactiveでもVulkanだけ違和感が残る場合、presenter自身が物理的に2 frame aheadできるresource modelをA/Bする必要がある

という課題は残る。

今回のcommitではそこまでは変更していない。

したがって、**Reflex ONでDX12とVulkanを比較しているなら、今回のstrict policy追加だけで差が消えないのは不自然ではない。**

---

# 9. P2-001: fence fallbackはone-frame budgetになっていない

## Severity

**P2 / High confidence / latency**

## 9.1 現在のFrameRing

Vulkan presenterは、

```cpp
Vk::FramesInFlight = 2;
```

を使う。

`FrameRing`のslot selectionは、

```cpp
CurrentIndex = (AbsoluteFrame - 1) % Frames.size();
```

である。

`NextFrameSlotHasPendingSubmission()`も同じindexを参照する。

## 9.2 fallback実装

policy 4で`present_wait2`を使えず、GenericHost authorityの場合、

```cpp
if (Frames.NextFrameSlotHasPendingSubmission())
    Frames.WaitForNextFrameSlot();
```

となる。

しかし2-slot ringで「next reusable slot」は通常2 submissions前である。

### 時系列

```text
AbsoluteFrame=1
Frame A -> slot0 submit
AbsoluteFrame=2

Frame B -> slot1 submit
AbsoluteFrame=3

次frameのpre-input:
nextIndex=(3-1)%2=0
wait slot0 = Frame A
```

Frame Bは待たない。

その後、Frame Cをslot0へsubmitすると、

```text
slot1 = Frame B pending
slot0 = Frame C pending
```

になれる。

したがって最大pending depthは2のままである。

## 9.3 なぜ既存testがPASSしたか

今回追加された`TestPresenterOneFrameBudgetWait()`は、

- present_wait2 available時にprevious presentをwaitすること
- present_wait2 unavailable時にwait2をcallしないこと

を検証している。

一方、

> fallback FrameRingが本当にprevious presenter submissionをwaitしているか

まではunit testしていない。

static auditも、

```text
WaitForNextFrameSlot tokenが存在すること
```

を主に確認しており、2-slot ring上でのqueue-depth semanticsまでは検証していない。

よって「all tests PASS」と今回のfindingは矛盾しない。

---

# 10. P2-002: DX12とVulkanのpresenter depth差が残る

## Severity

**P2 / High confidence / likely principal residual difference**

## DX12

current DX12 presenter:

```text
SwapEffect = FLIP_DISCARD
BufferCount = 2
SetMaximumFrameLatency(1)
FrameLatencyWaitableObject
presenter DX12CommandContext = 1
```

さらに`DX12CommandContext::Begin()`は、その1つのcommand allocator/listを再利用する前にprevious submission fenceを待つ。

つまりpresenter command recording自体も基本的に1-deepである。

## Vulkan

current Vulkan presenter:

```text
FrameRing = 2
swapchain image count = minImageCount + 1
AcquireNextImageKHR timeout = UINT64_MAX
```

Vulkan code自身のcommentでも、typical FIFO swapchainは3 imagesになり得ることを前提にしている。

したがって現在も、

```text
DX12:
application/presenter depth ≈ 1

Vulkan:
presenter resources = 2
WSI images = 2～3以上
```

という構造差がある。

これはVulkanとDX12のrasterizer accuracy差ではなく、**final presentation pipelineのlatency budget差**である。

---

# 11. P2-003: fallback fence wait後もlate `AcquireNextImageKHR`が残る

## Severity

**P2 / Medium-high confidence**

Vulkan presenterの`BeginFrame()`では、late input / emulation後のdraw pathで、

```cpp
Frames.BeginFrame();
AcquireNextImageKHR(
    ...,
    UINT64_MAX,
    ...);
```

を実行する。

今回のpre-input fence fallbackはGPU submission fenceを待つだけである。

GPU command submissionが完了したことと、presentation engine側でswapchain imageがapplicationへ再acquire可能になったことは同一条件ではない。

そのため、fallback pathでは、

```text
pre-input:
    old presenter GPU fence wait

input sample
simulation
render

draw:
    AcquireNextImageKHR(UINT64_MAX)
        -> WSI image shortageならここでblock
```

という経路が残る。

このblockは入力取得後なので、発生するとfresh inputを持ったframeがdisplayへ出るまでの時間を増やす。

今回追加された、

```text
acquire_wait_count
acquire_wait_ns
```

は、この仮説をruntimeで判定するために非常に有用である。

---

# 12. P3-001: strict fence fallback timeoutが1 frameではなく1秒

`WaitForNextFrameSlot()`は、

```cpp
WaitForFences(..., FenceTimeoutNanoseconds)
```

を使う。

timeout時logも、

```text
did not complete within 1s
```

となっている。

一方、`PresenterOneFrameBudget`のpresent_wait2側は最大16msである。

したがって、fallbackには、

```text
policy名/目的: one-frame budget
実際のhard timeout: 1 second
```

というsemantic mismatchがある。

通常GPUが正常なら1秒待つことはないので、steady-stateの主因ではない。

しかしGPU saturation / driver stall時には、low-latency policyが大きなjankやrenderer failureへ変わり得る。

修正すべきである。

---

# 13. P3-002: legacy present-wait capability ladderがない

現在のgeneric Vulkan present pacingは主に、

```text
VK_KHR_present_id2
VK_KHR_present_wait2
VK_EXT_present_timing
```

を利用する。

`present_wait2`がない環境は、そのままpresenter fence fallbackへ落ちる。

環境によってはlegacy `present_wait`系を利用できる可能性があるため、capability ladderを追加する余地がある。

推奨順位:

```text
1. present_wait2
2. legacy present-wait path if supported and correlation contract is valid
3. latest presenter submission fence
4. no behavioral wait / telemetry only
```

ただしextension dependency、present-id generation、swapchain recreation contractを必ず別途確認してから導入すること。

---

# 14. P3-003: Vulkan presenter transient descriptor poolを毎frame resetしている

PERF-006によりdirect output descriptorはpersistent cache化された。

しかしVulkan presenter `BeginFrame()`では現在も毎frame、

```cpp
ResetDescriptorPool(Device, DescriptorPools[frameIndex], 0);
```

を実行する。

一方DX12 presenterはpersistent descriptor prefixを維持し、steady-state frameでは、

```cpp
Descriptors.Reset(PersistentDescriptorCount);
```

というCPU-side bump pointer resetだけで済む。

Vulkan transient descriptor poolは、cache overflow / unusual fallback用として残す価値はあるが、steady-state direct pathで一度もtransient allocationしていないslotまで毎framedriver callでresetする必要はない。

これは大きなlatency主因ではないが、DX12との差を詰めるP3 optimization candidateである。

### 推奨

slotごとに、

```cpp
bool TransientDescriptorPoolUsed = false;
```

を保持し、

```text
used == true
    -> next reuse時だけvkResetDescriptorPool
used == false
    -> reset callをskip
```

する。

fallback correctnessは維持すること。

---

# 15. Throughput performanceについて静的監査で確定できること

ユーザーの「performanceが良い」が、

- input latency
- frame pacing
- stutter
- CPU usage
- GPU frame time
- minimum FPS

のどれを主に指すかで原因は変わる。

今回の静的監査で高い確度で説明できるのは、**input-to-present / presentation pacing側の差**である。

一方、pure GPU throughputについては、現在のtelemetryだけではVulkanとDX12を完全比較できない。

特に`VulkanPerf.h`にも、

```text
CaptureSidecarGpuTimeNs
PresentedScreenCopyGpuTimeNs
```

がfuture calibrated timestamp用としてreserveされており、完全なGPU stage timingはまだ実装されていない。

したがって、

> Vulkan compute shader自体がDX12 HLSLより何ms遅い

といった結論は、今回のstatic auditだけでは出さない。

---

# 16. Vulkan / DX12 renderer内部の大きな非対称は少ない

## 16.1 rasterizer frame depth

Vulkan 3D renderer自身は、

```cpp
RendererFramesInFlight = 1;
```

である。

これはshared intermediate buffersのcorrectness requirementで、DX12側もsingle main command contextを使うため、ここを安易に増やすべきではない。

今回の残存差の中心は**renderer raster queueではなくpresenter**である。

## 16.2 compositor slots

Vulkan:

```cpp
CompositorFramesInFlight = 3;
```

DX12:

```cpp
kCompositorFramesInFlight = 3;
```

であり、compositor slot countは対称である。

したがって、3-slot compositorそのものをVulkanだけの原因とは判定しない。

## 16.3 direct output

両backendともPERF-002後はGPU-native direct sampled outputを持つ。

Vulkan:

```text
Top direct image
Bottom direct image
```

DX12:

```text
2-slice DirectTexture
```

というresource shapeの差はあるが、いずれも高解像度frame全体をCPUへ戻すpathではない。

ここは第一優先ではない。

---

# 17. NVIDIA Reflexは今回壊れていないか

## 判定

**PASS STATIC / NO NEW REGRESSION FOUND**

今回commitではgeneric presenter pacingの追加が主であり、Reflex pathそのものはvendor authorityとして優先されている。

Vulkan Reflexは引き続き、

```text
sleep
INPUT_SAMPLE
SIMULATION_START
SIMULATION_END
RENDERSUBMIT_START
RENDERSUBMIT_END
PRESENT_START
vkQueuePresentKHR
PRESENT_END
```

という設計を維持している。

またpolicy 4でもReflex active時にgeneric `present_wait2` / fence waitを重ねない。

したがって今回の修正によってReflexが静的に破壊された証拠はない。

ただし重要なのは、

> Reflexが壊れていない

ことと、

> VulkanとDX12でReflex使用時の最終latencyが同じ

ことは別である。

Vulkan presenter側のphysical frame depthが2のままであるため、vendor pacingだけでは吸収しきれない差が残る可能性は依然ある。

---

# 18. 最優先の追加修正指示

## Phase 1: fence fallbackを本当にone-frameへする

### 現在

```cpp
WaitForNextFrameSlot()
```

### 問題

2-slot ringなので2 frame前を待つ。

### 必要なAPI

例:

```cpp
enum class FrameWaitResult
{
    Ready,
    Timeout,
    Error,
};

FrameWaitResult WaitForLatestSubmittedFrame(VkDeviceSize timeoutNs);
```

または同等機構。

重要なのは、

```text
next reusable slot
```

ではなく、

```text
most recently submitted presenter frame
```

を対象にすることである。

`SubmitFrame()`時に、

```cpp
LastSubmittedIndex
```

を保存する方式でもよい。

### 禁止

slot indexを暗黙計算だけで済ませ、ring size変更で意味が変わる設計にはしない。

---

# 19. Phase 2: Reflex ON + presenter depth=1 A/B

現在はReflex active時にgeneric waitを完全に外している。

これは二重pacing防止として正しいが、Vulkan presenterの2-slot physical depthもそのままになる。

次のdeveloper A/Bを追加する。

### A

```text
Reflex ON
presenter depth current behavior
```

### B

```text
Reflex ON
previous presenter GPU submissionをpre-inputでretire
その後Reflex sleep
```

ここでBは`present_wait2`をReflexへ重ねるのではなく、**application-side presenter submission depthだけを1へ制限するexperiment**とする。

結果を見てproduction behaviorを決める。

### 測定必須

- input-to-present
- frame time p50/p95/p99
- presenter fence wait
- acquire wait
- Reflex driver timings
- dropped / skipped present count

---

# 20. Phase 3: presenter-local `FramesInFlight=1` A/B

global:

```cpp
Vk::FramesInFlight
```

は変更しないこと。

代わりにpresenter専用に、

```cpp
static constexpr u32 PresenterFramesInFlight = 1;
```

のA/Bを行う。

現在、Vulkan presenter headerの、

```text
DescriptorPools
Staging
```

は`Vk::FramesInFlight`サイズなので、presenter-local constant導入時は関連array index contractも同時に監査する。

### 注意

単にring=1へするだけで、waitがdraw後へ移ると逆にinput latencyを悪化させる可能性がある。

したがって、**ring=1を採用するならpre-input back-pressureとの組み合わせで検証すること。**

---

# 21. Phase 4: late acquireをblockさせないA/B

現在:

```cpp
AcquireNextImageKHR(..., UINT64_MAX, ...)
```

である。

低遅延developer experimentとして、

```text
short bounded timeout
または
non-blocking acquire
```

を検討する価値がある。

image unavailableなら、fresh inputを読んだ後に長時間blockするのではなく、presentをskipして最新renderer outputを次回へ持ち越す方式である。

ただしこれはstutter / frame-drop trade-offがあるため、いきなりproduction defaultにはしない。

### acceptance

- no invalid semaphore reuse
- no command-buffer state leak
- no swapchain image ownership violation
- resize/minimize/fullscreen正常
- no visible stale-frame burst
- latency p95改善
- frame-drop率を記録

---

# 22. Phase 5: Windows限定swapchain image count A/B

現在Vulkanは、

```cpp
imageCount = caps.minImageCount + 1;
```

である。

DX12は2 buffersである。

Windows + low-latency developer policyに限って、surfaceが許す場合、

```text
2 images
vs
minImageCount + 1
```

をA/Bする価値がある。

### 禁止

- Linux / Wayland / BSD / macOSへ一律2 imagesをhard-codeしない
- surface `minImageCount`を無視しない
- `maxImageCount`を無視しない

throughput低下やVBlank miss増加があるならcurrent behaviorを維持する。

---

# 23. Phase 6: GPU timestamp telemetry

現在の主なCPU telemetryに加えて、DX12 / Vulkan双方へ同等のGPU timestamp queryを追加する。

最低限:

```text
Raster GPU time
CaptureSidecar GPU time
StructuredCompositor GPU time
Presenter render-pass GPU time
Total queue GPU span
```

scaleごとに、

```text
1x
4x
8x
16x
```

を比較する。

これにより、

> 体感差がpresentation latencyなのか

と、

> Vulkan shader executionそのものが遅いのか

を分離できる。

---

# 24. 追加telemetryの推奨

既存`MELONPRIME_PERF=1`へ次も追加するとよい。

```text
presenter_latest_submission_wait_count
presenter_latest_submission_wait_ns
presenter_latest_submission_wait_timeout_count
presenter_budget_miss_count
acquire_not_ready_count
present_skipped_for_latency_budget_count
```

さらにbackend共通summaryとして、

```text
renderer = Vulkan / DX12
vsync
present mode
vendor pacing authority
Reflex mode
swapchain/backbuffer count
presenter logical depth
```

を1行へ出す。

A/B結果の取り違えを防げる。

---

# 25. 現在追加済みtelemetryの使い方

Vulkanは`MELONPRIME_PERF=1`でtelemetryを有効化できる。

今回追加された重要項目:

```text
presenter_frame_fence_wait_count
presenter_frame_fence_wait_ns
acquire_wait_count
acquire_wait_ns
previous_present_wait_count
previous_present_wait_ns
previous_present_wait_timeout_count
swapchain_image_count
presenter_frames_in_flight
pacing_authority
present_mode
```

## 読み方

### case A

```text
pacing_authority = NvidiaReflex
previous_present_wait_count = 0
presenter_frames_in_flight = 2
```

これは現在のReflex expected path。

### case B

policy 4、Reflex OFF、present_wait2 available:

```text
previous_present_wait_count > 0
pacing_authority = GenericPresentTiming
```

### case C

policy 4、Reflex OFF、present_wait2 unavailable:

```text
previous_present_wait_count = 0
presenter_frame_fence_wait_count > 0
```

ただし現実装では、このfence waitはlatest submissionではなくnext reusable slotなので、**このcounterが増えてもone-frame queue depthが実現した証拠にはならない。**

ここは非常に重要。

---

# 26. 推奨A/B matrix

同一PC、同一display、同一ROM、同一scene、同一scaleで比較する。

## Latency matrix

| ID | Renderer | Reflex | Vulkan policy | VSync | 目的 |
|---|---|---:|---:|---:|---|
| D0 | DX12 | Off | - | Off | baseline |
| D1 | DX12 | On | - | Off | DX12 Reflex baseline |
| D2 | DX12 | On | - | On | DX12 Reflex + VSync |
| V0 | Vulkan | Off | 0 | Off | current generic baseline |
| V1 | Vulkan | Off | 4 | Off | strict generic |
| V2 | Vulkan | Off | 4 | On | strict FIFO |
| V3 | Vulkan | On | 0 | Off | current Vulkan Reflex |
| V4 | Vulkan | On | 4 | Off | current resolver上はReflex authority |
| V5 | Vulkan | On | experimental physical depth=1 | Off | 最重要A/B |
| V6 | Vulkan | On | experimental physical depth=1 | On | VSync comparison |

## scale matrix

最低限:

```text
1x
4x
8x
16x
```

4xだけでなく高scaleを含めることで、present pacing差とGPU throughput差を分離しやすい。

---

# 27. 修正してはいけないもの

今回の追加performance workで、次は触らない。

## 27.1 global renderer frames-in-flight

```cpp
Vk::FramesInFlight = 1
```

へglobal変更しない。

## 27.2 renderer correctness math

変更禁止:

- fixed-point interpolation
- polygon edge rules
- depth
- fog
- blend
- shadow
- AA
- texture coordinate semantics
- Display Capture ordering
- capture source selection

## 27.3 compositor semantics

- top / bottom routing
- structured 2D provenance
- screen routing
- same-frame capture ordering

をperformance目的で変更しない。

## 27.4 Reflex marker order

現在のmarker placementを「体感が悪いから」という理由だけで移動しない。

## 27.5 per-frame queue/device idle

禁止:

```text
vkQueueWaitIdle every frame
vkDeviceWaitIdle every frame
```

latency queueは浅く見えてもjitter / throughputを破壊する。

---

# 28. 追加unit test指示

## 28.1 fallback slot semantics test

現在欠けている。

2-slot ringをpure model化し、

```text
submit F1 slot0
submit F2 slot1
strict one-frame wait target
```

が、

```text
F1ではなくF2
```

を選ぶことをtestする。

### 必須assert

```text
latest submitted frame is the target
next reusable slot is NOT used as one-frame-budget definition
```

## 28.2 Reflex authority test

Reflex active:

```text
present_wait2 calls = 0
latest presenter resource-depth experiment = configured behavior only
Reflex sleep = 1/frame when presentation contract permits
```

## 28.3 timeout test

one-frame fallback timeoutが、

```text
<= target frame budget
```

であることをtestする。

1秒constantへ退行したらfailさせる。

---

# 29. runtime acceptance criteria

追加修正をproductionへ入れる条件:

## Correctness

- Software Renderingとの差分なし
- existing raster differential PASS
- Display Capture PASS
- Custom HUD PASS
- radar overlay PASS
- savestate PASS
- renderer switch PASS
- resize PASS
- minimize/restore PASS
- fullscreen toggle PASS

## Low latency

DX12 baselineと比較し、Vulkanの、

```text
input-to-present p50
input-to-present p95
input-to-present p99
```

が改善すること。

## Frame pacing

- p99 frame time regressionなし
- long stall増加なし
- acquire timeout / frame skip率が許容範囲

## Reflex

- requested On -> actual active
- On+Boost -> actual active
- generic previous-present waitとの二重化なし
- marker ordering維持
- swapchain recreation後も再arm

---

# 30. 優先順位

## Priority 1

**fence fallbackを`next reusable slot`から`latest submitted presenter frame`へ修正。**

これが今回の最重要source finding。

## Priority 2

**Reflex ONでphysical presenter depth=1 A/B。**

ユーザーが現在感じているDX12との差に最も直接的。

## Priority 3

**late `AcquireNextImageKHR` waitをtelemetryで確認し、必要ならbounded/nonblocking acquire A/B。**

## Priority 4

**presenter-local ring=1 / swapchain 2 imagesをWindows限定A/B。**

## Priority 5

legacy present wait capability fallback。

## Priority 6

transient descriptor-pool lazy reset。

## Priority 7

Vulkan / DX12共通GPU timestamp stage profiler。

---

# 31. finding summary

| ID | Severity | Finding | 判定 |
|---|---|---|---|
| FIX-DX12-001 | - | renderer transition quiesce + cache invalidate | **CLOSED** |
| FIX-VK-001 | - | present_wait2 previous-present wait | **PASS STATIC** |
| FIX-VK-002 | - | Reflex generic wait二重化防止 | **PASS STATIC** |
| VK-PERF-001 | **P2** | fallbackがnext reusable slotを待ち、2→1 depthにならない | **OPEN** |
| VK-PERF-002 | **P2** | Vulkan presenter 2-deep vs DX12 presenter / DXGI 1-deep | **OPEN** |
| VK-PERF-003 | **P2** | fallback後もpost-input `AcquireNextImageKHR(UINT64_MAX)` block可能 | **OPEN** |
| VK-PERF-004 | P3 | fallback timeoutが1 frameでなく1秒 | **OPEN** |
| VK-PERF-005 | P3 | legacy present-wait fallbackなし | **OPEN / OPTIONAL** |
| VK-PERF-006 | P3 | transient descriptor poolをsteady-stateでも毎frame reset | **OPEN / LOW** |
| MEASURE-001 | P3 | GPU stage timeのDX12/Vulkan対称測定不足 | **OPEN** |

---

# 32. 最終判定

今回のpushについては、**前回の2件の指示書に対する実装そのものは概ね成功している。**

DX12 PERF-006 transition bugは閉じられた。

Vulkan `present_wait2` pathも正しくprevious accepted presentへback-pressureを移し、Reflexとのgeneric double waitも回避している。

しかし、VulkanとDX12の体感差がまだ残ることについて、今回の再監査ではさらに具体的な原因が判明した。

特に、

```text
WaitForNextFrameSlot()
```

は2-slot presenter ring上では「直前frame」を待っていない。

そのためpresent_wait2非対応fallbackでは、**one-frame budgetという名前に対して実際のmaximum application presenter depthは2のまま**である。

さらに、

```text
Vulkan default policy = TelemetryOnly
NVIDIA Reflex default = On
Reflex active時はgeneric strict waitを意図的に無効化
Vulkan presenter FramesInFlight = 2
DX12 SetMaximumFrameLatency(1)
DX12 presenter command context = 1
```

という差が現在も残る。

したがって、現時点の最も合理的な結論は、

> **今回の修正が失敗したのではなく、修正範囲がgeneric strict-pacing prototypeまでであり、DX12が持つ実質的なone-frame presenter depthをVulkanの通常/Reflex pathへまだ完全には再現できていない。**

である。

次の実装は、rasterizerやaim処理を変更するのではなく、

1. fallback wait targetをlatest presenter submissionへ修正
2. Reflex active時のphysical presenter depth=1 A/B
3. post-input acquire waitの除去 / bounded化A/B

の順で行うのが最も安全である。

---

# 33. 総合判定

```text
PASS WITH REMAINING VULKAN P2 LATENCY / PERFORMANCE FINDINGS

P1 correctness findings: 0
DX12 transition finding: CLOSED
Vulkan present_wait2 strict path: PASS STATIC
Vulkan Reflex regression: NOT FOUND
Vulkan fallback true one-frame budget: NOT ACHIEVED
Primary residual difference: Vulkan presenter / WSI queue depth
```

---

# 34. 対応完遂後の実装・検証結果（2026-08-15）

## 34.1 判定の読み替え

本書の 1〜33 章は、実装前の残存差を記録した履歴として保持する。
以下は、その指示書に対して今回実装した結果の最終追記である。

総合判定は、

```text
IMPLEMENTED WITH RUNTIME / PHYSICAL-PLATFORM COVERAGE OPEN
P1 correctness findings: 0
source and unit-test requirements: PASS
hardware-dependent runtime acceptance: NOT RUN / OPEN
```

とする。ソース、構成ビルド、device-free unit、fake dispatch、static audit
で確認できる範囲は完遂したが、実GPU上のVulkan WSI、DX12 GPUView/Reflex、
入力から表示までの p50/p95/p99、ROM表示差分、savestate、renderer switch、
resize/minimize/fullscreen の今回HEADでの再実行結果は、この作業環境からは
取得していない。したがって、これらを実機 PASS へ昇格させない。

## 34.2 finding 更新

| ID | 今回の対応 | 今回の判定 |
|---|---|---|
| FIX-DX12-001 | renderer transition の queue quiesce と direct descriptor cache invalidate を維持 | **CLOSED / STATIC PASS** |
| FIX-VK-001 | `present_wait2` の accepted previous-present wait を維持 | **CLOSED / STATIC PASS** |
| FIX-VK-002 | Reflex / Anti-Lag authority 時に generic wait を重ねない制御を維持 | **CLOSED / STATIC PASS** |
| VK-PERF-001 | `WaitForLatestSubmittedFrame()` を追加し、strict fallback は next reusable slot ではなく最新 submit の fence を frame budget 内で待つ | **CLOSED / STATIC + UNIT PASS; physical runtime OPEN** |
| VK-PERF-002 | presenter-local `MELONPRIME_VULKAN_PRESENTER_FRAMES_IN_FLIGHT=1` A/B を追加。既定値は2のまま、global `Vk::FramesInFlight` は変更しない | **IMPLEMENTED A/B; runtime NOT RUN / OPEN** |
| VK-PERF-003 | `MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS` による bounded acquire A/B を追加。timeout/NOT_READY 時は semaphore を wait に流用せず dependency-free submit で安全に skip | **IMPLEMENTED A/B; runtime NOT RUN / OPEN** |
| VK-PERF-004 | strict wait timeout を frame interval 由来の 250us〜16ms に制限し、1秒 fallback を除去 | **CLOSED / STATIC + UNIT PASS; physical stall runtime OPEN** |
| VK-PERF-005 | `VK_KHR_present_wait2` → legacy `VK_KHR_present_wait` → latest-submission fence → no wait の ladder と runtime downgrade を追加 | **CLOSED / STATIC + FAKE-DISPATCH PASS; physical extension coverage OPEN** |
| VK-PERF-006 | transient descriptor pool は使用した slot のみ再利用境界で reset し、reset count を出力 | **CLOSED / STATIC PASS; runtime telemetry NOT RUN** |
| MEASURE-001 | Vulkan/DX12 両方に Raster、CaptureSidecar、StructuredCompositor、Presenter render-pass、Total queue span の timestamp query を追加 | **IMPLEMENTED / BUILD PASS; GPU measurements NOT RUN / OPEN** |

## 34.3 実装した A/B と telemetry

developer-only の既定値を変えず、次の環境変数で個別実験できる。

```text
MELONPRIME_VULKAN_PRESENTER_FRAMES_IN_FLIGHT=1
MELONPRIME_VULKAN_PRESENTER_PREINPUT_WAIT=1
MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS=<nanoseconds>
MELONPRIME_VULKAN_SWAPCHAIN_IMAGE_COUNT=2
MELONPRIME_PERF=1
```

Vulkan の perf report には、latest-submission wait の count/ns/timeout、
budget miss、acquire NOT_READY、latency-budget skip、lazy descriptor reset、
GPU stage 時間を追加した。Vulkan/DX12 共通 summary 行には renderer、VSync、
present mode、vendor pacing authority、Reflex mode、swapchain/backbuffer 数、
presenter logical depth を出す。

`MELONPRIME_VULKAN_PRESENTER_FRAMES_IN_FLIGHT=1` は presenter の `FrameRing`
だけに適用し、renderer/compositor の global frames-in-flight や数学的な
compositor/raster semantics は変更していない。steady-state の per-frame
`vkQueueWaitIdle`、`vkDeviceWaitIdle`、DX12 queue idle は追加していない。

## 34.4 実行した検証

```text
cmake --build build/debug-mingw-vulkan-validation2 --target melonPrimeDS.exe -j 1
  PASS (80/80, exit code 0)

cmake --build build/debug-mingw-vulkan-validation2 \
  --target melonprime_vulkan_present_timing_check -j 1
  PASS: Vulkan present timing model tests PASS

cmake --build build/debug-mingw-vulkan-validation2 \
  --target melonprime_vulkan_present_pacer_dispatch_check -j 1
  PASS: Vulkan present pacer fake-dispatch tests passed

python tools/ci/audits/audit-low-latency-contract.py
  PASS: Low-latency contract audit PASS

git diff --check
  PASS: whitespace error なし（LF/CRLF の変換警告のみ）
```

追加した pure unit の必須 assert は、F1/F2 submit 後の strict wait target
が F1 ではなく最新の F2 になること、next reusable slot を one-frame budget
の定義に使わないこと、timeout が frame budget 以下であることを確認する。
fake dispatch test は present_wait2 の runtime downgrade 後に legacy wait へ
移行できることも確認する。

## 34.5 未完了ではなく、証拠待ちとして残す項目

この追記の **OPEN / NOT RUN** はコード未実装を意味しない。実機または承認済み
GPU runner が必要な次の証拠を意味する。

- USA Rev.1 を使った今回HEADの Vulkan/DX12 correctness differential、F3/F4/F7
  savestate、renderer switch、resize、minimize/restore、fullscreen toggle。
- NVIDIA Reflex On / On+Boost の actual active、marker ordering、swapchain
  recreation 後の再arm、generic previous-present wait の実呼び出し数。
- Vulkan の presenter depth 1/2、2-image swapchain、bounded acquire の A/B に
  おける input-to-present p50/p95/p99、p99 frame time、long stall、skip 率。
- GPU timestamp の 1x/4x/8x/16x 実測値と、Vulkan/DX12 の同一シーン比較。
- AMD/Intel/Linux など、今回の Windows NVIDIA 側コード検査だけでは権威付け
  できない physical-platform coverage。

これらは別途実機ログを取得するまで **NOT RUN / OPEN** とし、別SHAのCIや
ユーザー報告だけで今回HEADの runtime PASS へ置換しない。
