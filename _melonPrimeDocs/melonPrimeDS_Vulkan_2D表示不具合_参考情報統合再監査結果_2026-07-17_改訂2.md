# melonPrimeDS `develop_vulkan` Vulkan 2D表示不具合 参考情報統合・再監査結果 改訂2

**監査日:** 2026-07-17
**対象リポジトリ:** `ag-advania/melonPrimeDS`
**対象ブランチ:** `develop_vulkan`
**対象HEAD:** `368b0b530e9cc282ef713dd3fb83b15379b2e2a2`
**HEADコミット:** `fix(vulkan): complete structured 2D frame ownership`
**比較元frontend:** `SapphireRhodonite/melonDS-android@2c10e59d7209d354e90d9ef4228330bac3f6e794`
**比較元core:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`
**参考資料1:** `melonPrimeDS_develop_vulkan_2D表示不具合_再監査.md`
**参考資料2:** `melonPrimeDS_Vulkan_Sapphire比較再監査書_監査結果_2026-07-17.md`
**改訂内容:** maskの4-bank制約、Finding優先順位、修正案の安全条件を再評価
**コード変更:** 実施していない
**commit／push／PR:** 実施していない

---

# 1. 監査対象の症状

Vulkan選択時、主にゲーム内メニューなどの2D画面で以下が継続している。

1. 上画面と下画面が入れ替わる
2. 下画面が激しく点滅する
3. 一部の黒色が抜け、背後の3D画像または過去フレームが透けて見える
4. 3D画面自体は正常に描画される

今回の監査では、添付された再監査資料を仮説集として扱い、最新HEADのコードとSapphire固定版を照合した。

---

# 2. HEAD確認

`develop_vulkan`は次のコミットと一致している。

```text
368b0b530e9cc282ef713dd3fb83b15379b2e2a2
fix(vulkan): complete structured 2D frame ownership
```

比較結果:

```text
base: 368b0b530e9cc282ef713dd3fb83b15379b2e2a2
head: develop_vulkan
status: identical
ahead_by: 0
behind_by: 0
```

したがって、本書はユーザーが症状継続を確認した最新pushそのものを対象にしている。

---

# 3. 総合結論

追加監査書を現行HEADへ再照合した結果、前回統合版の主要Findingは維持される。

確認済みの重要問題は次の3系統である。

1. completed snapshotに保存される正確な`CaptureScreenSwap`が、SnapshotBuilder以降へ伝搬されない
2. `CaptureLineUses3D`という192-lineの公開fieldへ、実際にはsource availabilityを表す`StructuredCapture3DSourceLineValid`を代入している
3. `structuredHandoffNoCurrent3D`分岐が、protected-blackを持つ黒いabove planeを採用しない

ただし、追加監査書により、前回統合版のmask修正案には重要な訂正が必要と分かった。

```cpp
std::array<u8, 4u * 192u> StructuredCaptureLineUses3D{};
std::array<u8, 192u> StructuredCapture3DSourceLineValid{};
```

`StructuredCaptureLineUses3D`は4 VRAM bank分である一方、completed snapshotの`CaptureLineUses3D`は192 lineだけである。

したがって、次の単純置換は安全ではない。

```cpp
completedFrame.CaptureLineUses3D =
    StructuredCaptureLineUses3D; // サイズも意味も対応しない
```

修正では、次を分離しなければならない。

```text
source availability
    = exact 3D source lineを読み出せるか

screen capture usage
    = 現在表示されるTop／Bottom lineが3D capture sourceを必要とするか

capture-bank usage
    = 各VRAM capture bankのlineに3D lineageが残るか
```

現行の意味契約違反は確認済みだが、正しいscreen-level use maskを作るには、active capture bank、screen line metadata、control planeのいずれをsource of truthにするかを明示する必要がある。

このため、修正の実装優先順位は次とする。

```text
Priority 0-A:
exact CaptureScreenSwapの伝搬
    → producerに正確な値があり、修正契約が明確

Priority 0-B:
mask semanticsの分離
    → 確認済み問題だが、4-bank対応を含む設計が必要

Priority 0-C:
protected-black handoff修正
    → 黒抜けへ直接対応するactive shader欠陥
```

また、前回こちらが最重要原因とした「completed 2D generationと後から取得するVulkan 3D generationの恒常的不一致」は、通常フレーム経路の呼出順を再確認した結果、確認済み主因とは言えない。

通常経路は同期的に実行される。

```text
nds->RunFrame()
    ↓
emuInstance->drawScreen()
    ↓
MainWindow::drawScreen()
    ↓
