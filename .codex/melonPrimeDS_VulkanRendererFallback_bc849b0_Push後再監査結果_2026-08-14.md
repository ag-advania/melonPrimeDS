# melonPrimeDS Vulkan Renderer Fallback

## bc849b0 Push後 再監査結果

- 作成日: 2026-08-14
- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver3`
- 前回監査HEAD: `0b5b49d10eb512ff87025afe4e02463514654786`
- 今回HEAD: `bc849b0baf61db47dec8ee7657b8772b20d0c092`
- HEAD commit: `Complete Vulkan renderer fallback re-audit`
- 前回HEADから: `1 commit ahead / 0 behind`
- 前回P3-1: **CLOSED**
- 前回P3-2: **CLOSED**
- 前回P4-1: **CLOSED**
- 前回P4-2: **CLOSED**
- 新規P2 production defect: **なし**
- 新規P3 production defect: **なし**
- 新規P4 hardening: **2件**
- GitHub-hosted CI: **NO STATUS / NO WORKFLOW RUN**
- Windows current-source local configure/build: repository evidence上 **PASS**
- Windows forced-failure / panel-lifetime stress: repository evidence上 **PASS**
- macOS Khronos validation: 既存repository evidence上 **PASS**
- Linux physical Vulkan: **NOT VERIFIED**
- Windows/Linux Khronos validation: **NOT VERIFIED**
- physical `VK_EXT_present_timing`: **NOT VERIFIED**
- 総合判定:

```text
PASS

PREVIOUS P3/P4 FINDINGS CLOSED

NO NEW P2/P3 PRODUCTION DEFECT FOUND

WITH 2 NEW P4 HARDENING / EVIDENCE-QUALITY FINDINGS

GITHUB-HOSTED CI AND EXTERNAL PLATFORM/GPU GATES REMAIN OPEN
```

---

# 1. 結論

前回監査HEAD:

```text
0b5b49d10eb512ff87025afe4e02463514654786
Guard renderer fallback panel lifetime
```

から現在HEAD:

```text
bc849b0baf61db47dec8ee7657b8772b20d0c092
Complete Vulkan renderer fallback re-audit
```

までを再監査した。

差分は1 commitのみ。

今回のcommitは前回指摘した:

```text
P3-1
Window.cpp共通sourceのMelonPrime専用mutex変更に
#ifdef MELONPRIME_DSがない

P3-2
forced Vulkan runtime failure / panel lifetime修正に
専用regression executionがない

P4-1
audit READMEのHEAD/status provenanceが古い

P4-2
vulkan-backend.mdのKhronos validation状態が古い
```

に直接対応している。

結果:

```text
P3-1:
    CLOSED

P3-2:
    CLOSED

P4-1:
    CLOSED

P4-2:
    CLOSED
```

と判定してよい。

特にP3-2については:

```text
GPU-independent selector regression test

+

developer-only one-shot runtime failure injection

+

production GUI/panel transition stress

+

20 sec process liveness

+

40/40 renderer switches
```

の二段構えになった。

今回のproduction差分を再監査した範囲で:

```text
新規P2:
    NONE FOUND

新規P3 production:
    NONE FOUND
```

である。

ただしdeveloper-only test seamと監査証跡について新規P4を2件残す。

```text
P4-1
one-shot failure injectionのstatic boolが
multi-instance EmuThread間で非atomic共有になる

P4-2
committed fallback stress logがderived summaryのみで、
元combined.logがTemp path参照のまま
```

どちらもshipping rendererの機能欠陥ではない。

---

# 2. Push確認

current branch:

```text
develop_remakeVulkan_ver3
```

current HEAD:

```text
bc849b0baf61db47dec8ee7657b8772b20d0c092
Complete Vulkan renderer fallback re-audit
```

parent:

```text
0b5b49d10eb512ff87025afe4e02463514654786
Guard renderer fallback panel lifetime
```

compare:

```text
status:
    ahead

ahead_by:
    1

behind_by:
    0

total_commits:
    1
```

**判定: Push確認 PASS**

---

# 3. 今回の変更ファイル

前回HEADからの差分:

```text
.codex/
    melonPrimeDS_VulkanPresentTiming_0b5b49d_Push後再監査結果_2026-08-14.md
        added

    melonPrimeDS_VulkanPresentTiming_5c05d249_Push後再監査結果_2026-08-14.md
        removed

docs/archive/audits/vulkan/2026-08-14-present-pacer-dispatch/
    README.md
        modified

    fallback-stress-runtime.log
        added

docs/features/rendering/
    vulkan-backend.md
        modified

