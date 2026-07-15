# Vulkan白画面・Sapphire完全準拠移植監査
## 車輪の再発明を避けるための再実装／置換指示書

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査時HEAD:** `cdf244f33d353fe8a0811c40c4467a8770db2f83`  
**確認build:** `melonPrimeDS v3.4.3 v55.exe`  
**参照実装:** `SapphireRhodonite/melonDS-android`  
**参照タグ:** `0.7.0.rc4`  
**参照parent commit:** `2c10e59d7209d354e90d9ef4228330bac3f6e794`  

---

# 0. 結論

現在のMelonPrimeDS Vulkan実装は、Sapphire `0.7.0.rc4`と同一実装ではない。

次の部分はSapphireに近い。

```text
FrameQueueの固定slot方式
VulkanOutputのresource ownership
VulkanSurfacePresenterのswapchain presentation
present成功後のcommit
失敗時のdefer
Vulkan 3D image＋packed 2D bufferによるcompositor構成
```

しかし、白画面の内容を生成する最重要部分は独自実装になっている。

```text
GPU2D_Structured.h
SoftRenderer内のstructured frame ring
CopyStructured2DFrameSnapshot()
MelonPrimeStructuredSnapshot
captureCompletedSnapshot()
buildSoftPackedSnapshot()
producer側composeAndSubmitFrame()
```

この独自経路はSapphireのframe producer／2D latchと等価ではない。

v55ログでは少なくとも120frame以上、

```text
submitCompletedFrame result=1
acquire
present result=1
commit
```

が継続している。

したがって、現在の白画面は主として:

```text
swapchainへ何もpresentできていない
```

問題ではなく、

```text
白または空のcomposition内容を正常にpresentしている
```

問題である。

さらにframe 13付近でARM9 data abort／prefetch abortが発生し、最終的にsegmentation faultになっている。

よって今回の方針は:

```text
現在の独自snapshot変換を追加修正し続けない
Sapphireのproducer／latch／presenter責務をdependency closureごと移植する
Sapphire core submoduleも参照タグが指す正確なcommitへ固定する
Windows／Qt差分だけをadapterとして外側に置く
```

とする。

---

# 1. v55ログから確定したこと

## 1.1 queue／timeline停止は解消済み

v55ではframe IDが:

```text
1
4
5
6
...
120
```

と進行し、各frameで:

```text
present result=1
commit
```

が出ている。

つまり:

```text
FrameQueue枯渇
presenter pointer消失
3frame目timeline deadlock
```

は今回の主原因ではない。

## 1.2 actual Vulkan認定も成立している

最初は:

```text
actual=Software
```

であり、present成功後に:

```text
video actual renderer changed:
actual=Vulkan
```

へ遷移している。

この判定は維持する。

## 1.3 guest CPUは正常に進行していない

frame 13付近で:

```text
ARM9: data abort (02007008)
ARM9: data abort (0200710C)
ARM9: data abort (02007114)
ARM9: data abort (02007118)
ARM9: prefetch abort (9A3B0204)
```

が出ている。

その後もVulkan presenterはframeをpresentし続けているため、

```text
present result=1
```

はゲーム内容の正常性を保証しない。

present成功が意味するのは:

```text
Vulkan command submissionとswapchain presentが成功した
```

ことだけである。

## 1.4 最終的にsegmentation fault

ログ終端は:

```text
Segmentation fault
```

である。

したがって、白画面だけをshader色問題として扱ってはいけない。

次を分離して検証する。

```text
A. 2D／3D composition内容が空になる問題
B. ARM9 abort
C. host segmentation fault
```

---

# 2. Sapphireと同一かどうかの監査結果

