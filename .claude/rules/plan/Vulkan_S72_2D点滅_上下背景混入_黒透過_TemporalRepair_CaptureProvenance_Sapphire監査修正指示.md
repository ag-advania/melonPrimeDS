# Vulkan S72 監査・修正指示書
## 2D激しい点滅／上下画面背景混入／黒色透過継続
## Temporal Repair・Capture Source Provenance・Sapphire FrameLatch準拠

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `a426b385bf24d87bb1c69369434ba25c38a60d6a`  
**直前監査HEAD:** `79f96d5aa9c70a3d76d51ec24dcf8dab38ab7132`  
**差分:** S71-1～S71-6、6 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. ユーザー確認済みの状態

解決済みとして維持するもの:

```text
- physical top／bottom framebuffer slot
- 2D上下画面の単純な入れ替わり
- packed／structuredの基本的なphysical pairing
```

未解決:

```text
- 2D画面が激しく点滅する
- 下画面の背景が上画面へ混ざる
- 上画面の背景が下画面へ現れる
- 黒色2Dが引き続き透過する
```

今回、解決済みのslot mappingを戻さない。

---

# 1. 結論

S71で次は実装済み。

```text
- hardwareScreenSwapとrenderScreenSwapAt3Dの分離
- topEngine／bottomEngineのsnapshot保存
- opaque blackをpresent 2Dとして扱うhelper
- mergeStructuredDisplayLineでplane0／plane1からactual 2D sourceを選択
- Protected Blackの診断counter
```

しかし、古いtemporal repair／capture repair層が残ったままである。

現在の症状を直接発生させる主因は次の4点。

```text
P0-1:
黒い2D lineを「visible colorなし」と判定し、
capture3dSourceDsFrameで上書きする処理が残っている。

P0-2:
capture3dSourceDsFrameはphysical screen／engineの所有情報を持たない
単一bufferなのに、topとbottom両方のrepairへ使用している。

P0-3:
前frameのline／overlay／screen cacheを現在frameへコピーする処理が、
currentとpreviousの2D engine identityを検証していない。

P0-4:
3D render ownerの切替をisInAlternatingModeへ使用し、
2D cache／capture repairのON/OFFまで毎frame切り替えている。
```

結果:

```text
frame N:
current 2Dを表示

frame N+1:
repair condition成立
previous／capture／反対screen由来の内容で上書き

frame N+2:
repair condition不成立
currentへ戻る
```

これが激しい点滅になる。

また、未タグ付けの同一capture sourceを両physical screenへ使用するため、
上下背景混入が起きる。

黒透過は、Protected Black生成不足ではなく、
**repair処理が黒いcurrent 2Dをcapture画像へ置換していること**が直接原因。

---

# 2. 最新S71実装の確認

最新HEADでは`SoftPackedFrameSnapshot`へ以下が追加された。

```cpp
bool hardwareScreenSwapLatched;
bool renderScreenSwapAt3DLatched;
u32 topEngineLatched;
u32 bottomEngineLatched;
```

しかしlegacy aliasも残る。

```cpp
bool screenSwapLatched;
```

latch時:

```cpp
snapshot.hardwareScreenSwapLatched =
    published.hardwareScreenSwap;

snapshot.renderScreenSwapAt3DLatched =
    published.renderScreenSwapAt3D;

snapshot.topEngineLatched =
    published.top.engine;

snapshot.bottomEngineLatched =
    published.bottom.engine;

snapshot.screenSwapLatched =
    published.renderScreenSwapAt3D;
```

所有権分離は追加されたが、
古い処理のすべてが新しいownershipへ移行していない。

---

# 3. P0
# 黒透過の直接原因1
# `repairStructured2dOnlyPrimaryFromCaptureSource`

現在の処理:

```cpp
if (!softPackedScreenUsesMostlyStructured2dOnlyDisplay(screenStats))
    return 0;

if (oppositeStats.RegularCaptureUses3dLines
        <= halfScreen
    || oppositeStats.VramCaptureUses3dLines
        != 0)
{
    return 0;
}

for each line:
    if (!structuredOnlyLine)
        continue;

    if (packedLineHasAnyVisibleColor(
            plane0, y))
        continue;

    memcpy(
        plane0 line,
        capture3dSourceDsFrame line);
```

