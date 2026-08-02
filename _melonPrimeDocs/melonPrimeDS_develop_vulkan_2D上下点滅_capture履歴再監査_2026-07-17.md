# melonPrimeDS `develop_vulkan` Vulkan 2D上下点滅 Capture履歴再監査

**監査日:** 2026-07-17
**対象リポジトリ:** `ag-advania/melonPrimeDS`
**対象ブランチ:** `develop_vulkan`
**対象HEAD:** `0205acadd201508a021ab311d22b4bbb8e4b01ac`
**コミット:** `fix(vulkan): separate renderer and capture ownership`
**前回HEAD:** `bb23e9543e38592c3593d2bd0c2718c0662a24de`
**コード変更:** 実施していない

---

# 1. 症状

最新push後も次が継続している。

1. 上画面の2D映像が下画面へ点滅表示される
2. 下画面では、本来の下画面と上画面が高速に交互表示される
3. 前回までの修正で改善はしたが、cross-LCD表示が消えていない

---

# 2. 総合結論

前回指摘した次の問題は修正されている。

```text
CaptureScreenSwap
    ↓
current renderer image ownerへ誤代入
```

現在は次が分離されている。

```text
rendererTargetScreenSwap
    = current Vulkan renderer targetのowner

captureScreenSwap
    = DS-timed capture3dBufferのowner

handoffHeuristicScreenSwap
    = temporal handoff判定用のheuristic
```

renderer snapshotとTop／Bottom accumulatorは
`rendererTargetScreenSwap`に基づいて作成されており、
前回の直接的なowner relabelは解消している。

しかし、capture3dBufferの生成側に別の重大な問題が残る。

```text
capture3dBufferにはownerが付く
    しかし
capture3dBufferへ入れる過去pixelにはownerが付かない
```

具体的には次がowner-unawareである。

```cpp
lastValidCapture3dSource
lastValidCapture3dSourceLines
previousResource->capture3dMapped
renderer3D.GetLine()
```

その結果、Top captureで保存されたpixelを、
次のBottom captureで再利用し、
Bottom ownerのcapture3dBufferとしてshaderへ渡すことができる。

これは現在の症状へ直接一致する。

---

# 3. 前回修正の確認

## 3.1 renderer target ownerの固定

現在は次でrenderer ownerを確定する。

```cpp
resource.screenSwap =
    softPackedSnapshot.screenSwapLatched;

const bool rendererTargetScreenSwap =
    resource.screenSwap;
```

renderer snapshotとaccumulatorは次を使う。

```cpp
recordDirectPresentationPrep(
    frame,
    resource,
    renderer3D,
    rendererTargetScreenSwap,
    rendererTargetScreenSwap,
    !rendererTargetScreenSwap,
    replaceAccumulatedHighres);
```

したがって、capture ownerでcurrent renderer targetを再ラベルする旧問題は修正済みである。

## 3.2 snapshot owner invariant

次の検証も追加されている。

```cpp
resource.renderer3dSnapshotScreenSwap
    == rendererTargetScreenSwap
```

不一致時にはtemporal historyをinvalidateする。

## 3.3 accumulator owner

Top／Bottom accumulatorにも次が追加されている。

```text
owner valid
owner screenSwap
structured generation
renderer serial
```

この修正方針は妥当である。

---

# 4. F-01 Critical: global capture履歴にownerがない

現在のclass memberは次である。

```cpp
std::array<u32, PixelCount>
    lastValidCapture3dSource{};

std::array<u8, LineCount>
    lastValidCapture3dSourceLines{};
```

この履歴には次がない。

```text
owner valid
captureScreenSwap
structured generation
renderer serial
```

一方、各FrameResourceには正確なcapture ownerがある。

```cpp
bool captureScreenSwap;
bool captureScreenSwapValid;
```

つまり、current frameにはownerがあるが、
再利用元のglobal historyにはownerがない。

---

# 5. F-02 Critical: ownerなしglobal履歴をcurrent ownerへ再ラベル

current source lineが存在すると、次のglobal履歴を更新する。

```cpp
memcpy(
    lastValidCapture3dSource + rowOffset,
    preparedCapture3dSource + rowOffset,
    rowBytes);

lastValidCapture3dSourceLines[y] = 1;
```

しかし、この保存時にownerを記録しない。

次のframeでcurrent lineが欠落すると、
次が先に使われる。

```cpp
if (latchedLineHasPixels)
{
    copy lastValidCapture3dSource
        into current capture3dBuffer;
}
```

owner比較がないため、次が成立する。

