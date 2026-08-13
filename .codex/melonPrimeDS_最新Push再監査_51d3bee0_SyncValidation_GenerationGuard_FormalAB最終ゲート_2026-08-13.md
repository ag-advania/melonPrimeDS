# melonPrimeDS 最新Push再監査
## Sync Validation / Swapchain Generation Guard / AMD Anti-Lag / Formal Phase 3 最終ゲート

- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver2`
- 監査HEAD: `51d3bee0800c8fb484599a6661b7d582f3ec87c4`
- Commit: `vulkan: harden present timing sync validation`
- 監査日: 2026-08-13
- Backend: **現行Vulkan clean-room backend**
- 参照した前回監査文書（親コミット `ce706e303`）:
  `.codex/melonPrimeDS_最新Push再監査_VUID03268修正_Phase1EventMatrix_FormalAB最終監査_2026-08-13.md`

---

# 1. 結論

今回Pushされた変更について、以下は実コード・runbook・保存evidence上で確認できた。

```text
VUID-03268 retry semaphore fix                PASS
present marker queue-lock boundary            PASS
queue-at-capacity proactive guard             IMPLEMENTED / PASS
complete-report-only slot release              IMPLEMENTED / SPEC-CONSISTENT
swapchain_generation capture                  IMPLEMENTED / PASS
generation change INVALID detection            IMPLEMENTED
AMD Anti-Lag PRESENT queue-lock placement      FIXED / STATIC PASS
Sync Validation harness                        IMPLEMENTED
stdout + stderr validation scan                IMPLEMENTED
Sync Validation actual-config self-check       PASS / intended configuration
Sync Validation saved runtime evidence         PASS / Policy2 + ReflexOff + VSyncOn
INVALID output suppression                     IMPLEMENTED / SYNTHETIC PASS
warmup-generation boundary guard               IMPLEMENTED / SYNTHETIC PASS
test config backup/restore                     PASS / byte-for-byte
runbook stale status                           FIXED
default pacing policy                          TelemetryOnly
latency benefit                                NOT MEASURED
```

従来の`min-sync-final2` evidenceは実在するが、effective configurationは
`TelemetryOnly / Reflex On / VSync Off`だった。harness修正後の意図設定runでは:

```text
CURRENT-VALIDATION-ENABLED
Synchronization listed
VUID 0
SYNC-HAZARD 0
DEVICE_LOST 0
Policy=JustInTime
Reflex requested=off actual=inactive
requested-vsync=on selected-present-mode=FIFO
config restore=PASS layer restore=PASS
23 swapchain rebuilds
queue-at-capacity 13
queue growth 4
exit 0
```

まで確認できる。

したがって、前回指摘した

```text
P1-A Sync Validation
P1-B swapchain counter lifecycle
P1-C AMD Anti-Lag PRESENT placement
P2 runbook / stderr scan
```

に加え、今回新たに発見したP1/P2のmeasurement integrity問題も修正・再検証した。

ただし、正式A/B前の再監査で新たに以下を確認した。

```text
P1-1 FIXED:
Sync Validation harnessがportable config rootへ書き、
runtime logでeffective policy/Reflex/VSync/present modeをself-check

P1-2 FIXED:
aggregate-vulkan-latency.pyがINVALID判定をnormal outputより前に行う

P2-1 FIXED:
warmup最後のgenerationとfirst measured generationの不一致をINVALID化

P2-2 FIXED:
event harnessがmelonDS.tomlとvk_layer_settings.txtをbyte-check付きでrestore

NEW P2-3:
標準build wrapperのRelease buildはNinja recompaction permissionで完走しておらず、
PASS evidenceはexisting build directory経由
```

従って最終判定:

```text
Renderer/runtime correctness blocker:
    none newly found

Relative JIT mechanism:
    UNBLOCKED

VUID / Sync fix:
    IMPLEMENTED / RUNTIME EVIDENCE PRESENT

Formal NVIDIA Phase 3 A/B:
    NOT READY YET

Reason:
    manual / measurement capture remains
    manual Phase 1 rows still open

AMD runtime:
    NOT RUN

Latency benefit:
    NOT MEASURED