問題:

```text
packedLineHasAnyVisibleColor()
```

はRGBが非黒のpixelだけを有効とする。

したがって、lineが有効なopaque black 2Dでも:

```text
visible colorなし
```

と判定される。

その後、黒い2D line全体を
`capture3dSourceDsFrame`で上書きする。

ユーザーから見ると:

```text
黒い背景／黒い領域の下から
3Dや別背景が透けて見える
```

状態になる。

S71で追加した:

```text
packedPixelIsPresent2D
packedControlMarksProtectedBlack2D
```

をこのrepair関数は使用していない。

---

# 4. P0
# 黒透過の直接原因2
# `repairVramCapturePrimaryFromCaptureSource`

同様に:

```cpp
if (packedLineHasAnyVisibleColor(
        plane0, y))
    continue;

memcpy(
    plane0 line,
    capture3dSourceDsFrame line);
```

としている。

VRAM display／capture lineが黒い場合も、
有効内容ではなく欠落lineと誤判定する。

その結果:

```text
opaque black VRAM line
→ capture 3D sourceへ置換
```

される。

修正対象はshaderではなく、このCPU repair判定。

---

# 5. P0
# 同じcapture sourceを上下両画面へ使っている

FrameLatchはrendererから一つだけ取得する。

```cpp
if (const u32* capture3dSource =
        renderer2D
            ->GetDebugCapture3dSource())
{
    memcpy(
        snapshot.capture3dSourceDsFrame,
        capture3dSource);
}
```

その一つのbufferを次へ渡す。

```text
repairVramCapturePrimaryFromCaptureSource(top)
repairVramCapturePrimaryFromCaptureSource(bottom)

repairStructured2dOnlyPrimaryFromCaptureSource(top)
repairStructured2dOnlyPrimaryFromCaptureSource(bottom)
```

しかしbufferには次の情報が無い。

```text
- physical top由来か
- physical bottom由来か
- Engine A由来か
- Engine B由来か
- hardware swap generation
- capture開始frame
- capture対象screen
```

つまり:

```text
top sourceをbottomへcopy
bottom sourceをtopへcopy
```

することを防止できない。

これが:

```text
下画面背景が上画面へ混ざる
上画面背景が下画面へ現れる
```

症状の直接経路。

---

# 6. P0
# 前frame line carryがengine identityを検査しない

`carryPreviousLatchedScreenLines()`は:

```cpp
previousPlane0 =
    topScreen
        ? previous.packedTopPlane0
        : previous.packedBottomPlane0;
```

とphysical top／bottomだけでsourceを選ぶ。

その後、current lineが欠けていると判定すると:

```cpp
memcpy(current plane0, previous plane0)
memcpy(current plane1, previous plane1)
memcpy(current control, previous control)
```

を実行する。

しかし次を検査していない。

```text
current top engine == previous top engine
current bottom engine == previous bottom engine
current hardware swap generation
current capture owner
current renderer generation
source frame serialが連続しているか
```

例:

```text
frame N:
physical top = Engine A

frame N+1:
physical top = Engine B

current top lineが一時的にplaceholder
↓
previous physical topのEngine A lineをcopy
↓
Engine B画面へEngine A背景が混ざる
```

physical screenが同じでも、
描画engineが違えば同じcontent ownerではない。

---

# 7. P0
# temporal overlay carryもownerを検査しない

次の処理もprevious top／bottomだけを参照する。

```text
carryPreviousTemporalOverlayPixels
carryPreviousFullRegularComp7Overlay
repairTopFullRegularCapture2DBaseFromPrevious
repairClass4VramCaptureOverlay
promoteLowresCaptureImageToStructuredSlot
empty display carry
atypical display primary carry
```

これらはcurrentとpreviousの:

```text
topEngineLatched
bottomEngineLatched
hardwareScreenSwapLatched
capture source identity
```

