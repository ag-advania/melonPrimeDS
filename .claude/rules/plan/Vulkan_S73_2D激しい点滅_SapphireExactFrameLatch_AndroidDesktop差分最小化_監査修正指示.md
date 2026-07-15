# Vulkan S73 監査・修正指示書
## 2D激しい点滅
## Sapphire Exact FrameLatch／Temporal Pipeline一体移植
## Android・Desktop差分の境界限定

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `494fd0f465099c0e8ee03958d81b0e01a9965b8c`  
**HEADコミット:** `Track Sapphire FrameLatch provenance adapters in CI (S72-10).`  
**前回監査HEAD:** `a426b385bf24d87bb1c69369434ba25c38a60d6a`  
**差分:** 6 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire frontend commit:** `2c10e59d7209d354e90d9ef4228330bac3f6e794`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 監査結論

S72ではdesktop側の危険なprevious-frame repairを
`CurrentFrameOnly`で停止した。

しかし、Sapphire Vulkan temporal pipelineは
次の複数層が一体で動く設計である。

```text
1. FrameLatch
2. updateVulkanTemporal3dHistoryGate
3. FrameQueue previous-frame ownership
4. VulkanOutput previous top／bottom source
5. capture 3D source
6. class4／regular capture temporal state
7. SurfacePresenter frame completion
```

現在のMelonPrimeDSは:

```text
FrameLatch内のCPU temporal repair:
    CurrentFrameOnlyで停止

history gate:
    動作継続

FrameQueue previous source:
    動作継続

VulkanOutput temporal source:
    動作継続

class4 state machine:
    動作継続
```

という**半分だけ無効化された非整合状態**である。

この状態では、同じemulated frameに対して:

```text
2D packed／control:
    current frame

high-resolution 3D source:
    previous frameになる場合あり

screen owner:
    current／previousのscreenSwap比較で切替

capture source:
    currentまたはprevious resource

class4 state:
    前frameのhash／stable counterを継続
```

が混在する。

結果として:

```text
frame N:
current 2D + current 3D

frame N+1:
current 2D + previous 3D／別owner

frame N+2:
current 2D + current 3D
```

となり、激しい点滅を起こせる。

今回の修正方針は、さらにdesktop heuristicを追加することではない。

```text
Sapphire 0.7.0.rc4の
FrameLatch／VulkanOutput／history gate／FrameQueue契約を
一体としてそのまま復元する
```

こと。

Desktop固有差分は次だけに限定する。

```text
- Android ANativeWindow ↔ Desktop Qt VkSurfaceKHR
- Android Vulkan loader ↔ Desktop volk
- Android input source ↔ Desktop immutable published-frame adapter
- Android overlayなし ↔ Desktop final CustomHUD pass
- Android log／filesystem ↔ Desktop platform adapter
```

2D／3D composition algorithm、
capture判定、temporal history、screen owner判定には
Desktop条件を追加しない。

---

# 1. 最新S72反映状況

S72で追加されたもの:

```text
MelonPrimeDesktop2DRepairMode.h
MelonPrimeDesktop2DProvenance.h
Capture3DSourceSnapshot
topCapture3dSource
bottomCapture3dSource
sourceFrameSerial
rendererGeneration
hardwareScreenSwapLatched
renderScreenSwapAt3DLatched
topEngineLatched
bottomEngineLatched
```

default:

```cpp
Desktop2DRepairMode::CurrentFrameOnly
```

このため、次は停止している。

```text
carryPreviousLatchedScreenLines
carryPreviousTemporalOverlayPixels
carryPreviousFullRegularComp7Overlay
legacy Engine A whole-screen replacement
legacy capture repair
```

この変更自体は危険なdesktop補完を止める診断として有効だった。

ただしproduction設計としては、
Sapphire temporal pipelineの一部分だけを止めている。

---

# 2. P0
# `CurrentFrameOnly`がtemporal pipeline全体を止めていない

## 2.1 FrameLatch history gate

`SapphireVulkanFrameLatch::updateVulkanTemporal3dHistoryGate()`は
repair modeを確認しない。

常に次を評価する。

```cpp
softPackedFramesAlternate3dOwner(...)
softPackedFrameNeedsReusablePreviousFrame(...)
softPackedFrameUsesTemporal3dHistory(...)
```

検出すると:

```cpp
vulkanTemporal3dHistoryGateFrames =
    kVulkanTemporal3dHistoryGateFrames;
```

とする。

`CurrentFrameOnly`でもhistory gateはactiveになる。

---

## 2.2 FrontendSession

producer開始時に毎frame:

```cpp
(void)frameLatch
    .updateVulkanTemporal3dHistoryGate();
```

を呼ぶ。

queue policyも:

```cpp
temporalHistoryRequired =
    frameLatch
        .isVulkanTemporal3dHistoryGateActive();
```

を使用する。

さらに:

```cpp
frameQueue.synchronizeHistoryReferences(
    [&](const Frame* frame)
    {
        return output
            .isFrameReferencedAsPendingPreviousSource(
                frame);
    });
```

でprevious resourceを保持する。

したがって、FrameLatchのprevious pixel copyを止めても:

```text
FrameQueue
VulkanOutput
previous renderer image
```

は前frameを使い続ける。

---

## 2.3 VulkanOutput

`VulkanOutput::prepareFrameForPresentation()`は
previous `FrameResource`を参照する。

保持するstate:

```text
previous renderer3dSnapshot
previous capture3d source
previous top source image
previous bottom source image
screenSwapToggledFromPrevious
class4NoAboveVramStructuredActive
class4BottomAboveHash
class4BottomAboveStableFrames
class4BottomAboveMotionActive
```

これらは固定Sapphireにも存在する正規algorithm。

問題はalgorithmではなく:

```text
Sapphireと異なるSoftPackedFrameSnapshot
+
一部停止されたFrameLatch
+
動作中のVulkanOutput temporal state
```

を入力していること。

---

# 3. P0
# Sapphire snapshot ABIをdesktopで変更しすぎている

固定Sapphireの`SoftPackedFrameSnapshot`:

```cpp
struct SoftPackedFrameSnapshot
{
    u64 frameId;
    int frontBufferLatched;
    bool screenSwapLatched;
    bool valid;
    bool hasCapture3dSource;
    bool captureBackedClass4Only;

    packed top/bottom planes;
    line metadata;
    capture3d source;
    capture mask;
    fallback lines;
    comp4 placeholders;
    top/bottom stats;
};
```

現在のMelonPrime版はさらに:

```text
sourceFrameSerial
rendererGeneration
hardwareScreenSwapLatched
renderScreenSwapAt3DLatched
topEngineLatched
bottomEngineLatched
topCapture3dSource
bottomCapture3dSource
```

をSapphire共通snapshotへ追加している。

この追加fieldを使って、
Sapphire本家には存在しないownershipルールを
FrameLatch内部へ入れている。

これは:

```text
AndroidとDesktopのplatform差
```

ではなく:

```text
composition semanticsのfork
```

である。

---

# 4. P0
# screen swapの意味が二重化している

固定Sapphireではrun-frame transaction内で:

```cpp
frontbuf =
    nds->GPU.FrontBuffer;

preparedFrameScreenSwap =
    nds->GPU.GPU3D.RenderScreenSwapAt3D;

latchSoftPackedFrameSnapshot(
    renderFrame,
    frontbuf,
    preparedFrameScreenSwap,
    useStructuredVulkan2D);

prepareFrameForPresentation(
    renderFrame,
    nds->GPU,
    frontbuf,
    preparedFrameScreenSwap,
    snapshot,
    renderer3D);
```

とする。

同じ`frontbuf`と
同じ`preparedFrameScreenSwap`を:

```text
latch
VulkanOutput
```

へ渡す。

現在のDesktopはlatchへ:

```cpp
SapphirePublished2DFrame
```

を渡し、その中に:

```text
hardwareScreenSwap
renderScreenSwapAt3D
physical top engine
physical bottom engine
```

を持たせる。

一方`VulkanOutput`へは別途live GPUから取得した:

```text
frontBuffer
preparedFrameScreenSwap
```

を渡す。

つまり同一frameのownerに
2つのsource-of-truthがある。

```text
Latch:
    published frame metadata

VulkanOutput:
    live GPU state
```

publicationとlive stateが1frameでもずれると:

```text
packed 2D owner
previous 3D owner
capture source owner
```

が別になる。

これが周期的に起きれば点滅する。

---

# 5. P0
# current-frame処理もSapphire exactではない

`CurrentFrameOnly`でprevious carryを止めても、
latch内では現在frameへ次を実行する。

