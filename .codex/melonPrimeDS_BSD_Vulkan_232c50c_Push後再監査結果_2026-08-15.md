# melonPrimeDS BSD Vulkan対応 Push後再監査結果

- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver3`
- 前回中間監査基準HEAD: `15cf59a56c40941189e6be0c39f3290d378789aa`
- 初回Push後監査対象HEAD: `232c50c726b1645852ccf731ab4695fb2729d4a0`
- P3/P4 closure implementation/workflow HEAD: `6cd32fc34d4a41d7797c5823ee62ca11a9efc467`
- P3/P4 closure implementation commits: `911a860120fad655167d3c90a27b220947247335`, `6cd32fc34d4a41d7797c5823ee62ca11a9efc467`
- 初回監査時のproduction / workflow実行対象HEAD: `24157d40f0723a090a55117448973887c650b8dd`
- 作成日: 2026-08-15
- 監査対象: BSD Vulkan X11 WSI、CMake、BSD CI、回帰workflow、前回P3/P4指摘

---

# 1. 最終結論

今回のPush後再監査結果:

```text
BSD Vulkan implementation direction:
    PASS

BSD 3種 Vulkan ON build:
    PASS

BSD 3種 Vulkan OFF build:
    PASS

BSD present timing model:
    PASS

BSD present pacer fake-dispatch:
    PASS

BSD binary packaging:
    PASS

All BSD artifacts:
    PASS

Ubuntu regression workflow:
    PASS

macOS regression workflow:
    PASS

Windows regression workflow:
    PASS

NetBSD前回CI blocker:
    CLOSED

新規P2 production defect:
    NONE FOUND

前回P3:
    0 OPEN / CLOSED (2/2)

P4:
    0 OPEN / CLOSED (3/3)

physical BSD GPU runtime:
    NOT TESTED

CI software Vulkan presentation smoke:
    NOT RUN
```

総合判定:

```text
PASS FOR BUILD / STATIC INTEGRATION

NO NEW P2 PRODUCTION DEFECT FOUND

BSD 3-OS STRICT BUILD MATRIX CLOSED

ALL REQUIRED P3/P4 VALIDATION AND OBSERVABILITY GATES CLOSED

PHYSICAL BSD VULKAN RUNTIME IS NOT VERIFIED
```

今回の完遂後再監査では、BSD 3種のVulkan-enabled buildに加えて、Qt 6.5+の明示assert、production `VulkanLoader::Library::Open()`の実ロード、CI observability分離、および監査provenance分離まで実証済み。

一方、

```text
実BSD + physical GPU + X11上で
実際にVkSurfaceKHRを生成しswapchain presentまで成功
```

した証拠はまだ無い。

これは元のDefinition of Doneでも必須ではないため、今回のbuild/static対応を否定するものではない。

---

# 2. HEAD確認（初回スナップショットと完遂後実装HEAD）

P3/P4 closure implementation/workflow HEAD:

```text
6cd32fc34d4a41d7797c5823ee62ca11a9efc467
Load versioned BSD Vulkan runtimes
```

このHEADを対象に、BSD strict workflowとUbuntu/macOS/Windows regression workflowを実行した。

初回Push後監査対象HEAD:

```text
232c50c726b1645852ccf731ab4695fb2729d4a0
Record final BSD and platform workflow results
```

初回Push後監査の親:

```text
24157d40f0723a090a55117448973887c650b8dd
Keep BSD stub guard outside scatter budget
```

`15cf59a...`から初回Push後監査対象HEADまで:

```text
ahead_by = 5
behind_by = 0
```

初回Push後監査時の変更ファイルは以下の3ファイルのみ:

```text
.codex/melonPrimeDS_BSD_Vulkan対応_実装指示書_2026-08-14_最新ブランチ対応改訂版.md

.github/workflows/build-bsd.yml

