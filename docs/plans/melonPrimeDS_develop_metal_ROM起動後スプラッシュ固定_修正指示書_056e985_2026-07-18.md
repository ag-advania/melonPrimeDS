# melonPrimeDS `develop_metal`
# ROM起動後もスプラッシュ画面から変わらない問題 修正指示書

**作成日:** 2026-07-18  
**対象リポジトリ:** `https://github.com/ag-advania/melonPrimeDS`  
**対象ブランチ:** `develop_metal`  
**基準HEAD:** `056e985196f7f07ad6e4bceaf5151c6877d01ec8`  
**症状:** ROMを開いてエミュレーションが開始しても、Metal presenter上のスプラッシュ画面が残り続け、DS画面が1フレームも表示されない。  
**優先度:** BLOCKER  
**対象renderer:** Metal Raster／Metal Compute Shader  
**対象OS:** macOS  
**目的:** Software rendererを再導入せず、Metal Full-GPU frameのbootstrap循環を解消し、ROM起動直後から正常な`MetalTexture` outputをpublishする。  

---

# 0. 結論

現在の停止原因は、Metal 2D segment snapshotの準備条件が循環していることである。

現行の大まかな順序:

```text
Start3DRendering()
  ↓
IsMetalFullGpuFrameEligible()
  ↓
mixed 3D＋2D frameではSegmentedSnapshotReady()を要求
  ↓
snapshot slotはまだ予約されていないためfalse
  ↓
FrameActive=false
  ↓
DrawScanline()／DrawSprites()は何もしない
  ↓
line 0で行うはずだったsnapshot slot予約も実行されない
  ↓
次frameもSegmentedSnapshotReady=false
```

この循環により、Metal Full-GPU pathは最初の1フレームを開始できない。

PR-7でSoftware fallbackを撤廃し、PR-9でpresenterをMetalTexture-onlyにしたため、Full-GPU frameが開始できない場合は次の状態になる。

```text
MetalRenderer::AcquireOutputLease()
  → RendererOutputKind::None

ScreenPanelMetal::drawScreen()
  → finalMetalTextureForFrame == nil
  → return
  → CAMetalLayerへ新しいdrawableをpresentしない
  → 最後にpresentされたスプラッシュ画面が残り続ける
```

したがって、修正の中心は次の2点である。

1. **segment snapshot slotをFull-GPU eligibility判定より前に予約する**
2. **Metalの必須pipeline初期化失敗を無視して`Init()`成功扱いしない**

Software／CpuBgra fallbackを復活させて症状を隠してはならない。

---

# 1. 根本原因

## 1.1 問題のある責務分担

現在の`MetalRenderer2D::BeginSegmentSnapshotFrameIfNeeded()`は、line 0から呼ばれたときだけ次frame用slotを予約する。

概念上の現行コード:

```cpp
void MetalRenderer2D::BeginSegmentSnapshotFrameIfNeeded(int line)
{
    if (!State || line != 0)
        return;

    const uint32_t frameEpoch = GPU.NDS.NumFrames;

    if (State->SnapshotFrameEpoch != frameEpoch)
    {
        // 前frame状態をreset
    }

    if (!State->SegmentedCaptureAttempted &&
        State->SnapshotBuffersReady)
    {
        // upload ring slotを予約
    }
}
```

呼出元:

```cpp
void MetalRenderer2D::CaptureScanlineState(int line)
{
    BeginSegmentSnapshotFrameIfNeeded(line);
    ...
}

void MetalRenderer2D::CaptureSpriteScanlineState(int line)
{
    BeginSegmentSnapshotFrameIfNeeded(line);
    ...
}
```

しかし`MetalRenderer::DrawScanline()`と`DrawSprites()`は、`FullGpuState->FrameActive`がfalseなら何もしない。

```cpp
if (FullGpuState && FullGpuState->FrameActive)
{
    Metal2D_A->CaptureScanlineState(line);
    Metal2D_B->CaptureScanlineState(line);
    ...
    return;
}

// FrameActive=falseではno-op
```

その`FrameActive`を決める前に、`IsMetalFullGpuFrameEligible()`が`SegmentedSnapshotReady()`を確認している。

```cpp
if (MetalEngineAUsesMixed3DAnd2DOverlay(GPU))
{
    if (!Metal2D_A->SegmentedSnapshotReady() ||
        !Metal2D_B->SegmentedSnapshotReady())
    {
        return false;
    }
}
```

つまり次の相互依存になっている。

```text
FrameActive=trueにするにはsnapshot slotが必要
snapshot slotを作るにはFrameActive=trueが必要
```