src/frontend/qt_sdl/
    CMakeLists.txt
        modified

    EmuThread.cpp
        modified

    Window.cpp
        modified

tools/testing/
    vulkan-renderer-fallback-stress.ps1
        added

    vulkan-renderer-fallback-tests.cpp
        added
```

production挙動へ影響する差分:

```text
EmuThread.cpp
Window.cpp
```

test/build wiring:

```text
CMakeLists.txt
vulkan-renderer-fallback-tests.cpp
vulkan-renderer-fallback-stress.ps1
```

documentation/evidence:

```text
README.md
vulkan-backend.md
fallback-stress-runtime.log
```

---

# 4. 前回P3-1再監査

前回の問題:

```cpp
void MainWindow::osdAddMessage(...)
{
    if (!showOSD) return;

    QMutexLocker panelLock(&screenPanelLock);
    if (panel)
        panel->osdAddMessage(color, msg);
}
```

この変更理由は:

```text
MelonPrime Vulkan/DX12 renderer fallback中の
cross-thread panel lifetime保護
```

であるにもかかわらず、melonDS共通sourceへ無条件で入っていた。

今回HEAD:

```cpp
void MainWindow::osdAddMessage(unsigned int color, const char* msg)
{
    if (!showOSD) return;

#ifdef MELONPRIME_DS
    // EmuThread can report a renderer failure while the GUI thread is
    // replacing the presentation panel.
    QMutexLocker panelLock(&screenPanelLock);
    if (panel)
        panel->osdAddMessage(color, msg);
#else
    panel->osdAddMessage(color, msg);
#endif
}
```

となった。

つまり:

```text
MelonPrime build:
    screenPanelLock + null guard

upstream/non-MelonPrime build:
    original behavior
```

へ分離されている。

これはmelonPrimeDSのcode-boundary contractに合う。

**判定: P3-1 CLOSED**

---

# 5. OSD panel lifetime契約の維持

MelonPrime path:

```text
osdAddMessage()
    ↓
screenPanelLock
    ↓
if (panel)
    panel->osdAddMessage()
```

panel teardown:

```text
destroyScreenPanel()
    ↓
screenPanelLock
    ↓
oldPanel = panel
    ↓
panel = nullptr
    ↓
beginClose()
    ↓
delete
```

よって:

```text
OSD first:
    OSD call完了
    ↓
unlock
    ↓
destroy

destroy first:
    panel = nullptr
    ↓
delete完了
    ↓
unlock
    ↓
OSD sees nullptr
```

になる。

前回確認したlifetime fixは維持されたまま、
upstream codeだけ元のbehaviorへ戻した。

**判定: PASS**

---

# 6. 前回P3-2の要求

前回は:

```text
0b5b49dのsource fixは静的には妥当

しかし:
    forced Vulkan failure
    runtime failure latch
    OSD
    rendererRuntimeFallback
    panel teardown/rebuild
    forced env re-selection prevention
```

を実プロセスで一続きに検証する専用gateがなかった。

要求していたminimum flow:

```text
MELONPRIME_FORCE_VULKAN_RENDERER=1

Vulkan runtime failure
    ↓
ReportRuntimeFailure
    ↓
OSD
    ↓
rendererRuntimeFallback
    ↓
panel teardown/rebuild
    ↓
NormalizeRendererForPlatform
    ↓
Vulkan再選択禁止
```

今回これが追加された。

**判定: P3-2 CLOSED**

---

# 7. GPU-independent selector regression test

新規:

```text
tools/testing/vulkan-renderer-fallback-tests.cpp
```

このtestは実GPUへ依存しない。

`VulkanFeatureCheck`だけをdeterministic test double化して:

```text
runtime available:
    true

forced Vulkan env:
    1
```

のとき:

```text
Software request
    ↓
forced Vulkan
```

になることを確認。

その後:

```cpp
ReportRuntimeFailure("test injected Vulkan runtime failure");
```

で:

```text
runtime available:
    false
```

へ変更。

その状態で:

```text
persisted Vulkan renderer
    -> Software

forced env + Software request
    -> Software

ResolvePresentationBackend(false, Software)
    -> NativeQt
```

を確認する。

重要なのは:

```text
MELONPRIME_FORCE_VULKAN_RENDERER
```

が存在していても:

```text
runtime availability latch = false
```

なら再度Vulkanを選ばないこと。

**判定: PASS**

---

# 8. selector testのproduction code利用

test targetは:

```text
vulkan-renderer-fallback-tests.cpp

+