ScreenPanelVulkan::drawScreen()
```

次の`RunFrame()`は`drawScreen()`完了後にしか開始されない。

明示的な3D generation serialがないことは診断性と不変条件の不足だが、現時点では根本原因ではなくruntime trace対象とする。

---

# 4. 統合Finding一覧

| ID | 優先度 | Finding | 静的確認 | 症状への寄与 |
|---|---:|---|---:|---:|
| F-01 | Critical | exact `CaptureScreenSwap`がadapter境界で失われる | 確認済み | 上下反転、点滅へ非常に高い |
| F-02 | High～Critical | `CaptureLineUses3D`へsource-valid maskを代入 | 確認済み | 点滅、history誤発火へ高い |
| F-03 | High～Critical | handoff分岐がprotected-blackを除外 | 確認済み | 黒抜けへ直接的 |
| F-04 | High | Class4 cadenceが誤分類時に1フレームごとのowner切替を作る | 機構確認済み | 点滅増幅へ高い |
| F-05 | High | wrong mask／ownerがprevious source replayを過剰発火させる | 経路確認済み | 点滅、黒抜けへ高い |
| F-06 | Medium | capture ownerを96-line統計から再推定 | 確認済み | partial captureで不安定 |
| F-07 | Medium | 2D／3D generation相関を実行時検証できない | 確認済み | 診断性不足 |
| F-08 | Medium | compute compositorとpresenter fragmentに合成ロジックが重複 | 確認済み | 修正漏れリスク |
| F-09 | Ruled out | Presenter atlasの単純Top／Bottom反転 | 整合確認 | 除外 |
| F-10 | Ruled out | swapchain／window alphaによる実透過 | opaque確認 | 除外 |

重要度は、コード欠陥の確実性と、修正方法の確実性を分けて評価する。

- F-01は欠陥と修正契約の両方が明確
- F-02は欠陥が明確だが、4-bankからscreen-level maskを作る設計は未確定
- F-03は黒抜けへ直接対応するが、症状1と症状2の主因ではない
- F-04とF-05は上流metadataの誤りを激しい点滅へ増幅する

---

# 5. F-02 High～Critical: capture使用maskとsource-valid maskの混同

## 5.1 内部には意味と次元の異なるmaskがある

`SoftRenderer`は少なくとも次を別配列として保持する。

```cpp
std::array<u8, 4u * 192u> StructuredCaptureLineUses3D{};
std::array<u8, 192u> StructuredCapture3DSourceLineValid{};
std::array<u8, 2u * 192u> StructuredEngineLineUsesCapture3D{};
```

これらは同一概念ではない。

### `StructuredCaptureLineUses3D`

4 VRAM capture bankそれぞれについて、保存されたcapture resultのlineに3D lineageが残るかを表す。

生成処理:

```cpp
lineUses3D =
    lineUses3D
    || (((control >> 24u) & 0x40u) != 0u);

StructuredCaptureLineUses3D[validIndex] =
    lineUses3D ? 1 : 0;
```

`validIndex`はbankとlineを含む。

```text
bank 0: 0～191
bank 1: 192～383
bank 2: 384～575
bank 3: 576～767
```

### `StructuredCapture3DSourceLineValid`

exact 3D source lineをrendererから取得し、`StructuredCapture3DSource`へコピーできたかを表す。

```cpp
std::memcpy(
    StructuredCapture3DSource.data() + rowBase,
    exact3DLine,
    256u * sizeof(u32));

StructuredCapture3DSourceLineValid[line] = 1;
```

これはsource availabilityである。

### `StructuredEngineLineUsesCapture3D`

Top／Bottomへ最終構築されるengine output側で、capture-backed 3D lineageを使用するlineを表すための別配列である。

この配列も2 engine × 192 lineであり、physical Top／Bottomとの対応にはそのフレームのscreen assignmentが関係する。

---

## 5.2 completed snapshotのfield名と実データが一致しない

`StructuredVulkanFrameSnapshot`は192-line fieldを持つ。

```cpp
std::array<u8, 192u> CaptureLineUses3D{};
```

しかし`SwapBuffers()`は次を代入する。

```cpp
completedFrame.CaptureLineUses3D =
    StructuredCapture3DSourceLineValid;
```

したがって、実際の内容は次である。

```text
field name:
CaptureLineUses3D

actual payload:
Capture3DSourceLineValid
```

これは確認済みの意味契約違反である。

---

## 5.3 ただし4-bank maskへ単純差替えしてはいけない

前回統合版は、次の方向を示していた。

```cpp
completedFrame.CaptureLineUses3D =
    StructuredCaptureLineUses3D;
```

これは不正確である。

理由:

1. 左辺は192 line
2. 右辺は4 bank × 192 line
3. current displayが参照するbankはcapture modeとVRAM display状態で変わる
4. 同一フレームでTop／Bottomが別のcapture lineageを参照し得る
5. screen-level use maskとbank-level use maskは同じではない

したがって、F-02は単なる代入先のtypoではなく、adapter contractの設計不足として扱う。

---

## 5.4 後段は公開fieldを使用maskとして扱う

SnapshotBuilderは公開値をそのままコピーする。

```cpp
destination.captureLineUses3dMask =
    source.captureLineUses3d;
```

その後、次の判定に使用する。

```cpp
const bool currentCaptureLine =
    destination.captureLineUses3dMask[y] != 0u;
```

この判定は以下へ影響する。

- same-ScreenSwap phaseからのcapture line recovery
- capture 3D source line fallback
- comp4 placeholder
- previous same-phase source
- current lineがcapture-backedであるという統計

`populateComp4Placeholder()`も同じfieldを使用する。

```cpp
const bool currentCaptureLine =
    snapshot.hasCapture3dSource
    && snapshot.captureLineUses3dMask[y] != 0u;
