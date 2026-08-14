# melonPrimeDS Vulkan低遅延技術 — コミット後再監査結果・追加修正指示書

## 0. 監査情報

- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver3`
- 前回監査基準HEAD: `4fb3abfad70dc1e43b5014649cd9bfe67ef159c5`
- 今回確認HEAD: `266aa7e4132e945d6e398d4d84197373e7ea4b3f`
- 差分: **4 commits ahead / 0 behind**
- 監査日: 2026-08-14
- 主対象:
  - 前回P2-1: EXT target不能時のGOOGLE fallback
  - 前回P2-2: `VK_GOOGLE_display_timing + VK_PRESENT_MODE_FIFO_LATEST_READY_KHR`
  - pure model tests
  - low-latency contract audit
  - present pacing lifecycle
  - push済み監査証拠
- 監査方法:
  - GitHub connectorでbranch HEAD、commit、現行sourceを直接確認
  - 前回監査HEADとのcompare
  - Khronos Vulkan Documentationとの仕様照合
  - push済みtest/audit/evidenceの静的再監査

この文書は、`353cd23c` 時点の指摘を `266aa7e4` で修正した後の
canonical auditである。旧監査MDは履歴資料として残すが、現行の判定は
本書の「30. 修正適用後の再監査結果」を優先する。

---

# 1. 今回の結論

## 総合判定

```text
PASS WITH OPEN RUNTIME GATES

P2-1 policy selector:
    PASS

P2-1 actual pacer lifecycle:
    PASS (policy-aware lifecycle fixed in 266aa7e4)

P2-2 GOOGLE + FIFO_LATEST_READY:
    PASS (source + pure model + contract)
    physical runtime: NOT RUN

Documentation / audit evidence:
    PASS (canonical document updated; prior document superseded)
```

なお、以下の3〜12節および19〜27節は `353cd23c` 時点で検出した残件の
再現・修正指示を履歴として保持している。修正後の証拠と現在の未実行ゲートは
30節に集約する。

前回指摘した2点に対し、commit:

```text
bd375cad7c926f22b0c5a7f429e4952d8f3a3e53
vulkan: complete Google timing fallback compatibility
```

で主要なarchitecture修正が入った。

特に以下は正しい。

```text
Telemetry backend selection
Target backend selection
```

を分離したこと。

また:

```text
VK_KHR_present_mode_fifo_latest_ready
```

をEXT専用条件から外し、GOOGLE target backendでも利用可能にしたこと。

しかし、P2-1については**pure policy resolverと実際のpacer lifecycleでbackend selectionが再び分岐している**。

そのため:

```text
EXT telemetryは利用可能
EXT target schedulingは利用不能
GOOGLE display timingは利用可能
```

という、まさにP2-1が対象としているmixed capability環境で、

```text
policy:
    GOOGLE target backendを選択

runtime lifecycle:
    GOOGLE timingを初期化・pollしない
