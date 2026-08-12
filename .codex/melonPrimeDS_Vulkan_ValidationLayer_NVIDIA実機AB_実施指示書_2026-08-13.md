# melonPrimeDS Vulkan
# Validation Layer → NVIDIA 実機 A/B 実施指示書

- 対象リポジトリ: `ag-advania/melonPrimeDS`
- 対象ブランチ: `develop_remakeVulkan_ver2`
- 実施基準HEAD: `6437a75ed59c2672e660da147cba8868960e29f3`
- 作成日: 2026-08-13
- 対象backend: **現行Vulkan clean-room backend**
- 主目的:
  1. Vulkan API / synchronization / lifetime correctnessをValidation Layerで確認
  2. NVIDIA GPU実機でgeneric JITとNVIDIA Reflexのruntime成立を確認
  3. 同一条件A/Bでframe pacing / software latency /可能ならclick-to-photonを比較
  4. runtime未検証状態を、再現可能なevidence付きのPASS / FAILへ更新する

---

# 0. 実施状況 — Phase 1 完了 / Phase 2-3 未完 (2026-08-13)

> この指示書は実行済み。以下は追記であり、本文は作成時点のまま。
> 実施結果の正式報告（§83 の13節構成）は
> `melonPrimeDS_Vulkan_ValidationLayer_NVIDIA実機AB_実施結果_2026-08-13.md`。
> 生ログは `docs/archive/audits/vulkan/2026-08-13-validation/`。

実施 SHA: `945823c7a` / 実施環境: RTX 5070 Ti, driver 610.74.0.0,
loader 1.4.357, `VK_LAYER_KHRONOS_validation` (SDK 1.4.357.0)

## Phase 別の到達点

| Phase | 状態 | 備考 |
|---|---|---|
| Phase 1 Pass A (Core Validation) | **PASS** | policy 0/1/2/3 × Reflex off/on/on+boost、VSync off control すべて ERROR 0 / WARNING 0 |
| Phase 1 Pass B (Sync Validation) | **PASS** | policy 0/2/3 + Reflex On+Boost で blocking hazard 0 |
| Phase 1 イベント行列 (§18-21) | **NOT RUN** | fullscreen/resize/DPI/minimize/F2/renderer切替/速度モード。人手操作が必要 |
| Phase 2 NVIDIA functional (§40-46) | **PARTIAL** | GPU選択・Reflex active・On+Boost・authority切替は確認済み。present ID 相関突合と `vkGetLatencyTimingsNV` は未実施 |
| Phase 3 A/B (§47-70) | **BLOCKED** | 下記 |
| click-to-photon (§38-39, §67) | **NOT RUN** | Reflex Analyzer 未使用 |

## §29 Validation Gate の判定

```text
core validation ERROR                    0    ✓
timing/present 関連 WARNING               0    ✓
sync validation blocking hazard          0    ✓
DEVICE_LOST loop / recreate loop /
software fallback / grey-out / hang      なし  ✓
```

ただしイベント行列未実施のため、Gate を「全面的に通過」とは書かない。

## 実行中に発見・修正した不具合

```text
VUID-VkPresentTimingInfoEXT-timeDomainId-12400
```

`timeDomainId` は target time を要求しない present でも列挙済み ID で
なければならないが、target-time 分岐の中でしか設定していなかった。
**出荷デフォルトの TelemetryOnly でも常時発生**。修正は `639a6e8b6`。

§91「Static audit DONE → Core Validation → ...」という順序指定が
正しかったことの実例。静的監査では原理的に検出できない欠陥だった。

## Phase 3 が BLOCKED である理由

デバイスは `presentAtAbsoluteTime = yes` を報告するが、**surface が
`presentAtAbsoluteTimeSupported = false`** を返す。したがって:

```text
A2 JustInTime                → targetScheduling 常時 off
A3 JustInTimeFifoLatestReady → FIFO_LATEST_READY 非選択 (UNSUPPORTED)
```

となり、A1 PresentWait と**定義上まったく同じ挙動**になる。この状態で
A/B を回しても「差がなかった」という結果すら得られず、
「そもそも target-time が動いていない」だけになる。

デバイスは `presentAtRelativeTime = yes` を報告しているため、
`VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT` の実装が
このハードウェアで Phase 3 を意味のあるものにする前提条件。
必要作業は実施結果 §13.1 に記載。

## 本指示書のために整備したもの

