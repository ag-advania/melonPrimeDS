# melonPrimeDS `develop_metal` 完全Metal化 第4回監査報告

**監査日:** 2026-07-17  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `develop_metal`  
**監査HEAD:** `c432e062f9cb355f8d2bfa34b0e5c76b4bc314fa`  
**直前監査HEAD:** `2fbca5597cc81699b6dc75abb757de8d97aa9772`  
**最新コミット:** `metal: fix scale-reconfigure lease deadlock and output ordering`  
**差分:** 1コミット、実装4ファイル＋前回監査文書  
**developとの差:** 9コミット先行、0コミット遅延  
**監査方式:** GitHub上の最新HEADに対する静的差分監査  
**実行上の制約:** macOS実機build、Metal Validation、Thread Sanitizer、GPU Frame Capture、実ROMプレイは本監査では再実行していない。対象HEADに紐づくGitHub Actions workflow runおよびcombined statusも確認できなかった。

---

# 1. 総合結論

## 1.1 判定

> **不合格／マージ・リリース停止。**

前回監査でBLOCKERだった、内部解像度変更時の`PresenterRefCount`循環待ちは、既存`MetalOutputState`をin-place再構成せず、新stateを構築して`shared_ptr`をswapする方式へ変更されている。

この方向は正しく、前回の永久待機は解消されている。

また、次も修正されている。

- same producer内のGeneration rollback拒否
- same producer／generation内のFrameSerial rollback拒否
- `RendererOutputLease::ReleaseNow()`後のstale Output消去
- CPU／GPU capture writerのin-flight state分離
- GPU capture destination layerのdeduplicate
- 8x staging memoryコメントの計算修正

しかし、immutable state swapを実装した一方、swap対象である次のmemberは通常の`std::shared_ptr`のままである。

```cpp
std::shared_ptr<MetalOutputState> OutputState;
```

renderer／emulation側では:

```cpp
std::exchange(OutputState, std::move(next));
```

presenter側では:

```cpp
state = OutputState;
```

が別threadから実行される。

`std::shared_ptr`のcontrol blockは、別々の`shared_ptr` objectに対する操作ならthread-safeである。しかし、**同じ`shared_ptr` objectを一方のthreadが書き、別threadが同期なしで読むことはdata raceであり未定義動作**である。

さらに`GetOutput()`は、同じfunction内で`OutputState`を複数回読み直しており、pointer swapが間に入ると:

```text
old stateのMutexをlock
→ new stateのfieldsをold Mutexで保護したつもりで読む
```

という状態にもなり得る。

したがって、前回の論理deadlockは修正されたが、同じscale変更経路にthread-safety上のrelease blockerが残る。

## 1.2 最終評価

正確な評価:

> **前回指摘の大部分は修正済み。ただしOutputStateのpublicationがatomicでなく、scale変更・renderer再構成とpresenter取得が重なった場合にC++ data raceが発生する。現HEADは静的監査上、受け入れ不可。**

---

# 2. 最新差分

直前HEAD `2fbca559`から最新HEAD `c432e062`まで:

| ファイル | 追加 | 削除 | 主な変更 |
|---|---:|---:|---|
| `docs/plans/melonPrimeDS_develop_metal_完全Metal化_再々監査報告_2026-07-17.md` | 980 | 0 | 前回監査報告 |
| `src/GPU.h` | 6 | 0 | lease release後のOutput消去 |
| `src/GPU_Metal.mm` | 148 | 118 | immutable OutputState swap |
| `src/GPU_MetalCaptureMethods.inc` | 111 | 51 | CPU／GPU writer分離、deduplicate |
| `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm` | 59 | 12 | generation／serial rollback拒否 |

実装コードのみ:

```text
追加 324行
削除 181行
```

---

# 3. 前回指摘の対応状況

## 3.1 scale変更循環待ち

**前回:** BLOCKER  
**今回:** 修正済み

以前:

```cpp
OutputState->Ready = false;
OutputState->PublishedSlot = -1;
OutputState->Completion.wait(lock, [] {
    return PresenterRefCount == 0 &&
           InFlightCount == 0;
});
```

この方式ではpresenterが`lastGoodMetalLease`を保持し続けるため、永久待機した。

