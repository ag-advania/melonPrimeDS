# Vulkan S70 監査・修正指示書
## 未ビルドHEAD／CustomHUD Staged-slot枯渇／Fullscreen QueueWaitIdle／初回ROM白画面／2D／4x文字／Sapphire準拠

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `659d1e6453126c5d96dcc2d781f5dddcba5de756`  
**前回監査HEAD:** `ba9c4f01dde5f8b916b04b9f3465d77771451e0a`  
**差分:** 8 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 結論

S69で行った次の修正方針は正しい。

```text
- physical top／bottom publicationの二重swap削除
- packed／structuredを同じphysical screen viewから公開
- HUD upload token導入
- fullscreen reverse toggleの再開処理
- same-extent resizeの無処理化
- present pathから毎frame native identity再検査を削除
- surface format単位のpipeline cache導入
```

しかし、現在のHEADには以下のP0不具合が残っている。

```text
P0-1:
VulkanSurfacePresenter.hと.cppで
ensureSurfaceFormatRenderResources()の引数型が不一致。
Vulkan有効buildはコンパイル不能。

P0-2:
CustomHUDはpresent frame取得前にstaging slotを確保する。
frameが無いtickでStaged slotが孤児化し、
3回で全slotが枯渇する。

P0-3:
vkQueueSubmit成功後にvkQueuePresentKHRが失敗すると、
HUDへ「submit失敗」と通知する。
実際にはtransfer commandが実行中なのにslotをFreeへ戻す。

P0-4:
HUD texture resize時に同じdescriptor setを即更新する。
旧command bufferがそのdescriptor setを使用中でも更新可能。

P0-5:
非timeline環境のHUD texture retirement serialが
「最後に完了したserial」であり
「旧textureを最後に使用したserial」ではない。

P0-6:
fullscreen recoveryでGUI threadから
vkQueueWaitIdle(presentQueue)を呼ぶ。
待機上限がなく、黒画面中の再切替でUIが停止する。

P0-7:
surface-format cacheのVkRenderPass／VkPipelineを
destroyCommonResources()で破棄していない。

P1:
4x時もUIのScreenFiltering=trueなら
game screen全体へlinear samplerを使用するため、
DS 2D文字が整数倍率でもぼける／潰れる。

P1:
S69 testはsource文字列検査であり、
C++ compile、runtime frame ownership、
submit／present failureを検査していない。
```

ユーザーが報告した症状がS69修正前と変わらない最大の理由は、
現在のHEADがVulkan有効構成ではコンパイル不能であり、
実行しているバイナリが旧buildである可能性が高いことである。

まずcompile blockerを直し、
必ずHEAD SHAを実行時ログへ出した新規buildで再検証すること。

---

# 1. 最新pushの反映確認

最新HEAD:

```text
659d1e6453126c5d96dcc2d781f5dddcba5de756
S69-12: Record S69-12 commit hash in plan section 40.
```

S69進捗表ではS69-1～12がdoneになっている。

```text
S69-1  physical publication pointer fix
S69-2  BuildPhysicalScreenView
S69-3  physical screen contract tests
S69-4  HUD slot state machine
S69-5  exact upload token submission
S69-6  texture commit after submit
S69-7  texture retirement + notifier teardown
S69-8  fullscreen transition coalescing
S69-9  surface resize coalescing
S69-10 present identity recheck removal
S69-11 format-scoped pipeline cache
S69-12 static tests + CI
```

ただし「plan上done」と「実行可能なbuild」は別である。

現HEADには後述のC++ declaration mismatchがあるため、
少なくともVulkan有効C++ compileの完了証拠はない。

---

# 2. P0
# 現HEADはVulkan有効buildでコンパイル不能

## 2.1 header宣言

`src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h`

```cpp
bool ensureSurfaceFormatRenderResources(
    VkFormat format,
    SurfaceFormatRenderResources& resources);
```

## 2.2 cpp定義

`src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp`

```cpp
bool VulkanSurfacePresenter::
ensureSurfaceFormatRenderResources(
    VkFormat format,
    SwapchainBundle& bundle)
```

引数型が一致しない。

```text
header: SurfaceFormatRenderResources&
cpp:    SwapchainBundle&
```

call siteも:

```cpp
ensureSurfaceFormatRenderResources(
    surfaceFormat.format,
    building);
```

と`SwapchainBundle`を渡している。

したがって最小修正はheaderを次へ直すこと。

