# melonPrimeDS Vulkan Present Timing / Renderer Fallback

## 0b5b49d Push後 再監査結果

- 作成日: 2026-08-14
- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver3`
- 前回監査HEAD: `5c05d249c27cc11d667349bfccae763a941d3ece`
- 今回HEAD: `0b5b49d10eb512ff87025afe4e02463514654786`
- HEAD commit: `Guard renderer fallback panel lifetime`
- 前回HEADから: `5 commits ahead / 0 behind`
- 前回P4 3件: **CLOSED**
- macOS Khronos validation layer: **PASS evidence追加**
- Windows Vulkan build: **current-source configure/build PASS evidence**
- Windows forced-failure/panel-lifetime stress: **PASS evidence追加**
- 今回新規P2 functional defect: **なし**
- 今回新規P3: **2件 → CLOSED**
- 今回新規P4: **2件 → CLOSED**
- GitHub-hosted CI: **NO STATUS / NO WORKFLOW RUN**
- 総合判定:

```text
PASS FOR THE 0b5b49d SOURCE FIX AND FOLLOW-UP RE-AUDIT

P3/P4 FINDINGS CLOSED WITH CURRENT WINDOWS LOCAL EVIDENCE

GITHUB-HOSTED CI REMAINS NOT VERIFIED

EXTERNAL GPU/PLATFORM VALIDATION GATES REMAIN OUT OF SCOPE
```

---

# 1. 結論

前回監査HEAD:

```text
5c05d249c27cc11d667349bfccae763a941d3ece
Add Vulkan present pacer dispatch coverage
```

から今回HEAD:

```text
0b5b49d10eb512ff87025afe4e02463514654786
Guard renderer fallback panel lifetime
```

までを再監査した。

前回残していたP4:

```text
P4-1
audit READMEのcommit provenance不整合

P4-2
vulkan-backend.mdのIntel/macOS/MoltenVK検証状態ドリフト

P4-3
queue-pressure recoveryの証跡不足
```

については、今回のcommit列で実質的にCLOSEDしている。

特に:

```text
queue pressure
    ↓
completed timing report
    ↓
queue drain
    ↓
queue size 16 → 32
    ↓
TimingQueueRecoveries = 1
    ↓
timing metadata re-enable
```

をproduction `VulkanPresentPacer.cpp`経由で直接assertするfake-dispatch testが追加された。

さらに:

```text
GetPhysicalDeviceSurfaceCapabilities2KHR failure
    ↓
legacy GetPhysicalDeviceSurfaceCapabilitiesKHR
```

のfallback testも追加された。

macOSでは:

```text
VK_LAYER_KHRONOS_validation
Homebrew Vulkan loader
MoltenVK ICD
Intel Iris Plus Graphics 655
Vulkan actual renderer
F2 state loaded
60秒実行
VUID 0
validation ERROR 0
```

まで実施した証跡が追加されている。

一方、前回監査後の最後のcommit:

```text
0b5b49d
Guard renderer fallback panel lifetime
```

でproduction sourceが新たに2ファイル変更されている。

```text
src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp
src/frontend/qt_sdl/Window.cpp
```

この修正を静的に監査した結果:

```text
forced Vulkan失敗後の再強制ループ防止:
    設計妥当

panel破棄中OSDのnull/UAF防止:
    設計妥当

明確な新規P2 runtime defect:
    見つからず
```

ただし以下2点をP3として残す。

```text
P3-1
MelonPrime固有のrenderer fallback race修正を
共通 Window.cpp の osdAddMessage()へ
#ifdef MELONPRIME_DS なしで追加している

P3-2
0b5b49dの2-file production source差分そのものに対する
専用regression testおよびpost-commit build/runtime evidenceがない
```

加えてaudit/documentation側にP4を2件確認した。

```text
P4-1
present-pacer audit READMEの
"Current repository HEAD" が fd8e5c69 のまま

P4-2
vulkan-backend.mdが
Khronos validation-layer coverageを未検証扱いしたまま
```

よって今回のsource fixそのものはPASS寄りだが、

```text
完全CLOSED
```

とはまだしない。

---

# 2. Push確認

branch:

```text
develop_remakeVulkan_ver3
```

現在HEAD:

```text
0b5b49d10eb512ff87025afe4e02463514654786
Guard renderer fallback panel lifetime
```

commit時刻:

```text
2026-08-14 13:42:49 UTC
2026-08-14 22:42:49 JST
```

前回監査HEAD:

```text
5c05d249c27cc11d667349bfccae763a941d3ece
```

GitHub compare:

```text
status:
    ahead

ahead_by:
    5

behind_by:
    0

total_commits:
    5
```

**判定: Push確認 PASS**

---

# 3. 前回HEADからの5 commits

順序:

```text
18bad3ade1f0b3c22b1a2d6830e7d53e3f400ebe
    fix

6f1ffb720489b734297467ee791dbde698a271e1
    add md

fd8e5c69e254c5692872c6c76a38471173ef6e73
    Close Vulkan present-pacer audit follow-up

eb35eeb92738b272e1ba3563ccaff2236a917a80
    Document macOS Vulkan validation follow-up

0b5b49d10eb512ff87025afe4e02463514654786
    Guard renderer fallback panel lifetime
```

production挙動に直接影響する最新変更は主に:

```text
0b5b49d
```

である。

それ以前は主に:

```text
audit MD
documentation
fake test hardening
validation evidence
.codex config
```

である。

---

# 4. 前回P4-1: commit provenance

前回指摘:

```text
audit READMEでは
.codex/
docs/development/codex/
がcommitに含まれていないように読める

