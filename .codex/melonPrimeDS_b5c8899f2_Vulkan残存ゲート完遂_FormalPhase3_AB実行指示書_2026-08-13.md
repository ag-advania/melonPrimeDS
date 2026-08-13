# melonPrimeDS Vulkan 残存ゲート完遂・Formal Phase 3 A/B 実行指示書

- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver2`
- 指示書基準HEAD: `b5c8899f275f8ffdf73779f5d4c8902261fa02c1`
- Parent implementation commit: `c6a4fe0c80fb38dd8a5faaee032a44187a48b3c4`
- 作成日: 2026-08-13
- Backend: **現行Vulkan clean-room backend**
- 目的:
  1. 残るmanual Phase 1を完了する
  2. Release capture buildを固定する
  3. NVIDIA Formal Phase 3 A/Bを正式実行する
  4. 結果をINVALIDなく集計する
  5. AMD / Intel / Linux / MoltenVKをshipping-default判定の別gateとして残す

---

# 1. 今回の再監査結果

`b5c8899f2` / `c6a4fe0c8` をGitHub上で再監査した。

前回残していた:

```text
P2-A  queue-pressureとquery failureのfallback診断混同
P2-B  INVALID run時の古いsummary.csv残存
P3    VUID履歴の文言
```

はすべて修正済み。

判定:

```text
Previous P2-A                    FIXED / PASS
Previous P2-B                    FIXED / PASS
Previous P3                      FIXED / PASS
Current intended Sync gate       PASS
Relative JIT NVIDIA runtime      PASS
Measurement-integrity tooling    PASS
New blocking P0/P1               NONE FOUND
New P3 documentation mismatch    CLOSED / NON-BLOCKING
Manual Phase 1                   PASS except DPI NOT RUN
Formal NVIDIA Phase 3 A/B        COMPLETE
AMD actual runtime               NOT RUN
Latency benefit                  NO WINNER / threshold not met
Default pacing                   TelemetryOnly
GitHub Actions                   PASS at audited implementation SHA
```

したがって、今後の主作業は**コード修正ではなく、残存validationと正式measurement**である。

---

# 2. 今回確認済みの修正

## 2.1 Timing queue pressure reason

current policyは:

```cpp
if (!caps.TimingMetadataEnabled)
{
    if (caps.TimingQueuePressure)
        return VulkanJitFallbackReason::TimingQueuePressure;

    return caps.PresentTimingSurface
        ? VulkanJitFallbackReason::TimingQueryFailed
        : VulkanJitFallbackReason::PresentTimingUnsupported;
}
```

となっている。

従って:

```text
capacity pressure
```

と:

```text
actual timing query failure
```

を別診断として扱う。

**PASS**

---

## 2.2 runtime evidence

保存runではcapacity pause時に:

```text
fallback=timing queue pressure
```

を確認し、その後:

```text
queue grown
timing metadata re-enabled
fallback=none
```

へ復帰している。

これは単なるenum追加ではなくruntimeで到達した証拠を持つ。

**PASS**

---

## 2.3 stale summary removal

aggregatorは`--out`が指定された場合、集計開始時に:

```python
out_path.unlink(missing_ok=True)
```

を実行。

その後INVALID判定を行い、問題があればnormal summaryを出さずexit 1。

従って:

```text
前回runのsummary.csv
```

が今回のINVALID run後に残る問題を閉じた。

**PASS**

---

## 2.4 aggregator regression

regression testは事前に:

```text
stale summary
```

を作った状態でINVALID captureを実行し、

```text
exit 1
stdout empty
summary不存在
per-mode outputなし
warm-up boundary diagnosticあり
```

を要求。

valid captureは:

```text
exit 0
stdout summaryあり
summary.csvあり
```

。

**PASS / TEST COVERAGE PRESENT**

---

## 2.5 P3 VUID wording

runbookは現在:

```text
initial validation session:
    timeDomainId-12400

later minimize/restore matrix:
    pWaitSemaphores-03268