| §  | 要求 | 成果物 |
|---|---|---|
| §9 | `debug-mingw-x86_64` preset の追加を推奨 | `CMakePresets.json` + `tools/build/windows/build-mingw-validation.bat` |
| §33-34 | developer features と分離した専用 capture flag | `MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE` (default OFF) |
| §35-36 | CSV フィールドと蓄積方式 | `src/VulkanPresentLatencyCapture.{h,cpp}`（固定容量リング + 終了時 flush） |
| §82 | run 単位 → mode 単位の集計 | `tools/perf/aggregate-vulkan-latency.py` |
| §3,5,70,83-86 | 環境記録・run 構成・結果表・チェックリスト | `docs/development/testing/vulkan-present-pacing-runbook.md` |
| §80 | Validation evidence 保存 | `docs/archive/audits/vulkan/2026-08-13-validation/` |

Phase 3 の計測基盤は整備済みで、上記 BLOCKER が解消すればそのまま実行できる。

## §75 default policy

`TelemetryOnly` のまま。source default は変更していない。

---

# 1. 最重要原則

この工程は:

```text
Correctness validation
```

と:

```text
Latency measurement
```

を**同じbuildでやらない**。

理由:

Vulkan Validation Layerはdeveloper debugging用であり、performance overheadを持つ。

またmelonPrimeDS自身も:

```text
MELONPRIME_ENABLE_DEVELOPER_FEATURES
```

有効時にはpresent timingで全supported stageをqueryするため、production release pathよりdriver work / timing queue pressureが増え得る。

したがって:

```text
Validation build
    → Debug
    → Validation Layer ON
    → 正しさを見る
    → latency数値は採用しない

A/B build
    → Release-like
    → Validation OFF
    → production-like timing query
    → latency / frame pacingを見る
```

を厳守する。

---

# 2. 作業開始時のHEAD確認

開始前:

```text
git fetch
git rev-parse HEAD
git status --short
```

基準:

```text
6437a75ed59c2672e660da147cba8868960e29f3
```

HEADが進んでいる場合:

1. 変更内容確認
2. Vulkan present / Reflex / limiterへ影響する変更がある場合はこの指示書との差分監査
3. runtime結果には実際のtested SHAを必ず記録

dirty working treeのbinaryで正式A/Bを取らない。

---

# 3. Test environmentを固定する

結果fileの冒頭に必ず保存。

```text
Commit SHA
Build type
Compiler/toolchain
Windows version
NVIDIA GPU model
NVIDIA driver version
Vulkan loader version
VK_LAYER_KHRONOS_validation version
Monitor model
Refresh rate
G-SYNC / VRR ON/OFF
VSync ON/OFF
Windowed / Fullscreen
Internal resolution scale
TargetFPS
ROM region/version
Test save state / location
Power plan
NVIDIA Control Panel overrides
Background capture / overlay state
```

同じA/B group内で変更禁止。

---

# 4. Game workload固定

A/Bは同じsceneを使う。

推奨:

```text
同一ROM
同一savestate
同一map/room
同一camera direction
同一HUD
同一internal resolution
同一audio
同一window/fullscreen
```

。

可能なら:

```text
CPU負荷が一定
GPU負荷がある程度発生
input反応が画面へ現れやすい
```

sceneを1つ固定。

sceneを途中で変えない。

---

# 5. Run metadata

各runへIDを付ける。

例:

```text
20260813_NV_A0_R1
20260813_NV_A0_R2
20260813_NV_A0_R3

20260813_NV_A2_R1
...
```

。

各run folder:

```text
run.json
melonPrimeDS.log
frame.csv
reflex.csv
present.csv
notes.txt
```

を推奨。

---

# 6. Phase 1 — Validation Layer build

## 6.1 repoの現状

melonPrimeDSはDebug configurationで:

```text
MELONDS_VULKAN_ENABLE_VALIDATION=1
```

を`core`へcompile defineする。

`VulkanContext`は:

```text
VK_LAYER_KHRONOS_validation
```

を検出し、有効なら:

```text
[Vulkan] validation layer enabled
```

を出す。

Debug messengerは:

```text
WARNING
ERROR

GENERAL
VALIDATION
PERFORMANCE
```

を取得する。

---

# 7. 通常のMinGW release buildをValidation用に使わない

現在:

```text
tools/build/windows/build-mingw.bat
```

は:

```text
release-mingw-x86_64
MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON
```

。

Releaseなので:

```text
MELONDS_VULKAN_ENABLE_VALIDATION
```

は付かない。

したがって:

> `build-mingw.bat`が成功したこととValidation Layerが動くことは別。

---

# 8. Validation build推奨方法

repoには:

```text
debug-windows-x86_64
```

presetがある。

利用可能なclang/vcpkg Windows environmentなら:

```bat
cmake --preset debug-windows-x86_64 -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON
cmake --build --preset debug-windows-x86_64 --parallel 1
```

。