現在:

```cpp
auto next = std::make_shared<MetalOutputState>();
```

新stateへtexture／pipeline／metadataを完全構成した後:

```cpp
std::shared_ptr<MetalOutputState> previous =
    std::exchange(OutputState, std::move(next));
```

としている。

旧stateは:

- presenter lease
- command completion block
- local shared ownership

によって自然に生存し、最後の参照が解放された時点で破棄される。

この設計変更は妥当であり、前回の循環待ちは解消している。

## 3.2 Generation rollback

**前回:** MEDIUM  
**今回:** 修正済み

same producerかつ:

```cpp
output.Generation < lastGoodGeneration
```

の場合、incoming leaseを解放してlast-known-goodを維持する。

rollback outputをtracker reset後に再受理していた前回の問題は解消した。

## 3.3 FrameSerial rollback

**前回:** MEDIUM  
**今回:** 修正済み

same producer／same generationかつ:

```cpp
output.FrameSerial < lastGoodFrameSerial
```

を拒否する。

同値は同じpublished frameの再presentとして許可し、増加は新frameとして許可する。

このordering contractは妥当。

## 3.4 lease release後のstale Output

**前回:** LOW  
**今回:** 修正済み

`RendererOutputLease::ReleaseNow()`は現在:

```cpp
Context = nullptr;
ReleaseFn = nullptr;
Output = {};
```

を実行する。

move元のleaseも`Output = {}`にするため、解放済みtexture pointer／metadataの誤用リスクは低減した。

## 3.5 CPU／GPU capture writer分離

**前回:** MEDIUM  
**今回:** 部分修正

以前:

```cpp
bool UploadInFlight;
uint64_t SubmittedSerial;
```

現在:

```cpp
bool CpuUploadInFlight;
bool GpuCaptureInFlight;
uint64_t CpuSubmittedSerial;
uint64_t GpuSubmittedSerial;
```

CPU writerとGPU writerが互いのin-flight flagを上書きしないようになった。

CPU completion:

```text
GpuCaptureInFlightならValidをclaimしない
```

GPU completion:

```text
CpuUploadInFlightならValidをclaimしない
```

同一renderer queueのsubmission orderingと組み合わせて、以前より明確なwriter ownershipになっている。

ただし、GPU writer自体が複数command in-flightになった場合をbool 1個では表現できない。これは後述H-01。

## 3.6 GPU capture pendingWrites deduplicate

**前回:** MEDIUM  
**今回:** 改善

同じdestination layerが192 scanline分vectorへ重複していた問題に対し:

```cpp
bool layerSeen[16] = {};
```

を追加し、layer単位にdeduplicateしている。

同じlayerが再度現れた場合は既存entryのsizeを更新し、最後のenabled scanlineを採用する。

allocation／completion iterationの無駄は減った。

ただし、mid-frameで同じlayerのcapture size／modeが変化する場合、単一layer metadataでは因果を完全表現できない。M4 per-segment実装時に再設計が必要。

## 3.7 staging memoryコメント

**前回:** LOW  
**今回:** 修正済み

現在のコメント:

```text
256@8x  = 2048x2048x4 = 16 MiB
256@16x = 4096x4096x4 = 64 MiB
```

へ修正されている。

---

# 4. BLOCKER

## B-01: `OutputState` shared_ptr publication data race

**重大度:** BLOCKER  
**対象:**

- `src/GPU_Metal.h`
- `src/GPU_Metal.mm`
- presenterから呼ばれる`AcquireOutputLease()`／`GetOutput()`

## 4.1 問題の構造

member:

```cpp
std::shared_ptr<MetalOutputState> OutputState;
```

writer:

```cpp
std::exchange(OutputState, std::move(next));
```

reader:

```cpp
std::shared_ptr<MetalOutputState> state = OutputState;
```

writerは内部解像度変更／output再構成を行うrenderer側thread。

readerは`ScreenPanelMetal::drawScreen()`から:

```cpp
nds->GPU.AcquireRendererOutputLease();
```

を経由して呼ばれるpresenter側thread。

同じ`std::shared_ptr` instanceへの同期なしread／writeは未定義動作。

