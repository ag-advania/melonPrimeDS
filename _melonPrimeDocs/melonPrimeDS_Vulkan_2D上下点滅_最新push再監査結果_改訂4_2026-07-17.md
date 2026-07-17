# melonPrimeDS `develop_vulkan` Vulkan 2D上下点滅 最新push再監査結果 改訂4

**監査日:** 2026-07-17
**対象リポジトリ:** `ag-advania/melonPrimeDS`
**対象ブランチ:** `develop_vulkan`
**監査対象HEAD:** `dd3b695fd13af5f88b869db5bb5a76f229e97cf4`
**監査対象コミット:** `fix(vulkan): isolate capture history by owner`
**前回HEAD:** `0205acadd201508a021ab311d22b4bbb8e4b01ac`
**前回からの距離:** 1コミットahead、behind 0
**恒久修正:** 本書23章のとおり実装
**commit／push:** 実装検証後に`develop_vulkan`へ実施

---

# 1. 今回確認する症状

最新push後も次が残っている。

1. 下画面へ上画面の映像が混入する
2. 下画面が本来の下画面と上画面の間で点滅する
3. 上画面側も点滅しているように見える
4. capture履歴をowner別にした後もcross-LCD表示が消えていない

前回までは主にBottomへTopが混入する症状だった。

今回、Top自身の点滅も認識できるようになったことは重要である。

```text
Bottom専用capture bufferだけの汚染
```

では、Top自身の点滅を十分に説明できない。

今回の症状は、Top／Bottomの両方が参照する共通source、共通frame phase、または両画面のtemporal source selectionが揺れている可能性を強く示す。

---

# 2. 最新pushの変更範囲

前回HEADから変更されたのは次の5ファイルである。

```text
_melonPrimeDocs/
    melonPrimeDS_develop_vulkan_2D上下点滅_capture履歴再監査_2026-07-17.md

src/frontend/qt_sdl/
    MelonPrimeVulkanOutput.cpp
    MelonPrimeVulkanOutput.h
    MelonPrimeVulkanSnapshotBuilder.cpp
    MelonPrimeVulkanSnapshotBuilder.h
```

変更規模:

```text
MelonPrimeVulkanOutput.cpp
    +303 / -189

MelonPrimeVulkanOutput.h
    +21 / -6

MelonPrimeVulkanSnapshotBuilder.cpp
    +43 / -32

MelonPrimeVulkanSnapshotBuilder.h
    +3 / -1
```

重要なのは、次のlifecycle／renderer coreファイルが変更されていないことである。

```text
src/GPU.cpp
src/GPU3D.h
src/GPU_Soft.cpp
src/GPU3D_Vulkan.cpp
src/GPU3D_Vulkan.h
src/GPU_Vulkan.cpp
```

したがって、最新pushは主にpresentation側のcapture historyとSnapshotBuilderを修正しているが、3D render phaseの確定時点、VCount scheduling、単一ColorImageのownership architectureは変更していない。

---

# 3. 最新pushで修正された事項

コミットの中心目的は次である。

```text
capture historyをowner別に分離する
```

前回監査で問題とした、

```text
Top capture履歴
    ↓
Bottom-owned capture3dBufferへ再利用
```

というglobal historyのcross-owner reuseを防ぐ方向の変更が入っている。

`MelonPrimeVulkanOutput`だけでなく`SnapshotBuilder`にも変更が入り、capture／phase historyの扱いが見直されている。

したがって、前回の次の結論は改訂する。

```text
旧:
ownerなしglobal capture historyが最有力root cause

新:
ownerなしcapture historyは実在した問題だが、
現在の残存症状の単独root causeではない
```

## 判定

最新pushでcapture history isolationは前進している。

しかし症状が残り、Top側も点滅する以上、より上流のframe pairingまたは共通source ownershipを調査する必要がある。

---

# 4. 改訂後の総合結論

## 現在の最有力原因

```text
completed structured 2D frame
    と
Vulkan 3D color target
    のframe phase不一致
```

より具体的には次である。

```text
physical structured 2D frame N
    +
pending／next Vulkan 3D target N+1
```

または、

```text
structured frameが参照すべきcompleted 3D target
    ではなく
presentation時点のcurrent singleton ColorImage
```

をsnapshotしている可能性が高い。

## 根拠

最新pushはcapture historyとSnapshotBuilderを変更したが、次は変更していない。