```

と分離している。

旧:

```text
One VUID was found...
```

という誤解を招く表現は修正済み。

**PASS**

---

# 3. 最新current-tree Sync gate

保存済みfinal targeted Sync gate:

```text
Policy          JustInTime
Reflex          requested off / actual inactive
VSync           on
PresentMode     FIFO
config path     portable\melonDS.toml
config restore  PASS
layer restore   PASS
rebuilds        23
VUID            0
SYNC-HAZARD     0
DEVICE_LOST     0
capacity        13
growth          4
queue-full err  0
exit            0
```

さらにruntime側で:

```text
authority=GenericPresentTiming
targetMode=relative
TargetSchedulingActive
```

まで確認済み。

**CURRENT NVIDIA SYNC GATE = PASS**

---

# 4. 新規P3 — VSync OFF説明の旧文言を整理する

## 4.1 問題

current runbook Status節には、VSync OFFについて:

```text
absolute timing unsupported by surface
```

が:

```text
present mode is not FIFO
```

より先に表示される、という趣旨の旧説明が残っている。

しかしcurrent policyは:

```text
Absolute unsupported
↓
Relative supportedならRelativeを選択
↓
FIFO familyか確認
↓
non-FIFOならNonFifoPresentMode
```

。

現在のNVIDIA surfaceはrelative timingを実際にsupportしている。

従って、その旧説明は**relative fallback実装前の状態を反映している可能性が高い**。

---

# 5. P3修正指示

runtime codeは変更しない。

runbookの旧説明を、current behaviorに合わせて修正する。

推奨文意:

```text
On this NVIDIA surface absolute timing is unavailable but relative timing is
supported. With VSync off, target scheduling should therefore progress through
the relative-capability path and then be disabled because the selected present
mode is outside the FIFO family.

The C0 runtime control is the source of truth. If a future driver reports a
different fallback reason, record the exact capability state and investigate
rather than changing the documentation to match an unexplained result.
```

日本語文書なら同義でよい。

### DoD

```markdown
- [x] absolute非対応だけでtarget schedulerが失敗する説明を削除
- [x] relative fallbackを明記
- [x] VSync OFF / non-FIFO controlの期待を明記
- [x] current runtime evidenceと矛盾しない
- [x] runtime codeを変更しない
```

Classification:

```text
P3 / DOC ONLY / NON-BLOCKING
```

Formal A/Bを止めない。

---

# 6. これ以降の変更原則

Formal A/Bに入る前に、新しいVulkan pacing codeを不用意に変更しない。

以下を守る。

```text
・現行Vulkan clean-room backendの設計を維持
・旧Sapphire / WatermelonDS Vulkan実装を参照して実装を移植しない
・Reflex / Anti-Lag 2 active時はgeneric pacingを重ねない
・host FPS limiterを維持
・optional Vulkan featureはfail-soft
・defaultはTelemetryOnly
・latency superiorityを測定前に主張しない
```

manual validationで新規runtime defectが見つかった場合のみ、そこで停止し根因修正へ戻る。

---

# 7. Stage 0 — Preflight

開始前:

```bat
git fetch origin
git checkout develop_remakeVulkan_ver2
git rev-parse HEAD
git rev-parse origin/develop_remakeVulkan_ver2
git status --short
```

期待:

```text
HEAD:
b5c8899f275f8ffdf73779f5d4c8902261fa02c1