```text
frame N
capture owner = Top
Top用pixelをglobal履歴へ保存

frame N+1
capture owner = Bottom
current Bottom lineが欠落

global Top履歴をcurrent bufferへコピー
current bufferのowner labelはBottom

shaderはBottomへTop pixelを表示
```

frameごとにcurrent sourceとlatched sourceが切り替われば、
下画面がBottom／Topで高速点滅する。

---

# 6. F-03 Critical: immediate previous frameもowner確認なし

previous sourceは次の条件だけで選ばれる。

```cpp
previousResource != nullptr
&& previousResource->hasPreparedCapture3dSource
```

その後、line単位で次を確認する。

```cpp
previousResource
    ->capture3dSourceLineValidMask[y]

previousResource
    ->captureFallbackLines[y]
```

しかし、次を比較しない。

```cpp
previousResource->captureScreenSwap
    ==
resource.captureScreenSwap
```

したがって、Top ownerのprevious capture bufferを
Bottom ownerのcurrent capture bufferへline単位でコピーできる。

この経路はglobal履歴とは独立してcross-LCD混入を作る。

---

# 7. F-04 Critical: renderer3D fallbackもowner不一致を許可

current capture sourceが不足すると、次を実行する。

```cpp
renderer3D.PrepareCaptureFrame();

const u32* line =
    renderer3D.GetLine(y);

copy line into capture3dBuffer;
```

current renderer target ownerは次である。

```cpp
resource.screenSwap
rendererTargetScreenSwap
```

capture buffer ownerは次である。

```cpp
resource.captureScreenSwap
```

しかしfallback前に次を確認しない。

```cpp
!captureScreenSwapValid
||
captureScreenSwap
    == rendererTargetScreenSwap
```

したがって、current renderer targetがTopで、
current capture buffer ownerがBottomの場合でも、
Top renderer lineをBottom capture bufferへ投入できる。

これは正確なcapture ownerを伝搬しても防げない。

owner labelは正しいBottomのまま、
中身だけがTopになるためである。

---

# 8. F-05 High: lineUses3dがTop／Bottom ORになっている

fallback判断は次である。

```cpp
lineUses3d =
    topScreenNeedsCapture3dMask[y]
    ||
    bottomScreenNeedsCapture3dMask[y];
```

これは「このlineでどちらかのscreenが3Dを必要とする」
ことしか表さない。

しかし、capture3dBufferは単一ownerとしてshaderへ渡される。

```text
Top line need = true
Bottom line need = false
capture owner = Bottom
```

この場合でも`lineUses3d`はtrueになり、
Bottom owner bufferへTop用fallbackを入れる可能性がある。

ownerとscreen needを組み合わせる必要がある。

推奨:

```cpp
const bool ownerScreenNeeds3d =
    !resource.captureScreenSwapValid
        ? topNeed || bottomNeed
        : resource.captureScreenSwap
            ? topNeed
            : bottomNeed;
```

---

# 9. F-06 High: SnapshotBuilder phaseHistory keyの契約が弱い

SnapshotBuilderは次でpacked recovery historyを選ぶ。

```cpp
phaseHistory[
    source.screenSwap ? 1 : 0
]
```

`source.screenSwap`には
`ScreenSwapAt3D`が渡されている。

一方、packed Top／Bottom planeは既にphysical screenへ構築済みである。

したがって、packed line recoveryのphase keyとして
renderer target ownerを使う契約は自明ではない。

次がずれるframeでは危険である。

```text
2D physical screen assignment
3D renderer target owner
Display Capture owner
```

ただしcopyLine自体はTop→Top、Bottom→Bottomであり、
今回の直接主因としてはcapture historyより優先度が低い。

---

# 10. 除外できた箇所

## 10.1 raw packed Top／Bottom copy

次は固定されている。

```text
source screen 0 → packedTop
source screen 1 → packedBottom

packedTop → topPackedBuffer
packedBottom → bottomPackedBuffer
```

直接Top bufferをBottom bufferへコピーする処理は確認されなかった。

## 10.2 compositor atlas mapping

compute shaderの最終mappingは固定である。

```text
output y 0～191
    → composeTopScreenPixel

output y 194～385
    → composeBottomScreenPixel
```

## 10.3 composed-frame replay rectangle

過去の合成済みframeを再生する処理も、
TopはTop領域、BottomはBottom領域を
同じY位置へコピーしている。

単純なY-offsetミスは確認されなかった。

## 10.4 push constant ABI

CPU側`CompositorPushConstants`と
shader側push constant layoutは一致している。

header変更によるfieldずれは確認されなかった。

---

# 11. 最小診断パッチ

最初の診断では、cross-owner fallbackだけを止める。

## 11.1 previous frame reuseのowner制限