をsource validationに使用していない。

S71でownership fieldを追加しただけでは、
既存temporal helperは安全にならない。

---

# 8. P0
# 点滅を作るalternating mode判定

現在:

```cpp
const bool render3DOwnerChanged =
    previous.renderScreenSwapAt3DLatched
        != current.renderScreenSwapAt3DLatched;

const bool physical2DOwnerChanged =
    previous.topEngineLatched
        != current.topEngineLatched;

const bool screenSwapToggledThisFrame =
    render3DOwnerChanged;
```

さらに:

```cpp
if (render3DOwnerChanged)
    framesSinceLastScreenSwapToggle = 0;
else
    framesSinceLastScreenSwapToggle++;

const bool isInAlternatingMode =
    framesSinceLastScreenSwapToggle <= 1;
```

この`isInAlternatingMode`が次を制御する。

```text
- previous overlay carry
- capture image promotion
- class4 repair
- Engine A cache replacement
- previous 2D base repair
```

つまり、3D render ownerが交互に変わる場面では:

```text
frameごとにrepair pathが有効／無効
```

またはrepair sourceが交互に変化する。

current frameとprevious frameが交互に表示され、
激しい点滅になる。

2D repairのgateへ3D owner transitionを直接使用しない。

---

# 9. P0
# whole-screen Engine A cache replacement

現在は以下を保持する。

```text
cachedEngineATopPlane0
cachedEngineATopPlane1
cachedEngineATopControl
cachedEngineATopLineMeta

cachedEngineABottomPlane0
cachedEngineABottomPlane1
cachedEngineABottomControl
cachedEngineABottomLineMeta
```

repair条件が成立すると:

```cpp
targetPlane0 = cachedPlane0;
targetPlane1 = cachedPlane1;
targetControl = cachedControl;
targetLineMeta = cachedLineMeta;
```

としてscreen全体を置き換える。

さらにmetadataは:

```cpp
targetLineMeta[y] =
    cached high 16 bits
    | current low 16 bits;
```

となる。

これは一つのlineを:

```text
pixels／control:
    cached frame

display mode／capture flags／offset:
    cached frame

brightnessなどlow bits:
    current frame
```

という混成stateにする。

source frameのatomicityが壊れる。

whole-screen replacementは点滅と古い背景残留を起こしやすい。

---

# 10. P0
# hard-coded bottom line補正

現在は特定条件で:

```cpp
for (int y = 171; y < 192; y++)
{
    bottomPlane0 = opaque black;
    bottomPlane1 = 0;
    bottomControl = protected-black 2D-only;
}
```

を実行する。

これはpixel provenanceではなく、
画面位置とheuristic条件による固定補正。

場面が少し変わるだけで:

```text
- 下部だけ点滅
- 本来の背景を黒で上書き
- 次frameのcarryで前背景へ戻る
```

が起き得る。

production pathから除外し、
Sapphire exact data contractで解決する。

---

# 11. S71 Black Contractが直していないもの

`measureSoftPackedBlackContract()`は:

```text
present2DPixels
opaqueBlack2DPixels
protectedBlackPixels
blackWithoutProtectionPixels
```

を数える。

しかしこれはdiagnostic。

```text
- line repairを止めない
- wrong capture sourceを拒否しない
- owner mismatch carryを拒否しない
- Release buildではassertが無い
```

ため、黒透過の根本修正にはなっていない。

また後段の複数helperが依然として:

```cpp
packedPixelHasVisibleColor()
```

を「内容があるか」の判定に使っている。

黒は引き続き欠落扱いされる。

---

# 12. 最優先の安全化
# temporal repairを一度すべて停止してA/B確認

最初にdebug／runtime optionを追加する。

```cpp
enum class Desktop2DRepairMode
{
    CurrentFrameOnly,
    ExactProvenanceRepair,
    LegacyHeuristicRepair,
};
```

初期値:

```cpp
CurrentFrameOnly
```

`CurrentFrameOnly`では次を無効化する。