origin/develop_remakeVulkan_ver2:
b5c8899f275f8ffdf73779f5d4c8902261fa02c1
```

ただしP3 doc fixを先にcommitした場合は、その新しいSHAを以後の全evidenceへ記録する。

### STOP条件

```text
dirty worktree
local/remote SHA mismatch
意図しないcommit混入
build treeが別commit
ROM条件不明
```

の場合、measurementへ進まない。

---

# 8. Stage 1 — Manual Phase 1を完了する

Automated subsetは既に完了:

```text
[x] resize x40
[x] minimize/restore x20
[x] fullscreen x8
[x] idle
```

残件:

```text
[ ] DPI change — NOT RUN; one physical monitor was exposed
[x] Video Settings open/cancel/apply — cancel/same-value x20 and changed-VSync Apply PASS
[x] renderer switching — Software/OpenGL/OpenGL Compute/DX12 x20 PASS
[x] ROM lifecycle — save/load/undo/reset and second-session reopen PASS
[x] Fast Forward
[x] Slow Motion
```

---

# 9. Manual Phase 1 共通条件

Debug validation buildを使う。

Latency measurement buildを使わない。

確認:

```text
[Vulkan] validation layer enabled
```

。

最低限保存:

```text
commit SHA
build path
GPU
driver
Vulkan loader
Validation Layer version
Windows version
monitor / DPI
VSync
renderer
ROM
実行日時
stdout
stderr
```

判定対象:

```text
VUID
SYNC-HAZARD
DEVICE_LOST
software fallback
hang
crash
recreate storm
renderer mismatch
```

---

# 10. Manual 1-A — DPI change

目的:

```text
swapchain / surface / window coordinate lifecycle
```

をDPI変化で検証。

推奨:

```text
100% → 125%
125% → 150%
150% → 100%
```

または複数monitorを使える場合:

```text
異なるDPI monitor間へwindow移動
```

。

確認:

```markdown
- [ ] Vulkanがactual rendererのまま — NOT RUN; no genuine cross-DPI transition
- [ ] swapchain再生成後に表示正常 — NOT RUN; no genuine cross-DPI transition
- [ ] VUID 0 — NOT RUN; no genuine cross-DPI transition
- [ ] DEVICE_LOST 0 — NOT RUN; no genuine cross-DPI transition
- [ ] resize stormなし — NOT RUN; no genuine cross-DPI transition
- [ ] mouse/cursor regressionなし — NOT RUN; no genuine cross-DPI transition
- [ ] Custom HUD位置破綻なし — NOT RUN; no genuine cross-DPI transition
```

---

# 11. Manual 1-B — Video Settings

操作:

```text
Video Settings open
↓
Cancel
↓
open
↓
Apply
```

を複数回。

runbookの要求を優先して:

```text
x20
```

を目安にする。

確認:

```text
設定を変えないCancel
同値Apply
Vulkan設定変更Apply
```

。

**Cancelでrendererを再生成しないことも観察する。**

---

# 12. Manual 1-C — renderer switching

対象:

```text
Vulkan ↔ Software
Vulkan ↔ OpenGL Compute
Vulkan ↔ DX12（buildで利用可能なら）
```

。

runbook目安:

```text
各 x20
```

。

最低1系列はfullscreen状態でも実施。

確認:

```markdown
- [x] requested rendererとactual renderer一致
- [x] silent software fallbackなし
- [x] Vulkan再初期化成功
- [x] old swapchain/device resource残留なし
- [x] VUID 0
- [x] DEVICE_LOST 0
- [x] cursor/focus正常
- [x] ROM進行継続
```

Evidence: `docs/archive/audits/vulkan/2026-08-13-formal-ab/manual-phase1/`
contains the four x20 switch logs, each with actual-renderer, swapchain,
validation, and Sync Validation results.

---

# 13. Manual 1-D — ROM lifecycle

対象:

```text
ROM launch
savestate load
reset
close
reopen
```

。

同一ROMで複数cycle。

重要:

```text
renderer / pacer / swapchain generation
```

が旧ROM/旧sessionから残っていないこと。

確認:

```markdown
- [x] reopen後もVulkan actual
- [x] JIT bootstrap正常
- [x] stale present IDなし
- [x] stale target baselineなし
- [x] stale swapchain generationなし
- [x] VUID 0
- [x] DEVICE_LOST 0
```

---

# 14. Manual 1-E — Fast Forward / Slow Motion

対象:

```text
Fast Forward Hold
Fast Forward Toggle
Slow Motion
```

。

normal speed以外ではgeneric pacingがcadenceを固定してはいけない。

期待:

```text
normalSpeed=false
↓
boundedWait=off
targetScheduling=off
fallback=not normal speed
```

。

normal speedへ戻したら:

```text
GenericPresentTiming
relative target scheduling
```

へ正常復帰すること。

確認:

```markdown
- [x] FF中 generic wait OFF
- [x] FF中 target scheduling OFF
- [x] Slow Motion中 generic wait OFF
- [x] Slow Motion中 target scheduling OFF
- [x] normal復帰後target path再開
- [x] frame limiter挙動正常
- [x] VUID 0
```

---

# 15. Stage 1B — Synchronization Validation補完

既存targeted minimize gateはPASS。

ただしrunbook Pass Bはmanual interactionsも要求するため、manual Phase 1で特に危険な:

```text
renderer switch
fullscreen
```

について短いSync Validation follow-upを実施する。

必須確認:

```text
CURRENT-VALIDATION-ENABLED
Synchronization
```

。

結果:

```text
VUID 0
SYNC-HAZARD 0
DEVICE_LOST 0
```

。

### 注意

Validation buildのframe timeは**一切A/B結果へ使わない**。

---

# 16. Phase 1 Gate

以下がすべて満たされるまでPhase 3へ進まない。

```text
core validation related ERROR      0
sync hazard                        0
DEVICE_LOST                        0
software fallback                  none
hang/crash                         none
recreate storm                     none
requested/actual renderer mismatch none
```

第三者layer warningは分類して記録する。

---

# 17. Stage 2 — Formal A/B Release capture build

Validation buildを閉じる。

runbook指定:

```bash
cmake -S . -B build/release-mingw-x86_64 ^
  -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF ^
  -DMELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE=ON