control blockのreference countがatomicであることは、このmember object自体の同時read／writeを合法化しない。

## 4.2 concrete race

### renderer thread

```cpp
std::shared_ptr<MetalOutputState> previous =
    std::exchange(OutputState, std::move(next));
```

### presenter thread

```cpp
if (OutputState)
{
    std::shared_ptr<MetalOutputState> state = OutputState;
}
```

以下が起こり得る。

- shared_ptr内部pointer／control-block pointerのtear
- old control blockのrefcountを正しくincrementできない
- freed stateの参照
- new state pointerとold control blockの組み合わせ
- random crash
- scale変更時のみ再現
- optimization buildだけで再現
- Thread Sanitizer data race

## 4.3 `GetOutput()`はさらに危険

現在の形:

```cpp
if (OutputState)
{
    std::lock_guard<std::mutex> lock(OutputState->Mutex);

    if (OutputState->Ready &&
        OutputState->PublishedSlot >= 0)
    {
        ...
    }
}
```

`OutputState`を毎回読み直している。

swap timing:

```text
1. if(OutputState)でold state確認
2. lock(OutputState->Mutex)でold Mutexをlock
3. rendererがOutputStateをnew stateへswap
4. if(OutputState->Ready)でnew stateを読む
```

結果:

```text
old stateのMutexをlock
new stateのfieldsを読む
```

new stateのfieldsはold Mutexでは保護されない。

これはshared_ptr objectのdata raceとは別に、snapshotを固定していないlogic bugでもある。

## 4.4 必須修正

### C++17互換案

`std::shared_ptr`向けfree atomic operationを使用する。

load:

```cpp
std::shared_ptr<MetalOutputState> LoadOutputState() const
{
    return std::atomic_load_explicit(
        &OutputState,
        std::memory_order_acquire);
}
```

exchange:

```cpp
std::shared_ptr<MetalOutputState> previous =
    std::atomic_exchange_explicit(
        &OutputState,
        std::move(next),
        std::memory_order_acq_rel);
```

store null:

```cpp
std::atomic_store_explicit(
    &OutputState,
    std::shared_ptr<MetalOutputState>{},
    std::memory_order_release);
```

### C++20案

build standardがC++20以上で全target compilerが対応する場合:

```cpp
std::atomic<std::shared_ptr<MetalOutputState>> OutputState;
```

ただし既存Windows／Linux toolchainとの互換性を確認すること。

## 4.5 全accessを一度だけsnapshotする

修正対象:

- `ConfigureMetalVisibleOutput()`
- `ConfigureMetalCaptureState()`
- `CaptureMetalVisible3DFrame()`
- `ComposeMetalVisibleOutput()`
- `AcquireOutputLease()`
- `GetOutput()`
- destructor／renderer shutdown
- Full-GPU compose helper内の参照

正しい形:

```cpp
std::shared_ptr<MetalOutputState> state = LoadOutputState();
if (!state)
    return;

std::lock_guard<std::mutex> lock(state->Mutex);
```

禁止:

```cpp
if (OutputState)
{
    lock(OutputState->Mutex);
    read(OutputState->...);
}
```

## 4.6 受け入れ条件

Thread Sanitizerで:

```text
OutputState pointer publication race 0
scale変更中AcquireOutputLease race 0
GetOutput state mismatch 0
renderer switch中race 0
shutdown中race 0
```

stress:

```text
1x→2x→4x→8x→16x→8x→4x→2x→1x
```

を1000回。

同時に:

- fullscreen toggle
- fast-forward
- VSync toggle
- pause／resume
- HUD／OSD
- savestate
- renderer Metal↔Metal Compute
- ROM close／open

を実行する。

---

# 5. HIGH

## H-01: `GpuCaptureInFlight`が複数GPU commandを表現できない

**対象:** `GPU_MetalCaptureMethods.inc`

現在:

```cpp
bool GpuCaptureInFlight;
uint64_t GpuSubmittedSerial;
```

`EncodeMetalDisplayCapture()`は、前のGPU capture commandが未完了でも新commandをsubmitできる。

新command時:

```cpp
meta.GpuCaptureInFlight = true;
meta.GpuSubmittedSerial = meta.DirtySerial;
```