このbuildの目的はperformanceではない。

---

# 9. MinGWしか使わない場合

現在repoに:

```text
debug-mingw-x86_64
```

presetはない。

既存`release-mingw-x86_64`と同じMinGW/vcpkg environmentを使い、**別build directory**でDebugをconfigureする。

推奨実装:

```text
debug-mingw-x86_64 presetをCMakePresets.jsonへ追加
```

。

内容は`release-mingw-x86_64`を継承し:

```json
"CMAKE_BUILD_TYPE": "Debug"
```

だけ上書きする。

正式なValidation workflowとして今後繰り返すため、ad-hoc commandよりpreset化を推奨。

---

# 10. Validation layer preflight

Vulkan SDK / Khronos Validation Layerをinstall。

起動logで必ず:

```text
[Vulkan] validation layer enabled
```

を確認。

次の場合:

```text
VK_LAYER_KHRONOS_validation is not installed
validation disabled
```

なら:

```text
BLOCKED
```

。

Validation PASSとしない。

---

# 11. Validation Layer version記録

最低:

```text
vulkaninfo
```

等で:

```text
VK_LAYER_KHRONOS_validation
implementationVersion
specVersion
```

を記録。

OS、driverとセットで保存。

---

# 12. Validation Phaseではoverlayを最小化

Validation correctness確認時は:

```text
OBS Vulkan hook
ReShade
RTSS
第三者Vulkan layer
```

等が入っていれば記録。

可能なら一度:

```text
Khronos Validation以外のimplicit layerを無効化
```

したclean runを作る。

理由:

inject layer由来warning/device lifetime問題とmelonPrimeDS本体を分離するため。

---

# 13. Validation Pass A — Core Validation

まず追加validation featureなし。

```text
VK_LAYER_KHRONOS_validation
```

標準core validationだけで実施。

最初から:

```text
GPU Assisted
Synchronization Validation
Best Practices
```

を全部重ねない。

原因切り分けを容易にする。

---

# 14. Core Validation test matrix

以下を最低1回ずつ。

## VSync ON

```text
Policy 0 TelemetryOnly
Policy 1 PresentWait
Policy 2 JustInTime
Policy 3 JustInTimeFifoLatestReady
```

Policy 3は:

```text
extension / surface / present mode対応時のみ
```

。

unsupportedなら:

```text
UNSUPPORTED
```

でありFAILではない。

---

# 15. Reflex state

NVIDIA GPUで:

```text
Reflex Off
Reflex On
Reflex On+Boost
```

。

Reflex active時はgeneric:

```text
BoundedPresentWait
TargetTimeScheduling
```

がOFFになることをlog確認。

---

# 16. VSync OFF

最低:

```text
Policy 2 JustInTime
```

。

期待:

```text
TargetTimeScheduling = OFF
fallback = NonFifoPresentMode
```

。

IMMEDIATE / MAILBOXでtarget time schedulingを有効化しない。

---

# 17. Speed modes

```text
Normal
Fast Forward Hold
Fast Forward Toggle
Slow Motion
```

。

Fast Forward / Slow Motionでは:

```text
generic bounded wait OFF
generic target scheduling OFF
```

を確認。

---

# 18. Window lifecycle

```text
Windowed
Fullscreen
Windowedへ戻る
Resize
DPI change
Minimize
Restore
```

。

確認:

```text
swapchain recreate成功
Validation ERRORなし
recreate stormなし
```

。

---

# 19. Settings lifecycle

```text
F2
Video Settings open
Cancel
Apply
renderer変更なしで閉じる
```

。

確認:

```text
VK_ERROR_DEVICE_LOSTなし
Software fallbackなし
Vulkan option grey-outなし
```

。

---

# 20. Renderer switching

可能なら:

```text
Vulkan → Software → Vulkan
Vulkan → OpenGL Compute → Vulkan
Vulkan → DX12 → Vulkan
```

。

Validation Debug build上でDX12共存がbuildされていない場合は:

```text
NOT AVAILABLE
```

。

無理に追加しない。

---

# 21. ROM lifecycle

```text
ROM launch
savestate load
reset
ROM close
ROM reopen
```

。

Validation ERROR / object lifetime ERRORを確認。

---

# 22. Validation run length

各主要policy:

```text
warm-up 300 frames
+
test 3,000 frames以上
```

推奨。

resize / switch等はevent testなので回数で管理。

例:

```text
fullscreen toggle 20回
resize 50回
F2 open/close 20回
Vulkan ↔ Software 20往復
```

。

---

# 23. Core Validation PASS条件

PASS:

```text
Validation ERROR = 0
```

。

WARNINGは分類する。