```cpp
const bool previousCaptureOwnerMatches =
    previousResource != nullptr
    && resource.captureScreenSwapValid
    && previousResource->captureScreenSwapValid
    && previousResource->captureScreenSwap
        == resource.captureScreenSwap;

const u32* previousPreparedCapture3dSource =
    previousCaptureOwnerMatches
    && previousResource->hasPreparedCapture3dSource
        ? ...
        : nullptr;
```

owner不明時は安全側でreuseしない。

## 11.2 global last-valid cacheの一時無効化

診断用として次をfalseにする。

```cpp
const bool latchedLineHasPixels = false;
```

これでcross-screen点滅が消え、
一部lineがblackまたはlow-resolutionになるなら、
ownerなしglobal履歴が主因と確定する。

## 11.3 renderer fallbackのowner制限

```cpp
const bool rendererFallbackOwnerCompatible =
    !resource.captureScreenSwapValid
    || resource.captureScreenSwap
        == rendererTargetScreenSwap;
```

`renderer3D.GetLine()`は
`rendererFallbackOwnerCompatible`の時だけ許可する。

owner不一致なら次の順にする。

```text
1. same-owner capture history
2. exact current capture source
3. line invalidのままshaderで2D保持
4. black／safe fallback
```

反対ownerのrenderer lineを入れてはならない。

---

# 12. 恒久修正

global capture historyをowner別にする。

```cpp
struct CaptureSourceHistory
{
    std::array<u32, PixelCount> pixels{};
    std::array<u8, LineCount> validLines{};

    u64 structuredGeneration{};
    u64 rendererSerial{};

    bool valid{};
};

std::array<CaptureSourceHistory, 2>
    captureHistoryByOwner{};
```

index:

```cpp
const size_t ownerIndex =
    resource.captureScreenSwap ? 1u : 0u;
```

保存条件:

```text
captureScreenSwapValid
current source line valid
generation monotonic
```

reuse条件:

```text
same owner
history valid
history generation <= current generation
history serial <= current renderer serial
line valid
```

owner不明historyをowner確定frameへ流用しない。

---

# 13. line-level provenanceを追加する案

より安全な設計では、各lineへsource provenanceを持たせる。

```cpp
enum class CaptureLineSource : u8
{
    None,
    CurrentExact2D,
    SameOwnerHistory,
    SameOwnerPreviousFrame,
    SameOwnerRendererFallback
};
```

少なくともdebug buildでは、
各lineについて次をtraceする。

```text
capture owner
source kind
source owner
source generation
source serial
```

不変条件:

```cpp
if (captureScreenSwapValid
    && sourceOwnerValid)
{
    assert(
        sourceOwner
        == captureScreenSwap);
}
```

---

# 14. trace改善

現行`MELONPRIME_VULKAN_2D_TRACE`は、
renderer owner、capture owner、accumulator owner、
previous frame IDを出力できる。

しかし、capture bufferを構成したsource内訳が不足している。

次を追加する。

```text
currentCaptureOwnerValid
currentCaptureOwner

previousCaptureOwnerValid
previousCaptureOwner
previousOwnerMatches

lastValidCaptureOwnerValid
lastValidCaptureOwner
lastValidOwnerMatches

rendererTargetOwner
rendererFallbackOwnerCompatible

linesFromRenderer2d
linesFromLatchedValid
linesFromPreviousFrame
linesFromRenderer3d

crossOwnerLatchedRejected
crossOwnerPreviousRejected
crossOwnerRendererFallbackRejected
```

詳細capture logは現在、
常にfalseを返すdebug gate配下にあるため、
`vulkan2dTraceEnabled()`でも出せるようにする。

---

# 15. テスト手順

## T-01: temporal fallback全停止

次を一時停止する。

```text
global latched capture reuse
previous frame capture reuse
owner不一致renderer fallback
```

期待:

```text
Top映像のBottom混入が停止
高速交互表示が停止
一部capture lineが欠ける可能性あり
```

## T-02: same-owner previousだけ有効

期待:

```text
画面混入なし
欠落lineの一部が回復
```

## T-03: owner別global history有効

期待:

```text
画面混入なし
temporal安定性が回復
```

## T-04: capture owner交互切替

連続frameで次を意図的に作る。

```text
Top
Bottom
Top
Bottom
```

期待:

```text
Top historyはTopだけ
Bottom historyはBottomだけ
rejected cross-owner count > 0でも表示混入なし
```

## T-05: partial line validity

current source valid maskを一部lineだけ欠落させる。

期待:

```text
同じownerのlineだけ回復
反対owner lineは使わない
```

---

# 16. 修正優先順位