```text
VCount 192:
current 3D finish

VCount 215:
next 3D owner latch
next 3D rendering start

VCount 262:
structured 2D snapshot publish
```

さらにVulkan rendererは依然として、presentation sourceとして単一のcurrent ColorImage、current owner、current serialを公開するarchitectureである。

そのため、capture履歴をowner別にしても次が残る。

```text
2D planes:
completed frame N

ColorImage:
next phase N+1

owner／serial:
N+1
```

owner metadataとColorImageが同じN+1 phaseなら、現在のowner invariantとserial checkは通過する。

しかしphysical 2D planesだけがNであるため、最終表示は誤る。

---

# 5. F-01 Critical: VCount内部でcompleted／pending phaseが分離されていない

現在のGPU schedulingは概ね次である。

```text
VCount 0～191
    visible 2D scanlineを生成
    structured Top／Bottom planesを構築

VCount 192
    Finish3DRendering()
    visible frameに使われた3D描画を完了

VCount 215
    LatchRenderScreenSwapAt3D(ScreenSwap)
    Start3DRendering()
    次の3D phaseを開始

VCount 262
    FinishFrame()
    SoftRenderer::SwapBuffers()
    completed structured snapshotをpublish
```

`GPU3D::LatchRenderScreenSwapAt3D()`は単一のboolを上書きするだけである。

```cpp
RenderScreenSwapAt3D = screenSwap;
```

completed ownerとpending ownerを別々に保持していない。

`SoftRenderer::SwapBuffers()`はVCount 262時点で次を読み込む。

```cpp
completedFrame.ScreenSwapAt3D =
    GPU.GPU3D.GetRenderScreenSwapAt3D();

completedFrame.Renderer3DRenderSerial =
    Rend3D->GetRenderSerial();
```

しかしVCount 262より前のVCount 215で、次の3D phaseが開始されている。

そのため、published structured frameへpending／next phaseのownerとserialが付く可能性がある。

## 最新pushとの関係

最新pushではこのlifecycleファイル群を変更していない。

したがって、このphase contractは未修正のままである。

## 症状との一致

ScreenSwapまたは3D handoff phaseが交互に変化する場合:

```text
frame N:
2D physical Top／Bottom = phase A
3D current target = phase B

frame N+1:
2D physical Top／Bottom = phase B
3D current target = phase A
```

となると、Top／Bottomの両方が交互に異なるsourceを参照する。

これは次を同時に説明できる。

```text
BottomへTopが混入
Bottomが高速点滅
Top自身も点滅
```

## 判定

**Critical／現在の最有力。**

---

# 6. F-02 Critical: current singleton ColorImageを後からsnapshotする設計

Vulkan rendererはcurrent render targetとして概ね次を持つ。

```text
ColorImage
ColorImageView
CurrentRenderScreenSwap
RenderSerial
```

presentation側はcurrent targetをframe-owned snapshotへcopyする。

問題は、structured 2D snapshot側に、

```text
この2D generationが参照すべきimmutable 3D image slot
```

が存在しないことである。

現在のAPIは概ね次を公開する。

```cpp
GetColorTargetImage()
GetColorTargetImageView()
GetCurrentRenderScreenSwap()
GetRenderSerial()
```

これらは「現在のrenderer state」であり、completed structured frame専用のreferenceではない。

## 成立する問題

```text
VCount 192:
3D frame Sが完成

VCount 215:
同じColorImageを次のrenderへ使用開始

VCount 262:
2D frame Nをpublish

GUI presentation:
その時点のColorImageをcopy
```

VCount 215以降にColorImageが次render用にclear、draw、finalizeされていれば、2D frame Nに対して3D frame S+1または途中状態を合成する。

## Top点滅との関係

単一ColorImageが交互phaseまたは途中状態になると、capture bufferだけではなくcurrent live 3D sourceを使うTop側も点滅する。

Bottomはさらにcapture、previous source、accumulator、composed replayの影響を受けるため、Top映像の混入として強く見える。

## 判定

**Critical。**

owner metadataだけを増やしても、image lifetimeを固定しなければ解消しない。

---

# 7. F-03 Critical: 現行invariantは誤phase内の自己整合性しか検査できない

現行の検証は主に次を比較する。

```text
rendererTargetOwner
rendererSnapshotOwner

renderer3dRenderSerial
renderer3dSnapshotSerial

accumulator owner
accumulator generation
accumulator serial
```