## FAIL扱いするwarning

```text
VUID
object lifetime
pNext
invalid feature
invalid extension usage
invalid image layout
invalid synchronization
invalid swapchain usage
invalid present ID
invalid timing metadata
```

。

## 要調査だが即FAILとは限らない

```text
PERFORMANCE warning
best-practice style warning
third-party layer warning
```

。

ただし無視せず記録。

---

# 24. Present Timingで重点確認するVUID

特に:

```text
VkPresentId2KHR
VkPresentTimingInfoEXT
VkPresentTimingsInfoEXT
presentStageQueries
targetTime
timeDomainId
targetTimeDomainPresentStage
VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT
vkSetSwapchainPresentTimingQueueSizeEXT
FIFO_LATEST_READY
pNext chain lifetime
```

。

---

# 25. Device loss確認

`vkWaitForPresent2KHR`がもし:

```text
VK_ERROR_DEVICE_LOST
```

を返した場合:

期待:

```text
Fail("vkWaitForPresent2KHR", VK_ERROR_DEVICE_LOST)
```

へ進み、

```text
swapchain recreate loop
```

へ入らない。

このfailureを意図的に発生させる必要はない。

自然発生した場合にroutingを確認。

---

# 26. Validation Pass B — Synchronization Validation

Core Validation PASS後のみ。

Khronos Validationの:

```text
Synchronization Validation
```

を有効化。

目的:

```text
buffer/image access conflicts
missing barrier
wrong access mask
queue synchronization
```

確認。

現行Vulkan backendはcompute rasterizer + presentation copy/compositionを持つため有益。

---

# 27. Sync Validationの扱い

これは通常Validationよりoverheadが増える。

したがって:

```text
数値measurement禁止
```

。

再度:

```text
Policy 0
Policy 2
Policy 3 if supported
Reflex On
resize
fullscreen
renderer switch
```

を中心に短縮matrixで実施。

---

# 28. GPU-Assisted Validation

今回のpresent pacing完成判定には必須ではない。

使う場合:

```text
別run
```

。

理由:

shader instrumentationを行い、performance / resource layoutへ影響する。

主用途:

```text
descriptor indexing
shader-side invalid access
```

等。

Target-Time JIT / WSI correctnessの第一検査ではない。

---

# 29. Validation Gate

NVIDIA A/Bへ進む条件:

```text
Core Validation:
    ERROR 0

Timing/present関連WARNING:
    0

Sync Validation:
    blocking hazard 0

No:
    DEVICE_LOST loop
    swapchain recreate loop
    Software fallback
    Vulkan grey-out
    hang
    crash
```

。

満たさなければ:

```text
STOP
↓
root cause fix
↓
Validation再実施
```

。

---

# 30. Phase 2 — NVIDIA実機 functional validation

Validation buildを閉じる。

Release-like buildへ切替。

**Validation Layerは必ずOFF。**

---

# 31. A/B buildで避けるもの

Latency比較時:

```text
Debug build
Validation Layer
GPU Assisted Validation
Sync Validation
RenderDoc capture
Nsight frame capture
```

は禁止。

それらはtimingを変える。

---

# 32. Developer featuresにも注意

現状:

```text
MELONPRIME_ENABLE_DEVELOPER_FEATURES
```

有効時:

```text
PresentStageQueries = all supported stages
```

。

production release:

```text
TargetPresentStage only
```

。

したがって**最終A/Bはproduction-like stage queryで行う**。

---

# 33. 推奨A/B instrumentation改善

現状Reflex timing logはdeveloper buildで:

```text
600 framesごと
最大8 reports
最新reportをlog
```

。

functional proofには使えるが、統計A/Bには粗い。

そのため正式A/B前に、必要なら専用:

```text
MELONPRIME_VULKAN_LATENCY_CAPTURE
```

のようなbuild flagを追加することを推奨。

重要:

> `MELONPRIME_ENABLE_DEVELOPER_FEATURES`とは分離する。

---

# 34. Dedicated capture flag要件

default:

```text
OFF
```

。

ONでもrender behaviorを変えない。

特に:

```text
RequestedStageQueries()
```

をdeveloper全stageへ変えない。

A/B capture flagは:

```text
logging / CSV only
```

。

---

# 35. CSV capture fields

最低:

```text
run_id
sample_index
present_id

input_sample_time_us
sim_start_time_us
sim_end_time_us
render_submit_start_time_us
render_submit_end_time_us
present_start_time_us
present_end_time_us
gpu_render_start_time_us
gpu_render_end_time_us

policy
authority
reflex_mode
target_scheduling
bounded_wait
present_mode

target_time_ns
feedback_present_id
feedback_stage_time_ns
baseline_sequence
present_sequence
frame_interval_ns

wait_timeout_count
timing_queue_size
timing_queue_full_count
timing_queue_recovery_count
```

