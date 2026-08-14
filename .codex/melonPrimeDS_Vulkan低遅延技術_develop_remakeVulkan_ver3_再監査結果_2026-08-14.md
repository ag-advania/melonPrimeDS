# melonPrimeDS — Vulkan低遅延技術 develop_remakeVulkan_ver3 再監査結果・追加修正指示書

## 0. 監査情報

- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver3`
- 元監査・作業指示書HEAD: `94d767591d9c144491037ad07336563b85869736`
- 今回の実装確認HEAD: `bd375cad7c926f22b0c5a7f429e4952d8f3a3e53`
- 差分: 元監査HEADから **10 commits ahead / 0 behind**
- 監査日: 2026-08-14
- 対象: Vulkan低遅延技術、とくに `VK_GOOGLE_display_timing` fallback実装とその周辺契約
- 監査方法:
  - GitHub上の現行branchを直接確認
  - 元監査指示書との項目照合
  - 実装コードの静的監査
  - repositoryへpush済みのIntel Mac / MoltenVK実機証拠の監査
  - Khronosの現行Vulkan仕様との照合
- 注意:
  - 今回こちらで新規の実機build/runtimeを実行したわけではない。
  - `PASS (repository evidence)` は、branchへ記録済みのbuild/runtime証拠を監査して妥当と判断したもの。
  - HEAD `bd375cad...` に対するGitHub combined statusはstatus entryなし。したがってGitHub Actions PASSとは数えない。

> **Superseded:** 現行のcanonical判定は
> `.codex/melonPrimeDS_Vulkan低遅延技術_353cd23c_コミット後再監査_2026-08-14.md`
> を参照する。この文書は履歴資料として保持する。

---

# 1. 最終結論

## 判定

```text
PASS — P2 compatibility fixes complete; physical runtime gates remain open
```

現行 `develop_remakeVulkan_ver3` は、元指示書でP2候補だった:

```text
VK_GOOGLE_display_timing fallback
```

を**実装済み**であり、さらに:

```text
Physical Intel Mac
MoltenVK 1.4.2
pinned MoltenVK 1.4.0
functional matrix
5-minute smoke
Formal M0/M1/M2
```

まで証拠が追加されている。

したがって、

```text
「Google backendそのものが未実装」
```

という状態ではもうない。

前回静的監査で確認した**generic fallbackの2点のP2互換性ギャップ**は、
`bd375cad...` で実装・model test・contract auditまで完了した。

ただし、実機でのGOOGLE + FIFO_LATEST_READY組み合わせはまだ未実行であり、
Intel Macの既存証拠もFIFO_LATEST_READY非対応である。したがって、
実装DoDはPASS、対応hardwareのruntime gateはNOT RUNとして分離する。

```text
P2-1
EXTが存在するがtarget schedulingには使えない場合、
利用可能なGOOGLEへfallbackしない。

P2-2
VK_PRESENT_MODE_FIFO_LATEST_READY_KHRを
GOOGLE timing backendと組み合わせられない。
```

上記2件のコード上の残件は解消済みである。

前回のIntel Mac / MoltenVK surfaceでは、そもそも:

```text
VK_EXT_present_timing     unavailable
FIFO_LATEST_READY         unavailable
VK_GOOGLE_display_timing  available
```

であった。そのため既存MacのFormal結果は有効なGOOGLE JIT証拠だが、今回のFIFO_LATEST_READY修正のruntime証拠には流用しない。

現時点で残るのは、他GPU / 他driver / 他OSのruntime gateとMac lifecycle gateだけであり、P2 compatibility codeの追加修正は残っていない。

---

# 2. 今回の差分概要

元監査HEAD:

```text
94d767591d9c144491037ad07336563b85869736
```

からP2実装確認HEAD:

```text
bd375cad7c926f22b0c5a7f429e4952d8f3a3e53
```

まで10 commits進んでいる。

低遅延関連の主要追加:

```text
src/VulkanGoogleDisplayTimingModel.h
src/VulkanDevice.cpp
src/VulkanDevice.h
src/VulkanLoader.cpp
src/VulkanLoader.h
src/VulkanPresentPacer.cpp
src/VulkanPresentPacer.h
src/VulkanPresentPacingPolicy.h
src/VulkanPresentLatencyCapture.cpp
src/frontend/qt_sdl/MelonPrimeVulkanPresenter.cpp

