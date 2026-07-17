# melonPrimeDS `develop_metal` 完全Metal化 再々監査報告

**監査日:** 2026-07-17  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `develop_metal`  
**監査HEAD:** `2fbca5597cc81699b6dc75abb757de8d97aa9772`  
**直前HEAD:** `14c814b497d2a0e97152053e5bc26c1c7b04de1c`  
**差分:** 1コミット、コード5ファイル＋監査文書1ファイル  
**コミット:** `metal: fix capture metadata races and presenter lease lifecycle`  
**監査方式:** GitHub上のHEADソースに対する静的差分監査  
**実行上の制約:** 本監査ではmacOS実機ビルド、Metal Validation、Thread Sanitizer、GPU Frame Capture、実ROMプレイを再実行していない。GitHub connector上でも対象HEADに紐づくworkflow runおよびstatus checkは確認できなかった。

---

# 1. 結論

## 1.1 総合判定

> **不合格／現HEADのマージ・リリース停止。**

前回監査で指摘した次の問題は、コード上では概ね修正されている。

- `GPU_MetalCaptureMethods.inc`の文字列リテラル破損
- `CaptureMeta`のcompletion thread／renderer thread間データ競合
- upload完了前の毎フレーム再送livelock
- stale completionによる新しいdirty状態の上書き
- GPU capture command失敗時の誤った`Valid=true`
- persistent staging ringの無制限拡大
- Metal output metadataのfail-open検証
- producer identityの欠如
- retained textureのROM／renderer跨ぎ
- Compute renderer説明文の不一致

しかし、今回のoutput lease lifecycle修正により、**内部解像度変更時に循環待ちが発生する設計**になっている。

循環:

```text
presenter:
    lastGoodMetalLeaseをmemberとして保持
    → OutputState::PresenterRefCountが常時1以上

renderer:
    scale変更
    → ConfigureMetalVisibleOutput()
    → Ready=false
    → PresenterRefCount==0を待つ

presenter:
    Ready=falseなのでAcquireOutputLease()はCpuBgraを返す
    → CpuBgra branchではlastGoodMetalLeaseを解放しない

結果:
    rendererはPresenterRefCount==0にならない
    presenterは新generationのMetalTextureを受け取れない
    → 永久待機
```

これはユーザーがVideo Settingsで内部解像度を変更する通常操作で発生し得るため、BLOCKERと判定する。

## 1.2 現在の完全Metal化判定

今回の修正を含めても、完全Metal化は未達である。

```text
通常のFull-GPU適格フレーム:
    Metal 3D／Metal Compute
    → Metal segmented 2D
    → Metal final texture array
    → CAMetalLayer

Display Capture／不適格フレーム:
    Metal 3D readback
    → SoftRenderer 2D
    → CpuBgra
    → Metal presenterへupload

HUD／OSD:
    QImage／QPainter
    → CPU bitmap
    → Metal UI textureへpartial upload

Metal Compute失敗／不適格:
    Metal raster RasterReference
```

## 1.3 判定要約

| 項目 | 判定 |
|---|---|
| 前回の文字列リテラルBLOCKER | **修正** |
| CaptureMeta mutex化 | **合格** |
| Dirty／UploadInFlight state machine | **概ね合格** |
| CPU upload stale completion対策 | **合格** |
| GPU capture completion検証 | **合格** |
| staging ring上限 | **部分合格** |
| output metadata fail-closed化 | **概ね合格** |
| ProducerId導入 | **合格** |
| retained lease lifecycle | **不合格／新規BLOCKER** |
| scale変更 | **デッドロックリスク** |
| Generation rollback拒否 | **不合格** |
| FrameSerial rollback拒否 | **不合格** |
| capture memory総量 | **高負荷リスク残存** |
| M4 Display Capture GPU常駐 | **未実装** |
| M6 SoftRenderer撤廃 | **未実装** |
| M7 Compute独立化 | **未実装** |
| M9 HUD／OSD Metal化 | **未実装** |
| CI／macOS build evidence | **なし** |