。

---

# 36. Capture frequency

毎framefile I/Oは禁止。

推奨:

```text
driver report query
→ memory buffer/ringへ蓄積
→ run終了時flush
```

。

または:

```text
60～120 framesごとにbatch query
```

。

A/Bの全modeで同じcapture overheadを使う。

---

# 37. Software Reflex metricsの意味

`vkGetLatencyTimingsNV()`は:

```text
vkSetLatencyMarkerNVで付けたmarker timestamp
+
implementation-specific marker
```

を返す。

これはPC内のrender pipeline分析には使える。

ただし:

> **ソフトウェアmarkerだけをclick-to-photon system latencyと呼ばない。**

display scanout / actual pixel responseまで含むend-to-end測定とは別。

---

# 38. End-to-end measurement

対応hardwareがある場合は:

```text
NVIDIA Reflex Analyzer
```

を使用。

対応monitor + mouseなら:

```text
mouse click
↓
display pixel response
```

のSystem Latencyを測れる。

これを最優先のclick-to-photon evidenceとする。

---

# 39. Reflex Analyzerがない場合

代替:

```text
high-speed camera
external photodiode / LDAT系
```

。

それもない場合:

```text
software latency A/B
```

として明記。

結果名:

```text
PC pipeline latency proxy
```

等にする。

**System Latencyと書かない。**

---

# 40. NVIDIA runtime capability preflight

起動logで確認:

```text
GPU = expected NVIDIA GPU
VK_NV_low_latency2 supported/enabled
Present Timing device/surface support
Present ID2
Present Wait2 availability
FIFO_LATEST_READY availability
```

。

extensionなしの場合:

```text
UNSUPPORTED
```

。

コードbug扱いしない。

---

# 41. Reflex functional proof

Reflex On:

```text
actual=active
```

確認。

marker path:

```text
BeginFrame / sleep
INPUT_SAMPLE
SIMULATION_START
SIMULATION_END
RENDERSUBMIT_START
RENDERSUBMIT_END
PRESENT_START
PRESENT_END
```

。

`vkGetLatencyTimingsNV` report:

```text
presentID != 0
timestamps != 0
```

を確認。

---

# 42. ID correlation

同一frameで:

```text
Reflex frame ID
VkLatencySubmissionPresentIdNV
VkPresentIdKHR
VkPresentId2KHR logical ID
latency timing report presentID
```

が対応していること。

最低数sampleをlogで突合。

---

# 43. Generic JIT functional proof

Reflex Off。

Policy:

```text
JustInTime
```

。

warm-up後:

```text
authority = GenericPresentTiming
targetScheduling = capable/active
targetTime != 0
baseline valid
feedback present ID advancing
```

を確認。

---

# 44. JIT bootstrap

swapchain作成直後:

```text
BootstrapWaitingForFeedback
targetTime = 0
```

は正常。

feedback取得後:

```text
targetTime != 0
```

へ遷移すればPASS。

最初からtargetTimeが非0であることを要求しない。

---

# 45. PresentWait2なしのdriver

NVIDIA環境でPresentWait2がもしunsupportedでも:

```text
JustInTime
TargetTimeScheduling ON
BoundedPresentWait OFF
```

なら正常。

前回静的監査で修正したcapability separationのruntime証明になる。

---

# 46. FIFO_LATEST_READY

Policy:

```text
JustInTimeFifoLatestReady
```

。

対応環境のみ:

```text
selected-present-mode=FIFO_LATEST_READY
```

を確認。

unsupported:

```text
UNSUPPORTED
```

。

fallback FIFOをFAIL扱いしない。

---

# 47. Phase 3 — NVIDIA A/B matrix

## Baseline A0

```text
NVIDIA Reflex = Off
VulkanPresentPacingPolicy = TelemetryOnly
VSync = ON
```

目的:

```text
従来host limiter + FIFO
```

baseline。

---

# 48. A1 — PresentWait

```text
Reflex = Off
Policy = PresentWait
VSync = ON
```

目的:

```text
bounded previous-present wait単体
```

。

---

# 49. A2 — Target-Time JIT

```text
Reflex = Off
Policy = JustInTime
VSync = ON
```

目的:

```text
generic target-time scheduling
```

。

---

# 50. A3 — JIT + FIFO Latest Ready

対応時:

```text
Reflex = Off
Policy = JustInTimeFifoLatestReady
VSync = ON
```

。

目的:

```text
time-based scheduling
+
latest-ready FIFO
```

。

unsupportedならskip。