tools/testing/vulkan-google-display-timing-probe.cpp
tools/testing/vulkan-present-timing-tests.cpp
tools/perf/aggregate-vulkan-latency.py
tools/testing/aggregate-vulkan-latency-tests.py
tools/ci/audits/audit-low-latency-contract.py
```

実機証拠:

```text
docs/archive/audits/vulkan/2026-08-14-intel-macbook/google-display-timing.md
docs/archive/audits/vulkan/2026-08-14-intel-macbook/formal-f2-macbook.md
docs/archive/audits/vulkan/2026-08-14-intel-macbook/README.md
```

最新HEAD `4fb3abfa...` ではFormal用diagnostic savestate hookが:

```cpp
MELONPRIME_ENABLE_DEVELOPER_FEATURES
```

配下へ限定された。

これはproduction buildへ診断用state-load hookを残さないための妥当なhardeningである。

---

# 3. 元Google backend DoDとの照合

| 項目 | 判定 | 監査結果 |
|---|---|---|
| Physical Macで`VK_GOOGLE_display_timing` expose確認 | PASS | MoltenVK 1.4.2でyes |
| pinned MoltenVK 1.4.0 expose確認 | PASS | yes |
| extension request optional | PASS | optional device extensionとして追加 |
| function pointer resolution | PASS | 2 entry pointsをoptional resolve |
| backend enumとtarget mode enumを分離 | PASS | `VulkanPresentTimingBackend`と`VulkanTargetSchedulingMode`を分離 |
| EXT backend priority | **PARTIAL** | priority条件が広すぎ、EXT target不能でもGOOGLEを塞ぐ |
| `desiredPresentTime`生成 | PASS | Google専用modelで生成 |
| Google `presentID` correlation | PASS | 独立32-bit ID、0 skip wrapあり |
| refresh duration query | PASS | `vkGetRefreshCycleDurationGOOGLE` |
| past timing query | PASS | `vkGetPastPresentationTimingGOOGLE` |
| Prepare / Commit / Abandon | PASS | transaction実装済み |
| OUT_OF_DATE処理 | PASS | feedback queryからswapchain rebuildへrouting |
| DEVICE_LOST処理 | PASS | renderer failure classへrouting |
| SURFACE_LOST処理 | PASS | renderer failure classへrouting |
| ReflexがGOOGLEを抑止 | PASS | backend=None |
| Anti-Lag 2がGOOGLEを抑止 | PASS | backend=None |
| Fast Forward / Slow Motion抑止 | PASS | abnormal speedでbackend=None |
| VSync OFF / non-FIFO target抑止 | PASS | target scheduling OFF |
| telemetry backend field | PASS | `target_backend` / `timingBackend`追加 |
| backend-aware capture | PASS | GOOGLE feedback専用列、EXT stage列を捏造しない |
| model tests | **PARTIAL** | Google基本testは十分。ただし混在capability caseが欠落 |
| Physical Mac functional PASS | PASS | 1.4.2 functional matrixあり |
| pinned 1.4.0 smoke | PASS | 5分、17,296 frames |
| Formal A/B | PASS | M0/M1/M2 各3 runs |
| shipping default維持 | PASS | `TelemetryOnly`維持 |

結論:

```text
Google backend core implementation:
    COMPLETE

generic cross-capability fallback:
    PARTIAL

Intel Mac runtime evidence:
    STRONG

full platform lifecycle gate:
    PARTIAL
```

---

# 4. 正しく実装されている主要項目

## 4.1 backendとtarget semanticsの分離

現行:

```cpp
enum class VulkanPresentTimingBackend
{
    None,
    ExtPresentTiming,
    GoogleDisplayTiming,
};
```

と:

```cpp
enum class VulkanTargetSchedulingMode
{
    None,
    Absolute,
    Relative,
};
```

が分離されている。

これは元指示書どおり。

GOOGLEをtarget mode enumへ混ぜていない。

---

## 4.2 GOOGLE device extensionはoptional

`VulkanDevice.cpp`では:

```text
VK_GOOGLE_display_timing
```

がexposeされる場合のみenable。

機能structを必要とせず、`present_id2`にも依存させていない。

これは正しい。

またoptional extension群を理由に最終的なVulkan renderer creationを即FAILさせないfail-soft設計も維持されている。

---

## 4.3 dispatch resolution

以下をoptional resolve:

```text
vkGetPastPresentationTimingGOOGLE
vkGetRefreshCycleDurationGOOGLE
```

extensionが有効でもentry pointが解決できない場合、`VulkanPresentPacer::Initialize()`側でGoogle backendを利用不可にする。

renderer全体の必須entry point扱いにしていない。

元指示書の:

```text
extension enabled
+
function missing
→ backend unavailable
→ rendererは継続
```

という契約に合致する。

---

# 5. Google clock domain

## 5.1 Appleは修正済み

実機検証中に:

```text
libc++ steady_clock
MoltenVK presentation clock
```

のepoch不一致が実際に発見されている。

現行Apple pathは:

```cpp
mach_absolute_time()
```

を`mach_timebase_info`でnsへ変換する。

repository evidenceでは修正後:

```text
target
feedback
```

が同じtimeline上にあることを再確認済み。

この修正は重要であり妥当。

---

## 5.2 非Apple

非Appleでは:

```cpp
std::chrono::steady_clock
```

を使用。

これは現時点で静的に不正とは判定しない。

ただし元監査で残っている:

```text
native Intel Vulkan
Linux hardware
```

のruntime gateでは必ず:

```text
desiredPresentTime
actualPresentTime
earliestPresentTime
```

のepoch / magnitude / monotonicityを確認する。

Appleで実際にclock mismatchが発生した実績があるため、別platformで「steady_clockだから同一domain」と推測だけでPASSにしない。

---

# 6. Present ID / transaction

## 6.1 独立32-bit ID

Google用:

```text
uint32_t
```

のpresent IDを独立管理。

```text
0xFFFFFFFF
→ 1
```

とし、0をskipするwrap testも存在する。

EXT/Reflexの64-bit IDを雑にcastしていない。

PASS。

---

## 6.2 Prepare / Commit / Abandon

Google modelは:

```text
Prepare
Commit
Abandon
Reset
```

を持つ。

present reject時:

```text
Abandon
```

accepted:

```text
Commit
```

swapchain lifecycle:

```text
Reset
```

。

present failureでID/cadenceを二重consumeしない。

PASS。

---

# 7. Present metadata chain

`PreparePresent()`は:

```text
ExtPresentTiming
```

と:

```text
GoogleDisplayTiming
```

を`if / else if`で排他的にattachする。

したがって:

```text
VkPresentTimingsInfoEXT
+
VkPresentTimesInfoGOOGLE
```

を同じpresentへ二重attachしない。

またpresenterは現在:

```cpp
present.swapchainCount = 1;
```

であり、Google metadataも:

```cpp
metadata.GoogleTimes.swapchainCount = 1;
```

なので現行presenter contractでは一致する。

PASS。

---

# 8. Vendor pacing ownership

以下は正しく抑止される。

```text
Reflex active
→ TimingBackend=None
→ Generic wait OFF
→ Generic target OFF

