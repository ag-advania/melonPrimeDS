# melonPrimeDS 最新Push再監査
## VUID-03268修正 / Phase 1 Event Matrix / Formal Phase 3 A/B 最終監査

- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver2`
- 監査時HEAD: `ce706e3032dd13f8c9b9def105972634598dd016` (`Fix linux build`)
- 監査作業tree: 上記HEADに対するP1 follow-up実装を含むdirty tree
- 監査日: 2026-08-13
- Backend: **現行Vulkan clean-room backend**
- 関連commit:
  - `e5ce3d6bd815ac9117a5e49aa9e6d37cfdb68772` — present markers inside queue lock
  - `c30fa1ba25859ba2a5520a163ecc33ee9882f2bd` — event matrix automation
  - `a2bc6bdeb3d8f4136c3d780bcd79546c373c8aa3` — queue-full retry semaphore fix
  - `0cea66e0d25521d4d5d975119d4068729e840c8a` — VSync-off control / manual-row documentation
  - `ce706e303` — current HEAD; Linux build fix（本監査の起点）

---

# 1. 結論

今回提示された作業内容を、最新branch、実コード、保存済みruntime evidence、runbook、
Khronos Vulkan仕様まで突き合わせた。

```text
PRESENT marker vs queue mutex              FIXED / PASS
PRESENT_END vs bookkeeping                 FIXED / PASS
wait timeout warmup補正                    IMPLEMENTED
VUID-03268 reproduction                    CONFIRMED
VUID-03268 root cause                      CONFIRMED
queue-full retry semaphore removal         IMPLEMENTED / SPEC-CONSISTENT
core validation event matrix after fix     PASS
minimize retry-path coverage               PASS / NON-VACUOUS
window event automation                    IMPLEMENTED / PASS
Sync Validation current-tree gate          PASS / banner-confirmed / hazard 0
swapchain counter reset guard              IMPLEMENTED / STATIC PASS
AMD Anti-Lag PRESENT queue-lock placement  FIXED / STATIC PASS
runbook stale status                       FIXED
latency benefit                            NOT MEASURED
default                                    TelemetryOnly
```

`VUID-vkQueuePresentKHR-pWaitSemaphores-03268` の根本原因は、
**`VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT` で最初のpresentがrejectされた後も、
そのcallに含まれるsemaphore wait operationsはqueueへenqueue済みなのに、
retryで同じbinary semaphoreを再びwaitしていたこと**で確定してよい。

今回の再監査で、実装として以下を閉じた。

```text
P1-A  Sync Validation用ハーネスとbanner/VUID/SYNC-HAZARD検出を実装し、current-tree runtime gateをPASS
P1-B  swapchain_generationをcaptureへ追加し、測定窓跨ぎをINVALID化
P1-C  AMD Anti-Lag PRESENT updateをqueue mutex取得後へ移動
P2    runbook上部のevent matrix旧statusとstderr監視を更新
```

したがって現時点では:

```text
Relative JIT mechanism      UNBLOCKED
Window lifecycle core val   ARCHIVED PASS (pre-follow-up runtime evidence)
Sync Validation new-path    CURRENT-TREE PASS / targeted minimize gate
Formal Phase 3 A/B          NOT RUN / GATE CLOSED
Latency benefit             NOT MEASURED
```

---

# 2. Current HEAD

監査時点のbranch HEADは:

```text
ce706e3032dd13f8c9b9def105972634598dd016
```

`a2bc6bdeb...`の後にVSync-off control、手動rowの記録、Linux build fixが追加されているため、
`a2bc6bdeb`単体ではなく`ce706e3032dd13f8c9b9def105972634598dd016`をcurrent source of truthとする。本ファイルの
P1 follow-up差分はこのHEAD上の未コミット作業treeとして監査する。

---

# 3. Present marker / queue mutex

現在:

```cpp
std::unique_lock<std::mutex> queueLock(Device.GetQueueMutex());

