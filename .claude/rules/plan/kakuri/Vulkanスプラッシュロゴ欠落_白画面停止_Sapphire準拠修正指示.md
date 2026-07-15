# Vulkanスプラッシュロゴ欠落・ROM起動後白画面停止
## Sapphire `melonDS-android` 0.7.0.rc4 準拠 修正指示書

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査時HEAD:** `617bceb7f3210b201b0b2fb752c9560bd618b47b`  
**確認build:** `melonPrimeDS v3.4.3 v52.exe`  
**参照フロントエンド:** `SapphireRhodonite/melonDS-android`  
**参照タグ:** `0.7.0.rc4`  
**参照URL:** `https://github.com/SapphireRhodonite/melonDS-android/releases/tag/0.7.0.rc4`

---

# 0. 実装者への必須指示

この修正では、次の二つを別問題として扱うこと。

```text
A. Vulkan選択時のno-ROMスプラッシュでmelonDSロゴだけ表示されない
B. VulkanでROMを起動すると白画面になり、frame producerが停止状態になる
```

優先順位:

```text
V1. splashLogoの初期化条件を修正
V2. Vulkan presenter登録のlifecycleを修正
V3. FrameQueue drainを診断可能にする
V4. actual renderer判定を「first present成功」まで遅延
V5. Sapphire型producer／presenter責務へ段階的に寄せる
V6. ARM9 abortを再試験
```

禁止事項:

```text
ScreenPanelVulkanをScreenPanel(parent, true)へ変更しない
QThread stack size変更で回避しない
FrameQueueサイズを増やして隠さない
submitCompletedFrame()失敗を無視してVulkan ready扱いしない
ROM未起動用dummy NDS frameを作らない
CPU readbackを追加しない
Software 2D framebufferをpresenterへ再アップロードする互換経路を追加しない
```

今回の変更は原則として:

```cpp
#if defined(MELONPRIME_DS) \
    && defined(MELONPRIME_ENABLE_VULKAN)
```

の範囲へ隔離すること。

---

# 1. 現在の症状

## 1.1 スプラッシュ

Vulkan panelへ切り替えると、次は表示される。

```text
黒背景
「ROMを開く」案内
melonDS URL
```

しかし、中央のmelonDSロゴだけ表示されない。

## 1.2 ROM起動

VulkanでROMを開くと、最初はVulkan producer処理が成功する。

```text
capture snapshot result=1
submitCompletedFrame result=1
actual renderer changed: Vulkan
```

その後、`submitCompletedFrame result=0`が永久に続き、画面は白いまま更新されない。

さらに後段で:

```text
ARM9: data abort
ARM9: prefetch abort
```

が発生する。

---

# 2. ログ監査結果

提供ログ全体を集計した結果:

| 項目 | 回数 |
|---|---:|
| `submitCompletedFrame result=1` | 9 |
| `submitCompletedFrame result=0` | 271 |
| `ARM9: data abort` | 4 |
| `ARM9: prefetch abort` | 1 |

重要なのは、成功回数が**正確に9回**であること。

Sapphire `0.7.0.rc4`の`FrameQueue`は:

```cpp
constexpr std::size_t FRAME_QUEUE_SIZE = 9;
```

であり、9個の固定frame slotを持つ。

したがって今回の挙動は:

```text
9個のFrameをproducerが作成
↓
presenterが1個も正常にcommit／recycleしない
↓
全slotが使用不能
↓
10個目からgetRenderFrame()が失敗
↓
submitCompletedFrame()が永久にfalse
```

と一致する。

これは「Vulkan shaderが白色を出力し続けている」ことを第一原因とするログではない。

第一原因は:

```text
FrameQueueのconsumer／presenter側がdrainしていない
```

ことである。

---

# 3. 正常完了している処理

次はログ上、正常完了している。

```text
VulkanContext作成
present queue選択
desktop VkSurface attach
Vulkan Renderer3D作成
VulkanOutput初期化
compositor pipeline cache初期化
最初のNDS::RunFrame()
structured snapshot取得
SoftPacked snapshot生成
Vulkan frame resource準備
最初のcompose／submit
actual rendererのVulkan認定
EmuInstance::drawScreen()
```

特に:

```text
[VulkanSubmitTrace] capture snapshot result=1
[VulkanSubmitTrace] submitCompletedFrame result=1
```

が9回続く。

したがって、前回の巨大stack object問題は修正されている。