MelonPrimeVideoBackend.cpp
```

を直接compileする。

つまり:

```text
NormalizeRendererForPlatform()
ResolvePresentationBackend()
```

はproduction sourceそのもの。

test doubleは:

```text
VulkanFeatureCheck::IsRuntimeAvailable()
VulkanFeatureCheck::ReportRuntimeFailure()
```

のみに限定。

そのため:

```text
production selector logicをコピーした偽物
```

をtestしているのではない。

**判定: PASS**

---

# 9. CMake wiring

developer + Vulkan active時:

```cmake
add_executable(
    melonprime_vulkan_renderer_fallback_tests
    vulkan-renderer-fallback-tests.cpp
    MelonPrimeVideoBackend.cpp)
```

definitions:

```text
MELONPRIME_DS
MELONPRIME_ENABLE_VULKAN=1
MELONPRIME_ENABLE_DEVELOPER_FEATURES=1
OGLRENDERER_ENABLED
SDL_MAIN_HANDLED
VK_NO_PROTOTYPES=1
```

custom target:

```text
melonprime_vulkan_renderer_fallback_check
```

が`ALL`へ追加され、build時にtest executableを実行する。

release-features OFFではtest seamそのものが対象外。

**判定: PASS**

---

# 10. developer-only one-shot runtime failure seam

`EmuThread::updateRenderer()`のVulkan caseへ:

```cpp
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    static bool injectedVulkanFailure = false;

    const char* forceVulkanFailure =
        std::getenv(
            "MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE");

    const bool injectVulkanFailure =
        !injectedVulkanFailure &&
        forceVulkanFailure &&
        forceVulkanFailure[0] != '\0' &&
        forceVulkanFailure[0] != '0';

    if (injectVulkanFailure)
    {
        injectedVulkanFailure = true;

        Platform::Log(
            Platform::LogLevel::Error,
            "[fallback-test] forced Vulkan runtime failure injection count=1\n");

        nds->SetRenderer(
            std::make_unique<SoftRenderer>(*nds));
    }
    else
#endif
    {
        nds->SetRenderer(
            std::make_unique<VulkanRenderer>(*nds));
    }
```

が追加された。

外側は:

```text
MELONPRIME_DS
MELONPRIME_ENABLE_VULKAN
```

Vulkan case内。

注入点はさらに:

```text
MELONPRIME_ENABLE_DEVELOPER_FEATURES
```

でgate。

従ってshipping release buildには入らない。

**判定: PASS**

---

# 11. failure seamがproduction fallbackを迂回していないか

test seamはfailure時に:

```text
SoftRendererを意図的にセット
```

するだけ。

その直後のproduction code:

```cpp
if (dynamic_cast<VulkanRenderer*>(
        &nds->GetRenderer()) == nullptr)
{
    VulkanFeatureCheck::ReportRuntimeFailure(...);

    Log(
        "Renderer fallback requested=Vulkan actual=Software ...");

    emuInstance->osdAddMessage(...);

    videoRenderer = renderer3D_Software;

    emit rendererRuntimeFallback();
}
```

がそのまま実行される。

つまりtest seam自身が:

```text
runtime failure latch
OSD
renderer fallback signal
videoRenderer state
GUI transition
```

を直接偽装していない。

実際のproduction failure branchを起動するtriggerだけを作っている。

**判定: PASS**

---

# 12. process-level stress harness

新規:

```text
tools/testing/vulkan-renderer-fallback-stress.ps1
```

環境:

```text
MELONPRIME_FORCE_VULKAN_RENDERER=1

MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE=1

MELONPRIME_RENDERER_SWITCH_STRESS=1,0

MELONPRIME_RENDERER_SWITCH_STRESS_ITERATIONS=20

MELONPRIME_RENDERER_SWITCH_STRESS_INTERVAL_MS=100
```

timeout/liveness:

```text
20 sec
```

renderer switch count:

```text
2 steps x 20 iterations
=
40 switches
```

を要求する。

---

# 13. stress harnessのassert

harnessはcaptured stdout/stderrへregexを適用する。

要求:

```text
[fallback-test] forced Vulkan runtime failure injection count=1
    exactly 1

[Vulkan] runtime failure reported:
    exactly 1

Renderer fallback requested=Vulkan actual=Software
    exactly 1

Renderer selection requested=Vulkan presentation=Vulkan
    exactly 1

[switch-stress] complete: 40/40 switches performed
    exactly 1
```

さらに:

```text
processが20秒deadlineより前に終了しない

QFATALなし

ASSERT FAILEDなし

segmentation faultなし

access violationなし

