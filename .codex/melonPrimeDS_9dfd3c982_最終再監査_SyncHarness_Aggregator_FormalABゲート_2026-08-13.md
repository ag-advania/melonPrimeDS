# melonPrimeDS 最新Push再監査
## `9dfd3c982` 起点 / follow-up `c6a4fe0c8` — Sync Validation / Harness Integrity / Aggregator Integrity / Formal Phase 3 最終ゲート

- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver2`
- 監査起点HEAD: `9dfd3c982a4a29c5f721c087c3a61a8485693e1b`
- 追補実装コミット: `c6a4fe0c80fb38dd8a5faaee032a44187a48b3c4`
- Commit message: `fix: close Vulkan diagnostic and audit hygiene gates`
- 追補実装コミットのParent: `9dfd3c982a4a29c5f721c087c3a61a8485693e1b`
- 監査日: 2026-08-13
- Backend: **現行Vulkan clean-room backend**

---

# 1. 結論

前回監査で残した主要指摘:

```text
P1-1  Sync harness applied-config integrity
P1-2  INVALID run output ordering
P2-1  warmup-generation boundary
P2-2  test config restoration
```

は、監査起点 `9dfd3c982...` に対する追補実装コミット
`c6a4fe0c...` まで含めて**すべて実装上解消済み**と判定する。

さらに、修正後の保存runtime evidenceは意図した:

```text
Policy       JustInTime
Reflex       Off / inactive
VSync        On
PresentMode  FIFO
Authority    GenericPresentTiming
TargetMode   relative
Target JIT   active
```

まで到達している。

最終Sync gate:

```text
Synchronization Validation banner   confirmed
VUID                                0
SYNC-HAZARD                         0
DEVICE_LOST                         0
swapchain rebuilds                  22
queue-at-capacity                   15
queue growth                        4
queue-full driver errors            0
config restore                      PASS
layer settings restore              PASS
exit                                0
```

。

したがって:

```text
Relative-Time Scheduling        IMPLEMENTED / PASS
Present pacing sync integrity   PASS
VUID-03268                      FIXED / PASS
Capacity / recovery path        PASS / NON-VACUOUS
Sync harness applied config     FIXED / RUNTIME PASS
Aggregator INVALID gate         FIXED / TEST COVERED
Generation lifecycle guard      FIXED / TEST COVERED
Timing queue-pressure reason    FIXED / RUNTIME PASS
Stale summary output hygiene    FIXED / TEST COVERED
Runbook VUID wording             FIXED
AMD Anti-Lag placement          STATIC PASS
Formal NVIDIA Phase 3 A/B       NOT RUN / NOT READY
Latency benefit                 NOT MEASURED
AMD runtime                     NOT RUN
Default pacing                  TelemetryOnly
```

。

**前回のP1 blockerは閉じた。**

今回の再監査で確認した追加指摘も、すべて実装・回帰テスト・runtimeまたは
runbook差分で閉じた。

```text
P2-A FIXED:
capacity pauseは`TimingQueuePressure`として記録し、
実際の`GetPastPresentationTimingEXT` failureとは分離

P2-B FIXED:
aggregatorは既存`--out` summaryを先に除去し、INVALID時のstale fileを残さない

P3 FIXED:
runbookのinitial VUIDと後段のVUID-03268を別sessionとして明記
```

これにより、コード・測定基盤・出力hygiene・runbook記述の今回監査範囲は
閉じた。ただし手動lifecycleとFormal A/Bの外部測定ゲートは別に未完了である。

---

# 2. Remote branch / commit確認

GitHub上で検証対象とした実装コミットは:

```text
origin/develop_remakeVulkan_ver2
```

相当のbranchに積む追補実装コミットは:

```text
c6a4fe0c80fb38dd8a5faaee032a44187a48b3c4
```

。

commit message（追補実装）:

```text
fix: close Vulkan diagnostic and audit hygiene gates
```

。

parent（監査起点）:

```text
9dfd3c982a4a29c5f721c087c3a61a8485693e1b
```

。

この後続の監査文書メタデータ更新コミットを含む最終branch/worktree状態は、
本監査のhandoff時に `git status` と `git ls-remote` で再確認する。

監査起点でのユーザー報告:

```text
Commit: c6a4fe0c8
実装コミット作成済み
```

を追補実装コミットとして引き継ぐ。

なおGitHub connectorからローカルPCの:

```text
git status
worktree clean
originとの0/0
```

そのものは独立確認できない。

remote branchが追補実装コミットおよび後続の監査文書更新を含む最終commitを
指すことをhandoff時に確認する。

---

# 3. Harness config root — FIXED

前回問題:

```text
script:
    <BuildDir>\melonDS.toml へ書く