これはbootstrap不能である。

## 1.2 `SegmentedSnapshotReady()`の意味が不明瞭

現在の`SegmentedSnapshotReady()`は「192 line分のsnapshotが完成している」という意味ではない。

実際に確認しているもの:

- snapshot buffer resourcesが存在する
- upload ringが存在する
- current slotが予約済み
- BusyMaskにslot bitが立っている
- buffer contentsへアクセスできる

一方、192 lineの完成確認は`SegmentedFrameComplete()`が行う。

```text
SegmentedSnapshotReady()
    = frame capture用slotを使用できる

SegmentedFrameComplete()
    = LayerSnapshotValid[0～191]と
      SpriteSnapshotValid[0～191]が完成
```

この2つを明確に分離する必要がある。

## 1.3 PR-7以降は失敗が画面に出ない

`MetalRenderer`は既に`SoftRenderer`を継承していない。

Full-GPU不適格frame:

```text
DrawScanline no-op
DrawSprites no-op
VBlank RetainPrevious
SwapBuffers composeなし
AcquireOutputLease None
```

初回frameではRetainPrevious対象のtextureも存在しない。

そのためpresenterは新しいdrawableを出さず、スプラッシュが残る。

## 1.4 初期化失敗も成功扱いされる

現在の`MetalRenderer::Init()`は、次の初期化が失敗しても最後に`true`を返す。

- `ConfigureMetalVisibleOutput`
- `InitializeMetalFullGpuOutput`
- `ConfigureMetalCaptureState`

ログには次のような古い説明も残る。

```text
CPU output remains available
stable CPU compositor path remains active
capture frames remain on the CPU path
```

しかしPR-7／PR-9後はこれらのCPU fallbackは存在しない。

必須pipelineが失敗した状態で`Init()`を成功扱いすると、rendererは選択されたままoutputだけ永久に`None`になる。

---

# 2. 修正方針

## 2.1 最小かつ正しい修正

次のframe lifecycleへ変更する。

```text
Start3DRendering
  ↓
renderer-owned frame epochを発行
  ↓
Metal2D_A／Bのsegment snapshot frameを明示的に開始
  ↓
upload ring slot予約成功を確認
  ↓
Full-GPU static eligibilityを確認
  ↓
FrameActive=true
  ↓
line 0～191でsnapshotを記録
  ↓
VBlankでSegmentedFrameCompleteを確認
  ↓
segment render
  ↓
final texture compose
  ↓
output publish
  ↓
presenterがlease取得
```

重要:

- line 0をframe initializationのトリガーにしない
- `NDS::NumFrames`更新タイミングへ暗黙依存しない
- renderer側で明示的なmonotonic epochを持つ
- snapshot完成確認はVBlankで行う
- temporary ring backpressureは1frameのRetainPreviousで扱う
- permanent initialization failureはrenderer init failureとして扱う

## 2.2 やってはいけない修正

禁止:

```text
Software fallback復活
CpuBgra presenter復活
screenTex CPU upload復活
SegmentedSnapshotReady()を常にtrueにする
FrameActiveを無条件trueにする
SegmentedFrameComplete()確認削除
失敗時に古いsplashを無期限表示
60frame待てば成功扱い
MELONPRIME_METAL_FULL_GPU=0を通常回避策にする
```

---

# 3. 対象ファイル

必須変更:

```text
src/GPU2D_Metal.h
src/GPU2D_MetalFullGpuMethods.inc
src/GPU_Metal.h
src/GPU_MetalFullGpuMethods.inc
src/GPU_Metal.mm
src/frontend/qt_sdl/MelonPrimeScreenMetal.mm
tools/ci/audits/audit-metal-forbidden-paths.py
```

追加推奨:

```text
tools/ci/audits/audit-metal-frame-bootstrap.py
docs/plans/evidence/metal-frame-bootstrap-fix-2026-07-18.md
```

原則変更しない:

```text
src/GPU_Soft.cpp
src/GPU_Soft.h
src/GPU2D_Soft.cpp
src/GPU3D_Soft.cpp
OpenGL renderer
Windows presenter
Linux presenter
```

---

# 4. 実装1: 明示的なsnapshot frame begin API

## 4.1 `GPU2D_Metal.h`

次のpublic APIを追加する。

```cpp
bool BeginSegmentedSnapshotFrame(uint64_t frameEpoch) noexcept;
void CancelSegmentedSnapshotFrame(uint64_t frameEpoch) noexcept;
```

任意だが推奨:

```cpp
[[nodiscard]] uint64_t CurrentSegmentedSnapshotEpoch() const noexcept;
[[nodiscard]] bool SegmentedSnapshotCaptureReady() const noexcept;
```

既存の`SegmentedSnapshotReady()`は意味が曖昧なので、可能なら次へrenameする。

```cpp
SegmentedSnapshotReady()
    ↓
SegmentedSnapshotCaptureReady()
```

rename範囲が大きくなる場合は、今回は既存名を維持してコメントで意味を固定してよい。

コメント:

```cpp
// Returns whether a per-frame upload-ring slot has been reserved and is
// writable for the current renderer-owned frame epoch.
// This does NOT mean all 192 scanline snapshots are complete.
// Use SegmentedFrameComplete() for completion.
```

## 4.2 renderer-owned epoch

`NDS::NumFrames`をframe slot ownershipの唯一のauthorityにしない。

`MetalRenderer::MetalFullGpuState`へ追加:

```cpp
uint64_t NextSnapshotEpoch = 1;
uint64_t ActiveSnapshotEpoch = 0;
```

または`MetalRenderer`本体へ追加してもよい。

要求:

- `Start3DRendering()`ごとに1回だけ発行
- 0を使用しない
- reset／renderer recreateで新instanceになるため、instance内monotonicでよい
- overflow時は0をskipする

helper例:

```cpp
uint64_t MetalRenderer::AllocateSnapshotEpoch() noexcept
{
    uint64_t epoch = FullGpuState->NextSnapshotEpoch++;
    if (epoch == 0)
        epoch = FullGpuState->NextSnapshotEpoch++;
    return epoch;
}
```

## 4.3 `BeginSegmentedSnapshotFrame`

現在の`BeginSegmentSnapshotFrameIfNeeded(int line)`に含まれるreset／slot予約処理を、明示APIへ移す。

実装意図:

```cpp
bool MetalRenderer2D::BeginSegmentedSnapshotFrame(
    uint64_t frameEpoch) noexcept
{
    if (!State ||
        frameEpoch == 0 ||
        !State->SnapshotBuffersReady ||
        !State->SegmentedUploadRing ||
        !State->SegmentedUploadRing->Ready())
    {
        return false;
    }

    // 同じepochへの二重beginはidempotent。
    if (State->SnapshotFrameEpoch == frameEpoch)
        return SegmentedSnapshotReady();

    // 前frameがVBlankまで到達せずslotをconsumeしなかった場合に解放。
    MetalReleaseSegmentedUploadSlot(
        State->SegmentedCaptureRing,
        State->SegmentedCaptureSlot);

    State->SegmentedCaptureRing.reset();
    State->SegmentedCaptureSlot = -1;
    State->SegmentedCaptureAttempted = false;

    State->LayerSnapshotValid.fill(0);
    State->SpriteSnapshotValid.fill(0);
    State->LayerSnapshotLastLine = -1;
    State->SpriteSnapshotLastLine = -1;
    State->SegmentedFrameOutputCleared = false;
    State->SnapshotFrameEpoch = frameEpoch;

    State->SegmentedCaptureAttempted = true;

    std::shared_ptr<MetalSegmentedUploadRing> ring =
        State->SegmentedUploadRing;

    const int32_t slot =
        MetalReserveSegmentedUploadSlot(ring);

    if (slot < 0)
        return false;

    State->SegmentedCaptureRing = std::move(ring);
    State->SegmentedCaptureSlot = slot;

    return SegmentedSnapshotReady();
}
```

注意:

- 上記は実装意図であり、実際の型名／helper visibilityへ合わせる
- slot予約後にfalseを返す経路では必ずslotを解放する
- `SegmentedCaptureAttempted=true`のまま永久にretry不能にしない
- 同epochで一度失敗した場合のretry policyを明示する

推奨retry policy:

```text
同じStart3DRendering呼出内:
    retryしない

次frame:
    新epochで再試行

pause／savestate abort:
    CancelSegmentedSnapshotFrame
```

## 4.4 line 0依存を削除

現在のprivate helper:

```cpp
void BeginSegmentSnapshotFrameIfNeeded(int line) noexcept;
```

は削除するか、互換wrapperにする。

推奨は削除。

`CaptureScanlineState()`／`CaptureSpriteScanlineState()`はframeを開始してはいけない。

変更後:

```cpp
void MetalRenderer2D::CaptureScanlineState(int line) noexcept
{
    if (!State ||
        line < 0 ||
        line >= kScreenH ||
        !SegmentedSnapshotReady())
    {
        return;
    }

    ...
}
```