```

source-valid lineが多い場合、本来そのscreen lineが3D captureを使用しなくてもhistory対象になり得る。

---

## 5.5 安全な修正契約

最低限、次を別fieldにする。

```cpp
std::array<u8, 192> Capture3DSourceLineValid;
```

既存の`CaptureLineUses3D`については、次のいずれかを選ぶ。

### 案A: screen metadataから必要性を導出

Top／Bottomそれぞれについて、already-latchedな以下をsource of truthにする。

- `ScreenLineMeta`
- structured control plane
- display mode
- regular capture flag
- VRAM capture flag
- force-live flag
- structured 3D slot

利点:

- physical Top／Bottomに直接対応
- active bank選択を後段へ漏らさない
- current screenが本当に3D sourceを必要とするかを表現できる

### 案B: bank-aware dataをsnapshotへ保持

```cpp
std::array<u8, 4 * 192> CaptureBankLineUses3D;
u32 activeCaptureBank;
u32 displayedVramBankTop;
u32 displayedVramBankBottom;
```

利点:

- capture lineageを完全に保持できる

欠点:

- adapter contractが大きくなる
- VRAM display mappingを正確にラッチする必要がある

### 案C: 目的別maskをproducerで完成させる

```cpp
std::array<u8, 192> TopScreenNeedsCapture3D;
std::array<u8, 192> BottomScreenNeedsCapture3D;
std::array<u8, 192> Capture3DSourceLineValid;
```

最も後段が単純になるが、producerでphysical screen assignmentまで確定しなければならない。

---

## 5.6 推奨

最初の修正では、既存fieldを実態に合わせて改名する。

```text
CaptureLineUses3D
    ↓
Capture3DSourceLineValid
```

次に、screenが3D captureを必要とするかはTop／Bottomのlatched line metadataとcontrol planeから導出する。

bank-level lineageが不足する場面がruntime traceで確認された場合のみ、4-bank snapshotを追加する。

---

## 5.7 症状との対応

### 下画面点滅

source availabilityをscreen usageとして扱うと、previous source recoveryの対象lineが過大になる。current source、same-phase history、previous composed sourceの選択がフレーム間で変動し得る。

### 黒抜け

本来2D-onlyの黒いlineをcapture-backedと誤認し、last-valid captureまたはprevious 3Dへ置換する経路へ入れ得る。

### 上下入替

このmask単独でLCD ownerを決定するとは限らないが、capture line統計とClass4判定へ影響し、F-01のowner誤推定を増幅し得る。

---

## 5.8 判定

**意味契約違反は確認済み。**

**ただし、正しい修正は4-bank maskの単純コピーではなく、source availabilityとscreen usageの分離である。**

**修正方法の設計が必要なため、実装優先度はexact `CaptureScreenSwap`伝搬と同じPriority 0群に置くが、先に低リスクのF-01を適用する。**

---

# 6. F-01 Critical: exact `CaptureScreenSwap`が途中で失われる

## 6.1 producerは正確な値を保持している

completed snapshotには次が別々にある。

```cpp
bool CaptureScreenSwap;
bool ScreenSwapAt3D;
```

`SwapBuffers()`でも両方を保存している。

```cpp
completedFrame.CaptureScreenSwap =
    StructuredCaptureScreenSwap;

completedFrame.ScreenSwapAt3D =
    GPU.GPU3D.GetRenderScreenSwapAt3D();
```

意味:

```text
ScreenSwapAt3D
    = live Vulkan 3D render sourceのLCD owner

CaptureScreenSwap
    = Display Capture用に取得したexact 3D sourceのLCD phase
```

両者は同義ではない。

---

## 6.2 SnapshotBuilder入力にフィールドがない

`StructuredVulkanSnapshotSource`には次がある。

```cpp
capture3dSource
captureLineUses3d
hasCapture3dSource
captureBackedClass4Only
frontBuffer
screenSwap
generation
```

しかし、`captureScreenSwap`は存在しない。

`SoftPackedFrameSnapshot`にも次しかない。

```cpp
screenSwapLatched
hasCapture3dSource
captureLineUses3dMask
```

したがって、producerが持つ正確な`CaptureScreenSwap`は、Qt adapterから先へ進まない。

---

## 6.3 FrameResourceにもexact ownerがない

`FrameResource`には以下が保存される。

- capture 3D buffer
- capture line mask
- capture fallback line
- screen statistics
- `screenSwap`
- renderer snapshot owner

しかし、producer由来のexact capture ownerはない。

後段の`VulkanCompositionInputs`には次のフィールドがある。

```cpp
bool capture3dSourceScreenSwapValid;
bool capture3dSourceScreenSwap;
```

ただし、この値はproducerから伝搬されたものではなく、統計から再構築される。

---

## 6.4 統計推定

`buildCompositionInputs()`は次の閾値を使う。

```cpp
topUsesRegularCapture3d =
    top.RegularCaptureUses3dLines > 96;

bottomUsesRegularCapture3d =
    bottom.RegularCaptureUses3dLines > 96;

topUsesVramCapture3d =
    top.VramCaptureUses3dLines > 96;

bottomUsesVramCapture3d =
    bottom.VramCaptureUses3dLines > 96;