---

# 51. B1 — NVIDIA Reflex On

```text
Reflex = On
Policy = JustInTime
VSync = ON
```

あえてPolicyをJustInTimeにしてよい。

期待:

```text
authority = NvidiaReflex
BoundedPresentWait = OFF
TargetTimeScheduling = OFF
```

。

これでvendor authorityがgenericを正しく抑止することも同時に確認。

---

# 52. B2 — NVIDIA Reflex On+Boost

```text
Reflex = On+Boost
Policy = JustInTime
VSync = ON
```

期待:

```text
lowLatencyMode = true
lowLatencyBoost = true
authority = NvidiaReflex
```

。

---

# 53. Control C0 — VSync OFF

```text
Reflex = Off
Policy = JustInTime
VSync = OFF
```

期待:

```text
TargetTimeScheduling = OFF
fallback = NonFifoPresentMode
```

。

A/B winner選定用ではなくcontract control。

---

# 54. Primary comparison

最重要:

```text
A0 TelemetryOnly
vs
A2 Generic Target-Time JIT
vs
B1 NVIDIA Reflex On
```

。

A3は対応hardwareで追加。

B2は:

```text
power / clock boost trade-off
```

を見る追加mode。

---

# 55. Run repetition

各正式mode:

```text
最低3 runs
```

。

推奨:

```text
warm-up 60秒
measurement 5分以上
```

またはframe count固定:

```text
warm-up 600 frames
measurement 10,000 frames
```

。

frame count固定の方が比較しやすい。

---

# 56. Run order randomization

温度 / clock / background drift対策。

悪い例:

```text
A0 x3
A2 x3
B1 x3
```

。

推奨:

```text
A0
B1
A2
A2
A0
B1
B1
A2
A0
```

等。

各modeがrun前半/後半へ偏らないようにする。

---

# 57. Thermal stabilization

各run開始前:

```text
GPU温度
GPU clock
CPU温度
```

が大きく変動していないことを確認。

On+Boost比較では:

```text
GPU clock固定
```

を強制するとBoostの効果自体を消すため、原則固定しない。

代わりに:

```text
temperature
clock
power
```

を記録。

---

# 58. Background process

A/B中:

```text
Windows Update
browser video
OBS encoding
virus scan
shader compilation
download
```

を避ける。

overlayを使う場合は全modeで同じ。

---

# 59. Frame pacing metrics

最低:

```text
FPS
frame time P50
frame time P95
frame time P99
worst 0.1%相当
```

。

平均FPSだけで評価しない。

---

# 60. Vulkan present metrics

```text
TargetScheduling active ratio
PresentWait timeout count
Timing queue full count
Timing queue recovery count
Swapchain recreation count
DEVICE_LOST count
fallback reason count
```

。

---

# 61. Recommended thresholds

これはVulkan仕様の規定値ではなく、**melonPrimeDS project acceptance criterion**。

steady-state supported JIT run:

```text
TargetScheduling active
    >= 95%
```

を目標。

bootstrap / swapchain recreate直後は除外。

---

# 62. Queue acceptance

安定run:

```text
TimingQueueFullCount = 0
TimingQueueRecoveries = 0
```

が理想。

発生した場合:

```text
FAIL
```

と即断せず、

```text
driver feedback latency
queue size
developer instrumentation accidentally enabled
```

を確認。

release-like target-stage-only buildで再測定。

---

# 63. PresentWait timeout

timeoutはboundedなのでcorrectness failureではない。

ただし:

```text
timeout rate > 1%
```

なら要調査。

これはproject diagnostic thresholdでありVulkan spec requirementではない。

---

# 64. Reflex software timing metrics

`VkLatencyTimingsFrameReportNV`から可能な差分:

```text
Input → Simulation Start
Simulation duration
Input → Render Submit Start
Render Submit duration
Input → Present Start
Present duration
Input → GPU Render End
```

。

同じ定義で全Reflex sampleを算出。

---

# 65. Generic JITとReflexの比較注意

generic JITにはNVIDIA Reflex marker reportがない。

そのため:

```text
vkGetLatencyTimingsNV
```

だけでA2とB1を完全比較できない。

共通metricには:

```text
host frame timestamps
external click-to-photon
frame pacing
```

を使う。

Reflex marker timingはB1/B2内部の追加diagnostic。

---

# 66. 共通software timestampを追加する場合

A/B専用captureとして:

```text
input sampled
simulation start/end
queue submit
queue present
```

をQPC / SDL performance counterで全mode共通capture。

ただし:

```text
hot pathでfile I/O禁止
```

。

memory bufferへ保存。

---

# 67. External Reflex Analyzerがある場合