同様にsprite側も、予約済みslotへ書くだけにする。

## 4.5 bool returnへの変更

可能なら次をboolへ変更する。

```cpp
bool CaptureScanlineState(int line) noexcept;
bool CaptureSpriteScanlineState(int line) noexcept;
```

成功条件:

- line範囲内
- active snapshot slotあり
- buffer contents取得成功
- scanline config更新成功
- valid bit設定成功

`MetalRenderer::DrawScanline()`側:

```cpp
const bool a = Metal2D_A->CaptureScanlineState(line);
const bool b = Metal2D_B->CaptureScanlineState(line);

if (!a || !b)
{
    FullGpuState->FrameValid = false;
    FullGpuState->BlockedByMidFrameInvalidation = true;
    ...
}
```

sprite側も同様。

今回の変更量を抑える場合、voidのままでもVBlankの`SegmentedFrameComplete()`で検出できる。ただしbool化を推奨する。

---

# 5. 実装2: `Start3DRendering()`の順序修正

## 5.1 正しい順序

`MetalRenderer::Start3DRendering()`を次の順序へ変更する。

```cpp
void MetalRenderer::Start3DRendering()
{
    if (!FullGpuState)
        FullGpuState = std::make_unique<MetalFullGpuState>();

    // 既存block/cooldown更新。
    UpdateFullGpuBlockState();

    const uint64_t epoch =
        AllocateSnapshotEpoch();

    FullGpuState->ActiveSnapshotEpoch = epoch;

    const bool snapshotAReady =
        Metal2D_A &&
        Metal2D_A->BeginSegmentedSnapshotFrame(epoch);

    const bool snapshotBReady =
        Metal2D_B &&
        Metal2D_B->BeginSegmentedSnapshotFrame(epoch);

    const bool staticEligible =
        !FullGpuState->BlockedByCaptureFeedback &&
        !FullGpuState->BlockedByMidFrameInvalidation &&
        MetalCaptureResourcesCoherent() &&
        IsMetalFullGpuFrameEligible();

    FullGpuState->FrameActive =
        snapshotAReady &&
        snapshotBReady &&
        staticEligible;

    FullGpuState->FrameValid =
        FullGpuState->FrameActive;

    if (FullGpuState->FrameActive)
    {
        BeginMetalCaptureFrame();
    }
    else
    {
        if (Metal2D_A)
            Metal2D_A->CancelSegmentedSnapshotFrame(epoch);
        if (Metal2D_B)
            Metal2D_B->CancelSegmentedSnapshotFrame(epoch);
    }

    Metal3DSetCpuReadbackRequired(
        Rend3D.get(),
        !FullGpuState->FrameActive);

    Rend3D->RenderFrame();
}
```

## 5.2 `IsMetalFullGpuFrameEligible()`の責務

この関数は副作用なしの判定に限定する。

確認してよいもの:

- FullGpuState存在
- Requested
- pipeline存在
- output state Ready
- Metal2D resource Ready
- capture state Ready
- ScreensEnabled
- display mode
- required device／size consistency
- capture resource coherence
- feature support

確認してはいけないもの:

- frame slotの新規予約
- valid arrayのreset
- epoch発行
- command submission
- completion wait

## 5.3 snapshot readiness条件

二つの設計から選ぶ。

### 推奨設計A

`Start3DRendering()`でbegin結果を別途確認するため、`IsMetalFullGpuFrameEligible()`から次を削除する。

```cpp
Metal2D_A->SegmentedSnapshotReady()
Metal2D_B->SegmentedSnapshotReady()
```

代わりに静的resource readiness:

```cpp
Metal2D_A->FullGpuReady()
Metal2D_B->FullGpuReady()
```

のみを確認する。

### 設計B

beginを先に実行したあとで、`IsMetalFullGpuFrameEligible()`がreadyを確認する。

この場合も順序は必ず次である。

```text
BeginSegmentedSnapshotFrame
→ IsMetalFullGpuFrameEligible
```

beginより前にreadyを確認してはならない。

設計Aを推奨する。判定関数がcurrent frame lifecycleへ依存しなくなるためである。

## 5.4 全frameで準備する

mixed 3D＋2D frameだけを特別扱いしない。

`RenderMetalFullGpuFrameSegmented()`は両engineの`SegmentedFrameComplete()`を常に要求するため、Full-GPU候補frameではA／B両方のsnapshot frameを常に開始する。

---

# 6. 実装3: VBlank completion contract

