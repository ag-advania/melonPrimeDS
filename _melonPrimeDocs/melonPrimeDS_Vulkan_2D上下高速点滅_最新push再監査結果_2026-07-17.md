# melonPrimeDS `develop_vulkan` Vulkan 2D上下高速点滅 最新push再監査結果

**監査日:** 2026-07-17
**対象リポジトリ:** `ag-advania/melonPrimeDS`
**対象ブランチ:** `develop_vulkan`
**前回監査HEAD:** `368b0b530e9cc282ef713dd3fb83b15379b2e2a2`
**今回監査HEAD:** `bb23e9543e38592c3593d2bd0c2718c0662a24de`
**今回コミット:** `fix(vulkan): separate capture ownership metadata`
**比較:** 前回HEADから1コミット進行、behind 0
**コード変更:** 実施していない
**commit／push／PR:** 実施していない

---

# 1. 今回の症状

ユーザー確認結果:

1. 以前より症状は大幅に改善した
2. 現在、2D上画面の映像が下画面へ点滅表示される
3. 下画面では、本来の下画面と上画面の映像が高速で交互表示される
4. 完全な黒抜けや以前の全面的な所有権崩壊は軽減した

この症状は、Top／Bottom packed 2D plane自体を常時逆コピーしている状態よりも、次のどちらかに整合する。

```text
現在のsource imageへ誤ったLCD ownerを付ける
    または
Top／Bottom temporal historyの片方へ反対画面sourceを蓄積する
```

最新差分を監査した結果、両方を引き起こせる新規回帰が確認できた。

---

# 2. HEADと差分

現在の`develop_vulkan`:

```text
bb23e9543e38592c3593d2bd0c2718c0662a24de
fix(vulkan): separate capture ownership metadata
```

前回監査対象:

```text
368b0b530e9cc282ef713dd3fb83b15379b2e2a2
fix(vulkan): complete structured 2D frame ownership
```

今回の差分は1コミットである。

主な変更:

- capture source-valid maskを独立
- Top／Bottom screen-level capture need maskを独立
- exact `CaptureScreenSwap`とvalid flagを伝搬
- capture phase historyを追加
- renderer 3D serialを追加
- protected-black handoffを修正
- exact owner適用時にClass4 cadenceを抑止
- Vulkan 2D traceを追加
- shader生成headerとSPIR-V manifestを更新

これらの大部分は前回監査の指摘に沿っており、症状改善と整合する。

---

# 3. 総合結論

今回残った症状の最有力原因は、次の1行である。

```cpp
if (exactCaptureOwnerApplies)
    liveSourceScreenSwap = softPackedSnapshot.captureScreenSwap;
```

`CaptureScreenSwap`は、Display Capture用のexact 3D source bufferがどちらのLCDに属するかを示す。

一方、`liveSourceScreenSwap`は後段で次の複数用途へ使われる。

1. 現在のVulkan renderer color targetのLCD owner
2. current renderer imageをコピーしたframe snapshotのowner label
3. current renderer imageをTop accumulatorへ入れるかBottom accumulatorへ入れるか
4. shaderがcurrent live 3DをTop／Bottomどちらへ割り当てるか
5. `framesSinceTopLive3D`／`framesSinceBottomLive3D`の更新
6. `lastTopRendererSourceFrame`／`lastBottomRendererSourceFrame`の更新
7. previous sourceをTop／Bottomどちらから選ぶか

この2つのownerは同義ではない。

```text
CaptureScreenSwap
    = DS-timed Display Capture sourceのowner

ScreenSwapAt3D
    = current renderer color targetのowner
```

最新コミットは、capture bufferのexact ownerを正しく伝搬した一方で、その値をcurrent live renderer imageのownerへも適用している。

その結果:

```text
実際のcurrent renderer image
    = owner A

付与されるsnapshot owner label
    = capture owner B
```

という不一致が成立する。

さらに、同じ誤ownerに基づいてcurrent imageをTop／Bottom accumulatorへ保存するため、temporal historyそのものが反対画面の内容で汚染される。

## 最有力因果関係

```text
Display Captureが3D sourceを必要とする
    ↓
CaptureScreenSwapValid = true
    ↓
TopまたはBottomのneed maskに1 lineでも立つ
    ↓
exactCaptureOwnerApplies = true
    ↓
liveSourceScreenSwapをCaptureScreenSwapで上書き
    ↓
current renderer color targetをそのままコピー
    ↓
コピーimageへcapture ownerをowner labelとして保存
    ↓
誤owner側のaccumulatorへcurrent imageを蓄積
    ↓
previous Top／Bottom source historyが交差汚染
    ↓
下画面へ上画面sourceが現れる
    ↓
次のphaseではpacked下画面または別historyが選ばれる
    ↓
下画面が下／上で高速点滅
```