unhandled exceptionなし
```

を確認する。

これは前回要求した:

```text
single fallback
no Vulkan re-selection loop
panel transition liveness
crash/UAF symptomsなし
```

を実プロセスで検査する。

**判定: PASS**

---

# 14. renderer switch stressがproduction GUI pathを通るか

既存:

```text
MelonPrimeRendererSwitchStress
```

はGUI thread上で:

```text
Config 3D.Renderer変更
    ↓
ResolvePresentationBackend()
    ↓
backendChanged計算
    ↓
QMetaObject::invokeMethod(
        MainWindow,
        "onUpdateVideoSettings",
        Qt::DirectConnection,
        backendChanged)
```

を実行する。

`backendChanged == true`なら:

```text
onUpdateVideoSettings(true)
    ↓
emuPause()
    ↓
destroyScreenPanel()
    ↓
prepare renderer transition
    ↓
createScreenPanel()
```

という実UI transitionを使う。

従って40/40 switch completionは:

```text
単なるconfig field書換え
```

ではない。

**判定: PASS**

---

# 15. committed runtime evidence

前回の実行サマリーとして追加された証跡:

```text
docs/archive/audits/vulkan/
2026-08-14-present-pacer-dispatch/
fallback-stress-runtime.log
```

記録:

```text
source_audit_head:
    0b5b49d10eb512ff87025afe4e02463514654786

build:
    build/release-mingw-x86_64

mode:
    Release
    Vulkan=ON
    DX12=ON
    developer_features=ON

result:
    PASS

forced injection:
    1

runtime failure report:
    1

Vulkan selection:
    1

Vulkan -> Software fallback:
    1

renderer switches:
    40/40

process liveness:
    20s

fatal diagnostics:
    0
```

このコミット済みサマリーは、follow-up sourceをcommitする前の実行として
`source_audit_head=0b5b49d...`を記録している。今回の再監査では、current
HEADを確認した後に同じ実ROM stressを再実行した。

```text
tested_head:
    bc849b0baf61db47dec8ee7657b8772b20d0c092

working_tree_at_run:
    non-clean (監査ファイルの追加/旧監査ファイルの削除のみ)

raw_combined_log:
    C:\Users\Admin\AppData\Local\Temp\melonprime-vulkan-fallback-a252b0fb4d574c49be1c9c90d404e983\combined.log

forced injection:
    1

runtime failure report:
    1

Vulkan selection:
    1

Vulkan -> Software fallback:
    1

renderer switches:
    40/40

process liveness:
    20s

fatal diagnostics:
    0
```

**判定: repository evidence PASS**

---

# 16. Windows current-source build evidence

updated audit READMEは:

```text
tools/build/windows/build-mingw.bat
    --jobs 1
    --tail 220
```

でreconfigure/buildしたと記録している。

configuration:

```text
Vulkan ON
DX12 ON
developer-features ON
Release
```

PASS対象:

```text
current source compilation

pure Vulkan timing test

production fake-dispatch test

renderer-fallback test

XeLL state-machine test
```

したがって前回の:

```text
latest 0b5 source compile evidenceなし
```

というgapは解消したと判定できる。

**判定: PASS evidence**

---

# 17. 前回P4-1 README HEAD/status drift

READMEは以前:

```text
Current repository HEAD:
    fd8e5c69
```

としていた。

今回:

```text
Present-pacer audited source HEAD:
    fd8e5c69e

Latest repository HEAD at the
renderer-fallback re-audit start:
    0b5b49d10

0b5b49d renderer-fallback follow-up:
    separately recorded
```

へ変更。

つまり:

```text
present-pacer evidence

renderer fallback follow-up
```

を別のsource generationとして扱う。

これは前回要求したprovenance分離そのもの。

**判定: 前回P4-1 CLOSED**

---

# 18. Windows evidence記述の整理

READMEは現在:

```text
current Windows host
build-mingw.bat
current source compiled
```

としつつ:

```text
Windows Khronos validation:
    unverified
```

を明確に分離している。

以前の:

```text
Windows compilation still requires environment
```

という矛盾は解消。

**判定: PASS**

---

# 19. 前回P4-2 validation-layer documentation drift

`docs/features/rendering/vulkan-backend.md`は現在:

```text
macOS Intel/MoltenVK no-bundle Debug F2:
    VK_LAYER_KHRONOS_validation
    60 sec
    VUID 0
    validation ERROR 0

Windows Khronos validation:
    unverified

Linux Khronos validation:
    unverified