しかし、次の状態ではすべて一致する。

```text
structured 2D frame
    = N

renderer target owner
    = N+1

renderer snapshot owner
    = N+1

renderer render serial
    = S+1

renderer snapshot serial
    = S+1
```

つまり:

```text
renderer metadata
    と
copied renderer image
```

は自己整合している。

不一致なのは、

```text
structured 2D generation N
    と
renderer phase S+1
```

の関係である。

現在はこの明示的なreference contractがない。

## 必要なinvariant

```cpp
structuredFrame.referencedRenderer3dSerial
    ==
rendererSnapshot.serial;
```

さらに、

```cpp
structuredFrame.referencedRenderer3dOwner
    ==
rendererSnapshot.owner;
```

だけでは不十分である。

同じserialのimmutable image slotであることまで確認する必要がある。

## 判定

**Critical。**

現行traceでowner mismatchとserial mismatchが0でも、今回の問題を否定できない。

---

# 8. F-04 High: temporal sourceが両画面の点滅を増幅

`MelonPrimeVulkanOutput`には次のtemporal sourceがある。

```text
Top accumulator
Bottom accumulator
previous Top renderer source
previous Bottom renderer source
last Top composed frame
last Bottom composed frame
packed carry
Class4 cadence
structured handoff replay
```

latest commitではcapture historyのowner分離が中心であり、これらの全機能を廃止したわけではない。

current frameの2D／3D pairingが誤っていると、誤frameが次へ保存される。

```text
current mismatch frame
    ↓
Top／Bottom accumulator
    ↓
last renderer source
    ↓
last composed frame
    ↓
次frameでprevious sourceとして再利用
```

結果として、初期の1frame mismatchが複数frameに持続する。

## Top点滅との関係

BottomだけでなくTop accumulator、Top previous source、Top composed replayも存在する。

したがって、共通current sourceが誤phaseならTop側もtemporal replayによって点滅する。

## 判定

**High／root causeの増幅器。**

最初にtemporal sourceを全停止して、current frame pairingだけを検証する必要がある。

---

# 9. F-05 High: SnapshotBuilder history修正だけではimmutable sourceを作れない

latest commitでは`MelonPrimeVulkanSnapshotBuilder.cpp/.h`にも変更が入っている。

これはphysical Top／Bottom packed recovery、phase history、capture metadataの改善としては妥当である。

しかしSnapshotBuilderが受け取るsourceに次がない限り、完全な解決にはならない。

```text
completed 3D image slot
completed 3D serial
completed 3D owner
completed fence／timeline value
```

SnapshotBuilderがowner別historyを正しく保持しても、presentation側がcurrent mutable ColorImageを参照すればframe pairingは保証できない。

## 判定

**High。**

SnapshotBuilderのhistory keyより先に、completed 3D frame referenceを設計すべきである。

---

# 10. F-06 Medium: capture history isolationは必要だが十分ではない

latest commitのcapture history isolationは撤回すべきではない。

次は引き続き必要である。

```text
Top capture historyはTopだけで再利用
Bottom capture historyはBottomだけで再利用
previous capture sourceはsame-ownerだけ
owner不明historyをowner確定frameへ使わない
renderer fallback sourceのowner provenanceを検証
```

ただし、最新症状から次が分かる。

```text
capture history isolation
    だけでは
current live source／frame phaseを安定化できない
```

## 判定

**Medium／既存修正として維持。**

ここへさらにheuristicを重ねるより、上流のcompleted source lifetimeを直す。

---

# 11. 最新症状から除外しにくくなった箇所

前回まではBottomへのTop混入が中心だったため、Bottom capture bufferの汚染を最優先にできた。

今回Top側にも点滅が見えるため、次を再度対象に含める。

```text
current renderer snapshot
Top accumulator
Bottom accumulator
Top previous renderer source
Bottom previous renderer source
Top composed replay
Bottom composed replay
ScreenSwapAt3D phase
renderer serial phase
frame queue resource lifetime
```

ただし、単純なatlas Y-offset反転は依然として優先度が低い。

Y-offsetが固定で逆なら点滅ではなく恒常的な上下入替になりやすいためである。

---

# 12. 現在も優先度が低い対症箇所

次を先に変更してはならない。

