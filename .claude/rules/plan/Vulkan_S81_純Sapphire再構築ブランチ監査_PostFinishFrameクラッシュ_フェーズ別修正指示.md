# Vulkan S81 監査・フェーズ別修正指示書
## `vulkan_sapphire_desktop_rebuild` 再構築ブランチ監査
## First GPU Frame完走後／`NDS::RunFrame()`終端 ACCESS_VIOLATION
## 「純Sapphire完了」判定の是正
## Desktop差分をPlatform Adapterへ限定

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `vulkan_sapphire_desktop_rebuild`  
**監査HEAD:** `fafad722bcd0314502cb8e46cccc7d6325753029`  
**クラッシュ実行commit:** `c640a33c19d055340c7c093b04c3a7fb2baf6579`  
**クラッシュbinary SHA-256:** `298db925f5fc9ef3ddcc418b973b9194ee7ed5ee3dfba23e98fb947255500a5a`  
**クラッシュbuild状態:** `dirty=1`  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire frontend commit:** `2c10e59d7209d354e90d9ef4228330bac3f6e794`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 監査結論

再構築方針そのものは正しい。

旧Vulkan実装を継ぎ足し修正する代わりに、

```text
固定Sapphire core
        +
Desktop platform adapter
```

へ戻す判断は妥当。

ただし、現在のブランチはまだ次の状態。

```text
純Sapphire core
        +
旧MelonPrime Desktop Vulkan pipeline
        +
rebuild用compile-time bypass
```

つまり、

```text
旧実装を削除して純Sapphireへ置換した
```

のではなく、

```text
旧実装を多く残したまま
MELONPRIME_SAPPHIRE_REBUILDで一部を迂回している
```

段階。

したがって進捗表の:

```text
Phase 1 純Sapphire core: done
Phase 3 Sapphire output接続: done
Phase 5 旧実装削除: done
```

は、現在のproduction source構成と一致しない。

特にPhase 5 commit `fafad722b`は、
旧実装の削除ではなく、次の説明ファイルを追加しただけ。

```text
VulkanReference_LegacyCustom/ARCHIVED.md
```

CMakeから実際に大量の旧Desktop統合コードを外したcommitではない。

進捗は次へ戻すべき。

| Phase | 正しい状態 |
|---|---|
| 0 現状凍結 | done |
| 1 純Sapphire core | **partial** |
| 2 最小Desktop WSI + 単色clear | done |
| 3 Sapphire output最小接続 | **partial** |
| 4 ROM cold-start | **blocked** |
| 5 機能復元・旧実装削除 | **not started / blocked** |

---

# 1. 最新HEADと実行binary

## 1.1 branch HEAD

最新branch進捗:

```text
Phase 0: 1360cc76e
Phase 1: e95b8d40f
Phase 2: b4557998f
Phase 3: c640a33c1
Phase 4: blocked
Phase 5: fafad722b
```

最新HEAD:

```text
fafad722bcd0314502cb8e46cccc7d6325753029
```

## 1.2 実行binary

添付ログの実行binary:

```text
commit=c640a33c19d0
branch=vulkan_sapphire_desktop_rebuild
dirty=1
```

したがって今回のクラッシュbinaryは:

```text
- 最新HEADより2 commits前
- commitそのものとも一致しないdirty build
```

である。

`c640a33c → fafad722b`の2 commitsは主に:

```text
- Phase 4説明文書
- workflow verify 1行
- archived marker
- 進捗表更新
```

なので、クラッシュ修正は含まれない。

ただし厳密な監査ではdirty buildをcommit sourceと同一扱いしてはならない。

---

# 2. クラッシュ位置の更新

以前の:

```text
Unit B
HBlank
DMA
event scheduling
```

という候補は、今回のログで除外できる。

## 2.1 初回GPU frameは完走

添付ログで確認済み:

```text
StartFrame
scanline 0..191
Unit A DrawScanline
Unit B DrawScanline
Unit A DrawSprites
Unit B DrawSprites
HBlank DMA
StartScanline
VBlank
FinishFrame enter
FinishFrame done
```

最後のconsole:

```text
[FirstGpuLine] FinishFrame enter lines=263
[FirstGpuLine] FinishFrame done lines=263 TotalScanlines=263
ACCESS_VIOLATION
```

したがって:

```text
Sapphire GPU2Dの初回frame描画本体
```

は完走している。

GPU2Dへ新しい描画heuristicやUnit B workaroundを追加してはいけない。

---

# 3. crash report 1
# Emulation thread／RunFrame終端family

対象:

```text
melonPrimeDS-c640a33c19d0-run-124468152238081.crash.txt
```

## 3.1 exception

```text
exception.code = 0xC0000005
accessKind = read
faultAddress = 0xFFFFFFFFFFFFFFFF
RVA = 0x9F10
RAX = 0xFFFFFFFFFFFFFFFF
RSI = 0xFFFFFFFFFFFFFFFF
```

## 3.2 trace ring末尾

```text
FinishFrame enter
FinishFrame done
event-after LCD_FinishFrame
after-cpu-jit-slice
ACCESS_VIOLATION
```

重要:

`event-after`があるため、
`GPU::FinishFrame()` callbackはreturnしている。

`after-cpu-jit-slice`もあるため、
`NDS::RunSystem()`もreturnしている。

現在の最小候補区間:

```text
NDS::RunFrame inner loop末尾
    ↓
CPUStop sleep判定
    ↓
TotalScanlines確認
    ↓
outer while break
    ↓
SPU.BufferAudio()
    ↓
NumFrames++
    ↓
NumLagFrames++
    ↓
GPU.TotalScanlines return
    ↓
EmuThreadのfirst RunFrame complete log
```

`first RunFrame complete`は出ていない。

したがって最優先候補は:

```text
SPU.BufferAudio()
frame counter更新
RunFrame return境界
stack/return address破壊
```

である。

RVA symbolizationなしでどれかを断定してはならない。

---

# 4. crash report 2
# 別thread／別crash family

対象:

```text
melonPrimeDS-c640a33c19d0-run-10496900071425.crash.txt
```

## 4.1 exception

```text
exception.code = 0xC0000005
accessKind = read
faultAddress = 0x0000000002C4EEF0
RVA = 0x13EBE75
traceRing.endSequence = 0
```

## 4.2 判定

同一binaryだが:

```text
- RVAが異なる
- fault addressが異なる
- trace ringが空
- thread IDが異なる
```

ため、report 1と同じ原因としてまとめてはいけない。

可能性:

```text
GUI thread
presenter thread
Qt event thread
audio thread
secondary crash during shutdown/crash handling
```

現reportにはthread role/nameがないため未確定。

S81では2系統へ分離して追跡する。

---

# 5. P0
# rebuild build scriptがexact pinned GPU2Dを無効化

rebuild用script:

```bat
-DMELONPRIME_SAPPHIRE_REBUILD=ON
-DMELONPRIME_SAPPHIRE_REBUILD_SOLID_COLOR=OFF
-DMELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=OFF
```

`src/CMakeLists.txt`:

```cmake
OFF:
    SapphireGPU2DCore/GPU2D_Soft.cpp

ON:
    SapphireVendor/upstream/melonDS-android-lib/src/GPU2D_Soft.cpp
```

つまり「純Sapphire再構築」用scriptが、
純Sapphire pinned sourceを明示的にOFFにしている。

これはPhase 1完了条件と矛盾。

## 修正

rebuild production buildでは:

```text
MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=ON
```

を必須にする。

normalized sourceを残す場合は:

```text
diagnostic comparison target
```

に限定する。

---

# 6. P0
# rebuild scriptが既存build treeを再利用

現在:

```text
build/release-mingw-x86_64
```

を再configureし、incremental buildしている。

危険:

```text
- highres_fonts_v3 object残存
- compile definition切替漏れ
- unity/PCH/MOC生成物残存
- old generated source残存
- LTO cache
- stale Qt object
```

今回のbuildは`dirty=1`でもある。

## 修正

専用clean build tree:

```text
build/rebuild-mingw-release
build/rebuild-mingw-debug
build/rebuild-linux-debug
build/rebuild-linux-asan
```

rebuild監査時は既存treeを使用禁止。

---

# 7. P0
# build identity生成失敗がwarning

build script:

```bat
python generate_build_identity.py
if errorlevel 1 (
    WARNING
    continuing
)
```

再現binaryをcommit-addressableにする要件と矛盾。

## 修正

```text
identity生成失敗 = build failure
dirty=1 = audit/CI failure
commit unknown = build failure
```

ローカル開発用dirty buildを許す場合も、
正式cold-start artifactとしては受理しない。

---

# 8. P0
# CIがrebuild branchを検証していない

workflow push branch:

```yaml
highres_fonts_v3
main
```

欠落:

```yaml
vulkan_sapphire_desktop_rebuild
```

さらにbuild jobsは:

```text
MELONPRIME_ENABLE_VULKAN=ON
```

だけで、

```text
MELONPRIME_SAPPHIRE_REBUILD=ON
MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=ON
```

を指定していない。

Windows jobも通常の:

```text
build-mingw.bat
```

を呼び、rebuild scriptを呼んでいない。

したがって現在のCIは:

```text
rebuild branchのrebuild production path
```

を検証していない。

combined statusとworkflow runも監査HEADで確認できない。

---

# 9. P0
# Phase 5「旧実装削除」は未達

最新CMakeのrebuild pathでも次をproductionへlinkしている。

```text
MelonPrimeScreenVulkan
MelonPrimeVulkanFrontendSession
MelonPrimeVulkanDebugIsolation
MelonPrimeDesktopVulkanPresenter
MelonPrimeVulkanRuntimePacing
VulkanPreparedContentStats
SapphireVulkanFrameLatch
DesktopVulkanResourceLease
MelonPrimeSapphireFrameInput
SapphireVulkanFramePipeline
VulkanReference/VulkanOutput
VulkanReference/VulkanSurfacePresenter
VulkanReference/FrameQueue
```

除外されたのは主に:

```text
DesktopFrameLifetimeTracker
```

だけ。

`ARCHIVED.md`を追加しても、
上記production pathは旧統合architectureのまま。

Phase 5完了条件は:

```text
CMake target_sourcesから旧統合sourceを除去
```

であり、説明ファイル追加ではない。

---

# 10. P0
# GPU ownershipがまだ純Sapphireではない

固定Sapphire GPU:

```text
GPU
 ├─ GPU2D Unit A
 ├─ GPU2D Unit B
 ├─ GPU2D Renderer2D
 ├─ GPU3D renderer
 └─ framebuffer
```

現在のrebuild GPU:

```text
GPU
 ├─ Sapphire Unit A/B
 ├─ Sapphire GPU2D Renderer
 ├─ SapphireGpu2DState
 ├─ SapphireVulkan2DAccess
 ├─ old outer Renderer Rend
 ├─ old Renderer-owned 3D bridge
 ├─ current GPU3D renderer
 └─ framebuffer
```

残っているfield:

```text
Rend
ActiveGPU2DPath
Sapphire2D
GPU2D_Renderer
SapphireVulkan2DAccess
```

`PublishSapphire2DFrameIfReady()`も:

```cpp
dynamic_cast<SoftRenderer*>(Rend.get())
    -> PublishSapphire2DFrame()
```

としてouter Rendererを経由する。

これは純Sapphire GPU ownershipではない。

---

# 11. P0
# VBlank／Finish lifecycleもouter Rendererを経由

VBlank時:

```text
Rend->VCount1443D()
Rend->Finish3DRendering()
Rend->VBlank()
```

FinishFrame後のAbortFrame処理:

```text
Rend->Restart3DRendering()
```

GPU2DはSapphireだが、
3D lifecycleは旧outer Renderer interfaceを介している。

今回のcrashはGPU frame完了後なので、
この混成境界は特に重要。

ただしtrace上`FinishFrame callback`はreturnしているため、
現在の直接faultはさらに後。

architecture修正とdirect crash修正を混同しない。

---

# 12. P0
# Phase 3が最小接続ではない

現在の`MelonPrimeVulkanFrontendSession`はrebuildでも:

```text
runtime pacing
temporal history
resource lease
generation resync
previous frame references
pre-run 3D snapshot
structured capture gate
VulkanPreparedContentStats
Desktop frame sidecar
```

を保持する。

`SapphirePipelineMode`は常に:

```text
SapphireExact
temporal enabled
```

を返す。

したがってrebuild Phase 3は:

```text
最小Sapphire output接続
```

ではなく:

```text
旧Desktop full pipelineへ
Sapphire outputを再接続
```

している。

cold-start原因を狭める目的に反する。

---

# 13. P1
# atomic inputがraw borrowed pointer

Phase 3で追加:

```cpp
const u32* packedTop;
const u32* packedBottom;
```

これらはpublished framebufferへのborrowed pointer。

問題:

```text
- lifetime ownerがtypeに現れない
- FrontBuffer swap後の有効期間が不明
- queueへ保存された場合のimmutability不明
- producer/presenter thread間でのownership不明
```

現時点ではFrameLatchが同期的にcopyするなら成立するが、
契約を明示する必要がある。

推奨:

```cpp
struct SapphireCompletedFrameView
{
    std::span<const u32> packedTop;
    std::span<const u32> packedBottom;
    u64 frameSerial;
    int frontBuffer;
    bool screenSwap;
    FramebufferLease lease;
};
```

またはFrameLatchへ渡す前に、
Sapphire exact snapshotへ直ちにcopyし、
queueへraw framebuffer pointerを残さない。

---

# 14. P1
# FrameQueueはcore exactでもwrapperは非exact

rebuildではDesktop lifetime trackerを迂回し、
selectionはgenerated Sapphire coreへdelegateする。

これは改善。

ただしwrapperには次が残る。

```text
custom clear()
private core field access
Desktop-added Frame fields reset
historyReferences
presentationReferences
FrameQueueState
rendererGeneration
surfaceGeneration
Vulkan resource fields
membership invariant
no-op Desktop compatibility methods
```

純Sapphire化の最終形は:

```text
pinned Sapphire FrameQueue classをそのままcompile
```

または:

```text
public APIだけを呼ぶ薄いadapter
```

である。

private containerへfriend accessするwrapperを
production exact pathに残さない。

---

# 15. P1
# generator対象がFrameQueue／FrameLatchだけ

統一generator:

```text
generate_sapphire_frame_queue.py
generate_sapphire_frame_latch.py
```

だけを実行する。

未対象:

```text
GPU lifecycle
GPU2D Unit
GPU2D SoftRenderer
GPU3D Vulkan
VulkanOutput全体
VulkanSurfacePresenter全体
shader source全体
```

したがってファイルコメントの:

```text
all pinned Sapphire desktop generators
```

は実態より広い。

## 修正

次のいずれか。

### 推奨

generator対象を本当に全sourceへ拡大。

### 最小

名前を:

```text
generate_sapphire_queue_and_latch.py
```

へ変更し、exact保証範囲を明記。

---

# 16. P1
# VulkanOutput exact testは3関数regionのみ

現在比較する主要function:

```text
prepareFrameForPresentation
updatePreparedCapture3dSource
buildCompositionInputs
```

良い点:

```text
- snapshot struct比較
- platform barrier normalization
- pinned upstream region hash比較
```

不足:

```text
- dispatchCompositor body exact
- resource creation/destruction
- descriptor update
- temporal resource rotation
- capture resource lifecycle
- complete file inventory
```