```

環境に応じてWindows cmdでは1行でもよい。

build:

```bash
cmake --build --preset=release-mingw-x86_64 --parallel 1
```

### 必須

```text
Release
developer features OFF
latency capture ON
Validation Layer OFF
Sync Validation OFF
RenderDoc OFF
Nsight capture OFF
```

。

---

# 18. Release build provenance

A/B開始前に記録:

```text
commit SHA
executable path
executable SHA-256
build timestamp
compiler
CMake
Ninja
Qt
Windows
GPU
driver
Vulkan loader
monitor
refresh rate
VRR/G-SYNC
VSync
window mode
internal scale
TargetFPS
ROM
savestate
power plan
NVIDIA Control Panel overrides
overlays
```

同一A/B group中に変更しない。

---

# 19. Stage 2-A — NVIDIA functional runtime

正式measurement前のsmoke。

## A2 candidate

```text
Reflex Off
Policy JustInTime
VSync On
```

。

確認:

```text
authority=GenericPresentTiming
targetMode=relative
targetScheduling active after bootstrap
target value non-zero
present mode FIFO family
queue-full stormなし
DEVICE_LOSTなし
```

。

このNVIDIA surfaceでは:

```text
absolute surface support = no
relative surface support = yes
```

なのでrelativeは正常なsupported fallback。

---

# 20. Stage 2-B — Reflex ownership

Reflex On:

```text
authority=NvidiaReflex
boundedWait=off
targetScheduling=off
```

。

Reflex On+Boostも同様にgeneric schedulerを切る。

確認:

```text
VK_NV_low_latency2 enabled
actual=active
present IDs / latency timing reports non-zero
```

。

---

# 21. Stage 3 — Formal Phase 3 A/B mode

| ID | Reflex | Policy | VSync | 備考 |
|---|---|---|---|---|
| A0 | Off | TelemetryOnly | On | baseline |
| A1 | Off | PresentWait | On | generic wait |
| A2 | Off | JustInTime | On | primary JIT |
| A3 | Off | JustInTimeFifoLatestReady | On | supported時のみ |
| B1 | On | JustInTime | On | Reflex owns pacing |
| B2 | On+Boost | JustInTime | On | Reflex Boost |
| C0 | Off | JustInTime | Off | contract control |

Primary:

```text
A0 vs A2 vs B1
```

。

C0はwinner候補ではない。

---

# 22. Config mapping

現在の設定key:

```text
3D.Vulkan.PresentPacingPolicy

0 = TelemetryOnly
1 = PresentWait
2 = JustInTime
3 = JustInTimeFifoLatestReady
```

Reflex:

```text
3D.DX12.NvidiaReflexMode

0 = Off
1 = On
2 = On+Boost
```

VSync:

```text
Screen.VSync = true / false
```

。

設定後は必ずruntime logでeffective stateを確認。

---

# 23. C0 VSync OFF control

期待:

```text
requested-vsync=off
selected-present-mode=IMMEDIATE
```

またはdriverが選ぶnon-FIFO mode。

current code contract上、relative scheduling capabilityが利用可能でも:

```text
NonFifoPresentMode
```

によりtarget schedulingはOFFになる。

期待:

```text
targetScheduling=off
fallback=present mode is not FIFO
```

。

もし別reasonなら:

```text
actual capabilities
target mode
present mode
TimingMetadataEnabled
```

を保存し、原因を特定する。

文書へ結果を合わせるだけの修正は禁止。

---

# 24. Run count

各mode:

```text
最低3 runs
```

。

各run:

```text
600 warm-up frames
+
10,000 measured frames以上
```

。

同一scene。

---

# 25. Randomization

禁止:

```text
A0 A0 A0
A1 A1 A1
A2 A2 A2
...
```

。

thermal/session driftをmodeへ偏らせない。

例として7 mode全部を3回ずつ使う場合:

```text
A3
C0
B2
A2
C0
A3
A1
B2
B1
A0
A2
C0
A0
A1
A2
A1
B1
A3
B2
A0
B1
```

。

これは**例**。

A3がUNSUPPORTEDならA3を除外し、残りmodeでrandomized orderを再作成。

---

# 26. A3 unsupported

以下の場合:

```text
extension unavailable
surface capability unavailable
FIFO_LATEST_READY unavailable
```

なら:

```text
UNSUPPORTED
```

。

FAILではない。

A3をA2へ代入して同じ結果として扱わない。

---

# 27. per-run environment

例:

```bat
set MELONPRIME_LATENCY_RUN_ID=20260813_NV_A2_R1
set MELONPRIME_LATENCY_CSV=runs\20260813_NV_A2_R1.csv
```

。

必ずunique filename。

---

# 28. 各run保存物

最低:

```text
CSV
stdout log
stderr log
metadata
```

。

metadata例:

```text
run_id
mode
commit SHA
exe SHA256
start/end time
GPU
driver
monitor
refresh
VRR
power plan
temperature
clock
VSync
present mode
policy
Reflex
scene
savestate
```

。

---

# 29. Environment discipline

A/B group中:

```text
Windows Updateなし
browser videoなし
OBS encodingなし
virus scanなし
downloadなし
shader compileなし
background buildなし
```

。

overlayを使うなら全run同条件。

---

# 30. Aggregator

実行:

```bash
python tools/perf/aggregate-vulkan-latency.py ^
  --warmup 600 ^
  --out summary.csv ^
  runs/