```text
capture class集計
partial capture判定
broad capture flag clear
VRAM display lineのcapture flag昇格
structured line copy／merge選択
packed／structured control再構成
capture-backed class4判定
regular capture transition resync
```

これらの多くはSapphire本家にも存在するが、
Desktop版では次の追加条件が混入している。

```text
published physical owner
topEngine／bottomEngine
render3DOwnerChanged
legacyHeuristicRepairEnabled
desktop black contract
desktop capture provenance
```

したがって、単にprevious repairを止めても
「Sapphire current frameそのまま」にはならない。

---

# 6. P0
# 現HEADのclean build blocker

現在の`VulkanOutput.h`は:

```cpp
struct SoftPackedFrameSnapshot
{
    Capture3DSourceSnapshot
        topCapture3dSource;

    Capture3DSourceSnapshot
        bottomCapture3dSource;
};
```

の後に:

```cpp
struct Capture3DSourceSnapshot
{
    ...
};
```

を定義している。

by-value memberにはcomplete typeが必要。

この順序のままならclean C++ buildは失敗する。

修正方法は二択。

短期:

```text
Capture3DSourceSnapshot定義を
SoftPackedFrameSnapshotより前へ移動
```

推奨:

```text
Sapphire exact snapshotへ戻し、
Capture3DSourceSnapshot自体を
desktop sidecarへ移動
```

最新commitにCI status／workflow runは確認できない。

static Python testが通ることは
C++ build成功を意味しない。

---

# 7. S73の基本方針
# Productionは`SapphireExact`一択

現在の:

```cpp
CurrentFrameOnly
ExactProvenanceRepair
LegacyHeuristicRepair
```

をproduction分岐として残さない。

推奨:

```cpp
enum class SapphirePipelineMode
{
    SapphireExact,
#if !defined(NDEBUG)
    AllTemporalDisabled,
#endif
};
```

default:

```cpp
SapphirePipelineMode::SapphireExact
```

---

## 7.1 `SapphireExact`

次を固定Sapphireと同時に有効化する。

```text
FrameLatch exact temporal logic
history gate
FrameQueue previous frame policy
VulkanOutput previous source
capture 3D temporal source
class4 state machine
presenter completion ownership
```

一部だけ止めない。

---

## 7.2 `AllTemporalDisabled`

診断専用。

次をすべて同時に無効化する。

```text
FrameLatch previous carry
history gate
FrameQueue previous reuse
VulkanOutput previousTop／Bottom source
previous renderer3d snapshot
previous capture3d source
class4 persistent state
pending previous source references
```

実装例:

```cpp
bool temporalEnabled =
    pipelineMode
        == SapphirePipelineMode::SapphireExact;
```

この一つの値を:

```text
FrameLatch
FrontendSession
FrameQueue policy
VulkanOutput
```

へ同じframe transactionで渡す。

半分だけ無効化しない。

---

# 8. S73-1
# Sapphire `VulkanOutput.h`をexactへ戻す

固定Sapphireの次を
ファイル単位でそのままcopyする。

```text
SoftPackedScreenStats
SoftPackedFrameSnapshot
PreparedSoftPackedFrameDebugView
VulkanCompositionInputs
VulkanOutputTemporalStats
```

Desktop fieldを削除する。

```text
sourceFrameSerial
rendererGeneration
hardwareScreenSwapLatched
renderScreenSwapAt3DLatched
topEngineLatched
bottomEngineLatched
topCapture3dSource
bottomCapture3dSource
```

必要なDesktop validation metadataは
別のsidecarへ置く。

```cpp
struct DesktopSapphireFrameSidecar
{
    u64 emulatedFrameSerial = 0;
    u64 rendererGeneration = 0;

    bool hardwareScreenSwap = false;

    u32 physicalTopEngine =
        UINT32_MAX;

    u32 physicalBottomEngine =
        UINT32_MAX;
};
```

このsidecarをSapphire algorithmへ渡さない。

用途:

```text
debug assert
generation rejection
diagnostic log
```

だけ。

---

# 9. S73-2
# FrameLatch coreをupstreamから自動生成する

`SapphireVulkanFrameLatch.cpp`を
手作業で修正し続けない。

現在4,000行超のdesktop forkになっており、
変更箇所の境界が不明確。

generatorを追加する。