actual runtime:
    <BuildDir>\portable\melonDS.toml を読む
```

。

最新scriptでは:

```powershell
$portableDir = Join-Path $dir 'portable'

$configRoot = if (Test-Path -LiteralPath $portableDir -PathType Container) {
    $portableDir
} else {
    $dir
}

$configPath = Join-Path $configRoot 'melonDS.toml'
```

。

portable buildでは:

```text
<BuildDir>\portable\melonDS.toml
```

を明示的に選択。

**評価: FIXED / PASS**

---

# 4. Config backup / restore

実行前:

```powershell
$hadConfig = Test-Path $configPath
$originalConfig = ReadAllBytes(...)
```

。

終了時:

```text
元から存在:
    WriteAllBytes(original)
    Base64比較でbyte-for-byte確認

元から不存在:
    test configを削除
    不存在を確認
```

。

layer settingsについても同じ復元方式。

これは前回P2-2の要求を満たす。

**評価: PASS**

---

# 5. Runtime self-check

harnessはrun終了後のstdout/stderrからactual stateを抽出。

確認項目:

```text
policy
Reflex requested
Reflex actual
Reflex lowLatencyMode
Reflex boost
requested VSync
selected present mode
```

。

指定値と異なる場合:

```text
config integrity : MISMATCH
exit 1
```

。

従って:

> config fileへ書いたから設定されたはず

ではなく:

> runtime logでeffective stateを証明する

構造へ改善された。

**評価: PASS**

---

# 6. Synchronization banner検証

以前は:

```text
CURRENT-VALIDATION-ENABLEDがどこかに存在
Synchronizationという文字列がどこかに存在
```

という独立検索だった。

現在はbanner行を見つけ、その後20行のblock内に:

```text
Synchronization
```

が存在することを確認。

これはvalidation設定の誤検出耐性を上げる。

**評価: PASS**

---

# 7. stdout / stderr監視

現在:

```powershell
$logPaths = @($out, $err)
$log = Get-Content $logPaths
```

として両方を統合。

その上で:

```text
VUID-
SYNC-HAZARD
DEVICE_LOST
validation banner
runtime configuration
```

を検索。

前回P2のstderr漏れは閉じている。

**評価: PASS**

---

# 8. Intended Sync runtime evidence

保存済み:

```text
vk-min-sync-final-audit.out.log
vk-min-sync-final-audit.err.log
vk-min-sync-final-audit.harness.log
```

。

harness summary:

```text
config path:
  ...\build\debug-mingw-vulkan-validation2\portable\melonDS.toml

config restore : PASS
layer restore  : PASS

swapchain rebuilds: 22
device lost: 0

config self-check:
  policy=JustInTime
  reflex=off/inactive
  vsync=on
  present-mode=FIFO