Anti-Lag 2 active
→ TimingBackend=None
→ Generic wait OFF
→ Generic target OFF
```

既存model testにもvendor ownership testが存在。

GOOGLE追加によって:

```text
Reflex + GOOGLE
Anti-Lag + GOOGLE
```

のdouble pacingは発生しない。

PASS。

---

# 9. Speed / VSync contract

Fast Forward / Slow Motionなどnormal speedでない場合:

```text
backend=None
bounded wait OFF
target scheduling OFF
```

。

またnon-FIFO present modeではGOOGLE targetもOFF。

実Mac functional evidenceでも:

```text
JustInTime + VSync OFF
→ IMMEDIATE
→ target 0%
```

が確認されている。

PASS。

---

# 10. Telemetry / capture

追加済み:

```text
target_backend
feedback_desired_present_time_ns
feedback_actual_present_time_ns
feedback_earliest_present_time_ns
feedback_present_margin_ns
```

。

GOOGLE backendで:

```text
feedback_stage_time_ns
```

へ架空のEXT timestampを入れない。

aggregator側も:

```text
GOOGLEでEXT feedback stageが非0ならinvalid
GOOGLE target modeはabsoluteのみ
desired targetのnon-monotonicをinvalid
```

としている。

これは元指示書のbackend-aware capture方針に合致。

PASS。

---

# 11. 実機証拠

## 11.1 capability probe

repository evidence:

| Runtime | extension | past timing fn | refresh fn |
|---|---:|---:|---:|
| MoltenVK 1.4.2 | yes | resolved | resolved |
| pinned MoltenVK 1.4.0 | yes | resolved | resolved |

Phase 0は完了扱いでよい。

---

## 11.2 MoltenVK 1.4.2 functional matrix

記録済み:

| Mode | 状態 |
|---|---|
| TelemetryOnly | GOOGLE metadata / targetなし |
| PresentWait | GOOGLE telemetry + bounded wait path |
| JustInTime | GOOGLE absolute target 100% |
| JustInTime + VSync OFF | targetなし / non-FIFO fallback |

機能成立を確認済み。

---

## 11.3 pinned MoltenVK 1.4.0 smoke

記録:

```text
Duration                         5 min
Captured frames                  17,296
GOOGLE backend                   100%
Absolute targets                 100%
Feedback                         17,295
Non-monotonic targets            0
Swapchain generation             1
Timing queue-full                0
DEVICE_LOST / SIGABRT            0
```

5-minute shipping-pin smokeはPASS。

---

# 12. Formal M0/M1/M2

Formal fixed F2 scene:

```text
AMHJ real ROM
matching F2 savestate
same binary
same display
same 1x internal resolution
3 randomized runs / mode
600 warmup
>= 10,000 measured frames / run
```

。

中央値:

| Mode | Frame P50 | Frame P95 | Frame P99 | Pipeline P50 | Pipeline P95 |
|---|---:|---:|---:|---:|---:|
| M0 TelemetryOnly | 16.671 ms | 20.719 ms | 23.036 ms | 16.611 ms | 18.703 ms |
| M1 PresentWait | 16.678 ms | 20.125 ms | 22.442 ms | 15.851 ms | 18.425 ms |
| M2 Google JustInTime | 16.688 ms | 20.506 ms | 23.326 ms | 15.543 ms | 18.474 ms |

M2はM0に対して:

```text
Pipeline P50:
    約1.068 ms改善

Frame P99:
    約0.290 ms悪化

median FPS:
    59.257 vs 59.708
```

。

判定:

```text
NO MATERIAL DIFFERENCE
```

で妥当。

したがって:

```text
shipping default = TelemetryOnly
```

を変更しない現行判断も妥当。

---

# 13. P2 FINDING 1 — EXTがtarget不能でもGOOGLEへfallbackしない

## 13.1 現行コード

`VulkanPresentPacingPolicy.h`のbackend selectorは概念上:

```cpp
if (caps.PresentTimingSurface && caps.TimingMetadataEnabled)
    return VulkanPresentTimingBackend::ExtPresentTiming;