```

そしてownerを決める。

```cpp
capture3dSourceScreenSwap =
    asymmetricRegularCapture3d
        ? topUsesRegularCapture3d
        : topUsesCurrentCapture3d;
```

問題点:

- partial captureでは96-line閾値を跨ぐ
- fadeやmenu transitionでline countが変動する
- F-01のwrong maskにより使用line統計が過大になる
- TopとBottomの両方にmetadataがある場合にexact ownerを復元できない
- same-phase history回復後のlineが混在する

---

## 6.5 症状との対応

- capture 3D sourceが反対LCDへ供給される
- frameごとにowner推定が変動する
- previous Top／Bottom sourceが反対側へ選ばれる
- Class4 cadenceの入力条件が誤る

---

## 6.6 判定

**確認済みのmetadata loss。F-01と組み合わさると上下反転と点滅を説明できる。**

---

# 7. F-03 Critical: `structuredHandoffNoCurrent3D`でprotected-blackを除外

## 7.1 protected-blackはproducerで生成されている

実在する黒い2D画素は、control alphaの`0x20`で保護される。

```cpp
const bool protectedBlack2D =
    StructuredVulkan2DIsOpaqueBlack(...);

structuredAlpha |=
    protectedBlack2D ? 0x20u : 0u;
```

SnapshotBuilderも再検査し、必要なら`0x20`を補強する。

```cpp
if (black2d)
{
    alpha |= 0x20u;
    control[index] =
        (control[index] & 0x00FFFFFFu)
        | (alpha << 24u);
}
```

したがって、producer metadataが全面的に不足しているわけではない。

---

## 7.2 通常判定ではprotected-blackを使用可能と扱う

compute compositorは次を計算する。

```glsl
bool structuredPlane1Usable2D =
    isStructured2DVisible(above2D)
    || structured2DProtectedBlack;
```

この判定自体は正しい。

---

## 7.3 handoff分岐だけ非黒を要求する

問題の分岐:

```glsl
else if (structuredHandoffNoCurrent3D)
{
    bool aboveVisibleNonBlack =
        structured2DAbove
        && isStructured2DVisible(above2D)
        && ((above2D.r | above2D.g | above2D.b) != 0);

    bool belowVisible =
        isStructured2DVisible(below2D);

    if (aboveVisibleNonBlack)
        composed = above2D;
    else if (belowVisible)
        composed = below2D;
}
```

`above2D`が実在する不透明黒であってもRGBが0なので、`aboveVisibleNonBlack`はfalseになる。

その結果、次のいずれかが残り得る。

- `below2D`
- live 3D
- previous Top 3D
- previous Bottom 3D
- capture 3D
- temporal carry source
- earlier branchで選ばれたcomposed value

これが「黒い部分だけ背後の3Dまたは過去フレームが見える」という症状と直接一致する。

---

## 7.4 presenter fragmentにも同じ欠陥がある

`MelonPrimeVulkanSurfacePresenter.frag`にも、同一の`aboveVisibleNonBlack`分岐が複製されている。

通常表示でcompute compositorがatlasを完成させた後にPresenterが単純sampleする構成でも、以下の理由から両方を同期修正すべきである。

- direct／validation modeとのparity
- 将来の経路切替
- shader source間の修正漏れ防止
- generated SPIR-V headerの一貫性

---

## 7.5 判定

**静的コード上で確認できる直接的な黒抜けバグ。**

---

# 8. F-04 High: Class4 cadenceが点滅を増幅する

`MelonPrimeVulkanOutput`には、Class4 asymmetric pair用のcadence補正がある。

概略:

```cpp
class4AsymmetricCadenceAllowsTop =
    !topUsesVramCapture3d
    || ((phase & 1u) == 0u);

if (topUsesVramCapture3d
    && !class4AsymmetricCadenceAllowsTop)
{
    liveSourceScreenSwap = false;
    class4AsymmetricCadenceSuppressesTop = true;
}

class4AsymmetricCadencePhase =
    (phase + 1u) & 1u;
```

条件成立時、ownerを1フレームごとに意図的に抑制する。

この処理はSapphire由来のtemporal補正であり、存在自体を即バグとは断定できない。

ただし、以下が誤っている場合に激しい点滅へ変わる。

- F-01のcapture use mask
- F-02のcapture owner
- `captureBackedClass4Only`
- VramCapture line count
- StructuredSlot pixel count
- same-phase history
- previous composed source

誤分類された状態でcadenceがactiveになると、sporadicな誤判定ではなく、明示的な偶数／奇数フレーム切替になる。

---

## 判定

- cadence機構の存在: 確認済み
- affected menuでactiveか: runtime traceが必要
- 「激しく点滅」という症状への整合性: 高い
- 根本原因というよりF-01／F-02の増幅器

---

# 9. F-05 High: previous source replayの過剰発火

現行実装には複数のtemporal recoveryがある。

- same-ScreenSwap phase snapshot
- last valid capture source
- previous prepared capture source
- comp4 placeholder
- previous Top renderer source
- previous Bottom renderer source
- accumulated Top high-resolution source
- accumulated Bottom high-resolution source
- previous composed Top LCD
- previous composed Bottom LCD

これらはDisplay Captureやalternating ownershipを再現するために必要であり、全面削除すべきではない。

しかし、F-01とF-02により入力metadataが誤ると、正しい補完ロジックが次へ変わる。

```text
本来2D-onlyのline
    ↓