```

という不一致が残る。

結果としてGoogle JIT targetが:

```text
TimingPropertiesNotReady
```

から永続的に抜けられない経路が存在する。

したがって、`353cd23c` 時点ではpush済み監査MDの:

```text
P2-1 implementation DoD: PASS
```

は成立しなかった。この残件は後続commit `266aa7e4132e945d6e398d4d84197373e7ea4b3f`
で修正済みであり、現HEADの判定は30節に記載する。

---

# 2. 今回pushされた4 commits

前回確認HEAD:

```text
4fb3abfad70dc1e43b5014649cd9bfe67ef159c5
```

から `353cd23c` 時点のHEAD:

```text
353cd23c44aa8bd09b307cf152cbe8d56c4442ff
```

まで3 commits。

## 2.1 実装修正

```text
bd375cad7c926f22b0c5a7f429e4952d8f3a3e53
vulkan: complete Google timing fallback compatibility
```

主変更:

```text
src/VulkanDevice.cpp
src/VulkanPresentPacer.cpp
src/VulkanPresentPacingPolicy.h
tools/testing/vulkan-present-timing-tests.cpp
tools/ci/audits/audit-low-latency-contract.py
```

## 2.2 監査MD追加

```text
19dd1bc29397cc263fc88a1255aa0621b2e819f7
audit: close Vulkan timing compatibility re-audit
```

## 2.3 Linux build evidence追記

```text
353cd23c44aa8bd09b307cf152cbe8d56c4442ff
audit: record Linux Vulkan build evidence
```

## 2.4 今回のP2-1 lifecycle修正

```text
266aa7e4132e945d6e398d4d84197373e7ea4b3f
vulkan: align Google polling with target backend
```

`BeginFrame`、`OnSwapchainCreated`、policy resolverが同じ純粋な
`SelectVulkanPresentBackendForPolicy`を使うようになり、混在EXT/GOOGLE
ケースでもGoogleのrefresh/past-timing bootstrapが抜け落ちない。

現HEADのGitHub combined statusはstatus entryなし。

PR-triggered workflow runも現HEADでは取得できなかった。

したがってLinux build PASSは現在:

```text
repositoryへ記録された実行証拠
```

として扱い、

```text
GitHub Actionsで独立再確認済み
```

とは扱わない。

---

# 3. P2-1 — target-aware EXT → GOOGLE fallback

## 3.1 policy層の修正は正しい

`src/VulkanPresentPacingPolicy.h`に:

```cpp
SelectVulkanPresentTimingBackend(...)
```

と:

```cpp
SelectVulkanPresentTargetBackend(...)
```

が分離された。

Telemetry用:

```cpp
if (caps.PresentTimingSurface && caps.TimingMetadataEnabled)
    return ExtPresentTiming;

if (caps.GoogleDisplayTimingAvailable
    && caps.GoogleDisplayTimingRuntimeEnabled)
    return GoogleDisplayTiming;
```

Target用:

```cpp
const bool extTargetCapable =
    caps.PresentTimingSurface
    && caps.TimingMetadataEnabled
    && caps.PresentId2Surface
    && !caps.TargetSchedulingLifecycleFailed
    && SelectVulkanTargetSchedulingMode(caps) != None;

if (extTargetCapable)
    return ExtPresentTiming;

if (Google usable)
    return GoogleDisplayTiming;
```

これは前回指摘した:

```text
EXTがmetadata backendとして存在する
```

ことと:

```text
EXTがtarget scheduling backendとして使用可能
```

であることを分離している。

### 判定

```text
PASS
```

---

# 4. P2-1残件 — 実pacer lifecycleがtarget-awareになっていない

## 4.1 問題箇所1: OnSwapchainCreated()

現行:

```cpp
if (PresentTimingSurface)
{
    // EXT timing queue / properties / domains
    ...
}

if (!PresentTimingSurface && GoogleDisplayTimingRuntimeEnabled)
{
    RefreshGoogleTiming();
}
```

つまり:

```text
PresentTimingSurface == true
```

である限り、Google refresh-cycle initializationを行わない。

しかしP2-1が対象とするmixed caseでは:

```text
PresentTimingSurface = true
EXT metadata = usable

Absolute target = unsupported
Relative target = unsupported

Google = usable
```

である。

Target selectorは正しく:

```text
GOOGLE
```

を選ぶが、Google lifecycle初期化は:

```text
!PresentTimingSurface
```

条件で拒否される。

## 4.2 問題箇所2: BeginFrame()

現行Google poll gate:

```cpp
if (!reflexActive && !antiLagActive
    && SelectVulkanPresentTimingBackend(BuildCapabilities())
        == VulkanPresentTimingBackend::GoogleDisplayTiming)
{
    ReportGooglePastTiming();
}
```

ここで使っているのはTelemetry selector。

P2-1修正後のJIT policyが使うのは:

```text
SelectVulkanPresentTargetBackend
```

。

したがってmixed caseでは:

```text
Telemetry selector:
    EXT

Target selector:
    GOOGLE
```

となる。

結果:

```text
BeginFrame:
    Google pollしない

ResolveDecision:
    Google target backendを選ぶ