---

# 4. 前回指摘の改善確認

## 4.1 mask semanticsの分離

現在のcompleted snapshotは次を別fieldとして持つ。

```cpp
Capture3DSourceLineValid[192]
TopScreenNeedsCapture3D[192]
BottomScreenNeedsCapture3D[192]
```

内部には引き続き次がある。

```cpp
StructuredCaptureLineUses3D[4 * 192]
StructuredEngineLineUsesCapture3D[2 * 192]
StructuredCapture3DSourceLineValid[192]
```

Top／Bottom need maskはphysical screenへ構築済みの`StructuredScreenLineMeta`から生成される。

```cpp
TopScreenNeedsCapture3D[line] =
    (TopLineMeta & ((1 << 21) | (1 << 22))) != 0;

BottomScreenNeedsCapture3D[line] =
    (BottomLineMeta & ((1 << 21) | (1 << 22))) != 0;
```

前回問題だったsource-valid／screen-useの混同は、構造上ほぼ解消している。

---

## 4.2 exact CaptureScreenSwapの伝搬

現在は次の全層へ伝搬される。

```text
SoftRenderer completed snapshot
StructuredVulkanSnapshotSource
SoftPackedFrameSnapshot
FrameResource
VulkanCompositionInputs
Compositor push constants
```

`buildCompositionInputs()`では、capture buffer用ownerとして正しく次へ設定される。

```cpp
capture3dSourceScreenSwapValid
capture3dSourceScreenSwap
```

この部分は正しい。

---

## 4.3 protected-black handoff

compute compositorとPresenter fragmentの両方で、旧コードの非黒限定判定が修正されている。

修正後:

```glsl
bool aboveUsable2D =
    structured2DAbove
    && structuredPlane1Usable2D;

if (aboveUsable2D)
    composed = above2D;
```

`structuredPlane1Usable2D`にはprotected-blackが含まれる。

```glsl
isStructured2DVisible(above2D)
    || structured2DProtectedBlack
```

黒抜け改善はこの修正と整合する。

---

## 4.4 Class4 cadence

exact capture ownerが適用されるframeでは、Class4 alternating cadenceを開始しない条件が追加された。

```cpp
if (class4AsymmetricBottomDominantPair
    && !exactCaptureOwnerApplies)
```

以前の1フレームごとの明示的owner反転は抑制される。

症状が大幅に改善した理由の1つと考えられる。

---

## 4.5 serial trace

次が追加された。

```text
structuredGeneration
renderer3dRenderSerial
renderer3dSnapshotSerial
serialMismatch
```

世代不一致仮説をruntimeで検証できるようになった。

現時点では、今回の上下高速点滅をgeneration mismatchで説明する必要はない。

---

# 5. F-01 Critical: capture ownerでlive renderer ownerを上書き

## 5.1 owner domainが異なる

### live renderer owner

producerはcompleted frameへ次を保存する。

```cpp
completedFrame.ScreenSwapAt3D =
    GPU.GPU3D.GetRenderScreenSwapAt3D();
```

Qt adapterではこれを次へ渡す。

```cpp
snapshotSource.screenSwap =
    structuredSource.ScreenSwapAt3D;
```

SnapshotBuilderは次へ保存する。

```cpp
destination.screenSwapLatched =
    source.screenSwap;
```

Outputでは次へ入る。

```cpp
resource.screenSwap =
    softPackedSnapshot.screenSwapLatched;
```

この値がcurrent Vulkan renderer targetのownerである。

### capture buffer owner

別に次がある。

```cpp
CaptureScreenSwap
CaptureScreenSwapValid
```

これはDisplay Capture用にexportしたexact 3D sourceのownerである。

両者が同じframeで一致する保証はない。

むしろScreenSwapやalternating captureを扱うため、別fieldとして追加したものである。

---

## 5.2 問題の上書き

current owner selectionは次で開始する。

```cpp
bool liveSourceScreenSwap =
    resource.screenSwap;
```

その後、旧来の各種heuristicが適用される。

さらに最新コミットで次が追加された。