しかし実際の5c05d249には含まれている
```

follow-upではREADMEが:

```text
Vulkan implementation-scope files
```

と:

```text
Commit provenance
```

を分離した。

さらに:

```text
.codex/config.toml
.codex/agents/luna-worker.toml
docs/development/codex/luna-orchestrator-prompt.md
```

について:

```text
outside the Vulkan implementation scope
but co-committed
```

と明記した。

**判定: P4-1 CLOSED**

---

# 5. 前回P4-2: Intel/macOS/MoltenVK検証状態

`docs/features/rendering/vulkan-backend.md`はfollow-upで:

```text
Intel Iris Plus Graphics 655
macOS
bundled MoltenVK
Google display timing JIT
resize/minimize/restore
```

のruntime evidenceを明記した。

また:

```text
AMD physical:
    unverified

Linux physical:
    unverified

VK_EXT_present_timing physical on Intel/MoltenVK:
    unavailable / unverified
```

と検証範囲を分離した。

前回の:

```text
no non-NVIDIA GPU has run this backend
no macOS system has run this backend
Intel/MoltenVK unverified
```

という誤った状態は解消された。

**判定: 前回P4-2 CLOSED**

ただしKhronos validationについては後述の新規P4がある。

---

# 6. 前回P4-3: queue-pressure recovery証跡

最新fake-dispatch testにはrecovery assertが追加された。

流れ:

```text
16 timed presents
    ↓
queue pressure

17th present
    ↓
TimingAttached = false

fake completed past-timing report
    PresentId = 1
    Complete = true

BeginFrame()
    ↓
ReportPastTiming()
    ↓
completed slot drain
    ↓
queue growth

next PreparePresent()
    ↓
TimingAttached = true
```

assert:

```cpp
Require(recoveredMetadata.TimingAttached, ...);

Require(
    recoverySnapshot.TimingQueueSize == 32,
    ...);

Require(
    recoverySnapshot.TimingQueueRecoveries == 1,
    ...);
```

これにより前回問題だった:

```text
"recovery"と書いているが
pauseまでしかtestしていない
```

という証跡不足は解消。

**判定: P4-3 CLOSED**

---

# 7. legacy surface capability fallback test

追加:

```cpp
TestSurfaceCapabilitiesFallback()
```

内容:

```text
SurfaceCapabilities2Result =
    VK_ERROR_EXTENSION_NOT_PRESENT

QuerySurfaceCapabilities()
    ↓
legacy fallback

Caps2Calls = 1
LegacyCapsCalls = 1

capabilities.minImageCount = 2

GetSwapchainCreateFlags() = 0
```

つまりmodern capability chainが取れない場合に:

```text
modern present timing flagsを残さず
legacy surface capabilityだけで継続
```

することをproduction pacer経由で確認している。

**判定: PASS**

---

# 8. fake-dispatch P3 closure状態

現時点のproduction fake dispatch coverage:

```text
surface capabilities:
    modern success
    modern failure → legacy fallback

WaitForPresent2KHR:
    SUCCESS
    TIMEOUT
    SUBOPTIMAL
    OUT_OF_DATE
    DEVICE_LOST
    SURFACE_LOST
    unknown disable

EXT past timing:
    SUCCESS
    INCOMPLETE
    OUT_OF_DATE
    DEVICE_LOST
    SURFACE_LOST
    unknown disable

EXT timing properties:
    SUCCESS
    NOT_READY
    SURFACE_LOST
    unknown lifecycle disable

EXT time domain:
    SUCCESS
    INCOMPLETE retry
    bounded retry exhaustion
    post-present recovery
    SURFACE_LOST
    required domain missing

GOOGLE refresh:
    SUCCESS
    DEVICE_LOST
    SURFACE_LOST
    unknown disable

GOOGLE past timing:
    SUCCESS
    INCOMPLETE
    OUT_OF_DATE
    DEVICE_LOST
    SURFACE_LOST
    unknown disable

queue:
    initial allocation failure
    queue-full retry
    pressure pause
    completed-report recovery
    queue growth
    metadata re-enable

generation:
    same-frame swapchain recreation
    capture invalidation
    eager lifecycle failure
```

前回最大P3だった:

```text
API-level fake Vulkan dispatch integration
```

は引き続きCLOSED。

---

# 9. macOS Khronos validation follow-up

commit:

```text
eb35eeb92738b272e1ba3563ccaff2236a917a80
Document macOS Vulkan validation follow-up
```

で追加された証跡:

```text
build:
    build_macos_metal_n_vulkan.command
    --jobs 4
    --debug
    --no-bundle
    --build-dir build-mac-vulkan-validation

runtime loader:
    Homebrew vulkan-loader

ICD:
    MoltenVK 1.4.2

layer:
    VK_LAYER_KHRONOS_validation

GPU:
    Intel Iris Plus Graphics 655

ROM:
    Metroid Prime - Hunters (Japan)

state:
    F2 state
    loaded=1

actual renderer:
    Vulkan

internal resolution:
    1x

duration:
    60 sec
```

結果:

```text
VUID:
    0

validation ERROR:
    0

validation channel warning:
    2

warning:
    MoltenVK
    VK_ERROR_FEATURE_NOT_PRESENT
    primitive restart disabling unsupported
```

前回:

```text
Khronos validation:
    BLOCKED / NOT RUN
```

だったmacOSについては:

```text
macOS:
    CLOSED / PASS evidence
```

へ進んだ。

ただし:

```text
Windows validation:
    NOT VERIFIED

Linux validation:
    NOT VERIFIED
```

は残る。

---

# 10. Windows follow-up

repository audit READMEはfollow-up Windows hostで:

```text
tools/build/windows/build-mingw-existing.bat
    --build-dir build/release-mingw-x86_64
    --jobs 1
```

を実行したとしている。

条件:

```text
existing configured tree
Vulkan ON
DX12 ON
developer features ON
Release
```

PASSしたもの:

```text
incremental build