```

とplatform-scopedに記述。

以前の:

```text
Khronos validation-layer coverage remain unverified
```

というglobalな未検証表現は修正された。

**判定: 前回P4-2 CLOSED**

---

# 20. macOS validationの現在の位置付け

既存証跡:

```text
Intel Iris Plus Graphics 655

MoltenVK

Homebrew Vulkan loader

VK_LAYER_KHRONOS_validation

F2 state loaded

Vulkan actual renderer

60 sec

VUID 0

validation ERROR 0
```

は引き続き有効。

ただし今回追加された:

```text
EmuThread developer failure seam
Window ifdef整理
```

はWindows stress/current-source buildで確認されているため、

macOSの過去validation evidenceと今回Windows follow-upを混ぜないREADME構成になっている。

**判定: evidence separation PASS**

---

# 21. production release behaviorへの影響

新規failure injectionは:

```cpp
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
```

内。

したがってrelease-features OFFでは:

```text
MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE
```

というenvを設定してもcode自体が存在しない。

通常Vulkan creation:

```cpp
nds->SetRenderer(
    std::make_unique<VulkanRenderer>(*nds));
```

は元のpath。

shipping hot pathへ:

```text
extra atomic
mutex
virtual call
heap allocation
env check
```

を追加していない。

Window OSD mutexは既存0b5 fixをguardしただけ。

**判定: PASS**

---

# 22. forced Vulkan runtime latch回帰

0b5修正:

```cpp
if (forceVulkan &&
    forceVulkan[0] == '1' &&
    VulkanFeatureCheck::IsRuntimeAvailable())
{
    return renderer3D_Vulkan;
}
```

は今回変更されていない。

selector testで:

```text
available:
    forced Vulkan

failure:
    Software

failure + force env:
    Software
```

を固定。

process stressで:

```text
Vulkan selection = 1
Vulkan fallback = 1
```

を固定。

**判定: PASS**

---

# 23. panel lifetime回帰

Window:

```text
MelonPrime:
    lock + null guard

upstream:
    original direct call
```

process stress:

```text
forced failure
    ↓
failure OSD
    ↓
fallback signal
    ↓
panel transition

+

40 panel/backend switches

+

20 sec liveness
```

でfatal diagnostic 0。

前回のstatic-only状態からexecution coverageへ進んだ。

**判定: P3 CLOSED**

---

# 24. GitHub-hosted CI

current HEAD:

```text
bc849b0baf61db47dec8ee7657b8772b20d0c092
```

GitHub combined status:

```text
statuses:
    []
```

commit-associated workflow runs:

```text
workflow_runs:
    []
```

よって:

```text
GitHub-hosted CI:
    NOT VERIFIED
```

local Windows build/stress evidenceと区別する。

---

# 25. 新規P4-1: developer failure seamのmulti-instance data race

今回追加:

```cpp
static bool injectedVulkanFailure = false;
```

は:

```text
EmuThread::updateRenderer()
```

内のfunction-local static。

これは:

```text
process全体で共有
```

される。

MelonPrimeDSはmultiple EmuInstanceを持てるため、explicit test env:

```text
MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE=1
```

を有効にした状態で複数EmuThreadが同時にVulkan `updateRenderer()`へ入ると:

```text
thread A:
    read injectedVulkanFailure

thread B:
    read injectedVulkanFailure

thread A:
    write true

thread B:
    write true
```

が非atomicに発生し得る。

C++ memory model上、これはdata raceになり得る。

重要:

```text
shipping release:
    影響なし

normal developer run without TEST env:
    実質影響なし

explicit diagnostic env + multi-instance:
    hardening gap
```

である。

分類:

```text
P4
DEVELOPER-ONLY TEST-SEAM CONCURRENCY
OPEN
```

推奨:

process-wide one-shotを維持するなら:

```cpp
static std::atomic_bool injectedVulkanFailure{false};

bool expected = false;
if (envEnabled &&
    injectedVulkanFailure.compare_exchange_strong(
        expected, true,
        std::memory_order_relaxed))
{
    inject;
}
```

または:

```text
各EmuInstanceごとに1回
```

が目的ならinstance-owned stateへ移す。

現在stress scriptが期待する:

```text
exactly 1 process-wide injection
```

と整合するのはatomic one-shot。

---

# 26. P4-1をP2/P3としない理由

このcodeは:

```text
MELONPRIME_ENABLE_DEVELOPER_FEATURES
```

かつ:

```text
MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE
```

を明示設定した場合のみinjectする。

shipping renderer:

```text
影響なし
```

通常developer runtime:

```text
env未設定ならinjectなし
```

今回検証したsingle-instance stress:

```text
PASS
```

よってproduction severityではない。

ただしtest infrastructure自身が:

```text
multiple EmuInstance
```

でUBを持つ可能性は残さない方がよい。

---

# 27. 新規P4-2: raw stress evidence未archive

committed:

```text
fallback-stress-runtime.log
```

は有用だがderived summary。

今回の再実行でregex判定へ使った:

```text
stdout
stderr
combined.log
```

は `C:\Users\Admin\AppData\Local\Temp\melonprime-vulkan-fallback-a252b0fb4d574c49be1c9c90d404e983\combined.log`
に存在するが、repositoryへarchiveされていない。

現repositoryから独立再監査できるのは:

```text
stress harness source