Reflex.MarkPresentStart();
LatencyCapture.MarkPresentStart();

vkQueuePresentKHR(...);

LatencyCapture.MarkPresentEnd();
Reflex.MarkPresentEnd();

queueLock.unlock();

PresentPacer.NotifyPresentResult(...);
LatencyCapture.Commit(...);
Reflex.NotifyPresented();
```

前回指摘したmutex acquisition/releaseとpost-present bookkeepingは
Reflex/host present spanから外れている。

**PASS**

`unique_lock + explicit unlock()`にしたことで、

```text
lock → START → QueuePresent → END → unlock
```

をCI contractで直接検証できる点も妥当。

---

# 4. Timeout-rate修正

aggregatorには:

```text
wait_timeouts_at_warmup
wait_timeouts_in_window
wait_timeout_rate
```

が追加され、

```text
window timeout / bounded_wait_attempted
```

でrateを計算する。

synthetic example:

```text
warmup timeout       33
measurement timeout   3
attempted waits      600
```

では、旧式の6%ではなく0.5%となる。

1% acceptance thresholdを跨ぐため、これはmeasurement correctness修正として有効。

ただし後述のswapchain counter reset問題が残る。

---

# 5. Phase 1 Event Matrix automation

`tools/testing/vulkan-present-event-matrix.ps1`で:

```text
resize
minimize
fullscreen
idle
all
```

をWin32から駆動可能。

VUIDまたはDEVICE_LOSTを検出するとexit 1。

自動化対象のevent stressとして妥当。

---

# 6. VUID修正前の切り分け

修正前:

| Phase | Rebuilds | VUID-03268 |
|---|---:|---:|
| resize x40 | 42 | 0 |
| minimize/restore x20 | 22 | 20 |
| idle | 2 | 0～2 |

この結果から、swapchain recreation一般ではなく、
minimize/restore時に起きる特殊条件と強く相関することが分かった。

marker/lock変更前の`33c4c6849...`でも
24 rebuild中20件だったため、marker移動が原因ではないことも確認できる。

---

# 7. 否定済み仮説

render-finished semaphore destructionをold swapchain destruction後まで遅延する案を実装しても:

```text
22 rebuild / 20 VUID
```

で変化なし。

その変更をrevertした判断は正しい。

結論:

```text
semaphore destruction ordering単独は原因ではない
```

---

# 8. Instrumentationによる根本原因

保存されたtraceでは:

```text
submit signal semaphore = S
present1 = VK_SUCCESS
...
present1 = VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT
present RETRY
直後に VUID-03268
```

が繰り返される。

同じimage/semaphoreのretryでのみVUIDが発生しており、
timing queue full → retryが直接trigger。

---

# 9. Khronos仕様との一致

Vulkan WSI仕様では
`VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT`でpresentation requestがrejectされても:

```text
set of queue operations are still considered enqueued
semaphore wait operations execute
```

と明記されている。

また`VK_EXT_present_timing` proposalもqueue-full時に:

```text
wait for results
grow results queue
present again without timing data
```

を選択肢としている。

したがって:

```text
first call:  binary semaphore Sをwait
retry:       同じSをもう一度wait
```

は不正。

最初のwaitがSをconsumeするため、retry側のwaitに対応するsignalが無い。
VUID-03268と一致する。

**ROOT CAUSE CONFIRMED**

---

# 10. 現在のVUID修正

`PrepareRetryWithoutTiming()`は現在:

```cpp
present.waitSemaphoreCount = 0;
present.pWaitSemaphores = nullptr;
```

としてからtiming metadataを外してretryする。

target/capture stateも:

```text
TimingAttached = false
TargetValueNs = 0
TargetMode = None
RelativeRequest = {}
```

へclear。

**PASS**

同一present queueでは最初のcallでwait operationが既にqueueへ入っており、
retry presentationはその後続で処理されるため、waitを再指定しない設計は合理的。

---

# 11. 修正後のruntime evidence

修正後:

| Phase | Rebuilds | Validation | DEVICE_LOST |
|---|---:|---|---:|
| resize x40 | 41 | clean | 0 |
| minimize/restore x20 | 22 | clean | 0 |
| fullscreen x8 | 8 | clean | 0 |
| idle | 2 | clean | 0 |

minimize runではqueue-full retryが9回発生した状態でもclean。

追加runでも9 / 10 / 10 retriesと記録されている。

したがって:

> retry pathが発火しなかったためcleanだった

というvacuous passではない。

**CORE VALIDATION PASS / NON-VACUOUS**

---

# 12. VSync-off control

`-NoVSync`は`Screen.VSync=false`を書き、
tested NVIDIA surfaceではIMMEDIATEを選択。

ただしtiming queueをfillしたのはNoVSyncではなくminimize/restore。

従って:

```text
-NoVSync     contract control
minimize     queue-full retry stress
```

という整理で正しい。

---

# 13. Manual-only rows

自動化済み:

```text
resize
minimize/restore
fullscreen
idle
```

未完了:

```text
DPI change
Video Settings open/cancel/apply
renderer switching
ROM launch / savestate / reset / close / reopen
Fast Forward
Slow Motion
```

SendKeysがQActionへは届くが`HK_*` pathへ届かないこと、
`HK_SlowMo`にkeyboard bindingが無いことを記録し、
常にfalse-passするphaseをrevertした判断は妥当。

---

# 14. P1-A — Sync Validation harness

`a2bc6bdeb`は:

```text
retry VkPresentInfoKHRからwait semaphoreを除去
```

という同期契約の変更。

Core ValidationでVUID 0なのは保存済みevidenceで確認済みであり、
さらに本作業treeのDebug + Synchronization Validationでも実行確認した。
最初のcurrent-tree runでは、queue-full retryを同一present imageへ再実行した際の
`SYNC-HAZARD-WRITE-AFTER-PRESENT`を20件検出した。これはVUID-03268修正後に
初めて見えた実同期問題として扱い、queue満杯前のmetadata停止と、完了reportだけを
queue解放と数える修正を追加した。

Formal gate自身が:

```text
synchronization blocking hazard = 0
```

を要求しているため、最低:

```text
validate_sync=true
Phase=minimize
queue-at-capacity > 0
queue growth > 0
queue-full errors = 0 (capacity guard active)
SYNC-HAZARD 0
VUID 0
DEVICE_LOST 0
```

である。

今回、`tools/testing/vulkan-present-event-matrix.ps1`へ`-ValidateSync`を追加した。
このswitchは一時的な`vk_layer_settings.txt`を生成し、
`CURRENT-VALIDATION-ENABLED` bannerに**Synchronization**が出ること、
stdout/stderr双方のVUID・`SYNC-HAZARD`・`DEVICE_LOST`を確認し、終了時に設定を復元または削除する。
実行コマンドは:

```powershell
powershell -ExecutionPolicy Bypass -File tools\testing\vulkan-present-event-matrix.ps1 `
  -Rom <rom> -Phase minimize -ValidateSync -Tag min-sync1
```