---

# 2. 最新コミット差分

直前HEAD `14c814b4`から最新HEAD `2fbca559`までの変更:

| ファイル | 追加 | 削除 | 内容 |
|---|---:|---:|---|
| `docs/plans/melonPrimeDS_develop_metal_完全Metal化_再監査報告_2026-07-17.md` | 1205 | 0 | 前回再監査書 |
| `src/GPU.h` | 10 | 1 | `ProducerId`、lease release補強 |
| `src/GPU_Metal.mm` | 13 | 2 | output producer identity |
| `src/GPU_MetalCaptureMethods.inc` | 256 | 66 | mutex、Dirty／InFlight、GPU completion、memory上限 |
| `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm` | 93 | 35 | fail-closed validation、last-good lifecycle |

実装コードのみでは約372行追加、104行削除である。

---

# 3. 前回指摘の対応状況

## 3.1 文字列リテラル破損

**前回:** BLOCKER  
**今回:** 修正済み

前回の実改行は、現在`"\n"`へ戻されている。

確認対象:

- shader compile failure
- capture pipeline failure
- readback pipeline failure
- allocation failure
- capture configured
- first GPU capture
- staging fallback
- CPU fallback upload failure
- CPU upload submitted
- incomplete sync
- on-demand readback

静的ソース上では構文破損は解消している。ただし対象HEADのCI runがないため、実際のClang compile成功は別途確認が必要である。

## 3.2 CaptureMetaのデータ競合

**前回:** HIGH  
**今回:** 修正済みと判定

追加されたstate:

```cpp
struct CaptureMeta
{
    int Size = -1;
    bool Valid = false;
    bool Dirty = false;
    bool UploadInFlight = false;
    uint64_t DirtySerial = 0;
    uint64_t SubmittedSerial = 0;
};

std::mutex MetaMutex;
```

mutex対象:

- reconfigure時のmetadata snapshot
- `CaptureMetalDisplayCaptureLine()`
- GPU capture dispatch／completion
- `AllocCapture()`
- CPU upload dispatch／completion
- `SyncVRAMCapture()`
- `MetalCaptureResourcesCoherent()`

`MetalCaptureResourcesCoherent()`は、一度lockして全16 layerをsnapshotする。この修正は妥当である。

## 3.3 upload再送livelock

**前回:** HIGH  
**今回:** 修正済みと判定

現在のdispatch条件:

```cpp
if (!meta.Dirty ||
    meta.UploadInFlight ||
    meta.Size < 0)
{
    continue;
}
```

dispatch時:

```cpp
meta.UploadInFlight = true;
meta.SubmittedSerial = meta.DirtySerial;
```

completion時:

```cpp
meta.UploadInFlight = false;

if (!ok)
{
    meta.Valid = false;
    meta.Dirty = true;
}
else if (meta.DirtySerial == submittedSerial)
{
    meta.Valid = true;
    meta.Dirty = false;
}
else
{
    meta.Valid = false;
    meta.Dirty = true;
}
```

GPUが1フレーム以上遅延しても、同一dirty generationを毎フレーム再送しない。

## 3.4 GPU capture command failure

**前回:** MEDIUM  
**今回:** 修正済みと判定

以前はGPU capture commandをcommitした直後に`Valid=true`としていた。

現在はdestination layer、size、SubmittedSerialを収集し、command completionのsuccessかつserial一致の場合だけ`Valid=true`にする。failure／serial不一致は`Dirty=true`に戻す。

## 3.5 staging ring memory

**前回:** MEDIUM  
**今回:** 部分修正

変更:

```cpp
kStagingRingSize = 4
kMaxCaptureStagingBytes = 16 MiB
```

persistent ring上限は64 MiBへ縮小された。

ただし16 MiBを超えるuploadはone-off `MTLBuffer`へfallbackするため、capture全体のmemoryはboundedではない。

## 3.6 output metadata validation