```text
packedTopとpackedBottomの交換
Presenter UVの反転
atlas Top／Bottom領域の交換
ScreenSwapの無条件反転
CaptureScreenSwapをlive ownerへ再適用
Top／Bottom output rectangleの交換
```

現在の症状は固定mappingの逆転より、frameごとにsourceが変わるtemporal／phase問題に一致する。

---

# 13. 最も識別力の高い診断モード

次の順でsourceを1種類ずつ再有効化する。

---

## D-00: packed physical 2D only

完全に無効化する。

```text
current Vulkan renderer source
capture3dBuffer
Top／Bottom previous renderer source
Top／Bottom accumulator
Top／Bottom composed replay
packed carry
Class4 cadence
```

使用するのはcurrent structured packed Top／Bottomだけとする。

### 結果判定

```text
両画面が安定
    → physical packed routingは正常
    → 3D source／temporal sourceが原因

Top／Bottomがまだ点滅
    → SnapshotBuilderまたはphysical packed generationが原因
```

このモードは最初に必要である。

---

## D-01: current completed 3D snapshotだけ追加

capture history、previous source、accumulator、replayは引き続き無効。

current 3D sourceだけを追加する。

ただしcurrent singleton ColorImageを直接使わず、VCount 192時点のcompleted targetを使う。

### 結果判定

```text
ここで点滅が再発
    → completed 2D／3D pairingまたはowner phaseが原因
```

---

## D-02: exact current capture sourceだけ追加

許可:

```text
current exact capture line
same-frame capture owner
```

禁止:

```text
previous capture
global history
renderer fallback
```

### 結果判定

```text
ここでBottom混入が再発
    → exact capture owner／need mask／shader selectionが原因
```

---

## D-03: same-owner previous captureを追加

owner、generation、serialが一致するprevious sourceだけ許可する。

---

## D-04: owner別global capture historyを追加

latest commitのowner別履歴を再有効化する。

---

## D-05: Top／Bottom accumulatorsを追加

各LCDを別々に有効化する。

```text
Top accumulator only
Bottom accumulator only
両方
```

---

## D-06: composed replayを追加

最後に有効化する。

これでどのsourceを追加した瞬間に点滅が戻るか確定できる。

---

# 14. 必須trace

現行`Vulkan2DTrace`へ、frame内phaseを追加する。

## 14.1 VCount 0／visible 2D開始

```text
event=Visible2DStart
VCount
physicalScreenSwap
structuredGenerationCandidate
backBuffer
rendererSerialAtLine0
rendererOwnerAtLine0
```

---

## 14.2 VCount 192／current 3D完成

```text
event=Completed3D
VCount
completed3dSerial
completed3dOwner
completedColorImageSlot
completedTimelineValue
colorHash
```

---

## 14.3 VCount 215／next 3D開始

```text
event=Pending3DStart
VCount
pending3dSerial
pending3dOwner
pendingColorImageSlot
previousCompletedSlot
```

---

## 14.4 VCount 262／structured snapshot publish

```text
event=StructuredPublish
structuredGeneration
publishedReferenced3dSerial
publishedReferenced3dOwner
publishedReferencedImageSlot
currentRendererSerial
currentRendererOwner
currentColorImageSlot
```

---

## 14.5 presenter snapshot copy

```text
event=PresentationSnapshot
frameId
structuredGeneration
requested3dSerial
requested3dOwner
requestedImageSlot
copied3dSerial
copied3dOwner
copiedImageSlot
```

必須invariant:

```cpp
requested3dSerial == copied3dSerial
requested3dOwner == copied3dOwner
requestedImageSlot == copiedImageSlot
```

---

## 14.6 per-screen source selection

Top／Bottomそれぞれについて出力する。

```text
screen=Top／Bottom

packedPlaneUsed
currentRendererUsed
exactCaptureUsed
previousRendererUsed
accumulatorUsed
composedReplayUsed
safeFallbackUsed

selectedSourceSerial
selectedSourceOwner
selectedSourceGeneration

packedHash
source3dHash
composedHash
```

Top flickerとBottom混入が、どのsource hashの変化と同期するかを確認する。

---

# 15. 恒久修正設計

## 15.1 completed／pending 3D frameを分離

```cpp
struct Vulkan3DFrameSlot
{
    VkImage image;
    VkImageView imageView;

    u64 serial;
    bool ownerScreenSwap;

    u64 timelineValue;
    bool valid;
    bool presentationPending;
};
```