src/frontend/qt_sdl/MelonPrimeVulkanSurfaceStub.cpp
```

BSD Vulkan本体:

```text
MelonPrimeVulkanSurfaceBSD.cpp
VulkanContext.cpp
CMakeLists.txt
```

には、前回中間監査後のfunctional変更は入っていない。

その後のP3/P4 closureで、以下のproduction/CMake/test/workflow変更を追加した。

```text
.github/workflows/build-bsd.yml
src/VulkanLoader.cpp
src/frontend/qt_sdl/CMakeLists.txt
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceStub.cpp
tools/testing/vulkan-loader-open-tests.cpp
```

したがって、初回Push後監査の静的評価に加えて、P3/P4 closureの実装とCI実証を完遂した。

---

# 3. 5 commitsの流れ

前回中間監査HEAD:

```text
15cf59a
Implement BSD Vulkan X11 WSI and strict CI validation
```

以降:

```text
007b139
Make BSD Vulkan dependency probe portable

d1fbd92
Probe BSD Vulkan files without find-specific syntax

272c8cd
Record successful BSD Vulkan matrix verification

24157d4
Keep BSD stub guard outside scatter budget

232c50c
Record final BSD and platform workflow results

911a860
Close BSD Vulkan audit validation gates

6cd32fc
Load versioned BSD Vulkan runtimes
```

初回監査後の2 commitで、

```text
Qt 6.5+ CI assertとproduction loader actual-load testを追加
↓
BSDのversioned Vulkan loader pathをproduction loader候補へ追加
```

まで実装し、対象HEADでCIを再実行した。

---

# 4. 前回NetBSD failureの根本原因

最初のBSD workflow:

```text
run 31812835788
```

ではNetBSDが:

```text
Verify BSD Vulkan dependencies and shader source sync
```

でFAILした。

中間監査時点では:

```text
header
loader
Python
shader sync
```

のどれかまでしか断定できなかった。

その後のcommitで原因は:

```text
NetBSD上のfind expression互換性
```

と特定された。

元処理:

```sh
find ... -path '*/vulkan/vulkan.h'
```

をBSD間で共有していた。

修正後は`find`式に依存せず:

```sh
for include_root in ...
    candidate="$include_root/vulkan/vulkan.h"
    [ -f "$candidate" ]
```

で既知package prefixを直接確認する。

loaderも同様に:

```sh
for lib_root in ...
    "$lib_root"/libvulkan.so
    "$lib_root"/libvulkan.so.*
```

へ変更。

**判定: ROOT CAUSE CLOSED**

---

# 5. NetBSD CI blocker

前回:

```text
NetBSD:
    FAIL
```

現在の最終strict BSD workflow:

```text
run 31816873249
head_sha 24157d40f0723a090a55117448973887c650b8dd
conclusion success
```

NetBSD job:

```text
VM start:
    PASS

Install dependencies:
    PASS

Configure:
    PASS

Verify BSD Vulkan dependencies and shader source sync:
    PASS

Build and run BSD Vulkan validation targets:
    PASS

Build Vulkan-disabled BSD variant:
    PASS

Build and package binary:
    PASS

Upload binary:
    PASS
```

したがって前回の:

```text
CI BLOCKER-1
NetBSD preflight failure
```

は**CLOSED**。

---

# 6. FreeBSD最終結果

最終BSD run:

```text
31816873249
```

FreeBSD x86_64:

```text
Start VM:
    PASS

Install dependencies:
    PASS

Vulkan ON Configure:
    PASS

Vulkan dependencies + shader source sync:
    PASS

Vulkan present timing validation:
    PASS

Vulkan present pacer fake-dispatch:
    PASS

full Vulkan ON build:
    PASS

Vulkan OFF configure/build:
    PASS

binary package:
    PASS

artifact upload:
    PASS
```

**判定: PASS**

---

# 7. NetBSD最終結果

NetBSD x86_64:

```text
Start VM:
    PASS

Install dependencies:
    PASS

Vulkan ON Configure:
    PASS

dependency/shader preflight:
    PASS

Vulkan validation targets:
    PASS

full Vulkan ON build:
    PASS

Vulkan OFF build:
    PASS

package:
    PASS

artifact:
    PASS