古いcommand completion時:

```cpp
meta.GpuCaptureInFlight = false;
```

したがって:

```text
GPU capture A submit
GPU capture B submit
A completion
    → GpuCaptureInFlight=false
Bはまだ実行中
```

というmetadataになり得る。

その間にCPU側dirtyが発生すると、CPU upload gateが:

```cpp
!meta.GpuCaptureInFlight
```

を満たし、Bがまだin-flightでもCPU uploadを開始できる。

同一queue orderingによりtexture write順は維持される可能性が高いが、metadataは実際のin-flight command数を表していない。

さらにA／Bが同じ`DirtySerial`を使うため、completionの新旧も判別できない。

## H-01修正案

GPU dispatchごとにmonotonic tokenを発行する。

```cpp
uint64_t NextGpuWriteToken = 1;
uint64_t LatestGpuSubmittedToken = 0;
uint32_t GpuWritesInFlight = 0;
```

dispatch:

```cpp
const uint64_t token = NextGpuWriteToken++;
meta.LatestGpuSubmittedToken = token;
meta.GpuWritesInFlight++;
```

completion:

```cpp
meta.GpuWritesInFlight--;

if (token != meta.LatestGpuSubmittedToken)
{
    // 古いcompletion。in-flight countだけ減らし、Validを変更しない。
    return;
}
```

latest commandだけが`Valid／Dirty`をfinalizeする。

CPU gate:

```cpp
meta.GpuWritesInFlight == 0
```

M4実装時にはframe／segment serialもtokenへ含める。

---

## H-02: immutable state swapによる高解像度memory peak

前回のdeadlockを避けるため、旧stateを保持したまま新stateを完全allocateする方式になった。

lifetimeとしては正しいが、8x／16xでは大きな一時memory spikeが発生する。

## H-02.1 CaptureState

scale 16:

```text
Capture128   256 MiB
Snapshot128  256 MiB
Capture256   256 MiB
Snapshot256  256 MiB
--------------------
合計          1 GiB
```

scale 8:

```text
合計          256 MiB
```

8x→16x reconfigureで旧8x＋新16xが共存すると:

```text
1.25 GiB
```

## H-02.2 OutputState

scale 16:

```text
CapturedHigh3D       48 MiB
CapturedLow3D       約0.19 MiB
FinalTexture 3 slot 288 MiB
CpuComposite 3 slot 約1.13 MiB
----------------------------
約337.3 MiB
```

scale 8:

```text
約85.3 MiB
```

8x→16x overlap:

```text
約422.6 MiB
```

CaptureStateとOutputStateだけで:

```text
約1.70 GiB
```

が一時共存し得る。

ここに:

- 3D color／depth／attribute
- Metal Compute span／tile buffers
- 2D layer textures
- texture cache
- presenter drawable
- CPU scratch
- one-off staging buffer
- Qt images

が追加される。

8 GB Intel／Apple Silicon unified-memory環境ではmemory pressure、allocation failure、OSによるterminationのリスクがある。

## H-02修正案

優先順:

1. capture textureをnative sizeで保持
2. sampling時にscale変換
3. CPU upload stagingをRGB5551 native size化
4. Metal computeでdecode／scale expansion
5. scale変更前にmemory budgetを計算
6. 新state allocation失敗時に旧stateを維持
7. 8x／16xをdevice memory条件で制限
8. old/new overlap telemetry追加
9. `recommendedMaxWorkingSetSize`等を利用可能範囲で参照
10. memory pressure時は段階的scale fallback

---

# 6. MEDIUM

## M-01: `ValidateMetalRendererOutput()`がpixel formatを検証しない

現在検証するもの:

- device
- texture type
- array length
- Width／Height
- Scale
- Generation
- FrameSerial
- ProducerId

未検証:

```cpp
texture.pixelFormat
```

producer contractは:

```text
MTLPixelFormatBGRA8Unorm
```

である。

異なるpixel formatを受理すると、shader samplingの色／alpha contractが崩れる。

追加:

```cpp
if (texture.pixelFormat != MTLPixelFormatBGRA8Unorm)
    return fail("texture.pixelFormat != BGRA8Unorm");
```