config integrity : PASS
sync validation  : enabled (banner confirmed)
sync hazards     : 0
validation       : clean
```

。

**評価: RUNTIME PASS**

---

# 9. Raw logでもconfigを確認

raw output自体でも:

```text
Opened ".../portable\melonDS.toml"
```

。

起動時:

```text
NVIDIA Reflex mode=off
policy=JustInTime
requested-vsync=on
selected-present-mode=FIFO
Reflex requested=off
Reflex actual=inactive
```

。

従ってharness summaryだけのself-reportではなく、
raw emulator logと一致する。

**評価: PASS**

---

# 10. Relative JIT actual activation

raw logではbootstrap後に:

```text
policy=JustInTime
authority=GenericPresentTiming
state=TargetSchedulingActive
targetScheduling=capable
targetMode=relative
boundedWait=on
absoluteSupported=no
relativeSupported=yes
frameIntervalNs=16666667
fallback=none
```

。

さらにperiodic timing:

```text
jit=active
target != 0
queueFull=0
```

を確認できる。

従って今回のSync runは単に:

```text
Policy 2というconfig値を読んだ
```

だけでなく、

> **current NVIDIA surface上のrelative target schedulerを実際にactivateした**

runtime evidenceになっている。

**評価: PASS**

---

# 11. Capacity pressure path

final intended runでも:

```text
present timing results queue at capacity;
timing metadata paused pending drain
```

が発生。

その後:

```text
present timing results queue grown to 32 after draining;
timing metadata re-enabled
```

。

archive集計:

```text
capacity 15
growth    4
fallback=timing queue pressure 14
fallback=timing query failed 0
queue-full errors 0
```

。

従ってpressure/recovery pathは非vacuous。

**評価: PASS**

---

# 12. VUID-03268 defensive retry

既存fix:

```cpp
present.waitSemaphoreCount = 0;
present.pWaitSemaphores = nullptr;
```

は保持。

ただしcurrent normal pressure pathではcapacity guardにより、
driverがqueue-full errorを返す前にmetadataをpause。

従って:

```text
capacity guard       normal path
queue-full retry     defensive fallback
```

という整理。

**評価: PASS**

---

# 13. Complete-report-only slot accounting

current:

```cpp
if (reports[i].reportComplete == VK_TRUE)
    ++completedReportCount;

OutstandingTimedPresents -= completedReportCount;
```

。

queue growthも:

```text
completedReportCount > 0
```

時のみ。

partial reportをslot解放扱いしない。

前回のSync hazard対策として成立。

**評価: PASS**

---

# 14. `swapchain_generation`

capture:

```text
swapchain_generation
```

。

Pacer:

```cpp
++SwapchainGeneration;
```

。

`ResetTimingLifecycle()`ではgenerationをresetしない。

従って:

```text
swapchain-local timing counters
```

と:

```text
pacer-lifetime swapchain generation
```

を分離できる。

**評価: PASS**

---

# 15. Warmup boundary

aggregatorは現在:

```python
self.warmup_generation = generation
```

としてlast warmup generationを保持。

first measured rowで:

```text
warmup generation != measured generation
```

なら:

```text
warm-up baseline invalid
```

としてINVALID。

前回の:

```text
旧swapchainのwait timeout baseline
+
新swapchainのmeasurement counter
```

混在問題は閉じた。

**評価: FIXED / PASS**

---

# 16. INVALID-before-output

current `main()`:

```text
load runs
↓
collect problems
↓
if problems:
    stderrへINVALID
    return 1