+

derived counters summary
```

まで。

例えば後から:

```text
Renderer transition begin/complete全列

OSD timing

switch order

warning

non-fatal renderer diagnostics
```

を再確認することはできない。

分類:

```text
P4
AUDIT EVIDENCE REPRODUCIBILITY
OPEN
```

推奨:

```text
docs/archive/audits/vulkan/
2026-08-14-present-pacer-dispatch/
fallback-stress-combined.log
```

としてsanitize後のraw combined logを保存する。

最低でも:

```text
all fallback-test lines
all runtime failure lines
all renderer selection/fallback/transition lines
all switch-stress lines
fatal diagnostic search result
```

を抜粋したdeterministic logをcommitする。

---

# 28. source_audit_headの扱い

committed runtime summaryは:

```text
source_audit_head=0b5b49d...
```

としている。

現在closure commitは:

```text
bc849b0...
```

である。

今回の再実行では:

```text
git HEAD:
    bc849b0baf61db47dec8ee7657b8772b20d0c092

tested_head:
    bc849b0baf61db47dec8ee7657b8772b20d0c092

working tree:
    監査ファイルの追加/旧監査ファイルの削除のみdirty

renderer source:
    HEADと一致
```

したがって今回のruntime resultは、bc849b0のrenderer sourceに対する
current-head local evidenceとして扱える。ただしraw log自体は一時領域にあり、
P4-2の再現性gapは残る。今後は:

```text
git rev-parse HEAD
git diff --stat
git diff --check
git status --porcelain
```

をtest logへ記録するとよい。

---

# 29. current Windows build evidenceの評価

READMEと今回の再実行結果の:

```text
current source compiled
fallback test PASS
```

は今回のclosure判定に使える。公式Windows build entry pointとcurrent-head
stressをこの監査中にも再実行し、fallback selector、timing、fake-dispatch、
XeLLの各テストと40/40 switch・20秒livenessを確認した。

ただしGitHub connectorからWindows commandを再実行したわけではない。

本監査では:

```text
repository-authored local evidence
```

として扱う。

GitHub-hosted CIとは区別。

---

# 30. current commitのtest coverage matrix

| 対象 | Coverage |
|---|---|
| forced Vulkan available | C++ selector test |
| forced Vulkan after runtime failure | C++ selector test |
| persisted Vulkan after failure | C++ selector test |
| presentation fallback NativeQt | C++ selector test |
| real `ReportRuntimeFailure` | process stress |
| real failure OSD path | process stress |
| real `rendererRuntimeFallback` | process stress |
| real panel teardown/rebuild | process stress |
| Vulkan re-selection loop | process stress |
| 40 renderer switches | process stress |
| 20s liveness | process stress |
| obvious crash/fatal diagnostics | process stress |
| multi-instance injection | NOT TESTED |
| GitHub CI | NOT RUN |
| Windows validation layer | NOT RUN |
| Linux validation layer | NOT RUN |

---

# 31.前回Definition of Doneとの照合

前回要求:

```text
[ ] Window.cpp共通コード境界を整理
```

今回:

```text
[x] CLOSED
```

前回要求:

```text
[ ] forced Vulkan failure regression test
```

今回:

```text
[x] CLOSED
```

前回要求:

```text
[ ] fallback signal count assert
```

今回:

```text
[x] exactly one fallback log
```

前回要求:

```text
[ ] no repeated Vulkan selection assert
```

今回:

```text
[x] Vulkan selection exactly one
```

前回要求:

```text
[ ] panel teardown/rebuild stress
```

今回:

```text
[x] 40/40 renderer switches
```

前回要求:

```text
[ ] process alive assert
```

今回:

```text
[x] 20 sec
```

前回要求:

```text
[ ] current source compile PASS
```

今回:

```text
[x] Windows current-source local build evidence
```

前回要求:

```text
[ ] audit README HEAD更新
```

今回:

```text
[x] evidence generations separated
```

前回要求:

```text
[ ] vulkan-backend validation status更新
```

今回:

```text
[x] platform-scoped
```

**前回DoDは満たしたと判定する。**

---

# 32. 新規production regression確認

今回変更したproduction sourceは:

```text
Window.cpp:
    ifdef追加のみ