ただし、VulkanOutput内の複雑なcomposition helperの多くは
Sapphire本家にも存在する。

それらをMelonPrime独自heuristicとして無条件削除してはいけない。

必要なのは削除ではなく:

```text
upstream同一かの証明
```

である。

---

# 17. P1
# generation manifest自体をverifyしていない

FrameQueue generatorの`--verify`は:

```text
generated .h
generated .cpp
```

をbyte compareする。

しかし:

```text
SapphireFrameQueueGenerationManifest.json
```

自体はtemp生成して比較していない。

manifest改変・古いhash残存を検出できない。

修正:

```text
generated H
generated CPP
generation manifest
vendor manifest
upstream region SHA
generator SHA
```

を全てverify。

---

# 18. 現在の直接クラッシュ修正フェーズ

---

## Phase S81-0 — 進捗表の是正

### 作業

```text
Phase 1: partial
Phase 3: partial
Phase 4: blocked
Phase 5: blocked/not started
```

へ変更。

### 禁止

```text
ARCHIVED.md追加だけで旧実装削除done
```

### 推奨コミット

```text
S81-0: Correct Sapphire rebuild phase completion status
```

---

## Phase S81-1 — clean exact rebuildを固定

### 作業

専用tree:

```text
build/rebuild-mingw-release
```

必須flags:

```text
MELONPRIME_ENABLE_VULKAN=ON
MELONPRIME_SAPPHIRE_REBUILD=ON
MELONPRIME_SAPPHIRE_REBUILD_SOLID_COLOR=OFF
MELONPRIME_SAPPHIRE_REBUILD_FEATURES=OFF
MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=ON
MELONPRIME_DIAGNOSTIC_SYMBOLS=ON
ENABLE_LTO_RELEASE=OFF
```

必須条件:

```text
clean configure
dirty=0
commit known
binary SHA
map file
debug file
```

### 推奨コミット

```text
S81-1: Build the rebuild branch from a clean exact-pinned tree
```

---

## Phase S81-2 — `RunFrame()`終端trace

### 対象

```text
src/NDS.cpp
src/frontend/qt_sdl/EmuThread.cpp
```

### 追加trace

```text
before CPUStop sleep check
after CPUStop sleep check
before inner loop exit
after inner loop exit
before SPU.BufferAudio
after SPU.BufferAudio
before NumFrames increment
after NumFrames increment
before RunFrame return
after RunFrame call in EmuThread
```

ring recordに:

```text
thread ID
CPU mode
GPU.TotalScanlines
Running
CPUStop
NumFrames
SPU pointer
```

を入れる。

### 完了条件

RVA symbolization前でも、
fault区間を1処理へ限定。

### 推奨コミット

```text
S81-2: Trace the NDS RunFrame epilogue after Sapphire FinishFrame
```

---

## Phase S81-3 — 2 crash familyを分離

### 作業

crash handlerへ:

```text
thread name
thread role
Qt thread object name
emulation thread ID
GUI thread ID
audio thread ID
presenter thread ID
```

を保存。

report名とconsoleを同一run IDでbundle。

### 完了条件

```text
RVA 0x9F10 family
RVA 0x13EBE75 family
```

を別issue／別testへ分離。

### 推奨コミット

```text
S81-3: Classify Vulkan cold-start crashes by thread and fault family
```

---

## Phase S81-4 — symbolization

### 必須RVA

report 1:

```text
0x9F10
0x1C010E
0x1C56B9
0x698C3
0x6D672
```

report 2:

```text
0x13EBE75
0x13EBA93
0x1140C1F
...
```

同一SHAの:

```text
exe
debug file
linker map
```

で`addr2line`。

### 完了条件

各#00が:

```text
function
file
line
```

へ解決。

### 推奨コミット

```text
S81-4: Symbolize both Sapphire rebuild cold-start crash families
```

---

## Phase S81-5 — owner層だけ修正

### 条件分岐