**前回:** MEDIUM  
**今回:** 概ね修正

現在の必須検証:

- texture non-null
- device一致
- `MTLTextureType2DArray`
- texture array length == 2
- output array length == 2
- Width／Height／Scale／Generation／FrameSerial／ProducerIdが非0
- metadata sizeとtexture size一致
- Width == 256 × Scale
- Height == 192 × Scale

前回のfail-open holeは閉じられている。

残る問題:

- Generation rollbackを拒否しない
- FrameSerial rollbackを拒否しない
- 同一producer／generationの古いtextureを拒否しない

---

# 4. BLOCKER

## B-01: scale変更時のretained lease循環待ち

**重大度:** BLOCKER  
**影響:** 内部解像度変更、output texture再構成時の永久待機  
**対象:**

- `src/GPU_Metal.mm`
- `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`

## 4.1 producer側

`ConfigureMetalVisibleOutput()`は、scale／sizeが変わると既存の同じ`MetalOutputState`を再構成する。

```cpp
OutputState->Ready = false;
OutputState->PublishedSlot = -1;
OutputState->PublishedSerial = 0;
OutputState->Generation++;

OutputState->Completion.wait(lock, [] {
    return InFlightCount == 0 &&
           PresenterRefCount == 0;
});
```

ここで`PresenterRefCount`が0になるまで無期限に待つ。timeout、cancel、retiring generationの通知はない。

## 4.2 presenter側

presenterは最後にvalidate成功したleaseをmemberとして保持する。

```cpp
RendererOutputLease lastGoodMetalLease;
```

新しいMetal outputを受け取ると:

```cpp
lastGoodMetalLease = std::move(rendererOutputLease);
```

定常状態では少なくとも1 slotの`PresenterRefs`が保持される。

## 4.3 循環

scale変更後:

```text
renderer:
    Ready=false
    PresenterRefCount==0をwait

presenter:
    AcquireOutputLease()
    → Ready=false
    → SoftRenderer::GetOutput()
    → CpuBgra branch

CpuBgra branch:
    lastGoodMetalLeaseを解放しない
```

presenterがleaseを解放する条件:

- Metal以外のrendererが選択された
- emulationが停止した
- ProducerIdが異なるMetalTextureを受け取った
- Generationが退行したMetalTextureを受け取った

しかしscale再構成中は新しいMetalTextureを生成できない。

```text
新MetalTexture生成
    requires ConfigureMetalVisibleOutput完了

ConfigureMetalVisibleOutput完了
    requires lastGoodMetalLease解放

lastGoodMetalLease解放
    currently requires新MetalTexture受信
```

循環が閉じる。

## 4.4 推奨修正

scale／size変更時に既存OutputStateをin-place再構成しない。

```text
1. 新しいMetalOutputStateを作成
2. 新texture ringを割り当て
3. pipeline／scale／sizeを構成
4. Ready=true
5. OutputStateを新stateへswap
6. 旧stateはpresenter lease／command handlerが自然に保持
7. 最後の参照解放時に旧stateを破棄
```

新stateには新しい`ProducerId`を付ける。

擬似コード:

```cpp
auto next = CreateConfiguredOutputState(
    device,
    rendererQueue,
    scale,
    width,
    height);

if (!next)
    return false;

std::shared_ptr<MetalOutputState> previous =
    std::exchange(OutputState, std::move(next));
```

rendererは旧`PresenterRefCount`を待たない。settings変更threadをGPU／UI leaseでblockしない。

## 4.5 受け入れ試験

Metal raster／Metal Computeそれぞれで:

```text
1x → 2x → 4x → 8x → 16x
16x → 8x → 4x → 2x → 1x
```

条件:

- 通常プレイ中
- HUD表示中
- OSD表示中
- pause中
- fast-forward中
- fullscreen中
- Display Capture使用シーン
- savestate load直後
- renderer切替直後

合格条件:

- `SetRenderSettings()`が100ms以上停止しない
- `PresenterRefCount`待ちがない
- ProducerIdが適切に変更
- 旧leaseが自然解放
- stale texture表示なし
- CpuBgra fallbackからMetalへ復帰
- output ring leakなし
- shutdown正常

---

# 5. MEDIUM

## M-01: Generation rollbackを検出後に受理している

現在:

```cpp
if (sameProducer &&
    output.Generation < lastGoodGeneration)
{
    lastGoodMetalLease.ReleaseNow();
    lastGoodProducerId = 0;
    lastGoodGeneration = 0;
}

if (ValidateMetalRendererOutput(...))
{
    lastGoodMetalLease = std::move(rendererOutputLease);
}
```

Generation rollbackを検出しても、tracking値を0へ戻した後、rollbackしたoutput自体を新しいlast-goodとして受理する。

同じproducer内のGenerationは単調増加contractなので、rollbackは拒否する。

```cpp
if (output.ProducerId == lastGoodProducerId &&
    output.Generation < lastGoodGeneration)
{
    RejectOutput("generation rollback");
    rendererOutputLease.ReleaseNow();
    return;
}
```

## M-02: FrameSerial rollbackを検証していない

同じpublished textureを複数回presentするため、FrameSerialの同値は正常である。

同じProducerId／Generationで、受理済みserialより小さい値は異常である。

```cpp
if (sameProducer &&
    sameGeneration &&
    output.FrameSerial < lastGoodFrameSerial)
{
    reject;
}
```

許可:

```text
serial == lastGoodFrameSerial
    → 同じpublished frameの再present

serial > lastGoodFrameSerial
    → 新frame

serial < lastGoodFrameSerial
    → stale／rollback
```

## M-03: capture memory総量は依然大きい

scale 16時:

```text
Capture128   256 MiB
Snapshot128  256 MiB
Capture256   256 MiB
Snapshot256  256 MiB
--------------------
texture合計   1 GiB
```

追加:

```text
CpuUploadScratch      最大64 MiBを保持
persistent ring       最大64 MiB
one-off upload        最大64 MiB／in-flight
```

通常ピーク概算:

```text
約1.125～1.1875 GiB
```

reconfigure中に旧`CaptureState`がcompletion handlerに保持されると、新旧texture arrayが一時共存し、2 GiBを超え得る。

現在のコメントにある:

```text
256@8x = 8 MiB
```

は誤りである。

正しくは:

```text
2048 × 2048 × 4
= 16 MiB
```

推奨:

- CPU stagingはnative 16-bit／native size
- GPU computeでRGB5551 decodeとscale expansion
- CPUのscale² loop撤廃
- 64 MiB scratch撤廃
- one-off巨大buffer撤廃
- capture memory budgetのfeature probe追加

## M-04: 単一`UploadInFlight`では複数writerを表現できない

writer:

- CPU fallback upload
- GPU Display Capture

metadata:

```cpp
bool UploadInFlight;
uint64_t SubmittedSerial;
```

GPU capture dispatchは既存`UploadInFlight`を確認せず上書きする。

CPU upload completionとGPU capture completionが重なると、先に完了した方が`UploadInFlight=false`へ戻し、もう一方がまだin-flightでも新しいwriteを許可し得る。

現在はM4未実装でproduction到達頻度は限定的だが、M4前に修正する。

推奨:

```cpp
bool CpuUploadInFlight;
bool GpuCaptureInFlight;
uint64_t CpuSubmittedSerial;
uint64_t GpuSubmittedSerial;
```

またはwriter token／in-flight countを使用する。

## M-05: GPU capture pendingWritesがscanline単位で重複する

`EncodeMetalDisplayCapture()`はenabled lineごとに`pendingWrites`へ追加する。

同じdestination layerが多数回入るため:

- 不要なvector allocation／iteration
- mid-frame size変更時の「最後のlineが勝つ」暗黙仕様
- layer単位stateとscanline因果の混同