EmuThread.cpp:
    developer-only fault injectionのみ
```

release production behavior:

```text
0b5b49dと実質同じ
```

VulkanPresentPacer:

```text
変更なし
```

Vulkan renderer shader/raster/compositor:

```text
変更なし
```

DX12:

```text
直接変更なし
```

OpenGL:

```text
直接変更なし
```

Software:

```text
直接変更なし
```

Metal:

```text
直接変更なし
```

**新規P2/P3 production defectは確認しなかった。**

---

# 33. future regression禁止事項

次を維持する。

```text
Window::osdAddMessageのpanel lifetime mutex:
    MELONPRIME_DS内

upstream path:
    original behavior維持

MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE:
    developer-only

failure seam:
    production failure branchを迂回しない

MELONPRIME_FORCE_VULKAN_RENDERER:
    runtime availability latchを尊重

ReportRuntimeFailure:
    first failure sticky

rendererRuntimeFallback:
    GUI transition authority

screenPanelLock:
    panel ownership authority
```

test seamを理由にshipping pathへ:

```text
per-frame env check
atomic
lock
virtual abstraction
sleep
```

を追加しない。

---

# 34. P4-1修正推奨

推奨最小差分:

```cpp
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    static std::atomic_bool injectedVulkanFailure{false};

    const char* forceVulkanFailure =
        std::getenv(
            "MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE");

    const bool enabled =
        forceVulkanFailure &&
        forceVulkanFailure[0] != '\0' &&
        forceVulkanFailure[0] != '0';

    bool expected = false;
    const bool injectVulkanFailure =
        enabled &&
        injectedVulkanFailure.compare_exchange_strong(
            expected,
            true,
            std::memory_order_relaxed);
#endif
```

include:

```cpp
#include <atomic>
```

ただしdeveloper-only sourceのため、可能なら:

```text
専用helper
```

へ切り出しhot production sourceを散らかさない方がよい。

---

# 35. P4-1 test

追加するなら:

```text
2 EmuInstance
2 EmuThread
forced Vulkan test env ON
```

で:

```text
injection count:
    exactly 1
```

を確認。

atomic one-shotにすればdeterministic。

ただし今回P3 closureを取り消す必要はない。

これはtest seam hardening。

---

# 36. P4-2 evidence推奨

stress scriptにoption:

```text
-OutLog
```

を追加し、

```text
repository-relative archive path
```

を明示可能にする。

またはscript終了時:

```text
Write-Host SHA256(combined.log)
```

を記録。

commit evidence:

```text
tested_commit
working_tree_state
command
environment
ROM hash
combined log hash
PASS counters
```

を一つにまとめる。

---

# 37. platform validation matrix

| 項目 | 判定 |
|---|---|
| current branch HEAD | PASS |
| previous P3-1 | CLOSED |
| previous P3-2 | CLOSED |
| previous P4-1 | CLOSED |
| previous P4-2 | CLOSED |
| current Windows local configure/build | PASS evidence |
| fallback selector test | PASS evidence |
| forced failure process stress | PASS evidence |
| 40/40 switch stress | PASS evidence |
| 20s process liveness | PASS evidence |
| macOS Khronos validation | PASS previous evidence |
| Windows Khronos validation | NOT VERIFIED |
| Linux Khronos validation | NOT VERIFIED |
| Linux physical Vulkan | NOT VERIFIED |
| AMD physical Vulkan | NOT VERIFIED |
| Intel non-MoltenVK physical | NOT VERIFIED |
| physical `VK_EXT_present_timing` | NOT VERIFIED |
| GitHub-hosted CI | NO RUN |
| cross-GPU endurance/parity | NOT VERIFIED |

---

# 38. Severity summary

```text
P0:
    none

P1:
    none

P2:
    none found

P3 production:
    none found

Previous P3:
    all CLOSED

P4:
    2 OPEN
```

P4 details:

```text
P4-A
developer-only test injection static bool
multi-instance atomicity

P4-B
stress raw evidence archive / commit binding
```

---

# 39. 今回の総合評価

今回commitは前回監査に対するfollow-upとして質が高い。

理由:

```text
指摘されたifdef:
    直接修正

指摘されたregression gap:
    unitだけでなくprocess stress追加