| 領域 | 現在のMelonPrime | Sapphire 0.7.0.rc4 | 判定 |
|---|---|---|---|
| 固定FrameQueue | 9 slot、state追加 | 9 slot | 部分一致 |
| queue policy | 単一desktop policy中心 | realtime／late／fast-forward別 | 部分一致 |
| frame取得時期 | `RunFrame()`後に取得 | `RunFrame()`前に取得 | 不一致 |
| front buffer | `frontBufferLatched = 0` | `nds->GPU.FrontBuffer` | 不一致 |
| 2D source | 独自complete-frame snapshot | coreのpacked framebuffer＋structured planes | 不一致 |
| latch | 簡略変換 | 大規模なcapture／temporal処理を含む | 不一致 |
| capture 3D source | 実質未構築 | line mask／fallback／placeholderを構築 | 不一致 |
| previous frame | 単純swap | ownership／capture状態を解析 | 不一致 |
| producer責務 | prepare＋compose＋submit | prepareまで | 不一致 |
| presenter責務 | composed imageを表示 | 必要時にcomposeして表示 | 不一致 |
| debug capture | 独自statsのみ | packed plane／capture mask／meta JSON／burst | 不一致 |
| surface host | Qt／Win32 | ANativeWindow | 意図的差分 |
| layout | affine transform＋HUD | axis-aligned rect | 意図的差分 |
| timeline submit | desktop修正版 | rc4に旧pattern | desktop修正を維持 |

結論:

```text
FrameQueueとVulkan resource classの外形は近いが、
frame内容を作る経路はSapphireと同じではない。
```

---

# 3. 現在の白画面を説明できる具体的な独自実装バグ

これは今回の最重要監査結果。

## 3.1 SoftRendererがregular displayのnative finalを作らない

現在の`GPU_Soft.cpp`では、structured sourceが有効なregular displayについて:

```text
writeCpuFinalA = false
writeCpuFinalB = false
```

になり得る。

その場合:

```text
DrawScanlineA／DrawScanlineBを呼ばない
ExpandColorも呼ばない
```

構造になっている。

一方、`StructuredNativeFinal`は毎scanlineの先頭でゼロclearされる。

`StructuredNativeFinal`へnative outputをcopyするのは、
主にnon-regular display側である。

したがってregular displayでは:

```text
StructuredNativeFinal == 0
```

になり得る。

## 3.2 独自converterが「3D slotなし」をnative finalへ落としている

現在の`buildSoftPackedSnapshot()`は、概略として:

```text
regular structured displayではない
または
3D slotがない
↓
packedPlane0 = nativeFinal
packedPlane1 = 0
```

としている。

ところがNDS起動直後のロゴ、メニュー、2D UIなどは:

```text
regular display
2D BG／OBJのみ
3D slotなし
```

になり得る。

この条件では:

```text
本来有効なPlane0／Plane1／Control
```

を捨てて、

```text
ゼロのStructuredNativeFinal
```

を採用する。

結果:

```text
2D-only regular displayが空frameへ変換される
```

可能性が高い。

これは:

```text
present result=1なのに白／空画面
```

というv55の挙動と一致する。

## 3.3 Sapphireはこの簡略化を行わない

Sapphireのlatchは:

```text
実際のFrontBuffer
coreが生成したpacked framebuffer
structured Vulkan 2D planes
display mode metadata
capture 3D line mask
previous frame
screen swap
capture class
VRAM capture
regular capture
```

をまとめて解析する。

単純に:

```text
3D slotがない
→ nativeFinalへ置換
```

とはしていない。

したがって、この独自converterへ条件分岐を追加し続けるのではなく、
Sapphireのlatch dependency closureをそのまま移植する。

---

# 4. 現在の実装でSapphireと異なる重要点

## 4.1 独自complete-frame snapshot

現在:

```text
SoftRenderer
→ SapphireStructured2DFrameSnapshot
→ GPU::CopyStructured2DFrameSnapshot
→ MelonPrimeStructuredSnapshot
→ SoftPackedFrameSnapshot
```

と複数段階のcopyを行っている。

Sapphire frontendは:

```text
GPU FrontBufferのpacked framebuffer
＋
GPU2D::SoftRendererのstructured plane accessor
```

を直接latchする。

現在の中間型:

```text
MelonPrimeStructuredSnapshot
```

はSapphireに存在しない。

## 4.2 frontBufferを固定値にしている