Default:
    TelemetryOnly
```

---

# 2. Current HEAD

GitHub branch HEAD:

```text
51d3bee0800c8fb484599a6661b7d582f3ec87c4
```

commit:

```text
vulkan: harden present timing sync validation
```

。

このHEADには:

```text
swapchain_generation
Anti-Lag queue-lock placement
Sync Validation harness
queue-capacity guard
complete report accounting
runbook/evidence updates
```

が含まれている。

---

# 3. `swapchain_generation` — 実装確認

`VulkanPresentPacer::StateSnapshot`へ:

```cpp
u64 SwapchainGeneration = 0;
```

が追加されている。

Pacer lifetime側にも:

```cpp
u64 SwapchainGeneration = 0;
```

を保持。

`OnSwapchainCreated()`:

```cpp
++SwapchainGeneration;
ResetTimingLifecycle();
```

。

重要なのはgenerationが:

```text
ResetTimingLifecycle()
```

の対象外であること。

つまりswapchain-local counterがresetされても、capture側は:

```text
generation N
generation N+1
```

として境界を識別できる。

**評価: PASS**

---

# 4. Capture CSV

CSVへ:

```text
swapchain_generation
```

を追加。

capture:

```cpp
snapshot.SwapchainGeneration = SwapchainGeneration;
```

CSV writer:

```cpp
p.SwapchainGeneration
```

を出力。

これで:

```text
wait_timeout_count
timing_queue_full_count
timing_queue_recovery_count
```

がresetされた理由をoffline側で識別できる。

**評価: PASS**

---

# 5. Aggregator generation guard

aggregatorは測定windowに入ってから最初のgenerationを保持し、その後:

```python
generation != self.swapchain_generation
```

なら:

```text
swapchain_generation changed ...
inside the measured window; lifecycle counters reset
```

を`problems`へ追加。

最終的にexit 1。

また:

```text
swapchain_generation column missing
invalid value
non-positive generation
```

もINVALID。さらに、last warm-up rowとfirst measured rowのgenerationが
異なる場合も、wait-timeout baselineの世代不一致としてINVALIDにする。

INVALID判定はsummary CSV / per-mode集計の出力より前に行い、
contradictory runのnormal outputを作らない。synthetic boundary CSVで
exit 1、stdout空、summary不在、per-mode出力不在を確認済み。

目的:

> lifecycle counter resetをrun totalとして誤解しない

に対して正しい。

**評価: PASS / core design**

---

# 6. Queue capacity guard

current `PreparePresent()`では:

```cpp
if (TimingMetadataEnabled
    && TimingQueueAllocated
    && TimingQueueSize != 0
    && OutstandingTimedPresents >= TimingQueueSize)
{
    TimingMetadataEnabled = false;
    TimingQueueRecoveryPending = true;
}
```

。

これにより、results queueが満杯になってから:

```text
vkQueuePresentKHR
→ VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT
→ same image retry
```

する経路を通常のpressure handlingとして使わない。

代わりに:

```text
queue at capacity
→ optional timing metadata pause
→ presentation itself continues
→ results drain
→ queue growth
→ metadata re-enable
```

。

**評価: PASS**

---

# 7. Complete report accounting

`VK_PAST_PRESENTATION_TIMING_ALLOW_PARTIAL_RESULTS_BIT_EXT`を使う場合、partial resultはqueue slotをまだ保持する。

current codeは:

```cpp
u32 completedReportCount = 0;

if (reports[i].reportComplete == VK_TRUE)
    ++completedReportCount;

OutstandingTimedPresents -= completedReportCount;
```

。

さらにqueue recoveryは:

```cpp
TimingQueueRecoveryPending
&& completedReportCount > 0
```

のみ。

Khronos `VK_EXT_present_timing`では、complete reportが返されたentryだけ内部results queue slotを解放し、incomplete reportは後続queryでも再度返る。

従ってcurrent accountingは仕様と一致。

**評価: PASS / SPEC-CONSISTENT**

---

# 8. Queue growth runtime evidence

保存Sync evidenceでは:

```text
queue-at-capacity events = 13
queue growth events      = 4
queue-full errors        = 0
```

。

したがってfinal runtimeは:

```text
pressure branch
drain branch
growth branch
```

を実際に通過している。

**評価: NON-VACUOUS PASS**

---

# 9. AMD Anti-Lag PRESENT位置

current presenter:

```cpp
std::unique_lock<std::mutex> queueLock(Device.GetQueueMutex());