production pacer fake-dispatch target

pure timing test

fake-dispatch test

XeLL state-machine test
```

したがって:

```text
Windows compile:
    incremental PASS evidence

Windows clean build:
    NOT VERIFIED

Windows physical Vulkan runtime:
    NOT VERIFIED
```

とする。

---

# 11. 最新source commit 0b5b49d

最新commit:

```text
0b5b49d10eb512ff87025afe4e02463514654786
Guard renderer fallback panel lifetime
```

変更:

```text
src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp
src/frontend/qt_sdl/Window.cpp
```

目的は2つ。

```text
1.
MELONPRIME_FORCE_VULKAN_RENDERER=1
環境でもruntime failure latchを無視して
Vulkanを再強制し続けない

2.
renderer fallback中にGUIがpanelを破棄している瞬間
EmuThreadのOSD messageがpanelを直接dereferenceしない
```

---

# 12. forced Vulkan renderer fallback loop監査

変更前:

```cpp
if (forceVulkan && forceVulkan[0] == '1')
    return renderer3D_Vulkan;
```

問題:

```text
developer env:
    force Vulkan

Vulkan runtime failure
    ↓
ReportRuntimeFailure()
    ↓
rendererRuntimeFallback
    ↓
GUI panel rebuild
    ↓
NormalizeRendererForPlatform()
    ↓
envが再びVulkanを強制
    ↓
再失敗
    ↓
fallback
    ↓
再強制
```

というloopが成立し得る。

変更後:

```cpp
if (forceVulkan && forceVulkan[0] == '1' &&
    VulkanFeatureCheck::IsRuntimeAvailable())
{
    return renderer3D_Vulkan;
}
```

`VulkanFeatureCheck::ReportRuntimeFailure()`は:

```text
g_runtimeFailed = true
g_probed = false
```

とする。

次の:

```cpp
IsRuntimeAvailable()
```

では:

```text
g_runtimeFailed
    ↓
Available = false
```

となる。

したがってforced env overrideもruntime failure latchを尊重する。

saved rendererがVulkanなら後段:

```cpp
case renderer3D_Vulkan:
    return IsRuntimeAvailable()
        ? requested
        : renderer3D_Software;
```

でSoftwareへ落ちる。

saved rendererがOpenGL等で、envだけVulkan強制していたケースなら:

```text
env Vulkan force:
    disabled by runtime latch

original saved renderer:
    restored
```

となる。

**設計判定: PASS**

---

# 13. Vulkan runtime failure latchとの整合

`MelonPrimeVulkanFeatureCheck.cpp`では:

```cpp
bool g_runtimeFailed = false;
std::string g_runtimeFailureReason;
```

がmutex保護される。

`ReportRuntimeFailure()`:

```text
first failure wins

g_runtimeFailed = true

g_runtimeFailureReason = reason

g_probed = false
```

`RunProbeLocked()`:

```text
if g_runtimeFailed:
    Available = false
    return
```

従って今回の:

```cpp
VulkanFeatureCheck::IsRuntimeAvailable()
```

gateは:

```text
renderer factory
presentation backend
developer env override
```

で共通のruntime failure truthを使う。

別のlocal flagを新設していない。

**判定: PASS**

---

# 14. panel lifetime race監査

変更前:

```cpp
void MainWindow::osdAddMessage(...)
{
    if (!showOSD) return;
    panel->osdAddMessage(color, msg);
}
```

一方panel teardown:

```cpp
void MainWindow::destroyScreenPanel()
{
    QMutexLocker panelLock(&screenPanelLock);

    ScreenPanel* oldPanel = panel;
    panel = nullptr;

    if (!oldPanel)
        return;

    oldPanel->beginClose();
    delete oldPanel;
}
```

つまりrenderer failure時:

```text
EmuThread
    ↓
emuInstance->osdAddMessage()
    ↓
MainWindow::osdAddMessage()

GUI thread
    ↓
rendererRuntimeFallback
    ↓
onUpdateVideoSettings(true)
    ↓
destroyScreenPanel()
```

が近接する。

旧実装では:

```text
panel pointer read
```

にlifetime guardがなかった。

変更後:

```cpp
QMutexLocker panelLock(&screenPanelLock);

if (panel)
    panel->osdAddMessage(color, msg);
```

となった。

これにより:

```text
destroyScreenPanel()
    panel = nullptr
```

と:

```text
osdAddMessage()
    panel dereference
```

が同一mutexでserializeされる。

**設計判定: PASS**

---

# 15. screenPanelLock ordering監査

既存のcross-thread系:

```text
drawScreen()
invalidateRendererOutput()
beginVulkanLowLatencyFrame()
markVulkanReflexInputSample()
markVulkanReflexSimulationStart()
markVulkanReflexSimulationEnd()
finishVulkanLowLatencyFrame()
```

も:

```cpp
QMutexLocker panelLock(&screenPanelLock);
```

を使っている。

panel teardown:

```text
destroyScreenPanel()
```

も同じmutex。

今回OSD pathを同じownership lockへ統合したこと自体は自然。

今回確認したcall orderでは:

```text
onUpdateVideoSettings()
    emuPause()
    ↓
destroyScreenPanel()
```

であり、GUIが`screenPanelLock`を握ったままEmuThread pause完了を待つ形ではない。

明白なABBA deadlockは確認できなかった。

**判定: PASS**

---

# 16. panel delete中lock保持

`destroyScreenPanel()`は:

```text
lock
    ↓
panel = nullptr
    ↓
oldPanel->beginClose()
    ↓
delete oldPanel
    ↓