現在:

```cpp
destination.frontBufferLatched = 0;
```

相当の処理がある。

Sapphireはframe完了時の:

```text
nds->GPU.FrontBuffer
```

を使う。

FrontBufferを固定すると:

```text
framebuffer double buffering
screen swap
capture ownership
previous frame
```

との対応が崩れる。

## 4.3 capture情報が不足

現在の`SoftPackedFrameSnapshot`型にはSapphire由来のfieldが残っているが、
独自builderは次を十分に構築していない。

```text
capture3dSourceDsFrame
captureLineUses3dMask
captureFallbackLines
comp4TopPlaceholder
comp4BottomPlaceholder
captureBackedClass4Only
regular capture flags
VRAM capture flags
exact capture flags
x offset metadata
temporal ownership
```

型だけ同じでも、値が作られていなければ同一実装ではない。

## 4.4 composition ownershipが逆

現在:

```text
EmuThread producer
→ prepareFrameForPresentation
→ buildCompositionInputs
→ composeAndSubmitFrame
→ queueへpush
```

Sapphire:

```text
EmuThread producer
→ prepareFrameForPresentation
→ queueへpush

Presenter
→ build／consume composition inputs
→ 必要ならcomposeAndSubmitFrame
→ surfaceへpresent
```

Sapphireではfallback compositionをpresenterが行う。

現在のproducer-side compositionはSapphireと異なる。

## 4.5 frame取得時期が異なる

現在:

```text
NDS::RunFrame()
→ FrameQueueからframe取得
```

Sapphire:

```text
FrameQueueからframe取得
→ reuse可能性確認
→ resource準備
→ 必要なpre-run snapshot
→ NDS::RunFrame()
→ completed frameをlatch
```

Sapphireはframe resourceのownershipを
`RunFrame()`前から確定する。

これにより:

```text
renderer3D source
previous frame source
capture source
frameId
```

の対応が一つのtransactionになる。

---

# 5. 車輪の再発明を避けるための基本方針

## 5.1 「Sapphire風」を禁止する

今後は次の表現を実装理由にしない。

```text
Sapphire風
Sapphireに近い
Sapphireを参考に簡略化
desktop向けに再設計
```

代わりに各変更を次の三分類にする。

```text
COPY:
Sapphire 0.7.0.rc4から内容をそのまま移植

ADAPT:
OS／window system／core API差だけを薄いadapterで変換

MELONPRIME:
HUD、Qt layout、backend transactionなど固有機能
```

## 5.2 COPY領域へ独自ロジックを混ぜない

COPYしたfile／functionには冒頭へ:

```cpp
// Source: SapphireRhodonite/melonDS-android
// Tag: 0.7.0.rc4
// Parent commit: 2c10e59d7209d354e90d9ef4228330bac3f6e794
// Local adaptations are marked with MELONPRIME_ADAPT.
```

を付ける。

独自変更は必ず:

```cpp
// MELONPRIME_ADAPT_BEGIN
...
// MELONPRIME_ADAPT_END
```

で囲む。

## 5.3 COPY後に整形しない

最初の移植commitでは次を行わない。

```text
命名変更
class分割
helper統合
C++ style変更
Qt型への置換
最適化
不要に見える分岐の削除
debug機能削除
```

Sapphireとdiff可能な状態を維持する。

---

# 6. 正確なSapphire sourceを固定する

Sapphire parent repositoryはcoreをsubmoduleで持つ。

`.gitmodules`だけを見てmoving branch tipを取得してはいけない。

参照タグが指す正確なgitlinkを使用する。

## 6.1 推奨取得手順

```bash
git clone \
  --recursive \
  --branch 0.7.0.rc4 \
  https://github.com/SapphireRhodonite/melonDS-android.git \
  sapphire-melonds-android

cd sapphire-melonds-android

git rev-parse HEAD
git submodule status melonDS-android-lib
git -C melonDS-android-lib rev-parse HEAD
```

期待parent:

```text
2c10e59d7209d354e90d9ef4228330bac3f6e794
```

