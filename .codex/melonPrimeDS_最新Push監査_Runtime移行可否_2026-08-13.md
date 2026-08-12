# melonPrimeDS 最新Push監査
## Vulkan Present Pacing Fail-Soft / Device-Lost / Runtime移行可否

- 対象リポジトリ: `ag-advania/melonPrimeDS`
- 対象ブランチ: `develop_remakeVulkan_ver2`
- 監査HEAD: `6437a75ed59c2672e660da147cba8868960e29f3`
- 親HEAD: `f4d8c4078baa1de2e4ca0af5b87d2c09879d8c61`
- 前回機能監査HEAD: `366d6a3012b3442191266979abfa1cd9b2fa7eb9`
- 最新コミット: `Vulkan: fail soft on timing queue allocation and propagate device loss`
- 監査日: 2026-08-13
- 対象backend: **現行Vulkan clean-room backend**

---

# 0. 対応状況 — CLOSED (2026-08-13)

> この監査書の指摘は対応済み。以下は追記であり、本文は監査時点のまま。

## 指摘への対応

| 指摘 | 優先度 | 対応 | コミット |
|---|---|---|---|
| §18-20 `reportCount == 0` を「queue drained」と読むのは仕様上強すぎる | P2 | `OutstandingTimedPresents` を追加し、polling 停止条件を `outstanding == 0 && reportCount == 0` へ変更。§20 が「最も堅い」とした outstanding counter 方式 | `639a6e8b6` |
| §36 Runtimeへ進むGate（静的側11項目） | — | 監査どおり充足と確認。Validation へ進んだ | — |

## この監査が承認した次工程の実施結果

§38 の推奨に従い `melonPrimeDS_Vulkan_ValidationLayer_NVIDIA実機AB_実施指示書_2026-08-13.md`
を実行した。結果は
`melonPrimeDS_Vulkan_ValidationLayer_NVIDIA実機AB_実施結果_2026-08-13.md`。

要点:

- Phase 1 Core / Synchronization Validation とも **ERROR 0 / hazard 0**
- ただし実行中に実在の VUID を1件発見・修正
  （`VUID-VkPresentTimingInfoEXT-timeDomainId-12400`、`639a6e8b6`）。
  **出荷デフォルトの TelemetryOnly でも常時発生していた**
- §37「Validation 前に必須修正はあるか → P0/P1 blocker なし」という判定は
  静的監査の範囲では正しかったが、実機 Validation は静的監査が原理的に
  検出できない欠陥を1件出した。この監査書が A/B より先に Validation を
  置いた順序判断は妥当だったと確認された
- Phase 3 A/B は **BLOCKED**。surface が `presentAtAbsoluteTimeSupported=false`
  を返すため、このドライバでは JustInTime が PresentWait と定義上同一挙動になる

---

# 1. 結論

今回のPushでは、前回監査で指摘した:

```text
P1a  timing results queue初回allocation failureのfail-soft不足
P1b  vkWaitForPresent2KHRのVK_ERROR_DEVICE_LOSTをswapchain out-of-dateと混同
P2   timing永久disable後の不要polling
P3   time-domain列挙のVK_INCOMPLETE handling
```

へ実装対応が入っている。

静的監査結果:

```text
P1a Initial timing queue allocation failure     FIXED / STATIC PASS
P1b DEVICE_LOST propagation                     FIXED / STATIC PASS
P2  Permanent timing polling                    IMPLEMENTED
P3  Time-domain VK_INCOMPLETE retry             FIXED / STATIC PASS
Target-Time JIT                                 PASS
PresentWait2 / Target-Time separation           PASS
Reflex / Anti-Lag authority                     PASS
Host FPS limiter ownership                      PASS
Pure timing/policy test                         IMPLEMENTED
Validation Layer runtime                        NOT RUN
NVIDIA runtime                                  NOT RUN
Latency A/B                                     NOT RUN
```

**静的なP0/P1 blockerは今回確認した範囲では残っていない。**

したがって次工程は:

```text
Validation Layer
↓
NVIDIA実機 functional validation
↓
NVIDIA A/B measurement
```

へ進めてよい。

ただしP2として1点、runtime blockerではないが仕様上の注意を残す。

> `vkGetPastPresentationTimingEXT()`である1回のpollが`reportCount == 0`でも、以前投入したtiming requestの結果が将来出てこないことまでは証明しない。