if (caps.GoogleDisplayTimingAvailable
    && caps.GoogleDisplayTimingRuntimeEnabled)
    return VulkanPresentTimingBackend::GoogleDisplayTiming;
```

。

問題は:

```text
EXTが「metadata backendとして存在する」
```

ことと:

```text
EXTが「JIT target schedulingに使える」
```

ことを同じ条件として扱っている点。

---

## 13.2 具体的な失敗case

例えば:

```text
VK_EXT_present_timing                yes
presentTimingSupported               yes
timing metadata queue                usable

presentAtAbsoluteTime                no
presentAtRelativeTime                no

VK_GOOGLE_display_timing             yes
GOOGLE functions                     resolved
```

。

現行:

```text
SelectBackend
→ EXT

Classify target
→ NoTargetTimingModeDevice / Surface

JIT target
→ OFF
```

。

しかし元指示書の意図は:

```text
EXT target scheduling usable
    → EXT

else GOOGLE usable
    → GOOGLE
```

。

このcaseでは本来:

```text
GOOGLE absolute target
```

へfallback可能。

---

## 13.3 runtime impact

現在のIntel Macでは:

```text
VK_EXT_present_timing = no
```

なのでGOOGLEへ正常に入る。

従ってMac Formal evidenceはこの問題で無効にはならない。

ただし他GPU / 他driverで:

```text
EXT telemetry only
+
GOOGLE target capable
```

という組み合わせが出ると、JIT機能を不必要に失う。

---

## 13.4 test gap

現行`TestTimingBackendSelection()`は:

```text
EXT usable + GOOGLE
→ EXT

EXT absent + GOOGLE
→ GOOGLE
```

を確認している。

しかし以下が無い:

```text
EXT present but no target mode + GOOGLE
→ GOOGLE
```

。

つまりmodel testが現行selectorの過剰priorityを検出できない。

---

## 13.5 修正方針

単純に:

```text
PresentTimingSurface == true
→ EXT
```

としない。

少なくともtarget policyについて:

```text
EXT target structurally usable
    → EXT

else GOOGLE target usable
    → GOOGLE

else
    → no target backend
```

へ分ける。

### 推奨

telemetry backendとtarget backendの選定目的を区別する。

例:

```cpp
enum class VulkanPresentTimingIntent
{
    Telemetry,
    TargetScheduling,
};
```

またはpure helperを分離:

```cpp
SelectVulkanPresentTelemetryBackend(...)
SelectVulkanPresentTargetBackend(...)
```

。

### 重要

以下の**一時的bootstrap状態**だけで即GOOGLEへflapさせないこと。

```text
TimingPropertiesNotReady
TimeDomainsNotReady
first-feedback待ち
```

これらはEXTが構造的に非対応なのではなく、初期化途中である。

fallback判定はまず:

```text
extension / surface support
target mode support
必要entry point
有効なtarget stageの存在
runtime backend disable状態
```

など、構造的な利用可否を基準にする。

---

# 14. P2 FINDING 2 — FIFO_LATEST_READYがGOOGLE backendで使えない

これは今回の静的監査で最も明確な仕様上の互換性欠落。

---

## 14.1 VulkanDevice側

現行:

```cpp
const bool hasLatestReady = hasPresentTiming
    && HasExtension(VK_KHR_present_mode_fifo_latest_ready)
    && latestReadyFeatures.presentModeFifoLatestReady;
```

。

つまり:

```text
VK_EXT_present_timing
```

が無いと:

```text
VK_KHR_present_mode_fifo_latest_ready
```

をenableしない。

GOOGLE extension判定はその後に行われている。

---

## 14.2 Pacer側

`ShouldUseFifoLatestReady()`も:

```text
PresentTimingSurface
PresentId2Surface
EXT absolute/relative target mode
EXT time-domain query
vkSetSwapchainPresentTimingQueueSizeEXT
```

を要求。

したがってGOOGLE-only timing backendでは:

```text
JustInTimeFifoLatestReady
```

を選択できない。

---

## 14.3 Vulkan仕様との不一致

`VK_KHR_present_mode_fifo_latest_ready`のextension dependencyは:

```text
VK_KHR_swapchain
```

。

`VK_EXT_present_timing`への依存ではない。

さらにVulkan specificationはFIFO_LATEST_READYについて明示的に:

```text
VK_GOOGLE_display_timing
```

でtarget present timeを与える場合にも、そのtarget timeをready判定へ使用すると規定している。

つまり:

```text
GOOGLE timing
+
FIFO_LATEST_READY
```

は正当な組み合わせ。

現行melonPrimeDSはこれをarchitecture上拒否している。

---

## 14.4 現Macへの影響

今回のIntel Mac / MoltenVKは:

```text
FIFO_LATEST_READY = unavailable
```

。

従って現Mac Formal runへ影響はない。

元指示書でもMac A3はunsupportedならUNSUPPORTED扱いでよいとしていたため、Mac evidenceは妥当。

問題は将来の:

```text
GOOGLE available
FIFO_LATEST_READY available
EXT unavailable
```

なdevice。

---

## 14.5 修正方針

### VulkanDevice

Google availabilityをlatest-ready判定より前に計算する。

概念上:

```cpp
const bool hasGoogleDisplayTiming = ...;