```cpp
bool ensureSurfaceFormatRenderResources(
    VkFormat format,
    SwapchainBundle& bundle);
```

ただし後述の通り、
このcache自体をvendor presenterへ埋め込む設計は
Sapphireとの差分を増やしている。

短期修正後、desktop wrapperへ移すこと。

---

# 3. CIがこの不具合を検出できない理由

現在の`Sapphire vendor parity` workflowは:

```text
Python source tests
vendor snapshot verification
hash／normalized parity verification
```

のみ実行する。

実行される主なコマンド:

```text
python3 tools/test_sapphire_vendor_parity.py
python3 tools/test_sapphire_gpu2d_lifecycle_parity.py
python3 tools/test_sapphire_gpu2d_runtime_gate_parity.py
python3 tools/test_sapphire_vulkan_lifecycle_s68_parity.py
python3 tools/test_sapphire_vulkan_lifecycle_s69_parity.py
python3 tools/vendor_sapphire.py --verify-upstream-snapshots
python3 tools/vendor_sapphire.py --verify-generated
python3 tools/check_sapphire_vendor_parity.py --verify-upstream
```

CMake configure／C++ compileは行わない。

S69 testも以下のようなsource文字列検査である。

```python
self.assertIn(
    "ensureSurfaceFormatRenderResources",
    presenter)

self.assertIn(
    "OverlayUploadState",
    overlay_h)
```

そのため:

```text
宣言と定義の型不一致
incorrect overload
missing symbol
template instantiation failure
Vulkan build gate内だけのcompile error
```

を検出できない。

---

# 4. 必須CI修正

最低限、同じworkflowへcompile-only jobを追加する。

## 4.1 Linux Vulkan compile

```yaml
vulkan-compile-linux:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y \
          cmake ninja-build \
          libsdl2-dev \
          qt6-base-dev \
          libvulkan-dev \
          glslang-tools \
          libarchive-dev \
          libzstd-dev
    - name: Configure
      run: |
        cmake -S . -B build-vulkan -G Ninja \
          -DMELONPRIME_DS=ON \
          -DMELONPRIME_ENABLE_VULKAN=ON
    - name: Build Vulkan frontend
      run: |
        cmake --build build-vulkan \
          --target melonDS
```

実際のproject option名に合わせること。

## 4.2 Windows Vulkan compile

ユーザーの主環境であるWindows buildを必須にする。

```text
MSYS2 MinGW64
Qt 6.8.x
Vulkan SDK／volk
MELONPRIME_ENABLE_VULKAN=ON
```

## 4.3 build identity

起動時に必ず:

```text
Git commit SHA
build timestamp
MELONPRIME_ENABLE_VULKAN
Sapphire frontend pin
Sapphire core pin
```

を1行で出す。

例:

```text
[BuildIdentity]
commit=659d1e645312...
vulkan=1
sapphireFrontend=0.7.0.rc4
sapphireCore=d77944275
```

これがないバイナリでruntime結果を判定しない。

---

# 5. 2D physical publicationの監査結果

## 5.1 S69修正は正しい

現在の`AssignSapphireFramebuffers()`は、
Unit A／Bの出力先を`GPU.ScreenSwap`に従って割り当てる。

完成frame slotの意味は:

```text
Framebuffer[buffer][0] = physical top
Framebuffer[buffer][1] = physical bottom
```

である。

新しい`BuildPhysicalScreenView()`は:

```cpp
view.packed =
    Framebuffer[frontBuffer]
        [top ? 0 : 1].get();

view.plane0 =
    renderer.GetStructuredVulkan2DPlane(
        top, 0);

view.plane1 =
    renderer.GetStructuredVulkan2DPlane(
        top, 1);

view.control =
    renderer.GetStructuredVulkan2DPlane(
        top, 2);
```

としている。

publicationも:

```text
topView.packed + topView.planes
bottomView.packed + bottomView.planes
```

を同じviewから設定している。

前回指摘した二重swapは修正済み。

---

## 5.2 後段の再swapは見つからない

`SapphireVulkanFrameLatch::latchSoftPackedFrameSnapshot()`は:

```cpp
const u32* topPackedRaw =
    published.top.packed;

const u32* bottomPackedRaw =
    published.bottom.packed;
```

として、そのまま受け取る。

structured planesも:

```cpp
published.top.structuredPlane0
published.top.structuredPlane1
published.top.structuredControl

published.bottom.structuredPlane0
published.bottom.structuredPlane1
published.bottom.structuredControl
```