`VK_EXT_present_timing`のfeedbackは非同期で、結果のavailabilityに厳密な時刻関係はない。現在コードはtiming metadataを永久OFFにした後、`reportCount == 0`を「queue drained」と解釈してpollingを停止する。

この状態では新規timing requestは発生しないため、**Target-Time JIT correctnessやrenderer correctnessのblockerではない**。ただし、未availableの古いresultsが残っている可能性はあるため、コメントの「queue drained empty」は仕様上やや強い。

優先度:

```text
P2 / cleanup robustness
```

Validation Layer / NVIDIA A/B開始を止める必要はない。

---

# 2. 最新HEAD確認

branch:

```text
develop_remakeVulkan_ver2
```

HEAD:

```text
6437a75ed59c2672e660da147cba8868960e29f3
```

commit:

```text
Vulkan: fail soft on timing queue allocation and propagate device loss
```

このcommit自身が前回監査の:

```text
P1 x2
P2
P3
```

対応を目的としている。

---

# 3. HEAD直前のcleanup commit

親commit:

```text
f4d8c4078baa1de2e4ca0af5b87d2c09879d8c61
remove md
```

これはrepository内に一時的に置かれていた生成済み監査Markdownを削除したcleanup。

runtime Vulkan codeの修正本体は最新:

```text
6437a75e...
```

。

---

# 4. P1a — Initial timing queue allocation failure

## 前回問題

以前はswapchain作成時:

```text
TimingMetadataEnabled = true
TimingResultsQueryEnabled = true
↓
vkSetSwapchainPresentTimingQueueSizeEXT()
```

の順になり得た。

そのため内部timing results queue確保が失敗しても:

```text
timing metadata ON
results polling ON
```

が残る可能性があった。

---

# 5. `ApplyTimingQueueSize()`返却値

現在:

```cpp
bool VulkanPresentPacer::ApplyTimingQueueSize(u32 size)
```

へ変更。

成功時のみ:

```text
TimingQueueSize = size
TimingQueueAllocated = true
return true
```

。

failure:

```text
return false
```

。

callerがallocationの成否を判別できる。

**評価: PASS**

---

# 6. Swapchain初期state

`OnSwapchainCreated()`では最初に:

```text
TimingMetadataEnabled = false
TimingResultsQueryEnabled = false
```

。

そして:

```text
ApplyTimingQueueSize(...)
```

成功時だけ:

```text
TimingMetadataEnabled = true
TimingResultsQueryEnabled = true
RefreshTimingProperties()
RefreshTimeDomains()
```

へ進む。

**評価: PASS**

---

# 7. 初回allocation failure

失敗時:

```text
TimingQueueRecoveryPending = false
TargetSchedulingLifecycleFailed = true
```

。

timing metadata/queryはOFFのまま。

つまり:

```text
present timing queueなし
↓
timing metadataを付けない
↓
Target-Time JITを使わない
↓
普通のVulkan presentation継続
```

。

renderer全体をSoftwareへfallbackさせない。

これは期待するfail-soft。

**評価: PASS**

---

# 8. Initial failureとgrowth failureの分離

新規state:

```text
TimingQueueAllocated
```

。

これにより:

## Initial allocation failure

```text
queueそのものが存在しない
→ timing metadataを有効にしない
```

## Growth failure

```text
旧queueは存在
→ queue自体は有効
→ metadata再有効化だけ見送る
```

を区別。

**設計: PASS**

---

# 9. FIFO_LATEST_READY lifecycle

初回queue確保失敗時:

```text
TargetSchedulingLifecycleFailed = true
```

。

同surfaceでswapchainを作り直すたび:

```text
FIFO_LATEST_READY
→ timing lifecycle failure
→ recreate
→ FIFO_LATEST_READY
```

を繰り返さない。

**評価: PASS**

---

# 10. P1b — Device Lost propagation

## 前回問題

`VulkanPresentPacer::BeginFrame()`はboolで:

```text
true
=
swapchain rebuild required
```

という契約だった。

しかし:

```text
VK_ERROR_OUT_OF_DATE_KHR
VK_ERROR_DEVICE_LOST
```

の両方を`true`にしていた。

Presenter側では:

```text
SwapchainDirty = true
```

。

lost device上でswapchainだけrecreateしても根本回復しない。

---

# 11. Result enum

追加:

```cpp
enum class VulkanPacerBeginResult
{
    Continue,
    SwapchainOutOfDate,
    DeviceLost,
};
```

**評価: PASS**

---

# 12. Presenter action

追加pure helper:

```cpp
VulkanPacerActionFor()
```

mapping:

```text
Continue
→ rebuild=false
→ fail=false

SwapchainOutOfDate
→ rebuild=true
→ fail=false

DeviceLost
→ rebuild=false
→ fail=true
```

Vulkan deviceなしでunit test可能。

**評価: PASS**

---

# 13. `vkWaitForPresent2KHR`

現在:

```text
VK_SUCCESS
→ Continue

VK_TIMEOUT
→ Continue

VK_ERROR_OUT_OF_DATE_KHR
→ SwapchainOutOfDate

VK_ERROR_DEVICE_LOST
→ DeviceLost

その他optional wait failure
→ DisableWait()
→ Continue
```

。

DEVICE_LOSTとoptional extension failureを分離できている。

**評価: PASS**

---

# 14. DEVICE_LOST時に`DisableWait()`しない

これは重要。

DEVICE_LOSTを:

```text
generic waitだけ壊れた
```

として扱うと:

```text
target scheduling continues without it
```

という誤った含意になる。

現在はDEVICE_LOST時:

```text
TargetSchedulingActive = false
→ DeviceLost
```

として上位へ伝播。

**評価: PASS**

---

# 15. Presenter routing

Presenter:

```text
Pacer Begin
↓
VulkanPacerActionFor()

RebuildSwapchain
    → SwapchainDirty=true

FailRenderer
    → Fail("vkWaitForPresent2KHR", VK_ERROR_DEVICE_LOST)
```

。

既存Vulkan runtime-failure pathへ入る。

**評価: PASS**

---

# 16. Unit test

追加:

```text
TestBeginResultRouting
```

確認対象:

```text
Continue
SwapchainOutOfDate
DeviceLost
```

特に:

```text
DeviceLost
→ FailRenderer=true
→ RebuildSwapchain=false
```

を固定。

**評価: PASS**

---

# 17. P2 — Permanent timing polling

最新実装では:

```text
!TimingMetadataEnabled
&&
!TimingQueueRecoveryPending
&&
reportCount == 0
```

の場合:

```text
TimingResultsQueryEnabled = false
```

として毎frame:

```text
vkGetPastPresentationTimingEXT
```

を止める。

目的:

```text
timingを永久disable済み
↓
不要なdriver queryを残さない
```

。

---

# 18. P2仕様上の残注意

ここは「完全なqueue drain」と断言しない方がよい。

Khronos仕様ではpresentation timing feedbackは非同期。

`vkGetPastPresentationTimingEXT()`は:

```text
newly-available results
```

を返す。

したがってある瞬間:

```text
presentationTimingCount == 0
```

でも、

```text
以前timing metadata付きでpresentしたrequestが
将来availableになる
```

可能性はある。

仕様ではtiming feedback availabilityに厳密な時間関係は要求されず、有限時間内にavailableになることだけが要求される。

---

# 19. P2影響

現在このstop条件へ入る時は:

```text
TimingMetadataEnabled = false
TimingQueueRecoveryPending = false
```

。

つまり新しいtiming requestはもう投入しない。

そのため:

```text
renderer correctness
target-time scheduling correctness
FPS limiting
present correctness
```

への直接影響はない。

残る可能性:

```text
未availableの旧timing resultsを回収せずswapchain破棄まで残す
```

。

**P2 / NON-BLOCKING**

---

# 20. P2推奨改善

将来整理するなら:

```text
OutstandingTimedPresents
```

をpresentation sequence / logical IDから明示追跡。

例:

```text
timing metadata付きpresent accepted
→ outstanding++

reportComplete取得
→ outstanding--

metadata permanent OFF
&& recovery OFF
&& outstanding == 0
→ polling OFF
```

。

または軽量案:

```text
metadata permanent OFF後
N consecutive empty polls
```

。

ただし後者は仕様的な完全証明ではない。

最も堅いのはoutstanding counter。

---

# 21. P3 — Time-domain `VK_INCOMPLETE`

最新コードは:

```text
count
↓
allocate
↓
query
```

を最大3回retry。

`VK_INCOMPLETE`時はpartial subsetを最終結果として採用しない。

優先time-domainがtruncate側に落ちる問題を回避。

retry exhausted時:

```text
TimeDomainsRetryPending = true
```

を維持。

次回再試行できる。

**評価: PASS**

---

# 22. Target-Time JIT regression

以前実装済みの:

```text
presentation sequence
feedback baseline
absolute targetTime
NEAREST_REFRESH_CYCLE
timeDomainId
target stage
```

は維持。

JIT active:

```cpp
metadata.Timing.targetTime = metadata.TargetTimeNs;
```

。

**評価: PASS**

---

# 23. PresentWait2 separation regression

pure resolver:

```text
BoundedPresentWait
TargetTimeScheduling
```

は独立したまま。

PresentWait2非対応:

```text
JustInTime
→ target scheduling可能
→ bounded wait OFF
```

を維持。

**評価: PASS**

---

# 24. Reflex / Anti-Lag authority

priority:

```text
NVIDIA Reflex
↓
AMD Anti-Lag 2
↓
GenericPresentTiming
↓
GenericHost
```

。

vendor API active時:

```text
generic bounded wait OFF
generic target scheduling OFF
```

。

二重pacingを避ける。

**評価: PASS**

---

# 25. Host limiter

Vulkan generic pacing時もhost FPS limiterを維持。

役割:

```text
Host limiter
→ emulator rate / TargetFPS

PresentWait2
→ optional previous-present wait

Present Timing
→ presentation scheduling
```

。

**評価: PASS**

---

# 26. Config default

default:

```text
TelemetryOnly
```

。

runtime validation前にJITを一般defaultへ上げていない。

**評価: PASS**

---

# 27. Validation support確認

repository側にはValidation Layer supportが既にある。

Debug build時:

```text
MELONDS_VULKAN_ENABLE_VALIDATION=1
```

を`core`へ付与。

`VulkanContext`は:

```text
VK_LAYER_KHRONOS_validation
```

をenumerateし、存在時:

```text
[Vulkan] validation layer enabled
```

をlog。

存在しない場合:

```text
validation disabled
```

としてVulkan renderer自体はfailさせない。

---

# 28. Debug messenger

Validation有効時:

```text
VK_EXT_debug_utils
```

を有効化。

severity:

```text
WARNING
ERROR
```

type:

```text
GENERAL
VALIDATION
PERFORMANCE
```

をcallbackへ流す。

したがって現状repoのままでも通常のVulkan core validationを開始可能。

---

# 29. 注意: 通常のMinGW release build

`tools/build/windows/build-mingw.bat`は:

```text
release-mingw-x86_64
MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON
```

。

しかしValidation compile defineは:

```text
CONFIG:Debug
```

でのみ付く。

したがって:

> **通常の`build-mingw.bat`成功だけではValidation Layerは有効にならない。**

Validation phaseは必ずDebug buildを用意すること。

---

# 30. Developer buildとA/B measurementの分離

もう1点重要。

`MELONPRIME_ENABLE_DEVELOPER_FEATURES`有効時、present timingはdeveloper telemetry用に:

```text
surfaceが対応する全present stage
```

をqueryする。

release production pathは:

```text
TargetPresentStageのみ
```

。

全stage queryはtiming queue pressureやdriver側のtiming collection workを増やし得る。

したがって:

```text
Validation build
→ Debug + developer diagnostics OK

Latency A/B build
→ Validation OFF
→ production-like target-stage-only behavior
```

を分離するべき。

---

# 31. Commit-reported tests

最新commit messageでは以下をPASSと報告。

```text
Windows MinGW build
melonprime_vulkan_present_timing_tests
melonprime_xell_state_machine_tests
audit-low-latency-contract.py
audit-melonprime-srp-performance.ps1
check-inc-ownership.ps1
audit-platform-scatter-budget.ps1
audit-config-defaults.ps1
git diff --check
```

また:

```text
VulkanPresentPacer.cpp
developer / release
-Wall -Wextra clean
```

。

これらは本監査で再実行していないので:

```text
COMMIT-REPORTED PASS
```

と分類。

---

# 32. GitHub Actions

最新HEADに対してconnectorで:

```text
combined statuses = none
pull-request-triggered workflow runs = none
```

。

したがって:

```text
GitHub Actions
NOT VERIFIED
```

。

---

# 33. Runtime未検証

最新commit自身が:

```text
Validation Layer    NOT RUN
NVIDIA runtime      NOT RUN
AMD runtime         NOT RUN
Intel runtime       NOT RUN
MoltenVK runtime    NOT RUN
Linux build         NOT RUN
macOS build         NOT RUN
Vulkan OFF build    NOT RUN
latency measurement NOT RUN
runtime matrix      NOT RUN
```

と明記。

本監査でもPASSへ変更しない。

---