unlock
```

で、delete終了までlockを保持する。

この設計によりOSD側は:

```text
panel=nullptrを見てreturn
```

するだけでなく、

```text
oldPanel destructorが完全に終わるまで
新しいcross-thread panel callを通さない
```

ことができる。

今回追加されたOSD pathとの関係では安全側。

ただし将来:

```text
ScreenPanel destructor
beginClose()
```

の内部から同じMainWindowの:

```text
osdAddMessage()
drawScreen()
Vulkan low-latency wrapper
```

を同期呼び出しするコードを追加するとnon-recursive `QMutex`再入deadlockになる。

現時点の今回差分からその経路は確認していない。

これは現在のdefectではなく将来契約。

---

# 17. rendererRuntimeFallback signal order

EmuThreadのVulkan renderer init failure pathは:

```text
ReportRuntimeFailure()

OSD:
    "Vulkan initialization failed"

videoRenderer =
    Software

emit rendererRuntimeFallback()
```

となる。

重要なのは:

```text
OSD call
```

が:

```text
emit rendererRuntimeFallback()
```

より先。

そのため今回の`screenPanelLock`は:

```text
fallback panel rebuild開始前のOSD
```

を主に守る。

signalはEmuThread側からGUI receiverへ送られ、

```text
MainWindow::onRendererRuntimeFallback()
    ↓
onUpdateVideoSettings(true)
```

へ到達する。

forced Vulkan latchもこの時点ではすでに:

```text
runtime unavailable
```

になっている。

**判定: PASS**

---

# 18. P3-1: 共通Window.cppへのifdef漏れ

今回の新規変更:

```cpp
void MainWindow::osdAddMessage(...)
```

は:

```text
src/frontend/qt_sdl/Window.cpp
```

というmelonDS共通ファイルにある。

追加コメントも:

```text
EmuThread can report a renderer failure
while the GUI thread is replacing the presentation panel
```

と、MelonPrime native renderer fallbackを直接理由にしている。

追加処理:

```cpp
QMutexLocker panelLock(&screenPanelLock);
if (panel)
    panel->osdAddMessage(color, msg);
```

には:

```cpp
#ifdef MELONPRIME_DS
```

がない。

MelonPrimeDSの既定コード境界ルールでは:

```text
MelonPrime専用不具合のために
melonDS共通sourceを変更する場合

原則:
    #ifdef MELONPRIME_DS
```

で囲う。

この変更は:

```text
Vulkan runtime fallback
MelonPrime native presenter
MelonPrime screenPanelLock lifetime policy
```

を根拠にしたものなので、upstream melonDSにも同じroot causeがあると証明されていない限り、MelonPrime guardを付ける方がrepository contractに合う。

推奨:

```cpp
void MainWindow::osdAddMessage(unsigned int color, const char* msg)
{
    if (!showOSD) return;

#ifdef MELONPRIME_DS
    QMutexLocker panelLock(&screenPanelLock);
    if (panel)
        panel->osdAddMessage(color, msg);
#else
    panel->osdAddMessage(color, msg);
#endif
}
```

もしくは、

```text
screenPanelLockによるOSD lifetime protectionが
upstream melonDSにも必要な汎用bug fix
```

であることを別監査で証明して、guardなし変更として明示する。

現在の情報では後者は証明されていない。

分類:

```text
P3
CODE-BOUNDARY / UPSTREAM-MAINTAINABILITY
CLOSED
```

runtime crashが見つかったという意味ではない。

### 追補判定

この追補では推奨どおり `#ifdef MELONPRIME_DS` を追加し、非MelonPrime
ビルドでは従来の `panel->osdAddMessage()` を維持する形に整理した。
現Windows開発者ビルドでコンパイル・回帰テスト・実プロセスstressが通過
しているため、このP3-1はCLOSEDとする。

---

# 19. P3-2: 最新0b5b49d専用regression evidence不足

前回までのvalidation evidenceは強い。

```text
macOS:
    fd8e5c69 source stateを対象に
    Debug validation F2 runtime

Windows:
    follow-up treeでincremental build

Linux:
    5c05d249系のclean build/test
```

しかし最新:

```text
0b5b49d
```

はその後に:

```text
MelonPrimeVideoBackend.cpp
Window.cpp
```

を変更している。

現HEADに対してGitHub API上:

```text
combined status:
    empty

workflow runs:
    empty
```

である。

さらに0b5b49d commit自体には:

```text
test source追加:
    なし

audit script追加:
    なし

runtime log追加:
    なし

build evidence追加:
    なし
```

である。

特に今回の修正対象は:

```text
runtime failure
cross-thread OSD
panel teardown
forced renderer environment override
```

という、static compileだけでは再現性を保証しにくいlifecycle/concurrency path。

従って最低1本のregression gateが欲しい。

推奨test:

```text
MELONPRIME_FORCE_VULKAN_RENDERER=1

Vulkan runtime failureを
developer-only fault injectionで1回発生

期待:
    ReportRuntimeFailure() = sticky

    rendererRuntimeFallback emit = 1回

    Vulkan force再適用 = 0回

    panel teardown/recreation完了

    OSD path:
        crashなし
        UAFなし
        null panel dereferenceなし

    actual renderer:
        Software
        またはsaved non-Vulkan renderer

    process alive

    fallback loopなし
```

可能なら:

```text
20～100 iterations
```

程度のfail/rebuild stressでpanel lifetimeを固定する。

分類:

```text
P3
REGRESSION / EXECUTION VALIDATION
CLOSED
```

### 追補判定

開発者ビルド限定の一回限りの
`MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE` 注入点、renderer-selection
回帰テスト、`MELONPRIME_RENDERER_SWITCH_STRESS=1,0` の本番経路stressを追加
した。実行結果は以下のとおり。

```text
forced failure injection: 1
Vulkan runtime failure report: 1
Vulkan selection: 1
Vulkan -> Software fallback: 1
renderer switch stress: 40/40
process liveness: 20 seconds
fatal diagnostics: 0
```