↓
summary rows作成
↓
stdout CSV
↓
--out CSV
↓
per-mode
```

。

つまり:

```text
INVALID run
```

からcurrent invocationがnormal summaryを新規生成しない。

前回P1-2は解消。

**評価: FIXED / PASS**

---

# 17. Aggregator regression test

追加:

```text
tools/testing/aggregate-vulkan-latency-tests.py
```

。

invalid synthetic:

```text
warmup generation = 1
measured generation = 2
pre-existing summary = stale
```

期待:

```text
exit 1
stdout empty
summary.csv not created
stale summary removed
per-mode output absent
warm-up diagnostic present
```

。

valid synthetic:

```text
same generation
```

期待:

```text
exit 0
stdout normal summary
summary.csv created
```

。

testはgeneration boundary、stale output、normal output orderingを直接回帰化している。

**評価: TEST COVERAGE PASS / current execution PASS**

---

# 18. Low-latency contract audit

static contractには追加で:

```text
actual config path
original config backup
config integrity mismatch
requested-vsync parsing
Synchronization banner-local check
warmup_generation
warm-up baseline invalid
TimingQueuePressure / timing queue pressure
remove_stale_output / unlink(missing_ok=True)
aggregate regression test presence
INVALID判定がsummary/per-modeより前
```

を要求。

つまり今回修正したmeasurement/test integrityが
単発修正で終わらず静的契約化されている。

**評価: PASS**

---

# 19. Short Sync controls

archive READMEには追加で:

```text
Policy0 / ReflexOff / VSyncOn
Policy2 / ReflexOn  / VSyncOn
```

のshort idle controlsを記録。

両方:

```text
2 rebuilds
banner confirmed
VUID 0
SYNC-HAZARD 0
DEVICE_LOST 0
config/layer restore PASS
```

。

これによりharness self-checkがPolicy2/ReflexOff専用に
偶然成立しているだけではないことを補強。

**評価: PASS**

---

# 20. AMD Anti-Lag

current queue ordering:

```text
queue lock acquired
↓
AntiLag.EndFrame / PRESENT update
↓
Reflex PRESENT_START if active
↓
host PRESENT_START
↓
vkQueuePresentKHR
```

。

static placementは正しい。

NVIDIA evidenceでは:

```text
AMD extension unsupported
actual inactive
```

なのでAMD functionalityのruntime proofにはならない。

```text
AMD placement     STATIC PASS
AMD runtime       NOT RUN
```

。

---

# 21. Formal Phase 1 status

完了済みautomated subset:

```text
[x] resize x40
[x] minimize/restore x20
[x] fullscreen x8
[x] idle
```

。

current-tree targeted Sync:

```text
[x] Policy2 / ReflexOff / VSyncOn / FIFO minimize
[x] Policy0 / ReflexOff short control
[x] Policy2 / ReflexOn short control
```

。

未完了manual:

```text
[ ] DPI change
[ ] Video Settings open/cancel/apply
[ ] renderer switching
[ ] ROM lifecycle
[ ] Fast Forward
[ ] Slow Motion
```

。

従って:

```text
Phase 1 = PARTIAL
```

。

---

# 22. Formal Phase 3 status

未実行:

```text
A0 TelemetryOnly
A1 PresentWait
A2 JustInTime
A3 JustInTimeFifoLatestReady
B1 Reflex On
B2 Reflex On+Boost
C0 VSync Off control
```

。

runbook requirement:

```text
>= 3 runs / mode
randomized order
600 warmup
10,000 measured frames
same scene/build/settings
```

。

従って:

```text
FORMAL PHASE 3 A/B = NOT RUN / NOT READY
```

。

---

# 23. Latency claim

現在証明済み:

```text
Relative JIT mechanism works
TargetSchedulingActive reached
GenericPresentTiming authority reached
VSync/FIFO intended config applied
Sync Validation clean in targeted run
```

。

未証明:

```text
Relative JIT lowers latency
Relative beats TelemetryOnly
Relative beats PresentWait
Relative beats Reflex
FIFO_LATEST_READY lowers latency
Reflex On+Boost is best
```

。

**Latency benefit = NOT MEASURED**

---

# 24. Default policy

default:

```text
TelemetryOnly
```

維持。

runtime validationや機能実装を
latency superiorityと混同してdefaultを変更していない。

**評価: PASS**

---

# 25. P2-A — capacity pause reason（FIXED）

前回raw logで確認したdiagnostic inconsistencyは、今回のfollow-upで修正した。

queue capacityに達すると:

```cpp
TimingMetadataEnabled = false;
TimingQueuePressureActive = true;
TimingQueueRecoveryPending = true;
```

。

修正前のpure policy classifierは:

```cpp
if (!caps.TimingMetadataEnabled)
{
    return caps.PresentTimingSurface
        ? VulkanJitFallbackReason::TimingQueryFailed
        : VulkanJitFallbackReason::PresentTimingUnsupported;
}
```

。

修正前は:

```text
metadata disabled because queue at capacity
```

でも:

```text
fallback=timing query failed
```

になる。

実際のSync raw logにもcapacity pause直後に、修正前は:

```text
fallback=timing query failed
```

が出ているが、その直後に:

```text
queue grown
metadata re-enabled
fallback=none
```

へ復帰。

本当の`GetPastPresentationTimingEXT` failure pathなら
polling自体を停止するため、このケースはquery failureではない。

---

# 26. P2-Aの影響

影響しない:

```text
renderer correctness
target_scheduling actual column
Sync correctness
queue recovery
A/B numerical result
```

。

影響する:

```text
fallback_reason診断
ログ読解
capacity pauseと本当のquery failureの区別
```

。

以前はqueue-full countが別診断だったが、
current proactive capacity guardではdriver queue-full errorを避けるため:

```text
timing_queue_full_count = 0
```

のままpressureが起こり得る。

従って専用reasonの価値は以前より高い。

修正後は`TimingQueuePressure`を既存fallback enumの末尾へ追加し、
旧capture CSVの数値reasonを変えずに、capacity guardとqueue-full retryの
pressure stateをclassifierへ渡す。Release developer runでは:

```text
queue-at-capacity                 12
fallback=timing queue pressure    12
fallback=timing query failed       0
queue growth                       5
queue-full driver errors            0
exit                                0
```

分類:

```text
P2 / DIAGNOSTIC SEMANTICS / FIXED / RUNTIME PASS
```

。

---

# 27. P2-A実装結果

実装:

```text
TimingQueuePressure
```

を追加し、既存enum値の後ろへ配置。

capacity guard時に明示的なstate/reasonを持つ。

理想:

```text
queue capacity pause:
    fallback=timing queue pressure