```text
tools/generate_sapphire_frame_latch.py
```

入力:

```text
SapphireRhodonite/melonDS-android
tag 0.7.0.rc4
app/src/main/cpp/MelonInstance.cpp
app/src/main/cpp/MelonInstance.h
```

出力:

```text
src/frontend/qt_sdl/SapphireGenerated/
    SapphireFrameLatchCore.cpp
    SapphireFrameLatchCore.h
```

抽出対象:

```text
packed helper functions
screen stats
latchSoftPackedFrameSnapshot
updateVulkanTemporal3dHistoryGate
clearLatchedSoftPackedFrameSnapshot
temporal state fields
```

許可transform:

```text
namespace変更
include path変更
class名変更
member access prefix変更
source access adapter化
Platform log adapter
```

禁止transform:

```text
条件式変更
閾値変更
screen owner変更
capture class変更
temporal gate変更
pixel／control書換え変更
previous source選択変更
```

---

# 10. S73-3
# Android／Desktop input adapterを1か所へ限定

固定Sapphire coreへ渡す入力を
immutable objectとして一度だけ確定する。

```cpp
struct SapphireFrameInput
{
    Frame* frame = nullptr;

    int frontBuffer = -1;

    bool preparedFrameScreenSwap =
        false;

    const u32* packedTop = nullptr;
    const u32* packedBottom = nullptr;

    const u32* structuredTopPlane0 =
        nullptr;
    const u32* structuredTopPlane1 =
        nullptr;
    const u32* structuredTopControl =
        nullptr;

    const u32* structuredBottomPlane0 =
        nullptr;
    const u32* structuredBottomPlane1 =
        nullptr;
    const u32* structuredBottomControl =
        nullptr;

    const u32* capture3dSource =
        nullptr;

    const u8* captureLineMask =
        nullptr;
};
```

Desktop adapter:

```cpp
SapphireFrameInput
BuildDesktopSapphireFrameInput(
    Frame* frame,
    const SapphirePublished2DFrame&
        published,
    const Vulkan3DFrameView&
        frame3d);
```

ここでのみ:

```text
physical slot mapping
published frame validation
renderer generation validation
Qt/Desktop lifetime validation
```

を行う。

coreへ渡した後はlive GPU stateを再読しない。

---

## 10.1 atomic input条件

同じinput内で次が一致すること。

```text
published.emulatedFrameSerial
frame3d.FrameSerial
frame.rendererGeneration
published.rendererGeneration
active renderer generation
frontBuffer
```

不一致ならframe全体をrejectする。

pixel／line単位で補完しない。

---

# 11. S73-4
# run-frame transactionをSapphire exactへ合わせる

固定Sapphireの順序:

```text
1. render frame取得
2. pre-run renderer snapshot
3. nds->RunFrame()
4. frontBuffer取得
5. preparedFrameScreenSwap取得
6. latch
7. VulkanOutput prepare
8. frame queue push
```

Desktopも同じ順序にする。

現在のFrontendSessionの:

```text
beginProducerFrame
completeProducerFrame
```

分離は維持してよいが、
frame inputはcomplete時に一度だけfreezeする。

```cpp
completeProducerFrame()
{
    Vulkan3DFrameView frame3d =
        renderer3D
            .GetVulkan3DFrameView();

    SapphirePublished2DFrame published =
        GPU.GetPublished2DFrame();

    SapphireFrameInput input =
        BuildDesktopSapphireFrameInput(
            frame,
            published,
            frame3d);

    exactLatch.latch(input);

    output.prepareFrameForPresentation(
        frame,
        input.frontBuffer,
        input.preparedFrameScreenSwap,
        exactLatch.snapshot(),
        renderer3D);
}
```

同じtransaction中に:

```text
GPU.FrontBuffer
RenderScreenSwapAt3D
Published2DFrame
```

を別々の時点で再取得しない。

---

# 12. S73-5
# temporal enableをend-to-endで統一

`SapphirePipelineMode`を
FrontendSessionが所有する。

```cpp
const bool temporalEnabled =
    mode == SapphireExact;
```

### FrameLatch

```cpp
updateVulkanTemporal3dHistoryGate(
    temporalEnabled);
```

disabled:

```text
gateFrames = 0
previous snapshot clear
```

### FrameQueue

disabled:

```text
AllowPreviousFrameReuse = false
PreserveBacklogOnPresent = false
history reference = none
```