`rendererRuntimeFallback` の一回性はfallbackログ1件、再Vulkan選択の不在は
Vulkan selectionログ1件でassertした。パネル破棄/再生成を含む40遷移後も
プロセスは生存した。証跡は
`docs/archive/audits/vulkan/2026-08-14-present-pacer-dispatch/fallback-stress-runtime.log`
に固定した。

---

# 20. 最新sourceのpost-commit buildが必要な理由

0b5b49dの変更量は小さい。

しかし:

```text
Window.cpp:
    Qt
    GUI thread
    EmuThread
    QMutex
    panel lifecycle

MelonPrimeVideoBackend.cpp:
    Vulkan feature probe
    runtime failure latch
    env override
    backend selection
```

と、責務境界が広い。

最低でも:

```text
Windows incremental build
macOS build
Linux compile
```

のうち可能なものをcurrent HEADで再実行し、

さらに1 platformで:

```text
forced Vulkan runtime failure
```

を再現するのが望ましい。

前のfd8e/eb35 PASSをそのまま:

```text
0b5b49d PASS
```

へ継承してはいけない。

この要件は追補で実施済み。現Windows開発者ビルドを再configure/buildし、
renderer fallback回帰テストと実ROMのpanel teardown/rebuild stressを同じ
source treeで実行した。

---

# 21. P4-1: audit READMEのCurrent HEAD stale

current branch:

```text
0b5b49d
```

なのに:

```text
docs/archive/audits/vulkan/
2026-08-14-present-pacer-dispatch/README.md
```

は現在も:

```text
Current repository HEAD:
    fd8e5c69e
    Close Vulkan present-pacer audit follow-up
```

と書いている。

その後:

```text
eb35eeb9
0b5b49d
```

が入っているためcurrentという表現は不正確。

特に0b5b49dはproduction source変更なので、

```text
READMEのfinal PASS
```

がcurrent production HEADを対象とするように読めてしまう。

修正案:

```text
Present-pacer audited source HEAD:
    fd8e5c69e

Latest repository HEAD at document update:
    0b5b49d

0b5b49d renderer-fallback follow-up:
    separately audited / validation pending
```

のように分ける。

分類:

```text
P4
AUDIT PROVENANCE
CLOSED
```

### 追補判定

`docs/archive/audits/vulkan/2026-08-14-present-pacer-dispatch/README.md` は、
present-pacerの監査対象source HEAD (`fd8e5c69e`) と、後続の
`0b5b49d` renderer-fallback follow-upを分離して記載するよう更新した。
後続変更を過去のmacOS/Linux present-pacer runtime証跡へ遡及して帰属させない
ため、P4-1はCLOSEDとする。

---

# 22. P4-2: validation-layer documentation drift

`docs/features/rendering/vulkan-backend.md`のKnown limitationsには現在:

```text
Khronos validation-layer coverage remain unverified
```

相当の記述が残る。

しかしeb35eeb9のfollow-up evidenceでは:

```text
macOS no-bundle Debug
VK_LAYER_KHRONOS_validation active
F2 60 sec
VUID 0
validation ERROR 0
```

が完了している。

正確には:

```text
macOS validation:
    verified on Intel/MoltenVK

Windows validation:
    unverified

Linux validation:
    unverified
```

である。

よってglobalに:

```text
validation-layer coverage unverified
```

と書くよりplatform matrixへ分けるべき。

分類:

```text
P4
DOCUMENTATION DRIFT
CLOSED
```

### 追補判定

`docs/features/rendering/vulkan-backend.md` のKnown limitationsをplatform
matrixへ更新した。macOS Intel/MoltenVK no-bundle Debug F2は
`VK_LAYER_KHRONOS_validation`、60秒、VUID 0、validation ERROR 0として
verified、Windows/Linux validation-layerはunverifiedと明記したため、P4-2は
CLOSEDとする。

---

# 23. README内Windows記述の軽微なドリフト

present-pacer audit READMEのRemaining risksには:

```text
Windows compilation/runtime
...
require their respective environments
```

という表現が残る。

同じREADMEの上部では:

```text
Windows incremental build PASS
```

を報告している。

正確には:

```text
Windows incremental compile:
    verified

Windows clean compile:
    not verified

Windows runtime:
    not verified

Windows validation layer:
    not verified
```

と書くべき。

P4-1のaudit provenance/documentation driftへ包含する。

---

# 24. 現HEADのGitHub CI

current HEAD:

```text
0b5b49d10eb512ff87025afe4e02463514654786
```

GitHub combined status:

```text
statuses:
    []
```

commit workflow runs:

```text
workflow_runs:
    []
```

branch protection:

```text
protected:
    false

required_status_checks:
    none
```

したがって:

```text
GitHub-hosted CI PASS
```

という証跡はない。

---

# 25. 今回のsource差分とbuild gate

`MelonPrimeVideoBackend.cpp`はMelonPrime専用sourceであり問題なし。

変更は:

```cpp
#if defined(MELONPRIME_ENABLE_VULKAN) \
    && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
```

内なのでdeveloper forced Vulkan overrideに限定される。

**判定: PASS**

`Window.cpp`は共通source。

追加mutex guardに:

```text
MELONPRIME_DS guardなし
```

のため前述P3-1。

---

# 26. forced Vulkan fixのproduction release影響

新しいenv override条件は:

```cpp
MELONPRIME_ENABLE_DEVELOPER_FEATURES
```

配下。

従って:

```text
MELONPRIME_FORCE_VULKAN_RENDERER
```

の今回変更はdeveloper buildにのみ影響。

通常releaseのrenderer selection:

```cpp
case renderer3D_Vulkan:
    return IsRuntimeAvailable()
        ? requested
        : Software;
```

は元のruntime gateを使用する。

release renderer selection semanticsを広げてはいない。

**判定: PASS**

---

# 27. OSD mutex変更のbackend影響

`osdAddMessage()`は全panel種別へ共通。

今回のmutex追加によりMelonPrime buildでは:

```text
Software / NativeQt
OpenGL
Vulkan
DX12
Metal
```

のpanel lifetimeを同じlockで守ることになる。

新しいrenderer-specific castやAPI callはない。

従ってVulkanだけを特別扱いして他backendを壊す構造ではない。

ただしshared-source guard問題は別。

---

# 28. deadlock static audit

今回確認した主要lock sequence:

```text
EmuThread runtime failure
    ↓
EmuInstance::osdAddMessage
    ↓
MainWindow::osdAddMessage
    ↓
screenPanelLock
    ↓
panel->osdAddMessage
    ↓
unlock
    ↓
emit rendererRuntimeFallback
```

GUI:

```text
onRendererRuntimeFallback
    ↓
onUpdateVideoSettings
    ↓
emuPause
    ↓
destroyScreenPanel
    ↓
screenPanelLock
```

つまり通常failure pathでは:

```text
EmuThread:
    screenPanelLockをreleaseしてからsignal

GUI:
    signal受信後にpause
    その後screenPanelLock
```

なので明白な:

```text
EmuThread:
    lock A → wait GUI

GUI:
    lock B → wait EmuThread
```

のABBA cycleは確認しなかった。

**静的判定: PASS**

ただしP3-2のstress executionは必要。

---

# 29. UAF static audit

`destroyScreenPanel()`:

```cpp
ScreenPanel* oldPanel = panel;
panel = nullptr;
...
delete oldPanel;
```

がlock内。

`osdAddMessage()`:

```cpp
lock
if (panel)
    panel->...
```

なので:

```text
OSD obtains lock first:
    old panel call完了
    ↓
    destroy waits
    ↓
    delete

destroy obtains lock first:
    panel=null
    delete完了
    ↓
    OSD obtains lock
    ↓
    panel null
    ↓
    no dereference
```

となる。

この2経路間のpanel UAFは防げている。

**静的判定: PASS**

---

# 30. 前回P2/P3の回帰確認

前回CLOSED:

```text
production fake dispatch
```

今回:

```text
PASS
```

前回CLOSED:

```text
queue initial allocation failure
```

今回:

```text
PASS
```

前回P4:

```text
queue recovery
```

今回:

```text
explicit fake recovery test追加
CLOSED
```

前回CLOSED:

```text
same-frame recreation
generation invalidation
typed lifecycle routing
```

今回それらのproduction sourceは変更されていない。

**判定: regressionなし**

---

# 31. platform matrix

| 項目 | 現在判定 |
|---|---|
| current branch HEAD確認 | PASS |
| current HEAD source static audit | PASS with P3 gaps |
| production fake pacer | PASS evidence |
| queue recovery fake test | PASS evidence |
| legacy capability fallback fake test | PASS evidence |
| macOS Vulkan build | PASS evidence on fd8e-era source |
| macOS physical Vulkan F2 | PASS evidence on fd8e-era source |
| macOS Khronos validation | PASS evidence on fd8e-era source |
| Windows incremental Vulkan/DX12 build | PASS evidence before 0b5 latest source |
| Windows clean build | NOT VERIFIED |
| Windows Vulkan runtime | NOT VERIFIED |
| Windows Khronos validation | NOT VERIFIED |
| Linux clean build | PASS older evidence |
| Linux physical Vulkan runtime | NOT VERIFIED |
| Linux Khronos validation | NOT VERIFIED |
| AMD physical Vulkan | NOT VERIFIED |
| physical VK_EXT_present_timing lifecycle | NOT VERIFIED on current available Mac |
| current 0b5 renderer fallback failure loop test | NOT RUN |
| current 0b5 panel lifetime stress | NOT RUN |
| current HEAD GitHub-hosted CI | NO RUN |

---

# 32. 今回のfinding一覧

```text
P2:
    NONE FOUND

P3-1:
    common Window.cpp change lacks MELONPRIME_DS guard
    OPEN

P3-2:
    latest 0b5 fallback/lifetime fix lacks dedicated regression execution
    OPEN

P4-1:
    audit README current HEAD / Windows status stale
    OPEN

P4-2:
    vulkan-backend.md validation-layer status stale
    OPEN
```

---

# 33. 推奨修正順

```text
1.
Window.cpp osdAddMessageの今回追加部分を
MELONPRIME_DS guardへ入れる
またはgeneric upstream bugである証拠を明示する

2.
developer-only Vulkan failure injectionを用意

3.
MELONPRIME_FORCE_VULKAN_RENDERER=1
+ forced runtime failure
を20回以上stress

4.
assert:
    fallback signal 1回
    no loop
    panel teardown/rebuild完了
    no crash
    process alive
    actual renderer fallback

5.
current 0b5 HEADで
macOS build
またはWindows buildを再実行

6.
可能ならcurrent HEADで
macOS validation F2を再実行

7.
audit READMEのCurrent HEADを更新

8.
vulkan-backend.mdのvalidation matrix更新

9.
再監査
```

---

# 34. 最小regression test仕様

developer-only test seam案:

```text
MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE=1
```

条件:

```text
MELONPRIME_ENABLE_DEVELOPER_FEATURES
MELONPRIME_ENABLE_VULKAN
```

動作:

```text
Vulkan renderer/presenterが一度正常に選択された後
初回の指定checkpointで:

ReportRuntimeFailure("test injected Vulkan runtime failure")

renderer fallback pathを通す
```

検証ログ:

```text
runtime failure reported:
    exactly 1

Renderer fallback requested=Vulkan:
    exactly 1

renderer requested after fallback:
    Vulkanでない

presentation backend after fallback:
    NativeQt / OpenGL等のexpected fallback

repeated rendererRuntimeFallback:
    0

process alive:
    yes
```

panel lifetime:

```text
failure OSD message
    ↓
rendererRuntimeFallback
    ↓
destroyScreenPanel
    ↓
createScreenPanel
```

を毎iterationで通す。

---

# 35. regression testで避けるべき実装

次のようなtest-only production behaviorは避ける。

```text
sleepを挿入してraceを隠す

screenPanelLockをrecursive mutexへ変える

fallback中OSDを全面disable

runtime failure時にsignalを送らない

MELONPRIME_FORCE_VULKAN_RENDERER env自体を無視

panel pointerをatomicにしただけで
widget lifetimeをlockしない
```

根本契約:

```text
panel ownership:
    screenPanelLock

runtime availability truth:
    VulkanFeatureCheck latch

fallback authority:
    GUI renderer transition path
```

を維持する。

---

# 36. 0b5b49dでCLOSEDできているroot cause

今回最新commitで修正されたroot causeを整理すると:

## A. forced renderer retry loop

```text
root cause:
    developer env overrideが
    runtime failure latchより優先されていた
```

修正:

```text
env overrideもIsRuntimeAvailable()に従う
```

**静的にCLOSED**

## B. fallback OSD panel lifetime

```text
root cause:
    OSD cross-thread pathだけ
    panel ownership mutexを通していなかった
```

修正:

```text
osdAddMessage()
    screenPanelLock
```

**静的にCLOSED**

ただしBはexecution stress未実施なので:

```text
implementation CLOSED
validation OPEN
```

とする。

---

# 37. sourceの変更量とrisk

0b5b49d:

```text
MelonPrimeVideoBackend.cpp:
    +7 -1

Window.cpp:
    +9 -1
```

変更量は小さい。

しかしrisk種別は:

```text
renderer selection state
runtime failure state
cross-thread GUI pointer lifetime
mutex ordering
panel destruction
```

なので、

```text
small diff = low risk
```

とは扱わない。

静的設計は妥当だがregression executionを要求する。

---

# 38. current audit READMEの扱い

現READMEに書かれた:

```text
Final audit:
    PASS
```

は主として:

```text
fd8e5c69
present-pacer fake-dispatch hardening
```

に対するPASSとしては妥当。

しかし最新branch全体:

```text
0b5b49d
```

に対するPASS証拠として引用してはいけない。

理由:

```text
0b5b49d production source変更
```

がその後にあるため。

今後READMEは:

```text
present-pacer audit HEAD

renderer-fallback audit HEAD

latest repository HEAD
```

を分けると混乱しにくい。

---

# 39. macOS validation evidenceの扱い

eb35eeb9のvalidationは有効な証拠。

ただし対象source:

```text
fd8e5c69
```

である。

従って:

```text
VulkanPresentPacer fake dispatch:
    strong evidence

Intel/MoltenVK presentation:
    strong evidence

Khronos layer setup:
    strong evidence
```

として使える。

一方:

```text
0b5b49d Window.cpp OSD mutex
0b5b49d forced Vulkan fallback latch
```

については実行していない。

この境界をMD上でも維持する。

---

# 40. Windows evidenceの扱い

Windows incremental PASSは:

```text
compiler compatibility
link compatibility
fake test execution
```

には有効。

ただし最新0b5の:

```text
Window.cpp
MelonPrimeVideoBackend.cpp
```

まで再compileした証拠はrepository上明示されていない。

そのためcurrent HEADのWindows buildをPASSとは書かない。

---

# 41. Linux evidenceの扱い

Linux VM clean build:

```text
313/313
```

は過去source stateの有効なportability evidence。

ただし:

```text
0b5b49d
```

はQt shared Window sourceを触っているため、

```text
Linux compile regressionなし
```

をcurrent HEADで再確認する価値がある。

特に:

```text
QMutexLocker
```

自体は既存使用があるのでcompile riskは低いが、証拠は分ける。

---

# 42. non-Vulkan backendへの静的回帰

今回のOSD mutex changeは:

```text
panel base class
```

に対して動く。

Vulkan-specific castなし。

forced Vulkan logicは:

```text
MELONPRIME_ENABLE_VULKAN
MELONPRIME_ENABLE_DEVELOPER_FEATURES
```

配下。

従って:

```text
DX12 renderer choice
Metal renderer choice
OpenGL renderer choice
Software renderer choice
```

の通常selectionを直接変更していない。

明白なcross-backend functional regressionは確認できなかった。

---

# 43. 追加で監査したい将来点

今回のfixを入れた後、将来以下が増えたら再監査対象。

```text
ScreenPanel::beginClose()
    からMainWindow OSDを同期呼び出す

ScreenPanel destructor
    からMainWindowのpanel wrapperを同期呼び出す

renderer fallback signalをDirectConnection化

screenPanelLockを握ったまま
emuPause()/wait()する

runtime failure latchを
ResetProbeForRetry()以外で自動clearする

developer forced Vulkanが
IsRuntimeAvailable()を迂回する別pathを追加
```

これらは今回の安全性前提を壊す。

---

# 44. Definition of Done

今回残件を完全CLOSEDする条件:

```text
[x] Window.cpp共通コード境界を整理
    MELONPRIME_DS guard
    またはgeneric upstream fix証明

[x] forced Vulkan failure regression test

[x] fallback signal count assert

[x] no repeated Vulkan selection assert

[x] panel teardown/rebuild stress

[x] process alive assert

[x] current 0b5 HEAD compile PASS

[x] current 0b5 HEAD runtime smoke PASS

[x] audit README HEAD更新

[x] vulkan-backend validation status更新

[x] GitHub-hosted CI
    または明示的local evidence
```

