# Vulkan選択時のスプラッシュ欠落・ROM起動クラッシュ調査
## Sapphire `melonDS-android` 0.7.0.rc4 準拠への改訂版

**作成日:** 2026-07-15  
**改訂日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**参照フロントエンド:** `SapphireRhodonite/melonDS-android`  
**参照タグ:** `0.7.0.rc4`  
**参照URL:** `https://github.com/SapphireRhodonite/melonDS-android/releases/tag/0.7.0.rc4`  
**確認対象build:** `melonPrimeDS v3.4.3 v51.exe`

---

# 1. 改訂方針

前版では、Vulkan ROM起動クラッシュに対して次を推奨していた。

```text
MelonPrimeStructuredSnapshotをsession-owned scratchへ移す
SoftPackedFrameSnapshotをsession-owned scratchへ移す
```

方向性自体は正しい。

ただし、Sapphire `0.7.0.rc4`を参照すると、より適切な方針は次となる。

```text
巨大snapshotを単純な一時scratchとして追加するのではなく、
Sapphireと同じようにEmuInstance単位の永続frame stateとして保持する。

RunFrame前後のVulkan producer transactionを、
FrameQueueのFrame ownershipと一体化する。

可能な範囲で中間のMelonPrimeStructuredSnapshot自体を廃止し、
GPU stateからSoftPackedFrameSnapshotへ直接latchする。
```

スプラッシュについても、Vulkan presenterへ無理にemulation frameを作らせるのではなく、
Sapphireと同じく「emulator frame presentation」と「frontend UI」を分離する。

Qt版では次が最も近い。

```text
Vulkan native surface
＋
frontend-owned Qt splash overlay
```

---

# 2. 現在の症状

1. SoftwareでROMを開いた際のクラッシュは修正済み。
2. Vulkanを選択すると、ROM未起動時のmelonDSスプラッシュが表示されない。
3. Vulkanを選択した状態でROMを開くと、最初のframe生成後に終了する。

最後に出ているログ:

```text
[VulkanSubmitTrace] capture snapshot result=1
```

その後に出る予定の:

```text
[VulkanSubmitTrace] submitCompletedFrame result=...
```

は出ていない。

---

# 3. クラッシュ位置

次は正常完了している。

```text
VulkanContext作成
Vulkan presenter初期化
desktop surface attach
ROM読込
NDS作成
NDS Reset
Vulkan Renderer3D作成
VulkanOutput初期化
最初のNDS::RunFrame
structured 2D snapshot取得
```

停止区間:

```text
MelonPrimeVulkanFrontendSession::submitCompletedFrame()
```

現行実装:

```cpp
bool MelonPrimeVulkanFrontendSession::submitCompletedFrame(
    VulkanRenderer3D& renderer3D,
    const MelonPrimeStructuredSnapshot& snapshot)
{
    ...

    SoftPackedFrameSnapshot packedSnapshot{};
    buildSoftPackedSnapshot(snapshot, frame->frameId, packedSnapshot);

    ...
}
```

呼出元:

```cpp
MelonPrimeStructuredSnapshot snapshot{};

if (captureCompletedSnapshot(
        nds->GPU,
        rendererGeneration,
        snapshot))
{
    submitCompletedFrame(renderer3D, snapshot);
}
```

---

# 4. stack overflow判定

## 4.1 現行MelonPrime側

### `MelonPrimeStructuredSnapshot`

約1.50 MiB。

主な配列:

```text
topPlane0
topPlane1
topControl
topNativeFinal
bottomPlane0
bottomPlane1
bottomControl
bottomNativeFinal
```

### `SoftPackedFrameSnapshot`

約1.69 MiB。

主な配列:

```text
packedTopPlane0
packedTopPlane1
packedTopControl
packedBottomPlane0
packedBottomPlane1
packedBottomControl
capture3dSourceDsFrame
comp4TopPlaceholder
comp4BottomPlaceholder
```

### ネスト合計

```text
約1.50 MiB
＋ 約1.69 MiB
＝ 約3.19 MiB
```

さらにcall frameと他のlocal objectが加わる。

関数prologueで`SoftPackedFrameSnapshot`分のstackを確保した時点で
stack guardへ衝突すれば、関数内の最初のログへ到達せず終了する。

今回のログ位置と一致する。

---

# 5. Sapphire参照実装との差

## 5.1 SapphireはSoftwareで初期化する

参照:

```text
app/src/main/cpp/MelonInstance.cpp
```

Sapphireはconstructorで次の方針を明示している。

```cpp
// Software renderer is always used during initialisation.
// Actual renderer will be set of first frame run
currentRenderer = Renderer::Software;
isRenderConfigurationDirty = true;
```

つまり:

```text
NDS construction
→ Software renderer
→ 最初のrunFrameで実rendererへ切替
```

MelonPrimeも現在、NDS生成直後はSoftwareで開始し、
最初のframe前にrenderer transactionを実行している。

この方向はSapphireと一致しているため維持する。

---

## 5.2 Sapphireは巨大なSoftPacked snapshotをstackへ置かない

参照:

```text
app/src/main/cpp/MelonInstance.h
```

Sapphireは次を`MelonInstance` memberとして保持する。

```cpp
SoftPackedFrameSnapshot lastSoftPackedFrameSnapshot;
SoftPackedFrameSnapshot previousSoftPackedFrameSnapshot;
```

`MelonInstance`自体はheap上のinstance ownerであり、
これらはper-frame stack objectではない。

Sapphireの設計:

```text
instance lifetime
├─ lastSoftPackedFrameSnapshot
└─ previousSoftPackedFrameSnapshot
```

現行MelonPrime:

```text
frame call stack
├─ MelonPrimeStructuredSnapshot
└─ nested SoftPackedFrameSnapshot
```

ここが重大な相違。

---

## 5.3 SapphireはRunFrameとframe ownershipを一体化している

参照:

```text
app/src/main/cpp/MelonInstance.cpp
MelonInstance::runFrame()
```

概略:

```text
1. renderer設定更新
2. FrameQueueからrender Frameを取得
3. Frameの再利用可否を確認
4. Vulkan resourceを準備
5. 必要ならRunFrame前の3D snapshot
6. nds->RunFrame()
7. SoftPackedFrameSnapshotをmemberへlatch
8. VulkanOutput::prepareFrameForPresentation()
9. 有効frameをFrameQueueへpush
```

Sapphireのproducer flow:

```cpp
Frame* renderFrame =
    frameQueue.getRenderFrame(frameQueuePolicy);

...

u32 nLines = nds->RunFrame();

hasLatchedSoftPackedFrame =
    latchSoftPackedFrameSnapshot(
        renderFrame,
        frontbuf,
        preparedFrameScreenSwap,
        useStructuredVulkan2D);

...

vulkanOutput->prepareFrameForPresentation(
    renderFrame,
    nds->GPU,
    frontbuf,
    preparedFrameScreenSwap,
    lastSoftPackedFrameSnapshot,
    renderer3D);

...

frameQueue.pushRenderedFrame(
    renderFrame,
    frameQueuePolicy);
```

重要なのは、snapshotが単独の一時データではなく、
`Frame*`のlifecycleと結びついていること。

---

## 5.4 SapphireのFrameQueueは固定frame pool

参照:

```text
app/src/main/cpp/renderer/FrameQueue.h
```

```cpp
constexpr std::size_t FRAME_QUEUE_SIZE = 9;

std::array<Frame, FRAME_QUEUE_SIZE> frames{};
std::queue<Frame*> freeQueue{};
std::deque<Frame*> presentQueue{};
```

現行MelonPrimeの`VulkanReference/FrameQueue.h`も、
この構造を既にSapphire `0.7.0.rc4`から移植している。

したがって、追加のsnapshot所有も同じ思想へ合わせる。

```text
固定frame pool
固定instance state
明示的なFrame ownership
per-frame巨大allocationなし
```

なお、物理slot数が9でも、SapphireのVulkan realtime policyは
`MaxBacklogDepth`を通常1～2に抑えている。

```text
physical resource pool
≠
表示遅延として許可するbacklog
```

低遅延を維持するため、MelonPrimeでもこの区別を保つ。

---

# 6. 現行portでSapphireから外れた箇所

現行MelonPrimeは、Sapphire由来の次を既に持つ。

```text
FrameQueue
FrameQueuePolicy
VulkanOutput
VulkanSurfacePresenter
SoftPackedFrameSnapshot
VulkanCompositionInputs
```

しかし、その外側へ独自に次を追加した。

```text
MelonPrimeVulkanFrontendSession
MelonPrimeStructuredSnapshot
captureCompletedSnapshot()
submitCompletedFrame()
```

このadapterで:

```text
GPU structured snapshot
→ 巨大中間copy
→ SoftPacked snapshotへ再変換
→ VulkanOutput
```

という一段多い経路になっている。

Sapphireは基本的に:

```text
GPU state
→ instance-owned SoftPacked snapshotへlatch
→ VulkanOutput
```

である。

今回のstack overflowは、Sapphireの固定instance stateを
stack localへ変形したことで発生したport固有不具合と判断できる。

---

# 7. Sapphire準拠の推奨修正

## Phase 1: 即時クラッシュ修正

最初にstack localを完全に除去する。

### `MelonPrimeVulkanFrontendSession`へmember追加

```cpp
MelonPrimeStructuredSnapshot structuredSnapshot;
SoftPackedFrameSnapshot lastSoftPackedFrameSnapshot;
SoftPackedFrameSnapshot previousSoftPackedFrameSnapshot;
```

ただし`MelonPrimeVulkanFrontendSession`自体は
`std::unique_ptr`でheap上に所有されていることを維持する。

### submit前

```cpp
structuredSnapshot = {};

if (!captureCompletedSnapshot(
        nds->GPU,
        rendererGeneration,
        structuredSnapshot))
{
    return false;
}
```

### packed snapshot更新

```cpp
std::swap(
    previousSoftPackedFrameSnapshot,
    lastSoftPackedFrameSnapshot);

lastSoftPackedFrameSnapshot.clear();

buildSoftPackedSnapshot(
    structuredSnapshot,
    frame->frameId,
    lastSoftPackedFrameSnapshot);
```

### VulkanOutputへ渡す

```cpp
output.prepareFrameForPresentation(
    frame,
    nds->GPU,
    0,
    structuredSnapshot.screenSwap,
    lastSoftPackedFrameSnapshot,
    renderer3D,
    frameView);
```

これで、Sapphireと同じ:

```text
last snapshot
previous snapshot
```

の永続二重保持になる。

---

## Phase 2: 中間`MelonPrimeStructuredSnapshot`を廃止する

Phase 1は安全な応急修正だが、
約1.50 MiBの中間copyは残る。

Sapphireへ近づける恒久形:

```cpp
bool latchSoftPackedFrameSnapshot(
    Frame* frame,
    int frontBuffer,
    bool screenSwap,
    bool useStructuredVulkan2D);
```

内部でGPU producer stateから直接:

```text
packedTopPlane0
packedTopPlane1
packedTopControl
packedTopLineMeta
packedBottomPlane0
packedBottomPlane1
packedBottomControl
packedBottomLineMeta
```

を`lastSoftPackedFrameSnapshot`へ格納する。

推奨flow:

```text
GPU::CopyStructured2DFrameSnapshot()
```

を巨大な独立aggregate全体のcopy APIとして維持するのではなく、
次のどちらかへ変更する。

### 案A: destinationをSoftPacked形式にする

```cpp
bool GPU::CopyStructured2DFrameSnapshot(
    SoftPackedFrameSnapshot& destination,
    u64 expectedGeneration);
```

### 案B: read-only viewを返す

```cpp
Structured2DFrameView GPU::AcquireStructured2DFrameView();
```

viewからsession-owned snapshotへ変換する。

案Bの方がcopy回数を減らせるが、
renderer output lease／generation／thread ownershipを厳密にする必要がある。

---

## Phase 3: Sapphire型producer transactionへ統合する

現在:

```text
NDS::RunFrame()
→ submitVulkanFrontendFrame()
→ sessionがFrame取得
→ snapshot
→ prepare
→ compose
```

推奨:

```text
Frame取得
→ resource準備
→ 必要ならpre-run 3D snapshot
→ NDS::RunFrame()
→ 2D snapshot latch
→ prepare
→ FrameQueue push
```

つまりSapphireの`MelonInstance::runFrame()`に近づける。

MelonPrimeでは`EmuThread::frameAdvanceOnce()`内へ
すべて直接書き込むのではなく、次のwrapperを作る。

```cpp
class MelonPrimeVulkanFrameProducer
{
public:
    bool beginFrame(
        NDS& nds,
        VulkanRenderer3D& renderer3D,
        u64 rendererGeneration,
        u64 surfaceGeneration);

    bool completeFrame(
        NDS& nds,
        VulkanRenderer3D& renderer3D,
        u64 rendererGeneration);
};
```

使用:

```cpp
Frame* preparedFrame = nullptr;

if (videoBackend == PresentationBackend::Vulkan)
{
    preparedFrame =
        vulkanFrontendSession.beginProducerFrame(...);
}

const u32 nlines = nds->RunFrame();

if (videoBackend == PresentationBackend::Vulkan)
{
    vulkanFrontendSession.completeProducerFrame(
        ..., preparedFrame);
}
```

これにより:

```text
Frame ID
renderer generation
surface generation
snapshot serial
Vulkan resources
```

を一つのtransactionとして扱える。

---

# 8. compose/present責務もSapphireへ合わせる

Sapphireではproducer側が主に:

```text
prepareFrameForPresentation()
FrameQueueへpush
```

を行う。

GUI presenter側が:

```text
FrameQueueからpresent candidate取得
waitForFrame()
必要ならcomposeAndSubmitFrame()
swapchainへpresent
```

を行う。

現行MelonPrimeの`submitCompletedFrame()`はproducer側で:

```cpp
output.composeAndSubmitFrame(frame, inputs);
```

まで実行している。

これは必ずしも不正ではないが、
Sapphireの責務分離から外れる。

中期的には次へ寄せる。

```text
EmuThread producer
→ Frame resource準備
→ snapshot upload
→ VulkanCompositionInputs保存
→ FrameQueue push

GUI presenter
→ Frame acquire
→ composeAndSubmitFrame
→ swapchain present
→ commit／defer
```

既に現行sessionは:

```cpp
std::unordered_map<Frame*, VulkanCompositionInputs> frameInputs;
```

を持っているため、Sapphire型へ戻しやすい。

---

# 9. runtime failure処理もSapphireへ合わせる

SapphireはVulkan prepare failureを回数管理する。

概略:

```text
prepare失敗
→ presentation resync要求
→ failure count++
→ 一定回数でhandleVulkanRuntimeFailure()
→ frontendへrenderer failure通知
→ NDS停止
```

現行MelonPrimeも、クラッシュではなく次のtransactionへする。

```text
Vulkan stage失敗
→ failed stage記録
→ Frameをdiscard
→ presenter resync
→ 一定回数連続失敗でSoftware fallback
→ configは必要に応じて維持
→ OSD／logで通知
```

推奨counter:

```cpp
u32 consecutivePrepareFailures = 0;
u32 consecutiveSubmitFailures = 0;
u32 consecutivePresentFailures = 0;
```

成功時に対応counterを0へ戻す。

---

# 10. スプラッシュ欠落の原因

## NativeQt

NativeQtは停止中に`QPainter`で:

```text
黒背景
melonDS logo
splash text 0
splash text 1
splash text 2
```

を直接描画する。

## Vulkan

`ScreenPanelVulkan::paintEvent()`は:

```cpp
presentOnGuiThread();
```

しか行わない。

`presentOnGuiThread()`は:

```cpp
Frame* frame =
    session.acquirePresentFrame();

if (frame == nullptr)
    return;
```

ROM未起動時はproducer frameがないため、何も表示されない。

---

# 11. Sapphire presenterのbackground機能

Sapphire `VulkanSurfacePresenter`は:

```cpp
struct VulkanBackgroundImage
{
    const u32* pixels;
    u32 width;
    u32 height;
};
```

を持ち、surface config時にbackground textureを作成できる。

またpresenter shaderには:

```text
kDrawModeBackground
```

がある。

ただし、Sapphireの`presentFrame()`は最初に:

```cpp
if (frame == nullptr
    || !output.waitForFrame(frame, timeoutNs))
{
    return false;
}
```

を実行する。

つまり、background imageも
**emulation Frameをpresentする際の背景layer**であり、
frameがない状態のstandalone splash APIではない。

そのため、Sapphireのbackground imageへmelonDS splashを渡すだけでは、
ROM未起動時の問題は解決しない。

---

# 12. Sapphireに近いスプラッシュ実装

## 推奨: frontend UI overlay

Sapphire Android版では、
native Vulkan presenterとAndroid frontend UIが別layerになっている。

Qt版でも同じ責務分離にする。

```text
MainWindow / ScreenPanelVulkan
├─ Vulkan native surface
└─ NoRomSplashOverlay QWidget
```

`NoRomSplashOverlay`:

```text
通常のQt paint engineを使用
黒背景を描画
既存splashLogoを描画
既存localized splashTextを描画
mouse eventを透過
focusを取得しない
panel resizeへ追従
```