`git submodule status`が返したSHAを:

```text
SAPPHIRE_CORE_SHA
```

として記録する。

## 6.2 provenance manifestを作る

MelonPrime repoへ:

```text
src/frontend/qt_sdl/VulkanReference/SAPPHIRE_SOURCE_MANIFEST.md
```

を追加する。

内容:

```text
parent repository
parent tag
parent commit
core repository
core gitlink commit
copied source path
destination path
copy status
local adaptation summary
```

## 6.3 moving branchを禁止

次を参照元にしない。

```text
GBARumble_PRの最新HEAD
masterの最新HEAD
release後の新commit
```

必ず`0.7.0.rc4` parentが指すcore gitlinkを使う。

---

# 7. そのまま移植するsource群

# 7.1 Frontend renderer files

以下は、原則としてSapphire tagからfile単位でcopyする。

```text
app/src/main/cpp/renderer/FrameQueue.h
app/src/main/cpp/renderer/FrameQueue.cpp

app/src/main/cpp/renderer/VulkanOutput.h
app/src/main/cpp/renderer/VulkanOutput.cpp

app/src/main/cpp/renderer/VulkanCompositorShader.comp
app/src/main/cpp/renderer/VulkanAccumulate3dShader.comp

app/src/main/cpp/renderer/VulkanSurfacePresenter.h
app/src/main/cpp/renderer/VulkanSurfacePresenter.cpp

app/src/main/cpp/renderer/VulkanSurfacePresenter.vert
app/src/main/cpp/renderer/VulkanSurfacePresenter.frag
```

生成済みSPIR-V headerも、
shader sourceと同じrevisionから再生成する。

古いgenerated headerを残さない。

## 7.2 MelonInstanceのdependency closure

`MelonInstance.cpp`から一部の数十行だけを抜き出して再実装しない。

次のdependency closureをまとめてcopyする。

```text
SoftPackedFrameSnapshot関連constant
packed control／capture flag constant
SoftPackedScreenStats collector
capture／temporal判定helper
queue policy helper
clearLatchedSoftPackedFrameSnapshot()
updateVulkanTemporal3dHistoryGate()
isVulkanTemporal3dHistoryGateActive()
latchSoftPackedFrameSnapshot()
runFrame()内Vulkan transaction
presentVulkanFrame()
requestVulkanPresentationResync()
requestVulkanFastForwardPresentationTransition()
Vulkan debug capture helper
dense burst capture helper
performance／temporal stats helper
```

`latchSoftPackedFrameSnapshot()`は多数のhelperとcache memberに依存する。

関数本体だけをcopyして、
依存分岐を削除してはいけない。

## 7.3 MelonInstance.hのstate members

次のstateもdependencyとして移植する。

```text
lastSoftPackedFrameSnapshot
previousSoftPackedFrameSnapshot

lastValidTopScreenCapture3dDsFrame
lastValidBottomScreenCapture3dDsFrame

lastValidTopScreenResolvedPrimary
lastValidBottomScreenResolvedPrimary
valid line masks

cachedEngineA structured planes
cached atypical display primary
capture transition state
screen swap transition state
temporal history gate state
prepare failure counters
last completed Vulkan frame
last completed Vulkan scale
debug snapshot state
```

これらを省略すると、
latch本体だけをcopyしてもSapphireと等価にならない。

---

# 8. Sapphire core submoduleを正確に移植する

Frontendだけをcopyしても不十分。

Sapphire frontendはcore側の次のAPI／data layoutへ依存している。

```text
GPU.FrontBuffer
GPU.Framebuffer[frontBuffer][screen]
GPU.GetRenderer2D()
GPU2D::SoftRenderer
GetStructuredVulkan2DPlane()
GetDebugCaptureStats()
structured capture metadata
VulkanRenderer3D
capture frame export
RenderScreenSwapAt3D
```

現在のMelonPrimeは不足APIを補うため、
独自の:

```text
GPU2D_Structured.h
SapphireStructured2DFrameSnapshot
CopyStructured2DFrameSnapshot()
SoftRenderer structured ring
```