### VulkanOutput

disabled:

```text
previousTopSourceValid = false
previousBottomSourceValid = false
previous capture source禁止
class4 persistent state clear
lastPreparedFrame = nullptr
```

### Presenter

disabled:

```text
current completed frameのみ
```

Productionは必ず全部enabled。

---

# 13. S73-6
# current `screenSwapLatched`意味をexactへ戻す

固定Sapphire:

```cpp
snapshot.screenSwapLatched =
    preparedFrameScreenSwap;
```

これだけに戻す。

次をsnapshotから削除。

```text
hardwareScreenSwapLatched
renderScreenSwapAt3DLatched
topEngineLatched
bottomEngineLatched
```

physical engine情報はDesktop sidecarにのみ置く。

VulkanOutputが参照する:

```text
snapshot.screenSwapLatched
resource.screenSwap
screenSwapToggledFromPrevious
```

をすべて同じSapphire semanticsにする。

---

# 14. S73-7
# `VulkanOutput.cpp`はupstream exactを維持

現在のclass4／capture／motion stateは
Sapphire固定版にも存在する。

したがって、点滅対策として:

```text
class4 hashを削除
stable frame数を変更
screenSwap条件を追加
Desktop GPU名別例外を追加
```

してはいけない。

必要な作業:

```text
1. current fileとpinned upstreamをnormalized diff
2. Desktop Vulkan API差だけをallowed
3. composition condition差を0へする
```

許可するDesktop差:

```text
volk include／dispatch
VkImage resource type
timeline／queue family fix
pipeline cache path
validation logging
```

許可しない差:

```text
class4判定
capture owner
screenSwap owner
previous source判定
structured handoff判定
```

---

# 15. S73-8
# FrameLatchのDesktop helperを削除

productionから削除する。

```text
MelonPrimeDesktop2DRepairMode.h
MelonPrimeDesktop2DProvenance.h
MelonPrimeDesktop2DBlackContract.h
```

ただしblack helperが
Sapphire本家のexact helperと完全一致する場合は、
generated core内のupstream helperへ戻す。

Desktopに残してよいのは:

```text
BuildDesktopSapphireFrameInput
DesktopSapphireFrameSidecar
generation validation
physical pointer validation
diagnostic output
```

pixel semanticsを持つDesktop helperを残さない。

---

# 16. S73-9
# current header compile blocker修正

Sapphire exact `VulkanOutput.h`へ戻すことで
`Capture3DSourceSnapshot`問題を消す。

一時的に現構造をbuildする場合でも:

```text
Capture3DSourceSnapshotを
SoftPackedFrameSnapshotより前へ定義
```

する。

ただしこれは暫定。

最終形ではSnapshotへ含めない。

---

# 17. S73-10
# parity CIをsource文字列testからexact diffへ変更

現在のS72 testは:

```text
CurrentFrameOnly文字列がある
if (!temporalRepairEnabled)がある
provenance helper名がある
```

ことだけを検査する。

これはSapphire parityではない。

新CI:

```bash
python3 tools/generate_sapphire_frame_latch.py \
    --verify

python3 tools/check_sapphire_frame_latch_parity.py \
    --upstream-tag 0.7.0.rc4

python3 tools/check_sapphire_vulkan_output_exact.py
```

完了条件:

```text
generated latch core:
    byte-identical after approved transform

VulkanOutput condition body:
    normalized diff 0

SoftPackedFrameSnapshot:
    exact upstream

temporal state fields:
    exact upstream
```

---

# 18. S73-11
# clean C++ buildをCI必須化

static Python testだけで完了扱いにしない。

最低限:

```text
Windows Vulkan ON
Linux Vulkan ON
Debug
Release
```

build target:

```text
melonPrimeDS Qt frontend
Sapphire Vulkan files
CustomHUD ON
```

CIで:

```text
header ordering
missing type
signature mismatch
undefined symbol
```

を検出する。

最新HEADにはworkflow runが無いため、
実装完了と判断しない。

---

# 19. 点滅のstage特定diagnostics

120frameだけ次を記録する。

```text
[SapphireFrameIdentity]
presentedFrameId
emulatedFrameSerial
rendererGeneration
publishedFrameSerial
frontBuffer
preparedFrameScreenSwap
snapshotScreenSwap
previousSnapshotScreenSwap
screenSwapToggledFromPrevious
historyGateActive
previousTopSourceValid
previousBottomSourceValid
capture3dSourceValid
liveSourceScreenSwap
```