destination layerごとにdeduplicateする。

M4 per-segment化では、layer metadataだけでなくsegment tokenへ昇格する。

## M-06: CI evidenceがない

対象HEADのconnector結果:

```text
workflow_runs: []
combined statuses: []
```

最低限必要:

- macOS ARM64 Metal ON
- macOS Intel Metal ON
- macOS Metal OFF
- macOS FORCE_DISABLE
- Windows
- Linux

---

# 6. LOW

## L-01: `ReleaseNow()`後もOutput metadataが残る

`RendererOutputLease::ReleaseNow()`はcontext／callbackをclearするが、`Output`は残す。

現在は`lastGoodProducerId != 0`をguardにしているため表示には使わないが、安全性を高めるなら`Output = {};`も行う。

## L-02: `LeaseContext`の毎frame heap allocation

`AcquireOutputLease()`は毎回`new LeaseContext`を行う。

60 FPS:

```text
3600 allocation／minute
216000 allocation／hour
```

M8完了後にfixed poolまたはslot tokenへ変更する。

## L-03: `MetalOutputState` destructorのblocking wait

destructorは:

```cpp
Completion.wait(... InFlightCount == 0 &&
                    PresenterRefCount == 0);
```

を行う。

最後の参照がMetal completion callback thread上で落ちた場合に、別completionを同じcallback execution contextで待つ可能性を避けるため、explicit retire／nonblocking destructionへ分離することを推奨する。

---

# 7. 完全Metal化フェーズの最新状態

## M0 診断

**概ね完了**

残り:

- CIでstrict mode
- multi-instance別集計
- memory telemetry

## M1 output contract

**大幅改善、未完了**

残り:

- scale reconfigure deadlock
- generation rollback拒否
- serial rollback拒否
- hot-path allocation
- producer retirement contract

## M2 shared device

**完了**

renderer／presenterは同じ`MTLDevice`を使用。queueは別でlease同期。

## M3 Full-GPU 2D default

**完了。ただし適格フレームのみ**

## M4 Display Capture

**未完了**

capture frameのper-line／per-segment因果をGPUだけで再現していない。

## M5 readback 0

**部分完了**

capture CPU fallbackとon-demand readbackが残る。

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

を保持する。

## M8 presenter texture-only

**部分完了**

output validationは改善したが:

- CpuBgra fallback
- software screen texture
- scale deadlock
- HUD用CPU framebuffer

が残る。

## M9 HUD／OSD

**未実装**

- `QImage`
- `QPainter`
- bottom framebuffer memcpy
- CPU OSD bitmap
- CPU UI texture upload

を使用する。

## M10 macOS初回既定

**未実装**

```text
3D.Renderer = OpenGL
Screen.UseGL = true
```

## M11 shader分離

**未実装**

embedded MSLのまま。

---

# 8. 最優先修正順序

## PR 1: output generation reconfigure deadlock

1. scale／size変更時に既存OutputStateをin-place変更しない。
2. 新OutputStateを構築。
3. 完全構成後にpointer swap。
4. 旧stateはshared ownershipでdrain。
5. old PresenterRefCountをwaitしない。
6. 新ProducerIdを付与。
7. settings threadをblocking waitから解放。

## PR 2: output ordering contract

追加:

```cpp
lastGoodProducerId
lastGoodGeneration
lastGoodFrameSerial
```

拒否:

- generation rollback
- serial rollback
- metadata mismatch
- invalid texture

## PR 3: capture writer state

- CPU writerとGPU writerを分離
- in-flight count／token
- pendingWrites layer deduplicate
- segment serial
- writer ordering test

## PR 4: memory

- native-size staging
- GPU decode／scale
- one-off巨大buffer撤廃
- capture memory budget
- 8x／16x probe

---

# 9. 必須受け入れ試験

## 9.1 Build

```text
macOS ARM64 Metal ON
macOS Intel Metal ON
macOS Metal OFF
macOS FORCE_DISABLE
Windows
Linux
```