```text
Priority 0-A
previous frame capture reuseへowner一致条件を追加

Priority 0-B
global lastValidCapture3dSourceをowner別に分割

Priority 0-C
renderer3D fallbackへowner一致条件を追加

Priority 1
lineUses3dをcapture ownerに対応するscreen needへ限定

Priority 2
capture provenance traceとassertを追加

Priority 3
SnapshotBuilder phaseHistory keyを再設計
```

---

# 17. 最終判定

最新pushはrenderer snapshotとaccumulatorのownerを正しく分離した。

しかし、capture bufferの中身は次のownerなしsourceから構築され得る。

```text
global last-valid capture history
immediate previous frame capture buffer
current renderer3D fallback
```

このため、現在は次の状態になっている可能性が高い。

```text
buffer owner label
    = Bottom

buffer pixel provenance
    = Top
```

shaderはowner labelどおりBottomへ表示するため、
外から見ると上画面の映像が下画面へ現れる。

次frameでcurrent Bottom sourceへ戻れば、
下画面がTop／Bottomで高速点滅する。

静的コード上、このcross-owner reuse経路は確認済みであり、
現在の最有力根本原因である。

---

# 18. 修正実装結果

本監査で特定したPriority 0-AからPriority 3までを実装した。

## 18.1 previous frame capture reuse

previous frameの`capture3dMapped`および`preparedCapture3dSource`は、
次をすべて満たす場合だけ再利用する。

```text
current capture owner valid
previous capture owner valid
current owner == previous owner
previous structured generation <= current structured generation
previous renderer serial <= current renderer serial
```

owner不明frameをowner既知frameへ流用する経路は停止した。

## 18.2 owner別capture history

単一の`lastValidCapture3dSource`を廃止し、次の履歴をTop／Bottom owner別に分離した。

```text
captureHistoryByOwner[2]
topComp4HistoryByOwner[2]
bottomComp4HistoryByOwner[2]
```

各履歴はpixel、line validity、structured generation、renderer serial、validを保持する。
履歴の参照と更新はcapture owner既知かつgeneration／serialが単調な場合だけ許可する。

## 18.3 renderer fallback owner制約

`renderer3D.GetLine()`は次の場合だけcapture sourceとして使用する。

```cpp
!captureScreenSwapValid
    || captureScreenSwap == rendererTargetScreenSwap
```

capture ownerとrenderer target ownerが異なる場合は、renderer fallbackを拒否し、
反対LCDのpixelをcapture bufferへ注入しない。

## 18.4 owner対応line need

capture owner既知時の`lineUses3d`は、ownerに対応するscreen maskだけを参照する。

```text
capture owner Top    -> topScreenNeedsCapture3dMask
capture owner Bottom -> bottomScreenNeedsCapture3dMask
capture owner unknown -> Top OR Bottom
```

## 18.5 provenance invariantとtrace

capture line sourceを次のprovenanceへ分類した。

```text
CurrentExact2D
SameOwnerHistory
SameOwnerPreviousFrame
SameOwnerRendererFallback
```

capture ownerとsource ownerがともに既知なら、owner一致をassertする。
`MELONPRIME_VULKAN_2D_TRACE`ではowner一致、fallback互換性、source別line数、
cross-owner拒否数を`VulkanCaptureTrace`として出力する。

## 18.6 SnapshotBuilder history key

`phaseHistory[2]`を6 phaseへ拡張した。

```text
capture owner unknown: renderer owner別 2 phase
capture owner known: (renderer owner, capture owner)別 4 phase
```

capture source回復とComp4 placeholder回復は、capture owner既知時に
同一capture ownerの履歴だけを使用する。

---

# 19. 実装後検証

## 19.1 完了

```text
Windows MinGW Vulkan ON build: PASS
Windows MinGW Vulkan OFF build: PASS
Vulkan OFF build graphの対象Vulkan frontend参照: 0
Vulkan SPIR-V 29 headers byte-for-byte: PASS
MelonPrime SRP/performance audit: PASS
MelonPrime GUI/EmuThread boundary strict audit: 0 findings
platform scatter budget: 21 / 22 PASS
process-global mutable-state strict audit: 既存baseline 22、今回増加なし
```

## 19.2 実機確認

対象ROMをローカル環境で利用できないため、T-01からT-05の実画面確認は未実施。
ビルド成功は表示症状の解消を保証しないため、次の実機確認が必要である。

```text
2DメニューでTop映像がBottomへ混入しない
BottomがTop／Bottomで交互点滅しない
映像部分で緑画面化しない
黒pixelが透過して背景を見せない
3D画面、Software renderer、OpenGL rendererに退行がない
```