capture lineと誤認
    ↓
current capture source不足と判定
    ↓
same-phase historyまたはprevious sourceを採用
    ↓
黒いUI部分に過去3Dが出る
```

---

# 10. F-06 Medium: 96-line owner推定はexact値の代替にならない

96-line閾値は、dominant screenを推定するheuristicとしては利用できる。

しかし、producerにexact `CaptureScreenSwap`が存在する以上、通常経路でheuristicを優先する必要はない。

推奨契約:

```text
exact CaptureScreenSwap valid
    → exact値を使用

exact値なし
    → statistics fallback
```

現在はexact値がadapterで消えるため、常にheuristicへ依存する。

---

# 11. F-07 Medium: shader合成ロジックの重複

同種の2D／3D合成ロジックが次へ重複している。

```text
MelonPrimeVulkanCompositorShader.comp
MelonPrimeVulkanSurfacePresenter.frag
```

両方に次が複製されている。

- structured slot
- protected black
- previous source
- capture source
- comp mode
- handoff
- Class4
- capture-backed comp4
- brightness

今回のprotected-black欠陥も両方に存在する。

これは根本原因そのものではないが、修正漏れやSPIR-V parity driftを起こしやすい構造である。

---

# 12. 前回監査の修正: 2D／3D generation不一致を格下げ

## 12.1 前回の主張

前回監査では、次を最重要原因とした。

```text
completed 2D generation N
    +
Qt表示時点のlive 3D generation M
```

## 12.2 呼出順の再確認

実際の通常フレーム経路:

```cpp
nlines = nds->RunFrame();
emuInstance->drawScreen();
```

`EmuInstance::drawScreen()`:

```cpp
for (window)
    window->drawScreen();
```

`MainWindow::drawScreen()`:

```cpp
panel->drawScreen();
```

これらは直接呼出しであり、通常frame compositionは次フレームの`RunFrame()`前に完了する。

したがって、単にQt panelで処理するという理由だけで、2D generationと3D targetが次世代へずれるとは言えない。

## 12.3 残る懸念

次は依然として有用である。

- renderer 3D submit serial
- renderer 3D completed serial
- 2D snapshot generation
- VulkanFrame sourceGeneration
- renderer3dSnapshot serial
- timeline value

これらを追加すれば、不変条件をruntimeで検証できる。

しかし現時点では、明示generation serialの欠如は**診断性不足**であり、確認済み主因ではない。

---

# 13. Presenter Top／Bottom反転説の除外

`ScreenLayout::GetScreenTransforms()`のkind定義:

```text
0 = top screen
1 = bottom screen
```

Vulkan region:

```cpp
region.bottomScreen =
    screenKind[index] != 0;
```

atlas:

```text
Top:    y = 0～191
Gap:    y = 192～193
Bottom: y = 194～385
```

Presenterの基本UV切出しはこのatlasと整合する。

したがって、以下は推奨しない。

- `bottomScreen`を無条件反転
- Top／Bottom UVを交換
- atlasのTop／Bottom領域を交換
- `screenKind`を逆に解釈

症状1は、物理LCD layoutではなく、3D／capture sourceのowner誤判定である可能性が高い。

---

# 14. 実alpha透過説の除外

見えている現象はwindow alphaではなく、source selectionの結果と考えるべきである。

理由:

- swapchainはopaque composite alpha
- output alphaは不透明
- presenter screen drawも不透明
- 過去3D／capture 3D／previous composedを選ぶ明示経路が存在する
- protected-blackを除外するbranchが存在する

したがって、「黒が透明になる」の実態は次である。

```text
黒い2D sourceを選ばない
    ↓
背後の3D／history sourceが残る
```

---

# 15. 症状別の統合判定

## 15.1 上画面と下画面が入れ替わる

最有力:

```text
exact CaptureScreenSwap消失
    +
capture use maskの過大判定
    +
statistics-based owner inference
    +
Class4 owner override
```

Presenter atlas自体の反転ではない。

---

## 15.2 下画面が激しく点滅する

最有力:

```text
wrong capture line mask
    ↓
previous／same-phase source recoveryの発火範囲が変動
    ↓
capture owner heuristicが変動
    ↓
Class4 cadenceが1フレームごとにownerを切替
    ↓
previous Bottom／composed Bottomが交互に表示
```

---

## 15.3 黒色が抜ける

直接原因候補:

```text
structuredHandoffNoCurrent3D
    ↓
above protected-blackを非黒条件で除外
    ↓