## 6.1 現行確認位置を維持

次はVBlank直前／segment render直前で確認する。

```cpp
Metal2D_A->SegmentedFrameComplete()
Metal2D_B->SegmentedFrameComplete()
```

この位置は正しい。

## 6.2 completion failure

一部lineが欠けた場合:

```text
FrameValid=false
Completed=RetainPrevious
current snapshot slotを確実に解放
strict diagnostic
次frameで新epochから再試行
```

初回frameでRetainPrevious対象がない場合も、slot leakを起こさない。

## 6.3 slot ownership

slot lifecycle:

```text
BeginSegmentedSnapshotFrame
  → reserved

Capture line 0～191
  → writable

RenderSegmentedGpuFrame
  → MetalTakeSegmentedUploadLease
  → ownershipをcommand buffer completionへ移動

command completion
  → BusyMask clear
```

異常終了:

```text
FrameActive=false
Frame invalidated
VBlank早期return
renderer reset
savestate
ROM close
renderer switch
```

では`CancelSegmentedSnapshotFrame()`が未submit slotを解放する。

## 6.4 二重解放防止

`CancelSegmentedSnapshotFrame()`はidempotentにする。

```cpp
void MetalRenderer2D::CancelSegmentedSnapshotFrame(
    uint64_t frameEpoch) noexcept
{
    if (!State ||
        State->SnapshotFrameEpoch != frameEpoch)
    {
        return;
    }

    MetalReleaseSegmentedUploadSlot(
        State->SegmentedCaptureRing,
        State->SegmentedCaptureSlot);

    State->SegmentedCaptureRing.reset();
    State->SegmentedCaptureSlot = -1;
    State->SegmentedCaptureAttempted = false;
}
```

command bufferへownershipを移した後は、state側slotが既にresetされているため何もしない。

---

# 7. 実装4: 初期化失敗の伝搬

## 7.1 `MetalRenderer::Init()`

必須component:

```text
Rend3D->Init()
ConfigureMetal2DMirror
ConfigureMetalVisibleOutput
InitializeMetalFullGpuOutput
ConfigureMetalCaptureState
```

`ConfigureMetal2DMirror()`は現在voidなのでboolへ変更する。

推奨:

```cpp
bool MetalRenderer::ConfigureMetal2DMirror(void* preferredDevice);
```

新しい`Init()`:

```cpp
bool MetalRenderer::Init()
{
    if (!Rend3D || !Rend3D->Init())
    {
        LogInitFailure("3d-renderer");
        return false;
    }

    void* preferredDevice =
        Metal3DPreferredDevice(Rend3D.get());

    if (!preferredDevice)
    {
        LogInitFailure("preferred-device");
        return false;
    }

    if (!ConfigureMetal2DMirror(preferredDevice))
    {
        LogInitFailure("2d-renderer");
        return false;
    }

    if (!ConfigureMetalVisibleOutput(preferredDevice))
    {
        LogInitFailure("visible-output");
        return false;
    }

    if (!InitializeMetalFullGpuOutput())
    {
        LogInitFailure("full-gpu-output");
        return false;
    }

    if (!ConfigureMetalCaptureState(preferredDevice))
    {
        LogInitFailure("display-capture");
        return false;
    }

    EnsureMetalCaptureExperimentState(preferredDevice);
    return true;
}
```

capture experiment scaffoldはdebug補助のため、失敗してもrenderer initを失敗させなくてよい。

## 7.2 古いログを削除

削除:

```text
CPU output remains available
stable CPU compositor path remains active
capture frames remain on the CPU path
```

置換:

```text
Metal renderer initialization failed at stage=<stage>;
no displayable MetalTexture path is available
```

## 7.3 renderer fallback

`Init()` false時は、既存のrenderer creation layerへ失敗を返す。

要件:

- Metal renderer instanceをactiveにしない
- frontendが明示的にOpenGLまたはSoftwareへ切り替える
- logとOSDを出す
- configへ無断保存しない
- 同一sessionだけfallbackしてよい
-ユーザーがMetalを選び直せる
- silent fallbackは禁止

推奨OSD:

```text
Metal renderer initialization failed.
Temporarily using OpenGL for this session.
See the log for the failing Metal stage.
```

日本語表示:

```text
Metalレンダラーの初期化に失敗しました。
このセッションでは一時的にOpenGLを使用します。
詳細はログを確認してください。
```

---

# 8. 実装5: presenterのstale splash対策

これは根本修正の代替ではない。診断性とUXの改善として行う。

## 8.1 現在の問題