```cpp
const bool exactCaptureOwnerApplies =
    softPackedSnapshot.captureScreenSwapValid
    && (
        any_of(topScreenNeedsCapture3dMask)
        || any_of(bottomScreenNeedsCapture3dMask)
    );

if (exactCaptureOwnerApplies)
    liveSourceScreenSwap =
        softPackedSnapshot.captureScreenSwap;
```

これにより、capture buffer ownerがlive image ownerへ代入される。

---

## 5.3 1 lineでもframe全体を上書きする

`exactCaptureOwnerApplies`は、Top／Bottom need maskのどこか1 lineに非0があるだけで成立する。

```text
partial capture 1 line
    ↓
capture owner valid
    ↓
frame全体のlive ownerを変更
```

capture bufferをsampleするlineだけへownerを適用するなら問題ない。

しかし現行コードでは、current renderer snapshot全体のownerを変更する。

partial capture、fade、menu transitionで特に不安定になり得る。

---

## 5.4 capture ownerは既に正しい場所へ伝搬されている

`buildCompositionInputs()`は次を独立して設定する。

```cpp
if (resource.captureScreenSwapValid)
{
    outInputs.capture3dSourceScreenSwapValid = true;
    outInputs.capture3dSourceScreenSwap =
        resource.captureScreenSwap;
}
```

shaderはこれを`screenMatchesCapture3DSource`へ使う。

したがって、capture ownerを`liveSourceScreenSwap`へ上書きする必要はない。

この上書きは重複ではなく、異なるsourceへ同じownerを誤適用している。

---

## 判定

**最新pushで追加されたCritical regression。**

**今回の新症状に最も直接的に一致する。**

---

# 6. F-02 Critical: current imageへ誤ったsnapshot owner labelを付ける

## 6.1 コピーされるimage

`recordDirectPresentationPrep()`は次を呼ぶ。

```cpp
recordRenderer3dSnapshotCopy(
    resource,
    renderer3D,
    snapshotScreenSwap);
```

コピー元は常にcurrent renderer targetである。

```cpp
renderer3D.GetColorTargetImage()
```

`vkCmdCopyImage()`でframe固有の`renderer3dSnapshot`へコピーする。

---

## 6.2 owner label

コピー後、次を保存する。

```cpp
resource.renderer3dSnapshotScreenSwap =
    snapshotScreenSwap;
```

この`snapshotScreenSwap`へ現在渡しているのは、F-01でcapture ownerに上書き済みの`liveSourceScreenSwap`である。

呼出し:

```cpp
recordDirectPresentationPrep(
    frame,
    resource,
    renderer3D,
    liveSourceScreenSwap,
    liveSourceScreenSwap,
    !liveSourceScreenSwap,
    replaceAccumulatedHighres);
```

したがって、実際のimageとowner labelが分離する。

```text
copy source:
renderer3D.GetColorTargetImage()

actual owner:
resource.screenSwap / ScreenSwapAt3D

stored owner:
captureScreenSwapで上書きされたliveSourceScreenSwap
```

---

## 6.3 shaderへ誤ownerが渡る

`buildCompositionInputs()`はrenderer snapshotが存在すると、次をlive ownerとする。

```cpp
outInputs.liveSourceScreenSwap =
    resource.renderer3dSnapshotScreenSwap;
```

shaderは次でcurrent imageをTop／Bottomどちらへ使用するか決める。

```glsl
screenOwnsLive3D =
    Top
        ? liveSourceScreenSwap
        : !liveSourceScreenSwap;
```

このため、current imageが反対LCDへ供給される。

---

## 判定

**F-01の直接的な下流障害。**

**current sourceの物理ownerとsoftware labelの不一致。**

---

# 7. F-03 Critical: 誤owner側のTop／Bottom accumulatorを更新する

`recordDirectPresentationPrep()`へ渡す次の2引数も、同じ`liveSourceScreenSwap`から作られる。

```cpp
accumulateTopHighres =
    liveSourceScreenSwap;

accumulateBottomHighres =
    !liveSourceScreenSwap;
```

内部:

```cpp
if (accumulateTopHighres)
    recordAccumulateMerge(resource, true, ...);

if (accumulateBottomHighres)
    recordAccumulateMerge(resource, false, ...);
```

capture ownerとactual current image ownerが異なるframeでは、current imageを反対側LCDのaccumulatorへ保存する。

## 具体例

```text
actual current image owner:
Top

exact capture buffer owner:
Bottom
```

現行処理:

```text
liveSourceScreenSwap = Bottom
renderer snapshot label = Bottom
accumulateBottomHighres = true
```