同じframe内で:

```text
preparedFrameScreenSwap
snapshotScreenSwap
VulkanOutput resource.screenSwap
```

が一致しなければassert。

---

## 19.1 stage hash

physical screenごと:

```text
raw published packed hash
latched packed hash
prepared GPU buffer hash
final compositor input hash
final screen hash
```

点滅pixelが最初に変わるstageを特定する。

---

# 20. 必須A/B mode

## Mode A: SapphireExact

production候補。

```text
exact FrameLatch
exact history gate
exact FrameQueue
exact VulkanOutput temporal state
```

## Mode B: AllTemporalDisabled

diagnostic。

```text
previous source 0
history gate 0
previous reuse 0
class4 persistent state reset
current frameのみ
```

禁止:

```text
FrameLatchだけcurrent
VulkanOutputはtemporal
```

---

## 20.1 判定

Mode Bで点滅が消える:

```text
temporal pipeline integration不整合
```

Mode AでSapphire Androidと一致:

```text
修正完了
```

Mode Aでも点滅しAndroidでは正常:

```text
Desktop input adapter／surface frame identity不一致
```

Mode AとAndroidの両方で点滅:

```text
ROMの本来挙動またはSapphire upstream issue
```

---

# 21. Golden test

同じsavestate／同じframe sequenceで:

```text
Sapphire Android 0.7.0.rc4
MelonPrime Desktop SapphireExact
```

を比較する。

取得対象:

```text
packedTopPlane0
packedTopPlane1
packedTopControl
packedTopLineMeta
packedBottomPlane0
packedBottomPlane1
packedBottomControl
packedBottomLineMeta
capture3dSource
final composited output
```

完了条件:

```text
FrameLatch snapshot差分 = 0
VulkanOutput composition input差分 = 0
final output差分 = platform color-format差のみ
```

---

# 22. 120frame flicker test

固定2D背景を120frame表示。

各frameのhash:

```text
top 2D plane hash
bottom 2D plane hash
top control hash
bottom control hash
```

ゲーム上の変更が無い区間では:

```text
hash alternation period 2 = 0
hash alternation period 3 = 0
unexpected owner toggle = 0
```

特に:

```text
A-B-A-B
```

パターンを自動検出する。

---

# 23. screen owner transition test

次を独立に変化させる。

```text
hardware ScreenSwap
RenderScreenSwapAt3D
physical top engine
physical bottom engine
```

Desktop adapterはvalidationにだけ使い、
Sapphire snapshotへ渡すのは:

```text
preparedFrameScreenSwap
```

だけ。

final top／bottom packed pointerは
解決済みphysical publicationを維持する。

---

# 24. 推奨commit分割

## S73-1

```text
Fix VulkanOutput snapshot type ordering and add clean Vulkan build CI
```

## S73-2

```text
Restore Sapphire VulkanOutput snapshot ABI exactly
```

## S73-3

```text
Generate the Sapphire FrameLatch core from pinned Android sources
```

## S73-4

```text
Add an immutable Desktop Sapphire frame-input adapter
```

## S73-5

```text
Use one atomic frame identity for latch and VulkanOutput preparation
```

## S73-6

```text
Replace partial CurrentFrameOnly mode with coherent SapphireExact and AllTemporalDisabled modes
```

## S73-7

```text
Restore exact Sapphire temporal history and FrameQueue ownership
```

## S73-8

```text
Remove pixel-semantic Desktop repair and provenance helpers from production
```

## S73-9

```text
Verify VulkanOutput composition conditions against pinned Sapphire
```

## S73-10

```text
Add Android-versus-Desktop snapshot golden tests
```

## S73-11

```text
Add 120-frame period-two flicker detection
```

## S73-12

```text
Document Android/Desktop-only allowed differences
```

---

# 25. 実装順

```text
1. S73-1
2. clean build確認
3. S73-2
4. S73-3
5. S73-4
6. S73-5
7. S73-6
8. AllTemporalDisabled A/B確認
9. S73-7
10. SapphireExact確認
11. S73-8
12. S73-9
13. S73-10
14. S73-11
15. S73-12
```

---

# 26. Android／Desktop差分として許可するもの