AntiLag.EndFrame(LowLatencyFrameIndex);

Reflex.MarkPresentStart();
LatencyCapture.MarkPresentStart();

vkQueuePresentKHR(...);
```

。

前回は:

```text
AntiLag PRESENT
↓
queue mutex acquisition
↓
QueuePresent
```

だった。

current:

```text
queue mutex acquisition
↓
AntiLag PRESENT
↓
QueuePresent
```

。

`VK_AMD_anti_lag`のPRESENT stageは`before vkQueuePresentKHR`であり、queue contentionをmarkerとpresentationの間に入れない構造になった。

**評価: STATIC PASS**

ただし:

```text
AMD actual GPU runtime = NOT RUN
```

。

---

# 10. Sync Validation harness

event matrixへ:

```powershell
[switch]$ValidateSync
```

を追加し、portable buildでは実行バイナリが読む:

```text
<BuildDir>\portable\melonDS.toml
```

をactual config rootとして解決する。

一時`vk_layer_settings.txt`を生成し:

```text
validate_core = true
validate_sync = true
report_flags includes info
```

。

終了時には既存settingsをrestore、元々無ければ削除する。

さらにstdout/stderr両方をscanし:

```text
VUID-
SYNC-HAZARD
DEVICE_LOST
CURRENT-VALIDATION-ENABLED
Synchronization
```

を検査する。configとlayer settingsは開始前のbytesを保存し、終了時に
restoreまたはremoveした後、byte-for-byteで復元結果も検査する。さらに
runtime logの最終状態について、policy、Reflex requested/actual、requested
VSync、selected present modeをharness自身がself-checkし、mismatchならexit 1。

**評価: IMPLEMENTED / PASS / actual-config self-check付き**

---

# 11. 保存された最終Sync runtime evidence

archive:

```text
docs/archive/audits/vulkan/2026-08-13-event-matrix/
vk-min-sync-final2.out.log
vk-min-sync-final2.err.log
vk-min-sync-configfix3.out.log
vk-min-sync-configfix3.err.log
vk-min-sync-configfix3.harness.log
vk-sync-policy0-short.out.log / .err.log / .harness.log
vk-sync-reflexon-short.out.log / .err.log / .harness.log
```

。

log先頭で:

```text
CURRENT-VALIDATION-ENABLED
Core Checks
Synchronization
Stateless Parameter
Object lifetime
Thread Safety
Handle Wrapping
```

を確認。

archive README:

```text
swapchain rebuilds = 23
VUID = 0
SYNC-HAZARD = 0
DEVICE_LOST = 0
exit = 0
queue-at-capacity = 13
queue growth = 4
queue-full errors = 0
config path = build/debug-mingw-vulkan-validation2/portable/melonDS.toml
policy = JustInTime
Reflex = requested off / actual inactive
VSync = on / FIFO
config restore = PASS
layer restore = PASS
```

。

**評価: RUNTIME PASS — intended Policy2 / ReflexOff / VSyncOn confirmed**

---

# 12. P1-1 — Sync harnessのconfig path / self-check（修正済み）

前回の`min-sync-final2`では、harnessが`$BuildDir\melonDS.toml`へ書く一方、
portable buildは`$BuildDir\portable\melonDS.toml`を読む不一致があった。

今回の修正では、harnessがportable directoryの有無をruntimeと同じ順序で
解決し、実際に読まれるconfig pathへ書き込む。さらにprocess logの最終状態を
policy、Reflex requested/actual、requested VSync、selected present modeで
self-checkし、mismatch時はexit 1にする。

**評価: FIXED / STATIC PASS / RUNTIME PASS**

---

# 13. 旧`min-sync-final2`の指定値とactual runtimeの不一致

final Sync commandはREADME上:

```powershell
-Phase minimize
-ValidateSync
-Tag min-sync-final2
-WarmupSeconds 18
```

。

`NoVSync / ReflexMode / Policy`は明示されていないためscript default:

```text
NoVSync     false
VSync       ON