結果:

```text
Topのcurrent image
    ↓
Bottom accumulatorへ蓄積
```

後続frameでBottomがprevious／accumulated sourceを必要とすると、BottomへTopの内容が表示される。

次のphaseでcurrent packed Bottomが選ばれれば、Bottomは次を高速で交互表示する。

```text
本来のBottom
Topで汚染されたBottom history
```

これはユーザー報告とほぼ完全に一致する。

---

## 判定

**今回の「下画面に上画面が混入する」直接経路。**

---

# 8. F-04 High: temporal historyの交差汚染が症状を持続・増幅

owner誤判定後、以下が汚染され得る。

```text
accumulatedTopHighresImage
accumulatedBottomHighresImage
lastTopRendererSourceFrame
lastBottomRendererSourceFrame
previousTopRendererSourceImage
previousBottomRendererSourceImage
lastTopComposedFrame
lastBottomComposedFrame
previousTopComposedFrame
previousBottomComposedFrame
```

## 8.1 last renderer source

current snapshotのowner labelから次を更新する。

```cpp
if (live3dOwnerIsTop)
    lastTopRendererSourceFrame = frame;
else
    lastBottomRendererSourceFrame = frame;
```

`live3dOwnerIsTop`は`renderer3dSnapshotScreenSwap`を参照する。

owner labelが誤っていれば、current frameは反対側のlast-source slotへ保存される。

---

## 8.2 previous renderer source

後続frameでは、Top／Bottomごとに次をlatchする。

```text
lastTopRendererSourceFrame
lastBottomRendererSourceFrame
```

汚染済みslotをprevious sourceとしてshaderへ渡すため、上画面sourceが下画面のprevious imageとして使われる。

---

## 8.3 composed LCD replay

compositor後には、必要に応じてprevious composed atlasのTopまたはBottom領域をcurrent atlasへコピーする。

コピー自体は次であり、TopをBottomへ直接コピーしてはいない。

```text
Top replay:
previous Top region → current Top region

Bottom replay:
previous Bottom region → current Bottom region
```

しかしprevious Bottom regionが既にTop sourceで汚染されていれば、Bottom replayによってTop画像が再表示される。

したがってcomposed replayは初期原因ではなく、汚染を持続させる増幅器である。

---

## 8.4 history invalidation不足

owner domainの不一致を検出しても、次を自動clearする処理は確認できない。

```text
Top／Bottom accumulator
last renderer source
last composed source
packed carry history
capture phase history
```

修正後の初回起動ではstateは初期化されるが、runtimeでowner classificationが切り替わる場面では過去の誤historyを再利用できる。

---

## 判定

**F-01～F-03で作られた誤表示を高速点滅として持続させるHigh amplifier。**

---

# 9. F-05 Medium: 1つの変数が3種類の意味を持つ

`liveSourceScreenSwap`は現在、少なくとも次の意味を兼ねる。

```text
A. actual current renderer image owner
B. compositorがcurrent imageを割り当てるowner
C. accumulator destination
```

さらに最新コミットで次も統合された。

```text
D. exact capture buffer owner
```

これらは必ずしも同じではない。

## 必要な分離

```cpp
bool rendererSnapshotScreenSwap;
bool compositorLiveSourceScreenSwap;
bool captureSourceScreenSwap;
```

### `rendererSnapshotScreenSwap`

actual copied renderer imageのowner。

source of truth:

```text
ScreenSwapAt3D
renderer3D current render owner
```

### `captureSourceScreenSwap`

capture 3D bufferのowner。

source of truth:

```text
CaptureScreenSwap
CaptureScreenSwapValid
```

### `compositorLiveSourceScreenSwap`

shaderへcurrent live imageを割り当てるowner。

原則は`rendererSnapshotScreenSwap`と一致させる。

特殊なtemporal handoffで反対LCDへsourceが必要なら、current imageを誤labelするのではなく、次を明示選択する。

```text
capture buffer
previous Top source
previous Bottom source
accumulator
composed replay
```

---

# 10. F-06 Medium: traceにowner invariantがない

現在のtraceには次がある。

```text
screenSwapAt3D
captureOwner
computedOwner
rendererSnapshotOwner
renderer3dRenderSerial
renderer3dSnapshotSerial
```

情報量は十分である。

しかし、最重要invariantを自動判定していない。

## 必須invariant

current renderer targetをコピーした場合:

```text
rendererSnapshotOwner
    ==
screenSwapAt3D
```

capture buffer:

```text
captureSourceOwner
    ==
captureOwner
```

この2つは別々に検証する必要がある。

## 現行traceで予想される異常

症状発生frameでは次になり得る。

```text
screenSwapAt3D = 1
captureOwner = 0
computedOwner = 0
rendererSnapshotOwner = 0
```

serialは一致していてもowner labelが誤っている。

```text
renderer3dRenderSerial
    ==
renderer3dSnapshotSerial

しかし

rendererSnapshotOwner
    !=
screenSwapAt3D
```

serial一致だけでは今回の回帰を検出できない。

---

# 11. Class4 cadenceの再評価

以前はClass4 cadenceが高速点滅の第一候補だった。

最新コードでは、exact capture ownerが適用されるframeではcadenceを開始しない。

```cpp
class4AsymmetricBottomDominantPair
    && !exactCaptureOwnerApplies
```

したがって今回の症状がexact owner有効時に発生しているなら、Class4 cadenceは第一原因ではない。

ただし、次の場合には依然として増幅要因となる。

- capture owner validが一時的にfalseになる
- need maskが全0になる
- exact owner適用frameとheuristic frameが交互になる
- history汚染済みの状態でcadenceへ戻る

## 判定

```text
旧第一原因候補
    ↓
今回の主因ではない可能性が高い
```

最初にF-01～F-03を修正し、その後も残る場合に再評価する。

---

# 12. Presenter Top／Bottom mappingの再確認

Qt layout:

```cpp
region.bottomScreen =
    screenKind[index] != 0;
```

producer physical screen:

```cpp
screenA = GPU.ScreenSwap ? 0 : 1;
screenB = screenA ^ 1;
```

structured screen plane:

```text
screen 0 = physical Top
screen 1 = physical Bottom
```

atlas:

```text
Top:    y = 0～191
Gap:    y = 192～193
Bottom: y = 194～385
```

今回の差分はPresenter layoutを反転していない。

症状はlayout mappingではなくsource ownerとtemporal historyの問題である。

## 禁止する対症療法

- Top／Bottom UVを交換
- `bottomScreen`を反転
- packed Top／Bottom bufferを交換
- atlas領域を交換
- `ScreenSwapAt3D`を無条件反転

これらは正常frameまで壊す。

---

# 13. SPIR-VとCI

## SPIR-V

manifestでは以下のsource/header hashが更新されている。

```text
MelonPrimeVulkanCompositorShader.comp
MelonPrimeVulkanCompositorShaderData.h

MelonPrimeVulkanSurfacePresenter.frag
MelonPrimeVulkanSurfacePresenterFragmentShaderData.h
```

protected-black修正はsource上でcompute／Presenterの両方に存在する。

静的には旧shaderが残っている兆候はない。

## CI

今回HEADに紐づくcombined statusは空である。

```text
statuses: []
```

workflow runも取得されなかった。

```text
workflow_runs: []
```

したがって、次を実行済みとは断定できない。

```text
melonprime_check_vulkan_spirv
Windows Vulkan build
runtime validation
```

ただし今回確認したowner回帰はC++側の論理問題であり、CIが成功しても検出されない可能性が高い。

---

# 14. 推奨修正順序

本監査では実装していない。

## Priority 0: exact capture ownerによるlive owner上書きを撤去

撤去対象:

```cpp
if (exactCaptureOwnerApplies)
    liveSourceScreenSwap =
        softPackedSnapshot.captureScreenSwap;
```

exact capture ownerはcapture source専用に保持する。

```cpp
captureSourceScreenSwap =
    softPackedSnapshot.captureScreenSwap;
```

current renderer image ownerは別に保持する。

```cpp
rendererSnapshotScreenSwap =
    resource.screenSwap;
```

---

## Priority 1: renderer snapshot ownerをactual target ownerで固定

`recordRenderer3dSnapshotCopy()`へ渡すownerは、current targetの実ownerとする。

概念例:

```cpp
const bool rendererSnapshotScreenSwap =
    resource.screenSwap;

recordDirectPresentationPrep(
    frame,
    resource,
    renderer3D,
    rendererSnapshotScreenSwap,
    rendererSnapshotScreenSwap,
    !rendererSnapshotScreenSwap,
    replaceAccumulatedHighres);
```

より安全には、accumulator destinationも別変数にする。

```cpp
const bool accumulateTop =
    rendererSnapshotScreenSwap;

const bool accumulateBottom =
    !rendererSnapshotScreenSwap;
```