```

という不一致になる。

---

# 5. mixed capabilityでの再現シーケンス

以下のcapabilityを持つdevice/surfaceを想定する。

```text
VK_EXT_present_timing:
    presentTiming = yes

EXT timing metadata:
    usable

VK_KHR_present_id2:
    yes

presentAtAbsoluteTime:
    no

presentAtRelativeTime:
    no

VK_GOOGLE_display_timing:
    yes

Google entry points:
    resolved
```

## Step 1 — QuerySurfaceCapabilities

```text
PresentTimingSurface = true
TimingMetadataEnabled = true

AbsoluteTimingSurface / Device = false
RelativeTimingSurface / Device = false

GoogleDisplayTimingRuntimeEnabled = true
GoogleRefreshDurationReady = false
```

## Step 2 — OnSwapchainCreated

`PresentTimingSurface`がtrueなのでEXT初期化へ入る。

Google側:

```cpp
if (!PresentTimingSurface && GoogleDisplayTimingRuntimeEnabled)
```

はfalse。

従って`RefreshGoogleTiming()`は呼ばれない。

## Step 3 — BeginFrame

Telemetry selector:

```text
EXT metadata usable
→ EXT
```

なので`ReportGooglePastTiming()`は呼ばれない。

## Step 4 — ResolveDecision

Target selector:

```text
EXT target modeなし
GOOGLE usable
→ GOOGLE
```

。

しかし:

```text
GoogleRefreshDurationReady = false
```

なので:

```text
TimingPropertiesNotReady
```

となりtarget schedulingはOFF。

## Step 5 — PreparePresent

backendはGOOGLEなのでmetadata自体はattachされるが、target schedulingがOFFなので:

```text
desiredPresentTime = 0
```

。

## Step 6 — 次frame

BeginFrameは再びTelemetry selectorでEXTを選択するためGoogle past timingをpollしない。

このループが続く。

---

# 6. 判定

mixed EXT + GOOGLE環境では:

```text
resolverはGOOGLEを選ぶ
```

にもかかわらず:

```text
GoogleRefreshDurationReady
```

がtrueにならない。

したがって:

```text
JIT Google target
```

が実際には開始できない。

### Severity

```text
P2
```

renderer自体は継続するが、低遅延featureとしてtarget schedulingが機能しないため、P2-1をclosedにはできない。

---

# 7. EXT lifecycle failure fallbackにも同じ問題がある

pure testでは:

```text
TargetSchedulingLifecycleFailed = true
Google usable
→ GOOGLE
```

を確認している。

しかし実runtimeでは:

```text
TargetSchedulingLifecycleFailed = true
TimingMetadataEnabled = true
```

になり得る。

例:

```text
vkGetSwapchainTimingPropertiesEXT failure
```

。

この場合:

```text
Target selector = GOOGLE
Telemetry selector = EXT
```

となり、同じくGoogle pollが開始されない。

従ってpure testの:

```text
a failed EXT timing lifecycle must still allow GOOGLE target fallback
```

はselector単体では正しいが、production lifecycleを証明していない。

---

# 8. 既存testが検出しない理由

`TestTimingBackendSelection()`のmixed caseは`FullCapabilities()`を基にしており、Google capabilityを有効化した状態でresolverを直接評価する。

そのため:

```text
GoogleRefreshDurationReady = true
```

の完成済みcapabilityとして評価される。

つまりtest対象は:

```text
capability resolver
```

であり:

```text
BeginFrame
→ Google refresh/poll
→ resolver
→ PreparePresent
```

というproduction state transitionではない。

---

# 9. contract auditもこの穴を検出しない

追加されたcontract auditは:

```text
SelectVulkanPresentTargetBackend exists
mixed model test exists
VulkanTargetCanUseFifoLatestReady exists
```

を確認する。

しかし:

```text
BeginFrameのGoogle pollがpolicy-selected backendと一致すること
```

は監査していない。

現在の:

```cpp
SelectVulkanPresentTimingBackend(BuildCapabilities())
```

がBeginFrameに残っていてもauditはPASSする。

---

# 10. P2-1 根本修正方針

## 10.1 policyごとのbackend selectionを共通helperへ統一

推奨:

```cpp
constexpr VulkanPresentTimingBackend SelectVulkanPresentBackendForPolicy(
    VulkanPresentPacingPolicy policy,
    const VulkanPacingCapabilities& caps) noexcept
{
    return VulkanPolicyRequestsTargetTime(policy)
        ? SelectVulkanPresentTargetBackend(caps)
        : SelectVulkanPresentTimingBackend(caps);
}
```

最低でも:

```text
ResolveVulkanPresentPacing()
ClassifyVulkanTargetFallback()
VulkanPresentPacer::BeginFrame()
```

で同じhelperを使う。

目的:

```text
resolverが選ぶbackend
feedback pollするbackend
present metadataをattachするbackend
```

を一致させる。

## 10.2 BeginFrame

Google poll decisionをTelemetry selector直呼びから外す。

概念上:

```cpp
const VulkanPacingCapabilities caps = BuildCapabilities();
const VulkanPresentTimingBackend frameBackend =
    SelectVulkanPresentBackendForPolicy(GetPolicy(), caps);