ReflexMode  0
Reflex      OFF

Policy      2
JustInTime
```

を意図する。

しかしactual log:

```text
requested-vsync=off
selected-present-mode=IMMEDIATE

NVIDIA Reflex mode=on
actual=active

policy=TelemetryOnly
authority=NvidiaReflex

AMD Anti-Lag requested=on
（NVIDIAなのでunsupported）
```

。

| Setting | Harness intent | Actual saved Sync run |
|---|---|---|
| VSync | ON | **OFF / IMMEDIATE** |
| Vulkan policy | `JustInTime` | **TelemetryOnly** |
| Reflex | OFF | **ON** |
| Anti-Lag | OFF | **requested ON / unsupported** |

。

これは明確なtest-harness integrity bugだった。`min-sync-configfix3`では
この不一致を修正し、harness自身のself-checkでPASSを確認した。

---

# 14. この問題が何を無効にするか

無効にはならないもの:

```text
Synchronization Validation自体が有効だった
queue capacity pressureが発生した
queue growthが発生した
VUID 0
SYNC-HAZARD 0
DEVICE_LOST 0
```

。

従って旧final Sync runを完全に無効とする必要はない。これは実際に走った
configurationのdiagnostic evidenceとして保存する。一方、正式なcurrent-tree
gateには`min-sync-configfix3`を採用する。

実際に走ったconfiguration:

```text
TelemetryOnly
Reflex On
VSync Off / IMMEDIATE
```

については有効なdiagnostic evidence。正式gateの実測は:

```text
JustInTime
Reflex Off / inactive
VSync On / FIFO
config restore PASS; layer restore PASS
```

---

# 15. P1-1修正後に証明できたもの

script既定値が意図した:

```text
Policy 2 JustInTime
Reflex Off
VSync On
relative target scheduling
```

のcurrent-tree Sync Validation PASSを、実際のportable config rootとruntime
self-check付きで確認した。

runbookのPass Bも:

```text
policies 0 and 2
policy 3 if supported
Reflex on
...
```

を要求する。

したがって、**current-tree generic Relative JIT pathのSync Validation gateは
閉じた**と判定できる。ただしFormal Phase 3 A/B自体は、manual Phase 1と
randomized latency captureが未実施のため別gateとして閉じたままにする。

。

---

# 16. P1-1実装結果

event harnessはactual config rootを明示的に解決する。

最低:

```powershell
$portableDir = Join-Path $dir 'portable'

if (Test-Path $portableDir) {
    $configPath = Join-Path $portableDir 'melonDS.toml'
} else {
    $configPath = Join-Path $dir 'melonDS.toml'
}
```

。

run開始後logからactual stateを確認し、

例:

```text
expected policy       JustInTime
expected Reflex       off
expected VSync        on
expected presentMode  FIFO family
```

。

actual logが異なれば:

```text
CONFIG MISMATCH
exit 1
```

となる。`min-sync-configfix3`で期待値一致とexit 0を確認した。

---

# 17. P1-1 DoD

```markdown
- [x] configPathが実際のmelonDS config rootを指す
- [x] existing configをbackup
- [x] test configを書込
- [x] process logでpolicyをself-check
- [x] Reflex actual/requestedをself-check
- [x] requested VSync / selected present modeをself-check
- [x] mismatch時 exit 1
- [x] finallyでoriginal config restore（byte-for-byte確認）
- [x] Policy2 / ReflexOff / VSyncOnのSync minimizeを再実行
- [x] Synchronization banner confirmed
- [x] VUID 0
- [x] SYNC-HAZARD 0
- [x] DEVICE_LOST 0
```

---

# 18. P1-2 — INVALID判定とnormal output順序（修正済み）

旧`aggregate-vulkan-latency.py`のmain順序は:

```python
runs = ...
rows = [run.summary_row() ...]

# stdoutへsummary CSV
writer.writerows(rows)

# --out summary.csv
out.writerows(rows)