必要に応じて:

- sampleCount == 1
- mipmapLevelCount == 1
- depth == 1

も確認する。

---

## M-02: old `MetalOutputState` destructorのblocking wait

destructor:

```cpp
Completion.wait(lock, [this]() {
    return InFlightCount == 0 &&
           PresenterRefCount == 0;
});
```

shared ownershipのinvariantが正しければ:

```text
InFlightCount > 0
    → completion blockがshared_ptrを保持

PresenterRefCount > 0
    → LeaseContextがshared_ptrを保持
```

となるため、最後のshared_ptrが消えてdestructorへ到達する時点で、通常countは0である。

つまりwaitは原則不要。

count accountingにbugが発生した場合は、最後の参照を解放するthreadが永久待機する。

特にrelease mismatch branch:

```cpp
if (generation／serial一致)
    count--;
else
    log only;
```

ではcountを残すため、invariant破損時にdestructorが停止する。

推奨:

- destructorはassert／diagnosticのみ
- explicit `Retire()`／`Drain()`を管理threadで行う
- destructorで無期限waitしない
- mismatch時のcount recovery policyを定義
- strict buildではabort可能
- productionではleakよりhangを避ける設計を検討

---

## M-03: `GetOutput()`はleaseなしraw textureを返す

`AcquireOutputLease()`はring slotを保持する。

一方`GetOutput()`は:

```cpp
RendererOutput::MetalTexture(rawTexture, ...)
```

をleaseなしで返す。

consumerがraw textureを非同期利用すると:

- slot reuse
- state swap
- old state destruction

と競合し得る。

現presenterはlease APIを使用しているが、将来のconsumer／debug pathが`GetOutput()`を使う可能性がある。

方針:

1. MetalTexture consumerはlease API必須
2. `GetOutput()`はMetal時`None`またはdiagnostic専用
3. raw APIにはsynchronous-use限定を明記
4. static analysis／assertで非lease consumerを検出

---

## M-04: producer変更時にold last-goodをnew output validation前に破棄

現在:

```text
ProducerIdが変化
→ old leaseをReleaseNow()
→ new outputをValidate
```

new producerの最初のoutputがinvalidだった場合:

- old known-goodは既に破棄
- new outputは拒否
- CpuBgraが同frameにない場合は何も表示できない

stale old producer textureを長期表示しない方針は妥当だが、transitionの1frame blank／returnを避けたい場合は:

```text
new outputを先にshape／device validation
→ validならold lease release
→ new lease採用
```

とする。

ただしscale変更後のold textureを新sessionで表示してよいか、明示的policyが必要。

安全優先なら現状維持でもよい。

---

## M-05: rollback後のinvalid metadata logが誤解を招く

rollback branch:

```cpp
rendererOutputLease.ReleaseNow();
```

で`Output = {}`にする。

その後同じflowで:

```cpp
if (!Context)
    validation skip
else if (!loggedInvalidOutputMetadata)
    log invalid output
```

となるため、rollback rejectを既に専用logした後、条件によってはgeneric invalid output logが`unknown`理由で出る可能性がある。

rollback reject後は明示的なlocal flag:

```cpp
bool rejectedByOrdering = true;
```

でgeneric validation logをskipする。

---

## M-06: GitHub Actions／status checkなし

最新HEAD:

```text
c432e062f9cb355f8d2bfa34b0e5c76b4bc314fa
```

について:

```text
workflow_runs: []
combined statuses: []
```

を確認した。

最低限必要:

- macOS ARM64 Metal ON
- macOS x86_64 Metal ON
- macOS Metal OFF
- macOS FORCE_DISABLE
- Windows
- Linux

特に今回はconcurrency／Objective-C++／shared_ptr lifecycleを変更しているため:

- Release build
- Debug build
- Thread Sanitizer
- Address Sanitizer
- UndefinedBehaviorSanitizer

のうち実行可能なmatrixを追加する。

---

# 7. LOW

## L-01: `lastPresentedFrame`がProducerId変更時にresetされない

tracking:

```cpp
lastGoodProducerId
lastGoodGeneration
lastGoodFrameSerial
```