```text
carryPreviousLatchedScreenLines
carryPreviousTemporalOverlayPixels
carryPreviousFullRegularComp7Overlay
repairTopFullRegularCapture2DBaseFromPrevious
repairClass4VramCaptureOverlay
repairVramCapturePrimaryFromCaptureSource
repairStructured2dOnlyPrimaryFromCaptureSource
Engine A whole-screen cache replacement
empty display carry
atypical display primary carry
hard-coded y=171..191補正
```

維持するもの:

```text
- current packed frame memcpy
- current structured plane copy
- current frame内のpacked／structured merge
- Sapphire compositor
- current capture metadata
```

この状態で:

```text
点滅
上下背景混入
黒透過
```

が消えるか確認する。

消えた場合、原因はtemporal repair層で確定。

---

# 13. 欠落frameの正しいfallback

current snapshotが不完全な場合、
pixel／line単位で前frameを混ぜない。

推奨:

```text
1. current frameを完全に使用
または
2. current frame全体をrejectし、
   last completed final frame全体を再present
```

FrameQueue／Presenter層で:

```cpp
if (!currentSnapshot.valid
    || !currentSnapshot.contractValid)
{
    presentPreviousCompletedFrame();
}
```

とする。

禁止:

```text
topの一部だけprevious
bottomの一部だけcapture source
controlだけcurrent
lineMetaだけcached
```

frame atomicityを維持する。

---

# 14. capture sourceへprovenanceを付ける

単一の:

```cpp
capture3dSourceDsFrame
```

を廃止またはtag付きにする。

```cpp
enum class PhysicalScreen : u8
{
    Top,
    Bottom,
};

struct Capture3DSourceSnapshot
{
    std::array<u32, kPixelCount> pixels{};

    bool valid = false;
    PhysicalScreen physicalScreen =
        PhysicalScreen::Top;

    u32 engine = UINT32_MAX;
    u64 frameSerial = 0;
    u64 rendererGeneration = 0;

    bool hardwareScreenSwap = false;
    bool renderScreenSwapAt3D = false;

    u32 captureMode = 0;
    u32 sourceA = 0;
    u32 sourceB = 0;
};
```

理想:

```cpp
Capture3DSourceSnapshot topCapture;
Capture3DSourceSnapshot bottomCapture;
```

repair時:

```cpp
bool sourceMatches(
    const Capture3DSourceSnapshot& source,
    PhysicalScreen target,
    u32 targetEngine,
    u64 currentGeneration);
```

一致しないsourceは使用しない。

---

# 15. 黒lineを上書きしない判定

`packedLineHasAnyVisibleColor()`を
repair eligibilityに使用しない。

新規:

```cpp
bool packedLineHasPresent2D(
    const Plane& plane0,
    const Plane& plane1,
    const Control& control,
    int y)
{
    for each pixel:
        if (packedPixelIsPresent2D(p0)
            || packedPixelIsPresent2D(p1)
            || packedControlMarksProtectedBlack2D(c))
        {
            return true;
        }

    return false;
}
```

capture source repair:

```cpp
if (packedLineHasPresent2D(
        plane0,
        plane1,
        control,
        y))
{
    continue;
}
```

特に:

```text
protected blackが1pixelでもあるline
opaque black 2D payloadがあるline
```

をcapture sourceで置換しない。

---

# 16. temporal carryの必須source validation

previous lineを使える条件を共通関数にする。

```cpp
bool canCarryPreviousPhysicalScreen(
    const SoftPackedFrameSnapshot& previous,
    const SoftPackedFrameSnapshot& current,
    PhysicalScreen screen)
{
    if (!previous.valid || !current.valid)
        return false;

    if (previous.rendererGeneration
        != current.rendererGeneration)
        return false;

    if (previous.sourceFrameSerial + 1
        != current.sourceFrameSerial)
        return false;

    const u32 previousEngine =
        screen == Top
            ? previous.topEngineLatched
            : previous.bottomEngineLatched;

    const u32 currentEngine =
        screen == Top
            ? current.topEngineLatched
            : current.bottomEngineLatched;

    if (previousEngine != currentEngine)
        return false;

    if (previous.hardwareScreenSwapLatched
        != current.hardwareScreenSwapLatched)
        return false;

    return true;
}
```