最優先metric:

```text
System Latency
P50
P95
P99
```

。

同じmouse movement/click patternを多数sample。

NVIDIA Reflex Analyzerはcompatible mouseで:

```text
mouse click
→ resulting display pixel change
```

まで計測できる。

---

# 68. Visual event

Metroid Prime Hunters上でA/B用に:

```text
click
→ 明確なpixel変化
```

が出る動作を固定。

例:

```text
射撃
UI flash
crosshair reaction
```

。

ただしゲームロジック自体のランダム要素が少ないものを選ぶ。

必要ならmphCodexのゲーム調査成果から最も安定したvisual response eventを選定する。

---

# 69. Sample count

External latency:

```text
最低100 clicks / run
```

推奨:

```text
200～500 clicks / run
```

。

平均だけでなくpercentileを見る。

---

# 70. A/B結果table

最終report:

| Mode | Runs | FPS | FT P50 | FT P95 | FT P99 | Latency P50 | P95 | P99 | QueueFull | WaitTimeout | Status |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| A0 Telemetry | 3 | | | | | | | | | | |
| A1 PresentWait | 3 | | | | | | | | | | |
| A2 JIT | 3 | | | | | | | | | | |
| A3 JIT Latest | 3 | | | | | | | | | | |
| B1 Reflex On | 3 | | | | | | | | | | |
| B2 Reflex Boost | 3 | | | | | | | | | | |

---

# 71. Status classification

使うstatus:

```text
PASS
REGRESSION
NO MATERIAL DIFFERENCE
UNSUPPORTED
BLOCKED
NOT RUN
```

。

「なんとなく速い」は判定に使わない。

主観は:

```text
notes
```

へ別記。

---

# 72. Winner判定

候補をwinnerにする条件:

```text
correctness regressionなし
60FPS/emulation pacing維持
P95/P99 frame time悪化なし
latency P50/P95改善
3 runsで同方向
```

。

1runだけ勝ったmodeをwinnerにしない。

---

# 73. On+Boost判定

B2をB1より推奨する条件:

```text
P95/P99 latency
or
latency variance
```

に再現性ある改善。

改善がなく:

```text
power / temperatureだけ増える
```

なら:

```text
On
```

を推奨。

---

# 74. Generic JIT採用判定

A2/A3を一般default候補へ上げる条件:

```text
Validation clean
NVIDIA runtime stable
60 / 120 / 144Hzでstable
VRR ON/OFFでstable
queue full問題なし
frame pacing regressionなし
latency benefitまたは明確なpacing benefit
```

。

NVIDIA 1環境だけで:

```text
TelemetryOnly default
→ JIT default
```

へ即変更しない。

AMD / Intel / Linux / MoltenVKも後続確認が必要。

---

# 75. Default policy

この工程中も:

```text
TelemetryOnly
```

をdefault維持。

A/Bのためにconfig値を変えるだけ。

source defaultを変更しない。

---

# 76. Failure triage

## Validation ERROR

```text
STOP A/B
```

。

VUIDごとに:

```text
API call
object
present ID
frame
policy
Reflex mode
```

を記録し根本修正。

---

# 77. Queue Full

release-like buildで:

```text
VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT
```

が繰り返す場合:

```text
timing queue size
present rate
feedback latency
reportComplete
target-stage query
```

を確認。

developer all-stage buildの結果と混同しない。

---

# 78. DEVICE_LOST

1回でも:

```text
VK_ERROR_DEVICE_LOST
```

が出た場合:

```text
REGRESSION / BLOCKER
```

としてsessionを止める。

取得:

```text
last 500 log lines
policy
Reflex mode
swapchain mode
fullscreen state
renderer switch history
driver version
```

。

---

# 79. F2 regression

次のどれか:

```text
F2後device lost
Software fallback
Vulkan grey-out
renderer stuck
```

が出ればblocker。

以前の修正の再発として扱う。

---

# 80. Validation evidence保存

runごと:

```text
full log
Validation ERROR count
Validation WARNING count
VUID一覧
environment metadata
```

。

「画面上で何も出なかった」だけでPASSにしない。

---

# 81. A/B evidence保存

最低:

```text
raw CSV
aggregation script
summary CSV
environment
commit SHA
config snapshot
```

。

後から再計算できるようraw dataを残す。

---

# 82. 集計

run単位:

```text
P50
P95
P99
mean
stddev
sample count
```

。

さらにmode単位では:

```text
各runのP50/P95/P99
```

を並べる。

全runの全frameを単純mergeするだけにしない。

長いrunが過剰weightになる。

---

# 83. 最終report構成