below／live／previous／capture sourceが残る
```

F-01のwrong maskによりhistory fallbackが入ることで、さらに悪化する。

---

## 15.4 3D画面は正常

次と整合する。

```text
Vulkan 3D rasterizerは正常
2D structured metadataとtemporal source selectionが異常
```

3D core、texture sampling、polygon pipelineを第一原因とする必要はない。

---

# 16. 推奨修正順序

本監査では実装していない。

## Priority 0-A: exact `CaptureScreenSwap`を全経路へ伝搬

これはproducerに正確な値が存在し、修正契約が最も明確である。

### `StructuredVulkanSnapshotSource`

```cpp
bool captureScreenSwap{};
bool captureScreenSwapValid{};
```

### `SoftPackedFrameSnapshot`

```cpp
bool captureScreenSwapLatched{};
bool captureScreenSwapValid{};
```

### `FrameResource`

```cpp
bool captureScreenSwap{};
bool captureScreenSwapValid{};
```

### `VulkanCompositionInputs`

既存の次へexact値を設定する。

```cpp
capture3dSourceScreenSwapValid
capture3dSourceScreenSwap
```

原則:

```text
exact valid
    → exact ownerを使用

exact invalid
    → statistics fallback
```

`captureScreenSwapValid`は単純に`HasCapture3DSource`だけで決めず、producerがそのownerを確定したことを表す独立valid flagにする。

---

## Priority 0-B: source availabilityとscreen capture usageを分離

最初に、現行fieldの実態を正しく表す。

```cpp
std::array<u8, 192> Capture3DSourceLineValid;
```

次に、Top／Bottomのscreen-level needを以下から導出する。

```text
ScreenLineMeta
structured control
display mode
regular capture flag
VRAM capture flag
force-live flag
structured slot
```

必要なら次を追加する。

```cpp
std::array<u8, 192> TopScreenNeedsCapture3D;
std::array<u8, 192> BottomScreenNeedsCapture3D;
```

避けるべき実装:

```cpp
completedFrame.CaptureLineUses3D =
    StructuredCaptureLineUses3D;
```

`StructuredCaptureLineUses3D`は4 bank × 192 lineであり、192-line fieldへ直接対応しない。

bank-level lineageが必要なら、bank IDと4-bank maskを一体でラッチする。

---

## Priority 0-C: protected-black handoffを修正

次の両shaderを同期して修正する。

```text
MelonPrimeVulkanCompositorShader.comp
MelonPrimeVulkanSurfacePresenter.frag
```

修正前:

```glsl
bool aboveVisibleNonBlack =
    structured2DAbove
    && isStructured2DVisible(above2D)
    && ((above2D.r | above2D.g | above2D.b) != 0);
```

修正方針:

```glsl
bool aboveUsable2D =
    structured2DAbove
    && structuredPlane1Usable2D;
```

またはprotected-blackを明示優先する。

```glsl
if (structured2DAbove && structured2DProtectedBlack)
    composed = above2D;
else if (structured2DAbove && isStructured2DVisible(above2D))
    composed = above2D;
else if (isStructured2DVisible(below2D))
    composed = below2D;
```

---

## Priority 1: Class4 cadenceをexact ownerの後段へ限定

- exact capture ownerが有効ならheuristic ownerで上書きしない
- mask semantics修正前にcadenceを恒久削除しない
- diagnostic flagで一時無効化可能にする
- phase reset条件を明示する
- exact ownerとheuristic ownerの不一致をログ化する
- cadence suppressが偶数／奇数フレームで交互になっていないか記録する

---

## Priority 2: temporal fallbackを個別診断

以下を個別にOFFできる診断フラグを用意する。

- same-phase capture line recovery
- last-valid capture source
- previous prepared capture source
- comp4 placeholder history
- previous renderer source
- previous composed LCD
- accumulated high-resolution source

全面削除ではなく、どの経路が症状を発生させるかを特定する。

---

## Priority 3: 2D／3D serial traceを追加

大規模なproducer packet化の前に、次を記録する。

```text
structuredGeneration
renderer3dRenderSerial
renderer3dColorTargetSerial
renderer3dSnapshotSerial
VulkanFrame sourceGeneration
timeline submissionValue
```

実測で不一致が確認された場合のみ、2D snapshotと3D image leaseを一体化する設計を昇格する。

---

## Priority 4: SPIR-V再生成と同期検証

shader変更後、生成headerを更新する。

想定対象:

```text
MelonPrimeVulkanCompositorShaderData.h
MelonPrimeVulkanSurfacePresenterFragmentShaderData.h
```

次を確認する。

- sourceとgenerated headerの同期
- SPIR-V target environment
- descriptor／push constant layout
- `melonprime_check_vulkan_spirv`
- Vulkan validation error 0

---

# 17. 必須runtime trace

1フレーム1行で次を記録する。

```text
frameId
sourceGeneration
frontBufferLatched
screenSwapAt3D
captureScreenSwapValid
captureScreenSwap
captureUseMaskHash
captureSourceValidMaskHash
captureSourceValid
topRegularCaptureLines
bottomRegularCaptureLines
topVramCaptureLines
bottomVramCaptureLines
topStructuredSlotPixels
bottomStructuredSlotPixels
topProtectedBlackPixels
bottomProtectedBlackPixels
computedCaptureOwner
exactCaptureOwner
liveSourceOwner
rendererSnapshotOwner
class4Pair
class4CadencePhase
class4CadenceSuppressed
previousTopSourceFrameId
previousBottomSourceFrameId
replayTopComposed
replayBottomComposed
presentResult
```

特に確認する条件:

```text
captureUseMaskHash
    !=