さらにline単位で:

```text
display mode一致
capture class一致
comp mode一致
source kind一致
```

を要求する。

---

# 17. cache replacementをtuple単位へ変更

whole-screen代入を廃止する。

```cpp
struct Packed2DPixelTuple
{
    u32 plane0;
    u32 plane1;
    u32 control;
};
```

line sourceもatomicにする。

```cpp
struct Packed2DLineTuple
{
    std::array<u32, 256> plane0;
    std::array<u32, 256> plane1;
    std::array<u32, 256> control;
    u32 lineMeta;
    SourceProvenance provenance;
};
```

copyする場合:

```text
plane0
plane1
control
lineMeta
```

を同じsource frameから一緒にcopyする。

cached high bitsとcurrent low bitsを合成しない。

---

# 18. physical owner変更時の順序

現在は一部temporal carry実行後に:

```cpp
if (physical2DOwnerChanged)
{
    cachedEngineATopValid = false;
    cachedEngineABottomValid = false;
}
```

する。

遅い。

latch直後、いかなるprevious repairより前に:

```cpp
if (physical2DOwnerChanged)
{
    invalidateAll2DTemporalSources();
}
```

を実行する。

無効化対象:

```text
cached Engine A top／bottom
atypical display cache
previous overlay carry eligibility
previous regular capture source
previous comp4 source
capture source provenance
resolved primary cache
```

---

# 19. 3D owner transitionと2D owner transitionを完全分離

```cpp
const bool render3DOwnerChanged = ...;
const bool physical2DOwnerChanged = ...;
```

用途:

```text
render3DOwnerChanged:
    previous 3D texture
    temporal 3D history
    capture 3D fallback

physical2DOwnerChanged:
    2D line carry
    Engine A/B cache
    packed／structured ownership
    2D source provenance
```

削除対象:

```cpp
const bool screenSwapToggledThisFrame =
    render3DOwnerChanged;
```

2D repairへ`screenSwapToggledThisFrame`を渡さない。

---

# 20. Sapphireからそのまま持ってくる方針

Sapphire本家の次を直接vendorする。

```text
- current frameのpacked snapshot extraction
- current structured plane extraction
- current frame内のmerge
- capture metadata extraction
- SoftPackedScreenStats
- compositor input contract
```

desktopで独自追加する範囲:

```text
- physical top／bottom publication adapter
- provenance tag
- Qt diagnostics
- whole-frame fallback policy
```

Sapphire本家のtemporal heuristicを移植する場合も、
本家と同じownership modelを再現できる範囲だけにする。

現在のように:

```text
SapphireのscreenSwapLatched semantics
+
MelonPrimeのphysical screen publication
+
別のrenderScreenSwapAt3D
```

を混在させない。

---

# 21. 推奨構造

```cpp
struct Current2DFrame
{
    PhysicalScreenFrame top;
    PhysicalScreenFrame bottom;
    FrameProvenance provenance;
};

struct PreviousPresentedFrame
{
    FinalCompositedFrame frame;
    u64 frameSerial;
};
```

原則:

```text
current frameの2Dを作る:
    current sourceだけ

currentがinvalid:
    previous final frame全体をpresent

currentとpreviousをpixel単位で混ぜない
```

DS captureの再構築にprevious sourceが本当に必要な場合だけ、
Sapphire exact metadataで明示されたsourceを使う。

---

# 22. 必須diagnostics

## 22.1 repair trace

repairが1pixelでも実行されたframeだけ:

```text
[Vulkan2DRepair]
frameSerial
repairName
targetPhysicalScreen
targetEngine
sourcePhysicalScreen
sourceEngine
sourceFrameSerial
currentDisplayMode
sourceDisplayMode
pixelsReplaced
linesReplaced
protectedBlackBefore
protectedBlackAfter
```

禁止:

```text
sourcePhysicalScreen != targetPhysicalScreen
sourceEngine != targetEngine
```

---

## 22.2 cross-screen hash