```

。

### 最重要

```text
process exit code = 0
```

を必ず確認。

---

# 31. INVALID

以下は測定結果ではない:

```text
swapchain generation changed
warmup generation mismatch
target column contradiction
relative cadence invariant violation
missing required capture column
invalid capture state
```

。

aggregatorが:

```text
INVALID
exit 1
```

したrunは破棄して再採取。

「遅かったrun」として平均へ入れない。

---

# 32. stale summary regression

current toolはrun開始時にold `--out`を削除する。

Formal A/B実行時にも:

```text
INVALID終了後にsummary.csvが存在しない
```

ことを1回spot check。

もし残るならmeasurement停止。

---

# 33. target active gate

A2/A3:

```text
target_active_ratio >= 95%
```

をsteady measured windowで要求。

bootstrap/warmupは除外。

95%未満:

```text
A2として比較禁止
```

。

fallback reasonsを分析して再採取。

---

# 34. B1/B2 vendor gate

Reflex active時:

```text
authority=NvidiaReflex
generic target scheduling off
bounded wait off
```

が期待。

target active ratioが高い場合:

```text
INVALID CONFIG / implementation regression
```

として止める。

---

# 35. wait gate

`PresentWait` / JIT bounded wait:

```text
wait_timeout_rate < 1%
```

。

rate:

```text
timeouts / actual attempted waits
```

。

frame数を分母にしない。

---

# 36. queue gate

目標:

```text
timing_queue_full_count      0 ideally
timing_queue_recovery_count  0 ideally
```

。

Formal steady-stateでpressure/recoveryが頻発するなら、
latency resultをそのままwinner判定へ使わず原因分析。

---

# 37. generation gate

measured window:

```text
swapchain_generation changes = 0
```

。

1回でも変化:

```text
INVALID
```

。

window resize、fullscreen、settings変更はmeasurement中に行わない。

---

# 38. relative cadence invariants

relative row:

```text
target_value_ns
==
relative_quanta
*
target_generation_refresh_interval_ns
```

。

また:

```text
relative_accumulator_after_ns
<
target_generation_refresh_interval_ns
```

。

aggregatorが検査。

---

# 39. Frame-time metrics

最低:

```text
P50
P95
P99
P99.9
```

。

mode比較は:

```text
per-run percentile
↓
modeごとにrun間比較
```

。

全frameをmode単位でpoolしない。

---

# 40. Host pipeline proxy

CSVの:

```text
input sample
→ QueuePresent return
```

はhost-side proxy。

次の表現は禁止:

```text
click-to-photon
system latency
pixel latency
```

。

---

# 41. winner rule

runbook準拠で、primary comparison:

```text
A0 vs A2 vs B1
```

。

winner候補は少なくとも:

```text
median pipeline P50 >= 2% improvement vs A0
median pipeline P95 >= 2% improvement vs A0
>=3 independent runsで同方向
P99 frame timeの重大悪化なし
```

。

単発runだけでwinnerにしない。

---

# 42. Frame-time regression

目安:

```text
>1% regression
```

がある場合、latency benefitだけで自動採用しない。

trade-offを記録。

---

# 43. Boost

B2はclock/power behaviorも含む。

On+Boost比較でGPU clockを固定するとBoost効果自体を消す可能性がある。

従って:

```text
clock
power
temperature
```

を記録し、無理に固定しない。

---

# 44. Formal result classifications

各mode:

```text
PASS
UNSUPPORTED
INVALID
BLOCKED
NOT RUN
```

。

使用例:

```text
A3 = UNSUPPORTED
AMD = NOT RUN
run 20260813_A2_R2 = INVALID
```

。

---

# 45. NVIDIA Formal A/B completion condition

```markdown
- [x] manual Phase 1 PASS except DPI NOT RUN
- [x] release capture build fixed
- [x] same SHA
- [x] same scene
- [x] same display conditions
- [x] >=3 runs/mode
- [x] randomized order
- [x] 600 warmup
- [x] >=10,000 measured
- [x] aggregator exit 0
- [x] no measured-window generation changes
- [x] A2/A3 target active >=95%
- [x] wait timeout <1%
- [x] queue pressure acceptable
- [x] no pooled-frame analysis
- [x] winner criteria evaluated
```

。

これを満たして初めて:

```text
FORMAL NVIDIA PHASE 3 = COMPLETE
```

。

---

# 46. AMD runtimeの位置付け

重要:

> **AMD実機runtimeはNVIDIA Formal Phase 3を実行するためのblockerではない。**

ただし:

```text
shipping default変更
cross-vendor recommendation
「Vulkan JITが一般的に最良」
```

を主張するには別gateとして必要。

---

# 47. Stage 4 — AMD Anti-Lag runtime

AMD実機で確認:

```text
VK_AMD_anti_lag exposed
Anti-Lag requested
actual active
authority=AmdAntiLag2
generic wait off
generic target scheduling off
```

。

present ordering:

```text
queue lock
↓
AntiLag PRESENT
↓
QueuePresent
```

。

確認:

```text
VUID 0
SYNC-HAZARD 0
DEVICE_LOST 0
```

。

---

# 48. AMD unsupported

GPU/driverがextensionを出さない場合:

```text
UNSUPPORTED
```

。

FAILではない。

---

# 49. Stage 5 — shipping-default gate

`TelemetryOnly`からdefaultを変える前に最低:

```text
NVIDIA
AMD
Intel Vulkan
Windows
Linux
macOS / MoltenVK
```

のfunctional結果を確認。

利用可能platformのみでも:

```text
UNSUPPORTED
NOT RUN
```

を明確に分離。

---

# 50. Default変更禁止条件

以下のいずれかなら:

```text
TelemetryOnlyを維持
```

。

```text
Formal NVIDIA A/B未完了
AMD未確認
cross-vendor regression
platform regression
latency superiority不明
frame-time regression
click-to-photon未裏付け
```

。

---

# 51. click-to-photon

本当にsystem latencyを主張する場合:

```text
Reflex Analyzer
high-speed camera
photodiode
```

等の外部計測を使う。

host CSVだけでは不可。

---

# 52. Stop conditions

次のどれかで即停止:

```text
VUID
SYNC-HAZARD
DEVICE_LOST
hang
crash
silent fallback
software fallback
requested/actual renderer mismatch
swapchain recreate storm
aggregator INVALID
generation change
config mismatch
thermal runaway
background load contamination
wrong SHA
dirty worktree
```

。

先へ進めず、原因修正→validationへ戻る。

---

# 53. Evidence directory推奨

例:

```text
docs/archive/audits/vulkan/2026-08-13-formal-ab/
```

。

構成:

```text
README.md
environment.txt
run-order.txt
runs/
summary.csv
manual-phase1/
sync-followup/
```

。

---

# 54. Manual evidence naming

例:

```text
manual-dpi-125.out.log
manual-dpi-150.out.log
manual-video-settings.out.log
manual-renderer-switch.out.log
manual-rom-lifecycle.out.log
manual-speed-modes.out.log
```

。

---

# 55. Formal run naming

```text
20260813_NV_A0_R1.csv
20260813_NV_A0_R2.csv
20260813_NV_A0_R3.csv