現在の主問題は、その次の:

```text
GUI presenter
→ acquirePresentFrame()
→ presentAcquiredFrame()
→ commitPresentedFrame()
```

がFrameQueueを解放できていないこと。

---

# 4. スプラッシュロゴ欠落の原因

## 4.1 Vulkan panelのconstructor

現在:

```cpp
ScreenPanelVulkan::ScreenPanelVulkan(QWidget* parent)
    : ScreenPanel(parent, false)
{
    ...
}
```

第二引数`false`により:

```text
cpuOverlayStorageEnabled == false
```

となる。

## 4.2 base constructor

`ScreenPanel`は現在、ロゴを次の条件でのみloadする。

```cpp
if (cpuOverlayStorageEnabled)
    splashLogo = QPixmap(":/melon-logo");
```

そのためVulkan panelでは:

```text
splashLogo.isNull() == true
```

になる。

## 4.3 Vulkan splash overlay

`NoRomSplashOverlay::paintEvent()`は:

```cpp
painter.drawPixmap(
    QRect(screen.splashPos[3], QSize(192, 192)),
    screen.splashLogo);
```

を実行している。

しかし`splashLogo`がnullなので、何も描画されない。

案内テキスト用bitmapは`osdUpdate()`で生成されるため、テキストだけ表示される。

---

# 5. スプラッシュロゴ修正

## 5.1 推奨する最小patch

`ScreenPanelVulkan` constructorで、Vulkan splash専用にlogo resourceをloadする。

```cpp
ScreenPanelVulkan::ScreenPanelVulkan(QWidget* parent)
    : ScreenPanel(parent, false)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_KeyCompression, false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(screenGetMinSize(1));

    if (splashLogo.isNull()
        && !splashLogo.load(QStringLiteral(":/melon-logo")))
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan no-ROM splash logo resource failed to load\n");
    }

    noRomSplashOverlay = new NoRomSplashOverlay(*this);
    noRomSplashOverlay->hide();
}
```

この修正は:

```text
CPU gameplay overlay storage
```

を有効化しない。

loadするのは小さな静的UI resourceだけ。

## 5.2 避ける修正

次は行わない。

```cpp
ScreenPanelVulkan(QWidget* parent)
    : ScreenPanel(parent, true)
```

理由:

```text
cpuOverlayStorageEnabledを再有効化する
Vulkan presenterで不要なCPU QImage保存経路が復活する
Sapphire型のnative GPU presentationから離れる
```

## 5.3 共通helper化する場合

長期的には`ScreenPanel`へ:

```cpp
bool ensureNoRomSplashLogoLoaded();
```

を追加してもよい。

ただし、このhelperは:

```text
static UI assetのload
```

だけを担当し、CPU gameplay overlay allocationとは分離すること。

---

# 6. 白画面停止の直接構造

## 6.1 producer側

現在の`submitCompletedFrame()`は:

```cpp
Frame* frame = frameQueue.getRenderFrame(policy);
if (frame == nullptr)
    return false;
```

となる。

その後、成功時には:

```cpp
frameQueue.pushRenderedFrame(frame, policy);
```

を実行する。

ログでは9回成功後、永久にfalse。

したがって:

```text
freeなFrame slotが戻っていない
```

と判定できる。

## 6.2 GUI側

`ScreenPanelVulkan::presentOnGuiThread()`:

```cpp
if (!session.updatePresenterOverlay(
        *presenter, surfaceId, buildOverlayOnGuiThread()))
{
    return;
}

Frame* frame = session.acquirePresentFrame();
if (frame == nullptr)
    return;
```

overlay更新が失敗した場合、FrameQueue取得前にreturnする。

つまり:

```text
updatePresenterOverlay()が1回でも恒常的にfalse
↓
acquirePresentFrame()へ到達しない
↓
commitPresentedFrame()も呼ばれない
↓
FrameQueueが埋まる
```

## 6.3 Session側

```cpp
bool MelonPrimeVulkanFrontendSession::updatePresenterOverlay(...)
{
    std::scoped_lock lock(stateMutex);

    if (!initialized
        || producerSuspended
        || activePresenter != &presenter)
    {
        return false;
    }

    return presenter.updateOverlay(surfaceId, overlay);
}
```

producer側のsubmitが成功しているため:

```text
initialized == true
producerSuspended == false
```

である。

残る有力条件は:

```text
activePresenter != &presenter
```

または:

```text
presenter.updateOverlay()内部失敗
```

である。

コード監査では、`activePresenter`を失うlifecycle不具合が存在する。

---

# 7. presenter登録lifecycleの欠陥

現在:

```cpp
void MelonPrimeVulkanFrontendSession::beginBackendSwitch()
{
    std::scoped_lock lock(stateMutex);
    producerSuspended = true;
    stagedPresenter = nullptr;
}
```

```cpp
void MelonPrimeVulkanFrontendSession::completeBackendSwitch(
    bool vulkanPresentationActive)
{
    std::scoped_lock lock(stateMutex);
    activePresenter =
        vulkanPresentationActive ? stagedPresenter : nullptr;

    stagedPresenter = nullptr;
    producerSuspended = false;
    frameQueue.requestPresentationResync();
}
```

この契約では:

```text
既存のactivePresenterが存在
↓
beginBackendSwitch()でstagedPresenterをnullにする
↓
switch中にScreenPanelVulkanが再生成されない
↓
registerPresenter()が再実行されない
↓
completeBackendSwitch(true)
↓
activePresenter = nullptr
```

となる。

特に、ROM未起動状態でVulkan panelを先に作り、その後ROM起動時にRenderer3Dだけ作成するケースでは:

```text
panel／VkSurfaceは既存
Renderer3Dだけ初期化
```

なので、panelの再登録は発生しない。

Sapphireではpresenter objectはsurface lifetimeへ結び付いており、renderer frame transactionごとにpointerを破棄しない。

renderer変更時は:

```text
FrameQueue presentation resync
temporal history invalidation
descriptor cache invalidation
```

を行う。

presenter ownership自体は保持する。

---

# 8. presenter lifecycle修正

## 8.1 必須patch

`beginBackendSwitch()`で既存presenterをstageへ引き継ぐ。

```cpp
void MelonPrimeVulkanFrontendSession::beginBackendSwitch()
{
    std::scoped_lock lock(stateMutex);

    if (!producerSuspended)
        stagedPresenter = activePresenter;

    producerSuspended = true;

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanPresenterTrace] begin switch active=%p staged=%p\n",
        static_cast<void*>(activePresenter),
        static_cast<void*>(stagedPresenter));
}
```

`completeBackendSwitch()`では、Vulkan継続時に既存pointerを不用意にnullへしない。

```cpp
void MelonPrimeVulkanFrontendSession::completeBackendSwitch(
    bool vulkanPresentationActive)
{
    std::scoped_lock lock(stateMutex);

    if (vulkanPresentationActive)
    {
        if (stagedPresenter != nullptr)
            activePresenter = stagedPresenter;

        // stagedPresenterがnullでも、既存activePresenterを破棄しない。
        // panelを実際に破棄した場合はunregisterPresenter()が両方をclearする。
    }
    else
    {
        activePresenter = nullptr;
    }

    stagedPresenter = nullptr;
    producerSuspended = false;

    frameQueue.requestPresentationResync();
    output.invalidateTemporalHistory();

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanPresenterTrace] complete switch vulkan=%d active=%p\n",
        vulkanPresentationActive ? 1 : 0,
        static_cast<void*>(activePresenter));
}
```

## 8.2 register／unregisterの契約

`registerPresenter()`:

```cpp
void MelonPrimeVulkanFrontendSession::registerPresenter(
    VulkanSurfacePresenter* presenter)
{
    std::scoped_lock lock(stateMutex);

    if (producerSuspended)
        stagedPresenter = presenter;
    else
        activePresenter = presenter;

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanPresenterTrace] register presenter=%p suspended=%d active=%p staged=%p\n",
        static_cast<void*>(presenter),
        producerSuspended ? 1 : 0,
        static_cast<void*>(activePresenter),
        static_cast<void*>(stagedPresenter));
}
```

`unregisterPresenter()`は必ず両方をclearする。

```cpp
if (stagedPresenter == presenter)
    stagedPresenter = nullptr;

if (activePresenter == presenter)
{
    ...
    activePresenter = nullptr;
}
```

これは現在の方向を維持する。

## 8.3 長期的なSapphire準拠

よりSapphireへ近づける場合、backend switchとpresenter ownershipを分離する。

推奨命名:

```cpp
suspendProducerForBackendSwitch();
resumeProducerAfterBackendSwitch(bool vulkanActive);
registerPresenter();
unregisterPresenter();
```