captureSourceValidMaskHash
```

これ自体は正常に起こり得る。

異常なのは、両者を同一fieldとして扱うことである。

---

# 18. 診断テスト

## Test A: exact capture owner

`CaptureScreenSwap`をcompositorまで直接伝搬し、exact valid時はstatistics fallbackを無効化する。

期待:

- Top／Bottom入替が止まる
- owner override logが安定する
- partial captureでownerが反転しない
- Class4 pair誤判定が減る

## Test B: mask semantics分離

現行`CaptureLineUses3D`を`Capture3DSourceLineValid`として扱い、screen-level capture needをTop／Bottom metadataから別導出する。

期待:

- previous capture recovery lineが減る
- comp4 placeholder対象lineが安定する
- 下画面点滅が減る
- 黒い2D-only lineがhistoryへ置換されにくくなる

禁止する比較:

```text
4-bank StructuredCaptureLineUses3D
    を
192-line fieldへ無条件コピー
```

## Test C: protected-black branch

handoff branchでprotected-blackを採用する。

期待:

- 黒いUI枠、黒文字、黒背景から3Dが抜けなくなる
- previous sourceが黒領域へ現れなくなる

## Test D: cadence diagnostic OFF

Class4 cadenceだけ一時OFFする。

期待:

- 1フレームおきの点滅が止まれば、cadenceが直接増幅器
- 点滅継続ならprevious replay、mask fallback、owner inferenceが主

## Test E: temporal recovery個別OFF

history経路を1つずつ止める。

期待:

- 黒抜けまたは点滅を作る具体的sourceを特定できる

## Test F: 2D／3D serial trace

コードの動作を変更せずserialだけ記録する。

期待:

```text
structuredGeneration
    ==
renderer3dSnapshotSerial
```

不一致がない場合、generation mismatch仮説を除外する。

---

# 19. 避けるべき対症療法

- Presenter UVを無条件反転
- `screenSwap`を無条件反転
- Top／Bottom packed bufferを交換
- RGB黒をすべて同一扱い
- previous frame機構を全面削除
- Class4 cadenceを根拠なく固定
- capture ownerをTopまたはBottomへ固定
- source-valid maskとuse maskを1フィールドのまま補正
- 4-bank `StructuredCaptureLineUses3D`を192-line fieldへ無条件コピー
- compute shaderだけ修正しfragment shaderを放置
- generated SPIR-V headerを更新しない
- 3D generation serial追加だけで根治扱い

---

# 20. 受け入れ条件

## mask契約

- [ ] source availabilityが`Capture3DSourceLineValid`として独立
- [ ] screen capture usageがTop／Bottom単位で独立
- [ ] bank-level lineageを使う場合はbank IDとmaskを同世代でラッチ
- [ ] 4-bank maskを192-line fieldへ無条件コピーしない
- [ ] SnapshotBuilderが用途ごとに正しいmaskを使う
- [ ] comp4 placeholderがsource-validとscreen-useを混同しない
- [ ] owner統計がsource availabilityだけで増加しない

## owner契約

- [ ] exact `CaptureScreenSwap`がproducerからcompositorまで保持される
- [ ] exact ownerに独立valid flagがある
- [ ] exact ownerが有効な場合はheuristicを使用しない
- [ ] partial captureで96-line閾値を跨いでもownerが反転しない
- [ ] `ScreenSwapAt3D`と`CaptureScreenSwap`を同一視しない
- [ ] Class4 cadenceがexact ownerを逆転させない

## black契約

- [ ] protected-black above planeをhandoff branchが採用
- [ ] 黒い2D pixelがprevious 3Dへ置換されない
- [ ] 黒いUI枠、文字、背景が安定
- [ ] comp mode 1～7でprotected-blackが保持される
- [ ] compute／fragment shader parityがある

## generation契約

- [ ] structured generationをログ化
- [ ] renderer 3D render serialをログ化
- [ ] frame固有snapshot serialをログ化
- [ ] 不一致がないことをruntimeで確認
- [ ] 不一致が確認されるまで大規模frame packet化を前提にしない

## presentation

- [ ] Top／Bottomが1フレームも逆転しない
- [ ] 下画面に偶数／奇数フレーム点滅がない
- [ ] menu、map、pause、result、fadeで安定
- [ ] Display Capture開始／終了で破綻しない
- [ ] VRAM display切替で破綻しない
- [ ] 3D画面品質が現状から劣化しない

## 回帰

- [ ] Software不変
- [ ] OpenGL不変
- [ ] Vulkan OFF build成功
- [ ] Vulkan validation error 0
- [ ] SPIR-V synchronization成功
- [ ] VSync ON／OFF
- [ ] fullscreen／windowed
- [ ] resize
- [ ] renderer切替
- [ ] stop／restart
- [ ] fast-forward

---

# 20A. 改訂2で変更した点

1. exact `CaptureScreenSwap`伝搬を最初の低リスク修正へ昇格
2. mask mismatchの存在は維持しつつ、単純な4-bank maskコピー案を撤回
3. source availability、screen usage、bank lineageを別概念として定義
4. `CaptureLineUses3D`の安全な修正案をTop／Bottom metadata導出へ変更
5. generation mismatchをMediumのruntime trace項目として維持
6. 症状別に原因を分離し、単一の因果鎖として断定しない
7. 受け入れ条件へbank-aware contractを追加

この改訂は監査文書のみであり、リポジトリのコード、commit、branch、PRには変更を加えていない。

---

# 21. 最終判定

追加監査書の中心的な再評価は妥当である。

維持するFinding:

```text
exact CaptureScreenSwapの伝搬欠落
CaptureLineUses3Dとsource-valid maskの意味不一致
protected-black handoff分岐の欠陥
Class4 cadenceによる点滅増幅
statistics-based owner inference
temporal source replay
shader合成ロジック重複
```

修正するFinding:

```text
completed 2D generationとlive 3D targetが
恒常的に別generationである
    ↓