はresetされるが:

```cpp
lastPresentedFrame
```

はresetされない。

新producerのfirst serialが旧producerのlast serialと同じ場合、first-frame diagnosticが出ない。

表示correctnessには影響しないが、監査logが欠落する。

ProducerId変更時:

```cpp
lastPresentedFrame = 0;
```

を追加する。

## L-02: GPU capture deduplicateが線形探索

`layerSeen[16]`を使う一方、重複layer更新時に`pendingWrites`を線形探索する。

最大16 layerなので性能影響は小さい。

より明確には:

```cpp
std::array<int, 16> pendingIndex;
pendingIndex.fill(-1);
```

を使用する。

## L-03: one-off 64 MiB upload allocation

16x CPU fallback captureはpersistent ring上限を超えるため:

```cpp
newBufferWithBytes(... 64 MiB ...)
```

を実行する。

correctness fallbackとしては機能するが、Display Capture頻度が高い場合:

- allocation churn
- memory pressure
- copy cost
- CPU scale² loop

が残る。

native-size staging＋GPU expansionまで暫定実装扱いとする。

---

# 8. 完全Metal化フェーズ評価

## M0 診断

**概ね完了**

不足:

- CI自動strict mode
- state swap telemetry
- memory telemetry
- writer token telemetry

## M1 output contract

**大幅改善、未完了**

完了:

- Width／Height／ArrayLength／Scale
- Generation
- FrameSerial
- ProducerId
- fail-closed validation
- lease
- immutable state swap
- ordering rollback拒否

未完了:

- OutputState atomic publication
- pixel format validation
- raw GetOutput整理
- destructor blocking wait整理

## M2 shared device

**完了**

renderer／presenterはprocess-wide shared `MTLDevice`を使用。

## M3 Full-GPU 2D default

**適格フレームで完了**

## M4 Display Capture

**未完了**

same-frame capture feedbackをper-line／per-segment GPU因果として完結していない。

## M5 readback 0

**未完了**

- capture CPU fallback
- on-demand readback
- SoftRenderer GetLine
- HUD CPU framebuffer

が残る。

## M6 SoftRenderer撤廃

**未実装**

```cpp
class MetalRenderer : public SoftRenderer
```

のまま。

## M7 Compute独立化

**未実装**

```cpp
MetalRenderer3D RasterReference;
```

を保持。

## M8 presenter texture-only

**部分完了**

残る:

- CpuBgra production fallback
- raw GetOutput
- OutputState publication race
- screenTex CPU upload
- HUD CPU buffer

## M9 HUD／OSD Metal化

**未実装**

現在も:

- `QImage`
- `QPainter`
- CPU OSD bitmap
- bottom framebuffer memcpy
- `replaceRegion`

を使用。

## M10 macOS初回既定

**未実装**

現在の既定:

```text
3D.Renderer = OpenGL
Screen.UseGL = true
```

## M11 shader asset分離

**未実装**

embedded MSLのまま。

---

# 9. 推奨修正順序

## PR-A: OutputState atomic publication

変更対象:

- `GPU_Metal.h`
- `GPU_Metal.mm`

実装:

1. atomic shared_ptr load helper
2. atomic exchange helper
3. 全readerをsingle snapshot化
4. `GetOutput()`のpointer再読込撤廃
5. shutdown／resetもatomic store
6. TSan test

これだけを独立PRにする。

## PR-B: GPU writer token

1. `GpuWritesInFlight`
2. monotonically increasing `GpuWriteToken`
3. latest-token-only finalize
4. CPU gateをcount==0化
5. callback order fault injection
6. command error ordering test

## PR-C: memory reduction

1. native RGB5551 staging
2. GPU decode
3. GPU scale expansion
4. one-off巨大buffer撤廃
5. capture texture native化検討
6. 8x／16x memory budget
7. old/new overlap telemetry

## PR-D: output contract finish

1. pixelFormat validation
2. raw `GetOutput()`整理
3. destructor wait撤廃
4. producer transition policy
5. diagnostics reset

---

# 10. 必須テスト

## 10.1 Thread Sanitizer

対象:

```text
OutputState
MetalOutputState::Slots
PresenterRefCount
InFlightCount
PublishedSlot
PublishedSerial
CaptureState::Meta
CPU／GPU writer flags
```

操作:

- scale変更
- renderer切替
- fullscreen
- VSync
- pause
- fast-forward
- savestate
- ROM close
- shutdown
- multi-instance

合格:

```text
data race 0
deadlock 0
use-after-free 0
```

## 10.2 scale stress

1000 cycle:

```text
1x→2x→4x→8x→16x→8x→4x→2x→1x
```

記録:

- ProducerId
- state pointer
- PresenterRefCount
- InFlightCount
- RSS
- Metal allocated size
- frame time
- fallback count
- skipped present count

## 10.3 output ordering fault injection

拒否確認:

- Generation rollback
- FrameSerial rollback
- ProducerId 0
- Generation 0
- FrameSerial 0
- wrong array length
- wrong size
- wrong device
- wrong pixel format
- nil texture

## 10.4 GPU callback ordering

意図的に複数capture commandをin-flightにし:

```text
A submit
B submit
A complete
B complete
```

およびcallback dispatchを遅延させる。

合格:

- older completionがnewer stateをfinalizeしない
- in-flight countが正しい
- CPU uploadが早期開始しない
- latest writerだけがValidを確定

## 10.5 memory

scale 1／2／4／8／16:

- steady-state
- 8→16
- 16→8
- renderer Metal→Compute
- ROM restart
- capture active scene

でpeakを記録。

---

# 11. 修正禁止事項

1. atomic publicationの代わりにraw pointerへ戻さない。
2. data race回避のためglobal emulation lockでpresenterを長時間停止しない。
3. scale変更時にGUI threadへ同期invokeしない。
4. old state textureをleaseなしで保持しない。
5. PresenterRefCountを強制0へ変更しない。
6. `waitUntilCompleted`を通常present pathへ追加しない。
7. GPU writer overlapをboolだけで隠さない。
8. memory問題をone-off allocation増加で隠さない。
9. CPU fallbackを完全Metal完了と判定しない。
10. Software／OpenGL既存経路を壊さない。
11. melonPrime外の既存コード変更では`MELONPRIME_DS` guardを維持する。
12. Metal固有変更ではMetal build guardを維持する。

---

# 12. チェックリスト

## 前回指摘

- [x] scale変更循環待ち解消
- [x] immutable OutputState swap
- [x] ProducerId変更
- [x] Generation rollback拒否
- [x] FrameSerial rollback拒否
- [x] lease release後Output消去
- [x] CPU／GPU writer flag分離
- [x] pendingWrites deduplicate
- [x] 8x memoryコメント修正
- [ ] OutputState atomic publication
- [ ] GPU writer multi-in-flight token
- [ ] high-scale memory削減
- [ ] pixel format validation
- [ ] CI／TSan

## 完全Metal化

- [ ] Display Capture Full-GPU
- [ ] normal readback 0
- [ ] SoftRenderer継承撤廃
- [ ] RasterReference撤廃
- [ ] CpuBgra production fallback撤廃
- [ ] raw GetOutput撤廃
- [ ] HUD CPU framebuffer撤廃
- [ ] QPainter通常frame撤廃
- [ ] Metal初回既定
- [ ] metallib asset化
- [ ] macOS ARM64／Intel受け入れ
- [ ] Windows／Linux回帰なし

---

# 13. 最終判定

最新pushは、前回監査の主要指摘へ正面から対応している。

特に:

- immutable state swap
- ordering rollback拒否
- lease Output消去
- CPU／GPU writer分離
- capture layer deduplicate

は有効な改善である。

前回のscale変更永久待機は静的コード上、解消した。

しかしstate swapの公開先が通常の`std::shared_ptr`であり、renderer writeとpresenter readをatomic／mutexで同期していない。

> **論理deadlockは修正されたが、state publication data raceへ置き換わっている。**

したがって現HEADはまだ受け入れ不可。

次のpushでは、まず`OutputState`のatomic shared_ptr publicationと全readerのsingle snapshot化だけを行い、Thread Sanitizerでscale変更stressを通すこと。その後、GPU writer tokenと高解像度memory削減へ進むこと。