設定:

```cpp
splashOverlay->setAttribute(
    Qt::WA_TransparentForMouseEvents,
    true);

splashOverlay->setFocusPolicy(
    Qt::NoFocus);

splashOverlay->setGeometry(rect());
splashOverlay->raise();
```

表示条件:

```cpp
const bool showSplash =
    !emuInstance->getEmuThread()->emuIsActive();

splashOverlay->setVisible(showSplash);
```

first present成功まで表示を維持したい場合:

```text
emu active
かつ
lastPresentedFrameId != 0
```

になった時点でhideする。

これによりVulkan初期化中の黒画面も回避できる。

---

# 13. frontend overlayがSapphireに近い理由

次の実装は避ける。

```text
no-ROM専用dummy NDS Frameを生成
blank VulkanOutput Frameを生成
presenterへ特殊null Frameを許可
background-only swapchain presentをcore pathへ混入
```

これらはSapphireのframe contract:

```text
valid Frame
→ output synchronization
→ presenter
```

を崩す。

frontend UI overlayなら:

```text
emulator frame presentation
と
起動前UI
```

を分離できる。

これはAndroidのnative surfaceとActivity／View UIの分離に近い。

---

# 14. 代替案: presenter background-only API

Qt overlayがplatform上でnative child stacking問題を起こす場合のみ、
Sapphire presenterのbackground resourceを拡張する。

例:

```cpp
bool VulkanSurfacePresenter::presentBackgroundOnly(
    int surfaceId,
    u64 timeoutNs);
```

処理:

```text
swapchain acquire
background-only vertex作成
screen descriptorは使用しない
kDrawModeBackgroundだけrecord
submit
present
```

ただし、これはSapphire `0.7.0.rc4`に存在するAPIではない。

Sapphireのbackground resourceを利用したdesktop拡張であり、
参照実装そのものではない。

優先順位:

```text
1. Qt frontend overlay
2. native stacking不具合がある場合のみbackground-only presenter
```

---

# 15. 推奨class構造

```cpp
class MelonPrimeVulkanFrontendSession
{
public:
    bool beginProducerFrame(...);
    bool completeProducerFrame(...);

    Frame* acquirePresentFrame();
    bool presentAcquiredFrame(...);

private:
    FrameQueue frameQueue;
    VulkanOutput output;

    SoftPackedFrameSnapshot lastSoftPackedFrameSnapshot;
    SoftPackedFrameSnapshot previousSoftPackedFrameSnapshot;

    // Phase 1のみ。Phase 2で廃止予定。
    MelonPrimeStructuredSnapshot structuredSnapshot;

    std::unordered_map<
        Frame*,
        VulkanCompositionInputs> frameInputs;

    Frame* activeRenderFrame = nullptr;
};
```

重要:

```text
snapshotはsession member
FrameはFrameQueue owner
GUIはFrame*とinputsだけを参照
GPU live stateをGUI threadから読まない
```

---

# 16. clear／generation処理

Sapphire型のpersistent snapshotへ変更する場合、
backend switchとgeneration変更時に明示的に無効化する。

```cpp
void clearProducerState()
{
    structuredSnapshot = {};
    lastSoftPackedFrameSnapshot.clear();
    previousSoftPackedFrameSnapshot.clear();
    activeRenderFrame = nullptr;
}
```

呼出箇所:

```text
shutdown()
beginBackendSwitch()
beginGeneration()
beginSurfaceGeneration()で必要なsurface依存state
renderer fallback
device lost
ROM unload
```

`beginGeneration()`:

```cpp
activeGeneration = newGeneration;
lastSubmittedSerial = 0;
clearProducerState();
frameInputs.clear();
output.invalidateTemporalHistory();
frameQueue.requestPresentationResync();
```

---

# 17. `lastCompleteSnapshot` full copyの廃止

現行sessionは:

```cpp
MelonPrimeStructuredSnapshot lastCompleteSnapshot;
```

を持ち、submit成功時にfull copyしている。

ready判定だけならSapphire型へ寄せてmetadata化する。

```cpp
bool hasCompleteStructuredSnapshot = false;
u64 lastCompleteFrameSerial = 0;
u64 lastCompleteGeneration = 0;
```

submit成功時:

```cpp
hasCompleteStructuredSnapshot = true;
lastCompleteFrameSerial =
    lastSoftPackedFrameSnapshot.sourceFrameSerial;
lastCompleteGeneration =
    lastSoftPackedFrameSnapshot.rendererGeneration;
```