current-treeで実行した実コマンドと結果は:

```text
Tag: min-sync-final2
ROM: C:\DSMPH\melonPrimeDS最新版\balancedRom.nds
Build: build\debug-mingw-vulkan-validation2 (MELONDS_VULKAN_ENABLE_VALIDATION=1)
swapchain rebuilds: 26
device lost: 0
Synchronization banner: confirmed
SYNC-HAZARD: 0
VUID: 0
queue-at-capacity events: 22
queue growth events: 10 (16 -> 32; queue-full errors=0)
exit: 0
```

初回のcurrent-tree Sync runはhazard 20件でexit 1となったが、上記修正後の同じ
minimize/restore gateはhazard 0でPASSした。なお、queue-at-capacity guardが
正常に働くため、current-treeのSync runでは不正な実queue-full retryを発生させず、
pressure branchとdrain/growth branchを非vacuousに検証している。VUID修正そのものの
retry semaphore除去は既存のcore after-fix evidenceと静的契約監査でも保持される。

**P1-A runtime gate: PASS**

**P1 / RUNTIME VALIDATION GATE**

---

# 15. P1-B — counter lifecycle measurement guard

aggregator/runbookは`wait_timeout_count`をwarmup補正後の累積値として扱う。

しかし`VulkanPresentPacer::OnSwapchainCreated()`で:

```cpp
WaitTimeouts = 0;
TimingQueueFullCount = 0;
```

`ResetTimingLifecycle()`で:

```cpp
TimingQueueRecoveries = 0;
```

となる。

つまりこれらは**swapchain-local counter**。

measured window中にswapchain recreationが入ると:

```text
old counter → 0 reset → new counter
```

になり、現aggregatorの:

```text
final - warmup baseline
last row queue_full
last row recovery
```

では過小集計し得る。

Formal A/Bでunexpected recreationが起きても
silentにcleanに見える可能性がある。

**P1 / MEASUREMENT INTEGRITY — IMPLEMENTED**

採用した設計は保守的なA:

```text
A. measured window中にswapchain recreateしたrunをINVALID
B. reset-aware delta accumulation（今回は採用しない）
```

`VulkanPresentPacer`のlifetime monotonicな`SwapchainGeneration`を追加し、
各latency capture rowへ`swapchain_generation`として出力する。
`aggregate-vulkan-latency.py`は測定window内の世代変更を`INVALID`にし、
reset後のcounterをrun totalとして報告しない。warmup境界以前のrecreateは、
測定開始時点の世代が一つなら許可する。

静的契約監査で、世代インクリメント・capture列・INVALID判定を確認済み。

---

# 16. P1-C — AMD Anti-Lag PRESENT位置

修正前:

```cpp
AntiLag.EndFrame(...);

std::unique_lock<std::mutex> queueLock(...);

Reflex.MarkPresentStart();
LatencyCapture.MarkPresentStart();
vkQueuePresentKHR(...);
```

となっている。

Khronos `VK_AMD_anti_lag`では
PRESENT stageは`vkQueuePresentKHR`前であり、
reference exampleでも:

```text
vkAntiLagUpdateAMD(PRESENT)
vkQueuePresentKHR
```

と連続する。

両者の間にqueue mutex acquisitionがあるため、
mutex contentionが入る可能性がある。

Reflex markerをmutex内へ移したのと同じ原則で、
Anti-Lag PRESENTをmutex取得後へ移動した。現在の順序は:

```cpp
std::unique_lock<std::mutex> queueLock(...);
AntiLag.EndFrame(...);
Reflex.MarkPresentStart();
LatencyCapture.MarkPresentStart();
vkQueuePresentKHR(...);
```

これでqueue ownership取得前の待ち時間がAMD PRESENT updateとqueue operationの間に入らない。
静的契約監査はPASS。AMD実機でのruntime validationは未実行であり、
**AMD runtime gateは別途残る**。

ただしこれはNVIDIA A/BではAnti-Lagがactiveでないため、
**NVIDIA Phase 3 blockerではない**。

---

# 17. P2 — runbook status

runbook上部には以前:

```text
event matrixはNOT RUN
fullscreen/resize/minimize等はperson required
```

という旧記述が残る一方、Phase 1本体では:

```text
resize      [x]
minimize    [x]
fullscreen  [x]
idle        [x]
```

になっている。

document内部に矛盾があった。

上部を:

```text
Automated/completed:
  resize, minimize/restore, fullscreen, idle

Still manual:
  DPI, Video Settings, renderer switching,
  ROM lifecycle, Fast Forward, Slow Motion
```

へ訂正した。現在は自動完了4行とmanual残件を分離し、
Sync Validationのcurrent-tree targeted gate PASSとmanual残件を併記している。

**P2 status: FIXED**

---

# 18. P2 — event harness stderr