原則:

```text
presenter pointerはScreenPanelVulkan／VkSurface lifetimeが所有
renderer transactionはpointerを交換しない
renderer transactionはproducer停止とpresentation resyncだけを行う
```

---

# 9. なぜ9回だけ成功するか

Sapphireの固定pool:

```cpp
std::array<Frame, FRAME_QUEUE_SIZE> frames{};
```

```cpp
constexpr std::size_t FRAME_QUEUE_SIZE = 9;
```

今回のログ:

```text
成功9回
失敗271回
```

presenterがdrainしない場合、固定slotを使い切る。

現在の`submitCompletedFrame()`は失敗理由を一つのboolへ潰しているため、次のどこでfalseになったか分からない。

```text
snapshot不正
frameView mismatch
getRenderFrame() == nullptr
ensureFrameResources失敗
prepareFrameForPresentation失敗
buildCompositionInputs失敗
composeAndSubmitFrame失敗
```

今回の成功回数から最有力は:

```text
getRenderFrame() == nullptr
```

である。

---

# 10. submit結果をstage化する

## 10.1 boolだけを返さない

追加:

```cpp
enum class VulkanProducerResult
{
    Submitted,
    NotInitialized,
    Suspended,
    InvalidSnapshot,
    FrameViewMismatch,
    NoRenderFrame,
    EnsureFrameResourcesFailed,
    PrepareFrameFailed,
    BuildInputsFailed,
    ComposeSubmitFailed,
};
```

```cpp
struct VulkanProducerOutcome
{
    VulkanProducerResult result =
        VulkanProducerResult::NotInitialized;

    u64 frameSerial = 0;
    u64 rendererGeneration = 0;
    u64 frameId = 0;
};
```

`submitCompletedFrame()`は`VulkanProducerOutcome`を返す。

## 10.2 最低限の段階ログ

```cpp
[VulkanProducer] snapshot serial=... generation=...
[VulkanProducer] frameView serial=... generation=... valid=...
[VulkanProducer] getRenderFrame frame=... backlog=...
[VulkanProducer] ensureFrameResources result=...
[VulkanProducer] prepareFrameForPresentation result=...
[VulkanProducer] buildCompositionInputs result=...
[VulkanProducer] composeAndSubmitFrame result=...
[VulkanProducer] pushRenderedFrame id=...
```

`NoRenderFrame`時:

```cpp
const FrameQueueStats stats =
    frameQueue.takeStatsSnapshotAndReset();

Platform::Log(
    Platform::LogLevel::Warn,
    "[VulkanProducer] no render frame backlog=%llu max=%llu "
    "queued=%llu presented=%llu discarded=%llu stateFailures=%llu\n",
    ...);
```

---

# 11. presenter側ログ

`presentOnGuiThread()`へ次を追加する。

```cpp
Platform::Log(
    Platform::LogLevel::Info,
    "[VulkanPresenterTrace] tick panel=%p presenter=%p surface=%d visible=%d\n",
    static_cast<void*>(this),
    static_cast<void*>(presenter.get()),
    surfaceId,
    isVisible() ? 1 : 0);
```

overlay:

```cpp
const bool overlayUpdated =
    session.updatePresenterOverlay(
        *presenter,
        surfaceId,
        buildOverlayOnGuiThread());

Platform::Log(
    Platform::LogLevel::Info,
    "[VulkanPresenterTrace] overlay result=%d\n",
    overlayUpdated ? 1 : 0);

if (!overlayUpdated)
    return;
```

frame取得:

```cpp
Frame* frame = session.acquirePresentFrame();

Platform::Log(
    Platform::LogLevel::Info,
    "[VulkanPresenterTrace] acquire frame=%p id=%llu serial=%llu "
    "rendererGen=%llu surfaceGen=%llu expectedSurface=%llu\n",
    static_cast<void*>(frame),
    frame ? static_cast<unsigned long long>(frame->frameId) : 0ull,
    frame ? static_cast<unsigned long long>(frame->frameSerial) : 0ull,
    frame ? static_cast<unsigned long long>(frame->rendererGeneration) : 0ull,
    frame ? static_cast<unsigned long long>(frame->surfaceGeneration) : 0ull,
    static_cast<unsigned long long>(surfaceHost.generation()));
```

present:

```cpp
const bool presented =
    session.presentAcquiredFrame(
        frame,
        *presenter,
        kPresentTimeoutNs);

Platform::Log(
    Platform::LogLevel::Info,
    "[VulkanPresenterTrace] present id=%llu result=%d\n",
    static_cast<unsigned long long>(frame->frameId),
    presented ? 1 : 0);
```

commit／defer:

```cpp
if (presented)
{
    session.commitPresentedFrame(frame);
    Platform::Log(..., "[VulkanPresenterTrace] commit id=...\n");
}
else
{
    session.deferPresentedFrame(frame);
    Platform::Log(..., "[VulkanPresenterTrace] defer id=...\n");
}
```

release buildでは毎frame出さず、最初の120frameまたは失敗時だけ出す。

---

# 12. actual renderer判定の修正

現在:

```cpp
bool hasCompositedFrame() const
{
    return initialized
        && !producerSuspended
        && lastSubmittedSerial != 0;
}
```

これは:

```text
producerがframeをcomposeした
```

ことしか示さない。

今回のログでは、GUI present成功が確認されていないのに:

```text
actual renderer changed: Vulkan
```

となっている。

Sapphireでは:

```text
Frameを取得
presentFrame()
成功時のみcommitPresentedFrame()
```

という契約を持つ。

MelonPrimeもVulkanのruntime actual認定には:

```text
Renderer3D ready
structured snapshot ready
compositor submission ready
presenter registered
first swapchain present成功
```

を要求する。

## 12.1 Session member追加

```cpp
u64 lastPresentedSerial = 0;
u64 lastPresentedFrameId = 0;
```

## 12.2 commit時更新

```cpp
void MelonPrimeVulkanFrontendSession::commitPresentedFrame(
    Frame* frame)
{
    std::scoped_lock lock(stateMutex);

    if (frame == nullptr)
        return;

    lastPresentedSerial = frame->frameSerial;
    lastPresentedFrameId = frame->frameId;

    frameQueue.commitPresentedFrame(
        frame,
        queuePolicy());
}
```

## 12.3 query

```cpp
bool hasPresentedFrame() const
{
    std::scoped_lock lock(stateMutex);

    return initialized
        && !producerSuspended
        && activePresenter != nullptr
        && lastPresentedSerial != 0;
}
```

## 12.4 generation変更時

```cpp
lastPresentedSerial = 0;
lastPresentedFrameId = 0;
```

へ戻す。

## 12.5 EvaluateActualRenderer

Vulkan actual条件へ:

```cpp
session.hasPresentedFrame()
```

を追加する。

これにより:

```text
FrameQueueへ入っただけ
```

ではVulkan readyにならない。

---

# 13. splashをhideする条件

現在panel側は:

```cpp
bool showSplash =
    lastPresentedFrameId == 0;

if (emuThread)
    showSplash =
        showSplash || !emuThread->emuIsActive();
```

方向は正しい。

ただし、session側とpanel側に別々のpresented stateを持つと不整合になりやすい。

推奨:

```cpp
const bool hasPresentedGameFrame =
    emuInstance->vulkanFrontendSession()
        .hasPresentedFrame();

const bool showSplash =
    !emuThread
    || !emuThread->emuIsActive()
    || !hasPresentedGameFrame;
```

panel memberの`lastPresentedFrameId`はdebug表示用に限定する。

これにより:

```text
emuActiveになった
しかしpresenterが壊れている
```

場合にも、splashを残せる。

---

# 14. Sapphireとのproducer／presenter比較

## Sapphire producer

`MelonInstance::runFrame()`:

```text
FrameQueueからrender Frame取得
reuse可能性確認
resource準備
NDS::RunFrame()
SoftPacked snapshot latch
prepareFrameForPresentation()
成功frameをFrameQueueへpush
```

Sapphireはproducer failure時に:

```text
presentation resync
failure count
runtime failure通知
```

を行う。

## Sapphire presenter

`presentVulkanFrame()`:

```text
FrameQueueからpresent candidate取得
frame ready確認
buildCompositionInputs()
VulkanSurfacePresenter::presentFrame()
成功ならcommitPresentedFrame()
失敗ならdeferPresentedFrame()
```

## 現在のMelonPrime

producer側の`submitCompletedFrame()`で:

```cpp
output.buildCompositionInputs(...)
output.composeAndSubmitFrame(...)
frameQueue.pushRenderedFrame(...)
```

まで行う。