```text
ANativeWindow ↔ Qt VkSurfaceKHR
EGL fence ↔ Vulkan timeline／fence
direct Vulkan prototypes ↔ volk
Android filesystem ↔ Desktop filesystem
Android logging ↔ Desktop logging
Android screen layout ↔ Qt affine layout
Android overlayなし ↔ Desktop final CustomHUD
Android lifecycle callbacks ↔ Qt surface events
```

---

# 27. Android／Desktop差分として許可しないもの

```text
SoftPackedFrameSnapshot fields
capture class判定
comp mode判定
Protected Black semantics
line metadata semantics
screenSwapLatched semantics
temporal history gate
previous source selection
class4 state machine
structured handoff
capture 3D owner
FrameQueue temporal requirements
```

これらはSapphire exactとする。

---

# 28. 禁止事項

```text
- 点滅を抑えるためframeを間引く
- A/B frameの片方だけpresentする
- 前frameを常時固定する
- class4 thresholdを推測で変更する
- Desktop GPU vendor別のcomposition例外を追加する
- screenSwap boolをさらに増やす
- SnapshotへDesktop ownership fieldを追加し続ける
- CurrentFrameOnlyのままVulkanOutput previous sourceを動かす
- source文字列testだけでparity完了とする
- 上下画面の解決済みphysical pointer mappingを戻す
- shaderで点滅を平均化する
- temporal accumulationで見た目だけぼかす
```

---

# 29. 完了条件

```text
1. production modeがSapphireExact
2. FrameLatch coreがpinned upstream生成物
3. SoftPackedFrameSnapshotがupstream exact
4. latchとVulkanOutputが同じatomic frame inputを使用
5. partial temporal disableが存在しない
6. 120frameでperiod-2 flicker 0
7. Android Sapphire snapshotとDesktop snapshot一致
8. previous source選択がAndroid Sapphireと一致
9. clean Windows／Linux Vulkan build成功
10. Android／Desktop差分がWSI・loader・HUD境界だけ
```

---

# 40. 進捗

| Phase | Commit | Status | Notes |
|-------|--------|--------|-------|
| S73-1 | `74400323e` | done | VulkanOutput.h type ordering + `check_vulkan_output_header_compile.py` CI |
| S73-2 | `cd5b1adb9` | done | Exact `SoftPackedFrameSnapshot` ABI + `DesktopSapphireFrameSidecar` |
| S73-3 | `5046dd97f` | done | `generate_sapphire_frame_latch.py` + `SapphireGenerated/` core |
| S73-4 | `7bd8e9a0e` | done | `SapphireFrameInput` + `BuildDesktopSapphireFrameInput` |
| S73-5 | `7c909ca84` | done | Atomic frame identity in `MelonPrimeVulkanFrontendSession` |
| S73-6 | `d4fffb0ad` | done | `SapphireExact` default; removed `CurrentFrameOnly` |
| S73-7 | `c1a74f99a` | done | End-to-end `sapphireTemporalEnabled()` gating |
| S73-8 | `4a2538855` | done | Removed desktop provenance/repair helpers from production |
| S73-9 | `6e71cf429` | done | `check_sapphire_vulkan_output_exact.py` |
| S73-10 | `bc8931c3b` | done | Golden snapshot contract tests |
| S73-11 | `ad2520013` | done | Period-two flicker contract tests |
| S73-12 | `4c484e5fe` | done | `S73_ANDROID_DESKTOP_ALLOWED_DIFFS.md` + CI wiring |

---

# 30. 最終判断

S72は危険なDesktop previous-frame repairを止めたが、
Sapphire temporal pipeline全体は止めなかった。

現在は:

```text
FrameLatch current-only
+
history gate active
+
FrameQueue temporal
+
VulkanOutput previous source active
```

というSapphire本家に存在しない組合せ。

これが現在の激しい点滅の最有力原因である。

今回の正しい修正は、
新しい点滅抑制heuristicではない。

```text
SapphireのFrameLatch
SapphireのSnapshot ABI
Sapphireのhistory gate
SapphireのFrameQueue
SapphireのVulkanOutput
```

を一体でそのまま使用し、

```text
Androidから入力を取るか
Desktopから入力を取るか
```

だけをadapterで切り替えること。

これにより、違いを実質的に:

```text
Android WSI
Desktop WSI
```

へ縮小できる。