full snapshot履歴が必要なdebug機能は、
Sapphireと同じように明示的なdebug snapshotへcopyする。

通常runtime pathでは毎frame巨大copyをしない。

---

# 18. 実装順序

## S1: stack overflow除去

対象:

```text
MelonPrimeVulkanFrontendSession.h
MelonPrimeVulkanFrontendSession.cpp
EmuInstance.cpp
```

内容:

```text
MelonPrimeStructuredSnapshotをmember化
SoftPackedFrameSnapshotをlast／previous member化
stack localを削除
```

## S2: Sapphire型snapshot latch

内容:

```text
captureCompletedSnapshot
＋
buildSoftPackedSnapshot

を

latchSoftPackedFrameSnapshot

へ統合
```

## S3: producer transaction統合

内容:

```text
FrameQueueからFrame取得
RunFrame前resource準備
RunFrame後snapshot latch
prepare
queue push
```

## S4: presenter責務整理

内容:

```text
producerからcomposeAndSubmitFrameを外す
GUI presenter側へ寄せる
Frame inputsをFrame単位で保持
```

## S5: splash overlay

内容:

```text
NoRomSplashOverlay QWidget
既存splashLogo／localized splashText再利用
first present成功時hide
emu stop時show
```

## S6: runtime fallback

内容:

```text
prepare／submit／present failure counter
presentation resync
Software fallback
```

---

# 19. 推奨commit分割

```text
Fix Vulkan frontend stack overflow with persistent frame snapshots
```

```text
Align Vulkan producer flow with Sapphire frame latching
```

```text
Move Vulkan composition ownership toward the Sapphire presenter flow
```

```text
Show the no-ROM splash as a frontend overlay above the Vulkan surface
```

```text
Add staged Vulkan runtime failure recovery
```

共通core修正と混ぜない。

---

# 20. ガード方針

今回の変更はMelonPrime Vulkan専用。

```cpp
#if defined(MELONPRIME_DS) \
    && defined(MELONPRIME_ENABLE_VULKAN)
```

対象:

```text
MelonPrimeVulkanFrontendSession.h/.cpp
MelonPrimeScreenVulkan.h/.cpp
EmuInstance.cppのVulkan frontend部分
EmuThread.cppのVulkan producer scheduling
```

`Screen.h`／`Screen.cpp`へsplash描画helperを追加する場合:

```text
upstream共通helper
```

としてNative側も利用するか、

```cpp
#if defined(MELONPRIME_DS) \
    && defined(MELONPRIME_ENABLE_VULKAN)
```

へ完全隔離する。

---

# 21. テスト

## Software

| 操作 | 期待結果 |
|---|---|
| 起動直後 | melonDS splash表示 |
| ROM起動 | 正常 |
| ROM停止 | splash再表示 |
| Software再選択 | 正常 |

## Vulkan

| 操作 | 期待結果 |
|---|---|
| Vulkan選択、ROMなし | splash表示 |
| Vulkan初期化中 | splashを維持 |
| first RunFrame | 完走 |
| snapshot latch | 成功 |
| prepareFrameForPresentation | 成功 |
| FrameQueue push | 成功 |
| first present | 成功 |
| first present後 | splash非表示 |
| ROM停止 | splash再表示 |
| resize | splash中央配置 |
| 言語変更 | localized splash更新 |
| Vulkan→Software→Vulkan | generation不整合なし |

## stress

```text
10分連続動作
fast-forward
pause／resume
ROM再読込
renderer連続切替
window resize連打
fullscreen切替
複数EmuInstance
device lost injection
```

---

# 22. 最終判断

Vulkan ROM起動クラッシュの最有力原因は、
Sapphireではinstance memberとして保持されている巨大frame snapshotを、
MelonPrimeの追加session adapterがネストしたstack localとして確保していること。

修正は単なるstack size増加ではなく:

```text
Sapphire型のpersistent last／previous snapshot
＋
FrameQueueと一体化したproducer transaction
```

へ戻すべき。

スプラッシュ欠落はVulkan rendering failureではない。

```text
ROMなし
→ producer Frameなし
→ presenterはpresentできない
→ Vulkan panel自身にfrontend splash描画がない
```

というfrontend責務の欠落。

Sapphireのframe contractを崩さず、
Qt frontend overlayとして実装するのが最も参照実装に近い。