GUI presenter側は既にcomposed済みframeをsurfaceへpresentする。

この方式でも動作可能だが、Sapphireよりproducer責務が大きい。

---

# 15. Sapphireへ近づける段階的修正

## Phase S1: presenter pointer修正

最優先。

```text
既存activePresenterをswitch中も保持
GUI queue drainを復旧
9frameで停止しないことを確認
```

この段階ではcompose ownershipを変更しない。

## Phase S2: queue transactionを診断可能にする

```text
VulkanProducerOutcome
FrameQueue stats
presenter stage logs
first-present readiness
```

を追加する。

## Phase S3: producerをSapphire型へ寄せる

producer:

```text
ensureFrameResources
prepareFrameForPresentation
FrameQueue push
```

までに縮小する。

`buildCompositionInputs()`と最終surface composeはGUI presentation側へ移す。

## Phase S4: presenter側

```text
getPresentCandidate
frame readiness確認
buildCompositionInputs
presentFrame
commit／defer
```

へ寄せる。

ただし、desktop VulkanではGUI threadへlive GPU stateを渡さない契約を維持する。

必要なinputsは:

```text
Frame単位のimmutable metadata
```

としてproducerからqueueへ付随させる。

## Phase S5: failure recovery

Sapphire同様:

```text
連続prepare失敗
連続present失敗
FrameQueue starvation
surface generation mismatch
```

を数える。

例:

```cpp
u32 consecutiveProducerFailures = 0;
u32 consecutivePresenterFailures = 0;
u32 consecutiveNoRenderFrameFailures = 0;
```

4回連続で:

```text
presentation resync
temporal history invalidation
descriptor cache invalidation
```

それでも回復しなければ:

```text
Software fallback
OSD通知
failed_stage記録
```

を行う。

---

# 16. FrameQueue policy

現在のdesktop session:

```cpp
policy.MaxBacklogDepth = 2;
policy.AllowStealPending = true;
policy.AllowPreviousFrameReuse = true;
policy.PreferOldestFrame = false;
```

Sapphire realtime Vulkan:

```cpp
policy.MaxBacklogDepth =
    renderScale > 1 ? 2 : 1;

policy.AllowStealPending = false;
policy.AllowPreviousFrameReuse = true;
policy.AllowDropForDeadline = false;
policy.PreferOldestFrame = false;
policy.PreserveBacklogOnPresent = false;
```

presenter復旧後、MelonPrimeも:

```text
1x: backlog 1
2x以上: backlog 2
AllowStealPending=false
```

へ寄せる。

ただし、今回の白画面修正より先にqueue policyだけを変更しない。

consumerが壊れたままpolicyを変更しても解決しない。

---

# 17. ARM9 abortの扱い

ログ後半:

```text
ARM9: data abort (02007008)
ARM9: data abort (0200710C)
ARM9: data abort (02007114)
ARM9: data abort (02007118)
ARM9: prefetch abort (9A3B0204)
```

これはguest CPU側の異常。

ただし発生前に:

```text
submitCompletedFrame result=0
```

が連続している。

現時点では:

```text
FrameQueue starvationの直接結果
```

と断定しない。

GPU presentationがguest ARM9 memoryを直接変更する設計ではないため、
別不具合の可能性もある。

手順:

```text
1. presenter queue drainを修正
2. Vulkan画面更新を復旧
3. 同一ROM／同一saveで再試験
4. abortが残る場合だけCPU／JIT／MelonPrime ROM patchを別調査
```

abortが再現する場合、次をSoftwareとVulkanで比較する。

```text
PC
LR
CPSR
fault address
fault status
ARM9 code page CRC
JIT on／off
MelonPrime patch適用前後
```

表示不具合とCPU abortを一つのpatchへ混ぜない。

---

# 18. 修正対象ファイル

## ロゴ

```text
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
```

必要に応じて:

```text
src/frontend/qt_sdl/Screen.h
src/frontend/qt_sdl/Screen.cpp
```

ただし共通file変更はlogo asset helperだけに限定。

## presenter lifecycle

```text
src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.h
src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp
```

## actual renderer readiness

```text
src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp
src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.h
src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp
src/frontend/qt_sdl/EmuThread.cpp
```

## presenter diagnostics

```text
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/VulkanReference/FrameQueue.h
src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp
```

---

# 19. commit分割

## Commit 1

```text
Fix Vulkan no-ROM splash logo loading
```