```

前回failureの再発なし。

**判定: PASS**

---

# 8. OpenBSD最終結果

OpenBSD x86_64:

```text
Start VM:
    PASS

Install dependencies:
    PASS

Vulkan ON Configure:
    PASS

dependency/shader preflight:
    PASS

Vulkan validation targets:
    PASS

full Vulkan ON build:
    PASS

Vulkan OFF build:
    PASS

package:
    PASS

artifact:
    PASS
```

**判定: PASS**

---

# 9. All BSD artifacts

最終runには:

```text
All BSD artifacts
```

jobも存在。

```text
FreeBSD artifact download:
    PASS

NetBSD artifact download:
    PASS

OpenBSD artifact download:
    PASS

bundle:
    PASS

upload:
    PASS
```

つまり単なるcompile successだけではなく、
3 BSDのbinary artifact生成まで成立。

**判定: PASS**

---

# 10. strict CI gate

`build` jobにはjob-level:

```yaml
continue-on-error: true
```

が無い。

そのため:

```text
FreeBSD
NetBSD
OpenBSD
```

いずれかのvalidation/build failureはmatrix failureとして伝播する。

以前のtolerant BSD buildより、
BSD VulkanのDefinition of Done用evidenceとして適切。

`all-artifacts`側の部分的toleranceはartifact aggregationの責務であり、
build matrix自体のstrictnessとは分離されている。

**判定: PASS**

---

# 11. Vulkan ON / OFF hard gate

workflowはVulkan ON:

```text
MELONPRIME_ENABLE_VULKAN=ON
MELONPRIME_FORCE_DISABLE_VULKAN=OFF
```

を明示。

さらにconfigure logから:

```text
MelonPrime Vulkan backend: enabled
```

をgrep。

Vulkan OFF:

```text
MELONPRIME_ENABLE_VULKAN=OFF
MELONPRIME_FORCE_DISABLE_VULKAN=ON
```

別build directoryで実行。

```text
MelonPrime Vulkan backend: disabled
```

もgrep。

3 BSDすべてで両方PASS。

これにより:

```text
Vulkanがheader不足でsilent disableされたbuildをPASS扱い
```

する問題は防げている。

**判定: PASS**

---

# 12. BSD surface translation unit

前回監査済みの:

```text
MelonPrimeVulkanSurfaceBSD.cpp
```

について今回functional差分は無い。

維持されている設計:

```text
FreeBSD / NetBSD / OpenBSD明示guard

Qt private QPA禁止

Qt 6.5+ public QNativeInterface::QX11Application

xcb plugin only

XCB first

Xlib fallback

Wayland unsupported

unknown Qt plugin unsupported

local Vulkan ABI structs

VK_USE_PLATFORM_*を拡散しない
```

3 BSDのfull Vulkan ON buildがPASSしたため、
少なくとも各BSD toolchain/package環境でtranslation unitがcompile/link可能なことも実証された。

**判定: PASS**

---

# 13. CMake routing

前回実装済み:

```text
Windows -> Win32 adapter
macOS -> Metal adapter
Linux -> Linux adapter
FreeBSD / NetBSD / OpenBSD -> BSD adapter
unknown Unix -> Stub
```

今回のworkflowで3 BSD full buildがPASS。

BSD source selectionが実際のCMake環境でも成立することを確認できた。

**判定: PASS**

---

# 14. Stub guard

Stubは現在:

```text
!_WIN32
!__APPLE__
!__linux__
!__FreeBSD__
!__NetBSD__
!__OpenBSD__
```

に限定。

BSD adapterとのduplicate symbolは発生しない。

`24157d4`ではscatter-budget audit向けにpreprocessor guardを1行へ整理しただけで、
条件式の意味は変えていない。

**functional判定: PASS**

---

# 15. Linux regression workflow

Ubuntu run:

```text
31816875432

head_sha:
24157d40f0723a090a55117448973887c650b8dd

status:
completed

conclusion:
success
```

Linux WSI本体はBSD実装時に変更していない。

workflowもPASS。

**判定: PASS**

---

# 16. macOS regression workflow

macOS run:

```text
31816878478