actual query error:
    fallback=timing query failed
```

。

policy matrix testとRelease runtime logの両方で、pressureとquery failureの
区別を確認した。これはFormal A/B前のdiagnostic integrity gateを閉じる。

---

# 28. P2-B — pre-existing summary file（FIXED）

current aggregatorはINVALIDをoutput前に判定するため、
**current invocationがsummaryを生成することはない**。

前回は:

```text
summary.csvが前回runから既に存在
```

する場合、INVALID invocationはそのfileを削除しなかった。

従ってoperatorが:

```text
exit codeを見ず
既存summary.csvだけを見る
```

と古い結果を今回の結果と誤認し得る。

修正後のsynthetic regression testは:

```text
stale summary pathが最初から存在
```

するケースで、INVALID実行後にpathが消えることを確認している。

---

# 29. P2-B実装結果

aggregatorは入力収集後、summary出力がcapture入力と同一でないことを確認して
から、既存出力を除去する:

```python
out_path = Path(args.out) if args.out else None
if out_path is not None:
    remove_stale_output(out_path, files)
```

。

INVALIDなら:

```text
summary path absent
```

を保証。

分類:

```text
P2 / OPERATIONAL OUTPUT HYGIENE / FIXED / TEST COVERED
```

。

`python tools/testing/aggregate-vulkan-latency-tests.py`でstale除去、INVALID
stdout/per-mode抑止、valid summary生成を同時にPASSした。

---

# 30. P3 — runbookのVUID表現（FIXED）

runbook Status節:

```text
One VUID was found and fixed during the session
(VUID-VkPresentTimingInfoEXT-timeDomainId-12400)
```

。

しかし同じrunbook後段では:

```text
VUID-vkQueuePresentKHR-pWaitSemaphores-03268
```

も明示的に「found and fixed」と記載。

歴史的sessionの範囲が違うという読み方は可能だが、
文書だけ読むと:

```text
VUIDは1種類だけ
```

にも読める。

修正後:

```text
An earlier validation session found...
```

runbookはinitial `timeDomainId-12400` sessionと、後続の
`pWaitSemaphores-03268` Sync sessionを分けて記載する。

**P3 / DOCUMENTATION CLARITY / FIXED**

---

# 31. Build / test evidence分類

最新audit documentに記録されている検証:

```text
audit-low-latency-contract.py      PASS
PowerShell parse                   PASS
aggregate regression              PASS
Python syntax                      PASS
C++ configured-tree build          PASS / 1 job
Vulkan present timing tests        PASS
Intel XeLL state-machine tests     PASS
git diff --check                   PASS
```

。

今回のfollow-upは:

```text
TimingQueuePressure reason
stale summary cleanup
runbook VUID wording
tests
runtime evidence
```

を追加した。

Release existing treeは変更ソースを再コンパイルし、
`Vulkan present timing model tests PASS`を実行した。

Canonical Release wrapperについては既存Ninja permission issueが記録されており、
成功したbuild evidenceはexisting build directory経由と区別されている。

この区別は適切。

---

# 32. GitHub Actions

追補実装コミット `c6a4fe0c...` について:

```text
combined statuses:
    none

associated PR workflow runs:
    none