if (!reflexActive && !antiLagActive
    && normalSpeed
    && frameBackend == VulkanPresentTimingBackend::GoogleDisplayTiming)
{
    const VulkanPacerBeginResult google = ReportGooglePastTiming();
    if (google != VulkanPacerBeginResult::Continue)
        return google;
}
```

`ReportGooglePastTiming()`は`GoogleRefreshDurationReady`がfalseなら内部で`RefreshGoogleTiming()`を行うので、mixed fallback bootstrapを開始できる。

## 10.3 OnSwapchainCreated

現在の:

```cpp
if (!PresentTimingSurface && GoogleDisplayTimingRuntimeEnabled)
```

はbackend-awareではない。

policy-selected backendを使うか、BeginFrameのcorrectness pathに一本化する。

推奨は:

```text
BeginFrameのpolicy-aware selection
```

を正しさの本体とし、OnSwapchainCreatedのGoogle queryはeager optimizationに留める。

---

# 11. 追加すべきtest

## Test A — policy backend selection

```text
JustInTime
EXT telemetry=yes
EXT target mode=none
GOOGLE=yes
GoogleRefreshReady=false

frame backend
→ GOOGLE
```

## Test B — Google bootstrap requested

`ShouldPollGoogleForFrame(...)`等をpure helper化するなら:

```text
mixed EXT telemetry + GOOGLE target
GoogleRefreshReady=false
→ true
```

。

## Test C — TelemetryOnly

```text
TelemetryOnly
EXT telemetry=yes
GOOGLE=yes
→ EXT
→ Google poll不要
```

。

## Test D — EXT bootstrap does not flap

```text
JustInTime
EXT target structurally capable
TimingPropertiesReady=false
TimeDomainsReady=false
GOOGLE=yes
→ EXT
```

。

## Test E — lifecycle failure fallback

```text
JustInTime
EXT metadata=yes
TargetSchedulingLifecycleFailed=true
GOOGLE=yes
GoogleRefreshReady=false

frame backend
→ GOOGLE
Google bootstrap requested
→ true
```

。

---

# 12. contract audit追加

最低限:

```text
BeginFrame must use policy-aware backend selection
for GOOGLE refresh/poll decisions.
```

を固定する。

また:

```text
SelectVulkanPresentTimingBackend(BuildCapabilities())
```

をBeginFrameのGoogle poll gateへ直接使わないことを監査する。

---

# 13. P2-2 — GOOGLE + FIFO_LATEST_READY

## 判定

```text
PASS
```

source / pure model / contract auditの範囲では前回指摘を解消している。

---

# 14. VulkanDevice側

現行:

```cpp
const bool hasGoogleDisplayTiming = ...;

const bool hasTimeBasedPresentBackend =
    hasPresentTiming || hasGoogleDisplayTiming;