20260813_NV_A2_R1.csv
...
```

。

stdout/stderrも同じstem。

---

# 56. READMEへ残すこと

```text
tested SHA
build command
exe SHA256
environment
run order
invalid/retried runs
unsupported modes
manual matrix results
aggregate command
aggregate exit code
winner criteria
final conclusion
```

。

---

# 57. Formal result table

最終的に:

| Mode | Runs | Valid | Target active | Wait timeout | P50 | P95 | P99 | 判定 |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| A0 | | | N/A | | | | | |
| A1 | | | N/A | | | | | |
| A2 | | | | | | | | |
| A3 | | | | | | | | |
| B1 | | | N/A | N/A | | | | |
| B2 | | | N/A | N/A | | | | |
| C0 | | | 0 | | | | | control |

を作る。

---

# 58. 結論の書き方

許可:

```text
A2 lowered the measured host pipeline proxy P50/P95 by X/Y% relative to A0
under this fixed NVIDIA test configuration.
```

。

禁止:

```text
A2 has X ms click-to-photon latency
Vulkan JIT is always lower latency
this is the best setting for all GPUs
```

。

---

# 59. Formal A/B後の次判断

### Case A — A2明確勝利

```text
NVIDIA result:
    candidate

Default:
    still TelemetryOnly
```

。

AMD/Intel/platform gateへ進む。

### Case B — B1勝利

Reflex NVIDIA向けrecommendation候補。

generic defaultとは別。

### Case C —差なし

default変更不要。

### Case D —frame-time悪化

latency benefitとtrade-offを記録し、default変更しない。

---

# 60. 変更を加える場合のルール

Formal measurement中にcodeを変更したら:

> **その前のrunと後のrunを同じA/B groupへ混ぜない。**

新SHAで全modeを最初から採取。

---

# 61. Build/test status表現

今回の既存evidenceは:

```text
RECORDED PASS / SOURCE VERIFIED
```

と:

```text
COMMITTED RUNTIME EVIDENCE PASS
```

を分ける。

この作業指示実行者自身が再実行した場合のみ:

```text
PASS
```

と記録。

---

# 62. GitHub Actions

初期監査時のcurrent HEADについてGitHub connector上:

```text
combined status: none
workflow run: none
```

。

従って:

```text
初期スナップショットではGitHub Actions = NOT VERIFIED
```

。

実行結果（監査対象実装 SHA `4a503debf15abd4120e1bf4e19629f396800bf33`）:

```text
Ubuntu 31707409936 = PASS
macOS 31707412897 = PASS
Windows 31707415459 = PASS
```

各結果は `docs/archive/audits/vulkan/2026-08-13-formal-ab/ci/README.md` に
記録し、Linux llvmpipe smoke / macOS MoltenVK bundle verification と
実機ランタイム未実行を分離する。local build結果だけでActions PASSとは書かない。

---

# 63. P3 doc cleanup後の推奨commit

```text
docs: correct Vulkan VSync-off fallback note
```

。

manual validation後:

```text
test: record Vulkan manual lifecycle validation
```

。

Formal A/B後:

```text
perf: record Vulkan formal presentation A/B
```

。

---

# 64. Runtime codeを変更しない対象

次は現状維持:

```text
relative cadence
target scheduling mode selection
queue capacity guard
complete report accounting
semaphore retry fallback
present marker boundary
bounded wait implementation
Reflex ownership
Anti-Lag ownership
host limiter
swapchain generation capture
aggregator validity contract
default TelemetryOnly
```

。

---

# 65. Definition of Done — 今回の次工程

## Documentation

```markdown
- [x] VSync-off stale note corrected
- [x] current relative fallback semantics documented
```

## Manual Phase 1

```markdown
- [ ] DPI — NOT RUN; one physical monitor is exposed
- [x] Video Settings
- [x] renderer switching
- [x] ROM lifecycle
- [x] Fast Forward
- [x] Slow Motion
- [x] core validation clean
- [x] targeted Sync follow-up clean
```

## Formal NVIDIA

```markdown
- [x] release capture build
- [x] developer features OFF
- [x] validation OFF
- [x] fixed environment
- [x] >=3 randomized runs/mode
- [x] 600 warmup
- [x] >=10k measured
- [x] all valid run CSVs
- [x] aggregator exit 0
- [x] thresholds checked
- [x] conclusion written without overclaim
```

## AMD / generalization

```markdown
- [ ] AMD runtime — NOT RUN; host exposes NVIDIA only
- [ ] Intel Vulkan — NOT RUN; no Intel Vulkan device
- [ ] Linux hardware/vendor runtime — NOT RUN; hosted CI build + llvmpipe/Xvfb
      validation smoke PASS, but no AMD/Intel Linux device was exercised