をそのまま使用する。

この経路ではphysical top／bottomを再反転していない。

---

## 5.3 報告症状が残る場合の判断

現HEADを本当に実行しているなら、
少なくとも前回の二重swapは消えているはずである。

しかし現HEADはcompile blockerを含む。

したがって最初に疑うべきは:

```text
旧実行ファイル
incremental build残骸
Vulkan無効構成
別worktree
別branch
ビルド失敗後に以前のexeを起動
```

である。

---

## 5.4 runtime sentinelを必須にする

static source testでは不十分。

debug buildで最初の120 frameだけ:

```text
[SapphirePhysicalPublish]
buildCommit
frameSerial
frontBuffer
screenSwap
topPackedSlot=0
bottomPackedSlot=1
topPhysical=Top
bottomPhysical=Bottom
topEngine
bottomEngine
topPackedHash
bottomPackedHash
topControlHash
bottomControlHash
```

を出す。

次をassertする。

```cpp
assert(
    published.top.physicalScreen
    == SapphirePhysicalScreen::Top);

assert(
    published.bottom.physicalScreen
    == SapphirePhysicalScreen::Bottom);
```

---

# 6. P0
# CustomHUDが表示されない直接原因
# Staged upload slotがframe無しtickで枯渇する

## 6.1 現在の実行順

`ScreenPanelVulkan::presentOnGuiThread()`は:

```text
1. HUDをQImageへ描画
2. overlayRenderer.uploadRegion()
3. session.acquirePresentFrame()
4. frameが無ければreturn
5. presenterへrecord／submit
```

となっている。

`uploadRegion()`は単なるCPU image cacheではない。

内部で即座に:

```text
free slot取得
mapped staging bufferへcopy
slot.state = Staged
```

まで進める。

---

## 6.2 frameが無い場合

初回ROM起動、
fullscreen中、
frame queue一時停止、
surface generation mismatch時には:

```cpp
Frame* frame =
    session.acquirePresentFrame();

if (frame == nullptr)
    return;
```

となる。

この場合:

```text
Staged slot
```

はcommand bufferへ記録されない。

次のGUI tickでもHUD dirty rectがあれば、
別のFree slotへstageする。

`kUploadSlotCount = 3`なので:

```text
tick 1: slot 0 = Staged
tick 2: slot 1 = Staged
tick 3: slot 2 = Staged
tick 4: Free slot無し
```

になる。

`acquireUploadSlotForStaging()`が再利用するのは:

```text
Free
または
Submittedかつcompleted
```

だけである。

古いStaged slotは永続的に解放されない。

これがCustomHUDが一度も表示されない、
または初回失敗後に復帰しない直接原因である。

---

# 7. CustomHUD推奨修正
# Just-in-time staging

## 7.1 uploadRegionの責務をCPU側だけにする

`uploadRegion()`では:

```text
QImage snapshot保持
dirty rectをunion
pending generation更新
```

だけを行う。

Vulkan slotを取得しない。

```cpp
bool uploadRegion(
    const QImage& image,
    const QRect& rect)
{
    pendingCpuImage = image;
    pendingCpuDirty =
        pendingCpuDirty.united(rect);
    ++pendingCpuGeneration;
    return true;
}
```

---

## 7.2 command record時にstageする

次がすべて成立した後に初めてslotを取得する。

```text
valid present frame
valid surface
swapchain image acquire成功
surface in-flight fence完了
command buffer begin成功
```

`recordSurfaceCommands()`内の
desktop transfer hookで:

```text
1. Free slot取得
2. CPU imageをmapped bufferへcopy
3. transfer barrier
4. vkCmdCopyBufferToImage
5. token生成
6. Recordedへ遷移
```

を一つのtransactionとして行う。

---

## 7.3 代替案

JIT stagingが難しい場合は、
Staged slotを常に1個だけにする。

新しいHUD imageが来たとき:

```text
既存Staged slotを上書き
dirty rectをunion
新規slotは取らない
```

ただしcommand buffer記録との境界が複雑になるため、
JIT stagingを推奨する。

---

# 8. P0
# vkQueueSubmitとvkQueuePresentを同じ成功判定にしている

## 8.1 現在の処理

`submitSurfaceCommands()`は:

```text
vkQueueSubmit()
vkQueuePresentKHR()
```

を連続実行する。

submit成功後に:

```cpp
++surfaceState.submittedSerial;
```

する。

しかしreturn値は:

```cpp
return presentResult == VK_SUCCESS;
```

である。

---

## 8.2 presentだけ失敗した場合

例:

```text
vkQueueSubmit = VK_SUCCESS
vkQueuePresentKHR = VK_ERROR_OUT_OF_DATE_KHR
```

transfer commandはGPUへsubmit済みである。

それにもかかわらずcallerはfalseを受けて:

```cpp
desktopOverlaySubmissionNotifier(
    uploadToken,
    false,
    0,
    0,
    userData);
```

を呼ぶ。

HUD側は:

```text
Recorded → Free
pending texture transition rollback
```

する。

しかしGPUはstaging bufferとtextureを使用中である。

その結果:

```text
GPU使用中staging bufferを再利用
実際には遷移済みtextureを未初期化扱い
次回barrier oldLayout不一致
HUD消失
validation error
driver hang
```

が発生し得る。

fullscreen時は`VK_ERROR_OUT_OF_DATE_KHR`が起きやすいため、
HUD不具合とfullscreen freezeが同時に発生する。

---

# 9. submit／present分離修正

戻り値を構造体にする。

```cpp
struct SurfaceSubmitResult
{
    VkResult submitResult =
        VK_NOT_READY;

    VkResult presentResult =
        VK_NOT_READY;

    bool commandSubmitted =
        false;

    u64 timelineValue = 0;
    u64 submissionSerial = 0;
};
```

処理:

```cpp
SurfaceSubmitResult result{};

result.submitResult =
    vkQueueSubmit(...);

if (result.submitResult != VK_SUCCESS)
    return result;

result.commandSubmitted = true;
result.timelineValue = signalValue;
result.submissionSerial =
    ++surfaceState.submittedSerial;

// HUDへはここでsubmit成功を通知
notifyOverlaySubmitted(
    token,
    result.timelineValue,
    result.submissionSerial);

result.presentResult =
    vkQueuePresentKHR(...);

return result;
```

present失敗は:

```text
swapchain recovery
次frameのpresentation
```

の問題であり、
すでにsubmitされたHUD transferをrollbackしてはいけない。

---

# 10. P0
# HUD texture／descriptor ownership

## 10.1 descriptor setを即更新している

`createTexture()`は新textureを作ると、
同じglobal `descriptorSet`へ:

```cpp
vkUpdateDescriptorSets(...)
```

を即実行する。

この処理はHUD描画より前、
surface fence waitより前に呼ばれ得る。

旧command bufferが同じdescriptor setを参照している場合、
update-after-bind機能無しで更新するのは不正である。

---

## 10.2 texture generationごとのdescriptor set

次を一つのbundleにする。

```cpp
struct OverlayTextureGeneration
{
    u64 generation = 0;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory =
        VK_NULL_HANDLE;
    VkImageView imageView =
        VK_NULL_HANDLE;

    VkDescriptorSet descriptorSet =
        VK_NULL_HANDLE;

    VkImageLayout committedLayout =
        VK_IMAGE_LAYOUT_UNDEFINED;

    u64 lastUseTimeline = 0;
    u64 lastUseSubmissionSerial = 0;
};
```

新texture作成時は
新しいdescriptor setをallocateする。

旧descriptor setを上書きしない。

---

## 10.3 retirement serialが誤っている

現在、旧texture退役時に:

```cpp
retireCurrentTexture(
    lastPresentTimelineValue,
    lastCompletedSubmissionSerial);
```

を使う。

非timeline環境では
`lastCompletedSubmissionSerial`は
すでに完了済みの値である。

必要なのは:

```text
旧textureを最後に参照したsubmission serial
```

である。

```cpp
oldTexture.lastUseSubmissionSerial
```

を保存し、
そのserialがcompletedになった後に破棄する。

---

## 10.4 single pending transitionを廃止

現在はglobalに:

```text
pendingTextureTransition
```

を一つだけ持つ。

transitionはupload tokenへ所属させる。

```cpp
struct OverlayUploadTicket
{
    u64 token = 0;
    u32 slotIndex = UINT32_MAX;
    u64 textureGeneration = 0;

    VkImageLayout oldLayout =
        VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageLayout newLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    QRect dirtyRect;
};
```

submit成功時に
そのtokenのtexture generationだけcommitする。

---

# 11. P0
# fullscreen黒画面と再切替freezeの直接原因