静的コードでは未確認

正しい表現:
2D／3D snapshotのgeneration相関を
実行時に検証できない
```

前回統合版からさらに訂正する点:

```text
誤:
StructuredCaptureLineUses3Dを
completed 192-line fieldへ直接代入する

正:
source availabilityを独立fieldにし、
screen capture usageはTop／Bottom metadataから導出する

bank-level lineageが必要なら、
4-bank maskとactive bank情報を一体でラッチする
```

## 症状別の最有力原因

### 上下入替

**exact `CaptureScreenSwap`のadapter境界での欠落と、96-line統計owner推定。**

### 下画面点滅

**source-valid／screen-useの混同、owner誤推定、Class4 cadence、previous source replayの複合。**

### 黒抜け

**`structuredHandoffNoCurrent3D`がprotected-blackを除外するactive shader分岐。**

### 3D正常

**Vulkan 3D rasterizer本体ではなく、2D metadataとtemporal source selectionの境界に問題があることを支持する。**

## 改訂後の実装順序

```text
1. exact CaptureScreenSwap伝搬
2. source-validとscreen-use maskの分離
3. protected-black shader修正
4. Class4 cadence診断
5. temporal fallback診断
6. 2D／3D serial trace
7. SPIR-V同期と回帰
```

## 統合結論

**最も低リスクで直接的な第一修正はexact capture ownerの伝搬である。**

**mask semantic bugは確認済みだが、4-bank maskを単純コピーせず、source availabilityとscreen usageを設計上分離する必要がある。**

**黒抜けにはprotected-black handoff分岐の修正が必要である。**

**Class4 cadenceとprevious source replayは、上流metadataの誤りを激しい点滅へ増幅する。**

**2D／3D generation mismatchは、serial traceで不一致を確認するまで主因としない。**

---

# 22. 改訂2に対する実装反映（2026-07-17）

本監査で確定したPriority 0-A～0-Cと診断要件を`develop_vulkan`へ反映した。

## 22.1 owner契約

- completed snapshotへ`CaptureScreenSwapValid`を追加し、producerがcapture ownerをラッチした場合だけvalidにする
- `CaptureScreenSwap`とvalidをSnapshotBuilder、`SoftPackedFrameSnapshot`、`FrameResource`、`VulkanCompositionInputs`まで同一世代で伝搬する
- exact owner valid時は96-line統計ownerを使用せず、Class4 cadenceによるowner反転も行わない
- structured screen historyは`ScreenSwapAt3D`、capture source historyは`CaptureScreenSwap`で別phase管理する

## 22.2 mask契約

- source availabilityを`Capture3DSourceLineValid`として独立させる
- physical Top／Bottomのscreen usageを`TopScreenNeedsCapture3D`、`BottomScreenNeedsCapture3D`として独立させる
- screen usageはcompleted generationの`ScreenLineMeta`にあるregular capture／VRAM capture bitから生成する
- 4-bankの`StructuredCaptureLineUses3D`はbank内部のlineageにだけ使用し、192-line fieldへコピーしない
- SnapshotBuilderのhistory recovery、comp4 placeholder、capture fallbackはscreen-useとsource-validを別条件として扱う

## 22.3 protected-black契約

- compute compositorとpresenter fragmentの`structuredHandoffNoCurrent3D`を同期修正する
- `structuredPlane1Usable2D`を採用条件とし、protected-black above planeを非黒RGB条件で除外しない
- 変更したGLSLから生成SPIR-V headerを再生成する

## 22.4 generation／runtime trace

- Vulkan 3D renderごとの`RenderSerial`を追加する
- completed structured generationへ3D render serialをラッチする
- frame resourceへstructured generation、render serial、snapshot serialを保持する
- `MELONPRIME_VULKAN_2D_TRACE=1`でmask hash、exact owner、computed owner、Class4 cadence、previous source、serial mismatchを1フレーム1行で出力する
- present側は同じ環境変数でframe ID、source generation、timeline submission value、present resultを出力する

## 22.5 静的検証

- Windows MinGW Release Vulkan ON: 成功
- Windows MinGW Release Vulkan OFF: 成功
- Vulkan OFF build graphのVulkan source参照: 0件
- 29 Vulkan shaderのsource／generated header: byte-for-byte一致

ROM上のmenu、map、pause、result、fade、Display Capture、VRAM display、fullscreen、resize、renderer切替、stop／restart、fast-forwardは、本実装を使用したruntime受け入れで最終確認する。