- [ ] MoltenVK/macOS runtime — NOT RUN; hosted CI bundle verification PASS,
      but no macOS runtime host was exercised
```

。

---

# 66. 最終完了判定

現時点:

```text
CODE / SYNC / MEASUREMENT INFRA:
    PASS

PREVIOUS P2-A/B/P3:
    CLOSED

NEW RUNTIME P0/P1:
    NONE FOUND

NEW DOC P3:
    ONE NON-BLOCKING CLEANUP

MANUAL PHASE 1:
    PASS except DPI NOT RUN

FORMAL NVIDIA A/B:
    COMPLETE

AMD RUNTIME:
    NOT RUN — NVIDIA-only host

LATENCY BENEFIT:
    NO WINNER — A2 P50 improved, but P95 threshold was missed

DEFAULT:
    TelemetryOnly

OVERALL INSTRUCTION:
    OPEN — external hardware/platform gates remain

CURRENT-SHA HOSTED CI:
    Ubuntu PASS — build + x86_64 Xvfb Vulkan validation smoke
    macOS PASS — x86_64/arm64/universal + MoltenVK bundle verification
    Windows PASS — Vulkan-enabled release build
```

。

本指示書の完遂後、初めて:

```text
Formal NVIDIA Phase 3 result
```

を作成できる。

---

# 67. 最重要原則

```text
Validation cleanをlatency superiorityと混同しない。
実装されたことと、速いことを混同しない。
host proxyをclick-to-photonと呼ばない。
INVALID runを結果へ混ぜない。
同じA/B groupでSHAを変えない。
unsupportedをFAILにしない。
AMD未確認をNVIDIA A/Bのblockerにしない。
AMD未確認のままcross-vendor defaultを変えない。
defaultはTelemetryOnlyのまま維持する。