## 11.1 recoveryでGUI threadを最大250ms待つ

`recoverSwapchain()`:

```cpp
constexpr u64 kRecoverSwapchainWaitNs =
    250'000'000ull;

waitForSurfaceIdle(
    surfaceState,
    kRecoverSwapchainWaitNs);
```

これは最大250ms GUI threadを止める。

---

## 11.2 成功するとさらに無制限wait

wait成功後:

```cpp
destroySwapchain(
    surfaceState);
```

へ進む。

`destroySwapchain()`内で:

```cpp
vkQueueWaitIdle(
    surfaceState.presentQueue);
```

を呼ぶ。

このwaitにはtimeoutがない。

さらに共有queue lockを保持している。

したがって:

```text
fullscreen
→ swapchain out-of-date
→ recovery
→ GUI threadがqueue idle待ち
→ その間にfullscreen再切替
→ event処理不能
→ フリーズに見える
```

という報告症状と一致する。

---

# 12. fullscreen修正
# normal UI pathからQueueWaitIdleを完全排除

通常のfullscreen／resize／out-of-date recoveryで
以下を呼ばない。

```text
vkDeviceWaitIdle
vkQueueWaitIdle
無制限vkWaitForFences
```

## 12.1 非破壊recovery

```text
active swapchainは保持
swapchainDirty=true
replacementをpendingへ作成
replacement完成後にactiveと交換
旧activeをtimeline／submission serialでretire
```

present queueの完了を直接確認できない環境では:

```text
旧swapchainを即破棄しない
application final shutdownまで保留
またはpresent-id／present-wait対応時のみ正確に退役
```

とする。

少量の一時的resource保持は、
GUI freezeより安全である。

---

## 12.2 recovery state machine

```cpp
enum class SurfaceRecoveryState
{
    Stable,
    ResizePending,
    ReplacementBuilding,
    ReplacementReady,
    RetiringOld,
    SurfaceLost,
};
```

GUI eventではstateを変更するだけ。

重いresource buildは1 event loopに1回、
再入不可で行う。

```cpp
if (rebuildInProgress)
{
    pendingExtent = newestExtent;
    return;
}
```

---

## 12.3 二度目のtoggle

Window側のreverse toggle修正は正しい。

しかしVulkan presentがGUI threadを止めている限り、
Qtの`QTimer::singleShot`は実行されない。

したがってWindow state machineだけでは
freezeを解決できない。

QueueWaitIdle排除が必須。

---

# 13. swapchain format cacheの追加不具合

## 13.1 cache resourceが破棄されない

新しく:

```cpp
std::unordered_map<
    VkFormat,
    SurfaceFormatRenderResources>
cachedSurfaceFormatResources;
```

を持っている。

bundle側は:

```cpp
ownsRenderResources = false;
```

としてcacheのpipeline／render passを借用する。

しかし`destroyCommonResources()`では:

```text
sampler
shader module
pipeline layout
descriptor pool
descriptor set layout
```

だけを破棄している。

cache内の:

```text
VkPipeline
VkRenderPass
```

を破棄していない。

---

## 13.2 正しいshutdown順序

```cpp
for (auto& [format, resources] :
     cachedSurfaceFormatResources)
{
    if (resources.pipeline
        != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(
            device,
            resources.pipeline,
            nullptr);
    }

    if (resources.renderPass
        != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(
            device,
            resources.renderPass,
            nullptr);
    }
}

cachedSurfaceFormatResources.clear();
```

これは`pipelineLayout`とshader moduleを破棄する前に行う。

---

# 14. overlay shutdownのDeviceWaitIdle

`MelonPrimeVulkanOverlayRenderer::shutdown()`は:

```cpp
vkDeviceWaitIdle(device);
```

を呼ぶ。

application最終終了では許容できるが、
renderer switchやpanel teardownが
通常UI経路で起きる場合はfreeze要因になる。

推奨:

```text
最終process shutdown:
    device wait idle可

renderer switch／window再生成:
    exact timeline／serial retirement
    bounded wait
    callback detach
```

に分離する。

---

# 15. 初回Vulkan ROM白画面

## 15.1 renderer transaction自体

新しいNDS作成時には:

```cpp
updateVideoRenderer();
```

が呼ばれ、

```cpp
videoSettingsDirty = true;
lastVideoRenderer = -1;
```

となる。

`updateRenderer()`は:

```text
CreateRendererForSelection
SetRenderer3D
ActivateSapphireVulkan2D
session.initialize
session.beginGeneration
```

を行う。

前回疑った
`lastVideoRenderer`だけが古い問題は
現在の経路では回避されている。

---

## 15.2 現時点の最優先判断

現HEADはcompile不能なので、
初回白画面のruntime監査を
このHEADの結果として扱えない。

次の順序で切り分ける。

```text
1. header／cpp mismatch修正
2. clean build
3. BuildIdentityでHEAD確認
4. Vulkan選択状態でapplication起動
5. ROM open
6. producer／publication／present trace確認
```

---

## 15.3 必須trace

最初の180 frameだけ:

```text
[RomBootVulkan]
newNds
updateRendererBegin
renderer3DInstalled
sapphire2DActivated
sessionInitialized
rendererGeneration
surfaceGeneration

[SapphirePhysicalPublish]
valid
frameSerial
publicationGeneration
rendererGeneration
topSlot
bottomSlot

[VulkanProducer]
beginResult
frameViewValid
frameViewSerial
frameViewGeneration
publishedValid
latchResult
prepareResult
queuePush

[VulkanPresenter]
candidateFrame
surfaceGenerationMatch
buildInputs
ensureSwapchain
acquireResult
submitResult
presentResult
```

白画面が続く最初のreasonを一つだけ記録する。

---

## 15.4 禁止する初回白画面対策

```text
最初のN frameを捨てる
white率でframe reject
Software framebufferを一時表示
他rendererを自動経由
splashを強制的に隠す
sleepを追加
```

これらはownership不具合を隠すだけである。

---

# 16. 4xでゲーム内文字が潰れる理由

## 16.1 現在のfilter設定

Qt側は:

```cpp
config.filtering =
    filter
        ? VulkanFilterMode::Linear
        : VulkanFilterMode::Nearest;
```

とする。

presenterはLinear時に:

```cpp
screenImageInfo.sampler =
    linearSampler;
```

を選ぶ。

つまりinteger 4xでも、
Screen Filteringが有効なら
DS 2D文字をlinear samplingする。

細い1px線を持つDSフォントは:

```text
隣接pixelとの平均
半pixel origin
最終window scaleの非整数比
```

により潰れて見える。

---

## 16.2 integer scaleでは2DをNearestにする

Sapphire compositorの役割を分離する。

```text
packed／structured 2D reconstruction:
    Nearest

3D image sample:
    user-selected filter

最終surface scaling:
    user-selected filter
```

最小修正として、
axis aligned integer transformでは:

```cpp
const bool exactIntegerScreen =
    integerAxisAligned
    && transformScaleIsInteger
    && destinationOriginIsInteger;

effective2DFilter =
    exactIntegerScreen
        ? VulkanFilterMode::Nearest
        : config.filtering;
```

とする。

---

## 16.3 4x validation matrix

| Internal | Surface transform | UI Filtering | 2D source | Expected |
|---:|---|---|---|---|
| 1x | integer | OFF | Nearest | reference |
| 4x | integer | OFF | Nearest | pixel exact |
| 4x | integer | ON | Nearest for 2D | glyph exact |
| 4x | non-integer | ON | final Linear | smooth layout |
| 4x | rotated | ON | final Linear | rotated output |
| 4x | integer | RetroArch | preset-defined | preset test |

---

# 17. Sapphire準拠判定

## 17.1 exact upstreamの部分

manifest上、次はexact upstreamである。

```text
VulkanSurfacePresenter.vert
VulkanSurfacePresenter.frag
VulkanCompositorShader.comp
VulkanAccumulate3dShader.comp
```

shader本体はSapphireと同一。

---

## 17.2 normalized／desktop adapterの部分

次はSapphireそのままではない。

```text
FrameQueue.cpp／h
VulkanOutput.cpp／h
VulkanSurfacePresenter.cpp／h
GPU2D_Soft.cpp／h
```

manifestでも:

```text
parity_mode = normalized_upstream
allowed_transform = desktop_vulkan_adapter
```

となっている。

---

## 17.3 presenterの差分は大きい

Sapphire upstream presenterは:

```text
ANativeWindow ownership
単純SurfaceState
1 active swapchain
HUD callback無し
affine screen transform無し
submission serial無し
format resource cache無し
desktop overlay無し
```

である。

現在のMelonPrime presenterには:

```text
VkSurfaceKHR borrowed host
pending／active swapchain bundle
retire queue
affine transforms
desktop overlay callbacks
HUD upload token
submission serial
format-scoped render resources
Qt lifecycle向けrecovery
```

が入っている。

したがって:

```text
Sapphireと同じ実装
```

ではなく:

```text
Sapphire presenterを大幅にdesktop拡張したfork
```

である。

---

# 18. 車輪の再発明を減らす最終構造

## 18.1 vendor core

次を可能な限りSapphireそのまま保持する。

```text
VulkanOutput
FrameQueue
presenter shader
composition rules
draw-call generation
frame wait
descriptor input contract
GPU2D SoftRenderer algorithm
```

## 18.2 desktop wrapper

新規:

```text
MelonPrimeDesktopVulkanPresenter.h
MelonPrimeDesktopVulkanPresenter.cpp
```

責務:

```text
Qt VkSurfaceKHR lifecycle
fullscreen resize coalescing
swapchain replacement policy
HUD JIT upload
HUD final overlay
platform present queue
desktop diagnostics
```

## 18.3 generic extension point

vendor presenterへHUD固有APIを増やさない。

必要なら一つの汎用hookだけにする。

```cpp
struct DesktopCommandExtension
{
    virtual void recordBeforeGamePass(
        VkCommandBuffer) = 0;

    virtual void recordAfterGameDraw(
        VkCommandBuffer,
        const DesktopRenderTarget&) = 0;

    virtual void onQueueSubmitted(
        const DesktopSubmission&) = 0;
};
```

HUD token、Qt widget、QImageを
Sapphire presenterへ直接認識させない。

---

# 19. GPU2Dの長期方針

physical publicationは修正されたが、
GPU2D state ownershipはまだ:

```text
native melonDS GPU2D_A／B
+
Sapphire UnitA／B mirror
```

である。

`UnitSync`を増やし続けず、
Sapphire coreのGPU2D dependency closureを
ファイル単位でvendorする。

対象:

```text
GPU2D.h
GPU2D.cpp
GPU2D_Soft.h
GPU2D_Soft.cpp
Renderer2D contract
register write timing
window timing
mosaic timing
VBlank ordering
capture ownership
AssignFramebuffers
```

canonical GPU2D stateを一つにする。

---

# 20. 推奨commit分割

## S70-1

```text
Fix VulkanSurfacePresenter declaration mismatch and add Vulkan compile CI
```

内容:

```text
header／cpp一致
clean Vulkan build
BuildIdentity
```

## S70-2

```text
Stage Vulkan HUD uploads only while recording a valid present command
```

内容:

```text
uploadRegionはCPU cacheのみ
JIT staging
孤児Staged slot廃止
```

## S70-3

```text
Separate Vulkan queue submission from queue presentation results
```

内容:

```text
SurfaceSubmitResult
submit成功をtokenへ正確に通知
present failureでrollback禁止
```

## S70-4

```text
Version Vulkan HUD textures and descriptor sets per resource generation
```

## S70-5

```text
Retire HUD resources by exact last-use timeline or submission serial
```

## S70-6

```text
Remove queue-idle waits from fullscreen swapchain recovery
```

## S70-7

```text
Make swapchain replacement non-blocking and non-destructive
```

## S70-8

```text
Destroy cached surface-format render resources at presenter shutdown
```

## S70-9

```text
Force nearest 2D reconstruction for exact integer screen transforms
```

## S70-10

```text
Add initial Vulkan ROM boot runtime contract tests
```

## S70-11

```text
Add fullscreen reverse-toggle and out-of-date failure injection tests
```

## S70-12

```text
Move desktop WSI and overlay ownership behind a generic presenter wrapper
```

---

# 21. 実装順序

```text
1. S70-1
2. clean build／HEAD確認
3. 2D／初回ROM再試験
4. S70-2
5. S70-3
6. S70-4
7. S70-5
8. CustomHUD再試験
9. S70-6
10. S70-7
11. S70-8
12. fullscreen stress test
13. S70-9
14. 4x golden test
15. S70-10
16. S70-11
17. S70-12
```

---

# 22. 必須test

## 22.1 clean compile

```text
Windows Vulkan ON
Linux Vulkan ON
macOS Vulkan ON
MELONPRIME_CUSTOM_HUD ON
Debug
Release
```

## 22.2 CustomHUD no-frame test

100 GUI tickの間:

```text
acquirePresentFrame = nullptr
HUD dirty = true
```

完了条件:

```text
Staged slot数 <= 1
slot永久枯渇 = 0
frame復帰後1～2 submit以内にHUD表示
```

## 22.3 submit／present分離test

```text
vkQueueSubmit = success
vkQueuePresentKHR = out-of-date
```

完了条件:

```text
HUD token = Submitted
slot = GPU completionまで再利用不可
texture layout = committed after submit
swapchainだけdirty
```

## 22.4 fullscreen stress

```text
fullscreen
100ms後windowed
100ms後fullscreen
```

20回。

failure injection:

```text
acquire out-of-date
present out-of-date
present suboptimal
surface resize storm
DPR change
```

完了条件:

```text
vkQueueWaitIdle call = 0
vkDeviceWaitIdle call = 0
GUI thread block > 100ms = 0
surface recreation = 0
freeze = 0
```

## 22.5 2D physical test

```text
ScreenSwap=false
ScreenSwap=true
menu
match
pause
transition
```

完了条件:

```text
top packed slot = 0
bottom packed slot = 1
top physical enum = Top
bottom physical enum = Bottom
packed／structured pairing一致
```

## 22.6 initial ROM test

application fresh startから20回。

```text
Vulkanを事前選択
ROM open
renderer switch無し
```

完了条件:

```text
queuePush <= 3 valid renderer frames
PresentedGameFrame到達
白画面固定 = 0
```

## 22.7 4x golden image

```text
Software reference
Vulkan 1x nearest
Vulkan 4x nearest
Vulkan 4x filtering ON
```

2D text regionをpixel diffする。

---

# 23. 禁止事項

```text
旧exeでHEAD修正を評価する
Python source testだけでdoneにする
HUDをpresent frame取得前にVulkan stagingする
Staged slotを複数作る
vkQueueSubmit成功をpresent失敗でrollbackする
in-flight descriptor setを即更新する
completed serialをlast-use serialとして使う
fullscreen recoveryでvkQueueWaitIdleする
通常UI transitionでvkDeviceWaitIdleする
白画面をframe delayで隠す
2D文字へROM固有補正shaderを追加する
Sapphire vendor presenterへQt／HUD固有状態を増やし続ける
UnitSync heuristicを無制限に追加する
```

---

# 24. 最終判断

S69のphysical publication修正はコード上は正しい。

```text
top = framebuffer slot 0
bottom = framebuffer slot 1
packed／structured = 同じphysical screen view
```

後段のframe latchでも再swapは見つからない。

それでもユーザー環境で症状が変わらないのは、
現HEADがVulkan有効buildでコンパイル不能であり、
旧バイナリが実行されている可能性が最も高い。

新規build後に残ると予測される主要不具合は:

```text
CustomHUD:
    no-frame時のStaged slot枯渇
    submit／present結果混同
    descriptor／texture generation ownership

fullscreen:
    recovery内のvkQueueWaitIdle
    GUI thread同期swapchain処理

4x text:
    integer倍率でもlinear sampler使用
```

車輪の再発明を最小化する方針は:

```text
Sapphire composition／queue／shaderをvendor coreとして固定
Qt WSI／fullscreen／HUDをdesktop wrapperへ分離
GPU2Dは長期的にSapphire Unitをcanonical stateへする
```

---

# 40. 進捗トラッキング（実施ログ）

| Phase | Commit | Status |
|---|---|---|
| S70-1 | d4bae9670 | BuildIdentity, header/cpp fix, S70 parity CI hook |
| S70-2 | d4bae9670 | JIT HUD staging in recordPendingTransfer |
| S70-3 | d4bae9670 | SurfaceSubmitResult submit/present separation |
| S70-4 | d4bae9670 | Per-generation HUD descriptor sets |
| S70-5 | d4bae9670 | Retire textures by last-use serial |
| S70-6 | d4bae9670 | Remove vkQueueWaitIdle from destroySwapchain |
| S70-7 | d4bae9670 | Non-destructive recoverSwapchain (dirty only) |
| S70-8 | d4bae9670 | Destroy cachedSurfaceFormatResources at shutdown |
| S70-9 | d4bae9670 | screenIntegerDescriptorSet for integer transforms |
| S70-10 | d4bae9670 | test_sapphire_vulkan_lifecycle_s70_parity.py |
| S70-11 | d4bae9670 | Recovery contract tests in S70 parity suite |
| S70-12 | d4bae9670 | MelonPrimeDesktopVulkanPresenter wrapper |


である。