指摘されたdocs drift:
    platform scopeを分離

source fix:
    release behaviorを広げていない

stress:
    production GUI transition pathを利用

failure:
    production fallback branchを利用
```

特に:

```text
fake unit testだけ追加してCLOSED
```

ではなく:

```text
real process
real ROM
real Vulkan initial selection
real fallback signal
real panel transition
40 switches
20 sec liveness
```

まで進めた点は妥当。

---

# 40. 残る主要platform gap

今回のsource/fallback監査を離れると、主な未検証は:

```text
Windows Khronos validation layer

Linux Khronos validation layer

Linux physical Vulkan

AMD physical Vulkan

physical VK_EXT_present_timing lifecycle

cross-GPU endurance/parity
```

である。

macOS Intel/MoltenVK/GOOGLE timingは既存証跡あり。

Windows forced fallbackは今回証跡あり。

---

# 41. 次回Pushで見るべき点

今回P4を直すなら:

```text
1.
injectedVulkanFailureをatomic化
またはinstance-owned化

2.
multi-instance developer test

3.
raw fallback stress logをarchive

4.
tested HEAD / clean working treeをlogへ記録

5.
GitHub-hosted CIまたは別platform validation
```

P4を直さずplatform validationへ進む場合は:

```text
Windows validation layer

Linux physical Vulkan

Linux validation layer

AMD runtime
```

の順が有効。

---

# 42. 最終判定

current HEAD:

```text
bc849b0baf61db47dec8ee7657b8772b20d0c092
Complete Vulkan renderer fallback re-audit
```

判定:

```text
Push:
    PASS

previous P3-1:
    CLOSED

previous P3-2:
    CLOSED

previous P4-1:
    CLOSED

previous P4-2:
    CLOSED

forced Vulkan selector regression:
    PASS

real runtime failure latch:
    PASS evidence

Vulkan selection once:
    PASS evidence

Vulkan -> Software fallback once:
    PASS evidence

panel/backend switch:
    40/40 PASS evidence

process liveness:
    20s PASS evidence

fatal diagnostics:
    0

current-source Windows build:
    PASS evidence

new P2 production defect:
    NONE FOUND

new P3 production defect:
    NONE FOUND
```

新規P4:

```text
P4-A
developer-only one-shot bool atomicity
OPEN

P4-B
raw stress evidence reproducibility
OPEN
```

最終:

```text
PASS

PREVIOUS P3/P4 FINDINGS ARE CLOSED

NO NEW P2/P3 PRODUCTION DEFECT FOUND

2 NON-BLOCKING P4 HARDENING ITEMS REMAIN

GITHUB-HOSTED CI / EXTERNAL PLATFORM VALIDATION REMAIN OPEN
```

---

# 43. GitHub参照

Branch:

```text
https://github.com/ag-advania/melonPrimeDS/tree/develop_remakeVulkan_ver3
```

Current HEAD:

```text
https://github.com/ag-advania/melonPrimeDS/tree/bc849b0baf61db47dec8ee7657b8772b20d0c092
```

Current commit:

```text
https://github.com/ag-advania/melonPrimeDS/commit/bc849b0baf61db47dec8ee7657b8772b20d0c092
```

主要source:

```text
src/frontend/qt_sdl/Window.cpp
src/frontend/qt_sdl/EmuThread.cpp
src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp
src/frontend/qt_sdl/MelonPrimeVulkanFeatureCheck.cpp
src/frontend/qt_sdl/MelonPrimeRendererSwitchStress.cpp
src/frontend/qt_sdl/CMakeLists.txt
```

test:

```text
tools/testing/vulkan-renderer-fallback-tests.cpp
tools/testing/vulkan-renderer-fallback-stress.ps1
tools/testing/vulkan-present-pacer-dispatch-tests.cpp
```

evidence:

```text
docs/archive/audits/vulkan/
2026-08-14-present-pacer-dispatch/
README.md

docs/archive/audits/vulkan/
2026-08-14-present-pacer-dispatch/
fallback-stress-runtime.log

docs/features/rendering/vulkan-backend.md
```

---

# 44. 次回再監査の最小チェック

```text
[ ] branch HEAD確認

[ ] atomic one-shot確認

[ ] release compile pathにtest seamなし

[ ] multi-instance test結果

[ ] raw stress evidence archive

[ ] tested_head == commit HEAD

[ ] working tree clean evidence

[ ] CI status確認
```

今回のrenderer fallback root causeについては、
shipping production sourceの観点ではCLOSEDとして扱ってよい。