# per-mode aggregation
...

# 最後に
problems = [...]
if problems:
    print("INVALID ...")
    return 1
```

。

これは修正前の挙動であり、今回:

```python
problems = [...]
if problems:
    print_invalid_diagnostics(...)
    return 1

rows = [...]
write_summary(...)
print_per_mode(...)
```

へ変更した。INVALID runではstdout summary、`--out` summary.csv、per-mode
出力のいずれも作らない。

---

# 19. なぜP1か

今回generation guardの目的は:

```text
swapchain recreationを跨いだA/B結果を
正式measurementとして使わせない
```

こと。

修正後はINVALID判定がnormal outputより前にあるため、INVALID dataを正式結果
として読めるsummary fileを新規生成しない。automationのexit codeだけでなく、
artifactの存在自体もmeasurement integrity boundaryになる。

**評価: FIXED / SYNTHETIC PASS**

。

---

# 20. P1-2実装結果

`problems`判定をoutputより前へ移動した。

```python
runs = ...

problems = [...]
if problems:
    print_invalid_diagnostics(...)
    return 1

rows = [...]
write_summary(...)
print_per_mode(...)
```

。

INVALID時はdiagnosticをstderrへ出すが、normal `summary.csv`は生成しない。

synthetic generation-change CSVで:

```text
exit 1
INVALID message present
summary.csv absent
per-mode winner output absent
```

を`tools/testing/aggregate-vulkan-latency-tests.py`で確認済み。

---

# 21. P2-1 — warmup境界generation edge（修正済み）

current aggregatorはwarmup rowで:

```python
self.wait_timeouts_at_warmup = wait_timeout_count
```

を保持。

しかしwarmup中のgenerationはbaselineとして保存していない。

first measured rowで初めて:

```python
self.swapchain_generation = generation
```

。

Edge case:

```text
last warmup row:
  generation=5
  waitTimeout=3

swapchain recreate

first measured row:
  generation=6
  waitTimeout=1
```

。

修正後はgeneration 6をmeasurement baselineとして受理せず、timeout baselineと
世代が異なるためrunをINVALIDにする。

Formal A/Bでは:

```text
warmup generation != first measured generation
```

ならrun INVALID、またはbaselineを明示rebaseする方が安全。

**評価: FIXED / SYNTHETIC PASS**

---

# 22. P2-2 — test config restore（修正済み）

event harnessは`vk_layer_settings.txt`とactual config rootの
`melonDS.toml`をともにbackup/restoreする。復元後はbytesを比較し、
`min-sync-configfix3`でconfig restore / layer restoreともPASSを確認した。

**評価: FIXED / RUNTIME PASS**

---

# 23. Optional P3 — Sync banner一致判定

現在:

```powershell
$syncBanner = log contains CURRENT-VALIDATION-ENABLED
$syncEnabled = log contains Synchronization
```

を独立検索。

理論上、bannerは存在するが`Synchronization`という文字列が別箇所にある場合でもPASSし得る。

今回のfinal logでは実際にbanner block内へSynchronizationが出ているので今回evidenceには問題なし。

将来はbanner近傍blockとして検査するとより強い。

**P3**

---

# 24. Build evidenceの正確な分類

最終監査文書の記録:

```text
build-mingw.bat --jobs 1
    CONFIGURE OK
    existing Ninja recompaction Permission deniedでblocked

build-mingw-existing.bat --jobs 1
    build/rebuild-mingw-x86_64
    BUILD PASS
    Vulkan timing tests PASS
    XeLL tests PASS

build-mingw-existing.bat --jobs 1
    build/debug-mingw-vulkan-validation2
    BUILD PASS
    MELONDS_VULKAN_ENABLE_VALIDATION=1
    unit tests PASS