## 9.2 scale deadlock

100回連続:

```text
1x→2x→4x→8x→16x→8x→4x→2x→1x
```

同時条件:

- fullscreen
- VSync
- fast-forward
- pause
- HUD
- OSD
- savestate
- Metal raster
- Metal Compute

合格:

- hang 0
- wait >100ms 0
- stale texture 0
- lease leak 0
- ProducerId transition正常

## 9.3 Thread Sanitizer

確認:

- scale変更
- renderer切替
- ROM close
- shutdown
- capture fallback
- GPU遅延
- command failure injection

合格:

```text
CaptureMeta race 0
StagingRing race 0
OutputState race 0
Presenter lease race 0
```

## 9.4 output fault injection

拒否:

- nil texture
- wrong device
- wrong type
- layer 1／3
- width／height／scale 0
- generation／serial／producer 0
- generation rollback
- serial rollback
- size mismatch

## 9.5 memory

scale 1／2／4／8／16で記録:

- capture texture bytes
- snapshot texture bytes
- scratch capacity
- staging ring bytes
- one-off bytes
- old/new state overlap
- process RSS
- Metal allocated size
- peak memory

---

# 10. 修正禁止事項

1. deadlock回避のためlast-good leaseをraw pointer化しない。
2. presenterがsampling中のslotを再利用しない。
3. scale変更時にGUI threadへ同期invokeしない。
4. `waitUntilCompleted`を通常present pathへ追加しない。
5. timeout後にPresenterRefsを強制0へ書き換えない。
6. generation rollbackを新session扱いしない。
7. capture metadata mutexを外さない。
8. UploadInFlight gateを外さない。
9. staging memory問題をone-off allocationだけで隠さない。
10. CPU fallbackを完全Metal完了として扱わない。
11. `SoftRenderer`／OpenGL既存経路を壊さない。
12. melonPrime以外の既存コード変更では必要な`MELONPRIME_DS`／Metal guardを維持する。

---

# 11. 最終チェックリスト

## 今回コミット

- [x] 前回の実改行修正
- [x] MetaMutex
- [x] Dirty state
- [x] UploadInFlight gate
- [x] CPU completion serial
- [x] GPU completion status
- [x] staging ring縮小
- [x] output metadata必須化
- [x] ProducerId
- [ ] scale変更deadlock解消
- [ ] generation rollback拒否
- [ ] serial rollback拒否
- [ ] Metal ON build
- [ ] Metal OFF build
- [ ] CI green
- [ ] TSan
- [ ] 実ROM回帰確認

## 完全Metal化

- [ ] capture frame Full-GPU
- [ ] normal readback 0
- [ ] SoftRenderer継承撤廃
- [ ] Compute RasterReference撤廃
- [ ] presenter CpuBgra撤廃
- [ ] HUD CPU framebuffer撤廃
- [ ] QPainter通常frame撤廃
- [ ] Metal初回既定
- [ ] metallib
- [ ] macOS ARM64／Intel acceptance
- [ ] Windows／Linux回帰なし

---

# 12. 最終評価

最新コミットは、前回再監査で指摘したcapture metadataのthread safetyとstate machineを明確に改善している。

評価できる点:

- mutexによるmetadata snapshot
- Dirty／UploadInFlight分離
- serialをdata change時に更新
- stale completion拒否
- GPU command failure反映
- fail-closed output validation
- ProducerId
- persistent ring memory上限

しかし、last-known-good leaseを長期保持する設計と、同じOutputStateをscale変更時に`PresenterRefCount==0`まで待って再構成する設計は両立しない。

> **前回のBLOCKER／HIGHを多数解消した一方、内部解像度変更で永久待機し得る新しいBLOCKERが導入されている。**

したがって現HEADは受け入れ不可である。

最優先は、output texture generationをin-place再構成からimmutable state swapへ変更すること。これを修正してから、Generation／FrameSerial ordering、capture writer token、memory削減へ進むこと。