scriptはstdoutとstderrを別fileへredirectしており、以前のVUID集計はstdoutのみだった。

今回のknown validation VUIDはstdout callback経由で検出できているため
今回のpassを否定するものではない。

今回、`out.log + err.log`の両方をVUID、SYNC-HAZARD、DEVICE_LOST、swapchain再生成の
判定対象にした。`-ValidateSync`時はvalidation bannerも必須にする。

**P2 status: FIXED**

---

# 19. Formal Phase 3開始条件

現在:

```text
[x] Relative JIT active on NVIDIA
[x] FIFO_LATEST_READY reachable
[x] target capture = actual present
[x] aggregator contradictory rows = INVALID
[x] relative cadence generation evidence
[x] PRESENT marker inside queue mutex
[x] bounded wait allowed/attempted split
[x] queue-full retry VUID fixed
[x] automated window matrix clean
[x] retry path exercised after fix
[x] swapchain_generation guard for measurement counters
[x] AMD Anti-Lag PRESENT update after queue-lock acquisition
[x] runbook stale event-matrix status corrected
[x] current-tree Sync Validation banner / VUID / SYNC-HAZARD gate
[x] queue-at-capacity pressure and timing-queue growth path

[ ] DPI
[ ] Video Settings
[ ] renderer switching
[ ] ROM lifecycle
[ ] Fast Forward
[ ] Slow Motion
```

従って、manual lifecycle rowsと正式A/B runtime測定をまだ満たしていないため:

```text
FORMAL PHASE 3 A/B = NOT READY YET / GATE CLOSED
```

Relative JIT mechanismそのもののblockerとcounter measurement guardは解除済み。

残るのはmanual lifecycleと正式A/B runtime測定である。

---

# 20. Latency status

現時点で言える:

```text
Relative scheduling implemented                  YES
Relative scheduling active on tested NVIDIA      YES
FIFO_LATEST_READY reachable                      YES
VUID-03268 root cause/fix                         PASS
Window event core validation                     PASS
```

まだ言えない:

```text
Relative lowers latency                          NOT MEASURED
Relative beats PresentWait                       NOT MEASURED
Relative beats TelemetryOnly                     NOT MEASURED
Relative beats Reflex                            NOT MEASURED
FIFO_LATEST_READY is faster                      NOT MEASURED
On+Boost is best                                 NOT MEASURED
```

---

# 21. Default

```text
TelemetryOnly
```

維持。

**PASS**

---

# 22. GitHub Actions

監査HEADについてconnector上:

```text
combined statuses       none
associated workflow run none
```

従って:

```text
GitHub Actions = NOT VERIFIED
```

local build/audit報告とは分離する。

---

# 23. 推奨次作業順

```text
1. manual Phase 1
   DPI / dialog / renderer / ROM / speed

2. Formal NVIDIA Phase 3 A/B
   counter guardを含むcurrent-tree captureで実施

3. AMD Anti-Lag runtime validation
   AMD deviceでPRESENT stageとqueue orderingを確認

4. click-to-photon

5. Intel / Linux / MoltenVK
```

---

# 24. 主要監査対象

```text
src/frontend/qt_sdl/MelonPrimeVulkanPresenter.cpp
src/VulkanPresentPacer.cpp
src/VulkanPresentPacer.h
src/VulkanPresentLatencyCapture.cpp
tools/testing/vulkan-present-event-matrix.ps1
tools/perf/aggregate-vulkan-latency.py
tools/ci/audits/audit-low-latency-contract.py
docs/development/testing/vulkan-present-pacing-runbook.md
docs/archive/audits/vulkan/2026-08-13-event-matrix/README.md
docs/archive/audits/vulkan/2026-08-13-event-matrix/minimize-vuid-rootcause-trace.log
docs/archive/audits/vulkan/2026-08-13-event-matrix/min-after-fix.log
docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-current.out.log
docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-current.err.log
docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-final2.out.log
docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-final2.err.log
```

---

# 25. 公式仕様

Khronos Vulkan WSI:

```text
https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html
```

`VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT`でも
semaphore waitsを含むqueue operationsはenqueue済み。

VK_EXT_present_timing proposal:

```text
https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html
```

queue-full時に「present again without timing data」が明示される。

VK_AMD_anti_lag:

```text
https://docs.vulkan.org/features/latest/features/proposals/VK_AMD_anti_lag.html
```

PRESENT stageは`vkQueuePresentKHR`前。

---

# 26. 最終監査総括

今回のVUID調査は、phase分離、pre-change比較、失敗仮説のrevert、
instrumentation、同一reproによるafter-fix verificationまで揃っている。

根本原因は:

```text
VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT
↓
最初のpresentのbinary semaphore waitはenqueue済み
↓
retryが同じbinary semaphoreを再wait
↓
signal sourceが無くVUID-03268
```

で、Khronos仕様とも一致する。

修正後もminimize phaseでretryが実際に9回以上発生した状態で
core validation cleanなので、VUID fixは**非vacuousなruntime PASS**と判定してよい。

今回のfollow-upでは、Sync Validationを再現可能にするハーネス、
queue満杯前のtiming metadata停止と完了reportベースのqueue recovery、
swapchain-awareなcounter guard、AMD PRESENTのqueue-lock境界、runbookのstatus整合を
実装した。current-tree Sync Validationはbanner確認・VUID 0・SYNC-HAZARD 0・
DEVICE_LOST 0でPASSした。一方、manual lifecycleとFormal A/Bのlatency結果は未実施であり、
主張しない。

最終status:

```text
Relative-Time Scheduling          IMPLEMENTED / PASS
NVIDIA functional runtime         PASS
Target capture integrity          PASS
Present marker boundary           PASS
VUID-03268                        FIXED / PASS
Automated window matrix           PASS
Sync Validation current-tree      PASS / banner confirmed / SYNC-HAZARD 0
Swapchain counter guard           IMPLEMENTED / STATIC PASS
AMD Anti-Lag queue-lock placement FIXED / STATIC PASS / RUNTIME PENDING
Runbook status                    FIXED
Formal Phase 3 A/B                NOT RUN
A/B readiness                     GATE CLOSED
Latency benefit                   NOT MEASURED
Default                           TelemetryOnly
```

---

# 27. この再監査で実行した検証

```text
python -m py_compile tools/perf/aggregate-vulkan-latency.py
python -m py_compile tools/ci/audits/audit-low-latency-contract.py
python tools/ci/audits/audit-low-latency-contract.py   PASS
PowerShell event-matrix script parse                 PASS
git diff --check                                      PASS
build-mingw.bat --jobs 1                              CONFIGURE OK; blocked by existing Ninja
  failed recompaction: Permission denied in build/release-mingw-x86_64
build-mingw-existing.bat --jobs 1 --build-dir build/rebuild-mingw-x86_64
  BUILD PASS; Vulkan timing tests PASS; XeLL tests PASS
build-mingw-existing.bat --jobs 1 --build-dir build/debug-mingw-vulkan-validation2
  BUILD PASS; MELONDS_VULKAN_ENABLE_VALIDATION=1; unit tests PASS
aggregator synthetic generation guard                 PASS
current-tree Sync Validation minimize/restore        PASS; 26 rebuilds; banner confirmed;
  VUID 0; SYNC-HAZARD 0; DEVICE_LOST 0; pressure 22; growth 10; exit 0
first current-tree Sync attempt                     FAIL as intended evidence;
  22 rebuilds; SYNC-HAZARD-WRITE-AFTER-PRESENT 20; fixed by capacity guard
```

未実行:

```text
AMD runtime validation
Formal Phase 3 A/B latency capture
manual DPI/dialog/renderer/ROM/speed rows
```

未実行項目は、AMD実機、正式A/B測定、manual lifecycleに限定される。current-treeの
Sync Validation runtime gateは、外部ROM/GPUを使って実行しPASSを記録済みである。