const bool hasTimeBasedPresentBackend =
    hasPresentTiming || hasGoogleDisplayTiming;

const bool hasLatestReady =
    hasTimeBasedPresentBackend
    && HasExtension(VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME)
    && latestReadyFeatures.presentModeFifoLatestReady == VK_TRUE;
```

さらに単純化するなら:

```text
GenericPresentTimingがrequestされていて
latest-ready extension + featureがある
→ device extensionはenableしてよい

実際にswapchainで使うかはpacer policyが決める
```

でもよい。

---

### VulkanPresentPacer

`ShouldUseFifoLatestReady()`を:

```text
EXT target scheduler can become active
OR
GOOGLE target scheduler can become active
```

で判定する。

GOOGLE pathに不要な:

```text
PresentId2Surface
EXT time-domain query
vkSetSwapchainPresentTimingQueueSizeEXT
```

を要求しない。

---

## 14.6 必須test

追加:

```text
GOOGLE available
EXT unavailable
LatestReady feature available
JustInTimeFifoLatestReady
→ FIFO_LATEST_READY eligible
```

さらに:

```text
EXT target unusable
GOOGLE usable
LatestReady available
→ GOOGLE + FIFO_LATEST_READY
```

。

contract auditにも:

```text
FIFO_LATEST_READY must remain usable with
VK_GOOGLE_display_timing
```

を追加する。

---

# 15. Google feedbackについて

現行は:

```text
desiredPresentTime
actualPresentTime
earliestPresentTime
presentMargin
```

を取得しcaptureへ保持する。

一方、target generator:

```text
previous desiredPresentTime
+
frame interval
```

を基本としたopen-loop modelであり、`actualPresentTime`や`presentMargin`を直接control loopへ戻してはいない。

これは**correctness defectとは判定しない**。

理由:

```text
API上有効なdesiredPresentTimeを生成
monotonic
実機でtarget 100%
Formalで安定動作
```

が確認できているため。

ただしFormal結果が:

```text
NO MATERIAL DIFFERENCE
```

だったことから、将来R&Dをするなら:

```text
actualPresentTime
earliestPresentTime
presentMargin
late wake
target-to-actual delta
```

を用いてphase / safety marginを分析する価値はある。

ここはP2互換性修正と混ぜない。

---

# 16. full Physical Mac gateはまだPARTIAL

Google backendのfunctional/Formal PASSと、

```text
Mac Vulkan全体のlifecycle PASS
```

は別。

現行repository evidenceでもfull gateはPARTIAL。

未完:

```text
match-end / recap
resize
fullscreen
minimize / restore
reset-after-load
visual renderer handover
single continuous 30-minute AC session
validation-layer observable run
```

。

renderer switch自体はstress:

```text
30 / 30 production transitions
```

が記録されているが、visual handoverまでのPASSは未主張。

---

# 17. Validation layer

Mac evidenceでは:

```text
VK_LAYER_KHRONOS_validation
```

をbundle直load構成から観測できず:

```text
BLOCKED
```

。

従ってログ上:

```text
VUID 0
```

であっても、

```text
validation layer全面PASS
```

とは表現しない。

現行READMEがBLOCKEDを明記しているため、証拠管理としては正しい。

---

# 18. AMD Anti-Lag 2

元監査では:

```text
implementation PRESENT
hardware runtime NOT RUN
```

。

今回の元監査HEAD以降9 commitsの主要低遅延差分はGoogle / Mac evidenceが中心であり、AMD実機functional evidenceを閉じる変更は確認できない。

従ってAMDは引き続き:

```text
implementation = PRESENT
hardware gate = OPEN
```

。

AMD-specific workaroundはまだ追加しない。

実機で:

```text
extension advertised but init fails
ordering mismatch
frameIndex mismatch
authority stacking
generic pacing remains active
DEVICE_LOST / VUID
```

が出た場合だけ修正。

---

# 19. NVIDIA Reflex

Google追加後も:

```text
NvidiaReflex authority
→ generic backend None
```

がmodel testで維持されている。

今回のGoogle追加による静的なdouble-pacing regressionは見つからない。

NVIDIA Reflex自体の元Formal結果を再実装対象へ戻す必要はない。

---

# 20. legacy VK_KHR_present_id / present_wait

現状も:

```text
P3 / conditional compatibility
```

のままでよい。

Google backend実装によって、今すぐlegacy pairを実装すべき新しい理由は生じていない。

実際のhardwareで:

```text
present_id2  = no
present_wait2 = no
present_id   = yes
present_wait = yes
```

が観測された場合だけ追加。

---

# 21. native Intel / Linux

Mac Intel Iris Plus 655は:

```text
MoltenVK
```

経路の証拠。

これは:

```text
native Intel Vulkan on Windows/Linux
```

の証拠ではない。

引き続き:

```text
native Intel Vulkan
Linux native Vulkan
```

は別gate。

特にGoogle backendをnative Linuxで使う場合:

```text
desiredPresentTimeとfeedbackのclock epoch
target monotonicity
OUT_OF_DATE lifecycle
fullscreen/resize
VRR
```

を実測する。

---

# 22. Generic JIT R&D

元監査のNVIDIA EXT JIT:

```text
P50 improvement = 3.3815%
P95 improvement = 1.9302%
```

に対するtail tuningは、今回のGoogle compatibility fixとは独立。

またIntel Mac GOOGLE Formalも:

```text
NO MATERIAL DIFFERENCE
```

。

従って今は:

```text
適当なsleep margin変更
固定100us調整
vendor別magic number
```

を入れない。

まずP2-1 / P2-2のarchitecture correctnessを閉じる。

その後、必要なら別R&D branchで行う。

---

# 23. 追加修正の推奨順序

```text
1. P2-1 backend selectorをpolicy-aware / target-awareに修正
2. P2-1 mixed-capability model tests追加
3. P2-2 latest-ready device enable条件をGOOGLE対応
4. P2-2 ShouldUseFifoLatestReadyをbackend-neutral化
5. GOOGLE + latest-ready model/static contract tests追加
6. low-latency contract auditへ2 regressionを追加
7. existing EXT / NVIDIA Formal contractの回帰確認
8. Mac functional smoke
9. 対応hardwareがあればGOOGLE + FIFO_LATEST_READY runtime
10. 残存Mac lifecycle gate
11. AMD実機gate
12. native Intel / Linux gate
```

---

# 24. 修正時に壊してはいけないもの

## 24.1 EXT優先の意味

修正後も:

```text
EXT target schedulingが実際に使用可能
+
GOOGLEも使用可能
```

なら:

```text
EXT wins
```

。

FINDING 1は:

```text
EXT優先を廃止する
```

という意味ではない。

正しくは:

```text
「使えないEXTがGOOGLEを塞がない」
```

。

---

## 24.2 TelemetryOnly

shipping default:

```text
TelemetryOnly
```

を変更しない。

---

## 24.3 Vendor ownership

```text
Reflex
Anti-Lag 2
```

がactiveなら:

```text
GOOGLE
EXT
generic bounded wait
generic target
```

を重ねない。

---

## 24.4 speed modes

```text
Fast Forward
Slow Motion
unlimited / abnormal cadence
```

で新selectorがGoogleへfallbackしてしまわないこと。

---

## 24.5 swapchain transaction

backend切替で:

```text
old Google present ID
old EXT sequence
old target
old feedback
```

を新swapchainへ持ち越さない。

---

# 25. 追加すべきmodel tests

最低限:

```text
TEST 1
EXT usable + GOOGLE usable
→ EXT