---

## Priority 2: capture ownerはcapture bufferだけへ適用

現行`buildCompositionInputs()`の次は維持する。

```cpp
capture3dSourceScreenSwapValid =
    resource.captureScreenSwapValid;

capture3dSourceScreenSwap =
    resource.captureScreenSwap;
```

shaderでは次だけへ使う。

```text
screenMatchesCapture3DSource
captureBackedComp4
regular capture source
VRAM capture source
```

current renderer snapshotのownerへ使わない。

---

## Priority 3: owner mismatch時にtemporal historyをinvalidate

少なくとも次の条件でhistoryをclearする。

```text
rendererSnapshotOwner != screenSwapAt3D
capture owner domainからlive owner domainへ切替
ScreenSwap phase transitionでsource contractが変わる
renderer serialは同じだがowner labelが変わる
```

clear対象:

```text
accumulatedTopHighres
accumulatedBottomHighres
lastTopRendererSourceFrame
lastBottomRendererSourceFrame
lastTopComposedFrame
lastBottomComposedFrame
lastValidTopPacked
lastValidBottomPacked
pending previous source references
```

既存のhistory invalidation APIを一括利用できるなら、個別clearより安全である。

---

## Priority 4: owner変数を型または名前で分離

推奨名:

```cpp
rendererTargetScreenSwap
rendererSnapshotScreenSwap
captureSourceScreenSwap
compositionCurrentSourceScreenSwap
```

`liveSourceScreenSwap`のように複数domainで再利用可能な名前を避ける。

さらに構造体化するなら:

```cpp
struct VulkanSourceOwnership
{
    bool rendererTargetOwner;
    bool rendererSnapshotOwner;
    bool captureSourceOwner;
    bool captureSourceOwnerValid;
};
```

---

## Priority 5: runtime assertion

current target copy直後:

```cpp
assert(
    resource.renderer3dSnapshotScreenSwap
    == softPackedSnapshot.screenSwapLatched);
```

release buildではwarning counterにする。

capture ownerについては別assert:

```cpp
if (resource.captureScreenSwapValid)
{
    // capture buffer用ownerとしてのみ検証
}
```

---

# 15. 最小診断テスト

## Test A: exact live overrideだけ無効化

変更:

```text
CaptureScreenSwap伝搬は維持
capture3dSourceScreenSwapも維持
liveSourceScreenSwapへの代入だけ無効化
```

期待:

- 下画面への上画面混入が大幅に減る
- Bottomの下／上高速交互表示が止まる
- capture-backed 3D自体は維持される
- protected-black改善も維持される

これが最も高い識別力を持つ。

---

## Test B: accumulator一時無効化

一時的に:

```text
accumulateTopHighres = false
accumulateBottomHighres = false
```

期待:

- 上下混入が止まるなら、誤ownerによるaccumulator汚染が直接原因
- 一部のcapture handoff品質は低下し得るため、恒久対応ではない

---

## Test C: previous composed replay一時無効化

一時的に:

```text
replayTopComposedFromPrevious = false
replayBottomComposedFromPrevious = false
```

期待:

- 点滅だけ減る場合、history replayが増幅器
- 上下混入そのものが残る場合、初期原因はsnapshot owner／accumulator

---

## Test D: owner trace

環境変数:

```text
MELONPRIME_VULKAN_2D_TRACE
```

確認項目:

```text
screenSwapAt3D
captureOwnerValid
captureOwner
computedOwner
rendererSnapshotOwner
replayTop
replayBottom
previousTopFrameId
previousBottomFrameId
```

異常条件:

```text
rendererSnapshotOwner
    !=
screenSwapAt3D
```

この不一致が症状frameと同期すれば、F-01～F-03が実証される。

---

## Test E: clean history start

owner修正後に、必ず次のどちらかでテストする。

```text
アプリ完全再起動
ROM停止後再起動
```

または明示的history invalidateを行う。

誤ownerで作られたaccumulatorが残ると、修正後もしばらく点滅が続き、判定を誤る。

---

# 16. 推奨trace形式

```text
frameId
sourceGeneration
renderer3dRenderSerial
renderer3dSnapshotSerial
screenSwapAt3D
rendererTargetOwner
rendererSnapshotOwner
captureOwnerValid
captureOwner
captureUseTopHash
captureUseBottomHash
captureSourceValidHash
exactCaptureOwnerApplies
compositionCurrentSourceOwner
accumulateTop
accumulateBottom
topAccumulatorValid
bottomAccumulatorValid
lastTopRendererSourceFrameId
lastBottomRendererSourceFrameId
previousTopFrameId
previousBottomFrameId
replayTop
replayBottom
class4Pair
cadencePhase
cadenceSuppressed
```