を追加している。

これはSapphire coreと同一ではない。

## 8.1 core差分を生成する

```bash
SAPPHIRE_CORE_SHA=$(
  git -C sapphire-melonds-android/melonDS-android-lib rev-parse HEAD
)

git -C sapphire-melonds-android/melonDS-android-lib \
  log --oneline --decorate -n 30

git -C sapphire-melonds-android/melonDS-android-lib \
  merge-base \
  <MelonPrimeが基準にしているupstream-melonDS-commit> \
  "$SAPPHIRE_CORE_SHA"
```

そのmerge-baseから:

```bash
git -C sapphire-melonds-android/melonDS-android-lib \
  diff --binary \
  <MERGE_BASE>.."$SAPPHIRE_CORE_SHA" \
  -- src/GPU* src/frontend
```

を取得する。

## 8.2 core patchはfile単位で監査する

最低限:

```text
GPU.h
GPU.cpp
GPU2D.h
GPU2D.cpp
GPU2D_Soft.h
GPU2D_Soft.cpp
GPU3D.h
GPU3D.cpp
GPU3D_Vulkan.h
GPU3D_Vulkan.cpp
GPU3D_Vulkan shader群
GPU renderer ownership関連
```

を比較する。

## 8.3 独自core bridgeを残さない

Sapphire coreに同じ型／関数が存在しない場合、
次は削除候補。

```text
src/GPU2D_Structured.h
GPU::CopyStructured2DFrameSnapshot()
SoftRenderer::BeginStructured2DFrame()
SoftRenderer::SubmitStructured2DLine()
SoftRenderer::EndStructured2DFrame()
StructuredFrames
StructuredLineReceived
StructuredPlane0
StructuredPlane1
StructuredControl
StructuredNativeFinal
```

ただし削除は、
Sapphire core側の正確なpacked／structured出力を移植した後に行う。

---

# 9. 推奨するdesktop adapter構造

Sapphire codeへQt lifecycleを直接混ぜない。

新規:

```text
src/frontend/qt_sdl/SapphireVulkanFramePipeline.h
src/frontend/qt_sdl/SapphireVulkanFramePipeline.cpp
```

を作る。

このclassにはSapphireの:

```text
FrameQueue
VulkanOutput
SoftPacked snapshot state
runFrame transaction
presentVulkanFrame transaction
```

をできるだけ同じ構造で置く。

Qt側:

```text
EmuThread
ScreenPanelVulkan
MelonPrimeVideoBackend
```

は薄いadapterとして呼ぶ。

## 9.1 producer transaction

Sapphireと同じ順序にする。

```text
1. renderer configuration更新
2. FrameQueueからrender frame取得
3. presenter／history reference完了確認
4. frame resource準備
5. 必要ならpre-run renderer3D snapshot
6. NDS::RunFrame()
7. actual FrontBuffer取得
8. actual ScreenSwap取得
9. exact latchSoftPackedFrameSnapshot()
10. prepareFrameForPresentation()
11. valid frameをqueueへpush
```

現在の:

```text
NDS::RunFrame()
→ submitVulkanFrontendFrame()
→ complete snapshotを再copy
```

方式を廃止する。

## 9.2 presenter transaction

Sapphireと同じ順序にする。

```text
1. present candidate取得
2. frame readiness確認
3. buildCompositionInputs
4. direct present判定
5. fallbackならcomposeAndSubmitFrame
6. composition completion待機
7. swapchain acquire
8. surface commands
9. queue submit
10. queue present
11. successならcommit
12. failureならdefer
```

## 9.3 current producer-side compositionを削除

現在の`submitCompletedFrame()`から:

```text
buildCompositionInputs()
composeAndSubmitFrame()
```

を削除する。

producerは:

```text
prepareFrameForPresentation()
pushRenderedFrame()
```

までにする。

---

# 10. Windows／Qt固有として残す領域

次はSapphireからそのままcopyできない。