---

# 45. 最終監査判定

current HEAD:

```text
0b5b49d10eb512ff87025afe4e02463514654786
Guard renderer fallback panel lifetime
```

監査結果:

```text
Push:
    PASS

previous P4 provenance:
    CLOSED

previous P4 backend-doc drift:
    CLOSED for Intel/macOS/MoltenVK runtime status

previous P4 queue recovery evidence:
    CLOSED

legacy capability fallback test:
    PASS

queue recovery fake integration:
    PASS

macOS Khronos validation:
    PASS evidence on fd8e-era source

Windows incremental build:
    PASS on current follow-up source tree

Windows current-source configure/build:
    PASS

Windows forced Vulkan fallback/panel stress:
    PASS (40/40 switches, 20s process liveness)

latest forced Vulkan loop fix:
    STATIC PASS

latest panel lifetime fix:
    STATIC PASS

latest P2 runtime defect:
    NONE FOUND
```

残件:

```text
P3-1:
    Window.cpp common-code ifdef boundary
    CLOSED

P3-2:
    0b5 forced-failure / panel-lifetime regression execution
    CLOSED

P4-1:
    audit README latest HEAD/status drift
    CLOSED

P4-2:
    vulkan-backend validation-layer documentation drift
    CLOSED
```

最終判定:

```text
PASS FOR 0b5 SOURCE AND FOLLOW-UP VALIDATION

NO NEW P2 FUNCTIONAL DEFECT FOUND

P3-1/P3-2 CLOSED
P4-1/P4-2 CLOSED

LOCAL WINDOWS BUILD AND RUNTIME EVIDENCE PASS

GITHUB-HOSTED CI: NOT RUN / NOT VERIFIED

EXTERNAL PLATFORM/GPU VALIDATION GATES REMAIN OPEN
```

---

# 46. GitHub参照

Branch:

```text
https://github.com/ag-advania/melonPrimeDS/tree/develop_remakeVulkan_ver3
```

Current HEAD:

```text
https://github.com/ag-advania/melonPrimeDS/tree/0b5b49d10eb512ff87025afe4e02463514654786
```

Current commit:

```text
https://github.com/ag-advania/melonPrimeDS/commit/0b5b49d10eb512ff87025afe4e02463514654786
```

Follow-up commits:

```text
https://github.com/ag-advania/melonPrimeDS/commit/fd8e5c69e254c5692872c6c76a38471173ef6e73

https://github.com/ag-advania/melonPrimeDS/commit/eb35eeb92738b272e1ba3563ccaff2236a917a80
```

主要ファイル:

```text
src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp
src/frontend/qt_sdl/MelonPrimeVulkanFeatureCheck.cpp
src/frontend/qt_sdl/Window.cpp
src/frontend/qt_sdl/Window.h
src/frontend/qt_sdl/EmuThread.cpp
src/frontend/qt_sdl/EmuInstance.cpp

tools/testing/vulkan-present-pacer-dispatch-tests.cpp
tools/testing/vulkan-renderer-fallback-tests.cpp
tools/testing/vulkan-renderer-fallback-stress.ps1
tools/testing/vulkan-manual-ui-actions.ps1

docs/archive/audits/vulkan/2026-08-14-present-pacer-dispatch/README.md
docs/archive/audits/vulkan/2026-08-14-present-pacer-dispatch/fallback-stress-runtime.log
docs/features/rendering/vulkan-backend.md
docs/development/build/macos-vulkan.md
```

---

# 47. 完遂後の次回監査境界

今回の0b5b49d fallback/panel-lifetime追補は完遂した。次回の監査対象は
今回のCLOSED項目ではなく、以下の外部環境依存ゲートへ戻る。

```text
Windows/Linux Khronos validation layer
AMD physical Vulkan
physical Linux Vulkan
physical VK_EXT_present_timing lifecycle
cross-GPU endurance/parity
```

今回の明示的local evidenceは、GitHub-hosted CIの代替としてこの監査範囲を
閉じる。外部platform validation側の主要残件は:

```text
Windows physical Vulkan runtime
Linux physical Vulkan runtime
Windows/Linux validation layer
AMD physical Vulkan
physical VK_EXT_present_timing lifecycle
cross-GPU endurance/parity
```

へ戻る。

---

# 48. 完遂時の実行証跡

現source treeで以下を実行した。

```text
cmd /c tools\build\windows\build-mingw.bat --jobs 1 --tail 220
```

結果:

```text
configure: PASS
build: PASS
Vulkan present timing model tests: PASS
Vulkan present pacer fake-dispatch tests: PASS
Vulkan renderer fallback regression tests: PASS
Intel XeLL fake API state-machine tests: PASS
```

実ROM runtime stress:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File tools\testing\vulkan-renderer-fallback-stress.ps1 `
  -Rom C:\DSMPH\melonPrimeDS最新版\balancedRom.nds `
  -BuildDir build\release-mingw-x86_64 `
  -Iterations 20 -IntervalMs 100 -Seconds 20
```

結果:

```text
forced Vulkan runtime failure injection: 1
Vulkan runtime failure report: 1
Vulkan selection: 1
Vulkan -> Software fallback: 1
renderer switch stress: 40/40
process liveness: 20 seconds
fatal diagnostics: 0
result: PASS
```

この実行はWindows NVIDIAホストのlocal developer-runtime evidenceであり、
Khronos validation-layerやAMD/Intel/Linuxの外部platform gateをPASSとする
ものではない。詳細は
`docs/archive/audits/vulkan/2026-08-14-present-pacer-dispatch/fallback-stress-runtime.log`
に固定した。