```

。

従って:

```text
GitHub Actions = NOT VERIFIED
```

。

local validation/auditと混同しない。

---

# 33. Formal NVIDIA A/B readiness

現時点:

```text
[x] Relative JIT implemented
[x] NVIDIA relative runtime active
[x] FIFO_LATEST_READY reachable from previous evidence
[x] target capture actual-state
[x] bounded wait actual-state
[x] swapchain generation guard
[x] warmup generation guard
[x] INVALID-before-output
[x] Sync intended config self-check
[x] Policy2 / ReflexOff / VSyncOn / FIFO Sync PASS
[x] VUID 0
[x] SYNC-HAZARD 0
[x] DEVICE_LOST 0
[x] capacity/recovery path exercised
[x] TimingQueuePressure reason distinct from query failure
[x] stale --out summary removed before aggregation
[x] runbook historical VUID wording corrected

[ ] manual DPI
[ ] manual Video Settings lifecycle
[ ] manual renderer switching
[ ] manual ROM lifecycle
[ ] manual Fast Forward
[ ] manual Slow Motion
[ ] Formal A/B >=3 randomized runs/mode
```

。

従って:

```text
FORMAL NVIDIA PHASE 3 A/B:
    GATE CLOSED / NOT READY
```

。

理由はもうstatic P1ではなく:

```text
manual lifecycle validation
formal measurement execution
```

Current configured trees also prove why no Formal CSV is claimed here:

```text
release-mingw-x86_64:
    MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON
    MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE=OFF

debug-mingw-vulkan-validation2:
    MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON
    MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE=OFF
```

These trees are valid for runtime correctness/diagnostic gates, not for the
Release capture measurement required by Phase 3.

。

これはユーザー報告と一致する。

---

# 34. AMD gate

```text
AMD Anti-Lag placement       STATIC PASS
AMD actual GPU runtime       NOT RUN
```

。

AMD実機未検証は:

```text
NVIDIA Formal A/B blockerではない
AMD low-latency claim blocker
```

。

---

# 35. click-to-photon

host CSV:

```text
input sample
→ vkQueuePresentKHR return
```

はPC pipeline proxy。

pixel illuminationではない。

従って:

```text
click-to-photon = NOT RUN
```

を維持。

必要なら:

```text
Reflex Analyzer
high-speed camera
photodiode
```

など外部計測。

---

# 36. 最終status

| 項目 | 判定 |
|---|---|
| 追補実装コミット | **c6a4fe0c... VERIFIED** |
| 最終branch/worktree handoff | **PUSH後にVERIFIED** |
| Relative-Time Scheduling | **IMPLEMENTED / PASS** |
| NVIDIA relative target activation | **RUNTIME PASS** |
| Intended Sync config | **PASS** |
| Config path | **FIXED** |
| Config restore | **PASS** |
| Layer restore | **PASS** |
| Sync banner | **PASS** |
| VUID | **0 / PASS** |
| SYNC-HAZARD | **0 / PASS** |
| DEVICE_LOST | **0 / PASS** |
| Capacity pressure | **NON-VACUOUS PASS** |
| Queue growth | **PASS** |
| Queue-full driver errors | **0 / PASS** |
| Generation capture | **PASS** |
| Warmup generation guard | **PASS** |
| INVALID-before-output | **PASS** |
| Aggregator regression coverage | **PASS / SOURCE VERIFIED** |
| Timing queue-pressure reason | **FIXED / RUNTIME PASS** |
| Stale summary output hygiene | **FIXED / TEST COVERED** |
| Runbook VUID wording | **FIXED** |
| AMD Anti-Lag placement | **STATIC PASS** |
| AMD runtime | **NOT RUN** |
| Manual Phase 1 | **PARTIAL / NOT RUN** |
| Formal Phase 3 A/B | **NOT RUN / NOT READY** |
| Latency benefit | **NOT MEASURED** |
| click-to-photon | **NOT RUN** |
| Default | **TelemetryOnly** |
| GitHub Actions | **NOT VERIFIED** |
| New blocking P0/P1 | **NONE FOUND** |
| New P2 | **NONE / CLOSED** |
| New P3 | **NONE / CLOSED** |

---

# 37. 推奨次作業

優先順位:

```text
1. Manual Phase 1
   - DPI
   - Video Settings
   - renderer switching
   - ROM lifecycle
   - Fast Forward
   - Slow Motion