```text
ANativeWindow attach
Android Vulkan surface creation
JNI lifecycle
Activity／View lifecycle
Android device profile
Adreno tools
EGL／OpenGL legacy presenter
Android file／asset API
```

MelonPrime側で残す。

```text
MelonPrimeVulkanSurfaceHost
Win32 VkSurfaceKHR生成
Qt QWidget native child
Qt resize／DPI
screenMatrix affine transform
HUD solid quad
radar overlay
no-ROM splash overlay
video backend transaction
Windows present queue family選択
pipeline cache path
```

ただしこれらは:

```text
SapphireVulkanFramePipeline
```

の内側へ入れず、

```text
surface／layout adapter
```

として外側に置く。

---

# 11. Sapphireからcopyしつつdesktop修正を維持する箇所

Sapphire `0.7.0.rc4`を完全に無修正でcopyしてはいけない箇所もある。

## 11.1 Timeline semaphore submit

Sapphire rc4の旧実装には:

```text
signalSemaphoreCount = 2
signalSemaphoreValueCount = 1
```

のpatternがある。

現在MelonPrimeで修正した:

```text
binary semaphore value = 0
timeline semaphore value = signalValue
countを一致
```

する修正は維持する。

この変更だけを:

```cpp
// MELONPRIME_ADAPT: Vulkan timeline value array must match signal semaphore count.
```

として明示する。

## 11.2 Present queue family

Windowsではgraphics queueとpresent queueが異なる可能性がある。

現在の:

```text
presentQueue
presentQueueFamilyIndex
separatePresentQueue
queue family ownership barrier
```

は維持する。

## 11.3 swapchain resource retirement

Qt resize／native child再生成に対応するため、
現在のdeferred resource retirementが必要なら維持する。

ただしSapphireのpresenter本体へ散らさず、
desktop surface adapterへ隔離する。

---

# 12. 現在の独自実装から削除するもの

完全移植後、次を削除する。

```text
MelonPrimeStructuredSnapshot
producerStructuredSnapshot()
captureCompletedSnapshot()
buildSoftPackedSnapshot()
collectStats()の独自版
frameInputs mapによるproducer-side composition inputs保存
producer-side composeAndSubmitFrame()
frontBufferLatched固定値
```

core側はSapphire core patchへ置換後:

```text
GPU2D_Structured.h
SapphireStructured2DFrameSnapshot
CopyStructured2DFrameSnapshot()
SoftRenderer内の独自structured frame copy ring
```

を削除する。

「念のためfallbackとして残す」は禁止。

二つのframe生成経路を残すと、
どちらが動作しているか再び不明になる。

---

# 13. 移植前に行う一回限りの内容診断

完全移植の判断を補強するため、
現在のv55へ最小限の診断だけを追加してよい。

新しい独自debug systemは作らない。

Sapphireの既存debug helperをcopyして使う。

## 13.1 capture対象

frame:

```text
1
5
13
30
60
120
```

について:

```text
packedTopPlane0
packedTopPlane1
packedTopControl
packedTopLineMeta

packedBottomPlane0
packedBottomPlane1
packedBottomControl
packedBottomLineMeta

renderer3D source
composed atlas
capture3dSource
captureLineUses3dMask
SoftPacked meta JSON
```

を保存する。

## 13.2 判定

### packed dataが空

```text
latch／core structured output不具合
```

### packed dataは正常、composed atlasが空

```text
VulkanOutput／compositor shader不具合
```

### composed atlasは正常、windowだけ白

```text
presenter descriptor／UV／layout不具合
```

### ARM9 abort前は正常、abort後から空

```text
guest CPU／memory corruptionが一次原因
```

## 13.3 CRCだけで済ませない

CRCが毎frame変化していても、
全画面whiteに少量の変化があるだけかもしれない。

最低限:

```text
nonzero pixel count
visible RGB pixel count
opaque black count
white count
plane別count
display mode count
comp mode count
```

を出す。

---

# 14. ARM9 abort／segmentation faultの分離

Sapphire完全準拠移植と同じcommitで、
ARM9／JITコードを修正しない。