TEST 2
EXT absent + GOOGLE usable
→ GOOGLE

TEST 3
EXT metadata usable
but absolute=false
and relative=false
and GOOGLE usable
→ GOOGLE for JIT

TEST 4
EXT runtime metadata disabled
+ GOOGLE usable
→ GOOGLE

TEST 5
TelemetryOnly
+ EXT telemetry usable
+ GOOGLE usable
→ EXT telemetry priorityを維持

TEST 6
Reflex active
+ both timing backends
→ None

TEST 7
AntiLag active
+ both timing backends
→ None

TEST 8
abnormal speed
+ GOOGLE
→ None

TEST 9
non-FIFO
+ GOOGLE
→ no target

TEST 10
GOOGLE
+ FIFO_LATEST_READY feature
+ JIT latest-ready policy
→ latest-ready eligible

TEST 11
EXT target unusable
+ GOOGLE
+ FIFO_LATEST_READY
→ GOOGLE target backend + latest-ready eligible
```

。

---

# 26. contract audit追加案

`audit-low-latency-contract.py`へ、単なるsymbol存在チェックではなく少なくとも以下のsource contractを追加。

```text
backend selector:
    EXT target incapable must not suppress GOOGLE JIT

device extension:
    latest-ready must not require VK_EXT_present_timing

pacer:
    latest-ready eligibility must accept GOOGLE target backend

tests:
    mixed EXT/GOOGLE capability regression test must exist
    GOOGLE + latest-ready regression test must exist
```

。

---

# 27. 推奨commit分割

```text
vulkan: fix Google target fallback selection

test: cover mixed Vulkan present timing backends

vulkan: allow FIFO latest-ready with Google display timing

test: cover Google FIFO latest-ready compatibility