2. Formal NVIDIA Phase 3 A/B
   - >=3 randomized runs/mode
   - 600 warmup
   - 10,000 measured
   - same build/scene/settings
   - INVALID exit code必須確認

3. AMD runtime validation

4. click-to-photon
```

。

---

# 38. Formal A/Bへ入る直前のDoD

```markdown
- [x] queue pressure reason is distinct from timing query failure
- [x] stale `--out` summary is removed before aggregation
- [x] initial and later VUID sessions are distinguished in the runbook
- [x] current implementation SHAを固定（section 2）
- [x] 最終push後のworktree cleanをoperator側で記録
- [ ] explicit A/B Release capture build
- [ ] developer features OFF
- [ ] Validation Layer OFF
- [ ] manual Phase 1 complete
- [ ] fixed savestate / scene / camera / HUD
- [ ] monitor / VRR / driver / power plan記録
- [ ] randomized run order作成
- [ ] unique CSV per run
- [ ] aggregate process exit codeを確認
- [ ] INVALID runを再採取
- [ ] target active ratio >=95%確認
- [ ] queue recoveries / timeouts確認
- [ ] latency superiorityを測定前に主張しない
```

---

# 39. 監査対象

```text
tools/testing/vulkan-present-event-matrix.ps1
tools/perf/aggregate-vulkan-latency.py
tools/testing/aggregate-vulkan-latency-tests.py
tools/ci/audits/audit-low-latency-contract.py
docs/development/testing/vulkan-present-pacing-runbook.md
docs/archive/audits/vulkan/2026-08-13-event-matrix/README.md
docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-configfix3.out.log
docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-configfix3.err.log
docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-configfix3.harness.log
docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-pressurefix.out.log
docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-pressurefix.err.log
docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-final-audit.out.log
docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-final-audit.err.log
docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-final-audit.harness.log
.codex/melonPrimeDS_最新Push再監査_51d3bee0_SyncValidation_GenerationGuard_FormalAB最終ゲート_2026-08-13.md
src/VulkanPresentPacer.cpp
src/VulkanPresentPacingPolicy.h
src/frontend/qt_sdl/MelonPrimeVulkanPresenter.cpp
```

---

# 40. 最終監査総括

追補実装コミット `c6a4fe0c8` は、前回監査で見つけたmeasurement/test-integrityの
穴を正しく閉じている。

最大の問題だった:

```text
harnessが別configへ書いていたため
「cleanだが違うconfiguration」
```

は、

```text
portable config root resolution
+
runtime self-check
+
byte-for-byte restore
```

で解消。

修正後raw evidenceでは:

```text
JustInTime
ReflexOff
VSyncOn
FIFO
GenericPresentTiming
relative target
TargetSchedulingActive
```

を確認でき、
その状態で:

```text
Synchronization enabled
VUID 0
SYNC-HAZARD 0
DEVICE_LOST 0
```

。

aggregatorも:

```text
generation boundary
INVALID-before-output
```

を回帰テスト化しており、
前回P1/P2は閉じたと判断してよい。

今回新しく見つかったP2/P3は以下のとおり修正済み:

```text
TimingQueuePressure diagnostic reason       FIXED / RUNTIME PASS
stale --out summary cleanup                 FIXED / TEST PASS
historical VUID wording                     FIXED / DOC PASS
```

。

従って現時点の正確な結論は:

```text
STATIC / MEASUREMENT-INTEGRITY BLOCKERS:
    CLOSED

FOLLOW-UP P2/P3:
    CLOSED

CURRENT NVIDIA SYNC GATE:
    PASS

FORMAL PHASE 3:
    STILL NOT READY

REMAINING REASON:
    manual Phase 1
    formal >=3-run A/B measurement

AMD:
    runtime pending

LATENCY BENEFIT:
    NOT MEASURED

DEFAULT:
    TelemetryOnly
```

。

つまり、**コード/測定基盤側の重大な監査指摘は今回で閉じた**。
次の本質的な作業は、手動lifecycle matrixを終わらせた後の正式A/B測定である。