内容:

```text
Vulkan panelで:/melon-logoを明示load
CPU overlay storageはfalseのまま
logo load failure log追加
```

## Commit 2

```text
Preserve the Vulkan presenter across backend transactions
```

内容:

```text
activePresenterをstagedPresenterへ引継ぎ
complete時のnull上書きを防止
register／unregister transition log
presentation resync
```

## Commit 3

```text
Require a successful Vulkan present before marking the backend actual
```

内容:

```text
lastPresentedSerial
hasPresentedFrame()
EvaluateActualRenderer readiness追加
splash hide条件をsession present stateへ統一
```

## Commit 4

```text
Add Sapphire-style Vulkan producer and presenter diagnostics
```

内容:

```text
producer failure stage
FrameQueue stats
acquire／present／commit／defer log
```

## Commit 5

```text
Align the desktop Vulkan frame transaction with Sapphire
```

内容:

```text
producer／presenter責務整理
realtime queue policy
runtime failure recovery
```

---

# 20. テスト手順

## T1: splash

```text
アプリ起動
映像設定でVulkanを選択
ROMは開かない
```

期待:

```text
黒背景
中央melonDSロゴ
ローカライズ済み案内文
URL
```

確認:

```text
splashLogo.isNull() == false
logo logical size
devicePixelRatio
resize後の中央配置
```

## T2: first present

ROM起動。

期待ログ:

```text
register presenter=0x...
complete switch vulkan=1 active=0x...
producer push frame id=1
presenter acquire frame id=1
present result=1
commit frame id=1
actual renderer changed: Vulkan
```

禁止されるログ:

```text
active=0x0
9回成功後にNoRenderFrame永久ループ
```

## T3: steady state

300frame以上動作。

期待:

```text
submit成功が継続
present成功が継続
CurrentBacklogDepth <= 2
free frameが循環
NoRenderFrame連続なし
```

## T4: backend switch

```text
Software
→ Vulkan
→ Software
→ Vulkan
```

各10回。

期待:

```text
presenter pointerがstaleにならない
surface generation mismatchをdrop／resync
splash logoが維持
ROM再起動可能
```

## T5: ROM stop／restart

```text
VulkanでROM起動
停止
別ROM起動
同一ROM再起動
```

期待:

```text
停止時splash再表示
logo表示
generation reset
first present後splash hide
```

## T6: stress

```text
window resize連打
fullscreen切替
pause／resume
fast-forward
1x／2x／4x内部解像度
VSync on／off
```

期待:

```text
FrameQueue starvationなし
white native surface固定なし
presenter unregister漏れなし
```

## T7: ARM9 abort

presenter修正後、同一Korean ROM／saveで再実行。

期待:

```text
data abortなし
prefetch abortなし
ゲーム画面進行
```

残る場合は別issue化する。

---

# 21. 完了条件

次をすべて満たした時点で完了。

```text
Vulkan no-ROM splashにmelonDSロゴが表示される
CPU overlay storageはfalseのまま
最初のVulkan frameがpresent／commitされる
actual Vulkan認定はfirst present成功後
9frame後にsubmit失敗へ固定されない
FrameQueue backlogが定常的に回収される
ROM画面が継続更新される
Software／Vulkan切替後もpresenter pointerが有効
ROM停止後にsplashがロゴ込みで再表示される
ARM9 abortが再現しない、または別原因として切り離される
```

---

# 22. 最終判断

## ロゴ欠落

原因確定。

```text
ScreenPanelVulkan(parent, false)
↓
cpuOverlayStorageEnabled=false
↓
base constructorがsplashLogoをloadしない
↓
NoRomSplashOverlayがnull QPixmapを描画
```

修正:

```text
Vulkan panelでstatic logo resourceだけを明示load
```

## 白画面停止

ログ上、producerは最初の9frameを作成できている。

成功数9はSapphireの`FRAME_QUEUE_SIZE=9`と一致する。

したがって:

```text
presenterがFrameQueueをdrain／commitしていない
```

ことが主原因。

コード上の最優先修正点は:

```text
backend switch完了時にactivePresenterをnullへ失うlifecycle
```

である。

Sapphireと同様に:

```text
presenter ownershipはsurface lifetimeへ保持
renderer transactionではpresentation resyncだけを行う
producerはFrameをqueueへ投入
presenterは取得・present・commitする
```

構造へ寄せること。