audit: guard Vulkan timing backend fallback contracts
```

必要ならruntime evidenceは別commit:

```text
test: record Google latest-ready runtime
```

。

---

# 28. 修正後DoD

## P2-1

- [x] telemetry backendとtarget backendの選定意図を区別
- [x] EXT target capableならEXT優先
- [x] EXT target incapable + GOOGLE usableならGOOGLE
- [x] bootstrap一時状態でbackendが毎frameflapしない
- [x] Reflex / Anti-Lag抑止維持
- [x] abnormal speed抑止維持
- [x] non-FIFO target抑止維持
- [x] mixed-capability tests追加

## P2-2

- [x] latest-ready feature enableがEXTだけへ依存しない
- [x] GOOGLE timingでlatest-ready eligible
- [x] GOOGLE pathにPresentId2を不必要に要求しない
- [x] GOOGLE pathにEXT time-domain APIを不必要に要求しない
- [x] EXT relative / absolute latest-ready既存pathを壊さない
- [x] GOOGLE + latest-ready test追加
- [x] contract audit追加

## Regression

- [x] `vulkan-present-timing-tests`
- [x] aggregate Vulkan latency tests
- [x] low-latency contract audit
- [x] `git diff --check`
- [x] macOS Vulkan build (Developer Features ON / OFF)
- [ ] Windows Vulkan build
- [x] Linux Vulkan build (Ubuntu VM / 2 vCPU)
- [x] NVIDIA Reflex authority regression (model + static)
- [x] AMD authority static regression
- [x] TelemetryOnly default確認

---

# 29. 残件分類・更新版

## 実装済み + 実機確認済み

```text
Generic policy resolver基盤
PresentWait2
EXT absolute target
EXT relative target
NVIDIA Reflex
vendor ownership
timing queue fail-soft
Formal capture
GOOGLE display timing core
GOOGLE present ID transaction
GOOGLE Mac clock mapping
GOOGLE Intel Mac functional
GOOGLE pinned MoltenVK smoke
GOOGLE Formal M0/M1/M2
```

## 実装済み + compatibility修正済み（bd375cad）

```text
EXT → GOOGLE target fallback selector
GOOGLE + FIFO_LATEST_READY integration
```

## 実装済み + 実機確認待ち

```text
AMD Anti-Lag 2
native Intel Vulkan
Linux native Vulkan
some absolute-capability surfaces
```

## Physical lifecycle残件

```text
Mac match-end / recap
Mac resize / fullscreen / minimize
Mac reset-after-load
Mac 30-minute continuous AC
validation layer observable configuration
```

## 条件付き

```text
legacy VK_KHR_present_id / present_wait fallback
```

## R&D

```text
Generic JIT tail-latency tuning
GOOGLE feedback-driven phase / margin research
```

## 非対象

```text
Vulkan XeLL
```

---

# 30. 最終判断

元監査時点では:

```text
VK_GOOGLE_display_timing
= implementation candidate
```

だった。

現HEADでは:

```text
VK_GOOGLE_display_timing
= implemented
= Intel Mac functional validated
= pinned MoltenVK validated
= Formal A/B completed
```

まで進んでいる。

これは明確な前進。

ただし、

```text
「全generic timing capability combinationで完成」
```

とはまだ言えない。

今回確定した残件は:

```text
P2-1
EXTが存在するだけでGOOGLE fallbackを塞ぐselector