# 34. Intel XeLL

今回の対象外。

現状の正確な状態:

> **XeLL実装済み・静的/Fake検証済み・Intel Arc実機runtime未検証**

---

# 35. 最新status matrix

| 項目 | 状態 | 備考 |
|---|---|---|
| Vulkan clean-room backend | **PASS** | |
| Present ID2 | **PASS** | |
| Present Wait2 | **PASS** | |
| PresentWait2 / JIT分離 | **PASS** | |
| Target-Time JIT | **PASS** | non-zero targetTime |
| Timing properties lifecycle | **PASS** | |
| Time-domain lifecycle | **PASS** | |
| VK_INCOMPLETE retry | **FIXED / PASS** | bounded retry |
| Initial timing queue fail-soft | **FIXED / PASS** | latest commit |
| Queue growth failure separation | **PASS** | |
| DEVICE_LOST propagation | **FIXED / PASS** | latest commit |
| Swapchain out-of-date routing | **PASS** | |
| Permanent timing polling stop | **IMPLEMENTED** | residual P2 noteあり |
| Host FPS limiter | **PASS** | |
| Reflex authority | **PASS** | static |
| Anti-Lag authority | **PASS** | static |
| Pure policy/timing tests | **IMPLEMENTED** | commit-reported PASS |
| Static CI audits | **IMPLEMENTED** | commit-reported PASS |
| Static P0/P1 blocker | **NONE FOUND** | inspected scope |
| Validation Layer | **NOT RUN** | 次工程 |
| NVIDIA real hardware | **NOT RUN** | 次工程 |
| Latency A/B | **NOT RUN** | 次工程 |
| GitHub Actions | **NOT VERIFIED** | no status/run |

---

# 36. Runtimeへ進むGate

以下は満たしたと判定。

```text
[x] Target-Time JIT static implementation
[x] PresentWait2 dependency separation
[x] timing queue initial failure containment
[x] timing queue bounded recovery
[x] DEVICE_LOST / OUT_OF_DATE separation
[x] time-domain incomplete handling
[x] default TelemetryOnly
[x] host limiter retained
[x] Reflex generic-pacing exclusion
[x] pure model/policy tests present
[x] CI static contract present
```

次:

```text
[ ] Validation Layer
[ ] NVIDIA real hardware
[ ] A/B measurement
```

。

---

# 37. Validation前に必須修正はあるか

今回確認範囲では:

```text
P0 blocker なし
P1 blocker なし
```

。

P2 timing polling semanticsは:

```text
measurement / presentation correctness blockerではない
```

ため、Validation Layer開始を止めない。

---

# 38. 推奨次工程

別紙:

```text
melonPrimeDS_Vulkan_ValidationLayer_NVIDIA実機AB_実施指示書_2026-08-13.md
```

に従って:

```text
Phase 1
Validation Layer

Phase 2
NVIDIA functional runtime

Phase 3
NVIDIA A/B

Phase 4
判定
```

を行う。

---

# 39. 一次資料

## melonPrimeDS

Branch:

https://github.com/ag-advania/melonPrimeDS/tree/develop_remakeVulkan_ver2

Latest commit:

https://github.com/ag-advania/melonPrimeDS/commit/6437a75ed59c2672e660da147cba8868960e29f3

主要file:

```text
src/VulkanPresentPacer.h
src/VulkanPresentPacer.cpp
src/VulkanPresentPacingPolicy.h
src/VulkanPresentTimingModel.h
src/VulkanContext.cpp
src/VulkanNvidiaReflex.cpp
src/frontend/qt_sdl/MelonPrimeVulkanPresenter.cpp
src/frontend/qt_sdl/CMakeLists.txt
tools/testing/vulkan-present-timing-tests.cpp
tools/ci/audits/audit-low-latency-contract.py
tools/build/windows/build-mingw.bat
CMakePresets.json
```

## Khronos

Validation overview:

https://docs.vulkan.org/guide/latest/validation_overview.html

VK_EXT_present_timing:

https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html

vkGetPastPresentationTimingEXT:

https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingEXT.html

vkSetSwapchainPresentTimingQueueSizeEXT:

https://docs.vulkan.org/refpages/latest/refpages/source/vkSetSwapchainPresentTimingQueueSizeEXT.html

VK_NV_low_latency2:

https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_low_latency2.html

vkGetLatencyTimingsNV:

https://docs.vulkan.org/refpages/latest/refpages/source/vkGetLatencyTimingsNV.html