```text
1. Tested SHA
2. Environment
3. Validation result
4. Extension capability
5. Functional runtime result
6. A/B matrix
7. Raw metrics
8. Aggregated metrics
9. Regression
10. Unsupported
11. Winner / No material difference
12. Remaining NOT RUN
13. Recommendation
```

---

# 84. Validation completion checklist

```markdown
- [ ] Debug build
- [ ] VK_LAYER_KHRONOS_validation installed
- [ ] log: validation layer enabled
- [ ] Policy 0 core validation
- [ ] Policy 1 core validation
- [ ] Policy 2 core validation
- [ ] Policy 3 if supported
- [ ] Reflex Off
- [ ] Reflex On
- [ ] Reflex Boost
- [ ] VSync ON/OFF
- [ ] fullscreen/windowed
- [ ] resize/minimize/restore
- [ ] F2
- [ ] renderer switching
- [ ] Fast Forward/Slow Motion
- [ ] Core Validation ERROR=0
- [ ] present/timing related VUID=0
- [ ] Sync Validation blocking hazard=0
```

---

# 85. NVIDIA functional checklist

```markdown
- [ ] expected NVIDIA GPU selected
- [ ] VK_NV_low_latency2 enabled
- [ ] Reflex actual=active
- [ ] Reflex On
- [ ] Reflex On+Boost
- [ ] present ID correlation verified
- [ ] vkGetLatencyTimingsNV report exists
- [ ] marker timestamps non-zero
- [ ] JIT bootstrap → active transition
- [ ] targetTime non-zero
- [ ] FIFO_LATEST_READY checked
- [ ] no queue full storm
- [ ] no device lost
- [ ] no F2 regression
```

---

# 86. A/B checklist

```markdown
- [ ] Validation OFF
- [ ] Release-like build
- [ ] production-like TargetPresentStage query
- [ ] same commit
- [ ] same driver
- [ ] same monitor mode
- [ ] same scene/savestate
- [ ] same resolution
- [ ] A0 x3
- [ ] A1 x3
- [ ] A2 x3
- [ ] A3 x3 if supported
- [ ] B1 x3
- [ ] B2 x3
- [ ] randomized run order
- [ ] raw data retained
- [ ] P50/P95/P99 calculated
- [ ] queue/wait counters recorded
- [ ] result classified
```

---

# 87. 完了判定

## Validation PASS

```text
Validation ERROR = 0
timing/present VUID = 0
blocking synchronization hazard = 0
runtime lifecycle regression = 0
```

。

## NVIDIA Functional PASS

```text
Reflex active
Reflex reports valid
JIT active when selected
authority switching correct
frame rate stable
no device loss
```

。

## A/B COMPLETE

```text
minimum 3 runs per supported main mode
same conditions
raw data
percentiles
result classification
```

。

---

# 88. 今回はまだPASSと書いてはいけない項目

実施前:

```text
Validation Layer
NVIDIA runtime
NVIDIA Reflex latency improvement
Generic JIT latency improvement
FIFO_LATEST_READY latency improvement
click-to-photon improvement
```

は:

```text
NOT RUN
```

。

---

# 89. Khronos一次資料

Validation Overview:

https://docs.vulkan.org/guide/latest/validation_overview.html

Development Tools / Validation features:

https://docs.vulkan.org/guide/latest/development_tools.html

Synchronization:

https://docs.vulkan.org/guide/latest/synchronization.html

VK_EXT_present_timing:

https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html

vkGetPastPresentationTimingEXT:

https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingEXT.html

VK_NV_low_latency2:

https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_low_latency2.html

vkGetLatencyTimingsNV:

https://docs.vulkan.org/refpages/latest/refpages/source/vkGetLatencyTimingsNV.html

---

# 90. NVIDIA一次資料

NVIDIA Reflex:

https://www.nvidia.com/en-us/geforce/technologies/reflex/

NVIDIA Reflex Analyzer guide:

https://www.nvidia.com/en-us/geforce/news/reflex-latency-analyzer-360hz-g-sync-monitors/

NVIDIA Performance Tools:

https://developer.nvidia.com/rendering-performance

---

# 91. 最終指示

順序を変えない。

```text
Static audit
    DONE
↓
Core Validation Layer
↓
Synchronization Validation
↓
Validation clean
↓
Release-like NVIDIA functional test
↓
A/B
↓
external latency measurement if available
↓
result analysis
```

Validation buildのlatency値をA/Bへ持ち込まない。

またNVIDIAのsoftware marker timingを:

```text
click-to-photon
```

と誤表記しない。

最終的に必要なのは:

> **低遅延機能が「動く」ことだけでなく、correctnessを崩さず、同一条件で測定して実際に改善したと証明できること。**