即時warning:

```text
rendererSnapshotOwner != rendererTargetOwner
```

即時history invalidate候補:

```text
rendererSnapshotOwner != rendererTargetOwner
```

capture ownerとrenderer ownerの不一致自体は異常ではない。

```text
captureOwner != rendererTargetOwner
```

は正常に起こり得る。

異常なのは、その不一致を解消しようとしてcapture ownerをrenderer ownerへコピーすることである。

---

# 17. 症状別対応

## 17.1 上画面映像が下画面へ点滅表示

最有力:

```text
current Top-owned renderer image
    ↓
CaptureScreenSwapによりBottom-ownedと誤label
    ↓
Bottom accumulatorまたはBottom previous sourceへ保存
    ↓
BottomへTop内容を表示
```

---

## 17.2 下画面が下／上で高速点滅

最有力:

```text
frame N:
exact capture owner適用
    → Bottom historyへTop source

frame N+1:
packed Bottomまたは別phase history
    → 正常Bottom

frame N+2:
汚染済みBottom history
    → Top source
```

---

## 17.3 症状が以前より改善

説明:

- source-valid／screen-use mask分離が成功
- exact capture source ownerがcompositorまで到達
- protected-black修正が成功
- exact owner frameでClass4 cadenceが抑止
- frame-specific serial traceが追加

ただし、exact ownerを正しいcapture domainだけでなくlive renderer domainへも流したため、残存回帰が発生した。

---

# 18. 受け入れ条件

## owner contract

- [ ] renderer target ownerは`ScreenSwapAt3D`から決まる
- [ ] renderer snapshot ownerはactual copied target ownerと一致
- [ ] capture source ownerは`CaptureScreenSwap`から決まる
- [ ] capture ownerがrenderer snapshot ownerを上書きしない
- [ ] partial capture 1 lineでframe全体のlive ownerを変更しない
- [ ] `rendererSnapshotOwner == rendererTargetOwner`

## accumulator

- [ ] Top-owned current imageをBottom accumulatorへ保存しない
- [ ] Bottom-owned current imageをTop accumulatorへ保存しない
- [ ] owner mismatch時に両accumulatorをinvalidate
- [ ] accumulated source採用時にowner／generationを検証
- [ ] Top／Bottom accumulatorへowner serialを保持

## temporal source

- [ ] lastTopRendererSourceFrameがTop ownerだけを保持
- [ ] lastBottomRendererSourceFrameがBottom ownerだけを保持
- [ ] previous composed replayが汚染済みhistoryを再利用しない
- [ ] ScreenSwap phaseごとのhistoryが混ざらない
- [ ] source contract変更時にpending referencesを解放

## display

- [ ] Top映像がBottomへ1フレームも現れない
- [ ] BottomがTop／Bottomで交互表示されない
- [ ] menu transitionで安定
- [ ] fadeで安定
- [ ] Display Capture開始／終了で安定
- [ ] VRAM display切替で安定
- [ ] ScreenSwap alternating titleで安定

## 回帰

- [ ] protected-black改善を維持
- [ ] capture use mask分離を維持
- [ ] exact capture buffer ownerを維持
- [ ] Class4 cadence抑止を維持
- [ ] Software不変
- [ ] OpenGL不変
- [ ] Vulkan validation error 0
- [ ] SPIR-V同期check成功
- [ ] VSync ON／OFF
- [ ] windowed／fullscreen
- [ ] fast-forward
- [ ] stop／restart

---

# 19. 最終判定

最新pushは前回の主要問題を複数修正しており、症状改善は実装内容と一致する。

しかし、exact capture ownerを導入した際に、owner domainを分離し切れていない。

## 確認済みの中心問題

```text
CaptureScreenSwap
    を
liveSourceScreenSwap
    へ代入
```

その`liveSourceScreenSwap`はcurrent renderer imageのsnapshot ownerとTop／Bottom accumulator destinationへ使われる。

したがって:

```text
capture source owner
    ↓
current renderer target ownerとして誤使用
    ↓
current imageを反対LCD ownerとして保存
    ↓
反対側accumulator／previous sourceを汚染
    ↓
上画面映像が下画面へ混入
    ↓
正常Bottomと汚染Bottomが高速交互表示
```