## 14.1 比較matrix

同一ROM、同一save、同一設定で:

| Test | Renderer | JIT | Sapphire exact core path |
|---|---|---|---|
| A | Software | ON | N/A |
| B | Software | OFF | N/A |
| C | Vulkan | ON | OFF／現行 |
| D | Vulkan | OFF | OFF／現行 |
| E | Vulkan | ON | ON |
| F | Vulkan | OFF | ON |

記録:

```text
最初のabort frame
ARM9 PC
LR
CPSR
fault address
fault status
host call stack
```

## 14.2 custom core pathを疑う理由

現在のcustom pathはcoreのhot pathを変更している。

```text
SoftRenderer::DrawScanline()
CPU final生成条件
structured line copy
complete-frame copy
renderer3D selection
```

これらは毎scanline実行される。

host側のout-of-bounds／use-after-freeがあれば、
guest ARM stateやJIT memoryへ間接的な破壊を起こし得る。

断定はしないが、
Sapphire exact coreへ戻す前にJITを修正するのは順序が逆。

## 14.3 sanitizer build

debug buildで:

```text
AddressSanitizer
UndefinedBehaviorSanitizer
frame pointer保持
```

を有効化する。

例:

```text
-fsanitize=address,undefined
-fno-omit-frame-pointer
```

segmentation fault時の最初のhost stackを取得する。

---

# 15. 実装phase

# Phase P0: source pin

```text
Sapphire parent tag固定
parent SHA記録
core gitlink SHA記録
source manifest追加
```

# Phase P1: clean copy directory

現在の`VulkanReference`を一旦退避する。

```text
VulkanReference_LegacyCustom
```

新しい`VulkanReference`へSapphire sourceをcopyする。

最初のcommitではbuild adaptation以外を行わない。

# Phase P2: FrameQueue exact port

```text
state transition
queue ordering
previous frame
pending frame
deadline policy
fast-forward policy
reference ownership
stats
```

をSapphireと一致させる。

Frame resource handleだけdesktop Vk型へadapter化する。

# Phase P3: VulkanOutput／shader exact port

```text
VulkanOutput.h/.cpp
Compositor shader
Accumulate3D shader
generated SPIR-V
descriptor ABI
push constants
```

を同revisionで揃える。

# Phase P4: core exact port

Sapphire core gitlink commitから:

```text
GPU2D packed framebuffer
structured plane output
capture metadata
VulkanRenderer3D frame export
screen swap／front buffer
```

を移植する。

このphase完了まで、
独自`MelonPrimeStructuredSnapshot`を新pipelineへ接続しない。

# Phase P5: latch dependency closure port

```text
SoftPacked constants
helper群
cache state
latchSoftPackedFrameSnapshot()
temporal gate
capture ownership
debug capture
```

をまとめてcopyする。

# Phase P6: runFrame transaction port

EmuThreadのframe loopを:

```text
acquire before RunFrame
latch after RunFrame
prepare
queue
```

へ変更する。

# Phase P7: presenter ownership port

Sapphireと同様に:

```text
presenterが必要時にcomposition
```

する。

producer-side compositionを削除する。

# Phase P8: desktop adapters復元

```text
Win32 surface
Qt layout transform
HUD
radar
splash
DPI
present queue family
timeline fix
```

を一つずつ戻す。

各機能を戻すたびに白画面回帰テストを行う。

# Phase P9: legacy custom path削除

旧snapshot／converter／frameInputs経路を完全削除する。

---

# 16. 推奨commit分割

## Commit 1

```text
Pin the Sapphire 0.7.0.rc4 Vulkan source manifest
```

## Commit 2

```text
Import the Sapphire frame queue without frontend redesign
```

## Commit 3

```text
Import the Sapphire Vulkan output and compositor shaders
```

## Commit 4

```text
Port the exact Sapphire core Vulkan 2D data path
```

## Commit 5

```text
Import the Sapphire soft-packed frame latch dependency closure
```

## Commit 6