P2-2
GOOGLE timingではFIFO_LATEST_READYを使えないEXT固定gate
```

。

特にP2-2はKhronos仕様上、

```text
FIFO_LATEST_READY
+
VK_GOOGLE_display_timing
```

が明示的に想定されているため、修正価値が高い。

一方、現在のIntel MacではFIFO_LATEST_READY自体が非対応なので、現在push済みFormal結果を破棄する必要はない。

shipping defaultについても:

```text
TelemetryOnly
```

を維持する。

Intel Mac FormalはGoogle JITのcorrectnessを確認したが、shipping default変更を正当化するほど一貫したlatency勝利は示していない。

したがって次フェーズは:

```text
新しい低遅延APIを増やす
```

ではなく:

```text
Google fallbackのcapability selectionを完全にbackend-neutral化し、
その後vendor/platform runtime gateを閉じる
```

が最優先。

---

# 31. 参照

## melonPrimeDS

- Branch:
  - https://github.com/ag-advania/melonPrimeDS/tree/develop_remakeVulkan_ver3
- Google implementation:
  - `src/VulkanGoogleDisplayTimingModel.h`
  - `src/VulkanPresentPacer.cpp`
  - `src/VulkanPresentPacingPolicy.h`
  - `src/VulkanDevice.cpp`
  - `src/VulkanLoader.cpp`
- Tests:
  - `tools/testing/vulkan-present-timing-tests.cpp`
  - `tools/testing/vulkan-google-display-timing-probe.cpp`
  - `tools/ci/audits/audit-low-latency-contract.py`
- Intel Mac evidence:
  - `docs/archive/audits/vulkan/2026-08-14-intel-macbook/google-display-timing.md`
  - `docs/archive/audits/vulkan/2026-08-14-intel-macbook/formal-f2-macbook.md`
  - `docs/archive/audits/vulkan/2026-08-14-intel-macbook/README.md`

## Khronos Vulkan

- `VK_GOOGLE_display_timing`
  - https://docs.vulkan.org/refpages/latest/refpages/source/VK_GOOGLE_display_timing.html
- `VkPresentTimeGOOGLE`
  - https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentTimeGOOGLE.html
- `vkGetPastPresentationTimingGOOGLE`
  - https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingGOOGLE.html
- `VK_KHR_present_mode_fifo_latest_ready`
  - https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_mode_fifo_latest_ready.html
- `VkPresentModeKHR`
  - https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html

---

# 32. P2互換性修正の完了再監査（2026-08-14）

## 32.1 P2-1 — EXTからGOOGLEへのtarget-aware fallback

実装commit:

```text
bd375cad7c926f22b0c5a7f429e4952d8f3a3e53
vulkan: complete Google timing fallback compatibility
```

判定:

```text
PASS (source + pure model test + contract audit)
```

確認内容:

- `SelectVulkanPresentTimingBackend()` は従来どおりTelemetryのEXT優先を保持。
- `SelectVulkanPresentTargetBackend()` を新設し、EXTが`present_id2`またはabsolute/relative target modeを満たさない場合はGOOGLEへfallback。
- EXTがtarget-capableである場合は、GOOGLEが同時に存在してもEXTを維持。
- `TimingPropertiesReady` / `TimeDomainsReady` はbootstrap中の一時状態として扱い、毎frameのbackend flapを起こさない。
- `TargetSchedulingLifecycleFailed` はEXTに対してstickyだが、Google runtimeが使える場合はGOOGLEへ移行可能。
- Reflex、Anti-Lag 2、異常速度、non-FIFO抑止は既存resolverのまま維持。

## 32.2 P2-2 — GOOGLE + FIFO_LATEST_READY

判定:

```text
PASS (source + pure model test + contract audit)
```

確認内容:

- `VulkanDevice::Create()` は`hasPresentTiming || hasGoogleDisplayTiming`を基準に`VK_KHR_present_mode_fifo_latest_ready`をenableする。
- `VK_KHR_present_mode_fifo_latest_ready`は`VK_KHR_swapchain`依存であり、EXT専用条件にしない。
- `ShouldUseFifoLatestReady()` はtarget backendをresolverと共有し、GOOGLE pathでは`present_id2`、EXT time-domain、EXT results queueを要求しない。
- EXT pathのabsolute/relative targetおよびqueue/time-domain gateは維持。
- `VulkanTargetCanUseFifoLatestReady()` と混在capability testsで、GOOGLE-only、EXT+GOOGLE、feature unavailable、EXT lifecycle failureを確認。

仕様上も、`VK_KHR_present_mode_fifo_latest_ready`は`VK_KHR_swapchain`のみを依存先とし、Vulkanのpresent-mode仕様はGOOGLEのtarget present timeをFIFO_LATEST_READYのready判定に使用すると定義している。参照:

- https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_mode_fifo_latest_ready.html
- https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html

## 32.3 実行済み検証

| Gate | Result | Evidence |
|---|---|---|
| pure Vulkan present timing tests | PASS | `cmake --build build-mac-vulkan --target melonprime_vulkan_present_timing_check --parallel 4` |
| macOS Vulkan build / Developer Features ON | PASS | `tools/build/macos/build-macos-vulkan.sh --build-only --with-metal --jobs 4` |
| macOS Vulkan build / Developer Features OFF | PASS | `tools/build/macos/build-macos-vulkan.sh --build-only --release --with-metal --build-dir build-mac-vulkan-devguard-off --jobs 4` |
| Linux Vulkan build / Developer Features ON | PASS | `tools/build/linux/build-linux.sh` — Ubuntu VM 2 vCPU, 307/307, linked `build-linux/melonPrimeDS` |
| low-latency contract audit | PASS | `python3 tools/ci/audits/audit-low-latency-contract.py` |
| aggregate Vulkan latency tests | PASS | `python3 tools/testing/aggregate-vulkan-latency-tests.py` |
| Software parity audit | PASS | `python3 tools/ci/audits/audit-raster-software-parity.py` |
| whitespace check | PASS | `git diff --check` |

両macOS構成ともMoltenVK bundleとad-hoc signingまで完了している。Linux VMもVulkan headersを固定して307/307でリンクまで完了した。純粋なmodel testはmacOS ON/OFFとLinux buildで実行され、PASSを確認した。

## 32.4 未実行・別hardware gate

以下は今回のP2実装修正だけでは証明できないため、完了扱いにしない。

```text
NOT RUN — GOOGLE + FIFO_LATEST_READY対応hardwareのruntime実測
NOT RUN — Windows native Vulkan build/runtime（macOS環境にMSYS2/MinGWなし）
PASS    — Linux Vulkan build / NOT RUN — Linux native runtime
OPEN    — AMD Anti-Lag 2 hardware gate
OPEN    — native Intel Vulkan / Linux lifecycle gate
OPEN    — Mac match-end, resize/fullscreen/minimize, reset-after-load, 30-minute AC
BLOCKED — bundled macOS validation-layer observable run
```

既存Intel Mac / MoltenVK証拠ではFIFO_LATEST_READY自体が非対応なので、既存Formal M0/M1/M2結果をP2修正のruntime証拠へ流用しない。

## 32.5 完了判定

```text
P2-1 implementation DoD: PASS
P2-2 implementation DoD: PASS
source/model/contract/macOS/Linux build regression: PASS
GOOGLE + FIFO_LATEST_READY physical runtime: NOT RUN
Windows build and full cross-platform/lifecycle runtime audit: OPEN
```

したがって、今回の指示書で指定されたgeneric Vulkan compatibility修正は完遂済みである。一方、実機hardwareとfull lifecycleに依存する残件は、別のruntime evidence gateとして残す。