const bool hasLatestReady =
    hasTimeBasedPresentBackend
    && HasExtension(VK_KHR_PRESENT_MODE_FIFO_LATEST_READY...)
    && latestReadyFeatures.presentModeFifoLatestReady;
```

。

これにより:

```text
EXT unavailable
GOOGLE available
FIFO_LATEST_READY available
```

でもdevice extensionをenable可能。

前回のEXT固定dependencyは解消済み。

---

# 15. Pacer側

`ShouldUseFifoLatestReady()`は:

```text
VulkanTargetCanUseFifoLatestReady()
SelectVulkanPresentTargetBackend()
```

を使用。

GOOGLE backendでは:

```text
PresentId2
EXT time domain
EXT timing queue
```

を要求しない。

EXT backendだけが:

```text
absolute/relative support
time-domain query
timing queue entry point
```

を要求する。

正しい分離。

---

# 16. Vulkan仕様との照合

Khronos Vulkan Documentationでは:

```text
VK_KHR_present_mode_fifo_latest_ready
```

のextension dependencyは:

```text
VK_KHR_swapchain
```

。

`VK_EXT_present_timing` dependencyではない。

また`VK_PRESENT_MODE_FIFO_LATEST_READY_KHR`は、target present timeを使う場合に:

```text
VK_GOOGLE_display_timing
```

を明示的に考慮する。

従って今回のP2-2修正は仕様に合致する。

---

# 17. P2-2 test

追加された`TestFifoLatestReadyBackendCompatibility()`では:

```text
GOOGLE-only + latest-ready
EXT + latest-ready
latest-ready unavailable
EXT target-incapable + GOOGLE
EXT lifecycle failure + GOOGLE
```

を確認している。

pure model coverageとして妥当。

---

# 18. P2-2残件

physical runtimeは未実行。

既存Intel Mac / MoltenVKは:

```text
FIFO_LATEST_READY unsupported
```

なので既存Formal結果をP2-2 runtime evidenceへ流用しない判断は正しい。

```text
P2-2 code DoD:
    PASS

P2-2 physical runtime:
    NOT RUN
```

。

---

# 19. P3 — lifecycle failure flagのsemanticsがsource内で不一致（353cd23c時点）

`VulkanPacingCapabilities`側コメントは:

```text
Sticky per-swapchain failure
```

。

一方`VulkanPresentPacer.h`の実state側コメントは:

```text
Sticky across swapchain recreation
```

。

実コードでは`ResetTimingLifecycle()`で`TargetSchedulingLifecycleFailed`をclearせず、`Shutdown()`でfalseへ戻す。

したがって実際のsemanticsは:

```text
swapchain-local
```

ではなく:

```text
pacer/surface lifetime sticky
```

に近い。

### 修正結果

`TargetSchedulingLifecycleFailed`の実装はこのsemanticsを維持し、コメントを
`pacer/surface lifetime (including swapchain recreation)`へ統一した。初期化・
Shutdownでのみclearされることをcontract auditで固定している。

353cd23c時点では上記2つの設計案が未決定だった。現実装は
`pacer/surface lifetime (including swapchain recreation)`を採用し、初期化・
Shutdownでのみclearされることをsourceコメントとcontract auditで固定した。

---

# 20. P3 — push済み監査MDのcommit SHA誤記（353cd23c時点）

push済みMD:

```text
.codex/melonPrimeDS_Vulkan低遅延技術_develop_remakeVulkan_ver3_再監査結果_2026-08-14.md
```

には:

```text
`bd375cad7f38...`（修正前の誤記）
```

と記載されている。

実commitは:

```text
bd375cad7c926f22b0c5a7f429e4952d8f3a3e53
```

。

前者はGitHub commitとして存在しない。

監査証拠ではidentityが重要なので正しいfull SHAへ置換する。

`266aa7e4`以降のcanonical auditでは実在するfull SHAへ修正済みである。

---

# 21. P3 — push済み監査MDの判定が矛盾（353cd23c時点）

同一MD内に旧判定と追記後判定が同居している。

旧MDの旧sectionでは:

```text
EXT backend priority:
    PARTIAL