---

# 68. 2026-08-13 execution addendum

This addendum is the authoritative result of executing this instruction on
the fixed local NVIDIA surface. The raw evidence is in
`docs/archive/audits/vulkan/2026-08-13-formal-ab/`.

## Documentation

```text
PASS — VSync-off stale note corrected in the runbook.
PASS — current relative-capability fallback semantics documented.
```

## Manual Phase 1

```text
PASS — Video Settings cancel x20, same-value apply x20, and changed-VSync
      Apply; `requested-vsync=off` / `selected-present-mode=IMMEDIATE` observed.
PASS — renderer switching x20 each for Software, OpenGL, OpenGL Compute, DX12.
PASS — ROM save/load/undo/reset and second-session reopen.
PASS — Fast Forward and Slow Motion hold/toggle paths.
PASS — window/minimize/fullscreen matrix and targeted Sync follow-up.
PASS — Debug Validation clean; VUID/SYNC-HAZARD/DEVICE_LOST findings 0.
NOT RUN — DPI transition; only one physical monitor was exposed.
```

The first Video Settings 20-cycle attempt was aborted after desktop mouse
input stole focus. It is retained as a diagnostic artifact; the independent
`video-20-final-r2` run is the authoritative PASS.

## NVIDIA Formal Phase 3

```text
COMPLETE — Release capture build, developer features OFF, latency capture ON.
COMPLETE — fixed source SHA, executable SHA, ROM SHA and display conditions.
COMPLETE — 21 valid CSV runs: 7 modes x 3 runs, randomized order recorded.
COMPLETE — 600 warmup + >=10,000 measured rows in every run.
COMPLETE — aggregator exit 0; invalid_rows 0; measured generation changes 0.
COMPLETE — A2/A3 target active 100% in every measured run.
COMPLETE — wait timeout rate <1%; queue full/recovery 0/0 in every run.
COMPLETE — per-run percentiles retained; no pooled-frame winner analysis.
COMPLETE — winner criteria evaluated without click-to-photon overclaim.
```

Formal result: A2 is the closest candidate, with median host-pipeline proxy
P50 improvement 3.3815% versus A0, but P95 improvement 1.9302% is below the
2% threshold. No mode satisfies the winner rule. `TelemetryOnly` remains the
shipping default. This is a fixed NVIDIA/Windows host-proxy result, not a
system-latency or cross-vendor claim.

## Separate environment gates

```text
NOT RUN — AMD Anti-Lag runtime (NVIDIA GPU only).
NOT RUN — Intel Vulkan runtime.
PARTIAL — Linux hosted CI build + Xvfb/llvmpipe Vulkan validation smoke PASS;
          Linux hardware/vendor runtime NOT RUN.
PARTIAL — macOS hosted CI MoltenVK bundle verification PASS;
          physical macOS/MoltenVK runtime NOT RUN.
```

These are separate shipping-default/generalization gates. Their absence does
not invalidate the completed NVIDIA Formal Phase 3 result, but the remaining
hardware/runtime gaps prevent a cross-vendor recommendation or a default
change. Hosted CI evidence is summarized in
`docs/archive/audits/vulkan/2026-08-13-formal-ab/ci/README.md`.
```