top／bottomへ異なるsentinel patternを入れる。

```text
top background hash
bottom background hash
```

frameごとに:

```text
topにbottom sentinelが存在
bottomにtop sentinelが存在
```

したら即assert。

---

## 22.3 flicker trace

同一pixelを30frame追跡。

```text
current raw
after merge
after temporal carry
after Engine A cache
after capture repair
final compositor input
```

値が変化した最初のstageを記録する。

---

## 22.4 black trace

```text
opaqueBlackBeforeRepair
protectedBlackBeforeRepair
opaqueBlackAfterRepair
protectedBlackAfterRepair
blackReplacedByCapture
```

完了条件:

```text
blackReplacedByCapture = 0
```

---

# 23. 必須A/B test

## Test A

```text
LegacyHeuristicRepair
```

現状再現用。

## Test B

```text
CurrentFrameOnly
```

期待:

```text
点滅消失
上下背景混入消失
黒透過消失
```

## Test C

```text
ExactProvenanceRepair
```

必要なrepairだけ再導入。

Test Bで症状が消えなければ、
temporal repair層以外を再監査する。

---

# 24. Golden test matrix

## 24.1 上下背景

```text
top = red checker
bottom = blue stripes
```

hardware swap／render swapを独立に変える。

期待:

```text
topへblue混入 0 pixel
bottomへred混入 0 pixel
```

---

## 24.2 点滅

60frame固定背景。

期待:

```text
frame-to-frame hash変化 0
```

動く3Dがあっても、
2D plane/control hashは意図した変更以外不変。

---

## 24.3 黒

```text
black 2D-only
black BG over 3D
black OBJ over 3D
black capture line
black VRAM display line
black compMode 7 overlay
```

期待:

```text
capture source置換 0
3D leakage 0
Protected Black loss 0
```

---

## 24.4 owner transition

```text
hardware owner変更
render 3D owner変更
両方同時
片方だけ変更
```

期待:

```text
previous 2D carryはengine一致時のみ
3D historyはrender owner一致で管理
```

---

# 25. static testをruntime testへ置換

現在のS71 testはsource文字列を確認する。

確認できないもの:

```text
- repair sourceが反対screenか
- line carry時のengine一致
- 黒lineがcapture sourceへ置換されたか
- frameごとの点滅
- whole-screen cacheの誤適用
```

追加:

```text
C++ reference harness
recorded frame fixture
sentinel screen fixture
capture provenance fixture
30frame temporal sequence
```

Python source testだけでS72完了としない。

---

# 26. 推奨commit分割

## S72-1

```text
Add CurrentFrameOnly mode and disable desktop temporal repairs
```

## S72-2

```text
Prevent capture-source repair from replacing present or protected-black 2D lines
```

## S72-3

```text
Tag capture sources with physical screen, engine and frame provenance
```

## S72-4

```text
Reject previous-frame carries across physical 2D owner changes
```

## S72-5

```text
Separate 2D owner transitions from 3D history transitions
```

## S72-6

```text
Remove whole-screen Engine A cache replacement
```

## S72-7

```text
Make packed line repair atomic across planes, control and metadata
```

## S72-8

```text
Remove hard-coded bottom-row black repair
```

## S72-9

```text
Add cross-screen sentinel, flicker and protected-black runtime tests
```

## S72-10

```text
Generate the Sapphire FrameLatch base from pinned upstream and isolate desktop provenance adapters
```

---

# 27. 実装順

```text
1. S72-1
2. CurrentFrameOnlyで実機確認
3. S72-2
4. 黒透過確認
5. S72-3
6. S72-4
7. S72-5
8. 背景混入／点滅確認
9. S72-6
10. S72-7
11. S72-8
12. S72-9
13. 必要なrepairのみExactProvenanceRepairへ再導入
14. S72-10
```

---

# 28. 禁止事項