#### SPU.BufferAudioなら

audio側を修正。

GPU/Vulkanへworkaroundを入れない。

#### RunFrame return／stack corruptionなら

ASan、stack protector、canaryで破壊元を遡る。

#### Qt/GUI threadなら

presenter／surface lifecycle側を修正。

#### old outer Rendererなら

rebuild architecture削除Phaseへ統合。

### 禁止

```text
first frame skip
Vulkan fallback
GPU2D line skip
exception握り潰し
```

### 推奨コミット

```text
S81-5: Fix the symbolized post-FinishFrame fault in its owning subsystem
```

---

# 19. 純Sapphire architecture修正フェーズ

---

## Phase S81-6 — GPU ownershipをSapphireへ統一

### 目標

rebuild compileでは:

```text
Rend
ActiveGPU2DPath
SapphireGpu2DState
SapphireVulkan2DAccess
```

をGPU2D lifecycleから除去。

### 構造

```text
GPU
 ├─ pinned Unit A
 ├─ pinned Unit B
 ├─ pinned Renderer2D
 ├─ pinned/compatible Renderer3D
 └─ pinned framebuffer lifecycle
```

### publication

outer `SoftRenderer`へのdynamic_castを廃止。

GPU frame completionから直接:

```text
Sapphire completed frame
```

をpublish。

### 推奨コミット

```text
S81-6: Make pinned Sapphire GPU ownership canonical in rebuild builds
```

---

## Phase S81-7 — rebuild専用最小session

### 現在除去するもの

cold-start greenまでは:

```text
runtime pacing
temporal debug gates
Desktop history synchronization
VulkanPreparedContentStats
pre-run snapshot
Custom HUD
feature layout
live renderer switching
```

をproduction transactionから外す。

### 最小session

```text
one FrameQueue
one FrameLatch
one VulkanOutput
one Presenter
one immutable completed frame input
```

### 注意

Sapphire本家にあるtemporal semanticsは残す。

Desktop独自temporal wrapperだけを外す。

### 推奨コミット

```text
S81-7: Add a minimal pinned-Sapphire desktop producer/presenter session
```

---

## Phase S81-8 — exact FrameQueueを直接compile

### 作業

rebuildではwrapperのcustom `clear()`とprivate accessを使用しない。

次のいずれか。

```text
A. pinned Sapphire FrameQueueを直接compile
B. generated exact classをpublic APIのみで使用
```

### 完了条件

```text
Desktop friend access = 0
custom queue membership mutation = 0
```

### 推奨コミット

```text
S81-8: Remove the Desktop FrameQueue wrapper from rebuild production
```

---

## Phase S81-9 — atomic input ownership

### 作業

raw pointer追加だけで完了扱いにしない。

契約:

```text
- source framebuffer owner
- immutable期間
- latch copy完了時点
- thread ownership
- generation
```

をtypeへ反映。

### 完了条件

```text
queue/presenterがraw GPU framebuffer pointerを保持しない
```

### 推奨コミット

```text
S81-9: Give Sapphire completed-frame input an explicit immutable lifetime
```

---

## Phase S81-10 — source verification拡張

### verify対象

```text
GPU2D Unit
GPU2D SoftRenderer
GPU lifecycle regions
FrameQueue
FrameLatch
VulkanOutput
VulkanSurfacePresenter core
shader sources
all generation manifests
```

### 許容差

```text
namespace
include path
volk
WSI
barrier helper
logging
filesystem
```

### 禁止差

```text
algorithm
ownership
queue order
capture
screen swap
temporal choice
composition mode
```

### 推奨コミット

```text
S81-10: Verify the complete pinned-Sapphire Vulkan dependency closure
```

---

# 20. CI修正フェーズ

## Phase S81-11 — rebuild branch trigger

追加:

```yaml
push:
  branches:
    - vulkan_sapphire_desktop_rebuild
```

## Phase S81-12 — rebuild configure

全rebuild jobへ:

```text
MELONPRIME_SAPPHIRE_REBUILD=ON
MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=ON
MELONPRIME_SAPPHIRE_REBUILD_FEATURES=OFF
```

を指定。

Windowsでは:

```text
build-mingw-vulkan-sapphire-rebuild.bat
```

のclean版を使用。

## Phase S81-13 — cold-start matrix

```text
Windows Release exact
Windows Debug exact
Linux Debug exact
Linux ASan/UBSan exact
```

MSanは依存tree未instrumentならgreen扱いしない。

## 完了条件

```text
first RunFrame complete
producer completion
queuePush
surfacePresent=1
splashHidden=1
exit code 0
dirty=0
```

---

# 21. Phase 5を開始できる条件

以下が全てgreenになるまで、
HUD、fullscreen、filter、renderer switchingを戻さない。

```text
clean exact build
RunFrame complete
first present
Windows Debug/Release
Linux sanitizer
Sapphire parity
crash family 0件
```

その後:

```text
layout
fullscreen
scaling
filter
HUD
renderer switching
```

を1commitずつ復元。

---

# 22. 推奨commit順

```text
S81-0  Correct phase completion status
S81-1  Clean exact-pinned rebuild
S81-2  RunFrame epilogue trace
S81-3  Thread/crash family classification
S81-4  Symbolization
S81-5  Direct fault fix
S81-6  Canonical Sapphire GPU ownership
S81-7  Minimal rebuild session
S81-8  Direct exact FrameQueue
S81-9  Immutable frame input lifetime
S81-10 Complete source verification
S81-11 Rebuild branch CI trigger
S81-12 Exact rebuild CI flags
S81-13 Cold-start matrix
```

---

# 23. 禁止事項

```text
- dirty buildをcommit exactとして監査
- exact pin OFFで純Sapphire完了扱い
- ARCHIVED.mdだけで旧実装削除完了扱い
- Phase 4 redのままPhase 5 done
- GPU2Dへ新しいクラッシュ回避heuristic
- Unit B skip
- first frame skip
- Software fallback
- ACCESS_VIOLATION握り潰し
- 2 crash reportを同一原因に統合
- symbolization前に複数subsystemを同時修正
- Sapphire本家にもあるcomposition logicを独自heuristicと誤認して削除
```

---

# 24. 最終完了条件

## Build

```text
dirty = 0
exact pin = ON
dedicated clean build tree
known commit
symbols/map保存
```

## Architecture

```text
pinned Sapphire GPU ownership
pinned Sapphire FrameQueue
pinned Sapphire FrameLatch
pinned Sapphire VulkanOutput core
Desktop差分はWSI/loader/window/resource lifetimeのみ
```

## Runtime

```text
all 263 lines complete
NDS::RunFrame returns
producer completion succeeds
queuePush succeeds
first present succeeds
splash hidden
```

## CI

```text
rebuild branch trigger
rebuild flags ON
Windows Release green
Windows Debug green
Linux Debug green
ASan/UBSan green
```

## Progress

```text
Phase 4 green後にのみPhase 5開始
```

---

# 25. 最終判断

今回の再構築により、重要なことが1つ確定した。

```text
初回GPU2D frameは正常に最後まで描画できる
```

したがって、現在の即落ちをGPU2D描画本体のせいにして
再度作り直す必要はない。

現在の直接faultは:

```text
FinishFrame callback return後
NDS::RunFrame終端
```

にある。

一方、architecture監査では:

```text
exact pin OFF
old frontend session残存
old outer Renderer残存
rebuild CI未実行
Phase 5未削除
```

が確認された。

よって次の正しい順序は:

```text
1. clean exact buildへ固定
2. RunFrame epilogueをsymbolize
3. direct crashをowner層で修正
4. old Desktop integrationを本当に除去
5. rebuild CIをgreen化
6. その後にDesktop機能を戻す
```

である。

再構築方針は継続してよいが、
現在の完了判定は巻き戻す必要がある。