model tests:
    PARTIAL
```

。

旧MDのsection 30でもP2-1 / P2-2を残件として記述。

一方、旧MDのsection 32では:

旧MDはその後superseded扱いとし、現行のcanonical判定を本書へ集約した。

```text
P2-1 implementation DoD: PASS
P2-2 implementation DoD: PASS
```

。

当時は文書単体のcanonical verdictが不明確だった。この問題は旧MDを
superseded扱いにし、本書30節をcanonicalにすることで解消した。

今回さらにP2-1 runtime lifecycle残件が判明したため、section追記方式ではなく新監査MDをcanonicalにすることを推奨する。

---

# 22. Linux build evidence

現HEADの監査MDには:

```text
Linux Vulkan build / Developer Features ON:
PASS
Ubuntu VM / 2 vCPU
307/307
linked build-linux/melonPrimeDS
```

が記録されている。

ただし現HEADにはGitHub combined status entryがなく、PR-triggered workflow runも取得できなかった。

従って今回の再監査では:

```text
PASS (repository-recorded evidence)
```

までとする。

---

# 23. 残件分類（353cd23c時点。現行判定は30節）

## PASS

```text
Google core backend
Mac MoltenVK Google functional
pinned MoltenVK smoke
Formal Google M0/M1/M2
Target backend pure selector
GOOGLE + FIFO_LATEST_READY device enable
GOOGLE + FIFO_LATEST_READY pacer eligibility
GOOGLE + FIFO_LATEST_READY pure model tests
vendor ownership model
TelemetryOnly default
macOS build evidence
Linux build repository evidence
```

## P2

```text
P2-1 policy-aware lifecycle selector:
closed in 266aa7e4; physical runtime remains open
P2-2 GOOGLE + FIFO_LATEST_READY:
source/model/contract closed; physical runtime remains open
```

## P3（353cd23c時点）

```text
TargetSchedulingLifecycleFailed semantics/comment inconsistency
audit MD implementation commit SHA typo
audit MD old/new verdict contradiction
```

## NOT RUN / OPEN

```text
GOOGLE + FIFO_LATEST_READY physical hardware runtime
Windows Vulkan build/runtime
Linux native Vulkan runtime
AMD Anti-Lag 2 hardware gate
native Intel Vulkan gate
Mac full lifecycle
macOS observable validation-layer gate
```

---

# 24. 修正優先順位（353cd23c時点）

```text
1. P2 BeginFrame backend selectionをpolicy-awareへ統一
2. Google bootstrap lifecycle regression test追加
3. contract auditでBeginFrame selection driftを禁止
4. TargetSchedulingLifecycleFailed semanticsを決定・コメント/test統一
5. push済み監査MDのSHA修正
6. 旧PARTIAL / 新PASSが同居する監査文書を整理
7. macOS build
8. Linux build
9. Windows build
10. 対応hardwareでGOOGLE + FIFO_LATEST_READY runtime
```

---

# 25. 修正後DoD

## P2-1

- [x] Telemetry selectorとTarget selectorを分離
- [x] EXT target incapable + GOOGLE usableでTarget selectorはGOOGLE
- [x] EXT bootstrap時はGOOGLEへflapしない
- [x] **BeginFrameのGoogle poll backendをpolicy-aware selectorと統一**
- [x] **mixed EXT + GOOGLEでGoogle refresh bootstrapが開始される**
- [x] **EXT lifecycle failure + GOOGLEでGoogle refresh bootstrapが開始される**
- [x] policy-aware lifecycle regression coverage追加（pure model + production call-site contract）
- [x] contract auditでselector driftを検出
- [x] Reflex / Anti-Lag排他
- [x] abnormal speed target抑止
- [x] non-FIFO target抑止

## P2-2

- [x] device extension enableがEXTだけに依存しない
- [x] GOOGLE target backendでlatest-ready eligible
- [x] GOOGLEにPresentId2を不必要に要求しない
- [x] GOOGLEにEXT time-domain APIを不必要に要求しない
- [x] EXT absolute/relative path維持
- [x] model test追加
- [x] contract audit追加
- [ ] physical runtime

## Documentation

- [x] 正しいimplementation commit SHAへ修正
- [x] 旧PARTIAL判定と最新判定を整理（旧MDをsuperseded化）
- [x] `TargetSchedulingLifecycleFailed` semanticsを統一

## Regression

- [x] `vulkan-present-timing-tests` (macOS Developer Features ON/OFF)
- [x] aggregate Vulkan latency tests
- [x] low-latency contract audit
- [x] `git diff --check`
- [x] macOS Developer Features ON
- [x] macOS Developer Features OFF
- [ ] Linux Vulkan build for current 266aa7e4 (VM guestcontrol boot failure; prior 353cd23c evidence recorded)
- [ ] Windows Vulkan build
- [x] TelemetryOnly default
- [x] Reflex authority
- [x] Anti-Lag authority static contract

---

# 26. 推奨commit分割（353cd23c時点）

```text
vulkan: align Google timing polling with target backend selection