head_sha:
24157d40f0723a090a55117448973887c650b8dd

status:
completed

conclusion:
success
```

BSD CMake分岐追加によりApple surface adapter selectionが壊れていない。

**判定: PASS**

---

# 17. Windows regression workflow

Windows run:

```text
31816881225

head_sha:
24157d40f0723a090a55117448973887c650b8dd

status:
completed

conclusion:
success
```

Win32 Vulkan / DX12等を含む既存Windows workflowの回帰なし。

**判定: PASS**

---

# 18. workflow evidence boundary

重要:

上記4 workflow:

```text
BSD
Ubuntu
macOS
Windows
```

が実際に走ったHEADは:

```text
24157d40f0723a090a55117448973887c650b8dd
```

現在branch HEAD:

```text
232c50c726b1645852ccf731ab4695fb2729d4a0
```

は、その後の:

```text
Record final BSD and platform workflow results
```

という`.codex`文書だけのcommit。

`232c50c`自体にはGitHub Actions runは存在しない。

ただし:

```text
24157d -> 232c50c
```

の差分は実装source/workflowではなく監査文書のみ。

従ってproduction binary/sourceについて、
`24157d`のworkflow証跡は現在HEADのproduction treeにも実質適用可能。

一方、監査文書内で:

```text
all targeted the current branch HEAD
```

と書くのは、現在のrefを基準にすると厳密には誤り。

後述P4。

---

# 19. 初回監査P3-1: Qt 6.5+ CI assert

前回指摘:

```text
P3
Qt 6.5+ CI assert欠落
```

状態:

```text
初回監査時 OPEN
```

理由:

BSD adapterは:

```cpp
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    UnsupportedQtVersion()
#endif
```

となる。

つまりQt < 6.5でもsource自体はcompile可能。

そのため:

```text
Vulkan ON full build PASS
```

だけでは:

```text
runtimeでBSD X11 adapterが有効なQt 6.5+
```

を証明しない。

現在workflowには:

```text
qtpaths --qt-version
qmake --version
Qt version assert
```

等が無い。

元改訂指示書にも:

```text
使用Qt versionを各BSD jobでログ
Phase 1正式範囲 Qt 6.5+を確認
```

という要件があった。

### 推奨修正

各BSDでQt versionを取得し:

```text
Qt >= 6.5
```

を明示assert。

完遂後再監査:

```text
src/frontend/qt_sdl/CMakeLists.txtにMELONPRIME_REQUIRE_QT6_5=ONのconfigure-time gateを追加。
BSD workflowのON/OFF configureと独立したVerify Qt 6.5+ stepの双方でQt 6.5未満をfail。
```

対象HEAD `6cd32fc34d4a41d7797c5823ee62ca11a9efc467`のBSD CIログ:

```text
NetBSD:  Qt 6.11.1 / Verify Qt 6.5+ PASS
FreeBSD: Qt 6.11.1 / Verify Qt 6.5+ PASS
OpenBSD: Qt 6.10.2 / Verify Qt 6.5+ PASS
```

**P3-1: 初回監査時 OPEN / 完遂後再監査 CLOSED**

---

# 20. 初回監査P3-2: production Vulkan loader candidate actual-load

前回指摘:

```text
P3
production loader candidate actual-load検証欠落
```

状態:

```text
初回監査時 OPEN
```

production `VulkanLoader.cpp`がUnixで試す名前:

```text
libvulkan.so.1
libvulkan.so
```

現在CIが確認するのは:

```sh
"$lib_root"/libvulkan.so
"$lib_root"/libvulkan.so.*
```

のどれかが`-f`で存在すること。

したがって理論上:

```text
libvulkan.so.0
```

だけ存在してもpreflightはPASSし得る。

さらに:

```text
file exists
```

と:

```text
dlopen("libvulkan.so.1") succeeds
```

は同じではない。

今回full application buildはPASSしたが、
Vulkan loaderはruntime `dlopen`設計なのでlink時には証明されない。

### 推奨修正

最善:

```text
VulkanLoader::Library::Open()
```

をそのまま使う小さいBSD CI test targetを追加。

最低限:

```text
dlopen("libvulkan.so.1")
or
dlopen("libvulkan.so")
```

のどちらかが成功することをCIで確認。

完遂後再監査:

production `melonDS::Vk::Library::Open()`を直接呼ぶ
`melonprime_vulkan_loader_open_check` targetを追加し、BSD workflowでbuild/runした。
また、BSDのpackage prefixにあるversioned `libvulkan.so.*`をproduction loader候補として列挙するようにした。

対象HEAD `6cd32fc34d4a41d7797c5823ee62ca11a9efc467`の実ロード結果:

```text
NetBSD:  PASS: production Vulkan loader opened /usr/pkg/lib/libvulkan.so.1
FreeBSD: PASS: production Vulkan loader opened /usr/local/lib/libvulkan.so.1
OpenBSD: PASS: production Vulkan loader opened /usr/local/lib/libvulkan.so.1
```

これはloaderファイルの存在確認ではなく、production loader実装の`Open()`、instance proc address取得までを通過した結果である。

**P3-2: 初回監査時 OPEN / 完遂後再監査 CLOSED**

---

# 21. 初回監査P4-1: Stub comment stale

現在Stub冒頭:

```text
currently the BSDs
```

という説明が残っている。

しかし現在:

```text
FreeBSD / NetBSD / OpenBSD
    -> MelonPrimeVulkanSurfaceBSD.cpp