`emuIsActive()==true`なのに有効なMetalTextureがない場合、presenterはreturnする。

その結果、ROM起動前に表示したスプラッシュのdrawableがCAMetalLayerに残る。

## 8.2 startup clear

ROM開始を検知した最初のframeで、まだrenderer outputがない場合は、スプラッシュを再利用せず黒clear drawableを1回presentする。

条件:

```text
emu active
Metal renderer selected
lastGoodMetalLeaseなし
valid current outputなし
startup grace内
startup clear未実施
```

動作:

```text
drawable取得
BGRA black clear
present
startup clear済み
```

DS framebufferやCpuBgraをuploadしてはならない。

## 8.3 grace超過

60 presenter frame超過後も一度もvalid MetalTextureがない場合:

- 黒clearを維持
- error OSDまたはUI通知
- strict violation
- failing stageをログ
-スプラッシュを残さない

## 8.4 状態reset

次でstartup clear／failure stateをresetする。

- ROM close
- renderer switch
- renderer recreate
- successful new ProducerId
- emulation inactive
- app reset

---

# 9. 診断ログ

## 9.1 frame bootstrap log

`MELONPRIME_METAL_DIAG=1`時だけ出す。

frame開始:

```text
[MelonPrime] metal bootstrap:
epoch=1
snapshotA=1
snapshotB=1
staticEligible=1
captureCoherent=1
frameActive=1
displayModeA=1
displayModeB=1
```

失敗時:

```text
[MelonPrime] metal bootstrap rejected:
epoch=1
stage=snapshot-A-reserve
ringReady=1
busyMask=0x7
frameActive=0
```

または:

```text
stage=static-eligibility
reason=display-mode-A
```

## 9.2 reason enum

文字列を散在させずenumを推奨する。

```cpp
enum class MetalFrameBootstrapFailure
{
    None,
    FullGpuNotRequested,
    OutputNotReady,
    FullGpuPipelineMissing,
    Metal2DANotReady,
    Metal2DBNotReady,
    CaptureNotReady,
    CaptureNotCoherent,
    ScreensDisabled,
    UnsupportedDisplayModeA,
    UnsupportedDisplayModeB,
    SnapshotAReserveFailed,
    SnapshotBReserveFailed,
    SnapshotAIncomplete,
    SnapshotBIncomplete,
    SegmentBuildFailed,
    SegmentRenderAFailed,
    SegmentRenderBFailed,
    CaptureEncodeFailed,
    FinalComposeFailed,
};
```

## 9.3 最初の成功ログ

必須:

```text
metal bootstrap: first frame activated
metal 2d: first complete segmented snapshot
metal full-gpu: first segmented frame rendered
metal visible output: first Full-GPU compose submitted
metal visible output: first slot published
metal presenter: first native texture accepted
metal frame: present frame=1
```

## 9.4 rate limit

同じ失敗を毎frame出さない。

- first occurrence
- reason change
- 600 frame summary
- shutdown summary

のみ。

---

# 10. static audit追加

## 10.1 `audit-metal-frame-bootstrap.py`

検証項目:

```text
BeginSegmentedSnapshotFrame public APIが存在
Start3DRenderingがeligibility前にA／B beginを呼ぶ
line 0 helperだけにbeginが依存していない
IsMetalFullGpuFrameEligible内でbeginしない
SegmentedFrameCompleteはVBlank pathに残る
FrameActive=falseのとき未submit slotをcancelする
MetalRenderer::Initが必須stage failureでfalseを返す
古い"CPU output remains available"ログが存在しない
```

疑似audit:

```python
issues = []

if "BeginSegmentedSnapshotFrame" not in gpu2d_h:
    issues.append("explicit segmented snapshot frame begin API missing")

start_pos = full_gpu.find("void MetalRenderer::Start3DRendering")
begin_pos = full_gpu.find("BeginSegmentedSnapshotFrame", start_pos)
eligible_pos = full_gpu.find("IsMetalFullGpuFrameEligible", start_pos)

if begin_pos < 0 or eligible_pos < 0 or begin_pos > eligible_pos:
    issues.append("snapshot begin must occur before eligibility")

if "line != 0" in legacy_begin_body:
    issues.append("snapshot begin still depends exclusively on line 0")

if "CPU output remains available" in metal_mm:
    issues.append("stale CPU fallback initialization log remains")
```

## 10.2 audit runner

`tools/ci/audits/run-metal-fullgpu-audits.sh`へ追加する。

---

# 11. 実装commit分割

## Commit 1: bootstrap lifecycle

対象:

```text
GPU2D_Metal.h
GPU2D_MetalFullGpuMethods.inc
GPU_Metal.h
GPU_MetalFullGpuMethods.inc
```

内容:

- renderer-owned epoch
- explicit begin／cancel
- Start3DRendering順序修正
- line 0 begin依存撤廃
- slot lifecycle修正
- diagnostic reason

commit例:

```text
metal: prepare segmented snapshots before frame eligibility
```

## Commit 2: initialization fail propagation

対象:

```text
GPU_Metal.h
GPU_Metal.mm
renderer creation／frontend fallback箇所
```

内容:

- ConfigureMetal2DMirror bool化
- required init stage failureをreturn false
- stale CPU fallback log削除
- explicit session fallback／error

commit例:

```text
metal: fail renderer initialization when output pipelines are unavailable
```

## Commit 3: presenter stale splash clear

対象:

```text
MelonPrimeScreenMetal.mm
MelonPrimeScreenMetal.h
```

内容:

- emulation開始時のblack startup clear
- no-output error state
- state reset

commit例:

```text
metal: clear stale splash when emulation starts without output
```

## Commit 4: audits and evidence

対象:

```text
tools/ci/audits/audit-metal-frame-bootstrap.py
tools/ci/audits/run-metal-fullgpu-audits.sh
docs/plans/evidence/metal-frame-bootstrap-fix-2026-07-18.md
```

commit例:

```text
ci: gate Metal frame bootstrap ordering
```

---

# 12. ビルド

ローカル:

```zsh
cd /Users/admin/git/melonPrimeDS
git switch develop_metal
git pull --ff-only
```

Metal test build:

```zsh
./tools/build/macos/build-macos-metal-test.sh
```

または既存build directory:

```zsh
cmake --build build-mac-metal -j8
```

必須build:

```text
macOS ARM64 Metal ON
macOS Metal OFF
macOS FORCE_DISABLE_METAL
```

可能なら:

```text
macOS Intel
Windows
Linux
```

audit:

```zsh
bash tools/ci/audits/run-metal-fullgpu-audits.sh
```

---

# 13. 実ROM検証

## 13.1 最優先再現確認

設定:

```text
3D Renderer: Metal
Internal Resolution: 1x
VSync: ON
```

手順:

```text
1. アプリ起動
2. スプラッシュ表示確認
3. Metroid Prime Hunters ROMを開く
4. 5秒以内にDS画面へ切り替わることを確認
5. メニューまで進む
6. ゲームへ入る
7. 30秒以上操作
```

合格:

```text
スプラッシュ固定なし
黒画面固定なし
first native texture logあり
frame serial増加
DS上画面／下画面更新
音声だけ進む状態なし
```

## 13.2 Metal Compute

同じ手順をMetal Compute Shaderで実施する。

Compute側がfail-closedする場合、明示エラーが出ること。

## 13.3 scale

```text
1x
2x
4x
8x
```

ROM起動前後で変更する。

各scaleで新ProducerIdのfirst frameが出ること。

## 13.4 ROM version

最低限:

```text
US 1.0
US 1.1
EU 1.0
JP 1.0
KR 1.0
```

## 13.5 firmware／direct boot

- firmware boot
- direct boot
- ROM close→再open
- reset
- savestate load
- renderer switch

を確認する。

---

# 14. ログ付き検証

起動:

```zsh
MELONPRIME_METAL_DIAG=1 \
MELONPRIME_METAL_PERF=1 \
build-mac-metal/melonPrimeDS.app/Contents/MacOS/melonPrimeDS
```

期待ログ:

```text
metal renderer: initializing
metal 2d: configured
metal visible output: configured
metal full-gpu: enabled
metal display capture: configured
metal bootstrap: first frame activated
metal full-gpu: first segmented frame rendered
metal visible output: first compose
metal presenter: source texture ... visibleSource=MetalFinalTexture
metal frame: present frame=1
```

出てはいけないログ:

```text
ERROR no valid MetalTexture output produced within startup grace window
segment-snapshot-A-incomplete が永久継続
segment-snapshot-B-incomplete が永久継続
snapshot-A-reserve が永久継続
snapshot-B-reserve が永久継続
CPU output remains available
stable CPU compositor path remains active
```

---

# 15. stress test

## 15.1 renderer switch

```text
OpenGL
→ Metal
→ Metal Compute
→ Metal
→ Software
→ Metal
```

50 cycle。

合格:

- stale splashなし
- stale producer textureなし
- slot leakなし
- crashなし
- renderer init failure時に明示fallback