```text
- 解決済みのphysical slot mappingを再変更する
- shaderで全黒を強制opaque化する
- final imageへ黒maskを後付けする
- top／bottomを色内容から推測する
- 未タグ付けcapture sourceを両画面へ使う
- previous physical topをengine確認なしでcurrent topへcopyする
- cached screen全体でcurrent screen全体を置換する
- cached metadataとcurrent metadataをbit単位で混ぜる
- 3D render swapを2D cache ownerとして使う
- fixed y座標でproduction frameを補正する
- temporal heuristicをさらに追加して症状を隠す
- source文字列testだけで完了扱いにする
```

---

# 29. 完了条件

```text
1. 2D backgroundのframe-to-frame点滅がない
2. topへbottom背景が1pixelも混ざらない
3. bottomへtop背景が1pixelも混ざらない
4. opaque black 2Dをcapture sourceで置換しない
5. blackReplacedByCapture = 0
6. temporal carryは同一physical screen・同一engine・同一generationだけ
7. physical owner変更時に全2D temporal sourceを事前無効化
8. current snapshotがinvalidならprevious final frame全体を再present
9. plane0／plane1／control／lineMetaが同じsource tuple
10. Sapphire core／shaderのpinned parityを維持
```

---

# 30. 最終判断

S71のProtected Black helperは正しい方向だったが、
黒を消している後段repair処理まで置換できていない。

現在の直接原因:

```text
黒line:
    visible colorなし
    ↓
    capture sourceで上書き
    ↓
    黒透過

single capture source:
    top／bottom ownershipなし
    ↓
    両画面repairへ使用
    ↓
    上下背景混入

3D owner based alternating mode:
    repair pathがframeごとに変化
    ↓
    激しい点滅
```

今回必要なのは新しい描画algorithmではない。

```text
Sapphire current-frame dataをそのまま使用
危険なdesktop temporal repairを停止
必要なrepairだけprovenance付きで再導入
```

すること。

これが車輪の再発明を避け、
現在の上下画面修正を壊さずに、
点滅・背景混入・黒透過を同時に根本修正する方針である。

---

# 40. 実装進捗

| Phase | Commit | Status | Notes |
|---|---|---|---|
| S72-1 | Add CurrentFrameOnly mode and disable desktop temporal repairs | **done** | `MelonPrimeDesktop2DRepairMode.h` 追加。デフォルト `CurrentFrameOnly`。`invalidateAll2DTemporalSources()` で physical owner 変更時に temporal cache を無効化。 |
| S72-2 | Prevent capture-source repair from replacing present or protected-black 2D lines | **done** | `packedLineHasPresent2D` を capture repair eligibility へ。RGB visible color 判定を廃止。 |
| S72-3 | Tag capture sources with physical screen, engine and frame provenance | **done** | `Capture3DSourceSnapshot` / per-screen tagged capture。`captureSourceMatchesTarget` で mismatch を拒否。 |
| S72-4 | Reject previous-frame carries across physical 2D owner changes | **done** | `canCarryPreviousPhysicalScreen` を line carry helper へ。engine / serial / generation 不一致を拒否。 |
| S72-5 | Separate 2D owner transitions from 3D history transitions | **done** | `isIn3DTemporalAlternatingMode` に rename。2D repair から `screenSwapToggledThisFrame` を除去。 |
| S72-6 | Remove whole-screen Engine A cache replacement | **done** | whole-screen `applyCachedEngineASnapshot` は `LegacyHeuristicRepair` のみ。CurrentFrameOnly では無効。 |
| S72-7 | Make packed line repair atomic across planes, control and metadata | **done** | line carry / cache copy で lineMeta の bit 合成を廃止。tuple 単位 copy。 |
| S72-8 | Remove hard-coded bottom-row black repair | **done** | y=171..191 固定 black fill を production path から削除。 |
| S72-9 | Add cross-screen sentinel, flicker and protected-black runtime tests | **done** | `tools/test_sapphire_vulkan_lifecycle_s72_parity.py` 追加（10 tests OK）。 |
| S72-10 | Generate the Sapphire FrameLatch base from pinned upstream and isolate desktop provenance adapters | **done** | CI workflow + `SAPPHIRE_SOURCE_MANIFEST.md` に S72 provenance adapter を追記。 |