```

でありStub対象ではない。

preprocessor guardは正しいためfunctional issueではない。

### 推奨

```text
currently unsupported Unix-like platforms
```

等へ変更。

完遂後再監査では、Stub commentを以下の意味へ更新した。

```text
BSDs use the dedicated BSD X11 adapter;
the fallback remains for other Unix-like platforms without a supported surface path.
```

BSD 3OSのsource routingと一致するため、P4-1をCLOSEDとする。

**P4-1: 初回監査時 OPEN / 完遂後再監査 CLOSED**

---

# 22. 初回監査P4-2: preflight observability

現在:

```text
Verify BSD Vulkan dependencies and shader source sync
```

という1 step内に:

```text
header path
loader path
Python
shader source sync
```

がまとめられている。

最初のNetBSD failure時、
job summaryからはfailure要因が即座に分離しづらかった。

推奨:

```text
Verify Vulkan headers
Verify Vulkan loader
Verify Qt version
Verify shader source sync
```

完遂後再監査では、以下を独立stepへ分離した。

```text
Verify Qt 6.5+
Verify Vulkan headers
Verify Vulkan loader candidate files
Verify shader source sync
Build and run production Vulkan loader open check
```

対象HEADのFreeBSD/NetBSD/OpenBSD jobはいずれも各stepを通過し、失敗時にもQt、headers、loader、shader、production actual-loadを個別に識別できる。

**P4-2: 初回監査時 OPEN / 完遂後再監査 CLOSED**

---

# 23. 初回監査P4-3: current-HEAD wording / evidence provenance

現在の`.codex`報告には:

```text
Ubuntu / macOS / Windows workflows:
all targeted the current branch HEAD
```

という表現がある。

実際のrun対象:

```text
24157d40f0723a090a55117448973887c650b8dd
```

現在のbranch HEAD:

```text
232c50c726b1645852ccf731ab4695fb2729d4a0
```

ただし現在HEADとの差は報告MDのみなので、
production evidenceの意味は壊れていない。

問題は**監査provenanceの表現だけ**。

### 推奨

以下のように分ける:

```text
tested_source_head:
24157d40...

current_repository_head:
232c50c...

delta_after_tested_source_head:
documentation only
```

完遂後再監査では、production/source/workflowの実証HEADと、後続の監査書artifactを分離した。

```text
tested_source/workflow_head:
6cd32fc34d4a41d7797c5823ee62ca11a9efc467

BSD strict workflow:
31823333053 / success

Ubuntu regression:
31824256449 / success

macOS regression:
31824256438 / success

Windows regression:
31824255781 / success

