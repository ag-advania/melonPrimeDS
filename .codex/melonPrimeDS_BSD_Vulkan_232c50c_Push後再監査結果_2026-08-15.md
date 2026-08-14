# melonPrimeDS BSD Vulkan対応 Push後再監査結果

- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver3`
- 前回中間監査基準HEAD: `15cf59a56c40941189e6be0c39f3290d378789aa`
- 今回最新HEAD: `232c50c726b1645852ccf731ab4695fb2729d4a0`
- 最新HEAD message: `Record final BSD and platform workflow results`
- production / workflow最終実行対象HEAD: `24157d40f0723a090a55117448973887c650b8dd`
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
    2 OPEN

P4:
    3 OPEN

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

BUT
2 P3 VALIDATION GAPS REMAIN
3 P4 DOCUMENTATION / CI-OBSERVABILITY GAPS REMAIN

PHYSICAL BSD VULKAN RUNTIME IS NOT VERIFIED
```

今回のBSD Vulkan対応は、**「BSD 3種でVulkan-enabled buildが成立する」段階までは実証済み**。

一方、

```text
実BSD + physical GPU + X11上で
実際にVkSurfaceKHRを生成しswapchain presentまで成功
```

した証拠はまだ無い。

これは元のDefinition of Doneでも必須ではないため、今回のbuild/static対応を否定するものではない。

---

# 2. HEAD確認

最新branch HEAD:

```text
232c50c726b1645852ccf731ab4695fb2729d4a0
Record final BSD and platform workflow results
```

親:

```text
24157d40f0723a090a55117448973887c650b8dd
Keep BSD stub guard outside scatter budget
```

`15cf59a...`から最新HEADまで:

```text
ahead_by = 5
behind_by = 0
```

変更ファイルは最終的に3ファイルのみ:

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

したがってproduction WSI implementationの静的評価は前回結果を維持できる。

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
```

実装本体の修正ではなく、

```text
NetBSD CI portability修正
↓
CI結果記録
↓
scatter audit対応
↓
最終回帰結果記録
```

という流れ。

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

# 19. 前回P3-1: Qt 6.5+ CI assert

前回指摘:

```text
P3
Qt 6.5+ CI assert欠落
```

状態:

```text
OPEN
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

**P3-1: OPEN**

---

# 20. 前回P3-2: production Vulkan loader candidate actual-load

前回指摘:

```text
P3
production loader candidate actual-load検証欠落
```

状態:

```text
OPEN
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

**P3-2: OPEN**

---

# 21. P4-1: Stub comment stale

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

**P4-1: OPEN**

---

# 22. P4-2: preflight observability

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

**P4-2: OPEN**

---

# 23. P4-3: current-HEAD wording / evidence provenance

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

**P4-3: OPEN**

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

# 25. source severity

今回の差分および前回実装全体を再確認した範囲:

```text
P0:
    none

P1:
    none

P2 production:
    none found

P3:
    2 open

P4:
    3 open
```

P3はいずれもCI validation coverageの問題。

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
[ ] Qt >= 6.5 explicit CI assert
[ ] production loader candidate actual-load test
```

## Runtime

```text
[ ] optional software Vulkan presentation smoke
[ ] physical FreeBSD GPU
[ ] physical NetBSD GPU
[ ] physical OpenBSD GPU
```

---

# 27. 推奨次修正

production renderer/sourceに今すぐ大きな変更は不要。

次pushでは以下だけでよい。

### Required to close P3

```text
1.
BSD 3 OSでQt versionをlog + >=6.5 assert

2.
production Vulkan loader candidate
libvulkan.so.1 / libvulkan.so
のactual dlopen test
```

### Small P4 cleanup

```text
3.
Stub冒頭commentの"currently the BSDs"を修正

4.
preflight stepを分割

5.
tested_source_headとcurrent_repository_headを文書で分離
```

この5点以外に、
今回の監査で新しいrenderer本体修正要求は見つからなかった。

---

# 28. 再監査時点のローカル／Git境界

## HEADとremote

再監査時点のローカルHEADと、対象branchのremote refは一致している。

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

これらは現在のローカルcheckoutに対する静的検証であり、GitHub Actionsの成功結果や物理BSD GPUのruntime結果を代替しない。

## 作業ツリーの境界

監査時点の未コミット差分は、監査対象のsource/workflowではなく、以下に限定されている。

```text
 D  .codex/melonPrimeDS_BSD_Vulkan対応_実装指示書_2026-08-14_最新ブランチ対応改訂版.md
??  .codex/melonPrimeDS_BSD_Vulkan_232c50c_Push後再監査結果_2026-08-15.md
```

前者の削除は既存の作業ツリー変更として保持し、今回の再監査で復元・変更していない。後者は本再監査書自身であり、source/workflowのpush後状態とは別に扱う。

したがって、本書のCI判定は引き続き`tested_source_head = 24157d40...`、リポジトリの現在refは`current_repository_head = 232c50c...`として記録する。

---

# 29. 最終判定

```text
HEAD:
232c50c726b1645852ccf731ab4695fb2729d4a0

TESTED SOURCE/WORKFLOW HEAD:
24157d40f0723a090a55117448973887c650b8dd

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
2 OPEN

P4:
3 OPEN

PHYSICAL BSD VULKAN:
NOT TESTED
```

結論:

**BSD Vulkan X11 WSIのbuild/static integrationは成功と判定してよい。**

ただし最終的な監査品質としては、

```text
Qt 6.5+をCIで明示証明する
production Vulkan loader名を実際にdlopenする
```

の2点を追加すれば、今回のP3は閉じられる。

physical BSD GPU runtimeは引き続き別枠の未検証項目とする。