少なくとも2slot必要である。

```text
slot A:
completed／presentable

slot B:
pending next render
```

---

## 15.2 VCount 192でcompleted slotを確定

```text
Finish3DRendering
    ↓
completed serial確定
completed owner確定
completed image slot確定
fence／timeline確定
```

この後、completed slotをpresenter完了まで上書きしない。

---

## 15.3 VCount 215は別slotへ描画

```text
next Start3DRendering
    ↓
completed slot以外をrender targetにする
```

同じColorImageをcompleted displayとnext renderingで共有しない。

---

## 15.4 structured snapshotへexact referenceを保存

```cpp
struct StructuredVulkanFrameSnapshot
{
    u64 generation;

    u64 referencedRenderer3dSerial;
    bool referencedRenderer3dOwner;
    u32 referencedRenderer3dSlot;
    u64 referencedRenderer3dTimelineValue;

    // physical Top／Bottom packed data
};
```

---

## 15.5 presentation側はcurrent renderer stateを参照しない

禁止:

```cpp
renderer3D.GetColorTargetImage()
renderer3D.GetCurrentRenderScreenSwap()
renderer3D.GetRenderSerial()
```

をcurrent structured frameのsource of truthとして使用すること。

使用するのはstructured snapshotに保存されたexact completed referenceだけとする。

---

## 15.6 exact pairがない場合はsafe degradation

次の場合:

```text
referenced slotが失効
serial不一致
owner不一致
timeline未完了
```

反対ownerの過去frameやcurrent mutable targetを使わない。

安全な順序:

```text
1. current physical packed 2D
2. current exact capture source
3. same-owner exact completed 3D
4. safe black／2D保持
```

誤screen contentより、1frameの低解像度またはblackを優先する。

---

## 15.7 mismatch frameをhistoryへ保存しない

次のframeはtemporal historyへ登録しない。

```text
2D／3D serial mismatch
owner mismatch
image slot mismatch
timeline incomplete
source provenance unknown
```

対象:

```text
Top accumulator
Bottom accumulator
last Top renderer source
last Bottom renderer source
last Top composed frame
last Bottom composed frame
capture histories
packed carry
```

---

# 16. 最小診断パッチ案

根本設計前に原因を確定する最小変更。

## P-00: temporal source全停止

次をfalse固定する。

```text
topNeedsAccumulatedHighres
bottomNeedsAccumulatedHighres
topAccumulatorAvailable
bottomAccumulatorAvailable
replayTopComposedFromPrevious
replayBottomComposedFromPrevious
previousTopRendererSourceValid
previousBottomRendererSourceValid
packed carry
Class4 cadence
```

---

## P-01: current renderer source無効

compositorへcurrent renderer imageを渡さず、packed 2Dとexact captureだけで表示する。

### 期待

```text
Top点滅が止まる
BottomのTop混入も止まる
```

ならcurrent renderer source／phase mismatchが確定する。

---

## P-02: immutable completed imageを手動snapshot

VCount 192後、VCount 215前にColorImageを別imageへcopyする。

presentationではこのcopyだけを使用する。

### 期待

```text
上下両方が安定
```

ならsingle mutable ColorImageがroot causeである。

---

## P-03: ownerを固定せずserialだけ固定

owner metadataだけではなく、copyしたimageへserialを埋め込み、structured snapshotのserialと完全一致させる。

```cpp
assert(
    structured.renderer3dRenderSerial
        == copiedSnapshot.serial);
```

`<=`ではなく`==`を使う。

---

# 17. テストマトリクス

## 17.1 ScreenSwap

```text
固定Top
固定Bottom
毎frame交互
2frameごと交互
menu transition
fade transition
gameplay transition
```

---

## 17.2 Display Capture

```text
capture disabled
source A only
source B only
A+B blend
VRAM display
partial line capture
capture start frame
capture end frame
capture owner毎frame交互
```

---

## 17.3 Temporal source

```text
packed only
current 3D only
exact capture only
same-owner previous capture
owner-specific global history
Top accumulator only
Bottom accumulator only
composed replay only
全機能
```

---

## 17.4 Presentation

```text
1x
2x
4x
8x
VSync ON
VSync OFF
windowed
fullscreen
frame skip
fast-forward
stop／restart
savestate load
renderer restart
```

---

## 17.5 Platform