report artifact:
tested source/workflow HEADの後続documentation-only commit
```

したがって、今回の最終報告は「現在のreport commitが実装検証対象HEADと同一」とは主張せず、実装検証対象を明示した。

**P4-3: 初回監査時 OPEN / 完遂後再監査 CLOSED**

---

# 24. runtime smoke

現状:

```text
CI software Vulkan presentation smoke:
    NOT RUN

physical FreeBSD GPU:
    NOT TESTED

physical NetBSD GPU:
    NOT TESTED

physical OpenBSD GPU:
    NOT TESTED
```

これは文書にも明記されている。

元Definition of Doneではsoftware Vulkan runtime smokeは:

```text
possibleなら実施
```

で必須ではない。

physical GPU runtimeも依頼者が環境を持たない前提から必須ではない。

したがってこれをP2/P3 defectには数えない。

---

# 25. source severity（完遂後再監査）

今回の差分および前回実装全体を再確認した範囲:

```text
P0:
    none

P1:
    none

P2 production:
    none found

P3:
    0 open / 2 closed

P4:
    0 open / 3 closed
```

初回監査で検出した5件のP3/P4 closure gateは、実装変更・workflow変更・対象HEADのCI実証によりすべてCLOSEDとなった。

P3はいずれも初回時点ではCI validation coverageの問題であり、完遂後はQt version assertとproduction loader actual-loadで解消した。

現時点で確認されたBSD WSI production crash/UB/compile defectではない。

---

# 26. Definition of Done再評価

## Source

```text
[x] BSD adapter
[x] BSD 3 OS explicit guard
[x] Qt private QPAなし
[x] Qt 6.5+ public native interface path
[x] old Qt explicit unsupported
[x] XCB
[x] Xlib fallback
[x] unknown plugin fail
[x] Linux WSI回帰なし
[x] Windows/macOS source routing回帰なし
```

## Build

```text
[x] FreeBSD Vulkan ON
[x] NetBSD Vulkan ON
[x] OpenBSD Vulkan ON

[x] FreeBSD Vulkan OFF
[x] NetBSD Vulkan OFF
[x] OpenBSD Vulkan OFF
```

## Automated Vulkan tests

```text
[x] FreeBSD present timing
[x] NetBSD present timing
[x] OpenBSD present timing

[x] FreeBSD pacer fake-dispatch
[x] NetBSD pacer fake-dispatch
[x] OpenBSD pacer fake-dispatch

[x] shader source sync
```

## Regression workflows

```text
[x] Ubuntu
[x] macOS
[x] Windows
```

## Remaining validation hardening

```text
[x] Qt >= 6.5 explicit CI assert
[x] production loader candidate actual-load test
```

## Runtime

```text
[ ] optional software Vulkan presentation smoke
[ ] physical FreeBSD GPU
[ ] physical NetBSD GPU
[ ] physical OpenBSD GPU
```

---

# 27. 推奨次修正（完遂後再監査）

P3/P4 closureに必要だった5点は完了した。production renderer/sourceに追加の必須修正はない。

残るのは元のDefinition of Doneで任意扱いのruntime検証だけである。

### Residual optional validation

```text
[ ] optional software Vulkan presentation smoke
[ ] physical FreeBSD/NetBSD/OpenBSD GPU runtime
```

この任意項目は今回のP3/P4 closureを再オープンしない。

この5点以外に、
今回の監査で新しいrenderer本体修正要求は見つからなかった。

---

# 28. 初回Push後監査スナップショット（履歴）

## HEADとremote

初回Push後監査時点では、ローカルHEADと対象branchのremote refは一致していた。

```text
local HEAD:
232c50c726b1645852ccf731ab4695fb2729d4a0

origin/develop_remakeVulkan_ver3:
232c50c726b1645852ccf731ab4695fb2729d4a0
```

`git diff --check`も終了コード0で完了した。

## 今回のローカル検証

今回の再監査で再実行したローカル検証は以下のとおり。

```text
audit-platform-scatter-budget.ps1 -Budget 22:
PASS — Platform scatter budget: 21 / 22