```

。

従って表現は:

```text
Release-like existing build       PASS
Debug validation existing build   PASS
canonical build-mingw.bat run     BLOCKED by existing Ninja permission issue
```

が正確。

---

# 25. Static audit / test status

監査文書に記録済み:

```text
python py_compile aggregator            PASS
python py_compile contract audit        PASS
audit-low-latency-contract.py           PASS
aggregate-vulkan-latency-tests.py       PASS
PowerShell parse                        PASS
Sync configfix3 runtime gate            PASS
Vulkan timing tests                     PASS
XeLL tests                              PASS
generation synthetic guard              PASS
```

Python/static/harness/runtimeの上記項目は本再監査で再実行した。C++ buildと
Vulkan timing/XeLL unit testは`51d3bee0`作成時の既存build directory evidenceを
引き続き採用し、今回の差分はtools/docsのみである。canonical wrapperのRelease
treeは既存Ninja recompaction permission issueでblockedのまま:

```text
Release-like existing build       RECORDED PASS
Debug validation existing build   RECORDED PASS
canonical build-mingw.bat run     BLOCKED / Ninja permission
```

と分類する。

---

# 26. Manual Phase 1

依然未完了:

```text
DPI change
Video Settings open/cancel/apply
renderer switching
ROM launch
savestate load
reset
close/reopen
Fast Forward
Slow Motion
```

。

window event subset:

```text
resize
minimize/restore
fullscreen
idle
```

は完了。

---

# 27. Formal NVIDIA A/B gate再判定

現在:

```text
[x] Relative JIT implemented
[x] Relative JIT NVIDIA runtime evidence
[x] FIFO_LATEST_READY reachable
[x] target capture actual-state integrity
[x] generation column implemented
[x] generation-change detection implemented
[x] PRESENT marker boundary fixed
[x] bounded wait actual-state capture
[x] VUID-03268 fix
[x] queue capacity guard
[x] complete report accounting
[x] intended Policy2 / ReflexOff / VSyncOn Sync Validation run clean
[x] harness actual-config self-check and byte restore
[x] INVALID-before-output aggregator behavior
[x] warmup-generation boundary invalidation

[ ] manual Phase 1 rows
[ ] randomized >=3-run Phase 3 latency capture
[ ] AMD GPU runtime validation
```

。

従って:

```text
FORMAL PHASE 3 A/B = NOT READY
```

。

---

# 28. AMD status

```text
Anti-Lag PRESENT queue-lock placement:
    STATIC PASS

AMD GPU runtime:
    NOT RUN
```

。

NVIDIA formal A/Bのblockerではない。

---

# 29. Latency status

まだ:

```text
A0/A1/A2/A3/B1/B2 randomized >=3 runs:
    NOT RUN

click-to-photon:
    NOT RUN

Relative latency benefit:
    NOT MEASURED
```

。

---

# 30. GitHub Actions

HEAD `51d3bee0...`:

```text
combined statuses:
    none

associated workflow runs:
    none
```

。

従って:

```text
GitHub Actions = NOT VERIFIED
```

。

---

# 31. 推奨修正順

```text
1-6. **DONE** — harness config/self-check/restore, intended Sync rerun,
     INVALID-before-output, and warmup-generation hardening