```text
Windows Vulkan
Linux X11 Vulkan
Linux Wayland Vulkan
macOS Vulkan対応範囲

Software regression
OpenGL regression
```

---

# 18. 受け入れ条件

## Frame pairing

- [ ] structured generationがexact completed 3D serialを参照する
- [ ] completed 3D slotはpresent完了までimmutable
- [ ] next renderは別slotを使用する
- [ ] pending ownerをcompleted snapshotへ付けない
- [ ] presenterはcurrent singleton targetを使わない
- [ ] requested serialとcopied serialが完全一致
- [ ] requested image slotとcopied slotが完全一致

## Top／Bottom

- [ ] BottomへTopが1frameも混入しない
- [ ] TopへBottomが1frameも混入しない
- [ ] BottomがTop／Bottomで点滅しない
- [ ] Top自身が点滅しない
- [ ] owner毎frame交互でも安定
- [ ] fade／transitionでも安定

## Capture

- [ ] owner別capture historyを維持
- [ ] previous captureはsame-ownerだけ
- [ ] owner不明sourceを使用しない
- [ ] exact capture line provenanceをtrace可能
- [ ] cross-owner reject countを取得可能

## Temporal

- [ ] mismatch frameをaccumulatorへ保存しない
- [ ] mismatch frameをlast renderer sourceへ保存しない
- [ ] mismatch frameをlast composedへ保存しない
- [ ] owner transition時に誤historyを再生しない
- [ ] temporal sourceを順番に有効化しても再発しない

## Build

- [ ] Windows build成功
- [ ] Linux build成功
- [ ] Vulkan validation error 0
- [ ] generated SPIR-V同期check成功
- [ ] Software／OpenGL build不変
- [ ] `MELONPRIME_DS`ガード維持

---

# 19. 回帰させてはならない修正

latest commitで導入した次は維持する。

```text
capture history owner isolation
renderer ownerとcapture ownerの分離
Top／Bottom accumulator owner metadata
previous source owner contract
protected-black処理
exact capture owner伝搬
```

ただし、これらへ追加heuristicを重ねてframe phase mismatchを隠してはならない。

---

# 20. 現在のroot cause順位

## Rank 1 — Critical

```text
completed structured 2D frame
    と
current／pending Vulkan 3D target
    のVCount内部phase mismatch
```

## Rank 2 — Critical

```text
single mutable ColorImageを
completed presentation sourceと
next render targetで共有
```

## Rank 3 — Critical

```text
structured generation
    →
exact completed 3D serial／slot
```

というreference contractの欠落。

## Rank 4 — High

```text
Top／Bottom accumulator
previous renderer source
composed replay
```

による誤frameの持続。

## Rank 5 — High

SnapshotBuilder historyとphysical frame phaseの契約。

## Rank 6 — Medium

capture history／renderer fallbackの残存provenance問題。

latest commitで改善したため順位を下げるが、traceは維持する。

---

# 21. CI状況

対象HEADに関連するpull-request-triggered workflow runは確認できなかった。

したがって、少なくとも今回の監査時点では、

```text
CI成功
Vulkan validation成功
runtime回帰なし
```

は確認できていない。

---

# 22. 最終判定

最新push:

```text
dd3b695fd13af5f88b869db5bb5a76f229e97cf4
fix(vulkan): isolate capture history by owner
```

では、前回指摘したcapture historyのcross-owner reuseを防ぐ方向の修正が入った。

これは必要な修正であり、撤回すべきではない。

しかし次の実機症状が残る。

```text
BottomへTopが混入
Bottomが点滅
Top側も点滅
```

この症状は、Bottom専用capture historyだけの汚染では説明しきれない。

最新commitで変更されていないrenderer lifecycleでは、依然として次の順序で処理される。

```text
visible 2D frame生成
    ↓
VCount 192でcurrent 3D finish
    ↓
VCount 215でnext 3D owner latch／render start
    ↓
VCount 262でstructured snapshot publish
    ↓
presentation時にcurrent ColorImageをsnapshot
```

そのため現在の最有力root causeは、

```text
physical structured 2D frame N
    +
mutable Vulkan 3D target N+1
```

のpairingである。

次に行うべきことは、capture historyへさらに条件を追加することではない。

```text
packed 2D only
    ↓
immutable completed 3D
    ↓
exact capture
    ↓
same-owner history
    ↓
accumulator
    ↓
composed replay
```