compile-shaders.py --check-source-sync:
PASS — source fingerprint
2f20448c7f7840d7c0c26e4b2d9fdcc4024b33a5efb71f7157a274116aa61693
PASS — 111 committed SPIR-V modules match manifest hashes
```

これらは初回Push後監査時点のローカルcheckoutに対する静的検証であり、GitHub Actionsの成功結果や物理BSD GPUのruntime結果を代替しない。

## 作業ツリーの境界

監査時点の未コミット差分は、監査対象のsource/workflowではなく、以下に限定されている。

```text
 D  .codex/melonPrimeDS_BSD_Vulkan対応_実装指示書_2026-08-14_最新ブランチ対応改訂版.md
??  .codex/melonPrimeDS_BSD_Vulkan_232c50c_Push後再監査結果_2026-08-15.md
```

前者の削除は既存の作業ツリー変更として保持し、今回の再監査で復元・変更していない。後者は本再監査書自身であり、source/workflowのpush後状態とは別に扱う。

この節の`232c50c...`は初回Push後監査の履歴スナップショットである。完遂後のCI判定は、後述の`tested_source/workflow_head = 6cd32fc...`を基準とする。

---

# 29. P3/P4 closure再監査

完遂後の対象source/workflow HEADは以下である。

```text
tested_source/workflow_head:
6cd32fc34d4a41d7797c5823ee62ca11a9efc467
```

## BSD strict workflow

[BSD 3OS strict workflow run 31823333053](https://github.com/ag-advania/melonPrimeDS/actions/runs/31823333053)

```text
status: completed
conclusion: success
NetBSD / x86_64: success
FreeBSD / x86_64: success
OpenBSD / x86_64: success
All BSD artifacts: success
```

このrunの各jobで、Qt 6.5+ assert、Vulkan headers、versioned loader candidate、shader source sync、production loader actual-loadを通過した。

## Regression workflows

```text
[Ubuntu run 31824256449](https://github.com/ag-advania/melonPrimeDS/actions/runs/31824256449): success
[macOS run 31824256438](https://github.com/ag-advania/melonPrimeDS/actions/runs/31824256438): success
[Windows run 31824255781](https://github.com/ag-advania/melonPrimeDS/actions/runs/31824255781): success
```

4 workflow runすべてが同じ`6cd32fc34d4a41d7797c5823ee62ca11a9efc467`を`headSha`として実行された。

## Closure result

```text
P3-1 Qt 6.5+ explicit CI assert: CLOSED
P3-2 production Vulkan loader actual-load: CLOSED
P4-1 stale BSD stub comment: CLOSED
P4-2 preflight observability split: CLOSED
P4-3 tested-head provenance wording: CLOSED
```

---

# 30. 最終判定

```text
TESTED SOURCE/WORKFLOW HEAD:
6cd32fc34d4a41d7797c5823ee62ca11a9efc467

BSD STRICT WORKFLOW:
PASS

FREEBSD:
PASS

NETBSD:
PASS

OPENBSD:
PASS

VULKAN ON:
PASS x3

VULKAN OFF:
PASS x3

PACKAGE:
PASS x3

UBUNTU REGRESSION:
PASS

MACOS REGRESSION:
PASS

WINDOWS REGRESSION:
PASS

NEW P2:
NONE FOUND

P3:
0 OPEN / CLOSED (2/2)

P4:
0 OPEN / CLOSED (3/3)

PHYSICAL BSD VULKAN:
NOT TESTED
```

結論:

**BSD Vulkan X11 WSIのbuild/static integrationと、今回指定されたP3/P4 closureは完遂した。**

完遂後の必須監査項目は、

```text
Qt 6.5+をCIで明示証明: CLOSED
production Vulkan loaderを実装経由で実際にOpen: CLOSED
P4 provenance/observability/documentation: CLOSED
```

physical BSD GPU runtimeとsoftware Vulkan presentation smokeは、引き続き別枠のNOT TESTED/NOT RUN項目であり、今回のP3/P4 closureを阻害しない。