7. manual Phase 1
8. Formal NVIDIA Phase 3 A/B
9. AMD runtime validation
10. click-to-photon / other vendors/platforms
```

---

# 32. 今回のfollow-up実装

Harness（実装済み）:

```text
Vulkan: make the event matrix verify its applied config
```

Aggregator（実装済み）:

```text
Vulkan: reject invalid latency runs before writing summaries
```

generation edge（実装済み）:

```text
Vulkan: harden latency capture generation boundaries
```

。

---

# 33. Harness修正後の必須runtime

最低:

```powershell
-Phase minimize
-ValidateSync
-Policy 2
-ReflexMode 0
```

VSync on。

ログself-check:

```text
policy=JustInTime
Reflex requested=off actual=inactive
requested-vsync=on
FIFO-family present mode
Synchronization banner present
config restore=PASS
layer restore=PASS
```

。

結果:

```text
VUID 0
SYNC-HAZARD 0
DEVICE_LOST 0
exit 0
```

`min-sync-configfix3`で上記を実行済み（23 rebuilds、capacity 13、growth 4）。

追加のshort Sync matrixとして、以下も実行済み:

```text
Policy0 / ReflexOff
Policy2 / ReflexOff
ReflexOn
```

Policy0 / ReflexOff / VSyncOn、Policy2 / ReflexOn / VSyncOnのidle controlは
各2 rebuilds、banner確認、VUID/SYNC-HAZARD/DEVICE_LOST 0、config/layer
restore PASSだった。raw evidenceはarchiveの`vk-sync-policy0-short.*`と
`vk-sync-reflexon-short.*`。

---

# 34. 最終status table

| Item | Status |
|---|---|
| Relative-Time Scheduling | **IMPLEMENTED / PASS** |
| NVIDIA functional runtime | **PASS / previous evidence** |
| Present marker boundary | **PASS** |
| VUID-03268 | **FIXED / PASS** |
| Queue capacity guard | **PASS** |
| Complete report accounting | **PASS / spec-consistent** |
| Swapchain generation capture | **PASS** |
| Generation-change detection | **PASS** |
| AMD Anti-Lag queue-lock placement | **STATIC PASS** |
| AMD runtime | **NOT RUN** |
| Sync Validation | **PASS / intended Policy2 + ReflexOff + VSyncOn** |
| Intended Policy2 Sync gate | **PASS / config self-check confirmed** |
| Sync harness config integrity | **FIXED / RUNTIME PASS** |
| Invalid-run output safety | **FIXED / SYNTHETIC PASS** |
| Warmup-generation boundary | **FIXED / SYNTHETIC PASS** |
| Test config restoration | **FIXED / byte-check PASS** |
| Automated window Phase 1 | **PASS** |
| Manual Phase 1 | **NOT RUN / PARTIAL** |
| Formal Phase 3 A/B | **NOT RUN / NOT READY** |
| Latency benefit | **NOT MEASURED** |
| Default pacing | **TelemetryOnly** |
| GitHub Actions | **NOT VERIFIED** |

---

# 35. 公式仕様確認

## `VK_EXT_present_timing`

Khronos documentation:

```text
https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html
https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingEXT.html
```

確認事項:

```text
complete resultだけresults queue slotをrelease
incomplete resultはcompleteになるまで後続queryで返る
```

。

current `completedReportCount`設計と一致。

## `VK_AMD_anti_lag`

```text
https://docs.vulkan.org/refpages/latest/refpages/source/VkAntiLagStageAMD.html
https://docs.vulkan.org/features/latest/features/proposals/VK_AMD_anti_lag.html
```

PRESENT:

```text
before vkQueuePresentKHR
```

。

current queue-lock内配置はstaticに妥当。

---

# 36. 最終監査総括

今回のfollow-upは、前回指摘した実装問題をかなり適切に解消している。

特に:

```text
swapchain_generation
queue-at-capacity proactive pause
complete-report slot accounting
Anti-Lag marker placement
Sync Validation harness
stdout/stderr scan
```

はいずれも方向として正しい。

また保存されたfinal Sync logは:

```text
Synchronization enabled
VUID 0
SYNC-HAZARD 0
DEVICE_LOST 0
pressure/recovery path exercised
```

という実runtime evidenceを持つ。

そのため:

> **Sync fixそのものは成功している**

と評価してよい。

前回見つかったconfig path不一致は、portable root解決とruntime self-checkで修正した。
`min-sync-configfix3`は意図した:

```text
JustInTime / ReflexOff / VSyncOn / FIFO
```

で実行され、config/layer restoreもPASSした。generation boundaryを含むINVALID
runはnormal summaryを出力しないこともsynthetic testで確認済み。

最終判定:

```text
Core implementation quality:
    STRONG

Previous P1 fixes:
    IMPLEMENTED

Current-tree Sync evidence:
    PASS / intended Policy2 + ReflexOff + VSyncOn

New Formal-A/B blockers:
    manual Phase 1 rows
    randomized >=3-run latency capture

Formal Phase 3:
    NOT READY / GATE CLOSED

Latency benefit:
    NOT MEASURED

Default:
    TelemetryOnly
```

P1/P2のmeasurement integrity修正と意図設定Sync gateは完了した。Formal A/Bへ
進むには、残るmanual Phase 1、randomized >=3-run latency capture、click-to-
photon/AMD runtimeの別gateを実施する。