```text
Align the desktop frame producer transaction with Sapphire
```

## Commit 7

```text
Move fallback composition back to the Sapphire presenter path
```

## Commit 8

```text
Adapt the Sapphire presenter to the Qt Win32 surface host
```

## Commit 9

```text
Restore MelonPrime affine layouts and HUD overlays
```

## Commit 10

```text
Remove the legacy custom structured snapshot bridge
```

## Commit 11

```text
Add Sapphire debug frame captures for Vulkan parity validation
```

---

# 17. 禁止する修正

```text
buildSoftPackedSnapshot()へ条件分岐を追加し続ける
frontBuffer=0のまま補正する
white pixelを黒へ置換して隠す
CPU framebuffer upload fallbackを製品経路にする
present result=1だけで正常判定する
FrameQueue slot数を増やす
shaderだけを書き直す
ARM9 abortをJIT workaroundで先に隠す
Sapphire helperを一部だけ抜き出して簡略化する
Sapphire coreのmoving branch HEADを使う
旧経路と新経路をruntime切替可能なまま残す
```

---

# 18. テスト手順

# T1: source parity

copy対象ごとに:

```bash
git diff --no-index \
  sapphire-source-file \
  melonprime-copied-file
```

期待:

```text
license header
include path
namespace
OS adapter
MELONPRIME guard
```

以外の差分がない。

# T2: first 120 frames

期待:

```text
packed visible pixel count > 0
composed atlas visible pixel count > 0
present／commit継続
ARM9 abortなし
segmentation faultなし
```

# T3: 2D-only scene

firmware menuや2D logoで:

```text
3D slotがないregular display
```

を確認する。

期待:

```text
Plane0／Plane1の2D内容が保持される
nativeFinal zeroへ落ちない
```

# T4: 3D scene

Metroid Prime Hunters起動後:

```text
3D slot
2D overlay
screen swap
capture
```

を確認する。

# T5: front buffer

各frameで:

```text
actual FrontBuffer
snapshot.frontBufferLatched
prepared frame ID
```

が一致する。

固定0にならない。

# T6: capture／temporal

```text
regular capture
VRAM capture
screen swap alternation
previous 3D source
capture line mask
```

をSapphire debug outputと比較する。

# T7: presenter readback

```text
packed
composed atlas
swapchain presentation
```

の三段階を比較する。

# T8: guest CPU

Software／Vulkan、JIT ON／OFFで:

```text
ARM9 data abort
prefetch abort
```

が出ないこと。

# T9: stress

```text
resize
fullscreen
pause／resume
ROM stop／restart
Software→Vulkan→Software
internal resolution 1x／2x／4x
VSync
fast-forward
```

を実施する。

---

# 19. 完了条件

次をすべて満たすこと。

```text
最新HEADからSapphire source provenanceを追跡できる
parent commitとcore gitlink commitを記録している
Sapphire producer transactionと同じ順序
actual FrontBufferを使用
Sapphire latch dependency closureを使用
producerはprepareまで
presenterが必要時にcompose
2D-only regular displayが空にならない
packed dataがSapphireと同じ意味を持つ
present／commitが継続
ARM9 abortが発生しない
segmentation faultが発生しない
Qt／Win32差分がadapterへ隔離されている
旧MelonPrimeStructuredSnapshot経路を削除
```

---

# 20. 最終実装判断

現在のv55は:

```text
Vulkan presentation transport
```

は動いている。

しかし:

```text
frame内容の生成
```

はSapphireと同一ではない。

特に:

```text
regular structured displayでCPU native finalを作らない
＋
3D slotがない場合にゼロのnative finalへ置換する
```

独自組み合わせが、
白／空画面を直接説明できる。

今後の最短経路は、
このconverterを修理し続けることではない。

```text
Sapphire parent tagをsubmodule込みで固定
core data pathを正確に移植
latch dependency closureをそのままcopy
runFrame transactionを同じ順序へ変更
composition ownershipをpresenterへ戻す
Windows／Qt差分だけをadapter化
```

これを実施すること。