## 15.2 lifecycle

100 cycle:

```text
ROM open
ROM close
ROM open
reset
pause
resume
fullscreen
windowed
```

## 15.3 scale

```text
1x→2x→4x→8x→4x→2x→1x
```

100 cycle。

## 15.4 backpressure

意図的にpresenter／GPU負荷を上げる。

確認:

- ring slot一時不足で1frame retainは可
- 永久bootstrap failure不可
- BusyMaskが最終的に解放される
- same epochで二重解放なし
-次epochでretryできる

---

# 16. TSan／sanitizer

確認対象:

- `SegmentedCaptureRing`
- `SegmentedCaptureSlot`
- `SegmentedCaptureAttempted`
- `SnapshotFrameEpoch`
- valid arrays
- BusyMask
- command completion
- renderer reset
- ROM close
- renderer switch

合格:

```text
data race 0
use-after-free 0
double release 0
slot leak 0
BusyMask stuck 0
```

---

# 17. 受け入れ条件

## 17.1 BLOCKER解消

次をすべて満たす。

```text
ROM起動後にスプラッシュからDS画面へ切り替わる
first MetalTextureが60 presenter frame以内にpublishされる
frame serialが継続的に増える
Metal Rasterで表示
Metal Computeで表示、または明示的init failure
```

## 17.2 bootstrap contract

```text
snapshot beginはeligibilityより前
line 0はframe reservationを開始しない
renderer-owned epochを使用
A／B両方のslot予約成功後にFrameActive=true
VBlankで192 line completion確認
失敗時にslot解放
次frameでretry
```

## 17.3 fallback contract

```text
Software renderer再導入なし
CpuBgra presenter再導入なし
screenTex CPU upload再導入なし
Metal init failureは明示的
OpenGL session fallbackはsilentではない
```

## 17.4 output contract

6000 frame:

```text
MetalTexture presented > 0
None sustained = 0
startup grace failure = 0
cpuBgraPresented = 0
softwareFallback = 0
normalReadbackBytes = 0
```

capture-heavy gameplayでは別途capture correctnessを確認する。

---

# 18. 回帰防止チェックリスト

- [ ] `BeginSegmentedSnapshotFrame()`追加
- [ ] renderer-owned epoch追加
- [ ] `Start3DRendering()`でeligibility前にA／B begin
- [ ] line 0 only initialization撤廃
- [ ] current slot reservation失敗を検出
- [ ] FrameActive=false時に未submit slot解放
- [ ] VBlank early rejection時にslot leakなし
- [ ] `SegmentedFrameComplete()`維持
- [ ] Metal init必須stage failureで`false`
- [ ]古いCPU fallbackログ削除
- [ ] explicit renderer fallback／error
- [ ] stale splashをblack clear
- [ ] first output log
- [ ] bootstrap audit追加
- [ ] Metal OFF build
- [ ] FORCE_DISABLE build
- [ ] Metal Raster実ROM
- [ ] Metal Compute実ROM
- [ ] renderer switch
- [ ] ROM close／reopen
- [ ] scale change
- [ ] TSan
- [ ] 6000 frame counter

---

# 19. 修正後に追加で監査すべき点

今回のBLOCKER修正後も、次は別問題として残る。

1. Display Captureのpixel parity
2. same-frame capture feedbackの正確性
3. Metal Computeの実ROM互換性
4. Custom HUD本体のQPainter依存
5. release metallib bundle整合性
6. Intel Mac検証
7. Windows／Linux回帰
8. memory pressure
9. explicit CPU VRAM readback stall
10. output lease allocation

これらを今回のbootstrap修正commitへ混ぜない。

---

# 20. 最終指示

今回の修正で優先するのは完全Metal化の項目数ではなく、最初の有効frameを確実に開始し、最後までpublishするframe lifecycleの成立である。

正しい順序:

```text
frame epoch発行
→ snapshot slot予約
→ eligibility
→ scanline snapshot
→ completion確認
→ segment render
→ capture
→ final compose
→ slot publish
→ output lease
→ presenter
```

現在のような次の順序は禁止する。

```text
snapshot readyを確認
→ readyならframe開始
→ frame開始後にsnapshotを準備
```

> **snapshotを準備できたframeだけを開始するのではなく、frame開始判定の前段階としてsnapshot準備を明示的に実行し、その結果を含めてframeを開始すること。**

この修正が実ROMで確認できるまで、`develop_metal`を完全Metal化完了扱い、release可能扱い、macOS既定rendererとして安定扱いしてはならない。