test: cover mixed EXT Google pacing lifecycle

audit: guard policy-aware Google polling

docs: correct Vulkan timing re-audit evidence
```

`TargetSchedulingLifecycleFailed` semanticsも変更するなら別commit:

```text
vulkan: clarify present timing lifecycle failure scope
```

---

# 27. 設計原則（353cd23c時点の残件分析）

今回の残件の根本原因はselector分離そのものではない。

問題は:

```text
policy resolver:
    target-aware selector

runtime feedback polling:
    telemetry selector
```

と、同一frameで異なるselectorを使っていること。

今後は:

```text
One policy
→ One selected backend
→ One lifecycle
→ One metadata path
```

を守る。

frame開始時にpolicy-selected backendを一度決め、その結果を:

```text
feedback poll
wait decision
target decision
PreparePresent
telemetry capture
```

まで共有する設計が安全。

既存`LastDecision`思想とも一致する。

---

# 28. 最終判断

`266aa7e4132e945d6e398d4d84197373e7ea4b3f`で、今回の残件だった
P2-1 runtime lifecycleを解消した。

特に:

```text
P2-2 GOOGLE + FIFO_LATEST_READY
```

はsource architectureとして正しく閉じた。

一方:

```text
P2-1 EXT → GOOGLE fallback
```

は:

```text
pure resolver:
    fixed

runtime lifecycle:
    fixed (policy-aware helper shared with BeginFrame and swapchain bootstrap)
```

。

従って現HEADの実装判定は:

```text
P2 compatibility fixes complete (source/model/contract)
```

と判定する。ただし、対応ハードウェアでのGOOGLE + FIFO_LATEST_READY
runtime、Windows build、Linux native runtime、Mac full lifecycleなどは
引き続きNOT RUN / OPENであり、実機PASSを主張しない。

正確な状態:

```text
P2-2:
    COMPLETE

P2-1:
    SELECTOR + PACER LIFECYCLE COMPLETE