## 優先修正

```text
1. CaptureScreenSwapによるliveSourceScreenSwap上書きを撤去
2. renderer snapshot ownerをScreenSwapAt3Dへ固定
3. accumulator destinationをrenderer snapshot ownerから決定
4. capture ownerはcapture3dSourceScreenSwap専用にする
5. owner mismatch時にTop／Bottom temporal historyをinvalidate
6. rendererSnapshotOwner == screenSwapAt3Dをruntime検証
```

## 結論

**今回の残存症状は、Top／Bottom packed planeの生成ミスではなく、exact capture ownerをcurrent live renderer imageのownerへ誤適用した回帰が最有力である。**

**下画面の高速点滅は、その誤ownerによってBottom accumulatorとprevious sourceへTop内容が蓄積されることで説明できる。**

**Presenter UV反転、packed buffer交換、ScreenSwap無条件反転は行うべきではない。**

---

# 20. 実装反映（2026-07-17）

本再監査で確定したPriority 0～5を`develop_vulkan`へ反映した。

## 20.1 owner domain分離

- `CaptureScreenSwap`によるcurrent renderer owner上書きを撤去した
- current renderer target ownerを`rendererTargetScreenSwap`として`ScreenSwapAt3D`由来の値へ固定した
- C++側のcompositor入力を`compositionCurrentSourceScreenSwap`へ改名し、capture ownerと同じ変数へ代入できない構造にした
- `CaptureScreenSwap`は`capture3dSourceScreenSwapValid`／`capture3dSourceScreenSwap`だけへ伝搬する
- partial captureやexact capture ownerの有効化はrenderer snapshot全体のownerを変更しない

## 20.2 snapshot／accumulator契約

- current color targetのsnapshot labelを常に`rendererTargetScreenSwap`から設定する
- pre-run snapshotを再利用する場合も、snapshot ownerとrenderer target ownerの一致を必須にする
- current snapshotのTop／Bottom accumulator destinationをrenderer target ownerだけから決める
- Top／Bottom accumulatorへowner、structured generation、renderer snapshot serialを保持する
- accumulator採用時にowner、generation、serialが現在のframe契約と矛盾しないことを検証する
- ownerと異なるLCD accumulatorへの書込み要求を拒否し、両accumulatorを無効化する

## 20.3 temporal history契約

- previous Top sourceにはTop-owned snapshotだけを、previous Bottom sourceにはBottom-owned snapshotだけを許可する
- previous source採用時にsnapshot generationとrenderer serialの前後関係を検証する
- pre-run snapshot ownerまたはaccumulator契約の不一致を検出した場合、既存の`invalidateTemporalHistory()`でaccumulator、last source、composed source、packed carry、pending参照を一括破棄する
- renderer owner修正後もcapture phase history、protected-black、mask分離、Class4 cadence抑止は維持する

## 20.4 runtime invariant／trace

- Debug buildでは`rendererSnapshotOwner == rendererTargetOwner`を`assert`する
- Release buildでもowner不一致、cross-LCD accumulator書込み、非target ownerでのsnapshot copyを即時warningとして出力する
- `MELONPRIME_VULKAN_2D_TRACE=1`の1-frame traceをsnapshot作成とtemporal source選択後へ移動した
- traceへrenderer target／snapshot owner、exact capture適用、composition owner、accumulator更新先、有効性、generation、serial、last／previous source frame、composed replayを追加した
- capture ownerとrenderer ownerの不一致自体は異常扱いせず、別domainとして記録する

## 20.5 静的検証

- Windows MinGW Release Vulkan ON: 成功
- Windows MinGW Release Vulkan OFF: 成功（変更対象Vulkan sourceはbuild graph参照0件）
- 29 Vulkan shaderのsource／generated header: byte-for-byte一致
- GUI／EmuThread boundary strict: findings 0
- MelonPrime SRP／performance audit: 成功
- platform scatter budget: 21／22
- renderer snapshot labelを`CaptureScreenSwap`から代入する経路: 0件
- Top／Bottom previous source採用時のowner検証: 実装済み
- Top／Bottom accumulator採用時のowner／generation／serial検証: 実装済み

ROM上のmenu transition、fade、Display Capture開始／終了、VRAM display切替、ScreenSwap alternating、VSync、fullscreen、fast-forward、stop／restartとVulkan validation error 0は、本実装を使用したruntime受け入れで最終確認する。