の順でsourceを再有効化し、VCount 192時点のcompleted 3D imageをVCount 215以降のnext renderから完全に分離する。

**現時点の最優先修正は、capture historyではなく、completed／pending Vulkan 3D frameのowner、serial、image lifetimeの分離である。**

---

# 23. 改訂4に対する恒久修正実装結果

## 23.1 実装したframe lifecycle

`VulkanRenderer3D`へ4枚のimmutable completed image slotを追加した。

```text
VCount 215／VCount 144
    mutable ColorImageへ次の3Dを描画

VCount 192
    render fence完了待ち
    mutable ColorImageから空きcompleted slotへcopy
    serial／owner／slot／completion valueを固定

VCount 262
    completed slotの参照をStructuredVulkanFrameSnapshotへ移譲

GUI presentation
    structured snapshotのexact referenceをretain
    exact completed slotからpresentation-owned snapshotへcopy
    queue submit後にreferenceをrelease
```

structured ring 2世代、GUI copy中、次のcompleted frameが同時に存在できるよう、2枚ではなく4枚を確保した。slotは参照数が0になるまで再使用しない。

## 23.2 structured snapshotへ追加したexact provenance

```text
Renderer3DCompletedFrameReference
    Serial
    OwnerScreenSwap
    ImageSlot
    CompletionValue
    Valid
```

この参照を次の全経路へ伝搬する。

```text
SoftRenderer::StructuredVulkanFrameSnapshot
    ↓
StructuredVulkanSnapshotSource
    ↓
SoftPackedFrameSnapshot
    ↓
MelonPrimeVulkanOutput::FrameResource
```

`Screen.cpp`はstructured ringからcopyした時点で参照をretainし、completed image viewの取得後にring-copy分をreleaseする。presentation commandのqueue submitが終わるまでview分を保持する。

## 23.3 presenterのexact invariant

current structured frameのsource選択から、次を除去した。

```cpp
renderer3D.GetColorTargetImage()
renderer3D.GetColorTargetImageView()
renderer3D.GetCurrentRenderScreenSwap()
renderer3D.GetRenderSerial()
```

presentation copyを許可する条件は次の完全一致である。

```text
requested serial == completed view serial
requested owner == completed view owner
requested image slot == completed view image slot
requested completion value == completed view completion value
```

## 23.4 mismatch時のsafe degradation

exact referenceが失効、またはserial／owner／slot／completion valueが一致しない場合、current mutable targetや反対owner履歴は使用しない。

```text
current structured packed 2D
    +
same-frame exact capture source（存在する場合）
    +
opaque black 3D source
```

presentation-owned 3D snapshotを不透明黒でclearするため、黒色がQt背景を透過する経路も作らない。

## 23.5 mismatch frameのhistory遮断

exact pair不成立時はtemporal historyをinvalidateし、当該frameを次へ登録しない。

```text
Top／Bottom accumulator
last Top／Bottom renderer source
last Top／Bottom composed frame
capture histories
SnapshotBuilder phase／capture history
packed carry recovery
```

既存のcapture owner isolation、previous source owner contract、protected-black、exact capture owner伝搬は維持した。

## 23.6 追加trace

環境変数`MELONPRIME_VULKAN_2D_TRACE=1`で次を出力する。

```text
event=Visible2DStart
event=Completed3D
event=Pending3DStart
event=StructuredPublish
event=PresentationSnapshot
Vulkan2DSource screen=Top
Vulkan2DSource screen=Bottom
```

`PresentationSnapshot`にはrequested／copiedのserial、owner、slot、completion valueを含める。不一致はWarnで記録し、copy元をblackへ切り替える。

## 23.7 ガードと既存renderer

追加したcore／frontendコードはすべて次の二重guard内に限定した。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
```

Software rendererとOpenGL rendererの非Vulkan経路は変更していない。

## 23.8 検証結果

検証は実装完了後にまとめて実施する。

```text
Windows MinGW build: PASS（tools/build/windows/build-mingw.bat --jobs 1、134/134）
git diff --check: PASS
thread boundary strict audit: PASS（findings 0）
instance state strict audit: PASS（既存22 findings、exit 0）
inc ownership audit: PASS（57 files）
platform scatter audit: PASS（21 / 22）
Linux build: 未実施
Vulkan validation layer: ROM runtime未実施
Top／Bottom実機症状: ROM runtime未実施
```