```

。

次の実機確認では、GOOGLE + FIFO_LATEST_READYのruntimeと、未実行の
Windows/Linux native/Mac full lifecycle gateだけを追加確認する。

---

# 29. 参照

## Repository

- https://github.com/ag-advania/melonPrimeDS/tree/develop_remakeVulkan_ver3
- https://github.com/ag-advania/melonPrimeDS/commit/266aa7e4132e945d6e398d4d84197373e7ea4b3f
- https://github.com/ag-advania/melonPrimeDS/commit/bd375cad7c926f22b0c5a7f429e4952d8f3a3e53

## Main source

```text
src/VulkanPresentPacingPolicy.h
src/VulkanPresentPacer.cpp
src/VulkanPresentPacer.h
src/VulkanDevice.cpp
tools/testing/vulkan-present-timing-tests.cpp
tools/ci/audits/audit-low-latency-contract.py
```

## Khronos Vulkan Documentation

- https://docs.vulkan.org/refpages/latest/refpages/source/VK_GOOGLE_display_timing.html
- https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentTimeGOOGLE.html
- https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_mode_fifo_latest_ready.html
- https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html
- https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimingPropertiesEXT.html
- https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimeDomainPropertiesEXT.html

---

# 30. 修正適用後の再監査結果（canonical）

## 30.1 P2-1 lifecycle closure

実装commit `266aa7e4132e945d6e398d4d84197373e7ea4b3f`で、backend選択を
次の純粋helperへ統一した。

```text
SelectVulkanPresentBackendForPolicy(policy, capabilities)
```

`ResolveVulkanPresentPacing`と`ClassifyVulkanTargetFallback`だけでなく、
`VulkanPresentPacer::OnSwapchainCreated`と`BeginFrame`も同じpolicy-aware
selectorを使う。したがって、EXT telemetryが利用可能でもEXT targetが
不能でGOOGLEが利用可能なmixed capabilityでは、JIT policyがGOOGLEを選び、
swapchain creation時のrefresh bootstrapとframe開始時のpast-timing pollが
同じbackendで実行される。

`VulkanShouldPollGoogleForFrame`は、Reflex、Anti-Lag、異常速度を抑止しつつ、
EXT lifecycle failure後のGOOGLE fallbackも許可する。TelemetryOnlyは従来通り
EXT telemetry優先で、GOOGLE pollingを行わない。

## 30.2 regression / contract coverage

`TestPolicyAwareGooglePolling`で以下をpure modelとして固定した。

```text
JIT mixed EXT + GOOGLE             -> GOOGLE poll/bootstrap
TelemetryOnly mixed EXT + GOOGLE   -> EXT telemetry / no GOOGLE poll
EXT lifecycle failure + GOOGLE     -> GOOGLE poll/bootstrap
Reflex / Anti-Lag / abnormal speed -> no GOOGLE poll
```

`audit-low-latency-contract.py`は、production `BeginFrame`と
`OnSwapchainCreated`がpolicy-aware helperを使うこと、旧telemetry selectorを
直接使わないこと、lifecycleコメントが一致することを検査する。

## 30.3 verification status

| Gate | Result | Evidence / limitation |
|---|---|---|
| macOS Developer Features ON pure model | PASS | `melonprime_vulkan_present_timing_check`, parallel 4 |
| macOS Developer Features OFF pure model | PASS | `melonprime_vulkan_present_timing_check`, parallel 4 |
| macOS Vulkan build ON | PASS | bundle MoltenVK + ad-hoc signing |
| macOS Vulkan build OFF | PASS | release shape, bundle MoltenVK + ad-hoc signing |
| low-latency contract audit | PASS | `audit-low-latency-contract.py` |
| aggregate Vulkan latency tests | PASS | source/model aggregation |
| raster/software parity audit | PASS | static parity audit |
| `git diff --check` | PASS | clean whitespace |
| Linux Vulkan build at current `266aa7e4` | NOT RUN | Ubuntu VM guestcontrol remained unavailable during boot; prior `353cd23c` 307/307 evidence is historical |
| Windows Vulkan build | NOT RUN | no MSYS2/MinGW toolchain on this macOS host |
| GOOGLE + FIFO_LATEST_READY physical runtime | NOT RUN | no qualifying physical runtime evidence in this pass |
| Linux native / AMD / Intel / Mac full lifecycle / validation layer | NOT RUN / OPEN | hardware or validation gate remains unexecuted |

## 30.4 final status

```text
P2-1 source + pure model + production call-site contract: PASS
P2-2 source + pure model + contract: PASS
P3 lifecycle comments and audit SHA/canonical-doc state: PASS
Physical/runtime gates: NOT RUN / OPEN
```

The existing `MELONPRIME_ENABLE_DEVELOPER_FEATURES` option remains the compile
gate for the diagnostic EmuThread savestate hook and developer-only pacing
telemetry; the release build keeps it OFF. No 16x or high-parallel workload was
introduced.
